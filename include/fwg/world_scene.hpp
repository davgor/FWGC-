// One globe coordinate system for everything the planners built.
//
// Every city, hamlet and castle plan is laid out in its OWN local metres -- a gnomonic
// tangent frame at its raster node -- and the regional roads are node paths on the coarse
// raster. Nothing a game can drop a player into is in either form. This is the pass that
// puts all of it in one place: globe-centred XYZ metres, Y north, X at latitude 0
// longitude 0, Z at latitude 0 longitude 90, the same axes as a unit direction
// (cos(lat)cos(lon), sin(lat), cos(lat)sin(lon)).
//
//   * every planned building, as a box: a centre, an up axis that is the unit radial at
//     that centre, width and depth axes in the tangent plane, and four footprint corners;
//   * every street polyline, and every approach street from a connected junction;
//   * every junction between a settlement and a regional or support road;
//   * every regional road, draped onto the height field one vertex per ~4 m of arc, with
//     its river crossings carried as bridge CANDIDATES.
//
// FRAMES, EXACTLY. A plan's local (x, z) becomes a direction through LocalFrame -- the
// same object the planner sampled its ground through -- so a plot and the ground it was
// planned on cannot disagree about where it is: a building's up axis is bit-for-bit the
// direction its plot was planned at. A building's centre is its plot's RECORDED ground
// elevation (the planner's highest sampled corner), not a fresh height sample: the box
// stands where the planner said it stands. Everything else -- streets, junctions, road
// vertices -- sits on HeightField, the one continuous surface.
//
// TWO KINDS OF METRE, and they differ by (R + e) / R. A plan's window metres are metres
// on the REFERENCE SPHERE's tangent plane; a building's box is in true metres at its own
// elevation e. So a corner u window-metres from its plot's centre lands u R / (R + e) out
// once the box is on the globe: above the datum the box comes out smaller in the window
// than the planner drew it, below the datum larger. That is the reference's model, kept
// and pinned. It is not small here: this planet's radius is 31.8 km and its buildings
// stand up to 2.1 km high, so on seed 42 a footprint corner lands up to 1.17 m inside
// the planner's own corners() (size 33; 0.89 m at 65, 0.69 m at 129) and within 4 mm
// once the scale is taken out. The lowest buildings, 40 m below the datum, come out 0.12%
// LARGER in the window than the planner drew them. Streets and junctions are lifted
// radially from the window, so on the ground they are longer than the window says by
// the same factor.
//
// A building's axes: `east` is the frame's east projected into the tangent plane at the
// building and normalised, `north` is east x up, and the plot's rotation turns them --
//
//     width = east cos(r) + north sin(r)        depth = -east sin(r) + north cos(r)
//
// which is the planners' own `corners()` convention carried onto the sphere. Note the
// handedness: width x depth = -up. (east, up, north) is right-handed, so (width, depth,
// up) is not; a consumer building a rotation matrix from these three must take that into
// account rather than assume a right-handed box.
//
// A DRAPED ROAD IS STEEPER THAN ITS ROUTE. The router held each regional road to the
// 0.35 road grade node to node, one raster cell (1.6-6.3 km) at a time. Draped every 4 m
// onto the continuous surface, 0.6-1.4% of the steps on seed 42's 200 km world exceed that
// grade -- at worst 1.25 (size 65), where the coarse bilinear base alone climbs at 1.18.
// The scene reports the ground as it is; the route was never engineered for it, which is
// what `limits` says.
//
// ORDER IS A RECORD. Plans are walked cities, then hamlets, then castles, each in its
// block's own order; within a plan its street paths come first, then each road connection
// (its junction, then its approach street when connected), then its plots. Regional roads
// follow the road network's own order.
//
// WHAT DIFFERS FROM THE REFERENCE, deliberately:
//
//   * The reference rebuilt the whole scene three times -- at the end of fill_cities,
//     fill_hamlets and fill_castles -- and kept the last. It is built once here, after all
//     three, which is the scene the reference keeps.
//   * Each entry also carries its settlement's STABLE uid beside the reference's join key.
//     The reference keys hamlet and castle entries on the ordinal `hamlet_id` and
//     `fortress_id`, which renumber on every age advance; a game persisting anything
//     against a building needs the key that does not. The ids themselves are unchanged.
//   * `terrain_detail` records the definition the height field actually applied, not the
//     world's published copy. They are the same definition on every world that has one.
//   * A malformed request -- a city plan whose settlement is missing, a road node off the
//     node table -- is refused with std::invalid_argument where the reference raised a bare
//     StopIteration or IndexError out of the middle of a world.
#pragma once

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fwg/castle_plan.hpp"         // CastlePlan
#include "fwg/city_plan.hpp"           // CityPlan, HamletPlan, BoxSize
#include "fwg/detail.hpp"              // HeightField, TerrainDetail
#include "fwg/roads.hpp"               // Road
#include "fwg/settlements_stage.hpp"   // SettlementSite
#include "fwg/vec3.hpp"

