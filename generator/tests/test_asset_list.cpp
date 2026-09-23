// The exhaustive asset list: every identity reachable from a supported generator state.
//
// The content hash is PINNED to the reference's: the Python compile_asset_list over the same
// authored data produced 7b240fc7...0a, and a byte-for-byte comparison of the two documents
// found them identical. A change here is either a data change (regenerate the pin and say
// why) or a compiler that stopped reproducing the reference.
#include "check.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>

#include "fwg/asset_list.hpp"
#include "fwg/city_plan.hpp"
#include "fwg/civilizations.hpp"
#include "fwg/content.hpp"
#include "fwg/key_locations.hpp"
#include "fwg/nests.hpp"
#include "fwg/sha256.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

const JsonValue& assets_document() {
    static const JsonValue doc = compile_asset_list();
    return doc;
}

std::map<std::string, const JsonValue*> by_id() {
    std::map<std::string, const JsonValue*> out;
    for (const JsonValue& a : assets_document().at("assets").as_array()) out[a.at("id").as_string()] = &a;
    return out;
}

}  // namespace

TEST(asset_list_matches_the_references_digest_and_counts) {
    const JsonValue& doc = assets_document();
    check::equal(doc.at("schema").as_string(), std::string("fantasy-world-generator.asset-list"), "schema");
    check::equal(doc.at("schema_version").as_int(), std::int64_t{2}, "schema version");
    check::equal(doc.at("recipe_version").as_int(), std::int64_t{3}, "recipe");
    check::equal(doc.at("content_sha256").as_string(),
                 std::string("7b240fc7e77ca33913866f88910c7995e5871a48455f5e1a5f2e06374132530a"), "the reference's digest");
    const std::map<std::string, std::int64_t> expected = {
        {"production.world_asset_catalogue", 492}, {"simulation.biome_mutations", 156}, {"simulation.biomes", 13},
        {"simulation.building_packs", 84},        {"simulation.castle_planner", 13},    {"simulation.city_planner", 96},
        {"simulation.creature_profiles", 680},    {"simulation.history", 1},            {"simulation.key_location_exteriors", 75},
        {"simulation.key_locations", 97}};
    std::map<std::string, std::int64_t> got;
    for (const auto& [source, n] : doc.at("summary").at("by_source").as_object()) got[source] = n.as_int();
    check::is_true(got == expected, "counts by source");
    check::equal(doc.at("summary").at("total").as_int(), std::int64_t{1707}, "total");
    JsonDumpOptions canonical;
    canonical.ensure_ascii = false;
    canonical.sort_keys = true;
    canonical.item_separator = ",";
    canonical.key_separator = ":";
    check::equal(sha256_hex(dump_json(doc.at("assets"), canonical)), doc.at("content_sha256").as_string(),
                 "the digest is over the published rows");
}

TEST(asset_list_ids_are_sorted_unique_and_statuses_closed) {
    std::vector<std::string> ids;
    std::set<std::string> statuses;
    int unreachable = 0;
    for (const JsonValue& a : assets_document().at("assets").as_array()) {
        ids.push_back(a.at("id").as_string());
        statuses.insert(a.at("status").as_string());
        unreachable += a.at("status").as_string() == "unreachable";
    }
    check::is_true(std::is_sorted(ids.begin(), ids.end()), "sorted by id");
    check::is_true(std::adjacent_find(ids.begin(), ids.end()) == ids.end(), "unique");
    check::is_true(statuses == std::set<std::string>{"not_started", "schematic", "supported", "unreachable"}, "the four statuses");
    check::equal(unreachable, 26, "biomes 1 and 6 and their 24 variants keep their identity and lose their claim");
    const auto rows = by_id();
    for (const char* id : {"creature.kraken", "terrain.biome.017", "building.building_market_stall_generic",
                           "terrain.mutation.snow.umbral", "marker.city_ruins", "keyloc.part.adit_mouth"}) {
        check::is_true(rows.count(id) == 1, std::string("lists ") + id);
    }
    check::equal(rows.at("terrain.biome.001")->at("status").as_string(), std::string("unreachable"), "tundra is tombstoned");
    check::equal(dump_json(rows.at("production.mat-026")->at("selectors").at("biome_variant_ids")), std::string(R"(["marsh.umbral"])"),
                 "a variant selector");
    check::is_true(compile_asset_list() == assets_document(), "deterministic");
}

