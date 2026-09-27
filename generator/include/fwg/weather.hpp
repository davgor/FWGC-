// The world's weather, hour by hour, once the world exists.
//
// Weather is a READ of a finished world at a moment. `weather_report(world, day)` is a pure
// function of the world and the day, as the almanac is: nothing is stored on the World,
// nothing is recorded in its history, and no answer depends on an earlier call. The weather at
// a moment is the same whether it was asked first, last, every hour or once a season. This is
// the time API's binding invariant (time.hpp) carried down to a sub-day clock: every draw is
// keyed on the ABSOLUTE HOUR since founding year 0, never on a call count, so no way of cutting
// time into calls can change the sky.
//
// What it reads, all present on a world from stage 9: the twelve monthly temperature and water
// ice grids (ecology), `water_type`, `moisture`, `height`, the genesis seed and the globe
// radius. What it does NOT read is the climate stage's single `wind_bearing`: weather rides the
// planet's wind belts instead (below), so a storm track and the rain shadow the world was built
// with can disagree. That is the owner's ruling (2026-09-25), not an oversight.
//
// ---------------------------------------------------------------------------------------------
// The percentages.
//
// INTENSITY, 0..100, is one severity scale for all weather, whatever makes it. 0 is calm and
// clear, about 5 light rain, about 50 a thunderstorm, 75 a storm, and 100 a tornado or a
// hurricane: hurricane force, 33 m/s, is the top of the scale, so every hurricane's core reads
// 100 and its wind says how far past that it is. A moving system reads it off its sustained
// wind (`weather_intensity_pct`); the sky off how hard it is raining and the air it rains in
// (`weather_rain_severity_pct`). Snow or rain is not part of it: the same number is a light
// snowfall or a light rain, and the season says which. CLOUD, 0..100, is how overcast the sky
// is beside it.
//
// SUMMER, 0..100, with winter the rest, is how warm the place is at that time of year: 0 at a
// monthly mean of 0 C or colder, where precipitation falls as snow, and 100 at 20 C or warmer,
// linear between. It is ABSOLUTE, not a place's position within its own year: an island on the
// equator is summer all year and never has a winter, an ice cap never has a summer, and a cool
// coast peaks at a mild summer. It reads the month's temperature, not the hour's, so a night
// does not change the season. Mountains are colder, so a high peak can hold winter under a
// tropical sky.
//
// ---------------------------------------------------------------------------------------------
// Where weather comes from: three sources, each fed by what feeds it on Earth.
//
//  * WARM-CORE SYSTEMS (tropical cyclones) form over open sea at 26.5 C or warmer, never within
//    three degrees of the equator and seldom within five, so they follow the warm months. Each
//    grows toward the empirical maximum wind its sea temperature allows (DeMaria and Kaplan
//    1994), weakens over cooler sea, and dies over land at the published inland decay rate
//    (Kaplan and DeMaria 1995: 0.095 per hour toward a humid-land floor of 13.7 m/s, which dry
//    land lowers). One carried into cold, high-contrast latitudes becomes a cold-core system.
//  * COLD-CORE SYSTEMS (frontal cyclones) form where temperature changes fast with distance,
//    which on this world's temperature model is the mid-latitudes, most of all in winter: about
//    two a day in each hemisphere, most of them rain and a quarter of them gales. They deepen
//    for a day or two, then fill.
//  * THE SKY: cloud water over the whole planet, on a grid of its own (kSkyRows x kSkyCols, the
//    same on every size of world). Every hour it is carried on the belts' surface wind, gathered
//    in where the flow converges -- where the trade winds meet and at the polar front -- and
//    spread thin where it diverges at 30 degrees, which is why the tropics' convergence belt
//    rains and the subtropics are desert. Warm open water feeds it, and wet land by day. What
//    the air can hold rises about 6.5% a degree, and falls where the wind drives it up a slope,
//    over land in the afternoon's heat and over the sea about dawn. The world's own climate
//    steers it too: ground the climate made wet holds less before it rains, dry ground more, and
//    desert dries the air carried over it, so the sky rains where the world's biomes say it
//    does. A cell nearing what it can hold starts to rain, then rains a burst until it is down to
//    about half, and fills again: cloud that stays in one place builds until it rains and comes
//    back down. A burst draws in the cloud around it as it breaks, so cloud carried together
//    rains out together, harder the more of it there is, and past full harder still. A cell's
//    rain covers the cell and a little past it at its full strength. Heavy rain in warm, moist
//    air heated from below is a thunderstorm; where heat, moisture and contrast meet over land it
//    turns severe, and the hardest rain where they are strongest has a tornado somewhere in it.
//
// A system's kind is internal to this model: a report gives each system's position, heading,
// size and intensity, never a kind, and the sky is cloud and rain, never named showers.
//
// ---------------------------------------------------------------------------------------------
// Scale: proportional to the planet.
//
// A world is 200 to 600 km around, and a real hurricane is about a thousand km across. So every
// length and every ground speed is Earth's, times this world's circumference over Earth's. A
// system covers the share of this planet a real one covers of Earth and crosses an ocean in the
// days a real one takes, and the planet sees Earth's count of systems, so a place sees weather
// about as often as a real place does. Wind speeds AT a point are real metres per second and
// are not scaled: they are air speeds, not ground covered. Time is real: lifetimes, growth and
// decay all run in real hours.
//
// ---------------------------------------------------------------------------------------------
// The wind belts.
//
// Trade winds (0-30 degrees: from the east, bending toward the equator), westerlies (30-60:
// from the west, bending toward the pole) and polar easterlies (60-90), with calm belts between
// them. The belts follow the sun: over the year the equatorial calm moves five degrees toward
// the summer hemisphere and back, the calms at 30 and 60 degrees less (about 3.3 and 1.7), the
// poles not at all. A system rides the deeper steering flow (stronger westerlies aloft) plus
// the drift poleward and westward every cyclone has on a spinning planet, so a tropical system
// runs west, turns poleward, and recurves east into the westerlies. Cyclones turn
// counterclockwise in the north and clockwise in the south.
//
// ---------------------------------------------------------------------------------------------
// The clock.
//
// `day` is real days since founding year 0 on the world's calendar (time.hpp, astrology.hpp):
// 24-hour days, 30-day months, and hour 0 is sunrise. The calendar has no longitude, so the
// afternoon's heat comes at the same hour everywhere.
//
// The sky has a past, and a read has no stored state, so the sky at an hour is run: from an
// anchor every thirty days, read on its own only once it has run a week, with the next anchor's
// run blended in over the day before that one has. Any hour reads the same sky whichever hour asks,
// and the sky never jumps from one run to the next. Two runs never quite agree -- how full a
// cell is and whether it is in a burst carry its whole past -- so the blend is the one day a
// month when showers fade out and others fade in rather than moving on. Recent runs are remembered between
// reads (a memo keyed on the ground, the seed and the anchor), so reading hour after hour steps
// a run on by an hour instead of running a month again; a remembered run stepped on reaches
// exactly the state a fresh run would, so what a read returns never depends on what was read
// before it.
//
// Limits: a proxy, as the climate is. No fronts as lines, no pressure field, no ocean cooling
// under a system, no terrain steering, no snow depth; systems and the sky do not trade water.
// The sky's winds are the belts', steady but for the seasons, so a steep slope facing a wet
// wind can rain for weeks without a break.
#pragma once

