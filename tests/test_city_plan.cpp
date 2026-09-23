// Town plans: the shape draw, the street tree, and what actually gets built.
//
// The geometry tests run on synthetic rasters, because what they are about is
// clearances, grades and overlaps rather than terrain. The planning tests run on a real
// generated world, because a plan is only interesting where the ground is: a plan over
// flat synthetic ground would place every building and prove nothing about the refusals
// that make up most of this file's behaviour.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "fwg/city_plan.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Built {
    World world;
    std::shared_ptr<const SphereGrid> grid;
    std::vector<CitySite> sites;
};

// Sites on real land, spread far enough apart that the neighbour crop does not bind and
// each city gets its class's full extent.
Built world_with_sites(int count, CityClass city_class = CityClass::Medium, std::uint32_t seed = 42,
                       int size = 65) {
    Request r;
    r.seed = seed;
    r.size = size;
    r.phase = 9;
    Built built;
    built.world = generate(resolve(r));
    // effective, not config: config is design space and every reach in a plan is metres.
    built.grid = sphere_grid(built.world.config.size, built.world.effective.globe_radius);

    const Grid& water = built.world.layers.at("water_type");
    const Grid& slope = built.world.layers.at("slope");
    std::vector<int> land;
    for (std::size_t i = 0; i < built.grid->node_count(); ++i) {
        const int x = built.grid->point_x[i];
        const int z = built.grid->point_z[i];
        // Away from the poles, where a whole latitude row collapses to one node.
        if (z < 4 || z > size - 5) continue;
        if (water.at(x, z) != 0.0 || slope.at(x, z) > 12.0) continue;
        land.push_back(static_cast<int>(i));
    }
    const std::size_t step = std::max<std::size_t>(1, land.size() / std::max(1, count));
    for (int k = 0; k < count && static_cast<std::size_t>(k) * step < land.size(); ++k) {
        const int node = land[static_cast<std::size_t>(k) * step];
        CitySite site;
        site.uid = "city-" + std::to_string(k);
        site.id = k;
        site.name = "City " + std::to_string(k);
        site.x = built.grid->point_x[static_cast<std::size_t>(node)];
        site.z = built.grid->point_z[static_cast<std::size_t>(node)];
        site.city_class = city_class;
        site.population_profile = "human_heartland";
        site.freshwater_distance_m = 120.0;
        site.resource_potential = 0.6;
        built.sites.push_back(std::move(site));
    }
    return built;
}

// A pin one raster cell from its parent city. The offset is load-bearing: a hamlet
// crops its window to half the metre distance to the NEAREST city or hamlet, and a pin
// sitting on its own parent crops to the four-metre floor and can build nothing.
HamletPin pin_beside(const Built& built, const std::string& id, const std::string& role,
                     int cells_away = 1) {
    const CitySite& site = built.sites.front();
    HamletPin pin;
    pin.id = id;
    pin.x = site.x + cells_away;
    pin.z = site.z;
    pin.core_id = 0;
    pin.population_profile = site.population_profile;
    pin.role = role;
    pin.node = built.grid->node_index(static_cast<int>(pin.x), static_cast<int>(pin.z));
    pin.uid = "hamlet-node-" + std::to_string(pin.node);
    return pin;
}

// Every cell a plot's rectangle covers, at the rotation the plot records.
std::vector<Cell> plot_cells(const Plot& plot, double half) {
    return footprint_cells(plot.x_m, plot.z_m, plot.plot_m.width, plot.plot_m.depth,
                           plot.rotation_degrees * kPi / 180.0, half, 4.0);
}

ShapeSite generous_site() {
    SiteFacts facts;
    facts.buildable_area_m2 = 90000.0;
    facts.usable_land_fraction = 0.8;
    facts.local_slope_degrees = 3.0;
    facts.aridity = 0.2;
    facts.wooded_fraction = 0.4;
    facts.regional_threat = 0.6;
    facts.defense_priority = 0.6;
    return facts.as_shape_site();
}

const CityShape* shape_named(std::string_view id) {
    for (const CityShape& s : city_shapes()) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

}  // namespace

// --- the catalogue identity ------------------------------------------------

TEST(the_shape_catalogue_identity_reproduces_the_recorded_digest) {
    // This digest is an RNG INPUT, not a checksum: it is hashed into every per-city
    // draw, so a catalogue whose bytes moved gives every city in every world a different
    // shape. It is recomputed here from the embedded document rather than copied, which
    // is the only version of this check that can ever fail for a real reason.
    const std::string& identity = city_shape_catalogue_identity();
    check::equal(identity.size(), std::size_t{64}, "a full sha256 hex digest");
    check::equal(identity, std::string("3037a9f7a56bbfcdd8c036fd78009d6ad4f61f4c35f8fb7a879b4aed68756dad"),
                 "the digest data/city-shapes.json records");
}

TEST(the_catalogue_is_whole_and_already_in_draw_order) {
    const auto shapes = city_shapes();
    check::equal(shapes.size(), std::size_t{17}, "every shipped shape");
    for (std::size_t i = 1; i < shapes.size(); ++i) {
        check::is_true(shapes[i - 1].id < shapes[i].id,
                       std::string("shapes to be in id order, but ") + std::string(shapes[i - 1].id) +
                           " precedes " + std::string(shapes[i].id));
    }
    // Variation keys are drawn in sorted order too, and a table that is not sorted turns
    // that into a silent per-shape reordering of the draws.
    for (const CityShape& shape : shapes) {
        for (std::size_t i = 1; i < shape.variation.size(); ++i) {
            check::is_true(shape.variation[i - 1].key < shape.variation[i].key,
                           std::string(shape.id) + " variation keys to be sorted");
        }
    }

    // The authored design guidance is carried, and carried WHOLE: one shape declares an
    // eighth note, and a fixed-width record would drop it the way the reference's
    // hard-coded layout-feature tuple drops a feature.
    std::size_t widest = 0;
    for (const CityShape& shape : shapes) {
        check::is_true(shape.layout.size() >= 7,
                       std::string(shape.id) + " to carry its design guidance, got " +
                           std::to_string(shape.layout.size()));
        for (const ShapeLayoutNote& note : shape.layout) {
            check::is_true(!note.key.empty() && !note.text.empty(),
                           std::string(shape.id) + " layout note to say something");
        }
        widest = std::max(widest, shape.layout.size());
    }
    check::equal(widest, std::size_t{8}, "the one shape that declares an eighth note to keep it");
}

// --- shape selection -------------------------------------------------------

TEST(only_the_shapes_the_planner_can_qualify_for_are_offered) {
    // The planner measures seven of the catalogue's twenty-one features, and a rule on
    // an absent one fails. Eleven shapes therefore cannot be reached at all today. This
    // pins the reachable set so that adding a fact quietly is noticed.
    const std::map<std::string_view, int> none;
    const auto medium = rank_shapes(generous_site(), CityClass::Medium, none);
    std::set<std::string> ids;
    for (const ShapeCandidate& c : medium) ids.insert(std::string(c.id));
    const std::set<std::string> expected = {"ceremonial_clusters", "courtyard_lanes", "market_grid",
                                            "organic_market", "walled_compounds"};
    check::is_true(ids == expected, "the five shapes a medium city can reach");

    const auto capital = rank_shapes(generous_site(), CityClass::Capital, none);
    std::set<std::string> capital_ids;
    for (const ShapeCandidate& c : capital) capital_ids.insert(std::string(c.id));
    check::equal(capital_ids.count("concentric_enceintes"), std::size_t{1},
                 "a capital to be able to build concentric enceintes");
    check::equal(capital_ids.size(), std::size_t{6}, "the capital-only shape on top of the five");
}

TEST(a_feature_the_site_never_set_fails_every_rule_that_names_it) {
    const std::map<std::string_view, int> none;
    ShapeSite site = generous_site();
    const auto without = rank_shapes(site, CityClass::Medium, none);
    bool found = false;
    for (const ShapeCandidate& c : without) found = found || c.id == "hill_rings";
    check::is_true(!found, "hill_rings to be ineligible while `hill` is unmeasured");

    // The other direction, so this is not merely asserting that nothing works: supply
    // the fact and the shape becomes reachable.
    site.set_flag(ShapeFeature::hill, true);
    const auto with = rank_shapes(site, CityClass::Medium, none);
    for (const ShapeCandidate& c : with) found = found || c.id == "hill_rings";
    check::is_true(found, "hill_rings to become eligible once the site says it is on a hill");
    check::is_true(with.size() > without.size(), "supplying a fact to widen the choice");
}

