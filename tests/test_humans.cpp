// Stage 12, first half: the hinterland under a city, and the food year it feeds.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <span>
#include <string>

#include "fwg/civilizations.hpp"
#include "fwg/humans.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

World peopled(int size = 65, std::uint32_t seed = 42) {
    Request r;
    r.seed = seed;
    r.size = size;
    r.phase = 11;
    return generate(resolve(r));
}

HumanHinterland hinterland(const World& world, std::vector<int> wars = {}) {
    HumansRequest request;
    request.world = &world;
    request.war_history = std::move(wars);
    return add_humans(request);
}

// The 11.15 km world the rural constants were authored for. A shipped world's cells are
// kilometres across, so the 250-unit catchment reach never leaves a hamlet's own node
// there and several rules inside the delivery loop have nothing to act on. At this width
// a catchment spans a couple of cells and those rules are live.
World reference_width() {
    Request r;
    r.seed = 42;
    r.size = 129;
    r.phase = 11;
    r.circumference_km = 11.15;
    // The default 1667 m relief budget needs a world at least 90 km round, so a world this
    // narrow has to be told to be flatter than any shipped preset is.
    r.relief_m = 200.0;
    return generate(resolve(r));
}


// A two-city world with a known surplus and a known shortage, so the trade rules can be
// read off the numbers rather than inferred from a generated world.
std::vector<CityCore> two_cities(double rich_supply, double poor_demand, double poor_materials) {
    std::vector<CityCore> cores(2);
    cores[0].site_id = 0;
    cores[0].food_supply = rich_supply;
    cores[0].food_demand = 10.0;
    cores[0].material_supply = 1000.0;
    cores[1].site_id = 1;
    cores[1].food_supply = 0.0;
    cores[1].food_demand = poor_demand;
    cores[1].material_supply = poor_materials;
    return cores;
}

std::vector<Road> one_road(double cost) {
    Road road;
    road.from = 0;
    road.to = 1;
    road.cost = cost;
    road.length_m = cost;
    return {road};
}

}  // namespace

// --- cultures --------------------------------------------------------------

TEST(a_culture_group_is_named_by_its_lowest_member_whatever_order_the_links_arrive_in) {
    // The canonical id has to be the lowest member index, because a caller that adds an
    // unrelated link must not be able to rename a group that already existed.
    const std::vector<CultureLink> links = {{3, 1, 10.0}, {5, 3, 10.0}, {4, 2, 10.0}};
    const std::vector<int> groups = culture_groups(6, links, 100.0);
    check::equal(groups[1], 1, "1, 3 and 5 to answer to 1");
    check::equal(groups[3], 1, "3 to answer to 1");
    check::equal(groups[5], 1, "5 to answer to 1");
    check::equal(groups[2], 2, "2 and 4 to answer to 2");
    check::equal(groups[4], 2, "4 to answer to 2");
    check::equal(groups[0], 0, "0 to be alone");

    // Same links, every permutation: the answer cannot depend on the order.
    std::vector<CultureLink> shuffled = links;
    std::sort(shuffled.begin(), shuffled.end(),
              [](const CultureLink& a, const CultureLink& b) { return a.from > b.from; });
    check::is_true(culture_groups(6, shuffled, 100.0) == groups, "link order not to matter");
    std::reverse(shuffled.begin(), shuffled.end());
    check::is_true(culture_groups(6, shuffled, 100.0) == groups, "link order still not to matter");
}

TEST(a_link_above_the_threshold_joins_nothing) {
    // The threshold is the whole of what makes this single-link rather than a road map.
    const std::vector<CultureLink> links = {{0, 1, 50.0}, {1, 2, 5000.0}};
    const std::vector<int> joined = culture_groups(3, links, 100.0);
    check::equal(joined[0], 0, "the cheap link to merge");
    check::equal(joined[1], 0, "the cheap link to merge both ways");
    check::equal(joined[2], 2, "the expensive link to be refused");

    const std::vector<int> none = culture_groups(3, links, 0.0);
    for (int i = 0; i < 3; ++i) check::equal(none[static_cast<std::size_t>(i)], i, "nobody to merge at zero");
}

TEST(only_one_people_ever_shares_a_culture) {
    // Two peoples sharing a road never share a culture: the link list is filtered on the
    // profile before the union-find ever sees it.
    const World w = peopled();
    const HumanHinterland h = hinterland(w);
    check::is_true(!h.cultures.empty(), "cultures, or this proves nothing");

    std::size_t members = 0;
    for (const CultureGroup& culture : h.cultures) {
        check::is_true(!culture.city_ids.empty(), "a culture to have members");
        check::equal(culture.index, culture.city_ids.front(), "the root to be the lowest member");
        for (const int city : culture.city_ids) {
            ++members;
            check::equal(w.settlements.sites[static_cast<std::size_t>(city)].population_profile,
                         culture.population_profile, "every member to share the culture's people");
        }
        check::is_true(culture.id.rfind(culture.population_profile + "-", 0) == 0,
                       "a culture id to be prefixed by its people");
        check::equal(culture.id.size(), culture.population_profile.size() + 9,
                     "a culture id to carry eight hex digits");
    }
    check::equal(members, w.settlements.sites.size(), "every city to belong to exactly one culture");
}

