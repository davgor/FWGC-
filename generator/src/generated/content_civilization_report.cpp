// GENERATED FILE -- DO NOT EDIT.
//
// Produced by tools/embed_content.py from data/registry-sections.json and data/entity-rules.json.
// Edit the JSON and regenerate; `python tools/embed_content.py --check` fails if this
// file is stale.

#include "fwg/civilization_report.hpp"

namespace fwg {
namespace {

// The registry's `defaults` keys ending in _color_rgb, in authored order.
constexpr PresentationDefault kPresentationDefaults[] = {
    {"outpost_wall_color_rgb", {232, 199, 125}},
    {"outpost_roof_color_rgb", {133, 112, 72}},
};

struct OutpostColors {
    std::string_view entity_id;
    bool outpost_colors;
};

// In registry order, one row per entity.
constexpr OutpostColors kOutpostColors[] = {
    {"human_maritime", true},
    {"human_desert", true},
    {"human_cold", true},
    {"human_large_island", true},
    {"human_rainforest", true},
    {"human_heartland", true},
    {"dwarf", false},
    {"elf", false},
    {"gnome", true},
    {"tidekin", true},
    {"hill_dwarf", true},
    {"frosthold_dwarf", false},
};

}  // namespace

std::string_view capital_scope() { return "Active surface cities, one per civilization; sky settlements are separate communities."; }

std::span<const PresentationDefault> presentation_defaults() { return kPresentationDefaults; }

std::optional<bool> entity_outpost_colors(std::string_view entity_id) {
    for (const OutpostColors& row : kOutpostColors) {
        if (row.entity_id == entity_id) return row.outpost_colors;
    }
    return std::nullopt;
}

}  // namespace fwg
