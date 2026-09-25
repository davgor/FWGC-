// The weather read.
//
// Weather is a proxy with no reference to diff against, so these hold it to the real-world
// rules it claims -- warm sea feeds a warm core, land starves it at the published rate, the
// belts blow the way Earth's do, cyclones turn the way the hemisphere turns them, storms come
// with the afternoon and the warm months -- to the percentages meaning what they say, to its
// sizes being the planet's share of Earth's, and to weather being a function of the moment alone.
#include "check.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "fwg/weather.hpp"
#include "fwg/world.hpp"
#include "fwg/world_time.hpp"

using namespace fwg;

namespace {

constexpr double kPi = std::numbers::pi;
constexpr std::int64_t kYearStart = 360LL * 1000 * 24;  // the first hour of year 1000
constexpr std::int64_t kYearHours = 360LL * 24;

World world_at(int size = 65, double offset = 0.0, double seasonality = 1.0, int phase = 9) {
    Request r;
    r.seed = 42;
    r.size = size;
    r.phase = phase;
    if (offset != 0.0) r.temperature_offset = offset;
    if (seasonality != 1.0) r.options["seasonality"] = seasonality;
    return generate(resolve(r));
}

double latitude_of(const Vec3& p) { return std::asin(std::max(-1.0, std::min(1.0, p.y))) * 180.0 / kPi; }
double longitude_of(const Vec3& p) { return std::atan2(p.z, p.x) * 180.0 / kPi; }

Vec3 point(double latitude_deg, double longitude_deg) {
    const double lat = latitude_deg * kPi / 180.0;
    const double lon = longitude_deg * kPi / 180.0;
    return {std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon)};
}

// East and north at p, written out here rather than borrowed from the model.
Vec3 east_at(const Vec3& p) {
    const double lon = std::atan2(p.z, p.x);
    return {-std::sin(lon), 0.0, std::cos(lon)};
}

Vec3 north_at(const Vec3& p) {
    const double lon = std::atan2(p.z, p.x);
    const double lat = std::asin(std::max(-1.0, std::min(1.0, p.y)));
    return {-std::sin(lat) * std::cos(lon), std::cos(lat), -std::sin(lat) * std::sin(lon)};
}

// Degrees clockwise from north.
double bearing(double east, double north) {
    double b = std::atan2(east, north) * 180.0 / kPi;
    return b < 0.0 ? b + 360.0 : b;
}

double apart_deg(double a, double b) {
    const double d = std::fmod(std::abs(a - b), 360.0);
    return std::min(d, 360.0 - d);
}

// The day whose month position is `month`: the middle of that month.
double mid_month(double month, int year = 1000) { return 360.0 * year + 30.0 * month + 15.0; }

std::string document(const std::vector<WeatherMoment>& moments) {
    std::ostringstream out;
    write_weather_json(out, moments);
    return out.str();
}

std::string document(const World& world, double day) {
    WeatherMoment moment;
    moment.report = weather_report(world, day);
    moment.samples = {weather_sample(world, moment.report, 12.5, -40.0),
                      weather_sample(world, moment.report, -47.0, 100.0)};
    return document(std::vector<WeatherMoment>{moment});
}

std::string id_of(const WeatherBirth& b) {
    return "weather-" + std::to_string(b.candidate.hour) + "-" + std::to_string(b.candidate.index);
}

const WeatherSystem* find(const WeatherReport& report, const std::string& id) {
    for (const WeatherSystem& s : report.systems) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

WeatherGround sea(double temperature_c, double latitude_deg = 15.0) {
    WeatherGround g;
    g.latitude_deg = latitude_deg;
    g.water = 1.0;
    g.ocean = 1.0;
    g.temperature_c = temperature_c;
    g.moisture = 0.6;
    g.contrast = 0.1;
    return g;
}

WeatherGround land(double moisture, double temperature_c, double contrast = 0.1) {
    WeatherGround g;
    g.latitude_deg = 15.0;
    g.temperature_c = temperature_c;
    g.moisture = moisture;
    g.contrast = contrast;
    return g;
}

WeatherState warm_core(double wind, double latitude_deg = 15.0) {
    WeatherState s;
    s.at = point(latitude_deg, 0.0);
    s.wind_m_s = wind;
    s.core = WeatherCore::Warm;
    s.born = WeatherCore::Warm;
    s.mature_hours = 45.0;
    s.size = 0.5;
    return s;
}

WeatherSystem synthetic(double latitude_deg, double wind, double core_m, double radius_m) {
    WeatherSystem s;
    s.id = "weather-test";
    s.latitude_deg = latitude_deg;
    s.longitude_deg = 0.0;
    s.wind_m_s = wind;
    s.intensity_pct = weather_intensity_pct(wind);
    s.core_radius_m = core_m;
    s.radius_m = radius_m;
    return s;
}

// The wind a sample reports, as east and north components.
std::pair<double, double> components(const WeatherSample& s) {
    const double b = s.wind_bearing_deg * kPi / 180.0;
    return {s.wind_speed_m_s * std::sin(b), s.wind_speed_m_s * std::cos(b)};
}

template <typename Call>
void refused(Call&& call, const std::string& field) {
    try {
        call();
    } catch (const WorldTimeError& error) {
        check::equal(error.field(), field, "the field refused");
        return;
    }
    check::fail("expected a refusal naming " + field);
}

}  // namespace

// ---- the two percentages -------------------------------------------------------------------

TEST(weather_intensity_reads_a_wind_on_the_named_scale) {
    check::close(weather_intensity_pct(0.0), 0.0, 1e-12, "calm");
    check::close(weather_intensity_pct(4.0), 5.0, 1e-12, "light rain");
    check::close(weather_intensity_pct(17.0), 40.0, 1e-12, "a gale, a tropical storm");
    check::close(weather_intensity_pct(21.0), 50.0, 1e-12, "a thunderstorm's wind");
    check::close(weather_intensity_pct(27.0), 75.0, 1e-12, "a storm");
    check::close(weather_intensity_pct(33.0), 100.0, 1e-12, "hurricane force tops the scale");
    check::close(weather_intensity_pct(60.0), 100.0, 1e-12, "a major hurricane reads the top too");
    check::close(weather_intensity_pct(-3.0), 0.0, 1e-12, "no negative severity");
    check::close(weather_intensity_pct(std::nan("")), 0.0, 1e-12, "no severity from nothing");
    double previous = -1.0;
    for (double wind = 0.0; wind <= 100.0; wind += 0.25) {
        const double pct = weather_intensity_pct(wind);
        check::is_true(pct >= previous, "severity never falls as the wind rises, at " + std::to_string(wind));
        previous = pct;
    }
    for (double pct = 0.0; pct <= 100.0; pct += 2.5) {
        check::close(weather_intensity_pct(weather_wind_for_pct(pct)), pct, 1e-9, "the inverse at " + std::to_string(pct));
    }
}

