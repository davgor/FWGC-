#include "fwg/patch.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <utility>

#include "fwg/grid.hpp"
#include "fwg/json.hpp"  // format_double
#include "fwg/noise.hpp"
#include "fwg/random.hpp"
#include "fwg/vec3.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

constexpr double kPi = 3.14159265358979323846;

// --- the refusal shapes the reference's terrain_errors builds, as a patch uses them ----

PatchRefusal wrong_type(std::string field, double value) {
    PatchRefusal r;
    r.kind = "wrong_type";
    r.message = field + " must be a finite number; received " + format_double(value) + ".";
    r.field = std::move(field);
    r.received = value;
    r.expected_type = "number";
    return r;
}

PatchRefusal out_of_range(std::string field, double value, double low, double high, std::string_view units) {
    PatchRefusal r;
    r.kind = "out_of_range";
    r.message = field + " must be " + format_double(low) + ".." + format_double(high) + " " + std::string(units) +
                "; received " + format_double(value) + ".";
    r.field = std::move(field);
    r.received = value;
    r.expected_type = "number";
    r.min = low;
    r.max = high;
    r.units = units;
    r.suggestion = "clamp";
    r.suggestion_value = std::min(std::max(value, low), high);
    return r;
}

PatchRefusal cross_field(std::string message, std::vector<std::string_view> fields) {
    PatchRefusal r;
    r.kind = "cross_field";
    r.message = std::move(message);
    r.fields = std::move(fields);
    return r;
}

PatchRefusal refused_by_world(std::string field, double value, std::string reason) {
    PatchRefusal r;
    r.kind = "refused_by_world";
    r.message = std::move(reason);
    r.field = std::move(field);
    r.received = value;
    r.refused_by_world = true;
    return r;
}

PatchRefusal missing_block(std::string_view block, std::string message) {
    PatchRefusal r;
    r.kind = "missing_block";
    r.message = std::move(message);
    r.field = "world." + std::string(block);
    r.requires_block = block;
    r.retry = false;
    return r;
}

std::string general(double value) {
    std::array<char, 64> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%g", value);
    return std::string(buffer.data());
}

}  // namespace

PatchError::PatchError(PatchRefusal refusal) : std::invalid_argument(refusal.message), refusal_(std::move(refusal)) {}

void PatchConfig::validate() const {
    const std::pair<const char*, double> fields[] = {{"latitude", latitude},           {"longitude", longitude},
                                                     {"span", span},                   {"spacing", spacing},
                                                     {"detail_height", detail_height}, {"detail_scale", detail_scale}};
    for (const auto& [name, value] : fields) {
        if (!std::isfinite(value)) throw PatchError(wrong_type(std::string("patch.") + name, value));
    }
    if (!(-kPatchLatitudeMaxDeg <= latitude && latitude <= kPatchLatitudeMaxDeg)) {
        throw PatchError(out_of_range("patch.latitude", latitude, -kPatchLatitudeMaxDeg, kPatchLatitudeMaxDeg, "degrees"));
    }
    if (!(-kPatchLongitudeMaxDeg <= longitude && longitude <= kPatchLongitudeMaxDeg)) {
        throw PatchError(
            out_of_range("patch.longitude", longitude, -kPatchLongitudeMaxDeg, kPatchLongitudeMaxDeg, "degrees"));
    }
    if (!(kPatchSpanMinM <= span && span <= kPatchSpanMaxM)) {
        throw PatchError(out_of_range("patch.span", span, kPatchSpanMinM, kPatchSpanMaxM, "m"));
    }
    if (!(kPatchSpacingMinM <= spacing && spacing <= kPatchSpacingMaxM)) {
        throw PatchError(out_of_range("patch.spacing", spacing, kPatchSpacingMinM, kPatchSpacingMaxM, "m"));
    }
    const double segments = std::ceil(span / spacing);
    if (segments > kPatchMaxSegments) {
        throw PatchError(cross_field("A patch is at most " + std::to_string(kPatchMaxSegments + 1) +
                                         " vertices per axis and span/spacing asks for " +
                                         std::to_string(static_cast<long long>(segments) + 1) +
                                         ". Reduce span or increase spacing.",
                                     {"span", "spacing"}));
    }
    if (!(0.0 <= detail_height && detail_height <= kPatchDetailHeightMaxM)) {
        throw PatchError(out_of_range("patch.detail_height", detail_height, 0.0, kPatchDetailHeightMaxM, "m"));
    }
    if (!(kPatchDetailScaleMinM <= detail_scale && detail_scale <= kPatchDetailScaleMaxM)) {
        throw PatchError(
            out_of_range("patch.detail_scale", detail_scale, kPatchDetailScaleMinM, kPatchDetailScaleMaxM, "m"));
    }
}

