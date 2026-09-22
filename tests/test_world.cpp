// Invariants of a generated world.
//
// There is no reference implementation to diff against -- the port is a clean
// rewrite and its worlds are its own -- so these tests are the acceptance
// mechanism. That puts two obligations on them:
//
//   * assert PROPERTIES, not pinned numbers. A pinned histogram passes for the
//     wrong reason the moment the model is tuned, and then gets updated to match
//     whatever came out, which is not a test.
//   * assert that each metric MOVES with the knob that should move it. A number
//     that ignores its own control is the measurement failing, not the model.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>

#include <limits>

#include "fwg/json.hpp"
#include "fwg/measure.hpp"
#include "fwg/scale.hpp"
#include "fwg/serialize.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

Request small_request(int size = 33, int phase = 5) {
    Request request;
    request.seed = 42;
    request.size = size;
    request.phase = phase;
    return request;
}

World build(int size = 33, int phase = 5) { return generate(resolve(small_request(size, phase))); }

std::string to_json(const World& world) {
    std::ostringstream out;
    write_world_json(out, world, false);
    return out.str();
}

}  // namespace

// --- determinism ---------------------------------------------------------

TEST(the_same_request_produces_the_same_world) {
    // Deliberately NOT at size 17. At that raster one octave of five resolves, so a
    // determinism proof there is a proof about a world with almost no surface detail
    // in it -- it would pass against a generator whose entire noise ladder was dead.
    // The non-vacuity assertion below is the point, and it is the same guard the
    // Python gate used for the same reason.
    const World a = build(129);
    const World b = build(129);
    check::is_true(a.resolved_octaves >= 3,
                   "the proof to run on a world with terrain in it (octaves=" +
                       std::to_string(a.resolved_octaves) + ")");
    check::is_true(a.layers.count("noise") == 1 && a.layers.count("catchment") == 1,
                   "the proof to cover the noise and erosion layers");
    check::equal(to_json(a).size(), to_json(b).size(), "document size");
    check::is_true(to_json(a) == to_json(b), "two generations of one seed to be byte-identical");
}

TEST(a_different_seed_produces_a_different_world) {
    Request other = small_request(17);
    other.seed = 43;
    const World a = build(17);
    const World b = generate(resolve(other));
    check::is_true(to_json(a) != to_json(b), "a different seed to change the world");
}

TEST(a_layout_variation_moves_the_plates_and_leaves_the_seed_alone) {
    Request varied = small_request(17);
    varied.layout_variation = 1;
    const World base = build(17);
    const World other = generate(resolve(varied));
    check::is_true(base.layout_seed != other.layout_seed, "the layout seed to move");
    check::equal(base.config.seed, other.config.seed, "the master seed to be unchanged");
}

// --- grid coherence ------------------------------------------------------

TEST(every_layer_closes_its_seam_and_collapses_its_poles) {
    const World world = build(17);
    check::is_true(!world.layers.empty(), "a world to have layers");
    for (const auto& [name, layer] : world.layers) {
        const int n = layer.size();
        check::equal(n, world.config.size, name + " grid size");
        for (int z = 0; z < n; ++z) {
            check::equal(layer.at(n - 1, z), layer.at(0, z), name + " seam at row " + std::to_string(z));
        }
        for (int x = 1; x < n; ++x) {
            check::equal(layer.at(x, 0), layer.at(0, 0), name + " north pole at column " + std::to_string(x));
            check::equal(layer.at(x, n - 1), layer.at(0, n - 1), name + " south pole at column " + std::to_string(x));
        }
    }
}

TEST(every_layer_is_finite) {
    const World world = build(17);
    // Floor first: the loop below is vacuously true on an empty layer map.
    check::is_true(world.layers.size() >= 15,
                   "a phase-5 world to carry its layers (" + std::to_string(world.layers.size()) + ")");
    for (const auto& [name, layer] : world.layers) {
        for (const double value : layer.cells()) {
            check::is_true(std::isfinite(value), name + " to hold only finite values");
        }
    }
}

// --- stage ladder --------------------------------------------------------

