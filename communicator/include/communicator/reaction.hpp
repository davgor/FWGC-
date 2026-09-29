// The reaction picker (contract version 30): how a hero takes one of their deeds, chosen by a model from the
// reactions the director holds open (orchestrator/README.md, "How a hero takes their deed").
//
// The owner, 2026-09-29: "Let the LLM interpret the outcome, based on the heroes personality, and based on the
// outcome." The model reads the brief the director gives -- the hero's card as a stranger could know it, what they
// did in the world's words, how it went -- and answers one word, held by a grammar to the brief's open reactions.
// The game hands the word to the director's react(), which refuses one not open and decides by its own rules what
// it does. The picker decides nothing else.
#pragma once

#include <string>
#include <vector>

#include "inference/runtime.hpp"
#include "orchestrator/contract.hpp"

namespace communicator {

struct PickOptions {
    int max_tokens = 8;  // the longest reaction's name, and room to close
};

class ReactionPicker {
public:
    // The model is borrowed, not owned, and must outlive the picker. Throws Error for options out of range.
    explicit ReactionPicker(inference::Model& model, PickOptions options = {});

    // How the hero of `brief` takes their deed: one of `brief.open`. Greedy: the same brief gives the same pick.
    // Throws Error for a brief with nothing open, or a field past kMaxLineBytes.
    orchestrator::Reaction pick(const orchestrator::ReactionBrief& brief) const;

    // What pick() puts to the model, and the grammar it holds the answer to: for a test or a measure. Neither
    // needs a model.
    static std::vector<inference::Message> messages(const orchestrator::ReactionBrief& brief);
    static std::string grammar(const orchestrator::ReactionBrief& brief);

private:
    inference::Model& model_;
    PickOptions options_;
};

}  // namespace communicator
