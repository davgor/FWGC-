// Stage 12, second half: harbours, fisheries, the sea and air networks, and the
// authoritative food stress test over all three at once.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>

#include "fwg/civilizations.hpp"
#include "fwg/routing.hpp"
#include "fwg/society.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

// The whole of stage 12 in one place, because society reads what humans and the seasonal
// pass wrote and there is no point testing it against anything else.
struct Stage12 {
    World world;
    HumanHinterland humans;
    SeasonalFood seasonal;
    SocietyReport society;
};

Stage12 stage12(int size = 65, std::uint32_t seed = 42, const Grid* suitability = nullptr) {
    Stage12 out;
    Request r;
    r.seed = seed;
    r.size = size;
    r.phase = 11;
    out.world = generate(resolve(r));

    HumansRequest hr;
    hr.world = &out.world;
    out.humans = add_humans(hr);

    SeasonalFoodRequest sr;
    sr.world = &out.world;
    sr.humans = &out.humans;
    out.seasonal = add_seasonal_food(sr);

    SocietyRequest cr;
    cr.world = &out.world;
    cr.humans = &out.humans;
    cr.seasonal = &out.seasonal;
    cr.suitability = suitability;
    out.society = add_world_society(cr);
    return out;
}

}  // namespace

// --- gates and the sky -----------------------------------------------------

TEST(the_pass_refuses_outright_before_phase_six_and_writes_only_the_sky_before_phase_nine) {
    const Stage12 s = stage12();
    check::is_true(s.society.ran, "a stage 11 world to run the pass");
    check::is_true(s.society.has_economy, "and to reach the economy");

    World early = s.world;
    early.effective.phase = 5;
    SocietyRequest request;
    request.world = &early;
    request.humans = &s.humans;
    request.seasonal = &s.seasonal;
    const SocietyReport nothing = add_world_society(request);
    check::is_true(!nothing.ran, "phase 5 to write nothing at all");
    check::is_true(nothing.sky.method.empty(), "not even the sky block");

    World middle = s.world;
    middle.effective.phase = 8;
    request.world = &middle;
    const SocietyReport sky_only = add_world_society(request);
    check::is_true(sky_only.ran, "phase 8 to write the sky block");
    check::is_true(!sky_only.sky.method.empty(), "the sky block to carry its method");
    check::is_true(!sky_only.has_economy, "and to stop before the economy");
    check::is_true(sky_only.fisheries.ports.empty(), "and to found no harbours");
}

TEST(the_sky_is_suspended_on_the_only_recipe_the_generator_ships) {
    // make_sky returns nothing on recipe 3, which is the only live recipe, so no world
    // this generator can build has a sky island. The block still says so out loud rather
    // than looking like an empty result.
    const Stage12 s = stage12();
    check::equal(s.world.effective.world_recipe, 3, "the shipped recipe");
    check::is_true(s.society.sky.islands.empty(), "no island on recipe 3");
    check::is_true(s.society.sky.settlements.empty(), "and no sky settlement");
    check::equal(s.society.sky.area_km2, 0.0, "and no area");
    check::is_true(!s.society.sky.enabled, "the block to report itself disabled");
    check::equal(s.society.sky.disabled_reason, std::string("Sky islands suspended in recipe 3"),
                 "and to say why");
    // Every economy row is therefore a surface city, never a sky settlement.
    check::equal(s.society.world_economy.sites.size(), s.world.settlements.sites.size(),
                 "one economy row per surface city and no more");
    for (const EconomySite& row : s.society.world_economy.sites) {
        check::is_true(row.sky_site_id.empty(), "no economy row to belong to an island");
    }
}

// --- harbours --------------------------------------------------------------