TEST(weather_summer_is_how_warm_a_place_is_not_where_it_stands_in_its_year) {
    check::close(weather_summer_pct(-12.0), 0.0, 1e-12, "a frozen month is all winter");
    check::close(weather_summer_pct(0.0), 0.0, 1e-12, "freezing is all winter");
    check::close(weather_summer_pct(5.0), 25.0, 1e-12, "a cold month");
    check::close(weather_summer_pct(10.0), 50.0, 1e-12, "halfway");
    check::close(weather_summer_pct(20.0), 100.0, 1e-12, "a warm month is all summer");
    check::close(weather_summer_pct(31.0), 100.0, 1e-12, "a hot one too");

    const World world = world_at();
    const WeatherInputs in = weather_inputs(world);
    const Grid& type = world.layers.at("water_type");
    const int n = type.size();

    // An equatorial sea is summer in every month: it never has a winter.
    int tropical = 0;
    for (int x = 0; x < n - 1; ++x) {
        const int z = (n - 1) / 2;
        if (type.at(x, z) != 1.0) continue;
        ++tropical;
        const Vec3 p = direction(x, z, n);
        for (int m = 0; m < 12; ++m) {
            check::close(weather_summer_pct(weather_ground(in, p, mid_month(m)).temperature_c), 100.0, 1e-9,
                         "the equatorial sea in month " + std::to_string(m));
        }
        // And a sample there says so, whatever the month: the season a sample carries is this
        // absolute one, not the place's position in its own year.
        if (tropical == 1) {
            for (const int m : {0, 3, 6, 9}) {
                const WeatherSample s = weather_at(world, mid_month(m), 0.0, longitude_of(p));
                check::close(s.summer_pct, 100.0, 1e-9, "a sample on the equatorial sea in month " + std::to_string(m));
            }
        }
    }
    check::is_true(tropical > 0, "premise: some sea on the equator");

    // Ground whose warmest month is below freezing is winter in every month: it never has a summer.
    int frozen = 0;
    for (int z = 1; z < n - 1 && frozen < 8; ++z) {
        for (int x = 0; x < n - 1 && frozen < 8; ++x) {
            double warmest = -1e9;
            for (const SeasonalMonth& month : world.seasonal_months) warmest = std::max(warmest, month.temperature.at(x, z));
            if (!(warmest < -2.0)) continue;
            ++frozen;
            const Vec3 p = direction(x, z, n);
            for (int m = 0; m < 12; ++m) {
                check::close(weather_summer_pct(weather_ground(in, p, mid_month(m)).temperature_c), 0.0, 1e-9,
                             "frozen ground in month " + std::to_string(m));
            }
            if (frozen == 1) {
                for (const int m : {0, 6}) {
                    const WeatherSample s = weather_at(world, mid_month(m), latitude_of(p), longitude_of(p));
                    check::close(s.summer_pct, 0.0, 1e-9, "a sample on frozen ground in month " + std::to_string(m));
                }
            }
        }
    }
    check::is_true(frozen > 0, "premise: some ground that never thaws");
}

TEST(weather_season_is_the_month_s_not_the_hour_s) {
    // A night is colder than an afternoon on land, and the season does not follow it: a sample's
    // summer is its month's temperature, blended to the day, with no daily swing.
    const World world = world_at();
    const WeatherInputs in = weather_inputs(world);
    const Grid& type = world.layers.at("water_type");
    const int n = type.size();
    const double day = std::floor(mid_month(3));
    int checked = 0;
    for (int z = 1; z < n - 1 && checked < 6; ++z) {
        for (int x = 0; x < n - 1 && checked < 6; ++x) {
            if (type.at(x, z) != 0.0) continue;
            const Vec3 p = direction(x, z, n);
            const double month_c = weather_ground(in, p, day).temperature_c;
            if (!(month_c > 4.0 && month_c < 16.0)) continue;  // not pinned at either end
            ++checked;
            for (const double hour : {3.0, 9.0, 15.0, 21.0}) {
                const double at = day + hour / 24.0;
                const WeatherSample s = weather_at(world, at, latitude_of(p), longitude_of(p));
                check::close(s.summer_pct, weather_summer_pct(weather_ground(in, p, at).temperature_c), 1e-9,
                             "the month's season at hour " + std::to_string(hour));
            }
        }
    }
    check::is_true(checked > 0, "premise: land between a cold and a warm month");
}

TEST(weather_each_hemisphere_has_its_summer_in_its_own_warm_months) {
    const World world = world_at();
    const WeatherInputs in = weather_inputs(world);
    for (const double latitude : {48.0, -48.0}) {
        int warmest = -1;
        double best = -1.0;
        double coldest = 101.0;
        for (int m = 0; m < 12; ++m) {
            const double summer = weather_summer_pct(weather_ground(in, point(latitude, 30.0), mid_month(m)).temperature_c);
            if (summer > best) {
                best = summer;
                warmest = m;
            }
            coldest = std::min(coldest, summer);
        }
        check::is_true(best - coldest > 20.0, "premise: a mid-latitude year swings, at " + std::to_string(latitude));
        const int expected = latitude > 0.0 ? 6 : 0;
        const int apart = std::min((warmest - expected + 12) % 12, (expected - warmest + 12) % 12);
        check::is_true(apart <= 1, "summer peaks in month " + std::to_string(warmest) + " at latitude " +
                                       std::to_string(latitude));
    }
}

TEST(weather_without_seasons_is_the_same_season_all_year) {
    // The seasonality control moves the summer percentage: with none, a place's season is fixed
    // by how warm it is; with the default, a mid-latitude place swings through the year.
    for (const double seasonality : {0.0, 1.0}) {
        const World world = world_at(33, 0.0, seasonality);
        const WeatherInputs in = weather_inputs(world);
        double low = 101.0;
        double high = -1.0;
        for (int m = 0; m < 12; ++m) {
            const double summer = weather_summer_pct(weather_ground(in, point(45.0, 10.0), mid_month(m)).temperature_c);
            low = std::min(low, summer);
            high = std::max(high, summer);
        }
        if (seasonality == 0.0) {
            check::is_true(high - low < 1e-9, "no seasons: one season all year");
        } else {
            check::is_true(high - low > 20.0, "seasons: the season moves");
        }
    }
}

// ---- a function of the moment ----------------------------------------------------------------

TEST(weather_is_a_function_of_the_moment_alone) {
    const World world = world_at();
    const double day = static_cast<double>(kYearStart) / 24.0 + 123.25;
    const std::string first = document(world, day);
    for (const double other : {day - 40.0, day + 3.5, 0.0, day + 900.0}) (void)weather_report(world, other);
    const World copy = world;
    check::equal(document(world, day), first, "the same moment asked again, after other moments");
    check::equal(document(copy, day), first, "the same moment on a copy of the world");
    check::is_true(!weather_report(world, day).systems.empty(), "premise: systems at that moment");
}

