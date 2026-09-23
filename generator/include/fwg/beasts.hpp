// Creatures that do not hold ground, and the index that makes them findable.
//
// The nest passes place everything as if it lived somewhere. Most creatures do. The ones
// that do not are here, and each moves for a different reason -- which is why they are
// four classes rather than one "wanders" flag, and why collapsing them would make every
// moving thing move the same way:
//
//   migratory  the green wave: high open pasture in the warm months, sheltered low ground
//              in the cold, through a calving camp. Routes are inherited, so a herd
//              walking the same ground every year is the intent and not a limitation
//   irruptive  normally sparse, and marching in a body only in a year that is actually
//              bad for it. Not a season but a trigger, so a group whose year is ordinary
//              carries route_status `solitary`, which is a NORMAL state and not a failure
//   follower   derives its circuit from a host's rather than searching for one. A wolf
//              pack does not hold a range when the herd it eats does not either
//   drifter    the dead nobody buried, circuiting between the ruins that made them
//
// `add_encounters` is an index, not a generator. Travelling groups are the one kind of
// world content a consumer cannot find by looking at a place -- a city is at a node, a
// caravan is at a different node every week -- so this indexes every group by the node and
// the month it can be met at, plus enough of what it IS (`domain`, `class`, `airborne`)
// that a land query drops the water without joining back through two other blocks. There
// are no quests, objectives, rewards or difficulty here on purpose: the consumer that
// would define them does not exist yet.
//
// Beast groups deliberately share the shape of a nomad band, so one consumer reads both
// with one code path. That is why `EncounterBand` and `BeastGroup` carry the same camps.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/grid.hpp"
#include "fwg/nests.hpp"
#include "fwg/vec3.hpp"

