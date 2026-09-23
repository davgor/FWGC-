// Record that a person is gone, or back: the liveness changes a playing session makes.
//
// A game loses a character, or finds one it thought lost. Four blocks record that fact in
// three sets of words -- a hero's or a dread's `living`/`legend`, an NPC's `alive`/`dead`, a
// villain's `living`/`fallen` -- and a caller should not have to know which, nor get the depth
// right: the heroes and NPC blocks carry a block-level ok/failed status under the same name as
// the person-level one.
//
// **The request speaks the predicate's pair and no new word.** `present` and `gone` are the
// liveness module's own published states (liveness.hpp), and `token` turns either into the
// word the record's block uses. The caller names a uid and a state, never a block, and never
// sees `legend`, `dead` or `fallen` unless it reads the report.
//
// **Every write goes through the predicate, never around it.** The current state is read with
// `liveness`, the new word comes from `token`, and the result is read back through `liveness`
// before it is kept. A block cannot be handed in place of a person: the plan overloads are
// deleted for the three block types, as `liveness` itself is.
//
// **A no-op writes nothing.** Asking for the state a record is already in changes nothing,
// records nothing, and is reported with `changed: false`. An absent status reads as present,
// so stamping one in would move the bytes of a document whose reproducibility is a contract.
//
// **What this deliberately does not do.** It writes one field on one record per change and
// nothing else. It does not close the person's quests (the next tick does, reading liveness,
// exactly as it does for a giver the world itself removed), decay a fallen villain's claims,
// add them to villains.fallen, write a tombstone, vacate a roster post, update the roster's
// mirror of a hero, or recount a block's summary -- `heroes.summary.living` and
// `npcs.summary.alive` keep the counts generation wrote, and the census (liveness.hpp) is the
// count that moves. What persists when someone stops existing is an open design question the
// reference left open; the report's `notes` say all of this, because a consumer that learns
// it from behaviour learns it late.
//
// Ported from `Sim/icarus_sim/terrain_person_state.py` (the pure half: shape validation and
// the per-record plan) and `terrain_person_api.py` (`person_state_request`, PERSON_API_VERSION
// 1). The Python tree is being deleted; this is the only copy.
#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/liveness.hpp"
#include "fwg/person_record.hpp"
#include "fwg/read_select.hpp"  // PersonEntry
#include "fwg/world.hpp"
#include "fwg/world_time.hpp"   // WorldTimeError

