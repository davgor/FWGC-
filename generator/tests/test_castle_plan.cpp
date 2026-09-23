// Castle plans: the registry and its digest, the kit draw, the wall rings, and the castle
// packed around them.
//
// The geometry tests run on hand-built rings and rasters, because what they are about is
// how a ring is cut, opened and joined. The planning tests run on real fortresses in a
// generated world, because a fortress's window, its road and its rivers are only ever
// what stage 12 put there.
//
// Several tests here PIN DEFECTS carried over from the reference -- the interior-angle
// corner test, rings that collapse onto one boundary, curtain runs that start at every
// vertex, a tower budget that counts candidates, buildings placed with no ground test.
// Each is the reference's behaviour, kept on purpose; the test exists so that fixing one
// is a decision somebody makes and not something that happens while tidying.
#include "check.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "fwg/castle_plan.hpp"
#include "fwg/grid.hpp"
#include "fwg/villains.hpp"  // great_circle_m
#include "fwg/world.hpp"

using namespace fwg;

namespace {

constexpr double kPi = 3.14159265358979323846;

// Whether a plot type has a bed count at all. A concept, because a requires-expression
// outside a template is a hard error rather than false.
template <typename T>
concept CarriesBeds = requires(const T& plot) { plot.beds; };

struct Fortified {
    World world;
    std::shared_ptr<const SphereGrid> grid;

    // Spans into `world`: the request must not outlive this.
    CastleRequest request() const {
        CastleRequest r;
        r.world = &world;
        r.grid = grid.get();
        r.globe_radius_m = world.effective.globe_radius;
        r.sites = world.settlements.sites;
        r.fortresses = world.humans.fortresses;
        return r;
    }
};

// A shipped-width world with its fortresses, which is where every castle is planned.
Fortified fortified(std::uint32_t seed = 42, int size = 65) {
    Request r;
    r.seed = seed;
    r.size = size;
    r.phase = 12;
    Fortified f;
    f.world = generate(resolve(r));
    // effective, not config: config is design space and every reach in a plan is metres.
    f.grid = sphere_grid(f.world.config.size, f.world.effective.globe_radius);
    return f;
}

// By id, never by position: position is the draw contract, and only the golden test
// should notice when it moves.
const CastleKit& kit_named(std::string_view id) {
    for (const CastleKit& kit : castle_kits()) {
        if (kit.id == id) return kit;
    }
    check::fail("a kit named " + std::string(id));
}

const RuralSite* fortress_drawing(const Fortified& f, std::string_view kit_id) {
    for (const RuralSite& fortress : f.world.humans.fortresses) {
        if (select_castle_kit(f.world.config.seed, fortress.uid).id == kit_id) return &fortress;
    }
    return nullptr;
}

// A fortress pinned by hand onto a chosen node. Its uid follows the convention every
// real one carries, because the kit is drawn on it.
RuralSite fortress_at(const Fortified& f, int x, int z, const std::string& id) {
    RuralSite site;
    site.id = id;
    site.kind = "fortress";
    site.node = f.grid->node_index(x, z);
    site.uid = "fortress-node-" + std::to_string(site.node);
    site.x = x;
    site.z = z;
    return site;
}

// Checked access, so a plan missing what a test expects FAILS that test rather than
// reading past an empty vector and taking the whole run down with it.
const CastleWallNetwork& primary_ring(const CastlePlan& plan) {
    if (plan.wall_networks.empty()) check::fail(plan.fortress_id + " to have a wall network");
    return plan.wall_networks.front();
}

const CastleJoin& main_gate(const CastleWallNetwork& ring) {
    if (ring.gates.empty()) check::fail(ring.id + " to have a gate");
    return ring.gates.front();
}

const RoadConnection& support_road(const CastlePlan& plan) {
    if (plan.road_connections.empty()) check::fail(plan.fortress_id + " to have a support road");
    return plan.road_connections.front();
}

const CastleWallNetwork* ring_named(const CastlePlan& plan, std::string_view id) {
    for (const CastleWallNetwork& network : plan.wall_networks) {
        if (network.id == id) return &network;
    }
    return nullptr;
}

std::size_t count_kind(const CastlePlan& plan, std::string_view kind) {
    std::size_t n = 0;
    for (const CastlePlot& plot : plan.plots) n += plot.kind == kind ? 1 : 0;
    return n;
}

double distance(Point a, Point b) { return std::hypot(a.x - b.x, a.z - b.z); }

// A regular n-gon of radius r, anticlockwise from +x.
std::vector<Point> polygon(int n, double r) {
    std::vector<Point> points;
    for (int k = 0; k < n; ++k) {
        const double angle = 2.0 * kPi * k / n;
        points.push_back({r * std::cos(angle), r * std::sin(angle)});
    }
    return points;
}

CastleRingSpec ring_spec(std::string_view gate = "gatehouse", bool postern = true, int stairs = 2,
                         int tower_budget = 4) {
    CastleRingSpec spec;
    spec.role = "outer";
    spec.scale = 1.0;
    spec.gate = gate;
    spec.postern = postern;
    spec.stairs = stairs;
    spec.tower_budget = tower_budget;
    return spec;
}

CastleWallRequest wall_request(std::vector<Point> polyline, std::optional<Point> approach,
                               CastleRingSpec spec = ring_spec()) {
    CastleWallRequest request;
    request.ring_id = "ring-outer";
    request.role = "outer";
    request.polyline = std::move(polyline);
    request.approach_m = approach;
    request.ring = spec;
    request.modules = castle_modules();
    request.rules = castle_join_rules();
    return request;
}

}  // namespace

// --- the registry and its identity ------------------------------------------

TEST(the_castle_registry_identity_reproduces_the_recorded_digest) {
    // The digest an age advance compares before it lets a saved world move on. Recomputed
    // here from the embedded bytes, so this is the check that can fail for a real reason:
    // the bytes, the serialisation (sorted keys, tightest separators, ASCII escapes) and
    // the hash all have to agree with what data/castles.json recorded.
    const std::string& identity = castle_registry_identity();
    check::equal(identity.size(), std::size_t{64}, "a full sha256 hex digest");
    check::equal(identity, std::string("5a11ec3b6d1657858fc93f6d291780c93cfc380bdbe9e3ac00ff85025616f53f"),
                 "the castles_sha256 data/castles.json records");
}

TEST(the_building_half_of_the_identity_is_a_digest_of_its_own) {
    // Measured from the reference's planner_identity() at b67c99b. Nothing records it in a
    // data file, because it is a digest of ANOTHER registry's castle library: editing a
    // castle structure in buildings.json retires every saved world, and this is where
    // that shows.
    check::equal(castle_structures_identity(),
                 std::string("568f5d262d38a0da4b6ef9d325649c1adb2d27921548535847a7aebf9e8a51c3"),
                 "the castle_buildings_sha256 the reference publishes");

    const CastlePlannerIdentity identity = castle_planner_identity();
    check::equal(identity.version, 3, "the planner version the digest was recorded under");
    check::equal(identity.geometry_version, 1, "and the geometry version");
    check::equal(identity.castles_sha256, castle_registry_identity(), "the castle registry's digest");
    check::equal(identity.castle_buildings_sha256, castle_structures_identity(), "the building library's digest");
    check::is_true(identity.castles_sha256 != identity.castle_buildings_sha256, "two registries, two digests");
    // The gate compares the whole identity, so one differing digest is a different world.
    CastlePlannerIdentity edited = identity;
    edited.castle_buildings_sha256.back() = edited.castle_buildings_sha256.back() == '0' ? '1' : '0';
    check::is_true(!(edited == identity), "a changed building digest to be a different identity");
}

