// The world-level mutators: corrupt, cleanse, summon, depart.
//
// These test the glue -- that each call copies the world, writes the pass's result back, runs
// the rebuild it owes, redraws what the reference redraws and logs itself -- not the passes,
// which have their own suites (corruption, visitation).
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "fwg/beasties_stage.hpp"
#include "fwg/json_value.hpp"
#include "fwg/magic.hpp"
#include "fwg/serialize.hpp"
#include "fwg/world_api.hpp"
#include "fwg/world_time.hpp"
#include "fwg/nomads.hpp"
#include "fwg/heroes.hpp"

using namespace fwg;

namespace {

// Two ages turned, villains seated at the last: a world a hidden god can reach into.
const World& finished() {
    static const World world = [] {
        Request r;
        r.seed = 42;
        r.size = 33;
        r.phase = 16;
        return generate(resolve(r));
    }();
    return world;
}

const GodRecord* god_of(const World& w, const std::string& id) {
    for (const GodRecord& g : w.religion.gods) {
        if (g.id == id) return &g;
    }
    return nullptr;
}

template <typename Op>
const Op* last_op(const World& w) {
    if (w.history.operations.empty()) return nullptr;
    return std::get_if<Op>(&w.history.operations.back());
}

// Far past every hidden god's noticing point: whoever watches, bites.
CorruptWorldResult corrupted() {
    CorruptWorldRequest request;
    request.encounters = 1000;
    return corrupt(finished(), request);
}

// The first god the pantheon holds manifest, summoned onto the first standing city.
World summoned(std::string& god_id) {
    for (const GodRecord& g : finished().religion.gods) {
        if (g.status != "manifest") continue;
        SummonRequest request;
        request.god_id = g.id;
        request.target_city_uid = finished().settlements.sites.at(0).uid;
        try {
            World out = summon(finished(), request);
            god_id = g.id;
            return out;
        } catch (const VisitationError&) {
            continue;  // this god cannot come here; try the next
        }
    }
    check::fail("no manifest god could be summoned onto the first city");
}

}  // namespace

TEST(nothing_watching_leaves_the_world_as_it_was) {
    CorruptWorldRequest request;
    request.encounters = 0;  // below every noticing point
    const CorruptWorldResult result = corrupt(finished(), request);
    check::is_true(!result.report.acted, "nothing to act at zero encounters");
    check::is_true(result.report.watching.empty(), "and nothing watching");
    check::is_true(!result.report.reason.empty(), "saying why");
    check::equal(result.world.ruins.size(), finished().ruins.size(), "no ruin");
    check::equal(result.world.history.operations.size(), std::size_t{0}, "nothing logged");
    check::is_true(result.world.has_plans, "the plans untouched");
}

