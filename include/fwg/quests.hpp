// The quest board: what the world offers, what it withdraws, and the two transitions a
// player makes.
//
// A quest here is **anchor + verb + difficulty 1..5** and nothing else. No loot, no XP, no
// treasure table: `difficulty` is the one number a consuming game prices, and pricing it is
// that game's job. Difficulty is deliberately NOT a field of a quest row -- it belongs to
// the hook the offer was opened from, and a consumer wanting a priced offer joins
// `QuestRecord::quest_id` back to `QuestHook::hook_id`.
//
// The split of labour is the whole design. **The world decides whether an offer still
// stands; it never decides whether a player succeeded.** An outcome the world invented
// would be an outcome the player did not play, so a completion arrives from the game as a
// QuestResolution and the two player-side transitions -- taking an offer and letting one go
// -- arrive as a QuestAction. Everything else on this board is the world withdrawing an
// offer it can no longer make: the giver died, the target is gone, the window closed, the
// hook left, or the age turned.
//
// Ported from `Sim/icarus_sim/terrain_time.py` (the lifecycle) and
// `Sim/icarus_sim/terrain_quest_actions.py` (the transitions). The Python tree is being
// deleted; this is the only copy.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/magic.hpp"

namespace fwg {

inline constexpr int kQuestsVersion = 1;

// An offer does not stand forever: half a year to three years, drawn uniformly. The window
// is drawn once per quest from the step that created it, so it is a function of simulated
// time like everything else here, and a quest created on the same day by two differently
// chunked calls draws the same window. Widening it makes quests linger; narrowing it churns
// the board.
inline constexpr double kQuestOfferMinDays = 180.0;
inline constexpr double kQuestOfferMaxDays = 1080.0;

// At most this many resolutions or actions in one request. One session cannot take a
// thousand quests in an instant, and the two limits are deliberately the same number so a
// caller batching both does not have to learn two.
inline constexpr std::size_t kMaxQuestActions = 1024;
inline constexpr std::size_t kMaxQuestResolutions = 1024;

// Note the state collapse: EVERY world-side closure lands on `Expired` whatever the cause,
// and `QuestClosedReason` is the only discriminator. A consumer that reads only the state
// cannot tell a lapsed offer from a murdered giver.
enum class QuestState : int { Offered = 0, Taken, Expired, Completed, Failed, Abandoned };
std::string_view to_string(QuestState state);

// The published vocabulary. `HookGone` and `AgeTurned` are the pair that is easiest to
// collapse and MUST NOT BE: `HookGone` says this hook left the roster while the world
// around it stood, and `AgeTurned` says the world moved on -- five thousand years passed,
// the cast was regenerated, and everyone who could have offered or populated the quest is
// gone. A consumer that cannot tell those apart cannot tell a player whether their quest
// went away or their age did.
//
// `Abandoned` is written by the action route rather than by a sweep; it is in the
// vocabulary because the vocabulary is the board's.
enum class QuestClosedReason : int {
    Expired = 0, GiverGone, TargetGone, HookGone, AgeTurned, Resolved, Abandoned
};
std::string_view to_string(QuestClosedReason reason);

// What a hook's declared effect points at. Only `City` and `Nest` carry an id this module
// can resolve; everything else reads as present, because **a quest wrongly left open is a
// smaller lie than a quest silently reaped because this module did not recognise its
// target.**
enum class QuestEffectKind : int {
    Unknown = 0, Ruin, Relic, City, Hamlet, Fortress, Port, Shrine, College, Nest, LeyNode
};

struct QuestEffect {
    QuestEffectKind kind = QuestEffectKind::Unknown;
    std::string uid;

    // NULLOPT MEANS ABSENT, and that distinction is load-bearing exactly once: the nest
    // lookup falls back to `uid` only when this key is missing, never when it is present
    // and empty. A malformed hook carrying an empty nest_id therefore matches no nest and
    // closes its quest `TargetGone`, which is the reference's behaviour; treating empty as
    // absent would match by uid and leave it open instead.
    std::optional<std::string> nest_id;

