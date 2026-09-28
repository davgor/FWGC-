// Written lines, spoken for fact packets. No model: a reply is a line a person wrote, its blanks
// filled from the packet. Every row in data/lines/ is tried alone against every packet in
// authored_cases.hpp and held to the voice's rules, so a row that breaks one fails by name.
#include "communicator/authored.hpp"

#include <algorithm>
#include <iostream>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "authored_cases.hpp"
#include "check.hpp"
#include "communicator/meaning.hpp"
#include "embedder.hpp"
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
        {"Farewell, and enjoy a evening.", parted},
        {"Farewell, and have an good day.", parted},
        {"Farewell; every the turn of the season is hard.", parted},
        {"No, that's Halbrann's work.", refused},
    };
    for (const auto& [reply, p] : broken) check::is_true(!broken_rules(reply, p).empty(), "a broken rule found in: " + reply);
    check::is_true(broken_rules("Farewell, and mind the road.", parted).empty(), "a line that keeps them passes");
    check::is_true(broken_rules("Farewell; an hour's walk is a short one on a one-horse cart, and after that the road.", parted).empty(),
                   "an hour, a one-horse cart, after that the road: articles that fit");
    // An article before a blank fits one filling and not another: tried against every packet, a
    // row with "a {time}" meets an evening.
    check::is_true(!row_problems("any | small_talk | It's quiet, for a {time}.").empty(), "a {time}, as \"a evening\"");
    check::is_true(!row_problems("any | small_talk | It's hard going, this {season}.").empty(), "this {season}, as \"this the turn of the season\"");
    check::is_true(row_problems("any | small_talk | It's quiet, for {season}.").empty(), "the premise: the same lines pass without the article");
    check::is_true(row_problems("any | small_talk | It's quiet, this {time}.").empty(), "this {time} fits every hour");
    check::is_true(broken_rules("No, that's Halbrann's work." + closing, refused).empty(), "with its closing, it passes");
    // A name the writer typed passes the reply check when a test packet holds it, so the row's
    // own text is held too: the town is a blank, never a word the line writes.
    check::is_true(broken_rules("Farewell, and give Wenweg my regards.", parted).empty(), "the premise: the packet holds Wenweg");
    check::is_true(!row_problems("any | parted | Farewell, and give Wenweg my regards.").empty(), "a town the line writes itself");
    check::is_true(row_problems("any | parted | Farewell, and give {place} my regards.").empty(), "the town as a blank passes");
    check::is_true(row_problems("any | declined | Suit yourself, then.").empty(), "an outcome's opening is the program's own");
    // A saying is dropped in too: left out, "Suit yourself." is sound; said, a second sentence is not.
    check::is_true(row_problems("any | small_talk | Much as ever[; as we say, {saying}].").empty(), "a saying that continues the sentence");
    check::is_true(!row_problems("any | declined | Suit yourself.[ As we say, {saying}.]").empty(),
                   "an answer of one sentence may not add a second, even an optional one");
    check::is_true(!row_problems("any | small_talk | Much as ever[; as we say, {saying}], and no worse.").empty(), "a saying ends its sentence");
    check::is_true(row_problems("any | small_talk | Much as ever.[ As we say, {saying}.]").empty(), "its stop inside the part");
    // And none after a farewell, where four blind judges of five heard it as a non sequitur (2026-09-28).
    check::is_true(row_problems("any | parted | Go well.").empty(), "the premise: a farewell");
    check::is_true(!row_problems("any | parted | Go well[; as we say, {saying}].").empty(), "no saying after a farewell");
    // Where news goes in a line (contract version 11; writers found these let through, 2026-09-27):
    // {news} may open with "you" or a name, and {deed} is what the stranger did.
    check::is_true(row_problems("any | news | The word is {news}.").empty(), "the premise: news after words of the line's own");
    check::is_true(!row_problems("any | news | {news}, they say.").empty(), "{news} opening a sentence");
    check::is_true(!row_problems("any | news | They say you {news}.").empty(), "{news} after you: \"you you\"");
    check::is_true(row_problems("any | heard_deed | It was you that {deed}.").empty(), "the premise: {deed} after \"you that\"");
    check::is_true(row_problems("any | heard_deed | You're the one who {deed}, aren't you?").empty(), "and after \"who\"");
    check::is_true(!row_problems("any | heard_deed | Somebody {deed}, they say.").empty(), "{deed} said of somebody else");
    check::is_true(!row_problems("any | heard_deed | I knew you would {deed}.").empty(), "{deed} after \"you would\"");
    check::is_true(!row_problems("any | parted | Farewell (for now).").empty(), "a round bracket");
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

TEST(who_holds_a_place_is_said_in_plain_words) {
    const Lines lines = Lines::parse("any | about_place | That's {about}.", "test");
    const auto about = [&](std::vector<std::pair<std::string, std::string>> fields) {
        fields.emplace_back("range", "near");
        return first(lines.replies(packets::packet(packets::castellan(), ActKind::AskAboutPlace, Outcome::Answered,
                                                   {{FactKind::Place, "keyloc-test", std::move(fields)}})));
    };
    check::equal(about({{"kind", "reliquary_chapel"}, {"name", "the chapel at Ulkbarg"}, {"occupant", "descendants"}}),
                 std::string("That's the chapel at Ulkbarg, a short walk away, kept by its builders' heirs."), "descendants");
    check::equal(about({{"kind", "lime_kiln"}, {"name", "the kiln above Wenweg"}, {"occupant", "builders"}}),
                 std::string("That's the kiln above Wenweg, a short walk away, kept by its builders."), "builders");
    check::equal(about({{"kind", "shrine"}, {"name", "the green shrine"}, {"occupant", "nature"}}),
                 std::string("That's the green shrine, a short walk away, overgrown."), "nature");
    check::equal(about({{"kind", "border_fort"}, {"name", "the silent keep"}, {"occupant", "squatters"}}),
                 std::string("That's the silent keep, a short walk away, held by squatters."), "anyone else holds it");
    check::equal(about({{"kind", "waystone"}, {"name", "the first stone on the Ulkbarg road"}, {"state", "abandoned"}}),
                 std::string("That's the first stone on the Ulkbarg road, a short walk away, disused."), "a road's stone disused");
    check::equal(about({{"kind", "pass_hospice"}, {"name", "the hospice on the pass"}, {"state", "abandoned"}}),
                 std::string("That's the hospice on the pass, a short walk away, abandoned."), "a hospice abandoned");
    check::equal(about({{"kind", "waystone"}, {"name", "the ninth stone"}, {"state", "active"}}),
                 std::string("That's the ninth stone, a short walk away."), "in use, nothing to say");
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
                 std::string("No, that's Halbrann's work. Find them at the ruins of Linbrig."), "the premise: one person, named");
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

TEST(a_conversation_situation_is_read_from_the_packet_and_what_is_remembered) {
    using Chain = std::vector<std::string>;
    const std::vector<std::pair<FactPacket, Chain>> expected = {
        {packets::greeted_first(), {"greeted"}},
        {packets::greeted_again(), {"greeted_again"}},
        {packets::greeted_warmly(), {"greeted_warm", "greeted_again"}},
        {packets::greeted_coldly(), {"greeted_cold"}},
        {packets::greeted_after("taken", "friendly"), {"greeted_taken", "greeted_warm", "greeted_again"}},
        {packets::greeted_after("finished", "warm"), {"greeted_finished", "greeted_warm", "greeted_again"}},
        {packets::greeted_after("failed", "neutral"), {"greeted_failed", "greeted_again"}},
        {packets::greeted_after("abandoned", "neutral"), {"greeted_abandoned", "greeted_again"}},
        // In a later conversation, what came of it is no longer news (contract version 17).
        {packets::greeted_long_after("finished", "warm"), {"greeted_warm", "greeted_again"}},
        {packets::greeted_long_after("failed", "neutral"), {"greeted_again"}},
        {packets::beside(packets::greeted_long_after("finished", "warm")), {"greeted_warm", "greeted_again"}},
        {packets::greeted_long_after("taken", "friendly"), {"greeted_taken", "greeted_warm", "greeted_again"}},
        {packets::greeted_long_after("abandoned", "neutral"), {"greeted_abandoned", "greeted_again"}},
        {packets::small_talk("clear", "breezy"), {"small_talk_fair", "small_talk"}},
        {packets::small_talk("unsettled", "windy"), {"small_talk_wet", "small_talk"}},
        {packets::small_talk("stormy", "a gale"), {"small_talk_storm", "small_talk"}},
        {packets::small_talk("fair", "a gale"), {"small_talk_storm", "small_talk"}},
        {packets::small_talk("fair", "still", "hostile"), {"small_talk_cold"}},
        {packets::parted(), {"parted"}},
        {packets::parted_coldly(), {"parted_cold"}},
        {packets::forgiven("neutral"), {"forgiven", "forgiven_state"}},
        {packets::parted_holding_work(), {"parted", "parted_work"}},
        {packets::forgiven("hostile"), {"forgiven_cold"}},
        {packets::unwronged(), {"unwronged", "unwronged_state"}},
        {packets::about_the_town(), {"about_place"}},
        {packets::acknowledged(),
         {"acknowledged", "acknowledged_hardship", "acknowledged_deed", "acknowledged_plan", "acknowledged_kin", "acknowledged_grief",
          "acknowledged_doubt"}},
        {packets::acknowledged_with("taken"),
         {"acknowledged", "acknowledged_hardship", "acknowledged_deed", "acknowledged_plan", "acknowledged_kin", "acknowledged_grief",
          "acknowledged_doubt", "acknowledged_hardship_work", "acknowledged_grief_work", "acknowledged_deed_work", "acknowledged_doubt_work"}},
        {packets::acknowledged_with("finished"),
         {"acknowledged", "acknowledged_hardship", "acknowledged_deed", "acknowledged_plan", "acknowledged_kin", "acknowledged_grief",
          "acknowledged_doubt", "acknowledged_hardship_work", "acknowledged_grief_work", "acknowledged_doubt_done"}},
        {packets::acknowledged_with("offered"),
         {"acknowledged", "acknowledged_hardship", "acknowledged_deed", "acknowledged_plan", "acknowledged_kin", "acknowledged_grief",
          "acknowledged_doubt", "acknowledged_deed_work", "acknowledged_doubt_work"}},
        {packets::acknowledged_with("", "hostile"), {"acknowledged"}},
        {packets::small_talk_after_work(), {"small_talk_fair", "small_talk", "small_talk_work_done", "small_talk_since_work"}},
        {packets::beside(packets::small_talk_after_work()),
         {"small_talk_fair", "small_talk", "small_talk_work_done_beside", "small_talk_work_done", "small_talk_since_work"}},
        {packets::beside(packets::greeted_after("finished", "warm")),
         {"greeted_finished_beside", "greeted_finished", "greeted_warm", "greeted_again"}},
        {packets::beside(packets::greeted_after("taken", "friendly")), {"greeted_taken", "greeted_warm", "greeted_again"}},
        {packets::thanked_with_work("taken"), {"thanked", "thanked_work"}},
        {packets::thanked_with_work("offered"), {"thanked", "thanked_work"}},
        {packets::thanked_with_work("finished"), {"thanked"}},
        {packets::offended_after("finished"), {"offended", "offended_complaint"}},
        {packets::offended_after(""), {"offended"}},
        {packets::about_them(),
         {"them_who", "them_here", "them_long", "them_after", "them_need", "them_stakes", "them_past", "them_ties", "them_plans",
          "them_secret", "them_home", "them_foes"}},
        {packets::about_them_without_rivals(),
         {"them_who", "them_here", "them_long", "them_after", "them_need", "them_stakes", "them_past", "them_ties", "them_plans",
          "them_secret", "them_home", "them_foes_none"}},
        {packets::thanked(), {"thanked"}},
        {packets::thanked("hostile"), {"thanked_cold"}},
        {packets::offended(false), {"offended"}},
        {packets::offended(true), {"offended_again", "offended"}},
        {packets::safety_unsafe(), {"safety_unsafe"}},
        {packets::safety_wary(), {"safety_wary"}},
        {packets::safety_safe(), {"safety_safe"}},
        {packets::surroundings_again(), {"surroundings"}},
        {packets::exposed_mask(), {"exposed_true", "exposed"}},
        {packets::exposed_beast(), {"exposed_unleashed", "exposed"}},
        {packets::exposed_heart(), {"exposed_second", "exposed"}},
        {packets::denied(false), {"denied"}},
        {packets::denied(true), {"denied_again", "denied"}},
    };
    for (const auto& [p, chain] : expected) {
        const Chain got = Lines::situations_for(p);
        std::string shown;
        for (const std::string& s : got) shown += s + " ";
        check::is_true(got == chain, "the situations for " + chain.front() + ": " + shown);
        check::equal(Lines::situation(p), chain.front(), "the closest");
        for (const std::string& s : chain) check::is_true(Lines::situations().count(s) == 1, "a situation lines may use: " + s);
    }
}

TEST(a_bank_answers_from_its_closest_situation_before_another_bank_does) {
    // The warden has no warm greeting of its own: it greets as itself, from "greeted_again",
    // before anyone's warmer line.
    const Lines lines = Lines::parse("warden/LN | greeted_again | Back again, then.\n"
                                     "any | greeted_warm | Welcome back, friend.\n"
                                     "any | greeted_finished | Welcome back. You did good work.\n",
                                     "test");
    check::equal(first(lines.replies(packets::greeted_warmly())), std::string("Back again, then."), "the warden's own");
    FactPacket keyless = packets::greeted_warmly();
    keyless.persona.voice_key.clear();
    keyless.persona.role = "gate_captain";
    check::equal(first(lines.replies(keyless)), std::string("Welcome back, friend."), "anyone's warm line, for a speaker with none");
    keyless.facts.back() =
        packets::memory_of(keyless.speaker, {.met = "again", .times = 1, .mood = "warm", .work = "finished", .work_news = true});
    check::equal(first(lines.replies(keyless)), std::string("Welcome back. You did good work."), "what came of the work, first");
}

TEST(a_greeting_remembers_when_and_what) {
    const Lines lines = Lines::parse("any | greeted_again | Hello again.[ Still asking after {asked}?]\n"
                                     "any | greeted_again | Good day. I saw you {since}.\n",
                                     "test");
    // What was asked, and how long since, are there to say once they have met: a line saying one
    // is no fuller than one without, so a greeting recalls them now and then, not every visit.
    const std::vector<std::string> asked = lines.replies(packets::greeted_again());
    check::equal(asked.size(), std::size_t(2), "both lines, the one recalling what was asked no fuller");
    check::is_true(std::find(asked.begin(), asked.end(), "Hello again. Still asking after the burnt works?") != asked.end(),
                   "what they asked");
    FactPacket nothing_asked = packets::greeted_again();
    nothing_asked.facts.back() = packets::memory_of(nothing_asked.speaker, {.met = "again", .times = 1, .since = "weeks"});
    const std::vector<std::string> both = lines.replies(nothing_asked);
    check::equal(both.size(), std::size_t(2), "nothing asked: both lines, the one saying when no fuller");
    check::is_true(std::find(both.begin(), both.end(), "Good day. I saw you some weeks ago.") != both.end(), "when, said roughly");
    check::is_true(std::find(both.begin(), both.end(), "Hello again.") != both.end(), "and the plain one");
    nothing_asked.facts.back() = packets::memory_of(nothing_asked.speaker, {.met = "again", .times = 1, .since = "yesterday"});
    const std::vector<std::string> yesterday = lines.replies(nothing_asked);
    check::is_true(std::find(yesterday.begin(), yesterday.end(), "Good day. I saw you yesterday.") != yesterday.end(),
                   "the day before is yesterday, not a few days ago");
    // Asked about the town the speaker stands in, they do not ask after it: the stranger is in it.
    FactPacket own_town = packets::greeted_again();
    own_town.facts.back() = packets::memory_of(own_town.speaker, {.met = "again", .times = 1, .asked_before = own_town.persona.place});
    const std::vector<std::string> in_town = lines.replies(own_town);
    const auto asks_after = [](const std::string& line) { return line.find("asking after") != std::string::npos; };
    check::is_true(std::find(in_town.begin(), in_town.end(), "Hello again.") != in_town.end() &&
                       std::none_of(in_town.begin(), in_town.end(), asks_after),
                   "not asked after the town the speaker stands in");
    // Nor after themself, asked about last time: "Were you able to learn what you needed of Khassulk?",
    // said by Khassulk.
    FactPacket themself = packets::greeted_again();
    themself.facts.back() = packets::memory_of(themself.speaker, {.met = "again", .times = 1, .asked_before = themself.persona.name});
    const std::vector<std::string> of_them = lines.replies(themself);
    check::is_true(std::find(of_them.begin(), of_them.end(), "Hello again.") != of_them.end() &&
                       std::none_of(of_them.begin(), of_them.end(), asks_after),
                   "not asked after themself");
    // The same for the season and the time of day in small talk.
    const Lines talk = Lines::parse("any | small_talk_fair | A fine day for {season}.\nany | small_talk_fair | A fine day.\n", "test");
    check::equal(talk.replies(packets::small_talk("clear", "still")).size(), std::size_t(2), "the season silences no line");
}

