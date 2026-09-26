// The world scene: every plan and every regional road in one globe-centred frame.
//
// These run on a real phase-16 world, because what they are about is whether the scene
// puts each thing EXACTLY where its planner put it -- a building on the direction its
// plot was planned at, a junction on its plan's gate, a road on the ground between two
// gates -- and only a generated world has plans, gates and roads that meet. One world is
// generated for the whole file; every test only reads it.
//
// Two tests PIN reference behaviour a consumer could trip on: the building axes are
// left-handed (width x depth = -up), and a building's footprint is in true metres at its
// elevation while its plot was reserved in reference-sphere metres, so the two differ by
// the factor (R + elevation) / R.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "fwg/city_plan.hpp"
#include "fwg/grid.hpp"
#include "fwg/local_sampler.hpp"
#include "fwg/world.hpp"
#include "fwg/world_scene.hpp"

using namespace fwg;

namespace {

constexpr double kPi = 3.14159265358979323846;

// Generated once: a shipped-width world (200 km around) at the smallest raster, which
// still plans 34 cities, 82 hamlets and 34 castles.
const World& scene_world() {
    static const World world = [] {
        Request r;
        r.seed = 42;
        r.size = 33;
        r.phase = 16;
        return generate(resolve(r));
    }();
    return world;
}

double length(const Vec3& v) { return std::sqrt(dot(v, v)); }

std::string key_of(const SettlementSite& s) { return !s.uid.empty() ? s.uid : std::to_string(s.id); }

// The frame the owning plan was laid out in, found the way the planner found it.
LocalFrame frame_of(const World& w, const SceneOwner& owner) {
    const int n = w.config.size;
    const double radius = w.effective.globe_radius;
    switch (owner.kind) {
        case SceneSettlementKind::City:
            for (const SettlementSite& s : w.settlements.sites) {
                if (key_of(s) == owner.id) return LocalFrame::at_node(n, radius, s.x, s.z);
            }
            break;
        case SceneSettlementKind::Hamlet:
            for (const HamletPlan& p : w.hamlet_plans.hamlets) {
                if (p.hamlet_id == owner.id) return LocalFrame::at_node(n, radius, p.x, p.z);
            }
            break;
        case SceneSettlementKind::Castle:
            for (const CastlePlan& p : w.castle_plans.castles) {
                if (p.fortress_id == owner.id) return LocalFrame::at_node(n, radius, p.x, p.z);
            }
            break;
    }
    check::fail("a plan for " + owner.id);
}

// The planners' gnomonic projection, inverted: where a direction lands in a plan's window.
Point project(const LocalFrame& f, const Vec3& p) {
    const double den = dot(p, f.up);
    return {f.radius * dot(p, f.east) / den, f.radius * dot(p, f.north) / den};
}

// Metres between a point on the ground and the height field under it. Zero for a point
// the scene put on the surface.
double off_surface(const HeightField& field, const Vec3& position) {
    return std::abs(length(position) - field.radius() - field.height(unit(position)));
}

struct PlotRecord {
    SceneSettlementKind kind;
    std::string owner_id;
    std::string owner_uid;
    std::string_view building_id;
    double x_m, z_m, rotation_degrees, ground_elevation_m;
    BoxSize dimensions_m;
};

// Every plot of every plan, by the id the scene gives its building.
std::map<std::string, PlotRecord> every_plot(const World& w) {
    std::map<std::string, PlotRecord> out;
    const auto add = [&](SceneSettlementKind kind, const std::string& id, const std::string& uid, const auto& plots) {
        for (const auto& p : plots) {
            const bool fresh = out.emplace(id + ":" + p.id, PlotRecord{kind, id, uid, p.building_id, p.x_m, p.z_m,
                                                                        p.rotation_degrees, p.ground_elevation_m,
                                                                        p.dimensions_m})
                                   .second;
            check::is_true(fresh, id + ":" + p.id + " to be one plot, not two");
        }
    };
    for (const CityPlan& p : w.city_plans.cities) add(SceneSettlementKind::City, p.city_uid, p.city_uid, p.plots);
    for (const HamletPlan& p : w.hamlet_plans.hamlets) {
        add(SceneSettlementKind::Hamlet, p.hamlet_id, p.hamlet_uid, p.plots);
    }
    for (const CastlePlan& p : w.castle_plans.castles) {
        add(SceneSettlementKind::Castle, p.fortress_id, p.fortress_uid, p.plots);
    }
    return out;
}

const SceneJunction* junction_named(const WorldScene& scene, const std::string& id) {
    for (const SceneJunction& j : scene.junctions) {
        if (j.id == id) return &j;
    }
    return nullptr;
}

}  // namespace

