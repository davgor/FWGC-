// The civilization registry: who lives in this world, and what ground suits them.
//
// A profile is a complete, independent trait record — thirty-two numbers describing
// what a people can farm, build on, walk up, tolerate and reach, plus its opinions
// about every natural biome and every magically mutated one. Stages 10 to 12 site
// civilizations from these, stage 16's planners build from them, and the food model
// prices everything through them.
//
// Two things about the shape are deliberate and were learned the hard way in the
// reference:
//
//   * **Traits are named fields, not a string-keyed map.** An untyped `map<string,
//     double>` looks flexible and is only safe for numerics: the moment a profile
//     grows a string-valued trait, a map-based reader parses it as a number and
//     throws. Named fields make that a compile error instead.
//   * **Profile order is a contract.** Civilization ids are exported by index in
//     places. Append, never insert.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace fwg {

// Which fields a habitat rule can test. `kNone` marks a combinator node.
enum class HabitatField : int {
    none = 0,
    biome,
    variant,
    temperature,
    moisture,
    maritime,
    landmass_area_m2,
    landmass_fraction,
    largest_landmass,
    resource,
    slope,
    height,
    tpi,
    coastal_support,
    abs_latitude,
};

enum class HabitatOp : int {
    leaf = 0,  // a comparison on one field
    all,       // every child must hold
    any,       // at least one child must hold
    always,    // an empty rule: everywhere suits
};

// One node of a habitat predicate, in a flat array: no allocation to evaluate, and
// the whole tree of every profile lives in one cache-friendly block.
struct HabitatNode {
    HabitatOp op;
    HabitatField field;
    // Bounds. Only the ones whose `has_` flag is set take part.
    double min_value;
    double max_value;
    double gt_value;
    double equals_value;
    bool has_min;
    bool has_max;
    bool has_gt;
    bool has_equals;
    // Membership test, as a range into the shared `in` pools. A rule tests either
    // numbers or strings, never both.
    std::uint32_t number_begin, number_end;
    std::uint32_t text_begin, text_end;
    // Children, as a range into a shared pool of node INDICES -- not a range of
    // nodes. A combinator's children are only contiguous in the node array while no
    // child has children of its own; the moment a rule nests two levels, a range would
    // silently sweep in a grandchild. The indirection costs one lookup and cannot.
    std::uint32_t child_begin, child_end;
};

// What a habitat rule is evaluated against. A field the caller cannot supply is
// `has_*` false, and a rule testing it does not hold.
struct HabitatSample {
    double biome = 0.0;
    std::string_view variant;
    double temperature = 0.0;
    double moisture = 0.0;
    double maritime = 0.0;
    double landmass_area_m2 = 0.0;
    double landmass_fraction = 0.0;
    double largest_landmass = 0.0;
    double resource = 0.0;
    double slope = 0.0;
    double height = 0.0;
    double tpi = 0.0;
    double coastal_support = 0.0;
    double abs_latitude = 0.0;
};

// Thirty-two numbers, in the registry's own alphabetical order.
struct CivilizationTraits {
    double climate_weight;
    double college_flood_limit;
    double college_slope_limit;
    double college_suitability_min;
    double college_temperature_max;
    double college_temperature_min;
    double college_water_reach;
    double difficult_fraction;
    double flood_penalty;
    double food_demand;
    double food_moisture_ideal;
    double food_slope_comfort;
    double food_temperature_ideal;
    double food_temperature_tolerance;
    double irrigation;
    double land_per_city_km2;
    double magic_penalty;
    double minimum_founding_residents;
    double moisture_ideal;
    double moisture_weight;
    double mutation_limit;
    double resource_weight;
    double road_grade_limit;
    double site_slope_limit;
    double slope_comfort;
    double slope_weight;
    double support_multiplier;
    double temperature_ideal;
    double temperature_tolerance;
    double water_reach;
    double water_weight;
    double work_slope_limit;
};

