// The World-level time and quest APIs: the reference's advance_time_request and
// quest_request, over the pure tick (time.cpp) and the pure board (quests.cpp).
//
// This file is glue. It owns three things the pure layers cannot: turning a World into
// their requests and their answers back into a World, the passes the tick schedules but
// does not run, and the route into the age API. Every decision about WHICH steps a span
// crosses, how a quest opens, closes or moves, and what a span would cost is made by the
// pure layers and only relayed here.
#include "fwg/world_time.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <system_error>
#include <utility>

#include "fwg/ages.hpp"
#include "fwg/complete_stage.hpp"
#include "fwg/humans.hpp"
#include "fwg/liveness.hpp"

namespace fwg {

WorldTimeError::WorldTimeError(std::string_view code, std::string field, std::string message, std::string received,
                               bool retry, std::vector<std::string> candidates, bool refused_by_world)
    : std::runtime_error(field.empty() ? message : field + ": " + message),
      code_(code),
      field_(std::move(field)),
      message_(std::move(message)),
      received_(std::move(received)),
      retry_(retry),
      candidates_(std::move(candidates)),
      refused_by_world_(refused_by_world) {}

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point started) {
    return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

// A number as a refusal renders it: integral values without a fraction, the rest in their
// shortest round-trip form. The reference's %r, near enough for a sentence a model reads.
std::string render(double value) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return value > 0.0 ? "inf" : "-inf";
    std::array<char, 64> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (error != std::errc()) return std::to_string(value);
    return std::string(buffer.data(), end);
}

std::string in_quotes(std::string_view text) { return "'" + std::string(text) + "'"; }

std::string joined(const std::vector<std::string>& names, std::size_t limit) {
    std::string out;
    for (std::size_t i = 0; i < names.size() && i < limit; ++i) {
        if (i != 0) out += ", ";
        out += in_quotes(names[i]);
    }
    return out;
}

// ---------------------------------------------------------------------------
// The refusal envelope's builders, one per shape the reference names (terrain_errors).
// ---------------------------------------------------------------------------

constexpr std::size_t kMaxCandidates = 5;

WorldTimeError over_capacity(const std::string& field, std::size_t value, std::size_t limit, std::string_view noun) {
    return WorldTimeError(kRefusalStateCapacity, field,
                          field + " " + std::to_string(value) + " is above the " + std::string(noun) + " maximum of " +
                              std::to_string(limit) + ".",
                          std::to_string(value));
}

// Not retryable with a different value: the remedy is a different client.
WorldTimeError unsupported_api(int value, int supported) {
    return WorldTimeError(kRefusalUnsupportedVersion, "api_version",
                          "api_version " + std::to_string(value) + " is not implemented; this producer speaks " +
                              std::to_string(supported) + ".",
                          std::to_string(value), /*retry=*/false);
}

// `article` is how the reference words the type wanted: "an integer", "text" or "a finite
// number".
WorldTimeError wrong_type(const std::string& field, const std::string& received, std::string_view article) {
    return WorldTimeError(kRefusalInvalidInput, field,
                          field + " must be " + std::string(article) + "; received " + received + ".", received);
}

// Names the BLOCK rather than a request field, because the fix is to generate or advance
// the world far enough to have it, not to change an argument.
WorldTimeError missing_block(const std::string& block, const std::string& message) {
    return WorldTimeError(kRefusalInvalidInput, "world." + block, message, {}, /*retry=*/false);
}

// A well-formed request the world will not satisfy. The alternatives are truncated to five,
// as the reference truncates them: an unbounded list is not a suggestion, it is the board.
WorldTimeError refused_by_world(const std::string& field, const std::string& received, const std::string& reason,
                                const std::vector<std::string>& alternatives) {
    std::vector<std::string> shown(alternatives.begin(),
                                   alternatives.begin() + static_cast<std::ptrdiff_t>(
                                                              std::min(alternatives.size(), kMaxCandidates)));
    return WorldTimeError(kRefusalInvalidInput, field, reason, received, /*retry=*/true, std::move(shown),
                          /*refused_by_world=*/true);
}

