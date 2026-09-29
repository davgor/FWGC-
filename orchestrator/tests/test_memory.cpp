// Short and long memory against a real world: seed 99, size 33, a month after founding, as the
// conversation and brain tests have it. Everyone the player speaks to keeps a log of it, those who
// overhear keep what they heard, compact() makes a log into what a person remembers, and a packet's
// Recall facts bring it back. Every test that looks for something first checks the world holds it.
#include "orchestrator/director.hpp"

#include <algorithm>
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
#include "knowledge.hpp"

namespace {

using orchestrator::ActKind;
using orchestrator::Cell;
using orchestrator::Compacted;
using orchestrator::Director;
using orchestrator::Fact;
using orchestrator::FactKind;
using orchestrator::FactPacket;
using orchestrator::Outcome;
using orchestrator::QueryResult;
using orchestrator::Remembered;

const fwg::World& memory_world() {
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

std::string cell_text(const Cell& cell) {
    if (const auto* text = std::get_if<std::string>(&cell)) return *text;
    if (const auto* integer = std::get_if<std::int64_t>(&cell)) return std::to_string(*integer);
    if (const auto* real = std::get_if<double>(&cell)) return std::to_string(*real);
    return "NULL";
}

// Every row `sql` answers, each cell as text ("NULL" for none).
std::vector<std::vector<std::string>> rows(const Director& d, const std::string& sql) {
    std::vector<std::vector<std::string>> out;
    for (const std::vector<Cell>& row : d.query(sql).rows) {
        std::vector<std::string>& texts = out.emplace_back();
        for (const Cell& cell : row) texts.push_back(cell_text(cell));
    }
    return out;
}

std::int64_t scalar(const Director& d, const std::string& sql) {
    const QueryResult r = d.query(sql);
    if (r.rows.size() != 1 || r.rows[0].size() != 1) check::fail("one value from " + sql);
    const auto* value = std::get_if<std::int64_t>(&r.rows[0][0]);
    if (value == nullptr) check::fail("an integer from " + sql);
    return *value;
}

std::string quoted(const std::string& text) { return "'" + text + "'"; }

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

std::vector<fwg::QuestRow> board(const fwg::World& world, fwg::QuestState state) {
    fwg::QuestsReadRequest request;
    request.states = std::vector<fwg::QuestState>{state};
    return fwg::read_quests(world, request).quests;
}

std::optional<int> npc_node(const fwg::World& world, const fwg::NpcPerson& person) {
    if (person.presence_node) return person.presence_node;
    if (!person.site_uid) return std::nullopt;
    for (const fwg::NpcSite& site : world.npcs.sites)
        if (site.uid == *person.site_uid) return site.node;
    return std::nullopt;
}

// A living roster person who gives no work, in a city: whom most of these tests talk to.
std::string townsperson(std::size_t n) {
    const fwg::World& w = memory_world();
    std::set<std::string> givers;
    for (const fwg::QuestRow& q : board(w, fwg::QuestState::Offered))
        if (q.giver_uid) givers.insert(*q.giver_uid);
    std::set<int> nodes;
    std::size_t seen = 0;
    for (const fwg::NpcPerson& p : w.npcs.people) {
        if (!p.alive || p.is_cast || givers.count(p.uid) || !(p.site_kind && *p.site_kind == "city")) continue;
        const std::optional<int> node = npc_node(w, p);
        if (!node || !nodes.insert(*node).second) continue;
        if (seen++ == n) return p.uid;
    }
    check::fail("the premise: " + std::to_string(n + 1) + " city folk standing apart");
}

// Open work whose giver stands where roster folk stand too: the giver, the quest, and someone
// standing with the giver.
struct Work {
    std::string giver, quest, neighbour, second;
};

Work open_work() {
    const fwg::World& w = memory_world();
    for (const fwg::QuestRow& q : board(w, fwg::QuestState::Offered)) {
        if (!q.giver_uid || !q.giver || q.giver->liveness != fwg::Liveness::Present || !q.giver->anchor.node) continue;
        std::vector<std::string> there;
        for (const fwg::NpcPerson& p : w.npcs.people)
            if (p.alive && !p.is_cast && p.uid != *q.giver_uid && npc_node(w, p) == q.giver->anchor.node) there.push_back(p.uid);
        if (there.size() >= 2) return Work{*q.giver_uid, q.quest_id, there[0], there[1]};
    }
    check::fail("the premise: open work whose giver stands with two roster folk");
}

void take(Director& d, const Work& work) {
    (void)d.respond(work.giver, {ActKind::AskForWork, ""});
    if (d.respond(work.giver, {ActKind::AcceptQuest, work.quest}).outcome != Outcome::Accepted) check::fail("the premise: the work is taken");
}

// A place `speaker` knows: the first their surroundings name, and its fact.
Fact a_place_known_to(Director& d, const std::string& speaker) {
    Director trial = d;
    const std::vector<Fact> places = facts_of(trial.respond(speaker, {ActKind::AskSurroundings, ""}), FactKind::Place);
    if (places.empty()) check::fail("the premise: a place " + speaker + " knows");
    return places.front();
}

const Remembered* find_memory(const std::vector<Remembered>& all, std::string_view memory, std::string_view subject,
                              std::string_view act = {}) {
    for (const Remembered& r : all)
        if (r.memory == memory && r.subject == subject && (act.empty() || r.act == act)) return &r;
    return nullptr;
}

std::size_t count_of(const std::vector<Remembered>& all, std::string_view memory) {
    return static_cast<std::size_t>(std::count_if(all.begin(), all.end(), [&](const Remembered& r) { return r.memory == memory; }));
}

bool has_refusal(const Compacted& c, std::string_view says) {
    return std::any_of(c.refused.begin(), c.refused.end(), [&](const std::string& why) { return why.find(says) != std::string::npos; });
}

// The Recall facts of a packet, by memory; and that they all stand before its Memory fact.
std::map<std::string, Fact> recalled(const FactPacket& p) {
    std::map<std::string, Fact> out;
    bool memory_seen = false;
    for (const Fact& f : p.facts) {
        if (f.kind == FactKind::Memory) memory_seen = true;
        if (f.kind != FactKind::Recall) continue;
        check::is_true(!memory_seen, "a Recall fact before the Memory fact");
        if (!out.emplace(value(f, "memory"), f).second) check::fail("one Recall fact of each memory, twice " + value(f, "memory"));
    }
    return out;
}

std::string standing(const FactPacket& p) {
    const std::vector<Fact> memory = facts_of(p, FactKind::Memory);
    if (memory.size() != 1) check::fail("one Memory fact");
    return value(memory.front(), "standing");
}

// Every table of the brains, a replayed director's against the live one's, cell for cell.
void same_tables(const Director& live, const Director& replayed, const std::vector<std::string>& tables) {
    for (const std::string& table : tables) {
        const QueryResult a = live.query("SELECT * FROM " + table + " ORDER BY 1, 2");
        const QueryResult b = replayed.query("SELECT * FROM " + table + " ORDER BY 1, 2");
        check::equal(b.rows.size(), a.rows.size(), table + " rows");
        for (std::size_t r = 0; r < a.rows.size(); ++r)
            for (std::size_t c = 0; c < a.rows[r].size(); ++c)
                if (!(a.rows[r][c] == b.rows[r][c]))
                    check::fail(table + " row " + std::to_string(r) + " column " + a.columns[c] + ": " + cell_text(a.rows[r][c]) +
                                " live, " + cell_text(b.rows[r][c]) + " replayed");
    }
}

}  // namespace

TEST(every_exchange_goes_into_the_log_of_the_one_spoken_to) {
    const std::string speaker = townsperson(0);
    Director d(memory_world());
    const Fact place = a_place_known_to(d, speaker);
    check::equal(scalar(d, "SELECT COUNT(*) FROM exchanges"), std::int64_t{0}, "nobody spoken to, nothing logged");
    (void)d.respond(speaker, {ActKind::Greet, ""});
    const FactPacket around = d.respond(speaker, {ActKind::AskSurroundings, ""});
    const FactPacket asked = d.respond(speaker, {ActKind::AskAboutPlace, place.subject});
    check::is_true(asked.outcome == Outcome::Answered, "the premise: the place answered for");
    const std::vector<std::vector<std::string>> log =
        rows(d, "SELECT id, conversation, turn, act, target, about, outcome, heard_from, of_exchange, joined, line, reply, day FROM "
                "exchanges WHERE person = " + quoted(speaker) + " ORDER BY id");
    check::equal(log.size(), std::size_t{3}, "three exchanges");
    const std::vector<std::vector<std::string>> want = {{"1", "1", "greet", "", "", "greeted"},
                                                        {"1", "2", "ask_surroundings", "", "", "answered"},
                                                        {"1", "3", "ask_about_place", place.subject,
                                                         orchestrator::knowledge::place_label(place), "answered"}};
    for (std::size_t i = 0; i < want.size(); ++i) {
        for (std::size_t c = 0; c < want[i].size(); ++c)
            check::equal(log[i][c + 1], want[i][c], "exchange " + std::to_string(i + 1) + ", column " + std::to_string(c + 1));
        check::is_true(log[i][7] == "NULL" && log[i][8] == "NULL" && log[i][9] == "0", "their own, not overheard or joined");
        check::is_true(log[i][10] == "NULL" && log[i][11] == "NULL", "no words until the game hands them over");
        check::equal(log[i][12], std::to_string(today(d)), "when");
    }
    // What the reply named, in its order: the places and the nests it gave.
    std::vector<std::string> named;
    for (const Fact& f : around.facts)
        if (f.kind == FactKind::Place || f.kind == FactKind::Danger) named.push_back(std::string(orchestrator::to_string(f.kind)) + " " + f.subject);
    check::is_true(!named.empty(), "the premise: the surroundings named something");
    std::vector<std::string> kept;
    for (const std::vector<std::string>& row : rows(d, "SELECT kind, subject FROM exchange_facts WHERE exchange = " + log[1][0] + " ORDER BY seq"))
        kept.push_back(row[0] + " " + row[1]);
    check::is_true(kept == named, "the exchange keeps what the reply named, in order");
    check::equal(scalar(d, "SELECT COUNT(*) FROM exchange_facts WHERE exchange = " + log[0][0]), std::int64_t{0}, "a greeting names nothing");
    // The game's cleanup list: a row for each person with a log.
    const std::string other = townsperson(1);
    (void)d.respond(other, {ActKind::Greet, ""});
    const std::vector<std::vector<std::string>> active = rows(d, "SELECT person, exchanges FROM active_short ORDER BY exchanges DESC");
    check::is_true(active == std::vector<std::vector<std::string>>{{speaker, "3"}, {other, "1"}},
                   "each one spoken to, with how many exchanges");
}

TEST(words_are_kept_with_their_exchange_by_whoever_heard_it) {
    const Work work = open_work();
    Director d(memory_world());
    check::throws([&] { d.words("Hello.", "Hello."); }, "words before anything is said");
    check::equal(d.inputs().size(), std::size_t{0}, "and nothing recorded");
    (void)d.respond(work.giver, {ActKind::Greet, ""});
    d.words("Good morning.", "Morning to you.");
    (void)d.overhear({work.neighbour, work.giver});
    // Words before the overhearing: the bystander's copy carries them.
    const std::string giver_log = "SELECT id, line, reply FROM exchanges WHERE person = " + quoted(work.giver) + " ORDER BY id";
    const std::string neighbour_log =
        "SELECT id, line, reply, heard_from, of_exchange, conversation, act, outcome FROM exchanges WHERE person = " + quoted(work.neighbour) +
        " ORDER BY id";
    std::vector<std::vector<std::string>> given = rows(d, giver_log);
    std::vector<std::vector<std::string>> heard = rows(d, neighbour_log);
    check::equal(given.size(), std::size_t{1}, "the one spoken to heard it once, not again as a bystander");
    check::equal(heard.size(), std::size_t{1}, "the bystander keeps what they overheard");
    check::is_true(given[0][1] == "Good morning." && given[0][2] == "Morning to you.", "the words with the exchange");
    check::is_true(heard[0][1] == "Good morning." && heard[0][2] == "Morning to you.", "and with the bystander's copy");
    check::is_true(heard[0][3] == work.giver && heard[0][4] == given[0][0] && heard[0][5] == "0", "heard from the giver, that exchange");
    check::is_true(heard[0][6] == "greet" && heard[0][7] == "greeted", "the act and what came of it");
    check::throws([&] { (void)d.overhear({work.second, "npc-nobody-at-all"}); }, "a bystander the world does not name");
    check::equal(scalar(d, "SELECT COUNT(*) FROM exchanges WHERE person = " + quoted(work.second)), std::int64_t{0},
                 "and nobody's log keeps that overhearing");
    // Words after the overhearing reach the bystander's copy too.
    (void)d.respond(work.giver, {ActKind::SmallTalk, ""});
    (void)d.overhear({work.neighbour});
    d.words("Fine weather.", "It is that.");
    given = rows(d, giver_log);
    heard = rows(d, neighbour_log);
    check::is_true(given.size() == 2 && given[1][1] == "Fine weather." && given[1][2] == "It is that.", "the second exchange's words");
    check::is_true(heard.size() == 2 && heard[1][1] == "Fine weather." && heard[1][2] == "It is that.", "the second copy's words");
    check::equal(given[0][1], std::string("Good morning."), "and the first exchange's left as they were");
    const std::size_t inputs = d.inputs().size();
    check::throws([&] { d.words(std::string(4001, 'a'), ""); }, "a line longer than 4,000 bytes");
    check::throws([&] { d.words("", std::string(4001, 'a')); }, "a reply longer than 4,000 bytes");
    check::throws([&] { d.words("\xC3\x28", ""); }, "a line that is not UTF-8");
    check::equal(d.inputs().size(), inputs, "nothing recorded for any of them");
    d.words(std::string(4000, 'a'), "");
    check::equal(d.inputs().size(), inputs + 1, "4,000 bytes is not too long");
}

// The owner's example: a villager knows of the work asked for their town, overhears the player
// greeted by its giver once it is done, is compacted, and remembers the player's part in it.
TEST(a_villager_who_overhears_the_work_done_remembers_the_players_part) {
    const Work work = open_work();
    Director d(memory_world());
    (void)d.share({work.neighbour});
    std::string event;
    for (const orchestrator::Event& e : d.events())
        if (e.kind == "work" && e.quest_id == work.quest) event = e.id;
    const std::string known = "SELECT state || '/' || how FROM facts WHERE person = " + quoted(work.neighbour) + " AND event = " + quoted(event);
    check::is_true(!event.empty() && rows(d, known) == std::vector<std::vector<std::string>>{{"asked/word"}},
                   "the premise: the villager has heard the work was asked for");
    take(d, work);
    d.resolve(work.quest, true);
    const FactPacket greeted = d.respond(work.giver, {ActKind::Greet, ""});
    const std::vector<Fact> news = facts_of(greeted, FactKind::News);
    check::is_true(news.size() == 1 && news[0].subject == event && value(news[0], "state") == "finished" && value(news[0], "player") == "yes",
                   "the premise: the giver's greeting carries the work finished by the player");
    d.words("It's done.", "You're the one who helped us hold.");
    check::equal(rows(d, known)[0][0], std::string("asked/word"), "the premise: no word of it has reached the villager");
    check::is_true(d.acquaintance(work.neighbour) == nullptr, "the premise: the villager has no regard for the player yet");
    (void)d.overhear({work.neighbour});
    check::equal(rows(d, known)[0][0], std::string("finished/overheard"), "overhearing it, they know it");
    check::is_true(d.acquaintance(work.neighbour) && d.acquaintance(work.neighbour)->disposition == 1,
                   "and take the player better for it, having heard it where they live");
    const Compacted done = d.compact(work.neighbour, "");
    check::equal(done.cleared, std::size_t{1}, "the one exchange overheard, cleared");
    const Remembered* overheard = find_memory(done.kept, "overheard", work.giver, "greet");
    check::is_true(overheard != nullptr && overheard->times == 1, "remembering whom they overheard the player with");
    const std::vector<Fact> said = facts_of(d.respond(work.neighbour, {ActKind::Greet, ""}), FactKind::News);
    check::is_true(said.size() == 1 && said[0].subject == event && value(said[0], "state") == "finished" &&
                       value(said[0], "player") == "yes" && value(said[0], "how") == "overheard",
                   "greeted, they have the player's part in it to say, as they heard it");
    const std::vector<orchestrator::Learned> later = d.share({work.neighbour});
    check::is_true(std::none_of(later.begin(), later.end(), [&](const orchestrator::Learned& l) { return l.event == event; }),
                   "and word of it later tells them nothing new");
    check::equal(d.acquaintance(work.neighbour)->disposition, 1, "nor moves them twice");
}

TEST(compact_makes_a_log_into_what_was_asked_told_and_done_and_clears_it) {
    const std::string speaker = townsperson(1);
    Director d(memory_world());
    const Fact place = a_place_known_to(d, speaker);
    const double first = today(d);
    (void)d.respond(speaker, {ActKind::AskAboutPlace, place.subject});
    (void)d.respond(speaker, {ActKind::Thank, ""});
    (void)d.respond(speaker, {ActKind::Farewell, ""});
    d.advance(1.0);
    const double second = today(d);
    (void)d.respond(speaker, {ActKind::AskAboutPlace, place.subject});
    (void)d.respond(speaker, {ActKind::AskSurroundings, ""});
    check::is_true(d.acquaintance(speaker)->talking, "the premise: a conversation under way");
    const std::size_t inputs = d.inputs().size();
    const Compacted done = d.compact(speaker, "");
    check::equal(done.cleared, std::size_t{5}, "five exchanges cleared");
    check::equal(scalar(d, "SELECT COUNT(*) FROM exchanges WHERE person = " + quoted(speaker)), std::int64_t{0}, "the log is empty");
    check::equal(scalar(d, "SELECT COUNT(*) FROM active_short"), std::int64_t{0}, "and nobody is left to clean up");
    check::is_true(!d.acquaintance(speaker)->talking, "the conversation under way ended");
    check::equal(d.inputs().size(), inputs + 1, "recorded");
    const std::vector<Remembered> all = d.memories(speaker);
    check::equal(done.kept.size(), all.size(), "everything kept is new");
    const std::string label = orchestrator::knowledge::place_label(place);
    const Remembered* asked = find_memory(all, "asked", place.subject, "ask_about_place");
    check::is_true(asked && asked->times == 2 && asked->first_day == first && asked->last_day == second && asked->about == label,
                   "asked about the place twice, first then last");
    const Remembered* looked = find_memory(all, "asked", "", "ask_surroundings");
    check::is_true(looked && looked->times == 1, "asked what is around, once");
    const Remembered* told = find_memory(all, "told", place.subject, "place");
    check::is_true(told && told->times == 2 && told->first_day == first && told->about == label,
                   "told of the place in both answers, not again for its name among the surroundings");
    std::size_t places_told = 0;
    for (const Remembered& r : all) places_told += r.memory == "told" && r.act == "place" ? 1 : 0;
    check::equal(places_told, std::size_t{1}, "nor any other place the surroundings named");
    const Remembered* thanked = find_memory(all, "did", "", "thanked");
    check::is_true(thanked && thanked->times == 1, "thanked, once");
    const Remembered* last = find_memory(all, "last", place.subject, "ask_about_place");
    check::is_true(last && last->last_day == second && count_of(all, "last") == 1,
                   "what they last spoke of: the place asked after, not the surroundings, which named no one thing");
    check::equal(count_of(all, "said"), std::size_t{0}, "no answer of the stranger's to keep");
    for (const Remembered& r : all) check::is_true(r.source == "rules" && r.person == speaker, "all by the rules, all theirs");
    for (std::size_t i = 1; i < all.size(); ++i) check::is_true(all[i].seq > all[i - 1].seq, "in the order kept");
    // Compacted again with nothing new, nothing changes.
    const Compacted again = d.compact(speaker, "");
    check::is_true(again.kept.empty() && again.cleared == 0, "a second compaction with an empty log changes nothing");
}

TEST(a_memory_comes_up_to_date_and_keeps_when_it_first_was) {
    const Work work = open_work();
    Director d(memory_world());
    take(d, work);
    const double taken = today(d);
    (void)d.compact(work.giver, "");
    std::vector<Remembered> all = d.memories(work.giver);
    const Remembered* did = find_memory(all, "did", work.quest);
    const Remembered* open = find_memory(all, "open", work.quest);
    check::is_true(did && did->act == "taken" && open && open->act == "taken" && open->first_day == taken,
                   "the premise: work taken, and held");
    check::is_true(find_memory(all, "told", work.quest, "quest_offer") != nullptr, "and told of, when asked for work");
    std::string purpose;
    for (const fwg::QuestRow& q : board(memory_world(), fwg::QuestState::Offered))
        if (q.quest_id == work.quest) purpose = q.stated_purpose.value_or("");
    check::is_true(!purpose.empty() && did->about == purpose && open->about == purpose, "the work by why its giver asked it");
    const std::int64_t seq = did->seq;
    const std::size_t count = all.size();
    d.advance(1.0);
    d.resolve(work.quest, true);
    const double finished = today(d);
    std::string event;
    for (const orchestrator::Event& e : d.events())
        if (e.kind == "work" && e.quest_id == work.quest) event = e.id;
    const std::vector<std::vector<std::string>> ended =
        rows(d, "SELECT day FROM event_states WHERE event = " + quoted(event) + " AND state = 'finished'");
    check::is_true(ended.size() == 1, "the premise: the work's event finished");
    const Compacted done = d.compact(work.giver, "");
    check::equal(done.cleared, std::size_t{0}, "nothing new said");
    all = d.memories(work.giver);
    did = find_memory(all, "did", work.quest);
    check::is_true(did && did->act == "finished" && did->first_day == taken && std::to_string(did->last_day) == ended[0][0] &&
                       did->seq == seq,
                   "remembered finished now, from when it was taken, to when it was finished, where it was kept");
    check::is_true(find_memory(all, "did", work.quest, "taken") == nullptr, "and no longer only taken");
    check::is_true(find_memory(all, "open", work.quest) == nullptr, "nor held");
    check::equal(all.size(), count - 1, "one memory changed and one gone");
    check::equal(done.kept.size(), std::size_t{1}, "compact says what it brought up to date, and only that");
    check::is_true(done.kept[0].memory == "did" && done.kept[0].act == "finished", "the work finished");
    // Asked about it again, a memory counts once more and keeps when it first was.
    (void)d.respond(work.giver, {ActKind::AskForWork, ""});
    (void)d.compact(work.giver, "");
    const Remembered* asked = find_memory(d.memories(work.giver), "asked", "", "ask_for_work");
    check::is_true(asked && asked->times == 2 && asked->first_day == taken && asked->last_day == finished, "asked for work twice");
}

TEST(outcomes_handed_in_are_kept_only_where_the_log_bears_them_out) {
    const std::string speaker = townsperson(2);
    const auto session = [&](Director& d) {
        (void)d.respond(speaker, {ActKind::Greet, ""});
        d.words("Hello there.", "Well met.");
        (void)d.respond(speaker, {ActKind::Answer, ""});
        d.words("My sister keeps the mill at the ford, near Wenweg.", "Does she now.");
        (void)d.respond(speaker, {ActKind::SmallTalk, ""});
        d.words("Fine weather for it.", "Fine enough.");
    };
    {
        Director d(memory_world());
        session(d);
        const Compacted done = d.compact(
            speaker,
            R"({"said": [{"quote": "My sister keeps the mill", "about": "the stranger's sister"},
                         {"quote": "my sister keeps the mill"},
                         {"quote": "ister keeps"},
                         {"quote": "I slew the dragon of Harrowmere"},
                         {"quote": "near Wenweg", "about": "where the mill stands, by Dunmoor"},
                         {"quote": "Hello there"}, {"quote": "at the ford"}, {"quote": "keeps the mill"}, {"quote": "Fine weather"},
                         {"quote": "Hello"}],
                "last": "  They spoke of a sister at the mill near Wenweg.  "})");
        const std::vector<Remembered> all = d.memories(speaker);
        std::vector<std::string> kept;
        for (const Remembered& r : all)
            if (r.memory == "said") {
                check::is_true(r.source == "given" && r.act == "said" && r.subject.empty(), "a quote, as given");
                kept.push_back(r.words);
            }
        check::is_true(kept == std::vector<std::string>{"My sister keeps the mill", "Hello there", "at the ford", "keeps the mill", "Fine weather"},
                       "the five quotes the log bears out, in the order given");
        check::equal(find_memory(all, "said", "")->about, std::string("the stranger's sister"), "with what it is about");
        check::equal(done.refused.size(), std::size_t{5}, "five refused");
        check::is_true(has_refusal(done, "\"my sister keeps the mill\" is not the stranger's words"), "a quote not word for word");
        check::is_true(has_refusal(done, "\"ister keeps\" is not the stranger's words"), "a quote cutting a word");
        check::is_true(has_refusal(done, "\"I slew the dragon of Harrowmere\" is not the stranger's words"), "a quote never said");
        check::is_true(has_refusal(done, "names Dunmoor, which the log does not"), "a name the log does not hold");
        check::is_true(has_refusal(done, "\"Hello\" is past the 5 quotes kept"), "a sixth quote");
        const Remembered* last = nullptr;
        for (const Remembered& r : all)
            if (r.memory == "last") last = &r;
        check::is_true(last && last->words == "They spoke of a sister at the mill near Wenweg." && last->source == "given" &&
                           last->act == "small_talk",
                       "what they last spoke of, in the words given, beside the rules' own");
    }
    {
        Director d(memory_world());
        session(d);
        const Compacted done = d.compact(speaker, R"({"last": "They spoke of Dunmoor.", "said": []})");
        const std::vector<Remembered> all = d.memories(speaker);
        check::is_true(has_refusal(done, "a last that names Dunmoor, which the log does not"), "a last naming what the log does not");
        const Remembered* last = find_memory(all, "last", "", "small_talk");
        check::is_true(last && last->words.empty() && last->source == "rules", "the rules' last kept, without the words");
        check::equal(count_of(all, "said"), std::size_t{0}, "outcomes that keep no quote keep none");
    }
    {
        Director d(memory_world());
        session(d);
        const Compacted done = d.compact(speaker, "");
        const std::vector<Remembered> all = d.memories(speaker);
        const Remembered* said = find_memory(all, "said", "");
        check::is_true(done.refused.empty() && said && said->words == "My sister keeps the mill at the ford, near Wenweg." &&
                           said->source == "rules" && count_of(all, "said") == 1,
                       "with no outcomes, the rules keep the stranger's answer word for word");
    }
    {
        Director d(memory_world());
        (void)d.respond(speaker, {ActKind::Greet, ""});
        (void)d.respond(speaker, {ActKind::Farewell, ""});
        const Compacted done = d.compact(speaker, R"({"last": "They said hello."})");
        check::is_true(has_refusal(done, "where the log holds nothing they spoke of"), "a last with nothing spoken of to keep it by");
    }
}