TEST(no_harbour_stands_on_a_node_a_city_hamlet_or_fortress_already_holds) {
    // Fortresses belong in the occupied set as much as cities and hamlets do. Left out,
    // half the forts in a reference world shared their exact node with a harbour, which
    // draws as one icon on top of another and means two settlements occupy one place.
    const Stage12 s = stage12();
    check::is_true(!s.society.fisheries.ports.empty(), "harbours, or this proves nothing");
    check::is_true(!s.humans.fortresses.empty(), "fortresses, or the rule has nothing to exclude");

    std::set<int> taken;
    for (const SettlementSite& site : s.world.settlements.sites) taken.insert(site.node);
    for (const RuralSite& x : s.humans.hamlets) taken.insert(x.node);
    for (const RuralSite& x : s.humans.fortresses) taken.insert(x.node);
    const std::size_t before = taken.size();

    for (const HarbourPort& port : s.society.fisheries.ports) {
        check::is_true(taken.insert(port.node).second, port.id + " to hold its node alone");
    }
    check::equal(taken.size(), before + s.society.fisheries.ports.size(),
                 "every harbour to have added a node of its own");
}

TEST(a_harbour_launches_into_navigable_water_and_walks_home_over_land) {
    const Stage12 s = stage12();
    const auto held = sphere_grid(s.world.effective.size, s.world.effective.globe_radius);
    const Grid& water = s.world.layers.at("water_type");
    const Grid& depth = s.world.layers.at("water_depth");
    const double draft = s.world.effective.options[Option::sea_draft];
    check::is_true(!s.society.fisheries.ports.empty(), "harbours, or this proves nothing");

    for (const HarbourPort& port : s.society.fisheries.ports) {
        const auto node = static_cast<std::size_t>(port.node);
        const auto sea = static_cast<std::size_t>(port.sea_node);
        check::equal(water.at(held->point_x[node], held->point_z[node]), 0.0, port.id + " to stand on land");
        check::equal(water.at(held->point_x[sea], held->point_z[sea]), 1.0,
                     port.id + " to launch into the ocean, not a lake");
        check::is_true(depth.at(held->point_x[sea], held->point_z[sea]) >= draft,
                       port.id + " to launch into water deep enough to float a hull");
        check::is_true(port.landing_distance_m > 0.0, port.id + " to have a real landing distance");
        check::is_true(port.harbor_quality > 0.1, port.id + " to be a real anchorage");
        check::equal(port.trade_terminal, port.harbor_quality >= 0.4, port.id + " terminal status");
        check::equal(port.delivered_materials, 0.0, "a harbour to mine nothing");
        check::equal(port.irrigation_benefit, 0.0, "a harbour to farm nothing");

        // The path runs CITY FIRST, unlike a hamlet's, which runs outward.
        check::is_true(!port.access_nodes.empty(), port.id + " to have a path home");
        check::equal(port.access_nodes.front(),
                     s.world.settlements.sites[static_cast<std::size_t>(port.core_id)].node,
                     port.id + " to start at its city");
        check::equal(port.access_nodes.back(), port.node, port.id + " to end at itself");
        for (std::size_t k = 0; k + 1 < port.access_nodes.size(); ++k) {
            bool adjacent = false;
            for (const SphereNeighbour& nb : held->neighbours[static_cast<std::size_t>(port.access_nodes[k])]) {
                if (nb.index == port.access_nodes[k + 1]) adjacent = true;
            }
            check::is_true(adjacent, "step " + std::to_string(k) + " of " + port.id + " to be one step");
        }
        check::is_true(port.access_cost <= s.world.effective.support_reach,
                       port.id + " to stand inside the support reach");
    }
}

TEST(a_city_gets_no_more_harbours_than_the_option_allows) {
    const Stage12 s = stage12();
    const int allowed = s.world.effective.options.as_int(Option::coastal_hamlets);
    check::is_true(allowed > 0, "harbours to be allowed at all");
    std::map<int, int> per_city;
    for (const HarbourPort& port : s.society.fisheries.ports) ++per_city[port.core_id];
    check::is_true(!per_city.empty(), "some city to have a harbour");
    for (const auto& [city, harbours] : per_city) {
        check::is_true(harbours <= allowed, "city " + std::to_string(city) + " to stay under the ceiling");
    }
    // The port id carries the city and the GLOBAL port count, not a per-city count.
    for (std::size_t k = 0; k < s.society.fisheries.ports.size(); ++k) {
        const HarbourPort& port = s.society.fisheries.ports[k];
        check::equal(port.id, "coastal-" + std::to_string(port.core_id) + "-" + std::to_string(k),
                     "a port id to carry its city and its global ordinal");
    }
}