TEST(stage_sixteen_builds_the_scene_once_its_planners_have_run) {
    const World& w = scene_world();
    check::is_true(w.has_world_scene, "stage 16 to build the world scene");
    check::is_true(std::find(w.systems.begin(), w.systems.end(), "world_scene") != w.systems.end(),
                   "the systems list to record the scene");
    check::equal(w.world_scene.version, kWorldSceneVersion, "the scene version");
    check::equal(w.world_scene.radius_m, w.effective.globe_radius, "the scene radius to be the metre radius");
    check::equal(w.world_scene.regional_roads.size(), w.roads.routes.size(), "one scene road per regional road");

    // Below stage 16 there are no plans, so there is nothing to put in a scene.
    Request r;
    r.seed = 42;
    r.size = 33;
    r.phase = 13;
    const World early = generate(resolve(r));
    check::is_true(!early.has_world_scene, "a phase-13 world to have no scene");
    check::is_true(early.world_scene.buildings.empty() && early.world_scene.regional_roads.empty(),
                   "a phase-13 world's scene to be empty");
}

TEST(every_plot_of_every_plan_becomes_one_building_tagged_with_its_settlement) {
    const World& w = scene_world();
    const std::map<std::string, PlotRecord> plots = every_plot(w);
    std::map<SceneSettlementKind, int> by_kind;
    std::set<std::string> seen;
    for (const SceneBuilding& b : w.world_scene.buildings) {
        check::is_true(seen.insert(b.id).second, b.id + " to appear once");
        const auto found = plots.find(b.id);
        check::is_true(found != plots.end(), b.id + " to be a plot of some plan");
        const PlotRecord& plot = found->second;
        check::is_true(b.owner.kind == plot.kind, b.id + " to be tagged with its own settlement's kind");
        check::equal(b.owner.id, plot.owner_id, b.id + " owner id");
        // The stable key rides beside the ordinal one. For a city they are the same key.
        check::equal(b.owner.uid, plot.owner_uid, b.id + " owner uid");
        check::equal(b.plot_id, b.id.substr(plot.owner_id.size() + 1), b.id + " plot id");
        check::equal(b.asset_id, plot.building_id, b.id + " asset");
        check::equal(b.rotation_degrees, plot.rotation_degrees, b.id + " rotation");
        check::equal(b.dimensions_m.width, plot.dimensions_m.width, b.id + " width");
        check::equal(b.dimensions_m.depth, plot.dimensions_m.depth, b.id + " depth");
        check::equal(b.dimensions_m.height, plot.dimensions_m.height, b.id + " height");
        ++by_kind[b.owner.kind];
    }
    check::equal(w.world_scene.buildings.size(), plots.size(), "exactly one building per plot");
    check::is_true(by_kind[SceneSettlementKind::City] > 0, "some city building, or cities are untested");
    check::is_true(by_kind[SceneSettlementKind::Hamlet] > 0, "some hamlet building, or hamlets are untested");
    check::is_true(by_kind[SceneSettlementKind::Castle] > 0, "some castle building, or castles are untested");
    check::equal(to_string(SceneSettlementKind::City), std::string_view("city"), "the city tag");
    check::equal(to_string(SceneSettlementKind::Hamlet), std::string_view("hamlet"), "the hamlet tag");
    check::equal(to_string(SceneSettlementKind::Castle), std::string_view("castle"), "the castle tag");

    // A hamlet's and a castle's entries join on the ORDINAL, as the reference's do, and the
    // key that survives an age advance is carried beside it rather than instead of it.
    long long rural = 0;
    for (const SceneBuilding& b : w.world_scene.buildings) {
        if (b.owner.kind == SceneSettlementKind::City) continue;
        check::is_true(b.owner.uid != b.owner.id, b.id + " to carry a stable uid distinct from its ordinal");
        check::is_true(b.id.rfind(b.owner.id + ":", 0) == 0, b.id + " to be keyed on the ordinal");
        ++rural;
    }
    check::is_true(rural > 0, "some rural building, or the keying is untested");
}

