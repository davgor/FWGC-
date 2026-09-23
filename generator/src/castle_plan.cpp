#include "fwg/castle_plan.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "fwg/detail.hpp"
#include "fwg/grid.hpp"
#include "fwg/random.hpp"
#include "fwg/local_sampler.hpp"
#include "fwg/villains.hpp"  // round_dp
#include "fwg/world.hpp"

namespace fwg {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Every metre constant below is LOCAL -- measured inside one castle window, which is at
// most 192 m across -- and none of them is multiplied by any world-size factor. That is
// correct for all of them: a curtain run, a gate's clearance or a river's setback is the
// same size on a small planet and a large one. What does change with the planet is how
// often these windows meet anything, and that is recorded where it bites (the crop).

constexpr int kCell = 4;  // metres per raster cell
// Above this a cell is blocked, and a ray may not snap to it.
constexpr double kSlopeLimitDegrees = 25.0;
// The river setbacks (12 m water, 28 m channel, 40 m padding) live with the shared
// sampler in local_sampler.cpp.
// DIMENSIONLESS: a neighbour crops the window to this fraction of the metre distance to
// it. It only ever binds against a neighbour within half_m / 0.28, 257 to 343 m. The
// shipped worlds' raster steps are 1.5 to 6 km, so outside the polar rows -- where
// longitude convergence brings nodes to ~150 m -- every castle keeps its kit's full extent
// and this crop is effectively dead. At the 11 km reference world it bound hard.
constexpr double kNeighbourCrop = 0.28;
// Fewer buildable cells than this cannot hold a precinct. A hamlet's floor is 8.
constexpr std::size_t kMinPrecinctCells = 24;
constexpr int kRingSamples = 32;
// The ellipse every ring starts from, as fractions of the window. Fixed for every kit.
constexpr double kRingRx = 0.72;
constexpr double kRingRz = 0.62;
// A tower is not raised at a corner this close to a gate or postern: the opening's own
// structure stands there.
constexpr double kTowerGateClearanceM = 10.0;
// How far out along the approach a drawbridge spans the ditch from the gate.
constexpr double kDrawbridgeReachM = 10.0;
// The garrison ring, as a fraction of the window, and its angular step: 63 degrees, which
// overruns a full turn after six services and is never wrapped.
constexpr double kServiceRadius = 0.22;
constexpr double kServiceStep = kPi * 0.35;
// A gate plot this close to an existing plot of the same structure is the same building.
constexpr double kGateDedupM = 1.0;

double radians(double degrees) { return degrees * kPi / 180.0; }
double degrees_of(double rad) { return rad * 180.0 / kPi; }
double distance2d(Point a, Point b) { return std::hypot(a.x - b.x, a.z - b.z); }

// The shortest text that reads back as the same double, with ".0" on an integral value --
// the way the reference prints a float into a reason string, so "exceeds 1.0" stays
// "exceeds 1.0" rather than becoming "exceeds 1".
std::string float_text(double value) {
    std::array<char, 64> buffer{};
    const auto written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    std::string text(buffer.data(), written.ptr);
    if (text.find_first_of(".eEn") == std::string::npos) text += ".0";
    return text;
}

std::string fixed3(double value) {
    std::array<char, 64> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.3f", value);
    return std::string(buffer.data());
}

const CastleModule* find_module(std::span<const CastleModule> modules, std::string_view key) {
    for (const CastleModule& m : modules) {
        if (m.key == key) return &m;
    }
    return nullptr;
}

// The reference raises a KeyError here and takes the whole world with it. A module a ring
// needs and cannot find is a broken registry, not a smaller castle.
const CastleModule& require_module(std::span<const CastleModule> modules, std::string_view key) {
    const CastleModule* found = find_module(modules, key);
    if (found == nullptr) {
        throw std::invalid_argument("castle wall needs module '" + std::string(key) +
                                    "' and the registry has none");
    }
    return *found;
}

double nominal_height(const CastleModule& module) {
    if (!module.height_m.has_value()) {
        throw std::invalid_argument("castle module '" + std::string(module.key) +
                                    "' has no height, and a join is judged by one");
    }
    return module.height_m->nominal;
}

Point centroid(const std::vector<Point>& points) {
    double x = 0.0;
    double z = 0.0;
    for (const Point& p : points) {
        x += p.x;
        z += p.z;
    }
    const auto count = static_cast<double>(points.size());
    return {x / count, z / count};
}

}  // namespace

// ===========================================================================
// Content accessors
// ===========================================================================

const CastleModule* castle_module(std::string_view key) { return find_module(castle_modules(), key); }

const Structure* castle_structure(std::string_view id) {
    for (const Structure& s : structures()) {
        if (s.library == "castle" && s.id == id) return &s;
    }
    return nullptr;
}

CastlePlannerIdentity castle_planner_identity() {
    CastlePlannerIdentity identity;
    identity.castles_sha256 = castle_registry_identity();
    identity.castle_buildings_sha256 = castle_structures_identity();
    return identity;
}

// ===========================================================================
// Kit selection
// ===========================================================================

std::size_t castle_kit_index(std::span<const CastleKit> kits, double unit) {
    if (kits.empty()) throw std::invalid_argument("a castle kit draw needs at least one kit");
    double total = 0.0;
    for (const CastleKit& kit : kits) {
        // The reference reads max(1, weight), so a zero there silently means one. Refused
        // instead: a kit nobody should draw must not be quietly drawable.
        if (kit.weight < 1) {
            throw std::invalid_argument("castle kit '" + std::string(kit.id) + "' has weight " +
                                        std::to_string(kit.weight) + "; the least a drawable kit has is 1");
        }
        total += kit.weight;
    }
    const double target = unit * total;
    double running = 0.0;
    for (std::size_t i = 0; i < kits.size(); ++i) {
        running += kits[i].weight;
        // EXCEEDS, not reaches: a draw landing exactly on a running sum belongs to the next
        // kit, as a bisect-right over the running sums puts it.
        if (running > target) return i;
    }
    return kits.size() - 1;
}

