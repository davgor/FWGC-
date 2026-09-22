#include "fwg/config.hpp"

#include <cmath>
#include <numbers>
#include <sstream>
#include <stdexcept>

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

double circumference_for(WorldSize size) {
    switch (size) {
        case WorldSize::Small: return 200000.0;
        case WorldSize::Medium: return 400000.0;
        case WorldSize::Large: return 600000.0;
    }
    return 200000.0;
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
    require_range("mountain_abundance", mountain_abundance, 0.25, 2.0);
    require_range("erosion_strength", erosion_strength, 0.0, 1.0);
    require_range("wind_bearing", wind_bearing, 0.0, 360.0, "deg");
    require_range("rain_strength", rain_strength, 0.0, 3.0);
    require_range("temperature_offset", temperature_offset, -40.0, 40.0, "C");
    require_range("moisture_bias", moisture_bias, -1.0, 1.0);
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
    cfg.mountain_abundance = pick(request.mountain_abundance, cfg.mountain_abundance);
    cfg.erosion_passes = pick(request.erosion_passes, cfg.erosion_passes);
    cfg.erosion_strength = pick(request.erosion_strength, cfg.erosion_strength);
    cfg.wind_bearing = pick(request.wind_bearing, cfg.wind_bearing);
    cfg.rain_passes = pick(request.rain_passes, cfg.rain_passes);
    cfg.rain_strength = pick(request.rain_strength, cfg.rain_strength);
    cfg.temperature_offset = pick(request.temperature_offset, cfg.temperature_offset);
    cfg.moisture_bias = pick(request.moisture_bias, cfg.moisture_bias);

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
