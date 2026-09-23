// Stage 10, "Founding": the pass that actually puts cities on the map.
//
// Everything this needs already exists in pieces -- habitation assesses the ground,
// routing prices capacity, founding places the sites, settlement classifies and
// populates them. This is the stage that calls them in order and writes the result into
// the world, which is what makes `completed_stage = 10` true rather than a claim.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/city_layout.hpp"
#include "fwg/founding.hpp"
#include "fwg/settlement.hpp"
#include "fwg/vec3.hpp"
#include "fwg/wars.hpp"

namespace fwg {

struct World;

// The `settlements` block's version and its founding record's, the reference's (terrain_settlements
// and founding.found_cities). Both are world-output schema versions.
inline constexpr int kSettlementsVersion = 16;
inline constexpr int kFoundingVersion = 4;

// The founding record's `definition`, as the reference words it.
inline constexpr std::string_view kFoundingDefinition =
    "Sufficient population means capacity-derived city quotas, plus at most one food-supported diaspora bonus "
    "per parent, subject to safe habitat, spacing and a 24-city lab ceiling; 128-round safety limit.";

// The `settlements` block's method, and the sentence the reference appends once magic exists.
inline constexpr std::string_view kSettlementsMethod =
    "Candidate sites, not built cities or population simulation. Resources are seeded potential, flood risk a "
    "proximity/height proxy. Outposts can accept poor conditions; water cells and slopes beyond the selected "
    "population limit remain excluded. City assets are selected from deterministic data-driven building packs.";
inline constexpr std::string_view kSettlementsMagicMethod =
    " All city sites must pass the mutation hazard limit, including difficult-site cities.";

// A city a hidden school holds (ruled 2026-09-23): marked, left standing, and left for the
// game to interpret. Two ways in -- the age lottery chose a hidden school as the city's fate,
// or a corruption farmed or took it -- and every rebuild carries it. Corruption persists; the
// ruling says it ends only when a node overpowers it, and HOW that is measured is not yet
// decided, so nothing ends a hold yet.
struct CityCorruption {
    std::string school;  // the hidden school that holds the city
    // How it came to be held: "fallen" (the age lottery chose the hidden school), or what a
    // corruption did to it, "farmed" (blood) or "taken" (eldritch).
    std::string cause;
    int age = 0;                    // the age it was marked in
    std::optional<int> corruption;  // the corruption record that marked it, where one did
};

// One settlement as the world publishes it.
struct SettlementSite {
    // Position in the list. RENUMBERS whenever the set changes, which an age advance
    // does, so nothing downstream should key on it.
    int id = 0;
    // "surface-city-<age>-<node>-<people>". The stable identity: keyed on the terrain
    // node, so it survives the renumbering that `id` does not. Threat reports, claims,
    // seats and ruin ids are all keyed on this.
    std::string uid;
    int node = 0;
    int x = 0;
    int z = 0;
    // The unit vector to this city's cell centre. Carried rather than recomputed because
    // every consumer that measures a distance needs it and would otherwise each need the
    // grid size to derive it.
    Vec3 direction;
    // What its own people call it, and what the name means in their tongue. Drawn from
    // that people's heritage lexicon, so a dwarven city is named in dwarvish and the
    // gloss says why.
    std::string name;
    std::string name_gloss;
    std::string population_profile;  // the civilization
    std::string parent_race_id;
    CityClass city_class = CityClass::Small;
    std::string classification_reason;
    bool founding_capital = false;
    int founding_turn = 0;
    long long founding_year = 0;

    // Why here: the score and the conditions behind it.
    double suitability = 0.0;
    double height_m = 0.0;
    double slope_degrees = 0.0;
    double temperature_c = 0.0;
    double moisture = 0.0;
    double flood_risk = 0.0;
    double resource_potential = 0.0;
    // Infinity where no fresh water is reachable, which is a real answer rather than a
    // missing one: a site can be founded away from water and pay for it.
    double freshwater_distance_m = 0.0;
    // Why the site stands here, in words: the people's own settlement reason where its rules
    // author one, the registry's normal-site reason otherwise, "Founded by the player." for a
    // player's city.
    std::string reason;
    // The terms `suitability` is built from, named and ordered as the reference publishes them
    // (settlement.hpp, suitability_factors). Empty on a player's city until a rebuild founds
    // around it, as the reference's player record carries none.
    std::vector<SuitabilityFactor> suitability_factors;

    // What this city is known for, from the society pass. Empty until stage 12 runs.
    std::vector<std::string> community;

    // The age this city was founded in and the culture that founded it. A city that
    // stands through an age turn CARRIES both; a new one takes the current age.
    int founded_age = 0;
    std::string source_culture;
    // "player" for a city the player founded; empty for a generated one. Carried.
    std::string founded_by;
    // The node-scale layout (city_layout.hpp): planned by stage 10 on every city it founds,
    // re-planned once the city is sized, and drawn afresh at every settlement rebuild. On a
    // player city founded with layout "player" it is the player's own record instead, which
    // the rebuild CARRIES -- the survivor carry is the only reason an authored layout
    // outlives an age turn -- with its population estimate following the site's. Absent on
    // a player city founded with layout "generated", until the next rebuild plans one.
    std::optional<CityLayout> city_layout;
    // The building pack the layout drew its assets from, with the seed that drew it and the
    // pack catalogue's version. Present wherever stage 10 founded the city; absent on a city
    // the player founded, until the next rebuild. NOT carried: a rebuilt city draws again.
    std::optional<BuildingPackDraw> building_pack;
    // Refugees taken in at an age boundary, and the bands they came from. Added AFTER
    // the capacity split, so the split stays a pure function of the ground. Carried.
    long long absorbed_refugees = 0;
    std::vector<std::string> absorbed_bands;
    // Every war this city has fought. Both sides carry each war, and a survivor carries
    // its history through the rebuild, so a veteran's walls answer what it has fought.
    std::vector<WarParticipation> war_history;
    // The victor's uid, for a city that stood on as a vassal this age. NOT carried: the
    // rebuild drops it, as the reference's does -- a vassalage lasts one age boundary.
    std::string vassal_of;
    // Held by a hidden school, where one holds it. CARRIED: see CityCorruption.
    std::optional<CityCorruption> corruption;

