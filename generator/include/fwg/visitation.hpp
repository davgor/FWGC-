// Visitation: the orchestrator summons a known god onto the mortal plane.
//
// A summoned god is highly disruptive. An avatar ley cluster saturates its schools around
// the target, the schools opposed to them are suppressed inside the reach, every city within
// reach rolls its fate with the god itself as one more cause, rival monster nests are driven
// out, the god's own faithful are favoured, and the peoples that stand convert. When the god
// departs, the cluster collapses to a footprint key point and the rivals recover; the ruins,
// the mutated biomes and the converted faiths remain.
//
// Every distance is a ratio of settlement spacing -- never a metre constant, never a raster
// cell -- so world scale and raster size cannot turn a local visitation into a regional one.
//
// Stateless, like the reference: same request in, same value out, and nothing the caller owns
// is modified. The reference calls back into the whole generator twice (the tail rebuild and
// the stage snapshot); this port does neither. A summons is therefore TWO calls with the
// caller's rebuild between them, because three of its steps read the rebuilt world:
//
//   summon_god        cluster, suppression, fate lottery, ruins, their key points
//   <caller>          append the ruins, take the magic, drop the plans, and rebuild the tail
//                     over `survivors` WITHOUT re-resolving religion: evaluate the networks,
//                     refresh the environment, the biome variants, re-found civilization over
//                     the survivors, refresh astrology, regenerate the beast nests, re-assess
//                     threats
//   complete_summons  nest purge, favour relief, conversion, the records
//   <caller>          write back the nests, the relief and the operation; re-resolve religion
//                     with `previous` = the returned block; re-plan cities, hamlets, castles
//
// What survives what: the ruins, their key points, the footprint and the conversions last.
// The nest purge and the favour relief last only until the next rebuild regenerates nests and
// re-assesses threats -- a departure, a corruption, a cleanse or an age turn -- which is the
// reference's lifetime for both and is not changed here.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fwg/astrology.hpp"
#include "fwg/grid.hpp"
#include "fwg/magic.hpp"
#include "fwg/nests.hpp"
#include "fwg/religion.hpp"
#include "fwg/settlement.hpp"
#include "fwg/settlements_stage.hpp"
#include "fwg/vec3.hpp"
#include "fwg/villains.hpp"

