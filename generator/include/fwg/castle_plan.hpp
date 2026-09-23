// What a route-defence fortress actually is on the ground.
//
// Stage 16 takes each fortress the populated-regions pass sited -- a pin on the world
// raster guarding a road -- and lays it out as a schematic castle in LOCAL METRES: a
// square raster of the ground around the pin, one wall ring per ring of a typology kit,
// each cut into curtain runs with a gatehouse facing the road, a postern opposite, corner
// towers and wall stairs, then a bailey court and a keep at the ring centres and the
// garrison's buildings fanned around the primary bailey.
//
// It exists because a fortress record says only WHERE a fort stands and why. A plan is
// the only record of which walls, gates and buildings it has, where each one stands and
// how many people it takes to hold it: `stats.workers` is the garrison the NPC roster
// opens one post per.
//
// Four things about the shape are load-bearing:
//
//   * **The kit list order is an RNG contract.** A fortress's kit is one uniform draw
//     walked cumulatively along `castle_kits()` in list order, so swapping two kits --
//     even two of EQUAL weight -- hands a different kit to the same fortress, and a
//     frequency count cannot see it. Appending is not safe either: the draw is scaled by
//     the TOTAL weight, so a new kit moves some fortresses between the old ones.
//   * **The registry is hashed whole and the digest gates an age advance.** See
//     castle_registry_identity(). The digest is over the document serialised with sorted
//     keys, so key order and whitespace are free and every value and list order is not.
//   * **A castle declares no sleeping capacity.** No castle structure carries a bed count,
//     so CastlePlot has no `beds` member at all rather than a zero nobody measured beside
//     a placed garrison.
//   * **Nothing here tests a plot against the ground or against another plot.** The
//     walls are clipped to buildable land; the buildings inside them are placed where
//     the schematic says, full stop. Courts and the keep share a centre, and a drawbridge
//     can hang outside the window. That is the reference's model, reproduced.
//
// THE REFERENCE'S GEOMETRY IS REPRODUCED WITH ITS DEFECTS, and the tests pin each one so
// a fix is a decision rather than an accident: the corner test measures the interior
// angle, so every vertex of a convex ring is a corner and the tower budget alone decides
// where towers go (the first vertices from +x, bunched); a ring's `scale` does not
// survive clipping, so every ring of a kit collapses onto the same boundary; curtain runs
// restart at each polyline vertex, so they come out ragged and the stretch before each
// vertex -- roughly a third of every ring -- carries no curtain at all, on a ring still
// reported `walkway_continuous`; the tower budget counts candidates, so a gate beside a
// candidate vertex costs a tower; and a ring whose clip collapses falls back to the raw
// ellipse, which nothing checks against the ground.
//
// WHAT DIFFERS FROM THE REFERENCE, deliberately:
//
//   * The kit draw is keyed on the fortress's stable `uid` ("fortress-node-<n>"), not its
//     ordinal `id`. Ordinals renumber on every age advance, so the reference re-drew a
//     fort's kit whenever the list was re-sorted even though its ground never moved. The
//     plan still JOINS on the ordinal, as consumers expect, and carries the uid and node
//     beside it: the reference's castle plan had no stable key at all.
//   * The kit draw is this library's own stream, one uniform from a named child. The
//     reference seeded CPython's Mersenne Twister from a hex string.
//   * `stats` is a truthful summary on every path. The reference drops keys on its two
//     early returns and writes `segments: 0` there even when a broken ring carries curtain.
//
// AND ONE THING THAT LOOKS LIKE A DIFFERENCE AND IS NOT: the neighbour crop measures the
// short way across the longitude seam, and so did the reference's. Its spherical law of
// cosines is even and 2*pi-periodic in the longitude difference, whatever the comments
// beside its copies say, so it agrees with great_circle_m to within 1e-10 m.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fwg/city_plan.hpp"          // Point, CellSet, Structure, Pass, RoadConnection, TerrainRaster
#include "fwg/humans.hpp"             // RuralSite: what a castle is planned on
#include "fwg/settlements_stage.hpp"  // SettlementSite: what crops its window

