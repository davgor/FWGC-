// Stage 10, "Founding": the pass that actually puts cities on the map.
//
// Everything this needs already exists in pieces -- habitation assesses the ground,
// routing prices capacity, founding places the sites, settlement classifies and
// populates them. This is the stage that calls them in order and writes the result into
// the world, which is what makes `completed_stage = 10` true rather than a claim.
#pragma once

#include <string>
#include <vector>

#include "fwg/founding.hpp"
#include "fwg/settlement.hpp"
#include "fwg/vec3.hpp"
#include "fwg/wars.hpp"

namespace fwg {

struct World;

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

    // What this city is known for, from the society pass. Empty until stage 12 runs.
    std::vector<std::string> community;

    // The age this city was founded in and the culture that founded it. A city that
    // stands through an age turn CARRIES both; a new one takes the current age.
    int founded_age = 0;
    std::string source_culture;
    // "player" for a city the player founded; empty for a generated one. Carried.
    std::string founded_by;
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
};

// What the world can support, and what it was asked for. Published so a reader can ask
// why a world has the number of cities it has rather than inferring it.
struct PopulationBudget {
    long long world_capacity = 0;                      // residents the ground could feed
    std::map<std::string, long long> allowances;       // per people
    std::map<std::string, double> weighted_habitat_km2;
    std::map<std::string, long long> requested_cities;  // quotas after every ceiling
    CityCeiling ceiling;                                // what the ground alone allowed
    long long city_limit = 0;                           // what actually bound
};

struct SettlementReport {
    std::vector<SettlementSite> sites;
    PopulationBudget budget;
    FoundingReport founding;
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

}  // namespace fwg
