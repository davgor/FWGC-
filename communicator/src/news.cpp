#include "news.hpp"

#include <cctype>

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