TEST(compact_refuses_outcomes_it_cannot_read_and_changes_nothing) {
    const std::string speaker = townsperson(0);
    Director d(memory_world());
    (void)d.respond(speaker, {ActKind::Greet, ""});
    d.words("Hello.", "Hello.");
    const std::size_t inputs = d.inputs().size();
    for (const std::string& outcomes :
         {std::string("not json"), std::string("[]"), std::string(R"({"said": {}})"), std::string(R"({"said": [3]})"),
          std::string(R"({"said": [{"quote": 3}]})"), std::string(R"({"said": [{"said": "x"}]})"), std::string(R"({"last": 3})"),
          std::string(R"({"other": 1})"), std::string(16001, ' ')})
        check::throws([&] { (void)d.compact(speaker, outcomes); }, "refused: " + outcomes.substr(0, 40));
    check::throws([&] { (void)d.compact("npc-nobody-at-all", ""); }, "compacting nobody");
    check::equal(d.inputs().size(), inputs, "nothing recorded");
    check::equal(scalar(d, "SELECT COUNT(*) FROM exchanges WHERE person = " + quoted(speaker)), std::int64_t{1}, "the log as it was");
    check::equal(scalar(d, "SELECT COUNT(*) FROM memories"), std::int64_t{0}, "no memory kept");
    check::is_true(d.acquaintance(speaker)->talking, "and the conversation still under way");
}

