// Stage 12's second half, "Populated regions": the world becomes multimodal.
//
// This is the only pass where a world stops being a landmass with roads on it. It sites
// coastal harbour hamlets against each inland city, allocates every reachable ocean cell
// to exactly ONE port as an exclusive fishery, prices monthly fish delivery under sea
// ice, builds the sea and air networks on top of the existing roads, re-sites the witch
// huts onto isolated low-suitability ground, and then runs the authoritative four-year
// monthly food stress test over all three transport modes at once.
//
// It exists because the seasonal pass is ground-only and knows nothing about coasts:
// without this pass a coastal world has no harbours, no fisheries, no sea trade and no
// authoritative supply diagnostic.
//
// THE SKY PASS IS STRUCTURALLY DEAD. `make_sky` returns nothing on world recipe 3, and
// recipe 3 is the only recipe the pipeline ships, so no world this generator can build
// has a sky island. It is ported because recipe 0 exists in the config surface, not
// because anything reaches it — and the `sky_clusters` option is bounded at zero on both
// ends besides, so even recipe 0 would produce an empty list today.
//
// ON THE LIVE PATH THIS PASS CONSUMES NO RANDOM NUMBERS. Every draw is inside `make_sky`.
// It is a pure function of the world document plus the config.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fwg/grid.hpp"
#include "fwg/humans.hpp"
#include "fwg/vec3.hpp"

namespace fwg {

struct World;

// --- shared helpers --------------------------------------------------------

// Fraction of the same finite fish harvest accessible under route ice.
//
// One at open water, exactly `winter_fraction` under full ice. It NEVER exceeds one, so
// it can only reduce a harvest — it is not a cold-weather bonus.
double fishing_access_factor(double ice, double winter_fraction);

// The cost of moving a hull between two adjacent water cells, or nothing where it cannot
// go. Navigable ocean only — a lake is not the sea — deep enough for the draft and below
// the mutation limit this hull's people will sail through.
//
// The three-comparison passability test is written out INLINE at each of the four places
// it is needed rather than factored into a helper: the routing search calls this several
// million times per world and the call overhead dominated the three comparisons.
class WaterCost {
public:
    WaterCost(const SphereGrid& grid, const std::vector<double>& water, const std::vector<double>& depth,
              const std::vector<double>& hazard, double limit, double draft)
        : grid_(&grid), water_(&water), depth_(&depth), hazard_(&hazard), limit_(limit), draft_(draft) {}

    std::optional<double> operator()(int i, int j, double distance) const;

private:
    const SphereGrid* grid_;
    const std::vector<double>* water_;
    const std::vector<double>* depth_;
    const std::vector<double>* hazard_;
    double limit_;
    double draft_;
};

// --- sky -------------------------------------------------------------------

struct SkyMonth {
    double temperature = 0.0;
    double snow = 0.0;
};

// An independent disk surface, never a displacement of the ground below.
struct SkyMesh {
    std::vector<std::array<double, 3>> vertices;   // local east, radial altitude, north; metres
    std::vector<std::array<int, 3>> triangles;
};

struct SkyIsland {
    std::string id;         // "sky-<k>-<j>", which is also the layer name
    std::string cluster_id; // "sky-<k>"
    std::string layer;
    int x = 0;
    int z = 0;
    Vec3 direction;
    double radius_m = 0.0;
    double altitude_m = 0.0;
    double area_km2 = 0.0;
    double temperature_c = 0.0;
    double moisture = 0.0;
    std::string cold_habitat;
    double food_potential = 0.0;
    double freshwater_capacity = 0.0;
    double magic_support = 0.0;
    SkyMesh mesh;
    std::array<SkyMonth, 12> months{};
};

struct SkySettlement {
    // A STRING id, which is why a world-economy row's site identity is mixed: a surface
    // city is an int ordinal and a sky settlement is this.
    std::string id;
    std::string layer;
    std::string kind;  // "sky_settlement"
    std::string population_profile;
    std::string civilization_id;
    int x = 0;
    int z = 0;
    Vec3 direction;
    double altitude_m = 0.0;
    long long population_estimate = 0;
    std::string name;
    double freshwater_capacity = 0.0;
    std::string reason;
};

struct SkyReport {
    int version = 1;
    std::vector<SkyIsland> islands;
    std::vector<SkySettlement> settlements;
    double area_km2 = 0.0;
    bool enabled = false;
    std::string disabled_reason;
    std::string method;
};

// --- harbours and fisheries ------------------------------------------------

// A coastal harbour hamlet. Published into the fisheries block AND appended verbatim to
// the hinterland's hamlet list, where it sits beside "hamlet-N" rows with a different
// field set.
//
// It has NO uid, unlike every generated hamlet and fortress, so it is the one rural row
// that cannot be followed across an age boundary by uid. Its NODE is the stable key.
struct HarbourPort {
    std::string id;      // "coastal-<city index>-<global port count>"
    std::string kind;    // always "hamlet"
    std::string role;    // "harbor + fishing" | "harbor" | "fishing" | "landing"
    std::string layer;   // always "surface"

    int node = 0;      // the land node the harbour stands on
    int sea_node = 0;  // the water node it launches from
    double landing_distance_m = 0.0;
    int x = 0;
    int z = 0;
    int core_id = 0;  // the owning city's index
    std::string population_profile;
    std::string culture_id;
    double height_m = 0.0;
    double harbor_quality = 0.0;  // always above the candidate floor
    // Whether this harbour can carry sea trade at all. The single most consequential
    // threshold in the pass: raise it and the whole sea network empties.
    bool trade_terminal = false;