TEST(the_strangers_own_words_are_said_back_after_a_greeting_now_and_then) {
    // Contract version 12: what the stranger once told the speaker of themselves, kept word for word,
    // said back to them as theirs, a sentence after the greeting.
    const Lines lines = Lines::parse("any | recalled | You told me once: {words}.\n"
                                     "any | greeted_again | Hello again.\n"
                                     "any | greeted_cold | What now.\n"
                                     "any | greeted_eased | Good to see you; I've you to thank for my nerve.\n"
                                     "any | greeted_taken | Back again. The work is still yours.\n"
                                     "any | greeted_finished | Welcome back. You did good work.\n"
                                     "any | greeted_failed | Hello again. It went badly, then.\n"
                                     "any | greeted_abandoned | Back again. You left my work undone.\n"
                                     "any | heard_deed | You're the one who {deed}, aren't you?\n",
                                     "test");
    const FactPacket remembering = packets::greeted_remembering();
    check::equal(lines.speak(remembering, "Hello!"), std::string("Hello again. You told me once: your sister keeps the mill at the ford."),
                 "their words, turned to them, after the greeting");
    check::equal(lines.speak(packets::greeted_again(), "Hello!"), std::string("Hello again."), "the control: none kept, none said back");
    // Not while said lately: now and then, not every greeting.
    const std::vector<std::string> recent = {"Hello again. You told me once: your sister keeps the mill at the ford."};
    check::equal(lines.speak(remembering, "Hello!", recent), std::string("Hello again."), "said lately, not again");
    // News to tell comes first.
    FactPacket with_news = packets::greeted_remembering();
    with_news.facts.insert(with_news.facts.end() - 1, packets::news_of_work("finished", true));
    const std::string told = lines.speak(with_news, "Hello!");
    check::is_true(told.rfind("Hello again. You're the one who", 0) == 0 && told.find("told me once") == std::string::npos,
                   "news first: " + told);
    // A cold speaker says only their cold greeting.
    FactPacket cold = packets::greeted_remembering();
    cold.facts.back() = packets::memory_of(cold.speaker, {.met = "again", .times = 1, .since = "days", .mood = "hostile"});
    check::equal(lines.speak(cold, "Hello!"), std::string("What now."), "nothing said back by a cold speaker");
    // Nor after a greeting that thanks the stranger for easing them, which would take "what you told
    // me" for the calming words.
    FactPacket eased = packets::greeted_remembering();
    eased.facts.back() = packets::memory_of(eased.speaker, {.met = "again", .times = 1, .since = "days", .eased = true});
    check::equal(lines.speak(eased, "Hello!"), std::string("Good to see you; I've you to thank for my nerve."), "nothing said back after an eased greeting");
    // Nor after a greeting for work of theirs the stranger holds, failed or gave up, which would make it
    // a reproach; nor after one that thanks them for work finished, where the words were heard as bolted
    // onto the thanks.
    for (const auto& [work, greeting] : std::vector<std::pair<std::string, std::string>>{
             {"taken", "Back again. The work is still yours."},
             {"failed", "Hello again. It went badly, then."},
             {"abandoned", "Back again. You left my work undone."}}) {
        FactPacket after = packets::greeted_remembering();
        after.facts.back() =
            packets::memory_of(after.speaker, {.met = "again", .times = 1, .since = "days", .work = work, .work_news = work == "failed"});
        check::equal(lines.speak(after, "Hello!"), greeting, "nothing said back after work " + work);
    }
    FactPacket finished = packets::greeted_remembering();
    finished.facts.back() =
        packets::memory_of(finished.speaker, {.met = "again", .times = 1, .since = "days", .work = "finished", .work_news = true});
    check::equal(lines.speak(finished, "Hello!"), std::string("Welcome back. You did good work."), "nothing said back after work finished");
    // A later conversation, the thanks said (contract version 17): greeted as ever, and the words may come back.
    FactPacket later = packets::greeted_remembering();
    later.facts.back() = packets::memory_of(later.speaker, {.met = "again", .times = 2, .since = "weeks", .work = "finished"});
    check::equal(lines.speak(later, "Hello!"), std::string("Hello again. You told me once: your sister keeps the mill at the ford."),
                 "work finished, no longer news: greeted as ever, the words said back");
    // Words that speak to someone would turn about, said back: none is said.
    check::equal(lines.speak(packets::greeted_remembering("I owe you my life."), "Hello!"), std::string("Hello again."),
                 "words to the speaker are not said back");
    // Of two sentences, the first of three words or more.
    check::equal(lines.speak(packets::greeted_remembering("Yes. I'm headed for the coast. My ship sails at dawn."), "Hello!"),
                 std::string("Hello again. You told me once: you were headed for the coast."), "the first sentence worth saying back");
    // A next sentence that leans on the first goes with it: alone, the first told the least of it.
    check::equal(lines.speak(packets::greeted_remembering("My brother came with me. He didn't make it back."), "Hello!"),
                 std::string("Hello again. You told me once: your brother came with you; he didn't make it back."),
                 "a sentence that leans on the first, with it");
    check::equal(lines.speak(packets::greeted_remembering(
                                 "My brother came with me to the fortress above the river. He never came home again after that night."),
                             "Hello!"),
                 std::string("Hello again."), "the two too long together: none");
    // Nor words too long for a line to carry.
    check::equal(lines.speak(packets::greeted_remembering(
                                 "My sister keeps the mill at the ford below the old bridge where the river bends toward the hills."),
                             "Hello!"),
                 std::string("Hello again."), "words too long to say back");
    check::is_true(lines.follows(remembering) == std::vector<std::string>{"You told me once: your sister keeps the mill at the ford."},
                   "what follow could say, for the rules");
}

// The floor the embedding model reaches on the questions below, never tuned to (2026-09-28): the first
// 21 held out, 19 right with seven descriptions; two more (what is at stake, their days) added after
// the hero benchmark, then 14 more questions written before those were tried: 29 of 35 with them, 28
// without, and 10 of the fresh 14 with them, 9 without. The misses are near ones ("Where did you come
// from before this?" as who they are, "Are you staying long?" as how they stand here).
constexpr std::size_t kAboutThemFloor = 29;
// Four more subjects after the hero benchmark's second and third runs (why they need help, how long,
// what comes if the work fails, and their days asked as the stranger asks), then 22 questions written
// before any wording of them was tried, two or more to a subject: 20 right, the wordings chosen on the
// 35 above and the benchmark's own six (the 22's numbers were seen for five wordings tried). The six:
// 6 of 6. Then a twelfth, where home is (2026-09-28): 19, "Would you ever go home?" heard as home.
constexpr std::size_t kAboutThemLaterFloor = 19;
// Six questions of home, written before any description of it was tried: 5 right (0 without it).
constexpr std::size_t kAboutThemHomeFloor = 5;
// Twelve questions of who stands against them or with them, written before the two were told apart: 7
// right with them apart (3 before).
constexpr std::size_t kFoesFloor = 7;
// Contract version 13: asked about themself, a speaker answers what was asked of them, heard by
// meaning among seven subjects (Lines::speak): who they are, their past, what they are after, how
// they stand here, who stands with them, whether they would leave, and what they keep to themselves.
// Measured on 21 questions written before the subjects were tried, three to a subject (2026-09-28).
TEST(asked_about_themself_they_answer_what_was_asked) {
    communicator::Meaning meaning(test_model::embedder());
    const std::vector<std::pair<std::string, std::string>> rows = {
        {"them_who", "I'm {speaker}, {known_as}."},        {"them_past", "I {lost}."},
        {"them_after", "I mean to {after}."},             {"them_here", "I {standing}."},
        {"them_ties", "{ties}."},                         {"them_plans", "Leave? Not while I mean to {after}."},
        {"them_secret", "That's mine to keep."}};
    std::string bank;
    std::map<std::string, std::string> subject_of;  // each subject's one line, as said to the packet
    const FactPacket p = packets::about_them();
    for (const auto& [situation, line] : rows) {
        const std::string row = "any | " + situation + " | " + line + "\n";
        bank += row;
        const std::vector<std::string> said = Lines::parse(row, "one").replies(p);
        check::is_true(said.size() == 1, "the premise: " + situation + " is said to the packet");
        subject_of[said.front()] = situation;
    }
    const Lines lines = Lines::parse(bank, "test");
    const std::vector<std::pair<std::string, std::string>> asked = {
        {"Who are you?", "them_who"},
        {"What's your name?", "them_who"},
        {"What do they call you?", "them_who"},
        {"What happened to your city?", "them_past"},
        {"What have you done in your life?", "them_past"},
        {"Did you fight in the war?", "them_past"},
        {"What do you want here?", "them_after"},
        {"What are you trying to do?", "them_after"},
        {"What's your aim?", "them_after"},
        {"How long have you been here?", "them_here"},
        {"Is this your post?", "them_here"},
        {"Where is your home?", "them_here"},
        {"Who are your friends?", "them_ties"},
        {"Do you have any enemies?", "them_ties"},
        {"Who do you trust?", "them_ties"},
        {"Will you ever leave?", "them_plans"},
        {"What will you do next?", "them_plans"},
        {"Where will you go after this?", "them_plans"},
        {"What are you afraid of?", "them_secret"},
        {"What do you really want?", "them_secret"},
        {"What are you hiding?", "them_secret"},
        // Fourteen more, written before the two further descriptions were tried (2026-09-28).
        {"And you are?", "them_who"},
        {"Who might you be?", "them_who"},
        {"Where did you come from before this?", "them_past"},
        {"What was your life like before?", "them_past"},
        {"What are you fighting for?", "them_after"},
        {"Why does this fortress matter so much?", "them_after"},
        {"How do you spend your days?", "them_here"},
        {"What's your work here?", "them_here"},
        {"Is anyone on your side?", "them_ties"},
        {"Who's against you?", "them_ties"},
        {"Are you staying long?", "them_plans"},
        {"What comes after all this?", "them_plans"},
        {"What keeps you up at night?", "them_secret"},
        {"What's your deepest wish?", "them_secret"}};
    std::size_t right = 0;
    std::string missed;
    for (const auto& [question, want] : asked) {
        const std::string reply = lines.speak(p, question, {}, &meaning);
        const auto got = subject_of.find(reply);
        const std::string heard = got == subject_of.end() ? "(" + reply + ")" : got->second;
        if (heard == want)
            ++right;
        else
            missed += "\n  " + question + ": " + heard + ", not " + want;
    }
    std::cout << "  about themself: " << right << " of " << asked.size() << " heard right" << missed << "\n";
    check::is_true(right >= kAboutThemFloor, std::to_string(right) + " of " + std::to_string(asked.size()) + missed);
    // Without meaning, who they are.
    check::equal(subject_of[lines.speak(p, "What happened to your city?", {}, nullptr)], std::string("them_who"),
                 "no meaning: who they are");
    // What is at stake, where a bank has a line of it; where none, what they are after.
    const std::string stake_row = "any | them_stakes | No one could {after} alone.\n";
    const std::string stakes = Lines::parse(stake_row, "one").replies(p).front();
    const Lines staked = Lines::parse(bank + stake_row, "staked");
    for (const std::string question : {"Why do you need help?", "What happens if the work fails?"}) {
        check::equal(staked.speak(p, question, {}, &meaning), stakes, "at stake: " + question);
        check::equal(subject_of[lines.speak(p, question, {}, &meaning)], std::string("them_after"), "no line of it: " + question);
    }
    // Nothing of theirs to say of what was asked: who they are.
    const std::string who_line = Lines::parse("any | them_who | I'm {speaker}, {known_as}.\n", "one").replies(p).front();
    FactPacket plain = p;
    plain.facts.erase(std::remove_if(plain.facts.begin(), plain.facts.end(), [](const Fact& f) { return f.kind == FactKind::Deed; }),
                      plain.facts.end());
    for (Fact& f : plain.facts)
        if (f.kind == FactKind::Self)
            f.fields.erase(std::remove_if(f.fields.begin(), f.fields.end(), [](const auto& field) { return field.first == "lost"; }),
                           f.fields.end());
    check::equal(subject_of[lines.speak(p, "What happened to your city?", {}, &meaning)], std::string("them_past"), "the premise: their past");
    check::equal(subject_of[lines.speak(plain, "What happened to your city?", {}, &meaning)], std::string("them_who"),
                 "nothing of their past to say: who they are");
    // Who they are, from whichever bank has it: not the speaker's own bank's first line of something else.
    const Lines banks = Lines::parse("warden/LN | them_here | I {standing}, as ever.\n" + bank, "banks");
    check::equal(banks.speak(plain, "What happened to your city?", {}, &meaning), who_line,
                 "nothing of their past to say, their own bank with no who: who they are, from another");
    // A frightened speaker says who they are and no more, their fear first.
    FactPacket frightened = p;
    for (auto& [field, value] : frightened.facts.back().fields)
        if (field == "feeling") value = "afraid";
    check::is_true(Lines::situations_for(frightened) == std::vector<std::string>{"them_who"}, "frightened: who they are");
    const std::string who = Lines::parse("any | them_who | I'm {speaker}, {known_as}.\n", "one").replies(p).front();
    const Lines shaken = Lines::parse(bank + "any | afraid_lead | Forgive me, I'm shaking.\n", "shaken");
    check::equal(shaken.speak(frightened, "How long have you held it?", {}, &meaning), "Forgive me, I'm shaking. " + who,
                 "frightened, asked how long: their fear, and who they are");
    // What they stand against, by name alone: small talk and "is it safe" say how far, and said with
    // how far in every answer, one conversation heard the same nest four times.
    const std::string against =
        Lines::parse("any | them_after | I mean to {after}, and the {danger} would have it otherwise.\n", "one").replies(p).front();
    check::is_true(against.find(", and the Frost owls would have it otherwise.") != std::string::npos, "the danger by name: " + against);
    // Why they need help, where a bank has a line of it; where none, what is at stake.
    const std::string need_row = "any | them_need | Because no one can {after} alone.\n";
    const std::string need = Lines::parse(need_row, "one").replies(p).front();
    const Lines needed = Lines::parse(bank + stake_row + need_row, "needed");
    check::equal(needed.speak(p, "Why do you need help?", {}, &meaning), need, "why they need help");
    check::equal(needed.speak(p, "What happens if the work fails?", {}, &meaning), stakes, "not why they need help");
    // Every subject with a line of its own: the later 22, and the benchmark's six.
    const std::vector<std::pair<std::string, std::string>> more = {
        {"them_need", "Because no one can {after} alone."}, {"them_long", "A good while now; I {standing}."}, {"them_stakes", "No one could {after} alone."}};
    std::string every = bank;
    for (const auto& [situation, line] : more) {
        const std::string row = "any | " + situation + " | " + line + "\n";
        every += row;
        const std::vector<std::string> said = Lines::parse(row, "one").replies(p);
        check::is_true(said.size() == 1, "the premise: " + situation + " is said to the packet");
        subject_of[said.front()] = situation;
    }
    const Lines all_of_them = Lines::parse(every, "every");
    const std::vector<std::pair<std::string, std::string>> later = {
        {"Who exactly am I talking to?", "them_who"},
        {"Do you have a name, castellan?", "them_who"},
        {"What did you do before you came here?", "them_past"},
        {"Were you ever at war?", "them_past"},
        {"What's your goal here?", "them_after"},
        {"What are you working toward?", "them_after"},
        {"What's your job, exactly?", "them_here"},
        {"Do you live here?", "them_here"},
        {"What fills your days?", "them_here"},
        {"What's a normal day like for you?", "them_here"},
        {"Have you got allies?", "them_ties"},
        {"Who would you call a friend?", "them_ties"},
        {"Would you ever go home?", "them_plans"},
        {"What will you do when this is over?", "them_plans"},
        {"What frightens you?", "them_secret"},
        {"Is there something you're not telling me?", "them_secret"},
        {"Why can't you do it yourself?", "them_need"},
        {"Why hire outsiders?", "them_need"},
        {"Have you been here long?", "them_long"},
        {"When did you take this post?", "them_long"},
        {"What if nobody helps you?", "them_stakes"},
        {"What's lost if the fortress falls?", "them_stakes"}};
    std::size_t later_right = 0;
    std::string later_missed;
    for (const auto& [question, want] : later) {
        const std::string reply = all_of_them.speak(p, question, {}, &meaning);
        const auto got = subject_of.find(reply);
        const std::string heard = got == subject_of.end() ? "(" + reply + ")" : got->second;
        if (heard == want)
            ++later_right;
        else
            later_missed += "\n  " + question + ": " + heard + ", not " + want;
    }
    std::cout << "  about themself, the later 22: " << later_right << " of " << later.size() << " heard right" << later_missed << "\n";
    check::is_true(later_right >= kAboutThemLaterFloor, std::to_string(later_right) + " of " + std::to_string(later.size()) + later_missed);
    for (const auto& [question, want] : std::vector<std::pair<std::string, std::string>>{
             {"Tell me about yourself.", "them_who"},
             {"How long have you held it?", "them_long"},
             {"Why do you need help holding it?", "them_need"},
             {"What happens if I fail?", "them_stakes"},
             {"What do you do when you're not holding the walls?", "them_here"},
             {"Do you ever think of leaving?", "them_plans"}})
        check::equal(subject_of[all_of_them.speak(p, question, {}, &meaning)], want, "the hero benchmark's: " + question);
    // Where home is: six questions written before the description was tried, and a held-out session's.
    const std::string home_row = "any | them_home | This is home: {home_here}.\n";
    const Lines homely = Lines::parse(every + home_row, "homely");
    subject_of[Lines::parse(home_row, "one").replies(p).front()] = "them_home";
    std::size_t home_right = 0;
    std::string home_missed;
    for (const std::string question : {"Is this where you were born?", "Did you grow up around here?", "Where are your people from?",
                                       "Is this place home to you?", "What's your home like?", "Do you think of home often?"}) {
        const auto got = subject_of.find(homely.speak(p, question, {}, &meaning));
        const std::string heard = got == subject_of.end() ? "?" : got->second;
        if (heard == "them_home")
            ++home_right;
        else
            home_missed += "\n  " + question + ": " + heard + ", not them_home";
    }
    std::cout << "  about themself, home: " << home_right << " of 6 heard right" << home_missed << "\n";
    check::is_true(home_right >= kAboutThemHomeFloor, std::to_string(home_right) + " of 6" + home_missed);
    check::equal(subject_of[homely.speak(p, "Where's home for you?", {}, &meaning)], std::string("them_home"), "the held-out session's");
    // What the stranger heard from them lately makes a line no fuller: how they stand, said once, is
    // not said again to what they do off duty.
    const Lines days = Lines::parse("any | them_here | I {standing}.\nany | them_here | Not much else; the post takes all of me.\n", "days");
    const std::string standing = Lines::parse("any | them_here | I {standing}.\n", "one").replies(p).front();
    check::equal(days.speak(p, "What do you do when you're off duty?", {}, &meaning), standing, "the premise: the fuller line");
    // Whichever way the speaker's lines turn (what they have heard moves them on), not said again.
    for (int heard = 0; heard < 4; ++heard) {
        FactPacket turned = p;
        for (auto& [field, value] : turned.facts.back().fields)
            if (field == "heard") value = std::to_string(heard);
        check::equal(days.speak(turned, "What do you do when you're off duty?", {"More seasons than I care to count; " + standing}, &meaning),
                     std::string("Not much else; the post takes all of me."),
                     "how they stand, heard lately: not said again, turn " + std::to_string(heard));
    }
    // Who they are, with a loss or a feat of their own: not their post again.
    const Lines story = Lines::parse("any | them_who | I'm {speaker}[ {known_as}].\n"
                                     "any | them_who | I'm {speaker}. I {lost}, and I mean to {after}.\n"
                                     "any | them_who | I'm {speaker}; I {feat}, and I mean to {after}.\n",
                                     "story");
    for (int heard = 0; heard < 4; ++heard) {
        FactPacket turned = p;
        for (auto& [field, value] : turned.facts.back().fields)
            if (field == "heard") value = std::to_string(heard);
        const std::string told = story.speak(turned, "Tell me about yourself.", {}, &meaning);
        check::is_true(told.find("Brenlin") != std::string::npos && told.find("command the fortress") == std::string::npos,
                       "who they are, with their story: " + told);
    }
    // An epithet that names what they are after is left out of who they are: "Haknbor Who Holds the
    // fortress above Kasdorl ... and I mean to hold the fortress above Kasdorl" (a held-out session).
    FactPacket holds = p;
    for (Fact& f : holds.facts)
        if (f.kind == FactKind::Self)
            for (auto& [field, value] : f.fields)
                if (field == "known_as") value = "Who Holds the fortress above Wenweg";
    const Lines named_for_it = Lines::parse("any | them_who | I'm {speaker}[ {known_as}], and I mean to {after}.\n", "named");
    check::equal(named_for_it.speak(holds, "Tell me about yourself.", {}, &meaning),
                 std::string("I'm Selbren, and I mean to hold the fortress above Wenweg."), "an epithet that names the aim, left out");
    check::equal(named_for_it.speak(p, "Tell me about yourself.", {}, &meaning),
                 std::string("I'm Selbren the Shield of Wenweg, and I mean to hold the fortress above Wenweg."), "the premise: an epithet said");
    // Home, where they stand or elsewhere: what they would do if they could leave is answered from it.
    const Lines leaving = Lines::parse("any | them_plans | If I could? I'd go home to {away}.\n"
                                       "any | them_plans | If I could? {home_here} is my home.\n",
                                       "leaving");
    check::equal(leaving.speak(p, "What would you do if you could leave?", {}, &meaning), std::string("If I could? Wenweg is my home."),
                 "at home");
    check::equal(leaving.speak(packets::about_them_far_from_home(), "What would you do if you could leave?", {}, &meaning),
                 std::string("If I could? I'd go home to Linbrig."), "far from home");
    // How long, where a bank has a line of it; where none, how they stand here.
    const std::string long_row = "any | them_long | A good while now; I {standing}.";
    const std::string how_long = Lines::parse(long_row, "one").replies(p).front();
    const Lines longer = Lines::parse(bank + long_row, "longer");
    for (const std::string question : {"How long have you been here?", "How long have you held this place?"}) {
        check::equal(longer.speak(p, question, {}, &meaning), how_long, "how long: " + question);
        check::equal(subject_of[lines.speak(p, question, {}, &meaning)], std::string("them_here"), "no line of it: " + question);
    }
    // What they are after, heard from them lately, is not marked "as I said": marked, it came back in four
    // answers of one conversation, and four judges of five heard a catchphrase (2026-09-28).
    const Lines aims = Lines::parse("any | them_after | I mean to {after}.\n", "aims");
    const std::string aim = aims.speak(p, "What are you trying to do?", {}, &meaning);
    check::equal(aim, std::string("I mean to hold the fortress above Wenweg."), "the premise: said as it comes, heard nothing lately");
    check::equal(aims.speak(p, "What are you trying to do?", {aim}, &meaning), aim, "heard lately: as it comes");
    // An optional part saying only what the stranger heard lately is left out, and the line is then the
    // freshest: "... alone I'd be too late.[ The {danger} don't wait.]" (the hero banks' third writers).
    const Lines needing = Lines::parse("any | them_need | I mean to {after}, and the {danger} stand in the way.\n"
                                       "any | them_need | I'd be too late alone.[ The {danger} don't wait.]\n",
                                       "needing");
    const std::string both = "I mean to hold the fortress above Wenweg, and the Frost owls stand in the way.";
    check::equal(needing.speak(p, "Why do you need help?", {}, &meaning), both, "heard nothing lately: the fullest line");
    check::equal(needing.speak(p, "Why do you need help?", {both}, &meaning), std::string("I'd be too late alone."),
                 "heard it all lately: the part left out, the line that says nothing again");
    check::equal(needing.speak(p, "Why do you need help?", {aim}, &meaning), std::string("I'd be too late alone. The Frost owls don't wait."),
                 "heard only the aim: the nest, not the aim");
    const Lines meant = Lines::parse("any | them_after | I mean to {after}[ as the {post} here].\n", "meant");
    check::equal(meant.speak(p, "What are you trying to do?", {aim}, &meaning),
                 std::string("I mean to hold the fortress above Wenweg as the castellan here."), "a part that says nothing counted: kept");
}

