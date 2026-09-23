// Stage 16: the travelling population of a finished world.
//
// The land decides who walks it. A thinned point process proposes start points over
// habitable land, the ground around each one is read, and a classification whose
// PRECONDITIONS FAIL IS INELIGIBLE OUTRIGHT whatever its weight. A candidate that
// satisfies no gate raises nobody, and that is a result rather than a failure. The
// inverse -- roll a kind, then find somewhere to put it -- produces cults where nothing
// is sacred, caravans with no road and no market, and bandits in open country with
// nothing to rob; those are not balance bugs that tuning fixes, they are the feature
// being wrong.
//
// Three passes in a fixed order: `add_nomads` places and classifies, `add_nomad_routes`
// builds circuits and then splits clans that outgrew their round, `apply_nomad_effects`
// computes what the bands write back. A fourth entry point, `absorb_survivor_camps`,
// belongs to the age boundary rather than to the effects pass.
//
// EVERY GATE RETURNS A SCORE IN 0..1, so `weight` in data/nomads.json is the only balance
// lever. Unnormalised scores made that table a lie: ley intensity runs to 4 while a
// bandit's score sat near 0.5, so cultists won almost every contested draw no matter what
// the weights said.
//
// EVERY REACH IS A MULTIPLE OF SETTLEMENT SPACING, never a metre constant, so world scale
// and raster size cannot turn a local band into a regional one. Travel speed is the one
// deliberate absolute: a walking pace belongs to the traveller, not to the world.
//
// Nothing here touches a World. Each pass takes a request and returns a value, so the
// stages that produce hamlets, ruins, villains, roads and threat assessments can land
// later without this file changing shape.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fwg/grid.hpp"
#include "fwg/magic.hpp"
#include "fwg/vec3.hpp"

