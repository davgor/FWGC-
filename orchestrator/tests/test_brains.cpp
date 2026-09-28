// The brain database against a real world: seed 99, size 33, a month after founding, when the
// quest board has opened -- the conversation tests' world, made again here. Events, what people
// hear of them and when, the News a speaker gives, and the SQL a game reads it all with. Every test
// that looks for something first checks the world holds it.
#include "orchestrator/director.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "check.hpp"
#include "fwg/api/quests.hpp"
#include "fwg/api/read.hpp"
#include "fwg/api/world_time.hpp"

namespace {

using orchestrator::ActKind;
using orchestrator::Cell;
using orchestrator::Director;
using orchestrator::Event;
using orchestrator::Fact;
using orchestrator::FactKind;
using orchestrator::FactPacket;
using orchestrator::Outcome;
using orchestrator::QueryResult;

const fwg::World& brains_world() {
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

double today(const Director& d) { return fwg::world_clock_of(d.world()).day; }

std::vector<fwg::QuestRow> board(const fwg::World& world, std::optional<fwg::QuestState> state = std::nullopt) {
    fwg::QuestsReadRequest request;
    if (state) request.states = std::vector<fwg::QuestState>{*state};
    return fwg::read_quests(world, request).quests;
}

// Where a roster person stands: their presence node, else their site's node.
std::optional<int> npc_node(const fwg::World& world, const fwg::NpcPerson& person) {
    if (person.presence_node) return person.presence_node;
    if (!person.site_uid) return std::nullopt;
    for (const fwg::NpcSite& site : world.npcs.sites)
        if (site.uid == *person.site_uid) return site.node;
    return std::nullopt;
}

std::optional<int> node_of(const fwg::World& world, const std::string& uid) {
    fwg::PersonReadRequest request;
    request.uid = uid;
    return fwg::read_person(world, request).person.anchor.node;
}

// Living roster people standing on `node`, but `except`.
std::vector<const fwg::NpcPerson*> roster_at(const fwg::World& world, int node, const std::set<std::string>& except = {}) {
    std::vector<const fwg::NpcPerson*> out;
    for (const fwg::NpcPerson& p : world.npcs.people)
        if (p.alive && !p.is_cast && !except.count(p.uid) && npc_node(world, p) == node) out.push_back(&p);
    return out;
}

// How far each place stands from `node`, in metres, out to `radius_m`: the near read, the oracle a
// rule about reach is held to.
std::map<int, double> places_within(const fwg::World& world, int node, double radius_m) {
    fwg::NearReadRequest near;
    near.node = node;
    near.radius_m = radius_m;
    near.categories = std::vector<std::string>{"settlements", "hamlets", "fortresses", "key_locations"};
    const fwg::NearRead read = fwg::read_near(world, near);
    std::map<int, double> out;
    const auto keep = [&](int at, double metres) {
        const auto found = out.find(at);
        if (found == out.end() || metres < found->second) out[at] = metres;
    };
    for (const auto& row : read.near.settlements) keep(row.node, row.distance_m);
    for (const auto& row : read.near.hamlets) keep(row.node, row.distance_m);
    for (const auto& row : read.near.fortresses) keep(row.node, row.distance_m);
    for (const auto& row : read.near.key_locations) keep(row.node, row.distance_m);
    return out;
}

double ceiling_m(const fwg::World& world) { return world.effective.globe_radius * fwg::kNearRadiusFraction; }

std::int64_t scalar(const Director& d, std::string_view sql) {
    const QueryResult r = d.query(sql);
    if (r.rows.size() != 1 || r.rows[0].size() != 1) check::fail("one value from " + std::string(sql));
    const auto* value = std::get_if<std::int64_t>(&r.rows[0][0]);
    if (value == nullptr) check::fail("an integer from " + std::string(sql));
    return *value;
}

std::string joined(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& item : items) out += (out.empty() ? "" : ", ") + item;
    return out;
}

std::string cell_text(const Cell& cell) {
    if (const auto* text = std::get_if<std::string>(&cell)) return *text;
    if (const auto* integer = std::get_if<std::int64_t>(&cell)) return std::to_string(*integer);
    if (const auto* real = std::get_if<double>(&cell)) return std::to_string(*real);
    return "NULL";
}

// What `person` knows `event` by, "state/how", or "" when they have not heard of it.
std::string known(const Director& d, const std::string& person, const std::string& event) {
    const QueryResult r = d.query("SELECT state || '/' || how FROM facts WHERE person = '" + person + "' AND event = '" + event + "'");
    return r.rows.empty() ? std::string() : cell_text(r.rows[0][0]);
}

std::optional<Event> event_of_work(const Director& d, const std::string& quest) {
    for (const Event& e : d.events())
        if (e.kind == "work" && e.quest_id == quest) return e;
    return std::nullopt;
}

std::vector<std::string> states_of(const Director& d, const std::string& event) {
    std::vector<std::string> out;
    for (const std::vector<Cell>& row : d.query("SELECT state FROM event_states WHERE event = '" + event + "' ORDER BY seq").rows)
        out.push_back(cell_text(row[0]));
    return out;
}

std::vector<Fact> facts_of(const FactPacket& p, FactKind kind) {
    std::vector<Fact> out;
    for (const Fact& f : p.facts)
        if (f.kind == kind) out.push_back(f);
    return out;
}

std::string value(const Fact& fact, std::string_view key) {
    const std::string* v = fact.field(key);
    return v ? *v : std::string("(none)");
}

// A piece of open work whose giver stands where roster folk stand too: the giver, the quest, where
// the giver stands, its difficulty, the node of the place it is for, and someone standing with the
// giver.
struct Work {
    std::string giver, quest;
    int node = 0;
    int difficulty = 0;
    std::optional<int> about;
    std::string neighbour;
};

// Every such piece of work, the easiest first.
std::vector<Work> open_works(const fwg::World& world) {
    std::vector<Work> out;
    for (const fwg::QuestRow& q : board(world, fwg::QuestState::Offered)) {
        if (!q.giver_uid || !q.giver || q.giver->liveness != fwg::Liveness::Present || !q.giver->anchor.node) continue;
        const std::vector<const fwg::NpcPerson*> there = roster_at(world, *q.giver->anchor.node, {*q.giver_uid});
        if (there.empty()) continue;
        Work work{*q.giver_uid, q.quest_id, *q.giver->anchor.node, 0, std::nullopt, there.front()->uid};
        for (const fwg::HeroQuestHook& hook : world.heroes.quest_hooks)
            if (hook.hook_id == q.quest_id) {
                work.difficulty = hook.difficulty;
                if (hook.has_target_node) work.about = hook.target_node;
            }
        if (work.difficulty > 0) out.push_back(work);
    }
    std::stable_sort(out.begin(), out.end(), [](const Work& a, const Work& b) { return a.difficulty < b.difficulty; });
    if (out.empty()) check::fail("the premise: open work whose giver stands among roster folk");
    return out;
}

// The hardest.
Work open_work(const fwg::World& world) { return open_works(world).back(); }

// How far word of finished work goes: by its difficulty, and never further than the read looks.
double work_reach(const fwg::World& world, int difficulty) {
    return std::min(ceiling_m(world),
                    std::array<double, 5>{5000.0, 10000.0, 20000.0, 40000.0, 60000.0}[static_cast<std::size_t>(difficulty - 1)]);
}

void take(Director& d, const Work& work) {
    (void)d.respond(work.giver, {ActKind::AskForWork, ""});
    if (d.respond(work.giver, {ActKind::AcceptQuest, work.quest}).outcome != Outcome::Accepted) check::fail("the premise: the work is taken");
}

// A roster person, living, standing on a node other than `apart`s, at a place `places` holds
// between `from_m` and `to_m`: whom word reaches later, or never.
std::string someone_between(const fwg::World& world, const std::map<int, double>& places, double from_m, double to_m,
                            const std::set<int>& apart) {
    for (const auto& [node, metres] : places) {
        if (metres <= from_m || metres > to_m || apart.count(node)) continue;
        const std::vector<const fwg::NpcPerson*> there = roster_at(world, node);
        if (!there.empty()) return there.front()->uid;
    }
    return {};
}

}  // namespace

