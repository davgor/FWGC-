// Bounded questions about a world that already exists: the request half of the read surface,
// glue over the selectors in read_select.hpp.
//
// The point of the package is arithmetic. A generated world is megabytes, and the named
// consumer is a local model packaged inside the game with finite context. It cannot hold a
// world, so every question it has must be answerable without handing it the planet. Each
// read here answers in kilobytes.
//
// **A closed verb set, not a query language.** Five named reads -- blocks, near, place,
// person, quests -- each with its own typed request, cost less and refuse better than a
// parser would. A sixth, `patch`, is this port's addition (see read_patch).
//
// **What a read never does.**
//
//   * Never mints a version or records an operation: a read answers with its own document
//     and the world's history is untouched.
//   * Never mutates: every read takes the world by const reference.
//   * Never regenerates. The patch request API (patch.hpp's patch_request) rebuilds the whole
//     world from its config before cutting a patch; that is deliberate there, and it is the
//     one thing a read whose purpose is to be orders cheaper than a generate must not copy.
//     read_patch cuts the patch from the world it is handed.
//   * Never invents a join: every join is resolved in read_select.hpp against the field the
//     writer wrote, and named again in the `joins` rows of the answer.
//
// **Bounded is a number, not a word.** `near` and `quests` REFUSE past a declared row ceiling
// rather than truncating, because both have a remedy the caller can apply -- a smaller radius,
// a narrower state filter. `place` has no such argument, so its roster posts are projected and
// capped with the true count reported beside them.
//
// REFUSALS throw WorldTimeError (world_time.hpp), the envelope the other World-level request
// APIs already refuse in: code, the field that refused, the value received, whether a retry
// can help, candidates, and whether the WORLD is the constraint. A third error type would be a
// third envelope for one caller to learn. The one exception is `patch`, whose refusals are
// generate_patch's own PatchError: it carries the bounds and the clamp suggestion a patch
// config needs and this envelope does not have. Refusals that exist only because the
// reference parsed JSON -- a body that is not an object, an unknown key, a node sent as a
// string, a world that is not an object -- cannot be expressed with typed arguments and are
// not simulated.
//
// Ported from `Sim/icarus_sim/terrain_read.py` (READ_API_VERSION 1, READ_SCHEMA_VERSION 1).
// The Python tree is being deleted; this is the only copy.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/patch.hpp"
#include "fwg/read_select.hpp"
#include "fwg/world_time.hpp"