TEST(a_corruption_takes_a_villain_rebuilds_and_reveals_its_god) {
    const CorruptWorldResult result = corrupted();
    const World& w = result.world;
    check::is_true(result.report.acted, "a hidden god to bite at a thousand encounters");
    check::equal(w.corruptions.size(), std::size_t{1}, "one corruption record");
    check::equal(w.corruptions[0].villain_uid, result.report.villain_uid, "naming the villain it took");
    const GodRecord* god = god_of(w, result.report.god_id);
    check::is_true(god != nullptr && god->status == "walking", "the hidden god revealed walking after the resolve");
    // The rivals it roused stay Wild: the reveal runs AFTER the resolve, which would otherwise
    // recompute every known god's aspect and erase it.
    // Sixteen known gods have one face and are roused without being pushed; only a rival with
    // a Wild face can show the order.
    std::size_t pushed = 0;
    for (const std::string& rival : w.corruptions[0].roused) {
        const PantheonGod* known = pantheon_god(rival);
        if (known == nullptr || known->aspects.wild.empty()) continue;
        ++pushed;
        check::equal(god_of(w, rival)->aspect, std::string("wild"), rival + " roused to its Wild aspect");
    }
    check::is_true(pushed > 0, "some roused rival with a Wild face, or the order is untested");
    // The world was rebuilt, and the reference redraws no plan after a corruption.
    check::is_true(!w.has_plans && !w.has_world_scene, "the plans and the scene dropped");
    check::equal(w.threat_assessments.evaluated_after, std::string("corruption"), "the threat re-assessed");
    // The religion block carries the same record, as JSON.
    check::equal(w.religion.corruptions.size(), std::size_t{1}, "the religion block's copy");
    check::equal(w.religion.corruptions[0], corruption_record_json(w.corruptions[0]), "and it is this one");
    const auto* op = last_op<CorruptionOperation>(w);
    check::is_true(op != nullptr && op->god_id == result.report.god_id, "the corruption logged");
    // A corruption's ruins name it; the caller's world is untouched.
    for (std::size_t i = finished().ruins.size(); i < w.ruins.size(); ++i) {
        check::is_true(w.ruins[i].corruption == 0, w.ruins[i].id + " to name its corruption");
        check::is_true(w.ruins[i].cause == "rot_plague" || w.ruins[i].cause == "void_unmade", w.ruins[i].id + " cause");
    }
    check::equal(finished().corruptions.size(), std::size_t{0}, "the caller's world untouched");
}

TEST(a_full_cleanse_ends_the_corruption_and_puts_the_god_back_to_sleep) {
    const CorruptWorldResult bitten = corrupted();
    CleanseWorldRequest request;
    request.corruption = 0;
    request.power = 1.0;
    const CleanseWorldResult result = cleanse(bitten.world, request);
    const World& w = result.world;
    check::is_true(result.report.cleansed, "power 1.0 to drain every node in one tick");
    check::equal(result.report.kind, std::string("corruption"), "logged under the report's kind");
    check::is_true(w.corruptions[0].cleansed_age.has_value(), "the record closed");
    check::equal(god_of(w, bitten.report.god_id)->status, std::string("sleeping"), "the god asleep again");
    for (const std::string& id : result.report.removed) {
        const auto& nodes = w.magic.networks.at(w.corruptions[0].school).nodes;
        check::is_true(std::none_of(nodes.begin(), nodes.end(), [&](const LeyNode& n) { return n.id == id; }),
                       id + " gone from the field");
    }
    check::equal(w.religion.corruptions[0], corruption_record_json(w.corruptions[0]), "the religion copy follows");
    check::is_true(last_op<CleanseOperation>(w) != nullptr, "the cleanse logged");
}

TEST(a_god_revealed_by_corruption_is_opposed_through_its_corruption) {
    const CorruptWorldResult bitten = corrupted();
    CleanseWorldRequest request;
    request.god_id = bitten.report.god_id;
    try {
        (void)cleanse(bitten.world, request);
        check::fail("a corrupted god sent home by name");
    } catch (const CorruptionError& refused) {
        check::equal(refused.field(), std::string("target.god_id"), "refused naming the target");
    }
    CleanseWorldRequest both;
    both.corruption = 0;
    both.god_id = bitten.report.god_id;
    check::throws([&] { (void)cleanse(bitten.world, both); }, "two targets refused");
}

TEST(a_summoned_god_walks_leaves_a_theophany_and_the_plans_are_redrawn) {
    std::string god_id;
    const World w = summoned(god_id);
    check::equal(god_of(w, god_id)->status, std::string("walking"), "the god walks after the resolve");
    check::equal(w.visitations.size(), std::size_t{1}, "one visitation record");
    check::equal(w.religion.visitations.size(), std::size_t{1}, "and its copy in the religion block");
    check::equal(w.religion.visitations[0], visitation_record_json(w.visitations[0]), "the same record");
    check::is_true(w.has_plans && w.has_world_scene, "the plans and the scene redrawn");
    check::equal(w.city_plans.cities.size(), w.settlements.sites.size(), "a plan for every standing city");
    const auto* op = last_op<VisitationOperation>(w);
    check::is_true(op != nullptr && op->kind == "visitation", "the summons logged");
    bool theophany = false;
    for (const ReligionSite& s : w.religion.sites) theophany = theophany || s.kind == "theophany";
    check::is_true(theophany, "a theophany where it stood");
}

