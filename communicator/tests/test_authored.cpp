// Written lines, spoken for fact packets. No model: a reply is a line a person wrote, its blanks
// filled from the packet. Every row in data/lines/ is tried alone against every packet in
// authored_cases.hpp and held to the voice's rules, so a row that breaks one fails by name.
#include "communicator/authored.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "authored_cases.hpp"
#include "check.hpp"
#include "communicator/error.hpp"
#include "packets.hpp"
#include "reply_shape.hpp"
#include "rules.hpp"

namespace {

using namespace authored_cases;
using communicator::Lines;
using orchestrator::ActKind;
using orchestrator::Fact;
using orchestrator::FactKind;
using orchestrator::FactPacket;
using orchestrator::Outcome;

// The first of `replies`, failing by name when there is none.
std::string first(const std::vector<std::string>& replies) {
    check::is_true(!replies.empty(), "a reply");
    return replies.front();
}

}  // namespace

TEST(the_lines_file_loads_every_row) {
    const Lines lines = Lines::load(COMMUNICATOR_LINES);
    check::equal(lines.size(), rows_of(COMMUNICATOR_LINES).size(), "rows read");
    check::is_true(lines.size() > 50, "the premise: a pilot's worth of lines");
}

TEST(every_line_is_spoken_and_keeps_the_voices_rules) {
    // Each row alone, against every packet, as its bank's speaker: a row no packet can speak, or
    // a reply that breaks a rule, fails by the row's words.
    const std::vector<Row> rows = rows_of(COMMUNICATOR_LINES);
    check::is_true(!rows.empty(), "the premise: rows to read");
    for (const Row& row : rows) {
        const std::vector<std::string> problems = row_problems(row.text);
        check::is_true(problems.empty(), row.where + " to be sound: " + row.text + "; " + (problems.empty() ? "" : problems.front()));
    }
}

TEST(every_packet_gets_a_reply_from_the_file) {
    const Lines lines = Lines::load(COMMUNICATOR_LINES);
    for (const FactPacket& p : cases())
        check::is_true(!lines.replies(p).empty(), "a reply for " + p.persona.name + ", " + Lines::situation(p));
}

TEST(the_rule_checks_catch_what_they_check) {
    // The control for the test above: each check fails a reply that breaks its rule.
    const FactPacket parted = packets::parted(), refused = packets::refused_brokered();
    const std::string closing = " " + communicator::reply_shape(refused).closing;
    const std::vector<std::pair<std::string, FactPacket>> broken = {
        {"Farewell, {speaker}.", parted},
        {"Farewell, \"friend\".", parted},
        {"Farewell, hero-castellan-fortress-node-178.", parted},
        {"Well met.", parted},
        {"Farewell. Come back soon.", parted},
        {"Farewell, and greet Castle Grey for me.", parted},
        {"Farewell, and mind the Watch.", parted},
        {"Farewell, and head north.", parted},
        {"Farewell, and mind your gold.", parted},
        {"No, that's Halbrann's work.", refused},
    };
    for (const auto& [reply, p] : broken) check::is_true(!broken_rules(reply, p).empty(), "a broken rule found in: " + reply);
    check::is_true(broken_rules("Farewell, and mind the road.", parted).empty(), "a line that keeps them passes");
    check::is_true(broken_rules("No, that's Halbrann's work." + closing, refused).empty(), "with its closing, it passes");
    // A name the writer typed passes the reply check when a test packet holds it, so the row's
    // own text is held too: the town is a blank, never a word the line writes.
    check::is_true(broken_rules("Farewell, and give Wenweg my regards.", parted).empty(), "the premise: the packet holds Wenweg");
    check::is_true(!row_problems("any | parted | Farewell, and give Wenweg my regards.").empty(), "a town the line writes itself");
    check::is_true(row_problems("any | parted | Farewell, and give {place} my regards.").empty(), "the town as a blank passes");
    check::is_true(row_problems("any | declined | Suit yourself, then.").empty(), "an outcome's opening is the program's own");
}