namespace fwg {

struct World;
struct SphereGrid;

// Written into every plan and into the identity an age advance compares. Bumping either
// refuses every saved world at that gate.
inline constexpr int kCastlePlanVersion = 3;
inline constexpr int kCastleGeometryVersion = 1;

// ===========================================================================
// Content: the castle registry (data/castles.json)
// ===========================================================================

struct CastleBand {
    double min = 0.0;
    double max = 0.0;
    double nominal = 0.0;
};

// One fortification module. Reached BY KEY -- `curtain_segment`, `gatehouse` -- so the
// table's order is free.
struct CastleModule {
    std::string_view key;
    std::string_view id;            // module.gatehouse
    std::string_view structure_id;  // building.barbican: the building registry row it stands for
    std::string_view role;          // curtain | node | court | landmark
    // Empty where the registry says null. Such a module has no join end, and a join
    // against it is refused rather than given one.
    std::string_view end_type;
    // Absent on the court and the keep, which never join a wall.
    std::optional<CastleBand> thickness_m;
    std::optional<CastleBand> height_m;
    bool walkway = false;
    std::span<const std::string_view> compatible_ends;
};

struct CastleJoinRules {
    double max_thickness_delta_m = 0.0;
    double max_height_delta_m = 0.0;
    // Compared against castle_turn_degrees, which is the INTERIOR angle: 25 admits every
    // vertex that is not a needle. See the header comment.
    double min_turn_degrees_for_corner = 0.0;
    // Never read by any code path. Carried because it is inside the registry digest.
    double max_turn_degrees = 0.0;
    // The target curtain run, and (through max(8, 0.6 * depth)) the gate-collision radius.
    double segment_depth_m = 0.0;
    bool walkway_required = false;
};

struct CastleRingSpec {
    std::string_view role;  // the ring id is "ring-<role>"
    // Scales the ellipse BEFORE clipping, which normalises each ray -- so it only shows on
    // the fallback path where the unclipped ellipse is used. See the header comment.
    double scale = 1.0;
    bool ditch = false;
    std::string_view gate;  // a module key; empty means "gatehouse"
    bool postern = false;
    int stairs = 0;
    int tower_budget = 4;
};

struct CastleCourtSpec {
    std::string_view role;  // never read
    std::string_view ring_role;
    std::string_view structure_id;
};

struct CastleLandmarkSpec {
    std::string_view structure_id;
    std::string_view ring_role;
    double offset_scale = 0.0;  // never read: every landmark stands at its ring's centroid
};

struct CastleServiceSpec {
    std::string_view structure_id;
    int count = 1;
};

struct CastleKit {
    std::string_view id;
    std::string_view name;
    int weight = 1;
    double half_m = 0.0;  // the window's half-extent before any neighbour crops it
    // ORDER CONTRACT: rings[0] is the PRIMARY ring -- the only one shown the real road
    // approach, and the one the garrison is fanned around.
    std::span<const CastleRingSpec> rings;
    std::span<const CastleCourtSpec> courts;
    std::span<const CastleLandmarkSpec> landmarks;
    // Installed in this order, and the order is the fan's angular sequence.
    std::span<const CastleServiceSpec> services;
};

// Sorted by key. Order is free: every lookup is by name.
std::span<const CastleModule> castle_modules();
// nullptr for an unknown key rather than a default module: a join against a module that
// does not exist must fail, not succeed against zeros.
const CastleModule* castle_module(std::string_view key);
const CastleJoinRules& castle_join_rules();
// FILE ORDER, which is the RNG contract described at the top of this header.
std::span<const CastleKit> castle_kits();
int castle_registry_revision();

// A structure from the CASTLE library only. The reference resolves every castle row
// against that library alone, so a kit naming a city building gets "Unknown castle
// structure" rather than quietly building a well from the common catalogue.
const Structure* castle_structure(std::string_view id);

// sha256 over the castle registry serialised exactly as the reference hashes it --
// sort_keys=True, separators (',', ':'), ensure_ascii at its default -- recomputed here
// from the embedded bytes. data/castles.json records the expected value and the codegen
// refuses a mismatch. This is NOT the scheme the heritage or civilization registries
// use; do not borrow theirs.
const std::string& castle_registry_identity();

// The same serialisation over the castle library of the BUILDING registry, flattened to
// {structure id: structure}. A separate registry and a separate digest: the two join
// only by structure id.
const std::string& castle_structures_identity();

// What an age advance compares before it lets a saved world move on. A world whose
// stored identity differs was built by different castle data and must be regenerated.
struct CastlePlannerIdentity {
    int version = kCastlePlanVersion;
    int geometry_version = kCastleGeometryVersion;
    std::string castles_sha256;
    std::string castle_buildings_sha256;