TEST(a_departed_god_goes_home_and_its_theophany_becomes_a_pilgrimage) {
    std::string god_id;
    const World walked = summoned(god_id);
    const World w = depart(walked, god_id);
    check::equal(god_of(w, god_id)->status, std::string("manifest"), "home again");
    check::is_true(w.visitations[0].departed_age.has_value(), "the record closed");
    bool pilgrimage = false;
    for (const ReligionSite& s : w.religion.sites) pilgrimage = pilgrimage || s.kind == "pilgrimage";
    check::is_true(pilgrimage, "the theophany carried forward as a pilgrimage");
    const auto* op = last_op<VisitationOperation>(w);
    check::is_true(op != nullptr && op->kind == "departure", "the departure logged");
    check::is_true(w.has_plans, "the plans redrawn");
    check::throws([&] { (void)depart(w, god_id); }, "a god that no longer walks cannot depart again");
}

TEST(a_cleanse_aimed_at_a_summoned_god_sends_it_home) {
    std::string god_id;
    const World walked = summoned(god_id);
    CleanseWorldRequest request;
    request.god_id = god_id;
    const CleanseWorldResult result = cleanse(walked, request);
    check::equal(god_of(result.world, god_id)->status, std::string("manifest"), "sent home");
    check::equal(result.report.kind, std::string("god"), "logged as the reference logs it");
    check::is_true(result.report.departed, "departed");
}

TEST(an_age_turn_sends_a_summoned_god_home) {
    std::string god_id;
    const World walked = summoned(god_id);
    const World later = advance_ages(walked, AgeAdvanceRequest{});
    check::equal(god_of(later, god_id)->status, std::string("manifest"), "the visitor went home with the age");
    check::is_true(later.visitations[0].departed_age.has_value(), "its record closed by the age");
    check::equal(later.history.operations.size(), std::size_t{2}, "the summons and the advance, in order");
    check::is_true(std::holds_alternative<VisitationOperation>(later.history.operations[0]), "summons first");
    check::is_true(std::holds_alternative<AgeOperation>(later.history.operations[1]), "then the advance");
}

namespace {

bool hidden_school(const std::string& school) {
    const int s = magic_school_index(school);
    return s >= 0 && magic_school_table()[static_cast<std::size_t>(s)].hidden;
}

const SettlementSite* site_with_uid(const World& w, const std::string& uid) {
    for (const SettlementSite& s : w.settlements.sites) {
        if (s.uid == uid) return &s;
    }
    return nullptr;
}

}  // namespace

TEST(a_city_blood_farms_or_eldritch_takes_is_held_by_the_gods_school) {
    // Ruled 2026-09-23: marked on the rebuilt site, where the reference's rebuild erased both
    // effects. What each city lost stays in the record's `touched` list.
    const CorruptWorldResult bitten = corrupted();
    const CorruptionRecord* record = nullptr;
    for (const CorruptionRecord& r : bitten.world.corruptions) {
        if (r.index == bitten.report.corruption) record = &r;
    }
    check::is_true(record != nullptr, "the record the report names");
    std::size_t held = 0;
    for (const CorruptionTouch& t : record->touched) {
        if (t.effect != "farmed" && t.effect != "taken") continue;
        const SettlementSite* site = site_with_uid(bitten.world, t.city_uid);
        check::is_true(site != nullptr, t.city_uid + " stands");
        check::is_true(site->corruption.has_value(), t.city_uid + " is held");
        check::equal(site->corruption->school, record->school, t.city_uid + " by the god's school");
        check::equal(site->corruption->cause, t.effect, t.city_uid + " for what the god did to it");
        check::equal(site->corruption->age, record->age, t.city_uid + " in the age it was done");
        check::is_true(site->corruption->corruption == std::optional<int>(record->index),
                       t.city_uid + " naming the corruption");
        ++held;
    }
    check::is_true(held > 0, "a god that farms or takes, or nothing is tested");
}

