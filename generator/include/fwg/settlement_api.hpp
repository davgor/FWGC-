// Found a settlement the player chose, in a world that has to keep working around it: the
// reference's `found_settlement_request` (terrain_settlement_api, API version 2).
//
// The player acts in the game; an orchestrator interprets that action and calls this. The
// player owns the layout -- where the buildings go is their design -- and the world owns the
// metadata, which has to be indistinguishable in kind from a generated settlement's or every
// block that reads `settlements` starts reading a site that is not quite a site. So the site
// record is built from the layers that already describe the ground, the uid is minted by the
// generated rule verbatim, and the class is derived, never supplied.
//
// Stateless: a world goes in, a new world comes out, the caller's world is never touched,
// and the world's operation history records the call.
//
// THREE KINDS, TWO ID SPACES. `kind` is city, hamlet or fortress; there is no "village",
// and asking for one is refused by name rather than quietly given a city. A city takes the
// generated uid rule, "surface-city-<age>-<node>-<people>", which survives an age boundary.
// A hamlet or a fortress takes the ordinal id its block has always used ("hamlet-12"), which
// the plan blocks join on and which RENUMBERS whenever the rural layer is rebuilt, and beside
// it the node-and-kind uid ("hamlet-node-412") that does not. Hold the uid.
//
// WHAT IT REFUSES TO DO, each for a reason:
//
//   * Set the class. `city_class` is a per-people suitability RANK, not a size, so it is
//     recomputed over the whole list after insertion, and founding a strong site can demote
//     an incumbent. The report names every city whose class moved.
//   * Re-run the nest pass. The nests draw from one sequential stream whose per-cell draw
//     count depends on distance to settled ground, so one new town re-rolls nests on the far
//     side of the planet. The next age advance re-rolls them anyway; the report says so.
//   * Pretend the rural layer is untouched. With rebuild "humans" the hinterland is
//     re-derived from scratch and its ordinals trade places; the report lists the ids that
//     now name different ground. The default, "none", leaves every rural join intact and
//     gives the new settlement no hinterland until something rebuilds it.
//
// HOW THIS DIFFERS FROM THE REFERENCE, deliberately:
//
//   * STAGE 16 IS REDRAWN. On a world that carries stage 16, the founding re-derives it
//     exactly as an age advance does -- every plan and the world scene, the bands, herds and
//     encounters, the cast and the story web, the rosters, the key locations -- so the new
//     settlement has a plan and every block that reads the settlement list has read it. The
//     reference redraws none of it: its new city had no plan until the next advance, and its
//     `rebuild: "humans"` dropped the hamlet and castle plans (reported in `dropped_blocks`)
//     rather than re-deriving them. Here nothing is dropped, so `dropped_blocks` is always
//     empty and a note says what was redrawn. The price is the reference's own objection to
//     the nest pass, in a smaller dose: band placement is one sequential stream, so a band
//     far from the new settlement can re-roll, and band, roster and key-location ids held
//     from before the call may not resolve after it. Measured at seed 42, size 33, founding
//     at four different nodes: 0 to 8 of 15 bands changed, 0 to 5 of 158 key locations,
//     every hero kept its uid, and 1 of 29 city plans changed each time.
//   * The threat assessment is re-read before the redraw, because stage 16's planners read
//     the threat before the raiders raise it and the raid pass adds onto whatever threat it
//     finds -- redrawn over the last pass's raised threat, a town would be raided twice. The
//     re-read also sees what the generated world's own stage 16 did not: the final age
//     builds the villains' works AFTER its threat assessment, and a well moves the ley
//     field. A redraw with nothing founded at all changes one city plan too, the size of
//     the plan change measured above: 8 villain seats read a higher ley pressure, and the
//     shape counter carries a changed shape on to a later city.
//   * The player city's `parent_race_id` is its people's parent race. The reference wrote
//     the CIVILIZATION id there, which for most peoples names no parent race. The reference
//     never read it before its next rebuild recomputed it; the stage-16 redraw here resolves
//     a heritage from it at once, the heritage refuses an unknown parent race, and the hero
//     pass -- which never throws -- fails whole: measured on a human_maritime city, 156
//     heroes to none, status "failed". Fixed rather than reproduced, because reproduced it
//     would cost every founding its cast.
//   * The message that says which stage places a block names this ladder's stage (10 for
//     cities, 12 for the rural layer); the reference's said 9 for both.
//   * The reference's refusals of a malformed JSON body -- not an object, an unknown key,
//     a node that is not an integer -- cannot be expressed by typed arguments. A caller
//     cannot set `city_class` because the request has no such member. A missing node or
//     civilization is still expressible, and is refused as the reference refuses it.
//
// REFERENCE DEFECTS REPRODUCED, each pinned by a test:
//
//   * An unknown civilization is refused through the catalogue refusal, which names the
//     refused VALUE as its field: civilization_id "dwarfs" is refused with field "dwarfs".
//   * A rural row's `access_cost` is great-circle METRES to its nearest city, where a
//     generated row's is a road cost; no path search stands behind a player's site.
//   * `settlements.founded_by_player` is dropped by the next settlement rebuild although the
//     player's cities stand (see SettlementReport).
//   * A player city on ground with no reachable fresh water reads the layer's -1 as its
//     freshwater distance, where a generated city reads none.
//   * `rebuild: "humans"` re-runs the hinterland pass alone (populated_stage.hpp,
//     rebuild_hinterland): the harbours drop out of the rural layer, and colleges, seasonal
//     food and society go on describing the hinterland as it was. The harbour drop is
//     pinned; with the redraw it also means the harbours lose their plans until the next age.
//   * The refusal wording, "None" included: "node must be 0..None grid index; received -1."
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "fwg/settlement.hpp"

