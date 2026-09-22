// Content tables, generated from data/ by tools/embed_content.py.
//
// The JSON under data/ is the source of truth; these are typed, zero-cost views of it
// baked into the binary, so the library still has no dependencies and needs no file
// beside it at runtime. See tools/embed_content.py for why, and run it with --check
// before committing a data change.
#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace fwg {

struct NaturalBiome {
    int id;                      // stable identity; NOT an index into the table
    std::string_view core;       // machine name, e.g. "boreal_forest"
    std::string_view name;       // display name
    std::string_view asset_id;   // "terrain.biome.NNN"
    int color[3];                // RGB, for maps and previews
    // False means "no art will be commissioned for this", NOT "cannot occur". A
    // tombstoned row keeps its identity and its POSITION, because the position is a
    // contract: biome_variant is a flat index into the cross product of these rows
    // with the magic schools.
    bool reachable;
};

// In catalogue order. Position is a contract — append, never insert.
std::span<const NaturalBiome> natural_biomes();

// By identity, not position. Returns nullptr for an unknown id.
const NaturalBiome* natural_biome(int id);

// Magic school machine names, in the order the variant table is indexed by.
std::span<const std::string_view> magic_schools();

// The school's name for a natural biome, both by POSITION in their tables.
std::string_view variant_name(std::size_t school_index, std::size_t biome_index);

}  // namespace fwg
