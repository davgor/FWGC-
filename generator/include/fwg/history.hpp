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

// Stage 10's first half: mark every cell with the exact magical state of its ground.
//
// `biome_variant` is a flat index into the 156-entry magical catalogue, or -1 where no
// school leads clearly enough to have claimed the ground. It is an INDEX, while `biome`
// and `natural_biome` are catalogue IDENTITIES -- confusing the two silently reads a
// different biome, which is why the world document states the contract out loud.
void add_biome_variants(World& world);

// How far the plates turn in the epoch. Dimensionless: angular speed times this is an
// angle, and the speeds are not calibrated geodynamics.
inline constexpr double kEpochDuration = 0.65;

// How far behind the nearest moved plate another plate may trail and still carry some of
// its ground to a cell, as a fraction of belt_width in plate-score units (see
// plate_membership). Half a belt. Seed 2660691850 at size 513 was measured at three widths.
// A quarter of a belt left the old boundaries as straight ramps in the sea floor. A whole
// belt blurred two plates' relief together over a third of the map. Half a belt changed 20%
// of that world's cells, all of them near a boundary, and left every other cell bit for bit.
inline constexpr double kCarriedBlend = 0.5;

// The ground the epoch carries to direction p, before the untouched quarter and the new
// uplift are added. Every moved plate that still holds p backtraces its own rotation into
// `old_height`, weighed by plate_membership within `support` (score units) of the nearest.
// Continuous across every moved-plate boundary. Taking the nearest plate's backtrace alone
// put a cliff there, because the two sides of a boundary come from far-apart ground.
double carried_height(const Grid& old_height, const Vec3& p, const std::vector<Plate>& moved, double support);

}  // namespace fwg
