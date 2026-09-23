// A local patch of ground in physical metres, cut from the globe around one point.
//
// A square of `span` metres centred on a latitude/longitude, sampled every `spacing`
// metres and returned as a mesh in the LOCAL TANGENT FRAME at its centre: x east, y up,
// z north, origin on the terrain at the patch centre. Up to 512 m across and 257 vertices
// a side -- the scale a player stands on, which the n x n raster is thousands of times too
// coarse for.
//
// WHICH SURFACE IT SAMPLES. Two, and which one is decided by the world, not the request:
//
//   * CANONICAL (the world publishes `terrain_detail`, stage 9 onward, patch_version 2):
//     the height IS HeightField's -- `base + (field.height(p) - base)` -- the one
//     continuous surface the city, hamlet and castle planners and the world scene all
//     sample. `detail_height` and `detail_scale` are IGNORED and a warning says so;
//     `detail` reports the height field's own relief above the coarse bilinear base.
//   * LEGACY (no `terrain_detail`, patch_version 1): the coarse bilinear sample of
//     `layers.height` plus this patch's own micro relief, `detail_height * perlin3(p *
//     radius / detail_scale)` on the child seed "micro-relief", zeroed when the scale is
//     finer than two samples (`detail_resolved` false). This surface exists nowhere else.
//
// DOES THE PATCH AGREE WITH THE PLANNERS' GROUND? MEASURED, not argued: seed 42, phase
// 16, a patch centred on every city (34, 43 and 47 cities at sizes 33, 65 and 129) at
// span min(2 half, 512) m and 4 m spacing, compared vertex for vertex.
//
//   * Against HeightField at the patch's own vertex directions: 3.5e-11 m at worst. The
//     same function; the residue is `base + (h - base)` rounding.
//   * Against each city plan's stored surface (`terrain.surface_heights_m`, 0.1 mm) at
//     the same local (u, v): 13.6, 12.0 and 8.0 mm at worst, 1.0, 1.0 and 0.6 mm rms,
//     over 0.56, 0.69 and 0.76 million vertices. All of it is the sampling geometry
//     below: at the same (u, v) the two grids stand up to 15.6 mm apart at a window's
//     corner, on sloping ground.
//   * On a LEGACY world (phase 8, no published detail) they do NOT agree. The patch is the
//     bilinear base plus its own micro relief (peaks 0.25-0.26 m at the default 0.4 m);
//     this port's HeightField applies its three bands whether or not the world publishes
//     them, and adds 3.6-6.4 m of relief at the most detail-prone land cell, so the two
//     differ by up to 6.4 m. No planner runs on such a world -- plans need stage 16 --
//     but a patch cut from one shows ground no other consumer sees. (The reference's
//     HeightField returned the bare base without a definition, so there the gap was the
//     micro relief alone; the port's HeightField chose otherwise before this module.)
//
// SAMPLING GEOMETRY. The patch is azimuthal-equidistant about its centre: vertex (u, v)
// is the point at geodesic distance hypot(u, v) along bearing (u, v). The planners are
// gnomonic: normalize(up + (east x + north z) / radius). The two agree at the centre and
// drift apart as r^3 / (3 R^2) with distance r from it.
//
// REFUSALS are machine-readable: PatchError carries the reference's refusal envelope --
// code, field, the value received, the bound or field list it violated, a clamped value
// that would be accepted, and whether a retry can help. See PatchRefusal.
//
// WHAT DIFFERS FROM THE REFERENCE, deliberately:
//
//   * Grids are flat and row-major, [z * size + x], rather than a list of rows.
//   * A world with no `height` layer is refused (`missing_block`, field
//     "world.layers.height") where the reference raised a bare KeyError. Note that this
//     port's phase-1 world HAS a height layer, all zeros; `patch_request` refuses it by
//     phase, as the reference's did, and `generate_patch` samples whatever it is handed.
//   * Only the envelope fields are carried; rendering them as the wire document
//     (`RequestError.document`) belongs to whatever wire layer this library grows.
//
// AND TWO REFERENCE DEFECTS REPRODUCED, each pinned by a test:
//
//   * The world refusal names `patch.span` and carries the span as the received value even
//     when it is `detail_height` that exceeds a hundredth of the radius. A caller
//     correcting the named field never clears it.
//   * A clamp suggestion is per field and blind to the vertex budget: spacing 0 is told to
//     try 0.1, and 0.1 at the default 64 m span is 640 segments, which is then refused. A
//     caller that trusts the suggestion is refused twice.
#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/config.hpp"
#include "fwg/detail.hpp"