// --- fisheries -------------------------------------------------------------

TEST(every_reachable_ocean_cell_is_awarded_to_exactly_one_port) {
    const Stage12 s = stage12();
    const auto held = sphere_grid(s.world.effective.size, s.world.effective.globe_radius);
    check::is_true(!s.society.fisheries.ports.empty(), "harbours, or this proves nothing");

    std::set<int> claimed;
    std::size_t total = 0;
    for (std::size_t k = 0; k < s.society.fisheries.ports.size(); ++k) {
        for (const int node : s.society.fisheries.ports[k].fishing_nodes) {
            check::is_true(claimed.insert(node).second,
                           "node " + std::to_string(node) + " to be fished by one port only");
            ++total;
            // The published layer has to agree with the port's own list.
            check::equal(static_cast<int>(s.society.fishing_ground_owner.at(held->point_x[static_cast<std::size_t>(node)],
                                                                           held->point_z[static_cast<std::size_t>(node)])),
                         static_cast<int>(k), "the owner layer to name the same port");
        }
    }
    check::is_true(total > 0, "some water to actually be fished");

    // Nearest wins, and nothing is nearer to a port than the water it launches from. A
    // port must therefore hold its own sea node — unless it shares that node with an
    // earlier-founded port, in which case the tie goes to the lower index.
    std::map<int, int> earliest_launcher;
    for (std::size_t k = 0; k < s.society.fisheries.ports.size(); ++k) {
        const int sea = s.society.fisheries.ports[k].sea_node;
        if (earliest_launcher.find(sea) == earliest_launcher.end()) earliest_launcher[sea] = static_cast<int>(k);
    }
    check::is_true(!earliest_launcher.empty(), "some launching water, or this proves nothing");
    for (const auto& [sea, port] : earliest_launcher) {
        const auto us = static_cast<std::size_t>(sea);
        check::equal(static_cast<int>(s.society.fishing_ground_owner.at(held->point_x[us], held->point_z[us])),
                     port, "the water a port launches from to be fished by that port");
    }
}

TEST(delivered_fish_can_never_exceed_what_was_allocated) {
    // Delivery is an exponential of distance times an ice factor, and both are at most
    // one, so this is a structural guarantee rather than an observation about one world.
    const Stage12 s = stage12();
    const Fisheries& f = s.society.fisheries;
    check::is_true(f.potential_annual_food > 0.0, "some potential, or this proves nothing");
    check::is_true(f.delivered_annual_food > 0.0, "and some of it to arrive");
    check::is_true(f.delivered_annual_food <= f.potential_annual_food,
                   "delivery never to exceed the allocated potential");

    double summed = 0.0;
    for (const HarbourPort& port : f.ports) {
        double months = 0.0;
        for (const double month : port.monthly_fish) {
            check::is_true(month >= 0.0, "no negative catch");
            months += month;
        }
        check::close(port.delivered_food, months, std::max(months, 1.0) * 1e-9,
                     port.id + " to deliver what its months add up to");
        summed += port.delivered_food;
        const bool fishes = port.delivered_food > 0.0;
        const std::string expected = port.trade_terminal ? (fishes ? "harbor + fishing" : "harbor")
                                                         : (fishes ? "fishing" : "landing");
        check::equal(port.role, expected, port.id + " to be named for what it does");
        if (!port.fishing_nodes.empty()) check::is_true(port.worked_area_km2 > 0.0, port.id + " to work water");
    }
    check::close(f.delivered_annual_food, summed, std::max(summed, 1.0) * 1e-9,
                 "the block total to be the sum of its ports");
}

