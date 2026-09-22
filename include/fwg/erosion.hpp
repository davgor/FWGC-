// Conservative downhill sediment transport on the spherical node grid.
//
// Each node routes to its steepest downhill neighbour, discharge accumulates down
// those routes, and a stream-power proxy decides how much material a reach can cut
// and how much it has to drop. Closed depressions and submerged cells retain their
// load -- this does NOT simulate lake overflow or rainfall, and the water routing
// that runs later does not rerun sediment transport.
//
// The budget returned is a real conservation check: eroded minus deposited minus
// stored should be zero, and `balance_error_m3` is how far off it is.
#pragma once

#include "fwg/grid.hpp"

namespace fwg {

struct ErosionResult {
    Grid height;
    Grid erosion;        // metres cut, cumulative over passes
    Grid deposition;     // metres dropped, cumulative over passes
    Grid erosion_delta;  // net change against the pre-erosion surface
    Grid catchment;      // upstream area draining through each cell

    double eroded_m3 = 0.0;
    double deposited_m3 = 0.0;
    double stored_sediment_m3 = 0.0;
    double balance_error_m3 = 0.0;
};

ErosionResult erode(const Grid& height, double radius, double sea_level, int passes, double strength);

}  // namespace fwg
