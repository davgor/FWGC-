// Stage 12's first half, "Populated regions": the hinterland a city actually holds.
//
// A city on its own is a pin. This is the ground under it — which cells it controls,
// how much exportable food and material that ground yields, where the rural hamlets and
// the route-defence fortresses stand, which cities share a culture, and whether annual
// trade over the existing roads can close each city's food gap.
//
// EVERY NUMBER HERE IS A RELATIVE EXPORTABLE POTENTIAL UNIT. Not people, not tonnes per
// year, not population capacity. A hamlet's `delivered_food` is the surplus its cells can
// export AFTER their own subsistence, which is why zero is a normal answer rather than a
// failure to be explained away.
//
// The seasonal simulator at the bottom of this header lives here rather than inside the
// seasonal pass because it has TWO authoritative callers: this stage's own pass, and the
// society pass, which re-runs the identical loop over a larger route set. Two copies of a
// conservation loop that must agree is how the two stop agreeing.
#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "fwg/grid.hpp"
#include "fwg/roads.hpp"

namespace fwg {

struct World;

inline constexpr int kHumansBlockVersion = 9;

// --- rural settlement ------------------------------------------------------

// A hamlet or a fortress. One struct rather than two, because the adoption rule, the
// occupied-node set and the access-path reconstruction treat them identically and only
// the scoring that placed them differs.
struct RuralSite {
    // TWO IDS, AND THEY MEAN DIFFERENT THINGS.
    //
    // `id` is the ORDINAL — "hamlet-3" — a position in the list THIS pass is building.
    // The hamlet and castle planners join on exactly that, and it renumbers whenever the
    // rural layer is rebuilt, which includes every age advance: after an advance,
    // "fortress-36" names different ground.
    //
    // `uid` is "hamlet-node-412", the handle that SURVIVES an age boundary because the
    // terrain node does not move. Rosters, heroes and the player-founding route all mint
    // and key on this exact spelling — one convention, not three.
    std::string id;
    std::string uid;
    std::string kind;  // "hamlet" or "fortress"

    int node = 0;
    int x = 0;
    int z = 0;
    int core_id = -1;  // the owning city index
    std::string population_profile;
    std::string culture_id;
    double height_m = 0.0;

    // Terrain-cost distance from the owning city, in road-cost units — NOT a length. It
    // carries the grade, flood and bridge surcharges, and is nonetheless divided by a
    // reach (a length) in the delivery exponential.
    double access_cost = 0.0;
    // [own node, ..., owning city's node]. Every consecutive pair is a traversable edge.
    std::vector<int> access_nodes;
    std::string reason;

    // Hamlets only.
    std::string role;  // "farming" or "resource"
    double irrigation_benefit = 0.0;
    double worked_area_km2 = 0.0;
    double delivered_food = 0.0;
    double delivered_materials = 0.0;

    // Fortresses only.
    double defence_score = 0.0;
    int protected_route_node = -1;

    // What the player chose, carried across a rebuild rather than re-derived.
    std::string founded_by;
    long long founded_age = 0;
    std::string name;
};

// --- cultures --------------------------------------------------------------

// One road cheap enough to carry a shared culture, between two cities of the SAME
// people. Two peoples sharing a road never share a culture.
struct CultureLink {
    int from = 0;
    int to = 0;
    double cost = 0.0;
};

// Single-link interaction groups over those links.
//
// The merge always points the LARGER root at the smaller, so the canonical group id is
// the lowest member city index and the result does not depend on the order the links
// were listed in.
std::vector<int> culture_groups(int count, const std::vector<CultureLink>& links, double threshold);

struct CultureGroup {
    int index = 0;              // the root city index
    std::string id;             // "<profile>-<8 lowercase hex>"; seed-local, not an ethnicity
    std::vector<int> city_ids;  // ascending
    std::string population_profile;
    std::string civilization_id;
    std::string architecture_style_id;
};

// --- cores and trade -------------------------------------------------------

struct CityCore {
    int site_id = 0;
    std::string population_profile;
    std::string culture_id;
    std::vector<std::string> hamlet_ids;
    std::vector<std::string> fortress_ids;

    double food_demand = 0.0;
    double food_supply = 0.0;
    double material_supply = 0.0;

