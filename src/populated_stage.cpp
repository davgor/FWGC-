// Stage 12, "Populated regions": the pass that ties the hinterland, the seasons and the
// society passes together and writes the result into the world.
//
// The three passes below all return values rather than editing the world, deliberately,
// so that each stays a function of its inputs. This is the one place that turns those
// values into a world, which means it is also the one place that has to get the order and
// the mutations right.
#include "fwg/populated_stage.hpp"

#include <string>
#include <utility>

#include "fwg/humans.hpp"
#include "fwg/society.hpp"
#include "fwg/world.hpp"

namespace fwg {

void add_populated_regions(World& world) {
    if (!world.has_roads) return;

    HumansRequest humans_request;
    humans_request.world = &world;
    // No war history: nothing has fought yet, because the age transitions that produce
    // wars are not ported. Empty is the honest input, not a placeholder -- it means a
    // world that has never been to war, which keeps exactly the road-and-city answer.
    HumanHinterland hinterland = add_humans(humans_request);
    if (hinterland.cores.empty()) return;

    SeasonalFoodRequest seasonal_request;
    seasonal_request.world = &world;
    seasonal_request.humans = &hinterland;
    SeasonalFood seasonal = add_seasonal_food(seasonal_request);

    SocietyRequest society_request;
    society_request.world = &world;
    society_request.humans = &hinterland;
    society_request.seasonal = &seasonal;
    // The suitability grid the witch huts are judged against. Left null it falls back to
    // a ZERO field, and a zero field admits every cell rather than refusing every cell --
    // so huts would stop being sited on poor ground and start being sited anywhere.
    const std::string primary = "suitability_" + world.effective.population_profile;
    if (const auto it = world.layers.find(primary); it != world.layers.end()) {
        society_request.suitability = &it->second;
    } else if (!world.settlements.sites.empty()) {
        // A mixed world has no single primary people. Judge against the people who
        // actually hold the most ground, which is the nearest honest answer.
        std::map<std::string, int> counts;
        for (const SettlementSite& s : world.settlements.sites) ++counts[s.population_profile];
        const std::string* best = nullptr;
        int most = 0;
        for (const auto& [people, count] : counts) {
            if (count > most) {
                most = count;
                best = &people;
            }
        }
        if (best != nullptr) {
            const auto found = world.layers.find("suitability_" + *best);
            if (found != world.layers.end()) society_request.suitability = &found->second;
        }
    }
    for (const SettlementSite& s : world.settlements.sites) society_request.site_names.push_back(s.name);

    SocietyReport society = add_world_society(society_request);

    // --- the layers both passes produce ------------------------------------
    world.layers["food_potential"] = hinterland.food_potential;
    world.layers["natural_food_potential"] = hinterland.natural_food_potential;
    world.layers["irrigation_benefit"] = hinterland.irrigation_benefit;
    world.layers["culture_region"] = hinterland.culture_region;
    world.layers["civilization_region"] = hinterland.civilization_region;
    world.layers["hamlet_catchment"] = hinterland.hamlet_catchment;
    world.layers["fishing_ground_owner"] = society.fishing_ground_owner;

    // --- the mutations society returns rather than performing ---------------
    // It returns them precisely because applying them twice is the reference's own bug:
    // a second run appends the ports to the hamlet list again. Doing it here, once,
    // where the order is visible, is what keeps that from being a hidden hazard.
    for (std::size_t k = 0; k < society.fisheries.ports.size(); ++k) {
        const auto& port = society.fisheries.ports[k];
        if (port.core_id >= 0 && static_cast<std::size_t>(port.core_id) < hinterland.cores.size()) {
            hinterland.cores[static_cast<std::size_t>(port.core_id)].hamlet_ids.push_back(port.id);
        }

        // And the port itself joins the rural layer, as the reference does, so that
        // everything downstream which walks "the hamlets" sees harbours too. A harbour
        // is a kind of hamlet to every consumer except the pass that sites it.
        RuralSite row;
        row.id = port.id;
        // The uid convention is one spelling, not three: "<kind>-node-<node>". The
        // harbour's own id carries its city and a running count and renumbers with them;
        // this does not, because the node does not move.
        row.uid = "hamlet-node-" + std::to_string(port.node);
        row.kind = port.kind;
        row.node = port.node;
        row.x = port.x;
        row.z = port.z;
        row.core_id = port.core_id;
        row.population_profile = port.population_profile;
        row.culture_id = port.culture_id;
        row.height_m = port.height_m;
        row.role = port.role;
        row.reason = "Harbour and fishing ground within the city's support reach.";
        hinterland.hamlets.push_back(std::move(row));
    }

    for (std::size_t k = 0; k < society.community_traits.size() && k < world.settlements.sites.size(); ++k) {
        world.settlements.sites[k].community = society.community_traits[k];
    }

    world.humans = std::move(hinterland);
    world.seasonal_food = std::move(seasonal);
    world.society = std::move(society);
    world.has_humans = true;
    world.systems.push_back("humans");
    world.systems.push_back("seasonal_food");
    world.systems.push_back("society");
}

}  // namespace fwg
