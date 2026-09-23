// The capability descriptor and the control catalogue.
//
// The descriptor describes THIS producer, so every version it claims is pinned here
// against the code that owns it: a claim that drifts from the code fails a test rather than
// a consumer. The reference's negotiation fixture is run too; where its "valid" cases name
// a version this producer does not publish, the refusal is asserted and explained.
#include "check.hpp"

#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "fwg/asset_list.hpp"
#include "fwg/capabilities.hpp"
#include "fwg/city_plan.hpp"
#include "fwg/coordinates.hpp"
#include "fwg/counter_kernel.hpp"
#include "fwg/request_json.hpp"
#include "fwg/serialize.hpp"
#include "fwg/sha256.hpp"
#include "fwg/taxonomy.hpp"
#include "fwg/world.hpp"
#include "fwg/world_scene.hpp"
#include "product_fixtures.hpp"

using namespace fwg;

namespace {

std::vector<std::int64_t> versions(const JsonValue& descriptor, std::string_view capability) {
    std::vector<std::int64_t> out;
    for (const JsonValue& v : descriptor.at("supported").at(capability).as_array()) out.push_back(v.as_int());
    return out;
}

bool claims(const JsonValue& descriptor, std::string_view capability) {
    return descriptor.at("supported").contains(capability);
}

const JsonValue& small_world_document() {
    static const JsonValue doc = [] {
        Request request;
        request.size = 17;
        request.phase = 16;
        std::ostringstream text;
        write_world_json(text, generate(resolve(request)), false, false);
        return parse_json(text.str());
    }();
    return doc;
}

bool accepted(const JsonValue& request) {
    try {
        require_capabilities(request);
        return true;
    } catch (const PatchError&) {
        return false;
    }
}

}  // namespace

TEST(capabilities_claims_are_pinned_to_the_code_that_owns_them) {
    const JsonValue d = capabilities_document();
    check::equal(d.at("schema").as_string(), std::string("fantasy-world-generator.capabilities"), "schema");
    check::equal(d.at("capability_version").as_int(), std::int64_t{1}, "descriptor version");
    const JsonValue& world = small_world_document();
    check::is_true(versions(d, "world_json") == std::vector<std::int64_t>{world.at("schema_version").as_int()},
                   "world_json is the schema version the writer stamps");
    check::equal(world.at("schema_version").as_int(), std::int64_t{kWorldSchemaVersion}, "writer and constant agree");
    check::is_true(versions(d, "generation_algorithm") == std::vector<std::int64_t>{world.at("generator_version").as_int()},
                   "generation_algorithm is the generator_version a world carries");
    check::is_true(versions(d, "recipe") == std::vector<std::int64_t>{Config{}.world_recipe}, "recipe");
    check::is_true(versions(d, "city_plans_json") == std::vector<std::int64_t>{CityPlans{}.version}, "city plans");
    check::is_true(versions(d, "hamlet_plans_json") == std::vector<std::int64_t>{HamletPlans{}.version}, "hamlet plans");
    check::is_true(versions(d, "asset_list_json") == std::vector<std::int64_t>{kAssetListSchemaVersion}, "asset list");
    check::is_true(versions(d, "coordinates") == std::vector<std::int64_t>{coordinates::kCoordinateVersion}, "coordinates");
    check::is_true(versions(d, "terrain_taxonomy") == std::vector<std::int64_t>{kTaxonomyVersion}, "taxonomy");
    check::is_true(versions(d, "world_controls") == std::vector<std::int64_t>{kControlsVersion}, "controls");
    check::equal(d.at("kernel").at("max_events_per_command").as_int(), std::int64_t{kernel::kMaxBatch}, "kernel batch");
    check::equal(d.at("kernel").at("max_effects_per_evaluation").as_int(), std::int64_t{kernel::kMaxBatch}, "kernel budget");
    check::equal(d.at("kernel").at("max_retained_receipts").as_int(), std::int64_t{kernel::kMaxReceipts}, "kernel receipts");
    // A block the document does not carry is not a supported JSON product, and one it does
    // carry is claimed at the version the scene stamps.
    check::equal(claims(d, "world_scene_json"), world.contains("world_scene"), "world_scene_json tracks the document");
    check::is_true(versions(d, "world_scene_json") == std::vector<std::int64_t>{kWorldSceneVersion}, "world scene");
    check::is_true(versions(d, "height_tile_json") == std::vector<std::int64_t>{kHeightTileVersion}, "height tile");
    check::equal(world.at("world_scene").at("version").as_int(), std::int64_t{kWorldSceneVersion},
                 "the scene block stamps the constant");
    for (const JsonValue& u : d.at("unsupported").as_array()) {
        check::is_true(!claims(d, u.as_string()), "unsupported and supported are disjoint: " + u.as_string());
    }
}