TEST(a_shape_already_used_nearby_is_worth_less) {
    std::map<std::string_view, int> none;
    const auto fresh = rank_shapes(generous_site(), CityClass::Medium, none);
    check::is_true(!fresh.empty(), "at least one eligible shape");
    const std::string_view first = fresh.front().id;
    const double base = fresh.front().weight;
    check::is_true(base > 0.0, "a positive base weight");

    std::map<std::string_view, int> seen;
    seen[first] = 2;
    const auto penalised = rank_shapes(generous_site(), CityClass::Medium, seen);
    check::equal(penalised.size(), fresh.size(), "the same shapes to remain eligible");
    // weight / (1 + seen * 0.75); twice seen is two fifths of the original.
    check::close(penalised.front().weight, base / (1.0 + 2 * 0.75), 1e-12,
                 "the repetition penalty to divide the weight");
}

TEST(an_integer_variation_can_draw_either_endpoint) {
    const CityShape* enceintes = shape_named("concentric_enceintes");
    check::is_true(enceintes != nullptr, "the concentric enceintes shape to exist");
    const ShapeVariation* rings = nullptr;
    for (const ShapeVariation& v : enceintes->variation) {
        if (v.key == "ring_count") rings = &v;
    }
    check::is_true(rings != nullptr && rings->integer, "ring_count to be an integer variation");

    std::set<int> drawn;
    int draws = 0;
    const std::map<std::string_view, int> none;
    for (int k = 0; k < 400; ++k) {
        const ShapeSelection s = select_shape(generous_site(), 42, "city-" + std::to_string(k),
                                              CityClass::Capital, none);
        if (s.shape_id != "concentric_enceintes") continue;
        ++draws;
        const double value = s.parameter("ring_count", -1.0);
        check::between(value, rings->min_value, rings->max_value, "ring_count inside its interval");
        drawn.insert(static_cast<int>(value));
    }
    check::is_true(draws >= 20, "enough enceinte draws for the sweep to mean anything, got " +
                                    std::to_string(draws));
    check::equal(drawn.size(), static_cast<std::size_t>(rings->max_value - rings->min_value + 1),
                 "an integer interval to be inclusive of BOTH endpoints");
}

TEST(the_same_city_always_draws_the_same_shape) {
    const std::map<std::string_view, int> none;
    const ShapeSelection a = select_shape(generous_site(), 42, "city-7", CityClass::Medium, none);
    const ShapeSelection b = select_shape(generous_site(), 42, "city-7", CityClass::Medium, none);
    check::is_true(!a.shape_id.empty(), "a shape to have been chosen");
    check::equal(std::string(a.shape_id), std::string(b.shape_id), "the draw to be reproducible");
    check::equal(a.parameters.size(), b.parameters.size(), "the same parameters");
    for (std::size_t i = 0; i < a.parameters.size(); ++i) {
        check::equal(a.parameters[i].value, b.parameters[i].value,
                     std::string(a.parameters[i].key) + " to be reproducible");
    }

    // And that it is not simply constant: a different city gets a different draw often
    // enough that the roulette is doing something.
    std::set<std::string> ids;
    for (int k = 0; k < 60; ++k) {
        ids.insert(std::string(
            select_shape(generous_site(), 42, "city-" + std::to_string(k), CityClass::Medium, none)
                .shape_id));
    }
    check::is_true(ids.size() >= 3, "the roulette to spread across shapes, got " +
                                        std::to_string(ids.size()));
}

TEST(a_site_fact_outside_its_declared_range_stops_the_draw) {
    // The catalogue is a validation table, not documentation. A threat assessment that
    // wrote 1.2 would otherwise pass every `regional_threat >= 0.55` preference in the
    // world and shift every capital's shape, invisibly.
    const std::map<std::string_view, int> none;
    ShapeSite over = generous_site();
    over.set(ShapeFeature::regional_threat, 1.2);
    check::throws([&] { rank_shapes(over, CityClass::Capital, none); },
                  "a threat above one to be refused");

    ShapeSite incomplete;
    incomplete.set(ShapeFeature::buildable_area_m2, 50000.0);
    check::throws([&] { rank_shapes(incomplete, CityClass::Medium, none); },
                  "a site missing a required fact to be refused");

    std::map<std::string_view, int> bogus;
    bogus["no_such_shape"] = 1;
    check::throws([&] { rank_shapes(generous_site(), CityClass::Medium, bogus); },
                  "a tally naming a shape that does not exist to be refused");

    check::throws([&] { select_shape(generous_site(), 42, "", CityClass::Medium, none); },
                  "an empty city id to be refused");

    // The floor: the same call with everything in range does rank shapes, so the
    // refusals above are the validation and not a function that always throws.
    check::is_true(!rank_shapes(generous_site(), CityClass::Capital, none).empty(),
                   "a legal site to still be ranked");
}

TEST(a_site_no_shape_fits_is_reported_rather_than_forced) {
    SiteFacts barren;  // the defaults are the unbuildable sentinels
    const std::map<std::string_view, int> none;
    const ShapeSelection s = select_shape(barren.as_shape_site(), 42, "city-0", CityClass::Small, none);
    check::is_true(s.shape_id.empty(), "no shape to be chosen for unbuildable ground");
    check::is_true(s.candidates.empty(), "no candidate to be ranked");
    check::is_true(!s.reason.empty(), "a stated reason");
    check::equal(s.catalogue_sha256, city_shape_catalogue_identity(), "the identity to be reported");
}

// --- geometry --------------------------------------------------------------

TEST(a_footprint_covers_the_rectangle_and_is_never_clamped_to_the_raster) {
    const double half = 40.0;
    const std::vector<Cell> axis = footprint_cells(0.0, 0.0, 12.0, 16.0, 0.0, half, 4.0);
    check::is_true(!axis.empty(), "an axis-aligned plot to cover ground");
    // The dilated test admits the cells whose SQUARE overlaps, so a 12x16 plot on a 4 m
    // grid covers more than the 3x4 cells it exactly spans.
    check::is_true(axis.size() >= 12, "at least the twelve cells the rectangle spans, got " +
                                          std::to_string(axis.size()));
    bool centre = false;
    for (Cell c : axis) centre = centre || c == Cell{10, 10};
    check::is_true(centre, "the cell holding the plot centre");

    const std::vector<Cell> rotated = footprint_cells(0.0, 0.0, 12.0, 16.0, kPi / 4.0, half, 4.0);
    check::is_true(rotated.size() >= axis.size(),
                   "a rotated plot to cover at least as many cells as an aligned one");

    // Off the edge of the world on purpose: clamping here silently admits a plot that
    // hangs over the raster, because the containment test would then pass.
    const std::vector<Cell> over = footprint_cells(-half + 2.0, 0.0, 12.0, 16.0, 0.0, half, 4.0);
    bool negative = false;
    for (Cell c : over) negative = negative || c.first < 0;
    check::is_true(negative, "cells outside the raster to be produced, not clamped");
}

TEST(clearance_is_the_chebyshev_distance_to_ground_that_is_not_free) {
    const int size = 16;
    CellSet free(size);
    for (int j = 0; j < size; ++j) {
        for (int i = 0; i < size; ++i) {
            if (i == 7 && j == 7) continue;  // one blocked cell in the middle
            free.insert({i, j});
        }
    }
    const std::vector<int> field = clearance_field(free, size);

    int deepest = 0;
    for (int j = 0; j < size; ++j) {
        for (int i = 0; i < size; ++i) {
            int expected = 0;
            if (free.contains({i, j})) {
                expected = size + size;
                for (int bj = -1; bj <= size; ++bj) {
                    for (int bi = -1; bi <= size; ++bi) {
                        if (free.contains({bi, bj})) continue;  // off-raster counts as blocked
                        expected = std::min(expected, std::max(std::abs(bi - i), std::abs(bj - j)));
                    }
                }
            }
            const int actual = field[static_cast<std::size_t>(j) * size + static_cast<std::size_t>(i)];
            check::equal(actual, expected,
                         "clearance at " + std::to_string(i) + "," + std::to_string(j));
            deepest = std::max(deepest, actual);
        }
    }
    check::is_true(deepest >= 4, "some cell to be several cells from anything blocked, got " +
                                     std::to_string(deepest));
}