    // The verb a pre-version-2 hook did not carry. Empty is falsy, as the fallback chain
    // that reads it expects.
    std::string action;

    // Ley effects only. A ley effect carries a school and a delta and never an `action`, so
    // every ley quest of a pre-version-2 world reads as a verbless offer -- which is the
    // only way `QuestRecord::verb` can still be null.
    std::string school;
    std::string node_id;
    bool has_intensity_delta = false;
    double intensity_delta = 0.0;
};

// What the hero generator wrote and this module reads. `hook_id` is minted as
// "hook-<giver uid>-<that giver's hook ordinal>", which is the mechanism behind the age-turn
// chimera `quest_turn_age_board` exists to prevent: a regenerated cast re-mints positional
// uids as the same strings, so it re-mints hook ids too.
struct QuestHook {
    std::string hook_id;
    std::string giver_uid;      // empty is the published null
    std::string target;         // empty is falsy and falls back to effect.nest_id
    std::string verb;           // empty is falsy and falls back to effect.action
    std::string stated_purpose;
    bool unwitting = false;
    QuestEffect actual_effect;
};

// `quest_id` is the key field -- not `id`, not `uid` -- and it equals the `hook_id` the
// offer was opened from, so it names ONE age's offer only.
//
// The nullable fields are optional rather than empty-string because the published schema
// types them `string|null` and both cases are reachable: a pre-version-2 ley hook always
// produces a null verb.
struct QuestRecord {
    std::string quest_id;
    std::optional<std::string> giver_uid;
    std::optional<std::string> anchor;
    std::optional<std::string> verb;
    std::optional<std::string> stated_purpose;
    bool unwitting = false;
    QuestState state = QuestState::Offered;
    double offered_day = 0.0;
    double expires_day = 0.0;
    std::optional<double> closed_day;
    std::optional<QuestClosedReason> closed_reason;
};

// A closing reason, an outcome, "taken", or "ley_applied" -- which is the one event that
// carries the extra three fields.
struct QuestLogEntry {
    double day = 0.0;
    std::string quest_id;
    std::string event;
    std::string school;
    std::string node_id;
    std::optional<double> intensity;
};

struct QuestBoard {
    int version = kQuestsVersion;
    // Sorted by `quest_id` as a byte-wise string at the end of every sweep. That order is
    // published: the read surface documents the board as being in it.
    std::vector<QuestRecord> quests;
    // Append order within a sweep is the union-walk order; across sweeps it is
    // chronological. The log outlives the board -- an age turn retires every row but keeps
    // every closure, so why a quest ended survives the board it stood on.
    std::vector<QuestLogEntry> log;
};

// ---------------------------------------------------------------------------
// Who can hold an offer open.
// ---------------------------------------------------------------------------

// Four packages answer one liveness question in three vocabularies (heroes and dreads say
// living/legend, npcs say alive/dead, villains say living/fallen). The translation belongs
// to whoever owns the person lists, so this carries only the ANSWER -- and villains are
// absent on purpose: a villain is never a quest giver.
enum class QuestGiverBlock : int { Heroes = 0, Dreads, Npcs };
std::string_view to_string(QuestGiverBlock block);

struct QuestGiver {
    std::string uid;
    // Liveness is resolved once, with the index, rather than at every sweep. A tick never
    // changes who exists -- the cast moves at an age boundary and nowhere else -- so a long
    // span's daily sweeps would otherwise ask the same question a million times.
    bool present = true;
};

// The three lists a giver can come from, IN THIS ORDER. A block whose package failed
// carries no people at all: zero present people and no package are different facts, and a
// quest whose giver was in a failed block closes as `GiverGone` on the next sweep.
struct QuestCast {
    bool heroes_ok = true;
    std::span<const QuestGiver> heroes;  // heroes.people
    std::span<const QuestGiver> dreads;  // heroes.dreads -- the antagonist cast INSIDE heroes
    bool npcs_ok = true;
    std::span<const QuestGiver> npcs;    // npcs.people
};

// uid -> giver, FIRST WRITER WINS across heroes.people, then heroes.dreads, then npcs.people.
//
// A uid claimed by two of those resolves to the first, silently. That is deliberately the
// opposite policy from the person-state route, which collects every match and refuses a
// contested uid rather than preferring one: widening this resolver would change which
// people the quest lifecycle considers, so the two are held to the same answer by a test
// rather than by a shared function.
class QuestGiverIndex {
public:
    QuestGiverIndex() = default;
    explicit QuestGiverIndex(const QuestCast& cast);

