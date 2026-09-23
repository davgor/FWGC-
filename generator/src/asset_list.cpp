#include "fwg/asset_list.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "fwg/content.hpp"
#include "fwg/product_content.hpp"
#include "fwg/serialize.hpp"
#include "fwg/sha256.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

const JsonValue& sources() {
    static const JsonValue parsed = parse_json(asset_sources_json());
    return parsed;
}

JsonValue rgb(const int (&color)[3]) {
    return JsonValue(JsonValue::Array{JsonValue(color[0]), JsonValue(color[1]), JsonValue(color[2])});
}

// One row, in the reference's key order.
JsonValue row(std::string id, std::string_view kind, std::string name, std::string_view source, std::string_view status,
              JsonValue selectors, JsonValue metadata) {
    JsonValue out = JsonValue::object();
    out.set("id", std::move(id));
    out.set("kind", std::string(kind));
    out.set("name", std::move(name));
    out.set("source", std::string(source));
    out.set("status", std::string(status));
    out.set("selectors", std::move(selectors));
    out.set("metadata", std::move(metadata));
    return out;
}

// CPython's str.title() on an ASCII identifier: a letter after a non-letter is upper-cased,
// a letter after a letter lower-cased; digits and spaces are left alone and break words.
std::string title_case(std::string text) {
    bool previous_cased = false;
    for (char& c : text) {
        const bool upper = c >= 'A' && c <= 'Z';
        const bool lower = c >= 'a' && c <= 'z';
        if (upper || lower) {
            if (previous_cased && upper) c = static_cast<char>(c - 'A' + 'a');
            if (!previous_cased && lower) c = static_cast<char>(c - 'a' + 'A');
            previous_cased = true;
        } else {
            previous_cased = false;
        }
    }
    return text;
}

bool truthy(const JsonValue* value) {
    if (value == nullptr) return false;
    switch (value->type()) {
        case JsonValue::Type::Null: return false;
        case JsonValue::Type::Bool: return value->as_bool();
        case JsonValue::Type::Integer: return !(value->is_int64() && value->as_int() == 0);
        case JsonValue::Type::Float: return value->as_float() != 0.0;
        case JsonValue::Type::String: return !value->as_string().empty();
        default: return value->size() != 0;
    }
}

// Every key of `from` except the excluded ones, in its own order.
JsonValue without(const JsonValue& from, std::initializer_list<std::string_view> excluded) {
    JsonValue out = JsonValue::object();
    for (const auto& [key, value] : from.as_object()) {
        if (std::find(excluded.begin(), excluded.end(), key) == excluded.end()) out.as_object().emplace_back(key, value);
    }
    return out;
}

void terrain_assets(std::vector<JsonValue>& out) {
    for (const NaturalBiome& b : natural_biomes()) {
        JsonValue selectors = JsonValue::object();
        selectors.set("biome_ids", JsonValue(JsonValue::Array{JsonValue(b.id)}));
        JsonValue metadata = JsonValue::object();
        metadata.set("display_color_rgb", rgb(b.color));
        out.push_back(row(std::string(b.asset_id), "terrain_surface", std::string(b.name), "simulation.biomes",
                          b.reachable ? "supported" : "unreachable", std::move(selectors), std::move(metadata)));
    }
}

void creature_assets(std::vector<JsonValue>& out) {
    for (const JsonValue& profile : sources().at("creature_profiles").as_array()) {
        JsonValue selectors = JsonValue::object();
        selectors.set("species_id", profile.at("id"));
        selectors.set("medium", profile.at("medium"));
        selectors.set("family", profile.at("family"));
        out.push_back(row("creature." + profile.at("id").as_string(), "creature", profile.at("name").as_string(),
                          "simulation.creature_profiles", "supported", std::move(selectors),
                          without(profile, {"id", "name"})));
    }
}

