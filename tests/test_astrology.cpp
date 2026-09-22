// Moon, tide and almanac invariants.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <string>

#include "fwg/astrology.hpp"
#include "fwg/magic.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

World build(int size = 33) {
    Request r;
    r.seed = 42;
    r.size = size;
    r.phase = 6;
    return generate(resolve(r));
}

}  // namespace

// --- seeding --------------------------------------------------------------

TEST(the_moon_is_seeded_inside_its_declared_ranges) {
    for (std::uint32_t seed = 1; seed <= 40; ++seed) {
        const Moon moon = seed_moon(seed);
        check::between(moon.synodic_days, 26, 34, "synodic period");
        check::between(moon.spin_days, 15, 63, "spin period");
        check::between(moon.nod_days, 17, 37, "nod period");
        check::between(moon.tilt_max_degrees, 10.0, 35.0, "tilt");
        check::between(moon.synodic_offset, 0, moon.synodic_days - 1, "synodic offset");
        check::between(moon.spin_offset, 0, moon.spin_days - 1, "spin offset");
        check::between(moon.nod_offset, 0, moon.nod_days - 1, "nod offset");

        // The great year is exactly the beat of the three cycles.
        const long long expected = std::lcm(static_cast<long long>(moon.synodic_days),
                                            std::lcm(static_cast<long long>(moon.spin_days),
                                                     static_cast<long long>(moon.nod_days)));
        check::equal(moon.great_year_days, expected, "great year is the lcm");
    }
    // And it replays.
    const Moon a = seed_moon(7);
    const Moon b = seed_moon(7);
    check::equal(a.synodic_days, b.synodic_days, "the moon to replay from its seed");
    check::equal(a.tilt_max_degrees, b.tilt_max_degrees, "including its tilt");
    check::is_true(seed_moon(7, 1).great_year_days != 0, "a variation still produces a moon");
}

TEST(coprime_periods_are_preferred_not_required) {
    // Eight redraws, then the draw stands. A common factor only shortens the great
    // year; it does not break anything, so this asserts a MAJORITY rather than all.
    int coprime = 0;
    const int seeds = 60;
    for (std::uint32_t seed = 1; seed <= static_cast<std::uint32_t>(seeds); ++seed) {
        const Moon m = seed_moon(seed);
        if (std::gcd(m.spin_days, m.synodic_days) == 1 && std::gcd(m.nod_days, m.synodic_days) == 1 &&
            std::gcd(m.nod_days, m.spin_days) == 1) {
            ++coprime;
        }
    }
    check::is_true(coprime > seeds * 3 / 4,
                   "most moons to come out fully coprime (" + std::to_string(coprime) + "/" +
                       std::to_string(seeds) + ")");
}

TEST(an_override_pins_one_cycle_without_moving_the_others) {
    // The draws happen in the same order whether or not an override is supplied, so
    // pinning the spin must leave the phase and the nod exactly where they were.
    const Moon base = seed_moon(42);
    MoonOverrides pin;
    pin.spin_days = 31;
    const Moon pinned = seed_moon(42, 0, pin);

    check::equal(pinned.spin_days, 31, "the pinned cycle");
    check::equal(pinned.synodic_days, base.synodic_days, "the phase to be untouched");
    check::equal(pinned.nod_days, base.nod_days, "the nod to be untouched");
    check::equal(pinned.tilt_max_degrees, base.tilt_max_degrees, "the tilt to be untouched");
    check::is_true(pinned.spin_offset >= 0 && pinned.spin_offset < 31, "the offset folded into the new period");

    // Zero means "keep the seeded value", not "set to zero".
    MoonOverrides none;
    none.spin_days = 0;
    check::equal(seed_moon(42, 0, none).spin_days, base.spin_days, "zero keeps the seeded period");

    MoonOverrides bad;
    bad.spin_days = 9;  // below the published range
    check::throws([&] { seed_moon(42, 0, bad); }, "a period outside its range");
    MoonOverrides bad_tilt;
    bad_tilt.tilt_degrees = 90.0;
    check::throws([&] { seed_moon(42, 0, bad_tilt); }, "a tilt outside its range");
}

