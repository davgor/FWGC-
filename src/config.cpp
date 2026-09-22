#include "fwg/config.hpp"

#include <cmath>
#include <numbers>
#include <sstream>
#include <stdexcept>

#include "fwg/civilizations.hpp"
#include "fwg/scale.hpp"

namespace fwg {
namespace {

constexpr double kPi = std::numbers::pi;

[[noreturn]] void out_of_range(std::string_view field, double value, double low, double high,
                               std::string_view units = {}) {
    std::ostringstream message;
    message << field << " must be " << low << ".." << high;
    if (!units.empty()) message << ' ' << units;
    message << "; received " << value;
    throw std::invalid_argument(message.str());
}

void require_range(std::string_view field, double value, double low, double high,
                   std::string_view units = {}) {
    if (!std::isfinite(value) || value < low || value > high) out_of_range(field, value, low, high, units);
}

void require_int_range(std::string_view field, long long value, long long low, long long high) {
    if (value < low || value > high) {
        std::ostringstream message;
        message << field << " must be an integer " << low << ".." << high << "; received " << value;
        throw std::invalid_argument(message.str());
    }
}

template <typename T>
T pick(const std::optional<T>& override_value, T fallback) {
    return override_value ? *override_value : fallback;
}

}  // namespace

const char* to_string(WorldSize size) {
    switch (size) {
        case WorldSize::Small: return "small";
        case WorldSize::Medium: return "medium";
        case WorldSize::Large: return "large";
    }
    return "small";
}

std::optional<WorldSize> parse_world_size(std::string_view text) {
    if (text == "small") return WorldSize::Small;
    if (text == "medium") return WorldSize::Medium;
    if (text == "large") return WorldSize::Large;
    return std::nullopt;
}

// Asking for a magical region biases the conditions that region needs.
//
// A caller who raises `draconic_occurrence` is asking for dragons, and dragons need
// mountains -- so the world nudges `mountain_abundance` up as well. Without this the
// request raises the region's own weight against ground that still cannot host it, and
// the caller gets a stronger multiplier on zero.
//
// Two rules keep it honest. The nudge is proportional to how far past the default the
// caller actually went, so a small request bends the world a little. And an EXPLICIT
// setting of the target always wins: a caller who set `mountain_abundance` themselves
// has said what they want and is not overruled by a side effect of another knob.
//
// Placement stays conditional either way. This bias makes a region possible, never
// certain.
struct Prerequisite {
    const char* zone;
    Option target;
    double gain;
};

// The eight authored rules. `red_sands` pulls its target DOWNWARD, which is why the
// gain is signed rather than a magnitude: red sands want dry ground.
constexpr Prerequisite kPrerequisites[] = {
    {"draconic", Option::mountain_abundance, 0.6},
    {"demonic", Option::infernal_strength, 0.8},
    {"haunted_sands", Option::umbral_strength, 0.8},
    {"steampunk", Option::metal_abundance, 0.7},
    {"dead_sea", Option::salinity, 0.6},
    {"starlight_lakes", Option::weave_strength, 0.6},
    {"pirate", Option::archipelago_occurrence, 0.4},
};

// `red_sands` biases `moisture_bias`, which is a Config field rather than a world
// option, so it is applied separately below.
constexpr double kRedSandsMoistureGain = -0.5;

// How far past its default the caller pushed this zone, across both of its knobs. The
// LARGER of the two wins rather than their sum: asking for more regions and stronger
// regions is one wish stated twice, not two independent demands on the ground.
double zone_demand(const std::map<std::string, double>& requested, const char* zone) {
    double delta = 0.0;
    for (const char* suffix : {"_occurrence", "_intensity"}) {
        const std::string name = std::string(zone) + suffix;
        const auto it = requested.find(name);
        if (it == requested.end()) continue;
        const auto option = option_by_name(name);
        if (!option) continue;
        delta = std::max(delta, it->second - option_spec(*option).default_value);
    }
    return delta;
}

void apply_prerequisites(Config& cfg, const Request& request) {
    for (const Prerequisite& rule : kPrerequisites) {
        const double demand = zone_demand(request.options, rule.zone);
        if (demand <= 0.0) continue;
        const OptionSpec& spec = option_spec(rule.target);
        // An explicit request for the target is the caller's word and outranks this.
        if (request.options.find(std::string(spec.name)) != request.options.end()) continue;
        const double biased = spec.default_value + demand * rule.gain;
        cfg.options.set(rule.target, std::max(spec.min_value, std::min(spec.max_value, biased)));
        cfg.option_biases.push_back({std::string(spec.name), std::string(rule.zone)});
    }

    const double demand = zone_demand(request.options, "red_sands");
    if (demand > 0.0 && !request.moisture_bias) {
        // Red sands are dry ground, so this one pulls downward.
        cfg.moisture_bias = std::max(-1.0, std::min(1.0, demand * kRedSandsMoistureGain));
        cfg.option_biases.push_back({"moisture_bias", "red_sands"});
    }
}

double circumference_for(WorldSize size) {
    switch (size) {
        case WorldSize::Small: return 200000.0;
        case WorldSize::Medium: return 400000.0;
        case WorldSize::Large: return 600000.0;
    }
    return 200000.0;
}

bool Config::infers_own_settings() const {
    return world_recipe != 0 || auto_parameters != 0 || population_profile == "mixed";
}

double Config::step() const { return 2.0 * kPi * globe_radius / (size - 1); }

void Config::validate() const {
    require_int_range("size", size, 3, 1025);
    if (size % 2 == 0) throw std::invalid_argument("size must be odd so the grid has a pole row and a seam");
    require_int_range("phase", phase, 1, 16);
    require_int_range("plate_count", plate_count, 2, 64);
    require_int_range("octaves", octaves, 1, 12);
    require_int_range("erosion_passes", erosion_passes, 0, 40);
    require_int_range("rain_passes", rain_passes, 1, 128);
    require_int_range("magic_enabled", magic_enabled, 0, 1);
    require_int_range("auto_parameters", auto_parameters, 0, 1);
    if (world_recipe != 0 && world_recipe != 3) {
        throw std::invalid_argument(
            "world_recipe must be 0 (geometry experiment) or 3; retired worlds must be regenerated");
    }
    if (world_recipe != 0 && auto_parameters != 0) {
        throw std::invalid_argument(
            "world recipe 3 uses explicit defaults and overrides; auto_parameters requires world_recipe 0");
    }
    // A geometry experiment has no peoples, so there is nothing for stages 10-16 to run.
    if (world_recipe == 0 && phase > 9) {
        throw std::invalid_argument("world_recipe 0 stops at phase 9; phase " + std::to_string(phase) +
                                    " needs recipe 3");
    }
    if (population_profile != "mixed" && civilization_profile(population_profile) == nullptr) {
        throw std::invalid_argument("population_profile '" + population_profile +
                                    "' is not a civilization in the registry");
    }
    require_int_range("layout_variation", layout_variation, 0, 1000000);
    require_int_range("detail_variation", detail_variation, 0, 1000000);

    require_range("globe_radius", globe_radius, 1.0, 1e9, "m");
    require_range("world_scale", world_scale, 1e-6, 1e6, "ratio");
    require_range("tectonic_relief", tectonic_relief, 0.0, 1e7, "m");
    require_range("amplitude", amplitude, 0.0, 1e7, "m");
    require_range("wavelength", wavelength, 1.0, 1e9, "m");
    require_range("ridge", ridge, 0.0, 1.0);
    require_range("sea_level", sea_level, -1e7, 1e7, "m");
    require_range("radius", radius, 1.0, 1e9, "m");
    require_range("crust_bias", crust_bias, -1.0, 1.0);
    require_range("belt_width", belt_width, 0.001, 1.0, "rad");
    require_range("mountain_detail", mountain_detail, 0.0, 1.0);
    require_range("orogeny", orogeny, 0.0, 20.0);
    require_range("erosion_strength", erosion_strength, 0.0, 1.0);
    require_range("wind_bearing", wind_bearing, 0.0, 360.0, "deg");
    require_range("rain_strength", rain_strength, 0.0, 3.0);
    require_range("temperature_offset", temperature_offset, -40.0, 40.0, "C");
    require_range("moisture_bias", moisture_bias, -1.0, 1.0);
    require_range("road_max_grade", road_max_grade, 0.01, 1.0);
    require_range("bridge_cost", bridge_cost, 0.0, 10000.0, "m");
    require_range("human_magic_limit", human_magic_limit, 0.0, 1.0);
    require_range("stubbornness", stubbornness, 0.0, 1.0);
    require_range("urban_food_demand", urban_food_demand, 0.0, 10000.0);
    require_range("human_adaptation", human_adaptation, 0.0, 1.0);
    require_int_range("settlement_count", settlement_count, 0, 24);
    require_int_range("hamlets_per_core", hamlets_per_core, 0, 8);
    require_int_range("fortress_count", fortress_count, 0, 1024);
    require_int_range("college_count", college_count, 0, 12);
    require_range("settlement_spacing", settlement_spacing, 10.0, kReachMaxM, "m");
    require_range("support_reach", support_reach, 100.0, kReachMaxM, "m");
    require_range("culture_link_cost", culture_link_cost, 1.0, kReachMaxM, "m");
    require_range("river_threshold_km2", river_threshold_km2, 0.001, kRiverThresholdMaxKm2, "km2");

    // The relief budget has to fit inside the planet, or the surface folds through
    // the origin and every downstream layer reads nonsense.
    const double budget = scale::kReliefBudgetFactor * tectonic_relief + scale::kDetailBudgetFactor * amplitude;
    if (budget >= scale::kBudgetHeadroom * globe_radius) {
        std::ostringstream message;
        message << "relief budget " << budget << " exceeds " << scale::kBudgetHeadroom
                << " of the design radius " << globe_radius
                << "; widen the world or lower tectonic_relief/amplitude";
        throw std::invalid_argument(message.str());
    }
}

Config resolve(const Request& request) {
    Config cfg;
    cfg.seed = request.seed;
    cfg.size = pick(request.size, cfg.size);
    cfg.phase = pick(request.phase, cfg.phase);
    cfg.world_size = pick(request.world_size, cfg.world_size);

    cfg.world_scale = pick(request.world_scale, cfg.world_scale);
    cfg.octaves = pick(request.octaves, cfg.octaves);
    cfg.ridge = pick(request.ridge, cfg.ridge);
    cfg.sea_level = pick(request.sea_level, cfg.sea_level);
    cfg.radius = pick(request.radius, cfg.radius);
    cfg.plate_count = pick(request.plate_count, cfg.plate_count);
    cfg.layout_variation = pick(request.layout_variation, cfg.layout_variation);
    cfg.detail_variation = pick(request.detail_variation, cfg.detail_variation);
    cfg.crust_bias = pick(request.crust_bias, cfg.crust_bias);
    cfg.belt_width = pick(request.belt_width, cfg.belt_width);
    cfg.mountain_detail = pick(request.mountain_detail, cfg.mountain_detail);

    cfg.erosion_passes = pick(request.erosion_passes, cfg.erosion_passes);
    cfg.erosion_strength = pick(request.erosion_strength, cfg.erosion_strength);
    cfg.magic_enabled = pick(request.magic_enabled, cfg.magic_enabled);
    cfg.wind_bearing = pick(request.wind_bearing, cfg.wind_bearing);
    cfg.rain_passes = pick(request.rain_passes, cfg.rain_passes);
    cfg.rain_strength = pick(request.rain_strength, cfg.rain_strength);
    cfg.temperature_offset = pick(request.temperature_offset, cfg.temperature_offset);
    cfg.moisture_bias = pick(request.moisture_bias, cfg.moisture_bias);
    cfg.road_max_grade = pick(request.road_max_grade, cfg.road_max_grade);
    cfg.bridge_cost = pick(request.bridge_cost, cfg.bridge_cost);
    cfg.human_magic_limit = pick(request.human_magic_limit, cfg.human_magic_limit);
    cfg.stubbornness = pick(request.stubbornness, cfg.stubbornness);
    cfg.urban_food_demand = pick(request.urban_food_demand, cfg.urban_food_demand);
    cfg.human_adaptation = pick(request.human_adaptation, cfg.human_adaptation);
    cfg.settlement_count = pick(request.settlement_count, cfg.settlement_count);
    cfg.hamlets_per_core = pick(request.hamlets_per_core, cfg.hamlets_per_core);
    cfg.fortress_count = pick(request.fortress_count, cfg.fortress_count);
    cfg.college_count = pick(request.college_count, cfg.college_count);

    // World options, by published name. An unknown name or an out-of-range value is
    // REFUSED, never ignored and never clamped: a caller that misspells a control
    // otherwise gets a default-shaped world and no indication it asked for anything.
    for (const auto& [name, value] : request.options) {
        const auto option = option_by_name(name);
        if (!option) throw std::invalid_argument("unknown world option '" + name + "'");
        cfg.options.set(*option, value);  // throws, naming the field and its bounds
    }
    // A request for a region biases the conditions that region needs, before the
    // combination rules are checked -- a bias must not be able to produce a config
    // that validation would have refused.
    apply_prerequisites(cfg, request);

    // And the rules that only a COMBINATION of options can break.
    cfg.options.validate();

    // Physical width in, design-space geometry out. An explicit globe_radius bypasses
    // all of it; an explicit override of any derived term still wins.
    double circumference = request.circumference_km ? *request.circumference_km * 1000.0 : 0.0;
    if (circumference <= 0.0 && !request.globe_radius) {
        circumference = circumference_for(cfg.world_size);
        cfg.world_size_was_used = true;
    }
    cfg.circumference_m = circumference;

    if (circumference > 0.0) {
        const auto derived = scale::shape_overrides(
            circumference,
            request.relief_m.value_or(kDefaultReliefM),
            request.orogeny.value_or(kDefaultOrogeny),
            cfg.plate_count,
            cfg.world_scale);
        cfg.globe_radius = derived.globe_radius;
        cfg.tectonic_relief = derived.tectonic_relief;
        cfg.amplitude = derived.amplitude;
        cfg.wavelength = derived.wavelength;
        cfg.orogeny = derived.orogeny;

        // Distances authored against the 11.15 km reference world have to grow with
        // the world or they stop spanning anything. NOT clamped: past the width where
        // a ceiling binds, a clamped reach stops growing with the planet, silently.
        const double factor = scale::reach_scale(circumference);
        const auto scale_reach = [&](std::string_view field, double& value, const std::optional<double>& override_value) {
            if (override_value) { value = *override_value; return; }
            const double scaled = value * factor;
            if (scaled > kReachMaxM) {
                const double binds_km = value > 0.0 ? kReachMaxM / value * scale::reference_circumference_m() / 1000.0 : 0.0;
                std::ostringstream message;
                message << "a " << circumference / 1000.0 << " km world resolves " << field << " to " << scaled
                        << " m, above the " << kReachMaxM << " m maximum; widths above " << binds_km
                        << " km need that bound raised or an explicit " << field << " override";
                throw std::invalid_argument(message.str());
            }
            value = scaled;
        };
        scale_reach("settlement_spacing", cfg.settlement_spacing, request.settlement_spacing);
        scale_reach("support_reach", cfg.support_reach, request.support_reach);
        scale_reach("culture_link_cost", cfg.culture_link_cost, request.culture_link_cost);

        // A catchment is an AREA, so it takes the SQUARE of the same factor. Left
        // absolute, every land cell drains more than the threshold, so every land cell
        // is a river, freshwater_distance is 0 everywhere and flood_risk saturates.
        if (request.river_threshold_km2) {
            cfg.river_threshold_km2 = *request.river_threshold_km2;
        } else {
            const double scaled = cfg.river_threshold_km2 * scale::runoff_scale(circumference);
            if (scaled > kRiverThresholdMaxKm2) {
                std::ostringstream message;
                message << "a " << circumference / 1000.0 << " km world resolves river_threshold_km2 to "
                        << scaled << " km2, above the " << kRiverThresholdMaxKm2
                        << " km2 maximum; raise that bound or supply an explicit override";
                throw std::invalid_argument(message.str());
            }
            cfg.river_threshold_km2 = scaled;
        }
    } else {
        cfg.settlement_spacing = pick(request.settlement_spacing, cfg.settlement_spacing);
        cfg.support_reach = pick(request.support_reach, cfg.support_reach);
        cfg.culture_link_cost = pick(request.culture_link_cost, cfg.culture_link_cost);
        cfg.river_threshold_km2 = pick(request.river_threshold_km2, cfg.river_threshold_km2);
        cfg.orogeny = pick(request.orogeny, cfg.orogeny);
    }

    // Explicit overrides of the derived terms win over the derivation.
    cfg.globe_radius = pick(request.globe_radius, cfg.globe_radius);
    cfg.tectonic_relief = pick(request.tectonic_relief, cfg.tectonic_relief);
    cfg.amplitude = pick(request.amplitude, cfg.amplitude);
    cfg.wavelength = pick(request.wavelength, cfg.wavelength);
    cfg.orogeny = pick(request.orogeny, cfg.orogeny);

    cfg.validate();
    return cfg;
}

}  // namespace fwg