TEST(the_castle_registry_is_carried_whole) {
    check::equal(castle_registry_revision(), 1, "the registry revision");
    check::equal(castle_modules().size(), std::size_t{9}, "every module");
    // Order is the draw contract, so the head of the list is pinned, not just its length.
    const auto kits = castle_kits();
    check::equal(kits.size(), std::size_t{3}, "every kit");
    const std::array<std::string_view, 3> ids = {"kit.simple_enclosure", "kit.motte_bailey", "kit.concentric"};
    const std::array<int, 3> weights = {4, 3, 2};
    const std::array<double, 3> halves = {72.0, 88.0, 96.0};
    for (std::size_t i = 0; i < kits.size(); ++i) {
        check::equal(std::string(kits[i].id), std::string(ids[i]), "kit " + std::to_string(i));
        check::equal(kits[i].weight, weights[i], std::string(kits[i].id) + " weight");
        check::equal(kits[i].half_m, halves[i], std::string(kits[i].id) + " extent");
        check::is_true(!kits[i].rings.empty() && !kits[i].services.empty(), std::string(kits[i].id) + " to build");
    }
    check::equal(std::string(kits[1].rings[0].role), std::string("bailey"), "the motte's PRIMARY ring is the bailey");
    check::equal(kits[1].rings[1].scale, 0.38, "and the motte ring's scale is carried");

    const CastleJoinRules& rules = castle_join_rules();
    check::equal(rules.segment_depth_m, 10.0, "the curtain run");
    check::equal(rules.min_turn_degrees_for_corner, 25.0, "the corner threshold");
    check::is_true(rules.walkway_required, "walkway continuity to be required");
    // Dead data, carried because it is inside the digest: nothing reads any of these.
    check::equal(rules.max_turn_degrees, 135.0, "the unread turn limit");
    check::is_true(castle_module("interval_tower") != nullptr, "the unread interval tower");
    check::equal(kits[0].landmarks[0].offset_scale, 0.0, "the unread landmark offset");

    // Every name the planner can reach resolves. The codegen refuses a registry where one
    // does not, and this is that refusal's floor.
    std::size_t resolved = 0;
    for (const CastleModule& module : castle_modules()) {
        check::is_true(castle_structure(module.structure_id) != nullptr,
                       std::string(module.key) + " to stand for a castle structure");
        ++resolved;
    }
    for (const CastleKit& kit : kits) {
        for (const CastleRingSpec& ring : kit.rings) {
            check::is_true(castle_module(ring.gate) != nullptr, std::string(kit.id) + " gate module");
        }
        for (const CastleServiceSpec& service : kit.services) {
            check::is_true(castle_structure(service.structure_id) != nullptr,
                           std::string(service.structure_id) + " to be a castle structure");
            ++resolved;
        }
    }
    check::is_true(resolved >= 26, "the names to have been walked, got " + std::to_string(resolved));
}

TEST(a_castle_names_its_buildings_from_the_castle_library_alone) {
    const Structure* keep = castle_structure("building.keep_tower");
    check::is_true(keep != nullptr, "the keep to be a castle structure");
    check::equal(keep->workers, 8, "the keep's garrison");
    // A city's well is a real structure, and a castle still may not build it: the
    // reference resolves castle rows against the castle library only.
    check::is_true(structure("building.well") != nullptr, "the common library to have a well");
    check::is_true(castle_structure("building.well") == nullptr, "and a castle not to be able to name it");
}

// --- the kit draw -----------------------------------------------------------

TEST(the_kit_walk_takes_the_first_kit_whose_running_weight_exceeds_the_draw) {
    const std::array<CastleKit, 3> kits = {CastleKit{"a", "A", 1, 72.0, {}, {}, {}, {}},
                                          CastleKit{"b", "B", 1, 72.0, {}, {}, {}, {}},
                                          CastleKit{"c", "C", 2, 72.0, {}, {}, {}, {}}};
    // Total 4, so these units land on exact running sums: 0.25 * 4 == 1.0, the end of
    // kit a's share, belongs to kit b.
    check::equal(castle_kit_index(kits, 0.0), std::size_t{0}, "the bottom of the range");
    check::equal(castle_kit_index(kits, 0.2499), std::size_t{0}, "just under the first boundary");
    check::equal(castle_kit_index(kits, 0.25), std::size_t{1}, "ON the boundary, the next kit");
    check::equal(castle_kit_index(kits, 0.5), std::size_t{2}, "on the second boundary, the next kit again");
    check::equal(castle_kit_index(kits, 0.999), std::size_t{2}, "the top of the range");

    // THE ORDER CONTRACT, at EQUAL weight: swapping a and b hands the same draw the other
    // kit. No count of outcomes could see this, because both kits keep their share.
    const std::array<CastleKit, 3> swapped = {kits[1], kits[0], kits[2]};
    check::equal(std::string(swapped[castle_kit_index(swapped, 0.1)].id), std::string("b"),
                 "a swap of equal weights to move the draw");
    check::equal(std::string(kits[castle_kit_index(kits, 0.1)].id), std::string("a"), "from where it was");

    const std::array<CastleKit, 2> zero = {CastleKit{"a", "A", 1, 72.0, {}, {}, {}, {}},
                                           CastleKit{"z", "Z", 0, 72.0, {}, {}, {}, {}}};
    check::throws([&] { castle_kit_index(zero, 0.5); }, "a kit that could never be drawn to be refused");
}

TEST(a_fortress_draws_its_kit_from_a_pinned_stream) {
    // GOLDEN. These pin the list order and the derivation together. node-15 alone changes
    // answer under every one of the five other orderings of the three shipped kits, and
    // node-7 and node-0 sit either side of the first boundary (draws of 3.996 and 4.099
    // out of 9), so a changed total weight, a changed key or a changed walk moves at
    // least one of them. Nothing distributional can see a reordering; this can.
    const std::map<std::string, std::string> golden = {
        {"fortress-node-0", "kit.motte_bailey"},     {"fortress-node-2", "kit.concentric"},
        {"fortress-node-3", "kit.simple_enclosure"}, {"fortress-node-5", "kit.motte_bailey"},
        {"fortress-node-7", "kit.simple_enclosure"}, {"fortress-node-8", "kit.simple_enclosure"},
        {"fortress-node-13", "kit.simple_enclosure"}, {"fortress-node-15", "kit.motte_bailey"},
        {"fortress-node-16", "kit.concentric"},      {"fortress-node-29", "kit.simple_enclosure"},
        {"fortress-node-39", "kit.motte_bailey"},    {"fortress-node-46", "kit.concentric"},
    };
    std::set<std::string> drawn;
    for (const auto& [key, kit] : golden) {
        const std::string first(select_castle_kit(42, key).id);
        check::equal(first, kit, key + " under seed 42");
        check::equal(std::string(select_castle_kit(42, key).id), first, key + " to draw the same kit again");
        drawn.insert(first);
    }
    check::equal(drawn.size(), std::size_t{3}, "the golden cases to reach every kit");
    // The seed is part of the stream: the same fortress on another world is another draw.
    std::size_t moved = 0;
    for (const auto& [key, kit] : golden) moved += std::string(select_castle_kit(43, key).id) != kit ? 1 : 0;
    check::is_true(moved > 0, "another world seed to redraw some kits");
}

TEST(the_kit_draw_follows_the_published_weights) {
    // A frequency count cannot see a reordering -- the golden cases are for that -- but it
    // does see a weight that is read wrong or a walk that favours one end.
    std::map<std::string, int> counts;
    const int draws = 9000;
    for (int node = 0; node < draws; ++node) {
        ++counts[std::string(select_castle_kit(7, "fortress-node-" + std::to_string(node)).id)];
    }
    check::close(counts["kit.simple_enclosure"] / static_cast<double>(draws), 4.0 / 9.0, 0.02, "the 4/9 share");
    check::close(counts["kit.motte_bailey"] / static_cast<double>(draws), 3.0 / 9.0, 0.02, "the 3/9 share");
    check::close(counts["kit.concentric"] / static_cast<double>(draws), 2.0 / 9.0, 0.02, "the 2/9 share");
}

TEST(a_fortress_keeps_its_kit_when_its_ordinal_is_renumbered) {
    // The ordinal renumbers every time the fortress list is rebuilt, which every age
    // advance does. The kit is drawn on the uid, which names ground that does not move, so
    // a fort that stayed put keeps its castle.
    const Fortified f = fortified();
    const CastleRequest request = f.request();
    check::is_true(!f.world.humans.fortresses.empty(), "fortresses, or this proves nothing");
    const RuralSite& original = f.world.humans.fortresses.front();

    RuralSite renumbered = original;
    renumbered.id = "fortress-999";
    const CastlePlan before = plan_castle(request, original);
    const CastlePlan after = plan_castle(request, renumbered);
    check::equal(std::string(after.kit_id), std::string(before.kit_id), "the kit to survive a renumbering");
    check::equal(after.fortress_id, std::string("fortress-999"), "while the plan still joins on the new ordinal");
    check::equal(after.fortress_uid, original.uid, "and carries the stable key beside it");
    check::equal(after.node, original.node, "and the node");

    // The other direction, so this is not merely asserting a constant: the same ordinal on
    // different ground draws that ground's kit.
    RuralSite moved = original;
    std::size_t differing = 0;
    for (int node = 0; node < 64 && differing == 0; ++node) {
        moved.uid = "fortress-node-" + std::to_string(node);
        differing += select_castle_kit(f.world.config.seed, moved.uid).id != before.kit_id ? 1 : 0;
    }
    check::is_true(differing > 0, "some uid to draw another kit");
    check::is_true(plan_castle(request, moved).kit_id != before.kit_id, "the kit to follow the uid");

    RuralSite anonymous = original;
    anonymous.uid.clear();
    check::throws([&] { plan_castle(request, anonymous); }, "a fortress with no stable key to be refused");
}

// --- wall-ring geometry -----------------------------------------------------