namespace fwg {

class Rng;

// The three sub-blocks version independently, and that split is the precise statement of
// what changed. `routes` moved to 2 when lineage fission landed -- no field was new, but
// `parent_uid` stopped being null on every band -- and to 3 when no merchant leg was
// allowed to exceed one day's march, which changes what a consumer that sized a day's
// travel off the longest leg reads.
inline constexpr int kNomadsVersion = 1;
inline constexpr int kNomadRoutesVersion = 3;
inline constexpr int kNomadEffectsVersion = 1;

// ---------------------------------------------------------------------------
// The classification order.
//
// THIS ENUM IS THE CONTRACT, and it is carried in CODE rather than read from
// data/nomads.json. The reference walks a Python tuple and its loader checks the file's
// key SET only, so re-sorting the JSON is a no-op while reordering the tuple renumbers
// anything that stored an index into it. The gate table below is indexed by this enum and
// iterated in enum order, so the evaluation order is structural rather than remembered.
//
// APPEND, NEVER INSERT. Inserting changes the order shares accumulate in, which changes
// every weighted draw, which changes every world.
// ---------------------------------------------------------------------------
enum class NomadClass : int {
    Survivors = 0,
    Deserters,
    Cultists,
    Merchants,
    Bandits,
    Wanderers,
};
inline constexpr int kNomadClassCount = 6;

std::string_view to_string(NomadClass value);
// -1 for a name outside the six.
int nomad_class_index(std::string_view name);

// `calving` is in the schema's enum and in the rest table but NO nomad builder produces
// one -- it belongs to the beast-movement sibling. Kept for schema compatibility; its
// rest share is unreachable ground.
enum class NomadCampKind : int { Winter = 0, Summer, Base, Station, Lair, Shrine, Terminal, Calving };
std::string_view to_string(NomadCampKind value);

// `fission` is the ONLY branch whose `to` names a camp of a DIFFERENT band.
enum class NomadBranch : int { Trunk = 0, BaseLimb, Fission, Spur };
std::string_view to_string(NomadBranch value);

enum class NomadDisposition : int { Hostile = 0, Wary, Tradeable, Devout, Fleeing };
std::string_view to_string(NomadDisposition value);

// Stranded is a REAL OUTCOME, not an error: the wanderer gate checks forage and seasonal
// swing but not whether summer and winter ground are mutually REACHABLE, which needs the
// route pass's cost function -- so a herder with nowhere to winter is discovered late and
// reported rather than pretended away.
enum class NomadRouteStatus : int { Stranded = 0, Routed };
std::string_view to_string(NomadRouteStatus value);

// What a CELL is. A salt lake gets its own label so that "not land" and "marine" stay
// different answers; only `Land` raises a band or carries a walked leg.
enum class NomadMedium : int { Land = 0, Marine, Freshwater, SaltLake };

// World generation always writes `world`; the request API is what raises a band on a
// god's or a villain's account.
enum class NomadOriginKind : int { World = 0, God, Villain };
std::string_view to_string(NomadOriginKind value);

// ---------------------------------------------------------------------------
// The balance table, generated from data/nomads.json into src/generated.
//
// NO IDENTITY IS STAMPED ON IT. Unlike the castle and pantheon catalogues nothing hashes
// this document and nothing compares it on reopen, so editing these numbers silently
// changes new worlds while saved worlds keep their old bands and never notice. That is
// inherited from the reference and recorded rather than fixed: inventing a hash here
// would be a new contract, not a port.
// ---------------------------------------------------------------------------

// The union of every gate key any classification reads, typed.
//
// The reference reads these out of a free-form dict with a fallback per call site, so a
// key missing from the file silently becomes the fallback. The defaults here ARE those
// fallbacks, and the codegen additionally refuses a file whose classification has lost a
// key its own gate reads -- so the two halves of that behaviour are split: a key the gate
// requires fails the build, a key only a route builder wants falls back as it always did.
struct NomadGate {
    // survivors
    double ruin_reach_spacings = 0.9;
    double refuge_reach_spacings = 2.5;
    // deserters
    double war_reach_spacings = 0.8;
    // deserters and bandits both, with different numbers
    double min_ruggedness = 0.0;
    // cultists
    double min_ley_intensity = 0.0;
    double ley_reach_spacings = 0.35;
    double shrine_reach_spacings = 0.8;
    double claim_reach_spacings = 1.2;
    // merchants
    double road_reach_spacings = 0.35;
    double market_reach_spacings = 3.0;
    int min_markets = 2;
    double min_market_population = 0.0;
    // bandits
    double strike_reach_spacings = 1.3;
    double control_clear_spacings = 0.9;
    // wanderers
    double min_forage = 0.0;
    double min_seasonal_swing = 0.3;
    double summer_reach_spacings = 3.5;
    double winter_reach_spacings = 3.5;
};

struct NomadClassPolicy {
    std::string_view name;
    double weight = 1.0;
    NomadDisposition disposition = NomadDisposition::Wary;
    // ABSOLUTE metres per day, the one deliberate non-scaled distance.
    double speed_m_per_day = 1.0;
    int size_low = 1;
    int size_high = 1;
    // A caravan strings out six metres per head because of the pack animals; a refugee
    // column 1.4. So a player meeting one on the move sees a column, not a point.
    double column_length_per_head_m = 0.0;
    std::span<const std::string_view> seeks;
    std::span<const std::string_view> carries;
    NomadGate gate;
};

struct NomadCandidatePolicy {
    double density_per_1000km2 = 18.0;
    // The distance over which the placement rate ramps from 0 at a settlement to full.
    double settlement_clearance_spacings = 0.3;
    // DEAD. No nomad code in the reference reads it, and implementing a suitability floor
    // would change every world's band count against no reference behaviour. Carried for
    // fidelity of the data file and never consulted.
    double min_suitability = 0.12;
};

struct NomadFissionPolicy {
    std::span<const NomadClass> classifications;
    // The round forage at which a round carries the TOP of the size band. Calibrated, not
    // derived, and it must EXCEED the gate's own `min_forage` or the capacity curve
    // divides by zero width and every band of that class is always or never over capacity.
    double rich_forage = 0.55;
    double child_share_low = 0.3;
    double child_share_high = 0.45;
};

struct NomadPolicy {
    int version = 2;
    NomadCandidatePolicy candidate;
    NomadFissionPolicy fission;
    // IN THE ORDER OF THE NomadClass ENUM, which is what makes indexing by the enum sound.
    std::span<const NomadClassPolicy> classifications;
};

const NomadPolicy& nomad_policy();

// Convenience, and the only sanctioned way to reach a row: the index IS the enum.
const NomadClassPolicy& nomad_class_policy(NomadClass value);

// ---------------------------------------------------------------------------
// Constants the ALGORITHM owns rather than the data file. Single-value tuning numbers
// deliberately not extracted, because a balance table with one entry is a worse home for
// them than the code that is the only reader.
// ---------------------------------------------------------------------------

// A laden pack animal makes 30-40 km in a day, which is why caravan stations sat that far
// apart. ABSOLUTE metres and NOT scaled by settlement spacing, so it is ~4.3 spacings on
// a 200 km world and ~1.4 on a 600 km one.
inline constexpr double kNomadDayMarchM = 35000.0;

// Multiplier on `road_max_grade` for the walking ceiling. Herds and refugees cross ground
// roads refuse: steeper, and they ford rather than bridge. Toward 1.0 they route like
// roads and mountain herders strand.
inline constexpr double kNomadGradeRelief = 2.2;

// Midsummer in the NORTHERN hemisphere as a FRACTION of the year; the south is half a
// year away. A fraction rather than a day so this stays correct if the calendar changes.
inline constexpr double kNomadMidsummerNorth = 0.5;

inline constexpr int kNomadDaysPerYear = 360;

// What a circuit leaves behind: a pilgrimage deepens a groove that was already there, so
// the gain is a fraction of the node's existing charge rather than a flat addition.
inline constexpr double kNomadDevotionGain = 0.18;
inline constexpr double kNomadIntensityCeiling = 4.0;

// The reach within which a raider is felt, in settlement spacings. DOUBLE the bandit
// gate's own `control_clear_spacings` and wider than its `strike_reach_spacings`, because
// being felt and being in strike range are different questions.
inline constexpr double kNomadStrikeSpacings = 2.0;

// How long a band lingers, BY WHAT THE CAMP IS FOR, as a share of the spare year.
// Splitting the year evenly across camps put a bandit at the hamlet it had raided for
// four months, which is not a raid.
double nomad_camp_rest(NomadCampKind kind);

// Some stops are EVENTS, not seasons, however much of the year is left to divide. A kind
// with no ceiling returns nullopt, which the caller reads as "no ceiling" rather than as
// zero -- the reference passes `held` as the default so the min is with itself.
std::optional<int> nomad_camp_rest_ceiling(NomadCampKind kind);

// A raider's contribution to a town's sense of threat before distance falls off. Zero for
// the four classifications that are not raiders, which is also how the raider filter is
// written: a weight of zero is not a raider.
double nomad_raid_weight(NomadClass value);

// ---------------------------------------------------------------------------
// What the gates read. Every record the pass locates is supplied by the caller, because
// the stages that produce them are not ported yet and a generic JSON tree would lose the
// join the `basis` exists to publish.
// ---------------------------------------------------------------------------

// A settlement, a hamlet or a port.
//
// `uid` is the stable handle. `id` is the ORDINAL one (`hamlet-7`), which the reference
// uses as the `wealth` list's tie-break key for hamlets -- and hamlets are re-sorted by
// defence score at every age, so that key names different ground after an advance. Both
// are carried because both are read, in different places.
struct NomadSite {
    std::string uid;
    std::string id;
    int node = -1;
    int x = -1;
    int z = -1;
    // Set it. A record with no direction is resolved from (x, z) or from `node` wherever
    // the reference's `_where` chain applies -- but `war_direction` reads this field RAW,
    // as the reference does, so a deserter fleeing a site that carries no direction gets
    // no lead term and runs on pure ruggedness. Loud here rather than silent in a world.
    std::optional<Vec3> direction;
    // `population_of`'s chain, in order. A hamlet carries no head count at all and is
    // sized by the food it delivers -- WITHOUT that fallback a hamlet scores zero and
    // bandits ignore the rural settlements that are their easiest prey. The units are
    // therefore inconsistent by construction: for a hamlet this is a yield, not people.
    std::optional<double> population_estimate;
    std::optional<double> urban_population_estimate;
    std::optional<double> rural_population_estimate;
    std::optional<double> delivered_food;
    // Recorded engagements. Zero means NO war history, which is what keeps the record out
    // of the deserter gate's candidate list entirely.
    int war_history = 0;