TEST(capabilities_negotiation_against_the_reference_fixture) {
    const JsonValue cases = product_test::fixture("capabilities-v1.json");
    const auto& valid = cases.at("valid").as_array();
    check::is_true(accepted(valid.at(0)), "the kernel capabilities");
    check::is_true(accepted(valid.at(1)), "an empty requirement");
    // The reference's third "valid" case names ITS versions: generation algorithm 16 and
    // world_json 2. This producer publishes neither, and says so (data/capabilities.json
    // `deviations`). Its fourth -- taxonomy, asset list, height tile, city plans and world
    // scene -- names products this producer publishes at the reference's versions.
    const PatchRefusal third = product_test::refusal_of([&] { require_capabilities(valid.at(2)); });
    check::equal(third.field, std::string("requires.generation_algorithm"), "the first unmatched claim is named");
    check::is_true(accepted(valid.at(3)), "the reference's product inventory, at its versions");
    const PatchRefusal unsupported = product_test::refusal_of([&] {
        require_capabilities(parse_json(R"({"capability_version": 1, "requires": {"unreal_editor_import": 1}})"));
    });
    check::equal(unsupported.field, std::string("requires.unreal_editor_import"), "an unsupported capability is named");
    check::equal(std::string(unsupported.kind), std::string("unsupported_api"), "and not retryable with a value");
    for (const JsonValue& request : cases.at("invalid").as_array()) {
        const bool native = request.at("requires").contains("native_kernel");
        check::equal(accepted(request), native, "fixture invalid case " + dump_json(request) +
                                                    (native ? " (native_kernel is supported here)" : ""));
    }
    for (const char* bad : {"true", "1.0", "\"1\"", "0", "2", "null"}) {
        check::throws([&] { capabilities_document(parse_json(bad)); }, std::string("capability version ") + bad);
    }
    for (const char* bad : {"true", "2.0", "\"2\"", "[2]", "null", "{}"}) {
        check::throws([&] { require_capabilities(parse_json(std::string(R"({"capability_version": 1, "requires": {"world_json": )") + bad + "}}")); },
                      std::string("world_json version ") + bad);
    }
    for (const char* bad : {"null", "[]", "{}", R"({"capability_version": 1, "requires": []})",
                            R"({"capability_version": 1, "requires": {}, "extra": 0})"}) {
        check::throws([&] { require_capabilities(parse_json(bad)); }, std::string("request ") + bad);
    }
    const PatchRefusal typo = product_test::refusal_of(
        [] { require_capabilities(parse_json(R"({"capability_version": 1, "requires": {"world_jsn": 1}})")); });
    check::equal(std::string(typo.suggestion), std::string("did_you_mean"), "a misspelt capability gets suggestions");
    JsonValue copy = capabilities_document();
    copy.at("supported").at("world_json").as_array().clear();
    check::equal(capabilities_document().at("supported").at("world_json").size(), std::size_t{1}, "caller-owned");
}

TEST(controls_catalogue_status_hash_and_groups) {
    const JsonValue doc = controls_document();
    const JsonValue& controls = doc.at("controls");
    check::equal(controls.size(), std::size_t{209}, "209 controls");
    std::map<std::string, int> statuses;
    for (const auto& [name, row] : controls.as_object()) {
        ++statuses[row.at("status").as_string()];
        const bool pinned = row.contains("min") && python_equal(row.at("min"), row.at("max"));
        check::equal(row.at("status").as_string() == "pinned", pinned, "pinned exactly when min == max: " + name);
        check::is_true(!row.at("description").as_string().empty(), "described: " + name);
    }
    check::is_true(statuses == std::map<std::string, int>{{"inert", 1}, {"open", 201}, {"pinned", 7}}, "201 open, 7 pinned, 1 inert");
    check::equal(controls.at("stubbornness").at("status").as_string(), std::string("inert"), "stubbornness is read by nothing");
    // sha256 over json.dumps(controls, indent=1, sort_keys=True), recomputed independently by
    // the reference's recipe over this catalogue.
    check::equal(doc.at("content_sha256").as_string(),
                 std::string("19e65b478cfb4b2b6fa613c49ae802c1a25e9a0f90d9be573da14a18c7cfca59"), "content hash");
    std::vector<std::string> keys;
    for (const auto& m : doc.as_object()) keys.push_back(m.first);
    check::is_true(keys == std::vector<std::string>{"content_sha256", "controls", "recipe_version", "schema", "version"},
                   "sorted keys, as the reference's file");
    check::equal(controls.at("circumference_km").at("overrides").size(), std::size_t{9}, "the width derives nine controls");
}

