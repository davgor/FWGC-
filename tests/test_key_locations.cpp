// The charged places a finished world implies, and the ground they stand on.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <set>
#include <string>
#include <vector>

#include "fwg/key_locations.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

constexpr double kPi = std::numbers::pi;

// A world carrying every layer the catalogue asks for.
//
// The generator reaches stage 11, which leaves seven referenced layers unwritten --
// coastal_exposure, metal_richness, rocky_coast, freshwater_distance, wetland_distance,
// culture_region and fishing_productivity all arrive with stages the ladder has not
// ported. They are synthesised here rather than left out, because a fixture missing them
// would test the refusal path for a third of the catalogue and nothing else. Two tests
// below deliberately take them away again to pin exactly that refusal.
struct Fixture {
    World world;
    std::map<std::string, Grid> layers;
    KeyLocationRequest request;
};

double wrapped(double v) { return v - std::floor(v); }

void add_synthetic_layers(const World& world, std::map<std::string, Grid>& layers) {
    const int n = world.config.size;
    const Grid& water = layers.at("water_type");
    const Grid& height = layers.at("height");

    // The real definition: the share of a cell's eight neighbours that are water, so the
    // smallest positive value is 1/8 and `min: 0.125` admits exactly the cells with one.
    Grid exposure(n, 0.0);
    Grid rocky(n, 0.0);
    Grid fishing(n, 0.0);
    Grid region(n, -1.0);
    Grid metal(n, 0.0);
    Grid fresh(n, 0.0);
    Grid wet(n, 0.0);
    for (int z = 1; z < n - 1; ++z) {
        for (int x = 0; x < n - 1; ++x) {
            int wet_neighbours = 0;
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dz == 0) continue;
                    const int nz = std::max(0, std::min(n - 1, z + dz));
                    const int nx = ((x + dx) % (n - 1) + (n - 1)) % (n - 1);
                    if (water.at(nx, nz) != 0.0) ++wet_neighbours;
                }
            }
            exposure.at(x, z) = wet_neighbours / 8.0;
            rocky.at(x, z) = wet_neighbours > 0 && height.at(x, z) > 40.0 ? 0.6 : 0.0;
            fishing.at(x, z) = water.at(x, z) != 0.0 ? 0.2 + 0.7 * wrapped(x * 0.37 + z * 0.11) : 0.0;
            // Two owners with a real border between them, so the frontier field has
            // something to find and the march-stone chain has something to walk.
            region.at(x, z) = water.at(x, z) != 0.0 ? -1.0 : (x < (n - 1) / 2 ? 0.0 : 1.0);
            metal.at(x, z) = 0.15 + 0.53 * wrapped(x * 0.211 + z * 0.093);
            fresh.at(x, z) = 400.0 * wrapped(x * 0.07 + z * 0.29);
            wet.at(x, z) = 900.0 * wrapped(x * 0.13 + z * 0.41);
        }
    }
    for (Grid* grid : {&exposure, &rocky, &fishing, &region, &metal, &fresh, &wet}) {
        for (int z = 0; z < n; ++z) grid->at(n - 1, z) = grid->at(0, z);
    }
    layers.emplace("coastal_exposure", std::move(exposure));
    layers.emplace("rocky_coast", std::move(rocky));
    layers.emplace("fishing_productivity", std::move(fishing));
    layers.emplace("culture_region", std::move(region));
    layers.emplace("metal_richness", std::move(metal));
    layers.emplace("freshwater_distance", std::move(fresh));
    layers.emplace("wetland_distance", std::move(wet));
}

KeyLocationPlace place(const std::string& id, const std::string& name, int x, int z, int n,
                       const SphereGrid& grid) {
    KeyLocationPlace p;
    p.id = id;
    p.name = name;
    p.x = x;
    p.z = z;
    p.node = grid.node_index(x, z);
    p.direction = direction(x, z, n);
    return p;
}

Fixture build_fixture(int size = 33, std::uint32_t seed = 42) {
    Fixture f;
    Request r;
    r.seed = seed;
    r.size = size;
    r.phase = 11;
    f.world = generate(resolve(r));
    f.layers = f.world.layers;
    add_synthetic_layers(f.world, f.layers);

    const int n = f.world.config.size;
    const auto grid = sphere_grid(n, f.world.effective.globe_radius);

    f.request.seed = seed;
    f.request.n = n;
    // EFFECTIVE, never config: they differ by the world-scale factor and every reach in
    // the catalogue is absolute metres.
    f.request.radius_m = f.world.effective.globe_radius;
    f.request.spacing_m = f.world.spacing_m;
    f.request.ages = 3;
    f.request.layers = &f.layers;

    int index = 0;
    for (const SettlementSite& site : f.world.settlements.sites) {
        KeyLocationPlace city = place("city-" + std::to_string(site.id), "Hold " + std::to_string(index),
                                      site.x, site.z, n, *grid);
        city.founded_age = 1;
        city.civilization_id = site.population_profile;
        city.source_culture = site.population_profile;
        f.request.cities.push_back(std::move(city));
        ++index;
    }
    for (const Road& road : f.world.roads.routes) {
        KeyLocationRoute route;
        route.from = std::to_string(road.from);
        route.to = std::to_string(road.to);
        route.nodes = road.nodes;
        f.request.routes.push_back(std::move(route));
    }
    // A pair of ruins and a nest, so near_ruin, near_nest and the origin join have
    // something to resolve. Without them every site ages off the nearest city instead and
    // three context flags are permanently false.
    const Grid& water = f.layers.at("water_type");
    int placed = 0;
    for (int z = 2; z < n - 2 && placed < 3; z += 3) {
        for (int x = 0; x < n - 1 && placed < 3; x += 5) {
            if (water.at(x, z) != 0.0) continue;
            KeyLocationPlace ruin = place("ruin-" + std::to_string(placed),
                                          "Old Kelnar " + std::to_string(placed), x, z, n, *grid);
            ruin.founded_age = 1;
            ruin.source_culture = "human_heartland";
            ruin.civilization_id = "human_heartland";
            f.request.ruins.push_back(std::move(ruin));
            KeyLocationPlace nest = place("nest-" + std::to_string(placed), "", x, z + 1, n, *grid);
            nest.tier = 3;
            f.request.nests.push_back(std::move(nest));
            ++placed;
        }
    }
    f.request.gods.push_back({"god_weave", "The Weave"});
    f.request.gods.push_back({"god_red_field", "The Red Field"});
    f.request.magic_school_order = {"weave", "umbral", "infernal", "radiant"};
    f.request.natural_biomes.push_back({4, "Temperate forest", ""});
    f.request.magical_biomes.push_back({0, "Weave-touched forest", "terrain.variant.000"});
    return f;
}

const Fixture& reference() {
    static const Fixture f = build_fixture();
    return f;
}

const KeyLocationBlock& reference_block() {
    static const KeyLocationBlock block = generate_key_locations(reference().request);
    return block;
}

const KeyLocationArchetype& archetype_by_id(std::string_view id) {
    const KeyLocationArchetype* a = key_location_archetype(id);
    if (a == nullptr) check::fail("the catalogue has no archetype " + std::string(id));
    return *a;
}

// A one-entry weight map is a weighted draw with no choice in it, which is exactly what a
// state test wants when it is pinning a bend rather than a distribution.
KeyLocationShapingPlacement shaping(KeyLocationShape shape, double radius, double height,
                                    double depth, double x = 0.0, double z = 0.0) {
    KeyLocationShapingPlacement p;
    p.spec.present = true;
    p.spec.shape = shape;
    p.spec.has_radius = true;
    p.spec.radius_m = radius;
    p.spec.height_m = height;
    p.spec.depth_m = depth;
    p.x_m = x;
    p.z_m = z;
    return p;
}

}  // namespace

// ---------------------------------------------------------------------------
// The catalogue itself.
// ---------------------------------------------------------------------------

