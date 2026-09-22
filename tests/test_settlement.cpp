// Settlement scoring invariants.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

#include "fwg/settlement.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

World build(int size = 33) {
    Request r;
    r.seed = 42;
    r.size = size;
    r.phase = 9;
    return generate(resolve(r));
}

const CivilizationTraits& default_traits() {
    return civilization_profile(default_profile_id())->traits;
}

}  // namespace

// --- fresh water ----------------------------------------------------------

TEST(fresh_water_is_lakes_and_rivers_and_never_the_sea) {
    const World world = build(65);
    const auto held = sphere_grid(world.config.size, world.effective.globe_radius);
    const SphereGrid& grid = *held;
    const FreshwaterAccess fresh = freshwater_access(world, grid);

    const Grid& type = world.layers.at("water_type");
    const Grid& river = world.layers.at("rain_river");
    const Grid& salinity = world.layers.at("salinity");

    int sources = 0;
    int ocean_sources = 0;
    for (std::size_t i = 0; i < grid.node_count(); ++i) {
        const int x = grid.point_x[i];
        const int z = grid.point_z[i];
        if (fresh.distance_m[i] != 0.0) continue;
        ++sources;
        const bool is_fresh = (type.at(x, z) == 2.0 || river.at(x, z) != 0.0) && salinity.at(x, z) < 0.2;
        check::is_true(is_fresh, "a source to be a drinkable lake or river");
        if (type.at(x, z) == 1.0) ++ocean_sources;
    }
    check::is_true(sources > 0, "a world with rivers to have fresh water");
    check::equal(ocean_sources, 0, "the sea is never a source of drinking water");

    // Every reachable cell's distance is non-negative and names a real source.
    for (std::size_t i = 0; i < grid.node_count(); ++i) {
        if (!std::isfinite(fresh.distance_m[i])) {
            check::equal(fresh.source[i], -1, "unreachable water names no source");
            continue;
        }
        check::is_true(fresh.distance_m[i] >= 0.0, "a non-negative distance");
        check::is_true(fresh.source[i] >= 0, "a reachable cell names its source");
        check::equal(fresh.distance_m[static_cast<std::size_t>(fresh.source[i])], 0.0,
                     "and that source is itself at zero");
    }
}

TEST(fresh_water_does_not_travel_across_the_ocean) {
    // A spring on one island is no use on another, however close across the water. The
    // search refuses to expand through ocean, so an island with no fresh water of its
    // own has none at all.
    const World world = build(65);
    const auto held = sphere_grid(world.config.size, world.effective.globe_radius);
    const SphereGrid& grid = *held;
    const FreshwaterAccess fresh = freshwater_access(world, grid);
    const Grid& type = world.layers.at("water_type");

    int unreachable = 0;
    for (std::size_t i = 0; i < grid.node_count(); ++i) {
        if (type.at(grid.point_x[i], grid.point_z[i]) != 1.0) continue;
        // Open ocean cells are only ever reached if a river ran into them, never
        // through them.
        if (!std::isfinite(fresh.distance_m[i])) ++unreachable;
    }
    check::is_true(unreachable > 0, "most of the sea to have no fresh water");
}

// --- flood ----------------------------------------------------------------

TEST(flood_risk_falls_with_distance_and_with_height_above_the_water) {
    const World world = build(65);
    const auto held = sphere_grid(world.config.size, world.effective.globe_radius);
    const SphereGrid& grid = *held;
    const FreshwaterAccess fresh = freshwater_access(world, grid);
    const std::vector<double> flood = flood_risk(world, grid, fresh);

    for (const double v : flood) check::between(v, 0.0, 1.0, "flood risk is a 0..1 proxy");

    const Grid& height = world.layers.at("height");
    const Grid& surface = world.layers.at("water_surface");
    int at_risk = 0;
    for (std::size_t i = 0; i < grid.node_count(); ++i) {
        if (flood[i] <= 0.0) continue;
        ++at_risk;
        // Anything at risk is close to water AND not far above it.
        check::is_true(std::isfinite(fresh.distance_m[i]), "at-risk ground to have water in reach");
        const auto src = static_cast<std::size_t>(fresh.source[i]);
        const double freeboard = height.at(grid.point_x[i], grid.point_z[i]) -
                                 surface.at(grid.point_x[src], grid.point_z[src]);
        check::is_true(freeboard < 10.0, "ten metres of freeboard removes the risk entirely");
    }
    check::is_true(at_risk > 0, "a world with rivers to have some flood-prone ground");

    // And a cell far from water carries no risk at all.
    int dry = 0;
    for (std::size_t i = 0; i < grid.node_count(); ++i) {
        if (std::isfinite(fresh.distance_m[i]) && fresh.distance_m[i] < 10000.0) continue;
        check::close(flood[i], 0.0, 1e-9, "ground far from water does not flood");
        ++dry;
    }
    check::is_true(dry > 0, "some ground to be far from water");
}

