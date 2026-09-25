// Written lines, spoken for fact packets. No model: a reply is a line a person wrote, its blanks
// filled from the packet. Every line in data/lines.txt is tried alone against every packet here
// and held to the voice's rules, so a line that breaks one fails by name.
#include "communicator/authored.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "communicator/error.hpp"
#include "packets.hpp"
#include "reply_shape.hpp"
#include "rules.hpp"

namespace {

using communicator::Lines;
using orchestrator::ActKind;
using orchestrator::Fact;
using orchestrator::FactKind;
using orchestrator::FactPacket;
using orchestrator::Outcome;

Fact halbrann() {
    return {FactKind::Person, "hero-pretender-surface-city-0-243-human_cold",
            {{"name", "Halbrann"}, {"role", "pretender"}, {"where", "the ruins of Linbrig"}, {"status", "about"}}};
}

Fact reason(const std::string& why, const std::string& text) { return {FactKind::Reason, "", {{"reason", text}, {"why", why}}}; }

// The situations packets.hpp has no packet for, built from its facts.
FactPacket about_halbrann() { return packets::packet(packets::castellan(), ActKind::AskAboutPerson, Outcome::Answered, {halbrann()}); }

FactPacket about_the_works() {
    return packets::packet(packets::castellan(), ActKind::AskAboutPlace, Outcome::Answered,
                           {{FactKind::Place, "keyloc-smelter_ruin-180",
                             {{"kind", "smelter_ruin"}, {"name", "the burnt works"}, {"family", "extractive"}, {"state", "ruined"},
                              {"threat", "2"}, {"occupant", "squatters"}, {"distance", "3.1 km"}}}});
}

FactPacket no_work() { return packets::packet(packets::castellan(), ActKind::AskForWork, Outcome::Answered); }

FactPacket refused_release() {
    return packets::packet(packets::castellan(), ActKind::AbandonQuest, Outcome::Refused,
                           {reason("not_theirs_to_release", "the work is Halbrann's to release"), halbrann()});
}

FactPacket refused_with_no_giver() {
    return packets::packet(packets::castellan(), ActKind::AcceptQuest, Outcome::Refused,
                           {reason("not_theirs_to_give", "the work is someone else's to give")});
}

FactPacket refused_gone() {
    return packets::packet(packets::castellan(), ActKind::NoAct, Outcome::Refused, {reason("gone", "Selbren is gone")});
}

// Refused by the world, in the director's words for a lapsed offer.
FactPacket refused_by_the_world() {
    return packets::packet(packets::castellan(), ActKind::AcceptQuest, Outcome::Refused,
                           {reason("refused_by_world", "the offer has lapsed")});
}

std::vector<FactPacket> cases() {
    std::vector<FactPacket> out = packets::all();
    for (FactPacket p : {about_halbrann(), about_the_works(), no_work(), refused_release(), refused_with_no_giver(), refused_gone(),
                         refused_by_the_world()})
        out.push_back(std::move(p));
    return out;
}

// The first of `replies`, failing by name when there is none.
std::string first(const std::vector<std::string>& replies) {
    check::is_true(!replies.empty(), "a reply");
    return replies.front();
}

// The rows of the lines file as the test reads them, apart from Lines: every row that is not
// blank or a comment.
std::vector<std::string> rows_of_the_file() {
    std::ifstream in(COMMUNICATOR_LINES, std::ios::binary);
    check::is_true(static_cast<bool>(in), std::string("the lines file at ") + COMMUNICATOR_LINES);
    std::vector<std::string> out;
    std::string row;
    while (std::getline(in, row)) {
        const std::size_t first = row.find_first_not_of(" \t\r");
        if (first == std::string::npos || row[first] == '#') continue;
        out.push_back(row);
    }
    return out;
}

std::string voice_of(const std::string& row) {
    std::string voice = row.substr(0, row.find('|'));
    while (!voice.empty() && voice.back() == ' ') voice.pop_back();
    return voice;
}

// `p` spoken by someone the row's bank answers for: a hero with that voice key, someone at that
// post, or, for "any", the packet's own speaker.
FactPacket as_voiced_by(FactPacket p, const std::string& voice) {
    if (voice.rfind("post:", 0) == 0) {
        p.persona.voice_key.clear();
        p.persona.role = voice.substr(5);
    } else if (voice != "any") {
        p.persona.voice_key = voice;
    }
    return p;
}

// Every rule of the voice's that `reply` breaks, as spoken for `p`.
std::vector<std::string> broken_rules(const std::string& reply, const FactPacket& p) {
    std::vector<std::string> out;
    if (reply.find_first_of("{}") != std::string::npos) out.push_back("a blank left unfilled");
    for (const std::string mark : {"\"", "\xE2\x80\x9C", "\xE2\x80\x9D"})
        if (reply.find(mark) != std::string::npos) out.push_back("a double quote");
    std::vector<std::string> ids = {p.persona.uid};
    for (const Fact& f : p.facts)
        if (!f.subject.empty()) ids.push_back(f.subject);
    for (const std::string id_start : {"hook-", "hero-", "npc-", "keyloc-", "surface-city-", "nest-"}) ids.push_back(id_start);
    for (const std::string& id : ids)
        if (reply.find(id) != std::string::npos) out.push_back("an id: " + id);

    const communicator::ReplyShape shape = communicator::reply_shape(p);
    std::string own = reply;
    if (!shape.closing.empty()) {
        const std::string closing = " " + shape.closing;
        if (own.size() < closing.size() || own.compare(own.size() - closing.size(), closing.size(), closing) != 0)
            out.push_back("not the closing: " + shape.closing);
        else
            own.resize(own.size() - closing.size());
    }
    std::string past_opening = own;
    if (!shape.openings.empty()) {
        const auto opening = std::find_if(shape.openings.begin(), shape.openings.end(),
                                          [&](const std::string& o) { return own.rfind(o, 0) == 0; });
        if (opening == shape.openings.end())
            out.push_back("not an opening of its outcome's");
        else
            past_opening = own.substr(opening->size());  // C++'s own words: "Suit yourself"
    }
    const std::size_t sentences = rules::sentences_in(own);
    if (sentences < 1 || sentences > static_cast<std::size_t>(shape.sentences))
        out.push_back(std::to_string(sentences) + " sentences, of at most " + std::to_string(shape.sentences));
    for (const std::string& name : rules::unlisted_names(past_opening, p)) out.push_back("a capital no packet gave: " + name);
    for (const std::string& stray : rules::stray_capitals(reply, p)) out.push_back("a common capital mid-sentence: " + stray);
    for (const std::string& barred : rules::barred_words(reply, p)) out.push_back("a barred word: " + barred);
    return out;
}

}  // namespace

