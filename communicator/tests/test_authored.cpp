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
    // A saying is dropped in too: left out, "Go well." is sound; said, a second sentence is not.
    check::is_true(row_problems("any | parted | Go well[; as we say, {saying}].").empty(), "a saying that continues the sentence");
    check::is_true(!row_problems("any | parted | Go well.[ As we say, {saying}.]").empty(),
                   "a farewell of one sentence may not add a second, even an optional one");
    check::is_true(!row_problems("any | parted | Go well[; as we say, {saying}], and good luck.").empty(), "a saying ends its sentence");
    check::is_true(row_problems("any | small_talk | Much as ever.[ As we say, {saying}.]").empty(), "its stop inside the part");
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
        {packets::small_talk("clear", "breezy"), {"small_talk_fair", "small_talk"}},
        {packets::small_talk("unsettled", "windy"), {"small_talk_wet", "small_talk"}},
        {packets::small_talk("stormy", "a gale"), {"small_talk_storm", "small_talk"}},
        {packets::small_talk("fair", "a gale"), {"small_talk_storm", "small_talk"}},
        {packets::small_talk("fair", "still", "hostile"), {"small_talk_cold"}},
        {packets::parted(), {"parted"}},
        {packets::parted_coldly(), {"parted_cold"}},
        {packets::acknowledged(), {"acknowledged"}},
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
    keyless.facts.back() = packets::memory_of(keyless.speaker, {.met = "again", .times = 1, .mood = "warm", .work = "finished"});
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
    // The same for the season and the time of day in small talk.
    const Lines talk = Lines::parse("any | small_talk_fair | A fine day for {season}.\nany | small_talk_fair | A fine day.\n", "test");
    check::equal(talk.replies(packets::small_talk("clear", "still")).size(), std::size_t(2), "the season silences no line");
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
    const Lines lines = Lines::parse("any | small_talk_fair | Clear skies.[ Mind the {danger}.]\n", "test");
    const FactPacket chat = packets::small_talk("clear", "still");
    const std::string warned = "Clear skies. Mind the Hippogryphs nesting an hour's walk away.";
    check::equal(lines.speak(chat, "Nice day."), warned, "the nest mentioned, the first time");
    check::equal(lines.speak(chat, "Nice day.", {"Not safe. There are Hippogryphs nesting an hour's walk away."}), std::string("Clear skies."),
                 "not again after a warning lately");
    check::equal(lines.speak(chat, "Nice day.", {"Fine weather."}), warned, "mentioned when nothing said lately named it");
    // An answer about what is near says it whatever was said before.
    const Lines around = Lines::parse("any | surroundings | The nearest is {nearest}.[ Mind the {danger}.]\n", "test");
    const std::string answer = around.speak(packets::surroundings(), "What's around?", {warned});
    check::is_true(answer.find("Mind the") != std::string::npos, "the answer keeps its warning: " + answer);
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
