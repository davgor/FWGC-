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
// realms, in the last age", and against whom where the Deed fact says (contract version 22): "won a war
// between realms against Linbrig", "lost a war among our own to Brenlin". Empty for a deed these words
// cannot say.
std::string done_words(const orchestrator::Fact& deed);

// Contract version 25: what someone else is known for, as words that follow their name: "won a war between
// realms against Linbrig", "commands the fortress above Iskmor", "is the heir of Brenlin", "stood for Brenlin
// when it fell". The same deeds as done_words(), said of another: their neighbours, not ours. Empty for a
// deed these words cannot say.
std::string known_for_words(const orchestrator::Fact& deed);

// How far word of a deed went (a Deed fact's `scope`), as words after "told": "all through these parts",
// "all through the realm"; empty for word that went no further than where it was done, or a scope these words
// do not know.
std::string far_words(const orchestrator::Fact& deed);

// Who stands with them, and against them: "Iskgraf and Nerisk stand with me", "Ulknord is my
// rival". From the packet's Person facts marked `tie`; empty when there are none.
std::string ties_words(const orchestrator::FactPacket& packet);

// Who stands against them alone: "Ulknord is my rival", "Ulknord and Eldisk are my rivals". Empty
// when the packet names no rival.
std::string foes_words(const orchestrator::FactPacket& packet);

// How long a castellan has held their post (contract version 19), as words that follow "I've held it":
// "a few years", "the better part of ten years", "ten years and more", "near on twenty years",
// "twenty years and more", "near on thirty years". Empty when the Self fact does not say. Counted, but
// the Self fact's own: the truth check reads them as backed.
std::string held_words(const orchestrator::Fact& self);

// Where a castellan served before their post, and as what (contract version 19), as words that follow
// "I": "served as a soldier in the garrison of Iskmor", "was second in command at the fortress above
// Mokdorl". Empty when the Self fact does not say.
std::string served_words(const orchestrator::Fact& self);

// How the speaker's own work went (contract version 20), in C++'s words from an Account fact: what came
// and how many, what it cost, the stranger's part, as one clause list ("Abyssal krakens came at us, more
// than I'd have liked; we lost a few good people, and you held when it counted"). `beside`: the work was
// done where the speaker stands (the Memory fact's `beside`), so they were there: "came at us", "we
// lost"; otherwise "came", "were lost". Empty when the fact says nothing the words know.
std::string account_words(const orchestrator::Fact& account, bool beside);

// The phrases account_words() says of what the work cost and of the stranger's part: claims no reply
// makes without the Account fact that holds them (truth.hpp).
const std::vector<std::string>& account_phrases();

// The packet's Account fact, or nullptr.
const orchestrator::Fact* account_of(const orchestrator::FactPacket& packet);

// When they were born, against the world's latest age: "this age", "the last age", "ages ago",
// as the Self fact has it. Empty when it does not say.
std::string born_words(const orchestrator::Fact& self);

// The packet's Self fact and Deed facts, in the director's order; nullptr and empty when it holds
// none.
const orchestrator::Fact* self_of(const orchestrator::FactPacket& packet);
std::vector<const orchestrator::Fact*> deeds_of(const orchestrator::FactPacket& packet);

}  // namespace communicator
