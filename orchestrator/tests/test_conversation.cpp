// Conversation against a real world: seed 99, size 33, a month after founding. Seed 99 holds
// every kind of hidden face -- deceivers, a beast, a fractured heart -- and masks whose rival or
// kin can be asked about them. Every test that looks for something first checks the world holds
// it, so a world that changed shape fails loudly here rather than passing over nothing.
#include "orchestrator/director.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "check.hpp"
#include "knowledge.hpp"
#include "fwg/api/quests.hpp"
#include "fwg/api/read.hpp"
#include "fwg/api/serialize.hpp"
#include "fwg/api/weather.hpp"
#include "fwg/api/world_time.hpp"

namespace {

using orchestrator::ActKind;
using orchestrator::Director;
using orchestrator::Fact;
using orchestrator::FactKind;
using orchestrator::FactPacket;
using orchestrator::Outcome;
using Memory = std::map<std::string, std::string>;

const fwg::World& faced_world() {
    static const fwg::World world = [] {
        fwg::Request request;
        request.seed = 99;
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

std::vector<fwg::QuestRow> quest_board(const fwg::World& world, fwg::QuestState state) {
    fwg::QuestsReadRequest request;
    request.states = std::vector<fwg::QuestState>{state};
    return fwg::read_quests(world, request).quests;
}

bool on_board(const fwg::World& world, fwg::QuestState state, const std::string& quest) {
    const std::vector<fwg::QuestRow> rows = quest_board(world, state);
    return std::any_of(rows.begin(), rows.end(), [&](const fwg::QuestRow& q) { return q.quest_id == quest; });
}

// Copies, so nothing read from a packet outlives it.
std::vector<Fact> facts_of(const FactPacket& p, FactKind kind) {
    std::vector<Fact> out;
    for (const Fact& f : p.facts)
        if (f.kind == kind) out.push_back(f);
    return out;
}

std::string value(const Fact& fact, std::string_view key) {
    const std::string* v = fact.field(key);
    if (v == nullptr) check::fail("a " + std::string(key) + " field on " + fact.subject);
    return *v;
}

// The one Memory fact every packet carries, about its speaker.
Memory memory(const FactPacket& p) {
    const std::vector<Fact> kept = facts_of(p, FactKind::Memory);
    if (kept.size() != 1) check::fail("one Memory fact, got " + std::to_string(kept.size()));
    check::equal(kept.front().subject, p.speaker, "a memory of the speaker");
    return Memory(kept.front().fields.begin(), kept.front().fields.end());
}

bool only_memory(const FactPacket& p) { return p.facts.size() == 1 && p.facts.front().kind == FactKind::Memory; }

// Everything a packet would put in front of a voice, as one text.
std::string text_of(const FactPacket& p) {
    std::string out = std::string(orchestrator::to_string(p.outcome)) + "\n" + p.persona.name + "\n" + p.persona.role + "\n" +
                      p.persona.place + "\n" + p.persona.manner + "\n" + p.persona.voice + "\n" + p.persona.toward_player +
                      "\n" + p.persona.line + "\n" + p.persona.voice_key + "\n" + p.persona.face + "\n";
    for (const std::string& line : p.persona.never) out += line + "\n";
    for (const Fact& f : p.facts) {
        out += std::string(orchestrator::to_string(f.kind)) + " " + f.subject + "\n";
        for (const auto& [key, v] : f.fields) out += key + "=" + v + "\n";
    }
    return out;
}

double today(const Director& d) { return fwg::world_clock_of(d.world()).day; }

std::optional<int> node_of(const fwg::World& world, const std::string& uid) {
    fwg::PersonReadRequest request;
    request.uid = uid;
    return fwg::read_person(world, request).person.anchor.node;
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

// How near a band is: "here" 0 to "distant" 5 (orchestrator/contract.hpp's bands).
int band_rank(const std::string& range) {
    const std::vector<std::string> bands = {"here", "close", "near", "walk", "far", "distant"};
    const auto at = std::find(bands.begin(), bands.end(), range);
    if (at == bands.end()) check::fail("a published band, not " + range);
    return static_cast<int>(at - bands.begin());
}

// Living roster people who give no quest, one to a node, city folk first: the speakers most of
// these tests talk to. City folk know the whole of the small world's near-read ceiling.
std::vector<std::string> townsfolk(std::size_t count) {
    const fwg::World& w = faced_world();
    std::set<std::string> givers;
    for (const fwg::QuestRow& q : quest_board(w, fwg::QuestState::Offered))
        if (q.giver_uid) givers.insert(*q.giver_uid);
    std::set<int> nodes;
    std::vector<std::string> out;
    for (const bool city : {true, false})
        for (const fwg::NpcPerson& p : w.npcs.people) {
            if (out.size() == count) return out;
            if (!p.alive || p.is_cast || givers.count(p.uid)) continue;
            if (city != (p.site_kind && *p.site_kind == "city")) continue;
            const std::optional<int> node = npc_node(w, p);
            if (!node || !nodes.insert(*node).second) continue;
            out.push_back(p.uid);
        }
    if (out.size() < count) check::fail("the premise: " + std::to_string(count) + " roster people standing apart, got " + std::to_string(out.size()));
    return out;
}

bool has_face(const fwg::Hero& hero, std::string_view label) {
    return std::any_of(hero.faces.begin(), hero.faces.end(), [&](const fwg::HeroFace& f) { return f.label == label; });
}

const fwg::HeroFace& face(const fwg::Hero& hero, std::string_view label) {
    for (const fwg::HeroFace& f : hero.faces)
        if (f.label == label) return f;
    check::fail(hero.uid + " to have a " + std::string(label) + " face");
}

// Living heroes, standing somewhere, with both faces.
std::vector<const fwg::Hero*> heroes_with(std::string_view shown, std::string_view hidden) {
    std::vector<const fwg::Hero*> out;
    for (const fwg::Hero& h : faced_world().heroes.people)
        if (h.status == "living" && has_face(h, shown) && has_face(h, hidden) && node_of(faced_world(), h.uid)) out.push_back(&h);
    if (out.empty()) check::fail("the premise: a living hero with a " + std::string(shown) + " and a " + std::string(hidden) + " face");
    return out;
}

std::string key_of(const fwg::Hero& hero, const fwg::HeroFace& f) { return hero.archetype + "/" + f.alignment.code; }

// A giver's own open offers, from the giver, in the order they are said.
std::vector<std::string> own_offers(Director& d, const std::string& giver) {
    const FactPacket work = d.respond(giver, {ActKind::AskForWork, ""});
    std::vector<std::string> out;
    for (const Fact& f : facts_of(work, FactKind::QuestOffer))
        if (value(f, "from_speaker") == "yes") out.push_back(f.subject);
    return out;
}

// Givers with open offers of their own, standing somewhere.
std::vector<std::string> givers() {
    std::vector<std::string> out;
    for (const fwg::QuestRow& q : quest_board(faced_world(), fwg::QuestState::Offered))
        if (q.giver_uid && q.giver && q.giver->anchor.node && q.giver->liveness == fwg::Liveness::Present &&
            std::find(out.begin(), out.end(), *q.giver_uid) == out.end())
            out.push_back(*q.giver_uid);
    if (out.size() < 2) check::fail("the premise: two givers with open offers");
    return out;
}

// Advance to the middle of the first whole day, from tomorrow on, that `wanted` accepts.
void advance_to_a_day(Director& d, const std::function<bool(double)>& wanted, const std::string& what) {
    const double now = today(d);
    for (double day = std::floor(now) + 1.0; day < now + 90.0; day += 1.0)
        if (wanted(day)) {
            d.advance(day + 0.5 - now);
            return;
        }
    check::fail("the premise: " + what + " within 90 days");
}

// The full moon's day, or the day either side of it: when the contract says a beast is unleashed.
bool full_moon_near(const fwg::World& world, double day) {
    for (const double d : {day - 1.0, day, day + 1.0}) {
        const std::optional<fwg::MoonPhase> moon = fwg::moon_phase_on(world, std::floor(d));
        if (!moon) check::fail("the premise: a world with a moon");
        if (moon->full) return true;
    }
    return false;
}

}  // namespace

TEST(a_speaker_remembers_the_player_from_one_conversation_to_the_next) {
    Director d(faced_world());
    const std::string x = townsfolk(1).front();
    const FactPacket hello = d.respond(x, {ActKind::Greet, ""});
    check::is_true(hello.outcome == Outcome::Greeted, "a greeting returned");
    Memory m = memory(hello);
    check::equal(m["met"], std::string("first"), "a first meeting");
    check::equal(m["times"], std::string("0"), "nothing before it");
    check::equal(m["since"], std::string(), "no last time");
    check::equal(m["turn"], std::string("1"), "the first turn");
    check::equal(m["mood"], std::string("neutral"), "a stranger's regard");
    check::equal(memory(d.respond(x, {ActKind::SmallTalk, ""}))["turn"], std::string("2"), "the conversation goes on");
    check::equal(memory(d.respond(x, {ActKind::Farewell, ""}))["turn"], std::string("3"), "to its farewell");
    m = memory(d.respond(x, {ActKind::Greet, ""}));
    check::equal(m["met"], std::string("again"), "met again");
    check::equal(m["times"], std::string("1"), "once before");
    check::equal(m["since"], std::string("today"), "earlier today");
    check::equal(m["turn"], std::string("1"), "a new conversation");
    int times = 2;
    for (const auto& [days, since] :
         std::vector<std::pair<double, std::string>>{{1.0, "yesterday"}, {3.0, "days"}, {20.0, "weeks"}, {100.0, "months"}}) {
        d.advance(days);
        m = memory(d.respond(x, {ActKind::Greet, ""}));
        check::equal(m["times"], std::to_string(times++), "every conversation counted");
        check::equal(m["since"], since, "how long since, after " + std::to_string(days) + " days");
    }
    const Director::Acquaintance* a = d.acquaintance(x);
    check::is_true(a != nullptr && a->conversations == 6, "six conversations held");
}

TEST(since_is_by_the_calendar_not_by_the_hours_between) {
    Director d(faced_world());
    const std::string x = townsfolk(1).front();
    const auto now = [&d] { return fwg::world_clock_of(d.world()).day; };
    // Late in a day, so the next begins within hours.
    if (now() - std::floor(now()) < 0.5) d.advance(0.5);
    const double first = now();
    (void)d.respond(x, {ActKind::Greet, ""});
    (void)d.respond(x, {ActKind::Farewell, ""});
    d.advance(std::floor(first) + 1.05 - first);
    check::is_true(now() - first < 1.0 && std::floor(now()) == std::floor(first) + 1.0, "the premise: the next day, under a day on");
    check::equal(memory(d.respond(x, {ActKind::Greet, ""}))["since"], std::string("yesterday"), "yesterday, hours ago");
    (void)d.respond(x, {ActKind::Farewell, ""});
    d.advance(0.9);
    check::is_true(std::floor(now()) == std::floor(first) + 1.0, "the premise: the same day, most of it later");
    check::equal(memory(d.respond(x, {ActKind::Greet, ""}))["since"], std::string("today"), "today, however many hours ago");
}

TEST(a_speaker_counts_what_the_player_says_across_conversations) {
    Director d(faced_world());
    const std::string x = townsfolk(1).front();
    const auto heard = [&](ActKind kind, const std::string& target = "") { return memory(d.respond(x, {kind, target}))["heard"]; };
    check::equal(heard(ActKind::Greet), std::string("0"), "a first greeting");
    check::equal(heard(ActKind::SmallTalk), std::string("0"), "a first word of small talk");
    check::equal(heard(ActKind::SmallTalk), std::string("1"), "small talk again, in the same conversation");
    const FactPacket around = d.respond(x, {ActKind::AskSurroundings, ""});
    const std::vector<Fact> places = facts_of(around, FactKind::Place);
    check::is_true(places.size() >= 2, "the premise: two places to ask about");
    check::equal(heard(ActKind::AskAboutPlace, places[0].subject), std::string("0"), "asked about one place");
    check::equal(heard(ActKind::AskAboutPlace, places[1].subject), std::string("1"), "and another: the same act, counted");
    check::equal(heard(ActKind::Farewell), std::string("0"), "a first farewell");
    d.advance(3.0);
    check::equal(heard(ActKind::Greet), std::string("1"), "greeted again, another day");
    check::equal(heard(ActKind::SmallTalk), std::string("2"), "small talk counted across conversations");
    check::equal(heard(ActKind::AskSurroundings), std::string("1"), "and a question");
    check::equal(heard(ActKind::Farewell), std::string("1"), "and a farewell");
    const std::string y = townsfolk(2)[1];
    check::equal(memory(d.respond(y, {ActKind::Greet, ""}))["heard"], std::string("0"), "each speaker counts only their own");
}

TEST(a_conversation_ends_at_a_farewell_when_time_passes_or_on_turning_away) {
    Director d(faced_world());
    const std::vector<std::string> folk = townsfolk(2);
    const std::string& x = folk[0];
    const std::string& y = folk[1];
    (void)d.respond(x, {ActKind::Greet, ""});
    check::equal(memory(d.respond(x, {ActKind::SmallTalk, ""}))["turn"], std::string("2"), "one conversation while the player stays");
    (void)d.respond(y, {ActKind::Greet, ""});
    Memory m = memory(d.respond(x, {ActKind::Greet, ""}));
    check::is_true(m["turn"] == "1" && m["times"] == "1", "turning to someone else ended it");
    check::is_true(d.acquaintance(y) != nullptr && !d.acquaintance(y)->talking, "and the other's ended in turn");
    d.advance(0.25);
    m = memory(d.respond(x, {ActKind::SmallTalk, ""}));
    check::is_true(m["turn"] == "1" && m["times"] == "2", "time passing ended it");
    (void)d.respond(x, {ActKind::Farewell, ""});
    m = memory(d.respond(x, {ActKind::Greet, ""}));
    check::is_true(m["turn"] == "1" && m["times"] == "3", "a farewell ended it");
}

TEST(asking_the_same_thing_twice_in_one_conversation_is_a_repeat) {
    Director d(faced_world());
    const std::string x = townsfolk(1).front();
    const FactPacket around = d.respond(x, {ActKind::AskSurroundings, ""});
    check::equal(memory(around)["repeat"], std::string("no"), "asked once");
    const std::vector<Fact> places = facts_of(around, FactKind::Place);
    check::is_true(places.size() >= 2, "the premise: two places to ask about");
    check::equal(memory(d.respond(x, {ActKind::AskSurroundings, ""}))["repeat"], std::string("yes"), "asked twice");
    check::equal(memory(d.respond(x, {ActKind::AskAboutPlace, places[0].subject}))["repeat"], std::string("no"), "a new question");
    check::equal(memory(d.respond(x, {ActKind::AskAboutPlace, places[1].subject}))["repeat"], std::string("no"),
                 "the same question of another place is another question");
    check::equal(memory(d.respond(x, {ActKind::AskAboutPlace, places[0].subject}))["repeat"], std::string("yes"), "the first place again");
    (void)d.respond(x, {ActKind::Farewell, ""});
    check::equal(memory(d.respond(x, {ActKind::AskSurroundings, ""}))["repeat"], std::string("no"), "a new conversation starts afresh");
}

TEST(thanks_insults_and_work_move_how_a_speaker_takes_the_player) {
    Director d(faced_world());
    const std::string x = townsfolk(1).front();
    const FactPacket thanked = d.respond(x, {ActKind::Thank, ""});
    check::is_true(thanked.outcome == Outcome::Thanked, "thanks taken");
    check::equal(memory(thanked)["mood"], std::string("friendly"), "thanked");
    check::equal(memory(d.respond(x, {ActKind::Thank, ""}))["mood"], std::string("friendly"), "thanks count once a conversation");
    (void)d.respond(x, {ActKind::Farewell, ""});
    check::equal(memory(d.respond(x, {ActKind::Thank, ""}))["mood"], std::string("warm"), "thanked again, another time");
    const FactPacket rude = d.respond(x, {ActKind::Insult, ""});
    check::is_true(rude.outcome == Outcome::Offended, "an insult taken");
    check::equal(memory(rude)["mood"], std::string("neutral"), "insulted once");
    check::equal(memory(d.respond(x, {ActKind::Insult, ""}))["mood"], std::string("hostile"), "insulted twice");

    const std::string g = givers().front();
    const FactPacket work = d.respond(g, {ActKind::AskForWork, ""});
    check::equal(memory(work)["work"], std::string("offered"), "work offered");
    std::string quest;
    for (const Fact& f : facts_of(work, FactKind::QuestOffer))
        if (quest.empty() && value(f, "from_speaker") == "yes") quest = f.subject;
    check::is_true(!quest.empty(), "the premise: the giver's own offer");
    const FactPacket taken = d.respond(g, {ActKind::AcceptQuest, quest});
    check::is_true(taken.outcome == Outcome::Accepted, "the premise: taken");
    check::is_true(memory(taken)["work"] == "taken" && memory(taken)["mood"] == "friendly", "taking work is welcome");
    const FactPacket dropped = d.respond(g, {ActKind::AbandonQuest, quest});
    check::is_true(dropped.outcome == Outcome::Abandoned, "the premise: given up");
    check::is_true(memory(dropped)["work"] == "abandoned" && memory(dropped)["mood"] == "wary", "giving it up is not");
}

TEST(what_the_player_asked_about_last_time_is_remembered) {
    Director d(faced_world());
    const std::string x = townsfolk(1).front();
    const FactPacket around = d.respond(x, {ActKind::AskSurroundings, ""});
    const std::vector<Fact> places = facts_of(around, FactKind::Place);
    check::is_true(!places.empty(), "the premise: a place to ask about");
    const Fact& place = places.front();
    const FactPacket asked = d.respond(x, {ActKind::AskAboutPlace, place.subject});
    check::is_true(asked.outcome == Outcome::Answered, "the premise: an answer");
    check::equal(memory(asked)["asked_before"], std::string(), "nothing from an earlier conversation");
    (void)d.respond(x, {ActKind::Farewell, ""});
    const std::string* name = place.field("name");
    const std::string label = name ? *name : "the " + value(place, "kind") + " of " + value(place, "of");
    check::equal(memory(d.respond(x, {ActKind::Greet, ""}))["asked_before"], label, "the place asked about, by name");
    (void)d.respond(x, {ActKind::SmallTalk, ""});
    check::equal(memory(d.respond(x, {ActKind::Farewell, ""}))["asked_before"], label, "all through the next conversation");
    check::equal(memory(d.respond(x, {ActKind::Greet, ""}))["asked_before"], std::string(),
                 "and not after one in which nothing was asked");
}

TEST(an_apology_eases_the_latest_grievance_and_gives_back_what_it_cost) {
    Director d(faced_world());
    const std::string x = townsfolk(1).front();
    const auto said = [&](ActKind kind) { return d.respond(x, {kind, ""}); };
    FactPacket p = said(ActKind::Apologize);
    check::is_true(p.outcome == Outcome::Unwronged, "nothing to forgive yet");
    check::equal(memory(p)["mood"], std::string("neutral"), "and nothing moves");
    check::equal(memory(said(ActKind::Insult))["mood"], std::string("hostile"), "the premise: insulted");
    p = said(ActKind::Apologize);
    check::is_true(p.outcome == Outcome::Forgiven, "forgiven");
    check::equal(memory(p)["mood"], std::string("neutral"), "back where it was");
    // Said again once forgiven, it is taken again: "nothing to forgive" would deny the wrong.
    p = said(ActKind::Apologize);
    check::is_true(p.outcome == Outcome::Forgiven, "sorry again, once forgiven: taken again");
    check::equal(memory(p)["mood"], std::string("neutral"), "and nothing moves");
    // At the bottom of the scale an insult costs less, and its apology gives back only that.
    check::equal(memory(said(ActKind::Thank))["mood"], std::string("friendly"), "the premise: thanked");
    check::equal(memory(said(ActKind::Insult))["mood"], std::string("wary"), "insulted");
    check::equal(memory(said(ActKind::Insult))["mood"], std::string("hostile"), "insulted again");
    check::equal(memory(said(ActKind::Insult))["mood"], std::string("hostile"), "and again, at the bottom");
    check::equal(memory(said(ActKind::Apologize))["mood"], std::string("hostile"), "the last insult cost nothing, and gives back nothing");
    check::equal(memory(said(ActKind::Apologize))["mood"], std::string("wary"), "the one before");
    p = said(ActKind::Apologize);
    check::is_true(p.outcome == Outcome::Forgiven && memory(p)["mood"] == "friendly", "the first: where the thanks had put it");
    p = said(ActKind::Apologize);
    check::is_true(p.outcome == Outcome::Forgiven && memory(p)["mood"] == "friendly", "all three forgiven: taken again, moving nothing");
    // Someone who never wronged the speaker has nothing to be forgiven, whatever else passed.
    const std::string y = townsfolk(2).back();
    check::is_true(y != x, "the premise: a second speaker");
    (void)d.respond(y, {ActKind::Thank, ""});
    check::is_true(d.respond(y, {ActKind::Apologize, ""}).outcome == Outcome::Unwronged, "never wronged: nothing to forgive");
}

TEST(giving_up_the_speakers_own_work_is_a_grievance_too) {
    Director d(faced_world());
    const std::string g = givers().front();
    const std::vector<std::string> own = own_offers(d, g);
    check::is_true(!own.empty(), "the premise: an offer of the giver's own");
    check::is_true(d.respond(g, {ActKind::AcceptQuest, own.front()}).outcome == Outcome::Accepted, "the premise: taken");
    const std::string before = memory(d.respond(g, {ActKind::SmallTalk, ""}))["mood"];
    check::is_true(d.respond(g, {ActKind::AbandonQuest, own.front()}).outcome == Outcome::Abandoned, "the premise: given up");
    const FactPacket p = d.respond(g, {ActKind::Apologize, ""});
    check::is_true(p.outcome == Outcome::Forgiven, "forgiven");
    check::equal(memory(p)["mood"], before, "as they were before it was given up");
}

TEST(an_answer_about_a_place_says_who_is_there_what_nests_near_and_the_work_there) {
    Director d(faced_world());
    std::size_t trades = 0, heroes = 0, nests = 0, work = 0, asked = 0, own_town = 0;
    std::vector<std::string> speakers = townsfolk(8);
    for (const std::string& g : givers()) speakers.push_back(g);
    for (const std::string& x : speakers) {
        const FactPacket around = d.respond(x, {ActKind::AskSurroundings, ""});
        for (const Fact& place : facts_of(around, FactKind::Place)) {
            const FactPacket about = d.respond(x, {ActKind::AskAboutPlace, place.subject});
            ++asked;
            const std::vector<Fact> places = facts_of(about, FactKind::Place);
            check::is_true(places.size() == 1 && places[0].subject == place.subject, "the one place asked about");
            fwg::PlaceReadRequest request;
            request.place_id = place.subject;
            const fwg::PlaceRead read = fwg::read_place(d.world(), request);
            const int node = read.place.anchor.node;
            for (const fwg::NpcPostRow& row : read.place.resolved.npc_posts)
                if (row.status == "alive" && row.hero_uid && *row.hero_uid == x) {
                    ++own_town;  // the roster there names the speaker
                    break;
                }
            if (const std::string* listed = places[0].field("trades")) {
                ++trades;
                std::size_t parts = 1;
                for (const char c : *listed) parts += c == ',';
                check::is_true(!listed->empty() && parts <= 3, "up to three trades: " + *listed);
            }
            const std::string* name = places[0].field("name");
            const std::string label = name ? *name : "the " + value(places[0], "kind") + " of " + value(places[0], "of");
            const std::vector<Fact> people = facts_of(about, FactKind::Person);
            check::is_true(people.size() <= 2, "up to two heroes there");
            for (const Fact& who : people) {
                ++heroes;
                check::equal(value(who, "where"), label, "a hero who stands there");
                check::equal(value(who, "status"), std::string("about"), "and is about");
                check::is_true(who.subject != x, "never the speaker themself: " + x);
            }
            const std::vector<Fact> near = facts_of(about, FactKind::Danger);
            check::is_true(near.size() <= 2, "up to two nests around it");
            for (const Fact& nest : near) {
                ++nests;
                const std::string range = value(nest, "range");
                check::is_true(value(nest, "around") == "yes" && (range == "here" || range == "close" || range == "near"),
                               "a nest within 2 km of it, its range from it: " + range);
            }
            for (const Fact& offer : facts_of(about, FactKind::QuestOffer)) {
                ++work;
                check::equal(value(offer, "marker_node"), std::to_string(node), "work anchored there");
            }
        }
        (void)d.respond(x, {ActKind::Farewell, ""});
    }
    check::is_true(asked > 0 && trades > 0 && heroes > 0 && nests > 0 && work > 0 && own_town > 0,
                   "the premise: places with trades, heroes, nests and work, and a speaker asked about the town they hold a "
                   "post in, of " + std::to_string(asked) + " asked about (" + std::to_string(trades) + ", " + std::to_string(heroes) +
                       ", " + std::to_string(nests) + ", " + std::to_string(work) + ", " + std::to_string(own_town) + ")");
}

TEST(a_giver_remembers_work_finished_or_failed) {
    Director d(faced_world());
    const std::vector<std::string> all = givers();
    const std::string& kept = all[0];
    const std::string& lost = all[1];
    const std::vector<std::string> kept_offers = own_offers(d, kept);
    const std::vector<std::string> lost_offers = own_offers(d, lost);
    check::is_true(!kept_offers.empty() && !lost_offers.empty(), "the premise: offers of their own");
    const std::string done = kept_offers.front();
    const std::string failed = lost_offers.front();
    check::is_true(d.respond(kept, {ActKind::AcceptQuest, done}).outcome == Outcome::Accepted, "the premise: taken");
    check::is_true(d.respond(lost, {ActKind::AcceptQuest, failed}).outcome == Outcome::Accepted, "the premise: taken");
    const std::size_t operations = d.world().history.operations.size();
    d.resolve(done, true);
    d.resolve(failed, false);
    check::equal(d.world().history.operations.size(), operations + 2, "each resolution a logged operation");
    check::is_true(on_board(d.world(), fwg::QuestState::Completed, done), "the board has it completed");
    check::is_true(on_board(d.world(), fwg::QuestState::Failed, failed), "and the other failed");
    d.advance(2.0);
    Memory m = memory(d.respond(kept, {ActKind::Greet, ""}));
    check::is_true(m["work"] == "finished" && m["mood"] == "warm", "work finished, gladly remembered");
    m = memory(d.respond(lost, {ActKind::Greet, ""}));
    check::is_true(m["work"] == "failed" && m["mood"] == "neutral", "work failed, remembered too");
    const std::size_t inputs = d.inputs().size();
    check::throws([&] { d.resolve(done, true); }, "work no longer held");
    check::throws([&] { d.resolve("no-such-quest", true); }, "work that never was");
    check::equal(d.inputs().size(), inputs, "a caller's mistake records nothing");
}

// The site a hero holds (their presence), empty for anyone else.
std::string site_held(const fwg::World& world, const std::string& uid) {
    for (const fwg::Hero& h : world.heroes.people)
        if (h.uid == uid && h.presence) return h.presence->uid;
    return {};
}

// A giver's own first offer, and where it is: its quest, anchor and anchor's node.
struct OwnOffer {
    std::string quest, marker, node;
};

std::optional<OwnOffer> own_first(Director& d, const std::string& giver) {
    const FactPacket asked = d.respond(giver, {ActKind::AskForWork, ""});
    std::optional<OwnOffer> out;
    for (const Fact& f : facts_of(asked, FactKind::QuestOffer))
        if (value(f, "from_speaker") == "yes") {
            out = OwnOffer{f.subject, value(f, "marker"), value(f, "marker_node")};
            break;
        }
    (void)d.respond(giver, {ActKind::Farewell, ""});
    return out;
}

// Contract version 14: work of the speaker's own at the site they hold, or where they stand, was done
// beside them.
TEST(work_done_where_its_giver_stands_was_done_beside_them) {
    Director d(faced_world());
    std::size_t beside = 0, apart = 0, held = 0, node_only = 0;
    for (const std::string& giver : givers()) {
        const std::optional<OwnOffer> own = own_first(d, giver);
        if (!own) continue;
        const bool at_their_site = !own->marker.empty() && own->marker == site_held(d.world(), giver);
        const bool at_their_place = own->node == std::to_string(*node_of(d.world(), giver)) || at_their_site;
        check::equal(memory(d.respond(giver, {ActKind::AskForWork, ""}))["beside"], std::string("no"),
                     giver + ": offered, nothing done beside them yet");
        const FactPacket took = d.respond(giver, {ActKind::AcceptQuest, own->quest});
        check::is_true(took.outcome == Outcome::Accepted, "the premise: taken");
        const std::string expected = at_their_place ? "yes" : "no";
        check::equal(memory(took)["beside"], expected, giver + ": taken");
        d.resolve(own->quest, true);
        check::equal(memory(d.respond(giver, {ActKind::Greet, ""}))["beside"], expected, giver + ": finished");
        ++(at_their_place ? beside : apart);
        if (at_their_site && own->node != std::to_string(*node_of(d.world(), giver))) ++held;
        if (!at_their_site && own->node == std::to_string(*node_of(d.world(), giver))) ++node_only;
        (void)d.respond(giver, {ActKind::Farewell, ""});
        if (beside > 1 && apart > 1 && held > 0 && node_only > 0) break;
    }
    check::is_true(beside > 0 && apart > 0 && held > 0 && node_only > 0,
                   "the premise: work at a site the giver holds but does not stand on, where they stand but hold nothing, and "
                   "elsewhere (" + std::to_string(held) + ", " + std::to_string(node_only) + ", " + std::to_string(apart) + ")");
}

// A hero's ties, as a hero and another giver's work both name them: rivals, kin, then allies, each in
// the record's order. On a record built for it, since no hero of the test world has kin among the
// ties it names.
TEST(a_heros_ties_are_rivals_then_kin_then_allies) {
    fwg::Hero hero;
    hero.allies = {"ally-1", "ally-2"};
    hero.kin = {"kin-1"};
    hero.rivals = {"rival-1"};
    const std::vector<std::pair<std::string, std::string>> expected = {
        {"rival", "rival-1"}, {"kin", "kin-1"}, {"ally", "ally-1"}, {"ally", "ally-2"}};
    check::is_true(orchestrator::knowledge::ties_of(hero) == expected, "rivals, kin, then allies, each named for what they are");
}

// Contract version 15: another giver's work, with how that giver stands to the speaker.
TEST(another_givers_work_says_how_they_stand_to_the_speaker) {
    Director d(faced_world());
    std::map<std::string, std::string> giver_of;  // a quest, its giver
    for (const fwg::QuestRow& q : quest_board(d.world(), fwg::QuestState::Offered))
        if (q.giver_uid) giver_of[q.quest_id] = *q.giver_uid;
    std::size_t tied = 0, untied = 0;
    for (const fwg::Hero& h : d.world().heroes.people) {
        if (h.status != "living" || !node_of(d.world(), h.uid)) continue;
        (void)d.respond(h.uid, {ActKind::AskForWork, ""});
        const FactPacket p = d.respond(h.uid, {ActKind::AskForWork, ""});  // asked again: others' work too (contract version 16)
        for (const Fact& f : facts_of(p, FactKind::QuestOffer)) {
            const std::string giver = giver_of[f.subject];
            std::string expected;
            if (std::find(h.rivals.begin(), h.rivals.end(), giver) != h.rivals.end()) expected = "rival";
            else if (std::find(h.kin.begin(), h.kin.end(), giver) != h.kin.end()) expected = "kin";
            else if (std::find(h.allies.begin(), h.allies.end(), giver) != h.allies.end()) expected = "ally";
            if (value(f, "from_speaker") == "yes") expected.clear();
            const std::string* tie = f.field("giver_tie");
            check::equal(tie ? *tie : std::string(), expected, h.uid + ": " + f.subject);
            ++(expected.empty() ? untied : tied);
        }
        (void)d.respond(h.uid, {ActKind::Farewell, ""});
    }
    check::is_true(tied > 0 && untied > 0, "the premise: others' work from givers tied to the speaker, and from others (" +
                                               std::to_string(tied) + ", " + std::to_string(untied) + ")");
}

// Nor is someone else's work at the place they stand theirs.
TEST(another_givers_work_where_the_speaker_stands_was_not_done_beside_them) {
    Director d(faced_world());
    std::map<std::string, OwnOffer> offer_of;
    for (const std::string& giver : givers())
        if (const std::optional<OwnOffer> own = own_first(d, giver)) offer_of[giver] = *own;
    for (const auto& [x, xo] : offer_of) {
        const std::string here = std::to_string(*node_of(d.world(), x));
        if (xo.node == here || (!xo.marker.empty() && xo.marker == site_held(d.world(), x))) continue;
        for (const auto& [y, yo] : offer_of) {
            if (y == x || yo.node != here) continue;
            check::is_true(d.respond(x, {ActKind::AcceptQuest, xo.quest}).outcome == Outcome::Accepted, "the premise: theirs taken");
            check::is_true(d.respond(y, {ActKind::AcceptQuest, yo.quest}).outcome == Outcome::Accepted, "the premise: the other's taken");
            check::equal(memory(d.respond(x, {ActKind::SmallTalk, ""}))["beside"], std::string("no"),
                         x + ": their own work elsewhere, another's where they stand");
            return;
        }
    }
    check::fail("the premise: a speaker whose own work is elsewhere, and another's work where they stand");
}

TEST(small_talk_is_of_the_weather_and_season_where_the_speaker_stands) {
    // By the reading at each speaker's own node: a word never ranks below the word for a milder
    // reading. Four moments a few days apart, so the weather has room to differ.
    const std::vector<std::string> weathers = {"clear", "fair", "unsettled", "stormy", "a great storm"};
    const std::vector<std::string> winds = {"still", "breezy", "windy", "a gale"};
    const std::vector<std::string> seasons = {"deep winter", "winter", "the turn of the season", "summer", "high summer"};
    const std::set<std::string> times = {"dawn", "morning", "midday", "afternoon", "evening", "night"};
    struct Reading {
        double measure;
        std::size_t rank;
    };
    std::vector<Reading> weather, wind, season;
    const auto rank = [](const std::vector<std::string>& words, const std::string& word) {
        const auto at = std::find(words.begin(), words.end(), word);
        if (at == words.end()) check::fail("a published word, not " + word);
        return static_cast<std::size_t>(at - words.begin());
    };
    Director d(faced_world());
    const std::vector<std::string> folk = townsfolk(40);
    std::size_t with_danger = 0, worst_not_nearest = 0;
    for (int moment = 0; moment < 4; ++moment) {
        if (moment > 0) d.advance(3.3);
        const fwg::WeatherReport report = fwg::weather_report(d.world(), today(d));
        for (const std::string& x : folk) {
            const FactPacket p = d.respond(x, {ActKind::SmallTalk, ""});
            check::is_true(p.outcome == Outcome::Chatted, "small talk");
            const std::vector<Fact> chat = facts_of(p, FactKind::Chat);
            check::equal(chat.size(), std::size_t{1}, "one Chat fact");
            const fwg::WeatherSample here = fwg::weather_sample_at_node(d.world(), report, *node_of(d.world(), x));
            weather.push_back({here.intensity_pct, rank(weathers, value(chat[0], "weather"))});
            wind.push_back({here.wind_speed_m_s, rank(winds, value(chat[0], "wind"))});
            season.push_back({here.summer_pct, rank(seasons, value(chat[0], "season"))});
            check::is_true(times.count(value(chat[0], "time")) == 1, "a time of day");
            // The worst of what is about, when anything is: the first nest the surroundings name,
            // which is the worst, the nearest of those -- checked here against the danger each says.
            const std::vector<Fact> danger = facts_of(p, FactKind::Danger);
            check::is_true(danger.size() <= 1, "one danger at most");
            const FactPacket around = d.respond(x, {ActKind::AskSurroundings, ""});
            const std::vector<Fact> near = facts_of(around, FactKind::Danger);
            check::equal(danger.size(), std::size_t(near.empty() ? 0 : 1), "a danger exactly when a nest is near");
            if (danger.empty()) continue;
            ++with_danger;
            check::equal(danger.front().subject, near.front().subject, "the first the surroundings name");
            for (const Fact& nest : near)
                check::is_true(std::stoi(value(nest, "danger")) <= std::stoi(value(danger.front(), "danger")), "none worse");
            for (const Fact& nest : near)
                if (band_rank(value(nest, "range")) < band_rank(value(danger.front(), "range"))) ++worst_not_nearest;
        }
    }
    for (const auto* readings : {&weather, &wind, &season}) {
        std::set<std::size_t> ranks;
        for (const Reading& a : *readings) {
            ranks.insert(a.rank);
            for (const Reading& b : *readings)
                if (a.measure < b.measure) check::is_true(a.rank <= b.rank, "a milder reading never gets the stronger word");
        }
        check::is_true(ranks.size() >= 2, "the premise: more than one word met, " + std::to_string(ranks.size()));
    }
    check::is_true(with_danger > 0, "the premise: someone with a nest nearby");
    check::is_true(worst_not_nearest > 0, "the premise: someone with a nest nearer than the worst");
}

TEST(is_it_safe_is_answered_with_a_verdict_on_what_is_within_a_walk) {
    // The verdict by the contract's rule, over what came with it; and the places named are the
    // ones the contract names, by the near read out to the ceiling (city folk know all of it here).
    const fwg::World& w = faced_world();
    Director d(w);
    std::map<std::string, int> verdicts;
    std::size_t oracled = 0, near_danger = 0;
    const auto within_a_walk = [](const std::string& range) {
        return range == "here" || range == "close" || range == "near" || range == "walk";
    };
    for (const std::string& x : townsfolk(40)) {
        const FactPacket p = d.respond(x, {ActKind::AskSafety, ""});
        check::is_true(p.outcome == Outcome::Answered, "an answer");
        check::is_true(!p.facts.empty() && p.facts.front().kind == FactKind::Safety, "the verdict first");
        const std::string verdict = value(p.facts.front(), "verdict");
        const std::vector<Fact> places = facts_of(p, FactKind::Place);
        const std::vector<Fact> nests = facts_of(p, FactKind::Danger);
        check::is_true(places.size() <= 3, "the worst three places at most");
        bool unsafe = false, wary = false, further = false;
        int last = 6;
        for (const Fact& place : places) {
            const int threat = std::stoi(value(place, "threat"));
            const bool near = within_a_walk(value(place, "range"));
            check::is_true(!(near && further), "the places within a walk first");
            if (!near && !further) last = 6;  // the further group begins
            further = !near;
            check::is_true(threat >= 4 && threat <= last, "dangerous places, worst first in each group");
            last = threat;
            if (!near) continue;
            wary = true;
            if (threat >= 5) unsafe = true;
        }
        for (const Fact& nest : nests) {
            const int danger = std::stoi(value(nest, "danger"));
            if (danger >= 4) unsafe = true;
            if (danger >= 3) wary = true;
        }
        check::equal(verdict, std::string(unsafe ? "unsafe" : wary ? "wary" : "safe"), "the verdict for " + x);
        ++verdicts[verdict];
        // The places named are the ones the contract names, by the near read: within a walk
        // (under 7 km) first, then the rest, each worst then nearest, three at most.
        const fwg::NpcPerson* person = nullptr;
        for (const fwg::NpcPerson& n : w.npcs.people)
            if (n.uid == x) person = &n;
        if (!person || !person->site_kind || *person->site_kind != "city") continue;
        fwg::NearReadRequest near;
        near.node = *node_of(w, x);
        near.radius_m = w.effective.globe_radius * fwg::kNearRadiusFraction;
        near.categories = std::vector<std::string>{"key_locations"};
        std::vector<fwg::NearKeyLocationRow> dangerous;
        for (const fwg::NearKeyLocationRow& row : fwg::read_near(w, near).near.key_locations)
            if (row.threat >= 4) dangerous.push_back(row);
        std::stable_sort(dangerous.begin(), dangerous.end(), [](const auto& a, const auto& b) {
            const bool na = a.distance_m < 7000.0, nb = b.distance_m < 7000.0;
            if (na != nb) return na;
            if (a.threat != b.threat) return a.threat > b.threat;
            return a.distance_m < b.distance_m;
        });
        std::vector<std::string> expected, named;
        for (std::size_t i = 0; i < dangerous.size() && i < 3; ++i) expected.push_back(dangerous[i].id);
        for (const Fact& place : places) named.push_back(place.subject);
        check::is_true(named == expected, "the dangerous places the contract names, for " + x);
        ++oracled;
        if (!dangerous.empty() && dangerous.front().distance_m < 7000.0) ++near_danger;
    }
    check::is_true(oracled > 10, "the premise: city folk to check against the near read, " + std::to_string(oracled));
    check::is_true(near_danger > 0, "the premise: a dangerous place within a walk of someone");
    check::is_true(verdicts.size() >= 2, "the premise: more than one verdict among " + std::to_string(verdicts.size()));
}

TEST(a_mask_comes_off_to_long_acquaintance_and_not_before) {
    const fwg::Hero& mask = *heroes_with("public", "true").front();
    const fwg::HeroFace& shown = face(mask, "public");
    const fwg::HeroFace& hidden = face(mask, "true");
    check::is_true(shown.apparent_role != hidden.apparent_role, "the premise: a mask over another role");
    Director d(faced_world());
    for (int meeting = 1; meeting < 3; ++meeting) {
        (void)d.respond(mask.uid, {ActKind::Greet, ""});
        const FactPacket denied = d.respond(mask.uid, {ActKind::Confront, ""});
        check::is_true(denied.outcome == Outcome::Denied, "denied at meeting " + std::to_string(meeting));
        check::is_true(only_memory(denied), "and nothing given away");
        check::is_true(denied.persona.role == shown.apparent_role && denied.persona.voice_key.empty() && denied.persona.face.empty(),
                       "still the mask");
        check::equal(memory(denied)["exposed"], std::string("no"), "not exposed");
        (void)d.respond(mask.uid, {ActKind::Farewell, ""});
    }
    (void)d.respond(mask.uid, {ActKind::Greet, ""});
    const FactPacket exposed = d.respond(mask.uid, {ActKind::Confront, ""});
    check::is_true(exposed.outcome == Outcome::Exposed, "exposed at the third meeting");
    const std::vector<Fact> reveal = facts_of(exposed, FactKind::Reveal);
    check::equal(reveal.size(), std::size_t{1}, "one Reveal fact");
    check::equal(reveal[0].subject, mask.uid, "of the speaker");
    check::equal(value(reveal[0], "face"), std::string("true"), "the true face");
    check::equal(value(reveal[0], "was"), shown.apparent_role, "what the mask claimed");
    check::equal(value(reveal[0], "is"), hidden.apparent_role, "what it hid");
    check::equal(exposed.persona.role, hidden.apparent_role, "the true face speaks");
    check::equal(exposed.persona.manner, hidden.persona.manner, "with its own card");
    check::equal(exposed.persona.voice_key, key_of(mask, hidden), "and its archetype's lines");
    check::equal(exposed.persona.face, std::string("true"), "and says which face it is");
    check::equal(memory(exposed)["exposed"], std::string("yes"), "exposed");
    (void)d.respond(mask.uid, {ActKind::Farewell, ""});
    d.advance(1.0);
    check::equal(d.respond(mask.uid, {ActKind::Greet, ""}).persona.face, std::string("true"), "a mask once off stays off");
    Director again = orchestrator::replay(faced_world(), d.inputs());
    check::equal(again.respond(mask.uid, {ActKind::Greet, ""}).persona.face, std::string("true"), "and replay keeps it off");
}

TEST(finishing_a_masks_covert_work_is_cause_to_confront_it) {
    const fwg::World& w = faced_world();
    std::set<std::string> masks;
    for (const fwg::Hero* h : heroes_with("public", "true")) masks.insert(h->uid);
    std::string giver, quest;
    for (const fwg::QuestRow& q : quest_board(w, fwg::QuestState::Offered))
        if (quest.empty() && q.unwitting && q.giver_uid && masks.count(*q.giver_uid) && q.giver &&
            q.giver->liveness == fwg::Liveness::Present) {
            giver = *q.giver_uid;
            quest = q.quest_id;
        }
    check::is_true(!quest.empty(), "the premise: a mask's offer under a false purpose");
    for (const bool finished : {false, true}) {
        Director d(w);
        (void)d.respond(giver, {ActKind::AskForWork, ""});
        check::is_true(d.respond(giver, {ActKind::AcceptQuest, quest}).outcome == Outcome::Accepted, "the premise: taken");
        check::is_true(d.respond(giver, {ActKind::Confront, ""}).outcome == Outcome::Denied, "taking it is no cause");
        d.resolve(quest, finished);
        d.advance(1.0);
        const FactPacket p = d.respond(giver, {ActKind::Confront, ""});
        check::is_true(p.outcome == (finished ? Outcome::Exposed : Outcome::Denied),
                       finished ? "finishing it is cause" : "failing it is none");
    }
}

TEST(a_masks_rival_or_kin_tells_the_player_what_it_hides) {
    const fwg::World& w = faced_world();
    // A mask, and a rival, kin or ally who knows where it stands.
    std::string mask, knower, tried;
    for (const fwg::Hero* h : heroes_with("public", "true"))
        for (const std::vector<std::string>* list : {&h->rivals, &h->kin, &h->allies})
            for (const std::string& uid : *list) {
                if (!mask.empty()) break;
                Director probe(w);
                const std::optional<orchestrator::FactPacket> p = [&]() -> std::optional<FactPacket> {
                    try {
                        return probe.respond(uid, {ActKind::AskAboutPerson, h->uid});
                    } catch (const orchestrator::Error&) {
                        return std::nullopt;
                    }
                }();
                const std::optional<int> at = node_of(w, uid);
                const std::optional<int> them = node_of(w, h->uid);
                tried += " [" + uid + " at " + (at ? std::to_string(*at) : "nowhere") + " of " + h->uid + " at " +
                         (them ? std::to_string(*them) : "nowhere") + ": " +
                         (p ? std::string(orchestrator::to_string(p->outcome)) : std::string("error")) + "]";
                if (p && p->outcome == Outcome::Answered) {
                    mask = h->uid;
                    knower = uid;
                }
            }
    check::is_true(!mask.empty(), "the premise: a mask whose rival, kin or ally knows where it stands;" + tried);
    Director d(w);
    const FactPacket told = d.respond(knower, {ActKind::AskAboutPerson, mask});
    const std::vector<Fact> person = facts_of(told, FactKind::Person);
    check::is_true(!person.empty() && person.front().subject == mask, "the premise: told of the mask");
    check::equal(value(person.front(), "hides"), std::string("yes"), "and told it is one");
    check::is_true(d.respond(mask, {ActKind::Confront, ""}).outcome == Outcome::Exposed, "cause enough at a first meeting");
    // Someone who only lives beside them says nothing of it, and gives no cause.
    const std::optional<int> where = node_of(w, mask);
    std::string stranger;
    for (const fwg::NpcPerson& n : w.npcs.people)
        if (stranger.empty() && n.alive && !n.is_cast && npc_node(w, n) == where) stranger = n.uid;
    check::is_true(!stranger.empty(), "the premise: a roster person where the mask stands");
    Director other(w);
    const FactPacket plain = other.respond(stranger, {ActKind::AskAboutPerson, mask});
    check::is_true(plain.outcome == Outcome::Answered, "the premise: the stranger knows of the mask");
    const std::vector<Fact> seen = facts_of(plain, FactKind::Person);
    check::is_true(!seen.empty() && seen.front().field("hides") == nullptr, "a stranger knows no more than the mask");
    check::is_true(other.respond(mask, {ActKind::Confront, ""}).outcome == Outcome::Denied, "and gives no cause");
}

TEST(a_beast_is_unleashed_about_the_full_moon_and_owns_to_it_after) {
    const fwg::World& w = faced_world();
    const fwg::Hero& beast = *heroes_with("calm", "unleashed").front();
    const fwg::HeroFace& calm = face(beast, "calm");
    const fwg::HeroFace& unleashed = face(beast, "unleashed");
    Director d(w);
    advance_to_a_day(d, [&](double day) { return !full_moon_near(w, day); }, "a day far from the full moon");
    FactPacket p = d.respond(beast.uid, {ActKind::Greet, ""});
    check::is_true(p.persona.role == calm.apparent_role && p.persona.face.empty() && p.persona.voice_key == key_of(beast, calm),
                   "the calm face, speaking as itself");
    check::is_true(d.respond(beast.uid, {ActKind::Confront, ""}).outcome == Outcome::Denied, "no cause before it was seen");
    (void)d.respond(beast.uid, {ActKind::Insult, ""});
    (void)d.respond(beast.uid, {ActKind::Farewell, ""});
    advance_to_a_day(d, [&](double day) { return fwg::moon_phase_on(w, day)->full; }, "the full moon");
    p = d.respond(beast.uid, {ActKind::Greet, ""});
    check::is_true(p.persona.role == unleashed.apparent_role && p.persona.face == "unleashed" &&
                       p.persona.voice_key == key_of(beast, unleashed),
                   "unleashed under the full moon");
    const Memory raging = memory(p);
    check::is_true(raging.at("met") == "first" && raging.at("mood") == "hostile" && raging.at("heard") == "0", "knowing nobody");
    check::is_true(facts_of(p, FactKind::Recall).empty(), "and recalling nothing of the player");
    check::is_true(d.respond(beast.uid, {ActKind::Apologize, ""}).outcome == Outcome::Unwronged,
                   "and forgiving nothing: it does not know the player");
    d.advance(1.0);
    check::equal(d.respond(beast.uid, {ActKind::Greet, ""}).persona.face, std::string("unleashed"), "and the day after");
    (void)d.respond(beast.uid, {ActKind::Farewell, ""});
    advance_to_a_day(d, [&](double day) { return !full_moon_near(w, day); }, "a calm day after");
    p = d.respond(beast.uid, {ActKind::Greet, ""});
    check::is_true(p.persona.face.empty() && memory(p)["times"] == "3", "calm again, and counting every meeting");
    check::is_true(d.respond(beast.uid, {ActKind::Apologize, ""}).outcome == Outcome::Forgiven, "the calm face forgives the insult");
    const FactPacket owned = d.respond(beast.uid, {ActKind::Confront, ""});
    check::is_true(owned.outcome == Outcome::Exposed, "it owns to what the player saw");
    const std::vector<Fact> reveal = facts_of(owned, FactKind::Reveal);
    check::is_true(reveal.size() == 1 && value(reveal[0], "face") == "unleashed" && value(reveal[0], "was") == calm.apparent_role &&
                       value(reveal[0], "is") == unleashed.apparent_role,
                   "the face it owns to");
    check::is_true(owned.persona.face.empty(), "and stays calm while it does");
}

TEST(a_fractured_heart_shows_each_face_on_its_own_days) {
    const fwg::World& w = faced_world();
    const fwg::Hero& heart = *heroes_with("first", "second").front();
    const fwg::HeroFace& first = face(heart, "first");
    const fwg::HeroFace& second = face(heart, "second");
    // Before the second face is seen, on a first-face day: nothing to confront.
    Director probe(w);
    if (probe.respond(heart.uid, {ActKind::Greet, ""}).persona.face == "second") probe.advance(1.0);
    Director fresh(probe.world());
    const FactPacket before = fresh.respond(heart.uid, {ActKind::Confront, ""});
    check::is_true(before.persona.face.empty(), "the premise: a first-face day");
    check::is_true(before.outcome == Outcome::Denied, "no cause before the second face was seen");
    Director d(w);
    std::vector<std::string> shown;
    for (int day = 0; day < 4; ++day) {
        const FactPacket p = d.respond(heart.uid, {ActKind::Greet, ""});
        const fwg::HeroFace& expected = p.persona.face == "second" ? second : first;
        check::is_true(p.persona.face.empty() || p.persona.face == "second", "one of its two faces");
        check::is_true(p.persona.role == expected.apparent_role && p.persona.voice_key == key_of(heart, expected), "speaking as that face");
        shown.push_back(p.persona.face);
        d.advance(1.0);
    }
    for (std::size_t i = 1; i < shown.size(); ++i) check::is_true(shown[i] != shown[i - 1], "the faces take turns, a day each");
    if (d.respond(heart.uid, {ActKind::Greet, ""}).persona.face == "second") d.advance(1.0);
    const FactPacket owned = d.respond(heart.uid, {ActKind::Confront, ""});
    check::is_true(owned.persona.face.empty() && owned.outcome == Outcome::Exposed, "the first face owns to the second");
    const std::vector<Fact> reveal = facts_of(owned, FactKind::Reveal);
    check::is_true(reveal.size() == 1 && value(reveal[0], "face") == "second", "and names it");
}

TEST(someone_with_nothing_hidden_denies_it) {
    const fwg::World& w = faced_world();
    std::string plain;
    for (const fwg::Hero& h : w.heroes.people)
        if (plain.empty() && h.status == "living" && h.faces.size() == 1 && node_of(w, h.uid)) plain = h.uid;
    check::is_true(!plain.empty(), "the premise: a hero with one face");
    Director d(w);
    for (int meeting = 0; meeting < 4; ++meeting) {
        const FactPacket p = d.respond(plain, {ActKind::Confront, ""});
        check::is_true(p.outcome == Outcome::Denied && only_memory(p) && p.persona.face.empty(), "denied, however often");
        (void)d.respond(plain, {ActKind::Farewell, ""});
    }
    const FactPacket roster = d.respond(townsfolk(1).front(), {ActKind::Confront, ""});
    check::is_true(roster.outcome == Outcome::Denied && only_memory(roster), "and a roster person too");
}

TEST(a_speaker_is_of_the_people_the_world_records) {
    // Whose sayings a speaker knows: a hero's civilization, a roster person's people.
    const fwg::World& w = faced_world();
    Director d(w);
    std::size_t heroes = 0, folk = 0;
    std::set<std::string> peoples;
    for (const fwg::Hero& h : w.heroes.people) {
        if (h.status != "living" || !node_of(w, h.uid) || heroes == 20) continue;
        check::is_true(!h.civilization_id.empty(), "the premise: a hero of a people");
        check::equal(d.respond(h.uid, {ActKind::Greet, ""}).persona.people, h.civilization_id, "the hero's people");
        peoples.insert(h.civilization_id);
        ++heroes;
    }
    for (const std::string& uid : townsfolk(20)) {
        const fwg::NpcPerson* person = nullptr;
        for (const fwg::NpcPerson& n : w.npcs.people)
            if (n.uid == uid) person = &n;
        check::is_true(person && person->appearance && !person->appearance->civilization_id.empty(),
                       "the premise: a roster person of a people");
        check::equal(d.respond(uid, {ActKind::Greet, ""}).persona.people, person->appearance->civilization_id, "their people");
        peoples.insert(person->appearance->civilization_id);
        ++folk;
    }
    check::is_true(heroes == 20 && folk == 20, "the premise: speakers of both kinds");
    check::is_true(peoples.size() > 1, "the premise: more than one people among them");
}

TEST(every_packet_carries_one_memory_of_its_speaker) {
    Director d(faced_world());
    const std::string g = givers().front();
    const std::vector<std::string> own = own_offers(d, g);
    check::is_true(!own.empty(), "the premise: an offer of the giver's own");
    const std::vector<std::pair<ActKind, std::string>> acts = {
        {ActKind::Greet, ""},         {ActKind::SmallTalk, ""},        {ActKind::AskSurroundings, ""},
        {ActKind::AskSafety, ""},     {ActKind::AskAboutPerson, g},    {ActKind::AskForWork, ""},
        {ActKind::DeclineQuest, own[0]}, {ActKind::AcceptQuest, own[0]}, {ActKind::AbandonQuest, own[0]},
        {ActKind::Answer, ""},        {ActKind::Thank, ""},            {ActKind::Insult, ""},
        {ActKind::Confront, ""},      {ActKind::NoAct, ""},            {ActKind::AskAboutPlace, "no-such-place"},
        {ActKind::Apologize, ""},     {ActKind::Calm, ""},             {ActKind::AskAboutThem, ""},
        {ActKind::Farewell, ""}};
    std::set<ActKind> kinds;
    int turn = 1;  // own_offers asked once already
    for (const auto& [kind, target] : acts) {
        kinds.insert(kind);
        const Memory m = memory(d.respond(g, {kind, target}));
        check::equal(m.at("turn"), std::to_string(++turn), std::string(orchestrator::to_string(kind)) + "'s turn");
    }
    check::equal(kinds.size(), static_cast<std::size_t>(orchestrator::kActCount), "the premise: every act of the contract");
}

TEST(replay_restores_what_every_speaker_remembers) {
    Director d(faced_world());
    const std::vector<std::string> folk = townsfolk(2);
    const std::string g = givers().front();
    const std::string mask = heroes_with("public", "true").front()->uid;
    (void)d.respond(folk[0], {ActKind::Greet, ""});
    (void)d.respond(folk[0], {ActKind::Thank, ""});
    const FactPacket around = d.respond(folk[0], {ActKind::AskSurroundings, ""});
    const std::vector<Fact> places = facts_of(around, FactKind::Place);
    check::is_true(!places.empty(), "the premise: a place to ask about");
    (void)d.respond(folk[0], {ActKind::AskAboutPlace, places.front().subject});
    (void)d.respond(folk[1], {ActKind::Insult, ""});
    (void)d.respond(folk[1], {ActKind::Insult, ""});
    (void)d.respond(folk[1], {ActKind::Apologize, ""});
    const std::vector<std::string> own = own_offers(d, g);
    check::is_true(!own.empty(), "the premise: an offer of the giver's own");
    (void)d.respond(g, {ActKind::AcceptQuest, own.front()});
    d.resolve(own.front(), true);
    d.advance(2.0);
    for (int meeting = 0; meeting < 3; ++meeting) {
        (void)d.respond(mask, {ActKind::Greet, ""});
        (void)d.respond(mask, {ActKind::Farewell, ""});
    }
    (void)d.respond(mask, {ActKind::Confront, ""});
    Director again = orchestrator::replay(faced_world(), d.inputs());
    check::is_true(json_of(again.world()) == json_of(d.world()), "a byte-identical world");
    for (const std::string& uid : {folk[0], folk[1], g, mask}) {
        const Director::Acquaintance* a = d.acquaintance(uid);
        const Director::Acquaintance* b = again.acquaintance(uid);
        check::is_true(a && b, "both remember " + uid);
        check::is_true(a->conversations == b->conversations && a->disposition == b->disposition && a->work == b->work &&
                           a->asked_before == b->asked_before && a->asked_now == b->asked_now && a->exposed == b->exposed &&
                           a->finished_covert == b->finished_covert && a->turn == b->turn && a->said == b->said &&
                           a->heard == b->heard && a->grievances == b->grievances && a->forgiven == b->forgiven,
                       "the same acquaintance with " + uid);
        check::equal(text_of(d.respond(uid, {ActKind::SmallTalk, ""})), text_of(again.respond(uid, {ActKind::SmallTalk, ""})),
                     "the same next packet from " + uid);
    }
}

// ---- feelings (contract version 9) --------------------------------------------------------------

namespace {

// Whether a nest of danger 4 or more lies within a kilometre of `node`: what leaves people uneasy
// at rest.
bool dangerous_nest_within_a_km(const fwg::World& world, int node) {
    fwg::NearReadRequest request;
    request.node = node;
    request.radius_m = 1000.0;
    request.categories = std::vector<std::string>{"beast_nests"};
    const fwg::NearRead read = fwg::read_near(world, request);
    return std::any_of(read.near.beast_nests.begin(), read.near.beast_nests.end(), [](const auto& nest) { return nest.tier >= 4; });
}

// Living roster people, standing somewhere, who rest as `resting`: calm or uneasy.
std::vector<std::string> resting_as(const std::string& resting, std::size_t count) {
    const Director d(faced_world());
    std::vector<std::string> out;
    for (const fwg::NpcPerson& p : faced_world().npcs.people) {
        if (out.size() == count) break;
        if (!p.alive || !node_of(faced_world(), p.uid)) continue;
        if (d.feeling(p.uid).resting == resting) out.push_back(p.uid);
    }
    if (out.size() < count) check::fail("the premise: " + std::to_string(count) + " roster people resting " + resting);
    return out;
}

double minutes(double n) { return n / 1440.0; }

}  // namespace

TEST(everyone_rests_calm_or_uneasy_by_the_nests_near_them) {
    Director d(faced_world());
    std::size_t calm = 0, uneasy = 0;
    std::vector<std::string> spoken;
    for (const fwg::NpcPerson& p : faced_world().npcs.people) {
        if (!p.alive) continue;
        const std::optional<int> node = node_of(faced_world(), p.uid);
        if (!node) continue;
        const orchestrator::Feeling f = d.feeling(p.uid);
        const bool near = dangerous_nest_within_a_km(faced_world(), *node);
        check::equal(f.resting, std::string(near ? "uneasy" : "calm"), "at rest, by the nests within a kilometre: " + p.uid);
        check::equal(f.now, f.resting, "nothing has frightened " + p.uid + " yet");
        ++(near ? uneasy : calm);
        if (spoken.size() < 2 && (spoken.empty() || d.feeling(spoken.front()).resting != f.resting)) spoken.push_back(p.uid);
    }
    check::is_true(calm > 0 && uneasy > 0,
                   "the premise: people at rest both ways (" + std::to_string(calm) + " calm, " + std::to_string(uneasy) + " uneasy)");
    for (const std::string& uid : spoken) {
        const Memory m = memory(d.respond(uid, {ActKind::Greet, ""}));
        check::is_true(m.at("feeling") == d.feeling(uid).now && m.at("resting") == d.feeling(uid).resting,
                       "the packet says how they feel, and how they rest: " + uid);
    }
    bool threw = false;
    try {
        (void)d.feeling("no-such-person");
    } catch (const orchestrator::Error&) {
        threw = true;
    }
    check::is_true(threw, "a uid the world does not name is refused");
}

TEST(fear_rises_with_a_fright_and_falls_a_step_each_half_hour_nothing_frightens) {
    Director d(faced_world());
    const std::string x = resting_as("calm", 1).front();
    d.fright(x, 3);
    check::equal(d.feeling(x).now, std::string("panicked"), "frightened to panic");
    check::equal(memory(d.respond(x, {ActKind::Greet, ""}))["feeling"], std::string("panicked"), "and the packet says so");
    d.fright(x, 1);
    check::equal(d.feeling(x).now, std::string("panicked"), "a lesser fright eases nothing");
    d.advance(minutes(29.0));
    check::equal(d.feeling(x).now, std::string("panicked"), "not yet half an hour");
    d.advance(minutes(2.0));
    check::equal(d.feeling(x).now, std::string("afraid"), "a step down after half an hour");
    d.advance(minutes(30.0));
    check::equal(d.feeling(x).now, std::string("uneasy"), "another");
    d.advance(minutes(30.0));
    check::equal(d.feeling(x).now, std::string("calm"), "and at rest");
    d.advance(minutes(60.0));
    check::equal(d.feeling(x).now, std::string("calm"), "and no further");
    // A fright kept up keeps them where it put them.
    d.fright(x, 2);
    d.advance(minutes(25.0));
    d.fright(x, 2);
    d.advance(minutes(25.0));
    check::equal(d.feeling(x).now, std::string("afraid"), "frightened again before it could ease");
    // Someone uneasy at rest eases no further than that.
    const std::string y = resting_as("uneasy", 1).front();
    d.fright(y, 3);
    d.advance(minutes(240.0));
    check::equal(d.feeling(y).now, std::string("uneasy"), "at rest, uneasy");
    for (const int level : {0, 4}) {
        bool threw = false;
        try {
            d.fright(x, level);
        } catch (const orchestrator::Error&) {
            threw = true;
        }
        check::is_true(threw, "a fright at level " + std::to_string(level) + " is refused");
    }
    bool threw = false;
    try {
        d.fright("no-such-person", 2);
    } catch (const orchestrator::Error&) {
        threw = true;
    }
    check::is_true(threw, "a uid the world does not name is refused");
}

TEST(fear_spreads_through_people_together) {
    Director d(faced_world());
    const std::vector<std::string> folk = resting_as("calm", 4);
    d.fright(folk[0], 3);
    d.gather({folk[0], folk[1], folk[2]});
    check::equal(d.feeling(folk[1]).now, std::string("afraid"), "one panicking leaves the others afraid");
    check::equal(d.feeling(folk[2]).now, std::string("afraid"), "each of them");
    check::equal(d.feeling(folk[0]).now, std::string("panicked"), "and falls no lower for their company");
    d.fright(folk[1], 3);
    d.gather({folk[0], folk[1], folk[2], folk[3]});
    check::equal(d.feeling(folk[2]).now, std::string("panicked"), "two panicking leave the others panicking");
    check::equal(d.feeling(folk[3]).now, std::string("panicked"), "a newcomer too");
    Director e(faced_world());
    e.fright(folk[0], 2);
    e.gather({folk[0], folk[1]});
    check::equal(e.feeling(folk[1]).now, std::string("uneasy"), "one afraid leaves the other uneasy");
    e.gather({folk[1], folk[2]});
    check::equal(e.feeling(folk[2]).now, std::string("calm"), "and the uneasy one frightens nobody");
    bool threw = false;
    try {
        e.gather({folk[0], "no-such-person"});
    } catch (const orchestrator::Error&) {
        threw = true;
    }
    check::is_true(threw, "a uid the world does not name is refused");
}

TEST(calming_words_ease_a_frightened_speaker_a_step_when_the_danger_has_passed) {
    Director d(faced_world());
    const std::string x = resting_as("calm", 1).front();
    const auto calm = [&](const std::string& uid) { return d.respond(uid, {ActKind::Calm, ""}); };
    FactPacket p = calm(x);
    check::is_true(p.outcome == Outcome::NotAfraid && memory(p)["feeling"] == "calm", "nothing to ease at rest");
    d.fright(x, 3);
    p = calm(x);
    check::is_true(p.outcome == Outcome::StillAfraid && memory(p)["feeling"] == "panicked", "not while what frightened them is there");
    d.advance(minutes(16.0));
    p = calm(x);
    check::is_true(p.outcome == Outcome::Calmed, "eased once it has passed");
    check::equal(memory(p)["feeling"], std::string("afraid"), "a step");
    check::equal(memory(p)["mood"], std::string("friendly"), "and the speaker thinks better of the one who eased them");
    p = calm(x);
    check::is_true(p.outcome == Outcome::Calmed && memory(p)["feeling"] == "uneasy", "another step");
    check::equal(memory(p)["mood"], std::string("friendly"), "which counts once in a conversation");
    // The half hour before fear falls again counts from the easing, not from the fright.
    d.advance(minutes(20.0));
    check::equal(d.feeling(x).now, std::string("uneasy"), "twenty minutes after being eased");
    p = calm(x);
    check::is_true(p.outcome == Outcome::Calmed && memory(p)["feeling"] == "calm", "and at rest");
    check::equal(memory(p)["mood"], std::string("warm"), "eased in another conversation: counted again");
    check::is_true(calm(x).outcome == Outcome::NotAfraid, "then nothing to ease");
    // Not from someone they do not trust.
    const std::string y = resting_as("calm", 2).back();
    check::is_true(y != x, "the premise: a second speaker");
    d.fright(y, 2);
    d.advance(minutes(16.0));
    check::equal(memory(d.respond(y, {ActKind::Insult, ""}))["mood"], std::string("hostile"), "the premise: insulted");
    p = calm(y);
    check::is_true(p.outcome == Outcome::StillAfraid && memory(p)["feeling"] == "afraid", "hostile: not eased");
    // Someone uneasy at rest is not afraid.
    const std::string z = resting_as("uneasy", 1).front();
    p = calm(z);
    check::is_true(p.outcome == Outcome::NotAfraid && memory(p)["feeling"] == "uneasy", "uneasy at rest: nothing to ease");
}

TEST(a_face_that_does_not_know_the_player_is_not_soothed_by_them) {
    const fwg::World& w = faced_world();
    const fwg::Hero& beast = *heroes_with("calm", "unleashed").front();
    Director d(w);
    advance_to_a_day(d, [&](double day) { return fwg::moon_phase_on(w, day)->full; }, "the full moon");
    check::equal(d.respond(beast.uid, {ActKind::Greet, ""}).persona.face, std::string("unleashed"), "the premise: unleashed");
    d.fright(beast.uid, 2);
    d.advance(minutes(16.0));
    FactPacket p = d.respond(beast.uid, {ActKind::Calm, ""});
    check::is_true(p.outcome == Outcome::StillAfraid && memory(p)["feeling"] == "afraid", "unleashed, it does not know the player");
    (void)d.respond(beast.uid, {ActKind::Farewell, ""});
    advance_to_a_day(d, [&](double day) { return !full_moon_near(w, day); }, "a calm day after");
    d.fright(beast.uid, 2);
    d.advance(minutes(16.0));
    p = d.respond(beast.uid, {ActKind::Calm, ""});
    check::is_true(p.outcome == Outcome::Calmed && p.persona.face.empty(), "the control: its calm face is eased");
}

TEST(replay_restores_how_everyone_feels) {
    Director d(faced_world());
    const std::vector<std::string> folk = resting_as("calm", 3);
    d.fright(folk[0], 3);
    d.gather({folk[0], folk[1], folk[2]});
    d.advance(minutes(20.0));
    (void)d.respond(folk[1], {ActKind::Calm, ""});
    d.advance(minutes(20.0));
    Director again = orchestrator::replay(faced_world(), d.inputs());
    for (const std::string& uid : folk) {
        check::is_true(d.feeling(uid).now == again.feeling(uid).now, "the same feeling for " + uid);
        check::equal(text_of(d.respond(uid, {ActKind::Calm, ""})), text_of(again.respond(uid, {ActKind::Calm, ""})),
                     "the same next packet from " + uid);
    }
}

// ---- overhearing (contract version 10) ----------------------------------------------------------

namespace {

// A place two townsfolk know and two others do not: (two who do not, two who do, the place).
struct PlaceGap {
    std::string unaware, aware, place, also_unaware, also_aware;
};

PlaceGap place_gap() {
    const std::vector<std::string> folk = townsfolk(12);
    const auto knows = [](const std::string& who, const std::string& place) {
        Director probe(faced_world());
        return probe.respond(who, {ActKind::AskAboutPlace, place}).outcome == Outcome::Answered;
    };
    for (const std::string& aware : folk) {
        Director probe(faced_world());
        for (const Fact& place : facts_of(probe.respond(aware, {ActKind::AskSurroundings, ""}), FactKind::Place)) {
            std::vector<std::string> unaware, also;
            for (const std::string& other : folk) {
                if (other == aware) continue;
                (knows(other, place.subject) ? also : unaware).push_back(other);
            }
            if (unaware.size() >= 2 && !also.empty()) return {unaware[0], aware, place.subject, unaware[1], also[0]};
        }
    }
    check::fail("the premise: a place two townsfolk know and two others do not");
}

}  // namespace

TEST(a_speaker_remembers_being_eased_for_two_days) {
    const std::vector<std::string> folk = resting_as("calm", 2);
    const std::string x = folk[0], y = folk[1];
    Director d(faced_world());
    d.fright(x, 2);
    d.advance(minutes(16.0));
    FactPacket p = d.respond(x, {ActKind::Greet, ""});
    check::equal(memory(p)["eased"], std::string("no"), "not eased yet");
    p = d.respond(x, {ActKind::Calm, ""});
    check::is_true(p.outcome == Outcome::Calmed && memory(p)["eased"] == "yes", "eased, and it says so");
    // Turning to someone else ends the conversation, not the memory.
    (void)d.respond(y, {ActKind::Greet, ""});
    check::equal(memory(d.respond(x, {ActKind::SmallTalk, ""}))["eased"], std::string("yes"), "after talking to another");
    d.advance(1.0);
    check::equal(memory(d.respond(x, {ActKind::Greet, ""}))["eased"], std::string("yes"), "remembered the next day");
    d.advance(1.0);
    check::equal(memory(d.respond(x, {ActKind::Greet, ""}))["eased"], std::string("no"), "and gone two days on");
}

TEST(calming_words_overheard_ease_a_frightened_bystander_too) {
    const std::vector<std::string> folk = resting_as("calm", 3);
    const std::string a = folk[0], b = folk[1], c = folk[2];
    const auto frightened = [&](Director& d) {
        d.fright(a, 3);
        d.gather({a, b, c});
        d.advance(minutes(16.0));
    };
    Director d(faced_world());
    frightened(d);
    check::equal(d.feeling(b).now, std::string("afraid"), "the premise: the bystander afraid");
    check::is_true(d.respond(a, {ActKind::Calm, ""}).outcome == Outcome::Calmed, "the premise: the one spoken to eased");
    std::vector<orchestrator::Bystander> heard = d.overhear({a, b});
    check::is_true(heard.size() == 2 && heard[0].reaction == orchestrator::Overheard::stays_out, "the one spoken to heard nothing new");
    check::is_true(heard[1].reaction == orchestrator::Overheard::calmed && !heard[1].packet, "the bystander eased too");
    check::equal(d.feeling(b).now, std::string("uneasy"), "a step");
    // Not from words that did not ease the one spoken to.
    Director e(faced_world());
    frightened(e);
    e.fright(a, 3);
    check::is_true(e.respond(a, {ActKind::Calm, ""}).outcome == Outcome::StillAfraid, "the premise: not eased");
    check::is_true(e.overhear({b})[0].reaction == orchestrator::Overheard::stays_out && e.feeling(b).now == "afraid",
                   "calming that failed calms nobody");
    // Nor a bystander something still frightens, nor one hostile to the player.
    Director f(faced_world());
    frightened(f);
    f.fright(b, 2);
    (void)f.respond(c, {ActKind::Insult, ""});
    check::is_true(f.respond(a, {ActKind::Calm, ""}).outcome == Outcome::Calmed, "the premise: eased");
    heard = f.overhear({b, c});
    check::is_true(heard[0].reaction == orchestrator::Overheard::stays_out, "still frightened by something of their own");
    check::is_true(heard[1].reaction == orchestrator::Overheard::stays_out, "hostile to the player");
}

TEST(a_bystander_who_knows_what_the_one_spoken_to_did_not_joins_in) {
    const PlaceGap gap = place_gap();
    Director d(faced_world());
    check::is_true(d.respond(gap.unaware, {ActKind::AskAboutPlace, gap.place}).outcome == Outcome::Unknown, "the premise: unknown");
    const std::vector<orchestrator::Bystander> heard = d.overhear({gap.unaware, gap.aware});
    check::is_true(heard[1].reaction == orchestrator::Overheard::joins && heard[1].packet, "joins in");
    const FactPacket& p = *heard[1].packet;
    check::is_true(p.outcome == Outcome::Answered && p.answering == ActKind::AskAboutPlace && p.speaker == gap.aware, "and answers");
    const std::vector<Fact> places = facts_of(p, FactKind::Place);
    check::is_true(!places.empty() && places.front().subject == gap.place, "about the place asked of");
    check::equal(memory(p)["joined"], std::string("yes"), "marked joined");
    check::equal(memory(p)["turn"], std::string("1"), "a conversation with them begins");
    // Nobody joins who does not know it, and only the first who can.
    Director i(faced_world());
    (void)i.respond(gap.unaware, {ActKind::AskAboutPlace, gap.place});
    check::is_true(i.overhear({gap.also_unaware})[0].reaction == orchestrator::Overheard::stays_out, "one who does not know it");
    Director e(faced_world());
    (void)e.respond(gap.unaware, {ActKind::AskAboutPlace, gap.place});
    const std::vector<orchestrator::Bystander> twice = e.overhear({gap.aware, gap.aware});
    check::is_true(twice[0].reaction == orchestrator::Overheard::joins && twice[1].reaction == orchestrator::Overheard::stays_out,
                   "one joins, however many could");
    // Nor one hostile to the player, nor one panicking.
    Director f(faced_world());
    (void)f.respond(gap.aware, {ActKind::Insult, ""});
    (void)f.respond(gap.unaware, {ActKind::AskAboutPlace, gap.place});
    check::is_true(f.overhear({gap.aware})[0].reaction == orchestrator::Overheard::stays_out, "hostile");
    Director g(faced_world());
    g.fright(gap.aware, 3);
    (void)g.respond(gap.unaware, {ActKind::AskAboutPlace, gap.place});
    check::is_true(g.overhear({gap.aware})[0].reaction == orchestrator::Overheard::stays_out, "panicking");
    // An answer the one spoken to could give leaves nothing to add, even for one who knows it too.
    Director h(faced_world());
    check::is_true(h.respond(gap.aware, {ActKind::AskAboutPlace, gap.place}).outcome == Outcome::Answered, "the premise: answered");
    check::is_true(h.overhear({gap.also_aware})[0].reaction == orchestrator::Overheard::stays_out, "nothing to add");
}

TEST(a_bystander_who_knows_someone_the_one_spoken_to_did_not_joins_in) {
    const std::vector<std::string> folk = townsfolk(8);
    for (const std::string& asked : folk)
        for (const std::string& unaware : folk) {
            if (unaware == asked) continue;
            Director d(faced_world());
            if (d.respond(unaware, {ActKind::AskAboutPerson, asked}).outcome != Outcome::Unknown) continue;
            // Anyone knows someone standing where they stand: the one asked about knows themself.
            const std::vector<orchestrator::Bystander> heard = d.overhear({asked});
            check::is_true(heard[0].reaction == orchestrator::Overheard::joins && heard[0].packet &&
                               heard[0].packet->outcome == Outcome::Answered && heard[0].packet->answering == ActKind::AskAboutPerson,
                           "joins with the person");
            return;
        }
    check::fail("the premise: a townsperson who does not know another");
}

TEST(a_bystander_with_work_joins_one_who_had_none) {
    const std::string giver = givers().front();
    // Most townsfolk know of work where they stand: find two who know of none.
    std::vector<std::string> idle;
    for (const fwg::NpcPerson& person : faced_world().npcs.people) {
        if (idle.size() == 2) break;
        if (!person.alive || person.uid == giver || !node_of(faced_world(), person.uid)) continue;
        Director probe(faced_world());
        if (facts_of(probe.respond(person.uid, {ActKind::AskForWork, ""}), FactKind::QuestOffer).empty()) idle.push_back(person.uid);
    }
    if (idle.size() < 2) check::fail("the premise: two people with no work to speak of");
    Director d(faced_world());
    check::is_true(facts_of(d.respond(idle[0], {ActKind::AskForWork, ""}), FactKind::QuestOffer).empty(), "the premise: none");
    const std::vector<orchestrator::Bystander> heard = d.overhear({idle[1], giver});
    check::is_true(heard[0].reaction == orchestrator::Overheard::stays_out, "one with no work to give stays out");
    check::is_true(heard[1].reaction == orchestrator::Overheard::joins && heard[1].packet &&
                       !facts_of(*heard[1].packet, FactKind::QuestOffer).empty(),
                   "a giver joins with work");
}

TEST(overhearing_is_recorded_and_refused_when_nothing_was_said) {
    const PlaceGap gap = place_gap();
    Director d(faced_world());
    bool threw = false;
    try {
        (void)d.overhear({gap.aware});
    } catch (const orchestrator::Error&) {
        threw = true;
    }
    check::is_true(threw, "nothing said yet");
    (void)d.respond(gap.unaware, {ActKind::AskAboutPlace, gap.place});
    threw = false;
    try {
        (void)d.overhear({"no-such-person"});
    } catch (const orchestrator::Error&) {
        threw = true;
    }
    check::is_true(threw, "a uid the world does not name");
    (void)d.respond(gap.unaware, {ActKind::AskAboutPlace, gap.place});
    const std::size_t before = d.inputs().size();
    check::is_true(d.overhear({gap.aware})[0].reaction == orchestrator::Overheard::joins, "the premise: one joins");
    // What the game did, once: the joiner's reply is the overhearing's, not an input of its own
    // (recorded as one too, a replay would say the player asked the joiner).
    check::is_true(d.inputs().size() == before + 1 && std::holds_alternative<orchestrator::Overhear>(d.inputs().back()),
                   "recorded once, as the overhearing");
    Director again = orchestrator::replay(faced_world(), d.inputs());
    const Director::Acquaintance* a = d.acquaintance(gap.aware);
    const Director::Acquaintance* b = again.acquaintance(gap.aware);
    check::is_true(a && b && a->conversations == b->conversations && a->said == b->said, "the joiner's conversation, replayed");
    check::equal(text_of(d.respond(gap.aware, {ActKind::Greet, ""})), text_of(again.respond(gap.aware, {ActKind::Greet, ""})),
                 "the same next packet");
}

// Contract version 13: asked about themself, a hero tells what the world records of them beyond
// their card: what they go by, their fame, how they stand where they are, what they are after, a city
// they lost, when they were born, their deeds and ties. Never the card's secrets, and nothing of their
// work, which is asked for.
TEST(asked_about_themself_a_hero_tells_their_story_and_keeps_their_secrets) {
    const fwg::World& w = faced_world();
    const fwg::Hero* hero = nullptr;
    for (const fwg::Hero& h : w.heroes.people)
        if (!hero && h.status == "living" && h.faces.size() == 1 && node_of(w, h.uid) && !h.epithet.empty() && h.presence &&
            !h.claim.verb.empty() && h.lost && !h.deeds.empty() && h.rivals.size() + h.kin.size() + h.allies.size() >= 2)
            hero = &h;
    check::is_true(hero != nullptr, "the premise: a one-faced hero with an epithet, a claim, a city lost, deeds and two ties");
    Director d(w);
    const FactPacket p = d.respond(hero->uid, {ActKind::AskAboutThem, ""});
    check::is_true(p.outcome == Outcome::Answered, "answered");
    const std::vector<Fact> self = facts_of(p, FactKind::Self);
    check::is_true(self.size() == 1 && self.front().subject == hero->uid, "one Self fact, about the speaker");
    const Fact& me = self.front();
    check::equal(value(me, "known_as"), hero->epithet, "what they go by");
    check::equal(value(me, "fame"), hero->tier, "their fame");
    check::equal(value(me, "stands"), hero->presence->situation, "how they stand where they are");
    check::equal(value(me, "after"), hero->claim.verb, "what they are after");
    check::equal(value(me, "after_what"), hero->claim.target_name, "and its object");
    check::equal(value(me, "lost"), hero->lost->name, "the city they lost");
    // When, in bands against the world's latest age; and every hero's loss in a word a person uses.
    int now = 0;
    for (const fwg::AgeRecord& record : w.history.ages) now = std::max(now, record.age);
    const auto band = [&](int age) { return age >= now ? std::string("this age") : age == now - 1 ? std::string("the last age") : std::string("ages ago"); };
    check::equal(value(me, "since"), band(hero->born_age), "when they were born");
    check::equal(value(me, "lost_when"), band(hero->lost->destroyed_age), "when they lost it");
    const std::set<std::string> takers = {"war", "dragon", "monster", "magic", "radiance"};
    std::set<std::string> causes;
    for (const fwg::Hero& h : w.heroes.people) {
        if (h.status != "living" || h.faces.size() != 1 || !h.lost || !node_of(w, h.uid)) continue;
        Director each(w);
        const std::vector<Fact> theirs = facts_of(each.respond(h.uid, {ActKind::AskAboutThem, ""}), FactKind::Self);
        check::is_true(theirs.size() == 1 && takers.count(value(theirs.front(), "lost_to")) == 1,
                       "what took " + h.lost->name + " (" + h.lost->cause + "), in a word");
        causes.insert(h.lost->cause);
    }
    check::is_true(causes.count("dragon") == 1 && std::any_of(causes.begin(), causes.end(), [](const std::string& c) { return c.rfind("war", 0) == 0; }),
                   "the premise: cities lost to wars and to dragons");
    const std::set<std::string> bands = {"this age", "the last age", "ages ago"};
    check::equal(value(me, "role"), p.persona.role, "the role they present");
    // Their deeds, newest first, at most three.
    const std::vector<Fact> deeds = facts_of(p, FactKind::Deed);
    check::equal(deeds.size(), std::min<std::size_t>(3, hero->deeds.size()), "their deeds, at most three");
    for (const Fact& deed : deeds) {
        const auto it = std::find_if(hero->deeds.begin(), hero->deeds.end(), [&](const fwg::HeroDeed& x) { return x.event_id == deed.subject; });
        check::is_true(it != hero->deeds.end(), "a deed of their record: " + deed.subject);
        check::equal(value(deed, "deed"), it->event_kind, "its kind");
        check::equal(value(deed, "part"), it->role, "their part");
        check::is_true(bands.count(value(deed, "when")) == 1, "when");
    }
    // A deed at the city they lost is known by its name.
    const auto at_loss = std::find_if(deeds.begin(), deeds.end(), [&](const Fact& f) { return f.subject == hero->lost->ruin_id; });
    check::is_true(at_loss != deeds.end(), "the premise: a deed at the city they lost");
    check::equal(value(*at_loss, "place"), hero->lost->name, "named by the city lost");
    // Their ties, rivals first, at most three.
    std::vector<std::pair<std::string, std::string>> record;
    for (const auto& [tie, list] : {std::pair<std::string, const std::vector<std::string>*>{"rival", &hero->rivals},
                                    {"kin", &hero->kin},
                                    {"ally", &hero->allies}})
        for (const std::string& uid : *list) record.emplace_back(uid, tie);
    std::size_t tied = 0;
    for (const Fact& f : facts_of(p, FactKind::Person)) {
        const std::string* tie = f.field("tie");
        if (tie == nullptr) continue;
        check::is_true(std::find(record.begin(), record.end(), std::pair{f.subject, *tie}) != record.end(),
                       "a tie of their record: " + f.subject + " " + *tie);
        ++tied;
    }
    check::equal(tied, std::min<std::size_t>(3, record.size()), "their ties, at most three");
    // Never the card's secrets, and nothing of their work.
    std::string told;
    for (const Fact& f : p.facts)
        if (f.kind != FactKind::Memory)
            for (const auto& [key, v] : f.fields) told += v + "\n";
    const fwg::HeroPersona& card = hero->faces.front().persona;
    for (const std::string* secret : {&card.belief, &card.wants, &card.fears, &card.tells, &card.lies, &card.mechanic, &card.cornered})
        if (!secret->empty()) check::is_true(told.find(*secret) == std::string::npos, "no secret of the card: " + *secret);
    check::is_true(facts_of(p, FactKind::QuestOffer).empty() && facts_of(p, FactKind::QuestState).empty(), "nothing of their work");
    // What they stand against: the worst nest near, as small talk has it.
    const std::vector<Fact> near = facts_of(p, FactKind::Danger);
    const std::vector<Fact> chat = facts_of(d.respond(hero->uid, {ActKind::SmallTalk, ""}), FactKind::Danger);
    check::is_true(!chat.empty(), "the premise: a nest near them");
    check::is_true(near.size() == 1 && near.front().subject == chat.front().subject, "the worst nest near, as small talk has it");
    // Remembered as what was asked about, next time.
    (void)d.respond(hero->uid, {ActKind::Farewell, ""});
    d.advance(1.0);
    check::equal(memory(d.respond(hero->uid, {ActKind::Greet, ""})).at("asked_before"), p.persona.name, "asked about themself, last time");
    // Replayed, the same.
    Director again = orchestrator::replay(faced_world(), d.inputs());
    check::equal(text_of(d.respond(hero->uid, {ActKind::AskAboutThem, ""})), text_of(again.respond(hero->uid, {ActKind::AskAboutThem, ""})),
                 "the same next packet, replayed");
}

// A mask still worn says only what its cover would: its trade and where it stands. Exposed, it tells
// the story of the one behind it.
TEST(a_mask_says_only_its_trade_until_it_comes_off) {
    const fwg::World& w = faced_world();
    std::string mask, knower;
    for (const fwg::Hero* h : heroes_with("public", "true"))
        for (const std::vector<std::string>* list : {&h->rivals, &h->kin, &h->allies})
            for (const std::string& uid : *list) {
                if (!mask.empty()) break;
                Director probe(w);
                try {
                    if (probe.respond(uid, {ActKind::AskAboutPerson, h->uid}).outcome == Outcome::Answered) {
                        mask = h->uid;
                        knower = uid;
                    }
                } catch (const orchestrator::Error&) {
                }
            }
    check::is_true(!mask.empty(), "the premise: a mask whose rival, kin or ally knows where it stands");
    Director d(w);
    const FactPacket worn = d.respond(mask, {ActKind::AskAboutThem, ""});
    const std::vector<Fact> self = facts_of(worn, FactKind::Self);
    check::is_true(self.size() == 1, "one Self fact");
    for (const auto& [key, v] : self.front().fields)
        check::is_true(key == "role" || key == "at", "a worn mask says its trade and where it stands, not " + key + "=" + v);
    check::equal(value(self.front(), "role"), worn.persona.role, "the trade it shows");
    check::is_true(facts_of(worn, FactKind::Deed).empty(), "no deeds");
    for (const Fact& f : facts_of(worn, FactKind::Person)) check::is_true(f.field("tie") == nullptr, "no ties");
    (void)d.respond(mask, {ActKind::Farewell, ""});
    (void)d.respond(knower, {ActKind::AskAboutPerson, mask});
    check::is_true(d.respond(mask, {ActKind::Confront, ""}).outcome == Outcome::Exposed, "the premise: exposed with cause");
    const FactPacket off = d.respond(mask, {ActKind::AskAboutThem, ""});
    const std::vector<Fact> true_self = facts_of(off, FactKind::Self);
    check::is_true(true_self.size() == 1 && true_self.front().field("since") != nullptr, "exposed, the story behind the mask");
}

// A roster person asked about themself says what they are and where they work, and nothing more:
// the world keeps no story of them.
TEST(a_roster_person_asked_about_themself_gives_their_post_and_place) {
    const fwg::World& w = faced_world();
    const std::vector<std::string> folk = townsfolk(1);
    check::is_true(!folk.empty(), "the premise: townsfolk");
    Director d(w);
    const FactPacket p = d.respond(folk.front(), {ActKind::AskAboutThem, ""});
    const std::vector<Fact> self = facts_of(p, FactKind::Self);
    check::is_true(self.size() == 1, "one Self fact");
    check::equal(value(self.front(), "role"), p.persona.role, "their post");
    check::is_true(self.front().field("at") != nullptr, "where they stand");
    for (const auto& [key, v] : self.front().fields) check::is_true(key == "role" || key == "at", "nothing more: " + key);
    check::is_true(facts_of(p, FactKind::Deed).empty(), "no deeds");
}

// Asked for work, a speaker names their own and at most two others' (contract version 13): what the
// answer names is what a reply can say, and what they are remembered to have told. The first time
// they are asked in a conversation, someone with work of their own names only that (16), and in the
// next conversation, only that again.
TEST(asked_for_work_a_speaker_names_their_own_and_two_others) {
    const fwg::World& w = faced_world();
    Director d(w);
    std::size_t at_most = 0, with_own = 0, doubled = 0, own_alone = 0;
    const auto subjects = [](const FactPacket& p, bool own_only) {
        std::vector<std::string> out;
        for (const Fact& f : facts_of(p, FactKind::QuestOffer))
            if (!own_only || value(f, "from_speaker") == "yes") out.push_back(f.subject);
        return out;
    };
    for (const fwg::Hero& h : w.heroes.people) {
        if (h.status != "living" || !node_of(w, h.uid)) continue;
        const FactPacket first = d.respond(h.uid, {ActKind::AskForWork, ""});
        const FactPacket p = d.respond(h.uid, {ActKind::AskForWork, ""});  // asked again
        std::size_t mine = 0, theirs = 0;
        std::set<std::string> purposes;
        for (const Fact& f : facts_of(p, FactKind::QuestOffer)) {
            ++(value(f, "from_speaker") == "yes" ? mine : theirs);
            check::is_true(purposes.insert(value(f, "purpose")).second, h.uid + " names one errand twice: " + value(f, "purpose"));
        }
        // The first answer: their own alone when they have any, in the same order; else what the
        // second says.
        check::is_true(subjects(first, false) == subjects(p, mine > 0), h.uid + ": the first answer");
        if (mine > 0 && theirs > 0) ++own_alone;
        check::is_true(theirs <= 2, h.uid + " names " + std::to_string(theirs) + " others' work");
        if (theirs == 2) ++at_most;
        if (mine >= 1 && theirs == 2) ++with_own;
        // The premise for the check above: one errand offered by two givers where the speaker stands.
        std::map<std::string, std::set<std::string>> givers;
        const std::optional<int> here = node_of(w, h.uid);
        for (const fwg::QuestRow& q : quest_board(w, fwg::QuestState::Offered))
            if (q.giver_uid && *q.giver_uid != h.uid && q.stated_purpose && q.giver && q.giver->anchor.node &&
                *q.giver->anchor.node == *here && q.giver->liveness == fwg::Liveness::Present)
                givers[*q.stated_purpose].insert(*q.giver_uid);
        if (std::any_of(givers.begin(), givers.end(), [](const auto& g) { return g.second.size() >= 2; })) ++doubled;
        // Two others' errands, or all there are: one a purpose, none the speaker's own, and a skipped
        // second giver of one errand leaves room for another errand.
        std::set<std::string> own_purposes, errands;
        std::size_t unnamed = 0;
        for (const fwg::QuestRow& q : quest_board(w, fwg::QuestState::Offered))
            if (q.giver_uid && *q.giver_uid == h.uid) own_purposes.insert(q.stated_purpose.value_or(""));
        for (const fwg::QuestRow& q : quest_board(w, fwg::QuestState::Offered)) {
            if (!q.giver_uid || *q.giver_uid == h.uid || !q.giver || !q.giver->anchor.node || *q.giver->anchor.node != *here ||
                q.giver->liveness != fwg::Liveness::Present)
                continue;
            const std::string purpose = q.stated_purpose.value_or("");
            if (purpose.empty()) ++unnamed;
            else if (own_purposes.count(purpose) == 0) errands.insert(purpose);
        }
        check::equal(theirs, std::min<std::size_t>(2, errands.size() + unnamed), h.uid + ": two others' errands, or all there are");
        (void)d.respond(h.uid, {ActKind::Farewell, ""});
        check::is_true(subjects(d.respond(h.uid, {ActKind::AskForWork, ""}), false) == subjects(first, false),
                       h.uid + ": asked first in the next conversation, as in the last");
        (void)d.respond(h.uid, {ActKind::Farewell, ""});
    }
    check::is_true(at_most > 0 && with_own > 0, "the premise: speakers who know of more work than an answer names, givers among them");
    check::is_true(own_alone > 0, "the premise: speakers with work of their own who know of others'");
    check::is_true(doubled > 0, "the premise: speakers who know one errand from two givers");
}
