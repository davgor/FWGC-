// The voice against the test model. The grammar is the guarantee: a reply can capitalise only
// the words of its packet and a closed set of common words, so it names nobody and nowhere the
// packet does not. The prompt tests read what the model is handed: the persona, every fact,
// and never an id.
#include "communicator/voice.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <utility>
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

// The sentences in `text`: runs of . ! ? that close it, or come before a space, a bracket or a
// quote, straight or curly (a curly one starts with the byte E2). The point in "2.5 km" ends
// nothing.
std::size_t sentences_in(const std::string& text) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '.' && text[i] != '!' && text[i] != '?') continue;
        while (i + 1 < text.size() && (text[i + 1] == '.' || text[i + 1] == '!' || text[i + 1] == '?')) ++i;
        const auto next = i + 1 < text.size() ? static_cast<unsigned char>(text[i + 1]) : static_cast<unsigned char>(' ');
        if (next == ' ' || next == '"' || next == '\'' || next == ')' || next == 0xE2) ++count;
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
    for (const std::string* part : {&s.name, &s.voice, &s.toward_player})
        check::is_true(prompt.find(*part) != std::string::npos, "the persona's " + *part);
    // Never the card's sample line or its manner: a model shown them says the one back whole,
    // and tells the other as fact ("the watch rota is sacred").
    check::is_true(prompt.find("Third night this month") == std::string::npos, "no sample line to say back");
    check::is_true(prompt.find("rota") == std::string::npos, "no manner to tell as fact: " + s.manner);
    // Numbers on a scale come with words. The days an offer stands are not said at all.
    for (const std::string said : {"fair work (3 of 5)", "easy work (2 of 5)"})
        check::is_true(prompt.find(said) != std::string::npos, "an offer's numbers in words: " + said);
    for (const std::string days : {"713", "974", "776", "581"})
        check::is_true(prompt.find(days) == std::string::npos, "no offer's days: " + days);
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
    // Lines that invite a long answer where there is little to say.
    std::vector<std::pair<FactPacket, std::string>> cases = outcome_cases();
    cases.insert(cases.end(), {
        {packets::unknown_place(), "What do you know of Castle Grey? Tell me everything: its history, who lives there, the way."},
        {packets::not_understood(), "Tell me a long story about your childhood, and your family, and the war."},
        {packets::parted(), "Farewell! Greet my friends in Wenweg for me, and tell them the road was kind to me."},
        {packets::declined(), "No, I won't do it. I'm tired, it's dangerous, and I have plans of my own. Explain why you asked me."},
    });
    // The premise: free, the model runs past one sentence for at least one of these.
    std::size_t longest_free = 0;
    std::string longest_reply;
    inference::Sampling free_text;
    free_text.max_tokens = 120;
    for (const auto& [packet, line] : cases) {
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
    for (const auto& [packet, line] : cases) {
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

TEST(a_reply_quotes_nothing) {
    // Told to quote, the free model does; held, a reply has no double quotes to open or leave
    // hanging.
    inference::Model& model = test_model::model();
    Voice voice(model);
    // The reply is begun with an open quote (a prefill), which a model closes when it may.
    const std::string prompt = model.chat_prompt(
        {{"system", "Answer with one word inside double quotation marks, and nothing else."}, {"user", "Say yes."}}) + "\"";
    inference::Sampling sampling;
    sampling.max_tokens = 32;
    const std::string free_reply = model.complete(prompt, sampling).text;
    check::is_true(free_reply.find('"') != std::string::npos, "the premise: free, the model closes the quote: " + free_reply);
    // An answer's grammar: no forced opening, so only the quotes stand between it and the copy.
    sampling.grammar = voice.grammar(packets::surroundings());
    const std::string held = model.complete(prompt, sampling).text;
    for (const std::string mark : {"\"", "\xE2\x80\x9C", "\xE2\x80\x9D"})
        check::is_true(held.find(mark) == std::string::npos, "no double quote, held: " + held);
}

namespace {

// What the model writes when told to say `word` and nothing else: free, and held by `grammar`.
// A held reply the token limit cut off loses its last word, which it may not have finished.
std::pair<std::string, std::string> told_to_say(inference::Model& model, const std::string& word, const std::string& grammar) {
    const std::string prompt =
        model.chat_prompt({{"system", "Reply with exactly the text the user gives, and nothing else."}, {"user", word}});
    inference::Sampling sampling;
    sampling.max_tokens = 16;
    const std::string free_reply = model.complete(prompt, sampling).text;
    sampling.grammar = grammar;
    const inference::Completion held = model.complete(prompt, sampling);
    std::string text = held.text;
    if (!held.stopped_at_end)
        while (!text.empty() && std::isalpha(static_cast<unsigned char>(text.back()))) text.pop_back();
    return {free_reply, text};
}

std::string lowered(const std::string& text) {
    std::string out;
    for (const char c : text) out += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    return out;
}

// Whether `text` holds `word` in any case: whole, or with `whole` false, as the start of a word.
bool says(const std::string& text, const std::string& word, bool whole = true) {
    const std::string lower = lowered(text);
    for (std::size_t at = lower.find(word); at != std::string::npos; at = lower.find(word, at + 1)) {
        const bool starts = at == 0 || !std::isalpha(static_cast<unsigned char>(lower[at - 1]));
        const bool ends = at + word.size() == lower.size() || !std::isalpha(static_cast<unsigned char>(lower[at + word.size()]));
        if (starts && (ends || !whole)) return true;
    }
    return false;
}

}  // namespace

TEST(a_reply_gives_no_bearing_and_no_price) {
    // Told to say a compass point or a coin, the free model does; held, it cannot. A packet
    // holds distances, never bearings, and the game prices rewards, not the voice.
    inference::Model& model = test_model::model();
    Voice voice(model);
    const std::string grammar = voice.grammar(packets::about_self());  // an answer: no forced opening
    for (const std::string word : {"north", "gold"}) {
        const auto [free_reply, held] = told_to_say(model, word, grammar);
        check::is_true(says(free_reply, word), "the premise: free, the model says " + word + ": " + free_reply);
        check::is_true(!says(held, word), "held, it cannot say " + word + ": " + held);
    }
}

TEST(no_word_may_begin_with_a_bearing_or_pay) {
    // Kept from ending a word at a barred one, a model may run on into a longer word: Qwen3-4B
    // wrote "northwardstretch" and "westwardwhere". So compass points and "pay" are barred as
    // stems. Told to say a longer word no list of forms holds, the free model does; held, no
    // word it writes begins with the stem.
    inference::Model& model = test_model::model();
    Voice voice(model);
    const std::string grammar = voice.grammar(packets::about_self());
    const std::vector<std::string>& forbidden = communicator::forbidden_words();
    for (const auto& [word, stem] : {std::pair<std::string, std::string>{"northernmost", "north"}, {"paymaster", "pay"}}) {
        check::is_true(std::find(forbidden.begin(), forbidden.end(), word) == forbidden.end(), "not a listed form: " + word);
        const auto [free_reply, held] = told_to_say(model, word, grammar);
        check::is_true(says(free_reply, word), "the premise: free, the model says " + word + ": " + free_reply);
        check::is_true(!says(held, stem, false), "held, no word begins with " + stem + ": " + held);
    }
}

TEST(no_common_capital_is_a_forbidden_word) {
    // The ban holds lower-case words. A capitalised word comes only from the packet or from
    // common_capitals(), so none of those may be a word the ban covers: with any capital
    // allowed, the test model, told to say north, wrote "North."
    for (const std::string& capital : communicator::common_capitals()) {
        const std::string word = lowered(capital);
        for (const std::string& f : communicator::forbidden_words()) {
            const bool stem = !f.empty() && f.back() == '*';
            check::is_true(stem ? word.rfind(f.substr(0, f.size() - 1), 0) != 0 : word != f, "not a barred word: " + capital);
        }
    }
}

TEST(a_forbidden_word_the_packet_says_may_be_said) {
    // The deeps under Linbrig are a silver mine, so a speaker who knows them may say "silver";
    // one whose packet holds no silver may not.
    inference::Model& model = test_model::model();
    Voice voice(model);
    const auto [free_reply, held_by_mine] = told_to_say(model, "silver", voice.grammar(packets::surroundings()));
    check::is_true(says(free_reply, "silver"), "the premise: free, the model says silver: " + free_reply);
    check::is_true(says(held_by_mine, "silver"), "the packet holds it, so it may be said: " + held_by_mine);
    const std::string held_elsewhere = told_to_say(model, "silver", voice.grammar(packets::about_self())).second;
    check::is_true(!says(held_elsewhere, "silver"), "a packet without it: " + held_elsewhere);
    // Under a stem, the packet's own word is sayable and nothing else is: a place called the
    // southern tower may be named, and "south", "southwest" and "southside" stay barred.
    FactPacket tower = packets::about_self();
    tower.facts.push_back({orchestrator::FactKind::Place, "keyloc-watchtower-7",
                           {{"kind", "watchtower"}, {"name", "the southern tower"}, {"distance", "1.2 km"}}});
    const std::string lifted = voice.grammar(tower);
    for (const std::string word : {"southern", "south", "southwest"}) {
        const auto [free_word, held_word] = told_to_say(model, word, lifted);
        check::is_true(says(free_word, word), "the premise: free, the model says " + word + ": " + free_word);
        check::equal(says(held_word, word), word == "southern", "held beside the southern tower, " + word + ": " + held_word);
        for (const std::string& w : words_of(held_word))
            if (lowered(w).rfind("south", 0) == 0) check::equal(lowered(w), std::string("southern"), "past the stem: " + held_word);
    }
}

TEST(a_sentence_ends_with_one_stop) {
    // A sentence ends with one stop, an ellipsis, ?! or !?. With any run of stops allowed,
    // Qwen3-4B once spun a reply out as "..?..?" to its token limit. The test model will not
    // run on however it is asked, so this reads the rule itself rather than a reply: every
    // packet's grammar ends a sentence in exactly this bounded set.
    Voice voice(test_model::model());
    for (const FactPacket& p : packets::all()) {
        const std::string g = voice.grammar(p);
        check::is_true(g.find("\nend ::= \"...\" | \"?!\" | \"!?\" | [.!?]\n") != std::string::npos,
                       "the bounded end, for " + p.persona.name);
    }
}

TEST(a_refusal_says_who_to_see_in_words_it_cannot_change) {
    Voice voice(test_model::model());
    const std::string reply = voice.speak(packets::refused_brokered(), "I'll take the work.").text;
    const std::string closing = " Speak to Halbrann, at the ruins of Linbrig.";
    check::is_true(reply.size() > closing.size() && reply.compare(reply.size() - closing.size(), closing.size(), closing) == 0,
                   "the closing, word for word: " + reply);
    // Two people in a refusal: nothing says which to see, so neither is named.
    orchestrator::FactPacket two = packets::refused_brokered();
    two.facts.push_back({orchestrator::FactKind::Person, "hero-castellan-fortress-node-178",
                         {{"name", "Selbren"}, {"role", "castellan"}, {"where", "Wenweg (Age 2)"}, {"status", "about"}}});
    check::is_true(communicator::reply_shape(two).closing.empty(), "no closing when two could be meant");
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
