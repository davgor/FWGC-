// What the player has been told: every place, person and piece of work a fact packet has named,
// under the words a player would use for it.
//
// A player's words can point only at these. The interpreter offers a model the labels and never
// the ids; a label the model picks comes back here to become the id a DialogueAct carries.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "orchestrator/contract.hpp"

namespace communicator {

enum class Kind : int {
    Place = 0,  // a place fact: what ask_about_place targets
    Person,     // a person fact or a packet's speaker: what ask_about_person targets
    Offer,      // work on offer: what accept_quest and decline_quest target
    Held,       // work the player has taken: what abandon_quest targets
};

std::string_view to_string(Kind kind);

struct Referent {
    Kind kind = Kind::Place;
    std::string id;     // the world id the packet gave it: what a DialogueAct's target carries
    std::string label;  // how a player would name it
};

class Known {
public:
    // Everything `packet` names that a player could point at. The latest packet comes first:
    // when two things share a label, the one named most recently is meant, and within one
    // packet the one named first (the director lists places nearest first).
    //
    // Accepting work moves it from Offer to Held; abandoning it forgets it. Throws Error for a
    // packet of another contract version.
    void learn(const orchestrator::FactPacket& packet);

    // What a player can name of one kind, one entry per label, in the order above; at most
    // `limit` of them.
    std::vector<Referent> nameable(Kind kind, std::size_t limit = kMaxNameable) const;

    // The id `label` names among things of `kind`, or nullopt.
    std::optional<std::string> resolve(Kind kind, std::string_view label) const;

    // How many of each kind are kept: the oldest past this are forgotten.
    static constexpr std::size_t kKeptPerKind = 256;
    // How many of each kind nameable() offers by default.
    static constexpr std::size_t kMaxNameable = 24;

private:
    std::vector<Referent> referents_;  // the latest first
};

// How a player would name what `fact` is about, or empty when it cannot be named: a place by its
// name, or as "the hamlet of X" when it has none; a person by name; an offer as its giver and
// what they say it is for.
std::string label_of(const orchestrator::Fact& fact);

}  // namespace communicator
