// Three-component vector arithmetic on the unit sphere.
//
// Deliberately not a general maths library: every operation here appears in the
// generator, and the association order of each expression is the order the
// reference implementation evaluates it in.
#pragma once

#include <cmath>

namespace fwg {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    constexpr double operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
};

constexpr Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr Vec3 operator*(const Vec3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
constexpr Vec3 operator*(double s, const Vec3& a) { return a * s; }

constexpr double dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

inline Vec3 unit(const Vec3& p) {
    const double norm = std::sqrt(dot(p, p));
    return {p.x / norm, p.y / norm, p.z / norm};
}

// Clamp helper used wherever a dot product feeds asin/acos. Rounding can push a
// unit-vector dot product a few ulp outside [-1, 1] and the result is a NaN that
// only shows up on some seeds.
inline double clamp_unit(double v) {
    return v < -1.0 ? -1.0 : (v > 1.0 ? 1.0 : v);
}

// Surface distance between two unit directions, in the units of `radius_m`.
//
// Kept in exactly this clamp-then-acos form. An atan2-based formula is more accurate and
// would therefore NOT reproduce the behaviour at the tie boundaries where `distance <
// best` decides which ley node anchors a region or which city owns a cell.
//
// Lives here, with the vectors, because almost every subsystem measures between two
// directions. It started in villains.hpp, and a distance helper in one subsystem's header
// is how the next subsystem writes its own copy instead. This is the one that takes
// DIRECTIONS and so wraps the longitude seam; city_plan.cpp's raster-coordinate arc,
// `unwrapped_arc_m`, deliberately does not.
inline double great_circle_m(double radius_m, const Vec3& a, const Vec3& b) {
    return radius_m * std::acos(clamp_unit(dot(a, b)));
}

}  // namespace fwg
