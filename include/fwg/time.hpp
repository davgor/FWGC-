// The world's clock, and the only thing that moves it.
//
// Before this existed there were request-shaped mutators and no clock between them, so
// every consequence of elapsed time had to be smuggled into an age boundary: "a swarm that
// should erupt in a bad year erupts in every year BECAUSE THERE ARE NO YEARS". This takes a
// world and an elapsed span, decides which periodic passes cross that span, runs them in
// absolute-time order, drifts the ley field, recomputes the lunar surge and moves the quest
// board.
//
// **The binding invariant: the RNG domain is keyed by simulated time, never by tick ordinal
// or call count.** Every cadence fires on an absolute step index floor(day / period)
// measured from the epoch, so a call for elapsed E executes exactly the indices falling in
// (now, now + E] and nothing else. No draw is made per call and no index depends on how the
// span was cut. That is what makes advance(30y) and advance(15y) twice identical.
//
// The failure mode this shape exists to prevent is a seed derived from tick ordinal. It is
// invisible until someone chunks differently, and by then a saved world has been advanced
// across it.
//
// **A band names how big a span is. It does NOT decide which cadences may run.** It used to,
// and that was a silent, PERMANENT data-loss bug rather than a conservative one: the window
// is half-open at the start, so a crossing skipped because this call was too short is never
// revisited by any later call. A game ticking a month at a time would never once run ley
// drift, seasonal food or the villain outlook -- not deferred, skipped -- and a game ticking
// by the second would advance nothing but its clock, forever.
//
// Ported from `Sim/icarus_sim/terrain_time_schedule.py` and `terrain_time.py`. The Python
// tree is being deleted; this is the only copy.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fwg/astrology.hpp"
#include "fwg/config.hpp"
#include "fwg/magic.hpp"
#include "fwg/quests.hpp"

