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
#include <vector>
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

    // Which recipe authored these defaults. 3 is the only live one and every default
    // in this struct is its; 0 is a bare geometry experiment that stops at stage 9 and
    // has no civilizations to infer anything from. Retired recipes are not resurrected:
    // a world built by one has to be regenerated.
    int world_recipe = 3;
    // Whether the world derives its own settings from its ground. Forbidden on recipe
    // 3, which carries explicit defaults and overrides instead -- the two ways of
    // choosing a setting would otherwise both claim the same field.
    int auto_parameters = 0;
    // Which people founds the first cities, or "mixed" to let every eligible profile
    // compete for the same ground.
    std::string population_profile = "mixed";
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

    // --- roads and settlement ------------------------------------------------
    double road_max_grade = 0.35;      // steepest a road may climb, as a ratio
    double bridge_cost = 200.0;        // metres-equivalent added for a river crossing
    double human_magic_limit = 0.45;   // magic hazard people will not live or road through
    double stubbornness = 0.2;         // how hard a people clings to poor ground
    double urban_food_demand = 10.0;   // food units per urban resident per month
    double human_adaptation = 0.7;     // how far a people bends to unsuited ground
    int settlement_count = 24;         // requested cities; a ceiling, not a target
    int hamlets_per_core = 3;
    int fortress_count = 1024;         // a ceiling that deliberately never binds
    int college_count = 2;

    // --- carried for stages not yet ported -----------------------------------
    // Present so that the struct does not churn as stages land, and so a resolved
    // config already reports the scaled values. Nothing in the kernel reads them.
    double settlement_spacing = 450.0;
    double support_reach = 1000.0;
    double culture_link_cost = 1800.0;
    double river_threshold_km2 = 0.15;

    // Conditions this world nudged because the caller asked for a region that needs
    // them. Recorded rather than silent: a world whose mountains grew because someone
    // asked for dragons should be able to say so.
    struct OptionBias {
        std::string target;  // the option or field that moved
        std::string zone;    // the region that asked for it
    };
    std::vector<OptionBias> option_biases;

    // What the request itself set, by published control name, sorted by name: the world
    // document's recipe.overrides. Recorded by resolve() because a world keeps its Config
    // and not its Request, and "what the caller asked for" cannot be read back off resolved
    // values -- the world_size preset moves globe_radius off its default with nobody having
    // asked for a radius. A string-valued control (world_size, population_profile) sets
    // `text` and `is_text`; everything else is a number.
    struct RequestedOverride {
        std::string name;
        double number = 0.0;
        std::string text;
        bool is_text = false;
    };
    std::vector<RequestedOverride> requested_overrides;

    // The world-options control surface: every knob that is not a field above.
    // Ocean islands, coasts, cold, nests, nomads, the moon, the magical networks and
    // the thirteen regions all read from here.
    WorldOptions options;

    // Whether this world derives its own quotas rather than taking a caller's count.
    // Recipe worlds and mixed populations both infer; a bare geometry experiment with
    // an explicit profile does not.
    bool infers_own_settings() const;

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
    // Routed to world option `mountain_abundance`, which is where it lives. It is
    // kept on Request because callers already pass it by this name.
    std::optional<double> mountain_abundance;

    // Which people founds the first cities, or "mixed" for all of them at once.
    std::optional<std::string> population_profile;

    std::optional<int> erosion_passes;
    std::optional<double> erosion_strength;

    std::optional<int> magic_enabled;
    std::optional<double> wind_bearing;
    std::optional<int> rain_passes;
    std::optional<double> rain_strength;
    std::optional<double> temperature_offset;
    std::optional<double> moisture_bias;

    std::optional<double> road_max_grade;
    std::optional<double> bridge_cost;
    std::optional<double> human_magic_limit;
    std::optional<double> stubbornness;
    std::optional<double> urban_food_demand;
    std::optional<double> human_adaptation;
    std::optional<int> settlement_count;
    std::optional<int> hamlets_per_core;
    std::optional<int> fortress_count;
    std::optional<int> college_count;

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
