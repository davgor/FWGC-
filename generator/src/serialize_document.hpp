// The world-document writers for the blocks that describe the world rather than a stage of
// it: the ladder (`phases`), the request (`recipe`), the peoples (`population`,
// `population_profiles`, `population_budget`, `peoples`, `civilizations`), the one-frame
// scene (`world_scene`), the diagnostics (`debug_stats`) and the opt-in stage snapshots
// (`build_stages`).
//
// Private to the library, like serialize_blocks.hpp, and split from it for the same reason:
// these read the registry, the control surface and the World as a whole, where the late
// blocks each read one stage's struct.
//
// FIELD NAMES ARE THE REFERENCE'S, checked against a generated reference world (seed 42,
// size 65, phase 16) rather than recalled. Where a value differs in kind -- a 64-bit seed
// written as a string, a digest over this port's own canonical form -- the difference is
// named at the site.
#pragma once

#include <span>

#include "fwg/build_stages.hpp"
#include "fwg/json.hpp"
#include "fwg/world.hpp"

namespace fwg::serialize_detail {

// Defined in serialize.cpp: blocks the stage-8 and stage-10 writers already produced, now
// also needed elsewhere -- the scene carries the terrain detail it sampled, and generate()
// fingerprints the settlements block to decide whether a stage changed it.
void write_terrain_detail(JsonWriter& json, const TerrainDetail& detail);
void write_settlements(JsonWriter& json, const World& world);
void write_founding_event(JsonWriter& json, const FoundingEvent& event);
// The reference's natural_catalogue() and biome_catalogue(), as arrays: `terrain.biomes`,
// `terrain.natural_biomes` and `terrain.magical_biomes`, and every plan's own copies.
void write_natural_catalogue(JsonWriter& json);
void write_magical_catalogue(JsonWriter& json);

// Defined in serialize_document.cpp.
//
// The reference's Config dataclass, field by field in its declaration order, into an object
// the caller has opened: what `config`, `effective_config` and `recipe.resolved` all carry.
// `effective` scales the four lengths this port has no field for by world_scale, as the
// reference's apply_world_scale does.
void write_config_fields(JsonWriter& json, const Config& cfg, bool effective);
void write_phases(JsonWriter& json, const World& world);
void write_recipe(JsonWriter& json, const World& world);
void write_population(JsonWriter& json, const World& world);
void write_population_profiles(JsonWriter& json);
void write_population_budget(JsonWriter& json, const World& world);
void write_peoples(JsonWriter& json, const World& world);
void write_civilizations(JsonWriter& json, const World& world);
void write_world_scene(JsonWriter& json, const WorldScene& scene);
void write_debug_stats(JsonWriter& json, const World& world);
void write_build_stages(JsonWriter& json, std::span<const BuildStage> stages);

}  // namespace fwg::serialize_detail
