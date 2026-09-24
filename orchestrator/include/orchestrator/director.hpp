// The director: one world, changed only through the generator's logged verbs, and the
// dialogue acts a player makes, answered as knowledge-scoped fact packets.
//
// Deterministic by construction. There is no clock, no randomness and no I/O in here, and the
// world changes in exactly two places: advance() calls fwg::advance_world_time, and accepting
// or abandoning a quest calls fwg::world_quest_request. Both append to the world's
// history.operations. The same starting world and the same inputs give the same world and the
// same packets; replay() is how a test proves it and how a save is restored.
//
// This header names the generator, so only what depends on the generator may include it. The
// communicator includes orchestrator/contract.hpp alone.
#pragma once

#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "fwg/api/world.hpp"
#include "orchestrator/contract.hpp"

namespace orchestrator {

// A caller's mistake: a uid the world does not name, an act without the target it needs, a
// span of time that is not a span. What the WORLD refuses is an answer, not an error: a
// packet whose outcome is Refused.
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The inputs a session applies, in order: what replay() re-applies.
struct Advance {
    double days = 0.0;
};
struct Say {
    std::string speaker;
    DialogueAct act;
};
using Input = std::variant<Advance, Say>;

class Director {
public:
    explicit Director(fwg::World world);

    const fwg::World& world() const { return world_; }
    const std::vector<Input>& inputs() const { return inputs_; }

    // Time passes: `days` of it, through fwg::advance_world_time. Throws Error for a span that
    // is negative, not finite, or an age or more long (an age turning is a separate decision,
    // not something a conversation's clock does), and when the world refuses the span.
    void advance(double days);

    // One act from the player to `speaker`, a person uid. Accepting or abandoning a quest
    // changes the world; nothing else here does. Throws Error for a uid the world does not name
    // and for an act missing the target it needs.
    FactPacket respond(const std::string& speaker, const DialogueAct& act);

private:
    fwg::World world_;
    std::vector<Input> inputs_;
};

// A session again: `start` with every input re-applied, in order.
Director replay(fwg::World start, const std::vector<Input>& inputs);

}  // namespace orchestrator
