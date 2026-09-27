#include "fwg/weather.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <numbers>
#include <ostream>
#include <stdexcept>
#include <string>

#include "fwg/astrology.hpp"
#include "fwg/json.hpp"
#include "fwg/noise.hpp"
#include "fwg/random.hpp"
#include "fwg/water.hpp"
#include "fwg/world.hpp"
#include "fwg/world_time.hpp"

namespace fwg {
namespace {

namespace refuse = ::fwg::request_errors;

constexpr double kPi = std::numbers::pi;
constexpr double kDegree = kPi / 180.0;
constexpr double kSecondsPerHour = 3600.0;

// ---- the intensity scale -------------------------------------------------------------------
// Sustained wind (m/s) to severity (percent). The anchors are the published thresholds a
// sustained wind is named by: a tropical depression below 17 m/s, a gale and a tropical storm
// from 17, a storm (Beaufort 10) from 25, and hurricane force from 33 -- the top of the scale,
// so every hurricane's core reads 100 whatever its category. The wind itself is published too.
constexpr double kScaleWind[] = {0.0, 4.0, 8.0, 12.0, 17.0, 21.0, 27.0, 33.0};
constexpr double kScalePct[] = {0.0, 5.0, 12.0, 25.0, 40.0, 50.0, 75.0, 100.0};
constexpr int kScalePoints = 8;

// ---- the belts -----------------------------------------------------------------------------
// Zonal and meridional amplitudes for the trades, the westerlies and the polar easterlies, in
// real m/s. The surface wind crosses toward low pressure by about a third of its speed; the
// steering flow is the deeper mean, whose westerlies are the fast ones.
constexpr double kSurfaceZonal[3] = {6.0, 7.0, 4.0};
constexpr double kSurfaceMeridional[3] = {2.0, 2.5, 1.5};
constexpr double kSteeringZonal[3] = {5.0, 11.0, 3.0};
constexpr double kSteeringMeridional[3] = {0.0, 1.5, 0.5};
// How far the belts follow the sun toward the summer hemisphere, degrees of latitude.
constexpr double kBeltShiftDeg = 5.0;
// The drift a cyclone has on a spinning planet, poleward and westward, m/s: a base, and more
// for a stronger system.
constexpr double kDriftPoleward = 2.0;
constexpr double kDriftPolewardStrong = 1.0;
constexpr double kDriftWestward = 0.5;
constexpr double kDriftWestwardStrong = 1.0;

// ---- genesis -------------------------------------------------------------------------------
// Candidates per hour over the whole planet, and the share of each kind's favour that becomes
// a system. Calibrated on generated worlds toward Earth's counts: about eighty warm-core systems
// a year (Earth has about eighty-five), and about two cold-core systems a day in each
// hemisphere's mid-latitudes, most of them rain and a quarter of them gales.
constexpr double kCandidatesPerHour = 2.0;
constexpr int kMaxCandidatesPerHour = 64;
constexpr double kWarmShare = 0.08;
constexpr double kColdShare = 0.35;

// ---- warm cores ----------------------------------------------------------------------------
constexpr double kWarmBirthWind = 10.0;
constexpr double kWarmDeathWind = 6.0;
constexpr double kWarmLifeHours = 504.0;
// Logistic growth at one e-fold in twenty hours: about ten m/s a day as a depression, faster in
// mid-life, and two to three days from depression to a major hurricane in calm air over the
// warmest sea. Only open water feeds it: over land it never grows.
constexpr double kWarmGrowthHours = 20.0;
// Over cooler sea a warm core relaxes to what it can hold with a thirty-hour time constant.
constexpr double kColdSeaDecayHours = 30.0;
// Kaplan and DeMaria (1995): over land, V(t) = Vb + (V0 - Vb) e^(-0.095 t), Vb = 26.7 kt.
constexpr double kLandDecayPerHour = 0.095;
constexpr double kLandFloorWind = 13.7;
// DeMaria and Kaplan (1994): the empirical maximum wind over a sea surface, 28.2 + 55.8
// e^(0.1813 (SST - 30)) m/s. Capped: the fit was never meant for seas far past 30 C.
constexpr double kPotentialCapWind = 90.0;
// The most a system's shear takes off what the ground can feed it, and the range a system's
// own shear is drawn from: what makes most systems fall short of what their sea allows, as
// about half of Earth's tropical storms never reach hurricane force.
constexpr double kShearCut = 0.75;
constexpr double kShearLow = 0.0;
constexpr double kShearHigh = 0.9;
constexpr double kShearSwing = 0.3;

// ---- cold cores ----------------------------------------------------------------------------
constexpr double kColdBirthWind = 8.0;
constexpr double kColdDeathWind = 5.0;
constexpr double kColdLifeHours = 240.0;
// Deepening (e-fold ten hours: a fast one is a "bomb"), relaxing to support, then filling.
constexpr double kColdGrowthHours = 10.0;
constexpr double kColdRelaxHours = 24.0;
constexpr double kFillingHours = 40.0;
constexpr double kMatureLowHours = 30.0;
constexpr double kMatureHighHours = 60.0;
// The strongest a contrast can feed a cold core: a deep mid-latitude storm.
constexpr double kFrontalWind = 40.0;
// A warm core turns cold only this far from the equator, and deepens for a day after.
constexpr double kTransitionLatitude = 25.0;
constexpr double kTransitionDeepenHours = 24.0;

// ---- sizes, in Earth kilometres; scaled to the planet where used -------------------------------
constexpr double kWarmOuterKm = 250.0;
constexpr double kWarmOuterSpreadKm = 300.0;
constexpr double kWarmCoreKm = 25.0;
constexpr double kWarmCoreSpreadKm = 55.0;
constexpr double kColdOuterKm = 900.0;
constexpr double kColdOuterSpreadKm = 700.0;
constexpr double kColdCoreShare = 0.3;
// Past hurricane force a system's weather clears toward its middle -- an eye -- over the inner
// half of its core. A property of the wind alone, so a report needs no field saying which
// systems have one.
constexpr double kEyeShare = 0.5;
constexpr double kEyeWind = 33.0;
constexpr double kEyeCalmWind = 8.0;   // what the very middle reads as

// ---- a system's wind at a point ------------------------------------------------------------
constexpr double kInflowDeg = 20.0;    // surface wind spirals in across the circles by this much
constexpr double kTaperShare = 0.35;   // the outer share of a system over which it fades out

// ---- contrast ------------------------------------------------------------------------------
constexpr double kContrastArcDeg = 2.0;
constexpr double kLapseCPerM = 0.0065;  // climate.hpp's surface_temperature lapse

// ---- the sky: cloud water, carried and rained out ------------------------------------------
// Earth's radius. The wind carries cloud an angle an hour, the same on every planet, as a system
// moves: the sky is a planet's share, like everything else here.
constexpr double kEarthRadiusM = kEarthCircumferenceM / (2.0 * kPi);
// Warm air holds more water, about 6.5% more a degree (Clausius-Clapeyron): 25 mm of
// precipitable water at 15 C, about 55 at 27 C, 6 at -7 C.
constexpr double kCapacityAt15Mm = 25.0;
constexpr double kCapacityWarmth = 0.065;
// What feeds the air, mm an hour: warm open water, and wet land by day. Earth's oceans evaporate
// about 4 mm a day at 25 C, and this sea about 5; wet land gives as much over a whole day as the
// sea does, as a rainforest does, most of it by day.
constexpr double kEvaporationSeaMm = 0.2;
constexpr double kEvaporationLandMm = 0.35;
constexpr double kEvaporationWarmth = 0.06;
constexpr double kNightEvaporation = 0.3;
// Where the belts' flow converges it gathers cloud water in, and where it diverges it spreads it
// thin: a cell's water grows or shrinks at this multiple of the flow's convergence.
constexpr double kGatherGain = 1.5;
// Air driven up a slope holds less: a rise of this much across a cell of the sky takes the most
// off what it holds. And land heated through the day turns over in the afternoon.
constexpr double kUpliftMetres = 400.0;
constexpr double kUpliftCut = 0.35;
constexpr double kAfternoonCut = 0.1;
// The open sea's own daily beat: its showers peak about dawn, when the air over the water is
// coolest against it.
constexpr double kDawnCut = 0.02;
constexpr double kDawnHour = 23.0;
// A cell starts to rain somewhere between this share of what its air can hold and a little
// past it -- the hour's own draw decides which hour. Then each hour it rains this share of what
// it holds above the drain share, harder by the square of how far past full it was, until it is
// down to the stop share, and dries: a burst that starts hard and tails off, longer where the air
// keeps being fed, and a spike where cloud piled in past full. The drain lies below the stop, so
// a burst ends -- exactly at the stop -- once what the wind carries in no longer holds the cell
// above it; a steep slope facing a steady, wet wind can rain for weeks.
constexpr double kReadyShare = 0.9;
constexpr double kReadyPast = 1.3;
constexpr double kRainRate = 0.3;
constexpr double kDrainShare = 0.3;
constexpr double kStopShare = 0.55;
// A burst draws in the cloud around it as it breaks: in its first hour a cell takes this share
// of what each of its neighbours not raining (eight; five in the rows at the poles) holds past
// its own stop share. So cloud carried together rains out together -- a burst breaking amid
// cloud rains harder than one in clear air -- and a burst draws only once, so it cannot feed on
// its neighbours for days. At most an eighth, so a cell ringed by breaking bursts gives no more
// than it has to spare.
constexpr double kInflowShare = 0.125;
// The world's own climate: ground its climate made dry holds more before it rains, and wet
// ground less, so the sky rains where the world's biomes say it does.
constexpr double kClimateHold = 1.0;
// And dry ground dries the air over it -- the sinking air that made it a desert -- so cloud the
// wind carries over a desert thins out instead of piling up and raining. Up to this share of its
// water an hour, over the driest ground, none over ground as wet as a grassland's middle (the
// world's biomes call ground under 0.3 desert).
constexpr double kDesertDrying = 0.05;
constexpr double kDesertWet = 0.1;
constexpr double kSteppeWet = 0.35;
// Cloud: none below this share of what the air holds, overcast by the next, and always while it
// rains.
constexpr double kCloudLow = 0.05;
constexpr double kCloudHigh = 0.75;
// A cell's rain and cloud stand over the cell and a little past it: at full strength out to this
// share of a cell's width from its middle, fading to nothing a whole width out. A place reads
// the most any cell around it gives it, so a downpour keeps its strength over the ground it
// covers instead of being averaged with the dry cells beside it. Past half the diagonal, so
// every place is within full reach of some cell, and a sky the same everywhere reads the same
// everywhere.
constexpr double kSkyFullReach = 0.71;
// How many runs of the sky are remembered between reads (below, "runs remembered").
constexpr std::size_t kSkyRunsKept = 8;
// Rain (mm an hour) to severity (percent): light rain under 2.5 mm/h, moderate to 7.6, heavy
// above it (the AMS classes); a thunderstorm's downpour about 20; violent past 50.
constexpr double kRainMm[] = {0.0, 0.5, 2.5, 7.6, 20.0, 50.0};
constexpr double kRainPct[] = {0.0, 5.0, 15.0, 30.0, 50.0, 65.0};
constexpr int kRainPoints = 6;
// Hour 0 is sunrise; the warm of the afternoon peaks nine hours later.
constexpr double kWarmestHour = 9.0;
constexpr double kDailySwingC = 5.0;
// A downpour in warm, moist air heated from below is a thunderstorm: heavy rain in such air adds
// up to 25 to its rain. Over land in the afternoon's heat most; over the sea half as much, as
// lightning is many times commoner over land.
constexpr double kConvectiveShare = 0.25;
constexpr double kConvectiveRainMm = 7.6;
constexpr double kSeaInstability = 0.5;
// The severe tiers of a downpour. Where heat, moisture and strong contrast meet over land, heavy
// rain adds up to 25 more (a severe storm, with hail), and somewhere in a cell raining harder
// still, where those ingredients are strongest, stands a tornado: the top of the scale.
constexpr double kSevereShare = 0.25;
constexpr double kSevereRainMm = 10.0;
constexpr double kTornadoLow = 0.5;
constexpr double kTornadoHigh = 0.9;
constexpr double kTornadoRainMm = 12.0;
constexpr double kTornadoRadiusKm = 6.0;

double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

double smoothstep(double low, double high, double v) {
    const double t = clamp01((v - low) / (high - low));
    return t * t * (3.0 - 2.0 * t);
}

double lerp(double a, double b, double t) { return a + (b - a) * t; }

double interpolate(const double* xs, const double* ys, int count, double x) {
    if (x <= xs[0]) return ys[0];
    for (int i = 1; i < count; ++i) {
        if (x <= xs[i]) return ys[i - 1] + (ys[i] - ys[i - 1]) * (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
    }
    return ys[count - 1];
}

// ---- where on the sphere ---------------------------------------------------------------------

Vec3 point_at(double latitude_deg, double longitude_deg) {
    const double lat = latitude_deg * kDegree;
    const double lon = longitude_deg * kDegree;
    return {std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon)};
}

double latitude_of(const Vec3& p) { return std::asin(clamp_unit(p.y)) / kDegree; }
double longitude_of(const Vec3& p) { return std::atan2(p.z, p.x) / kDegree; }

// East and north in the tangent plane at p, as climate.cpp resolves a bearing.
struct Basis {
    Vec3 east;
    Vec3 north;
};

Basis basis_at(const Vec3& p) {
    const double lon = std::atan2(p.z, p.x);
    const double lat = std::asin(clamp_unit(p.y));
    return {{-std::sin(lon), 0.0, std::cos(lon)},
            {-std::sin(lat) * std::cos(lon), std::cos(lat), -std::sin(lat) * std::sin(lon)}};
}

// Degrees clockwise from north, 0 <= bearing < 360.
double bearing_of(double east, double north) {
    double b = std::atan2(east, north) / kDegree;
    if (b < 0.0) b += 360.0;
    if (b >= 360.0) b -= 360.0;
    return b;
}

// p moved along the great circle its tangent displacement (metres) points along.
Vec3 travel(const Vec3& p, double east_m, double north_m, double radius_m) {
    const double distance = std::sqrt(east_m * east_m + north_m * north_m);
    if (!(distance > 0.0)) return p;
    const Basis b = basis_at(p);
    const Vec3 along = (b.east * east_m + b.north * north_m) * (1.0 / distance);
    const double angle = distance / radius_m;
    return unit(p * std::cos(angle) + along * std::sin(angle));
}

double planet_ratio(double radius_m) { return 2.0 * kPi * radius_m / kEarthCircumferenceM; }

// ---- reading the raster ----------------------------------------------------------------------

// The four cells and weights grid.hpp's `sample` blends at p, kept so one footprint serves
// every grid read at a point. A test holds this to `sample` itself.
struct Raster {
    int ix = 0;
    int iz = 0;
    int jz = 0;
    double fx = 0.0;
    double fz = 0.0;
};

Raster raster_at(const Vec3& p, int n) {
    double x = (std::atan2(p.z, p.x) + kPi) / (2.0 * kPi) * (n - 1);
    x = std::fmod(x, static_cast<double>(n - 1));
    if (x < 0.0) x += n - 1;
    double z = (kPi / 2.0 - std::asin(clamp_unit(p.y))) / kPi * (n - 1);
    z = std::max(0.0, std::min(static_cast<double>(n - 1), z));
    Raster r;
    r.ix = static_cast<int>(x);
    r.iz = static_cast<int>(z);
    r.fx = x - r.ix;
    r.fz = z - r.iz;
    r.jz = std::min(n - 1, r.iz + 1);
    return r;
}

template <typename Fn>
double bilinear_of(const Grid& g, const Raster& r, Fn fn) {
    return ((1.0 - r.fx) * fn(g.at(r.ix, r.iz)) + r.fx * fn(g.at(r.ix + 1, r.iz))) * (1.0 - r.fz) +
           ((1.0 - r.fx) * fn(g.at(r.ix, r.jz)) + r.fx * fn(g.at(r.ix + 1, r.jz))) * r.fz;
}

double bilinear(const Grid& g, const Raster& r) {
    return bilinear_of(g, r, [](double v) { return v; });
}

// The month as a real number: m at the middle of month m, where that month's grid holds. Any
// finite day, before founding year 0 included; a caller refuses a day that is not finite.
double month_position(double day) {
    double into = std::fmod(day, static_cast<double>(kDaysPerYear));
    if (into < 0.0) into += kDaysPerYear;
    double m = into / kDaysPerMonth - 0.5;
    if (m < 0.0) m += kMonthsPerYear;
    return m;
}

struct MonthBlend {
    int a = 0;
    int b = 0;
    double w = 0.0;
};

MonthBlend month_blend(double day) {
    const double m = month_position(day);
    const double whole = std::floor(m);
    MonthBlend blend;
    blend.a = static_cast<int>(whole) % kMonthsPerYear;
    blend.b = (blend.a + 1) % kMonthsPerYear;
    blend.w = m - whole;
    return blend;
}

double month_temperature(const WeatherInputs& in, const Raster& r, const MonthBlend& m) {
    return lerp(bilinear(*in.temperature[static_cast<std::size_t>(m.a)], r),
                bilinear(*in.temperature[static_cast<std::size_t>(m.b)], r), m.w);
}

// The month's temperature with the lapse above the datum put back: what the air would be at
// sea level, so a mountain range is not read as a front.
double sea_level_temperature(const WeatherInputs& in, const Vec3& p, const MonthBlend& m) {
    const Raster r = raster_at(p, in.size);
    const double sea = in.sea_level_m;
    return month_temperature(in, r, m) +
           kLapseCPerM * bilinear_of(*in.height, r, [sea](double h) { return std::max(0.0, h - sea); });
}

double contrast_at(const WeatherInputs& in, const Vec3& p, const MonthBlend& m) {
    const Basis b = basis_at(p);
    const double arc = kContrastArcDeg * kDegree;
    const double c = std::cos(arc);
    const double s = std::sin(arc);
    const auto at = [&](const Vec3& toward, double sign) {
        return sea_level_temperature(in, unit(p * c + toward * (sign * s)), m);
    };
    const double north = (at(b.north, 1.0) - at(b.north, -1.0)) / (2.0 * kContrastArcDeg);
    const double east = (at(b.east, 1.0) - at(b.east, -1.0)) / (2.0 * kContrastArcDeg);
    return std::sqrt(north * north + east * east);
}

// ---- the belts --------------------------------------------------------------------------------

// Latitude in the belts' own frame: the pattern moves with the sun, the poles stay put.
double belt_latitude(double latitude_deg, double day) {
    const double shift = kBeltShiftDeg * std::cos(2.0 * kPi * (month_position(day) - 6.0) / kMonthsPerYear);
    if (latitude_deg >= shift) return (latitude_deg - shift) * 90.0 / (90.0 - shift);
    return (latitude_deg - shift) * 90.0 / (90.0 + shift);
}

BeltWind belt(double latitude_deg, double day, const double (&zonal)[3], const double (&meridional)[3]) {
    const double lat = belt_latitude(latitude_deg, day);
    const double b = std::min(90.0, std::abs(lat));
    const int band = b < 30.0 ? 0 : (b < 60.0 ? 1 : 2);
    // Zero at the equator and at 30, 60 and 90 degrees -- the calm belts -- and a full
    // easterly, westerly and easterly between them.
    const double swing = std::sin(6.0 * b * kDegree);
    const double hemisphere = lat < 0.0 ? -1.0 : 1.0;
    return {-zonal[band] * swing, -hemisphere * meridional[band] * swing};
}

// ---- a system at a point -----------------------------------------------------------------------

struct SystemAt {
    double severity = 0.0;  // 0..1
    double cloud = 0.0;     // percent
    double calm = 0.0;      // 0..1: how far into a clearing eye; the sky's rain stops there too
    Vec3 wind;              // m/s, in the tangent plane
};

SystemAt system_at(const WeatherSystem& s, const Vec3& p, double radius_m) {
    SystemAt out;
    const Vec3 centre = point_at(s.latitude_deg, s.longitude_deg);
    const double d = great_circle_m(radius_m, centre, p);
    if (!(d < s.radius_m)) return out;
    // Overcast over the whole system but its fading edge -- and its clearing eye, below.
    out.cloud = 100.0 * clamp01((s.radius_m - d) / (0.25 * s.radius_m));

    const double core = std::max(s.core_radius_m, 1e-9);
    double wind_here = 0.0;
    double severity_wind = 0.0;
    if (d <= core) {
        // Inside the core the wind rises from the calm middle to the peak at its edge, but the
        // weather is the storm's full weight -- except past hurricane force, where it clears
        // toward the middle: an eye.
        wind_here = s.wind_m_s * d / core;
        severity_wind = s.wind_m_s;
        const double eye = s.wind_m_s >= kEyeWind ? kEyeShare * core : 0.0;
        if (d < eye) {
            const double out_of_eye = smoothstep(0.0, eye, d);
            severity_wind = lerp(kEyeCalmWind, s.wind_m_s, out_of_eye);
            out.calm = 1.0 - out_of_eye;
            out.cloud *= out_of_eye;
        }
    } else {
        // The wind falls off as one over the root of the distance, the weather -- mostly rain,
        // which gathers toward the centre -- as one over the distance.
        const double taper = clamp01((s.radius_m - d) / (kTaperShare * s.radius_m));
        wind_here = s.wind_m_s * std::sqrt(core / d) * taper;
        severity_wind = s.wind_m_s * (core / d) * taper;
    }
    out.severity = weather_intensity_pct(severity_wind) / 100.0;

    const Vec3 radial = p * dot(centre, p) - centre;  // away from the centre, in the plane at p
    const double length = std::sqrt(dot(radial, radial));
    if (length > 1e-12 && wind_here > 0.0) {
        const Vec3 outward = radial * (1.0 / length);
        // Counterclockwise seen from above in the north, clockwise in the south: on a map with
        // north up, the outward direction turned a quarter to the left. Turned through east and
        // north, NOT as cross(p, outward): this frame is left-handed -- east x north is -p, not
        // p -- so that cross product turns it the other way and every cyclone spins backwards.
        const Basis b = basis_at(p);
        Vec3 around = b.east * -dot(outward, b.north) + b.north * dot(outward, b.east);
        if (s.latitude_deg < 0.0) around = around * -1.0;
        const double inflow = kInflowDeg * kDegree;
        out.wind = (around * std::cos(inflow) - outward * std::sin(inflow)) * wind_here;
    }
    return out;
}

struct Radii {
    double outer = 0.0;
    double core = 0.0;
};

Radii radii_of(const WeatherState& s, double wind, double latitude_deg, double radius_m) {
    const double km = 1000.0 * planet_ratio(radius_m);
    // A warm core widens as it leaves the tropics; its wind tightens as it strengthens.
    const double warm_outer =
        (kWarmOuterKm + kWarmOuterSpreadKm * s.size) * (1.0 + 0.8 * smoothstep(20.0, 45.0, std::abs(latitude_deg)));
    const double warm_core = kWarmCoreKm + kWarmCoreSpreadKm * (1.0 - clamp01((wind - 17.0) / 53.0));
    Radii r;
    if (s.core == WeatherCore::Warm) {
        r.outer = warm_outer;
        r.core = warm_core;
    } else if (s.born == WeatherCore::Warm) {
        // A warm core that turned cold spreads to twice its width over a day.
        const double t = clamp01((s.age_hours - s.transition_hours) / 24.0);
        r.outer = warm_outer * (1.0 + t);
        r.core = lerp(warm_core, kColdCoreShare * r.outer, t);
    } else {
        r.outer = kColdOuterKm + kColdOuterSpreadKm * s.size;
        r.core = kColdCoreShare * r.outer;
    }
    r.outer *= km;
    r.core *= km;
    return r;
}

// ---- the clock -------------------------------------------------------------------------------

// Hours since founding year 0. A day written as h / 24 comes back a few ulps either side of h,
// and a moment a hair short of a whole hour would read the hour before it -- a different set of
// systems -- so a value within a few ulps of a whole hour IS that hour.
double hours_of(double day) {
    const double hours = day * kHoursPerDay;
    const double whole = std::round(hours);
    const double slack = 64.0 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::abs(hours));
    return std::abs(hours - whole) <= slack ? whole : hours;
}

double hour_of_day(double day) {
    const double hours = hours_of(day);
    return hours - kHoursPerDay * std::floor(hours / kHoursPerDay);
}

// Sunlight, 0..1: up at hour 0, down at hour 12.
double daylight(double hour) { return hour < 12.0 ? std::sin(kPi * hour / 12.0) : 0.0; }

// How far into the afternoon's heat the hour is, 0..1: late morning to evening.
double afternoon_heat(double hour) {
    return smoothstep(-0.1, 0.6, std::cos(2.0 * kPi * (hour - kWarmestHour) / kHoursPerDay));
}

// The air at a place at an hour of the day: land warms by day and cools by night, dry land
// most; water hardly at all.
double air_temperature(const WeatherGround& g, double hour) {
    const double land = 1.0 - g.water;
    const double daily = std::cos(2.0 * kPi * (hour - kWarmestHour) / kHoursPerDay);
    return g.temperature_c + land * kDailySwingC * (1.0 - 0.5 * g.moisture) * daily;
}

// Warm, moist air over ground that heats it, 0..1: what makes a downpour a thunderstorm.
double instability(const WeatherGround& g, double hour) {
    return smoothstep(20.0, 27.0, air_temperature(g, hour)) * smoothstep(0.3, 0.6, g.moisture) *
           ((1.0 - g.water) + kSeaInstability * g.water);
}

// Heat, moisture and strong contrast together over land, 0..1: what makes a downpour severe.
double severe_ingredients(const WeatherGround& g, double hour) {
    return (1.0 - g.water) * smoothstep(21.0, 27.0, air_temperature(g, hour)) * smoothstep(0.3, 0.5, g.contrast) *
           smoothstep(0.35, 0.65, g.moisture);
}

// ---- the sky ----------------------------------------------------------------------------------

std::size_t sky_cell(int row, int col) {
    return static_cast<std::size_t>(row) * static_cast<std::size_t>(kSkyCols) + static_cast<std::size_t>(col);
}

double sky_latitude(int row) { return 90.0 - (row + 0.5) * 180.0 / kSkyRows; }
double sky_longitude(int col) { return -180.0 + (col + 0.5) * 360.0 / kSkyCols; }

// A point's place on the sky grid, in rows (counted southward) and columns between cell centres.
std::pair<double, double> sky_position(double latitude_deg, double longitude_deg) {
    return {(90.0 - latitude_deg) * kSkyRows / 180.0 - 0.5, (longitude_deg + 180.0) * kSkyCols / 360.0 - 0.5};
}

// A sky grid read at a place between its cell centres: the most any of the four cells around
// it gives it, each at full strength within kSkyFullReach of its middle and fading out by a whole
// cell (a cell further off is a whole cell away or more, so gives nothing). Longitude wraps;
// latitude stops at the last rows.
double sky_read(const std::vector<double>& v, double row, double col) {
    row = std::max(0.0, std::min(static_cast<double>(kSkyRows - 1), row));
    col = std::fmod(col, static_cast<double>(kSkyCols));
    if (col < 0.0) col += kSkyCols;
    const int r0 = std::min(kSkyRows - 1, static_cast<int>(row));
    const int c0 = std::min(kSkyCols - 1, static_cast<int>(col));
    const double fr = row - r0;
    const double fc = col - c0;
    double most = 0.0;
    for (int dr = 0; dr <= 1; ++dr) {
        for (int dc = 0; dc <= 1; ++dc) {
            const double across = dr == 0 ? fr : 1.0 - fr;
            const double along = dc == 0 ? fc : 1.0 - fc;
            const double reach = 1.0 - smoothstep(kSkyFullReach, 1.0, std::sqrt(across * across + along * along));
            if (!(reach > 0.0)) continue;
            const int r = std::min(kSkyRows - 1, r0 + dr);
            const int c = (c0 + dc) % kSkyCols;
            most = std::max(most, reach * v[sky_cell(r, c)]);
        }
    }
    return most;
}

// How fast the belts' surface flow spreads out (positive) or gathers in (negative) at a
// latitude, per second. The flow is the same all round a latitude, so only its north-south part
// counts: it gathers where the trade winds meet and at the polar front, and spreads at 30 degrees.
double belt_divergence(double latitude_deg, double day) {
    const double north = std::min(89.9, latitude_deg + 0.5);
    const double south = std::max(-89.9, latitude_deg - 0.5);
    const double vn = weather_surface_wind(north, day).north_m_s * std::cos(north * kDegree);
    const double vs = weather_surface_wind(south, day).north_m_s * std::cos(south * kDegree);
    return (vn - vs) / ((north - south) * kDegree) /
           (kEarthRadiusM * std::max(0.05, std::cos(latitude_deg * kDegree)));
}

// The hour's clock over the whole sky.
struct SkyClock {
    double day = 0.0;
    MonthBlend month;
    double lit = 0.0;   // what share of its daytime evaporation wet land gives, 0..1
    double heat = 0.0;  // how far into the afternoon's heat, 0..1
    double dawn = 0.0;  // how near the sea's dawn, 0..1
};

SkyClock sky_clock(std::int64_t hour) {
    SkyClock k;
    k.day = static_cast<double>(hour) / kHoursPerDay;
    k.month = month_blend(k.day);
    const double clock = hour_of_day(k.day);
    k.lit = kNightEvaporation + (1.0 - kNightEvaporation) * daylight(clock);
    k.heat = afternoon_heat(clock);
    k.dawn = smoothstep(-0.1, 0.6, std::cos(2.0 * kPi * (clock - kDawnHour) / kHoursPerDay));
    return k;
}

// The belts' wind over one row of the sky: where the air arriving in the row was an hour before
// (rows r0..r1 at fr between them, and columns k..k+1 along from each column at fc), how much
// the flow gathers it in, and which way the ground must rise to lift it.
struct SkyRow {
    double gather = 1.0;
    double to_east = 0.0;
    double to_north = 0.0;
    int r0 = 0;
    int r1 = 0;
    double fr = 0.0;
    int k = 0;
    double fc = 0.0;
};

SkyRow sky_row(int row, double day) {
    const double lat = sky_latitude(row);
    const BeltWind w = weather_surface_wind(lat, day);
    SkyRow out;
    out.gather = std::exp(-kGatherGain * belt_divergence(lat, day) * kSecondsPerHour);
    const double speed = std::sqrt(w.east_m_s * w.east_m_s + w.north_m_s * w.north_m_s);
    out.to_east = speed > 0.0 ? w.east_m_s / speed : 0.0;
    out.to_north = speed > 0.0 ? w.north_m_s / speed : 0.0;
    // Air from the south arrives on a northward wind, and rows are counted southward.
    const double row_angle = kPi / kSkyRows;
    const double from = std::max(0.0, std::min(static_cast<double>(kSkyRows - 1),
                                               row + w.north_m_s * kSecondsPerHour / kEarthRadiusM / row_angle));
    out.r0 = static_cast<int>(from);
    out.r1 = std::min(kSkyRows - 1, out.r0 + 1);
    out.fr = from - out.r0;
    const double col_angle = 2.0 * kPi / kSkyCols;
    const double across = std::max(0.05, std::cos(lat * kDegree));
    double shift = std::fmod(-w.east_m_s * kSecondsPerHour / (kEarthRadiusM * across) / col_angle,
                             static_cast<double>(kSkyCols));
    if (shift < 0.0) shift += kSkyCols;
    out.k = std::min(kSkyCols - 1, static_cast<int>(shift));
    out.fc = shift - out.k;
    return out;
}

// What the air over a cell can hold this hour, mm: less where the wind drives it up a slope,
// less over land turning over in the afternoon and over the sea about dawn, and more where the
// world's climate is dry.
double sky_holds(const WeatherSkyGround& g, std::size_t c, double temperature_c, const SkyRow& row,
                 const SkyClock& clock) {
    const double wet = g.water[c];
    const double land = 1.0 - wet;
    const double rise = row.to_east * g.rise_east[c] + row.to_north * g.rise_north[c];
    return weather_capacity_mm(temperature_c) * (1.0 - kUpliftCut * clamp01(rise / kUpliftMetres)) *
           (1.0 - kAfternoonCut * land * clock.heat - kDawnCut * wet * clock.dawn) *
           (1.0 + kClimateHold * (0.5 - g.moisture[c]));
}

double sky_temperature(const WeatherSkyGround& g, std::size_t c, const MonthBlend& m) {
    return lerp(g.temperature[static_cast<std::size_t>(m.a)][c], g.temperature[static_cast<std::size_t>(m.b)][c], m.w);
}

double sky_ice(const WeatherSkyGround& g, std::size_t c, const MonthBlend& m) {
    return lerp(g.ice[static_cast<std::size_t>(m.a)][c], g.ice[static_cast<std::size_t>(m.b)][c], m.w);
}

// The share of the water over a cell that the ground under it keeps each hour: all of it, but
// over dry land.
double sky_keeps(const WeatherSkyGround& g, std::size_t c) {
    return 1.0 - kDesertDrying * (1.0 - g.water[c]) * (1.0 - smoothstep(kDesertWet, kSteppeWet, g.moisture[c]));
}

// What feeds the air over a cell in an hour, mm: warm open water most, wet land by day. `lit` is
// the share of its daytime evaporation wet land gives at that hour.
double sky_supply(const WeatherSkyGround& g, std::size_t c, double temperature_c, double ice, double lit) {
    const double warmth = std::exp(kEvaporationWarmth * (temperature_c - 25.0));
    return g.water[c] * kEvaporationSeaMm * warmth * (1.0 - ice) +
           (1.0 - g.water[c]) * kEvaporationLandMm * g.moisture[c] * warmth * lit;
}

// The sky's cloud and rain at a whole hour, per cell.
struct SkyFrame {
    std::vector<double> cloud;
    std::vector<double> rain;
};

SkyFrame frame_of(const WeatherSkyState& s) {
    SkyFrame f;
    f.rain = s.rain_mm_h;
    f.cloud.resize(s.water_mm.size());
    for (std::size_t c = 0; c < s.water_mm.size(); ++c) {
        f.cloud[c] = s.rain_mm_h[c] > 0.0
                         ? 100.0
                         : 100.0 * smoothstep(kCloudLow, kCloudHigh, s.water_mm[c] / s.capacity_mm[c]);
    }
    return f;
}

SkyFrame mix(const SkyFrame& a, const SkyFrame& b, double w) {
    SkyFrame out = a;
    for (std::size_t c = 0; c < a.rain.size(); ++c) {
        out.cloud[c] = lerp(a.cloud[c], b.cloud[c], w);
        out.rain[c] = lerp(a.rain[c], b.rain[c], w);
    }
    return out;
}

std::int64_t floor_div(std::int64_t a, std::int64_t b) {
    const std::int64_t q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

// How much of the next anchor's run an hour of this anchor's window takes: none until the
// window's last day, then rising to all of it at the window's end.
double sky_weight(std::int64_t hour, std::int64_t anchor) {
    const std::int64_t from = anchor + kSkySpinUpHours + kSkyAnchorHours - kSkyBlendHours;
    return clamp01(static_cast<double>(hour - from) / static_cast<double>(kSkyBlendHours));
}

// The sky's model parts take one value per cell of its grid, in every field; anything else would
// be read past its end, so it is refused.
void require_sky_ground(const WeatherSkyGround& g) {
    const auto cells = static_cast<std::size_t>(kSkyRows) * kSkyCols;
    bool whole = g.water.size() == cells && g.moisture.size() == cells && g.rise_east.size() == cells &&
                 g.rise_north.size() == cells;
    for (std::size_t m = 0; m < g.temperature.size(); ++m) {
        whole = whole && g.temperature[m].size() == cells && g.ice[m].size() == cells;
    }
    if (!whole) {
        throw std::invalid_argument("weather sky: a ground needs one value per cell of the " + std::to_string(kSkyRows) +
                                    " x " + std::to_string(kSkyCols) + " sky in every field and month");
    }
}

void require_sky_state(const WeatherSkyState& s) {
    const auto cells = static_cast<std::size_t>(kSkyRows) * kSkyCols;
    if (s.water_mm.size() != cells || s.capacity_mm.size() != cells || s.rain_mm_h.size() != cells ||
        s.raining.size() != cells) {
        throw std::invalid_argument("weather sky: a state needs one value per cell of the " + std::to_string(kSkyRows) +
                                    " x " + std::to_string(kSkyCols) + " sky in every field");
    }
}

// ---- runs remembered -----------------------------------------------------------------------------
// A run is a function of its ground, the seed and its anchor alone, and stepping is exact, so a
// run remembered at one hour and stepped on reaches the state a fresh run from the anchor would,
// bit for bit. Reading hour after hour then steps a run on from where the last read left it
// instead of running up to a month again. Each entry is one run's latest remembered hour; a read
// that steps an entry on moves it, and a read before every remembered hour of its run starts a
// new entry. Equal grounds are held once and matched by pointer.
struct SkyRun {
    std::shared_ptr<const WeatherSkyGround> ground;
    std::uint32_t seed = 0;
    std::int64_t anchor = 0;
    WeatherSkyState state;
};

struct SkyRuns {
    std::mutex mutex;
    std::vector<SkyRun> entries;  // bounded, most recently used first
};

SkyRuns& sky_runs() {
    static SkyRuns instance;
    return instance;
}

bool same_ground(const WeatherSkyGround& a, const WeatherSkyGround& b) {
    return a.water == b.water && a.moisture == b.moisture && a.rise_east == b.rise_east &&
           a.rise_north == b.rise_north && a.temperature == b.temperature && a.ice == b.ice;
}

// The ground as a remembered run holds it, if one was run on the same ground.
std::shared_ptr<const WeatherSkyGround> held_ground(WeatherSkyGround g) {
    SkyRuns& runs = sky_runs();
    const std::lock_guard<std::mutex> lock(runs.mutex);
    for (const SkyRun& run : runs.entries) {
        if (same_ground(*run.ground, g)) return run.ground;
    }
    return std::make_shared<const WeatherSkyGround>(std::move(g));
}

// The frames at `hour` and the hour after, of the run from `anchor`.
std::pair<SkyFrame, SkyFrame> run_sky(const std::shared_ptr<const WeatherSkyGround>& g, std::int64_t anchor,
                                      std::int64_t hour, std::uint32_t seed) {
    SkyRuns& runs = sky_runs();
    const auto same_run = [&](const SkyRun& run) {
        return run.ground == g && run.seed == seed && run.anchor == anchor;
    };
    std::optional<WeatherSkyState> from;
    {
        const std::lock_guard<std::mutex> lock(runs.mutex);
        for (const SkyRun& run : runs.entries) {
            if (!same_run(run) || run.state.hour > hour) continue;
            if (!from || run.state.hour > from->hour) from = run.state;
        }
    }
    WeatherSkyState s = from ? std::move(*from) : weather_sky_start(*g, anchor, seed);
    const std::int64_t stepped_from = s.hour;
    while (s.hour < hour) step_weather_sky(*g, s, seed);
    {
        const std::lock_guard<std::mutex> lock(runs.mutex);
        auto it = std::find_if(runs.entries.begin(), runs.entries.end(), [&](const SkyRun& run) {
            return same_run(run) && run.state.hour == stepped_from;
        });
        if (it == runs.entries.end()) {
            runs.entries.insert(runs.entries.begin(), SkyRun{g, seed, anchor, s});
            if (runs.entries.size() > kSkyRunsKept) runs.entries.resize(kSkyRunsKept);
        } else {
            it->state = s;
            std::rotate(runs.entries.begin(), it, it + 1);
        }
    }
    SkyFrame now = frame_of(s);
    step_weather_sky(*g, s, seed);
    return {std::move(now), frame_of(s)};
}

// ---- assembling ------------------------------------------------------------------------------

std::string system_id(const WeatherCandidate& c) {
    return "weather-" + std::to_string(c.hour) + "-" + std::to_string(c.index);
}

WeatherSystem describe(const WeatherBirth& birth, const WeatherState& now, const WeatherState& next, double f,
                       double radius_m) {
    const Vec3 at = f > 0.0 ? unit(now.at * (1.0 - f) + next.at * f) : now.at;
    const double wind = lerp(now.wind_m_s, next.wind_m_s, f);
    WeatherSystem s;
    s.id = system_id(birth.candidate);
    s.latitude_deg = latitude_of(at);
    s.longitude_deg = longitude_of(at);
    s.wind_m_s = wind;
    s.intensity_pct = weather_intensity_pct(wind);
    const Radii r = radii_of(now, wind, s.latitude_deg, radius_m);
    s.radius_m = r.outer;
    s.core_radius_m = r.core;
    const Basis b = basis_at(at);
    const Vec3 moved = next.at - now.at;
    s.heading_deg = bearing_of(dot(moved, b.east), dot(moved, b.north));
    s.speed_m_s = great_circle_m(radius_m, now.at, next.at) / kSecondsPerHour;
    s.age_hours = now.age_hours + f;
    s.born_day = static_cast<double>(birth.candidate.hour) / kHoursPerDay;
    return s;
}

// What the sky gives a point: its rain as severity, 0..1, with the severe tiers where the ground
// holds their ingredients, and its cloud.
struct SkyAt {
    double severity = 0.0;
    double cloud = 0.0;
};

SkyAt sky_at(const WeatherInputs& in, const WeatherSky& sky, const WeatherGround& g, const Vec3& p, double day) {
    SkyAt out;
    const auto cells = static_cast<std::size_t>(kSkyRows) * kSkyCols;
    if (sky.rain_mm_h.size() != cells || sky.cloud_pct.size() != cells) return out;
    const auto [row, col] = sky_position(g.latitude_deg, longitude_of(p));
    const double rain = std::max(0.0, sky_read(sky.rain_mm_h, row, col));
    out.cloud = std::max(0.0, std::min(100.0, sky_read(sky.cloud_pct, row, col)));
    if (!(rain > 0.0)) return out;
    const double hour = hour_of_day(day);
    out.severity = weather_rain_severity_pct(g, rain, hour) / 100.0;
    // In the cell whose rain this is, raining hard where the ingredients are strongest, a tornado,
    // at its spot for the hour.
    const double tornado = smoothstep(kTornadoLow, kTornadoHigh, severe_ingredients(g, hour));
    if (tornado > 0.0) {
        const int r = std::max(0, std::min(kSkyRows - 1, static_cast<int>(std::lround(row))));
        int c = static_cast<int>(std::lround(col)) % kSkyCols;
        if (c < 0) c += kSkyCols;
        if (sky.rain_mm_h[sky_cell(r, c)] >= kTornadoRainMm) {
            const Vec3 spot = weather_tornado_spot(in.seed, static_cast<std::int64_t>(std::floor(hours_of(day))), r, c);
            const double reach = kTornadoRadiusKm * 1000.0 * planet_ratio(in.radius_m) * tornado;
            if (great_circle_m(in.radius_m, spot, p) < reach) out.severity = 1.0;
        }
    }
    return out;
}

WeatherSample sample_at(const WeatherInputs& in, const WeatherReport& report, const Vec3& p) {
    const WeatherGround g = weather_ground(in, p, report.day);
    const Basis b = basis_at(p);
    const BeltWind surface = weather_surface_wind(g.latitude_deg, report.day);
    const Vec3 prevailing = b.east * surface.east_m_s + b.north * surface.north_m_s;

    WeatherSample out;
    out.day = report.day;
    out.latitude_deg = g.latitude_deg;
    out.longitude_deg = longitude_of(p);

    Vec3 wind = prevailing;
    double severity = 0.0;
    double cloud = 0.0;
    double calm = 0.0;
    for (const WeatherSystem& s : report.systems) {
        const SystemAt here = system_at(s, p, in.radius_m);
        wind = wind + here.wind;
        cloud = std::max(cloud, here.cloud);
        calm = std::max(calm, here.calm);
        if (here.severity > severity) {
            severity = here.severity;
            out.system = s.id;
        }
    }

    // The sky's own rain and cloud, but none of either in a hurricane's clearing eye.
    SkyAt sky = sky_at(in, report.sky, g, p, report.day);
    sky.severity *= 1.0 - calm;
    sky.cloud *= 1.0 - calm;
    cloud = std::max(cloud, sky.cloud);
    if (sky.severity > severity) {
        severity = sky.severity;
        out.system.clear();
    }
    if (sky.severity > 0.0) {
        // A downpour's gust runs with the prevailing wind.
        const double length = std::sqrt(dot(prevailing, prevailing));
        const Vec3 along = length > 1e-9 ? prevailing * (1.0 / length) : b.east;
        wind = wind + along * weather_wind_for_pct(100.0 * sky.severity);
    }

    out.intensity_pct = 100.0 * severity;
    out.cloud_pct = cloud;
    const double east = dot(wind, b.east);
    const double north = dot(wind, b.north);
    out.wind_speed_m_s = std::sqrt(east * east + north * north);
    out.wind_bearing_deg = bearing_of(east, north);
    out.summer_pct = weather_summer_pct(g.temperature_c);
    return out;
}

}  // namespace

// ---- the scale and the season ----------------------------------------------------------------

double weather_intensity_pct(double wind_m_s) {
    if (!(wind_m_s > 0.0)) return 0.0;
    return interpolate(kScaleWind, kScalePct, kScalePoints, wind_m_s);
}

double weather_wind_for_pct(double pct) {
    if (!(pct > 0.0)) return 0.0;
    return interpolate(kScalePct, kScaleWind, kScalePoints, std::min(100.0, pct));
}

double weather_summer_pct(double temperature_c) {
    return 100.0 * clamp01((temperature_c - kWinterAtC) / (kSummerAtC - kWinterAtC));
}

// ---- the belts --------------------------------------------------------------------------------

BeltWind weather_surface_wind(double latitude_deg, double day) {
    return belt(latitude_deg, day, kSurfaceZonal, kSurfaceMeridional);
}

BeltWind weather_steering_wind(double latitude_deg, double day) {
    return belt(latitude_deg, day, kSteeringZonal, kSteeringMeridional);
}

// ---- refusals --------------------------------------------------------------------------------

void weather_require_day(double day) {
    if (!std::isfinite(day)) throw refuse::wrong_type("day", refuse::render(day), "a finite number");
    if (day < 0.0 || day > kWeatherLastDay) throw refuse::out_of_range("day", day, 0.0, kWeatherLastDay, "days");
}

void weather_require_place(double latitude_deg, double longitude_deg) {
    if (!(latitude_deg >= -90.0 && latitude_deg <= 90.0)) {
        throw refuse::out_of_range("latitude", latitude_deg, -90.0, 90.0, "deg");
    }
    if (!(longitude_deg >= -180.0 && longitude_deg <= 180.0)) {
        throw refuse::out_of_range("longitude", longitude_deg, -180.0, 180.0, "deg");
    }
}

// ---- the ground ------------------------------------------------------------------------------

WeatherInputs weather_inputs(const World& world) {
    WeatherInputs in;
    const auto layer = [&world](const char* name) {
        const auto it = world.layers.find(name);
        if (it == world.layers.end() || it->second.size() < 3) {
            throw refuse::missing_block("layers." + std::string(name),
                                        "Weather reads the " + std::string(name) +
                                            " layer, which a world generated through stage 9 or later has.");
        }
        return &it->second;
    };
    in.water_type = layer("water_type");
    in.moisture = layer("moisture");
    in.height = layer("height");
    in.size = in.water_type->size();

    if (world.seasonal_months.size() != static_cast<std::size_t>(kMonthsPerYear)) {
        throw refuse::missing_block("seasonal_months",
                                    "Weather reads the twelve monthly temperature and water ice grids, which "
                                    "ecology writes at stage 9; this world has " +
                                        std::to_string(world.seasonal_months.size()) + ".");
    }
    for (std::size_t m = 0; m < in.temperature.size(); ++m) {
        in.temperature[m] = &world.seasonal_months[m].temperature;
        in.ice[m] = &world.seasonal_months[m].water_ice;
        if (in.temperature[m]->size() != in.size || in.ice[m]->size() != in.size) {
            throw refuse::missing_block("seasonal_months",
                                        "Month " + std::to_string(m) + "'s grids are not the size of the world's.");
        }
    }
    if (in.moisture->size() != in.size || in.height->size() != in.size) {
        throw refuse::missing_block("layers", "Weather's layers are not all the size of the world's grid.");
    }

    in.radius_m = world.effective.globe_radius;
    if (!(in.radius_m > 0.0) || !std::isfinite(in.radius_m)) {
        throw refuse::missing_block("effective_config", "Weather measures on this world's globe; "
                                                        "effective_config.globe_radius is " +
                                                            refuse::render(in.radius_m) + ".");
    }
    in.sea_level_m = world.effective.sea_level;
    in.seed = world.config.seed;
    return in;
}

WeatherGround weather_ground(const WeatherInputs& in, const Vec3& at, double day) {
    const MonthBlend m = month_blend(day);
    const Raster r = raster_at(at, in.size);
    WeatherGround g;
    g.latitude_deg = latitude_of(at);
    g.water = bilinear_of(*in.water_type, r, [](double v) { return static_cast<int>(v) != kDry ? 1.0 : 0.0; });
    g.ocean = bilinear_of(*in.water_type, r, [](double v) { return static_cast<int>(v) == kOcean ? 1.0 : 0.0; });
    g.ice = clamp01(lerp(bilinear(*in.ice[static_cast<std::size_t>(m.a)], r),
                         bilinear(*in.ice[static_cast<std::size_t>(m.b)], r), m.w));
    g.temperature_c = month_temperature(in, r, m);
    g.moisture = clamp01(bilinear(*in.moisture, r));
    g.contrast = contrast_at(in, at, m);
    return g;
}

// ---- what the ground feeds -------------------------------------------------------------------

double warm_core_support(const WeatherGround& g) {
    const double potential = std::min(kPotentialCapWind, 28.2 + 55.8 * std::exp(0.1813 * (g.temperature_c - 30.0)));
    // Below about 26 C a warm core cannot hold itself up, and by 23 C it has nothing left.
    const double sea = potential * smoothstep(23.0, 26.5, g.temperature_c) * (1.0 - g.ice);
    // Over land only warm, wet ground holds a remnant up, at the published floor.
    const double land = kLandFloorWind * smoothstep(0.2, 0.7, g.moisture) * smoothstep(8.0, 22.0, g.temperature_c);
    return g.water * sea + (1.0 - g.water) * land;
}

double cold_core_support(const WeatherGround& g) {
    return kFrontalWind * smoothstep(0.35, 1.1, g.contrast) * (0.7 + 0.3 * g.water) *
           (0.6 + 0.4 * smoothstep(0.0, 0.6, g.moisture));
}

double warm_core_genesis(const WeatherGround& g) {
    // Open ocean -- at least half of the ground under the point, fully from nine tenths -- at
    // 26.5 C or more, and far enough from the equator for a spin to start.
    return smoothstep(0.5, 0.9, g.ocean) * (1.0 - g.ice) * smoothstep(26.5, 28.5, g.temperature_c) *
           smoothstep(3.0, 7.0, std::abs(g.latitude_deg));
}

double cold_core_genesis(const WeatherGround& g) {
    return smoothstep(0.35, 1.0, g.contrast) * (0.4 + 0.6 * g.moisture);
}

// ---- systems -------------------------------------------------------------------------------

double weather_shear(std::uint32_t seed, double age_hours) {
    // Each system has its own air: some are born into calm air aloft and can reach what the sea
    // allows, others into hostile air that keeps them weak all their lives. The seed's low bits
    // place it in that range, and the noise moves it through the system's life.
    const double own = static_cast<double>(seed & 0xFFFFu) / 65535.0;
    return clamp01(lerp(kShearLow, kShearHigh, own) + kShearSwing * perlin3(age_hours / 30.0, 0.37, 0.71, seed));
}

WeatherState step_weather(const WeatherState& s, const WeatherGround& g, double shear, double day, double radius_m) {
    WeatherState n = s;
    if (!s.alive) return n;
    const double v = s.wind_m_s;

    if (s.core == WeatherCore::Warm) {
        const double support = warm_core_support(g) * (1.0 - kShearCut * shear);
        if (std::abs(g.latitude_deg) >= kTransitionLatitude && cold_core_support(g) > support) {
            // Carried where contrast feeds it better than the sea does, it turns cold-core and
            // deepens for a day on the contrast before it fills.
            n.core = WeatherCore::Cold;
            n.transition_hours = s.age_hours;
            n.mature_hours = s.age_hours + kTransitionDeepenHours;
        } else if (v < support) {
            // Only the water under it feeds it: over land a warm core never strengthens.
            n.wind_m_s = v + (v / kWarmGrowthHours) * (1.0 - v / support) * (1.0 - 0.5 * shear) * g.water;
        } else {
            const double rate = g.water / kColdSeaDecayHours + (1.0 - g.water) * kLandDecayPerHour;
            n.wind_m_s = v - (v - support) * rate;
        }
    } else {
        const double support = cold_core_support(g) * (1.0 - kShearCut * shear);
        if (s.age_hours < s.mature_hours) {
            if (v < support) {
                n.wind_m_s = v + (v / kColdGrowthHours) * (1.0 - v / support) * (1.0 - 0.5 * shear);
            } else {
                n.wind_m_s = v - (v - support) / kColdRelaxHours;
            }
        } else {
            n.wind_m_s = v - v / kFillingHours - std::max(0.0, v - support) / kColdRelaxHours;
        }
    }

    // Ride the steering flow, plus the drift every cyclone has: poleward and westward.
    const BeltWind steer = weather_steering_wind(g.latitude_deg, day);
    const double strength = clamp01(n.wind_m_s / 50.0);
    const double hemisphere = g.latitude_deg < 0.0 ? -1.0 : 1.0;
    const double east = steer.east_m_s - (kDriftWestward + kDriftWestwardStrong * strength);
    const double north = steer.north_m_s + hemisphere * (kDriftPoleward + kDriftPolewardStrong * strength);
    const double metres = planet_ratio(radius_m) * kSecondsPerHour;
    n.at = travel(s.at, east * metres, north * metres, radius_m);

    n.age_hours = s.age_hours + 1.0;
    const double death_wind = n.core == WeatherCore::Warm ? kWarmDeathWind : kColdDeathWind;
    const double life = n.born == WeatherCore::Warm ? kWarmLifeHours : kColdLifeHours;
    n.alive = n.wind_m_s >= death_wind && n.age_hours < life;
    return n;
}

std::vector<WeatherCandidate> weather_candidates(std::uint32_t seed, std::int64_t hour) {
    Rng rng = Rng::child(seed, "weather-genesis-v1", hour);
    // A Poisson count by inversion, from one draw whatever the count.
    const double u = rng.random();
    int count = 0;
    double term = std::exp(-kCandidatesPerHour);
    double cdf = term;
    while (u >= cdf && count < kMaxCandidatesPerHour) {
        ++count;
        term *= kCandidatesPerHour / count;
        cdf += term;
    }
    std::vector<WeatherCandidate> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        WeatherCandidate c;
        c.hour = hour;
        c.index = i;
        // Uniform over the sphere's area: height uniform in -1..1, longitude uniform.
        const double y = rng.uniform(-1.0, 1.0);
        const double lon = rng.uniform(-kPi, kPi);
        const double ring = std::sqrt(std::max(0.0, 1.0 - y * y));
        c.at = {ring * std::cos(lon), y, ring * std::sin(lon)};
        c.accept = rng.random();
        c.size = rng.random();
        c.mature = rng.random();
        c.shear_seed = static_cast<std::uint32_t>(rng.next_u64() >> 32);
        out.push_back(c);
    }
    return out;
}

