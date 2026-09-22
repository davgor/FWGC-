// Super villains: antagonists that operate on a different order than the kings around them.
//
// Every other figure the generator produces is a POINT in the world, named after events
// that already happened. A super villain is a FIELD: a reach in metres with linear
// falloff, like a ley network, that causes wars and city fates in its own name.
//
// Tier is reach and it is continuous, so "super villain" is a band on a curve rather than
// a flag that flips. That is the whole pacing model: one that has just crossed the line is
// a threat that is GROWING, not one that has arrived, so the peace after a player kills
// one comes from the successor being weak rather than from a cooldown holding it back.
//
// Nothing here draws a random number. Every decision -- which region seats, which city is
// the seat, which growth function, which ley node is the anchor, who is promoted first --
// is a deterministic reading of world state with an explicit tie-break. A port that needs
// a draw has a bug, and `Rng::draws()` is how a test says so.
//
// Like `resolve_religion`, every pass here takes a request and returns a value. It does
// not reach into `World`, and it writes nothing: the caller decides what to keep.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/magic.hpp"
#include "fwg/settlement.hpp"
#include "fwg/vec3.hpp"

namespace fwg {

// ---------------------------------------------------------------------------
// Constants. Every one of these is seed-visible.
// ---------------------------------------------------------------------------

// Block contract. v1 `tiers` was turmoil-EVER; v2 is turmoil-SUSTAINED. A consumer that
// reads a v1 value as "the worst this region ever was" reads a v2 one wrong, which is
// what the integer is for.
inline constexpr int kVillainsVersion = 2;

// The band above which a villain is "super". Seating threshold, the tier floor for
// `villain_causes` and the works pass, and the exact tier `promote_villains` grants.
inline constexpr double kSuperTier = 1.0;

// Above this band a claim is a seat rather than a village. Derived, not authored: raising
// kSuperTier moves the claim boundary with it.
inline constexpr double kSeatTier = kSuperTier * 1.5;

// reach_m = tier * this * settlement_spacing. SCALES: settlement_spacing is resolved as
// 450 m * reach_scale(circumference), so a tier-one reach is a constant share of the
// planet at every world width -- 32 290 m on a 200 km world, 16% of the way round.
inline constexpr double kReachSpacingsPerTier = 4.0;

// The forward-looking half of `turmoil`. A region about to fight is concentrating as
// surely as one that already has.
inline constexpr double kRiskWeight = 0.3;
inline constexpr double kHungerWeight = 0.3;

// Reach multiples at which a villain's pressure is spent, in the fate lottery ALONE.
// Lowering it sharpens the shadow; it does not change war contention, which reads
// `reach_m` directly.
inline constexpr double kVillainFalloff = 1.5;

// Its share of a city's fate lottery at the seat, at tier one. The lottery caps total
// chance at 0.9, so 0.5 is already more than half the ceiling.
inline constexpr double kVillainWeight = 0.5;

// Where a pretender sits when the ceiling is full. MUST be strictly below kSuperTier, and
// the clamp is applied to the LEDGER rather than to a local, so a blocked region cannot
// bank tier and burst through when a slot opens.
inline constexpr double kStalledTier = 0.999;

// The tier a region keeps when its villain falls -- the successor squabble, which is what
// a villain that left no mark leaves behind.
inline constexpr double kFragmentShare = 0.35;

// What a region's tier ledger keeps from one age to the next, and the most consequential
// number in the file.
//
// Without it the ledger is turmoil-EVER -- `tier = tier + rise * concentration`, both
// factors non-negative, so a seated villain's tier never falls. Seating needs
// `tier >= kSuperTier` and `villain_hold` is declared 0..1, so `tier < hold` required
// `hold > 1.0000000000000009`: the reachable set was empty by one epsilon and every line
// that writes a fall was dead code in every buildable world.
//
// With it, tier is turmoil SUSTAINED. A steady concentration p settles at
// `rise * p / (1 - 0.9)` rather than growing without bound with the world's age, so reach
// measures how bad a region is and not how old the world is. Decay from the bottom of the
// band: 1.0, .9, .81, .729, .656 -- four quiet ages to cross the default hold of .7.
inline constexpr double kTierKeptPerAge = 0.9;

// What a claim keeps at the moment its holder falls, what it keeps of that each further
// age, and the floor below which it is reported as exactly zero -- so a consumer scaling
// by it stops being nudged by claims too old to matter instead of being nudged
// imperceptibly forever. Series: .60 .36 .216 .130 .078 0.
inline constexpr double kFallenClaimInfluence = 0.6;
inline constexpr double kClaimDecayPerAge = 0.6;
inline constexpr double kClaimInfluenceFloor = 0.05;

// A well is a source in its own right, not a marker. Must stay at or below 4.0, the
// ceiling every network edit enforces.
inline constexpr double kWellIntensity = 3.75;
inline constexpr double kWellEdgeIntensity = 2.0;

// How many nearest cities a claim draws its peoples from.
inline constexpr int kMixedFactions = 3;

// The prefix `held_node` skips. A villain's own well sits at its own seat and is therefore
// always the nearest node; left in the running it re-anchors the region onto the holder's
// own work, `advance` looks the villain up by the old anchor and misses, and the reign is
// stranded on the very age it began -- deterministically, for every villain bound to a
// school god. The anchor must be ground the world laid down, not ground the villain did.
inline constexpr std::string_view kWellNodePrefix = "well-";

// Used when no pressure dominates: all three totals zero, or no reports at all.
inline constexpr std::string_view kDefaultGrowth = "usurping";

// ---------------------------------------------------------------------------
// Shared arithmetic. Written once here and used by corruption too, because the
// reference had three character-for-character copies of the spherical helpers.
// ---------------------------------------------------------------------------

// Decimal rounding, half away from zero.
//
// NOT CPython's `round`, which is banker's rounding on the decimal representation. This is
// a clean rewrite so the tie behaviour is allowed to differ, but it is not cosmetic
// everywhere: `VillainBlock::tiers` stores 9-dp values that are read back next age and fed
// into `tier * 0.9 + rise * p`, so this rounding is part of the recurrence.
double round_dp(double value, int places);

// radius * acos(clamp(dot(a, b))) on unit vectors.
//
// Kept in exactly this clamp-then-acos form. An atan2-based great-circle formula is more
// accurate and would therefore NOT reproduce the behaviour at the tie boundaries where
// `distance < best` decides which ley node anchors a region.
//
// This is the one that takes DIRECTIONS and wraps the seam. `src/city_plan.cpp` has a
// file-local arc over raster coordinates that deliberately does NOT wrap, and it was
// called great_circle_m too. Renamed there to `unwrapped_arc_m`, because two functions of
// one name measuring two different things is a near miss that compiles: they differ in
// arity today, so no call could resolve wrongly, but the next one to take a matching
// signature would.
double great_circle_m(double radius_m, const Vec3& a, const Vec3& b);

// A unit vector `angle` radians from `up` along `bearing`.
//
// The pole guard is load-bearing: without it the Gram-Schmidt reference is parallel to
// `up` at the poles, `north` comes out a zero vector and normalising it divides by zero.
Vec3 offset_direction(const Vec3& up, double angle, double bearing);

// ---------------------------------------------------------------------------
// What the world has to say for itself.
// ---------------------------------------------------------------------------

// A settlement as villains and corruption read one.
//
// `id` is the INTEGER a culture's city list references and `uid` is the stable string
// key; they are two different fields and confusing them silently produces empty regions.
struct VillainCity {
    std::string uid;
    int id = 0;
    int node = 0;
    Vec3 direction{};  // unit vector of the cell the city stands on
    std::string population_profile;
    // Read only when corruption ends a city: it is what the ruin's legacy intensity is
    // keyed on, and the one field of the thirteen a ruin carries that is not identity.
    CityClass city_class = CityClass::Small;
    // A DOUBLE, not the site's integer count: blood corruption farms a fraction of a
    // city's people and writes the remainder back here.
    double population_estimate = 0.0;
    // Written by eldritch corruption. Nothing visibly changes, which is the horror --
    // and see the note on CorruptionOutcome: the rebuild erases it.
    std::string taken_by;
};

// A road-linked group of cities, as stage 12 publishes one.
//
// `city_ids` are the INTEGER `id` of each site. The id string looks like
// `human_heartland-672bcb6c` and is rebuilt every age from whatever the roads join that
// age, which is exactly why it cannot be the anchor.
struct VillainCulture {
    std::string id;
    std::vector<int> city_ids;
};

// The forward-looking war outlook, as stage 14 publishes one per city.
struct VillainThreatReport {
    std::string city_uid;
    double regional_threat = 0.0;
    double war_risk = 0.0;
    double war_hunger = 0.0;
    double war_pressure = 0.0;
    double nest_pressure = 0.0;
    double ley_pressure = 0.0;
};

// A ruin, as a fallen villain's mark reads one.
//
// Read back out of the world rather than accumulated on the villain, for the same reason
// the region is anchored to a ley node: anything hung on a live record is lost the moment
// that record is rebuilt, and ruins are already durable and already carry the attribution.
struct VillainRuin {
    std::string id;
    std::string villain_uid;  // evidence.villain_uid; empty when nothing was blamed
};

// ---------------------------------------------------------------------------
// The records.
// ---------------------------------------------------------------------------

// One line of a villain's history. The fields a given event carries differ, and the
// optionals preserve that: a `rose` entry has a concentration and no school.
struct VillainLogEntry {
    int age = 0;
    std::string event;  // rose | promoted | fell | sank a well | corrupted | cleansed
    std::optional<double> concentration;
    std::string school;
    std::optional<int> lines;
    std::string god;
    std::optional<long long> encounters;
};

// Ground a villain has taken. THE KEY FIELD IS `id`, which is `claim-<villain uid>` --
// exactly one constant id per villain, ever.
//
// A snapshot with a standing half, and the split is deliberate. `age`, `direction`,
// `node`, `factions` and `drawn_from` record what this villain took and when, so a city
// founded two ages later does not retroactively join a claim staked before it existed.
// What tracks the present is the holder's own standing: `kind`, and the `holder_status`,
// `holder_fell_age` and `influence` that the advance pass restamps.
struct VillainClaim {
    std::string id;
    std::string villain_uid;
    int age = 0;
    Vec3 direction{};
    int node = 0;
    std::string kind;  // seat | village -- a band on the holder's tier RIGHT NOW
    int refreshed_age = 0;
    std::vector<std::string> factions;    // sorted, deduplicated population profiles
    std::vector<std::string> drawn_from;  // three city uids, in DISTANCE order
    std::string well;                     // the holder's well node id, empty when none
    std::string holder_status = "living";
    std::optional<int> holder_fell_age;
    double influence = 1.0;
};

// A leyline well: a source that is JOINED. The edges are the point -- they change what the
// network carries between the villain's holdings, which a lone node cannot do.
struct VillainWell {
    std::string node;  // well-<villain uid>
    std::string school;
    std::string god;
    std::vector<std::string> lines;  // edge ids, in node-list order
    int age = 0;
};

// THE KEY FIELD IS `uid`: `villain-<born age>-<seat node index>`.
struct Villain {
    std::string uid;
    // The ANCHOR: a ley node id, or `seat-<seat uid>` where the world has no magic. This
    // is the stable cross-age key, and it is a node rather than a culture id because a
    // culture's id is rebuilt every age and accumulating tier against it would silently
    // reset the region every time.
    std::string region;
    int born_age = 0;
    std::string seat_uid;
    int node = 0;
    Vec3 direction{};
    std::string school;  // empty where the world has no magic at all
    std::vector<std::string> held_nodes;
    double tier = 0.0;
    double reach_m = 0.0;
    std::string growth;  // devouring | spreading | hoarding | usurping
    // EMPTY READS AS LIVING: a record written before the fall was recorded at all
    // described someone who stood. The empty/"living" distinction is kept rather than
    // normalised, because normalising it moves the bytes of every world written before.
    std::string status;
    std::optional<int> fell_age;
    std::vector<VillainLogEntry> log;
    std::string god;        // school god, or the hidden god that took it
    bool corrupted = false;
    std::optional<VillainWell> well;
    std::vector<VillainClaim> claims;
};

// The durable record of a reign. THE KEY FIELD IS `id`, which is `fallen-<villain uid>`.
//
// Deliberately no `tier` and no `reach_m`: both describe a grip it no longer has. And
// deliberately no asset id, unlike a ruin -- a ruin stands on the ground and needs a
// marker in the exhaustive asset list, while a fallen villain is a record about ground
// that other records already hold.
struct FallenVillainMark {
    std::string id;
    std::string kind = "fallen_villains";
    // The thirteen carried fields, chosen the way a ruin chooses what a city carries:
    // identity, where it stood, and what it was -- not its live state.
    std::string uid;
    std::string region;
    int born_age = 0;
    std::string seat_uid;
    int node = 0;
    Vec3 direction{};
    std::string school;
    std::string growth;
    std::vector<std::string> held_nodes;
    std::vector<VillainClaim> claims;
    std::optional<VillainWell> well;
    std::string god;
    std::vector<VillainLogEntry> log;