namespace fwg {

inline constexpr int kVisitationApiVersion = 1;

// Per world, counted over EVERY record including the departed ones, so a world can be
// permanently out of summons. That is the reference's rule and it is deliberate: eight
// theophanies is already a world the gods have visibly walked through.
inline constexpr int kMaxVisitations = 8;

// Ring nodes around the centre, so an avatar cluster is 1 + 4 = 5 nodes PER SCHOOL.
inline constexpr int kAvatarRingPoints = 4;
// Ring radius in settlement spacings -- 4 036 m on the 200 km reference world. SCALES.
inline constexpr double kAvatarRingSpacings = 0.5;
// The whole footprint of a visitation in settlement spacings -- 24 217.5 m on the 200 km
// world, 12% of the way round. SCALES: settlement spacing is resolved as
// 450 m * reach_scale(circumference) and is not in the world-scale list, so this is already
// a share of the planet at every width and fires at every shipped one.
inline constexpr double kVisitationReachSpacings = 3.0;

// The centre saturates at the network ceiling; the ring sits below it.
inline constexpr double kAvatarIntensity = 4.0;
inline constexpr double kAvatarRingIntensity = 3.0;
// What the collapsing cluster leaves behind when the god goes home.
inline constexpr double kFootprintIntensity = 3.5;

// The god's own share of a city's fate lottery at the centre, before wrath, aspect and faith.
inline constexpr double kDivineWeight = 0.6;
// Opposed intensities inside the reach are multiplied by 1 - kSuppression * wrath, so at
// full wrath a rival school is halved.
inline constexpr double kSuppression = 0.5;
// Taken off a favoured city's regional threat, times the favour's strength.
inline constexpr double kFavorRelief = 0.2;

// Wild doubles and Sovereign halves, so a Wild god destroys FOUR times as much as a Sovereign
// one. Corruption pushes the rivals of a returning hidden god Wild, which is what lets the
// cure become worse than the disease. Any other aspect, including none, is 1.
inline constexpr double kWildAspectFactor = 2.0;
inline constexpr double kSovereignAspectFactor = 0.5;
// A god spares its own faithful and punishes a rival's flock.
inline constexpr double kOwnFaithfulFactor = 0.25;
inline constexpr double kRivalFlockFactor = 2.0;
// Identical to the age fate's cap: a city can never be certain to fall.
inline constexpr double kVisitationChanceCap = 0.9;

inline constexpr double kDefaultWrath = 0.5;

// Stamped on every record in the reference so a reader knows what ran in what order.
std::span<const std::string_view> visitation_order();

// A refused request. The messages are user-visible.
class VisitationError : public std::runtime_error {
public:
    VisitationError(std::string field, std::string message)
        : std::runtime_error(field + ": " + message), field_(std::move(field)) {}
    const std::string& field() const { return field_; }

private:
    std::string field_;
};

// ---------------------------------------------------------------------------
// What a visitation writes.
// ---------------------------------------------------------------------------

// One intensity a summons suppressed, as it was BEFORE the suppression.
//
// Departure writes the value back EXACTLY rather than re-scaling, and it looks the target up
// in the network THIS ENTRY names. One summons suppresses up to three opposed schools, so an
// implementation that reused whichever network it last touched would miss every entry of the
// others -- silently, because a missed id is skipped -- and leave those rivals halved forever.
struct VisitationRestore {
    std::string school;
    // Exactly one of these is set, and which one says whether the entry is a key point or a
    // line: the two live in different lists of the same network.
    std::string node_id;
    std::string line_id;
    double intensity = 0.0;
};

// A standing city of the god's own faith, and how hard it was favoured.
struct VisitationFavor {
    std::string city_uid;
    double strength = 0.0;  // wrath * (1 - distance / reach), to 6 dp
};

// THE KEY FIELD IS `index`: its own position in the world's visitation list.
struct VisitationRecord {
    int index = -1;
    std::string god_id;
    std::string aspect;  // the aspect the god wore when it came
    int node = 0;        // the sphere-grid node it stood on
    Vec3 direction{};
    int age = 0;
    long long day = 0;
    double wrath = kDefaultWrath;
    long long variation = 0;
    // KNOWN-SCHOOL ORDER, not the order of the god's affinity list. The first is the school a
    // divine ruin's legacy takes and the school the footprint is planted in, so a port that
    // kept the affinity order would move both.
    std::vector<std::string> schools;
    double reach_m = 0.0;
    // Five ids per school, school-major and ring-point-minor.
    std::vector<std::string> cluster;
    std::vector<VisitationRestore> restore;
    std::vector<std::string> ruins;
    std::vector<std::string> purged_nests;
    std::vector<VisitationFavor> favored;
    // Peoples, in FIRST-SEEN order over the rebuilt sites -- not sorted.
    std::vector<std::string> converted;
    // Absent while the god walks. Tested by presence: the age a god left in can be zero.
    std::optional<int> departed_age;
    // The sky on the arrival day itself, which the orchestrator may have named.
    DayState moon;
};

// Why a city fell, as the cause that won the selector walk recorded it. The three kinds carry
// different evidence and the kind says which fields are meaningful.
enum class VisitationCauseKind { Ley, Nest, Divine };

struct VisitationEvidence {
    VisitationCauseKind kind = VisitationCauseKind::Divine;
    // Ley: the school that owns the ground and its potency there.
    std::string school;
    double potency = 0.0;
    // Nest. The threat's `name` is deliberately not carried; the reference drops it too.
    std::string nest_id;
    std::string family;
    // Nest and divine.
    double distance_m = 0.0;
    double reach_m = 0.0;
    // Divine.
    std::string god_id;
    std::string aspect;
    double faith_factor = 1.0;
};

// ---------------------------------------------------------------------------
// The two things this port does not own, joined through the request.
// ---------------------------------------------------------------------------

// What a visitation asks the ruins module about one city it ended.
//
// THE RUINS PORT OWNS THE ANSWER. `terrain_ruins.ruin_legacy` resolves a ruin's school by
// the destroyer first, the region's dominant magic second and the ruined people's own school
// last, and the culture step reads the heritage tables. A copy of that ladder here would be
// the second of two that must agree, so the caller supplies it.
struct VisitationLegacyQuery {
    const SettlementSite* city = nullptr;
    // A KNOWN school name for a ley cause, `dragon` or `monster` for a nest, `divine_<god id>`
    // for the god itself.
    std::string_view cause;
    // ALL TWELVE schools in school-table order, hidden ones included: a god smiting a city
    // sees the ground as it is.
    std::span<const double> potencies;
    // The nest's family for a nest cause; empty otherwise.
    std::string_view nest_family;
    // The first school the god saturates, which is what a divine cause's legacy takes.
    std::string_view god_school;
};

struct VisitationLegacy {
    std::string school;
    double intensity = 0.0;
    std::string basis;  // source | region | culture
};

using VisitationLegacyResolver = std::function<VisitationLegacy(const VisitationLegacyQuery&)>;

// A city a visitation ended. Stage 15 owns the real ruin record; this is the slice a
// visitation writes, returned for the caller to append BEFORE its rebuild, as the reference
// does. Of the thirteen fields a ruin copies from its city, the four this port's sites do not
// carry yet -- civilization_id, source_culture, founded_age, war_history -- are absent.
struct VisitationRuin {
    std::string id;  // ruin-<city uid>
    std::string kind = "ruins";
    std::string city_uid;
    std::string name;
    int node = 0;
    int x = 0;
    int z = 0;
    Vec3 direction{};
    double height_m = 0.0;
    std::string population_profile;
    CityClass city_class = CityClass::Small;
    int destroyed_age = 0;
    std::string asset_id = "marker.city_ruins";
    std::string cause;
    std::string reason;
    VisitationEvidence evidence;
    double probability = 0.0;  // the capped chance the city rolled against
    double roll = 0.0;
    // Where the ruin's key point was planted: `<id>-key`, at the legacy intensity.
    std::string new_node_school;
    VisitationLegacy legacy;
    int visitation = -1;
};

// ---------------------------------------------------------------------------
// The summons.
// ---------------------------------------------------------------------------

struct VisitationRequest {
    int api_version = kVisitationApiVersion;
    std::uint32_t seed = 0;
    // The world's age: how many ages have already turned.
    int age = 0;
    // METRES. This is world.effective.globe_radius, not world.config's -- the two differ by
    // the world scale, a factor of 5.6 on the default world, and design units here compare
    // against metre reaches and silently answer a different question.
    double globe_radius_m = 1.0;
    // METRES and ALREADY SCALED -- see kVisitationReachSpacings.
    double settlement_spacing_m = 450.0;

