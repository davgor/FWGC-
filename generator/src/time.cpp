#include "fwg/time.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>

#include "fwg/random.hpp"

namespace fwg {
namespace {

// Ordered, and the order IS the rank. Periods in days.
//
// Deliberately NOT extracted to data: this is scheduler structure, not tuning. The rank
// breaks ties between cadences firing on the same day AND is dependency order, so a data
// file would turn a thing a reader can check against the passes into a fifth thing to keep
// in sync with them.
struct CadenceRow {
    Cadence cadence;
    std::string_view name;
    double period;
    // Replaces its block wholesale, so only the last crossing in a span can survive.
    bool coalesces;
};

constexpr CadenceRow kCadences[] = {
    {Cadence::Quests, "quests", 1.0, false},
    {Cadence::LeyDrift, "ley_drift", static_cast<double>(kDaysPerYear), false},
    {Cadence::SeasonalFood, "seasonal_food", kSeasonDays, true},
    {Cadence::Nomads, "nomads", static_cast<double>(kDaysPerYear), true},
    {Cadence::NomadRoutes, "nomad_routes", static_cast<double>(kDaysPerMonth), true},
    {Cadence::BeastMovements, "beast_movements", static_cast<double>(kDaysPerMonth), true},
    {Cadence::Encounters, "encounters", static_cast<double>(kDaysPerMonth), true},
    {Cadence::NomadEffects, "nomad_effects", static_cast<double>(kDaysPerYear), true},
    {Cadence::VillainOutlook, "villain_outlook", static_cast<double>(kDaysPerYear), true},
};

constexpr std::size_t kCadenceCount = sizeof(kCadences) / sizeof(kCadences[0]);

const CadenceRow& row_of(Cadence cadence) {
    return kCadences[static_cast<std::size_t>(cadence)];
}

constexpr Cadence kCadenceOrder[] = {
    Cadence::Quests,        Cadence::LeyDrift,       Cadence::SeasonalFood,
    Cadence::Nomads,        Cadence::NomadRoutes,    Cadence::BeastMovements,
    Cadence::Encounters,    Cadence::NomadEffects,   Cadence::VillainOutlook,
};

std::int64_t floor_div(double value, double period) {
    return static_cast<std::int64_t>(std::floor(value / period));
}

// Half away from zero, not CPython's round-half-to-even. These are report figures a human
// reads, and a rubric that sends 2.5 down and 3.5 up reads as arbitrary to anyone checking
// it by hand.
double round_to(double value, int places) {
    double scale = 1.0;
    for (int i = 0; i < places; ++i) scale *= 10.0;
    return std::round(value * scale) / scale;
}

constexpr std::string_view kTickBasisProse =
    "inherited from the reference implementation and NOT re-measured in C++: at size 17, "
    "72,207 steps in 2.05 s and 1,804,646 in 26.81 s, so 14.2 us a step over a 1.0 s copy; "
    "at size 33 the same spans took 6.47 s and 79.90 s, so both terms are scaled by cell "
    "count, which over-reports size 33 by about a fifth. The C++ tick is far cheaper and "
    "these figures must be replaced by measurement once it can be run end to end";

constexpr std::string_view kTickPrecisionProse =
    "order of magnitude; steps are a proxy for cost, because a coalesced pass runs once per "
    "call however many crossings it had while the daily quest sweep runs every day";

constexpr std::string_view kAgeBasisProse =
    "extrapolated by cell count from age advances timed on the reference implementation "
    "(34.3-39.4 s at size 17 and 181.8 s at size 33, on a contended development machine, so "
    "upper bounds) and NOT re-measured in C++; the seconds take size 33 as the reference "
    "because the cost grows faster than cell count between the two, and nothing has been "
    "timed above size 33";

constexpr std::string_view kAgeMemoryProse =
    "566 MB peak working set measured at size 17 on the reference implementation, "
    "extrapolated by cell count. The measurement is a lower bound, because the machine was "
    "trimming working sets under load; the extrapolation is an over-estimate of unknown "
    "size, because the two measured world documents grew 3.0x for 3.8x the cells rather "
    "than in step with them. Nothing above size 33 has been measured at all";

}  // namespace

std::string_view to_string(TimeBand band) {
    switch (band) {
        case TimeBand::Instant: return "instant";
        case TimeBand::Day: return "day";
        case TimeBand::Season: return "season";
        case TimeBand::Long: return "long";
        case TimeBand::Age: return "age";
    }
    return "instant";
}

std::string_view to_string(Cadence cadence) { return row_of(cadence).name; }

double cadence_period(Cadence cadence) { return row_of(cadence).period; }

bool cadence_coalesces(Cadence cadence) { return row_of(cadence).coalesces; }

std::span<const Cadence> cadences() {
    return std::span<const Cadence>(kCadenceOrder, kCadenceCount);
}

TimeBand time_band(double days) {
    if (days < 1.0) return TimeBand::Instant;
    if (days < kSeasonDays) return TimeBand::Day;
    if (days < 2.0 * kDaysPerYear) return TimeBand::Season;
    if (days < kAgeDays) return TimeBand::Long;
    return TimeBand::Age;
}

std::int64_t ages_for(double days) {
    return static_cast<std::int64_t>(std::floor(days / kAgeDays));
}

double elapsed_days(const TimeElapsed& elapsed) {
    if (!std::isfinite(elapsed.value) || elapsed.value < 0.0) {
        throw std::invalid_argument("elapsed: a finite count of time to advance, at or above "
                                    "zero, got " + std::to_string(elapsed.value) + '.');
    }
    switch (elapsed.unit) {
        case TimeUnit::Seconds:
            return elapsed.value / (static_cast<double>(kHoursPerDay) * 3600.0);
        case TimeUnit::Years:
            return elapsed.value * static_cast<double>(kDaysPerYear);
        case TimeUnit::Days:
            break;
    }
    return elapsed.value;
}

ScheduleRange schedule_crossings(Cadence cadence, double now, double days) {
    const double period = row_of(cadence).period;
    ScheduleRange range;
    range.first = floor_div(now, period) + 1;
    range.last = floor_div(now + days, period);
    return range;
}

std::vector<ScheduleStep> schedule_steps(double now, double days) {
    if (days < 0.0) throw std::invalid_argument("days must be >= 0");
    std::vector<ScheduleStep> out;
    // Every cadence, always. Which ones produce a step is decided by whether the window
    // contains one of their crossings, NEVER by how long the window is.
    for (const Cadence cadence : kCadenceOrder) {
        const CadenceRow& row = row_of(cadence);
        ScheduleRange range = schedule_crossings(cadence, now, days);
        if (row.coalesces && !range.empty()) range.first = range.last;
        for (std::int64_t i = range.first; i <= range.last; ++i) {
            out.push_back({static_cast<double>(i) * row.period, cadence, i});
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const ScheduleStep& a, const ScheduleStep& b) {
        if (a.day != b.day) return a.day < b.day;
        if (a.cadence != b.cadence) return static_cast<int>(a.cadence) < static_cast<int>(b.cadence);
        return a.index < b.index;
    });
    return out;
}

std::vector<std::pair<Cadence, std::int64_t>> schedule_by_cadence(double now, double days) {
    if (days < 0.0) throw std::invalid_argument("days must be >= 0");
    std::vector<std::pair<Cadence, std::int64_t>> counts;
    for (const Cadence cadence : kCadenceOrder) {
        std::int64_t count = schedule_crossings(cadence, now, days).count();
        if (row_of(cadence).coalesces) count = std::min<std::int64_t>(count, 1);
        if (count != 0) counts.emplace_back(cadence, count);
    }
    // By NAME, which is a different order from the rank, and it is the one that reaches the
    // report.
    std::stable_sort(counts.begin(), counts.end(), [](const auto& a, const auto& b) {
        return to_string(a.first) < to_string(b.first);
    });
    return counts;
}

std::int64_t schedule_work(double now, double days) {
    std::int64_t total = 0;
    for (const auto& [cadence, count] : schedule_by_cadence(now, days)) {
        (void)cadence;
        total += count;
    }
    return total;
}

double schedule_longest_span(double now, std::int64_t limit) {
    if (limit < 0) throw std::invalid_argument("limit must be >= 0");
    std::int64_t low = 0;
    std::int64_t high = static_cast<std::int64_t>(kAgeDays);
    if (schedule_work(now, static_cast<double>(high)) <= limit) return static_cast<double>(high);
    while (low < high) {
        const std::int64_t middle = (low + high + 1) / 2;
        if (schedule_work(now, static_cast<double>(middle)) <= limit) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    return static_cast<double>(low);
}

SchedulePlan schedule_plan(double now, double days) {
    SchedulePlan plan;
    plan.band = time_band(days);
    plan.elapsed_days = days;
    plan.from_day = now;
    plan.to_day = now + days;
    plan.ages = plan.band == TimeBand::Age ? ages_for(days) : 0;
    plan.by_cadence = schedule_by_cadence(now, days);
    for (const auto& [cadence, count] : plan.by_cadence) {
        (void)cadence;
        plan.steps += count;
    }
    return plan;
}

// ---------------------------------------------------------------------------

WorldClock world_clock_of(const ClockSource& source) {
    if (source.existing != nullptr && source.existing->version == kClockVersion) {
        return *source.existing;
    }
    WorldClock clock;
    clock.day = source.has_founding_end_year
                    ? static_cast<double>(source.founding_end_year) * kDaysPerYear
                    : 0.0;
    clock.age = source.ages;
    clock.epoch_day = 0.0;
    clock.derived_from = source.has_founding_end_year ? "founding.end_year" : "epoch";
    return clock;
}

long long clock_year(const WorldClock& clock) {
    return static_cast<long long>(std::floor(clock.day / static_cast<double>(kDaysPerYear)));
}

// ---------------------------------------------------------------------------

double ley_year_low() { return std::pow(kLeyAgeLow, 1.0 / kAgeYears); }
double ley_year_high() { return std::pow(kLeyAgeHigh, 1.0 / kAgeYears); }

bool ley_drift(std::map<std::string, LeyNetwork>& networks, std::uint32_t seed,
               std::int64_t index, bool magic_enabled) {
    if (!magic_enabled) return false;
    const double low = ley_year_low();
    const double high = ley_year_high();
    bool moved = false;
    // One generator per school per year. The map is sorted rather than insertion-ordered,
    // which is a deliberate choice recorded here: a per-school generator means the ORDER
    // schools are walked cannot change any value, only the order they are written, and a
    // sorted walk is the more reproducible of the two.
    for (auto& [name, net] : networks) {
        Rng rng = Rng::child(seed, "time-ley-" + name, index);
        // Nodes first, then edges, each in list order, all drawing from this one generator.
        // Reordering silently reassigns every multiplier.
        for (LeyNode& node : net.nodes) {
            node.intensity = std::min(4.0, node.intensity * rng.uniform(low, high));
            moved = true;
        }
        for (LeyEdge& edge : net.edges) {
            edge.intensity = std::min(4.0, edge.intensity * rng.uniform(low, high));
            moved = true;
        }
    }
    return moved;
}

LunarSurge apply_surge(std::map<std::string, LeyNetwork>& networks, const Moon& moon, double day) {
    const std::array<double, kKnownSchools> factors =
        tide(moon, static_cast<long long>(std::floor(day)));
    LunarSurge surge;
    surge.day = day;
    const std::span<const MagicSchool> schools = magic_school_table();
    for (int i = 0; i < kKnownSchools; ++i) {
        surge.factors.emplace_back(schools[static_cast<std::size_t>(i)].name,
                                   factors[static_cast<std::size_t>(i)]);
    }
    for (auto& [name, net] : networks) {
        const int index = magic_school_index(name);
        // A hidden school is not in the tide and is LEFT ENTIRELY ALONE -- not given a zero
        // surge, not flagged. Widening this to all twelve would add a field to four networks
        // in every world.
        if (index < 0 || index >= kKnownSchools) continue;
        net.has_surge = true;
        net.surged_strength = net.strength * factors[static_cast<std::size_t>(index)];
    }
    return surge;
}

void dedupe_warnings(std::vector<std::string>& warnings) {
    std::set<std::string> seen;
    std::vector<std::string> out;
    for (const std::string& warning : warnings) {
        if (!seen.insert(warning).second) continue;
        out.push_back(warning);
    }
    warnings = std::move(out);
}

// ---------------------------------------------------------------------------

TickCostEstimate tick_cost_estimate(double size, double days, std::int64_t steps) {
    TickCostEstimate estimate;
    const double scale = std::pow((size > 0.0 ? size : kTickBasisSize) / kTickBasisSize, 2.0);
    const double copy_seconds = kTickBasisCopySeconds * scale;
    const double per_step = kTickBasisStepSeconds * scale;
    estimate.steps = steps;
    estimate.elapsed_days = days;
    estimate.elapsed_years = round_to(days / static_cast<double>(kDaysPerYear), 3);
    // Two terms, because the cost has two shapes: a fixed cost per call that scales with the
    // world and not with the span, and a marginal cost per cadence step that scales with the
    // span. A single-term model gets whichever one it omits badly wrong.
    estimate.total_seconds =
        round_to(copy_seconds + static_cast<double>(steps) * per_step, 3);
    estimate.copy_seconds = round_to(copy_seconds, 3);
    estimate.per_step_seconds = per_step;
    estimate.peak_mb = round_to(
        kTickBasisCopyMb * scale + static_cast<double>(steps) * kTickBasisStepBytes / 1048576.0, 1);
    estimate.basis = std::string(kTickBasisProse);
    estimate.precision = std::string(kTickPrecisionProse);
    estimate.measured = false;
    return estimate;
}

AgeCostEstimate age_cost_estimate(double size, std::int64_t ages, const AgeCostBasis& basis) {
    AgeCostEstimate estimate;
    estimate.ages = ages;
    double per_age = 0.0;
    if (basis.recorded_seconds.has_value() && *basis.recorded_seconds > 0.0) {
        const std::int64_t carried = std::max<std::int64_t>(1, basis.recorded_ages);
        per_age = *basis.recorded_seconds / static_cast<double>(carried);
        estimate.basis = "this world's own recorded age advance over the " +
                         std::to_string(carried) + " age(s) that call advanced";
        estimate.measured = true;
    } else {
        per_age = kAgeBasisSeconds * std::pow(size / kAgeBasisSize, 2.0);
        estimate.basis = std::string(kAgeBasisProse);
        estimate.measured = false;
    }
    estimate.per_age_seconds = round_to(per_age, 3);
    estimate.total_seconds = round_to(per_age * static_cast<double>(ages), 3);
    estimate.peak_mb = round_to(kAgeBasisMb * std::pow(size / kAgeBasisMbSize, 2.0), 1);
    estimate.peak_mb_precision = std::string(kAgeMemoryProse);
    return estimate;
}

// ---------------------------------------------------------------------------

void validate_time_world(const TickRequest& request) {
    if (request.config.world_recipe != 3 || request.config.phase < 13) {
        throw std::domain_error(
            "Time advance requires a recipe 3 world generated through creatures (phase 13); "
            "this world is recipe " + std::to_string(request.config.world_recipe) +
            " at phase " + std::to_string(request.config.phase) + '.');
    }
    if (!request.has_astrology) {
        throw std::domain_error("This world predates the moon contract and carries no sky to "
                                "advance; regenerate it with recipe_version 3.");
    }
}

TickResult advance_time(const TickState& state, const TickRequest& request) {
    const auto started = std::chrono::steady_clock::now();
    const auto stamp = [&started](TickResult& out) {
        out.elapsed_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count();
    };

    // Everything judgeable without the world first, then the world gate.
    const double days = elapsed_days(request.elapsed);
    validate_time_world(request);
    if (request.resolutions.size() > kMaxQuestResolutions) {
        throw std::length_error("resolutions: " + std::to_string(request.resolutions.size()) +
                                " is above the maximum of " +
                                std::to_string(kMaxQuestResolutions) + " resolutions per request.");
    }

    ClockSource source;
    source.existing = state.has_clock ? &state.clock : nullptr;
    source.has_founding_end_year = request.has_founding_end_year;
    source.founding_end_year = request.founding_end_year;
    source.ages = state.ages;
    const WorldClock now = world_clock_of(source);
    const TimeBand which = time_band(days);
    const double size = static_cast<double>(request.config.size);

    TickResult out;
    out.state = state;  // the caller's slice is copied, and the original is never touched
    out.state.has_clock = true;
    out.state.clock = now;
    out.report.band = which;
    out.report.elapsed_days = days;
    out.report.from_day = now.day;
    out.report.to_day = now.day + days;
    out.report.ages = which == TimeBand::Age ? ages_for(days) : 0;
    out.report.resolutions = static_cast<int>(request.resolutions.size());

    // What this request would execute AS A TICK, priced before anything else happens.
    //
    // Only the remainder after whole ages, because the ages are delegated and are not
    // ticked; pricing them as cadence steps would refuse every age advance for work it never
    // does. Asked BEFORE the age branch so a span of ages plus an unaffordable remainder is
    // reported whole instead of advancing its ages and then baulking.
    const double tick_days =
        which == TimeBand::Age ? days - static_cast<double>(ages_for(days)) * kAgeDays : days;
    const double tick_from = now.day + (days - tick_days);
    const std::int64_t tick_steps = schedule_work(tick_from, tick_days);

    if (tick_steps > kMaxTickSteps && !request.commit) {
        // The span that would fit, in days, measured from the CALLER'S OWN clock -- so
        // sending it straight back is guaranteed to be accepted. A suggestion computed from
        // the day the ages would have ended is a suggestion about a request the caller
        // cannot make.
        const double fits = schedule_longest_span(now.day, kMaxTickSteps);
        TickBlock block;
        block.code = "STATE_CAPACITY";
        block.field = "elapsed";
        block.received = tick_steps;
        block.limit = kMaxTickSteps;
        block.message = "elapsed " + std::to_string(tick_steps) +
                        " is above the maximum of " + std::to_string(kMaxTickSteps) +
                        " cadence steps per request.";
        block.clamp_days = fits;
        block.note = "the longest span from this world_clock that runs at most " +
                     std::to_string(kMaxTickSteps) + " steps";
        out.report.steps = tick_steps;
        out.report.blocked = std::move(block);
        out.report.tick_cost = tick_cost_estimate(size, tick_days, tick_steps);
        out.report.note = "Estimate only. Send commit: true to run it anyway, or ask for at "
                          "most " + std::to_string(fits) + " day(s).";
        stamp(out);
        return out;
    }

    if (which == TimeBand::Age) {
        const std::int64_t ages = ages_for(days);
        out.report.age_days = static_cast<std::int64_t>(static_cast<double>(ages) * kAgeDays);
        if (ages > kMaxAgesPerRequest) {
            TickBlock block;
            block.code = "STATE_CAPACITY";
            block.field = "elapsed";
            block.received = ages;
            block.limit = kMaxAgesPerRequest;
            block.message = "elapsed " + std::to_string(ages) + " is above the maximum of " +
                            std::to_string(kMaxAgesPerRequest) + " ages per request.";
            out.report.blocked = std::move(block);
            out.report.age_cost = age_cost_estimate(size, ages);
            out.report.note = "Ask for fewer ages, or send several requests.";
            stamp(out);
            return out;
        }
        out.report.age_cost = age_cost_estimate(size, ages);
        out.report.note = "A span of one age or longer is an age advancement, not a tick.";
        if (!request.age_blocked.empty()) {
            // Reported rather than raised: a caller that asked for five thousand years
            // deserves the estimate and the reason together.
            TickBlock block;
            block.code = "INVALID_INPUT";
            block.field = "world";
            block.message = request.age_blocked;
            block.retry = false;
            out.report.blocked = std::move(block);
            stamp(out);
            return out;
        }
        if (request.commit) {
            // The one place this port cannot do what the reference did. An age advance
            // rebuilds every derived layer through a capability this build does not carry,
            // so the commit is REFUSED IN THE OPEN rather than silently returning a world
            // whose clock did not move while the report claimed it had.
            TickBlock block;
            block.code = "INVALID_INPUT";
            block.field = "elapsed";
            block.received = ages;
            block.message = "An age advance rebuilds every derived layer and this build "
                            "carries no age machinery to run one, so a span of one age or "
                            "longer cannot be committed.";
            block.clamp_days = kAgeDays - 1.0;
            block.note = "the longest span this build can tick";
            block.retry = false;
            out.report.blocked = std::move(block);
        } else {
            out.report.note = "Estimate only. Send commit: true to advance " +
                              std::to_string(ages) +
                              " age(s), or ask for a shorter span to tick instead.";
        }
        stamp(out);
        return out;
    }

    out.executed = schedule_steps(now.day, days);
    out.report.steps = static_cast<std::int64_t>(out.executed.size());
    out.report.by_cadence = schedule_by_cadence(now.day, days);
    const double end_day = now.day + days;

    // Keyed on "this span crosses nothing", NOT on "this span is short". The old test was
    // the band, which let a sub-day call carrying a resolution fall through to the full path
    // and move the ley field -- the opposite of what the band promised.
    if (out.executed.empty() && request.resolutions.empty()) {
        if (out.state.has_magic && request.has_astrology) {
            out.state.surge = apply_surge(out.state.networks, request.moon, end_day);
            out.state.has_surge = true;
        }
        out.state.clock.day = end_day;
        out.state.clock.age = state.ages;
        if (days > 0.0) {
            // A span of exactly zero records NOTHING. A game asking what time it is every
            // frame would otherwise append an operation per frame -- an unbounded record of
            // nothing happening, in the one band built to be free.
            out.state.operations.push_back({kTimeApiVersion, "time", which, days, now.day,
                                            end_day, 0, 0, 0});
            out.state.replay = std::string(kReplaySentence);
        }
        out.report.committed = true;
        out.report.leylines_evaluated = false;
        stamp(out);
        return out;
    }

    const bool magic_on = request.config.magic_enabled != 0 && out.state.has_magic;
    // Built ONCE rather than on each of a long span's daily quest sweeps, because a tick
    // never changes who exists: the cast moves at an age boundary and nowhere else.
    const QuestGiverIndex givers(request.cast);
    const QuestTargetIndex targets(request.targets);
    bool ley_touched = false;

    QuestStepRequest step;
    step.seed = request.config.seed;
    step.hooks = request.hooks;
    step.heroes_ok = request.heroes_ok;
    step.givers = &givers;
    step.targets = &targets;

    if (!request.resolutions.empty()) {
        if (!out.state.has_board) {
            // The bootstrap sweep is at index 0, NOT floor(now.day): it is not a crossing of
            // the quests cadence, so borrowing that day's index would draw the window the
            // day's own sweep is going to draw.
            step.day = now.day;
            step.index = 0;
            quest_step(out.state.board, step);
            out.state.has_board = true;
        }
        const QuestResolutionResult applied = quest_apply_resolutions(
            out.state.board, request.resolutions, now.day, request.hooks, magic_on,
            magic_on ? &out.state.networks : nullptr);
        if (applied.ley_moved) ley_touched = true;
    }

    for (const ScheduleStep& executed : out.executed) {
        if (executed.cadence == Cadence::Quests) {
            step.day = executed.day;
            step.index = executed.index;
            quest_step(out.state.board, step);
            out.state.has_board = true;
        } else if (executed.cadence == Cadence::LeyDrift) {
            // Only claim the field moved if it did.
            if (ley_drift(out.state.networks, request.config.seed, executed.index, magic_on)) {
                ley_touched = true;
            }
        } else {
            CadenceCall call;
            call.cadence = executed.cadence;
            call.day = executed.day;
            call.index = executed.index;
            call.seed = child_seed(request.config.seed,
                                   "time-" + std::string(to_string(executed.cadence)),
                                   executed.index);
            out.pending.push_back(call);
        }
    }

    if (out.state.has_magic && request.has_astrology) {
        out.state.surge = apply_surge(out.state.networks, request.moon, end_day);
        out.state.has_surge = true;
    }
    out.state.clock.day = end_day;
    out.state.clock.age = state.ages;
    out.state.operations.push_back({kTimeApiVersion, "time", which, days, now.day, end_day,
                                    out.report.steps, 0,
                                    static_cast<int>(request.resolutions.size())});
    out.state.replay = std::string(kReplaySentence);
    out.report.committed = true;
    out.report.leylines_evaluated = ley_touched;
    stamp(out);
    return out;
}

}  // namespace fwg