TEST(weather_system_is_the_same_whenever_it_is_asked_about) {
    const World world = world_at();
    const WeatherInputs in = weather_inputs(world);
    // An hour when a system more than ten days old is alive: the report must reach that far back.
    std::int64_t now = -1;
    for (const WeatherBirth& b : weather_births(in, kYearStart, kYearStart + kYearHours - 1)) {
        if (b.core == WeatherCore::Warm && weather_track(in, b, b.candidate.hour + 600).size() > 262) {
            now = b.candidate.hour + 250;
            break;
        }
    }
    check::is_true(now > 0, "premise: a system that lives past ten days");

    // A track to a later hour starts with the track to an earlier one, state for state.
    const std::vector<WeatherBirth> births = weather_births(in, now - 200, now);
    check::is_true(births.size() >= 5, "premise: systems born in those hours");
    for (std::size_t i = 0; i < births.size(); i += 3) {
        const auto shorter = weather_track(in, births[i], now);
        const auto longer = weather_track(in, births[i], now + 100);
        check::is_true(longer.size() >= shorter.size(), "a longer track is no shorter");
        for (std::size_t k = 0; k < shorter.size(); ++k) {
            check::is_true(shorter[k].wind_m_s == longer[k].wind_m_s && shorter[k].at.x == longer[k].at.x &&
                               shorter[k].at.y == longer[k].at.y && shorter[k].at.z == longer[k].at.z &&
                               shorter[k].core == longer[k].core,
                           "the same state at hour " + std::to_string(k) + " of " + id_of(births[i]));
        }
    }

    // A report on a whole hour holds exactly the systems alive then -- every one of them, however
    // old -- each read at its state for that hour.
    const WeatherReport report = weather_report(world, static_cast<double>(now) / 24.0);
    std::set<std::string> alive;
    int old = 0;
    for (const WeatherBirth& b : weather_births(in, now - 503, now)) {
        const auto track = weather_track(in, b, now + 1);
        const auto at = static_cast<std::size_t>(now - b.candidate.hour);
        if (at >= track.size() || !track[at].alive) continue;
        alive.insert(id_of(b));
        if (now - b.candidate.hour > 240) ++old;
        const WeatherSystem* system = find(report, id_of(b));
        check::is_true(system != nullptr, "a live system is reported: " + id_of(b));
        check::equal(system->wind_m_s, track[at].wind_m_s, "the hour's wind of " + system->id);
        check::close(system->latitude_deg, latitude_of(track[at].at), 1e-12, "the hour's latitude of " + system->id);
        check::close(system->born_day, static_cast<double>(b.candidate.hour) / 24.0, 0.0, "the birth of " + system->id);
    }
    check::equal(report.systems.size(), alive.size(), "nothing reported that is not alive");
    check::is_true(!alive.empty(), "premise: systems alive at that hour");
    check::is_true(old > 0, "premise: a system older than ten days alive at that hour");

    // Half an hour on, each system stands halfway between its two hours: wind, place and age.
    const WeatherReport half = weather_report(world, (static_cast<double>(now) + 0.5) / 24.0);
    const WeatherReport next = weather_report(world, static_cast<double>(now + 1) / 24.0);
    int between = 0;
    for (const WeatherSystem& h : half.systems) {
        const WeatherSystem* a = find(report, h.id);
        const WeatherSystem* b = find(next, h.id);
        if (a == nullptr || b == nullptr) continue;
        check::close(h.wind_m_s, 0.5 * (a->wind_m_s + b->wind_m_s), 1e-9, "the half hour's wind of " + h.id);
        // Halfway along the way it went: as far from where it was as from where it will be,
        // and nearer both than they are to each other.
        const Vec3 pa = point(a->latitude_deg, a->longitude_deg);
        const Vec3 pb = point(b->latitude_deg, b->longitude_deg);
        const Vec3 ph = point(h.latitude_deg, h.longitude_deg);
        const auto chord = [](const Vec3& u, const Vec3& v) { return std::sqrt(dot(u - v, u - v)); };
        check::close(chord(pa, ph), chord(ph, pb), 1e-12, "the half hour's place of " + h.id);
        check::is_true(chord(pa, ph) <= chord(pa, pb) + 1e-15, "between its hours: " + h.id);
        check::close(h.age_hours, a->age_hours + 0.5, 1e-9, "half an hour older: " + h.id);
        if (chord(pa, pb) > 1e-9) ++between;
    }
    check::is_true(between > 0, "premise: systems alive across the hour");
}

TEST(weather_a_clock_a_hair_short_of_an_hour_reads_that_hour) {
    // A clock advanced in half hours lands a few ulps either side of each hour. One a hair short
    // must read the hour it is, not the hour before -- which holds a different set of systems.
    const World world = world_at(33);
    double day = static_cast<double>(kYearStart) / 24.0;
    double short_of = -1.0;
    double hour = 0.0;
    for (int k = 0; k < 400 && short_of < 0.0; ++k) {
        day += 0.5 / 24.0;
        const double hours = day * 24.0;
        const double gap = std::round(hours) - hours;
        if (gap > 0.0 && gap < 1e-6) {
            short_of = day;
            hour = std::round(hours);
        }
    }
    check::is_true(short_of > 0.0, "premise: a clock a hair short of an hour");
    const WeatherReport near = weather_report(world, short_of);
    const WeatherReport exact = weather_report(world, hour / 24.0);
    check::is_true(!exact.systems.empty(), "premise: systems at that hour");
    check::close(near.hour, exact.hour, 0.0, "the same hour of the day");
    check::equal(near.systems.size(), exact.systems.size(), "the same systems");
    for (std::size_t i = 0; i < near.systems.size() && i < exact.systems.size(); ++i) {
        check::equal(near.systems[i].id, exact.systems[i].id, "the same system");
        check::equal(near.systems[i].age_hours, exact.systems[i].age_hours, "the same age");
    }
}

TEST(weather_ground_reads_each_month_at_its_middle_and_blends_between) {
    // A month's grid holds at the middle of the month; halfway to the next month's middle the
    // ground reads the two equally. Read against grid.hpp's own sample, not the model's reader.
    const World world = world_at();
    const WeatherInputs in = weather_inputs(world);
    const Grid& moisture = world.layers.at("moisture");
    for (int i = 0; i < 200; ++i) {
        const double latitude = -89.0 + 178.0 * ((i * 37) % 200) / 200.0;
        const double longitude = -180.0 + 360.0 * ((i * 91) % 200) / 200.0;
        const Vec3 p = point(latitude, longitude);
        const auto m = static_cast<std::size_t>(i % 12);
        const std::size_t before = (m + 11) % 12;
        check::close(weather_ground(in, p, mid_month(static_cast<double>(m))).temperature_c,
                     sample(world.seasonal_months[m].temperature, p), 1e-9, "mid-month at point " + std::to_string(i));
        const double boundary = 360.0 * 1000 + 30.0 * static_cast<double>(m);  // the first day of month m
        check::close(weather_ground(in, p, boundary).temperature_c,
                     0.5 * (sample(world.seasonal_months[before].temperature, p) + sample(world.seasonal_months[m].temperature, p)),
                     1e-9, "a month's first day at point " + std::to_string(i));
        check::close(weather_ground(in, p, boundary).moisture, std::max(0.0, std::min(1.0, sample(moisture, p))), 1e-12,
                     "moisture at point " + std::to_string(i));
    }
}