std::optional<WeatherCore> weather_accept(const WeatherGround& g, double accept) {
    const double warm = kWarmShare * warm_core_genesis(g);
    if (accept < warm) return WeatherCore::Warm;
    if (accept < warm + kColdShare * cold_core_genesis(g)) return WeatherCore::Cold;
    return std::nullopt;
}

std::vector<WeatherBirth> weather_births(const WeatherInputs& in, std::int64_t first_hour, std::int64_t last_hour) {
    std::vector<WeatherBirth> out;
    for (std::int64_t hour = first_hour; hour <= last_hour; ++hour) {
        for (const WeatherCandidate& c : weather_candidates(in.seed, hour)) {
            const WeatherGround g = weather_ground(in, c.at, static_cast<double>(hour) / kHoursPerDay);
            if (const auto core = weather_accept(g, c.accept)) out.push_back({c, *core, g});
        }
    }
    return out;
}

WeatherState weather_birth_state(const WeatherBirth& birth) {
    WeatherState s;
    s.at = birth.candidate.at;
    s.core = birth.core;
    s.born = birth.core;
    s.wind_m_s = birth.core == WeatherCore::Warm ? kWarmBirthWind : kColdBirthWind;
    s.mature_hours = lerp(kMatureLowHours, kMatureHighHours, birth.candidate.mature);
    s.size = birth.candidate.size;
    return s;
}

