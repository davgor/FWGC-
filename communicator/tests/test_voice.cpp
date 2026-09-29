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
#include "rules.hpp"

namespace {

using namespace rules;
using communicator::Reply;
using communicator::Voice;
using orchestrator::Fact;
using orchestrator::FactPacket;

std::string prompt_of(const Voice& voice, const FactPacket& p, const std::string& line) {
    std::string out;
    for (const inference::Message& m : voice.messages(p, line)) out += m.content + "\n";
    return out;
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
        // Contract version 4: conversation.
        {packets::greeted_first(), "Hello there!"},
        {packets::greeted_coldly(), "Hello again."},
        {packets::small_talk("clear", "breezy"), "Fine weather today."},
        {packets::acknowledged(), "To the coast, by the high road."},
        {packets::thanked(), "Thank you kindly."},
        {packets::offended(false), "You're a useless old fool."},
        {packets::safety_unsafe(), "Is it safe out there?"},
        {packets::exposed_mask(), "I know you're no midwife."},
        {packets::denied(false), "You're not what you seem."},
        // Contract version 18: a complaint heard out.
        {packets::heard(), "That job was far worse than you let on."},
        // Contract version 24: the stranger's name.
        {packets::introduced(), "I'm Aldric."},
        {packets::known_name(), "I'm Aldric."},
        {packets::renamed(), "Call me Brenna."},
    };
}

