// Fortresses (ruled 2026-09-23): who asks for one, where each kind stands, how far apart
// they keep, and that a fort stands while the city that garrisons it stands.
//
// A capital and a city that has fought each ask for one road fort on their own roads. Two
// civilizations joined by a road ask for one frontier fort beside the narrowest point of the
// natural route between their lands. The corridor analysis behind the second is tested on
// its own, on a plain with a wall and a gate, where the right answer is known.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "fwg/ages.hpp"
#include "fwg/grid.hpp"
#include "fwg/humans.hpp"
#include "fwg/routing.hpp"
#include "fwg/scale.hpp"
#include "fwg/settlement.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

using Frontier = std::pair<std::string, std::string>;

// Cities and roads, and nothing yet placed on them: the pass under test runs by hand.
const World& roads_world() {
    static const World world = [] {
        Request r;
        r.seed = 42;
        r.size = 65;
        r.phase = 11;
        return generate(resolve(r));
    }();
    return world;
}

HumanHinterland hinterland(const World& world, std::vector<int> wars = {}, std::vector<RuralSite> previous = {}) {
    HumansRequest request;
    request.world = &world;
    request.war_history = std::move(wars);
    request.previous_fortresses = std::move(previous);
    return add_humans(request);
}

std::set<int> cities_on_roads(const World& w) {
    std::set<int> out;
    for (const Road& road : w.roads.routes) {
        out.insert(road.from);
        out.insert(road.to);
    }
    return out;
}

std::set<int> road_nodes(const World& w) {
    std::set<int> out;
    for (const Road& road : w.roads.routes) out.insert(road.nodes.begin(), road.nodes.end());
    return out;
}

// Pairs of peoples a road joins, each in id order.
std::set<Frontier> frontiers_of(const World& w) {
    std::set<Frontier> out;
    for (const Road& road : w.roads.routes) {
        const std::string& a = w.settlements.sites.at(static_cast<std::size_t>(road.from)).population_profile;
        const std::string& b = w.settlements.sites.at(static_cast<std::size_t>(road.to)).population_profile;
        if (a != b) out.emplace(std::min(a, b), std::max(a, b));
    }
    return out;
}

bool beside(const SphereGrid& grid, int a, int b) {
    if (a == b) return true;
    for (const SphereNeighbour& nb : grid.neighbours[static_cast<std::size_t>(a)]) {
        if (nb.index == b) return true;
    }
    return false;
}

double metres_between(const World& w, int a, int b) {
    const auto grid = sphere_grid(w.effective.size, w.effective.globe_radius);
    const Vec3 p = direction(grid->point_x[static_cast<std::size_t>(a)], grid->point_z[static_cast<std::size_t>(a)],
                             w.effective.size);
    const Vec3 q = direction(grid->point_x[static_cast<std::size_t>(b)], grid->point_z[static_cast<std::size_t>(b)],
                             w.effective.size);
    const double dot = std::max(-1.0, std::min(1.0, p.x * q.x + p.y * q.y + p.z * q.z));
    return w.effective.globe_radius * std::acos(dot);
}

bool is_capital(const World& w, int city) {
    return w.settlements.sites.at(static_cast<std::size_t>(city)).city_class == CityClass::Capital;
}

// --- the walled plain ------------------------------------------------------
// A sphere with two walls from pole to pole -- one down the seam with no gap, one across the
// middle with a single gate -- and the polar caps blocked, so the only way between the two
// halves is the gate. Land A is a city west of the gate's wall, land B one east of it.
struct WalledPlain {
    std::shared_ptr<const SphereGrid> grid;
    int a = -1;
    int b = -1;
    int gate = -1;
    std::vector<double> from_a;
    std::vector<double> from_b;
};

// `wall_x` is the gated wall's column; `gate_z` its gate's row, or -1 for no gate.
WalledPlain walled_plain(int wall_x, int gate_z, bool walled = true) {
    WalledPlain p;
    p.grid = sphere_grid(33, 10000.0);
    const SphereGrid& g = *p.grid;
    std::vector<char> blocked(g.node_count(), 0);
    for (std::size_t i = 0; i < g.node_count(); ++i) {
        const int x = g.point_x[i];
        const int z = g.point_z[i];
        if (!walled) continue;
        const bool cap = z <= 2 || z >= 30;
        const bool seam = x == 0;
        const bool wall = x == wall_x && z != gate_z;
        blocked[i] = cap || seam || wall ? 1 : 0;
    }
    p.a = g.node_index(6, 16);
    p.b = g.node_index(26, 16);
    if (gate_z >= 0) p.gate = g.node_index(wall_x, gate_z);
    const auto cost = [&](int i, int j, double d) -> std::optional<double> {
        if (blocked[static_cast<std::size_t>(i)] != 0 || blocked[static_cast<std::size_t>(j)] != 0) return std::nullopt;
        return d;
    };
    p.from_a = allocate_access(g, {{p.a, 0}}, cost).distance;
    p.from_b = allocate_access(g, {{p.b, 0}}, cost).distance;
    return p;
}

}  // namespace

