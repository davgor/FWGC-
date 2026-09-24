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
    int sentences = 3;                  // at most this many of the model's own
    std::string closing;                // a last sentence, word for word; empty: none
};

ReplyShape reply_shape(const orchestrator::FactPacket& packet);

}  // namespace communicator
