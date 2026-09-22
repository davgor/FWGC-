// Hydrology invariants.
//
// The receiver graph produced here is what rivers, settlement siting, sea routes and
// every later stage's notion of "downhill" are built on. A defect in it does not look
// like a defect — it looks like a world whose cities are in odd places.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "fwg/water.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

World build(int size = 65, double sea_level = 0.0) {
    Request request;
    request.seed = 42;
    request.size = size;
    request.phase = 5;
    request.sea_level = sea_level;
    return generate(resolve(request));
}

std::vector<double> node_heights(const World& world, const SphereGrid& grid) {
    const Grid& h = world.layers.at("height");
    std::vector<double> out(grid.node_count());
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = h.at(grid.point_x[i], grid.point_z[i]);
    return out;
}

}  // namespace

TEST(water_partitions_the_sphere_exactly) {
    const World world = build();
    check::is_true(world.has_water, "a phase-5 world to route its water");
    const WaterReport& w = world.water;
    const double sum = w.ocean_km2 + w.lake_km2 + w.dry_km2;
    check::close(sum, world.area.total_km2, world.area.total_km2 * 1e-9,
                 "ocean + lake + dry to be the whole sphere");
    check::is_true(w.ocean_km2 > 0.0 && w.dry_km2 > 0.0,
                   "the reference world to have both sea and land");
}

TEST(the_water_surface_never_falls_below_the_ground) {
    const World world = build();
    const Grid& surface = world.layers.at("water_surface");
    const Grid& height = world.layers.at("height");
    const Grid& depth = world.layers.at("water_depth");
    for (int z = 0; z < height.size(); ++z) {
        for (int x = 0; x < height.size(); ++x) {
            check::is_true(surface.at(x, z) >= height.at(x, z) - 1e-9,
                           "water surface at or above ground");
            check::close(depth.at(x, z), std::max(0.0, surface.at(x, z) - height.at(x, z)), 1e-9,
                         "depth to be surface minus ground");
        }
    }
}

TEST(water_never_flows_uphill) {
    const World world = build();
    const auto held = sphere_grid(world.config.size, world.effective.globe_radius);
    const SphereGrid& grid = *held;
    const WaterRouting routed = route_water(node_heights(world, grid), grid, world.effective.sea_level);

    check::equal(routed.order.size(), grid.node_count(), "every node to be settled");
    int drained = 0;
    for (std::size_t i = 0; i < grid.node_count(); ++i) {
        const int j = routed.parent[i];
        if (j < 0) continue;
        ++drained;
        check::is_true(routed.level[static_cast<std::size_t>(j)] <= routed.level[i] + 1e-9,
                       "node " + std::to_string(i) + " not to drain uphill");
    }
    check::is_true(drained > 0, "some node to drain somewhere");
}

TEST(the_receiver_graph_reaches_an_outlet_from_everywhere) {
    // The property that makes catchment accumulation terminate, and the one a
    // depression-filling bug destroys: follow the receivers from any node and you
    // must arrive at an outlet rather than going round forever.
    const World world = build();
    const auto held = sphere_grid(world.config.size, world.effective.globe_radius);
    const SphereGrid& grid = *held;
    const WaterRouting routed = route_water(node_heights(world, grid), grid, world.effective.sea_level);

    const std::size_t count = grid.node_count();
    for (std::size_t start = 0; start < count; ++start) {
        std::size_t steps = 0;
        int at = static_cast<int>(start);
        while (at >= 0) {
            at = routed.parent[static_cast<std::size_t>(at)];
            if (++steps > count) break;
        }
        check::is_true(steps <= count, "node " + std::to_string(start) + " to reach an outlet");
    }
}

TEST(catchment_accumulates_downstream_and_totals_the_sphere) {
    const World world = build();
    const auto held = sphere_grid(world.config.size, world.effective.globe_radius);
    const SphereGrid& grid = *held;
    const WaterRouting routed = route_water(node_heights(world, grid), grid, world.effective.sea_level);

    for (std::size_t i = 0; i < grid.node_count(); ++i) {
        check::is_true(routed.flow[i] >= grid.areas[i] * (1.0 - 1e-9),
                       "every cell to carry at least its own area");
    }
    // Every node's area reaches exactly one outlet, so the outlets together carry the
    // whole sphere. That is a real conservation check on the accumulation.
    double at_outlets = 0.0;
    double total = 0.0;
    for (std::size_t i = 0; i < grid.node_count(); ++i) {
        total += grid.areas[i];
        if (routed.parent[i] < 0) at_outlets += routed.flow[i];
    }
    check::close(at_outlets / total, 1.0, 1e-9, "outlets to carry the whole sphere");
}

