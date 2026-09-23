#include "fwg/coordinates.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "fwg/patch.hpp"
#include "fwg/request_json.hpp"

namespace fwg::coordinates {
namespace {

// CPython's math.radians is ONE multiplication by this constant, not x * pi / 180.
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

// The reference's arithmetic guard: any non-finite intermediate refuses the request.
double finite(double value, std::vector<std::string_view> fields) {
    if (!std::isfinite(value)) {
        throw PatchError(refusal::cross_field("coordinate arithmetic must remain finite", std::move(fields)));
    }
    return value;
}

void require_finite_input(double value, const char* field) {
    if (!std::isfinite(value)) {
        PatchRefusal r = refusal::wrong_type(std::string("values.") + field, JsonValue(value), "number");
        r.message = std::string(field) + " must be a finite number";
        throw PatchError(std::move(r));
    }
}

void require_angles(double latitude, double longitude) {
    if (!(-90.0 <= latitude && latitude <= 90.0)) {
        throw PatchError(refusal::out_of_range("values.latitude_degrees", JsonValue(latitude), -90.0, 90.0, "number", "degrees"));
    }
    if (!(-180.0 <= longitude && longitude <= 180.0)) {
        throw PatchError(
            refusal::out_of_range("values.longitude_degrees", JsonValue(longitude), -180.0, 180.0, "number", "degrees"));
    }
}

void require_radius(double radius) {
    if (!(radius > 0.0)) {
        PatchRefusal r = refusal::out_of_range("values.radius_m", JsonValue(radius), 0.0, std::nullopt, "number", "m");
        r.message = "radius_m must be positive; received " + python_float_repr(radius) + ".";
        r.suggestion = "none";
        r.suggestion_value.reset();
        throw PatchError(std::move(r));
    }
}

// radius + height, which must be positive: a radial distance, not a height.
double radial_distance(double radius, double height, std::string_view height_field) {
    const double distance = finite(radius + height, {"radius_m", height_field});
    if (!(distance > 0.0)) {
        throw PatchError(refusal::cross_field("radius plus radial height must be positive", {"radius_m", height_field}));
    }
    return distance;
}

Frame frame_of(double latitude, double longitude) {
    require_angles(latitude, longitude);
    const double lat = latitude * kDegToRad;
    const double lon = (longitude == 180.0 ? -180.0 : longitude) * kDegToRad;
    Frame f;
    f.up = {std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon)};
    if (std::fabs(latitude) == 90.0) f.up = {0.0, latitude > 0.0 ? 1.0 : -1.0, 0.0};
    f.east = {-std::sin(lon), 0.0, std::cos(lon)};
    f.north = {-std::sin(lat) * std::cos(lon), std::cos(lat), -std::sin(lat) * std::sin(lon)};
    return f;
}

// CPython's vector_norm (Modules/mathmodule.c), including the subnormal rescale.
double vector_norm(std::size_t n, double* vec, double max, bool found_nan) {
    if (std::isinf(max)) return max;
    if (found_nan) return std::numeric_limits<double>::quiet_NaN();
    if (max == 0.0 || n <= 1) return max;
    int max_e = 0;
    std::frexp(max, &max_e);
    if (max_e < -1023) {
        // ldexp(1.0, -max_e) would overflow: convert subnormals to normals and recurse.
        for (std::size_t i = 0; i < n; ++i) vec[i] /= DBL_MIN;
        return DBL_MIN * vector_norm(n, vec, max / DBL_MIN, found_nan);
    }
    const double scale = std::ldexp(1.0, -max_e);
    double csum = 1.0;
    double frac1 = 0.0;
    double frac2 = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double x = vec[i] * scale;  // lossless scaling
        const double pr_hi = x * x;       // lossless squaring
        const double pr_lo = std::fma(x, x, -pr_hi);
        const double sm_hi = csum + pr_hi;  // lossless addition
        const double sm_lo = (csum - sm_hi) + pr_hi;
        csum = sm_hi;
        frac1 += pr_lo;  // lossy addition
        frac2 += sm_lo;  // lossy addition
    }
    double h = std::sqrt(csum - 1.0 + (frac1 + frac2));
    const double pr_hi = -h * h;
    const double pr_lo = std::fma(-h, h, -pr_hi);
    const double sm_hi = csum + pr_hi;
    const double sm_lo = (csum - sm_hi) + pr_hi;
    csum = sm_hi;
    frac1 += pr_lo;
    frac2 += sm_lo;
    const double x = csum - 1.0 + (frac1 + frac2);
    h += x / (2.0 * h);  // differential correction
    return h / scale;
}

