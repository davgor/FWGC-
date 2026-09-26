#include "schedule.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace orchestrator::schedule {

namespace {

constexpr double kHoursPerDay = 24.0;
constexpr double kMinutesPerDay = 1440.0;

// Bread, fields, herds, boats, mills, water, the market's steward and the stable: trades that
// begin before the sun.
constexpr std::string_view kEarlyBuildings[] = {
    "building.bakery",           "building.butchery",         "building.cart_yard",
    "building.cistern",          "building.dock",             "building.farmstead",
    "building.garden_orchard",   "building.hamlet_barn",      "building.hamlet_cart_shed",
    "building.hamlet_farmyard",  "building.hamlet_landing",   "building.hamlet_net_shed",
    "building.hamlet_staging_yard", "building.hamlet_well",   "building.hand_mill",
    "building.livestock_yard",   "building.market_square",    "building.stable",
    "building.water_point",      "building.watermill",        "building.well",
    "building.windmill"};

// Inns, taverns, the guildhall's kitchen and bar, the bath, and the stargazers.
constexpr std::string_view kLateBuildings[] = {"building.bathhouse", "building.guildhall", "building.inn",
                                               "building.observatory", "building.tavern"};

// Gates, walls, garrisons, cells and the sick: kept round the clock.
constexpr std::string_view kWatchBuildings[] = {
    "building.barbican",   "building.barracks",        "building.castle_barracks", "building.corner_tower",
    "building.gatehouse",  "building.guardhouse",      "building.holding_cells",   "building.infirmary",
    "building.isolation_house", "building.keep",       "building.keep_tower",      "building.postern",
    "building.tower"};

// Each list is sorted, so a post's hours are a binary search: a whole roster is asked at once.
static_assert(std::is_sorted(std::begin(kEarlyBuildings), std::end(kEarlyBuildings)));
static_assert(std::is_sorted(std::begin(kLateBuildings), std::end(kLateBuildings)));
static_assert(std::is_sorted(std::begin(kWatchBuildings), std::end(kWatchBuildings)));

template <std::size_t N>
bool among(const std::string_view (&list)[N], std::string_view id) {
    return std::binary_search(std::begin(list), std::end(list), id);
}

// A stay the plan puts a person in: doing what, where along the way, and when. `to` is the
// moment it was to end, which no hold moves; no two stays share one.
struct Stay {
    Doing doing = Doing::at_home;
    double along = 0.0;
    double from = 0.0;
    double to = 0.0;
};

// `hour` hours after the sunrise that begins calendar day `day`.
double moment(long long day, double hour) { return static_cast<double>(day) + hour / kHoursPerDay; }

// The walk, cut to the shorter margin around work so that it never eats into sleep.
double walk_of(const Person& p) {
    const double margin = std::min(p.hours.work_from - p.hours.wake, p.hours.sleep - p.hours.work_until) / kHoursPerDay;
    return std::clamp(p.walk, 0.0, margin);
}

// Where someone is when at home: 0, or 1 for someone housed nowhere, who lives at their post.
double home_of(double walk) { return walk > 0.0 ? 0.0 : 1.0; }

// Calendar day `d`'s stays, from its wake to the next day's: at home, working, at home, asleep.
// One the walk leaves empty -- a morning at home -- is left out.
struct Cycle {
    std::array<Stay, 4> stays{};
    std::size_t count = 0;
};
Cycle cycle_of(const Person& p, long long d) {
    const Hours& h = p.hours;
    const double walk = walk_of(p);
    const double home = home_of(walk);
    const double wake = moment(d, h.wake);
    const double start = moment(d, h.work_from);
    const double finish = moment(d, h.work_until);
    const double sleep = moment(d, h.sleep);
    Cycle c;
    for (const Stay& s : {Stay{Doing::at_home, home, wake, std::max(wake, start - walk)},
                          Stay{Doing::working, 1.0, start, finish},
                          Stay{Doing::at_home, home, std::min(sleep, finish + walk), sleep},
                          Stay{Doing::asleep, home, sleep, moment(d + 1, h.wake)}}) {
        if (s.from < s.to) c.stays[c.count++] = s;
    }
    return c;
}

// The planned stays from the wake of calendar day `first` to the wake after day `last`.
std::vector<Stay> stays_of(const Person& p, long long first, long long last) {
    std::vector<Stay> out;
    for (long long d = first; d <= last; ++d) {
        const Cycle c = cycle_of(p, d);
        out.insert(out.end(), c.stays.begin(), c.stays.begin() + static_cast<std::ptrdiff_t>(c.count));
    }
    return out;
}

// The plan's stint under way at `day`, worked out from the one day's cycle it falls in: the
// stint plan_of(stays_of(...)) holds there, to the bit, without building either.
Stint planned_at(const Person& p, double day) {
    // The cycle begun last: day `d`'s begins at its wake, which may fall the day before or after.
    long long d = static_cast<long long>(std::floor(day)) + 1;
    while (moment(d, p.hours.wake) > day) --d;
    const Cycle c = cycle_of(p, d);
    // Before the first stay, the walk from the night before: asleep at home.
    double from = moment(d, p.hours.wake);
    double along = home_of(walk_of(p));
    for (std::size_t i = 0; i < c.count; ++i) {
        const Stay& s = c.stays[i];
        if (day < s.from) return {Doing::walking, from, s.from, along, s.along};
        if (day < s.to) return {s.doing, s.from, s.to, s.along, s.along};
        from = s.to;
        along = s.along;
    }
    throw std::logic_error("schedule: a day's cycle does not reach its end");
}

// The plan as stints: each stay, and a walk wherever one stay ends before the next begins.
std::vector<Stint> plan_of(const std::vector<Stay>& stays) {
    std::vector<Stint> out;
    out.reserve(stays.size() * 2);
    for (std::size_t i = 0; i < stays.size(); ++i) {
        if (i > 0 && stays[i - 1].to < stays[i].from) {
            out.push_back({Doing::walking, stays[i - 1].to, stays[i].from, stays[i - 1].along, stays[i].along});
        }
        out.push_back({stays[i].doing, stays[i].from, stays[i].to, stays[i].along, stays[i].along});
    }
    return out;
}

// `s` from `day` on, or up to it.
Stint tail_of(const Stint& s, double day) {
    Stint out = s;
    out.from_day = day;
    out.from_along = along_at(s, day);
    return out;
}
Stint head_of(const Stint& s, double day) {
    Stint out = s;
    out.to_day = day;
    out.to_along = along_at(s, day);
    return out;
}

// `stints` with `d` in place of what it covers.
void splice(std::vector<Stint>& stints, const Detour& d) {
    std::vector<Stint> out;
    out.reserve(stints.size() + d.stints.size());
    bool placed = false;
    const auto place = [&] {
        if (placed) return;
        out.insert(out.end(), d.stints.begin(), d.stints.end());
        placed = true;
    };
    for (const Stint& s : stints) {
        if (s.to_day <= d.from_day) {
            out.push_back(s);
            continue;
        }
        if (s.from_day < d.from_day) out.push_back(head_of(s, d.from_day));
        place();
        if (s.to_day > d.to_day) out.push_back(s.from_day >= d.to_day ? s : tail_of(s, d.to_day));
    }
    place();
    stints = std::move(out);
}

// The one stint under way at `day`, or nullptr.
const Stint* under_way(const std::vector<Stint>& stints, double day) {
    for (const Stint& s : stints) {
        if (s.from_day <= day && day < s.to_day) return &s;
    }
    return nullptr;
}

}  // namespace

