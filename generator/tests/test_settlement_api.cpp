// Founding a settlement the player chose (settlement_api.hpp).
//
// The properties that matter are the ones a game depends on mid-session: the founding adds
// exactly the player's settlement and moves nothing else's identity; every illegal choice is
// refused naming the field to change and why; the caller's world is untouched and the call is
// a function of its request; and the world keeps working around the new settlement -- its
// class is derived, its plan exists, it survives a rebuild with its layout, and the world
// still advances an age.
//
// Legal nodes are FOUND, by asking the real call, never hardcoded or re-derived here: the
// legal set depends on the seed, the size and the civilization, and a restated predicate
// agrees with the one it restates in the test that was supposed to catch it.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "fwg/entities.hpp"
#include "fwg/request_error.hpp"
#include "fwg/serialize.hpp"
#include "fwg/settlement_api.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

// The entries of one kind in the world's one operation log, in call order.
template <typename Op>
std::vector<const Op*> ops_in(const World& w) {
    std::vector<const Op*> out;
    for (const HistoryOperation& entry : w.history.operations) {
        if (const auto* op = std::get_if<Op>(&entry)) out.push_back(op);
    }
    return out;
}

template <typename Op>
std::size_t count_ops(const World& w) {
    return ops_in<Op>(w).size();
}

template <typename Op>
const Op& first_op(const World& w) {
    const auto ops = ops_in<Op>(w);
    if (ops.empty()) check::fail("the log to hold an entry of this kind");
    return *ops.front();
}

template <typename Op>
const Op& last_op(const World& w) {
    const auto ops = ops_in<Op>(w);
    if (ops.empty()) check::fail("the log to hold an entry of this kind");
    return *ops.back();
}

World at(int phase, int size = 33, const char* profile = nullptr) {
    Request r;
    r.seed = 42;
    r.size = size;
    r.phase = phase;
    if (profile != nullptr) r.population_profile = std::string(profile);
    return generate(resolve(r));
}

// Generated once per run: every test below reads it and none may change it.
const World& genesis() {
    static const World w = at(16);
    return w;
}
const World& rural_world() {
    static const World w = at(12);
    return w;
}

std::string document(const World& w) {
    std::ostringstream out;
    write_world_json(out, w, /*pretty=*/false, /*include_timings=*/false);
    return out.str();
}

template <typename Call>
std::optional<RequestRefusal> refusal_of(Call&& call) {
    try {
        call();
    } catch (const RequestError& refused) {
        return refused.refusal();
    }
    return std::nullopt;
}

RequestRefusal refused(const World& w, const FoundSettlementRequest& request, const std::string& what) {
    const std::optional<RequestRefusal> r = refusal_of([&] { (void)found_settlement(w, request); });
    check::is_true(r.has_value(), what + " to be refused");
    check::is_true(!r->message.empty(), what + " to say why");
    return *r;
}

FoundSettlementRequest asking(int node, const std::string& civilization, const std::string& kind = "city") {
    FoundSettlementRequest r;
    r.node = node;
    r.civilization_id = civilization;
    r.kind = kind;
    return r;
}

std::string first_people(const World& w) { return w.settlements.sites.at(0).population_profile; }

struct Founded {
    int node = -1;
    World world;
};

// The first node, in node order, the real call accepts. Every node it passes over must have
// been refused by the WORLD -- a malformed-request refusal here would mean the template, not
// the ground, was wrong.
Founded first_legal(const World& w, FoundSettlementRequest request) {
    const auto held = sphere_grid(w.config.size, w.effective.globe_radius);
    for (std::size_t node = 0; node < held->node_count(); ++node) {
        request.node = static_cast<int>(node);
        try {
            return {static_cast<int>(node), found_settlement(w, request)};
        } catch (const RequestError& refused) {
            check::is_true(refused.refusal().refused_by_world && refused.field() == "node",
                           "node " + std::to_string(node) + " refused by the ground, not the request: " +
                               refused.what());
        }
    }
    check::fail("no node in this world accepts a " + request.kind);
}

const Founded& founded_city() {
    static const Founded f = [] {
        FoundSettlementRequest r = asking(0, first_people(genesis()));
        r.name = "Player Town";
        r.population_estimate = 400;
        return first_legal(genesis(), r);
    }();
    return f;
}

const SettlementSite* site_with(const World& w, const std::string& uid) {
    for (const SettlementSite& s : w.settlements.sites) {
        if (s.uid == uid) return &s;
    }
    return nullptr;
}

int node_of(const World& w, int x, int z) {
    const auto held = sphere_grid(w.config.size, w.effective.globe_radius);
    return held->node_index(x, z);
}

// Set one layer at one node, in a copy.
void set_at(World& w, const std::string& layer, int node, double value) {
    const auto held = sphere_grid(w.config.size, w.effective.globe_radius);
    const auto n = static_cast<std::size_t>(node);
    if (w.layers.count(layer) == 0) w.layers[layer] = Grid(w.config.size, 0.0);
    w.layers[layer].at(held->point_x[n], held->point_z[n]) = value;
}

}  // namespace

// --- a legal founding -------------------------------------------------------------