TEST(asset_list_building_references_name_standalone_civilizations) {
    std::set<std::string> known;
    for (const std::string_view id : civilization_ids()) known.emplace(id);
    std::set<std::string> seen;
    for (const JsonValue& a : assets_document().at("assets").as_array()) {
        if (a.at("source").as_string() != "simulation.building_packs") continue;
        for (const JsonValue& reference : a.at("metadata").at("references").as_array()) {
            for (const JsonValue& c : reference.at("civilization_ids").as_array()) {
                check::is_true(known.count(c.as_string()) == 1, "a civilization entity: " + c.as_string());
                seen.insert(c.as_string());
            }
        }
    }
    check::is_true(seen == known, "every civilization draws some building");
}

TEST(asset_list_names_every_identity_this_producers_tables_can_place) {
    // The potential set from THIS producer's own tables, independent of any one world: every
    // surface, variant, creature, structure, dwelling, key-location marker and exterior part
    // its generator could name must be listed.
    const auto rows = by_id();
    std::vector<std::string> expected{"marker.city_ruins"};
    for (const NaturalBiome& b : natural_biomes()) expected.emplace_back(b.asset_id);
    for (const BiomeVariant& v : biome_variant_catalogue()) expected.emplace_back(v.asset_id);
    for (const CreatureProfile& p : creature_profiles()) expected.push_back("creature." + std::string(p.id));
    for (const Structure& s : structures()) expected.emplace_back(s.id);
    for (const HousingProfile& h : housing_profiles()) expected.emplace_back(h.id);
    for (const KeyLocationArchetype& a : key_location_archetypes()) expected.push_back("marker.key_location." + std::string(a.id));
    for (const KeyLocationPart& p : key_location_parts()) expected.emplace_back(p.id);
    std::string missing;
    for (const std::string& id : expected) {
        if (rows.count(id) == 0) missing += id + " ";
    }
    check::is_true(missing.empty(), "listed: " + missing);
    check::equal(expected.size(), std::size_t{1 + 13 + 156 + 680 + 106 + 3 + 97 + 75}, "the tables' own sizes");
}

TEST(asset_list_covers_every_identity_a_generated_world_references) {
    Request request;
    request.seed = 42;
    request.size = 33;
    request.phase = 16;
    const World world = generate(resolve(request));
    const std::set<std::string> referenced = referenced_asset_ids(world);
    const auto rows = by_id();
    std::vector<std::string> missing;
    for (const std::string& id : referenced) {
        if (rows.count(id) == 0) missing.push_back(id);
    }
    std::string listed;
    for (const std::string& m : missing) listed += m + " ";
    check::is_true(missing.empty(), "every referenced identity is listed; missing: " + listed);
    // The reader is doing its job: a phase-16 world names every kind of identity.
    std::map<std::string, int> kinds;
    for (const std::string& id : referenced) ++kinds[id.substr(0, id.find('.'))];
    for (const char* prefix : {"terrain", "creature", "building", "keyloc", "marker"}) {
        check::is_true(kinds[prefix] > 0, std::string("the world names ") + prefix + " identities");
    }
    check::is_true(referenced.size() > 300, "hundreds of identities, not a handful");
    // PINNED GAP, reported rather than hidden: every city draws a building pack, but the
    // layout's anchor band never leaves the city's own node at a shipped width, so no pack
    // asset is drawn (settlements.sites[].city_layout.required_assets is empty everywhere;
    // docs/scale-defects.md) and none of the 84 building-pack identities is reachable from
    // this producer's worlds -- neither under its list identity nor under the bare pack
    // asset id a `required_assets` row would carry.
    check::is_true(std::none_of(referenced.begin(), referenced.end(),
                                [](const std::string& id) {
                                    return id.rfind("building.building_", 0) == 0 || id.rfind("building_", 0) == 0;
                                }),
                   "no building-pack identity is referenced by this producer's worlds");
}