    // The world's node graph. A target node indexes it, and its cell is where the god stands.
    const SphereGrid* grid = nullptr;
    // `water_type` (the god walks on land) and the twelve `ley_<school>` potency grids. A
    // missing ley grid reads as zero, exactly as the reference's lottery reads it.
    const std::map<std::string, Grid>* layers = nullptr;
    // A seeded moon: it decides the moon god's schools and the record's sky.
    Moon moon;
    // settlements.founding.end_year. The arrival day defaults to its first day.
    long long founding_end_year = 0;

    std::string god_id;
    // EXACTLY ONE of these. A city target stands the god on its node.
    std::optional<std::string> target_city_uid;
    std::optional<int> target_node;
    double wrath = kDefaultWrath;  // 0..1
    long long variation = 0;       // 0 .. 2^32-1
    std::optional<long long> day;  // >= 0

    MagicReport magic;
    Religion religion;
    // The world's visitation list, TYPED. `Religion::visitations` is a vector of opaque
    // serialised records the religion pass carries forward without interpreting; this module
    // neither reads nor writes it, because keeping a typed and a serialised copy in step by
    // hand is how two copies drift. Wiring decides which one a world holds.
    std::vector<VisitationRecord> visitations;
    std::vector<SettlementSite> cities;
    // The beast nests standing now. Only fantasy nests threaten a city.
    std::vector<NestSite> nests;