TEST(a_line_speaks_only_when_its_packet_fills_every_blank) {
    const Lines lines = Lines::parse("any | surroundings | The nearest is {nearest}. Mind the {danger}.\n"
                                     "any | surroundings | The nearest is {nearest}.\n",
                                     "test");
    // No nest near the inn: only the line without a danger is said.
    const std::vector<std::string> at_the_inn = lines.replies(packets::surroundings_from_the_inn());
    check::equal(at_the_inn.size(), std::size_t(1), "one line fits at the inn");
    check::equal(at_the_inn.front(), std::string("The nearest is the silent keep, an hour's walk away, dangerous, held by squatters."), "the reply");
    // Both fit at the gate, and the one that says more is said: a speaker who knows of a nest does
    // not leave it out.
    const std::vector<std::string> at_the_gate = lines.replies(packets::surroundings_from_the_gate());
    check::equal(at_the_gate.size(), std::size_t(1), "the fuller line at the gate");
    check::equal(at_the_gate.front(),
                 std::string("The nearest is the hamlet of Wenweg, an hour's walk away. Mind the Glacier dragons nesting an hour's walk "
                             "away."),
                 "the reply");
    // Where the speaker stands is not the nearest place, and the worst nest is named.
    const Lines around = Lines::parse("any | surroundings | {nearest}. Past that, {others}. Mind the {danger}.", "test");
    check::equal(first(around.replies(packets::surroundings())),
                 std::string("The ninth stone on the Linbrig road, a short walk away, dangerous, held by monsters. Past "
                             "that, the deeps under Linbrig, deadly, held by monsters, and the hamlet of Wenweg, both an hour's "
                             "walk away. Mind the Hippogryphs nesting an hour's walk away."),
                 "the castellan's surroundings");
}

TEST(an_optional_part_is_left_out_when_its_blank_is_not_filled) {
    const Lines lines = Lines::parse("any | surroundings | The nearest is {nearest}.[ Then there's {others}.][ Mind the {danger}.]", "test");
    check::equal(first(lines.replies(packets::surroundings_from_the_inn())),
                 std::string("The nearest is the silent keep, an hour's walk away, dangerous, held by squatters. Then there's "
                             "Dunfor and Strandbrig, both a long walk away."),
                 "no nest near the inn, so no warning");
    check::equal(first(lines.replies(packets::surroundings_from_the_gate())),
                 std::string("The nearest is the hamlet of Wenweg, an hour's walk away. Then there's the hospice on the Wenweg "
                             "pass, dangerous, held by monsters, and another hamlet of Wenweg, both an hour's walk away. Mind the "
                             "Glacier dragons nesting an hour's walk away."),
                 "all of it at the gate");
    // A part left out still leaves a line that says more than one without it.
    const Lines two = Lines::parse("any | surroundings | The nearest is {nearest}.\n"
                                   "any | surroundings | The nearest is {nearest}.[ Mind the {danger}.]\n",
                                   "test");
    check::equal(two.replies(packets::surroundings_from_the_inn()).size(), std::size_t(2), "at the inn, both say the same");
    check::equal(two.replies(packets::surroundings_from_the_gate()).size(), std::size_t(1), "at the gate, the one with the nest");
    // The speaker's own name, post and place say nothing of the packet: they make no line fuller.
    const Lines named = Lines::parse("any | surroundings | The nearest is {nearest}.\n"
                                     "any | surroundings | I'm {speaker}, and the nearest is {nearest}.\n",
                                     "test");
    check::equal(named.replies(packets::surroundings_from_the_inn()).size(), std::size_t(2), "both, with a name or without");
    // Nor does a blank said twice: the line says no more of the packet than one that says it once.
    const Lines twice = Lines::parse("any | refused_give | No, that's {giver}'s work, and {giver} knows it.\n"
                                     "any | refused_give | No, that's {giver}'s work.\n",
                                     "test");
    check::equal(twice.replies(packets::refused_brokered()).size(), std::size_t(2), "both, with the giver once or twice");
}

TEST(where_the_speaker_stands_is_named_and_a_place_named_twice_is_another) {
    const Lines lines = Lines::parse("any | surroundings | This is {here}, and the nearest is {nearest}.[ Then there's {others}.]", "test");
    check::equal(first(lines.replies(packets::surroundings())),
                 std::string("This is Wenweg, and the nearest is the ninth stone on the Linbrig road, a short walk away, "
                             "dangerous, held by monsters. Then there's the deeps under Linbrig, deadly, held by monsters, and "
                             "the hamlet of Wenweg, both an hour's walk away."),
                 "the castellan's town");
    check::equal(first(lines.replies(packets::surroundings_from_the_gate())),
                 std::string("This is the fortress of Wenweg, and the nearest is the hamlet of Wenweg, an hour's walk away. Then "
                             "there's the hospice on the Wenweg pass, dangerous, held by monsters, and another hamlet of Wenweg, "
                             "both an hour's walk away."),
                 "the gate's fortress, and the second hamlet as another");
}

