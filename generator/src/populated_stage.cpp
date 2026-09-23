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

#include "fwg/colleges.hpp"
#include "fwg/humans.hpp"
#include "fwg/society.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

// The six layers the hinterland pass returns rather than writes.
void install_hinterland_layers(World& world, const HumanHinterland& hinterland) {
    world.layers["food_potential"] = hinterland.food_potential;
    world.layers["natural_food_potential"] = hinterland.natural_food_potential;
    world.layers["irrigation_benefit"] = hinterland.irrigation_benefit;
    world.layers["culture_region"] = hinterland.culture_region;
    world.layers["civilization_region"] = hinterland.civilization_region;
    world.layers["hamlet_catchment"] = hinterland.hamlet_catchment;
}

}  // namespace

HumansRequest humans_request(const World& world) {
    HumansRequest request;
    request.world = &world;
    // The rows a previous run placed. The pass reads only the player's among them and
    // re-derives everything else, so handing it the whole list is the reference's reading
    // of `result['humans']`, not a carry of generated rows.
    if (world.has_humans) {
        request.previous_hamlets = world.humans.hamlets;
        request.previous_fortresses = world.humans.fortresses;
    }
    // Each city's wars, by site index (the pass keys its owner field on site id, which is
    // the index): the reference reads every city's war_history here (veteran_wars), so a
    // city that has fought asks for more forts and weights the ground it owns. A world no
    // age has turned has none, and keeps the road-and-city answer.
    request.war_history.reserve(world.settlements.sites.size());
    for (const SettlementSite& site : world.settlements.sites) {
        request.war_history.push_back(static_cast<int>(site.war_history.size()));
    }
    return request;
}

void rebuild_hinterland(World& world) {
    if (!world.has_roads) return;
    HumanHinterland hinterland = add_humans(humans_request(world));
    if (hinterland.cores.empty()) return;
    install_hinterland_layers(world, hinterland);
    world.humans = std::move(hinterland);
    world.has_humans = true;
    world.systems.push_back("humans");
}

void add_populated_regions(World& world) {
    if (!world.has_roads) return;

    HumanHinterland hinterland = add_humans(humans_request(world));
    if (hinterland.cores.empty()) return;

    // Colleges between the hinterland and the seasons, as the reference orders them: they
    // stand clear of the hamlets and fortresses the hinterland just placed, and nothing
    // they decide is read by the seasonal food.
    CollegeReport colleges = add_colleges(world, hinterland);

    SeasonalFoodRequest seasonal_request;
    seasonal_request.world = &world;
    seasonal_request.humans = &hinterland;
    SeasonalFood seasonal = add_seasonal_food(seasonal_request);

    SocietyRequest society_request;
    society_request.world = &world;
    society_request.humans = &hinterland;
    society_request.seasonal = &seasonal;
    // The suitability grid the witch huts are judged against: the generic field the
    // settlement stage publishes, exactly as the reference reads it. Left null it falls
    // back to a ZERO field, and a zero field admits every cell rather than refusing every
    // cell -- huts would stop being sited on poor ground and start being sited anywhere.
    if (const auto it = world.layers.find("suitability"); it != world.layers.end()) {
        society_request.suitability = &it->second;
    }
    for (const SettlementSite& s : world.settlements.sites) society_request.site_names.push_back(s.name);

    SocietyReport society = add_world_society(society_request);

    // --- the layers both passes produce ------------------------------------
    install_hinterland_layers(world, hinterland);
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

    // The witch huts, re-sited by their own preference for isolation and poor conventional
    // suitability. Every hut the ecology pass placed is replaced, as the reference replaces
    // them: the old rows removed, the re-sited ones appended after everything else.
    if (society.ran) {
        std::erase_if(world.ecology.landmarks, [](const Landmark& mark) { return mark.kind == "witch_hut"; });
        for (const SocietyWitchHut& hut : society.witch_huts) {
            Landmark mark;
            mark.id = hut.id;
            mark.kind = hut.kind;
            mark.x = hut.x;
            mark.z = hut.z;
            mark.layer = hut.layer;
            mark.intensity = hut.intensity;
            mark.conventional_suitability = hut.conventional_suitability;
            mark.reason = hut.reason;
            world.ecology.landmarks.push_back(std::move(mark));
        }
    }

    for (std::size_t k = 0; k < society.community_traits.size() && k < world.settlements.sites.size(); ++k) {
        world.settlements.sites[k].community = society.community_traits[k];
    }

    world.colleges = std::move(colleges);
    world.humans = std::move(hinterland);
    world.seasonal_food = std::move(seasonal);
    world.society = std::move(society);
    world.has_humans = true;
    world.systems.push_back("humans");
    world.systems.push_back("colleges");
    world.systems.push_back("seasonal_food");
    world.systems.push_back("society");

    // Now the cores exist, a city founded this age takes its culture from its own.
    remember_cities(world, world.age);
}

}  // namespace fwg