namespace fwg {

// STATUSES: the two states a change may ask for, the predicate's own pair.
inline constexpr std::array<Liveness, 2> kPersonStatuses{Liveness::Present, Liveness::Gone};

// The same ceiling advance-time puts on resolutions and the quest route on actions: one
// batch, one number to learn.
inline constexpr std::size_t kMaxPersonChanges = 1024;

// A free-text note the caller writes for itself, in CHARACTERS (code points). Bounded because
// it is copied verbatim into history.operations, which is persisted and replayed, and an
// unbounded string there is an unbounded world document.
inline constexpr std::size_t kMaxPersonReason = 200;

// FIELDS: every change carries all four, and a missing one is refused by name in this order.
std::span<const std::string_view> person_change_fields();

// One change as the caller sends it. `status` and `day` are optional so that leaving one out
// is refused, as the reference refuses a change missing either, rather than silently meaning
// "present" or day 0. `uid` and `reason` have no such state: an empty one is refused as the
// wrong value.
struct PersonChange {
    std::string uid;
    std::optional<Liveness> status;
    std::string reason;  // why, for the record: not blank, at most kMaxPersonReason characters
    std::optional<double> day;  // a finite day at or after founding year 0, on the moon calendar
};

// Shape only: no world, no uids, no blocks. Returns the changes normalised, in order.
//
// Throws WorldTimeError, in the reference's order: `changes` above kMaxPersonChanges
// (STATE_CAPACITY); then per change: a missing `status` or `day` (the reference's shape for a
// missing field, an unknown-field refusal whose `field` is the missing name); `uid` empty;
// `status` outside the pair (reachable only through a cast, since the field is typed);
// `reason` blank, whitespace judged as Python's str.strip judges it, Unicode included;
// `reason` longer than kMaxPersonReason (STATE_CAPACITY); `day` not finite; `day` below
// zero.
//
// Two reference defects REPRODUCED, each pinned by a test: a missing field is reported as
// "No person change field named 'day'" and offered itself as the correction; and a negative
// day is refused as a wrong TYPE -- "day must be a finite number; received -1" -- although -1
// is a finite number.
std::vector<PersonChangeEntry> validate_changes(std::span<const PersonChange> changes);

// The plan for one record: the new record, or nullopt when it already reads as `status` and
// nothing needs writing; and its state before and after. Pure: `record` is not modified.
//
// The word written is `token(block, status)` for the record's block; an NPC stores that
// published alive/dead pair as its `alive` flag. The result is read back through `liveness`
// and a disagreement throws std::logic_error -- unreachable while `token` is the inverse of
// `liveness`, and asserted because the two tables live side by side and a block added to one
// and not the other would otherwise write a word that reads back as its opposite.
template <typename Person>
struct PersonStatePlan {
    std::optional<Person> record;
    Liveness before = Liveness::Present;
    Liveness after = Liveness::Present;
};

PersonStatePlan<Hero> person_state_plan(const Hero& record, Liveness status);
PersonStatePlan<HeroDread> person_state_plan(const HeroDread& record, Liveness status);
PersonStatePlan<NpcPerson> person_state_plan(const NpcPerson& record, Liveness status);
PersonStatePlan<Villain> person_state_plan(const Villain& record, Liveness status);

// A block handed where a person belongs does not compile, as liveness() refuses it.
void person_state_plan(const HeroesBlock&, Liveness) = delete;
void person_state_plan(const NpcRoster&, Liveness) = delete;
void person_state_plan(const VillainBlock&, Liveness) = delete;

// uid -> EVERY place that uid names a person, over heroes.people, heroes.dreads, npcs.people
// and villains.people, in that order. A heroes or npcs package that failed is skipped.
//
// A list, not a first-writer-wins entry: a uid claimed by two blocks is DETECTED and refused
// rather than resolved by iteration order. That is deliberately the opposite policy from the
// quest lifecycle's giver index and the read API's person index, which keep the first writer;
// the two are held to the same answer by a test rather than by a shared function, because a
// shared function would have to be the wider one. No generated world has a contested uid;
// that is a fact about the uid minters, not a guarantee any of them makes.
std::map<std::string, std::vector<PersonEntry>> person_state_index(const World& world);

struct PersonStateRequest {
    int api_version = kPersonApiVersion;
    std::vector<PersonChange> changes;  // applied in caller order; at most kMaxPersonChanges
};

// One change as the call treated it.
struct PersonStateChangeReport {
    std::string uid;
    LivenessBlock block = LivenessBlock::Heroes;
    Liveness from_status = Liveness::Present;
    Liveness to_status = Liveness::Present;
    std::string_view token;  // the word the record's block uses for `to_status`
    double day = 0.0;
    std::string reason;
    bool changed = false;
};

// The reference's `person_state` block, returned rather than written into the world: a
// report of the call, not state.
struct PersonStateReport {
    int api_version = kPersonApiVersion;
    int applied = 0;  // the changes that wrote a record
    std::vector<PersonStateChangeReport> changes;  // every change, in order, applied or not
    std::vector<std::string_view> notes;           // what a consumer would otherwise learn late
};

struct PersonStateResult {
    World world;
    PersonStateReport report;
};

// Apply liveness changes and return the new world. Same world and request in, same world
// out: nothing draws and nothing records a wall time. The caller's world is never modified.
//
// Every change is resolved and refused before any record is written, so no path exists on
// which a partly written world was built. A call that applies at least one change appends one
// PersonOperation to history.operations, holding the changes that applied; a call that
// applies none -- an empty batch, or only no-ops -- records nothing. history.replay is left as
// found.
//
// Throws WorldTimeError, in order: api_version; everything validate_changes refuses; the
// time API's own world gate (validate_time_world: `world.config`, `world.astrology`) -- the
// same question the tick asks, because a weaker one here would admit a world the tick
// refuses a moment later; `world.people` when the world carries no person list at all; an
// unknown uid (field = that uid, candidates = the nearest uids); and, as a refusal by the
// world, a uid that names a person in more than one block.
PersonStateResult person_state_request(const World& world, const PersonStateRequest& request);

}  // namespace fwg