TEST(the_catalogue_carries_every_archetype_family_part_and_kit) {
    check::equal(key_location_archetypes().size(), std::size_t(97), "archetypes");
    check::equal(key_location_families().size(), std::size_t(11), "families");
    check::equal(key_location_parts().size(), std::size_t(75), "exterior parts");
    check::equal(key_location_family_kits().size(), std::size_t(11), "family kits");
    check::equal(key_location_archetype_kits().size(), std::size_t(91), "archetype kits");
    check::equal(key_location_succession().size(), std::size_t(9), "succession rules");
    check::equal(key_location_chain_specs().size(), std::size_t(4), "chain specs");
    check::equal(key_location_cluster_specs().size(), std::size_t(4), "cluster specs");
    check::equal(key_location_adjectives().size(), std::size_t(24), "adjectives");
    check::equal(key_location_ordinals().size(), std::size_t(12), "ordinals");
}

TEST(every_archetype_resolves_a_kit_and_every_tier_two_declares_an_interior) {
    int tier_two = 0;
    int composed = 0;
    for (const KeyLocationArchetype& a : key_location_archetypes()) {
        check::is_true(key_location_kit_for(a.id, a.family) != nullptr,
                       "a kit for " + std::string(a.id));
        check::is_true(key_location_family(a.family) != nullptr,
                       "a family for " + std::string(a.id));
        if (a.tier == 2) {
            ++tier_two;
            check::is_true(a.has_interior, std::string(a.id) + " to declare an interior");
        }
        if (a.tier == 0) check::is_true(!a.has_interior, std::string(a.id) + " to declare none");
        if (a.placement != KeyLocationPlacement::Node) ++composed;
    }
    check::is_true(tier_two >= 20, "the catalogue to carry tier-2 complexes at all");
    check::equal(composed, 7, "path and cluster archetypes");
}

TEST(every_family_carries_the_display_fields_the_block_republishes) {
    // The families table is republished whole, so a family losing its glyph or colour is a
    // silent hole in the lab's legend rather than an error anywhere.
    for (const KeyLocationFamily& family : key_location_families()) {
        check::is_true(!family.name.empty(), std::string(family.id) + " to have a display name");
        check::is_true(!family.glyph.empty(), std::string(family.id) + " to have a glyph");
        check::is_true(family.family_spacing_cells > 0.0,
                       std::string(family.id) + " to have a positive family spacing");
    }
}

// ---------------------------------------------------------------------------
// Fields.
// ---------------------------------------------------------------------------

TEST(percentile_is_nearest_rank_and_never_interpolates) {
    const std::vector<double> values{0.0, 1.0, 2.0, 3.0};
    // An interpolating percentile answers 1.5 at the median; nearest-rank must answer a
    // value that is actually in the list.
    const double half = key_location_percentile(values, 0.5);
    check::is_true(std::find(values.begin(), values.end(), half) != values.end(),
                   "the median to be a real sample");
    check::equal(half, 2.0, "nearest rank at q=0.5 over four samples");
    check::equal(key_location_percentile(values, 0.0), 0.0, "the floor");
    check::equal(key_location_percentile(values, 1.0), 3.0, "the ceiling");
    check::equal(key_location_percentile({7.0}, 0.8), 7.0, "a single sample");
}

TEST(a_distance_field_with_no_sources_reads_the_ceiling_everywhere) {
    const int n = 17;
    const Grid empty = key_location_distance_field({}, n, 1000.0);
    int cells = 0;
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            check::equal(empty.at(x, z), 1000.0 * n, "the ceiling at every cell");
            ++cells;
        }
    }
    check::equal(cells, n * n, "every cell to have been checked");
    // ...and a world that HAS a source must not read the ceiling, or the test above is
    // passing on a field that is constant for the wrong reason.
    const Grid one = key_location_distance_field({{4, 4}}, n, 1000.0);
    check::equal(one.at(4, 4), 0.0, "zero at the source");
    check::is_true(one.at(4, 5) < 1000.0 * n, "and less than the ceiling next to it");
}

TEST(a_distance_field_wraps_the_seam_and_seals_it) {
    const int n = 17;
    const Grid field = key_location_distance_field({{0, 8}}, n, 100.0);
    // Column n-2 is one step west of column 0 across the seam, not n-2 steps east.
    check::close(field.at(n - 2, 8), 100.0, 1e-9, "one step across the seam");
    for (int z = 0; z < n; ++z) {
        check::equal(field.at(n - 1, z), field.at(0, z), "the seam column to mirror column 0");
    }
    for (int x = 0; x < n; ++x) {
        check::equal(field.at(x, 0), field.at(0, 0), "a flat north pole row");
        check::equal(field.at(x, n - 1), field.at(0, n - 1), "a flat south pole row");
    }
    // A diagonal step costs sqrt(2) and not 1, which is what makes these distances
    // slightly non-Euclidean in the way every threshold was tuned against.
    check::close(field.at(1, 9), 100.0 * std::numbers::sqrt2, 1e-9, "a diagonal step");
}

TEST(unowned_ground_is_not_a_political_border) {
    const int n = 17;
    Grid region(n, -1.0);
    for (int z = 1; z < n - 1; ++z) {
        for (int x = 0; x < n - 1; ++x) region.at(x, z) = 0.0;
    }
    for (int z = 0; z < n; ++z) region.at(n - 1, z) = region.at(0, z);
    const Grid none = key_location_frontier_field(&region, n);
    double total = 0.0;
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) total += none.at(x, z);
    }
    check::equal(total, 0.0, "one owner everywhere to make no border");

    // Now carve out a block of unowned ground. It must STILL make no border, or every
    // coastline in the world reads as a political frontier.
    Grid holed = region;
    for (int z = 4; z < 8; ++z) {
        for (int x = 4; x < 8; ++x) holed.at(x, z) = -1.0;
    }
    for (int z = 0; z < n; ++z) holed.at(n - 1, z) = holed.at(0, z);
    const Grid still_none = key_location_frontier_field(&holed, n);
    double holed_total = 0.0;
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) holed_total += still_none.at(x, z);
    }
    check::equal(holed_total, 0.0, "unowned ground not to count as another owner");

    // The floor: two real owners DO make a border, so the two zeros above are a rule and
    // not a field that is always empty.
    Grid split = region;
    for (int z = 1; z < n - 1; ++z) {
        for (int x = 8; x < n - 1; ++x) split.at(x, z) = 1.0;
    }
    for (int z = 0; z < n; ++z) split.at(n - 1, z) = split.at(0, z);
    const Grid real = key_location_frontier_field(&split, n);
    double border = 0.0;
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) border += real.at(x, z);
    }
    check::is_true(border > 0.0, "two owners to make a border");
}

TEST(the_coastal_field_takes_the_best_water_in_reach_and_not_the_mean) {
    const int n = 17;
    Grid water(n, 0.0);
    Grid source(n, 0.0);
    // One land cell with two water neighbours of very different worth.
    water.at(5, 8) = 1.0;
    water.at(4, 7) = 1.0;
    source.at(5, 8) = 0.9;
    source.at(4, 7) = 0.1;
    const Grid field = key_location_coastal_water_field(&source, &water, n);
    check::equal(field.at(4, 8), 0.9, "the richest water in reach of a boat");
    check::is_true(field.at(4, 8) != 0.5, "not the mean of its neighbours");
    check::equal(field.at(5, 8), 0.0, "a water cell to read zero");
    check::equal(field.at(12, 8), 0.0, "an inland cell to read zero");
}

// ---------------------------------------------------------------------------
// Placement.
// ---------------------------------------------------------------------------

TEST(a_percentile_gate_that_admits_the_whole_domain_refuses_the_archetype) {
    // holy_well asks for the nearest 35 per cent of freshwater_distance. Flatten that layer
    // and the cut equals the one value present, every cell clears it, and a naive port
    // reports `placed` with the archetype's defining requirement voided.
    Fixture f = build_fixture();
    Grid& fresh = f.layers.at("freshwater_distance");
    for (int z = 0; z < f.request.n; ++z) {
        for (int x = 0; x < f.request.n; ++x) fresh.at(x, z) = 0.0;
    }
    const KeyLocationBlock block = generate_key_locations(f.request);
    check::is_true(block.ok, "the block still to report ok");
    bool found = false;
    for (const KeyLocationDiagnostic& row : block.diagnostics) {
        if (row.archetype != "holy_well") continue;
        found = true;
        check::equal(row.placed, 0, "holy_well to place nothing");
        check::equal(row.candidates, 0, "and to admit no candidate");
        check::is_true(row.reason.find("freshwater_distance") != std::string::npos,
                       "the diagnostic to name the layer, got: " + row.reason);
        check::is_true(row.reason.find("carries no data") != std::string::npos,
                       "and to say the field carries no data, got: " + row.reason);
    }
    check::is_true(found, "a holy_well diagnostic to exist at all");
    for (const KeyLocationSite& site : block.sites) {
        check::is_true(site.kind != "holy_well", "no holy_well to have been placed");
    }
}

