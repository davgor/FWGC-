// The exhaustive asset list: every asset identity reachable from a supported generator
// state -- the potential set, not one sampled world.
//
// A port of the reference's fantasy_world_generator.asset_list.compile_asset_list, over
// the same authored data (see tools/embed/asset_sources.py). Nine sources, each compiled
// exactly as the reference compiles it:
//
//   simulation.biomes                  13 natural surfaces; a tombstoned one keeps its
//                                      identity and is marked `unreachable`
//   simulation.creature_profiles       680 creatures, metadata = the whole profile
//   simulation.building_packs          84 pack assets, with the civilizations that may
//                                      draw each one through each option
//   production.world_asset_catalogue   492 production briefs, selector-validated
//   simulation.biome_mutations         156 core x school variants (hidden schools
//                                      included) + marker.city_ruins
//   simulation.city_planner            96 structures and worker housing
//   simulation.castle_planner          13 castle structures
//   simulation.key_locations           97 archetype markers
//   simulation.key_location_exteriors  75 exterior kit parts
//
// Sorted by id, ids unique (a duplicate is refused), and `content_sha256` is sha256 over
// json.dumps(assets, ensure_ascii=False, separators=(",", ":"), sort_keys=True) -- the
// same bytes the reference hashes, which is why the metadata is copied verbatim from the
// authored rows rather than rebuilt from typed tables.
//
// status is one of supported | unreachable | schematic (simulation rows) or the
// production catalogue's own work state (today only not_started).
//
// WHAT THIS PRODUCER CAN ACTUALLY REFERENCE is narrower than the list, and that is
// reported rather than hidden: `referenced_asset_ids` reads a world document and returns
// every identity it names -- natural biomes and variants through the biome layers, every
// creature by species, every building and wall structure a plan places, every key-location
// marker, exterior part and ruin. The list must cover all of them (tested on a generated
// size-33 phase-16 world). The converse does not hold. Every city draws a building pack
// (settlements.sites[].city_layout), but at every shipped width the layout's anchor band
// never leaves the city's own node, so no pack asset is ever drawn and `required_assets` is
// empty (docs/scale-defects.md): the 84 simulation.building_packs identities are unreachable
// from this producer's worlds. And production briefs are commissions no world names by design.
//
// A world that DID draw one would name it here under its bare pack asset id -- the
// `asset_id` of a `required_assets` row, e.g. building_temple_generic -- where the list's
// identity is "building." + that id. The reader does not map it, because no world draws one.
#pragma once

#include <set>
#include <string>
#include <string_view>

#include "fwg/json_value.hpp"

namespace fwg {

struct World;

inline constexpr std::string_view kAssetListSchema = "fantasy-world-generator.asset-list";
inline constexpr int kAssetListSchemaVersion = 2;
inline constexpr int kAssetListRecipeVersion = 3;

// The whole document: schema, schema_version, recipe_version, scope, content_sha256,
// summary {total, by_kind, by_source} and assets, in the reference's key order. Throws
// std::invalid_argument (naming the row) for an invalid production selector, a brief whose
// only ground is tombstoned, or a duplicate id.
JsonValue compile_asset_list();

// Every asset identity a world document names. See the header comment for where each is
// read from.
std::set<std::string> referenced_asset_ids(const JsonValue& world_document);
// The same, over the document write_world_json writes for this world.
std::set<std::string> referenced_asset_ids(const World& world);

}  // namespace fwg
