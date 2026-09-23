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

// --- the city ceiling -----------------------------------------------------

TEST(the_raster_binds_the_ceiling_when_cells_are_coarser) {
    // The measured defect. A 200 km world on a size-17 grid has 12.5 km cells against
    // an 8 km settlement spacing; honouring the spacing alone admitted about 63 cities
    // onto 289 nodes. This is the test that would have caught it.
    const double r = 200000.0 / (2.0 * std::acos(-1.0));
    const double cell = 2.0 * std::acos(-1.0) * r / (17 - 1);
    check::between(cell, 12400.0, 12600.0, "a size-17 200 km world to have 12.5 km cells");

    const double habitable_km2 = 20000.0;
    const CityCeiling bound = city_ceiling(habitable_km2, 8000.0, cell);
    const CityCeiling unbound = city_ceiling(habitable_km2, 8000.0, 0.0);

    check::equal(bound.resolvable_spacing_m, cell, "the coarser spacing to bind");
    check::is_true(bound.packing < unbound.packing / 2,
                   "the raster to more than halve the ceiling (" + std::to_string(bound.packing) +
                       " against " + std::to_string(unbound.packing) + ")");
}

TEST(the_authored_spacing_binds_when_it_is_the_coarser) {
    // The other side of the max: a fine grid does not let cities crowd past the spacing.
    const CityCeiling fine = city_ceiling(20000.0, 8000.0, 87.0);
    check::equal(fine.resolvable_spacing_m, 8000.0, "the authored spacing to bind on a fine grid");
}

TEST(the_ceiling_packs_hexagonally_not_squarely) {
    // sqrt(3)/2 per unit square, the densest packing of the plane. A square packing
    // would reserve a full spacing^2 per city and undercount what the ground holds.
    const CityCeiling c = city_ceiling(1000.0, 1000.0, 0.0);
    check::close(c.per_city_km2, std::sqrt(3.0) / 2.0, 1e-12, "a 1 km spacing to reserve sqrt(3)/2 km2");
    check::equal(c.packing, 1154LL, "the packing over 1000 km2");
    check::is_true(c.per_city_km2 < 1.0, "hexagonal packing to be denser than square");
}

TEST(a_zero_spacing_admits_no_cities_rather_than_dividing_by_zero) {
    check::equal(city_ceiling(1000.0, 0.0, 0.0).packing, 0LL, "no cities at zero spacing");
}

TEST(the_ceiling_takes_from_the_largest_quota_first) {
    std::map<std::string, long long> quotas{{"dwarf", 2}, {"elf", 7}, {"human", 4}};
    apply_city_ceiling(quotas, 9);
    check::equal(quotas["dwarf"] + quotas["elf"] + quotas["human"], 9LL, "the total to reach the limit");
    check::equal(quotas["dwarf"], 2LL, "the smallest quota to be untouched");
    // elf gives up three to fall level with human, then the tie breaks to the later
    // name and human gives up the fourth.
    check::equal(quotas["elf"], 4LL, "the largest to give up three");
    check::equal(quotas["human"], 3LL, "the middle to give up one once it ties");
}

TEST(a_tied_quota_loses_to_the_later_name) {
    // A tie has to break on something stable. Taking from the later name is arbitrary
    // but it is not map order, which is what matters: the same world twice reduces the
    // same way.
    std::map<std::string, long long> quotas{{"alder", 3}, {"birch", 3}};
    apply_city_ceiling(quotas, 5);
    check::equal(quotas["alder"], 3LL, "the earlier name to keep its quota");
    check::equal(quotas["birch"], 2LL, "the later name to give one up");
}

TEST(a_quota_already_under_the_ceiling_is_untouched) {
    std::map<std::string, long long> quotas{{"dwarf", 2}, {"elf", 1}};
    apply_city_ceiling(quotas, 100);
    check::equal(quotas["dwarf"], 2LL, "a quota under the ceiling to survive");
    check::equal(quotas["elf"], 1LL, "a quota under the ceiling to survive");
}

TEST(a_zero_ceiling_empties_every_quota) {
    std::map<std::string, long long> quotas{{"dwarf", 5}, {"elf", 3}};
    apply_city_ceiling(quotas, 0);
    check::equal(quotas["dwarf"] + quotas["elf"], 0LL, "an empty world to have no cities");
}

