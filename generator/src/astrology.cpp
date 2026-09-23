#include "fwg/astrology.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <stdexcept>

#include "fwg/magic.hpp"
#include "fwg/random.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kDeg = kPi / 180.0;

// Tide shaping. A school at rest sits slightly below one, so an unlit or hidden
// quarter damps its school rather than leaving it untouched.
constexpr double kTideFloor = 0.7;
constexpr double kTideGain = 1.1;
constexpr double kTideMin = 0.5;
constexpr double kTideMax = 1.8;

constexpr int kSynodicLow = kSynodicDaysRange[0], kSynodicHigh = kSynodicDaysRange[1];
constexpr int kSpinLow = kSpinDaysRange[0], kSpinHigh = kSpinDaysRange[1];
constexpr int kNodLow = kNodDaysRange[0], kNodHigh = kNodDaysRange[1];
constexpr double kTiltLow = kTiltDegreesRange[0], kTiltHigh = kTiltDegreesRange[1];
constexpr int kRedraws = 8;

// Which hemisphere a school sits on, and which quarter of it. Indexed by school table
// position: weave, umbral, infernal, radiant, fire, water, earth, air.
//
// Still carries the ordered schools (radiant, water, earth, umbral); Restless the
// chaotic ones (weave, fire, air, infernal). The quarter is the position within that
// hemisphere's list.
struct Quarter {
    double latitude_deg;
    int quarter;
};
constexpr Quarter kQuarters[kKnownSchools] = {
    {-45.0, 0},  // weave    - restless, quarter 0
    {45.0, 3},   // umbral   - still,    quarter 3
    {-45.0, 3},  // infernal - restless, quarter 3
    {45.0, 0},   // radiant  - still,    quarter 0
    {-45.0, 1},  // fire     - restless, quarter 1
    {45.0, 1},   // water    - still,    quarter 1
    {45.0, 2},   // earth    - still,    quarter 2
    {-45.0, 2},  // air      - restless, quarter 2
};

// Rotate a moon-frame vector by the spin about y, then lean it by the nod about z.
Vec3 facing(const Vec3& v, double spin, double nod_degrees) {
    double x = v.x;
    double y = v.y;
    double z = v.z;
    const double cs = std::cos(spin);
    const double ss = std::sin(spin);
    const double x1 = x * cs + z * ss;
    const double z1 = -x * ss + z * cs;
    x = x1;
    z = z1;
    // A positive nod turns the Still (y > 0) hemisphere toward the world (+x).
    const double cn = std::cos(nod_degrees * kDeg);
    const double sn = std::sin(nod_degrees * kDeg);
    const double x2 = x * cn + y * sn;
    const double y2 = -x * sn + y * cn;
    return {x2, y2, z};
}

int period_of(const Moon& moon, int which) {
    return which == 0 ? moon.synodic_days : (which == 1 ? moon.spin_days : moon.nod_days);
}

int offset_of(const Moon& moon, int which) {
    return which == 0 ? moon.synodic_offset : (which == 1 ? moon.spin_offset : moon.nod_offset);
}

long long floor_mod(long long value, long long modulus) {
    const long long r = value % modulus;
    return r < 0 ? r + modulus : r;
}

}  // namespace

Vec3 school_region(int school_index) {
    if (school_index < 0 || school_index >= kKnownSchools) return {0.0, 0.0, 0.0};
    const Quarter& q = kQuarters[school_index];
    const double latitude = q.latitude_deg * kDeg;
    const double longitude = q.quarter * 90.0 * kDeg;
    return {std::cos(latitude) * std::cos(longitude), std::sin(latitude),
            std::cos(latitude) * std::sin(longitude)};
}

