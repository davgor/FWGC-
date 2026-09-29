// The director against a real world: seed 42, size 33, a month after founding, when the quest
// board has opened. Every test that looks for something first checks the world holds it, so a
// world that changed shape fails loudly here rather than passing over nothing.
#include "orchestrator/director.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#include "check.hpp"
#include "fwg/api/quests.hpp"
#include "fwg/api/read.hpp"
#include "fwg/api/serialize.hpp"
#include "fwg/api/world_time.hpp"

namespace {

using orchestrator::ActKind;
using orchestrator::Director;
using orchestrator::Fact;
using orchestrator::FactKind;
using orchestrator::FactPacket;
using orchestrator::Outcome;

const fwg::World& month_old() {
    static const fwg::World world = [] {
        fwg::Request request;
        request.seed = 42;
        request.size = 33;
        Director director(fwg::generate(fwg::resolve(request)));
        director.advance(30.0);
        return director.world();
    }();
    return world;
}

std::string json_of(const fwg::World& world) {
    std::ostringstream out;
    fwg::write_world_json(out, world, /*pretty=*/false);
    return out.str();
}

std::vector<fwg::QuestRow> board(const fwg::World& world, fwg::QuestState state) {
    fwg::QuestsReadRequest request;
    request.states = std::vector<fwg::QuestState>{state};
    return fwg::read_quests(world, request).quests;
}

// Everything a packet would put in front of a voice, as one text.
std::string text_of(const FactPacket& p) {
    std::string out = p.persona.name + "\n" + p.persona.role + "\n" + p.persona.place + "\n" + p.persona.manner + "\n" +
                      p.persona.voice + "\n" + p.persona.toward_player + "\n" + p.persona.line + "\n";
    for (const std::string& line : p.persona.never) out += line + "\n";
    for (const Fact& f : p.facts) {
        out += f.subject + "\n";
        for (const auto& [key, value] : f.fields) out += key + "=" + value + "\n";
    }
    return out;
}

double reach_ceiling_km(const fwg::World& world) {
    return world.effective.globe_radius * fwg::kNearRadiusFraction / 1000.0;
}

// A large world: its near read's ceiling (47.75 km) passes a region's width, as far as a renowned hero's own
// reach (40 km), which passes a city dweller's reach (20 km), so every width fame is told by shows in one read.
// Seed 18: a hero known through a realm of five cities 89 km across, where most realms are a city or two.
const fwg::World& wide() {
    static const fwg::World world = [] {
        fwg::Request request;
        request.seed = 18;
        request.size = 33;
        request.world_size = fwg::parse_world_size("large");
        return fwg::generate(fwg::resolve(request));
    }();
    return world;
}

// How far each place stands from `node`, in metres, by the near read out to the ceiling, keyed by the node it
// stands on; `node` itself at 0. A node missing stands past the ceiling. Of two places on one node, the
// nearer: a city is measured from where it stands, a hamlet from its node's cell, and the director counts a
// node as within a width when any place on it is.
std::map<int, double> place_metres(const fwg::World& world, int node) {
    fwg::NearReadRequest near;
    near.node = node;
    near.radius_m = reach_ceiling_km(world) * 1000.0;
    near.categories = std::vector<std::string>{"settlements", "hamlets", "fortresses", "key_locations"};
    const fwg::NearRead read = fwg::read_near(world, near);
    std::map<int, double> out = {{node, 0.0}};
    const auto keep = [&](const auto& rows) {
        for (const auto& r : rows)
            if (const auto [at, fresh] = out.emplace(r.node, r.distance_m); !fresh) at->second = std::min(at->second, r.distance_m);
    };
    keep(read.near.settlements);
    keep(read.near.hamlets);
    keep(read.near.fortresses);
    keep(read.near.key_locations);
    return out;
}

// How far each place and nest stands from `speaker`, in metres, by the near read out to the
// ceiling: the oracle a packet's rough `range` is checked against, since the packet itself no
// longer says metres. Keyed by the ids facts use as subjects.
std::map<std::string, double> metres_from(const fwg::World& world, const std::string& speaker) {
    fwg::PersonReadRequest who;
    who.uid = speaker;
    const std::optional<int> node = fwg::read_person(world, who).person.anchor.node;
    if (!node) check::fail("the premise: " + speaker + " stands somewhere");
    fwg::NearReadRequest near;
    near.node = *node;
    near.radius_m = reach_ceiling_km(world) * 1000.0;
    near.categories = std::vector<std::string>{"settlements", "hamlets", "fortresses", "key_locations", "beast_nests"};
    const fwg::NearRead read = fwg::read_near(world, near);
    std::map<std::string, double> out;
    for (const auto& r : read.near.settlements) out[r.uid] = r.distance_m;
    for (const auto& r : read.near.hamlets) out[r.id] = r.distance_m;
    for (const auto& r : read.near.fortresses) out[r.id] = r.distance_m;
    for (const auto& r : read.near.key_locations) out[r.id] = r.distance_m;
    for (const auto& r : read.near.beast_nests) out[r.id] = r.distance_m;
    return out;
}

// The bands contract.hpp publishes for `range`, as [from, to) metres.
std::pair<double, double> band_metres(const std::string& range) {
    if (range == "here") return {0.0, 50.0};
    if (range == "close") return {50.0, 1000.0};
    if (range == "near") return {1000.0, 3000.0};
    if (range == "walk") return {3000.0, 7000.0};
    if (range == "far") return {7000.0, 15000.0};
    if (range == "distant") return {15000.0, 1e12};
    check::fail("a published band, not " + range);
}

// A giver with open offers who stands somewhere the world names.
std::string a_giver() {
    for (const fwg::QuestRow& q : board(month_old(), fwg::QuestState::Offered))
        if (q.giver_uid && q.giver && q.giver->anchor.node) return *q.giver_uid;
    check::fail("the premise: a month-old world has an offer from a giver who stands somewhere");
}

// Where a roster person stands: their presence node, else their site's node -- the rule the
// read API documents for a roster person's anchor.
std::optional<int> npc_node(const fwg::World& world, const fwg::NpcPerson& person) {
    if (person.presence_node) return person.presence_node;
    if (!person.site_uid) return std::nullopt;
    for (const fwg::NpcSite& site : world.npcs.sites)
        if (site.uid == *person.site_uid) return site.node;
    return std::nullopt;
}

// A living roster person who gives no quest, standing where a giver with an open offer stands.
// With `in_a_city`, one who lives in a city: on the small world their 20 km reach clamps to
// exactly the near read's ceiling, so a test can use the ceiling as the reach without restating
// the reach table.
std::string a_broker(bool in_a_city = false) {
    const fwg::World& w = month_old();
    std::set<int> giver_nodes;
    std::set<std::string> givers;
    for (const fwg::QuestRow& q : board(w, fwg::QuestState::Offered)) {
        if (q.giver_uid) givers.insert(*q.giver_uid);
        if (q.giver && q.giver->anchor.node) giver_nodes.insert(*q.giver->anchor.node);
    }
    for (const fwg::NpcPerson& p : w.npcs.people) {
        if (!p.alive || p.is_cast || givers.count(p.uid)) continue;
        if (in_a_city && (!p.site_kind || *p.site_kind != "city")) continue;
        const std::optional<int> node = npc_node(w, p);
        if (node && giver_nodes.count(*node)) return p.uid;
    }
    check::fail("the premise: some roster person stands where a giver stands");
}

// The realm whose cities hold a roster person's site, read off the world's records (contract version 25's
// oracle): a city by its uid, a hamlet or a fortress by the city it belongs to.
std::string realm_of_roster(const fwg::World& world, const fwg::NpcPerson& person) {
    if (!person.site_uid) return {};
    std::string city = *person.site_uid;
    for (const std::vector<fwg::RuralSite>* list : {&world.humans.hamlets, &world.humans.fortresses})
        for (const fwg::RuralSite& rural : *list)
            if (rural.uid == city)
                for (const auto& settlement : world.settlements.sites)
                    if (settlement.id == rural.core_id) city = settlement.uid;
    for (const fwg::HeroRealm& realm : world.heroes.realms)
        if (std::find(realm.city_uids.begin(), realm.city_uids.end(), city) != realm.city_uids.end()) return realm.uid;
    return {};
}

const fwg::NpcPerson& roster_person(const fwg::World& world, const std::string& uid) {
    for (const fwg::NpcPerson& p : world.npcs.people)
        if (p.uid == uid) return p;
    check::fail("a roster person " + uid);
}

std::string sovereign_of(const fwg::World& world, const std::string& realm) {
    for (const fwg::HeroRealm& r : world.heroes.realms)
        if (r.uid == realm) return r.sovereign_uid;
    return {};
}

const fwg::HeroDeed* known_deed_of(const fwg::Hero& hero) {
    for (const fwg::HeroDeed& deed : hero.deeds)
        if (deed.known_for) return &deed;
    return nullptr;
}

// Whether a roster person knows `hero` by fame, never having met them, on a world whose ceiling is their
// reach, and so is a region's width: their realm's sovereign, or a hero whose known deed's word went through
// the person's own realm.
bool famous_to(const fwg::World& world, const std::string& roster, const fwg::Hero& hero) {
    const std::string realm = realm_of_roster(world, roster_person(world, roster));
    if (!realm.empty() && sovereign_of(world, realm) == hero.uid) return true;
    const fwg::HeroDeed* deed = known_deed_of(hero);
    return deed != nullptr && deed->scope == "realm" && !realm.empty() && realm == hero.realm_uid;
}

std::vector<const Fact*> of_kind(const FactPacket& p, FactKind kind) {
    std::vector<const Fact*> out;
    for (const Fact& f : p.facts)
        if (f.kind == kind) out.push_back(&f);
    return out;
}

// Nothing said but what the speaker remembers: the one Memory fact every packet carries.
bool only_memory(const FactPacket& p) { return p.facts.size() == 1 && p.facts.front().kind == FactKind::Memory; }

const fwg::HeroQuestHook& hook(const std::string& quest_id) {
    for (const fwg::HeroQuestHook& h : month_old().heroes.quest_hooks)
        if (h.hook_id == quest_id) return h;
    check::fail("a hook for " + quest_id);
}

}  // namespace