// --- which ceiling binds --------------------------------------------------

TEST(asking_for_no_cities_outranks_the_ground) {
    Config cfg;
    cfg.auto_parameters = 0;
    cfg.settlement_count = 0;
    check::equal(city_limit(500, cfg, 24), 0LL, "a caller who wants an empty world to get one");
}

TEST(a_world_that_infers_is_ceilinged_by_its_ground) {
    // The defect this guards: a recipe world silently keeping the default 24 when its
    // ground would hold far more.
    Config cfg;  // recipe 3, settlement_count at the recipe default
    check::is_true(cfg.infers_own_settings(), "a recipe world to infer its own settings");
    check::equal(city_limit(500, cfg, 24), 500LL, "the ground alone to bind an inferring world");
}

TEST(lowering_the_requested_count_is_honoured_even_when_inferring) {
    // settlement_count is published as "Maximum surface cities". A request for two that
    // returns ten is a broken parameter, not a ground rule.
    Config cfg;
    cfg.settlement_count = 2;
    check::equal(city_limit(500, cfg, 24), 2LL, "a lowered request to bind");
}

TEST(raising_the_requested_count_does_not_lift_the_ground) {
    Config cfg;
    cfg.settlement_count = 900;
    check::equal(city_limit(500, cfg, 24), 500LL, "the ground to bind a raised request");
}

TEST(a_world_that_does_not_infer_takes_the_smaller_of_the_two) {
    Config cfg;
    cfg.world_recipe = 0;
    cfg.phase = 9;
    cfg.population_profile = std::string(civilization_ids().front());
    check::is_true(!cfg.infers_own_settings(), "a bare experiment with one people not to infer");
    check::equal(city_limit(500, cfg, 24), 24LL, "the request to bind when nothing is inferred");
    check::equal(city_limit(10, cfg, 24), 10LL, "the ground to bind when it is the smaller");
}

// --- the recipe fields ----------------------------------------------------

TEST(a_geometry_experiment_cannot_reach_the_civilization_stages) {
    Config cfg;
    cfg.world_recipe = 0;
    cfg.phase = 16;
    check::throws([&] { cfg.validate(); }, "recipe 0 at phase 16");
}

TEST(a_recipe_world_refuses_to_also_infer_its_parameters) {
    // Both would claim the same field, and the world could not say which chose it.
    Config cfg;
    cfg.world_recipe = 3;
    cfg.auto_parameters = 1;
    check::throws([&] { cfg.validate(); }, "recipe 3 with auto_parameters");
}

TEST(a_retired_recipe_is_refused_rather_than_reinterpreted) {
    Config cfg;
    cfg.world_recipe = 2;
    check::throws([&] { cfg.validate(); }, "a retired recipe version");
}

TEST(an_unknown_population_profile_is_refused) {
    Config cfg;
    cfg.population_profile = "wyrmkin";
    check::throws([&] { cfg.validate(); }, "a people the registry does not have");
    cfg.population_profile = "mixed";
    cfg.validate();  // mixed is not a registry id but is always legal
}

// --- population and classification ----------------------------------------

TEST(the_allowance_is_split_evenly_with_the_remainder_going_to_the_oldest) {
    // 10 residents across 3 cities: 4, 3, 3. The odd one goes to the city founded first.
    std::vector<long long> got;
    for (long long i = 0; i < 3; ++i) got.push_back(city_population(10, 3, i).residents);
    check::equal(got[0], 4LL, "the oldest city to carry the odd resident");
    check::equal(got[1], 3LL, "the second city to take its even share");
    check::equal(got[2], 3LL, "the third city to take its even share");
    check::equal(got[0] + got[1] + got[2], 10LL, "the whole allowance to be spent");
}

TEST(no_resident_is_lost_or_invented_by_the_split) {
    for (long long allowance : {0LL, 1LL, 7LL, 100LL, 99991LL}) {
        for (long long count = 1; count <= 9; ++count) {
            long long total = 0;
            for (long long i = 0; i < count; ++i) total += city_population(allowance, count, i).residents;
            check::equal(total, allowance,
                         "the split of " + std::to_string(allowance) + " across " +
                             std::to_string(count) + " cities to be exact");
        }
    }
}