namespace fwg {

inline constexpr int kTimeApiVersion = 1;
inline constexpr int kClockVersion = 1;

// ---------------------------------------------------------------------------
// The calendar.
//
// 24-hour days, 30-day months, 12-month years, day 0 at founding year 0 -- TAKEN FROM
// astrology.hpp RATHER THAN RESTATED. A tick and the almanac disagreeing about what a year
// is would be invisible in both, so there is one calendar and this is not it.
// ---------------------------------------------------------------------------

inline constexpr double kSeasonDays = kDaysPerYear / 4.0;  // 90

// **New contract, not a discovered constant.** An age had no duration before the clock
// existed: the age list is a list, and the age lottery samples the founding end year for its
// moon day, so nothing in the generator said how long one lasts. A tick cannot route a long
// span into an age advance without an answer, and this is that answer. It is load-bearing in
// two places outside this line and both move with it or break silently: `time_band` returns
// Age at kAgeDays, so what counts as a tick rather than an age advance moves with it; and
// the per-year ley bounds are derived from it.
inline constexpr double kAgeYears = 5000.0;
inline constexpr double kAgeDays = kAgeYears * kDaysPerYear;  // 1'800'000

enum class TimeBand : int { Instant = 0, Day, Season, Long, Age };
std::string_view to_string(TimeBand band);

// Instant < 1 day; Day < 90; Season < 720 (two years); Long < 1'800'000; Age at or above.
// Moving a threshold changes only the LABEL in the report and the route into the age band.
// It cannot change which cadences run, and any port that lets it reintroduces the data loss.
TimeBand time_band(double days);

// How many age advances a span is worth.
std::int64_t ages_for(double days);

enum class TimeUnit : int { Seconds = 0, Days, Years };

// Exactly one unit, by construction. The reference counts the keys of a JSON object to
// enforce that and refuses two; a sum type cannot express two, so only the value refusals
// survive the port.
struct TimeElapsed {
    TimeUnit unit = TimeUnit::Days;
    double value = 0.0;
};

// Days from a span. Throws std::invalid_argument on a value that is not finite or is
// negative -- seconds divide by 24 * 3600 and years multiply by 360.
double elapsed_days(const TimeElapsed& elapsed);

// ---------------------------------------------------------------------------
// The cadences.
//
// THE ORDER IS THE RANK, and it is load-bearing twice over: it breaks ties between cadences
// firing on the same day, AND it is dependency order, producers before consumers. The nomad
// route pass reads and writes the block the nomad pass replaced, and the encounter index
// reads both the nomads and the beast movements. Ordering them any other way would build an
// index of bands that no longer exist. `NomadEffects` runs last for the same reason: the
// nomad pass replaces its block wholesale, discarding the effects the PREVIOUS population
// left behind, so the write-backs must re-run after routes exist to ride.
// ---------------------------------------------------------------------------
enum class Cadence : int {
    Quests = 0,       // 1 day
    LeyDrift,         // 360
    SeasonalFood,     // 90
    Nomads,           // 360
    NomadRoutes,      // 30
    BeastMovements,   // 30
    Encounters,       // 30
    NomadEffects,     // 360
    VillainOutlook,   // 360
};

// In rank order. Rank is the position here and nothing else reads it.
std::span<const Cadence> cadences();
std::string_view to_string(Cadence cadence);
double cadence_period(Cadence cadence);

// Whether a cadence's pass REPLACES ITS BLOCK WHOLESALE rather than accumulating into it.
// Only the last crossing inside a span is executed, because every earlier one would be
// overwritten by it: thirty years runs one encounter index instead of 360. Sound only
// because a shorter period divides a longer one -- so a consumer's last crossing is never
// earlier than its producer's -- and because none of them reads the block it writes.
//
// **A pass that ACCUMULATES must never coalesce.** The quest sweep carries expiry forward
// and the ley drift is a compounding random walk, and neither does.
bool cadence_coalesces(Cadence cadence);

// The absolute step indices of a cadence falling in (now, now + days]. Half-open at the
// start so a step is never executed twice by two adjacent calls, and closed at the end so a
// step landing exactly on the boundary is executed once. Empty when last < first.
struct ScheduleRange {
    std::int64_t first = 0;
    std::int64_t last = -1;
    bool empty() const { return last < first; }
    std::int64_t count() const { return empty() ? 0 : last - first + 1; }
};
ScheduleRange schedule_crossings(Cadence cadence, double now, double days);

struct ScheduleStep {
    // index * period: the ABSOLUTE cadence boundary, never now + something.
    double day = 0.0;
    Cadence cadence = Cadence::Quests;
    std::int64_t index = 0;
};

// Every cadence step inside (now, now + days], ordered by absolute day, then by the pinned
// cadence rank, then by index. **Ordering by time rather than by subsystem is the second
// half of chunk-independence**: a subsystem-major order would run a year of one pass before
// a month of another, and the two orders diverge the moment a call boundary lands between
// them. Throws std::invalid_argument on a negative span.
std::vector<ScheduleStep> schedule_steps(double now, double days);

// How many steps each cadence would run, COUNTED NOT BUILT, and sorted by cadence NAME --
// which is a different order from the rank and is the one that reaches the report. A span
// just under an age is 1.8 million steps and the work ceiling has to answer before that list
// exists rather than after it. Cadences contributing zero are absent.
std::vector<std::pair<Cadence, std::int64_t>> schedule_by_cadence(double now, double days);

// The number of steps, without building them. It must answer EXACTLY the question
// schedule_steps answers, which is asserted against the enumerator itself rather than
// against a formula: a counter that answers a slightly different question than the thing it
// stands in for is the near-miss this codebase keeps producing.
std::int64_t schedule_work(double now, double days);

// The longest span in whole days, from `now`, whose work is at most `limit` steps. `work` is
// non-decreasing in `days` -- a longer window contains every crossing a shorter one did --
// so this is a bisection that allocates nothing. Rounded DOWN, so a caller that sends the
// value straight back is not refused again by a fraction of a day.
double schedule_longest_span(double now, std::int64_t limit);

struct SchedulePlan {
    TimeBand band = TimeBand::Instant;
    double elapsed_days = 0.0;
    double from_day = 0.0;
    double to_day = 0.0;
    std::int64_t ages = 0;  // zero below the age band
    std::int64_t steps = 0;
    std::vector<std::pair<Cadence, std::int64_t>> by_cadence;
};
SchedulePlan schedule_plan(double now, double days);

// ---------------------------------------------------------------------------
// The clock.
// ---------------------------------------------------------------------------

struct WorldClock {
    int version = kClockVersion;
    // A real number of days since founding year 0 on the moon's calendar, so a tick and the
    // almanac cannot disagree about what time it is.
    double day = 0.0;
    std::int64_t age = 0;
    double epoch_day = 0.0;
    // "founding.end_year" or "epoch". NOT refreshed once a clock exists: it is provenance,
    // saying how the FIRST clock was derived, not state.
    std::string derived_from;
};

struct ClockSource {
    // Null on a world that has never been advanced.
    const WorldClock* existing = nullptr;
    bool has_founding_end_year = false;
    long long founding_end_year = 0;
    std::int64_t ages = 0;
};

// A world with no clock is read at its founding year, which is the same day the age lottery
// already samples, so adopting a clock does not move a world's position in time.
WorldClock world_clock_of(const ClockSource& source);

// The absolute year a pass that takes one is given. **This is the number the beast movement
// pass means by `year`** -- it takes it as a caller input precisely because the clock did not
// exist when it was ported. On a world with no clock, `world_clock_of` puts the founding end
// year on the clock, so this answers the founding end year and the two paths agree by
// construction rather than by a second fallback written somewhere else.
long long clock_year(const WorldClock& clock);

// ---------------------------------------------------------------------------
// The ley field.
// ---------------------------------------------------------------------------

// The band ley intensities drift across ONE AGE TRANSITION, owned by the age boundary and
// unchanged here.
inline constexpr double kLeyAgeLow = 0.65;
inline constexpr double kLeyAgeHigh = 1.35;

// A year step must compose to that same band over an age rather than applying an age-sized
// shock once per year, so these are the age bounds to the power of one over the age's length
// in years.
//
// **The exponent is derived from kAgeYears and must never be written as a literal.** The two
// numbers are one contract: the pair that composes correctly over a CENTURY composes to
// 0.65^50 -- about 2e-10 -- over the five thousand years an age now lasts, WHICH IS THE
// MAGIC LAYER DRAINING TO ZERO THROUGH ORDINARY TICKING, silently, with every individual
// step inside its stated bounds and nothing out of range anywhere to notice. Functions
// rather than constants so the derivation is code that a test can re-run.
double ley_year_low();
double ley_year_high();

// One year of drift, keyed on the ABSOLUTE year index. Nodes first, then edges, each in list
// order, all drawing from one per-school generator: reordering silently reassigns every
// multiplier. Clamped at 4 with NO lower bound -- the multiplier is strictly positive, so
// intensity approaches but never reaches zero.
//
// Returns whether the field actually moved. **Claiming it unconditionally is the defect**:
// it sent a magicless world into the network evaluation, which then indexed a block that was
// not there and died on a bare lookup rather than any refusal this boundary knows how to
// make.
bool ley_drift(std::map<std::string, LeyNetwork>& networks, std::uint32_t seed,
               std::int64_t index, bool magic_enabled);

// The fixed sentence the surge block carries, so every writer emits the same bytes.
inline constexpr std::string_view kLunarSurgeFormula =
    "surged_strength = strength * tide_school(day)";

struct LunarSurge {
    double day = 0.0;
    // School name -> factor, FOR THE KNOWN EIGHT ONLY and in school-table order. A hidden
    // school is absent, takes no surge and keeps no surged_strength: alien magic does not
    // answer to this world's sky, and that asymmetry is the twelve-school contract rather
    // than an oversight.
    std::vector<std::pair<std::string_view, double>> factors;
    std::string_view formula = kLunarSurgeFormula;
};

// Recompute the lunar surge at `day`. Closed form: no state, no draw, free either way.
//
// The tide primitive takes a WHOLE day, so a sub-day span reads the same sky it read at the
// start of the day. That is a deliberate narrowing of the reference, which interpolated the
// three lunar cycles on a real-valued day; the sky contract here is integer-day and one
// implementation of it is the point.
LunarSurge apply_surge(std::map<std::string, LeyNetwork>& networks, const Moon& moon, double day);

// ---------------------------------------------------------------------------
// Cost reporting.
//
// **Every constant below is INHERITED FROM THE PYTHON AND HAS NOT BEEN RE-MEASURED IN C++.**
// They describe the wall clock and peak working set of an implementation that is being
// deleted, on a contended machine, and the C++ copy is far cheaper. The STRUCTURE is what is
// ported: two terms, a fixed cost per call and a marginal cost per step, both scaled by cell
// count, with a precision note saying steps are a proxy. The numbers must be replaced by
// measurement once the tick is wired and can be run; the emitted basis says so rather than
// laundering a stale figure as a fresh one.
// ---------------------------------------------------------------------------

inline constexpr double kAgeBasisSize = 33.0;
inline constexpr double kAgeBasisSeconds = 181.8;
// Memory has its OWN reference size, and that is deliberate rather than untidy: the only
// figure available was peak working set, and the machine was trimming working sets under
// load, so the size-33 reading came back smaller per cell than the size-17 one -- which is
// the instrument and not the world. Pretending both quantities share one reference would
// state a precision neither has.
inline constexpr double kAgeBasisMbSize = 17.0;
inline constexpr double kAgeBasisMb = 566.0;

inline constexpr double kTickBasisSize = 17.0;
inline constexpr double kTickBasisCopySeconds = 1.0;
inline constexpr double kTickBasisStepSeconds = 0.0000142;
inline constexpr double kTickBasisCopyMb = 51.0;
inline constexpr double kTickBasisStepBytes = 132.0;

struct TickCostEstimate {
    std::int64_t steps = 0;
    double elapsed_days = 0.0;
    double elapsed_years = 0.0;
    double total_seconds = 0.0;
    double copy_seconds = 0.0;
    double per_step_seconds = 0.0;
    double peak_mb = 0.0;
    std::string basis;
    std::string precision;
    bool measured = false;  // false until the C++ tick has been timed
};
TickCostEstimate tick_cost_estimate(double size, double days, std::int64_t steps);

struct AgeCostEstimate {
    std::int64_t ages = 0;
    double per_age_seconds = 0.0;
    double total_seconds = 0.0;
    double peak_mb = 0.0;
    std::string peak_mb_precision;
    std::string basis;
    // Whether THIS WORLD's own cost was used, not whether anything has ever been timed.
    bool measured = false;
};

struct AgeCostBasis {
    // Seconds this world's own last AGE advance took, and how many ages that call carried.
    // The divisor is the ages that call actually advanced: reading the total raw reported a
    // ten-age run as the cost of one.
    //
    // **The reference found that operation with a filter that also matched a TICK's step
    // count**, so on any world ticked since its last age advance the divisor was a step
    // count -- potentially 200,000 -- and the per-age figure came back near zero while
    // claiming to be measured. Taking the pair explicitly is the fix: a caller that cannot
    // name the age operation has no measurement to offer.
    std::optional<double> recorded_seconds;
    std::int64_t recorded_ages = 1;
};
AgeCostEstimate age_cost_estimate(double size, std::int64_t ages, const AgeCostBasis& basis = {});

// ---------------------------------------------------------------------------
// Ceilings.
// ---------------------------------------------------------------------------

// A run has to end: each age rebuilds every derived layer, so an unbounded span is an
// unbounded loop. Kept BESIDE the work ceiling rather than replaced by it, because a work
// ceiling and an ages ceiling refuse different things -- one prices a tick, the other bounds
// a loop of whole-world rebuilds this module does not execute itself. The number was chosen
// when fifty ages was five thousand years and is now two hundred and fifty thousand; whether
// it is still right is deliberately open.
inline constexpr std::int64_t kMaxAgesPerRequest = 50;

// The most work one request executes as a tick before it is REPORTED rather than attempted.
//
// **An absolute count, and never a share of an age.** Writing it as a fraction of kAgeDays
// would re-couple the guard to the band boundary, which is the defect itself: how much work
// a machine can afford does not change when the calendar is redefined. It also does NOT
// scale with the world, deliberately -- a ceiling read out of the cost model would move
// every time somebody re-measured, and the same request would be refused on Monday and run
// on Tuesday with no constant edited.
//
// **It advises; it does not forbid**: commit proceeds.
inline constexpr std::int64_t kMaxTickSteps = 200000;

// At most this many player leyline edits in one request. Each edit copies a whole network,
// which is why the bound is two orders below the other two.
//
// **Player leyline edits are NOT part of this port and `TickRequest` carries no field for
// them.** The edit itself belongs to the leyline history module -- it validates a new node's
// unit direction and a unique id, and refuses an intensity outside [0, 4] -- and that module
// is not ported. The bound is recorded here because it is this request boundary's, so
// whoever adds the field does not have to rediscover it. What a resolved hook declares for
// itself is a different thing and IS ported: see quest_apply_resolutions.
inline constexpr std::size_t kMaxLeylineEdits = 128;

// ---------------------------------------------------------------------------
// The tick.
// ---------------------------------------------------------------------------

// One entry per call that ADVANCES the world. An estimate and a refusal record nothing.
struct TimeOperation {
    int api_version = kTimeApiVersion;
    std::string_view kind = "time";
    TimeBand band = TimeBand::Instant;
    double elapsed_days = 0.0;
    double from_day = 0.0;
    double to_day = 0.0;
    std::int64_t steps = 0;
    std::int64_t ages = 0;
    int resolutions = 0;
};

// The fixed sentence. Rewording it would make the key flip between two strings depending on
// which call ran last, which is churn in the bytes of a document whose reproducibility is a
// contract.
inline constexpr std::string_view kReplaySentence =
    "Persist this returned world for subsequent calls. Config reproduces genesis; operation "
    "history records subsequent advances, player leyline edits and quest resolutions.";

// The slice of a world a tick reads and writes. World-in, world-out by value: the caller's
// copy is never touched, so a throw leaves no half-advanced world anywhere. That guarantee
// is nearly free in this shape and expensive to retrofit once anything mutates in place, so
// it is taken here rather than later.
struct TickState {
    bool has_clock = false;
    WorldClock clock;

