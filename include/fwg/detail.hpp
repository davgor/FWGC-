// Continuous, position-addressed local relief.
//
// Everything else in this library samples the world on its grid. This is the piece an
// ENGINE uses: a height function evaluable at any point on the sphere, at any level of
// detail, that always agrees with itself and with `layers.height`.
//
// Two properties make it usable as a terrain source:
//
//   * **Level of detail never changes the height function.** Sampling the same
//     direction at two tile levels gives the same metre. There is no LOD blending and
//     no seam, because there is nothing to blend between.
//   * **Detail fades out where it would lie.** Water, shorelines, river channels and
//     flood-prone ground are protected: the added relief is zeroed there rather than
//     roughening a lake surface or a river bed.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fwg/grid.hpp"
#include "fwg/vec3.hpp"

namespace fwg {

struct DetailBand {
    double scale_m = 0.0;
    double amplitude_m = 0.0;
    std::uint32_t seed = 0;
};

struct TerrainDetail {
    int version = 1;
    std::uint32_t seed = 0;
    std::vector<DetailBand> bands;
};

struct World;

// The detail definition a world publishes. Deterministic from the world seed.
TerrainDetail make_terrain_detail(std::uint32_t world_seed);

// A sampler bound to one world. Holds references into the world's layers, so it must
// not outlive the world it was built from.
class HeightField {
public:
    explicit HeightField(const World& world);

    // Metres above the reference sphere, at a unit direction. Same datum as
    // `layers.height`.
    double height(const Vec3& direction) const;

    const TerrainDetail& detail() const { return detail_; }
    double radius() const { return radius_; }

    // The protection mask: 0 where detail is fully suppressed, 1 where it is free.
    const Grid& strength() const { return strength_; }

private:
    struct Cell {
        int ix, iz, jz;
        double fx, fz;
    };
    Cell locate(const Vec3& p) const;
    static double bilinear(const Grid& g, const Cell& c);

    const Grid* base_ = nullptr;
    const Grid* water_ = nullptr;
    Grid strength_;
    Grid water_owned_;
    double radius_ = 0.0;
    double sea_level_ = 0.0;
    int n_ = 0;
    TerrainDetail detail_;
};

struct HeightTile {
    int level = 0;
    int origin_x = 0;
    int origin_z = 0;
    int size = 0;               // cells + 1: tiles share their edges
    int global_cols = 0;
    int global_rows = 0;
    double radius_m = 0.0;
    double angular_step_degrees = 0.0;
    double equatorial_spacing_m = 0.0;
    std::string source_sha256;  // over the inputs, so two tiles from one world agree
    std::vector<std::vector<double>> heights_m;  // row-major
};

// Indexed latitude/longitude samples. Shared indices between neighbouring tiles yield
// identical heights, which is what makes the tiles seamless.
//
// Throws std::invalid_argument for a level outside 1..24, a cell count outside 1..256,
// or a tile that runs off the global grid.
HeightTile export_height_tile(const World& world, int level, int x, int z, int cells = 64);

}  // namespace fwg