TEST(a_legal_founding_adds_exactly_the_players_city_keyed_on_its_node) {
    const World& before = genesis();
    const Founded& f = founded_city();
    const World& after = f.world;
    const std::string people = first_people(before);

    check::equal(after.settlements.sites.size(), before.settlements.sites.size() + 1, "exactly one more city");
    const SettlementSite& site = after.settlements.sites.back();
    check::equal(site.uid, "surface-city-2-" + std::to_string(f.node) + "-" + people,
                 "the generated uid rule verbatim, at the world's age and the chosen node");
    check::equal(site.node, f.node, "standing on the node asked for");
    check::equal(node_of(after, site.x, site.z), f.node, "its cell is that node's");
    check::equal(site.founded_by, std::string("player"), "marked as the player's");
    check::equal(site.name, std::string("Player Town"), "named by the player");
    check::equal(site.population_estimate, 400LL, "sized as asked");
    check::equal(site.founded_age, 2, "founded in the world's age");
    check::equal(site.source_culture, people + "-player-" + std::to_string(f.node), "its founders named");

    // Every other standing city keeps its uid, and its position, because the new one is
    // appended: nothing that holds an old city's id or uid is left pointing elsewhere.
    for (std::size_t i = 0; i < before.settlements.sites.size(); ++i) {
        check::equal(after.settlements.sites[i].uid, before.settlements.sites[i].uid, "city " + std::to_string(i) + " keeps its uid");
    }
    for (std::size_t i = 0; i < after.settlements.sites.size(); ++i) {
        check::equal(after.settlements.sites[i].id, static_cast<int>(i), "every id is its index");
    }
    check::is_true(after.settlements.founded_by_player == std::vector<std::string>{site.uid},
                   "the world lists the player's cities");
    check::is_true(after.settlement_founding.has_value(), "the report stamped on the world");
    check::equal(after.settlement_founding->uid, site.uid, "the report names the city");
    check::equal(after.settlement_founding->kind, std::string("city"), "and its kind");
}

TEST(the_player_city_reads_the_ground_it_stands_on) {
    // Seven fields are exact layer reads, so a player town carries the numbers a generated
    // one would have carried on the same ground rather than plausible invented ones.
    const World& before = genesis();
    const SettlementSite& site = founded_city().world.settlements.sites.back();
    const std::string people = first_people(before);
    const auto layer = [&](const char* name) { return before.layers.at(name).at(site.x, site.z); };
    check::equal(site.height_m, layer("height"), "height");
    check::equal(site.slope_degrees, layer("slope"), "slope");
    check::equal(site.temperature_c, layer("temperature"), "temperature");
    check::equal(site.moisture, layer("moisture"), "moisture");
    check::equal(site.flood_risk, layer("flood_risk"), "flood risk");
    check::equal(site.resource_potential, layer("resource_potential"), "resource potential");
    check::equal(site.suitability, before.layers.at("suitability_" + people).at(site.x, site.z), "its people's suitability");
}

namespace {

// A city for a people the world carries but has not settled, and whose id is NOT its parent
// race's: its first city must rank as its capital, and only such a people can tell a parent
// race from a civilization id.
const Founded& newcomer_city() {
    static const Founded f = [] {
        const World& before = genesis();
        std::set<std::string> settled;
        for (const SettlementSite& s : before.settlements.sites) settled.insert(s.population_profile);
        std::string people;
        for (const std::string_view id : civilization_ids()) {
            const EntityRules* rules = entity_rule(id);
            if (settled.count(std::string(id)) == 0 && rules != nullptr && rules->parent_race_id != id &&
                before.layers.count("suitability_" + std::string(id)) != 0) {
                people = std::string(id);
                break;
            }
        }
        check::is_true(!people.empty(), "a people carried, unsettled, and not named for its parent race");
        return first_legal(before, asking(0, people));
    }();
    return f;
}

}  // namespace

TEST(the_player_city_names_its_peoples_parent_race_and_the_cast_survives_it) {
    // The reference wrote the civilization id here. The stage-16 redraw resolves a heritage
    // from this field, and a heritage refuses a parent race that does not exist: measured, the
    // hero pass then fails whole -- status failed, every hero gone.
    const World& w = newcomer_city().world;
    const SettlementSite& site = w.settlements.sites.back();
    check::equal(site.parent_race_id, std::string(entity_rule(site.population_profile)->parent_race_id),
                 "the parent race, not the civilization");
    check::equal(w.heroes.status, std::string("ok"), "the cast redrawn around the city: " + w.heroes.error);
    check::is_true(!w.heroes.people.empty(), "with heroes in it");
}

TEST(the_class_is_derived_over_the_whole_list_and_a_move_is_reported) {
    const World& before = genesis();
    const World& after = founded_city().world;
    std::vector<SettlementSite> reranked = after.settlements.sites;
    classify_settlements(reranked);
    for (std::size_t i = 0; i < reranked.size(); ++i) {
        check::is_true(reranked[i].city_class == after.settlements.sites[i].city_class,
                       after.settlements.sites[i].uid + " to hold the class the whole list derives");
    }
    std::vector<std::string> moved;
    for (std::size_t i = 0; i < before.settlements.sites.size(); ++i) {
        if (before.settlements.sites[i].city_class != after.settlements.sites[i].city_class) {
            moved.push_back(before.settlements.sites[i].uid);
        }
    }
    std::sort(moved.begin(), moved.end());
    check::is_true(after.settlement_founding->reclassified_cities == moved, "every demoted incumbent reported, and no other");
    check::is_true(after.settlement_founding->city_class == after.settlements.sites.back().city_class,
                   "the report carries the new city's class");

    // A people's first city is its capital: the rank is derived, not a default.
    const World& first = newcomer_city().world;
    check::is_true(first.settlements.sites.back().city_class == CityClass::Capital,
                   first.settlements.sites.back().population_profile + "'s first city its capital");
    check::is_true(first.settlement_founding->city_class == CityClass::Capital, "and reported so");
}

