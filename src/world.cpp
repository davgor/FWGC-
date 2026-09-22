#include "fwg/world.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>

namespace fwg {
namespace {

constexpr double kPi = std::numbers::pi;

using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point from) {
    return std::chrono::duration<double, std::milli>(Clock::now() - from).count();
}

// Layers that carry a length and therefore scale with the world.
constexpr const char* kLengthLayers[] = {
    "base", "height", "structure", "interaction", "noise", "tpi", "boundary_distance",
    "continental", "surface", "erosion", "deposition", "erosion_delta"};

void scale_layer(Grid& layer, double factor) {
    for (double& value : layer.cells()) value *= factor;
}

}  // namespace

AreaReport land_area(const Grid& height, double radius, double sea_level) {
    const int n = height.size();

    double land = 0.0;
    double max_sample = 0.0;
    for (int z = 0; z < n; ++z) {
        const double weight = band_wedge_area(z, n, radius);
        max_sample = std::max(max_sample, weight);
        // The last column duplicates the first, so it is excluded from the count.
        int above = 0;
        for (int x = 0; x < n - 1; ++x) {
            if (height.at(x, z) > sea_level) ++above;
        }
        land += weight * above;
    }

    const double total = 4.0 * kPi * radius * radius;
    AreaReport report;
    report.land_km2 = land / 1e6;
    report.submerged_km2 = (total - land) / 1e6;
    report.total_km2 = total / 1e6;
    report.land_fraction = land / total;
    report.largest_sample_km2 = max_sample / 1e6;
    return report;
}

void add_water(World& world) {
    const Config& cfg = world.effective;
    const Grid& height = world.layers.at("height");
    const int n = height.size();

    const auto held = sphere_grid(n, cfg.globe_radius);
    const SphereGrid& grid = *held;
    const std::size_t count = grid.node_count();

    std::vector<double> h(count);
    for (std::size_t i = 0; i < count; ++i) h[i] = height.at(grid.point_x[i], grid.point_z[i]);

    const WaterRouting routed = route_water(h, grid, cfg.sea_level);

    std::vector<double> depth(count);
    std::vector<double> kind(count);
    for (std::size_t i = 0; i < count; ++i) {
        depth[i] = std::max(0.0, routed.level[i] - h[i]);
        kind[i] = routed.ocean[i] ? kOcean : (depth[i] > kLakeDepthEpsilon ? kLake : kDry);
    }

    // A river is a dry cell with somewhere to drain to and enough upstream area to
    // carry a channel. The threshold is an AREA and was scaled with the world at
    // resolve() time; left absolute, every land cell qualifies and the layer is a
    // constant.
    const double threshold_m2 = cfg.river_threshold_km2 * 1e6;
    std::vector<double> river(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        const bool flows = static_cast<int>(kind[i]) == kDry && routed.parent[i] >= 0 &&
                           routed.flow[i] >= threshold_m2;
        river[i] = flows ? 1.0 : 0.0;
    }

    world.layers["water_type"] = grid.to_grid(kind);
    world.layers["water_depth"] = grid.to_grid(depth);
    world.layers["water_surface"] = grid.to_grid(routed.level);
    world.layers["routed_catchment"] = grid.to_grid(routed.flow);
    world.layers["river"] = grid.to_grid(river);

    WaterReport report;
    report.receivers = routed.parent;
    for (std::size_t i = 0; i < count; ++i) {
        if (river[i] != 0.0) {
            report.river_segments.push_back({static_cast<int>(i), routed.parent[i]});
        }
        const double area = grid.areas[i];
        switch (static_cast<int>(kind[i])) {
            case kOcean: report.ocean_km2 += area; break;
            case kLake:
                report.lake_km2 += area;
                report.lake_capacity_m3 += area * depth[i];
                break;
            default: report.dry_km2 += area; break;
        }
        if (routed.ocean[i]) report.has_ocean = true;
    }
    report.ocean_km2 /= 1e6;
    report.lake_km2 /= 1e6;
    report.dry_km2 /= 1e6;

    world.water = report;
    world.has_water = true;

    world.warnings.push_back(
        "Largest connected below-sea component is the ocean. Inland depressions are filled to their spill levels "
        "assuming sufficient water. Water extents do not use a rainfall budget or time-dependent filling; lake "
        "evaporation and river width are not modelled.");
}