TEST(a_shorter_run_agrees_with_a_longer_one_at_the_shared_stage) {
    // Stage 3 adds surface noise to `height`; it must not rewrite `structure`.
    // A stage that silently revises an earlier stage's output makes every
    // phase-limited build a different world from a prefix of the full one.
    const World two = build(17, 2);
    const World three = build(17, 3);
    const Grid& a = two.layers.at("structure");
    const Grid& b = three.layers.at("structure");
    for (int z = 0; z < a.size(); ++z) {
        for (int x = 0; x < a.size(); ++x) {
            check::equal(a.at(x, z), b.at(x, z), "structure at (" + std::to_string(x) + "," + std::to_string(z) + ")");
        }
    }
    check::equal(two.layers.count("noise"), std::size_t{0}, "phase 2 to have no noise layer");
    check::equal(three.layers.count("noise"), std::size_t{1}, "phase 3 to have a noise layer");
}

TEST(each_stage_reports_what_it_actually_ran) {
    // Keyed on the constant, not on a number written here: the ladder advances, and a
    // hardcoded cap turns every advance into a test failure that says nothing.
    for (int phase = 1; phase <= kHighestImplementedStage; ++phase) {
        const World world = build(17, phase);
        check::equal(world.completed_stage, phase, "completed_stage at phase " + std::to_string(phase));
    }
    // Asking for more than is ported runs what exists and says so, rather than
    // claiming a stage nothing implemented.
    const World beyond = build(17, 16);
    check::equal(beyond.completed_stage, kHighestImplementedStage,
                 "completed_stage beyond the ported ladder");
    const bool warned = std::any_of(beyond.warnings.begin(), beyond.warnings.end(),
                                    [](const std::string& w) {
                                        return w.find("not implemented") != std::string::npos;
                                    });
    check::is_true(warned, "a world beyond the ladder to say so in its warnings");
}

TEST(phase_one_has_plates_but_no_surface) {
    const World world = build(17, 1);
    check::equal(world.layers.count("plates"), std::size_t{1}, "plates layer");
    check::equal(world.layers.count("structure"), std::size_t{0}, "no structure at phase 1");
    check::equal(world.layers.count("land"), std::size_t{0}, "no land mask at phase 1");
}

// --- plates --------------------------------------------------------------

TEST(plates_are_distributed_without_crowding) {
    for (const int count : {6, 12, 20}) {
        Request request = small_request(9, 1);
        request.plate_count = count;
        const World world = generate(resolve(request));
        check::equal(static_cast<int>(world.plates.size()), count, "plate count");

        const double cutoff = std::cos(0.55 * std::sqrt(4.0 * std::acos(-1.0) / count));
        for (std::size_t i = 0; i < world.plates.size(); ++i) {
            check::close(std::sqrt(dot(world.plates[i].center, world.plates[i].center)), 1.0, 1e-12,
                         "plate centre to be a unit vector");
            for (std::size_t j = i + 1; j < world.plates.size(); ++j) {
                check::is_true(dot(world.plates[i].center, world.plates[j].center) <= cutoff,
                               "plates " + std::to_string(i) + " and " + std::to_string(j) + " to keep their distance");
            }
        }
    }
}

TEST(every_plate_owns_some_of_the_world) {
    const World world = build(33, 1);
    std::vector<int> owned(world.plates.size(), 0);
    const Grid& layer = world.layers.at("plates");
    for (int z = 1; z < layer.size() - 1; ++z) {
        for (int x = 0; x < layer.size() - 1; ++x) {
            const auto id = static_cast<std::size_t>(layer.at(x, z));
            check::is_true(id < owned.size(), "plate id in range");
            ++owned[id];
        }
    }
    for (std::size_t i = 0; i < owned.size(); ++i) {
        check::is_true(owned[i] > 0, "plate " + std::to_string(i) + " to own at least one cell");
    }
}

// --- the surface ---------------------------------------------------------

TEST(land_fraction_answers_to_sea_level) {
    // The control that must move the metric. If this ever passes with the two
    // fractions equal, the measurement is broken, not the model.
    Request low = small_request(33);
    low.sea_level = -400.0;
    Request high = small_request(33);
    high.sea_level = 400.0;

    const double base = build(33).area.land_fraction;
    const double flooded = generate(resolve(high)).area.land_fraction;
    const double drained = generate(resolve(low)).area.land_fraction;

    check::is_true(drained > base, "a lower sea level to expose more land");
    check::is_true(flooded < base, "a higher sea level to drown more land");
}