    std::vector<int> access_nodes;  // ground path, CITY FIRST, port node last
    double access_cost = 0.0;       // road cost from the city, not a length
    std::string reason;

    double worked_area_km2 = 0.0;
    double delivered_food = 0.0;
    double delivered_materials = 0.0;  // always zero: a harbour mines nothing
    double irrigation_benefit = 0.0;   // always zero: a harbour farms nothing
    std::array<double, 12> monthly_fish{};
    std::vector<int> fishing_nodes;  // allocated water nodes, in claim order
    double winter_fishing_fraction = 0.0;
};

struct Fisheries {
    std::vector<HarbourPort> ports;
    // Summed over ALLOCATED cells only, never over all water: potential capacity is never
    // credited before allocation.
    double potential_annual_food = 0.0;
    double delivered_annual_food = 0.0;  // provably <= the above
    std::string method;
};

// --- transport -------------------------------------------------------------

// One link of the multimodal network. `food` is exactly what the simulator reads; the
// rest is what a map or an atlas reads.
//
// Ground and sea carry `nodes` (sphere node indices); air carries `path` (grid cells) and
// no nodes at all.
struct TransportRoute {
    FoodRoute food;
    std::string mode;  // "ground" | "sea" | "air"
    std::vector<int> nodes;
    std::vector<std::pair<int, int>> path;  // air only, as (x, z)
    double length_m = 0.0;
    double cost = 0.0;                // sea only
    double cruise_altitude_m = 0.0;   // air only
    std::string port_a;               // sea only
    std::string port_b;
};

// A row of `transport.sites`. Identity is mixed: a surface city is an int, a sky
// settlement is a string.
struct TransportSite {
    int site_id = -1;
    std::string sky_site_id;
    std::string layer;
};

struct TransportReport {
    // ORDER IS LOAD-BEARING: ground, then sea, then air. The simulator walks the route
    // list in order and earlier routes get first claim on a seller's surplus and on a
    // buyer's material budget.
    std::vector<TransportRoute> routes;
    std::vector<int> terminals;  // indices into the combined surface + sky site list
    std::vector<TransportSite> sites;
};

// --- the world economy -----------------------------------------------------

struct EconomySite {
    int site_id = -1;
    std::string sky_site_id;
    std::string layer;
    std::string name;
    double annual_demand = 0.0;
    double annual_harvest = 0.0;
    double annual_shortage = 0.0;
    double food_coverage = 0.0;
    std::vector<int> lean_months;  // 1..12, over the FOURTH year only
    double ending_reserve = 0.0;
};

struct WorldEconomy {
    int version = 1;
    std::vector<EconomySite> sites;
    std::vector<FoodModel> models;  // the copied seasonal models with fish folded in, plus sky
    std::vector<FoodMonth> months;  // the raw 48-month history
    std::string method;
    std::string limits;
};

// --- witch huts ------------------------------------------------------------

// A re-sited witch hut. NOTE THE SPELLING: the ecology pass mints "witch_huts-<node>"
// and this pass mints "witch-hut-<node>". There is no node field — the node is in the id.
struct SocietyWitchHut {
    std::string id;
    std::string kind;  // always "witch_hut"
    int x = 0;
    int z = 0;
    std::string layer;  // always "surface"
    double intensity = 0.0;
    double conventional_suitability = 0.0;
    std::string reason;
};

// --- the pass --------------------------------------------------------------

struct SocietyReport {
    // The two gates, reported rather than inferred. `ran` false means the pass refused
    // before writing anything at all, not even the sky block.
    bool ran = false;
    bool has_economy = false;

    SkyReport sky;
    Fisheries fisheries;
    TransportReport transport;
    WorldEconomy world_economy;

    // Everything below is a CHANGE to a block another pass owns. Returned rather than
    // applied, so this pass stays a function of the world; see the header note on wiring.

    // Replaces every witch_hut row in the region landmarks. Up to as many as were there.
    std::vector<SocietyWitchHut> witch_huts;
    // One list per city, in site order. The caller writes these onto the sites.
    std::vector<std::vector<std::string>> community_traits;
    // Value is the owning PORT INDEX into fisheries.ports, or -1.
    Grid fishing_ground_owner;
};

struct SocietyRequest {
    const World* world = nullptr;
    const HumanHinterland* humans = nullptr;
    const SeasonalFood* seasonal = nullptr;

    // The settlement-suitability grid the witch huts are judged against. The generator
    // publishes one per people ("suitability_<id>"); this pass wants the primary one.
    // Null falls back to "suitability_<cfg.population_profile>" and then to a zero field,
    // which admits every cell rather than refusing every cell.
    const Grid* suitability = nullptr;

    // City names by site index, for the economy summary. Empty is honest: a name this
    // pass does not have is better left blank than invented.
    std::vector<std::string> site_names;

    // The nodes the colleges stand on. They are seated before this pass and are no hamlet,
    // so a harbour founded on one put two sites on one node (colleges_stand_apart, found
    // 2026-09-23). Empty is a world with no colleges.
    std::vector<int> college_nodes;
};

// Runs the society pass.
//
// Needs stage 11 and the hinterland. NOT IDEMPOTENT in the reference: calling it twice
// without an intervening hinterland rebuild appends the ports to the hamlet list twice.
// Returning the additions rather than applying them is what makes that the caller's
// problem to get right rather than a hidden one.
SocietyReport add_world_society(const SocietyRequest& request);

}  // namespace fwg
