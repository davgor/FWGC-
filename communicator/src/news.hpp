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
// news of the speaker's own work or of the speaker themself, `self` their name: their greeting
// says that already.
std::string deed_words(const orchestrator::Fact& news, const std::string& self);

// What happened, as words a sentence can hold: "you helped Wraignwight hold", "Halbrann is looking
// for help with Wraignwight", "there was a scare at Wenweg". What the stranger did is said with
// `stranger` for them: "you", to them; "the stranger", in the voice's own instructions. Empty for a
// fact these words cannot say, and for news of the speaker's own work or of the speaker themself.
std::string news_words(const orchestrator::Fact& news, const std::string& self, std::string_view stranger = "you");

// The News facts of a packet, in the director's order.
std::vector<const orchestrator::Fact*> news_of(const orchestrator::FactPacket& packet);

// The News fact a speaker passes on: the first in the director's order with words, what the
// stranger did (deed_words) when `of_the_stranger`, else what happened (news_words). Never, in an
// answer about a place or a person, work only asked for, which that answer lists already. Null when
// there is none. Whether this speaker passes news on at all is the caller's to say. The written
// lines and the voice both choose by it, so both pass on the same news.
const orchestrator::Fact* news_to_pass_on(const orchestrator::FactPacket& packet, bool of_the_stranger);

}  // namespace communicator
