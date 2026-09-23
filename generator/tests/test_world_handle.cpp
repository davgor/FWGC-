// World handles: the rule, the reference's parity fixture that binds a second
// implementation to it, and the operation envelope.
//
// Bytes are asserted before digests, as the fixture asks: two implementations that agree
// on a digest while disagreeing on the bytes agree by luck.
#include "check.hpp"

#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "fwg/serialize.hpp"
#include "fwg/world.hpp"
#include "fwg/world_handle.hpp"
#include "product_fixtures.hpp"

using namespace fwg;

namespace {

JsonValue world_doc(const char* operations_json, const char* extra_json = "{}") {
    JsonValue doc = parse_json(extra_json);
    doc.set("generator_version", 16);
    JsonValue history = JsonValue::object();
    history.set("operations", parse_json(operations_json));
    doc.set("history", history);
    return doc;
}

std::vector<std::string> strings(const JsonValue& array) {
    std::vector<std::string> out;
    for (const JsonValue& v : array.as_array()) out.push_back(v.as_string());
    return out;
}

}  // namespace

TEST(handle_fixture_cases_serialise_to_the_pinned_bytes_then_hash_to_the_pinned_handle) {
    const JsonValue fixture = product_test::fixture("world-handle-v1.json");
    std::set<std::string> handles;
    for (const JsonValue& c : fixture.at("cases").as_array()) {
        const std::string name = c.at("name").as_string();
        const std::string bytes = transfer_bytes(c.at("document"));
        check::equal(bytes, c.at("transfer_bytes").as_string(), "transfer bytes of " + name);
        check::equal(static_cast<long long>(bytes.size()), c.at("transfer_length").as_int(), "length of " + name);
        check::equal(world_handle(c.at("document")), c.at("handle").as_string(), "handle of " + name);
        handles.insert(c.at("handle").as_string());
    }
    check::equal(handles.size(), fixture.at("cases").size(), "the pinned handles are distinct");
    check::equal(fixture.at("world").at("handle").as_string(), std::string("w_b38f2e3e7a94c790"),
                 "the reference world's handle is recorded (its bytes are the reference's world, not ours)");
}

TEST(handle_refuses_nan_infinity_and_non_documents) {
    for (const char* doc : {R"({"a": NaN})", R"({"a": Infinity})", R"({"a": [-Infinity]})"}) {
        check::throws([&] { transfer_bytes(parse_json(doc)); }, std::string("refuses ") + doc);
    }
    for (const char* doc : {"[]", "\"world\"", "3", "null"}) {
        const PatchRefusal r = product_test::refusal_of([&] { transfer_document(parse_json(doc)); });
        check::equal(std::string(r.kind), std::string("wrong_type"), std::string("a non-object ") + doc);
    }
}

TEST(handle_ignores_key_order_timings_and_top_level_build_stages) {
    check::equal(world_handle(parse_json(R"({"z": 1, "a": {"y": 2, "b": 3}})")),
                 world_handle(parse_json(R"({"a": {"b": 3, "y": 2}, "z": 1})")), "key order");
    check::equal(dump_json(transfer_document(parse_json(
                     R"({"a": 1, "timing_ms": {"total": 1.0}, "b": {"timing_ms": 2, "c": [{"timing_ms": 3, "d": 4}]}})"))),
                 std::string(R"({"a": 1, "b": {"c": [{"d": 4}]}})"), "timing_ms at every depth, inside arrays too");
    check::equal(dump_json(transfer_document(parse_json(R"({"build_stages": [1], "a": {"build_stages": [2]}})"))),
                 std::string(R"({"a": {"build_stages": [2]}})"), "build_stages only at the top");
}

TEST(handle_shape_check) {
    check::is_true(is_handle(world_handle(parse_json("{}"))), "a handle is a handle");
    for (const char* bad : {"", "w_", "w_nothex0123456", "w_44136fa355b3678ab", "44136fa355b3678a", "W_44136fa355b3678a"}) {
        check::is_true(!is_handle(std::string_view(bad)), std::string("not a handle: ") + bad);
    }
    check::is_true(!is_handle(JsonValue(3)) && !is_handle(JsonValue()), "not a string");
}

TEST(handle_of_a_generated_world_is_a_function_of_the_world_not_the_run) {
    Request request;
    request.size = 17;
    request.phase = 16;
    const World world = generate(resolve(request));
    std::ostringstream timed;
    write_world_json(timed, world, /*pretty=*/true, /*include_timings=*/true);
    const JsonValue with_timings = parse_json(timed.str());
    check::is_true(with_timings.contains("timing_ms"), "the premise: the timed document carries timings");
    const std::string name = world_handle(world);
    check::is_true(is_handle(name), "shaped like a handle");
    check::equal(world_handle(with_timings), name, "pretty and timed, the same name");
    JsonValue touched = with_timings;
    touched.at("config").set("seed", 43);
    check::is_true(world_handle(touched) != name, "a changed world gets a different name");
}