const CastleKit& select_castle_kit(std::uint32_t world_seed, std::string_view fortress_key) {
    if (fortress_key.empty()) {
        throw std::invalid_argument("a castle kit is drawn on the fortress's stable uid, and this one has none");
    }
    std::string domain = "castle-kit:";
    domain.append(fortress_key);
    Rng stream = Rng::child(static_cast<std::uint64_t>(world_seed), domain);
    const std::span<const CastleKit> kits = castle_kits();
    return kits[castle_kit_index(kits, stream.random())];
}

// ===========================================================================
// Wall-ring geometry
// ===========================================================================

double castle_turn_degrees(Point a, Point b, Point c) {
    const double first = std::atan2(a.z - b.z, a.x - b.x);
    const double second = std::atan2(c.z - b.z, c.x - b.x);
    double delta = degrees_of(second - first);
    while (delta <= -180.0) delta += 360.0;
    while (delta > 180.0) delta -= 360.0;
    return delta;
}

std::vector<std::size_t> castle_corner_indices(const std::vector<Point>& polyline, double min_turn_degrees) {
    std::vector<std::size_t> hits;
    const std::size_t n = polyline.size();
    if (n < 3) return hits;
    for (std::size_t i = 0; i < n; ++i) {
        const Point& before = polyline[(i + n - 1) % n];
        const Point& after = polyline[(i + 1) % n];
        if (std::abs(castle_turn_degrees(before, polyline[i], after)) >= min_turn_degrees) hits.push_back(i);
    }
    return hits;
}

CastleJoinCheck castle_can_join(const CastleModule* a, const CastleModule* b, const CastleJoinRules& rules,
                                std::optional<double> thickness_a, std::optional<double> thickness_b,
                                std::optional<double> height_a, std::optional<double> height_b) {
    if (a == nullptr || b == nullptr) return {false, "Missing module"};
    if (a->end_type.empty() || b->end_type.empty()) return {false, "Module has no join end"};
    const auto accepts = [](const CastleModule& module, std::string_view end) {
        return std::find(module.compatible_ends.begin(), module.compatible_ends.end(), end) !=
               module.compatible_ends.end();
    };
    // Either side accepting the other is enough: the curtain lists every node's end, and
    // no node lists the curtain's in reverse.
    if (!accepts(*a, b->end_type) && !accepts(*b, a->end_type)) {
        return {false, "Incompatible ends " + std::string(a->end_type) + "/" + std::string(b->end_type)};
    }
    if (!thickness_a && a->thickness_m) thickness_a = a->thickness_m->nominal;
    if (!thickness_b && b->thickness_m) thickness_b = b->thickness_m->nominal;
    if (!height_a && a->height_m) height_a = a->height_m->nominal;
    if (!height_b && b->height_m) height_b = b->height_m->nominal;
    if (!thickness_a || !thickness_b || !height_a || !height_b) return {false, "Missing thickness or height"};
    const double thickness_delta = std::abs(*thickness_a - *thickness_b);
    if (thickness_delta > rules.max_thickness_delta_m) {
        return {false, "Thickness delta " + fixed3(thickness_delta) + " exceeds " +
                           float_text(rules.max_thickness_delta_m)};
    }
    const double height_delta = std::abs(*height_a - *height_b);
    if (height_delta > rules.max_height_delta_m) {
        return {false, "Height delta " + fixed3(height_delta) + " exceeds " + float_text(rules.max_height_delta_m)};
    }
    if (rules.walkway_required && a->walkway != b->walkway) return {false, "Walkway continuity broken"};
    return {true, {}};
}

std::vector<Point> castle_ellipse_ring(double rx, double rz, int samples) {
    std::vector<Point> points;
    if (samples <= 0) return points;
    points.reserve(static_cast<std::size_t>(samples));
    for (int i = 0; i < samples; ++i) {
        const double angle = 2.0 * kPi * i / samples;
        points.push_back({round_dp(rx * std::cos(angle), 2), round_dp(rz * std::sin(angle), 2)});
    }
    return points;
}

std::vector<Point> castle_clip_ring(const std::vector<Point>& points, const CellSet& valid, double half, int cell,
                                    double max_slope_degrees, const std::vector<double>& slopes) {
    if (points.empty() || valid.empty()) return {};
    if (cell <= 0) throw std::invalid_argument("castle_clip_ring needs a positive cell size");
    const int size = valid.size();
    if (slopes.size() != static_cast<std::size_t>(size) * static_cast<std::size_t>(size)) {
        throw std::invalid_argument("castle_clip_ring needs one slope per raster cell");
    }
    std::vector<Point> snapped;
    snapped.reserve(points.size());
    for (const Point& p : points) {
        double length = std::hypot(p.x, p.z);
        // A point at the exact origin has no ray. It walks the origin itself rather than
        // dividing by zero.
        if (length == 0.0) length = 1.0;
        const double ux = p.x / length;
        const double uz = p.z / length;
        // From the OUTSIDE in, so the ring takes the outermost buildable ground on each ray
        // even when a lake lies between it and the centre.
        for (int dist = static_cast<int>(std::max(half, 1.0)); dist > 0; dist -= cell) {
            const double x = ux * dist;
            const double z = uz * dist;
            const Cell at{static_cast<int>(std::floor((x + half) / cell)),
                          static_cast<int>(std::floor((z + half) / cell))};
            if (!valid.contains(at)) continue;
            if (slopes[static_cast<std::size_t>(at.second) * static_cast<std::size_t>(size) +
                       static_cast<std::size_t>(at.first)] > max_slope_degrees) {
                continue;
            }
            snapped.push_back({round_dp(x, 2), round_dp(z, 2)});
            break;
        }
    }
    // Returned untouched below eight points: there is no ring to smooth, and the caller
    // decides what a ring that short means.
    return smooth_closed_ring(snapped);
}