TEST(the_default_world_is_neither_all_land_nor_all_ocean) {
    // Deliberately a wide band, not a pinned figure: the point is to catch a world
    // that has saturated -- every cell land, or none -- which is what a broken
    // hypsometric profile or a runaway relief budget produces.
    const World world = build(33);
    check::between(world.area.land_fraction, 0.05, 0.75, "land fraction of the default world");

    // NOT `land_km2 + submerged_km2 == total_km2`. That was here, and it is true by
    // construction -- submerged is computed as (total - land), so the assertion holds
    // for any land value whatsoever, including a broken one. The real control is that
    // the integral agrees with an independent count of the cells it integrated.
    const Grid& height = world.layers.at("height");
    const int n = height.size();
    double recomputed = 0.0;
    for (int z = 0; z < n; ++z) {
        const double weight = band_wedge_area(z, n, world.effective.globe_radius);
        for (int x = 0; x < n - 1; ++x) {
            if (height.at(x, z) > world.effective.sea_level) recomputed += weight;
        }
    }
    check::close(recomputed / 1e6, world.area.land_km2, world.area.land_km2 * 1e-9,
                 "the reported land area to match a direct recount");
    check::close(world.area.total_km2, 4.0 * std::acos(-1.0) * world.effective.globe_radius *
                                           world.effective.globe_radius / 1e6,
                 1e-6, "total area to be the sphere's");
}

TEST(the_land_mask_agrees_with_the_height_field) {
    const World world = build(17);
    const Grid& height = world.layers.at("height");
    const Grid& land = world.layers.at("land");
    for (int z = 0; z < height.size(); ++z) {
        for (int x = 0; x < height.size(); ++x) {
            const bool above = height.at(x, z) > world.effective.sea_level;
            check::equal(land.at(x, z), above ? 1.0 : 0.0, "land mask at (" + std::to_string(x) + "," + std::to_string(z) + ")");
        }
    }
}

TEST(relief_stays_inside_the_bound_the_model_implies) {
    // NOT the config's relief budget. That bound -- 5*tectonic_relief + 2*amplitude
    // -- constrains the CONFIGURED terms so the surface cannot fold through the
    // origin; it says nothing about realised heights, because `orogeny` multiplies
    // collision uplift on top of it. An earlier version of this test asserted the
    // config budget, passed at size 33, and would have failed at 129: it was green
    // because the raster resolved almost no terrain, not because the claim held.
    //
    // The real bound, term by term:
    //   continental_profile           in [-1.8, 0.3]
    //   uplift                        in [0, 2*orogeny]      (modulation <= 1)
    //   trench + rift + ridge + volc  in [-1.9, 1.15]
    //   blended interaction           <= max |interaction|   (weighted mean)
    //   surface noise                 <= amplitude * 1.45    (sum 0.5^k <= 2)
    // and erosion only moves material between existing cells.
    for (const int size : {33, 129}) {
        const World world = build(size);
        const Config& e = world.effective;
        const double bound = e.tectonic_relief * (1.8 + 2.0 * e.orogeny + 1.15) + e.amplitude * 1.45;
        for (const double value : world.layers.at("height").cells()) {
            check::is_true(std::abs(value) < bound,
                           "height " + std::to_string(value) + " inside " + std::to_string(bound) +
                               " at size " + std::to_string(size));
        }
    }
}

TEST(erosion_never_builds_above_the_surface_it_started_from) {
    // The tight version of the bound above, and the one that would catch a runaway
    // deposition term: the erosion pass caps fill at the pre-erosion high point, so
    // no cell may end higher than the highest cell it began with.
    const World world = build(65);
    const auto& eroded = world.layers.at("height").cells();
    const auto& surface = world.layers.at("surface").cells();
    const double ceiling = *std::max_element(surface.begin(), surface.end());
    const double floor_before = *std::min_element(surface.begin(), surface.end());
    const double span = ceiling - floor_before;
    for (const double value : eroded) {
        check::is_true(value <= ceiling + 1e-6, "no cell above the pre-erosion ceiling");
        check::is_true(value >= floor_before - span, "no cell cut below the surface's own span");
    }
}

TEST(mountains_answer_to_orogeny) {
    Request quiet = small_request(33);
    quiet.orogeny = 1.0;
    Request loud = small_request(33);
    loud.orogeny = 20.0;

    const auto peak = [](const World& world) {
        const Grid& height = world.layers.at("height");
        return *std::max_element(height.cells().begin(), height.cells().end());
    };
    check::is_true(peak(generate(resolve(loud))) > peak(generate(resolve(quiet))),
                   "more orogeny to raise the highest peak");
}

// --- erosion -------------------------------------------------------------

