// City fates: what the ley field, the lairs and the powers around a city make of it when an
// age turns, the scar a fallen city leaves, and the standing threat each living city reads
// for its walls.
//
// Three readers share every weight here, and that sharing is the point of the file. The age
// lottery draws against these causes; the threat assessment reports the same nest and ley
// pressures to the city planner, which builds walls from them; and a visitation's own fate
// reuses the ley threat and the ruin legacy. One definition each, so a city's walls answer
// the thing that actually kills cities.
//
// Nothing here reaches into `World` or writes anything. Every pass takes what it reads and
// returns what it decided -- `assess_threats` returns the block rather than writing it --
// and the age transition keeps what it keeps.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/astrology.hpp"
#include "fwg/grid.hpp"
#include "fwg/humans.hpp"
#include "fwg/religion.hpp"
#include "fwg/settlement.hpp"
#include "fwg/villains.hpp"
#include "fwg/wars.hpp"

namespace fwg {

// ---------------------------------------------------------------------------
// Constants. Every one of these is seed-visible; none is a metre figure.
// ---------------------------------------------------------------------------

// Stamped into the block. The city planner reads `regional_threat` as `defense_priority`,
// so a consumer that caches plans against a version needs to know when the meaning moved.
inline constexpr int kThreatAssessmentVersion = 3;

// A dominant school's share of a fate: potency * gain, capped. The cap is a hard ceiling on
// what magic ALONE can contribute to a city's lottery or its threat.
inline constexpr double kThreatLeyGain = 0.45;
inline constexpr double kThreatLeyCap = 0.6;

// A city that has already been to war expects the next one: this much threat per war it
// carries, saturating at three wars. Backward-looking only.
inline constexpr double kThreatWarPerWar = 0.15;
inline constexpr double kThreatWarCap = 0.4;

// Magical self-destruction: a floor of 3.5% an age even on dead ground, rising with the
// Weave under the city. Present only when magic is enabled, and never in the threat
// assessment -- a city does not build walls against its own experiments.
inline constexpr double kFateSelfMagicBase = 0.035;
inline constexpr double kFateSelfMagicWeaveGain = 0.06;

// No city can be more than 90% likely to fall in one age, however many causes stack.
inline constexpr double kFateChanceCap = 0.9;

// A ruin's key-point intensity is its class's (the kClassIntensity table in corruption.hpp),
// floored for a city that destroyed itself and forced for a god's work.
inline constexpr double kRuinSelfMagicFloor = 2.5;
inline constexpr double kRuinDivineIntensity = 4.0;

// ---------------------------------------------------------------------------
// Authored text and the tables behind a ruin's scar.
// ---------------------------------------------------------------------------

// The sentence a city leaves behind when a leyline killed it, from data/ley-destruction.json.
// EMPTY for any school that has none, which is all four hidden ones: generation can never
// raise a hidden school, so none was ever written.
std::string_view ley_destruction_reason(std::string_view school);

// Each civilization's own magic school, its `magic_school` heritage key trait. EMPTY for a
// civilization the registry does not know.
std::string_view culture_school(std::string_view civilization_id);

// What the destroyer brought with it, where it had any magic of its own. Empty means none,
// which is the reference's None.
struct RuinSources {
    std::string nest_family;     // a nest cause's family
    std::string victor_culture;  // a war's victor civilization
    std::string god_school;      // a divine cause's school
    // A villain's school. city_fate hands EVERY chosen cause's evidence `school` to this
    // slot, as the reference does, so a ley cause's own school arrives here too -- harmless,
    // because a school-named cause has already answered before this slot is read.
    std::string villain_school;
};

// The key point a fallen city leaves. THE LEY FIELD IS WHAT READS IT: the age transition
// appends a node `ruin-<uid>-key` of `school` at `intensity` to that school's network.
struct RuinLegacy {
    std::string school;
    double intensity = 0.0;
    // Which fallback answered: source (the destroyer's own school), region (the dominant
    // school under the city) or culture (the fallen people's own).
    std::string basis;
};

// The school the destroyer itself leaves behind, or empty when it has no magic of its own.
//
// A cause that IS a school name -- any of the twelve -- answers first, then self-magic
// (weave), then the divine, villain and war prefixes, and last the nest family. A primordial
// nest scars with the strongest element under the city, a dead tie keeping school order.
std::string source_school(std::string_view cause, const std::vector<double>& potencies,
                          const RuinSources& sources);

// Every ruin seeds a key point: the destroyer's school, else the region's, else the fallen
// culture's, else weave. `potencies` is in school-table order, twelve long.
RuinLegacy ruin_legacy(std::string_view cause, const std::vector<double>& potencies,
                       std::string_view population_profile, CityClass city_class,
                       const RuinSources& sources, double dominance_threshold,
                       double dominance_margin);

// ---------------------------------------------------------------------------
// The moon on the day an age turns.
// ---------------------------------------------------------------------------

// What the lottery reads -- per-school tide factors and the world's lunar influence -- and
// what the age record keeps of the day. One struct because both halves are read off the
// same day: the record's tide IS the factor set the lottery swayed by.
struct CityFateLunar {
    std::array<double, kKnownSchools> tide{};  // school-table order, known eight only
    double influence = 0.0;                    // world option lunar_influence, 0..1