std::pair<std::vector<CastleSegment>, std::vector<CastleWallNode>> castle_segmentize(
    const std::vector<Point>& polyline, double segment_depth_m, std::string_view structure_id,
    const std::string& ring_id, double thickness_m, double height_m) {
    std::vector<CastleSegment> segments;
    std::vector<CastleWallNode> nodes;
    if (polyline.size() < 3) return {std::move(segments), std::move(nodes)};
    // Cut points are rounded to the centimetre, so a run much shorter than that rounds
    // back onto the point it started from and the walk below never advances.
    if (!(segment_depth_m >= 0.1)) {
        throw std::invalid_argument("a curtain run must be at least 0.1 m; got " + float_text(segment_depth_m));
    }
    for (std::size_t i = 0; i < polyline.size(); ++i) {
        nodes.push_back({ring_id + "-node-" + std::to_string(i), "vertex", polyline[i], ring_id});
    }

    int seg_i = 0;
    double carry = 0.0;
    for (std::size_t k = 0; k < polyline.size(); ++k) {
        Point a = polyline[k];
        const Point b = polyline[(k + 1) % polyline.size()];
        // Each edge's first run starts at the edge's own VERTEX, not at the point `carry`
        // metres back along the previous edge. So the arc consumed since the last cut is a
        // full module but the chord emitted spans only what was left of it: runs come out
        // ragged, between a centimetre and a module long, and each ring keeps its corners.
        Point start = a;
        double edge = distance2d(a, b);
        while (carry + edge >= segment_depth_m - 0.01) {
            const double need = segment_depth_m - carry;
            const double t = edge != 0.0 ? need / edge : 0.0;
            const Point end{round_dp(a.x + (b.x - a.x) * t, 2), round_dp(a.z + (b.z - a.z) * t, 2)};
            const double heading = degrees_of(std::atan2(end.z - start.z, end.x - start.x));
            CastleSegment segment;
            segment.id = ring_id + "-seg-" + std::to_string(seg_i);
            segment.ring_id = ring_id;
            segment.structure_id = structure_id;
            segment.module_id = "module.curtain_segment";
            segment.from_m = start;
            segment.to_m = end;
            segment.center_m = {round_dp((start.x + end.x) / 2.0, 2), round_dp((start.z + end.z) / 2.0, 2)};
            segment.length_m = round_dp(distance2d(start, end), 3);
            // A wall module's depth axis runs along the wall.
            segment.rotation_degrees = round_dp(heading - 90.0, 4);
            segment.thickness_m = thickness_m;
            segment.height_m = height_m;
            segment.walkway = true;
            segments.push_back(std::move(segment));
            ++seg_i;
            start = end;
            a = end;
            edge = distance2d(a, b);
            carry = 0.0;
        }
        carry += edge;
    }
    return {std::move(segments), std::move(nodes)};
}

std::optional<std::size_t> castle_gate_index(const std::vector<Point>& polyline, std::optional<Point> approach_m) {
    if (polyline.empty()) return std::nullopt;
    std::size_t best = 0;
    if (!approach_m) {
        // The +x extremity: a deterministic stand-in for a road nobody projected. STRICTLY
        // greater, so the first vertex wins a tie.
        for (std::size_t i = 1; i < polyline.size(); ++i) {
            const Point& p = polyline[i];
            const Point& q = polyline[best];
            if (p.x > q.x || (p.x == q.x && -std::abs(p.z) > -std::abs(q.z))) best = i;
        }
        return best;
    }
    double best_distance = distance2d(polyline[0], *approach_m);
    for (std::size_t i = 1; i < polyline.size(); ++i) {
        const double d = distance2d(polyline[i], *approach_m);
        if (d < best_distance) {
            best_distance = d;
            best = i;
        }
    }
    return best;
}

std::size_t castle_opposite_index(const std::vector<Point>& polyline, std::size_t index) {
    if (polyline.empty()) throw std::invalid_argument("an empty ring has no opposite vertex");
    return (index + polyline.size() / 2) % polyline.size();
}

std::vector<CastleJoinFailure> castle_validate_joins(const std::vector<CastleSegment>& segments,
                                                     const std::vector<CastleJoin>& joins,
                                                     std::span<const CastleModule> modules,
                                                     const CastleJoinRules& rules) {
    std::vector<CastleJoinFailure> failures;
    const CastleModule& curtain = require_module(modules, "curtain_segment");
    for (const CastleSegment& seg : segments) {
        const CastleJoinCheck check = castle_can_join(&curtain, &curtain, rules, seg.thickness_m, seg.thickness_m,
                                                      seg.height_m, seg.height_m);
        if (!check.ok) failures.push_back({seg.id, {}, check.reason});
    }
    for (const CastleJoin& join : joins) {
        // Node structures may rise above the walkway; join continuity is judged at the
        // CURTAIN's height, not the tower's.
        const CastleJoinCheck check =
            castle_can_join(&curtain, find_module(modules, join.module_key), rules, join.thickness_m,
                            join.thickness_m, join.curtain_height_m, join.curtain_height_m);
        if (!check.ok) failures.push_back({{}, join.id, check.reason.empty() ? "join failed" : check.reason});
    }
    return failures;
}