TEST(a_founding_leaves_the_callers_world_alone_and_repeats_exactly) {
    const World& w = genesis();
    const std::string before = document(w);
    FoundSettlementRequest r = asking(founded_city().node, first_people(w));
    r.name = "Player Town";
    r.population_estimate = 400;
    const World again = found_settlement(w, r);
    check::is_true(document(w) == before, "the caller's world untouched");
    check::is_true((count_ops<SettlementOperation>(w) == 0) && !w.settlement_founding, "and nothing recorded on it");
    check::is_true(document(again) == document(founded_city().world), "the same request, the same world");
    check::is_true(again.settlement_founding->notes == founded_city().world.settlement_founding->notes, "the same report");
    check::equal(again.settlements.sites.back().uid, founded_city().world.settlements.sites.back().uid, "the same city");
}

TEST(stage_sixteen_is_redrawn_around_the_new_city) {
    const World& w = founded_city().world;
    const std::string uid = w.settlements.sites.back().uid;
    check::is_true(w.has_plans && w.has_world_scene, "the plans and the scene redrawn");
    check::equal(w.city_plans.cities.size(), w.settlements.sites.size(), "one plan per city, the new one included");
    const auto plan = std::find_if(w.city_plans.cities.begin(), w.city_plans.cities.end(),
                                   [&](const CityPlan& p) { return p.city_uid == uid; });
    check::is_true(plan != w.city_plans.cities.end(), "a plan for the player's city");
    // The player owns where the buildings go, so the planner packs nothing and says why.
    check::is_true(plan->authored_by_player, "the plan marked as the player's layout");
    check::equal(std::string(plan->status), std::string("unbuildable"), "a status the planner already emits");
    check::is_true(plan->plots.empty(), "no building placed for the player");
    check::is_true(!plan->unplaced.empty() && plan->unplaced.front().reason.find("player") != std::string::npos,
                   "the unplaced programme saying the player authored it");
    // Re-read before the redraw, so the new city has a threat and the planners read it.
    const bool assessed = std::any_of(w.threat_assessments.cities.begin(), w.threat_assessments.cities.end(),
                                      [&](const ThreatReport& t) { return t.city_uid == uid; });
    check::is_true(assessed, "the new city's threat assessed");
    check::equal(w.threat_assessments.cities.size(), w.settlements.sites.size(), "every city assessed");
    const auto& notes = w.settlement_founding->notes;
    check::is_true(std::any_of(notes.begin(), notes.end(),
                               [](const std::string& n) { return n.find("Stage 16 was redrawn") == 0; }),
                   "the report saying stage 16 was redrawn and what that costs");
    check::is_true(w.settlement_founding->dropped_blocks.empty(), "nothing dropped: redrawn instead");
}

TEST(a_redraw_reads_the_threat_once_rather_than_raising_it_again) {
    // Stage 16's raid pass adds onto whatever threat it finds. Redrawn over the threat the
    // last pass already raised, a town beside raiders would read the same raiders twice. So
    // every raided town must read exactly its fresh assessment plus its own raid pressure.
    const World& after = founded_city().world;
    World fresh = after;
    const std::string label = after.threat_assessments.evaluated_after;
    assess_world_threats(fresh, label, after.threat_assessments.age);
    std::size_t raided = 0;
    for (const ThreatReport& t : after.threat_assessments.cities) {
        if (t.nomad_contributors.empty()) continue;
        ++raided;
        const auto base = std::find_if(fresh.threat_assessments.cities.begin(), fresh.threat_assessments.cities.end(),
                                       [&](const ThreatReport& u) { return u.city_uid == t.city_uid; });
        check::is_true(base != fresh.threat_assessments.cities.end(), t.city_uid + " assessed");
        check::close(t.regional_threat, std::min(1.0, base->regional_threat + t.nomad_pressure.value_or(0.0)), 2e-6,
                     t.city_uid + " raised by its raiders once");
    }
    check::is_true(raided > 0, "some town beside raiders, or this tests nothing");
}

TEST(the_report_discloses_what_it_left_alone) {
    const SettlementFoundingReport& report = *founded_city().world.settlement_founding;
    check::is_true(!report.nests_rerolled, "the nests never re-rolled");
    const auto says = [&](const char* text) {
        return std::any_of(report.notes.begin(), report.notes.end(),
                           [&](const std::string& n) { return n.find(text) != std::string::npos; });
    };
    check::is_true(says("beast book was deliberately left alone"), "the nest note");
    check::is_true(says("rural layer was not rebuilt"), "the rural-layer note, rebuild being none");
    check::is_true(report.rural_ids_reassigned.hamlets.empty() && report.rural_ids_reassigned.fortresses.empty(),
                   "no rural id moved");
    check::equal(report.layout, std::string("player"), "the layout recorded");
    check::equal(report.rebuild, std::string("none"), "the rebuild recorded");
    check::equal(report.version, 2, "report version 2");
}

TEST(a_founding_is_recorded_as_the_reference_records_it) {
    const World& w = founded_city().world;
    check::equal(count_ops<SettlementOperation>(w), std::size_t{1}, "one operation");
    const SettlementOperation& op = first_op<SettlementOperation>(w);
    check::equal(op.api_version, 2, "api 2");
    check::equal(op.kind, std::string("founding"), "the operation's kind");
    check::equal(op.settlement_kind, std::string("city"), "the settlement's kind on its own axis");
    check::equal(op.node, founded_city().node, "the node");
    check::equal(op.civilization_id, first_people(genesis()), "the people");
    check::equal(op.uid, w.settlements.sites.back().uid, "the uid");
    check::equal(op.layout, std::string("player"), "the layout");
    check::equal(op.rebuild, std::string("none"), "the rebuild");
    check::is_true(op.name == std::optional<std::string>("Player Town"), "the name");
}

// --- the layout, and a rebuild around it -----------------------------------------------