TEST(a_missing_layer_refuses_the_archetype_and_the_diagnostic_names_it) {
    Fixture f = build_fixture();
    f.layers.erase("metal_richness");
    const KeyLocationBlock block = generate_key_locations(f.request);
    check::is_true(block.ok, "the block still to report ok");
    int named = 0;
    for (const KeyLocationDiagnostic& row : block.diagnostics) {
        if (row.reason == "the world never generated the metal_richness layer") ++named;
    }
    check::is_true(named > 0, "at least one archetype to be refused by name");
    for (const KeyLocationSite& site : block.sites) {
        check::is_true(site.kind != "iron_mine" && site.kind != "silver_mine",
                       "no mine to stand on a layer the world does not carry");
    }
}

TEST(an_absolute_floor_on_an_empty_field_fails_closed_rather_than_open) {
    // The asymmetry this whole gate exists for. border_fort requires frontier >= 0.5 with
    // NO percentile beside it, so a world with no frontier admits nothing and says so --
    // where a percentile over the same empty field would admit everything and report
    // `placed`.
    Fixture f = build_fixture();
    Grid& region = f.layers.at("culture_region");
    for (int z = 0; z < f.request.n; ++z) {
        for (int x = 0; x < f.request.n; ++x) region.at(x, z) = 0.0;
    }
    const KeyLocationBlock block = generate_key_locations(f.request);
    bool found = false;
    for (const KeyLocationDiagnostic& row : block.diagnostics) {
        if (row.archetype != "border_fort") continue;
        found = true;
        check::equal(row.candidates, 0, "no cell to clear an absolute frontier floor");
        check::equal(row.reason, std::string("no ground in this world satisfies the requirements"),
                     "the closed-failure reason");
    }
    check::is_true(found, "a border_fort diagnostic");

    // The floor for the floor: with a real border it DOES admit cells, so the refusal
    // above is a property of the flattened field and not of the archetype.
    const KeyLocationBlock real = reference_block();
    bool admitted = false;
    for (const KeyLocationDiagnostic& row : real.diagnostics) {
        if (row.archetype == "border_fort" && row.candidates > 0) admitted = true;
    }
    check::is_true(admitted, "a world with two cultures to admit border forts");
}

TEST(the_budget_thins_a_fractional_expectation_instead_of_rounding_it_away) {
    // An expectation of 0.4 rounds to zero at every seed and every raster, and the whole
    // catalogue rounds to zero with it. Thinned, it must come out positive about two fifths
    // of the time -- which is the difference between a small world growing a few notable
    // places and growing none.
    const KeyLocationArchetype& cave = archetype_by_id("karst_cave");
    check::is_true(cave.per_1000_km2 > 0.0, "a positive rate to thin");
    check::is_true(!cave.has_min_count && !cave.has_max_count, "and no floor or ceiling on it");
    const double land_km2 = 0.4 * 1000.0 / cave.per_1000_km2;
    int positive = 0;
    const int trials = 400;
    for (int seed = 0; seed < trials; ++seed) {
        Rng draw = Rng::child(static_cast<std::uint64_t>(seed), "keyloc-place-karst_cave");
        const int count = key_location_budget(cave, land_km2, draw);
        check::equal(draw.draws(), std::uint64_t(1), "exactly one word spent on a budget");
        check::between(count, 0, 1, "a sub-one expectation to yield zero or one");
        positive += count;
    }
    check::between(positive / static_cast<double>(trials), 0.32, 0.48,
                   "a fractional expectation to land positive at about its own rate");

    // And an integral expectation still spends its one word, so an archetype's stream
    // length never depends on how much land the world happened to grow.
    Rng exact = Rng::child(3, "keyloc-place-karst_cave");
    check::equal(key_location_budget(cave, 3.0 * 1000.0 / cave.per_1000_km2, exact), 3,
                 "an expectation of exactly three");
    check::equal(exact.draws(), std::uint64_t(1), "and one word spent on it anyway");
}

TEST(a_min_count_is_applied_before_a_max_count_and_the_result_is_floored_at_zero) {
    // The five wonders ship as min 0 / max 1: "at most one of these in a world".
    const KeyLocationArchetype& star = archetype_by_id("fallen_star");
    check::is_true(star.has_min_count && star.has_max_count, "a wonder to carry both bounds");
    check::equal(star.min_count, 0, "the floor");
    check::equal(star.max_count, 1, "the ceiling");
    for (int seed = 0; seed < 64; ++seed) {
        Rng draw = Rng::child(static_cast<std::uint64_t>(seed), "keyloc-place-fallen_star");
        // A continent's worth of land: without the ceiling this would want dozens.
        const int count = key_location_budget(star, 400000.0, draw);
        check::between(count, 0, 1, "a wonder capped at one however much land there is");
    }
    Rng huge = Rng::child(9, "probe");
    check::equal(key_location_budget(star, 400000.0, huge), 1, "and at least one on a continent");

    // On the SHIPPED catalogue the order of the two clamps is unobservable, because every
    // authored floor is 0 and max(min(c, 1), 0) and min(max(c, 0), 1) agree for every
    // non-negative c. An author who writes a floor above the ceiling is the only case that
    // separates them, so that is what this pins: floor first, then ceiling, so the CEILING
    // is what survives. Swap them and this archetype wants three.
    KeyLocationArchetype crossed = star;
    crossed.min_count = 3;
    crossed.max_count = 1;
    Rng probe = Rng::child(5, "keyloc-place-crossed");
    check::equal(key_location_budget(crossed, 400000.0, probe), 1,
                 "the ceiling to be applied last and therefore to win");

    // And the result is floored at zero, so a negative ceiling cannot produce a negative
    // count that a caller would read as "place nothing" only by accident.
    KeyLocationArchetype negative = star;
    negative.has_min_count = false;
    negative.max_count = -4;
    Rng below = Rng::child(5, "keyloc-place-negative");
    check::equal(key_location_budget(negative, 400000.0, below), 0, "never below zero");
}

TEST(a_claimed_node_is_skipped_before_its_jitter_draw) {
    // Read the direction carefully, because it is the opposite of what it first looks like.
    // The skip precedes the draw, so a claimed candidate SPENDS NO WORD -- which means every
    // candidate after it in enumeration order takes the word the claimed one would have had,
    // and the whole archetype's jitter sequence shifts up by one.
    //
    // So claiming a cell that nothing had chosen anyway must still visibly change the world.
    // Move the skip to AFTER the draw and the claimed candidate spends its word, every later
    // candidate keeps the one it had, and removing an unchosen cell from the ranking becomes
    // completely invisible -- the world comes out byte-identical. That is what this test
    // separates, and it is the one difference a frequency check could never see.
    Fixture f = build_fixture();
    const KeyLocationBlock before = generate_key_locations(f.request);
    check::is_true(before.sites.size() >= 20, "a world with enough sites for this to mean anything");

    const auto grid = sphere_grid(f.request.n, f.request.radius_m);
    const Grid& water = f.layers.at("water_type");
    std::set<int> used;
    for (const KeyLocationSite& site : before.sites) used.insert(site.node);
    for (const KeyLocationPlace& record : f.request.ruins) used.insert(*record.node);
    for (const KeyLocationPlace& record : f.request.nests) used.insert(*record.node);

    // An EARLY land cell in row-major enumeration order that no site stands on: early so
    // that a post-draw skip would shift nearly every later candidate, unused so that the
    // correct implementation changes nothing at all.
    int claimed_node = -1;
    for (int z = 1; z < f.request.n - 1 && claimed_node < 0; ++z) {
        for (int x = 0; x < f.request.n - 1; ++x) {
            const int node = grid->node_index(x, z);
            if (water.at(x, z) == 0.0 && used.count(node) == 0) {
                claimed_node = node;
                break;
            }
        }
    }
    check::is_true(claimed_node >= 0, "an unused land node to claim");

    KeyLocationPlace landmark;
    landmark.id = "landmark-0";
    landmark.node = claimed_node;
    f.request.landmarks.push_back(landmark);
    const KeyLocationBlock after = generate_key_locations(f.request);

    std::set<std::string> was;
    for (const KeyLocationSite& site : before.sites) was.insert(site.id);
    int moved = 0;
    for (const KeyLocationSite& site : after.sites) {
        check::is_true(site.node != claimed_node, "nothing to stand on claimed ground");
        if (was.count(site.id) == 0) ++moved;
    }
    check::is_true(moved > 5,
                   "claiming an unchosen cell to shift the jitter stream after it, moved only " +
                       std::to_string(moved));
    // The floor: the run is still a real world and not a failure that trivially differs.
    check::is_true(after.ok, "the claimed run to report ok");
    check::is_true(after.sites.size() >= 20, "and still to grow a catalogue of places");
}