CastleWallNetwork build_castle_wall(const CastleWallRequest& request) {
    CastleWallNetwork report;
    report.id = request.ring_id;
    report.role = request.role;
    report.polyline_m = request.polyline;
    const std::vector<Point>& polyline = request.polyline;
    if (polyline.size() < 8) {
        report.unplaced.push_back({"building.curtain_segment", 1, "Perimeter could not close"});
        return report;
    }

    const CastleModule& curtain = require_module(request.modules, "curtain_segment");
    if (!curtain.thickness_m.has_value()) {
        throw std::invalid_argument("the curtain has no thickness, and every join on the wall takes its thickness");
    }
    const double thickness = curtain.thickness_m->nominal;
    const double height = nominal_height(curtain);
    auto [segments, vertices] = castle_segmentize(polyline, request.rules.segment_depth_m, curtain.structure_id,
                                                  request.ring_id, thickness, height);
    if (segments.size() < 6) {
        report.unplaced.push_back({std::string(curtain.structure_id), 1, "Insufficient continuous wall segments"});
        return report;
    }

    const auto join_at = [&](std::string id, std::string_view kind, std::string_view key, const CastleModule& module,
                             Point position, std::optional<Point> approach) {
        CastleJoin join;
        join.id = std::move(id);
        join.ring_id = request.ring_id;
        join.kind = kind;
        join.module_key = key;
        join.module_id = module.id;
        join.structure_id = module.structure_id;
        join.position_m = position;
        join.approach_m = approach;
        join.thickness_m = thickness;
        join.height_m = nominal_height(module);
        join.curtain_height_m = height;
        join.walkway = true;
        return join;
    };

    std::vector<CastleJoin> gates;
    const std::size_t gate_idx = *castle_gate_index(polyline, request.approach_m);
    const std::string_view gate_key = request.ring.gate.empty() ? std::string_view("gatehouse") : request.ring.gate;
    gates.push_back(join_at(request.ring_id + "-gate-0", "gate", gate_key, require_module(request.modules, gate_key),
                            polyline[gate_idx], request.approach_m ? *request.approach_m : polyline[gate_idx]));
    if (request.ring.postern) {
        const std::size_t post_idx = castle_opposite_index(polyline, gate_idx);
        gates.push_back(join_at(request.ring_id + "-postern-0", "postern", "postern",
                                require_module(request.modules, "postern"), polyline[post_idx],
                                polyline[post_idx]));
    }

    // Drop the curtain that collides with an opening, so the gate is a real gap in the wall
    // rather than a gatehouse standing in front of one. Ids were assigned before this, so
    // the survivors' ids skip.
    const double collision = std::max(8.0, request.rules.segment_depth_m * 0.6);
    std::vector<CastleSegment> keep;
    for (CastleSegment& seg : segments) {
        bool blocked = false;
        for (const CastleJoin& gate : gates) {
            if (distance2d(seg.center_m, gate.position_m) < collision) {
                blocked = true;
                break;
            }
        }
        if (!blocked) keep.push_back(std::move(seg));
    }

    // THE BUDGET COUNTS CANDIDATES, NOT TOWERS. A candidate beside an opening is skipped
    // and still spends its slot, so its tower is simply missing and the ids skip it.
    std::vector<CastleJoin> towers;
    const std::vector<std::size_t> corners = castle_corner_indices(polyline, request.rules.min_turn_degrees_for_corner);
    for (std::size_t t_i = 0; t_i < corners.size(); ++t_i) {
        if (static_cast<long long>(t_i) >= request.ring.tower_budget) break;
        const Point at = polyline[corners[t_i]];
        bool beside_gate = false;
        for (const CastleJoin& gate : gates) {
            beside_gate = beside_gate || distance2d(at, gate.position_m) < kTowerGateClearanceM;
        }
        if (beside_gate) continue;
        towers.push_back(join_at(request.ring_id + "-tower-" + std::to_string(t_i), "tower", "corner_join",
                                 require_module(request.modules, "corner_join"), at, std::nullopt));
    }

    // Spread along the SURVIVING curtain, so a stair never lands in a gate's gap.
    std::vector<CastleJoin> stairs;
    if (request.ring.stairs > 0 && !keep.empty()) {
        const std::size_t budget = static_cast<std::size_t>(request.ring.stairs);
        const std::size_t step = std::max<std::size_t>(1, keep.size() / budget);
        const CastleModule& stair_module = require_module(request.modules, "wall_stair");
        for (std::size_t k = 0, s_i = 0; k < keep.size() && s_i < budget; k += step, ++s_i) {
            stairs.push_back(join_at(request.ring_id + "-stair-" + std::to_string(s_i), "stair", "wall_stair",
                                     stair_module, keep[k].center_m, std::nullopt));
        }
    }

    std::vector<CastleJoin> joins = gates;
    joins.insert(joins.end(), towers.begin(), towers.end());
    joins.insert(joins.end(), stairs.begin(), stairs.end());
    const std::vector<CastleJoinFailure> failures = castle_validate_joins(keep, joins, request.modules, request.rules);

    report.segments = std::move(keep);
    report.nodes = std::move(vertices);
    report.gates = std::move(gates);
    report.towers = std::move(towers);
    report.stairs = std::move(stairs);
    if (!failures.empty()) {
        // Broken, not open: the wall exists and says exactly which join could not be
        // proved, by the id of the segment or node that failed.
        for (const CastleJoinFailure& f : failures) {
            report.unplaced.push_back({f.segment_id.empty() ? f.node_id : f.segment_id, 1, f.reason});
        }
        report.status = "broken";
        return report;
    }
    report.status = "closed";
    report.walkway_continuous = true;
    report.segment_count = static_cast<int>(report.segments.size());
    report.gate_count = static_cast<int>(report.gates.size());
    report.ditch = request.ditch || request.ring.ditch;
    return report;
}

