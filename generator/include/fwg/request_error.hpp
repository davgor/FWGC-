// The refusal envelope the game-facing request APIs throw: the reference's
// `terrain_errors.RequestError` document, as data.
//
// The caller is a model packaged inside the game. When a request is refused it has to decide
// what to send next with no one to ask, so a refusal carries the field, the value it sent,
// the bound or vocabulary it violated, and where one exists a value that would work -- and
// it says whether the request was malformed (a better value fixes it) or whether the world
// refused a well-formed request (only a different node fixes it). A caller that cannot tell
// those two apart keeps correcting an argument that was never the problem.
//
// WHY A THIRD REFUSAL STYLE, and not PatchRefusal. The shape IS PatchRefusal's -- the same
// field names wherever the two overlap, so a consumer reads both alike -- but PatchRefusal's
// `received` and `suggestion_value` are `std::optional<double>` and it has no candidate list.
// These APIs refuse TEXT (a civilization id, a settlement kind, a band classification) and
// answer from vocabularies: a did-you-mean list for a misspelt civilization, the choice list
// for a kind, the suitability fields a world does carry. That is information the reference's
// envelope carries and PatchRefusal cannot hold, so the value slots here are a small variant
// instead of a double. AgeAdvanceError and its siblings carry only a field and a message and
// are poorer still.
//
// Only the envelope fields are carried; rendering them as the wire document belongs to
// whatever wire layer this library grows, as with PatchRefusal.
#pragma once

#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace fwg {

// A value a refusal names: the reference's JSON scalar. `monostate` is its `None`.
using RefusalValue = std::variant<std::monostate, long long, std::string>;

struct RequestRefusal {
    // INVALID_INPUT for everything except an api_version this build does not speak, which
    // is UNSUPPORTED_VERSION: a different client fixes that, not a different value.
    std::string_view code = "INVALID_INPUT";
    // Which builder made it: wrong_type | out_of_range | invalid_choice | unknown_field |
    // unsupported_api | missing_block | refused_by_world.
    std::string_view kind;
    // The field to change. `world.<block>` for a missing block, whose fix is to generate the
    // world further rather than to change an argument.
    //
    // For unknown_field it is the refused NAME, not the field that carried it: the reference
    // built that refusal for misspelt request keys and reuses it for a misspelt civilization
    // or classification VALUE, so refusing civilization_id "dwarfs" names field "dwarfs".
    // Reproduced, and pinned by a test.
    std::string field;
    RefusalValue received;

    // expected -----------------------------------------------------------------
    std::string_view expected_type;  // integer | string | object; empty where none is stated
    std::optional<long long> min;    // out_of_range
    std::optional<long long> max;
    std::string units;               // out_of_range; empty is the reference's None
    bool pinned = false;             // out_of_range with min == max
    std::vector<RefusalValue> choices;     // invalid_choice (text), unsupported_api (integers)
    bool one_of_catalogue = false;         // unknown_field
    std::vector<std::string> catalogue;    // unknown_field: the guesses, best first
    bool refused_by_world = false;         // the request was fine; the ground was not
    std::string requires_block;            // missing_block

    // suggestion ---------------------------------------------------------------
    std::string_view suggestion = "none";  // clamp | choose | did_you_mean | none
    RefusalValue suggestion_value;         // clamp and choose
    std::vector<RefusalValue> suggestion_candidates;  // choose and did_you_mean
    // False only when no change to the request can help.
    bool retry = true;

    std::string message;
};

// Subclasses std::invalid_argument for the reason the reference's RequestError subclasses
// ValueError: every existing handler of a refused request already catches that.
class RequestError : public std::invalid_argument {
public:
    explicit RequestError(RequestRefusal refusal);
    const RequestRefusal& refusal() const { return refusal_; }
    const std::string& field() const { return refusal_.field; }

private:
    RequestRefusal refusal_;
};

// ---------------------------------------------------------------------------
// The reference's builders, one per refusal kind. Each returns the refusal rather than
// throwing it, so a caller can inspect or throw as it likes.
// ---------------------------------------------------------------------------

// `type` is integer | string | object. The article is the reference's: "an integer",
// "text", and "a finite number" for anything else -- INCLUDING an object, so a missing
// origin reads "origin must be a finite number". Reproduced.
RequestRefusal wrong_type(std::string field, RefusalValue value, std::string_view type);

// A value outside a declared bound, suggesting the nearest value inside it. An absent bound
// prints as "None", as the reference's does: "node must be 0..None grid index".
RequestRefusal out_of_range(std::string field, long long value, std::optional<long long> min,
                            std::optional<long long> max, std::string_view units);

// A value outside a closed vocabulary, listing the vocabulary and suggesting the likeliest.
RequestRefusal invalid_choice(std::string field, const std::string& value, std::span<const std::string> choices);

// A name that is not in a catalogue, with what the caller probably meant. `noun` names the
// catalogue: "civilization", "classification".
RequestRefusal unknown_field(const std::string& name, std::span<const std::string> candidates,
                             std::string_view noun);

// An api_version this build does not implement. Not retryable with another value.
RequestRefusal unsupported_api(std::string field, long long value, std::span<const long long> supported);

// The world handed in does not carry something the call needs. Not retryable.
RequestRefusal missing_block(std::string_view block, std::string message);

// A well-formed request the world will not satisfy where it was asked. `alternatives`, when
// given, become a choose-suggestion of at most five.
RequestRefusal refused_by_world(std::string field, RefusalValue value, std::string reason,
                                std::span<const std::string> alternatives = {});

// Names a caller plausibly meant, best first, at most five: prefix matches, then substring
// matches, then difflib's close matches (ratio >= 0.6), each case-insensitively for the
// first two. A bare prefix is what a model guessing a name produces, and difflib alone
// scores it poorly.
std::vector<std::string> nearest_names(const std::string& name, std::span<const std::string> candidates);

// Python's `difflib.SequenceMatcher(None, a, b).ratio()`: 2 * matched / (len a + len b),
// with the matched count from the recursive longest-common-block decomposition. No junk
// heuristic, which is exact for strings under 200 characters, the only ones asked here.
// Compares bytes, so it agrees with the reference on ASCII and not beyond it.
double sequence_ratio(std::string_view a, std::string_view b);

// Python's `repr` of a string, for messages: single quotes unless the text holds one and no
// double quote. ASCII escapes only.
std::string py_repr(std::string_view text);

}  // namespace fwg
