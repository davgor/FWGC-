// Stages 6 and 7: a second tectonic epoch, and the scars the old water left.
//
// Everything before this is one snapshot of a planet. These two stages are the first
// time the world has a PAST — a surface that used to be somewhere else, and river beds
// and lake floors that are now dry ground you can walk through.
//
// Stage 6 rotates every plate along its own angular velocity for a dimensionless 0.65
// of turn — an artistic fifty million years — backtraces each cell's crust to where it
// came from, and blends the transported relief with fresh boundary uplift.
//
// Stage 7 finds the waterways that epoch abandoned and cuts them into the ground, so a
// drained river reads as a gorge and a drained lake as a dry valley.
#pragma once

#include <vector>

#include "fwg/grid.hpp"
#include "fwg/tectonics.hpp"

namespace fwg {

struct World;

// The surface as it stood before an epoch: what the relic pass compares against.
struct SurfaceSnapshot {
    Grid height;
    Grid river;
    Grid water_type;
    Grid water_depth;
};

struct GeologicalEpoch {
    int version = 1;
    int elapsed_million_years = 50;
    double angular_duration = 0.0;
    std::vector<Plate> plates_before;
    std::vector<Plate> plates_after;
};

// Re-measure slope, TPI, the land mask and the area report from the current height.
// Called after anything that rewrites the surface.
void measure_surface(World& world);

// Stage 6. Returns the pre-epoch surface, which stage 7 needs.
SurfaceSnapshot tectonic_transition(World& world);

// Which cells used to carry water and no longer do. Gorges are abandoned river beds;
// valleys are drained lake and sea floors.
struct AbandonedWaterways {
    Grid gorges;
    Grid valleys;
};
AbandonedWaterways abandoned_waterways(const SurfaceSnapshot& old, const World& world);

// Stage 7. Incises the abandoned beds, then re-routes water over the new surface.
void carve_relics(World& world, const SurfaceSnapshot& old);

// How far the plates turn in the epoch. Dimensionless: angular speed times this is an
// angle, and the speeds are not calibrated geodynamics.
inline constexpr double kEpochDuration = 0.65;

}  // namespace fwg
