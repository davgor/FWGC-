// Steady-wind moisture transport: a deterministic experiment, not a forecast.
//
// A fixed compass bearing carries humidity across the globe. Air picks moisture up
// over ocean and lake, and drops it where the ground rises into it — which is what
// puts rain on the windward side of a range and a dry shadow behind it. Iterated to
// a fixed pass count, not to convergence.
//
// The numbers are RELATIVE proxies. `rainfall` is not mm/year and `rain_runoff` is
// not discharge; lake volumes are not solved from this budget. What they are good
// for is comparing one place with another in the same world.
#pragma once

#include <array>
#include <vector>

#include "fwg/grid.hpp"
#include "fwg/vec3.hpp"

namespace fwg {

// The four upwind cells a node's air arrives from, with bilinear weights summing to 1.
struct Stencil {
    int node[4] = {0, 0, 0, 0};
    double weight[4] = {0.0, 0.0, 0.0, 0.0};
};

// One upwind step of one cell along `bearing` (radians clockwise from north),
// resolved into the bilinear stencil that samples it.
std::vector<Stencil> upwind_stencils(const SphereGrid& grid, double bearing);

struct MoistureField {
    std::vector<double> humidity;  // air moisture left after the final pass
    std::vector<double> rain;      // moisture dropped on the final pass
    std::vector<double> uplift;    // how far the ground rises into the arriving air, m
    double residual = 0.0;         // largest humidity change on the final pass
};

// `surface` is the WATER SURFACE, not the ground: air rides over a lake at its spill
// level, not over the basin floor beneath it.
MoistureField transport_moisture(const std::vector<double>& surface, const std::vector<char>& is_water,
                                 const std::vector<Stencil>& upwind, int passes, double strength);

// Rainfall to a 0..1 wetness, against a temperature-dependent demand: the same rain
// leaves a hot place drier than a cold one.
double climate_wetness(double rain, double temperature);

// Lapsed surface temperature in Celsius at a node.
double surface_temperature(const Vec3& p, double height_m, double sea_level_m, double offset_c);

struct WindArrow {
    int x = 0;
    int z = 0;
    Vec3 at;
    Vec3 wind;
};

struct ClimateReport {
    int version = 1;
    std::vector<WindArrow> wind_arrows;
    std::vector<std::array<int, 2>> river_segments;
    double residual = 0.0;
    int passes = 0;
    double transport_step_m = 0.0;
    double runoff_input_m2_equivalent = 0.0;
    double runoff_outlets_m2_equivalent = 0.0;
};

}  // namespace fwg
