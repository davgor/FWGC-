// What a packet's speaker remembers of the stranger (orchestrator/contract.hpp, the Memory fact),
// read once for the reply shapes, the written lines and the voice.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "orchestrator/contract.hpp"

namespace communicator {

struct Remembered {
    bool again = false;        // met before this conversation
    int times = 0;             // conversations before this one
    std::string since;         // "today", "yesterday", "days", "weeks", "months", "years"; empty the first time
    std::string mood = "neutral";
    std::string work;          // "offered", "taken", "declined", "abandoned", "finished", "failed", or empty
    bool beside = false;       // that work was at the place they stand, done beside them (contract version 14)
    bool work_news = false;    // what came of it, finished or failed, is news between them: the first
                               // conversation since (contract version 17)
    std::string asked_before;  // a place or person, by name, or empty
    int turn = 1;
    bool repeat = false;
    int heard = 0;             // how many times before the stranger made this act to the speaker, in any conversation
    bool exposed = false;
    std::string feeling = "calm";  // on the fear ladder (contract version 9): calm, uneasy, afraid, panicked
    std::string resting = "calm";  // how they feel when nothing frightens them
    bool eased = false;   // the player eased their fear within the last two days (contract version 10)
    bool joined = false;  // they overheard the player and joined in unasked
    // The name they know the stranger by, as it was given, perhaps a lie (contract version 24); how they
    // came by it: "told", "word" (with word of the stranger's deed), "overheard"; and another name they
    // also know them by, when two differ. Empty when they know none.
    std::string called, called_how, called_other;

    bool warm() const { return mood == "warm" || mood == "friendly"; }
    bool cold() const { return mood == "wary" || mood == "hostile"; }
    // Afraid or panicking: a speaker whose words show it.
    bool frightened() const { return feeling == "afraid" || feeling == "panicked"; }
};

// The packet's Memory fact, read; a first meeting, neutral, when it carries none.
Remembered remembered(const orchestrator::FactPacket& packet);

// One thing the speaker remembers of the stranger from before (contract version 12, a Recall fact):
// what the stranger asked them, what they told the stranger, what the stranger did, work of theirs
// the stranger holds, the stranger's own words, what they last spoke of.
struct Recall {
    std::string subject;
    std::string memory;  // "asked", "told", "did", "open", "said", "last", "question" (contract version 21)
    std::string act;
    std::string about;   // by name, as the speaker says it
    int times = 0;
    std::string since;   // when it last was, in the Memory fact's bands
    std::string first;   // when it first was
    std::string words;   // the stranger's own words, for "said"; the speaker's own question, for "question"
};

// The packet's Recall facts, in its order.
std::vector<Recall> recalls(const orchestrator::FactPacket& packet);

// The first of `all` that is a `memory`, or nullptr.
const Recall* recall_of(const std::vector<Recall>& all, std::string_view memory);

// How long ago, as a speaker says it: "earlier today", "yesterday", "a few days ago", "some
// weeks ago", "some months ago", "years ago". Empty for a band the contract does not publish.
std::string since_words(const std::string& since);

}  // namespace communicator