TEST(the_castle_turn_is_the_interior_angle_not_the_heading_change) {
    // The reference's docstring says "exterior turn". The code measures the angle AT the
    // vertex between its two edges, and the tower rule is built on that number.
    check::close(std::abs(castle_turn_degrees({0, 0}, {1, 0}, {2, 0})), 180.0, 1e-9, "a straight run to read 180");
    check::close(path_turn_degrees({0, 0}, {1, 0}, {2, 0}), 0.0, 1e-9, "where the heading change reads 0");
    check::close(std::abs(castle_turn_degrees({0, 0}, {1, 0}, {1, 1})), 90.0, 1e-9, "a right angle to read 90");

    const std::vector<Point> ring = polygon(32, 50.0);
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Point& a = ring[(i + ring.size() - 1) % ring.size()];
        const Point& c = ring[(i + 1) % ring.size()];
        check::close(std::abs(castle_turn_degrees(a, ring[i], c)), 168.75, 1e-9,
                     "a regular 32-gon's interior angle at " + std::to_string(i));
        check::close(std::abs(path_turn_degrees(a, ring[i], c)), 11.25, 1e-9, "against its heading change");
    }
    // THE CONSEQUENCE: at a 25 degree threshold every vertex of a convex ring is a corner,
    // so where towers go is decided by the tower budget and nothing else.
    check::equal(castle_corner_indices(ring, 25.0).size(), std::size_t{32}, "all 32 vertices to be corners");

    // The threshold is not inert, it is inverted: what it excludes is a needle.
    std::vector<Point> spiked = ring;
    spiked[8] = {spiked[8].x * 6.0, spiked[8].z * 6.0};
    const std::vector<std::size_t> corners = castle_corner_indices(spiked, 25.0);
    check::equal(corners.size(), std::size_t{31}, "one vertex to be excluded");
    check::is_true(std::find(corners.begin(), corners.end(), std::size_t{8}) == corners.end(),
                   "and it is the needle, the sharpest point on the ring");
}

TEST(a_join_is_proved_by_its_ends_its_dimensions_and_its_walkway) {
    const CastleJoinRules& rules = castle_join_rules();
    const CastleModule* curtain = castle_module("curtain_segment");
    const CastleModule* corner = castle_module("corner_join");
    const CastleModule* gatehouse = castle_module("gatehouse");
    const CastleModule* drawbridge = castle_module("drawbridge");
    const CastleModule* keep = castle_module("keep_tower");

    check::is_true(castle_can_join(curtain, corner, rules, std::nullopt, std::nullopt, 8.0, 8.0).ok,
                   "a curtain to join a corner tower at wall height");
    check::is_true(castle_can_join(curtain, gatehouse, rules, std::nullopt, std::nullopt, 8.0, 8.0).ok,
                   "and a gatehouse");

    const CastleJoinCheck thick = castle_can_join(curtain, corner, rules, 3.0, 5.0, 8.0, 14.0);
    check::equal(thick.reason, std::string("Thickness delta 2.000 exceeds 0.5"), "a thickness mismatch");
    const CastleJoinCheck tall = castle_can_join(curtain, curtain, rules, 3.0, 3.0, 8.0, 12.0);
    check::equal(tall.reason, std::string("Height delta 4.000 exceeds 1.0"), "a height mismatch");
    // Defaults are the modules' own nominals: a corner tower stands 14 m against an 8 m
    // curtain, which is why the planner judges joins at the curtain's height instead.
    check::is_true(!castle_can_join(curtain, corner, rules).ok, "a tower judged at its own height to fail");

    check::equal(castle_can_join(curtain, keep, rules).reason, std::string("Module has no join end"),
                 "the keep to join nothing");
    check::equal(castle_can_join(curtain, nullptr, rules).reason, std::string("Missing module"),
                 "a module that does not exist to join nothing");
    check::equal(castle_can_join(drawbridge, corner, rules, 3.0, 3.0, 8.0, 8.0).reason,
                 std::string("Incompatible ends gate_join/corner_join"), "two ends neither accepts");
    // The drawbridge is the one joinable module without a walkway.
    check::equal(castle_can_join(curtain, drawbridge, rules, 3.0, 3.0, 8.0, 8.0).reason,
                 std::string("Walkway continuity broken"), "a join that breaks the wall walk");
}

TEST(a_ring_is_cut_into_curtain_runs_that_restart_at_every_vertex) {
    const std::vector<Point> ring = castle_ellipse_ring(40.0, 30.0, 24);
    check::equal(ring.size(), std::size_t{24}, "an open ring of 24 samples");
    check::equal(ring[0].x, 40.0, "starting on +x");
    check::equal(ring[0].z, 0.0, "on the axis");

    const auto [segments, nodes] = castle_segmentize(ring, 10.0, "building.curtain_segment", "ring-0", 3.0, 8.0);
    check::equal(nodes.size(), ring.size(), "one node per vertex, never filtered");
    check::is_true(segments.size() >= 6, "a real curtain, got " + std::to_string(segments.size()));

    double perimeter = 0.0;
    for (std::size_t i = 0; i < ring.size(); ++i) perimeter += distance(ring[i], ring[(i + 1) % ring.size()]);
    double total = 0.0;
    bool ragged = false;
    std::set<std::pair<double, double>> vertices;
    for (const Point& p : ring) vertices.insert({p.x, p.z});
    for (std::size_t k = 0; k < segments.size(); ++k) {
        const CastleSegment& seg = segments[k];
        check::equal(seg.id, "ring-0-seg-" + std::to_string(k), "ids contiguous before any gate filter");
        check::between(seg.length_m, 0.0, 10.01, seg.id + " to be at most one module long");
        ragged = ragged || seg.length_m < 9.0;
        total += seg.length_m;
        // EVERY RUN STARTS AT A VERTEX OR AT THE PREVIOUS CUT. That is the reference's
        // rule and the reason lengths are ragged: the first run on an edge begins at the
        // edge's vertex, not at the point a full module back along the ring.
        const bool at_vertex = vertices.count({seg.from_m.x, seg.from_m.z}) == 1;
        const bool at_cut = k > 0 && seg.from_m.x == segments[k - 1].to_m.x && seg.from_m.z == segments[k - 1].to_m.z;
        check::is_true(at_vertex || at_cut, seg.id + " to start at a vertex or where the last run ended");
        // Half a unit in the fourth decimal: the recorded rotation is rounded there.
        check::close(seg.rotation_degrees,
                     std::atan2(seg.to_m.z - seg.from_m.z, seg.to_m.x - seg.from_m.x) * 180.0 / kPi - 90.0, 6e-5,
                     seg.id + " to be its heading less a right angle");
        check::equal(seg.thickness_m, 3.0, "the curtain thickness carried");
        check::is_true(seg.walkway, "a wall walk on every run");
    }
    // A run never bends round a vertex: both of its ends lie on ONE edge of the ring. That is
    // the rule that makes the runs ragged and leaves the stretch before each vertex without
    // curtain; a run carried round the corner would cut across it instead.
    const auto on_edge = [&](Point p, std::size_t e) {
        const Point a = ring[e];
        const Point b = ring[(e + 1) % ring.size()];
        const double dx = b.x - a.x;
        const double dz = b.z - a.z;
        const double t = std::clamp(((p.x - a.x) * dx + (p.z - a.z) * dz) / (dx * dx + dz * dz), 0.0, 1.0);
        return std::hypot(p.x - a.x - t * dx, p.z - a.z - t * dz) < 0.02;
    };
    for (const CastleSegment& seg : segments) {
        bool one_edge = false;
        for (std::size_t e = 0; e < ring.size() && !one_edge; ++e) {
            one_edge = on_edge(seg.from_m, e) && on_edge(seg.to_m, e);
        }
        check::is_true(one_edge, seg.id + " to lie along a single edge of the ring");
    }
    check::is_true(ragged, "runs to come out ragged, which is the reference's cut, kept");
    // Each chord lies along one edge, so together they cannot outrun the ring beyond the
    // centimetre rounding of their ends. They cover much less than all of it: what was
    // carried round a vertex is never walled by a chord.
    check::is_true(total <= perimeter + 0.1, "the chords never to outrun the ring");
    check::is_true(total > perimeter * 0.25, "and to wall a real share of it, got " + std::to_string(total) +
                                                 " of " + std::to_string(perimeter));
}