TEST(the_ocean_is_one_connected_body) {
    const World world = build();
    const auto held = sphere_grid(world.config.size, world.effective.globe_radius);
    const SphereGrid& grid = *held;
    const WaterRouting routed = route_water(node_heights(world, grid), grid, world.effective.sea_level);

    std::size_t first = grid.node_count();
    std::size_t ocean_count = 0;
    for (std::size_t i = 0; i < grid.node_count(); ++i) {
        if (routed.ocean[i]) {
            if (first == grid.node_count()) first = i;
            ++ocean_count;
        }
    }
    check::is_true(ocean_count > 0, "the reference world to have an ocean");

    // Flood the ocean from one cell and it must cover all of it — that is what makes
    // it the ocean rather than a set of seas.
    std::vector<char> seen(grid.node_count(), 0);
    std::vector<std::size_t> stack{first};
    seen[first] = 1;
    std::size_t reached = 0;
    while (!stack.empty()) {
        const std::size_t i = stack.back();
        stack.pop_back();
        ++reached;
        for (const SphereNeighbour& nb : grid.neighbours[i]) {
            const auto j = static_cast<std::size_t>(nb.index);
            if (seen[j] || !routed.ocean[j]) continue;
            seen[j] = 1;
            stack.push_back(j);
        }
    }
    check::equal(reached, ocean_count, "every ocean cell to be reachable from every other");
}

TEST(a_lake_holds_water_and_the_dry_land_does_not) {
    const World world = build(129);
    const Grid& type = world.layers.at("water_type");
    const Grid& depth = world.layers.at("water_depth");
    int lakes = 0;
    for (int z = 0; z < type.size(); ++z) {
        for (int x = 0; x < type.size() - 1; ++x) {
            const int kind = static_cast<int>(type.at(x, z));
            if (kind == kLake) {
                ++lakes;
                check::is_true(depth.at(x, z) > kLakeDepthEpsilon, "a lake cell to have depth");
            } else if (kind == kDry) {
                check::is_true(depth.at(x, z) <= kLakeDepthEpsilon, "a dry cell to hold no water");
            }
        }
    }
    check::is_true(lakes > 0, "a size-129 reference world to have some lakes");
    check::is_true(world.water.lake_capacity_m3 > 0.0, "lakes to have a capacity");
}

// --- the metrics move with their knobs -----------------------------------

TEST(ocean_area_answers_to_sea_level) {
    const double base = build(65, 0.0).water.ocean_km2;
    const double risen = build(65, 400.0).water.ocean_km2;
    const double fallen = build(65, -400.0).water.ocean_km2;
    check::is_true(risen > base, "a higher datum to flood more of the world");
    check::is_true(fallen < base, "a lower datum to drain it");
}

TEST(river_count_answers_to_the_catchment_threshold) {
    Request coarse;
    coarse.seed = 42;
    coarse.size = 65;
    coarse.phase = 5;
    coarse.river_threshold_km2 = 500.0;
    Request fine = coarse;
    fine.river_threshold_km2 = 1.0;

    const std::size_t few = generate(resolve(coarse)).water.river_segments.size();
    const std::size_t many = generate(resolve(fine)).water.river_segments.size();
    check::is_true(many > few, "a lower threshold to raise more rivers (" + std::to_string(few) +
                                   " vs " + std::to_string(many) + ")");
    check::is_true(few > 0 || many > 0, "some threshold to produce a river");
}

TEST(a_world_with_no_ocean_still_routes) {
    // Drop the datum below the deepest basin and nothing is sea. The routing must
    // still terminate somewhere — the global minimum becomes the single outlet —
    // rather than dividing by an empty ocean or looping.
    const World world = build(33, -100000.0);
    check::is_true(!world.water.has_ocean, "no ocean below every basin");
    check::equal(world.water.ocean_km2, 0.0, "no ocean area");
    check::close(world.water.dry_km2 + world.water.lake_km2, world.area.total_km2,
                 world.area.total_km2 * 1e-9, "the sphere to still be accounted for");

    int outlets = 0;
    for (const int r : world.water.receivers) {
        if (r < 0) ++outlets;
    }
    check::equal(outlets, 1, "exactly one outlet in an oceanless world");
}