Hours hours_of(std::string_view building_id, int ordinal) {
    if (among(kWatchBuildings, building_id)) return kWatches[static_cast<std::size_t>((ordinal % 3 + 3) % 3)];
    if (among(kEarlyBuildings, building_id)) return kEarlyTrade;
    if (among(kLateBuildings, building_id)) return kLateTrade;
    return kDayTrade;
}

double walk_days(int commute_m) {
    const double minutes = static_cast<double>(std::max(commute_m, 0)) / kPaceMetresPerSecond / 60.0;
    return std::max(kShortestWalkMinutes, minutes) / kMinutesPerDay;
}

double along_at(const Stint& stint, double day) {
    if (!(stint.to_day > stint.from_day)) return stint.from_along;
    const double t = std::clamp((day - stint.from_day) / (stint.to_day - stint.from_day), 0.0, 1.0);
    return stint.from_along + (stint.to_along - stint.from_along) * t;
}

void hold(const Person& person, std::vector<Detour>& detours, double from, double to) {
    if (!(to > from)) throw std::invalid_argument("schedule::hold: a hold must end after it begins");
    if (!detours.empty() && from < detours.back().from_day)
        throw std::invalid_argument("schedule::hold: a hold cannot begin before the last one did");
    const std::vector<Stay> stays = stays_of(person, static_cast<long long>(std::floor(from)) - 2,
                                             static_cast<long long>(std::floor(to)) + 3);

    // What is under way when the hold begins, and the stay the person is in or making for: the
    // last detour's, if it still runs, else the plan's.
    std::vector<Stint> day = plan_of(stays);
    double making_for = 0.0;  // that stay's end, which names it
    const bool bent = !detours.empty() && detours.back().to_day > from;
    if (bent) {
        splice(day, detours.back());
        making_for = detours.back().to_day;
    }
    const Stint* now = under_way(day, from);
    if (now == nullptr) throw std::logic_error("schedule::hold: the plan does not reach the hold");
    if (!bent) {
        const auto stay = std::find_if(stays.begin(), stays.end(), [&](const Stay& s) { return s.to > from; });
        making_for = stay->to;
    }
    const double at = along_at(*now, from);
    if (now->doing == Doing::held) to = std::max(to, now->to_day);  // still held from before

    // After the hold, on to the first stay still reachable before its end, from where they stand.
    Detour made{from, from, {{Doing::held, from, to, at, at}}};
    const double walk = walk_of(person);
    const auto first = std::find_if(stays.begin(), stays.end(), [&](const Stay& s) { return s.to >= making_for; });
    for (auto s = first; s != stays.end(); ++s) {
        const double arrive = to + std::abs(s->along - at) * walk;
        if (!(arrive < s->to)) continue;
        if (arrive > to) made.stints.push_back({Doing::walking, to, arrive, at, s->along});
        made.stints.push_back({s->doing, arrive, s->to, s->along, s->along});
        made.to_day = s->to;
        break;
    }
    if (!(made.to_day > from)) throw std::logic_error("schedule::hold: no stay reachable after the hold");

    // The last detour ends where this one begins.
    if (bent) {
        Detour& last = detours.back();
        std::vector<Stint> kept;
        for (const Stint& s : last.stints) {
            if (s.from_day >= from) break;
            kept.push_back(s.to_day > from ? head_of(s, from) : s);
        }
        last.stints = std::move(kept);
        last.to_day = from;
        if (last.stints.empty()) detours.pop_back();
    }
    detours.push_back(std::move(made));
}

