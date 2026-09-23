// The clock, and the schedule that is the whole of its determinism argument.
//
// The load-bearing assertions here are about CHUNKING: that the same span cut two ways runs
// the same steps and reaches the same world. That is the defect class this subsystem is
// shaped to prevent, it is invisible until somebody chunks differently, and by then a saved
// world has been advanced across it -- so it is asserted directly rather than described.
//
// `schedule_work` is checked against `schedule_steps` itself rather than against a formula,
// because a counter that answers a slightly different question than the thing it stands in
// for is the near-miss that keeps being mistaken here for an answer.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "fwg/time.hpp"

using namespace fwg;

namespace {

constexpr double kYear = static_cast<double>(kDaysPerYear);

std::string name_of(Cadence cadence) { return std::string(to_string(cadence)); }

std::int64_t count_of(const std::vector<std::pair<Cadence, std::int64_t>>& counts, Cadence which) {
    for (const auto& [cadence, count] : counts) {
        if (cadence == which) return count;
    }
    return 0;
}

int steps_of(const std::vector<ScheduleStep>& steps, Cadence which) {
    int count = 0;
    for (const ScheduleStep& step : steps) {
        if (step.cadence == which) ++count;
    }
    return count;
}

// A step's identity, so two chunkings can be compared as sets rather than as lists.
std::string key_of(const ScheduleStep& step) {
    return name_of(step.cadence) + '@' + std::to_string(step.index);
}

std::map<std::string, LeyNetwork> two_networks(double intensity) {
    std::map<std::string, LeyNetwork> networks;
    for (const char* name : {"fire", "blood"}) {  // one known, one hidden
        LeyNetwork net;
        net.name = name;
        net.strength = 2.0;
        net.nodes.push_back(LeyNode{std::string(name) + "-n1", Vec3{1.0, 0.0, 0.0}, intensity});
        net.nodes.push_back(LeyNode{std::string(name) + "-n2", Vec3{0.0, 1.0, 0.0}, intensity});
        net.edges.push_back(LeyEdge{std::string(name) + "-e1", 0, 1, intensity});
        networks.emplace(name, net);
    }
    return networks;
}

QuestHook hook_of(std::string id, std::string giver) {
    QuestHook h;
    h.hook_id = std::move(id);
    h.giver_uid = std::move(giver);
    h.target = "city-1";
    h.verb = "defend";
    return h;
}

// Everything a tick needs, with the spans' storage owned here so lifetimes are obvious.
struct Fixture {
    std::vector<QuestHook> hooks{hook_of("hook-a", "hero-1"), hook_of("hook-b", "hero-2")};
    std::vector<QuestGiver> heroes{QuestGiver{"hero-1", true}, QuestGiver{"hero-2", true}};
    std::vector<std::string> settlement_uids{"city-1"};
    std::vector<QuestResolution> resolutions;
    Config config;
    Moon moon = seed_moon(42);

    Fixture() {
        config.seed = 42;
        config.size = 33;
        config.phase = 16;
        config.world_recipe = 3;
        config.magic_enabled = 1;
    }

    TickRequest request(TimeElapsed elapsed) const {
        TickRequest r;
        r.elapsed = elapsed;
        r.config = config;
        r.has_astrology = true;
        r.moon = moon;
        r.hooks = hooks;
        r.cast.heroes = heroes;
        r.targets.settlement_uids = settlement_uids;
        r.resolutions = resolutions;
        return r;
    }
};

TickState fresh_state() {
    TickState state;
    state.has_magic = true;
    state.networks = two_networks(2.0);
    return state;
}

// The content two chunkings must agree on: the clock, every quest row and every ley
// intensity. Timings and the operation log are excluded -- two calls are honestly two
// operations, and a wall clock is the only reason two runs of one seed differ.
std::string digest_of(const TickState& state) {
    std::string out = "day=" + std::to_string(state.clock.day) +
                      " age=" + std::to_string(state.clock.age);
    for (const QuestRecord& q : state.board.quests) {
        out += " |" + q.quest_id + ':' + std::string(to_string(q.state)) + ':' +
               std::to_string(q.expires_day) + ':' +
               (q.closed_reason.has_value() ? std::string(to_string(*q.closed_reason)) : "-");
    }
    for (const auto& [name, net] : state.networks) {
        out += " #" + name;
        for (const LeyNode& node : net.nodes) out += ':' + std::to_string(node.intensity);
        for (const LeyEdge& edge : net.edges) out += ';' + std::to_string(edge.intensity);
        out += '/' + std::to_string(net.surged_strength);
    }
    return out;
}

}  // namespace

// --- the cadence table ------------------------------------------------------

TEST(every_cadence_carries_the_authored_period_and_the_authored_rank) {
    // The rank breaks ties between cadences firing on the same day AND is dependency order:
    // routes read the block the nomad pass replaced, and the encounter index reads both the
    // nomads and the beast movements.
    const std::span<const Cadence> all = cadences();
    check::equal(all.size(), std::size_t{9}, "nine cadences");
    const std::pair<const char*, double> authored[] = {
        {"quests", 1.0},          {"ley_drift", 360.0},      {"seasonal_food", 90.0},
        {"nomads", 360.0},        {"nomad_routes", 30.0},    {"beast_movements", 30.0},
        {"encounters", 30.0},     {"nomad_effects", 360.0},  {"villain_outlook", 360.0}};
    for (std::size_t i = 0; i < all.size(); ++i) {
        check::equal(name_of(all[i]), std::string(authored[i].first), "cadence at rank " +
                                                                         std::to_string(i));
        check::close(cadence_period(all[i]), authored[i].second, 1e-12,
                     std::string(authored[i].first) + "'s period");
        check::equal(static_cast<int>(all[i]), static_cast<int>(i), "the rank IS the position");
    }
    // A pass that ACCUMULATES must never coalesce: the quest sweep carries expiry forward and
    // the ley drift is a compounding random walk, so coalescing either would silently discard
    // every step but the last.
    check::is_true(!cadence_coalesces(Cadence::Quests), "quests never coalesce");
    check::is_true(!cadence_coalesces(Cadence::LeyDrift), "ley drift never coalesces");
    int coalescing = 0;
    for (const Cadence c : all) {
        if (cadence_coalesces(c)) ++coalescing;
    }
    check::equal(coalescing, 7, "the seven passes that replace their block wholesale");
}

