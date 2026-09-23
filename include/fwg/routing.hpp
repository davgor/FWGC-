// Movement over the world: what is reachable, at what cost, and by whom.
//
// Shared by everything that has to cross ground rather than fly over it — roads,
// settlement support, sea routes, creature territory and the food model. Kept here
// because five modules independently needed the same three searches, and because the
// bound on a search is part of its meaning rather than an optimisation.
//
// **Bound the search, not the result.** A caller that discards everything past a reach
// was still paying for the whole component. Stopping at the reach is exact, not an
// approximation: edge costs are non-negative, so every prefix of a path costing at most
// `limit` also costs at most `limit`, and a node inside the bound keeps the distance and
// the parent it would have had unbounded. Nodes OUTSIDE it stay at infinity rather than
// taking their true value — so read a bounded result only inside the bound.
#pragma once

#include <algorithm>
#include <limits>
#include <optional>
#include <queue>
#include <utility>
#include <vector>

#include "fwg/config.hpp"
#include "fwg/grid.hpp"

namespace fwg {

inline constexpr double kUnreachable = std::numeric_limits<double>::infinity();

struct PathResult {
    std::vector<double> distance;
    std::vector<int> parent;
};

// Cheapest routes from one node. `cost(i, j, edge_length)` returns nullopt for an edge
// that cannot be crossed at all.
//
// `targets` stops the search once every listed node is settled; `limit` stops it at a
// reach. Both are exact for non-negative costs.
template <typename Cost>
PathResult shortest_paths(const SphereGrid& grid, int start, Cost&& cost,
                          const std::vector<int>* targets = nullptr, double limit = kUnreachable) {
    const std::size_t count = grid.node_count();
    PathResult out;
    out.distance.assign(count, kUnreachable);
    out.parent.assign(count, -1);
    if (start < 0 || static_cast<std::size_t>(start) >= count) return out;
    out.distance[static_cast<std::size_t>(start)] = 0.0;

    std::vector<char> wanted;
    std::size_t remaining = 0;
    if (targets) {
        wanted.assign(count, 0);
        for (const int t : *targets) {
            if (t >= 0 && static_cast<std::size_t>(t) < count && !wanted[static_cast<std::size_t>(t)]) {
                wanted[static_cast<std::size_t>(t)] = 1;
                ++remaining;
            }
        }
    }

    using Entry = std::pair<double, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
    queue.emplace(0.0, start);

    while (!queue.empty()) {
        const auto [value, i] = queue.top();
        queue.pop();
        const auto ui = static_cast<std::size_t>(i);
        if (value != out.distance[ui]) continue;  // stale
        if (targets && wanted[ui]) {
            wanted[ui] = 0;
            if (--remaining == 0) break;
        }
        for (const SphereNeighbour& nb : grid.neighbours[ui]) {
            const std::optional<double> edge = cost(i, nb.index, nb.distance);
            if (!edge) continue;
            const double candidate = value + *edge;
            const auto j = static_cast<std::size_t>(nb.index);
            if (candidate <= limit && candidate < out.distance[j]) {
                out.distance[j] = candidate;
                out.parent[j] = i;
                queue.emplace(candidate, nb.index);
            }
        }
    }
    return out;
}

struct AccessResult {
    std::vector<double> distance;
    std::vector<int> owner;   // the label of the seed that claimed this node, or -1
    std::vector<int> parent;
};

// One owner per node, by terrain cost, from many seeds at once.
//
// Ties go to the LOWER label, deterministically — without that rule two seeds equidistant
// from a cell would divide it by whichever the heap happened to pop first, and the map
// would change when an unrelated seed was added.
template <typename Cost>
AccessResult allocate_access(const SphereGrid& grid, const std::vector<std::pair<int, int>>& seeds,
                             Cost&& cost, double limit = kUnreachable) {
    const std::size_t count = grid.node_count();
    AccessResult out;
    out.distance.assign(count, kUnreachable);
    out.owner.assign(count, -1);
    out.parent.assign(count, -1);

    // (distance, label, node): the label is part of the key, so the tie rule is in the
    // ordering rather than in a comparison after the fact.
    using Entry = std::tuple<double, int, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
    for (const auto& [node, label] : seeds) {
        if (node < 0 || static_cast<std::size_t>(node) >= count) continue;
        const auto un = static_cast<std::size_t>(node);
        if (out.owner[un] < 0 || label < out.owner[un]) {
            out.distance[un] = 0.0;
            out.owner[un] = label;
        }
        queue.emplace(0.0, label, node);
    }

    while (!queue.empty()) {
        const auto [value, label, i] = queue.top();
        queue.pop();
        const auto ui = static_cast<std::size_t>(i);
        if (value != out.distance[ui] || label != out.owner[ui]) continue;  // stale
        for (const SphereNeighbour& nb : grid.neighbours[ui]) {
            const std::optional<double> edge = cost(i, nb.index, nb.distance);
            if (!edge) continue;
            const double candidate = value + *edge;
            if (candidate > limit) continue;
            const auto j = static_cast<std::size_t>(nb.index);
            const bool closer = candidate < out.distance[j];
            const bool tied_and_lower = candidate == out.distance[j] && label < out.owner[j];
            if (!closer && !tied_and_lower) continue;
            out.distance[j] = candidate;
            out.owner[j] = label;
            out.parent[j] = i;
            queue.emplace(candidate, label, nb.index);
        }
    }
    return out;
}

// The cost of moving a road between two adjacent cells, or nothing where it cannot go.
//
// Refuses water, refuses a grade steeper than the profile allows, and refuses a diagonal
// that would slip between two water-filled corners — a road cannot cut the corner of a
// lake just because both its own endpoints are dry. Grade is charged quadratically, so a
// gentle detour beats a short climb, and a river crossing costs a bridge.
class RoadCost {
public:
    RoadCost(const SphereGrid& grid, const Config& cfg, const std::vector<double>& water,
             const std::vector<double>& height, const std::vector<double>& flood,
             const std::vector<double>& river, const std::vector<double>* hazard = nullptr);