TEST(an_authored_layout_is_the_players_and_a_generated_one_is_absent) {
    const SettlementSite& site = founded_city().world.settlements.sites.back();
    check::is_true(site.city_layout.has_value(), "layout player: an authored record");
    check::equal(site.city_layout->layout_profile_id, std::string("player"), "the player's profile");
    check::equal(site.city_layout->authored_by, std::string("player"), "authored by the player");
    check::equal(site.city_layout->population_estimate, 400LL, "sized as the site is");
    check::equal(site.city_layout->version, kCityLayoutVersion, "the layout version");
    check::is_true(!site.city_layout->building_pack_id && site.city_layout->required_assets.empty() &&
                       site.city_layout->features.empty(),
                   "no pack, asset or district claimed");

    // On a world with no stage 16, so no redraw: nothing here needs one.
    FoundSettlementRequest r = asking(0, first_people(rural_world()));
    r.layout = "generated";
    const World generated = first_legal(rural_world(), r).world;
    check::is_true(!generated.settlements.sites.back().city_layout.has_value(), "layout generated: no authored record");
    check::equal(generated.settlements.sites.back().founded_by, std::string("player"), "still the player's city");
    check::is_true(!generated.has_plans, "and no plans drawn on a world that had none");
}

TEST(a_settlement_rebuild_keeps_the_players_city_where_it_stands_with_its_layout) {
    // What an age turn does to a city that stood through it, without the fate lottery
    // deciding whether it did: re-found civilization around every standing city.
    World w = founded_city().world;
    const SettlementSite player = w.settlements.sites.back();
    SettlementRebuild rebuild;
    rebuild.age = w.age;
    rebuild.survivors = w.settlements.sites;
    for (const Ruin& ruin : w.ruins) rebuild.forbidden_nodes.push_back(ruin.node);
    rebuild.previous = w.settlements.founding;
    add_settlements(w, &rebuild);

    const SettlementSite* kept = site_with(w, player.uid);
    check::is_true(kept != nullptr, "the player's city still stands under its uid");
    check::equal(kept->node, player.node, "on its node");
    check::equal(kept->founded_by, std::string("player"), "still the player's");
    check::equal(kept->name, player.name, "with its name");
    check::equal(kept->source_culture, player.source_culture, "and its founders");
    check::is_true(kept->city_layout.has_value(), "its authored layout carried rather than re-derived");
    check::equal(kept->city_layout->layout_profile_id, std::string("player"), "still the player's layout");
    check::equal(kept->city_layout->population_estimate, kept->population_estimate,
                 "sized by the world at the rebuild: the world owns the metadata");

    // REFERENCE DEFECT, REPRODUCED: the rebuild writes a fresh settlement report and the
    // list of the player's cities goes with it, although the player's city stands.
    check::is_true(w.settlements.founded_by_player.empty(), "founded_by_player dropped by the rebuild");
}

TEST(the_founded_world_still_advances_an_age) {
    // Survival is not asserted: an age ruins most cities of a small world, and a player town
    // falling is attrition, not a founding defect. Either it stands as itself or it is a ruin
    // keyed on its uid.
    const World& w = founded_city().world;
    const std::string uid = w.settlements.sites.back().uid;
    AgeAdvanceRequest advance;
    const World later = advance_ages(w, advance);
    check::equal(later.history.ages.size(), w.history.ages.size() + 1, "one more age");
    const SettlementSite* kept = site_with(later, uid);
    const bool fell = std::any_of(later.ruins.begin(), later.ruins.end(), [&](const Ruin& r) { return r.uid == uid; });
    check::is_true((kept != nullptr) != fell, "the player's city either stands or lies in ruins, not both or neither");
    if (kept != nullptr) {
        check::equal(kept->founded_by, std::string("player"), "a survivor stays the player's");
        check::is_true(kept->city_layout.has_value(), "with its layout");
    }
}

// --- rural foundings ---------------------------------------------------------------

TEST(a_player_hamlet_lands_in_the_block_that_holds_hamlets_and_is_planned) {
    const World& before = genesis();
    FoundSettlementRequest r = asking(0, first_people(before), "hamlet");
    r.name = "Player Farmstead";
    r.role = "resource";
    const Founded f = first_legal(before, r);
    const World& w = f.world;
    std::vector<const RuralSite*> rows;
    for (const RuralSite& h : w.humans.hamlets) {
        if (h.node == f.node) rows.push_back(&h);
    }
    check::equal(rows.size(), std::size_t{1}, "exactly one hamlet on the node");
    const RuralSite& row = *rows.front();
    check::equal(row.kind, std::string("hamlet"), "a hamlet");
    check::equal(row.uid, "hamlet-node-" + std::to_string(f.node), "keyed on its node and kind");
    check::equal(row.id, "hamlet-" + std::to_string(before.humans.hamlets.size()), "an ordinal in the block's own space");
    check::equal(row.founded_by, std::string("player"), "the player's");
    check::equal(row.name, std::string("Player Farmstead"), "named");
    check::equal(row.role, std::string("resource"), "working what it was asked to");
    check::is_true(row.access_nodes == std::vector<int>{f.node}, "no measured approach path");
    check::equal(w.settlements.sites.size(), before.settlements.sites.size(), "no city added");
    const auto& cores = w.humans.cores;
    const bool listed = row.core_id >= 0 &&
                        std::find(cores.at(static_cast<std::size_t>(row.core_id)).hamlet_ids.begin(),
                                  cores.at(static_cast<std::size_t>(row.core_id)).hamlet_ids.end(),
                                  row.id) != cores.at(static_cast<std::size_t>(row.core_id)).hamlet_ids.end();
    check::is_true(listed, "its parent city's core lists it");
    check::equal(w.settlement_founding->uid, row.uid, "the report names its uid");
    check::is_true(!w.settlement_founding->city_class.has_value(), "and no class");
    check::equal(first_op<SettlementOperation>(w).settlement_kind, std::string("hamlet"), "recorded as a hamlet");
    const bool planned = std::any_of(w.hamlet_plans.hamlets.begin(), w.hamlet_plans.hamlets.end(),
                                     [&](const HamletPlan& p) { return p.hamlet_uid == row.uid; });
    check::is_true(planned, "a hamlet plan for it after the redraw");
    const auto& notes = w.settlement_founding->notes;
    check::is_true(!notes.empty() && notes.front().find("do not hold it") != std::string::npos,
                   "the report warning that its ordinal renumbers");

    // Every age turn re-runs stage 12 over the world, and the player's hamlet has to come
    // through it re-seated rather than dropped: the pass reads the rows the world carries.
    World turned = w;
    add_populated_regions(turned);
    const auto kept = std::find_if(turned.humans.hamlets.begin(), turned.humans.hamlets.end(),
                                   [&](const RuralSite& h) { return h.uid == row.uid; });
    check::is_true(kept != turned.humans.hamlets.end(), "the hamlet through a stage-12 rebuild");
    check::equal(kept->founded_by, std::string("player"), "still the player's");
    check::equal(kept->name, std::string("Player Farmstead"), "with its name");
    check::equal(kept->role, std::string("resource"), "and its work");
}

