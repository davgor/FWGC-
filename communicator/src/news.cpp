#include "news.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>

#include "text.hpp"

namespace communicator {

namespace {

using orchestrator::Fact;
using orchestrator::FactKind;

std::string said(const Fact& fact, std::string_view key) {
    const std::string* value = fact.field(key);
    return value ? text::spoken(*value) : std::string();
}

// "a herbalist", "an innkeeper": a role as it is said after "is".
std::string a(const std::string& noun) {
    if (noun.empty()) return {};
    const char first = static_cast<char>(std::tolower(static_cast<unsigned char>(noun.front())));
    const bool vowel = first == 'a' || first == 'e' || first == 'i' || first == 'o' || first == 'u';
    return (vowel ? "an " : "a ") + noun;
}

// A place as someone standing in `here` names it: one named for where they stand, by its kind
// alone ("the fortress of Iskmor" is "the fortress" in Iskmor). A judge heard the whole names from
// the townsfolk as labels off a map (2026-09-27).
std::string named(const std::string& place, std::string_view here) {
    const std::string of = " of " + std::string(here);
    if (here.empty() || place.rfind("the ", 0) != 0 || place.size() <= of.size() + 4 ||
        place.compare(place.size() - of.size(), of.size(), of) != 0)
        return place;
    return place.substr(0, place.size() - of.size());
}

// Where something happened, after what happened: "here" where the speaker stands, else at it.
std::string at(const std::string& place, std::string_view here) {
    return !here.empty() && place == here ? "here" : "at " + named(place, here);
}

// A piece of work done, as what the stranger did: by its verb where the place is named, else what
// its giver asked. A defence of the speaker's own place is theirs too: "helped us hold".
std::string work_done(const std::string& verb, const std::string& place, const std::string& giver, std::string_view here) {
    if (!place.empty()) {
        if (verb == "defend") return !here.empty() && place == here ? "helped us hold" : "helped " + named(place, here) + " hold";
        if (verb == "reclaim") return "took back " + named(place, here);
        if (verb == "recover") return "recovered " + named(place, here);
        if (verb == "slay") return "cleared out " + named(place, here);
        if (verb == "cleanse") return "cleansed " + named(place, here);
        if (verb == "rebuild") return "rebuilt " + named(place, here);
    }
    if (!giver.empty()) return "did what " + giver + " asked" + (place.empty() ? std::string() : " " + at(place, here));
    return {};
}

// What a giver asked for, as someone else says it, from the purpose they stated: "hold the fortress
// above Wenweg with them". Empty for work with no purpose.
std::string asked_for(const Fact& news) {
    const std::string purpose = said(news, "purpose");
    return purpose.empty() ? std::string() : text::retold(text::mid_sentence(text::ask_of(purpose)));
}

// Whether the news is of the speaker's own work, of the speaker, or of what the speaker did: they
// speak of that otherwise. A hero who beat off a threat heard "Orlan beat off the threat" from their
// own mouth (a writer found it, 2026-09-27).
bool own(const Fact& news, const std::string& self) {
    if (self.empty()) return false;
    return said(news, "giver") == self || said(news, "person") == self || said(news, "by") == self;
}

}  // namespace

std::string deed_words(const Fact& news, const std::string& self, std::string_view here) {
    if (news.kind != FactKind::News || said(news, "player") != "yes" || own(news, self)) return {};
    const std::string event = said(news, "event");
    const std::string state = said(news, "state");
    const std::string place = said(news, "place");
    const std::string giver = said(news, "giver");
    if (event == "work") {
        if (state == "finished") return work_done(said(news, "verb"), place, giver, here);
        if (giver.empty()) return {};
        if (state == "taken") return "took on " + giver + "'s work";
        if (state == "failed") return "failed " + giver;
        if (state == "given_up") return "gave up " + giver + "'s work";
        return {};
    }
    if (event == "exposed") {
        const std::string person = said(news, "person");
        const std::string is = text::spaced(said(news, "is"));
        const std::string was = text::spaced(said(news, "was"));
        if (person.empty()) return {};
        if (is.empty() || is == was) return "saw through " + person;
        return "showed that " + person + " is " + a(is);
    }
    if (event == "scare" && !place.empty()) {
        if (state == "calmed") return "calmed the folk " + at(place, here);
        if (state == "beaten") return "beat off the threat " + at(place, here);
    }
    return {};
}

std::string news_words(const Fact& news, const std::string& self, std::string_view stranger, std::string_view here) {
    if (news.kind != FactKind::News || own(news, self)) return {};
    // What the stranger did: "you helped Wraignwight hold", said to them.
    if (const std::string deed = deed_words(news, self, here); !deed.empty()) return std::string(stranger) + " " + deed;
    const std::string event = said(news, "event");
    const std::string state = said(news, "state");
    const std::string place = said(news, "place");
    const std::string giver = said(news, "giver");
    const std::string person = said(news, "person");
    if (event == "work" && !giver.empty()) {
        if (state == "asked") {
            // What the work is for, as its giver said it: "looking for help with Graffen" told a
            // judge nothing a stranger could go and do (2026-09-27).
            if (const std::string ask = asked_for(news); !ask.empty()) return giver + " wants someone to " + ask;
            if (place.empty()) return giver + " is looking for help";
            return giver + " is looking for help " + (!here.empty() && place == here ? std::string("here") : "with " + named(place, here));
        }
        if (state == "lapsed") return "nobody answered " + giver + "'s call for help";
        if (state == "withdrawn") return giver + " no longer asks for help";
        if (state == "target_gone") return giver + "'s work is past doing now";
        if (state == "giver_gone") return giver + " is gone, and their work with them";
        return {};
    }
    if (event == "exposed" && !person.empty()) {
        const std::string is = text::spaced(said(news, "is"));
        const std::string was = text::spaced(said(news, "was"));
        if (is.empty() || was.empty() || is == was) return person + " is not what they seemed";
        return person + ", who passed for " + a(was) + ", is " + a(is);
    }
    if (event == "hero" && !person.empty()) {
        // A hero's deed (contract version 26), as word of it goes round: who did what, against what; and who they
        // are to the speaker, when they are someone of theirs (contract version 28). A sovereign's lost fight in
        // the speaker's own town, told by name alone, was a stranger's (the golden benchmark's round 30).
        const std::string tie = said(news, "tie");
        const std::string who = tie == "sovereign" ? "our sovereign " + person
                                : tie == "ally"    ? person + ", who stands with me,"
                                : tie == "kin"     ? person + ", my own kin,"
                                : tie == "rival"   ? person + ", no friend of mine,"
                                                   : person;
        const std::string verb = said(news, "verb");
        const std::string against = said(news, "against");
        const bool won = state == "won";
        const bool there = !here.empty() && place == here;
        if (verb == "defended" && !place.empty()) {
            if (won) return who + " held " + (there ? "the line here" : named(place, here)) + (against.empty() ? "" : " against " + against);
            // A fight lost where they still stand: never driven from it.
            if (against.empty()) return who + " lost a fight " + (there ? std::string("here") : "at " + named(place, here));
            return who + " fought " + against + " " + (there ? std::string("here") : "at " + named(place, here)) + " and lost";
        }
        if (verb == "hunted" && !against.empty())
            return won ? who + " hunted down " + against : who + " went after " + against + " and was beaten back";
        if (verb == "assaulted" && !place.empty()) {
            if (won) return who + " took " + named(place, here) + (against.empty() ? "" : " from " + against);
            return who + " was thrown back from " + named(place, here) + (against.empty() ? "" : " by " + against);
        }
        return {};
    }
    if (event == "scare" && !place.empty()) {
        if (state == "frightened") return "there was a scare " + at(place, here);
        if (state == "beaten") {
            const std::string by = said(news, "by");
            if (!by.empty() && by != "player") return by + " beat off the threat " + at(place, here);
            return "the threat " + at(place, here) + " was beaten off";
        }
        if (state == "lost") return "the threat " + at(place, here) + " could not be held off";
        if (state == "passed") return "the threat " + at(place, here) + " passed";
        return {};
    }
    return {};
}

namespace {

// When a News fact's state came about, as words that end a clause, from its calendar band.
std::string when_words(const Fact& news) {
    const std::string since = said(news, "since");
    if (since == "today") return "today";
    if (since == "yesterday") return "yesterday";
    if (since == "days") return "a few days back";
    if (since == "weeks") return "some weeks back";
    if (since == "months") return "months back";
    if (since == "years") return "years back";
    return {};
}

// Whether words for this state say when it came about: a deed done, a scare and how it ended, a
// call for help nobody answered; not work still asked for, withdrawn or past doing, and not who
// someone turned out to be, which still holds.
std::string with_when(std::string words, const Fact& news, bool deed) {
    const std::string event = said(news, "event");
    const bool dated = event == "scare" || event == "hero" || (event == "work" && (deed || said(news, "state") == "lapsed")) ||
                       (event == "exposed" && deed);
    const std::string when = when_words(news);
    return words.empty() || !dated || when.empty() ? words : words + " " + when;
}

}  // namespace

std::string dated_deed_words(const Fact& news, const std::string& self, std::string_view here) {
    return with_when(deed_words(news, self, here), news, true);
}

namespace {

// One of the speaker's own deeds in the first person (own_deed_words); told after another in the same breath, its
// foe said as "them" and its place as "it" or "there" when the deed before named them.
std::string own_deed_clause(const Fact& news, const std::string& self, std::string_view here, bool foe_again, bool place_again) {
    if (news.kind != FactKind::News || self.empty() || said(news, "event") != "hero" || said(news, "person") != self) return {};
    const std::string verb = said(news, "verb");
    const std::string place = said(news, "place");
    const std::string against = said(news, "against");
    const bool won = said(news, "state") == "won";
    const bool there = !here.empty() && place == here;
    const std::string foe = foe_again ? std::string("them") : against;
    if (verb == "defended" && !place.empty()) {
        if (won)
            return "we held " + (there ? std::string("the line here") : place_again ? std::string("it") : named(place, here)) +
                   (against.empty() ? "" : " against " + foe);
        const std::string at = there ? std::string("here") : place_again ? std::string("there") : "at " + named(place, here);
        if (against.empty()) return "we lost a fight " + at;
        return "we fought " + foe + " " + at + " and lost";
    }
    if (verb == "hunted" && !against.empty())
        return won ? (foe_again ? std::string("I hunted them down") : "I hunted down " + against) : "I went after " + foe + " and was beaten back";
    if (verb == "assaulted" && !place.empty()) {
        const std::string ruin = place_again ? std::string("it") : named(place, here);
        if (won) return "we took " + ruin + (against.empty() ? "" : " from " + foe);
        return "we were thrown back from " + ruin + (against.empty() ? "" : " by " + foe);
    }
    return {};
}

}  // namespace

std::string own_deed_words(const Fact& news, const std::string& self, std::string_view here) {
    return own_deed_clause(news, self, here, false, false);
}

std::string dated_own_deed_words(const Fact& news, const std::string& self, std::string_view here) {
    return with_when(own_deed_words(news, self, here), news, true);
}

std::string own_story_words(const orchestrator::FactPacket& packet, bool dated) {
    const std::string self = text::spoken(packet.persona.name);
    const std::string here = text::spoken(packet.persona.place);
    std::vector<const Fact*> deeds;  // oldest first: the director gives them newest first
    for (const Fact& f : packet.facts)
        if (!own_deed_words(f, self, here).empty()) deeds.insert(deeds.begin(), &f);
    if (deeds.empty()) return {};
    // The same deed again, one after another, is counted: "we fought them at the fortress and lost twice".
    const auto same = [](const Fact& a, const Fact& b) {
        for (const std::string_view key : {"verb", "place", "against", "state"})
            if (said(a, key) != said(b, key)) return false;
        return true;
    };
    std::vector<std::pair<const Fact*, int>> runs;
    for (const Fact* deed : deeds) {
        if (!runs.empty() && same(*runs.back().first, *deed))
            ++runs.back().second;
        else
            runs.emplace_back(deed, 1);
    }
    std::string out;
    for (std::size_t i = 0; i < runs.size(); ++i) {
        const Fact& deed = *runs[i].first;
        const bool foe_again = i > 0 && !said(deed, "against").empty() && said(deed, "against") == said(*runs[i - 1].first, "against");
        const bool place_again = i > 0 && !said(deed, "place").empty() && said(deed, "place") == said(*runs[i - 1].first, "place");
        std::string clause = own_deed_clause(deed, self, here, foe_again, place_again);
        if (runs[i].second == 2) clause += " twice";
        if (runs[i].second > 2) clause += " three times";
        out += i == 0 ? clause : ", then " + clause;
    }
    // Dated as the newest of them was.
    return dated ? with_when(out, *deeds.back(), true) : out;
}

const Fact* own_deed_of(const orchestrator::FactPacket& packet) {
    const std::string self = text::spoken(packet.persona.name);
    const std::string here = text::spoken(packet.persona.place);
    for (const Fact& f : packet.facts)
        if (!own_deed_words(f, self, here).empty()) return &f;
    return nullptr;
}

std::string took_words(std::string_view reaction) {
    if (reaction == "humbled") return "it humbled you; you own your part in it";
    if (reaction == "embittered") return "it embittered you; you blame others for it, and it has hardened you";
    if (reaction == "resolved") return "it steeled you; you mean to learn from it and do better";
    if (reaction == "grieving") return "it grieves you; you mourn what it cost";
    if (reaction == "vengeful") return "you want the foe that beat you, and mean to go after them";
    if (reaction == "vindicated") return "it proved you right; you are surer of yourself for it";
    return {};
}

std::string dated_news_words(const Fact& news, const std::string& self, std::string_view stranger, std::string_view here) {
    return with_when(news_words(news, self, stranger, here), news, !deed_words(news, self, here).empty());
}

bool brought_up(const Fact& news, const orchestrator::FactPacket& packet) {
    using orchestrator::ActKind;
    static const std::vector<std::string> kBands = {"today", "yesterday", "days", "weeks", "months", "years"};
    const auto band = std::find(kBands.begin(), kBands.end(), said(news, "since"));
    if (band == kBands.end()) return true;  // undated: as fresh
    auto age = static_cast<int>(band - kBands.begin());
    // Asked about its place or its person, a speaker looks a band further back; and a hero's own deed stays
    // with them a band longer than what they heard of others (contract version 26): a castellan who had
    // fought and lost at their fortress six days before said nothing of it, and passed on another's news.
    const bool about = packet.outcome == orchestrator::Outcome::Answered &&
                       (packet.answering == ActKind::AskAboutPlace || packet.answering == ActKind::AskAboutPerson);
    const bool own = !own_deed_words(news, text::spoken(packet.persona.name), text::spoken(packet.persona.place)).empty();
    // Another hero's deed, of someone who is nothing to the speaker and elsewhere, is not brought up unasked
    // (contract version 28): asked for news, it is told. Brought up, strangers' fights came as bulletins of names no
    // one knew, and one was taken for the speaker's own side of a fight (the golden benchmark's round 30). A fight
    // in the speaker's own place is theirs to speak of, whoever fought it.
    if (!own && !about && said(news, "event") == "hero" && said(news, "tie").empty() &&
        said(news, "place") != text::spoken(packet.persona.place))
        return false;
    if (about || own) age = std::max(0, age - 1);
    if (age <= 1) return true;
    if (age >= 4) return false;
    // The same speaker decides the same way for the same news: a hash, never a roll (FNV-1a).
    std::uint64_t hash = 1469598103934665603ull;
    for (const char c : packet.speaker + "|" + news.subject) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ull;
    }
    return age == 2 ? hash % 2 == 0 : hash % 5 == 0;
}

