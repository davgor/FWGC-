#include "fwg/history.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "fwg/content.hpp"
#include "fwg/magic.hpp"
#include "fwg/measure.hpp"
#include "fwg/water.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

// Rotate a point about an axis by Rodrigues' formula. `omega` carries both the axis
// and the speed, so the angle is |omega| * t.
Vec3 rotate(const Vec3& p, const Vec3& omega, double t) {
    const double speed = std::sqrt(dot(omega, omega));
    if (speed == 0.0) return p;
    const Vec3 axis = unit(omega);
    const double angle = speed * t;
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const Vec3 crossed = cross(axis, p);
    const double along = dot(axis, p);
    return {p.x * c + crossed.x * s + axis.x * along * (1.0 - c),
            p.y * c + crossed.y * s + axis.y * along * (1.0 - c),
            p.z * c + crossed.z * s + axis.z * along * (1.0 - c)};
}

Grid copy_or_zero(const World& world, const char* name) {
    const auto it = world.layers.find(name);
    return it != world.layers.end() ? it->second : Grid(world.config.size, 0.0);
}

}  // namespace

void measure_surface(World& world) {
    const Config& cfg = world.effective;
    const Grid& height = world.layers.at("height");

    const Relief relief = measure_globe(height, cfg.globe_radius, cfg.radius);
    world.layers["slope"] = relief.slope;
    world.layers["tpi"] = relief.tpi;

    Grid land(cfg.size);
    for (int z = 0; z < cfg.size; ++z) {
        for (int x = 0; x < cfg.size; ++x) {
            land.at(x, z) = height.at(x, z) > cfg.sea_level ? 1.0 : 0.0;
        }
    }
    world.layers["land"] = land;
    world.area = land_area(height, cfg.globe_radius, cfg.sea_level);
}

SurfaceSnapshot tectonic_transition(World& world) {
    const Config& cfg = world.effective;
    const int n = cfg.size;

    SurfaceSnapshot old;
    old.height = world.layers.at("height");
    old.river = copy_or_zero(world, "river");
    old.water_type = copy_or_zero(world, "water_type");
    old.water_depth = copy_or_zero(world, "water_depth");

    GeologicalEpoch epoch;
    epoch.angular_duration = kEpochDuration;
    epoch.plates_before = world.plates;

    // Every plate turns along its OWN angular velocity, so the pattern of boundaries
    // is genuinely different afterwards rather than the whole map sliding as one.
    std::vector<Plate> moved = world.plates;
    for (Plate& plate : moved) plate.center = rotate(plate.center, plate.omega, kEpochDuration);
    epoch.plates_after = moved;

    Grid height(n), uplift_layer(n), owner_layer(n), convergence(n), divergence(n);
    std::vector<double> h_row, u_row, o_row, c_row, d_row;

    for (int z = 0; z < n; ++z) {
        const int count = distinct_count(z, n);
        h_row.assign(static_cast<std::size_t>(count), 0.0);
        u_row.assign(static_cast<std::size_t>(count), 0.0);
        o_row.assign(static_cast<std::size_t>(count), 0.0);
        c_row.assign(static_cast<std::size_t>(count), 0.0);
        d_row.assign(static_cast<std::size_t>(count), 0.0);

        for (int x = 0; x < count; ++x) {
            const Vec3 p = direction(x, z, n);
            // The plate model, re-evaluated against the MOVED plates.
            const PointSample record = layout_point(p, moved, world.crust_seed, cfg);
            const auto owner = static_cast<std::size_t>(record.owner);

            // Backtrace: where was this ground before its plate carried it here?
            const Vec3 back = rotate(p, moved[owner].omega, -kEpochDuration);
            const double transported = sample(old.height, back);

            // Fresh boundary work over the new geometry. Convergence builds, divergence
            // opens, shear contributes a little roughness.
            const double uplift = cfg.tectonic_relief *
                                  (0.9 * record.convergence - 0.7 * record.divergence + 0.18 * record.shear);

            // A quarter of the old surface stays put. Without that the whole crust
            // slides and nothing of the first epoch survives anywhere.
            const double value = 0.25 * old.height.at(x, z) + 0.75 * transported + uplift;

            h_row[static_cast<std::size_t>(x)] = value;
            u_row[static_cast<std::size_t>(x)] = value - old.height.at(x, z);
            o_row[static_cast<std::size_t>(x)] = record.owner;
            c_row[static_cast<std::size_t>(x)] = record.convergence;
            d_row[static_cast<std::size_t>(x)] = record.divergence;
        }

        height.fill_row(z, h_row);
        uplift_layer.fill_row(z, u_row);
        owner_layer.fill_row(z, o_row);
        convergence.fill_row(z, c_row);
        divergence.fill_row(z, d_row);
    }

    world.layers["height"] = height;
    world.layers["second_uplift"] = uplift_layer;
    world.layers["moved_plates"] = owner_layer;
    world.layers["second_convergence"] = convergence;
    world.layers["second_divergence"] = divergence;
    world.layers["pre_tectonic_height"] = old.height;
    world.layers["old_river"] = old.river;
    world.layers["old_water_type"] = old.water_type;

    world.plates = moved;
    world.epoch = epoch;
    world.has_epoch = true;

    // The surface moved, so everything measured from it is stale, and the water has to
    // find its way down the new ground.
    measure_surface(world);
    add_water(world);

    world.systems.push_back("tectonic_transition");
    world.warnings.push_back(
        "50-million-year artistic epoch: rotate the original plate centres, backtrace crust, blend transported "
        "relief with new boundary uplift. Angular speeds are dimensionless, not calibrated geodynamics.");
    return old;
}

