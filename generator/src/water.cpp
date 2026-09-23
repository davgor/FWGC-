#include "fwg/water.hpp"

#include <algorithm>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace fwg {
namespace {

// Min-heap keyed on (level, node). The node index is part of the key, not a
// tiebreak of convenience: two nodes at exactly the same spill level are common on
// a flat ocean floor, and without a total order the settle order -- and therefore
// every receiver assigned from it -- would depend on the heap's internals.
using Entry = std::pair<double, int>;
using MinHeap = std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>>;

}  // namespace

WaterRouting route_water(const std::vector<double>& h, const SphereGrid& grid, double sea_level) {
    const std::size_t count = h.size();

    WaterRouting out;
    out.ocean.assign(count, 0);
    out.level = h;
    out.parent.assign(count, -1);
    out.order.reserve(count);

    // --- 1. the ocean is the largest connected below-sea component ----------
    // Connectivity, not elevation: an inland sea below the datum is a lake, and the
    // only thing that distinguishes it from the ocean is that it does not join up.
    std::vector<char> assigned(count, 0);
    std::vector<int> component;
    std::vector<int> stack;
    double best_area = -1.0;
    std::vector<int> ocean_ids;

    for (std::size_t start = 0; start < count; ++start) {
        if (assigned[start] || h[start] > sea_level) continue;
        component.clear();
        stack.assign(1, static_cast<int>(start));
        assigned[start] = 1;
        double area = 0.0;
        while (!stack.empty()) {
            const int i = stack.back();
            stack.pop_back();
            component.push_back(i);
            area += grid.areas[static_cast<std::size_t>(i)];
            for (const SphereNeighbour& nb : grid.neighbours[static_cast<std::size_t>(i)]) {
                const auto j = static_cast<std::size_t>(nb.index);
                if (assigned[j] || h[j] > sea_level) continue;
                assigned[j] = 1;
                stack.push_back(nb.index);
            }
        }
        // Strictly greater, so the first component wins a tie and the choice does
        // not depend on scan order beyond the deterministic index order.
        if (area > best_area) {
            best_area = area;
            ocean_ids = component;
        }
    }

    for (const int i : ocean_ids) out.ocean[static_cast<std::size_t>(i)] = 1;

    // An oceanless world still has to drain somewhere: the global minimum becomes the
    // single outlet. Lowest cell, lowest index on a tie.
    std::vector<int> roots = ocean_ids;
    if (roots.empty()) {
        std::size_t lowest = 0;
        for (std::size_t i = 1; i < count; ++i) {
            if (h[i] < h[lowest]) lowest = i;
        }
        roots.push_back(static_cast<int>(lowest));
    }

    // --- 2. priority flood -------------------------------------------------
    // Settling nodes in increasing spill level fills every depression to the lowest
    // rim it can escape over. A node's level is the higher of its own ground and the
    // level of whatever it was reached from -- that max is the whole algorithm.
    std::vector<char> visited(count, 0);
    MinHeap queue;
    for (const int i : roots) {
        const auto r = static_cast<std::size_t>(i);
        out.level[r] = out.ocean[r] ? sea_level : h[r];
        visited[r] = 1;
        queue.emplace(out.level[r], i);
    }

    while (!queue.empty()) {
        const auto [elevation, i] = queue.top();
        queue.pop();
        out.order.push_back(i);
        for (const SphereNeighbour& nb : grid.neighbours[static_cast<std::size_t>(i)]) {
            const auto j = static_cast<std::size_t>(nb.index);
            if (visited[j]) continue;
            visited[j] = 1;
            out.parent[j] = i;
            out.level[j] = std::max(h[j], elevation);
            queue.emplace(out.level[j], nb.index);
        }
    }

    // --- 3. re-point receivers to steepest descent -------------------------
    // The flood parent is whichever neighbour happened to reach a node first, which
    // routes flats without cycles but is not the direction water would run. Where a
    // strictly lower already-settled neighbour exists, prefer the steepest one. The
    // rank guard is what keeps the receiver graph acyclic: a node may only drain to
    // one settled before it.
    std::vector<int> rank(count, 0);
    for (std::size_t k = 0; k < out.order.size(); ++k) {
        rank[static_cast<std::size_t>(out.order[k])] = static_cast<int>(k);
    }
    const std::vector<char> is_root = [&] {
        std::vector<char> flags(count, 0);
        for (const int i : roots) flags[static_cast<std::size_t>(i)] = 1;
        return flags;
    }();

    for (const int index : out.order) {
        const auto i = static_cast<std::size_t>(index);
        if (is_root[i]) continue;
        double best_slope = 0.0;
        int best_node = -1;
        for (const SphereNeighbour& nb : grid.neighbours[i]) {
            const auto j = static_cast<std::size_t>(nb.index);
            if (nb.distance <= 0.0 || out.level[j] >= out.level[i] || rank[j] >= rank[i]) continue;
            const double slope = (out.level[i] - out.level[j]) / nb.distance;
            // Ties take the higher node index. Arbitrary, fixed, and independent of
            // the order the neighbour list happens to be in.
            if (best_node < 0 || slope > best_slope || (slope == best_slope && nb.index > best_node)) {
                best_slope = slope;
                best_node = nb.index;
            }
        }
        if (best_node >= 0) out.parent[i] = best_node;
    }

    // --- 4. accumulate catchment ------------------------------------------
    // Uniform unit runoff: every cell contributes its own area and nothing else.
    // This is not a rainfall volume, and the climate stage does not revise it.
    out.flow = grid.areas;
    for (auto it = out.order.rbegin(); it != out.order.rend(); ++it) {
        const auto i = static_cast<std::size_t>(*it);
        const int j = out.parent[i];
        if (j >= 0) out.flow[static_cast<std::size_t>(j)] += out.flow[i];
    }

    return out;
}

}  // namespace fwg
