// The whole published control surface, as the world document's `recipe.parameters` carries it.
//
// Every knob a request may set: the 45 that are Config fields (including the two authored
// inputs, circumference_km and relief_m, that the request builder consumes and that are
// therefore not Config fields at all) and the 164 world options. The table is generated from
// data/config-controls.json and data/world-options.json by tools/embed/controls.py.
//
// DOCUMENTATION, NOT GENERATION INPUT. Nothing here is read to build a world: the options'
// numbers come from option_specs() and the Config fields' defaults from Config itself. The
// descriptions, groups, units and choices exist so a consumer holding a world can rebuild the
// request form without a second call -- which is why they are in the document at all.
//
// The bounds are the reference's published bounds. Config::validate() enforces its own, and
// where the two disagree (plate_count, octaves, the variation seeds, globe_radius) this table
// says what the reference would accept rather than what this build refuses.
#pragma once

#include <span>
#include <string_view>

namespace fwg {

enum class ControlType : int { Number, Integer, String };

struct ControlChoiceLabel {
    std::string_view id;
    std::string_view label;
};

struct ControlSpec {
    std::string_view name;
    ControlType type;
    double default_number;          // Number and Integer
    std::string_view default_text;  // String
    bool has_min;
    double min_value;
    bool has_max;
    double max_value;
    std::string_view group;
    std::string_view description;
    std::string_view units;
    std::span<const std::string_view> choices;          // String controls only
    std::span<const ControlChoiceLabel> choice_labels;  // population_profile only
    bool world_option;  // a world option rather than a Config field or an authored input
};

// Sorted by name. 209 rows.
std::span<const ControlSpec> control_specs();

// By published name, or nullptr.
const ControlSpec* control_spec(std::string_view name);

}  // namespace fwg
