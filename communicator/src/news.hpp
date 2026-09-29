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

// The same, dated, as they are said: with when the state came about, from the News fact's calendar
// band ("helped us hold a few days back", "Orlan beat off the threat here yesterday"; "today",
// "yesterday", "a few days back", "some weeks back", "months back", "years back"). A standing state
// is not dated: work still asked for, withdrawn or past doing, and who someone turned out to be.
// The owner, 2026-09-27: "Lets date the news".
std::string dated_deed_words(const orchestrator::Fact& news, const std::string& self, std::string_view here = {});
std::string dated_news_words(const orchestrator::Fact& news, const std::string& self, std::string_view stranger = "you",
                             std::string_view here = {});

// What the speaker themself did as a hero (contract version 26), in the first person, their side of it:
// "we held the fortress above Wenweg against the glacier dragons", "we fought the frost wyrms here and lost"
// (a fight lost where they still stand, never driven from it), "I hunted down the storm dragons", "we were
// thrown back from the ruins of Linbrig". A place held or assaulted is "we", a hunt "I". Empty unless the News fact is of a hero's deed and `self` names its
// hero. `here` as for deed_words. Dated as news is, from its calendar band.
std::string own_deed_words(const orchestrator::Fact& news, const std::string& self, std::string_view here = {});
std::string dated_own_deed_words(const orchestrator::Fact& news, const std::string& self, std::string_view here = {});

// Every one of the speaker's own deeds the packet holds (contract version 29: all since the stranger last saw them,
// newest first), told as one account in the order they came, the same deed again counted, a foe or place named
// before said again as "them" and "it" or "there": "we fought the leviathan at the fortress and lost twice, then we
// held it against them". Dated, as the newest was. Empty when the packet holds none. Told only the newest, a
// castellan who had lost twice and then held gave the hold alone (the golden benchmark's round 31).
std::string own_story_words(const orchestrator::FactPacket& packet, bool dated);

// The News fact of the speaker's own newest deed (contract version 26), or null: the first of the packet's
// that own_deed_words can say.
const orchestrator::Fact* own_deed_of(const orchestrator::FactPacket& packet);

// How the speaker took a deed of theirs (contract version 30), by a News fact's `took_it`, in the second person, for
// the voice to speak from: "it humbled you; you own your part in it". A feeling, never an event: nothing in it
// happened. Empty for anything but a reaction's name.
std::string took_words(std::string_view reaction);

// Whether the speaker of `packet` brings `news` up unasked, by how old it is: "relevant news is the
// talk of the town, old news is less likely to pop up" (the owner, 2026-09-27). Today's and
// yesterday's always; this week's by about one speaker in two; the last two months' by one in five;
// older, never unasked. In an answer about its place or person, a band fresher, and so is a hero's own
// deed to the hero (contract version 26): what they did stays with them longer. The same speaker
// decides the same way for the same news: a hash of who and which event, never a roll. Asked for
// news, a speaker tells it whatever its age.
bool brought_up(const orchestrator::Fact& news, const orchestrator::FactPacket& packet);

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