TEST(every_piece_of_work_on_the_board_is_an_event_its_giver_knows) {
    const fwg::World& w = brains_world();
    const std::vector<fwg::QuestRow> rows = board(w);
    check::is_true(!rows.empty(), "the premise: a board a month on");
    Director d(w);
    std::map<std::string, Event> by_quest;
    for (const Event& e : d.events())
        if (e.kind == "work") by_quest.emplace(e.quest_id, e);
    check::equal(by_quest.size(), rows.size(), "one work event a row");
    std::size_t open = 0;
    for (const fwg::QuestRow& row : rows) {
        const auto found = by_quest.find(row.quest_id);
        if (found == by_quest.end()) check::fail("an event for " + row.quest_id);
        const Event& e = found->second;
        check::equal(e.person, row.giver_uid.value_or(""), "the event is its giver's");
        if (row.state != fwg::QuestState::Offered) continue;
        ++open;
        check::equal(e.state, std::string("asked"), "open work is asked for");
        check::equal(e.purpose, row.stated_purpose.value_or(""), "in the words the giver gives");
        if (row.giver_uid && row.giver && row.giver->liveness == fwg::Liveness::Present)
            check::equal(known(d, *row.giver_uid, e.id), std::string("asked/there"), "and its giver knows it");
    }
    check::is_true(open > 0, "the premise: open work");
}

