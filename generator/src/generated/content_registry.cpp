// GENERATED FILE -- DO NOT EDIT.
//
// Produced by tools/embed_content.py from data/registry-sections.json.
// Edit the JSON and regenerate; `python tools/embed_content.py --check` fails if this
// file is stale.

#include "fwg/founding.hpp"

namespace fwg {
namespace {

// ORDER IS A CONTRACT. Founding rotates initiative through this table by turn,
// so reordering it changes which parent claims a contested site first.
constexpr ParentRace kParentRaces[] = {
    {"human", "Human", 95.0},
    {"elf", "Elf", 25.0},
    {"dwarf", "Dwarf", 60.0},
};

}  // namespace

std::span<const ParentRace> parent_races() { return kParentRaces; }

FoundingRules authored_founding_rules() {
    FoundingRules r;
    r.years_per_round = 250;
    r.origin_min_separation_degrees = 90.0;
    r.diaspora_interval_years = 1000;
    r.diaspora_bonus_per_parent = 1;
    return r;
}

RegistryDefaults registry_defaults() {
    RegistryDefaults d;
    d.profile_id = "human_heartland";
    d.aggregate_profile_id = "mixed";
    d.resource_score_factor_label = "gnome_metal_affinity";
    d.normal_site_reason = "Combined freshwater, slope, climate and flood-risk score";
    d.medium_suitability_min = 0.65;
    return d;
}

}  // namespace fwg