TEST(the_lines_file_loads_every_row) {
    const Lines lines = Lines::load(COMMUNICATOR_LINES);
    check::equal(lines.size(), rows_of_the_file().size(), "rows read");
    check::is_true(lines.size() > 50, "the premise: a pilot's worth of lines");
}

TEST(every_line_is_spoken_and_keeps_the_voices_rules) {
    // Each row alone, against every packet, as its bank's speaker: a row no packet can speak, or
    // a reply that breaks a rule, fails by the row's words.
    const std::vector<std::string> rows = rows_of_the_file();
    check::is_true(!rows.empty(), "the premise: rows to read");
    for (const std::string& row : rows) {
        const Lines one = Lines::parse(row, "the row");
        std::size_t spoken = 0;
        for (const FactPacket& packet : cases()) {
            const FactPacket p = as_voiced_by(packet, voice_of(row));
            for (const std::string& reply : one.replies(p)) {
                ++spoken;
                const std::vector<std::string> broken = broken_rules(reply, p);
                check::is_true(broken.empty(), "\"" + reply + "\" to keep the rules; it breaks: " + (broken.empty() ? "" : broken.front()));
            }
        }
        check::is_true(spoken > 0, "a packet that speaks the row: " + row);
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
}

TEST(a_line_speaks_only_when_its_packet_fills_every_blank) {
    const Lines lines = Lines::parse("any | surroundings | The nearest is {nearest}. Mind the {danger}.\n"
                                     "any | surroundings | The nearest is {nearest}.\n",
                                     "test");
    // No nest near the inn: only the line without a danger is said.
    const std::vector<std::string> at_the_inn = lines.replies(packets::surroundings_from_the_inn());
    check::equal(at_the_inn.size(), std::size_t(1), "one line fits at the inn");
    check::equal(at_the_inn.front(), std::string("The nearest is the silent keep, 3.1 km off, held by squatters."), "the reply");
    const std::vector<std::string> at_the_gate = lines.replies(packets::surroundings_from_the_gate());
    check::equal(at_the_gate.size(), std::size_t(2), "both fit at the gate");
    check::equal(at_the_gate.front(),
                 std::string("The nearest is the hamlet of Wenweg, 3.1 km off. Mind the Glacier dragons nesting 3.1 km off."),
                 "the reply");
    // Where the speaker stands is not the nearest place, and the worst nest is named.
    const Lines around = Lines::parse("any | surroundings | {nearest}. Past that, {others}. Mind the {danger}.", "test");
    check::equal(first(around.replies(packets::surroundings())),
                 std::string("The ninth stone on the Linbrig road, 2.5 km off, held by monsters. Past that, the hamlet of "
                             "Wenweg, 3.1 km off, and the burnt works, 3.1 km off, held by squatters. Mind the Hippogryphs "
                             "nesting 4.9 km off."),
                 "the castellan's surroundings");
}

TEST(a_refusal_naming_two_people_names_neither_as_the_giver) {
    // Nothing says which of two is meant, so a line that needs the giver is not said.
    const Lines lines = Lines::parse("any | refused_give | No, that's {giver}'s work.\n"
                                     "any | refused_give | No, that work isn't mine to give.\n",
                                     "test");
    check::equal(lines.replies(packets::refused_brokered()).size(), std::size_t(2), "the premise: one person, both lines");
    FactPacket two = packets::refused_brokered();
    two.facts.push_back({FactKind::Person, "hero-harbourmaster-coastal-23-30",
                         {{"name", "Nerbrann"}, {"role", "harbourmaster"}, {"where", "the hamlet of Wenweg (Age 2)"}, {"status", "about"}}});
    const std::vector<std::string> replies = lines.replies(two);
    check::equal(replies.size(), std::size_t(1), "one line fits");
    check::equal(replies.front(), std::string("No, that work isn't mine to give."), "the line without a giver");
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
    check::equal(first(lines.replies(warden_at_the_inn)), std::string("The nearest is the silent keep, 3.1 km off, held by squatters."),
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
                 std::string("Dornar the council wants someone to walk the walls of Torhas with them, and Mekmoh the keeper wants "
                             "someone to bring an offering to the shrine of The Red Field at Narbod."),
                 "others' work");
    check::equal(first(lines.replies(packets::work())), std::string("Hold the fortress above Wenweg with me, fair work, 8.0 km off."),
                 "the speaker's own, in their words");
    // A purpose that gives its reason first is told by what it asks.
    FactPacket dragons = packets::work_at_the_inn();
    dragons.facts = {{FactKind::QuestOffer, "hook-hero-warden-hamlet-12-0",
                      {{"giver", "Aldmer"}, {"giver_role", "reeve"}, {"from_speaker", "no"}, {"verb", "slay"},
                       {"purpose", "Our fields lie in its reach: drive off Forest dragons, Who Ate Linbrig."}, {"difficulty", "4"},
                       {"distance", "6.0 km"}}}};
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
                                  "any | parted | "})
        check::throws([&] { Lines::parse(bad, "test"); }, "refused: " + bad);
    check::equal(Lines::parse("# a comment\n\n  \nany | parted | Farewell.\n", "test").size(), std::size_t(1), "comments and blanks");
    check::equal(Lines::parse("\xEF\xBB\xBF# a comment\nany | parted | Farewell.\r\n", "test").size(), std::size_t(1),
                 "a byte-order mark and a CRLF");
}
