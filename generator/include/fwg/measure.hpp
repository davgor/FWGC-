// Slope and topographic position, measured on the reference sphere.
//
// Both are sampled along great circles rather than across grid neighbours, which is
// what keeps them meaningful near the poles where the lat/lon grid degenerates.
// TPI here is a RING mean -- eight interpolated points at one geodesic radius -- not
// a planar square-area mean and not an area-weighted cap. A consumer that treats it
// as a cap mean will read the numbers wrong.
#pragma once

#include "fwg/grid.hpp"

namespace fwg {

struct Relief {
    Grid slope;  // degrees from the reference sphere's tangent plane
    Grid tpi;    // metres above the eight-point geodesic ring mean
};

Relief measure_globe(const Grid& height, double radius, double neighbourhood);

}  // namespace fwg