namespace fwg {

// 2: irruptions gained a condition in time. Under version 1 every irruptive group marched
// in every year, so a consumer could cache the block against the world's identity; now the
// roster changes from year to year, which is the point of it.
inline constexpr int kBeastMovementsVersion = 2;

// 2: every entry gained `domain`, `class` and `airborne`. Under version 1 an entry said
// only which species it was, so "what is near here, on foot" could not be asked of the
// index at all.
inline constexpr int kEncountersVersion = 2;

// ---------------------------------------------------------------------------
// The authored constants. Deliberately NOT extracted to data: each is a per-class
// single-value lookup used in exactly one expression, and a data file would turn four
// tables a reader can hold in their head into a fifth thing to keep in sync.
// ---------------------------------------------------------------------------

// Metres a day, by how big the thing is. A herd moves at the pace of its slowest member
// and a swarm at the pace of the wind behind it. ABSOLUTE, never scaled by world size: a
// gait belongs to the animal rather than to the world. It sets travel days directly
// (length / speed), so halving these roughly doubles travel time and shrinks every rest
// window.
double beast_speed_m_per_day(std::string_view size);

// Head count by danger tier: the pyramid again -- many small things, few great ones.
int beast_herd_size(int tier);

// Column length is head count times this. ABSOLUTE metres per head, not scaled: a tier-one
// tiny swarm is 45 m long and a tier-five colossal group is 240 m.
double beast_spread_per_head_m(std::string_view size);

// Which host a follower prefers when several are in reach, as a multiplier on distance.
// Scavengers and camp followers attach to people -- that is the whole of what they do --
// while hunters attach to herds. A plain nearest-host rule got this wrong in one
// direction: herds outnumber bands roughly forty to one, so nothing ever followed a
// caravan.
//
// NEVER above one, which is what makes the host walk's second stopping floor sound: a bias
// below one shortens the effective distance, so a preferred host wins from further away
// without ever letting an unreachable one through.
//
// `host_kind` is "nomad" or "beastmove". An unknown role takes the default row.
double beast_host_bias(std::string_view role, std::string_view host_kind);

// Reaches, in SETTLEMENT SPACINGS, as everywhere else in this feature. They are the only
// authored distances here that scale with the world, and they scale because
// settlement_spacing does -- the caller passes an already-scaled spacing and these
// multiply it. Everything else in metres above is absolute.
inline constexpr double kSeasonalReach = 3.0;
inline constexpr double kIrruptionReach = 4.0;  // the neighbourhood AND the march bound
inline constexpr double kDriftReach = 2.5;
inline constexpr double kFollowReach = 3.0;  // in GREAT-CIRCLE metres, not walking cost

// Slack on the latitude bound the host walk stops on, in radians. The bound -- that the
// angle between two directions is at least the difference of their latitudes -- is exact
// in real arithmetic; this absorbs the rounding in the asin that stands in for a latitude
// and the acos inside the great-circle distance. Same number and same argument as the
// nest pass's own latitude slack, kept here rather than shared because the two walks stop
// on different quantities and one constant would imply they move together. It is ADDED to
// the threshold, so the walk goes one candidate too far rather than one too few.
inline constexpr double kBeastWalkSlack = 1e-9;

// How much a year's forage departs from an ordinary one. ONE DRAW FOR THE WHOLE WORLD, not
// one per group: a bad year is bad for everybody at once, which is what makes the swarms
// of one year a regional event rather than a flat probability wearing a calendar.
inline constexpr double kIrruptionYearLow = 0.55;
inline constexpr double kIrruptionYearHigh = 1.25;

// How far below its own NEIGHBOURHOOD this year's forage has to fall before the gregarious
// phase takes over. Marginal ground AND a bad year; either on its own is ordinary life.
//
// Calibrated, and the shape of the distribution is why it is well below one: an irruptive
// species is already placed on marginal ground by the nest pass, so forage(start) over the
// mean of what a march reaches ran 0.08 at the tenth percentile to 1.14 at the maximum,
// median 0.30. At 0.18 the solitary phase stays the normal state and a bad year roughly
// doubles the swarms. At 0.85 nearly every group marched in every year, which is the
// behaviour this condition exists to remove.
inline constexpr double kIrruptionMargin = 0.18;

// Herds and refugees cross ground roads refuse: steeper, and they ford rather than bridge.
// Multiplies road_max_grade to the walking ceiling (0.77 on the default world).
inline constexpr double kGradeRelief = 2.2;

// How long a group lingers, by what the camp is for, as a relative share of the leftover
// year. Splitting the year evenly across camps put a bandit at the hamlet it had raided
// for four months, which is not a raid. An unlisted kind takes 1.0.
double beast_camp_rest(std::string_view kind);

// DAYS, absolute, and a ceiling on the share above. Some stops are events, not seasons,
// however much of the year is left to divide: a raid and a market day are measured in
// days. No ceiling for an unlisted kind.
std::optional<int> beast_camp_rest_ceiling(std::string_view kind);

// Midsummer in the north as a fraction of the year; the south is half a year away.
// Anchoring on a fraction rather than a day keeps this correct if the calendar changes.
inline constexpr double kMidsummerNorth = 0.5;

// The day the sun is highest at this grid row, INVERTING ACROSS THE EQUATOR -- 180 in the
// north and 0 in the south. A southern group winters on the opposite half of the year, and
// getting that wrong is invisible on a world whose land happens to sit in one hemisphere.
//
// ON LOAN. These two, the camp-rest tables above and the walking cost inside belong to the
// nomad route pass, which is not ported. They are defined here because this pass cannot
// run without them and nothing else owns them yet. WHEN THE NOMAD PASS LANDS THEY MOVE
// THERE AND THIS HEADER TAKES THEM FROM IT -- two definitions of the same rule is how the
// two silently stop agreeing, and the duplicate-symbol error at that merge is the point.
int midsummer_day(int z, int n);
int midwinter_day(int z, int n);

// ---------------------------------------------------------------------------
// What a group is.
// ---------------------------------------------------------------------------

enum class BeastMovement : int { Migratory = 0, Irruptive, Follower, Drifter };

// The published vocabulary, and what a profile's `movement` field is matched against.
// Nullopt for `nester` and for anything the catalogue does not name -- a nester is not a
// travelling group and is skipped, not defaulted.
std::optional<BeastMovement> beast_movement_of(std::string_view movement);
std::string_view to_string(BeastMovement movement);

// `solitary` is NOT `stranded`. Stranded means no circuit could be built; solitary means a
// swarm that is not swarming, which is its normal state.
enum class BeastRouteStatus : int { Routed = 0, Stranded, Solitary };
std::string_view to_string(BeastRouteStatus status);

// Camp `kind` stays a string rather than an enum because a follower copies its host's
// camps, and a nomad band's kinds are authored by a pass this one does not own. An enum
// here would have to enumerate that pass's vocabulary and would silently drop whatever it
// grew.
struct BeastCamp {
    std::string id;  // "<group uid>-camp-<index>"
    int node = 0;
    Vec3 direction{};
    std::string kind;
    // Null on a stranded or solitary group, which is a real answer: it is here, with no
    // schedule. Both are null or neither is.
    std::optional<int> arrive_day;
    std::optional<int> depart_day;
};

struct BeastLeg {
    std::string from;  // camp ids, not node indices
    std::string to;
    std::vector<int> nodes;  // the walked path, INCLUSIVE of both endpoints
    // GREAT-CIRCLE metres along the node path, not the walking cost. The cost function's
    // grade and forage terms shape the route; the reported length is plain geodesic
    // distance, so a forward and a reverse leg between two camps measure the same.
    double length_m = 0.0;
    std::optional<int> depart_day;
    std::optional<int> arrive_day;
    // This pass only ever emits `trunk`; the nomad pass also uses base_limb, spur and
    // fission. Carried so one consumer reads both.
    std::string branch = "trunk";
};

struct BeastGroup {
    std::string uid;  // THE key field -- "beastmove-<species_id>-<node>", not `id`
    std::string_view species_id;
    std::string_view name;
    BeastMovement movement = BeastMovement::Migratory;
    std::string_view family;
    int tier = 1;  // creature DANGER tier 1..5, not a settlement scale
    // The profile's CLASS -- animal or monster -- not the nest kind. The word `kind` means
    // four different things across these two blocks and this is the first of them.
    CreatureClass kind = CreatureClass::Animal;
    std::string_view source;  // "wildlife" or "beast_nests"
    // Followers only: a nomad band uid or another beast group's uid. Claimed even when the
    // host's circuit was too short to derive from, because the follower DID find a host --
    // it is the host's route that failed it, and the block should say which host.
    std::string host_uid;
    int node = 0;
    int x = 0;
    int z = 0;
    Vec3 direction{};
    int size = 2;
    double speed_m_per_day = 0.0;
    double column_length_m = 0.0;
    std::string_view disposition;  // hostile for a monster, wary otherwise
    std::string_view role;         // feeding role; empty for monsters
    std::vector<BeastCamp> camps;  // at least one, always
    std::vector<BeastLeg> legs;    // empty on stranded and solitary
    BeastRouteStatus route_status = BeastRouteStatus::Stranded;
    // Routed groups only. Nullopt is the published absence, not a zero.
    std::optional<double> round_length_m;
};

struct BeastMovements {
    int version = kBeastMovementsVersion;
    std::vector<BeastGroup> groups;  // sorted by uid as a STRING
    // movement -> count, in order of FIRST APPEARANCE in the uid-sorted list. A map would
    // order it alphabetically, which is a different published order.
    std::vector<std::pair<std::string_view, int>> counts;
    int considered = 0;  // eligible sites seen, before thinning
    int skipped = 0;     // sites the share dropped
    double share = 1.0;
    int routed = 0;
    int stranded = 0;
    int solitary = 0;
    long long year = 0;
    double year_forage_factor = 0.0;  // in (0.55, 1.25)
    std::string_view method;
    std::string_view limits;
};

// routed + stranded + solitary == groups.size(), always. Exposed because it is the one
// invariant that catches a group counted twice or not at all.
bool beast_counts_balance(const BeastMovements& block);

// ---------------------------------------------------------------------------
// What goes in.
// ---------------------------------------------------------------------------

// A fallen city, as the drifter builder reads one. `node` is the terrain node, which is
// the stable key across an age boundary -- ruin ids are not. `uid` is the CITY uid the
// ruin was made from, not "ruin-<uid>" (that is its id), and it is a tie-break in the
// haunt sort, so it must be the same string the ruin block publishes.
struct BeastRuin {
    int node = -1;
    std::string uid;
};

// A nomad band, as these two passes read one.
//
// Named for the index rather than for the producer because the nomad pass is not ported
// and this is the shape its output has to be converted INTO, not the shape it has. Both
// passes read it: the host walk takes bands with legs as hosts, and the encounter index
// gives every band an entry whether or not it walks.
struct EncounterBand {
    // The "nomad-" prefix is what the host walk tests to pick a bias row, so a band whose
    // uid does not carry it is biased as a beast group. That is the reference's own rule.
    std::string uid;
    int node = 0;
    std::string classification;  // merchants | survivors | wanderers | cultists | ...
    std::string disposition;
    int size = 0;
    std::vector<std::string> carries;
    std::vector<std::string> seeks;
    std::string god_id;  // empty is the published null
    double speed_m_per_day = 0.0;
    double column_length_m = 0.0;
    std::vector<BeastCamp> camps;
    // bool(band.legs). Only bands that ALREADY carry a route are eligible hosts; nothing
    // here reads the legs themselves, so they are not carried.
    bool has_legs = false;
};

struct BeastMovementRequest {
    // Gate. Both passes no-op entirely below stage 16 or on a world with no recipe, which
    // is the reference's own behaviour and is not an error.
    int world_recipe = 3;
    int phase = 16;