TEST(asking_a_giver_for_work_names_their_own_offers_first) {
    Director d(month_old());
    const std::string giver = a_giver();
    std::vector<std::string> own;
    for (const fwg::QuestRow& q : board(month_old(), fwg::QuestState::Offered))
        if (q.giver_uid && *q.giver_uid == giver) own.push_back(q.quest_id);
    const FactPacket p = d.respond(giver, {ActKind::AskForWork, ""});
    check::is_true(p.outcome == Outcome::Answered, "an answer");
    const std::vector<const Fact*> offers = of_kind(p, FactKind::QuestOffer);
    check::is_true(offers.size() >= own.size(), "at least the giver's own offers");
    for (std::size_t i = 0; i < own.size(); ++i) {
        check::equal(offers[i]->subject, own[i], "own offers first, in board order");
        check::equal(*offers[i]->field("from_speaker"), std::string("yes"), "their own");
        check::equal(*offers[i]->field("difficulty"), std::to_string(hook(own[i]).difficulty), "the hook's difficulty");
    }
}

TEST(an_offer_carries_only_what_its_giver_would_say) {
    // Never the hook's actual effect, and never whether the stated purpose is the true one.
    const std::set<std::string> allowed = {"giver", "giver_role", "from_speaker", "verb", "purpose",
                                           "difficulty", "where", "range", "days_left", "marker", "marker_node",
                                           // How another giver stands to the speaker (contract version 15): public, as
                                           // a hero's ties are.
                                           "giver_tie"};
    Director d(month_old());
    std::set<std::string> givers;
    for (const fwg::QuestRow& q : board(month_old(), fwg::QuestState::Offered))
        if (q.giver_uid && givers.size() < 40) givers.insert(*q.giver_uid);
    std::size_t offers = 0, unwitting = 0;
    for (const std::string& giver : givers) {
        const FactPacket p = d.respond(giver, {ActKind::AskForWork, ""});
        for (const Fact* f : of_kind(p, FactKind::QuestOffer)) {
            ++offers;
            for (const auto& [key, value] : f->fields) check::is_true(allowed.count(key) == 1, "an allowed key, not " + key);
            const fwg::HeroQuestHook& h = hook(f->subject);
            if (h.unwitting) {
                ++unwitting;
                check::equal(*f->field("purpose"), h.stated_purpose, "the purpose the giver states, true or not");
            }
        }
    }
    check::is_true(offers > 0, "the premise: offers to look at");
    (void)unwitting;  // how many were unwitting is reported by the key check, not required
}

TEST(a_roster_person_brokers_the_work_of_givers_where_they_stand) {
    Director d(month_old());
    const std::string broker = a_broker();
    const FactPacket p = d.respond(broker, {ActKind::AskForWork, ""});
    const std::vector<const Fact*> offers = of_kind(p, FactKind::QuestOffer);
    check::is_true(!offers.empty(), "the work of givers standing with them");
    fwg::PersonReadRequest request;
    request.uid = broker;
    const std::optional<int> here = fwg::read_person(month_old(), request).person.anchor.node;
    check::is_true(here.has_value(), "the broker stands somewhere");
    std::map<std::string, fwg::QuestRow> rows;
    for (const fwg::QuestRow& q : board(month_old(), fwg::QuestState::Offered)) rows.emplace(q.quest_id, q);
    for (const Fact* f : offers) {
        check::equal(*f->field("from_speaker"), std::string("no"), "none of it their own");
        const fwg::QuestRow& q = rows.at(f->subject);
        check::is_true(q.giver && q.giver->anchor.node && *q.giver->anchor.node == *here, "a giver standing here: " + f->subject);
    }
}

TEST(taking_work_from_its_giver_takes_it_and_logs_one_operation) {
    Director d(month_old());
    const std::string giver = a_giver();
    const std::string quest = of_kind(d.respond(giver, {ActKind::AskForWork, ""}), FactKind::QuestOffer).front()->subject;
    const std::size_t before = d.world().history.operations.size();
    const FactPacket p = d.respond(giver, {ActKind::AcceptQuest, quest});
    check::is_true(p.outcome == Outcome::Accepted, "accepted");
    check::equal(*of_kind(p, FactKind::QuestState).front()->field("state"), std::string("taken"), "the state said");
    const std::vector<fwg::QuestRow> taken = board(d.world(), fwg::QuestState::Taken);
    check::is_true(std::any_of(taken.begin(), taken.end(), [&](const fwg::QuestRow& q) { return q.quest_id == quest; }),
                   "the board has it taken");
    check::equal(d.world().history.operations.size(), before + 1, "one logged operation");
}

