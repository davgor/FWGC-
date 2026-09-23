// Corruption: a hidden god seats a standing super villain as its instrument.
//
// This is the ONLY path by which a hidden magic school ever gets a leyline node.
// Generation locks hidden-school occurrence at zero and player leyline edits are
// restricted to the known eight, so nothing else in the library can reach them.
//
// The gate is the game's own ledger. It reports how many villains the player has met
// across playthroughs as `encounters`, and below a god's noticing point the chance is
// EXACTLY zero -- the gods are not watching yet. Above it, it climbs. Which god takes an
// interest is never rolled: it is read from the world, so the corruption fits the place it
// happens.
//
// The corrupted NODE, not the villain, is what persists. Killing the holder leaves the
// nodes standing and an unheld node is a spawner; the companion cleanse drains them back
// down and unseats the holder with its last node. That is the other order.
//
// Stateless, like the reference: same request in, same value out, and nothing the caller
// owns is modified. The two passes return the halves of the world they rewrote.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/magic.hpp"
#include "fwg/religion.hpp"
#include "fwg/settlement.hpp"
#include "fwg/villains.hpp"

namespace fwg {

inline constexpr int kCorruptionApiVersion = 1;

// Ring nodes around the seat, so a cluster is 1 + 3 = 4 nodes.
inline constexpr int kCorruptionRingPoints = 3;
// Ring radius in settlement spacings -- 3 229 m on the 200 km reference world. SCALES with
// the planet, because settlement spacing does.
inline constexpr double kCorruptionRingSpacings = 0.4;
// The FLOOR under a corrupted villain's reach, 20 181 m on the reference world. It matters
// because a villain whose own seat city has since fallen still sits where its node is, and
// the nearest survivor can be further away than a fixed constant would ever carry. SCALES.
inline constexpr double kCorruptionReachSpacings = 2.5;
// Exactly the network ceiling: a corrupted seat is a maximal source.
inline constexpr double kCorruptionSeatIntensity = 4.0;
inline constexpr double kCorruptionRingIntensity = 2.5;
// Corrupted nodes a world may carry at once; cleansing frees them. At four per corruption
// that is twenty-four concurrent corruptions.
inline constexpr int kCorruptionNodeBudget = 96;
// Of a city's people, AT THE SEAT, when blood takes it.
inline constexpr double kCorruptionFarmShare = 0.45;
// A node's full intensity, so power 1.0 ends one in a single tick. At the default power of
// 0.25 the drain is 1.0 per tick: the seat node needs four, the ring nodes three.
inline constexpr double kCleanseMaxDrain = 4.0;
inline constexpr double kCleanseDefaultPower = 0.25;

// The ruin intensity a city's class leaves behind. Keyed on city class, defaulting to
// small. The divine 4.0 branch does NOT fire for these causes, because neither
// `rot_plague` nor `void_unmade` starts with `divine`.
inline constexpr double kClassIntensitySmall = 1.5;
inline constexpr double kClassIntensityMedium = 2.5;
inline constexpr double kClassIntensityCapital = 3.5;

// Stamped on every record so a reader knows what ran in what order.
std::span<const std::string_view> corruption_order();

// A refused request. The reference raises one exception type carrying these fields out of
// both request APIs, and the messages are user-visible.
class CorruptionError : public std::runtime_error {
public:
    CorruptionError(std::string field, std::string message)
        : std::runtime_error(field + ": " + message), field_(std::move(field)) {}
    const std::string& field() const { return field_; }

private:
    std::string field_;
};

// ---------------------------------------------------------------------------
// What a corruption writes.
// ---------------------------------------------------------------------------

// What the four do to a city, which is the whole reason they play differently.
struct CorruptionTouch {
    std::string city_uid;
    std::string effect;  // farmed | taken
    double people_lost = 0.0;
};

// The key point a city unmade by a walking god leaves behind.
//
// The god IS the source, so the school and the basis are stated rather than resolved. The
// reference asks `ruin_legacy` for all three and overwrites two of them; only the class
// intensity survives, which is why no potency grid is read here at all.
struct CorruptionLegacy {
    std::string school;
    std::string basis = "source";
    double intensity = 0.0;
};

// A city ended by rot or void. Stage 15 owns the real ruin record; this is the slice
// corruption writes, returned for the caller to append rather than written into a block
// this port does not own.
struct CorruptionRuin {
    std::string id;  // ruin-<city uid>
    std::string kind = "ruins";
    std::string city_uid;
    int node = 0;
    Vec3 direction{};
    std::string population_profile;
    CityClass city_class = CityClass::Small;
    int destroyed_age = 0;
    std::string asset_id = "marker.city_ruins";
    std::string cause;   // rot_plague | void_unmade
    std::string reason;  // "<god name>: <god smite>."
    // evidence
    std::string god_id;
    std::string school;
    double share = 0.0;
    int corruption = -1;
    // Certain, not rolled: the four do not miss inside their reach.
    double probability = 1.0;
    double roll = 0.0;
    // WRITTEN AND NEVER ACTED ON, faithfully. An age transition is what turns this into a
    // ley node and a visitation does it explicitly for its own ruins; the reference's
    // corruption does neither, so `ruin-<uid>-key` appears in no network. Planting it here
    // would change the ley field of every corrupted world, which is a ruling for the owner
    // of stage 15 rather than a porting decision.
    std::string new_node_school;
    CorruptionLegacy legacy;
};

// THE KEY FIELD IS `index`: its own position in the world's corruption list.
struct CorruptionRecord {
    int index = -1;
    std::string god_id;
    std::string school;
    std::string villain_uid;
    int age = 0;
    long long encounters = 0;
    long long variation = 0;
    std::vector<std::string> nodes;  // the four corrupt-<god>-<index>-<k> ids
    double reach_m = 0.0;
    std::vector<CorruptionTouch> touched;
    std::vector<std::string> ruins;
    // Absent while the corruption stands. Tested by PRESENCE and not by truthiness: the
    // reference's node-budget counter uses `if not c.get('cleansed_age')`, which counts a
    // record cleansed in age 0 as still standing, while its own cleanse validator two
    // hundred lines later correctly uses `is not None`. The two disagreed; this is the one
    // that is right.
    std::optional<int> cleansed_age;
    // Which KNOWN gods the revelation roused to their Wild aspect.
    //
    // Recorded here and not on the god records, because `resolve_gods` rebuilds every known
    // god from scratch and recomputes its aspect: a `roused_by` written onto a known record
    // is erased by the next resolve, while a hidden god's `walking` status is carried
    // forward. That asymmetry is the reference's and is deliberate -- this is where the
    // fact survives it.
    std::vector<std::string> roused;
};

// ---------------------------------------------------------------------------
// The request.
// ---------------------------------------------------------------------------

struct CorruptionRequest {
    int api_version = kCorruptionApiVersion;
    std::uint32_t seed = 0;
    // The world's age, which is how many ages have already turned.
    int age = 0;
    // METRES, from world.effective -- see the same warning on VillainRequest.
    double globe_radius_m = 1.0;
    double settlement_spacing_m = 450.0;

