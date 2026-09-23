// The terrain patch: a local physical-metre mesh cut from the globe, and the machine-readable
// refusals that guard it.
//
// The sampling tests run on a hand-built world where the answer is known -- a flat globe
// with no published detail, so every metre of relief is the patch's own micro relief --
// and on a generated phase-16 world, where the patch must BE the height field the planners
// stand their buildings on.
//
// Two reference defects are PINNED: the world refusal names `patch.span` even when it is
// the detail height that the world cannot hold, and the spacing clamp a refusal suggests
// can itself be refused by the vertex budget.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "fwg/grid.hpp"
#include "fwg/patch.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

constexpr double kPi = 3.14159265358979323846;

// A flat globe with no published terrain detail: the legacy path, where the only relief is
// the patch's own. The reference's own fixture is a 1774.4 m world at a height of 10 m.
World flat_world(double radius = 1774.4, std::uint32_t seed = 42, double height = 10.0) {
    World w;
    w.config.seed = seed;
    w.config.size = 9;
    w.effective = w.config;
    w.effective.globe_radius = radius;
    w.layers["height"] = Grid(9, height);
    return w;
}

// Generated once: the phase-16 world every planner ran on.
const World& planned_world() {
    static const World world = [] {
        Request r;
        r.seed = 42;
        r.size = 33;
        r.phase = 16;
        return generate(resolve(r));
    }();
    return world;
}

PatchConfig config(double span, double spacing) {
    PatchConfig cfg;
    cfg.span = span;
    cfg.spacing = spacing;
    return cfg;
}

// The refusal a config or a patch raises, or a failure if it raises none.
PatchRefusal refusal_of(const PatchConfig& cfg, const World* world = nullptr) {
    try {
        if (world != nullptr) {
            generate_patch(*world, cfg);
        } else {
            cfg.validate();
        }
    } catch (const PatchError& error) {
        return error.refusal();
    }
    check::fail("a refusal, and there was none");
}

// The globe-centred position of patch vertex i, rebuilt from its local coordinates.
Vec3 global_of(const Patch& patch, std::size_t i) {
    const double lat = patch.patch_config.latitude * kPi / 180.0;
    const double lon = patch.patch_config.longitude * kPi / 180.0;
    const Vec3 center{std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon)};
    const Vec3 east{-std::sin(lon), 0.0, std::cos(lon)};
    const Vec3 north{-std::sin(lat) * std::cos(lon), std::cos(lat), -std::sin(lat) * std::sin(lon)};
    return center * (patch.radius_m + patch.origin_height_m) + east * patch.local_x[i] + center * patch.local_y[i] +
           north * patch.local_z[i];
}

}  // namespace