TEST(spacing_keeps_two_of_an_archetype_apart) {
    const Fixture& f = reference();
    const KeyLocationBlock& block = reference_block();
    std::map<std::string, std::vector<Vec3>> by_kind;
    for (const KeyLocationSite& site : block.sites) {
        if (site.placement != KeyLocationPlacement::Node) continue;
        by_kind[site.kind].push_back(site.direction);
    }
    const double reference_cell = 2.0 * kPi * f.request.radius_m / (kKeyLocationReferenceRaster - 1);
    int checked = 0;
    for (const auto& [kind, points] : by_kind) {
        const KeyLocationArchetype& a = archetype_by_id(kind);
        const double spacing = a.has_spacing_m ? a.spacing_m : a.spacing_cells * reference_cell;
        if (spacing <= 0.0 || points.size() < 2) continue;
        for (std::size_t i = 0; i < points.size(); ++i) {
            for (std::size_t j = i + 1; j < points.size(); ++j) {
                const double gap =
                    f.request.radius_m * std::acos(clamp_unit(dot(points[i], points[j])));
                check::is_true(gap >= spacing - 1e-6,
                               kind + " sites " + std::to_string(gap) + " m apart, under " +
                                   std::to_string(spacing));
                ++checked;
            }
        }
    }
    check::is_true(checked > 0, "at least one archetype to have placed two sites to compare");
}

TEST(nothing_stands_on_a_pole_or_on_the_seam_column) {
    const KeyLocationBlock& block = reference_block();
    const int n = reference().request.n;
    int scattered = 0;
    for (const KeyLocationSite& site : block.sites) {
        if (site.placement != KeyLocationPlacement::Node) continue;
        ++scattered;
        check::is_true(site.z >= 1 && site.z <= n - 2,
                       "a scattered site off the poles, got z=" + std::to_string(site.z));
        check::is_true(site.x >= 0 && site.x <= n - 2,
                       "a scattered site off the seam, got x=" + std::to_string(site.x));
    }
    check::is_true(scattered > 0, "the world to have scattered something");
}

// ---------------------------------------------------------------------------
// State, occupant, succession and threat.
// ---------------------------------------------------------------------------

TEST(a_water_domain_archetype_is_drowned_without_spending_a_draw) {
    const KeyLocationArchetype& drowned = archetype_by_id("drowned_village");
    check::is_true(drowned.domain != KeyLocationDomain::Land, "a water archetype to test");
    bool declares = false;
    for (const KeyLocationWeight& w : drowned.states) {
        if (w.name == "drowned") declares = true;
    }
    check::is_true(declares, "drowned_village to declare the drowned state");

    // A genuine branch in the STREAM LENGTH, not a shortcut: the occupant draw that follows
    // reads the FIRST word rather than the second, so spending one here would shift every
    // later draw for the site.
    KeyLocationContext context;
    context.ages = 3;
    Rng draw = Rng::child(11, "keyloc-site-drowned_village-42");
    check::equal(std::string(key_location_derive_state(drowned, context, draw)),
                 std::string("drowned"), "a water-domain site to be drowned");
    check::equal(draw.draws(), std::uint64_t(0), "and no word spent deciding it");

    // The floor, and the sharpest one available: `crannog` is ALSO a water-domain archetype
    // and does NOT declare `drowned`, so it falls through to the weighted pick and spends
    // its word. The zero above is the declared-state branch, not a function that never
    // draws and not a property of the water domain.
    const KeyLocationArchetype& crannog = archetype_by_id("crannog");
    check::is_true(crannog.domain != KeyLocationDomain::Land, "crannog to be a water archetype");
    Rng lake = Rng::child(11, "keyloc-site-crannog-42");
    const std::string_view state = key_location_derive_state(crannog, context, lake);
    check::is_true(state != "drowned", "a water archetype that never declares drowned");
    check::is_true(lake.draws() >= 1, "to spend its state draw like any other");

    // And every water-domain site that reaches a world is in that state.
    int seen = 0;
    for (const KeyLocationSite& s : reference_block().sites) {
        const KeyLocationArchetype& at = archetype_by_id(s.kind);
        if (at.domain == KeyLocationDomain::Land || at.domain == KeyLocationDomain::Any) continue;
        bool has_drowned = false;
        for (const KeyLocationWeight& w : at.states) {
            if (w.name == "drowned") has_drowned = true;
        }
        if (!has_drowned) continue;
        check::equal(s.state, std::string("drowned"), s.kind + " to be drowned");
        ++seen;
    }
    check::is_true(seen > 0, "at least one water-domain site to have been placed");
}

TEST(a_weighted_choice_walks_sorted_keys_so_authoring_order_cannot_move_it) {
    // A frequency test CANNOT see this: a reversed or unsorted cumulative walk preserves
    // every marginal probability. So the oracle here is an independently written sorted
    // walk, compared pick for pick on the same stream, plus a golden literal.
    const KeyLocationArchetype& karst = archetype_by_id("karst_cave");
    std::vector<std::string_view> declared;
    for (const KeyLocationWeight& w : karst.occupants) declared.push_back(w.name);
    check::equal(declared.size(), std::size_t(4), "karst_cave to declare four occupants");
    check::is_true(!std::is_sorted(declared.begin(), declared.end()),
                   "and to declare them out of sorted order, or the sort is unobservable");

    KeyLocationContext context;
    context.ages = 3;
    // `natural` is karst_cave's only state, so derive_occupant sees an untended state with
    // no near-nest or remote bend: exactly the archetype's own weights, sorted.
    std::vector<std::pair<std::string_view, double>> sorted;
    for (const KeyLocationWeight& w : karst.occupants) sorted.emplace_back(w.name, w.weight);
    std::sort(sorted.begin(), sorted.end());

    int agreed = 0;
    std::set<std::string_view> distinct;
    for (int seed = 0; seed < 200; ++seed) {
        Rng draw = Rng::child(static_cast<std::uint64_t>(seed), "keyloc-choose");
        Rng oracle_draw = Rng::child(static_cast<std::uint64_t>(seed), "keyloc-choose");
        const std::string_view got = key_location_derive_occupant(karst, "natural", context, draw);
        // The oracle: the same bends, then a SORTED cumulative walk.
        std::vector<std::pair<std::string_view, double>> bent = sorted;
        for (auto& [name, weight] : bent) {
            if (name == "builders") weight *= 0.2;
            if (name == "descendants") weight *= 0.4;
            if (name == "monsters") weight *= 0.6;  // not remote
        }
        double total = 0.0;
        for (const auto& [name, weight] : bent) total += weight;
        const double cut = oracle_draw.random() * total;
        std::string_view expected = bent.back().first;
        double running = 0.0;
        for (const auto& [name, weight] : bent) {
            running += weight;
            if (cut < running) {
                expected = name;
                break;
            }
        }
        check::equal(std::string(got), std::string(expected),
                     "the sorted cumulative walk at seed " + std::to_string(seed));
        distinct.insert(got);
        ++agreed;
    }
    check::equal(agreed, 200, "every seed compared");
    check::is_true(distinct.size() >= 3,
                   "the draw to actually reach several occupants, or the agreement is vacuous");
}

