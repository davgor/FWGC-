// The record the person-state API appends to a world's operation history.
//
// Split from person_state.hpp (the API) for the reason time_state.hpp is split from
// world_time.hpp: world.hpp holds these through ages.hpp's HistoryOperation, and the API's
// result type contains a World.
//
// Ported from `Sim/icarus_sim/terrain_person_api.py`, whose entry is
//
//   {"api_version": 1, "kind": "person",
//    "changes": [{"uid": ..., "status": "present" | "gone", "reason": ..., "day": ...}]}
//
// with `changes` holding the normalised changes that APPLIED, in call order -- a change that
// asked for the state its person was already in is reported but not recorded, and a call
// that applied nothing records nothing at all.
#pragma once

#include <string>
#include <vector>

#include "fwg/liveness.hpp"

namespace fwg {

inline constexpr int kPersonApiVersion = 1;

// One change as the log keeps it: the request's own words, normalised. `status` is the
// liveness pair, never a block's word -- the log replays the request, and the request never
// names a block.
struct PersonChangeEntry {
    std::string uid;
    Liveness status = Liveness::Present;
    std::string reason;
    double day = 0.0;
};

// The person route's entry: api_version, kind "person", changes.
struct PersonOperation {
    int api_version = kPersonApiVersion;
    std::vector<PersonChangeEntry> changes;
};

}  // namespace fwg