TEST(clipping_takes_the_outermost_buildable_cell_on_each_ray) {
    // A disc wide enough that neighbouring rays snap within one 4 m step of each other:
    // 76 against 72 is inside the smoother's 1.12 radial cap, so every point below is
    // where the RAY put it and not where smoothing moved it.
    const int size = 50;
    const double half = size * 4.0 / 2.0;
    CellSet valid(size);
    for (int j = 0; j < size; ++j) {
        for (int i = 0; i < size; ++i) {
            const double x = -half + (i + 0.5) * 4.0;
            const double z = -half + (j + 0.5) * 4.0;
            if (std::hypot(x, z) < 78.0) valid.insert({i, j});
        }
    }
    const std::vector<double> flat(static_cast<std::size_t>(size) * size, 2.0);
    const std::vector<Point> raw = castle_ellipse_ring(120.0, 100.0, 32);
    const std::vector<Point> clipped = castle_clip_ring(raw, valid, half, 4, 25.0, flat);
    check::equal(clipped.size(), raw.size(), "every ray to find ground on a disc");
    const auto cell_of = [&](double x, double z) {
        return Cell{static_cast<int>(std::floor((x + half) / 4.0)), static_cast<int>(std::floor((z + half) / 4.0))};
    };
    for (std::size_t k = 0; k < clipped.size(); ++k) {
        const Point p = clipped[k];
        check::is_true(valid.contains(cell_of(p.x, p.z)), "point " + std::to_string(k) + " on buildable ground");
        // Outermost: one more step out along the same ray is not buildable.
        const double r = std::hypot(p.x, p.z);
        check::is_true(!valid.contains(cell_of(p.x / r * (r + 4.0), p.z / r * (r + 4.0))),
                       "point " + std::to_string(k) + " to be the outermost buildable step");
    }

    // ONLY THE DIRECTION IS USED, so a ring a quarter the size clips to the same place.
    // This is why every ring of a kit lands on one boundary; see the planning tests.
    const std::vector<Point> small = castle_clip_ring(castle_ellipse_ring(30.0, 25.0, 32), valid, half, 4, 25.0, flat);
    check::equal(small.size(), clipped.size(), "the same number of points");
    for (std::size_t k = 0; k < small.size(); ++k) {
        check::is_true(distance(small[k], clipped[k]) < 0.1, "point " + std::to_string(k) + " to ignore the scale");
    }

    // A ray over no ground contributes nothing: the ring shortens rather than inventing.
    CellSet half_disc = valid;
    for (Cell c : valid.sorted()) {
        if (-half + (c.first + 0.5) * 4.0 > 2.0) half_disc.erase(c);
    }
    const std::vector<Point> shorter = castle_clip_ring(raw, half_disc, half, 4, 25.0, flat);
    check::is_true(shorter.size() < raw.size() && shorter.size() >= 8,
                   "a ring over half a disc to lose the rays over nothing, got " + std::to_string(shorter.size()));

    // A steep cell is passed over and the ray keeps walking inward.
    std::vector<double> steep = flat;
    const Cell edge = cell_of(clipped[0].x, clipped[0].z);
    steep[static_cast<std::size_t>(edge.second) * size + static_cast<std::size_t>(edge.first)] = 40.0;
    const std::vector<Point> skipped = castle_clip_ring(raw, valid, half, 4, 25.0, steep);
    check::equal(skipped.size(), clipped.size(), "a steep cell not to cost the ray its point");
    check::is_true(std::hypot(skipped[0].x, skipped[0].z) < std::hypot(clipped[0].x, clipped[0].z),
                   "a steep outermost cell to push the ray inward");
}

TEST(a_wall_opens_a_gate_on_the_road_and_a_postern_opposite) {
    const std::vector<Point> ring = castle_ellipse_ring(48.0, 40.0, 32);
    const CastleWallNetwork wall = build_castle_wall(wall_request(ring, Point{48.0, 0.0}));
    check::equal(std::string(wall.status), std::string("closed"), "the ring to close");
    check::is_true(wall.walkway_continuous, "with a continuous wall walk");
    check::equal(wall.gate_count, 2, "a gate and a postern");
    check::equal(wall.segment_count, static_cast<int>(wall.segments.size()), "the stated segment count");

    const CastleJoin& gate = wall.gates[0];
    check::equal(std::string(gate.structure_id), std::string("building.barbican"), "the gatehouse is a barbican");
    check::equal(std::string(gate.kind), std::string("gate"), "and says it is the gate");
    check::is_true(distance(gate.position_m, {48.0, 0.0}) < 12.0, "the gate to face the road");
    check::is_true(gate.approach_m.has_value() && gate.approach_m->x == 48.0, "and to remember where it came from");
    const CastleJoin& postern = wall.gates[1];
    check::equal(std::string(postern.kind), std::string("postern"), "the second opening to be the postern");
    check::equal(postern.position_m.x, ring[16].x, "opposite the gate");
    check::equal(postern.position_m.z, ring[16].z, "exactly half the ring round");

    // The openings are real: no curtain run is left standing across either.
    std::set<std::string> kept;
    for (const CastleSegment& seg : wall.segments) {
        kept.insert(seg.id);
        for (const CastleJoin& opening : wall.gates) {
            check::is_true(distance(seg.center_m, opening.position_m) >= 8.0, seg.id + " not to block " + opening.id);
        }
    }
    const auto [all, nodes] = castle_segmentize(ring, 10.0, "building.curtain_segment", "ring-outer", 3.0, 8.0);
    check::is_true(kept.size() < all.size(), "curtain to have been cut away at the openings");
    check::equal(nodes.size(), wall.nodes.size(), "every vertex kept as a node");

    // Two stairs, a half-ring apart along the SURVIVING curtain.
    check::equal(wall.stairs.size(), std::size_t{2}, "the stair budget");
    check::equal(wall.stairs[0].position_m.x, wall.segments[0].center_m.x, "the first stair on the first run");
    check::equal(wall.stairs[1].position_m.x, wall.segments[wall.segments.size() / 2].center_m.x,
                 "the second a half-ring on");
    check::is_true(!wall.ditch, "no ditch unless the ring asks for one");
}

TEST(the_tower_budget_counts_candidates_not_towers) {
    // Every vertex is a corner (see the interior-angle test), so the budget takes the first
    // four vertices from +x. With the road on the far side the POSTERN lands on vertex 0:
    // that candidate is skipped for standing beside an opening and still spends its slot,
    // so four budgeted towers are three, numbered from one. The ring is castle-sized, so
    // vertex 1 is clear of the postern's 10 m.
    const std::vector<Point> ring = castle_ellipse_ring(96.0, 80.0, 32);
    const CastleWallNetwork far = build_castle_wall(wall_request(ring, Point{-96.0, 0.0}));
    std::vector<std::string> ids;
    for (const CastleJoin& tower : far.towers) ids.push_back(tower.id);
    check::is_true(ids == std::vector<std::string>{"ring-outer-tower-1", "ring-outer-tower-2", "ring-outer-tower-3"},
                   "three towers with the first id skipped, got " + std::to_string(ids.size()));
    for (const CastleJoin& tower : far.towers) {
        check::is_true(tower.position_m.x > 0.0 && tower.position_m.z > 0.0,
                       tower.id + " to stand in the first quadrant: the budget bunches them");
    }
    // With the road to the north neither opening is near vertices 0..3, and the budget
    // buys all four.
    const CastleWallNetwork north = build_castle_wall(wall_request(ring, Point{0.0, 80.0}));
    check::equal(north.towers.size(), std::size_t{4}, "four towers when no opening stands in the way");
    check::equal(north.towers[0].id, std::string("ring-outer-tower-0"), "numbered from zero");
    check::equal(north.towers[0].height_m, 14.0, "a tower rises above the 8 m wall");
    check::equal(north.towers[0].curtain_height_m, 8.0, "and joins it at wall height");
}

TEST(a_gate_that_breaks_the_wall_walk_leaves_the_ring_broken_and_says_where) {
    const std::vector<Point> ring = castle_ellipse_ring(48.0, 40.0, 32);
    const CastleWallNetwork broken = build_castle_wall(wall_request(ring, Point{48.0, 0.0}, ring_spec("drawbridge")));
    check::equal(std::string(broken.status), std::string("broken"), "a drawbridge gate cannot carry the walk");
    check::is_true(!broken.walkway_continuous, "and the walk is not continuous");
    check::equal(broken.segment_count, 0, "a broken ring is not counted as a wall");
    check::is_true(!broken.segments.empty(), "but it still carries its curtain");
    check::equal(broken.unplaced.size(), std::size_t{1}, "exactly the one join that failed");
    check::equal(broken.unplaced[0].building_id, std::string("ring-outer-gate-0"), "named by the node that failed");
    check::equal(broken.unplaced[0].reason, std::string("Walkway continuity broken"), "and why");

    // The floor: the same ring with a gatehouse closes, so the break is the drawbridge.
    check::equal(std::string(build_castle_wall(wall_request(ring, Point{48.0, 0.0})).status), std::string("closed"),
                 "the same ring with a gatehouse to close");
}

TEST(a_ring_that_cannot_close_says_why) {
    const CastleWallNetwork stub = build_castle_wall(wall_request(polygon(7, 60.0), std::nullopt));
    check::equal(std::string(stub.status), std::string("open"), "seven points not to be a ring");
    check::equal(stub.unplaced.size(), std::size_t{1}, "one stated failure");
    check::equal(stub.unplaced[0].reason, std::string("Perimeter could not close"), "the reason");
    check::is_true(stub.segments.empty() && stub.gates.empty(), "and nothing built");

    // Thirty-two points on a 6 m circle is a ring too small to cut six curtain runs from.
    const CastleWallNetwork tiny = build_castle_wall(wall_request(polygon(32, 6.0), std::nullopt));
    check::equal(std::string(tiny.status), std::string("open"), "a ring too small to wall");
    check::equal(tiny.unplaced.size(), std::size_t{1}, "one stated failure");
    check::equal(tiny.unplaced[0].reason, std::string("Insufficient continuous wall segments"), "the reason");
    check::equal(tiny.unplaced[0].building_id, std::string("building.curtain_segment"), "naming the curtain");

    // A registry missing a module the ring needs is refused, not built as a wall of
    // nothing.
    CastleWallRequest bare = wall_request(polygon(32, 60.0), std::nullopt);
    const std::array<CastleModule, 1> only = {*castle_module("curtain_segment")};
    bare.modules = only;
    check::throws([&] { build_castle_wall(bare); }, "a ring with no gatehouse module to be refused");

    // The floor.
    check::equal(std::string(build_castle_wall(wall_request(polygon(32, 60.0), std::nullopt)).status),
                 std::string("closed"), "a real ring to close");
}