    // Written by exchange_food, and only there.
    double food_imports = 0.0;
    double food_exports = 0.0;
    double trade_material_cost = 0.0;
    double food_deficit = 0.0;
};

struct FoodShipment {
    int from = 0;  // the seller city
    int to = 0;    // the buyer city
    double sent = 0.0;
    double delivered = 0.0;
    double lost = 0.0;
    double material_cost = 0.0;
    std::vector<int> city_path;  // seller to buyer, by city index
};

// Annual trade of a FINITE local surplus over roads that already exist.
//
// Neediest city buys first, nearest seller first. A seller's already-committed exports
// are subtracted from its surplus, so it cannot sell the same grain twice, and a buyer
// pays in material units out of a budget that runs out. What the trade cannot close stays
// visible in `food_deficit` rather than being papered over.
//
// Mutates `cores`: the import, export, material-cost and deficit fields are written here
// and nowhere else.
//
// REQUIRES `cores[k].site_id == k`, because a road names cities by index and the city
// graph is built at those positions. `add_humans` guarantees it by building the list in
// city order; a caller assembling cores by hand has to keep that.
std::vector<FoodShipment> exchange_food(std::vector<CityCore>& cores, const std::vector<Road>& roads);

// --- the block -------------------------------------------------------------

// What the road network asked for, and what it was allowed. Published so a reader can ask
// why a world has the fortress count it has instead of inferring it.
struct FortressDemand {
    int proposed = 0;
    int city_ceiling = 0;
    int requested_ceiling = 0;
    int limit = 0;
    int player_founded = 0;
    int veteran_demand = 0;
    int veteran_cities = 0;
    int crossings = 0;
    double road_length_m = 0.0;
    double support_reach = 0.0;
};

struct HumanHinterland {
    int version = kHumansBlockVersion;
    std::string population_profile;

    std::vector<CityCore> cores;        // one per city, in city order
    std::vector<RuralSite> hamlets;     // generated city-major, then player-adopted
    std::vector<RuralSite> fortresses;  // generated by descending defence score, then adopted
    std::vector<CultureGroup> cultures; // one per group root, ascending root index
    std::vector<FoodShipment> shipments;
    FortressDemand fortress_demand;

    std::string method;
    std::string culture_method;
    std::string defence_method;

    // The six layers this pass produces. RETURNED rather than written, so the pass stays
    // a function of the world instead of an edit to it; the caller installs them.
    Grid food_potential;
    Grid natural_food_potential;
    Grid irrigation_benefit;
    Grid culture_region;
    Grid civilization_region;
    Grid hamlet_catchment;
};

struct HumansRequest {
    const World* world = nullptr;

    // Rural rows from a previous run of this pass. ONLY rows whose `founded_by` is
    // "player" are read; everything else in an old block is discarded and re-derived. A
    // rural site the player founded is an instruction, not a proposal: it is not subject
    // to the counts and ceilings below, and its node joins the occupied set BEFORE the
    // generated passes run, so nothing is placed on top of it.
    std::vector<RuralSite> previous_hamlets;
    std::vector<RuralSite> previous_fortresses;

