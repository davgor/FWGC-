// The voice against the test model. The grammar is the guarantee: a reply can capitalise only
// the words of its packet and a closed set of common words, so it names nobody and nowhere the
// packet does not. The prompt tests read what the model is handed: the persona, every fact,
// and never an id.
#include "communicator/voice.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "check.hpp"
#include "communicator/error.hpp"
#include "model.hpp"
#include "packets.hpp"
#include "reply_shape.hpp"

namespace {

using communicator::Reply;
using communicator::Voice;
using orchestrator::Fact;
using orchestrator::FactPacket;

// The words of `text`: runs of letters, ASCII or not. Typographic punctuation (curly quotes,
// dashes, the ellipsis: U+2000 to U+207F, bytes E2 80 or E2 81 and one more) parts words, as
// ASCII punctuation does, so "I’m" is "I" and "m".
std::vector<std::string> words_of(const std::string& text) {
    std::vector<std::string> out;
    std::string word;
    const auto flush = [&] {
        if (!word.empty()) out.push_back(word);
        word.clear();
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        const bool punctuation = c == 0xE2 && i + 2 < text.size() && (static_cast<unsigned char>(text[i + 1]) & 0xFE) == 0x80;
        if (punctuation) {
            flush();
            i += 2;
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80) {
            word += text[i];
        } else {
            flush();
        }
    }
    flush();
    return out;
}

// Every word the packet puts in front of the voice as something to say: the speaker's name,
// post, place and line, and every fact's text.
std::set<std::string> packet_words(const FactPacket& p) {
    std::set<std::string> out;
    for (const std::string* text : {&p.persona.name, &p.persona.role, &p.persona.place, &p.persona.line})
        for (const std::string& w : words_of(*text)) out.insert(w);
    for (const Fact& f : p.facts)
        for (const auto& [key, value] : f.fields)
            for (const std::string& w : words_of(value)) out.insert(w);
    return out;
}

// The capitalised words of a reply that are neither the packet's nor common words: names the
// packet does not give.
std::vector<std::string> unlisted_names(const std::string& reply, const FactPacket& p) {
    const std::set<std::string> known = packet_words(p);
    const std::vector<std::string>& common = communicator::common_capitals();
    std::vector<std::string> out;
    for (const std::string& w : words_of(reply)) {
        const auto first = static_cast<unsigned char>(w[0]);
        const bool capital = (first >= 'A' && first <= 'Z') || first >= 0x80;
        if (capital && !known.count(w) && std::find(common.begin(), common.end(), w) == common.end()) out.push_back(w);
    }
    return out;
}

std::string prompt_of(const Voice& voice, const FactPacket& p, const std::string& line) {
    std::string out;
    for (const inference::Message& m : voice.messages(p, line)) out += m.content + "\n";
    return out;
}

bool opens_with_one_of(const std::string& reply, const std::vector<std::string>& openings) {
    return std::any_of(openings.begin(), openings.end(), [&](const std::string& o) { return reply.rfind(o, 0) == 0; });
}

// The sentences in `text`: runs of . ! ? that close it or come before a space or a closing quote.
// The point in "2.5 km" ends nothing.
std::size_t sentences_in(const std::string& text) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '.' && text[i] != '!' && text[i] != '?') continue;
        while (i + 1 < text.size() && (text[i + 1] == '.' || text[i + 1] == '!' || text[i + 1] == '?')) ++i;
        const char next = i + 1 < text.size() ? text[i + 1] : ' ';
        if (next == ' ' || next == '"' || next == '\'' || next == ')') ++count;
    }
    return count;
}

// One packet and line for each outcome.
std::vector<std::pair<FactPacket, std::string>> outcome_cases() {
    return {
        {packets::surroundings(), "What is around here?"},
        {packets::accepted(), "I'll hold the fortress with you."},
        {packets::declined(), "No, not for me."},
        {packets::abandoned(), "I'm giving up the fortress."},
        {packets::unknown_place(), "What do you know of Castle Grey?"},
        {packets::refused_brokered(), "I'll help take back Linbrig."},
        {packets::parted(), "Farewell, and thank you for your time."},
        {packets::not_understood(), "What's the name of the blacksmith here, and the innkeeper?"},
    };
}

}  // namespace

