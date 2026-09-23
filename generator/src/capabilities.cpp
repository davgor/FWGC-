#include "fwg/capabilities.hpp"

#include <algorithm>
#include <set>

#include "fwg/product_content.hpp"
#include "fwg/request_json.hpp"
#include "fwg/sha256.hpp"

namespace fwg {
namespace {

const JsonValue& descriptor() {
    static const JsonValue parsed = parse_json(capabilities_source_json());
    return parsed;
}

const JsonValue& catalogue() {
    static const JsonValue parsed = parse_json(world_controls_json());
    return parsed;
}

// ASCII case folding. Every control name, group and description is ASCII, so this is
// exact on the haystack; a non-ASCII query is folded in its ASCII letters only.
std::string fold(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::string text_of(const JsonValue* value) {
    // str(v.get(key, '')) on a JSON scalar; every catalogue value read this way is a string.
    if (value == nullptr) return {};
    if (value->is_string()) return value->as_string();
    return dump_json(*value);
}

void check_capability_version(const JsonValue& version) {
    if (!version.is_integer()) {
        throw PatchError(refusal::wrong_type("capability_version", version, "integer"));
    }
    if (!version.is_int64() || version.as_int() != kCapabilityVersion) {
        throw PatchError(refusal::unsupported_api("capability_version", version, {JsonValue(kCapabilityVersion)}));
    }
}

void check_controls_version(int version) {
    if (version != kControlsVersion) {
        throw PatchError(refusal::unsupported_api("version", JsonValue(version), {JsonValue(kControlsVersion)}));
    }
}

}  // namespace

JsonValue capabilities_document(int version) { return capabilities_document(JsonValue(version)); }

JsonValue capabilities_document(const JsonValue& version) {
    check_capability_version(version);
    return descriptor();
}

JsonValue require_capabilities(const JsonValue& request) {
    if (!request.is_object() || request.size() != 2 || !request.contains("capability_version") ||
        !request.contains("requires")) {
        throw PatchError(refusal::cross_field(
            "A capability request is an object with exactly capability_version and requires.",
            {"capability_version", "requires"}));
    }
    JsonValue document = capabilities_document(request.at("capability_version"));
    const JsonValue& required = request.at("requires");
    if (!required.is_object()) throw PatchError(refusal::wrong_type("requires", required, "object"));
    const JsonValue& supported = document.at("supported");
    const JsonValue& unsupported = document.at("unsupported");
    for (const auto& [name, version] : required.as_object()) {
        const JsonValue* versions = supported.find(name);
        if (versions == nullptr) {
            const auto& listed = unsupported.as_array();
            const bool known_unavailable =
                std::any_of(listed.begin(), listed.end(), [&](const JsonValue& v) { return v.as_string() == name; });
            if (known_unavailable) {
                PatchRefusal r = refusal::unsupported_api("requires." + name, version, {});
                r.message = "Capability " + name + " is not available from this producer.";
                throw PatchError(std::move(r));
            }
            std::vector<std::string> names;
            for (const auto& member : supported.as_object()) names.push_back(member.first);
            PatchRefusal r = refusal::unknown_field(name, names, "capability");
            r.field = "requires." + name;
            throw PatchError(std::move(r));
        }
        if (!version.is_integer()) throw PatchError(refusal::wrong_type("requires." + name, version, "integer"));
        const auto& listed = versions->as_array();
        const bool ok = std::any_of(listed.begin(), listed.end(), [&](const JsonValue& v) { return python_equal(v, version); });
        if (!ok) {
            PatchRefusal r = refusal::unsupported_api("requires." + name, version, listed);
            r.message = "unsupported capability version: " + name + " " + dump_json(version) + "; this producer supports " +
                        dump_json(*versions) + ".";
            throw PatchError(std::move(r));
        }
    }
    return document;
}

JsonValue controls_document(int version) {
    check_controls_version(version);
    const JsonValue& body = catalogue();
    JsonDumpOptions hashed;
    hashed.ensure_ascii = true;
    hashed.sort_keys = true;
    hashed.indent = 1;
    JsonValue document = JsonValue::object();
    document.set("content_sha256", sha256_hex(dump_json(body.at("controls"), hashed)));
    document.set("controls", body.at("controls"));
    document.set("recipe_version", body.at("recipe_version"));
    document.set("schema", body.at("schema"));
    document.set("version", body.at("version"));
    return document;
}

JsonValue describe_controls(const ControlQuery& query, int version) {
    check_controls_version(version);
    const JsonValue& all = catalogue().at("controls");
    std::vector<const JsonValue::Member*> selected;
    for (const JsonValue::Member& member : all.as_object()) selected.push_back(&member);

    if (query.names) {
        std::set<std::string> unknown;
        for (const std::string& name : *query.names) {
            if (all.find(name) == nullptr) unknown.insert(name);
        }
        if (!unknown.empty()) {
            PatchRefusal r = refusal::unknown_field(*unknown.begin(), control_names());
            std::string listed;
            for (const std::string& name : unknown) listed += (listed.empty() ? "" : ", ") + name;
            r.message = "unknown control: " + listed + ". " + r.message;
            throw PatchError(std::move(r));
        }
        const std::set<std::string> wanted(query.names->begin(), query.names->end());
        std::erase_if(selected, [&](const JsonValue::Member* m) { return wanted.count(m->first) == 0; });
    }
    if (query.group) {
        const std::string wanted = fold(*query.group);
        std::erase_if(selected, [&](const JsonValue::Member* m) { return fold(text_of(m->second.find("group"))) != wanted; });
    }
    if (query.query) {
        const std::string needle = fold(*query.query);
        std::erase_if(selected, [&](const JsonValue::Member* m) {
            return fold(m->first).find(needle) == std::string::npos &&
                   fold(text_of(m->second.find("description"))).find(needle) == std::string::npos;
        });
    }
    if (!query.include_pinned) {
        std::erase_if(selected, [](const JsonValue::Member* m) {
            const JsonValue* status = m->second.find("status");
            return status == nullptr || !status->is_string() || status->as_string() != "open";
        });
    }
    std::sort(selected.begin(), selected.end(),
              [](const JsonValue::Member* a, const JsonValue::Member* b) { return python_less(a->first, b->first); });

    std::set<std::string> groups;
    for (const JsonValue::Member& member : all.as_object()) groups.insert(text_of(member.second.find("group")));

    JsonValue result = JsonValue::object();
    const JsonValue& body = catalogue();
    result.set("schema", body.at("schema"));
    result.set("version", body.at("version"));
    result.set("recipe_version", body.at("recipe_version"));
    JsonValue group_list = JsonValue::array();
    for (const std::string& group : groups) group_list.push_back(group);  // std::set: code-point order
    result.set("groups", std::move(group_list));
    result.set("total", static_cast<long long>(all.size()));
    result.set("returned", static_cast<long long>(selected.size()));
    JsonValue controls = JsonValue::object();
    for (const JsonValue::Member* m : selected) controls.set(m->first, m->second);
    result.set("controls", std::move(controls));
    return result;
}

const JsonValue* control_definition(std::string_view name) { return catalogue().at("controls").find(name); }

const std::vector<std::string>& control_names() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> out;
        for (const JsonValue::Member& member : catalogue().at("controls").as_object()) out.push_back(member.first);
        std::sort(out.begin(), out.end());
        return out;
    }();
    return names;
}

}  // namespace fwg