TEST(succession_records_what_moved_in_and_the_threat_follows_the_new_occupant) {
    const KeyLocationBlock& block = reference_block();
    int succeeded = 0;
    for (const KeyLocationSite& site : block.sites) {
        if (!site.succession) continue;
        ++succeeded;
        check::is_true(key_location_untended(site.state),
                       "succession only into an untended state, got " + site.state);
        check::is_true(site.succession->from_occupant != site.succession->to_occupant,
                       "a succession never to be a no-op");
        check::equal(site.occupant, site.succession->to_occupant,
                     "the occupant to have been replaced in place");
        check::is_true(!site.succession->note.empty(), "a succession to carry its family's note");
        // lair and wonder have no rule at all, so nothing may ever succeed into them.
        check::is_true(site.family != "lair" && site.family != "wonder",
                       "nothing to move into " + site.family);
    }
    check::is_true(succeeded > 0, "the world to have recorded at least one succession");
}

TEST(threat_stays_on_the_one_to_five_creature_scale) {
    const KeyLocationBlock& block = reference_block();
    std::set<int> seen;
    for (const KeyLocationSite& site : block.sites) {
        check::between(site.threat, kKeyLocationMinThreat, kKeyLocationMaxThreat,
                       "threat for " + site.id);
        seen.insert(site.threat);
    }
    check::is_true(seen.size() >= 2, "threat to actually vary across a world");
    check::is_true(!block.sites.empty(), "and for there to be sites to rate");
}

TEST(dungeons_are_counted_from_tier_and_a_hostile_occupant_and_nothing_else) {
    const KeyLocationBlock& block = reference_block();
    int expected = 0;
    for (const KeyLocationSite& site : block.sites) {
        if (site.tier >= 2 && key_location_hostile(site.occupant)) ++expected;
    }
    check::equal(block.summary.dungeons, expected, "the dungeon count");
    check::is_true(block.summary.sites > 0, "a non-empty world to count over");
}

// ---------------------------------------------------------------------------
// Interiors.
// ---------------------------------------------------------------------------

TEST(only_tier_two_sites_get_an_interior_and_chamber_zero_is_the_entrance) {
    const KeyLocationBlock& block = reference_block();
    int built = 0;
    for (const KeyLocationSite& site : block.sites) {
        if (site.tier == 2) {
            check::is_true(site.interior.has_value(), site.id + " to carry an interior");
        } else {
            check::is_true(!site.interior.has_value(), site.id + " to carry none");
        }
        if (!site.interior) continue;
        ++built;
        const KeyLocationInterior& interior = *site.interior;
        check::is_true(!interior.chambers.empty(), "an interior to have chambers");
        check::equal(interior.chambers.front().id, std::string("chamber-0"), "the first chamber id");
        check::equal(interior.chambers.front().depth, 0, "the entrance chamber's depth");
        check::equal(interior.chambers.front().role, std::string("entrance chamber"),
                     "the entrance chamber's role");
        check::is_true(interior.chambers.front().connects.empty(),
                       "the entrance chamber to hang off nothing");
        check::is_true(!interior.chambers.front().collapsed,
                       "chamber 0 never to be collapsed: the test short-circuits before its draw");
        check::is_true(!interior.plan_note.empty(), "a plan note");
        for (const KeyLocationChamber& chamber : interior.chambers) {
            check::between(chamber.depth, 0, interior.levels - 1, "chamber depth within levels");
            check::between(chamber.size_w_m, 3.0, 12.0, "chamber width");
            check::between(chamber.size_d_m, 3.0, 12.0, "chamber depth in metres");
            for (const std::string& link : chamber.connects) {
                check::is_true(link != chamber.id, "a chamber never to connect to itself");
            }
        }
        check::is_true(!interior.entrances.empty(), "at least one entrance");
        check::equal(interior.entrances.front().chamber, std::string("chamber-0"),
                     "the first entrance to be on the entrance chamber");
    }
    check::is_true(built > 0, "the world to have built at least one interior");
}

TEST(an_interior_spends_exactly_the_words_its_chamber_graph_accounts_for) {
    // Two of the short-circuits in this build are INVISIBLE IN THE OUTPUT and visible only
    // in the stream: chamber 0 takes neither a role draw nor a collapse draw, and an
    // `index > 0 &&` written the other way round produces the same graph while spending an
    // extra word per interior -- which silently moves every later draw for the site.
    //
    // A linear plan is the one whose word count is fully determined by its result: no
    // branch roll, no warren links, and no second entrance. So it is the one that can be
    // counted exactly.
    //
    //   2 for levels and the chamber count,
    //   chamber 0:  0 connect + 0 depth + 0 role + 2 size + 1 flooded + 0 collapsed = 3,
    //   chamber i:  0 connect + 1 depth + 1 role + 2 size + 1 flooded + 1 collapsed = 6.
    const KeyLocationArchetype& tube = archetype_by_id("lava_tube");
    check::is_true(tube.has_interior, "lava_tube to declare an interior");
    check::is_true(tube.interior_plan == KeyLocationPlan::Linear, "and for it to be linear");

    int checked = 0;
    for (int seed = 0; seed < 32; ++seed) {
        Rng draw = Rng::child(static_cast<std::uint64_t>(seed), "keyloc-site-lava_tube-1");
        const auto interior = key_location_build_interior(tube, "ruined", draw);
        check::is_true(interior.has_value(), "an interior");
        const auto chambers = static_cast<std::uint64_t>(interior->chambers.size());
        check::is_true(chambers >= 1, "at least one chamber");
        const std::uint64_t expected = 2 + 3 + 6 * (chambers - 1);
        check::equal(draw.draws(), expected,
                     "the exact word count for " + std::to_string(chambers) + " chambers");
        ++checked;
    }
    check::equal(checked, 32, "every seed counted");

    // The floor: a plan that DOES roll for its shape spends more than the linear formula,
    // so the equality above is a real account and not a number that happens to fit.
    const KeyLocationArchetype& warren = archetype_by_id("karst_cave");
    check::is_true(warren.interior_plan == KeyLocationPlan::Warren, "a warren to compare against");
    Rng loose = Rng::child(4, "keyloc-site-karst_cave-1");
    const auto dug = key_location_build_interior(warren, "ruined", loose);
    const auto dug_chambers = static_cast<std::uint64_t>(dug->chambers.size());
    check::is_true(loose.draws() > 2 + 3 + 6 * (dug_chambers - 1),
                   "a warren to spend more than a linear plan of the same size");
}

TEST(a_linear_interior_is_a_chain_and_a_vault_hangs_off_its_entrance) {
    const KeyLocationBlock& block = reference_block();
    int linear = 0;
    int vault = 0;
    for (const KeyLocationSite& site : block.sites) {
        if (!site.interior) continue;
        const KeyLocationInterior& interior = *site.interior;
        if (interior.plan == KeyLocationPlan::Linear) {
            ++linear;
            for (std::size_t i = 1; i < interior.chambers.size(); ++i) {
                check::equal(interior.chambers[i].connects.size(), std::size_t(1),
                             "one way on in a linear plan");
                check::equal(interior.chambers[i].connects.front(),
                             "chamber-" + std::to_string(i - 1), "a linear chain");
            }
        }
        if (interior.plan == KeyLocationPlan::Vault) {
            ++vault;
            for (std::size_t i = 1; i < std::min<std::size_t>(3, interior.chambers.size()); ++i) {
                check::equal(interior.chambers[i].connects.front(), std::string("chamber-0"),
                             "a vault's first rooms to open off the entrance");
            }
        }
    }
    check::is_true(linear > 0, "at least one linear interior to have been built");
    check::is_true(vault > 0, "at least one vault interior to have been built");
}

// ---------------------------------------------------------------------------
// Naming.
// ---------------------------------------------------------------------------

TEST(no_two_locations_in_a_world_carry_the_same_name) {
    const KeyLocationBlock& block = reference_block();
    std::set<std::string> names;
    std::set<std::string> ids;
    for (const KeyLocationSite& site : block.sites) {
        check::is_true(!site.name.empty(), site.id + " to have a name");
        check::is_true(names.insert(site.name).second, "a unique name, got a repeat: " + site.name);
        check::is_true(ids.insert(site.id).second, "a unique id, got a repeat: " + site.id);
        check::is_true(site.name.find('{') == std::string::npos,
                       "every template token resolved, got: " + site.name);
    }
    check::is_true(names.size() > 5, "enough names for uniqueness to mean anything");
}