// building_pack_data(), then _building_assets(): an option is drawable by exactly the
// civilizations whose bindings name it, and an option nobody binds is dropped.
void building_assets(std::vector<JsonValue>& out) {
    const JsonValue& bindings = sources().at("entity_building_packs");
    std::vector<std::string> order;  // first appearance of each asset id
    std::map<std::string, JsonValue> rows;
    for (const JsonValue& pack : sources().at("building_packs").at("packs").as_array()) {
        const std::string& pack_id = pack.at("id").as_string();
        for (const JsonValue& option : pack.at("building_options").as_array()) {
            const std::string& option_id = option.at("id").as_string();
            std::vector<std::string> people;
            for (const auto& [entity, packs] : bindings.as_object()) {
                const JsonValue* bound = packs.find(pack_id);
                if (bound == nullptr) continue;
                const auto& chosen = bound->as_array();
                if (std::any_of(chosen.begin(), chosen.end(), [&](const JsonValue& o) { return o.as_string() == option_id; })) {
                    people.push_back(entity);
                }
            }
            if (people.empty()) continue;
            std::sort(people.begin(), people.end());
            JsonValue civilization_ids = JsonValue::array();
            for (const std::string& p : people) civilization_ids.push_back(p);
            for (const JsonValue& choice : option.at("asset_choices").as_array()) {
                const std::string& asset_id = choice.at("asset_id").as_string();
                auto found = rows.find(asset_id);
                if (found == rows.end()) {
                    JsonValue selectors = JsonValue::object();
                    selectors.set("asset_id", asset_id);
                    JsonValue metadata = JsonValue::object();
                    metadata.set("references", JsonValue::array());
                    std::string name = asset_id;
                    std::replace(name.begin(), name.end(), '_', ' ');
                    found = rows.emplace(asset_id, row("building." + asset_id, "building", title_case(name),
                                                       "simulation.building_packs", "supported", std::move(selectors),
                                                       std::move(metadata)))
                                .first;
                    order.push_back(asset_id);
                }
                JsonValue reference = JsonValue::object();
                reference.set("pack_id", pack_id);
                reference.set("option_id", option_id);
                reference.set("weight", choice.at("weight"));
                reference.set("civilization_ids", civilization_ids);
                found->second.at("metadata").at("references").push_back(std::move(reference));
            }
        }
    }
    for (const std::string& asset_id : order) {
        JsonValue& references = rows.at(asset_id).at("metadata").at("references");
        std::stable_sort(references.as_array().begin(), references.as_array().end(),
                         [](const JsonValue& a, const JsonValue& b) {
                             const auto& ap = a.at("pack_id").as_string();
                             const auto& bp = b.at("pack_id").as_string();
                             if (ap != bp) return ap < bp;
                             const auto& ao = a.at("option_id").as_string();
                             const auto& bo = b.at("option_id").as_string();
                             if (ao != bo) return ao < bo;
                             return *a.at("weight").to_double() < *b.at("weight").to_double();
                         });
        out.push_back(std::move(rows.at(asset_id)));
    }
}