std::string casefold(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Ratcliff/Obershelp: twice the characters the longest-common-block recursion matches, over
// the two lengths -- what difflib's SequenceMatcher.ratio() computes, without the junk
// heuristics it only engages at two hundred characters. The longest block is the leftmost
// in `a`, then in `b`, as difflib picks it.
double similarity(std::string_view a, std::string_view b) {
    const std::size_t total = a.size() + b.size();
    if (total == 0) return 1.0;
    struct Range {
        std::size_t alo, ahi, blo, bhi;
    };
    std::size_t matched = 0;
    std::vector<Range> pending{{0, a.size(), 0, b.size()}};
    std::vector<std::size_t> previous;
    std::vector<std::size_t> current;
    while (!pending.empty()) {
        const Range r = pending.back();
        pending.pop_back();
        std::size_t best = 0;
        std::size_t best_i = r.alo;
        std::size_t best_j = r.blo;
        previous.assign(r.bhi - r.blo + 1, 0);
        current.assign(r.bhi - r.blo + 1, 0);
        for (std::size_t i = r.alo; i < r.ahi; ++i) {
            for (std::size_t j = r.blo; j < r.bhi; ++j) {
                const std::size_t k = j - r.blo + 1;
                current[k] = a[i] == b[j] ? previous[k - 1] + 1 : 0;
                if (current[k] > best) {
                    best = current[k];
                    best_i = i + 1 - best;
                    best_j = j + 1 - best;
                }
            }
            std::swap(previous, current);
        }
        if (best == 0) continue;
        matched += best;
        pending.push_back({best_i + best, r.ahi, best_j + best, r.bhi});
        pending.push_back({r.alo, best_i, r.blo, best_j});
    }
    return 2.0 * static_cast<double>(matched) / static_cast<double>(total);
}

// Names a caller plausibly meant, best first, at most five: prefix matches, then
// substrings, then fuzzy matches. Folded in that order because a fuzzy measure alone is
// good on a typo and poor on a bare prefix.
std::vector<std::string> nearest(std::string_view name, std::vector<std::string> pool) {
    std::sort(pool.begin(), pool.end());
    const std::string lowered = casefold(name);
    std::vector<std::string> prefixed;
    std::vector<std::string> contained;
    for (const std::string& candidate : pool) {
        const std::string folded = casefold(candidate);
        if (folded.rfind(lowered, 0) == 0) {
            prefixed.push_back(candidate);
        } else if (folded.find(lowered) != std::string::npos) {
            contained.push_back(candidate);
        }
    }
    // difflib's get_close_matches: cutoff 0.6, the five best by (score, name) descending.
    std::vector<std::pair<double, std::string>> scored;
    for (const std::string& candidate : pool) {
        const double score = similarity(candidate, name);
        if (score >= 0.6) scored.emplace_back(score, candidate);
    }
    std::sort(scored.begin(), scored.end(), [](const auto& l, const auto& r) {
        return l.first != r.first ? l.first > r.first : l.second > r.second;
    });
    if (scored.size() > kMaxCandidates) scored.resize(kMaxCandidates);

    std::vector<std::string> out = prefixed;
    out.insert(out.end(), contained.begin(), contained.end());
    for (const auto& [score, candidate] : scored) {
        (void)score;
        if (std::find(prefixed.begin(), prefixed.end(), candidate) != prefixed.end()) continue;
        if (std::find(contained.begin(), contained.end(), candidate) != contained.end()) continue;
        out.push_back(candidate);
    }
    if (out.size() > kMaxCandidates) out.resize(kMaxCandidates);
    return out;
}

// A name that is not in the vocabulary. **`field` is the unknown name itself**, which is the
// reference's convention for this refusal (its own tests assert an unknown quest id comes
// back as the field); the candidates say what exists.
WorldTimeError unknown_name(const std::string& name, const std::vector<std::string>& known, std::string_view noun) {
    std::vector<std::string> guesses = nearest(name, known);
    std::string message = "No " + std::string(noun) + " named " + in_quotes(name) + ".";
    if (!guesses.empty()) {
        message += " Did you mean " + joined(guesses, kMaxCandidates) + "?";
    } else if (!known.empty()) {
        std::vector<std::string> sorted = known;
        std::sort(sorted.begin(), sorted.end());
        message += " Known " + std::string(noun) + "s: " + joined(sorted, 8) + ".";
    } else {
        message += " Nothing of that kind exists in this world.";
    }
    return WorldTimeError(kRefusalInvalidInput, name, message, {}, /*retry=*/true, std::move(guesses));
}

std::string_view unit_name(TimeUnit unit) {
    switch (unit) {
        case TimeUnit::Seconds: return "seconds";
        case TimeUnit::Years: return "years";
        case TimeUnit::Days: break;
    }
    return "days";
}

// ---------------------------------------------------------------------------
// The World, as the pure quest board and tick read it.
// ---------------------------------------------------------------------------

QuestEffectKind effect_kind(std::string_view kind) {
    static constexpr std::pair<std::string_view, QuestEffectKind> kKinds[] = {
        {"ruin", QuestEffectKind::Ruin},       {"relic", QuestEffectKind::Relic},
        {"city", QuestEffectKind::City},       {"hamlet", QuestEffectKind::Hamlet},
        {"fortress", QuestEffectKind::Fortress}, {"port", QuestEffectKind::Port},
        {"shrine", QuestEffectKind::Shrine},   {"college", QuestEffectKind::College},
        {"nest", QuestEffectKind::Nest},       {"ley_node", QuestEffectKind::LeyNode},
    };
    for (const auto& [name, value] : kKinds) {
        if (name == kind) return value;
    }
    return QuestEffectKind::Unknown;
}

QuestHook quest_hook_of(const HeroQuestHook& hook) {
    QuestHook out;
    out.hook_id = hook.hook_id;
    out.giver_uid = hook.giver_uid;
    out.target = hook.target;
    out.verb = hook.verb;
    out.stated_purpose = hook.stated_purpose;
    out.unwitting = hook.unwitting;
    const HeroEffect& effect = hook.actual_effect;
    out.actual_effect.kind = effect_kind(effect.kind);
    out.actual_effect.uid = effect.uid;
    // A nest effect is the only one that carries a nest id, and PRESENT-BUT-EMPTY is not
    // absent: the target check falls back to the uid only when the key is missing.
    if (effect.kind == "nest") out.actual_effect.nest_id = effect.nest_id;
    out.actual_effect.action = effect.action;
    out.actual_effect.school = effect.school;
    out.actual_effect.node_id = effect.node_id;
    out.actual_effect.has_intensity_delta = effect.has_delta;
    out.actual_effect.intensity_delta = effect.intensity_delta;
    return out;
}

// Everything the pure sweep and tick read off a world, owning the storage their spans point
// into, so it must outlive every request built from it.
struct QuestInputs {
    // heroes.quest_hooks, ALL of them, IN LIST ORDER: that order decides which new quest
    // draws which offer window. A sweep offers them only when the heroes package is ok; a
    // resolution reads them regardless, as the reference's does.
    std::vector<QuestHook> hooks;
    bool heroes_ok = false;
    bool npcs_ok = false;
    std::vector<QuestGiver> heroes;
    std::vector<QuestGiver> dreads;
    std::vector<QuestGiver> npcs;
    std::vector<std::string> settlement_uids;  // settlements join on uid
    std::vector<std::string> nest_ids;         // nests join on id

    QuestCast cast() const {
        QuestCast out;
        out.heroes_ok = heroes_ok;
        out.heroes = heroes;
        out.dreads = dreads;
        out.npcs_ok = npcs_ok;
        out.npcs = npcs;
        return out;
    }
    QuestWorldView view() const { return QuestWorldView{settlement_uids, nest_ids}; }
};

QuestInputs quest_inputs_of(const World& world) {
    QuestInputs in;
    in.heroes_ok = world.has_heroes && world.heroes.status == "ok";
    in.npcs_ok = world.has_npcs && world.npcs.ok;
    in.hooks.reserve(world.heroes.quest_hooks.size());
    for (const HeroQuestHook& hook : world.heroes.quest_hooks) in.hooks.push_back(quest_hook_of(hook));
    // Liveness resolved once, through the one predicate, in each block's own vocabulary.
    for (const Hero& person : world.heroes.people) in.heroes.push_back({person.uid, is_present(person)});
    for (const HeroDread& dread : world.heroes.dreads) in.dreads.push_back({dread.uid, is_present(dread)});
    for (const NpcPerson& person : world.npcs.people) in.npcs.push_back({person.uid, is_present(person)});
    for (const SettlementSite& site : world.settlements.sites) in.settlement_uids.push_back(site.uid);
    for (const NestSite& nest : world.beast_nests.sites) in.nest_ids.push_back(nest.id);
    return in;
}

QuestStepRequest sweep_of(const World& world, const QuestInputs& in, const QuestGiverIndex& givers,
                          const QuestTargetIndex& targets, double day, std::int64_t index) {
    QuestStepRequest request;
    request.seed = world.config.seed;  // the GENESIS seed, never a stepped one
    request.day = day;
    request.index = index;
    request.hooks = in.hooks;
    request.heroes_ok = in.heroes_ok;
    request.givers = &givers;
    request.targets = &targets;
    return request;
}

TickState tick_state_of(const World& world) {
    TickState state;
    if (world.world_clock) {
        state.has_clock = true;
        state.clock = *world.world_clock;
    }
    if (world.quests) {
        state.has_board = true;
        state.board = *world.quests;
    }
    state.has_magic = world.has_magic;
    if (world.has_magic) state.networks = world.magic.networks;
    if (world.lunar_surge) {
        state.has_surge = true;
        state.surge = *world.lunar_surge;
    }
    state.ages = static_cast<std::int64_t>(world.history.ages.size());
    state.replay = world.history.replay;
    return state;
}

// The genesis config, in design space: the seed every stream is a child of, the size the
// estimates scale by, and the recipe, phase and magic flag the gate reads. Resolutions and
// edits are NOT handed over -- this file applies both before the span, where it can say
// which one refused.
TickRequest tick_request_of(const World& world, const QuestInputs& in, const TimeAdvanceRequest& request) {
    TickRequest out;
    out.elapsed = request.elapsed;
    out.commit = request.commit;
    out.config = world.config;
    out.has_astrology = world.has_astrology;
    out.moon = world.moon;
    // The reference's reported_year: where founding ended, once there were settlements.
    out.has_founding_end_year = world.has_settlements;
    out.founding_end_year = world.settlements.founding.end_year;
    out.hooks = in.hooks;
    out.heroes_ok = in.heroes_ok;
    out.cast = in.cast();
    out.targets = in.view();
    return out;
}

std::vector<std::string> open_ids(const QuestBoard& board) {
    std::vector<std::string> out;
    for (const QuestRecord& quest : board.quests) {
        if (quest_is_open(quest.state)) out.push_back(quest.quest_id);
    }
    return out;
}

std::vector<std::string> ids_of(const QuestBoard& board) {
    std::vector<std::string> out;
    out.reserve(board.quests.size());
    for (const QuestRecord& quest : board.quests) out.push_back(quest.quest_id);
    return out;
}

double checked_elapsed(const TimeElapsed& elapsed) {
    try {
        return elapsed_days(elapsed);
    } catch (const std::invalid_argument&) {
        throw wrong_type("elapsed." + std::string(unit_name(elapsed.unit)), render(elapsed.value), "a finite number");
    }
}

// ---------------------------------------------------------------------------
// What a tick writes beyond the pure slice.
// ---------------------------------------------------------------------------

// Resolutions describe what the player did BEFORE the span, so they are applied before it
// runs: applied at the end, the world could expire a quest during the very span the player
// spent completing it, and then refuse the completion. One at a time, in caller order, so a
// refusal names its own resolution; the pure function applies a batch the same way.
bool apply_resolutions(World& out, const QuestInputs& in, std::span<const QuestResolution> resolutions, double day,
                       bool magic_on) {
    QuestBoard& board = *out.quests;
    std::map<std::string, LeyNetwork>* networks = magic_on ? &out.magic.networks : nullptr;
    bool moved = false;
    for (const QuestResolution& item : resolutions) {
        try {
            const QuestResolutionResult applied = quest_apply_resolutions(
                board, std::span<const QuestResolution>(&item, 1), day, in.hooks, magic_on, networks);
            if (applied.ley_moved) moved = true;
        } catch (const std::invalid_argument&) {
            throw unknown_name(item.quest_id, ids_of(board), "quest");
        } catch (const std::domain_error& refused) {
            // The id was real and the caller is simply late: refused by the world, with the
            // quests still open to resolve.
            throw refused_by_world("quest_id", item.quest_id, refused.what(), open_ids(board));
        }
    }
    return moved;
}

// The seasonal food pass over the hinterland as it stands. The reference refuses without a
// hinterland; so does this.
void run_seasonal_food(World& world) {
    if (!world.has_humans) return;
    SeasonalFoodRequest request;
    request.world = &world;
    request.humans = &world.humans;
    SeasonalFood food = add_seasonal_food(request);
    world.seasonal_food = std::move(food);
}

// One pass the tick scheduled and does not own. The clock is still the one the call began
// with, exactly as the reference's loop runs before it writes the advanced clock.
void run_cadence(World& world, const CadenceCall& call, bool& ley_touched) {
    switch (call.cadence) {
        case Cadence::Quests:
        case Cadence::LeyDrift:
            return;  // the tick ran these itself and never hands them over
        case Cadence::VillainOutlook:
            // The cast does not move; the outlook is re-read. Draws nothing, appends nothing.
            refresh_villain_outlook(world);
            return;
        case Cadence::SeasonalFood:
            run_seasonal_food(world);
            break;
        case Cadence::Nomads:
            run_nomads(world, call.seed);
            break;
        case Cadence::NomadRoutes:
            run_nomad_routes(world, call.seed);
            break;
        case Cadence::BeastMovements:
            run_beast_movements(world, call.seed, beast_movement_year(world));
            break;
        case Cadence::Encounters:
            run_encounters(world);
            break;
        case Cadence::NomadEffects:
            run_nomad_effects(world, call.seed);
            // The reference marks the field touched whenever this pass is SCHEDULED, whether
            // or not it wrote a node -- on a magicless world, and on one with no bands.
            // Reproduced rather than fixed: the only cost is one globe pass and the report
            // claiming it.
            ley_touched = true;
            break;
    }
    dedupe_warnings(world.warnings);
}

// ---------------------------------------------------------------------------
// The three paths.
// ---------------------------------------------------------------------------

// A request the pure tick REPORTS rather than runs: past the work ceiling without commit,
// or an age span it will not advance (an estimate, too many ages, or the gate). The world
// comes back as it was, with the clock the refusal was measured against.
TimeAdvanceResult reported(const World& world, const TimeAdvanceRequest& request, const WorldClock& now,
                           const std::optional<AgeAdvanceError>& gate) {
    const QuestInputs in = quest_inputs_of(world);
    TickRequest ask = tick_request_of(world, in, request);
    if (gate) ask.age_blocked = gate->what();
    TickResult told = advance_time(tick_state_of(world), ask);

    TimeAdvanceResult result{world, {}};
    result.world.world_clock = now;
    // The pure tick has no world to read a measurement from, so its age estimate is always
    // the extrapolation; this world's own last age advance is the better price.
    if (told.report.age_cost) {
        told.report.age_cost = age_cost_estimate(static_cast<double>(world.config.size), told.report.ages,
                                                 world_age_cost_basis(world));
    }
    // The gate's refusal is the pure tick's one INVALID_INPUT block on field "world"; the
    // ceilings are STATE_CAPACITY on "elapsed".
    if (gate && told.report.blocked && told.report.blocked->code == kRefusalInvalidInput &&
        told.report.blocked->field == "world") {
        result.report.gate = "validate_age_world";
        result.report.gate_field = "world." + gate->field();
    }
    result.report.tick = std::move(told.report);
    return result;
}

// Every span below an age that the ceiling admits.
TimeAdvanceResult tick(const World& world, const TimeAdvanceRequest& request, const WorldClock& now, double days,
                       Clock::time_point started) {
    // The private copy. Every pass below writes into it and the caller's world is never
    // touched, so a refusal anywhere leaves no half-advanced world behind.
    World out = world;
    const bool magic_on = out.config.magic_enabled != 0 && out.has_magic;
    bool ley_touched = false;

    // The player's ley edits, in caller order, before anything reads the field.
    for (const LeylineEdit& edit : request.leyline_edits) {
        try {
            apply_leyline_edit(out.magic, edit);
        } catch (const LeylineEditError& refused) {
            throw WorldTimeError(kRefusalInvalidInput, refused.field(), refused.what(), edit.school);
        }
    }
    if (!request.leyline_edits.empty()) ley_touched = true;

    // Built ONCE: a tick never changes who exists or what stands -- the cast and the ground
    // move at an age boundary and nowhere else.
    const QuestInputs in = quest_inputs_of(out);
    if (!request.resolutions.empty()) {
        if (!out.quests) {
            // A resolution on a world that has never been ticked opens the board first. The
            // bootstrap sweep is at index 0, NOT floor(day): it is not a crossing of the
            // quests cadence, and borrowing that day's index would draw the window the day's
            // own sweep is going to draw.
            const QuestGiverIndex givers(in.cast());
            const QuestTargetIndex targets(in.view());
            QuestBoard board;
            quest_step(board, sweep_of(out, in, givers, targets, now.day, 0));
            out.quests = std::move(board);
        }
        if (apply_resolutions(out, in, request.resolutions, now.day, magic_on)) ley_touched = true;
    }

    // The span: the quest sweeps, the ley drift, the sky, and the schedule of everything
    // else, from the pure tick.
    TickResult ticked = advance_time(tick_state_of(out), tick_request_of(out, in, request));
    if (ticked.report.blocked) {
        // Unreachable: the caller asked the same ceiling first. Kept honest rather than
        // committed over.
        TimeAdvanceResult result{world, {}};
        result.world.world_clock = now;
        result.report.tick = std::move(ticked.report);
        return result;
    }
    if (ticked.state.has_board) out.quests = std::move(ticked.state.board);
    if (out.has_magic) out.magic.networks = std::move(ticked.state.networks);
    if (ticked.state.has_surge) out.lunar_surge = ticked.state.surge;
    if (ticked.report.leylines_evaluated) ley_touched = true;

    // The passes the tick scheduled and does not own, in the absolute-time order it
    // scheduled them. Neither the sweeps nor the drift read a block these write, so running
    // those first cannot change the answer.
    for (const CadenceCall& call : ticked.pending) run_cadence(out, call, ley_touched);

    const double end_day = now.day + days;
    // The expensive globe pass, at most once per request and only when the base field was
    // touched; the surge after it is a closed form and free either way.
    if (ley_touched && out.has_magic) evaluate_networks(out);
    if (out.has_magic && out.has_astrology) out.lunar_surge = apply_surge(out.magic.networks, out.moon, end_day);
    out.world_clock = ticked.state.clock;

    // A span that crossed nothing and carried nothing records only if it was not zero: a
    // game asking what time it is every frame must not grow the log by a frame.
    const bool carried_nothing = ticked.executed.empty() && request.leyline_edits.empty() && request.resolutions.empty();
    if (!carried_nothing || days > 0.0) {
        TimeOperationRecord entry;
        entry.band = ticked.report.band;
        entry.elapsed_days = days;
        entry.from_day = now.day;
        entry.to_day = end_day;
        entry.steps = static_cast<std::int64_t>(ticked.executed.size());
        entry.leyline_edits = request.leyline_edits;
        entry.resolutions = request.resolutions;
        out.history.operations.push_back(std::move(entry));
        out.history.replay = std::string(kReplaySentence);
    }

    TimeAdvanceResult result{std::move(out), {}};
    result.report.tick = std::move(ticked.report);
    result.report.tick.resolutions = static_cast<int>(request.resolutions.size());
    result.report.tick.leylines_evaluated = ley_touched;
    result.report.leyline_edits = static_cast<int>(request.leyline_edits.size());
    result.world.timing_ms["time_advance_total"] = ms_since(started);
    return result;
}

// Retire the board at an age turn and rebuild it from the new age's hooks. Only a world
// that already carries a board gets one back: an age turn continues a lifecycle, it does
// not start one.
void turn_age_board(World& world, double day) {
    if (!world.quests) return;
    const QuestInputs in = quest_inputs_of(world);
    const QuestGiverIndex givers(in.cast());
    const QuestTargetIndex targets(in.view());
    // The index is recomputed as floor(day) by the board itself.
    quest_turn_age_board(*world.quests, sweep_of(world, in, givers, targets, day, 0));
}

// A committed span of one age or more.
TimeAdvanceResult advance_by_ages(const World& world, const TimeAdvanceRequest& request, const WorldClock& now,
                                  double days, std::int64_t ages) {
    TimeAdvanceReport report;
    report.tick.band = TimeBand::Age;
    report.tick.elapsed_days = days;
    report.tick.from_day = now.day;
    report.tick.ages = ages;
    // Priced before the ages run, from this world's own last age advance when it has one.
    report.tick.age_cost =
        age_cost_estimate(static_cast<double>(world.config.size), ages, world_age_cost_basis(world));
    report.tick.note = "A span of one age or longer is an age advancement, not a tick.";

    // In chunks: the age API turns at most kMaxAgeSteps in one call.
    World advanced = world;
    std::int64_t done = 0;
    while (done < ages) {
        AgeAdvanceRequest chunk;
        chunk.steps = static_cast<int>(std::min<std::int64_t>(kMaxAgeSteps, ages - done));
        chunk.moon_day = request.moon_day;
        advanced = advance_ages(advanced, chunk);
        done += chunk.steps;
    }
    // advance_ages wrote the age API's own sentence, as the reference's advance_age_request
    // does on every call. It stands unless the remainder tick writes the time API's.

    const double age_days = static_cast<double>(ages) * kAgeDays;
    WorldClock after = world_clock_of(advanced);
    after.day = now.day + age_days;
    after.age = static_cast<std::int64_t>(advanced.history.ages.size());
    advanced.world_clock = after;

    // The age turned, so the board does -- BEFORE the remainder ticks, because the
    // remainder is time spent in the new age.
    turn_age_board(advanced, after.day);

    TimeOperationRecord entry;
    entry.band = TimeBand::Age;
    entry.elapsed_days = age_days;
    entry.ages = ages;
    entry.from_day = now.day;
    entry.to_day = after.day;
    advanced.history.operations.push_back(std::move(entry));

    // The span is ages PLUS a remainder, and the remainder is a tick. Dropping it made
    // advance(one age and a month) differ from advance(one age) then advance(a month), and
    // left the log claiming an elapsed the clock never moved. Edits and resolutions ride the
    // same tick, in the new age, rather than being validated and discarded.
    const double remainder = days - age_days;
    if (remainder > 0.0 || !request.leyline_edits.empty() || !request.resolutions.empty()) {
        TimeAdvanceRequest rest;
        rest.elapsed = TimeElapsed{TimeUnit::Days, remainder};
        rest.leyline_edits = request.leyline_edits;
        rest.resolutions = request.resolutions;
        // **`commit` is NOT forwarded, as the reference does not forward it.** A remainder
        // past the work ceiling is therefore REPORTED by the tick below, not run: the ages
        // stand, the world's clock stops at them, and the report still says committed with
        // `to_day` at the end of the whole span. The reference records this as a hole in its
        // all-or-nothing promise; it is reproduced and pinned by a test, not fixed.
        TimeAdvanceResult ticked = advance_world_time(advanced, rest);
        advanced = std::move(ticked.world);
        report.remainder = std::move(ticked.report.tick);
        report.leyline_edits = ticked.report.leyline_edits;
    } else if (advanced.has_magic && advanced.has_astrology) {
        advanced.lunar_surge = apply_surge(advanced.magic.networks, advanced.moon, after.day);
    }

    report.tick.committed = true;
    report.tick.age_days = static_cast<std::int64_t>(age_days);
    report.tick.to_day = now.day + days;
    report.age = after.age;
    return TimeAdvanceResult{std::move(advanced), std::move(report)};
}

}  // namespace