// A biome opinion: preference in -1..1, food multiplier in 0..2.
struct BiomeOpinion {
    int biome_id;   // natural biome identity
    double value;
};
struct VariantOpinion {
    std::string_view variant_id;  // "<core>.<school>", e.g. "grassland.weave"
    double value;
};

struct CivilizationProfile {
    std::string_view id;
    std::string_view name;
    std::string_view description;
    std::string_view environment;
    std::string_view inspiration;        // empty when the profile authors none
    std::string_view allocation_group;   // empty when none
    std::string_view structure_blocks;   // empty when none
    bool is_entity;                      // false for an aggregate such as "mixed"
    int priority;                        // habitat priority; higher claims ground first
    CivilizationTraits traits;
    std::span<const HabitatNode> habitat;
    std::span<const BiomeOpinion> biome_preferences;
    std::span<const BiomeOpinion> food_biome_multipliers;
    std::span<const VariantOpinion> magic_biome_preferences;
    std::span<const VariantOpinion> food_magic_biome_multipliers;
};

// Every profile, in contract order.
std::span<const CivilizationProfile> civilization_profiles();

// By id, or nullptr.
const CivilizationProfile* civilization_profile(std::string_view id);

// Only the entity civilizations — the ones that actually settle. Excludes aggregates.
std::span<const std::string_view> civilization_ids();

// The profile a world uses when the caller names none.
std::string_view default_profile_id();

// Identity of the registry these profiles came from, and of this copy of them.
struct RegistryIdentity {
    int schema_version;
    int revision;
    std::string_view source_sha256;  // the Python registry's own digest, for provenance
    std::string_view sha256;         // over THIS data; what a world records
};
RegistryIdentity registry_identity();

// The pools a flattened habitat tree indexes into: the shared child pool a node's
// child_begin/child_end reads, and the membership pools its `in` test compares against.
//
// Bundled rather than reached through free functions because there is MORE THAN ONE set
// of habitat trees -- the civilizations' own, and the entity settlement rules, which are
// generated into a different translation unit and cannot share arrays with these. A tree
// that carried only its nodes would silently index another tree's pools.
struct HabitatPools {
    std::span<const std::uint32_t> children;
    std::span<const double> numbers;
    std::span<const std::string_view> texts;
};

// The pools the civilization profiles' habitat trees read.
HabitatPools civilization_habitat_pools();

// Does this ground suit these people? An empty rule suits everywhere.
bool habitat_admits(const CivilizationProfile& profile, const HabitatSample& sample);
bool habitat_admits(std::span<const HabitatNode> nodes, const HabitatPools& pools,
                    std::uint32_t index, const HabitatSample& sample);

// A people's opinion of exact ground. An explicit magical state overrides the natural
// core beneath it.
//
// Ground held by a HIDDEN school is uninhabitable to everyone, and that is a rule
// rather than fifty-two authored opinions per people: nobody has a cultural preference
// about a school they have never heard of. Without it a rotted grassland would read as
// plain grassland and settlement placement would find corrupted ground exactly as
// attractive as clean ground.
inline constexpr double kHiddenPreference = -1.0;
inline constexpr double kHiddenFood = 0.0;

double biome_preference(const CivilizationProfile& profile, int core_biome_id,
                        std::string_view variant_id = {});
double biome_food_multiplier(const CivilizationProfile& profile, int core_biome_id,
                             std::string_view variant_id = {});

// Which peoples this ground admits, as ids in the order they claimed it.
//
// Allocation groups PARTITION capacity: once a member of a group has claimed ground,
// no sibling can also claim it, so two variants of one people never both settle the
// same environment. Lowest priority is considered first, ties broken by id, so the
// order is a property of the registry rather than of the caller.
std::vector<std::string_view> eligible_civilizations(const HabitatSample& sample);

// Whether a variant id names ground held by a hidden school.
bool is_hidden_variant(std::string_view variant_id);

}  // namespace fwg