TEST(the_clearance_prefilter_never_rejects_a_plot_the_real_tests_would_accept) {
    // The prefilter decides most candidate positions without rasterizing. Its soundness
    // is an argument in the source; this is the argument executed. Every position it
    // rejects must really overlap ground that is not free.
    const int size = 32;
    const double half = size * 4.0 / 2.0;
    CellSet free(size);
    for (int j = 0; j < size; ++j) {
        for (int i = 0; i < size; ++i) {
            // A blocked lattice, so the clearance field is genuinely varied.
            if ((i * 7 + j * 11) % 23 < 4) continue;
            free.insert({i, j});
        }
    }
    const std::vector<int> field = clearance_field(free, size);

    const double w = 12.0;
    const double d = 16.0;
    int fired = 0;
    int checked = 0;
    for (int j = 0; j < size; ++j) {
        for (int i = 0; i < size; ++i) {
            const double x = -half + (i + 0.5) * 4.0;
            const double z = -half + (j + 0.5) * 4.0;
            const int k = field[static_cast<std::size_t>(j) * size + static_cast<std::size_t>(i)];
            const bool rejected = (k + 1) * 4.0 * std::sqrt(2.0) <= std::min(w, d) / 2.0;
            ++checked;
            if (!rejected) continue;
            ++fired;
            check::is_true(!free.contains_all(footprint_cells(x, z, w, d, 0.0, half, 4.0)),
                           "the prefilter to fire only where the footprint really is blocked, at " +
                               std::to_string(i) + "," + std::to_string(j));
        }
    }
    check::equal(checked, size * size, "every position considered");
    check::is_true(fired >= 100, "the prefilter to have fired often enough to mean something, got " +
                                     std::to_string(fired));
}

TEST(a_street_tree_spans_its_ground_reaches_its_gates_and_refuses_a_cliff) {
    const int size = 24;
    CellSet valid(size);
    std::vector<double> heights(static_cast<std::size_t>(size) * size, 0.0);
    // The step is at column 18, not at the middle: the tree roots at the cell nearest
    // the raster centre, and a cliff through that cell would strand the root on the far
    // plateau and make the whole test measure the wrong side.
    constexpr int kCliffColumn = 18;
    for (int j = 0; j < size; ++j) {
        for (int i = 0; i < size; ++i) {
            valid.insert({i, j});
            // A ten-metre step across one cell: far steeper than the 0.35 grade a street
            // may climb over a 4 m cell, so no street may cross it.
            heights[static_cast<std::size_t>(j) * size + static_cast<std::size_t>(i)] =
                i < kCliffColumn ? 0.0 : 10.0;
        }
    }
    const std::vector<Cell> gates = {{2, 2}, {3, 20}};
    const StreetNetwork network = grow_streets(valid, heights, size, 12345u, 12.0, gates, 4);

    check::is_true(network.cells.count() > 10, "a street network to have been grown, got " +
                                                   std::to_string(network.cells.count()));
    check::is_true(!network.paths.empty(), "at least one street path");
    for (Cell gate : gates) {
        check::is_true(network.cells.contains(gate),
                       "the gate at " + std::to_string(gate.first) + "," +
                           std::to_string(gate.second) + " to be on a street");
    }
    // Nothing reaches the far plateau: every route onto it is a ten-metre step.
    std::size_t far_side = 0;
    std::size_t near_side = 0;
    for (Cell c : network.cells.sorted()) {
        if (c.first >= kCliffColumn) {
            ++far_side;
        } else {
            ++near_side;
        }
    }
    check::equal(far_side, std::size_t{0}, "no street to climb the cliff");
    // And the near side really was reachable, so the zero above is a refusal and not an
    // empty search.
    check::is_true(near_side > 20, "the flat side to be covered, got " + std::to_string(near_side));
}

TEST(a_street_never_cuts_a_corner_between_two_blocks_of_ground) {
    // Two quadrants meeting at a single point. A diagonal step needs BOTH its orthogonal
    // support cells, so there is no way across — a street that slipped through the
    // corner would be a road with nothing under half of it.
    const int size = 16;
    CellSet valid(size);
    for (int j = 0; j < size; ++j) {
        for (int i = 0; i < size; ++i) {
            if ((i < 8 && j < 8) || (i >= 8 && j >= 8)) valid.insert({i, j});
        }
    }
    const std::vector<double> flat(static_cast<std::size_t>(size) * size, 0.0);
    // The cell nearest the raster centre is (8, 8), so the tree roots in the upper block.
    const StreetNetwork network = grow_streets(valid, flat, size, 99u, 12.0, {}, 4);

    std::size_t rooted = 0;
    for (Cell c : network.cells.sorted()) {
        check::is_true(c.first >= 8 && c.second >= 8,
                       "no street to reach the far block through the corner, but " +
                           std::to_string(c.first) + "," + std::to_string(c.second) + " did");
        ++rooted;
    }
    check::is_true(rooted > 10, "the block the tree roots in to be covered, got " +
                                    std::to_string(rooted));
}

TEST(a_street_tree_over_no_ground_is_empty_rather_than_invented) {
    const int size = 8;
    const CellSet nothing(size);
    const std::vector<double> heights(static_cast<std::size_t>(size) * size, 0.0);
    const StreetNetwork empty = grow_streets(nothing, heights, size, 1u, 12.0, {}, 4);
    check::equal(empty.cells.count(), std::size_t{0}, "no street over no ground");
    check::is_true(empty.paths.empty(), "no path either");

    // The floor: the same call over real ground does produce something, so the emptiness
    // above is the ground and not the function.
    CellSet all(size);
    for (int j = 0; j < size; ++j) {
        for (int i = 0; i < size; ++i) all.insert({i, j});
    }
    const StreetNetwork grown = grow_streets(all, heights, size, 1u, 12.0, {}, 4);
    check::is_true(grown.cells.count() > 1, "streets over ground that exists");
}

TEST(smoothing_pulls_in_a_needle_and_leaves_the_rest_of_the_ring_alone) {
    std::vector<Point> ring;
    for (int k = 0; k < 16; ++k) {
        const double angle = 2.0 * kPi * k / 16.0;
        ring.push_back({100.0 * std::cos(angle), 100.0 * std::sin(angle)});
    }
    const std::size_t spike = 4;
    ring[spike] = {ring[spike].x * 4.0, ring[spike].z * 4.0};

    const std::vector<Point> smoothed = smooth_closed_ring(ring);
    check::equal(smoothed.size(), ring.size(), "every vertex to survive");
    const double before = std::hypot(ring[spike].x, ring[spike].z);
    const double after = std::hypot(smoothed[spike].x, smoothed[spike].z);
    check::is_true(after < before * 0.6,
                   "the needle to be pulled in, from " + std::to_string(before) + " to " +
                       std::to_string(after));
    check::is_true(after > 50.0, "and pulled in rather than collapsed onto the centre");

    // A vertex on the far side of the ring is not dragged about by it.
    const std::size_t far = (spike + 8) % ring.size();
    check::close(std::hypot(smoothed[far].x, smoothed[far].z), 100.0, 12.0,
                 "a vertex opposite the needle to stay roughly where it was");

    // Too few points is not a ring, and is returned untouched.
    const std::vector<Point> stub = {{1.0, 1.0}, {2.0, 2.0}, {3.0, 1.0}};
    check::equal(smooth_closed_ring(stub).size(), std::size_t{3}, "a stub to survive whole");
    check::equal(smooth_closed_ring(stub)[0].x, 1.0, "and untouched");
}