TEST(the_calendar_is_the_moons_and_an_age_is_five_thousand_years) {
    // Taken from the astrology header rather than restated: a tick and the almanac
    // disagreeing about what a year is would be invisible in both.
    check::equal(kDaysPerYear, 360, "days in a year");
    check::close(kSeasonDays, 90.0, 1e-12, "days in a season");
    check::close(kAgeYears, 5000.0, 1e-12, "years in an age");
    check::close(kAgeDays, 1800000.0, 1e-9, "days in an age");
    check::close(kAgeDays, kAgeYears * kDaysPerYear, 1e-9, "the age derived from the calendar");
}

// --- the window -------------------------------------------------------------

TEST(a_crossing_window_is_half_open_at_the_start_and_closed_at_the_end) {
    // Half-open at the start so a step is never executed twice by two adjacent calls, and
    // closed at the end so a step landing exactly on the boundary is executed once.
    const ScheduleRange exact = schedule_crossings(Cadence::NomadRoutes, 0.0, 30.0);
    check::equal(exact.count(), std::int64_t{1}, "a step landing exactly on the boundary");
    check::equal(exact.first, std::int64_t{1}, "index 1, the boundary at day 30");

    const ScheduleRange after = schedule_crossings(Cadence::NomadRoutes, 30.0, 30.0);
    check::equal(after.first, std::int64_t{2}, "the next window starting past it");
    check::equal(after.count(), std::int64_t{1}, "and holding exactly one");

    check::is_true(schedule_crossings(Cadence::NomadRoutes, 30.0, 0.0).empty(),
                   "a zero span to cross nothing");
    check::is_true(schedule_crossings(Cadence::LeyDrift, 0.0, 359.0).empty(),
                   "a year's cadence not to fire before its year is out");
}

TEST(adjacent_calls_execute_every_index_exactly_once_between_them) {
    // The whole invariant in one assertion: a step belongs to exactly one window, decided by
    // its own absolute index, so no cut of a span can create or destroy one.
    const double now = 137.25;
    const double whole = 900.0;
    for (const double cut : {1.0, 89.9, 90.0, 400.0, 719.0, 899.0}) {
        const std::vector<ScheduleStep> together = schedule_steps(now, whole);
        std::vector<ScheduleStep> apart = schedule_steps(now, cut);
        const std::vector<ScheduleStep> rest = schedule_steps(now + cut, whole - cut);
        apart.insert(apart.end(), rest.begin(), rest.end());

        std::multiset<std::string> one, two;
        for (const ScheduleStep& s : together) one.insert(key_of(s));
        for (const ScheduleStep& s : apart) two.insert(key_of(s));

        // The coalesced cadences legitimately run once PER CALL, so two calls run them
        // twice. Every accumulating cadence must match exactly, and the coalesced ones must
        // never MISS an index -- only repeat the survivor.
        for (const Cadence c : cadences()) {
            const int whole_count = steps_of(together, c);
            const int apart_count = steps_of(apart, c);
            if (cadence_coalesces(c)) {
                check::is_true(apart_count >= whole_count,
                               name_of(c) + " never to lose a crossing to a cut");
            } else {
                check::equal(apart_count, whole_count,
                             name_of(c) + " to run the same steps however the span is cut");
            }
        }
        check::is_true(together.size() > 900, "the span to cross enough to be worth cutting");
    }
}

TEST(work_answers_exactly_the_question_steps_answers) {
    // Asserted against the enumerator itself rather than against a formula. A counter that
    // answers a slightly different question than the thing it stands in for is an answer to
    // nothing.
    int sampled = 0;
    int large = 0;
    for (const double now : {0.0, 0.5, 29.999, 30.0, 359.0, 360.0, 1234.5, 100000.0}) {
        for (const double days : {0.0, 0.5, 1.0, 29.0, 30.0, 90.0, 360.0, 721.0, 5000.0}) {
            const std::int64_t counted = schedule_work(now, days);
            const std::int64_t built = static_cast<std::int64_t>(schedule_steps(now, days).size());
            check::equal(counted, built, "work at now=" + std::to_string(now) + " days=" +
                                             std::to_string(days));
            ++sampled;
            if (built > 1000) ++large;
        }
    }
    check::equal(sampled, 72, "every sample to have been taken");
    check::is_true(large >= 8, "some samples to be large enough that a formula could drift");
}

TEST(a_coalesced_cadence_runs_once_however_many_crossings_it_had) {
    // Thirty years runs one encounter index instead of 360, because every earlier one would
    // be overwritten by it.
    const auto counts = schedule_by_cadence(0.0, 30.0 * kYear);
    check::equal(count_of(counts, Cadence::Encounters), std::int64_t{1}, "one encounter index");
    check::equal(count_of(counts, Cadence::Nomads), std::int64_t{1}, "one nomad pass");
    check::equal(count_of(counts, Cadence::Quests), std::int64_t{10800}, "a sweep every day");
    check::equal(count_of(counts, Cadence::LeyDrift), std::int64_t{30}, "a drift every year");

    // The survivor is the LAST crossing, not the first: an earlier one would be the world
    // thirty years ago.
    const std::vector<ScheduleStep> steps = schedule_steps(0.0, 30.0 * kYear);
    double encounter_day = -1.0;
    for (const ScheduleStep& s : steps) {
        if (s.cadence == Cadence::Encounters) encounter_day = s.day;
    }
    check::close(encounter_day, 10800.0, 1e-9, "the last monthly boundary in the span");
}