std::vector<WeatherState> weather_track(const WeatherInputs& in, const WeatherBirth& birth, std::int64_t until_hour) {
    std::vector<WeatherState> out;
    WeatherState s = weather_birth_state(birth);
    out.push_back(s);
    for (std::int64_t hour = birth.candidate.hour; hour < until_hour && s.alive; ++hour) {
        const double day = static_cast<double>(hour) / kHoursPerDay;
        const WeatherGround g = hour == birth.candidate.hour ? birth.ground : weather_ground(in, s.at, day);
        s = step_weather(s, g, weather_shear(birth.candidate.shear_seed, s.age_hours), day, in.radius_m);
        out.push_back(s);
    }
    return out;
}

// ---- the sky ----------------------------------------------------------------------------------

double weather_capacity_mm(double temperature_c) {
    return kCapacityAt15Mm * std::exp(kCapacityWarmth * (temperature_c - 15.0));
}

double weather_rain_pct(double rain_mm_h) {
    if (!(rain_mm_h > 0.0)) return 0.0;
    return interpolate(kRainMm, kRainPct, kRainPoints, rain_mm_h);
}

double weather_rain_severity_pct(const WeatherGround& g, double rain_mm_h, double hour_of_day) {
    if (!(rain_mm_h > 0.0)) return 0.0;
    return 100.0 * std::min(1.0, weather_rain_pct(rain_mm_h) / 100.0 +
                                     kConvectiveShare * instability(g, hour_of_day) * clamp01(rain_mm_h / kConvectiveRainMm) +
                                     kSevereShare * severe_ingredients(g, hour_of_day) * clamp01(rain_mm_h / kSevereRainMm));
}