TEST(taking_work_through_a_broker_is_refused_and_names_the_giver) {
    Director d(month_old());
    const std::string broker = a_broker();
    const FactPacket work = d.respond(broker, {ActKind::AskForWork, ""});
    const std::string quest = of_kind(work, FactKind::QuestOffer).front()->subject;
    const std::string before = json_of(d.world());
    const FactPacket p = d.respond(broker, {ActKind::AcceptQuest, quest});
    check::is_true(p.outcome == Outcome::Refused, "refused");
    check::is_true(!of_kind(p, FactKind::Reason).empty(), "with a reason");
    std::string giver;
    for (const fwg::QuestRow& q : board(month_old(), fwg::QuestState::Offered))
        if (q.quest_id == quest) giver = q.giver_uid.value_or("");
    const std::vector<const Fact*> people = of_kind(p, FactKind::Person);
    check::is_true(!people.empty() && people.front()->subject == giver, "and the giver to go to");
    check::is_true(json_of(d.world()) == before, "the world unchanged");
    // The reason says whose the work is, by name. "Not the speaker's to give", voiced by the
    // speaker, was read as "not yours" (judged 2026-09-24).
    const std::string* name = people.front()->field("name");
    check::is_true(name && !name->empty(), "the premise: the giver has a name");
    check::equal(*of_kind(p, FactKind::Reason).front()->field("reason"), "the work is " + *name + "'s to give", "whose work, by name");
    check::equal(*of_kind(p, FactKind::Reason).front()->field("why"), std::string("not_theirs_to_give"), "the kind of refusal");
}

TEST(giving_up_work_goes_back_to_its_giver_and_nobody_else) {
    Director d(month_old());
    const std::string giver = a_giver();
    const std::string broker = a_broker();
    const std::string quest = of_kind(d.respond(giver, {ActKind::AskForWork, ""}), FactKind::QuestOffer).front()->subject;
    check::is_true(d.respond(giver, {ActKind::AcceptQuest, quest}).outcome == Outcome::Accepted, "taken first");
    const FactPacket refused = d.respond(broker, {ActKind::AbandonQuest, quest});
    check::is_true(refused.outcome == Outcome::Refused, "a stranger cannot release it");
    const std::vector<const Fact*> named = of_kind(refused, FactKind::Person);
    check::is_true(!named.empty() && named.front()->field("name"), "the premise: the refusal names the giver");
    check::is_true(!of_kind(refused, FactKind::Reason).empty(), "with a reason");
    check::equal(*of_kind(refused, FactKind::Reason).front()->field("reason"),
                 "the work is " + *named.front()->field("name") + "'s to release", "who can release it, by name");
    check::equal(*of_kind(refused, FactKind::Reason).front()->field("why"), std::string("not_theirs_to_release"), "the kind of refusal");
    const FactPacket p = d.respond(giver, {ActKind::AbandonQuest, quest});
    check::is_true(p.outcome == Outcome::Abandoned, "its giver can");
    const std::vector<fwg::QuestRow> abandoned = board(d.world(), fwg::QuestState::Abandoned);
    check::is_true(std::any_of(abandoned.begin(), abandoned.end(), [&](const fwg::QuestRow& q) { return q.quest_id == quest; }),
                   "the board has it abandoned");
    check::is_true(d.respond(giver, {ActKind::AbandonQuest, quest}).outcome == Outcome::Unknown, "and it cannot be given up twice");
}

TEST(a_lapsed_offer_is_refused_in_the_speakers_words) {
    // An offer lapses at a moment of a day, and the board sweeps whole days: between the two it
    // is still on offer, and the world refuses to let it be taken. The generator's message then
    // names quests by id and lists work the speaker may know nothing of; the speaker's reason
    // must hold none of that.
    const fwg::World& w = month_old();
    const double today = fwg::world_clock_of(w).day;
    const std::vector<fwg::QuestRow> offers = board(w, fwg::QuestState::Offered);
    const fwg::QuestRow* soonest = nullptr;
    for (const fwg::QuestRow& q : offers)
        if (q.giver_uid && q.expires_day > today + 1.0 && q.expires_day != std::floor(q.expires_day) &&
            (!soonest || q.expires_day < soonest->expires_day))
            soonest = &q;
    check::is_true(soonest != nullptr, "the premise: an offer that lapses at a moment of a later day");
    const std::string quest = soonest->quest_id, giver = *soonest->giver_uid;
    const double lapse = soonest->expires_day;
    Director d(w);
    d.advance(lapse - today + (std::ceil(lapse) - lapse) / 2.0);  // past the lapse, before the day's sweep
    const std::vector<fwg::QuestRow> offered = board(d.world(), fwg::QuestState::Offered);
    check::is_true(std::any_of(offered.begin(), offered.end(), [&](const fwg::QuestRow& q) { return q.quest_id == quest; }),
                   "the premise: the board has not swept it yet");
    const FactPacket p = d.respond(giver, {ActKind::AcceptQuest, quest});
    check::is_true(p.outcome == Outcome::Refused, "the premise: the world refuses a lapsed offer");
    check::is_true(!of_kind(p, FactKind::Reason).empty(), "with a reason");
    const Fact& why = *of_kind(p, FactKind::Reason).front();
    check::equal(*why.field("why"), std::string("refused_by_world"), "the kind of refusal");
    check::equal(*why.field("reason"), std::string("the offer has lapsed"), "the speaker's words for it");
}

TEST(how_far_is_said_only_roughly) {
    // A speaker has measured nothing: every place and nest comes with a rough `range`, one of
    // the published bands, and the band holds the distance the near read measures. No fact says
    // metres or kilometres, so no voice can read a number out, or round it wrong.
    const fwg::World& w = month_old();
    Director d(w);
    std::size_t ranged = 0;
    std::set<std::string> bands;
    for (const std::string& speaker : {a_giver(), a_broker()}) {
        const FactPacket p = d.respond(speaker, {ActKind::AskSurroundings, ""});
        const std::map<std::string, double> metres = metres_from(w, speaker);
        for (const Fact& f : p.facts) {
            check::is_true(f.field("distance") == nullptr, "no distance field: " + f.subject);
            static const std::regex measure(R"(\d\s?k?m\b)");
            for (const auto& [key, value] : f.fields)
                check::is_true(!std::regex_search(value, measure), "no measure in " + key + ": " + value);
            if (f.kind != FactKind::Place && f.kind != FactKind::Danger) continue;
            const std::string* range = f.field("range");
            check::is_true(range != nullptr, "a range: " + f.subject);
            const auto [from, to] = band_metres(*range);
            const double m = metres.count(f.subject) ? metres.at(f.subject) : -1.0;
            check::is_true(m >= from && m < to, f.subject + " at " + std::to_string(m) + " m is " + *range);
            ++ranged;
            bands.insert(*range);
        }
    }
    check::is_true(ranged > 10, "the premise: places and nests to range, " + std::to_string(ranged));
    check::is_true(bands.size() >= 3, "the premise: more than one band met, " + std::to_string(bands.size()));
}

