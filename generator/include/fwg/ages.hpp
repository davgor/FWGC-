// Stages 14 and 15: an age turns.
//
// Everything before this point builds one world at one moment. An age transition is where
// that world acquires a past: cities fall to wars, lairs, the Weave and the villains that
// rose in the age just ended; each ruin scars the ley field with a key point; the field
// drifts; and civilization is rebuilt around the cities that stood. Ruins, villains and
// the history are written here and nowhere else.
//
// The order is the reference's and it is load-bearing:
//
//    1. nests on the ground the finished age left      (beast_nests.evaluated_age = age - 1)
//    2. the moon on the day the age turns
//    3. the villains advance, on the assessment this age INHERITED -- so a villain is always
//       one rebuild behind the region it is eating and is never raised by its own damage
//    4. wars, between the cities the finished age left; both sides carry each war
//    5. every city's fate: a war's loser falls to its war, the rest roll the lottery
//    6. only then does the ley field move: it drifts, each ruin plants its key point, and a
//       god still walking goes home
//    7. refugee bands that reached a standing town are absorbed by it
//    8. the rebuild: fields, environment, biome variants, civilization over the survivors,
//       nests (evaluated_age = age), the threat assessment, religion
//    9. at the final age, promotion; then the villains' works and their outlook
//   10. the age's record
//
// Every death is decided against the fields as the finished age left them; nothing a fate
// decides can change another city's odds in the same age.
#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "fwg/fates.hpp"
#include "fwg/magic.hpp"
#include "fwg/nomad_api.hpp"
#include "fwg/settlement_api.hpp"
#include "fwg/settlements_stage.hpp"
#include "fwg/time_state.hpp"
#include "fwg/vec3.hpp"
#include "fwg/visitation.hpp"
#include "fwg/wars.hpp"

namespace fwg {

struct World;

inline constexpr int kHistoryVersion = 3;

// Where a fallen city stood. Keyed on `id` = "ruin-<city uid>", which survives every
// renumbering because the city's uid does.
//
// The first block is the city as it stood (the reference's RUIN_KEYS). The fate fields are
// the lottery's or the war's.
struct Ruin {
    std::string uid;
    std::string name;
    int node = 0;
    int x = 0;
    int z = 0;
    Vec3 direction{};
    double height_m = 0.0;
    std::string population_profile;
    // The reference's classify_cities stamps every city with its people as its
    // civilization, so the two are the same string; the heroes name a ruin's people by this
    // one and refuse a ruin without it.
    std::string civilization_id;
    CityClass city_class = CityClass::Small;
    std::string source_culture;
    int founded_age = 0;
    std::vector<WarParticipation> war_history;

    std::string id;  // ruin-<uid>
    std::string kind = "ruins";
    int destroyed_age = 0;
    std::string asset_id = "marker.city_ruins";

    std::string cause;
    std::string reason;
    CityFateEvidence evidence;
    double probability = 0.0;
    double roll = 0.0;
    // The school whose network got `<id>-key`. Empty only where magic is off; every age
    // ruin's legacy names a school, and the key point is planted at the legacy intensity.
    std::string new_node_school;
    RuinLegacy legacy;