// ---------------------------------------------------------------------------

WorldClock world_clock_of(const World& world) {
    ClockSource source;
    source.existing = world.world_clock ? &*world.world_clock : nullptr;
    source.has_founding_end_year = world.has_settlements;
    source.founding_end_year = world.settlements.founding.end_year;
    source.ages = static_cast<std::int64_t>(world.history.ages.size());
    return world_clock_of(source);
}

void validate_time_world(const World& world) {
    // The pure gate, asked twice so each refusal names the block it is about without this
    // file restating either condition: once with the sky assumed, then as the world is.
    TickRequest probe;
    probe.config = world.config;
    probe.has_astrology = true;
    try {
        validate_time_world(probe);
    } catch (const std::domain_error& refused) {
        throw missing_block("config", refused.what());
    }
    probe.has_astrology = world.has_astrology;
    try {
        validate_time_world(probe);
    } catch (const std::domain_error& refused) {
        throw missing_block("astrology", refused.what());
    }
}

std::optional<AgeAdvanceError> world_age_gate(const World& world) {
    try {
        validate_age_world(world);
    } catch (const AgeAdvanceError& refused) {
        return refused;
    }
    return std::nullopt;
}

AgeCostBasis world_age_cost_basis(const World& world) {
    AgeCostBasis basis;
    const auto recorded = world.timing_ms.find("age_advance_total");
    if (recorded == world.timing_ms.end() || !(recorded->second > 0.0)) return basis;
    basis.recorded_seconds = recorded->second / 1000.0;
    // advance_ages writes the timing and its operation together, so the last AGE operation
    // is the call that was timed -- never merely the last entry, which the log shares with
    // every other API. (The reference took the last entry carrying steps or ages, which a
    // tick entry also carries.)
    basis.recorded_ages = 1;
    for (auto it = world.history.operations.rbegin(); it != world.history.operations.rend(); ++it) {
        if (const auto* advanced = std::get_if<AgeOperation>(&*it)) {
            basis.recorded_ages = advanced->steps;
            break;
        }
    }
    return basis;
}

