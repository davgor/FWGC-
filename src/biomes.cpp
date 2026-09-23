#include "fwg/biomes.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <queue>
#include <utility>

#include "fwg/climate.hpp"
#include "fwg/noise.hpp"
#include "fwg/random.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

constexpr double kPi = std::numbers::pi;

constexpr const char* kLandformNames[] = {"Submerged", "Plain / hillside", "Mountain / ridge", "Valley"};

using Entry = std::pair<double, int>;
using MinHeap = std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>>;

}  // namespace

const char* landform_name(int form) {
    return (form >= 0 && form < 4) ? kLandformNames[form] : "Submerged";
}

int classify(double above_sea, double slope, double temperature, double moisture) {
    if (above_sea <= 0.0) return 0;   // ocean
    if (temperature <= 0.0) return 6; // snow
    if (slope >= 38.0) return 5;      // exposed rock: too steep to hold anything
    if (temperature < 5.0) return 1;  // tundra
    if (moisture < 0.3) return 2;     // desert
    if (temperature >= 20.0 && moisture >= 0.78) return 7;  // rainforest
    if (moisture >= 0.55) return 4;   // forest
    return 3;                         // grassland
}

int classify_landform(double above_sea, double tpi) {
    if (above_sea <= 0.0) return kSubmerged;
    if (above_sea > 30.0 && tpi > 12.0) return kMountain;
    if (tpi < -10.0) return kValley;
    return kPlain;
}

bool marsh_suitable(double wet, double temperature, double slope, double water_distance,
                    double height_above_water) {
    return wet >= 0.55 && temperature > 0.0 && slope < 6.0 && water_distance >= 0.0 &&
           water_distance <= kWetlandMarginM && height_above_water >= 0.0 && height_above_water <= 4.0;
}

WetlandAccess wetland_access(const World& world) {
    const int n = world.config.size;
    const auto held = sphere_grid(n, world.effective.globe_radius);
    const SphereGrid& grid = *held;
    const std::size_t count = grid.node_count();

    const Grid& type = world.layers.at("water_type");
    const Grid& ground = world.layers.at("height");
    const Grid& surface = world.layers.at("water_surface");
    // Rain rivers supersede the routed ones once climate has run: a marsh follows the
    // water the world actually has, not the water the heightfield alone implied.
    const Grid* river = nullptr;
    if (const auto it = world.layers.find("rain_river"); it != world.layers.end()) river = &it->second;
    else if (const auto jt = world.layers.find("river"); jt != world.layers.end()) river = &jt->second;

    std::vector<double> distance(count, std::numeric_limits<double>::infinity());
    std::vector<int> source(count, -1);
    MinHeap queue;

    for (std::size_t i = 0; i < count; ++i) {
        const int x = grid.point_x[i];
        const int z = grid.point_z[i];
        const bool is_water = type.at(x, z) != 0.0 || (river && river->at(x, z) != 0.0);
        if (!is_water) continue;
        distance[i] = 0.0;
        source[i] = static_cast<int>(i);
        queue.emplace(0.0, static_cast<int>(i));
    }

    // Dijkstra with a hard radius. Bounding the search INSIDE the relaxation rather
    // than filtering afterwards is what keeps this cheap: nothing beyond the margin is
    // ever pushed.
    while (!queue.empty()) {
        const auto [d, i] = queue.top();
        queue.pop();
        const auto ui = static_cast<std::size_t>(i);
        if (d != distance[ui]) continue;  // stale entry
        for (const SphereNeighbour& nb : grid.neighbours[ui]) {
            const double candidate = d + nb.distance;
            const auto j = static_cast<std::size_t>(nb.index);
            if (candidate > kWetlandMarginM || candidate >= distance[j]) continue;
            distance[j] = candidate;
            source[j] = source[ui];  // carry the ORIGINAL water, not the step before
            queue.emplace(candidate, nb.index);
        }
    }

    std::vector<double> reported(count);
    std::vector<double> relief(count);
    for (std::size_t i = 0; i < count; ++i) {
        reported[i] = std::isfinite(distance[i]) ? distance[i] : -1.0;
        const int s = source[i] >= 0 ? source[i] : static_cast<int>(i);
        const auto us = static_cast<std::size_t>(s);
        relief[i] = ground.at(grid.point_x[i], grid.point_z[i]) -
                    surface.at(grid.point_x[us], grid.point_z[us]);
    }

    WetlandAccess out;
    out.distance_m = grid.to_grid(reported);
    out.height_above_water_m = grid.to_grid(relief);
    return out;
}