    // A god's work. A corruption's ruin (cause rot_plague | void_unmade) names the god, the
    // share of the falloff it was hit at and the corruption record; a visitation's ruin names
    // the visitation, and a divine cause (divine_<god id>) the god, its aspect and the faith
    // factor the city's own faith bought it. Absent on an age ruin.
    std::string god_id;
    std::string aspect;
    std::optional<double> share;
    std::optional<double> faith_factor;
    std::optional<int> corruption;  // index into World::corruptions
    std::optional<int> visitation;  // index into World::visitations
};

// The steps an age runs, stamped on every record so a reader knows what ran in what order.
std::vector<std::string_view> age_order();

struct AgeRecord {
    int age = 0;
    std::vector<Ruin> events;     // this age's ruins, as they fell
    std::vector<WarRecord> wars;  // in the order they were fought
    // The moon on the day the age turned. Absent for a world with no sky.
    std::optional<CityFateLunar> moon;
    std::vector<std::string> surviving_city_ids;  // sorted
    std::vector<std::string> new_city_ids;        // in settlement order
};

// ---------------------------------------------------------------------------
// Advancing a finished world by more ages.
// ---------------------------------------------------------------------------

inline constexpr int kAgeApiVersion = 1;
inline constexpr int kMaxAgeSteps = 10;

// A refused advance. The messages are user-visible; `field` names what was wrong.
class AgeAdvanceError : public std::runtime_error {
public:
    AgeAdvanceError(std::string field, const std::string& message)
        : std::runtime_error(field + ": " + message), field_(std::move(field)) {}
    const std::string& field() const { return field_; }

private:
    std::string field_;
};

// What an advance did, kept so a world records every advance and edit made since genesis.
struct AgeOperation {
    int api_version = kAgeApiVersion;
    int after_age = 0;
    int steps = 1;
    std::vector<LeylineEdit> leyline_edits;
    std::optional<long long> moon_day;
};

inline constexpr std::string_view kAgeReplayNote =
    "Persist this returned world for subsequent calls. Config reproduces genesis; operation history "
    "records subsequent advances and player leyline edits.";

// A hidden god taking a villain.
struct CorruptionOperation {
    int api_version = 1;
    int corruption = -1;  // index into World::corruptions
    std::string god_id;
    std::string villain_uid;
    long long encounters = 0;
    long long variation = 0;
};

// A corruption drained, or a walking god driven off the plane.
//
// The reference writes this record as {"kind": "cleanse", ..., **report}, and the report
// carries its own `kind` -- "corruption" or "god" -- which overwrites the first, so the
// logged kind is the report's and never "cleanse". Reproduced: `kind` here is the report's.
struct CleanseOperation {
    int api_version = 1;
    std::string kind;  // corruption | god
    std::optional<int> target_corruption;
    std::optional<std::string> target_god_id;
    double power = 0.0;
    int corruption = -1;
    std::string god_id;
    std::vector<std::string> removed;
    int remaining = 0;
    bool cleansed = false;
    bool departed = false;
};

// One line of the log every API after generation appends to, in call order: an age
// advance, a corruption, a cleanse, a summons or a departure, a span of time, a batch of
// quest actions, a player's founding, or a band raised on request.
using HistoryOperation = std::variant<AgeOperation, CorruptionOperation, CleanseOperation, VisitationOperation,
                                      TimeOperationRecord, QuestOperationRecord, SettlementOperation,
                                      NomadOperation>;

struct WorldHistory {
    int version = kHistoryVersion;
    std::vector<AgeRecord> ages;
    // Every operation made after generation, in order. Empty for a world as generated.
    std::vector<HistoryOperation> operations;
    // How to replay the operations, in the words of the API that wrote last: the age API's
    // kAgeReplayNote or the time API's sentence. Empty for a world as generated.
    std::string replay;
};

struct AgeAdvanceRequest {
    int api_version = kAgeApiVersion;
    int steps = 1;  // 1..kMaxAgeSteps
    // Applied in order before the first age turns; at most kMaxLeylineEdits.
    std::vector<LeylineEdit> leyline_edits;
    // The day the ages turn on. Absent: the founding report's end year decides.
    std::optional<long long> moon_day;
};

// Turn `request.steps` more ages over a finished world, after the player's ley edits, and
// re-run stage 16 over the result. Same request in, same world out; the caller's world is
// untouched. The world must have been generated through stage 13 with the sky and the
// pantheon, against this build's pantheon catalogue. Throws AgeAdvanceError naming the
// field that refused.
//
// Like the reference's advance_age_request, it records the wall time of the call as
// `timing_ms["age_advance_total"]`, which the time API's age cost estimate divides by this
// call's `steps` to price one age from this world's own measurement.
World advance_ages(const World& world, const AgeAdvanceRequest& request);

// The versioned state boundary every API after generation checks a world against: recipe
// 3, generated through creatures (stage 13) on a grid of at most 1025, with the sky and the
// pantheon of this build's catalogue. Throws AgeAdvanceError naming the field that refused,
// returns quietly when it can.
//
// Exposed because the time API's age gate and the corruption API ASK this rather than
// restating it. The
// reference once hard-coded a grid ceiling there and named this check as its source; the
// ceiling had already been raised, so a whole band of worlds was refused a span it would
// have advanced. Asking the real predicate is the only version that cannot go stale.
void validate_age_world(const World& world);

// The villains' outlook refreshed over the world as it stands, WITHOUT advancing the cast.
// The time API's villain_outlook cadence. A villain rises, holds or falls at an age
// boundary and nowhere else -- advancing reads the threat assessment an age inherited, so
// raising one mid-tick would let a villain be raised by its own damage -- but the outlook
// is a reading of present ground and is safe to refresh. Does nothing on a world with no
// villain block.
void refresh_villain_outlook(World& world);

// Turn age `age` over `world`. `moon_day` absent means the founding report's end year in
// days, which is the day both generation-time ages turn on. `final` promotes the villains
// at the end: stage 15, never stage 14, so a phase-14 world agrees with stage 14 of a
// phase-16 one.
//
// Needs stage 13's world: settlements, roads, the hinterland, nests, threat assessment.
void age_transition(World& world, int age, std::optional<long long> moon_day = std::nullopt,
                    bool final_age = false);

// The tail an age transition runs after the fates, and a visitation after its summons:
// fields, environment, biome variants, civilization over `survivors`, nests, the threat
// assessment labelled `label`, and religion when `religion` is set (an age turn re-resolves
// the pantheon; a visitation resolves it itself once its own records are written, and only
// refreshes the sky here). `evaluate` re-runs the ley fields; an age with magic off skips it.
void rebuild_tail(World& world, const std::vector<SettlementSite>& survivors, const std::string& label, int age,
                  bool evaluate, bool religion);

}  // namespace fwg
