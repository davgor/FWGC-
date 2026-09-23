// The portable terrain taxonomy, version 1: the reference's hand-authored fixture, strict
// types, no aliases, and agreement with the generator's catalogues and the asset list.
#include "check.hpp"

#include <set>
#include <string>

#include "fwg/asset_list.hpp"
#include "fwg/content.hpp"
#include "fwg/magic.hpp"
#include "fwg/taxonomy.hpp"
#include "product_fixtures.hpp"

using namespace fwg;

namespace {

JsonValue request(const char* taxonomy, const char* recipe, const char* natural, const char* school) {
    return parse_json(std::string(R"({"taxonomy_version": )") + taxonomy + R"(, "recipe_version": )" + recipe +
                      R"(, "natural_biome_id": )" + natural + R"(, "magic_school": )" + school + "}");
}

}  // namespace

TEST(taxonomy_fixture_cases) {
    const JsonValue cases = product_test::fixture("taxonomy-v1.json");
    for (const JsonValue& c : cases.at("valid").as_array()) {
        const JsonValue result = resolve_terrain_identity(c.at("request"));
        check::equal(dump_json(result), dump_json(c.at("expected")), "resolves " + dump_json(c.at("request")));
    }
    for (const JsonValue& r : cases.at("invalid").as_array()) {
        check::throws([&] { resolve_terrain_identity(r); }, "refuses " + dump_json(r));
    }
}

TEST(taxonomy_document_is_the_published_registry) {
    const JsonValue doc = taxonomy_document();
    check::equal(dump_json(doc.at("schools")), std::string(R"(["weave", "umbral", "infernal", "radiant", "fire", "water", "earth", "air"])"),
                 "school ids in exported order");
    std::string ids;
    for (const JsonValue& row : doc.at("natural_biomes").as_array()) ids += std::to_string(row.at("id").as_int()) + " ";
    check::equal(ids, std::string("0 1 2 3 4 5 6 7 8 13 15 16 17 "), "sparse natural ids");
    check::equal(dump_json(doc.at("retired_natural_ids")), std::string("[9, 10, 11, 12, 14]"), "retired ids reserved");
    check::equal(doc.at("aliases").size(), std::size_t{0}, "no aliases");
    JsonValue copy = taxonomy_document();
    copy.at("schools").as_array().clear();
    check::equal(taxonomy_document().at("schools").size(), std::size_t{8}, "caller-owned copies");
}

TEST(taxonomy_every_known_potential_state_resolves_to_a_listed_asset) {
    std::set<std::string> assets;
    const JsonValue list = compile_asset_list();
    for (const JsonValue& a : list.at("assets").as_array()) assets.insert(a.at("id").as_string());
    std::set<std::string> resolved;
    for (const NaturalBiome& b : natural_biomes()) {
        const TerrainIdentity t = resolve_terrain_identity(1, 3, b.id, std::nullopt);
        check::equal(t.core, std::string(b.core), "core of " + std::to_string(b.id));
        check::equal(t.asset_id, std::string(b.asset_id), "asset of " + std::to_string(b.id));
        check::is_true(assets.count(t.asset_id) == 1, t.asset_id + " is in the asset list");
        resolved.insert(t.asset_id);
    }
    for (const BiomeVariant& v : biome_variant_catalogue()) {
        if (v.hidden) {
            // Taxonomy v1 names only the eight known schools; the hidden ones are refused
            // although the asset list publishes their identities. Pinned, not endorsed.
            check::throws([&] { resolve_terrain_identity(1, 3, v.core_biome_id, v.magic_school); },
                          std::string("hidden school refused: ") + std::string(v.id));
            continue;
        }
        const TerrainIdentity t = resolve_terrain_identity(1, 3, v.core_biome_id, v.magic_school);
        check::equal(t.asset_id, std::string(v.asset_id), "asset of " + std::string(v.id));
        check::equal(*t.variant_id, std::string(v.id), "variant of " + std::string(v.id));
        check::is_true(assets.count(t.asset_id) == 1, t.asset_id + " is in the asset list");
        resolved.insert(t.asset_id);
    }
    check::equal(resolved.size(), std::size_t{117}, "13 natural + 104 known-school variants");
}

TEST(taxonomy_is_strict_and_has_no_aliases) {
    for (const char* bad : {"true", "false", "1.0", "null", "\"1\"", "[]", "{}"}) {
        check::throws([&] { resolve_terrain_identity(request(bad, "3", "13", "null")); }, std::string("taxonomy_version ") + bad);
        check::throws([&] { resolve_terrain_identity(request("1", bad, "13", "null")); }, std::string("recipe_version ") + bad);
        check::throws([&] { resolve_terrain_identity(request("1", "3", bad, "null")); }, std::string("natural id ") + bad);
    }
    for (const char* school : {"\"holy\"", "\"primordial\"", "\"necrotic\"", "\"nature\"", "\"arcane\"", "\"storm\"",
                               "\"Radiant\"", "\" fire\"", "\"\"", "0", "[]", "{}"}) {
        check::throws([&] { resolve_terrain_identity(request("1", "3", "13", school)); }, std::string("school ") + school);
    }
    for (const char* retired : {"9", "10", "11", "12", "14"}) {
        const PatchRefusal r = product_test::refusal_of([&] { resolve_terrain_identity(request("1", "3", retired, "null")); });
        check::equal(r.field, std::string("natural_biome_id"), std::string("retired id ") + retired);
    }
    const PatchRefusal version = product_test::refusal_of([&] { resolve_terrain_identity(request("1", "2", "0", "null")); });
    check::equal(std::string(version.code), std::string("UNSUPPORTED_VERSION"), "a retired recipe");
    check::is_true(!version.retry, "not retryable with a value");
    for (const char* shape : {"null", "[]", "\"marsh.water\"", R"({"taxonomy_version": 1, "recipe_version": 3, "natural_biome_id": 13, "magic_school": "water", "extra": 1})",
                              R"({"taxonomy_version": 1, "recipe_version": 3, "natural_biome_id": 13})"}) {
        check::throws([&] { resolve_terrain_identity(parse_json(shape)); }, std::string("shape ") + shape);
    }
}