Vec3 weather_tornado_spot(std::uint32_t seed, std::int64_t hour, int row, int col) {
    // Anywhere in the cell, drawn afresh each hour: a tornado lasts minutes to an hour.
    Rng rng = Rng::child(seed, "weather-tornado-v1",
                         hour * static_cast<std::int64_t>(kSkyRows) * kSkyCols + static_cast<std::int64_t>(sky_cell(row, col)));
    const double latitude = sky_latitude(row) + rng.uniform(-0.5, 0.5) * 180.0 / kSkyRows;
    const double longitude = sky_longitude(col) + rng.uniform(-0.5, 0.5) * 360.0 / kSkyCols;
    return point_at(latitude, longitude);
}

WeatherSkyGround weather_sky_ground(const WeatherInputs& in) {
    const std::size_t cells = static_cast<std::size_t>(kSkyRows) * kSkyCols;
    WeatherSkyGround g;
    g.water.resize(cells);
    g.moisture.resize(cells);
    g.rise_east.resize(cells);
    g.rise_north.resize(cells);
    for (std::size_t m = 0; m < g.temperature.size(); ++m) {
        g.temperature[m].resize(cells);
        g.ice[m].resize(cells);
    }
    // The ground under each cell's middle; the sea's own height is its surface, not its floor.
    std::vector<double> height(cells);
    const double sea = in.sea_level_m;
    for (int row = 0; row < kSkyRows; ++row) {
        for (int col = 0; col < kSkyCols; ++col) {
            const std::size_t c = sky_cell(row, col);
            const Raster r = raster_at(point_at(sky_latitude(row), sky_longitude(col)), in.size);
            g.water[c] = bilinear_of(*in.water_type, r, [](double v) { return static_cast<int>(v) != kDry ? 1.0 : 0.0; });
            g.moisture[c] = clamp01(bilinear(*in.moisture, r));
            height[c] = bilinear_of(*in.height, r, [sea](double h) { return std::max(h, sea); });
            for (std::size_t m = 0; m < g.temperature.size(); ++m) {
                g.temperature[m][c] = bilinear(*in.temperature[m], r);
                g.ice[m][c] = clamp01(bilinear(*in.ice[m], r));
            }
        }
    }
    // How far the ground rises across a cell, eastward and northward (rows run southward).
    for (int row = 0; row < kSkyRows; ++row) {
        const int north = std::max(0, row - 1);
        const int south = std::min(kSkyRows - 1, row + 1);
        for (int col = 0; col < kSkyCols; ++col) {
            const int west = (col + kSkyCols - 1) % kSkyCols;
            const int east = (col + 1) % kSkyCols;
            const std::size_t c = sky_cell(row, col);
            g.rise_east[c] = 0.5 * (height[sky_cell(row, east)] - height[sky_cell(row, west)]);
            g.rise_north[c] = (height[sky_cell(north, col)] - height[sky_cell(south, col)]) / (south - north);
        }
    }
    return g;
}

