// debug_stats: the reference's world_debug.build_debug, as a pure function of a World.
//
// A deterministic diagnostic summary of the finished world and nothing more: it makes no
// simulation decision and nothing reads it back. Per parent race and per civilization, the
// cities, their urban population and the workers and beds their plans placed; per city, its
// nearest neighbour and its plan's stats, placement audit, unplaced buildings and road
// junctions; every distinct founding event the world ever recorded and what became of the
// diaspora cities; the ruins; the settlement timeline across the stages; each ley network's
// size; and the share of the sphere each natural biome covers.
//
// THE TIMELINE AND THE EVENTS NEED THE PAST. The reference reads them out of its per-stage
// snapshots, which its generator always keeps (the CLI strips them only on the way out), so
// a generated reference world's timeline has a row for every stage at which the settlements
// block changed. The port does not keep snapshots unless asked, so generate() records the
// little this needs -- one SettlementStageRecord per such stage -- on World::settlement_stages
// as it runs. "Changed" is decided the way the reference decides it: the settlements block,
// as the document writer renders it, differs from the one last recorded.
//
// REFERENCE BEHAVIOURS KEPT, deliberately:
//   * Founding events are de-duplicated on (status, node, people, parent race, turn,
//     founding year). An event that founded nothing carries none of node, people or year,
//     so two ages' `did_not_participate` rows for one parent in one turn collapse into one:
//     the later record replaces the earlier in the earlier one's position.
//   * The timeline is the recorded stages only, never the finished world itself.
//   * Biome coverage weights each row by its latitude band's area and drops the duplicate
//     seam column; the pole rows' bands are clipped at the poles.
//
// DIFFERENT, and why:
//   * An age advanced by advance_ages() adds no rows: it runs outside generate(), and the
//     reference adds them only when the world it advances still holds its build_stages.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fwg/ages.hpp"       // Ruin
#include "fwg/city_plan.hpp"  // PlanStats, PlanDebug, Unplaced
#include "fwg/founding.hpp"   // FoundingEvent
#include "fwg/magic.hpp"      // NetworkDistribution

namespace fwg {

struct World;

inline constexpr int kDebugStatsVersion = 1;

// One stage at which the settlements block changed.
struct SettlementStageRecord {
    int stage = 0;
    std::string title;
    long long cities = 0;
    long long urban_population = 0;
    // The founding report's events as they stood at that stage.
    std::vector<FoundingEvent> founding_events;
    // SHA-256 of the settlements block as the document writes it.
    std::string fingerprint;
};

// Called by generate() after every stage: appends a record when the world has settlements
// and their block differs from the last one recorded. A no-op before stage 10.
void record_settlement_stage(World& world, int stage);

struct DebugGroup {
    std::string id;
    long long cities = 0;
    long long urban_population = 0;
    long long workers = 0;
    long long beds = 0;
};

struct DebugCity {
    std::string uid;
    std::string name;
    std::string civilization;
    std::string parent_race;
    long long urban_population_estimate = 0;
    std::optional<double> nearest_city_m;  // absent when the city stands alone
    // The city's plan, when stage 16 planned it; the reference writes {} and [] otherwise.
    bool planned = false;
    PlanStats stats;
    PlanDebug placement;
    std::vector<Unplaced> unplaced;
    std::vector<std::pair<std::string, long long>> junctions;  // status -> count, first seen first
};

struct DebugTimelineRow {
    int stage = 0;
    std::string title;
    long long cities = 0;
    long long urban_population = 0;
};

struct DebugLeyline {
    std::string id;
    long long nodes = 0;
    long long lines = 0;
    std::optional<NetworkDistribution> distribution;  // absent for an unmanifested network
};

struct DebugBiomeCoverage {
    int id = 0;
    std::string name;
    double percent_world = 0.0;
};

struct DebugStats {
    int version = kDebugStatsVersion;
    std::string_view scope = "Final completed state; population is urban estimate, beds cover placed workers only.";
    std::vector<DebugGroup> parent_races;   // first seen first
    std::vector<DebugGroup> civilizations;  // first seen first
    std::vector<DebugCity> cities;          // settlement order
    long long diaspora_founded = 0;
    long long diaspora_surviving = 0;
    long long diaspora_destroyed = 0;
    std::vector<std::pair<std::string, long long>> founding_outcomes;  // status -> count
    std::vector<FoundingEvent> founding_events;                        // de-duplicated
    std::vector<Ruin> ruins;
    std::vector<DebugTimelineRow> timeline;
    std::vector<DebugLeyline> leylines;             // school-table order
    std::vector<DebugBiomeCoverage> biome_coverage;  // ascending biome id
};

// Pure: reads the world and its settlement_stages, writes nothing.
DebugStats build_debug(const World& world);

}  // namespace fwg