    int fell_age = 0;
    int reigned_ages = 0;
    double tier_at_fall = 0.0;

    // What the world itself recorded, sorted. `held_nodes` alone does NOT make a mark:
    // a villain that reached the band, held nothing, took no city and sank no well did
    // not mark the world, and inventing a monument for it would make the record of the
    // dead less useful rather than more.
    std::vector<std::string> left_ruins;
    std::vector<std::string> left_claims;
    std::string left_well;
    std::vector<std::string> left_held_nodes;
};

struct VillainOutlookRow {
    std::string region;  // the anchor
    std::string culture;
    // A STRANDED row: an anchor that still holds a villain but that no region was
    // produced for this age. Its `culture` is null in the reference and empty here.
    bool has_culture = true;
    double concentration = 0.0;
    double tier = 0.0;
    double to_threshold = 0.0;
    bool seated = false;
    bool stalled = false;
    int cities = 0;
};

// `standing` always equals the number of rows marked `seated`. That needs saying because
// it did not use to: a villain holding an anchor no region was produced for was counted in
// the first and absent from the second, so one document gave two answers.
struct VillainOutlook {
    double rise = 0.0;
    int ceiling = 0;
    int standing = 0;
    std::vector<VillainOutlookRow> regions;
};

struct VillainWork {
    std::string villain_uid;
    bool well = false;
    bool claim = false;
};

struct VillainBlock {
    int version = kVillainsVersion;
    // Living first, by region anchor, then the fallen by (fell age, uid). The living keep
    // their existing order so a world in which nobody has fallen serialises exactly as it
    // did before the fallen were kept at all.
    std::vector<Villain> people;
    // KEY-SORTED, and the JSON byte order depends on it -- which is why this is a
    // std::map and not an insertion-ordered field list.
    std::map<std::string, double> tiers;
    VillainOutlook outlook;
    // Append-only and never pruned, exactly as ruins are. The world has a finite
    // population, so its dead are a knowable set rather than an unbounded stream.
    std::vector<FallenVillainMark> fallen;
    std::vector<VillainWork> works;
};

// ---------------------------------------------------------------------------
// The request every pass takes.
// ---------------------------------------------------------------------------

struct VillainRequest {
    // Share of a region's concentrated turmoil that becomes tier each age, 0..1. Zero
    // raises none AND writes no block at all: the option is invisible in the output rather
    // than merely inert in it.
    double rise = 0.5;
    // Tier a seated villain falls below to lose the world, 0..1. Strictly below kSuperTier
    // at the default, which is the hysteresis: the band to stay is below the band to rise
    // and nothing flickers across the line.
    double hold = 0.7;
    // Cultural regions per permitted villain, 1..12.
    double density = 3.0;