TEST(a_patch_config_refuses_each_bad_input_naming_the_field) {
    check::is_true([] {
        PatchConfig{}.validate();
        return true;
    }(), "the default config to be valid");

    // Not a finite number: every field, by name.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    struct Bad {
        const char* field;
        double PatchConfig::*member;
    };
    const Bad fields[] = {{"patch.latitude", &PatchConfig::latitude},       {"patch.longitude", &PatchConfig::longitude},
                          {"patch.span", &PatchConfig::span},               {"patch.spacing", &PatchConfig::spacing},
                          {"patch.detail_height", &PatchConfig::detail_height},
                          {"patch.detail_scale", &PatchConfig::detail_scale}};
    for (const Bad& bad : fields) {
        for (const double value : {nan, inf, -inf}) {
            PatchConfig cfg;
            cfg.*bad.member = value;
            const PatchRefusal r = refusal_of(cfg);
            check::equal(r.kind, std::string_view("wrong_type"), std::string(bad.field) + " non-finite kind");
            check::equal(r.field, std::string(bad.field), "the non-finite field named");
            check::equal(r.code, std::string_view("INVALID_INPUT"), std::string(bad.field) + " code");
            check::equal(r.expected_type, std::string_view("number"), std::string(bad.field) + " expected type");
            check::is_true(r.received.has_value() && !std::isfinite(*r.received), std::string(bad.field) + " received");
            check::equal(r.suggestion, std::string_view("none"), std::string(bad.field) + " no clamp for a non-number");
            check::is_true(r.retry, std::string(bad.field) + " retryable");
        }
    }

    // Out of range: every bound, both sides, with the bound, the units and a value that fits.
    struct Range {
        const char* field;
        double PatchConfig::*member;
        double value;
        double min;
        double max;
        const char* units;
        double clamp;
    };
    const Range ranges[] = {
        {"patch.latitude", &PatchConfig::latitude, 91.0, -90.0, 90.0, "degrees", 90.0},
        {"patch.latitude", &PatchConfig::latitude, -90.5, -90.0, 90.0, "degrees", -90.0},
        {"patch.longitude", &PatchConfig::longitude, 180.25, -180.0, 180.0, "degrees", 180.0},
        {"patch.longitude", &PatchConfig::longitude, -181.0, -180.0, 180.0, "degrees", -180.0},
        {"patch.span", &PatchConfig::span, 3.5, 4.0, 512.0, "m", 4.0},
        {"patch.span", &PatchConfig::span, 1000.0, 4.0, 512.0, "m", 512.0},
        {"patch.spacing", &PatchConfig::spacing, 0.0, 0.1, 8.0, "m", 0.1},
        {"patch.spacing", &PatchConfig::spacing, 9.0, 0.1, 8.0, "m", 8.0},
        {"patch.detail_height", &PatchConfig::detail_height, -1.0, 0.0, 5.0, "m", 0.0},
        {"patch.detail_height", &PatchConfig::detail_height, 5.5, 0.0, 5.0, "m", 5.0},
        {"patch.detail_scale", &PatchConfig::detail_scale, 0.25, 0.5, 100.0, "m", 0.5},
        {"patch.detail_scale", &PatchConfig::detail_scale, 101.0, 0.5, 100.0, "m", 100.0},
    };
    for (const Range& bad : ranges) {
        PatchConfig cfg;
        // Keep the vertex budget out of the way of a span or spacing bound.
        cfg.spacing = 8.0;
        cfg.*bad.member = bad.value;
        const std::string what = std::string(bad.field) + " = " + std::to_string(bad.value);
        const PatchRefusal r = refusal_of(cfg);
        check::equal(r.kind, std::string_view("out_of_range"), what + " kind");
        check::equal(r.field, std::string(bad.field), what + " names its field");
        check::is_true(r.received == bad.value, what + " carries the value received");
        check::is_true(r.min == bad.min && r.max == bad.max, what + " carries its bounds");
        check::equal(r.units, std::string_view(bad.units), what + " units");
        check::equal(r.suggestion, std::string_view("clamp"), what + " suggests a clamp");
        check::is_true(r.suggestion_value == bad.clamp, what + " suggests the nearest legal value");
        check::is_true(r.message.find(bad.field) == 0, what + " message leads with the field");
        // And the suggestion is accepted -- except where following it trips the vertex
        // budget, pinned below.
        PatchConfig fixed = cfg;
        fixed.*bad.member = *r.suggestion_value;
        if (!(bad.member == &PatchConfig::spacing && bad.clamp == 0.1)) fixed.validate();
    }

    // PINNED: a clamp is per field. Spacing 0 clamps to 0.1, and 0.1 at the default 64 m
    // span is 640 segments, which the vertex budget then refuses. The reference suggests
    // exactly this; a caller that trusts the suggestion is refused twice.
    PatchConfig tight;
    tight.spacing = 0.0;
    const PatchRefusal first = refusal_of(tight);
    tight.spacing = *first.suggestion_value;
    check::equal(refusal_of(tight).kind, std::string_view("cross_field"),
                 "the spacing clamp at the default span to be refused by the vertex budget");

    // Catchable where every refused config already is.
    bool caught = false;
    try {
        config(1000.0, 8.0).validate();
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    check::is_true(caught, "a PatchError to be a std::invalid_argument");
}

TEST(the_first_refusal_is_the_first_the_reference_checks) {
    // Finiteness of every field comes before any range, and the ranges run in declaration
    // order with the vertex budget between spacing and detail_height.
    PatchConfig both;
    both.latitude = 91.0;
    both.detail_scale = std::numeric_limits<double>::quiet_NaN();
    check::equal(refusal_of(both).field, std::string("patch.detail_scale"), "a non-number before any range");

    PatchConfig ranges;
    ranges.latitude = 91.0;
    ranges.span = 1000.0;
    check::equal(refusal_of(ranges).field, std::string("patch.latitude"), "latitude before span");

    PatchConfig budget = config(512.0, 0.1);
    budget.detail_height = 6.0;
    check::equal(refusal_of(budget).kind, std::string_view("cross_field"), "the vertex budget before detail_height");
}

