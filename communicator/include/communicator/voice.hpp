// The voice: a fact packet in the speaker's own words, streamed as the model writes them.
//
// The model is told who the speaker is (the face a stranger sees, and their card's voice), what
// they know (the packet's facts, without ids) and what just happened (the outcome). A grammar
// then holds its words: every capitalised word in the reply is a word of the packet's own text,
// or one of a closed set of words sentences commonly begin with ("I", "The", "You"...). The
// voice therefore cannot name a person or a place the packet does not name. Nor can it give a
// bearing or speak of pay, in words the packet does not hold (forbidden_words()).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "inference/runtime.hpp"
#include "orchestrator/contract.hpp"

namespace communicator {

struct VoiceOptions {
    int max_tokens = 120;      // room for three short sentences
    float temperature = 0.0f;  // 0: greedy, the same reply to the same packet and line
    std::uint32_t seed = 0;    // used when temperature > 0
};

struct Reply {
    std::string text;
    int tokens = 0;
    bool finished = false;           // the model ended it, rather than max_tokens
    bool stopped_by_caller = false;  // on_piece returned false
};

class Voice {
public:
    // The model is borrowed, not owned, and must outlive the voice. Throws Error for options
    // out of range.
    explicit Voice(inference::Model& model, VoiceOptions options = {});

    // The speaker's reply to `line`, saying what `packet` holds. Each piece goes to `on_piece`
    // as it comes, in whole UTF-8 characters; returning false stops. Throws Error for a packet
    // of another contract version or a line past kMaxLineBytes.
    Reply speak(const orchestrator::FactPacket& packet, std::string_view line,
                const std::function<bool(std::string_view)>& on_piece = {});

    // What speak() puts to the model, and the grammar it holds the reply to: for a log, or a
    // test.
    std::vector<inference::Message> messages(const orchestrator::FactPacket& packet, std::string_view line) const;
    std::string grammar(const orchestrator::FactPacket& packet) const;

private:
    inference::Model& model_;
    VoiceOptions options_;
};

// The capitalised words a reply may use besides its packet's: "I", and words sentences commonly
// begin with. None is a name.
const std::vector<std::string>& common_capitals();

// The words a reply may not say unless its packet's own text does: compass points, since a
// packet holds distances and never bearings, and money, since the game prices rewards and the
// voice promises none. Lower case, and whole words, so "golden" is not "gold". An entry ending
// in "*" is a stem, and no word may begin with it: "north*" bars "northernmost" too. The
// packet's own words are sayable, and only they: a packet naming the southern tower lets a reply
// say "southern", not "south".
const std::vector<std::string>& forbidden_words();

}  // namespace communicator