TEST(a_rural_rows_access_cost_is_great_circle_metres_to_its_city) {
    // REFERENCE DEFECT, REPRODUCED: a generated row's access_cost is a road cost; a player's
    // is the great-circle distance to the nearest city, because no search stood behind it.
    const World& before = rural_world();
    const Founded f = first_legal(before, asking(0, first_people(before), "fortress"));
    const RuralSite* row = nullptr;
    for (const RuralSite& fort : f.world.humans.fortresses) {
        if (fort.node == f.node) row = &fort;
    }
    check::is_true(row != nullptr, "the player's keep in the fortress block");
    const Vec3 here = direction(row->x, row->z, before.config.size);
    double nearest = 1e300;
    int core = -1;
    for (std::size_t i = 0; i < before.settlements.sites.size(); ++i) {
        const double d = great_circle_m(before.effective.globe_radius, here, before.settlements.sites[i].direction);
        if (d < nearest) {
            nearest = d;
            core = static_cast<int>(i);
        }
    }
    check::equal(row->core_id, core, "answering to the nearest city");
    check::equal(row->access_cost, nearest, "metres, not a road cost");
    check::equal(row->protected_route_node, -1, "no route it protects");
    const auto& ids = f.world.humans.cores.at(static_cast<std::size_t>(core)).fortress_ids;
    check::is_true(std::find(ids.begin(), ids.end(), row->id) != ids.end(), "its city's core lists it");
}

TEST(the_players_fortress_survives_the_rural_rebuild) {
    // rebuild "humans" re-derives the hinterland from scratch. A keep the player raised is an
    // instruction, not a proposal: it is re-seated, never dropped and never built over.
    const World& before = genesis();
    FoundSettlementRequest r = asking(0, first_people(before), "fortress");
    r.name = "Player Keep";
    r.rebuild = "humans";
    const Founded f = first_legal(before, r);
    const World& w = f.world;
    std::vector<const RuralSite*> keeps;
    for (const RuralSite& fort : w.humans.fortresses) {
        if (fort.node == f.node) keeps.push_back(&fort);
    }
    check::equal(keeps.size(), std::size_t{1}, "the keep survives the rebuild, once");
    check::equal(keeps.front()->uid, "fortress-node-" + std::to_string(f.node), "under its uid");
    check::equal(keeps.front()->founded_by, std::string("player"), "still the player's");
    check::equal(keeps.front()->name, std::string("Player Keep"), "with its name");
    std::set<int> nodes;
    for (const RuralSite& h : w.humans.hamlets) check::is_true(nodes.insert(h.node).second, "no two rural sites on a node");
    for (const RuralSite& k : w.humans.fortresses) check::is_true(nodes.insert(k.node).second, "no two rural sites on a node");
    for (const SettlementSite& s : w.settlements.sites) check::is_true(nodes.count(s.node) == 0, "no rural site on a city");
    const SettlementFoundingReport& report = *w.settlement_founding;
    check::equal(report.rebuild, std::string("humans"), "the rebuild recorded");
    check::is_true(report.dropped_blocks.empty(), "the plans redrawn, not dropped");
    const bool planned = std::any_of(w.castle_plans.castles.begin(), w.castle_plans.castles.end(),
                                     [&](const CastlePlan& p) { return p.fortress_uid == keeps.front()->uid; });
    check::is_true(planned, "the keep planned in the redrawn castle block");
    // The harbours society folded into the rural layer drop out of it: the rebuild re-runs
    // the hinterland alone, as the reference's does. Reproduced.
    const auto harbour = [](const RuralSite& h) { return h.id.rfind("coastal-", 0) == 0; };
    const bool had_harbour = std::any_of(before.humans.hamlets.begin(), before.humans.hamlets.end(), harbour);
    const bool has_harbour = std::any_of(w.humans.hamlets.begin(), w.humans.hamlets.end(), harbour);
    check::is_true(had_harbour && !has_harbour, "harbours in the rural layer before, and out of it after");
    // ...and with the redraw, out of the hamlet plans too, until a full stage-12 pass returns them.
    const auto harbour_plan = [](const HamletPlan& p) { return p.hamlet_id.rfind("coastal-", 0) == 0; };
    check::is_true(std::any_of(before.hamlet_plans.hamlets.begin(), before.hamlet_plans.hamlets.end(), harbour_plan) &&
                       std::none_of(w.hamlet_plans.hamlets.begin(), w.hamlet_plans.hamlets.end(), harbour_plan),
                   "harbour plans before, and none after the redraw");
}

