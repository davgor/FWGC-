// What a packet's speaker remembers of the stranger (orchestrator/contract.hpp, the Memory fact),
// read once for the reply shapes, the written lines and the voice.
#pragma once

#include <string>

#include "orchestrator/contract.hpp"

namespace communicator {

struct Remembered {
    bool again = false;        // met before this conversation
    int times = 0;             // conversations before this one
    std::string since;         // "today", "yesterday", "days", "weeks", "months", "years"; empty the first time
    std::string mood = "neutral";
    std::string work;          // "offered", "taken", "declined", "abandoned", "finished", "failed", or empty
    std::string asked_before;  // a place or person, by name, or empty
    int turn = 1;
    bool repeat = false;
    int heard = 0;             // how many times before the stranger made this act to the speaker, in any conversation
    bool exposed = false;
    std::string feeling = "calm";  // on the fear ladder (contract version 9): calm, uneasy, afraid, panicked
    std::string resting = "calm";  // how they feel when nothing frightens them

    bool warm() const { return mood == "warm" || mood == "friendly"; }
    bool cold() const { return mood == "wary" || mood == "hostile"; }
    // Afraid or panicking: a speaker whose words show it.
    bool frightened() const { return feeling == "afraid" || feeling == "panicked"; }
};

// The packet's Memory fact, read; a first meeting, neutral, when it carries none.
Remembered remembered(const orchestrator::FactPacket& packet);

// How long ago, as a speaker says it: "earlier today", "yesterday", "a few days ago", "some
// weeks ago", "some months ago", "years ago". Empty for a band the contract does not publish.
std::string since_words(const std::string& since);

}  // namespace communicator