TEST(the_vertex_budget_is_refused_naming_both_fields_and_no_single_one) {
    const PatchRefusal r = refusal_of(config(512.0, 0.1));
    check::equal(r.kind, std::string_view("cross_field"), "kind");
    check::is_true(r.field.empty(), "no single field to own the refusal");
    check::is_true(!r.received.has_value(), "no single value received");
    check::is_true(r.fields == std::vector<std::string_view>{"span", "spacing"}, "both fields named");
    check::is_true(r.message.find("5121") != std::string::npos, "the message to say how many vertices were asked for");
    check::equal(r.suggestion, std::string_view("none"), "no single clamp for two fields");

    // The budget is on SEGMENTS, rounded up: 256 fit, and a hair over asks for 257.
    check::equal(refusal_of(config(256.5, 1.0)).kind, std::string_view("cross_field"), "257 segments");
    config(256.0, 1.0).validate();
    config(25.6, 0.1).validate();
}

TEST(a_patch_has_one_vertex_more_than_segments_per_axis) {
    const World w = flat_world();
    struct Case {
        double span, spacing;
        int size;
        double step;
    };
    // ceil(span / spacing) + 1: a spacing that does not divide the span is TIGHTENED so the
    // patch still spans exactly `span`.
    const Case cases[] = {{32.0, 1.0, 33, 1.0}, {64.0, 0.5, 129, 0.5}, {10.0, 3.0, 5, 2.5}, {512.0, 2.0, 257, 2.0},
                          {4.0, 8.0, 2, 4.0}};
    for (const Case& c : cases) {
        PatchConfig cfg = config(c.span, c.spacing);
        cfg.detail_height = 0.0;
        const Patch p = generate_patch(w, cfg);
        const std::string what = std::to_string(c.span) + " m at " + std::to_string(c.spacing) + " m";
        check::equal(p.size, c.size, what + " vertices per axis");
        check::close(p.spacing_m, c.step, 1e-12, what + " actual step");
        const auto vertices = static_cast<std::size_t>(c.size) * static_cast<std::size_t>(c.size);
        for (const std::vector<double>* grid : {&p.height, &p.detail, &p.local_x, &p.local_y, &p.local_z}) {
            check::equal(grid->size(), vertices, what + " one value per vertex");
        }
        const auto cells = static_cast<std::size_t>(c.size - 1) * static_cast<std::size_t>(c.size - 1);
        check::equal(p.triangle_indices.size(), cells * 6, what + " two triangles per cell");
        for (const std::uint32_t i : p.triangle_indices) check::is_true(i < vertices, what + " index in range");
        // The patch spans exactly `span` ON THE REFERENCE SPHERE: its south-west and
        // north-east corners are span / sqrt(2) along the surface from the centre, on the
        // right bearings.
        const Vec3 centre{1.0, 0.0, 0.0};  // latitude 0, longitude 0
        const Vec3 south_west = unit(global_of(p, 0));
        const Vec3 north_east = unit(global_of(p, vertices - 1));
        check::close(great_circle_m(w.effective.globe_radius, centre, south_west), c.span / std::sqrt(2.0), 1e-6,
                     what + " south-west corner distance");
        check::close(great_circle_m(w.effective.globe_radius, centre, north_east), c.span / std::sqrt(2.0), 1e-6,
                     what + " north-east corner distance");
        check::is_true(south_west.z < 0.0 && south_west.y < 0.0, what + " first vertex to the south-west");
        check::is_true(north_east.z > 0.0 && north_east.y > 0.0, what + " last vertex to the north-east");
    }
}

TEST(the_patch_origin_is_on_the_ground_and_the_sphere_falls_away_from_it) {
    PatchConfig cfg = config(32.0, 1.0);
    cfg.detail_height = 0.0;
    const Patch p = generate_patch(flat_world(), cfg);
    const std::size_t centre = 16 * 33 + 16;
    check::close(p.height[centre], 10.0, 1e-12, "the centre height");
    check::close(p.origin_height_m, 10.0, 1e-12, "the origin height");
    check::close(p.local_x[centre], 0.0, 1e-9, "centre x");
    check::close(p.local_y[centre], 0.0, 1e-9, "centre y");
    check::close(p.local_z[centre], 0.0, 1e-9, "centre z");
    // Curvature: 16 m out along the reference sphere, flat ground 10 m above it has fallen
    // (R + 10)(1 - cos(16 / R)) below the tangent plane -- 7 cm on this 1774 m globe.
    const double drop = (1774.4 + 10.0) * (1.0 - std::cos(16.0 / 1774.4));
    check::close(p.local_y[16], -drop, 1e-9, "the south edge below the origin by the sphere's fall");
    check::is_true(p.local_y[16] < p.local_y[centre], "the ground to fall away from the origin");
    check::is_true(std::all_of(p.detail.begin(), p.detail.end(), [](double d) { return d == 0.0; }),
                   "no relief at a detail height of zero");
    check::equal(p.patch_version, 1, "a world without published detail to take the legacy path");
    check::is_true(!p.terrain_detail.has_value(), "no published detail to carry");
}

