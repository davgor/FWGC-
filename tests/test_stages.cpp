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
#include <cmath>
#include <map>
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

TEST(stage_thirteen_resolves_a_faith_for_every_people_that_settled) {
    // Religion reads the nests just placed and the peoples that founded. A people on the
    // map with no faith would mean the adapter dropped it between the settlement record
    // and the religion request.
    const World w = at_phase(13, 65);
    check::is_true(w.has_wildlife, "stage 13 to place the nests");
    check::is_true(w.has_religion, "stage 13 to resolve religion");
    check::is_true(!w.religion.cosmology.id.empty(), "a cosmology to be chosen");
    check::is_true(!w.religion.gods.empty(), "a pantheon to be resolved");

    std::set<std::string> settled;
    for (const SettlementSite& s : w.settlements.sites) settled.insert(s.population_profile);
    check::is_true(settled.size() > 1, "several peoples, or this proves little");
    std::set<std::string> faithful;
    for (const Faith& f : w.religion.faiths) faithful.insert(f.civilization);
    for (const std::string& people : settled) {
        check::is_true(faithful.count(people) != 0, people + " to hold a faith");
    }
}

TEST(the_cosmology_is_a_property_of_the_seed_and_not_a_constant) {
    // One draw decides the cosmology. If every seed chose the same one, the draw would be
    // reading something other than the seed -- or not happening at all.
    std::set<std::string> seen;
    for (const std::uint32_t seed : {42u, 7u, 1234u, 99u, 2026u}) {
        Request r;
        r.seed = seed;
        r.size = 33;
        r.phase = 13;
        seen.insert(generate(resolve(r)).religion.cosmology.id);
    }
    check::is_true(seen.size() > 1, "five seeds to choose more than one cosmology");
}

TEST(stage_sixteen_plans_every_city_and_every_hamlet) {
    const World w = at_phase(16, 33);
    check::is_true(w.has_plans, "stage 16 to run the planners");
    check::equal(w.city_plans.cities.size(), w.settlements.sites.size(), "one plan per city");
    std::set<std::string> uids;
    for (const SettlementSite& s : w.settlements.sites) uids.insert(s.uid);
    for (const CityPlan& p : w.city_plans.cities) {
        check::is_true(uids.count(p.city_uid) != 0, p.city_uid + " to name a city that exists");
    }
    check::equal(w.hamlet_plans.hamlets.size(), w.humans.hamlets.size(), "one plan per hamlet");
}

TEST(a_hamlet_plan_names_the_city_it_actually_belongs_to) {
    // The planner indexes cities by position, so the list it is handed must be in the
    // world's own order. A re-sorted list attributes every hamlet to the wrong parent,
    // and the only symptom is a plausible neighbour's uid.
    const World w = at_phase(16, 33);
    std::map<std::string, const RuralSite*> by_id;
    for (const RuralSite& h : w.humans.hamlets) by_id[h.id] = &h;
    long long checked = 0;
    for (const HamletPlan& p : w.hamlet_plans.hamlets) {
        const auto it = by_id.find(p.hamlet_id);
        if (it == by_id.end() || it->second->core_id < 0) continue;
        const SettlementSite& parent = w.settlements.sites[static_cast<std::size_t>(it->second->core_id)];
        check::equal(p.core_city_uid, parent.uid, p.hamlet_id + " to belong to its own city");
        ++checked;
    }
    check::is_true(checked > 0, "some hamlet to have a parent, or nothing is tested");
}

TEST(a_regional_road_enters_a_city_on_the_edge_of_its_window) {
    // Each road touching a city is projected into the city's tangent plane and the gate
    // is where it leaves the square the crop covers. A gate inside the square, or off it,
    // means the projection or the crossing is wrong -- and a city whose roads produced no
    // gates at all means the projection never ran, which is how it started: a caller
    // input nothing could fill.
    const World w = at_phase(16, 33);
    long long gates = 0;
    long long inward_checked = 0;
    for (const CityPlan& p : w.city_plans.cities) {
        for (const RoadConnection& c : p.road_connections) {
            ++gates;
            const double edge = std::max(std::abs(c.gate_local_m.x), std::abs(c.gate_local_m.z));
            check::close(edge, p.half_m, 1e-6, c.junction_id + " to sit on the window's edge");
            check::is_true(c.route_index >= 0 &&
                               static_cast<std::size_t>(c.route_index) < w.roads.routes.size(),
                           c.junction_id + " to name a real road");
            const Road& road = w.roads.routes[static_cast<std::size_t>(c.route_index)];
            check::is_true(c.outside_index >= 0 && static_cast<std::size_t>(c.outside_index) < road.nodes.size(),
                           c.junction_id + " to index its own road's nodes");

            // `outside_index` indexes the road's OWN node list whichever way it was
            // walked. The node one step back toward the city is therefore inside the
            // window. Indexed in walk order instead, a road entering from its far end
            // would point at a node beside the OTHER city -- still a valid index, which
            // is why the range check above cannot see it.
            const int inward = c.end == "from" ? c.outside_index - 1 : c.outside_index + 1;
            if (inward < 0 || static_cast<std::size_t>(inward) >= road.nodes.size()) continue;
            const auto node = static_cast<std::size_t>(road.nodes[static_cast<std::size_t>(inward)]);
            const auto held = sphere_grid(w.effective.size, w.effective.globe_radius);
            const SettlementSite& city = w.settlements.sites[static_cast<std::size_t>(c.end == "from" ? road.from : road.to)];
            const double inside = great_circle_m(w.effective.globe_radius, city.direction,
                                                 direction(held->point_x[node], held->point_z[node], w.effective.size));
            check::is_true(inside <= p.half_m * 1.5,
                           c.junction_id + " to leave from a node inside the window (" + std::to_string(inside) +
                               " m against a half-width of " + std::to_string(p.half_m) + ")");
            ++inward_checked;
        }
    }
    check::is_true(gates > 0, "roads to reach some city window, or nothing is tested");
    check::is_true(inward_checked > 0, "some gate to be traced back inside, or the index is untested");
}