TEST(taking_finishing_and_giving_up_work_move_its_event) {
    const Work work = open_work(brains_world());
    Director d(brains_world());
    take(d, work);
    std::optional<Event> e = event_of_work(d, work.quest);
    if (!e) check::fail("the work's event");
    check::equal(e->state, std::string("taken"), "taken");
    check::is_true(e->player && e->actor == "player", "by the player");
    d.resolve(work.quest, true);
    e = event_of_work(d, work.quest);
    check::equal(e->state, std::string("finished"), "finished");
    check::equal(joined(states_of(d, e->id)), std::string("asked, taken, finished"), "each state in turn");
    check::equal(known(d, work.giver, e->id), std::string("finished/there"), "its giver knows");

    Director g(brains_world());
    take(g, work);
    (void)g.respond(work.giver, {ActKind::AbandonQuest, work.quest});
    check::equal(event_of_work(g, work.quest)->state, std::string("given_up"), "given up");
}

TEST(word_of_open_work_stays_where_its_giver_stands) {
    const fwg::World& w = brains_world();
    const Work work = open_work(w);
    std::string elsewhere;
    for (const auto& [node, metres] : places_within(w, work.node, ceiling_m(w)))
        if (node != work.node && !roster_at(w, node).empty()) {
            elsewhere = roster_at(w, node).front()->uid;
            break;
        }
    check::is_true(!elsewhere.empty(), "the premise: roster folk elsewhere");
    Director d(w);
    const std::string id = event_of_work(d, work.quest)->id;
    check::equal(known(d, work.neighbour, id), std::string(), "nobody has told the giver's neighbour yet");
    const orchestrator::Related before = d.related(id);
    check::is_true(std::count(before.would_learn.begin(), before.would_learn.end(), work.neighbour) == 1,
                   "the neighbour is who the next share would tell");
    (void)d.share({});
    check::equal(known(d, work.neighbour, id), std::string("asked/word"), "the neighbour has heard");
    check::equal(known(d, elsewhere, id), std::string(), "nobody standing elsewhere has");
    const orchestrator::Related after = d.related(id);
    check::is_true(std::count(after.knows.begin(), after.knows.end(), work.neighbour) == 1 && after.would_learn.empty(),
                   "and nobody else would");
}

TEST(word_of_finished_work_starts_where_the_work_was_for_too) {
    const fwg::World& w = brains_world();
    Work work;
    std::string there;
    for (const Work& candidate : open_works(w)) {
        if (!candidate.about || *candidate.about == candidate.node) continue;
        const std::vector<const fwg::NpcPerson*> folk = roster_at(w, *candidate.about);
        if (folk.empty()) continue;
        work = candidate;
        there = folk.front()->uid;
    }
    check::is_true(!work.quest.empty(), "the premise: work for a place with roster folk, given from another");
    Director d(w);
    take(d, work);
    d.resolve(work.quest, true);
    (void)d.share({});
    check::equal(known(d, there, event_of_work(d, work.quest)->id), std::string("finished/word"),
                 "where the work was for hears at once, as where its giver stands does");
}