TEST(every_triangle_faces_out_of_the_ground) {
    const Patch p = generate_patch(flat_world(), config(16.0, 1.0));
    for (std::size_t t = 0; t + 2 < p.triangle_indices.size(); t += 3) {
        const std::uint32_t a = p.triangle_indices[t], b = p.triangle_indices[t + 1], c = p.triangle_indices[t + 2];
        const Vec3 va{p.local_x[a], p.local_y[a], p.local_z[a]};
        const Vec3 vb{p.local_x[b], p.local_y[b], p.local_z[b]};
        const Vec3 vc{p.local_x[c], p.local_y[c], p.local_z[c]};
        // (x east, y up, z north) is right-handed, so the component-wise cross product is
        // the real normal and its y is how far it points up.
        const Vec3 normal = cross(vb - va, vc - va);
        check::is_true(normal.y > 0.0, "triangle " + std::to_string(t / 3) + " to face up");
    }
}

TEST(micro_relief_stays_within_detail_height_and_scales_with_it) {
    const World w = flat_world();
    PatchConfig cfg = config(64.0, 0.5);
    cfg.detail_height = 0.4;
    const Patch low = generate_patch(w, cfg);
    cfg.detail_height = 0.8;
    const Patch high = generate_patch(w, cfg);
    check::is_true(low.detail_resolved, "an 8 m scale over a 0.5 m step to be resolved");

    double peak = 0.0;
    for (std::size_t i = 0; i < low.detail.size(); ++i) {
        peak = std::max(peak, std::abs(low.detail[i]));
        check::is_true(std::abs(low.detail[i]) <= 0.4, "micro relief never past its detail height");
        // Linear in the knob, exactly: doubling a double is exact.
        check::is_true(high.detail[i] == 2.0 * low.detail[i], "double the detail height, double the relief");
        check::close(low.height[i], 10.0 + low.detail[i], 1e-12, "height = the flat base + the relief");
    }
    // Real relief, not a rounding error: most of the amplitude is used somewhere.
    check::is_true(peak > 0.4 * 0.4, "the relief to reach a real share of its amplitude, got " + std::to_string(peak));

    // Finer than two samples, the detail is not resolved and is dropped rather than aliased.
    PatchConfig coarse = config(16.0, 2.0);
    coarse.detail_scale = 1.0;
    const Patch unresolved = generate_patch(w, coarse);
    check::is_true(!unresolved.detail_resolved, "a 1 m scale over a 2 m step to be unresolved");
    check::is_true(std::all_of(unresolved.detail.begin(), unresolved.detail.end(), [](double d) { return d == 0.0; }),
                   "unresolved relief to be omitted");
}

TEST(micro_relief_belongs_to_the_ground_not_to_the_patch) {
    const World w = flat_world();
    // Nested resolutions sample the same relief at the vertices they share.
    const Patch coarse = generate_patch(w, config(32.0, 1.0));
    const Patch fine = generate_patch(w, config(32.0, 0.5));
    for (int z = 0; z < coarse.size; ++z) {
        for (int x = 0; x < coarse.size; ++x) {
            const double a = coarse.height[static_cast<std::size_t>(z * coarse.size + x)];
            const double b = fine.height[static_cast<std::size_t>(2 * z * fine.size + 2 * x)];
            check::close(a, b, 1e-9, "a shared vertex to have one height");
        }
    }
    // And the relief is the world's: another seed is other ground.
    const Patch other = generate_patch(flat_world(1774.4, 43), config(32.0, 1.0));
    check::is_true(other.detail != coarse.detail, "a different world seed to give different relief");
    check::is_true(other.detail_seed != coarse.detail_seed, "and a different detail seed");
}

