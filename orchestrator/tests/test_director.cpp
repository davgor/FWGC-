// The director against a real world: seed 42, size 33, a month after founding, when the quest
// board has opened. Every test that looks for something first checks the world holds it, so a
// world that changed shape fails loudly here rather than passing over nothing.
#include "orchestrator/director.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <optional>
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

double km_of(const Fact& f) {
    const std::string* distance = f.field("distance");
    if (!distance) check::fail("a distance on " + f.subject);
    return *distance == "here" ? 0.0 : std::strtod(distance->c_str(), nullptr);
}

double reach_ceiling_km(const fwg::World& world) {
    return world.effective.globe_radius * fwg::kNearRadiusFraction / 1000.0;
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

std::vector<const Fact*> of_kind(const FactPacket& p, FactKind kind) {
    std::vector<const Fact*> out;
    for (const Fact& f : p.facts)
        if (f.kind == kind) out.push_back(&f);
    return out;
}

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
                                           "difficulty", "where", "distance", "days_left"};
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
}

TEST(giving_up_work_goes_back_to_its_giver_and_nobody_else) {
    Director d(month_old());
    const std::string giver = a_giver();
    const std::string broker = a_broker();
    const std::string quest = of_kind(d.respond(giver, {ActKind::AskForWork, ""}), FactKind::QuestOffer).front()->subject;
    check::is_true(d.respond(giver, {ActKind::AcceptQuest, quest}).outcome == Outcome::Accepted, "taken first");
    check::is_true(d.respond(broker, {ActKind::AbandonQuest, quest}).outcome == Outcome::Refused, "a stranger cannot release it");
    const FactPacket p = d.respond(giver, {ActKind::AbandonQuest, quest});
    check::is_true(p.outcome == Outcome::Abandoned, "its giver can");
    const std::vector<fwg::QuestRow> abandoned = board(d.world(), fwg::QuestState::Abandoned);
    check::is_true(std::any_of(abandoned.begin(), abandoned.end(), [&](const fwg::QuestRow& q) { return q.quest_id == quest; }),
                   "the board has it abandoned");
    check::is_true(d.respond(giver, {ActKind::AbandonQuest, quest}).outcome == Outcome::Unknown, "and it cannot be given up twice");
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
        for (const Fact* f : of_kind(p, FactKind::Place)) {
            ++described;
            check::is_true(places.count(f->subject) == 1, "a place the world holds: " + f->subject);
            check::is_true(km_of(*f) <= reach_ceiling_km(w) + 0.05, "within reach: " + f->subject);
        }
        for (const Fact* f : of_kind(p, FactKind::Danger)) {
            ++dangers;
            check::is_true(nests.count(f->subject) == 1, "a nest the world holds: " + f->subject);
            check::is_true(km_of(*f) <= 5.05, "close by: " + f->subject);
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
            check::is_true(km_of(p.facts.front()) <= reach_ceiling_km(w) + 0.05, "a known city is within reach");
        } else {
            check::is_true(p.outcome == Outcome::Unknown, "otherwise unknown");
            check::is_true(p.facts.empty(), "and nothing said about it");
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
    std::size_t known = 0, unknown = 0;
    for (const fwg::Hero& hero : w.heroes.people) {
        fwg::PersonReadRequest request;
        request.uid = hero.uid;
        const std::optional<int> node = fwg::read_person(w, request).person.anchor.node;
        if (!node) continue;
        const FactPacket p = d.respond(broker, {ActKind::AskAboutPerson, hero.uid});
        if (in_reach.count(*node)) {
            check::is_true(p.outcome == Outcome::Answered, hero.uid + " stands within reach");
            check::equal(p.facts.front().subject, hero.uid, "the person asked about");
            ++known;
        } else {
            check::is_true(p.outcome == Outcome::Unknown, hero.uid + " stands beyond reach");
            check::is_true(p.facts.empty(), "and nothing is said of them");
            ++unknown;
        }
    }
    check::is_true(known > 0 && unknown > 0, "the premise: located heroes both within and beyond reach");
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
        // And a hidden face's card stays out wherever it differs from the one shown.
        for (const fwg::HeroFace& f : hero.faces) {
            if (&f == shown || f.label == "public" || f.label == "first" || f.label == "calm") continue;
            ++hidden_faces;
            for (const std::string* s : {&f.persona.manner, &f.persona.voice, &f.persona.toward_player, &f.persona.line})
                if (s->size() > 12 && said.find(*s) != std::string::npos) {
                    const bool shared = *s == shown->persona.manner || *s == shown->persona.voice ||
                                        *s == shown->persona.toward_player || *s == shown->persona.line;
                    check::is_true(shared, "the hidden face stays hidden: " + *s);
                }
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

TEST(no_act_and_farewell_answer_and_change_nothing) {
    Director d(month_old());
    const std::string broker = a_broker();
    const std::string before = json_of(d.world());
    const FactPacket confused = d.respond(broker, {ActKind::NoAct, ""});
    check::is_true(confused.outcome == Outcome::NotUnderstood && confused.facts.empty(), "not understood, nothing said");
    check::is_true(!confused.persona.role.empty(), "but still in character");
    const FactPacket bye = d.respond(broker, {ActKind::Farewell, ""});
    check::is_true(bye.outcome == Outcome::Parted && bye.facts.empty(), "parted");
    check::is_true(json_of(d.world()) == before, "the world unchanged");
    check::equal(d.inputs().size(), std::size_t{2}, "both recorded, for replay");
}
