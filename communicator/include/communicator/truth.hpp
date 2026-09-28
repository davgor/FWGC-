// What a reply claims that its speaker has no ground for: the check every reply passes before the
// game shows it.
//
// A reply is words for one fact packet (orchestrator/contract.hpp), spoken by a speaker who
// remembers what the packet's Memory fact says and what they said lately (`recent`, as the game
// keeps it). untrue() reads the reply for the claims a speaker can make without the packet's
// ground, and returns one problem for each: a name no fact gives, a distance or a time no fact
// holds ("three days' march"), a danger word no place or nest has, when they met or whether they
// have, "as I told you" with nothing told, "you told me" with none of the stranger's words kept,
// what came of the speaker's own work, a verdict's words
// against the verdict, the weather, the time of day or the season against what the packet says of
// them, and an acquaintance with someone the packet only names. It is deterministic: no model
// reads the reply.
//
// Lines::speak() says no written line it finds untrue, and the game runs the check on the model
// voice's reply before showing it, falling back to the written lines when it fails.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "orchestrator/contract.hpp"

namespace communicator {

// Every claim of `reply` that `packet` and the speaker's memory do not back, each as "<kind>:
// <what was said>"; empty when there is none. `recent` is what the speaker said lately, the
// latest last, as Lines::speak() takes it.
std::vector<std::string> untrue(std::string_view reply, const orchestrator::FactPacket& packet,
                                const std::vector<std::string>& recent = {});

// The kinds of claim untrue() can never back, whatever the packet: an acquaintance with a person
// the packet only names, and a distance or a length of time counted out. A written line with one
// of these is wrong wherever it is said; the other kinds depend on the packet.
bool never_backed(const std::string& problem);

}  // namespace communicator
