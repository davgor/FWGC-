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

#include <map>
#include <stdexcept>
#include <string>
#include <utility>
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
// What the game reports the player did with work they took: finished it, or failed it.
struct Resolve {
    std::string quest_id;
    bool completed = true;
};
using Input = std::variant<Advance, Say, Resolve>;

class Director {
public:
    explicit Director(fwg::World world);

    const fwg::World& world() const { return world_; }
    const std::vector<Input>& inputs() const { return inputs_; }

    // Time passes: `days` of it, through fwg::advance_world_time. Throws Error for a span that
    // is negative, not finite, or an age or more long (an age turning is a separate decision,
    // not something a conversation's clock does), and when the world refuses the span. Every
    // conversation under way ends: time passing is walking away.
    void advance(double days);

    // The game reports taken work finished (`completed`) or failed, through
    // fwg::advance_world_time's resolutions with no time passing. The giver remembers it.
    // Throws Error for work the world does not hold open, or holds but not taken.
    void resolve(const std::string& quest_id, bool completed);

    // One act from the player to `speaker`, a person uid. Accepting or abandoning a quest
    // changes the world; nothing else here does, though every act changes what the speaker
    // remembers (the Memory fact). Throws Error for a uid the world does not name and for an
    // act missing the target it needs.
    FactPacket respond(const std::string& speaker, const DialogueAct& act);

    // What one speaker and the player have between them. Folded from the inputs, so replay
    // rebuilds it and a save is the inputs alone.
    struct Acquaintance {
        int conversations = 0;  // begun, the one under way included
        bool talking = false;   // under way: none after a farewell, time passing, or talk with another
        double last_day = 0.0;  // the world day of the last act to this speaker
        std::string since;      // how long before this conversation the last one was, roughly
        int turn = 0;           // acts in this conversation
        int disposition = 0;    // -3 hostile .. 3 warm
        bool thanked = false;   // in this conversation: thanks count once
        std::vector<std::pair<ActKind, std::string>> said;  // this conversation's acts
        std::string asked_now, asked_before;  // the last place or person asked about, by name
        std::string work;       // the player's latest dealing with this speaker's own work
        bool exposed = false;          // the player made a hidden face come out
        bool finished_covert = false;  // finished work this speaker gave under a false purpose
        bool heard_truth = false;      // someone who knows them told the player what they are
        bool seen_unleashed = false;   // met a beast unleashed
        bool seen_second = false;      // met a fractured heart's second face
    };
    // Nullptr for someone the session holds nothing of: never spoken to, never dealt with, never
    // told the truth of.
    const Acquaintance* acquaintance(const std::string& speaker) const;

private:
    void end_conversation(const std::string& speaker);

    fwg::World world_;
    std::vector<Input> inputs_;
    std::map<std::string, Acquaintance> acquainted_;
    std::string talking_to_;
};

// A session again: `start` with every input re-applied, in order.
Director replay(fwg::World start, const std::vector<Input>& inputs);

}  // namespace orchestrator