    // Nullptr when nobody holds that uid -- including for a null giver_uid, which is why a
    // hook with no giver closes immediately.
    const QuestGiver* find(const std::optional<std::string>& uid) const;
    std::size_t size() const { return by_uid_.size(); }

private:
    std::map<std::string, QuestGiver> by_uid_;
};

// The ids a hook's target can be resolved against. **Settlements join on `uid` and nests
// join on `id`** -- two different key fields, and swapping them reaps every nest quest
// without an error anywhere.
struct QuestWorldView {
    std::span<const std::string> settlement_uids;
    std::span<const std::string> nest_ids;
};

// Built once per request: a tick adds and removes no city and no nest.
class QuestTargetIndex {
public:
    QuestTargetIndex() = default;
    explicit QuestTargetIndex(const QuestWorldView& view);

    bool present(const QuestEffect& effect) const;

private:
    std::set<std::string> settlements_;
    std::set<std::string> nests_;
};

// ---------------------------------------------------------------------------
// The sweep.
// ---------------------------------------------------------------------------

struct QuestStepRequest {
    // The GENESIS seed. The offer-window stream is keyed on it and on the absolute day
    // index, never on a stepped seed and never on a call ordinal.
    std::uint32_t seed = 42;

    double day = 0.0;
    // The ABSOLUTE day index, floor(day / 1.0), measured from the epoch. Keying on the call
    // instead is the defect this whole subsystem is shaped to prevent: it is invisible
    // until someone chunks a span differently, and by then a saved world has been advanced
    // across it.
    std::int64_t index = 0;

    // heroes.quest_hooks IN LIST ORDER. **That order decides which new quest draws which
    // offer window** -- new quests are created in walk order and each draws one uniform from
    // a single per-day generator -- so this is the single most load-bearing ordering fact in
    // the module. Never hand it over from a container that reorders.
    std::span<const QuestHook> hooks;
    // A failed heroes package offers no hooks at all.
    bool heroes_ok = true;