TEST(recall_facts_bring_back_what_bears_on_the_act) {
    const std::string speaker = townsperson(3);
    Director d(memory_world());
    const Fact place = a_place_known_to(d, speaker);
    const std::string label = orchestrator::knowledge::place_label(place);
    check::is_true(recalled(d.respond(speaker, {ActKind::Greet, ""})).empty(), "met for the first time, nothing to recall");
    check::is_true(recalled(d.respond(speaker, {ActKind::AskAboutPlace, place.subject})).empty(), "nor asked of anything before");
    check::is_true(recalled(d.respond(speaker, {ActKind::Greet, ""})).empty(),
                   "greeted again in the same conversation: what they last spoke of is from an earlier one");
    (void)d.respond(speaker, {ActKind::Farewell, ""});
    d.advance(1.0);
    // Before any compaction, from the log.
    std::map<std::string, Fact> r = recalled(d.respond(speaker, {ActKind::Greet, ""}));
    check::is_true(r.size() == 1 && r.count("last"), "a greeting recalls what they last spoke of, and nothing else yet");
    check::is_true(r.at("last").subject == place.subject && value(r.at("last"), "act") == "ask_about_place" &&
                       value(r.at("last"), "about") == label && value(r.at("last"), "since") == "yesterday",
                   "the place asked after, yesterday");
    r = recalled(d.respond(speaker, {ActKind::AskAboutPlace, place.subject}));
    check::is_true(r.count("asked") && r.count("told"), "asked of it again: that it was asked, and told of");
    check::is_true(value(r.at("asked"), "times") == "1" && value(r.at("asked"), "since") == "yesterday" &&
                       value(r.at("asked"), "first") == "yesterday" && r.at("asked").subject == place.subject,
                   "asked once before, yesterday");
    check::equal(value(r.at("told"), "about"), label, "told of it by name");
    // After a compaction, from long memory, the same.
    (void)d.compact(speaker, "");
    d.advance(1.0);
    r = recalled(d.respond(speaker, {ActKind::AskAboutPlace, place.subject}));
    check::is_true(r.count("asked") && value(r.at("asked"), "times") == "2" && value(r.at("asked"), "first") == "days",
                   "asked twice before, first some days ago: " + (r.count("asked") ? value(r.at("asked"), "times") : std::string()));
    // Something else asked of: nothing of the place.
    const std::string other = townsperson(4);
    check::is_true(recalled(d.respond(speaker, {ActKind::AskAboutPerson, other})).count("asked") == 0,
                   "asked of someone never asked of before, nothing recalled of it");
    // Work: what came of the speaker's own, and what the stranger holds.
    const Work work = open_work();
    take(d, work);
    (void)d.respond(work.giver, {ActKind::Farewell, ""});
    r = recalled(d.respond(work.giver, {ActKind::Greet, ""}));
    check::is_true(r.count("open") && r.at("open").subject == work.quest && value(r.at("open"), "act") == "taken",
                   "greeted, the giver recalls the work the stranger holds");
    d.resolve(work.quest, true);
    r = recalled(d.respond(work.giver, {ActKind::AskForWork, ""}));
    check::is_true(r.count("did") && r.at("did").subject == work.quest && value(r.at("did"), "act") == "finished" && !r.count("open"),
                   "asked for work, what came of theirs: finished, and nothing held");
    // Someone never spoken to recalls nothing.
    check::is_true(recalled(d.respond(townsperson(5), {ActKind::Greet, ""})).empty(), "a stranger recalls nothing");
}