// ---- where and when systems are born ---------------------------------------------------------

TEST(weather_warm_cores_are_born_only_over_warm_open_sea_off_the_equator) {
    const World world = world_at();
    const WeatherInputs in = weather_inputs(world);
    // An independent read of the ocean: one where the cell is ocean, blended as grid.hpp blends.
    const Grid& type = world.layers.at("water_type");
    Grid ocean(type.size());
    for (int z = 0; z < type.size(); ++z) {
        for (int x = 0; x < type.size(); ++x) ocean.at(x, z) = type.at(x, z) == 1.0 ? 1.0 : 0.0;
    }
    int warm = 0;
    int cold = 0;
    for (const WeatherBirth& b : weather_births(in, kYearStart, kYearStart + kYearHours - 1)) {
        if (b.core == WeatherCore::Cold) {
            ++cold;
            continue;
        }
        ++warm;
        const Vec3& p = b.candidate.at;
        const double day = static_cast<double>(b.candidate.hour) / 24.0;
        double m = std::fmod(day, 360.0) / 30.0 - 0.5;
        if (m < 0.0) m += 12.0;
        const auto first = static_cast<std::size_t>(std::floor(m)) % 12;
        const double w = m - std::floor(m);
        const double sea_surface = (1.0 - w) * sample(world.seasonal_months[first].temperature, p) +
                                   w * sample(world.seasonal_months[(first + 1) % 12].temperature, p);
        check::is_true(sea_surface > 26.5, id_of(b) + " born over sea at " + std::to_string(sea_surface) + " C");
        check::is_true(std::abs(latitude_of(p)) > 3.0, id_of(b) + " born off the equator");
        check::is_true(sample(ocean, p) > 0.5, id_of(b) + " born over mostly open ocean, not " +
                                                   std::to_string(sample(ocean, p)));
    }
    check::is_true(warm >= 20, "premise: warm cores are born in a year, got " + std::to_string(warm));
    check::is_true(cold >= 200, "premise: cold cores are born in a year, got " + std::to_string(cold));
}

TEST(weather_systems_follow_each_hemisphere_s_seasons) {
    // Warm cores in the warm months, cold cores in the cold ones, each hemisphere on its own
    // calendar: the north's summer is month 6, the south's month 0.
    const World world = world_at();
    const WeatherInputs in = weather_inputs(world);
    int warm[2][12] = {};
    int cold[2][12] = {};
    for (const WeatherBirth& b : weather_births(in, kYearStart, kYearStart + kYearHours - 1)) {
        const auto month = static_cast<std::size_t>((b.candidate.hour - kYearStart) / 24 / 30);
        const std::size_t south = latitude_of(b.candidate.at) < 0.0 ? 1 : 0;
        if (b.core == WeatherCore::Warm) {
            ++warm[south][month];
        } else {
            ++cold[south][month];
        }
    }
    using Months = std::array<int, 5>;
    constexpr Months kNorthSummer = {4, 5, 6, 7, 8};
    constexpr Months kSouthSummer = {10, 11, 0, 1, 2};
    const auto sum = [](const int (&by_month)[12], const Months& months) {
        int total = 0;
        for (const int m : months) total += by_month[m];
        return total;
    };
    const auto summer_of = [&](std::size_t south) { return south == 0 ? kNorthSummer : kSouthSummer; };
    const auto winter_of = [&](std::size_t south) { return south == 0 ? kSouthSummer : kNorthSummer; };
    for (std::size_t h = 0; h < 2; ++h) {
        const std::string name = h == 0 ? "north" : "south";
        const int warm_summer = sum(warm[h], summer_of(h));
        const int warm_winter = sum(warm[h], winter_of(h));
        check::is_true(warm_summer >= 10, "premise: " + name + " warm cores in summer, got " + std::to_string(warm_summer));
        check::is_true(warm_summer > 4 * warm_winter, name + ": warm cores " + std::to_string(warm_summer) +
                                                          " in summer against " + std::to_string(warm_winter));
        const int cold_summer = sum(cold[h], summer_of(h));
        const int cold_winter = sum(cold[h], winter_of(h));
        check::is_true(cold_winter > 2 * cold_summer, name + ": cold cores " + std::to_string(cold_winter) +
                                                          " in winter against " + std::to_string(cold_summer));
    }
}

TEST(weather_warmer_world_breeds_more_warm_cores) {
    int previous = -1;
    for (const double offset : {-3.0, 0.0, 3.0}) {
        const World world = world_at(33, offset);
        int warm = 0;
        for (const WeatherBirth& b : weather_births(weather_inputs(world), kYearStart, kYearStart + kYearHours - 1)) {
            if (b.core == WeatherCore::Warm) ++warm;
        }
        check::is_true(warm > previous, "offset " + std::to_string(offset) + " breeds " + std::to_string(warm) +
                                            ", more than " + std::to_string(previous));
        previous = warm;
    }
    check::is_true(previous > 0, "premise: the warmest world breeds some");
}

TEST(weather_candidates_are_spread_evenly_over_the_sphere) {
    int count = 0;
    Vec3 sum;
    int middle = 0;
    int eastern = 0;
    for (std::int64_t hour = 0; hour < 20000; ++hour) {
        const auto candidates = weather_candidates(42u, hour);
        const auto again = weather_candidates(42u, hour);
        check::equal(candidates.size(), again.size(), "the same hour draws the same count");
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            check::is_true(candidates[i].at.y == again[i].at.y && candidates[i].accept == again[i].accept,
                           "the same hour draws the same candidates");
            ++count;
            sum = sum + candidates[i].at;
            if (std::abs(candidates[i].at.y) < 0.5) ++middle;
            if (candidates[i].at.z > 0.0) ++eastern;
        }
    }
    check::is_true(count > 10000, "premise: candidates drawn");
    // Uniform over area: the height (sine of latitude) is uniform in -1..1 and every longitude
    // as likely as another, so the points balance about the centre in every direction.
    check::close(sum.x / count, 0.0, 0.02, "the mean along x");
    check::close(sum.y / count, 0.0, 0.02, "the mean height");
    check::close(sum.z / count, 0.0, 0.02, "the mean along z");
    check::close(static_cast<double>(middle) / count, 0.5, 0.02, "the share within 30 degrees of the equator");
    check::close(static_cast<double>(eastern) / count, 0.5, 0.02, "the share in the half with z above zero");
}

// ---- how systems live ------------------------------------------------------------------------