Moon seed_moon(std::uint32_t seed, std::int64_t variation, const MoonOverrides& overrides) {
    Rng rng(child_seed64(seed, "astrology-v1", variation));

    int synodic = static_cast<int>(rng.randint(kSynodicLow, kSynodicHigh));
    int spin = static_cast<int>(rng.randint(kSpinLow, kSpinHigh));
    // Coprime is PREFERRED, not required: after eight tries the draw stands. A common
    // factor only shortens the great year, it does not break anything.
    for (int i = 0; i < kRedraws; ++i) {
        if (std::gcd(spin, synodic) == 1) break;
        spin = static_cast<int>(rng.randint(kSpinLow, kSpinHigh));
    }
    int nod = static_cast<int>(rng.randint(kNodLow, kNodHigh));
    for (int i = 0; i < kRedraws; ++i) {
        if (std::gcd(nod, synodic) == 1 && std::gcd(nod, spin) == 1) break;
        nod = static_cast<int>(rng.randint(kNodLow, kNodHigh));
    }

    Moon moon;
    moon.synodic_days = synodic;
    moon.spin_days = spin;
    moon.nod_days = nod;
    // Offsets in the order synodic, spin, nod. The order is part of the contract.
    moon.synodic_offset = static_cast<int>(rng.randint(0, synodic - 1));
    moon.spin_offset = static_cast<int>(rng.randint(0, spin - 1));
    moon.nod_offset = static_cast<int>(rng.randint(0, nod - 1));
    moon.tilt_max_degrees = rng.uniform(kTiltLow, kTiltHigh);
    moon.variation = variation;

    // Overrides are applied AFTER every draw, so pinning one cycle never moves another.
    // Zero means "keep the seeded value" rather than "set to zero".
    const auto pin = [](const char* name, int value, int low, int high, int& target, int& offset) {
        if (value == 0) return;
        if (value < low || value > high) {
            throw std::invalid_argument(std::string("moon_") + name + "_days must be 0 or " +
                                        std::to_string(low) + ".." + std::to_string(high));
        }
        target = value;
        offset %= value;  // the seeded offset is kept, folded into the new period
    };
    pin("synodic", overrides.synodic_days, kSynodicLow, kSynodicHigh, moon.synodic_days, moon.synodic_offset);
    pin("spin", overrides.spin_days, kSpinLow, kSpinHigh, moon.spin_days, moon.spin_offset);
    pin("nod", overrides.nod_days, kNodLow, kNodHigh, moon.nod_days, moon.nod_offset);
    if (overrides.tilt_degrees != 0.0) {
        if (overrides.tilt_degrees < kTiltLow || overrides.tilt_degrees > kTiltHigh) {
            throw std::invalid_argument("moon_tilt_degrees must be 0 or 10..35");
        }
        moon.tilt_max_degrees = overrides.tilt_degrees;
    }
    moon.overrides = overrides;

    moon.great_year_days = std::lcm(static_cast<long long>(moon.synodic_days),
                                    std::lcm(static_cast<long long>(moon.spin_days),
                                             static_cast<long long>(moon.nod_days)));
    return moon;
}

double cycle_fraction(const Moon& moon, int which, long long t) {
    const long long period = period_of(moon, which);
    // Integer days stay in integer arithmetic right up to the division, so the fraction
    // is exact for every representable day.
    return static_cast<double>(floor_mod(t + offset_of(moon, which), period)) / static_cast<double>(period);
}

MoonState moon_state(const Moon& moon, long long t) {
    MoonState state;
    state.phase_angle = 2.0 * kPi * cycle_fraction(moon, 0, t);
    state.spin_angle = 2.0 * kPi * cycle_fraction(moon, 1, t);
    state.nod_degrees = moon.tilt_max_degrees * std::sin(2.0 * kPi * cycle_fraction(moon, 2, t));
    return state;
}