TEST(a_reply_names_only_what_its_packet_names) {
    inference::Model& model = test_model::model();
    Voice voice(model);
    // The premise: asked about a place its packet does not hold, the free model names it, and
    // says what it is like, though the speaker knows nothing of it.
    const FactPacket unknown = packets::unknown_place();
    const std::string asked = "What do you know of Castle Grey?";
    inference::Sampling free_text;
    free_text.max_tokens = 120;
    const std::string free_reply = model.complete(model.chat_prompt(voice.messages(unknown, asked)), free_text).text;
    check::is_true(!unlisted_names(free_reply, unknown).empty(), "the premise: free, the model names what its packet lacks: " + free_reply);
    // Held by the voice's grammar, it cannot, for that line or any packet.
    const Reply held = voice.speak(unknown, asked);
    check::is_true(unlisted_names(held.text, unknown).empty(), "no name the packet lacks: " + held.text);
    const std::vector<std::pair<FactPacket, std::string>> cases = {
        {packets::surroundings(), "What is around here?"},
        {packets::work(), "Is there any work for me?"},
        {packets::about_self(), "Who are you?"},
        {packets::not_understood(), "What's the name of the blacksmith here, and the innkeeper?"},
        {packets::refused_brokered(), "I'll help take back Linbrig."},
        {packets::accepted(), "I'll hold the fortress with you."},
        {packets::parted(), "Farewell."},
    };
    for (const auto& [packet, line] : cases) {
        const Reply r = voice.speak(packet, line);
        check::is_true(!r.text.empty(), "a reply to: " + line);
        const std::vector<std::string> unlisted = unlisted_names(r.text, packet);
        check::is_true(unlisted.empty(), "no name the packet lacks (" + (unlisted.empty() ? "" : unlisted.front()) + ") in: " + r.text);
    }
}

TEST(no_id_reaches_the_voices_prompt) {
    Voice voice(test_model::model());
    for (const FactPacket& p : packets::all()) {
        const std::string seen = prompt_of(voice, p, "Tell me everything.") + voice.grammar(p);
        check::is_true(seen.find(p.persona.uid) == std::string::npos, "no speaker uid: " + p.persona.uid);
        for (const Fact& f : p.facts)
            if (!f.subject.empty()) check::is_true(seen.find(f.subject) == std::string::npos, "no fact id: " + f.subject);
    }
}

TEST(the_prompt_carries_the_persona_and_every_fact_as_it_is_said) {
    Voice voice(test_model::model());
    const FactPacket work = packets::work();
    const std::string prompt = prompt_of(voice, work, "Any work?");
    const orchestrator::Persona& s = work.persona;
    for (const std::string* part : {&s.name, &s.manner, &s.voice, &s.toward_player, &s.line})
        check::is_true(prompt.find(*part) != std::string::npos, "the persona's " + *part);
    for (const std::string& never : s.never) check::is_true(prompt.find(never) != std::string::npos, "never: " + never);
    // Names come as they are said: "Wenweg (Age 2)" is Wenweg.
    for (const std::string said : {"at Wenweg,", "Hold the fortress above Wenweg with me.", "Nerbrann",
                                   "Keep the sea lanes to the harbour of Wenweg open.", "Halbrann", "Help me take back Linbrig.",
                                   "Recover the reliquary of Linbrig from the ruins."})
        check::is_true(prompt.find(said) != std::string::npos, "the persona and the offers, as said: " + said);
    const FactPacket around = packets::surroundings();
    const std::string surroundings = prompt_of(voice, around, "What is around?");
    for (const std::string name : {"the burnt works", "the hamlet of Wenweg:", "Nerstrann:", "Hippogryphs", "Frost owls"})
        check::is_true(surroundings.find(name) != std::string::npos, "the place or danger " + name);
    for (const FactPacket& p : packets::all())
        check::is_true(prompt_of(voice, p, "Hello.").find("(Age") == std::string::npos, "no bracket said aloud, for " + p.persona.name);
    const FactPacket refused = packets::refused_brokered();
    check::is_true(prompt_of(voice, refused, "I'll take it.").find(*refused.facts.front().field("reason")) != std::string::npos,
                   "a refusal's reason");
    // A roster person has no card: the prompt stands on name, post and place.
    const std::string gate = prompt_of(voice, packets::not_understood(), "Hello.");
    check::is_true(gate.find("Fersel, gate captain, at the fortress of Wenweg,") != std::string::npos,
                   "the post as words, not a key: " + gate.substr(0, 80));
}