// --- geometry and tide ----------------------------------------------------

TEST(the_eight_quarters_are_unit_vectors_split_between_hemispheres) {
    int still = 0;
    int restless = 0;
    for (int s = 0; s < kKnownSchools; ++s) {
        const Vec3 r = school_region(s);
        check::close(std::sqrt(dot(r, r)), 1.0, 1e-12, "a quarter centre on the unit sphere");
        if (r.y > 0.0) ++still;
        else ++restless;
    }
    check::equal(still, 4, "four ordered schools on the still hemisphere");
    check::equal(restless, 4, "four chaotic schools on the restless one");

    // The named split: radiant, water, earth and umbral are Still.
    for (const char* name : {"radiant", "water", "earth", "umbral"}) {
        check::is_true(school_region(magic_school_index(name)).y > 0.0, std::string(name) + " is still");
    }
    for (const char* name : {"weave", "fire", "air", "infernal"}) {
        check::is_true(school_region(magic_school_index(name)).y < 0.0, std::string(name) + " is restless");
    }
}

TEST(the_tide_stays_inside_its_band_for_a_whole_great_year) {
    const Moon moon = seed_moon(42);
    // Sample the whole beat, not just a year: the three cycles only realign over the
    // great year, so a shorter sweep never sees their worst combination.
    const long long step = std::max<long long>(1, moon.great_year_days / 4000);
    double low = 99.0;
    double high = -99.0;
    for (long long t = 0; t < moon.great_year_days; t += step) {
        const auto factors = tide(moon, t);
        for (const double v : factors) {
            check::between(v, 0.5, 1.8, "tide inside its band");
            low = std::min(low, v);
            high = std::max(high, v);
        }
    }
    check::is_true(high > low, "the tide to actually move");
    check::is_true(low <= 0.7 + 1e-12, "an unlit or hidden quarter to fall to the floor");
}

TEST(the_tide_is_a_pure_function_of_the_moon_and_the_day) {
    const Moon moon = seed_moon(99);
    for (const long long t : {0LL, 1LL, 57LL, 1234LL}) {
        const auto a = tide(moon, t);
        const auto b = tide(moon, t);
        for (int s = 0; s < kKnownSchools; ++s) {
            check::equal(a[static_cast<std::size_t>(s)], b[static_cast<std::size_t>(s)], "the same day, the same tide");
        }
    }
    // And it repeats on the great year, because all three cycles have come back round.
    const auto now = tide(moon, 0);
    const auto later = tide(moon, moon.great_year_days);
    for (int s = 0; s < kKnownSchools; ++s) {
        check::close(now[static_cast<std::size_t>(s)], later[static_cast<std::size_t>(s)], 1e-12,
                     "the tide to repeat on the great year");
    }
}

TEST(cycle_fractions_are_exact_at_the_turn) {
    const Moon moon = seed_moon(5);
    for (int which = 0; which < 3; ++which) {
        const int period = which == 0 ? moon.synodic_days : (which == 1 ? moon.spin_days : moon.nod_days);
        const int offset = which == 0 ? moon.synodic_offset : (which == 1 ? moon.spin_offset : moon.nod_offset);
        // Integer days stay in integer arithmetic until the division, so the turn of a
        // cycle is exactly zero rather than nearly zero.
        check::equal(cycle_fraction(moon, which, -offset), 0.0, "exactly zero at the turn");
        for (long long t = 0; t < 200; ++t) {
            const double f = cycle_fraction(moon, which, t);
            check::is_true(f >= 0.0 && f < 1.0, "a fraction in [0, 1)");
            check::close(f, cycle_fraction(moon, which, t + period), 1e-15, "a cycle to repeat");
        }
    }
}

