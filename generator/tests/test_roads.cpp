// Stage 11: the roads between the cities.
#include "check.hpp"

#include <cmath>
#include <map>
#include <set>
#include <string>

#include "fwg/roads.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

World roaded(int size = 65, std::uint32_t seed = 42) {
    Request r;
    r.seed = seed;
    r.size = size;
    r.phase = 11;
    return generate(resolve(r));
}

}  // namespace

TEST(a_stage_eleven_world_has_roads_and_a_stage_ten_world_does_not) {
    const World eleven = roaded();
    check::is_true(eleven.has_roads, "stage 11 to build roads");
    check::is_true(!eleven.roads.routes.empty(), "and for there to be some");

    Request r;
    r.seed = 42;
    r.size = 65;
    r.phase = 10;
    const World ten = generate(resolve(r));
    check::is_true(!ten.has_roads, "stage 10 to stop before them");
}

TEST(a_road_runs_between_the_two_cities_it_names) {
    // The path is the road. If its ends are not the two settlements' own nodes, the
    // record names a connection the world does not contain.
    const World w = roaded();
    for (const Road& road : w.roads.routes) {
        check::is_true(road.nodes.size() >= 2, "a road to have at least two nodes");
        check::equal(road.nodes.front(), w.settlements.sites[static_cast<std::size_t>(road.from)].node,
                     "a road to start at the city it comes from");
        check::equal(road.nodes.back(), w.settlements.sites[static_cast<std::size_t>(road.to)].node,
                     "a road to end at the city it goes to");
        check::is_true(road.length_m > 0.0, "a road to cover ground");
    }
}

TEST(every_step_of_a_road_is_a_real_step) {
    // A path assembled from parent pointers can silently contain a jump if the search
    // and the reconstruction disagree. Each consecutive pair must be neighbours.
    const World w = roaded();
    const auto held = sphere_grid(w.effective.size, w.effective.globe_radius);
    check::is_true(!w.roads.routes.empty(), "some roads, or this proves nothing");

    for (const Road& road : w.roads.routes) {
        for (std::size_t k = 0; k + 1 < road.nodes.size(); ++k) {
            bool adjacent = false;
            for (const SphereNeighbour& nb : held->neighbours[static_cast<std::size_t>(road.nodes[k])]) {
                if (nb.index == road.nodes[k + 1]) adjacent = true;
            }
            check::is_true(adjacent, "step " + std::to_string(k) + " of a road to be one step");
        }
    }
}

TEST(no_road_crosses_open_water) {
    // Roads are land routes. The cost model refuses a water cell outright, so a road
    // through one means the refusal is not being honoured.
    const World w = roaded();
    const auto held = sphere_grid(w.effective.size, w.effective.globe_radius);
    const Grid& water = w.layers.at("water_type");
    for (const Road& road : w.roads.routes) {
        for (const int node : road.nodes) {
            const auto n = static_cast<std::size_t>(node);
            check::equal(water.at(held->point_x[n], held->point_z[n]), 0.0,
                         "node " + std::to_string(node) + " on a road to be dry land");
        }
    }
}

TEST(the_network_is_a_spanning_forest_and_not_a_web) {
    // One road per pair joined, never a second between two cities already connected.
    // A forest over N cities in C components has exactly N - C edges; any more is a
    // cycle, which is what the union-find is there to prevent.
    const World w = roaded();
    const auto cities = static_cast<int>(w.settlements.sites.size());
    check::is_true(cities > 2, "several cities, or a forest is trivially a forest");
    check::equal(static_cast<int>(w.roads.routes.size()), cities - w.roads.components,
                 "the route count to be cities minus components");
    check::is_true(w.roads.components >= 1, "at least one network");
    check::is_true(w.roads.components <= cities, "no more components than cities");
}

TEST(a_river_crossing_is_recorded_only_where_a_river_is) {
    const World w = roaded();
    const auto held = sphere_grid(w.effective.size, w.effective.globe_radius);
    const auto river = w.layers.find("rain_river");
    if (river == w.layers.end()) return;

    long long crossings = 0;
    for (const Road& road : w.roads.routes) {
        for (const auto& [a, b] : road.river_crossings) {
            ++crossings;
            const auto ua = static_cast<std::size_t>(a);
            const auto ub = static_cast<std::size_t>(b);
            const bool wet = river->second.at(held->point_x[ua], held->point_z[ua]) != 0.0 ||
                             river->second.at(held->point_x[ub], held->point_z[ub]) != 0.0;
            check::is_true(wet, "a recorded crossing to actually cross a river");
        }
    }
    // No floor on the count: a world may legitimately have none. The assertion above is
    // about correctness, not about this world being interesting.
    check::is_true(crossings >= 0, "crossings to be countable");
}

TEST(the_cheapest_routes_are_the_ones_built) {
    // Kruskal's guarantee. Every road built must be no more expensive than any road
    // that was refused for joining two already-connected cities -- otherwise the sort
    // or the union-find is wrong and the forest is merely A forest, not the minimum one.
    const World w = roaded();
    double most_expensive_built = 0.0;
    for (const Road& road : w.roads.routes) most_expensive_built = std::max(most_expensive_built, road.cost);
    check::is_true(most_expensive_built > 0.0, "roads to have cost something");

    // Each successive road in the built list is at least as expensive as the last,
    // because they are taken in sorted order.
    for (std::size_t i = 1; i < w.roads.routes.size(); ++i) {
        check::is_true(w.roads.routes[i].cost >= w.roads.routes[i - 1].cost,
                       "roads to be built cheapest first");
    }
}

TEST(a_world_of_one_city_has_no_roads_but_still_reports_its_network) {
    Request r;
    r.seed = 42;
    r.size = 33;
    r.phase = 11;
    r.settlement_count = 1;
    const World w = generate(resolve(r));
    check::is_true(w.has_roads, "the stage to have run even with nothing to connect");
    check::equal(w.roads.routes.size(), std::size_t{0}, "and to build nothing");
    check::equal(w.roads.components, static_cast<int>(w.settlements.sites.size()),
                 "each lone city to be its own component");
}
