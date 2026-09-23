// The sixteen-stage ladder: what a world contains at each rung.
//
// These tests are about the LADDER, not about any one system: that asking for stage N
// gets you stage N's world and not stage N+2's, and that a stage's outputs are absent
// before the stage that makes them. The reference ladder is:
//
//   1 plate layout      5 connected water   9  leylines        13 beasties
//   2 tectonic relief   6 second relief     10 founding        14 age transition 1
//   3 surface detail    7 valleys, gorges   11 roads           15 age transition 2
//   4 erosion           8 wind and rain     12 populated       16 complete
#include "check.hpp"

#include <algorithm>
#include <set>
#include <string>

#include "fwg/world.hpp"

using namespace fwg;

namespace {

World at_phase(int phase, int size = 33) {
    Request r;
    r.seed = 42;
    r.size = size;
    r.phase = phase;
    return generate(resolve(r));
}

bool has(const World& w, const char* layer) { return w.layers.find(layer) != w.layers.end(); }

}  // namespace

TEST(water_is_stage_five_and_does_not_route_before_the_land_is_finished) {
    // This ran from phase 2, which published a drainage network for a landscape the
    // world did not yet have: stages 3 and 4 rewrite the surface those rivers were
    // routed over. Nothing in the suite noticed, because nothing asserted WHEN water
    // appears -- only what it looks like once it is there.
    for (const int phase : {2, 3, 4}) {
        const World early = at_phase(phase);
        check::is_true(!has(early, "river"),
                       "phase " + std::to_string(phase) + " to have no rivers yet");
        check::is_true(!has(early, "water_type"),
                       "phase " + std::to_string(phase) + " to have no water classified yet");
    }
    // `catchment` is deliberately NOT in that list: flow accumulation is erosion's, at
    // stage 4, and it exists before anything is called a river.
    check::is_true(has(at_phase(4), "catchment"), "erosion at stage 4 to publish catchment");

    const World routed = at_phase(5);
    // `river` is water's own channel network. `rain_river` is a LATER layer, fed by
    // rainfall once climate exists, and asserting on it here would be testing stage 8.
    check::is_true(has(routed, "river"), "phase 5 to route its rivers");
    check::is_true(!has(routed, "rain_river"), "phase 5 to have no rain-fed rivers yet");
    check::is_true(has(routed, "water_type"), "phase 5 to classify its water");
}

TEST(each_rung_of_the_ladder_adds_something) {
    // A ladder whose rungs produce identical worlds is not a ladder. Every stage should
    // either add a system or change the world; two adjacent phases that agree on both
    // mean one of them is not really a stage.
    std::string previous_systems;
    for (int phase = 1; phase <= kHighestImplementedStage; ++phase) {
        const World w = at_phase(phase);
        std::string systems;
        for (const std::string& s : w.systems) systems += s + ",";
        check::equal(w.completed_stage, phase,
                     "phase " + std::to_string(phase) + " to report itself completed");
        if (phase > 1) {
            check::is_true(systems.size() >= previous_systems.size(),
                           "phase " + std::to_string(phase) + " to keep what phase " +
                               std::to_string(phase - 1) + " had");
        }
        previous_systems = systems;
    }
}

TEST(a_stage_beyond_the_ladder_says_what_is_actually_missing) {
    // The warning named water, climate, biomes and history as unimplemented long after
    // all four were ported. A warning that lies about the state of the port is worse
    // than none: it sends a reader to look for a gap that closed.
    const World w = at_phase(16);
    bool warned = false;
    for (const std::string& warning : w.warnings) {
        if (warning.find("not implemented") == std::string::npos) continue;
        warned = true;
        for (const char* ported : {"water", "climate", "biomes"}) {
            check::is_true(warning.find(ported) == std::string::npos,
                           std::string("the warning not to claim ") + ported +
                               " is missing; it is ported");
        }
    }
    check::is_true(warned, "a world past the ladder to say so");
    check::equal(w.completed_stage, kHighestImplementedStage,
                 "a world past the ladder to stop at the last implemented stage");
}

TEST(the_systems_list_records_what_actually_ran) {
    // `completed_stage` is a number; `systems` is the evidence. A world that claims a
    // stage but lists no system for it is claiming work it did not do.
    const World w = at_phase(kHighestImplementedStage);
    const std::set<std::string> systems(w.systems.begin(), w.systems.end());
    for (const char* expected : {"tectonics", "surface_detail", "erosion", "water",
                                 "climate", "leylines", "ecology", "settlements"}) {
        check::is_true(systems.count(expected) != 0,
                       std::string("a full world to record the ") + expected + " system");
    }
}

