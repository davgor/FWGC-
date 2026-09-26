// A roster post's day and what a hold does to it: first the schedule alone, on a made-up person,
// then through the director over the seed-42 size-33 world. Every test that looks for something
// first checks it is there.
#include "orchestrator/director.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "fwg/api/serialize.hpp"
#include "fwg/api/world_time.hpp"
#include "schedule.hpp"

namespace {

using orchestrator::Detour;
using orchestrator::Director;
using orchestrator::Doing;
using orchestrator::Stint;
namespace schedule = orchestrator::schedule;

constexpr double kMinute = 1.0 / 1440.0;
// A calendar day's sunrise, as far from zero as a world's clock is: the arithmetic is tested at
// the magnitude it runs at.
constexpr double kDay = 360000.0;

double at_hour(double hour) { return kDay + hour / 24.0; }

bool close_to(double a, double b) { return std::abs(a - b) < 1e-7; }  // under a hundredth of a second

std::string describe(const Stint& s) {
    std::ostringstream out;
    out << orchestrator::to_string(s.doing) << " [" << (s.from_day - kDay) * 24.0 << "h, " << (s.to_day - kDay) * 24.0
        << "h) " << s.from_along << "->" << s.to_along;
    return out.str();
}

bool same(const Stint& a, const Stint& b) {
    return a.doing == b.doing && a.from_day == b.from_day && a.to_day == b.to_day && a.from_along == b.from_along &&
           a.to_along == b.to_along;
}

// The first stint doing `doing` that begins at or after `after`.
const Stint* first_of(const std::vector<Stint>& day, Doing doing, double after = -1.0) {
    for (const Stint& s : day) {
        if (s.doing == doing && s.from_day >= after) return &s;
    }
    return nullptr;
}

// The stints beginning at or after `from`.
std::vector<Stint> from_on(const std::vector<Stint>& day, double from) {
    std::vector<Stint> out;
    for (const Stint& s : day) {
        if (s.from_day >= from) out.push_back(s);
    }
    return out;
}

// The stints ending at or before `to`.
std::vector<Stint> until(const std::vector<Stint>& day, double to) {
    std::vector<Stint> out;
    for (const Stint& s : day) {
        if (s.to_day <= to) out.push_back(s);
    }
    return out;
}

void same_days(const std::vector<Stint>& got, const std::vector<Stint>& want, const std::string& what) {
    check::equal(got.size(), want.size(), what + ": as many stints");
    for (std::size_t i = 0; i < got.size() && i < want.size(); ++i) {
        check::is_true(same(got[i], want[i]), what + ": stint " + std::to_string(i) + " " + describe(got[i]) +
                                                  " to be " + describe(want[i]));
    }
}

// The stints run on without a gap or an overlap across [from, to), each where the last left off,
// and a walk runs only between home and work.
void tiles(const std::vector<Stint>& day, double from, double to, const std::string& what) {
    check::is_true(!day.empty(), what + ": stints");
    if (day.empty()) return;
    check::is_true(day.front().from_day <= from && day.back().to_day >= to, what + ": the whole span covered");
    for (std::size_t i = 0; i < day.size(); ++i) {
        const Stint& s = day[i];
        check::is_true(s.from_day < s.to_day, what + ": " + describe(s) + " to last a while");
        if (i > 0) {
            check::is_true(day[i - 1].to_day == s.from_day, what + ": " + describe(s) + " to begin where the last ended");
            check::is_true(close_to(day[i - 1].to_along, s.from_along), what + ": " + describe(s) + " to begin where the last left them");
        }
        if (s.doing == Doing::walking) {
            check::is_true(s.from_along != s.to_along, what + ": a walk to go somewhere");
        } else {
            check::is_true(s.from_along == s.to_along, what + ": " + describe(s) + " to stay put");
        }
    }
}

// A smith ten minutes' walk from the smithy: 600 straight metres.
const schedule::Person kSmith{schedule::kDayTrade, schedule::walk_days(600)};

}  // namespace

