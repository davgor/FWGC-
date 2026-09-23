// The world-level mutators a game calls between sessions: a hidden god corrupting a villain,
// a cleanse that drains a corruption or drives a walking god off the plane, and a god summoned
// to the mortal plane or sent home.
//
// Each takes the world by const reference and returns a new one; the caller's world is never
// touched, so the caller persists what comes back. The passes themselves are pure and live in
// corruption.hpp and visitation.hpp. This file is the glue the reference's *_request functions
// are: copy the world, adapt it into the pass's request, run it, write the result back, run the
// rebuild the pass owes, and log the operation in history.operations.
//
// What each one redraws is the reference's, deliberately and unevenly:
//
//   corrupt        drops the plans and the scene and does not redraw them
//   cleanse        a corruption: the same; a god: leaves the plans standing
//   summon/depart  redraws the city, hamlet and castle plans and the scene
//
// None of them re-runs the bands, the cast, the rosters or the key locations; those are
// stage 16's and an age advance redraws them. A consumer that needs them current after one of
// these calls advances an age or regenerates.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "fwg/corruption.hpp"
#include "fwg/visitation.hpp"
#include "fwg/world.hpp"

namespace fwg {

// --- corruption --------------------------------------------------------------

struct CorruptWorldRequest {
    int api_version = kCorruptionApiVersion;
    // The game's ledger of villains the player has met across playthroughs, 0 .. 2^32-1.
    long long encounters = 0;
    long long variation = 0;
    // Absent: whoever reaches furthest. Present: that villain, even one already taken.
    std::optional<std::string> villain_uid;
};

// What the call did. When nothing acted the world comes back unchanged and says why.
struct CorruptionReport {
    bool acted = false;
    long long encounters = 0;
    std::vector<std::string> watching;  // only when nothing acted
    std::string reason;                 // only when nothing acted
    std::string god_id;
    std::string villain_uid;
    int corruption = -1;
};

struct CorruptWorldResult {
    World world;
    CorruptionReport report;
};

// Gate, god, host, corrupted cluster, failure mode, ruins, the rebuild over the survivors, the
// pantheon's re-resolve and the hidden god revealed walking. Throws CorruptionError naming the
// field on a refused request.
//
// A city blood farmed or eldritch took is marked held by the god's school on the rebuilt site
// (SettlementSite::corruption, ruled 2026-09-23), and every later rebuild carries the mark. The
// rebuild still re-derives the population; what each city lost, and who took it, stays in the
// corruption record's `touched` list.
CorruptWorldResult corrupt(const World& world, const CorruptWorldRequest& request);

// --- cleansing ---------------------------------------------------------------

// The share of a cleansed city's people who get out, drawn per city (ruled 2026-09-23).
inline constexpr double kCleanseEscapeLow = 0.10;
inline constexpr double kCleanseEscapeHigh = 0.40;
// The quest line's difficulties: raising the city again is as hard as the city was large
// (small 2, medium 3, capital 4); leading its people to their kin is 2.
inline constexpr int kReturnDifficulty = 2;

struct CleanseWorldRequest {
    int api_version = kCorruptionApiVersion;
    // Exactly one of these.
    std::optional<int> corruption;
    std::optional<std::string> god_id;
    std::optional<std::string> city_uid;  // a city a hidden school holds
    // Share of the structure drained, in (0, 1]. Read by a corruption target only.
    double power = kCleanseDefaultPower;
};

struct CleanseWorldResult {
    World world;
    CleanseOperation report;  // the logged operation is the report
};

// A corruption target drains its nodes and rebuilds; a god target sends a god that walks by
// visitation home and rebuilds. A god revealed by corruption is refused by name: it is opposed
// through its corruption. Throws CorruptionError naming the field.
//
// CLEANSING DESTROYS WHAT A HIDDEN SCHOOL HOLDS (ruled 2026-09-23). A city target -- only a
// city marked held (SettlementSite::corruption) -- is destroyed: a ruin, cause "cleansed", and
// no key point. So is every city a corruption holds when a cleanse drains that corruption's
// last node; a partial drain destroys nothing. Of each destroyed city's people, a share drawn
// per city from the world's seed (kCleanseEscapeLow..High) gets out and is raised as one
// survivors band at the ruin (raise_refugees), and the band gives a quest line through the
// cast's hooks: `rebuild` the city from its ruins, or `return` its people to their kin. The
// board offers both on the next span of time; a band that cannot be raised (no refuge within
// the survivors' reach) or a world with no cast is named in the city's `note` instead.
CleanseWorldResult cleanse(const World& world, const CleanseWorldRequest& request);

// --- visitation ----------------------------------------------------------------

struct SummonRequest {
    int api_version = kVisitationApiVersion;
    std::string god_id;
    // Exactly one of these.
    std::optional<std::string> target_city_uid;
    std::optional<int> target_node;
    double wrath = kDefaultWrath;  // 0..1
    long long variation = 0;       // 0 .. 2^32-1
    // The arrival day. Absent: the founding report's end year, in days.
    std::optional<long long> day;
};

// Cluster, suppression, the divine lottery and its ruins, the rebuild over the survivors, the
// nest purge, the favour, the conversions, the records, the pantheon's re-resolve, and the plans
// redrawn. Throws VisitationError naming the field.
World summon(const World& world, const SummonRequest& request);

// The god goes home: its cluster collapses to a footprint, the rivals it suppressed recover
// exactly, the theophany becomes a pilgrimage, the world rebuilds and the plans are redrawn.
// Throws VisitationError for a god that does not walk by visitation.
World depart(const World& world, const std::string& god_id);

// --- the records the religion block carries -------------------------------------

// The typed records as the compact JSON the religion pass carries forward opaquely. Every call
// above rewrites religion.visitations and religion.corruptions from World's typed lists with
// these, so the two can never disagree.
std::string visitation_record_json(const VisitationRecord& record);
std::string corruption_record_json(const CorruptionRecord& record);

}  // namespace fwg
