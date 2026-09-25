#include "fwg/weather.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <ostream>
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

// ---- convection ----------------------------------------------------------------------------
// Hour 0 is sunrise; the warm of the afternoon peaks nine hours later.
constexpr double kWarmestHour = 9.0;
constexpr double kDailySwingC = 5.0;
constexpr double kSeaTiming = 0.6;
constexpr double kNightTiming = 0.15;
// The scale's convective tiers. A full cell over warm, wet air is a thunderstorm at 60; where
// heat, moisture and strong contrast meet over land, the upper part of a cell adds up to 25 more
// (a severe storm, with hail), and in the rarest cores, where those ingredients are strong, the
// point reads a tornado: the top of the scale.
constexpr double kThunderShare = 0.6;
constexpr double kSevereShare = 0.25;
constexpr double kTornadoLow = 0.5;
constexpr double kTornadoHigh = 0.9;
// Cells of Earth's thunderstorm clusters, a few tens of kilometres, as a noise frequency on the
// unit sphere: an angle, so the same share of any planet. At this frequency the noise's upper
// quartile starts at about 0.14, its top twentieth at 0.32, its top hundredth at 0.41 and its
// top thousandth at 0.51 (measured): a cell covers about a quarter of the ground where the air
// allows one, a full cell about a twentieth, and the core a tornado needs a thousandth.
constexpr double kCellFrequency = 120.0;
constexpr double kCellLow = 0.14;
constexpr double kCellHigh = 0.32;
constexpr double kCoreLow = 0.41;
constexpr double kCoreHigh = 0.51;

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
    Vec3 wind;              // m/s, in the tangent plane
};

SystemAt system_at(const WeatherSystem& s, const Vec3& p, double radius_m) {
    SystemAt out;
    const Vec3 centre = point_at(s.latitude_deg, s.longitude_deg);
    const double d = great_circle_m(radius_m, centre, p);
    if (!(d < s.radius_m)) return out;

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
        if (d < eye) severity_wind = lerp(kEyeCalmWind, s.wind_m_s, smoothstep(0.0, eye, d));
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

// ---- thunderstorm cells --------------------------------------------------------------------

struct CellSeeds {
    std::uint32_t now = 0;
    std::uint32_t next = 0;
    double blend = 0.0;
};

// Hours since founding year 0. A day written as h / 24 comes back a few ulps either side of h,
// and a moment a hair short of a whole hour would read the hour before it -- a different set of
// systems -- so a value within a few ulps of a whole hour IS that hour.
double hours_of(double day) {
    const double hours = day * kHoursPerDay;
    const double whole = std::round(hours);
    const double slack = 64.0 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::abs(hours));
    return std::abs(hours - whole) <= slack ? whole : hours;
}

// A cell field for each whole hour, cross-faded into the next: cells grow and die within about
// an hour, and the field at a moment is a function of the hour alone.
CellSeeds cell_seeds(std::uint32_t seed, double day) {
    const double hours = hours_of(day);
    const double whole = std::floor(hours);
    const auto hour = static_cast<std::int64_t>(whole);
    return {child_seed(seed, "weather-cells-v1", hour), child_seed(seed, "weather-cells-v1", hour + 1),
            smoothstep(0.0, 1.0, hours - whole)};
}

// The cell noise at p. Blended so its spread stays the same through the hour: a plain average
// of two fields is narrower halfway, and every cell would shrink at the half hour.
double cell_noise(const CellSeeds& seeds, const Vec3& p) {
    const double x = p.x * kCellFrequency;
    const double y = p.y * kCellFrequency;
    const double z = p.z * kCellFrequency;
    const double a = 1.0 - seeds.blend;
    const double b = seeds.blend;
    return (a * perlin3(x, y, z, seeds.now) + b * perlin3(x, y, z, seeds.next)) / std::sqrt(a * a + b * b);
}

double hour_of_day(double day) {
    const double hours = hours_of(day);
    return hours - kHoursPerDay * std::floor(hours / kHoursPerDay);
}