TEST(asked_what_is_around_a_speaker_names_the_worst_of_it) {
    // Is it safe? A dangerous place says so, and the most dangerous place the speaker knows is
    // named even past the nearest three: the deeps under Linbrig are the castellan's fifth.
    const FactPacket around = packets::surroundings();
    std::vector<std::string> away;
    for (const Fact& f : around.facts)
        if (f.kind == FactKind::Place && *f.field("range") != "here") away.push_back(f.subject);
    check::is_true(std::find(away.begin(), away.end(), "keyloc-silver_mine-211") - away.begin() >= 3,
                   "the premise: the deeps are past the nearest three");
    const Lines lines = Lines::parse("any | surroundings | The nearest is {nearest}.[ Then there's {others}.]", "test");
    const std::string said = first(lines.replies(around));
    check::is_true(said.find("the deeps under Linbrig, deadly") != std::string::npos, "the deadliest named: " + said);
    check::is_true(said.find("the ninth stone on the Linbrig road, a short walk away, dangerous") != std::string::npos,
                   "a dangerous place says so: " + said);
    // With nothing dangerous about, the nearest three are named and nothing is called dangerous.
    FactPacket quiet = around;
    for (Fact& f : quiet.facts)
        for (auto& [key, value] : f.fields)
            if (key == "threat") value = "2";
    const std::string calm = first(lines.replies(quiet));
    check::is_true(calm.find("the burnt works") != std::string::npos, "the third nearest, then: " + calm);
    check::is_true(calm.find("deadly") == std::string::npos && calm.find("dangerous") == std::string::npos, "no danger claimed: " + calm);
}

TEST(a_marker_changes_no_written_reply) {
    // The work's exact map marker is the game's: the written lines say the same with it or without.
    const Lines lines = Lines::load(COMMUNICATOR_LINES);
    for (FactPacket p : {packets::work(), packets::accepted()}) {
        const std::vector<std::string> with = lines.replies(p);
        std::size_t markers = 0;
        for (Fact& f : p.facts) {
            const auto end = std::remove_if(f.fields.begin(), f.fields.end(),
                                            [](const auto& field) { return field.first == "marker" || field.first == "marker_node"; });
            markers += static_cast<std::size_t>(f.fields.end() - end);
            f.fields.erase(end, f.fields.end());
        }
        check::is_true(markers > 0, "the premise: markers to take away");
        check::is_true(lines.replies(p) == with, "the same replies");
    }
}

TEST(a_refusal_naming_two_people_names_neither_as_the_giver) {
    // Nothing says which of two is meant, so a line that needs the giver is not said.
    const Lines lines = Lines::parse("any | refused_give | No, that's {giver}'s work.\n"
                                     "any | refused_give | No, that work isn't mine to give.\n",
                                     "test");
    check::equal(first(lines.replies(packets::refused_brokered())),
                 std::string("No, that's Halbrann's work. Speak to Halbrann, at the ruins of Linbrig."), "the premise: one person, named");
    FactPacket two = packets::refused_brokered();
    two.facts.push_back({FactKind::Person, "hero-harbourmaster-coastal-23-30",
                         {{"name", "Nerbrann"}, {"role", "harbourmaster"}, {"where", "the hamlet of Wenweg (Age 2)"}, {"status", "about"}}});
    const std::vector<std::string> replies = lines.replies(two);
    check::equal(replies.size(), std::size_t(1), "one line fits");
    check::equal(replies.front(), std::string("No, that work isn't mine to give."), "the line without a giver");
}