WeatherSkyState weather_sky_start(const WeatherSkyGround& g, std::int64_t hour, std::uint32_t seed) {
    require_sky_ground(g);
    const SkyClock clock = sky_clock(hour);
    const std::size_t cells = g.water.size();
    WeatherSkyState s;
    s.hour = hour;
    s.water_mm.resize(cells);
    s.capacity_mm.resize(cells);
    s.rain_mm_h.assign(cells, 0.0);
    s.raining.assign(cells, 0);
    // Each cell somewhere between where a burst leaves it and where one can start, drawn from a
    // stream keyed on the anchor: cells start at every point of filling, as a long run has them,
    // not in step with each other. Where the flow spreads the air out or dry ground dries it, a
    // cell settles where what the ground feeds it matches what is taken away, and one that would
    // settle lower starts there instead, so a run does not open with cloud over its deserts.
    const double day_lit = kNightEvaporation + (1.0 - kNightEvaporation) / kPi;  // daylight's mean over a day
    Rng rng = Rng::child(seed, "weather-sky-start-v1", hour);
    for (int row = 0; row < kSkyRows; ++row) {
        const SkyRow wind = sky_row(row, clock.day);
        for (int col = 0; col < kSkyCols; ++col) {
            const std::size_t c = sky_cell(row, col);
            const double t = sky_temperature(g, c, clock.month);
            const double can_hold = sky_holds(g, c, t, wind, clock);
            double share = rng.uniform(kStopShare, kReadyShare);
            const double kept = wind.gather * sky_keeps(g, c);
            if (kept < 1.0) {
                const double settles = sky_supply(g, c, t, sky_ice(g, c, clock.month), day_lit) / (1.0 - kept);
                share = std::min(share, settles / can_hold);
            }
            s.capacity_mm[c] = can_hold;
            s.water_mm[c] = can_hold * share;
        }
    }
    return s;
}