void add_terrain_labels(World& world) {
    const Config& cfg = world.effective;
    const int n = cfg.size;

    const Grid& height = world.layers.at("height");
    const Grid& slope = world.layers.at("slope");
    const Grid& tpi = world.layers.at("tpi");

    const bool has_water = world.layers.count("water_type") != 0;
    const Grid* water = has_water ? &world.layers.at("water_type") : nullptr;
    const Grid* moisture_layer =
        world.layers.count("climate_moisture") ? &world.layers.at("climate_moisture") : nullptr;

    WetlandAccess wetland;
    const bool has_wetland = has_water;
    if (has_wetland) wetland = wetland_access(world);

    const std::uint32_t climate_seed = child_seed(cfg.seed, "climate");

    Grid temperature(n), moisture(n), biome(n), landform(n);

    struct Candidate {
        double score;
        int kind;  // index into kKinds
        int x;
        int z;
        Vec3 direction;
    };
    std::vector<Candidate> candidates;

    static constexpr const char* kKinds[] = {"marsh", "mountain", "forest", "desert", "snow"};
    static constexpr const char* kLabels[] = {"Wetland margin", "Prominent ridge", "Forest region",
                                              "Dry region", "Snow region"};

    std::vector<double> t_row, m_row, b_row, f_row;
    for (int z = 0; z < n; ++z) {
        const int count = distinct_count(z, n);
        t_row.assign(static_cast<std::size_t>(count), 0.0);
        m_row.assign(static_cast<std::size_t>(count), 0.0);
        b_row.assign(static_cast<std::size_t>(count), 0.0);
        f_row.assign(static_cast<std::size_t>(count), 0.0);

        for (int x = 0; x < count; ++x) {
            const Vec3 p = direction(x, z, n);
            const double above = height.at(x, z) - cfg.sea_level;
            const double temp = surface_temperature(p, height.at(x, z), cfg.sea_level, cfg.temperature_offset);

            // Before climate exists, wetness is a latitude band plus seeded noise.
            // Once it does, the transported rainfall supersedes it entirely.
            double wet;
            if (moisture_layer) {
                wet = moisture_layer->at(x, z);
            } else {
                const double banded = 0.5 + 0.22 * std::cos(3.0 * std::asin(clamp_unit(p.y))) +
                                      0.9 * perlin3(p.x * 2.3, p.y * 2.3, p.z * 2.3, climate_seed) +
                                      cfg.moisture_bias;
                wet = std::max(0.0, std::min(1.0, banded));
            }

            const double cell_slope = slope.at(x, z);
            const double cell_tpi = tpi.at(x, z);
            int label = classify(above, cell_slope, temp, wet);

            const int water_kind = water ? static_cast<int>(water->at(x, z)) : 0;
            if (water_kind == 1) label = 0;       // ocean overrides everything
            else if (water_kind == 2) label = 8;  // lake

            if (has_wetland && water_kind == 0 && above > 0.0 &&
                wetland.distance_m.at(x, z) >= 0.0 &&
                marsh_suitable(wet, temp, cell_slope, wetland.distance_m.at(x, z),
                               wetland.height_above_water_m.at(x, z))) {
                label = 13;
            }

            const int form = classify_landform(above, cell_tpi);

            t_row[static_cast<std::size_t>(x)] = temp;
            m_row[static_cast<std::size_t>(x)] = wet;
            b_row[static_cast<std::size_t>(x)] = label;
            f_row[static_cast<std::size_t>(x)] = form;

            // Feature candidates: dry land only, and never a pole, which is one node
            // standing in for a whole cap and would anchor a marker nowhere.
            if (above > 0.0 && water_kind == 0 && z != 0 && z != n - 1) {
                int kind = -1;
                double score = 0.0;
                if (label == 13) { kind = 0; score = -temp; }
                else if (form == kMountain) { kind = 1; score = cell_tpi + above * 0.1; }
                else if (label == 4 || label == 7) { kind = 2; score = wet; }
                else if (label == 2) { kind = 3; score = 1.0 - wet; }
                else if (label == 6) { kind = 4; score = -temp; }
                if (kind >= 0) candidates.push_back({score, kind, x, z, p});
            }
        }

        temperature.fill_row(z, t_row);
        moisture.fill_row(z, m_row);
        biome.fill_row(z, b_row);
        landform.fill_row(z, f_row);
    }

    world.layers["temperature"] = temperature;
    world.layers["moisture"] = moisture;
    world.layers["biome"] = biome;
    world.layers["landform"] = landform;
    world.layers["natural_biome"] = biome;
    if (has_wetland) {
        world.layers["water_distance"] = wetland.distance_m;
        world.layers["height_above_water"] = wetland.height_above_water_m;
    }

    // Balanced per-kind selection. Sorted once, descending, with a total order so the
    // choice cannot depend on the sort's internals.
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.kind != b.kind) return a.kind > b.kind;
        if (a.x != b.x) return a.x > b.x;
        return a.z > b.z;
    });

    TerrainReport report;
    report.climate_seed = climate_seed;
    const double separation = std::cos(kFeatureSeparationRad);
    for (int kind = 0; kind < 5; ++kind) {
        int taken = 0;
        for (const Candidate& c : candidates) {
            if (c.kind != kind) continue;
            // Separation is checked against EVERY selected feature, not just this
            // kind's, so a ridge and a forest never mark the same place.
            const bool crowded = std::any_of(report.features.begin(), report.features.end(),
                                             [&](const TerrainFeature& f) {
                                                 return dot(c.direction, f.direction) > separation;
                                             });
            if (crowded) continue;
            report.features.push_back({kKinds[kind], kLabels[kind], c.x, c.z, c.direction,
                                       height.at(c.x, c.z)});
            if (++taken == kFeaturesPerKind) break;
        }
    }

    report.method = moisture_layer
        ? "Temperature remains a latitude/elevation proxy. Biome moisture follows wind-transport rainfall; "
          "lake extents still assume equilibrium filling."
        : "Temperature: latitude and elevation proxy. Moisture: latitude and seeded spatial noise. No "
          "prevailing winds, rainfall or rain shadows simulated.";
    if (has_wetland) {
        report.method +=
            " Marsh is wet, unfrozen, gently sloping land within 180 m and 4 m above mapped water; no soil "
            "saturation simulation.";
    }

    world.terrain = report;
    world.has_terrain = true;
    world.systems.push_back("terrain_labels");
    world.warnings.push_back(report.method);
}

}  // namespace fwg
