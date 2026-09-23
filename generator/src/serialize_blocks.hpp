// The world-document writers for the blocks stages 12, 13 and 16 add.
//
// Private to the library: serialize.cpp is the only caller. Split out because the late
// blocks are most of the document's vocabulary and would bury the early stages' writers.
//
// FIELD NAMES ARE THE REFERENCE'S. Every key below is the key the Python generator's own
// writer uses for that block; the values come from the C++ structs. The key tree is pinned
// against a generated reference world by tests/test_document_paths.cpp. Where a C++ header
// documents a deliberate difference -- an extra stable key, stats filled on every path --
// the struct is followed and the difference is named at the site.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

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

// Inside one `settlements.sites[]` object: its `city_layout` record and the building pack it
// drew (`building_pack_id`, `building_pack_seed`, `building_pack_version`), each where it has one.
void write_site_layout_fields(JsonWriter& json, const World& world, std::size_t site_index);

// A plan's file in a split export (serialize.hpp, write_world_split): the key its block joins
// on, and its path relative to the export's directory.
struct SplitFile {
    std::string key;
    std::string path;
};

// What a split export's world.json writes in place of each plan block's rows.
struct SplitIndex {
    std::vector<SplitFile> cities;
    std::vector<SplitFile> hamlets;
    std::vector<SplitFile> castles;
    std::vector<SplitFile> key_locations;
};

// Every top-level block from stage 12 on, in stage order. Each is written only when the
// World says it exists.
// With `split`, each plan block lists its rows' files instead of carrying the rows.
void write_late_blocks(JsonWriter& json, const World& world, const SplitIndex* split = nullptr);

// The nest blocks' catalogue copies, re-emitted from the authored JSON: one pass's profile
// rows, and wildlife's feeding-role table. Values only; the caller writes the key.
void write_nest_profiles(JsonWriter& json, CreatureClass which);
void write_nest_roles(JsonWriter& json);

// For debug_stats, which republishes a city plan's stats (as `stats`), its debug record (as
// `placement`) and its unplaced rows, and the ruins -- each written here exactly as the
// city_plans and ruins blocks write it. Values only; the caller writes the key.
void write_city_plan_stats(JsonWriter& json, const PlanStats& stats);
void write_city_plan_debug(JsonWriter& json, const PlanDebug& debug);
void write_plan_unplaced(JsonWriter& json, const std::vector<Unplaced>& rows);
void write_ruin_list(JsonWriter& json, const std::vector<Ruin>& ruins);

// One plan row, written exactly as its block writes it. The world supplies what a plan's
// terrain carries besides the plan -- the detail definition, the biome catalogues and each
// ley network's colour -- so a row's bytes are its bytes in the full document. For the place
// read, which measures one row without writing the whole world.
// Without `catalogues`, a row leaves out the copies of the world's catalogues its terrain
// otherwise carries (biome_catalogue, magical_catalogue, magic_colors, and the surface's
// terrain_detail): a split export writes each of those once, in world.json.
void write_city_plan_row(JsonWriter& json, const CityPlan& plan, const World& world, bool catalogues = true);
void write_hamlet_plan_row(JsonWriter& json, const HamletPlan& plan, const World& world, bool catalogues = true);
void write_castle_plan_row(JsonWriter& json, const CastlePlan& plan, const World& world, bool catalogues = true);
void write_key_location_plan_row(JsonWriter& json, const KeyLocationSitePlan& plan);

}  // namespace fwg::serialize_detail