TEST(weather_warm_sea_feeds_a_warm_core_and_land_starves_it) {
    const double radius = 31831.0;
    // Over a 29 C sea in calm air it strengthens every hour, reaching hurricane force within three
    // days and never passing what the sea allows.
    WeatherState s = warm_core(10.0);
    for (int h = 0; h < 72; ++h) {
        const double before = s.wind_m_s;
        s = step_weather(s, sea(29.0), 0.0, 1000.0, radius);
        check::is_true(s.wind_m_s > before, "stronger each hour over warm sea, at hour " + std::to_string(h));
    }
    check::is_true(s.wind_m_s > 33.0, "hurricane force within three days: " + std::to_string(s.wind_m_s));
    check::is_true(s.wind_m_s <= warm_core_support(sea(29.0)) + 1e-9, "no more than the sea allows");

    // Over dry land it decays at the published inland rate, e-folding in 1/0.095 hours.
    s = warm_core(50.0);
    for (int h = 0; h < 18; ++h) s = step_weather(s, land(0.05, 25.0), 0.0, 1000.0, radius);
    check::between(s.wind_m_s, 50.0 * std::exp(-0.11 * 18), 50.0 * std::exp(-0.08 * 18), "eighteen hours over dry land");

    // Over warm, wet land it settles at the published humid-land floor instead of dying.
    s = warm_core(50.0);
    for (int h = 0; h < 96; ++h) s = step_weather(s, land(0.9, 28.0), 0.0, 1000.0, radius);
    check::close(s.wind_m_s, 13.7, 0.5, "the humid-land floor");
    check::is_true(s.alive, "a remnant over wet land lives on");

    // A weak one over that same land never grows toward the floor: only water feeds a warm core.
    s = warm_core(10.0);
    for (int h = 0; h < 24; ++h) {
        const double before = s.wind_m_s;
        s = step_weather(s, land(0.9, 28.0), 0.0, 1000.0, radius);
        check::is_true(s.wind_m_s <= before + 1e-12, "no strengthening over land, at hour " + std::to_string(h));
    }

    // Over cool sea it weakens, and more slowly than over land.
    WeatherState over_sea = warm_core(45.0);
    WeatherState over_land = warm_core(45.0);
    for (int h = 0; h < 12; ++h) {
        over_sea = step_weather(over_sea, sea(20.0), 0.0, 1000.0, radius);
        over_land = step_weather(over_land, land(0.05, 25.0), 0.0, 1000.0, radius);
    }
    check::is_true(over_sea.wind_m_s < 45.0, "cool sea weakens it");
    check::is_true(over_land.wind_m_s < over_sea.wind_m_s, "land weakens it faster than cool sea");
}

TEST(weather_shear_holds_a_warm_core_below_what_its_sea_allows) {
    const double radius = 31831.0;
    WeatherState calm = warm_core(10.0);
    WeatherState sheared = warm_core(10.0);
    for (int h = 0; h < 200; ++h) {
        calm = step_weather(calm, sea(29.0), 0.0, 1000.0, radius);
        sheared = step_weather(sheared, sea(29.0), 0.8, 1000.0, radius);
    }
    check::is_true(sheared.wind_m_s < 0.6 * calm.wind_m_s, "shear keeps it weak: " + std::to_string(sheared.wind_m_s) +
                                                               " against " + std::to_string(calm.wind_m_s));
}

TEST(weather_warm_core_carried_into_cold_contrast_turns_cold) {
    // Over cool sea in the high-contrast mid-latitudes a warm core becomes a cold core, as a
    // hurricane carried into the westerlies does; in the tropics the same ground cannot turn it.
    const double radius = 31831.0;
    WeatherGround westerlies = sea(18.0, 40.0);
    westerlies.contrast = 1.0;
    westerlies.moisture = 0.7;
    WeatherState s = warm_core(35.0, 40.0);
    s.age_hours = 100.0;
    s = step_weather(s, westerlies, 0.0, 1000.0, radius);
    check::is_true(s.core == WeatherCore::Cold, "turned cold-core in the westerlies");
    check::close(s.transition_hours, 100.0, 0.0, "the age it turned at");

    WeatherGround tropics = westerlies;
    tropics.latitude_deg = 15.0;
    WeatherState t = warm_core(35.0);
    t = step_weather(t, tropics, 0.0, 1000.0, radius);
    check::is_true(t.core == WeatherCore::Warm, "still warm-core in the tropics");

    // And on a real world, some of a year's warm cores make that passage.
    const World world = world_at();
    const WeatherInputs in = weather_inputs(world);
    int warm = 0;
    int turned = 0;
    for (const WeatherBirth& b : weather_births(in, kYearStart, kYearStart + kYearHours - 1)) {
        if (b.core != WeatherCore::Warm) continue;
        ++warm;
        const auto track = weather_track(in, b, b.candidate.hour + 600);
        turned += track.back().core == WeatherCore::Cold ? 1 : 0;
    }
    check::is_true(turned > 0 && turned < warm, "some warm cores turn cold, not all: " + std::to_string(turned) +
                                                    " of " + std::to_string(warm));
}

TEST(weather_belts_blow_the_way_earth_s_do) {
    const double equinox = mid_month(3);  // the belts sit centred on the equator
    const BeltWind trade_north = weather_surface_wind(15.0, equinox);
    const BeltWind trade_south = weather_surface_wind(-15.0, equinox);
    const BeltWind west_north = weather_surface_wind(45.0, equinox);
    const BeltWind west_south = weather_surface_wind(-45.0, equinox);
    const BeltWind polar = weather_surface_wind(75.0, equinox);
    check::is_true(trade_north.east_m_s < 0.0 && trade_north.north_m_s < 0.0, "northern trades from the northeast");
    check::is_true(trade_south.east_m_s < 0.0 && trade_south.north_m_s > 0.0, "southern trades from the southeast");
    check::is_true(west_north.east_m_s > 0.0 && west_north.north_m_s > 0.0, "northern westerlies from the southwest");
    check::is_true(west_south.east_m_s > 0.0 && west_south.north_m_s < 0.0, "southern westerlies from the northwest");
    check::is_true(polar.east_m_s < 0.0, "polar easterlies");
    for (const double calm : {0.0, 30.0, -30.0, 60.0, -60.0}) {
        const BeltWind w = weather_surface_wind(calm, equinox);
        check::close(std::hypot(w.east_m_s, w.north_m_s), 0.0, 1e-9, "a calm belt at " + std::to_string(calm));
    }
    // The belts follow the sun: the equatorial calm moves north in the north's summer, south in the south's.
    check::close(std::hypot(weather_surface_wind(5.0, mid_month(6)).east_m_s, weather_surface_wind(5.0, mid_month(6)).north_m_s),
                 0.0, 1e-9, "the calm at 5 N in the north's summer");
    check::close(std::hypot(weather_surface_wind(-5.0, mid_month(0)).east_m_s, weather_surface_wind(-5.0, mid_month(0)).north_m_s),
                 0.0, 1e-9, "the calm at 5 S in the south's summer");
    // Aloft the westerlies run faster than at the surface.
    check::is_true(weather_steering_wind(45.0, equinox).east_m_s > west_north.east_m_s, "faster westerlies aloft");
}