TEST(the_adjective_draw_fires_even_for_a_template_that_never_asks_for_one) {
    // A stream-length rule with NO trace in the output. `lava_tube` carries no `{adj}` in
    // either of its templates, so drawing the adjective lazily would name it identically
    // and spend one word instead of two -- moving the succession roll, the threat inputs
    // and the whole interior build for every lava tube in the world.
    const KeyLocationArchetype& tube = archetype_by_id("lava_tube");
    for (std::string_view t : tube.names) {
        check::is_true(t.find("{adj}") == std::string_view::npos,
                       "lava_tube to want no adjective, got " + std::string(t));
    }
    KeyLocationNameFacts facts;
    facts.near = "Bargdorn";  // present, so no {near} redraw
    Rng draw = Rng::child(3, "keyloc-site-lava_tube-1");
    const std::string name = key_location_name_for(tube, facts, draw);
    check::is_true(!name.empty(), "a name");
    check::is_true(name.find('{') == std::string::npos, "with every token resolved");
    check::equal(draw.draws(), std::uint64_t(2),
                 "one word for the template and one for the adjective it never uses");

    // The floor: an archetype that DOES use `{adj}` spends the same two words, which is
    // the whole point -- the stream length does not depend on which template was drawn.
    const KeyLocationArchetype& cave = archetype_by_id("karst_cave");
    bool mixed = false;
    for (std::string_view t : cave.names) {
        if (t.find("{adj}") != std::string_view::npos) mixed = true;
    }
    check::is_true(mixed, "karst_cave to offer an adjective template");
    Rng other = Rng::child(3, "keyloc-site-karst_cave-1");
    key_location_name_for(cave, facts, other);
    check::equal(other.draws(), std::uint64_t(2), "the same two words");
}

TEST(a_template_that_needs_a_neighbour_it_has_not_got_is_redrawn_once) {
    // `lava_tube` offers one template naming `{near}` and one that does not, so a site with
    // no neighbour must be able to fall back rather than be named "the fire road under the
    // waste". The redraw is a THIRD word and only happens when it is needed.
    //
    // `sea_cave` is the counter-case and is why the `usable` list is tested for emptiness:
    // BOTH its templates name a neighbour, so there is nothing to fall back to and the
    // authored "the waste" is the right answer rather than a failure.
    const KeyLocationArchetype& cave = archetype_by_id("lava_tube");
    bool with_near = false;
    bool without_near = false;
    for (std::string_view t : cave.names) {
        if (t.find("{near}") != std::string_view::npos) with_near = true;
        else without_near = true;
    }
    check::is_true(with_near && without_near, "an archetype with a mixed template list");
    int redrawn = 0;
    for (int seed = 0; seed < 40; ++seed) {
        KeyLocationNameFacts none;  // no `near` at all
        Rng draw = Rng::child(static_cast<std::uint64_t>(seed), "keyloc-name");
        const std::string name = key_location_name_for(cave, none, draw);
        check::between(static_cast<double>(draw.draws()), 2.0, 3.0, "two or three words");
        if (draw.draws() == 3) {
            ++redrawn;
            check::is_true(name.find("the waste") == std::string::npos,
                           "a redrawn name to avoid the fallback neighbour, got " + name);
        }
    }
    check::is_true(redrawn > 0, "the redraw to actually fire across forty seeds");
}

TEST(a_chain_member_is_named_by_its_place_in_the_chain) {
    const KeyLocationBlock& block = reference_block();
    int ordinal_named = 0;
    int members = 0;
    for (const KeyLocationSite& site : block.sites) {
        if (site.placement != KeyLocationPlacement::Path) continue;
        ++members;
        check::is_true(!site.links.chain.empty(), "a path member to carry its chain");
        check::is_true(site.links.chain_index >= 0, "and its position in it");
        const KeyLocationArchetype& a = archetype_by_id(site.kind);
        bool offers_ordinal = false;
        for (std::string_view t : a.names) {
            if (t.find("{ordinal}") != std::string_view::npos) offers_ordinal = true;
        }
        if (!offers_ordinal) continue;
        for (std::string_view word : key_location_ordinals()) {
            if (site.name.find(word) != std::string::npos) ++ordinal_named;
        }
    }
    if (members > 0) {
        check::is_true(ordinal_named > 0,
                       "a run of chain furniture to count itself out rather than read as "
                       "unrelated stones");
    }
    check::is_true(!block.chains.empty() || members == 0,
                   "chain members only ever to arrive with a chain record");
}

// ---------------------------------------------------------------------------
// Composition.
// ---------------------------------------------------------------------------

TEST(a_chain_is_a_walk_in_order_and_never_shorter_than_two) {
    const KeyLocationBlock& block = reference_block();
    std::map<std::string, const KeyLocationSite*> by_id;
    for (const KeyLocationSite& site : block.sites) by_id.emplace(site.id, &site);
    check::is_true(!block.chains.empty(), "the world to have composed at least one chain");
    for (const KeyLocationChain& chain : block.chains) {
        check::is_true(chain.members.size() >= 2, "a chain of at least two members");
        check::is_true(chain.length_m > 0.0, "a chain with a length");
        for (std::size_t i = 0; i < chain.members.size(); ++i) {
            const auto found = by_id.find(chain.members[i]);
            check::is_true(found != by_id.end(), "every member to be an emitted site");
            check::equal(found->second->links.chain_index, static_cast<int>(i),
                         "members listed in walk order");
            check::equal(found->second->links.chain, chain.id, "and linked back to the chain");
        }
    }
}

TEST(a_cluster_member_stands_at_a_real_offset_from_its_anchor) {
    const KeyLocationBlock& block = reference_block();
    std::map<std::string, const KeyLocationSite*> by_id;
    for (const KeyLocationSite& site : block.sites) by_id.emplace(site.id, &site);
    int checked = 0;
    for (const KeyLocationCluster& cluster : block.clusters) {
        const auto anchor = by_id.find(cluster.anchor);
        check::is_true(anchor != by_id.end(), "the anchor to be an emitted site");
        const KeyLocationClusterSpec* spec = nullptr;
        for (const KeyLocationClusterSpec& row : key_location_cluster_specs()) {
            if (row.id == cluster.kind) spec = &row;
        }
        check::is_true(spec != nullptr, "the cluster to name a real spec");
        check::is_true(!cluster.members.empty(), "a cluster that produced nothing to be dropped");
        for (const std::string& id : cluster.members) {
            const auto member = by_id.find(id);
            check::is_true(member != by_id.end(), "every member to be an emitted site");
            const double gap = reference().request.radius_m *
                               std::acos(clamp_unit(dot(anchor->second->direction,
                                                        member->second->direction)));
            // Walked, not snapped: the offset has to land inside the authored band rather
            // than on whichever node the raster happened to allow.
            check::between(gap, spec->offset_near_m * 0.98, spec->offset_far_m * 1.02,
                           "a cluster member inside its authored offset band");
            check::equal(member->second->links.anchor, cluster.anchor, "linked back to the anchor");
            ++checked;
        }
    }
    if (!block.clusters.empty()) check::is_true(checked > 0, "members to have been checked");
}

TEST(the_emitted_site_order_interleaves_every_pass) {
    const KeyLocationBlock& block = reference_block();
    check::is_true(block.sites.size() > 1, "a world with sites to order");
    for (std::size_t i = 1; i < block.sites.size(); ++i) {
        const KeyLocationSite& a = block.sites[i - 1];
        const KeyLocationSite& b = block.sites[i];
        const auto key = [](const KeyLocationSite& s) {
            return std::make_tuple(s.family, s.kind, s.links.chain, s.links.chain_index, s.id);
        };
        check::is_true(key(a) < key(b), "sites in (family, kind, chain, index, id) order");
    }
}