TEST(work_carries_an_exact_marker_for_the_game) {
    // The speaker says how far only roughly; the game still marks the work on its map exactly:
    // the anchor, and the anchor's terrain node, on the offer and on the work once taken.
    const fwg::World& w = month_old();
    Director d(w);
    const std::string giver = a_giver();
    const FactPacket work = d.respond(giver, {ActKind::AskForWork, ""});
    std::map<std::string, fwg::QuestRow> rows;
    for (const fwg::QuestRow& q : board(w, fwg::QuestState::Offered)) rows.emplace(q.quest_id, q);
    std::size_t marked = 0;
    std::string quest;
    for (const Fact* f : of_kind(work, FactKind::QuestOffer)) {
        const fwg::QuestRow& row = rows.at(f->subject);
        const fwg::HeroQuestHook& h = hook(f->subject);
        if (row.anchor) {
            check::is_true(f->field("marker") != nullptr, "a marker on " + f->subject);
            check::equal(*f->field("marker"), *row.anchor, "the anchor, exactly");
        }
        if (h.has_target_node) {
            check::is_true(f->field("marker_node") != nullptr, "a marker node on " + f->subject);
            check::equal(*f->field("marker_node"), std::to_string(h.target_node), "the anchor's node, exactly");
            ++marked;
            if (quest.empty() && *f->field("from_speaker") == "yes") quest = f->subject;
        }
    }
    check::is_true(marked > 0, "the premise: offers with a node to mark");
    check::is_true(!quest.empty(), "the premise: the giver's own sited offer");
    const FactPacket taken = d.respond(giver, {ActKind::AcceptQuest, quest});
    check::is_true(taken.outcome == Outcome::Accepted, "the premise: taken");
    const Fact* state = of_kind(taken, FactKind::QuestState).front();
    check::is_true(state->field("marker_node") != nullptr, "taken work keeps a marker");
    check::equal(*state->field("marker_node"), std::to_string(hook(quest).target_node), "taken work keeps its marker");
}

TEST(a_hero_with_one_face_speaks_from_their_archetypes_lines) {
    // The key names the written lines a speaker speaks from. A mask -- a public face over a true
    // one -- gets none: its archetype (false_hero, deceiver) is what the face hides.
    const fwg::World& w = month_old();
    const fwg::Hero* plain = nullptr;
    const fwg::Hero* faced = nullptr;
    for (const fwg::Hero& h : w.heroes.people) {
        if (!plain && h.faces.size() == 1 && !h.archetype.empty() && !h.alignment.code.empty()) plain = &h;
        const auto labelled = [&](std::string_view label) {
            return std::any_of(h.faces.begin(), h.faces.end(), [&](const fwg::HeroFace& f) { return f.label == label; });
        };
        if (!faced && labelled("public") && labelled("true")) faced = &h;
    }
    check::is_true(plain != nullptr, "the premise: a hero with one face, of " + std::to_string(w.heroes.people.size()));
    check::is_true(faced != nullptr, "the premise: a masked hero, of " + std::to_string(w.heroes.people.size()));
    Director d(w);
    check::equal(d.respond(plain->uid, {ActKind::Farewell, ""}).persona.voice_key, plain->archetype + "/" + plain->alignment.code,
                 "archetype and alignment, for " + plain->uid);
    check::equal(d.respond(faced->uid, {ActKind::Farewell, ""}).persona.voice_key, std::string(), "none for " + faced->uid);
    check::equal(d.respond(a_broker(), {ActKind::Farewell, ""}).persona.voice_key, std::string(), "none for a roster person");
}

TEST(a_speaker_who_is_gone_refuses_by_name) {
    const fwg::World& w = month_old();
    std::string gone;
    for (const fwg::NpcPerson& person : w.npcs.people)
        if (!person.alive) {
            gone = person.uid;
            break;
        }
    check::is_true(!gone.empty(), "the premise: a month-old world has someone gone");
    Director d(w);
    const FactPacket p = d.respond(gone, {ActKind::NoAct, ""});
    check::is_true(p.outcome == Outcome::Refused, "refused");
    check::is_true(!of_kind(p, FactKind::Reason).empty(), "with a reason");
    check::is_true(!p.persona.name.empty(), "the premise: the one gone has a name");
    check::equal(*of_kind(p, FactKind::Reason).front()->field("reason"), p.persona.name + " is gone", "gone, by name");
    check::equal(*of_kind(p, FactKind::Reason).front()->field("why"), std::string("gone"), "the kind of refusal");
    // Never spoken to, in no conversation: nothing is news between them (contract version 17).
    check::equal(*of_kind(p, FactKind::Memory).front()->field("work_news"), std::string("no"), "no news between strangers");
}

TEST(what_a_speaker_describes_is_real_and_within_reach) {
    const fwg::World& w = month_old();
    std::set<std::string> places, nests;
    for (const auto& s : w.settlements.sites) places.insert(s.uid);
    for (const auto& h : w.humans.hamlets) places.insert(h.id);
    for (const auto& f : w.humans.fortresses) places.insert(f.id);
    for (const auto& k : w.key_locations.sites) places.insert(k.id);
    for (const auto& n : w.beast_nests.sites) nests.insert(n.id);
    Director d(w);
    std::size_t described = 0, dangers = 0;
    for (const std::string& speaker : {a_giver(), a_broker()}) {
        const FactPacket p = d.respond(speaker, {ActKind::AskSurroundings, ""});
        check::is_true(p.outcome == Outcome::Answered, "an answer");
        const std::map<std::string, double> metres = metres_from(w, speaker);
        for (const Fact* f : of_kind(p, FactKind::Place)) {
            ++described;
            check::is_true(places.count(f->subject) == 1, "a place the world holds: " + f->subject);
            check::is_true(metres.count(f->subject) == 1, "within reach: " + f->subject);
        }
        for (const Fact* f : of_kind(p, FactKind::Danger)) {
            ++dangers;
            check::is_true(nests.count(f->subject) == 1, "a nest the world holds: " + f->subject);
            check::is_true(metres.count(f->subject) == 1 && metres.at(f->subject) <= 5050.0, "close by: " + f->subject);
        }
    }
    check::is_true(described > 0, "the premise: places to describe");
    check::is_true(dangers > 0, "the premise: dangers to warn of");
}

TEST(a_speaker_knows_the_places_in_reach_and_not_the_rest) {
    const fwg::World& w = month_old();
    Director d(w);
    const std::string broker = a_broker();
    std::size_t known = 0, unknown = 0;
    for (const auto& site : w.settlements.sites) {
        const FactPacket p = d.respond(broker, {ActKind::AskAboutPlace, site.uid});
        if (p.outcome == Outcome::Answered) {
            ++known;
            check::is_true(metres_from(w, broker).count(site.uid) == 1, "a known city is within reach");
        } else {
            check::is_true(p.outcome == Outcome::Unknown, "otherwise unknown");
            check::is_true(only_memory(p), "and nothing said about it");
            ++unknown;
        }
    }
    check::is_true(known > 0 && unknown > 0, "the premise: cities both within and beyond reach");
}

TEST(on_a_wide_world_a_speaker_knows_only_their_own_reach) {
    // On the small world every reach is the near read's ceiling, so the reach table is invisible
    // there. On a large world the ceiling is wider than a city dweller's 20 km: a city further
    // than that, but inside the ceiling, must be unknown to them.
    fwg::Request request;
    request.seed = 42;
    request.size = 33;
    request.world_size = fwg::parse_world_size("large");
    Director d(fwg::generate(fwg::resolve(request)));
    const fwg::World& w = d.world();
    const double ceiling_km = reach_ceiling_km(w);
    check::is_true(ceiling_km > 25.0, "the premise: a ceiling wider than a city dweller's reach");
    std::size_t witnessed = 0;
    for (const fwg::NpcPerson& person : w.npcs.people) {
        if (!person.alive || person.is_cast || !person.site_kind || *person.site_kind != "city") continue;
        const std::optional<int> node = npc_node(w, person);
        if (!node) continue;
        fwg::NearReadRequest near;
        near.node = *node;
        near.radius_m = ceiling_km * 1000.0;
        near.categories = std::vector<std::string>{"settlements"};
        for (const fwg::NearSettlementRow& city : fwg::read_near(w, near).near.settlements) {
            const FactPacket p = d.respond(person.uid, {ActKind::AskAboutPlace, city.uid});
            if (city.distance_m > 20050.0) {
                check::is_true(p.outcome == Outcome::Unknown, city.uid + " at " + std::to_string(city.distance_m) + " m is past a city dweller's reach");
                ++witnessed;
            } else if (city.distance_m < 19950.0) {
                check::is_true(p.outcome == Outcome::Answered, city.uid + " at " + std::to_string(city.distance_m) + " m is within it");
            }
        }
        if (witnessed >= 3) break;
    }
    check::is_true(witnessed > 0, "the premise: a city past 20 km but inside the ceiling, from some city");
}

