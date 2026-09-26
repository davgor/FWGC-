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
    (void)d.respond(beast.uid, {ActKind::Farewell, ""});
    advance_to_a_day(d, [&](double day) { return fwg::moon_phase_on(w, day)->full; }, "the full moon");
    p = d.respond(beast.uid, {ActKind::Greet, ""});
    check::is_true(p.persona.role == unleashed.apparent_role && p.persona.face == "unleashed" &&
                       p.persona.voice_key == key_of(beast, unleashed),
                   "unleashed under the full moon");
    const Memory raging = memory(p);
    check::is_true(raging.at("met") == "first" && raging.at("mood") == "hostile" && raging.at("heard") == "0", "knowing nobody");
    d.advance(1.0);
    check::equal(d.respond(beast.uid, {ActKind::Greet, ""}).persona.face, std::string("unleashed"), "and the day after");
    (void)d.respond(beast.uid, {ActKind::Farewell, ""});
    advance_to_a_day(d, [&](double day) { return !full_moon_near(w, day); }, "a calm day after");
    p = d.respond(beast.uid, {ActKind::Greet, ""});
    check::is_true(p.persona.face.empty() && memory(p)["times"] == "3", "calm again, and counting every meeting");
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
                           a->heard == b->heard,
                       "the same acquaintance with " + uid);
        check::equal(text_of(d.respond(uid, {ActKind::SmallTalk, ""})), text_of(again.respond(uid, {ActKind::SmallTalk, ""})),
                     "the same next packet from " + uid);
    }
}