// --- the corridor analysis --------------------------------------------------

TEST(the_narrowest_crossing_of_a_walled_plain_is_its_gate) {
    const WalledPlain p = walled_plain(16, 16);
    const std::vector<Crossing> crossings = narrowest_crossings(p.from_a, p.from_b, p.grid->areas);
    check::is_true(!crossings.empty(), "a route through the gate");
    const Crossing& c = crossings.front();
    check::is_true(beside(*p.grid, c.node, p.gate), "the narrowest crossing at the gate or beside it");
    check::close(c.route_cost, p.from_a[static_cast<std::size_t>(p.gate)] + p.from_b[static_cast<std::size_t>(p.gate)],
                 1e-6, "the cheapest route runs through the gate");
    for (std::size_t k = 1; k < crossings.size(); ++k) {
        check::is_true(crossings[k].node != c.node, "each slice offers its own crossing");
    }
}

TEST(a_gate_near_one_land_is_found_there_and_not_pulled_to_the_middle) {
    // The gated wall three columns from land A: the gate sits about a fifth of the way
    // across, where an open plain's corridor would also be narrowing.
    const WalledPlain p = walled_plain(10, 13);
    const std::vector<Crossing> crossings = narrowest_crossings(p.from_a, p.from_b, p.grid->areas);
    check::is_true(!crossings.empty(), "a route through the gate");
    const double t = p.from_a[static_cast<std::size_t>(p.gate)] /
                     (p.from_a[static_cast<std::size_t>(p.gate)] + p.from_b[static_cast<std::size_t>(p.gate)]);
    check::between(t, 0.2, 0.4, "the gate off the middle, inside the searched band");
    check::is_true(beside(*p.grid, crossings.front().node, p.gate), "the narrowest crossing still the gate");
}

TEST(an_open_plain_has_no_crossing_as_narrow_as_a_gate) {
    const WalledPlain gated = walled_plain(16, 16);
    const WalledPlain open = walled_plain(16, 16, /*walled=*/false);
    const std::vector<Crossing> through_gate = narrowest_crossings(gated.from_a, gated.from_b, gated.grid->areas);
    const std::vector<Crossing> across_open = narrowest_crossings(open.from_a, open.from_b, open.grid->areas);
    check::is_true(!through_gate.empty() && !across_open.empty(), "both lands joined");
    check::is_true(across_open.front().width_km2 > through_gate.front().width_km2,
                   "open ground wider at its narrowest than a gate");
}

TEST(a_slice_is_judged_against_the_corridor_an_open_plain_would_have_there) {
    // Two slices of one route, both on its cheapest path (from_a + from_b = 10): one a fifth
    // of the way across with 0.8 km2 of ground, one midway with 0.9. Raw, the first is the
    // narrower -- but an open plain's corridor there is only sqrt(0.225 * 0.775) /
    // sqrt(0.475 * 0.525) = 84% as wide as midway, so the midway slice squeezes harder.
    const std::vector<double> from_a = {2.25, 4.75};
    const std::vector<double> from_b = {7.75, 5.25};
    const std::vector<double> areas_m2 = {0.8e6, 0.9e6};
    const std::vector<Crossing> c = narrowest_crossings(from_a, from_b, areas_m2);
    check::equal(c.size(), std::size_t{2}, "two occupied slices");
    check::equal(c.front().node, 1, "the midway slice the narrower for where it lies");
    check::close(c.front().width_km2, 0.9, 1e-12, "its own ground reported, not the judged width");
    check::close(c.front().route_cost, 10.0, 1e-12, "the cheapest route's cost");
}

TEST(a_route_too_short_for_the_band_offers_its_cheapest_interior_node) {
    // A tenth and nine tenths of the way across, and the two cities themselves: nothing in
    // the band. The cities are never a crossing; the interior node on the cheaper route is.
    const std::vector<double> from_a = {0.0, 1.0, 9.1, 10.0};
    const std::vector<double> from_b = {10.0, 9.0, 0.9, 0.0};
    const std::vector<double> areas_m2 = {1e6, 1e6, 1e6, 1e6};
    const std::vector<Crossing> c = narrowest_crossings(from_a, from_b, areas_m2);
    check::equal(c.size(), std::size_t{1}, "one crossing offered");
    check::equal(c.front().node, 1, "the interior node on the cheapest route, not a city");
}

