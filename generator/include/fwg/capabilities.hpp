// What this producer supports, and what a caller may ask of it.
//
// A port of the reference's fantasy_world_generator.capabilities: the capability
// descriptor and its negotiation, and the control catalogue with the filters that make it
// affordable to ask about.
//
// THE DESCRIPTOR describes THIS producer. Its structure, its negotiation rules and its
// `coordinates` and `kernel` sections are the reference's contract, unchanged; its
// supported inventory tracks what this library actually publishes, which is not what the
// reference published. data/capabilities.json names every difference and why, and the
// codegen refuses an undocumented one. Negotiation validates nothing about a world: a
// consumer still validates the document it receives.
//
// THE CONTROL CATALOGUE is every knob a request may set -- 209 of them -- with its bounds,
// units, group and prose, and three derived facts: `status` (open | pinned | inert),
// `derived_from`/`overrides` for what the world's physical width computes, and
// `producers`. The whole catalogue is about nine thousand tokens, which is why
// describe_controls filters, and why by default it returns only the controls a caller can
// actually use (status open).
//
// Refusals are PatchError (see fwg/request_json.hpp for the vocabulary and the wire form).
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/json_value.hpp"

namespace fwg {

inline constexpr int kCapabilityVersion = 1;
inline constexpr int kControlsVersion = 1;

// A fresh, caller-owned descriptor. Refuses (unsupported_api) any version but 1; the
// JsonValue overload is the reference's `type(version) is not int` check, so True, 1.0 and
// "1" are refused too (wrong_type).
JsonValue capabilities_document(int version = kCapabilityVersion);
JsonValue capabilities_document(const JsonValue& version);

// {"capability_version": 1, "requires": {id: integer version, ...}}, exactly. Returns the
// descriptor when every requirement is supported; an empty `requires` is allowed. Refuses
// an unknown id (with suggestions), an id this producer lists as unsupported, a version it
// does not support, and any malformed shape or type.
JsonValue require_capabilities(const JsonValue& request);

// The whole catalogue: {content_sha256, controls, recipe_version, schema, version}, keys
// sorted as the reference's file is. content_sha256 is sha256 over
// json.dumps(controls, indent=1, sort_keys=True), computed here rather than stored.
JsonValue controls_document(int version = kControlsVersion);

struct ControlQuery {
    std::optional<std::string> group;               // case-insensitive, exact
    std::optional<std::vector<std::string>> names;  // every name must exist
    std::optional<std::string> query;               // case-insensitive substring of name or description
    bool include_pinned = false;                    // also pinned and inert controls
};

// {schema, version, recipe_version, groups, total, returned, controls}: the controls the
// query selects, sorted by name. `groups` and `total` always describe the whole catalogue.
// An unknown name is refused (unknown_field, naming the nearest controls).
JsonValue describe_controls(const ControlQuery& query = {}, int version = kControlsVersion);

// One control's catalogue row, or nullptr. The row lives for the whole process.
const JsonValue* control_definition(std::string_view name);

// Every control name, sorted.
const std::vector<std::string>& control_names();

}  // namespace fwg
