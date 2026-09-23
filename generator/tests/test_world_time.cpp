// The world clock and the quest board, on a real World.
//
// test_time.cpp and test_quests.cpp pin the pure layers. These pin the glue: that a World
// goes in and a World comes out with the caller's untouched, that the passes the tick hands
// back run over the world they leave, that a span crossing an age turns exactly the ages it
// crosses and the board with them, and that every refusal names what refused.
//
// Fixtures are generated once and shared (function-local statics): a size-33 world through
// stage 16 costs about two seconds and an age advance over it about as much again. The
// size-9 world is for the tests that turn many ages or run a whole work ceiling of steps,
// where the size of the world is not what is being tested.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "fwg/ages.hpp"
#include "fwg/complete_stage.hpp"
#include "fwg/json_value.hpp"
#include "fwg/liveness.hpp"
#include "fwg/random.hpp"
#include "fwg/serialize.hpp"
#include "fwg/world_time.hpp"

using namespace fwg;

namespace {

constexpr double kYear = static_cast<double>(kDaysPerYear);

World generated(int size) {
    Request r;
    r.seed = 42;
    r.size = size;
    r.phase = 16;
    return generate(resolve(r));
}

TimeAdvanceRequest span(TimeUnit unit, double value, bool commit = false) {
    TimeAdvanceRequest request;
    request.elapsed = TimeElapsed{unit, value};
    request.commit = commit;
    return request;
}
TimeAdvanceRequest days(double value, bool commit = false) { return span(TimeUnit::Days, value, commit); }
TimeAdvanceRequest years(double value, bool commit = false) { return span(TimeUnit::Years, value, commit); }

// --- fixtures ---------------------------------------------------------------

const World& genesis() {
    static const World world = generated(33);
    return world;
}

const World& small() {
    static const World world = generated(9);
    return world;
}

// A world whose quest board is open: thirty days on from genesis.
const World& ticked() {
    static const World world = advance_world_time(genesis(), days(30)).world;
    return world;
}

// Checked reads of the time API's optional blocks: a missing block is a test failure, never
// a dereference of an empty optional.
const WorldClock& clock_of(const World& world) {
    if (!world.world_clock) check::fail("expected a world clock");
    return *world.world_clock;
}
const QuestBoard& board_of(const World& world) {
    if (!world.quests) check::fail("expected a quest board");
    return *world.quests;
}
const LunarSurge& surge_of(const World& world) {
    if (!world.lunar_surge) check::fail("expected a lunar surge");
    return *world.lunar_surge;
}

std::vector<std::string> ids_in(const World& world, std::initializer_list<QuestState> states) {
    std::vector<std::string> out;
    if (!world.quests) return out;
    for (const QuestRecord& q : board_of(world).quests) {
        if (std::find(states.begin(), states.end(), q.state) != states.end()) out.push_back(q.quest_id);
    }
    return out;
}

// The row for `id`, or a test failure naming it.
const QuestRecord* quest(const World& world, const std::string& id) {
    for (const QuestRecord& q : board_of(world).quests) {
        if (q.quest_id == id) return &q;
    }
    check::fail("expected a quest row for " + id);
}

// ticked(), with every offered quest taken.
const World& held() {
    static const World world = [] {
        QuestRequest take;
        for (const std::string& id : ids_in(ticked(), {QuestState::Offered})) {
            take.actions.push_back({id, QuestActionKind::Take, std::nullopt});
        }
        return world_quest_request(ticked(), take).world;
    }();
    return world;
}

// held(), one committed age later.
const TimeAdvanceResult& aged() {
    static const TimeAdvanceResult result = advance_world_time(held(), years(5000.0, true));
    return result;
}

// --- digests ----------------------------------------------------------------

// Everything a tick determines, without the record of how it got there: two calls are
// honestly two operations, and a wall clock is the only reason two runs of one seed differ.
std::string state_of(const World& w, bool with_clock = true) {
    std::ostringstream o;
    o << std::setprecision(17);
    if (with_clock) {
        if (w.world_clock) {
            o << "clock " << clock_of(w).day << ' ' << clock_of(w).age << ' ' << clock_of(w).derived_from
              << '\n';
        } else {
            o << "clock -\n";
        }
    }
    if (w.quests) {
        o << "board " << board_of(w).version << '\n';
        for (const QuestRecord& q : board_of(w).quests) {
            o << ' ' << q.quest_id << ' ' << to_string(q.state) << ' ' << q.offered_day << ' ' << q.expires_day
              << ' ' << q.closed_day.value_or(-1.0) << ' '
              << (q.closed_reason ? to_string(*q.closed_reason) : std::string_view("-")) << ' '
              << q.anchor.value_or("-") << ' ' << q.verb.value_or("-") << ' ' << q.giver_uid.value_or("-") << ' '
              << q.stated_purpose.value_or("-") << ' ' << q.unwitting << '\n';
        }
        for (const QuestLogEntry& e : board_of(w).log) {
            o << " log " << e.day << ' ' << e.quest_id << ' ' << e.event << ' ' << e.school << ' ' << e.node_id << ' '
              << e.intensity.value_or(-1.0) << '\n';
        }
    } else {
        o << "board -\n";
    }
    if (w.lunar_surge) {
        o << "surge " << surge_of(w).day;
        for (const auto& [name, factor] : surge_of(w).factors) o << ' ' << name << '=' << factor;
        o << '\n';
    } else {
        o << "surge -\n";
    }
    for (const auto& [name, net] : w.magic.networks) {
        o << "net " << name;
        for (const LeyNode& n : net.nodes) o << ' ' << n.id << '=' << n.intensity;
        for (const LeyEdge& e : net.edges) o << ' ' << e.id << '=' << e.intensity;
        o << " surge " << net.has_surge << ' ' << net.surged_strength << '\n';
    }
    for (const auto& [name, grid] : w.layers) {
        double sum = 0.0;
        double weighted = 0.0;
        const std::vector<double>& cells = grid.cells();
        for (std::size_t i = 0; i < cells.size(); ++i) {
            sum += cells[i];
            weighted += cells[i] * static_cast<double>(i % 97 + 1);
        }
        o << "layer " << name << ' ' << sum << ' ' << weighted << '\n';
    }
    o << "nomads " << w.has_nomads << ' ' << w.nomads.groups.size();
    for (const NomadBand& b : w.nomads.groups) {
        o << ' ' << b.uid << ':' << to_string(b.classification) << ':' << b.node << ':' << b.size << ':'
          << to_string(b.route_status) << ':' << b.camps.size();
    }
    if (w.nomads.routes) o << " routes " << w.nomads.routes->routed << ' ' << w.nomads.routes->stranded;
    o << '\n';
    o << "effects " << w.nomad_effects.leyline_edits.size() << ' ' << w.nomad_effects.raid_pressure.size() << ' '
      << w.nomad_effects.caravan_trade.size() << ' ' << w.nomad_effects.settlement_candidates.size() << '\n';
    o << "beasts " << w.has_beast_movements << ' ' << w.beast_movements.year << ' '
      << w.beast_movements.year_forage_factor;
    for (const BeastGroup& g : w.beast_movements.groups) o << ' ' << g.uid << ':' << g.camps.size();
    o << '\n';
    o << "encounters " << w.encounters.entries.size() << ' ' << w.encounters.occupancy.size();
    for (const EncounterEntry& e : w.encounters.entries) o << ' ' << e.group_uid;
    o << '\n';
    o << "food";
    for (const SeasonalCity& c : w.seasonal_food.cities) o << ' ' << c.site_id << ':' << c.food_coverage;
    o << '\n';
    o << "villains " << w.has_villains << ' ' << w.villains.outlook.rise << ' ' << w.villains.outlook.ceiling << ' '
      << w.villains.outlook.standing;
    for (const VillainOutlookRow& r : w.villains.outlook.regions) {
        o << ' ' << r.region << ':' << r.tier << ':' << r.concentration << ':' << r.seated;
    }
    for (const Villain& v : w.villains.people) o << ' ' << v.uid << ':' << v.status;
    o << '\n';
    o << "threat";
    for (const ThreatReport& t : w.threat_assessments.cities) {
        o << ' ' << t.city_uid << ':' << t.regional_threat << ':' << t.nomad_pressure.value_or(-1.0);
    }
    o << '\n';
    o << "ages " << w.age << ' ' << w.history.ages.size();
    for (const AgeRecord& a : w.history.ages) o << ' ' << a.age << ':' << a.events.size() << ':' << a.new_city_ids.size();
    o << '\n';
    o << "cities";
    for (const SettlementSite& s : w.settlements.sites) o << ' ' << s.uid;
    o << "\nruins";
    for (const Ruin& r : w.ruins) o << ' ' << r.id;
    o << "\nheroes " << w.heroes.status << ' ' << w.heroes.people.size();
    for (const Hero& h : w.heroes.people) o << ' ' << h.uid << ':' << h.status;
    for (const HeroQuestHook& h : w.heroes.quest_hooks) o << ' ' << h.hook_id;
    o << "\nnpcs " << w.npcs.people.size() << "\nwarnings";
    for (const std::string& s : w.warnings) o << " | " << s;
    o << '\n';
    return o.str();
}

// What each call recorded, and which timings exist (names, not values).
std::string record_of(const World& w) {
    std::ostringstream o;
    o << std::setprecision(17);
    o << "ops";
    for (std::size_t i = 0; i < w.history.operations.size(); ++i) {
        const HistoryOperation& entry = w.history.operations[i];
        if (const auto* advance = std::get_if<AgeOperation>(&entry)) {
            o << " age " << advance->after_age << ':' << advance->steps;
        } else if (const auto* span = std::get_if<TimeOperationRecord>(&entry)) {
            o << " time " << to_string(span->band) << ':' << span->elapsed_days << ':' << span->from_day << ':'
              << span->to_day << ':' << span->steps.value_or(-1) << ':' << span->ages.value_or(-1) << ':'
              << span->leyline_edits.size() << ':' << span->resolutions.size();
        } else if (const auto* batch = std::get_if<QuestOperationRecord>(&entry)) {
            o << " quest [";
            for (const QuestActionEntry& a : batch->actions) {
                o << ' ' << a.quest_id << ':' << to_string(a.action) << ':' << a.day;
            }
            o << " ]";
        } else {
            o << " other";
        }
        o << '@' << i;
    }
    o << "\nreplay " << w.history.replay << "\ntimings";
    for (const auto& [name, value] : w.timing_ms) {
        (void)value;
        o << ' ' << name;
    }
    o << '\n';
    return o.str();
}

// The whole world, timing VALUES included: what "the caller's world is unchanged" means.
std::string everything_of(const World& w) {
    std::ostringstream o;
    o << std::setprecision(17) << state_of(w) << record_of(w);
    for (const auto& [name, value] : w.timing_ms) o << name << '=' << value << ' ';
    return o.str();
}

std::size_t log_size(const World& w) { return w.history.operations.size(); }

// The entries of one kind, in log order, and where each stands in the one log.
template <typename Op>
std::vector<std::pair<std::size_t, const Op*>> ops_of(const World& w) {
    std::vector<std::pair<std::size_t, const Op*>> out;
    for (std::size_t i = 0; i < w.history.operations.size(); ++i) {
        if (const auto* op = std::get_if<Op>(&w.history.operations[i])) out.emplace_back(i, op);
    }
    return out;
}

template <typename Op>
std::size_t count_of(const World& w) {
    return ops_of<Op>(w).size();
}

// The last entry of one kind. Fails the test rather than crashing on a log without one.
template <typename Op>
const Op& last_of(const World& w) {
    const auto ops = ops_of<Op>(w);
    if (ops.empty()) check::fail("the log to hold an entry of this kind");
    return *ops.back().second;
}

template <typename Op>
std::size_t position_of_last(const World& w) {
    const auto ops = ops_of<Op>(w);
    if (ops.empty()) check::fail("the log to hold an entry of this kind");
    return ops.back().first;
}

template <typename Fn>
WorldTimeError refusal(Fn&& fn, const std::string& what) {
    try {
        fn();
    } catch (const WorldTimeError& refused) {
        return refused;
    }
    check::fail(what + ": expected a WorldTimeError, none thrown");
}

// The first known school's network that has key points, in school-table order.
const LeyNetwork* manifest_network(const World& w) {
    for (const MagicSchool& school : magic_school_table()) {
        if (school.hidden) continue;
        const auto it = w.magic.networks.find(std::string(school.name));
        if (it != w.magic.networks.end() && !it->second.nodes.empty()) return &it->second;
    }
    check::fail("expected a manifest known school");
}

const LeyNode* node_named(const World& w, const std::string& school, const std::string& id) {
    for (const LeyNode& n : w.magic.networks.at(school).nodes) {
        if (n.id == id) return &n;
    }
    check::fail("expected key point " + id + " in " + school);
}

}  // namespace