    // A world that has never been ticked carries no board. No quests and no quest lifecycle
    // are different facts.
    bool has_board = false;
    QuestBoard board;

    bool has_magic = false;
    std::map<std::string, LeyNetwork> networks;
    bool has_surge = false;
    LunarSurge surge;

    std::vector<std::string> warnings;
    std::int64_t ages = 0;  // the length of the age list; the clock's age is read from it
    std::vector<TimeOperation> operations;
    std::string replay;
};

struct TickRequest {
    TimeElapsed elapsed;
    // Turns the two REPORTED refusals -- the work ceiling and the age gate -- into execution.
    bool commit = false;

    // seed, size, phase, world_recipe, magic_enabled. **The seed here is the GENESIS seed
    // and every derived stream is a child of it. It must survive the call unchanged**: the
    // beast movement pass keys its year forage draw on the genesis seed precisely so a
    // monthly cadence cannot re-roll a per-year decision, and stepping the world's own seed
    // would make that draw per-month noise instead.
    Config config;

    bool has_astrology = false;
    Moon moon;

    bool has_founding_end_year = false;
    long long founding_end_year = 0;

    // heroes.quest_hooks IN LIST ORDER -- see QuestStepRequest::hooks.
    std::span<const QuestHook> hooks;
    bool heroes_ok = true;
    QuestCast cast;
    QuestWorldView targets;