TEST(a_turn_is_the_heading_change_and_stays_inside_half_a_circle) {
    check::close(path_turn_degrees({0, 0}, {1, 0}, {2, 0}), 0.0, 1e-9, "a straight run to turn none");
    check::close(path_turn_degrees({0, 0}, {1, 0}, {1, 1}), 90.0, 1e-9, "a left turn");
    check::close(path_turn_degrees({0, 0}, {1, 0}, {1, -1}), -90.0, 1e-9, "a right turn");
    // A reversal lands on the positive end of the half-open range, not the negative one.
    check::close(path_turn_degrees({0, 0}, {1, 0}, {0, 0}), 180.0, 1e-9, "a reversal");
}

// --- programme sizing ------------------------------------------------------

TEST(the_city_crop_is_a_multiple_of_four_and_yields_to_a_close_neighbour) {
    const SettlementPreset& preset = settlement_preset("human_heartland", SettlementBlock::medium_city);
    const HousingProfile& house = housing_profile("worker_house");

    const double roomy = program_half_m(preset, CityClass::Medium, 100000.0, house.plot_m,
                                        house.worker_beds);
    check::equal(roomy, 320.0, "a medium city with no neighbour to take its class extent");

    // Below the class extent, because the crop is min(neighbour, max(class, demand)) and
    // a neighbour that is not actually closer than the class extent changes nothing.
    const double crowded = program_half_m(preset, CityClass::Medium, 250.0, house.plot_m,
                                          house.worker_beds);
    check::is_true(crowded < roomy, "a close neighbour to shrink the crop");
    check::equal(std::fmod(crowded, 4.0), 0.0, "the crop to be a whole number of cells");
    check::is_true(crowded >= 4.0, "and never to vanish");

    // The class minimum, not the programme, is what binds today: the demand term would
    // need roughly a fourfold programme to overtake it. If this ever stops holding, the
    // crop sizes in every world have changed.
    const double small = program_half_m(settlement_preset("human_heartland", SettlementBlock::small_city),
                                        CityClass::Small, 100000.0, house.plot_m, house.worker_beds);
    check::equal(small, 240.0, "a small city to take its class extent");

    // And the demand term is LIVE, not dead: it is a designed growth path that the
    // shipped registry never reaches, so nothing that exists today would notice it being
    // wrong. A capital's programme housed one worker to a house needs more room than a
    // small city's class extent, and the crop has to grow to give it.
    const SettlementPreset& big = settlement_preset("human_heartland", SettlementBlock::capital_city);
    const double demanded = program_half_m(big, CityClass::Small, 100000.0, house.plot_m, 1);
    check::is_true(demanded > 240.0,
                   "the programme to outgrow the class extent when it is big enough, got " +
                       std::to_string(demanded));
    check::equal(std::fmod(demanded, 4.0), 0.0, "and still be a whole number of cells");
    // Four beds a house instead of one is a quarter of the dwellings, and that is back
    // below the class extent — which is why the branch never fires on a shipped world.
    check::equal(program_half_m(big, CityClass::Small, 100000.0, house.plot_m, house.worker_beds),
                 240.0, "the shipped bed count to leave the class extent in charge");
}

TEST(only_a_town_big_enough_to_need_one_gets_an_enceinte) {
    ShapeSelection plain;
    plain.shape_id = "organic_market";
    check::equal(ring_count_for("organic_market", plain, CityClass::Small), 0,
                 "a small city to raise no wall");
    check::equal(ring_count_for("organic_market", plain, CityClass::Medium), 1, "a medium city, one ring");
    check::equal(ring_count_for("organic_market", plain, CityClass::Capital), 1, "a capital, one ring");

    ShapeSelection concentric;
    concentric.shape_id = "concentric_enceintes";
    concentric.parameters = {{"ring_count", 3.0}};
    check::equal(ring_count_for("concentric_enceintes", concentric, CityClass::Capital), 3,
                 "concentric enceintes to take their drawn ring count");
    concentric.parameters = {{"ring_count", 9.0}};
    check::equal(ring_count_for("concentric_enceintes", concentric, CityClass::Capital), 3,
                 "and to be clamped to three");
}

// --- the content tables ----------------------------------------------------

TEST(the_vocabularies_name_themselves_correctly) {
    // Both tables are written out by hand beside enums the codegen emits into. A name
    // table that slipped by one would put the wrong condition into every "Unverified
    // prerequisite" reason a consumer reads, and nothing else would notice.
    check::equal(std::string(to_string(PlacementCondition::usable_groundwater)),
                 std::string("usable_groundwater"), "the first condition");
    check::equal(std::string(to_string(PlacementCondition::defended_perimeter)),
                 std::string("defended_perimeter"), "a condition in the middle");
    check::equal(std::string(to_string(PlacementCondition::resource_support)),
                 std::string("resource_support"), "the last condition");
    std::set<std::string> conditions;
    for (int i = 0; i < kPlacementConditionCount; ++i) {
        conditions.insert(std::string(to_string(static_cast<PlacementCondition>(i))));
    }
    check::equal(conditions.size(), static_cast<std::size_t>(kPlacementConditionCount),
                 "every condition to have its own name");

    check::equal(std::string(to_string(ShapeFeature::coast_access)), std::string("coast_access"),
                 "the first site feature");
    check::equal(std::string(to_string(ShapeFeature::buildable_area_m2)),
                 std::string("buildable_area_m2"), "the last site feature");
    std::set<std::string> features;
    for (int i = 0; i < kShapeFeatureCount; ++i) {
        features.insert(std::string(to_string(static_cast<ShapeFeature>(i))));
    }
    check::equal(features.size(), static_cast<std::size_t>(kShapeFeatureCount),
                 "every site feature to have its own name");
    // And the names are the ones the catalogue's rules were authored against, which is
    // what makes a rule on a feature the planner never sets fail rather than misfire.
    for (const CityShape& shape : city_shapes()) {
        for (const ShapeRule& rule : shape.eligibility) {
            check::equal(features.count(std::string(to_string(rule.field))), std::size_t{1},
                         std::string(shape.id) + " to test a named feature");
        }
    }
}

TEST(a_settlement_programme_keeps_the_order_it_was_authored_in) {
    // The planner installs rows greedily in this order and stamps positional plot ids,
    // so an INSERTION moves buildings onto different plots and renumbers every plot
    // after it. Append, never insert — which is what pinning the head of the list
    // checks: appending leaves it alone, inserting does not.
    const SettlementPreset& medium =
        settlement_preset("human_heartland", SettlementBlock::medium_city);
    const std::vector<std::string_view> head = {"building.water_point", "building.well",
                                                "building.latrine", "building.waste_yard",
                                                "building.granary", "building.bakery"};
    check::is_true(medium.buildings.size() > head.size(), "a programme longer than its head");
    for (std::size_t i = 0; i < head.size(); ++i) {
        check::equal(std::string(medium.buildings[i].structure().id), std::string(head[i]),
                     "medium-city row " + std::to_string(i));
    }

    const SettlementPreset& hamlet = settlement_preset("human_heartland", SettlementBlock::hamlet);
    check::is_true(!hamlet.buildings.empty(), "a rural programme to exist");
    check::equal(std::string(hamlet.buildings[0].structure().id), std::string("building.hamlet_well"),
                 "a hamlet to dig its well first");

    // Civilizations keep the registry's own order, which a map colours regions by.
    const auto civs = civilization_presets();
    check::equal(civs.size(), std::size_t{12}, "every civilization");
    check::equal(std::string(civs[0].id), std::string("human_maritime"), "the registry's first people");
    check::equal(std::string(civs.back().id), std::string("frosthold_dwarf"), "and its last");
}

TEST(every_plan_row_resolves_to_a_measured_structure) {
    std::size_t rows = 0;
    std::size_t staffed = 0;
    for (const CivilizationPresets& civ : civilization_presets()) {
        check::equal(civ.blocks.size(), std::size_t{4}, std::string(civ.id) + " to have four blocks");
        for (const SettlementPreset& preset : civ.blocks) {
            for (const PresetRow& row : preset.buildings) {
                ++rows;
                const Structure& s = row.structure();
                check::is_true(!s.id.empty(), "a resolved structure id");
                check::is_true(s.plot_m.width > 0.0 && s.plot_m.depth > 0.0,
                               std::string(s.id) + " to have a plot to stand on");
                // The registry invariant the planner leans on: the plot is the building
                // plus its clearances, so reserving the plot reserves the clearance too.
                check::is_true(s.plot_m.width >= s.dimensions_m.width,
                               std::string(s.id) + " plot at least as wide as the building");
                check::is_true(s.plot_m.depth >= s.dimensions_m.depth,
                               std::string(s.id) + " plot at least as deep as the building");
                check::is_true(row.count > 0, std::string(s.id) + " to be wanted at all");
                if (s.workers > 0) ++staffed;
            }
        }
    }
    check::is_true(rows >= 1500, "the whole programme table, got " + std::to_string(rows));
    check::is_true(staffed > 0, "buildings that need staff, or nothing would need housing");
    check::equal(structures().size(), std::size_t{106}, "every structure in the three libraries");
}