    // Written by absorb_survivor_camps; read by it for idempotence.
    std::vector<std::string> absorbed_bands;
    long long absorbed_refugees = 0;
};

struct NomadRuin {
    std::string uid;
    std::string id;
    int node = -1;
    int x = -1;
    int z = -1;
    std::optional<Vec3> direction;
    int destroyed_age = 0;
    int war_history = 0;
    // Absent reads 1.0, giving scar 0.25. The 4.0 divisor is the ley ceiling reused,
    // because a ruin's legacy intensity lives on the same scale.
    std::optional<double> legacy_intensity;
};

// One key point of one network, flattened. The reference walks `magic.networks` in DICT
// INSERTION ORDER and then each network's node list; use `nomad_ley_nodes` rather than a
// std::map walk, which would be alphabetical and is a different list.
struct NomadLeyNode {
    std::string school;
    std::string id;
    Vec3 direction;
    double intensity = 0.0;
};

// Flatten a magic block into the ley list, in MAGIC SCHOOL TABLE ORDER -- the known eight
// then the hidden four -- with any network the table does not name appended after it in
// map order, so nothing is silently dropped.
std::vector<NomadLeyNode> nomad_ley_nodes(const MagicReport& magic);

struct NomadShrine {
    std::string id;
    std::string god_id;
    int node = -1;
    int x = -1;
    int z = -1;
    std::optional<Vec3> direction;
};

// Read by `_god_for_school`, which walks the WORLD's religion block rather than the
// authored pantheon: that file does not carry the four hidden gods, so a blood cult --
// the exact case a villain of the blood god creates -- would fail to find its patron.
struct NomadGod {
    std::string id;
    // Already folded: the god's own `schools`, or the single `exists.school` when it has
    // none. FIRST MATCH IN THIS LIST ORDER WINS.
    std::vector<std::string> schools;
};

// One villain's claim on ground, flattened with the holder's own fields.
//
// DELIBERATELY NOT filtered to standing villains: held ground outlives its holder, so a
// cult raised on a claim does not disband because the villain that staked it fell.
struct NomadClaim {
    std::string id;
    int node = -1;
    int x = -1;
    int z = -1;
    std::optional<Vec3> direction;
    // 1.0 for a living holder and the decayed value once its holder goes. Reading it off
    // the claim keeps the decay question in one place instead of in every consumer.
    double influence = 1.0;
    std::string villain_uid;
    std::string villain_school;
    // VILLAIN POWER LEVEL, a float -- not a beast-nest danger tier and not a key-location
    // scale. Floored at 1.0 where it is read.
    double villain_tier = 1.0;
};

struct NomadRoadRoute {
    std::vector<int> nodes;
    double length_m = 0.0;
};

// One row of `threat_assessments.cities`, in the order that block publishes them.
struct NomadThreatCity {
    std::string city_uid;
    double regional_threat = 0.0;
};

// ---------------------------------------------------------------------------
// Ground, per cell.
// ---------------------------------------------------------------------------

// Named NomadHabitatCell because nests.hpp already owns `HabitatCell` for a creature's
// reading of the same ground. The two carry different fields and are unrelated.
struct NomadHabitatCell {
    int node = 0;
    int x = 0;
    int z = 0;
    double area_km2 = 0.0;
    int biome = 0;
    Vec3 direction;
    NomadMedium medium = NomadMedium::Land;
    double slope_deg = 0.0;
    double tpi_m = 0.0;
    double height_m = 0.0;
    double temperature_c = 0.0;
    double food_potential = 0.0;
    double natural_food_potential = 0.0;
};

// One entry per graph node, in NODE ORDER -- which is the RNG stream order of the
// placement pass and therefore load-bearing.
//
// A layer this world does not carry reads 0.0, exactly as the reference's dict lookup
// does. `biome` is `natural_biome` when that LAYER EXISTS, even where its value is zero,
// and `biome` only when the layer is absent entirely; an `or` there would emit a
// different biome on every cell whose natural biome is 0, and that value is copied onto
// every band and every fission child.
std::vector<NomadHabitatCell> nomad_habitat_cells(const std::map<std::string, Grid>& layers,
                                                  const SphereGrid& grid);

// Summer-to-winter spread at one cell, 0..1. Fewer than two months, or any month missing
// its grid, reads 0.0 -- which fails the wanderer gate's 0.3 floor, so A WORLD GENERATED
// WITHOUT THE SEASONAL PASS RAISES NO HERDERS AT ALL and will not say why.
//
// The monthly grids already carry the reversal across the equator, so a southern cell
// swings on the opposite half of the year and this needs no hemisphere logic of its own.
double nomad_seasonal_swing(const std::vector<const Grid*>& months, int x, int z);

// How much cover the ground gives. Slope carries it and the topographic position index
// sharpens it: a ridge or a ravine hides a band that open ground of the same steepness
// does not. Range [0, 1.35] -- the gates compare the UNCLAMPED value against
// `min_ruggedness` and multiply the CLAMPED one into the score.
double nomad_ruggedness(const NomadHabitatCell& cell);

// A settlement's weight as a target or a market, in people -- except for a hamlet, where
// the chain ends at a food yield. Deliberately population rather than `city_class`, which
// is a label with no ordering to compare against.
double nomad_population_of(const NomadSite& site);

// ---------------------------------------------------------------------------
// The evidence that classified a band. This is the join back into the rest of the world
// and the field a quest compiler should build on, so it is typed rather than a free-form
// map: a consumer resolves `ruin_uid` against BOTH `ruins` and `settlements.sites`,
// because an age advance moves a ruined city from one into the other.
// ---------------------------------------------------------------------------
struct NomadBasis {
    NomadClass owner = NomadClass::Survivors;