TEST(a_building_stands_on_the_direction_its_plot_was_planned_at) {
    const World& w = scene_world();
    const std::map<std::string, PlotRecord> plots = every_plot(w);
    const double radius = w.effective.globe_radius;
    for (const SceneBuilding& b : w.world_scene.buildings) {
        const PlotRecord& plot = plots.at(b.id);
        const LocalFrame frame = frame_of(w, b.owner);
        // The planner's own frame and arithmetic: the same direction, bit for bit.
        const Vec3 planned = frame.direction_at(plot.x_m, plot.z_m);
        check::is_true(b.up.x == planned.x && b.up.y == planned.y && b.up.z == planned.z,
                       b.id + " to stand on exactly the direction its plot was planned at");
        const Point back = project(frame, unit(b.position_m));
        check::close(back.x, plot.x_m, 1e-6, b.id + " x in its plan's window");
        check::close(back.z, plot.z_m, 1e-6, b.id + " z in its plan's window");
        // At the plot's RECORDED ground elevation -- the planner's highest corner -- and
        // not a fresh sample of the ground at the centre.
        check::close(length(b.position_m) - radius, plot.ground_elevation_m, 1e-6, b.id + " elevation");
    }
}

TEST(a_building_is_upright_with_orthonormal_axes_in_its_tangent_plane) {
    const World& w = scene_world();
    for (const SceneBuilding& b : w.world_scene.buildings) {
        const Vec3 radial = unit(b.position_m);
        check::close(length(b.up), 1.0, 1e-12, b.id + " up is a unit vector");
        check::close(dot(b.up, radial), 1.0, 1e-12, b.id + " up is the radial at its position");
        check::close(length(b.width_axis), 1.0, 1e-12, b.id + " width axis is a unit vector");
        check::close(length(b.depth_axis), 1.0, 1e-12, b.id + " depth axis is a unit vector");
        check::close(dot(b.width_axis, b.up), 0.0, 1e-12, b.id + " width axis is horizontal");
        check::close(dot(b.depth_axis, b.up), 0.0, 1e-12, b.id + " depth axis is horizontal");
        check::close(dot(b.width_axis, b.depth_axis), 0.0, 1e-12, b.id + " width and depth are square");
        // PINNED: width x depth = -up. (east, up, north) is right-handed, so a box built
        // on (width, depth, up) is not; a consumer must not assume it is.
        const Vec3 handed = cross(b.width_axis, b.depth_axis);
        check::close(dot(handed, b.up), -1.0, 1e-12, b.id + " width x depth to be -up");
    }
}

TEST(a_footprint_is_the_building_box_centred_on_its_position) {
    const World& w = scene_world();
    for (const SceneBuilding& b : w.world_scene.buildings) {
        const double hw = b.dimensions_m.width / 2.0;
        const double hd = b.dimensions_m.depth / 2.0;
        const double us[4] = {-hw, hw, hw, -hw};
        const double vs[4] = {-hd, -hd, hd, hd};
        for (int k = 0; k < 4; ++k) {
            const Vec3 offset = b.footprint_positions_m[static_cast<std::size_t>(k)] - b.position_m;
            check::close(dot(offset, b.width_axis), us[k], 1e-9, b.id + " corner " + std::to_string(k) + " along width");
            check::close(dot(offset, b.depth_axis), vs[k], 1e-9, b.id + " corner " + std::to_string(k) + " along depth");
            check::close(dot(offset, b.up), 0.0, 1e-9, b.id + " corner " + std::to_string(k) + " level with the centre");
        }
        check::close(length(b.footprint_positions_m[1] - b.footprint_positions_m[0]), b.dimensions_m.width, 1e-9,
                     b.id + " front edge is the building's width");
        check::close(length(b.footprint_positions_m[2] - b.footprint_positions_m[1]), b.dimensions_m.depth, 1e-9,
                     b.id + " side edge is the building's depth");
    }
}