// The time API's blocks, written under the reference's keys: world_clock as terrain_time's
// clock() builds it, and the quest board as its sweep does -- every row the eleven keys a hook
// opens it with, every log entry {day, quest_id, event}, and an applied ley effect three more.
// Written from a bare world holding only the two blocks, so the document stays small.
TEST(the_document_writes_the_clock_and_the_board_under_the_references_keys) {
    const auto keys_of = [](const JsonValue& object) {
        std::vector<std::string> out;
        for (const auto& member : object.as_object()) out.push_back(member.first);
        return out;
    };
    World bare;
    bare.world_clock = clock_of(held());
    bare.quests = board_of(held());
    std::ostringstream out;
    write_world_json(out, bare, /*pretty=*/false, /*include_timings=*/false);
    const JsonValue doc = parse_json(out.str());

    check::is_true(keys_of(doc.at("world_clock")) ==
                       std::vector<std::string>{"version", "day", "age", "epoch_day", "derived_from"},
                   "the clock's keys");
    const JsonValue& board = doc.at("quests");
    check::is_true(keys_of(board) == std::vector<std::string>{"version", "quests", "log"}, "the board's keys");
    const auto& rows = board.at("quests").as_array();
    check::is_true(!rows.empty(), "a board with rows, or the row keys are untested");
    const std::vector<std::string> row_keys{"quest_id",       "giver_uid", "anchor",      "verb",
                                            "stated_purpose", "unwitting", "state",       "offered_day",
                                            "expires_day",    "closed_day", "closed_reason"};
    for (const JsonValue& row : rows) check::is_true(keys_of(row) == row_keys, "a quest row's keys");
    const auto& log = board.at("log").as_array();
    check::is_true(!log.empty(), "a board with a log, or the entry keys are untested");
    for (const JsonValue& entry : log) {
        const std::vector<std::string> keys = keys_of(entry);
        const bool plain = keys == std::vector<std::string>{"day", "quest_id", "event"};
        const bool ley =
            keys == std::vector<std::string>{"day", "quest_id", "event", "school", "node_id", "intensity"};
        check::is_true(plain || ley, "a log entry's keys");
    }
}

// An age rebuild reads the rebuilt year's sky: the reference's refresh rewrites
// magic.lunar_surge as the reported year's first day, so the surge the last tick left does not
// survive the turn, and the document writes the almanac's own day in its place.
TEST(an_age_advance_drops_the_surge_the_last_tick_left) {
    check::is_true(ticked().lunar_surge.has_value(), "a tick leaves a surge");
    AgeAdvanceRequest advance;
    advance.steps = 1;
    const World turned = advance_ages(ticked(), advance);
    check::is_true(!turned.lunar_surge.has_value(), "the turn's refresh drops it");
    check::equal(turned.lunar_almanac.reported_year_source, std::string("founding.end_year"), "the rebuilt year");
    check::equal(static_cast<long long>(turned.lunar_almanac.reported_year), turned.settlements.founding.end_year,
                 "the rebuilt founding's end year");
}

// --- the clock --------------------------------------------------------------

TEST(world_time_a_generated_world_is_read_at_its_founding_year) {
    const World& w = genesis();
    check::is_true(!w.world_clock && !w.quests && !w.lunar_surge, "a generated world carries no clock, board or surge");
    check::is_true(count_of<TimeOperationRecord>(w) == 0 && count_of<QuestOperationRecord>(w) == 0,
                   "and has recorded nothing");
    const WorldClock clock = world_clock_of(w);
    check::equal(clock.derived_from, std::string("founding.end_year"), "derived from where founding ended");
    check::close(clock.day, static_cast<double>(w.settlements.founding.end_year) * kYear, 0.0, "on that year's day");
    check::equal(clock.age, static_cast<std::int64_t>(w.history.ages.size()), "in the age its history says");
}

TEST(world_time_the_clock_only_moves_forward) {
    World w = genesis();
    double day = world_clock_of(w).day;
    const std::pair<TimeAdvanceRequest, double> steps[] = {
        {days(0.0), 0.0},
        {span(TimeUnit::Seconds, 3.0), 3.0 / 86400.0},
        {days(0.5), 0.5},
        {days(1.0), 1.0},
        {days(30.0), 30.0},
        {days(90.0), 90.0},
        {years(1.0), kYear},
    };
    for (const auto& [request, elapsed] : steps) {
        const TimeAdvanceResult out = advance_world_time(w, request);
        check::is_true(out.report.tick.committed, "each span to commit");
        check::is_true(out.world.world_clock.has_value(), "a clock once advanced");
        check::equal(clock_of(out.world).day, day + elapsed, "the clock moved by exactly the span");
        check::is_true(clock_of(out.world).day >= day, "and never backwards");
        w = out.world;
        day = clock_of(w).day;
    }
    // A span that would move it backwards is refused, and moves nothing.
    const std::string before = everything_of(w);
    const WorldTimeError refused = refusal([&] { (void)advance_world_time(w, days(-1.0)); }, "a negative span");
    check::equal(refused.field(), std::string("elapsed.days"), "the negative span names its field");
    check::equal(everything_of(w), before, "and the world is exactly as found");
}

