// The JSON request boundary: generate requests read from JSON, and refusals rendered as the
// reference's failure-v2 envelope -- including a patch's, which had no wire form before.
#include "check.hpp"

#include <string>
#include <vector>

#include "fwg/patch.hpp"
#include "fwg/request_json.hpp"
#include "product_fixtures.hpp"

using namespace fwg;

namespace {

PatchRefusal refusal_for(const char* body) {
    return product_test::refusal_of([&] { request_from_json(parse_json(body)); });
}

std::vector<std::string> keys(const JsonValue& object) {
    std::vector<std::string> out;
    for (const auto& m : object.as_object()) out.push_back(m.first);
    return out;
}

}  // namespace

TEST(request_refusal_document_has_the_references_keys_and_detail_tier) {
    const PatchRefusal r = refusal_for(R"({"overrides": {"size": 2000}})");
    const JsonValue full = refusal_document(r, true);
    check::is_true(keys(full) == std::vector<std::string>{"schema", "schema_version", "code", "message", "field", "received",
                                                          "expected", "suggestion", "retry"},
                   "the nine keys, in order");
    check::equal(full.at("schema").as_string(), std::string("fantasy-world-generator.failure"), "schema");
    check::equal(full.at("schema_version").as_int(), std::int64_t{2}, "version 2");
    check::equal(full.at("field").as_string(), std::string("size"), "names the field");
    check::equal(dump_json(full.at("received")), std::string("2000"), "echoes the value, as an integer");
    check::equal(dump_json(full.at("expected")), std::string(R"({"type": "integer", "min": 3, "max": 1025, "units": "cells"})"),
                 "states the bound");
    check::equal(dump_json(full.at("suggestion")), std::string(R"({"kind": "clamp", "value": 1025})"), "offers a value");
    check::is_true(full.at("retry").as_bool(), "retryable");
    const JsonValue terse = refusal_document(r, false);
    check::is_true(keys(terse) == std::vector<std::string>{"schema", "schema_version", "code", "message", "field"},
                   "terse: the core five only");
}

TEST(request_the_seven_refusal_shapes) {
    PatchRefusal r = refusal_for(R"({"overrides": {"rainpases": 3}})");
    check::equal(std::string(r.kind), std::string("unknown_field"), "a typo");
    JsonValue doc = refusal_document(r);
    check::equal(dump_json(doc.at("suggestion")), std::string(R"({"kind": "did_you_mean", "candidates": ["rain_passes", "erosion_passes"]})"),
                 "names the control meant, as the reference's nearest() does");
    check::is_true(doc.at("received").is_null(), "an unknown name echoes nothing");

    r = refusal_for(R"({"overrides": {"blood_occurrence": 0.5}})");
    doc = refusal_document(r);
    check::is_true(doc.at("expected").at("pinned").as_bool(), "a pinned control says it is pinned");
    check::equal(doc.at("suggestion").at("kind").as_string(), std::string("none"), "and suggests nothing");

    r = refusal_for(R"({"overrides": {"world_size": "huge"}})");
    doc = refusal_document(r);
    check::equal(dump_json(doc.at("expected")), std::string(R"({"type": "string", "choices": ["small", "medium", "large"]})"),
                 "a closed vocabulary is listed");

    r = refusal_for(R"({"recipe_version": 2})");
    check::equal(std::string(r.code), std::string("UNSUPPORTED_VERSION"), "a retired recipe");
    check::is_true(!r.retry, "is not retryable");
    check::equal(dump_json(refusal_document(r).at("suggestion")), std::string(R"({"kind": "choose", "value": 3, "candidates": [3]})"),
                 "and names the version that works");

    r = refusal_for(R"({"seed": "42"})");
    check::equal(std::string(r.kind), std::string("wrong_type"), "a string seed");
    check::equal(dump_json(refusal_document(r).at("received")), std::string("\"42\""), "echoed as sent");

    r = refusal_for(R"({"overrides": {"relief_m": 2000, "globe_radius": 5000}})");
    check::equal(std::string(r.kind), std::string("cross_field"), "relief without a width");
    check::is_true(refusal_document(r).at("field").is_null(), "no single field owns it");

    r = refusal_for(R"({"overrides": {"seed": 5}})");
    check::equal(dump_json(refusal_document(r).at("expected")), std::string(R"({"location": "request root"})"), "seed misplaced");

    r = refusal_for("[]");
    check::equal(dump_json(refusal_document(r).at("received")), std::string("\"list\""), "not an object: its type");
}