TEST(a_footprint_turns_the_way_the_planner_turned_its_plot) {
    // Each footprint corner, projected back into its plan's window, is the planner's own
    // corners() for that plot -- once the PINNED scale is taken out. The scene builds the
    // box in true metres at the building's elevation; the planner reserved the plot in
    // metres on the reference sphere. A corner u metres out at radius R + e subtends u/(R+e),
    // so it lands u R/(R+e) out in the window: 1.6% short on this 32 km world at 500 m.
    const World& w = scene_world();
    const std::map<std::string, PlotRecord> plots = every_plot(w);
    const double radius = w.effective.globe_radius;
    double worst = 0.0;
    double worst_unscaled = 0.0;
    for (const SceneBuilding& b : w.world_scene.buildings) {
        const PlotRecord& plot = plots.at(b.id);
        const LocalFrame frame = frame_of(w, b.owner);
        const std::vector<Point> planned = corners(plot.x_m, plot.z_m, plot.dimensions_m.width, plot.dimensions_m.depth,
                                                   plot.rotation_degrees * kPi / 180.0);
        const double scale = (radius + plot.ground_elevation_m) / radius;
        for (std::size_t k = 0; k < 4; ++k) {
            const Point at = project(frame, unit(b.footprint_positions_m[k]));
            const double dx = (at.x - plot.x_m) * scale - (planned[k].x - plot.x_m);
            const double dz = (at.z - plot.z_m) * scale - (planned[k].z - plot.z_m);
            worst = std::max(worst, std::hypot(dx, dz));
            worst_unscaled = std::max(worst_unscaled, std::hypot(at.x - planned[k].x, at.z - planned[k].z));
        }
    }
    // What is left is the gnomonic window's own distortion a few hundred metres out.
    check::between(worst, 0.0, 0.01, "every corner within a centimetre of the planner's, scale taken out");
    // And the scale is real: without it the corners miss by far more than the distortion.
    check::is_true(worst_unscaled > 0.05, "the (R + e) / R scale to be measurable on this world, or it is untested");
}

TEST(streets_lie_on_the_ground_along_their_plans_paths) {
    const World& w = scene_world();
    const HeightField field(w);
    std::map<std::string, const SceneStreet*> streets;
    for (const SceneStreet& s : w.world_scene.streets) {
        check::is_true(streets.emplace(s.id, &s).second, s.id + " to be one street");
        for (const Vec3& p : s.positions_m) check::close(off_surface(field, p), 0.0, 1e-6, s.id + " on the ground");
    }
    long long checked = 0;
    const auto check_plan = [&](SceneSettlementKind kind, const std::string& id,
                                const std::vector<std::vector<Point>>& paths) {
        const LocalFrame frame = frame_of(w, SceneOwner{kind, id, id});
        for (std::size_t i = 0; i < paths.size(); ++i) {
            const std::string street_id = id + ":street:" + std::to_string(i);
            const auto found = streets.find(street_id);
            check::is_true(found != streets.end(), street_id + " to be in the scene");
            const SceneStreet& street = *found->second;
            check::is_true(street.owner.kind == kind && street.owner.id == id, street_id + " owner");
            check::is_true(street.junction_id.empty(), street_id + " to be a plan street, not an approach");
            check::equal(street.positions_m.size(), paths[i].size(), street_id + " one vertex per path point");
            for (std::size_t k = 0; k < paths[i].size(); ++k) {
                const Point back = project(frame, unit(street.positions_m[k]));
                check::close(back.x, paths[i][k].x, 1e-6, street_id + " vertex x");
                check::close(back.z, paths[i][k].z, 1e-6, street_id + " vertex z");
                ++checked;
            }
        }
    };
    for (const CityPlan& p : w.city_plans.cities) check_plan(SceneSettlementKind::City, p.city_uid, p.street_paths_m);
    for (const HamletPlan& p : w.hamlet_plans.hamlets) {
        check_plan(SceneSettlementKind::Hamlet, p.hamlet_id, p.street_paths_m);
    }
    check::is_true(checked > 0, "some street vertex, or streets are untested");
}