    long long population_estimate = 0;
    long long urban_population_estimate = 0;
    long long rural_population_estimate = 0;

    // Where its settlers came from. -1 and empty for a parent's first city.
    int migration_source_node = -1;
    std::string source_civilization_id;
    double migration_distance_m = 0.0;
    bool cultural_branch = false;
    bool diaspora = false;
    std::string diaspora_reason;
    bool diaspora_bonus = false;  // a diaspora founding that waived a zero quota; carried
};

// What the world can support, and what it was asked for. Published so a reader can ask
// why a world has the number of cities it has rather than inferring it.
struct PopulationBudget {
    long long world_capacity = 0;                      // residents the ground could feed
    std::map<std::string, long long> allowances;       // per people
    std::map<std::string, double> weighted_habitat_km2;
    // The quotas founding actually worked to: after every ceiling, the food cap and any
    // diaspora bonus -- the founding report's effective_quotas. The reference rebinds its
    // quotas to exactly these before it publishes requested_cities.
    std::map<std::string, long long> requested_cities;
    CityCeiling ceiling;                                // what the ground alone allowed
    long long city_limit = 0;                           // what actually bound

    // --- the rest of the reference's population_budget block (document key names) ------
    // allowance / world_capacity per people; 0 on a world whose ground feeds nobody.
    std::map<std::string, double> shares;
    // Each competing people's minimum_founding_residents: the supported residents it needs
    // before it can found a city at all (calibration.minimum_city_region_residents).
    std::map<std::string, long long> minimum_city_region_residents;
    // Cities each competing people holds once founding is done, zero included.
    std::map<std::string, long long> placed_cities;
    // Residents the cities hold, refugees a survivor took in included, and the capacity
    // nobody settled. `unallocated` goes negative only if refugees outnumber the slack.
    long long allocated = 0;
    long long unallocated = 0;
};

struct SettlementReport {
    std::vector<SettlementSite> sites;
    PopulationBudget budget;
    FoundingReport founding;
    // The uids of the cities the player founded, sorted: written by the founding API on every
    // call. A settlement rebuild replaces this report and the list goes with it -- after an
    // age advance it is empty although the player's cities stand. That is the reference's
    // behaviour (its rebuild writes a fresh `settlements` block without the key), reproduced
    // and pinned by a test; `founded_by` on each site is the record that survives.
    std::vector<std::string> founded_by_player;
};

// What an age transition hands the settlement stage when it rebuilds civilization.
//
// The survivors are cities that stood through the turn; founding places the new age's
// cities AROUND them, and each survivor keeps its identity -- uid, name, founding age,
// founding culture, who founded it, the refugees it took in. The forbidden nodes are
// ruins, where nobody rebuilds. The previous founding record is where the clock and the
// once-per-parent diaspora bonuses resume from.
struct SettlementRebuild {
    int age = 0;
    std::vector<SettlementSite> survivors;
    std::vector<int> forbidden_nodes;
    FoundingReport previous;
};

// Runs stage 10, or rebuilds it after an age turn when `rebuild` is given. Needs stages
// 1-9: without climate, ecology and the leylines there is nothing to score ground on.
//
// Every city it founds draws a building pack and gets a node-scale layout: planned at
// founding, before the population is split, then RE-PLANNED from the same published seed
// once the city is sized -- except an authored layout, whose population is updated and
// whose districts are left alone.
void add_settlements(World& world, const SettlementRebuild* rebuild = nullptr);

// Whether a site's `city_layout` is the player's to keep rather than the planner's to
// redraw: the reference's `authored_layout`. It asks whether the PLAYER FOUNDED the site and
// whether the site carries a layout -- not whether the layout is the "player" profile.
//
// REFERENCE DEFECT, REPRODUCED AND PINNED: a player city founded with layout "generated"
// has no layout until its first rebuild plans one; from then on it passes this test. So
// that first rebuild keeps the plan drawn before the city was sized (only its population
// estimate follows the site), and every later rebuild carries it forward as if the player
// had authored it, while the site's own `building_pack` is drawn afresh each time.
bool authored_layout(const SettlementSite& site);

// What a city rebuilt on a survivor's node inherits from it, copied in place: the reference's
// `carry_survivor`. Its identity (uid, name, founding age and culture, who founded it), the
// refugees it took in and the wars it fought -- and its `city_layout` only where
// `authored_layout(old)` holds, which by the defect above includes a generated layout on a
// player's site. The building pack is never carried: the rebuilt city draws its own.
void carry_survivor(SettlementSite& site, const SettlementSite& old);

// Gives each new city its identity for this age, and each city founded this age the
// culture of its own core once the hinterland has placed one. Called after the
// settlements are founded and again after the hinterland, as the reference calls it at
// the end of every civilization pass.
void remember_cities(World& world, int age);

// Re-derive every city's class and its reason over the WHOLE list, as the founding pass
// classifies. `city_class` is a per-people suitability RANK rather than a size, so adding
// one city can demote an incumbent: a caller never sets it, it is recomputed.
void classify_settlements(std::vector<SettlementSite>& sites);

}  // namespace fwg
