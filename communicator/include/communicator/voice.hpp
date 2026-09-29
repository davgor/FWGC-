// The voice: a fact packet in the speaker's own words, streamed as the model writes them.
//
// The model is told who the speaker is (the face a stranger sees, and their card's stance), what
// they know (the packet's facts, without ids) and what just happened (the outcome). A grammar
// then holds its words: every capitalised word in the reply is a word of the packet's own text,
// or "I", or begins a sentence and is one of a closed set of words sentences commonly begin with
// ("The", "You"...). The voice therefore cannot name a person or a place the packet does not
// name. Nor can it give a bearing or speak of pay, in words the packet does not hold
// (forbidden_words()).
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

// One exchange of the conversation so far: what the stranger said, and the reply they were shown.
struct Exchange {
    std::string line;
    std::string reply;
};

// How many of a conversation's exchanges the voice is shown, the latest: room in the prompt to spare.
inline constexpr std::size_t kExchangesShown = 8;

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
    // as it comes, in whole UTF-8 characters; returning false stops. `said_so_far` is this
    // conversation's exchanges before `line`, oldest first, as the game kept them: the voice is
    // shown the last kExchangesShown, as the turns they were, and told to say nothing again. Empty
    // at a conversation's first turn, and then the prompt is as it always was. Throws Error for a
    // packet of another contract version or a line past kMaxLineBytes.
    Reply speak(const orchestrator::FactPacket& packet, std::string_view line,
                const std::function<bool(std::string_view)>& on_piece = {}, const std::vector<Exchange>& said_so_far = {});

    // What speak() puts to the model, and the grammar it holds the reply to: for a log, a test,
    // or a training pair. Neither needs a model.
    static std::vector<inference::Message> messages(const orchestrator::FactPacket& packet, std::string_view line,
                                                    const std::vector<Exchange>& said_so_far = {});
    static std::string grammar(const orchestrator::FactPacket& packet);

private:
    inference::Model& model_;
    VoiceOptions options_;
};

// The capitalised words a reply may use besides its packet's: "I", anywhere, and the words
// sentences commonly begin with, only where a sentence begins. None is a name.
const std::vector<std::string>& common_capitals();

// The words a reply may not say unless its packet's own text does: compass points, since a
// packet holds distances and never bearings, and money, since the game prices rewards and the
// voice promises none. Lower case, and whole words, so "golden" is not "gold". An entry ending
// in "*" is a stem, and no word may begin with it: "north*" bars "northernmost" too. The
// packet's own words are sayable, and only they: a packet naming the southern tower lets a reply
// say "southern", not "south".
const std::vector<std::string>& forbidden_words();

// The words that count out a distance or a time, which a reply may not say unless its packet's
// own text does: numbers from two up, units of distance, and times in the plural. Told "To the
// coast, by the high road", every model tried said how far the coast was (2026-09-26). A packet
// says how far only in words ("an hour's walk away"), and when the speaker last met the stranger
// by the memory's band ("days" in "a few days ago" is the packet's own word). Stems, as in
// forbidden_words(), wherever no word worth saying begins with one. The voice's alone:
// written lines keep their idioms ("most days", "the two of us"), and the truth check holds them
// to no counted distance or time.
const std::vector<std::string>& counting_words();

}  // namespace communicator
