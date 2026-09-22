#include "fwg/settlement.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <queue>

#include "fwg/routing.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

constexpr double kPi = std::numbers::pi;

// Above this a lake is brine and nobody drinks from it.
constexpr double kDrinkableSalinity = 0.2;

// Distance and freeboard scales for the flood proxy, in metres.
constexpr double kFloodDistanceScale = 80.0;
constexpr double kFloodFreeboard = 10.0;

std::vector<double> node_values(const World& world, const SphereGrid& grid, const char* layer,
                                double fallback = 0.0) {
    const auto it = world.layers.find(layer);
    std::vector<double> out(grid.node_count(), fallback);
    if (it == world.layers.end()) return out;
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = it->second.at(grid.point_x[i], grid.point_z[i]);
    return out;
}

}  // namespace

FreshwaterAccess freshwater_access(const World& world, const SphereGrid& grid) {
    const std::size_t count = grid.node_count();
    const std::vector<double> water = node_values(world, grid, "water_type");
    const std::vector<double> river = node_values(world, grid, "rain_river");
    const std::vector<double> salinity = node_values(world, grid, "salinity");

    FreshwaterAccess out;
    out.distance_m.assign(count, kUnreachable);
    out.source.assign(count, -1);

    using Entry = std::pair<double, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
    for (std::size_t i = 0; i < count; ++i) {
        // A lake or a river, and not brine. The ocean is never a source.
        const bool fresh = (water[i] == 2.0 || river[i] != 0.0) && salinity[i] < kDrinkableSalinity;
        if (!fresh) continue;
        out.distance_m[i] = 0.0;
        out.source[i] = static_cast<int>(i);
        queue.emplace(0.0, static_cast<int>(i));
    }

    while (!queue.empty()) {
        const auto [d, i] = queue.top();
        queue.pop();
        const auto ui = static_cast<std::size_t>(i);
        if (d != out.distance_m[ui]) continue;
        for (const SphereNeighbour& nb : grid.neighbours[ui]) {
            const auto j = static_cast<std::size_t>(nb.index);
            // Water does not carry itself across the sea: a spring on one island is no
            // use on another, however close.
            if (water[j] == 1.0) continue;
            const double candidate = d + nb.distance;
            if (candidate >= out.distance_m[j]) continue;
            out.distance_m[j] = candidate;
            out.source[j] = out.source[ui];  // carry the ORIGINAL water, not the step before
            queue.emplace(candidate, nb.index);
        }
    }
    return out;
}

std::vector<double> flood_risk(const World& world, const SphereGrid& grid, const FreshwaterAccess& fresh) {
    const std::size_t count = grid.node_count();
    const std::vector<double> height = node_values(world, grid, "height");
    const std::vector<double> surface = node_values(world, grid, "water_surface");

    std::vector<double> out(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        // Freeboard is measured against the SOURCE's water surface, not the cell's own
        // ground: what matters is how far the ground stands above the water that would
        // reach it, and a cell with no reachable water has nothing to rise.
        const double level = fresh.source[i] >= 0 ? surface[static_cast<std::size_t>(fresh.source[i])] : 0.0;
        const double freeboard = std::max(0.0, height[i] - level);
        const double nearness = std::exp(-fresh.distance_m[i] / kFloodDistanceScale);
        out[i] = nearness * std::max(0.0, 1.0 - freeboard / kFloodFreeboard);
    }
    return out;
}

LandmassContext landmass_context(const SphereGrid& grid, const std::vector<double>& water) {
    const std::size_t count = grid.node_count();
    LandmassContext out;
    out.area_m2.assign(count, 0.0);
    out.fraction.assign(count, 0.0);
    out.largest.assign(count, 0);

    std::vector<char> seen(count, 0);
    std::vector<std::vector<int>> components;
    std::vector<double> areas;
    std::vector<int> stack;

    for (std::size_t start = 0; start < count; ++start) {
        if (seen[start] || water[start] != 0.0) continue;
        std::vector<int> members;
        stack.assign(1, static_cast<int>(start));
        seen[start] = 1;
        double area = 0.0;
        while (!stack.empty()) {
            const int i = stack.back();
            stack.pop_back();
            members.push_back(i);
            area += grid.areas[static_cast<std::size_t>(i)];
            for (const SphereNeighbour& nb : grid.neighbours[static_cast<std::size_t>(i)]) {
                const auto j = static_cast<std::size_t>(nb.index);
                if (seen[j] || water[j] != 0.0) continue;
                seen[j] = 1;
                stack.push_back(nb.index);
            }
        }
        components.push_back(std::move(members));
        areas.push_back(area);
    }

    double total = 0.0;
    double largest = 0.0;
    for (const double a : areas) {
        total += a;
        largest = std::max(largest, a);
    }
    for (std::size_t c = 0; c < components.size(); ++c) {
        for (const int i : components[c]) {
            const auto ui = static_cast<std::size_t>(i);
            out.area_m2[ui] = areas[c];
            out.fraction[ui] = total > 0.0 ? areas[c] / total : 0.0;
            // Compared by area, so two landmasses of exactly equal area are BOTH the
            // largest. That is the honest reading of "largest" and it matters for a
            // habitat rule that requires it.
            out.largest[ui] = areas[c] == largest ? 1 : 0;
        }
    }
    return out;
}