TEST(ice_can_only_reduce_a_catch_never_raise_one) {
    check::equal(fishing_access_factor(0.0, 0.0), 1.0, "open water to cost nothing");
    check::equal(fishing_access_factor(0.0, 1.0), 1.0, "open water to cost nothing whoever fishes it");
    check::close(fishing_access_factor(1.0, 0.25), 0.25, 1e-12, "full ice to leave the winter fraction");
    check::equal(fishing_access_factor(1.0, 0.0), 0.0, "and nothing at all without one");
    // Never a cold-weather bonus, and out-of-range inputs are clamped rather than trusted.
    for (int i = 0; i <= 10; ++i) {
        const double ice = i / 10.0;
        for (int j = 0; j <= 10; ++j) {
            const double keep = j / 10.0;
            check::between(fishing_access_factor(ice, keep), 0.0, 1.0, "the factor inside zero and one");
        }
    }
    check::equal(fishing_access_factor(5.0, 0.3), fishing_access_factor(1.0, 0.3), "ice above one to clamp");
    check::equal(fishing_access_factor(-5.0, 0.3), 1.0, "ice below zero to clamp");
    // Monotone: more ice is never better.
    for (int i = 1; i <= 10; ++i) {
        check::is_true(fishing_access_factor(i / 10.0, 0.4) <= fishing_access_factor((i - 1) / 10.0, 0.4),
                       "more ice never to help");
    }
}

// --- the multimodal network ------------------------------------------------

TEST(the_route_list_is_ground_then_sea_then_air_because_the_simulator_reads_it_in_order) {
    // Earlier routes get first claim on a seller's surplus and on a buyer's material
    // budget, so this order is a decision about who trades first, not a presentation one.
    const Stage12 s = stage12();
    const std::vector<TransportRoute>& routes = s.society.transport.routes;
    check::is_true(!routes.empty(), "routes, or this proves nothing");

    const auto rank = [](const std::string& mode) { return mode == "ground" ? 0 : (mode == "sea" ? 1 : 2); };
    int seen = -1;
    std::size_t ground = 0;
    std::size_t sea = 0;
    for (const TransportRoute& route : routes) {
        const int r = rank(route.mode);
        check::is_true(r >= seen, "the modes never to go backwards");
        seen = r;
        if (route.mode == "ground") ++ground;
        if (route.mode == "sea") ++sea;
        check::between(route.food.efficiency, 0.0, 1.0, "efficiency inside zero and one");
        for (const double capacity : route.food.capacity) {
            check::is_true(capacity >= 0.0, "no negative capacity");
        }
    }
    check::equal(ground, s.world.roads.routes.size(), "one ground route per road");
    check::is_true(sea > 0, "some sea trade, or the ordering proves nothing");

    // Ground routes carry the seasonal pass's own numbers, positionally aligned.
    for (std::size_t i = 0; i < ground; ++i) {
        check::equal(routes[i].food.from, s.seasonal.routes[i].from, "a ground route to keep its cities");
        check::equal(routes[i].food.price, s.seasonal.routes[i].price, "and its price");
        check::equal(routes[i].nodes.size(), s.world.roads.routes[i].nodes.size(), "and its road");
    }
}

TEST(a_sea_route_joins_two_trade_terminals_of_two_different_cities_and_only_once) {
    const Stage12 s = stage12();
    const auto held = sphere_grid(s.world.effective.size, s.world.effective.globe_radius);
    const Grid& water = s.world.layers.at("water_type");

    std::map<std::string, const HarbourPort*> by_id;
    for (const HarbourPort& port : s.society.fisheries.ports) by_id[port.id] = &port;

    std::set<std::pair<int, int>> pairs;
    std::size_t counted = 0;
    for (const TransportRoute& route : s.society.transport.routes) {
        if (route.mode != "sea") continue;
        ++counted;
        check::is_true(route.food.from < route.food.to, "a sea pair to be keyed low city first");
        check::is_true(pairs.insert({route.food.from, route.food.to}).second,
                       "one sea route per city pair and no more");
        const HarbourPort* a = by_id.at(route.port_a);
        const HarbourPort* b = by_id.at(route.port_b);
        check::is_true(a->trade_terminal && b->trade_terminal, "both ends to be trade terminals");
        check::is_true(a->core_id != b->core_id, "two ports of one city never to route to each other");
        // The simulator indexes the model list with these, and a model is a CITY. A route
        // that named its ports here would ship food to whichever city happens to sit at
        // that ordinal.
        check::equal(std::min(route.food.from, route.food.to), std::min(a->core_id, b->core_id),
                     "a sea route to name the cities it joins, not the harbours");
        check::equal(std::max(route.food.from, route.food.to), std::max(a->core_id, b->core_id),
                     "a sea route to name the cities it joins, not the harbours");
        check::is_true(!route.nodes.empty(), "a sea route to have a path");
        for (const int node : route.nodes) {
            check::equal(water.at(held->point_x[static_cast<std::size_t>(node)],
                                  held->point_z[static_cast<std::size_t>(node)]),
                         1.0, "a sea route to stay on the ocean");
        }
        check::equal(route.nodes.front(), a->sea_node, "a sea route to start at its port's water");
        check::equal(route.nodes.back(), b->sea_node, "and end at the other's");
        check::is_true(route.length_m <= s.world.effective.options[Option::sea_reach],
                       "a sea route to stay inside the navigable reach");
        check::is_true(route.cost >= route.length_m, "the priced cost to include both ground approaches");
    }
    check::is_true(counted > 0, "some sea routes, or this proves nothing");
}

