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
#include <span>
#include <string_view>
#include <string>
#include <vector>

namespace fwg {

// A parent race: the thing that owns civilizations and takes a turn.
struct ParentRace {
    std::string_view id;
    std::string_view name;
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
    double required_separation_degrees = -1.0;  // only on no_separated_origin
    double draw_percent = -1.0;   // only on did_not_participate
    double suitability = -1.0;    // only on a founding
    int node = -1;                // only on a founding
    std::string population_profile;

    // [document: founding events] On a founding only: the site it placed, as the reference's
    // event carries it -- its event IS the founded site's record with the round's fields added.
    // The three diaspora fields are published on a diaspora founding only.
    int founding_turn = 0;
    bool founding_capital = false;
    long long founding_year = 0;
    int migration_source_node = -1;      // -1 for a capital
    std::string source_civilization_id;  // empty for a capital
    double migration_distance_m = 0.0;
    bool cultural_branch = false;
    bool diaspora = false;
    std::string diaspora_reason;
    bool diaspora_bonus = false;
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
    // The rules the rounds ran under, recorded as the reference's record carries them: each
    // competing people's minimum supported residents, the round length, the diaspora interval
    // and the separation a race's first city keeps from the other races' capitals.
    std::map<std::string, long long> minimum_residents;
    long long years_per_round = 0;
    long long diaspora_interval_years = 0;
    double origin_min_separation_degrees = 0.0;
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

// The authored parent races, in registry order. THE ORDER IS A CONTRACT: founding
// rotates initiative through this table by turn, so reordering it changes which parent
// claims a contested site first.
std::span<const ParentRace> parent_races();

// The authored founding rules.
FoundingRules authored_founding_rules();

// Registry-wide defaults that are not per-profile.
struct RegistryDefaults {
    std::string_view profile_id;            // the single people a non-mixed world uses
    std::string_view aggregate_profile_id;  // the name of "every eligible people at once"
    // The key a site's resource contribution is published under, for EVERY people --
    // not only the one the name suggests.
    std::string_view resource_score_factor_label;
    std::string_view normal_site_reason;
    double medium_suitability_min = 0.0;  // above this a city is medium rather than small
};
RegistryDefaults registry_defaults();

// `distance` is physical metres between two nodes along the surface.
FoundingResult found_cities(std::uint32_t seed, const FoundingRequest& request,
                            const std::function<double(int, int)>& distance);

}  // namespace fwg
