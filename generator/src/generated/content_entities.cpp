// GENERATED FILE -- DO NOT EDIT.
//
// Produced by tools/embed_content.py from data/entity-rules.json.
// Edit the JSON and regenerate; `python tools/embed_content.py --check` fails if this
// file is stale.

#include "fwg/entities.hpp"

namespace fwg {
namespace {

// The entities' habitat trees have their OWN pools. They are a different set of
// trees from the civilization profiles', generated into a different translation
// unit, and a tree that read the other's pools would silently test another
// people's rule.
constexpr std::uint32_t kChildren[] = {7, 8, 3, 4, 5, 6, 1, 2, 7, 8, 3, 4, 5, 6, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 8, 9, 11, 12, 5, 6, 7, 10, 1, 2, 3, 4, 8, 9, 11, 12, 5, 6, 7, 10, 1, 2, 3, 4};
constexpr double kNumbers[] = {5.0, 5.0, 4.0, 7.0, 15.0, 4.0, 7.0, 15.0, 3.0, 3.0, 5.0, 5.0};
constexpr std::string_view kTexts[] = {"desert.weave", "desert.infernal", "desert.water", "desert.weave", "desert.infernal", "desert.water"};

constexpr HabitatNode kNodes[] = {
    {HabitatOp::all, HabitatField::none, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 0u, 0u, 0u, 0u, 6u, 8u},
    {HabitatOp::leaf, HabitatField::resource, 0.5, 0.0, 0.0, 0.0, true, false, false, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::any, HabitatField::none, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 0u, 0u, 0u, 0u, 2u, 6u},
    {HabitatOp::leaf, HabitatField::biome, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 0u, 1u, 0u, 0u, 0u, 0u},
    {HabitatOp::leaf, HabitatField::variant, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 0u, 0u, 0u, 3u, 0u, 0u},
    {HabitatOp::leaf, HabitatField::tpi, 0.0, 0.0, 6.0, 0.0, false, false, true, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::all, HabitatField::none, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 0u, 0u, 0u, 0u, 0u, 2u},
    {HabitatOp::leaf, HabitatField::slope, 0.0, 0.0, 10.0, 0.0, false, false, true, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::leaf, HabitatField::height, 0.0, 0.0, 15.0, 0.0, false, false, true, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::all, HabitatField::none, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 0u, 0u, 0u, 0u, 14u, 16u},
    {HabitatOp::leaf, HabitatField::resource, 0.5, 0.0, 0.0, 0.0, true, false, false, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::any, HabitatField::none, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 0u, 0u, 0u, 0u, 10u, 14u},
    {HabitatOp::leaf, HabitatField::biome, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 1u, 2u, 0u, 0u, 0u, 0u},
    {HabitatOp::leaf, HabitatField::variant, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 0u, 0u, 3u, 6u, 0u, 0u},
    {HabitatOp::leaf, HabitatField::tpi, 0.0, 0.0, 6.0, 0.0, false, false, true, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::all, HabitatField::none, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 0u, 0u, 0u, 0u, 8u, 10u},
    {HabitatOp::leaf, HabitatField::slope, 0.0, 0.0, 10.0, 0.0, false, false, true, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::leaf, HabitatField::height, 0.0, 0.0, 15.0, 0.0, false, false, true, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::all, HabitatField::none, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 0u, 0u, 0u, 0u, 16u, 18u},
    {HabitatOp::leaf, HabitatField::biome, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 2u, 5u, 0u, 0u, 0u, 0u},
    {HabitatOp::leaf, HabitatField::moisture, 0.55, 0.0, 0.0, 0.0, true, false, false, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::all, HabitatField::none, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 0u, 0u, 0u, 0u, 18u, 20u},
    {HabitatOp::leaf, HabitatField::biome, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 5u, 8u, 0u, 0u, 0u, 0u},
    {HabitatOp::leaf, HabitatField::moisture, 0.4, 0.0, 0.0, 0.0, true, false, false, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::leaf, HabitatField::resource, 0.0, 0.0, 0.45, 0.0, false, false, true, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::all, HabitatField::none, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 0u, 0u, 0u, 0u, 20u, 22u},
    {HabitatOp::leaf, HabitatField::maritime, 0.0, 0.0, 0.3, 0.0, false, false, true, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::leaf, HabitatField::coastal_support, 0.0, 0.0, 0.1, 0.0, false, false, true, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::all, HabitatField::none, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 0u, 0u, 0u, 0u, 22u, 24u},
    {HabitatOp::leaf, HabitatField::biome, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 8u, 9u, 0u, 0u, 0u, 0u},
    {HabitatOp::leaf, HabitatField::slope, 0.0, 18.0, 0.0, 0.0, false, true, false, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::all, HabitatField::none, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 0u, 0u, 0u, 0u, 24u, 26u},
    {HabitatOp::leaf, HabitatField::biome, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 9u, 10u, 0u, 0u, 0u, 0u},
    {HabitatOp::leaf, HabitatField::slope, 0.0, 18.0, 0.0, 0.0, false, true, false, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::all, HabitatField::none, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 0u, 0u, 0u, 0u, 34u, 38u},
    {HabitatOp::leaf, HabitatField::abs_latitude, 50.0, 0.0, 0.0, 0.0, true, false, false, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::leaf, HabitatField::temperature, 0.0, 5.0, 0.0, 0.0, false, true, false, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::leaf, HabitatField::resource, 0.45, 0.0, 0.0, 0.0, true, false, false, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::any, HabitatField::none, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 0u, 0u, 0u, 0u, 30u, 34u},
    {HabitatOp::leaf, HabitatField::biome, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 10u, 11u, 0u, 0u, 0u, 0u},
    {HabitatOp::leaf, HabitatField::tpi, 0.0, 0.0, 6.0, 0.0, false, false, true, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::all, HabitatField::none, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 0u, 0u, 0u, 0u, 26u, 28u},
    {HabitatOp::leaf, HabitatField::slope, 0.0, 0.0, 10.0, 0.0, false, false, true, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::leaf, HabitatField::height, 0.0, 0.0, 15.0, 0.0, false, false, true, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::all, HabitatField::none, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 0u, 0u, 0u, 0u, 28u, 30u},
    {HabitatOp::leaf, HabitatField::maritime, 0.0, 0.0, 0.3, 0.0, false, false, true, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::leaf, HabitatField::slope, 8.0, 0.0, 0.0, 0.0, true, false, false, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::all, HabitatField::none, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 0u, 0u, 0u, 0u, 46u, 50u},
    {HabitatOp::leaf, HabitatField::abs_latitude, 50.0, 0.0, 0.0, 0.0, true, false, false, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::leaf, HabitatField::temperature, 0.0, 5.0, 0.0, 0.0, false, true, false, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::leaf, HabitatField::resource, 0.45, 0.0, 0.0, 0.0, true, false, false, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::any, HabitatField::none, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 0u, 0u, 0u, 0u, 42u, 46u},
    {HabitatOp::leaf, HabitatField::biome, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 11u, 12u, 0u, 0u, 0u, 0u},
    {HabitatOp::leaf, HabitatField::tpi, 0.0, 0.0, 6.0, 0.0, false, false, true, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::all, HabitatField::none, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 0u, 0u, 0u, 0u, 38u, 40u},
    {HabitatOp::leaf, HabitatField::slope, 0.0, 0.0, 10.0, 0.0, false, false, true, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::leaf, HabitatField::height, 0.0, 0.0, 15.0, 0.0, false, false, true, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::all, HabitatField::none, 0.0, 0.0, 0.0, 0.0, false, false, false, false, 0u, 0u, 0u, 0u, 40u, 42u},
    {HabitatOp::leaf, HabitatField::maritime, 0.0, 0.0, 0.3, 0.0, false, false, true, false, 0u, 0u, 0u, 0u, 0u, 0u},
    {HabitatOp::leaf, HabitatField::slope, 8.0, 0.0, 0.0, 0.0, true, false, false, false, 0u, 0u, 0u, 0u, 0u, 0u},
};

constexpr EntityRules kEntities[] = {
    {"human_maritime", "human",
     {{kNodes + 0, kNodes + 0}, {kNodes + 0, kNodes + 0}, std::nullopt, false, 0.15, 0.0, ""},
     {1.0, std::nullopt, std::nullopt, 0.0},
     {0.0, 0.0},
     {{83, 161, 190}, {255, 232, 194}, {186, 102, 83}}},
    {"human_desert", "human",
     {{kNodes + 0, kNodes + 0}, {kNodes + 0, kNodes + 0}, std::nullopt, false, 0.15, 0.0, ""},
     {1.0, std::nullopt, std::nullopt, 0.0},
     {0.0, 0.0},
     {{210, 171, 103}, {255, 232, 194}, {186, 102, 83}}},
    {"human_cold", "human",
     {{kNodes + 0, kNodes + 0}, {kNodes + 0, kNodes + 0}, std::nullopt, false, 0.15, 0.0, ""},
     {1.25, std::nullopt, std::nullopt, 0.25},
     {0.0, 0.0},
     {{149, 172, 202}, {255, 232, 194}, {186, 102, 83}}},
    {"human_large_island", "human",
     {{kNodes + 0, kNodes + 0}, {kNodes + 0, kNodes + 0}, std::nullopt, false, 0.15, 0.0, ""},
     {1.0, std::nullopt, std::nullopt, 0.0},
     {0.0, 0.0},
     {{138, 165, 115}, {255, 232, 194}, {186, 102, 83}}},
    {"human_rainforest", "human",
     {{kNodes + 0, kNodes + 0}, {kNodes + 0, kNodes + 0}, std::nullopt, false, 0.15, 0.0, ""},
     {1.0, std::nullopt, std::nullopt, 0.0},
     {0.0, 0.0},
     {{70, 136, 105}, {255, 232, 194}, {186, 102, 83}}},
    {"human_heartland", "human",
     {{kNodes + 0, kNodes + 0}, {kNodes + 0, kNodes + 0}, std::nullopt, false, 0.15, 0.0, ""},
     {1.0, std::nullopt, std::nullopt, 0.0},
     {0.0, 0.0},
     {{184, 131, 113}, {255, 232, 194}, {186, 102, 83}}},
    {"dwarf", "dwarf",
     {{kNodes + 0, kNodes + 9}, {kNodes + 9, kNodes + 18}, 2.0, true, 0.15, 0.0, "Mineral-rich upland with freshwater access"},
     {1.0, std::nullopt, std::nullopt, 0.0},
     {0.0, 0.0},
     {{142, 142, 154}, {185, 194, 207}, {104, 122, 150}}},
    {"elf", "elf",
     {{kNodes + 18, kNodes + 21}, {kNodes + 21, kNodes + 24}, 2.0, false, 0.15, 0.0, "Moist forest habitat with freshwater access"},
     {1.0, std::nullopt, std::nullopt, 0.0},
     {0.0, 3.0},
     {{123, 185, 143}, {179, 232, 199}, {54, 131, 101}}},
    {"gnome", "dwarf",
     {{kNodes + 24, kNodes + 24}, {kNodes + 24, kNodes + 25}, std::nullopt, false, 0.15, 0.15, ""},
     {1.0, 0.5, 0.5, 0.0},
     {4.0, 0.0},
     {{193, 166, 98}, {255, 232, 194}, {186, 102, 83}}},
    {"tidekin", "elf",
     {{kNodes + 25, kNodes + 25}, {kNodes + 25, kNodes + 28}, std::nullopt, false, 0.35, 0.0, ""},
     {1.5, std::nullopt, std::nullopt, 0.0},
     {-10.0, 0.0},
     {{102, 189, 184}, {255, 232, 194}, {186, 102, 83}}},
    {"hill_dwarf", "dwarf",
     {{kNodes + 28, kNodes + 31}, {kNodes + 31, kNodes + 34}, std::nullopt, false, 0.15, 0.0, "Fertile grassland and gentle slopes suited to a pastoral hill-dwarf community"},
     {1.0, std::nullopt, std::nullopt, 0.0},
     {0.0, 0.0},
     {{185, 174, 95}, {197, 183, 135}, {99, 139, 71}}},
    {"frosthold_dwarf", "dwarf",
     {{kNodes + 34, kNodes + 47}, {kNodes + 47, kNodes + 60}, 2.0, true, 0.25, 0.0, "Cold mineral-rich upland or coastal cliff with reachable freshwater and supply hinterland"},
     {1.25, std::nullopt, std::nullopt, 0.15},
     {0.0, 0.0},
     {{133, 182, 207}, {111, 129, 145}, {60, 92, 114}}},
};

}  // namespace

// ORDER IS A CONTRACT: an entity's position here is published as its
// region_index, and the map is coloured by it.
std::span<const EntityRules> entity_rules() { return kEntities; }

HabitatPools entity_habitat_pools() { return {kChildren, kNumbers, kTexts}; }

}  // namespace fwg