TEST(standing_grows_with_the_days_met_and_never_falls) {
    const std::string speaker = townsperson(6);
    Director d(memory_world());
    check::equal(standing(d.respond(speaker, {ActKind::Greet, ""})), std::string("acquainted"), "met once: acquainted");
    for (int i = 0; i < 3; ++i) (void)d.respond(speaker, {ActKind::Insult, ""});
    check::equal(standing(d.respond(speaker, {ActKind::SmallTalk, ""})), std::string("acquainted"), "insults take nothing from it");
    (void)d.respond(speaker, {ActKind::Farewell, ""});
    check::equal(standing(d.respond(speaker, {ActKind::Greet, ""})), std::string("acquainted"), "a second conversation the same day");
    check::equal(d.acquaintance(speaker)->days_met, 1, "is one day met");
    (void)d.respond(speaker, {ActKind::Farewell, ""});
    d.advance(1.0);
    check::equal(standing(d.respond(speaker, {ActKind::Greet, ""})), std::string("acquainted"), "on two days: still acquainted");
    (void)d.respond(speaker, {ActKind::Farewell, ""});
    d.advance(1.0);
    check::equal(standing(d.respond(speaker, {ActKind::Greet, ""})), std::string("regular"), "on three days: a regular");
    check::equal(d.acquaintance(speaker)->days_met, 3, "three days met");
    const QueryResult row = d.query("SELECT standing, days_met, exchanges FROM relationships WHERE person = " + quoted(speaker));
    check::is_true(row.rows.size() == 1 && cell_text(row.rows[0][0]) == "regular" && cell_text(row.rows[0][1]) == "3",
                   "the relationships table says so");
    // Ten days and thirty exchanges: a favourite, and not before both.
    for (int day = 0; day < 7; ++day) {
        (void)d.respond(speaker, {ActKind::Farewell, ""});
        d.advance(1.0);
        check::equal(standing(d.respond(speaker, {ActKind::Greet, ""})), std::string("regular"), "a regular, on ten days");
    }
    int exchanges = 0;
    for (const auto& [act, times] : d.acquaintance(speaker)->heard) exchanges += times;
    check::is_true(d.acquaintance(speaker)->days_met == 10 && exchanges < 29, "the premise: ten days, fewer than thirty exchanges");
    while (exchanges < 29) {
        check::equal(standing(d.respond(speaker, {ActKind::SmallTalk, ""})), std::string("regular"), "a regular, short of thirty");
        ++exchanges;
    }
    check::equal(standing(d.respond(speaker, {ActKind::SmallTalk, ""})), std::string("favourite"), "then thirty: a favourite");
    // A hero is a hero's standing from the first word.
    const fwg::World& w = memory_world();
    std::string hero;
    for (const fwg::Hero& h : w.heroes.people)
        if (h.status == "living" && h.faces.size() == 1 && hero.empty()) hero = h.uid;
    check::is_true(!hero.empty(), "the premise: a living hero with one face");
    check::equal(standing(d.respond(hero, {ActKind::Greet, ""})), std::string("hero"), "a hero: hero");
}

