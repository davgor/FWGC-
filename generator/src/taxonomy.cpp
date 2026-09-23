#include "fwg/taxonomy.hpp"

#include <array>
#include <cstdio>
#include <vector>

#include "fwg/patch.hpp"
#include "fwg/product_content.hpp"
#include "fwg/request_json.hpp"

namespace fwg {
namespace {

const JsonValue& registry() {
    static const JsonValue parsed = parse_json(terrain_taxonomy_json());
    return parsed;
}

std::vector<std::string> schools() {
    std::vector<std::string> out;
    for (const JsonValue& s : registry().at("schools").as_array()) out.push_back(s.as_string());
    return out;
}

// The natural biome's core, or nullptr for an unknown or retired id.
const std::string* core_of(std::int64_t id) {
    for (const JsonValue& row : registry().at("natural_biomes").as_array()) {
        if (row.at("id").as_int() == id) return &row.at("core").as_string();
    }
    return nullptr;
}

JsonValue identity_json(const TerrainIdentity& t) {
    JsonValue out = JsonValue::object();
    out.set("natural_biome_id", t.natural_biome_id);
    out.set("core", t.core);
    out.set("magic_school", t.magic_school ? JsonValue(*t.magic_school) : JsonValue());
    out.set("variant_id", t.variant_id ? JsonValue(*t.variant_id) : JsonValue());
    out.set("asset_id", t.asset_id);
    return out;
}

// Everything after the shape and type checks, which the typed and JSON forms share.
// The three integers arrive already type-checked; a big integer matches nothing.
TerrainIdentity resolve_checked(const JsonValue& taxonomy_version, const JsonValue& recipe_version,
                                const JsonValue& natural_id, const JsonValue& school) {
    if (!python_equal(taxonomy_version, registry().at("taxonomy_version"))) {
        throw PatchError(refusal::unsupported_api("taxonomy_version", taxonomy_version, {registry().at("taxonomy_version")}));
    }
    if (!python_equal(recipe_version, registry().at("recipe_version"))) {
        PatchRefusal r = refusal::retired_version("recipe_version", recipe_version, {registry().at("recipe_version")});
        r.message = "unsupported recipe_version " + dump_json(recipe_version) + "; regenerate retired worlds";
        throw PatchError(std::move(r));
    }
    const std::string* core = natural_id.is_int64() ? core_of(natural_id.as_int()) : nullptr;
    if (core == nullptr) {
        std::vector<std::string> ids;
        for (const JsonValue& row : registry().at("natural_biomes").as_array()) ids.push_back(std::to_string(row.at("id").as_int()));
        PatchRefusal r = refusal::invalid_choice("natural_biome_id", natural_id, ids);
        r.message = "unknown or retired natural_biome_id " + dump_json(natural_id);
        r.expected_type = "integer";
        r.choices_json = ids;  // integers, as JSON
        r.suggestion = "none";
        r.candidates_json.clear();
        throw PatchError(std::move(r));
    }
    if (!school.is_null()) {
        if (!school.is_string()) throw PatchError(refusal::wrong_type("magic_school", school, "string"));
        const std::vector<std::string> known = schools();
        bool listed = false;
        for (const std::string& s : known) listed = listed || s == school.as_string();
        if (!listed) {
            PatchRefusal r = refusal::invalid_choice("magic_school", school, known);
            r.message = "unknown magic_school " + dump_json(school) + "; aliases are not supported";
            throw PatchError(std::move(r));
        }
    }
    TerrainIdentity t;
    t.natural_biome_id = static_cast<int>(natural_id.as_int());
    t.core = *core;
    if (school.is_null()) {
        std::array<char, 32> buffer{};
        std::snprintf(buffer.data(), buffer.size(), "terrain.biome.%03d", t.natural_biome_id);
        t.asset_id = buffer.data();
    } else {
        t.magic_school = school.as_string();
        t.variant_id = *core + "." + school.as_string();
        t.asset_id = "terrain.mutation." + *t.variant_id;
    }
    return t;
}

}  // namespace

JsonValue taxonomy_document() { return registry(); }

TerrainIdentity resolve_terrain_identity(int taxonomy_version, int recipe_version, int natural_biome_id,
                                         std::optional<std::string_view> magic_school) {
    return resolve_checked(JsonValue(taxonomy_version), JsonValue(recipe_version), JsonValue(natural_biome_id),
                           magic_school ? JsonValue(*magic_school) : JsonValue());
}

JsonValue resolve_terrain_identity(const JsonValue& request) {
    static constexpr std::string_view kFields[] = {"taxonomy_version", "recipe_version", "natural_biome_id", "magic_school"};
    bool exact = request.is_object() && request.size() == std::size(kFields);
    for (const std::string_view field : kFields) exact = exact && request.contains(field);
    if (!exact) {
        throw PatchError(refusal::cross_field(
            "terrain identity request must contain exactly: magic_school, natural_biome_id, recipe_version, taxonomy_version",
            {"magic_school", "natural_biome_id", "recipe_version", "taxonomy_version"}));
    }
    for (const std::string_view key : {"taxonomy_version", "recipe_version", "natural_biome_id"}) {
        const JsonValue& value = request.at(key);
        if (!value.is_integer()) {
            PatchRefusal r = refusal::wrong_type(std::string(key), value, "integer");
            r.message = std::string(key) + " must be an integer (not a boolean, float or string)";
            throw PatchError(std::move(r));
        }
    }
    return identity_json(resolve_checked(request.at("taxonomy_version"), request.at("recipe_version"),
                                         request.at("natural_biome_id"), request.at("magic_school")));
}

}  // namespace fwg