void add_biome_variants(World& world) {
    const int n = world.config.size;
    const Grid& natural = world.layers.at("natural_biome");
    const auto schools = magic_school_table();

    // Hoisted: this runs once per cell per school, and a map lookup on a built string
    // key inside that loop is 263k string builds at size 513.
    std::vector<const Grid*> ley;
    ley.reserve(schools.size());
    for (const MagicSchool& s : schools) ley.push_back(&world.layers.at("ley_" + std::string(s.name)));

    Grid variant(n);
    std::vector<double> potency(schools.size());
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            for (std::size_t s = 0; s < schools.size(); ++s) potency[s] = ley[s]->at(x, z);
            const int school = dominant_school(potency, world.magic.mutation_threshold,
                                               world.magic.dominance_margin);
            if (school < 0) {
                variant.at(x, z) = -1.0;  // nobody holds this ground
                continue;
            }
            const NaturalBiome* core = natural_biome(static_cast<int>(natural.at(x, z)));
            variant.at(x, z) = core ? biome_variant_index(core->core, schools[static_cast<std::size_t>(school)].name)
                                    : -1.0;
        }
    }

    world.layers["biome_variant"] = variant;
    // `biome` follows the natural ground: the variant layer carries the magical state,
    // and duplicating it into `biome` would make two sources of truth disagree.
    world.layers["biome"] = natural;
    world.systems.push_back("biome_variants");
    world.warnings.push_back(
        "biome and natural_biome are natural catalogue IDs, never array offsets; biome_variant is a magical "
        "catalogue INDEX or -1. Food and habitat rules use the natural core and the explicit variant id, with "
        "independent school risk.");
}

AbandonedWaterways abandoned_waterways(const SurfaceSnapshot& old, const World& world) {
    const int n = world.config.size;
    const Grid& river_now = world.layers.at("river");
    const Grid& type_now = world.layers.at("water_type");

    AbandonedWaterways out;
    out.gorges = Grid(n);
    out.valleys = Grid(n);
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            // A gorge is a bed that carried a river and now carries nothing at all --
            // dry ground, not a river that merely moved into a lake.
            const bool gorge = old.river.at(x, z) != 0.0 && river_now.at(x, z) == 0.0 &&
                               type_now.at(x, z) == 0.0;
            // A valley is a lake or sea floor that is now dry.
            const bool valley = old.water_type.at(x, z) != 0.0 && type_now.at(x, z) == 0.0;
            out.gorges.at(x, z) = gorge ? 1.0 : 0.0;
            out.valleys.at(x, z) = valley ? 1.0 : 0.0;
        }
    }
    return out;
}

void carve_relics(World& world, const SurfaceSnapshot& old) {
    const Config& cfg = world.effective;
    const int n = cfg.size;

    const AbandonedWaterways relics = abandoned_waterways(old, world);

    const auto held = sphere_grid(n, cfg.globe_radius);
    const SphereGrid& grid = *held;
    const std::size_t count = grid.node_count();

    std::vector<double> cuts(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        const int x = grid.point_x[i];
        const int z = grid.point_z[i];
        if (relics.gorges.at(x, z) != 0.0) {
            // A river bed cuts a modest, bounded notch scaled to the world's relief.
            cuts[i] = std::min(45.0, std::max(4.0, cfg.tectonic_relief * 0.12));
        } else if (relics.valleys.at(x, z) != 0.0) {
            // A drained basin cuts to a fraction of how deep its water used to be.
            cuts[i] = std::min(100.0, std::max(12.0, old.water_depth.at(x, z) * 0.6));
        }
    }

    // Small shoulders, so a former bed reads as terrain rather than a line of marker
    // pixels. Taken from the ORIGINAL cuts, so shoulders do not grow shoulders.
    std::vector<double> shouldered = cuts;
    for (std::size_t i = 0; i < count; ++i) {
        double best = 0.0;
        for (const SphereNeighbour& nb : grid.neighbours[i]) {
            best = std::max(best, cuts[static_cast<std::size_t>(nb.index)] * 0.25);
        }
        shouldered[i] = std::max(cuts[i], best);
    }

    const Grid incision = grid.to_grid(shouldered);
    world.layers["relic_incision"] = incision;

    Grid& height = world.layers.at("height");
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) height.at(x, z) -= incision.at(x, z);
    }

    measure_surface(world);
    add_water(world);

    // Cutting a bed can let water back into it. Only the beds that are STILL dry are
    // current gorges and valleys; the rest are recorded as history.
    const AbandonedWaterways current = abandoned_waterways(old, world);
    world.layers["gorge"] = current.gorges;
    world.layers["dry_valley"] = current.valleys;
    world.layers["relic_gorge"] = relics.gorges;
    world.layers["relic_valley"] = relics.valleys;

    world.systems.push_back("relic_carving");
    world.warnings.push_back(
        "Abandoned waterways are incised as gorges and dry valleys after the epoch drained them. The cut is an "
        "artistic depth scaled to relief and former water depth, not a calculated erosion budget.");
}

}  // namespace fwg