TEST(a_junction_sits_on_its_plans_gate_and_its_approach_ends_on_it) {
    const World& w = scene_world();
    const HeightField field(w);
    long long connections = 0;
    long long approaches = 0;
    const auto check_plan = [&](SceneSettlementKind kind, const std::string& id,
                                const std::vector<RoadConnection>& links) {
        const LocalFrame frame = frame_of(w, SceneOwner{kind, id, id});
        for (const RoadConnection& c : links) {
            ++connections;
            const SceneJunction* j = junction_named(w.world_scene, c.junction_id);
            check::is_true(j != nullptr, c.junction_id + " to be in the scene");
            check::is_true(j->owner.kind == kind && j->owner.id == id, c.junction_id + " owner");
            check::equal(j->route_id, c.route_id, c.junction_id + " route");
            check::equal(j->status, c.status, c.junction_id + " status");
            check::close(off_surface(field, j->position_m), 0.0, 1e-6, c.junction_id + " on the ground");
            const Point back = project(frame, unit(j->position_m));
            check::close(back.x, c.gate_local_m.x, 1e-6, c.junction_id + " on its gate, x");
            check::close(back.z, c.gate_local_m.z, 1e-6, c.junction_id + " on its gate, z");

            const std::string approach_id = c.junction_id + ":approach";
            const SceneStreet* approach = nullptr;
            for (const SceneStreet& s : w.world_scene.streets) {
                if (s.id == approach_id) approach = &s;
            }
            if (c.status != "connected") {
                check::is_true(approach == nullptr, approach_id + " only for a connected junction");
                continue;
            }
            check::is_true(approach != nullptr, approach_id + " to exist for a connected junction");
            check::equal(approach->junction_id, c.junction_id, approach_id + " names its junction");
            check::equal(approach->positions_m.size(), c.local_path_m.size(), approach_id + " vertex count");
            // Along the plan's own approach path, on the ground.
            for (std::size_t k = 0; k < c.local_path_m.size(); ++k) {
                const Point at = project(frame, unit(approach->positions_m[k]));
                check::close(at.x, c.local_path_m[k].x, 1e-6, approach_id + " vertex x");
                check::close(at.z, c.local_path_m[k].z, 1e-6, approach_id + " vertex z");
                check::close(off_surface(field, approach->positions_m[k]), 0.0, 1e-6, approach_id + " on the ground");
            }
            const Vec3& end = approach->positions_m.back();
            check::is_true(end.x == j->position_m.x && end.y == j->position_m.y && end.z == j->position_m.z,
                           approach_id + " to end exactly on its junction");
            ++approaches;
        }
    };
    for (const CityPlan& p : w.city_plans.cities) check_plan(SceneSettlementKind::City, p.city_uid, p.road_connections);
    for (const HamletPlan& p : w.hamlet_plans.hamlets) {
        check_plan(SceneSettlementKind::Hamlet, p.hamlet_id, p.road_connections);
    }
    for (const CastlePlan& p : w.castle_plans.castles) {
        check_plan(SceneSettlementKind::Castle, p.fortress_id, p.road_connections);
    }
    check::equal(static_cast<long long>(w.world_scene.junctions.size()), connections, "one junction per connection");
    check::is_true(approaches > 0, "some connected junction, or approaches are untested");
}

