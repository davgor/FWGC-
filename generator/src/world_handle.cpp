#include "fwg/world_handle.hpp"

#include <algorithm>
#include <set>
#include <sstream>

#include "fwg/patch.hpp"
#include "fwg/request_json.hpp"
#include "fwg/serialize.hpp"
#include "fwg/sha256.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

constexpr OperationSpec kOperations[] = {
    {"generate", OperationKind::Root, ""},
    {"advance-age", OperationKind::Version, ""},
    {"advance-time", OperationKind::Version, "time_advance"},
    {"summon", OperationKind::Version, ""},
    {"depart", OperationKind::Version, ""},
    {"corrupt", OperationKind::Version, "corruption"},
    {"cleanse", OperationKind::Version, "cleanse"},
    {"nomad", OperationKind::Version, ""},
    {"found-settlement", OperationKind::Version, "settlement_founding"},
    {"moon", OperationKind::Read, ""},
    {"patch", OperationKind::Read, ""},
};

// `timing_ms` off at every depth, inside arrays too. terrain_patch emits a scalar and every
// other producer a map, so the key goes whatever it holds.
JsonValue without_timings(const JsonValue& value) {
    if (value.is_object()) {
        JsonValue out = JsonValue::object();
        for (const auto& [key, item] : value.as_object()) {
            if (key != "timing_ms") out.as_object().emplace_back(key, without_timings(item));
        }
        return out;
    }
    if (value.is_array()) {
        JsonValue out = JsonValue::array();
        for (const JsonValue& item : value.as_array()) out.push_back(without_timings(item));
        return out;
    }
    return value;
}

// Python truthiness of a JSON value.
bool truthy(const JsonValue& value) {
    switch (value.type()) {
        case JsonValue::Type::Null: return false;
        case JsonValue::Type::Bool: return value.as_bool();
        case JsonValue::Type::Integer: return !(value.is_int64() && value.as_int() == 0);
        case JsonValue::Type::Float: return value.as_float() != 0.0;
        case JsonValue::Type::String:
        case JsonValue::Type::Array:
        case JsonValue::Type::Object: return value.size() != 0 || (value.is_string() && !value.as_string().empty());
    }
    return false;
}

void require_document(const JsonValue& document, const std::string& field) {
    if (!document.is_object()) {
        PatchRefusal r = refusal::wrong_type(field, JsonValue(std::string(document.type_name())), "object");
        r.message = "a world document is an object, received " + std::string(document.type_name());
        throw PatchError(std::move(r));
    }
}

void diff(const JsonValue& before, const JsonValue& after, const std::string& prefix, int depth,
          std::vector<std::string>& out) {
    std::set<std::string> keys;
    for (const auto& member : before.as_object()) keys.insert(member.first);
    for (const auto& member : after.as_object()) keys.insert(member.first);
    for (const std::string& key : keys) {  // std::set: code-point order, as sorted() of str
        const JsonValue* mine = before.find(key);
        const JsonValue* theirs = after.find(key);
        if (mine != nullptr && theirs != nullptr && python_equal(*mine, *theirs)) continue;
        const std::string path = prefix + key;
        if (depth > 1 && mine != nullptr && theirs != nullptr && mine->is_object() && theirs->is_object()) {
            diff(*mine, *theirs, path + ".", depth - 1, out);
        } else {
            out.push_back(path);
        }
    }
}

}  // namespace

std::span<const OperationSpec> world_operations() { return kOperations; }

const OperationSpec* world_operation(std::string_view name) {
    for (const OperationSpec& spec : kOperations) {
        if (spec.name == name) return &spec;
    }
    return nullptr;
}

std::vector<std::string_view> version_operations() {
    std::vector<std::string_view> out;
    for (const OperationSpec& spec : kOperations) {
        if (spec.kind == OperationKind::Version) out.push_back(spec.name);
    }
    return out;
}

JsonValue transfer_document(const JsonValue& document) {
    require_document(document, "document");
    JsonValue top = JsonValue::object();
    for (const auto& [key, item] : document.as_object()) {
        if (key != "build_stages") top.as_object().emplace_back(key, item);
    }
    return without_timings(top);
}

std::string transfer_bytes(const JsonValue& document) {
    return dump_canonical(transfer_document(document), /*ensure_ascii=*/false);
}

std::string world_handle(const JsonValue& document) {
    return std::string(kHandlePrefix) + sha256_hex(transfer_bytes(document)).substr(0, kHandleDigits);
}