TEST(day_state_reports_a_consistent_sky) {
    const Moon moon = seed_moon(42);
    for (long long t = 0; t < 400; ++t) {
        const DayState d = day_state(moon, t);
        check::between(d.illumination, 0.0, 1.0, "illumination");
        check::is_true(d.moonrise_hour >= 0.0 && d.moonrise_hour < kHoursPerDay, "moonrise within the day");
        check::between(d.elongation_degrees, 0.0, 360.0, "elongation");
        check::is_true(d.leaning == "still" || d.leaning == "restless", "a named lean");
        check::is_true(std::abs(d.nod_degrees) <= moon.tilt_max_degrees + 1e-12, "nod within the tilt");
        // Illumination and elongation are two readings of one number.
        check::close(d.illumination, (1.0 + std::cos(2.0 * std::acos(-1.0) * d.phase_fraction)) / 2.0, 1e-12,
                     "illumination to follow the phase");
        check::close(d.elongation_degrees / 360.0, d.phase_fraction, 1e-12, "elongation to follow the phase");
    }
}

// --- the almanac ----------------------------------------------------------

TEST(the_almanac_covers_its_year_and_sorts_its_events) {
    const Moon moon = seed_moon(42);
    const Almanac year = almanac(moon, 0);

    check::equal(year.months.size(), std::size_t{12}, "twelve months");
    for (std::size_t m = 0; m < year.months.size(); ++m) {
        check::equal(year.months[m].first_day, static_cast<long long>(m) * kDaysPerMonth, "month start");
        for (const double v : year.months[m].mean_tide) {
            check::between(v, 0.5, 1.8, "a mean tide inside the band");
        }
    }

    check::is_true(!year.events.empty(), "a year to hold some events");
    for (std::size_t i = 1; i < year.events.size(); ++i) {
        const AlmanacEvent& a = year.events[i - 1];
        const AlmanacEvent& b = year.events[i];
        const bool ordered = a.day < b.day || (a.day == b.day && (a.kind < b.kind ||
                                                                  (a.kind == b.kind && a.school <= b.school)));
        check::is_true(ordered, "events sorted by day, then kind, then school");
    }
    for (const AlmanacEvent& e : year.events) {
        check::between(e.day, 0, kDaysPerYear - 1, "an event inside the year");
        check::is_true(!e.kind.empty(), "a named event");
        if (e.kind == "surge") check::is_true(e.tide >= kSurge, "a surge to clear the threshold");
    }

    // Full and new moon each happen once per synodic period, so roughly a year's worth.
    const int full = static_cast<int>(std::count_if(year.events.begin(), year.events.end(),
                                                    [](const AlmanacEvent& e) { return e.kind == "full_moon"; }));
    check::between(full, kDaysPerYear / 34 - 1, kDaysPerYear / 26 + 1, "full moons in a year");
}

TEST(the_grand_alignment_is_a_real_alignment) {
    for (std::uint32_t seed = 1; seed <= 12; ++seed) {
        const Moon moon = seed_moon(seed);
        const Almanac year = almanac(moon, 0);
        if (year.next_grand_alignment_day < 0) continue;  // none inside the great year is legal
        const long long day = year.next_grand_alignment_day;
        check::equal((day + moon.synodic_offset) % moon.synodic_days, 0LL, "phase restarts");
        check::equal((day + moon.spin_offset) % moon.spin_days, 0LL, "spin restarts");
        check::equal((day + moon.nod_offset) % moon.nod_days, 0LL, "nod restarts");
        check::is_true(day < moon.great_year_days, "inside the great year");
    }
}

// --- in a world -----------------------------------------------------------

TEST(a_world_carries_its_sky_and_surges_only_the_known_schools) {
    const World world = build();
    check::is_true(world.has_astrology, "a phase-6 world to have a sky");
    check::equal(world.lunar_almanac.months.size(), std::size_t{12}, "an almanac");

    for (const MagicSchool& s : magic_school_table()) {
        const LeyNetwork& net = world.magic.networks.at(std::string(s.name));
        if (s.hidden) {
            check::is_true(!net.has_surge, std::string(s.name) + " to take no surge");
        } else {
            check::is_true(net.has_surge, std::string(s.name) + " to be surged");
            // The surge is the strength times that school's tide, so it stays inside
            // the tide band around the base strength.
            check::between(net.surged_strength, net.strength * 0.5, net.strength * 1.8 + 1e-12,
                           std::string(s.name) + " surged strength");
        }
    }
}