TEST(a_band_names_how_big_a_span_is_and_does_not_decide_which_cadences_run) {
    // A month-long tick that straddles a year boundary MUST run the year's cadences. Filtering
    // by band was a silent, permanent data-loss bug: the window is half-open at the start, so
    // a crossing skipped because this call was too short is never revisited by any later call.
    const double eve = 350.0;
    const std::vector<ScheduleStep> month = schedule_steps(eve, 30.0);
    check::is_true(time_band(30.0) == TimeBand::Day, "a month to be a day-band span");
    check::equal(steps_of(month, Cadence::LeyDrift), 1, "a year's ley drift inside a month tick");
    check::equal(steps_of(month, Cadence::Nomads), 1, "and the year's nomad pass");
    check::equal(steps_of(month, Cadence::VillainOutlook), 1, "and the year's outlook");

    // And a sub-day span that crosses a day boundary runs that day's sweep. Three seconds
    // normally runs nothing because three seconds normally CROSSES nothing.
    check::is_true(time_band(0.6) == TimeBand::Instant, "a sub-day span is the instant band");
    check::equal(steps_of(schedule_steps(0.9, 0.2), Cadence::Quests), 1,
                 "an instant-band span that crosses a day to sweep it");
    check::equal(schedule_steps(0.1, 0.2).size(), std::size_t{0},
                 "and one that crosses nothing to run nothing");

    check::is_true(time_band(0.99) == TimeBand::Instant, "instant below one day");
    check::is_true(time_band(1.0) == TimeBand::Day, "day at one");
    check::is_true(time_band(90.0) == TimeBand::Season, "season at ninety");
    check::is_true(time_band(720.0) == TimeBand::Long, "long at two years");
    check::is_true(time_band(kAgeDays) == TimeBand::Age, "age at one age");
    check::is_true(time_band(kAgeDays - 1.0) == TimeBand::Long, "and long one day below it");
}

TEST(steps_come_back_in_absolute_time_order_with_ties_broken_by_dependency_rank) {
    // **Ordering by time rather than by subsystem is the second half of chunk-independence.**
    // A subsystem-major order would run a year of one pass before a month of another, and
    // the two orders diverge the moment a call boundary lands between them. It also breaks
    // the dependency chain on any single day: the route pass reads and writes the block the
    // nomad pass replaced, and the encounter index reads both the nomads and the beast
    // movements, so a wrong tie-break indexes bands that no longer exist.
    //
    // This is asserted on the step list itself because the tick hands that order straight to
    // the caller as `pending`, and the caller runs those six passes in it.
    const std::vector<ScheduleStep> steps = schedule_steps(0.0, 2.0 * kYear);
    check::is_true(steps.size() > 700, "a span long enough to have an order at all");

    int ties = 0;
    std::set<std::string> distinct;
    for (std::size_t i = 1; i < steps.size(); ++i) {
        const ScheduleStep& before = steps[i - 1];
        const ScheduleStep& after = steps[i];
        check::is_true(before.day <= after.day, "steps to be non-decreasing in absolute day");
        if (before.day == after.day && before.cadence != after.cadence) {
            ++ties;
            check::is_true(static_cast<int>(before.cadence) < static_cast<int>(after.cadence),
                           "a tie on one day broken by dependency rank, " +
                               name_of(before.cadence) + " before " + name_of(after.cadence));
        }
        distinct.insert(name_of(after.cadence));
    }
    // Non-vacuity, twice over: a span with one cadence, or with no two cadences landing on
    // one day, would pass the loop above without testing either rule.
    check::is_true(distinct.size() >= 7, "several cadences in the span");
    check::is_true(ties >= 8, "several days carrying more than one cadence");

    // And the dependency chain itself, on a day every cadence lands on: nomads, then routes,
    // then beast movements, then encounters, then the nomad write-backs. It has to be the
    // LAST day of the span, because a coalesced cadence only runs its final crossing.
    std::vector<std::string> on_the_year;
    for (const ScheduleStep& step : schedule_steps(0.0, kYear)) {
        if (step.day == kYear) on_the_year.push_back(name_of(step.cadence));
    }
    const std::vector<std::string> expected{"quests",          "ley_drift",      "seasonal_food",
                                            "nomads",          "nomad_routes",   "beast_movements",
                                            "encounters",      "nomad_effects",  "villain_outlook"};
    check::equal(on_the_year.size(), expected.size(), "every cadence landing on the year mark");
    for (std::size_t i = 0; i < on_the_year.size() && i < expected.size(); ++i) {
        check::equal(on_the_year[i], expected[i], "the order on a day they all share");
    }
}

