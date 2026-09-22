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

#include <map>
#include <string>
#include <vector>

#include "fwg/civilizations.hpp"
#include "fwg/config.hpp"
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

// How many cities the GROUND can hold, whoever wants them.
//
// Cities must stand a settlement spacing apart, so habitable land divided by the
// densest packing of that separation is how many can exist at all. This is a shared
// ceiling applied after every people has been assessed, so an early entry in the
// registry cannot exhaust the budget before a later one is considered.
//
// The binding spacing is whichever is COARSER: the authored one, or one raster cell.
// Spacing alone bounded this while the reference world was 11 km across and a cell was
// 87 m, but a coarse grid on a large world inverts that — at a 200 km circumference a
// size-17 grid has 12.5 km cells against an 8 km spacing, and the area ceiling then
// admitted about 63 cities onto 289 nodes, a city every four or five cells, each going
// on to plan a full crop.
struct CityCeiling {
    long long packing = 0;            // what the ground alone allows
    double habitable_km2 = 0.0;
    double per_city_km2 = 0.0;
    double resolvable_spacing_m = 0.0;  // the coarser of the authored spacing and a cell
};
CityCeiling city_ceiling(double habitable_km2, double settlement_spacing_m, double cell_spacing_m);

// How many cities this world will actually allow, from the ground ceiling and the
// caller's request.
//
// Three rules, in order:
//   * Asking for NO cities is not the same as asking for no ceiling, and it outranks the
//     ground: a caller who wants an empty world gets one.
//   * A world that infers its own quotas is ceilinged by its ground, not by a count the
//     caller never set -- otherwise a recipe world silently keeps the default of 24.
//   * A caller who LOWERS the count below the recipe default is asking for fewer cities
//     and gets them, inferred or not. `settlement_count` is published as "Maximum surface
//     cities", so a request for two that returns ten is a broken parameter, not a ground
//     rule. Leaving it at the default still means "no ceiling but the ground".
long long city_limit(long long packing, const Config& cfg, int default_settlement_count);

// Reduce per-people quotas until their total fits a ceiling, taking from the largest
// first and breaking ties by name — so the outcome does not depend on map order.
void apply_city_ceiling(std::map<std::string, long long>& quotas, long long limit);

// How a people's supported population is divided between its cities.
//
// The capacity allowance is split evenly and the remainder goes to the FIRST cities in
// founding order, so an older city is the one that carries the odd resident.
struct CityPopulation {
    long long residents = 0;
    long long urban = 0;
    long long rural = 0;
};

// `index` is the city's position among its own people's cities, in founding order.
// `absorbed_refugees` are people who walked in from a town that fell: the capacity split
// cannot know about them, because it is derived from the ground THIS city farms. They are
// added after the split, so the split stays a pure function of habitat and the arrivals
// stay auditable on their own.
CityPopulation city_population(long long allowance, long long city_count, long long index,
                               long long absorbed_refugees = 0);

// What a city is called, within its own people.
enum class CityClass { Capital, Medium, Small };

// One capital per people, and no forced cities.
//
// A founding capital is RETAINED even when a later city outscores it -- a people's first
// city does not stop being its capital because better ground was settled afterwards.
// Only where no founding capital survives does suitability decide, with the node index
// breaking ties so the choice cannot depend on the order the sites were listed in.
struct CityClassInput {
    int node = 0;
    std::string population_profile;
    double suitability = 0.0;
    bool founding_capital = false;
};
std::vector<CityClass> classify_cities(const std::vector<CityClassInput>& sites,
                                       double medium_suitability_min);

}  // namespace fwg
