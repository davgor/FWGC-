// One moon, two hemispheres, eight schools: seeded cycles, tide, almanac.
//
// The moon is NOT tidally locked. Its Still hemisphere carries the ordered schools and
// its Restless hemisphere the chaotic ones, one school per quarter. Three integer-day
// cycles — phase, spin and nod — are drawn with a preference for coprime periods, so
// the beat they make together is far longer than any one of them: fully replayable,
// and long enough to read as chaos.
//
// A school surges when its quarter both faces the world AND is lit.
//
// Numeric contract, kept deliberately narrow so any implementation can reproduce it:
// only sin, cos, products, sums, min and max, plus integer modulo on integer days.
// No pow. Every quantity is a ratio of a cycle or a unit vector, never a metre, so
// world scale cannot move it.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/grid.hpp"
#include "fwg/vec3.hpp"

namespace fwg {

inline constexpr int kHoursPerDay = 24;
inline constexpr int kDaysPerMonth = 30;
inline constexpr int kMonthsPerYear = 12;
inline constexpr int kDaysPerYear = kDaysPerMonth * kMonthsPerYear;

// The eight known schools, in school-table order. Hidden schools have no quarter: alien
// magic does not answer to this world's sky.
inline constexpr int kKnownSchools = 8;

// A school surges at or above this tide, and a night is hollow when every school is at
// or below the other.
inline constexpr double kSurge = 1.6;
inline constexpr double kHollow = 0.75;

struct MoonOverrides {
    int synodic_days = 0;  // 0 keeps the seeded value
    int spin_days = 0;
    int nod_days = 0;
    double tilt_degrees = 0.0;
};

struct Moon {
    int synodic_days = 0;  // phase: full to full
    int spin_days = 0;     // which meridian faces the world
    int nod_days = 0;      // which hemisphere leans in
    int synodic_offset = 0;
    int spin_offset = 0;
    int nod_offset = 0;
    double tilt_max_degrees = 0.0;
    long long great_year_days = 0;  // lcm of the three periods
    std::int64_t variation = 0;
    // What the request pinned, zero where the seed chose: the document's `moon.controls`.
    MoonOverrides overrides;
};

// The seeded ranges, inclusive, and the two hemispheres' schools in quarter order: the
// constants the document's `moon` block publishes beside the draws.
inline constexpr int kSynodicDaysRange[2] = {26, 34};
inline constexpr int kSpinDaysRange[2] = {15, 63};
inline constexpr int kNodDaysRange[2] = {17, 37};
inline constexpr double kTiltDegreesRange[2] = {10.0, 35.0};
inline constexpr std::string_view kStillHemisphere[4] = {"radiant", "water", "earth", "umbral"};
inline constexpr std::string_view kRestlessHemisphere[4] = {"weave", "fire", "air", "infernal"};

// The closed forms the almanac is computed from, as the reference's FORMULAS publishes them
// (key, sentence), so a consumer can drive its own clock bit-consistently with the almanac.
struct AstrologyFormula {
    std::string_view key;
    std::string_view text;
};
inline constexpr AstrologyFormula kAstrologyFormulas[] = {
    {"time", "t is real days since founding year 0; hour 0 is sunrise; integer days use integer modulo"},
    {"fraction", "f_k(t) = ((t + offsets[k]) mod periods[k]) / periods[k] for k in synodic, spin, nod"},
    {"phase_angle", "alpha = 2*pi*f_synodic; 0 is full, pi is new"},
    {"spin_angle", "beta = 2*pi*f_spin, rotation of the moon frame about its polar axis y"},
    {"nod_degrees",
     "gamma = tilt_max_degrees * sin(2*pi*f_nod); positive leans the still hemisphere toward the world"},
    {"region",
     "x,z = x*cos(beta)+z*sin(beta), -x*sin(beta)+z*cos(beta); then x,y = x*cos(gamma)+y*sin(gamma), "
     "-x*sin(gamma)+y*cos(gamma)"},
    {"tide",
     "tide_s = clamp(.7 + 1.1 * max(0,x) * max(0, x*cos(alpha)+z*sin(alpha)), .5, 1.8) using that school region"},
    {"illumination", "(1 + cos(alpha)) / 2"},
    {"moonrise_hour", "24 * f_synodic hours after sunrise (full moon rises at sunset)"},
    {"elongation_degrees", "360 * f_synodic"},
    {"leyline_at_time", "ley_s(t) = ley_s * (1 + lunar_sensitivity * (tide_s(t) - 1))"},
};

// The astrology block's `limits`, which the astrology pass also appends to the warnings.
inline constexpr std::string_view kAstrologyLimits =
    "The moon is artistic astronomy: one body reduced to phase, rise hour, spin and lean. No altitude, no "
    "latitude dependence, no solar eclipses, no water tides. Surges are momentary multipliers; the exported "
    "leyline grids are the base field.";

// Draw order is part of the contract: an override pins a value without moving any
// other cycle, because the draws still happen in the same sequence.
Moon seed_moon(std::uint32_t seed, std::int64_t variation = 0, const MoonOverrides& overrides = {});

// Fraction of a cycle at day t. Integer days stay in integer arithmetic.
double cycle_fraction(const Moon& moon, int which, long long t);  // 0 synodic, 1 spin, 2 nod

struct MoonState {
    double phase_angle = 0.0;  // 0 is full, pi is new
    double spin_angle = 0.0;
    double nod_degrees = 0.0;  // positive leans the Still hemisphere toward the world
};
MoonState moon_state(const Moon& moon, long long t);

// Per-school strength multiplier: visible fraction times lit fraction of its quarter.
std::array<double, kKnownSchools> tide(const Moon& moon, long long t);

struct DayState {
    long long day = 0;
    double phase_fraction = 0.0;
    double elongation_degrees = 0.0;
    double illumination = 0.0;
    double moonrise_hour = 0.0;
    double spin_fraction = 0.0;
    double nod_degrees = 0.0;
    std::string leaning;  // still | restless
    std::array<double, kKnownSchools> tide{};
};
DayState day_state(const Moon& moon, long long t);

struct AlmanacEvent {
    int day = 0;  // day of the reported year
    std::string kind;
    std::string school;  // only for a surge
    double tide = 0.0;
};

struct AlmanacMonth {
    long long first_day = 0;
    DayState state;
    std::array<double, kKnownSchools> mean_tide{};
};

struct Almanac {
    int reported_year = 0;
    std::string reported_year_source;
    DayState now;
    std::vector<AlmanacMonth> months;
    std::vector<AlmanacEvent> events;
    long long next_grand_alignment_day = -1;  // -1 when none falls inside the great year
};

Almanac almanac(const Moon& moon, int year);

// Unit vector of a school's quarter in the moon frame; y is the polar axis. Index is
// the school table position, and only the known eight have one.
Vec3 school_region(int school_index);

struct World;

// Stage 9: seed the moon once per world, then publish the first almanac.
void add_astrology(World& world);

// Recompute everything that depends on the year or on the ley fields. Never reseeds —
// an age advance moves the reported year, it does not give the world a new moon.
void refresh_astrology(World& world);

// How far a cell's magic sways with the moon, from fields that are already ratios.
Grid lunar_sensitivity(const World& world);

}  // namespace fwg
