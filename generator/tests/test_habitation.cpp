// Which ground each people will build on.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include "fwg/entities.hpp"
#include "fwg/habitation.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

struct Assessed {
    World world;
    std::shared_ptr<const SphereGrid> grid;
    std::vector<SpeciesAssessment> species;
};

Assessed assess(std::uint32_t seed = 42, int size = 65) {
    Request r;
    r.seed = seed;
    r.size = size;
    r.phase = 9;
    Assessed a;
    a.world = generate(resolve(r));
    // effective, not config: config is design space and every reach here is metres.
    a.grid = sphere_grid(a.world.config.size, a.world.effective.globe_radius);
    std::vector<std::string> ids;
    for (const auto id : civilization_ids()) ids.emplace_back(id);
    a.species = assess_habitation(a.world, *a.grid, ids);
    return a;
}

std::vector<double> node_layer(const World& w, const SphereGrid& g, const char* name) {
    std::vector<double> out(g.node_count(), 0.0);
    const auto it = w.layers.find(name);
    if (it == w.layers.end()) return out;
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = it->second.at(g.point_x[i], g.point_z[i]);
    return out;
}

}  // namespace

TEST(every_people_is_assessed_and_none_is_invented) {
    const Assessed a = assess();
    check::equal(a.species.size(), civilization_ids().size(), "one assessment per entity");
    std::set<std::string> seen;
    for (const SpeciesAssessment& s : a.species) {
        check::is_true(seen.insert(s.id).second, s.id + " to be assessed once");
        check::equal(s.suitability.size(), a.grid->node_count(), s.id + " to score every node");
        check::equal(s.food_potential.size(), a.grid->node_count(), s.id + " to assess every node");
    }
}

TEST(nobody_builds_on_the_sea) {
    const Assessed a = assess();
    const auto water = node_layer(a.world, *a.grid, "water_type");
    for (const SpeciesAssessment& s : a.species) {
        for (std::size_t i = 0; i < water.size(); ++i) {
            if (water[i] == 0.0) continue;
            check::equal(s.suitability[i], 0.0, s.id + " to score open water zero");
        }
        for (const int node : s.candidates) {
            check::equal(water[static_cast<std::size_t>(node)], 0.0,
                         s.id + " to settle node " + std::to_string(node) + " only if it is land");
        }
    }
}

TEST(a_suitability_is_always_a_score) {
    const Assessed a = assess();
    for (const SpeciesAssessment& s : a.species) {
        for (const double v : s.suitability) {
            check::between(v, 0.0, 1.0, s.id + " suitability to stay a 0..1 score");
        }
    }
}

TEST(the_candidate_list_is_ordered_and_has_no_repeats) {
    // Founding walks these lists and breaks ties on the node index, so a repeat would
    // let one cell be offered twice and an unordered list would make the walk depend on
    // the assessment's iteration order.
    const Assessed a = assess();
    for (const SpeciesAssessment& s : a.species) {
        for (std::size_t i = 1; i < s.candidates.size(); ++i) {
            check::is_true(s.candidates[i] > s.candidates[i - 1],
                           s.id + " candidates to be strictly ascending");
        }
    }
}

TEST(rotted_ground_is_settled_by_nobody_and_feeds_no_one) {
    // Biome 17 is excluded in code, not in any habitat table -- no authored rule says
    // so, which is exactly why a people whose habitat admits the biome would otherwise
    // settle a dead cell.
    const Assessed a = assess();
    if (a.world.config.world_recipe == 0) return;  // the rule is recipe-only
    const auto biome = node_layer(a.world, *a.grid, "biome");

    long long rotted = 0;
    for (const double b : biome) {
        if (static_cast<int>(b) == 17) ++rotted;
    }
    check::is_true(rotted > 0, "this world to contain rotted ground, or the rule is untested");

    for (const SpeciesAssessment& s : a.species) {
        for (const int node : s.candidates) {
            check::is_true(static_cast<int>(biome[static_cast<std::size_t>(node)]) != 17,
                           s.id + " not to settle rotted ground");
        }
        for (std::size_t i = 0; i < biome.size(); ++i) {
            if (static_cast<int>(biome[i]) != 17) continue;
            check::equal(s.food_potential[i], 0.0, s.id + " to grow nothing on rotted ground");
        }
    }
}

TEST(the_peoples_disagree_about_which_ground_is_theirs) {
    // If every people accepted the same cells, none of the three gates would be doing
    // anything and the registry's variety would not reach the map.
    const Assessed a = assess();
    long long differing_pairs = 0;
    for (std::size_t i = 0; i < a.species.size(); ++i) {
        for (std::size_t j = i + 1; j < a.species.size(); ++j) {
            if (a.species[i].candidates != a.species[j].candidates) ++differing_pairs;
        }
    }
    check::is_true(differing_pairs > 0, "some pair of peoples to want different ground");
}