void production_assets(std::vector<JsonValue>& out) {
    const JsonValue& catalogue = sources().at("production_catalogue");
    if (!python_equal(catalogue.at("schema_version"), JsonValue(2))) {
        throw std::invalid_argument("Production catalogue requires schema 2");
    }
    std::vector<std::string_view> unreachable_cores;
    for (const NaturalBiome& b : natural_biomes()) {
        if (!b.reachable) unreachable_cores.push_back(b.core);
    }
    const auto natural_known = [](const JsonValue& v) { return v.is_int64() && natural_biome(static_cast<int>(v.as_int())) != nullptr; };
    const auto variant_known = [](const JsonValue& v) {
        if (!v.is_string()) return false;
        for (const BiomeVariant& b : biome_variant_catalogue()) {
            if (b.id == v.as_string()) return true;
        }
        return false;
    };
    for (const JsonValue& item : catalogue.at("assets").as_array()) {
        const std::string& id = item.at("id").as_string();
        for (const auto& [field, known] :
             {std::pair<std::string_view, bool (*)(const JsonValue&)>{"biome_ids", +natural_known},
              std::pair<std::string_view, bool (*)(const JsonValue&)>{"biome_variant_ids", +variant_known}}) {
            const JsonValue* selector = item.find(field);
            if (selector == nullptr) continue;
            if (!selector->is_array() ||
                !std::all_of(selector->as_array().begin(), selector->as_array().end(), known)) {
                throw std::invalid_argument("Invalid production biome selector: " + id + "." + std::string(field));
            }
        }
        // A tombstoned id stays a legal selector; a brief whose ONLY ground is tombstoned is a
        // commission for a surface no world renders, and is refused.
        bool any_ground = false;
        bool reachable_ground = false;
        if (const JsonValue* ids = item.find("biome_ids"); ids != nullptr && ids->is_array()) {
            for (const JsonValue& v : ids->as_array()) {
                any_ground = true;
                reachable_ground = reachable_ground || natural_biome(static_cast<int>(v.as_int()))->reachable;
            }
        }
        if (const JsonValue* ids = item.find("biome_variant_ids"); ids != nullptr && ids->is_array()) {
            for (const JsonValue& v : ids->as_array()) {
                any_ground = true;
                const std::string& variant = v.as_string();
                const std::string_view core = std::string_view(variant).substr(0, variant.find('.'));
                reachable_ground = reachable_ground ||
                                   std::find(unreachable_cores.begin(), unreachable_cores.end(), core) == unreachable_cores.end();
            }
        }
        if (any_ground && !reachable_ground) {
            throw std::invalid_argument("Production asset targets only unreachable ground: " + id);
        }
        JsonValue selectors = JsonValue::object();
        for (const std::string_view key : {"biome_ids", "biome_variant_ids", "people", "settlement_role"}) {
            const JsonValue* value = item.find(key);
            if (value != nullptr && !value->is_null()) selectors.set(key, *value);
        }
        std::string lowered = id;
        for (char& c : lowered) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        out.push_back(row("production." + lowered, item.at("kind").as_string(), item.at("name").as_string(),
                          "production.world_asset_catalogue", item.at("status").as_string(), std::move(selectors),
                          without(item, {"id", "name", "kind", "status", "biome_ids", "biome_variant_ids", "people",
                                         "settlement_role"})));
    }
}

void history_assets(std::vector<JsonValue>& out) {
    for (const BiomeVariant& b : biome_variant_catalogue()) {
        const NaturalBiome* core = natural_biome(b.core_biome_id);
        JsonValue selectors = JsonValue::object();
        selectors.set("core_biome_id", b.core_biome_id);
        selectors.set("core", std::string(b.core));
        selectors.set("magic_school", std::string(b.magic_school));
        JsonValue metadata = JsonValue::object();
        metadata.set("display_color_rgb", rgb(b.color));
        metadata.set("group", std::string(b.group));
        metadata.set("recipe_version", kAssetListRecipeVersion);
        out.push_back(row(std::string(b.asset_id), "terrain_surface", std::string(b.name), "simulation.biome_mutations",
                          core != nullptr && core->reachable ? "supported" : "unreachable", std::move(selectors),
                          std::move(metadata)));
    }
    JsonValue selectors = JsonValue::object();
    selectors.set("settlement_role", "ruins");
    JsonValue metadata = JsonValue::object();
    metadata.set("source_culture", "carried by each generated ruin");
    metadata.set("recipe_version", kAssetListRecipeVersion);
    out.push_back(row("marker.city_ruins", "marker", "City ruins", "simulation.history", "supported", std::move(selectors),
                      std::move(metadata)));
}

JsonValue structure_row(const JsonValue& s, std::string_view source, std::string_view rendering) {
    JsonValue selectors = JsonValue::object();
    selectors.set("building_id", s.at("id"));
    JsonValue metadata = JsonValue::object();
    metadata.set("dimensions_m", s.at("dimensions_m"));
    metadata.set("plot_m", s.at("plot_m"));
    metadata.set("rendering", std::string(rendering));
    return row(s.at("id").as_string(), "building", s.at("name").as_string(), source, "schematic", std::move(selectors),
               std::move(metadata));
}