TEST(the_passes_the_tick_hands_back_keep_that_same_order) {
    // The tick executes the quest sweep and the ley drift itself and hands the other seven
    // over. Neither of the two it runs reads a block any of the seven writes, so running
    // them here rather than inside the loop cannot change the answer -- but the seven must
    // reach the caller in the order the schedule put them in, or the dependency chain breaks
    // in the caller's loop instead of in this one.
    const Fixture f;
    const TickResult out = advance_time(fresh_state(), f.request({TimeUnit::Years, 2.0}));
    check::is_true(out.pending.size() >= 7, "passes to have been handed over at all");
    for (std::size_t i = 1; i < out.pending.size(); ++i) {
        check::is_true(out.pending[i - 1].day <= out.pending[i].day,
                       "handed-over passes in absolute-time order");
        if (out.pending[i - 1].day == out.pending[i].day) {
            check::is_true(static_cast<int>(out.pending[i - 1].cadence) <
                               static_cast<int>(out.pending[i].cadence),
                           "and ties still broken by dependency rank");
        }
    }
    // The two it runs itself are absent from the handover, and the seven it does not are
    // present -- a caller that ran `pending` and then also swept quests would sweep twice.
    std::set<std::string> handed;
    for (const CadenceCall& call : out.pending) handed.insert(name_of(call.cadence));
    check::equal(handed.count("quests"), std::size_t{0}, "the quest sweep not handed over");
    check::equal(handed.count("ley_drift"), std::size_t{0}, "nor the ley drift");
    check::equal(handed.size(), std::size_t{7}, "and all seven of the rest handed over");
}

TEST(by_cadence_is_ordered_by_name_which_is_not_the_rank_order) {
    // Alphabetical by cadence name, and it lands in the report that way.
    const auto counts = schedule_by_cadence(0.0, 2.0 * kYear);
    check::is_true(counts.size() >= 5, "enough cadences to have an order at all");
    check::is_true(!counts.empty(), "a non-empty listing to index");
    for (std::size_t i = 1; i < counts.size(); ++i) {
        check::is_true(name_of(counts[i - 1].first) < name_of(counts[i].first),
                       "by_cadence sorted by name");
    }
    // Non-vacuity: the two orders really do differ, so a port that emitted rank order would
    // fail the loop above rather than passing it by coincidence.
    check::is_true(counts[0].first != Cadence::Quests,
                   "the name order to put something other than rank zero first");
    check::equal(name_of(counts[0].first), std::string("beast_movements"), "first by name");
}

TEST(longest_span_is_the_largest_whole_day_span_that_fits_under_the_limit) {
    for (const double now : {0.0, 137.5, 4000.0}) {
        const double fits = schedule_longest_span(now, kMaxTickSteps);
        check::is_true(fits == std::floor(fits), "a whole number of days");
        check::is_true(schedule_work(now, fits) <= kMaxTickSteps, "the answer to fit");
        check::is_true(schedule_work(now, fits + 1.0) > kMaxTickSteps, "and to be the largest");
        check::is_true(fits < kAgeDays, "the ceiling to actually bite below the band's maximum");
    }
    // A limit nothing can exceed returns the whole band rather than bisecting to it.
    check::close(schedule_longest_span(0.0, 100000000), kAgeDays, 1e-9,
                 "an unreachable limit to permit the whole band");
}

TEST(the_work_ceiling_is_an_absolute_count_and_not_a_share_of_an_age) {
    // Writing it as a fraction of an age would re-couple the guard to the band boundary,
    // which is the defect itself: how much a machine can afford does not change when the
    // calendar is redefined. What that buys is checked here in the units a caller thinks in.
    check::equal(kMaxTickSteps, std::int64_t{200000}, "the ceiling");
    const double fits = schedule_longest_span(0.0, kMaxTickSteps);
    check::is_true(fits > 100.0 * kYear,
                   "an order of magnitude above the century a game actually drives");
    check::is_true(fits < kAgeDays / 8.0,
                   "and an order of magnitude below the band's own maximum");
    check::is_true(schedule_work(0.0, 100.0 * kYear) < kMaxTickSteps,
                   "a century to pass the ceiling unremarked");
}

// --- the clock --------------------------------------------------------------

TEST(a_world_with_no_clock_is_read_at_its_founding_year) {
    // Adopting a clock must not move a world's position in time: the founding end year is
    // the same day the age lottery already samples. **This is also the number the beast
    // movement pass means by `year`**, which it takes as a caller input precisely because
    // the clock did not exist when it was ported -- so the two agree by construction rather
    // than by a second fallback written somewhere else.
    ClockSource source;
    source.has_founding_end_year = true;
    source.founding_end_year = 137;
    source.ages = 2;
    const WorldClock clock = world_clock_of(source);
    check::close(clock.day, 137.0 * kYear, 1e-9, "the founding year in days");
    check::equal(clock_year(clock), 137LL, "and read back as that year");
    check::equal(clock.age, std::int64_t{2}, "the age ordinal from the age list");
    check::equal(clock.derived_from, std::string("founding.end_year"), "where it came from");

    ClockSource none;
    check::equal(world_clock_of(none).derived_from, std::string("epoch"), "a world with neither");
    check::equal(clock_year(world_clock_of(none)), 0LL, "reads as year zero");
}

TEST(a_clock_is_provenance_and_is_never_rederived) {
    // Once a world carries a clock, `derived_from` keeps naming how the FIRST clock was
    // derived, forever -- including across an age advance that moved the founding end year.
    WorldClock existing;
    existing.day = 400.0;
    existing.age = 1;
    existing.derived_from = "epoch";
    ClockSource source;
    source.existing = &existing;
    source.has_founding_end_year = true;
    source.founding_end_year = 9999;
    const WorldClock clock = world_clock_of(source);
    check::close(clock.day, 400.0, 1e-12, "the existing day, not the founding year");
    check::equal(clock.derived_from, std::string("epoch"), "the original provenance");
}

// --- the ley field ----------------------------------------------------------