TEST(a_people_allowed_to_farm_outside_its_habitat_does) {
    // Its hinterland should reach ground it would not itself build on -- that is the
    // whole point of the flag -- but only ground it can actually reach by road.
    const Assessed a = assess();
    bool tested = false;
    for (const SpeciesAssessment& s : a.species) {
        const EntityRules* rules = entity_rule(s.id);
        if (rules == nullptr || !rules->settlement.support_outside_habitat) continue;
        if (s.candidates.empty()) continue;
        tested = true;

        const std::set<int> settles(s.candidates.begin(), s.candidates.end());
        long long beyond = 0;
        for (std::size_t i = 0; i < s.food_potential.size(); ++i) {
            if (s.food_potential[i] > 0.0 && settles.count(static_cast<int>(i)) == 0) ++beyond;
        }
        check::is_true(beyond > 0, s.id + " to farm ground it would not build on");
    }
    check::is_true(tested, "some people to author support_outside_habitat");
}

TEST(a_people_that_farms_only_its_own_habitat_never_reaches_past_it) {
    const Assessed a = assess();
    for (const SpeciesAssessment& s : a.species) {
        const EntityRules* rules = entity_rule(s.id);
        if (rules == nullptr || rules->settlement.support_outside_habitat) continue;
        const std::set<int> settles(s.candidates.begin(), s.candidates.end());
        for (std::size_t i = 0; i < s.food_potential.size(); ++i) {
            if (s.food_potential[i] <= 0.0) continue;
            check::is_true(settles.count(static_cast<int>(i)) != 0,
                           s.id + " to farm only ground it would settle");
        }
    }
}

// --- an inherited defect, pinned so a fix cannot land silently ------------

TEST(the_maritime_threshold_is_finer_than_the_grid_can_resolve) {
    // maritime is exp(-distance_to_ocean / 300 m). That 300 m decay is authored for the
    // 11.15 km reference world, whose grid step is about 87 m -- there a cell next to
    // the sea reads exp(-87/300) = 0.75 and the layer means something.
    //
    // human_maritime's population habitat requires maritime >= 0.3, which is to say the
    // cell must be within 300*ln(1/0.3) = 361 m of the sea. On the 200 km default world
    // the FINEST step in the grid is 1562 m, so no land cell anywhere can qualify: the
    // threshold asks for a resolution the raster does not have. The people cannot settle
    // any world at this scale.
    //
    // This is inherited from the source rather than introduced by the port -- it is the
    // same disease the source already diagnosed and fixed for island_habitat, whose
    // absolute-metre floor had likewise become unreachable once worlds grew past the
    // reference. It is PINNED rather than fixed because rescaling the decay changes
    // every world, which is a decision to take deliberately rather than in passing.
    // When it is taken, this test is the one to update.
    const Assessed a = assess();

    constexpr double kDecayM = 300.0;
    constexpr double kThreshold = 0.3;
    const double required_m = -kDecayM * std::log(kThreshold);

    // Measured away from the poles. Longitude convergence makes polar neighbours
    // arbitrarily close -- the finest step anywhere on this grid is about 153 m -- so
    // the threshold IS reachable within a few rows of a pole. That is a raster artifact
    // rather than a coastline, and it is why the layer is not identically zero.
    const int n = a.world.config.size;
    double finest_temperate_step = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < a.grid->node_count(); ++i) {
        if (std::abs(90.0 - 180.0 * a.grid->point_z[i] / (n - 1)) > 60.0) continue;
        for (const SphereNeighbour& nb : a.grid->neighbours[i]) {
            finest_temperate_step = std::min(finest_temperate_step, nb.distance);
        }
    }
    check::is_true(finest_temperate_step > required_m,
                   "the finest step in temperate latitudes (" +
                       std::to_string(finest_temperate_step) + " m) to be coarser than the " +
                       std::to_string(required_m) + " m the threshold needs");

    // And the consequence, measured rather than assumed: on temperate land the layer
    // is nowhere near the threshold, and every cell that does reach it is polar.
    const auto maritime = node_layer(a.world, *a.grid, "maritime");
    const auto water = node_layer(a.world, *a.grid, "water_type");
    double highest_temperate = 0.0;
    double lowest_qualifying_latitude = 90.0;
    long long qualifying = 0;
    for (std::size_t i = 0; i < water.size(); ++i) {
        if (water[i] != 0.0) continue;
        const double latitude = std::abs(90.0 - 180.0 * a.grid->point_z[i] / (n - 1));
        if (latitude <= 60.0) highest_temperate = std::max(highest_temperate, maritime[i]);
        if (maritime[i] >= kThreshold) {
            ++qualifying;
            lowest_qualifying_latitude = std::min(lowest_qualifying_latitude, latitude);
        }
    }
    check::is_true(highest_temperate < 0.01,
                   "temperate land to read essentially zero, highest is " +
                       std::to_string(highest_temperate));
    check::is_true(qualifying == 0 || lowest_qualifying_latitude > 60.0,
                   "any cell that does reach the threshold to be polar, lowest is at " +
                       std::to_string(lowest_qualifying_latitude) + " degrees");

    for (const SpeciesAssessment& s : a.species) {
        if (s.id != "human_maritime") continue;
        check::equal(s.candidates.size(), std::size_t{0},
                     "human_maritime to be unable to settle this world at all");
    }
}