// ===========================================================================
// The ground in the window
// ===========================================================================

namespace {

struct CastleGround {
    TerrainRaster terrain;
    CellSet valid;
    std::vector<double> slopes;  // [j*size + i]
};

CastleGround raster_ground(const LocalSampler& sampler, double half, int size) {
    CastleGround out;
    out.terrain.cell_m = kCell;
    out.terrain.size = size;
    const auto area = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    out.terrain.codes.resize(area);
    out.terrain.natural_biome.resize(area);
    out.terrain.biome_variant.resize(area);
    out.slopes.resize(area);
    out.valid = CellSet(size);
    for (int j = 0; j < size; ++j) {
        for (int i = 0; i < size; ++i) {
            const LocalSample v = sampler.sample(-half + (i + 0.5) * kCell, -half + (j + 0.5) * kCell);
            const auto at = static_cast<std::size_t>(j) * static_cast<std::size_t>(size) + static_cast<std::size_t>(i);
            out.slopes[at] = v.slope;
            // Standing water, real steepness and the reserved river channel block a cell;
            // unlike a hamlet, a castle does not build in a river's setback.
            const int code = v.water ? 1 : ((v.slope > kSlopeLimitDegrees || v.channel != 0) ? 2 : 0);
            out.terrain.codes[at] = code;
            out.terrain.natural_biome[at] = v.biome;
            out.terrain.biome_variant[at] = v.variant;
            if (code == 0) out.valid.insert({i, j});
        }
    }
    const int surface_size = size + 1;
    const double step = 2.0 * half / (surface_size - 1);
    out.terrain.surface_size = surface_size;
    out.terrain.surface_step_m = step;
    out.terrain.surface_heights_m.resize(static_cast<std::size_t>(surface_size) * static_cast<std::size_t>(surface_size));
    for (int j = 0; j < surface_size; ++j) {
        for (int i = 0; i < surface_size; ++i) {
            out.terrain.surface_heights_m[static_cast<std::size_t>(j) * static_cast<std::size_t>(surface_size) +
                                          static_cast<std::size_t>(i)] =
                round_dp(sampler.height_at(-half + i * step, -half + j * step), 4);
        }
    }
    return out;
}

struct SupportApproach {
    std::optional<Point> gate;
    std::vector<RoadConnection> entries;
};

// Where the fortress's own road leaves the window: the first crossing of the window edge by
// the access path, projected node by node toward the parent city.
SupportApproach support_approach(const SphereGrid& grid, const LocalSampler& sampler, const RuralSite& fortress,
                                 double half) {
    SupportApproach out;
    if (fortress.access_nodes.size() < 2 || grid.node_count() == 0) return out;
    Point prev{0.0, 0.0};
    for (std::size_t k = 1; k < fortress.access_nodes.size(); ++k) {
        const int node = fortress.access_nodes[k];
        if (node < 0 || static_cast<std::size_t>(node) >= grid.node_count()) break;
        const Vec3 p = direction(grid.point_x[static_cast<std::size_t>(node)],
                                 grid.point_z[static_cast<std::size_t>(node)], grid.n);
        const double den = dot(p, sampler.up());
        // NOT the river projection's 0.1: a road node merely has to be on the near side.
        if (den <= 0.0) break;
        const Point q{sampler.radius() * dot(p, sampler.east()) / den, sampler.radius() * dot(p, sampler.north()) / den};
        if (std::max(std::abs(q.x), std::abs(q.z)) > half) {
            // Neither division can be by zero: `prev` is the origin or a node that stayed
            // inside the window on BOTH axes, so on any axis where q is outside, q and prev
            // differ. The reference's spec calls this reachable; it is not.
            double t = std::numeric_limits<double>::infinity();
            if (std::abs(q.x) > half) t = std::min(t, ((q.x > 0.0 ? half : -half) - prev.x) / (q.x - prev.x));
            if (std::abs(q.z) > half) t = std::min(t, ((q.z > 0.0 ? half : -half) - prev.z) / (q.z - prev.z));
            const Point gate{prev.x + t * (q.x - prev.x), prev.z + t * (q.z - prev.z)};
            RoadConnection entry;
            entry.route_id = "support:" + fortress.id;
            entry.route_index = -1;
            entry.junction_id = "junction:" + fortress.id + ":support";
            entry.gate_local_m = gate;
            entry.direction = sampler.direction_at(gate.x, gate.z);
            entry.status = "unreachable";
            entry.reason = "No terrain-safe street connection to parent-city approach";
            out.gate = gate;
            out.entries.push_back(std::move(entry));
            break;
        }
        prev = q;
    }
    return out;
}

void check_request(const CastleRequest& request) {
    if (request.world == nullptr || request.grid == nullptr) {
        throw std::invalid_argument("a castle plan needs the world and a sphere grid: the ground is sampled from "
                                    "one and the road and rivers are projected through the other");
    }
    const double radius = request.globe_radius_m;
    if (!(radius > 0.0) || !std::isfinite(radius)) {
        throw std::invalid_argument("a castle plan needs a positive globe radius in metres");
    }
    const double tolerance = 1e-6 * std::max(1.0, radius);
    if (std::abs(request.grid->radius - radius) > tolerance) {
        throw std::invalid_argument("castle plan given a grid of radius " + float_text(request.grid->radius) +
                                    " m for a world of " + float_text(radius) +
                                    " m; a design-space grid puts every river line and road crossing in the "
                                    "wrong place in a window measured in metres");
    }
    if (std::abs(request.world->effective.globe_radius - radius) > tolerance) {
        throw std::invalid_argument("castle plan given a radius of " + float_text(radius) + " m but the world is " +
                                    float_text(request.world->effective.globe_radius) +
                                    " m; the height field reads the world's own, so the relief and the frame "
                                    "would disagree about where every metre is");
    }
    if (request.grid->n != request.world->config.size) {
        throw std::invalid_argument("castle plan given a grid of size " + std::to_string(request.grid->n) +
                                    " for a world of size " + std::to_string(request.world->config.size));
    }
    if (request.world->layers.find("height") == request.world->layers.end()) {
        throw std::invalid_argument("a castle plan needs the world's height layer");
    }
}

// A kit handed in by a caller is checked before any ground is sampled, so a bad kit fails
// the same way on every site rather than only on the sites where its ring happens to close.
// A SERVICE naming an unknown structure is the exception: the reference reports that one
// as unplaced rather than failing, and so does this.
void check_kit(const CastleKit& kit) {
    if (kit.rings.empty()) {
        throw std::invalid_argument("castle kit '" + std::string(kit.id) + "' has no ring");
    }
    for (const CastleRingSpec& ring : kit.rings) {
        const std::string_view gate = ring.gate.empty() ? std::string_view("gatehouse") : ring.gate;
        if (castle_module(gate) == nullptr) {
            throw std::invalid_argument("castle kit '" + std::string(kit.id) + "' opens its " +
                                        std::string(ring.role) + " ring with unknown module '" + std::string(gate) +
                                        "'");
        }
    }
    for (const CastleCourtSpec& court : kit.courts) {
        if (castle_structure(court.structure_id) == nullptr) {
            throw std::invalid_argument("castle kit '" + std::string(kit.id) + "' builds unknown court '" +
                                        std::string(court.structure_id) + "'");
        }
    }
    for (const CastleLandmarkSpec& landmark : kit.landmarks) {
        if (castle_structure(landmark.structure_id) == nullptr) {
            throw std::invalid_argument("castle kit '" + std::string(kit.id) + "' builds unknown landmark '" +
                                        std::string(landmark.structure_id) + "'");
        }
    }
}

void summarise(CastlePlan& plan, const CastleKit& kit) {
    CastleStats& stats = plan.stats;
    stats = CastleStats{};
    stats.kit_id = kit.id;
    stats.plots = static_cast<int>(plan.plots.size());
    for (const CastlePlot& plot : plan.plots) stats.workers += plot.workers;
    // A truthful summary on every path. The reference drops keys on its two early returns
    // and states `segments: 0` there even when a broken ring carries curtain; a struct
    // cannot drop a key, and a zero nobody measured is the thing the beds ruling refuses.
    bool continuous = true;
    for (const CastleWallNetwork& network : plan.wall_networks) {
        stats.segments += static_cast<int>(network.segments.size());
        if (network.status != "closed") continue;
        ++stats.wall_rings_closed;
        continuous = continuous && network.walkway_continuous;
    }
    // With no closed ring there is no walkway to be continuous: false, not the vacuous true
    // an `all()` over nothing returns.
    stats.walkway_continuous = stats.wall_rings_closed > 0 && continuous;
    stats.unplaced_total = static_cast<int>(plan.unplaced.size());
}

}  // namespace

