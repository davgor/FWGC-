// Layered habitats on the spherical grid. Every quantity here is an explicit proxy.
//
// Three separate jobs share this file because the reference shares them:
//
//   * `shape_islands` raises ocean archipelagos into the tectonic surface, BEFORE
//     erosion and drainage, so islands are eroded and drained like any other land.
//   * `add_environment` derives the coastal and marine habitat fields, relabels cold
//     ground, and lays down the thirteen magical regions.
//   * the cold pass is what actually puts boreal forest, cold tundra and land ice on
//     the map: `classify` never emits them.
#pragma once

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/config.hpp"
#include "fwg/grid.hpp"
#include "fwg/vec3.hpp"

namespace fwg {

struct Island {
    Vec3 direction;
    double angular_radius = 0.0;
};

struct Archipelago {
    std::string id;
    std::string kind;  // volcanic | atoll | continental | cold
    Vec3 direction;
    std::vector<Island> islands;
    bool eligible = false;  // only clusters whose centre falls on ocean floor manifest
};

// Seeded cluster layout. Independent of the terrain: eligibility is decided later.
std::vector<Archipelago> island_layout(const Config& cfg);

// Raises the clusters into `structure`, `height` and `base`, and records the lift in
// `archipelago_relief`. Returns the clusters with eligibility resolved.
std::vector<Archipelago> shape_islands(std::map<std::string, Grid>& layers, const Config& cfg);

// Twelve monthly means around an annual cycle. Amplitude grows with latitude and
// falls with moisture: a wet coast swings less than a dry interior.
std::vector<double> monthly_temperatures(double mean, double latitude_deg, double wet, double seasonality);

// Which cold habitat, if any, a year of those temperatures supports. Empty string
// means "not cold enough to relabel".
std::string cold_habitat(const std::vector<double>& temps, double wet, double accumulation);

struct RegionInfluence {
    std::string id;
    std::vector<int> centers;
    double maximum = 0.0;
    double area_km2 = 0.0;
    std::string reason;
};

struct Landmark {
    std::string id;
    std::string kind;
    int x = 0;
    int z = 0;
    std::string layer;
    double intensity = 0.0;
};

struct EcologyReport {
    std::vector<RegionInfluence> regions;
    std::vector<Landmark> landmarks;
    std::vector<Archipelago> archipelagos;
};

// The thirteen magical regions, in the order the reference declares them.
std::span<const std::string_view> zone_names();

}  // namespace fwg