TEST(no_air_route_can_exist_while_the_reach_stays_unscaled) {
    // air_reach is an absolute 4500 m authored for the 11 km reference world, and the
    // request builder does not scale world options. At every shipped width the nearest
    // pair of terminals is already tens of kilometres apart, so the reach test rejects
    // every pair before a path is ever sampled. This asserts the CAUSE, not just the
    // absence: if a future change scales the option, this test is the thing that notices.
    const Stage12 s = stage12();
    const std::vector<int>& terminals = s.society.transport.terminals;
    check::is_true(terminals.size() >= 2, "at least two terminals, or the reach proves nothing");

    const double reach = s.world.effective.options[Option::air_reach];
    const double radius = s.world.effective.globe_radius;
    double nearest = kUnreachable;
    for (std::size_t i = 0; i < terminals.size(); ++i) {
        for (std::size_t j = i + 1; j < terminals.size(); ++j) {
            const SettlementSite& a = s.world.settlements.sites[static_cast<std::size_t>(terminals[i])];
            const SettlementSite& b = s.world.settlements.sites[static_cast<std::size_t>(terminals[j])];
            const Vec3 va = direction(a.x, a.z, s.world.effective.size);
            const Vec3 vb = direction(b.x, b.z, s.world.effective.size);
            nearest = std::min(nearest, radius * std::acos(clamp_unit(dot(va, vb))));
        }
    }
    check::is_true(nearest > reach, "the closest pair of terminals to be beyond the authored air reach");

    std::size_t air = 0;
    for (const TransportRoute& route : s.society.transport.routes) {
        if (route.mode == "air") ++air;
    }
    check::equal(air, std::size_t{0}, "and therefore no air route to exist");
}

TEST(piracy_needs_both_a_pirate_coast_and_a_sea_link_to_prey_on) {
    const Stage12 s = stage12();
    check::equal(s.society.community_traits.size(), s.world.settlements.sites.size(),
                 "one trait list per city");
    std::size_t traits = 0;
    std::size_t pirates = 0;
    for (std::size_t i = 0; i < s.society.community_traits.size(); ++i) {
        const std::vector<std::string>& list = s.society.community_traits[i];
        traits += list.size();
        const bool pirate = std::find(list.begin(), list.end(), "pirate") != list.end();
        if (!pirate) continue;
        ++pirates;
        const SettlementSite& site = s.world.settlements.sites[i];
        const auto zone = s.world.layers.find("zone_pirate");
        check::is_true(zone != s.world.layers.end() && zone->second.at(site.x, site.z) > 0.02,
                       "a pirate city to sit in a pirate region");
        bool linked = false;
        for (const TransportRoute& route : s.society.transport.routes) {
            if (route.mode == "sea" && (route.food.from == site.id || route.food.to == site.id)) linked = true;
        }
        check::is_true(linked, "a pirate city to have trade to prey on");
        // Piracy is appended after the harbour traits, never instead of them.
        check::is_true(list.size() > 1, "a pirate city to have kept its maritime trait");
    }
    check::is_true(traits > 0, "some community traits, or this proves nothing");
    // No floor on pirates: a world may honestly have none. The loop above is the rule.
    check::is_true(pirates <= s.world.settlements.sites.size(), "pirates to be countable");
}

// --- witch huts ------------------------------------------------------------

