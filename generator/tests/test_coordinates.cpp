// The coordinate contract, version 1.
//
// Two kinds of evidence. The reference's hand-authored fixture (analytic values, compared
// within 1e-12 relative / 1e-9 absolute, as the reference compares them), and PINNED BITS:
// the reference's own results for requests off the cardinal axes, spelled as CPython repr
// strings, which this port must reproduce exactly. The formulas are a contract, so the
// port is held to the reference's evaluation order and its roundings -- math.radians as
// one multiply, CPython's hypot, 3.12's compensated sum() -- not to a tolerance.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "fwg/coordinates.hpp"
#include "product_fixtures.hpp"

using namespace fwg;
using namespace fwg::coordinates;

namespace {

bool close_enough(double a, double b) { return std::fabs(a - b) <= std::max(1e-12 * std::max(std::fabs(a), std::fabs(b)), 1e-9); }

JsonValue resolve(const char* operation, const char* values_json) {
    JsonValue request = JsonValue::object();
    request.set("coordinate_version", 1);
    request.set("operation", operation);
    request.set("values", parse_json(values_json));
    return resolve_coordinate(request);
}

// The result as CPython would print it: exact to the bit.
std::string text(const JsonValue& v) { return dump_json(v); }

}  // namespace

TEST(coordinates_fixture_valid_vectors_within_the_contract_tolerance) {
    const JsonValue fixture = product_test::fixture("coordinates-v1.json");
    for (const JsonValue& c : fixture.at("valid").as_array()) {
        const std::string name = c.at("name").as_string();
        const JsonValue result = resolve_coordinate(c.at("request"));
        const JsonValue& expected = c.at("expected");
        const auto vector_close = [&](const JsonValue& got, const JsonValue& want, const std::string& what) {
            check::equal(got.size(), want.size(), what + " length");
            for (std::size_t i = 0; i < want.size(); ++i) {
                check::is_true(close_enough(*got.at(i).to_double(), *want.at(i).to_double()), what + " component " + std::to_string(i));
            }
        };
        if (expected.is_object()) {
            for (const auto& [axis, want] : expected.as_object()) vector_close(result.at(axis), want, name + " " + axis);
            check::equal(result.size(), expected.size(), name + " keys");
        } else if (expected.is_array()) {
            vector_close(result, expected, name);
        } else {
            check::is_true(close_enough(*result.to_double(), *expected.to_double()), name);
        }
    }
    for (const JsonValue& request : fixture.at("invalid").as_array()) {
        check::throws([&] { resolve_coordinate(request); }, "refuses " + dump_json(request));
    }
}