// --- refusals: every illegal choice names its field and says why -------------------------

TEST(a_malformed_request_is_refused_naming_the_argument) {
    const World& w = rural_world();
    const std::string people = first_people(w);
    const int node = w.settlements.sites.at(0).node;
    const auto with = [&](auto change) {
        FoundSettlementRequest r = asking(node, people);
        change(r);
        return r;
    };

    RequestRefusal r = refused(w, with([](FoundSettlementRequest& q) { q.api_version = 1; }), "api 1");
    check::equal(r.field, std::string("api_version"), "api field");
    check::equal(std::string(r.code), std::string("UNSUPPORTED_VERSION"), "a protocol refusal");
    check::is_true(!r.retry, "not retryable with a value");

    r = refused(w, with([](FoundSettlementRequest& q) { q.node.reset(); }), "no node");
    check::equal(r.field, std::string("node"), "node field");
    check::equal(std::string(r.kind), std::string("wrong_type"), "a missing node is a wrong type");

    r = refused(w, with([](FoundSettlementRequest& q) { q.node = -1; }), "a negative node");
    check::equal(r.field, std::string("node"), "node field");
    check::equal(r.message, std::string("node must be 0..None grid index; received -1."), "the reference's words");
    check::equal(std::string(r.suggestion), std::string("clamp"), "a clamp suggestion");
    check::is_true(r.suggestion_value == RefusalValue(0LL), "to zero");

    r = refused(w, with([](FoundSettlementRequest& q) { q.kind = "village"; }), "a village");
    check::equal(r.field, std::string("kind"), "kind field");
    check::equal(std::string(r.kind), std::string("invalid_choice"), "refused by name, not given a city");
    check::equal(r.choices.size(), std::size_t{3}, "the three kinds listed");

    r = refused(w, with([](FoundSettlementRequest& q) { q.role = "fishing"; }), "a role");
    check::equal(r.field, std::string("role"), "role field");

    r = refused(w, with([](FoundSettlementRequest& q) { q.civilization_id.reset(); }), "no people");
    check::equal(r.field, std::string("civilization_id"), "civilization field");
    check::equal(std::string(r.kind), std::string("wrong_type"), "a missing people is a wrong type");

    r = refused(w, with([](FoundSettlementRequest& q) { q.name = "   "; }), "a blank name");
    check::equal(r.field, std::string("name"), "name field");

    r = refused(w, with([](FoundSettlementRequest& q) { q.layout = "blueprint"; }), "a layout");
    check::equal(r.field, std::string("layout"), "layout field");

    r = refused(w, with([](FoundSettlementRequest& q) { q.rebuild = "everything"; }), "a rebuild mode");
    check::equal(r.field, std::string("rebuild"), "rebuild field");

    r = refused(w, with([](FoundSettlementRequest& q) { q.population_estimate = -5; }), "negative residents");
    check::equal(r.field, std::string("population_estimate"), "population field");
    check::equal(std::string(r.suggestion), std::string("clamp"), "clamped");
    // The remedy is a better value, not a different node: a caller that cannot tell the two
    // apart keeps correcting an argument that was never the problem.
    check::is_true(!r.refused_by_world, "an argument refusal is not the world's");
}

TEST(a_world_that_cannot_hold_the_kind_is_refused_naming_the_block) {
    const World nine = at(9);
    RequestRefusal r = refused(nine, asking(0, "elf"), "a world with no cities");
    check::equal(r.field, std::string("world.settlements"), "the missing block");
    check::is_true(!r.retry, "fixed by generating further, not by a value");

    const World eleven = at(11);
    r = refused(eleven, asking(0, first_people(eleven), "hamlet"), "a hamlet on a world with no rural layer");
    check::equal(r.field, std::string("world.humans"), "the missing rural layer");

    World empty = rural_world();
    empty.settlements.sites.clear();
    r = refused(empty, asking(0, "elf", "fortress"), "a keep with no city to answer to");
    check::equal(r.field, std::string("world.settlements.sites"), "the missing parent");
}

TEST(an_unknown_civilization_is_refused_with_what_was_meant) {
    // REFERENCE DEFECT, REPRODUCED: the catalogue refusal names the refused VALUE as its
    // field, so the caller is told to change "dwarfs" rather than civilization_id.
    const World& w = rural_world();
    const RequestRefusal r = refused(w, asking(w.settlements.sites.at(0).node, "dwarfs"), "a misspelt people");
    check::equal(r.field, std::string("dwarfs"), "the value named as the field");
    check::equal(std::string(r.kind), std::string("unknown_field"), "a catalogue refusal");
    check::equal(std::string(r.suggestion), std::string("did_you_mean"), "with a guess");
    check::is_true(!r.suggestion_candidates.empty() && r.suggestion_candidates.front() == RefusalValue(std::string("dwarf")),
                   "and the guess is dwarf");
}

TEST(a_people_the_world_was_not_generated_with_is_refused_offering_the_one_it_was) {
    const World dwarves = at(10, 33, "dwarf");
    const RequestRefusal r = refused(dwarves, asking(dwarves.settlements.sites.at(0).node, "elf"), "elves on a dwarf world");
    check::equal(r.field, std::string("civilization_id"), "the people named");
    check::is_true(r.refused_by_world, "the world's refusal: it carries no suitability for them");
    check::is_true(r.suggestion_value == RefusalValue(std::string("dwarf")), "offering the people it was built for");
}