// --- hamlets and fortresses ------------------------------------------------

TEST(a_world_without_roads_gets_no_hinterland) {
    // The pass refuses on the phase gate and on a missing road network, and a refusal is
    // an empty block rather than a partial one.
    const World w = peopled();
    check::is_true(!hinterland(w).cores.empty(), "a stage 11 world to get cores");

    World early = w;
    early.effective.phase = 8;
    check::is_true(hinterland(early).cores.empty(), "phase 8 to refuse");

    World roadless = w;
    roadless.has_roads = false;
    check::is_true(hinterland(roadless).cores.empty(), "a roadless world to refuse");
}

TEST(a_world_of_one_city_still_gets_a_hinterland_and_asks_for_no_forts) {
    // A world with nothing to connect is a world the generator can actually produce, and
    // every count this pass derives from the road network is zero there. The rural layer
    // is not: one city still holds ground.
    Request r;
    r.seed = 42;
    r.size = 33;
    r.phase = 11;
    r.settlement_count = 1;
    const World lonely = generate(resolve(r));
    check::equal(lonely.settlements.sites.size(), std::size_t{1}, "one city");
    check::equal(lonely.roads.routes.size(), std::size_t{0}, "and no roads");

    const HumanHinterland h = hinterland(lonely);
    check::equal(h.cores.size(), std::size_t{1}, "one core");
    check::equal(h.cultures.size(), std::size_t{1}, "one culture, of one member");
    check::equal(h.cultures.front().city_ids.size(), std::size_t{1}, "and it holds that city");
    check::is_true(!h.hamlets.empty(), "the city still to hold rural ground");
    check::equal(h.fortress_demand.proposed, 0, "no road to defend");
    check::equal(h.fortresses.size(), std::size_t{0}, "and therefore no fort");
    check::equal(h.shipments.size(), std::size_t{0}, "and nobody to trade with");
    check::equal(h.version, 9, "the block version the age advance checks for");

    // And the food year still runs, with no routes to run over.
    SeasonalFoodRequest request;
    request.world = &lonely;
    request.humans = &h;
    const SeasonalFood food = add_seasonal_food(request);
    check::equal(food.models.size(), std::size_t{1}, "one model");
    check::equal(food.routes.size(), std::size_t{0}, "no routes");
    check::equal(food.months.size(), std::size_t{48}, "and four years all the same");
}

TEST(a_rural_id_is_an_ordinal_and_a_uid_names_the_node) {
    // The planners join on `id`, which renumbers every rebuild; everything that has to
    // survive an age boundary keys on `uid`, which names ground that does not move.
    const World w = peopled();
    const HumanHinterland h = hinterland(w);
    check::is_true(!h.hamlets.empty(), "hamlets, or this proves nothing");
    check::is_true(!h.fortresses.empty(), "fortresses, or this proves nothing");

    for (std::size_t k = 0; k < h.hamlets.size(); ++k) {
        check::equal(h.hamlets[k].id, "hamlet-" + std::to_string(k), "a hamlet id to be its ordinal");
        check::equal(h.hamlets[k].uid, "hamlet-node-" + std::to_string(h.hamlets[k].node),
                     "a hamlet uid to name its node");
        check::equal(h.hamlets[k].kind, std::string("hamlet"), "a hamlet to know what it is");
    }
    for (std::size_t k = 0; k < h.fortresses.size(); ++k) {
        check::equal(h.fortresses[k].id, "fortress-" + std::to_string(k), "a fortress id to be its ordinal");
        check::equal(h.fortresses[k].uid, "fortress-node-" + std::to_string(h.fortresses[k].node),
                     "a fortress uid to name its node");
    }
}

TEST(no_two_settlements_ever_share_a_node) {
    // The separation test is the only thing standing between two settlements and one
    // place on the map, drawn as one icon on top of another.
    const World w = peopled();
    const HumanHinterland h = hinterland(w);
    std::set<int> seen;
    std::size_t total = 0;
    for (const SettlementSite& site : w.settlements.sites) {
        check::is_true(seen.insert(site.node).second, "a city to hold its node alone");
        ++total;
    }
    for (const RuralSite& x : h.hamlets) {
        check::is_true(seen.insert(x.node).second, x.id + " to hold its node alone");
        ++total;
    }
    for (const RuralSite& x : h.fortresses) {
        check::is_true(seen.insert(x.node).second, x.id + " to hold its node alone");
        ++total;
    }
    check::is_true(total > w.settlements.sites.size(), "rural sites beyond the cities themselves");
}

