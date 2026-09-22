// Civilization registry invariants.
//
// The data itself was checked against the Python registry once, exactly: 13 profiles,
// 32 traits each, four biome maps, and 1,512 habitat eligibility probes, all agreeing
// with zero disagreements. What is asserted HERE is what must keep holding as the data
// changes — the validator's own rules, restated where the C++ can enforce them.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

#include "fwg/civilizations.hpp"
#include "fwg/content.hpp"
#include "fwg/magic.hpp"

using namespace fwg;

// --- the table ------------------------------------------------------------

TEST(the_registry_has_entities_an_aggregate_and_a_default) {
    const auto profiles = civilization_profiles();
    check::is_true(profiles.size() >= 2, "a registry with something in it");

    std::set<std::string_view> ids;
    int entities = 0;
    for (const CivilizationProfile& p : profiles) {
        check::is_true(ids.insert(p.id).second, std::string(p.id) + " to be unique");
        check::is_true(!p.name.empty() && !p.description.empty() && !p.environment.empty(),
                       std::string(p.id) + " to be described");
        check::is_true(p.priority >= 0, "a non-negative habitat priority");
        if (p.is_entity) ++entities;
    }
    check::is_true(entities > 0, "somebody to settle the world");
    check::equal(civilization_ids().size(), static_cast<std::size_t>(entities),
                 "civilization_ids to be exactly the entities");

    check::is_true(civilization_profile(default_profile_id()) != nullptr, "the default to exist");
    check::is_true(civilization_profile("not_a_people") == nullptr, "an unknown id to return nothing");

    // Order is a contract: ids are exported by index in places, so an insert renumbers
    // saved worlds. Pinning the ends catches that without pinning the middle.
    check::equal(std::string(profiles.front().id), std::string("human_maritime"), "the first profile");
    check::equal(std::string(profiles.back().id), std::string("mixed"), "the last profile");
    check::is_true(!profiles.back().is_entity, "the aggregate does not settle");
}

TEST(every_trait_obeys_the_rules_the_registry_validates) {
    // These are the reference validator's own bounds, restated where this library can
    // enforce them. Data arrives through a generator, and a generator with no floor is
    // how a broken profile ships quietly.
    for (const CivilizationProfile& p : civilization_profiles()) {
        const CivilizationTraits& t = p.traits;
        const std::string who(p.id);
        const auto finite = [&](double v, const char* name) {
            check::is_true(std::isfinite(v), who + " " + name + " to be finite");
        };
        const auto positive = [&](double v, const char* name) {
            finite(v, name);
            check::is_true(v > 0.0, who + " " + name + " to be positive");
        };
        const auto unit = [&](double v, const char* name) {
            finite(v, name);
            check::between(v, 0.0, 1.0, who + " " + name);
        };
        const auto degrees = [&](double v, const char* name) {
            finite(v, name);
            check::is_true(v > 0.0 && v < 90.0, who + " " + name + " to be a slope in degrees");
        };

        positive(t.temperature_tolerance, "temperature_tolerance");
        positive(t.slope_comfort, "slope_comfort");
        positive(t.water_reach, "water_reach");
        positive(t.food_temperature_tolerance, "food_temperature_tolerance");
        positive(t.food_slope_comfort, "food_slope_comfort");
        positive(t.land_per_city_km2, "land_per_city_km2");
        positive(t.support_multiplier, "support_multiplier");
        positive(t.college_water_reach, "college_water_reach");

        for (const auto& [v, name] : {std::pair<double, const char*>{t.mutation_limit, "mutation_limit"},
                                      {t.difficult_fraction, "difficult_fraction"},
                                      {t.irrigation, "irrigation"},
                                      {t.moisture_ideal, "moisture_ideal"},
                                      {t.food_moisture_ideal, "food_moisture_ideal"},
                                      {t.college_flood_limit, "college_flood_limit"},
                                      {t.college_suitability_min, "college_suitability_min"},
                                      {t.water_weight, "water_weight"},
                                      {t.slope_weight, "slope_weight"},
                                      {t.climate_weight, "climate_weight"},
                                      {t.moisture_weight, "moisture_weight"},
                                      {t.resource_weight, "resource_weight"},
                                      {t.flood_penalty, "flood_penalty"},
                                      {t.magic_penalty, "magic_penalty"}}) {
            unit(v, name);
        }
        for (const auto& [v, name] :
             {std::pair<double, const char*>{t.site_slope_limit, "site_slope_limit"},
              {t.work_slope_limit, "work_slope_limit"},
              {t.college_slope_limit, "college_slope_limit"},
              {t.slope_comfort, "slope_comfort"},
              {t.food_slope_comfort, "food_slope_comfort"}}) {
            degrees(v, name);
        }

        check::is_true(t.food_moisture_ideal != 0.0, who + " food_moisture_ideal must not be zero");
        check::between(t.road_grade_limit, 0.01, 1.0, who + " road_grade_limit");
        check::is_true(t.college_temperature_min < t.college_temperature_max,
                       who + " college climate range to be a range");
        check::between(t.food_demand, 0.0, 10000.0, who + " food_demand");
        check::between(t.land_per_city_km2, 0.01, 1000.0, who + " land_per_city_km2");
        check::is_true(t.support_multiplier <= 10.0, who + " support_multiplier ceiling");
        check::between(t.minimum_founding_residents, 1.0, 10000.0, who + " minimum_founding_residents");
        check::between(t.food_temperature_ideal, -100.0, 100.0, who + " food_temperature_ideal");
    }
}

