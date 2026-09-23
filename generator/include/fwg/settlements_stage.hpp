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

#include "fwg/founding.hpp"
#include "fwg/settlement.hpp"
#include "fwg/vec3.hpp"
#include "fwg/wars.hpp"

namespace fwg {

struct World;

// The version stamped on a city's node-scale layout record, the reference's
// CITY_LAYOUT_VERSION. Bumping it is a world-output schema change.
inline constexpr int kCityLayoutVersion = 2;

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

// The `city_layout` a PLAYER-AUTHORED city carries: what the founding API writes when the
// player owns where the buildings go (settlement_api.hpp, layout "player").
//
// Only this record exists in the port. The generated node-scale layout -- district anchors
// and asset roll-ups drawn from the building packs -- was not ported, and nothing needs it:
// city_plans is the one block that says where buildings stand. So a generated city has no
// record here, and neither has a player city founded with layout "generated": the reference
// derives a generated record for that one at its next rebuild, which this port cannot.
//
// Shaped like the reference's record key for key. Everything that would describe a DERIVED
// layout is empty, because nothing was derived: no profile but the player's, no river
// reading, no building pack, no assets, no district anchors.
struct AuthoredCityLayout {
    struct River {
        int adjacent_river_edges = 0;
        std::optional<double> river_distance_m;  // absent: none was read
        bool bridge_recommended = false;
        int bridge_threshold = 0;
    };
    int version = kCityLayoutVersion;
    std::string layout_profile_id = "player";
    std::string layout_profile_name = "Player-authored layout";
    std::string population_profile;
    // The site's own estimate. The world owns the sizing: a settlement rebuild re-derives the
    // site's population and this follows it, while the layout itself is carried untouched.
    long long population_estimate = 0;
    River river;
    std::optional<std::string> building_pack_id;  // absent: no pack was drawn
    std::vector<std::string> required_assets;
    int required_asset_count = 0;
    int required_node_slots = 0;
    bool asset_anchors_missing = false;
    std::map<std::string, double> features;     // empty: no district was claimed
    std::map<std::string, double> constraints;  // empty
    std::string authored_by = "player";
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
    // The player's own layout, on a player city founded with layout "player"; absent
    // everywhere else. Carried across a rebuild together with `founded_by` -- the survivor
    // carry is the only reason an authored layout outlives an age turn -- and its population
    // estimate follows the site's.
    std::optional<AuthoredCityLayout> city_layout;
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
void add_settlements(World& world, const SettlementRebuild* rebuild = nullptr);

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
