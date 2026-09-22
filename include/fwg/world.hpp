// A generated world, and the entry point that produces one.
//
// The generator works in DESIGN SPACE -- a unit-agnostic coordinate system in which
// the recipe's constants were authored -- and the world is converted to metres at
// the end by a single scale factor. `config` holds the design inputs so a world can
// be regenerated exactly; `effective` holds the same parameters in metres, which is
// what a consumer should read. Layers are always in metres by the time a World is
// returned.
//
// Stage coverage is deliberately partial and is tracked in docs/porting-status.md.
// A World therefore reports which stage it reached rather than implying a whole one.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "fwg/config.hpp"
#include "fwg/grid.hpp"
#include "fwg/tectonics.hpp"
#include "fwg/biomes.hpp"
#include "fwg/climate.hpp"
#include "fwg/content.hpp"
#include "fwg/water.hpp"

namespace fwg {

// The highest stage of the sixteen-stage ladder this library implements. Stages
// above it are accepted and reported honestly rather than silently claimed.
inline constexpr int kHighestImplementedStage = 5;

struct AreaReport {
    double land_km2 = 0.0;
    double submerged_km2 = 0.0;
    double total_km2 = 0.0;
    double land_fraction = 0.0;
    double largest_sample_km2 = 0.0;
};

// Reference-sphere footprint: latitude-weighted, duplicate longitude excluded. NOT
// slope surface area, NOT connected land, NOT walkable area.
AreaReport land_area(const Grid& height, double radius, double sea_level);

struct SedimentBudget {
    double eroded_m3 = 0.0;
    double deposited_m3 = 0.0;
    double stored_sediment_m3 = 0.0;
    double balance_error_m3 = 0.0;
    int passes = 0;
};

struct World {
    int generator_version = 1;
    Config config;      // design space, as supplied
    Config effective;   // metres

    std::map<std::string, Grid> layers;
    std::vector<Plate> plates;

    std::uint64_t layout_seed = 0;
    std::uint32_t crust_seed = 0;
    std::uint32_t detail_seed = 0;

    double spacing_m = 0.0;
    double effective_tpi_radius_m = 0.0;
    int resolved_octaves = 0;

    AreaReport area;
    bool has_sediment_budget = false;
    SedimentBudget sediment;

    // Present from stage 5. The receiver graph here is what rivers, settlement
    // siting and sea routes are all built on later.
    bool has_water = false;
    WaterReport water;

    bool has_climate = false;
    ClimateReport climate;

    bool has_terrain = false;
    TerrainReport terrain;

    // Which subsystems actually ran. More honest than a stage number while the
    // ladder is partial: climate can be present while stages 6 and 7 are not.
    std::vector<std::string> systems;

    std::vector<std::string> warnings;
    std::map<std::string, double> timing_ms;

    // Highest stage this build actually ran. See docs/porting-status.md for what
    // each stage contains and which are not ported yet.
    int completed_stage = 0;
};

// Stage 5's hydrology half. Reads `height` and the effective config; writes the
// water_* layers, `river`, `routed_catchment` and the water report.
void add_water(World& world);

// Stage 8's climate half, gated on phase >= 6 as the reference gates it. Reads the
// water surface and the height field; writes the rainfall layers and the climate
// report. Runs on whatever surface exists, so inserting stages 6-7 later changes its
// input without changing this code.
void add_climate(World& world);

// Terrain labels: temperature, moisture, biome and landform, plus the sparse feature
// markers. Runs from phase 2, and uses transported rainfall for moisture once climate
// has provided it — before that, a latitude band plus seeded noise.
void add_terrain_labels(World& world);

World generate(const Config& cfg);

}  // namespace fwg