JsonValue vec_json(const Vec& v) { return JsonValue(JsonValue::Array{JsonValue(v[0]), JsonValue(v[1]), JsonValue(v[2])}); }

}  // namespace

double python_hypot(std::span<const double> values) {
    std::vector<double> coordinates(values.size());
    double max = 0.0;
    bool found_nan = false;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const double x = std::fabs(values[i]);
        coordinates[i] = x;
        found_nan = found_nan || std::isnan(x);
        if (x > max) max = x;
    }
    return vector_norm(coordinates.size(), coordinates.data(), max, found_nan);
}

double python_sum(std::span<const double> values) {
    if (values.empty()) return 0.0;
    double total = 0.0 + values[0];  // int 0 + float: -0.0 becomes 0.0
    double c = 0.0;
    for (std::size_t i = 1; i < values.size(); ++i) {
        const double x = values[i];
        const double t = total + x;
        if (std::fabs(total) >= std::fabs(x)) {
            c += (total - t) + x;
        } else {
            c += (x - t) + total;
        }
        total = t;
    }
    if (c != 0.0 && std::isfinite(c)) total += c;
    return total;
}

Frame tangent_frame(double latitude_degrees, double longitude_degrees) {
    require_finite_input(latitude_degrees, "latitude_degrees");
    require_finite_input(longitude_degrees, "longitude_degrees");
    return frame_of(latitude_degrees, longitude_degrees);
}

Vec globe_position(double latitude_degrees, double longitude_degrees, double radius_m, double height_m) {
    require_finite_input(latitude_degrees, "latitude_degrees");
    require_finite_input(longitude_degrees, "longitude_degrees");
    require_finite_input(radius_m, "radius_m");
    require_finite_input(height_m, "height_m");
    const Frame f = frame_of(latitude_degrees, longitude_degrees);
    require_radius(radius_m);
    const double distance = radial_distance(radius_m, height_m, "height_m");
    Vec out{};
    for (std::size_t i = 0; i < 3; ++i) out[i] = finite(distance * f.up[i], {"radius_m", "height_m"});
    return out;
}

Vec city_direction(double latitude_degrees, double longitude_degrees, double radius_m, double x_m, double z_m) {
    require_finite_input(latitude_degrees, "latitude_degrees");
    require_finite_input(longitude_degrees, "longitude_degrees");
    require_finite_input(radius_m, "radius_m");
    require_finite_input(x_m, "x_m");
    require_finite_input(z_m, "z_m");
    const Frame f = frame_of(latitude_degrees, longitude_degrees);
    require_radius(radius_m);
    const double x = finite(x_m / radius_m, {"x_m", "radius_m"});
    const double z = finite(z_m / radius_m, {"z_m", "radius_m"});
    Vec point{};
    for (std::size_t i = 0; i < 3; ++i) {
        point[i] = finite(f.up[i] + f.east[i] * x + f.north[i] * z, {"x_m", "z_m", "radius_m"});
    }
    const double length = finite(python_hypot(point), {"x_m", "z_m", "radius_m"});
    return {point[0] / length, point[1] / length, point[2] / length};
}

Vec patch_direction(double latitude_degrees, double longitude_degrees, double radius_m, double x_m, double z_m) {
    require_finite_input(latitude_degrees, "latitude_degrees");
    require_finite_input(longitude_degrees, "longitude_degrees");
    require_finite_input(radius_m, "radius_m");
    require_finite_input(x_m, "x_m");
    require_finite_input(z_m, "z_m");
    const Frame f = frame_of(latitude_degrees, longitude_degrees);
    require_radius(radius_m);
    const double xz[2] = {x_m, z_m};
    const double distance = finite(python_hypot(xz), {"x_m", "z_m"});
    if (distance == 0.0) return f.up;
    const double angle = finite(distance / radius_m, {"x_m", "z_m", "radius_m"});
    Vec out{};
    for (std::size_t i = 0; i < 3; ++i) {
        out[i] = finite(f.up[i] * std::cos(angle) + (f.east[i] * (x_m / distance) + f.north[i] * (z_m / distance)) * std::sin(angle),
                        {"x_m", "z_m", "radius_m"});
    }
    return out;
}

