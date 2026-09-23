// The world clock and the quest board, on a World: the two request APIs a game calls every
// session.
//
// `advance_world_time` is the reference's `advance_time_request`: a world goes in with an
// elapsed span, a new world comes out, the caller keeps it. It adapts the World into the
// pure tick (time.hpp), runs the cadence passes the tick hands back, routes a span of an
// age or more through the age API (ages.hpp) in chunks, turns the quest board at each age,
// and records what it did. `world_quest_request` is the reference's `quest_request`: the
// two transitions a PLAYER makes -- take an offer, let one go -- which the world has no
// business inventing.
//
// **The caller's world is never modified, on any path.** Both take the world by const
// reference and return a new one; a refusal thrown anywhere leaves no half-advanced world,
// and a refusal REPORTED (the work ceiling, the age band's estimate and gate) returns the
// caller's world with only the clock it was measured against added.
//
// **Two kinds of refusal, deliberately.** A malformed request, or a request the world will
// not satisfy, THROWS WorldTimeError naming the field. A request that is well formed but
// would cost more than the caller may have meant -- more than kMaxTickSteps of cadence
// work, or an age advance -- is REPORTED in `report.tick.blocked` with the estimate beside
// it, because a caller that asked for five thousand years deserves the price and the
// reason together, and an exception throws away the more useful half.
//
// Ported from `Sim/icarus_sim/terrain_time.py` (advance_time_request, _advance_ages,
// _instant, clock, _turn_age_board, _apply_resolutions, cost_estimate, age_gate,
// validate_time_world) and `terrain_quest_api.py` (quest_request). The Python tree is being
// deleted; this is the only copy.
#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/magic.hpp"
#include "fwg/quests.hpp"
#include "fwg/time.hpp"
#include "fwg/time_state.hpp"
#include "fwg/world.hpp"

namespace fwg {

// ---------------------------------------------------------------------------
// Refusals.
// ---------------------------------------------------------------------------

// The reference's failure envelope, reduced to what a C++ caller branches on. The codes
// are the envelope's three and are not widened.
inline constexpr std::string_view kRefusalInvalidInput = "INVALID_INPUT";
inline constexpr std::string_view kRefusalUnsupportedVersion = "UNSUPPORTED_VERSION";
inline constexpr std::string_view kRefusalStateCapacity = "STATE_CAPACITY";

class WorldTimeError : public std::runtime_error {
public:
    WorldTimeError(std::string_view code, std::string field, std::string message, std::string received = {},
                   bool retry = true, std::vector<std::string> candidates = {}, bool refused_by_world = false);