TEST(a_speaker_knows_the_people_where_they_stand_and_not_the_far_off) {
    // The oracle is the near read itself: the nodes where some place stands within the ceiling,
    // which is the broker's reach on this world. A hero standing on one of them is known; a hero
    // standing on a node elsewhere is not. Heroes the world places nowhere prove nothing either
    // way and are left out.
    const fwg::World& w = month_old();
    Director d(w);
    const std::string broker = a_broker(/*in_a_city=*/true);
    fwg::PersonReadRequest who;
    who.uid = broker;
    const int here = *fwg::read_person(w, who).person.anchor.node;
    fwg::NearReadRequest near;
    near.node = here;
    near.radius_m = reach_ceiling_km(w) * 1000.0;
    near.categories = std::vector<std::string>{"settlements", "hamlets", "fortresses", "key_locations"};
    const fwg::NearRead read = fwg::read_near(w, near);
    std::set<int> in_reach = {here};
    for (const auto& r : read.near.settlements) in_reach.insert(r.node);
    for (const auto& r : read.near.hamlets) in_reach.insert(r.node);
    for (const auto& r : read.near.fortresses) in_reach.insert(r.node);
    for (const auto& r : read.near.key_locations) in_reach.insert(r.node);
    std::size_t known = 0, unknown = 0, famous = 0;
    for (const fwg::Hero& hero : w.heroes.people) {
        fwg::PersonReadRequest request;
        request.uid = hero.uid;
        const std::optional<int> node = fwg::read_person(w, request).person.anchor.node;
        if (!node) continue;
        const FactPacket p = d.respond(broker, {ActKind::AskAboutPerson, hero.uid});
        // Known by fame beyond reach (contract version 25): the broker's own sovereign, or a hero whose known
        // deed's word went through the broker's realm. A region's width clamps to the ceiling, which is the
        // reach, on this world.
        if (!in_reach.count(*node) && famous_to(w, broker, hero)) {
            check::is_true(p.outcome == Outcome::Answered, hero.uid + " is known by fame beyond reach");
            ++famous;
            continue;
        }
        if (in_reach.count(*node)) {
            check::is_true(p.outcome == Outcome::Answered, hero.uid + " stands within reach");
            check::equal(p.facts.front().subject, hero.uid, "the person asked about");
            ++known;
        } else {
            check::is_true(p.outcome == Outcome::Unknown, hero.uid + " stands beyond reach");
            check::is_true(only_memory(p), "and nothing is said of them");
            ++unknown;
        }
    }
    check::is_true(known > 0 && unknown > 0, "the premise: located heroes both within and beyond reach");
    (void)famous;  // how many, the fame test's to require: this broker's realm may have no famous far off
}

