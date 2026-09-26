#include "reply_shape.hpp"

#include "memory.hpp"
#include "text.hpp"

namespace communicator {

namespace {

using orchestrator::ActKind;
using orchestrator::Fact;
using orchestrator::FactKind;
using orchestrator::Outcome;

// Who a refusal sends the stranger to, as a sentence: the one person the packet names. With
// two or more, nothing says which is meant, so no one is named; the order facts come in is
// not a meaning to rely on. With `named`, the reply has said the name already, so the sentence
// says only where: "Find them at the ruins of Linbrig.", or nothing when the packet says nowhere.
std::string who_to_see(const orchestrator::FactPacket& packet, bool named) {
    const Fact* person = nullptr;
    for (const Fact& fact : packet.facts) {
        if (fact.kind != FactKind::Person) continue;
        if (person) return {};
        person = &fact;
    }
    const std::string* name = person ? person->field("name") : nullptr;
    if (!name || text::spoken(*name).empty()) return {};
    const std::string* where = person->field("where");
    const std::string at = where ? text::spoken(*where) : std::string();
    if (named) return at.empty() ? std::string() : "Find them at " + at + ".";
    return "Speak to " + text::spoken(*name) + (at.empty() ? "" : ", at " + at) + ".";
}

// The verdict of an answer to "is it safe", or empty when the packet has none.
std::string verdict_of(const orchestrator::FactPacket& packet) {
    for (const Fact& fact : packet.facts)
        if (fact.kind == FactKind::Safety)
            if (const std::string* verdict = fact.field("verdict")) return *verdict;
    return {};
}

}  // namespace

ReplyShape reply_shape(const orchestrator::FactPacket& packet) {
    ReplyShape shape;
    const Remembered memory = remembered(packet);
    switch (packet.outcome) {
        case Outcome::Answered: {
            // "Is it safe" is answered yes or no before anything else (judged 2026-09-25: no
            // reply said no first).
            const std::string verdict = packet.answering == ActKind::AskSafety ? verdict_of(packet) : std::string();
            if (verdict == "unsafe")
                shape.openings = {"No", "Not here", "Not safe", "Far from it", "I wouldn't say so"};
            else if (verdict == "wary")
                shape.openings = {"Safe enough", "Mostly", "More or less", "Careful", "Keep your wits about you"};
            else if (verdict == "safe")
                shape.openings = {"Safe enough", "Yes", "Aye", "It's safe", "Quiet enough", "Nothing to fear"};
            break;  // otherwise any start, up to three sentences
        }
        case Outcome::Accepted:
            shape.openings = {"Good", "Then it's agreed", "Done", "Very good", "Thank you"};
            shape.sentences = 2;
            break;
        case Outcome::Declined:
            shape.openings = {"Very well", "As you wish", "Suit yourself", "So be it", "Fair enough"};
            shape.sentences = 1;
            break;
        case Outcome::Abandoned:
            shape.openings = {"So be it", "Very well", "A pity", "As you wish", "Then it's done"};
            shape.sentences = 1;
            break;
        case Outcome::Unknown:
            shape.openings = {"I don't know", "I've never heard", "I can't say", "I know nothing", "Never heard",
                              "No idea", "I couldn't tell you"};
            shape.sentences = 1;
            break;
        case Outcome::Refused:
            shape.openings = {"No", "I can't", "I cannot", "I won't", "Not from me", "That's not mine"};
            shape.sentences = 1;
            shape.closing = who_to_see(packet, false);
            shape.closing_after_name = who_to_see(packet, true);
            break;
        case Outcome::Parted:
            // A wary or hostile speaker sees the stranger off, not well: "Go well" after "Keep
            // back" read as a speaker who forgot the last line (judged 2026-09-26).
            if (memory.cold())
                shape.openings = {"Go, then", "Be on your way", "On your way", "Off with you", "Get going"};
            else
                shape.openings = {"Farewell", "Safe travels", "Go well", "Goodbye", "Take care"};
            shape.sentences = 1;
            break;
        case Outcome::NotUnderstood:
            // Not "I can't help with that": said by a speaker in a fantasy world, it read as an AI
            // assistant turning a request down (judged 2026-09-24).
            shape.openings = {"I don't follow", "I'm not sure what you mean", "What do you mean", "That's beyond me",
                              "Speak plainly"};
            shape.sentences = 1;
            break;
        case Outcome::Greeted:
            // A greeting says how the speaker takes the stranger, from what they remember: a cold
            // one whether or not they have met, "again" only for someone met before.
            if (memory.cold())
                shape.openings = {"What do you want", "What now", "Keep back", "Stay back"};
            else if (memory.again)
                shape.openings = {"Hello again", "Back again", "Welcome back", "Good to see you", "Well met again", "Good day"};
            else
                shape.openings = {"Hello", "Greetings", "Well met", "Good day", "Welcome", "Hail"};
            shape.sentences = 2;
            break;
        case Outcome::Chatted:
            shape.sentences = 2;  // any start
            break;
        case Outcome::Acknowledged:
            shape.openings = {"I see", "Is that so", "Fair enough", "Good", "Right", "Ah", "Well", "Aye", "Indeed"};
            shape.sentences = 2;
            break;
        case Outcome::Thanked:
            if (memory.cold())
                shape.openings = {"Save your thanks", "Keep your thanks", "Hmph", "Spare me"};
            else
                shape.openings = {"You're welcome", "No need", "Think nothing of it", "Glad to", "My pleasure", "Don't mention it"};
            shape.sentences = 1;
            break;
        case Outcome::Offended:
            shape.openings = {"Watch your tongue", "Mind your manners", "How dare you", "Enough", "Careful", "Hold your tongue",
                              "There's no need for that"};
            shape.sentences = 1;
            break;
        case Outcome::Exposed:
            shape.openings = {"So", "So you know", "Very well", "Fine", "Clever", "Yes", "You've found me out", "Enough, then"};
            shape.sentences = 3;
            break;
        case Outcome::Denied:
            shape.openings = {"No", "Nonsense", "You're mistaken", "I don't know what you mean", "What are you on about",
                              "That's not so", "You have the wrong one"};
            shape.sentences = 1;
            break;
    }
    return shape;
}

}  // namespace communicator