    // INVALID_INPUT | UNSUPPORTED_VERSION | STATE_CAPACITY.
    std::string_view code() const { return code_; }
    // What refused. A request field ("elapsed.days", "moon_day", "leyline_edits[].school"),
    // a world block ("world.config", "world.quests"), or -- the reference's own convention
    // for a name that is not in the vocabulary -- the unknown NAME itself: an unknown quest
    // id is refused with `field` set to that id, and the candidates say what exists.
    const std::string& field() const { return field_; }
    // The sentence, without the "field: " prefix what() carries.
    const std::string& message() const { return message_; }
    // The value that was refused, rendered; empty where the reference sent null.
    const std::string& received() const { return received_; }
    // False when sending a different value cannot help: an api_version this build does not
    // speak, or a world that lacks a block it would need regenerating to have.
    bool retry() const { return retry_; }
    // What would work, best first, at most five: the quests still open, or the names
    // nearest an unknown one (prefix matches, then substrings, then fuzzy matches).
    const std::vector<std::string>& candidates() const { return candidates_; }
    // The request was well formed and the WORLD is the constraint -- the quest is real and
    // already closed, the world has no magic to edit. A caller that cannot tell this from a
    // malformed value keeps correcting a value that was never the problem.
    bool refused_by_world() const { return refused_by_world_; }

private:
    std::string code_;
    std::string field_;
    std::string message_;
    std::string received_;
    bool retry_ = true;
    std::vector<std::string> candidates_;
    bool refused_by_world_ = false;
};

// ---------------------------------------------------------------------------
// The clock, the gate and the estimate, read off a World.
// ---------------------------------------------------------------------------

// This world's clock: the one it carries, or -- for a world never advanced -- one derived
// at its founding year, which is the same day the age lottery samples, so adopting a clock
// does not move a world in time. The reference's `clock(world)`.
WorldClock world_clock_of(const World& world);

// Throws WorldTimeError when the world cannot tick at all: `world.config` for a recipe
// other than 3 or a phase below the creatures stage, `world.astrology` for a world with no
// sky. Deliberately NOT the age gate: a world that cannot turn an age can still move its
// encounters and its quests. The quest route asks this same gate, so it can never admit a
// world the tick refuses a moment later.
void validate_time_world(const World& world);

// Why this world cannot advance an age, or nullopt if it can -- the age API's own
// validate_age_world, asked rather than restated.
std::optional<AgeAdvanceError> world_age_gate(const World& world);

// What one age costs on THIS world, when it has been advanced before: the age API's last
// recorded wall time over the ages that call carried. Empty otherwise.
//
// **Keyed on the age operation, not on "the last entry carrying steps or ages".** The
// reference took the latter, which also matches a tick's step count, so on any world
// ticked since its last age advance it divided by up to 200,000 and reported a near-zero
// per-age cost as measured. The pure estimate (time.hpp) takes the pair explicitly for
// exactly that reason, and this is the pair.
AgeCostBasis world_age_cost_basis(const World& world);

// ---------------------------------------------------------------------------
// Advancing time.
// ---------------------------------------------------------------------------

struct TimeAdvanceRequest {
    int api_version = kTimeApiVersion;
    TimeElapsed elapsed;
    // Applied in order before the span runs; at most kMaxLeylineEdits, and only on a world
    // generated with magic. The player-edit contract is magic.hpp's.
    std::vector<LeylineEdit> leyline_edits;
    // What the player did BEFORE this span, applied before it runs; at most
    // kMaxQuestResolutions.
    std::vector<QuestResolution> resolutions;
    // Turns the two reported refusals -- the work ceiling and the age band -- into execution.
    bool commit = false;
    // The day an age turns on, handed to the age API unchanged. Must be >= 0.
    std::optional<long long> moon_day;
};

struct TimeAdvanceReport {
    // Everything the pure tick reports: band, span, steps, cadence counts, committed,
    // resolutions, leylines_evaluated, the reported refusal and the estimates.
    //
    // On a committed age span it describes the WHOLE request: band age, the full elapsed
    // span and `to_day`, `ages`, `age_days`, the age estimate priced before the ages ran.
    TickReport tick;
    // How many player ley edits this call applied (on an age span, the remainder did).
    int leyline_edits = 0;
    // A committed age span only: the age the world is in afterwards.
    std::optional<std::int64_t> age;
    // A committed age span only: the tick that ran the span left over after the whole ages,
    // or ran the request's edits and resolutions in the new age. It is a tick and its own
    // report, and it can itself be BLOCKED -- see advance_world_time.
    std::optional<TickReport> remainder;
    // When the age gate refused: the check that refused ("validate_age_world") and the
    // world block it named ("world.config", "world.religion.catalogue", ...). The refusal
    // itself is `tick.blocked`, in the gate's own words.
    std::string gate;
    std::string gate_field;
};

struct TimeAdvanceResult {
    World world;
    TimeAdvanceReport report;
};

// Advance a world by an elapsed span.
//
// Below one age the span is a TICK: the cadence steps it crosses run in absolute-time
// order, keyed on simulated time and never on the call, so the same span cut differently
// reaches the same world. A span that crosses nothing, with no edits and no resolutions,
// moves the clock and resamples the sky and nothing else, and a span of exactly zero
// records nothing at all.
//
// At or above one age the span is an AGE ADVANCE: reported with its estimate unless
// `commit`, refused (reported) above kMaxAgesPerRequest or when the age gate refuses, and
// otherwise run through the age API in chunks of at most kMaxAgeSteps; every open quest
// closes as `age_turned` and the board is rebuilt from the new age's hooks, and the span
// left over is ticked in the new age together with the request's edits and resolutions.
//
// **Reference defects reproduced here, not fixed**, each pinned by a world_time_reproduced_*
// test: the remainder tick is sent WITHOUT `commit`, so a committed age span whose
// remainder is past the work ceiling advances its ages, reports the remainder blocked, and
// returns a world whose clock stops at the whole ages while `report.tick.to_day` names the
// whole span; the beast movement pass reads the clock as it stood when the call began (and,
// inside an age advance, as it stood before the ages turned); the nomad write-back cadence
// claims `leylines_evaluated` whenever it is scheduled, magicless world or not; and a
// resolution sent with an age span meets the NEW age's board, so an outgoing quest is
// refused as unknown rather than as closed.
//
// Throws WorldTimeError on a malformed request or one the world refuses; the reported
// refusals return normally with `report.tick.committed == false`.
TimeAdvanceResult advance_world_time(const World& world, const TimeAdvanceRequest& request);

// ---------------------------------------------------------------------------
// The quest route.
// ---------------------------------------------------------------------------

struct QuestRequest {
    int api_version = kQuestApiVersion;
    std::vector<QuestAction> actions;  // applied in caller order; at most kMaxQuestActions
};

struct QuestRequestResult {
    World world;
    // `day` is the world clock's, which an action with no day of its own takes; `actions`
    // says what each action did, and its size is what the reference calls `applied`.
    QuestActionResult report;
};

// Take or abandon quests, all or nothing. It does not move the clock, re-evaluate the ley
// field, open new quests, expire old ones or touch any block but the board and the
// operation record: everything a quest's state implies for the world happens on the next
// tick, where the lifecycle already lives. An empty batch changes and records nothing.
//
// Throws WorldTimeError: the request's shape (api_version, actions, quest_id, day), the
// same world gate the tick asks, `world.quests` on a world never ticked (no quests and no
// quest lifecycle are different facts), an unknown quest id (field = that id), and -- as a
// refusal by the world -- a closed quest, a quest already taken, or a take after the offer
// lapsed.
QuestRequestResult world_quest_request(const World& world, const QuestRequest& request);

}  // namespace fwg