    // The game's ledger of villains the player has met across playthroughs, 0 .. 2^32-1.
    long long encounters = 0;
    long long variation = 0;
    // Absent means "whoever reaches furthest". An explicit uid BYPASSES the
    // already-corrupted filter, so a second god can take a host the first already has.
    std::optional<std::string> villain_uid;

    std::vector<VillainCity> cities;
    // `interest("decay")` reads the COUNT alone. The ruins this pass makes come back on the
    // outcome for the caller to append.
    int existing_ruins = 0;
    // `interest("aberrant")` reads the FAMILY alone, over the beast nests.
    std::vector<std::string> nest_families;

    MagicReport magic;
    Religion religion;
    VillainBlock villains;
    // The world's corruption list, TYPED. `Religion::corruptions` is a vector of opaque
    // serialised records that the religion pass carries forward without interpreting, and
    // this pass neither reads nor writes it: wiring has to decide whether a world holds
    // these typed or serialised, and keeping both in step by hand is how they drift.
    std::vector<CorruptionRecord> corruptions;
};

struct CorruptionOutcome {
    bool acted = false;
    long long encounters = 0;
    // Every hidden god watching this world, most interested first. Reported whether or not
    // anything acted, because "who is watching" is the answer a caller escalates on.
    std::vector<std::string> watching;
    // Only when nothing acted. Note the reference's own wording bug, reproduced: a
    // non-empty watch list with no eligible host says "a watching god did not act this
    // time" although nothing was rolled at all.
    std::string reason;

    std::string god_id;
    std::string villain_uid;
    int corruption = -1;
    std::optional<CorruptionRecord> record;

    // The halves of the world this rewrote. Identical to the request's when nothing acted.
    //
    // `survivors` carries blood's reduced population and eldritch's `taken_by` mark. The tail
    // rebuild re-derives the population from the habitat budget, as the reference's does;
    // the world then marks each farmed or taken city held by the god's school (ruled
    // 2026-09-23, world_api.hpp corrupt), a mark every later rebuild carries.
    std::vector<VillainCity> survivors;
    std::vector<CorruptionRuin> new_ruins;
    MagicReport magic;
    Religion religion;
    VillainBlock villains;
    std::vector<CorruptionRecord> corruptions;