TEST(weather_tropical_systems_run_west_and_poleward_and_frontal_ones_east) {
    const World world = world_at();
    const WeatherInputs in = weather_inputs(world);
    int tropical_steps = 0;
    int westward = 0;
    int warm = 0;
    int poleward = 0;
    int frontal_steps = 0;
    int eastward = 0;
    for (const WeatherBirth& b : weather_births(in, kYearStart, kYearStart + kYearHours - 1)) {
        const auto track = weather_track(in, b, b.candidate.hour + 600);
        for (std::size_t k = 0; k + 1 < track.size(); ++k) {
            const double lat = std::abs(latitude_of(track[k].at));
            const double east = dot(track[k + 1].at - track[k].at, east_at(track[k].at));
            if (b.core == WeatherCore::Warm && lat < 20.0) {
                ++tropical_steps;
                westward += east < 0.0 ? 1 : 0;
            }
            if (b.core == WeatherCore::Cold && lat > 40.0 && lat < 50.0) {
                ++frontal_steps;
                eastward += east > 0.0 ? 1 : 0;
            }
        }
        if (b.core == WeatherCore::Warm && track.size() > 24) {
            ++warm;
            poleward += std::abs(latitude_of(track.back().at)) > std::abs(latitude_of(track.front().at)) ? 1 : 0;
        }
    }
    check::is_true(tropical_steps > 500 && frontal_steps > 500, "premise: steps in both belts");
    check::is_true(westward > 0.9 * tropical_steps, "tropical steps westward: " + std::to_string(westward) + " of " +
                                                        std::to_string(tropical_steps));
    check::is_true(eastward > 0.9 * frontal_steps, "mid-latitude steps eastward: " + std::to_string(eastward) + " of " +
                                                       std::to_string(frontal_steps));
    check::is_true(warm > 10 && poleward > 0.8 * warm, "warm cores end poleward of where they began: " +
                                                          std::to_string(poleward) + " of " + std::to_string(warm));
}

TEST(weather_reports_say_when_they_are_and_where_systems_go) {
    const World world = world_at();
    const WeatherReport half_past = weather_report(world, std::floor(mid_month(4)) + 13.5 / 24.0);
    check::close(half_past.hour, 13.5, 1e-9, "the hour of the day");
    check::equal(half_past.month, 4, "the month");

    // A system's heading and speed are the move it makes over the next hour.
    const double day = std::floor(mid_month(4)) + 13.0 / 24.0;
    const WeatherReport now = weather_report(world, day);
    const WeatherReport later = weather_report(world, day + 1.0 / 24.0);
    const double radius = world.effective.globe_radius;
    int moved = 0;
    for (const WeatherSystem& a : now.systems) {
        const WeatherSystem* b = find(later, a.id);
        if (b == nullptr) continue;
        const Vec3 from = point(a.latitude_deg, a.longitude_deg);
        const Vec3 to = point(b->latitude_deg, b->longitude_deg);
        const double distance = great_circle_m(radius, from, to);
        if (!(distance > 1e-3)) continue;
        const Vec3 step = to - from;
        check::is_true(apart_deg(a.heading_deg, bearing(dot(step, east_at(from)), dot(step, north_at(from)))) < 2.0,
                       "the heading of " + a.id);
        check::close(a.speed_m_s, distance / 3600.0, 0.02 * distance / 3600.0, "the speed of " + a.id);
        ++moved;
    }
    check::is_true(moved > 3, "premise: systems moving across the hour");
}

TEST(weather_systems_are_the_planet_s_share_of_earth_s) {
    // A system covers the share of this planet a real one covers of Earth, and moves across it
    // at the share a real one moves across Earth, on every size of world.
    std::vector<double> mean_radius;
    for (const WorldSize size : {WorldSize::Small, WorldSize::Large}) {
        Request r;
        r.seed = 42;
        r.size = 33;
        r.phase = 9;
        r.world_size = size;
        const World world = generate(resolve(r));
        const double around = 2.0 * kPi * world.effective.globe_radius;
        double total = 0.0;
        int count = 0;
        for (int d = 0; d < 20; ++d) {
            const WeatherReport report = weather_report(world, mid_month(d % 12) + 0.25 + d);
            for (const WeatherSystem& s : report.systems) {
                check::between(s.radius_m / around, 200.0 / 40075.0, 3200.0 / 40075.0, "a system's share of the world");
                check::is_true(s.speed_m_s * 86400.0 / around < 0.06, "a day's travel is a small share of the world");
                total += s.radius_m;
                ++count;
            }
        }
        check::is_true(count > 50, "premise: systems to measure");
        mean_radius.push_back(total / count);
    }
    check::between(mean_radius[1] / mean_radius[0], 2.0, 4.5, "a world three times as wide has systems about three times as wide");
}

// ---- a system at a place ---------------------------------------------------------------------

TEST(weather_cyclones_turn_counterclockwise_north_and_clockwise_south) {
    const World world = world_at();
    const double radius = world.effective.globe_radius;
    for (const double latitude : {20.0, -20.0}) {
        WeatherReport with = weather_report(world, mid_month(3) + 0.25);
        with.systems.clear();
        WeatherReport without = with;
        with.systems.push_back(synthetic(latitude, 40.0, 400.0, 3000.0));
        // A kilometre east of the centre, outside the core.
        const double east_deg = (1000.0 / radius) * 180.0 / kPi / std::cos(latitude * kPi / 180.0);
        const auto [e1, n1] = components(weather_sample(world, with, latitude, east_deg));
        const auto [e0, n0] = components(weather_sample(world, without, latitude, east_deg));
        const double north = n1 - n0;
        const double east = e1 - e0;
        if (latitude > 0.0) {
            check::is_true(north > 5.0, "north of the equator, the wind east of a centre blows north");
        } else {
            check::is_true(north < -5.0, "south of the equator, the wind east of a centre blows south");
        }
        check::is_true(east < 0.0, "the wind spirals in toward the centre, at " + std::to_string(latitude));
    }
}