TimeAdvanceResult advance_world_time(const World& world, const TimeAdvanceRequest& request) {
    const auto started = Clock::now();

    // Everything judgeable without the world, in the reference's order.
    if (request.api_version != kTimeApiVersion) throw unsupported_api(request.api_version, kTimeApiVersion);
    const double days = checked_elapsed(request.elapsed);
    if (request.moon_day && *request.moon_day < 0) {
        throw wrong_type("moon_day", std::to_string(*request.moon_day), "an integer");
    }
    if (request.leyline_edits.size() > kMaxLeylineEdits) {
        throw over_capacity("leyline_edits", request.leyline_edits.size(), kMaxLeylineEdits, "edits per request");
    }
    if (request.resolutions.size() > kMaxQuestResolutions) {
        throw over_capacity("resolutions", request.resolutions.size(), kMaxQuestResolutions,
                            "resolutions per request");
    }

    // Then the world.
    validate_time_world(world);
    if (!request.leyline_edits.empty() && world.config.magic_enabled == 0) {
        throw refused_by_world("leyline_edits", std::to_string(request.leyline_edits.size()),
                               "This world was generated with magic disabled, so it has no ley networks to edit.", {});
    }

    const WorldClock now = world_clock_of(world);
    if (time_band(days) == TimeBand::Age) {
        // The one case the pure tick cannot execute: it has no world to turn an age over.
        // Everything else about the band -- the ceiling on the remainder, the ages ceiling,
        // the estimate, the gate -- it decides and reports.
        const std::optional<AgeAdvanceError> gate = world_age_gate(world);
        if (request.commit && ages_for(days) <= kMaxAgesPerRequest && !gate) {
            return advance_by_ages(world, request, now, days, ages_for(days));
        }
        return reported(world, request, now, gate);
    }
    // The work ceiling, asked before anything is applied: a span past it is reported whole,
    // and its edits and resolutions are neither applied nor judged, as the reference does.
    if (!request.commit && schedule_work(now.day, days) > kMaxTickSteps) {
        return reported(world, request, now, std::nullopt);
    }
    return tick(world, request, now, days, started);
}

