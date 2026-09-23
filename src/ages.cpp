#include "fwg/ages.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "fwg/astrology.hpp"
#include "fwg/beasties_stage.hpp"
#include "fwg/history.hpp"
#include "fwg/magic.hpp"
#include "fwg/nomads.hpp"
#include "fwg/populated_stage.hpp"
#include "fwg/random.hpp"
#include "fwg/roads.hpp"
#include "fwg/time.hpp"
#include "fwg/villains.hpp"
#include "fwg/visitation.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

// The ruin a fallen city leaves: the city as it stood, then how it fell.
Ruin ruin_of(const SettlementSite& city, const CityFate& fate, int age) {
    Ruin ruin;
    ruin.uid = city.uid;
    ruin.name = city.name;
    ruin.node = city.node;
    ruin.x = city.x;
    ruin.z = city.z;
    ruin.direction = city.direction;
    ruin.height_m = city.height_m;
    ruin.population_profile = city.population_profile;
    ruin.civilization_id = city.population_profile;
    ruin.city_class = city.city_class;
    ruin.source_culture = city.source_culture;
    ruin.founded_age = city.founded_age;
    ruin.war_history = city.war_history;

    ruin.id = "ruin-" + city.uid;
    ruin.destroyed_age = age;

    ruin.cause = fate.cause;
    ruin.reason = fate.reason;
    ruin.evidence = fate.evidence;
    ruin.probability = fate.probability;
    ruin.roll = fate.roll;
    ruin.new_node_school = fate.new_node_school;
    ruin.legacy = fate.legacy;
    return ruin;
}

// The villains' view of the world as it stands when each villain pass runs: the cities,
// their cultural regions, the threat reports, the ruins a villain is blamed for, the ley
// field, and the block itself.
VillainRequest villain_request(const World& world, int age) {
    const Config& cfg = world.effective;
    VillainRequest request;
    request.rise = cfg.options[Option::villain_rise];
    request.hold = cfg.options[Option::villain_hold];
    request.density = cfg.options[Option::villain_density];
    request.age = age;
    request.globe_radius_m = cfg.globe_radius;
    request.settlement_spacing_m = cfg.settlement_spacing;
    for (const SettlementSite& s : world.settlements.sites) {
        VillainCity city;
        city.uid = s.uid;
        city.id = s.id;
        city.node = s.node;
        city.direction = s.direction;
        city.population_profile = s.population_profile;
        city.city_class = s.city_class;
        city.population_estimate = static_cast<double>(s.population_estimate);
        request.cities.push_back(std::move(city));
    }
    if (world.has_humans) {
        for (const CultureGroup& group : world.humans.cultures) request.cultures.push_back({group.id, group.city_ids});
    }
    if (world.has_threat_assessments) {
        for (const ThreatReport& t : world.threat_assessments.cities) {
            request.reports.push_back({t.city_uid, t.regional_threat, t.outlook.war_risk, t.outlook.war_hunger,
                                       t.war_pressure, t.nest_pressure, t.ley_pressure});
        }
    }
    for (const Ruin& ruin : world.ruins) request.ruins.push_back({ruin.id, ruin.evidence.villain_uid});
    request.magic = world.has_magic ? &world.magic : nullptr;
    if (world.has_villains) request.villains = world.villains;
    return request;
}

// Every node, then every line, of each school, each drawing from that school's own stream
// for this age: reordering either list silently reassigns every multiplier. Capped at the
// network ceiling with no floor; the multiplier is strictly positive.
void drift_ley(MagicReport& magic, std::uint32_t seed, int age) {
    for (auto& [name, net] : magic.networks) {
        Rng rng = Rng::child(seed, "age-ley-" + name, age);
        for (LeyNode& node : net.nodes) {
            node.intensity = std::min(kLeyIntensityMax, node.intensity * rng.uniform(kLeyAgeLow, kLeyAgeHigh));
        }
        for (LeyEdge& edge : net.edges) {
            edge.intensity = std::min(kLeyIntensityMax, edge.intensity * rng.uniform(kLeyAgeLow, kLeyAgeHigh));
        }
    }
}