// Whether a reply may start any way: an answer, but for "is it safe", and small talk.
bool starts_freely(const FactPacket& p) {
    return (p.outcome == orchestrator::Outcome::Answered && p.answering != orchestrator::ActKind::AskSafety) ||
           p.outcome == orchestrator::Outcome::Chatted;
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
    check::is_true(stray_capitals(held.text, unknown).empty(), "no common capital made a name: " + held.text);
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
        const std::vector<std::string> stray = stray_capitals(r.text, packet);
        check::is_true(stray.empty(), "no common capital mid-sentence (" + (stray.empty() ? "" : stray.front()) + ") in: " + r.text);
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

TEST(markers_are_for_the_game_and_never_reach_a_prompt_or_a_grammar) {
    // Work carries its exact map marker for the game. No voice may see it: the prompt and the
    // grammar are the same with it and without, even when a marker's id holds a barred word, so
    // "silver" from a mine's id cannot become sayable.
    Voice voice(test_model::model());
    const auto without_markers = [](FactPacket p) {
        for (Fact& f : p.facts)
            f.fields.erase(std::remove_if(f.fields.begin(), f.fields.end(),
                                          [](const auto& field) { return field.first == "marker" || field.first == "marker_node"; }),
                           f.fields.end());
        return p;
    };
    FactPacket work = packets::work();
    work.facts.front().fields.emplace_back("marker", "keyloc-silver_mine-211");
    std::size_t markers = 0;
    for (const FactPacket& p : {work, packets::accepted()}) {
        for (const Fact& f : p.facts) markers += f.field("marker_node") != nullptr;
        const FactPacket bare = without_markers(p);
        check::equal(prompt_of(voice, p, "Any work?"), prompt_of(voice, bare, "Any work?"), "the same prompt");
        check::is_true(voice.grammar(p) == voice.grammar(bare), "the same grammar");
    }
    check::is_true(markers >= 2, "the premise: markers to keep out");
}

TEST(how_far_is_said_in_words_never_in_numbers) {
    // A packet says how far only roughly, and the prompt in words: no number to read out, or to
    // round wrong ("thirteen kilometres" for 13.6; judged 2026-09-24).
    Voice voice(test_model::model());
    const std::string around = prompt_of(voice, packets::surroundings(), "What is around?");
    for (const std::string said : {"right here", "a short walk away", "an hour's walk away", "a long walk away"})
        check::is_true(around.find(said) != std::string::npos, "how far, as said: " + said);
    for (const FactPacket& p : packets::all()) {
        const std::string prompt = prompt_of(voice, p, "How far?");
        check::is_true(prompt.find(" km") == std::string::npos, "no kilometres for " + p.persona.name);
    }
}

TEST(the_prompt_carries_the_persona_and_every_fact_as_it_is_said) {
    Voice voice(test_model::model());
    const FactPacket work = packets::work();
    const std::string prompt = prompt_of(voice, work, "Any work?");
    const orchestrator::Persona& s = work.persona;
    for (const std::string* part : {&s.name, &s.toward_player})
        check::is_true(prompt.find(*part) != std::string::npos, "the persona's " + *part);
    // Never the card's sample line, manner or voice line: a model shown them says the first back
    // whole, and tells the others as fact ("the watch rota is sacred", oaths sworn).
    check::is_true(prompt.find("Third night this month") == std::string::npos, "no sample line to say back");
    check::is_true(prompt.find("rota") == std::string::npos, "no manner to tell as fact: " + s.manner);
    check::is_true(prompt.find("oaths") == std::string::npos, "no voice line to tell as fact: " + s.voice);
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

// The conversation so far, as the turns it was: never shown what it had said, the voice said its
// introduction again to every question about itself (the hero benchmark, 2026-09-28). With none, the
// messages are as they always were; with some, the last kExchangesShown before the stranger's line,
// and the speaker told to say nothing again.
TEST(the_voice_is_shown_the_conversation_so_far) {
    using communicator::Exchange;
    const FactPacket p = packets::about_them();
    const std::vector<inference::Message> alone = Voice::messages(p, "How long have you held it?");
    check::equal(alone.size(), std::size_t{2}, "no conversation: the system and the stranger's line");
    check::is_true(alone.front().role == "system" && alone.back().role == "user" && alone.back().content == "How long have you held it?",
                   "the premise: as they always were");
    const std::vector<Exchange> so_far = {{"Hail.", "Well met. I'm Selbren."}, {"Tell me about yourself.", "I hold the fortress."}};
    const std::vector<inference::Message> with = Voice::messages(p, "How long have you held it?", so_far);
    check::equal(with.size(), std::size_t{6}, "the system, two turns each way, and the stranger's line");
    check::is_true(with[1].role == "user" && with[1].content == "Hail." && with[2].role == "assistant" &&
                       with[2].content == "Well met. I'm Selbren." && with[3].content == "Tell me about yourself." &&
                       with[4].content == "I hold the fortress." && with[5].role == "user" && with[5].content == "How long have you held it?",
                   "the turns as they were, oldest first, the stranger's line last");
    check::equal(with.front().content.substr(0, alone.front().content.size()), alone.front().content, "the same system text before it");
    check::is_true(with.front().content.find("say nothing you said there again") != std::string::npos, "told not to say it again");
    std::vector<Exchange> long_talk;
    for (std::size_t i = 0; i < communicator::kExchangesShown + 3; ++i)
        long_talk.push_back({"Line " + std::to_string(i) + ".", "Reply " + std::to_string(i) + "."});
    const std::vector<inference::Message> bounded = Voice::messages(p, "And now?", long_talk);
    check::equal(bounded.size(), 2 + 2 * communicator::kExchangesShown, "only the last few turns");
    check::equal(bounded[1].content, std::string("Line 3."), "the latest of them, the oldest dropped");
}

TEST(the_prompt_says_what_the_speaker_remembers_and_the_new_facts_in_words) {
    Voice voice(test_model::model());
    const std::string first = prompt_of(voice, packets::greeted_first(), "Hello!");
    check::is_true(first.find("You have not met this stranger before.") != std::string::npos, "a first meeting");
    const std::string again = prompt_of(voice, packets::greeted_again(), "Hello again!");
    for (const std::string said : {"You have met this stranger before, last a few days ago.", "Last time they asked you about the burnt works."})
        check::is_true(again.find(said) != std::string::npos, "remembered: " + said);
    check::is_true(prompt_of(voice, packets::greeted_after("finished", "warm"), "Hello!").find("They finished the work you gave them.") !=
                       std::string::npos,
                   "work finished, remembered");
    check::is_true(prompt_of(voice, packets::surroundings_again(), "What's around?").find("They have asked you this already") !=
                       std::string::npos,
                   "a question asked twice");
    // A fact is said in words, never as the packet's keys.
    for (const FactPacket& p : packets::conversation()) {
        const std::string prompt = prompt_of(voice, p, "Well?");
        for (const std::string key : {"met:", "times:", "asked_before", "verdict", "repeat:", "exposed:", "mood:", "feeling:", "resting:"})
            check::is_true(prompt.find(key) == std::string::npos, "no key " + key + " in the prompt for " + p.persona.name);
    }
    // Fame (contract version 25): the speaker's own sovereign, and what they are known for, said of them.
    const std::string sovereign = prompt_of(voice, packets::about_the_sovereign(), "Who is Aldomir?");
    check::is_true(sovereign.find("legendary; your own sovereign") != std::string::npos, "their fame, and theirs: " + sovereign);
    check::is_true(sovereign.find("What Aldomir is known for: Aldomir won a war between realms against Linbrig, told all through these parts.") !=
                       std::string::npos,
                   "the deed, said of them");
    check::is_true(sovereign.find("your own past") == std::string::npos, "never as the speaker's own");
    // The stranger's name (contract version 24): known, and how; told it, what just happened says it.
    const std::string by_name = prompt_of(voice, packets::greeted_again_named(), "Hello.");
    check::is_true(by_name.find("You know the stranger as Aldric.") != std::string::npos, "the name they know: " + by_name);
    check::is_true(prompt_of(voice, packets::greeted_first_named(), "Hello.").find("You know the stranger as Aldric (you heard it with word "
                                                                                   "of what they did).") != std::string::npos,
                   "by word");
    check::is_true(prompt_of(voice, packets::greeted_again(), "Hello.").find("You know the stranger as") == std::string::npos,
                   "no name, none said");
    const std::string introduced = prompt_of(voice, packets::introduced(), "I'm Aldric.");
    check::is_true(introduced.find("The stranger told you their name: Aldric.") != std::string::npos && introduced.find("You know the stranger as") == std::string::npos,
                   "told it: what just happened says it, once");
    check::is_true(prompt_of(voice, packets::renamed(), "Call me Brenna.").find("their name is Brenna, but you knew them as Aldric") !=
                       std::string::npos,
                   "another: and the one they had");
    const std::string chat = prompt_of(voice, packets::small_talk("stormy", "a gale"), "Foul weather.");
    check::is_true(chat.find("weather: stormy; wind: a gale; season: deep winter; time: evening") != std::string::npos,
                   "the weather here and now");
    check::is_true(prompt_of(voice, packets::safety_unsafe(), "Is it safe?").find("Your judgement: it is not safe here.") !=
                       std::string::npos,
                   "the verdict");
    check::is_true(prompt_of(voice, packets::exposed_mask(), "I know what you are.")
                           .find("seen through your mask: you are a magister, not a midwife") != std::string::npos,
                   "what the mask hid");
    check::is_true(prompt_of(voice, packets::exposed_beast(), "I saw you.").find("seen you unleashed") != std::string::npos,
                   "what the beast owns to");
    // Contract version 8: an apology, and what else is known of a place, a nest around it as far
    // from it rather than from the speaker.
    check::is_true(prompt_of(voice, packets::forgiven("neutral"), "Sorry.").find("said sorry for a wrong they did you, and you forgive it") !=
                       std::string::npos,
                   "a wrong forgiven");
    check::is_true(prompt_of(voice, packets::unwronged(), "Sorry.").find("they have done you no wrong") != std::string::npos,
                   "nothing to forgive");
    // Contract version 9: how the speaker feels, and calming words.
    check::is_true(prompt_of(voice, packets::frightened(orchestrator::ActKind::Greet, orchestrator::Outcome::Greeted, "panicked"), "Hello?").find("You are panicking") !=
                       std::string::npos,
                   "panicking");
    check::is_true(prompt_of(voice, packets::frightened(orchestrator::ActKind::Greet, orchestrator::Outcome::Greeted, "afraid"), "Hello?").find("You are afraid") !=
                       std::string::npos,
                   "afraid");
    for (const std::string said : {"You are uneasy", "You are afraid", "You are panicking"})
        check::is_true(prompt_of(voice, packets::greeted_first(), "Hello!").find(said) == std::string::npos, "calm: nothing said of it");
    check::is_true(prompt_of(voice, packets::calmed("afraid"), "Easy.").find("ease your fear a little") != std::string::npos, "eased");
    check::is_true(prompt_of(voice, packets::still_afraid(), "Easy.").find("you are still afraid") != std::string::npos, "not eased");
    check::is_true(prompt_of(voice, packets::not_afraid(), "Easy.").find("you are not afraid") != std::string::npos, "nothing to ease");
    // Contract version 10: eased lately, and joining unasked.
    check::is_true(prompt_of(voice, packets::not_afraid_eased(), "Easy.").find("talked you down from your fear lately") !=
                       std::string::npos,
                   "eased lately");
    check::is_true(prompt_of(voice, packets::not_afraid(), "Easy.").find("talked you down") == std::string::npos, "the control");
    // Eased by this reply's calming words, it is what just happened: "lately" would claim an easing before it.
    check::is_true(prompt_of(voice, packets::calmed("afraid"), "Easy.").find("talked you down") == std::string::npos,
                   "eased just now: not a memory");
    const std::string joined = prompt_of(voice, packets::joined_surroundings(), "What's around?");
    check::is_true(joined.find("came over unasked") != std::string::npos && joined.find("Say first that you overheard") != std::string::npos,
                   "joined: said first");
    check::is_true(prompt_of(voice, packets::surroundings(), "What's around?").find("came over unasked") == std::string::npos,
                   "the control");
    // Contract version 12: what the speaker recalls of the stranger, the stranger's words told of
    // them, and a telling before said first.
    const std::string remembering = prompt_of(voice, packets::greeted_remembering(), "Hello again!");
    check::is_true(remembering.find("Something they once told you of themselves: their sister keeps the mill at the ford.") !=
                       std::string::npos,
                   "their words, told of them");
    check::is_true(prompt_of(voice, packets::greeted_again(), "Hello again!").find("once told you") == std::string::npos, "the control");
    const std::string told = prompt_of(voice, packets::about_the_town_told(), "Tell me of Wenweg.");
    for (const std::string said : {"You told them of it before, some weeks ago.", "They have asked you of this before.",
                                   "Say you told them of it before, then tell them what you know of it"})
        check::is_true(told.find(said) != std::string::npos, "recalled: " + said);
    check::is_true(prompt_of(voice, packets::about_the_town_told("today"), "Tell me of Wenweg.").find("Say you told them") ==
                       std::string::npos,
                   "told today: nothing to say of it");
    const std::string town = prompt_of(voice, packets::about_the_town(), "Tell me more.");
    for (const std::string said : {"trades: administrator, smith, officiant", "- Tornar:", "- Danger near that place: Frost owls nesting: ",
                                   "; close by, from that place"})
        check::is_true(town.find(said) != std::string::npos, "what else is known of the place: " + said);
    // Contract version 13: asked about themself, the speaker's own story in C++'s words, and what to do
    // with it.
    const std::string them = prompt_of(voice, packets::about_them(), "What brought you here?");
    for (const std::string said : {"- Yourself: you are known as the Shield of Wenweg; your home is Wenweg; you command the garrison "
                                   "here; you mean to hold the fortress above Wenweg; you lost Brenlin to a dragon in the last age.",
                                   "- Something of your own past: you command the fortress of Wenweg.",
                                   "- Something of your own past: you stood for Brenlin when it fell, in the last age.",
                                   "- Tornar: role: council; where: Wenweg; status: about; tie: ally",
                                   "The stranger asked about you. Answer what they asked"})
        check::is_true(them.find(said) != std::string::npos, "about themself: " + said);
    check::is_true(prompt_of(voice, packets::about_self(), "Who are you?").find("Yourself:") == std::string::npos, "the control");
    // A castellan's past (contract version 19): the years at the post and where they served before it, in
    // C++'s words; the sovereign as a tie. A Self fact without them gives the line as it was (above).
    const std::string sworn = prompt_of(voice, packets::about_a_castellan_with_a_past(), "How long have you held it?");
    for (const std::string said : {"; you have held your post ten years and more; before it you served as a sergeant in the garrison of Wenweg.",
                                   "- Something of your own past: you won a war between realms.",
                                   "- Brenbrann: role: sovereign; status: about; tie: liege"})
        check::is_true(sworn.find(said) != std::string::npos, "a castellan's past: " + said);
    check::is_true(prompt_of(voice, packets::about_a_castellan_second_elsewhere(), "Where were you before?")
                           .find("; you have held your post a few years; before it you were second in command at the fortress above Mokdorl.") !=
                       std::string::npos,
                   "second at another fortress");
    // How their work went (contract version 20), in C++'s words: as told to them, or as one who was there.
    const std::string accounted = prompt_of(voice, packets::with_account(packets::greeted_after("finished", "warm")), "I'm back.");
    check::is_true(accounted.find("- How your work went, as it was told to you: Frost owls came, more than I'd have liked; a few were lost, and "
                             "you held when it counted.") != std::string::npos,
                   "the account, told to them");
    const std::string there = prompt_of(voice, packets::with_account(packets::beside(packets::greeted_after("finished", "warm"))), "I'm back.");
    check::is_true(there.find("- How your work went, the stranger beside you: Frost owls came at us") != std::string::npos,
                   "the account, as one who was there");
    check::is_true(prompt_of(voice, packets::greeted_after("finished", "warm"), "I'm back.").find("How your work went") == std::string::npos,
                   "the control: no account, no line");
    // A question of theirs never answered (contract version 21), to ask again.
    check::is_true(prompt_of(voice, packets::greeted_with_a_question(), "Hello.")
                           .find("You once asked them this, and they never answered: Who were they to you?") != std::string::npos,
                   "the question, as they asked it");
}

TEST(work_from_others_is_never_offered_as_the_speakers_own) {
    // Told "yours first" with no work of their own, an innkeeper claimed another's work as hers
    // (judged 2026-09-24). A speaker with work of their own gives theirs first; one without is
    // told none of it is theirs to give.
    Voice voice(test_model::model());
    const auto own_offers = [](const FactPacket& p) {
        return std::count_if(p.facts.begin(), p.facts.end(), [](const Fact& f) {
            return f.kind == orchestrator::FactKind::QuestOffer && f.field("from_speaker") && *f.field("from_speaker") == "yes";
        });
    };
    const FactPacket inn = packets::work_at_the_inn(), keep = packets::work();
    check::is_true(own_offers(inn) == 0 && own_offers(keep) > 0, "the premise: the innkeeper has no work of her own, the castellan has");
    const std::string at_the_inn = prompt_of(voice, inn, "Anyone hiring?");
    check::is_true(at_the_inn.find("yours first") == std::string::npos, "not 'yours first' at the inn");
    check::is_true(at_the_inn.find("None of it is yours to give") != std::string::npos, "the innkeeper told none of it is hers");
    check::is_true(prompt_of(voice, keep, "Any work?").find("yours first") != std::string::npos, "the castellan gives his own first");
}

TEST(no_opening_sounds_like_an_assistant) {
    // The openings are C++'s words in a speaker's mouth. "I can't help with that", said by a
    // castellan, read as an AI assistant turning a request down (judged 2026-09-24).
    const std::vector<std::string> assistant = {"help with that", "assist", "as an ai", "i'm sorry, but", "i apologize",
                                                "language model", "i'm unable"};
    std::size_t openings = 0;
    for (const auto& [packet, line] : outcome_cases()) {
        for (const std::string& opening : communicator::reply_shape(packet).openings) {
            ++openings;
            std::string lower;
            for (const char c : opening) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (const std::string& phrase : assistant)
                check::is_true(lower.find(phrase) == std::string::npos, "an opening a speaker would say, not \"" + opening + "\"");
        }
    }
    check::is_true(openings > 20, "the premise: openings to read");
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
    // and small talk has such words, and so does an answer to "is it safe": what happened is
    // said first.
    for (const auto& [packet, line] : outcome_cases()) {
        const communicator::ReplyShape shape = communicator::reply_shape(packet);
        const std::string outcome(orchestrator::to_string(packet.outcome));
        check::equal(shape.openings.empty(), starts_freely(packet), "opening words for " + outcome);
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
    // declining, giving up, parting, not following, a name known already (contract version 24). A
    // refusal's closing is C++'s, and comes after.
    using orchestrator::Outcome;
    const std::set<Outcome> short_ones = {Outcome::Unknown,       Outcome::Refused, Outcome::Declined, Outcome::Abandoned,
                                          Outcome::Parted,        Outcome::NotUnderstood, Outcome::Thanked, Outcome::Offended,
                                          Outcome::Denied,        Outcome::Known};
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

TEST(a_reply_counts_out_no_distance_or_time) {
    // Told "To the coast, by the high road", every model tried said how far the coast was
    // (judged 2026-09-26). So no number from two up, no unit of distance and no time in the
    // plural is said, unless the packet's own text says it. Told to say one, the free model does;
    // held, it cannot.
    inference::Model& model = test_model::model();
    Voice voice(model);
    const std::string grammar = voice.grammar(packets::about_self());
    // Each on its own line, so one barred word cannot steer the model off another; and as a stem,
    // since barred whole, "days" came back glued ("three daysago the high road").
    for (const auto& [said, stem] : {std::pair<std::string, std::string>{"It is three leagues by the high road.", "three"},
                                    {"It takes days to get there.", "days"},
                                    {"The coast is miles off.", "mile"}}) {
        const auto [free_reply, held] = told_to_say(model, said, grammar);
        check::is_true(says(free_reply, stem, false), "the premise: free, the model says " + stem + ": " + free_reply);
        check::is_true(!says(held, stem, false), "held, no word begins with " + stem + ": " + held);
    }
    // The memory's own band stays sayable: met a few days ago, "days" is the packet's word.
    FactPacket met = packets::about_self();
    met.facts.back() = packets::memory_of(met.speaker, {.met = "again", .times = 1, .since = "days"});
    const std::string held_met = told_to_say(model, "I saw you a few days ago.", voice.grammar(met)).second;
    check::is_true(says(held_met, "days"), "the packet's own word: " + held_met);
    // No entry bars what the packet's own ways of saying how far and when need, nor "one day".
    for (const std::string needed : {"one", "day", "hour", "walk", "short", "long", "way", "close", "few", "ago", "pace"})
        for (const std::string& entry : communicator::counting_words()) {
            const bool stem = !entry.empty() && entry.back() == '*';
            check::is_true(stem ? std::string(needed).rfind(entry.substr(0, entry.size() - 1), 0) != 0 : needed != entry,
                           entry + " bars " + needed);
        }
}

TEST(what_the_speaker_feels_toward_the_stranger_is_what_they_are_told_to_do) {
    // Told only in what they remember ("You dislike them."), every model tried chatted cordially
    // with a hostile speaker, and answered calming words from someone talked down lately as if it
    // had never happened: the instruction said "Answer in kind" and "you are not afraid. Say so"
    // (judged 2026-09-26). The instruction now carries it.
    Voice voice(test_model::model());
    const std::string hostile = prompt_of(voice, packets::small_talk("fair", "still", "hostile"), "Nice evening.");
    check::is_true(hostile.find("you dislike them. You want no chat with them: answer curtly") != std::string::npos,
                   "hostile small talk: curtly");
    check::is_true(hostile.find("Answer in kind") == std::string::npos, "and not in kind");
    const std::string wary = prompt_of(voice, packets::small_talk("fair", "still", "wary"), "Nice evening.");
    check::is_true(wary.find("you are wary of them") != std::string::npos, "wary small talk");
    check::is_true(prompt_of(voice, packets::small_talk("fair", "still"), "Nice evening.").find("Answer in kind") != std::string::npos,
                   "the control: a neutral speaker chats in kind");
    const std::string eased = prompt_of(voice, packets::not_afraid_eased(), "You're safe now, you know.");
    check::is_true(eased.find("they talked you down lately") != std::string::npos && eased.find("Thank them") != std::string::npos,
                   "talked down lately: thank them");
    check::is_true(prompt_of(voice, packets::not_afraid(), "Easy.").find("Thank them") == std::string::npos, "the control");
    // How far only for the packet's places: "Say how far places are" drew a distance to the coast,
    // which no packet held. Nothing said of anywhere else: "Of anywhere else, you know neither how
    // far it is nor how long it takes to reach" drew "I know no places beyond Wenweg" instead
    // (judged 2026-09-26).
    check::is_true(hostile.find("Say how far the places above are only in the words given") != std::string::npos &&
                       hostile.find("Say how far places are") == std::string::npos,
                   "how far, of the packet's places alone");
    check::is_true(hostile.find("Of anywhere else") == std::string::npos, "and nothing of anywhere else");
}

TEST(a_cold_speakers_small_talk_opens_curtly) {
    // The grammar holds the stance in the first words: a model reply to small talk from a cold
    // speaker opens curtly. Written lines carry the stance in their own words (a cold bank's
    // small talk is written cold), so their shape's openings stay free.
    Voice voice(test_model::model());
    const FactPacket cold = packets::small_talk("fair", "still", "hostile");
    check::is_true(communicator::reply_shape(cold).openings.empty(), "written lines: any start, as before");
    check::is_true(!communicator::reply_shape(cold).stance.empty(), "a model reply: a stance");
    const std::string grammar = voice.grammar(cold);
    check::is_true(grammar.find("root ::= first") != std::string::npos && grammar.find("\"Hmph\"") != std::string::npos,
                   "held to open with one: " + grammar.substr(0, 200));
    const std::string neutral = voice.grammar(packets::small_talk("fair", "still"));
    check::is_true(neutral.find("root ::= sentence") != std::string::npos, "the control: a neutral speaker, any start");
    // Frightened words come before coldness, as in the written lines.
    FactPacket afraid = cold;
    afraid.facts.back() = packets::memory_of(afraid.speaker, {.mood = "hostile", .feeling = "afraid"});
    check::is_true(communicator::reply_shape(afraid).stance.empty(), "frightened: no curt stance");
}

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

TEST(a_common_capital_only_begins_a_sentence) {
    // Mid-sentence, a common capital passes for a name: asked about Castle Grey, Qwen3-4B told of
    // "a castle named Greetings" (judged 2026-09-24). So past a sentence's first word, a capital
    // is the packet's, or "I". Told to say such a line, the free model does; held, no common
    // capital stands past a sentence's start.
    inference::Model& model = test_model::model();
    Voice voice(model);
    const FactPacket about = packets::about_self();
    const std::string grammar = voice.grammar(about);
    const std::string named = "It is a castle named Greetings.";
    const auto [free_reply, held] = told_to_say(model, named, grammar);
    check::is_true(!stray_capitals(free_reply, about).empty(), "the premise: free, the model says " + named + ": " + free_reply);
    check::is_true(stray_capitals(held, about).empty(), "held, no common capital mid-sentence: " + held);
    // A common capital still begins a sentence, and the packet's capitals and "I" still stand
    // anywhere.
    const std::string greeting = told_to_say(model, "Greetings, traveller.", grammar).second;
    check::is_true(greeting.rfind("Greetings", 0) == 0, "a common capital begins a sentence: " + greeting);
    const std::string serving = told_to_say(model, "Yes, I serve Selbren at Wenweg.", grammar).second;
    for (const std::string said : {" I ", "Selbren", "Wenweg"})
        check::is_true(serving.find(said) != std::string::npos, "still said, held: \"" + said + "\" in " + serving);
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
                           {{"kind", "watchtower"}, {"name", "the southern tower"}, {"range", "near"}}});
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