TEST(densification_is_legal_because_an_apartment_takes_the_house_plot) {
    const HousingProfile& house = housing_profile("worker_house");
    const HousingProfile& apartment = housing_profile("worker_apartment");
    // A house is replaced in place, with no re-test of terrain or corridor. That is only
    // sound while the apartment stands on exactly the same rectangle.
    check::equal(apartment.plot_m.width, house.plot_m.width, "the same plot width");
    check::equal(apartment.plot_m.depth, house.plot_m.depth, "the same plot depth");
    check::is_true(apartment.worker_beds > house.worker_beds, "and strictly more beds");
    check::is_true(housing_profile("hamlet_house").worker_beds > 0, "a cottage to sleep somebody");
}

TEST(the_pack_tables_keep_the_order_their_draws_depend_on) {
    const auto packs = building_packs();
    check::equal(packs.size(), std::size_t{7}, "every shipped pack");
    // A cumulative weighted walk with no sort in the path: position IS the contract.
    check::equal(std::string(packs.front().id), std::string("city_pack_generic_surface"),
                 "the fallback pack to still be first");
    bool fallback_present = false;
    std::size_t options = 0;
    for (const BuildingPack& pack : packs) {
        fallback_present = fallback_present || pack.id == fallback_building_pack_id();
        check::is_true(pack.weight > 0.0, std::string(pack.id) + " to be reachable at all");
        for (const BuildingOption& option : pack.building_options) {
            ++options;
            check::is_true(!option.asset_choices.empty(),
                           std::string(option.id) + " to offer an asset");
            for (const AssetChoice& choice : option.asset_choices) {
                check::is_true(choice.weight > 0.0,
                               std::string(choice.asset_id) + " to be drawable at all");
            }
        }
    }
    check::is_true(fallback_present, "the fallback pack to be in the list it falls back to");
    check::equal(options, std::size_t{90}, "every shipped building option");

    // A layout profile declares only features something reads. The reference iterates a
    // hard-coded seven-name tuple and drops an eighth silently; the codegen refuses one.
    check::equal(layout_feature_defaults().size(), std::size_t{7}, "seven known layout features");
    const auto profiles = layout_profiles();
    check::equal(profiles.size(), std::size_t{2}, "both shipped layout profiles");
    for (const LayoutProfile& profile : profiles) {
        check::is_true(!profile.features.empty(), std::string(profile.id) + " to declare features");
        for (const LayoutFeature& feature : profile.features) {
            bool known = false;
            for (const LayoutFeature& known_feature : layout_feature_defaults()) {
                known = known || known_feature.name == feature.name;
            }
            check::is_true(known, std::string(profile.id) + " feature " + std::string(feature.name) +
                                      " to be one something reads");
        }
    }
}

// --- planning a city -------------------------------------------------------

TEST(a_city_is_packed_with_buildings_streets_and_beds_for_its_workers) {
    const Built built = world_with_sites(1, CityClass::Medium);
    check::is_true(!built.sites.empty(), "a site on real land to plan");
    const std::map<std::string_view, int> none;
    const CityPlan plan = plan_city(built.world, *built.grid, built.sites[0], built.sites, none);

    check::is_true(plan.terrain.size > 0, "a terrain raster");
    check::equal(plan.terrain.codes.size(),
                 static_cast<std::size_t>(plan.terrain.size) * plan.terrain.size, "a full raster");
    check::equal(plan.terrain.surface_heights_m.size(),
                 static_cast<std::size_t>(plan.terrain.size + 1) * (plan.terrain.size + 1),
                 "corner-sampled elevations, one more per edge than there are cells");
    check::is_true(!plan.shape.shape_id.empty(), "a shape to have been chosen");
    check::is_true(!plan.roads.empty(), "streets to have been grown");
    check::is_true(!plan.plots.empty(), "buildings to have been placed");
    check::is_true(plan.stats.workers > 0, "staff to have been hired");
    check::is_true(plan.stats.service_buildings > 0, "services, not only housing");
    check::is_true(plan.stats.houses + plan.stats.apartments > 0,
                   "somewhere for the staff to sleep");
    check::is_true(plan.status == "partial" || plan.status == "complete",
                   "a buildable city to report a buildable status, got " + std::string(plan.status));

    // Plot ids are positional and the position is a contract: a consumer joins on them.
    for (std::size_t i = 0; i < plan.plots.size(); ++i) {
        check::equal(plan.plots[i].id, "plot-" + std::to_string(i), "plot ids to be positional");
    }
    // The stats are a summary of the plots, not a second opinion about them.
    int workers = 0;
    int beds = 0;
    for (const Plot& p : plan.plots) {
        workers += p.workers;
        beds += p.beds;
    }
    check::equal(plan.stats.workers, workers, "the worker total to match the plots");
    check::equal(plan.stats.worker_beds, beds, "the bed total to match the plots");
    check::equal(plan.stats.housing_shortfall, std::max(0, workers - beds), "the stated shortfall");
}

TEST(no_two_buildings_stand_on_the_same_ground) {
    const Built built = world_with_sites(1, CityClass::Medium);
    const std::map<std::string_view, int> none;
    const CityPlan plan = plan_city(built.world, *built.grid, built.sites[0], built.sites, none);
    check::is_true(plan.plots.size() >= 2, "enough buildings for overlap to be possible, got " +
                                               std::to_string(plan.plots.size()));

    CellSet taken(plan.terrain.size);
    for (const Plot& plot : plan.plots) {
        for (Cell c : plot_cells(plot, plan.half_m)) {
            check::is_true(taken.insert(c), plot.id + " to stand on ground no other plot holds");
        }
    }
    check::is_true(taken.count() > 0, "ground to have been claimed at all");
}

TEST(a_building_stands_on_buildable_ground_it_can_reach_from_a_street) {
    const Built built = world_with_sites(1, CityClass::Medium);
    const std::map<std::string_view, int> none;
    const CityPlan plan = plan_city(built.world, *built.grid, built.sites[0], built.sites, none);

    CellSet street(plan.terrain.size);
    for (Cell c : plan.roads) street.insert(c);

    std::size_t checked = 0;
    for (const Plot& plot : plan.plots) {
        // A gatehouse or tower is placed where the WALL says the opening is, which is
        // deliberately exempt from the terrain tests; everything else is not.
        if (plot.phase == "fortification") continue;
        ++checked;
        for (Cell c : plot_cells(plot, plan.half_m)) {
            check::is_true(c.first >= 0 && c.first < plan.terrain.size && c.second >= 0 &&
                               c.second < plan.terrain.size,
                           plot.id + " to stand inside the raster");
            check::equal(plan.terrain.code(c.first, c.second), 0,
                         plot.id + " to stand on buildable ground");
            check::is_true(!street.contains(c), plot.id + " not to stand on a street");
        }
        // The frontage it was given is a real point in the window.
        check::between(plot.road_access.x, -plan.half_m, plan.half_m, plot.id + " frontage x");
        check::between(plot.road_access.z, -plan.half_m, plan.half_m, plot.id + " frontage z");
        check::is_true(plot.ground_elevation_m >= plot.foundation_bottom_m,
                       plot.id + " to sit above its own foundation");
    }
    check::is_true(checked >= 5, "several ordinary buildings to test, got " + std::to_string(checked));
}

