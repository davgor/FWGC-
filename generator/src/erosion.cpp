#include "fwg/erosion.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace fwg {
namespace {

struct Receiver {
    double slope = 0.0;
    int node = -1;      // -1 means a sink: a pit, or a cell at or below sea level
    double distance = 1.0;
};

struct Routing {
    std::vector<Receiver> receivers;
    std::vector<int> order;       // descending height, ties by ascending index
    std::vector<double> discharge;
};

Routing route(const std::vector<double>& h, const SphereGrid& grid, double sea_level) {
    const std::size_t count = h.size();
    Routing routing;
    routing.receivers.resize(count);

    for (std::size_t i = 0; i < count; ++i) {
        if (h[i] <= sea_level) continue;  // submerged cells do not cut

        Receiver best;
        bool found = false;
        for (const SphereNeighbour& neighbour : grid.neighbours[i]) {
            const auto j = static_cast<std::size_t>(neighbour.index);
            if (h[j] >= h[i] || neighbour.distance <= 0.0) continue;
            const double slope = (h[i] - h[j]) / neighbour.distance;
            // Ties break on the higher node index, which is arbitrary but fixed --
            // the important part is that it does not depend on iteration order.
            if (!found || slope > best.slope || (slope == best.slope && neighbour.index > best.node)) {
                best = {slope, neighbour.index, neighbour.distance};
                found = true;
            }
        }
        if (found) routing.receivers[i] = best;
    }

    routing.order.resize(count);
    std::iota(routing.order.begin(), routing.order.end(), 0);
    std::sort(routing.order.begin(), routing.order.end(), [&h](int a, int b) {
        const double ha = h[static_cast<std::size_t>(a)];
        const double hb = h[static_cast<std::size_t>(b)];
        if (ha != hb) return ha > hb;
        return a < b;
    });

    routing.discharge = grid.areas;
    for (const int i : routing.order) {
        const int j = routing.receivers[static_cast<std::size_t>(i)].node;
        if (j >= 0) routing.discharge[static_cast<std::size_t>(j)] += routing.discharge[static_cast<std::size_t>(i)];
    }
    return routing;
}

}  // namespace

ErosionResult erode(const Grid& height, double radius, double sea_level, int passes, double strength) {
    const int n = height.size();
    const std::shared_ptr<const SphereGrid> held = sphere_grid(n, radius);
    const SphereGrid& grid = *held;
    const std::size_t count = grid.node_count();

    std::vector<double> h(count);
    for (std::size_t i = 0; i < count; ++i) h[i] = height.at(grid.point_x[i], grid.point_z[i]);
    const std::vector<double> original = h;
    const double ceiling = *std::max_element(h.begin(), h.end());

    std::vector<double> sediment(count, 0.0);
    std::vector<double> cut(count, 0.0);
    std::vector<double> fill(count, 0.0);
    double total_cut = 0.0;
    double total_fill = 0.0;

    Routing routing;
    for (int pass = 0; pass < passes; ++pass) {
        routing = route(h, grid, sea_level);
        std::vector<double> delta(count, 0.0);
        std::vector<double> retained(count, 0.0);

        for (const int index : routing.order) {
            const auto i = static_cast<std::size_t>(index);
            const Receiver& receiver = routing.receivers[i];
            const int j = receiver.node;
            const double area = grid.areas[i];

            // Stream-power proxy, limited to a fraction of the downstream drop so a
            // single pass cannot cut below the cell it drains into.
            double depth = 0.0;
            if (j >= 0) {
                depth = std::min(0.15 * (h[i] - h[static_cast<std::size_t>(j)]),
                                 strength * 0.04 * std::sqrt(routing.discharge[i]) * receiver.slope);
            }

            const double capacity = 2.0 * depth * area;
            const double incoming = sediment[i];
            const double removed = std::min(depth * area, std::max(0.0, capacity - incoming));
            const double load = incoming + removed;
            // Deposition cannot build a cell above the original surface's high point.
            const double deposited = std::min(std::max(0.0, load - capacity),
                                              std::max(0.0, ceiling - h[i]) * 0.25 * area);

            delta[i] += (deposited - removed) / area;
            cut[i] += removed / area;
            fill[i] += deposited / area;
            total_cut += removed;
            total_fill += deposited;

            if (j >= 0) {
                sediment[static_cast<std::size_t>(j)] += load - deposited;
            } else {
                retained[i] = load - deposited;
            }
        }

        for (std::size_t i = 0; i < count; ++i) h[i] += delta[i];
        sediment = retained;
    }

    // Diagnostics describe the FINAL surface, not the last iteration's routing.
    routing = route(h, grid, sea_level);

    std::vector<double> net(count);
    for (std::size_t i = 0; i < count; ++i) net[i] = h[i] - original[i];
    const double stored = std::accumulate(sediment.begin(), sediment.end(), 0.0);

    ErosionResult out;
    out.height = grid.to_grid(h);
    out.erosion = grid.to_grid(cut);
    out.deposition = grid.to_grid(fill);
    out.erosion_delta = grid.to_grid(net);
    out.catchment = grid.to_grid(routing.discharge);
    out.eroded_m3 = total_cut;
    out.deposited_m3 = total_fill;
    out.stored_sediment_m3 = stored;
    out.balance_error_m3 = total_cut - total_fill - stored;
    return out;
}

}  // namespace fwg
