// The interpreter: what a player says to someone, as one dialogue act of the orchestrator's
// closed set.
//
// A model answers two questions, each held by a grammar. First, which act the line is: only acts
// of the contract, and only those with something to point at. Then, for an act that takes a
// target, which of the things the player has been told of (Known) it points at: only things of
// the act's kind, or "not listed". A name the player says outright is taken without asking. The
// label comes back to C++ to become the id the act carries. The model therefore cannot name an
// act outside the contract, or a target the player was never told of: neither is in the language
// it may answer in.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "communicator/known.hpp"
#include "inference/runtime.hpp"
#include "orchestrator/contract.hpp"

namespace communicator {

struct Interpretation {
    orchestrator::DialogueAct act;
    std::string label;  // the words the target was named by; empty when the act takes none
    // Set when the player meant an act that takes a target, but pointed at something no packet
    // named ("Who is Gandalf?"). Such an act cannot be made, so `act` is no_act and this says
    // what was meant.
    std::optional<orchestrator::ActKind> meant;
};

// The answer the target question offers, beside the known labels, for something no packet
// named. Choosing it makes the act no_act, with `meant` set.
inline constexpr std::string_view kNotListed = "(not listed)";

// One question put to the model: what it is told, and the grammar its answer is held to.
struct Question {
    std::vector<inference::Message> messages;
    std::string grammar;
};

class Interpreter {
public:
    // The model is borrowed, not owned, and must outlive the interpreter. One model can serve an
    // interpreter and a voice, called in turn.
    explicit Interpreter(inference::Model& model);

    // The act `line` is. `last_reply` is what the speaker said last, if anything: "yes" after an
    // offer is an acceptance. Greedy, so the same line, knowledge and reply give the same act.
    // Throws Error for a line or a reply past kMaxLineBytes.
    Interpretation interpret(std::string_view line, const Known& known, std::string_view last_reply = {});

    // The questions interpret() puts to the model, for a log or a test: which act the line is,
    // and, for an act that takes a target, which known thing of its kind it points at. The
    // candidates come most likely first: those sharing the most words with the line, and with
    // the last reply.
    Question act_question(std::string_view line, const Known& known, std::string_view last_reply = {}) const;
    Question target_question(orchestrator::ActKind act, std::string_view line, const Known& known,
                             std::string_view last_reply = {}) const;

private:
    inference::Model& model_;
};

}  // namespace communicator