TEST(world_time_an_instant_and_a_tick_of_the_same_span_agree_on_the_clock) {
    const World& w = ticked();
    const double start = clock_of(w).day;
    check::equal(start, std::floor(start), "the fixture's clock to stand on a whole day");

    const TimeAdvanceResult instant = advance_world_time(w, days(0.25));
    check::is_true(instant.report.tick.band == TimeBand::Instant, "a quarter day is the instant band");
    check::equal(instant.report.tick.steps, std::int64_t{0}, "and crosses nothing");
    check::is_true(!instant.report.tick.leylines_evaluated, "and moves no field");

    // The same quarter day through the ticked path: an edit that sets a node to the value it
    // already has is still an edit, and an edit is never the instant path.
    const LeyNetwork* net = manifest_network(w);
    check::is_true(net != nullptr, "a manifest school to edit");
    TimeAdvanceRequest forced = days(0.25);
    forced.leyline_edits.push_back({net->name, net->nodes[0].id, std::nullopt, net->nodes[0].intensity, std::nullopt});
    const TimeAdvanceResult through = advance_world_time(w, forced);
    check::is_true(through.report.tick.leylines_evaluated, "the forced span took the ticked path");
    check::equal(clock_of(through.world).day, clock_of(instant.world).day, "and reached the same day");
    check::equal(clock_of(through.world).age, clock_of(instant.world).age, "in the same age");
    check::equal(surge_of(through.world).day, surge_of(instant.world).day, "under the same sky");

    // Four quarters make the day one whole-day tick makes, sweep and all: the fourth crosses
    // the day boundary and runs that day's sweep, as the whole day does.
    World pieces = w;
    for (int i = 0; i < 4; ++i) pieces = advance_world_time(pieces, days(0.25)).world;
    const TimeAdvanceResult whole = advance_world_time(w, days(1.0));
    check::equal(whole.report.tick.steps, std::int64_t{1}, "a day crosses one quest sweep");
    check::equal(state_of(pieces), state_of(whole.world), "four quarters and one day, the same world");
}

TEST(world_time_a_span_of_exactly_zero_records_nothing) {
    const World& w = ticked();
    const TimeAdvanceResult zero = advance_world_time(w, days(0.0));
    check::is_true(zero.report.tick.committed, "a zero span commits");
    check::equal(count_of<TimeOperationRecord>(zero.world), count_of<TimeOperationRecord>(w), "and records nothing");
    check::equal(zero.world.history.replay, w.history.replay, "nor rewrites the replay sentence");
    check::equal(clock_of(zero.world).day, clock_of(w).day, "the clock where it was");
    check::is_true(zero.world.lunar_surge.has_value(), "the sky still read");

    // On a world never advanced, "what time is it" hands back a clock and nothing else.
    const TimeAdvanceResult first = advance_world_time(genesis(), days(0.0));
    check::is_true(first.world.world_clock.has_value(), "a clock adopted");
    check::equal(clock_of(first.world).day, world_clock_of(genesis()).day, "at the founding year");
    check::is_true(!first.world.quests, "no board: nothing was swept");
    check::equal(count_of<TimeOperationRecord>(first.world), std::size_t{0}, "nothing recorded");

    // A span that crosses nothing but is not zero is one operation of zero steps.
    const TimeAdvanceResult moment = advance_world_time(w, days(0.25));
    check::equal(count_of<TimeOperationRecord>(moment.world), count_of<TimeOperationRecord>(w) + 1, "one operation");
    const TimeOperationRecord& op = last_of<TimeOperationRecord>(moment.world);
    check::equal(op.steps.value_or(-1), std::int64_t{0}, "of zero steps");
    check::is_true(!op.ages.has_value(), "a tick's entry, not an age's");
    check::equal(position_of_last<TimeOperationRecord>(moment.world), log_size(w), "at the end of the log");
    check::equal(moment.world.history.replay, std::string(kReplaySentence), "with the replay sentence");
}

TEST(world_time_the_callers_world_is_never_touched) {
    const World& w = ticked();
    const std::string before = everything_of(w);
    const std::vector<std::string> open = ids_in(w, {QuestState::Offered});
    check::is_true(open.size() >= 2, "open quests to act on");

    (void)advance_world_time(w, span(TimeUnit::Seconds, 3.0));
    (void)advance_world_time(w, days(30.0));
    TimeAdvanceRequest busy = years(1.0);
    busy.resolutions.push_back({open.at(0), QuestOutcome::Completed});
    const LeyNetwork* net = manifest_network(w);
    busy.leyline_edits.push_back({net->name, net->nodes[0].id, std::nullopt, 1.0, std::nullopt});
    (void)advance_world_time(w, busy);
    (void)advance_world_time(w, years(2000.0));  // reported: past the work ceiling
    (void)advance_world_time(w, years(5000.0));  // reported: an age estimate
    TimeAdvanceRequest unknown = days(1.0);
    unknown.resolutions.push_back({"hook-nobody-0", QuestOutcome::Failed});
    (void)refusal([&] { (void)advance_world_time(w, unknown); }, "an unknown quest");
    TimeAdvanceRequest late = days(1.0);
    late.resolutions.push_back({open.at(1), QuestOutcome::Completed});
    late.resolutions.push_back({open.at(1), QuestOutcome::Failed});
    (void)refusal([&] { (void)advance_world_time(w, late); }, "a quest resolved twice in one batch");
    QuestRequest take;
    take.actions.push_back({open.at(0), QuestActionKind::Take, std::nullopt});
    (void)world_quest_request(w, take);
    take.actions.push_back({"hook-nobody-0", QuestActionKind::Take, std::nullopt});
    (void)refusal([&] { (void)world_quest_request(w, take); }, "a batch with an unknown quest");

    check::equal(everything_of(w), before, "the caller's world, after every path");
}

TEST(world_time_the_same_request_twice_gives_the_same_world) {
    const World& w = ticked();
    const std::vector<std::string> open = ids_in(w, {QuestState::Offered});
    TimeAdvanceRequest request = years(1.0);
    request.resolutions.push_back({open.at(0), QuestOutcome::Completed});
    const LeyNetwork* net = manifest_network(w);
    request.leyline_edits.push_back({net->name, net->nodes[0].id, std::nullopt, 2.5, std::nullopt});
    const TimeAdvanceResult a = advance_world_time(w, request);
    const TimeAdvanceResult b = advance_world_time(w, request);
    check::equal(state_of(a.world), state_of(b.world), "the same world");
    check::equal(record_of(a.world), record_of(b.world), "and the same record");
    check::is_true(state_of(a.world) != state_of(w), "a year that changed something, or nothing is compared");
}

// --- chunking ---------------------------------------------------------------

TEST(world_time_a_span_cut_differently_reaches_the_same_world) {
    // The contract, over a real world. The RNG domain is keyed by simulated time, so the cut
    // cannot reach the result. The log is excluded: two calls are honestly two operations.
    const World& w = genesis();
    const TimeAdvanceResult month = advance_world_time(w, days(30.0));
    const World halves = advance_world_time(advance_world_time(w, days(15.0)).world, days(15.0)).world;
    check::equal(state_of(halves), state_of(month.world), "thirty days once, or fifteen twice");

    const TimeAdvanceResult year = advance_world_time(w, years(1.0));
    World seasons = w;
    for (int i = 0; i < 4; ++i) seasons = advance_world_time(seasons, days(90.0)).world;
    check::equal(state_of(seasons), state_of(year.world), "a year in four seasons is a year");
    // The cut that matters most, because it crosses a band boundary: before the band stopped
    // choosing cadences, the monthly path never ran the yearly passes at all.
    World months = w;
    for (int i = 0; i < 12; ++i) months = advance_world_time(months, days(30.0)).world;
    check::equal(state_of(months), state_of(year.world), "a year of monthly ticks is a year");

    // Non-vacuity: the year has to have done something, or two unmoved worlds would match.
    check::equal(year.report.tick.by_cadence.size(), std::size_t{9}, "every cadence crossed in a year");
    check::is_true(year.report.tick.leylines_evaluated, "the field drifted");
    check::is_true(year.world.quests && !board_of(year.world).quests.empty(), "a board was built");
    check::is_true(state_of(year.world) != state_of(month.world), "a year is not a month");
}

TEST(world_time_every_cadence_pass_runs_over_the_world_it_leaves) {
    // The route pass reads the block the nomad pass replaced; the encounter index reads both
    // the bands and the herds. Run in any other order, the index names bands that no longer
    // exist.
    const World& w = genesis();
    const TimeAdvanceResult year = advance_world_time(w, years(1.0));
    const World& out = year.world;

    std::ostringstream was;
    std::ostringstream now;
    for (const NomadBand& b : w.nomads.groups) was << b.uid << ':' << b.node << ':' << b.size << ' ';
    for (const NomadBand& b : out.nomads.groups) now << b.uid << ':' << b.node << ':' << b.size << ' ';
    check::is_true(was.str() != now.str(), "the year's nomad pass re-rolled the bands, or order is untestable");
    check::is_true(out.nomads.routes.has_value(), "the re-rolled bands were routed");

    std::multiset<std::string> expected;
    for (const NomadBand& b : out.nomads.groups) {
        if (!b.camps.empty()) expected.insert(b.uid);
    }
    for (const BeastGroup& g : out.beast_movements.groups) {
        if (!g.camps.empty()) expected.insert(g.uid);
    }
    std::multiset<std::string> indexed;
    for (const EncounterEntry& e : out.encounters.entries) indexed.insert(e.group_uid);
    check::is_true(!indexed.empty(), "an encounter index to compare");
    check::is_true(indexed == expected, "the index built over exactly the bands and herds this tick left");

    check::equal(out.beast_movements.year, w.settlements.founding.end_year,
                 "the herds built for the year the span started in");
    check::is_true(out.nomad_effects.version == kNomadEffectsVersion, "the write-backs re-applied");
    check::equal(out.history.ages.size(), w.history.ages.size(), "and no age turned");
    check::equal(out.settlements.sites.size(), w.settlements.sites.size(), "no city founded or lost by a tick");
}