TEST(word_of_finished_work_travels_a_days_ride_a_day_as_far_as_its_difficulty) {
    const fwg::World& w = brains_world();
    // Work whose reach falls short of how far the read looks, so that someone can stand past it and
    // still within sight: the easiest that has both someone within reach and someone past it.
    Work work;
    std::string later, never;
    double later_m = 0.0;
    for (const Work& candidate : open_works(w)) {
        const double reach = work_reach(w, candidate.difficulty);
        if (reach + 2000.0 > ceiling_m(w)) continue;
        std::set<int> starts = {candidate.node};
        if (candidate.about) starts.insert(*candidate.about);
        // How far each place is from the nearer of where word starts: the giver's place, and the
        // place the work was for.
        std::map<int, double> nearest = places_within(w, candidate.node, ceiling_m(w));
        if (candidate.about)
            for (const auto& [node, metres] : places_within(w, *candidate.about, ceiling_m(w)))
                if (!nearest.count(node) || metres < nearest[node]) nearest[node] = metres;
        const std::string near = someone_between(w, nearest, 1000.0, reach, starts);
        const std::string past = someone_between(w, nearest, reach + 1000.0, ceiling_m(w), starts);
        if (near.empty() || past.empty()) continue;
        work = candidate;
        later = near;
        never = past;
        for (const auto& [node, metres] : nearest)
            for (const fwg::NpcPerson* p : roster_at(w, node))
                if (p->uid == later) later_m = metres;
        break;
    }
    check::is_true(!work.quest.empty(), "the premise: work with roster folk within its reach and past it, within the read");

    Director d(w);
    take(d, work);
    d.resolve(work.quest, true);
    const std::string id = event_of_work(d, work.quest)->id;
    (void)d.share({});
    check::equal(known(d, work.neighbour, id), std::string("finished/word"), "where the giver stands, at once");
    check::equal(known(d, later, id), std::string(), "further off, not yet");
    d.advance(std::ceil(later_m / 20000.0) + 0.25);
    (void)d.share({});
    check::equal(known(d, later, id), std::string("finished/word"), "once a day's ride a day has carried it");
    check::is_true(d.acquaintance(later) == nullptr, "hearing of work done somewhere else moves nobody");
    d.advance(10.0);
    (void)d.share({});
    check::equal(known(d, never, id), std::string(), "and never past its reach");
}

TEST(hearing_the_player_finished_or_failed_work_where_they_live_moves_them_once) {
    const Work work = open_work(brains_world());
    for (const bool finished : {true, false}) {
        Director d(brains_world());
        check::is_true(d.acquaintance(work.neighbour) == nullptr, "the premise: a neighbour the player never met");
        take(d, work);
        d.resolve(work.quest, finished);
        (void)d.share({});
        const Director::Acquaintance* heard = d.acquaintance(work.neighbour);
        if (heard == nullptr) check::fail("hearing of it leaves a mark");
        check::equal(heard->disposition, finished ? 1 : -1, finished ? "friendlier for work finished" : "warier for work failed");
        check::equal(heard->conversations, 0, "without ever having met");
        d.advance(1.0);
        (void)d.share({});
        check::equal(d.acquaintance(work.neighbour)->disposition, finished ? 1 : -1, "and only once");
        const FactPacket met = d.respond(work.neighbour, {ActKind::Greet, ""});
        std::string mood;
        for (const Fact& f : facts_of(met, FactKind::Memory)) mood = value(f, "mood");
        check::equal(mood, std::string(finished ? "friendly" : "wary"), "which a first meeting shows");
    }
}

TEST(frights_within_the_hour_at_one_place_are_one_scare_its_frightened_know) {
    const fwg::World& w = brains_world();
    const Work work = open_work(w);
    const std::vector<const fwg::NpcPerson*> there = roster_at(w, work.node);
    check::is_true(there.size() >= 2, "the premise: two roster folk at one place");
    const std::string a = there[0]->uid, b = there[1]->uid;
    Director d(w);
    const auto scares = [&] {
        std::vector<Event> out;
        for (const Event& e : d.events())
            if (e.kind == "scare") out.push_back(e);
        return out;
    };
    d.fright(a, 2);
    d.advance(40.0 / 1440.0);
    d.fright(b, 3);
    check::equal(scares().size(), std::size_t{1}, "one scare for frights forty minutes apart");
    const Event scare = scares().front();
    check::is_true(scare.node == work.node && scare.state == "frightened", "frightened, where they stand");
    check::equal(known(d, a, scare.id), std::string("frightened/there"), "the first frightened know it");
    check::equal(known(d, b, scare.id), std::string("frightened/there"), "and the second");
    d.advance(61.0 / 1440.0);
    d.fright(a, 1);
    check::equal(scares().size(), std::size_t{2}, "a fright more than an hour on is another");
}