// ---------------------------------------------------------------------------

QuestRequestResult world_quest_request(const World& world, const QuestRequest& request) {
    // Shape first: no world, no quest ids, no state.
    if (request.api_version != kQuestApiVersion) throw unsupported_api(request.api_version, kQuestApiVersion);
    if (request.actions.size() > kMaxQuestActions) {
        throw over_capacity("actions", request.actions.size(), kMaxQuestActions, "quest actions per request");
    }
    for (const QuestAction& item : request.actions) {
        if (item.quest_id.empty()) throw wrong_type("quest_id", "''", "text");
        if (item.day && (!std::isfinite(*item.day) || *item.day < 0.0)) {
            throw wrong_type("day", render(*item.day), "a finite number");
        }
    }

    // The same gate the tick asks: a weaker question here would admit a world the tick
    // refuses a moment later.
    validate_time_world(world);
    if (!world.quests) {
        throw missing_block("quests", "This world carries no quests to act on. Advance it by at least one day "
                                      "first, which is what opens them.");
    }

    const double now = world_clock_of(world).day;
    QuestRequestResult result{world, {}};
    result.report.api_version = kQuestApiVersion;
    result.report.day = now;
    // Nothing changed, so nothing is recorded.
    if (request.actions.empty()) return result;

    // All or nothing: the batch is applied to the returned copy, and a refusal anywhere
    // throws it away with the caller's world untouched.
    QuestBoard& board = *result.world.quests;
    // As the reference builds it: the LAST row wins for an id two rows share.
    std::map<std::string, std::size_t> by_id;
    for (std::size_t i = 0; i < board.quests.size(); ++i) by_id[board.quests[i].quest_id] = i;

    QuestOperationRecord entry;
    for (const QuestAction& item : request.actions) {  // caller order
        const auto at = by_id.find(item.quest_id);
        if (at == by_id.end()) throw unknown_name(item.quest_id, ids_of(board), "quest");
        const double day = item.day.value_or(now);
        // Recomputed per action, so a batch that closes three quests stops offering them back.
        const std::vector<std::string> alternatives = open_ids(board);
        QuestTransition moved = [&]() {
            try {
                return quest_transition(board.quests[at->second], item.action, day, alternatives);
            } catch (const std::domain_error& refused) {
                throw refused_by_world("quest_id", item.quest_id, refused.what(), alternatives);
            }
        }();
        board.quests[at->second] = std::move(moved.quest);
        board.log.push_back(std::move(moved.entry));
        result.report.actions.push_back({item.quest_id, item.action, day, board.quests[at->second].state});
        entry.actions.push_back({item.quest_id, item.action, day});
    }
    result.world.history.operations.push_back(std::move(entry));
    return result;
}

