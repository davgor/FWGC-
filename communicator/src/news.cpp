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

// A piece of work done, as what the stranger did: by its verb where the place is named, else what
// its giver asked.
std::string work_done(const std::string& verb, const std::string& place, const std::string& giver) {
    if (!place.empty()) {
        if (verb == "defend") return "helped " + place + " hold";
        if (verb == "reclaim") return "took back " + place;
        if (verb == "recover") return "recovered " + place;
        if (verb == "slay") return "cleared out " + place;
        if (verb == "cleanse") return "cleansed " + place;
        if (verb == "rebuild") return "rebuilt " + place;
    }
    if (!giver.empty()) return "did what " + giver + " asked" + (place.empty() ? std::string() : " at " + place);
    return {};
}

// Whether the news is of the speaker's own work, or of the speaker: they speak of that otherwise.
bool own(const Fact& news, const std::string& self) {
    if (self.empty()) return false;
    return said(news, "giver") == self || said(news, "person") == self;
}

}  // namespace

std::string deed_words(const Fact& news, const std::string& self) {
    if (news.kind != FactKind::News || said(news, "player") != "yes" || own(news, self)) return {};
    const std::string event = said(news, "event");
    const std::string state = said(news, "state");
    const std::string place = said(news, "place");
    const std::string giver = said(news, "giver");
    if (event == "work") {
        if (state == "finished") return work_done(said(news, "verb"), place, giver);
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
        if (state == "calmed") return "calmed the folk at " + place;
        if (state == "beaten") return "beat off the threat at " + place;
    }
    return {};
}

std::string news_words(const Fact& news, const std::string& self, std::string_view stranger) {
    if (news.kind != FactKind::News || own(news, self)) return {};
    // What the stranger did: "you helped Wraignwight hold", said to them.
    if (const std::string deed = deed_words(news, self); !deed.empty()) return std::string(stranger) + " " + deed;
    const std::string event = said(news, "event");
    const std::string state = said(news, "state");
    const std::string place = said(news, "place");
    const std::string giver = said(news, "giver");
    const std::string person = said(news, "person");
    if (event == "work" && !giver.empty()) {
        if (state == "asked") return giver + " is looking for help" + (place.empty() ? std::string() : " with " + place);
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
        if (state == "frightened") return "there was a scare at " + place;
        if (state == "beaten") {
            const std::string by = said(news, "by");
            if (!by.empty() && by != "player") return by + " beat off the threat at " + place;
            return "the threat at " + place + " was beaten off";
        }
        if (state == "lost") return "the threat at " + place + " could not be held off";
        if (state == "passed") return "the threat at " + place + " passed";
        return {};
    }
    return {};
}

std::vector<const Fact*> news_of(const orchestrator::FactPacket& packet) {
    std::vector<const Fact*> out;
    for (const Fact& fact : packet.facts)
        if (fact.kind == FactKind::News) out.push_back(&fact);
    return out;
}

const Fact* news_to_pass_on(const orchestrator::FactPacket& packet, bool of_the_stranger) {
    using orchestrator::ActKind;
    const std::string self = text::spoken(packet.persona.name);
    const bool asked = packet.outcome == orchestrator::Outcome::Answered &&
                       (packet.answering == ActKind::AskAboutPlace || packet.answering == ActKind::AskAboutPerson);
    for (const Fact* heard : news_of(packet)) {
        // An answer about a place says the work there already: "Wenisk wants someone to stand with
        // Iskmor" then "The word is Wenisk is looking for help with Iskmor" says it twice.
        if (asked && said(*heard, "state") == "asked") continue;
        if (!(of_the_stranger ? deed_words(*heard, self) : news_words(*heard, self)).empty()) return heard;
    }
    return nullptr;
}

}  // namespace communicator