// Contract version 25: fame. "A common NPC should know the legendary heroes or kings in their land, even if
// they have never met them" (the owner, 2026-09-29). A roster person knows their realm's sovereign wherever
// the sovereign stands, and says so, with the deed word of them goes with; asked for "the king", they name
// their own; asked by a name no packet gave, they look it up among those they know; a far sovereign of
// another realm they do not know.
TEST(a_speaker_knows_their_sovereign_and_whom_word_of_a_deed_reaches) {
    const fwg::World& w = month_old();
    Director d(w);
    const auto located = [&](const std::string& uid) {
        fwg::PersonReadRequest who;
        who.uid = uid;
        return fwg::read_person(w, who).person.anchor.node;
    };
    const auto reach_of = [&](int node) {
        fwg::NearReadRequest near;
        near.node = node;
        near.radius_m = reach_ceiling_km(w) * 1000.0;
        near.categories = std::vector<std::string>{"settlements", "hamlets", "fortresses", "key_locations"};
        const fwg::NearRead read = fwg::read_near(w, near);
        std::set<int> out = {node};
        for (const auto& r : read.near.settlements) out.insert(r.node);
        for (const auto& r : read.near.hamlets) out.insert(r.node);
        for (const auto& r : read.near.fortresses) out.insert(r.node);
        for (const auto& r : read.near.key_locations) out.insert(r.node);
        return out;
    };
    // A city dweller of a realm with a sovereign, standing beyond their reach; another realm's sovereign beyond
    // their reach too.
    std::map<std::string, const fwg::Hero*> heroes;
    for (const fwg::Hero& hero : w.heroes.people) heroes.emplace(hero.uid, &hero);
    std::string dweller, own, far;
    for (const fwg::NpcPerson& person : w.npcs.people) {
        if (!person.alive || person.is_cast || !person.site_kind || *person.site_kind != "city") continue;
        const std::string realm = realm_of_roster(w, person);
        const std::string sovereign = sovereign_of(w, realm);
        if (sovereign.empty() || sovereign == person.uid || !heroes.count(sovereign)) continue;
        const std::optional<int> here = npc_node(w, person);
        if (!here) continue;
        const std::set<int> reach = reach_of(*here);
        const std::optional<int> theirs = located(sovereign);
        if (!theirs || reach.count(*theirs)) continue;  // their own sovereign beyond their reach: known by fame alone
        for (const fwg::Hero& hero : w.heroes.people) {
            if (hero.role != "sovereign" || hero.uid == sovereign || hero.realm_uid == realm) continue;
            const std::optional<int> there = located(hero.uid);
            if (!there || reach.count(*there)) continue;
            dweller = person.uid;
            own = sovereign;
            far = hero.uid;
            break;
        }
        if (!dweller.empty()) break;
    }
    check::is_true(!dweller.empty(), "the premise: a city dweller, their sovereign beyond reach, and another realm's too");
    const fwg::Hero* sovereign = heroes.at(own);
    // Their own sovereign: known, and said to be theirs, with fame and the deed they are known for, if any.
    const FactPacket asked = d.respond(dweller, {ActKind::AskAboutPerson, own});
    check::is_true(asked.outcome == Outcome::Answered, "their own sovereign is known");
    const Fact& person = asked.facts.front();
    check::equal(person.subject, own, "the sovereign asked about");
    check::equal(person.field("sovereign") ? *person.field("sovereign") : std::string(), std::string("yes"), "said to be theirs");
    check::equal(person.field("fame") ? *person.field("fame") : std::string(), sovereign->tier, "with their fame");
    const auto said_deed = [&](const FactPacket& p, const fwg::Hero& hero, const std::string& who) {
        const std::vector<const Fact*> deeds = of_kind(p, FactKind::Deed);
        const fwg::HeroDeed* known = known_deed_of(hero);
        if (known == nullptr) {
            check::is_true(deeds.empty(), "no deed for one with none: " + who);
            return;
        }
        check::equal(deeds.size(), std::size_t{1}, "the deed they are known for: " + who);
        check::equal(deeds.front()->subject, known->event_id, "that deed: " + who);
        const std::string* scope = deeds.front()->field("scope");
        check::equal(scope ? *scope : std::string("(none)"), known->scope, "with how far word of it went: " + who);
    };
    said_deed(asked, *sovereign, own);
    // Any hero of one face known for a deed, asked of by someone who stands where they stand: the same.
    std::size_t deeds_said = 0;
    for (const fwg::Hero& hero : w.heroes.people) {
        if (deeds_said >= 3 || hero.faces.size() > 1 || known_deed_of(hero) == nullptr) continue;
        const std::optional<int> there = located(hero.uid);
        if (!there) continue;
        for (const fwg::NpcPerson& p : w.npcs.people) {
            if (!p.alive || p.is_cast || npc_node(w, p) != there) continue;
            const FactPacket about = d.respond(p.uid, {ActKind::AskAboutPerson, hero.uid});
            check::is_true(about.outcome == Outcome::Answered, hero.uid + ", where they stand, is known");
            check::equal(about.facts.front().field("fame") ? *about.facts.front().field("fame") : std::string(), hero.tier,
                         "their fame: " + hero.uid);
            said_deed(about, hero, hero.uid);
            ++deeds_said;
            break;
        }
    }
    check::is_true(deeds_said > 0, "the premise: heroes known for a deed, and someone where they stand");
    // Another realm's, beyond reach, of no wide fame: not known.
    check::is_true(d.respond(dweller, {ActKind::AskAboutPerson, far}).outcome == Outcome::Unknown,
                   "another realm's sovereign, beyond their reach, which on this world is a region's width");
    // Asked for the king: their own.
    const FactPacket king = d.respond(dweller, {ActKind::AskAboutPerson, "=the king"});
    check::is_true(king.outcome == Outcome::Answered && king.facts.front().subject == own, "the king is their own sovereign");
    // By a name no packet gave: the sovereign's own name; a name nobody bears.
    const FactPacket by_name = d.respond(dweller, {ActKind::AskAboutPerson, "=" + sovereign->name});
    check::is_true(by_name.outcome == Outcome::Answered && by_name.facts.front().subject == own, "their sovereign by name");
    check::is_true(d.respond(dweller, {ActKind::AskAboutPerson, "=Gandalf the Grey"}).outcome == Outcome::Unknown, "a name nobody bears");
    check::throws([&] { (void)d.respond(dweller, {ActKind::AskAboutPerson, "=  "}); }, "no words");
    // A king named is that king, never taken for their own: the far one, whom they do not know.
    check::is_true(d.respond(dweller, {ActKind::AskAboutPerson, "=King " + heroes.at(far)->name}).outcome == Outcome::Unknown,
                   "another realm's king, by name: not known, and not their own");
    // A realm named by a city of it: its sovereign. Of the dweller's own realm, their own.
    const auto city_of = [&](const std::string& realm_uid) {
        for (const fwg::HeroRealm& realm : w.heroes.realms)
            if (realm.uid == realm_uid)
                for (const auto& site : w.settlements.sites)
                    if (!realm.city_uids.empty() && site.uid == realm.city_uids.front()) return site.name;
        check::fail("a city of " + realm_uid);
    };
    const std::string home_realm = realm_of_roster(w, roster_person(w, dweller));
    const FactPacket of_home = d.respond(dweller, {ActKind::AskAboutPerson, "=the king of " + city_of(home_realm)});
    check::is_true(of_home.outcome == Outcome::Answered && of_home.facts.front().subject == own, "the king of their own city: theirs");
    // Another realm's sovereign who stands within someone's reach: known, by the realm's city and by name.
    std::string near_one, neighbour, neighbours_realm;
    for (const fwg::NpcPerson& p : w.npcs.people) {
        if (!p.alive || p.is_cast || !p.site_kind || *p.site_kind != "city") continue;
        const std::optional<int> here = npc_node(w, p);
        if (!here) continue;
        const std::string realm = realm_of_roster(w, p);
        const std::set<int> reach = reach_of(*here);
        for (const fwg::HeroRealm& other : w.heroes.realms) {
            if (other.uid == realm || other.sovereign_uid.empty() || other.city_uids.empty()) continue;
            const std::optional<int> there = located(other.sovereign_uid);
            if (!there || !reach.count(*there)) continue;
            near_one = p.uid;
            neighbour = other.sovereign_uid;
            neighbours_realm = other.uid;
            break;
        }
        if (!near_one.empty()) break;
    }
    check::is_true(!near_one.empty(), "the premise: someone with another realm's sovereign in reach");
    const FactPacket of_theirs = d.respond(near_one, {ActKind::AskAboutPerson, "=the queen of " + city_of(neighbours_realm)});
    check::is_true(of_theirs.outcome == Outcome::Answered && of_theirs.facts.front().subject == neighbour,
                   "the sovereign of a realm named by its city");
    const FactPacket by_title = d.respond(near_one, {ActKind::AskAboutPerson, "=King " + heroes.at(neighbour)->name});
    check::is_true(by_title.outcome == Outcome::Answered && by_title.facts.front().subject == neighbour, "a king by name");
    // Replayed, the same.
    Director again = orchestrator::replay(month_old(), d.inputs());
    const FactPacket replayed = again.respond(dweller, {ActKind::AskAboutPerson, "=the king"});
    check::equal(text_of(replayed), text_of(d.respond(dweller, {ActKind::AskAboutPerson, "=the king"})), "replay says the same");
}

namespace {

// The heroes whose known deed's word went `scope`, with where each stands.
std::vector<std::pair<int, const fwg::Hero*>> known_through(const fwg::World& world, const std::string& scope) {
    std::vector<std::pair<int, const fwg::Hero*>> out;
    for (const fwg::Hero& hero : world.heroes.people) {
        const fwg::HeroDeed* deed = known_deed_of(hero);
        if (deed == nullptr || deed->scope != scope) continue;
        fwg::PersonReadRequest who;
        who.uid = hero.uid;
        if (const std::optional<int> node = fwg::read_person(world, who).person.anchor.node) out.emplace_back(*node, &hero);
    }
    return out;
}

// A region's width, as far as a renowned hero's own reach; and a roster person's own reach, by where they live.
constexpr double kRegionM = 40000.0;
double roster_reach_m(const fwg::NpcPerson& person) { return person.site_kind && *person.site_kind == "city" ? 20000.0 : 10000.0; }

}  // namespace