TEST(the_urban_split_rounds_half_to_even) {
    // 30 residents give exactly 16.5 urban. Rounding half away from zero would say 17;
    // the rule is half-to-even, and the two disagree on about one city in eighty five.
    const CityPopulation at_thirty = city_population(30, 1, 0);
    check::equal(at_thirty.residents, 30LL, "thirty residents");
    check::equal(at_thirty.urban, 16LL, "16.5 urban to round down to the even 16");

    const CityPopulation at_seventy = city_population(70, 1, 0);
    check::equal(at_seventy.urban, 38LL, "38.5 urban to round down to the even 38");

    // And an ordinary case, to show the rule is not just always-down.
    const CityPopulation at_ten = city_population(10, 1, 0);
    check::equal(at_ten.urban, 6LL, "5.5000000000000004 urban to round up to 6");
}

TEST(urban_and_rural_always_account_for_everyone) {
    for (long long n = 0; n < 500; ++n) {
        const CityPopulation p = city_population(n, 1, 0);
        check::equal(p.urban + p.rural, p.residents,
                     "the two halves of " + std::to_string(n) + " to be the whole");
        check::is_true(p.rural >= 0, "no negative countryside at " + std::to_string(n));
    }
}

TEST(refugees_arrive_after_the_split_not_inside_it) {
    // The capacity split is derived from the ground this city farms, and refugees walked
    // in from a town that fell. Adding them inside the split would silently take the
    // allowance away from this people's other cities.
    const CityPopulation without = city_population(90, 3, 1);
    const CityPopulation with = city_population(90, 3, 1, 25);
    check::equal(with.residents, without.residents + 25, "the arrivals to be added on top");

    long long total = 0;
    for (long long i = 0; i < 3; ++i) total += city_population(90, 3, i, i == 1 ? 25 : 0).residents;
    check::equal(total, 115LL, "the other cities' shares to be untouched");
}

TEST(a_founding_capital_stays_the_capital_even_when_outscored) {
    // A people's first city does not stop being its capital because better ground was
    // settled afterwards.
    const std::vector<CityClassInput> sites{
        {5, "elf", 0.40, true},
        {9, "elf", 0.95, false},
    };
    const auto classes = classify_cities(sites, 0.65);
    check::is_true(classes[0] == CityClass::Capital, "the founding capital to be retained");
    check::is_true(classes[1] == CityClass::Medium, "the better-scoring city to be merely medium");
}

TEST(without_a_founding_capital_the_best_ground_takes_it) {
    const std::vector<CityClassInput> sites{
        {5, "elf", 0.40, false},
        {9, "elf", 0.95, false},
        {2, "elf", 0.20, false},
    };
    const auto classes = classify_cities(sites, 0.65);
    check::is_true(classes[1] == CityClass::Capital, "the best-scoring city to be the capital");
    check::is_true(classes[0] == CityClass::Small, "a low-scoring city to be small");
    check::is_true(classes[2] == CityClass::Small, "a low-scoring city to be small");
}

TEST(a_tied_capital_is_decided_by_the_node_not_by_listing_order) {
    const std::vector<CityClassInput> forward{{9, "elf", 0.5, false}, {2, "elf", 0.5, false}};
    const std::vector<CityClassInput> backward{{2, "elf", 0.5, false}, {9, "elf", 0.5, false}};
    const auto a = classify_cities(forward, 0.65);
    const auto b = classify_cities(backward, 0.65);
    check::is_true(a[1] == CityClass::Capital, "the lower node to win the tie");
    check::is_true(b[0] == CityClass::Capital, "the lower node to win the tie either way round");
}

TEST(every_people_gets_its_own_capital) {
    const std::vector<CityClassInput> sites{
        {1, "elf", 0.9, false}, {2, "elf", 0.2, false},
        {3, "dwarf", 0.3, false}, {4, "dwarf", 0.1, false},
    };
    const auto classes = classify_cities(sites, 0.65);
    check::is_true(classes[0] == CityClass::Capital, "the elves to have a capital");
    check::is_true(classes[2] == CityClass::Capital, "the dwarves to have their own capital");
    // The dwarven capital scores below the medium threshold and is a capital anyway:
    // classification is relative to a people, not to the world.
    check::is_true(sites[2].suitability < 0.65, "the dwarven capital to be on poor ground");
}
