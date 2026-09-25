#include "reply_shape.hpp"

#include "text.hpp"

namespace communicator {

namespace {

using orchestrator::Fact;
using orchestrator::FactKind;
using orchestrator::Outcome;

// Who a refusal sends the stranger to, as a sentence: the one person the packet names. With
// two or more, nothing says which is meant, so no one is named; the order facts come in is
// not a meaning to rely on.
std::string who_to_see(const orchestrator::FactPacket& packet) {
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
    return "Speak to " + text::spoken(*name) + (at.empty() ? "" : ", at " + at) + ".";
}

}  // namespace

ReplyShape reply_shape(const orchestrator::FactPacket& packet) {
    ReplyShape shape;
    switch (packet.outcome) {
        case Outcome::Answered:
            break;  // any start, up to three sentences
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
            shape.closing = who_to_see(packet);
            break;
        case Outcome::Parted:
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
    }
    return shape;
}

}  // namespace communicator