void step_weather_sky(const WeatherSkyGround& g, WeatherSkyState& s, std::uint32_t seed) {
    require_sky_ground(g);
    require_sky_state(s);
    const std::int64_t hour = s.hour;
    const SkyClock clock = sky_clock(hour);
    const std::size_t cells = s.water_mm.size();

    std::vector<double> held(cells);
    std::vector<double> capacity(cells);
    std::vector<std::uint8_t> raining(cells, 0);
    std::vector<std::uint8_t> breaking(cells, 0);
    // One draw a cell an hour, from a stream keyed on the absolute hour: every run of the sky
    // meets the same draws.
    Rng rng = Rng::child(seed, "weather-sky-v1", hour);

    // Carried, gathered or spread, fed, and dried over desert; and which cells rain this hour,
    // and which of them break into a burst this hour.
    for (int row = 0; row < kSkyRows; ++row) {
        const SkyRow w = sky_row(row, clock.day);
        for (int col = 0; col < kSkyCols; ++col) {
            const std::size_t c = sky_cell(row, col);
            const int c0 = (col + w.k) % kSkyCols;
            const int c1 = (c0 + 1) % kSkyCols;
            const double carried =
                lerp(lerp(s.water_mm[sky_cell(w.r0, c0)], s.water_mm[sky_cell(w.r0, c1)], w.fc),
                     lerp(s.water_mm[sky_cell(w.r1, c0)], s.water_mm[sky_cell(w.r1, c1)], w.fc), w.fr) *
                w.gather;
            const double t = sky_temperature(g, c, clock.month);
            held[c] = (carried + sky_supply(g, c, t, sky_ice(g, c, clock.month), clock.lit)) * sky_keeps(g, c);
            capacity[c] = sky_holds(g, c, t, w, clock);
            const double draw = rng.random();
            const bool breaks = s.raining[c] == 0 && draw < smoothstep(kReadyShare, kReadyPast, held[c] / capacity[c]);
            raining[c] = s.raining[c] != 0 || breaks ? 1 : 0;
            breaking[c] = breaks ? 1 : 0;
        }
    }

    // Each breaking burst draws in a share of the spare cloud of each neighbour not raining, all
    // read from before any is drawn, so the order the cells are visited in changes nothing.
    std::vector<double> drawn(cells, 0.0);
    for (int row = 0; row < kSkyRows; ++row) {
        for (int col = 0; col < kSkyCols; ++col) {
            const std::size_t c = sky_cell(row, col);
            if (breaking[c] == 0) continue;
            for (int dr = -1; dr <= 1; ++dr) {
                const int r = row + dr;
                if (r < 0 || r >= kSkyRows) continue;
                for (int dc = -1; dc <= 1; ++dc) {
                    const std::size_t n = sky_cell(r, (col + dc + kSkyCols) % kSkyCols);
                    if (raining[n] != 0) continue;
                    const double take = kInflowShare * std::max(0.0, held[n] - kStopShare * capacity[n]);
                    drawn[c] += take;
                    drawn[n] -= take;
                }
            }
        }
    }

    // Rained out: each hour a share of what a raining cell holds above the drain share, harder
    // by the square of how far past full it is, until it is down to the stop share.
    std::vector<double> rain(cells, 0.0);
    for (std::size_t c = 0; c < cells; ++c) {
        double h = held[c] + drawn[c];
        if (raining[c] != 0) {
            const double past_full = std::max(1.0, h / capacity[c]);
            const double stop = kStopShare * capacity[c];
            const double fall = kRainRate * std::max(0.0, h - kDrainShare * capacity[c]) * past_full * past_full;
            if (h - fall <= stop) {
                // The last of the burst: down to the stop, and dry.
                rain[c] = std::max(0.0, h - stop);
                h = std::min(h, stop);
                raining[c] = 0;
            } else {
                rain[c] = fall;
                h -= fall;
            }
        }
        held[c] = h;
    }
    s.water_mm.swap(held);
    s.capacity_mm.swap(capacity);
    s.rain_mm_h.swap(rain);
    s.raining.swap(raining);
    s.hour = hour + 1;
}