TEST(calming_words_and_the_games_word_of_its_end_move_a_scare) {
    const fwg::World& w = brains_world();
    const Work work = open_work(w);
    const std::vector<const fwg::NpcPerson*> there = roster_at(w, work.node);
    check::is_true(there.size() >= 2 && there[0]->site_uid, "the premise: two roster folk at one site");
    const std::string a = there[0]->uid, b = there[1]->uid;
    Director d(w);
    d.fright(a, 2);
    d.advance(20.0 / 1440.0);  // what frightened them is gone
    check::is_true(d.respond(a, {ActKind::Calm, ""}).outcome == Outcome::Calmed, "the premise: talked down");
    std::string id;
    for (const Event& e : d.events())
        if (e.kind == "scare") id = e.id;
    check::equal(d.events().back().state, std::string("calmed"), "the player talked people down");
    d.report_event(*there[0]->site_uid, "beaten", "player");
    check::equal(joined(states_of(d, id)), std::string("frightened, calmed, beaten"), "then beat what threatened them");
    (void)d.share({b});
    check::equal(known(d, b, id), std::string("beaten/word"), "which the neighbour hears");
    const FactPacket chat = d.respond(b, {ActKind::SmallTalk, ""});
    const std::vector<Fact> news = facts_of(chat, FactKind::News);
    check::equal(news.size(), std::size_t{1}, "and brings up in small talk");
    check::equal(news[0].subject, id, "that scare");
    check::equal(value(news[0], "state"), std::string("beaten"), "beaten");
    check::equal(value(news[0], "by"), std::string("player"), "by the player");
    check::equal(value(news[0], "player"), std::string("yes"), "the player's doing");
    check::equal(value(news[0], "how"), std::string("word"), "heard, not seen");
}

TEST(a_threat_reported_ended_where_nothing_frightened_anyone_is_a_scare_of_its_own) {
    const fwg::World& w = brains_world();
    const Work work = open_work(w);
    const std::vector<const fwg::NpcPerson*> there = roster_at(w, work.node);
    check::is_true(!there.empty() && there[0]->site_uid, "the premise: roster folk at a site");
    Director d(w);
    const std::size_t before = d.events().size();
    d.report_event(*there[0]->site_uid, "lost", "");
    check::equal(d.events().size(), before + 1, "one event more");
    const Event e = d.events().back();
    check::is_true(e.kind == "scare" && e.state == "lost" && !e.player && e.node == work.node, "lost, by no one, there");
    (void)d.share({});
    check::equal(known(d, there[0]->uid, e.id), std::string("lost/word"), "heard of where it happened");
    // A scare the game says nothing of for more than a day is left as it was; its word ends another.
    d.fright(there[0]->uid, 2);
    const Event frightened = d.events().back();
    d.advance(2.0);
    d.report_event(*there[0]->site_uid, "beaten", "player");
    check::equal(d.events().back().state, std::string("beaten"), "a report two days on");
    check::is_true(d.events().back().id != frightened.id, "is a scare of its own");
    for (const Event& e2 : d.events())
        if (e2.id == frightened.id) check::equal(e2.state, std::string("frightened"), "and the old one is left frightened");
}

TEST(report_event_refuses_what_it_cannot_place_and_records_nothing) {
    const fwg::World& w = brains_world();
    const Work work = open_work(w);
    const std::vector<const fwg::NpcPerson*> there = roster_at(w, work.node);
    check::is_true(!there.empty() && there[0]->site_uid, "the premise: a site");
    Director d(w);
    const std::size_t events = d.events().size();
    check::throws([&] { d.report_event(*there[0]->site_uid, "won", "player"); }, "an outcome outside the three");
    check::throws([&] { d.report_event("nowhere-at-all", "beaten", "player"); }, "a place the world does not name");
    check::throws([&] { d.report_event(*there[0]->site_uid, "beaten", "nobody-at-all"); }, "a person it does not");
    check::throws([&] { (void)d.share({"nobody-at-all"}); }, "share, a person the world does not name");
    check::equal(d.events().size(), events, "no event");
    check::equal(d.inputs().size(), std::size_t{0}, "and nothing recorded");
}