TEST(a_gate_opens_nearest_the_road_or_else_at_the_eastern_extremity) {
    const std::vector<Point> ring = {{10, 5}, {12, -3}, {12, 3}, {0, 8}, {-12, 0}, {0, -8}};
    // No road: the greatest x, ties to the point nearest the z = 0 line, first on an
    // exact tie.
    check::equal(*castle_gate_index(ring, std::nullopt), std::size_t{1}, "the +x extremity, first of a tie");
    const std::vector<Point> nearer = {{10, 5}, {12, -3}, {12, 1}, {0, 8}, {-12, 0}, {0, -8}};
    check::equal(*castle_gate_index(nearer, std::nullopt), std::size_t{2}, "the one nearer the axis");
    check::equal(*castle_gate_index(ring, Point{-20.0, 1.0}), std::size_t{4}, "the vertex nearest the road");
    check::is_true(!castle_gate_index({}, std::nullopt).has_value(), "no gate on no ring");
    check::equal(castle_opposite_index(ring, 1), std::size_t{4}, "the postern half a ring on");
    check::equal(castle_opposite_index(ring, 5), std::size_t{2}, "wrapping");
}

// --- planning a castle ------------------------------------------------------

TEST(a_fortress_is_planned_as_walls_gates_towers_a_keep_and_a_garrison) {
    const Fortified f = fortified();
    const CastleRequest request = f.request();
    check::is_true(!f.world.humans.fortresses.empty(), "fortresses, or this proves nothing");
    const RuralSite& fortress = f.world.humans.fortresses.front();
    const CastlePlan plan = plan_castle(request, fortress);
    const CastleKit& kit = select_castle_kit(f.world.config.seed, fortress.uid);

    check::equal(std::string(plan.kit_id), std::string(kit.id), "the drawn kit");
    check::equal(plan.fortress_id, fortress.id, "the join key");
    check::equal(plan.core_id, fortress.core_id, "the parent city's position");
    check::equal(plan.half_m, kit.half_m, "a fort with no near neighbour to keep its kit's extent");
    check::equal(plan.terrain.size, static_cast<int>(2.0 * plan.half_m / 4.0), "a raster of 4 m cells");
    check::equal(plan.terrain.surface_heights_m.size(),
                 static_cast<std::size_t>(plan.terrain.size + 1) * static_cast<std::size_t>(plan.terrain.size + 1),
                 "corner-sampled elevations");
    check::is_true(plan.status == "complete" || plan.status == "partial",
                   "a buildable fort to report a buildable status, got " + std::string(plan.status));
    check::is_true(plan.stats.wall_rings_closed >= 1, "a wall to have closed");

    for (std::string_view kind : {"court", "landmark", "gate", "tower", "stair", "service"}) {
        check::is_true(count_kind(plan, kind) > 0, "a " + std::string(kind) + " plot");
    }
    bool keep = false;
    for (const CastlePlot& p : plan.plots) keep = keep || p.building_id == "building.keep_tower";
    check::is_true(keep, "a keep");

    // Plot ids are positional, and the stats are a summary of the plots rather than a
    // second opinion about them.
    int workers = 0;
    for (std::size_t i = 0; i < plan.plots.size(); ++i) {
        check::equal(plan.plots[i].id, "plot-" + std::to_string(i), "plot ids to be positional");
        workers += plan.plots[i].workers;
    }
    check::is_true(workers > 0, "a garrison to hold it");
    check::equal(plan.stats.workers, workers, "the stated garrison");
    check::equal(plan.stats.plots, static_cast<int>(plan.plots.size()), "the stated plot count");
    check::equal(std::string(plan.stats.kit_id), std::string(kit.id), "the stated kit");

    // THE PERIMETER PASS COUNTS TWO THINGS: every closed ring, then every gate, tower and
    // stair plot, because both write to the same pass. The reference's number, kept.
    const std::size_t perimeter_plots = count_kind(plan, "gate") + count_kind(plan, "tower") + count_kind(plan, "stair");
    check::equal(plan.passes.size(), std::size_t{6}, "six passes");
    check::equal(plan.passes[0].placed, 1LL, "the map pass is a literal one");
    check::equal(plan.passes[1].placed, 1LL, "and so is the kit pass");
    check::equal(plan.passes[2].placed, static_cast<long long>(plan.stats.wall_rings_closed + perimeter_plots),
                 "closed rings plus perimeter plots");
    check::equal(plan.passes[3].placed, static_cast<long long>(count_kind(plan, "court")), "the court pass");
    check::equal(plan.passes[4].placed, static_cast<long long>(count_kind(plan, "landmark")), "the landmark pass");
    check::equal(plan.passes[5].placed, static_cast<long long>(count_kind(plan, "service")), "the service pass");

    // One support road, projected to where it leaves the window, and never connected: a
    // castle has no street tree to meet it.
    check::equal(plan.road_connections.size(), std::size_t{1}, "the fortress's own road");
    const RoadConnection& road = plan.road_connections.front();
    check::equal(road.route_id, "support:" + fortress.id, "named for its fortress");
    check::equal(std::string(road.status), std::string("unreachable"), "never promoted");
    check::is_true(!road.has_cell && road.local_path_m.empty(), "with no cell and no local path");
    check::close(std::max(std::abs(road.gate_local_m.x), std::abs(road.gate_local_m.z)), plan.half_m, 1e-6,
                 "the crossing to lie on the window's edge");
    check::close(std::hypot(road.direction.x, road.direction.y, road.direction.z), 1.0, 1e-12, "a unit direction");

    const CastleWallNetwork& primary = primary_ring(plan);
    check::is_true(main_gate(primary).approach_m.has_value(), "the primary gate to know its road");
    check::equal(main_gate(primary).approach_m->x, road.gate_local_m.x, "and it is this road");
    const std::size_t nearest = *castle_gate_index(primary.polyline_m, road.gate_local_m);
    check::equal(main_gate(primary).position_m.x, primary.polyline_m[nearest].x, "opened nearest the road");

    // A plan is a function of its inputs and nothing else.
    const CastlePlan again = plan_castle(request, fortress);
    check::equal(again.plots.size(), plan.plots.size(), "the same plots on a replay");
    for (std::size_t i = 0; i < plan.plots.size(); ++i) {
        check::is_true(again.plots[i].x_m == plan.plots[i].x_m && again.plots[i].z_m == plan.plots[i].z_m &&
                           again.plots[i].building_id == plan.plots[i].building_id,
                       plan.plots[i].id + " to replay exactly");
    }
}

TEST(every_fortress_is_planned_in_id_string_order) {
    const Fortified f = fortified();
    const CastleRequest request = f.request();
    // ELEVEN IS THE FLOOR THAT MATTERS: below fortress-10 the string and numeric orders
    // agree and this test could not tell them apart.
    check::is_true(f.world.humans.fortresses.size() >= 11,
                   "enough fortresses for the two orders to differ, got " +
                       std::to_string(f.world.humans.fortresses.size()));
    const CastlePlans plans = fill_castles(request);
    check::equal(plans.castles.size(), f.world.humans.fortresses.size(), "one plan per fortress");
    for (std::size_t i = 1; i < plans.castles.size(); ++i) {
        check::is_true(plans.castles[i - 1].fortress_id < plans.castles[i].fortress_id, "plans in id order");
    }
    const auto at = [&](const std::string& id) {
        for (std::size_t i = 0; i < plans.castles.size(); ++i) {
            if (plans.castles[i].fortress_id == id) return i;
        }
        return plans.castles.size();
    };
    check::is_true(at("fortress-10") < at("fortress-2"), "fortress-10 before fortress-2: string order, not numeric");

    check::is_true(plans.identity == castle_planner_identity(), "the block to publish the identity it was built under");
    check::equal(plans.phase_order.size(), std::size_t{6}, "the phase order");
    check::is_true(!plans.beds.empty() && !plans.limits.empty(), "and to say what it does not model");

    std::set<std::string> kits;
    std::size_t walled = 0;
    for (const CastlePlan& plan : plans.castles) {
        kits.insert(std::string(plan.kit_id));
        walled += plan.stats.wall_rings_closed > 0 ? 1 : 0;
    }
    check::is_true(walled * 10 >= plans.castles.size() * 9,
                   "nearly every fortress on a shipped world to be walled, got " + std::to_string(walled) + " of " +
                       std::to_string(plans.castles.size()));
    check::equal(kits.size(), std::size_t{3}, "every kit somewhere in a world");
}