namespace fwg {

struct World;

inline constexpr int kSettlementApiVersion = 2;
inline constexpr int kSettlementFoundingReportVersion = 2;

struct FoundSettlementRequest {
    int api_version = kSettlementApiVersion;
    std::optional<int> node;  // required: the terrain node to found on
    std::string kind = "city";      // city | hamlet | fortress
    std::string role = "farming";   // farming | resource; a hamlet's work, checked for every kind
    std::optional<std::string> civilization_id;  // required: whose settlement
    std::optional<std::string> name;             // absent: "New Settlement" for a city, none for a rural row
    // player: the player owns where the buildings go, and the city carries an authored layout
    // record the generator never re-derives. generated: no record; the world decides.
    std::string layout = "player";
    std::optional<long long> population_estimate;  // a city's residents; absent is 0
    // none: leave the rural layer and every join into it alone. humans: re-derive the
    // hinterland around the new settlement, renumbering the rural ordinals.
    std::string rebuild = "none";
};

// What the founding did, and what it did to everything else. The reference's report key for
// key; `city_class` absent is its None.
struct SettlementFoundingReport {
    struct RuralIdsReassigned {
        std::vector<std::string> hamlets;     // ordinal ids whose ground changed, sorted
        std::vector<std::string> fortresses;
    };
    int version = kSettlementFoundingReportVersion;
    int node = 0;
    std::string uid;
    std::string kind;
    std::optional<CityClass> city_class;           // the new city's; absent for a rural row
    std::vector<std::string> reclassified_cities;  // existing cities whose class moved, sorted
    std::string layout;
    std::string rebuild;
    bool nests_rerolled = false;  // never: see the header
    std::vector<std::string> notes;
    RuralIdsReassigned rural_ids_reassigned;
    std::vector<std::string> dropped_blocks;  // always empty here: see the header
};

// One entry for the world's operation history, the reference's record key for key:
// {api_version, kind: "founding", settlement_kind, node, civilization_id, uid, layout,
// rebuild, name}. `kind` is the OPERATION's kind, which every request writes; the
// settlement's kind is a second axis under a second name. `name` absent is the reference's
// None (a rural row founded without one).
struct SettlementOperation {
    int api_version = kSettlementApiVersion;
    std::string kind = "founding";
    std::string settlement_kind;
    int node = 0;
    std::string civilization_id;
    std::string uid;
    std::string layout;
    std::string rebuild;
    std::optional<std::string> name;
};

// Found one settlement and return the world with it. Throws RequestError (request_error.hpp)
// naming the field that refused: a malformed request names the argument, and ground the
// world will not settle is `refused_by_world` naming the node, because the remedy there is
// a different node rather than a different value. Needs a world with settlements (stage 10);
// a hamlet or a fortress needs the rural layer too (stage 12).
World found_settlement(const World& world, const FoundSettlementRequest& request);

}  // namespace fwg