TEST(a_witch_hut_takes_poor_isolated_ground_and_never_more_than_were_there) {
    const Stage12 s = stage12();
    const auto held = sphere_grid(s.world.effective.size, s.world.effective.globe_radius);
    const Grid& water = s.world.layers.at("water_type");

    std::size_t wanted = 0;
    for (const Landmark& mark : s.world.ecology.landmarks) {
        if (mark.kind == "witch_hut") ++wanted;
    }
    check::is_true(wanted > 0, "huts to re-site, or this proves nothing");
    check::is_true(s.society.witch_huts.size() <= wanted, "never to mint more huts than were there");
    check::is_true(!s.society.witch_huts.empty(), "and to actually re-site some");

    const int n = s.world.effective.size;
    const double radius = s.world.effective.globe_radius;
    for (std::size_t k = 0; k < s.society.witch_huts.size(); ++k) {
        const SocietyWitchHut& hut = s.society.witch_huts[k];
        // The id spelling differs deliberately from the ecology pass's "witch_huts-N".
        check::equal(hut.id, "witch-hut-" + std::to_string(held->node_index(hut.x, hut.z)),
                     "a hut id to embed the node it stands on");
        check::equal(hut.kind, std::string("witch_hut"), "a hut to know what it is");
        check::equal(water.at(hut.x, hut.z), 0.0, "a hut to stand on dry land");
        check::is_true(hut.intensity >= 0.02, "a hut to stand in real umbral influence");
        check::is_true(hut.conventional_suitability < 0.55, "and on ground nobody else wanted");

        const Vec3 p = direction(hut.x, hut.z, n);
        for (const SettlementSite& site : s.world.settlements.sites) {
            const double gap = radius * std::acos(clamp_unit(dot(p, direction(site.x, site.z, n))));
            check::is_true(gap >= s.world.effective.settlement_spacing, "a hut to keep clear of every city");
        }
        for (std::size_t j = 0; j < k; ++j) {
            const SocietyWitchHut& other = s.society.witch_huts[j];
            const double gap =
                radius * std::acos(clamp_unit(dot(p, direction(other.x, other.z, n))));
            check::is_true(gap >= 200.0, "a hut to keep clear of every other hut");
        }
    }
}

TEST(a_witch_hut_keeps_its_distance_from_every_city) {
    // The spacing against cities is the "isolation" half of the intent, and it is the
    // one hut rule that scales: settlement_spacing is multiplied by the reach factor,
    // unlike the bare 200 m hut-to-hut literal beside it. Give a world a spacing wide
    // enough to cover the ground the huts wanted and none of them can be sited.
    Request r;
    r.seed = 42;
    r.size = 65;
    r.phase = 11;
    // An explicit spacing is taken as authored rather than scaled, so this is 20 km on a
    // world only 200 km round: each city's exclusion cap covers about a tenth of the
    // sphere, and between them they cover every cell the huts wanted.
    r.settlement_spacing = 20000.0;
    const World crowded = generate(resolve(r));
    check::is_true(crowded.effective.settlement_spacing >= 20000.0, "the spacing to actually be wide");
    check::is_true(!crowded.settlements.sites.empty(), "cities to keep clear of");

    HumansRequest hr;
    hr.world = &crowded;
    const HumanHinterland humans = add_humans(hr);
    SeasonalFoodRequest sr;
    sr.world = &crowded;
    sr.humans = &humans;
    const SeasonalFood seasonal = add_seasonal_food(sr);
    SocietyRequest cr;
    cr.world = &crowded;
    cr.humans = &humans;
    cr.seasonal = &seasonal;
    const SocietyReport report = add_world_society(cr);

    std::size_t wanted = 0;
    for (const Landmark& mark : crowded.ecology.landmarks) {
        if (mark.kind == "witch_hut") ++wanted;
    }
    check::is_true(wanted > 0, "huts to have been asked for, or this proves nothing");
    check::is_true(report.witch_huts.empty(), "no hut to be sited within reach of a city");

    // The same seed at the shipped spacing does site them, so the emptiness above is the
    // spacing rule and not some other refusal.
    check::is_true(!stage12().society.witch_huts.empty(), "the shipped spacing to leave room for huts");
}