TEST(every_rural_site_can_walk_home_one_step_at_a_time) {
    // A path assembled from parent pointers can silently contain a jump. The first node
    // must be the site's own and the last its city's, and every pair between them a step.
    const World w = peopled();
    const HumanHinterland h = hinterland(w);
    const auto held = sphere_grid(w.effective.size, w.effective.globe_radius);

    std::size_t checked = 0;
    for (const std::vector<RuralSite>* list : {&h.hamlets, &h.fortresses}) {
        for (const RuralSite& x : *list) {
            check::is_true(x.core_id >= 0, x.id + " to have an owning city");
            check::is_true(!x.access_nodes.empty(), x.id + " to have a path home");
            check::equal(x.access_nodes.front(), x.node, x.id + " to start at its own node");
            check::equal(x.access_nodes.back(), w.settlements.sites[static_cast<std::size_t>(x.core_id)].node,
                         x.id + " to end at its city");
            for (std::size_t k = 0; k + 1 < x.access_nodes.size(); ++k) {
                bool adjacent = false;
                for (const SphereNeighbour& nb :
                     held->neighbours[static_cast<std::size_t>(x.access_nodes[k])]) {
                    if (nb.index == x.access_nodes[k + 1]) adjacent = true;
                }
                check::is_true(adjacent, "step " + std::to_string(k) + " of " + x.id + " to be one step");
            }
            check::is_true(std::isfinite(x.access_cost) && x.access_cost <= w.effective.support_reach,
                           x.id + " to stand inside the support reach");
            ++checked;
        }
    }
    check::is_true(checked > 0, "rural sites to check");
}

TEST(a_generated_hamlet_stands_off_its_citys_doorstep_on_workable_dry_ground) {
    const World w = peopled();
    const HumanHinterland h = hinterland(w);
    const auto held = sphere_grid(w.effective.size, w.effective.globe_radius);
    const Grid& water = w.layers.at("water_type");
    const Grid& slope = w.layers.at("slope");
    check::is_true(!h.hamlets.empty(), "hamlets, or this proves nothing");

    std::size_t resource_roles = 0;
    for (const RuralSite& x : h.hamlets) {
        const auto node = static_cast<std::size_t>(x.node);
        check::equal(water.at(held->point_x[node], held->point_z[node]), 0.0, x.id + " to be on dry land");
        const CivilizationProfile* p = civilization_profile(x.population_profile);
        check::is_true(p != nullptr, x.id + " to name a real people");
        check::is_true(slope.at(held->point_x[node], held->point_z[node]) < p->traits.work_slope_limit,
                       x.id + " to stand on ground its people will work");
        // 100 road-cost units: the authored floor that keeps a hamlet off the city node.
        check::is_true(x.access_cost >= 100.0, x.id + " to stand off the city's doorstep");
        check::is_true(x.role == "farming" || x.role == "resource", x.id + " to have a role");
        if (x.role == "resource") ++resource_roles;
    }
    // Roles cycle farming, farming, resource per city; with three hamlets allowed per
    // city a world with no quarry at all would mean the cycle never reached k == 2.
    check::is_true(resource_roles > 0, "some hamlets to be quarries rather than farms");
}

TEST(the_doorstep_floor_is_dead_armour_behind_the_spacing_that_outranks_it) {
    // The floor is 100 ROAD-COST units and is not scaled with world width. It can never
    // decide anything at ANY width: a road cost is at least the great-circle length of
    // the path it prices, so any node the 120 m separation admits already costs at least
    // 120 — more than the floor asks for. Deleting the floor changes no world. The
    // separation against the city's own node is what keeps a hamlet off the doorstep; the
    // floor only looks as though it does.
    //
    // Checked at BOTH the shipped width and the 11.15 km width the constants were
    // authored for, because "it never fires" is a claim about the rule, not about one
    // world.
    const auto every_hamlet_clears_the_spacing = [](const World& w, const std::string& what) {
        const HumanHinterland h = hinterland(w);
        check::is_true(!h.hamlets.empty(), what + " to have hamlets, or this proves nothing");
        for (const RuralSite& x : h.hamlets) {
            check::is_true(x.access_cost >= 120.0, what + " " + x.id + " to clear the 120 m spacing");
        }
    };

    const World shipped = peopled();
    every_hamlet_clears_the_spacing(shipped, "a shipped world");
    const auto wide_grid = sphere_grid(shipped.effective.size, shipped.effective.globe_radius);
    double widest_step = 1e300;
    for (const auto& neighbours : wide_grid->neighbours) {
        for (const SphereNeighbour& nb : neighbours) widest_step = std::min(widest_step, nb.distance);
    }
    check::is_true(widest_step > 120.0,
                   "a shipped world's shortest step to already clear both the floor and the spacing");

    const World small = reference_width();
    check::is_true(!small.settlements.sites.empty(), "the reference world to have cities");
    every_hamlet_clears_the_spacing(small, "the reference world");

    // Non-vacuity for the reference case: here a single step really is cheaper than the
    // floor, so the floor had something it could in principle have refused and the
    // spacing is what got there first.
    const auto narrow_grid = sphere_grid(small.effective.size, small.effective.globe_radius);
    double closest = 1e300;
    for (const auto& neighbours : narrow_grid->neighbours) {
        for (const SphereNeighbour& nb : neighbours) closest = std::min(closest, nb.distance);
    }
    check::is_true(closest < 100.0, "the reference world to have steps cheaper than the floor");
}