TEST(lunar_sensitivity_is_bounded_and_follows_its_inputs) {
    const World world = build(65);
    const Grid& sensitivity = world.layers.at("lunar_sensitivity");
    for (const double v : sensitivity.cells()) {
        check::between(v, 0.0, 1.0, "lunar sensitivity is a 0..1 ratio");
        check::is_true(v >= 0.2 - 1e-12, "a floor of 0.2 everywhere");
    }
    // A lake is all shore, so it must sit at least the shore term above the floor.
    const Grid& water = world.layers.at("water_type");
    int lakes = 0;
    for (int z = 0; z < water.size(); ++z) {
        for (int x = 0; x < water.size() - 1; ++x) {
            if (water.at(x, z) != 2.0) continue;
            ++lakes;
            check::is_true(sensitivity.at(x, z) >= 0.2 + 0.3 - 1e-9, "a lake to sway with the moon");
        }
    }
    check::is_true(lakes > 0, "a world with lakes to check");
}

TEST(the_sky_is_independent_of_world_scale) {
    // Every quantity in this module is a ratio of a cycle or a unit vector, never a
    // metre, so a wider world has exactly the same moon.
    Request small;
    small.seed = 42;
    small.size = 33;
    small.phase = 6;
    small.world_size = WorldSize::Small;
    Request large = small;
    large.world_size = WorldSize::Large;

    const World a = generate(resolve(small));
    const World b = generate(resolve(large));
    check::equal(a.moon.synodic_days, b.moon.synodic_days, "the same phase cycle");
    check::equal(a.moon.spin_days, b.moon.spin_days, "the same spin");
    check::equal(a.moon.nod_days, b.moon.nod_days, "the same nod");
    check::equal(a.moon.tilt_max_degrees, b.moon.tilt_max_degrees, "the same tilt");
    check::equal(a.lunar_almanac.events.size(), b.lunar_almanac.events.size(), "the same events");
}

TEST(lunar_sensitivity_carries_its_coastal_term_not_only_its_lakes) {
    // The shore term reads `coastal_exposure`, which ecology publishes. Astrology has to
    // run before ecology -- ecology reads the leyline fields astrology's inputs depend on
    // -- so the field is computed once with the layer absent and refreshed once it
    // exists. Run only once, every non-lake cell silently took shore = 0 and the
    // published layer was `0.2 + 0.5*instability` with the coastal half missing.
    //
    // The existing bounds test cannot see this: it exercises the lake branch, which is
    // the hardcoded water == 2.0 arm, and the 0.2 floor. Both still hold with the whole
    // coastal term zeroed. This is the assertion that does not.
    Request r;
    r.seed = 42;
    r.size = 65;
    r.phase = 9;
    const World world = generate(resolve(r));

    const auto& sensitivity = world.layers.at("lunar_sensitivity");
    const auto& water = world.layers.at("water_type");
    const auto& exposure = world.layers.at("coastal_exposure");

    long long exposed_cells = 0;
    long long sensitive_non_lakes = 0;
    for (int z = 0; z < world.config.size; ++z) {
        for (int x = 0; x < world.config.size; ++x) {
            if (water.at(x, z) == 2.0) continue;  // a lake is all shore by fiat
            if (exposure.at(x, z) > 0.5) ++exposed_cells;
            if (sensitivity.at(x, z) >= 0.5) ++sensitive_non_lakes;
        }
    }
    check::is_true(exposed_cells > 0,
                   "this world to have exposed coast, or the term is untestable here");
    check::is_true(sensitive_non_lakes > 0,
                   "open coast to raise lunar sensitivity above 0.5 somewhere; with the "
                   "coastal term missing this count is exactly zero");
}