    // Every layer, as world.layers. The pass reads height, slope, tpi, temperature,
    // water_type, food_potential and natural_food_potential, and a MISSING LAYER READS
    // 0.0 rather than erroring -- which is load-bearing today, because the farming pass
    // that writes the two food layers is not ported and every forage term is therefore
    // zero. See the note on `forage` in src/beasts.cpp.
    const std::map<std::string, Grid>* layers = nullptr;

    // MUST be built at world.effective.globe_radius. `config` is DESIGN space and
    // `effective` is metres; the two differ by the world scale, a factor of 5.6 on the
    // default world, and every reach here is absolute metres. A design-space grid compares
    // metres against design units and every gate silently answers a different question.
    // Refused rather than documented.
    const SphereGrid* grid = nullptr;
    double globe_radius_m = 0.0;

    // METRES, and ALREADY SCALED by reach_scale -- world.effective.settlement_spacing, not
    // world.config's. The four reaches above are multiples of this and of nothing else, so
    // handing over the design-space 450 shrinks every search to a single cell.
    double settlement_spacing_m = 0.0;
    double road_max_grade = 0.35;

    // The MASTER seed, which on a time tick has been stepped. Feeds the thinning stream
    // only.
    std::uint32_t seed = 42;
    // The GENESIS seed -- world.config.seed -- retained separately. The year's forage
    // factor is keyed on it, deliberately not on `seed`: this pass runs on a monthly
    // cadence, so a factor drawn from a stepped seed would be a different number in every
    // month of the same year, and a swarm would form and dissolve with the call rather
    // than with the world. This is the single most likely porting defect in this module.
    std::uint32_t genesis_seed = 42;