TEST(controls_describe_filters_and_defaults) {
    const JsonValue all = describe_controls();
    check::equal(all.at("total").as_int(), std::int64_t{209}, "total is the whole catalogue");
    check::equal(all.at("returned").as_int(), std::int64_t{201}, "by default only the open ones");
    check::equal(all.at("groups").size(), std::size_t{27}, "every group, always");
    ControlQuery pinned;
    pinned.include_pinned = true;
    check::equal(describe_controls(pinned).at("returned").as_int(), std::int64_t{209}, "include_pinned");
    ControlQuery group;
    group.group = "drainage";
    const JsonValue drainage = describe_controls(group);
    check::equal(drainage.at("returned").as_int(), std::int64_t{3}, "group is case-insensitive and exact");
    ControlQuery query;
    query.query = "RIVER";
    const JsonValue matched = describe_controls(query);
    for (const auto& [name, row] : matched.at("controls").as_object()) {
        std::string haystack = name + " " + row.at("description").as_string();
        for (char& c : haystack) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        check::is_true(haystack.find("river") != std::string::npos, "query matches name or description: " + name);
    }
    ControlQuery names;
    names.names = std::vector<std::string>{"size", "seed"};
    std::vector<std::string> got;
    const JsonValue named = describe_controls(names);
    for (const auto& m : named.at("controls").as_object()) got.push_back(m.first);
    check::is_true(got == std::vector<std::string>{"seed", "size"}, "names, sorted");
    ControlQuery blood;
    blood.group = "Blood";
    check::equal(describe_controls(blood).at("returned").as_int(), std::int64_t{5}, "a pinned control hides by default");
    ControlQuery typo;
    typo.names = std::vector<std::string>{"rainpases"};
    const PatchRefusal r = product_test::refusal_of([&] { describe_controls(typo); });
    check::equal(r.field, std::string("rainpases"), "unknown name refused by name");
    check::equal(r.candidates_json.front(), std::string("\"rain_passes\""), "and the control meant comes first");
}

TEST(controls_every_published_name_is_honoured_by_this_producer) {
    // `producers` says "native" for every control: each must route through a request.
    for (const std::string& name : control_names()) {
        if (name == "seed") continue;  // a request-root field; inside overrides it is refused
        JsonValue overrides = JsonValue::object();
        overrides.set(name, control_definition(name)->at("default"));
        JsonValue body = JsonValue::object();
        body.set("overrides", overrides);
        resolve(request_from_json(body));
    }
    // The width inputs derive what they claim to, and an explicit value wins.
    const auto resolved = [](const char* overrides) {
        return resolve(request_from_json(parse_json(std::string(R"({"overrides": )") + overrides + "}")));
    };
    const Config narrow = resolved(R"({"circumference_km": 200, "relief_m": 1500})");
    const Config wide = resolved(R"({"circumference_km": 400, "relief_m": 2500})");
    const std::map<std::string, std::pair<double, double>> derived = {
        {"globe_radius", {narrow.globe_radius, wide.globe_radius}},
        {"tectonic_relief", {narrow.tectonic_relief, wide.tectonic_relief}},
        {"amplitude", {narrow.amplitude, wide.amplitude}},
        {"wavelength", {narrow.wavelength, wide.wavelength}},
        {"settlement_spacing", {narrow.settlement_spacing, wide.settlement_spacing}},
        {"support_reach", {narrow.support_reach, wide.support_reach}},
        {"culture_link_cost", {narrow.culture_link_cost, wide.culture_link_cost}},
        {"river_threshold_km2", {narrow.river_threshold_km2, wide.river_threshold_km2}},
    };
    for (const auto& [name, values] : derived) {
        check::is_true(values.first != values.second, name + " moves with the world's shape");
        check::equal(control_definition(name)->at("derived_from").size(), std::size_t{2}, name + " says so");
    }
    // PINNED DISCREPANCY: orogeny is published as derived, default 1.0; this producer's
    // derivation always writes 10.0 unless overridden, so it never moves with the shape.
    check::equal(narrow.orogeny, wide.orogeny, "orogeny does not move with the shape");
    check::equal(narrow.orogeny, 10.0, "and is 10, not the published 1.0");
    check::equal(resolved(R"({"circumference_km": 400, "support_reach": 5000})").support_reach, 5000.0, "an explicit value wins");
}

TEST(controls_published_bounds_this_producer_narrows_are_pinned) {
    // The catalogue is the reference's control surface; Config::validate is this port's. They
    // disagree at five published bounds (see the report). Pinned so neither side moves
    // silently. Everything else -- every default, and every other open control at both of
    // its published bounds -- resolves.
    const auto refused = [](const char* body) {
        try {
            resolve(request_from_json(parse_json(body)));
            return false;
        } catch (const std::exception&) {
            return true;
        }
    };
    check::is_true(refused(R"({"overrides": {"globe_radius": 0.01}})"), "globe_radius below 1");
    check::is_true(refused(R"({"overrides": {"radius": 0.01}})"), "radius below 1");
    check::is_true(refused(R"({"overrides": {"wavelength": 0.01}})"), "wavelength below 1");
    check::is_true(refused(R"({"overrides": {"layout_variation": 1000001}})"), "layout_variation above 1e6");
    check::is_true(refused(R"({"overrides": {"detail_variation": 4294967295}})"), "detail_variation above 1e6");
    check::is_true(!refused(R"({"overrides": {"layout_variation": 1000000}})"), "1e6 itself resolves");
}