    // survivors
    std::string ruin_uid;
    double ruin_distance_m = 0.0;
    std::string refuge_uid;
    double refuge_distance_m = 0.0;

    // deserters
    std::string war_site_uid;
    double war_distance_m = 0.0;
    double ruggedness = 0.0;
    int engagements = 0;

    // cultists -- exactly one of the three arms is live, and `school`/`god_id` ride all
    // three. A claim-derived or shrine-derived cult carries NO ley_node_id, which is what
    // the ley write-back checks before it deepens anything.
    std::string ley_node_id;
    double ley_distance_m = 0.0;
    double ley_intensity = 0.0;
    std::string shrine_id;
    double shrine_distance_m = 0.0;
    std::string claim_id;
    std::string villain_uid;
    double claim_distance_m = 0.0;
    std::string school;
    std::string god_id;

    // merchants
    std::vector<std::string> markets;  // up to four, nearest first
    int market_count = 0;
    double nearest_market_m = 0.0;
    double road_distance_m = 0.0;
    int road_node = -1;

    // bandits
    std::string prize_uid;
    double prize_distance_m = 0.0;
    double nearest_seat_m = 0.0;

    // wanderers
    double forage = 0.0;
    double seasonal_swing = 0.0;
    double openness = 0.0;

    // A fission child alone carries the parent camp it took over.
    std::string inherited_camp;
};

// ---------------------------------------------------------------------------
// What a pass produces.
// ---------------------------------------------------------------------------

struct NomadOrigin {
    NomadOriginKind kind = NomadOriginKind::World;
    std::string id;  // empty for a world-raised band
    int age = 0;
};

// A lair or a base is a place you can find and raid; a station or a seasonal camp is empty
// most of the year. That is why a quest's durable handle is the BAND uid and not this id.
struct NomadCamp {
    std::string id;  // "<band uid>-camp-<index>"
    int node = 0;
    Vec3 direction;
    NomadCampKind kind = NomadCampKind::Base;
    std::optional<int> arrive_day;
    std::optional<int> depart_day;
};

struct NomadLeg {
    std::string from;  // camp ids; a leg has no id of its own
    std::string to;
    std::vector<int> nodes;  // a contiguous walk: consecutive entries are graph neighbours
    // The GREAT-CIRCLE walk length, NOT the Dijkstra cost -- the two differ by the grade
    // and forage factors, and a consumer that reads one as the other overstates the walk.
    double length_m = 0.0;
    std::optional<int> depart_day;
    std::optional<int> arrive_day;
    NomadBranch branch = NomadBranch::Trunk;
};

struct NomadBand {
    // "nomad-<age>-<node>", with "-<draw index>" when a cell raises more than one. KEYED
    // TO THE TERRAIN NODE, never to an ordinal or a culture id: those renumber at an age
    // boundary and a band has to survive one.
    std::string uid;
    NomadClass classification = NomadClass::Survivors;
    std::string parent_uid;  // empty for a placed band, the parent's uid for a child
    NomadOrigin origin;
    std::string god_id;
    std::string school;
    int node = 0;
    int x = 0;
    int z = 0;
    Vec3 direction;
    int biome = 0;
    int size = 0;
    double speed_m_per_day = 0.0;
    double column_length_m = 0.0;
    NomadDisposition disposition = NomadDisposition::Wary;
    std::span<const std::string_view> seeks;
    std::span<const std::string_view> carries;
    std::vector<NomadCamp> camps;  // at least one
    std::vector<NomadLeg> legs;    // empty until the route pass
    NomadBasis basis;
    NomadRouteStatus route_status = NomadRouteStatus::Stranded;
    // Set by the route pass, EXCLUDING any fission leg: that leg happened once and is not
    // walked every year, so counting it would tell a time mover the clan travels further
    // than it does.
    std::optional<double> round_length_m;
};

// The diagnostic ledger, in candidate-evaluation order. A fission child and an
// API-raised band have NO roll, which is why the invariant a consumer can hold is that
// every band either names a roll or names a parent that does.
struct NomadRoll {
    int node = 0;
    int x = 0;
    int z = 0;
    std::vector<NomadClass> eligible;  // in NomadClass order
    std::vector<NomadBasis> evidence;  // parallel to `eligible`
    bool precipitated = false;
    double roll = 0.0;  // the uniform draw in [0, total); meaningful only when precipitated
    NomadClass chose = NomadClass::Survivors;
    std::string uid;
};

struct NomadRoutes {
    int version = kNomadRoutesVersion;
    int routed = 0;    // `fissioned` is counted INSIDE this
    int stranded = 0;  // routed + stranded == groups.size() is an invariant
    int fissioned = 0;
    double day_march_m = kNomadDayMarchM;
    std::string_view method;
    std::string_view limits;
    double elapsed_ms = 0.0;
};

struct NomadBlock {
    int version = kNomadsVersion;
    std::vector<NomadBand> groups;  // sorted by uid, a LEXICOGRAPHIC string sort
    // All six always present, so a quiet world is distinguishable from a broken pass.
    // RECOMPUTED by the route pass rather than carried, because three callers add to the
    // band set and a tally that is only ever incremented cannot survive a pass that
    // removes anybody.
    std::array<int, kNomadClassCount> counts{};
    std::vector<NomadRoll> rolls;
    std::string_view method;
    std::string_view limits;
    std::optional<NomadRoutes> routes;
    double elapsed_ms = 0.0;
};

// ---------------------------------------------------------------------------
// The write-backs, returned rather than applied. Each one names exactly what a caller
// must change, so the pass stays a function of its request.
// ---------------------------------------------------------------------------

// A cult may only DEEPEN a node that already exists, never create one -- only the
// corruption API seats a node in a hidden network.
struct NomadLeyEdit {
    std::string school;
    std::string node_id;
    double intensity = 0.0;  // the value to SET, not to add
    bool hidden = false;     // the school is outside the known eight
};

// One row per city that resolved to a site with a direction. `nomad_pressure` is written
// even when it is zero; the other two only when at least one raider is in reach.
struct NomadRaidPressure {
    std::string city_uid;
    double nomad_pressure = 0.0;
    std::vector<std::string> contributors;  // sorted uids; empty when nothing is in reach
    std::optional<double> regional_threat;  // the RAISED value, capped at 1.0
};

struct NomadCaravanTrade {
    std::size_t route_index = 0;
    std::vector<std::string> riders;  // sorted merchant uids
    double throughput = 0.0;          // a share of nodes ridden, not a cargo volume
};

// A survivor band that reached its refuge and stopped is exactly how a hamlet starts. It
// is a CANDIDATE rather than a founding, because founding one here would mean re-running
// settlement generation after every downstream block has read the settlements it produced.
//
// The candidate does NOT name free ground: `_flight` routes the band to the node of the
// site whose uid is `basis.refuge_uid`, so the terminal camp is the refuge city's own
// node. Adopting one as a settlement would found a second town on a standing city.
struct NomadSettlementCandidate {
    std::string id;  // "settlement-candidate-<band uid>"
    std::string from_band;
    int node = 0;
    Vec3 direction;
    int migration_source_node = 0;
    double migration_distance_m = 0.0;
    long long population_estimate = 0;
    std::string reason;
    // Written by absorb_survivor_camps at an age boundary.
    std::optional<int> absorbed_age;
    std::string absorbed_into;
};

struct NomadEffects {
    int version = kNomadEffectsVersion;
    std::vector<NomadLeyEdit> leyline_edits;     // in band uid order
    int leyline_edits_known = 0;
    int leyline_edits_hidden = 0;
    std::vector<NomadRaidPressure> raid_pressure;
    int towns_under_raid_pressure = 0;
    std::vector<NomadCaravanTrade> caravan_trade;
    std::vector<NomadSettlementCandidate> settlement_candidates;  // sorted by id
    // The reference ASSIGNS the candidate list only when it is non-empty, so a world can
    // carry a previous pass's list unchanged -- which is exactly why absorption has to be
    // idempotent by band uid. False here means "leave what is there alone".
    bool assign_settlement_candidates = false;
    std::string_view method;
    std::string_view limits;
    double elapsed_ms = 0.0;
};

// ---------------------------------------------------------------------------
// Running the passes.
// ---------------------------------------------------------------------------

struct NomadRequest {
    // The stage gate, reproduced as an ASYMMETRY on purpose: placement and effects refuse
    // below phase 16 or without a recipe; the route pass does NOT, because it is callable
    // standalone by the request API and by the monthly tick and a gate there breaks both.
    bool world_recipe = true;
    int phase = 16;