void planner_assets(std::vector<JsonValue>& out) {
    const JsonValue& libraries = sources().at("structure_blocks");
    for (const auto& [key, library] : libraries.as_object()) {
        if (key == "castle") continue;
        for (const JsonValue& block : library.at("blocks").as_array()) {
            for (const JsonValue& s : block.at("structures").as_array()) {
                out.push_back(structure_row(s, "simulation.city_planner",
                                            "Labeled rectangle; production art remains unassigned"));
            }
        }
    }
    for (const auto& [key, house] : sources().at("housing_profiles").as_object()) {
        out.push_back(structure_row(house, "simulation.city_planner", "Labeled rectangle; production art remains unassigned"));
    }
    for (const JsonValue& block : libraries.at("castle").at("blocks").as_array()) {
        for (const JsonValue& s : block.at("structures").as_array()) {
            out.push_back(structure_row(s, "simulation.castle_planner",
                                        "Fortification module or bailey shell; production art remains unassigned"));
        }
    }
}

void key_location_assets(std::vector<JsonValue>& out) {
    const JsonValue& document = sources().at("key_locations");
    for (const JsonValue& archetype : document.at("archetypes").as_array()) {
        const JsonValue& family = document.at("families").at(archetype.at("family").as_string());
        JsonValue selectors = JsonValue::object();
        selectors.set("key_location_kind", archetype.at("id"));
        JsonValue metadata = JsonValue::object();
        metadata.set("family", archetype.at("family"));
        metadata.set("tier", archetype.at("tier"));
        metadata.set("domain", archetype.at("domain"));
        metadata.set("display_color_rgb", family.at("color"));
        metadata.set("glyph", family.at("glyph"));
        metadata.set("interior", truthy(archetype.find("interior")));
        metadata.set("rendering", "Map marker; production art remains unassigned.");
        metadata.set("catalogue_revision", document.at("revision"));
        metadata.set("recipe_version", kAssetListRecipeVersion);
        out.push_back(row("marker.key_location." + archetype.at("id").as_string(), "marker", archetype.at("name").as_string(),
                          "simulation.key_locations", "schematic", std::move(selectors), std::move(metadata)));
    }
    const JsonValue& exteriors = sources().at("key_location_exteriors");
    for (const JsonValue& part : exteriors.at("parts").as_array()) {
        JsonValue selectors = JsonValue::object();
        selectors.set("part_id", part.at("id"));
        JsonValue metadata = JsonValue::object();
        metadata.set("part_kind", part.at("kind"));
        metadata.set("dimensions_m", part.at("dimensions_m"));
        metadata.set("plot_m", part.at("plot_m"));
        const JsonValue* shaping = part.find("shaping");
        metadata.set("shaping", shaping != nullptr ? *shaping : JsonValue());
        metadata.set("rendering", "Labeled footprint; production art remains unassigned.");
        metadata.set("catalogue_revision", exteriors.at("revision"));
        metadata.set("recipe_version", kAssetListRecipeVersion);
        out.push_back(row(part.at("id").as_string(), part.at("kind").as_string() == "structure" ? "building" : "marker",
                          part.at("name").as_string(), "simulation.key_location_exteriors", "schematic",
                          std::move(selectors), std::move(metadata)));
    }
}

// The biome layers name identities by number: a natural id, or a flat index into the
// variant catalogue. Negative means none.
void layer_ids(const JsonValue& grid, bool variants, std::set<std::string>& out) {
    if (!grid.is_array()) return;
    std::set<long long> seen;
    for (const JsonValue& line : grid.as_array()) {
        if (!line.is_array()) continue;
        for (const JsonValue& cell : line.as_array()) {
            const std::optional<double> v = cell.to_double();
            if (v && *v >= 0.0) seen.insert(std::llround(*v));
        }
    }
    const auto catalogue = biome_variant_catalogue();
    for (const long long v : seen) {
        if (variants) {
            out.insert(v < static_cast<long long>(catalogue.size()) ? std::string(catalogue[static_cast<std::size_t>(v)].asset_id)
                                                                     : "terrain.mutation.<index " + std::to_string(v) + ">");
        } else {
            std::array<char, 32> buffer{};
            std::snprintf(buffer.data(), buffer.size(), "terrain.biome.%03lld", v);
            out.insert(buffer.data());
        }
    }
}