// --- landmass -------------------------------------------------------------

TEST(landmass_context_partitions_the_land) {
    const World world = build(65);
    const auto held = sphere_grid(world.config.size, world.effective.globe_radius);
    const SphereGrid& grid = *held;

    std::vector<double> water(grid.node_count());
    const Grid& type = world.layers.at("water_type");
    for (std::size_t i = 0; i < water.size(); ++i) water[i] = type.at(grid.point_x[i], grid.point_z[i]);

    const LandmassContext ctx = landmass_context(grid, water);

    double land_total = 0.0;
    std::set<double> distinct_areas;
    int largest_cells = 0;
    for (std::size_t i = 0; i < grid.node_count(); ++i) {
        if (water[i] != 0.0) {
            check::equal(ctx.area_m2[i], 0.0, "water belongs to no landmass");
            check::equal(ctx.fraction[i], 0.0, "and holds no fraction of the land");
            continue;
        }
        land_total += grid.areas[i];
        check::is_true(ctx.area_m2[i] > 0.0, "a land cell to belong to a landmass");
        check::between(ctx.fraction[i], 0.0, 1.0, "a fraction of the land");
        distinct_areas.insert(ctx.area_m2[i]);
        if (ctx.largest[i]) ++largest_cells;
    }
    check::is_true(!distinct_areas.empty(), "a world with land");
    check::is_true(largest_cells > 0, "one landmass to be the largest");

    // The fractions of one cell per landmass must sum to one: the landmasses partition
    // the land exactly, with nothing double-counted and nothing lost.
    double summed = 0.0;
    for (const double area : distinct_areas) summed += area;
    check::close(summed, land_total, land_total * 1e-9, "the landmasses to account for all the land");
}

// --- farming --------------------------------------------------------------

TEST(farming_potential_is_ruined_by_any_one_bad_condition) {
    const CivilizationTraits& t = default_traits();
    const double ideal_temp = t.food_temperature_ideal;
    const double ideal_wet = t.food_moisture_ideal;

    const FarmingPotential best = farming_potential(0.0, ideal_temp, ideal_wet, 0.0, 0.0, 0.7, t);
    check::is_true(best.natural > 0.5, "perfect ground to farm well");

    // Each condition broken ALONE must ruin it, because they multiply rather than
    // average -- steep ground is not redeemed by a perfect climate.
    check::is_true(farming_potential(80.0, ideal_temp, ideal_wet, 0.0, 0.0, 0.7, t).natural < best.natural * 0.5,
                   "a cliff farms badly whatever its climate");
    check::is_true(farming_potential(0.0, ideal_temp + 200.0, ideal_wet, 0.0, 0.0, 0.7, t).natural == 0.0,
                   "ground far outside its temperature range grows nothing");
    check::is_true(farming_potential(0.0, ideal_temp, ideal_wet, 1.0, 0.0, 0.7, t).natural < best.natural,
                   "flooding costs yield");
    check::is_true(farming_potential(0.0, ideal_temp, ideal_wet * 10.0, 0.0, 0.0, 0.7, t).natural == 0.0,
                   "a swamp grows nothing either");
}