// ---------------------------------------------------------------------------
// The schedule alone.
// ---------------------------------------------------------------------------

TEST(a_post_keeps_its_building_s_hours_and_a_watch_its_shift) {
    const auto hours_are = [](const schedule::Hours& got, const schedule::Hours& want) {
        return got.wake == want.wake && got.work_from == want.work_from && got.work_until == want.work_until &&
               got.sleep == want.sleep;
    };
    check::is_true(hours_are(schedule::hours_of("building.smithy", 0), schedule::kDayTrade), "a smithy keeps day hours");
    check::is_true(hours_are(schedule::hours_of("building.bakery", 1), schedule::kEarlyTrade), "a bakery keeps early hours");
    check::is_true(hours_are(schedule::hours_of("building.inn", 0), schedule::kLateTrade), "an inn keeps late hours");
    check::is_true(hours_are(schedule::hours_of("building.nothing_known", 0), schedule::kDayTrade), "anything else, day hours");
    for (int ordinal = 0; ordinal < 6; ++ordinal) {
        check::is_true(hours_are(schedule::hours_of("building.gatehouse", ordinal), schedule::kWatches[ordinal % 3]),
                       "a gate's watch " + std::to_string(ordinal) + " in its turn");
    }
    // Every set of hours runs wake, work, sleep in order, with a margin either side of work.
    for (const schedule::Hours& h : {schedule::kDayTrade, schedule::kEarlyTrade, schedule::kLateTrade, schedule::kWatches[0],
                                     schedule::kWatches[1], schedule::kWatches[2]}) {
        check::is_true(h.wake < h.work_from && h.work_from < h.work_until && h.work_until < h.sleep && h.sleep < h.wake + 24.0,
                       "hours in order round the day");
    }
}

TEST(a_walk_takes_the_commute_at_about_a_metre_a_second_and_never_under_a_minute) {
    check::is_true(close_to(schedule::walk_days(600), 10.0 * kMinute), "600 m in ten minutes");
    check::is_true(close_to(schedule::walk_days(10), kMinute), "10 m in the shortest walk, a minute");
    check::is_true(close_to(schedule::walk_days(-5), kMinute), "nothing shorter");
}

TEST(a_day_runs_round_the_clock_for_every_trade_and_walk) {
    const std::vector<schedule::Hours> trades = {schedule::kDayTrade, schedule::kEarlyTrade, schedule::kLateTrade,
                                                 schedule::kWatches[0], schedule::kWatches[1], schedule::kWatches[2]};
    for (const schedule::Hours& h : trades) {
        // At their post; a minute's walk; ten minutes'; and one past the margin around work, cut.
        for (const double walk : {0.0, kMinute, 10.0 * kMinute, 3.0 / 24.0}) {
            const schedule::Person p{h, walk};
            const std::vector<Stint> day = schedule::stints(p, {}, kDay, kDay + 2.0);
            const std::string what = "hours from " + std::to_string(h.work_from) + ", walk " + std::to_string(walk / kMinute);
            tiles(day, kDay, kDay + 2.0, what);
            int walks = 0;
            for (const Stint& s : day) {
                if (s.doing == Doing::walking) ++walks;
                if (walk == 0.0) check::equal(s.from_along, 1.0, what + ": someone at their post stays at it");
                if (s.doing == Doing::working) check::equal(s.from_along, 1.0, what + ": work at the workplace");
            }
            check::is_true(walk == 0.0 ? walks == 0 : walks >= 4, what + ": two walks a day, or none at a post");
            // The sleep is never eaten: each is as long as the hours say.
            for (const Stint& s : day) {
                if (s.doing != Doing::asleep || s.from_day < kDay || s.to_day > kDay + 2.0) continue;
                check::is_true(close_to(s.to_day - s.from_day, (h.wake + 24.0 - h.sleep) / 24.0), what + ": a whole night's sleep");
            }
        }
    }
}

