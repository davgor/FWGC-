// The world-document writers for the blocks stages 12, 13 and 16 add.
//
// Private to the library: serialize.cpp is the only caller. Split out because the late
// blocks are most of the document's vocabulary and would bury the early stages' writers.
//
// FIELD NAMES ARE THE REFERENCE'S. Every key below is the key the Python generator's own
// writer uses for that block, checked against a generated reference world rather than
// recalled; the values come from the C++ structs. Where a C++ header documents a
// deliberate difference -- an extra stable key, a dropped per-plan catalogue copy, stats
// filled on every path -- the struct is followed and the difference is named at the site.
#pragma once

#include <cstddef>

#include "fwg/json.hpp"
#include "fwg/world.hpp"

namespace fwg::serialize_detail {

// Inside the `magic` object: the college pass's rows, or the pending row the reference
// writes before that pass has run.
void write_college_fields(JsonWriter& json, const World& world);

// Inside one `roads.routes[]` object: the riders a caravan put on it, when any did.
void write_route_trade_fields(JsonWriter& json, const World& world, std::size_t route_index);

// Inside one `settlements.sites[]` object: what the society pass wrote onto the site.
void write_site_society_fields(JsonWriter& json, const World& world, std::size_t site_index);

// Inside one `settlements.sites[]` object: the wars it fought and the victor it serves.
void write_site_age_fields(JsonWriter& json, const World& world, std::size_t site_index);

// Every top-level block from stage 12 on, in stage order. Each is written only when the
// World says it exists.
void write_late_blocks(JsonWriter& json, const World& world);

// For debug_stats, which republishes a city plan's stats (as `stats`), its debug record (as
// `placement`) and its unplaced rows, and the ruins -- each written here exactly as the
// city_plans and ruins blocks write it. Values only; the caller writes the key.
void write_city_plan_stats(JsonWriter& json, const PlanStats& stats);
void write_city_plan_debug(JsonWriter& json, const PlanDebug& debug);
void write_plan_unplaced(JsonWriter& json, const std::vector<Unplaced>& rows);
void write_ruin_list(JsonWriter& json, const std::vector<Ruin>& ruins);

}  // namespace fwg::serialize_detail
