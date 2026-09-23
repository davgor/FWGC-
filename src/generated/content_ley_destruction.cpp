// GENERATED FILE -- DO NOT EDIT.
//
// Produced by tools/embed_content.py from data/ley-destruction.json.
// Edit the JSON and regenerate; `python tools/embed_content.py --check` fails if this
// file is stale.

#include "fwg/fates.hpp"

namespace fwg {
namespace {

struct LeyDestructionRow {
    std::string_view school;
    std::string_view reason;
};

// The eight known schools, in school-table order. No hidden school has a row, and
// that is the contract rather than a gap: see tools/embed/ley_destruction.py.
constexpr LeyDestructionRow kRows[] = {
    {"weave", "An arcane surge tore the city apart."},
    {"umbral", "Necrotic entropy extinguished the city."},
    {"infernal", "Infernal corruption and demonic incursions drove its people away."},
    {"radiant", "An overwhelming radiant surge forced the city to be abandoned."},
    {"fire", "A fire leyline engulfed the city in flames."},
    {"water", "A water leyline drowned or froze the city."},
    {"earth", "An earth leyline let roots and stone reclaim the city."},
    {"air", "An air leyline shattered the city with storms and force."},
};

}  // namespace

std::string_view ley_destruction_reason(std::string_view school) {
    for (const LeyDestructionRow& row : kRows) {
        if (row.school == school) return row.reason;
    }
    return {};
}

}  // namespace fwg