    std::uint32_t seed = 0;
    int size = 0;  // the raster width, n

    // METRES, POST-SCALING. Every nomad reach is a multiple of this, so it is already an
    // absolute metre figure by the time it arrives and needs no further scaling.
    double settlement_spacing_m = 0.0;

    // METRES. This is world.effective.globe_radius, NOT world.config's -- the two differ
    // by the world scale, a factor of 5.6 on the default world, and `grid` must be built
    // at the same number or every reach compares metres against design units.
    double globe_radius_m = 1.0;

    // DIMENSIONLESS rise over run, bounded (0.01, 1.0) and taken from the civilization
    // profile's road grade limit. The walking ceiling is this times kNomadGradeRelief, so
    // roughly 0.77 to 1.21 across the shipped profiles.
    double road_max_grade = 0.4;

    // Overall multiplier on band count; 0 leaves the world settled.
    double occurrence = 1.0;
    // Bands per 1000 km2 of habitable land. Falls back to the policy's own density.
    std::optional<double> density;
    // Independent nomad seed variation, 0..4294967295.
    std::int64_t variation = 0;
    bool magic_enabled = true;

    const SphereGrid* grid = nullptr;
    const std::vector<NomadHabitatCell>* cells = nullptr;

    std::vector<NomadSite> sites;    // settlements.sites, in list order
    std::vector<NomadSite> hamlets;  // humans.hamlets
    std::vector<NomadSite> ports;    // fisheries.ports -- reaches the clearance list only
    std::vector<NomadRuin> ruins;
    std::vector<NomadLeyNode> ley;
    std::vector<NomadShrine> shrines;
    std::vector<NomadGod> gods;
    std::vector<NomadClaim> claims;
    std::vector<NomadRoadRoute> roads;
    std::vector<int> ages;  // history.ages[].age; the largest is the band uid's age
    std::vector<NomadThreatCity> threat_cities;
    // Twelve monthly temperature grids, in month order. Null entries are tolerated and
    // read as "no seasonal data", which silences every herder in the world.
    std::vector<const Grid*> seasonal_temperature;
};

// Place and classify. Nullopt when the stage gate refuses, which is not an error: no
// `nomads` block exists below phase 16 and one does at it.
//
// Throws when a settled record carries neither a direction nor a grid position -- the
// reference raises at the same point, and resolving it through the wider `_where` chain
// would silently move the clearance ramp.
std::optional<NomadBlock> add_nomads(const NomadRequest& request);

// Give every placed band the circuit its classification implies, then split the clans
// that outgrew their round. Mutates the block: `groups` loses its children and regains
// freshly derived ones, every band gains a route status, and `counts` is recomputed.
//
// NO PHASE GATE, deliberately. Returns an all-zero summary for a block with no bands.
NomadRoutes add_nomad_routes(NomadBlock& block, const NomadRequest& request);

// What the bands write back. Nullopt when the stage gate refuses.
std::optional<NomadEffects> apply_nomad_effects(const NomadBlock& block, const NomadRequest& request);

// Grow the refuge a survivor band walked to, at an age boundary, instead of founding a
// town on top of it. `survivors` are the cities that SURVIVED the boundary, so a refuge
// that fell this age absorbs nobody and the arrival is simply not recorded.
//
// IDEMPOTENT BY BAND UID, and it has to be: the effects pass only assigns the candidate
// list when it has candidates, so a world can carry a previous advance's list unchanged
// and absorbing it again would breed people every age out of one migration.
//
// The count MUST ride `absorbed_refugees` rather than `population_estimate`: the
// settlement rebuild re-derives the population estimate from the budget on every rebuild,
// so a number written into it before the rebuild is erased by the rebuild meant to carry
// it. The budget adds `absorbed_refugees` back AFTER the capacity split.
struct NomadAbsorption {
    std::string candidate_id;
    std::string band_uid;
    std::string refuge_uid;
    long long people = 0;
};
struct NomadAbsorptionResult {
    std::vector<NomadAbsorption> absorbed;  // in candidate id order
    long long people = 0;
};
NomadAbsorptionResult absorb_survivor_camps(std::vector<NomadSettlementCandidate>& candidates,
                                            const std::vector<NomadBand>& groups,
                                            std::vector<NomadSite>& survivors, int age);

// ---------------------------------------------------------------------------
// The pieces, exposed because a test that can only reach them through a whole pass
// cannot say which of six gates refused a cell, and because a caller outside a pass asks
// the same questions.
// ---------------------------------------------------------------------------

// Everything the gates read, resolved ONCE for a request rather than per candidate.
//
// Holds pointers into the request's own vectors, so it must not outlive the request.
struct NomadGround {
    struct Place {
        Vec3 where;
        std::string key;  // the `_nearest` tie-break, a STRING in every producer
        // Position in the list this one was built from -- the request's own vector for
        // the homogeneous lists, the parallel payload below for the two mixed ones, and
        // THE GRID NODE for `roads`, whose payload in the reference is the node itself.
        int index = -1;
    };
    // Both halves of the deserter gate's read, resolved: the `wars` list is the one place
    // a ruin and a site are interchangeable.
    struct WarSite {
        std::string uid;
        int engagements = 0;
    };

