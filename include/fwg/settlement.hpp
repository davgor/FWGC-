// What makes ground worth living on.
//
// Stage 10's scoring substrate: how far the nearest drinkable water is, how likely the
// ground is to flood, how much food it could grow, how big the landmass is, and — from
// all of that plus a people's own traits — a single 0..1 suitability.
//
// Every quantity here is an explicit proxy. `flood_risk` is not a return period,
// `farming_potential` is not a yield, and suitability is not a forecast that anyone will
// actually settle. They are comparable within one world and meaningless outside it.
#pragma once

#include <vector>

#include "fwg/civilizations.hpp"
#include "fwg/grid.hpp"

namespace fwg {

struct World;

// Distance over land to the nearest drinkable water, and which water that was.
//
// Ocean is NOT drinkable and is not crossed: a coastal cell with the sea at its door but
// no river is far from fresh water, which is most of why coasts are not automatically
// good sites. Salty lakes are excluded too.
struct FreshwaterAccess {
    std::vector<double> distance_m;  // infinity where no fresh water is reachable
    std::vector<int> source;         // the node of the water reached, or -1
};
FreshwaterAccess freshwater_access(const World& world, const SphereGrid& grid);

// How exposed a cell is to its nearest fresh water rising.
//
// Two terms, both proxies: how close the water is, and how far above ITS SURFACE the
// ground stands. Ten metres of freeboard removes the risk entirely; eighty metres of
// distance most of it.
std::vector<double> flood_risk(const World& world, const SphereGrid& grid, const FreshwaterAccess& fresh);

// Connected land bodies, and each cell's place in one.
struct LandmassContext {
    std::vector<double> area_m2;
    std::vector<double> fraction;   // of all land on the planet
    std::vector<char> largest;      // 1 where this is the biggest landmass
};
LandmassContext landmass_context(const SphereGrid& grid, const std::vector<double>& water);

// What a habitat rule is evaluated against at one cell.
HabitatSample environment_at(const World& world, const SphereGrid& grid, std::size_t node,
                             const LandmassContext& context);

// What a cell can grow, before and after irrigation.
//
// `natural` is what the ground yields unaided. `irrigated` adds what a people can win by
// carrying water to it, which needs a source within reach and only helps ground that is
// too DRY — irrigation cannot drain a swamp. Maintenance eats a quarter of the gain.
struct FarmingPotential {
    double natural = 0.0;
    double irrigated = 0.0;
};
FarmingPotential farming_potential(double slope_deg, double temperature_c, double moisture,
                                   double flood, double freshwater_distance_m, double adaptation,
                                   const CivilizationTraits& traits);

// Everything the suitability score reads at one cell.
struct SiteConditions {
    double freshwater_distance_m = 0.0;
    double slope_deg = 0.0;
    double temperature_c = 0.0;
    double moisture = 0.0;
    double resource = 0.0;
    double flood = 0.0;
    double magic_hazard = 0.0;
    double biome_opinion = 0.0;  // from biome_preference, already resolved
};

// A people's 0..1 opinion of one cell. Water scores zero: nobody builds on the sea.
double site_suitability(const CivilizationTraits& traits, const SiteConditions& at, bool is_water);

}  // namespace fwg