TEST(an_age_turns_over_ground_a_hidden_school_owns_and_every_hold_persists) {
    // Ruled 2026-09-23; the reference raised a KeyError at the first age turn after such a
    // corruption. The age turns, no ruin is a hidden school's, and every held city that still
    // stands is held as it was: corruption persists.
    //
    // Seed 7's world, not the shared seed-42 one: since beasts began razing cities (2026-09-23)
    // every one of seed 42's six held cities falls to a villain or a war in the next age, which
    // would leave the carry untested; seed 7's corruption holds fourteen and one still stands.
    Request seven;
    seven.seed = 7;
    seven.size = 33;
    seven.phase = 16;
    const World world = generate(resolve(seven));
    std::size_t carried = 0;
    int corruptions = 0;
    {
        CorruptWorldRequest request;
        request.encounters = 1000;
        const CorruptWorldResult bitten = corrupt(world, request);
        if (bitten.report.acted) ++corruptions;
        const World later = advance_ages(bitten.world, AgeAdvanceRequest{});
        for (const Ruin& ruin : later.ruins) {
            check::is_true(!hidden_school(ruin.cause), ruin.id + " not ruined by a hidden school");
        }
        for (const SettlementSite& before : bitten.world.settlements.sites) {
            if (!before.corruption) continue;
            const SettlementSite* after = site_with_uid(later, before.uid);
            if (after == nullptr) continue;  // fell to something else this age
            ++carried;
            check::is_true(after->corruption.has_value(), before.uid + " still held after the age");
            check::equal(after->corruption->school, before.corruption->school, before.uid + " by the same school");
            check::equal(after->corruption->cause, before.corruption->cause, before.uid + " for the same cause");
            check::equal(after->corruption->age, before.corruption->age, before.uid + " since the same age");
        }
        for (const SettlementSite& s : later.settlements.sites) {
            if (s.corruption) check::is_true(hidden_school(s.corruption->school), s.uid + " held by a hidden school");
        }
    }
    check::is_true(corruptions > 0, "the corruption to act");
    check::is_true(carried > 0, "some held city standing through the age, or the carry is untested");
}

// --- cleansing what a hidden school holds (ruled 2026-09-23) -------------------------------

namespace {

const SettlementSite* first_held(const World& w) {
    for (const SettlementSite& s : w.settlements.sites) {
        if (s.corruption) return &s;
    }
    return nullptr;
}

const Ruin* ruin_with_id(const World& w, const std::string& id) {
    for (const Ruin& r : w.ruins) {
        if (r.id == id) return &r;
    }
    return nullptr;
}

const NomadBand* band_with_uid(const World& w, const std::string& uid) {
    for (const NomadBand& b : w.nomads.groups) {
        if (b.uid == uid) return &b;
    }
    return nullptr;
}

const HeroQuestHook* hook_with_id(const World& w, const std::string& id) {
    for (const HeroQuestHook& h : w.heroes.quest_hooks) {
        if (h.hook_id == id) return &h;
    }
    return nullptr;
}

// One corruption for every test below: each call of corrupted() runs it again.
const CorruptWorldResult& bitten_once() {
    static const CorruptWorldResult result = corrupted();
    return result;
}

std::size_t band_requests(const World& w) {
    std::size_t n = 0;
    for (const HistoryOperation& op : w.history.operations) n += std::holds_alternative<NomadOperation>(op) ? 1 : 0;
    return n;
}

std::string cleanse_refusal(const World& w, const CleanseWorldRequest& request) {
    try {
        (void)cleanse(w, request);
    } catch (const CorruptionError& refused) {
        return refused.field();
    }
    return "<accepted>";
}

// The fixture corruption's first held city, cleansed by name.
const CleanseWorldResult& cleansed_city() {
    static const CleanseWorldResult result = [] {
        CleanseWorldRequest request;
        request.city_uid = first_held(bitten_once().world)->uid;
        return cleanse(bitten_once().world, request);
    }();
    return result;
}

}  // namespace

