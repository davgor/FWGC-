// The ground under a settlement, castle or hamlet, sampled in LOCAL METRES.
//
// A gnomonic tangent-plane frame at one raster node, with the routed river centrelines
// that reach a window projected into it. The city, hamlet and castle planners all read
// their ground through this one frame. The reference kept two copies -- the castle
// planner's on purpose, so it would not import the city's -- and the port briefly did the
// same; the copies had already drifted apart on where the river lines come from, which is
// how two samplers that must agree stop agreeing.
//
// Every metre constant here is LOCAL to a window at most a few hundred metres across and
// none of them scales with the planet: a river's setback is the same width on a small
// world and a large one.
#pragma once

#include <optional>
#include <vector>

#include "fwg/city_plan.hpp"  // Point
#include "fwg/detail.hpp"     // HeightField
#include "fwg/vec3.hpp"

namespace fwg {

struct World;
struct SphereGrid;
class Grid;

struct LocalSample {
    bool water = false;
    double slope = 0.0;  // degrees, by central difference over two metres
    int channel = 0;     // 1 inside a routed river's reserved channel
    double flood_risk = 0.0;
    double height = 0.0;
    double moisture = 0.5;
    int biome = 3;
    int variant = -1;
};

class LocalSampler {
public:
    // `radius_m` is the EFFECTIVE globe radius and `grid` must be built at it: a
    // design-space radius puts every river line in the wrong place. The frame's origin is
    // the raster node (node_x, node_z); river centrelines whose box misses the window of
    // half-width `half` by more than the setback padding are dropped.
    //
    // River lines come from the CLIMATE block's segments when climate has run and from
    // the water block's otherwise, as the reference's planners read them.
    LocalSampler(const World& world, const SphereGrid& grid, double radius_m, double node_x, double node_z,
                 double half);

    // Metres per raster step along a meridian.
    double resolution() const;
    double radius() const { return radius_; }
    const Vec3& up() const { return up_; }
    const Vec3& east() const { return east_; }
    const Vec3& north() const { return north_; }

    Vec3 direction_at(double x, double z) const;
    // The same metre sample() computes, without the river loop and the layer reads. The
    // height is a pure function of the direction, so this is the identical number.
    double height_at(double x, double z) const;
    LocalSample sample(double x, double z) const;

private:
    struct Line {
        Point a;
        Point b;
    };

    std::optional<Point> project(const SphereGrid& grid, int node) const;
    double river_distance(double x, double z) const;

    HeightField field_;
    int n_;
    double radius_;
    Vec3 up_{};
    Vec3 east_{};
    Vec3 north_{};
    const Grid* water_type_ = nullptr;
    const Grid* river_ = nullptr;
    const Grid* flood_ = nullptr;
    const Grid* moisture_ = nullptr;
    const Grid* biome_ = nullptr;
    const Grid* variant_ = nullptr;
    std::vector<Line> lines_;
};

}  // namespace fwg
