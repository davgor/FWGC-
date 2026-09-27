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
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
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

constexpr std::size_t kSkyCells = static_cast<std::size_t>(kSkyRows) * kSkyCols;

// A sky ground the same in every cell and every month: open water or land, at one temperature
// and wetness, flat and free of ice.
WeatherSkyGround even_ground(double water, double temperature_c, double moisture) {
    WeatherSkyGround g;
    g.water.assign(kSkyCells, water);
    g.moisture.assign(kSkyCells, moisture);
    g.rise_east.assign(kSkyCells, 0.0);
    g.rise_north.assign(kSkyCells, 0.0);
    for (auto& t : g.temperature) t.assign(kSkyCells, temperature_c);
    for (auto& i : g.ice) i.assign(kSkyCells, 0.0);
    return g;
}

std::size_t sky_index(int row, int col) {
    return static_cast<std::size_t>(row) * static_cast<std::size_t>(kSkyCols) + static_cast<std::size_t>(col);
}

// The row of the sky holding a latitude, and a row's middle latitude, worked out here.
int sky_row_of(double latitude_deg) { return static_cast<int>((90.0 - latitude_deg) * kSkyRows / 180.0); }
double sky_row_latitude(int row) { return 90.0 - (row + 0.5) * 180.0 / kSkyRows; }
double sky_col_longitude(int col) { return -180.0 + (col + 0.5) * 360.0 / kSkyCols; }

