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

namespace fwg {

struct World;

// One settlement as the world publishes it.
struct SettlementSite {
    int id = 0;
    int node = 0;
    int x = 0;
    int z = 0;
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

// Runs stage 10. Needs stages 1-9: without climate, ecology and the leylines there is
// nothing to score ground on.
void add_settlements(World& world);

}  // namespace fwg