TEST(cleansing_a_corrupted_city_ruins_it_and_its_refugees_become_a_band) {
    const World& before = bitten_once().world;
    const SettlementSite* held = first_held(before);
    check::is_true(held != nullptr, "a held city to cleanse");
    const SettlementSite city = *held;
    const CleanseWorldResult& result = cleansed_city();
    const World& w = result.world;
    check::equal(result.report.kind, std::string("city"), "a city cleanse");
    check::is_true(site_with_uid(w, city.uid) == nullptr, "the city no longer stands");

    const Ruin* ruin = ruin_with_id(w, "ruin-" + city.uid);
    check::is_true(ruin != nullptr, "a ruin where it stood");
    check::equal(ruin->cause, std::string("cleansed"), "cleansed");
    check::equal(ruin->evidence.school, city.corruption->school, "of the school that held it");
    check::equal(ruin->destroyed_age, before.age, "in the age it was cleansed");
    check::is_true(ruin->new_node_school.empty(), "and it plants no key point");

    check::equal(result.report.cities.size(), std::size_t{1}, "one city destroyed");
    const CleansedCity& c = result.report.cities.front();
    check::equal(c.city_uid, city.uid, "the city named");
    check::equal(c.people, city.population_estimate, "its people counted");
    check::between(c.escape_share, kCleanseEscapeLow, kCleanseEscapeHigh, "a share drawn in its range");
    check::equal(c.escaped, std::llround(static_cast<double>(c.people) * c.escape_share), "the people who got out");
    const NomadBand* band = band_with_uid(w, c.band_uid);
    check::is_true(band != nullptr, "raised as a band");
    check::is_true(band->classification == NomadClass::Survivors, "of survivors");
    check::equal(static_cast<long long>(band->size), c.escaped, "every one of them");
    check::equal(band->node, ruin->node, "at the ruin");
    check::equal(band->origin.id, ruin->id, "raised by the ruin");

    // One operation for the call: the cleanse names the band, and no band request is logged.
    check::equal(band_requests(w), band_requests(before), "no band request logged");
    check::is_true(std::holds_alternative<CleanseOperation>(w.history.operations.back()), "the cleanse logged last");
}

TEST(a_cleansed_citys_refugees_offer_to_rebuild_it_or_to_return_to_their_kin) {
    const World& before = bitten_once().world;
    const SettlementSite city = *first_held(before);
    const CleanseWorldResult& result = cleansed_city();
    const World& w = result.world;
    const CleansedCity& c = result.report.cities.front();
    check::is_true(c.hook_ids == std::vector<std::string>{"hook-" + c.band_uid + "-0", "hook-" + c.band_uid + "-1"},
                   "two hooks, minted as the cast mints them");
    const HeroQuestHook* rebuild = hook_with_id(w, c.hook_ids[0]);
    const HeroQuestHook* back = hook_with_id(w, c.hook_ids[1]);
    check::is_true(rebuild != nullptr && back != nullptr, "both on the cast's hooks");
    check::equal(rebuild->verb, std::string("rebuild"), "rebuild");
    check::equal(rebuild->giver_uid, c.band_uid, "given by the refugees");
    check::equal(rebuild->target, c.ruin_id, "on the ruin");
    const int expected = city.city_class == CityClass::Capital ? 4 : city.city_class == CityClass::Medium ? 3 : 2;
    check::equal(rebuild->difficulty, expected, "as hard as the city was large");
    check::equal(back->verb, std::string("return"), "return");
    check::equal(back->giver_uid, c.band_uid, "given by the refugees");
    check::equal(back->target, c.band_uid, "on the band");
    check::equal(back->difficulty, kReturnDifficulty, "return's difficulty");
    check::equal(w.heroes.summary.hooks, before.heroes.summary.hooks + 2, "the summary counts them");

    // The board offers both on the next span of time: the band stands, so its giver is present.
    TimeAdvanceRequest tick;
    tick.elapsed = TimeElapsed{TimeUnit::Days, 30.0};
    const World ticked = advance_world_time(w, tick).world;
    check::is_true(ticked.quests.has_value(), "a board");
    for (const std::string& id : c.hook_ids) {
        const auto quest = std::find_if(ticked.quests->quests.begin(), ticked.quests->quests.end(),
                                        [&](const QuestRecord& q) { return q.quest_id == id; });
        check::is_true(quest != ticked.quests->quests.end(), id + " offered");
        check::is_true(quest->state == QuestState::Offered, id + " still open: its giver stands");
        check::is_true(quest->giver_uid == std::optional<std::string>(c.band_uid), id + " given by the band");
    }
}