TEST(core_services_are_packed_before_anything_optional) {
    const Built built = world_with_sites(1, CityClass::Medium);
    const std::map<std::string_view, int> none;
    const CityPlan plan = plan_city(built.world, *built.grid, built.sites[0], built.sites, none);

    // The phases run in a fixed order and plots are appended as they are installed, so
    // the plot list IS the build order. A low-priority service appearing before core
    // housing means the housing pass ran late and beds were lost to shops.
    std::size_t last_high = 0;
    std::size_t first_low = plan.plots.size();
    std::size_t high = 0;
    std::size_t low = 0;
    for (std::size_t i = 0; i < plan.plots.size(); ++i) {
        const std::string_view phase = plan.plots[i].phase;
        if (phase == "high" || phase == "high_housing") {
            last_high = i;
            ++high;
        }
        if (phase == "low" || phase == "low_housing") {
            first_low = std::min(first_low, i);
            ++low;
        }
    }
    check::is_true(high > 0, "core buildings to have been placed, got " + std::to_string(high));
    if (low > 0) check::is_true(first_low > last_high, "every optional building to come after the core");

    // And the preset order decides which core building takes the most central frontage:
    // rows are installed greedily in list order, so the plots follow it.
    const SettlementPreset& preset =
        settlement_preset(built.sites[0].population_profile, SettlementBlock::medium_city);
    std::vector<std::string_view> wanted;
    for (const PresetRow& row : preset.buildings) {
        if (row.structure().priority != StructurePriority::core) continue;
        wanted.push_back(row.structure().id);
    }
    std::size_t cursor = 0;
    std::size_t matched = 0;
    for (const Plot& plot : plan.plots) {
        if (plot.phase != "high") continue;
        while (cursor < wanted.size() && wanted[cursor] != plot.building_id) ++cursor;
        check::is_true(cursor < wanted.size(),
                       plot.id + " (" + std::string(plot.building_id) +
                           ") to appear in preset order, not before a row that precedes it");
        ++matched;
    }
    check::is_true(matched >= 5, "enough core plots to prove the order, got " + std::to_string(matched));
}

TEST(worker_housing_is_reserved_before_services_take_the_frontage) {
    const Built built = world_with_sites(1, CityClass::Medium);
    const std::map<std::string_view, int> none;
    const CityPlan plan = plan_city(built.world, *built.grid, built.sites[0], built.sites, none);

    // Held street-front plots are what stop later service packing from starving the
    // housing pass. A budget of zero means nothing was held and the audit is decoration.
    check::is_true(!plan.debug.housing_reservations.empty(), "housing to have been budgeted for");
    int budgeted = 0;
    for (const HousingReservation& r : plan.debug.housing_reservations) budgeted += r.workers_budgeted;
    check::is_true(budgeted > 0, "a real worker budget, got " + std::to_string(budgeted));

    check::is_true(!plan.debug.housing_passes.empty(), "a housing pass to have run");
    int built_houses = 0;
    for (const HousingAudit& audit : plan.debug.housing_passes) {
        check::is_true(audit.final_beds >= audit.starting_beds, "a pass never to lose beds");
        check::equal(audit.shortfall, std::max(0, audit.workers - audit.final_beds),
                     std::string(audit.phase) + " shortfall to be what it says");
        // Upgrades densify in place, so they add beds without adding plots; houses add
        // both. Either way the arithmetic has to close.
        check::equal(audit.final_beds - audit.starting_beds,
                     audit.houses_placed * housing_profile("worker_house").worker_beds +
                         audit.upgrades * (housing_profile("worker_apartment").worker_beds -
                                           housing_profile("worker_house").worker_beds),
                     std::string(audit.phase) + " beds to be accounted for");
        built_houses += audit.houses_placed;
    }
    check::is_true(built_houses > 0, "houses to have been built, got " + std::to_string(built_houses));
    check::equal(plan.stats.houses + plan.stats.apartments, built_houses,
                 "every dwelling built to still be standing as one or the other");
}

TEST(a_prerequisite_the_planner_cannot_verify_is_reported_not_guessed) {
    const Built built = world_with_sites(1, CityClass::Medium);
    const std::map<std::string_view, int> none;
    const CityPlan plan = plan_city(built.world, *built.grid, built.sites[0], built.sites, none);

    std::size_t unverified = 0;
    for (const Unplaced& row : plan.unplaced) {
        check::is_true(row.count > 0, "an unplaced row to say how many");
        check::is_true(structure(row.building_id) != nullptr,
                       std::string(row.building_id) + " to name a real structure");
        if (row.reason.rfind("Unverified prerequisite: ", 0) == 0) ++unverified;
    }
    // The planner resolves no groundwater and no wind, so a well is never built on a
    // guess. If this ever reaches zero, either the conditions grew or the refusal broke.
    check::is_true(unverified > 0, "prerequisites to be reported rather than assumed");

    int stated = 0;
    for (const Unplaced& row : plan.unplaced) stated += row.count;
    check::equal(plan.stats.unplaced_total, stated, "the unplaced total to match the rows");
    check::is_true(plan.stats.unplaced_core <= plan.stats.unplaced_total,
                   "core failures to be a subset of all failures");
}

TEST(a_town_the_player_laid_out_gets_its_ground_but_no_opinion) {
    Built built = world_with_sites(1, CityClass::Medium);
    built.sites[0].founded_by_player = true;
    const std::map<std::string_view, int> none;
    const CityPlan plan = plan_city(built.world, *built.grid, built.sites[0], built.sites, none);

    check::is_true(plan.authored_by_player, "the plan to say who authored it");
    check::is_true(plan.shape.shape_id.empty(), "no shape to be chosen for an authored town");
    check::is_true(plan.plots.empty(), "and no buildings placed");
    check::equal(std::string(plan.status), std::string("unbuildable"),
                 "a status consumers already understand");
    check::is_true(!plan.fortifications.has_value(), "no enceinte either");
    // It still gets the ground, because a consumer rendering the player's town needs it.
    check::is_true(plan.terrain.size > 0, "the terrain raster to be produced anyway");
    check::is_true(!plan.terrain.surface_heights_m.empty(), "and the elevation surface");
    check::is_true(plan.reference_frame.radius_m > 0.0, "and the frame to place it on the globe");
    check::is_true(!plan.unplaced.empty(), "every programme row to be reported as unbuilt");
    for (const Unplaced& row : plan.unplaced) {
        check::equal(row.reason, std::string("Layout authored by the player"), "a stated reason");
    }
    // The shape record still carries the guidance and the candidates of the shape the
    // generator WOULD have picked. That is deliberate and it is a trap: a consumer must
    // read `shape_id` before reading `layout`, or it will describe the player's town
    // with a plan the player never made.
    check::is_true(!plan.shape.candidates.empty(),
                   "the shapes it would have considered to still be listed");
    check::is_true(!plan.shape.reason.empty(), "and a reason saying why none was used");
}

