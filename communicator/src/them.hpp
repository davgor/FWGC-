// What a speaker says of themself, in words: the director's Self and Deed facts (contract version
// 13), said by C++ from their own fields, never by a model, as news is. Every name in the words is
// one the facts hold, so a line that says them names nothing its packet does not.
#pragma once

#include <string>
#include <vector>

#include "orchestrator/contract.hpp"

namespace communicator {

// What they are after, as words that follow "I mean to": "hold the fortress above Iskmor", "kill
// the glacier dragons, the doom of Brenlin", "take back Eldkor", "keep the farmstead below Dunweg
// safe". Empty when the Self fact holds no claim.
std::string after_words(const orchestrator::Fact& self);

// How they stand where they are, as a sentence's words after "I": "hold the garrison here", "am a
// refugee here", "am held here", "sit on the council here". Empty when the Self fact does not say.
std::string standing_words(const orchestrator::Fact& self);

// A city they lost, and to what: "lost Brenlin to a dragon, in the last age". Words that follow "I".
// Empty when the Self fact holds none.
std::string lost_words(const orchestrator::Fact& self);

// A part they had in something, as words that follow "I": "command the fortress above Iskmor",
// "stood for Brenlin when it fell", "speak for the farmstead below Dunweg", "won a war between
// realms, in the last age". Empty for a deed these words cannot say.
std::string done_words(const orchestrator::Fact& deed);

// Who stands with them, and against them: "Iskgraf and Nerisk stand with me", "Ulknord is my
// rival". From the packet's Person facts marked `tie`; empty when there are none.
std::string ties_words(const orchestrator::FactPacket& packet);

// When they were born, against the world's latest age: "this age", "the last age", "ages ago",
// as the Self fact has it. Empty when it does not say.
std::string born_words(const orchestrator::Fact& self);

// The packet's Self fact and Deed facts, in the director's order; nullptr and empty when it holds
// none.
const orchestrator::Fact* self_of(const orchestrator::FactPacket& packet);
std::vector<const orchestrator::Fact*> deeds_of(const orchestrator::FactPacket& packet);

}  // namespace communicator