    const QuestGiverIndex* givers = nullptr;
    const QuestTargetIndex* targets = nullptr;
};

// Open what the world now offers, and close what it no longer does.
//
// Walks the UNION of the hooks and the tracked rows, because a hook the world no longer
// offers still has a quest to close. Walking hooks alone left an open quest for every hook
// that vanished, immortal and growing.
void quest_step(QuestBoard& board, const QuestStepRequest& request);

// Retire the board at an age turn and rebuild it from the new age's hooks.
//
// **It does not depend on the id disappearing, and that is the whole point.** That is how
// reaping used to happen and it was only mostly true: the regenerated cast re-mints
// positional uids -- and therefore hook ids -- as the same strings, so a lookup found a
// DIFFERENT hook wearing the old id and the quest carried on with the old age's anchor,
// verb and purpose, the new age's target check, and a giver naming someone who no longer
// exists. Measured in the reference with all 49 open quests taken and one age advanced: 39
// closed, 10 survived as that chimera.
//
// The previous age's rows are retired WITH it rather than kept beside the new ones, for the
// same reason rather than for tidiness: a retained closed row whose id the new cast re-mints
// could not be told apart from the offer that id now names.
//
// `request.index` is IGNORED and recomputed as floor(day): the quests cadence has a period
// of one day, so the absolute index of the sweep at `day` is floor(day), and the next tick's
// half-open window begins at floor(day) + 1. A caller cannot get that wrong here.
//
// Only a world that ALREADY carries a board should be given one -- an age turn continues a
// lifecycle rather than starting one -- so the caller gates on the board existing. The tick
// does exactly that.
void quest_turn_age_board(QuestBoard& board, const QuestStepRequest& request);

// ---------------------------------------------------------------------------
// What the game reports back.
// ---------------------------------------------------------------------------

enum class QuestOutcome : int { Completed = 0, Failed, Abandoned };
std::string_view to_string(QuestOutcome outcome);

struct QuestResolution {
    std::string quest_id;
    QuestOutcome outcome = QuestOutcome::Completed;
};

// One ley write a completed hook DECLARED for itself. Every other kind of effect is
// recorded and nothing else: inventing a world consequence the hook does not declare would
// be this module deciding play.
struct QuestLeyApplication {
    std::string school;
    std::string node_id;
    double intensity = 0.0;  // already clamped to [0, 4]
};

struct QuestResolutionResult {
    // True only when a node actually moved. Claiming it unconditionally is what sent a
    // magicless world into the network evaluation, which then died indexing a block that
    // was not there.
    bool ley_moved = false;
    std::vector<QuestLeyApplication> applied;
};

// Record what the game says the player did, and apply what the hook already declares.
//
// Throws std::invalid_argument on an unknown quest id and std::domain_error on a quest that
// is already closed -- the id was real and the caller is simply late, which is a refusal by
// the world rather than a malformed argument, and a caller that cannot tell those apart
// keeps correcting a value that was never the problem.
//
// `networks` may be null on a magicless world, in which case a completed ley hook is
// recorded and nothing moves.
QuestResolutionResult quest_apply_resolutions(QuestBoard& board,
                                              std::span<const QuestResolution> resolutions,
                                              double day,
                                              std::span<const QuestHook> hooks,
                                              bool magic_enabled,
                                              std::map<std::string, LeyNetwork>* networks);

// ---------------------------------------------------------------------------
// The two transitions a player makes.
// ---------------------------------------------------------------------------

enum class QuestActionKind : int { Take = 0, Abandon };
std::string_view to_string(QuestActionKind action);

struct QuestAction {
    std::string quest_id;
    QuestActionKind action = QuestActionKind::Take;
    // Nullopt means "whatever the world clock says". A day in the past or the future is
    // accepted as long as it is finite and not negative; it is used verbatim.
    std::optional<double> day;
};

struct QuestTransition {
    QuestRecord quest;
    QuestLogEntry entry;
};

// One action against one quest. Pure: the argument is never mutated, nothing is drawn.
//
// Throws std::domain_error for all three refusals, because every one of them is the world
// refusing a well-formed request: the quest is already closed, already taken, or its offer
// window has lapsed.
//
// The late-take refusal is **not pedantry**. A sweep tests the offer window only while a
// quest is `Offered`, so a take applied after the window closed but before the next sweep
// would make the offer immortal -- it leaves `Offered`, and the expiry branch is never
// reached again.
//
// `alternatives` are the quests still open, named in the refusal so a caller has somewhere
// to go. A batch recomputes them per action rather than once, so a batch that closes three
// quests stops offering them back.
QuestTransition quest_transition(const QuestRecord& quest, QuestActionKind action, double day,
                                 std::span<const std::string> alternatives = {});

// The two states a quest can still be acted on from. The same pair a sweep treats as live
// and a resolution is allowed against; a third opinion about what "still open" means is
// exactly the drift this shares its way out of.
bool quest_is_open(QuestState state);

struct QuestActionReport {
    std::string quest_id;
    QuestActionKind action = QuestActionKind::Take;
    double day = 0.0;
    QuestState state = QuestState::Offered;
};

struct QuestActionResult {
    int api_version = kQuestsVersion;
    double day = 0.0;  // the world clock's day, which an action with no day of its own takes
    std::vector<QuestActionReport> actions;
};

// Apply a batch in CALLER ORDER against a copy of the board. All-or-nothing: a refusal
// anywhere throws and `board` is untouched, because the batch is applied to a private copy
// that is only swapped in once every action has succeeded.
//
// Throws std::invalid_argument on an unknown quest id or a non-finite or negative day,
// std::length_error above kMaxQuestActions, and whatever quest_transition throws.
QuestActionResult quest_apply_actions(QuestBoard& board, std::span<const QuestAction> actions,
                                      double clock_day);

}  // namespace fwg
