// The coordinate contract, version 1: a pure oracle, with no generation behind it.
//
// A port of the reference's fantasy_world_generator.coordinates. Globe Cartesian
// coordinates are metres from the sphere centre: +X is latitude 0 / longitude 0, +Y the
// north pole, +Z latitude 0 / longitude +90, right-handed (X x Y = Z). Heights are radial
// elevation above the reference sphere, NOT above sea level. The local tangent frame is
// X east, Y up, Z north, with east x up = north.
//
//   up    = (cos lat cos lon, sin lat, cos lat sin lon)    exactly (0, +-1, 0) at a pole
//   east  = (-sin lon, 0, cos lon)
//   north = (-sin lat cos lon, cos lat, -sin lat sin lon)
//
// +180 longitude aliases -180. A pole's position ignores longitude but its frame does not:
// there is no longitude-free east at a pole.
//
//   city  (gnomonic):        normalize(up + (east*x + north*z) / radius)
//   patch (exponential map): up*cos(d/radius) + tangent*sin(d/radius), d = hypot(x, z)
//   tile level L:            latitude 90 - 180 z / 2^L, longitude -180 + 180 (x mod 2^(L+1)) / 2^L
//
// EXACT, NOT APPROXIMATE. Every operation here is evaluated in the reference's order with
// the reference's roundings, so on the same C runtime the result is bit-for-bit the
// reference's: math.radians is ONE multiplication by pi/180, math.hypot is CPython's own
// vector_norm (not the C library's hypot, which differs in the last bit), and the local
// projection's sum() is CPython 3.12's compensated (Neumaier) summation. The reference's
// fixtures compare within 1e-12 relative; this port is held to the bits (see
// tests/test_coordinates.cpp and the scratch parity sweep in the report).
//
// REFUSALS are PatchError, naming the field: an angle or index out of range, a
// non-positive radius or radial distance, a non-finite value anywhere in the arithmetic,
// and -- for the JSON oracle -- any deviation from the exact request shape: no defaults,
// no unknown fields, no booleans or strings where a number goes, integers where the
// contract says integer.
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "fwg/json_value.hpp"

namespace fwg::coordinates {

inline constexpr int kCoordinateVersion = 1;

using Vec = std::array<double, 3>;

struct Frame {
    Vec up;
    Vec east;
    Vec north;
};

Frame tangent_frame(double latitude_degrees, double longitude_degrees);
// (radius + height) * up, in metres.
Vec globe_position(double latitude_degrees, double longitude_degrees, double radius_m, double height_m);
// A city plan's (x, z) metres, gnomonic about the anchor: a unit direction.
Vec city_direction(double latitude_degrees, double longitude_degrees, double radius_m, double x_m, double z_m);
// A patch's (x, z) metres, exponential map about the anchor: a unit direction.
Vec patch_direction(double latitude_degrees, double longitude_degrees, double radius_m, double x_m, double z_m);
// A globe position (metres) in the anchor's east/up/north frame, origin at
// (radius + origin_height) * up.
Vec local_position(double latitude_degrees, double longitude_degrees, double radius_m, double origin_height_m,
                   const Vec& position_m);
// The unit direction of a global tile sample; indices are inclusive of the boundary.
Vec tile_direction(std::int64_t level, std::int64_t x, std::int64_t z);
double height_above_sea(double height_m, double sea_level_m);
double metres_to_centimetres(double value_m);

// The strict versioned oracle: {"coordinate_version": 1, "operation": ..., "values": {...}}
// with exactly the operation's declared fields. Returns an array of three numbers, a
// {"up", "east", "north"} object for tangent_frame, or a number.
JsonValue resolve_coordinate(const JsonValue& request);

// CPython's math.hypot over any number of coordinates, bit for bit.
double python_hypot(std::span<const double> values);
// CPython 3.12's built-in sum() of a non-empty sequence of floats, bit for bit: the int 0
// start added to the first item, then Neumaier compensation over the rest, the
// compensation added once at the end if it is non-zero and finite. (Not math.fsum.)
double python_sum(std::span<const double> values);

}  // namespace fwg::coordinates