TEST(the_per_year_ley_bounds_compose_to_the_age_band_over_exactly_one_age) {
    // A guard against a future edit rather than against this line. The pair that composes
    // correctly over a CENTURY composes to 0.65^50 -- about 2e-10 -- over the five thousand
    // years an age now lasts, which is the magic layer draining to zero through ordinary
    // ticking with every individual step inside its stated bounds.
    check::close(std::pow(ley_year_low(), kAgeYears), kLeyAgeLow, 1e-9,
                 "the low bound composed over an age");
    check::close(std::pow(ley_year_high(), kAgeYears), kLeyAgeHigh, 1e-9,
                 "the high bound composed over an age");
    check::is_true(ley_year_low() < 1.0 && ley_year_low() > 0.9999,
                   "a year's drift to be a whisper, not a shock");
    // The literal this must never be: a century-derived exponent.
    const double century_low = std::pow(kLeyAgeLow, 1.0 / 100.0);
    check::is_true(std::pow(century_low, kAgeYears) < 1e-6,
                   "a century-derived exponent to drain the layer over an age");
    check::is_true(std::pow(ley_year_low(), kAgeYears) > 1e-6, "and the derived one not to");
}

TEST(ley_drift_moves_every_node_and_edge_and_claims_nothing_when_it_cannot) {
    std::map<std::string, LeyNetwork> networks = two_networks(2.0);
    check::is_true(ley_drift(networks, 42, 1, true), "the field to have moved");
    int moved = 0;
    for (const auto& [name, net] : networks) {
        for (const LeyNode& node : net.nodes) {
            if (node.intensity != 2.0) ++moved;
            check::between(node.intensity, 2.0 * ley_year_low(), 2.0 * ley_year_high(),
                           name + " node inside one year's band");
        }
        for (const LeyEdge& edge : net.edges) {
            if (edge.intensity != 2.0) ++moved;
        }
    }
    check::equal(moved, 6, "every node and every edge of both networks");

    // A magicless world moves nothing and SAYS SO. Claiming it moved anyway is what sent
    // such a world into the network evaluation, which then indexed a block that was not
    // there and died on a bare lookup rather than any refusal this boundary can make.
    std::map<std::string, LeyNetwork> off = two_networks(2.0);
    check::is_true(!ley_drift(off, 42, 1, false), "a magicless world to claim nothing");
    check::close(off.at("fire").nodes[0].intensity, 2.0, 1e-12, "and to move nothing");
    std::map<std::string, LeyNetwork> empty;
    check::is_true(!ley_drift(empty, 42, 1, true), "a world with no networks to claim nothing");
}

TEST(ley_drift_clamps_at_four_and_has_no_lower_bound) {
    std::map<std::string, LeyNetwork> high = two_networks(4.0);
    for (std::int64_t year = 1; year <= 20; ++year) ley_drift(high, 42, year, true);
    for (const auto& [name, net] : high) {
        for (const LeyNode& node : net.nodes) {
            check::is_true(node.intensity <= 4.0, name + " never above the ceiling");
        }
    }
    // Twenty years of the high bound is a rise of about 0.12%, so a node starting at 4
    // stays pinned rather than drifting up -- which is the clamp doing its job.
    check::close(high.at("fire").nodes[0].intensity, 4.0, 0.01, "pinned at the ceiling");

    // No lower clamp: the multiplier is strictly positive, so intensity approaches but never
    // reaches zero. Floor: it must actually have fallen, or the claim is untested.
    std::map<std::string, LeyNetwork> low = two_networks(1e-8);
    for (std::int64_t year = 1; year <= 50; ++year) ley_drift(low, 42, year, true);
    const double after = low.at("fire").nodes[0].intensity;
    check::is_true(after > 0.0, "strictly positive after fifty years");
    check::is_true(after != 1e-8, "and actually moved");
}

TEST(the_surge_writes_the_known_eight_and_leaves_a_hidden_school_entirely_alone) {
    // A hidden school takes no surge and keeps no surged strength: alien magic does not
    // answer to this world's sky, and that asymmetry is the twelve-school contract rather
    // than an oversight. Widening it would add a field to four networks in every world.
    std::map<std::string, LeyNetwork> networks = two_networks(2.0);
    const Moon moon = seed_moon(42);
    const LunarSurge surge = apply_surge(networks, moon, 137.0);

    check::equal(surge.factors.size(), std::size_t{kKnownSchools}, "a factor per known school");
    check::equal(std::string(surge.formula),
                 std::string("surged_strength = strength * tide_school(day)"), "the formula");
    check::close(surge.day, 137.0, 1e-12, "the day it was taken at");

    check::is_true(networks.at("fire").has_surge, "a known school to surge");
    check::is_true(!networks.at("blood").has_surge, "a hidden school not to");
    check::close(networks.at("blood").surged_strength, 0.0, 1e-12,
                 "and to carry no surged strength at all");
    double fire_factor = -1.0;
    for (const auto& [name, factor] : surge.factors) {
        if (name == "fire") fire_factor = factor;
    }
    check::is_true(fire_factor > 0.0, "the fire tide to be a real factor");
    check::close(networks.at("fire").surged_strength, 2.0 * fire_factor, 1e-12,
                 "strength times its own school's tide");
}

TEST(dedupe_warnings_keeps_first_seen_order_and_leaves_a_list_with_none_alone) {
    // The seasonal food pass APPENDS its warning rather than replacing it, so run on a
    // cadence it turns a list of limitations into a counter -- one that would differ between
    // two chunkings of one span purely by how many calls were made.
    std::vector<std::string> warnings{"b", "a", "b", "c", "a"};
    dedupe_warnings(warnings);
    check::equal(warnings.size(), std::size_t{3}, "three distinct statements");
    check::equal(warnings[0], std::string("b"), "first seen first");
    check::equal(warnings[1], std::string("a"), "second seen second");
    check::equal(warnings[2], std::string("c"), "third seen third");

    std::vector<std::string> none;
    dedupe_warnings(none);
    check::equal(none.size(), std::size_t{0}, "a world with no warnings to gain none");
}

// --- the tick ---------------------------------------------------------------