TEST(only_the_primary_ring_is_shown_the_road) {
    const Fortified f = fortified();
    const RuralSite* fortress = fortress_drawing(f, "kit.concentric");
    check::is_true(fortress != nullptr, "a concentric castle in the world");
    const CastlePlan plan = plan_castle(f.request(), *fortress);
    check::equal(plan.wall_networks.size(), std::size_t{2}, "two rings");
    const CastleWallNetwork& outer = plan.wall_networks[0];
    const CastleWallNetwork& inner = plan.wall_networks[1];
    check::equal(std::string(inner.status), std::string("closed"), "the inner ring to close");
    check::equal(main_gate(outer).approach_m.value_or(Point{}).x, support_road(plan).gate_local_m.x,
                 "the outer gate to face the road");
    // The inner ring is never told where the road is, so its gate stands at the +x
    // extremity whatever direction the road comes from.
    const std::size_t east = *castle_gate_index(inner.polyline_m, std::nullopt);
    check::equal(main_gate(inner).position_m.x, inner.polyline_m[east].x, "the inner gate at the +x extremity");
    check::equal(main_gate(inner).approach_m.value_or(Point{-1.0, 0.0}).x, main_gate(inner).position_m.x,
                 "approached from its own position");
    check::equal(inner.gates.size(), std::size_t{1}, "and the inner ring has no postern");
}

TEST(every_ring_of_a_kit_collapses_onto_one_boundary) {
    // PINNED DEFECT. A ring's scale shrinks the ellipse, and clipping keeps only each
    // point's direction, so the motte (0.38) lands on the bailey's line. The two courts
    // and the keep then share one centre. Concentric wards were intended and are not what
    // the reference builds; fixing it changes every multi-ring castle in every world.
    const Fortified f = fortified();
    const RuralSite* fortress = fortress_drawing(f, "kit.motte_bailey");
    check::is_true(fortress != nullptr, "a motte-and-bailey castle in the world");
    const CastlePlan plan = plan_castle(f.request(), *fortress);
    const CastleWallNetwork* bailey = ring_named(plan, "ring-bailey");
    const CastleWallNetwork* motte = ring_named(plan, "ring-motte");
    check::is_true(bailey != nullptr && motte != nullptr, "both rings");
    check::is_true(bailey->clipped_to_terrain && motte->clipped_to_terrain, "both clipped to the ground");
    check::equal(motte->polyline_m.size(), bailey->polyline_m.size(), "the same number of points");
    double worst = 0.0;
    for (std::size_t k = 0; k < motte->polyline_m.size(); ++k) {
        worst = std::max(worst, distance(motte->polyline_m[k], bailey->polyline_m[k]));
    }
    check::is_true(worst < 0.1, "the motte to sit on the bailey's line, worst gap " + std::to_string(worst));

    std::vector<Point> centres;
    for (const CastlePlot& p : plan.plots) {
        if (p.kind == "court" || p.kind == "landmark") centres.push_back({p.x_m, p.z_m});
    }
    check::equal(centres.size(), std::size_t{3}, "two courts and a keep");
    check::is_true(distance(centres[0], centres[1]) < 0.1 && distance(centres[0], centres[2]) < 0.1,
                   "all three at one centre");
}

TEST(a_castle_places_its_buildings_without_testing_the_ground) {
    // PINNED. Nothing checks a castle plot against the buildable mask, another plot or the
    // window. A drawbridge spans the ditch ten metres out from the gate, and on a ring that
    // reaches the window's edge that is outside the window altogether.
    const Fortified f = fortified();
    std::size_t outside = 0;
    std::size_t drawbridges = 0;
    std::size_t fan_steps = 0;
    std::size_t past_full_turn = 0;
    for (const RuralSite& fortress : f.world.humans.fortresses) {
        const CastlePlan plan = plan_castle(f.request(), fortress);
        const CastleWallNetwork& primary = primary_ring(plan);
        for (const CastlePlot& p : plan.plots) {
            if (p.building_id != "building.drawbridge") continue;
            ++drawbridges;
            check::close(distance({p.x_m, p.z_m}, main_gate(primary).position_m), 10.0, 0.02,
                         plan.fortress_id + " drawbridge ten metres out from the gate");
            if (std::max(std::abs(p.x_m), std::abs(p.z_m)) > plan.half_m) ++outside;
        }
        // The service fan steps 63 degrees a building and is never wrapped.
        std::vector<double> fan;
        for (const CastlePlot& p : plan.plots) {
            if (p.kind == "service" && p.building_id != "building.drawbridge") fan.push_back(p.rotation_degrees);
        }
        for (std::size_t i = 1; i < fan.size(); ++i) {
            check::close(fan[i] - fan[i - 1], 63.0, 1e-3, plan.fortress_id + " fan step");
            ++fan_steps;
            if (fan[i] > 360.0) ++past_full_turn;
        }
    }
    check::is_true(drawbridges > 0, "drawbridges to have been built at all");
    check::is_true(outside > 0, "some drawbridge to stand outside its own window");
    check::is_true(fan_steps >= 3 * f.world.humans.fortresses.size(), "every fan to have been walked");
    check::is_true(past_full_turn > 0, "some rotation to run past a full turn unwrapped");
}

TEST(a_ring_clipping_cannot_close_falls_back_to_an_ellipse_nothing_checks) {
    // PINNED. When fewer than eight rays find buildable ground the ring is the RAW ellipse,
    // and nothing tests that ellipse against the ground at all.
    //
    // Built on real planner input: a row from the pole, adjacent columns are 153 m apart, so
    // an 80 m window reads three columns of every layer. Make the fort's own column water and
    // its neighbours land, and the only buildable ground is a 4 m strip at each side --
    // enough cells for a precinct, and in reach of just six of the 32 rays.
    Fortified f = fortified();
    const int n = f.world.config.size;
    const int x = 10;
    f.world.layers["height"] = Grid(n, 100.0);
    // Flood risk silences the relief detail, so no cell is steep and only water decides.
    f.world.layers["flood_risk"] = Grid(n, 1.0);
    Grid water(n, 0.0);
    water.at(x, 1) = 1.0;
    f.world.layers["water_type"] = water;
    f.world.climate.river_segments.clear();
    f.world.water.river_segments.clear();

    const std::array<CastleRingSpec, 1> rings = {ring_spec()};
    const std::array<CastleCourtSpec, 1> courts = {CastleCourtSpec{"bailey", "outer", "building.bailey_court"}};
    const CastleKit kit{"kit.test_strips", "Strips", 1, 80.0, rings, courts, {}, {}};
    const RuralSite polar = fortress_at(f, x, 1, "fortress-polar");
    CastleRequest request = f.request();
    request.sites = {};
    request.fortresses = {};
    const CastlePlan plan = plan_castle_with_kit(request, polar, kit);

    check::equal(plan.half_m, 80.0, "the window the scenario was built for");
    check::is_true(plan.debug.valid_cells >= 24, "enough ground for a precinct, got " +
                                                    std::to_string(plan.debug.valid_cells));
    const CastleWallNetwork& ring = primary_ring(plan);
    check::is_true(!ring.clipped_to_terrain, "the clip to have collapsed and said so");
    const std::vector<Point> raw = castle_ellipse_ring(80.0 * 0.72, 80.0 * 0.62, 32);
    check::equal(ring.polyline_m.size(), raw.size(), "the raw ellipse in its place");
    for (std::size_t k = 0; k < raw.size(); ++k) {
        check::is_true(ring.polyline_m[k].x == raw[k].x && ring.polyline_m[k].z == raw[k].z,
                       "vertex " + std::to_string(k) + " to be the unclipped ellipse's");
    }
    check::equal(std::string(ring.status), std::string("closed"), "and it closes");
    std::size_t wet = 0;
    for (const Point& p : ring.polyline_m) {
        const int i = static_cast<int>(std::floor((p.x + plan.half_m) / 4.0));
        const int j = static_cast<int>(std::floor((p.z + plan.half_m) / 4.0));
        wet += plan.terrain.code(i, j) == 1 ? 1 : 0;
    }
    check::is_true(wet > 16, "most of that wall to stand in water, got " + std::to_string(wet) + " of 32");
    check::equal(count_kind(plan, "court"), std::size_t{1}, "and the court built inside it regardless");
}