TEST(biome_opinions_reference_real_biomes_and_stay_in_range) {
    for (const CivilizationProfile& p : civilization_profiles()) {
        const std::string who(p.id);
        for (const BiomeOpinion& o : p.biome_preferences) {
            check::is_true(natural_biome(o.biome_id) != nullptr, who + " preference names a real biome");
            check::between(o.value, -1.0, 1.0, who + " biome preference");
        }
        for (const BiomeOpinion& o : p.food_biome_multipliers) {
            check::is_true(natural_biome(o.biome_id) != nullptr, who + " food multiplier names a real biome");
            check::between(o.value, 0.0, 2.0, who + " food multiplier");
        }
        for (const VariantOpinion& o : p.magic_biome_preferences) {
            check::is_true(o.variant_id.find('.') != std::string_view::npos,
                           who + " variant id to be <core>.<school>");
            check::between(o.value, -1.0, 1.0, who + " variant preference");
        }
        for (const VariantOpinion& o : p.food_magic_biome_multipliers) {
            check::between(o.value, 0.0, 2.0, who + " variant food multiplier");
        }
    }
}

TEST(the_registry_identity_is_a_digest_of_its_own_data) {
    const RegistryIdentity id = registry_identity();
    check::equal(id.sha256.size(), std::size_t{64}, "a sha256 of this data");
    check::equal(id.source_sha256.size(), std::size_t{64}, "and of the registry it came from");
    check::is_true(id.sha256 != id.source_sha256,
                   "the two digests to differ: they are over different bytes, and conflating them "
                   "would claim a reproduction this project does not attempt");
    check::is_true(id.schema_version > 0 && id.revision > 0, "a versioned registry");
}

// --- habitat rules --------------------------------------------------------

TEST(a_people_with_no_habitat_rule_lives_anywhere) {
    HabitatSample anywhere;
    anywhere.temperature = -40.0;
    anywhere.moisture = 0.0;
    anywhere.biome = 5;
    int unfussy = 0;
    for (const CivilizationProfile& p : civilization_profiles()) {
        if (p.habitat.size() == 1 && p.habitat[0].op == HabitatOp::always) {
            ++unfussy;
            check::is_true(habitat_admits(p, anywhere), std::string(p.id) + " to accept any ground");
            HabitatSample other;
            other.temperature = 35.0;
            other.moisture = 1.0;
            other.biome = 7;
            check::is_true(habitat_admits(p, other), std::string(p.id) + " to accept other ground too");
        }
    }
    check::is_true(unfussy > 0, "some people to have no habitat rule at all");
}