TEST(a_reply_opens_by_saying_what_happened) {
    inference::Model& model = test_model::model();
    Voice voice(model);
    // The premise: free, a speaker who knows nothing of a place claims to know it.
    const FactPacket unknown = packets::unknown_place();
    const std::string asked = "What do you know of Castle Grey?";
    inference::Sampling free_text;
    free_text.max_tokens = 120;
    const std::string free_reply = model.complete(model.chat_prompt(voice.messages(unknown, asked)), free_text).text;
    check::is_true(!opens_with_one_of(free_reply, communicator::reply_shape(unknown).openings),
                   "the premise: free, the model does not open by saying it does not know: " + free_reply);
    // Held, each outcome's reply opens with that outcome's words. Every outcome but an answer
    // has such words: what happened is said first.
    for (const auto& [packet, line] : outcome_cases()) {
        const communicator::ReplyShape shape = communicator::reply_shape(packet);
        const std::string outcome(orchestrator::to_string(packet.outcome));
        check::equal(shape.openings.empty(), packet.outcome == orchestrator::Outcome::Answered, "opening words for " + outcome);
        if (shape.openings.empty()) continue;
        const Reply r = voice.speak(packet, line);
        check::is_true(opens_with_one_of(r.text, shape.openings),
                       std::string(orchestrator::to_string(packet.outcome)) + " opens as it should: " + r.text);
    }
}

TEST(a_short_answer_stays_one_sentence) {
    inference::Model& model = test_model::model();
    Voice voice(model);
    // The premise: free, the model runs past one sentence for at least one of these.
    std::size_t longest_free = 0;
    std::string longest_reply;
    inference::Sampling free_text;
    free_text.max_tokens = 120;
    for (const auto& [packet, line] : outcome_cases()) {
        if (communicator::reply_shape(packet).sentences != 1) continue;
        const std::string reply = model.complete(model.chat_prompt(voice.messages(packet, line)), free_text).text;
        if (sentences_in(reply) > longest_free) {
            longest_free = sentences_in(reply);
            longest_reply = reply;
        }
    }
    check::is_true(longest_free > 1, "the premise: free, some reply runs on: " + longest_reply);
    // Held, the model says one sentence where there is little to say: not knowing, refusing,
    // declining, giving up, parting, not following. A refusal's closing is C++'s, and comes after.
    using orchestrator::Outcome;
    const std::set<Outcome> short_ones = {Outcome::Unknown, Outcome::Refused, Outcome::Declined,
                                          Outcome::Abandoned, Outcome::Parted, Outcome::NotUnderstood};
    for (const auto& [packet, line] : outcome_cases()) {
        const communicator::ReplyShape shape = communicator::reply_shape(packet);
        check::equal(shape.sentences == 1, short_ones.count(packet.outcome) == 1,
                     "one sentence for " + std::string(orchestrator::to_string(packet.outcome)));
        if (shape.sentences != 1) continue;
        std::string said = voice.speak(packet, line).text;
        if (!shape.closing.empty() && said.size() > shape.closing.size())
            said = said.substr(0, said.size() - shape.closing.size());
        check::equal(sentences_in(said), std::size_t{1},
                     std::string(orchestrator::to_string(packet.outcome)) + " in one sentence: " + said);
    }
}

TEST(a_refusal_says_who_to_see_in_words_it_cannot_change) {
    Voice voice(test_model::model());
    const std::string reply = voice.speak(packets::refused_brokered(), "I'll take the work.").text;
    const std::string closing = " Speak to Halbrann, at the ruins of Linbrig.";
    check::is_true(reply.size() > closing.size() && reply.compare(reply.size() - closing.size(), closing.size(), closing) == 0,
                   "the closing, word for word: " + reply);
}

TEST(a_reply_streams_in_whole_pieces_and_stops_when_asked) {
    Voice voice(test_model::model());
    std::string streamed;
    int pieces = 0;
    const Reply whole = voice.speak(packets::surroundings(), "What is around here?", [&](std::string_view piece) {
        streamed += piece;
        ++pieces;
        return true;
    });
    check::equal(streamed, whole.text, "the pieces are the reply");
    check::is_true(pieces > 1, "more than one piece");
    int taken = 0;
    const Reply stopped = voice.speak(packets::surroundings(), "What is around here?", [&](std::string_view) { return ++taken < 3; });
    check::is_true(stopped.stopped_by_caller, "stopped by the caller");
    check::equal(taken, 3, "no piece after the refusal");
}

TEST(the_same_packet_and_line_give_the_same_reply) {
    Voice voice(test_model::model());
    check::equal(voice.speak(packets::parted(), "Farewell.").text, voice.speak(packets::parted(), "Farewell.").text, "greedy");
}

TEST(what_the_voice_cannot_read_is_refused) {
    Voice voice(test_model::model());
    FactPacket later = packets::parted();
    later.contract_version = orchestrator::kContractVersion + 1;
    check::throws([&] { (void)voice.speak(later, "Farewell."); }, "a packet of another contract version");
    check::throws([&] { (void)voice.speak(packets::parted(), std::string(communicator::kMaxLineBytes + 1, 'a')); },
                  "a line past the limit");
    check::throws([] { Voice(test_model::model(), {0, 0.0f, 0}); }, "no tokens to speak with");
    check::throws([] { Voice(test_model::model(), {64, -1.0f, 0}); }, "a negative temperature");
}