    long long day = 0;
    double phase_fraction = 0.0;
    double elongation_degrees = 0.0;
    double illumination = 0.0;
    std::string leaning;  // still | restless
};

// Absent when the world has no moon (`moon` null). `day` absent means the founding report's
// end year times the calendar year, which is the day the generation-time ages turn on.
std::optional<CityFateLunar> lunar_context(const Moon* moon, std::optional<long long> day,
                                           long long founding_end_year, double lunar_influence);

// The potencies a lottery reads under one city: the base field, swayed by the moon when a
// surge is in force. Twelve values in school-table order.
//
// A missing ley_<school> layer reads ZERO -- not "this city has no school", which would drop
// the key point a charged city is owed. Only the eight KNOWN schools sway: the tide is
// charted against this world's moon, and a school it never named was never in that chart.
// The sway is momentary: it scales what the fate sees and never the exported field.
std::vector<double> city_potencies(const std::map<std::string, Grid>* layers, const WarCity& city,
                                   const std::optional<CityFateLunar>& lunar);

// ---------------------------------------------------------------------------
// The standing threat a city reads for its walls.
// ---------------------------------------------------------------------------

struct ThreatLey {
    std::string school;
    double potency = 0.0;
};

struct ThreatLeyPressure {
    double pressure = 0.0;
    std::optional<ThreatLey> ley;  // absent when no school leads clearly enough
};

// min(cap, potency * gain) for the dominant school, zero where none dominates.
ThreatLeyPressure ley_threat_pressure(const std::vector<double>& potencies, double dominance_threshold,
                                      double dominance_margin);

// One named source of a city's threat. The fields present depend on `kind`.
struct ThreatContributor {
    std::string kind;     // dragon | monster | ley | war
    double weight = 0.0;  // 6 dp
    // dragon | monster
    std::string nest_id;
    std::string name;
    std::string family;
    double distance_m = 0.0;  // 4 dp
    double reach_m = 0.0;     // 4 dp
    // ley
    std::string school;
    double potency = 0.0;
    // war
    int wars_fought = 0;
};

// THE KEY FIELD IS `city_uid`. `site_id` is the renumbering integer, carried for joins
// within one rebuild only.
struct ThreatReport {
    std::string city_uid;
    std::string city_name;
    int site_id = 0;
    // What the city planner reads as defense_priority: nest + ley + war, capped at 1. The
    // forward-looking outlook below does NOT move it -- a forecast does not build walls yet.
    double regional_threat = 0.0;
    double nest_pressure = 0.0;
    double ley_pressure = 0.0;
    double war_pressure = 0.0;
    int wars_fought = 0;
    WarOutlook outlook;
    std::vector<ThreatContributor> contributors;  // nests in id order, then ley, then war
    std::optional<ThreatLey> ley;
    // Written by stage 16's nomad effects AFTER the planners have read regional_threat: the
    // raiders in reach and their pressure. Where any are in reach, regional_threat is raised
    // by it, so a reader after stage 16 sees raids the walls were not built against.
    std::optional<double> nomad_pressure;
    std::vector<std::string> nomad_contributors;  // sorted band uids
};

struct ThreatAssessmentBlock {
    int version = kThreatAssessmentVersion;
    std::string evaluated_after;  // beast_nests at stage 13; the rebuild's label at an age turn
    std::optional<int> age;       // absent at stage 13
    std::vector<ThreatReport> cities;  // uid order
    std::string method;
    std::string limits;
};

struct ThreatRequest {
    std::string evaluated_after;
    std::optional<int> age;
    // The cities in any order: the report is in uid order, which is also the order the plan
    // passes fill cities in. `ground.villains` may be the block's whole roster -- the
    // standing filter is applied here, because a fallen villain still carries a live-looking
    // reach and would feed the outlook from beyond its grave.
    WarGround ground;
    const std::map<std::string, Grid>* layers = nullptr;
    // The beast nests, as the shared nest-threat reader takes them.
    std::vector<ReligionNest> nests;
    // world.magic's published mutation_threshold and dominance_margin; see WarRequest.
    double dominance_threshold = 0.35;
    double dominance_margin = 0.08;
};

// One city's report. Reads the UNSWAYED ley field: the moon's sway is momentary and scales
// only what an age's lottery sees, so a city's walls never answer a surge.
ThreatReport assess_city_threat(const ThreatRequest& request, const WarCity& city,
                                const WarOutlook& outlook);

// The block, for every city in the request. Excludes self-magic.
ThreatAssessmentBlock assess_threats(const ThreatRequest& request);

// ---------------------------------------------------------------------------
// The age lottery.
// ---------------------------------------------------------------------------

// Why a city fell. The fields present depend on the cause; the key order a writer emits
// follows the cause (ley: school, potency, moon_tide; nest: nest_id, family, distance_m,
// reach_m; villain: villain_uid, growth, tier, distance_m, reach_m, school; war: war_id,
// kind, opponent_uid, opponent_civilization_id, opponent_parent_race_id, distance_m,
// pressure; self_magic: nothing).
struct CityFateEvidence {
    std::string school;  // ley; also a villain's school, empty when it holds none
    std::optional<double> potency;
    std::optional<double> moon_tide;  // a ley cause of a KNOWN school, when the moon was read
    std::string nest_id;
    std::string family;
    std::optional<double> distance_m;
    std::optional<double> reach_m;
    std::string villain_uid;
    std::string growth;
    std::optional<double> tier;
    std::string war_id;
    std::string kind;
    std::string opponent_uid;
    std::string opponent_civilization_id;
    std::string opponent_parent_race_id;
    std::optional<double> pressure;
};

// One entry of a city's lottery.
struct CityFateCause {
    std::string kind;  // a school | dragon | monster | self_magic | villain_<growth>
    double weight = 0.0;
    std::string reason;
    CityFateEvidence evidence;
};

// How a city fell: the ruin record's fate fields.
struct CityFate {
    std::string cause;
    std::string reason;
    CityFateEvidence evidence;
    double probability = 0.0;  // the capped chance the roll was drawn against
    double roll = 0.0;
    std::string new_node_school;  // == legacy.school
    RuinLegacy legacy;
};

struct CityFateRequest {
    std::uint32_t seed = 0;
    int age = 0;
    // METRES: world.effective.globe_radius. A villain's reach is a multiple of the settlement
    // spacing and scales with the planet; a lair's reach is authored metres and does NOT, so
    // on a shipped width the nest causes almost never enter the lottery. That is pinned in
    // test_fates.cpp rather than fixed.
    double globe_radius_m = 1.0;
    bool magic_enabled = true;
    const std::map<std::string, Grid>* layers = nullptr;
    std::vector<ReligionNest> nests;
    // Absent when the world has no moon. See lunar_context.
    std::optional<CityFateLunar> lunar;
    // The STANDING cast `advance_villains` returned for this age.
    std::vector<Villain> villains;
    // world.magic's published mutation_threshold and dominance_margin; see WarRequest.
    double dominance_threshold = 0.35;
    double dominance_margin = 0.08;
};

// The lottery's causes, IN WALK ORDER: the dominant ley school, then every eligible nest in
// STRING id order, then self-magic when magic is on, then the villains. The selector walk is
// order-sensitive, so a new cause is APPENDED -- inserting one moves every world.
//
// Throws std::out_of_range when a HIDDEN school dominates the ground: it has no fall reason.
// Generation never raises one; a world the corruption API has touched can.
std::vector<CityFateCause> city_fate_causes(const CityFateRequest& request, const WarCity& city,
                                            const std::vector<double>& potencies);

// One city's age. Absent means it survives.
//
// Exactly ONE draw from `city-fate-<uid>` at variation `age`, taken before survival is
// decided, so every city's stream advances identically whatever it rolls. `roll` replaces
// that draw in tests. The chance is the capped sum; the selector rescales the draw onto the
// UNCAPPED sum and walks the causes in order, so past the cap the lottery still covers every
// cause in proportion.
std::optional<CityFate> city_fate(const CityFateRequest& request, const WarCity& city,
                                  std::optional<double> roll = std::nullopt);

// A city a neighbour took, ruined through the same path as any other fate. The victor's own
// magic scars the ground it took: the legacy is resolved with the victor's culture as the
// source, and its school REPLACES the key-point school `resolve_wars` computed -- so a war
// ruin always seeds a key point, where the wars module alone would seed one only on ground a
// school already held.
CityFate war_ruin_fate(const CityFateRequest& request, const WarCity& city, const WarFate& fate);

// Who a rebuilt city is -- its uid, its name and the culture it was founded by -- is
// remember_cities in settlements_stage.hpp, which runs over the World after a rebuild.

}  // namespace fwg