TEST(a_walk_to_work_held_twenty_minutes_arrives_twenty_minutes_late_and_the_shop_still_shuts_on_time) {
    const std::vector<Stint> plan = schedule::stints(kSmith, {}, kDay, kDay + 1.0);
    const Stint* walk = first_of(plan, Doing::walking);
    const Stint* work = first_of(plan, Doing::working);
    check::is_true(walk && work && walk->from_along == 0.0 && walk->to_along == 1.0, "the premise: a walk to work, then work");
    check::is_true(close_to(walk->to_day - walk->from_day, 10.0 * kMinute) && walk->to_day == work->from_day,
                   "the premise: ten minutes' walk, arriving as work begins");
    const double midway = (walk->from_day + walk->to_day) / 2.0;

    std::vector<Detour> detours;
    schedule::hold(kSmith, detours, midway, midway + 20.0 * kMinute);
    const std::vector<Stint> day = schedule::stints(kSmith, detours, kDay, kDay + 1.0);
    tiles(day, kDay, kDay + 1.0, "the held day");
    const Stint* held = first_of(day, Doing::held);
    check::is_true(held != nullptr, "a held stint");
    check::is_true(held->from_day == midway && close_to(held->to_day, midway + 20.0 * kMinute), "held twenty minutes from midway");
    check::is_true(close_to(held->from_along, 0.5) && held->to_along == held->from_along, "held where they stood, half way");
    const Stint* late = first_of(day, Doing::working);
    check::is_true(late != nullptr, "work, late");
    check::is_true(close_to(late->from_day, work->from_day + 20.0 * kMinute), "arriving twenty minutes late");
    check::is_true(late->to_day == work->to_day, "and shutting when the smithy always shuts");
    check::is_true(close_to((work->to_day - work->from_day) - (late->to_day - late->from_day), 20.0 * kMinute),
                   "open twenty minutes less");
    same_days(from_on(day, work->to_day), from_on(plan, work->to_day), "from closing on, the day as planned");
}

TEST(a_hold_at_work_within_its_hours_costs_nothing) {
    const std::vector<Stint> plan = schedule::stints(kSmith, {}, kDay, kDay + 1.0);
    const Stint* work = first_of(plan, Doing::working);
    check::is_true(work != nullptr, "the premise: work");
    const double noon = at_hour(6.0);
    check::is_true(work->from_day < noon && noon + 30.0 * kMinute < work->to_day, "the premise: noon within the hours");
    std::vector<Detour> detours;
    schedule::hold(kSmith, detours, noon, noon + 30.0 * kMinute);
    const std::vector<Stint> day = schedule::stints(kSmith, detours, kDay, kDay + 1.0);
    tiles(day, kDay, kDay + 1.0, "the held day");
    double at_work = 0.0;
    for (const Stint& s : day) {
        if ((s.doing == Doing::working || s.doing == Doing::held) && s.from_along == 1.0) at_work += s.to_day - s.from_day;
    }
    check::is_true(close_to(at_work, work->to_day - work->from_day), "at the smithy just as long");
    check::equal(first_of(day, Doing::held)->from_along, 1.0, "held at the smithy");
    const Stint* before = first_of(day, Doing::working);
    check::is_true(before && before->from_day == work->from_day && before->to_day == noon, "at work from opening until held");
    same_days(from_on(day, work->to_day), from_on(plan, work->to_day), "from closing on, the day as planned");
    same_days(until(day, work->from_day), until(plan, work->from_day), "and before work, untouched");
}