TEST(irrigation_needs_a_source_and_only_helps_dry_ground) {
    const CivilizationTraits& t = default_traits();
    const double dry = t.food_moisture_ideal * 0.3;

    const FarmingPotential with_water = farming_potential(0.0, t.food_temperature_ideal, dry, 0.0, 0.0, 0.7, t);
    check::is_true(with_water.irrigated > with_water.natural, "water at hand to improve dry ground");

    // No source in reach means no irrigation at all, however adaptable the people.
    const FarmingPotential far = farming_potential(0.0, t.food_temperature_ideal, dry, 0.0, -1.0, 1.0, t);
    check::equal(far.irrigated, far.natural, "no source, no irrigation");
    const FarmingPotential distant =
        farming_potential(0.0, t.food_temperature_ideal, dry, 0.0, t.water_reach * 50.0, 1.0, t);
    check::close(distant.irrigated, distant.natural, 1e-9, "water far out of reach is no water");

    // Irrigation cannot drain a swamp: ground already wetter than ideal gains nothing.
    const double wet = t.food_moisture_ideal * 1.2;
    const FarmingPotential swamp = farming_potential(0.0, t.food_temperature_ideal, wet, 0.0, 0.0, 0.7, t);
    check::close(swamp.irrigated, swamp.natural, 1e-12, "irrigation does not dry ground out");

    // And an unadaptable people wins nothing from it.
    const FarmingPotential stubborn = farming_potential(0.0, t.food_temperature_ideal, dry, 0.0, 0.0, 0.0, t);
    check::close(stubborn.irrigated, stubborn.natural, 1e-12, "no adaptation, no irrigation");
}

// --- suitability ----------------------------------------------------------

TEST(suitability_is_bounded_and_water_is_never_a_site) {
    const CivilizationTraits& t = default_traits();
    SiteConditions at;
    at.freshwater_distance_m = 100.0;
    at.slope_deg = 2.0;
    at.temperature_c = t.temperature_ideal;
    at.moisture = t.moisture_ideal;
    at.resource = 0.5;

    const double good = site_suitability(t, at, false);
    check::between(good, 0.0, 1.0, "suitability is a 0..1 score");
    check::is_true(good > 0.0, "good ground to score above zero");
    check::equal(site_suitability(t, at, true), 0.0, "nobody builds on the sea");

    // Every penalty must be able to reduce it, and every good raise it.
    SiteConditions flooded = at;
    flooded.flood = 1.0;
    check::is_true(site_suitability(t, flooded, false) < good, "flooding to lower a site");
    SiteConditions hazardous = at;
    hazardous.magic_hazard = 1.0;
    check::is_true(site_suitability(t, hazardous, false) < good, "magical hazard to lower a site");
    SiteConditions steep = at;
    steep.slope_deg = 60.0;
    check::is_true(site_suitability(t, steep, false) < good, "a cliff to lower a site");
    SiteConditions distant = at;
    distant.freshwater_distance_m = t.water_reach * 20.0;
    check::is_true(site_suitability(t, distant, false) < good, "distance from water to lower a site");
    SiteConditions disliked = at;
    disliked.biome_opinion = -1.0;
    check::is_true(site_suitability(t, disliked, false) < good, "a disliked biome to lower a site");

    // The clamp is real: a catastrophic cell does not go negative and a perfect one
    // does not exceed one.
    SiteConditions awful = at;
    awful.flood = 1.0;
    awful.magic_hazard = 1.0;
    awful.slope_deg = 89.0;
    awful.biome_opinion = -1.0;
    awful.freshwater_distance_m = 1e9;
    check::equal(site_suitability(t, awful, false), 0.0, "the worst ground floors at zero");
}

TEST(different_peoples_prefer_different_ground) {
    // The weights are the whole point of having thirteen profiles rather than one. If
    // every people ranked ground identically, the registry would be decoration.
    SiteConditions cold;
    cold.freshwater_distance_m = 200.0;
    cold.slope_deg = 5.0;
    cold.temperature_c = -5.0;
    cold.moisture = 0.5;
    SiteConditions warm = cold;
    warm.temperature_c = 25.0;

    int disagreements = 0;
    const auto profiles = civilization_profiles();
    for (const CivilizationProfile& a : profiles) {
        for (const CivilizationProfile& b : profiles) {
            if (a.id >= b.id) continue;
            const bool a_prefers_cold = site_suitability(a.traits, cold, false) >
                                        site_suitability(a.traits, warm, false);
            const bool b_prefers_cold = site_suitability(b.traits, cold, false) >
                                        site_suitability(b.traits, warm, false);
            if (a_prefers_cold != b_prefers_cold) ++disagreements;
        }
    }
    check::is_true(disagreements > 0,
                   "some pair of peoples to disagree about cold ground (" +
                       std::to_string(disagreements) + " pairs)");
}