std::string world_handle(const World& world) {
    std::ostringstream text;
    write_world_json(text, world, /*pretty=*/false, /*include_timings=*/false);
    return world_handle(parse_json(text.str()));
}

bool is_handle(std::string_view value) {
    if (value.size() != kHandlePrefix.size() + kHandleDigits || value.substr(0, kHandlePrefix.size()) != kHandlePrefix) {
        return false;
    }
    return std::all_of(value.begin() + static_cast<std::ptrdiff_t>(kHandlePrefix.size()), value.end(),
                       [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool is_handle(const JsonValue& value) { return value.is_string() && is_handle(std::string_view(value.as_string())); }

void check_generator(const JsonValue& document, const JsonValue& running) {
    require_document(document, "document");
    const JsonValue* held = document.find("generator_version");
    const JsonValue value = held != nullptr ? *held : JsonValue();
    if (!python_equal(value, running)) {
        throw PatchError(refusal::retired_version("generator_version", value, {running}));
    }
}

std::vector<std::string> changed_paths(const JsonValue& before, const JsonValue& after, int depth) {
    std::vector<std::string> out;
    diff(transfer_document(before), transfer_document(after), "", std::max(1, depth), out);
    return out;
}

JsonValue last_operation(const JsonValue& document) {
    require_document(document, "document");
    const JsonValue* history = document.find("history");
    if (history == nullptr || !truthy(*history)) return JsonValue();
    if (!history->is_object()) throw PatchError(refusal::wrong_type("history", *history, "object"));
    const JsonValue* operations = history->find("operations");
    if (operations == nullptr || !truthy(*operations)) return JsonValue();
    if (!operations->is_array()) throw PatchError(refusal::wrong_type("history.operations", *operations, "array"));
    return operations->as_array().back();
}

JsonValue operation_envelope(std::string_view operation, const JsonValue& after, const JsonValue* before) {
    const OperationSpec* spec = world_operation(operation);
    if (spec == nullptr) {
        std::vector<std::string> known;
        for (const OperationSpec& s : kOperations) known.emplace_back(s.name);
        std::sort(known.begin(), known.end());
        PatchRefusal r = refusal::invalid_choice("operation", JsonValue(operation), known);
        std::string listed;
        for (const std::string& k : known) listed += (listed.empty() ? "" : ", ") + k;
        r.message = "unknown operation '" + std::string(operation) + "'; known: " + listed;
        throw PatchError(std::move(r));
    }
    if (spec->kind == OperationKind::Read) {
        std::vector<std::string> producing;
        for (const OperationSpec& s : kOperations) {
            if (s.kind != OperationKind::Read) producing.emplace_back(s.name);
        }
        PatchRefusal r = refusal::invalid_choice("operation", JsonValue(operation), producing);
        r.message = "'" + std::string(operation) +
                    "' answers a question and produces no version, so it has no envelope; return its own document";
        throw PatchError(std::move(r));
    }
    if (spec->kind == OperationKind::Root && before != nullptr) {
        throw PatchError(refusal::cross_field("'" + std::string(operation) + "' starts a lineage and has no parent",
                                              {"operation", "before"}));
    }
    if (spec->kind == OperationKind::Version && before == nullptr) {
        throw PatchError(refusal::cross_field("'" + std::string(operation) +
                                                  "' transforms a world, so the world it was given is required to say "
                                                  "what changed",
                                              {"operation", "before"}));
    }

    JsonValue envelope = JsonValue::object();
    envelope.set("schema", std::string(kEnvelopeSchema));
    envelope.set("schema_version", kStoreContractVersion);
    envelope.set("operation", std::string(operation));
    envelope.set("handle", world_handle(after));
    envelope.set("parent", before != nullptr ? JsonValue(world_handle(*before)) : JsonValue());
    const JsonValue* generator = after.find("generator_version");
    envelope.set("generator_version", generator != nullptr ? *generator : JsonValue());
    JsonValue changed = JsonValue::array();
    if (before != nullptr) {
        for (std::string& path : changed_paths(*before, after)) changed.push_back(std::move(path));
    }
    envelope.set("changed", std::move(changed));
    envelope.set("operation_record", last_operation(after));
    const JsonValue* report = spec->report.empty() ? nullptr : after.find(spec->report);
    envelope.set("report", report != nullptr ? *report : JsonValue());
    return envelope;
}

}  // namespace fwg