TEST(an_enceinte_leaves_real_gaps_where_its_gates_are) {
    const Built built = world_with_sites(1, CityClass::Capital);
    check::is_true(!built.sites.empty(), "a capital site to plan");
    const std::map<std::string_view, int> none;
    const CityPlan plan = plan_city(built.world, *built.grid, built.sites[0], built.sites, none);
    check::is_true(plan.fortifications.has_value(), "a capital to be walled");
    const Fortifications& walls = *plan.fortifications;
    check::is_true(walls.defended_perimeter, "a ring to have closed");
    check::is_true(!walls.rings.empty(), "at least one ring");
    check::is_true(walls.segments.size() >= 6, "a ring of real wall, got " +
                                                   std::to_string(walls.segments.size()));
    check::is_true(!walls.gates.empty(), "a wall always to get at least one opening");
    check::equal(plan.stats.wall_segments, static_cast<int>(walls.segments.size()),
                 "the stated segment count");

    for (const WallRing& ring : walls.rings) {
        check::is_true(ring.polyline_m.size() >= 8, "a ring to have closed on enough ground");
    }
    for (const WallSegment& seg : walls.segments) {
        // Every segment starts at a polyline vertex, so it comes out SHORT by the
        // remainder carried from the previous edge: lengths land in (0, depth], never
        // above it. A segment longer than a module means the carry reset is wrong.
        check::between(seg.length_m, 0.0, 10.01, seg.id + " to be at most one module long");
        for (const WallGate& gate : walls.gates) {
            if (gate.ring_id != seg.ring_id) continue;
            check::is_true(std::hypot(seg.center_m.x - gate.position_m.x,
                                      seg.center_m.z - gate.position_m.z) >= 8.0,
                           seg.id + " not to block " + gate.id);
        }
    }
    // A wall is a condition, and something in the programme depends on it.
    check::equal(plan.stats.defended_perimeter, walls.defended_perimeter, "the stated perimeter");

    // The openings are reserved BEFORE ordinary packing, so a gatehouse is not crowded
    // out of the gap the wall left for it. A gate that carries a plot id must have a
    // plot to point at, and that plot is exempt from the terrain tests on purpose: the
    // opening is where the wall says it is.
    std::size_t wall_plots = 0;
    for (const Plot& plot : plan.plots) {
        if (plot.phase == "fortification") ++wall_plots;
    }
    check::is_true(wall_plots > 0, "the enceinte to have been given buildings, got " +
                                       std::to_string(wall_plots));
    std::size_t backed = 0;
    for (const WallGate& gate : walls.gates) {
        if (gate.plot_id.empty()) continue;
        ++backed;
        bool found = false;
        for (const Plot& plot : plan.plots) {
            found = found || (plot.id == gate.plot_id && plot.phase == "fortification");
        }
        check::is_true(found, gate.id + " to point at a fortification plot that exists");
    }
    check::is_true(backed > 0, "at least one gate to have a gatehouse built in it");

    // A tower record with no budget behind it carries no plot id, and a consumer must
    // not assume one. The wall geometry is independent of the building programme.
    for (const WallTower& tower : walls.towers) {
        if (tower.plot_id.empty()) continue;
        bool found = false;
        for (const Plot& plot : plan.plots) found = found || plot.id == tower.plot_id;
        check::is_true(found, tower.id + " to point at a plot that exists");
    }
}

TEST(neighbouring_cities_are_planned_in_uid_order_and_do_not_all_wear_one_shape) {
    Built built = world_with_sites(12, CityClass::Medium);
    // ELEVEN IS THE FLOOR THAT MATTERS, not a round number: below city-10 the
    // lexicographic and numeric orders agree and this test cannot tell them apart.
    check::is_true(built.sites.size() >= 11, "enough cities for the two orders to differ, got " +
                                                 std::to_string(built.sites.size()));
    // Lexicographic on the string form, not numeric: city-10 comes before city-2, and
    // that order is what the repetition counter walks, so it decides which city gets
    // which shape and not merely the order of the output.
    std::reverse(built.sites.begin(), built.sites.end());
    const CityPlans plans = fill_cities(built.world, *built.grid, built.sites);

    check::equal(plans.cities.size(), built.sites.size(), "every site to be planned");
    for (std::size_t i = 1; i < plans.cities.size(); ++i) {
        check::is_true(plans.cities[i - 1].city_uid < plans.cities[i].city_uid,
                       "plans to come out in uid order");
    }
    check::equal(plans.shapes_sha256, city_shape_catalogue_identity(),
                 "the block to publish the catalogue identity it drew against");

    std::set<std::string> shapes;
    std::size_t shaped = 0;
    for (const CityPlan& plan : plans.cities) {
        if (plan.shape.shape_id.empty()) continue;
        ++shaped;
        shapes.insert(std::string(plan.shape.shape_id));
    }
    check::is_true(shaped >= 5, "most cities to have been given a shape, got " + std::to_string(shaped));
    check::is_true(shapes.size() >= 2, "a region not to be one repeated pattern, got " +
                                           std::to_string(shapes.size()));
}

// --- planning a hamlet -----------------------------------------------------

TEST(a_hamlet_packs_cottages_and_never_city_apartments) {
    const Built built = world_with_sites(1, CityClass::Medium);
    const HamletPin pin = pin_beside(built, "hamlet-0", "farming");
    const HamletPlan plan = plan_hamlet(built.world, *built.grid, pin, built.sites, {pin});

    check::is_true(plan.half_m <= 100.0, "a hamlet window to be at most 200 m across");
    check::equal(std::fmod(plan.half_m, 4.0), 0.0, "and a whole number of cells");
    // The shape is fixed, but its two numbers are still published: they are the street
    // spacing the tree was grown at and the aspect of the ellipse the window was cut to,
    // and a consumer redrawing the window has no other way to know either.
    check::equal(std::string(plan.shape_id), std::string("hamlet_compact"), "the fixed rural shape");
    check::equal(plan.shape_block_spacing_m, 48.0, "the spacing the streets were grown at");
    check::equal(plan.shape_aspect_ratio, 1.1, "the ellipse the window was cut to");
    check::is_true(plan.terrain.size > 0, "a terrain raster");
    check::is_true(!plan.plots.empty(), "buildings to have been placed, got " +
                                            std::to_string(plan.plots.size()));
    check::is_true(plan.stats.workers > 0, "staff to have been hired");
    check::equal(plan.stats.apartments, 0, "a hamlet never densifies");
    check::equal(std::string(plan.debug.apartment_policy), std::string("hamlet_houses_only"),
                 "and says so");

    const HousingProfile& cottage = housing_profile("hamlet_house");
    const HousingProfile& city_house = housing_profile("worker_house");
    const HousingProfile& apartment = housing_profile("worker_apartment");
    std::size_t cottages = 0;
    for (const Plot& plot : plan.plots) {
        check::is_true(plot.building_id != city_house.id, "no city worker house in a hamlet");
        check::is_true(plot.building_id != apartment.id, "and no apartment block");
        check::is_true(!plot.housing_upgrade.has_value(), "and nothing upgraded in place");
        if (plot.building_id == cottage.id) ++cottages;
    }
    check::equal(plan.stats.houses, static_cast<int>(cottages), "the cottage count to match");
    check::is_true(cottages > 0, "cottages for the staff, got " + std::to_string(cottages));
    check::equal(plan.core_city_uid, built.sites[0].key(), "the parent city to be named");

    // The seeded ellipse is what keeps hamlets independent of the city shape catalogue,
    // and it has to actually cut: a clip that kept the whole square would be a no-op
    // nothing else in the plan would reveal, since a square hamlet still packs fine.
    check::is_true(plan.debug.shape_safe_cells > 0, "the ellipse to leave ground to build on");
    check::is_true(plan.debug.shape_safe_cells < plan.debug.terrain_safe_cells,
                   "the ellipse to cut the corners off the window, kept " +
                       std::to_string(plan.debug.shape_safe_cells) + " of " +
                       std::to_string(plan.debug.terrain_safe_cells));
    // Streets, buildings and vacant ground are disjoint and all inside the ellipse. An
    // ACCESS corridor is not in that partition: it may cross a street, and is meant to —
    // a spur that had to stop at the kerb would reach nothing. So the four counts sum to
    // MORE than the ellipse by exactly the overlap, and a test that demanded equality
    // would be asserting that corridors never reach the road.
    check::is_true(plan.debug.road_cells + plan.debug.occupied_cells + plan.debug.vacant_shape_cells <=
                       plan.debug.shape_safe_cells,
                   "street, building and vacant ground to be disjoint inside the ellipse");
    check::is_true(plan.debug.access_cells > 0, "buildings to have access corridors at all");
    check::is_true(plan.debug.road_cells + plan.debug.occupied_cells + plan.debug.access_cells +
                           plan.debug.vacant_shape_cells >
                       plan.debug.shape_safe_cells,
                   "corridors to reach the street rather than stopping short of it");
}