Patch generate_patch(const World& world, const PatchConfig& cfg) {
    const auto started = std::chrono::steady_clock::now();
    cfg.validate();

    const auto height_layer = world.layers.find("height");
    if (height_layer == world.layers.end()) {
        throw PatchError(missing_block("layers.height",
                                       "A patch is cut from a globe with elevation, and this world has no height "
                                       "layer yet; generate it to phase 2 or beyond."));
    }
    const Grid& coarse = height_layer->second;

    const double radius = world.effective.globe_radius;
    if (cfg.span > radius * 0.5 || cfg.detail_height > radius * 0.01) {
        // The reference names the span whichever bound failed. Reproduced; see the header.
        throw PatchError(refused_by_world("patch.span", cfg.span,
                                          "A patch spans at most half this globe's radius (" + general(radius * 0.5) +
                                              " m) and its detail at most a hundredth (" + general(radius * 0.01) +
                                              " m). This world has radius " + general(radius) + " m."));
    }

    const double lat = cfg.latitude * (kPi / 180.0);
    const double lon = cfg.longitude * (kPi / 180.0);
    const Vec3 center{std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon)};
    const Vec3 east{-std::sin(lon), 0.0, std::cos(lon)};
    const Vec3 north{-std::sin(lat) * std::cos(lon), std::cos(lat), -std::sin(lat) * std::sin(lon)};

    const int segments = static_cast<int>(std::ceil(cfg.span / cfg.spacing));
    const double step = cfg.span / segments;
    const bool canonical = world.has_terrain_detail;
    const std::uint32_t seed = child_seed(world.config.seed, "micro-relief");
    const bool resolved = cfg.detail_scale >= 2.0 * step;

    // Built only for a canonical world, the only one that reads it.
    std::optional<HeightField> field;
    if (canonical) field.emplace(world);

    // (base, detail): the coarse bilinear sample and the relief above it.
    const auto elevation = [&](const Vec3& p) -> std::pair<double, double> {
        const double base = sample(coarse, p);
        if (canonical) return {base, field->height(p) - base};
        const double detail = resolved ? cfg.detail_height * perlin3(p.x * radius / cfg.detail_scale,
                                                                     p.y * radius / cfg.detail_scale,
                                                                     p.z * radius / cfg.detail_scale, seed)
                                       : 0.0;
        return {base, detail};
    };

    Patch patch;
    patch.patch_version = canonical ? 2 : 1;
    if (world.has_terrain_detail) patch.terrain_detail = world.terrain_detail;
    patch.world_config = world.config;
    patch.world_generator_version = world.generator_version;
    patch.patch_config = cfg;
    patch.radius_m = radius;
    patch.size = segments + 1;
    patch.spacing_m = step;
    patch.detail_seed = seed;
    patch.detail_resolved = canonical ? true : resolved;

    const auto [origin_base, origin_detail] = elevation(center);
    const double origin_height = origin_base + origin_detail;
    patch.origin_height_m = origin_height;

    const auto count = static_cast<std::size_t>(patch.size) * static_cast<std::size_t>(patch.size);
    patch.height.reserve(count);
    patch.detail.reserve(count);
    patch.local_x.reserve(count);
    patch.local_y.reserve(count);
    patch.local_z.reserve(count);
    for (int z = 0; z <= segments; ++z) {
        const double v = z * step - cfg.span / 2.0;
        for (int x = 0; x <= segments; ++x) {
            const double u = x * step - cfg.span / 2.0;
            // Azimuthal equidistant: the point hypot(u, v) metres along the surface from
            // the centre, on bearing (u, v).
            const double distance = std::hypot(u, v);
            const double theta = distance / radius;
            const Vec3 tangent = distance != 0.0 ? Vec3{(u * east.x + v * north.x) / distance,
                                                        (u * east.y + v * north.y) / distance,
                                                        (u * east.z + v * north.z) / distance}
                                                 : east;
            const Vec3 p = center * std::cos(theta) + tangent * std::sin(theta);
            const auto [base, detail] = elevation(p);
            const double h = base + detail;
            const Vec3 position = p * (radius + h) - center * (radius + origin_height);
            patch.height.push_back(h);
            patch.detail.push_back(detail);
            patch.local_x.push_back(dot(position, east));
            patch.local_y.push_back(dot(position, center));
            patch.local_z.push_back(dot(position, north));
        }
    }

    patch.triangle_indices.reserve(static_cast<std::size_t>(segments) * static_cast<std::size_t>(segments) * 6);
    for (int z = 0; z < segments; ++z) {
        for (int x = 0; x < segments; ++x) {
            const auto a = static_cast<std::uint32_t>(z * (segments + 1) + x);
            const auto b = a + static_cast<std::uint32_t>(segments + 1);
            for (const std::uint32_t i : {a, b, a + 1, a + 1, b, b + 1}) patch.triangle_indices.push_back(i);
        }
    }

    if (canonical) {
        patch.warnings.push_back(
            "Canonical world heights; legacy patch detail controls are ignored. Sample spacing changes mesh "
            "fidelity, not terrain.");
    }
    patch.warnings.push_back("Drainage is regional; protected water masks suppress local detail.");
    patch.warnings.push_back("Spacing is on the reference sphere; elevated surface edge lengths differ slightly.");
    patch.timing_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return patch;
}

Patch patch_request(const Request& world, const PatchConfig& patch) {
    const Config cfg = resolve(world);
    patch.validate();
    // This port has no plane shape, and resolve() already refuses a size over 1025, so of
    // the reference's three conditions only the phase can fail here. The message and the
    // field list are the reference's, because a caller reads them.
    if (cfg.size > 1025 || cfg.phase < 2) {
        throw PatchError(cross_field("A patch is cut from a globe with elevation: shape must be globe, size at most "
                                     "1025, and phase at least 2 when tectonics run. This config is shape 'globe', "
                                     "size " + std::to_string(cfg.size) + ", phase " + std::to_string(cfg.phase) + ".",
                                     {"shape", "size", "phase"}));
    }
    return generate_patch(generate(cfg), patch);
}

}  // namespace fwg