// Who stands against them (`them_foes`), asked apart from who stands with them: "Who are your enemies?"
// had drawn an ally, twice (a hero session's judges, 2026-09-28). Their rivals, or with none on record
// that they would name none; asked alone, as home is; with no line of it, who stands with them.
TEST(asked_who_stands_against_them_they_name_their_rivals_or_none) {
    communicator::Meaning meaning(test_model::embedder());
    const Lines lines = Lines::parse("warden/LN | them_ties | {ties}, as ever.\n"
                                     "any | them_foes | {foes}, and I don't forget it.\n"
                                     "any | them_foes_none | None I'd name.\n",
                                     "foes");
    const FactPacket rivals = packets::about_them();
    const std::string against = "Ulknord is my rival, and I don't forget it.";
    const std::string with = "Tornar stands with me; Ulknord is my rival, as ever.";
    check::equal(lines.speak(rivals, "Who are your enemies?", {}, &meaning), against, "their rival, though their own bank has only ties");
    check::equal(lines.speak(packets::about_them_without_rivals(), "Who are your enemies?", {}, &meaning), std::string("None I'd name."),
                 "none on record: none, and never the ally");
    check::equal(lines.speak(rivals, "Who are your friends?", {}, &meaning), with, "who stands with them, as ever");
    const Lines ties_only = Lines::parse("warden/LN | them_who | I'm {speaker}.\nwarden/LN | them_ties | {ties}, as ever.\n", "ties");
    check::equal(ties_only.speak(rivals, "Who are your enemies?", {}, &meaning), with, "no line of it: who stands with them, not who they are");
    // Twelve written before the two were told apart, six of each.
    std::size_t right = 0;
    std::string missed;
    for (const auto& [question, foe] : std::vector<std::pair<std::string, bool>>{
             {"Who would like to see you fall?", true},  {"Anyone out to get you?", true},         {"Who do you fight against?", true},
             {"Have you made enemies here?", true},       {"Who's your greatest rival?", true},     {"Does anyone want you gone?", true},
             {"Who fights at your side?", false},         {"Anyone you can count on?", false},      {"Who are your closest companions?", false},
             {"Do you have family nearby?", false},       {"Who would you trust with your life?", false}, {"Who helps you hold this place?", false}}) {
        const std::string reply = lines.speak(rivals, question, {}, &meaning);
        if (reply == (foe ? against : with))
            ++right;
        else
            missed += "\n  " + question + ": " + reply;
    }
    std::cout << "  who stands against them: " << right << " of 12 heard right" << missed << "\n";
    check::is_true(right >= kFoesFloor, std::to_string(right) + " of 12" + missed);
}

// Where home is (`them_home`), asked alone: the speaker's own bank's lines of how they stand here would
// shadow `any`'s of home. "Where's home for you?" had drawn a castellan's post (a held-out session's
// judge, 2026-09-28). With no home on record, how they stand here.
TEST(asked_where_home_is_they_say_where_home_is) {
    communicator::Meaning meaning(test_model::embedder());
    const Lines lines = Lines::parse("warden/LN | them_here | I {standing}, as ever.\n"
                                     "any | them_who | I'm {speaker}.\n"
                                     "any | them_here | I {standing}.\n"
                                     "any | them_home | This is home: {home_here}.\n"
                                     "any | them_home | My home is {away}, not here.\n",
                                     "home");
    const FactPacket home = packets::about_them();
    check::equal(home.persona.voice_key, std::string("warden/LN"), "the premise: the speaker's own bank says how they stand, not home");
    for (const std::string question : {"Where's home for you?", "Where is your home?"}) {
        check::equal(lines.speak(home, question, {}, &meaning), std::string("This is home: Wenweg."), "at home: " + question);
        check::equal(lines.speak(packets::about_them_far_from_home(), question, {}, &meaning), std::string("My home is Linbrig, not here."),
                     "far from home: " + question);
    }
    FactPacket homeless = home;
    for (Fact& f : homeless.facts)
        if (f.kind == FactKind::Self) std::erase_if(f.fields, [](const auto& field) { return field.first == "home"; });
    check::equal(lines.speak(homeless, "Where's home for you?", {}, &meaning), std::string("I command the garrison here, as ever."),
                 "no home on record: how they stand here, from their own bank");
    check::equal(lines.speak(home, "Is this your post?", {}, &meaning), std::string("I command the garrison here, as ever."),
                 "their post: how they stand here, as ever");
}