TEST(a_span_of_exactly_zero_moves_the_clock_and_records_nothing) {
    // This is the "what time is it every frame" path, and it is deliberately free of history
    // growth: an operation log that grows on every no-op is an unbounded record of nothing.
    const Fixture f;
    const TickState before = fresh_state();
    const TickResult out = advance_time(before, f.request({TimeUnit::Days, 0.0}));

    check::is_true(out.report.committed, "a zero span still commits");
    check::equal(out.report.steps, std::int64_t{0}, "and crosses nothing");
    check::equal(out.state.operations.size(), std::size_t{0}, "nothing recorded");
    check::equal(out.state.replay, std::string(), "and no replay sentence written");
    check::is_true(out.state.has_clock, "but the clock exists now");
    check::is_true(out.state.has_surge, "and the sky was resampled");

    // A span that crosses nothing but is NOT zero does record an operation.
    const TickResult moment = advance_time(before, f.request({TimeUnit::Days, 0.25}));
    check::equal(moment.state.operations.size(), std::size_t{1}, "one operation");
    check::equal(moment.state.operations[0].steps, std::int64_t{0}, "carrying zero steps");
    check::close(moment.state.clock.day, 0.25, 1e-12, "and the clock moved");
    check::is_true(!moment.state.replay.empty(), "with the replay sentence");
}

TEST(a_sub_day_span_carrying_a_resolution_does_not_take_the_instant_path) {
    // Keyed on "this span crosses nothing", NOT on "this span is short". The old test was
    // the band, which let a sub-day call carrying a resolution fall through and move the ley
    // field -- the opposite of what the band promised.
    Fixture f;
    const TickState before = fresh_state();
    // Open a board first, so there is something to resolve.
    const TickResult day_one = advance_time(before, f.request({TimeUnit::Days, 1.0}));
    check::equal(day_one.state.board.quests.size(), std::size_t{2}, "two offers opened");

    f.resolutions = {{"hook-a", QuestOutcome::Completed}};
    const TickResult moment = advance_time(day_one.state, f.request({TimeUnit::Days, 0.25}));
    check::equal(moment.report.steps, std::int64_t{0}, "the span still crosses nothing");
    check::equal(moment.report.resolutions, 1, "but carries a resolution");
    check::equal(moment.state.board.quests.size(), std::size_t{2}, "the board it acted on");
    check::equal(std::string(to_string(moment.state.board.quests[0].state)),
                 std::string("completed"), "which was applied");
    check::equal(moment.state.operations.size(), std::size_t{2}, "and recorded");
}

TEST(resolutions_are_applied_before_the_span_runs) {
    // Applying them at the end meant the world could expire a quest during the very span the
    // player spent completing it, and then refuse the completion.
    Fixture f;
    const TickResult opened = advance_time(fresh_state(), f.request({TimeUnit::Days, 1.0}));
    check::equal(opened.state.board.quests.size(), std::size_t{2}, "two offers to work with");
    const double window = opened.state.board.quests[0].expires_day;
    check::is_true(window < kQuestOfferMaxDays + 1.0, "a window inside the drawn range");

    f.resolutions = {{opened.state.board.quests[0].quest_id, QuestOutcome::Completed}};
    // A span long enough that the sweep WOULD have expired it partway through.
    const TickResult out =
        advance_time(opened.state, f.request({TimeUnit::Days, kQuestOfferMaxDays + 10.0}));
    check::equal(out.state.board.quests.size(), std::size_t{2}, "the same two rows");
    check::equal(std::string(to_string(out.state.board.quests[0].state)), std::string("completed"),
                 "the completion to have landed before the span could lapse it");
    // Floor: the other quest, which nobody resolved, DID lapse over the same span -- so the
    // span really was capable of expiring the first one.
    check::equal(std::string(to_string(out.state.board.quests[1].state)), std::string("expired"),
                 "the unresolved offer to have lapsed");
}

TEST(the_work_ceiling_reports_rather_than_raises_and_its_suggestion_is_accepted_verbatim) {
    const Fixture f;
    const TickState before = fresh_state();
    const TickResult blocked = advance_time(before, f.request({TimeUnit::Years, 2000.0}));

    check::is_true(blocked.report.blocked.has_value(), "a refusal document");
    check::is_true(!blocked.report.committed, "and nothing committed");
    check::equal(std::string(blocked.report.blocked->code), std::string("STATE_CAPACITY"),
                 "the code");
    check::is_true(blocked.report.blocked->received > kMaxTickSteps, "the work it priced");
    check::is_true(blocked.report.tick_cost.has_value(), "the estimate beside the reason");
    check::close(blocked.state.clock.day, 0.0, 1e-12, "the clock did not move");
    check::equal(blocked.state.operations.size(), std::size_t{0}, "and nothing was recorded");

    // The suggestion is measured from the CALLER'S OWN clock, so sending it straight back is
    // guaranteed to be accepted. A value computed from anywhere else is a suggestion about a
    // request the caller cannot make.
    check::is_true(blocked.report.blocked->clamp_days.has_value(), "a span that would fit");
    const double fits = *blocked.report.blocked->clamp_days;
    const TickResult retried = advance_time(before, f.request({TimeUnit::Days, fits}));
    check::is_true(!retried.report.blocked.has_value(), "the suggested span to be accepted");
    check::is_true(retried.report.committed, "and to run");

    // It ADVISES, it does not forbid.
    TickRequest forced = f.request({TimeUnit::Years, 2000.0});
    forced.commit = true;
    const TickResult ran = advance_time(before, forced);
    check::is_true(ran.report.committed, "commit to proceed past the ceiling");
    check::close(ran.state.clock.day, 2000.0 * kYear, 1e-6, "all the way to the asked-for day");
}