TEST(coordinates_reproduce_the_references_bits) {
    // Each right-hand side is the reference's resolve_coordinate output for the same request.
    check::equal(text(resolve("globe_position", R"({"latitude_degrees": 37.25, "longitude_degrees": -122.5, "radius_m": 31830.98861837907, "height_m": 412.75})")),
                 std::string("[-13790.375005516225, 19516.94113773135, -21646.552749200342]"), "globe_position");
    check::equal(text(resolve("globe_position", R"({"latitude_degrees": -89.999, "longitude_degrees": 179.999, "radius_m": 1000, "height_m": -999.5})")),
                 std::string("[-8.726646258207324e-06, -0.49999999992384564, 1.5230870988063572e-10]"), "near the south pole");
    check::equal(text(resolve("tangent_frame", R"({"latitude_degrees": 45, "longitude_degrees": 70})")),
                 std::string(R"({"up": [0.24184476264797533, 0.7071067811865476, 0.6644630243886747], "east": [-0.9396926207859083, 0.0, 0.3420201433256688], "north": [-0.24184476264797533, 0.7071067811865476, -0.6644630243886747]})"),
                 "tangent_frame");
    check::equal(text(resolve("city_direction", R"({"latitude_degrees": 12.5, "longitude_degrees": -45, "radius_m": 31830.98861837907, "x_m": 812.25, "z_m": -117.75})")),
                 std::string("[0.7087197862862237, 0.21275736285457322, -0.6726444596349375]"), "city_direction");
    check::equal(text(resolve("patch_direction", R"({"latitude_degrees": -33.3, "longitude_degrees": 151.2, "radius_m": 31830.98861837907, "x_m": -240.5, "z_m": 96.125})")),
                 std::string("[-0.730212338532009, -0.5464806539416209, 0.41005955119123033]"), "patch_direction");
    check::equal(text(resolve("patch_direction", R"({"latitude_degrees": 0, "longitude_degrees": 0, "radius_m": 1, "x_m": 3, "z_m": 4})")),
                 std::string("[0.28366218546322625, -0.7671394197305108, -0.575354564797883]"), "five radians");
    check::equal(text(resolve("local_position", R"({"latitude_degrees": 10, "longitude_degrees": 20, "radius_m": 1000, "origin_height_m": 12.5, "position_m": [925.25, 186.5, 335.75]})")),
                 std::string("[-0.9523401832062479, -10.784237876232833, 12.747547253328673]"), "local_position");
    check::equal(text(resolve("local_position", R"({"latitude_degrees": -60, "longitude_degrees": -150, "radius_m": 31830.98861837907, "origin_height_m": -40, "position_m": [-13768.0, -27529.5, -7948.25]})")),
                 std::string("[-0.6135843703334668, -0.9608852432896138, 2.94320781482781]"), "local_position, compensated sum");
    check::equal(text(resolve("tile_direction", R"({"level": 24, "x": 33554431, "z": 1})")),
                 std::string("[-1.8725351408915763e-07, 0.9999999999999825, 3.5063878550205553e-14]"), "finest tile");
    check::equal(text(resolve("tile_direction", R"({"level": 5, "x": 17, "z": 11})")),
                 std::string("[0.08644340032725618, 0.47139673682599764, -0.877674572406916]"), "tile");
    // Chosen by search because their bits DEPEND on the exact ports: the first changes if
    // hypot is sqrt(x*x + y*y + z*z) or if radians is x * pi / 180; the second if sum() is
    // added left to right. A port that swaps in the obvious alternative fails here.
    check::equal(text(resolve("city_direction", R"({"latitude_degrees": -17.091902660027387, "longitude_degrees": -107.37299084216954, "radius_m": 1000.0, "x_m": -485.52708888667553, "z_m": -660.1600783483731})")),
                 std::string("[-0.5343462616268853, -0.7153846612556402, -0.4502209003650601]"), "hypot- and radians-sensitive");
    check::equal(text(resolve("local_position", R"({"latitude_degrees": -73.00913878507109, "longitude_degrees": 44.363236331644885, "radius_m": 1000.0, "origin_height_m": -28.330768799324336, "position_m": [444.1946593678873, -1004.8693379319982, 131.3995715336768]})")),
                 std::string("[-216.64264419716068, 108.98501595466891, 97.9259513431332]"), "sum()-sensitive");
    check::equal(text(resolve("height_above_sea", R"({"height_m": 0.1, "sea_level_m": 0.2})")), std::string("-0.1"), "datum");
    check::equal(text(resolve("metres_to_centimetres", R"({"value_m": 0.29})")), std::string("28.999999999999996"),
                 "the boundary conversion is one multiply");
}

TEST(coordinates_cpython_hypot_and_sum_are_ported_to_the_bit) {
    check::equal(python_float_repr(python_hypot(std::vector<double>{3e-320, 4e-320})), std::string("5e-320"), "subnormal rescale");
    check::equal(python_float_repr(python_hypot(std::vector<double>{1e308, 1e308})), std::string("1.4142135623730951e+308"),
                 "no overflow");
    check::equal(python_float_repr(python_hypot(std::vector<double>{0.1, 0.2, 0.3})), std::string("0.3741657386773941"), "three");
    check::equal(python_float_repr(python_sum(std::vector<double>{0.1, 0.2, 0.3})), std::string("0.6"), "compensated, not 0.6000000000000001");
    check::equal(python_float_repr(python_sum(std::vector<double>{1e16, 1.0, -1e16})), std::string("1.0"), "Neumaier keeps the 1");
    check::equal(python_float_repr(python_sum(std::vector<double>{-0.0, -0.0, -0.0})), std::string("0.0"), "the int 0 start");
    check::is_true(std::isinf(python_sum(std::vector<double>{1e308, 1e308, -1e308})), "an overflowed sum stays infinite");
}

