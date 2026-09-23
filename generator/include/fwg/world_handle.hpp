// Content-addressed world handles, and the envelope a host returns for one operation.
//
// A port of the reference's fantasy_world_generator.world_handle.
//
// A handle names one immutable version of a world. It is "w_" followed by the first 16 hex
// digits of sha256 over the TRANSFER BYTES: the world document with its top-level
// `build_stages` and every `timing_ms` (at any depth, inside arrays too) removed, written as
// CPython's json.dumps(doc, ensure_ascii=False, sort_keys=True, separators=(",", ":"),
// allow_nan=False) and encoded as UTF-8. That form is the canonical form and the wire form
// at once, so a host that digests what it received and a producer that digests what it sent
// agree by construction. See fwg/json_value.hpp for exactly how floats and strings are
// spelled; Fixtures/world-handle-v1.json in the reference pins the bytes and
// tests/test_world_handle.cpp carries its cases.
//
// Dropping profiling residue is what makes a handle a function of the WORLD rather than of
// the run: a world generated with timings and the same world generated without them carry
// the same name. A NaN or an infinity is refused (JsonError naming its path), because NaN
// is not JSON and a world carrying one must fail at the boundary rather than travel.
//
// Nothing here holds state or stores a world. The store belongs to the host; this module
// says what a version is called, which bytes the name is taken over, and what one
// operation reports. Refusals of a request are PatchError.
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/json_value.hpp"

namespace fwg {

struct World;

inline constexpr int kStoreContractVersion = 1;
inline constexpr std::string_view kHandlePrefix = "w_";
inline constexpr std::size_t kHandleDigits = 16;
inline constexpr std::string_view kEnvelopeSchema = "fantasy-world-generator.operation";

// root starts a lineage, version produces a child of the world it was given, read answers
// without producing a version at all.
enum class OperationKind { Root, Version, Read };

struct OperationSpec {
    std::string_view name;
    OperationKind kind;
    // The top-level key the verb stamps its own report into; empty for the verbs that write
    // none, which is why the envelope leans on the operations record instead.
    std::string_view report;
};

// Every verb a host can be asked for, in the reference's order.
std::span<const OperationSpec> world_operations();
// nullptr for an unknown verb.
const OperationSpec* world_operation(std::string_view name);
// The verbs that produce a new version, in the order a host should list them.
std::vector<std::string_view> version_operations();

// The world as it crosses to a host. Refuses a non-object (PatchError, wrong_type).
JsonValue transfer_document(const JsonValue& document);
// The exact bytes a handle is taken over, and the exact bytes to hand a host.
std::string transfer_bytes(const JsonValue& document);
std::string world_handle(const JsonValue& document);
// A generated world's handle: its document, as write_world_json writes it without
// timings, read back and named. The same name a host computes from the bytes it received.
std::string world_handle(const World& world);

// Whether a value is shaped like a handle. Says nothing about a world existing.
bool is_handle(std::string_view value);
bool is_handle(const JsonValue& value);
inline bool is_handle(const std::string& value) { return is_handle(std::string_view(value)); }
inline bool is_handle(const char* value) { return is_handle(std::string_view(value)); }

// Refuses a world this generator will not read: `generator_version` absent or not equal
// (by the reference's ==) to `running`. The refusal is retired_version -- code
// UNSUPPORTED_VERSION, retry false -- because the remedy is to regenerate, not to resend.
void check_generator(const JsonValue& document, const JsonValue& running);

// Dotted paths, to `depth` levels (at least 1), whose contents differ between two versions,
// in sorted order, compared in the transfer form: a path the handle ignores is never a
// change.
std::vector<std::string> changed_paths(const JsonValue& before, const JsonValue& after, int depth = 2);

// The final `history.operations` entry, the one per-call record every version verb writes;
// null when there is none.
JsonValue last_operation(const JsonValue& document);

// What a host returns for one call: a name, its parent, what moved, and the record. Never
// the world. `before` is the world the call was given -- required for a version verb,
// refused for `generate`; a read verb has no envelope at all. Refusals are PatchError.
JsonValue operation_envelope(std::string_view operation, const JsonValue& after, const JsonValue* before = nullptr);

}  // namespace fwg