#include <array>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/grid.hpp"
#include "fwg/vec3.hpp"

namespace fwg {

struct World;

inline constexpr int kWeatherApiVersion = 2;

// The two ends of the summer scale: a month's mean temperature, in Celsius.
inline constexpr double kWinterAtC = 0.0;
inline constexpr double kSummerAtC = 20.0;

// Earth's equatorial circumference: every length and ground speed here is Earth's scaled by
// this world's circumference over it.
inline constexpr double kEarthCircumferenceM = 40'075'017.0;

// The latest day a weather read answers: past it, a count of hours stops fitting the step index.
// Far past any clock: nearly three billion years.
inline constexpr double kWeatherLastDay = 1e12;

// The sky's grid: 3.75 degrees a cell, rows counted southward from the north pole.
inline constexpr int kSkyRows = 48;
inline constexpr int kSkyCols = 96;

// The sky's runs (the clock, above): an anchor every thirty days, each run read on its own
// only once it has run a week, and blended in over the day before (from six days of running)
// as the last day of the window before it.
inline constexpr std::int64_t kSkyAnchorHours = 720;
inline constexpr std::int64_t kSkySpinUpHours = 168;
inline constexpr std::int64_t kSkyBlendHours = 24;

// The fixed sentence a weather document carries.
inline constexpr std::string_view kWeatherLimits =
    "Weather is a pure function of the world and the moment, keyed on the absolute hour. Intensity is one "
    "severity scale: 0 calm, about 5 light rain, about 50 a thunderstorm, 75 a storm, 100 a tornado or a "
    "hurricane (hurricane force, 33 m/s, tops the scale). Summer is 0 at a monthly mean of 0 C or colder and "
    "100 at 20 C or warmer; winter is the rest. Lengths and ground speeds are Earth's scaled to this planet; "
    "wind speeds at a point are real. Systems and cloud ride trade winds, westerlies and polar easterlies, not "
    "the climate stage's single bearing; cloud builds over warm water, gathers where the winds meet and rains "
    "out in bursts.";

// ---------------------------------------------------------------------------------------------
// The report.
// ---------------------------------------------------------------------------------------------

// A moving system at a report's moment.
struct WeatherSystem {
    // "weather-<birth hour>-<n>", fixed at birth, so a system carries one id in every report it
    // appears in, whenever and in whatever order the reports were asked for.
    std::string id;
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    // At its strongest point. Past hurricane force a system's weather also clears toward its
    // middle, over the inner half of its core; that shows only in what a sample there reads.
    double intensity_pct = 0.0;
    double wind_m_s = 0.0;       // its peak sustained wind
    double radius_m = 0.0;       // where its weather ends
    double core_radius_m = 0.0;  // where its wind is strongest
    double heading_deg = 0.0;    // the bearing it moves toward, clockwise from north
    double speed_m_s = 0.0;      // over the ground, on this planet
    double age_hours = 0.0;
    double born_day = 0.0;
};

// The sky at a report's moment, per cell of its grid (row-major, rows counted southward).
struct WeatherSky {
    std::vector<double> cloud_pct;  // 0..100
    std::vector<double> rain_mm_h;  // what is falling, mm an hour
};

struct WeatherReport {
    int api_version = kWeatherApiVersion;
    double day = 0.0;
    double hour = 0.0;  // of the day, 0 <= hour < 24; hour 0 is sunrise
    int month = 0;      // of the year, 0..11
    std::vector<WeatherSystem> systems;  // in birth order
    WeatherSky sky;
};

// The weather at one place and moment.
struct WeatherSample {
    double day = 0.0;
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    double intensity_pct = 0.0;
    double cloud_pct = 0.0;         // 0..100: how overcast
    double wind_bearing_deg = 0.0;  // the bearing the air moves toward, clockwise from north
    double wind_speed_m_s = 0.0;
    double summer_pct = 0.0;        // winter is 100 - summer_pct
    // The system whose weather this is, or empty where the weather is the sky's own: its rain
    // and cloud, or a clear sky.
    std::string system;
};

// Every system alive at `day` (real days since founding year 0), and the sky then.
//
// Throws WorldTimeError (world_time.hpp), the read envelope: `day` as weather_require_day
// refuses it, and `world.<block>` when the world lacks what weather reads, which a world
// generated through stage 9 or later always has.
WeatherReport weather_report(const World& world, double day);

// The weather at one place at a report's moment. Cheap beside the report, so a report taken
// once serves any number of places. `report` must have been taken from this world. Throws
// WorldTimeError on the report's day as weather_report would, and on a place as
// weather_require_place refuses it.
WeatherSample weather_sample(const World& world, const WeatherReport& report, double latitude_deg,
                             double longitude_deg);

// weather_sample over a report taken at `day`.
WeatherSample weather_at(const World& world, double day, double latitude_deg, double longitude_deg);

// weather_sample at a terrain node of the world's own grid, at the latitude and longitude the
// grid places that node. Throws std::invalid_argument for a node outside the grid, and
// WorldTimeError as weather_sample does.
WeatherSample weather_sample_at_node(const World& world, const WeatherReport& report, int node);

// The moon on a day, by the world's almanac: how far through its phase it is (0 full, 0.5
// new), its period in days, and whether `day` is the full moon's own day, the day it is
// nearest full. Nullopt on a world with no almanac (generated through stage 8 or less).
// Throws WorldTimeError on a day weather_require_day refuses.
struct MoonPhase {
    double fraction = 0.0;
    int synodic_days = 0;
    bool full = false;
};
std::optional<MoonPhase> moon_phase_on(const World& world, double day);

// The weather at every cell of the world's own grid at a report's moment: the map of it.
struct WeatherField {
    double day = 0.0;
    Grid intensity_pct;
    Grid wind_bearing_deg;
    Grid wind_speed_m_s;
    Grid summer_pct;
    Grid cloud_pct;
};
WeatherField weather_field(const World& world, const WeatherReport& report);

// The checks every read above makes, published so a caller can make them before it spends a
// world on a read. Each throws WorldTimeError naming its field: `day` when it is not a finite
// number in 0..kWeatherLastDay; `latitude` outside -90..90; `longitude` outside -180..180.
void weather_require_day(double day);
void weather_require_place(double latitude_deg, double longitude_deg);

// One moment of the `fwg.weather` document: the systems alive then and the samples taken then,
// so every system a sample names is in its own moment.
struct WeatherMoment {
    WeatherReport report;
    std::vector<WeatherSample> samples;
};

// The `fwg.weather` document: its moments in the order given, compact.
void write_weather_json(std::ostream& out, std::span<const WeatherMoment> moments);

// ---------------------------------------------------------------------------------------------
// The model's parts. Published so the tests can drive each one on its own.
// ---------------------------------------------------------------------------------------------

// The severity of a sustained wind, 0..100, piecewise linear through: 4 m/s light rain at 5,
// 12 m/s (a tropical depression) at 25, 17 m/s (gale, a tropical storm) at 40, 21 m/s at 50,
// 27 m/s (a storm) at 75, and 33 m/s -- hurricane force -- and above at 100.
double weather_intensity_pct(double wind_m_s);
// Its inverse over 0..100: the least wind that reads as that severity.
double weather_wind_for_pct(double pct);

// Summer for a monthly mean temperature, 0..100.
double weather_summer_pct(double temperature_c);

// Wind as east and north components, in real metres per second.
struct BeltWind {
    double east_m_s = 0.0;
    double north_m_s = 0.0;
};
// The wind at the surface, and the deeper flow that steers a system, at a latitude and day.
BeltWind weather_surface_wind(double latitude_deg, double day);
BeltWind weather_steering_wind(double latitude_deg, double day);

// The grids weather reads, looked up once. Throws the read envelope's missing-block refusal.
struct WeatherInputs {
    int size = 0;
    double radius_m = 0.0;
    double sea_level_m = 0.0;
    std::uint32_t seed = 0;
    const Grid* water_type = nullptr;
    const Grid* moisture = nullptr;
    const Grid* height = nullptr;
    std::array<const Grid*, 12> temperature{};
    std::array<const Grid*, 12> ice{};
};
WeatherInputs weather_inputs(const World& world);

// What a point stands over at a moment: all the fuel this model reads.
struct WeatherGround {
    double latitude_deg = 0.0;
    double water = 0.0;          // 0..1, open water (ocean or lake) under the point
    double ocean = 0.0;          // 0..1, the ocean part of it
    double ice = 0.0;            // 0..1, ice on that water this month
    double temperature_c = 0.0;  // the month's mean, interpolated to the day; no daily swing
    double moisture = 0.0;       // 0..1, the climate's wetness
    double contrast = 0.0;       // how fast the sea-level temperature changes, C per degree of arc
};
WeatherGround weather_ground(const WeatherInputs& inputs, const Vec3& at, double day);

// The strongest sustained wind the ground can feed, m/s: a warm-core system's from sea
// temperature (and a humid-land floor), a cold-core system's from contrast.
double warm_core_support(const WeatherGround& ground);
double cold_core_support(const WeatherGround& ground);

// How strongly the ground favours a new system of each kind, 0..1.
double warm_core_genesis(const WeatherGround& ground);
double cold_core_genesis(const WeatherGround& ground);

enum class WeatherCore : int { Warm = 0, Cold };

// A tracked system at a whole hour.
struct WeatherState {
    Vec3 at;
    double wind_m_s = 0.0;
    WeatherCore core = WeatherCore::Warm;
    WeatherCore born = WeatherCore::Warm;
    double age_hours = 0.0;
    double mature_hours = 0.0;     // a cold core deepens until this age, then fills
    double transition_hours = -1;  // the age a warm core turned cold at, or -1
    double size = 0.0;             // 0..1, drawn at birth: how large within its kind's range
    bool alive = true;
};

// One hour of a system over the ground it stands on, starting at `day`. `shear` (0..1) is the
// system's own disruption at this age, which cuts what the ground can feed it. Grows or decays
// it, moves it on the belts, ages it, and says whether it lives.
WeatherState step_weather(const WeatherState& state, const WeatherGround& ground, double shear, double day,
                          double radius_m);

// A system's state the hour it is born.
struct WeatherBirth;
WeatherState weather_birth_state(const WeatherBirth& birth);

// A system's shear at an age, 0..1, from its own seed: smooth over about a day.
double weather_shear(std::uint32_t seed, double age_hours);

// A possible birth. Every hour draws a Poisson count of candidates at points uniform over the
// sphere, from a stream keyed on the absolute hour alone and consuming a fixed number of draws
// per candidate. The ground then decides which become systems, so the draws do not depend on
// the world's raster, and neither does where a system can be born.
struct WeatherCandidate {
    std::int64_t hour = 0;
    int index = 0;
    Vec3 at;
    double accept = 0.0;  // compared against the ground's favour for each kind
    double size = 0.0;
    double mature = 0.0;
    std::uint32_t shear_seed = 0;
};
std::vector<WeatherCandidate> weather_candidates(std::uint32_t seed, std::int64_t hour);

// Which kind of system the ground at a candidate starts, if any.
std::optional<WeatherCore> weather_accept(const WeatherGround& ground, double accept);

// The systems born in the hours [first_hour, last_hour], in hour and candidate order.
struct WeatherBirth {
    WeatherCandidate candidate;
    WeatherCore core = WeatherCore::Warm;
    WeatherGround ground;
};
std::vector<WeatherBirth> weather_births(const WeatherInputs& inputs, std::int64_t first_hour,
                                         std::int64_t last_hour);

// A system's states at every whole hour from its birth to `until_hour`, stopping at the first
// state that is no longer alive (which is included).
std::vector<WeatherState> weather_track(const WeatherInputs& inputs, const WeatherBirth& birth,
                                        std::int64_t until_hour);

// ---- the sky ----------------------------------------------------------------------------------

// The water air can hold, mm of precipitable water, at a temperature: 25 at 15 C, about 6.5%
// more a degree.
double weather_capacity_mm(double temperature_c);

// The severity of rain, 0..100, piecewise linear through: 0.5 mm/h (light rain) at 5, 2.5 at
// 15, 7.6 (heavy) at 30, 20 (a thunderstorm's downpour) at 50, and 50 and more at 65.
double weather_rain_pct(double rain_mm_h);

// What rain gives a place at an hour of the day (0 is sunrise), 0..100: its rain on the scale
// above, up to 25 more where the air is warm and moist and heated from below (heavy rain there
// is a thunderstorm; over the sea half as much), and up to 25 more again where heat, moisture
// and strong contrast meet over land (a severe storm). A tornado is not part of it: that stands
// at one spot in a cell (weather_tornado_spot), and a sample reads it.
double weather_rain_severity_pct(const WeatherGround& ground, double rain_mm_h, double hour_of_day);

// Where in a sky cell (its row and column) a tornado would stand during a whole hour: anywhere
// in the cell, drawn afresh each hour from a stream keyed on the hour and the cell.
Vec3 weather_tornado_spot(std::uint32_t seed, std::int64_t hour, int row, int col);

// What each cell of the sky stands over, from the world, once per read. Per cell, row-major.
struct WeatherSkyGround {
    std::vector<double> water;       // 0..1, open water
    std::vector<double> moisture;    // 0..1, the climate's wetness
    std::vector<double> rise_east;   // metres the ground rises across a cell going east
    std::vector<double> rise_north;  // and going north
    std::array<std::vector<double>, 12> temperature;  // each month's mean, C
    std::array<std::vector<double>, 12> ice;          // each month's water ice, 0..1
};
WeatherSkyGround weather_sky_ground(const WeatherInputs& inputs);

// The sky at a whole hour, per cell.
struct WeatherSkyState {
    std::int64_t hour = 0;
    std::vector<double> water_mm;        // cloud water
    std::vector<double> capacity_mm;     // what the air there could hold this hour
    std::vector<double> rain_mm_h;       // what fell this hour
    std::vector<std::uint8_t> raining;   // 1 while a burst runs, 0 while the cell fills
};

// A run's start at `hour`: none raining, and every cell somewhere between where a burst leaves
// a cell and where one can start, drawn from a stream keyed on `hour` -- or lower, at the level
// it would settle at, where the flow spreads the air out or dry ground dries it.
//
// These two take and give one value per cell of the sky's grid, and throw
// std::invalid_argument for a ground or a state of any other size.
WeatherSkyState weather_sky_start(const WeatherSkyGround& ground, std::int64_t hour, std::uint32_t seed);

// One hour of the sky: carried, gathered or spread, fed, drawn into the bursts around it, and
// rained out.
void step_weather_sky(const WeatherSkyGround& ground, WeatherSkyState& state, std::uint32_t seed);

// The anchor whose run the sky at a whole hour is read from: the latest one that has run for
// at least kSkySpinUpHours by then. The next anchor's run is blended in over the last
// kSkyBlendHours before that one has run as long.
std::int64_t weather_sky_anchor(std::int64_t hour);

// The sky at `day`, from its anchored runs, blended between them and between the hours.
WeatherSky weather_sky(const WeatherInputs& inputs, double day);

}  // namespace fwg