// Measured once, never tuned to. The first 15: 12 right with the descriptions first written, 14 with
// those now (2026-09-28; "My old friend runs the inn" taken plainly). 30 more, written after the first
// 15 were measured and before other descriptions were tried: 16 of 30 with the first, 20 with those
// now, chosen on the first 15 and the next 15 (the last 15's numbers were seen for every variant
// tried, four in all, and chose nothing).
constexpr std::size_t kToldFloor = 14;
constexpr std::size_t kToldLaterFloor = 20;
constexpr std::size_t kGriefFloor = 12;
// Doubt of themself (untried, afraid of failing), apart from what they can do: "I've never fought
// anything bigger than a wolf" had been met as a boast (a held-out hero session, 2026-09-28). One wording
// tried once: 10 of 12 lines written before it, and the 57 above unchanged.
constexpr std::size_t kDoubtFloor = 10;
// Contract version 13: what the stranger tells of themself is taken as what it is, heard by meaning (a
// hardship, something they did, a plan, their people, anything else), and small talk with someone whose
// work they just finished can be about that work. Measured on 15 lines written before the descriptions
// were tried, three to a kind (2026-09-28).
TEST(what_the_stranger_tells_is_taken_as_what_it_is) {
    communicator::Meaning meaning(test_model::embedder());
    const std::vector<std::pair<std::string, std::string>> rows = {
        {"acknowledged_hardship", "Ah, that's hard."},   {"acknowledged_deed", "Is that so? No small thing."},
        {"acknowledged_plan", "Right, go well."},        {"acknowledged_kin", "Aye, good to have them."},
        {"acknowledged", "I see."},                       {"acknowledged_hardship_work", "Ah, and on my account."},
        {"acknowledged_deed_work", "Good; the work needs that."}, {"acknowledged_grief", "Ah, I'm sorry for your loss."},
        {"acknowledged_grief_work", "Ah, and in my service; I'm sorry."}, {"acknowledged_doubt", "Everyone starts somewhere."},
        {"acknowledged_doubt_work", "Then keep your head."},     {"acknowledged_doubt_done", "And you stood anyway."}};
    std::string bank;
    std::map<std::string, std::string> kind_of;
    const FactPacket p = packets::acknowledged();
    for (const auto& [situation, line] : rows) {
        bank += "any | " + situation + " | " + line + "\n";
        kind_of[line] = situation;
    }
    const Lines lines = Lines::parse(bank, "test");
    const std::vector<std::pair<std::string, std::string>> told = {
        {"I lost my brother to the fever last spring.", "acknowledged_grief"},
        {"I'm exhausted, I haven't slept in days.", "acknowledged_hardship"},
        {"My leg's been broken since the fall.", "acknowledged_hardship"},
        {"I've been a smith for twenty years.", "acknowledged_deed"},
        {"I fought at the siege of the old keep.", "acknowledged_deed"},
        {"I can read and write, if that helps.", "acknowledged_deed"},
        {"I'm heading north to find work.", "acknowledged_plan"},
        {"Tomorrow I'll try the mountain pass.", "acknowledged_plan"},
        {"I mean to buy a boat and fish the coast.", "acknowledged_plan"},
        {"My mother still lives in the valley.", "acknowledged_kin"},
        {"I have two daughters waiting for me at home.", "acknowledged_kin"},
        {"My old friend runs the inn at the crossroads.", "acknowledged_kin"},
        {"The bread here is good.", "acknowledged"},
        {"I think it'll rain tomorrow.", "acknowledged"},
        {"Yes, that's right.", "acknowledged"}};
    std::size_t right = 0;
    std::string missed;
    for (const auto& [line, want] : told) {
        const std::string reply = lines.speak(p, line, {}, &meaning);
        const auto got = kind_of.find(reply);
        const std::string heard = got == kind_of.end() ? "(" + reply + ")" : got->second;
        if (heard == want)
            ++right;
        else
            missed += "\n  " + line + ": " + heard + ", not " + want;
    }
    std::cout << "  told of themself: " << right << " of " << told.size() << " taken right" << missed << "\n";
    check::is_true(right >= kToldFloor, std::to_string(right) + " of " + std::to_string(told.size()) + missed);
    // The later 30, and the benchmark's own lines apart from them.
    const std::vector<std::pair<std::string, std::string>> later = {
        {"Took an arrow in the shoulder on the road here.", "acknowledged_hardship"},
        {"We buried my father last winter.", "acknowledged_grief"},
        {"I haven't had a proper meal in a week.", "acknowledged_hardship"},
        {"I sailed with the coast fleet for ten years.", "acknowledged_deed"},
        {"I've tracked wolves through these hills since I was a boy.", "acknowledged_deed"},
        {"I built half the bridges in my valley.", "acknowledged_deed"},
        {"I'm off to the coast once the snow clears.", "acknowledged_plan"},
        {"Next I'll try my luck in the capital.", "acknowledged_plan"},
        {"I'm thinking of settling down around here.", "acknowledged_plan"},
        {"My brother fights for the king.", "acknowledged_kin"},
        {"My wife makes the best bread in our village.", "acknowledged_kin"},
        {"I grew up on a farm by the river.", "acknowledged_kin"},
        {"The inn was full last night.", "acknowledged"},
        {"That's a fine sword you have.", "acknowledged"},
        {"Aye, I heard you.", "acknowledged"},
        {"My horse went lame two days back and I've been walking since.", "acknowledged_hardship"},
        {"I lost everything when the river flooded.", "acknowledged_hardship"},
        {"The cold has got into my bones.", "acknowledged_hardship"},
        {"I was a ship's carpenter before all this.", "acknowledged_deed"},
        {"I held the gate at the ford with six others.", "acknowledged_deed"},
        {"I know a little healing.", "acknowledged_deed"},
        {"I'll be heading back to my village after this.", "acknowledged_plan"},
        {"I mean to find the men who burned my farm.", "acknowledged_plan"},
        {"I'm saving up to buy land of my own.", "acknowledged_plan"},
        {"My sister is waiting for me in the city.", "acknowledged_kin"},
        {"I've a son about your age.", "acknowledged_kin"},
        {"My people are fishers on the coast.", "acknowledged_kin"},
        {"The road was muddy.", "acknowledged"},
        {"I like it here.", "acknowledged"},
        {"Fair enough.", "acknowledged"}};
    std::size_t later_right = 0;
    std::string later_missed;
    for (const auto& [line, want] : later) {
        const std::string reply = lines.speak(p, line, {}, &meaning);
        const auto got = kind_of.find(reply);
        const std::string heard = got == kind_of.end() ? "(" + reply + ")" : got->second;
        if (heard == want)
            ++later_right;
        else
            later_missed += "\n  " + line + ": " + heard + ", not " + want;
    }
    std::cout << "  told of themself, the later 30: " << later_right << " of " << later.size() << " taken right" << later_missed << "\n";
    check::is_true(later_right >= kToldLaterFloor, std::to_string(later_right) + " of " + std::to_string(later.size()) + later_missed);
    check::equal(kind_of[lines.speak(p, "I nearly lost my arm on the second night.", {}, &meaning)], std::string("acknowledged_hardship"),
                 "the benchmark's hurt");
    check::equal(kind_of[lines.speak(p, "I've done some soldiering. Held a bridge in the south for a winter once.", {}, &meaning)],
                 std::string("acknowledged_deed"), "the benchmark's deed");
    check::equal(lines.speak(p, "I lost my brother to the fever last spring.", {}, nullptr), std::string("I see."), "no meaning: taken plainly");
    check::equal(lines.speak(packets::acknowledged_with("", "hostile"), "I lost my brother to the fever last spring.", {}, &meaning),
                 std::string("I see."), "a cold speaker takes it plainly");
    // In the light of the speaker's own work: a hurt taken doing it, a deed told while they hire.
    // Grief apart (contract version 13, a held-out hero session): 12 lines written before it was
    // tried, six deaths and six other hardships, and the sessions' own.
    std::size_t grief_right = 0;
    std::string grief_missed;
    for (const auto& [line, want] : std::vector<std::pair<std::string, std::string>>{
             {"My wife died in the winter.", "acknowledged_grief"},
             {"My son didn't come home from the war.", "acknowledged_grief"},
             {"I buried my best friend on the road here.", "acknowledged_grief"},
             {"My mother passed last month.", "acknowledged_grief"},
             {"We lost my uncle to the plague.", "acknowledged_grief"},
             {"My sister was killed in the raid.", "acknowledged_grief"},
             {"I broke my hand in the fall.", "acknowledged_hardship"},
             {"I've had a fever for days.", "acknowledged_hardship"},
             {"Our farm burned down.", "acknowledged_hardship"},
             {"I'm cold and I haven't eaten.", "acknowledged_hardship"},
             {"Bandits took everything I had.", "acknowledged_hardship"},
             {"My back's been bad since the mines.", "acknowledged_hardship"}}) {
        const std::string reply = lines.speak(p, line, {}, &meaning);
        const auto got = kind_of.find(reply);
        const std::string heard = got == kind_of.end() ? "(" + reply + ")" : got->second;
        if (heard == want)
            ++grief_right;
        else
            grief_missed += "\n  " + line + ": " + heard + ", not " + want;
    }
    std::cout << "  grief or hardship: " << grief_right << " of 12 taken right" << grief_missed << "\n";
    check::is_true(grief_right >= kGriefFloor, std::to_string(grief_right) + " of 12" + grief_missed);
    // Doubt of themself, apart from what they can do: 12 lines written before its one wording was tried.
    std::size_t doubt_right = 0;
    std::string doubt_missed;
    for (const std::string doubt : {"I'm not much of a fighter, to be honest.", "I've never held a sword in anger.",
                                    "I'm scared I'll let you down.", "I don't know if I'm good enough for this.",
                                    "I've only ever worked a farm.", "Truth is, I'm no soldier.", "I get nervous when it comes to a fight.",
                                    "I'm not as strong as I look.", "I've never done anything like this before.", "I'm new to all this.",
                                    "My hands shake when it comes to blows.", "I've no real training."}) {
        const std::string heard = kind_of[lines.speak(p, doubt, {}, &meaning)];
        if (heard == "acknowledged_doubt")
            ++doubt_right;
        else
            doubt_missed += "\n  " + std::string(doubt) + ": " + heard + ", not acknowledged_doubt";
    }
    std::cout << "  doubt: " << doubt_right << " of 12 taken right" << doubt_missed << "\n";
    check::is_true(doubt_right >= kDoubtFloor, std::to_string(doubt_right) + " of 12" + doubt_missed);
    // Told while the speaker's work is to do, answered as the one who asked it; once done, as fear stood through.
    const std::string wolf = "I've never been much of a fighter, but I'm quick.";
    check::equal(kind_of[lines.speak(p, wolf, {}, &meaning)], std::string("acknowledged_doubt"), "the premise: a session's doubt");
    for (const std::string work : {"offered", "taken"})
        check::equal(lines.speak(packets::acknowledged_with(work), wolf, {}, &meaning), std::string("Then keep your head."), "doubt, their work " + work);
    check::equal(lines.speak(packets::acknowledged_with("finished"), wolf, {}, &meaning), std::string("And you stood anyway."),
                 "doubt, their work done");
    for (const std::string work : {"", "failed", "abandoned"})
        check::equal(lines.speak(packets::acknowledged_with(work), wolf, {}, &meaning), std::string("Everyone starts somewhere."),
                     "doubt, their work " + (work.empty() ? std::string("none") : work));
    const std::string brother = "My brother came with me. He didn't make it back.";
    check::equal(kind_of[lines.speak(p, brother, {}, &meaning)], std::string("acknowledged_grief"), "the held-out session's death");
    check::equal(lines.speak(packets::acknowledged_with("finished"), brother, {}, &meaning), std::string("Ah, and in my service; I'm sorry."),
                 "a death in the speaker's service");
    const std::string hurt = "My leg's been broken since the fall.", deed = "I fought at the siege of the old keep.";
    check::equal(kind_of[lines.speak(p, hurt, {}, &meaning)], std::string("acknowledged_hardship"), "the premise: a hardship");
    check::equal(kind_of[lines.speak(p, deed, {}, &meaning)], std::string("acknowledged_deed"), "the premise: a deed");
    for (const std::string work : {"taken", "finished", "failed"})
        check::equal(lines.speak(packets::acknowledged_with(work), hurt, {}, &meaning), std::string("Ah, and on my account."),
                     "a hurt, their work " + work);
    for (const std::string work : {"", "offered", "abandoned"})
        check::equal(lines.speak(packets::acknowledged_with(work), hurt, {}, &meaning), std::string("Ah, that's hard."),
                     "a hurt, their work " + (work.empty() ? std::string("none") : work));
    for (const std::string work : {"offered", "taken"})
        check::equal(lines.speak(packets::acknowledged_with(work), deed, {}, &meaning), std::string("Good; the work needs that."),
                     "a deed, their work " + work);
    for (const std::string work : {"", "finished", "failed"})
        check::equal(lines.speak(packets::acknowledged_with(work), deed, {}, &meaning), std::string("Is that so? No small thing."),
                     "a deed, their work " + (work.empty() ? std::string("none") : work));
    // Small talk of the work just done: said when asked of it, never in its turn with the rest, and
    // never from a speaker whose work the stranger has not finished.
    const Lines talk = Lines::parse("any | small_talk | Much the same as ever.\n"
                                    "any | small_talk_work_done | It held, and that's what counts.\n",
                                    "talk");
    check::equal(talk.speak(packets::small_talk_after_work(), "Was it as bad as you feared?", {}, nullptr),
                 std::string("Much the same as ever."), "no meaning: the work is not talked of");
    for (const std::string asked : {"Was it as bad as you feared?", "Was it worth it, holding that place?"})
        check::equal(talk.speak(packets::small_talk_after_work(), asked, {}, &meaning), std::string("It held, and that's what counts."),
                     "asked how the work went: " + asked);
    for (const std::string asked : {"Nice weather today.", "How are you keeping?", "Any news?"})
        check::equal(talk.speak(packets::small_talk_after_work(), asked, {}, &meaning), std::string("Much the same as ever."),
                     "not the work: " + asked);
    for (const std::string work : {"", "taken", "failed", "abandoned"}) {
        FactPacket unfinished = packets::small_talk_after_work();
        for (auto& [field, value] : unfinished.facts.back().fields)
            if (field == "work") value = work;
        check::equal(talk.speak(unfinished, "Was it as bad as you feared?", {}, &meaning), std::string("Much the same as ever."),
                     "work " + (work.empty() ? std::string("none") : work) + ": nothing finished to talk of");
    }
}

// Contract version 13, from the hero benchmark's third judge (2026-09-28): thanks for the speaker's
// own work while it is to do, how things have been since that work was done, a complaint about it,
// and a greeting that asks who they are, each heard for what it is. Measured once on lines written
// before the descriptions were tried, the benchmark's own apart: 16 of 16, 13 of 14, 14 of 16 (two
// insults heard as complaints), 16 of 16.
constexpr std::size_t kThanksFloor = 16;
// How things have been since: 13 of 14, then 12 once news asked for with none to give drew how things
// have been since (2026-09-28): "Cold out, isn't it?" is read as news by both readings, and had been
// answered as small talk only because this test's bank holds no news lines ("No news that I've heard"
// with the library's).
constexpr std::size_t kSinceFloor = 12;
constexpr std::size_t kComplaintFloor = 14;
constexpr std::size_t kGreetingFloor = 16;
TEST(thanks_a_complaint_and_a_greeting_are_heard_for_what_they_are) {
    communicator::Meaning meaning(test_model::embedder());
    using Set = std::vector<std::pair<std::string, std::string>>;
    const auto measure = [&](const Lines& lines, const FactPacket& p, const Set& set, const std::map<std::string, std::string>& kind_of,
                             const std::string& name, std::size_t floor) {
        std::size_t right = 0;
        std::string missed;
        for (const auto& [line, want] : set) {
            const std::string reply = lines.speak(p, line, {}, &meaning);
            const auto got = kind_of.find(reply);
            const std::string heard = got == kind_of.end() ? "(" + reply + ")" : got->second;
            if (heard == want)
                ++right;
            else
                missed += "\n  " + line + ": " + heard + ", not " + want;
        }
        std::cout << "  " << name << ": " << right << " of " << set.size() << " heard right" << missed << "\n";
        check::is_true(right >= floor, name + ": " + std::to_string(right) + " of " + std::to_string(set.size()) + missed);
    };
    // Said alone, through the truth check speak() applies: replies() applies none, and a test line
    // opening with a capital no packet gives is dropped from the pool without a word.
    const auto said = [](const std::string& row, const FactPacket& p) {
        const std::string reply = Lines::parse(row, "one").speak(p, "", {}, nullptr);
        check::is_true(!reply.empty(), "the premise: said alone: " + row);
        return reply;
    };

    // Thanks, to a speaker whose work the stranger took.
    const FactPacket taken = packets::thanked_with_work("taken");
    const std::string welcome = said("any | thanked | You're welcome.\n", taken);
    const std::string not_yet = said("any | thanked_work | No need; thank me when it's done.\n", taken);
    const Lines thanks = Lines::parse("any | thanked | You're welcome.\nany | thanked_work | No need; thank me when it's done.\n", "thanks");
    measure(thanks, taken,
            Set{{"Thanks for the chance.", "work"},
                {"I appreciate you giving me the work.", "work"},
                {"Thank you for taking me on.", "work"},
                {"Thanks for putting your faith in me.", "work"},
                {"I won't let you down. Thank you.", "work"},
                {"Grateful for the job.", "work"},
                {"Thanks for this opportunity.", "work"},
                {"Thank you for picking me for this.", "work"},
                {"Thanks for the directions.", "other"},
                {"Thank you for the warning.", "other"},
                {"Thanks for the advice.", "other"},
                {"Thank you for your time.", "other"},
                {"Thanks, that's good to know.", "other"},
                {"Thank you for the ale.", "other"},
                {"Much obliged for the news.", "other"},
                {"Thanks for listening.", "other"}},
            {{welcome, "other"}, {not_yet, "work"}}, "thanks, their work taken", kThanksFloor);
    check::equal(thanks.speak(taken, "Thank you for trusting me with this.", {}, &meaning), not_yet, "the benchmark's thanks");
    check::equal(thanks.speak(packets::thanked_with_work("offered"), "Thanks for the chance.", {}, &meaning), not_yet, "their work offered");
    for (const std::string work : {"", "finished", "failed"})
        check::equal(thanks.speak(packets::thanked_with_work(work), "Thanks for the chance.", {}, &meaning), welcome,
                     "no work of theirs to do: " + (work.empty() ? std::string("none") : work));
    check::equal(thanks.speak(taken, "Thanks for the chance.", {}, nullptr), welcome, "no meaning: thanked plainly");

    // How things have been since, to a speaker whose work the stranger finished.
    const FactPacket finished = packets::small_talk_after_work();
    const std::string same = said("any | small_talk | Much the same as ever.\n", finished);
    const std::string held = said("any | small_talk_work_done | It held, and that's what counts.\n", finished);
    const std::string quieter = said("any | small_talk_since_work | It's been quieter since that work was done.\n", finished);
    const Lines talk = Lines::parse("any | small_talk | Much the same as ever.\n"
                                    "any | small_talk_work_done | It held, and that's what counts.\n"
                                    "any | small_talk_since_work | It's been quieter since that work was done.\n",
                                    "talk");
    measure(talk, finished,
            Set{{"Has it been quieter since?", "since"},
                {"Any trouble since then?", "since"},
                {"How's the place holding up since?", "since"},
                {"Things better now?", "since"},
                {"Has it been calm since we held it?", "since"},
                {"Is it easier now that it's done?", "since"},
                {"Was it as bad as you feared?", "work"},
                {"Was it worth it, holding that place?", "work"},
                {"Did you think we'd make it?", "work"},
                {"How many did we lose?", "work"},
                {"Nice weather today.", "other"},
                {"How are you keeping?", "other"},
                // Asked for news with none to give, how things have been since (below).
                {"Any news?", "since"},
                {"Cold out, isn't it?", "other"}},
            {{same, "other"}, {held, "work"}, {quieter, "since"}}, "small talk, their work done", kSinceFloor);
    check::equal(talk.speak(finished, "How have things been since?", {}, &meaning), quieter, "the benchmark's since");
    // How things have been since may name the worst nest near, as small talk does, and not while said
    // lately: "It sits easier with me, having that one squared" had said nothing (the judges, 2026-09-28).
    const Lines since = Lines::parse("any | small_talk | Much the same as ever.\n"
                                     "any | small_talk_since_work | It's been quieter since, though the {danger} are still about.\n",
                                     "since");
    check::equal(since.speak(packets::small_talk_after_work_near(), "How have things been since?", {}, &meaning),
                 std::string("It's been quieter since, though the Hippogryphs are still about."), "the nest near, named");
    check::equal(since.speak(packets::small_talk_after_work_near(), "How have things been since?", {"Mind the Hippogryphs."}, &meaning), same,
                 "the nest said lately: not again");
    check::equal(since.speak(finished, "How have things been since?", {}, &meaning), same, "no nest near: not that line");
    // Asked for news with none to give, how things have been since: "Anything new since we last spoke?"
    // drew "No news that I've heard" (a held-out session's judge, 2026-09-28). With news, the news; and
    // with their work not done, that there is none.
    const Lines asked_news = Lines::parse("any | small_talk | Much the same as ever.\n"
                                          "any | small_talk_since_work | It's been quieter since that work was done.\n"
                                          "any | no_news | No news that I've heard.\n"
                                          "any | news | The word is {news}.\n",
                                          "news");
    check::equal(asked_news.speak(finished, "Anything new since we last spoke?", {}, &meaning), quieter, "no news: how things have been since");
    FactPacket with_news = finished;
    with_news.facts.insert(with_news.facts.end() - 1, packets::news_of_a_scare("beaten", "Orlan"));
    const std::string word = asked_news.speak(with_news, "Anything new since we last spoke?", {}, &meaning);
    check::is_true(word.rfind("The word is ", 0) == 0, "with news: the news: " + word);
    FactPacket not_done = finished;
    for (auto& [field, value] : not_done.facts.back().fields)
        if (field == "work") value = "taken";
    check::equal(asked_news.speak(not_done, "Anything new since we last spoke?", {}, &meaning), std::string("No news that I've heard."),
                 "their work not done: no news");
    // Done beside them (contract version 14): talked of as one who was there, and where a bank has no
    // line of it, as ever.
    const Lines there = Lines::parse("any | small_talk | Much the same as ever.\n"
                                     "any | small_talk_work_done | It held, and that's what counts.\n"
                                     "any | small_talk_work_done_beside | You saw it as well as I did.\n",
                                     "there");
    check::equal(there.speak(packets::beside(finished), "Was it as bad as you feared?", {}, &meaning), std::string("You saw it as well as I did."),
                 "work done beside them");
    check::equal(there.speak(finished, "Was it as bad as you feared?", {}, &meaning), held, "not beside them");
    check::equal(talk.speak(packets::beside(finished), "Was it as bad as you feared?", {}, &meaning), held, "no line of it: as ever");
    check::equal(there.speak(packets::beside(finished), "Nice weather today.", {}, nullptr), std::string("Much the same as ever."),
                 "never in its turn with the weather");
    check::equal(talk.speak(finished, "Has it been quieter since?", {}, nullptr), same, "no meaning: small talk");
    FactPacket unfinished = finished;
    for (auto& [field, value] : unfinished.facts.back().fields)
        if (field == "work") value = "taken";
    check::equal(talk.speak(unfinished, "Has it been quieter since?", {}, &meaning), same, "their work not done: small talk");

    // A complaint about the speaker's work, which the act has as an insult.
    const FactPacket after = packets::offended_after("finished");
    const std::string manners = said("any | offended | Mind your manners.\n", after);
    const std::string easy = said("any | offended_complaint | There's no need for that; I never said it would be easy.\n", after);
    const Lines offended = Lines::parse("any | offended | Mind your manners.\n"
                                        "any | offended_complaint | There's no need for that; I never said it would be easy.\n",
                                        "offended");
    measure(offended, after,
            Set{{"You sent me in there blind.", "complaint"},
                {"That was far worse than you said.", "complaint"},
                {"Why didn't you tell me about the krakens?", "complaint"},
                {"You should have sent more help.", "complaint"},
                {"I nearly died out there, and for what?", "complaint"},
                {"You made it sound easy.", "complaint"},
                {"Nobody told me there'd be so many.", "complaint"},
                {"You owe me more than thanks for that.", "complaint"},
                {"You're a coward.", "insult"},
                {"Shut up, old man.", "insult"},
                {"You stink.", "insult"},
                {"You're useless.", "insult"},
                {"What a pathetic excuse for a castellan.", "insult"},
                {"Go to hell.", "insult"},
                {"You're an idiot.", "insult"},
                {"I've met goats with more sense.", "insult"}},
            {{manners, "insult"}, {easy, "complaint"}}, "an insult or a complaint, their work done", kComplaintFloor);
    check::equal(offended.speak(after, "You could have warned me it would be that bad.", {}, &meaning), easy, "the benchmark's complaint");
    check::equal(offended.speak(packets::offended_after(""), "You sent me in there blind.", {}, &meaning), manners,
                 "no work of theirs: an insult as ever");
    check::equal(offended.speak(after, "You sent me in there blind.", {}, nullptr), manners, "no meaning: an insult as ever");

    // A greeting that asks who they are.
    const FactPacket first = packets::greeted_first();
    const std::string hello = said("any | greeted | Good day.\n", first);
    const std::vector<std::string> whos = Lines::parse("any | greeted_who | I'm {speaker}, {post} here.\n", "one").follows(first);
    check::is_true(whos.size() == 1, "the premise: who they are follows a first greeting");
    const std::string who = whos.empty() ? std::string() : whos.front();
    check::is_true(communicator::untrue(who, first).empty(), "the premise: who they are, sayable");
    const Lines greetings = Lines::parse("any | greeted | Good day.\n"
                                         "any | greeted_cold | Speak your business.\n"
                                         "any | greeted_who | I'm {speaker}, {post} here.\n",
                                         "greetings");
    measure(greetings, first,
            Set{{"You must be the captain here.", "asks"},
                {"Are you the one in charge?", "asks"},
                {"Good day. Is this your fortress?", "asks"},
                {"Hello! You're the castellan, yes?", "asks"},
                {"Morning. You run this place?", "asks"},
                {"Greetings. Are you the keeper here?", "asks"},
                {"Well met. You're the one they call the Castellan?", "asks"},
                {"Hail. Are you the master of this hall?", "asks"},
                {"Hello.", "plain"},
                {"Good morning.", "plain"},
                {"Hail, friend.", "plain"},
                {"Hi there.", "plain"},
                {"Good day to you.", "plain"},
                {"Well met.", "plain"},
                {"Evening.", "plain"},
                {"Greetings, and good health to you.", "plain"}},
            {{hello, "plain"}, {hello + " " + who, "asks"}}, "a greeting, asking or not", kGreetingFloor);
    check::equal(greetings.speak(first, "Hail. You're the one who holds this place, aren't you?", {}, &meaning), hello + " " + who,
                 "the benchmark's greeting");
    check::equal(greetings.speak(packets::greeted_coldly(), "Are you the one in charge?", {}, &meaning), std::string("Speak your business."),
                 "a cold speaker: their greeting alone");
    // Cold at a first meeting too (word of the stranger's failed work can set a speaker against them).
    FactPacket cold_first = first;
    for (auto& [field, value] : cold_first.facts.back().fields)
        if (field == "mood") value = "hostile";
    check::equal(greetings.speak(cold_first, "Are you the one in charge?", {}, &meaning), std::string("Speak your business."),
                 "cold at a first meeting: their greeting alone");
    check::equal(greetings.speak(first, "Are you the one in charge?", {}, nullptr), hello, "no meaning: the greeting alone");
    // Nothing after a greeting that asks something itself: the stranger has a question to answer.
    const Lines asking = Lines::parse("any | greeted | Good day. What brings you here?\nany | greeted_who | I'm {speaker}, {post} here.\n",
                                      "asking");
    check::equal(asking.speak(first, "Are you the one in charge?", {}, &meaning), std::string("Good day. What brings you here?"),
                 "a greeting that asks: nothing after it");
    // Nor after a greeting that names them already.
    const Lines named = Lines::parse("any | greeted | Hello. I'm {speaker}.\nany | greeted_who | I'm {speaker}, {post} here.\n", "named");
    check::equal(named.speak(first, "Are you the one in charge?", {}, &meaning), std::string("Hello. I'm Selbren."),
                 "a greeting that names them: nothing after it");
    // Someone met before knows who they are.
    const Lines again = Lines::parse("any | greeted_again | Back again.\nany | greeted_who | I'm {speaker}, {post} here.\n", "again");
    for (const std::string line : {"Are you the one in charge?", "It's done. The fortress held.", "Remember me?"})
        check::equal(again.speak(packets::greeted_again(), line, {}, &meaning), std::string("Back again."), "met before: " + line);
}