// Contract version 25, on the wide world: a hero whose known deed's word went through a region is known to a
// city dweller beyond their own reach but within a region's width of where the hero stands, and not past it,
// where the near read still reaches or beyond it.
TEST(word_of_a_regional_deed_carries_a_regions_width) {
    const fwg::World& w = wide();
    Director d(w);
    check::is_true(reach_ceiling_km(w) * 1000.0 > kRegionM + 1000.0, "the premise: the near read reaches past a region's width");
    const std::vector<std::pair<int, const fwg::Hero*>> regional = known_through(w, "regional");
    check::is_true(!regional.empty(), "the premise: heroes known through a region");
    std::size_t within = 0, past = 0, out_of_sight = 0;
    for (const fwg::NpcPerson& person : w.npcs.people) {
        if (within >= 3 && past >= 3 && out_of_sight >= 3) break;
        if (!person.alive || person.is_cast || !person.site_kind || *person.site_kind != "city") continue;
        const std::optional<int> here = npc_node(w, person);
        if (!here) continue;
        const std::string sovereign = sovereign_of(w, realm_of_roster(w, person));
        const std::map<int, double> metres = place_metres(w, *here);
        for (const auto& [node, hero] : regional) {
            if (hero->uid == sovereign) continue;
            const auto m = metres.find(node);
            const double far = m == metres.end() ? 1e12 : m->second;
            if (far > roster_reach_m(person) + 50.0 && far < kRegionM - 50.0 && within < 3) {
                check::is_true(d.respond(person.uid, {ActKind::AskAboutPerson, hero->uid}).outcome == Outcome::Answered,
                               hero->uid + " at " + std::to_string(far) + " m: within a region's width, known");
                ++within;
            } else if (m != metres.end() && far > kRegionM + 50.0 && past < 3) {
                check::is_true(d.respond(person.uid, {ActKind::AskAboutPerson, hero->uid}).outcome == Outcome::Unknown,
                               hero->uid + " at " + std::to_string(far) + " m: past a region's width, not known");
                ++past;
            } else if (m == metres.end() && out_of_sight < 3) {
                check::is_true(d.respond(person.uid, {ActKind::AskAboutPerson, hero->uid}).outcome == Outcome::Unknown,
                               hero->uid + " beyond the near read, not known");
                ++out_of_sight;
            }
        }
    }
    check::is_true(within > 0 && past > 0 && out_of_sight > 0,
                   "the premise: regional heroes within a region's width, past it inside the near read, and beyond it");
}

// Contract version 25, on the wide world: word of a realm's deed goes all through the hero's realm, however far
// from where they stand, and a region's width past its borders, but no further. Candidates are found from where
// each hero stands and confirmed from where each person stands, as the director reads it.
TEST(word_of_a_realms_deed_goes_through_the_realm_and_a_regions_width_past_it) {
    const fwg::World& w = wide();
    Director d(w);
    const std::vector<std::pair<int, const fwg::Hero*>> famous = known_through(w, "realm");
    check::is_true(!famous.empty(), "the premise: heroes known through their realm");
    std::size_t through = 0, past_border = 0, beyond = 0;
    for (const auto& [node, hero] : famous) {
        const std::map<int, double> around = place_metres(w, node);
        for (const fwg::NpcPerson& person : w.npcs.people) {
            if (through >= 3 && past_border >= 3 && beyond >= 3) break;
            if (!person.alive || person.is_cast) continue;
            const std::optional<int> here = npc_node(w, person);
            if (!here) continue;
            const std::string realm = realm_of_roster(w, person);
            if (hero->uid == sovereign_of(w, realm)) continue;
            const bool same = !realm.empty() && realm == hero->realm_uid;
            const auto a = around.find(*here);
            const double rough = a == around.end() ? 1e12 : a->second;
            const bool wanted = (same && rough > kRegionM + 50.0 && through < 3) ||
                                (!same && rough > roster_reach_m(person) + 50.0 && rough < kRegionM - 50.0 && past_border < 3) ||
                                (!same && rough > kRegionM + 50.0 && beyond < 3);
            if (!wanted) continue;
            const std::map<int, double> theirs = place_metres(w, *here);
            const auto t = theirs.find(node);
            const double far = t == theirs.end() ? 1e12 : t->second;
            if (far <= roster_reach_m(person) + 50.0) continue;
            const FactPacket p = d.respond(person.uid, {ActKind::AskAboutPerson, hero->uid});
            const std::string where = hero->uid + " at " + std::to_string(far) + " m from " + person.uid;
            if (same && far > kRegionM + 50.0 && through < 3) {
                check::is_true(p.outcome == Outcome::Answered, where + ": of their realm, past a region's width, known");
                ++through;
            } else if (!same && far < kRegionM - 50.0 && past_border < 3) {
                check::is_true(p.outcome == Outcome::Answered, where + ": outside their realm, within a region's width, known");
                ++past_border;
            } else if (!same && far > kRegionM + 50.0 && beyond < 3) {
                check::is_true(p.outcome == Outcome::Unknown, where + ": outside their realm, past a region's width, not known");
                ++beyond;
            }
        }
    }
    check::is_true(through > 0 && past_border > 0 && beyond > 0,
                   "the premise: people of a famous hero's realm past a region's width, and of others within it and past it (" +
                       std::to_string(famous.size()) + " heroes: " + std::to_string(through) + ", " + std::to_string(past_border) +
                       ", " + std::to_string(beyond) + ")");
}

TEST(an_offer_names_its_anchor_itself_and_not_what_stands_beside_it) {
    // A pretender's "take back Linbrig" is anchored on Linbrig's ruins, and a hamlet stands on
    // the same node: the offer must name the ruins. An anchor the world's blocks do not name --
    // a ley node, a shrine -- gets no `where`, since its stated purpose says where it is.
    const fwg::World& w = month_old();
    std::map<std::string, std::string> ruins, cities;
    for (const auto& r : w.ruins) ruins.emplace(r.uid, "the ruins of " + r.name);
    for (const auto& s : w.settlements.sites) cities.emplace(s.uid, s.name);
    std::set<std::string> named;
    for (const auto& s : w.settlements.sites) named.insert(s.uid);
    for (const auto& r : w.ruins) named.insert(r.uid);
    for (const auto& h : w.humans.hamlets) { named.insert(h.id); named.insert(h.uid); }
    for (const auto& f : w.humans.fortresses) { named.insert(f.id); named.insert(f.uid); }
    for (const auto& k : w.key_locations.sites) named.insert(k.id);
    for (const auto& n : w.beast_nests.sites) named.insert(n.id);
    for (const auto& r : w.heroes.relics) named.insert(r.uid);
    Director d(w);
    std::set<std::string> givers;
    std::map<std::string, std::string> anchor_of;
    for (const fwg::QuestRow& q : board(w, fwg::QuestState::Offered)) {
        if (q.giver_uid) givers.insert(*q.giver_uid);
        anchor_of.emplace(q.quest_id, q.anchor.value_or(""));
    }
    std::size_t at_ruins = 0, at_cities = 0, unnamed = 0;
    for (const std::string& giver : givers) {
        const FactPacket work = d.respond(giver, {ActKind::AskForWork, ""});  // alive for the loop
        for (const Fact* f : of_kind(work, FactKind::QuestOffer)) {
            const std::string& anchor = anchor_of.at(f->subject);
            const std::string* where = f->field("where");
            if (ruins.count(anchor)) {
                check::is_true(where && *where == ruins.at(anchor), f->subject + " names its ruins");
                ++at_ruins;
            } else if (cities.count(anchor)) {
                check::is_true(where && *where == cities.at(anchor), f->subject + " names its city");
                ++at_cities;
            } else if (!named.count(anchor)) {
                check::is_true(where == nullptr, f->subject + " has no where the world can name");
                ++unnamed;
            }
        }
    }
    check::is_true(at_ruins > 0 && at_cities > 0 && unnamed > 0, "the premise: offers at ruins, at cities and at unnamed anchors");
}

