// The JSON request boundary: a generate request read from JSON, and a refusal written as
// JSON.
//
// A port of the reference's terrain_world.generate_request (the request half) and
// terrain_errors (the refusal half). The caller is a local model packaged inside the game:
// when a request is refused it decides what to send next with no one to ask, so a refusal
// names the field, echoes the value, states the bound or the vocabulary it broke and, where
// one exists, offers a value that would be accepted.
//
// REFUSALS ARE PatchError / PatchRefusal, the one refusal type this library already has.
// The builders below produce the reference's seven request shapes plus its world shapes,
// and `refusal_document` renders any PatchRefusal -- a patch's included -- as the
// reference's `fantasy-world-generator.failure` version 2 envelope:
//
//   {schema, schema_version: 2, code, message, field}                 detail = false
//   + {received, expected, suggestion, retry}                          detail = true
//
// `detail = false` is the reference's `--terse-errors` tier, kept switchable on purpose:
// whether a suggestion reduces a small model's retries is an empirical question.
//
// WHAT IS NOT BIT-FOR-BIT. The keys and their meaning are the reference's. The message
// prose is this port's (the reference calls it diagnostic). A bound or a clamp value is
// carried as a double, so an integral bound on a NUMBER-typed control prints as "-1.0"
// where the reference's table held the int -1; integer-typed controls print integers.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/config.hpp"
#include "fwg/json_value.hpp"
#include "fwg/patch.hpp"

namespace fwg {

inline constexpr std::string_view kFailureSchema = "fantasy-world-generator.failure";
inline constexpr int kFailureSchemaVersion = 2;

// The reference's terrain_errors constructors. `field` is what the refusal names; the
// received value is kept exactly (its JSON text) so an integer is echoed as an integer.
namespace refusal {

// A value of the wrong kind. `expected_type` is integer | number | string | object | ...
PatchRefusal wrong_type(std::string field, const JsonValue& received, std::string_view expected_type);
// Outside a declared bound, with the nearest value inside it; `min == max` is a PINNED
// control, which refuses every value and suggests nothing.
PatchRefusal out_of_range(std::string field, const JsonValue& received, std::optional<double> min,
                          std::optional<double> max, std::string_view expected_type, std::string_view units);
// Outside a closed vocabulary, with the nearest members as suggestions.
PatchRefusal invalid_choice(std::string field, const JsonValue& received, const std::vector<std::string>& choices);
// A version this producer no longer reads. code UNSUPPORTED_VERSION; retry is false.
PatchRefusal retired_version(std::string field, const JsonValue& received, const std::vector<JsonValue>& supported);
// A protocol version this producer does not speak. code UNSUPPORTED_VERSION; retry false.
PatchRefusal unsupported_api(std::string field, const JsonValue& received, const std::vector<JsonValue>& supported);
// A name outside a vocabulary, with what the caller probably meant. `noun` is "control"
// unless the vocabulary is something else ("request field", "capability", ...).
PatchRefusal unknown_field(std::string name, const std::vector<std::string>& candidates,
                           std::string_view noun = "control");
// A refusal no single field owns.
PatchRefusal cross_field(std::string message, std::vector<std::string_view> fields);
// Past a hard cost ceiling rather than a validation range. code STATE_CAPACITY.
PatchRefusal over_capacity(std::string field, const JsonValue& received, double limit, std::string_view noun);
// A field supplied somewhere it does not belong (`location` says where it does).
PatchRefusal misplaced(std::string field, const JsonValue& received, std::string message);
// A request that is not even the right shape: `received` is its type name.
PatchRefusal malformed(std::string message, const JsonValue& received);

}  // namespace refusal

// CPython's difflib.get_close_matches, over code points: candidates scoring at least
// `cutoff` by SequenceMatcher.ratio(), best first, ties broken by the larger string.
std::vector<std::string> close_matches(std::string_view word, const std::vector<std::string>& possibilities,
                                       std::size_t n = 3, double cutoff = 0.6);

// terrain_errors.nearest: the names a caller plausibly meant, best first -- prefix matches,
// then substring matches, then difflib's -- at most five. Case folding is ASCII-only.
std::vector<std::string> nearest(std::string_view name, std::vector<std::string> candidates);

// The failure-v2 envelope. Every PatchRefusal renders, whatever built it.
JsonValue refusal_document(const PatchRefusal& refusal, bool detail = true);

// The reference's generate request -- {"seed", "recipe_version", "overrides"}, the last
// keyed by published control name -- validated in the reference's order and turned into a
// Request. Throws PatchError for everything the reference refuses at this boundary:
// the body's shape, a retired recipe, the seed's type and range, an unknown control (with
// suggestions), `seed` inside overrides, and each override's type, vocabulary and bounds as
// the control catalogue publishes them. resolve() then applies this producer's own
// validation, which throws std::invalid_argument.
Request request_from_json(const JsonValue& body);

}  // namespace fwg
