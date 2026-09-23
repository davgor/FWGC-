#include "fwg/civilization_report.hpp"

#include <sstream>
#include <string>

#include "fwg/sha256.hpp"

namespace fwg {
namespace {

// The name a habitat leaf's field is authored under.
std::string_view field_name(HabitatField field) {
    switch (field) {
        case HabitatField::none: return "none";
        case HabitatField::biome: return "biome";
        case HabitatField::variant: return "variant";
        case HabitatField::temperature: return "temperature";
        case HabitatField::moisture: return "moisture";
        case HabitatField::maritime: return "maritime";
        case HabitatField::landmass_area_m2: return "landmass_area_m2";
        case HabitatField::landmass_fraction: return "landmass_fraction";
        case HabitatField::largest_landmass: return "largest_landmass";
        case HabitatField::resource: return "resource";
        case HabitatField::slope: return "slope";
        case HabitatField::height: return "height";
        case HabitatField::tpi: return "tpi";
        case HabitatField::coastal_support: return "coastal_support";
        case HabitatField::abs_latitude: return "abs_latitude";
    }
    return "none";
}

// The reference stores largest_landmass as a Python bool (terrain_civilizations
// .landmass_context) and authors its test as `equals: false`. The generated table keeps
// only the number it is compared against, so the one boolean-valued field is named here.
bool boolean_valued(HabitatField field) { return field == HabitatField::largest_landmass; }

void write_opinions(JsonWriter& json, std::span<const BiomeOpinion> opinions) {
    json.begin_object();
    // The generated tables are already in the string order of their keys ("13" < "2").
    for (const BiomeOpinion& o : opinions) json.field(std::to_string(o.biome_id), o.value);
    json.end_object();
}

void write_opinions(JsonWriter& json, std::span<const VariantOpinion> opinions) {
    json.begin_object();
    for (const VariantOpinion& o : opinions) json.field(o.variant_id, o.value);
    json.end_object();
}

void text_or_null(JsonWriter& json, std::string_view key, std::string_view value) {
    json.key(key);
    if (value.empty()) {
        json.value_null();
    } else {
        json.value(value);
    }
}

// Every key in sorted order, so the same emitter serves the digest and the document. The
// hash, when given, lands in its own sorted position.
void write_profile_fields(JsonWriter& json, const CivilizationProfile& p, const std::string* hash) {
    const CivilizationTraits& t = p.traits;
    json.begin_object();
    json.key("biome_preferences");
    write_opinions(json, p.biome_preferences);
    json.key("civilization");
    json.begin_object();
    text_or_null(json, "allocation_group", p.allocation_group);
    json.field("environment", p.environment);
    json.key("habitat");
    write_habitat(json, p.habitat, civilization_habitat_pools(), 0);
    text_or_null(json, "inspiration", p.inspiration);
    json.field("kind", std::string_view(p.is_entity ? "entity" : "aggregate"));
    json.field("priority", p.priority);
    text_or_null(json, "structure_blocks", p.structure_blocks);
    json.end_object();
    json.field("climate_weight", t.climate_weight);
    json.field("college_flood_limit", t.college_flood_limit);
    json.field("college_slope_limit", t.college_slope_limit);
    json.field("college_suitability_min", t.college_suitability_min);
    json.field("college_temperature_max", t.college_temperature_max);
    json.field("college_temperature_min", t.college_temperature_min);
    json.field("college_water_reach", t.college_water_reach);
    if (hash != nullptr) json.field("definition_hash", std::string_view(*hash));
    json.field("description", p.description);
    json.field("difficult_fraction", t.difficult_fraction);
    json.field("flood_penalty", t.flood_penalty);
    json.key("food_biome_multipliers");
    write_opinions(json, p.food_biome_multipliers);
    json.field("food_demand", t.food_demand);
    json.key("food_magic_biome_multipliers");
    write_opinions(json, p.food_magic_biome_multipliers);
    json.field("food_moisture_ideal", t.food_moisture_ideal);
    json.field("food_slope_comfort", t.food_slope_comfort);
    json.field("food_temperature_ideal", t.food_temperature_ideal);
    json.field("food_temperature_tolerance", t.food_temperature_tolerance);
    json.field("id", p.id);
    json.field("irrigation", t.irrigation);
    json.field("land_per_city_km2", t.land_per_city_km2);
    json.key("magic_biome_preferences");
    write_opinions(json, p.magic_biome_preferences);
    json.field("magic_penalty", t.magic_penalty);
    // An integer in the registry (validate_profile refuses anything else); a double here only
    // because every trait is.
    json.field("minimum_founding_residents", static_cast<long long>(t.minimum_founding_residents));
    json.field("moisture_ideal", t.moisture_ideal);
    json.field("moisture_weight", t.moisture_weight);
    json.field("mutation_limit", t.mutation_limit);
    json.field("name", p.name);
    json.field("resource_weight", t.resource_weight);
    json.field("road_grade_limit", t.road_grade_limit);
    json.field("schema_version", kProfileSchemaVersion);
    json.field("site_slope_limit", t.site_slope_limit);
    json.field("slope_comfort", t.slope_comfort);
    json.field("slope_weight", t.slope_weight);
    json.field("support_multiplier", t.support_multiplier);
    json.field("temperature_ideal", t.temperature_ideal);
    json.field("temperature_tolerance", t.temperature_tolerance);
    json.field("water_reach", t.water_reach);
    json.field("water_weight", t.water_weight);
    json.field("work_slope_limit", t.work_slope_limit);
    json.end_object();
}

}  // namespace

void write_habitat(JsonWriter& json, std::span<const HabitatNode> nodes, const HabitatPools& pools,
                   std::uint32_t index) {
    json.begin_object();
    if (index < nodes.size()) {
        const HabitatNode& n = nodes[index];
        switch (n.op) {
            case HabitatOp::always:
                break;  // {} -- suits everywhere
            case HabitatOp::all:
            case HabitatOp::any:
                json.key(n.op == HabitatOp::all ? "all" : "any");
                json.begin_array();
                for (std::uint32_t c = n.child_begin; c < n.child_end; ++c) {
                    // Child indices are relative to the tree's own first node.
                    write_habitat(json, nodes, pools, pools.children[c]);
                }
                json.end_array();
                break;
            case HabitatOp::leaf:
                // Sorted key order: equals, field, gt, in, max, min.
                if (n.has_equals) {
                    json.key("equals");
                    if (boolean_valued(n.field)) {
                        json.value(n.equals_value != 0.0);
                    } else {
                        json.value(n.equals_value);
                    }
                }
                json.field("field", field_name(n.field));
                if (n.has_gt) json.field("gt", n.gt_value);
                if (n.number_end > n.number_begin || n.text_end > n.text_begin) {
                    json.key("in");
                    json.begin_array();
                    for (std::uint32_t k = n.number_begin; k < n.number_end; ++k) json.value(pools.numbers[k]);
                    for (std::uint32_t k = n.text_begin; k < n.text_end; ++k) json.value(pools.texts[k]);
                    json.end_array();
                }
                if (n.has_max) json.field("max", n.max_value);
                if (n.has_min) json.field("min", n.min_value);
                break;
        }
    }
    json.end_object();
}

std::string profile_definition_hash(const CivilizationProfile& profile) {
    std::ostringstream canonical;
    JsonWriter json(canonical, /*pretty=*/false);
    write_profile_fields(json, profile, nullptr);
    return sha256_hex(canonical.str());
}

void write_profile_record(JsonWriter& json, const CivilizationProfile& profile) {
    const std::string hash = profile_definition_hash(profile);
    write_profile_fields(json, profile, &hash);
}

}  // namespace fwg