TEST(a_hamlet_only_works_ground_its_own_city_holds) {
    // The catchment search refuses an edge that crosses a city catchment, so every cell a
    // hamlet farms must carry that hamlet's own culture in the published region layer.
    const World w = peopled();
    const HumanHinterland h = hinterland(w);
    const auto held = sphere_grid(w.effective.size, w.effective.globe_radius);
    check::is_true(!h.hamlets.empty(), "hamlets, or this proves nothing");

    std::map<int, std::string> culture_of_root;
    for (const CultureGroup& culture : h.cultures) culture_of_root[culture.index] = culture.id;

    std::size_t cells = 0;
    for (std::size_t i = 0; i < held->node_count(); ++i) {
        const int x = held->point_x[i];
        const int z = held->point_z[i];
        const int k = static_cast<int>(h.hamlet_catchment.at(x, z));
        if (k < 0) continue;
        check::is_true(static_cast<std::size_t>(k) < h.hamlets.size(), "a catchment to name a real hamlet");
        const int root = static_cast<int>(h.culture_region.at(x, z));
        check::is_true(root >= 0, "a farmed cell to lie inside a catchment");
        check::equal(culture_of_root[root], h.hamlets[static_cast<std::size_t>(k)].culture_id,
                     "a farmed cell to belong to its hamlet's own culture");
        ++cells;
    }
    check::is_true(cells > 0, "some ground to actually be farmed");
}

TEST(what_the_hamlets_deliver_is_what_the_city_has) {
    // A core's supply is a sum over its own members and nothing else. If the two disagree
    // the trade below is spending food that no hamlet grew.
    const World w = peopled();
    const HumanHinterland h = hinterland(w);
    double supplied = 0.0;
    double delivered = 0.0;
    for (const CityCore& core : h.cores) supplied += core.food_supply;
    for (const RuralSite& x : h.hamlets) delivered += x.delivered_food;
    check::is_true(delivered > 0.0, "the world to grow something");
    check::close(supplied, delivered, delivered * 1e-9, "every delivered unit to reach a city");

    for (const CityCore& core : h.cores) {
        double own = 0.0;
        for (const RuralSite& x : h.hamlets) {
            if (x.core_id == core.site_id) own += x.delivered_food;
        }
        check::close(core.food_supply, own, std::max(own, 1.0) * 1e-9, "a core to count only its own");
        check::equal(core.hamlet_ids.size(),
                     static_cast<std::size_t>(std::count_if(h.hamlets.begin(), h.hamlets.end(),
                                                            [&core](const RuralSite& x) {
                                                                return x.core_id == core.site_id;
                                                            })),
                     "a core to list exactly its own hamlets");
    }
}

static void check_delivery_rebuilds(const World& w, const std::string& what) {
    // An independent recomputation of the delivery rule out of published data only: the
    // hundred-residents calibration, the cell area, the food and resource fields, the
    // distance decay on the hamlet's own access cost, the quarter rate a hamlet works land
    // outside its speciality at, and the refusal to farm the ground a city stands on. If
    // the block and the layers disagree, one of them describes a world the other did not
    // build.
    const HumanHinterland h = hinterland(w);
    const auto held = sphere_grid(w.effective.size, w.effective.globe_radius);
    const Grid& slope = w.layers.at("slope");
    const Grid& resource = w.layers.at("metal_richness");
    check::is_true(!h.hamlets.empty(), "hamlets, or this proves nothing");

    std::set<int> city_nodes;
    for (const SettlementSite& site : w.settlements.sites) city_nodes.insert(site.node);
    const std::span<const std::string_view> ids = civilization_ids();

    std::vector<double> food(h.hamlets.size(), 0.0);
    std::vector<double> materials(h.hamlets.size(), 0.0);
    std::vector<double> area_km2(h.hamlets.size(), 0.0);
    std::size_t cells = 0;
    for (std::size_t i = 0; i < held->node_count(); ++i) {
        const int x = held->point_x[i];
        const int z = held->point_z[i];
        const int k = static_cast<int>(h.hamlet_catchment.at(x, z));
        if (k < 0 || city_nodes.count(static_cast<int>(i)) != 0) continue;
        // A cell is worked by whoever OWNS it, so the slope gate reads that people's
        // limit rather than the hamlet's.
        const int entity = static_cast<int>(h.civilization_region.at(x, z));
        check::is_true(entity >= 0 && static_cast<std::size_t>(entity) < ids.size(),
                       "a farmed cell to name a real people");
        const CivilizationProfile* p = civilization_profile(ids[static_cast<std::size_t>(entity)]);
        check::is_true(p != nullptr, "and for that people to exist");
        if (slope.at(x, z) >= p->traits.work_slope_limit) continue;

        const RuralSite& hamlet = h.hamlets[static_cast<std::size_t>(k)];
        const double area = held->areas[i] / 1e6;
        const double delivery = std::exp(-hamlet.access_cost / w.effective.support_reach);
        const bool farming = hamlet.role == "farming";
        area_km2[static_cast<std::size_t>(k)] += area;
        food[static_cast<std::size_t>(k)] +=
            100.0 * area * h.food_potential.at(x, z) * delivery * (farming ? 1.0 : 0.25);
        materials[static_cast<std::size_t>(k)] +=
            100.0 * area * resource.at(x, z) * delivery * (farming ? 0.25 : 1.0);
        ++cells;
    }
    check::is_true(cells > 0, what + " to actually work some ground");

    double total = 0.0;
    for (std::size_t k = 0; k < h.hamlets.size(); ++k) {
        const std::string who = what + " " + h.hamlets[k].id;
        check::close(h.hamlets[k].worked_area_km2, area_km2[k], std::max(area_km2[k], 1.0) * 1e-9,
                     who + " to work the area the catchment gave it");
        check::close(h.hamlets[k].delivered_food, food[k], std::max(food[k], 1.0) * 1e-9,
                     who + " to deliver the food its cells grew");
        check::close(h.hamlets[k].delivered_materials, materials[k], std::max(materials[k], 1.0) * 1e-9,
                     who + " to deliver the materials its cells hold");
        total += h.hamlets[k].delivered_food;
    }
    check::is_true(total > 0.0, what + " to have grown something");
}

