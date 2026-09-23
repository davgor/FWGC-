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
#include <memory>
#include <numbers>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "fwg/complete_stage.hpp"
#include "fwg/grid.hpp"
#include "fwg/heroes.hpp"
#include "fwg/key_locations.hpp"
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

// The seed-42 size-65 world at phase 16, built once for the tests below that only read it.
// It is the world both codebases were measured on when no logging camp placed here: the
// reference's own world for this request places five logging camps and two geyser basins.
const World& complete_65() {
    static const World w = at_phase(16, 65);
    return w;
}

const KeyLocationDiagnostic* diagnostic(const World& w, std::string_view archetype) {
    for (const KeyLocationDiagnostic& d : w.key_locations.diagnostics) {
        if (d.archetype == archetype) return &d;
    }
    return nullptr;
}

bool weighs(const KeyLocationSite& site, std::string_view layer) {
    for (const KeyLocationFactor& f : site.suitability_factors) {
        if (f.layer == layer) return true;
    }
    return false;
}

// Every archetype of `kind` the world placed must stand on ground its own rules allow: on a
// node no ruin, lair, shrine or college holds, and outside its clearance of every city.
// Returns how many there were.
std::size_t placed_on_free_ground(const World& w, std::string_view kind, std::string_view factor) {
    std::set<int> held;
    for (const Ruin& r : w.ruins) held.insert(r.node);
    for (const NestSite& n : w.beast_nests.sites) held.insert(n.node);
    for (const ReligionSite& s : w.religion.sites) held.insert(s.node);
    for (const College& c : w.colleges.colleges) held.insert(c.node);
    const KeyLocationArchetype* archetype = key_location_archetype(kind);
    check::is_true(archetype != nullptr, "the catalogue to carry " + std::string(kind));
    const double radius = w.effective.globe_radius;
    const double reference_cell = 2.0 * std::numbers::pi * radius / (kKeyLocationReferenceRaster - 1);
    const double clearance = archetype->has_settlement_clearance_m
                                 ? archetype->settlement_clearance_m
                                 : archetype->settlement_clearance_cells * reference_cell;
    std::size_t placed = 0;
    for (const KeyLocationSite& site : w.key_locations.sites) {
        if (site.kind != kind) continue;
        ++placed;
        check::is_true(weighs(site, factor), site.id + " to weigh " + std::string(factor));
        check::is_true(held.count(site.node) == 0, site.id + " to stand on ground no other feature holds");
        for (const SettlementSite& city : w.settlements.sites) {
            check::is_true(great_circle_m(radius, site.direction, city.direction) >= clearance,
                           site.id + " to keep its clearance of " + city.uid);
        }
    }
    return placed;
}

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