// A report with no systems and a sky raining `rain_mm_h` everywhere (overcast where it rains).
void even_sky(WeatherReport& report, double rain_mm_h) {
    report.systems.clear();
    report.sky.rain_mm_h.assign(kSkyCells, rain_mm_h);
    report.sky.cloud_pct.assign(kSkyCells, rain_mm_h > 0.0 ? 100.0 : 0.0);
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
    const WeatherSky sky = weather_report(world, day).sky;
    for (const double other : {day - 40.0, day + 3.5, 0.0, day + 900.0, day - 0.5}) (void)weather_report(world, other);
    const World copy = world;
    check::equal(document(world, day), first, "the same moment asked again, after other moments");
    check::equal(document(copy, day), first, "the same moment on a copy of the world");
    // The whole sky too, not only the two places the document samples.
    for (const World* w : {&world, &copy}) {
        const WeatherSky again = weather_report(*w, day).sky;
        check::is_true(again.rain_mm_h == sky.rain_mm_h && again.cloud_pct == sky.cloud_pct, "the same sky asked again");
    }
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
    even_sky(report, 0.0);
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

    // The sky's own rain stops in the eye too: a downpour everywhere, and the eye still calm.
    even_sky(report, 40.0);
    report.systems = {synthetic(latitude, 60.0, 800.0, 4000.0)};
    const double downpour = weather_sample(world, report, latitude + 60.0, 0.0).intensity_pct;
    check::is_true(downpour >= 55.0, "premise: the sky's downpour reads heavy outside the system: " + std::to_string(downpour));
    const WeatherSample eye = weather_sample(world, report, latitude, 0.0);
    check::close(eye.intensity_pct, centre.intensity_pct, 1e-9, "the eye under a raining sky reads as the eye under a clear one");
    // And the eye's sky clears: no cloud at its very middle, overcast at its wall.
    check::close(eye.cloud_pct, 0.0, 1e-9, "a clear eye, though the sky around it is raining");
    check::close(weather_sample(world, report, latitude + to_wall, 0.0).cloud_pct, 100.0, 1e-9, "an overcast wall");
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

// ---- the sky ---------------------------------------------------------------------------------

TEST(weather_rain_reads_on_the_named_scale_and_warm_air_holds_more) {
    check::close(weather_rain_pct(0.0), 0.0, 1e-12, "no rain");
    check::close(weather_rain_pct(0.5), 5.0, 1e-12, "light rain");
    check::close(weather_rain_pct(2.5), 15.0, 1e-12, "moderate from 2.5 mm an hour");
    check::close(weather_rain_pct(7.6), 30.0, 1e-12, "heavy from 7.6 mm an hour");
    check::close(weather_rain_pct(20.0), 50.0, 1e-12, "a thunderstorm's downpour");
    check::close(weather_rain_pct(50.0), 65.0, 1e-12, "violent");
    check::close(weather_rain_pct(500.0), 65.0, 1e-12, "and no further on rain alone");
    for (double r = 0.1; r < 60.0; r += 0.7) {
        check::is_true(weather_rain_pct(r + 0.7) >= weather_rain_pct(r), "rising with the rain at " + std::to_string(r));
    }
    check::close(weather_capacity_mm(15.0), 25.0, 1e-12, "25 mm of water held at 15 C");
    for (double t = -20.0; t < 35.0; t += 5.0) {
        check::between(weather_capacity_mm(t + 1.0) / weather_capacity_mm(t), 1.06, 1.07,
                       "about 6.5% more a degree at " + std::to_string(t));
    }
}

TEST(weather_sky_left_in_place_builds_until_it_rains_then_comes_back_down) {
    // Warm sea everywhere, and one cell watched where the trade winds meet: its water climbs while
    // it is dry, a burst that rains hardest first takes it back down to about where every burst
    // ends, and it fills again.
    const WeatherSkyGround g = even_ground(1.0, 27.0, 0.6);
    WeatherSkyState s = weather_sky_start(g, static_cast<std::int64_t>(mid_month(3) * 24.0), 42);
    const std::size_t c = sky_index(sky_row_of(1.0), 30);
    std::vector<double> began;  // the cell's share of what it can hold as each burst began
    std::vector<double> ended;  // and as each ended
    std::vector<std::vector<double>> bursts;
    bool raining = false;
    for (int h = 0; h < 2000; ++h) {
        const double share = s.water_mm[c] / s.capacity_mm[c];
        step_weather_sky(g, s, 42);
        const double rain = s.rain_mm_h[c];
        if (rain > 0.0 && !raining) {
            bursts.emplace_back();
            began.push_back(share);
        }
        if (rain > 0.0) bursts.back().push_back(rain);
        if (!(rain > 0.0) && raining) ended.push_back(share);
        raining = rain > 0.0;
    }
    check::is_true(ended.size() >= 5, "burst after burst: " + std::to_string(ended.size()));
    const auto [low, high] = std::minmax_element(ended.begin(), ended.end());
    check::is_true(*high - *low < 0.05, "every burst ends about where the others do: " + std::to_string(*low) + " to " +
                                            std::to_string(*high));
    for (std::size_t i = 0; i < ended.size(); ++i) {
        check::is_true(began[i] > ended[i] + 0.2, "burst " + std::to_string(i) + " began fuller than it ended: " +
                                                      std::to_string(began[i]) + " then " + std::to_string(ended[i]));
        if (i > 0) check::is_true(began[i] > ended[i - 1] + 0.2, "and the cell filled again before burst " + std::to_string(i));
        const std::vector<double>& b = bursts[i];
        check::is_true(b.front() >= *std::max_element(b.begin(), b.end()), "burst " + std::to_string(i) + " rains hardest first");
    }
}

TEST(weather_sky_cloud_rides_the_belts) {
    // A patch of extra cloud, too thin to rain, is carried on the wind: east in the westerlies,
    // west in the trades, as far as the belt's wind goes in that time as a share of the planet.
    const WeatherSkyGround g = even_ground(0.0, 15.0, 0.6);
    const auto start = static_cast<std::int64_t>(mid_month(3) * 24.0);
    const int hours = 72;
    for (const double latitude : {45.0, 12.0}) {
        const int row = sky_row_of(latitude);
        const std::size_t at = sky_index(row, 40);
        WeatherSkyState base = weather_sky_start(g, start, 42);
        for (std::size_t c = 0; c < kSkyCells; ++c) base.water_mm[c] = 0.2 * base.capacity_mm[c];
        WeatherSkyState patch = base;
        const double added = 0.3 * patch.capacity_mm[at];
        patch.water_mm[at] += added;
        int wet = 0;
        for (int h = 0; h < hours; ++h) {
            step_weather_sky(g, base, 42);
            step_weather_sky(g, patch, 42);
            for (std::size_t c = 0; c < kSkyCells; ++c) wet += base.rain_mm_h[c] > 0.0 || patch.rain_mm_h[c] > 0.0;
        }
        check::equal(wet, 0, "premise: too thin to rain anywhere");
        // The patch's middle: the circular mean of its extra water's longitude, and the mean of
        // its row (rows counted southward).
        double sx = 0.0, sy = 0.0, rows = 0.0, extra = 0.0;
        for (int r = 0; r < kSkyRows; ++r) {
            for (int col = 0; col < kSkyCols; ++col) {
                const double d = patch.water_mm[sky_index(r, col)] - base.water_mm[sky_index(r, col)];
                const double a = 2.0 * kPi * col / kSkyCols;
                sx += d * std::cos(a);
                sy += d * std::sin(a);
                rows += d * r;
                extra += d;
            }
        }
        double moved = std::atan2(sy, sx) / (2.0 * kPi) * kSkyCols - 40.0;
        moved = std::remainder(moved, static_cast<double>(kSkyCols));
        const double moved_north = row - rows / extra;
        const BeltWind wind = weather_surface_wind(sky_row_latitude(row), static_cast<double>(start) / 24.0);
        const double earth_radius = kEarthCircumferenceM / (2.0 * kPi);
        const double metres = hours * 3600.0;
        const double expected = wind.east_m_s * metres /
                                (earth_radius * std::cos(sky_row_latitude(row) * kPi / 180.0)) / (2.0 * kPi / kSkyCols);
        const double expected_north = wind.north_m_s * metres / earth_radius / (kPi / kSkyRows);
        check::is_true(std::abs(expected) > 3.0 && std::abs(expected_north) > 1.0,
                       "premise: a belt wind that carries the patch several cells along and one or more across");
        check::close(moved, expected, 0.2 * std::abs(expected),
                     "carried at " + std::to_string(latitude) + ": " + std::to_string(moved) + " cells east");
        check::close(moved_north, expected_north, 0.3 * std::abs(expected_north),
                     "and " + std::to_string(moved_north) + " rows north");
        check::between(extra / added, 0.7, 1.3, "the cloud is carried, not made or lost, at " + std::to_string(latitude));
    }
}

TEST(weather_sky_gathers_cloud_where_the_winds_meet_and_thins_it_where_they_part) {
    // Warm sea everywhere, the same share of cloud in every cell to begin with, fed alike: two
    // days on, the air over the equator, where the trade winds meet, holds far more than the air
    // at thirty degrees, where the flow parts.
    const WeatherSkyGround g = even_ground(1.0, 25.0, 0.5);
    WeatherSkyState s = weather_sky_start(g, static_cast<std::int64_t>(mid_month(3) * 24.0), 42);
    for (std::size_t c = 0; c < kSkyCells; ++c) s.water_mm[c] = 0.3 * s.capacity_mm[c];
    int wet = 0;
    for (int h = 0; h < 48; ++h) {
        step_weather_sky(g, s, 42);
        for (std::size_t c = 0; c < kSkyCells; ++c) wet += s.rain_mm_h[c] > 0.0;
    }
    check::equal(wet, 0, "premise: too thin to rain anywhere");
    const auto rows_mean = [&](std::initializer_list<double> latitudes) {
        double total = 0.0;
        int count = 0;
        for (const double latitude : latitudes) {
            for (int col = 0; col < kSkyCols; ++col) {
                total += s.water_mm[sky_index(sky_row_of(latitude), col)];
                ++count;
            }
        }
        return total / count;
    };
    const double meeting = rows_mean({1.0, -1.0});
    const double parting = rows_mean({29.0, 31.0, -29.0, -31.0});
    check::is_true(meeting > 1.5 * parting, "gathered where the winds meet: " + std::to_string(meeting) + " mm against " +
                                                std::to_string(parting));
}

TEST(weather_sky_a_burst_breaking_amid_cloud_draws_it_in_and_rains_harder) {
    // Cloud carried together rains out together. The same cell breaking into a burst at the
    // same fullness, once ringed by cloud near full and once by thin cloud: amid cloud it draws
    // that cloud in and rains far harder, and the cloud around it thins. Fullness as real runs
    // have it at a burst's start (about full); the cell is one whose hour's draw breaks it there.
    const WeatherSkyGround g = even_ground(1.0, 25.0, 0.5);
    const WeatherSkyState start = weather_sky_start(g, kYearStart, 42);
    const int row = sky_row_of(12.0);
    const auto ringed = [&](int col, double own, double around) {
        WeatherSkyState s = start;
        for (int dr = -1; dr <= 1; ++dr) {
            for (int dc = -1; dc <= 1; ++dc) {
                const std::size_t n = sky_index(row + dr, (col + dc + kSkyCols) % kSkyCols);
                s.water_mm[n] = (dr == 0 && dc == 0 ? own : around) * s.capacity_mm[n];
            }
        }
        step_weather_sky(g, s, 42);
        return s;
    };
    int col = -1;
    for (int k = 0; k < kSkyCols && col < 0; ++k) {
        if (ringed(k, 1.05, 0.6).rain_mm_h[sky_index(row, k)] > 0.0) col = k;
    }
    check::is_true(col >= 0, "premise: a cell whose hour's draw breaks it at about full");
    if (col < 0) return;
    const std::size_t c = sky_index(row, col);
    const WeatherSkyState thin = ringed(col, 1.05, 0.6);
    const WeatherSkyState cloudy = ringed(col, 1.05, 0.85);
    const WeatherSkyState quiet = ringed(col, 0.5, 0.85);  // the same cloud around a cell that stays dry
    check::is_true(cloudy.rain_mm_h[c] > 1.5 * thin.rain_mm_h[c], "amid cloud it rains far harder: " +
                                                                      std::to_string(cloudy.rain_mm_h[c]) + " against " +
                                                                      std::to_string(thin.rain_mm_h[c]) + " mm");
    check::is_true(quiet.rain_mm_h[c] == 0.0, "premise: the dry cell does not break");
    // The neighbours upwind of it in the trades (east and north), whose own water the wind does
    // not bring from the cell: the burst drew their cloud in.
    const BeltWind wind = weather_surface_wind(sky_row_latitude(row), static_cast<double>(kYearStart) / 24.0);
    check::is_true(wind.east_m_s < 0.0 && wind.north_m_s < 0.0, "premise: a wind from the northeast");
    for (const std::size_t n : {sky_index(row, (col + 1) % kSkyCols), sky_index(row - 1, col)}) {
        check::is_true(cloudy.water_mm[n] < quiet.water_mm[n], "the burst drew in its neighbour's cloud");
    }
}

TEST(weather_sky_bursts_breaking_amid_cloud_rain_hardest_in_a_real_run) {
    // Over a month of a generated world's sky, the bursts that broke amid the most cloud rained
    // harder in their first hour, for what their air holds, than those that broke amid the least.
    const World world = world_at(33);
    const WeatherSkyGround g = weather_sky_ground(weather_inputs(world));
    WeatherSkyState s = weather_sky_start(g, kYearStart, world.config.seed);
    struct Start {
        double around;
        double first;
    };
    std::vector<Start> starts;
    std::vector<int> run(kSkyCells, 0);
    std::vector<int> lengths;  // of the bursts that end within the month
    for (int h = 0; h < 30 * 24; ++h) {
        const WeatherSkyState before = s;
        step_weather_sky(g, s, world.config.seed);
        for (std::size_t c = 0; c < kSkyCells; ++c) {
            if (s.rain_mm_h[c] > 0.0) {
                ++run[c];
            } else if (run[c] > 0) {
                if (h >= 168) lengths.push_back(run[c]);
                run[c] = 0;
            }
        }
        if (h < 168) continue;
        for (int row = 1; row < kSkyRows - 1; ++row) {
            for (int col = 0; col < kSkyCols; ++col) {
                const std::size_t c = sky_index(row, col);
                if (!(s.rain_mm_h[c] > 0.0) || before.raining[c] != 0) continue;
                double around = 0.0;
                for (int dr = -1; dr <= 1; ++dr) {
                    for (int dc = -1; dc <= 1; ++dc) {
                        if (dr == 0 && dc == 0) continue;
                        const std::size_t n = sky_index(row + dr, (col + dc + kSkyCols) % kSkyCols);
                        around += before.water_mm[n] / before.capacity_mm[n] / 8.0;
                    }
                }
                starts.push_back({around, s.rain_mm_h[c] / s.capacity_mm[c]});
            }
        }
    }
    check::is_true(starts.size() > 1000, "premise: bursts enough: " + std::to_string(starts.size()));
    std::sort(starts.begin(), starts.end(), [](const Start& a, const Start& b) { return a.around < b.around; });
    const std::size_t third = starts.size() / 3;
    double clearest = 0.0, cloudiest = 0.0;
    for (std::size_t i = 0; i < third; ++i) {
        clearest += starts[i].first;
        cloudiest += starts[starts.size() - 1 - i].first;
    }
    check::is_true(cloudiest > 1.15 * clearest, "amid cloud, harder first hours: " + std::to_string(cloudiest / third) +
                                                   " against " + std::to_string(clearest / third) + " of what the air holds");
    // A burst draws its neighbours in as it breaks, not all through: bursts last hours, not days.
    std::sort(lengths.begin(), lengths.end());
    check::is_true(!lengths.empty() && lengths[lengths.size() / 2] <= 6,
                   "the middle burst lasts hours: " + std::to_string(lengths.empty() ? -1 : lengths[lengths.size() / 2]));
}

TEST(weather_sky_before_founding_day_rains_like_any_other_time) {
    // The first week of the world's clock reads a run anchored before hour 0. Its hours before
    // the clock starts rain as any run's do: no cell holds its water past its time and lets it all
    // go at once.
    const World world = world_at(33);
    const WeatherInputs in = weather_inputs(world);
    const WeatherSkyGround g = weather_sky_ground(in);
    const std::int64_t first = weather_sky_anchor(0);
    check::is_true(first < 0, "premise: the first week reads a run anchored before hour 0");
    const auto rained = [&](std::int64_t anchor) {
        WeatherSkyState s = weather_sky_start(g, anchor, in.seed);
        long long hours = 0;
        double hardest = 0.0;
        for (int h = 0; h < kSkyAnchorHours; ++h) {
            step_weather_sky(g, s, in.seed);
            for (const double r : s.rain_mm_h) {
                hours += r > 0.0;
                hardest = std::max(hardest, r);
            }
        }
        return std::pair{hours, hardest};
    };
    const auto [before, hardest_before] = rained(first);
    const auto [later, hardest_later] = rained(weather_sky_anchor(kYearStart));
    check::between(static_cast<double>(before) / static_cast<double>(later), 0.5, 2.0,
                   "as much rain before the clock starts: " + std::to_string(before) + " against " + std::to_string(later) +
                       " cell-hours");
    check::is_true(hardest_before < 2.0 * hardest_later + 10.0, "and no deluge: " + std::to_string(hardest_before) + " mm/h");
    for (const double day : {0.0, 0.5, 3.25, 6.9}) {
        const WeatherSky sky = weather_sky(in, day);
        int raining = 0;
        for (const double r : sky.rain_mm_h) raining += r > 0.0;
        check::is_true(raining < static_cast<int>(kSkyCells) / 8, "day " + std::to_string(day) + ": " +
                                                                      std::to_string(raining) + " cells raining");
        check::is_true(*std::max_element(sky.rain_mm_h.begin(), sky.rain_mm_h.end()) < 100.0,
                       "day " + std::to_string(day) + ": no cell raining 100 mm an hour");
    }
}

TEST(weather_sky_air_driven_up_a_slope_rains_sooner) {
    // The same land twice, flat and rising to the east everywhere. Where the westerlies blow east
    // the air is driven up the slope and rains more often; where the trades blow west it runs
    // down the slope, and rains as it would over the flat.
    const WeatherSkyGround flat = even_ground(0.0, 20.0, 0.6);
    WeatherSkyGround sloped = flat;
    sloped.rise_east.assign(kSkyCells, 400.0);
    const auto rain_rows = [](const WeatherSkyGround& g, double from_deg, double to_deg) {
        WeatherSkyState s = weather_sky_start(g, kYearStart, 42);
        long long hours = 0;
        for (int h = 0; h < 30 * 24; ++h) {
            step_weather_sky(g, s, 42);
            if (h < 168) continue;
            for (int row = 0; row < kSkyRows; ++row) {
                const double latitude = std::abs(sky_row_latitude(row));
                if (latitude < from_deg || latitude > to_deg) continue;
                for (int col = 0; col < kSkyCols; ++col) hours += s.rain_mm_h[sky_index(row, col)] > 0.0;
            }
        }
        return static_cast<double>(hours);
    };
    const double westerlies_flat = rain_rows(flat, 38.0, 52.0);
    const double westerlies_up = rain_rows(sloped, 38.0, 52.0);
    const double trades_flat = rain_rows(flat, 8.0, 22.0);
    const double trades_down = rain_rows(sloped, 8.0, 22.0);
    check::is_true(westerlies_flat > 0.0 && trades_flat > 0.0, "premise: rain over the flat in both belts");
    check::is_true(westerlies_up > 1.3 * westerlies_flat, "driven up the slope, more rain: " + std::to_string(westerlies_up) +
                                                               " against " + std::to_string(westerlies_flat));
    check::between(trades_down / trades_flat, 0.8, 1.25, "running down it, about as much as over the flat");
}

TEST(weather_sky_showers_at_sea_break_most_about_dawn) {
    // The open sea's own daily beat: bursts break more often in the hours before sunrise, when the
    // air over the water is coolest against it, than in the middle of the day.
    const WeatherSkyGround g = even_ground(1.0, 27.0, 0.6);
    WeatherSkyState s = weather_sky_start(g, kYearStart, 42);
    std::array<int, 24> starts{};
    for (int h = 0; h < 30 * 24; ++h) {
        const WeatherSkyState before = s;
        step_weather_sky(g, s, 42);
        const auto hour_of_day = static_cast<std::size_t>((s.hour - 1) % 24);
        for (std::size_t c = 0; c < kSkyCells; ++c) starts[hour_of_day] += s.rain_mm_h[c] > 0.0 && before.raining[c] == 0;
    }
    int dawn = 0;
    int midday = 0;
    for (std::size_t h = 20; h <= 23; ++h) dawn += starts[h];
    for (std::size_t h = 5; h <= 8; ++h) midday += starts[h];
    check::is_true(dawn > 1.3 * midday, "before sunrise " + std::to_string(dawn) + " against midday " + std::to_string(midday));
}

TEST(weather_sky_parts_refuse_a_grid_of_the_wrong_size) {
    const auto refuses = [](auto&& call) {
        try {
            call();
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };
    check::is_true(refuses([] { (void)weather_sky_start(WeatherSkyGround{}, 1000, 42); }), "an empty ground");
    const WeatherSkyGround g = even_ground(1.0, 20.0, 0.5);
    WeatherSkyGround short_month = g;
    short_month.ice[7].resize(10);
    check::is_true(refuses([&] { (void)weather_sky_start(short_month, 1000, 42); }), "a month short of cells");
    WeatherSkyState s = weather_sky_start(g, 1000, 42);
    s.raining.resize(10);
    check::is_true(refuses([&] { step_weather_sky(g, s, 42); }), "a state short of cells");
    WeatherSkyState whole = weather_sky_start(g, 1000, 42);
    check::is_true(refuses([&] { step_weather_sky(short_month, whole, 42); }), "stepped on a ground short of cells");
}

TEST(weather_sky_rains_where_the_world_s_climate_is_wet_and_never_over_desert) {
    // The same warm land twice, once as wet as a forest and once a desert: the forest's sky rains
    // and carries cloud, the desert's never rains and carries a fraction of the cloud.
    std::array<int, 2> rain_hours{};
    std::array<double, 2> cloud{};
    for (int k = 0; k < 2; ++k) {
        const WeatherSkyGround g = even_ground(0.0, 25.0, k == 0 ? 0.8 : 0.05);
        WeatherSkyState s = weather_sky_start(g, kYearStart, 42);
        if (k == 1) {
            // A run opens with its deserts already clear, not with cloud that takes days to thin.
            for (std::size_t c = 0; c < kSkyCells; c += 97) {
                check::is_true(s.water_mm[c] < 0.05 * s.capacity_mm[c], "a desert cell opens clear");
            }
        }
        for (int h = 0; h < 600; ++h) {
            step_weather_sky(g, s, 42);
            if (h < 120) continue;
            for (std::size_t c = 0; c < kSkyCells; ++c) {
                rain_hours[static_cast<std::size_t>(k)] += s.rain_mm_h[c] > 0.0;
                cloud[static_cast<std::size_t>(k)] += s.water_mm[c] / s.capacity_mm[c];
            }
        }
    }
    check::is_true(rain_hours[0] > 1000, "the forest's sky rains: " + std::to_string(rain_hours[0]) + " cell-hours");
    check::equal(rain_hours[1], 0, "the desert's never does");
    check::is_true(cloud[1] < 0.2 * cloud[0], "the desert's air carries a fraction of the forest's cloud");

    // On a generated world, the driest tenth of the land rains far less than the wettest tenth.
    const World world = world_at(33);
    const WeatherSkyGround g = weather_sky_ground(weather_inputs(world));
    WeatherSkyState s = weather_sky_start(g, kYearStart, world.config.seed);
    std::vector<int> hours_raining(kSkyCells, 0);
    for (int h = 0; h < 30 * 24; ++h) {
        step_weather_sky(g, s, world.config.seed);
        if (h < 168) continue;
        for (std::size_t c = 0; c < kSkyCells; ++c) hours_raining[c] += s.rain_mm_h[c] > 0.0;
    }
    std::vector<std::pair<double, std::size_t>> land;
    for (std::size_t c = 0; c < kSkyCells; ++c) {
        if (g.water[c] < 0.1) land.push_back({g.moisture[c], c});
    }
    std::sort(land.begin(), land.end());
    const std::size_t tenth = land.size() / 10;
    check::is_true(tenth >= 20, "premise: land enough to split into tenths");
    double dry = 0.0, wet = 0.0;
    for (std::size_t i = 0; i < tenth; ++i) {
        dry += hours_raining[land[i].second];
        wet += hours_raining[land[land.size() - 1 - i].second];
    }
    check::is_true(wet > 0.0 && dry < 0.2 * wet, "the driest land rains a fifth as often as the wettest or less: " +
                                                      std::to_string(dry) + " against " + std::to_string(wet));
}

TEST(weather_sky_warm_open_water_feeds_the_air) {
    // The same sea twice, open and frozen over, from the same thin cloud: after a day the open
    // sea's air holds more water, and a warm sea feeds it more than a cool one.
    const auto fed = [](double temperature_c) {
        WeatherSkyGround open = even_ground(1.0, temperature_c, 0.5);
        WeatherSkyGround frozen = open;
        for (auto& i : frozen.ice) i.assign(kSkyCells, 1.0);
        double gained = 0.0;
        for (int k = 0; k < 2; ++k) {
            const WeatherSkyGround& g = k == 0 ? open : frozen;
            WeatherSkyState s = weather_sky_start(g, kYearStart, 42);
            for (std::size_t c = 0; c < kSkyCells; ++c) s.water_mm[c] = 0.2 * s.capacity_mm[c];
            int wet = 0;
            for (int h = 0; h < 24; ++h) {
                step_weather_sky(g, s, 42);
                for (std::size_t c = 0; c < kSkyCells; ++c) wet += s.rain_mm_h[c] > 0.0;
            }
            check::equal(wet, 0, "premise: too thin to rain anywhere");
            double total = 0.0;
            for (const double w : s.water_mm) total += w;
            gained += (k == 0 ? 1.0 : -1.0) * total / static_cast<double>(kSkyCells);
        }
        return gained;
    };
    const double warm = fed(27.0);
    const double cool = fed(10.0);
    check::is_true(warm > 1.0, "open warm water fed the air a day's worth: " + std::to_string(warm) + " mm");
    check::is_true(warm > 1.5 * cool, "a warm sea feeds it more than a cool one: " + std::to_string(warm) + " against " +
                                          std::to_string(cool) + " mm");
}

TEST(weather_sky_air_the_climate_made_wet_rains_sooner) {
    // The same warm sea twice, fed alike, once under air the world's climate made wet and once
    // dry: the wet air holds less before it rains, so it rains more often and is cloudier.
    std::array<int, 2> rain_hours{};
    std::array<double, 2> share{};
    for (int k = 0; k < 2; ++k) {
        const WeatherSkyGround g = even_ground(1.0, 27.0, k == 0 ? 0.8 : 0.2);
        WeatherSkyState s = weather_sky_start(g, kYearStart, 42);
        for (int h = 0; h < 600; ++h) {
            step_weather_sky(g, s, 42);
            if (h < 168) continue;
            for (std::size_t c = 0; c < kSkyCells; ++c) {
                rain_hours[static_cast<std::size_t>(k)] += s.rain_mm_h[c] > 0.0;
                share[static_cast<std::size_t>(k)] += s.water_mm[c] / s.capacity_mm[c];
            }
        }
    }
    check::is_true(rain_hours[0] > 1.5 * rain_hours[1], "wet air rains more often: " + std::to_string(rain_hours[0]) +
                                                            " against " + std::to_string(rain_hours[1]) + " cell-hours");
    check::is_true(share[0] > share[1], "and holds its water nearer to full");
}

TEST(weather_sky_land_showers_start_in_the_afternoon) {
    // Wet land heated through the day turns over in the afternoon: bursts start far more often
    // from late morning to evening than through the night.
    const WeatherSkyGround g = even_ground(0.0, 25.0, 0.8);
    WeatherSkyState s = weather_sky_start(g, kYearStart, 42);
    std::array<int, 24> starts{};
    std::vector<char> was(kSkyCells, 0);
    for (int h = 0; h < 30 * 24; ++h) {
        step_weather_sky(g, s, 42);
        const auto hour_of_day = static_cast<std::size_t>((s.hour - 1) % 24);  // the hour the step rained through
        for (std::size_t c = 0; c < kSkyCells; ++c) {
            const bool now = s.rain_mm_h[c] > 0.0;
            if (now && !was[c]) ++starts[hour_of_day];
            was[c] = now ? 1 : 0;
        }
    }
    int day = 0;
    int night = 0;
    for (std::size_t h = 4; h <= 10; ++h) day += starts[h];
    for (std::size_t h = 15; h <= 21; ++h) night += starts[h];
    check::is_true(day > 3 * night, "afternoon starts " + std::to_string(day) + " against night " + std::to_string(night));
}

TEST(weather_thunderstorms_need_heavy_rain_in_warm_moist_air_heated_from_below) {
    const double afternoon = 9.0;
    const double night = 21.0;
    check::is_true(weather_rain_severity_pct(land(0.8, 27.0), 10.0, afternoon) >= 50.0,
                   "heavy rain on warm, wet land in the afternoon is a thunderstorm");
    check::close(weather_rain_severity_pct(land(0.8, 8.0), 10.0, afternoon), weather_rain_pct(10.0), 1e-12,
                 "over cold land the same rain is only rain");
    check::close(weather_rain_severity_pct(land(0.1, 27.0), 10.0, afternoon), weather_rain_pct(10.0), 1e-12,
                 "and in dry air");
    check::is_true(weather_rain_severity_pct(land(0.8, 23.0), 10.0, afternoon) >
                       weather_rain_severity_pct(land(0.8, 23.0), 10.0, night) + 5.0,
                   "land warmed by the day storms more than land cooled by the night");
    check::close(weather_rain_severity_pct(sea(28.0), 10.0, afternoon), weather_rain_severity_pct(sea(28.0), 10.0, night),
                 1e-12, "the sea by day and night");
    check::is_true(weather_rain_severity_pct(sea(28.0), 10.0, afternoon) > weather_rain_pct(10.0) &&
                       weather_rain_severity_pct(sea(28.0), 10.0, afternoon) <
                           weather_rain_severity_pct(land(0.8, 28.0), 10.0, afternoon),
                   "the warm sea storms, but less than warm land");
    check::is_true(weather_rain_severity_pct(land(0.8, 29.0), 1.0, afternoon) < 30.0, "light rain is no thunderstorm");
    check::is_true(weather_rain_severity_pct(land(0.8, 29.0, 0.6), 12.0, afternoon) >= 75.0,
                   "heat, moisture and contrast together over land make it severe");
    check::is_true(weather_rain_severity_pct(land(0.8, 29.0, 0.1), 12.0, afternoon) < 70.0,
                   "without contrast, a thunderstorm and no more");
    check::equal(weather_rain_severity_pct(land(0.8, 29.0, 0.6), 0.0, afternoon), 0.0, "no rain, nothing");
    for (const double rain : {0.3, 5.0, 30.0, 300.0}) {
        check::between(weather_rain_severity_pct(land(0.8, 29.0, 0.6), rain, afternoon), 0.0, 100.0, "on the scale");
    }
}

TEST(weather_tornado_stands_at_its_spot_in_hard_rain_over_severe_ground) {
    // With hard rain everywhere on a summer afternoon, a tornado stands at the hour's spot of a
    // cell where the ground under it holds a severe storm's makings: never over the sea, never in
    // lighter rain, and not a quarter of a cell off its spot.
    const World world = world_at();
    const WeatherInputs in = weather_inputs(world);
    int tornadoes = 0;
    int sea_spots = 0;
    for (const double month : {6.0, 0.0}) {  // each hemisphere's summer
        const double day = std::floor(mid_month(month)) + 9.0 / 24.0;
        const auto hour = static_cast<std::int64_t>(std::llround(day * 24.0));
        WeatherReport hard = weather_report(world, day);
        even_sky(hard, 15.0);
        WeatherReport lighter = hard;
        even_sky(lighter, 8.0);
        for (int row = 0; row < kSkyRows; ++row) {
            for (int col = 0; col < kSkyCols; ++col) {
                const Vec3 spot = weather_tornado_spot(in.seed, hour, row, col);
                const double latitude = latitude_of(spot);
                const double longitude = longitude_of(spot);
                const WeatherGround ground = weather_ground(in, spot, day);
                const double here = weather_sample(world, hard, latitude, longitude).intensity_pct;
                if (ground.water >= 0.5) {
                    ++sea_spots;
                    check::is_true(here < 99.9, "no tornado over the sea");
                    continue;
                }
                if (here < 99.9) continue;
                ++tornadoes;
                WeatherGround flat = ground;
                flat.contrast = 0.0;
                check::is_true(weather_rain_severity_pct(ground, 15.0, 9.0) > weather_rain_severity_pct(flat, 15.0, 9.0),
                               "contrast is at work where a tornado stands");
                check::is_true(weather_sample(world, lighter, latitude, longitude).intensity_pct < 99.9,
                               "no tornado in lighter rain");
                const double nearer = latitude + (latitude > sky_row_latitude(row) ? -0.25 : 0.25) * 180.0 / kSkyRows;
                check::is_true(weather_sample(world, hard, nearer, longitude).intensity_pct < 99.9,
                               "no tornado a quarter of a cell off its spot");
            }
        }
    }
    check::is_true(tornadoes > 0, "premise: a tornado somewhere");
    check::is_true(sea_spots > 0, "premise: spots over the sea");
}

TEST(weather_sky_rain_covers_its_cell_at_full_strength_and_an_even_sky_reads_even) {
    const World world = world_at(33);
    const WeatherInputs in = weather_inputs(world);
    const double day = std::floor(mid_month(2)) + 22.0 / 24.0;
    WeatherReport report = weather_report(world, day);

    // Rain the same everywhere reads the same rain at every place: that rain's severity on the
    // ground there, under an overcast sky.
    even_sky(report, 6.0);
    for (int i = 0; i < 400; ++i) {
        const double latitude = -89.0 + 178.0 * ((i * 37) % 400) / 400.0;
        const double longitude = -180.0 + 360.0 * ((i * 91) % 400) / 400.0;
        const WeatherSample s = weather_sample(world, report, latitude, longitude);
        check::close(s.intensity_pct, weather_rain_severity_pct(weather_ground(in, point(latitude, longitude), day), 6.0, 22.0),
                     1e-9, "an even sky at place " + std::to_string(i));
        check::close(s.cloud_pct, 100.0, 1e-9, "overcast at place " + std::to_string(i));
    }

    // One cell raining alone: its full strength across the cell, to its corners; fading past
    // them; nothing a whole cell away.
    even_sky(report, 0.0);
    const int row = sky_row_of(20.0);
    const int col = 50;
    report.sky.rain_mm_h[sky_index(row, col)] = 6.0;
    report.sky.cloud_pct[sky_index(row, col)] = 100.0;
    const double across = 180.0 / kSkyRows;
    const double along = 360.0 / kSkyCols;
    const auto at = [&](double rows, double cols) {
        const double latitude = sky_row_latitude(row) - rows * across;
        const double longitude = sky_col_longitude(col) + cols * along;
        const WeatherSample s = weather_sample(world, report, latitude, longitude);
        return std::pair{s, weather_rain_severity_pct(weather_ground(in, point(latitude, longitude), day), 6.0, 22.0)};
    };
    for (const auto& [rows, cols] : {std::pair{0.0, 0.0}, {0.3, 0.3}, {-0.45, 0.2}, {0.5, 0.0}, {0.0, -0.5}, {0.5, 0.5}}) {
        const auto [s, full] = at(rows, cols);
        check::close(s.intensity_pct, full, 1e-9, "full strength within the cell");
        check::close(s.cloud_pct, 100.0, 1e-9, "overcast within the cell");
    }
    const auto [fading, full] = at(0.85, 0.0);
    check::is_true(fading.intensity_pct > 0.0 && fading.intensity_pct < full, "fading past the cell");
    for (const auto& [rows, cols] : {std::pair{1.0, 0.0}, {0.0, 1.05}, {-1.0, 0.3}, {0.8, 0.8}}) {
        const auto [s, unused] = at(rows, cols);
        check::close(s.intensity_pct, 0.0, 1e-12, "dry a whole cell away");
        check::close(s.cloud_pct, 0.0, 1e-12, "and clear");
    }
}

TEST(weather_sky_is_the_same_whichever_hour_asks_and_is_the_run_by_hand) {
    const World world = world_at(33);
    const WeatherInputs in = weather_inputs(world);
    const WeatherSkyGround g = weather_sky_ground(in);
    const std::int64_t hour = kYearStart + 5000;
    const std::int64_t anchor = weather_sky_anchor(hour);
    check::is_true(hour >= anchor + kSkySpinUpHours, "the hour's run has spun up by then");
    check::is_true(hour < anchor + kSkySpinUpHours + kSkyAnchorHours - kSkyBlendHours, "premise: outside a blend");

    // By hand: the run from the anchor, stepped to the hour.
    WeatherSkyState s = weather_sky_start(g, anchor, in.seed);
    while (s.hour < hour) step_weather_sky(g, s, in.seed);

    // Through the read, first thirty hours before, so the read at the hour steps on from there.
    (void)weather_sky(in, static_cast<double>(hour - 30) / 24.0);
    const WeatherSky read = weather_sky(in, static_cast<double>(hour) / 24.0);
    check::is_true(read.rain_mm_h == s.rain_mm_h, "the read is the run by hand, rain for rain");
    int raining = 0;
    for (std::size_t c = 0; c < kSkyCells; ++c) {
        check::between(read.cloud_pct[c], 0.0, 100.0, "cloud on its scale");
        if (!(s.rain_mm_h[c] > 0.0)) continue;
        ++raining;
        check::close(read.cloud_pct[c], 100.0, 0.0, "overcast where it rains");
    }
    check::is_true(raining > 20, "premise: rain somewhere");

    // Where it is not raining, cloud rises with how full the air is, from clear to overcast.
    std::vector<std::pair<double, double>> dry;  // how full, and the cloud
    for (std::size_t c = 0; c < kSkyCells; ++c) {
        if (!(s.rain_mm_h[c] > 0.0)) dry.push_back({s.water_mm[c] / s.capacity_mm[c], read.cloud_pct[c]});
    }
    std::sort(dry.begin(), dry.end());
    for (std::size_t i = 1; i < dry.size(); ++i) {
        check::is_true(dry[i].second >= dry[i - 1].second - 1e-9, "fuller air is no less cloudy");
    }
    int clear = 0;
    int overcast = 0;
    for (const auto& [full, cloud] : dry) {
        clear += cloud < 20.0;
        overcast += cloud > 80.0;
    }
    check::is_true(clear > 100 && overcast > 100, "dry cells from clear to overcast: " + std::to_string(clear) + " clear, " +
                                                      std::to_string(overcast) + " overcast");

    // Asked again, after hours far off either way, the same sky.
    (void)weather_sky(in, static_cast<double>(hour + 2000) / 24.0);
    (void)weather_sky(in, static_cast<double>(hour - 3000) / 24.0);
    const WeatherSky again = weather_sky(in, static_cast<double>(hour) / 24.0);
    check::is_true(again.rain_mm_h == read.rain_mm_h && again.cloud_pct == read.cloud_pct, "the same sky asked again");

    // Between two hours, the straight blend of them.
    const WeatherSky next = weather_sky(in, static_cast<double>(hour + 1) / 24.0);
    const WeatherSky quarter = weather_sky(in, (static_cast<double>(hour) + 0.25) / 24.0);
    for (std::size_t c = 0; c < kSkyCells; ++c) {
        check::close(quarter.rain_mm_h[c], read.rain_mm_h[c] + 0.25 * (next.rain_mm_h[c] - read.rain_mm_h[c]), 1e-6,
                     "a quarter past the hour, rain");
        check::close(quarter.cloud_pct[c], read.cloud_pct[c] + 0.25 * (next.cloud_pct[c] - read.cloud_pct[c]), 1e-6,
                     "a quarter past the hour, cloud");
    }
}

TEST(weather_sky_blends_into_the_next_run_over_the_last_day_of_each_window) {
    const World world = world_at(33);
    const WeatherInputs in = weather_inputs(world);
    const WeatherSkyGround g = weather_sky_ground(in);
    const std::int64_t anchor = weather_sky_anchor(kYearStart + 5000);
    const std::int64_t next = anchor + kSkyAnchorHours;
    const std::int64_t from = next + kSkySpinUpHours - kSkyBlendHours;
    const std::int64_t to = next + kSkySpinUpHours;
    check::equal(weather_sky_anchor(to - 1), anchor, "the window's last hour reads its own anchor's run");
    check::equal(weather_sky_anchor(to), next, "the hour after it, the next anchor's");

    WeatherSkyState a = weather_sky_start(g, anchor, in.seed);
    WeatherSkyState b = weather_sky_start(g, next, in.seed);
    while (a.hour < from) step_weather_sky(g, a, in.seed);
    while (b.hour < from) step_weather_sky(g, b, in.seed);
    int differ = 0;
    for (std::int64_t h = from; h <= to; ++h) {
        const double w = static_cast<double>(h - from) / static_cast<double>(kSkyBlendHours);
        const WeatherSky read = weather_sky(in, static_cast<double>(h) / 24.0);
        for (std::size_t c = 0; c < kSkyCells; ++c) {
            check::close(read.rain_mm_h[c], a.rain_mm_h[c] + w * (b.rain_mm_h[c] - a.rain_mm_h[c]), 1e-9,
                         "the blend at hour " + std::to_string(h - from) + " of the day");
            if (a.rain_mm_h[c] != b.rain_mm_h[c]) ++differ;
        }
        step_weather_sky(g, a, in.seed);
        step_weather_sky(g, b, in.seed);
    }
    check::is_true(differ > 0, "premise: the two runs differ, so the blend has something to blend");
}

TEST(weather_gusts_run_with_the_prevailing_wind) {
    // A downpour's gust is added along the belt's wind, so where the sky's rain is the weather the
    // wind still blows the belt's way.
    const World world = world_at();
    const double day = std::floor(mid_month(6)) + 9.0 / 24.0;
    WeatherReport report = weather_report(world, day);
    even_sky(report, 10.0);
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
    check::equal(checked, 10, "premise: heavy rain in the trades");
}

// ---- samples, the field, the document -------------------------------------------------------

TEST(weather_sample_blows_toward_its_bearing) {
    // With no system about and air too cold to storm, a sample is the belt's wind: at 45 N in the
    // westerlies it blows toward the northeast.
    const World world = world_at();
    WeatherReport report = weather_report(world, mid_month(0) + 0.9);
    even_sky(report, 0.0);
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
            check::between(s.cloud_pct, 0.0, 100.0, "cloud");
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
            check::between(field.cloud_pct.at(x, z), 0.0, 100.0, "field cloud");
            check::between(field.summer_pct.at(x, z), 0.0, 100.0, "field summer");
        }
    }
    // A cell of the field is the sample at that cell's own point.
    for (const auto& [x, z] : {std::pair{7, 11}, std::pair{20, 16}, std::pair{3, 29}}) {
        const Vec3 p = direction(x, z, n);
        const WeatherSample s = weather_sample(world, report, latitude_of(p), longitude_of(p));
        check::close(field.intensity_pct.at(x, z), s.intensity_pct, 1e-9, "the field's intensity and the sample agree");
        check::close(field.cloud_pct.at(x, z), s.cloud_pct, 1e-9, "the field's cloud and the sample agree");
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
    check::is_true(text.rfind("{\"schema\":\"fwg.weather\",\"version\":2,\"moments\":[{", 0) == 0,
                   "the schema leads: " + text.substr(0, 60));
    check::is_true(text.find("\"summer_pct\":100,\"winter_pct\":0,") != std::string::npos, "summer, and no winter");
    check::is_true(text.find("\"summer_pct\":0,\"winter_pct\":100,") != std::string::npos, "winter, and no summer");
    check::is_true(text.find("\"hour\":13,\"month\":2,") != std::string::npos, "the first moment's hour and month");
    check::is_true(text.find("\"hour\":14,\"month\":2,") != std::string::npos, "the second moment's hour and month");
    for (const char* key : {"\"systems\":[", "\"samples\":[", "\"intensity_pct\":", "\"cloud_pct\":", "\"wind_bearing_deg\":",
                            "\"limits\":"}) {
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

TEST(weather_at_a_node_is_the_weather_at_the_place_the_grid_puts_it) {
    // The node's latitude and longitude worked out here from its row and column, as the grid
    // lays them out, not from the direction the read itself goes through.
    const World world = world_at(33);
    const WeatherReport report = weather_report(world, 40.25);
    const int n = world.effective.size;
    const auto grid = sphere_grid(n, world.effective.globe_radius);
    const std::size_t count = grid->node_count();
    for (const std::size_t node : {count / 3, count / 2, (2 * count) / 3}) {
        const int x = grid->point_x[node];
        const int z = grid->point_z[node];
        check::is_true(x > 0 && x < n - 1 && z > 0 && z < n - 1, "the premise: an interior node, clear of the seam and the poles");
        const double latitude = 90.0 - 180.0 * z / (n - 1);
        const double longitude = 360.0 * x / (n - 1) - 180.0;
        const WeatherSample at_node = weather_sample_at_node(world, report, static_cast<int>(node));
        const WeatherSample at_place = weather_sample(world, report, latitude, longitude);
        check::close(at_node.latitude_deg, latitude, 1e-9, "the node's latitude");
        check::close(at_node.longitude_deg, longitude, 1e-9, "the node's longitude");
        check::close(at_node.intensity_pct, at_place.intensity_pct, 1e-9, "the weather there");
        check::close(at_node.summer_pct, at_place.summer_pct, 1e-9, "the season there");
    }
    bool refused = false;
    try {
        (void)weather_sample_at_node(world, report, static_cast<int>(count));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    check::is_true(refused, "a node outside the grid refused");
}

TEST(the_moon_is_full_on_one_day_in_each_of_its_periods) {
    const World world = world_at(33);
    const std::optional<MoonPhase> first = moon_phase_on(world, 0.0);
    check::is_true(first.has_value(), "a world with an almanac has a moon");
    if (!first.has_value()) return;
    const int period = first->synodic_days;
    check::is_true(period > 1, "the premise: a moon with a period of days");
    int full = 0;
    int last = -1;
    for (int day = 0; day < 3 * period; ++day) {
        const MoonPhase morning = *moon_phase_on(world, day);
        const MoonPhase evening = *moon_phase_on(world, day + 0.75);
        check::equal(evening.fraction, morning.fraction, "one phase all day");
        if (!morning.full) continue;
        ++full;
        check::equal(morning.fraction, 0.0, "full at the phase's zero");
        if (last >= 0) check::equal(day - last, period, "full a period apart");
        last = day;
    }
    check::equal(full, 3, "full once in each of three periods");
    check::is_true(!moon_phase_on(world_at(17, 0.0, 1.0, 8), 0.0).has_value(), "no almanac, no moon");
}
