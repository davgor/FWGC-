// How what happened shapes a reply: the words it opens with, how many sentences it may run to,
// and a closing sentence C++ writes itself. The voice's grammar holds a reply to its shape, so a
// refusal opens as one and cannot turn into agreement, and a speaker who knows nothing says so
// before anything else.
#pragma once

#include <string>
#include <vector>

#include "orchestrator/contract.hpp"

namespace communicator {

struct ReplyShape {
    std::vector<std::string> openings;  // the reply begins with one of these; empty: any start
    // Where `openings` leaves the start free, what a model's reply begins with all the same, so
    // the speaker's stance shows in its first words: a cold speaker's small talk. Written lines
    // carry the stance in their own words (a cold bank's small talk is written cold), so the rules
    // hold them to `openings` alone.
    std::vector<std::string> stance;
    int sentences = 3;                  // at most this many of the model's own
    std::string closing;                // a last sentence, word for word; empty: none
    // The closing for a reply that has already named who it sends the stranger to: where they
    // are, without the name again ("Find them at the ruins of Linbrig."), or empty. A written
    // line that says {giver} ends with this; a refusal then names the giver once.
    std::string closing_after_name;
};

ReplyShape reply_shape(const orchestrator::FactPacket& packet);

}  // namespace communicator