TEST(world_time_the_ley_field_moves_on_a_year_and_not_on_a_month) {
    const World& w = genesis();
    const TimeAdvanceResult month = advance_world_time(w, days(30.0));
    for (const auto& [name, net] : w.magic.networks) {
        const LeyNetwork& after = month.world.magic.networks.at(name);
        for (std::size_t i = 0; i < net.nodes.size(); ++i) {
            check::equal(after.nodes[i].intensity, net.nodes[i].intensity, name + " untouched by a month");
        }
    }
    check::is_true(!month.report.tick.leylines_evaluated, "a month re-evaluates nothing");
    check::equal(surge_of(month.world).day, clock_of(month.world).day, "the sky read at the clock's day");

    const TimeAdvanceResult two = advance_world_time(w, years(2.0));
    bool moved = false;
    for (const auto& [name, net] : w.magic.networks) {
        const LeyNetwork& after = two.world.magic.networks.at(name);
        for (std::size_t i = 0; i < net.nodes.size(); ++i) moved = moved || after.nodes[i].intensity != net.nodes[i].intensity;
    }
    check::is_true(moved, "two years drift the base field");
    check::is_true(two.report.tick.leylines_evaluated, "and say so");
    const LeyNetwork* net = manifest_network(w);
    const std::string layer = "ley_" + net->name;
    check::is_true(two.world.layers.at(layer).cells() != w.layers.at(layer).cells(), "and the field was re-evaluated");
}

TEST(world_time_reproduced_the_nomad_write_backs_claim_the_ley_field_even_where_nothing_moves) {
    // REFERENCE DEFECT, reproduced and pinned: the reference marks the ley field touched
    // whenever the nomad write-back cadence is SCHEDULED (LEY_WRITING_PASSES), whether or not
    // it wrote a node -- including on a world with magic disabled, where the drift claims
    // nothing. A fix changes this test on purpose.
    World off = genesis();
    off.config.magic_enabled = 0;
    off.effective.magic_enabled = 0;
    const TimeAdvanceResult year = advance_world_time(off, years(1.0));
    bool crossed = false;
    for (const auto& [cadence, count] : year.report.tick.by_cadence) crossed = crossed || (cadence == Cadence::NomadEffects && count == 1);
    check::is_true(crossed, "the year crossed the write-back cadence");
    check::is_true(year.report.tick.leylines_evaluated, "and claims the field on a magicless world");
    // A month crosses no write-back, and claims nothing.
    check::is_true(!advance_world_time(off, days(30.0)).report.tick.leylines_evaluated, "a month claims nothing");
}

TEST(world_time_reproduced_the_herds_are_built_for_the_year_the_call_began) {
    // REFERENCE DEFECT, reproduced and pinned ("known imprecision" in the reference's
    // world_year): the beast movement pass reads the clock as it stands when the pass runs,
    // and the advanced clock is written after the cadence loop, so a span crossing years
    // builds the herds for the year it STARTED in. It makes the herds depend on how a span
    // is cut across a year boundary; the reference's own chunking tests cut inside a year.
    const World& w = ticked();
    const long long start_year = clock_year(*w.world_clock);
    const TimeAdvanceResult two = advance_world_time(w, years(2.0));
    check::equal(clock_year(*two.world.world_clock), start_year + 2, "the clock two years on");
    check::equal(two.world.beast_movements.year, start_year, "the herds for the year the span began");
    const World stepped = advance_world_time(advance_world_time(w, years(1.0)).world, years(1.0)).world;
    check::equal(stepped.beast_movements.year, start_year + 1, "cut at the year, the herds of the second call's year");
}

// --- quests on the tick -----------------------------------------------------

TEST(world_time_resolutions_land_before_the_span_runs) {
    // Applied at the end, the world could expire a quest during the very span the player
    // spent completing it, and then refuse the completion.
    const World& w = ticked();
    const std::vector<std::string> open = ids_in(w, {QuestState::Offered});
    check::is_true(open.size() >= 2, "two offers to work with");
    const double start = clock_of(w).day;
    TimeAdvanceRequest request = days(kQuestOfferMaxDays + 10.0);
    request.resolutions.push_back({open.at(0), QuestOutcome::Completed});
    const TimeAdvanceResult out = advance_world_time(w, request);
    const QuestRecord* done = quest(out.world, open.at(0));
    check::equal(std::string(to_string(done->state)), std::string("completed"), "completed before it could lapse");
    check::is_true(done->closed_reason == QuestClosedReason::Resolved, "closed as resolved");
    check::equal(done->closed_day.value_or(-1.0), start, "on the day the span began");
    check::equal(out.report.tick.resolutions, 1, "the report counts it");
    // Floor: the offer nobody resolved lapsed over the same span, so the span could have.
    check::is_true(quest(out.world, open.at(1))->closed_reason == QuestClosedReason::Expired, "the other offer lapsed");
    const TimeOperationRecord& op = last_of<TimeOperationRecord>(out.world);
    check::equal(op.resolutions.size(), std::size_t{1}, "the resolution recorded verbatim");
    check::equal(op.resolutions[0].quest_id, open.at(0), "naming its quest");
}

TEST(world_time_a_resolution_for_a_quest_the_world_does_not_carry_is_refused_as_the_reference_refuses_it) {
    const World& w = ticked();
    const std::string before = everything_of(w);
    TimeAdvanceRequest request = days(1.0);
    request.resolutions.push_back({"hook-nobody-0", QuestOutcome::Completed});
    const WorldTimeError unknown = refusal([&] { (void)advance_world_time(w, request); }, "an unknown quest");
    // The reference's unknown_field: INVALID_INPUT, and the FIELD is the unknown name itself.
    check::equal(std::string(unknown.code()), std::string("INVALID_INPUT"), "a malformed value");
    check::equal(unknown.field(), std::string("hook-nobody-0"), "the field names the quest it could not find");
    check::is_true(!unknown.refused_by_world() && unknown.retry(), "a value to correct, not a world to regenerate");
    check::is_true(unknown.message().rfind("No quest named 'hook-nobody-0'.", 0) == 0, "in the reference's words");

    // A near miss names the quest it nearly was.
    const std::string real = board_of(w).quests.at(0).quest_id;
    std::string typo = real;
    typo.back() = typo.back() == '9' ? '8' : '9';
    request.resolutions = {{typo, QuestOutcome::Completed}};
    const WorldTimeError near = refusal([&] { (void)advance_world_time(w, request); }, "a mistyped quest");
    check::is_true(!near.candidates().empty() && near.candidates().size() <= 5, "up to five suggestions");
    check::is_true(std::find(near.candidates().begin(), near.candidates().end(), real) != near.candidates().end(),
                   "the quest it nearly named among them");

    // A quest the world carries but has already closed is refused BY THE WORLD: the id was
    // real and the caller is late.
    const std::vector<std::string> open = ids_in(w, {QuestState::Offered});
    TimeAdvanceRequest first = days(1.0);
    first.resolutions.push_back({open.at(0), QuestOutcome::Completed});
    const World resolved = advance_world_time(w, first).world;
    TimeAdvanceRequest again = days(1.0);
    again.resolutions.push_back({open.at(0), QuestOutcome::Failed});
    const WorldTimeError late = refusal([&] { (void)advance_world_time(resolved, again); }, "a closed quest");
    check::equal(late.field(), std::string("quest_id"), "the field");
    check::is_true(late.refused_by_world(), "refused by the world, not by validation");
    check::is_true(!late.candidates().empty() && late.candidates().size() <= 5, "naming quests still open");
    const std::vector<std::string> still = ids_in(resolved, {QuestState::Offered, QuestState::Taken});
    for (const std::string& id : late.candidates()) {
        check::is_true(std::find(still.begin(), still.end(), id) != still.end(), id + " to be open");
    }
    check::equal(everything_of(w), before, "and the caller's world untouched by either");
}

TEST(world_time_a_resolution_on_a_world_never_ticked_opens_the_board_first) {
    // No quests and no quest lifecycle are different facts; a resolution on a world with no
    // board opens one (the bootstrap sweep) and then resolves against it.
    const World& g = genesis();
    const std::string id = ids_in(ticked(), {QuestState::Offered}).at(0);
    TimeAdvanceRequest request = days(0.25);
    request.resolutions.push_back({id, QuestOutcome::Completed});
    const TimeAdvanceResult out = advance_world_time(g, request);
    check::is_true(out.world.quests.has_value(), "a board opened");
    check::equal(board_of(out.world).quests.size(), g.heroes.quest_hooks.size(), "one row per hook");
    check::equal(std::string(to_string(quest(out.world, id)->state)), std::string("completed"), "and resolved");
    // The bootstrap sweep draws from index 0, NOT from the day's own index: it is not a
    // crossing of the quests cadence, and borrowing the day's index would draw the window
    // that day's own sweep is going to draw. One uniform per new quest, in hook-list order.
    const double day = world_clock_of(g).day;
    Rng stream = Rng::child(g.config.seed, "time-quests", 0);
    for (const HeroQuestHook& hook : g.heroes.quest_hooks) {
        const double window = stream.uniform(kQuestOfferMinDays, kQuestOfferMaxDays);
        check::equal(quest(out.world, hook.hook_id)->expires_day, day + window,
                     hook.hook_id + "'s window drawn from the bootstrap stream, in list order");
    }
    check::is_true(out.report.tick.band == TimeBand::Instant && out.report.tick.steps == 0,
                   "on a span that crossed nothing");
    request.resolutions = {{"hook-nobody-0", QuestOutcome::Completed}};
    check::equal(refusal([&] { (void)advance_world_time(g, request); }, "unknown").field(), std::string("hook-nobody-0"),
                 "an unknown quest refused the same way");
}

