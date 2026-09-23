// What each people is allowed to do with the world.
//
// The civilization registry says what a people IS -- its traits, its opinions of biomes,
// where its population can live. These rules say what its SETTLEMENTS may do: which
// ground a city may stand on, how far it fishes, whether it builds an air terminal, and
// what colour it paints the map.
//
// Kept apart from the profiles because they answer a different question and because the
// two are gated differently: a profile's habitat decides where a people can exist at all,
// while a settlement habitat decides where it will build.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "fwg/civilizations.hpp"

namespace fwg {

// Where a people will build, and how it scores what it finds.
struct SettlementRules {
    // TWO habitats, and the world picks ONE by recipe rather than merging them. A
    // surface world and a full world are different places, and a people that tolerates
    // a coastline on one does not necessarily tolerate it on the other.
    std::span<const HabitatNode> surface_habitat;
    std::span<const HabitatNode> world_habitat;

    // Multiplies the people's own water_reach to decide how far a settlement may stand
    // from fresh water. ABSENT MEANS NO GATE AT ALL -- not zero, which would forbid
    // every cell that is not standing in a river.
    std::optional<double> freshwater_reach_multiplier;

    // Whether the hinterland this city farms may extend past the people's habitat.
    bool support_outside_habitat = false;

    // Extra score for a coast, and for minerals. Both are applied only on the full
    // world recipe. The resource weight is read as a flag as well as a number, so an
    // authored 0.0 contributes nothing rather than a zero term.
    double coastal_score_weight = 0.0;
    double resource_score_weight = 0.0;

    // Why this people settled here, when it has its own answer. Empty falls back to the
    // registry default.
    std::string_view reason;
};

struct EconomyRules {
    double fishing_reach_multiplier = 1.0;
    // ABSENT MEANS NEVER. A resource minimum of zero builds the terminal everywhere;
    // no minimum at all builds it nowhere.
    std::optional<double> industrial_resource_min;
    std::optional<double> air_terminal_resource_min;
    double winter_fishing_fraction = 0.0;
};

// How a people weighs a floating island. The bias is added outright and the moisture
// weight scales the island's wetness; the winner is a max over entities, so the table's
// ORDER breaks a tie.
struct SkyRules {
    double score_bias = 0.0;
    double moisture_score_weight = 0.0;
};

struct Rgb {
    int r = 0, g = 0, b = 0;
};

// Display only. Nothing in generation reads these.
struct PresentationRules {
    Rgb map_color;
    Rgb wall_color;
    Rgb roof_color;
};

struct EntityRules {
    std::string_view id;
    std::string_view parent_race_id;
    SettlementRules settlement;
    EconomyRules economy;
    SkyRules sky;
    PresentationRules presentation;
};

// Every entity, in registry order. ORDER IS A CONTRACT: an entity's position here is
// published as its region_index and the map is coloured by it.
std::span<const EntityRules> entity_rules();

// By id, or nullptr.
const EntityRules* entity_rule(std::string_view id);

// The pools the entity habitat trees index into. A DIFFERENT set from the civilization
// profiles' -- see HabitatPools.
HabitatPools entity_habitat_pools();

// Which of the two habitats a world of this recipe uses. They are not merged.
std::span<const HabitatNode> settlement_habitat(const EntityRules& rules, int world_recipe);

// May this people build here? An empty habitat admits everywhere.
bool settlement_admits(const EntityRules& rules, int world_recipe, const HabitatSample& sample);

}  // namespace fwg
