#include "fwg/entities.hpp"

namespace fwg {

const EntityRules* entity_rule(std::string_view id) {
    for (const EntityRules& e : entity_rules()) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

std::span<const HabitatNode> settlement_habitat(const EntityRules& rules, int world_recipe) {
    // One or the other, never both. A surface world and a full world are different
    // places; merging the two rules would admit ground that neither authored.
    return world_recipe != 0 ? rules.settlement.world_habitat : rules.settlement.surface_habitat;
}

bool settlement_admits(const EntityRules& rules, int world_recipe, const HabitatSample& sample) {
    const auto nodes = settlement_habitat(rules, world_recipe);
    if (nodes.empty()) return true;  // a people with no rule is not fussy
    // Child indices are relative to this tree's own first node, so the root is index 0.
    return habitat_admits(nodes, entity_habitat_pools(), 0, sample);
}

}  // namespace fwg