TEST(a_stranger_sees_the_public_face_and_nothing_the_card_keeps_back) {
    const fwg::World& w = month_old();
    Director d(w);
    std::size_t hidden_faces = 0, checked = 0;
    for (const fwg::Hero& hero : w.heroes.people) {
        if (hero.status != "living") continue;
        // A beast's and a fractured heart's other face shows on its own days, by design: a test
        // of their own covers them.
        const auto has = [&](std::string_view label) {
            return std::any_of(hero.faces.begin(), hero.faces.end(), [&](const fwg::HeroFace& f) { return f.label == label; });
        };
        if (has("unleashed") || has("second")) continue;
        const FactPacket p = d.respond(hero.uid, {ActKind::NoAct, ""});
        const std::string said = text_of(p);
        const fwg::HeroFace* shown = nullptr;
        for (const std::string_view label : {"public", "first", "calm"})
            for (const fwg::HeroFace& f : hero.faces)
                if (!shown && f.label == label) shown = &f;
        if (!shown) continue;
        ++checked;
        check::equal(p.persona.name, shown->name, "the name a stranger hears");
        check::equal(p.persona.manner, shown->persona.manner, "the shown face's manner");
        // What the card keeps to itself stays out of the packet.
        for (const std::string* kept : {&shown->persona.lies, &shown->persona.belief, &shown->persona.wants,
                                        &shown->persona.fears, &shown->persona.tells})
            if (kept->size() > 12) check::is_true(said.find(*kept) == std::string::npos, "the card's secret stays out: " + *kept);
        // And a hidden face's card stays out wherever the shown face does not say the same: a
        // true face's manner can be a sentence of the public one's ("Deceptions that are
        // technically true.").
        std::string shown_says = shown->name + "\n" + shown->apparent_role + "\n" + shown->persona.manner + "\n" +
                                 shown->persona.voice + "\n" + shown->persona.toward_player + "\n" + shown->persona.line + "\n";
        for (const std::string& line : shown->persona.never) shown_says += line + "\n";
        for (const fwg::HeroFace& f : hero.faces) {
            if (&f == shown || f.label == "public" || f.label == "first" || f.label == "calm") continue;
            ++hidden_faces;
            for (const std::string* s : {&f.persona.manner, &f.persona.voice, &f.persona.toward_player, &f.persona.line})
                if (s->size() > 12 && said.find(*s) != std::string::npos)
                    check::is_true(shown_says.find(*s) != std::string::npos, "the hidden face stays hidden: " + *s);
        }
    }
    check::is_true(checked > 100, "the premise: many heroes to hear");
    check::is_true(hidden_faces > 0, "the premise: at least one hidden face in this world");
}

TEST(the_same_inputs_replay_to_the_same_world) {
    Director d(month_old());
    const std::string giver = a_giver();
    const std::string broker = a_broker();
    const std::string quest = of_kind(d.respond(giver, {ActKind::AskForWork, ""}), FactKind::QuestOffer).front()->subject;
    (void)d.respond(broker, {ActKind::AskForWork, ""});
    (void)d.respond(giver, {ActKind::AcceptQuest, quest});
    d.advance(12.0);
    (void)d.respond(giver, {ActKind::AskSurroundings, ""});
    (void)d.respond(broker, {ActKind::AskAboutPerson, giver});
    (void)d.respond(giver, {ActKind::AbandonQuest, quest});
    (void)d.respond(broker, {ActKind::NoAct, ""});
    (void)d.respond(giver, {ActKind::Farewell, ""});
    check::is_true(d.world().history.operations.size() > month_old().history.operations.size(), "the session changed the world");
    const Director again = orchestrator::replay(month_old(), d.inputs());
    check::equal(again.inputs().size(), d.inputs().size(), "every input re-applied");
    check::is_true(json_of(again.world()) == json_of(d.world()), "a byte-identical world");
}

TEST(the_same_act_on_the_same_world_says_the_same_thing) {
    const std::string broker = a_broker();
    Director one(month_old());
    Director two(month_old());
    for (const ActKind kind : {ActKind::AskSurroundings, ActKind::AskForWork}) {
        check::equal(text_of(one.respond(broker, {kind, ""})), text_of(two.respond(broker, {kind, ""})),
                     std::string("packets for ") + std::string(orchestrator::to_string(kind)));
    }
}

TEST(a_callers_mistake_is_an_error_and_records_nothing) {
    Director d(month_old());
    check::throws([&] { (void)d.respond("no-such-person", {ActKind::AskForWork, ""}); }, "an unknown speaker");
    check::throws([&] { (void)d.respond(a_giver(), {ActKind::AcceptQuest, ""}); }, "an act without its target");
    check::throws([&] { d.advance(-1.0); }, "a negative span");
    check::throws([&] { d.advance(std::nan("")); }, "a span that is not a number");
    check::throws([&] { d.advance(fwg::kAgeDays); }, "an age");
    check::is_true(d.inputs().empty(), "nothing recorded");
}

TEST(the_world_changes_exactly_on_the_acts_the_contract_says_change_it) {
    // Every act of the contract, granted in turn: the world document changes exactly when
    // act_changes_world says so. A game confirms those acts with the player, so this is what
    // makes a confirmation enough.
    Director d(month_old());
    const std::string giver = a_giver();
    const FactPacket work = d.respond(giver, {ActKind::AskForWork, ""});
    const std::string quest = of_kind(work, FactKind::QuestOffer).front()->subject;
    const FactPacket around = d.respond(giver, {ActKind::AskSurroundings, ""});
    const std::vector<const Fact*> places = of_kind(around, FactKind::Place);
    check::is_true(!places.empty(), "the premise: a place in reach to ask about");
    const std::string place = places.front()->subject;
    // In an order where each can be granted: declined before it is taken, taken before it is given up.
    const std::vector<std::pair<ActKind, std::string>> acts = {
        {ActKind::Greet, ""},           {ActKind::SmallTalk, ""},         {ActKind::AskSurroundings, ""},
        {ActKind::AskSafety, ""},       {ActKind::AskAboutPlace, place},  {ActKind::AskAboutPerson, giver},
        {ActKind::AskForWork, ""},      {ActKind::DeclineQuest, quest},   {ActKind::AcceptQuest, quest},
        {ActKind::AbandonQuest, quest}, {ActKind::Answer, ""},            {ActKind::Thank, ""},
        {ActKind::Insult, ""},          {ActKind::Confront, ""},          {ActKind::NoAct, ""},
        {ActKind::Apologize, ""},       {ActKind::Calm, ""},              {ActKind::AskAboutThem, ""},
        {ActKind::Complain, ""},        {ActKind::Introduce, "Aldric"},   {ActKind::Farewell, ""}};
    std::set<ActKind> kinds;
    for (const auto& act : acts) kinds.insert(act.first);
    check::equal(kinds.size(), static_cast<std::size_t>(orchestrator::kActCount), "the premise: every act of the contract");
    for (const auto& [kind, target] : acts) {
        const std::string name(orchestrator::to_string(kind));
        const std::string before = json_of(d.world());
        const FactPacket p = d.respond(giver, {kind, target});
        check::is_true(p.outcome != Outcome::Refused && p.outcome != Outcome::Unknown, "the premise: " + name + " granted");
        check::equal(json_of(d.world()) != before, orchestrator::act_changes_world(kind), "the world changed on " + name);
    }
}

TEST(no_act_and_farewell_answer_and_change_nothing) {
    Director d(month_old());
    const std::string broker = a_broker();
    const std::string before = json_of(d.world());
    const FactPacket confused = d.respond(broker, {ActKind::NoAct, ""});
    check::is_true(confused.outcome == Outcome::NotUnderstood && only_memory(confused), "not understood, nothing said");
    check::is_true(!confused.persona.role.empty(), "but still in character");
    const FactPacket bye = d.respond(broker, {ActKind::Farewell, ""});
    check::is_true(bye.outcome == Outcome::Parted && only_memory(bye), "parted");
    check::is_true(json_of(d.world()) == before, "the world unchanged");
    check::equal(d.inputs().size(), std::size_t{2}, "both recorded, for replay");
}