TEST(a_folder_of_lines_files_loads_as_one_library) {
    // Every .txt in the folder, and nothing else: a note beside them is not read as lines.
    const Lines folder = Lines::load(std::string(COMMUNICATOR_TEST_DATA) + "/lines_folder");
    check::equal(folder.size(), std::size_t(3), "the rows of a.txt and b.txt, not notes.md");
    check::equal(first(folder.replies(packets::parted())), std::string("Go well, stranger."), "b.txt's warden");
    check::equal(first(folder.replies(packets::packet(packets::gate_captain(), ActKind::Farewell, Outcome::Parted))),
                 std::string("Farewell."), "a.txt's anyone");
    check::throws([] { Lines::load(std::string(COMMUNICATOR_TEST_DATA) + "/no_lines"); }, "a folder with no .txt file");
}

TEST(an_alignment_with_no_bank_speaks_from_its_archetypes_core) {
    // "warden/NN" is the archetype almost unmodified: a warden of another alignment with no bank
    // of its own speaks from it before falling back to their post's lines.
    const Lines lines = Lines::parse("warden/NN | parted | Go well, stranger.\n"
                                     "warden/LE | parted | Farewell, and mind whose ground you cross.\n"
                                     "post:castellan | parted | Farewell from the wall.\n",
                                     "test");
    const auto as = [](const std::string& key) {
        FactPacket p = packets::parted();
        p.persona.voice_key = key;
        return p;
    };
    check::equal(first(lines.replies(as("warden/LE"))), std::string("Farewell, and mind whose ground you cross."), "its own bank");
    check::equal(first(lines.replies(as("warden/CG"))), std::string("Go well, stranger."), "no bank: the archetype's core");
    check::equal(first(lines.replies(as("warden/NN"))), std::string("Go well, stranger."), "the core itself");
    check::equal(first(lines.replies(as("tyrant/LE"))), std::string("Farewell from the wall."), "no archetype bank: the post");
}

TEST(a_speaker_speaks_from_their_own_bank_first) {
    const Lines lines = Lines::parse("warden/LN | parted | Go well, stranger.\n"
                                     "post:castellan | parted | Farewell from the wall.\n"
                                     "any | parted | Farewell.\n"
                                     "warden/LN | surroundings | Mind the {danger}.\n"
                                     "any | surroundings | The nearest is {nearest}.\n",
                                     "test");
    check::equal(first(lines.replies(packets::parted())), std::string("Go well, stranger."), "the warden's own");
    FactPacket keyless = packets::parted();
    keyless.persona.voice_key.clear();
    check::equal(first(lines.replies(keyless)), std::string("Farewell from the wall."), "the post's, with no voice key");
    check::equal(first(lines.replies(packets::packet(packets::gate_captain(), ActKind::Farewell, Outcome::Parted))),
                 std::string("Farewell."), "anyone's, for a post with no bank");
    // A bank whose lines the packet cannot fill gives way to the next.
    FactPacket warden_at_the_inn = packets::surroundings_from_the_inn();
    warden_at_the_inn.persona.voice_key = "warden/LN";
    check::equal(first(lines.replies(warden_at_the_inn)), std::string("The nearest is the silent keep, an hour's walk away, dangerous, held by squatters."),
                 "anyone's, when the warden's need a nest");
}

TEST(each_situation_is_read_from_the_packet) {
    const std::vector<std::pair<FactPacket, std::string>> expected = {
        {packets::surroundings(), "surroundings"},
        {packets::work(), "work_own"},
        {packets::work_at_the_inn(), "work_others"},
        {no_work(), "work_none"},
        {packets::about_self(), "about_self"},
        {about_halbrann(), "about_person"},
        {about_the_works(), "about_place"},
        {packets::unknown_place(), "unknown"},
        {packets::refused_brokered(), "refused_give"},
        {refused_with_no_giver(), "refused_give"},
        {refused_release(), "refused_release"},
        {refused_gone(), "refused_gone"},
        {refused_by_the_world(), "refused"},
        {packets::accepted(), "accepted"},
        {packets::declined(), "declined"},
        {packets::abandoned(), "abandoned"},
        {packets::parted(), "parted"},
        {packets::not_understood(), "not_understood"},
        {packets::packet(packets::castellan(), ActKind::AskSurroundings, Outcome::Answered), ""},
    };
    for (const auto& [p, situation] : expected) check::equal(Lines::situation(p), situation, "the situation");
    // A refusal whose reason has no kind is the world's.
    FactPacket unkinded = packets::refused_brokered();
    unkinded.facts.front().fields = {{"reason", "the work is Halbrann's to give"}};
    check::equal(Lines::situation(unkinded), std::string("refused"), "a refusal of no kind");
}

