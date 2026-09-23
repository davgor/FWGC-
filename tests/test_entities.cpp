// Entity settlement rules.
#include "check.hpp"

#include <set>
#include <string>

#include "fwg/entities.hpp"
#include "fwg/founding.hpp"

using namespace fwg;

namespace {

// Ground that passes no rule in particular: every field at a neutral value.
HabitatSample blank() { return HabitatSample{}; }

}  // namespace

TEST(every_entity_is_owned_by_a_parent_race_that_exists) {
    std::set<std::string_view> races;
    for (const ParentRace& r : parent_races()) races.insert(r.id);

    const auto rules = entity_rules();
    check::is_true(!rules.empty(), "at least one entity, or nobody settles anything");
    for (const EntityRules& e : rules) {
        check::is_true(races.count(e.parent_race_id) != 0,
                       std::string(e.id) + " to be owned by a parent race in the registry");
    }
}

TEST(the_entity_order_matches_the_registry_it_publishes_positions_into) {
    // An entity's position here is published as its region_index and the map is
    // coloured by it. If the two lists ever drift, the colours move.
    const auto rules = entity_rules();
    const auto ids = civilization_ids();
    check::equal(rules.size(), ids.size(), "one rule per entity civilization");
    for (std::size_t i = 0; i < rules.size(); ++i) {
        check::equal(std::string(rules[i].id), std::string(ids[i]),
                     "entity " + std::to_string(i) + " to agree with the registry order");
    }
}

TEST(every_entity_rule_can_be_found_by_id) {
    for (const EntityRules& e : entity_rules()) {
        const EntityRules* found = entity_rule(e.id);
        check::is_true(found != nullptr, std::string(e.id) + " to be findable");
        check::equal(std::string(found->id), std::string(e.id), "the right rule to come back");
    }
    check::is_true(entity_rule("wyrmkin") == nullptr, "an unknown id to return nothing");
}

TEST(the_two_habitats_are_carried_separately_and_never_merged) {
    // A surface world and a full world are different places. Merging the two rules
    // would admit ground that neither of them authored.
    bool any_differ = false;
    for (const EntityRules& e : entity_rules()) {
        const auto surface = settlement_habitat(e, 0);
        const auto world = settlement_habitat(e, 3);
        if (surface.size() != world.size() || surface.data() != world.data()) any_differ = true;
    }
    check::is_true(any_differ,
                   "some entity to author different surface and world habitats, or the "
                   "recipe switch would be testing nothing");
}

TEST(an_absent_freshwater_gate_is_not_a_zero_one) {
    // `null` means the entity is not gated on fresh water at all. Zero would forbid
    // every cell that is not standing in a river.
    bool any_absent = false;
    bool any_present = false;
    for (const EntityRules& e : entity_rules()) {
        if (e.settlement.freshwater_reach_multiplier.has_value()) {
            any_present = true;
            check::is_true(*e.settlement.freshwater_reach_multiplier > 0.0,
                           std::string(e.id) + " to author a positive multiplier if it authors one");
        } else {
            any_absent = true;
        }
    }
    check::is_true(any_absent, "some entity to author no freshwater gate");
    check::is_true(any_present, "some entity to author one, or the distinction is untested");
}

TEST(an_absent_resource_minimum_means_never_not_everywhere) {
    bool any_absent = false;
    for (const EntityRules& e : entity_rules()) {
        if (!e.economy.air_terminal_resource_min.has_value()) any_absent = true;
    }
    check::is_true(any_absent,
                   "some entity to build no air terminal at all; a zero minimum would "
                   "build one on every cell");
}

TEST(an_entity_with_no_habitat_rule_settles_anywhere) {
    // Not fussy, not homeless.
    for (const EntityRules& e : entity_rules()) {
        if (!settlement_habitat(e, 3).empty()) continue;
        check::is_true(settlement_admits(e, 3, blank()),
                       std::string(e.id) + " with no rule to admit blank ground");
    }
}

TEST(a_three_level_habitat_evaluates_its_innermost_branch) {
    // frosthold_dwarf nests all -> any -> all. The flattener records children as
    // indices in a shared pool precisely because a contiguous RANGE is correct only
    // while no child has children of its own -- which stopped being true when this data
    // arrived. A sample that satisfies ONLY the innermost `all` is what separates the
    // two: a range-based flattener sweeps the grandchildren in as siblings and the
    // branch stops meaning what it says.
    const EntityRules* frost = entity_rule("frosthold_dwarf");
    check::is_true(frost != nullptr, "frosthold_dwarf to be in the registry");
    if (frost == nullptr) return;

    HabitatSample s;
    s.abs_latitude = 60.0;   // >= 50
    s.temperature = -5.0;    // <= 5
    s.resource = 0.9;        // >= 0.45
    s.biome = 0.0;           // NOT the admitted biome
    s.tpi = 0.0;             // not above 6
    s.maritime = 0.0;        // not above 0.3
    s.slope = 20.0;          // > 10, and the inner `all` also needs height
    s.height = 100.0;        // > 15 -- the innermost pair, both satisfied
    check::is_true(settlement_admits(*frost, 0, s),
                   "the innermost slope-and-height branch to admit this ground");

    // The same ground with the height removed satisfies HALF the inner `all`, and no
    // other branch of the `any`. A flattener that flattened the grandchildren into the
    // `any` would admit it on the slope alone.
    s.height = 1.0;
    check::is_true(!settlement_admits(*frost, 0, s),
                   "half of an inner AND not to be enough");

    // And the outer `all` still binds: warm ground fails whatever the inner branch says.
    s.height = 100.0;
    s.temperature = 30.0;
    check::is_true(!settlement_admits(*frost, 0, s),
                   "the outer temperature ceiling to bind regardless of the inner branch");
}

TEST(the_entity_habitat_pools_are_not_the_civilizations_pools) {
    // They are separate trees in separate translation units. A tree reading the other's
    // pools would silently test another people's rule, and would not crash doing it.
    const HabitatPools entities = entity_habitat_pools();
    const HabitatPools civs = civilization_habitat_pools();
    check::is_true(entities.children.data() != civs.children.data(),
                   "the child pools to be distinct arrays");
    check::is_true(entities.numbers.data() != civs.numbers.data(),
                   "the number pools to be distinct arrays");
}

TEST(the_presentation_colours_are_real_colours) {
    for (const EntityRules& e : entity_rules()) {
        for (const Rgb& c : {e.presentation.map_color, e.presentation.wall_color,
                             e.presentation.roof_color}) {
            check::between(c.r, 0, 255, std::string(e.id) + " red channel");
            check::between(c.g, 0, 255, std::string(e.id) + " green channel");
            check::between(c.b, 0, 255, std::string(e.id) + " blue channel");
        }
    }
}

TEST(the_sky_bias_is_authored_by_someone) {
    // The winner of a floating island is a max over entities, so a table where every
    // bias is zero would decide every island on table order alone.
    bool any_opinion = false;
    for (const EntityRules& e : entity_rules()) {
        if (e.sky.score_bias != 0.0 || e.sky.moisture_score_weight != 0.0) any_opinion = true;
    }
    check::is_true(any_opinion,
                   "some entity to hold an opinion about sky islands, or every island "
                   "goes to whichever entity is listed first");
}
