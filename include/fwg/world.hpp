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
#include "fwg/history.hpp"
#include "fwg/magic.hpp"
#include "fwg/tectonics.hpp"
#include "fwg/astrology.hpp"
#include "fwg/beasties_stage.hpp"
#include "fwg/religion.hpp"
#include "fwg/colleges.hpp"
#include "fwg/humans.hpp"
#include "fwg/nests.hpp"
#include "fwg/populated_stage.hpp"
#include "fwg/society.hpp"
#include "fwg/roads.hpp"
#include "fwg/settlements_stage.hpp"
#include "fwg/biomes.hpp"
#include "fwg/climate.hpp"
#include "fwg/detail.hpp"
#include "fwg/ecology.hpp"
#include "fwg/content.hpp"
#include "fwg/water.hpp"

namespace fwg {

// The highest stage of the sixteen-stage ladder this library implements. Stages
// above it are accepted and reported honestly rather than silently claimed.
// The highest CONTIGUOUS stage. Stages 12 and 13 now both run, so this is 13; above it
// a great deal is ported and unwired, and `systems` is the channel that says what
// actually ran. This number only ever promises that everything below it did.
inline constexpr int kHighestImplementedStage = 13;

struct SeasonalMonth {
    Grid temperature;
    Grid snow;
    Grid water_ice;
};

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

    bool has_ecology = false;
    EcologyReport ecology;

    bool has_magic = false;
    MagicReport magic;

    bool has_astrology = false;
    Moon moon;
    Almanac lunar_almanac;

    bool has_epoch = false;
    GeologicalEpoch epoch;

    // The continuous height field's definition. Present from stage 8; an engine builds
    // a HeightField from the world and samples terrain anywhere.
    bool has_terrain_detail = false;
    TerrainDetail terrain_detail;

    // Twelve months of temperature, snow cover and water ice. Proxies: none of them
    // modify bedrock, and there is no weather.
    std::vector<SeasonalMonth> seasonal_months;

    // Stage 10. Cities, what they can feed, and the record of how they were founded.
    bool has_settlements = false;
    SettlementReport settlements;

    // Stage 11. What the cities built between themselves.
    bool has_roads = false;
    RoadNetwork roads;

    // Stage 13's first half. Animals and monsters, in two passes that never read
    // each other.
    bool has_wildlife = false;
    NestBlock wildlife;
    NestBlock beast_nests;

    // Stage 12. The hinterland its cities farm, the seasons they eat through, and
    // the harbours, fisheries and trade that grow on top.
    bool has_humans = false;
    HumanHinterland humans;
    SeasonalFood seasonal_food;
    SocietyReport society;
    CollegeReport colleges;

    // Stage 13's second half: the pantheon, the cosmology and the faiths.
    bool has_religion = false;
    Religion religion;

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

// Coastal and marine habitats, metals, the cold relabel that actually puts boreal
// forest, cold tundra and land ice on the map, island habitat, the seasonal months and
// the thirteen magical regions.
void add_environment(World& world);

World generate(const Config& cfg);

}  // namespace fwg