TEST(lands_no_ground_joins_have_no_crossing) {
    const WalledPlain p = walled_plain(16, -1);
    check::is_true(!std::isfinite(p.from_a[static_cast<std::size_t>(p.b)]), "the wall seals, or nothing is tested");
    check::is_true(narrowest_crossings(p.from_a, p.from_b, p.grid->areas).empty(), "no crossing where no route is");
}

// --- who asks, and where the forts stand -------------------------------------

TEST(in_peace_only_a_capital_on_a_road_asks_for_a_road_fort_and_it_holds_one) {
    const World& w = roads_world();
    const HumanHinterland h = hinterland(w);
    const auto grid = sphere_grid(w.effective.size, w.effective.globe_radius);
    const std::set<int> on_road = cities_on_roads(w);
    const std::set<int> used = road_nodes(w);
    std::set<int> asking;
    for (const int city : on_road) {
        if (is_capital(w, city)) asking.insert(city);
    }
    check::is_true(asking.size() >= 2, "capitals on roads, or nothing is tested");

    const FortressDemand& d = h.fortress_demand;
    check::equal(d.city_ceiling, static_cast<int>(asking.size()), "every capital on a road asks, and nothing else");
    check::equal(d.veteran_demand, 0, "no war, no veteran's fort");
    check::equal(d.veteran_cities, 0, "no veterans");
    std::map<int, int> held;
    for (const RuralSite& f : h.fortresses) {
        if (f.watch != "road") continue;
        const SettlementSite& city = w.settlements.sites.at(static_cast<std::size_t>(f.core_id));
        check::is_true(asking.count(f.core_id) != 0, f.uid + " garrisoned by a city that asked");
        check::is_true(++held[f.core_id] == 1, f.uid + ": one road fort to a city");
        check::equal(f.core_uid, city.uid, f.uid + " names its city by the uid an age keeps");
        check::equal(f.access_nodes.back(), city.node, f.uid + " reached from its own city");
        check::is_true(used.count(f.protected_route_node) != 0, f.uid + " guards a node a road uses");
        check::is_true(beside(*grid, f.node, f.protected_route_node), f.uid + " on that node or beside it");
        check::equal(f.raised_age, static_cast<long long>(w.age), f.uid + " raised this age");
    }
    check::is_true(held.size() >= 2, "road forts, or nothing is tested");
    check::equal(d.road_forts, static_cast<int>(held.size()), "the demand counts them");
}

TEST(war_adds_one_road_fort_to_each_city_that_fought_however_many_wars) {
    const World& w = roads_world();
    std::vector<int> veterans;
    for (const int city : cities_on_roads(w)) {
        if (!is_capital(w, city) && veterans.size() < 2) veterans.push_back(city);
    }
    check::equal(veterans.size(), std::size_t{2}, "two cities that are not capitals, on roads");
    std::vector<int> wars(w.settlements.sites.size(), 0);
    wars[static_cast<std::size_t>(veterans[0])] = 1;
    wars[static_cast<std::size_t>(veterans[1])] = 9;

    const HumanHinterland peace = hinterland(w);
    const HumanHinterland fought = hinterland(w, wars);
    check::equal(fought.fortress_demand.veteran_cities, 2, "two cities fought");
    check::equal(fought.fortress_demand.veteran_demand, 2, "each asks for one road fort, however many wars");
    check::equal(fought.fortress_demand.city_ceiling, peace.fortress_demand.city_ceiling + 2,
                 "and the asks rise by exactly those two");
    std::map<int, int> held;
    for (const RuralSite& f : fought.fortresses) {
        if (f.watch == "road") ++held[f.core_id];
    }
    for (const int city : veterans) {
        check::is_true(held[city] <= 1, "a veteran holds one road fort at most");
    }
    check::is_true(held[veterans[0]] + held[veterans[1]] >= 1, "some veteran holds its fort, or nothing is tested");
}