std::int64_t weather_sky_anchor(std::int64_t hour) {
    return floor_div(hour - kSkySpinUpHours, kSkyAnchorHours) * kSkyAnchorHours;
}

WeatherSky weather_sky(const WeatherInputs& in, double day) {
    weather_require_day(day);
    const double hours = hours_of(day);
    const double whole = std::floor(hours);
    const auto now = static_cast<std::int64_t>(whole);
    const double f = hours - whole;

    const std::shared_ptr<const WeatherSkyGround> g = held_ground(weather_sky_ground(in));
    const std::int64_t anchor = weather_sky_anchor(now);
    auto frames = run_sky(g, anchor, now, in.seed);
    const double w0 = sky_weight(now, anchor);
    const double w1 = sky_weight(now + 1, anchor);
    if (w0 > 0.0 || w1 > 0.0) {
        const auto next = run_sky(g, anchor + kSkyAnchorHours, now, in.seed);
        frames.first = mix(frames.first, next.first, w0);
        frames.second = mix(frames.second, next.second, w1);
    }
    WeatherSky sky;
    sky.cloud_pct.resize(frames.first.cloud.size());
    sky.rain_mm_h.resize(frames.first.rain.size());
    for (std::size_t c = 0; c < sky.rain_mm_h.size(); ++c) {
        sky.cloud_pct[c] = lerp(frames.first.cloud[c], frames.second.cloud[c], f);
        sky.rain_mm_h[c] = lerp(frames.first.rain[c], frames.second.rain[c], f);
    }
    return sky;
}