namespace fwg {

struct SphereGrid;

inline constexpr int kWorldSceneVersion = 1;

// The regional-road densification step: one vertex per this many metres of arc on the
// REFERENCE sphere, per coarse segment, rounded up. Authored for the reference's 11 km
// world and NOT scaled; see docs/scale-defects.md for what it costs at shipped widths.
//
// The interpolation is a normalised chord, not a slerp, so the step is uniform in chord
// and not quite in arc: the middle of a segment of angle a is sampled 2 tan(a/2) / a
// times more coarsely than its ends. For a 1.5-8 km raster segment that is 0.02-0.6%
// over this figure, and it is the reference's arithmetic.
inline constexpr double kSceneRoadStepM = 4.0;

enum class SceneSettlementKind : int { City = 0, Hamlet, Castle };
// city | hamlet | castle -- the reference's `settlement_kind` values.
std::string_view to_string(SceneSettlementKind kind);

// Which settlement an entry belongs to.
struct SceneOwner {
    SceneSettlementKind kind = SceneSettlementKind::City;
    // The reference's join key, and the prefix of every id this settlement emits: a city's
    // `city_uid`, a hamlet's `hamlet_id`, a castle's `fortress_id`. The last two are
    // ORDINALS and renumber on an age advance.
    std::string id;
    // The key that does not renumber: `city_uid`, `hamlet_uid`, `fortress_uid`.
    std::string uid;
};

struct SceneBuilding {
    std::string id;  // "<owner id>:<plot id>"
    SceneOwner owner;
    std::string plot_id;
    std::string_view asset_id;  // the plot's building id
    // At the plot's recorded ground elevation, on the unit radial through the plot.
    Vec3 position_m;
    Vec3 up;
    Vec3 width_axis;
    Vec3 depth_axis;
    double rotation_degrees = 0.0;  // the plot's, carried unchanged
    BoxSize dimensions_m;
    // centre + u * width_axis + v * depth_axis for (u, v) = (-w/2, -d/2), (w/2, -d/2),
    // (w/2, d/2), (-w/2, d/2), with w and d the BUILDING's dimensions, not its plot's.
    std::array<Vec3, 4> footprint_positions_m{};
};

struct SceneStreet {
    // "<owner id>:street:<i>" for a street path; "<junction id>:approach" for the street
    // that joins a connected junction to its plan's street tree.
    std::string id;
    SceneOwner owner;
    std::string junction_id;  // set on an approach street only
    // On the height field, except an approach street's last vertex, which IS its
    // junction's position so the two meet exactly.
    std::vector<Vec3> positions_m;
};

struct SceneJunction {
    std::string id;  // the connection's junction id
    SceneOwner owner;
    std::string route_id;
    Vec3 position_m;          // on the height field at the gate
    std::string_view status;  // connected | unreachable
};

struct SceneRegionalRoad {
    std::string id;  // regional-road-<i>
    int source_route_index = 0;
    // From the from-city's gate to the to-city's gate where each has one, else from the
    // road's first node to its last. The end vertices are EXACTLY the junction positions.
    std::vector<Vec3> positions_m;
    // Empty where that end has no gate: the road never left its city's window, or the
    // window never projected it.
    std::string from_junction;
    std::string to_junction;
    std::string_view from_status = "no_city_boundary";
    std::string_view to_status = "no_city_boundary";
    // River crossings as node pairs, copied from the route. Candidates, not bridges.
    std::vector<std::pair<int, int>> bridge_candidates;
};

struct WorldScene {
    int version = kWorldSceneVersion;
    std::string_view unit = "metres";
    std::string_view coordinates =
        "globe-centred XYZ; Y north; X at latitude 0 longitude 0; Z at latitude 0 longitude 90";
    double radius_m = 0.0;
    TerrainDetail terrain_detail;
    std::vector<SceneBuilding> buildings;
    std::vector<SceneStreet> streets;
    std::vector<SceneJunction> junctions;
    std::vector<SceneRegionalRoad> regional_roads;
    std::string_view limits =
        "Regional routes remain coarse transport proposals, including unengineered bridge "
        "candidates; local connection failures are explicit. Hamlet entries are tagged "
        "settlement_kind=hamlet and castle entries settlement_kind=castle; both remain "
        "filterable from city payloads.";
};

struct WorldSceneRequest {
    // The raster size and the EFFECTIVE globe radius in metres -- the frame every plan was
    // laid out in. A design-space radius is refused: it disagrees with the height field.
    int n = 0;
    double radius_m = 0.0;
    // The continuous surface. Its radius must be `radius_m`.
    const HeightField* field = nullptr;
    // The node table road paths index into: node -> raster (x, z). Required when `roads`
    // is not empty, and its size must be `n`.
    const SphereGrid* nodes = nullptr;
    // Where each city stands. A city plan's frame is its settlement's raster node, found by
    // the plan's `city_uid` -- a site's uid, or its id's string when it has none.
    std::span<const SettlementSite> settlements;
    // The regional roads. `from`/`to` index `settlements`; `nodes` index `nodes`.
    std::span<const Road> roads;
    std::span<const CityPlan> cities;
    std::span<const HamletPlan> hamlets;
    std::span<const CastlePlan> castles;
};

// Pure: the same request always yields the same scene.
//
// Throws std::invalid_argument for a missing height field, a radius that disagrees with
// it, a size below 3, a missing or mis-sized node table when there are roads, a city plan
// whose settlement is not in `settlements`, or a road node outside the node table.
WorldScene build_world_scene(const WorldSceneRequest& request);

}  // namespace fwg
