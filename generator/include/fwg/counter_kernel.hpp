// The bounded counter kernel: kernel contract v1, kernel numeric v1 and the counter proof.
//
// A port of the reference's fantasy_world_generator.kernel_contract, .kernel_numeric and
// .counter_kernel. It is a headless proof of a durable transition, not a resource economy:
// a host submits accepted events, the kernel prepares at most `budget` effects against an
// immutable snapshot, and the host commits the result atomically. Nothing here does I/O,
// holds state between calls or reads a clock.
//
// EVERYTHING HERE IS EXACT, because every output is a byte form another implementation
// must reproduce:
//
//   * canonical bytes are CPython's json.dumps(value, ensure_ascii=True, sort_keys=True,
//     separators=(",", ":"), allow_nan=False). The value domain is null, booleans,
//     integers within +/-(2^53 - 1), strings of at most 4096 UTF-8 bytes, arrays and
//     string-keyed objects; floats are refused outright, 1.0 and -0.0 included.
//     Depth is at most 32 (root 0) and a document at most 16384 nodes, KEYS COUNTED AS
//     NODES one level below their object -- so an object at depth 32 must be empty.
//   * canonical_loads is the strict inverse: UTF-8 without a BOM, at most 1 MiB, no
//     duplicate keys, no fraction or exponent tokens, no NaN/Infinity, integer tokens of
//     at most 16 digits. Non-canonical whitespace and key order are read and normalized.
//   * random_word is sha256("MathLab/stream/v1\0" + seed(8 bytes BE) + len(stream)(1 byte)
//     + stream + index(8 bytes BE)), first 8 bytes as 16 lowercase hex digits. The label is
//     the frozen v1 domain, not the product name; do not rename it.
//
// FAILURES are KernelError, whose `code` is the stable contract and whose message is
// diagnostic. They are NOT PatchError, deliberately: the kernel's failure envelope is the
// version-1 `fantasy-world-generator.failure` document -- schema, schema_version 1, code,
// message and nothing else -- and the reference keeps it at exactly that shape while the
// request boundary moved to version 2 (terrain_errors.RequestError, which PatchRefusal
// mirrors). Emitting the request vocabulary here would change a published v1 wire shape.
// A KernelError still names its field where the reference's message does (`field()`), so
// a C++ caller is not left parsing prose.
//
// The eleven codes: INVALID_INPUT, UNSUPPORTED_VERSION, UNKNOWN_ID, STALE_REVISION,
// AUTHORITY_MISMATCH, CONFLICTING_EVENT, INTERVAL_CONFLICT, WORK_BUDGET, STATE_CAPACITY,
// NUMERIC_OVERFLOW, INVALID_CANDIDATE.
//
// TYPES. The reference checks `type(v) is int` everywhere, so True, 1.0 and "1" are all
// refused where an integer is expected, and so they are here: every function that takes a
// document takes a JsonValue and checks it the same way, in the same order, raising the
// same code. Where the reference's argument is a Python scalar with a keyword default
// (`version=1`, `index`), the C++ takes a typed int -- a bool or a float is then
// unrepresentable rather than refused, and the range checks remain.
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>

#include "fwg/json_value.hpp"