// A refugee band that reached a standing town is absorbed by it rather than founding a
// settlement on top of it. Runs on the cities that SURVIVED the boundary, so a refuge that
// fell this age takes nobody in, and before the rebuild, which carries the count. Writes
// nothing on a generated world: the candidates are written at stage 16, after both
// generation-time ages have turned.
void absorb_refugees(World& world, std::vector<SettlementSite>& survivors, int age) {
    std::vector<NomadSettlementCandidate>& candidates = world.nomad_effects.settlement_candidates;
    if (candidates.empty()) return;
    std::vector<NomadSite> standing;
    standing.reserve(survivors.size());
    for (const SettlementSite& s : survivors) {
        NomadSite site;
        site.uid = s.uid;
        site.id = std::to_string(s.id);
        site.node = s.node;
        site.absorbed_bands = s.absorbed_bands;
        site.absorbed_refugees = s.absorbed_refugees;
        standing.push_back(std::move(site));
    }
    absorb_survivor_camps(candidates, world.nomads.groups, standing, age);
    for (std::size_t i = 0; i < survivors.size(); ++i) {
        survivors[i].absorbed_bands = standing[i].absorbed_bands;
        survivors[i].absorbed_refugees = standing[i].absorbed_refugees;
    }
}

// Each pass a rebuild re-runs records itself again; the list says what ran, once.
void dedupe_systems(std::vector<std::string>& systems) {
    std::set<std::string> seen;
    std::vector<std::string> out;
    for (std::string& name : systems) {
        if (seen.insert(name).second) out.push_back(std::move(name));
    }
    systems = std::move(out);
}

}  // namespace

// The versioned state boundary a world re-enters through. Everything an advance runs reads
// the sky, the pantheon and the creatures, so a world without them cannot be advanced; and a
// pantheon from another catalogue would name gods this build does not know.
void validate_age_world(const World& world) {
    const Config& cfg = world.config;
    if (cfg.world_recipe != 3 || world.completed_stage < 13 || cfg.size > 1025) {
        throw AgeAdvanceError("config", "Age advancement requires a recipe 3 world generated through creatures "
                                        "(phase 13) on a grid of at most 1025. This world is recipe " +
                                            std::to_string(cfg.world_recipe) + " at phase " +
                                            std::to_string(world.completed_stage) + ", size " +
                                            std::to_string(cfg.size) + ".");
    }
    if (world.history.version != kHistoryVersion || world.magic.version != 4) {
        throw AgeAdvanceError("magic", "This world predates the twelve-school contract, so it carries no hidden "
                                       "networks to validate. Regenerate it.");
    }
    if (!world.has_astrology) {
        throw AgeAdvanceError("astrology",
                              "This world predates the moon. It is seeded during generation, so regenerate rather "
                              "than migrate.");
    }
    if (!world.has_religion || world.religion.version != kReligionVersion) {
        throw AgeAdvanceError("religion", "This world predates the pantheon. Regenerate rather than migrate.");
    }
    if (world.religion.catalogue.sha256 != pantheon_identity().sha256) {
        throw AgeAdvanceError("religion.catalogue", "This world was built against a different pantheon catalogue, "
                                                    "so the gods it names may not exist here.");
    }
}

namespace {

}  // namespace

World advance_ages(const World& world, const AgeAdvanceRequest& request) {
    if (request.api_version != kAgeApiVersion) {
        throw AgeAdvanceError("api_version", "this build serves age API version 1");
    }
    if (request.steps < 1 || request.steps > kMaxAgeSteps) {
        throw AgeAdvanceError("steps", "between 1 and " + std::to_string(kMaxAgeSteps) + " ages");
    }
    if (request.moon_day && *request.moon_day < 0) throw AgeAdvanceError("moon_day", "a day, at least 0");
    if (request.leyline_edits.size() > kMaxLeylineEdits) {
        throw AgeAdvanceError("leyline_edits", "at most " + std::to_string(kMaxLeylineEdits) + " edits per request");
    }
    validate_age_world(world);
    if (!request.leyline_edits.empty() && world.config.magic_enabled == 0) {
        throw AgeAdvanceError("leyline_edits", "Leyline edits require a world generated with magic_enabled; this "
                                               "one has no networks to edit.");
    }

    World out = world;
    for (const LeylineEdit& edit : request.leyline_edits) {
        try {
            apply_leyline_edit(out.magic, edit);
        } catch (const LeylineEditError& refused) {
            throw AgeAdvanceError(refused.field(), refused.what());
        }
    }

    // The edited field, and everything that reads it, before the first age turns.
    evaluate_networks(out);
    add_terrain_labels(out);
    add_environment(out);
    add_biome_variants(out);
    refresh_astrology(out);

    const int start_age = static_cast<int>(out.history.ages.size());
    out.history.operations.push_back(
        AgeOperation{kAgeApiVersion, start_age, request.steps, request.leyline_edits, request.moon_day});
    for (int age = start_age + 1; age <= start_age + request.steps; ++age) {
        // The plans, and the scene built from them, describe cities an age is about to ruin;
        // both are redrawn at the end.
        out.city_plans = {};
        out.hamlet_plans = {};
        out.castle_plans = {};
        out.has_plans = false;
        out.world_scene = {};
        out.has_world_scene = false;
        const bool last = age == start_age + request.steps;
        age_transition(out, age, request.moon_day, /*final_age=*/last);
        // Regenerated, not carried: bands classified against the previous age would name
        // ruins that moved and refuges that no longer stand.
        if (last) add_simulation_complete(out);
    }
    dedupe_systems(out.systems);
    dedupe_warnings(out.warnings);
    return out;
}