Vec local_position(double latitude_degrees, double longitude_degrees, double radius_m, double origin_height_m,
                   const Vec& position_m) {
    require_finite_input(latitude_degrees, "latitude_degrees");
    require_finite_input(longitude_degrees, "longitude_degrees");
    require_finite_input(radius_m, "radius_m");
    require_finite_input(origin_height_m, "origin_height_m");
    const Frame f = frame_of(latitude_degrees, longitude_degrees);
    require_radius(radius_m);
    for (const double p : position_m) require_finite_input(p, "position_m");
    const double distance = radial_distance(radius_m, origin_height_m, "origin_height_m");
    Vec delta{};
    for (std::size_t i = 0; i < 3; ++i) delta[i] = finite(position_m[i] - distance * f.up[i], {"position_m", "radius_m"});
    Vec out{};
    const Vec* axes[3] = {&f.east, &f.up, &f.north};
    for (std::size_t k = 0; k < 3; ++k) {
        double terms[3];
        for (std::size_t i = 0; i < 3; ++i) terms[i] = finite(delta[i] * (*axes[k])[i], {"position_m"});
        out[k] = finite(python_sum(terms), {"position_m"});
    }
    return out;
}

Vec tile_direction(std::int64_t level, std::int64_t x, std::int64_t z) {
    if (level < 1 || level > 24) {
        throw PatchError(refusal::out_of_range("values.level", JsonValue(level), 1.0, 24.0, "integer", "level"));
    }
    const std::int64_t rows = std::int64_t{1} << level;
    if (x < 0 || x > 2 * rows) {
        throw PatchError(refusal::out_of_range("values.x", JsonValue(x), 0.0, static_cast<double>(2 * rows), "integer", "index"));
    }
    if (z < 0 || z > rows) {
        throw PatchError(refusal::out_of_range("values.z", JsonValue(z), 0.0, static_cast<double>(rows), "integer", "index"));
    }
    const double latitude = 90.0 - static_cast<double>(180 * z) / static_cast<double>(rows);
    const double longitude = -180.0 + static_cast<double>(180 * (x % (2 * rows))) / static_cast<double>(rows);
    return frame_of(latitude, longitude).up;
}

double height_above_sea(double height_m, double sea_level_m) {
    require_finite_input(height_m, "height_m");
    require_finite_input(sea_level_m, "sea_level_m");
    return finite(height_m - sea_level_m, {"height_m", "sea_level_m"});
}

double metres_to_centimetres(double value_m) {
    require_finite_input(value_m, "value_m");
    return finite(value_m * 100.0, {"value_m"});
}

// ---------------------------------------------------------------------------------------
// The strict JSON oracle.

namespace {

struct OperationFields {
    std::string_view operation;
    std::vector<std::string_view> fields;
};

const std::vector<OperationFields>& operation_fields() {
    static const std::vector<OperationFields> table = {
        {"globe_position", {"latitude_degrees", "longitude_degrees", "radius_m", "height_m"}},
        {"tangent_frame", {"latitude_degrees", "longitude_degrees"}},
        {"city_direction", {"latitude_degrees", "longitude_degrees", "radius_m", "x_m", "z_m"}},
        {"patch_direction", {"latitude_degrees", "longitude_degrees", "radius_m", "x_m", "z_m"}},
        {"local_position", {"latitude_degrees", "longitude_degrees", "radius_m", "origin_height_m", "position_m"}},
        {"tile_direction", {"level", "x", "z"}},
        {"height_above_sea", {"height_m", "sea_level_m"}},
        {"metres_to_centimetres", {"value_m"}},
    };
    return table;
}

// The reference's `_finite`: an int or a float -- never a bool or a string -- that converts
// to a finite double.
double number(const JsonValue& values, std::string_view field) {
    const JsonValue& value = values.at(field);
    const std::string name = "values." + std::string(field);
    if (!value.is_number()) throw PatchError(refusal::wrong_type(name, value, "number"));
    const std::optional<double> v = value.to_double();
    if (!v || !std::isfinite(*v)) {
        PatchRefusal r = refusal::wrong_type(name, value, "number");
        r.message = std::string(field) + " must be a finite number inside the double range";
        throw PatchError(std::move(r));
    }
    return *v;
}

std::int64_t integer_field(const JsonValue& values, std::string_view field, std::int64_t low, std::int64_t high) {
    const JsonValue& value = values.at(field);
    const std::string name = "values." + std::string(field);
    if (!value.is_integer()) throw PatchError(refusal::wrong_type(name, value, "integer"));
    if (!value.is_int64() || value.as_int() < low || value.as_int() > high) {
        throw PatchError(refusal::out_of_range(name, value, static_cast<double>(low), static_cast<double>(high), "integer",
                                               field == "level" ? "level" : "index"));
    }
    return value.as_int();
}

}  // namespace