    std::optional<double> operator()(int i, int j, double distance) const;

private:
    bool blocked(std::size_t i) const;

    const SphereGrid& grid_;
    const std::vector<double>& water_;
    const std::vector<double>& height_;
    const std::vector<double>& flood_;
    const std::vector<double>& river_;
    const std::vector<double>* hazard_;
    double max_grade_;
    double bridge_cost_;
    double magic_limit_;
};

// Support has to follow passable terrain, not a circle drawn across water and cliffs.
template <typename Cost>
std::vector<char> reachable_support(const SphereGrid& grid, const std::vector<int>& starts, Cost&& cost,
                                    double reach) {
    std::vector<std::pair<int, int>> seeds;
    seeds.reserve(starts.size());
    for (std::size_t k = 0; k < starts.size(); ++k) seeds.emplace_back(starts[k], static_cast<int>(k));
    const AccessResult access = allocate_access(grid, seeds, std::forward<Cost>(cost), reach);

    std::vector<char> out(grid.node_count(), 0);
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = access.owner[i] >= 0 && access.distance[i] <= reach ? 1 : 0;
    }
    return out;
}

// --- capacity -------------------------------------------------------------
// All three are explicit provisional calibrations, not demography.

// Whole communities supported by quality-weighted habitat footprint. No minimum: a
// world too small for one city gets none rather than one bad one.
long long habitat_capacity(const std::vector<double>& areas, const std::vector<double>& scores,
                           const std::vector<int>& eligible, double land_per_city_km2);

// Cities, bounded BOTH by land and by the people available to fill them.
long long city_capacity(double weighted_habitat_km2, long long allowance, double land_per_city_km2,
                        double minimum_founding_residents);

struct LifeCapacity {
    long long total = 0;
    std::vector<long long> by_people;  // parallel to the potentials passed in
};

// A hundred residents per fully productive square kilometre.
//
// Overlapping claims DIVIDE a cell's best supported capacity between the claimants;
// they never multiply it. Fractional residents stay unallocated rather than rounding
// up into people who do not exist.
LifeCapacity life_capacity(const std::vector<double>& areas,
                           const std::vector<std::vector<double>>& potentials);

}  // namespace fwg