namespace fwg {

struct World;

inline constexpr int kReadApiVersion = 1;
inline constexpr int kReadSchemaVersion = 1;
inline constexpr std::string_view kReadSchemaPrefix = "fantasy-world-generator.read.";

// A near radius is capped at half this world's globe radius -- the same relative ceiling
// generate_patch puts on a patch span. Relative rather than absolute because an absolute
// metre bound means a different question on a 200 km world and a 600 km one.
inline constexpr double kNearRadiusFraction = 0.5;

// Row ceilings. A wide radius on a dense world is not bounded by construction, so the bound
// is declared, refused past, and tested.
inline constexpr std::size_t kMaxNearRows = 512;
inline constexpr std::size_t kMaxQuestRows = 2048;
inline constexpr std::size_t kMaxPlacePosts = 1024;

// The reference's READS table, by the names its routes carry: blocks, near, place, person,
// quests. `patch` is not among them; see read_patch.
std::span<const std::string_view> read_names();

// ---------------------------------------------------------------------------
// What every read carries: what it is, and which world answered. Not a handle -- naming a
// world by its content is the host's job -- but enough for a caller holding two answers to
// tell whether they came from the same world at the same moment.
// ---------------------------------------------------------------------------

struct ReadSource {
    int generator_version = 0;
    std::uint32_t seed = 0;
    int size = 0;
    int phase = 0;
    // The world clock's; empty on a world that has never been advanced.
    std::optional<double> day;
    std::optional<std::int64_t> age;
};

struct ReadEnvelope {
    std::string schema;  // kReadSchemaPrefix + read
    int schema_version = kReadSchemaVersion;
    std::string_view read;
    int api_version = kReadApiVersion;
    ReadSource source;
};

// ---------------------------------------------------------------------------
// blocks: which blocks this world carries, each one's declared version and its size.
//
// The read that makes every other read discoverable, and the only one with no join. It walks
// the world document and marks membership of the reference's registry in both directions
// (read_select.hpp, block_rows, for what the byte count means here). Cost is one
// serialisation of the world, because a byte count that was estimated would not be one.
// ---------------------------------------------------------------------------

struct BlocksReadRequest {
    int api_version = kReadApiVersion;
};

struct BlocksRead {
    ReadEnvelope envelope;
    BlockSummary summary;
    std::string_view encoding;
};

// Throws WorldTimeError: api_version.
BlocksRead read_blocks(const World& world, const BlocksReadRequest& request = {});

// ---------------------------------------------------------------------------
// near: what is at, or within a radius of, one terrain node.
// ---------------------------------------------------------------------------

struct NearReadRequest {
    int api_version = kReadApiVersion;
    // REQUIRED, and its absence is refused rather than defaulted to node 0: a read about the
    // north pole that nobody asked for is a wrong answer that looks like a right one.
    std::optional<int> node;
    double radius_m = 0.0;  // metres; 0 asks what stands on the node itself
    // Absent: every category. Present: only the categories named, by near_categories()'s
    // names; an empty list selects nothing -- a filter selects, it never invents. The row
    // ceiling counts the selected rows only, so narrowing the categories is a remedy for a
    // dense neighbourhood, as a smaller radius is. Measured on 2026-09-24 on the seed-42
    // size-33 worlds after a 30-day tick: at the radius ceiling most cities' reads pass
    // kMaxNearRows, nearly all of it encounter rows, so without this a caller could not ask
    // which settlements stand within reach. THIS PORT'S ADDITION: the reference's near read
    // takes no filter.
    std::optional<std::vector<std::string>> categories;
};

// The node read: its cell, and every layer's value there.
struct NearCell {
    NodeCell cell;
    std::vector<std::pair<std::string, double>> layers;
};

struct NearRead {
    ReadEnvelope envelope;
    int node = 0;
    double radius_m = 0.0;
    double radius_ceiling_m = 0.0;
    std::size_t row_ceiling = kMaxNearRows;
    std::size_t rows = 0;  // the selected rows
    NearCell cell;
    NearRows near;  // an unselected category is empty
    std::vector<ReadJoin> joins;
    std::optional<std::vector<std::string>> categories;  // the filter, echoed
};

// Throws WorldTimeError, in this order: api_version; `node` absent; `radius_m` not finite;
// `radius_m` below zero (out of range, suggesting zero); a `categories` name outside
// near_categories() (an invalid choice); `world.config` when the world has no grid (size
// below 2) or `world.effective_config` when it has no positive globe radius; then, as
// refusals by the world, `node` off this world's grid and `radius_m` above half its globe
// radius; then `near.rows` (STATE_CAPACITY) when more than kMaxNearRows selected rows answer.
NearRead read_near(const World& world, const NearReadRequest& request);

// ---------------------------------------------------------------------------
// place: one settlement, hamlet, fortress or key location by id, with its joins resolved.
// ---------------------------------------------------------------------------

struct PlaceReadRequest {
    int api_version = kReadApiVersion;
    std::string place_id;  // a settlement uid, a hamlet or fortress id, or a key location id
};

struct PlaceRead {
    ReadEnvelope envelope;
    PlaceDocument place;
    std::size_t post_ceiling = kMaxPlacePosts;
};

// Throws WorldTimeError: api_version; `place_id` empty; `world.settlements` when the world
// has none; an unknown id (field = that id, candidates = the nearest place ids).
//
// Reference defect REPRODUCED: the missing-settlements refusal tells the caller to
// "Generate it through founding (phase 9)", and founding is stage 10 -- in the reference's
// own ladder as in this one. A phase-9 world has no settlements. Pinned by a test.
PlaceRead read_place(const World& world, const PlaceReadRequest& request);

// ---------------------------------------------------------------------------
// person: one hero, dread, NPC or villain by uid, with liveness resolved through the one
// predicate. A caller never has to know which status vocabulary its record uses.
// ---------------------------------------------------------------------------

struct PersonReadRequest {
    int api_version = kReadApiVersion;
    std::string uid;
};

struct PersonRead {
    ReadEnvelope envelope;
    PersonDocument person;
};

// Throws WorldTimeError: api_version; `uid` empty; `world.heroes` when the world carries no
// heroes, npcs or villains block at all; an unknown uid (field = that uid). A FAILED heroes
// or npcs block is still a block: its people are simply not there, and asking for one is an
// unknown uid, not a missing block.
PersonRead read_person(const World& world, const PersonReadRequest& request);

// ---------------------------------------------------------------------------
// quests: the board, with giver and anchor resolved.
// ---------------------------------------------------------------------------

struct QuestsReadRequest {
    int api_version = kReadApiVersion;
    // Absent: every state. Present and empty: nothing -- a filter selects, it never invents.
    std::optional<std::vector<QuestState>> states;
};

struct QuestsRead {
    ReadEnvelope envelope;
    std::vector<QuestRow> quests;
    QuestCounts counts;                             // over the whole board, not the filter
    std::optional<std::vector<QuestState>> states;  // the filter, echoed
    std::size_t row_ceiling = kMaxQuestRows;
    std::vector<ReadJoin> joins;
};

// Throws WorldTimeError: api_version; a `states` value outside the vocabulary (an invalid
// choice -- reachable only through a cast, since the filter is typed); `world.quests` on a
// world never advanced, which is refused rather than answered with an empty list because no
// quests and no quest lifecycle are different facts; `quests.rows` (STATE_CAPACITY) past
// kMaxQuestRows.
QuestsRead read_quests(const World& world, const QuestsReadRequest& request = {});

// ---------------------------------------------------------------------------
// patch: a local patch of ground in metres, cut from the world this read is handed.
//
// NOT ONE OF THE REFERENCE'S READS. The reference's read module has the five above and names
// the patch only to forbid copying it: its patch route regenerates the whole world from a
// config first. This port adds the read form over the world the caller already holds --
// generate_patch, unchanged, behind the read envelope -- so a caller can ask for ground
// without paying for a generate. Everything about the patch itself (what surface it samples,
// its bounds, its reference defects) is patch.hpp's.
// ---------------------------------------------------------------------------

struct PatchReadRequest {
    int api_version = kReadApiVersion;
    PatchConfig patch;
};

struct PatchRead {
    ReadEnvelope envelope;
    Patch patch;
};

// Throws WorldTimeError for api_version, then whatever generate_patch throws: PatchError for
// every patch config bound, for a world with no height layer, and -- as a refusal by the
// world -- for a span over half this globe's radius or a detail height over a hundredth.
PatchRead read_patch(const World& world, const PatchReadRequest& request);

}  // namespace fwg
