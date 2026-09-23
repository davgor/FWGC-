// Connected ocean and equilibrium lake spill routing on the physical globe.
//
// This is where a heightfield becomes a hydrology. Three things come out of it that
// every later stage depends on: which cells are sea, which are lakes, and which way
// water runs — the receiver graph that rivers, settlement siting and sea routes are
// all built on.
//
// What it is NOT, and the world document says so in its own warnings: there is no
// rainfall budget and no time-dependent filling. A depression is filled to its spill
// level on the assumption that enough water exists to do it. Lake evaporation is not
// modelled and rivers have no width.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "fwg/grid.hpp"

namespace fwg {

// Per-node routing result. Indices are sphere_grid node indices throughout.
struct WaterRouting {
    std::vector<char> ocean;    // 1 where the node is part of the connected ocean
    std::vector<double> level;  // water surface: the spill level, >= ground
    std::vector<int> parent;    // receiver node; -1 is an outlet or the global minimum
    std::vector<double> flow;   // accumulated upstream area, m^2, uniform unit runoff
    std::vector<int> order;     // the order nodes were settled, lowest spill level first
};

// Largest connected below-sea component is the ocean; inland depressions are filled
// to their spill levels by priority flooding, then receivers are re-pointed to the
// steepest descent on the resulting spill surface.
WaterRouting route_water(const std::vector<double>& height, const SphereGrid& grid, double sea_level);

// What a world carries about its water, beside the layers.
struct WaterReport {
    int version = 1;
    std::vector<int> receivers;                    // per node, -1 at an outlet
    std::vector<std::array<int, 2>> river_segments; // {from, to} node pairs
    double ocean_km2 = 0.0;
    double lake_km2 = 0.0;
    double dry_km2 = 0.0;
    double lake_capacity_m3 = 0.0;
    bool has_ocean = false;
};

// Cell classification in the `water_type` layer.
enum WaterType : int { kDry = 0, kOcean = 1, kLake = 2 };

// A lake is any filled depth above this, in metres. Below it the cell is dry: the
// spill surface sits a rounding error above the ground almost everywhere, so without
// a floor every land cell would be a puddle.
inline constexpr double kLakeDepthEpsilon = 1e-7;

}  // namespace fwg
