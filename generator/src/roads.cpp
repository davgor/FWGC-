#include "fwg/roads.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <tuple>

#include "fwg/routing.hpp"
#include "fwg/settlements_stage.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

std::vector<double> nodes_of(const World& world, const SphereGrid& grid, const std::string& layer) {
    std::vector<double> out(grid.node_count(), 0.0);
    const auto it = world.layers.find(layer);
    if (it == world.layers.end()) return out;
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = it->second.at(grid.point_x[i], grid.point_z[i]);
    return out;
}

// One candidate route, before the spanning forest decides whether it is built.
struct Candidate {
    double cost = 0.0;
    int from = 0;
    int to = 0;
    std::vector<int> path;
};

// Union-find over settlement indices.
int root_of(std::vector<int>& group, int i) {
    while (group[static_cast<std::size_t>(i)] != i) i = group[static_cast<std::size_t>(i)];
    return i;
}

}  // namespace

void add_roads(World& world) {
    if (!world.has_settlements) return;
    const std::vector<SettlementSite>& sites = world.settlements.sites;
    if (sites.size() < 2) {
        world.roads.components = static_cast<int>(sites.size());
        world.has_roads = true;
        world.systems.push_back("roads");
        return;
    }

    const Config& cfg = world.effective;
    const auto held = sphere_grid(cfg.size, cfg.globe_radius);
    const SphereGrid& grid = *held;

    const std::vector<double> water = nodes_of(world, grid, "water_type");
    const std::vector<double> height = nodes_of(world, grid, "height");
    const std::vector<double> river = nodes_of(world, grid, "rain_river");
    const std::vector<double> hazard = nodes_of(world, grid, "magic_hazard");

    const FreshwaterAccess fresh = freshwater_access(world, grid);
    const std::vector<double> flood = flood_risk(world, grid, fresh);

    // Each people builds to its OWN tolerances: how steep a road it will cut and how
    // much magical hazard it will walk through. A road is not a neutral line on the
    // map, and two peoples do not agree about where one can go.
    std::map<std::string, std::vector<double>> risk_by_people;
    const auto risk_for = [&](const std::string& people) -> const std::vector<double>& {
        const auto cached = risk_by_people.find(people);
        if (cached != risk_by_people.end()) return cached->second;
        const std::string layer = "magic_risk_" + people;
        std::vector<double> field =
            world.layers.count(layer) != 0 ? nodes_of(world, grid, layer) : hazard;
        return risk_by_people.emplace(people, std::move(field)).first->second;
    };
    const auto config_for = [&](const std::string& people) {
        Config theirs = cfg;
        const CivilizationProfile* profile = civilization_profile(people);
        if (profile != nullptr) {
            theirs.road_max_grade = profile->traits.road_grade_limit;
            theirs.human_magic_limit = profile->traits.mutation_limit;
        }
        return theirs;
    };

    std::vector<int> targets;
    targets.reserve(sites.size());
    for (const SettlementSite& s : sites) targets.push_back(s.node);

    std::vector<Candidate> candidates;
    for (std::size_t a = 0; a < sites.size(); ++a) {
        const Config mine = config_for(sites[a].population_profile);
        const RoadCost cost(grid, mine, water, height, flood, river,
                            &risk_for(sites[a].population_profile));
        const PathResult routes = shortest_paths(grid, sites[a].node, cost, &targets);

        for (std::size_t b = a + 1; b < sites.size(); ++b) {
            const int goal = sites[b].node;
            const double reach = routes.distance[static_cast<std::size_t>(goal)];
            if (!std::isfinite(reach)) continue;

            std::vector<int> path;
            for (int i = goal; i != sites[a].node; i = routes.parent[static_cast<std::size_t>(i)]) {
                if (routes.parent[static_cast<std::size_t>(i)] < 0) {
                    path.clear();
                    break;
                }
                path.push_back(i);
            }
            if (path.empty()) continue;
            path.push_back(sites[a].node);
            std::reverse(path.begin(), path.end());

            // A road belongs to BOTH ends. This route was found under the first
            // people's tolerances; if the second cannot walk it, it is not a road
            // between them, it is a road one of them could use alone.
            const Config theirs = config_for(sites[b].population_profile);
            const RoadCost other(grid, theirs, water, height, flood, river,
                                 &risk_for(sites[b].population_profile));
            bool passable = true;
            for (std::size_t k = 0; k + 1 < path.size() && passable; ++k) {
                double edge_length = 0.0;
                for (const SphereNeighbour& nb : grid.neighbours[static_cast<std::size_t>(path[k])]) {
                    if (nb.index == path[k + 1]) edge_length = nb.distance;
                }
                if (!other(path[k], path[k + 1], edge_length)) passable = false;
            }
            if (!passable) continue;

            candidates.push_back({reach, static_cast<int>(a), static_cast<int>(b), std::move(path)});
        }
    }

    // Cheapest first, ties broken on the settlement indices so the forest does not
    // depend on the order the pairs happened to be generated in.
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& x, const Candidate& y) {
        return std::tie(x.cost, x.from, x.to) < std::tie(y.cost, y.from, y.to);
    });

    std::vector<int> group(sites.size());
    for (std::size_t i = 0; i < group.size(); ++i) group[i] = static_cast<int>(i);

    RoadNetwork network;
    for (Candidate& c : candidates) {
        const int ra = root_of(group, c.from);
        const int rb = root_of(group, c.to);
        if (ra == rb) continue;  // already joined; a second road between them adds nothing
        group[static_cast<std::size_t>(ra)] = rb;

        Road road;
        road.from = c.from;
        road.to = c.to;
        road.cost = c.cost;
        for (std::size_t k = 0; k + 1 < c.path.size(); ++k) {
            for (const SphereNeighbour& nb : grid.neighbours[static_cast<std::size_t>(c.path[k])]) {
                if (nb.index == c.path[k + 1]) road.length_m += nb.distance;
            }
            // A crossing is recorded when EITHER end of the step is a river cell: a road
            // stepping off a river bank is crossing it just as much as one stepping on.
            if (river[static_cast<std::size_t>(c.path[k])] != 0.0 ||
                river[static_cast<std::size_t>(c.path[k + 1])] != 0.0) {
                road.river_crossings.emplace_back(c.path[k], c.path[k + 1]);
            }
        }
        road.nodes = std::move(c.path);
        network.routes.push_back(std::move(road));
    }

    std::set<int> roots;
    for (std::size_t i = 0; i < group.size(); ++i) roots.insert(root_of(group, static_cast<int>(i)));
    network.components = static_cast<int>(roots.size());

    world.roads = std::move(network);
    world.has_roads = true;
    world.systems.push_back("roads");
}

}  // namespace fwg