HabitatSample environment_at(const World& world, const SphereGrid& grid, std::size_t node,
                             const LandmassContext& context) {
    const int x = grid.point_x[node];
    const int z = grid.point_z[node];
    const int n = world.config.size;

    const auto layer = [&](const char* name) {
        const auto it = world.layers.find(name);
        return it != world.layers.end() ? it->second.at(x, z) : 0.0;
    };

    HabitatSample s;
    s.abs_latitude = std::abs(90.0 - 180.0 * z / (n - 1));
    s.biome = layer("biome");
    s.temperature = layer("temperature");
    s.moisture = layer("moisture");
    s.maritime = layer("maritime");
    s.slope = layer("slope");
    s.height = layer("height");
    s.tpi = layer("tpi");
    s.coastal_support = layer("coastal_support");
    s.landmass_area_m2 = context.area_m2[node];
    s.landmass_fraction = context.fraction[node];
    s.largest_landmass = context.largest[node] ? 1.0 : 0.0;

    // The exact magical state, where there is one. A habitat rule that tests `variant`
    // compares identities, so this must be the catalogue id rather than the index.
    const auto variant = world.layers.find("biome_variant");
    if (variant != world.layers.end()) {
        const int index = static_cast<int>(variant->second.at(x, z));
        const auto catalogue = biome_variant_catalogue();
        if (index >= 0 && static_cast<std::size_t>(index) < catalogue.size()) {
            s.variant = catalogue[static_cast<std::size_t>(index)].id;
        }
    }
    return s;
}

FarmingPotential farming_potential(double slope_deg, double temperature_c, double moisture, double flood,
                                   double freshwater_distance_m, double adaptation,
                                   const CivilizationTraits& t) {
    // Three independent ways ground can be bad for farming, multiplied rather than
    // averaged: steep, or the wrong temperature, or liable to flood, and any one of
    // them alone is enough to ruin it.
    const double steepness = slope_deg / t.food_slope_comfort;
    const double climate = std::max(0.0, 1.0 - std::abs(temperature_c - t.food_temperature_ideal) /
                                             t.food_temperature_tolerance);
    const double terrain = std::exp(-steepness * steepness) * climate * (1.0 - 0.7 * flood);

    const double natural =
        terrain * std::max(0.0, 1.0 - std::abs(moisture - t.food_moisture_ideal) / t.food_moisture_ideal);

    // No source means no irrigation, and irrigation only helps ground that is too DRY --
    // it cannot drain a swamp.
    const double access = freshwater_distance_m >= 0.0 && std::isfinite(freshwater_distance_m)
                              ? std::exp(-freshwater_distance_m / t.water_reach)
                              : 0.0;
    const double deficit = std::max(0.0, (t.food_moisture_ideal - moisture) / t.food_moisture_ideal);
    const double irrigation = adaptation * access * deficit;

    // Maintenance eats a quarter of what irrigation wins.
    return {natural, natural + 0.75 * terrain * irrigation};
}

CityCeiling city_ceiling(double habitable_km2, double settlement_spacing_m, double cell_spacing_m) {
    CityCeiling out;
    out.habitable_km2 = habitable_km2;
    // Cities cannot be packed closer than the raster can separate them.
    out.resolvable_spacing_m = std::max(settlement_spacing_m, cell_spacing_m);
    // The area each city needs at that separation, under the densest packing of the
    // plane -- hexagonal, sqrt(3)/2 per unit square. Not a circle: circles leave gaps
    // and would over-count.
    const double spacing_km = out.resolvable_spacing_m / 1000.0;
    out.per_city_km2 = std::sqrt(3.0) / 2.0 * spacing_km * spacing_km;
    out.packing = out.per_city_km2 > 0.0
                      ? static_cast<long long>(std::floor(habitable_km2 / out.per_city_km2))
                      : 0;
    return out;
}

long long city_limit(long long packing, const Config& cfg, int default_settlement_count) {
    // An explicit request for nothing outranks the ground.
    if (cfg.auto_parameters == 0 && cfg.settlement_count == 0) return 0;
    const bool asked_for_fewer = cfg.settlement_count < default_settlement_count;
    if (cfg.infers_own_settings() && !asked_for_fewer) return packing;
    return std::min(packing, static_cast<long long>(cfg.settlement_count));
}

void apply_city_ceiling(std::map<std::string, long long>& quotas, long long limit) {
    if (limit < 0) limit = 0;
    const auto total = [&] {
        long long sum = 0;
        for (const auto& [id, q] : quotas) sum += q;
        return sum;
    };
    while (total() > limit) {
        // Take from the largest quota, ties to the LAST name in order. A people whose
        // quota is already zero is never chosen, so this always terminates.
        auto victim = quotas.end();
        for (auto it = quotas.begin(); it != quotas.end(); ++it) {
            if (it->second <= 0) continue;
            if (victim == quotas.end() || it->second >= victim->second) victim = it;
        }
        if (victim == quotas.end()) break;  // nothing left to take
        --victim->second;
    }
}

double site_suitability(const CivilizationTraits& t, const SiteConditions& at, bool is_water) {
    if (is_water) return 0.0;  // nobody builds on the sea

    const double slope_term = at.slope_deg / t.slope_comfort;
    const double climate_term = (at.temperature_c - t.temperature_ideal) / t.temperature_tolerance;

    // A weighted sum of independent goods, less two penalties. The weights are the
    // people's own: a maritime culture weighs water differently from a mountain one.
    const double score = t.water_weight * std::exp(-at.freshwater_distance_m / t.water_reach) +
                         t.slope_weight * std::exp(-slope_term * slope_term) +
                         t.climate_weight * std::exp(-climate_term * climate_term) +
                         t.moisture_weight * std::max(0.0, 1.0 - std::abs(at.moisture - t.moisture_ideal)) +
                         t.resource_weight * at.resource - t.flood_penalty * at.flood + at.biome_opinion -
                         t.magic_penalty * at.magic_hazard;

    return std::max(0.0, std::min(1.0, score));
}

}  // namespace fwg
