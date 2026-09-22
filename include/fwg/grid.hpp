// The latitude/longitude sampling grid and the spherical node graph built from it.
//
// Every layer in a world is an n x n grid of doubles with two redundancies that the
// consumer relies on and that the generator must maintain:
//
//   * column n-1 duplicates column 0 (the longitude seam), and
//   * rows 0 and n-1 are a single pole value repeated n times.
//
// So a row holds `distinct_count(z, n)` independent values and the rest is
// bookkeeping. Grid stores the full n x n so that output shape and indexing stay
// obvious; `fill_row` is how a computed row becomes a stored one.
#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "fwg/vec3.hpp"

namespace fwg {

// Unit-sphere direction of grid cell (x, z) on an n x n lat/lon grid.
Vec3 direction(int x, int z, int n);

// Independent cells in row z: 1 at either pole, n-1 elsewhere.
inline int distinct_count(int z, int n) { return (z == 0 || z == n - 1) ? 1 : n - 1; }

class Grid {
public:
    Grid() = default;
    Grid(int n, double value) : n_(n), cells_(static_cast<std::size_t>(n) * static_cast<std::size_t>(n), value) {}
    explicit Grid(int n) : Grid(n, 0.0) {}

    int size() const { return n_; }
    bool empty() const { return n_ == 0; }

    double& at(int x, int z) { return cells_[index(x, z)]; }
    double at(int x, int z) const { return cells_[index(x, z)]; }

    const std::vector<double>& cells() const { return cells_; }
    std::vector<double>& cells() { return cells_; }

    // Expand `values` -- one entry per distinct cell of row z -- across the full row,
    // repeating a pole value and closing the seam.
    void fill_row(int z, const std::vector<double>& values);

private:
    std::size_t index(int x, int z) const {
        return static_cast<std::size_t>(z) * static_cast<std::size_t>(n_) + static_cast<std::size_t>(x);
    }

    int n_ = 0;
    std::vector<double> cells_;
};

// Area of ONE longitudinal wedge of the latitude band centred on row z.
//
// Shared deliberately. This was written out twice -- once to weight the erosion
// node graph, once to weight the land-area integral -- and two copies of a formula
// that must agree is how the two silently stop agreeing.
double band_wedge_area(int z, int n, double radius);

// Bilinear sample of a height grid at an arbitrary point on the sphere.
double sample(const Grid& h, const Vec3& p);

// The node graph the hydrology runs on: one node per distinct cell, with each
// node's spherical cell area and its great-circle distance to each neighbour.
//
// Pole nodes are single nodes adjacent to the whole ring below (or above) them and
// carry the area of that whole ring, which is why they are not just another cell.
struct SphereNeighbour {
    int index = 0;
    double distance = 0.0;
};

struct SphereGrid {
    int n = 0;
    double radius = 0.0;
    std::vector<int> point_x;
    std::vector<int> point_z;
    std::vector<double> areas;
    std::vector<std::vector<SphereNeighbour>> neighbours;

    std::size_t node_count() const { return areas.size(); }

    // (x, z) -> node index, with the poles collapsed to one node each and longitude
    // wrapped. x may be negative or past the seam; z is clamped to the poles.
    int node_index(int x, int z) const;

    // Scatter one value per node back into an n x n grid, closing the seam and the poles.
    Grid to_grid(const std::vector<double>& values) const;
};

// Built once per (n, radius); the result is immutable and shared.
//
// Returns a shared_ptr, NOT a reference. The cache evicts, so a reference handed out
// here dangles as soon as two other grids are requested -- which no stage does today
// and several of stages 6-16 will. The caller holding ownership makes that impossible
// rather than merely unlikely.
std::shared_ptr<const SphereGrid> sphere_grid(int n, double radius);

}  // namespace fwg
