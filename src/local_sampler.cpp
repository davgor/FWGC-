#include "fwg/local_sampler.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>

#include "fwg/grid.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

constexpr double kPi = 3.14159265358979323846;
// Within this of a routed river centreline a point is water outright...
constexpr double kRiverWaterM = 12.0;
// ...and within this, on a river cell, it is the reserved channel and setback.
constexpr double kRiverChannelM = 28.0;
// A centreline whose box misses the window by more than this is dropped: it covers the
// 28 m setback plus slack, so a centreline just outside the window still reserves ground
// inside it.
constexpr double kRiverPadM = 40.0;
// A river node more than about 84 degrees away projects to infinity.
constexpr double kHorizonDen = 0.1;

double degrees_of(double rad) { return rad * 180.0 / kPi; }

const Grid* layer(const World& world, const char* name) {
    const auto found = world.layers.find(name);
    return found == world.layers.end() ? nullptr : &found->second;
}

}  // namespace

LocalSampler::LocalSampler(const World& world, const SphereGrid& grid, double radius_m, double node_x,
                           double node_z, double half)
    : field_(world), n_(grid.n), radius_(radius_m) {
    const double lat = kPi / 2.0 - kPi * node_z / (n_ - 1);
    const double lon = -kPi + 2.0 * kPi * node_x / (n_ - 1);
    up_ = {std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon)};
    east_ = {-std::sin(lon), 0.0, std::cos(lon)};
    north_ = {-std::sin(lat) * std::cos(lon), std::cos(lat), -std::sin(lat) * std::sin(lon)};

    water_type_ = layer(world, "water_type");
    river_ = layer(world, "river");
    flood_ = layer(world, "flood_risk");
    moisture_ = layer(world, "moisture");
    biome_ = layer(world, "natural_biome");
    variant_ = layer(world, "biome_variant");

    const std::vector<std::array<int, 2>>* segments =
        world.has_climate ? &world.climate.river_segments : (world.has_water ? &world.water.river_segments : nullptr);
    if (segments == nullptr) return;
    for (const auto& segment : *segments) {
        const auto a = project(grid, segment[0]);
        const auto b = project(grid, segment[1]);
        if (!a || !b) continue;
        const bool overlaps_x =
            std::min(a->x, b->x) <= half + kRiverPadM && std::max(a->x, b->x) >= -half - kRiverPadM;
        const bool overlaps_z =
            std::min(a->z, b->z) <= half + kRiverPadM && std::max(a->z, b->z) >= -half - kRiverPadM;
        if (overlaps_x && overlaps_z) lines_.push_back({*a, *b});
    }
}

double LocalSampler::resolution() const { return kPi * radius_ / (n_ - 1); }

Vec3 LocalSampler::direction_at(double x, double z) const {
    const Vec3 p{up_.x + (east_.x * x + north_.x * z) / radius_, up_.y + (east_.y * x + north_.y * z) / radius_,
                 up_.z + (east_.z * x + north_.z * z) / radius_};
    const double length = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    return {p.x / length, p.y / length, p.z / length};
}

double LocalSampler::height_at(double x, double z) const { return field_.height(direction_at(x, z)); }

LocalSample LocalSampler::sample(double x, double z) const {
    const Vec3 p = direction_at(x, z);
    int gx = static_cast<int>(std::llround((std::atan2(p.z, p.x) + kPi) / (2.0 * kPi) * (n_ - 1))) % (n_ - 1);
    if (gx < 0) gx += n_ - 1;
    // clamp_unit, not a bare asin: rounding pushes a unit-vector component a few ulp past
    // one often enough, and the NaN that follows indexes the grid at random.
    const int gz = static_cast<int>(std::clamp(std::round((kPi / 2.0 - std::asin(clamp_unit(p.y))) / kPi * (n_ - 1)),
                                               0.0, static_cast<double>(n_ - 1)));

    const double distance = river_distance(x, z);
    LocalSample out;
    // Real local slope by central difference over two metres. The reference reads the
    // coarse slope layer only for a world with no continuous relief definition, and every
    // world here has one: the height field derives it from the seed. Reading the coarse
    // layer would contradict the very heights a plan goes on to emit.
    const double dx = (height_at(x + 1.0, z) - height_at(x - 1.0, z)) / 2.0;
    const double dz = (height_at(x, z + 1.0) - height_at(x, z - 1.0)) / 2.0;
    out.slope = degrees_of(std::atan(std::hypot(dx, dz)));
    // `channel` is local: this point is inside the reserved channel of a routed
    // centreline. `flood_risk` is the REGIONAL proxy, one value for a raster cell
    // kilometres across, so it is the same number everywhere in a window and could only
    // ever block all of it or none of it.
    const double river_strength = river_ != nullptr ? river_->at(gx, gz) : 0.0;
    out.channel = (!lines_.empty() && river_strength > 0.5 && distance < kRiverChannelM) ? 1 : 0;
    const double water_type = water_type_ != nullptr ? water_type_->at(gx, gz) : 0.0;
    out.water = water_type != 0.0 || distance < kRiverWaterM;
    out.flood_risk = flood_ != nullptr ? flood_->at(gx, gz) : 0.0;
    out.height = field_.height(p);
    out.moisture = moisture_ != nullptr ? moisture_->at(gx, gz) : 0.5;
    out.biome = biome_ != nullptr ? static_cast<int>(std::lround(biome_->at(gx, gz))) : 3;
    out.variant = variant_ != nullptr ? static_cast<int>(std::lround(variant_->at(gx, gz))) : -1;
    return out;
}

std::optional<Point> LocalSampler::project(const SphereGrid& grid, int node) const {
    if (node < 0 || static_cast<std::size_t>(node) >= grid.node_count()) return std::nullopt;
    const Vec3 p = direction(grid.point_x[static_cast<std::size_t>(node)], grid.point_z[static_cast<std::size_t>(node)], n_);
    const double den = dot(p, up_);
    if (den <= kHorizonDen) return std::nullopt;
    return Point{radius_ * dot(p, east_) / den, radius_ * dot(p, north_) / den};
}

double LocalSampler::river_distance(double x, double z) const {
    double best = 1e30;
    for (const Line& line : lines_) {
        const double dx = line.b.x - line.a.x;
        const double dz = line.b.z - line.a.z;
        const double denominator = dx * dx + dz * dz;
        // A zero-length segment divides by one and measures to its point.
        const double t =
            std::clamp(((x - line.a.x) * dx + (z - line.a.z) * dz) / (denominator != 0.0 ? denominator : 1.0), 0.0, 1.0);
        best = std::min(best, std::hypot(x - line.a.x - t * dx, z - line.a.z - t * dz));
    }
    return best;
}

}  // namespace fwg