TEST(what_a_hamlet_delivers_can_be_rebuilt_from_the_layers_it_published) {
    check_delivery_rebuilds(peopled(), "a shipped world");
    check_delivery_rebuilds(reference_width(), "the reference world");
}

TEST(the_catchment_reach_cannot_leave_a_hamlets_own_cell_at_any_shipped_width) {
    // The catchment reach is 250 ROAD-COST units and is not scaled with world width. At
    // the 11.15 km width it was authored for it spans a couple of cells, which is what
    // makes a hamlet the centre of a worked hinterland. At every width the generator
    // actually ships, one step away from the poles already costs more than 250, so a
    // hamlet's catchment is exactly the cell it stands on and the rest of the delivery
    // loop — the city-node refusal, the slope gate, the neighbour ownership test — has
    // nothing to act on. (Longitudinal steps DO shrink towards the poles, so the bound is
    // about where settlements actually land rather than about the grid everywhere.)
    //
    // This is a defect, recorded rather than fixed: the constant needs the reach factor
    // the support reach already gets.
    const World shipped = peopled();
    const HumanHinterland wide = hinterland(shipped);
    const auto wide_grid = sphere_grid(shipped.effective.size, shipped.effective.globe_radius);
    check::is_true(!wide.hamlets.empty(), "hamlets, or this proves nothing");
    for (const RuralSite& x : wide.hamlets) {
        const double own = wide_grid->areas[static_cast<std::size_t>(x.node)] / 1e6;
        check::close(x.worked_area_km2, own, own * 1e-9, x.id + " to work exactly the cell it stands on");
    }

    // At the authored width a hamlet reaches past its own doorstep, so the reach is a real
    // rule there and the assertion above is about world width rather than about the code.
    const World small = reference_width();
    const HumanHinterland narrow = hinterland(small);
    const auto narrow_grid = sphere_grid(small.effective.size, small.effective.globe_radius);
    check::is_true(!narrow.hamlets.empty(), "reference-width hamlets, or this proves nothing");
    std::size_t reaching = 0;
    for (const RuralSite& x : narrow.hamlets) {
        const double own = narrow_grid->areas[static_cast<std::size_t>(x.node)] / 1e6;
        if (x.worked_area_km2 > own * 1.5) ++reaching;
    }
    check::is_true(reaching > 0, "the authored width to give a hamlet more than one cell to work");
}

TEST(the_irrigated_field_never_falls_below_the_unaided_one) {
    // Irrigation can only ever ADD: it needs a freshwater source in reach and it only
    // helps ground that is too dry. A benefit layer that went negative anywhere would
    // mean the maintenance term had eaten into the natural yield.
    const World w = peopled();
    const HumanHinterland h = hinterland(w);
    const int n = w.effective.size;
    std::size_t checked = 0;
    double most = 0.0;
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            const double natural = h.natural_food_potential.at(x, z);
            const double irrigated = h.food_potential.at(x, z);
            check::is_true(natural >= 0.0, "no cell to grow negative food");
            check::is_true(irrigated >= natural - 1e-12, "irrigation never to reduce a yield");
            check::close(h.irrigation_benefit.at(x, z), irrigated - natural, 1e-12,
                         "the benefit layer to be the difference between the other two");
            most = std::max(most, irrigated);
            ++checked;
        }
    }
    check::is_true(checked == static_cast<std::size_t>(n) * static_cast<std::size_t>(n),
                   "every cell to have been read");
    check::is_true(most > 0.0, "some cell to grow something, or this proves nothing");
}

TEST(fortresses_are_taken_best_first_under_a_ceiling_the_world_never_exceeds) {
    const World w = peopled();
    const HumanHinterland h = hinterland(w);
    const FortressDemand& demand = h.fortress_demand;
    check::is_true(!h.fortresses.empty(), "fortresses, or this proves nothing");
    check::is_true(demand.proposed > 0, "the road network to ask for something");

    double road_length = 0.0;
    std::set<std::pair<int, int>> crossings;
    for (const Road& road : w.roads.routes) {
        road_length += road.length_m;
        for (const auto& [a, b] : road.river_crossings) crossings.emplace(std::min(a, b), std::max(a, b));
    }
    check::equal(demand.crossings, static_cast<int>(crossings.size()), "distinct crossings to be counted once");
    check::equal(demand.proposed,
                 static_cast<int>(std::floor(road_length / w.effective.support_reach)) +
                     static_cast<int>(crossings.size()) + demand.veteran_demand,
                 "the proposal to be road length, crossings and veterans");
    check::equal(demand.city_ceiling, static_cast<int>(w.settlements.sites.size()) + demand.veteran_demand,
                 "a people to raise no more forts than it has cities");
    check::equal(demand.limit, std::min({demand.requested_ceiling, demand.proposed, demand.city_ceiling}),
                 "the limit to be the strictest of the three");
    check::is_true(static_cast<int>(h.fortresses.size()) <= demand.limit, "the ceiling to actually bind");

    for (std::size_t k = 1; k < h.fortresses.size(); ++k) {
        check::is_true(h.fortresses[k].defence_score <= h.fortresses[k - 1].defence_score,
                       "forts to be taken best first");
    }
    std::set<int> route_nodes;
    for (const Road& road : w.roads.routes) {
        for (const int node : road.nodes) route_nodes.insert(node);
    }
    for (const RuralSite& fort : h.fortresses) {
        check::is_true(route_nodes.count(fort.protected_route_node) != 0,
                       fort.id + " to guard a node a road actually uses");
    }
}