std::vector<std::string_view> age_order() {
    return {"nests before fates",     "wars",   "city fates",
            "ruins and hamlet removal", "leyline update", "biomes",
            "civilization",           "nests",  "threat assessment"};
}

void rebuild_tail(World& world, const std::vector<SettlementSite>& survivors, const std::string& label, int age,
                  bool evaluate, bool religion) {
    if (evaluate) evaluate_networks(world);
    // The environment re-read over the moved field: the labels, the ecology over them, and
    // the magical state of the ground.
    add_terrain_labels(world);
    add_environment(world);
    add_biome_variants(world);

    // Civilization, rebuilt around the survivors. Every dependent institution -- roads,
    // hamlets, fortresses, colleges, food, trade -- is rebuilt from the cities that stand,
    // so no dead city's hamlet survives it.
    world.age = age;
    SettlementRebuild rebuild;
    rebuild.age = age;
    rebuild.survivors = survivors;
    for (const Ruin& ruin : world.ruins) rebuild.forbidden_nodes.push_back(ruin.node);
    rebuild.previous = world.settlements.founding;
    add_settlements(world, &rebuild);
    add_roads(world);
    add_populated_regions(world);
    if (world.has_astrology) refresh_astrology(world);

    place_nests(world, age);
    assess_world_threats(world, label, age);
    if (religion) {
        if (world.has_astrology) resolve_world_religion(world);
    } else if (world.has_astrology) {
        refresh_astrology(world);
    }
}

