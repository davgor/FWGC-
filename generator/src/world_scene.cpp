#include "fwg/world_scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

#include "fwg/grid.hpp"
#include "fwg/local_sampler.hpp"  // LocalFrame

namespace fwg {
namespace {

constexpr double kPi = 3.14159265358979323846;

// (radius + height) along a unit direction: a point on the ground, or at the height given.
Vec3 surface(const HeightField& field, const Vec3& p) { return p * (field.radius() + field.height(p)); }
Vec3 at_height(const HeightField& field, const Vec3& p, double height) { return p * (field.radius() + height); }

std::string site_key(const SettlementSite& site) { return !site.uid.empty() ? site.uid : std::to_string(site.id); }

// What one plan contributes, whichever planner wrote it. Plot is the city and hamlet plot;
// CastlePlot carries the same five fields a building needs.
template <typename PlotT>
void emit_plan(WorldScene& scene, const HeightField& field, const LocalFrame& frame, const SceneOwner& owner,
               const std::vector<std::vector<Point>>* street_paths, const std::vector<RoadConnection>& connections,
               const std::vector<PlotT>& plots,
               std::map<std::pair<int, std::string_view>, const RoadConnection*>* gates) {
    if (street_paths != nullptr) {
        for (std::size_t i = 0; i < street_paths->size(); ++i) {
            SceneStreet street;
            street.id = owner.id + ":street:" + std::to_string(i);
            street.owner = owner;
            const std::vector<Point>& path = (*street_paths)[i];
            street.positions_m.reserve(path.size());
            for (const Point& p : path) street.positions_m.push_back(surface(field, frame.direction_at(p.x, p.z)));
            scene.streets.push_back(std::move(street));
        }
    }

    for (const RoadConnection& c : connections) {
        // Only a city's REGIONAL connections trim a regional road. Last one wins, as the
        // reference's dict assignment does.
        if (gates != nullptr && c.route_index >= 0) (*gates)[{c.route_index, c.end}] = &c;
        const Vec3 position = surface(field, c.direction);
        SceneJunction junction;
        junction.id = c.junction_id;
        junction.owner = owner;
        junction.route_id = c.route_id;
        junction.position_m = position;
        junction.status = c.status;
        scene.junctions.push_back(std::move(junction));

        if (c.status == "connected") {
            SceneStreet approach;
            approach.id = c.junction_id + ":approach";
            approach.owner = owner;
            approach.junction_id = c.junction_id;
            approach.positions_m.reserve(c.local_path_m.size());
            for (const Point& p : c.local_path_m) {
                approach.positions_m.push_back(surface(field, frame.direction_at(p.x, p.z)));
            }
            // The approach ends ON its junction, exactly. The reference indexes [-1] blind;
            // a connected junction always carries at least the gate point, so this guard
            // never fires on a planner's output.
            if (!approach.positions_m.empty()) approach.positions_m.back() = position;
            scene.streets.push_back(std::move(approach));
        }
    }

    for (const PlotT& b : plots) {
        const Vec3 p = frame.direction_at(b.x_m, b.z_m);
        const double angle = b.rotation_degrees * kPi / 180.0;
        // The frame's east carried to this plot: projected into its tangent plane and
        // normalised. north = east x up completes the basis the rotation turns.
        const double along = dot(frame.east, p);
        const Vec3 east = unit(frame.east - p * along);
        const Vec3 north = cross(east, p);
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        const Vec3 width = east * c + north * s;
        const Vec3 depth = east * (-s) + north * c;
        const Vec3 centre = at_height(field, p, b.ground_elevation_m);

        SceneBuilding building;
        building.id = owner.id + ":" + b.id;
        building.owner = owner;
        building.plot_id = b.id;
        building.asset_id = b.building_id;
        building.position_m = centre;
        building.up = p;
        building.width_axis = width;
        building.depth_axis = depth;
        building.rotation_degrees = b.rotation_degrees;
        building.dimensions_m = b.dimensions_m;
        const double hw = b.dimensions_m.width / 2.0;
        const double hd = b.dimensions_m.depth / 2.0;
        const double us[4] = {-hw, hw, hw, -hw};
        const double vs[4] = {-hd, -hd, hd, hd};
        for (int k = 0; k < 4; ++k) {
            building.footprint_positions_m[static_cast<std::size_t>(k)] = centre + width * us[k] + depth * vs[k];
        }
        scene.buildings.push_back(std::move(building));
    }
}

Vec3 node_direction(const SphereGrid& nodes, int node, int n, std::size_t route) {
    if (node < 0 || static_cast<std::size_t>(node) >= nodes.node_count()) {
        throw std::invalid_argument("world scene: regional road " + std::to_string(route) + " names node " +
                                    std::to_string(node) + ", which is not in the node table");
    }
    const auto at = static_cast<std::size_t>(node);
    return direction(nodes.point_x[at], nodes.point_z[at], n);
}

void check(const WorldSceneRequest& request) {
    if (request.field == nullptr) {
        throw std::invalid_argument("world scene needs the height field: every position is on it");
    }
    if (request.n < 3) throw std::invalid_argument("world scene needs a raster of size 3 or more");
    const double radius = request.radius_m;
    if (!(radius > 0.0) || !std::isfinite(radius)) {
        throw std::invalid_argument("world scene needs a positive globe radius in metres");
    }
    if (std::abs(request.field->radius() - radius) > 1e-6 * std::max(1.0, radius)) {
        throw std::invalid_argument(
            "world scene given a radius of " + std::to_string(radius) + " m for a height field of " +
            std::to_string(request.field->radius()) +
            " m; the plans were laid out at the world's effective radius and a design-space one "
            "puts every building in the wrong place");
    }
    if (!request.roads.empty()) {
        if (request.nodes == nullptr) {
            throw std::invalid_argument("world scene needs the node table to place " +
                                        std::to_string(request.roads.size()) + " regional roads");
        }
        if (request.nodes->n != request.n) {
            throw std::invalid_argument("world scene given a node table of size " + std::to_string(request.nodes->n) +
                                        " for a raster of size " + std::to_string(request.n));
        }
    }
}

}  // namespace

std::string_view to_string(SceneSettlementKind kind) {
    switch (kind) {
        case SceneSettlementKind::City: return "city";
        case SceneSettlementKind::Hamlet: return "hamlet";
        case SceneSettlementKind::Castle: return "castle";
    }
    return "city";
}

WorldScene build_world_scene(const WorldSceneRequest& request) {
    check(request);
    const HeightField& field = *request.field;
    const int n = request.n;
    const double radius = request.radius_m;

    WorldScene scene;
    scene.radius_m = field.radius();
    scene.terrain_detail = field.detail();

    std::map<std::pair<int, std::string_view>, const RoadConnection*> gates;

    for (const CityPlan& city : request.cities) {
        // The plan's frame is its settlement's raster node, exactly as the planner built it.
        const SettlementSite* site = nullptr;
        for (const SettlementSite& s : request.settlements) {
            if (site_key(s) == city.city_uid) {
                site = &s;
                break;
            }
        }
        if (site == nullptr) {
            throw std::invalid_argument("world scene: city plan " + city.city_uid +
                                        " has no settlement to stand on; its frame is that settlement's node");
        }
        const LocalFrame frame = LocalFrame::at_node(n, radius, site->x, site->z);
        const SceneOwner owner{SceneSettlementKind::City, city.city_uid, city.city_uid};
        emit_plan(scene, field, frame, owner, &city.street_paths_m, city.road_connections, city.plots, &gates);
    }
    for (const HamletPlan& hamlet : request.hamlets) {
        const LocalFrame frame = LocalFrame::at_node(n, radius, hamlet.x, hamlet.z);
        const SceneOwner owner{SceneSettlementKind::Hamlet, hamlet.hamlet_id, hamlet.hamlet_uid};
        emit_plan(scene, field, frame, owner, &hamlet.street_paths_m, hamlet.road_connections, hamlet.plots,
                  nullptr);
    }
    for (const CastlePlan& castle : request.castles) {
        const LocalFrame frame = LocalFrame::at_node(n, radius, castle.x, castle.z);
        const SceneOwner owner{SceneSettlementKind::Castle, castle.fortress_id, castle.fortress_uid};
        // A castle has no street tree, so it contributes no street paths.
        emit_plan(scene, field, frame, owner, nullptr, castle.road_connections, castle.plots, nullptr);
    }

    for (std::size_t i = 0; i < request.roads.size(); ++i) {
        const Road& road = request.roads[i];
        const auto gate = [&](std::string_view end) -> const RoadConnection* {
            const auto found = gates.find({static_cast<int>(i), end});
            return found == gates.end() ? nullptr : found->second;
        };
        const RoadConnection* a = gate("from");
        const RoadConnection* b = gate("to");

        // The stretch between the two gates: from the first node outside the from-city's
        // window to the first node outside the to-city's, with each gate's own direction
        // at the ends. Python slice semantics: clipped to the path, empty when crossed.
        const std::size_t count = road.nodes.size();
        const std::size_t lo = a != nullptr ? std::min(static_cast<std::size_t>(std::max(a->outside_index, 0)), count) : 0;
        const std::size_t hi =
            b != nullptr ? std::min(static_cast<std::size_t>(std::max(b->outside_index, 0)) + 1, count) : count;
        std::vector<Vec3> vectors;
        if (a != nullptr) vectors.push_back(a->direction);
        for (std::size_t k = lo; k < hi; ++k) vectors.push_back(node_direction(*request.nodes, road.nodes[k], n, i));
        if (b != nullptr) vectors.push_back(b->direction);

        // Densify onto the same surface; route topology is unchanged. One vertex per
        // kSceneRoadStepM of arc, rounded up, by normalised linear interpolation.
        std::vector<Vec3> dense;
        for (std::size_t k = 0; k + 1 < vectors.size(); ++k) {
            const Vec3& p = vectors[k];
            const Vec3& q = vectors[k + 1];
            const double arc = field.radius() * std::acos(clamp_unit(dot(p, q)));
            const int steps = std::max(1, static_cast<int>(std::ceil(arc / kSceneRoadStepM)));
            for (int j = 0; j < steps; ++j) {
                const double t = static_cast<double>(j) / steps;
                dense.push_back(unit(p + (q - p) * t));
            }
        }
        if (!vectors.empty()) dense.push_back(vectors.back());

        SceneRegionalRoad out;
        out.id = "regional-road-" + std::to_string(i);
        out.source_route_index = static_cast<int>(i);
        out.positions_m.reserve(dense.size());
        for (const Vec3& p : dense) out.positions_m.push_back(surface(field, p));
        // The ends ARE the junctions, not a re-normalised copy of their directions.
        if (a != nullptr && !out.positions_m.empty()) out.positions_m.front() = surface(field, a->direction);
        if (b != nullptr && !out.positions_m.empty()) out.positions_m.back() = surface(field, b->direction);
        if (a != nullptr) {
            out.from_junction = a->junction_id;
            out.from_status = a->status;
        }
        if (b != nullptr) {
            out.to_junction = b->junction_id;
            out.to_status = b->status;
        }
        out.bridge_candidates = road.river_crossings;
        scene.regional_roads.push_back(std::move(out));
    }
    return scene;
}

}  // namespace fwg