// Contract version 13: an apology for how the stranger is, not for a wrong, met as that (a held-out
// hero session, 2026-09-28: "Whatever for? You have done me no wrong" two lines after a death).
// Measured once on 16 lines written before the descriptions were tried: 14 right.
constexpr std::size_t kSorryFloor = 14;
TEST(an_apology_for_how_one_is_is_met_as_that) {
    communicator::Meaning meaning(test_model::embedder());
    const FactPacket p = packets::unwronged();
    const Lines lines = Lines::parse("any | unwronged | What for? You've done me no wrong.\n"
                                     "any | unwronged_state | Nothing to forgive; you've reason enough to be.\n",
                                     "sorry");
    const std::string wrong = Lines::parse("any | unwronged | What for? You've done me no wrong.\n", "one").speak(p, "", {}, nullptr);
    const std::string state = Lines::parse("any | unwronged_state | Nothing to forgive; you've reason enough to be.\n", "one").speak(p, "", {}, nullptr);
    check::is_true(!wrong.empty() && !state.empty(), "the premise: both said alone");
    const std::vector<std::pair<std::string, std::string>> sorry = {
        {"Sorry, I'm not myself today.", "state"},
        {"Forgive me, I'm tired.", "state"},
        {"Sorry, I'm in a foul mood.", "state"},
        {"Excuse me, I've had a long day.", "state"},
        {"Sorry, I'm not good company right now.", "state"},
        {"Pardon me, I'm still shaken.", "state"},
        {"Sorry if I'm short with you; it's been hard.", "state"},
        {"Forgive my mood.", "state"},
        {"Sorry for what I said earlier.", "wrong"},
        {"I apologise for barging in.", "wrong"},
        {"Sorry I took so long.", "wrong"},
        {"Forgive me for doubting you.", "wrong"},
        {"I'm sorry I was rude.", "wrong"},
        {"Sorry about the mess.", "wrong"},
        {"My apologies for interrupting.", "wrong"},
        {"Sorry, I shouldn't have said that.", "wrong"}};
    std::size_t right = 0;
    std::string missed;
    for (const auto& [line, want] : sorry) {
        const std::string reply = lines.speak(p, line, {}, &meaning);
        const std::string heard = reply == state ? "state" : reply == wrong ? "wrong" : "(" + reply + ")";
        if (heard == want)
            ++right;
        else
            missed += "\n  " + line + ": " + heard + ", not " + want;
    }
    std::cout << "  an apology: " << right << " of " << sorry.size() << " heard right" << missed << "\n";
    check::is_true(right >= kSorryFloor, std::to_string(right) + " of " + std::to_string(sorry.size()) + missed);
    check::equal(lines.speak(p, "Sorry. I'm just tired.", {}, &meaning), state, "the benchmark's apology");
    check::equal(lines.speak(p, "Sorry, I'm not myself today.", {}, nullptr), wrong, "no meaning: as ever");
    // The same once there is something to forgive: "Sorry. I'm just tired." after a complaint had drawn
    // "Apology accepted; we're square" (the benchmark's judge, 2026-09-28). Not from a speaker still cold.
    const Lines forgiving = Lines::parse("any | forgiven | Apology accepted.\n"
                                         "any | forgiven_state | All right; get some rest.\n"
                                         "any | forgiven_cold | We'll see.\n",
                                         "forgiving");
    const FactPacket forgiven = packets::forgiven("neutral");
    check::equal(forgiving.speak(forgiven, "Sorry. I'm just tired.", {}, &meaning), std::string("All right; get some rest."), "forgiven, tired");
    check::equal(forgiving.speak(forgiven, "Sorry for what I said earlier.", {}, &meaning), std::string("Apology accepted."), "forgiven, a wrong");
    check::equal(forgiving.speak(packets::forgiven("hostile"), "Sorry. I'm just tired.", {}, &meaning), std::string("We'll see."),
                 "still cold: as a cold speaker");
    check::equal(forgiving.speak(forgiven, "Sorry. I'm just tired.", {}, nullptr), std::string("Apology accepted."), "no meaning: as ever");
}

// Seen off with the speaker's work to do, the stranger is wished luck, from whichever bank has it: "Wish
// me luck." had drawn "Goodbye, and don't be a stranger" (a held-out hero session's judge, 2026-09-28).
TEST(a_farewell_to_one_with_the_speakers_work_to_do_wishes_them_luck) {
    const Lines lines = Lines::parse("warden/LN | parted | Farewell, and keep your word.\n"
                                     "any | parted | Take care.\n"
                                     "any | parted_work | Go well, and good luck with it.\n"
                                     "any | parted_cold | Go, then.\n",
                                     "luck");
    const auto parting = [](const std::string& work, const std::string& mood) {
        FactPacket p = packets::parted();
        p.facts.back() = packets::memory_of(p.speaker, {.met = "again", .times = 1, .since = "days", .mood = mood, .work = work});
        return p;
    };
    check::equal(packets::parted().persona.voice_key, std::string("warden/LN"), "the premise: their own bank has a farewell");
    check::equal(lines.speak(parting("taken", "friendly"), "Wish me luck."), std::string("Go well, and good luck with it."),
                 "their work to do: luck, though their own bank has none");
    for (const std::string work : {"", "offered", "finished", "failed", "abandoned"})
        check::equal(lines.speak(parting(work, "friendly"), "Wish me luck."), std::string("Farewell, and keep your word."),
                     "their work " + (work.empty() ? std::string("none") : work) + ": as ever");
    check::equal(lines.speak(parting("taken", "hostile"), "Wish me luck."), std::string("Go, then."), "a cold speaker as a cold one");
}

TEST(a_recalled_row_brings_the_words_in_after_a_colon_says_no_when_and_asks_nothing) {
    using authored_cases::row_problems;
    const auto fails = [](const std::string& row, const std::string& says) {
        const std::vector<std::string> problems = row_problems(row);
        return std::any_of(problems.begin(), problems.end(), [&](const std::string& p) { return p.find(says) != std::string::npos; });
    };
    check::is_true(row_problems("any | recalled | You told me once: {words}; I've not forgotten.").empty(), "a colon, and no when");
    check::is_true(fails("any | recalled | You told me once that {words}.", "not after a colon"), "that {words}");
    check::is_true(fails("any | recalled | {words}, you told me once.", "not after a colon"), "{words} opening the line");
    check::is_true(fails("any | recalled | Last time you told me: {words}.", "says when"), "last time");
    check::is_true(fails("any | recalled | You told me the other day: {words}.", "says when"), "the other day");
    check::is_true(fails("any | recalled | Is she well, the one you told me of: {words}?", "asks after the words"), "a question");
    check::is_true(fails("any | recalled | You told me once: {words}; is that still so?", "asks after the words"), "a question after");
    // The words are the stranger's before they begin.
    check::is_true(fails("any | recalled | If I may: {words}.", "not the stranger's"), "if I may");
    check::is_true(fails("any | recalled | I kept this: {words}; you told me so.", "not the stranger's"), "told only after the words");
    for (const std::string row : {"any | recalled | What I'm told stays with me: {words}.", "any | recalled | I keep what's said to me: {words}.",
                                  "any | recalled | Quiet's good for keeping what you're told: {words}.",
                                  "any | recalled | What you are told here stays here: {words}.",
                                  "any | recalled | At a bedside you learn to keep what folk tell you: {words}."})
        check::is_true(fails(row, "not the stranger's"), "told, but not by them: " + row);
    for (const std::string row : {"any | recalled | You gave me this once: {words}.", "any | recalled | I've kept your words: {words}.",
                                  "any | recalled | I had it from you once: {words}.", "any | recalled | What you were saying went in: {words}.",
                                  "any | recalled | Hear your own words from me: {words}.",
                                  "any | recalled | Of all I hear, I kept yours: {words}."})
        check::is_true(row_problems(row).empty(), "told before the words: " + row);
}

TEST(a_line_writes_the_first_person_as_a_capital) {
    using authored_cases::row_problems;
    const auto says = [](const std::string& row, const std::string& what) {
        const std::vector<std::string> problems = row_problems(row);
        return std::any_of(problems.begin(), problems.end(), [&](const std::string& p) { return p.find(what) != std::string::npos; });
    };
    check::is_true(says("any | safety_unsafe | No. There are {danger}, and that's reason enough.[ And i'd keep clear of {worst}.]",
                        "\"i'd\": I is written as a capital"),
                   "i'd");
    check::is_true(says("any | small_talk | Well, i think it will hold.", "\"i\": I is written as a capital"), "i");
    check::is_true(!says("any | safety_unsafe | No. There are {danger}, and that's reason enough.[ And I'd keep clear of {worst}.]",
                         "I is written as a capital"),
                   "the control: I'd");
}

TEST(words_are_said_back_to_the_one_who_said_them) {
    using communicator::text::of_them;
    using communicator::text::said_back;
    check::equal(said_back("My sister keeps the mill at the ford."), std::string("your sister keeps the mill at the ford"), "my");
    check::equal(said_back("I am a smith, and I was a soldier!"), std::string("you are a smith, and you were a soldier"), "I am, I was");
    check::equal(said_back("We lost our farm to the flood."), std::string("you lost your farm to the flood"), "we, our");
    check::equal(said_back("I'm headed to Dunmoor, to find my brother."), std::string("you were headed to Dunmoor, to find your brother"),
                 "I'm, and a name keeps its capital");
    check::equal(said_back("Dunmoor is where I was born."), std::string("Dunmoor is where you were born"), "a name opening them");
    check::equal(said_back("To the coast."), std::string("to the coast"), "a word any sentence begins with, lowered");
    check::equal(said_back("I owe you my life."), std::string(), "words to someone are never turned about");
    // What was under way or to come is shifted back, as words told days before are; who they are is not.
    check::equal(said_back("I'm on my way to see my sister."), std::string("you were on your way to see your sister"), "under way");
    check::equal(said_back("I am looking for my brother."), std::string("you were looking for your brother"), "I am looking");
    check::equal(said_back("I'll be back before the frost."), std::string("you'd be back before the frost"), "I'll");
    check::equal(said_back("We will rebuild the mill."), std::string("you would rebuild the mill"), "we will");
    check::equal(said_back("I'm Aldric, from Harrowmere."), std::string("you're Aldric, from Harrowmere"), "who they are, as it is");
    check::equal(said_back("I am a smith."), std::string("you are a smith"), "what they are, as it is");
    check::equal(of_them("My sister keeps the mill."), std::string("their sister keeps the mill"), "told of them");
    check::equal(of_them("I am a smith, and I owe you my life."), std::string("they are a smith, and they owe you their life"),
                 "told of them, the speaker still you");
    check::equal(of_them("I'm on my way, and I'll be back."), std::string("they were on their way, and they'd be back"),
                 "told of them, shifted back");
}