TEST(the_age_band_reports_its_cost_and_refuses_to_commit_in_this_build) {
    const Fixture f;
    const TickState before = fresh_state();
    const TickResult estimate = advance_time(before, f.request({TimeUnit::Years, 5000.0}));
    check::is_true(estimate.report.band == TimeBand::Age, "the age band");
    check::equal(estimate.report.ages, std::int64_t{1}, "one age");
    check::is_true(estimate.report.age_cost.has_value(), "the cost estimate");
    check::is_true(!estimate.report.age_cost->measured,
                   "not claiming this world's own measurement");
    check::is_true(estimate.report.age_cost->per_age_seconds > 0.0, "a real figure");
    check::is_true(!estimate.report.committed, "an estimate commits nothing");
    check::is_true(!estimate.report.blocked.has_value(), "and is not itself a refusal");
    check::close(estimate.state.clock.day, 0.0, 1e-12, "the clock untouched");

    // The ages ceiling refuses WITH the estimate attached, so a caller learns the size of
    // what it asked for rather than only that it was refused.
    const TickResult too_many = advance_time(before, f.request({TimeUnit::Years, 5000.0 * 60.0}));
    check::is_true(too_many.report.blocked.has_value(), "a refusal");
    check::equal(too_many.report.blocked->received, std::int64_t{60}, "sixty ages");
    check::equal(too_many.report.blocked->limit, kMaxAgesPerRequest, "against the ages ceiling");
    check::is_true(too_many.report.age_cost.has_value(), "with the estimate still attached");

    // The age gate is ASKED rather than restated: whatever the real predicate says comes
    // back verbatim.
    TickRequest gated = f.request({TimeUnit::Years, 5000.0});
    gated.age_blocked = "this world's grid is above the age ceiling";
    const TickResult refused = advance_time(before, gated);
    check::is_true(refused.report.blocked.has_value(), "the gate's refusal reported");
    check::equal(refused.report.blocked->message, gated.age_blocked, "in its own words");

    TickRequest committed = f.request({TimeUnit::Years, 5000.0});
    committed.commit = true;
    const TickResult cannot = advance_time(before, committed);
    check::is_true(cannot.report.blocked.has_value(),
                   "a commit this build cannot honour to be refused in the open");
    check::is_true(!cannot.report.committed, "rather than claiming it ran");
}

TEST(a_pass_seed_is_keyed_on_the_absolute_step_index_and_never_on_the_call) {
    const Fixture f;
    const TickState before = fresh_state();
    // One yearly call, and twelve monthly ones. The beast movement pass runs on a monthly
    // cadence, so its twelve seeds must be the same twelve numbers either way.
    std::vector<std::uint32_t> yearly;
    for (const CadenceCall& call : advance_time(before, f.request({TimeUnit::Days, 360.0})).pending) {
        if (call.cadence == Cadence::BeastMovements) yearly.push_back(call.seed);
    }
    std::vector<std::uint32_t> monthly;
    TickState state = before;
    for (int month = 0; month < 12; ++month) {
        const TickResult out = advance_time(state, f.request({TimeUnit::Days, 30.0}));
        for (const CadenceCall& call : out.pending) {
            if (call.cadence == Cadence::BeastMovements) monthly.push_back(call.seed);
        }
        state = out.state;
    }
    check::equal(monthly.size(), std::size_t{12}, "one beast movement call a month");
    // The yearly call coalesces to the LAST crossing, so it carries one seed -- and that one
    // must be the twelfth month's, not a fresh draw.
    check::equal(yearly.size(), std::size_t{1}, "coalesced to one call");
    check::equal(yearly[0], monthly[11], "and to the seed of the crossing it stands for");

    // Two months of the same year MUST take different seeds. That is exactly why the beast
    // pass keeps a genesis seed beside this one: a per-year decision keyed on a stepped seed
    // would be re-rolled every month.
    check::is_true(monthly[0] != monthly[1], "consecutive months to be differently seeded");
    check::is_true(monthly[0] != f.config.seed, "and none of them to be the genesis seed");
}

TEST(the_genesis_seed_survives_the_tick_untouched) {
    // The beast movement pass keys its year forage factor on `world.config.seed`, deliberately
    // not on the stepped seed: stepping the world's own seed would make the irruption trigger
    // per-month noise instead of a per-year decision.
    Fixture f;
    const std::uint32_t genesis = f.config.seed;
    const TickResult out = advance_time(fresh_state(), f.request({TimeUnit::Years, 3.0}));
    check::equal(f.config.seed, genesis, "the request's seed unchanged");
    check::is_true(!out.pending.empty(), "passes to have been scheduled at all");
    for (const CadenceCall& call : out.pending) {
        check::is_true(call.seed != genesis, name_of(call.cadence) + " to be stepped, not reused");
    }
}

TEST(advance_time_never_touches_the_callers_state_on_any_path) {
    // Failure is all-or-nothing, and so is a refusal: the caller's object is unchanged on
    // every path, so a raise leaves no half-advanced world anywhere.
    Fixture f;
    const TickState before = fresh_state();
    const std::string digest = digest_of(before);

    advance_time(before, f.request({TimeUnit::Years, 3.0}));
    advance_time(before, f.request({TimeUnit::Years, 2000.0}));
    advance_time(before, f.request({TimeUnit::Years, 5000.0}));
    check::equal(digest_of(before), digest, "after three advances of it");

    f.resolutions = {{"hook-nowhere", QuestOutcome::Completed}};
    TickState with_board = advance_time(before, Fixture().request({TimeUnit::Days, 1.0})).state;
    const std::string board_digest = digest_of(with_board);
    check::throws([&] { advance_time(with_board, f.request({TimeUnit::Days, 1.0})); },
                  "a resolution naming a quest that is not on the board");
    check::equal(digest_of(with_board), board_digest, "and the world left exactly as found");

    TickRequest bad = f.request({TimeUnit::Days, -1.0});
    check::throws([&] { advance_time(before, bad); }, "a negative span");
    TickRequest young = Fixture().request({TimeUnit::Days, 1.0});
    young.config.phase = 12;
    check::throws([&] { advance_time(before, young); }, "a world below the creatures stage");
    TickRequest skyless = Fixture().request({TimeUnit::Days, 1.0});
    skyless.has_astrology = false;
    check::throws([&] { advance_time(before, skyless); }, "a world with no sky");
}