TEST(the_poles_and_the_date_line_are_one_place) {
    const World w = flat_world();
    PatchConfig west = config(16.0, 0.5);
    west.latitude = 90.0;
    west.longitude = -180.0;
    PatchConfig east = west;
    east.longitude = 180.0;
    const Patch a = generate_patch(w, west);
    const Patch b = generate_patch(w, east);
    for (std::size_t i = 0; i < a.height.size(); ++i) {
        check::close(a.height[i], b.height[i], 1e-9, "the two sides of the date line at the pole");
        check::is_true(std::isfinite(a.local_x[i]) && std::isfinite(a.local_y[i]) && std::isfinite(a.local_z[i]),
                       "finite coordinates at the pole");
    }
}

TEST(on_a_world_with_published_detail_the_patch_is_the_height_field) {
    const World& w = planned_world();
    check::is_true(w.has_terrain_detail, "a phase-16 world to publish its detail");
    const HeightField field(w);
    // The widest city whose ground has real relief under it: a flat window would pass the
    // vertex comparison below trivially. Which city that is depends on what the ages left
    // standing, so it is searched for rather than assumed to be the widest one.
    std::vector<const CityPlan*> cities;
    for (const CityPlan& p : w.city_plans.cities) cities.push_back(&p);
    std::stable_sort(cities.begin(), cities.end(),
                     [](const CityPlan* a, const CityPlan* b) { return a->half_m > b->half_m; });
    const auto config_at = [](const CityPlan& p) {
        PatchConfig cfg;
        cfg.latitude = p.reference_frame.latitude_degrees;
        cfg.longitude = p.reference_frame.longitude_degrees;
        cfg.span = std::min(2.0 * p.half_m, kPatchSpanMaxM);
        cfg.spacing = p.terrain.surface_step_m;
        return cfg;
    };
    const auto relief_of = [](const Patch& p) {
        double most = 0.0;
        for (const double d : p.detail) most = std::max(most, std::abs(d));
        return most;
    };
    const CityPlan* city = nullptr;
    for (const CityPlan* candidate : cities) {
        if (relief_of(generate_patch(w, config_at(*candidate))) > 0.5) {
            city = candidate;
            break;
        }
    }
    check::is_true(city != nullptr, "some city with real relief under it, or the comparison is weak");

    const PatchConfig cfg = config_at(*city);
    const Patch patch = generate_patch(w, cfg);
    check::equal(patch.patch_version, 2, "the canonical patch version");
    check::is_true(patch.detail_resolved, "canonical detail to count as resolved");
    check::is_true(patch.terrain_detail.has_value() && patch.terrain_detail->seed == w.terrain_detail.seed,
                   "the world's detail definition to be carried");
    check::is_true(std::find(patch.warnings.begin(), patch.warnings.end(),
                             std::string_view("Canonical world heights; legacy patch detail controls are ignored. "
                                              "Sample spacing changes mesh fidelity, not terrain.")) != patch.warnings.end(),
                   "the patch to say its detail controls are ignored");

    // Vertex for vertex, the height field at the vertex's own direction.
    for (std::size_t i = 0; i < patch.height.size(); ++i) {
        const Vec3 global = global_of(patch, i);
        check::close(patch.height[i], field.height(unit(global)), 1e-6, "the patch to be the height field");
    }

    // The legacy controls change nothing.
    PatchConfig louder = cfg;
    louder.detail_height = 5.0;
    louder.detail_scale = 1.0;
    check::is_true(generate_patch(w, louder).height == patch.height, "detail_height and detail_scale to be ignored");

    // And it is the ground the city was planned on: the plan's own corner-sampled surface,
    // stored to 0.1 mm. Measured worst case over every city of this world is 14 mm, at the
    // window's corner, where the patch's equidistant grid and the planner's gnomonic one
    // have drifted 16 mm apart.
    const int offset = static_cast<int>(std::lround((2.0 * city->half_m - cfg.span) / 2.0 / cfg.spacing));
    const int surface = city->terrain.surface_size;
    double worst = 0.0;
    for (int z = 0; z < patch.size; ++z) {
        for (int x = 0; x < patch.size; ++x) {
            const double planned = city->terrain.surface_heights_m[static_cast<std::size_t>((z + offset) * surface + x + offset)];
            worst = std::max(worst, std::abs(planned - patch.height[static_cast<std::size_t>(z * patch.size + x)]));
        }
    }
    check::between(worst, 0.0, 0.03, "the patch to agree with the city's planned surface");
    const std::size_t centre = static_cast<std::size_t>((patch.size / 2) * patch.size + patch.size / 2);
    const double planned_centre =
        city->terrain.surface_heights_m[static_cast<std::size_t>((patch.size / 2 + offset) * surface + patch.size / 2 + offset)];
    check::close(patch.height[centre], planned_centre, 6e-5, "the centres to agree to the surface's rounding");
}