TEST(a_full_ladder_claims_every_stage_and_warns_of_nothing_missing) {
    // The warning named water, climate, biomes and history as unimplemented long after
    // all four were ported. With the whole ladder in, a warning that says anything is
    // unimplemented sends a reader to look for a gap that has closed.
    const World w = at_phase(16);
    check::equal(w.completed_stage, 16, "a full world to report the whole ladder");
    for (const std::string& warning : w.warnings) {
        check::is_true(warning.find("not implemented") == std::string::npos,
                       "no warning to claim a stage is missing: " + warning);
    }
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
    // Woven for the LIVING cast, people and Dreads alike: a legend walks no arcs. The ages
    // are what make legends, so a world that has turned them must have some, or the living
    // filter is not being tested at all.
    std::size_t living = 0;
    std::size_t legends = 0;
    for (const Hero& h : w.heroes.people) (h.status == "living" ? living : legends) += 1;
    for (const HeroDread& d : w.heroes.dreads) (d.status == "living" ? living : legends) += 1;
    check::is_true(legends > 0, "some legend after two ages, or the living filter is untested");
    check::equal(w.story_web.webs.size(), living, "one web per living hero or Dread");
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

TEST(stage_sixteen_staffs_every_settlement_and_places_every_person) {
    const World w = at_phase(16, 65);
    check::is_true(w.has_npcs, "stage 16 to build the rosters");
    check::is_true(w.npcs.ok, "the roster to resolve: " + w.npcs.error);
    check::is_true(!w.humans.fortresses.empty(), "a fortress to staff, or the castle rosters are not tested");
    check::equal(w.npcs.sites.size(),
                 w.settlements.sites.size() + w.humans.hamlets.size() + w.humans.fortresses.size(),
                 "a roster for every city, every hamlet and every fortress");

    std::set<std::string> places;
    for (const SettlementSite& s : w.settlements.sites) places.insert(s.uid);
    for (const RuralSite& h : w.humans.hamlets) places.insert(h.uid);
    for (const RuralSite& f : w.humans.fortresses) places.insert(f.uid);
    long long placed = 0;
    for (const NpcPerson& p : w.npcs.people) {
        if (p.is_cast || !p.site_uid) continue;
        ++placed;
        check::is_true(places.count(*p.site_uid) != 0, p.uid + " to live somewhere that exists");
        check::is_true(p.name.has_value() && !p.name->empty(), p.uid + " to have a name");
    }
    check::is_true(placed > 0, "somebody to live somewhere, or nothing is tested");
}

TEST(every_member_of_the_cast_is_folded_into_the_rosters) {
    // The roster folds in the whole cast, people and Dreads, the living as alive and the
    // legends as dead, each cross-linked by hero uid. A cast member missing from it is a
    // person the world names and nobody can find.
    const World w = at_phase(16, 65);
    const long long cast = static_cast<long long>(w.heroes.people.size() + w.heroes.dreads.size());
    check::is_true(cast > 0, "some cast, or nothing is tested");
    check::equal(static_cast<long long>(w.npcs.summary.heroes_linked), cast, "every cast member linked");
    long long dead = 0;
    for (const NpcPerson& p : w.npcs.people) dead += (p.is_cast && !p.alive) ? 1 : 0;
    check::is_true(dead > 0, "a legend folded in as dead, or the ages made none");
}

TEST(stage_sixteen_places_the_key_locations_and_plans_each_one) {
    const World w = at_phase(16, 65);
    check::is_true(w.has_key_locations, "stage 16 to place the key locations");
    check::is_true(w.key_locations.ok, "the key locations to resolve: " + w.key_locations.error);
    check::is_true(w.key_location_plans.ok, "their plans to resolve: " + w.key_location_plans.error);
    check::is_true(!w.key_locations.sites.empty(), "somewhere worth going, or nothing is tested");
    // One plan per site, in block order: the plan's location_id is the join.
    check::equal(w.key_location_plans.plans.size(), w.key_locations.sites.size(), "a plan for every site");
    for (std::size_t i = 0; i < w.key_locations.sites.size(); ++i) {
        check::equal(w.key_location_plans.plans[i].location_id, w.key_locations.sites[i].id,
                     "plan " + std::to_string(i) + " to join its site");
    }
    check::equal(w.systems.back(), std::string("key_location_plans"), "the plans to be the last system to run");
}

TEST(the_only_key_location_waiting_on_a_missing_layer_is_the_one_the_reference_knows) {
    // The reference pins this set exactly (KNOWN_UNPUBLISHED): `peat_cuttings` gates on a
    // `wetland_distance` layer nothing upstream writes, so it fails closed in every world.
    // Ported as it stands. The set cannot grow without failing here, and it cannot be
    // quietly fixed without failing either.
    const World w = at_phase(16, 65);
    const std::string prefix = "the world never generated the ";
    std::set<std::pair<std::string, std::string>> absent;
    for (const KeyLocationDiagnostic& d : w.key_locations.diagnostics) {
        if (d.reason.rfind(prefix, 0) != 0) continue;
        const std::string rest = d.reason.substr(prefix.size());
        absent.emplace(d.archetype, rest.substr(0, rest.find(' ')));
    }
    const std::set<std::pair<std::string, std::string>> known{{"peat_cuttings", "wetland_distance"}};
    check::is_true(absent == known, "the unpublished-layer set to be exactly the known one");
}

TEST(no_lair_or_range_stands_inside_its_clearance_of_a_city_a_harbour_or_a_hamlet) {
    // The reference's nest passes keep clear of `settled_directions`: every city, then every
    // harbour, then every hamlet. A monster of tier t needs base * t metres of it and an
    // animal base * t / 3. Handed the cities alone, the passes laired 472 of 3679 monsters on
    // this world inside a hamlet's or a harbour's clearance, where the reference's world for
    // the same request lairs none, and held 332 of the 365 cells a logging camp qualified for.
    const World& w = complete_65();
    const double radius = w.effective.globe_radius;
    const int n = w.effective.size;
    const double base = w.effective.options[Option::nest_settlement_clearance];
    check::is_true(!w.society.fisheries.ports.empty() && !w.humans.hamlets.empty(),
                   "harbours and hamlets to keep clear of, or nothing is tested");

    std::vector<std::pair<std::string, Vec3>> settled;
    for (const SettlementSite& s : w.settlements.sites) settled.emplace_back(s.uid, s.direction);
    for (const HarbourPort& p : w.society.fisheries.ports) settled.emplace_back(p.id, direction(p.x, p.z, n));
    for (const RuralSite& h : w.humans.hamlets) settled.emplace_back(h.uid, direction(h.x, h.z, n));

    // One message for the first offender and a count, not a check per pair: there are
    // millions of pairs.
    const auto audit = [&](const std::vector<NestSite>& sites, double divisor, const std::string& what) {
        std::size_t inside = 0;
        std::string first;
        for (const NestSite& site : sites) {
            const double need = base * site.tier / divisor;
            for (const auto& [uid, where] : settled) {
                if (great_circle_m(radius, site.direction, where) >= need) continue;
                if (inside++ == 0) first = site.id + " inside " + std::to_string(need) + " m of " + uid;
                break;
            }
        }
        check::equal(inside, std::size_t{0}, what + " inside a clearance the reference keeps; first: " + first);
    };
    check::is_true(!w.beast_nests.sites.empty() && !w.wildlife.sites.empty(), "lairs and ranges to test");
    audit(w.beast_nests.sites, 1.0, "lairs");
    audit(w.wildlife.sites, 3.0, "animal ranges");

    // The list stops there. Fortresses are not in it, in the reference or here, so a lair
    // may stand closer to a fortress than to anyone the list names -- and on this world some do.
    std::size_t by_fortress = 0;
    for (const NestSite& lair : w.beast_nests.sites) {
        for (const RuralSite& f : w.humans.fortresses) {
            if (great_circle_m(radius, lair.direction, direction(f.x, f.z, n)) < base * lair.tier) {
                ++by_fortress;
                break;
            }
        }
    }
    check::is_true(by_fortress > 0, "some lair inside a fortress's clearance: fortresses are not settled ground here");
}

TEST(key_locations_keep_clear_of_the_cities_and_only_the_cities) {
    // `settled_directions` keeps clear of every city, hamlet, fortress, harbour and college
    // THAT PUBLISHES A DIRECTION, and in the reference only the cities do: the other four
    // publish a node and no direction, so its clearance never sees them. Given directions
    // here, they refused all 140 free cells a logging camp qualified for on this world, where
    // the cities alone refuse 33. So every scatter site keeps its clearance from every city,
    // and some stand closer than that to a hamlet, a fortress or a harbour.
    const World& w = complete_65();
    const double radius = w.effective.globe_radius;
    const int n = w.effective.size;
    const double reference_cell = 2.0 * std::numbers::pi * radius / (kKeyLocationReferenceRaster - 1);
    std::vector<Vec3> rural;
    for (const RuralSite& h : w.humans.hamlets) rural.push_back(direction(h.x, h.z, n));
    for (const RuralSite& f : w.humans.fortresses) rural.push_back(direction(f.x, f.z, n));
    for (const HarbourPort& p : w.society.fisheries.ports) rural.push_back(direction(p.x, p.z, n));

    std::size_t scattered = 0;
    std::size_t near_rural = 0;
    for (const KeyLocationSite& site : w.key_locations.sites) {
        if (site.placement != KeyLocationPlacement::Node) continue;
        const KeyLocationArchetype* a = key_location_archetype(site.kind);
        check::is_true(a != nullptr, site.kind + " to be in the catalogue");
        const double clearance = a->has_settlement_clearance_m ? a->settlement_clearance_m
                                                               : a->settlement_clearance_cells * reference_cell;
        if (clearance <= 0.0) continue;
        ++scattered;
        for (const SettlementSite& city : w.settlements.sites) {
            check::is_true(great_circle_m(radius, site.direction, city.direction) >= clearance,
                           site.id + " to keep its clearance of " + city.uid);
        }
        for (const Vec3& where : rural) {
            if (great_circle_m(radius, site.direction, where) < clearance) {
                ++near_rural;
                break;
            }
        }
    }
    check::is_true(scattered > 0, "scattered key locations with a clearance, or nothing is tested");
    check::is_true(near_rural > 0,
                   "some key location inside its clearance of a hamlet, fortress or harbour, as the "
                   "reference's clearance cannot see them");
}

TEST(a_key_location_may_stand_on_a_landmarks_cell_because_no_landmark_claims_ground) {
    // `claimed_nodes` leaves ground to the module that placed it by NODE, and a landmark
    // publishes its cell and no node, so in the reference no landmark claims anything.
    // Claiming its cell's node here kept a portal arch off a witch hut's cell on the seed-10
    // size-33 world, and the block that followed left no room for the whaling station that
    // world now places. On this world a summoning circle stands on a witch hut's cell.
    const World& w = complete_65();
    const auto grid = sphere_grid(w.effective.size, w.effective.globe_radius);
    std::set<int> marked;
    for (const Landmark& l : w.ecology.landmarks) marked.insert(grid->node_index(l.x, l.z));
    check::is_true(!marked.empty(), "a landmark, or nothing is tested");
    std::size_t on_a_mark = 0;
    for (const KeyLocationSite& site : w.key_locations.sites) {
        // Scatter sites only: a cluster member is placed at an offset and never asks the
        // claimed set, so one standing on a landmark proves nothing about it.
        if (site.placement == KeyLocationPlacement::Node && marked.count(site.node) != 0) ++on_a_mark;
    }
    check::is_true(on_a_mark > 0, "a scattered key location on a landmark's cell, as the reference allows");
}

TEST(a_world_with_free_moist_ground_places_a_logging_camp_and_weighs_its_river) {
    // The finding this pins: no world of this port placed a logging camp, so no key location
    // carried `suitability_factors.river`, while every reference world measured places one.
    // Moist ground qualified; lairs crowded onto the hamlets' ground and a clearance the
    // reference does not keep refused the rest. The reference's world for this request
    // places five.
    const World& w = complete_65();
    const KeyLocationDiagnostic* row = diagnostic(w, "logging_camp");
    check::is_true(row != nullptr, "a diagnostic row for the logging camp");
    check::is_true(row->candidates > 0, "moist ground for a logging camp to qualify on");
    const std::size_t camps = placed_on_free_ground(w, "logging_camp", "river");
    check::is_true(camps > 0, "a logging camp on this world; the pass said: " + row->reason);
    check::equal(static_cast<std::size_t>(row->placed), camps, "the diagnostic to count the camps placed");
}

TEST(a_world_with_free_volcanic_ground_places_a_geyser_basin_and_a_lava_tube) {
    // Suppressed the same way as the logging camp, and for the same two reasons. The
    // reference's world for this request places two geyser basins and no lava tube: its one
    // free lava-tube cell is inside a city's clearance. This world frees four.
    const World& w = complete_65();
    for (const std::string_view kind : {std::string_view("geyser_basin"), std::string_view("lava_tube")}) {
        const KeyLocationDiagnostic* row = diagnostic(w, kind);
        check::is_true(row != nullptr, "a diagnostic row for " + std::string(kind));
        check::is_true(row->candidates > 0, "volcanic ground for " + std::string(kind) + " to qualify on");
        const std::size_t placed = placed_on_free_ground(w, kind, "volcanic");
        check::is_true(placed > 0, "a " + std::string(kind) + " on this world; the pass said: " + row->reason);
    }
}

TEST(stage_sixteen_lays_out_a_castle_for_every_fortress) {
    const World w = at_phase(16, 65);
    check::is_true(!w.humans.fortresses.empty(), "a fortress, or nothing is tested");
    check::equal(w.castle_plans.castles.size(), w.humans.fortresses.size(), "a castle plan for every fortress");

    std::set<std::string> fortresses;
    for (const RuralSite& f : w.humans.fortresses) fortresses.insert(f.uid);
    long long garrisoned = 0;
    for (std::size_t i = 0; i < w.castle_plans.castles.size(); ++i) {
        const CastlePlan& plan = w.castle_plans.castles[i];
        check::is_true(fortresses.count(plan.fortress_uid) != 0, plan.fortress_id + " to stand on a fortress");
        if (i > 0) {
            check::is_true(w.castle_plans.castles[i - 1].fortress_id < plan.fortress_id,
                           "castles in ascending id-string order");
        }
        for (const CastlePlot& plot : plan.plots) garrisoned += plot.workers;
    }
    check::is_true(garrisoned > 0, "some castle to need a garrison");

    // The rosters open one post per garrison worker, so a castle's people live at the
    // fortress's own node.
    long long at_forts = 0;
    for (const NpcPerson& p : w.npcs.people) {
        if (p.site_uid && p.site_uid->rfind("fortress-node-", 0) == 0) ++at_forts;
    }
    check::is_true(at_forts > 0, "somebody to hold a fortress");
}

TEST(a_city_plan_reads_its_threat_as_its_defense_priority) {
    // The planner builds walls against what the threat assessment says a city expects. The
    // nomads raise a city's threat AFTER the plans are drawn, so only a city no raider
    // reaches can be compared directly -- and some such city must expect something.
    const World w = at_phase(16);
    std::map<std::string, const ThreatReport*> reports;
    for (const ThreatReport& t : w.threat_assessments.cities) reports[t.city_uid] = &t;
    std::size_t compared = 0;
    for (const CityPlan& plan : w.city_plans.cities) {
        const auto it = reports.find(plan.city_uid);
        check::is_true(it != reports.end(), plan.city_uid + " to have a threat report");
        if (!it->second->nomad_contributors.empty()) continue;
        check::equal(plan.location.defense_priority, it->second->regional_threat,
                     plan.city_uid + " to defend against the threat it was assessed");
        compared += it->second->regional_threat > 0.0 ? 1 : 0;
    }
    check::is_true(compared > 0, "some unraided city under threat, or the join is untested");
}

TEST(the_raiders_raise_a_towns_threat_after_its_walls_are_drawn) {
    // The reference writes its raid pressure into the threat block itself: every town's
    // pressure, and a raised regional_threat where any raider is in reach.
    const World w = at_phase(16, 65);
    check::is_true(!w.nomad_effects.raid_pressure.empty(), "some raid pressure, or nothing is tested");
    std::size_t raised = 0;
    for (const NomadRaidPressure& raid : w.nomad_effects.raid_pressure) {
        const ThreatReport* report = nullptr;
        for (const ThreatReport& t : w.threat_assessments.cities) {
            if (t.city_uid == raid.city_uid) report = &t;
        }
        check::is_true(report != nullptr, raid.city_uid + " to be an assessed town");
        check::is_true(report->nomad_pressure.has_value(), raid.city_uid + " to record its raid pressure");
        check::equal(*report->nomad_pressure, raid.nomad_pressure, raid.city_uid + " raid pressure");
        if (!raid.regional_threat) continue;
        ++raised;
        check::equal(report->regional_threat, *raid.regional_threat, raid.city_uid + " raised threat");
        check::is_true(report->nomad_contributors == raid.contributors, raid.city_uid + " to name its raiders");
    }
    check::is_true(raised > 0, "some town a raider reaches, or the raise is untested");
}

TEST(the_society_pass_re_sites_every_witch_hut_the_ecology_placed) {
    // Stage 12 replaces the ecology pass's huts ("witch_huts-<node>") with its own re-sited
    // ones ("witch-hut-<node>"), each carrying the suitability it was judged too poor by.
    const World before = at_phase(11);
    const World after = at_phase(12);
    std::size_t placed = 0;
    for (const Landmark& l : before.ecology.landmarks) placed += l.kind == "witch_hut" ? 1 : 0;
    check::is_true(placed > 0, "the ecology pass to place some hut, or nothing is tested");
    std::size_t resited = 0;
    for (const Landmark& l : after.ecology.landmarks) {
        if (l.kind != "witch_hut") continue;
        ++resited;
        check::is_true(l.id.rfind("witch-hut-", 0) == 0, l.id + " to be the society pass's hut");
        check::is_true(l.conventional_suitability.has_value() && !l.reason.empty(), l.id + " to say why it stands there");
    }
    check::is_true(resited > 0 && resited <= placed, "up to as many huts as the ecology pass placed");
}

// The cast and the key locations measure their distances per grid node, as the reference's
// passes read world['spacing_m'] -- the grid step, not the settlement spacing. The two differ
// by more than half on this world, so an adapter handing over the wrong one cannot pass.
TEST(the_cast_and_the_key_locations_measure_in_grid_metres) {
    const World& w = complete_65();
    check::is_true(std::abs(w.spacing_m - w.effective.settlement_spacing) > 0.5 * w.spacing_m,
                   "the grid step and the settlement spacing to differ, or nothing is tested");
    check::equal(heroes_view(w).spacing_m, w.spacing_m, "the cast reads the grid step");
    const std::shared_ptr<const SphereGrid> grid = sphere_grid(w.config.size, w.effective.globe_radius);
    check::equal(key_location_view(w, *grid).spacing_m, w.spacing_m, "the key locations read the grid step");
}