TEST(work_taken_and_given_up_is_one_memory_of_it) {
    const Work work = open_work();
    Director d(memory_world());
    take(d, work);
    const double taken = today(d);
    check::is_true(d.respond(work.giver, {ActKind::AbandonQuest, work.quest}).outcome == Outcome::Abandoned, "the premise: given up");
    (void)d.compact(work.giver, "");
    std::vector<const Remembered*> of_it;
    const std::vector<Remembered> all = d.memories(work.giver);
    for (const Remembered& r : all)
        if (r.memory == "did" && r.subject == work.quest) of_it.push_back(&r);
    check::is_true(of_it.size() == 1 && of_it[0]->act == "given_up" && of_it[0]->first_day == taken,
                   "one memory of the work: given up, from when it was taken");
    check::is_true(find_memory(all, "open", work.quest) == nullptr, "and nothing held");
}

TEST(a_bystander_never_hears_an_older_state_than_they_know) {
    const Work work = open_work();
    Director d(memory_world());
    take(d, work);
    (void)d.share({work.neighbour});
    std::string event;
    for (const orchestrator::Event& e : d.events())
        if (e.kind == "work" && e.quest_id == work.quest) event = e.id;
    const auto known = [&](const std::string& person) {
        const std::vector<std::vector<std::string>> r =
            rows(d, "SELECT state || '/' || how FROM facts WHERE person = " + quoted(person) + " AND event = " + quoted(event));
        return r.empty() ? std::string() : r[0][0];
    };
    check::equal(known(work.neighbour), std::string("taken/word"), "the premise: one bystander heard the work taken");
    d.resolve(work.quest, true);
    (void)d.share({work.second});
    check::equal(known(work.second), std::string("finished/word"), "the premise: the other heard it finished");
    const std::vector<Fact> news = facts_of(d.respond(work.neighbour, {ActKind::Greet, ""}), FactKind::News);
    check::is_true(news.size() == 1 && news[0].subject == event && value(news[0], "state") == "taken",
                   "the premise: the first speaks of it as taken, all they know");
    (void)d.overhear({work.second});
    check::equal(known(work.second), std::string("finished/word"), "overhearing it called taken, the other still knows it finished");
}