TEST(weather_eye_is_calmer_than_its_wall_and_a_storm_without_one_is_not) {
    // Past hurricane force the weather clears toward a system's middle -- in what a sample reads,
    // with no field on the system saying so.
    const World world = world_at();
    const double radius = world.effective.globe_radius;
    const double day = mid_month(6) + 0.9;  // the south's winter, at night
    const double latitude = -70.0;           // too cold for any thunderstorm
    const double to_wall = (800.0 / radius) * 180.0 / kPi;

    WeatherReport report = weather_report(world, day);
    report.systems = {synthetic(latitude, 60.0, 800.0, 4000.0)};
    const WeatherSample centre = weather_sample(world, report, latitude, 0.0);
    const WeatherSample wall = weather_sample(world, report, latitude + to_wall, 0.0);
    check::equal(wall.system, std::string("weather-test"), "the wall is the system's weather");
    check::close(wall.intensity_pct, 100.0, 1e-3, "a hurricane's wall reads the top of the scale");
    check::is_true(centre.intensity_pct < 0.5 * wall.intensity_pct, "the eye is calm beside its wall: " +
                                                                        std::to_string(centre.intensity_pct));

    // Below hurricane force there is no eye: the middle is the worst of it.
    for (const double wind : {25.0, 32.0}) {
        report.systems = {synthetic(latitude, wind, 800.0, 4000.0)};
        check::close(weather_sample(world, report, latitude, 0.0).intensity_pct, weather_intensity_pct(wind), 1e-6,
                     "the centre of a " + std::to_string(wind) + " m/s system");
    }
}

TEST(weather_hurricane_force_systems_clear_toward_their_middle) {
    // Through a northern hurricane season, the real systems past hurricane force read calmer at
    // their middles than at their walls.
    const World world = world_at();
    const double radius = world.effective.globe_radius;
    int checked = 0;
    for (int d = 0; d < 120; d += 2) {
        const WeatherReport report = weather_report(world, 360.0 * 1000 + 150.0 + d + 0.5);
        for (const WeatherSystem& s : report.systems) {
            if (s.wind_m_s < 34.0) continue;
            const double to_wall = (s.core_radius_m / radius) * 180.0 / kPi;
            const double wall_latitude = s.latitude_deg + (s.latitude_deg > 0.0 ? -to_wall : to_wall);
            const WeatherSample centre = weather_sample(world, report, s.latitude_deg, s.longitude_deg);
            const WeatherSample wall = weather_sample(world, report, wall_latitude, s.longitude_deg);
            check::is_true(wall.intensity_pct > 99.0, s.id + "'s wall is at the top of the scale");
            check::is_true(centre.intensity_pct < 50.0, s.id + "'s middle is calm: " + std::to_string(centre.intensity_pct));
            ++checked;
        }
    }
    check::is_true(checked > 0, "premise: a system past hurricane force in a season");
}

// ---- thunderstorms ---------------------------------------------------------------------------

TEST(weather_thunderstorms_come_with_the_afternoon_over_land_steadily_at_sea) {
    const World world = world_at();
    const WeatherInputs in = weather_inputs(world);
    const Vec3 p = point(10.0, 20.0);
    const double day = std::floor(mid_month(6));
    const double afternoon = weather_convection(in, land(0.9, 26.0), p, day + 9.0 / 24.0).potential;
    const double night = weather_convection(in, land(0.9, 26.0), p, day + 21.0 / 24.0).potential;
    check::is_true(afternoon > 3.0 * night, "afternoon " + std::to_string(afternoon) + " against night " +
                                                std::to_string(night));
    check::close(weather_convection(in, sea(28.0), p, day + 9.0 / 24.0).potential,
                 weather_convection(in, sea(28.0), p, day + 21.0 / 24.0).potential, 1e-12, "the sea by day and night");
    check::is_true(weather_convection(in, land(0.9, 26.0), p, day + 9.0 / 24.0).potential >
                       weather_convection(in, land(0.1, 26.0), p, day + 9.0 / 24.0).potential,
                   "wet air storms more than dry");
    check::is_true(weather_convection(in, land(0.9, 26.0), p, day + 9.0 / 24.0).potential >
                       weather_convection(in, land(0.9, 8.0), p, day + 9.0 / 24.0).potential,
                   "warm air storms more than cold");
}

TEST(weather_tornado_needs_heat_moisture_and_contrast_together_over_land) {
    const World world = world_at();
    const WeatherInputs in = weather_inputs(world);
    const Vec3 p = point(30.0, 20.0);
    const double afternoon = std::floor(mid_month(6)) + 9.0 / 24.0;
    const double all = weather_convection(in, land(0.8, 29.0, 0.6), p, afternoon).potential;
    check::is_true(all > 0.95, "heat, moisture and contrast over land reach the top of the scale: " + std::to_string(all));
    check::is_true(weather_convection(in, land(0.8, 29.0, 0.1), p, afternoon).potential < 0.61,
                   "without contrast, a thunderstorm and no more");
    WeatherGround wet_sea = sea(29.0);
    wet_sea.moisture = 0.8;
    wet_sea.contrast = 0.6;
    check::is_true(weather_convection(in, wet_sea, p, afternoon).potential < 0.61, "over the sea, no tornado");
    check::is_true(weather_convection(in, land(0.2, 29.0, 0.6), p, afternoon).potential < 0.61,
                   "in dry air, no tornado");

    // Where the air allows one, a tornado does stand -- at the top of the scale -- but only in
    // the rarest cores; and what a point has never passes what its air could give.
    int tornadoes = 0;
    int points = 0;
    for (int i = 0; i < 20000; ++i) {
        const Vec3 q = point(-60.0 + 120.0 * ((i * 7919) % 20000) / 20000.0, -180.0 + 360.0 * ((i * 104729) % 20000) / 20000.0);
        const WeatherConvection c = weather_convection(in, land(0.8, 29.0, 0.6), q, afternoon);
        check::is_true(c.severity >= 0.0 && c.severity <= c.potential + 1e-12, "severity within potential");
        ++points;
        if (c.severity >= 0.999) ++tornadoes;
    }
    check::is_true(tornadoes > 0, "a tornado stands somewhere the air allows one");
    check::is_true(tornadoes < points / 100, "tornadoes are rare: " + std::to_string(tornadoes) + " of " +
                                                 std::to_string(points));
}

TEST(weather_gusts_run_with_the_prevailing_wind) {
    // A thunderstorm's gust is added along the belt's wind, so where a cell is the weather the
    // wind still blows the belt's way.
    const World world = world_at();
    const double day = std::floor(mid_month(6)) + 9.0 / 24.0;
    WeatherReport report = weather_report(world, day);
    report.systems.clear();
    int checked = 0;
    for (int i = 0; i < 4000 && checked < 10; ++i) {
        const double latitude = 8.0 + 17.0 * ((i * 37) % 4000) / 4000.0;
        const double longitude = -180.0 + 360.0 * ((i * 211) % 4000) / 4000.0;
        const WeatherSample s = weather_sample(world, report, latitude, longitude);
        if (s.intensity_pct < 30.0) continue;
        const BeltWind belt = weather_surface_wind(latitude, day);
        check::is_true(apart_deg(s.wind_bearing_deg, bearing(belt.east_m_s, belt.north_m_s)) < 30.0,
                       "a gust at " + std::to_string(latitude) + " runs with the trade wind");
        check::is_true(s.wind_speed_m_s > std::hypot(belt.east_m_s, belt.north_m_s), "a gust adds to the wind");
        ++checked;
    }
    check::is_true(checked > 0, "premise: a thunderstorm in the trades on a summer afternoon");
}

