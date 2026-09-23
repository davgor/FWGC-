// GENERATED FILE -- DO NOT EDIT.
//
// Produced by tools/embed_content.py from data/magic-schools.json.
// Edit the JSON and regenerate; `python tools/embed_content.py --check` fails if this
// file is stale.

#include "fwg/magic.hpp"

namespace fwg {
namespace {

// ORDER IS A CONTRACT. dominant_magic exports an index into this table.
constexpr MagicSchool kSchools[] = {
    {"weave", "Raw magic", "chaos, creation, creativity", {166, 104, 214}, false},
    {"umbral", "Raw magic", "necrotic, entropy, death, order", {72, 49, 35}, false},
    {"infernal", "Holy / unholy", "demons, hellscapes, corruption, evil", {154, 43, 49}, false},
    {"radiant", "Holy / unholy", "holy, healing, order, peace, righteousness", {225, 209, 133}, false},
    {"fire", "Primordial", "fire, heat, renewal, chaos", {207, 86, 37}, false},
    {"water", "Primordial", "water, ice, currents, order", {65, 156, 202}, false},
    {"earth", "Primordial", "nature, ground, stone, roots, order", {67, 120, 51}, false},
    {"air", "Primordial", "wind, storms, chaos, force", {176, 210, 213}, false},
    {"blood", "Outside", "sacrifice, lineage, vitality, hunger", {140, 26, 38}, true},
    {"void", "Outside", "absence, entropy, unmaking, silence", {26, 22, 40}, true},
    {"rot", "Outside", "disease, undeath, decay, rebirth", {124, 130, 62}, true},
    {"eldritch", "Outside", "madness, wrong geometry, dominion of minds", {62, 96, 92}, true},
};

}  // namespace

std::span<const MagicSchool> magic_school_table() { return kSchools; }

int magic_school_index(std::string_view name) {
    for (std::size_t i = 0; i < std::size(kSchools); ++i) {
        if (kSchools[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace fwg