TEST(a_city_cleanse_refuses_a_city_nothing_holds_and_names_one_target) {
    const World& plain = finished();
    CleanseWorldRequest request;
    request.city_uid = plain.settlements.sites.at(0).uid;
    check::equal(cleanse_refusal(plain, request), std::string("target.city_uid"), "a city nothing holds");
    request.city_uid = "surface-city-9-0-nobody";
    check::equal(cleanse_refusal(plain, request), std::string("target.city_uid"), "a city that is not there");
    const World& bitten = bitten_once().world;
    CleanseWorldRequest both;
    both.city_uid = first_held(bitten)->uid;
    both.corruption = bitten_once().report.corruption;
    check::equal(cleanse_refusal(bitten, both), std::string("target"), "two targets at once");
}

TEST(a_corruption_drained_to_its_last_node_destroys_every_city_it_holds) {
    const CorruptWorldResult& bitten = bitten_once();
    std::set<std::string> held;
    for (const SettlementSite& s : bitten.world.settlements.sites) {
        if (s.corruption && s.corruption->corruption == std::optional<int>(bitten.report.corruption)) held.insert(s.uid);
    }
    check::is_true(!held.empty(), "a corruption that holds cities, or nothing is tested");

    // A partial drain leaves the corruption standing, and the cities it holds.
    CleanseWorldRequest weak;
    weak.corruption = bitten.report.corruption;
    weak.power = 0.01;
    const CleanseWorldResult partial = cleanse(bitten.world, weak);
    check::is_true(!partial.report.cleansed, "a partial drain leaves the corruption");
    check::is_true(partial.report.cities.empty(), "and destroys nothing");
    for (const std::string& uid : held) {
        const SettlementSite* site = site_with_uid(partial.world, uid);
        check::is_true(site != nullptr && site->corruption.has_value(), uid + " still stands, held");
    }

    // Drained to its last node, it breaks its hold on every city it took: each is destroyed,
    // and each one's refugees are raised, a refuge within their reach or not.
    World current = bitten.world;
    CleanseWorldResult last{current, {}};
    for (int i = 0; i < 50 && !last.report.cleansed; ++i) {
        CleanseWorldRequest drain;
        drain.corruption = bitten.report.corruption;
        drain.power = 1.0;
        last = cleanse(current, drain);
        current = last.world;
    }
    check::is_true(last.report.cleansed, "the corruption drained to its last node");
    std::set<std::string> destroyed;
    for (const CleansedCity& c : last.report.cities) {
        destroyed.insert(c.city_uid);
        check::is_true(site_with_uid(current, c.city_uid) == nullptr, c.city_uid + " no longer stands");
        const Ruin* ruin = ruin_with_id(current, c.ruin_id);
        check::is_true(ruin != nullptr && ruin->cause == "cleansed", c.city_uid + " lies in a cleansed ruin");
        check::is_true(!c.band_uid.empty() && band_with_uid(current, c.band_uid) != nullptr,
                       c.city_uid + "'s refugees raised: " + c.note);
        check::equal(c.hook_ids.size(), std::size_t{2}, c.city_uid + "'s quest line");
    }
    check::is_true(destroyed == held, "every city it held, and no other");
}