void age_transition(World& world, int age, std::optional<long long> moon_day, bool final_age) {
    if (!world.has_settlements) return;
    const Config& cfg = world.effective;
    const bool magic = cfg.magic_enabled != 0;

    // 1. The nests on the ground the finished age left: the fates read them.
    place_nests(world, age - 1);

    // The cities as the finished age left them. A copy, because the rebuild replaces the
    // list while the fates are still being written from it.
    std::vector<SettlementSite> cities = world.settlements.sites;

    // 2. The moon on the day the age turns. A momentary surge scales every potency the
    // lottery reads; the fields themselves are not rewritten.
    const std::optional<CityFateLunar> lunar =
        lunar_context(world.has_astrology ? &world.moon : nullptr, moon_day, world.settlements.founding.end_year,
                      cfg.options[Option::lunar_influence]);

    // 3. The cast advances on the assessment this age inherited.
    VillainAdvance advanced = advance_villains(villain_request(world, age));
    if (advanced.block) {
        world.villains = std::move(*advanced.block);
        world.has_villains = true;
    }

    // 4. Wars, before any other fate: a city a neighbour has already taken cannot also be
    // eaten by a dragon, and its ruin records the war that ended it.
    WarRequest wars;
    wars.ground = war_ground(world);
    wars.ground.villains = advanced.standing;
    wars.seed = cfg.seed;
    wars.age = age;
    wars.magic_enabled = magic;
    wars.survival = cfg.options[Option::war_survival];
    wars.layers = &world.layers;
    if (world.has_magic) {
        wars.dominance_threshold = world.magic.mutation_threshold;
        wars.dominance_margin = world.magic.dominance_margin;
    }
    const WarResolution fought = resolve_wars(wars);
    {
        // Both sides carry the war, so a survivor's history says what it has already fought.
        std::map<std::string, SettlementSite*> by_uid;
        for (SettlementSite& city : cities) by_uid[city.uid] = &city;
        for (const WarRecord& war : fought.wars) {
            for (const std::string& uid : war.participants) {
                by_uid.at(uid)->war_history.push_back(war_participation(war, uid));
            }
            // World option war_survival: the loser stands as a vassal, a living enemy in
            // both histories.
            if (war.outcome == "vassalage") by_uid.at(war.defeated_uid)->vassal_of = war.victor_uid;
        }
    }

    // 5. Every city's fate, decided against the fields as the finished age left them.
    CityFateRequest fates;
    fates.seed = cfg.seed;
    fates.age = age;
    fates.globe_radius_m = cfg.globe_radius;
    fates.magic_enabled = magic;
    fates.layers = &world.layers;
    fates.nests = monster_nests(world);
    fates.lunar = lunar;
    fates.villains = advanced.standing;
    fates.dominance_threshold = wars.dominance_threshold;
    fates.dominance_margin = wars.dominance_margin;

    std::vector<SettlementSite> survivors;
    std::vector<Ruin> events;
    for (const SettlementSite& city : cities) {
        const WarCity as_war = war_city(city);
        std::optional<CityFate> fate;
        if (const auto lost = fought.fates.find(city.uid); lost != fought.fates.end()) {
            // The victor's own magic scars the ground it took.
            fate = war_ruin_fate(fates, as_war, lost->second);
        } else {
            fate = city_fate(fates, as_war);
        }
        if (!fate) {
            survivors.push_back(city);
            continue;
        }
        events.push_back(ruin_of(city, *fate, age));
    }
    world.ruins.insert(world.ruins.end(), events.begin(), events.end());

    // 6. Only now does the ley field move: it drifts, each ruin plants its key point, and a
    // god still walking the world goes home, leaving its footprint.
    if (magic) {
        drift_ley(world.magic, cfg.seed, age);
        for (const Ruin& ruin : events) {
            if (ruin.new_node_school.empty()) continue;
            plant_key_point(world.magic.networks.at(ruin.new_node_school),
                            {ruin.id + "-key", ruin.direction, ruin.legacy.intensity});
        }
        if (world.has_religion) {
            for (const std::string& god_id : walking_gods(world.religion, world.visitations)) {
                VisitationDeparture gone = depart_god({god_id, age, world.magic, world.religion, world.visitations});
                world.magic = std::move(gone.magic);
                world.religion = std::move(gone.religion);
                world.visitations = std::move(gone.visitations);
            }
        }
    }

    // 7. Refugees reach the towns that stand.
    absorb_refugees(world, survivors, age);

    // 8. The rebuild.
    rebuild_tail(world, survivors, "age", age, /*evaluate=*/magic, /*religion=*/true);

    // 9. The simulation ends with promotions. Accumulation alone cannot seat anybody in the
    // two ages a generation turns, so the last age is where the world names its
    // antagonists -- before the works, so a promoted villain sinks its well and stakes its
    // claims in the age it rises, exactly as an accumulated one does.
    if (final_age) {
        VillainPromotion promoted = promote_villains(villain_request(world, age));
        if (promoted.block) {
            world.villains = std::move(*promoted.block);
            world.has_villains = true;
        }
    }
    if (world.has_villains) {
        // Built after the rebuild, on settled ground: a well joins the holdings a villain
        // actually has this age, and a claim names the peoples actually near it.
        VillainWorks built = build_villain_works(villain_request(world, age));
        world.villains = std::move(built.block);
        if (!built.works.empty()) {
            // A well changed the ley geometry.
            world.magic = std::move(built.magic);
            evaluate_networks(world);
            add_biome_variants(world);
        }
        world.villains.outlook = villain_outlook(villain_request(world, age));
    }

    // 10. The age's record.
    AgeRecord record;
    record.age = age;
    record.events = std::move(events);
    record.wars = fought.wars;
    record.moon = lunar;
    std::set<std::string> old_ids;
    for (const SettlementSite& s : survivors) old_ids.insert(s.uid);
    record.surviving_city_ids.assign(old_ids.begin(), old_ids.end());
    for (const SettlementSite& s : world.settlements.sites) {
        if (old_ids.count(s.uid) == 0) record.new_city_ids.push_back(s.uid);
    }
    world.history.ages.push_back(std::move(record));
    world.age = age;

    world.systems.push_back("age_transition");
    dedupe_systems(world.systems);
    dedupe_warnings(world.warnings);
}

}  // namespace fwg
