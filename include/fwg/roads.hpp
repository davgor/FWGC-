// Stage 11, "Roads": what the cities build between themselves.
//
// A minimum spanning forest over the cheapest terrain-cost route between every pair of
// settlements. A forest rather than a tree because a world is not connected: islands
// stay islands, and the count of components is how many separate road networks a world
// ended up with.
//
// A road is not an engineered thing here. River crossings are recorded as bridge
// CANDIDATES rather than bridges, and the cost model prices a crossing without building
// anything.
#pragma once

#include <vector>

namespace fwg {

struct World;

// One built route between two settlements.
struct Road {
    int from = 0;  // index into world.settlements.sites
    int to = 0;
    std::vector<int> nodes;  // the path, from the `from` site's node to the `to` site's
    double length_m = 0.0;   // real distance over the ground
    double cost = 0.0;       // what the cost model charged, which is not a distance
    // Edges where a river is crossed, as node pairs. Bridge candidates, not bridges.
    std::vector<std::pair<int, int>> river_crossings;
};

struct RoadNetwork {
    std::vector<Road> routes;
    // How many separate networks this world has. More than one means islands, or
    // terrain no road could cross.
    int components = 0;
};

// Runs stage 11. Needs stage 10: without settlements there is nothing to connect.
void add_roads(World& world);

}  // namespace fwg