TEST(a_castle_whose_every_ring_breaks_builds_nothing_inside_it) {
    const Fortified f = fortified();
    const RuralSite& fortress = f.world.humans.fortresses.front();
    // A drawbridge cannot carry the wall walk, so a ring gated with one is broken.
    const std::array<CastleRingSpec, 1> rings = {ring_spec("drawbridge")};
    const std::array<CastleCourtSpec, 1> courts = {CastleCourtSpec{"bailey", "outer", "building.bailey_court"}};
    const std::array<CastleLandmarkSpec, 1> keep = {CastleLandmarkSpec{"building.keep_tower", "outer", 0.0}};
    const std::array<CastleServiceSpec, 1> services = {CastleServiceSpec{"building.castle_barracks", 1}};
    const CastlePlan plan =
        plan_castle_with_kit(f.request(), fortress, {"kit.test_broken", "Broken", 1, 72.0, rings, courts, keep, services});

    check::equal(std::string(plan.status), std::string("partial"), "a walled precinct with no wall");
    check::equal(std::string(primary_ring(plan).status), std::string("broken"), "the ring broken");
    check::is_true(plan.plots.empty(), "nothing built inside a wall that does not stand");
    // Only the ring's failure is reported: the courts, keep and garrison are never reached,
    // so they are not unplaced either -- the reference returns before them.
    check::equal(plan.unplaced.size(), std::size_t{1}, "one failure");
    check::equal(plan.unplaced[0].building_id, std::string("ring-outer-gate-0"), "the gate that broke the ring");
    check::equal(plan.passes[2].placed, 0LL, "no ring in the perimeter pass");
    check::equal(plan.stats.wall_rings_closed, 0, "no closed ring");
    // A truthful summary where the reference writes a literal zero: the broken ring still
    // carries its curtain, and a struct cannot drop the key the way the reference does.
    check::equal(plan.stats.segments, static_cast<int>(primary_ring(plan).segments.size()),
                 "the broken ring's curtain counted");
    check::is_true(plan.stats.segments > 0, "and there is curtain to count");
    check::is_true(!plan.stats.walkway_continuous, "no walkway over no wall");
    check::equal(plan.stats.unplaced_total, 1, "the stated failure count");
}

TEST(a_barbican_gate_is_not_built_a_second_time) {
    const Fortified f = fortified();
    const RuralSite* fortress = fortress_drawing(f, "kit.concentric");
    check::is_true(fortress != nullptr, "a concentric castle in the world");
    const CastlePlan plan = plan_castle(f.request(), *fortress);
    // The concentric kit lists a barbican among its services, and its gatehouse IS a
    // barbican, so the service is skipped: already represented as the gate node.
    std::vector<Point> barbicans;
    for (const CastlePlot& p : plan.plots) {
        if (p.building_id != "building.barbican") continue;
        check::equal(std::string(p.kind), std::string("gate"), p.id + " to be a gate, not a service");
        barbicans.push_back({p.x_m, p.z_m});
    }
    check::is_true(!barbicans.empty(), "a barbican at the gate");
    for (std::size_t i = 0; i < barbicans.size(); ++i) {
        for (std::size_t j = i + 1; j < barbicans.size(); ++j) {
            check::is_true(distance(barbicans[i], barbicans[j]) >= 1.0, "no two barbicans on one spot");
        }
    }
    // Five services in the fan, not six: the skipped barbican takes no slot.
    check::equal(count_kind(plan, "service"), std::size_t{6}, "barracks, armory, well, chapel, hall, drawbridge");
}

TEST(a_gate_is_not_built_twice_where_two_rings_open_on_one_spot) {
    // With no road every ring's gate falls back to the +x extremity, and the rings collapse
    // onto one boundary, so a concentric castle's two barbican gates are the same vertex.
    // Only a gate is de-duplicated, and only against the same structure.
    const Fortified f = fortified();
    RuralSite roadless = f.world.humans.fortresses.front();
    roadless.access_nodes.clear();
    const CastlePlan plan = plan_castle_with_kit(f.request(), roadless, kit_named("kit.concentric"));
    check::is_true(plan.road_connections.empty(), "no road to open on");
    check::equal(plan.stats.wall_rings_closed, 2, "both rings closed");
    const CastleWallNetwork* outer = ring_named(plan, "ring-outer");
    const CastleWallNetwork* inner = ring_named(plan, "ring-inner");
    check::is_true(outer != nullptr && inner != nullptr, "both rings");
    check::is_true(distance(main_gate(*outer).position_m, main_gate(*inner).position_m) < 1.0,
                   "the two gates on one spot");
    check::equal(std::string(main_gate(*inner).structure_id), std::string("building.barbican"),
                 "and both of them barbicans");
    std::size_t barbicans = 0;
    for (const CastlePlot& p : plan.plots) barbicans += p.building_id == "building.barbican" ? 1 : 0;
    check::equal(barbicans, std::size_t{1}, "one barbican built for the two gate records");
    // The postern is another structure at another vertex, and is built.
    check::equal(count_kind(plan, "gate"), std::size_t{2}, "the barbican and the postern");
}

TEST(a_castle_declares_no_sleeping_capacity) {
    // Not a zero: ABSENT. No castle structure carries a bed count, and a literal 0 beside a
    // placed garrison reads as soldiers sleeping nowhere. The type cannot say otherwise --
    // and the concept is shown to see a bed count where there is one.
    static_assert(!CarriesBeds<CastlePlot>, "a castle plot must carry no beds");
    static_assert(CarriesBeds<Plot>, "the concept must detect the city plot's beds, or it proves nothing");
    const Fortified f = fortified();
    const CastlePlan plan = plan_castle(f.request(), f.world.humans.fortresses.front());
    check::is_true(plan.stats.workers > 0, "while the garrison it staffs is real");
}

TEST(a_fortress_on_open_water_is_unbuildable_and_says_why) {
    const Fortified f = fortified();
    const Grid& water = f.world.layers.at("water_type");
    const int n = f.world.config.size;
    RuralSite wet;
    bool found = false;
    for (int z = 8; z < n - 8 && !found; ++z) {
        for (int x = 0; x < n - 1 && !found; ++x) {
            if (water.at(x, z) == 1.0) {
                wet = fortress_at(f, x, z, "fortress-wet");
                found = true;
            }
        }
    }
    check::is_true(found, "an ocean node to stand on");
    const CastlePlan plan = plan_castle(f.request(), wet);
    check::equal(std::string(plan.status), std::string("unbuildable"), "a fort at sea to be unbuildable");
    check::is_true(plan.debug.valid_cells < 24, "for want of land");
    check::is_true(plan.plots.empty() && plan.wall_networks.empty(), "with nothing built");
    check::equal(plan.unplaced.size(), std::size_t{1}, "and one stated reason");
    check::equal(plan.unplaced[0].reason, std::string("Insufficient buildable land for a fortress precinct"),
                 "the reason");
    check::equal(plan.passes[0].placed, 1LL, "the map pass still ran");
    check::equal(plan.passes[1].placed, 0LL, "and the kit pass did not");
    check::equal(plan.stats.unplaced_total, 1, "a truthful summary even here");
    // The floor: a fortress on land builds.
    check::is_true(!plan_castle(f.request(), f.world.humans.fortresses.front()).plots.empty(),
                   "a fortress on land to build");
}

TEST(regional_flood_risk_does_not_disqualify_a_castle_and_standing_water_does) {
    // The regional proxy is one value for a cell kilometres across. Honouring it left 34 of
    // 56 castles unbuildable in the reference: it cannot judge one cell of a castle.
    Fortified f = fortified();
    const RuralSite fortress = f.world.humans.fortresses.front();
    const std::size_t dry = plan_castle(f.request(), fortress).debug.valid_cells;
    f.world.layers["flood_risk"] = Grid(f.world.config.size, 1.0);
    const CastlePlan flooded = plan_castle(f.request(), fortress);
    check::is_true(flooded.status != "unbuildable", "a saturated flood proxy not to empty a castle");
    check::is_true(!flooded.plots.empty(), "and the castle to be built");
    check::is_true(flooded.debug.valid_cells * 2 >= dry, "most of the ground to stay buildable");
    // The other direction, so the mask is shown to respond to the layer that SHOULD block.
    f.world.layers["water_type"] = Grid(f.world.config.size, 1.0);
    check::equal(std::string(plan_castle(f.request(), fortress).status), std::string("unbuildable"),
                 "standing water to empty it");
}