// ---- the read ----------------------------------------------------------------------------------

WeatherReport weather_report(const World& world, double day) {
    weather_require_day(day);
    const WeatherInputs in = weather_inputs(world);

    const double hours = hours_of(day);
    const double whole = std::floor(hours);
    const auto now = static_cast<std::int64_t>(whole);
    const double f = hours - whole;

    WeatherReport report;
    report.day = day;
    report.hour = hour_of_day(day);
    report.month = std::min(kMonthsPerYear - 1,
                            static_cast<int>(std::fmod(day, static_cast<double>(kDaysPerYear)) / kDaysPerMonth));

    // Every system that can still be alive now was born within the longest life.
    const auto oldest = now - static_cast<std::int64_t>(kWarmLifeHours) + 1;
    for (const WeatherBirth& birth : weather_births(in, std::max<std::int64_t>(0, oldest), now)) {
        const double life = birth.core == WeatherCore::Warm ? kWarmLifeHours : kColdLifeHours;
        const std::int64_t age = now - birth.candidate.hour;
        if (static_cast<double>(age) >= life) continue;
        const std::vector<WeatherState> track = weather_track(in, birth, now + 1);
        const auto at = static_cast<std::size_t>(age);
        if (at >= track.size() || !track[at].alive) continue;
        const WeatherState& next = at + 1 < track.size() ? track[at + 1] : track[at];
        report.systems.push_back(describe(birth, track[at], next, f, in.radius_m));
    }
    report.sky = weather_sky(in, day);
    return report;
}

WeatherSample weather_sample(const World& world, const WeatherReport& report, double latitude_deg,
                             double longitude_deg) {
    // A report is the caller's to build, so its day is checked here too: a month is an index.
    weather_require_day(report.day);
    weather_require_place(latitude_deg, longitude_deg);
    const WeatherInputs in = weather_inputs(world);
    WeatherSample out = sample_at(in, report, point_at(latitude_deg, longitude_deg));
    out.latitude_deg = latitude_deg;
    out.longitude_deg = longitude_deg;
    return out;
}

WeatherSample weather_at(const World& world, double day, double latitude_deg, double longitude_deg) {
    weather_require_place(latitude_deg, longitude_deg);
    return weather_sample(world, weather_report(world, day), latitude_deg, longitude_deg);
}

WeatherSample weather_sample_at_node(const World& world, const WeatherReport& report, int node) {
    const int n = world.effective.size;
    const auto grid = sphere_grid(n, world.effective.globe_radius);
    if (node < 0 || static_cast<std::size_t>(node) >= grid->node_count()) {
        throw std::invalid_argument("weather_sample_at_node: node " + std::to_string(node) + " is not a node of this world's grid");
    }
    const auto at = static_cast<std::size_t>(node);
    const Vec3 p = direction(grid->point_x[at], grid->point_z[at], n);
    return weather_sample(world, report, latitude_of(p), longitude_of(p));
}

std::optional<MoonPhase> moon_phase_on(const World& world, double day) {
    weather_require_day(day);
    if (!world.has_astrology || world.moon.synodic_days <= 0) return std::nullopt;
    MoonPhase out;
    out.synodic_days = world.moon.synodic_days;
    out.fraction = cycle_fraction(world.moon, 0, static_cast<long long>(std::floor(day)));
    // A whole day's fraction is a whole number of periods' parts, so this is the day the
    // phase reads exactly zero.
    out.full = out.fraction * out.synodic_days < 0.5;
    return out;
}

WeatherField weather_field(const World& world, const WeatherReport& report) {
    weather_require_day(report.day);
    const WeatherInputs in = weather_inputs(world);
    const int n = in.size;

    WeatherField field;
    field.day = report.day;
    field.intensity_pct = Grid(n);
    field.wind_bearing_deg = Grid(n);
    field.wind_speed_m_s = Grid(n);
    field.summer_pct = Grid(n);
    field.cloud_pct = Grid(n);

    std::vector<double> intensity, bearing, speed, summer, cloud;
    for (int z = 0; z < n; ++z) {
        const auto count = static_cast<std::size_t>(distinct_count(z, n));
        intensity.assign(count, 0.0);
        bearing.assign(count, 0.0);
        speed.assign(count, 0.0);
        summer.assign(count, 0.0);
        cloud.assign(count, 0.0);
        for (std::size_t x = 0; x < count; ++x) {
            const WeatherSample s = sample_at(in, report, direction(static_cast<int>(x), z, n));
            intensity[x] = s.intensity_pct;
            bearing[x] = s.wind_bearing_deg;
            speed[x] = s.wind_speed_m_s;
            summer[x] = s.summer_pct;
            cloud[x] = s.cloud_pct;
        }
        field.intensity_pct.fill_row(z, intensity);
        field.wind_bearing_deg.fill_row(z, bearing);
        field.wind_speed_m_s.fill_row(z, speed);
        field.summer_pct.fill_row(z, summer);
        field.cloud_pct.fill_row(z, cloud);
    }
    return field;
}

void write_weather_json(std::ostream& out, std::span<const WeatherMoment> moments) {
    JsonWriter json(out, false);
    json.begin_object();
    json.field("schema", std::string_view("fwg.weather"));
    json.field("version", kWeatherApiVersion);
    json.key("moments");
    json.begin_array();
    for (const WeatherMoment& moment : moments) {
        const WeatherReport& report = moment.report;
        json.begin_object();
        json.field("day", report.day);
        json.field("hour", report.hour);
        json.field("month", report.month);
        json.key("systems");
        json.begin_array();
        for (const WeatherSystem& s : report.systems) {
            json.begin_object();
            json.field("id", std::string_view(s.id));
            json.field("latitude", s.latitude_deg);
            json.field("longitude", s.longitude_deg);
            json.field("intensity_pct", s.intensity_pct);
            json.field("wind_m_s", s.wind_m_s);
            json.field("radius_m", s.radius_m);
            json.field("core_radius_m", s.core_radius_m);
            json.field("heading_deg", s.heading_deg);
            json.field("speed_m_s", s.speed_m_s);
            json.field("age_hours", s.age_hours);
            json.field("born_day", s.born_day);
            json.end_object();
        }
        json.end_array();
        json.key("samples");
        json.begin_array();
        for (const WeatherSample& s : moment.samples) {
            json.begin_object();
            json.field("latitude", s.latitude_deg);
            json.field("longitude", s.longitude_deg);
            json.field("intensity_pct", s.intensity_pct);
            json.field("cloud_pct", s.cloud_pct);
            json.field("wind_bearing_deg", s.wind_bearing_deg);
            json.field("wind_speed_m_s", s.wind_speed_m_s);
            json.field("summer_pct", s.summer_pct);
            json.field("winter_pct", 100.0 - s.summer_pct);
            json.key("system");
            if (s.system.empty()) {
                json.value_null();
            } else {
                json.value(std::string_view(s.system));
            }
            json.end_object();
        }
        json.end_array();
        json.end_object();
    }
    json.end_array();
    json.field("limits", kWeatherLimits);
    json.end_object();
}

}  // namespace fwg