TEST(a_regional_road_is_draped_on_the_ground_every_four_metres_between_its_gates) {
    const World& w = scene_world();
    const HeightField field(w);
    const double radius = w.effective.globe_radius;
    // Whose gate is at which end of which road, straight from the city plans.
    std::map<std::pair<int, std::string_view>, const RoadConnection*> gates;
    for (const CityPlan& p : w.city_plans.cities) {
        for (const RoadConnection& c : p.road_connections) {
            if (c.route_index >= 0) gates[{c.route_index, c.end}] = &c;
        }
    }
    long long gated_ends = 0;
    for (std::size_t i = 0; i < w.world_scene.regional_roads.size(); ++i) {
        const SceneRegionalRoad& road = w.world_scene.regional_roads[i];
        const Road& route = w.roads.routes[i];
        const std::string id = "regional-road-" + std::to_string(i);
        check::equal(road.id, id, "road id");
        check::equal(road.source_route_index, static_cast<int>(i), id + " source index");
        check::is_true(road.bridge_candidates == route.river_crossings, id + " to carry its river crossings");
        check::is_true(road.positions_m.size() >= 2, id + " to have both ends");

        double arc = 0.0;
        double longest = 0.0;
        for (std::size_t k = 0; k < road.positions_m.size(); ++k) {
            check::close(off_surface(field, road.positions_m[k]), 0.0, 1e-6, id + " on the ground");
            if (k == 0) continue;
            const double step = great_circle_m(radius, unit(road.positions_m[k - 1]), unit(road.positions_m[k]));
            arc += step;
            longest = std::max(longest, step);
        }
        // The chord interpolation over-runs the step in mid-segment by 2 tan(a/2)/a; a
        // one-cell diagonal at this raster is 0.28 rad, which is under 0.7%.
        check::between(longest, 0.0, kSceneRoadStepM * 1.01, id + " longest step on the reference sphere");
        // And no denser than it has to be: ceil() adds at most one step per coarse segment.
        const double steps = static_cast<double>(road.positions_m.size() - 1);
        check::is_true(steps >= arc / kSceneRoadStepM - 1e-6, id + " to take at most one vertex per 4 m");
        check::is_true(steps <= arc / kSceneRoadStepM + static_cast<double>(route.nodes.size()) + 2.0,
                       id + " to be no denser than one vertex per 4 m per coarse segment");

        const auto end_check = [&](std::string_view end, const Vec3& vertex, const std::string& junction,
                                   std::string_view status) {
            const auto found = gates.find({static_cast<int>(i), end});
            if (found == gates.end()) {
                check::is_true(junction.empty(), id + " " + std::string(end) + " to name no junction without a gate");
                check::equal(status, std::string_view("no_city_boundary"), id + " " + std::string(end) + " status");
                return;
            }
            ++gated_ends;
            const RoadConnection& c = *found->second;
            check::equal(junction, c.junction_id, id + " " + std::string(end) + " junction");
            check::equal(status, c.status, id + " " + std::string(end) + " status");
            const SceneJunction* j = junction_named(w.world_scene, c.junction_id);
            check::is_true(j != nullptr, c.junction_id + " to be a scene junction");
            check::is_true(vertex.x == j->position_m.x && vertex.y == j->position_m.y && vertex.z == j->position_m.z,
                           id + " to meet " + c.junction_id + " exactly");
        };
        end_check("from", road.positions_m.front(), road.from_junction, road.from_status);
        end_check("to", road.positions_m.back(), road.to_junction, road.to_status);
    }
    check::is_true(!w.world_scene.regional_roads.empty(), "some regional road, or roads are untested");
    check::is_true(gated_ends > 0, "some gated road end, or trimming is untested");
}

TEST(a_regional_road_runs_from_its_first_node_when_no_city_window_cuts_it) {
    // A road whose end has no gate is drawn from that end's own node: the reference's
    // `no_city_boundary`. Built by hand, because on a generated world every road leaves
    // both of its cities' windows.
    const World& w = scene_world();
    const HeightField field(w);
    const auto grid = sphere_grid(w.config.size, w.effective.globe_radius);
    const Road& real = w.roads.routes.front();
    WorldSceneRequest request;
    request.n = w.config.size;
    request.radius_m = w.effective.globe_radius;
    request.field = &field;
    request.nodes = grid.get();
    request.settlements = w.settlements.sites;
    request.roads = std::span<const Road>(&real, 1);
    const WorldScene scene = build_world_scene(request);
    check::equal(scene.regional_roads.size(), std::size_t{1}, "one road");
    const SceneRegionalRoad& road = scene.regional_roads.front();
    check::is_true(road.from_junction.empty() && road.to_junction.empty(), "no junction without a plan");
    check::equal(road.from_status, std::string_view("no_city_boundary"), "from status");
    check::equal(road.to_status, std::string_view("no_city_boundary"), "to status");
    const auto node_point = [&](int node) {
        const auto at = static_cast<std::size_t>(node);
        return direction(grid->point_x[at], grid->point_z[at], w.config.size);
    };
    // By the chord, not the arc: acos of a dot product one rounding step below 1 reads as
    // sqrt(2 * 2^-53) of the radius, 0.47 mm on a 31.8 km globe, so an arc cannot resolve a
    // micrometre here. The chord is exact to rounding.
    check::close(request.radius_m * length(unit(road.positions_m.front()) - node_point(real.nodes.front())), 0.0, 1e-6,
                 "the road to start on its first node");
    check::close(request.radius_m * length(unit(road.positions_m.back()) - node_point(real.nodes.back())), 0.0, 1e-6,
                 "the road to end on its last node");
}

