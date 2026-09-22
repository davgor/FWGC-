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
    const double dlat = kPi / (n - 1);
    const double dlon = 2.0 * kPi / (n - 1);

    double land = 0.0;
    double max_sample = 0.0;
    for (int z = 0; z < n; ++z) {
        const double lat = kPi / 2.0 - z * dlat;
        const double north = std::min(kPi / 2.0, lat + dlat / 2.0);
        const double south = std::max(-kPi / 2.0, lat - dlat / 2.0);
        const double weight = radius * radius * dlon * (std::sin(north) - std::sin(south));
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
    }

    world.timing_ms["tectonics"] = tectonics_ms;
    world.timing_ms["scale"] = elapsed_ms(scaling_started);
    world.timing_ms["total"] = elapsed_ms(started);
    return world;
}

}  // namespace fwg
