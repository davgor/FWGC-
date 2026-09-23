// GENERATED FILE -- DO NOT EDIT.
//
// Produced by tools/embed_content.py from data/terrain-taxonomy.json.
// Edit the JSON and regenerate; `python tools/embed_content.py --check` fails if this
// file is stale.

#include "fwg/product_content.hpp"

#include <string>

namespace fwg {

namespace {

// the terrain taxonomy: 592 bytes of JSON in 1 pieces.
constexpr std::string_view k_terrain_taxonomy_json[] = {
    R"fwgjson({"schema":"fantasy-world-generator.terrain-taxonomy","taxonomy_version":1,"recipe_version":3,"schools":["weave","umbral","infernal","radiant","fire","water","earth","air"],"natural_biomes":[{"id":0,"core":"ocean"},{"id":1,"core":"tundra"},{"id":2,"core":"desert"},{"id":3,"core":"grassland"},{"id":4,"core":"forest"},{"id":5,"core":"exposed_rock"},{"id":6,"core":"snow"},{"id":7,"core":"rainforest"},{"id":8,"core":"lake"},{"id":13,"core":"marsh"},{"id":15,"core":"boreal_forest"},{"id":16,"core":"cold_tundra"},{"id":17,"core":"land_ice"}],"retired_natural_ids":[9,10,11,12,14],"aliases":{}})fwgjson",
};

}  // namespace

std::string_view terrain_taxonomy_json() {
    static const std::string joined = [] {
        std::string text;
        text.reserve(592);
        for (const std::string_view piece : k_terrain_taxonomy_json) text.append(piece);
        return text;
    }();
    return joined;
}

}  // namespace fwg