// ===========================================================================
// plan_castle
// ===========================================================================

CastlePlan plan_castle_with_kit(const CastleRequest& request, const RuralSite& fortress, const CastleKit& kit) {
    check_request(request);
    check_kit(kit);
    const World& world = *request.world;
    const SphereGrid& grid = *request.grid;
    const int n = grid.n;
    const double radius = request.globe_radius_m;

    // Bound the window by every city and every OTHER fortress without moving any of them.
    // The seam needs no special case: see the header comment.
    double half = kit.half_m;
    const Vec3 here = direction(fortress.x, fortress.z, n);
    const auto crop = [&](int x, int z) {
        half = std::min(half, great_circle_m(radius, here, direction(x, z, n)) * kNeighbourCrop);
    };
    for (const SettlementSite& site : request.sites) crop(site.x, site.z);
    for (const RuralSite& other : request.fortresses) {
        if (&other == &fortress || other.id == fortress.id) continue;
        crop(other.x, other.z);
    }
    // Truncated toward zero to a whole cell, and never below one: a fort crowded onto a
    // neighbour's node keeps a 4 m window that the precinct test then refuses.
    half = std::max(static_cast<double>(kCell), std::floor(half / kCell) * kCell);
    const auto size = static_cast<int>(2.0 * half / kCell);

    const LocalSampler sampler(world, grid, radius, fortress.x, fortress.z, half);
    CastleGround ground = raster_ground(sampler, half, size);

    CastlePlan plan;
    plan.fortress_id = fortress.id;
    plan.fortress_uid = fortress.uid;
    plan.node = fortress.node;
    plan.core_id = fortress.core_id;
    plan.x = fortress.x;
    plan.z = fortress.z;
    plan.kit_id = kit.id;
    plan.kit_name = kit.name;
    plan.half_m = half;
    plan.terrain = std::move(ground.terrain);
    static constexpr std::string_view kPhases[] = {"map", "kit", "perimeter", "courts", "landmarks", "services"};
    for (std::string_view phase : kPhases) plan.passes.push_back({phase, 0});
    plan.warnings = {
        "Schematic fortification modules; structural engineering and garrison beds are not resolved.",
        "Curtain segments are metre-scale wall runs; production ARCH meshes remain unassigned."};
    plan.source_resolution_m = round_dp(sampler.resolution(), 4);
    plan.debug.valid_cells = ground.valid.count();

    plan.passes[0].placed = 1;
    if (ground.valid.count() < kMinPrecinctCells) {
        plan.unplaced.push_back({"building.curtain_segment", 1, "Insufficient buildable land for a fortress precinct"});
        summarise(plan, kit);
        return plan;
    }
    plan.passes[1].placed = 1;
    const SupportApproach approach = support_approach(grid, sampler, fortress, half);
    plan.road_connections = approach.entries;
    plan.debug.approach_m = approach.gate;

    const double rx = half * kRingRx;
    const double rz = half * kRingRz;
    for (std::size_t ring_i = 0; ring_i < kit.rings.size(); ++ring_i) {
        const CastleRingSpec& spec = kit.rings[ring_i];
        const std::vector<Point> raw = castle_ellipse_ring(rx * spec.scale, rz * spec.scale, kRingSamples);
        std::vector<Point> polyline =
            castle_clip_ring(raw, ground.valid, half, kCell, kSlopeLimitDegrees, ground.slopes);
        const bool clipped = polyline.size() >= 8;
        // Fall back to the unclipped ellipse when clipping collapses the ring. Nothing
        // re-checks it against the ground; `clipped_to_terrain` says so.
        if (!clipped) polyline = raw;

        CastleWallRequest wall;
        wall.ring_id = "ring-" + std::string(spec.role);
        wall.role = spec.role;
        wall.polyline = std::move(polyline);
        // ONLY the primary ring sees the real road. Every inner ring's gate falls back to
        // the +x extremity.
        if (ring_i == 0) wall.approach_m = approach.gate;
        wall.ring = spec;
        wall.modules = castle_modules();
        wall.rules = castle_join_rules();
        wall.ditch = spec.ditch;
        CastleWallNetwork network = build_castle_wall(wall);
        network.clipped_to_terrain = clipped;
        if (network.status != "closed") {
            plan.unplaced.insert(plan.unplaced.end(), network.unplaced.begin(), network.unplaced.end());
        }
        plan.wall_networks.push_back(std::move(network));
    }

    long long closed = 0;
    for (const CastleWallNetwork& network : plan.wall_networks) closed += network.status == "closed" ? 1 : 0;
    plan.passes[2].placed = closed;
    if (closed == 0) {
        plan.status = "partial";
        summarise(plan, kit);
        return plan;
    }

    // NO FOOTPRINT TEST OF ANY KIND. A castle plot is placed where the schematic says:
    // not against the buildable mask, not against other plots, not inside the window.
    const auto install = [&](std::string_view structure_id, double x, double z, double degrees,
                             std::string_view phase, std::string_view kind) {
        const Structure* row = castle_structure(structure_id);
        if (row == nullptr) {
            throw std::invalid_argument("castle plot for unknown structure '" + std::string(structure_id) + "'");
        }
        double ground_max = -std::numeric_limits<double>::infinity();
        double ground_min = std::numeric_limits<double>::infinity();
        for (const Point& corner : corners(x, z, row->plot_m.width, row->plot_m.depth, radians(degrees))) {
            const double h = sampler.height_at(corner.x, corner.z);
            ground_max = std::max(ground_max, h);
            ground_min = std::min(ground_min, h);
        }
        CastlePlot plot;
        plot.id = "plot-" + std::to_string(plan.plots.size());
        plot.building_id = row->id;
        plot.name = row->name;
        plot.kind = kind;
        plot.phase = phase;
        plot.x_m = round_dp(x, 2);
        plot.z_m = round_dp(z, 2);
        plot.rotation_degrees = round_dp(degrees, 4);
        plot.ground_elevation_m = round_dp(ground_max, 4);
        plot.foundation_bottom_m = round_dp(ground_min, 4);
        plot.plot_m = row->plot_m;
        plot.dimensions_m = row->dimensions_m;
        plot.road_access = {plot.x_m, plot.z_m};
        plot.workers = row->workers;
        plan.plots.push_back(std::move(plot));
        for (Pass& pass : plan.passes) {
            if (pass.id == phase) ++pass.placed;
        }
    };

    // The LAST closed ring of a role, as the reference's role-keyed dict keeps. The codegen
    // refuses a kit with two rings of one role, so only a caller's own kit can tell.
    const auto closed_ring = [&](std::string_view role) -> const std::vector<Point>* {
        const std::vector<Point>* found = nullptr;
        for (const CastleWallNetwork& network : plan.wall_networks) {
            if (network.status == "closed" && network.role == role) found = &network.polyline_m;
        }
        return found;
    };
    // Every court and landmark stands at its ring's CENTROID. Because clipping collapses
    // every ring onto one boundary, two courts and the keep share a point.
    for (const CastleCourtSpec& court : kit.courts) {
        const std::vector<Point>* ring = closed_ring(court.ring_role);
        if (ring == nullptr || ring->empty()) {
            plan.unplaced.push_back({std::string(court.structure_id), 1,
                                     "No closed ring " + std::string(court.ring_role) + " for court"});
            continue;
        }
        const Point c = centroid(*ring);
        install(court.structure_id, c.x, c.z, 0.0, "courts", "court");
    }
    for (const CastleLandmarkSpec& landmark : kit.landmarks) {
        const std::vector<Point>* ring = closed_ring(landmark.ring_role);
        if (ring == nullptr || ring->empty()) {
            plan.unplaced.push_back({std::string(landmark.structure_id), 1,
                                     "No closed ring " + std::string(landmark.ring_role) + " for keep"});
            continue;
        }
        const Point c = centroid(*ring);
        install(landmark.structure_id, c.x, c.z, 0.0, "landmarks", "landmark");
    }

    // The garrison, fanned around the PRIMARY bailey's centre starting a quarter turn off
    // the approach, so the first service does not stand in the gate's road. An open primary
    // skips the whole fan with no report.
    const CastleWallNetwork& primary = plan.wall_networks.front();
    if (primary.status == "closed") {
        const Point c = centroid(primary.polyline_m);
        double angle0 = 0.0;
        if (approach.gate) angle0 = std::atan2(approach.gate->z - c.z, approach.gate->x - c.x);
        int service_i = 0;
        for (const CastleServiceSpec& service : kit.services) {
            const std::string_view sid = service.structure_id;
            if (castle_structure(sid) == nullptr) {
                plan.unplaced.push_back({std::string(sid), 1, "Unknown castle structure"});
                continue;
            }
            if (sid == "building.drawbridge") {
                if (!primary.ditch || primary.gates.empty()) {
                    plan.unplaced.push_back({std::string(sid), 1, "Drawbridge requires outer ditch and gate"});
                    continue;
                }
                // Out along the approach from the gate, across the ditch. It does not take
                // a slot in the fan.
                const Point g = primary.gates.front().position_m;
                install(sid, g.x + std::cos(angle0) * kDrawbridgeReachM, g.z + std::sin(angle0) * kDrawbridgeReachM,
                        degrees_of(angle0), "services", "service");
                continue;
            }
            bool barbican_gate = false;
            for (const CastleJoin& gate : primary.gates) {
                barbican_gate = barbican_gate || gate.structure_id == "building.barbican";
            }
            // Already represented as the gate node: a second barbican would be the same
            // gatehouse twice. Skipped silently, and it takes no slot in the fan.
            if (sid == "building.barbican" && barbican_gate) continue;
            for (int k = 0; k < service.count; ++k) {
                const double angle = angle0 + kPi * 0.5 + service_i * kServiceStep;
                const double reach = half * kServiceRadius;
                install(sid, c.x + std::cos(angle) * reach, c.z + std::sin(angle) * reach,
                        degrees_of(angle + kPi / 2.0), "services", "service");
                ++service_i;
            }
        }
    }

    // Gate, tower and stair plots, for the consumers that read only plots[]. They count
    // into the SAME perimeter pass the closed rings did.
    for (const CastleWallNetwork& network : plan.wall_networks) {
        if (network.status != "closed") continue;
        for (const CastleJoin& gate : network.gates) {
            // Only a gate is de-duplicated, and only against the same structure: inner rings
            // collapse onto the outer one, so their fallback gates can land on a gate
            // already built.
            bool built = false;
            for (const CastlePlot& p : plan.plots) {
                built = built || (p.building_id == gate.structure_id &&
                                  distance2d({p.x_m, p.z_m}, gate.position_m) < kGateDedupM);
            }
            if (built) continue;
            install(gate.structure_id, gate.position_m.x, gate.position_m.z, 0.0, "perimeter", "gate");
        }
        for (const CastleJoin& tower : network.towers) {
            install(tower.structure_id, tower.position_m.x, tower.position_m.z, 0.0, "perimeter", "tower");
        }
        for (const CastleJoin& stair : network.stairs) {
            install(stair.structure_id, stair.position_m.x, stair.position_m.z, 0.0, "perimeter", "stair");
        }
    }

    summarise(plan, kit);
    plan.status = (plan.stats.wall_rings_closed == static_cast<int>(kit.rings.size()) && plan.unplaced.empty())
                      ? "complete"
                      : "partial";
    return plan;
}