TEST(a_witch_hut_refuses_prime_settlement_ground) {
    // The suitability ceiling is the whole of "isolation and poor conventional
    // suitability". Hand it a world where every cell is prime and no hut can be sited;
    // hand it one where none is, and every hut that was there comes back.
    Grid prime(65, 1.0);
    const Stage12 rejected = stage12(65, 42, &prime);
    check::is_true(rejected.society.witch_huts.empty(), "no hut to sit on prime settlement ground");

    Grid poor(65, 0.0);
    const Stage12 accepted = stage12(65, 42, &poor);
    check::is_true(!accepted.society.witch_huts.empty(), "and the same world to site them on poor ground");
}

// --- the world economy -----------------------------------------------------

TEST(the_world_economy_folds_the_fish_in_and_reports_only_the_fourth_year) {
    const Stage12 s = stage12();
    const WorldEconomy& economy = s.society.world_economy;
    check::equal(economy.months.size(), std::size_t{48}, "four years simulated");
    check::equal(economy.models.size(), s.seasonal.models.size() + s.society.sky.settlements.size(),
                 "one model per community");
    check::equal(economy.sites.size(), economy.models.size(), "one summary per model");

    // The copied models carry the farm harvest plus the fish; the seasonal block itself
    // is never touched.
    std::vector<double> fish(s.seasonal.models.size(), 0.0);
    for (const HarbourPort& port : s.society.fisheries.ports) {
        if (static_cast<std::size_t>(port.core_id) < fish.size()) fish[static_cast<std::size_t>(port.core_id)] += port.delivered_food;
    }
    std::size_t raised = 0;
    for (std::size_t i = 0; i < s.seasonal.models.size(); ++i) {
        double before = 0.0;
        double after = 0.0;
        for (std::size_t m = 0; m < 12; ++m) {
            before += s.seasonal.models[i].harvest[m];
            after += economy.models[i].harvest[m];
        }
        check::close(after, before + fish[i], std::max(after, 1.0) * 1e-9,
                     "a city's year to be its farms plus its fisheries");
        if (fish[i] > 0.0) ++raised;
    }
    check::is_true(raised > 0, "some city to have been fed by the sea, or this proves nothing");

    for (const EconomySite& row : economy.sites) {
        check::between(row.food_coverage, 0.0, 1.0, "coverage inside zero and one");
        check::is_true(row.annual_shortage >= 0.0, "shortage never negative");
        check::is_true(row.lean_months.size() <= 12, "no more lean months than a year has");
        for (const int month : row.lean_months) check::between(month, 1.0, 12.0, "a lean month inside a year");
        check::equal(row.layer, std::string("surface"), "every surviving site to be on the surface");
    }
    // The fourth year is what is summarised: the totals must come from the last twelve
    // months, not from all forty-eight.
    double last_year_demand = 0.0;
    for (std::size_t m = 36; m < 48; ++m) last_year_demand += economy.months[m].cities[0].demand;
    check::close(economy.sites[0].annual_demand, last_year_demand, 1e-9,
                 "the summary to read the fourth year only");
}

TEST(the_seasonal_block_the_pass_was_handed_is_never_modified) {
    // The pass deep-copies the models before folding fish into them. If it did not, a
    // second call would fold the same fish in twice.
    const Stage12 s = stage12();
    check::is_true(!s.seasonal.models.empty(), "models, or this proves nothing");

    SocietyRequest request;
    request.world = &s.world;
    request.humans = &s.humans;
    request.seasonal = &s.seasonal;
    const SocietyReport again = add_world_society(request);

    check::equal(again.world_economy.models.size(), s.society.world_economy.models.size(),
                 "a second run to produce the same shape");
    for (std::size_t i = 0; i < again.world_economy.models.size(); ++i) {
        for (std::size_t m = 0; m < 12; ++m) {
            check::close(again.world_economy.models[i].harvest[m],
                         s.society.world_economy.models[i].harvest[m], 1e-9,
                         "and exactly the same harvest, not a doubled one");
        }
    }
    check::close(again.fisheries.delivered_annual_food, s.society.fisheries.delivered_annual_food, 1e-9,
                 "and the same catch");
}