    // LEY_DESTRUCTION: the sentence a city a leyline killed leaves behind, by school. Keyed by
    // the KNOWN eight in the reference, and all eight are required here. A HIDDEN school can
    // own a city's ground on a corrupted world -- a corrupted seat node sits on the host's own
    // seat city at full intensity -- and the reference then raises KeyError. This port refuses
    // by name instead, and an entry for a hidden school, should one be ruled, is data.
    std::map<std::string, std::string, std::less<>> ley_destruction;
    // terrain_ruins.ruin_legacy -- see VisitationLegacyQuery.
    VisitationLegacyResolver ruin_legacy;
};

// Everything up to the rebuild.
struct VisitationSummons {
    int index = -1;
    std::string god_id;
    std::string aspect;
    int node = 0;
    Vec3 centre{};
    int age = 0;
    long long day = 0;
    double wrath = kDefaultWrath;
    long long variation = 0;
    double reach_m = 0.0;
    double globe_radius_m = 1.0;
    std::vector<std::string> schools;
    std::vector<std::string> cluster;
    std::vector<VisitationRestore> restore;
    DayState moon;

    // Append these to the world's ruins BEFORE the rebuild.
    std::vector<VisitationRuin> new_ruins;
    // Recorded now, while the lottery's distances are in hand; applied as relief after the
    // rebuild re-assesses threats. The reference also writes a `favor` list onto each such
    // city, and its own rebuild then discards it -- founding carries only a fixed set of
    // survivor keys and `favor` is not one -- so this port writes only the two halves that
    // last: this list, and the threat relief.
    std::vector<VisitationFavor> favored;
    // Every city that still stands, in site order: what the tail rebuild re-founds over.
    std::vector<SettlementSite> survivors;
    // The cluster, the suppression and each ruin's key point. The ley FIELDS are not
    // re-evaluated here: the lottery read the ground as it was before the god arrived, and the
    // caller's rebuild is what re-evaluates it.
    MagicReport magic;

    // Carried unchanged for the second half, which writes the records onto the block the
    // summons saw: the reference does not re-resolve religion inside a summons's rebuild.
    Religion religion;
    std::vector<VisitationRecord> visitations;