TEST(habitat_rules_actually_exclude_ground) {
    // A rule that admits everything is indistinguishable from no rule, and would make
    // the whole habitat mechanism decorative.
    int discriminating = 0;
    for (const CivilizationProfile& p : civilization_profiles()) {
        if (p.habitat.size() == 1 && p.habitat[0].op == HabitatOp::always) continue;
        bool admitted = false;
        bool refused = false;
        for (int biome = 0; biome <= 17 && !(admitted && refused); ++biome) {
            for (int temp = -30; temp <= 30; temp += 10) {
                for (int moist = 0; moist <= 10; moist += 5) {
                    for (int maritime = 0; maritime <= 1; ++maritime) {
                        HabitatSample s;
                        s.biome = biome;
                        s.temperature = temp;
                        s.moisture = moist / 10.0;
                        s.maritime = maritime;
                        s.landmass_area_m2 = maritime ? 5e9 : 5e11;
                        s.landmass_fraction = maritime ? 0.02 : 0.6;
                        s.largest_landmass = maritime ? 0.0 : 1.0;
                        (habitat_admits(p, s) ? admitted : refused) = true;
                    }
                }
            }
        }
        ++discriminating;
        check::is_true(admitted, std::string(p.id) + " to accept some ground");
        check::is_true(refused, std::string(p.id) + " to refuse some ground");
    }
    check::is_true(discriminating >= 4, "several peoples to be choosy");
}

TEST(eligibility_partitions_by_allocation_group) {
    // Allocation groups partition capacity: two variants of one people never both
    // claim the same ground, however well it suits them both.
    int checked = 0;
    for (int biome = 0; biome <= 17; ++biome) {
        for (int temp = -30; temp <= 30; temp += 15) {
            for (int maritime = 0; maritime <= 1; ++maritime) {
                HabitatSample s;
                s.biome = biome;
                s.temperature = temp;
                s.moisture = 0.5;
                s.maritime = maritime;
                s.landmass_area_m2 = maritime ? 5e9 : 5e11;
                s.landmass_fraction = maritime ? 0.02 : 0.6;
                s.largest_landmass = maritime ? 0.0 : 1.0;

                const auto eligible = eligible_civilizations(s);
                std::set<std::string_view> groups;
                for (const std::string_view id : eligible) {
                    const CivilizationProfile* p = civilization_profile(id);
                    check::is_true(p != nullptr, "an eligible id to name a profile");
                    check::is_true(p->is_entity, "an aggregate never settles");
                    if (!p->allocation_group.empty()) {
                        check::is_true(groups.insert(p->allocation_group).second,
                                       "at most one claimant per allocation group");
                    }
                }
                ++checked;
            }
        }
    }
    check::is_true(checked > 50, "a real sweep of environments");
}

// --- biome opinions -------------------------------------------------------

TEST(an_exact_magical_state_overrides_the_core_beneath_it) {
    const CivilizationProfile* p = civilization_profile(default_profile_id());
    check::is_true(p != nullptr, "the default profile");

    // With no variant, the natural core's opinion stands.
    const double core_only = biome_preference(*p, 3);
    check::between(core_only, -1.0, 1.0, "a core preference");

    // A core nobody authored an opinion about is neutral, and feeds normally.
    check::equal(biome_preference(*p, 999), 0.0, "an unknown core is neutral");
    check::equal(biome_food_multiplier(*p, 999), 1.0, "an unknown core feeds normally");
}

TEST(ground_held_by_a_hidden_school_is_uninhabitable_to_everyone) {
    // Not 52 authored opinions per people: nobody has a cultural preference about a
    // school they have never heard of. Without this rule a rotted grassland reads as
    // plain grassland and settlement placement finds corrupted ground as attractive as
    // clean ground.
    int hidden_seen = 0;
    for (const MagicSchool& s : magic_school_table()) {
        if (!s.hidden) continue;
        ++hidden_seen;
        const std::string variant = std::string("grassland.") + std::string(s.name);
        check::is_true(is_hidden_variant(variant), variant + " to be hidden ground");
        for (const CivilizationProfile& p : civilization_profiles()) {
            check::equal(biome_preference(p, 3, variant), kHiddenPreference,
                         std::string(p.id) + " refuses " + variant);
            check::equal(biome_food_multiplier(p, 3, variant), kHiddenFood,
                         std::string(p.id) + " starves on " + variant);
        }
    }
    check::equal(hidden_seen, 4, "four hidden schools");

    // A known school's ground is NOT automatically refused.
    check::is_true(!is_hidden_variant("grassland.weave"), "a known school is not hidden");
    check::is_true(!is_hidden_variant("grassland"), "a bare core is not a variant");
    const CivilizationProfile* p = civilization_profile(default_profile_id());
    check::is_true(biome_preference(*p, 3, "grassland.weave") > kHiddenPreference,
                   "known-school ground to be better than hidden ground");
}