    bool operator==(const CastlePlannerIdentity&) const = default;
};
CastlePlannerIdentity castle_planner_identity();

// ===========================================================================
// Kit selection
// ===========================================================================

// The weighted walk: the first kit whose running weight sum EXCEEDS unit * total, and the
// last kit if rounding leaves none. `unit` is in [0, 1). Exposed on its own because the
// order contract is a property of this walk, not of any particular draw.
std::size_t castle_kit_index(std::span<const CastleKit> kits, double unit);

// One uniform from the child stream "castle-kit:<fortress_key>" of the world seed.
// `fortress_key` is the fortress's stable uid; see the header comment for why.
const CastleKit& select_castle_kit(std::uint32_t world_seed, std::string_view fortress_key);

// ===========================================================================
// Wall-ring geometry. Pure: no world, no grid, no randomness.
// ===========================================================================

// The signed angle AT b between the edges b->a and b->c, degrees in (-180, 180].
//
// The reference's docstring calls this the "exterior turn". It is not: a straight run
// reads 180 and a regular convex 32-gon reads +/-168.75, where the heading change
// (path_turn_degrees) would read 0 and 11.25. The tower rule was built on THIS number, so
// it is kept as measured and its consequence is pinned by a test.
double castle_turn_degrees(Point a, Point b, Point c);

// Every index whose |castle_turn_degrees| is at least `min_turn_degrees`, ascending, with
// the ring wrapped. Empty below three points.
std::vector<std::size_t> castle_corner_indices(const std::vector<Point>& polyline,
                                               double min_turn_degrees);

struct CastleJoinCheck {
    bool ok = false;
    std::string reason;  // empty when ok
};

// End-type compatibility, then the thickness and height deltas, then walkway continuity.
// An unset dimension defaults to the module's nominal. Either module being nullptr, or
// having no join end, fails -- a join is proved, never assumed.
CastleJoinCheck castle_can_join(const CastleModule* a, const CastleModule* b, const CastleJoinRules& rules,
                                std::optional<double> thickness_a = std::nullopt,
                                std::optional<double> thickness_b = std::nullopt,
                                std::optional<double> height_a = std::nullopt,
                                std::optional<double> height_b = std::nullopt);

// `samples` points on an axis-aligned ellipse from angle 0 (+x), anticlockwise, rounded
// to the centimetre. OPEN: the last point is not the first.
std::vector<Point> castle_ellipse_ring(double rx, double rz, int samples = 48);

// Snap each ring point along its ray from the LOCAL ORIGIN to the outermost buildable
// cell, walking inward from `half` in `cell` steps. A ray that finds no such cell
// contributes nothing, so the ring shortens; eight or more survivors are smoothed.
//
// Only each point's DIRECTION is used, which is why a uniformly scaled ellipse clips to
// the same ring. `slopes` is one value per raster cell, [j * size + i]; a cell steeper than
// `max_slope_degrees` is skipped even if it is in `valid`.
std::vector<Point> castle_clip_ring(const std::vector<Point>& points, const CellSet& valid, double half,
                                    int cell, double max_slope_degrees, const std::vector<double>& slopes);

struct CastleSegment {
    std::string id;  // ring-outer-seg-3. NOT contiguous once the gate filter has run.
    std::string ring_id;
    std::string_view structure_id;
    std::string_view module_id;
    Point from_m;
    Point to_m;
    Point center_m;
    double length_m = 0.0;
    // The heading MINUS 90: a wall module's depth axis runs along the wall.
    double rotation_degrees = 0.0;
    double thickness_m = 0.0;
    double height_m = 0.0;
    bool walkway = true;
};

struct CastleWallNode {
    std::string id;  // ring-outer-node-7: one per polyline vertex, never filtered
    std::string_view kind = "vertex";
    Point position_m;
    std::string ring_id;
};

// A gate, postern, tower or stair: anything that joins the curtain.
struct CastleJoin {
    std::string id;  // ring-outer-gate-0 | -postern-0 | -tower-2 | -stair-1
    std::string ring_id;
    std::string_view kind;  // gate | postern | tower | stair
    std::string_view module_key;
    std::string_view module_id;
    std::string_view structure_id;
    Point position_m;
    // Gates and posterns only: where the approach to this opening comes from.
    std::optional<Point> approach_m;
    double thickness_m = 0.0;
    double height_m = 0.0;          // the structure's own height; it may rise above the wall
    double curtain_height_m = 0.0;  // the height the join is judged at
    bool walkway = true;
};

// One thing that could not be built. `building_id` is a structure id -- or, when a join
// failed validation, the id of the SEGMENT or NODE that failed, which is what the
// reference records there and the only thing that locates the failure.
struct CastleUnplaced {
    std::string building_id;
    int count = 1;
    std::string reason;
};

struct CastleJoinFailure {
    std::string segment_id;  // exactly one of these two is set
    std::string node_id;
    std::string reason;
};

std::pair<std::vector<CastleSegment>, std::vector<CastleWallNode>> castle_segmentize(
    const std::vector<Point>& polyline, double segment_depth_m, std::string_view structure_id,
    const std::string& ring_id, double thickness_m, double height_m);

// The opening's ring vertex: nearest to `approach_m`, or without one the +x extremity,
// ties to the point nearest the z = 0 line. FIRST index on an exact tie. nullopt for an
// empty ring.
std::optional<std::size_t> castle_gate_index(const std::vector<Point>& polyline,
                                             std::optional<Point> approach_m);
std::size_t castle_opposite_index(const std::vector<Point>& polyline, std::size_t index);

// Every curtain segment against itself and every join against the curtain. Both sides of
// each dimension test carry the same value, so with the shipped registry only a missing
// module, a missing or incompatible end, or a walkway break can fail -- which is exactly
// what an edited registry must still be refused for.
std::vector<CastleJoinFailure> castle_validate_joins(const std::vector<CastleSegment>& segments,
                                                     const std::vector<CastleJoin>& joins,
                                                     std::span<const CastleModule> modules,
                                                     const CastleJoinRules& rules);

struct CastleWallNetwork {
    std::string id;  // ring-<role>
    std::string_view role;
    std::vector<Point> polyline_m;
    // open: never segmented. broken: segmented, but a join failed. closed: a real wall.
    std::string_view status = "open";
    std::vector<CastleSegment> segments;  // the curtain AFTER the gate filter
    std::vector<CastleWallNode> nodes;
    std::vector<CastleJoin> gates;  // the gate first, then the postern if any
    std::vector<CastleJoin> towers;
    std::vector<CastleJoin> stairs;
    bool walkway_continuous = false;
    std::vector<CastleUnplaced> unplaced;
    // Meaningful on a closed ring only.
    int segment_count = 0;
    int gate_count = 0;
    bool ditch = false;
    // False when clipping to buildable land collapsed the ring and the raw ellipse was
    // used instead. NOTHING re-checks that ellipse against the ground, so a consumer
    // drawing this wall has to know it may cross water.
    bool clipped_to_terrain = true;
};

struct CastleWallRequest {
    std::string ring_id;
    std::string_view role;
    std::vector<Point> polyline;
    std::optional<Point> approach_m;
    CastleRingSpec ring;
    // Looked up by key. The shipped table is castle_modules(); a test hands in its own.
    std::span<const CastleModule> modules;
    CastleJoinRules rules;
    bool ditch = false;
};

// One ring: segmentize, open a gate on the approach and a postern opposite, drop the
// curtain that collides with either, place towers and stairs, then prove every join.
//
// Throws std::invalid_argument when a module the ring needs is not in `modules`: the
// reference fails there too, and a ring built against a module that does not exist would
// be a wall of nothing.
CastleWallNetwork build_castle_wall(const CastleWallRequest& request);

// ===========================================================================
// Plans
// ===========================================================================

// A building in the castle. Deliberately NO `beds` member: see the header comment.
struct CastlePlot {
    std::string id;  // plot-<n>, the index at insertion time
    std::string_view building_id;
    std::string_view name;
    std::string_view kind;   // service | court | landmark | gate | tower | stair
    std::string_view phase;  // courts | landmarks | services | perimeter
    double x_m = 0.0;
    double z_m = 0.0;
    // NOT normalised to any range. The service fan steps 63 degrees a building and never
    // wraps, so a sixth service reads past 600.
    double rotation_degrees = 0.0;
    double ground_elevation_m = 0.0;   // highest of the four plot corners
    double foundation_bottom_m = 0.0;  // lowest of them
    PlotSize plot_m;
    BoxSize dimensions_m;
    // The plot's OWN centre. A castle has no streets for it to front.
    Point road_access;
    int workers = 0;
};

struct CastleStats {
    int wall_rings_closed = 0;
    // Summed over EVERY ring, closed or broken: a broken ring still carries its curtain.
    int segments = 0;
    int plots = 0;
    int workers = 0;
    std::string_view kit_id;
    bool walkway_continuous = false;
    // A ROW count. A hamlet's `unplaced_total` sums row counts instead; the two fields
    // share a name and not a meaning.
    int unplaced_total = 0;
};

struct CastleDebug {
    std::size_t valid_cells = 0;
    std::optional<Point> approach_m;
    int geometry_version = kCastleGeometryVersion;
};

struct CastlePlan {
    int version = kCastlePlanVersion;
    std::string_view kind = "castle";
    // The JOIN KEY consumers read, and an ordinal that renumbers on an age advance.
    std::string fortress_id;
    // What does not renumber: the ground the fortress stands on.
    std::string fortress_uid;
    int node = -1;
    // A POSITION in the settlement list, not a uid. -1 when the fortress has none.
    int core_id = -1;
    int x = 0;
    int z = 0;
    std::string_view kit_id;
    std::string_view kit_name;
    double half_m = 0.0;  // bounds are [-half, -half, half, half]
    TerrainRaster terrain;
    // Six, in phase order. `perimeter` is the closed-ring count PLUS one per gate, tower
    // and stair plot, because both write to the same pass -- the reference's number, kept.
    std::vector<Pass> passes;
    std::vector<CastlePlot> plots;
    std::vector<CastleWallNetwork> wall_networks;
    std::vector<CastleUnplaced> unplaced;
    // At most one support-road entry, and it is NEVER promoted to connected: a castle has
    // no street tree to connect it to.
    std::vector<RoadConnection> road_connections;
    std::vector<std::string_view> warnings;
    double source_resolution_m = 0.0;
    // unbuildable: too little land to wall. partial: something was not built.
    // complete: every ring closed and nothing is unplaced.
    std::string_view status = "unbuildable";
    CastleStats stats;
    CastleDebug debug;
};

struct CastlePlans {
    int version = kCastlePlanVersion;
    CastlePlannerIdentity identity;
    std::vector<CastlePlan> castles;  // one per fortress, in ascending id order
    std::vector<std::string_view> phase_order;
    std::string_view limits;
    std::string_view beds;
};

struct CastleRequest {
    // Layers, the continuous height field, the raster size and the world seed.
    const World* world = nullptr;
    // MUST be built at `globe_radius_m`. Its node table projects the support road and the
    // river centrelines into the window.
    const SphereGrid* grid = nullptr;
    // METRES: world.effective.globe_radius, not world.config's. The two differ by the
    // world scale, 5.6 on the default world, and every reach in a plan is absolute metres
    // -- a design-space radius puts every river line and road crossing in the wrong place.
    // Refused, not documented: it must agree with the grid and with the world.
    double globe_radius_m = 0.0;
    // Every city. Each crops the window; only x and z are read.
    std::span<const SettlementSite> sites;
    // Every fortress. Each is planned by fill_castles and each crops the others.
    std::span<const RuralSite> fortresses;
};

// Plan one fortress. `fortress` need not be an element of request.fortresses; any entry
// there with the same id is skipped when cropping, as the fortress itself is.
//
// Throws std::invalid_argument on a missing world or grid, a radius that disagrees with
// either, or a fortress with no uid to key its kit on.
CastlePlan plan_castle(const CastleRequest& request, const RuralSite& fortress);

// The same plan with the kit handed in rather than drawn. The draw is the contract; this
// is for a kit the shipped registry does not contain -- or for asking what a site would
// look like as each kit in turn.
//
// The plan VIEWS the kit's strings (its ids, roles and gate keys), so whatever they point
// into must outlive the plan. The shipped table and string literals always do.
//
// Throws std::invalid_argument, before sampling any ground, for a kit with no ring or one
// naming a gate module, court or landmark that does not exist. An unknown SERVICE is
// instead reported unplaced, as the reference reports it.
CastlePlan plan_castle_with_kit(const CastleRequest& request, const RuralSite& fortress,
                                const CastleKit& kit);

// Every fortress, planned in ascending order of its id STRING: "fortress-10" before
// "fortress-2". A castle carries no cross-castle state, so the order decides only the
// order of the output, which is a record consumers read. Planned on worker threads
// (workers.hpp), each into its own slot.
CastlePlans fill_castles(const CastleRequest& request);

}  // namespace fwg