TEST(a_castle_keeps_clear_of_the_rivers_its_world_routed_last) {
    // River centrelines come from the CLIMATE block once climate has run, and from the water
    // block only before it: the reference's `world.get('climate', world.get('water'))`.
    // city_plan.cpp reads the water block unconditionally; a castle follows the reference.
    Fortified f = fortified();
    const RuralSite fortress = f.world.humans.fortresses.front();
    // From the fortress's own node to its eastern neighbour, so the line leaves the window
    // along +x through its centre.
    const std::array<int, 2> river{fortress.node, f.grid->node_index(fortress.x + 1, fortress.z)};
    // The channel is reserved only on a river CELL, and every sample in a shipped-width
    // window reads the fortress's own cell.
    f.world.layers["river"].at(fortress.x, fortress.z) = 1.0;
    f.world.climate.river_segments.clear();
    f.world.water.river_segments.clear();
    const auto code_at = [](const CastlePlan& plan, double x, double z) {
        return plan.terrain.code(static_cast<int>(std::floor((x + plan.half_m) / 4.0)),
                                 static_cast<int>(std::floor((z + plan.half_m) / 4.0)));
    };

    const CastlePlan dry = plan_castle(f.request(), fortress);
    check::is_true(dry.half_m >= 60.0, "a window wide enough for the probes below");
    for (double z : {2.0, 22.0, 46.0}) {
        check::equal(code_at(dry, 42.0, z), 0, "buildable ground at z " + std::to_string(z) + " with no river");
    }

    f.world.climate.river_segments = {river};
    const CastlePlan routed = plan_castle(f.request(), fortress);
    check::equal(code_at(routed, 42.0, 2.0), 1, "water within 12 m of the centreline");
    check::equal(code_at(routed, 42.0, 22.0), 2, "the reserved channel within 28 m of it");
    check::equal(code_at(routed, 42.0, 46.0), 0, "buildable ground beyond the setback");
    check::is_true(routed.debug.valid_cells < dry.debug.valid_cells, "the river to cost the castle ground");

    // With climate present the water block's river is not read at all...
    f.world.climate.river_segments.clear();
    f.world.water.river_segments = {river};
    check::equal(plan_castle(f.request(), fortress).debug.valid_cells, dry.debug.valid_cells,
                 "the flow-routed river ignored once climate has run");
    // ...and before climate it is the only river there is.
    f.world.has_climate = false;
    check::equal(code_at(plan_castle(f.request(), fortress), 42.0, 2.0), 1, "and read when climate has not run");
}

TEST(a_close_neighbour_crops_the_window_and_the_seam_is_no_obstacle) {
    // Adjacent nodes a row from the pole are ~153 m apart, the only place on a shipped
    // world close enough for the 0.28 crop to bind (it needs a neighbour inside 343 m).
    const Fortified f = fortified();
    const int n = f.world.config.size;
    const RuralSite a = fortress_at(f, 0, 1, "fortress-a");
    const auto plan_among = [&](const std::vector<RuralSite>& all) {
        CastleRequest request = f.request();
        request.sites = {};
        request.fortresses = all;
        return plan_castle_with_kit(request, all.front(), kit_named("kit.concentric"));
    };
    check::equal(plan_among({a}).half_m, 96.0, "a lone fort to keep its kit's extent");

    const double step = great_circle_m(f.world.effective.globe_radius, direction(0, 1, n), direction(1, 1, n));
    const double expected = std::max(4.0, std::floor(step * 0.28 / 4.0) * 4.0);
    check::is_true(expected < 96.0, "the neighbour to be close enough to matter");
    check::equal(plan_among({a, fortress_at(f, 1, 1, "fortress-b")}).half_m, expected, "a neighbour to crop it");
    // Column n-2 is column 0's other neighbour, across the seam. The reference's crop
    // measured that pair the short way round too: its law of cosines is even and periodic
    // in the longitude difference, whatever the comments next to it say.
    check::equal(plan_among({a, fortress_at(f, n - 2, 1, "fortress-c")}).half_m, expected,
                 "a neighbour across the seam to crop it exactly the same");
    // A city crops it too, by the same rule.
    CastleRequest with_city = f.request();
    std::vector<SettlementSite> city(1);
    city[0].x = 1;
    city[0].z = 1;
    with_city.sites = city;
    const std::vector<RuralSite> alone = {a};
    with_city.fortresses = alone;
    check::equal(plan_castle_with_kit(with_city, a, kit_named("kit.concentric")).half_m, expected, "a city to crop it");
}

TEST(a_drawbridge_needs_a_ditch_and_a_gate) {
    const Fortified f = fortified();
    const RuralSite& fortress = f.world.humans.fortresses.front();
    const std::array<CastleServiceSpec, 2> services = {CastleServiceSpec{"building.drawbridge", 1},
                                                       CastleServiceSpec{"building.castle_well", 1}};
    const std::array<CastleRingSpec, 1> dry_ring = {ring_spec()};
    const CastleKit dry{"kit.test_dry", "Dry", 1, 72.0, dry_ring, {}, {}, services};
    const CastlePlan without = plan_castle_with_kit(f.request(), fortress, dry);
    check::equal(without.unplaced.size(), std::size_t{1}, "one thing not built");
    check::equal(without.unplaced[0].building_id, std::string("building.drawbridge"), "the drawbridge");
    check::equal(without.unplaced[0].reason, std::string("Drawbridge requires outer ditch and gate"), "and why");
    check::equal(std::string(without.status), std::string("partial"), "which leaves the castle partial");

    std::array<CastleRingSpec, 1> ditched_ring = {ring_spec()};
    ditched_ring[0].ditch = true;
    const CastleKit ditched{"kit.test_ditched", "Ditched", 1, 72.0, ditched_ring, {}, {}, services};
    const CastlePlan with = plan_castle_with_kit(f.request(), fortress, ditched);
    check::is_true(primary_ring(with).ditch, "a ditched ring");
    const CastlePlot* bridge = nullptr;
    for (const CastlePlot& p : with.plots) {
        if (p.building_id == "building.drawbridge") bridge = &p;
    }
    check::is_true(bridge != nullptr, "a drawbridge over the ditch");
    // Along the approach: from the primary centre through the road's crossing.
    const Point gate = main_gate(primary_ring(with)).position_m;
    const Point road = support_road(with).gate_local_m;
    double cx = 0.0;
    double cz = 0.0;
    for (const Point& p : primary_ring(with).polyline_m) {
        cx += p.x;
        cz += p.z;
    }
    cx /= static_cast<double>(primary_ring(with).polyline_m.size());
    cz /= static_cast<double>(primary_ring(with).polyline_m.size());
    const double bearing = std::atan2(road.z - cz, road.x - cx);
    check::close(bridge->x_m, gate.x + std::cos(bearing) * 10.0, 0.006, "ten metres out along the approach, x");
    check::close(bridge->z_m, gate.z + std::sin(bearing) * 10.0, 0.006, "and z");
    check::equal(std::string(with.status), std::string("complete"), "and nothing left unbuilt");
}

TEST(a_kit_naming_what_does_not_exist_is_refused_or_reported) {
    const Fortified f = fortified();
    const RuralSite& fortress = f.world.humans.fortresses.front();
    const std::array<CastleRingSpec, 1> rings = {ring_spec()};

    // A SERVICE the castle library does not have is reported, as the reference does -- a
    // city's well is a real building and still not a castle's.
    const std::array<CastleServiceSpec, 2> services = {CastleServiceSpec{"building.well", 1},
                                                       CastleServiceSpec{"building.castle_well", 1}};
    const CastlePlan plan = plan_castle_with_kit(f.request(), fortress, {"kit.t", "T", 1, 72.0, rings, {}, {}, services});
    check::equal(plan.unplaced.size(), std::size_t{1}, "the unknown service reported");
    check::equal(plan.unplaced[0].reason, std::string("Unknown castle structure"), "as unknown");
    for (const CastlePlot& p : plan.plots) check::is_true(p.building_id != "building.well", "and never built");
    check::equal(count_kind(plan, "service"), std::size_t{1}, "while the known one is");

    // Anything else unknown is refused before any ground is sampled: the reference raises
    // there, on only the sites whose ring happens to close.
    const std::array<CastleCourtSpec, 1> bad_court = {CastleCourtSpec{"bailey", "outer", "building.no_such_court"}};
    check::throws([&] { plan_castle_with_kit(f.request(), fortress, {"kit.t", "T", 1, 72.0, rings, bad_court, {}, {}}); },
                  "an unknown court to be refused");
    const std::array<CastleRingSpec, 1> bad_gate = {ring_spec("no_such_module")};
    check::throws([&] { plan_castle_with_kit(f.request(), fortress, {"kit.t", "T", 1, 72.0, bad_gate, {}, {}, {}}); },
                  "an unknown gate module to be refused");
}

TEST(a_request_in_design_units_is_refused) {
    const Fortified f = fortified();
    const RuralSite& fortress = f.world.humans.fortresses.front();
    check::is_true(f.world.config.globe_radius != f.world.effective.globe_radius,
                   "design and metre radii to differ, or this proves nothing");

    CastleRequest design = f.request();
    design.globe_radius_m = f.world.config.globe_radius;
    check::throws([&] { plan_castle(design, fortress); }, "a design-space radius to be refused");

    const auto design_grid = sphere_grid(f.world.config.size, f.world.config.globe_radius);
    CastleRequest wrong_grid = f.request();
    wrong_grid.grid = design_grid.get();
    check::throws([&] { plan_castle(wrong_grid, fortress); }, "a design-space grid to be refused");

    CastleRequest no_grid = f.request();
    no_grid.grid = nullptr;
    check::throws([&] { fill_castles(no_grid); }, "a request with no grid to be refused");

    check::is_true(!plan_castle(f.request(), fortress).plots.empty(), "the same fortress in metres to build");
}