Stint stint_at(const Person& person, const std::vector<Detour>& detours, double day) {
    // A detour under way has it. Otherwise the plan's, cut where the next detour begins, as
    // splicing that detour in cuts it.
    double next = std::numeric_limits<double>::infinity();
    for (const Detour& d : detours) {
        if (d.from_day <= day && day < d.to_day) {
            for (const Stint& s : d.stints) {
                if (s.from_day <= day && day < s.to_day) return s;
            }
        }
        if (d.from_day > day) next = std::min(next, d.from_day);
    }
    const Stint s = planned_at(person, day);
    return next < s.to_day ? head_of(s, next) : s;
}

std::vector<Stint> stints(const Person& person, const std::vector<Detour>& detours, double from, double to) {
    const double end = std::max(from, to);
    // A detour runs at most a day past its hold, and a hold is at most a day: three days either
    // side of the span covers every stint and detour that can meet it.
    constexpr double kMargin = 3.0;
    std::vector<Stint> all = plan_of(stays_of(person, static_cast<long long>(std::floor(from - kMargin)),
                                              static_cast<long long>(std::floor(end + kMargin))));
    for (const Detour& d : detours) {
        if (d.to_day > from - kMargin && d.from_day < end + kMargin) splice(all, d);
    }
    std::vector<Stint> out;
    for (const Stint& s : all) {
        const bool meets = to > from ? (s.to_day > from && s.from_day < to) : (s.from_day <= from && from < s.to_day);
        if (meets) out.push_back(s);
    }
    return out;
}

}  // namespace orchestrator::schedule