// Contract version 21: a question the speaker asked, which the stranger's next line did not answer, is kept
// and asked again once, at a greeting in a later conversation; one answered is settled. The judges of the
// golden benchmark asked for it three times: a hero's own question left hanging was never raised again.
TEST(a_question_never_answered_is_asked_again_once) {
    const std::string speaker = townsperson(0);
    const std::string other = townsperson(1);
    const std::string asked = "What brings you here?";
    const auto question_in = [](const FactPacket& p) {
        const std::map<std::string, Fact> recall = recalled(p);
        const auto found = recall.find("question");
        return found == recall.end() ? std::string() : value(found->second, "words");
    };
    Director d(memory_world());
    // Refused, and nothing recorded: a question the reply does not hold, one too long, one not UTF-8.
    (void)d.respond(speaker, {ActKind::Greet, ""});
    const std::size_t inputs = d.inputs().size();
    check::throws([&] { d.words("Hello.", "Hello, stranger.", asked); }, "a question the reply does not hold");
    check::throws([&] { d.words("Hello.", "Hello. " + std::string(241, 'a') + "?", std::string(241, 'a') + "?"); }, "a question too long");
    check::throws([&] { d.words("Hello.", "Hello. \xC3\x28?", "\xC3\x28?"); }, "a question not UTF-8");
    check::equal(d.inputs().size(), inputs, "nothing recorded");
    d.words("Hello.", "Hello, stranger. " + asked, asked);
    check::equal(rows(d, "SELECT asked FROM exchanges WHERE person = " + quoted(speaker) + " ORDER BY id DESC LIMIT 1").at(0).at(0), asked,
                 "the log keeps the question with its exchange");
    // Not answered: small talk next. Greeted again in the same conversation, it is not asked yet.
    (void)d.respond(speaker, {ActKind::SmallTalk, ""});
    d.words("Fine weather.", "It is that.");
    check::equal(question_in(d.respond(speaker, {ActKind::Greet, ""})), std::string(), "in the same conversation: not asked again yet");
    (void)d.respond(speaker, {ActKind::Farewell, ""});
    // Kept at compaction.
    (void)d.compact(speaker, "");
    const std::vector<Remembered> kept = d.memories(speaker);
    const Remembered* question = find_memory(kept, "question", "");
    check::is_true(question && question->words == asked && question->times == 1, "kept: the question, as asked");
    // A greeting in a later conversation carries it; then no greeting carries it again, compacted or not.
    d.advance(1.0);
    const FactPacket again = d.respond(speaker, {ActKind::Greet, ""});
    check::equal(question_in(again), asked, "a later greeting: asked again");
    d.words("Hello again.", "Welcome back. You never did say. " + asked, "");
    (void)d.respond(speaker, {ActKind::SmallTalk, ""});
    (void)d.respond(speaker, {ActKind::Farewell, ""});
    check::equal(question_in(d.respond(speaker, {ActKind::Greet, ""})), std::string(), "the next greeting: not a third time");
    (void)d.respond(speaker, {ActKind::Farewell, ""});
    (void)d.compact(speaker, "");
    const std::vector<Remembered> twice = d.memories(speaker);
    const Remembered* still = find_memory(twice, "question", "");
    check::is_true(still && still->times == 2, "kept as asked twice");
    d.advance(1.0);
    check::equal(question_in(d.respond(speaker, {ActKind::Greet, ""})), std::string(), "after the compaction: not a third time either");
    // Answered by the stranger's next line: settled, and nothing kept.
    (void)d.respond(other, {ActKind::Greet, ""});
    d.words("Good day.", "Good day. Who sent you to me?", "Who sent you to me?");
    (void)d.respond(other, {ActKind::Answer, ""});
    d.words("Nobody; I came of my own accord.", "Fair enough.");
    (void)d.respond(other, {ActKind::Farewell, ""});
    (void)d.compact(other, "");
    check::equal(count_of(d.memories(other), "question"), std::size_t{0}, "answered: nothing to ask again");
    // Replayed, the same.
    Director replayed = orchestrator::replay(memory_world(), d.inputs());
    same_tables(d, replayed, {"exchanges", "exchange_facts", "memories"});
}