WeatherConvection convection(const WeatherGround& g, double noise, double day) {
    const double daily = std::cos(2.0 * kPi * (hour_of_day(day) - kWarmestHour) / kHoursPerDay);
    const double land = 1.0 - g.water;
    // Land warms by day and cools by night, dry land most; water hardly at all.
    const double air = g.temperature_c + land * kDailySwingC * (1.0 - 0.5 * g.moisture) * daily;
    // Over land the storms come with the afternoon, from late morning to evening; over the sea
    // they are steadier and weaker.
    const double afternoon = smoothstep(-0.1, 0.6, daily);
    const double timing = g.water * kSeaTiming + land * (kNightTiming + (1.0 - kNightTiming) * afternoon);
    // Thunderstorms want warm, wet air: from about 14 C, fully from 25 C.
    const double thunder = smoothstep(14.0, 25.0, air) * smoothstep(0.25, 0.75, g.moisture);
    // Severe storms and tornadoes want heat, moisture and strong contrast together, over land.
    const double severe = land * smoothstep(21.0, 27.0, air) * smoothstep(0.3, 0.5, g.contrast) *
                          smoothstep(0.35, 0.65, g.moisture);
    const double tornado = smoothstep(kTornadoLow, kTornadoHigh, severe);
    WeatherConvection out;
    out.cell = smoothstep(kCellLow, kCellHigh, noise);
    out.potential = timing * std::max(kThunderShare * thunder + kSevereShare * severe, tornado);
    // A cell's rain reaches most of the way in quickly and the severe part only its upper
    // reaches; a tornado stands only in the rarest cores, and reads the top of the scale.
    const double core = smoothstep(kCoreLow, kCoreHigh, noise);
    out.severity = timing * std::max(kThunderShare * thunder * std::sqrt(out.cell) +
                                         kSevereShare * severe * out.cell * out.cell,
                                     tornado * core);
    return out;
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

WeatherSample sample_at(const WeatherInputs& in, const WeatherReport& report, const CellSeeds& seeds, const Vec3& p) {
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
    for (const WeatherSystem& s : report.systems) {
        const SystemAt here = system_at(s, p, in.radius_m);
        wind = wind + here.wind;
        if (here.severity > severity) {
            severity = here.severity;
            out.system = s.id;
        }
    }

    const WeatherConvection storm = convection(g, cell_noise(seeds, p), report.day);
    if (storm.severity > severity) {
        severity = storm.severity;
        out.system.clear();
    }
    if (storm.severity > 0.0) {
        // A cell's gust runs with the prevailing wind.
        const double length = std::sqrt(dot(prevailing, prevailing));
        const Vec3 along = length > 1e-9 ? prevailing * (1.0 / length) : b.east;
        wind = wind + along * weather_wind_for_pct(100.0 * storm.severity);
    }

    out.intensity_pct = 100.0 * severity;
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

WeatherConvection weather_convection(const WeatherInputs& in, const WeatherGround& ground, const Vec3& at,
                                     double day) {
    return convection(ground, cell_noise(cell_seeds(in.seed, day), at), day);
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
    return report;
}

WeatherSample weather_sample(const World& world, const WeatherReport& report, double latitude_deg,
                             double longitude_deg) {
    // A report is the caller's to build, so its day is checked here too: a month is an index.
    weather_require_day(report.day);
    weather_require_place(latitude_deg, longitude_deg);
    const WeatherInputs in = weather_inputs(world);
    WeatherSample out =
        sample_at(in, report, cell_seeds(in.seed, report.day), point_at(latitude_deg, longitude_deg));
    out.latitude_deg = latitude_deg;
    out.longitude_deg = longitude_deg;
    return out;
}

WeatherSample weather_at(const World& world, double day, double latitude_deg, double longitude_deg) {
    weather_require_place(latitude_deg, longitude_deg);
    return weather_sample(world, weather_report(world, day), latitude_deg, longitude_deg);
}

WeatherField weather_field(const World& world, const WeatherReport& report) {
    weather_require_day(report.day);
    const WeatherInputs in = weather_inputs(world);
    const CellSeeds seeds = cell_seeds(in.seed, report.day);
    const int n = in.size;

    WeatherField field;
    field.day = report.day;
    field.intensity_pct = Grid(n);
    field.wind_bearing_deg = Grid(n);
    field.wind_speed_m_s = Grid(n);
    field.summer_pct = Grid(n);

    std::vector<double> intensity, bearing, speed, summer;
    for (int z = 0; z < n; ++z) {
        const auto count = static_cast<std::size_t>(distinct_count(z, n));
        intensity.assign(count, 0.0);
        bearing.assign(count, 0.0);
        speed.assign(count, 0.0);
        summer.assign(count, 0.0);
        for (std::size_t x = 0; x < count; ++x) {
            const WeatherSample s = sample_at(in, report, seeds, direction(static_cast<int>(x), z, n));
            intensity[x] = s.intensity_pct;
            bearing[x] = s.wind_bearing_deg;
            speed[x] = s.wind_speed_m_s;
            summer[x] = s.summer_pct;
        }
        field.intensity_pct.fill_row(z, intensity);
        field.wind_bearing_deg.fill_row(z, bearing);
        field.wind_speed_m_s.fill_row(z, speed);
        field.summer_pct.fill_row(z, summer);
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