    double share = 1.0;      // beast_movement_share, [0,1]
    int nomad_variation = 0;  // lets this layer be rerolled with the nomad layer

    // The ABSOLUTE year. The caller resolves it (world_clock.day / 360 when a clock
    // exists, else the founding end year) because the clock belongs to a time module that
    // is not ported; keying the forage draw on it is this pass's own job.
    long long year = 0;

    // The placed creature sites, in the order their blocks publish them. `wildlife` is
    // read before `beast_nests`, but both are merged and sorted together before anything
    // reads them, and a species id belongs to exactly one half -- so the source never
    // decides the order.
    std::span<const NestSite> wildlife;
    std::span<const NestSite> beast_nests;

    // Bands, already routed. A world with none strands every follower.
    std::span<const EncounterBand> bands;
    // Empty on a world that has had no age transition, which strands every drifter.
    std::span<const BeastRuin> ruins;
};

// ---------------------------------------------------------------------------
// The encounter index.
// ---------------------------------------------------------------------------

struct EncounterEntry {
    std::string group_uid;  // THE key field; `entries` is also addressed POSITIONALLY
    std::string source;     // nomads | beast_movements
    // A nomad classification or a creature species id -- a THIRD meaning of the word.
    std::string kind;
    std::string domain;  // land | ocean | lake, resolved from the AUTHORED medium
    // Published as `class`. animal | monster | people, on the words wildlife and
    // beast_nests already use rather than a third spelling of the same distinction.
    std::string entity_class;
    // Flying is NOT a domain: a roost is still somewhere you can stand, and folding it in
    // would make domain == "land" stop meaning "reachable on foot".
    bool airborne = false;
    std::string disposition;
    int threat_tier = 1;  // 1..5, the creature rubric; a band takes its classification's
    int size = 0;
    std::vector<std::string> carries;
    std::vector<std::string> seeks;
    std::string host_uid;  // empty is the published null
    std::string god_id;    // empty is the published null
    double speed_m_per_day = 0.0;
    double column_length_m = 0.0;
};

struct EncounterOccupancy {
    int entry = 0;  // index into `entries`, one hop further than the two indexes below
    int node = 0;
    std::string camp_id;
    std::string camp_kind;  // a FOURTH meaning of `kind`
    std::optional<int> from_day;
    std::optional<int> to_day;
    std::vector<int> months;  // 0..11, both endpoints INCLUSIVE, wrapping the year
};

struct EncounterIndex {
    int version = kEncountersVersion;
    std::vector<EncounterEntry> entries;
    std::vector<EncounterOccupancy> occupancy;
    // All twelve months, always present and possibly empty. Values index `occupancy`.
    std::array<std::vector<int>, 12> by_month;
    // Only the nodes that occur, ASCENDING BY NODE. A map keyed on the published string
    // would put "10" before "9", which is the wrong order and is a contract a consumer
    // sees. Values index `occupancy`.
    std::vector<std::pair<int, std::vector<int>>> by_node;
    std::string_view method;
    std::string_view limits;
};

// Both indexes hold indices into `occupancy`, not into `entries`. Within one bucket the
// order is append order: nomads before beasts, then group order, then camp order.
struct EncounterRequest {
    int world_recipe = 3;
    int phase = 16;
    // Nomads FIRST is fixed. Entry indices are assigned in that order, so every occupancy
    // index in both indexes depends on it.
    std::span<const EncounterBand> bands;
    std::span<const BeastGroup> groups;
};

// How dangerous a band is to walk into. Bands have no tier of their own, so their
// classification supplies one: what a band will do to you is a property of what kind of
// band it is. Unknown classifications take 2.
int nomad_threat_tier(std::string_view classification);

// Where a species lives, on key_locations' word for the same idea rather than a fourth
// one. `shore` is a LAND medium -- the nest pass refuses a shore species every water cell
// and then asks for a coast -- so a puffin colony is on the beach and a traveller walks to
// it.
std::string_view medium_domain(CreatureMedium medium);

// Months a camp is occupied, wrapping across the turn of the year. Empty when either
// endpoint is null, which is how a stranded group lands in `by_node` and in NO month --
// see the gap named in the limits prose.
std::vector<int> window_months(std::optional<int> start, std::optional<int> end,
                               int days_per_year);

// ---------------------------------------------------------------------------
// The two passes.
// ---------------------------------------------------------------------------

// Give the creatures that do not hold ground the circuits they actually walk. Nullopt when
// the phase gate refuses.
//
// Throws std::invalid_argument on a grid whose radius is not globe_radius_m, and on a
// missing grid or layer map -- the three inputs whose silent mismatch would make every
// reach answer a different question.
std::optional<BeastMovements> add_beast_movements(const BeastMovementRequest& request);

// Index every travelling group by where and when it can be met. Pure: no randomness, no
// geometry, no layer read. Nullopt when the phase gate refuses.
std::optional<EncounterIndex> add_encounters(const EncounterRequest& request);

}  // namespace fwg