TEST(wars_raise_what_the_world_asks_for_and_what_it_allows_but_only_three_deep) {
    // A city that has fought wants more watching its roads. The cap is what stops one
    // long war from buying a city an unbounded garrison.
    const World w = peopled();
    const HumanHinterland peace = hinterland(w);
    check::equal(peace.fortress_demand.veteran_demand, 0, "a world at peace to ask for no extra");
    check::equal(peace.fortress_demand.veteran_cities, 0, "and to have no veterans");

    std::vector<int> wars(w.settlements.sites.size(), 0);
    wars[0] = 2;
    wars[1] = 9;  // far beyond the cap
    const HumanHinterland fought = hinterland(w, wars);
    check::equal(fought.fortress_demand.veteran_demand, 5, "two wars plus a capped three");
    check::equal(fought.fortress_demand.veteran_cities, 2, "two cities to have fought");
    check::is_true(fought.fortress_demand.proposed > peace.fortress_demand.proposed,
                   "war to raise what the roads ask for");
    check::is_true(fought.fortress_demand.city_ceiling > peace.fortress_demand.city_ceiling,
                   "war to raise what the cities may raise");
    check::is_true(fought.fortresses.size() >= peace.fortresses.size(),
                   "war never to reduce the garrison");
}

// --- trade -----------------------------------------------------------------

TEST(trade_loses_food_to_distance_and_charges_the_buyer_for_it) {
    std::vector<CityCore> cores = two_cities(200.0, 60.0, 1000.0);
    const std::vector<FoodShipment> shipments = exchange_food(cores, one_road(2000.0));
    check::equal(shipments.size(), std::size_t{1}, "one shipment over the one road");

    const FoodShipment& s = shipments.front();
    check::equal(s.from, 0, "the rich city to sell");
    check::equal(s.to, 1, "the poor city to buy");
    check::is_true(s.sent > s.delivered, "distance to cost the trade something");
    check::close(s.lost, s.sent - s.delivered, 1e-12, "the loss to be what did not arrive");
    // exp(-2000/4000) is the authored transport decay; 0.2 + 2000/5000 the authored price.
    check::close(s.delivered / s.sent, std::exp(-0.5), 1e-12, "the decay to be the authored one");
    check::close(s.material_cost, s.delivered * 0.6, 1e-12, "the price to be the authored one");
    check::is_true(s.city_path.size() == 2 && s.city_path.front() == 0 && s.city_path.back() == 1,
                   "the path to run seller to buyer");

    check::close(cores[1].food_imports, s.delivered, 1e-12, "the buyer to receive what arrived");
    check::close(cores[0].food_exports, s.sent, 1e-12, "the seller to lose what left");
    check::close(cores[1].food_deficit, std::max(0.0, 60.0 - s.delivered), 1e-12,
                 "what the trade could not close to stay visible");
}

TEST(a_seller_cannot_sell_the_same_grain_twice) {
    // One city with a surplus of 40 and two neighbours each wanting 100. The second buyer
    // may only have what the first left.
    std::vector<CityCore> cores(3);
    for (int i = 0; i < 3; ++i) cores[static_cast<std::size_t>(i)].site_id = i;
    cores[0].food_supply = 50.0;
    cores[0].food_demand = 10.0;  // surplus 40
    cores[1].food_demand = 100.0;
    cores[1].material_supply = 10000.0;
    cores[2].food_demand = 100.0;
    cores[2].material_supply = 10000.0;

    std::vector<Road> roads = one_road(100.0);
    Road second;
    second.from = 0;
    second.to = 2;
    second.cost = 100.0;
    roads.push_back(second);

    const std::vector<FoodShipment> shipments = exchange_food(cores, roads);
    check::is_true(!shipments.empty(), "some trade to happen");
    check::is_true(cores[0].food_exports <= 40.0 + 1e-9, "the seller never to export past its surplus");
    double bought = 0.0;
    for (const FoodShipment& s : shipments) bought += s.delivered;
    check::is_true(bought <= 40.0 + 1e-9, "the world never to buy more than was grown");
}

