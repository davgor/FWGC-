#include "them.hpp"

#include <map>
#include <string_view>

#include "text.hpp"

namespace communicator {

using orchestrator::Fact;
using orchestrator::FactKind;
using orchestrator::FactPacket;

namespace {

std::string field_of(const Fact& f, std::string_view key) {
    const std::string* v = f.field(key);
    return v ? *v : std::string();
}

// A name as said: without the world's bracketed parts ("Iskmor (Age 1)" is "Iskmor").
std::string name_of(const Fact& f, std::string_view key) { return text::spoken(field_of(f, key)); }

// A thing named without an article, lower case, takes "the": "glacier dragons, the doom of Brenlin"
// is "the glacier dragons, the doom of Brenlin"; a proper name keeps none ("Eldkor").
std::string with_article(const std::string& name) {
    if (name.empty() || name.rfind("the ", 0) == 0) return name;
    return (name.front() >= 'a' && name.front() <= 'z') ? "the " + name : name;
}

// A city as its ruins name it: "the ruins of Brenlin" is Brenlin.
std::string city_of(const std::string& place) {
    constexpr std::string_view ruins = "the ruins of ";
    return place.rfind(ruins, 0) == 0 ? place.substr(ruins.size()) : place;
}

// When, after a past deed: nothing for this age, ", in the last age", ", ages ago".
std::string when_after(const std::string& when) {
    if (when == "the last age") return ", in the last age";
    if (when == "ages ago") return ", ages ago";
    return {};
}

std::string joined_names(const std::vector<std::string>& names) {
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i) out += (i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + names[i];
    return out;
}

// The stranger's part in the speaker's work, as the game reported it (contract version 20); empty for a
// part the words do not know.
std::string part_words(const std::string& part) {
    static const std::map<std::string, std::string, std::less<>> parts = {{"fought", "you stood and fought"},
                                                                          {"led", "you led the rest"},
                                                                          {"turned", "you turned the fight"},
                                                                          {"held", "you held when it counted"},
                                                                          {"hurt", "you took a wound for it"}};
    const auto found = parts.find(part);
    return found == parts.end() ? std::string() : found->second;
}

}  // namespace

std::string after_words(const Fact& self) {
    const std::string verb = field_of(self, "after");
    const std::string what = with_article(name_of(self, "after_what"));
    if (what.empty()) return {};
    if (verb == "hold") return "hold " + what;
    if (verb == "retake") return "take back " + what;
    if (verb == "protect") return "keep " + what + " safe";
    if (verb == "kill") return "kill " + what;
    if (verb == "exploit") return "keep the trade of " + what + " turning";
    if (verb == "convert") return "bring " + what + " to the faith";
    return {};
}

std::string standing_words(const Fact& self) {
    static const std::map<std::string, std::string, std::less<>> words = {
        {"council", "sit on the council here"}, {"court", "keep a court here"},          {"reeve", "am reeve here"},
        {"quay", "keep the quay here"},         {"warding", "keep the wards here"},       {"throne", "hold the throne here"},
        {"garrison", "command the garrison here"}, {"altar", "serve at the altar here"},  {"refugee", "am a refugee here"},
        {"captive", "am held here"},            {"command", "hold command here"},         {"hidden", "keep out of sight here"},
        {"cult", "lead the faithful here"}};
    const auto it = words.find(field_of(self, "stands"));
    return it == words.end() ? std::string() : it->second;
}

std::string lost_words(const Fact& self) {
    const std::string city = name_of(self, "lost");
    if (city.empty()) return {};
    static const std::map<std::string, std::string, std::less<>> takers = {
        {"war", "war"}, {"dragon", "a dragon"}, {"monster", "a monster"}, {"magic", "my own magic"}, {"radiance", "the radiance"}};
    const auto taker = takers.find(field_of(self, "lost_to"));
    std::string out = "lost " + city;
    if (taker != takers.end()) out += " to " + taker->second;
    const std::string when = field_of(self, "lost_when");
    if (when == "the last age") out += " in the last age";
    if (when == "ages ago") out += ", ages ago";
    return out;
}

std::string done_words(const Fact& deed) {
    const std::string kind = field_of(deed, "deed");
    const std::string part = field_of(deed, "part");
    const std::string place = name_of(deed, "place");
    const std::string when = when_after(field_of(deed, "when"));
    if (kind == "war") {
        static const std::map<std::string, std::string, std::less<>> reach = {
            {"international", " between realms"}, {"regional", " with our neighbours"}, {"civil", " among our own"}};
        const auto r = reach.find(field_of(deed, "war"));
        const std::string between = r == reach.end() ? std::string() : r->second;
        // Against whom (contract version 22), by the city's name, one since fallen by the name it had: "won a
        // war between realms against Linbrig", "lost a war among our own to Brenlin".
        if (const std::string against = city_of(name_of(deed, "against")); !against.empty()) {
            if (part == "credited") return "won a war" + between + " against " + against + when;
            if (part == "victim") return "lost a war" + between + " to " + against + when;
            return {};
        }
        if (part == "credited") return "won a war" + between + when;
        if (part == "victim") return "lost a war" + between + when;
        return {};
    }
    if (place.empty()) return {};
    if (part == "keeper") {
        if (kind == "fortress") return "command " + place;
        if (kind == "hamlet") return "speak for " + place;
        if (kind == "port") return "run " + place;
        if (kind == "shrine") return "tend " + place;
        if (kind == "ruin") return "keep watch over " + place;
        return {};
    }
    if (kind == "ruin") {
        const std::string city = city_of(place);
        if (part == "credited") return "stood for " + city + " when it fell" + when;
        if (part == "heir") return "am the heir of " + city;
        if (part == "actual") return "was there when " + city + " fell" + when;
        return {};
    }
    if (kind == "founding") return "founded " + place + when;
    if (kind == "nest") return "faced " + with_article(place) + when;
    return {};
}

std::string known_for_words(const Fact& deed) {
    std::string words = done_words(deed);
    if (words.empty()) return {};
    // Said of another: the verb agrees with them, and a war was with their neighbours or among their own.
    static const std::vector<std::pair<std::string, std::string>> of_another = {
        {"command ", "commands "}, {"speak for ", "speaks for "}, {"run ", "runs "}, {"tend ", "tends "},
        {"keep watch over ", "keeps watch over "}, {"am the heir of ", "is the heir of "}};
    for (const auto& [mine, theirs] : of_another)
        if (words.rfind(mine, 0) == 0) {
            words = theirs + words.substr(mine.size());
            break;
        }
    for (const auto& [mine, theirs] :
         {std::pair<std::string, std::string>{" with our neighbours", " with their neighbours"}, {" among our own", " among their own"}})
        if (const std::size_t at = words.find(mine); at != std::string::npos) words.replace(at, mine.size(), theirs);
    return words;
}

std::string far_words(const Fact& deed) {
    const std::string scope = field_of(deed, "scope");
    if (scope == "regional") return "all through these parts";
    if (scope == "realm") return "all through the realm";
    return {};
}

std::string held_words(const Fact& self) {
    const std::string years = field_of(self, "held_years");
    if (years.empty() || years.find_first_not_of("0123456789") != std::string::npos || years.size() > 4) return {};
    const int n = std::stoi(years);
    if (n < 1) return {};
    if (n <= 5) return "a few years";
    if (n <= 9) return "the better part of ten years";
    if (n <= 14) return "ten years and more";
    if (n <= 19) return "near on twenty years";
    if (n <= 24) return "twenty years and more";
    return "near on thirty years";
}

std::string served_words(const Fact& self) {
    const std::string at = name_of(self, "served_at");
    const std::string as = field_of(self, "served_as");
    if (at.empty()) return {};
    if (as == "second" && at.rfind("the fortress", 0) == 0) return "was second in command at " + at;
    if (as == "soldier" || as == "sergeant") return "served as a " + as + " in " + at;
    return {};
}

std::string account_words(const Fact& account, bool beside) {
    std::vector<std::string> clauses;
    const std::string came = name_of(account, "came");
    const std::string many = field_of(account, "many");
    const std::string at = beside ? " at us" : "";
    if (!came.empty() && field_of(account, "came_as") == "person") {
        static const std::map<std::string, std::string, std::less<>> with = {
            {"one", " alone"},
            {"few", ", with a few at their back"},
            {"many", ", with more at their back than I'd have liked"},
            {"horde", ", with more at their back than anyone could count"}};
        const auto w = with.find(many);
        clauses.push_back(came + " came" + at + (w == with.end() ? std::string() : w->second));
    } else if (!came.empty()) {
        // A nest's creature, as the world names them: "Abyssal krakens".
        static const std::map<std::string, std::string, std::less<>> how_many = {
            {"few", ", a few of them"}, {"many", ", more than I'd have liked"}, {"horde", ", more than anyone could count"}};
        const auto h = how_many.find(many);
        clauses.push_back(many == "one" ? "one of the " + came + " came" + at
                                        : came + " came" + at + (h == how_many.end() ? std::string() : h->second));
    }
    static const std::map<std::string, std::pair<std::string, std::string>, std::less<>> costs = {
        {"none", {"we lost no one", "no one was lost"}},
        {"few", {"we lost a few good people", "a few were lost"}},
        {"many", {"we lost too many", "too many were lost"}}};
    if (const auto cost = costs.find(field_of(account, "cost")); cost != costs.end())
        clauses.push_back(beside ? cost->second.first : cost->second.second);
    std::string out;
    for (std::size_t i = 0; i < clauses.size(); ++i) out += (i == 0 ? "" : "; ") + clauses[i];
    if (const std::string part = part_words(field_of(account, "part")); !part.empty()) out += (out.empty() ? "" : ", and ") + part;
    return out;
}

const std::vector<std::string>& account_phrases() {
    static const std::vector<std::string> phrases = [] {
        std::vector<std::string> out = {"we lost no one",  "we lost a few good people", "we lost too many",
                                        "no one was lost", "a few were lost",           "too many were lost"};
        for (const std::string part : {"fought", "led", "turned", "held", "hurt"}) out.push_back(part_words(part));
        return out;
    }();
    return phrases;
}

const Fact* account_of(const FactPacket& packet) {
    for (const Fact& f : packet.facts)
        if (f.kind == FactKind::Account) return &f;
    return nullptr;
}

std::string ties_words(const FactPacket& packet) {
    std::vector<std::string> allies, rivals, kin, lieges;
    for (const Fact& f : packet.facts) {
        if (f.kind != FactKind::Person) continue;
        const std::string tie = field_of(f, "tie");
        const std::string name = name_of(f, "name");
        if (name.empty()) continue;
        if (tie == "ally") allies.push_back(name);
        if (tie == "rival") rivals.push_back(name);
        if (tie == "kin") kin.push_back(name);
        if (tie == "liege") lieges.push_back(field_of(f, "role") == "sovereign" ? name + ", our sovereign" : name);
    }
    std::vector<std::string> parts;
    // The sovereign they are sworn to (contract version 19), first.
    if (!lieges.empty()) parts.push_back("I'm sworn to " + joined_names(lieges));
    if (!allies.empty()) parts.push_back(joined_names(allies) + (allies.size() == 1 ? " stands with me" : " stand with me"));
    if (!kin.empty()) parts.push_back(joined_names(kin) + (kin.size() == 1 ? " is my kin" : " are my kin"));
    if (!rivals.empty()) parts.push_back(joined_names(rivals) + (rivals.size() == 1 ? " is my rival" : " are my rivals"));
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) out += (i == 0 ? "" : "; ") + parts[i];
    return out;
}

std::string foes_words(const FactPacket& packet) {
    std::vector<std::string> rivals;
    for (const Fact& f : packet.facts)
        if (f.kind == FactKind::Person && field_of(f, "tie") == "rival")
            if (const std::string name = name_of(f, "name"); !name.empty()) rivals.push_back(name);
    if (rivals.empty()) return {};
    return joined_names(rivals) + (rivals.size() == 1 ? " is my rival" : " are my rivals");
}

std::string born_words(const Fact& self) { return field_of(self, "since"); }

const Fact* self_of(const FactPacket& packet) {
    for (const Fact& f : packet.facts)
        if (f.kind == FactKind::Self) return &f;
    return nullptr;
}

std::vector<const Fact*> deeds_of(const FactPacket& packet) {
    std::vector<const Fact*> out;
    for (const Fact& f : packet.facts)
        if (f.kind == FactKind::Deed) out.push_back(&f);
    return out;
}

}  // namespace communicator