    // The tail rebuild -- founding, biomes, civilization, nests, threat assessment -- runs
    // over `survivors` and is stages 12-16, which this library does not have. True
    // whenever something acted; see `reveal_hidden_god` for the one step that must be
    // replayed after that rebuild re-resolves the pantheon.
    bool rebuild_required = false;
};

// ---------------------------------------------------------------------------
// Reading the world, which is how the god is chosen.
// ---------------------------------------------------------------------------

// How much a god wants this world, in 0..1. READ, NEVER ROLLED.
//
// Throws on a kind no hidden god declares -- a silent zero would make the god that
// declares it permanently uninterested and look like a quiet world.
double hidden_interest(std::string_view kind, const CorruptionRequest& request);

// Exactly zero below the noticing point, then a climb toward certain.
//
// Zero and not merely small: a guaranteed-clean early playthrough is what makes the first
// corruption read as something the player caused.
double hidden_chance(const HiddenGod& god, long long encounters);

// Every hidden god whose noticing point is passed and whose existence rule holds, ranked
// most interested first with ties by id -- decided by the world and then by name, never by
// a roll.
std::vector<const HiddenGod*> watching_gods(const CorruptionRequest& request);

// The villain a god reaches for: the named one, else the one with the most reach.
//
// Already taken by a hidden god is the ONLY disqualification. Being sworn to an old one is
// not: a villain bound to a school god is precisely who a hidden god reaches for, and
// excluding them made every seated villain ineligible the moment they started sinking
// wells. Ties on equal tier go to the lexicographically GREATEST uid.
const Villain* corruption_host(const VillainBlock& villains,
                              const std::optional<std::string>& villain_uid);

// The known gods stop not speaking of it.
//
// Before a return there is no trace of the hidden four anywhere; afterwards the known
// pantheon gains it as a rival and its aspects are pushed Wild, which is what makes the
// cure able to become worse than the disease -- a Wild god destroys four times as much as
// a Sovereign one.
//
// IDEMPOTENT, and public for that reason: the reference runs it AFTER the rebuild's own
// pantheon resolve, precisely so the resolve cannot overwrite it. A caller that re-resolves
// religion after the tail rebuild must call this again on the result. Returns the known
// gods it roused.
std::vector<std::string> reveal_hidden_god(Religion& religion, std::string_view god_id,
                                           std::string_view host_uid, int age);

// Gate, god, host, corrupted cluster, failure mode, ruins, record, pantheon reaction.
//
// Throws CorruptionError on a refused request. A well-formed request that nothing bites on
// returns an outcome with `acted == false` and the world unchanged.
CorruptionOutcome corrupt_world(const CorruptionRequest& request);

// ---------------------------------------------------------------------------
// Cleansing, which is also how a god is opposed.
// ---------------------------------------------------------------------------

struct CleanseRequest {
    int api_version = kCorruptionApiVersion;
    int age = 0;
    // Share of the structure drained, in (0, 1]. The structure drains as it ramps rather
    // than merely out-competing, because out-competing cannot work: potency saturates at a
    // network's strength, so a corrupted node at full intensity sits above what any
    // counter-node of equal strength can reach.
    double power = kCleanseDefaultPower;

    // EXACTLY ONE of these. A god target is refused by name in this port, and in the
    // reference too for every god corruption revealed: a god revealed by corruption is
    // opposed through its corruption rather than sent home, because it walks without a
    // visitation record and departure requires one.
    std::optional<int> corruption;
    std::optional<std::string> god_id;

    MagicReport magic;
    Religion religion;
    VillainBlock villains;
    std::vector<CorruptionRecord> corruptions;
};

struct CleanseOutcome {
    std::string kind = "corruption";
    int corruption = -1;
    std::string god_id;
    std::vector<std::string> removed;  // node ids driven to zero THIS tick
    int remaining = 0;
    bool cleansed = false;

    MagicReport magic;
    Religion religion;
    VillainBlock villains;
    std::vector<CorruptionRecord> corruptions;

    // Nobody dies from a cleanse, but the ley field moved, so the caller still owes the
    // tail rebuild over its unchanged settlement sites.
    bool rebuild_required = false;
};

CleanseOutcome cleanse_world(const CleanseRequest& request);

}  // namespace fwg
