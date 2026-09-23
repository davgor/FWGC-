// Stage 16, "Simulation complete": the passes that read a finished world.
//
// The reference runs, in order: the city, hamlet and castle planners; the nomads, their
// routes, the beast movements, the encounters, and the nomads' effects on the world; then
// the heroes, the story web, the rosters and the key locations. The planners come first
// because they only read the world, and the bands come before the packages that read
// them, so heroes and rosters see a world with its nomads in it.
//
// Each pass is a function of a request, not of a World. This file is where the World is
// turned into those requests, which makes it the one place that has to know how the
// blocks the earlier stages wrote map onto what each pass reads.
#include "fwg/complete_stage.hpp"

#include <cmath>
#include <utility>

#include "fwg/city_plan.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

std::vector<CitySite> city_sites(const World& world) {
    std::vector<CitySite> out;
    out.reserve(world.settlements.sites.size());
    for (const SettlementSite& s : world.settlements.sites) {
        CitySite site;
        site.uid = s.uid;
        site.id = s.id;
        site.name = s.name;
        site.x = s.x;
        site.z = s.z;
        site.city_class = s.city_class;
        site.population_profile = s.population_profile;
        site.population_estimate = s.population_estimate;
        site.urban_population_estimate = s.urban_population_estimate;
        // The planner never reads the distance, only whether one was measured: a city
        // with water it can collect or have delivered. Unreachable water is no water.
        if (std::isfinite(s.freshwater_distance_m)) site.freshwater_distance_m = s.freshwater_distance_m;
        site.resource_potential = s.resource_potential;
        site.founded_by_player = s.founded_by == "player";
        // Left empty: the planner projects the world's own roads into the city's window
        // once it has decided how wide the window is.
        out.push_back(std::move(site));
    }
    return out;
}

std::vector<HamletPin> hamlet_pins(const World& world) {
    std::vector<HamletPin> out;
    out.reserve(world.humans.hamlets.size());
    for (const RuralSite& h : world.humans.hamlets) {
        HamletPin pin;
        pin.id = h.id;
        pin.x = h.x;
        pin.z = h.z;
        pin.node = h.node;
        // A position in the settlement list, which is exactly what the planner indexes
        // with -- as long as the list it is handed is in the world's own order, which
        // city_sites() preserves. A re-sorted list would attribute every hamlet to the
        // wrong parent city, and the only symptom would be a plausible neighbour's name.
        pin.core_id = h.core_id;
        pin.population_profile = h.population_profile;
        pin.role = h.role;
        pin.access_nodes = h.access_nodes;
        out.push_back(std::move(pin));
    }
    return out;
}

}  // namespace

void add_simulation_complete(World& world) {
    if (!world.has_settlements) return;
    const auto held = sphere_grid(world.effective.size, world.effective.globe_radius);
    const SphereGrid& grid = *held;

    const std::vector<CitySite> sites = city_sites(world);
    world.city_plans = fill_cities(world, grid, sites);
    world.systems.push_back("city_plans");
    if (world.has_humans) {
        world.hamlet_plans = fill_hamlets(world, grid, sites, hamlet_pins(world));
        world.systems.push_back("hamlet_plans");
    }
    world.has_plans = true;
}

}  // namespace fwg