World generate(const Config& cfg) {
    cfg.validate();

    const auto started = Clock::now();
    const TectonicResult tectonics = generate_tectonics(cfg);
    const double tectonics_ms = elapsed_ms(started);

    const auto scaling_started = Clock::now();

    World world;
    world.config = cfg;
    world.layers = tectonics.layers;
    world.plates = tectonics.plates;
    world.layout_seed = tectonics.layout_seed;
    world.crust_seed = tectonics.crust_seed;
    world.detail_seed = tectonics.detail_seed;
    world.resolved_octaves = tectonics.resolved_octaves;
    world.warnings = tectonics.warnings;
    world.completed_stage = std::min(cfg.phase, kHighestImplementedStage);
    if (cfg.phase > kHighestImplementedStage) {
        world.warnings.push_back(
            "Stage " + std::to_string(cfg.phase) + " requested but stages " +
            std::to_string(kHighestImplementedStage + 1) +
            "-16 (water, climate, biomes, history, settlements, ...) are not implemented; this world stops at the "
            "tectonic kernel. See docs/porting-status.md.");
    }

    // Design space to metres. One factor for every seed; the config keeps the design
    // inputs so a world can be regenerated, and `effective` is what a consumer reads.
    const double factor = cfg.world_scale;
    world.effective = cfg;
    world.effective.globe_radius *= factor;
    world.effective.amplitude *= factor;
    world.effective.wavelength *= factor;
    world.effective.radius *= factor;
    world.effective.tectonic_relief *= factor;
    world.effective.sea_level *= factor;

    for (const char* name : kLengthLayers) {
        const auto it = world.layers.find(name);
        if (it != world.layers.end()) scale_layer(it->second, factor);
    }
    // A catchment is an area, so it takes the square.
    if (const auto it = world.layers.find("catchment"); it != world.layers.end()) {
        scale_layer(it->second, factor * factor);
    }

    world.spacing_m = tectonics.spacing_m * factor;
    world.effective_tpi_radius_m = tectonics.effective_tpi_radius_m * factor;

    if (tectonics.has_sediment_budget) {
        const double volume = factor * factor * factor;
        world.has_sediment_budget = true;
        world.sediment.eroded_m3 = tectonics.eroded_m3 * volume;
        world.sediment.deposited_m3 = tectonics.deposited_m3 * volume;
        world.sediment.stored_sediment_m3 = tectonics.stored_sediment_m3 * volume;
        world.sediment.balance_error_m3 = tectonics.balance_error_m3 * volume;
        world.sediment.passes = cfg.erosion_passes;
    }

    // Phase 1 has no surface yet, so there is nothing to call land.
    if (cfg.phase >= 2) {
        const Grid& height = world.layers.at("height");
        world.area = land_area(height, world.effective.globe_radius, world.effective.sea_level);
        Grid land(cfg.size);
        for (int z = 0; z < cfg.size; ++z) {
            for (int x = 0; x < cfg.size; ++x) {
                land.at(x, z) = height.at(x, z) > world.effective.sea_level ? 1.0 : 0.0;
            }
        }
        world.layers["land"] = land;

        // Water routes on the PHYSICAL sphere, not the design one: heights are in
        // metres by now and the node areas have to match them, or catchment is out
        // by 1/world_scale squared and every river threshold is meaningless.
        add_water(world);
    }

    world.timing_ms["tectonics"] = tectonics_ms;
    world.timing_ms["scale"] = elapsed_ms(scaling_started);
    world.timing_ms["total"] = elapsed_ms(started);
    return world;
}

}  // namespace fwg