    // What the player did BEFORE this span, so they are applied before it runs. Applying
    // them at the end meant the world could expire a quest during the very span the player
    // spent completing it, and then refuse the completion.
    std::span<const QuestResolution> resolutions;

    // Why this world cannot advance an age, or empty if it can. **Asked rather than
    // restated.** The reference once hard-coded a grid ceiling here and named the real gate
    // as its source; the ceiling had already been raised, so a caller debugging the refusal
    // found nothing at the named location and a whole band of worlds was refused a span it
    // would have advanced happily. Passing the real predicate's answer in is the only
    // version of this that cannot go stale again.
    std::string age_blocked;
};

// A refusal the caller is TOLD about rather than thrown at. A caller that asked for five
// thousand years deserves the estimate AND the reason together; an exception throws away the
// more useful half.
struct TickBlock {
    std::string_view code;  // STATE_CAPACITY | INVALID_INPUT
    std::string message;
    std::string_view field;
    std::int64_t received = 0;
    std::int64_t limit = 0;
    // The span that would fit, in days. A refusal that only named the step limit would be
    // telling a caller to clamp a field they never sent -- a unit this API does not accept.
    // Measured from the CALLER'S OWN clock rather than from where the ages would have ended,
    // so sending the value straight back is guaranteed to be accepted.
    std::optional<double> clamp_days;
    std::string note;
    bool retry = true;
};

struct TickReport {
    int api_version = kTimeApiVersion;
    TimeBand band = TimeBand::Instant;
    double elapsed_days = 0.0;
    double from_day = 0.0;
    double to_day = 0.0;
    std::int64_t steps = 0;
    std::int64_t ages = 0;
    std::int64_t age_days = 0;
    std::vector<std::pair<Cadence, std::int64_t>> by_cadence;
    bool committed = false;
    int resolutions = 0;
    // Whether the base ley field moved, and therefore whether the caller must re-run the
    // expensive globe pass. At most once per request, and only when it actually moved.
    bool leylines_evaluated = false;
    std::optional<TickBlock> blocked;
    std::optional<TickCostEstimate> tick_cost;
    std::optional<AgeCostEstimate> age_cost;
    std::string note;
};

// A pass this module schedules but does not own. The caller runs it.
struct CadenceCall {
    Cadence cadence = Cadence::Encounters;
    double day = 0.0;
    std::int64_t index = 0;
    // child_seed(config.seed, "time-<name>", index). A pass is a deterministic function of
    // the world and the seed, so a TIME-DERIVED seed is what makes it move at all -- and it
    // is keyed on the absolute step index, never on the call, so chunking cannot change
    // which world it draws.
    //
    // This is what a pass's own `seed` field takes. A pass that ALSO keeps a genesis seed --
    // the beast movement pass does -- takes `TickRequest::config.seed` for that one, not
    // this. Handing this value to both makes a per-year decision re-roll every month.
    //
    // The villain outlook is scheduled here too and draws nothing at all: it is a reading of
    // present ground, so its seed is carried for uniformity and goes unused. **A villain
    // rises, holds or falls at an age boundary and nowhere else** -- raising one mid-tick
    // would let a villain be raised by its own damage -- so the outlook refreshes and the
    // cast does not.
    std::uint32_t seed = 0;
};

struct TickResult {
    // The advanced slice. The argument is untouched on every path, including every refusal.
    TickState state;
    TickReport report;
    // Every step the span crossed, in absolute-time order: what the report counts.
    std::vector<ScheduleStep> executed;
    // The subset this module does not execute, because the passes are not its own. In the
    // same absolute-time order, so a caller that runs them in sequence reproduces the
    // reference's interleaving. The quest sweep and the ley drift are absent because this
    // module runs them; neither reads a block any of these writes, so running them here
    // rather than in the loop cannot change the answer.
    std::vector<CadenceCall> pending;
    double elapsed_ms = 0.0;
};

// Throws std::domain_error when the world cannot tick at all: a recipe other than 3, a phase
// below the creatures stage, or a world with no sky. **Deliberately not the age gate** --
// that answers a different question and carries its own grid ceiling, and a world that
// cannot turn an age can still move its encounters and its quests.
void validate_time_world(const TickRequest& request);

// Advance a world slice by an elapsed span.
//
// Throws std::invalid_argument on a malformed span and std::domain_error on a world that
// cannot tick or a resolution the world refuses. The work ceiling and the age gate are
// RETURNED, not thrown, with `report.committed == false` and the state otherwise unchanged.
TickResult advance_time(const TickState& state, const TickRequest& request);

// Keep a warnings list a set of statements rather than a tally of how often a pass ran.
// Order is preserved and only exact repeats are dropped -- a warning this tick did not
// produce is never removed. The seasonal food pass APPENDS its warning rather than replacing
// it, so run on a cadence it turns a list of limitations into a counter that would differ
// between two chunkings of one span purely by how many calls were made.
//
// **Call it after every pending pass.** `advance_time` does not, because it runs none of
// them: a world whose warnings no pass touched must come back with the bytes it arrived
// with. A future pass that appends to a shared list needs the same treatment.
void dedupe_warnings(std::vector<std::string>& warnings);

}  // namespace fwg