TEST(an_answer_told_in_an_earlier_conversation_says_so_first) {
    // Contract version 12: asked of a place the speaker told the stranger of before, they say when.
    const Lines lines = Lines::parse("any | about_place | That's {about}.\n", "test");
    const std::string plain = first(lines.replies(packets::about_the_town()));
    check::is_true(plain.rfind("That's Wenweg", 0) == 0, "the premise: the plain answer: " + plain);
    check::equal(first(lines.replies(packets::about_the_town_told())), "As I told you some weeks ago, that's" + plain.substr(6),
                 "told some weeks ago, said first, the line's first word lowered");
    check::equal(first(lines.replies(packets::about_the_town_told("days"))), "As I told you a few days ago, that's" + plain.substr(6),
                 "a few days ago");
    check::equal(first(lines.replies(packets::about_the_town_told("today"))), plain, "told today: heard lately, not said so here");
    // Asked twice in this conversation, the speaker says so as they always have.
    FactPacket twice = packets::about_the_town_told();
    twice.facts.back() = packets::memory_of(twice.speaker, {.turn = 2, .repeat = true});
    const std::string again = first(lines.replies(twice));
    check::is_true(again.rfind("As I told you some weeks ago", 0) != 0 && again.find(", that's Wenweg") != std::string::npos,
                   "asked twice now, the lead for that: " + again);
    // It is held to the truth: backed by what the speaker remembers telling.
    check::is_true(communicator::untrue(first(lines.replies(packets::about_the_town_told())), packets::about_the_town_told()).empty(),
                   "a told lead the truth check backs");
}

TEST(asked_twice_a_speaker_says_so_before_saying_it_again) {
    const Lines lines = Lines::parse("any | surroundings | The nearest is {nearest}.\n", "test");
    const std::string once = first(lines.replies(packets::surroundings()));
    const std::string twice = first(lines.replies(packets::surroundings_again()));
    check::is_true(once.rfind("The nearest is ", 0) == 0, "the premise: the line once: " + once);
    const std::string rest = "the nearest is" + once.substr(std::string("The nearest is").size());
    check::is_true(twice == "As I said, " + rest || twice == "Again, " + rest || twice == "As I told you, " + rest,
                   "a lead before the line, its first word lowered: " + twice);
    // A name that opens the line keeps its capital.
    const Lines named = Lines::parse("any | surroundings | {here} is where you are.\n", "test");
    const std::string said = first(named.replies(packets::surroundings_again()));
    check::is_true(said.find(", Wenweg is where you are.") != std::string::npos, "a name keeps its capital: " + said);
    // An answer with fixed openings -- "is it safe" -- takes no lead: it would come before them.
    FactPacket safety = packets::safety_unsafe();
    safety.facts.back() = packets::memory_of(safety.speaker, {.turn = 2, .repeat = true});
    const Lines verdicts = Lines::parse("any | safety_unsafe | Not here. Keep clear of {worst}.\n", "test");
    check::is_true(first(verdicts.replies(safety)).rfind("Not here.", 0) == 0, "the verdict still first");
}

TEST(a_refusal_that_names_the_giver_ends_with_where_not_the_name_again) {
    const Lines lines = Lines::parse("any | refused_give | No, that work is {giver}'s to give.\n"
                                     "any | refused_give | No, that work isn't mine to give.\n",
                                     "test");
    const std::vector<std::string> replies = lines.replies(packets::refused_brokered());
    check::equal(replies.size(), std::size_t(1), "the fullest line");
    check::equal(replies.front(), std::string("No, that work is Halbrann's to give. Find them at the ruins of Linbrig."), "named once");
    const Lines unnamed = Lines::parse("any | refused_give | No, that work isn't mine to give.\n", "test");
    check::equal(first(unnamed.replies(packets::refused_brokered())),
                 std::string("No, that work isn't mine to give. Speak to Halbrann, at the ruins of Linbrig."), "named by the closing");
    // Nowhere to send them: the name alone.
    FactPacket nowhere = packets::refused_brokered();
    nowhere.facts[1].fields = {{"name", "Halbrann"}, {"role", "pretender"}, {"status", "about"}};
    check::equal(first(lines.replies(nowhere)), std::string("No, that work is Halbrann's to give."), "no closing, no where");
}

TEST(is_it_safe_names_the_places_the_verdict_rests_on) {
    const Lines lines = Lines::parse("any | safety_unsafe | Not here. Keep clear of {worst}.[ And there are {danger}.]\n"
                                     "any | safety_safe | Safe enough here.[ Only {worst} is worth avoiding.]\n",
                                     "test");
    check::equal(first(lines.replies(packets::safety_unsafe())),
                 std::string("Not here. Keep clear of the hospice on the Wenweg pass, an hour's walk away, dangerous, held by monsters. "
                             "And there are Glacier dragons nesting an hour's walk away."),
                 "the worst place and the nest");
    check::equal(first(lines.replies(packets::safety_safe())),
                 std::string("Safe enough here. Only the deeps under Linbrig, a long walk away, deadly, held by monsters is worth avoiding."),
                 "safe here, and the far danger still named");
    // The nest named in small talk just before is marked as said, not given again as new: "the krakens
    // come back word for word from turn 4, as if new" (the hero benchmark's judge, 2026-09-28).
    const std::string answer = "Not here. Keep clear of the hospice on the Wenweg pass, an hour's walk away, dangerous, held by monsters. "
                               "And there are Glacier dragons nesting an hour's walk away.";
    check::equal(lines.speak(packets::safety_unsafe(), "Is it safe?", {"Mind the Glacier dragons nesting an hour's walk away."}),
                 std::string("Not here. Keep clear of the hospice on the Wenweg pass, an hour's walk away, dangerous, held by monsters. "
                             "And there are Glacier dragons nesting an hour's walk away, as I said."),
                 "the nest said lately");
    check::equal(lines.speak(packets::safety_unsafe(), "Is it safe?", {}), answer, "the premise: said as it comes, heard nothing lately");
    const std::vector<std::string> heard = {"Mind the Glacier dragons nesting an hour's walk away."};
    const Lines on = Lines::parse("any | safety_unsafe | Not here. There are {danger} and worse besides.\n", "on");
    check::equal(on.speak(packets::safety_unsafe(), "Is it safe?", heard),
                 std::string("Not here. There are Glacier dragons nesting an hour's walk away, as I said, and worse besides."), "before a word");
    check::equal(on.speak(packets::safety_unsafe(), "Is it safe?", {"Mind the Glacier dragons, they say."}),
                 std::string("Not here. There are Glacier dragons nesting an hour's walk away and worse besides."), "only some of it heard: as it comes");
    const Lines already = Lines::parse("any | safety_unsafe | Not here. As I said, there are {danger}.\n", "already");
    check::equal(already.speak(packets::safety_unsafe(), "Is it safe?", heard),
                 std::string("Not here. As I said, there are Glacier dragons nesting an hour's walk away."), "a line that says so already: once");
}

TEST(a_saying_is_of_the_speakers_own_people) {
    const Lines lines = Lines::parse("saying:human_cold | saying | the cold keeps what the warm forgets\n"
                                     "saying:hill_dwarf | saying | a full larder makes a quiet house\n"
                                     "any | parted | Go well[; as we say, {saying}].\n"
                                     "any | parted | Farewell.\n",
                                     "test");
    const auto as_of = [](const std::string& people) {
        FactPacket p = packets::parted();
        p.persona.people = people;
        return p;
    };
    const std::vector<std::string> cold = lines.replies(as_of("human_cold"));
    check::is_true(std::find(cold.begin(), cold.end(), "Go well; as we say, the cold keeps what the warm forgets.") != cold.end(),
                   "the cold peoples' saying");
    check::is_true(std::find(cold.begin(), cold.end(), "Farewell.") != cold.end(), "a saying crowds out no line");
    const std::vector<std::string> dwarf = lines.replies(as_of("hill_dwarf"));
    check::is_true(std::find(dwarf.begin(), dwarf.end(), "Go well; as we say, a full larder makes a quiet house.") != dwarf.end(),
                   "the hill dwarves' own, never another people's");
    for (const std::string& people : {std::string(), std::string("elf")}) {
        const std::vector<std::string> none = lines.replies(as_of(people));
        check::is_true(std::find(none.begin(), none.end(), "Go well.") != none.end(), "no saying to say: the part left out");
    }
    // A saying at a sentence's start takes the sentence's capital.
    const Lines opening = Lines::parse("saying:human_cold | saying | the cold keeps what the warm forgets\n"
                                       "any | parted | Farewell.[ {saying}.]\n",
                                       "test");
    check::equal(first(opening.replies(as_of("human_cold"))), std::string("Farewell. The cold keeps what the warm forgets."),
                 "a capital where a sentence begins");
}

TEST(a_line_said_with_another_saying_is_the_same_line_again) {
    const Lines lines = Lines::parse("saying:human_cold | saying | the cold keeps what the warm forgets\n"
                                     "saying:human_cold | saying | no fire was ever lit by waiting\n"
                                     "any | parted | Go well[; as we say, {saying}].\n"
                                     "any | parted | Farewell.\n",
                                     "test");
    const auto parting = [](int heard) {
        FactPacket p = packets::parted();
        p.persona.people = "human_cold";
        p.facts.back() = packets::memory_of(p.speaker, {.met = "again", .times = heard + 1, .heard = heard});
        return p;
    };
    std::set<std::string> in_turn;
    for (int heard = 0; heard < 2; ++heard) in_turn.insert(lines.speak(parting(heard), "Bye.").substr(0, 7));
    check::is_true(in_turn.count("Go well") == 1, "the premise: in turn, the farewell with a saying");
    // Said on an earlier turn with the other saying, it is not said again while another fits.
    for (int heard = 0; heard < 2; ++heard)
        for (const std::string lately : {"Go well; as we say, the cold keeps what the warm forgets.", "Go well; as we say, no fire was ever lit by waiting."})
            check::equal(lines.speak(parting(heard), "Bye.", {lately}), std::string("Farewell."), "not the same farewell again: " + lately);
    // The same where the saying opens its sentence, with a capital.
    const Lines talk = Lines::parse("saying:human_cold | saying | the cold keeps what the warm forgets\n"
                                    "saying:human_cold | saying | no fire was ever lit by waiting\n"
                                    "any | small_talk | Much as ever.[ {saying}.]\n"
                                    "any | small_talk | Always busy.\n",
                                    "test");
    for (int heard = 0; heard < 2; ++heard) {
        FactPacket chat = packets::small_talk("fair", "still");
        chat.persona.people = "human_cold";
        chat.facts.back() = packets::memory_of(chat.speaker, {.met = "again", .times = heard + 1, .heard = heard});
        for (const std::string lately : {"Much as ever. The cold keeps what the warm forgets.", "Much as ever. No fire was ever lit by waiting."})
            check::equal(talk.speak(chat, "How are you?", {lately}), std::string("Always busy."), "not the same small talk again: " + lately);
    }
}

TEST(a_saying_keeps_the_voices_rules_wherever_it_goes) {
    check::is_true(row_problems("saying:human_cold | saying | the cold keeps what the warm forgets").empty(), "a sound saying");
    for (const std::string bad : {"saying:human_cold | saying | The cold keeps what the warm forgets",
                                  "saying:human_cold | saying | the cold keeps what the warm forgets.",
                                  "saying:human_cold | saying | the north wind keeps what the warm forgets",
                                  "saying:human_cold | saying | gold keeps what the warm forgets",
                                  "saying:human_cold | saying | the cold keeps what Wenweg forgets",
                                  "saying:human_cold | parted | Farewell.", "any | saying | the cold keeps what the warm forgets"})
        check::is_true(!row_problems(bad).empty(), "a saying that breaks a rule: " + bad);
}