TEST(a_world_too_small_for_the_patch_is_refused_and_the_span_is_blamed) {
    // A 300 m globe holds a span of 150 m and relief of 3 m.
    const World small = flat_world(300.0);
    PatchConfig wide = config(200.0, 1.0);
    const PatchRefusal span = refusal_of(wide, &small);
    check::equal(span.kind, std::string_view("refused_by_world"), "an over-wide span");
    check::equal(span.field, std::string("patch.span"), "the span named");
    check::is_true(span.received == 200.0, "the span received");
    check::is_true(span.refused_by_world, "the ground, not the value, refused it");
    check::is_true(span.message.find("150 m") != std::string::npos, "the message to give the world's bound");

    // PINNED DEFECT: detail over a hundredth of the radius is refused under the SPAN's name
    // with the span's value, although the span is fine. The reference does exactly this.
    PatchConfig rough = config(100.0, 1.0);
    rough.detail_height = 4.0;
    const PatchRefusal detail = refusal_of(rough, &small);
    check::equal(detail.kind, std::string_view("refused_by_world"), "an over-rough detail");
    check::equal(detail.field, std::string("patch.span"), "the SPAN named for a detail-height refusal (reference defect)");
    check::is_true(detail.received == 100.0, "the span's value carried, not the detail height's");

    // The control: the same request on a world large enough is not refused.
    generate_patch(flat_world(1774.4), rough);
}

TEST(a_patch_request_regenerates_its_world_and_refuses_one_with_no_elevation) {
    Request world;
    world.seed = 42;
    world.size = 17;

    // Phase 1 has a height layer in this port -- all zeros -- so it is the request's phase
    // gate, not the missing-block refusal, that keeps a patch off a world with no relief.
    world.phase = 1;
    bool refused = false;
    try {
        patch_request(world, PatchConfig{});
    } catch (const PatchError& error) {
        refused = true;
        const PatchRefusal& r = error.refusal();
        check::equal(r.kind, std::string_view("cross_field"), "the phase gate kind");
        check::is_true(r.field.empty(), "no single field owns the gate");
        check::is_true(r.fields == std::vector<std::string_view>{"shape", "size", "phase"}, "the gate's fields");
        check::is_true(r.message.find("phase 1") != std::string::npos, "the message to say which phase was asked");
    }
    check::is_true(refused, "a phase-1 world to be refused");

    // The world request is judged before the patch: both bad, the world's refusal wins,
    // and it is the config's own std::invalid_argument, not a patch refusal.
    Request even = world;
    even.size = 18;
    PatchConfig bad;
    bad.span = std::numeric_limits<double>::quiet_NaN();
    bool world_first = false;
    try {
        patch_request(even, bad);
    } catch (const PatchError&) {
        world_first = false;
    } catch (const std::invalid_argument&) {
        world_first = true;
    }
    check::is_true(world_first, "an invalid world request to be refused before the patch config");

    // And the patch config before the phase gate.
    check::equal([&] {
        try {
            patch_request(world, bad);
        } catch (const PatchError& error) {
            return error.refusal().field;
        }
        return std::string();
    }(), std::string("patch.span"), "the patch config to be judged before the phase gate");

    // Past the gate, it is exactly the patch of the world the request describes.
    world.phase = 2;
    const PatchConfig cfg = config(32.0, 1.0);
    const Patch requested = patch_request(world, cfg);
    const Patch direct = generate_patch(generate(resolve(world)), cfg);
    check::is_true(requested.height == direct.height, "the requested patch to be the regenerated world's");
}

TEST(a_world_with_no_ground_yet_is_refused_as_a_missing_block) {
    World bare;
    bare.effective.globe_radius = 1774.4;
    const PatchRefusal r = refusal_of(config(16.0, 1.0), &bare);
    check::equal(r.kind, std::string_view("missing_block"), "kind");
    check::equal(r.field, std::string("world.layers.height"), "the missing block named");
    check::equal(r.requires_block, std::string_view("layers.height"), "the block required");
    check::is_true(!r.retry, "no change to the request can fix the world");
}