TEST(world_time_a_giver_who_is_gone_closes_the_quest_on_the_next_sweep) {
    World w = ticked();
    std::string target;
    std::string giver;
    for (const QuestRecord& q : board_of(w).quests) {
        if (q.state != QuestState::Offered || !q.giver_uid) continue;
        for (Hero& h : w.heroes.people) {
            if (h.uid != *q.giver_uid || !is_present(h)) continue;
            h.status = std::string(token(LivenessBlock::Heroes, Liveness::Gone));
            target = q.quest_id;
            giver = h.uid;
            break;
        }
        if (!target.empty()) break;
    }
    check::is_true(!target.empty(), "an offer from a living hero to take the hero from");
    const TimeAdvanceResult out = advance_world_time(w, days(1.0));
    const QuestRecord* closed = quest(out.world, target);
    check::is_true(closed->closed_reason == QuestClosedReason::GiverGone, giver + " passed to legend, the offer with them");
    check::equal(closed->closed_day.value_or(-1.0), clock_of(w).day + 1.0, "on the next sweep");
    // Control: left alive, the same giver keeps the offer standing.
    check::is_true(quest(advance_world_time(ticked(), days(1.0)).world, target)->state == QuestState::Offered,
                   "the offer stands while its giver does");
}

TEST(world_time_a_hook_that_leaves_closes_its_quest_as_hook_gone) {
    World w = ticked();
    const std::vector<std::string> open = ids_in(w, {QuestState::Offered, QuestState::Taken});
    check::is_true(!open.empty(), "open quests");
    w.heroes.quest_hooks.clear();
    const TimeAdvanceResult out = advance_world_time(w, days(2.0));
    check::equal(board_of(out.world).quests.size(), board_of(w).quests.size(), "the rows survive");
    for (const std::string& id : open) {
        check::is_true(quest(out.world, id)->closed_reason == QuestClosedReason::HookGone, id + " closed as hook_gone");
    }
}

TEST(world_time_a_failed_heroes_package_offers_nothing) {
    // A failed package carries no people and offers no hooks: zero present people and no
    // package at all are different facts, so its quests close rather than reading as offered.
    World w = ticked();
    const std::vector<std::string> open = ids_in(w, {QuestState::Offered, QuestState::Taken});
    check::is_true(!open.empty(), "open quests");
    w.heroes.status = "failed";
    const TimeAdvanceResult out = advance_world_time(w, days(1.0));
    for (const std::string& id : open) {
        check::is_true(!quest_is_open(quest(out.world, id)->state), id + " closed once its package failed");
    }
    // Control: the same day on the package as generated keeps them open.
    const World control = advance_world_time(ticked(), days(1.0)).world;
    check::is_true(quest_is_open(quest(control, open.front())->state), "open while the package stands");
}

TEST(world_time_a_completed_ley_hook_moves_the_node_it_names) {
    // A completed hook whose declared effect is a ley change applies it, because the hook
    // states the school and the delta itself.
    World w = ticked();
    const LeyNetwork* net = manifest_network(w);
    check::is_true(net != nullptr && w.config.magic_enabled != 0, "a magical world");
    const std::string school = net->name;
    const std::string node = net->nodes[0].id;
    const Hero* giver = nullptr;
    for (const Hero& h : w.heroes.people) {
        if (is_present(h)) {
            giver = &h;
            break;
        }
    }
    check::is_true(giver != nullptr, "a living hero to offer it");
    HeroQuestHook hook;
    hook.hook_id = "hook-" + giver->uid + "-99";
    hook.giver_uid = giver->uid;
    hook.stated_purpose = "Tend the node.";
    hook.actual_effect.kind = "ley_node";
    hook.actual_effect.school = school;
    hook.actual_effect.node_id = node;
    hook.actual_effect.intensity_delta = 0.5;
    hook.actual_effect.has_delta = true;
    hook.target = node;
    hook.verb = "tend";
    w.heroes.quest_hooks.push_back(hook);
    const World opened = advance_world_time(w, days(1.0)).world;
    check::is_true(quest(opened, hook.hook_id)->state == QuestState::Offered,
                   "the ley offer opened");
    const double was = node_named(opened, school, node)->intensity;

    TimeAdvanceRequest done = days(0.25);
    done.resolutions.push_back({hook.hook_id, QuestOutcome::Completed});
    const TimeAdvanceResult out = advance_world_time(opened, done);
    check::close(node_named(out.world, school, node)->intensity, std::min(4.0, was + 0.5), 1e-12, "the node lifted");
    check::is_true(out.report.tick.leylines_evaluated, "the field re-evaluated");
    check::is_true(out.world.layers.at("ley_" + school).cells() != opened.layers.at("ley_" + school).cells(),
                   "and the published layer followed");
    check::equal(board_of(out.world).log.size(), board_of(opened).log.size() + 2, "the outcome and the ley write logged");
    const QuestLogEntry& applied = board_of(out.world).log.back();
    check::equal(applied.event, std::string("ley_applied"), "logged as applied");
    check::equal(applied.node_id, node, "naming the node");

    // Any other outcome records and moves nothing: inventing a consequence the hook does not
    // declare would be the world deciding play.
    done.resolutions = {{hook.hook_id, QuestOutcome::Failed}};
    const TimeAdvanceResult failed = advance_world_time(opened, done);
    check::equal(node_named(failed.world, school, node)->intensity, was, "a failure moves nothing");
    check::is_true(!failed.report.tick.leylines_evaluated, "and claims nothing");
}

// --- refusals ---------------------------------------------------------------

TEST(world_time_each_refusal_names_its_field) {
    const World& g = genesis();
    const LeyNetwork* net = manifest_network(g);
    struct Case {
        std::string what;
        World world;
        TimeAdvanceRequest request;
        std::string field;
        std::string_view code;
        bool retry;
        bool by_world;
    };
    const auto with = [](auto change) {
        TimeAdvanceRequest r = days(1.0);
        change(r);
        return r;
    };
    const auto edit = [&](LeylineEdit e) { return with([&](TimeAdvanceRequest& r) { r.leyline_edits.push_back(e); }); };
    World young = g;
    young.config.phase = 12;
    World other_recipe = g;
    other_recipe.config.world_recipe = 2;
    World skyless = g;
    skyless.has_astrology = false;
    World mundane = g;
    mundane.config.magic_enabled = 0;
    const LeyNode twin{net->nodes[0].id, net->nodes[0].direction, 1.0};
    const LeyNode fresh{"player-well", net->nodes[0].direction, 1.0};

    std::vector<Case> cases = {
        {"a future api", g, with([](TimeAdvanceRequest& r) { r.api_version = 2; }), "api_version",
         kRefusalUnsupportedVersion, false, false},
        {"a negative span", g, days(-1.0), "elapsed.days", kRefusalInvalidInput, true, false},
        {"seconds that are not a number", g, span(TimeUnit::Seconds, std::numeric_limits<double>::quiet_NaN()),
         "elapsed.seconds", kRefusalInvalidInput, true, false},
        {"years without end", g, span(TimeUnit::Years, std::numeric_limits<double>::infinity()), "elapsed.years",
         kRefusalInvalidInput, true, false},
        {"a day before time", g, with([](TimeAdvanceRequest& r) { r.moon_day = -1; }), "moon_day",
         kRefusalInvalidInput, true, false},
        {"too many edits", g, with([&](TimeAdvanceRequest& r) {
             r.leyline_edits.assign(kMaxLeylineEdits + 1, LeylineEdit{net->name, net->nodes[0].id, std::nullopt, 1.0,
                                                                     std::nullopt});
         }),
         "leyline_edits", kRefusalStateCapacity, true, false},
        {"too many resolutions", g, with([](TimeAdvanceRequest& r) {
             r.resolutions.assign(kMaxQuestResolutions + 1, QuestResolution{"q", QuestOutcome::Completed});
         }),
         "resolutions", kRefusalStateCapacity, true, false},
        {"a world before creatures", young, days(1.0), "world.config", kRefusalInvalidInput, false, false},
        {"another recipe", other_recipe, days(1.0), "world.config", kRefusalInvalidInput, false, false},
        {"a world with no sky", skyless, days(1.0), "world.astrology", kRefusalInvalidInput, false, false},
        {"edits on a mundane world", mundane, edit({net->name, net->nodes[0].id, std::nullopt, 1.0, std::nullopt}),
         "leyline_edits", kRefusalInvalidInput, true, true},
        {"an unknown school", g, edit({"thaumaturgy", net->nodes[0].id, std::nullopt, 1.0, std::nullopt}),
         "leyline_edits[].school", kRefusalInvalidInput, true, false},
        {"a hidden school", g, edit({"blood", std::string("blood-node-0"), std::nullopt, 1.0, std::nullopt}),
         "leyline_edits[].school", kRefusalInvalidInput, true, false},
        {"an intensity past the ceiling", g, edit({net->name, net->nodes[0].id, std::nullopt, 5.0, std::nullopt}),
         "leyline_edits[].intensity", kRefusalInvalidInput, true, false},
        {"a node that does not exist", g, edit({net->name, std::string("no-such-node"), std::nullopt, 1.0, std::nullopt}),
         "leyline_edits[]", kRefusalInvalidInput, true, false},
        {"no target at all", g, edit({net->name, std::nullopt, std::nullopt, std::nullopt, std::nullopt}),
         "leyline_edits[]", kRefusalInvalidInput, true, false},
        {"a new node reusing an id", g, edit({net->name, std::nullopt, std::nullopt, std::nullopt, twin}),
         "leyline_edits[].new_node", kRefusalInvalidInput, true, false},
        {"an intensity beside a new node", g, edit({net->name, std::nullopt, std::nullopt, 1.0, fresh}),
         "leyline_edits[].new_node", kRefusalInvalidInput, true, false},
    };
    for (const Case& c : cases) {
        const std::string before = everything_of(c.world);
        const WorldTimeError refused = refusal([&] { (void)advance_world_time(c.world, c.request); }, c.what);
        check::equal(refused.field(), c.field, c.what + ": the field");
        check::equal(std::string(refused.code()), std::string(c.code), c.what + ": the code");
        check::equal(refused.retry(), c.retry, c.what + ": whether resending can help");
        check::equal(refused.refused_by_world(), c.by_world, c.what + ": who refused");
        check::is_true(!refused.message().empty(), c.what + ": a sentence");
        check::equal(everything_of(c.world), before, c.what + ": the world untouched");
    }

    // A span the ceiling or the age band REPORTS is reported whole: its edits are neither
    // applied nor judged, so a malformed one does not turn the report into a throw.
    TimeAdvanceRequest blocked = years(2000.0);
    blocked.leyline_edits.push_back({"thaumaturgy", net->nodes[0].id, std::nullopt, 1.0, std::nullopt});
    check::is_true(advance_world_time(g, blocked).report.tick.blocked.has_value(), "the ceiling reported first");
    TimeAdvanceRequest estimate = years(5000.0);
    estimate.leyline_edits = blocked.leyline_edits;
    check::is_true(!advance_world_time(g, estimate).report.tick.committed, "the age estimate reported first");
}

