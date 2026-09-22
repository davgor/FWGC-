// Who settles where, and in what order.
//
// Founding runs in rounds. Each round every parent race takes a turn, draws against its
// participation percentage, and — if it draws in — places ONE city on the best ground it
// can still reach. The reach grows each round, so a people spreads outward from where it
// started rather than teleporting to the best cell on the planet.
//
// Two things run alongside the rounds. A parent's FIRST city must stand a quarter of the
// world away from every other parent's first city, so peoples do not all begin in the
// same valley. And every diaspora interval one civilization may be carried somewhere new
// by exile, schism or expedition — which is the only way a people with no quota at all
// ever reaches the map.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fwg {

// A parent race: the thing that owns civilizations and takes a turn.
struct ParentRace {
    std::string id;
    // Chance out of 100 that this parent founds in a round. Zero means it places its
    // first city in round one and never expands -- the draw is skipped on the opening
    // round precisely so that such a parent still exists.
    double founding_participation_percent = 100.0;
};

// One founded settlement, in the order it was founded.
struct FoundedSite {
    int node = -1;
    std::string population_profile;  // the civilization
    std::string parent_race_id;
    int founding_turn = 0;
    bool founding_capital = false;  // the parent's first city
    long long founding_year = 0;
    int migration_source_node = -1;      // -1 for a capital
    std::string source_civilization_id;  // empty for a capital
    double migration_distance_m = 0.0;
    bool cultural_branch = false;  // settled by a DIFFERENT civilization than its source
    bool diaspora = false;
    std::string diaspora_reason;  // empty unless diaspora
    bool diaspora_bonus = false;  // waived a zero quota
};

// Why a parent did or did not place a city in a round. Kept in order; this is the
// record a reader uses to ask why a people is missing from the map.
struct FoundingEvent {
    int turn = 0;
    std::string parent_race_id;
    std::string status;  // founded | diaspora_founded | did_not_participate |
                         // search_radius_too_small | no_separated_origin
    double radius_m = 0.0;
    double draw_percent = -1.0;   // only on did_not_participate
    double suitability = -1.0;    // only on a founding
    int node = -1;                // only on a founding
    std::string population_profile;
};

// The knobs, all authored. Defaults are the recipe's.
struct FoundingRules {
    long long years_per_round = 250;
    double origin_min_separation_degrees = 90.0;
    // Must be an exact multiple of years_per_round. The interval is tested with
    // `year % interval == 0` against a clock that only ever lands on multiples of the
    // round length, so a non-multiple never fires at all -- no diaspora, no error, and
    // no people that depended on the bonus to reach the map. found_cities refuses it.
    long long diaspora_interval_years = 1000;
    // Validated as 0 or 1 and read as a flag, not a count: a parent gets one bonus or
    // none.
    int diaspora_bonus_per_parent = 1;
    long long start_year = 0;
    // Below this many supported residents a civilization cannot found at all.
    long long default_minimum_residents = 40;
    bool magic_enabled = true;
};

struct FoundingRequest {
    std::vector<ParentRace> parents;
    std::map<std::string, std::string> owners;      // civilization -> parent race
    std::map<std::string, long long> quotas;        // civilization -> cities wanted
    std::map<std::string, std::vector<int>> candidates;      // civilization -> eligible nodes
    std::map<std::string, std::vector<double>> scores;       // civilization -> per-node suitability
    std::map<std::string, long long> food_allowances;        // empty means unconstrained
    std::map<std::string, long long> minimum_residents;
    double spacing = 0.0;      // metres cities must stand apart
    double world_reach = 0.0;  // half the circumference: the longest useful distance
    // No ceiling, or a count INCLUDING ZERO. This is deliberately not zero-for-none:
    // that collided with a caller asking for no cities at all, whose quotas went to zero
    // and whose diaspora bonus -- whose whole purpose is to waive a zero quota -- then
    // had nothing left to stop it and settled the world anyway.
    std::optional<long long> city_limit;
    std::vector<FoundedSite> survivors;  // already standing, from an earlier age
    std::vector<int> forbidden;          // ruins and other ground nobody will build on
    std::vector<std::string> diaspora_bonus_used;  // parents that already spent theirs
    std::vector<std::string> used_civilizations;
    FoundingRules rules;
};

struct FoundingReport {
    std::map<std::string, long long> effective_quotas;  // after the food cap and any bonus
    std::vector<std::string> diaspora_bonus_used;
    std::vector<std::string> used_civilizations;
    long long start_year = 0;
    long long end_year = 0;
    long long elapsed_years = 0;
    int turns = 0;
    long long target_cities = 0;
    long long placed_cities = 0;
    std::string stop_reason;  // capacity_target_reached | no_eligible_expansion |
                              // round_limit_reached
    std::vector<FoundingEvent> events;
};

struct FoundingResult {
    std::vector<FoundedSite> sites;
    FoundingReport report;
};

// `distance` is physical metres between two nodes along the surface.
FoundingResult found_cities(std::uint32_t seed, const FoundingRequest& request,
                            const std::function<double(int, int)>& distance);

}  // namespace fwg