TEST(request_patch_refusals_render_through_the_same_envelope) {
    PatchConfig cfg;
    cfg.span = 1000.0;
    const PatchRefusal r = product_test::refusal_of([&] { cfg.validate(); });
    const JsonValue doc = refusal_document(r);
    check::equal(doc.at("field").as_string(), std::string("patch.span"), "field");
    check::equal(dump_json(doc.at("expected")), std::string(R"({"type": "number", "min": 4.0, "max": 512.0, "units": "m"})"),
                 "a patch bound");
    check::equal(dump_json(doc.at("suggestion")), std::string(R"({"kind": "clamp", "value": 512.0})"), "and its clamp");
}

TEST(request_maps_published_controls_onto_the_request) {
    const Request r = request_from_json(parse_json(
        R"({"recipe_version": 3, "seed": 7, "overrides": {"size": 33, "phase": 12, "world_size": "medium", "population_profile": "dwarf", "sea_level": -5.5, "draconic_occurrence": 0.9, "relief_m": 0, "circumference_km": 300}})"));
    check::equal(r.seed, std::uint32_t{7}, "seed");
    check::equal(*r.size, 33, "size");
    check::equal(*r.phase, 12, "phase");
    check::is_true(*r.world_size == WorldSize::Medium, "world_size");
    check::equal(*r.population_profile, std::string("dwarf"), "population_profile");
    check::equal(*r.sea_level, -5.5, "a Config field");
    check::equal(r.options.at("draconic_occurrence"), 0.9, "a world option");
    check::is_true(!r.relief_m.has_value(), "relief_m 0 keeps the default, as the reference reads it");
    check::equal(*r.circumference_km, 300.0, "circumference_km");
    const Request defaults = request_from_json(JsonValue::object());
    check::equal(defaults.seed, std::uint32_t{42}, "seed defaults to 42");
    check::is_true(defaults.options.empty() && !defaults.size, "an empty request overrides nothing");
    check::equal(std::string(refusal_for(R"({"overrides": {"size": 17.0}})").kind), std::string("wrong_type"),
                 "an integer control refuses a float");
    check::equal(std::string(refusal_for(R"({"overrides": {"crust_bias": NaN}})").kind), std::string("wrong_type"),
                 "a number must be finite");
}

TEST(request_difflib_and_nearest_match_cpython) {
    check::is_true(close_matches("appel", {"ape", "apple", "peach", "puppy"}) == std::vector<std::string>{"apple", "ape"},
                   "the difflib documentation's example");
    check::is_true(close_matches("wheel", {"while", "wheel", "whale", "wheeled"}, 2, 0.5) == std::vector<std::string>{"wheel", "wheeled"},
                   "best score first, n of them");
    const std::vector<std::string> controls = {"rain_passes", "rain_strength", "radius", "erosion_passes", "ridge"};
    // difflib alone scores `radius` above both rain controls (0.6 against 0.53); the prefix
    // tier puts what a model half-remembering a name meant ahead of it.
    check::is_true(nearest("rain", controls) == std::vector<std::string>{"rain_passes", "rain_strength", "radius"},
                   "prefix matches first, then difflib's");
    check::is_true(nearest("passes", controls) == std::vector<std::string>{"erosion_passes", "rain_passes"}, "substring");
    check::is_true(nearest("RIDGE", controls) == std::vector<std::string>{"ridge"}, "case-folded");
}