TEST(a_hamlets_role_decides_what_it_is_allowed_to_build) {
    const Built built = world_with_sites(1, CityClass::Medium);
    const HamletPin farming = pin_beside(built, "hamlet-0", "farming");
    HamletPin roleless = pin_beside(built, "hamlet-1", "");

    const HamletPlan farm = plan_hamlet(built.world, *built.grid, farming, built.sites, {farming});
    const HamletPlan none = plan_hamlet(built.world, *built.grid, roleless, built.sites, {roleless});

    const auto unverified = [](const HamletPlan& plan) {
        int count = 0;
        for (const Unplaced& row : plan.unplaced) {
            if (row.reason.rfind("Unverified prerequisite: ", 0) == 0) count += row.count;
        }
        return count;
    };
    check::is_true(unverified(farm) > 0, "even a farming hamlet to lack some prerequisite");
    check::is_true(unverified(none) > unverified(farm),
                   "a hamlet with no role to be allowed strictly less, got " +
                       std::to_string(unverified(none)) + " against " +
                       std::to_string(unverified(farm)));
    // A farm is allowed its barn, and a role-less pin is not: the difference above is
    // the role and not a coincidence of terrain.
    const auto builds = [](const HamletPlan& plan, std::string_view id) {
        for (const Plot& plot : plan.plots) {
            if (plot.building_id == id) return true;
        }
        return false;
    };
    check::is_true(builds(farm, "building.hamlet_barn"), "a farming hamlet to raise a barn");
    check::is_true(!builds(none, "building.hamlet_barn"), "a role-less pin not to");

    // A HAMLET IS NEVER 'complete', AND THAT IS THE BEHAVIOUR, NOT A BUG. Every role
    // excludes at least one core-priority building: a farming hamlet has no reason for a
    // resource staging yard, so that row goes unplaced and the status says so. `status`
    // is a consumer-facing vocabulary word, and "fixing" this changes every consumer's
    // reading of every rural settlement in the world.
    check::equal(std::string(farm.status), std::string("partial"), "a farming hamlet to be partial");
    check::equal(farm.stats.unplaced_core, 1, "exactly the staging yard a farm does not want");
    check::equal(std::string(none.status), std::string("partial"), "and a role-less pin too");
    check::equal(none.stats.unplaced_core, 3, "barn, farmyard and staging yard");
}

TEST(a_hamlet_with_a_support_path_gets_one_gate_and_no_more) {
    Built built = world_with_sites(2, CityClass::Medium);
    check::is_true(built.sites.size() == 2, "two sites, so there is a parent to walk toward");
    HamletPin pin = pin_beside(built, "hamlet-0", "farming");
    // The hamlet's own node first, then its neighbour. One raster cell is well over a
    // kilometre on this world, so the very first hop leaves a 200 m window; a node
    // further round the sphere would fall past the tangent horizon and open no gate at
    // all, which is a different behaviour from the one under test.
    pin.access_nodes = {pin.node, built.grid->node_index(static_cast<int>(pin.x) + 1,
                                                        static_cast<int>(pin.z))};

    const HamletPlan plan = plan_hamlet(built.world, *built.grid, pin, built.sites, {pin});
    check::is_true(plan.road_connections.size() <= 1, "at most one support gate");
    check::is_true(!plan.road_connections.empty(), "a path that leaves the window to open one");
    const RoadConnection& gate = plan.road_connections.front();
    check::equal(gate.route_id, std::string("support:hamlet-0"), "the gate to name its pin");
    check::equal(gate.route_index, -1, "a support road is not a regional route");
    check::is_true(gate.has_cell, "the gate to be resolved to a raster cell");
    check::between(gate.gate_local_m.x, -plan.half_m - 1e-6, plan.half_m + 1e-6, "gate x in the window");
    check::between(gate.gate_local_m.z, -plan.half_m - 1e-6, plan.half_m + 1e-6, "gate z in the window");
    check::is_true(gate.status == "connected" || gate.status == "unreachable",
                   "a stated junction status, got " + std::string(gate.status));
    if (gate.status == "connected") {
        check::is_true(!gate.local_path_m.empty(), "a connected gate to carry its approach");
    } else {
        check::is_true(gate.local_path_m.empty(), "an unreachable gate to carry no approach");
    }
}

TEST(hamlets_are_planned_in_id_order_and_each_stands_on_its_own_ground) {
    const Built built = world_with_sites(1, CityClass::Medium);
    std::vector<HamletPin> pins;
    // Ids descending, positions ascending: the output order must come from the ids.
    for (int k = 0; k < 3; ++k) {
        pins.push_back(pin_beside(built, "hamlet-" + std::to_string(2 - k), "farming", 1 + k));
    }
    const HamletPlans plans = fill_hamlets(built.world, *built.grid, built.sites, pins);
    check::equal(plans.hamlets.size(), pins.size(), "every pin to be planned");
    for (std::size_t i = 1; i < plans.hamlets.size(); ++i) {
        check::is_true(plans.hamlets[i - 1].hamlet_id < plans.hamlets[i].hamlet_id,
                       "plans to come out in id order");
    }
    std::size_t with_plots = 0;
    for (const HamletPlan& plan : plans.hamlets) {
        if (!plan.plots.empty()) ++with_plots;
        CellSet taken(plan.terrain.size);
        for (const Plot& plot : plan.plots) {
            for (Cell c : plot_cells(plot, plan.half_m)) {
                check::is_true(taken.insert(c), plot.id + " to stand on ground no neighbour holds");
            }
        }
    }
    check::is_true(with_plots > 0, "at least one hamlet to have been built at all");
}

TEST(a_hamlet_keeps_its_streets_when_its_ordinal_renumbers) {
    // The ordinal id renumbers on every age advance; the ground does not. Planned under two
    // ordinals the same hamlet lays out the same streets, and under a different uid it does
    // not -- or the uid is not what decides them.
    const Built built = world_with_sites(1, CityClass::Medium);
    const HamletPin pin = pin_beside(built, "hamlet-0", "farming", 2);
    const HamletPlan first = plan_hamlet(built.world, *built.grid, pin, built.sites, {pin});
    check::is_true(!first.street_paths_m.empty(), "a hamlet with streets, or nothing is tested");
    check::equal(first.hamlet_uid, pin.uid, "the plan to carry the stable key");

    const auto same_streets = [&](const HamletPlan& other) {
        if (other.street_paths_m.size() != first.street_paths_m.size()) return false;
        for (std::size_t i = 0; i < first.street_paths_m.size(); ++i) {
            const auto& a = first.street_paths_m[i];
            const auto& b = other.street_paths_m[i];
            if (a.size() != b.size()) return false;
            for (std::size_t k = 0; k < a.size(); ++k) {
                if (a[k].x != b[k].x || a[k].z != b[k].z) return false;
            }
        }
        return true;
    };

    HamletPin renumbered = pin;
    renumbered.id = "hamlet-7";
    check::is_true(same_streets(plan_hamlet(built.world, *built.grid, renumbered, built.sites, {renumbered})),
                   "the same streets under a new ordinal");

    HamletPin rekeyed = pin;
    rekeyed.uid = "hamlet-node-999999";
    check::is_true(!same_streets(plan_hamlet(built.world, *built.grid, rekeyed, built.sites, {rekeyed})),
                   "different streets under a different uid");

    HamletPin unkeyed = pin;
    unkeyed.uid.clear();
    check::throws([&] { plan_hamlet(built.world, *built.grid, unkeyed, built.sites, {unkeyed}); },
                  "a pin with no uid to be refused");
}

TEST(a_hamlet_on_unbuildable_ground_says_so_rather_than_packing_it) {
    const Built built = world_with_sites(1, CityClass::Medium);
    // Two pins one grid cell apart crop each other down to the 4 m floor, which is one
    // cell of ground: far too little to place anything.
    HamletPin a = pin_beside(built, "hamlet-0", "farming");
    HamletPin b = pin_beside(built, "hamlet-1", "farming");
    const std::vector<HamletPin> pins = {a, b};
    const HamletPlan plan = plan_hamlet(built.world, *built.grid, a, built.sites, pins);

    check::equal(plan.half_m, 4.0, "the window to have been cropped to the floor");
    check::equal(std::string(plan.status), std::string("unbuildable"), "and to say it is unbuildable");
    check::is_true(plan.plots.empty(), "with nothing built");
    check::is_true(!plan.unplaced.empty(), "and every programme row reported");
    for (const Unplaced& row : plan.unplaced) {
        check::equal(row.reason, std::string("No compatible buildable footprint"), "a stated reason");
    }
    // The floor: the same pin with room does build something, so the refusal above is
    // the ground and not the planner.
    const HamletPlan roomy = plan_hamlet(built.world, *built.grid, a, built.sites, {a});
    check::is_true(roomy.half_m > 4.0, "a lone pin to keep its window");
    check::is_true(!roomy.plots.empty(), "and to build on it");
}