TEST(each_pair_of_peoples_a_road_joins_is_watched_once_from_beside_its_narrowest_crossing) {
    const World& w = roads_world();
    const HumanHinterland h = hinterland(w);
    const auto grid = sphere_grid(w.effective.size, w.effective.globe_radius);
    const std::set<Frontier> frontiers = frontiers_of(w);
    check::is_true(frontiers.size() >= 2, "peoples a road joins, or nothing is tested");
    check::equal(h.fortress_demand.frontiers, static_cast<int>(frontiers.size()), "every such pair a frontier");
    std::set<int> city_nodes;
    for (const SettlementSite& s : w.settlements.sites) city_nodes.insert(s.node);

    std::set<Frontier> watched;
    for (const RuralSite& f : h.fortresses) {
        if (f.watch != "frontier") continue;
        const Frontier pair{f.frontier_a, f.frontier_b};
        const SettlementSite& garrison = w.settlements.sites.at(static_cast<std::size_t>(f.core_id));
        check::is_true(f.frontier_a < f.frontier_b, f.uid + " names its peoples in id order");
        check::is_true(frontiers.count(pair) != 0, f.uid + " watches peoples a road joins");
        check::is_true(watched.insert(pair).second, f.uid + ": one fort to a frontier");
        check::is_true(garrison.population_profile == f.frontier_a || garrison.population_profile == f.frontier_b,
                       f.uid + " garrisoned by one of its two peoples");
        check::equal(f.core_uid, garrison.uid, f.uid + " names its garrison by uid");
        check::equal(f.access_nodes.back(), garrison.node, f.uid + " reached from its garrison");
        check::is_true(city_nodes.count(f.protected_route_node) == 0, f.uid + "'s crossing lies between the lands");
        check::is_true(beside(*grid, f.node, f.protected_route_node), f.uid + " on its crossing or beside it");
        check::is_true(f.corridor_km2 > 0.0, f.uid + " publishes how narrow its crossing runs");
    }
    check::is_true(watched.size() >= 2, "frontier forts, or nothing is tested");
    check::equal(h.fortress_demand.frontier_forts, static_cast<int>(watched.size()), "the demand counts them");

    // The asks no fort answers, exactly: capitals on roads without a road fort, and the
    // frontiers without a frontier fort.
    std::set<int> holding;
    for (const RuralSite& f : h.fortresses) {
        if (f.watch == "road") holding.insert(f.core_id);
    }
    int unanswered = 0;
    for (const int city : cities_on_roads(w)) {
        if (is_capital(w, city) && holding.count(city) == 0) ++unanswered;
    }
    for (const Frontier& pair : frontiers) {
        if (watched.count(pair) == 0) ++unanswered;
    }
    check::equal(h.fortress_demand.unanswered, unanswered, "the unanswered asks published exactly");
}

TEST(no_two_forts_stand_closer_than_the_reference_spacing_scaled_with_the_world) {
    const World& w = roads_world();
    const HumanHinterland h = hinterland(w);
    const double spacing = 200.0 * scale::reach_scale(2.0 * std::numbers::pi * w.effective.globe_radius);
    check::close(h.fortress_demand.spacing_m, spacing, 1e-6, "the reference's 200 m, scaled as the support reach is");
    check::is_true(spacing > w.spacing_m, "wider than one cell here, so the rule binds and is tested");
    check::is_true(h.fortresses.size() >= 4, "forts to space, or nothing is tested");
    for (std::size_t i = 0; i < h.fortresses.size(); ++i) {
        for (std::size_t j = i + 1; j < h.fortresses.size(); ++j) {
            check::is_true(metres_between(w, h.fortresses[i].node, h.fortresses[j].node) >= spacing,
                           h.fortresses[i].uid + " and " + h.fortresses[j].uid + " kept apart");
        }
        for (const SettlementSite& city : w.settlements.sites) {
            check::is_true(metres_between(w, h.fortresses[i].node, city.node) >= spacing,
                           h.fortresses[i].uid + " kept off " + city.uid);
        }
    }
}

// --- forts stand -----------------------------------------------------------

TEST(a_fort_stands_through_a_rebuild_and_answers_its_ask) {
    const World& w = roads_world();
    std::vector<int> wars(w.settlements.sites.size(), 0);
    for (const int city : cities_on_roads(w)) wars[static_cast<std::size_t>(city)] = 1;
    const HumanHinterland first = hinterland(w, wars);
    check::is_true(first.fortresses.size() >= 4, "forts to carry, or nothing is tested");

    const HumanHinterland again = hinterland(w, wars, first.fortresses);
    check::equal(again.fortress_demand.carried, static_cast<int>(first.fortresses.size()), "every fort carried");
    check::equal(again.fortress_demand.proposed, first.fortress_demand.unanswered,
                 "only the asks no fort answered are asked again");
    check::equal(again.fortresses.size(), first.fortresses.size(),
                 "and the ground that could not answer them still cannot");
    for (std::size_t k = 0; k < first.fortresses.size(); ++k) {
        const RuralSite& was = first.fortresses[k];
        const RuralSite& now = again.fortresses[k];
        check::equal(now.uid, was.uid, was.uid + " stands where it stood");
        check::equal(now.watch, was.watch, was.uid + " watches what it watched");
        check::equal(now.core_uid, was.core_uid, was.uid + " held by the same city");
        check::equal(now.raised_age, was.raised_age, was.uid + " keeps the age that raised it");
        check::equal(now.protected_route_node, was.protected_route_node, was.uid + " guards what it guarded");
        check::equal(now.id, "fortress-" + std::to_string(k), "ordinals reissued in list order");
    }
}