TEST(a_diagnostic_row_exists_for_every_pass_and_the_sites_are_ground_truth) {
    const KeyLocationBlock& block = reference_block();
    check::is_true(!block.diagnostics.empty(), "diagnostics to have been emitted");
    // One row per scatter pass plus one per chain spec plus one per cluster spec.
    int scattered = 0;
    for (const KeyLocationArchetype& a : key_location_archetypes()) {
        if (a.placement == KeyLocationPlacement::Node) ++scattered;
    }
    check::equal(block.diagnostics.size(),
                 static_cast<std::size_t>(scattered) + key_location_chain_specs().size() +
                     key_location_cluster_specs().size(),
                 "one diagnostic per pass");
    int placed_total = 0;
    for (const KeyLocationDiagnostic& row : block.diagnostics) placed_total += row.placed;
    // Rows deliberately do NOT sum to the site count -- a composed archetype that also
    // scatters is counted twice -- so this asserts the inequality rather than equality.
    check::is_true(placed_total >= block.summary.sites,
                   "composed archetypes to be counted by both passes");
    check::equal(block.summary.sites, static_cast<int>(block.sites.size()), "the summary count");
}

// ---------------------------------------------------------------------------
// Geometry and shaping.
// ---------------------------------------------------------------------------

TEST(a_bearing_of_zero_walks_north_and_a_quarter_turn_walks_east) {
    const Vec3 origin{1.0, 0.0, 0.0};  // on the equator at longitude -pi
    const double radius = 6.0e6;
    const Vec3 north = key_location_offset(origin, 0.0, radius * 0.01, radius);
    check::is_true(north.y > 0.0, "bearing 0 to be local NORTH, not east");
    const Vec3 east = key_location_offset(origin, kPi / 2.0, radius * 0.01, radius);
    check::close(east.y, 0.0, 1e-9, "a quarter turn to stay on the equator");
    check::is_true(std::abs(east.z) > 0.0, "and to have moved along it");
    const double walked = radius * std::acos(clamp_unit(dot(origin, north)));
    check::close(walked, radius * 0.01, 1.0, "the distance actually walked");
}

TEST(cell_of_direction_inverts_the_cell_direction_it_was_given) {
    const int n = 33;
    int checked = 0;
    for (int z = 1; z < n - 1; ++z) {
        for (int x = 0; x < n - 1; ++x) {
            const auto [bx, bz] = key_location_cell_of_direction(direction(x, z, n), n);
            check::equal(bx, x, "round-tripped x");
            check::equal(bz, z, "round-tripped z");
            ++checked;
        }
    }
    check::equal(checked, (n - 2) * (n - 1), "every body cell round-tripped");
}

TEST(a_level_shape_moves_no_ground_as_a_delta_but_flattens_it_against_the_base) {
    KeyLocationShapingPlacement flat = shaping(KeyLocationShape::Level, 5.0, 0.0, 0.0);
    check::equal(key_location_shaping_delta(flat, 0.0, 0.0), 0.0,
                 "level to contribute nothing additively");
    const std::vector<KeyLocationShapingPlacement> entries{flat};
    // Base 10 m of ground, centre at 4 m: the centre of a levelled patch is pulled all the
    // way to the centre height, and the edge is untouched.
    check::close(key_location_shaped_height(entries, 0.0, 0.0, 10.0, 4.0), 4.0, 1e-9,
                 "the middle of a levelled patch");
    check::close(key_location_shaped_height(entries, 5.0, 0.0, 10.0, 4.0), 10.0, 1e-9,
                 "the edge of it");
    check::is_true(key_location_shaped_height(entries, 2.0, 0.0, 10.0, 4.0) < 10.0,
                   "and something in between");
}

TEST(a_rim_is_a_raised_ring_with_a_bowl_inside_it_not_a_mound_with_a_hole) {
    const KeyLocationShapingPlacement rim = shaping(KeyLocationShape::Rim, 10.0, 3.0, 5.0);
    const double centre = key_location_shaping_delta(rim, 0.0, 0.0);
    const double ring = key_location_shaping_delta(rim, 10.0, 0.0);
    const double outside = key_location_shaping_delta(rim, 15.0, 0.0);
    check::is_true(centre < 0.0, "a bowl at the centre, got " + std::to_string(centre));
    check::is_true(ring > 0.0, "a raised ring at the radius, got " + std::to_string(ring));
    check::equal(outside, 0.0, "nothing past 1.4 radii");
    check::is_true(ring > centre, "the ring to stand above the bowl");

    const KeyLocationShapingPlacement mound = shaping(KeyLocationShape::Mound, 10.0, 3.0, 0.0);
    check::is_true(key_location_shaping_delta(mound, 0.0, 0.0) > 0.0,
                   "a mound to be highest at its centre, unlike a rim");
    const KeyLocationShapingPlacement cut = shaping(KeyLocationShape::Cut, 8.0, 0.0, 6.5);
    check::is_true(key_location_shaping_delta(cut, 0.0, 0.0) < 0.0, "a cut to be a hole");
    check::equal(key_location_shaping_delta(cut, 9.0, 0.0), 0.0, "and to stop at its radius");
}

TEST(a_bank_runs_along_its_axis_and_a_trench_runs_down) {
    KeyLocationShapingPlacement bank = shaping(KeyLocationShape::Bank, 0.0, 2.8, 0.0);
    bank.spec.has_radius = false;
    bank.spec.has_length = true;
    bank.spec.length_m = 19.0;
    bank.spec.has_width = true;
    bank.spec.width_m = 7.0;
    bank.rotation_degrees = 0.0;
    check::is_true(key_location_shaping_delta(bank, 0.0, 0.0) > 0.0, "a bank to raise its axis");
    check::equal(key_location_shaping_delta(bank, 20.0, 0.0), 0.0, "and to stop at its ends");
    check::equal(key_location_shaping_delta(bank, 0.0, 10.0), 0.0, "and at its width");

    KeyLocationShapingPlacement trench = bank;
    trench.spec.shape = KeyLocationShape::Trench;
    trench.spec.height_m = 0.0;
    trench.spec.depth_m = 0.5;
    check::is_true(key_location_shaping_delta(trench, 0.0, 0.0) < 0.0, "a trench to cut down");
}

// ---------------------------------------------------------------------------
// Exterior plans.
// ---------------------------------------------------------------------------

TEST(every_location_gets_a_plan_that_joins_back_to_it) {
    const Fixture& f = reference();
    const KeyLocationBlock& block = reference_block();
    const KeyLocationPlanBlock plans = generate_key_location_plans(f.request, block);
    check::is_true(plans.ok, "the plans block to report ok: " + plans.error);
    check::equal(plans.plans.size(), block.sites.size(), "one plan per site");
    check::equal(plans.part_count, 75, "the part count");
    check::equal(plans.kit_count, 102, "the kit count");
    std::set<std::string> ids;
    for (const KeyLocationSite& site : block.sites) ids.insert(site.id);
    int plotted = 0;
    for (const KeyLocationSitePlan& plan : plans.plans) {
        check::is_true(ids.count(plan.location_id) != 0, "a plan joining a real site");
        check::equal(plan.terrain.surface.size, kKeyLocationSurfacePosts, "the surface post count");
        check::equal(plan.terrain.surface.heights_m.size(),
                     static_cast<std::size_t>(kKeyLocationSurfacePosts), "the surface rows");
        for (const std::vector<double>& row : plan.terrain.surface.heights_m) {
            check::equal(row.size(), static_cast<std::size_t>(kKeyLocationSurfacePosts),
                         "the surface columns");
        }
        check::equal(plan.terrain.size, kKeyLocationSurfacePosts - 1,
                     "the cell count beside the post count");
        for (std::size_t i = 0; i < plan.plots.size(); ++i) {
            check::equal(plan.plots[i].id, "plot-" + std::to_string(i), "positional plot ids");
            check::between(plan.plots[i].rotation_degrees, 0.0, 360.0, "a normalised rotation");
            check::is_true(plan.plots[i].ground_elevation_m >= plan.plots[i].foundation_bottom_m,
                           "the highest corner at or above the lowest");
            ++plotted;
        }
        check::equal(plan.complete, !plan.plots.empty(), "status to follow the plot count");
        check::equal(plan.stat_plots, static_cast<int>(plan.plots.size()), "the plot stat");
    }
    check::is_true(plotted > 0, "the plans to have produced plots at all");
    check::equal(plans.summary_plots, plotted, "the summary plot count");
}