    int age = 0;

    // METRES. This is world.effective.globe_radius, not world.config's -- the two differ
    // by the world scale, a factor of 5.6 on the default world. Handing this design units
    // compares metres against design units and every reach here silently answers a
    // different question.
    double globe_radius_m = 1.0;
    // METRES, and ALREADY SCALED. `settlement_spacing` is not in the world-scale list;
    // recipe 3 resolves it as 450 m * reach_scale(circumference), which is 8072.5 m on the
    // 200 km reference world. Every reach in this module is a multiple of it, so no reach
    // here is an absolute metre constant and none of them stop scaling with the planet.
    double settlement_spacing_m = 450.0;

    std::vector<VillainCity> cities;
    std::vector<VillainCulture> cultures;
    std::vector<VillainThreatReport> reports;
    std::vector<VillainRuin> ruins;
    const MagicReport* magic = nullptr;

    // The prior block. ABSENT is not the same as empty: absent plus `rise <= 0` is the
    // real off switch, while an empty block is a world that had villains enabled and has
    // not seated one yet.
    std::optional<VillainBlock> villains;
};

// ---------------------------------------------------------------------------
// The pure helpers, exposed because they are the properties worth testing on their own.
// ---------------------------------------------------------------------------

bool is_standing(const Villain& villain);
std::vector<Villain> standing_villains(const std::vector<Villain>& people);

// What a city contributes to its region concentrating, in 0..1.
double turmoil(const VillainThreatReport& report);

// Which kind of pressure dominates, for the growth function. Empty when all three totals
// are zero or the list is empty. Ties resolve ALPHABETICALLY -- ley beats nest beats war.
std::string dominant_pressure(const std::vector<const VillainThreatReport*>& reports);

// How many villains may stand at or above the band at once.
int villain_ceiling(int region_count, double density);

// How hard a fallen villain's claim still presses, `ages_since_fall` ages later. A
// negative argument returns 1.0: that is a guard against a fell_age ahead of the current
// age, not a decay value.
double claim_influence(int ages_since_fall);

// The god of a known school, so a villain may be bound to an old one as well as a new one.
// First match in catalogue order; empty when the school has none.
std::string school_god(std::string_view school);

// A cultural region with the stable anchor it accumulates tier against.
//
// BORROWS from the request it was produced from: `cities` and `seat` point into
// `VillainRequest::cities`, so the request must outlive the regions.
struct VillainRegion {
    std::string id;  // the culture id, or "world" for the no-cultures fallback
    std::vector<const VillainCity*> cities;
    const VillainCity* seat = nullptr;
    std::string school;
    std::string node_id;
    std::string anchor;  // node_id, or `seat-<seat uid>` where the world has no magic
};

// The single most delicate function in the module.
//
// ANCHOR-ASCENDING on return, which is the order of the advance loop, of the outlook rows
// and of the tie-break in promote. Two regions can share a nearest node; the troubled one
// keeps it on a strict `>`, so an anchor names one region and a tier cannot be
// double-counted into it -- and on an exact tie the earlier group in culture-id order wins.
std::vector<VillainRegion> villain_regions(const VillainRequest& request);

// The mean, not the sum. That is what separates "one bad city" from "a region coming
// apart", and a region the assessment has nothing to say about concentrates nothing.
double concentration(const VillainRegion& region,
                     const std::map<std::string, const VillainThreatReport*>& reports);

// The nearest existing ley node to a seat, as (school, node id). Both empty where there is
// no magic at all. Strict `<` on distance, schools walked in NAME ORDER, so two equidistant
// nodes resolve to the alphabetically earlier school.
std::pair<std::string, std::string> held_node(const MagicReport* magic, const Vec3& seat,
                                              double radius_m);

// ---------------------------------------------------------------------------
// The passes.
// ---------------------------------------------------------------------------

struct VillainAdvance {
    // Absent when `rise <= 0` and the request carried no block -- the off switch writes
    // nothing rather than writing an empty block.
    std::optional<VillainBlock> block;
    // The STANDING cast, not the block. This is what flows into war resolution, whose
    // villain pressure reads `reach_m` with no status test of its own -- and `reach_m` is
    // refreshed BEFORE the fall is decided, so a fallen villain still carries a
    // live-looking reach.
    std::vector<Villain> standing;
};

// Move each region's tier, then seat, hold or unseat its villain.
//
// Runs FIRST in an age transition, before wars and before any city fate: the cast advances
// on the assessment this age inherited, so a villain is always one rebuild behind the
// region it is eating and can never be raised by its own damage.
VillainAdvance advance_villains(const VillainRequest& request);

// A villain's share of a city's fate lottery.
//
// APPENDED by the caller, never inserted: the selector walk in the fate lottery is
// order-sensitive, and reordering these changes which cause is chosen for a given draw,
// which changes the ruin's school, which changes the ley field.
struct VillainCause {
    std::string kind;  // villain_devouring | villain_spreading | villain_hoarding | villain_usurping
    double weight = 0.0;
    std::string reason;
    // evidence
    std::string villain_uid;
    std::string growth;
    double tier = 0.0;
    double distance_m = 0.0;
    double reach_m = 0.0;
    std::string school;
};
std::vector<VillainCause> villain_causes(const std::vector<Villain>& cast, const VillainCity& city,
                                         double radius_m);

// Where turmoil is concentrating, so a story layer need not guess. A reading of present
// ground, and the only part of the block a sub-age tick may refresh.
VillainOutlook villain_outlook(const VillainRequest& request);

struct VillainPromotion {
    // Absent when nothing was promoted -- and then the caller must not touch the block at
    // all. Idempotency depends on it: the hook fires on the final age of a generation AND
    // on the final age of an age-advance request, and a caller may advance a world that
    // was already promoted.
    std::optional<VillainBlock> block;
    std::vector<Villain> promoted;
};

// Seat the world's most concentrated regions as the simulation ends.
//
// Deliberately NOT an accumulation: a promoted villain enters at exactly kSuperTier, the
// bottom of the band, because a villain that has just crossed the line is a threat that is
// growing rather than one that has arrived. The tier it is given is the tier it would have
// to hold. Accumulation alone cannot seat anybody inside the two age transitions a
// generation runs, so the rate is the campaign ramp and this is the arrival.
VillainPromotion promote_villains(const VillainRequest& request);

struct VillainWorks {
    VillainBlock block;
    // The networks with the wells sunk. A well changed the ley geometry, so the caller must
    // re-run `evaluate_networks` and the biome variants when `works` is non-empty.
    MagicReport magic;
    std::vector<VillainWork> works;
};

// Everything a seated villain puts on the ground this age: a leyline well and a mixed
// claim on the ground near its seat. Runs AFTER promotion, so a promoted villain sinks its
// well and stakes its claims in the same age it rises, exactly as an accumulated one does.
//
// Call it only on a world that HAS a block. A request with none returns an empty block and
// no works, which a caller must not write back over a world that never had villains --
// that would invent the block the zero rate exists to withhold.
VillainWorks build_villain_works(const VillainRequest& request);

}  // namespace fwg