TEST(word_of_a_mask_pulled_off_reaches_those_tied_to_it_wherever_they_are) {
    const fwg::World& w = brains_world();
    const auto has = [](const fwg::Hero& h, std::string_view label) {
        return std::any_of(h.faces.begin(), h.faces.end(), [&](const fwg::HeroFace& f) { return f.label == label; });
    };
    // A mask standing somewhere, and a living hero tied to it, standing far off.
    std::string mask, tie;
    for (const fwg::Hero& h : w.heroes.people) {
        if (!mask.empty()) break;
        if (h.status != "living" || !has(h, "public") || !has(h, "true") || !node_of(w, h.uid)) continue;
        const std::map<int, double> near = places_within(w, *node_of(w, h.uid), std::min(21000.0, ceiling_m(w)));
        for (const std::vector<std::string>* list : {&h.rivals, &h.kin, &h.allies})
            for (const std::string& uid : *list)
                for (const fwg::Hero& other : w.heroes.people)
                    if (tie.empty() && other.uid == uid && other.status == "living" && node_of(w, uid) && !near.count(*node_of(w, uid))) {
                        mask = h.uid;
                        tie = uid;
                    }
    }
    check::is_true(!mask.empty(), "the premise: a mask with a living tie standing more than 20 km off");
    Director d(w);
    for (int meeting = 1; meeting <= 3; ++meeting) {
        (void)d.respond(mask, {ActKind::Greet, ""});
        if (meeting < 3) (void)d.respond(mask, {ActKind::Farewell, ""});
    }
    check::is_true(d.respond(mask, {ActKind::Confront, ""}).outcome == Outcome::Exposed, "the premise: exposed");
    (void)d.respond(mask, {ActKind::Confront, ""});
    std::vector<Event> exposed;
    for (const Event& e : d.events())
        if (e.kind == "exposed") exposed.push_back(e);
    check::equal(exposed.size(), std::size_t{1}, "one event, however often confronted");
    check::is_true(exposed[0].person == mask && exposed[0].player && !exposed[0].revealed.empty(), "the mask, the player's doing");
    check::equal(known(d, mask, exposed[0].id), std::string("exposed/there"), "which the mask knows");
    (void)d.share({});
    check::equal(known(d, tie, exposed[0].id), std::string("exposed/word"), "and its tie hears, far off");
    // A day on, word has travelled further than the near read looks on this small world, and goes
    // no further than it looks.
    check::is_true(20000.0 > ceiling_m(w), "the premise: an exposure's word goes further than the read looks here");
    d.advance(2.0);
    const std::size_t heard = d.share({}).size();
    check::is_true(heard > 0, "word goes on spreading");
}

TEST(news_is_what_the_speaker_has_heard_and_nothing_more) {
    const fwg::World& w = brains_world();
    const Work work = open_work(w);
    Director d(w);
    check::is_true(facts_of(d.respond(work.neighbour, {ActKind::Greet, ""}), FactKind::News).empty(), "nothing heard, nothing said");
    (void)d.respond(work.neighbour, {ActKind::Farewell, ""});
    // What they hear first is the work asked for there, which the player had no part in.
    (void)d.share({work.neighbour});
    check::equal(known(d, work.neighbour, event_of_work(d, work.quest)->id), std::string("asked/word"), "the premise: heard of the work");
    check::is_true(facts_of(d.respond(work.neighbour, {ActKind::Greet, ""}), FactKind::News).empty(),
                   "a greeting says nothing of news the player had no part in");
    (void)d.respond(work.neighbour, {ActKind::Farewell, ""});
    take(d, work);
    check::is_true(facts_of(d.respond(work.neighbour, {ActKind::Greet, ""}), FactKind::News).empty(),
                   "nor of the player taking the work, which they have not heard");
    (void)d.respond(work.neighbour, {ActKind::Farewell, ""});
    d.resolve(work.quest, true);
    check::is_true(facts_of(d.respond(work.neighbour, {ActKind::Greet, ""}), FactKind::News).empty(), "not before word reaches them");
    (void)d.respond(work.neighbour, {ActKind::Farewell, ""});
    (void)d.share({});
    const FactPacket greeted = d.respond(work.neighbour, {ActKind::Greet, ""});
    const std::vector<Fact> news = facts_of(greeted, FactKind::News);
    check::equal(news.size(), std::size_t{1}, "then what the player did, in the greeting");
    const Event e = *event_of_work(d, work.quest);
    check::equal(news[0].subject, e.id, "that work");
    check::equal(value(news[0], "event"), std::string("work"), "work");
    check::equal(value(news[0], "state"), std::string("finished"), "finished");
    check::equal(value(news[0], "player"), std::string("yes"), "by the player");
    check::equal(value(news[0], "how"), std::string("word"), "as they heard it");
    check::equal(value(news[0], "since"), std::string("today"), "today");
    // Never the work's id, nor anything of what it was really for.
    for (const auto& [key, v] : news[0].fields) {
        check::is_true(v.find(work.quest) == std::string::npos, "no quest id in " + key);
        check::is_true(key != "effect" && key != "unwitting", "nothing of the actual effect: " + key);
    }
    check::is_true(facts_of(d.respond(work.giver, {ActKind::Greet, ""}), FactKind::News).size() == 1,
                   "the giver, who was there, says it too");
}