TEST(a_hold_past_closing_comes_out_of_the_evening_and_bedtime_holds) {
    const std::vector<Stint> plan = schedule::stints(kSmith, {}, kDay, kDay + 1.0);
    const Stint* work = first_of(plan, Doing::working);
    const Stint* evening = work ? first_of(plan, Doing::at_home, work->to_day) : nullptr;
    const Stint* night = first_of(plan, Doing::asleep, kDay);
    check::is_true(work && evening && night, "the premise: work, an evening at home, a night");
    const double before_close = work->to_day - 10.0 * kMinute;
    std::vector<Detour> detours;
    schedule::hold(kSmith, detours, before_close, before_close + 30.0 * kMinute);
    const std::vector<Stint> day = schedule::stints(kSmith, detours, kDay, kDay + 1.0);
    tiles(day, kDay, kDay + 1.0, "the held day");
    const Stint* home = first_of(day, Doing::at_home, work->to_day);
    check::is_true(home != nullptr, "an evening at home");
    check::is_true(close_to(home->from_day, evening->from_day + 20.0 * kMinute), "home twenty minutes late");
    check::is_true(home->to_day == evening->to_day, "and to bed when they always go");
    same_days(from_on(day, evening->to_day), from_on(plan, evening->to_day), "the night as planned");
}

TEST(held_on_the_way_past_the_shop_s_closing_they_turn_for_home_and_the_shop_stays_shut) {
    const std::vector<Stint> plan = schedule::stints(kSmith, {}, kDay, kDay + 1.0);
    const Stint* walk = first_of(plan, Doing::walking);
    const Stint* work = first_of(plan, Doing::working);
    const Stint* evening = work ? first_of(plan, Doing::at_home, work->to_day) : nullptr;
    check::is_true(walk && work && evening, "the premise: a walk to work, work, an evening");
    const double midway = (walk->from_day + walk->to_day) / 2.0;
    const double released = work->to_day + 45.0 * kMinute;
    std::vector<Detour> detours;
    schedule::hold(kSmith, detours, midway, released);
    const std::vector<Stint> day = schedule::stints(kSmith, detours, kDay, kDay + 1.0);
    tiles(day, kDay, kDay + 1.0, "the held day");
    check::is_true(first_of(day, Doing::working, walk->from_day) == nullptr || first_of(day, Doing::working, walk->from_day)->from_day >= kDay + 1.0,
                   "no work that day");
    const Stint* back = first_of(day, Doing::walking, released);
    check::is_true(back != nullptr && back->from_day == released, "walking on when let go");
    check::is_true(close_to(back->from_along, 0.5) && back->to_along == 0.0, "back home, from half way");
    check::is_true(close_to(back->to_day - back->from_day, 5.0 * kMinute), "the five minutes back");
    const Stint* home = first_of(day, Doing::at_home, released);
    check::is_true(home && home->from_day == back->to_day && home->to_day == evening->to_day, "home until bedtime");
}

TEST(a_hold_begun_while_another_runs_holds_them_until_the_later_ends) {
    const double t = at_hour(6.0);
    std::vector<Detour> detours;
    schedule::hold(kSmith, detours, t, t + 30.0 * kMinute);
    schedule::hold(kSmith, detours, t + 10.0 * kMinute, t + 20.0 * kMinute);  // within the first
    const std::vector<Stint> day = schedule::stints(kSmith, detours, kDay, kDay + 1.0);
    tiles(day, kDay, kDay + 1.0, "the twice-held day");
    double held = 0.0;
    for (const Stint& s : day) {
        if (s.doing == Doing::held) held += s.to_day - s.from_day;
    }
    check::is_true(close_to(held, 30.0 * kMinute), "held the full thirty minutes");
    check::equal(detours.size(), std::size_t(2), "two detours, the first cut where the second begins");
    check::is_true(detours[0].to_day == detours[1].from_day, "meeting without overlapping");
    check::throws([&] { schedule::hold(kSmith, detours, t, t + kMinute); }, "a hold begun before the last one did");
}

