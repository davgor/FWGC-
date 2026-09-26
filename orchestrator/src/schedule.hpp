// A roster post's day: when they sleep, walk to work, work and walk home, and what a hold does
// to it. Pure: the same person, holds and moment give the same stints, and nothing here reads a
// clock or a world.
//
// A day is its stays -- at home, working, at home, asleep -- each with the moment it was to end,
// and the walks between. A hold keeps the person where they stand. After it they make for the
// stay they were in or making for; one they cannot reach before its end is skipped for the next,
// from where they stand, and the stay they reach ends when it was to. So a hold delays only what
// comes before the end of the stay reached, and the day runs as planned from there.
#pragma once

#include <string_view>
#include <vector>

#include "orchestrator/director.hpp"

namespace orchestrator::schedule {

// When a post's day runs, in hours after sunrise -- the world's hour 0 -- on a 24-hour day:
// wake, work from, work until, sleep, in that order, and the sleep ends at the next day's wake.
struct Hours {
    double wake = 0.0;
    double work_from = 0.0;
    double work_until = 0.0;
    double sleep = 0.0;
};

// The trades' hours. Tunable; nothing depends on the exact numbers. With sunrise at six: a day
// trade works seven to five, an early trade (bread, fields, mills, water, animals) half past
// four to half past two, a late trade (inns, taverns, the bath) eleven to ten, and a watch
// (guards, garrisons, infirmaries) keeps three shifts round the clock.
inline constexpr Hours kDayTrade{-1.0, 1.0, 11.0, 15.5};
inline constexpr Hours kEarlyTrade{-2.5, -1.5, 8.5, 14.0};
inline constexpr Hours kLateTrade{2.0, 5.0, 16.0, 17.5};
inline constexpr Hours kWatches[3] = {{-1.5, 0.0, 8.0, 15.5}, {1.5, 8.0, 16.0, 17.5}, {9.0, 16.0, 24.0, 25.5}};

// The hours a post keeps, by the building it works in: a watch by its ordinal, the three shifts
// in turn, so each building's watch covers the day.
Hours hours_of(std::string_view building_id, int ordinal);

// How fast a person covers the straight metres between their home and their work. Streets wind,
// so this is slower than a walking pace: about a metre of straight line a second.
inline constexpr double kPaceMetresPerSecond = 1.0;
inline constexpr double kShortestWalkMinutes = 1.0;

// How long the walk between a home and a workplace `commute_m` straight metres apart takes, in
// days: never less than a minute.
double walk_days(int commute_m);

// A person's day as the schedule sees it. A walk longer than the margins around work is cut to
// them, so it never eats into sleep.
struct Person {
    Hours hours;
    double walk = 0.0;  // days, home to work; 0 for someone who lives at their post
};

// The person held where they stand over [from, to), world days: the detour it makes of their day
// as `detours` (in order, none overlapping) have already bent it, added to them. A hold begun
// while an earlier one still runs holds them until the later of the two ends, and the earlier
// detour is cut where the new one begins.
void hold(const Person& person, std::vector<Detour>& detours, double from, double to);

// The stints that meet [from, to): the plan, with each detour in place of what it covers. With
// `to` no later than `from`, the one stint under way at `from`.
std::vector<Stint> stints(const Person& person, const std::vector<Detour>& detours, double from, double to);

// The one stint under way at `day`: what stints(person, detours, day, day) gives, to the bit,
// worked out from the one day it falls in and allocating nothing, for asking after a whole
// roster at once.
Stint stint_at(const Person& person, const std::vector<Detour>& detours, double day);

// Where along the way (0 home, 1 work) a stint has the person at `day`.
double along_at(const Stint& stint, double day);

}  // namespace orchestrator::schedule