TEST(a_ruined_location_gains_rubble_and_a_held_one_does_not) {
    const Fixture& f = reference();
    const KeyLocationBlock& block = reference_block();
    const KeyLocationPlanBlock plans = generate_key_location_plans(f.request, block);
    std::map<std::string, const KeyLocationSite*> by_id;
    for (const KeyLocationSite& site : block.sites) by_id.emplace(site.id, &site);

    int weathered = 0;
    int intact = 0;
    for (const KeyLocationSitePlan& plan : plans.plans) {
        const KeyLocationSite& site = *by_id.at(plan.location_id);
        // The plot's `kind` is read from the KIT ENTRY, which no shipped kit authors, so
        // every catalogue plot reads `structure` even where the PART is terrain -- several
        // kits place `keyloc.part.rubble_pile` themselves and those plots are structures.
        // The one entry that DOES author a kind is the weathering entry the code appends,
        // so a terrain plot is exactly a weathering plot. (The asset list reads the part's
        // kind instead, so the two consumers disagree; this pins the producer's side.)
        std::size_t terrain_plots = 0;
        std::size_t first_terrain = plan.plots.size();
        for (std::size_t i = 0; i < plan.plots.size(); ++i) {
            if (plan.plots[i].kind != KeyLocationPartKind::Terrain) continue;
            if (terrain_plots == 0) first_terrain = i;
            ++terrain_plots;
            const bool rubble = plan.plots[i].building_id == "keyloc.part.rubble_pile" ||
                                plan.plots[i].building_id == "keyloc.part.overgrown_mound";
            check::is_true(rubble, "a terrain plot to be one of the two weathering parts, got " +
                                       std::string(plan.plots[i].building_id));
        }
        // APPENDED, so the rubble always occupies the LAST positions and the highest ids.
        if (terrain_plots > 0) {
            check::equal(first_terrain + terrain_plots, plan.plots.size(),
                         "weathering plots to sit contiguously at the end of the plot list");
        }
        const bool keyed = site.state == "ruined" || site.state == "buried" ||
                           site.state == "reclaimed" || site.state == "drowned" ||
                           site.state == "abandoned" || site.state == "corrupted";
        if (keyed) {
            check::is_true(terrain_plots >= 1, site.state + " to weather " + plan.location_id);
            const std::string_view expected = site.state == "reclaimed"
                                                  ? "keyloc.part.overgrown_mound"
                                                  : "keyloc.part.rubble_pile";
            check::is_true(plan.plots.back().building_id == expected,
                           "ground that nature took back to be mounded over rather than rubbled");
            ++weathered;
        } else {
            // Only SIX of the eleven states are keyed; the other five weather nothing.
            check::equal(terrain_plots, std::size_t(0),
                         site.state + " to leave " + plan.location_id + " intact");
            ++intact;
        }
    }
    check::is_true(weathered > 0, "some site to have weathered");
    check::is_true(intact > 0, "and some site to have been left alone");
}

TEST(exterior_plans_exercise_several_arrangements_and_replay_to_the_same_ground) {
    // NOT a draw-count test, and the name says so. The per-arrangement word counts -- row
    // and courtyard spend none, single, terrace and mouth-after-the-first spend one, ring
    // and scatter spend two -- are internal to the plan pass and are pinned only by the
    // replay below plus the comment beside `positions`.
    const Fixture& f = reference();
    const KeyLocationBlock& block = reference_block();
    const KeyLocationPlanBlock plans = generate_key_location_plans(f.request, block);
    check::is_true(plans.ok, "plans");

    std::map<KeyLocationArrangement, int> arrangements;
    for (const KeyLocationSitePlan& plan : plans.plans) ++arrangements[plan.arrangement];
    check::is_true(arrangements.size() >= 3,
                   "the world to exercise several arrangements, got " +
                       std::to_string(arrangements.size()));

    // Two identical requests must produce identical plans: nothing in the pass may read a
    // global stream.
    const KeyLocationPlanBlock again = generate_key_location_plans(f.request, block);
    check::equal(again.summary_plots, plans.summary_plots, "a replayed plot count");
    for (std::size_t i = 0; i < plans.plans.size(); ++i) {
        check::equal(again.plans[i].location_id, plans.plans[i].location_id, "a replayed plan");
        check::equal(again.plans[i].plots.size(), plans.plans[i].plots.size(), "replayed plots");
    }
}

// ---------------------------------------------------------------------------
// The block as a whole.
// ---------------------------------------------------------------------------

TEST(a_world_grows_key_locations_and_replays_to_the_same_ones) {
    const KeyLocationBlock& block = reference_block();
    check::is_true(block.ok, "the block to report ok: " + block.error);
    check::is_true(block.sites.size() >= 20,
                   "a 200 km world to grow a real catalogue of places, got " +
                       std::to_string(block.sites.size()));
    check::equal(block.version, kKeyLocationsVersion, "the block version");
    check::equal(block.catalogue_count, 97, "the archetype count");
    check::is_true(block.catalogue_revision >= 1, "a catalogue revision");
    check::is_true(!block.method.empty() && !block.limits.empty(), "method and limits prose");

    const KeyLocationBlock replay = generate_key_locations(reference().request);
    check::equal(replay.sites.size(), block.sites.size(), "a replayed site count");
    for (std::size_t i = 0; i < block.sites.size(); ++i) {
        check::equal(replay.sites[i].id, block.sites[i].id, "a replayed site id");
        check::equal(replay.sites[i].name, block.sites[i].name, "a replayed name");
        check::equal(replay.sites[i].occupant, block.sites[i].occupant, "a replayed occupant");
        check::equal(replay.sites[i].threat, block.sites[i].threat, "a replayed threat");
    }
}

TEST(every_site_carries_the_fields_a_consumer_joins_on) {
    const KeyLocationBlock& block = reference_block();
    std::map<std::string, int> by_family;
    for (const KeyLocationSite& site : block.sites) {
        check::equal(site.asset_id, "marker.key_location." + site.kind, "the asset identity");
        check::is_true(!site.reason.empty(), site.id + " to say why it is there");
        check::between(site.tier, 0, 3, "the tier");
        check::is_true(site.origin.ages_since >= 0, "a non-negative age gap");
            check::is_true(site.layer == "surface", "the layer");
        check::is_true(key_location_family(site.family) != nullptr, "a real family");
        // ruin_id is gated exactly as age and culture are: a site with no ruin near it
        // names none. A port that ungates it starts naming ruins hundreds of km away.
        ++by_family[site.family];
    }
    check::is_true(by_family.size() >= 3, "several families to be represented");
    check::equal(static_cast<int>(block.summary.by_family.size()),
                 static_cast<int>(by_family.size()), "the summary family breakdown");
}

TEST(a_world_without_a_height_layer_reports_a_failed_block_rather_than_throwing) {
    std::map<std::string, Grid> empty;
    KeyLocationRequest request;
    request.layers = &empty;
    request.radius_m = 6.0e6;
    const KeyLocationBlock block = generate_key_locations(request);
    check::is_true(!block.ok, "a failed block");
    check::is_true(!block.error.empty(), "with an error string");
    check::is_true(block.sites.empty(), "and no sites");
    // Still a BLOCK: the version and the catalogue identity survive a failure, because a
    // consumer has to be able to tell a failed roster from a missing one.
    check::equal(block.version, kKeyLocationsVersion, "the version on a failed block");

    const KeyLocationPlanBlock plans = generate_key_location_plans(request, block);
    check::is_true(!plans.ok, "and no plans for a failed block");
}

TEST(the_design_radius_and_the_metre_radius_are_not_interchangeable) {
    // The trap: config.globe_radius is design space and effective.globe_radius is metres,
    // and every reach in the catalogue is absolute metres. Running against the design one
    // must visibly change the world rather than quietly producing a plausible wrong answer.
    const Fixture& f = reference();
    check::is_true(f.world.effective.globe_radius != f.world.config.globe_radius,
                   "the two radii to differ at all, or this trap cannot be tested");
    KeyLocationRequest design = f.request;
    design.radius_m = f.world.config.globe_radius;
    design.spacing_m = 0.0;
    const KeyLocationBlock wrong = generate_key_locations(design);
    check::is_true(wrong.ok, "the wrong-radius run still to produce a block");
    check::is_true(wrong.sites.size() != reference_block().sites.size(),
                   "and to produce a different world, so the radius is load-bearing");
}