JsonValue resolve_coordinate(const JsonValue& request) {
    if (!request.is_object() || request.size() != 3 || !request.contains("coordinate_version") ||
        !request.contains("operation") || !request.contains("values")) {
        throw PatchError(refusal::cross_field(
            "coordinate request requires exactly coordinate_version, operation and values",
            {"coordinate_version", "operation", "values"}));
    }
    const JsonValue& version = request.at("coordinate_version");
    if (!version.is_integer()) throw PatchError(refusal::wrong_type("coordinate_version", version, "integer"));
    if (!version.is_int64() || version.as_int() != kCoordinateVersion) {
        throw PatchError(refusal::unsupported_api("coordinate_version", version, {JsonValue(kCoordinateVersion)}));
    }
    const JsonValue& operation = request.at("operation");
    const OperationFields* spec = nullptr;
    if (operation.is_string()) {
        for (const OperationFields& candidate : operation_fields()) {
            if (candidate.operation == operation.as_string()) spec = &candidate;
        }
    }
    if (spec == nullptr) {
        std::vector<std::string> names;
        for (const OperationFields& candidate : operation_fields()) names.emplace_back(candidate.operation);
        if (!operation.is_string()) throw PatchError(refusal::wrong_type("operation", operation, "string"));
        throw PatchError(refusal::invalid_choice("operation", operation, names));
    }
    const JsonValue& values = request.at("values");
    bool exact = values.is_object() && values.size() == spec->fields.size();
    for (const std::string_view field : spec->fields) exact = exact && values.contains(field);
    if (!exact) {
        throw PatchError(refusal::cross_field(
            std::string(spec->operation) + " requires exactly its declared values fields", spec->fields));
    }

    if (spec->operation == "tile_direction") {
        // Level first: the index bounds depend on it.
        const JsonValue& level_value = values.at("level");
        if (!level_value.is_integer()) throw PatchError(refusal::wrong_type("values.level", level_value, "integer"));
        for (const std::string_view field : {"x", "z"}) {
            if (!values.at(field).is_integer()) {
                throw PatchError(refusal::wrong_type("values." + std::string(field), values.at(field), "integer"));
            }
        }
        const std::int64_t level = integer_field(values, "level", 1, 24);
        const std::int64_t rows = std::int64_t{1} << level;
        const std::int64_t x = integer_field(values, "x", 0, 2 * rows);
        const std::int64_t z = integer_field(values, "z", 0, rows);
        return vec_json(tile_direction(level, x, z));
    }

    // Every scalar, in the request's own order, before any arithmetic.
    for (const auto& [field, value] : values.as_object()) {
        if (field != "position_m") number(values, field);
    }
    const auto num = [&](std::string_view field) { return number(values, field); };
    if (spec->operation == "height_above_sea") return JsonValue(height_above_sea(num("height_m"), num("sea_level_m")));
    if (spec->operation == "metres_to_centimetres") return JsonValue(metres_to_centimetres(num("value_m")));

    const double latitude = num("latitude_degrees");
    const double longitude = num("longitude_degrees");
    if (spec->operation == "tangent_frame") {
        const Frame f = tangent_frame(latitude, longitude);
        JsonValue out = JsonValue::object();
        out.set("up", vec_json(f.up));
        out.set("east", vec_json(f.east));
        out.set("north", vec_json(f.north));
        return out;
    }
    frame_of(latitude, longitude);  // the angles are refused before the radius, as in the reference
    const double radius = num("radius_m");
    require_radius(radius);
    if (spec->operation == "globe_position") {
        return vec_json(globe_position(latitude, longitude, radius, num("height_m")));
    }
    if (spec->operation == "local_position") {
        const JsonValue& position = values.at("position_m");
        if (!position.is_array() || position.size() != 3) {
            PatchRefusal r = refusal::wrong_type("values.position_m", position, "array");
            r.message = "position_m must be a three-element array";
            throw PatchError(std::move(r));
        }
        Vec p{};
        for (std::size_t i = 0; i < 3; ++i) {
            const JsonValue& item = position.at(i);
            if (!item.is_number()) throw PatchError(refusal::wrong_type("values.position_m", item, "number"));
            const std::optional<double> v = item.to_double();
            if (!v || !std::isfinite(*v)) throw PatchError(refusal::wrong_type("values.position_m", item, "number"));
            p[i] = *v;
        }
        return vec_json(local_position(latitude, longitude, radius, num("origin_height_m"), p));
    }
    if (spec->operation == "city_direction") {
        return vec_json(city_direction(latitude, longitude, radius, num("x_m"), num("z_m")));
    }
    return vec_json(patch_direction(latitude, longitude, radius, num("x_m"), num("z_m")));
}

}  // namespace fwg::coordinates
