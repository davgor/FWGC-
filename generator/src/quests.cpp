#include "fwg/quests.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "fwg/random.hpp"

namespace fwg {
namespace {

// The two live states, in one place. A sweep, a resolution and an action all ask the same
// question, and a third opinion about what "still open" means is the drift this avoids.
bool is_open(QuestState state) {
    return state == QuestState::Offered || state == QuestState::Taken;
}

// Candidates are truncated to five in the refusal. An unbounded list in a message a small
// model reads is not a suggestion, it is the whole board again.
constexpr std::size_t kMaxAlternatives = 5;

std::string with_alternatives(std::string message, std::span<const std::string> alternatives) {
    if (alternatives.empty()) return message;
    message += " Still open:";
    const std::size_t shown = std::min(alternatives.size(), kMaxAlternatives);
    for (std::size_t i = 0; i < shown; ++i) {
        message += i == 0 ? " " : ", ";
        message += alternatives[i];
    }
    message += '.';
    return message;
}

// The quests still open, in board order. Recomputed per action on purpose.
std::vector<std::string> open_ids(const QuestBoard& board) {
    std::vector<std::string> out;
    for (const QuestRecord& quest : board.quests) {
        if (is_open(quest.state)) out.push_back(quest.quest_id);
    }
    return out;
}

QuestClosedReason reason_of(QuestOutcome outcome) {
    // Every outcome closes as `Resolved`, whichever way it went. A resolution is the game
    // reporting an outcome it played, and that is one fact however it ended; `Abandoned` as
    // a reason is reserved for the party putting the promise down through the action route,
    // so a consumer can tell those apart without re-reading the log.
    (void)outcome;
    return QuestClosedReason::Resolved;
}

}  // namespace

std::string_view to_string(QuestState state) {
    switch (state) {
        case QuestState::Offered: return "offered";
        case QuestState::Taken: return "taken";
        case QuestState::Expired: return "expired";
        case QuestState::Completed: return "completed";
        case QuestState::Failed: return "failed";
        case QuestState::Abandoned: return "abandoned";
    }
    return "offered";
}

std::string_view to_string(QuestClosedReason reason) {
    switch (reason) {
        case QuestClosedReason::Expired: return "expired";
        case QuestClosedReason::GiverGone: return "giver_gone";
        case QuestClosedReason::TargetGone: return "target_gone";
        case QuestClosedReason::HookGone: return "hook_gone";
        case QuestClosedReason::AgeTurned: return "age_turned";
        case QuestClosedReason::Resolved: return "resolved";
        case QuestClosedReason::Abandoned: return "abandoned";
    }
    return "expired";
}

std::string_view to_string(QuestGiverBlock block) {
    switch (block) {
        case QuestGiverBlock::Heroes: return "heroes";
        case QuestGiverBlock::Dreads: return "dreads";
        case QuestGiverBlock::Npcs: return "npcs";
    }
    return "heroes";
}

std::string_view to_string(QuestOutcome outcome) {
    switch (outcome) {
        case QuestOutcome::Completed: return "completed";
        case QuestOutcome::Failed: return "failed";
        case QuestOutcome::Abandoned: return "abandoned";
    }
    return "completed";
}

std::string_view to_string(QuestActionKind action) {
    switch (action) {
        case QuestActionKind::Take: return "take";
        case QuestActionKind::Abandon: return "abandon";
    }
    return "take";
}

bool quest_is_open(QuestState state) { return is_open(state); }

// ---------------------------------------------------------------------------

QuestGiverIndex::QuestGiverIndex(const QuestCast& cast) {
    // heroes.people, then heroes.dreads, then npcs.people, and the FIRST writer of a uid
    // wins. A failed package contributes nobody at all: zero present people and no package
    // are different facts, and a hook whose giver was in one closes as GiverGone rather
    // than reading as alive.
    const auto take = [this](std::span<const QuestGiver> people) {
        for (const QuestGiver& person : people) {
            if (person.uid.empty()) continue;
            by_uid_.emplace(person.uid, person);
        }
    };
    if (cast.heroes_ok) {
        take(cast.heroes);
        take(cast.dreads);
    }
    if (cast.npcs_ok) take(cast.npcs);
}

const QuestGiver* QuestGiverIndex::find(const std::optional<std::string>& uid) const {
    if (!uid.has_value()) return nullptr;
    const auto it = by_uid_.find(*uid);
    return it == by_uid_.end() ? nullptr : &it->second;
}

QuestTargetIndex::QuestTargetIndex(const QuestWorldView& view)
    : settlements_(view.settlement_uids.begin(), view.settlement_uids.end()),
      nests_(view.nest_ids.begin(), view.nest_ids.end()) {}

bool QuestTargetIndex::present(const QuestEffect& effect) const {
    if (effect.kind == QuestEffectKind::City) return settlements_.count(effect.uid) != 0;
    if (effect.kind == QuestEffectKind::Nest) {
        // ABSENT falls back to the uid; present-and-empty does not. A malformed hook
        // carrying an empty nest id matches no nest and closes its quest, which is the
        // reference's answer -- reading empty as absent would match by uid and leave it open.
        const std::string& id = effect.nest_id.has_value() ? *effect.nest_id : effect.uid;
        return nests_.count(id) != 0;
    }
    // Only the kinds with an id this module can resolve are tested. Everything else reads
    // as present: a quest wrongly left open is a smaller lie than a quest silently reaped
    // because this module did not recognise its target.
    return true;
}

// ---------------------------------------------------------------------------

void quest_step(QuestBoard& board, const QuestStepRequest& request) {
    board.version = kQuestsVersion;

    const std::span<const QuestHook> hooks = request.heroes_ok ? request.hooks
                                                               : std::span<const QuestHook>{};

    // by_hook, as a dict comprehension builds one: a key takes the position of its FIRST
    // appearance and the value of its LAST. Two hooks sharing an id is malformed input, but
    // which of them the sweep reads is observable, so it is pinned rather than left to luck.
    std::vector<std::string> walk;
    std::map<std::string, std::size_t> by_hook;
    for (std::size_t i = 0; i < hooks.size(); ++i) {
        const auto [it, fresh] = by_hook.emplace(hooks[i].hook_id, i);
        if (fresh) {
            walk.push_back(hooks[i].hook_id);
        } else {
            it->second = i;
        }
    }

    // Indices rather than pointers: opening a quest appends to the vector, which would
    // invalidate every reference taken before it.
    std::map<std::string, std::size_t> tracked;
    for (std::size_t i = 0; i < board.quests.size(); ++i) {
        tracked.emplace(board.quests[i].quest_id, i);
    }

    // The second half of the union: rows whose hook the world no longer offers, in board
    // order, which after any previous sweep is quest_id order. These never draw, so this
    // half only decides the order HookGone closures reach the log.
    for (const QuestRecord& quest : board.quests) {
        if (by_hook.count(quest.quest_id) == 0) walk.push_back(quest.quest_id);
    }

    // One generator per simulated day, constructed whether or not anything opens, so the
    // stream a sweep owns is decided by the day and never by how the span was cut.
    Rng rng = Rng::child(request.seed, "time-quests", request.index);

    for (const std::string& hook_id : walk) {
        const auto hook_at = by_hook.find(hook_id);
        const QuestHook* hook = hook_at == by_hook.end() ? nullptr : &hooks[hook_at->second];
        auto quest_at = tracked.find(hook_id);

        if (hook == nullptr) {
            if (quest_at != tracked.end()) {
                QuestRecord& quest = board.quests[quest_at->second];
                if (is_open(quest.state)) {
                    quest.state = QuestState::Expired;
                    quest.closed_day = request.day;
                    quest.closed_reason = QuestClosedReason::HookGone;
                    board.log.push_back({request.day, quest.quest_id, "hook_gone", {}, {}, {}});
                }
            }
            continue;
        }

        if (quest_at == tracked.end()) {
            QuestRecord fresh;
            fresh.quest_id = hook->hook_id;
            if (!hook->giver_uid.empty()) fresh.giver_uid = hook->giver_uid;
            // Both fallbacks are for a world generated before the hook answered for itself.
            // A pre-version-2 target was uid-or-node_id only, so a nest hook -- which carries
            // nest_id -- anchored to nothing at all; and a pre-version-2 ley hook had no
            // action on its effect, so its verb read as null however the board was queried.
            if (!hook->target.empty()) {
                fresh.anchor = hook->target;
            } else if (hook->actual_effect.nest_id.has_value() &&
                       !hook->actual_effect.nest_id->empty()) {
                fresh.anchor = *hook->actual_effect.nest_id;
            }
            if (!hook->verb.empty()) {
                fresh.verb = hook->verb;
            } else if (!hook->actual_effect.action.empty()) {
                fresh.verb = hook->actual_effect.action;
            }
            if (!hook->stated_purpose.empty()) fresh.stated_purpose = hook->stated_purpose;
            fresh.unwitting = hook->unwitting;
            fresh.state = QuestState::Offered;
            fresh.offered_day = request.day;
            // Exactly one draw per newly opened quest, in walk order. Reorder the hooks and
            // every quest opened that day gets a different window.
            fresh.expires_day = request.day + rng.uniform(kQuestOfferMinDays, kQuestOfferMaxDays);

            board.quests.push_back(std::move(fresh));
            quest_at = tracked.emplace(hook_id, board.quests.size() - 1).first;
        }

        QuestRecord& quest = board.quests[quest_at->second];
        if (!is_open(quest.state)) continue;

        const QuestGiver* giver =
            request.givers == nullptr ? nullptr : request.givers->find(quest.giver_uid);
        std::optional<QuestClosedReason> reason;
        if (giver == nullptr || !giver->present) {
            reason = QuestClosedReason::GiverGone;
        } else if (request.targets != nullptr && !request.targets->present(hook->actual_effect)) {
            reason = QuestClosedReason::TargetGone;
        } else if (quest.state == QuestState::Offered && request.day > quest.expires_day) {
            // Guarded on `Offered`: a TAKEN quest does not expire on its offer window, which
            // is the whole reason the action route must refuse a late take.
            reason = QuestClosedReason::Expired;
        }
        if (reason.has_value()) {
            quest.state = QuestState::Expired;
            quest.closed_day = request.day;
            quest.closed_reason = reason;
            board.log.push_back(
                {request.day, quest.quest_id, std::string(to_string(*reason)), {}, {}, {}});
        }
    }

    // Byte-wise string order, and published: the read surface documents the board as being
    // in it. Stable, so two rows that somehow share an id keep their relative order.
    std::stable_sort(board.quests.begin(), board.quests.end(),
                     [](const QuestRecord& a, const QuestRecord& b) {
                         return a.quest_id < b.quest_id;
                     });
}

void quest_turn_age_board(QuestBoard& board, const QuestStepRequest& request) {
    for (QuestRecord& quest : board.quests) {
        if (!is_open(quest.state)) continue;
        quest.state = QuestState::Expired;
        quest.closed_day = request.day;
        quest.closed_reason = QuestClosedReason::AgeTurned;
        board.log.push_back({request.day, quest.quest_id, "age_turned", {}, {}, {}});
    }
    // Retired, not kept. A closed row whose id the regenerated cast re-mints could not be
    // told apart from the offer that id now names, and which rows survived would again be
    // decided by an accident of uid minting. The log keeps every closure.
    board.quests.clear();

    QuestStepRequest rebuild = request;
    rebuild.index = static_cast<std::int64_t>(std::floor(request.day));
    quest_step(board, rebuild);
}

// ---------------------------------------------------------------------------

QuestResolutionResult quest_apply_resolutions(QuestBoard& board,
                                              std::span<const QuestResolution> resolutions,
                                              double day,
                                              std::span<const QuestHook> hooks,
                                              bool magic_enabled,
                                              std::map<std::string, LeyNetwork>* networks) {
    QuestResolutionResult result;
    if (resolutions.empty()) return result;
    if (resolutions.size() > kMaxQuestResolutions) {
        throw std::length_error("resolutions: " + std::to_string(resolutions.size()) +
                                " is above the maximum of " +
                                std::to_string(kMaxQuestResolutions) + " resolutions per request.");
    }

    std::map<std::string, std::size_t> by_id;
    for (std::size_t i = 0; i < board.quests.size(); ++i) {
        by_id.emplace(board.quests[i].quest_id, i);
    }
    std::map<std::string, std::size_t> by_hook;
    for (std::size_t i = 0; i < hooks.size(); ++i) by_hook.emplace(hooks[i].hook_id, i);

    for (const QuestResolution& item : resolutions) {  // caller order
        const auto at = by_id.find(item.quest_id);
        if (at == by_id.end()) {
            throw std::invalid_argument("No quest named '" + item.quest_id + "' on this board.");
        }
        QuestRecord& quest = board.quests[at->second];
        if (!is_open(quest.state)) {
            // Refused by the world, not malformed: the id was real and the caller is simply
            // late.
            const std::vector<std::string> alternatives = open_ids(board);
            throw std::domain_error(with_alternatives(
                "Quest " + quest.quest_id + " is already " + std::string(to_string(quest.state)) +
                    " and cannot be resolved again.",
                alternatives));
        }
        quest.state = item.outcome == QuestOutcome::Completed   ? QuestState::Completed
                      : item.outcome == QuestOutcome::Failed    ? QuestState::Failed
                                                                : QuestState::Abandoned;
        quest.closed_day = day;
        quest.closed_reason = reason_of(item.outcome);
        board.log.push_back(
            {day, quest.quest_id, std::string(to_string(item.outcome)), {}, {}, {}});

        if (item.outcome != QuestOutcome::Completed || !magic_enabled || networks == nullptr) {
            continue;
        }
        const auto hook_at = by_hook.find(quest.quest_id);
        if (hook_at == by_hook.end()) continue;
        const QuestEffect& effect = hooks[hook_at->second].actual_effect;
        // A completed hook whose declared effect is a ley change applies it, BECAUSE THE
        // HOOK STATES THE SCHOOL AND THE DELTA ITSELF. Every other kind is recorded and
        // nothing else: inventing a world consequence the hook does not declare would be
        // this module deciding play.
        if (effect.school.empty() || !effect.has_intensity_delta || effect.node_id.empty()) {
            continue;
        }
        const auto net = networks->find(effect.school);
        if (net == networks->end()) continue;
        const auto node = std::find_if(net->second.nodes.begin(), net->second.nodes.end(),
                                       [&](const LeyNode& n) { return n.id == effect.node_id; });
        if (node == net->second.nodes.end()) continue;
        const double target = std::min(4.0, std::max(0.0, node->intensity + effect.intensity_delta));
        node->intensity = target;
        board.log.push_back({day, quest.quest_id, "ley_applied", effect.school, effect.node_id,
                             target});
        result.applied.push_back({effect.school, effect.node_id, target});
        result.ley_moved = true;
    }
    return result;
}

// ---------------------------------------------------------------------------

QuestTransition quest_transition(const QuestRecord& quest, QuestActionKind action, double day,
                                 std::span<const std::string> alternatives) {
    if (!is_open(quest.state)) {
        throw std::domain_error(with_alternatives(
            "Quest " + quest.quest_id + " is already " + std::string(to_string(quest.state)) +
                " and cannot be " + (action == QuestActionKind::Take ? "taken." : "abandoned."),
            alternatives));
    }
    QuestTransition out;
    out.quest = quest;
    out.entry.day = day;
    out.entry.quest_id = quest.quest_id;
    if (action == QuestActionKind::Take) {
        if (quest.state == QuestState::Taken) {
            throw std::domain_error(with_alternatives(
                "Quest " + quest.quest_id + " has already been taken.", alternatives));
        }
        if (day > quest.expires_day) {
            throw std::domain_error(with_alternatives(
                "Quest " + quest.quest_id + " was offered until day " +
                    std::to_string(quest.expires_day) + " and day " + std::to_string(day) +
                    " is past that; the world has already let it lapse.",
                alternatives));
        }
        out.quest.state = QuestState::Taken;
        out.entry.event = "taken";
        return out;
    }
    out.quest.state = QuestState::Abandoned;
    out.quest.closed_day = day;
    // Distinct from the `Resolved` a resolution writes: that is the game reporting an
    // outcome it played, this is the party putting the promise down.
    out.quest.closed_reason = QuestClosedReason::Abandoned;
    out.entry.event = "abandoned";
    return out;
}

QuestActionResult quest_apply_actions(QuestBoard& board, std::span<const QuestAction> actions,
                                      double clock_day) {
    QuestActionResult result;
    result.day = clock_day;
    if (actions.empty()) return result;  // nothing changed, so nothing is recorded
    if (actions.size() > kMaxQuestActions) {
        throw std::length_error("actions: " + std::to_string(actions.size()) +
                                " is above the maximum of " + std::to_string(kMaxQuestActions) +
                                " quest actions per request.");
    }
    for (const QuestAction& item : actions) {
        if (item.quest_id.empty()) {
            throw std::invalid_argument("quest_id: the hook id a quest is keyed on, not empty.");
        }
        if (!item.day.has_value()) continue;
        if (!std::isfinite(*item.day) || *item.day < 0.0) {
            throw std::invalid_argument("day: a finite day at or after founding year 0, got " +
                                        std::to_string(*item.day) + '.');
        }
    }

    // All-or-nothing. Every refusal below throws, and a batch that throws halfway must leave
    // the caller's board exactly as it found it, so the batch is applied to a private copy
    // and swapped in only once every action has succeeded.
    QuestBoard working = board;
    std::map<std::string, std::size_t> by_id;
    for (std::size_t i = 0; i < working.quests.size(); ++i) {
        by_id.emplace(working.quests[i].quest_id, i);
    }

    for (const QuestAction& item : actions) {  // caller order
        const auto at = by_id.find(item.quest_id);
        if (at == by_id.end()) {
            throw std::invalid_argument("No quest named '" + item.quest_id + "' on this board.");
        }
        const double day = item.day.value_or(clock_day);
        // Recomputed per action, so a batch that closes three quests stops offering them back.
        const std::vector<std::string> alternatives = open_ids(working);
        QuestTransition moved =
            quest_transition(working.quests[at->second], item.action, day, alternatives);
        working.quests[at->second] = std::move(moved.quest);
        working.log.push_back(std::move(moved.entry));
        result.actions.push_back(
            {item.quest_id, item.action, day, working.quests[at->second].state});
    }

    board = std::move(working);
    return result;
}

}  // namespace fwg