TEST(a_year_of_monthly_ticks_reaches_the_same_world_as_one_yearly_tick) {
    // THE contract, and the cut that matters most because it crosses a band boundary. The
    // operation log and the wall clock are excluded: two calls are honestly two operations.
    const Fixture f;
    const TickState before = fresh_state();
    const TickResult whole = advance_time(before, f.request({TimeUnit::Years, 1.0}));

    TickState state = before;
    for (int month = 0; month < 12; ++month) {
        state = advance_time(state, f.request({TimeUnit::Days, 30.0})).state;
    }
    check::equal(digest_of(state), digest_of(whole.state), "a year cut twelve ways");
    // Non-vacuity: the year has to have DONE something, or two empty worlds would match.
    check::equal(whole.state.board.quests.size(), std::size_t{2}, "a board was built");
    check::is_true(whole.report.steps > 360, "and a year's worth of steps ran");
    check::equal(whole.state.networks.at("fire").nodes.size(), std::size_t{2},
                 "the network the drift was meant to move");
    check::is_true(whole.state.networks.at("fire").nodes[0].intensity != 2.0,
                   "and the ley field drifted");

    // And the same span cut unevenly, across the season and day bands.
    TickState uneven = before;
    for (const double piece : {0.5, 0.5, 89.0, 1.0, 179.0, 90.0}) {
        uneven = advance_time(uneven, f.request({TimeUnit::Days, piece})).state;
    }
    check::close(uneven.clock.day, 360.0, 1e-9, "the uneven cuts to reach the same day");
    check::equal(digest_of(uneven), digest_of(whole.state), "and the same world");
}

TEST(the_tick_reports_evaluating_leylines_only_when_the_field_actually_moved) {
    // `leylines_evaluated` is what sends the caller into the expensive globe pass, and that
    // pass indexes the magic block. Setting the flag on a world that has none is how a
    // magicless world died on a bare lookup rather than on any refusal this boundary knows
    // how to make -- so the flag must answer whether anything moved, not whether a drift
    // step was scheduled.
    Fixture f;
    const TickState magical = fresh_state();
    const TickResult moved = advance_time(magical, f.request({TimeUnit::Years, 1.0}));
    // The floor: the flag is capable of being set at all, and it was set by a real move.
    check::is_true(moved.report.leylines_evaluated, "a year on a magical world to move the field");
    check::is_true(moved.state.networks.at("fire").nodes[0].intensity != 2.0, "and to move it");

    // A world generated with magic disabled. The drift step is still SCHEDULED -- a band
    // does not decide which cadences run -- and it still must move nothing and claim nothing.
    Fixture off;
    off.config.magic_enabled = 0;
    const TickResult quiet = advance_time(magical, off.request({TimeUnit::Years, 1.0}));
    check::is_true(quiet.report.steps > 360, "the year's steps to have been scheduled anyway");
    check::is_true(!quiet.report.leylines_evaluated,
                   "a magicless world never to be sent into the globe pass");
    check::close(quiet.state.networks.at("fire").nodes[0].intensity, 2.0, 1e-12,
                 "and its field to be untouched");

    // A world with magic enabled but no networks at all: same answer, different reason.
    TickState bare = fresh_state();
    bare.has_magic = false;
    bare.networks.clear();
    const TickResult nothing = advance_time(bare, f.request({TimeUnit::Years, 1.0}));
    check::is_true(!nothing.report.leylines_evaluated, "a world with no networks to claim nothing");

    // And a span that crosses no year boundary claims nothing either, on a world that would
    // otherwise have moved.
    const TickResult month = advance_time(magical, f.request({TimeUnit::Days, 30.0}));
    check::is_true(!month.report.leylines_evaluated, "a month that crosses no year");
    check::close(month.state.networks.at("fire").nodes[0].intensity, 2.0, 1e-12,
                 "with the field left where it was");
}

TEST(the_tick_cost_estimate_has_two_terms_and_says_steps_are_a_proxy) {
    // A single-term model gets whichever term it omits badly wrong: the private copy is paid
    // once per call and scales with the world, the per-step cost is paid per crossing.
    const TickCostEstimate small = tick_cost_estimate(17.0, 360.0, 400);
    const TickCostEstimate big = tick_cost_estimate(34.0, 360.0, 400);
    check::close(small.copy_seconds, kTickBasisCopySeconds, 1e-9, "the copy at the basis size");
    check::close(big.copy_seconds, 4.0 * small.copy_seconds, 1e-9, "copy scaling with cell count");
    check::close(big.per_step_seconds, 4.0 * small.per_step_seconds, 1e-12, "and so does a step");

    const TickCostEstimate longer = tick_cost_estimate(17.0, 3600.0, 4000);
    check::is_true(longer.total_seconds > small.total_seconds, "more steps cost more");
    check::close(longer.copy_seconds, small.copy_seconds, 1e-9,
                 "while the fixed term ignores the span entirely");
    check::is_true(small.precision.find("proxy") != std::string::npos,
                   "the precision note to say steps are a proxy");
    check::is_true(!small.measured, "and the C++ tick to admit it has not been timed");
}