TEST(a_cleanse_is_a_function_of_its_request) {
    CleanseWorldRequest request;
    request.city_uid = first_held(bitten_once().world)->uid;
    const CleanseWorldResult again = cleanse(bitten_once().world, request);
    const CleansedCity& a = cleansed_city().report.cities.front();
    const CleansedCity& b = again.report.cities.front();
    check::equal(a.escape_share, b.escape_share, "the same share");
    check::equal(a.escaped, b.escaped, "the same people");
    check::equal(a.band_uid, b.band_uid, "the same band");
}

TEST(a_held_city_publishes_its_hold) {
    // This port's own key, only on a held city: {school, cause, age, corruption}.
    World bare;
    bare.has_settlements = true;
    SettlementSite site;
    site.uid = "surface-city-0-1-gnome";
    site.corruption = CityCorruption{"eldritch", "taken", 2, 0};
    bare.settlements.sites.push_back(site);
    SettlementSite plain;
    plain.uid = "surface-city-0-2-gnome";
    bare.settlements.sites.push_back(plain);
    std::ostringstream out;
    write_world_json(out, bare, /*pretty=*/false, /*include_timings=*/false);
    const JsonValue doc = parse_json(out.str());
    const auto& sites = doc.at("settlements").at("sites").as_array();
    check::equal(sites.size(), std::size_t{2}, "both sites written");
    check::is_true(!sites.at(1).contains("corruption"), "no key on a city nothing holds");
    const JsonValue& hold = sites.at(0).at("corruption");
    std::vector<std::string> keys;
    for (const auto& member : hold.as_object()) keys.push_back(member.first);
    check::is_true(keys == std::vector<std::string>{"school", "cause", "age", "corruption"}, "the hold's keys");
    check::equal(hold.at("school").as_string(), std::string("eldritch"), "school");
    check::equal(hold.at("cause").as_string(), std::string("taken"), "cause");
    check::equal(hold.at("age").as_int(), std::int64_t{2}, "age");
    check::equal(hold.at("corruption").as_int(), std::int64_t{0}, "the corruption record");
}

TEST(the_reveal_outlasts_the_pantheons_re_resolve) {
    // A school god's aspect is Wild when its network runs hot, which on a generated world it
    // nearly always does -- so the order of the reveal and the resolve is invisible there.
    // Quiet the rivals' networks first: the resolve alone would now make them Sovereign, and
    // only a reveal that runs AFTER it leaves them Wild.
    World quiet = finished();
    for (auto& [school, net] : quiet.magic.networks) {
        for (LeyNode& node : net.nodes) node.intensity = std::min(node.intensity, 0.1);
        for (LeyEdge& edge : net.edges) edge.intensity = std::min(edge.intensity, 0.1);
    }
    CorruptWorldRequest request;
    request.encounters = 1000;
    const CorruptWorldResult result = corrupt(quiet, request);
    check::is_true(result.report.acted, "the corruption to act on a quiet field");
    World resolved_alone = result.world;
    resolve_world_religion(resolved_alone);
    std::size_t shown = 0;
    for (const std::string& rival : result.world.corruptions.at(0).roused) {
        const PantheonGod* known = pantheon_god(rival);
        if (known == nullptr || known->aspects.wild.empty()) continue;
        if (god_of(resolved_alone, rival)->aspect == "wild") continue;  // cannot show the order
        ++shown;
        check::equal(god_of(result.world, rival)->aspect, std::string("wild"), rival + " kept Wild by the reveal");
    }
    check::is_true(shown > 0, "some rival the resolve alone would calm, or the order is untested");
}