TEST(coordinates_seams_poles_and_nested_tiles) {
    for (const double lat : {-90.0, -45.0, 0.0, 45.0, 90.0}) {
        check::is_true(globe_position(lat, -180, 1000, 20) == globe_position(lat, 180, 1000, 20), "+180 aliases -180");
    }
    for (const double lon : {-180.0, -60.0, 0.0, 90.0, 180.0}) {
        check::is_true(globe_position(90, lon, 1000, -20) == Vec{0.0, 980.0, 0.0}, "a pole ignores longitude");
    }
    for (const auto& [x, z] : std::vector<std::pair<int, int>>{{0, 0}, {3, 2}, {8, 4}, {16, 8}}) {
        check::is_true(tile_direction(3, x, z) == tile_direction(4, 2 * x, 2 * z), "nested tile indices share a direction");
    }
}

TEST(coordinates_frames_are_right_handed_even_at_poles) {
    for (const auto& [lat, lon] : std::vector<std::pair<double, double>>{{0, 0}, {45, 70}, {90, -30}, {-90, 180}}) {
        const Frame f = tangent_frame(lat, lon);
        const Vec cross{f.east[1] * f.up[2] - f.east[2] * f.up[1], f.east[2] * f.up[0] - f.east[0] * f.up[2],
                        f.east[0] * f.up[1] - f.east[1] * f.up[0]};
        for (int i = 0; i < 3; ++i) check::close(cross[i], f.north[i], 1e-12, "east x up = north");
    }
}

TEST(coordinates_refusals_are_typed_and_name_the_field) {
    const auto kind_of = [](const char* request) {
        return product_test::refusal_of([&] { resolve_coordinate(parse_json(request)); });
    };
    PatchRefusal r = kind_of(R"({"coordinate_version": 1, "operation": "globe_position", "values": {"latitude_degrees": 91, "longitude_degrees": 0, "radius_m": 1000, "height_m": 0}})");
    check::equal(std::string(r.kind), std::string("out_of_range"), "latitude kind");
    check::equal(r.field, std::string("values.latitude_degrees"), "latitude field");
    check::is_true(r.max && *r.max == 90.0 && r.suggestion_value && *r.suggestion_value == 90.0, "bound and clamp");
    r = kind_of(R"({"coordinate_version": 2, "operation": "metres_to_centimetres", "values": {"value_m": 1}})");
    check::equal(std::string(r.code), std::string("UNSUPPORTED_VERSION"), "version code");
    r = kind_of(R"({"coordinate_version": true, "operation": "metres_to_centimetres", "values": {"value_m": 1}})");
    check::equal(std::string(r.kind), std::string("wrong_type"), "a boolean version is not an integer");
    r = kind_of(R"({"coordinate_version": 1, "operation": "unreal_import", "values": {}})");
    check::equal(std::string(r.kind), std::string("invalid_choice"), "unknown operation");
    r = kind_of(R"({"coordinate_version": 1, "operation": "globe_position", "values": {"latitude_degrees": 0, "longitude_degrees": 0, "radius_m": 1000}})");
    check::equal(std::string(r.kind), std::string("cross_field"), "a missing field");
    r = kind_of(R"({"coordinate_version": 1, "operation": "metres_to_centimetres", "values": {"value_m": "1"}})");
    check::equal(r.field, std::string("values.value_m"), "a string is not a number");
    r = kind_of(R"({"coordinate_version": 1, "operation": "tile_direction", "values": {"level": 2, "x": 1.0, "z": 2}})");
    check::equal(r.field, std::string("values.x"), "a float index");
    r = kind_of(R"({"coordinate_version": 1, "operation": "globe_position", "values": {"latitude_degrees": 0, "longitude_degrees": 0, "radius_m": 1000, "height_m": -1000}})");
    check::equal(std::string(r.kind), std::string("cross_field"), "radius plus height must be positive");
    r = kind_of(R"({"coordinate_version": 1, "operation": "metres_to_centimetres", "values": {"value_m": 1e308}})");
    check::equal(std::string(r.kind), std::string("cross_field"), "arithmetic must remain finite");
    r = kind_of(R"({"coordinate_version": 1, "operation": "globe_position", "values": {"latitude_degrees": 0, "longitude_degrees": 0, "radius_m": 1e308, "height_m": 1e308}})");
    check::equal(std::string(r.kind), std::string("cross_field"), "an overflowing radial distance");
    r = kind_of(R"({"coordinate_version": 1, "operation": "local_position", "values": {"latitude_degrees": 0, "longitude_degrees": 0, "radius_m": 1000, "origin_height_m": 0, "position_m": [0, 1]}})");
    check::equal(r.field, std::string("values.position_m"), "a short position");
}