TEST(handle_generator_staleness_is_a_retired_version_refusal) {
    check_generator(parse_json(R"({"generator_version": 16})"), JsonValue(16));
    check_generator(parse_json(R"({"generator_version": 16.0})"), JsonValue(16));  // the reference's ==
    const PatchRefusal r = product_test::refusal_of([] { check_generator(parse_json(R"({"generator_version": 15})"), JsonValue(16)); });
    check::equal(std::string(r.code), std::string("UNSUPPORTED_VERSION"), "code");
    check::equal(r.field, std::string("generator_version"), "field");
    check::equal(r.received_json, std::string("15"), "received");
    check::is_true(!r.retry, "a stale world is not retryable with a value");
    check::throws([] { check_generator(parse_json("{}"), JsonValue(16)); }, "no version is refused, not assumed");
}

TEST(handle_changed_paths_are_dotted_to_two_levels_in_the_transfer_form) {
    check::is_true(changed_paths(parse_json(R"({"a": {"b": 1}})"), parse_json(R"({"a": {"b": 1.0}})")).empty(),
                   "identical worlds (1 == 1.0) report nothing");
    check::is_true(changed_paths(parse_json(R"({"settlements": {"sites": [1], "founding": {"events": []}}, "layers": {"h": 1}})"),
                                 parse_json(R"({"settlements": {"sites": [1, 2], "founding": {"events": []}}, "layers": {"h": 1}})")) ==
                       std::vector<std::string>{"settlements.sites"},
                   "two levels");
    check::is_true(changed_paths(parse_json(R"({"a": 1})"), parse_json(R"({"b": 2})")) == std::vector<std::string>{"a", "b"},
                   "added and removed");
    check::is_true(changed_paths(parse_json(R"({"a": {"b": {"c": 1}}})"), parse_json(R"({"a": {"b": {"c": 2}}})")) ==
                       std::vector<std::string>{"a.b"},
                   "deeper change at the second level");
    check::is_true(changed_paths(parse_json(R"({"a": 1, "timing_ms": {"total": 1.0}, "build_stages": [1]})"),
                                 parse_json(R"({"a": 1, "timing_ms": {"total": 9.5}, "build_stages": [2]})"))
                       .empty(),
                   "what the handle ignores is never a change");
}

TEST(handle_envelopes_name_the_version_its_parent_and_what_moved) {
    const JsonValue before = world_doc("[]", R"({"settlements": {"sites": [1]}})");
    const JsonValue after = world_doc(R"([{"kind": "founding", "uid": "c-9"}])",
                                      R"({"settlements": {"sites": [1, 2]}, "settlement_founding": {"notes": ["placed"]}})");
    const JsonValue e = operation_envelope("found-settlement", after, &before);
    check::equal(e.at("schema").as_string(), std::string("fantasy-world-generator.operation"), "schema");
    check::equal(e.at("schema_version").as_int(), std::int64_t{1}, "schema version");
    check::equal(e.at("handle").as_string(), world_handle(after), "handle");
    check::equal(e.at("parent").as_string(), world_handle(before), "parent");
    check::equal(e.at("generator_version").as_int(), std::int64_t{16}, "generator version");
    const auto changed = strings(e.at("changed"));
    check::is_true(std::find(changed.begin(), changed.end(), "settlements.sites") != changed.end(), "what moved");
    check::equal(dump_json(e.at("operation_record")), std::string(R"({"kind": "founding", "uid": "c-9"})"), "the record");
    check::equal(dump_json(e.at("report")), std::string(R"({"notes": ["placed"]})"), "the verb's own report");
    std::vector<std::string> keys;
    for (const auto& m : e.as_object()) keys.push_back(m.first);
    check::is_true(keys == std::vector<std::string>{"schema", "schema_version", "operation", "handle", "parent",
                                                    "generator_version", "changed", "operation_record", "report"},
                   "the schema's nine keys, in order");

    const JsonValue plain = world_doc("[]");
    const JsonValue departed = world_doc(R"([{"kind": "departure"}])");
    check::is_true(operation_envelope("depart", departed, &plain).at("report").is_null(), "a verb with no report block");
    const JsonValue root = operation_envelope("generate", plain);
    check::is_true(root.at("parent").is_null() && root.at("changed").size() == 0, "a root has no parent");
    const JsonValue big = world_doc(R"([{"kind": "age"}])", R"({"layers": {"height": [["sentinel-27182818", 2]]}})");
    check::is_true(dump_json(operation_envelope("advance-age", big, &plain)).find("sentinel-27182818") == std::string::npos,
                   "the envelope names blocks without quoting them");
    check::equal(version_operations().size(), std::size_t{8}, "eight version verbs");
}

TEST(handle_envelope_misuse_is_refused_by_name) {
    const JsonValue plain = world_doc("[]");
    check::equal(std::string(product_test::refusal_of([&] { operation_envelope("moon", plain, &plain); }).kind),
                 std::string("invalid_choice"), "a read produces no version");
    check::equal(std::string(product_test::refusal_of([&] { operation_envelope("patch", plain, &plain); }).kind),
                 std::string("invalid_choice"), "a read produces no version");
    check::equal(std::string(product_test::refusal_of([&] { operation_envelope("generate", plain, &plain); }).kind),
                 std::string("cross_field"), "a root has no parent");
    check::equal(std::string(product_test::refusal_of([&] { operation_envelope("advance-age", plain); }).kind),
                 std::string("cross_field"), "a transform needs its input");
    const PatchRefusal unknown = product_test::refusal_of([&] { operation_envelope("summon-god", plain, &plain); });
    check::equal(unknown.field, std::string("operation"), "not a verb");
    check::is_true(!unknown.candidates_json.empty(), "a misspelt verb gets suggestions");
}
