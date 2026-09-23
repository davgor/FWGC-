// Raise a band on a world that already exists: the reference's `nomad_request`
// (terrain_nomad_api, API version 1).
//
// Same request in, same world out; the caller's world is untouched; persist the returned
// world to call again. This is the seam that lets a villain put cultists on the map without
// either feature importing the other: the villain pass calls in with its own uid as the
// origin and gets back a world with a band in it. Nothing here knows what a villain is beyond
// an id.
//
// NO CAP, deliberately. A summoning cap is right for a god walking the mortal plane and
// wrong for a spawner: bands are raised across ages as a matter of course, and a cap would
// silently stop working partway through a long history.
//
// The band is classified by the ground it is placed on, exactly as a world-generated one
// is: every gate is placement's own (nomads.hpp). A caller can ASK for a classification but
// only gets one whose gate the ground passes, so a request for cultists on ground with
// nothing sacred is refused rather than quietly honoured.
//
// What it re-runs, and what that costs a caller holding state. The band is appended and the
// block re-sorted; then the circuits, the encounter index and the write-backs are rebuilt,
// because all three are functions of the band set. That is the reference's behaviour and it
// has two consequences, both reproduced and pinned:
//
//   * every cult's ley node deepens again by the devotion gain on every call, so a node a
//     cult holds ratchets toward the ceiling across calls rather than settling;
//   * raid pressure is added onto the regional threat the previous pass already raised, so
//     a town beside raiders reads a higher threat after every call, raised by the same bands.
//
// Placement is NOT re-run: every placed band keeps its uid, its node and its classification.
// Daughter clans are re-derived by the route pass, as on every run of it.
//
// How this differs from the reference:
//
//   * Ground that raises nobody, and a requested classification the ground does not
//     support, are REFUSALS naming their field ("node", "classification"), with the
//     classifications the node does support offered as the choice. The reference raised a
//     bare ValueError for both, outside its own refusal envelope, so a caller had nothing to
//     branch on.
//   * The reference's other refusals -- a body that is not an object, an unknown request or
//     origin key, a node or variation that is not an integer -- are about parsing JSON, and
//     typed arguments cannot express them. A missing node, origin or origin age is still
//     expressible, and is refused as the reference refuses it.
//   * The ground is judged on the caller's world before it is copied, where the reference
//     copies first. A copy has no effect a refusal could show; a refused call is just cheap.
//
// Reference defects reproduced and pinned, besides the two above: an unknown classification
// is refused through the catalogue refusal, which names the refused VALUE as its field
// ("pirates"); and a missing origin reads "origin must be a finite number; received None.",
// the reference's wording for anything that is not an integer or text.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "fwg/nomads.hpp"

namespace fwg {

struct World;

inline constexpr int kNomadApiVersion = 1;

// Who asked for the band. `kind` is world | god | villain; `id` is the asker's own id (a
// villain's uid), absent for the world; `age` is the age the band is raised in, and its uid
// is keyed on it.
struct NomadBandOrigin {
    std::string kind;
    std::optional<std::string> id;
    std::optional<int> age;  // required
};

struct RaiseBandRequest {
    int api_version = kNomadApiVersion;
    std::optional<int> node;               // required; a land node
    std::optional<NomadBandOrigin> origin;  // required
    // One of the six; absent lets the ground choose by the same weighted draw placement makes.
    std::optional<std::string> classification;
    // 0..4294967295. The draw's own stream is keyed on the node and this, never on the call.
    std::int64_t variation = 0;
};

// One entry for the world's operation history, the reference's record key for key:
// {kind: "nomad_request", api_version, uid, classification, node, origin: {kind, id, age},
// requested_classification, variation}. `origin.id` empty and `requested_classification`
// absent are the reference's None.
struct NomadOperation {
    std::string kind = "nomad_request";
    int api_version = kNomadApiVersion;
    std::string uid;
    NomadClass classification = NomadClass::Survivors;
    int node = 0;
    NomadOrigin origin;
    std::optional<NomadClass> requested_classification;
    std::int64_t variation = 0;
};

// Raise one band at `request.node` and return the world with it. Throws RequestError
// (request_error.hpp) naming the field that refused. The world must carry a band block,
// which exists from stage 16.
World raise_band(const World& world, const RaiseBandRequest& request);

// The survivors of a destroyed city, raised as one band at `node` for a caller that records
// the event itself (a cleanse): classified survivors, SIZED to the people who got out rather
// than drawn from the class's range, a world band whose origin id is the ruin, and logged as
// no operation of its own -- the caller's record names the band. Where the survivors' own gate
// does not hold at the ruin -- no refuge within their reach, as when a cleanse took every town
// around it -- the band is raised anyway: the event is the evidence, and the band's basis names
// the ruin and no refuge. Refused only as raise_band refuses ground: no band block, a node off
// the grid, or water. The band joins `world` in place and its uid is returned; a refusal leaves
// `world` as it was.
struct RefugeeBandRequest {
    int node = 0;
    int people = 0;  // at least 1
    int age = 0;     // the age the band is raised in; its uid is keyed on it
    std::string ruin_id;   // the band's origin id
    std::string ruin_uid;  // the fallen city's uid, as a survivors band's basis names it
};
std::string raise_refugees(World& world, const RefugeeBandRequest& request);

}  // namespace fwg