TEST(ground_the_world_will_not_settle_is_refused_naming_the_node) {
    const World& w = rural_world();
    const std::string people = first_people(w);
    const auto held = sphere_grid(w.config.size, w.effective.globe_radius);
    const auto ground_refusal = [&](const World& world, int node, const std::string& kind, const std::string& reason,
                                    const std::string& what) {
        const RequestRefusal r = refused(world, asking(node, people, kind), what);
        check::equal(r.field, std::string("node"), what + ": the node named");
        check::is_true(r.refused_by_world, what + ": the world's refusal, not the argument's");
        check::is_true(r.received == RefusalValue(static_cast<long long>(node)), what + ": the node it was sent");
        check::is_true(r.message.find(reason) != std::string::npos, what + ": saying '" + reason + "', said: " + r.message);
    };

    ground_refusal(w, static_cast<int>(held->node_count()), "city", "outside this world", "past the grid");

    const CivilizationProfile& profile = *civilization_profile(people);
    int water = -1;
    int ice = -1;
    int open = -1;  // dry, unfrozen, gentle, calm and unclaimed: nothing but the tweak refuses it
    std::set<int> taken;
    for (const SettlementSite& s : w.settlements.sites) taken.insert(s.node);
    for (const RuralSite& h : w.humans.hamlets) taken.insert(h.node);
    for (const RuralSite& f : w.humans.fortresses) taken.insert(f.node);
    for (std::size_t i = 0; i < held->node_count(); ++i) {
        const int x = held->point_x[i];
        const int z = held->point_z[i];
        const bool wet = w.layers.at("water_type").at(x, z) != 0.0;
        const bool frozen = static_cast<int>(w.layers.at("natural_biome").at(x, z)) == 17;
        const bool gentle = w.layers.at("slope").at(x, z) < profile.traits.site_slope_limit;
        const bool calm = w.layers.at("magic_hazard").at(x, z) <= profile.traits.mutation_limit;
        if (wet && water < 0) water = static_cast<int>(i);
        if (!wet && frozen && ice < 0) ice = static_cast<int>(i);
        if (!wet && !frozen && gentle && calm && taken.count(static_cast<int>(i)) == 0 && open < 0) {
            open = static_cast<int>(i);
        }
    }
    check::is_true(water >= 0 && ice >= 0 && open >= 0, "water, land ice and open land in the world");
    const int legal = open;
    ground_refusal(w, water, "city", "cannot be founded on water", "the sea");
    ground_refusal(w, ice, "hamlet", "persistent land ice", "land ice");

    World steep = w;
    set_at(steep, "slope", legal, profile.traits.site_slope_limit);
    ground_refusal(steep, legal, "hamlet", "will not settle at or above", "a slope at the limit");

    World charged = w;
    set_at(charged, "magic_hazard", legal, profile.traits.mutation_limit + 0.01);
    ground_refusal(charged, legal, "hamlet", "magical hazard", "a hazard past the limit");
    // The people's own reading of the ley wins where the world publishes one: a site that
    // clears on it is judged on the NEXT gate, which here is the city already standing there.
    const int city = w.settlements.sites.at(0).node;
    World risk = w;
    set_at(risk, "magic_hazard", city, 0.99);
    set_at(risk, "magic_risk_" + people, city, 0.0);
    ground_refusal(risk, city, "city", "already holds", "the people's own risk overriding the plain hazard");
    set_at(risk, "magic_risk_" + people, legal, 0.99);
    ground_refusal(risk, legal, "hamlet", "magical hazard", "the people's own risk refusing");

    ground_refusal(w, city, "city", "already holds " + w.settlements.sites.at(0).name, "a node with a city on it");
    ground_refusal(w, w.humans.hamlets.at(0).node, "fortress", "already holds", "a node with a hamlet on it");
}

TEST(a_ruin_is_ground_nobody_founds_on) {
    // A generated hamlet may stand on a fallen city's node -- the hinterland pass does not
    // keep clear of ruins -- and then the hamlet is what the refusal names, a ruin never
    // renaming ground a standing settlement already names. So the ruin tested here is one
    // with nothing standing on it.
    const World& w = genesis();
    std::set<int> standing;
    for (const SettlementSite& s : w.settlements.sites) standing.insert(s.node);
    for (const RuralSite& h : w.humans.hamlets) standing.insert(h.node);
    for (const RuralSite& f : w.humans.fortresses) standing.insert(f.node);
    const Ruin* bare = nullptr;
    const Ruin* built_over = nullptr;
    for (const Ruin& ruin : w.ruins) {
        if (standing.count(ruin.node) == 0 && bare == nullptr) bare = &ruin;
        if (standing.count(ruin.node) != 0 && built_over == nullptr) built_over = &ruin;
    }
    check::is_true(bare != nullptr, "a ruin with nothing standing on it after two ages");
    RequestRefusal r = refused(w, asking(bare->node, first_people(w), "hamlet"), "a hamlet on a ruin");
    check::equal(r.field, std::string("node"), "the node named");
    check::is_true(r.refused_by_world, "the world's refusal");
    check::is_true(r.message.find("already holds " + bare->name) != std::string::npos, "naming the ruin: " + r.message);
    if (built_over != nullptr) {
        r = refused(w, asking(built_over->node, first_people(w), "fortress"), "a keep on a built-over ruin");
        check::is_true(r.message.find("already holds " + built_over->name) == std::string::npos,
                       "the settlement standing there named, not the ruin: " + r.message);
    }
}