TEST(a_fort_falls_with_its_city_and_the_new_citys_ask_is_answered_afresh) {
    const World& w = roads_world();
    const HumanHinterland first = hinterland(w);
    const RuralSite* road_fort = nullptr;
    for (const RuralSite& f : first.fortresses) {
        if (f.watch == "road") {
            road_fort = &f;
            break;
        }
    }
    check::is_true(road_fort != nullptr, "a road fort, or nothing is tested");
    const int city = road_fort->core_id;
    const std::string fallen = road_fort->core_uid;

    // The city that garrisoned it is gone: a new one of the same people stands on its node.
    World refounded = w;
    refounded.settlements.sites.at(static_cast<std::size_t>(city)).uid = fallen + "-refounded";
    const HumanHinterland again = hinterland(refounded, {}, first.fortresses);
    int garrisoned_by_fallen = 0;
    for (const RuralSite& f : first.fortresses) garrisoned_by_fallen += f.core_uid == fallen ? 1 : 0;
    check::equal(again.fortress_demand.carried, static_cast<int>(first.fortresses.size()) - garrisoned_by_fallen,
                 "every fort but the fallen city's carried");
    bool answered = false;
    for (const RuralSite& f : again.fortresses) {
        check::is_true(f.core_uid != fallen, f.uid + " is not held by a city that fell");
        if (f.watch == "road" && f.core_id == city) answered = f.core_uid == fallen + "-refounded";
    }
    check::is_true(answered, "the new city, a capital, raises its own road fort");
}

TEST(a_fort_on_ground_that_was_lost_falls) {
    const World& w = roads_world();
    const HumanHinterland first = hinterland(w);
    check::is_true(!first.fortresses.empty(), "a fort to copy, or nothing is tested");
    const auto grid = sphere_grid(w.effective.size, w.effective.globe_radius);

    // Built over: a city now stands on its node.
    RuralSite built_over = first.fortresses.front();
    built_over.node = w.settlements.sites.front().node;
    // Drowned: its node is now water.
    RuralSite drowned = first.fortresses.front();
    const Grid& water = w.layers.at("water_type");
    for (std::size_t i = 0; i < grid->node_count(); ++i) {
        if (water.at(grid->point_x[i], grid->point_z[i]) != 0.0) {
            drowned.node = static_cast<int>(i);
            break;
        }
    }
    check::is_true(drowned.node != first.fortresses.front().node, "a water node, or nothing is tested");
    const HumanHinterland again = hinterland(w, {}, {built_over, drowned});
    check::equal(again.fortress_demand.carried, 0, "neither is carried");
}

TEST(an_age_turn_carries_every_fort_whose_city_stands) {
    Request r;
    r.seed = 42;
    r.size = 33;
    const World before = generate(resolve(r));
    AgeAdvanceRequest one;
    one.steps = 1;
    const World after = advance_ages(before, one);

    std::map<std::string, const RuralSite*> now;
    for (const RuralSite& f : after.humans.fortresses) now[f.uid] = &f;
    std::set<std::string> standing;
    std::set<int> city_nodes;
    for (const SettlementSite& s : after.settlements.sites) {
        standing.insert(s.uid);
        city_nodes.insert(s.node);
    }
    int carried = 0;
    for (const RuralSite& f : before.humans.fortresses) {
        if (f.watch == "player" || standing.count(f.core_uid) == 0) continue;
        if (city_nodes.count(f.node) != 0) continue;  // built over this age
        const auto it = now.find(f.uid);
        check::is_true(it != now.end(), f.uid + " stands with " + f.core_uid);
        check::equal(it->second->raised_age, f.raised_age, f.uid + " keeps the age that raised it");
        check::equal(it->second->watch, f.watch, f.uid + " watches what it watched");
        ++carried;
    }
    check::is_true(carried > 0, "a fort whose city stood through the age, or nothing is tested");
    for (const RuralSite& f : after.humans.fortresses) {
        if (f.watch == "player") continue;
        check::is_true(standing.count(f.core_uid) != 0, f.uid + " held by a city that stands");
        check::is_true(f.raised_age <= after.age, f.uid + " raised no later than now");
    }
}