    double spacing = 0.0;
    double radius = 1.0;
    int latest_age = 0;

    // Sites first then hamlets: a tie between the two resolves to the site.
    std::vector<Place> wealth;
    std::vector<const NomadSite*> wealth_of;  // parallel payload for `wealth`
    std::vector<Place> markets;               // sites only; index into request.sites
    std::vector<Place> fresh_ruins;           // index into request.ruins
    // Ruins first then sites: a war site is where a war was DECIDED, and ruins carry the
    // loser's history while surviving cities carry their own.
    std::vector<Place> wars;
    std::vector<WarSite> wars_of;  // parallel payload for `wars`
    std::vector<Place> ley;        // index into request.ley
    std::vector<Place> shrines;    // index into request.shrines
    std::vector<Place> claims;     // index into request.claims
    std::vector<Place> roads;      // index is the GRID NODE
    std::vector<Place> settled;    // index unused
};
NomadGround nomad_ground(const NomadRequest& request);

// Great-circle metres between two unit directions.
double nomad_distance(const Vec3& a, const Vec3& b, double radius);

// Placement's own draw and its own band, exposed for the band request API (nomad_api.hpp),
// which raises one band exactly as placement raises one but from its own stream and on its
// caller's account. One copy of each, so the two cannot drift.
//
// `nomad_pick`: the cumulative walk over the eligible classifications, in their order. One
// uniform draw times `total` decides; the comparison is `<=`, and the LAST entry is the
// fallback when float drift puts the draw past the final cursor. Returns the choice and the
// draw.
//
// `nomad_make_band`: a band with the classification's authored speed, column, disposition,
// seeks and carries, and the one camp it starts from. Its origin is the world's, at `age`;
// a caller raising it on someone else's account overwrites the origin.
std::pair<NomadClass, double> nomad_pick(Rng& rng, const std::vector<std::pair<NomadClass, double>>& shares,
                                         double total);
NomadBand nomad_make_band(std::string uid, NomadClass value, const NomadHabitatCell& cell, int size, int age,
                          NomadBasis basis);

// Closest place, as (metres, list position). Ties break on (distance, key string) with a
// strict `<`, so on an exact tie of both the FIRST candidate in list order wins. An empty
// list returns infinity and -1, which every gate must tolerate.
struct NomadNearest {
    double distance_m = 0.0;
    int at = -1;  // position in the Place list, or -1
};
NomadNearest nomad_nearest(const Vec3& point, const std::vector<NomadGround::Place>& places,
                           double radius);

// One gate. Nullopt is INELIGIBLE, not "scored zero": a classification whose
// preconditions fail never enters the draw however well the weights would have scored it.
// The score is in [0, 1] by construction in all six.
struct NomadVerdict {
    double score = 0.0;
    NomadBasis basis;
};
std::optional<NomadVerdict> nomad_gate(NomadClass value, const NomadHabitatCell& cell, double swing,
                                       const NomadGround& ground, const NomadRequest& request);

// The day the sun is highest at this row. THE SEASONAL PHASE INVERTS ACROSS THE EQUATOR,
// which is invisible on a world whose land happens to sit in one hemisphere -- which is
// why it is pinned by a test rather than by inspection.
int nomad_midsummer_day(int z, int n);
int nomad_midwinter_day(int z, int n);

// Mean forage over the cells the band's round actually holds -- the ROUND rather than the
// start cell, because the route pass picks the best ground in reach: a clan's start point
// says where it was raised and its camps say what it eats.
double nomad_round_forage(const NomadBand& band, const std::vector<NomadHabitatCell>& cells);

// How many head that round carries. DIMENSIONLESS ON PURPOSE: head per square kilometre
// reads the raster rather than the land -- a cell is 52 km2 at raster 17 against 13 km2 at
// raster 33 -- so the same clan would outgrow the same ground at one resolution and not
// the other. The authored size band is already the statement of how large a band gets, so
// the round's forage picks a point on it.
double nomad_round_capacity(NomadClass value, double forage);

}  // namespace fwg