// --- the reported refusals --------------------------------------------------

TEST(world_time_the_work_ceiling_is_reported_and_its_suggestion_is_accepted) {
    const World& w = small();
    const double start = world_clock_of(w).day;
    const TimeAdvanceResult blocked = advance_world_time(w, years(2000.0));
    const TickReport& report = blocked.report.tick;
    check::is_true(!report.committed && report.blocked.has_value(), "reported, not run");
    check::equal(std::string(report.blocked->code), std::string("STATE_CAPACITY"), "a capacity refusal");
    check::equal(std::string(report.blocked->field), std::string("elapsed"), "of the span");
    check::equal(report.blocked->received, schedule_work(start, 2000.0 * kYear), "pricing the work it would do");
    check::is_true(report.blocked->received > kMaxTickSteps, "past the ceiling");
    check::is_true(report.tick_cost.has_value(), "the estimate beside the reason");
    // Nothing ran: the world comes back with only the clock it was measured against.
    check::equal(state_of(blocked.world, false), state_of(w, false), "nothing ran");
    check::equal(record_of(blocked.world), record_of(w), "nothing recorded");
    check::is_true(!w.world_clock && blocked.world.world_clock.has_value(), "a clock handed back where none was");
    check::equal(clock_of(blocked.world).day, start, "the clock it was measured against");

    // The suggestion is a span, measured from the caller's own clock, and is accepted as sent.
    check::is_true(report.blocked->clamp_days.has_value(), "a span that would fit");
    const double fits = *report.blocked->clamp_days;
    const TimeAdvanceResult accepted = advance_world_time(w, days(fits));
    check::is_true(accepted.report.tick.committed && !accepted.report.tick.blocked, "the suggested span runs");

    // It advises; it does not forbid.
    check::is_true(schedule_work(start, fits + 1.0) > kMaxTickSteps, "one more day is past the ceiling");
    const TimeAdvanceResult forced = advance_world_time(w, days(fits + 1.0, true));
    check::is_true(forced.report.tick.committed && !forced.report.tick.blocked, "commit runs it anyway");
    check::equal(clock_of(forced.world).day, start + fits + 1.0, "all the way");
}

TEST(world_time_an_age_span_is_reported_until_it_is_committed) {
    const World& g = genesis();
    const double start = world_clock_of(g).day;
    const TimeAdvanceResult estimate = advance_world_time(g, years(5000.0));
    const TickReport& report = estimate.report.tick;
    check::is_true(report.band == TimeBand::Age && report.ages == 1, "one age");
    check::is_true(!report.committed && !report.blocked, "an estimate, not a refusal");
    check::is_true(report.age_cost.has_value() && !report.age_cost->measured, "priced by extrapolation");
    check::equal(state_of(estimate.world, false), state_of(g, false), "nothing advanced");
    check::is_true(estimate.world.world_clock.has_value(), "the clock it read, handed back");
    check::equal(clock_of(estimate.world).day, start, "the clock it read");

    check::is_true(!advance_world_time(g, years(250000.0)).report.tick.blocked, "fifty ages is not refused");

    const TimeAdvanceResult sixty = advance_world_time(g, years(300000.0, true));
    check::is_true(sixty.report.tick.blocked.has_value() && !sixty.report.tick.committed, "sixty ages refused");
    check::equal(sixty.report.tick.blocked->received, std::int64_t{60}, "counting them");
    check::equal(sixty.report.tick.blocked->limit, kMaxAgesPerRequest, "against the ages ceiling");
    check::is_true(sixty.report.tick.age_cost.has_value(), "with the estimate attached");
    check::equal(sixty.world.history.ages.size(), g.history.ages.size(), "and no age turned");

    // The gate is the age API's own, asked rather than restated, and reported in its words.
    World foreign = g;
    foreign.religion.catalogue.sha256 = "another pantheon";
    const TimeAdvanceResult gated = advance_world_time(foreign, years(5000.0, true));
    check::is_true(gated.report.tick.blocked.has_value() && !gated.report.tick.committed, "the gate refuses");
    check::equal(gated.report.gate, std::string("validate_age_world"), "naming the check");
    check::equal(gated.report.gate_field, std::string("world.religion.catalogue"), "and the block it read");
    check::is_true(gated.report.tick.blocked->message.find("pantheon catalogue") != std::string::npos,
                   "in the gate's own words");
    check::is_true(!gated.report.tick.blocked->retry, "not a value to correct");
    check::equal(gated.world.history.ages.size(), g.history.ages.size(), "and no age turned");
    check::is_true(world_age_gate(g) == std::nullopt, "the fixture itself passes the gate");
}

// --- the age path -----------------------------------------------------------

TEST(world_time_a_committed_age_turns_exactly_the_age_it_crosses) {
    const World& before = held();
    const TimeAdvanceResult& r = aged();
    const double start = world_clock_of(before).day;
    check::is_true(r.report.tick.committed && r.report.tick.band == TimeBand::Age, "a committed age");
    check::equal(r.report.tick.ages, std::int64_t{1}, "one age");
    check::equal(r.report.tick.age_days, static_cast<std::int64_t>(kAgeDays), "of an age's days");
    check::equal(r.report.tick.to_day, start + kAgeDays, "to the end of the span");
    check::is_true(!r.report.remainder, "with nothing left over to tick");
    check::equal(r.world.history.ages.size(), before.history.ages.size() + 1, "one more age in the history");
    check::equal(r.world.age, before.age + 1, "and the world in it");
    check::equal(r.report.age.value_or(-1), static_cast<std::int64_t>(r.world.history.ages.size()), "as reported");
    check::equal(clock_of(r.world).day, start + kAgeDays, "the clock an age on");
    check::equal(clock_of(r.world).age, static_cast<std::int64_t>(r.world.history.ages.size()), "in that age");
    check::equal(clock_of(r.world).derived_from, clock_of(before).derived_from, "provenance kept");
    check::equal(surge_of(r.world).day, start + kAgeDays, "the sky read on the day the age turned");

    check::equal(count_of<AgeOperation>(r.world), count_of<AgeOperation>(before) + 1, "one age API call");
    check::equal(last_of<AgeOperation>(r.world).steps, 1, "of one step");
    const TimeOperationRecord& op = last_of<TimeOperationRecord>(r.world);
    check::is_true(op.band == TimeBand::Age && op.ages == 1 && !op.steps, "the age span's own entry");
    check::equal(op.elapsed_days, kAgeDays, "the whole ages");
    check::equal(op.from_day, start, "from");
    check::equal(op.to_day, start + kAgeDays, "to");
    check::is_true(op.leyline_edits.empty() && op.resolutions.empty(), "carrying no edits and no resolutions");
    check::equal(r.world.history.replay, std::string(kAgeReplayNote), "the age API's sentence stands");
    // The log, folded back into call order, puts the age API's own entry between the tick
    // that opened the board, the quest route's takes, and this span.
    check::equal(ops_of<TimeOperationRecord>(ticked()).at(0).first, std::size_t{0}, "the tick first");
    check::equal(ops_of<QuestOperationRecord>(held()).at(0).first, std::size_t{1}, "the takes second");
    check::equal(position_of_last<TimeOperationRecord>(r.world), std::size_t{3},
                 "this span after the age API's entry at 2");
}

