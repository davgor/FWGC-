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
#include "fwg/ages.hpp"
#include "fwg/beasts.hpp"
#include "fwg/fates.hpp"
#include "fwg/heroes.hpp"
#include "fwg/key_locations.hpp"
#include "fwg/story_web.hpp"
#include "fwg/visitation.hpp"
#include "fwg/nomads.hpp"
#include "fwg/npcs.hpp"
#include "fwg/castle_plan.hpp"
#include "fwg/city_plan.hpp"
#include "fwg/complete_stage.hpp"
#include "fwg/religion.hpp"
#include "fwg/colleges.hpp"
#include "fwg/corruption.hpp"
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
#include "fwg/world_scene.hpp"
#include "fwg/time_state.hpp"

namespace fwg {

// The highest stage of the sixteen-stage ladder this library implements: all of it.
// `completed_stage` only ever promises that every stage up to it ran, and `systems` is the
// evidence of what did.
inline constexpr int kHighestImplementedStage = 16;

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

    // Stage 13, between the nests and the faiths: the standing threat each city reads for
    // its walls. Re-assessed after every rebuild; the city planner reads regional_threat
    // as its defense priority, and the nomads widen it after the planners have read it.
    bool has_threat_assessments = false;
    ThreatAssessmentBlock threat_assessments;

    // Stage 13's second half: the pantheon, the cosmology and the faiths.
    bool has_religion = false;
    Religion religion;

    // Stage 16. The street plan of every city and hamlet, and the castle of every fortress.
    bool has_plans = false;
    CityPlans city_plans;
    HamletPlans hamlet_plans;
    CastlePlans castle_plans;  // one per fortress, in ascending id-string order

    // Stage 16. Every plan above, and the regional roads, in one globe-centred frame.
    bool has_world_scene = false;
    WorldScene world_scene;

    // Stage 16. The bands that do not settle, the herds that move with the seasons,
    // what a traveller meets, and what the bands did to the world around them.
    bool has_nomads = false;
    NomadBlock nomads;
    NomadEffects nomad_effects;
    bool has_beast_movements = false;
    BeastMovements beast_movements;
    EncounterIndex encounters;

    // Stage 16. The cast of heroes and the web of stories between them.
    bool has_heroes = false;
    HeroesBlock heroes;
    StoryWebBlock story_web;

    // Stage 16. Who lives and works in every settlement.
    bool has_npcs = false;
    NpcRoster npcs;

    // Stage 16. The places worth going: ruins, lairs, shrines and wonders, and the
    // plan of each.
    bool has_key_locations = false;
    KeyLocationBlock key_locations;
    KeyLocationPlanBlock key_location_plans;

    // Stages 14 and 15. What the ages destroyed, the antagonists that rose through them,
    // and the record of each turn. The history exists with no ages in it until one turns,
    // as the reference's does from stage 5: an unturned world has an empty history, not an
    // absent one.
    std::vector<Ruin> ruins;
    bool has_villains = false;
    VillainBlock villains;
    WorldHistory history;
    // Every summons this world has seen, departed or not. Written by the visitation API and
    // read at an age turn: a god still walking by visitation goes home when the age turns.
    std::vector<VisitationRecord> visitations;
    // Every corruption, cleansed or standing, in the order they happened. The typed lists are
    // the record; religion.visitations and religion.corruptions carry the same records as the
    // opaque JSON the religion pass hands forward, rewritten from these after each operation.
    std::vector<CorruptionRecord> corruptions;

    // The age this world is in. Zero until the first age transition turns it.
    int age = 0;

    // ---- The time and quest APIs (world_time.hpp) -- written by nothing else ----------
    // All absent on a world that has never been advanced, and absence is a fact: no clock
    // means the world is read at its founding year, no board means the quest lifecycle has
    // not begun, which is different from a lifecycle with no quests in it.
    std::optional<WorldClock> world_clock;  // the reference's `world_clock`
    std::optional<QuestBoard> quests;       // the reference's `quests`
    std::optional<LunarSurge> lunar_surge;  // the reference's `magic.lunar_surge`
    // Their operations go in history.operations with every other API's, in call order.
    // ---------------------------------------------------------------------------------

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