TEST(a_buyer_with_no_materials_buys_nothing) {
    // The material budget is a real constraint, not decoration: trade is paid for.
    std::vector<CityCore> cores = two_cities(200.0, 60.0, 0.0);
    const std::vector<FoodShipment> shipments = exchange_food(cores, one_road(2000.0));
    check::equal(shipments.size(), std::size_t{0}, "no shipment a buyer cannot pay for");
    check::close(cores[1].food_deficit, 60.0, 1e-12, "the whole shortage to stay visible");

    // The same world with materials does trade, so the emptiness above is the budget and
    // not some other refusal.
    std::vector<CityCore> funded = two_cities(200.0, 60.0, 1000.0);
    check::is_true(!exchange_food(funded, one_road(2000.0)).empty(), "materials to unlock the trade");
}

TEST(a_generated_world_trades_without_inventing_food) {
    const World w = peopled();
    const HumanHinterland h = hinterland(w);
    check::is_true(!h.shipments.empty(), "some trade, or this proves nothing");
    double sent = 0.0;
    double delivered = 0.0;
    for (const FoodShipment& s : h.shipments) {
        check::is_true(s.delivered > 0.0, "a recorded shipment to have moved something");
        check::is_true(s.sent >= s.delivered, "nothing to arrive that did not leave");
        sent += s.sent;
        delivered += s.delivered;
    }
    check::is_true(sent > delivered, "the world to lose something in transit");
    for (const CityCore& core : h.cores) {
        check::is_true(core.trade_material_cost <= core.material_supply + 1e-9,
                       "a city never to spend materials it does not have");
        check::is_true(core.food_deficit >= 0.0, "a deficit never to go negative");
    }
}

// --- the seasonal year -----------------------------------------------------

TEST(an_equatorial_year_has_no_seasons_and_delivers_exactly_the_annual_estimate) {
    // At the equator the amplitude vanishes, every month is the mean, and growth is
    // exactly one -- so the twelve deliveries must add back up to the annual figure.
    const std::array<double, 12> year = seasonal_harvest(120.0, 0.5, 0.0, 22.0, 24.0, 18.0, 1.0);
    double total = 0.0;
    for (const double month : year) {
        check::close(month, 10.0, 1e-9, "every month to carry a twelfth");
        total += month;
    }
    check::close(total, 120.0, 1e-9, "the year to deliver the annual estimate");
}

TEST(the_harvest_lags_a_month_and_reverses_across_the_equator) {
    // A place whose WHOLE year stays below the ideal, so growth rises monotonically with
    // temperature and the best growing month is unambiguously the warmest one. Northern
    // midsummer is index 6 of the growth curve, so the HARVEST peaks at index 7; the
    // southern mirror is warmest at index 0 and so peaks at index 1.
    const auto year_at = [](double latitude) {
        return seasonal_harvest(100.0, 0.85, latitude, 14.0, 30.0, 28.0, 1.0);
    };
    const auto peak_of = [&year_at](double latitude) {
        const std::array<double, 12> year = year_at(latitude);
        std::size_t best = 0;
        for (std::size_t m = 1; m < 12; ++m) {
            if (year[m] > year[best]) best = m;
        }
        return best;
    };
    check::equal(peak_of(50.0), std::size_t{7}, "a northern harvest to land one month after midsummer");
    check::equal(peak_of(-50.0), std::size_t{1}, "a southern harvest to land half a year away");

    // And the swing is real rather than a rounding artefact.
    const std::array<double, 12> north = year_at(50.0);
    check::is_true(north[7] > north[1] * 1.5, "the year to actually have a lean half");

    // Latitude is the only thing that reversed: the two hemispheres are the same year
    // read from different months.
    const std::array<double, 12> south = year_at(-50.0);
    for (std::size_t m = 0; m < 12; ++m) {
        check::close(north[(m + 6) % 12], south[m], 1e-9, "the southern year to be the northern one, moved");
    }
}

TEST(the_simulator_starts_empty_and_conserves_every_unit) {
    std::vector<FoodModel> cities(2);
    cities[0].site_id = 0;
    cities[0].harvest.fill(30.0);
    cities[0].demand = 10.0;
    cities[0].storage = 20.0;
    cities[0].spoilage = 0.02;
    cities[0].transport_budget = 100.0;
    cities[1].site_id = 1;
    cities[1].demand = 10.0;
    cities[1].storage = 20.0;
    cities[1].spoilage = 0.02;
    cities[1].transport_budget = 100.0;

    FoodRoute route;
    route.from = 0;
    route.to = 1;
    route.capacity.fill(50.0);
    route.efficiency = 0.8;
    route.price = 0.5;

    const FoodRun run = simulate_food(cities, {route}, 4);
    check::equal(run.months.size(), std::size_t{48}, "four years of months");
    check::equal(run.months.front().opening, 0.0, "stores to start empty");

    double moved = 0.0;
    for (const FoodMonth& m : run.months) {
        // opening - spoiled + harvest - consumed - overflow - transit loss = closing.
        check::close(m.closing,
                     m.opening - m.spoiled + m.harvest - m.consumed - m.overflow - m.transit_loss, 1e-9,
                     "month " + std::to_string(m.month) + " to conserve its food");
        for (const FoodMonthShipment& s : m.shipments) moved += s.delivered;
        for (const FoodMonthCity& c : m.cities) {
            check::is_true(c.closing >= -1e-12, "no city to hold negative stock");
            check::is_true(c.consumed <= c.demand + 1e-12, "nobody to eat more than they need");
            check::close(c.shortage, std::max(0.0, c.demand - c.consumed), 1e-12,
                         "shortage to be what was not eaten");
        }
    }
    check::is_true(moved > 0.0, "the route to have carried something");
}