TEST(the_database_answers_reads_and_refuses_everything_else) {
    const fwg::World& w = brains_world();
    Director d(w);
    (void)d.respond(open_work(w).giver, {ActKind::Greet, ""});
    const std::int64_t events = scalar(d, "SELECT COUNT(*) FROM events");
    const std::int64_t people = scalar(d, "SELECT COUNT(*) FROM people");
    check::is_true(events > 0 && people > 0, "events and people to read");
    const std::size_t inputs = d.inputs().size();
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "fwgc_brains_attach_test.db";
    std::filesystem::remove(file);
    for (const std::string& sql : {std::string("INSERT INTO facts (person, event, state, day, how) VALUES ('a', 'b', 'c', 1, 'd')"),
                                   std::string("UPDATE events SET state = 'x'"), std::string("DELETE FROM events"),
                                   std::string("DROP TABLE events"), std::string("CREATE TABLE t (x)"),
                                   std::string("CREATE TEMP TABLE t (x)"), "ATTACH DATABASE '" + file.generic_string() + "' AS x",
                                   std::string("PRAGMA user_version"), std::string("PRAGMA user_version = 5"),
                                   std::string("SELECT 1; SELECT 2"), std::string("SELECT 1; DELETE FROM events"), std::string(""),
                                   std::string("   "), std::string("-- nothing"), std::string("VACUUM"), std::string("BEGIN"),
                                   std::string("SELECT * FROM nowhere"),
                                   std::string("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM n) SELECT COUNT(*) FROM n"),
                                   std::string("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM n WHERE x < 100001) SELECT x FROM n")})
        check::throws([&] { (void)d.query(sql); }, "refused: " + sql);
    check::is_true(!std::filesystem::exists(file), "no file was made");
    check::equal(scalar(d, "SELECT COUNT(*) FROM events"), events, "nothing written");
    check::equal(d.inputs().size(), inputs, "nothing recorded");
    const QueryResult many =
        d.query("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM n WHERE x < 100000) SELECT x FROM n");
    check::equal(many.rows.size(), std::size_t{100000}, "a hundred thousand rows is not too many");
    const QueryResult named = d.query("SELECT kind, state FROM events ORDER BY seq LIMIT 1");
    check::equal(joined(named.columns), std::string("kind, state"), "columns by name");
}

TEST(the_database_copies_the_worlds_people_places_work_and_acquaintances) {
    const fwg::World& w = brains_world();
    Director d(w);
    std::size_t roster = 0;
    for (const fwg::NpcPerson& p : w.npcs.people) roster += p.is_cast ? 0 : 1;
    check::equal(scalar(d, "SELECT COUNT(*) FROM people WHERE kind = 'roster'"), static_cast<std::int64_t>(roster), "every roster post");
    check::equal(scalar(d, "SELECT COUNT(*) FROM people WHERE kind = 'hero'"), static_cast<std::int64_t>(w.heroes.people.size()),
                 "every hero");
    const Work work = open_work(w);
    const fwg::NpcPerson* neighbour = nullptr;
    for (const fwg::NpcPerson& p : w.npcs.people)
        if (p.uid == work.neighbour) neighbour = &p;
    const QueryResult row = d.query("SELECT name, role, site, node, present FROM people WHERE uid = '" + work.neighbour + "'");
    check::equal(row.rows.size(), std::size_t{1}, "the neighbour's row");
    check::equal(cell_text(row.rows[0][0]), neighbour->name.value_or(""), "name");
    check::equal(cell_text(row.rows[0][1]), neighbour->post, "post");
    check::equal(cell_text(row.rows[0][2]), neighbour->site_uid.value_or(""), "site");
    check::equal(cell_text(row.rows[0][3]), std::to_string(work.node), "node");
    check::equal(scalar(d, "SELECT COUNT(*) FROM work"), static_cast<std::int64_t>(board(w).size()), "the whole board");
    check::equal(scalar(d, "SELECT COUNT(*) FROM places WHERE kind = 'city'"), static_cast<std::int64_t>(w.settlements.sites.size()),
                 "every city");
    check::equal(scalar(d, "SELECT COUNT(*) FROM relationships"), std::int64_t{0}, "nobody met");
    (void)d.respond(work.neighbour, {ActKind::Greet, ""});
    (void)d.respond(work.neighbour, {ActKind::Insult, ""});
    const Director::Acquaintance& a = *d.acquaintance(work.neighbour);
    const QueryResult met =
        d.query("SELECT conversations, regard, mood, grievances FROM relationships WHERE person = '" + work.neighbour + "'");
    check::equal(met.rows.size(), std::size_t{1}, "met now");
    check::equal(cell_text(met.rows[0][0]), std::to_string(a.conversations), "conversations");
    check::equal(cell_text(met.rows[0][1]), std::to_string(a.disposition), "regard");
    check::equal(cell_text(met.rows[0][2]), std::string("hostile"), "mood");
    check::equal(cell_text(met.rows[0][3]), std::string("1"), "one grievance");
    take(d, work);
    check::equal(cell_text(d.query("SELECT state FROM work WHERE quest_id = '" + work.quest + "'").rows.at(0).at(0)), std::string("taken"),
                 "work taken, as the board has it now");
}