TEST(erosion_conserves_material) {
    const World world = build(33);
    check::is_true(world.has_sediment_budget, "phase 5 to report a sediment budget");
    const double moved = world.sediment.eroded_m3;
    check::is_true(moved > 0.0, "erosion to move some material");
    // Cut = deposited + still in transit. The residual is float noise, not physics.
    check::close(world.sediment.balance_error_m3 / moved, 0.0, 1e-9, "relative balance error");
}

TEST(no_erosion_passes_leaves_the_surface_alone) {
    Request request = small_request(17);
    request.erosion_passes = 0;
    const World world = generate(resolve(request));
    const Grid& height = world.layers.at("height");
    const Grid& surface = world.layers.at("surface");
    for (int z = 0; z < height.size(); ++z) {
        for (int x = 0; x < height.size(); ++x) {
            check::equal(height.at(x, z), surface.at(x, z), "height to equal surface with no passes");
        }
    }
    check::equal(world.sediment.eroded_m3, 0.0, "nothing eroded");
}

TEST(erosion_answers_to_its_strength) {
    Request gentle = small_request(33);
    gentle.erosion_strength = 0.05;
    Request fierce = small_request(33);
    fierce.erosion_strength = 1.0;
    const double a = generate(resolve(gentle)).sediment.eroded_m3;
    const double b = generate(resolve(fierce)).sediment.eroded_m3;
    check::is_true(b > a, "more strength to cut more material (" + std::to_string(a) + " vs " + std::to_string(b) + ")");
}

TEST(erosion_only_cuts_and_deposition_only_fills) {
    const World world = build(17);
    for (const double value : world.layers.at("erosion").cells()) {
        check::is_true(value >= 0.0, "erosion to be non-negative");
    }
    for (const double value : world.layers.at("deposition").cells()) {
        check::is_true(value >= 0.0, "deposition to be non-negative");
    }
}

TEST(catchment_is_at_least_a_cell_and_at_most_the_world) {
    const World world = build(17);
    const Grid& catchment = world.layers.at("catchment");
    double largest = 0.0;
    for (const double value : catchment.cells()) {
        check::is_true(value > 0.0, "every cell to drain at least itself");
        largest = std::max(largest, value);
    }
    const double sphere_m2 = world.area.total_km2 * 1e6;
    check::is_true(largest <= sphere_m2 * 1.000001, "no catchment larger than the planet");
}

// --- measurement ---------------------------------------------------------

TEST(a_flat_world_has_no_slope_and_no_relief) {
    const int n = 17;
    Grid flat(n, 250.0);
    const Relief relief = measure_globe(flat, 100000.0, 5000.0);
    for (const double value : relief.slope.cells()) check::close(value, 0.0, 1e-9, "slope of a flat field");
    for (const double value : relief.tpi.cells()) check::close(value, 0.0, 1e-9, "tpi of a flat field");
}

TEST(slope_is_reported_in_degrees) {
    const World world = build(17);
    for (const double value : world.layers.at("slope").cells()) {
        check::between(value, 0.0, 90.0, "slope in degrees");
    }
}

// --- configuration -------------------------------------------------------

TEST(world_size_presets_resolve_to_their_documented_widths) {
    const std::pair<WorldSize, double> presets[] = {
        {WorldSize::Small, 200000.0}, {WorldSize::Medium, 400000.0}, {WorldSize::Large, 600000.0}};
    for (const auto& [size, circumference] : presets) {
        Request request = small_request(9, 1);
        request.world_size = size;
        const Config cfg = resolve(request);
        const double physical = 2.0 * std::acos(-1.0) * cfg.globe_radius * cfg.world_scale;
        check::close(physical, circumference, 1e-6, std::string("circumference of the ") + to_string(size) + " preset");
    }
}

TEST(reaches_grow_with_the_world) {
    Request small = small_request(9, 1);
    small.world_size = WorldSize::Small;
    Request large = small_request(9, 1);
    large.world_size = WorldSize::Large;
    const Config a = resolve(small);
    const Config b = resolve(large);
    check::close(b.support_reach / a.support_reach, 3.0, 1e-9, "support reach to triple with a triple-width world");
    check::close(b.river_threshold_km2 / a.river_threshold_km2, 9.0, 1e-9,
                 "a catchment threshold to take the square of the width factor");
}

TEST(an_explicit_override_beats_the_derivation) {
    Request request = small_request(9, 1);
    request.world_size = WorldSize::Large;
    request.support_reach = 1234.0;
    request.globe_radius = 50000.0;
    const Config cfg = resolve(request);
    check::equal(cfg.support_reach, 1234.0, "an explicit reach to survive the scaling");
    check::equal(cfg.globe_radius, 50000.0, "an explicit radius to survive the derivation");
}

