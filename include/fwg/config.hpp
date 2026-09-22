// The world configuration and the request that resolves into one.
//
// `Config` is the fully resolved parameter set a generation stage reads. `Request`
// is what a caller supplies: a seed, a world size, and overrides. Resolution is
// where a physical world width becomes design-space radius, relief, amplitude and
// wavelength -- and where distances authored against the reference world are scaled
// to the world actually being built.
//
// Overrides are std::optional rather than a map because the derivation genuinely
// needs to distinguish "not supplied" from "supplied and equal to the default": an
// explicit globe_radius bypasses the width derivation entirely, and an explicit
// value of any derived term wins over the derived one.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <map>
#include <string_view>

#include "fwg/options.hpp"

namespace fwg {

enum class WorldSize { Small, Medium, Large };

const char* to_string(WorldSize size);
std::optional<WorldSize> parse_world_size(std::string_view text);

// Physical distance around the world for each preset, in metres.
double circumference_for(WorldSize size);

// Relief budget and collision gain behind the presets. Relief stays constant across
// the three sizes rather than scaling with circumference: at 200 km, 4 km peaks are
// already a hundred times Earth's relief-to-circumference ratio. A larger world
// means more land, not taller mountains.
inline constexpr double kDefaultReliefM = 1667.0;
inline constexpr double kDefaultOrogeny = 10.0;

// Ceilings the request layer refuses to silently clamp. A silent clamp here means a
// reach stops growing with the planet and cultural regions quietly stop scaling.
inline constexpr double kRiverThresholdMaxKm2 = 10000.0;
inline constexpr double kReachMaxM = 100000.0;

struct Config {
    // --- identity and extent -------------------------------------------------
    std::uint32_t seed = 42;
    int size = 129;      // cells along one edge of the sphere grid
    int phase = 16;      // how far through the sixteen stages to run
    WorldSize world_size = WorldSize::Small;
    // The width that ACTUALLY built this world, in metres. world_size above is only
    // the preset that was asked for; an explicit circumference_km or globe_radius
    // bypasses it, and a document reporting the untouched preset would name a world
    // that was never built. 0 means globe_radius was set directly.
    double circumference_m = 0.0;
    bool world_size_was_used = false;

    // --- planet geometry -----------------------------------------------------
    double globe_radius = 10000.0;   // design space
    double world_scale = 0.17744123532462844;
    double tectonic_relief = 800.0;
    double amplitude = 1100.0;
    double wavelength = 4300.0;
    int octaves = 5;
    double ridge = 0.55;
    double sea_level = 0.0;
    double radius = 500.0;           // TPI neighbourhood radius

    // --- tectonics -----------------------------------------------------------
    int plate_count = 12;
    int layout_variation = 0;
    int detail_variation = 0;
    double crust_bias = 0.0;
    double belt_width = 0.08;
    double mountain_detail = 0.65;
    // Gain on collision uplift alone. The hypsometric profile puts ocean floor far
    // below continental platform, so raising tectonic_relief deepens basins as fast
    // as it raises peaks. This lifts orogenic belts without touching the basins.
    double orogeny = 10.0;
    double mountain_abundance = 1.3;  // width of collision belts

    // --- erosion -------------------------------------------------------------
    int erosion_passes = 6;
    double erosion_strength = 0.5;

    // --- magic ---------------------------------------------------------------
    int magic_enabled = 1;  // 0 leaves every ley network unmanifested

    // --- climate -------------------------------------------------------------
    double wind_bearing = 90.0;      // degrees clockwise from north
    int rain_passes = 48;            // fixed iteration count; NOT run to convergence
    double rain_strength = 1.0;      // gain on orographic rainout
    double temperature_offset = 0.0; // Celsius, applied everywhere
    double moisture_bias = 0.0;      // added to the 0..1 wetness before clamping

    // --- carried for stages not yet ported -----------------------------------
    // Present so that the struct does not churn as stages land, and so a resolved
    // config already reports the scaled values. Nothing in the kernel reads them.
    double settlement_spacing = 450.0;
    double support_reach = 1000.0;
    double culture_link_cost = 1800.0;
    double river_threshold_km2 = 0.15;

    // The world-options control surface: every knob that is not a field above.
    // Ocean islands, coasts, cold, nests, nomads, the moon, the magical networks and
    // the thirteen regions all read from here.
    WorldOptions options;

    // Equatorial cell spacing in design space.
    double step() const;

    // Throws std::invalid_argument naming the field and its bounds.
    void validate() const;
};

struct Request {
    std::uint32_t seed = 42;
    std::optional<int> size;
    std::optional<int> phase;
    std::optional<WorldSize> world_size;

    // Authored world shape. Either of these overrides the world_size preset; an
    // explicit globe_radius override bypasses both.
    std::optional<double> circumference_km;
    std::optional<double> relief_m;

    std::optional<double> globe_radius;
    std::optional<double> world_scale;
    std::optional<double> tectonic_relief;
    std::optional<double> amplitude;
    std::optional<double> wavelength;
    std::optional<int> octaves;
    std::optional<double> ridge;
    std::optional<double> sea_level;
    std::optional<double> radius;

    std::optional<int> plate_count;
    std::optional<int> layout_variation;
    std::optional<int> detail_variation;
    std::optional<double> crust_bias;
    std::optional<double> belt_width;
    std::optional<double> mountain_detail;
    std::optional<double> orogeny;
    std::optional<double> mountain_abundance;

    std::optional<int> erosion_passes;
    std::optional<double> erosion_strength;

    std::optional<int> magic_enabled;
    std::optional<double> wind_bearing;
    std::optional<int> rain_passes;
    std::optional<double> rain_strength;
    std::optional<double> temperature_offset;
    std::optional<double> moisture_bias;

    std::optional<double> settlement_spacing;
    std::optional<double> support_reach;
    std::optional<double> culture_link_cost;
    std::optional<double> river_threshold_km2;

    // World options by published name. An unknown name or an out-of-range value is
    // refused by resolve(), never clamped.
    std::map<std::string, double> options;
};

// Applies the width derivation and the reference-world reach scaling, then validates.
Config resolve(const Request& request);

}  // namespace fwg