// Contract version 23: an apology carries the stranger's latest words of this conversation, their last
// answer, so "Sorry, I'm not myself today" two lines after a brother's death is heard in its light. Not
// words of an earlier conversation, and none without an answer.
TEST(an_apology_carries_the_strangers_latest_words_of_this_conversation) {
    const std::string speaker = townsperson(0);
    const std::string brother = "My brother came with me. He didn't make it back.";
    const auto said_in = [](const FactPacket& p) {
        const std::map<std::string, Fact> recall = recalled(p);
        const auto found = recall.find("said");
        return found == recall.end() ? std::string() : value(found->second, "words");
    };
    Director d(memory_world());
    (void)d.respond(speaker, {ActKind::Greet, ""});
    d.words("Hello.", "Hello.");
    check::equal(said_in(d.respond(speaker, {ActKind::Apologize, ""})), std::string(), "no answer yet: none");
    d.words("Sorry, I'm not myself today.", "No need.");
    (void)d.respond(speaker, {ActKind::Answer, ""});
    d.words("I was a smith once.", "Were you now.");
    (void)d.respond(speaker, {ActKind::Answer, ""});
    d.words(brother, "I'm sorry.");
    (void)d.respond(speaker, {ActKind::SmallTalk, ""});
    d.words("Cold, isn't it?", "It is.");
    const FactPacket sorry = d.respond(speaker, {ActKind::Apologize, ""});
    check::equal(said_in(sorry), brother, "their latest answer, word for word");
    for (const Fact& f : sorry.facts)
        if (f.kind == FactKind::Recall) check::equal(value(f, "since"), std::string("today"), "said today");
    (void)d.respond(speaker, {ActKind::Farewell, ""});
    d.advance(1.0);
    (void)d.respond(speaker, {ActKind::Greet, ""});
    check::equal(said_in(d.respond(speaker, {ActKind::Apologize, ""})), std::string(), "a later conversation: not that one's words");
}

