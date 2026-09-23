// The records the time and quest APIs append to a world's operation history.
//
// Split from world_time.hpp (the API) so world.hpp can hold them without seeing the API's
// result types, which contain a World. The block of World members these fill is in
// world.hpp; the writers are `advance_world_time` and `world_quest_request` in
// world_time.hpp.
//
// **The reference keeps one log, and so does this port.** Every request API appends to
// `history.operations` in call order -- the age API's entries, the time API's, the quest
// route's, the corruption and visitation APIs' -- as alternatives of one variant
// (HistoryOperation, ages.hpp).
//
// Ported from `Sim/icarus_sim/terrain_time.py` (advance_time_request) and
// `terrain_quest_api.py` (quest_request).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/magic.hpp"
#include "fwg/quests.hpp"
#include "fwg/time.hpp"

namespace fwg {

inline constexpr int kQuestApiVersion = 1;

// The time API's entry, in the reference's two shapes. The pure tick's own TimeOperation
// (time.hpp) does not fit: it counts the resolutions rather than carrying them and has no
// place for the ley edits, and a record that cannot replay the call is not the record.
//
//   A tick:     api_version, kind "time", band (below age), elapsed_days, from_day,
//               to_day, steps, leyline_edits (verbatim, possibly empty), resolutions
//               (verbatim, possibly empty).
//   An age span, written after its ages turn and before any remainder ticks: api_version,
//               kind "time", band "age", elapsed_days (the WHOLE ages only, ages * one
//               age's days), ages, from_day, to_day. No steps, no edits, no resolutions:
//               those ride the remainder, which records its own tick entry.
//
// `steps` and `ages` are the discriminators, and they are what the reference's age cost
// estimate reads back (it took the last entry carrying either -- see
// world_age_cost_basis in world_time.hpp for why this port does not).
struct TimeOperationRecord {
    int api_version = kTimeApiVersion;
    TimeBand band = TimeBand::Instant;
    double elapsed_days = 0.0;
    double from_day = 0.0;
    double to_day = 0.0;
    std::optional<std::int64_t> steps;  // a tick's entry only
    std::optional<std::int64_t> ages;   // an age span's entry only
    std::vector<LeylineEdit> leyline_edits;
    std::vector<QuestResolution> resolutions;
};

// One player action as the quest route records it: what was asked and on which day, not
// the state it produced.
struct QuestActionEntry {
    std::string quest_id;
    QuestActionKind action = QuestActionKind::Take;
    double day = 0.0;
};

// The quest route's entry: api_version, kind "quest", actions. Written only by a call that
// applied at least one action.
struct QuestOperationRecord {
    int api_version = kQuestApiVersion;
    std::vector<QuestActionEntry> actions;
};

}  // namespace fwg
