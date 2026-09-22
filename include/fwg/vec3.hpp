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

}  // namespace fwg