TEST(replay_rebuilds_the_brains_table_by_table) {
    const fwg::World& w = brains_world();
    const Work work = open_work(w);
    const std::vector<const fwg::NpcPerson*> there = roster_at(w, work.node, {work.giver});
    check::is_true(there.size() >= 2 && there[0]->site_uid, "the premise: two roster folk with the giver");
    Director d(w);
    take(d, work);
    d.resolve(work.quest, true);
    d.fright(there[0]->uid, 2);
    d.advance(20.0 / 1440.0);
    (void)d.respond(there[0]->uid, {ActKind::Calm, ""});
    d.report_event(*there[0]->site_uid, "beaten", "player");
    (void)d.share({});
    d.advance(3.0);
    (void)d.share({there[1]->uid});
    (void)d.respond(there[1]->uid, {ActKind::Greet, ""});
    Director again = orchestrator::replay(w, d.inputs());
    for (const std::string& table :
         {"people", "places", "work", "relationships", "events", "event_states", "facts", "exchanges", "exchange_facts", "memories"}) {
        const QueryResult live = d.query("SELECT * FROM " + table + " ORDER BY 1, 2");
        const QueryResult replayed = again.query("SELECT * FROM " + table + " ORDER BY 1, 2");
        check::equal(replayed.rows.size(), live.rows.size(), table + " rows");
        for (std::size_t r = 0; r < live.rows.size(); ++r)
            for (std::size_t c = 0; c < live.rows[r].size(); ++c)
                if (!(live.rows[r][c] == replayed.rows[r][c]))
                    check::fail(table + " row " + std::to_string(r) + " column " + live.columns[c] + ": " + cell_text(live.rows[r][c]) +
                                " live, " + cell_text(replayed.rows[r][c]) + " replayed");
    }
}

TEST(a_copied_director_keeps_brains_of_its_own) {
    const fwg::World& w = brains_world();
    const Work work = open_work(w);
    Director d(w);
    const std::int64_t events = scalar(d, "SELECT COUNT(*) FROM events");
    Director copy = d;
    copy.fright(work.neighbour, 2);
    (void)copy.share({});
    check::equal(scalar(copy, "SELECT COUNT(*) FROM events"), events + 1, "the copy's scare");
    check::equal(scalar(d, "SELECT COUNT(*) FROM events"), events, "is not the original's");
    check::equal(scalar(d, "SELECT COUNT(*) FROM facts WHERE how = 'word'"), std::int64_t{0}, "nor is what the copy shared");
}

TEST(work_nobody_takes_lapses_and_its_giver_knows) {
    const fwg::World& w = brains_world();
    const Work work = open_work(w);
    double expires = 0.0;
    for (const fwg::QuestRow& q : board(w, fwg::QuestState::Offered))
        if (q.quest_id == work.quest) expires = q.expires_day;
    Director d(w);
    check::is_true(expires > today(d), "the premise: the work still open");
    d.advance(std::floor(expires - today(d)) + 2.0);
    const std::optional<Event> e = event_of_work(d, work.quest);
    if (!e) check::fail("the work's event");
    check::equal(e->state, std::string("lapsed"), "left undone past its day, it lapses");
    check::is_true(!e->player && e->actor.empty(), "nobody's doing");
    check::equal(known(d, work.giver, e->id), std::string("lapsed/there"), "and its giver knows");
}

TEST(share_tells_only_the_people_it_names) {
    const fwg::World& w = brains_world();
    const Work work = open_work(w);
    const std::vector<const fwg::NpcPerson*> there = roster_at(w, work.node, {work.giver});
    check::is_true(there.size() >= 2, "the premise: two roster folk with the giver");
    Director d(w);
    const std::vector<orchestrator::Learned> learned = d.share({there[0]->uid});
    check::is_true(!learned.empty(), "the one named hears something");
    for (const orchestrator::Learned& one : learned) check::equal(one.person, there[0]->uid, "and only they do");
    check::equal(scalar(d, "SELECT COUNT(*) FROM facts WHERE person = '" + there[1]->uid + "'"), std::int64_t{0},
                 "the one not named hears nothing");
}
