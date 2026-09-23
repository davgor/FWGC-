#include "fwg/civilizations.hpp"

#include <algorithm>
#include <cmath>

#include "fwg/content.hpp"
#include "fwg/magic.hpp"

namespace fwg {
namespace {

// The value a sample offers for a field. `biome` and `variant` are identities rather
// than measurements, so `variant` is handled separately by the membership test.
double sample_value(HabitatField field, const HabitatSample& s) {
    switch (field) {
        case HabitatField::biome: return s.biome;
        case HabitatField::temperature: return s.temperature;
        case HabitatField::moisture: return s.moisture;
        case HabitatField::maritime: return s.maritime;
        case HabitatField::landmass_area_m2: return s.landmass_area_m2;
        case HabitatField::landmass_fraction: return s.landmass_fraction;
        case HabitatField::largest_landmass: return s.largest_landmass;
        case HabitatField::resource: return s.resource;
        case HabitatField::slope: return s.slope;
        case HabitatField::height: return s.height;
        case HabitatField::tpi: return s.tpi;
        case HabitatField::coastal_support: return s.coastal_support;
        case HabitatField::abs_latitude: return s.abs_latitude;
        case HabitatField::variant:
        case HabitatField::none:
            return 0.0;
    }
    return 0.0;
}

}  // namespace

bool habitat_admits(std::span<const HabitatNode> nodes, const HabitatPools& pools,
                    std::uint32_t index, const HabitatSample& sample) {
    if (nodes.empty() || index >= nodes.size()) return true;
    const HabitatNode& node = nodes[index];
    const auto children = pools.children;

    switch (node.op) {
        case HabitatOp::always:
            // An empty rule suits everywhere. A people with no habitat rule is not
            // fussy, not homeless.
            return true;
        case HabitatOp::all:
            for (std::uint32_t i = node.child_begin; i < node.child_end; ++i) {
                if (!habitat_admits(nodes, pools, children[i], sample)) return false;
            }
            return true;
        case HabitatOp::any:
            for (std::uint32_t i = node.child_begin; i < node.child_end; ++i) {
                if (habitat_admits(nodes, pools, children[i], sample)) return true;
            }
            return false;
        case HabitatOp::leaf:
            break;
    }

    // A membership test on a text field compares identities, not magnitudes.
    if (node.text_end > node.text_begin) {
        const auto texts = pools.texts;
        bool found = false;
        for (std::uint32_t i = node.text_begin; i < node.text_end; ++i) {
            if (texts[i] == sample.variant) { found = true; break; }
        }
        if (!found) return false;
    }

    const double value = sample_value(node.field, sample);

    if (node.number_end > node.number_begin) {
        const auto numbers = pools.numbers;
        bool found = false;
        for (std::uint32_t i = node.number_begin; i < node.number_end; ++i) {
            if (numbers[i] == value) { found = true; break; }
        }
        if (!found) return false;
    }

    // min and max are inclusive; gt is strict. Each applies only if authored, so a
    // rule with only a floor does not accidentally acquire a ceiling of zero.
    if (node.has_min && !(value >= node.min_value)) return false;
    if (node.has_max && !(value <= node.max_value)) return false;
    if (node.has_gt && !(value > node.gt_value)) return false;
    if (node.has_equals && value != node.equals_value) return false;
    return true;
}

bool habitat_admits(const CivilizationProfile& profile, const HabitatSample& sample) {
    if (profile.habitat.empty()) return true;
    // Child indices are relative to the profile's own first node, so its span is
    // self-contained and the root is always index 0.
    return habitat_admits(profile.habitat, civilization_habitat_pools(), 0, sample);
}

const CivilizationProfile* civilization_profile(std::string_view id) {
    for (const CivilizationProfile& p : civilization_profiles()) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

std::vector<std::string_view> eligible_civilizations(const HabitatSample& sample) {
    const auto profiles = civilization_profiles();

    // Lowest priority first, then by id. The reference sorts the registry itself, so
    // the order is the registry's, not the caller's.
    std::vector<const CivilizationProfile*> ordered;
    ordered.reserve(profiles.size());
    for (const CivilizationProfile& p : profiles) ordered.push_back(&p);
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const CivilizationProfile* a, const CivilizationProfile* b) {
                         if (a->priority != b->priority) return a->priority < b->priority;
                         return a->id < b->id;
                     });

    std::vector<std::string_view> eligible;
    std::vector<std::string_view> claimed;
    for (const CivilizationProfile* p : ordered) {
        if (!p->is_entity) continue;  // an aggregate does not settle
        const bool grouped = !p->allocation_group.empty();
        if (grouped && std::find(claimed.begin(), claimed.end(), p->allocation_group) != claimed.end()) {
            continue;  // a sibling already took this ground
        }
        if (!habitat_admits(*p, sample)) continue;
        eligible.push_back(p->id);
        if (grouped) claimed.push_back(p->allocation_group);
    }
    return eligible;
}

bool is_hidden_variant(std::string_view variant_id) {
    // A variant id is "<core>.<school>". The school decides whether it is hidden.
    const std::size_t dot = variant_id.rfind('.');
    if (dot == std::string_view::npos) return false;
    const std::string_view school = variant_id.substr(dot + 1);
    for (const MagicSchool& s : magic_school_table()) {
        if (s.name == school) return s.hidden;
    }
    return false;
}

namespace {

const double* find_variant(std::span<const VariantOpinion> table, std::string_view id) {
    if (id.empty()) return nullptr;
    for (const VariantOpinion& row : table) {
        if (row.variant_id == id) return &row.value;
    }
    return nullptr;
}

const double* find_biome(std::span<const BiomeOpinion> table, int id) {
    for (const BiomeOpinion& row : table) {
        if (row.biome_id == id) return &row.value;
    }
    return nullptr;
}

}  // namespace

double biome_preference(const CivilizationProfile& profile, int core_biome_id, std::string_view variant_id) {
    // Ground held by a hidden school is uninhabitable to everyone. A profile MAY still
    // author an opinion about one, and none does — this is the rule, not 52 authored
    // opinions per people about schools nobody has heard of.
    if (is_hidden_variant(variant_id)) {
        if (const double* v = find_variant(profile.magic_biome_preferences, variant_id)) return *v;
        return kHiddenPreference;
    }
    // An exact magical state overrides the natural core beneath it. Without this
    // fallback order a rotted grassland reads as plain grassland.
    if (const double* v = find_variant(profile.magic_biome_preferences, variant_id)) return *v;
    if (const double* v = find_biome(profile.biome_preferences, core_biome_id)) return *v;
    return 0.0;
}

double biome_food_multiplier(const CivilizationProfile& profile, int core_biome_id,
                             std::string_view variant_id) {
    // Corrupted ground feeds nobody; which of the four took it is the corruption
    // record's business, not the terrain table's.
    if (is_hidden_variant(variant_id)) {
        if (const double* v = find_variant(profile.food_magic_biome_multipliers, variant_id)) return *v;
        return kHiddenFood;
    }
    if (const double* v = find_variant(profile.food_magic_biome_multipliers, variant_id)) return *v;
    if (const double* v = find_biome(profile.food_biome_multipliers, core_biome_id)) return *v;
    return 1.0;
}

}  // namespace fwg
