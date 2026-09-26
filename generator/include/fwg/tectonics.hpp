// Static tectonic stages: a game model, not geodynamics.
//
// Plate motion is dimensionless and does not evolve in time. What the model buys is
// that mountains, trenches, rifts and volcanic arcs all fall out of the same plate
// geometry, so a world's mountains answer to its oceans instead of being painted on
// independently.
//
// Nearby plate pairs blend continuously -- a cell near three plates takes a weighted
// sum of all three pairwise interactions -- while plate identity itself stays
// categorical.
#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "fwg/config.hpp"
#include "fwg/ecology.hpp"
#include "fwg/grid.hpp"
#include "fwg/vec3.hpp"

namespace fwg {

struct Plate {
    int id = 0;
    Vec3 center;
    Vec3 omega;  // dimensionless angular velocity
};

// Poisson-ish rejection sampling: centres are drawn uniformly on the sphere and
// refused if they crowd an existing plate, so plates come out comparable in size
// without being laid out on a lattice.
std::vector<Plate> make_plates(std::uint64_t seed, int count);

// Continental affinity, independent of plate identity -- one plate can carry both
// continental and oceanic crust, as Earth's do.
double crust_at(const Vec3& p, std::uint32_t seed, double bias);

// Hypsometric curve: ocean floor sits far below continental platform, so raising
// the relief budget deepens basins as fast as it raises peaks.
double continental_profile(double c);

// Everything the plate model says about one cell. `structure` and `interaction` are
// scaled by tectonic_relief; the motion terms are dimensionless and scale-invariant.
struct PointSample {
    double owner = 0.0;
    double crust = 0.0;
    double boundary_distance = 0.0;
    double convergence = 0.0;
    double divergence = 0.0;
    double shear = 0.0;
    double structure = 0.0;
    double interaction = 0.0;
    double volcanic = 0.0;
};

// How much a plate still holds a point it does not own. `behind` is how far its score
// trails the nearest plate's, in score units (dot products of the point with the plate
// centres); `support` is how far behind a plate may trail and still count. The nearest
// plate holds 1, and the rest fall off cubically to 0 at `support`. Continuous across
// every boundary: where two plates tie, both hold 1.
inline double plate_membership(double behind, double support) {
    const double falloff = std::max(0.0, 1.0 - behind / support);
    return falloff * falloff * falloff;
}

// Evaluate the plate model at one direction. Used per cell by the tectonic stage and
// again by the second epoch at stage 6.
PointSample layout_point(const Vec3& p, const std::vector<Plate>& plates, std::uint32_t crust_seed,
                         const Config& cfg);

struct TectonicResult {
    std::map<std::string, Grid> layers;
    std::vector<Plate> plates;
    std::vector<Archipelago> archipelagos;
    std::uint64_t layout_seed = 0;  // an RNG stream seed, so 64-bit
    std::uint32_t crust_seed = 0;
    std::uint32_t detail_seed = 0;
    int resolved_octaves = 0;
    double spacing_m = 0.0;
    double effective_tpi_radius_m = 0.0;
    std::vector<std::string> warnings;

    // Present only from phase 4.
    bool has_sediment_budget = false;
    double eroded_m3 = 0.0;
    double deposited_m3 = 0.0;
    double stored_sediment_m3 = 0.0;
    double balance_error_m3 = 0.0;
};

TectonicResult generate_tectonics(const Config& cfg);

}  // namespace fwg