TEST(a_day_asked_of_any_span_reads_the_same_as_the_whole) {
    // A detour is worked out once, when held, so a day reads alike however it is asked.
    std::vector<Detour> detours;
    for (int d = 0; d < 5; ++d) {
        const double on_the_way = at_hour(24.0 * d + 0.95);
        schedule::hold(kSmith, detours, on_the_way, on_the_way + 45.0 * kMinute);
    }
    const std::vector<Stint> whole = schedule::stints(kSmith, detours, kDay - 1.0, kDay + 7.0);
    tiles(whole, kDay - 1.0, kDay + 7.0, "a week of held mornings");
    for (int d = 0; d < 6; ++d) {
        const std::vector<Stint> one = schedule::stints(kSmith, detours, kDay + d, kDay + d + 1.0);
        std::vector<Stint> want;
        for (const Stint& s : whole) {
            if (s.to_day > kDay + d && s.from_day < kDay + d + 1.0) want.push_back(s);
        }
        same_days(one, want, "day " + std::to_string(d));
    }
}

TEST(someone_housed_nowhere_lives_at_their_post_and_a_hold_there_costs_only_the_overrun) {
    const schedule::Person keeper{schedule::kDayTrade, 0.0};
    const std::vector<Stint> plan = schedule::stints(keeper, {}, kDay, kDay + 1.0);
    check::is_true(first_of(plan, Doing::walking) == nullptr, "no walk");
    const Stint* night = first_of(plan, Doing::asleep, kDay);
    check::is_true(night != nullptr, "the premise: a night");
    std::vector<Detour> detours;
    const double late = night->from_day - 15.0 * kMinute;
    schedule::hold(keeper, detours, late, late + 45.0 * kMinute);
    const std::vector<Stint> day = schedule::stints(keeper, detours, kDay, kDay + 1.0);
    tiles(day, kDay, kDay + 1.0, "the held day");
    const Stint* sleep = first_of(day, Doing::asleep, late);
    check::is_true(sleep && close_to(sleep->from_day, night->from_day + 30.0 * kMinute) && sleep->to_day == night->to_day,
                   "to bed thirty minutes late, up on time");
}

// ---------------------------------------------------------------------------
// Through the director, over a world.
// ---------------------------------------------------------------------------

namespace {

const fwg::World& fresh() {
    static const fwg::World world = [] {
        fwg::Request request;
        request.seed = 42;
        request.size = 33;
        return fwg::generate(fwg::resolve(request));
    }();
    return world;
}

std::string json_of(const fwg::World& world) {
    std::ostringstream out;
    fwg::write_world_json(out, world, /*pretty=*/false);
    return out.str();
}

double clock_of(const Director& d) { return fwg::world_clock_of(d.world()).day; }

// A city post in a day trade who lives far enough off to be met on the way: the first in roster
// order.
const fwg::NpcPerson* a_walker(const fwg::World& world) {
    for (const fwg::NpcPerson& p : world.npcs.people) {
        if (!p.is_cast && p.alive && p.site_kind == std::optional<std::string>("city") && p.commute_m && *p.commute_m >= 60 &&
            p.building_id == std::optional<std::string>("building.smithy"))
            return &p;
    }
    return nullptr;
}

}  // namespace

TEST(every_living_post_has_a_day_and_no_one_else_has_one) {
    const Director d(fresh());
    const double today = std::floor(clock_of(d));
    int posts = 0, at_a_post = 0;
    for (const fwg::NpcPerson& p : d.world().npcs.people) {
        const std::optional<orchestrator::Whereabouts> w = d.whereabouts(p.uid);
        if (p.is_cast || !p.alive) {
            check::is_true(!w && d.day_of(p.uid, today).empty(), p.uid + " to have no day: a cast record or the dead");
            continue;
        }
        ++posts;
        check::is_true(w.has_value(), p.uid + " to be somewhere");
        if (!w) continue;
        check::equal(w->site, p.site_uid.value_or(""), p.uid + "'s site");
        check::equal(w->home_plot, p.home_plot_id.value_or(""), p.uid + "'s home");
        check::is_true(w->stint.from_day <= w->day && w->day < w->stint.to_day, p.uid + " in the stint under way");
        const std::vector<Stint> day = d.day_of(p.uid, today);
        tiles(day, today, today + 1.0, p.uid);
        if (!p.home_plot_id) {
            ++at_a_post;
            check::is_true(first_of(day, Doing::walking) == nullptr, p.uid + ", housed nowhere, never to walk");
        }
    }
    check::is_true(posts > 1000 && at_a_post > 0, "the premise: posts, some housed nowhere, got " + std::to_string(posts));
    int cast = 0;
    for (const fwg::Hero& hero : d.world().heroes.people) {
        ++cast;
        check::is_true(!d.whereabouts(hero.uid), hero.uid + " a hero, with no day");
    }
    check::is_true(cast > 0, "the premise: heroes");
}