TEST(world_time_an_age_turn_closes_every_open_quest_because_the_age_turned) {
    // A quest does not cross an age, and not by luck: the regenerated cast re-mints
    // positional ids, so reaping by "the id disappeared" left some quests alive as chimeras.
    const World& before = held();
    const TimeAdvanceResult& r = aged();
    const std::vector<std::string> open = ids_in(before, {QuestState::Offered, QuestState::Taken});
    check::is_true(!ids_in(before, {QuestState::Taken}).empty(), "quests held taken into the age turn");
    std::set<std::string> reaped;
    for (const QuestLogEntry& e : board_of(r.world).log) {
        if (e.event == "age_turned") reaped.insert(e.quest_id);
    }
    check::is_true(reaped == std::set<std::string>(open.begin(), open.end()),
                   "every quest open when the age turned closed because it turned");

    const double turn = clock_of(r.world).day;
    std::vector<std::string> board;
    for (const QuestRecord& q : board_of(r.world).quests) {
        board.push_back(q.quest_id);
        check::equal(q.offered_day, turn, q.quest_id + " offered by the new age, on the day it turned");
        check::is_true(q.state != QuestState::Taken, q.quest_id + " not carried across taken");
        check::is_true(q.closed_reason != QuestClosedReason::AgeTurned, q.quest_id + " not reaped by the turn that opened it");
    }
    std::vector<std::string> hooks;
    for (const HeroQuestHook& h : r.world.heroes.quest_hooks) hooks.push_back(h.hook_id);
    std::sort(board.begin(), board.end());
    std::sort(hooks.begin(), hooks.end());
    check::is_true(!board.empty() && board == hooks, "the board is the new age's hooks");

    // The log outlives the board: every closure of the old age is still there, first.
    const std::vector<QuestLogEntry>& old_log = board_of(before).log;
    check::is_true(board_of(r.world).log.size() > old_log.size(), "the log grew");
    for (std::size_t i = 0; i < old_log.size(); ++i) {
        check::equal(board_of(r.world).log[i].event, old_log[i].event, "the old age's log kept in order");
    }
}

TEST(world_time_a_measured_age_prices_the_next_one) {
    const World& w = aged().world;
    const AgeCostBasis basis = world_age_cost_basis(w);
    check::is_true(basis.recorded_seconds.has_value() && *basis.recorded_seconds > 0.0, "the advance was timed");
    check::equal(basis.recorded_ages, std::int64_t{1}, "over the one age that call carried");
    const TimeAdvanceResult next = advance_world_time(w, years(5000.0));
    check::is_true(next.report.tick.age_cost.has_value() && next.report.tick.age_cost->measured,
                   "the next estimate uses this world's own measurement");
    check::close(next.report.tick.age_cost->per_age_seconds, *basis.recorded_seconds, 0.0005, "per age");
    check::is_true(!world_age_cost_basis(genesis()).recorded_seconds, "a world never advanced has no measurement");
}

TEST(world_time_an_age_and_a_remainder_is_the_age_then_the_remainder) {
    // Dropping the remainder made one age and a month differ from an age then a month, and
    // left the log claiming an elapsed the clock never moved.
    const World& w = held();
    const double start = world_clock_of(w).day;
    const TimeAdvanceResult whole = advance_world_time(w, days(kAgeDays + 30.0, true));
    const World stepped = advance_world_time(aged().world, days(30.0)).world;
    check::equal(whole.report.tick.ages, std::int64_t{1}, "one age");
    check::equal(clock_of(whole.world).day, start + kAgeDays + 30.0, "and the month after it");
    check::equal(whole.report.tick.to_day, start + kAgeDays + 30.0, "as reported");
    check::is_true(whole.report.remainder.has_value() && whole.report.remainder->committed, "the remainder ticked");
    check::close(whole.report.remainder->elapsed_days, 30.0, 0.0, "a month of it");
    check::equal(state_of(whole.world), state_of(stepped), "the same world as an age, then a month");
    const auto ops = ops_of<TimeOperationRecord>(whole.world);
    check::is_true(ops.size() >= 2 && ops[ops.size() - 2].second->band == TimeBand::Age &&
                       ops.back().second->band == TimeBand::Day,
                   "the age span's entry, then the remainder's");
    check::is_true(ops[ops.size() - 2].first < ops.back().first, "in call order");
    check::equal(whole.world.history.replay, std::string(kReplaySentence), "the tick's sentence last");
}

TEST(world_time_reproduced_a_committed_age_whose_remainder_is_past_the_ceiling_stops_at_the_ages) {
    // REFERENCE DEFECT, reproduced and pinned: the remainder tick is sent WITHOUT `commit`,
    // so a remainder past the work ceiling is reported, not run. The ages stand, the clock
    // stops at them, and the outer report still says committed with `to_day` at the end of
    // the whole span -- the hole in the reference's all-or-nothing promise.
    const World& w = small();
    const double start = world_clock_of(w).day;
    const double remainder = 600.0 * kYear;
    check::is_true(schedule_work(start + kAgeDays, remainder) > kMaxTickSteps, "a remainder past the ceiling");
    const TimeAdvanceResult r = advance_world_time(w, days(kAgeDays + remainder, true));
    check::equal(r.world.history.ages.size(), w.history.ages.size() + 1, "the age ran");
    check::is_true(r.report.tick.committed, "the report says committed");
    check::is_true(r.report.remainder.has_value() && r.report.remainder->blocked.has_value() &&
                       !r.report.remainder->committed,
                   "while the remainder was only reported");
    check::equal(clock_of(r.world).day, start + kAgeDays, "the clock stopped at the ages");
    check::equal(r.report.tick.to_day, start + kAgeDays + remainder, "the report names the whole span");
    check::is_true(last_of<TimeOperationRecord>(r.world).band == TimeBand::Age, "no entry for the remainder");
}

TEST(world_time_reproduced_a_resolution_sent_with_an_age_span_meets_the_new_age_board) {
    // REFERENCE WART, reproduced and pinned: resolutions ride the remainder tick, which runs
    // in the new age, so a resolution for a quest of the outgoing age is refused as an
    // UNKNOWN quest -- a malformed-argument refusal for a request that was well formed.
    const World& w = held();
    std::set<std::string> reminted;
    for (const QuestRecord& q : board_of(aged().world).quests) reminted.insert(q.quest_id);
    std::string gone;
    for (const std::string& id : ids_in(w, {QuestState::Taken})) {
        if (reminted.count(id) == 0) {
            gone = id;
            break;
        }
    }
    check::is_true(!gone.empty(), "a taken quest whose id the new age does not re-mint");
    TimeAdvanceRequest request = years(5000.0, true);
    request.resolutions.push_back({gone, QuestOutcome::Completed});
    const WorldTimeError refused = refusal([&] { (void)advance_world_time(w, request); }, "an old age's quest");
    check::equal(refused.field(), gone, "refused as unknown, naming the quest");
    check::is_true(!refused.refused_by_world(), "as a malformed value, not by the world");
}

TEST(world_time_reproduced_an_age_builds_its_herds_for_the_year_the_world_stood_in) {
    // REFERENCE IMPRECISION, reproduced and pinned: stage 16 re-runs inside the age API, and
    // the reference's beast movement pass reads the world clock as it stands there -- the
    // clock from BEFORE the ages turned, since the time API writes the advanced one after.
    // So a clocked world's new age builds its herds for the year it stood in beforehand; a
    // world with no clock uses the founding year, as generation does.
    const World& w = small();
    const World ticked_on = advance_world_time(w, years(2.0)).world;
    const long long stood = clock_year(clock_of(ticked_on));
    check::is_true(stood != w.settlements.founding.end_year, "a clock in a year founding did not end in");
    const TimeAdvanceResult r = advance_world_time(ticked_on, years(5000.0, true));
    check::is_true(r.world.has_beast_movements, "herds rebuilt by the age");
    check::equal(r.world.beast_movements.year, stood, "for the year the world stood in before the turn");
    check::equal(beast_movement_year(ticked_on), stood, "the year the pass is handed");
    check::equal(beast_movement_year(w), static_cast<long long>(w.settlements.founding.end_year),
                 "and without a clock, where founding ended");
}

TEST(world_time_a_span_of_many_ages_is_turned_in_chunks_of_ten) {
    const World& w = small();
    const double start = world_clock_of(w).day;
    const TimeAdvanceResult r = advance_world_time(w, years(5000.0 * 11.0, true));
    check::is_true(r.report.tick.committed, "eleven ages committed");
    check::equal(r.report.tick.ages, std::int64_t{11}, "eleven ages");
    check::equal(r.world.history.ages.size(), w.history.ages.size() + 11, "every one of them turned");
    const auto advances = ops_of<AgeOperation>(r.world);
    check::equal(advances.size(), count_of<AgeOperation>(w) + 2, "in two calls");
    const AgeOperation& first = *advances[advances.size() - 2].second;
    const AgeOperation& second = *advances.back().second;
    check::equal(first.steps, kMaxAgeSteps, "the first of ten");
    check::equal(second.steps, 1, "the second of one");
    check::equal(first.after_age, static_cast<int>(w.history.ages.size()), "numbered on from the first");
    check::equal(second.after_age, static_cast<int>(w.history.ages.size()) + kMaxAgeSteps, "and on from the chunk");
    check::equal(clock_of(r.world).day, start + 11.0 * kAgeDays, "eleven ages on the clock");
    check::equal(count_of<TimeOperationRecord>(r.world), std::size_t{1}, "one entry for the span");
    check::equal(ops_of<TimeOperationRecord>(r.world)[0].second->ages.value_or(-1), std::int64_t{11},
                 "carrying all eleven");
    // A world that never carried a board does not acquire one by being advanced: an age
    // turn continues a lifecycle, it does not start one.
    check::is_true(!w.quests && !r.world.quests, "no board begun by an age turn");
}

// --- the quest route --------------------------------------------------------