TEST(a_route_carries_no_more_in_a_month_than_it_holds_and_carries_it_either_way) {
    // The monthly capacity is one quantity, and it binds. Both directions of a route are
    // tried, but only one can ever fire in a month: a city with anything exportable is by
    // definition holding more than its own demand, so it never needs an import the same
    // month. The second half of this test is what proves the reverse direction is
    // evaluated at all.
    const auto run_pair = [](double harvest_a, double harvest_b) {
        std::vector<FoodModel> cities(2);
        for (std::size_t i = 0; i < 2; ++i) {
            cities[i].site_id = static_cast<int>(i);
            cities[i].demand = 40.0;
            cities[i].storage = 5.0;
            cities[i].spoilage = 0.0;
            cities[i].transport_budget = 1e9;
        }
        cities[0].harvest.fill(harvest_a);
        cities[1].harvest.fill(harvest_b);
        FoodRoute route;
        route.from = 0;  // the route is always written 0 -> 1
        route.to = 1;
        route.capacity.fill(7.0);
        route.efficiency = 1.0;
        route.price = 0.5;
        return simulate_food(cities, {route}, 1);
    };

    const FoodRun forward = run_pair(300.0, 0.0);
    bool bound = false;
    for (const FoodMonth& m : forward.months) {
        double sent = 0.0;
        for (const FoodMonthShipment& s : m.shipments) {
            sent += s.sent;
            check::equal(s.from, 0, "the rich city to be the seller");
            check::equal(s.to, 1, "and the poor one the buyer");
        }
        check::is_true(sent <= 7.0 + 1e-12, "a month never to move more than the road holds");
        if (sent > 7.0 - 1e-12) bound = true;
    }
    check::is_true(bound, "the capacity to actually bind, or the ceiling proves nothing");

    // Same route record, roles reversed: the food has to flow the other way down it.
    const FoodRun backward = run_pair(0.0, 300.0);
    bool reversed = false;
    for (const FoodMonth& m : backward.months) {
        for (const FoodMonthShipment& s : m.shipments) {
            check::equal(s.from, 1, "a route written 0 to 1 to sell from 1 when 1 is the rich one");
            check::equal(s.to, 0, "and to deliver to 0");
            reversed = true;
        }
    }
    check::is_true(reversed, "the reverse direction to have carried something");
}

TEST(a_city_eats_before_its_store_overflows) {
    std::vector<FoodModel> cities(1);
    cities[0].site_id = 0;
    cities[0].harvest.fill(100.0);
    cities[0].demand = 10.0;
    cities[0].storage = 20.0;
    cities[0].spoilage = 0.0;

    const FoodRun run = simulate_food(cities, {}, 1);
    const FoodMonthCity& first = run.months.front().cities.front();
    check::close(first.consumed, 10.0, 1e-12, "the city to eat its fill");
    check::close(first.overflow, 70.0, 1e-12, "the rest above the store to spill");
    check::close(first.closing, 20.0, 1e-12, "the store to end full");
    check::equal(first.shortage, 0.0, "and nobody to go hungry beside a full store");
}

TEST(a_generated_world_gets_a_food_year_it_can_be_judged_by) {
    const World w = peopled();
    const HumanHinterland h = hinterland(w);
    SeasonalFoodRequest request;
    request.world = &w;
    request.humans = &h;
    const SeasonalFood food = add_seasonal_food(request);

    check::equal(food.models.size(), w.settlements.sites.size(), "one model per city");
    check::equal(food.routes.size(), w.roads.routes.size(), "one route per road, positionally aligned");
    check::equal(food.months.size(), std::size_t{48}, "four years reported");
    check::equal(food.cities.size(), w.settlements.sites.size(), "one summary per city");

    std::size_t growing = 0;
    for (const FoodModel& model : food.models) {
        double year = 0.0;
        for (const double month : model.harvest) {
            check::is_true(month >= 0.0, "no negative harvest");
            year += month;
        }
        if (year > 0.0) ++growing;
        check::between(model.spoilage, 0.005, 0.08, "spoilage inside its authored band");
        check::is_true(model.storage >= 0.0, "storage never negative");
    }
    check::is_true(growing > 0, "some city to actually grow food");

    for (std::size_t i = 0; i < food.routes.size(); ++i) {
        check::equal(food.routes[i].from, w.roads.routes[i].from, "a route to name its road's cities");
        check::equal(food.routes[i].to, w.roads.routes[i].to, "a route to name its road's cities");
        check::between(food.routes[i].efficiency, 0.0, 1.0, "efficiency inside zero and one");
    }
    for (const SeasonalCity& city : food.cities) {
        check::between(city.food_coverage, 0.0, 1.0, "coverage inside zero and one");
        for (const int month : city.lean_months) check::between(month, 1.0, 12.0, "a lean month inside a year");
        check::is_true(city.annual_shortage >= 0.0, "shortage never negative");
    }
}