TEST(a_bad_config_is_refused_with_its_field_named) {
    Request even = small_request(16, 1);
    check::throws([&] { resolve(even); }, "an even grid size");
    Request tiny = small_request(1, 1);
    check::throws([&] { resolve(tiny); }, "a grid size below three");
    Request no_plates = small_request(9, 1);
    no_plates.plate_count = 1;
    check::throws([&] { resolve(no_plates); }, "a single plate");
}

// --- the document --------------------------------------------------------

TEST(the_document_is_structurally_well_formed) {
    const std::string text = to_json(build(9));
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (const char ch : text) {
        if (in_string) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') in_string = false;
            continue;
        }
        if (ch == '"') in_string = true;
        else if (ch == '{' || ch == '[') ++depth;
        else if (ch == '}' || ch == ']') --depth;
        check::is_true(depth >= 0, "no unbalanced close");
    }
    check::equal(depth, 0, "balanced delimiters");
    check::is_true(!in_string, "no unterminated string");
    check::is_true(text.find("\"schema\":\"fwg.world\"") != std::string::npos, "the document to name its schema");
}

TEST(doubles_survive_the_round_trip) {
    const double values[] = {0.0, 1.0, 0.1, 1e-300, 1e300, 1.0 / 3.0, 3.141592653589793,
                             -1234.56789012345, 0.17744123532462844};
    for (const double value : values) {
        const std::string text = format_double(value);
        const double parsed = std::strtod(text.c_str(), nullptr);
        check::is_true(parsed == value, "round trip of " + text);
    }

    // -0.0 needs its own case. `parsed == value` is TRUE for +0.0 under IEEE
    // comparison, so including it in the loop above asserted nothing about the sign --
    // which is the part that can actually be lost.
    const std::string negative_zero = format_double(-0.0);
    check::equal(negative_zero, std::string("-0"), "negative zero to keep its sign");
    check::is_true(std::signbit(std::strtod(negative_zero.c_str(), nullptr)),
                   "negative zero to parse back negative");
}

TEST(float_formatting_is_pinned_to_its_bytes) {
    // Round-tripping is necessary and not sufficient. `%.17g` round-trips every value
    // in the test above and writes different bytes for nearly all of them -- so with
    // only a round-trip test, swapping the formatter silently changes every published
    // number and every digest downstream, and nothing goes red.
    //
    // These strings are the contract. Shortest round-trip, as std::to_chars produces.
    check::equal(format_double(0.1), std::string("0.1"), "0.1");
    check::equal(format_double(1.0), std::string("1"), "1.0");
    check::equal(format_double(1.0 / 3.0), std::string("0.3333333333333333"), "1/3");
    check::equal(format_double(0.17744123532462844), std::string("0.17744123532462844"), "world_scale");
    check::equal(format_double(3.141592653589793), std::string("3.141592653589793"), "pi");
    check::equal(format_double(1e300), std::string("1e+300"), "1e300");
    check::equal(format_double(1e-300), std::string("1e-300"), "1e-300");
    check::equal(format_double(-1234.56789012345), std::string("-1234.56789012345"), "negative");
    // Non-finite values are not valid JSON numbers and must not be emitted as bare
    // NaN/Infinity, which a naive ostream would do.
    check::equal(format_double(std::nan("")), std::string("null"), "NaN");
    check::equal(format_double(std::numeric_limits<double>::infinity()), std::string("1e999"), "infinity");
}

TEST(the_document_leaves_wall_clock_timings_out_by_default) {
    // Timings are the one part of a world that is not a function of its seed, so
    // they are the one part that would stop a document from being hashable.
    const World world = build(9);
    std::ostringstream plain;
    std::ostringstream timed;
    write_world_json(plain, world, false, false);
    write_world_json(timed, world, false, true);
    check::is_true(plain.str().find("timing_ms") == std::string::npos, "no timings in the default document");
    check::is_true(timed.str().find("timing_ms") != std::string::npos, "timings when they are asked for");
    check::is_true(world.timing_ms.count("total") == 1, "the World itself to still carry them");
}

TEST(the_document_carries_the_layers_it_generated) {
    const World world = build(9);
    const std::string text = to_json(world);
    check::is_true(!world.layers.empty(), "a world to have layers to carry");
    for (const auto& [name, layer] : world.layers) {
        check::is_true(text.find("\"" + name + "\":") != std::string::npos, "the document to carry " + name);
    }
}