TEST(world_time_a_quest_taken_then_abandoned_leaves_the_board_as_the_reference_leaves_it) {
    const World& w = ticked();
    const std::string id = ids_in(w, {QuestState::Offered}).at(0);
    const double now = clock_of(w).day;

    QuestRequest take;
    take.actions.push_back({id, QuestActionKind::Take, std::nullopt});
    const QuestRequestResult taken = world_quest_request(w, take);
    const QuestRecord* row = quest(taken.world, id);
    check::is_true(row->state == QuestState::Taken, "taken");
    check::is_true(!row->closed_day && !row->closed_reason, "and still open");
    check::equal(board_of(taken.world).log.size(), board_of(w).log.size() + 1, "one log entry for the take");
    check::equal(board_of(taken.world).log.back().event, std::string("taken"), "logged");
    check::equal(board_of(taken.world).log.back().day, now, "on the clock's day");
    check::equal(taken.report.day, now, "the report's day is the clock's");
    check::equal(taken.report.actions.size(), std::size_t{1}, "one action applied");

    QuestRequest abandon;
    abandon.actions.push_back({id, QuestActionKind::Abandon, std::nullopt});
    const QuestRequestResult dropped = world_quest_request(taken.world, abandon);
    row = quest(dropped.world, id);
    check::is_true(row->state == QuestState::Abandoned, "abandoned");
    check::is_true(row->closed_reason == QuestClosedReason::Abandoned, "closed as abandoned, not as resolved");
    check::equal(row->closed_day.value_or(-1.0), now, "on the clock's day");
    const std::vector<QuestLogEntry>& log = board_of(dropped.world).log;
    check::equal(log.size(), board_of(w).log.size() + 2, "two entries");
    check::equal(log[log.size() - 2].event, std::string("taken"), "taken");
    check::equal(log.back().event, std::string("abandoned"), "then abandoned");
    check::equal(count_of<QuestOperationRecord>(dropped.world), count_of<QuestOperationRecord>(w) + 2, "two calls");

    // As one batch, in caller order: the same board, one operation.
    QuestRequest both;
    both.actions = {{id, QuestActionKind::Take, std::nullopt}, {id, QuestActionKind::Abandon, std::nullopt}};
    const QuestRequestResult batch = world_quest_request(w, both);
    check::equal(state_of(batch.world), state_of(dropped.world), "the batch leaves the same world");
    check::equal(batch.report.actions.size(), std::size_t{2}, "two actions");
    check::is_true(batch.report.actions[0].state == QuestState::Taken &&
                       batch.report.actions[1].state == QuestState::Abandoned,
                   "each reported with the state it left");
    const QuestOperationRecord& op = last_of<QuestOperationRecord>(batch.world);
    check::equal(op.actions.size(), std::size_t{2}, "one operation carrying both");
    check::equal(op.actions[0].quest_id, id, "the quest");
    check::equal(op.actions[0].day, now, "and the day it was acted on");
    check::equal(position_of_last<QuestOperationRecord>(batch.world), log_size(w), "at the end of the log");

    // Nothing else moves: not the clock, not the field, not the replay sentence, no timing.
    check::equal(clock_of(batch.world).day, now, "the clock where it was");
    check::equal(batch.world.history.replay, w.history.replay, "the replay sentence as found");
    check::is_true(batch.world.timing_ms == w.timing_ms, "no timing written");
    check::is_true(batch.world.magic.networks.at(manifest_network(w)->name).nodes[0].intensity ==
                       manifest_network(w)->nodes[0].intensity,
                   "the ley field untouched");
}

TEST(world_time_a_taken_quest_outlives_its_offer_window) {
    // A sweep tests the window only while a quest is offered: taking it is what holds it.
    const World& w = ticked();
    const std::vector<std::string> open = ids_in(w, {QuestState::Offered});
    QuestRequest take;
    take.actions.push_back({open.at(0), QuestActionKind::Take, std::nullopt});
    const World held_one = world_quest_request(w, take).world;
    const World later = advance_world_time(held_one, years(10.0)).world;
    check::is_true(quest(later, open.at(0))->state == QuestState::Taken, "still taken ten years on");
    check::is_true(!quest(later, open.at(0))->closed_reason, "never closed");
    check::is_true(quest(later, open.at(1))->closed_reason == QuestClosedReason::Expired, "while the untaken offer lapsed");
}

TEST(world_time_the_quest_route_refuses_by_field) {
    const World& w = ticked();
    const std::string before = everything_of(w);
    const std::vector<std::string> open = ids_in(w, {QuestState::Offered});
    const auto act = [&](const World& world, std::vector<QuestAction> actions, int api = kQuestApiVersion) {
        QuestRequest r;
        r.api_version = api;
        r.actions = std::move(actions);
        return world_quest_request(world, r);
    };
    const auto take = [](std::string id, std::optional<double> day = std::nullopt) {
        return QuestAction{std::move(id), QuestActionKind::Take, day};
    };

    WorldTimeError e = refusal([&] { (void)act(w, {}, 99); }, "a future api");
    check::is_true(e.field() == "api_version" && e.code() == kRefusalUnsupportedVersion && !e.retry(),
                   "an api this build does not speak, not worth resending");
    e = refusal([&] { (void)act(w, std::vector<QuestAction>(kMaxQuestActions + 1, take(open.at(0)))); }, "too many");
    check::is_true(e.field() == "actions" && e.code() == kRefusalStateCapacity, "the capacity");
    e = refusal([&] { (void)act(w, {take("")}); }, "an empty id");
    check::equal(e.field(), std::string("quest_id"), "an empty id");
    e = refusal([&] { (void)act(w, {take(open.at(0), std::numeric_limits<double>::quiet_NaN())}); }, "no day");
    check::equal(e.field(), std::string("day"), "a day that is not a number");
    e = refusal([&] { (void)act(w, {take(open.at(0), -1.0)}); }, "before time");
    check::equal(e.field(), std::string("day"), "a day before founding year 0");
    e = refusal([&] { (void)act(genesis(), {take(open.at(0))}); }, "no board");
    check::is_true(e.field() == "world.quests" && !e.retry(), "a world never ticked has no board to act on");
    World young = w;
    young.config.phase = 12;
    e = refusal([&] { (void)act(young, {take(open.at(0))}); }, "the tick's gate");
    check::equal(e.field(), std::string("world.config"), "the same gate the tick asks");
    e = refusal([&] { (void)act(w, {take("hook-nobody-0")}); }, "unknown");
    check::equal(e.field(), std::string("hook-nobody-0"), "an unknown quest names itself");

    // Refused by the world: the id is real, the request well formed, the world will not.
    const World taken = act(w, {take(open.at(0))}).world;
    e = refusal([&] { (void)act(taken, {take(open.at(0))}); }, "taken twice");
    check::is_true(e.field() == "quest_id" && e.refused_by_world(), "a quest already taken");
    check::is_true(!e.candidates().empty() && e.candidates().size() <= 5, "naming quests still open");
    const double lapsed = quest(w, open.at(0))->expires_day + 1.0;
    e = refusal([&] { (void)act(w, {take(open.at(0), lapsed)}); }, "a lapsed offer");
    check::is_true(e.field() == "quest_id" && e.refused_by_world(), "a take after the offer lapsed");
    const World dropped = act(w, {QuestAction{open.at(0), QuestActionKind::Abandon, std::nullopt}}).world;
    e = refusal([&] { (void)act(dropped, {QuestAction{open.at(0), QuestActionKind::Abandon, std::nullopt}}); }, "closed");
    check::is_true(e.refused_by_world(), "abandoning a closed quest");

    // All or nothing: a legal action followed by a refused one leaves nothing behind.
    (void)refusal([&] { (void)act(w, {take(open.at(0)), take("hook-nobody-0")}); }, "a half-legal batch");
    (void)refusal([&] { (void)act(w, {take(open.at(0)), take(open.at(0))}); }, "a batch that takes twice");
    check::equal(everything_of(w), before, "the caller's world after every refusal");

    // An empty batch changes and records nothing.
    const QuestRequestResult none = act(w, {});
    check::equal(everything_of(none.world), before, "an empty batch changes nothing");
    check::is_true(none.report.actions.empty() && none.report.day == clock_of(w).day, "and reports the clock's day");

    // A day the caller names is used verbatim, in the past or the future.
    const double earlier = clock_of(w).day - 5.0;
    const QuestRequestResult dated = act(w, {take(open.at(0), earlier)});
    check::equal(board_of(dated.world).log.size(), board_of(w).log.size() + 1, "the take logged");
    check::equal(board_of(dated.world).log.back().day, earlier, "the named day, verbatim");
    check::equal(dated.report.day, clock_of(w).day, "while the report keeps the clock's");
}

TEST(world_time_an_age_is_priced_from_the_last_age_advance_not_the_last_entry) {
    // The operation log is shared by every API. After a tick, the last entry is the tick's,
    // and it carries steps of its own; the divisor is the ages the last ADVANCE carried.
    AgeAdvanceRequest three;
    three.steps = 3;
    const World advanced = advance_ages(small(), three);
    check::equal(count_of<AgeOperation>(advanced), std::size_t{1}, "one advance logged");
    check::equal(last_of<AgeOperation>(advanced).steps, 3, "of three steps");
    check::equal(world_age_cost_basis(advanced).recorded_ages, std::int64_t{3}, "priced over three before any tick");
    const World after = advance_world_time(advanced, days(1.0, true)).world;
    check::is_true(!after.history.operations.empty() &&
                       std::holds_alternative<TimeOperationRecord>(after.history.operations.back()),
                   "a tick as the last entry");
    check::equal(count_of<AgeOperation>(after), std::size_t{1}, "the advance still logged after the tick");
    check::is_true(after.timing_ms.count("age_advance_total") != 0, "the advance's timing still held after the tick");
    check::equal(world_age_cost_basis(after).recorded_ages, std::int64_t{3}, "priced over the advance's three ages");
}
