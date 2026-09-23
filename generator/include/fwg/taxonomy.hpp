// The portable terrain taxonomy, version 1: which terrain identities a consumer may name.
//
// A port of the reference's fantasy_world_generator.taxonomy. The registry is
// data/terrain-taxonomy.json -- eight school ids in exported order, thirteen sparse natural
// ids (0-8, 13, 15-17; never array offsets), the retired ids 9-12 and 14 that stay
// reserved, and no aliases. It is independent of the generator's tables on purpose; the
// codegen checks the two agree.
//
// resolve_terrain_identity takes exactly {taxonomy_version, recipe_version,
// natural_biome_id, magic_school} and answers {natural_biome_id, core, magic_school,
// variant_id, asset_id}: magic_school null is the natural surface, "terrain.biome.NNN";
// a school gives the variant "<core>.<school>", "terrain.mutation.<core>.<school>". No
// coercion anywhere: a boolean, a float or a numeric string is not an integer, "Radiant"
// is not "radiant", and "holy" is not an alias of anything.
//
// The four hidden schools are NOT in taxonomy v1, so their 52 variant identities -- which
// the asset list does publish -- cannot be resolved here. That is the reference's
// behaviour, kept; widening it is a taxonomy version 2 decision.
//
// Refusals are PatchError.
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "fwg/json_value.hpp"

namespace fwg {

inline constexpr int kTaxonomyVersion = 1;

struct TerrainIdentity {
    int natural_biome_id = 0;
    std::string core;
    std::optional<std::string> magic_school;
    std::optional<std::string> variant_id;
    std::string asset_id;
};

// A fresh, caller-owned copy of the registry.
JsonValue taxonomy_document();

TerrainIdentity resolve_terrain_identity(int taxonomy_version, int recipe_version, int natural_biome_id,
                                         std::optional<std::string_view> magic_school);
// The strict JSON form.
JsonValue resolve_terrain_identity(const JsonValue& request);

}  // namespace fwg