TEST(others_work_is_retold_as_theirs) {
    // "Walk the walls with me" is the giver's line; the innkeeper tells it as theirs.
    const Lines lines = Lines::parse("any | work_others | {others}.\nany | work_own | {own}.\n", "test");
    check::equal(first(lines.replies(packets::work_at_the_inn())),
                 std::string("Dornar of the council wants someone to walk the walls of Torhas with them, and Mekmoh the keeper "
                             "wants someone to bring an offering to the shrine of The Red Field at Narbod."),
                 "others' work, and a council sat on, not been");
    check::equal(first(lines.replies(packets::work())), std::string("Hold the fortress above Wenweg with me, fair work, a long walk away."),
                 "the speaker's own, in their words");
    // A purpose that gives its reason first is told by what it asks.
    FactPacket dragons = packets::work_at_the_inn();
    dragons.facts = {{FactKind::QuestOffer, "hook-hero-warden-hamlet-12-0",
                      {{"giver", "Aldmer"}, {"giver_role", "reeve"}, {"from_speaker", "no"}, {"verb", "slay"},
                       {"purpose", "Our fields lie in its reach: drive off Forest dragons, Who Ate Linbrig."}, {"difficulty", "4"},
                       {"range", "walk"}}}};
    check::equal(first(lines.replies(dragons)), std::string("Aldmer the reeve wants someone to drive off Forest dragons, Who Ate Linbrig."),
                 "the ask, not the reason");
}

TEST(a_world_refusal_says_why) {
    // The director words the reason itself, never the generator's message (orchestrator 1228ec5).
    const Lines lines = Lines::load(COMMUNICATOR_LINES);
    const std::vector<std::string> replies = lines.replies(refused_by_the_world());
    check::is_true(!replies.empty(), "the premise: replies");
    for (const std::string& reply : replies)
        check::is_true(reply.find("the offer has lapsed") != std::string::npos, "the reason said: " + reply);
}

TEST(the_choice_is_steady_and_follows_the_strangers_words) {
    const Lines lines = Lines::load(COMMUNICATOR_LINES);
    const FactPacket p = packets::parted();
    const std::vector<std::string> all = lines.replies(p);
    check::is_true(all.size() > 1, "the premise: more than one farewell for the warden");
    std::set<std::string> chosen;
    for (int i = 0; i < 24; ++i) {
        const std::string said = "Goodbye, " + std::to_string(i);
        const std::string reply = lines.speak(p, said);
        check::equal(lines.speak(p, said), reply, "the same words, the same reply");
        check::is_true(std::find(all.begin(), all.end(), reply) != all.end(), "one of the replies: " + reply);
        chosen.insert(reply);
        // Never the last reply again while another fits.
        check::is_true(lines.speak(p, said, reply) != reply, "not the last reply twice: " + reply);
    }
    check::equal(chosen.size(), all.size(), "every farewell, over different words");
    // With one line, the last reply is said again rather than nothing.
    const Lines one = Lines::parse("any | parted | Farewell.", "test");
    check::equal(one.speak(p, "Bye.", "Farewell."), std::string("Farewell."), "the only line");
    check::equal(one.speak(packets::surroundings(), "Bye."), std::string(), "nothing, where no line fits");
}

TEST(a_bad_lines_file_is_refused) {
    check::throws([] { Lines::load("no/such/lines.txt"); }, "a missing file");
    for (const std::string bad : {"warden/LN | parted", "warden/LN | parted | Go well. | Farewell.", "warden/LN | greeting | Hello.",
                                  "any | parted | Farewell, {giver}.", "any | parted | Farewell, {speaker.", " | parted | Go well.",
                                  "any | parted | ", "any | parted | Farewell[, {speaker}.", "any | parted | Farewell], {speaker}.",
                                  "any | parted | Farewell[, [{speaker}]].", "any | parted | Farewell[, friend]."})
        check::throws([&] { Lines::parse(bad, "test"); }, "refused: " + bad);
    check::equal(Lines::parse("# a comment\n\n  \nany | parted | Farewell.\n", "test").size(), std::size_t(1), "comments and blanks");
    check::equal(Lines::parse("\xEF\xBB\xBF# a comment\nany | parted | Farewell.\r\n", "test").size(), std::size_t(1),
                 "a byte-order mark and a CRLF");
}
