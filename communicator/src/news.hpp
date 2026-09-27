// News a speaker has heard, in words: what the director's News fact holds (contract version 11),
// said by C++ from the fact's own fields, never by a model. Every name in the words is one the
// fact holds -- the place, the giver, the person -- so a line that says them names nothing its
// packet does not.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "orchestrator/contract.hpp"

namespace communicator {

// What the stranger did, as words that follow "you": "helped Wraignwight hold", "beat off the
// threat at Wenweg". Empty unless the News fact says the stranger brought its state about, and for
// news of the speaker's own work, of the speaker themself or of what they did, `self` their name:
// their own lines say that. `here` is where the speaker stands, by name: a place named for it is
// said by its kind ("the fortress of Iskmor" is "the fortress" in Iskmor), and the place itself is
// "here" ("beat off the threat here", a defence "helped us hold").
std::string deed_words(const orchestrator::Fact& news, const std::string& self, std::string_view here = {});

// What happened, as words a sentence can hold: "you helped Wraignwight hold", "Halbrann wants
// someone to hold the fortress above Wenweg with them", "there was a scare at Wenweg". What the
// stranger did is said with `stranger` for them: "you", to them; "the stranger", in the voice's own
// instructions. Work asked for is said by what its giver asked, where they said. Empty for a fact
// these words cannot say, and for news of the speaker's own work, themself or their own deed.
// `here` as for deed_words.
std::string news_words(const orchestrator::Fact& news, const std::string& self, std::string_view stranger = "you",
                       std::string_view here = {});

// Every way the speaker may have put this news already, to the stranger: what happened, what the
// stranger did, and, for work asked for, what its giver asked (as an answer about a place or a
// request for work lists it). News said lately in any of them is not said again.
std::vector<std::string> news_forms(const orchestrator::Fact& news, const std::string& self, std::string_view here);

// The News facts of a packet, in the director's order.
std::vector<const orchestrator::Fact*> news_of(const orchestrator::FactPacket& packet);

// Whether `news`, one of `packet`'s facts, is news its speaker may pass on in that packet: news
// they heard, with words (news_words), not what happened to them or what they did (a News fact's
// `how` "there": theirs to say in their own lines, not as word going round), and, in an answer
// about a place or a person, not work only asked for, which that answer lists already. The written
// lines and the voice both go by it, so both pass on the same news.
bool may_pass_on(const orchestrator::Fact& news, const orchestrator::FactPacket& packet);

// The News fact a speaker passes on: the first in the director's order they may (may_pass_on), of
// what the stranger did (deed_words) when `of_the_stranger`. Null when there is none. Whether this
// speaker passes news on at all is the caller's to say.
const orchestrator::Fact* news_to_pass_on(const orchestrator::FactPacket& packet, bool of_the_stranger);

}  // namespace communicator