// ---------------------------------------------------------------------------
// The shared refusal builders (world_time.hpp). The first eight forward, unchanged, to the
// ones this file's own API uses above; the last two are shapes only the read and
// person-state APIs need.
// ---------------------------------------------------------------------------
namespace request_errors {

std::string render(double value) { return fwg::render(value); }

std::vector<std::string> nearest(std::string_view name, std::vector<std::string> pool) {
    return fwg::nearest(name, std::move(pool));
}

WorldTimeError over_capacity(const std::string& field, std::size_t value, std::size_t limit, std::string_view noun) {
    return fwg::over_capacity(field, value, limit, noun);
}

WorldTimeError unsupported_api(int value, int supported) { return fwg::unsupported_api(value, supported); }

WorldTimeError wrong_type(const std::string& field, const std::string& received, std::string_view article) {
    return fwg::wrong_type(field, received, article);
}

WorldTimeError missing_block(const std::string& block, const std::string& message) {
    return fwg::missing_block(block, message);
}

WorldTimeError refused_by_world(const std::string& field, const std::string& received, const std::string& reason,
                                const std::vector<std::string>& alternatives) {
    return fwg::refused_by_world(field, received, reason, alternatives);
}

WorldTimeError unknown_name(const std::string& name, const std::vector<std::string>& known, std::string_view noun) {
    return fwg::unknown_name(name, known, noun);
}

WorldTimeError invalid_choice(const std::string& field, const std::string& received,
                              const std::vector<std::string>& choices) {
    std::string listed;
    for (std::size_t i = 0; i < choices.size(); ++i) {
        if (i != 0) listed += ", ";
        listed += in_quotes(choices[i]);
    }
    return WorldTimeError(kRefusalInvalidInput, field,
                          field + " must be one of " + listed + "; received " + received + ".", received,
                          /*retry=*/true, fwg::nearest(received, choices));
}

WorldTimeError out_of_range(const std::string& field, double value, std::optional<double> low,
                            std::optional<double> high, std::string_view units) {
    // 'seed must be 0..4294967295 seed' reads as a stutter: a unit that merely repeats a word
    // of the field name is dropped from the sentence.
    bool stutter = false;
    std::size_t start = 0;
    while (!units.empty() && start <= field.size()) {
        const std::size_t end = std::min(field.find('_', start), field.size());
        if (std::string_view(field).substr(start, end - start) == units) stutter = true;
        start = end + 1;
    }
    const std::string spoken = units.empty() || stutter ? std::string() : " " + std::string(units);
    return WorldTimeError(kRefusalInvalidInput, field,
                          field + " must be " + (low ? fwg::render(*low) : std::string("None")) + ".." +
                              (high ? fwg::render(*high) : std::string("None")) + spoken + "; received " +
                              fwg::render(value) + ".",
                          fwg::render(value));
}

}  // namespace request_errors

}  // namespace fwg