std::array<double, kKnownSchools> tide(const Moon& moon, long long t) {
    const MoonState state = moon_state(moon, t);
    // The sun sits in the moon's equatorial plane at the phase angle: at full moon it
    // is behind the world, at new moon behind the moon.
    const double sun_x = std::cos(state.phase_angle);
    const double sun_z = std::sin(state.phase_angle);

    std::array<double, kKnownSchools> factors{};
    for (int s = 0; s < kKnownSchools; ++s) {
        const Vec3 f = facing(school_region(s), state.spin_angle, state.nod_degrees);
        const double visible = std::max(0.0, f.x);            // turned toward the world
        const double lit = std::max(0.0, f.x * sun_x + f.z * sun_z);  // and in sunlight
        factors[static_cast<std::size_t>(s)] =
            std::min(kTideMax, std::max(kTideMin, kTideFloor + kTideGain * visible * lit));
    }
    return factors;
}

DayState day_state(const Moon& moon, long long t) {
    const MoonState state = moon_state(moon, t);
    const double fraction = cycle_fraction(moon, 0, t);

    DayState day;
    day.day = t;
    day.phase_fraction = fraction;
    day.elongation_degrees = 360.0 * fraction;
    day.illumination = (1.0 + std::cos(state.phase_angle)) / 2.0;
    // Hours after sunrise: a full moon rises at sunset.
    day.moonrise_hour = kHoursPerDay * fraction;
    day.spin_fraction = cycle_fraction(moon, 1, t);
    day.nod_degrees = state.nod_degrees;
    day.leaning = state.nod_degrees >= 0.0 ? "still" : "restless";
    day.tide = tide(moon, t);
    return day;
}

Almanac almanac(const Moon& moon, int year) {
    const long long start = static_cast<long long>(year) * kDaysPerYear;

    Almanac out;
    out.reported_year = year;
    out.now = day_state(moon, start);

    for (int m = 0; m < kMonthsPerYear; ++m) {
        const long long first = start + static_cast<long long>(m) * kDaysPerMonth;
        AlmanacMonth month;
        month.first_day = first;
        month.state = day_state(moon, first);
        std::array<double, kKnownSchools> totals{};
        for (int d = 0; d < kDaysPerMonth; ++d) {
            const auto factors = tide(moon, first + d);
            for (int s = 0; s < kKnownSchools; ++s) totals[static_cast<std::size_t>(s)] += factors[static_cast<std::size_t>(s)];
        }
        for (int s = 0; s < kKnownSchools; ++s) {
            month.mean_tide[static_cast<std::size_t>(s)] = totals[static_cast<std::size_t>(s)] / kDaysPerMonth;
        }
        out.months.push_back(std::move(month));
    }

    const auto table = magic_school_table();
    for (int d = 0; d < kDaysPerYear; ++d) {
        const long long day = start + d;
        const long long synodic = floor_mod(day + moon.synodic_offset, moon.synodic_days);
        if (synodic == 0) out.events.push_back({d, "full_moon", "", 0.0});
        else if (synodic == moon.synodic_days / 2) out.events.push_back({d, "new_moon", "", 0.0});

        const long long nod = floor_mod(day + moon.nod_offset, moon.nod_days);
        if (nod == moon.nod_days / 4) out.events.push_back({d, "still_ascendant", "", 0.0});
        else if (nod == (3 * moon.nod_days) / 4) out.events.push_back({d, "restless_ascendant", "", 0.0});

        const auto factors = tide(moon, day);
        for (int s = 0; s < kKnownSchools; ++s) {
            if (factors[static_cast<std::size_t>(s)] >= kSurge) {
                out.events.push_back({d, "surge", std::string(table[static_cast<std::size_t>(s)].name),
                                      factors[static_cast<std::size_t>(s)]});
            }
        }
        // A hollow night needs EVERY school low, not just most of them.
        const bool hollow = std::all_of(factors.begin(), factors.end(),
                                        [](double v) { return v <= kHollow; });
        if (hollow) out.events.push_back({d, "hollow_night", "", 0.0});
    }
    std::stable_sort(out.events.begin(), out.events.end(), [](const AlmanacEvent& a, const AlmanacEvent& b) {
        if (a.day != b.day) return a.day < b.day;
        if (a.kind != b.kind) return a.kind < b.kind;
        return a.school < b.school;
    });

    // The next day all three cycles restart together. Inside one great year there is at
    // most one, and there may be none if the offsets never coincide.
    for (long long day = start; day < start + moon.great_year_days; ++day) {
        if (floor_mod(day + moon.synodic_offset, moon.synodic_days) == 0 &&
            floor_mod(day + moon.spin_offset, moon.spin_days) == 0 &&
            floor_mod(day + moon.nod_offset, moon.nod_days) == 0) {
            out.next_grand_alignment_day = day;
            break;
        }
    }
    return out;
}

