// The product package's authored documents, as the JSON text they are published in.
//
// Generated from data/ by tools/embed/{capabilities,taxonomy,hero_guild,controls,
// asset_sources}.py and checked in, like every other content table. They are JSON rather
// than typed tables because each is published as JSON with free-form metadata copied from
// authored rows, and the published bytes depend on key order and on 1 versus 1.0 -- see
// tools/embed/json_text.py. Read them with fwg::parse_json; the modules that own them
// (capabilities.hpp, controls.hpp, taxonomy.hpp, hero_guild.hpp, asset_list.hpp) do, once.
//
// Each returns a view of a string that lives for the whole process.
#pragma once

#include <string_view>

namespace fwg {

// data/capabilities.json `descriptor`: the capability descriptor version 1 this producer
// publishes.
std::string_view capabilities_source_json();

// data/terrain-taxonomy.json `taxonomy`, verbatim.
std::string_view terrain_taxonomy_json();

// data/hero-guild-policies.json `document`, verbatim.
std::string_view hero_guild_policies_json();

// The control catalogue body -- schema, version, recipe_version and every control row with
// its derived status -- built from world-options.json, config-controls.json and
// control-annotations.json. Keys are sorted, as the reference's catalogue file is.
std::string_view world_controls_json();

// Everything the asset list compiles, in its source order: the raw creature profiles, the
// building packs and the entity bindings that filter them, the structure libraries, the
// housing profiles, the key-location archetypes and exterior parts, and the production
// catalogue.
std::string_view asset_sources_json();

}  // namespace fwg
