#include "fwg/world.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

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
    "continental", "surface", "erosion", "deposition", "erosion_delta", "archipelago_relief"};

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

void add_climate(World& world) {
    const Config& cfg = world.effective;
    const int n = cfg.size;

    const auto held = sphere_grid(n, cfg.globe_radius);
    const SphereGrid& grid = *held;
    const std::size_t count = grid.node_count();

    const Grid& ground = world.layers.at("height");
    const Grid& surface_layer = world.layers.at("water_surface");
    const Grid& type_layer = world.layers.at("water_type");

    // Air rides over a lake at its spill level, not over the basin floor: the
    // transport reads the WATER surface, while temperature lapses from the ground.
    std::vector<double> surface(count);
    std::vector<char> is_water(count);
    for (std::size_t i = 0; i < count; ++i) {
        const int x = grid.point_x[i];
        const int z = grid.point_z[i];
        surface[i] = surface_layer.at(x, z);
        is_water[i] = type_layer.at(x, z) > 0.0 ? 1 : 0;
    }

    const double bearing = cfg.wind_bearing * std::numbers::pi / 180.0;
    const std::vector<Stencil> upwind = upwind_stencils(grid, bearing);
    const MoistureField air =
        transport_moisture(surface, is_water, upwind, cfg.rain_passes, cfg.rain_strength);

    std::vector<double> moisture(count);
    for (std::size_t i = 0; i < count; ++i) {
        const int x = grid.point_x[i];
        const int z = grid.point_z[i];
        const double temperature =
            surface_temperature(direction(x, z, n), ground.at(x, z), cfg.sea_level, cfg.temperature_offset);
        const double wetness = climate_wetness(air.rain[i], temperature) + cfg.moisture_bias;
        moisture[i] = std::max(0.0, std::min(1.0, wetness));
    }

    // Rainfall becomes runoff on the receiver graph the water stage built. 0.04 is
    // the unit-runoff reference rate, so this is an AREA-equivalent, not a volume.
    std::vector<double> runoff(count);
    double input_total = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        runoff[i] = grid.areas[i] * air.rain[i] / 0.04;
        input_total += runoff[i];
    }

    // Leaf-to-root accumulation: works for any acyclic receiver graph without
    // needing the water stage's settle order, which is not kept on the World.
    const std::vector<int>& parent = world.water.receivers;
    std::vector<int> children(count, 0);
    for (const int p : parent) {
        if (p >= 0) ++children[static_cast<std::size_t>(p)];
    }
    std::vector<int> queue;
    queue.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (children[i] == 0) queue.push_back(static_cast<int>(i));
    }
    for (std::size_t k = 0; k < queue.size(); ++k) {
        const auto i = static_cast<std::size_t>(queue[k]);
        const int p = parent[i];
        if (p < 0) continue;
        const auto pi = static_cast<std::size_t>(p);
        runoff[pi] += runoff[i];
        if (--children[pi] == 0) queue.push_back(p);
    }

    const double threshold_m2 = cfg.river_threshold_km2 * 1e6;
    std::vector<double> rain_river(count, 0.0);
    ClimateReport report;
    double outlet_total = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        if (!is_water[i] && parent[i] >= 0 && runoff[i] >= threshold_m2) {
            rain_river[i] = 1.0;
            report.river_segments.push_back({static_cast<int>(i), parent[i]});
        }
        if (parent[i] < 0) outlet_total += runoff[i];
    }

    world.layers["air_moisture"] = grid.to_grid(air.humidity);
    world.layers["rainfall"] = grid.to_grid(air.rain);
    world.layers["wind_uplift"] = grid.to_grid(air.uplift);
    world.layers["climate_moisture"] = grid.to_grid(moisture);
    world.layers["rain_runoff"] = grid.to_grid(runoff);
    world.layers["rain_river"] = grid.to_grid(rain_river);

    // A sparse sample for drawing, not a field: roughly eight by six arrows.
    const int x_stride = std::max(1, (n - 1) / 8);
    const int z_stride = std::max(1, (n - 1) / 6);
    for (std::size_t i = 0; i < count; ++i) {
        const int x = grid.point_x[i];
        const int z = grid.point_z[i];
        if (z == 0 || z == n - 1 || x % x_stride != 0 || z % z_stride != 0) continue;
        const double lon = 2.0 * std::numbers::pi * x / (n - 1) - std::numbers::pi;
        const double lat = std::numbers::pi / 2.0 - std::numbers::pi * z / (n - 1);
        const Vec3 east{-std::sin(lon), 0.0, std::cos(lon)};
        const Vec3 north{-std::sin(lat) * std::cos(lon), std::cos(lat), -std::sin(lat) * std::sin(lon)};
        const double sb = std::sin(bearing);
        const double cb = std::cos(bearing);
        report.wind_arrows.push_back({x, z, direction(x, z, n),
                                      {sb * east.x + cb * north.x, sb * east.y + cb * north.y,
                                       sb * east.z + cb * north.z}});
    }

    report.residual = air.residual;
    report.passes = cfg.rain_passes;
    report.transport_step_m = cfg.globe_radius * 2.0 * std::numbers::pi / (n - 1);
    report.runoff_input_m2_equivalent = input_total;
    report.runoff_outlets_m2_equivalent = outlet_total;

    world.climate = report;
    world.has_climate = true;
    world.systems.push_back("climate");

    world.warnings.push_back(
        "Fixed compass-bearing wind transports humidity from equilibrium ocean and lake surfaces. Rising air "
        "loses extra moisture as rainfall, drying the lee. Relative rainfall and runoff proxies, not mm/year or "
        "discharge. Lake volumes are not solved from this rain budget. Fixed iteration count; the residual is "
        "the largest humidity change on the final pass, and there is no convergence guarantee.");
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
    world.ecology.archipelagos = tectonics.archipelagos;
    world.layout_seed = tectonics.layout_seed;
    world.crust_seed = tectonics.crust_seed;
    world.detail_seed = tectonics.detail_seed;
    world.resolved_octaves = tectonics.resolved_octaves;
    world.warnings = tectonics.warnings;
    world.completed_stage = std::min(cfg.phase, kHighestImplementedStage);
    world.systems = {"tectonics"};
    if (cfg.phase >= 3) world.systems.push_back("surface_detail");
    if (cfg.phase >= 4) world.systems.push_back("erosion");
    if (cfg.phase > kHighestImplementedStage) {
        world.warnings.push_back(
            "Stage " + std::to_string(cfg.phase) + " requested but stages " +
            std::to_string(kHighestImplementedStage + 1) +
            "-16 (roads, populated regions, beasts, age transitions) are not implemented; this world stops "
            "after stage " + std::to_string(kHighestImplementedStage) + ". See docs/porting-status.md.");
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
    // Stopped HERE, where the scaling ends. It used to be stopped at the end of
    // generate(), so it reported everything after tectonics -- 4.3 s of a 4.9 s world at
    // size 513 -- and read as by far the most expensive thing the generator does. It is
    // a few million multiplications and takes a handful of milliseconds.
    world.timing_ms["scale"] = elapsed_ms(scaling_started);

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

    // Stage 5, "Connected water", and not before. This ran from phase 2, which put
    // rivers and catchment into worlds that had not yet been eroded -- stages 3 and 4
    // rewrite the surface those rivers were routed over, so a phase-2 world published a
    // drainage network for a landscape it did not have.
    if (cfg.phase >= 5) {
        // Water routes on the PHYSICAL sphere, not the design one: heights are in
        // metres by now and the node areas have to match them, or catchment is out
        // by 1/world_scale squared and every river threshold is meaningless.
        add_water(world);
        world.systems.push_back("water");

        // Stage 6: a second tectonic epoch. Stage 7: the scars its draining left.
        // Both rewrite the surface, so everything after them reads the world they made.
        if (cfg.phase >= 6) {
            const auto epoch_started = Clock::now();
            const SurfaceSnapshot before = tectonic_transition(world);
            world.timing_ms["tectonic_transition"] = elapsed_ms(epoch_started);

            if (cfg.phase >= 7) {
                const auto relics_started = Clock::now();
                carve_relics(world, before);
                world.timing_ms["relic_carving"] = elapsed_ms(relics_started);
            }
        }

        // Stage 8, "Wind and rain". This was gated on phase 6 along with everything
        // below it, which made phases 6 through 10 differ only by the relic carving at
        // 7: asking for stage 8 and asking for stage 10 built the same world, and the
        // ladder stopped meaning anything past 7.
        if (cfg.phase >= 8) {
            const auto climate_started = Clock::now();
            add_climate(world);
            world.timing_ms["climate"] = elapsed_ms(climate_started);
            const auto labels_started = Clock::now();
            add_terrain_labels(world);
            world.timing_ms["terrain_labels"] = elapsed_ms(labels_started);
        }

        // Stage 9, "Leylines".
        //
        // Ecology sits here rather than at stage 8, where the reference puts it. The
        // reference runs environment at 8, then the networks, then a SECOND environment
        // pass at 9 to pick up the ley fields. This port has one ecology pass, and ten
        // of the thirteen magical regions read the ley layers, so that pass has to
        // follow them. The cost is that a stage-8 world here has weather but no
        // environment layers, where the reference's has both.
        if (cfg.phase >= 9) {
            const auto ley_started = Clock::now();
            generate_networks(world);
            world.timing_ms["leylines"] = elapsed_ms(ley_started);

            // Astrology after the leylines: the tide surges each school's strength,
            // and lunar_sensitivity reads the instability fields they publish.
            const auto sky_started = Clock::now();
            add_astrology(world);
            world.timing_ms["astrology"] = elapsed_ms(sky_started);

            const auto ecology_started = Clock::now();
            add_environment(world);
            world.timing_ms["ecology"] = elapsed_ms(ecology_started);

            // Astrology again, now that ecology has published coastal_exposure.
            //
            // lunar_sensitivity's shore term reads that layer, and astrology has to run
            // before ecology because ecology reads the leyline fields astrology's own
            // inputs depend on. Run once, the shore term silently read zero on every
            // non-lake cell -- three quarters of the published layer was wrong, with the
            // coastal half simply absent. The reference splits ecology into two passes
            // to break the cycle; this port runs the cheaper of the two twice instead.
            // Safe because refresh_astrology assigns rather than accumulates and does
            // not reseed the moon.
            refresh_astrology(world);

            // Stage 10's first half. After ecology, because it reads the cold-relabelled
            // natural biome, and after the leylines, because it reads their fields.
            const auto variants_started = Clock::now();
            add_biome_variants(world);
            world.timing_ms["biome_variants"] = elapsed_ms(variants_started);

            // The continuous field is a definition, not a computation: it is derived
            // from the seed and evaluated on demand by whoever samples it.
            world.terrain_detail = make_terrain_detail(cfg.seed);
            world.has_terrain_detail = true;
            world.systems.push_back("terrain_detail");
        }

        // Stage 10, "Founding". The world had been reporting completed_stage 10 with no
        // settlements in it at all -- a stage claimed rather than run.
        if (cfg.phase >= 10) {
            const auto settled_started = Clock::now();
            add_settlements(world);
            world.timing_ms["settlements"] = elapsed_ms(settled_started);
        }

        // Stage 11, "Roads".
        if (cfg.phase >= 11) {
            const auto roads_started = Clock::now();
            add_roads(world);
            world.timing_ms["roads"] = elapsed_ms(roads_started);
        }
    }

    world.timing_ms["tectonics"] = tectonics_ms;
    world.timing_ms["total"] = elapsed_ms(started);
    return world;
}

}  // namespace fwg