TEST(cities_keep_the_settlement_spacing_and_rural_sites_their_own) {
    const World& w = rural_world();
    const std::string people = first_people(w);
    const auto held = sphere_grid(w.config.size, w.effective.globe_radius);
    std::set<int> taken;
    for (const SettlementSite& s : w.settlements.sites) taken.insert(s.node);
    for (const RuralSite& h : w.humans.hamlets) taken.insert(h.node);
    for (const RuralSite& f : w.humans.fortresses) taken.insert(f.node);
    // Open land inside the spacing of some city: at this size a cell is closer than the
    // spacing, so the neighbours of a city are exactly that.
    const double slope_limit = civilization_profile(people)->traits.site_slope_limit;
    int crowded = -1;
    for (std::size_t i = 0; i < held->node_count() && crowded < 0; ++i) {
        const int x = held->point_x[i];
        const int z = held->point_z[i];
        if (taken.count(static_cast<int>(i)) != 0 || w.layers.at("water_type").at(x, z) != 0.0) continue;
        if (static_cast<int>(w.layers.at("natural_biome").at(x, z)) == 17) continue;
        if (w.layers.at("slope").at(x, z) >= slope_limit) continue;
        const Vec3 here = direction(x, z, w.config.size);
        for (const SettlementSite& s : w.settlements.sites) {
            if (great_circle_m(w.effective.globe_radius, here, s.direction) < w.effective.settlement_spacing) {
                crowded = static_cast<int>(i);
                break;
            }
        }
    }
    check::is_true(crowded >= 0, "open land inside a city's spacing");
    RequestRefusal r = refused(w, asking(crowded, people), "a city crowding another");
    check::equal(r.field, std::string("node"), "the node named");
    check::is_true(r.message.find("cities are kept at least") != std::string::npos, "saying why: " + r.message);

    // Rural sites are held to their own separation, 120 m for a hamlet and 200 m for a keep,
    // which only the converging rows by a pole resolve. A hamlet set down beside another.
    const World fine = at(12, 129);
    const auto grid = sphere_grid(fine.config.size, fine.effective.globe_radius);
    const int a = grid->node_index(0, 1);
    const int b = grid->node_index(1, 1);
    const Vec3 da = direction(0, 1, fine.config.size);
    const Vec3 db = direction(1, 1, fine.config.size);
    const double apart = great_circle_m(fine.effective.globe_radius, da, db);
    check::is_true(apart < 120.0, "two ring neighbours by the pole closer than a hamlet's spacing");
    World crafted = fine;
    for (const int node : {a, b}) {
        set_at(crafted, "water_type", node, 0.0);
        set_at(crafted, "natural_biome", node, 3.0);
        set_at(crafted, "slope", node, 0.0);
        set_at(crafted, "magic_hazard", node, 0.0);
    }
    RuralSite neighbour;
    neighbour.id = "hamlet-" + std::to_string(crafted.humans.hamlets.size());
    neighbour.uid = "hamlet-node-" + std::to_string(a);
    neighbour.kind = "hamlet";
    neighbour.node = a;
    crafted.humans.hamlets.push_back(neighbour);
    const std::string civ = first_people(crafted);
    r = refused(crafted, asking(b, civ, "hamlet"), "a hamlet beside a hamlet");
    check::equal(r.field, std::string("node"), "the node named");
    check::is_true(r.message.find("hamlet-node-" + std::to_string(a)) != std::string::npos &&
                       r.message.find("at least 120 m") != std::string::npos,
                   "naming what stands there and the hamlet's spacing: " + r.message);
    r = refused(crafted, asking(b, civ, "fortress"), "a keep beside a hamlet");
    check::is_true(r.message.find("at least 200 m") != std::string::npos, "the keep's spacing: " + r.message);
}

TEST(a_player_city_on_ground_with_no_reachable_water_reads_minus_one) {
    // REFERENCE DEFECT, REPRODUCED: the layer spells "no fresh water reachable" as -1, and the
    // site record tests the value for finiteness rather than for that sentinel -- so a player
    // city there reads -1 m where a generated city reads none.
    const Founded open = first_legal(rural_world(), asking(0, first_people(rural_world())));
    World w = rural_world();
    set_at(w, "freshwater_distance", open.node, -1.0);
    const World founded = found_settlement(w, asking(open.node, first_people(w)));
    check::equal(founded.settlements.sites.back().freshwater_distance_m, -1.0, "the sentinel read as a distance");
}

// --- the did-you-mean the refusals carry --------------------------------------------------

TEST(the_did_you_mean_is_difflibs) {
    // Python's own documented answers, the asymmetric pair included: the block decomposition
    // is order-dependent, and a symmetric shortcut would answer 0.5 both ways.
    check::close(sequence_ratio("tide", "diet"), 0.25, 1e-12, "SequenceMatcher('tide', 'diet')");
    check::close(sequence_ratio("diet", "tide"), 0.5, 1e-12, "SequenceMatcher('diet', 'tide')");
    check::close(sequence_ratio("abcd", "bcde"), 0.75, 1e-12, "SequenceMatcher('abcd', 'bcde')");
    check::close(sequence_ratio("dwarf", "dwarfs"), 10.0 / 11.0, 1e-12, "one letter short");
    const std::vector<std::string> fruit{"ape", "apple", "peach", "puppy"};
    check::is_true(nearest_names("appel", fruit) == std::vector<std::string>{"apple", "ape"},
                   "get_close_matches('appel', ...) is ['apple', 'ape']");
    // A bare prefix outranks a closer spelling: the case a model produces when it guesses.
    const std::vector<std::string> controls{"radius", "rain_passes"};
    check::is_true(nearest_names("rain", controls).front() == "rain_passes", "a prefix first");
    check::is_true(nearest_names("zzzz", controls).empty(), "nothing close is nothing");
    check::equal(py_repr("it's"), std::string("\"it's\""), "repr picks the other quote");
}