TEST(a_request_the_scene_cannot_place_is_refused) {
    const World& w = scene_world();
    const HeightField field(w);
    const auto grid = sphere_grid(w.config.size, w.effective.globe_radius);
    const auto good = [&] {
        WorldSceneRequest r;
        r.n = w.config.size;
        r.radius_m = w.effective.globe_radius;
        r.field = &field;
        r.nodes = grid.get();
        r.settlements = w.settlements.sites;
        r.roads = w.roads.routes;
        r.cities = w.city_plans.cities;
        r.hamlets = w.hamlet_plans.hamlets;
        r.castles = w.castle_plans.castles;
        return r;
    };
    // The control: the same request, unbroken, is accepted -- and is the scene the stage
    // wrote, vertex for vertex, bit for bit.
    const WorldScene again = build_world_scene(good());
    const auto same = [](const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
    check::equal(again.buildings.size(), w.world_scene.buildings.size(), "the control's buildings");
    for (std::size_t i = 0; i < again.buildings.size(); ++i) {
        check::is_true(again.buildings[i].id == w.world_scene.buildings[i].id &&
                           same(again.buildings[i].position_m, w.world_scene.buildings[i].position_m) &&
                           same(again.buildings[i].width_axis, w.world_scene.buildings[i].width_axis),
                       "building " + std::to_string(i) + " to be the stage's, bit for bit");
    }
    check::equal(again.streets.size(), w.world_scene.streets.size(), "the control's streets");
    check::equal(again.regional_roads.size(), w.world_scene.regional_roads.size(), "the control's roads");
    for (std::size_t i = 0; i < again.regional_roads.size(); ++i) {
        const auto& mine = again.regional_roads[i].positions_m;
        const auto& stage = w.world_scene.regional_roads[i].positions_m;
        check::equal(mine.size(), stage.size(), "road " + std::to_string(i) + " vertex count");
        for (std::size_t k = 0; k < mine.size(); ++k) {
            check::is_true(same(mine[k], stage[k]), "road " + std::to_string(i) + " to be the stage's, bit for bit");
        }
    }

    WorldSceneRequest no_field = good();
    no_field.field = nullptr;
    check::throws([&] { build_world_scene(no_field); }, "a scene with no height field");

    WorldSceneRequest design = good();
    design.radius_m = w.config.globe_radius;  // design space, not metres
    check::throws([&] { build_world_scene(design); }, "a design-space radius");

    WorldSceneRequest no_nodes = good();
    no_nodes.nodes = nullptr;
    check::throws([&] { build_world_scene(no_nodes); }, "roads with no node table");

    WorldSceneRequest homeless = good();
    homeless.settlements = {};
    check::throws([&] { build_world_scene(homeless); }, "a city plan with no settlement to stand on");

    // With no city plans there are no gates, so the whole node path is read.
    Road stray = w.roads.routes.front();
    stray.nodes.back() = static_cast<int>(grid->node_count());
    WorldSceneRequest off_table = good();
    off_table.cities = {};
    off_table.roads = std::span<const Road>(&stray, 1);
    check::throws([&] { build_world_scene(off_table); }, "a road node outside the node table");
    // The control for that one: the same road, unbroken, is placed.
    WorldSceneRequest on_table = off_table;
    on_table.roads = std::span<const Road>(&w.roads.routes.front(), 1);
    check::equal(build_world_scene(on_table).regional_roads.size(), std::size_t{1}, "the unbroken road");
}