namespace fwg {

struct World;

inline constexpr double kPatchLatitudeMaxDeg = 90.0;
inline constexpr double kPatchLongitudeMaxDeg = 180.0;
inline constexpr double kPatchSpanMinM = 4.0;
inline constexpr double kPatchSpanMaxM = 512.0;
inline constexpr double kPatchSpacingMinM = 0.1;
inline constexpr double kPatchSpacingMaxM = 8.0;
inline constexpr double kPatchDetailHeightMaxM = 5.0;
inline constexpr double kPatchDetailScaleMinM = 0.5;
inline constexpr double kPatchDetailScaleMaxM = 100.0;
// Segments per axis: 257 vertices.
inline constexpr int kPatchMaxSegments = 256;

struct PatchConfig {
    double latitude = 0.0;       // degrees, -90..90
    double longitude = 0.0;      // degrees, -180..180
    double span = 64.0;          // metres, 4..512
    double spacing = 0.5;        // metres, 0.1..8; the ACTUAL step is span / ceil(span / spacing)
    double detail_height = 0.4;  // metres, 0..5; legacy micro relief only
    double detail_scale = 8.0;   // metres, 0.5..100; legacy micro relief only

    // Throws PatchError at the first violation, in the reference's order: every field for
    // finiteness, then latitude, longitude, span, spacing, the vertex budget, detail_height,
    // detail_scale.
    void validate() const;
};

// The reference's refusal envelope (terrain_errors.RequestError), as data.
struct PatchRefusal {
    // INVALID_INPUT for every refusal a patch can make.
    std::string_view code = "INVALID_INPUT";
    // wrong_type | out_of_range | cross_field | refused_by_world | missing_block
    std::string_view kind;
    // "patch.span". EMPTY for a cross-field refusal, which no single field owns.
    std::string field;
    // The value sent. Absent for a cross-field or missing-block refusal.
    std::optional<double> received;

    // expected -----------------------------------------------------------------
    std::string_view expected_type;  // "number" on wrong_type and out_of_range
    std::optional<double> min;       // out_of_range
    std::optional<double> max;
    std::string_view units;          // out_of_range: degrees | m
    std::vector<std::string_view> fields;  // cross_field: every field that took part
    bool refused_by_world = false;         // refused_by_world: the request was fine, the ground was not
    std::string_view requires_block;       // missing_block

    // suggestion ---------------------------------------------------------------
    std::string_view suggestion = "none";    // clamp | none
    std::optional<double> suggestion_value;  // clamp: the nearest value inside the bound
    // False only when no change to the request can help.
    bool retry = true;

    std::string message;
};

// Subclasses std::invalid_argument for the reason the reference's RequestError subclasses
// ValueError: every existing handler of a refused config already catches that.
class PatchError : public std::invalid_argument {
public:
    explicit PatchError(PatchRefusal refusal);
    const PatchRefusal& refusal() const { return refusal_; }

private:
    PatchRefusal refusal_;
};

struct Patch {
    int patch_version = 1;  // 2 canonical, 1 legacy
    // The world's published relief definition; absent on a legacy world.
    std::optional<TerrainDetail> terrain_detail;
    Config world_config;  // the design-space config the world was built from
    int world_generator_version = 0;
    PatchConfig patch_config;
    double radius_m = 0.0;
    int size = 0;            // vertices per axis: ceil(span / spacing) + 1
    double spacing_m = 0.0;  // the actual step, span / (size - 1)
    std::uint32_t detail_seed = 0;
    // Legacy: whether detail_scale is at least two steps. Canonical: always true.
    bool detail_resolved = false;
    double origin_height_m = 0.0;  // the terrain height at the centre, which local y is measured from
    // Two triangles per cell, (a, b, a+1) and (a+1, b, b+1) with b = a + size. With x east
    // and z north each winds so its normal points up, out of the ground.
    std::vector<std::uint32_t> triangle_indices;
    std::string_view vertex_order = "row-major; flatten local_x/local_y/local_z; triangle normals face outward";
    std::string_view coordinates =
        "local tangent frame: x east, y up, z north; metres; origin on terrain at patch center";
    std::vector<std::string_view> warnings;
    double timing_ms = 0.0;

    // One value per vertex, row-major: [z * size + x], z northward, x eastward.
    std::vector<double> height;  // metres above the reference sphere, same datum as layers.height
    std::vector<double> detail;  // the relief above the coarse bilinear base
    std::vector<double> local_x;
    std::vector<double> local_y;
    std::vector<double> local_z;
};

// Validates `cfg`, refuses a patch the world cannot hold -- a span over half its radius or
// a detail height over a hundredth of it -- and samples.
//
// Throws PatchError for every refusal.
Patch generate_patch(const World& world, const PatchConfig& cfg);

// The reference's loopback request: regenerate the exact world asked for, then cut the
// patch from it. In the reference's order: the world request is resolved (resolve() throws
// std::invalid_argument for whatever a config refuses, a size over 1025 among them), the
// patch config is validated, and a world with no elevation yet -- phase below 2 -- is
// refused naming shape, size and phase, all before anything is generated. Regenerating a
// world per call is the caller's cost, as it was the reference's.
//
// The reference's other refusals here -- a body that is not an object, a misspelled or
// missing key -- are about parsing JSON, and typed arguments cannot express them.
Patch patch_request(const Request& world, const PatchConfig& patch);

}  // namespace fwg
