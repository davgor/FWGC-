#include "fwg/world_api.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "fwg/ages.hpp"
#include "fwg/beasties_stage.hpp"
#include "fwg/complete_stage.hpp"
#include "fwg/fates.hpp"
#include "fwg/json.hpp"
#include "fwg/magic.hpp"
#include "fwg/time.hpp"

namespace fwg {
namespace {

const SettlementSite* site_named(const std::vector<SettlementSite>& sites, const std::string& uid) {
    for (const SettlementSite& s : sites) {
        if (s.uid == uid) return &s;
    }
    return nullptr;
}

// The city as it stood, which a god's ruin copies exactly as an age's does.
Ruin ruin_from_city(const SettlementSite& city) {
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
    return ruin;
}

Ruin ruin_of(const CorruptionRuin& made, const std::vector<SettlementSite>& sites) {
    const SettlementSite* city = site_named(sites, made.city_uid);
    Ruin ruin = city != nullptr ? ruin_from_city(*city) : Ruin{};
    ruin.uid = made.city_uid;
    ruin.node = made.node;
    ruin.direction = made.direction;
    ruin.population_profile = made.population_profile;
    ruin.civilization_id = made.population_profile;
    ruin.city_class = made.city_class;
    ruin.id = made.id;
    ruin.kind = made.kind;
    ruin.destroyed_age = made.destroyed_age;
    ruin.asset_id = made.asset_id;
    ruin.cause = made.cause;
    ruin.reason = made.reason;
    ruin.god_id = made.god_id;
    ruin.evidence.school = made.school;
    ruin.share = made.share;
    ruin.corruption = made.corruption;
    ruin.probability = made.probability;
    ruin.roll = made.roll;
    ruin.new_node_school = made.new_node_school;
    ruin.legacy = {made.legacy.school, made.legacy.intensity, made.legacy.basis};
    return ruin;
}

Ruin ruin_of(const VisitationRuin& made, const std::vector<SettlementSite>& sites) {
    const SettlementSite* city = site_named(sites, made.city_uid);
    Ruin ruin = city != nullptr ? ruin_from_city(*city) : Ruin{};
    ruin.uid = made.city_uid;
    ruin.name = made.name;
    ruin.node = made.node;
    ruin.x = made.x;
    ruin.z = made.z;
    ruin.direction = made.direction;
    ruin.height_m = made.height_m;
    ruin.population_profile = made.population_profile;
    ruin.civilization_id = made.population_profile;
    ruin.city_class = made.city_class;
    ruin.id = made.id;
    ruin.kind = made.kind;
    ruin.destroyed_age = made.destroyed_age;
    ruin.asset_id = made.asset_id;
    ruin.cause = made.cause;
    ruin.reason = made.reason;
    const VisitationEvidence& e = made.evidence;
    switch (e.kind) {
        case VisitationCauseKind::Ley:
            ruin.evidence.school = e.school;
            ruin.evidence.potency = e.potency;
            break;
        case VisitationCauseKind::Nest:
            ruin.evidence.nest_id = e.nest_id;
            ruin.evidence.family = e.family;
            ruin.evidence.distance_m = e.distance_m;
            ruin.evidence.reach_m = e.reach_m;
            break;
        case VisitationCauseKind::Divine:
            ruin.god_id = e.god_id;
            ruin.aspect = e.aspect;
            ruin.evidence.distance_m = e.distance_m;
            ruin.evidence.reach_m = e.reach_m;
            ruin.faith_factor = e.faith_factor;
            break;
    }
    ruin.probability = made.probability;
    ruin.roll = made.roll;
    ruin.new_node_school = made.new_node_school;
    ruin.legacy = {made.legacy.school, made.legacy.intensity, made.legacy.basis};
    ruin.visitation = made.visitation;
    return ruin;
}

std::vector<VillainCity> villain_cities(const World& world) {
    std::vector<VillainCity> out;
    out.reserve(world.settlements.sites.size());
    for (const SettlementSite& s : world.settlements.sites) {
        VillainCity city;
        city.uid = s.uid;
        city.id = s.id;
        city.node = s.node;
        city.direction = s.direction;
        city.population_profile = s.population_profile;
        city.city_class = s.city_class;
        city.population_estimate = static_cast<double>(s.population_estimate);
        out.push_back(std::move(city));
    }
    return out;
}

std::vector<VillainThreatReport> threat_reports(const World& world) {
    std::vector<VillainThreatReport> out;
    for (const ThreatReport& t : world.threat_assessments.cities) {
        out.push_back({t.city_uid, t.regional_threat, t.outlook.war_risk, t.outlook.war_hunger, t.war_pressure,
                       t.nest_pressure, t.ley_pressure});
    }
    return out;
}

// The plans and the scene describe the cities a god is about to change.
void drop_plans(World& world) {
    world.city_plans = {};
    world.hamlet_plans = {};
    world.castle_plans = {};
    world.has_plans = false;
    world.has_rural_plans = false;
    world.world_scene = {};
    world.has_world_scene = false;
}

// The religion block carries the typed records as opaque JSON; rewrite them from the lists.
void sync_religion_records(World& world) {
    world.religion.visitations.clear();
    for (const VisitationRecord& v : world.visitations) world.religion.visitations.push_back(visitation_record_json(v));
    world.religion.corruptions.clear();
    for (const CorruptionRecord& c : world.corruptions) world.religion.corruptions.push_back(corruption_record_json(c));
}

void tidy(World& world) {
    std::set<std::string> seen;
    std::vector<std::string> systems;
    for (std::string& name : world.systems) {
        if (seen.insert(name).second) systems.push_back(std::move(name));
    }
    world.systems = std::move(systems);
    dedupe_warnings(world.warnings);
}

template <typename Error>
void validate_for(const World& world) {
    try {
        validate_age_world(world);
    } catch (const AgeAdvanceError& refused) {
        throw Error(refused.field(), refused.what());
    }
}

int current_age(const World& world) { return static_cast<int>(world.history.ages.size()); }

// The god goes home and the world rebuilds around the sites that stand. Shared by a cleanse
// aimed at a god and by a departure; only what each redraws afterwards differs.
VisitationDeparture send_home(World& out, const std::string& god_id, int age) {
    VisitationDeparture gone = depart_god({god_id, age, out.magic, out.religion, out.visitations});
    out.magic = gone.magic;
    out.religion = gone.religion;
    out.visitations = gone.visitations;
    const int departed = gone.record.departed_age.value_or(age);
    const std::vector<SettlementSite> standing = out.settlements.sites;
    rebuild_tail(out, standing, "departure", departed, /*evaluate=*/true, /*religion=*/false);
    sync_religion_records(out);
    resolve_world_religion(out);
    return gone;
}

void write_moon(JsonWriter& json, const DayState& moon) {
    json.begin_object();
    json.field("day", moon.day);
    json.field("phase_fraction", moon.phase_fraction);
    json.field("elongation_degrees", moon.elongation_degrees);
    json.field("illumination", moon.illumination);
    json.field("moonrise_hour", moon.moonrise_hour);
    json.field("spin_fraction", moon.spin_fraction);
    json.field("nod_degrees", moon.nod_degrees);
    json.key("leaning");
    json.value(std::string_view(moon.leaning));
    json.key("tide");
    json.begin_object();
    const auto schools = magic_school_table();
    for (std::size_t i = 0; i < moon.tide.size() && i < schools.size(); ++i) json.field(schools[i].name, moon.tide[i]);
    json.end_object();
    json.end_object();
}

void write_strings(JsonWriter& json, std::string_view key, const std::vector<std::string>& items) {
    json.key(key);
    json.value(items);
}

}  // namespace

std::string visitation_record_json(const VisitationRecord& r) {
    std::ostringstream text;
    JsonWriter json(text, false);
    json.begin_object();
    json.field("index", r.index);
    json.key("god_id");
    json.value(std::string_view(r.god_id));
    json.key("aspect");
    json.value(std::string_view(r.aspect));
    json.field("node", r.node);
    json.key("direction");
    json.begin_array();
    json.value(r.direction.x);
    json.value(r.direction.y);
    json.value(r.direction.z);
    json.end_array();
    json.field("age", r.age);
    json.field("day", r.day);
    json.field("wrath", r.wrath);
    json.field("variation", r.variation);
    write_strings(json, "schools", r.schools);
    json.field("reach_m", r.reach_m);
    write_strings(json, "cluster", r.cluster);
    json.key("restore");
    json.begin_array();
    for (const VisitationRestore& item : r.restore) {
        json.begin_object();
        json.key("school");
        json.value(std::string_view(item.school));
        if (!item.node_id.empty()) {
            json.key("node_id");
            json.value(std::string_view(item.node_id));
        } else {
            json.key("line_id");
            json.value(std::string_view(item.line_id));
        }
        json.field("intensity", item.intensity);
        json.end_object();
    }
    json.end_array();
    write_strings(json, "ruins", r.ruins);
    write_strings(json, "purged_nests", r.purged_nests);
    json.key("favored");
    json.begin_array();
    for (const VisitationFavor& f : r.favored) {
        json.begin_object();
        json.key("city_uid");
        json.value(std::string_view(f.city_uid));
        json.field("strength", f.strength);
        json.end_object();
    }
    json.end_array();
    write_strings(json, "converted", r.converted);
    json.key("departed_age");
    if (r.departed_age) {
        json.value(*r.departed_age);
    } else {
        json.value_null();
    }
    json.key("moon");
    write_moon(json, r.moon);
    json.key("order");
    json.begin_array();
    for (const std::string_view step : visitation_order()) json.value(step);
    json.end_array();
    json.end_object();
    return text.str();
}

std::string corruption_record_json(const CorruptionRecord& r) {
    std::ostringstream text;
    JsonWriter json(text, false);
    json.begin_object();
    json.field("index", r.index);
    json.key("god_id");
    json.value(std::string_view(r.god_id));
    json.key("school");
    json.value(std::string_view(r.school));
    json.key("villain_uid");
    json.value(std::string_view(r.villain_uid));
    json.field("age", r.age);
    json.field("encounters", r.encounters);
    json.field("variation", r.variation);
    write_strings(json, "nodes", r.nodes);
    json.field("reach_m", r.reach_m);
    json.key("touched");
    json.begin_array();
    for (const CorruptionTouch& t : r.touched) {
        // The reference keys a touch on `uid`, and only a farmed city counts its dead.
        json.begin_object();
        json.key("uid");
        json.value(std::string_view(t.city_uid));
        json.key("effect");
        json.value(std::string_view(t.effect));
        if (t.effect == "farmed") json.field("people_lost", t.people_lost);
        json.end_object();
    }
    json.end_array();
    write_strings(json, "ruins", r.ruins);
    json.key("cleansed_age");
    if (r.cleansed_age) {
        json.value(*r.cleansed_age);
    } else {
        json.value_null();
    }
    json.key("order");
    json.begin_array();
    for (const std::string_view step : corruption_order()) json.value(step);
    json.end_array();
    // Not in the reference's record: the known gods the revelation roused, which the
    // pantheon's re-resolve would otherwise erase from their own records.
    write_strings(json, "roused", r.roused);
    json.end_object();
    return text.str();
}

CorruptWorldResult corrupt(const World& world, const CorruptWorldRequest& request) {
    validate_for<CorruptionError>(world);
    const int age = current_age(world);

    CorruptionRequest pass;
    pass.api_version = request.api_version;
    pass.seed = world.effective.seed;
    pass.age = age;
    pass.globe_radius_m = world.effective.globe_radius;
    pass.settlement_spacing_m = world.effective.settlement_spacing;
    pass.encounters = request.encounters;
    pass.variation = request.variation;
    pass.villain_uid = request.villain_uid;
    pass.cities = villain_cities(world);
    pass.existing_ruins = static_cast<int>(world.ruins.size());
    for (const NestSite& n : world.beast_nests.sites) pass.nest_families.emplace_back(n.family);
    pass.magic = world.magic;
    pass.religion = world.religion;
    pass.villains = world.villains;
    pass.corruptions = world.corruptions;
    const CorruptionOutcome outcome = corrupt_world(pass);

    CorruptWorldResult result{world, {}};
    CorruptionReport& report = result.report;
    report.acted = outcome.acted;
    report.encounters = outcome.encounters;
    if (!outcome.acted) {
        // Nothing is watching, or nothing bit. The world comes back untouched and says so.
        report.watching = outcome.watching;
        report.reason = outcome.reason;
        return result;
    }
    report.god_id = outcome.god_id;
    report.villain_uid = outcome.villain_uid;
    report.corruption = outcome.corruption;

    World& out = result.world;
    for (const CorruptionRuin& made : outcome.new_ruins) out.ruins.push_back(ruin_of(made, world.settlements.sites));
    out.magic = outcome.magic;
    out.villains = outcome.villains;
    out.has_villains = true;
    out.corruptions = outcome.corruptions;

    // The rebuild over the cities rot and void spared.
    std::vector<SettlementSite> survivors;
    for (const VillainCity& city : outcome.survivors) {
        if (const SettlementSite* site = site_named(world.settlements.sites, city.uid)) survivors.push_back(*site);
    }
    drop_plans(out);
    rebuild_tail(out, survivors, "corruption", age, /*evaluate=*/true, /*religion=*/false);

    // A city blood farmed or eldritch took is held by the god's school (ruled 2026-09-23),
    // marked on the rebuilt site; the reference's rebuild erased both effects. What each lost
    // stays in the record's `touched` list.
    std::vector<std::pair<std::string, CityCorruption>> held;
    for (const CorruptionRecord& record : out.corruptions) {
        if (record.index != outcome.corruption) continue;
        for (const CorruptionTouch& touch : record.touched) {
            if (touch.effect == "farmed" || touch.effect == "taken") {
                held.emplace_back(touch.city_uid, CityCorruption{record.school, touch.effect, age, record.index});
            }
        }
    }
    mark_corrupted(out, held);

    // The pantheon re-resolves over the pass's religion, then the god is revealed walking --
    // after the resolve, precisely so the resolve cannot overwrite it.
    out.religion = outcome.religion;
    sync_religion_records(out);
    resolve_world_religion(out);
    reveal_hidden_god(out.religion, outcome.god_id, outcome.villain_uid, age);

    out.history.operations.push_back(CorruptionOperation{kCorruptionApiVersion, outcome.corruption, outcome.god_id,
                                                         outcome.villain_uid, request.encounters, request.variation});
    tidy(out);
    return result;
}

CleanseWorldResult cleanse(const World& world, const CleanseWorldRequest& request) {
    if (request.api_version != kCorruptionApiVersion) {
        throw CorruptionError("api_version", "this build serves corruption API version 1");
    }
    validate_for<CorruptionError>(world);
    if (request.corruption.has_value() == request.god_id.has_value()) {
        throw CorruptionError("target", "target names exactly one thing, as {\"corruption\": <index>} or "
                                        "{\"god_id\": ...}.");
    }
    if (!(request.power > 0.0 && request.power <= 1.0)) {
        throw CorruptionError("power", "a share of the structure drained, in (0, 1]");
    }
    const int age = current_age(world);
    CleanseWorldResult result{world, {}};
    World& out = result.world;
    CleanseOperation& op = result.report;
    op.api_version = kCorruptionApiVersion;
    op.power = request.power;

    if (request.god_id) {
        const std::string& god_id = *request.god_id;
        const auto god = std::find_if(world.religion.gods.begin(), world.religion.gods.end(),
                                      [&](const GodRecord& g) { return g.id == god_id; });
        if (god == world.religion.gods.end()) throw CorruptionError("target.god_id", god_id + " is no god of this world");
        if (god->status != "walking") {
            throw CorruptionError("target.god_id",
                                  "Only a god that walks the world can be opposed; " + god_id + " is " + god->status + ".");
        }
        const bool by_visitation =
            std::any_of(world.visitations.begin(), world.visitations.end(), [&](const VisitationRecord& v) {
                return v.god_id == god_id && !v.departed_age.has_value();
            });
        if (!by_visitation) {
            throw CorruptionError("target.god_id", "A god revealed by corruption is opposed through its corruption "
                                                   "rather than sent home; target {\"corruption\": <index>} instead.");
        }
        // The plans stand: the reference's cleanse sends the god home and redraws nothing.
        send_home(out, god_id, age);
        op.kind = "god";
        op.target_god_id = god_id;
        op.god_id = god_id;
        op.departed = true;
        out.history.operations.push_back(op);
        tidy(out);
        return result;
    }

    CleanseRequest pass;
    pass.api_version = request.api_version;
    pass.age = age;
    pass.power = request.power;
    pass.corruption = request.corruption;
    pass.magic = world.magic;
    pass.religion = world.religion;
    pass.villains = world.villains;
    pass.corruptions = world.corruptions;
    const CleanseOutcome drained = cleanse_world(pass);
    out.magic = drained.magic;
    out.religion = drained.religion;
    out.villains = drained.villains;
    out.corruptions = drained.corruptions;
    // Nobody died, but the ley field moved: the world rebuilds over its standing sites. The
    // pantheon is not re-resolved, as the reference does not.
    const std::vector<SettlementSite> standing = out.settlements.sites;
    drop_plans(out);
    rebuild_tail(out, standing, "cleanse", age, /*evaluate=*/true, /*religion=*/false);
    sync_religion_records(out);

    op.kind = "corruption";
    op.target_corruption = request.corruption;
    op.corruption = drained.corruption;
    op.god_id = drained.god_id;
    op.removed = drained.removed;
    op.remaining = drained.remaining;
    op.cleansed = drained.cleansed;
    out.history.operations.push_back(op);
    tidy(out);
    return result;
}

World summon(const World& world, const SummonRequest& request) {
    const int age = current_age(world);
    const auto held = sphere_grid(world.effective.size, world.effective.globe_radius);
    const double threshold = world.magic.mutation_threshold;
    const double margin = world.magic.dominance_margin;

    VisitationRequest pass;
    pass.api_version = request.api_version;
    pass.seed = world.effective.seed;
    pass.age = age;
    pass.globe_radius_m = world.effective.globe_radius;
    pass.settlement_spacing_m = world.effective.settlement_spacing;
    pass.grid = held.get();
    pass.layers = &world.layers;
    pass.moon = world.moon;
    pass.founding_end_year = world.settlements.founding.end_year;
    pass.god_id = request.god_id;
    pass.target_city_uid = request.target_city_uid;
    pass.target_node = request.target_node;
    pass.wrath = request.wrath;
    pass.variation = request.variation;
    pass.day = request.day;
    pass.magic = world.magic;
    pass.religion = world.religion;
    pass.visitations = world.visitations;
    pass.cities = world.settlements.sites;
    pass.nests = world.beast_nests.sites;
    // The fall reasons of the known schools; a hidden school owning the ground is refused by
    // name inside the pass, as it has none.
    for (const MagicSchool& school : magic_school_table()) {
        if (school.hidden) continue;
        pass.ley_destruction.emplace(std::string(school.name), std::string(ley_destruction_reason(school.name)));
    }
    // The ruins module owns a ruin's scar; the visitation asks it rather than keeping a copy.
    pass.ruin_legacy = [threshold, margin](const VisitationLegacyQuery& q) {
        RuinSources sources;
        sources.nest_family = std::string(q.nest_family);
        sources.god_school = std::string(q.god_school);
        const std::vector<double> potencies(q.potencies.begin(), q.potencies.end());
        const RuinLegacy legacy = ruin_legacy(q.cause, potencies, q.city->population_profile, q.city->city_class,
                                              sources, threshold, margin);
        return VisitationLegacy{legacy.school, legacy.intensity, legacy.basis};
    };
    const VisitationSummons summons = summon_god(pass);

    World out = world;
    for (const VisitationRuin& made : summons.new_ruins) out.ruins.push_back(ruin_of(made, world.settlements.sites));
    out.magic = summons.magic;
    drop_plans(out);
    rebuild_tail(out, summons.survivors, "visitation", age, /*evaluate=*/true, /*religion=*/false);

    VisitationRebuilt rebuilt;
    rebuilt.cities = out.settlements.sites;
    rebuilt.nests = out.beast_nests.sites;
    rebuilt.reports = threat_reports(out);
    const VisitationOutcome done = complete_summons(summons, rebuilt);

    out.beast_nests.sites = done.nests;
    for (const VisitationRelief& relief : done.relief) {
        for (ThreatReport& report : out.threat_assessments.cities) {
            if (report.city_uid != relief.city_uid) continue;
            report.divine_favor = relief.divine_favor;
            report.regional_threat = relief.regional_threat;
        }
    }
    out.visitations = done.visitations;
    out.religion = done.religion;
    sync_religion_records(out);
    resolve_world_religion(out);
    out.history.operations.push_back(done.operation);
    redraw_plans(out);
    tidy(out);
    return out;
}

World depart(const World& world, const std::string& god_id) {
    const auto god = std::find_if(world.religion.gods.begin(), world.religion.gods.end(),
                                  [&](const GodRecord& g) { return g.id == god_id; });
    if (god == world.religion.gods.end()) throw VisitationError("god_id", god_id + " is no god of this world");
    World out = world;
    drop_plans(out);
    const VisitationDeparture gone = send_home(out, god_id, current_age(world));
    out.history.operations.push_back(gone.operation);
    redraw_plans(out);
    tidy(out);
    return out;
}

}  // namespace fwg