CastlePlan plan_castle(const CastleRequest& request, const RuralSite& fortress) {
    check_request(request);
    return plan_castle_with_kit(request, fortress, select_castle_kit(request.world->config.seed, fortress.uid));
}

CastlePlans fill_castles(const CastleRequest& request) {
    check_request(request);
    std::vector<const RuralSite*> order;
    order.reserve(request.fortresses.size());
    for (const RuralSite& fortress : request.fortresses) order.push_back(&fortress);
    // Lexicographic on the id STRING, stable so two rows sharing an id keep the caller's
    // order rather than one the sort happened to leave.
    std::stable_sort(order.begin(), order.end(), [](const RuralSite* a, const RuralSite* b) { return a->id < b->id; });

    CastlePlans out;
    out.identity = castle_planner_identity();
    out.phase_order = {"map", "kit", "perimeter", "courts", "landmarks", "services"};
    out.limits = "Schematic fortification modules and bailey plots; not structural engineering or production art.";
    out.beds =
        "A castle plan declares no sleeping capacity. No structure in the castle registry carries a bed count, "
        "so castle plots carry no beds key at all rather than a zero that was never measured. The garrison a "
        "plan staffs is stats.workers, which npc_roster opens one post per.";
    out.castles.reserve(order.size());
    for (const RuralSite* fortress : order) out.castles.push_back(plan_castle(request, *fortress));
    return out;
}

}  // namespace fwg