    // Wars each city has fought, by city index. A city that has fought wants more watching
    // its roads, so this raises both what the world asks for and how much it is allowed.
    // Empty means a world that has never been to war, which keeps exactly the
    // road-and-city answer.
    std::vector<int> war_history;
};

// Runs the hinterland pass. Needs stage 11: without roads there is no culture link, no
// route to defend and nothing to trade over. Returns an empty block when `phase < 9` or
// the world has no road network.
HumanHinterland add_humans(const HumansRequest& request);

// --- the seasonal simulator ------------------------------------------------
// Shared, not private: the society pass re-runs this identical loop over ground, sea and
// air routes at once to produce the authoritative world economy.

// Twelve monthly deliveries from one annual, mean-climate estimate.
//
// The annual figure is an estimate, not a guaranteed yield: seasonal growing suitability
// can change the year's actual production. Thermal seasonality REVERSES across the
// equator, and the harvest arrives one month after the growth that produced it.
std::array<double, 12> seasonal_harvest(double annual_food, double wet, double latitude_deg,
                                        double mean_temp, double tolerance, double ideal = 18.0,
                                        double seasonality = 1.0);

inline constexpr double kUnlimitedTransportBudget = std::numeric_limits<double>::infinity();

// One community's year, as the simulator sees it.
struct FoodModel {
    int site_id = -1;    // a surface city's index, or -1 for a sky settlement
    std::string sky_id;  // the sky settlement's string id; empty for a surface city
    std::array<double, 12> harvest{};
    double demand = 0.0;   // per month
    double storage = 0.0;  // capacity in food units
    double spoilage = 0.0; // fraction of OPENING stock lost each month
    double transport_budget = kUnlimitedTransportBudget;  // renewable material units per month
};

// What the simulator needs of a route, whichever mode carries it.
struct FoodRoute {
    int from = 0;  // index into the model list
    int to = 0;
    std::array<double, 12> capacity{};
    double efficiency = 0.0;  // <= 0 means the route does not run at all
    double price = 1.0;       // material units per delivered food unit
};

struct FoodMonthShipment {
    int from = 0;
    int to = 0;
    int route_index = 0;
    double sent = 0.0;
    double delivered = 0.0;
    double lost = 0.0;
    double material_cost = 0.0;
};

struct FoodMonthCity {
    double opening = 0.0;
    double harvest = 0.0;
    double spoiled = 0.0;
    double imports = 0.0;
    double exports = 0.0;
    double material_spent = 0.0;
    double demand = 0.0;
    double consumed = 0.0;
    double shortage = 0.0;
    double overflow = 0.0;
    double closing = 0.0;
};

struct FoodMonth {
    int year = 0;
    int month = 0;
    std::vector<FoodMonthCity> cities;
    std::vector<FoodMonthShipment> shipments;
    double opening = 0.0;
    double harvest = 0.0;
    double closing = 0.0;
    double transit_loss = 0.0;
    double consumed = 0.0;
    double spoiled = 0.0;
    double overflow = 0.0;
};

struct FoodRun {
    std::vector<FoodMonth> months;
};

// Monthly conservation with shared edge throughput and no same-month re-export.
//
// Stores start EMPTY, spoilage is charged on the opening stock before the harvest lands,
// and consumption precedes storage overflow — so a community cannot hoard what it needed
// to eat. A route's monthly capacity is ONE quantity shared and decremented across both
// directions, and the route list is walked IN ORDER: earlier routes get first claim on a
// seller's surplus and on a buyer's material budget, which is why a caller's
// ground/sea/air concatenation order is load-bearing rather than cosmetic.
FoodRun simulate_food(const std::vector<FoodModel>& cities, const std::vector<FoodRoute>& roads,
                      int years = 4);

// --- the seasonal-food pass ------------------------------------------------

struct SeasonalCity {
    int site_id = 0;
    double annual_demand = 0.0;
    double annual_harvest = 0.0;
    double annual_shortage = 0.0;
    double food_coverage = 0.0;
    std::vector<int> lean_months;  // 1..12
    double reserve_change = 0.0;
    double ending_reserve = 0.0;
    bool passes_final_year = false;
    long long urban_food_equivalent = 0;
};

struct SeasonalFood {
    int version = 1;
    int years = 4;
    int reported_year = 4;
    std::vector<SeasonalCity> cities;
    std::vector<FoodModel> models;  // one per city, parallel to the site list
    std::vector<FoodRoute> routes;  // one per road, POSITIONALLY aligned with roads.routes
    std::vector<FoodMonth> months;  // 48 entries
    std::string method;
    std::string limits;
};

struct SeasonalFoodRequest {
    const World* world = nullptr;
    const HumanHinterland* humans = nullptr;
};

// Re-expresses the annual, mean-climate food numbers as a monthly curve and runs four
// repeated climatic years of stock accounting from empty stores.
//
// YEAR 4 IS A STRESS-TEST OBSERVATION, NOT A CLAIM OF EQUILIBRIUM. Returns an empty block
// when there is no hinterland to read, which is how it refuses rather than by a phase
// guard of its own.
SeasonalFood add_seasonal_food(const SeasonalFoodRequest& request);

}  // namespace fwg