TEST(others_work_is_retold_as_theirs) {
    // "Walk the walls with me" is the giver's line; the innkeeper tells it as theirs.
    const Lines lines = Lines::parse("any | work_others | {others}.\nany | work_own | {own}.\n", "test");
    check::equal(first(lines.replies(packets::work_at_the_inn())),
                 std::string("Dornar of the council wants someone to walk the walls of Torhas with them, and Mekmoh the keeper "
                             "wants someone to bring an offering to the shrine of The Red Field at Narbod."),
                 "others' work, and a council sat on, not been");
    check::equal(first(lines.replies(packets::work())), std::string("Hold the fortress above Wenweg with me, a long walk away."),
                 "the speaker's own, in their words, and how far; not how hard, which read as a notice board");
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

TEST(the_choice_is_steady_and_moves_on_each_time_the_same_is_heard) {
    const Lines lines = Lines::parse("any | parted | Farewell.\nany | parted | Safe travels.\nany | parted | Go well.\n"
                                     "any | parted | Take care.\n",
                                     "test");
    const auto parting = [](int heard, const std::string& uid = packets::castellan().uid) {
        FactPacket p = packets::parted();
        p.speaker = p.persona.uid = uid;
        p.facts.back() = packets::memory_of(uid, {.met = heard > 0 ? "again" : "first", .times = heard, .heard = heard});
        return p;
    };
    const std::vector<std::string> all = lines.replies(parting(0));
    check::equal(all.size(), std::size_t(4), "the premise: four farewells");
    std::set<std::string> chosen;
    for (int heard = 0; heard < 4; ++heard) {
        const std::string reply = lines.speak(parting(heard), "Goodbye.");
        check::equal(lines.speak(parting(heard), "Goodbye."), reply, "the same packet, the same reply");
        check::equal(lines.speak(parting(heard), "Farewell, then."), reply, "whatever the words, for now");
        check::is_true(std::find(all.begin(), all.end(), reply) != all.end(), "one of the replies: " + reply);
        if (heard > 0) check::is_true(reply != lines.speak(parting(heard - 1), "Goodbye."), "not the line said last time: " + reply);
        chosen.insert(reply);
        // Never the last reply again while another fits.
        check::is_true(lines.speak(parting(heard), "Goodbye.", {reply}) != reply, "not the last reply twice: " + reply);
    }
    check::equal(chosen.size(), all.size(), "every farewell once, before any twice");
    check::equal(lines.speak(parting(4), "Goodbye."), lines.speak(parting(0), "Goodbye."), "then round again");
    // Each speaker starts at a line of their own: two armorers do not part alike.
    std::set<std::string> first_lines;
    for (int i = 0; i < 12; ++i) first_lines.insert(lines.speak(parting(0, "hero-castellan-" + std::to_string(i)), "Goodbye."));
    check::is_true(first_lines.size() > 1, "speakers start apart");
    // With one line, the last reply is said again rather than nothing.
    const Lines one = Lines::parse("any | parted | Farewell.", "test");
    check::equal(one.speak(parting(0), "Bye.", {"Farewell."}), std::string("Farewell."), "the only line");
    // Said lately: not again while another fits; when all were, the one said longest ago.
    const std::string next = lines.speak(parting(0), "Goodbye.");
    std::vector<std::string> lately;
    for (int k = 0; k < 3; ++k) {
        lately.push_back(lines.speak(parting(0), "Goodbye.", lately));
        check::is_true(std::count(lately.begin(), lately.end(), lately.back()) == 1, "none said lately again: " + lately.back());
    }
    check::equal(lately.front(), next, "the turn's own line first");
    lately.push_back(lines.speak(parting(0), "Goodbye.", lately));
    check::equal(lines.speak(parting(0), "Goodbye.", lately), lately.front(), "all said: the one said longest ago");
    check::equal(one.speak(packets::surroundings(), "Bye."), std::string(), "nothing, where no line fits");
}

TEST(a_cold_speaker_parts_and_chats_coldly_whatever_their_bank_holds) {
    // The warden's bank has only warm lines: a cold warden leaves them for anyone's cold one.
    const Lines lines = Lines::parse("warden/LN | parted | Go well, and mind the road.\n"
                                     "warden/LN | small_talk | A fair {time} for it.\n"
                                     "any | parted_cold | Go, then.\n"
                                     "any | small_talk_cold | I've nothing to say to you.\n",
                                     "test");
    check::equal(first(lines.replies(packets::parted())), std::string("Go well, and mind the road."), "a warm farewell");
    const std::vector<std::string> cold = lines.replies(packets::parted_coldly());
    check::equal(cold.size(), std::size_t(1), "one farewell for a cold speaker");
    check::equal(cold.front(), std::string("Go, then."), "and a cold one");
    check::is_true(broken_rules(cold.front(), packets::parted_coldly()).empty(), "opening as a cold farewell does");
    check::is_true(!broken_rules("Go well, and mind the road.", packets::parted_coldly()).empty(), "a warm opening is not a cold one's");
    check::equal(first(lines.replies(packets::small_talk("fair", "still", "hostile"))), std::string("I've nothing to say to you."),
                 "no small talk from a cold speaker");
}

TEST(a_frightened_speaker_says_frightened_words_whatever_their_bank_holds) {
    // The warden's bank greets calmly and fearfully, and has a panicking farewell only; anyone's
    // bank has the rest.
    const Lines lines = Lines::parse("warden/LN | greeted | Good day to you.\n"
                                     "warden/LN | greeted_afraid | Oh! You startled me.\n"
                                     "warden/LN | parted | Farewell.\n"
                                     "any | greeted_panicked | Stay back! Please!\n"
                                     "any | parted_afraid | Be careful out there.\n"
                                     "any | small_talk_afraid | I can hardly think, let alone talk.\n"
                                     "any | small_talk | A fine {time} for it.\n",
                                     "test");
    const auto said = [&](ActKind act, Outcome outcome, const std::string& feeling) {
        return first(lines.replies(packets::frightened(act, outcome, feeling)));
    };
    check::equal(said(ActKind::Greet, Outcome::Greeted, "calm"), std::string("Good day to you."), "calm, a calm greeting");
    check::equal(said(ActKind::Greet, Outcome::Greeted, "uneasy"), std::string("Good day to you."), "uneasy: calm words, an uneasy face");
    check::equal(said(ActKind::Greet, Outcome::Greeted, "afraid"), std::string("Oh! You startled me."), "afraid: the bank's own");
    check::equal(said(ActKind::Greet, Outcome::Greeted, "panicked"), std::string("Oh! You startled me."),
                 "panicking, from a bank with afraid lines only: those, before another bank's panic");
    check::equal(said(ActKind::Farewell, Outcome::Parted, "afraid"), std::string("Be careful out there."),
                 "a bank with no frightened farewell leaves it to the next bank, never a calm one");
    check::equal(said(ActKind::SmallTalk, Outcome::Chatted, "afraid"), std::string("I can hardly think, let alone talk."),
                 "no weather from a frightened speaker");
    // Fear shows before any coldness the speaker feels for the stranger, and in thanks too.
    FactPacket hostile = packets::frightened(ActKind::Greet, Outcome::Greeted, "afraid");
    hostile.facts.back() = packets::memory_of(hostile.speaker, {.mood = "hostile", .turn = 2, .feeling = "afraid"});
    const Lines cold = Lines::parse("any | greeted_cold | Keep back.\nany | greeted_afraid | Oh! You startled me.\n"
                                    "any | thanked | You're welcome.\nany | thanked_afraid | Yes, yes, of course.\n",
                                    "test");
    check::equal(first(cold.replies(hostile)), std::string("Oh! You startled me."), "afraid and hostile: afraid");
    check::equal(first(cold.replies(packets::frightened(ActKind::Thank, Outcome::Thanked, "afraid"))), std::string("Yes, yes, of course."),
                 "thanked while afraid");
    check::is_true(broken_rules("Oh! You startled me.", packets::frightened(ActKind::Greet, Outcome::Greeted, "afraid")).empty(),
                   "opening as a frightened greeting does");
    check::is_true(!broken_rules("Good day to you.", packets::frightened(ActKind::Greet, Outcome::Greeted, "afraid")).empty(),
                   "a calm opening is not a frightened one's");
    // Calming words, by what came of them.
    const Lines calming = Lines::parse("any | calmed | All right. I'm breathing.\n"
                                       "any | still_afraid | No, I can't, not yet.\n"
                                       "any | not_afraid | I'm not afraid, but thank you.\n",
                                       "test");
    check::equal(first(calming.replies(packets::calmed("afraid"))), std::string("All right. I'm breathing."), "eased");
    check::equal(first(calming.replies(packets::still_afraid())), std::string("No, I can't, not yet."), "not eased");
    check::equal(first(calming.replies(packets::not_afraid())), std::string("I'm not afraid, but thank you."), "nothing to ease");
}

TEST(a_frightened_speaker_says_a_sentence_of_their_own_before_an_answer) {
    const Lines lines = Lines::parse("any | surroundings | The nearest is {nearest}.\n"
                                     "any | afraid_lead | Forgive me, I'm shaking.\n"
                                     "any | afraid_lead | All right, let me think.\n"
                                     "any | panicked_lead | Please, let me think!\n"
                                     "any | parted | Farewell.\n",
                                     "test");
    const std::string answer = "The nearest is the ninth stone on the Linbrig road, a short walk away, dangerous, held by monsters.";
    check::equal(lines.speak(packets::surroundings(), "What's around?"), answer, "the premise: a calm speaker's answer");
    const FactPacket afraid = packets::frightened(ActKind::AskSurroundings, Outcome::Answered, "afraid");
    const std::string lead = lines.lead(afraid);
    check::is_true(lead == "Forgive me, I'm shaking." || lead == "All right, let me think.", "an afraid speaker's lead: " + lead);
    check::equal(lines.speak(afraid, "What's around?"), lead + " " + answer, "said before the answer");
    check::is_true(lines.lead(afraid, {lead + " " + answer}) != lead, "not the lead said last, while another fits");
    check::equal(lines.lead(packets::frightened(ActKind::AskSurroundings, Outcome::Answered, "panicked")),
                 std::string("Please, let me think!"), "a panicking speaker's own");
    check::equal(lines.lead(packets::surroundings()), std::string(), "none from a calm speaker");
    check::equal(lines.lead(packets::frightened(ActKind::Farewell, Outcome::Parted, "afraid")), std::string(),
                 "none before an outcome's own opening");
    FactPacket safety = packets::safety_unsafe();
    safety.facts.back() = packets::memory_of(safety.speaker, {.turn = 2, .feeling = "afraid"});
    check::equal(lines.lead(safety), std::string(), "none before a verdict, which opens its answer");
    check::equal(communicator::reply_shape(afraid).sentences, communicator::reply_shape(packets::surroundings()).sentences + 1,
                 "and room for it in the reply's shape");
}

TEST(an_opening_is_whole_words_and_a_lead_one_plain_sentence) {
    // "No" opens "No, never." but not "Nothing to worry about.", which an unsafe answer must not say.
    check::is_true(rules::opens_with_one_of("No, never.", {"No"}) && rules::opens_with_one_of("No.", {"No"}), "No, as a word");
    check::is_true(!rules::opens_with_one_of("Nothing to worry about.", {"No"}), "not the start of Nothing");
    check::is_true(!broken_rules("Nothing to worry about.", packets::safety_unsafe()).empty(), "so an unsafe answer cannot open with it");
    // A lead goes before an answer: one sentence, and no question.
    check::is_true(row_problems("any | afraid_lead | Forgive me, I'm shaking.").empty(), "a lead");
    for (const std::string bad : {"any | afraid_lead | Wait. Let me think.", "any | afraid_lead | What was that?",
                                  "any | panicked_lead | Yes, yes, quickly!", "any | afraid_lead | No, wait, let me think.",
                                  "any | afraid_lead | Let me think, yes.", "any | joined_lead | Pardon me. I heard you ask."})
        check::is_true(!row_problems(bad).empty(), "not a lead: " + bad);
}

TEST(someone_talked_down_lately_remembers_it_and_a_bystander_who_joins_says_so) {
    // A bank with a line for being eased, and one without: the eased speaker says "now", never that
    // they were not afraid, where their bank has the line.
    const Lines lines = Lines::parse("warden/LN | not_afraid | I'm not afraid; I never was.\n"
                                     "warden/LN | not_afraid_eased | I'm all right now; you saw to that.\n"
                                     "any | not_afraid | I'm fine.\n"
                                     "warden/LN | greeted_again | Hello again.\n"
                                     "warden/LN | greeted_eased | Hello again. I'm steadier than when you saw me last.\n"
                                     "any | surroundings | The nearest is {nearest}.\n"
                                     "any | joined_lead | Pardon me, I couldn't help overhearing.\n"
                                     "any | afraid_lead | Forgive me, I'm shaking.\n",
                                     "test");
    check::equal(first(lines.replies(packets::not_afraid())), std::string("I'm not afraid; I never was."), "never eased: as ever");
    check::equal(first(lines.replies(packets::not_afraid_eased())), std::string("I'm all right now; you saw to that."),
                 "eased lately: all right now");
    check::equal(first(lines.replies(packets::greeted_eased())), std::string("Hello again. I'm steadier than when you saw me last."),
                 "met again after being eased: recalled first");
    check::equal(first(lines.replies(packets::greeted_again())), std::string("Hello again."), "the control: met again, not eased");
    // A bank without them leaves them to the next bank's, never its own plain line, which may deny it.
    const Lines plain = Lines::parse("warden/LN | not_afraid | I'm not afraid; I never was.\n"
                                     "warden/LN | greeted_again | Hello again.\n"
                                     "any | not_afraid_eased | I'm fine now, thanks to you.\n"
                                     "any | greeted_eased | Good to see you; I've you to thank for my nerve.\n",
                                     "test");
    check::equal(first(plain.replies(packets::not_afraid_eased())), std::string("I'm fine now, thanks to you."), "the next bank's");
    check::equal(first(plain.replies(packets::greeted_eased())), std::string("Good to see you; I've you to thank for my nerve."),
                 "the next bank's greeting, not a plain one");
    // The shape the same, for the voice too: all right now, never "I'm not afraid".
    const auto opens = [](const FactPacket& p, const std::string& words) {
        const std::vector<std::string> openings = communicator::reply_shape(p).openings;
        return std::find(openings.begin(), openings.end(), words) != openings.end();
    };
    check::is_true(opens(packets::not_afraid(), "I'm not afraid"), "the control: never eased, it may be denied");
    check::is_true(!opens(packets::not_afraid_eased(), "I'm not afraid") && opens(packets::not_afraid_eased(), "I'm all right"),
                   "eased lately: no denial in the shape");
    // A bystander who joined says so before the answer, whatever they feel; nobody else does.
    const std::string answer = lines.speak(packets::surroundings(), "What's around?");
    check::equal(lines.speak(packets::joined_surroundings(), "What's around?"), "Pardon me, I couldn't help overhearing. " + answer,
                 "joined: said first");
    FactPacket joined_afraid = packets::surroundings();
    joined_afraid.facts.back() = packets::memory_of(joined_afraid.speaker, {.feeling = "afraid", .joined = true});
    check::equal(lines.lead(joined_afraid), std::string("Pardon me, I couldn't help overhearing."), "joining first, fear or no");
    check::equal(lines.lead(packets::surroundings()), std::string(), "the control: asked, not joined");
    check::equal(communicator::reply_shape(packets::joined_surroundings()).sentences,
                 communicator::reply_shape(packets::surroundings()).sentences + 1, "room for it in the shape");
}

TEST(no_one_met_before_is_called_a_stranger) {
    const Lines lines = Lines::parse("any | offended | Mind your manners, stranger.\n"
                                     "any | offended | Watch your tongue.\n"
                                     "warden/LN | parted | Go well, stranger.\n"
                                     "any | parted | Farewell.\n",
                                     "test");
    FactPacket rude = packets::offended(false);
    check::equal(lines.replies(rude).size(), std::size_t(2), "at a first meeting, either");
    rude.facts.back() = packets::memory_of(rude.speaker, {.met = "again", .times = 2, .since = "days", .mood = "wary", .turn = 2});
    const std::vector<std::string> known = lines.replies(rude);
    check::is_true(known.size() == 1 && known.front() == "Watch your tongue.", "met before: never \"stranger\"");
    // A bank whose every line says it leaves the parting to the next bank.
    FactPacket bye = packets::parted();
    check::equal(first(lines.replies(bye)), std::string("Go well, stranger."), "the warden's own, the first time");
    bye.facts.back() = packets::memory_of(bye.speaker, {.met = "again", .times = 1, .since = "weeks"});
    check::equal(first(lines.replies(bye)), std::string("Farewell."), "met before: anyone's, over a stranger's");
    // Only the stranger addressed, whatever the case: the word in other uses stays.
    const Lines words = Lines::parse("any | parted | Farewell, and don't be a stranger.\n"
                                     "any | parted | Farewell; strangers are rare here.\n"
                                     "any | parted | Farewell, Stranger.\n"
                                     "any | parted | Go well, good stranger.\n",
                                     "test");
    check::is_true(words.replies(bye) == std::vector<std::string>{"Farewell, and don't be a stranger.", "Farewell; strangers are rare here."},
                   "only the stranger addressed");
}

TEST(small_talk_warns_of_a_nest_only_when_not_warned_of_lately) {
    const Lines lines = Lines::parse("any | small_talk_fair | A clear day.[ Mind the {danger}.]\n", "test");
    const FactPacket chat = packets::small_talk("clear", "still");
    const std::string warned = "A clear day. Mind the Hippogryphs nesting an hour's walk away.";
    check::equal(lines.speak(chat, "Nice day."), warned, "the nest mentioned, the first time");
    check::equal(lines.speak(chat, "Nice day.", {"Not safe. There are Hippogryphs nesting an hour's walk away."}), std::string("A clear day."),
                 "not again after a warning lately");
    check::equal(lines.speak(chat, "Nice day.", {"Fine weather."}), warned, "mentioned when nothing said lately named it");
    // An answer about what is near says it whatever was said before.
    const Lines around = Lines::parse("any | surroundings | The nearest is {nearest}.[ Mind the {danger}.]\n", "test");
    const std::string answer = around.speak(packets::surroundings(), "What's around?", {warned});
    check::is_true(answer.find("Mind the") != std::string::npos, "the answer keeps its warning: " + answer);
}

TEST(an_apology_is_answered_by_whether_there_was_a_wrong_and_how_the_speaker_takes_it_now) {
    // The warden's bank has a warm forgiveness only: a cold speaker leaves it for anyone's cold one.
    const Lines lines = Lines::parse("warden/LN | forgiven | Very well; the matter is closed.\n"
                                     "any | forgiven_cold | We'll see.\n"
                                     "any | unwronged | What for? You've done me no wrong.\n",
                                     "test");
    check::equal(first(lines.replies(packets::forgiven("neutral"))), std::string("Very well; the matter is closed."), "forgiven");
    check::equal(first(lines.replies(packets::forgiven("hostile"))), std::string("We'll see."), "forgiven, and still cold");
    check::equal(first(lines.replies(packets::unwronged())), std::string("What for? You've done me no wrong."), "nothing to forgive");
    check::is_true(broken_rules("We'll see.", packets::forgiven("hostile")).empty(), "a cold forgiveness opens as one");
    check::is_true(!broken_rules("Very well.", packets::forgiven("hostile")).empty(), "not as a warm one");
}

TEST(a_place_is_told_with_what_else_the_speaker_knows_of_it) {
    const Lines lines = Lines::parse(
        "any | about_place | That's {about}.[ There's {trades} there.][ You'll find {who} there.][ Mind the {around}.][ {work}.]\n"
        "any | about_place | That's {about}.\n",
        "test");
    const std::vector<std::string> told = lines.replies(packets::about_the_town());
    check::equal(told.size(), std::size_t(1), "the line that says the most");
    check::equal(told.front(),
                 std::string("That's Wenweg, right here. There's an administrator, a smith and an officiant there. You'll find Tornar of "
                             "the council there. Mind the Frost owls nesting close by it. Tornar of the council wants someone to walk "
                             "the walls of Wenweg with them."),
                 "its trades, who is there, the nest around it and the work there");
    check::equal(first(lines.replies(packets::about_the_fortress())),
                 std::string("That's the fortress of Wenweg, a long walk away. There's a gate guard there. You'll find Froswen the "
                             "warden and Legmok of the council there. Mind the Glacier dragons nesting right by it and Basilisks "
                             "nesting a short walk from it. I want someone to hold the fortress above Wenweg with me."),
                 "one trade, two heroes, nests right by it and a short walk from it, and the speaker's own work");
    // A nest near the speaker is not one around the place: its range is from the speaker.
    FactPacket mixed = packets::about_the_fortress();
    mixed.facts.push_back({FactKind::Danger, "nest-hippogryphs-179",
                           {{"creature", "Hippogryphs"}, {"danger", "3"}, {"family", "fantastic"}, {"range", "walk"}}});
    check::equal(first(lines.replies(mixed)), first(lines.replies(packets::about_the_fortress())), "only nests around the place");
    // A place with nothing more to it is told as before.
    check::equal(first(lines.replies(about_the_works())), std::string("That's the burnt works, an hour's walk away, held by squatters."),
                 "nothing more to say");
}

// Contract version 15: another giver's work says how that giver stands to the speaker.
TEST(another_givers_work_is_named_with_how_they_stand_to_the_speaker) {
    const Lines lines = Lines::parse("any | work_own | There's work: {own}.[ Otherwise, {others}.]\n", "work");
    FactPacket p = packets::work();
    const std::string plain = lines.speak(p, "Any work?");
    check::is_true(plain.find("Halbrann the pretender wants someone to help them take back Linbrig") != std::string::npos,
                   "the premise: the giver named plainly: " + plain);
    for (const auto& [tie, said] : std::vector<std::pair<std::string, std::string>>{
             {"ally", "Halbrann the pretender, who stands with me, wants someone to help them take back Linbrig"},
             {"kin", "Halbrann the pretender, my own kin, wants someone to help them take back Linbrig"},
             {"rival", "Halbrann the pretender, my rival, wants someone to help them take back Linbrig"}}) {
        FactPacket tied = p;
        for (Fact& f : tied.facts)
            if (f.kind == FactKind::QuestOffer && f.subject == packets::kBrokeredOffer) f.fields.emplace_back("giver_tie", tie);
        const std::string reply = lines.speak(tied, "Any work?");
        check::is_true(reply.find(said) != std::string::npos, tie + ": " + reply);
        check::is_true(communicator::untrue(reply, tied).empty(), tie + ": nothing untrue in it");
    }
}

// Contract version 13: asked for work again with nothing new to it, a speaker says so (the hero
// benchmark, 2026-09-28: one list, word for word, in two conversations days apart).
TEST(work_asked_for_again_with_nothing_new_is_said_as_told) {
    const Lines lines = Lines::parse("any | work_others | No work of mine, but {others}.\n"
                                     "any | work_others_told | Nothing new since I told you.\n"
                                     "any | work_own | There's work: {own}.[ Otherwise, {others}.]\n"
                                     "any | work_own_told | It's as I told you: {own}.\n",
                                     "test");
    const FactPacket asked = packets::work_at_the_inn();
    const std::string whole = lines.speak(asked, "Any work?");
    check::is_true(whole.rfind("No work of mine, but Dornar", 0) == 0, "the premise: the whole answer: " + whole);
    FactPacket again = asked;
    again.facts.back() = packets::memory_of(again.speaker, {.met = "again", .times = 1, .heard = 1});
    check::equal(lines.speak(again, "Any more work?", {whole}), std::string("Nothing new since I told you."),
                 "asked before, and all of it heard lately: said as told");
    check::equal(lines.speak(asked, "Any work?", {whole}), whole, "never asked before: the whole answer");
    check::equal(lines.speak(again, "Any more work?", {}), whole, "not heard lately: the whole answer");
    const std::string part = whole.substr(0, whole.find(", and "));
    check::is_true(part != whole, "the premise: the answer names two others' work");
    check::equal(lines.speak(again, "Any more work?", {part}), whole, "only some of it heard: the whole answer");
    // Their own work too.
    const FactPacket own = packets::work();
    const std::string all = lines.speak(own, "Any work?");
    check::is_true(all.rfind("There's work: ", 0) == 0, "the premise: their own work: " + all);
    FactPacket own_again = own;
    own_again.facts.back() = packets::memory_of(own_again.speaker, {.met = "again", .times = 1, .heard = 1});
    check::is_true(lines.speak(own_again, "Any work?", {all}).rfind("It's as I told you: ", 0) == 0, "their own, said as told");
}

// Contract version 16: the first answer names the speaker's own work alone, and asked again, they add
// the others' without saying their own again.
TEST(work_asked_for_again_after_their_own_adds_the_others) {
    const Lines lines = Lines::parse("any | work_own | There's work: {own}.[ Otherwise, {others}.]\n"
                                     "any | work_own_told | It's as I told you: {own}.[ Or else, {others}.]\n"
                                     "any | work_more | Beyond mine, {others}.\n",
                                     "test");
    FactPacket own_only = packets::work();
    std::erase_if(own_only.facts, [](const Fact& f) { return f.kind == FactKind::QuestOffer && f.subject != packets::kOwnOffer; });
    const std::string first = lines.speak(own_only, "Any work?");
    check::equal(first, std::string("There's work: hold the fortress above Wenweg with me, a long walk away."), "the premise: their own alone");
    FactPacket again = packets::work();
    again.facts.back() = packets::memory_of(again.speaker, {.met = "first", .turn = 2, .repeat = true, .heard = 1});
    const std::string more = lines.speak(again, "Anything else?", {first});
    check::is_true(more.rfind("Beyond mine, ", 0) == 0 && more.find("Wenweg with me") == std::string::npos, "the others' alone: " + more);
    check::is_true(communicator::untrue(more, again, {first}).empty(), "nothing untrue in it: " + more);
    // Their own not heard lately: the whole answer. All of it heard: as told. Never asked before: the
    // answer as it comes.
    const std::string whole = lines.speak(again, "Anything else?", {});
    check::is_true(whole.find("hold the fortress above Wenweg") != std::string::npos && whole.find("Otherwise, ") != std::string::npos,
                   "their own not heard lately: the whole answer: " + whole);
    check::is_true(lines.speak(again, "Anything else?", {first, more}).rfind("It's as I told you: ", 0) == 0, "all of it heard: as told");
    FactPacket never = packets::work();
    check::is_true(lines.speak(never, "Any work?", {first}).rfind("There's work: ", 0) == 0, "never asked before: the answer as it comes");
    // With no one else's work, what they said again.
    FactPacket own_again = own_only;
    own_again.facts.back() = again.facts.back();
    check::is_true(lines.speak(own_again, "Anything else?", {first}).rfind("It's as I told you: ", 0) == 0, "nothing else: as told");
}

TEST(an_answer_heard_lately_is_said_again_as_told) {
    const Lines lines = Lines::parse("any | safety_unsafe | Not safe. Keep clear of {worst}.[ Mind the {danger}.]\n"
                                     "any | safety_unsafe_told | Not safe, as I told you. Keep clear of {worst}.\n"
                                     "any | surroundings | You're in {here}. The nearest is {nearest}.[ Past that, {others}.][ Mind the {danger}.]\n"
                                     "any | surroundings_told | The same as I told you: the nearest is {nearest}.\n"
                                     "any | about_place | That's {about}.\n"
                                     "any | about_place_told | That's all I know of it: {about}.\n",
                                     "test");
    const FactPacket asked = packets::safety_unsafe();
    const std::string whole = lines.speak(asked, "Is it safe?");
    const std::string hospice = "the hospice on the Wenweg pass, an hour's walk away, dangerous, held by monsters";
    check::equal(whole, "Not safe. Keep clear of " + hospice + ". Mind the Glacier dragons nesting an hour's walk away.",
                 "the premise: the whole answer");
    // Never asked before, it is no verdict told, however much of it was named in passing: the whole answer,
    // the nest heard lately marked as said.
    check::equal(lines.speak(asked, "Is it safe?", {whole}),
                 "Not safe. Keep clear of " + hospice + ". Mind the Glacier dragons nesting an hour's walk away, as I said.",
                 "asked for the first time: the whole answer");
    FactPacket again = packets::safety_unsafe();
    again.facts.back() = packets::memory_of(again.speaker, {.met = "again", .times = 1, .heard = 1});
    check::is_true(lines.told(again, {whole}) && !lines.told(asked, {whole}) && !lines.told(again, {}),
                   "told() says whether the answer is said again as told");
    // Said again, it tells less: the place by name, the nest by kind.
    check::equal(lines.speak(again, "Is it still safe?", {whole}), std::string("Not safe, as I told you. Keep clear of the hospice on the Wenweg pass."),
                 "asked before, and all of it heard lately: said again as told, and briefly");
    const Lines nest = Lines::parse("any | safety_unsafe | Not safe. Keep clear of {worst}.[ Mind the {danger}.]\n"
                                    "any | safety_unsafe_told | Not safe, as I told you.[ Mind the {danger}.]\n",
                                    "test");
    check::equal(nest.speak(again, "Is it still safe?", {whole}), std::string("Not safe, as I told you. Mind the Glacier dragons."),
                 "the nest by kind");
    const Lines town = Lines::parse(
        "any | about_place | That's {about}.[ There's {trades} there.][ You'll find {who} there.][ Mind the {around}.][ {work}.]\n"
        "any | about_place_told | You've had all I know of it: {about}.[ Mind the {around}.]\n",
        "test");
    const std::string fortress = first(town.replies(packets::about_the_fortress()));
    check::equal(town.speak(packets::about_the_fortress(), "Tell me more.", {fortress}),
                 std::string("You've had all I know of it: the fortress of Wenweg, a long walk away. Mind the Glacier dragons and Basilisks."),
                 "the place whole, its nests by kind");
    check::equal(lines.speak(again, "Is it still safe?", {"Keep clear of " + hospice + "."}), whole,
                 "a nest not warned of yet: the whole answer again");
    // A short reminder takes its turn with a long one: nothing in an answer said again is fuller.
    const Lines both = Lines::parse("any | safety_unsafe | Not safe. Keep clear of {worst}.[ Mind the {danger}.]\n"
                                    "any | safety_unsafe_told | Not safe, as I told you.\n"
                                    "any | safety_unsafe_told | Not safe, as I told you. Keep clear of {worst}.\n",
                                    "test");
    check::equal(both.replies(again, {whole}).size(), std::size_t(2), "both told lines, the short one as full as the long");
    // What must have been heard is what the answer says: a bank whose lines never list the other
    // places never said them, and its answer is told all the same.
    const Lines shorter = Lines::parse("any | surroundings | You're in {here}. The nearest is {nearest}.[ Mind the {danger}.]\n"
                                       "any | surroundings_told | As I told you, the nearest is {nearest}.\n",
                                       "test");
    FactPacket around_again = packets::surroundings();
    around_again.facts.back() = packets::memory_of(around_again.speaker, {.met = "again", .times = 1, .heard = 1});
    const std::string short_answer = first(shorter.replies(packets::surroundings()));
    check::is_true(short_answer.find("Past that") == std::string::npos && short_answer.find("Hakndum") == std::string::npos,
                   "the premise: the other places are not said: " + short_answer);
    check::is_true(shorter.speak(around_again, "What's around?", {short_answer}).rfind("As I told you", 0) == 0,
                   "told, though the other places were never listed");
    // With no told line in any bank (a library written before them), the answer is said as ever, the nest
    // heard lately marked as said.
    const Lines older = Lines::parse("any | safety_unsafe | Not safe. Keep clear of {worst}.[ Mind the {danger}.]\n", "test");
    check::equal(older.speak(again, "Is it still safe?", {whole}),
                 "Not safe. Keep clear of " + hospice + ". Mind the Glacier dragons nesting an hour's walk away, as I said.",
                 "no told line anywhere: the answer as ever");
    // What counts is what was said, in any answer: a place warned of is a place told of.
    check::equal(lines.speak(about_the_works(), "Tell me more.", {"Past it, the burnt works, an hour's walk away, held by squatters."}),
                 std::string("That's all I know of it: the burnt works, an hour's walk away, held by squatters."), "told in another answer");
    check::equal(lines.speak(about_the_works(), "Tell me more.", {"The burnt works, an hour's walk away, held by squatters. Keep off."}),
                 std::string("That's all I know of it: the burnt works, an hour's walk away, held by squatters."),
                 "told, though it opened a sentence");
    // Asked twice in one conversation, a line said as told says so itself: no "As I said" before it.
    const std::string around = first(lines.replies(packets::surroundings()));
    FactPacket twice = packets::surroundings_again();
    twice.facts.back() = packets::memory_of(twice.speaker, {.turn = 2, .repeat = true, .heard = 1});
    const std::string said_twice = lines.speak(twice, "What's around, again?", {around});
    check::is_true(said_twice.rfind("The same as I told you: the nearest is ", 0) == 0, "no lead before a told line: " + said_twice);
    // A bank without told lines leaves the answer to the next bank's, never says its own again.
    const Lines banked = Lines::parse("post:gate_captain | safety_unsafe | Not here. Keep off {worst}.[ The {danger}, too.]\n"
                                      "any | safety_unsafe_told | Not safe, as I told you.\n",
                                      "test");
    const std::string own = banked.speak(asked, "Is it safe?");
    check::is_true(own.rfind("Not here.", 0) == 0, "the premise: the gate captain's own answer");
    check::equal(banked.speak(again, "Is it safe?", {own}), std::string("Not safe, as I told you."), "told, from the next bank");
}

TEST(a_line_said_again_with_other_passing_words_is_the_same_line) {
    const Lines lines = Lines::parse("any | greeted_again | Good day. I saw you {since}, and here you are.\n"
                                     "any | greeted_again | Hello again.\n",
                                     "test");
    FactPacket weeks = packets::greeted_again();
    const std::string since_line = "Good day. I saw you some weeks ago, and here you are.";
    // The premise: with nothing said lately, some visit gets the line that says how long since.
    bool said_since = false;
    for (int heard = 0; heard < 2; ++heard) {
        weeks.facts.back() = packets::memory_of(weeks.speaker, {.met = "again", .times = 2, .since = "weeks", .heard = heard});
        said_since = said_since || lines.speak(weeks, "Hello!") == since_line;
    }
    check::is_true(said_since, "the premise: the line that says how long since is said");
    // Said on a visit days ago, with "a few days ago" in it, it is the same line now.
    for (int heard = 0; heard < 2; ++heard) {
        weeks.facts.back() = packets::memory_of(weeks.speaker, {.met = "again", .times = 2, .since = "weeks", .heard = heard});
        check::equal(lines.speak(weeks, "Hello!", {"Good day. I saw you a few days ago, and here you are."}), std::string("Hello again."),
                     "said lately with other words in its blank");
    }
    // A line that shares only its opening words with one said lately is another line.
    const Lines two = Lines::parse("any | greeted_again | Good day. I saw you {since}, and here you are.\n"
                                   "any | greeted_again | Good day. I saw you {since}, and you look well.\n"
                                   "any | greeted_again | Hello again.\n",
                                   "test");
    bool other_said = false;
    for (int heard = 0; heard < 3; ++heard) {
        weeks.facts.back() = packets::memory_of(weeks.speaker, {.met = "again", .times = 2, .since = "weeks", .heard = heard});
        other_said = other_said || two.speak(weeks, "Hello!", {"Good day. I saw you a few days ago, and here you are."}) ==
                                       "Good day. I saw you some weeks ago, and you look well.";
    }
    check::is_true(other_said, "the line with other words after its blank is still said");
}

TEST(small_talk_says_the_weather_or_anything_else_alike) {
    const Lines lines = Lines::parse("any | small_talk_fair | A fine {time}.\n"
                                     "any | small_talk_fair | Clear skies for {season}.[ Mind the {danger}.]\n"
                                     "any | small_talk | Busy, as ever.\n"
                                     "any | small_talk_wet | Wet again.\n",
                                     "test");
    const std::vector<std::string> fair = lines.replies(packets::small_talk("clear", "still"));
    check::equal(fair.size(), std::size_t(3), "the fair weather's two and the rest's one, none of the wet's");
    check::is_true(std::find(fair.begin(), fair.end(), "Busy, as ever.") != fair.end(), "a line not of the weather");
    check::is_true(std::find(fair.begin(), fair.end(), "A fine evening.") != fair.end(), "a nest near silences no line");
    check::is_true(std::find(fair.begin(), fair.end(),
                             "Clear skies for deep winter. Mind the Hippogryphs nesting an hour's walk away.") != fair.end(),
                   "and the one that mentions it");
}

TEST(a_line_that_claims_what_the_speaker_has_no_ground_for_is_not_said) {
    // A farewell is one situation whether the two met before or not, so only the check keeps "good
    // to see you again" from a stranger. The warden's bank has it and a plain one; the next, a plain one.
    const Lines lines = Lines::parse("warden/LN | parted | Good to see you again; farewell.\n"
                                     "warden/LN | parted | Farewell.\n"
                                     "any | parted | Go well.\n",
                                     "test");
    const auto parting = [](bool met, int heard) {
        FactPacket p = packets::parted();
        p.facts.back() = met ? packets::memory_of(p.speaker, {.met = "again", .times = 1, .since = "days", .heard = heard})
                             : packets::memory_of(p.speaker, {.heard = heard});
        return p;
    };
    check::equal(lines.replies(parting(false, 0)).size(), std::size_t(2), "the premise: both of the warden's lines fit a stranger");
    std::set<std::string> to_a_stranger, to_someone_met;
    for (int heard = 0; heard < 4; ++heard) {
        to_a_stranger.insert(lines.speak(parting(false, heard), "Bye."));
        to_someone_met.insert(lines.speak(parting(true, heard), "Bye."));
    }
    check::is_true(to_a_stranger.size() == 1 && to_a_stranger.count("Farewell.") == 1, "never glad to see a stranger again");
    check::is_true(to_someone_met.count("Good to see you again; farewell.") == 1, "the control: glad to see someone met before");
    // A bank with nothing true to say gives way to the next.
    const Lines only = Lines::parse("warden/LN | parted | Good to see you again; farewell.\nany | parted | Go well.\n", "test");
    check::equal(only.speak(parting(false, 0), "Bye."), std::string("Go well."), "the next bank's line");
    check::equal(only.speak(parting(true, 0), "Bye."), std::string("Good to see you again; farewell."), "the control: its own, when true");
    // A saying is quoted, not claimed: rain in a proverb on a clear day reports none.
    FactPacket chat = packets::small_talk("clear", "still");
    chat.persona.people = "human_cold";
    const Lines quoting = Lines::parse("saying:human_cold | saying | rain finds every roof in the end\n"
                                       "any | small_talk_fair | It's a fine {time}[; as we say, {saying}].\n",
                                       "test");
    check::equal(quoting.speak(chat, "Nice day."), std::string("It's a fine evening; as we say, rain finds every roof in the end."),
                 "a saying quoted");
    const Lines claiming = Lines::parse("any | small_talk_fair | It's a fine {time}; rain finds every roof in the end.\n", "test");
    check::equal(claiming.speak(chat, "Nice day."), std::string(), "the control: the line's own rain, on a clear day");
    // The same where the saying opens its sentence, with a capital.
    const Lines opening = Lines::parse("saying:human_cold | saying | rain finds every roof in the end\n"
                                       "any | small_talk_fair | It's a fine {time}.[ {saying}.]\n",
                                       "test");
    check::equal(opening.speak(chat, "Nice day."), std::string("It's a fine evening. Rain finds every roof in the end."),
                 "a saying quoted, opening its sentence");
    // told() says what speak() does: a told line that is untrue is not said, and the answer is not told.
    const Lines told_lines = Lines::parse("any | safety_unsafe | Not safe. Keep clear of {worst}.\n"
                                          "any | safety_unsafe_told | Welcome back; not safe, as I told you.\n",
                                          "test");
    const std::string whole = told_lines.speak(packets::safety_unsafe(), "Is it safe?");
    FactPacket stranger = packets::safety_unsafe();
    stranger.facts.back() = packets::memory_of(stranger.speaker, {.heard = 1});
    FactPacket met = packets::safety_unsafe();
    met.facts.back() = packets::memory_of(met.speaker, {.met = "again", .times = 1, .since = "days", .heard = 1});
    check::is_true(!told_lines.told(stranger, {whole}) && told_lines.speak(stranger, "Is it safe?", {whole}) == whole,
                   "a stranger is not welcomed back: the answer as ever, not told");
    check::is_true(told_lines.told(met, {whole}), "the control: told to someone met before");
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