void walk(const JsonValue& value, std::string_view key, bool in_shaping, std::set<std::string>& out) {
    if (value.is_object()) {
        for (const auto& [k, item] : value.as_object()) walk(item, k, key == "shaping" || in_shaping, out);
        return;
    }
    if (value.is_array()) {
        for (const JsonValue& item : value.as_array()) walk(item, key, in_shaping || key == "shaping", out);
        return;
    }
    if (!value.is_string()) return;
    if (key == "asset_id" || key == "variant_asset_id" || key == "building_id" || key == "structure_id" ||
        (key == "part" && in_shaping)) {
        out.insert(value.as_string());
    } else if (key == "species_id") {
        out.insert("creature." + value.as_string());
    }
}

}  // namespace

JsonValue compile_asset_list() {
    std::vector<JsonValue> assets;
    terrain_assets(assets);
    creature_assets(assets);
    building_assets(assets);
    production_assets(assets);
    history_assets(assets);
    planner_assets(assets);
    key_location_assets(assets);
    std::stable_sort(assets.begin(), assets.end(), [](const JsonValue& a, const JsonValue& b) {
        return python_less(a.at("id").as_string(), b.at("id").as_string());
    });
    std::vector<std::string> duplicates;
    for (std::size_t i = 1; i < assets.size(); ++i) {
        const std::string& id = assets[i].at("id").as_string();
        if (id == assets[i - 1].at("id").as_string() && (duplicates.empty() || duplicates.back() != id)) {
            duplicates.push_back(id);
        }
    }
    if (!duplicates.empty()) {
        std::string listed;
        for (const std::string& d : duplicates) listed += (listed.empty() ? "'" : ", '") + d + "'";
        throw std::invalid_argument("duplicate normalized asset IDs: [" + listed + "]");
    }

    JsonValue list(JsonValue::Array(std::make_move_iterator(assets.begin()), std::make_move_iterator(assets.end())));
    JsonDumpOptions canonical;
    canonical.ensure_ascii = false;
    canonical.sort_keys = true;
    canonical.item_separator = ",";
    canonical.key_separator = ":";
    canonical.allow_nan = true;  // the reference hashes with json.dumps' default here
    const std::string digest = sha256_hex(dump_json(list, canonical));

    std::map<std::string, long long> by_kind;
    std::map<std::string, long long> by_source;
    for (const JsonValue& asset : list.as_array()) {
        ++by_kind[asset.at("kind").as_string()];
        ++by_source[asset.at("source").as_string()];
    }
    JsonValue summary = JsonValue::object();
    summary.set("total", static_cast<long long>(list.size()));
    JsonValue kinds = JsonValue::object();
    for (const auto& [kind, n] : by_kind) kinds.set(kind, n);
    JsonValue sources_count = JsonValue::object();
    for (const auto& [source, n] : by_source) sources_count.set(source, n);
    summary.set("by_kind", std::move(kinds));
    summary.set("by_source", std::move(sources_count));

    JsonValue document = JsonValue::object();
    document.set("schema", std::string(kAssetListSchema));
    document.set("schema_version", kAssetListSchemaVersion);
    document.set("recipe_version", kAssetListRecipeVersion);
    document.set("scope", "All assets reachable from potential recipe 3 generator states; not a single generated world.");
    document.set("content_sha256", digest);
    document.set("summary", std::move(summary));
    document.set("assets", std::move(list));
    return document;
}

std::set<std::string> referenced_asset_ids(const JsonValue& world_document) {
    std::set<std::string> out;
    for (const auto& [key, value] : world_document.as_object()) {
        if (key == "layers") continue;
        walk(value, key, false, out);
    }
    if (const JsonValue* layers = world_document.find("layers"); layers != nullptr && layers->is_object()) {
        for (const char* natural : {"biome", "natural_biome"}) {
            if (const JsonValue* grid = layers->find(natural)) layer_ids(*grid, false, out);
        }
        if (const JsonValue* grid = layers->find("biome_variant")) layer_ids(*grid, true, out);
    }
    return out;
}

std::set<std::string> referenced_asset_ids(const World& world) {
    std::ostringstream text;
    write_world_json(text, world, /*pretty=*/false, /*include_timings=*/false);
    return referenced_asset_ids(parse_json(text.str()));
}

}  // namespace fwg