TEST(everyone_at_a_site_is_listed_where_they_are) {
    const Director d(fresh());
    const fwg::NpcPerson* walker = a_walker(d.world());
    check::is_true(walker != nullptr, "the premise: a smith living at least 60 m from the smithy");
    const std::string site = *walker->site_uid;
    std::set<std::string> want;
    for (const fwg::NpcPerson& p : d.world().npcs.people) {
        if (!p.is_cast && p.alive && p.site_uid == std::optional<std::string>(site)) want.insert(p.uid);
    }
    std::set<std::string> got;
    for (const orchestrator::Whereabouts& w : d.whereabouts_at(site)) {
        got.insert(w.person);
        check::equal(w.site, site, w.person + " at the site asked of");
    }
    check::is_true(!want.empty() && got == want, "every living post of " + site + " and no one else");
    check::is_true(d.whereabouts_at("no-such-site").empty(), "nobody at a site the roster does not name");
}

TEST(through_the_director_a_smith_held_on_the_way_opens_late_and_shuts_on_time) {
    Director d(fresh());
    const fwg::NpcPerson* walker = a_walker(d.world());
    check::is_true(walker != nullptr, "the premise: a smith living at least 60 m from the smithy");
    const std::string uid = walker->uid;
    const double tomorrow = std::floor(clock_of(d)) + 1.0;
    const std::vector<Stint> plan = d.day_of(uid, tomorrow);
    const Stint* walk = first_of(plan, Doing::walking);
    const Stint* work = first_of(plan, Doing::working);
    check::is_true(walk && work && walk->to_along == 1.0 && walk->to_day == work->from_day, "the premise: the walk to the smithy");
    const double midway = (walk->from_day + walk->to_day) / 2.0;
    d.advance(midway - clock_of(d));
    const std::optional<orchestrator::Whereabouts> on_the_way = d.whereabouts(uid);
    check::is_true(on_the_way && on_the_way->stint.doing == Doing::walking && std::abs(on_the_way->along - 0.5) < 1e-3,
                   "met half way to the smithy");

    d.hold(uid, 20.0);
    const std::optional<orchestrator::Whereabouts> held = d.whereabouts(uid);
    check::is_true(held && held->stint.doing == Doing::held, "held");
    const std::vector<Stint> day = d.day_of(uid, tomorrow);
    tiles(day, tomorrow, tomorrow + 1.0, uid + "'s held day");
    const Stint* late = first_of(day, Doing::working);
    check::is_true(late != nullptr, "work, late");
    check::is_true(std::abs(late->from_day - (work->from_day + 20.0 * kMinute)) < 1e-6, "twenty minutes late");
    check::is_true(late->to_day == work->to_day, "shut on time");
    same_days(from_on(day, work->to_day), from_on(plan, work->to_day), "from closing on, as planned");
    same_days(d.day_of(uid, tomorrow + 1.0), Director(fresh()).day_of(uid, tomorrow + 1.0), "the next day as planned");
}