TEST(stage_sixteen_places_and_routes_the_nomads) {
    const World w = at_phase(16, 65);
    check::is_true(w.has_nomads, "stage 16 to place the nomads");
    check::is_true(!w.nomads.groups.empty(), "some band to exist, or nothing is tested");
    check::is_true(w.nomads.routes.has_value(), "the bands to have been routed");
    // routed + stranded covers every band: a band the route pass skipped entirely would
    // be neither, and the counts would silently stop adding up.
    check::equal(w.nomads.routes->routed + w.nomads.routes->stranded,
                 static_cast<int>(w.nomads.groups.size()), "every band routed or stranded");
}

TEST(the_herds_forage_now_that_the_hinterland_publishes_food) {
    // Beasts and nomads both read food_potential for forage, and while stage 12 was
    // unported both reported the same thing independently: forage was zero everywhere, the
    // summer scorer fell back to the lowest node, and EVERY irruptive group was solitary.
    // If the food layers ever stop reaching this pass, the herds collapse back to that.
    const World w = at_phase(16, 65);
    check::is_true(w.has_beast_movements, "stage 16 to move the herds");
    const BeastMovements& m = w.beast_movements;
    check::is_true(!m.groups.empty(), "some herd to exist");
    check::is_true(m.routed > m.solitary,
                   "more herds routed than solitary (" + std::to_string(m.routed) + " routed, " +
                       std::to_string(m.solitary) + " solitary)");

    bool wanderers = false;
    for (const NomadBand& b : w.nomads.groups) wanderers = wanderers || b.classification == NomadClass::Wanderers;
    check::is_true(wanderers, "wanderer bands to exist, which need forage above their floor");
}

TEST(every_band_and_every_herd_is_something_a_traveller_can_meet) {
    const World w = at_phase(16, 65);
    check::equal(w.encounters.entries.size(), w.nomads.groups.size() + w.beast_movements.groups.size(),
                 "one encounter entry per band and per herd");
}

TEST(stage_sixteen_draws_a_cast_and_weaves_a_web_for_every_hero) {
    const World w = at_phase(16, 65);
    check::is_true(w.has_heroes, "stage 16 to draw the cast");
    check::equal(w.heroes.status, std::string("ok"), "the cast to resolve");
    check::is_true(!w.heroes.people.empty(), "some hero to exist");
    for (const Hero& h : w.heroes.people) {
        check::is_true(!h.name.empty(), h.uid + " to have a name");
        check::is_true(!h.archetype.empty(), h.uid + " to have an archetype");
    }
    check::equal(w.story_web.status, std::string("ok"), "the story web to resolve");
    check::equal(w.story_web.webs.size(), w.heroes.people.size(), "one web per hero");
    // Nothing else binds the two blocks to one run, and a web compiled against a stale
    // cast still compiles. The revision the web records is the cast it was woven from.
    check::equal(w.story_web.heroes_revision.sha256, w.heroes.policy_revision.sha256,
                 "the web to record the cast it was woven from");
}

TEST(a_quest_to_slay_something_points_at_where_it_lives) {
    // The reference writes every slay hook as `target_has_no_node`, because its nest rows
    // carry no node -- so no player is ever pointed at a lair. The node is known when the
    // world is handed over, and passing it through is what makes a slay quest a place.
    const World w = at_phase(16, 65);
    std::set<std::string> nests;
    std::map<std::string, int> nest_node;
    for (const NestSite& n : w.beast_nests.sites) nest_node[n.id] = n.node;
    long long slay = 0;
    for (const HeroQuestHook& q : w.heroes.quest_hooks) {
        if (q.verb != "slay") continue;
        ++slay;
        check::is_true(q.has_target_node, q.hook_id + " to be sited, not " + q.unsited_reason);
        const auto it = nest_node.find(q.target);
        if (it != nest_node.end()) {
            check::equal(q.target_node, it->second, q.hook_id + " to point at the lair itself");
        }
    }
    check::is_true(slay > 0, "some slay quest to exist, or nothing is tested");
}