// Contract version 24: the player's name is only ever one they said. Told, the speaker knows them by it
// (perhaps a lie); told the same, known; told another, they keep both. Word of work the player did for a
// giver who knew a name carries it to whoever hears; an introduction overheard is learned too.
TEST(the_players_name_is_what_they_said_and_travels_with_word) {
    const Work work = open_work();
    const auto memory_of = [](const FactPacket& p) {
        const std::vector<Fact> memory = facts_of(p, FactKind::Memory);
        if (memory.size() != 1) check::fail("one Memory fact");
        return memory.front();
    };
    Director d(memory_world());
    // No name, one too long, one on two lines: refused, and nothing recorded.
    (void)d.respond(work.giver, {ActKind::Greet, ""});
    const std::size_t inputs = d.inputs().size();
    const int turn = d.acquaintance(work.giver)->turn;
    check::throws([&] { (void)d.respond(work.giver, {ActKind::Introduce, "   "}); }, "no name");
    check::throws([&] { (void)d.respond(work.giver, {ActKind::Introduce, std::string(61, 'a')}); }, "a name too long");
    check::throws([&] { (void)d.respond(work.giver, {ActKind::Introduce, "Al\nric"}); }, "a name on two lines");
    check::equal(d.inputs().size(), inputs, "nothing recorded");
    check::equal(d.acquaintance(work.giver)->turn, turn, "and nothing changed");
    check::equal(value(memory_of(d.respond(work.giver, {ActKind::SmallTalk, ""})), "called"), std::string(), "no name yet");
    // Told.
    const FactPacket told = d.respond(work.giver, {ActKind::Introduce, " Aldric "});
    check::is_true(told.outcome == Outcome::Introduced, "a name they did not know: introduced");
    check::equal(value(memory_of(told), "called"), std::string("Aldric"), "known by it, trimmed");
    check::equal(value(memory_of(told), "called_how"), std::string("told"), "told");
    check::equal(value(memory_of(told), "called_other"), std::string(), "no other");
    check::is_true(d.respond(work.giver, {ActKind::Introduce, "Aldric"}).outcome == Outcome::Known, "the same again: known");
    const FactPacket other = d.respond(work.giver, {ActKind::Introduce, "Brenna"});
    check::is_true(other.outcome == Outcome::Renamed, "another: renamed");
    check::equal(value(memory_of(other), "called"), std::string("Brenna"), "known by the new one");
    check::equal(value(memory_of(other), "called_other"), std::string("Aldric"), "and the old kept");
    check::equal(value(memory_of(d.respond(work.giver, {ActKind::Greet, ""})), "called"), std::string("Brenna"), "a greeting says it");
    const std::vector<std::string> row =
        rows(d, "SELECT called, called_how, called_other FROM relationships WHERE person = " + quoted(work.giver)).at(0);
    check::equal(row.at(0) + "|" + row.at(1) + "|" + row.at(2), std::string("Brenna|told|Aldric"), "the database says it");
    // Word: the giver knows Brenna; work done for them carries the name to whoever hears of it.
    take(d, work);
    d.resolve(work.quest, true);
    check::is_true(d.acquaintance(work.second) == nullptr || d.acquaintance(work.second)->called.empty(), "the premise: unknown to the second");
    (void)d.share({work.second});
    const Director::Acquaintance* heard = d.acquaintance(work.second);
    check::is_true(heard != nullptr && heard->called == "Brenna" && heard->called_how == "word", "the name, by word of the deed");
    const FactPacket greeted = d.respond(work.second, {ActKind::Greet, ""});
    bool carried = false;
    for (const Fact& f : facts_of(greeted, FactKind::News))
        if (f.field("called") && *f.field("called") == "Brenna") carried = true;
    check::is_true(carried, "news of the deed says the name the giver knew");
    check::equal(value(memory_of(greeted), "called_how"), std::string("word"), "known by word");
    // Overheard: the neighbour is told another name, and the second, standing by, hears it.
    (void)d.respond(work.neighbour, {ActKind::Introduce, "Cyril"});
    (void)d.overhear({work.second});
    check::equal(d.acquaintance(work.second)->called, std::string("Brenna"), "the name they had stays theirs");
    check::equal(d.acquaintance(work.second)->called_other, std::string("Cyril"), "and the one overheard is the other");
    // Replayed, the same.
    Director again = orchestrator::replay(memory_world(), d.inputs());
    for (const std::string& who : {work.giver, work.second, work.neighbour}) {
        const Director::Acquaintance* live = d.acquaintance(who);
        const Director::Acquaintance* replayed = again.acquaintance(who);
        check::is_true(live && replayed && live->called == replayed->called && live->called_how == replayed->called_how &&
                           live->called_other == replayed->called_other,
                       "replay keeps what " + who + " knows the player by");
    }
    same_tables(d, again, {"relationships", "events"});
}

TEST(replay_rebuilds_the_log_and_long_memory_row_for_row) {
    const Work work = open_work();
    const std::string outcomes = R"({"said": [{"quote": "I will see it done", "about": "the work"}], "last": "They took the work on."})";
    Director d(memory_world());
    (void)d.respond(work.giver, {ActKind::Greet, ""});
    d.words("Good day.", "Good day to you.");
    (void)d.overhear({work.neighbour});
    take(d, work);
    d.words("I will see it done.", "Good.");
    const Compacted first = d.compact(work.giver, outcomes);
    check::is_true(first.refused.empty() && find_memory(first.kept, "said", ""), "the premise: the quote given kept");
    d.resolve(work.quest, true);
    (void)d.compact(work.giver, "");
    (void)d.respond(work.neighbour, {ActKind::Greet, ""});
    d.words("Hello.", "");
    Director again = orchestrator::replay(memory_world(), d.inputs());
    same_tables(d, again, {"exchanges", "exchange_facts", "memories", "relationships", "facts"});
    const std::vector<Remembered> live = d.memories(work.giver);
    const std::vector<Remembered> replayed = again.memories(work.giver);
    check::equal(replayed.size(), live.size(), "the giver's memories");
    for (std::size_t i = 0; i < live.size() && i < replayed.size(); ++i)
        check::is_true(live[i].memory == replayed[i].memory && live[i].subject == replayed[i].subject && live[i].act == replayed[i].act &&
                           live[i].words == replayed[i].words && live[i].seq == replayed[i].seq && live[i].source == replayed[i].source,
                       "memory " + std::to_string(i) + " the same");
    // And both go on alike.
    const Compacted a = d.compact(work.neighbour, "");
    const Compacted b = again.compact(work.neighbour, "");
    check::is_true(a.kept.size() == b.kept.size() && a.cleared == b.cleared, "a compaction after, alike");
}