TEST(the_upper_rungs_are_distinct_worlds_not_one_world_four_times) {
    // Stages 8, 9 and 10 were all gated on phase 6, so phases 6 through 10 differed
    // only by the relic carving at 7. Asking for stage 8 and asking for stage 10 built
    // the same world, and every rung above 7 was decoration.
    const World seven = at_phase(7);
    const World eight = at_phase(8);
    const World nine = at_phase(9);

    check::is_true(!has(seven, "temperature"), "stage 7 to have no weather yet");
    check::is_true(has(eight, "temperature"), "stage 8 to be where wind and rain arrive");
    check::is_true(has(eight, "moisture"), "stage 8 to carry moisture");

    check::is_true(!has(eight, "ley_weave"), "stage 8 to have no leylines yet");
    check::is_true(has(nine, "ley_weave"), "stage 9 to be where the leylines arrive");
    check::is_true(has(nine, "biome_variant"), "stage 9 to resolve the magical biomes");

    // And ecology, which this port runs at 9 rather than the reference's 8 because its
    // single pass has to follow the ley fields.
    check::is_true(!has(eight, "coastal_support"), "stage 8 to have no environment yet");
    check::is_true(has(nine, "coastal_support"), "stage 9 to carry the environment");
}

TEST(stage_twelve_publishes_the_food_layers_three_subsystems_wait_on) {
    // Beasts and nomads both read food_potential and natural_food_potential for forage,
    // and both reported reading zero everywhere while stage 12 was unported. This is the
    // pass that supplies them.
    const World w = at_phase(12, 65);
    check::is_true(w.has_humans, "stage 12 to run the hinterland");
    for (const char* layer : {"food_potential", "natural_food_potential", "irrigation_benefit",
                              "culture_region", "civilization_region", "hamlet_catchment",
                              "fishing_ground_owner"}) {
        check::is_true(has(w, layer), std::string("stage 12 to publish ") + layer);
    }

    // A floor, because a layer of zeroes would satisfy the check above and feed the
    // forage terms exactly the nothing they had before.
    const Grid& food = w.layers.at("food_potential");
    long long fed = 0;
    double best = 0.0;
    for (int z = 0; z < w.config.size; ++z) {
        for (int x = 0; x < w.config.size; ++x) {
            if (food.at(x, z) > 0.0) ++fed;
            best = std::max(best, food.at(x, z));
        }
    }
    check::is_true(fed > 100, "a real food field, not a flat zero (" + std::to_string(fed) + " cells)");
    check::is_true(best > 0.1, "somewhere worth farming, best is " + std::to_string(best));
}

TEST(irrigation_reaches_the_waters_edge_and_no_further) {
    // Pinned because the number is wrong, not because it is right. A people's water_reach
    // is 400-650 m against a cell step of 1562-6250 m, so the irrigation exponential
    // underflows one step from water. It fires only where the distance is zero or nearly
    // so -- the cells standing on the watercourse.
    //
    // This is NOT identically zero, which is what the first reading of it claimed. See
    // docs/scale-defects.md. The assertion is that it fires and is confined; if someone
    // scales water_reach, this test fails and should be rewritten rather than relaxed.
    const World w = at_phase(12, 65);
    const Grid& food = w.layers.at("food_potential");
    const Grid& natural = w.layers.at("natural_food_potential");

    long long irrigated = 0;
    double total_gain = 0.0;
    long long land = 0;
    for (int z = 0; z < w.config.size; ++z) {
        for (int x = 0; x < w.config.size; ++x) {
            if (natural.at(x, z) > 0.0) ++land;
            const double gain = food.at(x, z) - natural.at(x, z);
            if (gain > 1e-12) {
                ++irrigated;
                total_gain += gain;
            }
            check::is_true(gain >= -1e-12, "irrigation never to make ground worse");
        }
    }
    check::is_true(irrigated > 0, "irrigation to fire somewhere at all");
    check::is_true(land > 0, "farmable ground to exist");
    check::is_true(irrigated < land,
                   "and to reach nowhere near all of it (" + std::to_string(irrigated) + " of " +
                       std::to_string(land) + ")");
    check::is_true(total_gain / static_cast<double>(irrigated) < 0.05,
                   "the mean gain to be the shoreline trickle it is, not a regional system");
}