Grid lunar_sensitivity(const World& world) {
    const int n = world.config.size;
    Grid out(n);

    const auto table = magic_school_table();
    std::vector<const Grid*> instability;
    for (int s = 0; s < kKnownSchools; ++s) {
        const auto it = world.layers.find("instability_" + std::string(table[static_cast<std::size_t>(s)].name));
        if (it != world.layers.end()) instability.push_back(&it->second);
    }
    const auto water_it = world.layers.find("water_type");
    const auto exposure_it = world.layers.find("coastal_exposure");

    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            double total = 0.0;
            for (const Grid* field : instability) total += field->at(x, z);
            // A lake is all shore; open coast takes its exposure. Both sway more with
            // the moon than dry interior does.
            const double water = water_it != world.layers.end() ? water_it->second.at(x, z) : 0.0;
            const double shore = water == 2.0
                                     ? 1.0
                                     : (exposure_it != world.layers.end() ? exposure_it->second.at(x, z) : 0.0);
            out.at(x, z) = std::min(1.0, std::max(0.0, 0.2 + 0.5 * total + 0.3 * shore));
        }
    }
    return out;
}

void add_astrology(World& world) {
    const WorldOptions& o = world.effective.options;
    MoonOverrides overrides;
    overrides.synodic_days = o.as_int(Option::moon_synodic_days);
    overrides.spin_days = o.as_int(Option::moon_spin_days);
    overrides.nod_days = o.as_int(Option::moon_nod_days);
    overrides.tilt_degrees = o[Option::moon_tilt_degrees];

    world.moon = seed_moon(world.config.seed, static_cast<std::int64_t>(o[Option::moon_variation]), overrides);
    world.has_astrology = true;
    world.systems.push_back("astrology");
    world.warnings.emplace_back(kAstrologyLimits);
    refresh_astrology(world);
}

void refresh_astrology(World& world) {
    // The reported year follows the world's own history once settlements exist -- the
    // founding's end year, which every settlement rebuild rewrites -- and until then it is
    // the epoch: the reference's reported_year. Never reseeds: an age advance moves the
    // year, it does not give the world a new moon.
    const bool founded = world.has_settlements;
    const int year = founded ? static_cast<int>(world.settlements.founding.end_year) : 0;
    world.lunar_almanac = almanac(world.moon, year);
    world.lunar_almanac.reported_year_source = founded ? "founding.end_year" : "epoch";
    world.layers["lunar_sensitivity"] = lunar_sensitivity(world);

    if (!world.has_magic) return;
    // The reference's refresh rewrites magic.lunar_surge as the reported year's first day,
    // over whatever day the time API last surged at. Here that record is the almanac's own
    // `now`, which the document writes wherever no later surge stands, so the refresh drops
    // the time API's: an age rebuild reads the rebuilt year's sky, not the last tick's.
    world.lunar_surge.reset();
    const auto table = magic_school_table();
    const auto& factors = world.lunar_almanac.now.tide;
    for (int s = 0; s < kKnownSchools; ++s) {
        LeyNetwork& net = world.magic.networks.at(std::string(table[static_cast<std::size_t>(s)].name));
        net.surged_strength = net.strength * factors[static_cast<std::size_t>(s)];
        net.has_surge = true;
    }
    // Hidden schools take no surge and carry no surged strength: alien magic does not
    // answer to this world's sky.
}

}  // namespace fwg
