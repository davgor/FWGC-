// Stage 12's first half, "Populated regions": the hinterland a city actually holds.
//
// A city on its own is a pin. This is the ground under it — which cells it controls,
// how much exportable food and material that ground yields, where the rural hamlets and
// the fortresses watching its roads and its people's frontiers stand, which cities share a
// culture, and whether annual
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
#include <optional>
#include <string>
#include <vector>

#include "fwg/grid.hpp"
#include "fwg/roads.hpp"

namespace fwg {

struct World;

// 10 (2026-09-23): fortresses watch roads and frontiers and stand through a rebuild, and
// each carries its watch, garrison uid and raising age. 9 was the reference's block.
inline constexpr int kHumansBlockVersion = 10;

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
    // A road fort: the road node it guards. A frontier fort: the chokepoint it overlooks.
    int protected_route_node = -1;
    // What a fort watches (ruled 2026-09-23): "road" -- a capital's or a war veteran's own
    // roads -- "frontier" -- the natural route between two civilizations' lands -- or
    // "player", a fort the player founded, which watches what the player says.
    std::string watch;
    // The city that garrisons it, by the uid that survives an age turn. A fort stands while
    // this city stands, and is carried across a rebuild rather than re-proposed.
    std::string core_uid;
    long long raised_age = 0;  // the age the fort was raised
    // Frontier forts only: the two civilizations whose route it watches, in id order, and
    // the ground the corridor of near-cheapest routes covers where it is narrowest.
    std::string frontier_a;
    std::string frontier_b;
    double corridor_km2 = 0.0;

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

// What the world asked for, and what it was allowed. Published so a reader can ask why a
// world has the fortress count it has instead of inferring it.
//
// The keys are the reference's; the rule behind them is this port's (ruled 2026-09-23). A
// capital and a city that has fought each ask for one road fort; two civilizations joined
// by a road ask for one frontier fort; a fort that stands already answers its ask.
struct FortressDemand {
    int proposed = 0;           // asks no standing fort answers: what this pass would raise
    int city_ceiling = 0;       // cities that ask for a road fort: capitals and veterans
    int requested_ceiling = 0;  // fortress_count: a caller's ceiling, never a target
    int limit = 0;              // the most this pass may raise: proposed, under the ceiling
    int player_founded = 0;
    int veteran_demand = 0;     // road forts war adds: cities that fought and are not capitals
    int veteran_cities = 0;     // cities that have fought, capitals included
    int crossings = 0;
    double road_length_m = 0.0;
    double support_reach = 0.0;
    // This port's own.
    int capitals = 0;
    int frontiers = 0;          // pairs of civilizations joined by a road
    int carried = 0;            // forts standing from an earlier pass
    int road_forts = 0;         // road forts the world holds, standing and new
    int frontier_forts = 0;     // frontier forts the world holds, standing and new
    double spacing_m = 0.0;     // the least distance between two forts
    int unanswered = 0;         // asks no fort answers after this pass: no usable, spaced ground
};

// --- frontiers -------------------------------------------------------------

// Where the natural route between two lands is narrowest: a pass, a land bridge, a ford.
//
// `from_a` and `from_b` are terrain-cost distances from each land's cities, infinite where
// unreachable. The cheapest route between the lands costs C, the least from_a + from_b of
// any node. The routes an army would weigh are every node within `slack` of it (from_a +
// from_b <= C * (1 + slack)); each sits a share t = from_a / (from_a + from_b) of the way
// across. The band [band_low, band_high] of t is cut into `slices`, and a slice's width is
// the ground its corridor nodes cover, divided by sqrt(t (1 - t)) at its centre: that is
// the shape an open plain's corridor takes between two points, so on open ground every
// slice scores alike and only terrain that squeezes the routes stands out. A slice's
// crossing is its node on the cheapest route. The ends of the route are left out because
// every route leaves from a city, where any corridor is narrow; a route too short to have
// any node inside the band offers its cheapest interior node instead.
//
// Every occupied slice, narrowest first (ties: nearer the middle, then lower slice), so a
// caller that cannot build at the narrowest point takes the next. Empty when the lands are
// not joined over ground.
struct Crossing {
    int node = -1;
    double route_cost = 0.0;  // C
    double width_km2 = 0.0;   // the corridor's ground in this slice
    double progress = 0.0;    // t at the crossing node
};
std::vector<Crossing> narrowest_crossings(const std::vector<double>& from_a, const std::vector<double>& from_b,
                                          const std::vector<double>& areas_m2, double slack = 0.1,
                                          double band_low = 0.2, double band_high = 0.8, int slices = 12);

struct HumanHinterland {
    int version = kHumansBlockVersion;
    std::string population_profile;

    std::vector<CityCore> cores;        // one per city, in city order
    std::vector<RuralSite> hamlets;     // generated city-major, then player-adopted
    // The forts that stand, in the order they were raised; then this pass's road forts, best
    // first; then its frontier forts; then the player's.
    std::vector<RuralSite> fortresses;
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

    // Rural rows from a previous run of this pass. Of the hamlets, ONLY rows whose
    // `founded_by` is "player" are read; the rest are discarded and re-derived. A rural
    // site the player founded is an instruction, not a proposal: it is not subject to the
    // counts and ceilings below, and its node joins the occupied set BEFORE the generated
    // passes run, so nothing is placed on top of it.
    //
    // A generated fort stands (ruled 2026-09-23): it is carried while the city that
    // garrisons it (`core_uid`) stands and its ground is dry, and it answers the ask that
    // raised it, so a rebuild raises only what the world newly needs.
    std::vector<RuralSite> previous_hamlets;
    std::vector<RuralSite> previous_fortresses;

    // Wars each city has fought, by city index. A city that has fought asks for a road
    // fort, as a capital does. Empty means a world that has never been to war, where only
    // the capitals ask.
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