TEST(a_hold_changes_neither_the_world_nor_what_anyone_remembers_nor_what_they_say) {
    const fwg::NpcPerson* walker = a_walker(fresh());
    check::is_true(walker != nullptr, "the premise: a smith");
    const std::string uid = walker->uid;
    Director held(fresh());
    Director not_held(fresh());
    const std::string before = json_of(held.world());
    held.hold(uid, 20.0);
    check::is_true(json_of(held.world()) == before, "the world document untouched");
    check::is_true(held.acquaintance(uid) == nullptr, "nothing remembered of a hold");
    orchestrator::DialogueAct greet;
    greet.kind = orchestrator::ActKind::Greet;
    orchestrator::DialogueAct talk;
    talk.kind = orchestrator::ActKind::SmallTalk;
    for (const orchestrator::DialogueAct& act : {greet, talk}) {
        const orchestrator::FactPacket a = held.respond(uid, act);
        const orchestrator::FactPacket b = not_held.respond(uid, act);
        check::equal(orchestrator::packet_text(a), orchestrator::packet_text(b), "the same packet, held or not");
    }
}

TEST(replay_rebuilds_every_day_the_holds_bent) {
    const fwg::NpcPerson* walker = a_walker(fresh());
    check::is_true(walker != nullptr, "the premise: a smith");
    const std::string uid = walker->uid;
    std::string other;
    for (const fwg::NpcPerson& p : fresh().npcs.people) {
        if (!p.is_cast && p.alive && !p.home_plot_id && p.site_uid) {
            other = p.uid;  // someone who lives at their post
            break;
        }
    }
    check::is_true(!other.empty(), "the premise: someone housed nowhere");
    Director d(fresh());
    d.advance(0.3);
    d.hold(uid, 15.0);
    orchestrator::DialogueAct greet;
    greet.kind = orchestrator::ActKind::Greet;
    (void)d.respond(uid, greet);
    d.hold(uid, 5.0);  // while the first still runs
    d.advance(0.1);
    d.hold(other, 90.0);
    d.advance(0.75);
    d.hold(uid, 600.0);
    const std::size_t holds = static_cast<std::size_t>(std::count_if(
        d.inputs().begin(), d.inputs().end(), [](const orchestrator::Input& i) { return std::holds_alternative<orchestrator::Hold>(i); }));
    check::equal(holds, std::size_t(4), "every hold recorded");
    const Director again = orchestrator::replay(fresh(), d.inputs());
    const double today = std::floor(clock_of(d));
    for (const std::string& who : {uid, other}) {
        for (const double day : {today - 1.0, today, today + 1.0}) {
            same_days(again.day_of(who, day), d.day_of(who, day), who + " on day " + std::to_string(day - today));
        }
        const std::optional<orchestrator::Whereabouts> a = d.whereabouts(who);
        const std::optional<orchestrator::Whereabouts> b = again.whereabouts(who);
        check::is_true(a && b && same(a->stint, b->stint) && a->along == b->along, who + " where they were");
    }
}

TEST(a_hold_is_refused_for_no_time_too_long_a_time_or_nobody_and_changes_no_one_without_a_day) {
    Director d(fresh());
    const fwg::NpcPerson* walker = a_walker(d.world());
    check::is_true(walker != nullptr, "the premise: a smith");
    const std::string uid = walker->uid;
    for (const double minutes : {0.0, -5.0, 1440.5, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        check::throws([&] { d.hold(uid, minutes); }, "a hold of " + std::to_string(minutes) + " minutes");
    }
    check::throws([&] { d.hold("npc-no-such-person", 20.0); }, "a hold of nobody");
    check::throws([&] { (void)d.day_of(uid, std::numeric_limits<double>::quiet_NaN()); }, "a day that is not a day");
    check::equal(d.inputs().size(), std::size_t(0), "nothing refused recorded");
    d.hold(uid, 1440.0);  // a whole day is allowed
    const fwg::Hero& hero = d.world().heroes.people.front();
    d.hold(hero.uid, 20.0);
    check::equal(d.inputs().size(), std::size_t(2), "a hero's hold recorded");
    check::is_true(!d.whereabouts(hero.uid) && d.day_of(hero.uid, clock_of(d)).empty(), "and the hero still without a day");
}