namespace fwg::kernel {

inline constexpr std::int64_t kMaxSafe = 9007199254740991;  // 2^53 - 1
inline constexpr int kMaxBatch = 64;
inline constexpr int kMaxReceipts = 256;
inline constexpr std::size_t kMaxBytes = 1024 * 1024;
inline constexpr int kMaxDepth = 32;
inline constexpr int kMaxNodes = 16384;
inline constexpr std::size_t kMaxStringBytes = 4096;

inline constexpr std::string_view kStateSchema = "fantasy-world-generator.counter-state";
inline constexpr std::string_view kEventSchema = "fantasy-world-generator.counter-event";
inline constexpr std::string_view kCommandSchema = "fantasy-world-generator.counter-command";
inline constexpr std::string_view kCandidateSchema = "fantasy-world-generator.counter-candidate";
inline constexpr std::string_view kFailureSchema = "fantasy-world-generator.failure";

class KernelError : public std::invalid_argument {
public:
    KernelError(std::string code, const std::string& message, std::string field = {});
    const std::string& code() const noexcept { return code_; }
    // The field the refusal is about, where the reference's message names one; empty
    // otherwise. A C++ addition: it is not part of the wire document.
    const std::string& field() const noexcept { return field_; }
    // {"schema": "fantasy-world-generator.failure", "schema_version": 1, "code", "message"}
    JsonValue document() const;

private:
    std::string code_;
    std::string field_;
};

// --- kernel contract v1 ----------------------------------------------------------------

// `value` must be an integer (not a bool, not a float) in [minimum, maximum]; otherwise
// `code`, naming `name`.
std::int64_t require_integer(const JsonValue& value, std::string_view name, std::int64_t minimum = 0,
                             std::int64_t maximum = kMaxSafe, std::string_view code = "INVALID_INPUT");
// "<kind>:" followed by [a-z0-9][a-z0-9._-]{0,63}. `field` is what the refusal names.
const std::string& require_identifier(const JsonValue& value, std::string_view kind, std::string_view field = {});
// An integer in range, then UNSUPPORTED_VERSION unless it is 1.
void require_version(const JsonValue& value);
// An object with exactly these keys.
void require_exact(const JsonValue& value, std::initializer_list<std::string_view> fields);
// `schema` equal to `schema`, then require_version(schema_version).
void require_header(const JsonValue& value, std::string_view schema);

JsonValue validate_event(const JsonValue& event, std::string_view world_id);
JsonValue validate_command(const JsonValue& command);

// --- kernel numeric v1 -----------------------------------------------------------------

std::string canonical_bytes(const JsonValue& value, int version = 1);
JsonValue canonical_loads(std::string_view raw, int version = 1);
// Lowercase sha256 hex of canonical_bytes.
std::string digest(const JsonValue& value, int version = 1);
// `seed` is 16 lowercase hex digits; `stream` matches [a-z][a-z0-9._-]{0,63}; `index` is
// 0..kMaxSafe.
std::string random_word(std::string_view seed, std::string_view stream, std::int64_t index, int version = 1);
// (word >> 11) / 2^53, in [0, 1).
double unit_float(std::string_view word, int version = 1);

// --- the bounded counter ---------------------------------------------------------------

// A fresh world: time, revision and counter zero, no receipts, nothing pending. Never use
// this to replace an existing occupied world.
JsonValue initialize(std::string_view world_id, std::int64_t authority_epoch = 0);

// Schema, versions, identities, the integer domain, monotonic order, unique identities,
// counter/receipt accounting, pending bounds and capacity. Returns a copy.
JsonValue validate_snapshot(const JsonValue& state);
std::string dump_snapshot(const JsonValue& state);
JsonValue load_snapshot(std::string_view raw);

// What evaluate prepares. Immutable canonical byte envelopes plus the work spent; the host
// commits it or throws it away.
struct Candidate {
    std::string base_state;
    std::string command;
    std::string next_state;
    std::string effects;
    std::int64_t work_used = 0;

    bool operator==(const Candidate&) const = default;
};

// Validates the whole interval first, then prepares at most `budget` effects. Never
// mutates its inputs.
Candidate evaluate(const JsonValue& snapshot, const JsonValue& request);

// Re-evaluates the candidate's captured base and command, refuses altered data, and
// compares the whole current snapshot with the captured base or the exact proposed result.
// Returns the state to store; performs no I/O.
JsonValue commit(const JsonValue& snapshot, const Candidate& candidate);

// The versioned transport envelope, validated by re-evaluation in both directions.
std::string dump_candidate(const Candidate& candidate);
Candidate load_candidate(std::string_view raw);

}  // namespace fwg::kernel