    bool rebuild_required = true;
};

// What the caller's rebuild produced, which the second half reads.
struct VisitationRebuilt {
    // Re-founded over `survivors`. Conversion reads THESE.
    std::vector<SettlementSite> cities;
    // Regenerated. The purge runs on these because purging before the rebuild would be undone
    // by it.
    std::vector<NestSite> nests;
    // Re-assessed. Only `city_uid` and `regional_threat` are read.
    std::vector<VillainThreatReport> reports;
};

// A favoured city's report, as it must now read.
struct VisitationRelief {
    std::string city_uid;
    double divine_favor = 0.0;
    double regional_threat = 0.0;  // max(0, before - kFavorRelief * favour), to 6 dp
};

// One entry for the world's operation history.
struct VisitationOperation {
    int api_version = kVisitationApiVersion;
    std::string kind;  // visitation | departure
    int visitation = -1;
    std::string god_id;
    // A summons only.
    std::optional<int> node;
    std::optional<double> wrath;
    std::optional<long long> variation;
};

struct VisitationOutcome {
    VisitationRecord record;
    // The rebuilt nests with the purged removed, in their own order.
    std::vector<NestSite> nests;
    std::vector<VisitationRelief> relief;
    // The god walking with its avatar, the converted faiths, and the theophany site. The
    // caller re-resolves religion with this as `previous`, which carries all three forward:
    // `walking` is sticky, theophany sites are copied, and conversions are read back.
    Religion religion;
    std::vector<VisitationRecord> visitations;  // with `record` appended
    VisitationOperation operation;
};

// ---------------------------------------------------------------------------
// Departure, which is the part an age transition calls.
// ---------------------------------------------------------------------------

struct VisitationDepartureRequest {
    std::string god_id;
    // Written as the record's departed age. The reference defaults it to the world's age.
    int age = 0;
    MagicReport magic;
    Religion religion;
    std::vector<VisitationRecord> visitations;
};

struct VisitationDeparture {
    VisitationRecord record;  // as it now reads, departed
    MagicReport magic;
    Religion religion;
    std::vector<VisitationRecord> visitations;
    std::string footprint;  // footprint-<god id>-<index>
    // Restore entries whose key point or line no longer exists and were skipped, as the
    // reference skips them. A god whose own schools oppose each other suppresses its own
    // cluster, and those entries always miss because the cluster is removed first.
    std::vector<std::string> unrestored;
    VisitationOperation operation;
    // The cluster collapsed and the rivals recovered, so the ley field moved. A departure
    // REQUEST owes the tail rebuild over the unchanged sites and then a religion resolve with
    // `previous` = `religion`. An AGE TURN owes nothing extra: its own tail rebuild, which
    // re-resolves religion, follows the departure loop.
    bool rebuild_required = true;
};

// ---------------------------------------------------------------------------
// The pure pieces, exposed because they are the properties worth testing alone.
// ---------------------------------------------------------------------------

// 3 x settlement spacing. The world's size does not appear: spacing already carries it.
double visitation_reach_m(double settlement_spacing_m);

// The schools a god's avatar saturates, in KNOWN-SCHOOL order. The moon god has no school of
// its own and takes whichever hemisphere is leaning in on the day. Throws for a god the known
// catalogue does not have -- a hidden god walks only by corruption -- and for the moon god
// against an unseeded moon, whose zero periods would divide by zero.
std::vector<std::string> god_schools(std::string_view god_id, const Moon& moon, long long day);

// How hard a god strikes a city of this people: its own faithful are spared, a rival's flock
// is punished, and anyone else takes the plain weight. A rivalry counts from either side.
double visitation_faith_factor(const Religion& religion, std::string_view population_profile,
                               std::string_view god_id);

// ---------------------------------------------------------------------------
// The passes.
// ---------------------------------------------------------------------------

// Avatar cluster, opposed suppression, fate lottery, ruins and their key points.
//
// Throws VisitationError on a refused request. Every refusal is checked up front except two
// that depend on the ground itself and so come from inside the lottery: a city in reach whose
// ground a hidden school owns, and a legacy that names a school with no network. Nothing is
// written anywhere until the value is returned, so a throw leaves the caller's world as it was.
VisitationSummons summon_god(const VisitationRequest& request);

// Nest purge, favour relief and conversion on the rebuilt world, then the records.
VisitationOutcome complete_summons(const VisitationSummons& summons, const VisitationRebuilt& rebuilt);

// The gods an age turn sends home, in pantheon order.
//
// ONLY GODS THAT WALK BY VISITATION. Two different things set a god `walking` and only one of
// them writes a visitation record: a summons does; a hidden god's return through corruption
// does not, because a corrupted god's footprint is its ley cluster, which persists across
// ages and is opposed by cleansing it. The reference returned every god whose status read
// `walking`, so the first age turn after any corruption reached `depart_god` with a god that
// has nowhere to go back to and raised -- a corrupted world could not advance an age.
//
// The question an age turn asks is "who has somewhere to go back to", and that is answered by
// an undeparted record, not by a status. This is the whole filter the age transition relies
// on: call exactly this, then `depart_god` for each id it names.
std::vector<std::string> walking_gods(const Religion& religion,
                                      const std::vector<VisitationRecord>& visitations);

// The god leaves: the cluster collapses to a footprint, the rivals recover EXACTLY, the
// theophany becomes a pilgrimage and the god is manifest again.
//
// STRICT, deliberately. It refuses by name a god that is not in the pantheon, one that does
// not walk, and one that walks without an undeparted visitation record -- a god revealed by
// corruption is opposed through its corruption, not sent home. A no-op here would turn "send
// this god home" into a reported success that leaves it walking forever. The age turn never
// reaches the refusal, because `walking_gods` hands it only gods with a record.
VisitationDeparture depart_god(const VisitationDepartureRequest& request);

}  // namespace fwg