std::vector<std::string> news_forms(const Fact& news, const std::string& self, std::string_view here) {
    std::vector<std::string> out;
    const std::string ask = said(news, "state") == "asked" ? asked_for(news) : std::string();
    for (std::string form : {news_words(news, self, "you", here), deed_words(news, self, here), ask})
        if (!form.empty()) out.push_back(std::move(form));
    return out;
}

std::vector<const Fact*> news_of(const orchestrator::FactPacket& packet) {
    std::vector<const Fact*> out;
    for (const Fact& fact : packet.facts)
        if (fact.kind == FactKind::News) out.push_back(&fact);
    return out;
}

bool may_pass_on(const Fact& news, const orchestrator::FactPacket& packet) {
    using orchestrator::ActKind;
    if (news.kind != FactKind::News) return false;
    // What happened to the speaker is not news they heard: the one frightened, or talked down, or
    // who beat the threat off themself, says so in their own lines, never "some are saying" (writers
    // found hearsay said of what the speaker saw, 2026-09-27).
    if (said(news, "how") == "there") return false;
    // An answer about a place says the work there already: "Wenisk wants someone to stand with
    // Iskmor" then "The word is Wenisk wants someone to stand with Iskmor" says it twice; shown both,
    // the tuned voice model gave the work to the wrong giver (2026-09-27).
    const bool about = packet.outcome == orchestrator::Outcome::Answered &&
                       (packet.answering == ActKind::AskAboutPlace || packet.answering == ActKind::AskAboutPerson);
    if (about && said(news, "state") == "asked") return false;
    return !news_words(news, text::spoken(packet.persona.name), "you", text::spoken(packet.persona.place)).empty();
}

const Fact* news_to_pass_on(const orchestrator::FactPacket& packet, bool of_the_stranger) {
    const std::string self = text::spoken(packet.persona.name);
    const std::string here = text::spoken(packet.persona.place);
    for (const Fact* heard : news_of(packet))
        if (may_pass_on(*heard, packet) && (!of_the_stranger || !deed_words(*heard, self, here).empty())) return heard;
    return nullptr;
}

}  // namespace communicator