// ---- samples, the field, the document -------------------------------------------------------

TEST(weather_sample_blows_toward_its_bearing) {
    // With no system about and air too cold to storm, a sample is the belt's wind: at 45 N in the
    // westerlies it blows toward the northeast.
    const World world = world_at();
    WeatherReport report = weather_report(world, mid_month(0) + 0.9);
    report.systems.clear();
    const WeatherSample s = weather_sample(world, report, 45.0, 0.0);
    check::is_true(s.intensity_pct < 5.0, "premise: a quiet winter night");
    check::between(s.wind_bearing_deg, 0.0, 90.0, "the westerlies blow toward the northeast");
    check::is_true(s.wind_speed_m_s > 1.0, "premise: a breeze");
}

TEST(weather_samples_stay_on_their_scales) {
    const World world = world_at();
    int stormy = 0;
    for (int r = 0; r < 12; ++r) {
        const WeatherReport report = weather_report(world, mid_month(r) + 0.375);
        for (const WeatherSystem& s : report.systems) {
            check::between(s.intensity_pct, 0.0, 100.0, "a system's intensity");
            check::between(s.heading_deg, 0.0, 360.0, "a heading");
            check::is_true(s.radius_m > s.core_radius_m && s.core_radius_m > 0.0, "radii nest");
        }
        for (int i = 0; i < 300; ++i) {
            const WeatherSample s = weather_sample(world, report, -85.0 + 170.0 * (i % 61) / 60.0,
                                                   -180.0 + 360.0 * (i % 67) / 67.0);
            check::between(s.intensity_pct, 0.0, 100.0, "intensity");
            check::between(s.summer_pct, 0.0, 100.0, "summer");
            check::is_true(s.wind_bearing_deg >= 0.0 && s.wind_bearing_deg < 360.0, "a bearing");
            check::is_true(std::isfinite(s.wind_speed_m_s) && s.wind_speed_m_s >= 0.0, "a wind speed");
            if (s.intensity_pct >= 5.0) ++stormy;
        }
    }
    check::is_true(stormy > 0, "premise: some weather somewhere");
}

TEST(weather_field_is_the_samples_on_the_world_s_grid) {
    const World world = world_at(33);
    const WeatherReport report = weather_report(world, mid_month(7) + 0.4);
    const WeatherField field = weather_field(world, report);
    const int n = world.layers.at("height").size();
    check::equal(field.intensity_pct.size(), n, "the field is the world's size");
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            check::between(field.intensity_pct.at(x, z), 0.0, 100.0, "field intensity");
            check::between(field.summer_pct.at(x, z), 0.0, 100.0, "field summer");
        }
    }
    // A cell of the field is the sample at that cell's own point.
    for (const auto& [x, z] : {std::pair{7, 11}, std::pair{20, 16}, std::pair{3, 29}}) {
        const Vec3 p = direction(x, z, n);
        const WeatherSample s = weather_sample(world, report, latitude_of(p), longitude_of(p));
        check::close(field.intensity_pct.at(x, z), s.intensity_pct, 1e-9, "the field's intensity and the sample agree");
        check::close(field.summer_pct.at(x, z), s.summer_pct, 1e-9, "the field's summer and the sample agree");
        check::close(field.wind_speed_m_s.at(x, z), s.wind_speed_m_s, 1e-9, "the field's wind and the sample agree");
    }
}

TEST(weather_document_names_its_schema_moments_and_both_seasons) {
    const World world = world_at(33);
    const Grid& type = world.layers.at("water_type");
    const int n = type.size();
    double sea_longitude = 1000.0;
    for (int x = 0; x < n - 1 && sea_longitude > 360.0; ++x) {
        if (type.at(x, (n - 1) / 2) == 1.0) sea_longitude = longitude_of(direction(x, (n - 1) / 2, n));
    }
    check::is_true(sea_longitude < 360.0, "premise: some sea on the equator");

    std::vector<WeatherMoment> moments(2);
    moments[0].report = weather_report(world, mid_month(2) + 13.0 / 24.0);
    moments[0].samples = {weather_sample(world, moments[0].report, 0.0, sea_longitude)};
    moments[1].report = weather_report(world, mid_month(2) + 14.0 / 24.0);
    moments[1].samples = {weather_sample(world, moments[1].report, -89.5, 0.0)};
    check::close(moments[0].samples[0].summer_pct, 100.0, 1e-9, "premise: an equatorial sea in summer");
    check::close(moments[1].samples[0].summer_pct, 0.0, 1e-9, "premise: the south pole in winter");

    const std::string text = document(moments);
    check::is_true(text.rfind("{\"schema\":\"fwg.weather\",\"version\":1,\"moments\":[{", 0) == 0,
                   "the schema leads: " + text.substr(0, 60));
    check::is_true(text.find("\"summer_pct\":100,\"winter_pct\":0,") != std::string::npos, "summer, and no winter");
    check::is_true(text.find("\"summer_pct\":0,\"winter_pct\":100,") != std::string::npos, "winter, and no summer");
    check::is_true(text.find("\"hour\":13,\"month\":2,") != std::string::npos, "the first moment's hour and month");
    check::is_true(text.find("\"hour\":14,\"month\":2,") != std::string::npos, "the second moment's hour and month");
    for (const char* key : {"\"systems\":[", "\"samples\":[", "\"intensity_pct\":", "\"wind_bearing_deg\":", "\"limits\":"}) {
        check::is_true(text.find(key) != std::string::npos, std::string("the document carries ") + key);
    }
}

// ---- refusals --------------------------------------------------------------------------------

TEST(weather_read_refuses_by_naming_the_field) {
    const World world = world_at(17);
    refused([&] { (void)weather_report(world, -1.0); }, "day");
    refused([&] { (void)weather_report(world, std::nan("")); }, "day");
    refused([&] { (void)weather_report(world, 2.0 * kWeatherLastDay); }, "day");
    refused([&] { (void)weather_at(world, 10.0, 91.0, 0.0); }, "latitude");
    refused([&] { (void)weather_at(world, 10.0, 0.0, -181.0); }, "longitude");
    // A report is the caller's to build: its day is checked wherever it is read.
    WeatherReport odd = weather_report(world, 10.0);
    odd.day = std::nan("");
    refused([&] { (void)weather_sample(world, odd, 0.0, 0.0); }, "day");
    odd.day = -350.0;
    refused([&] { (void)weather_field(world, odd); }, "day");
    refused([] { weather_require_day(std::numeric_limits<double>::infinity()); }, "day");
    refused([] { weather_require_place(0.0, 180.5); }, "longitude");
    const World early = world_at(17, 0.0, 1.0, 8);
    refused([&] { (void)weather_report(early, 10.0); }, "world.seasonal_months");
}
