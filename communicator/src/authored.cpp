#include "communicator/authored.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <utility>

#include "communicator/error.hpp"
#include "communicator/known.hpp"
#include "communicator/voice.hpp"
#include "memory.hpp"
#include "reply_shape.hpp"
#include "text.hpp"

namespace communicator {

namespace {

using orchestrator::ActKind;
using orchestrator::Fact;
using orchestrator::FactKind;
using orchestrator::FactPacket;
using orchestrator::Outcome;

// The blanks every situation may use: who the speaker is, where they stand, and a saying of their
// people.
const std::vector<std::string>& speaker_blanks() {
    static const std::vector<std::string> blanks = {"speaker", "post", "place", "saying"};
    return blanks;
}

// The blanks a line says that make it no fuller than a line without them: the speaker's own, and
// those filled whenever their situation is (how long since a meeting, the season, the time of
// day, a saying). Counted, a line saying one would silence every sibling that does not.
bool uncounted(const std::string& blank) {
    static const std::set<std::string> blanks = {"speaker", "post", "place", "saying", "since", "season", "time"};
    return blanks.count(blank) != 0;
}

std::string trimmed(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    return std::string(text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1));
}

std::string field_of(const Fact& fact, std::string_view key) {
    const std::string* value = fact.field(key);
    return value ? *value : std::string();
}

// What an offer asks, from its stated purpose: the order after any reason given first. "Our
// fields lie in its reach: drive off the wyrm." -> "drive off the wyrm.". Every purpose the
// generator writes is an order (2026-09-24), so the ask begins with its verb.
std::string ask_of(const std::string& purpose) {
    const std::size_t colon = purpose.rfind(": ");
    return colon == std::string::npos ? purpose : purpose.substr(colon + 2);
}

// A phrase from a packet's text, set in mid-sentence: its first letter lowered, its end stop
// dropped. "Hold the fortress with me." -> "hold the fortress with me".
std::string mid_sentence(std::string text) {
    while (!text.empty() && (text.back() == '.' || text.back() == ' ')) text.pop_back();
    if (!text.empty() && text[0] >= 'A' && text[0] <= 'Z' && !(text.size() > 1 && text[1] >= 'A' && text[1] <= 'Z'))
        text[0] = static_cast<char>(text[0] - 'A' + 'a');
    return text;
}

// How far something is, in words: "right here", "a short walk away". The packet says it only
// roughly (orchestrator/contract.hpp), so no line can read a number out, or round it wrong.
std::string how_far(const Fact& fact) { return text::range_words(field_of(fact, "range")); }

// How dangerous a place is, when it is worth a warning: "dangerous" at threat 4, "deadly" at 5.
std::string danger_word(const Fact& place) {
    const std::string threat = field_of(place, "threat");
    if (threat == "5") return "deadly";
    if (threat == "4") return "dangerous";
    return {};
}

// A place as one phrase: its name, how far, how dangerous, and who holds it or what became of
// it. A name already said is another of its kind the second time: "another hamlet of Wenweg".
// Without `with_range`, how far is left for the caller to say once for several places.
std::string place_phrase(const Fact& place, std::set<std::string>& said, bool with_range = true) {
    std::string out = text::spoken(label_of(place));
    if (!said.insert(out).second && out.rfind("the ", 0) == 0) out = "another " + out.substr(4);
    const std::string far = how_far(place);
    if (with_range && !far.empty()) out += ", " + far;
    const std::string danger = danger_word(place);
    if (!danger.empty()) out += ", " + danger;
    const std::string occupant = text::spaced(field_of(place, "occupant"));
    const std::string state = field_of(place, "state");
    if (!occupant.empty())
        out += ", held by " + occupant;
    else if (state == "ruined" || state == "abandoned")
        out += ", " + state;
    return out;
}

// A nest as one phrase: "Hippogryphs nesting an hour's walk away".
std::string danger_phrase(const Fact& nest) {
    std::string out = text::spoken(field_of(nest, "creature"));
    if (out.empty()) out = "something";
    const std::string far = how_far(nest);
    return far.empty() ? out + " nesting nearby" : out + " nesting " + far;
}

// The speaker's own offer: its purpose in the speaker's words, how hard, how far.
std::string own_offer_phrase(const Fact& offer) {
    std::string out = mid_sentence(ask_of(text::spoken(field_of(offer, "purpose"))));
    const std::string hard = text::scale_word("difficulty", field_of(offer, "difficulty"));
    if (!hard.empty()) out += ", " + hard + " work";
    const std::string far = how_far(offer);
    if (!far.empty()) out += ", " + far;
    return out;
}

// A giver's words retold by someone else: "with me" is "with them" in another's mouth.
std::string retold(const std::string& text) {
    static const std::map<std::string, std::string> shifts = {
        {"me", "them"}, {"my", "their"}, {"mine", "theirs"}, {"myself", "themselves"}};
    std::string out, word;
    const auto flush = [&] {
        const auto shift = shifts.find(word);
        out += shift == shifts.end() ? word : shift->second;
        word.clear();
    };
    for (const char c : text) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            word += c;
        } else {
            flush();
            out += c;
        }
    }
    flush();
    return out;
}

// Someone else's offer: who wants it done, and what. A giver is "the reeve", or "of the council"
// when the role names a body they sit on.
std::string others_offer_phrase(const Fact& offer) {
    static const std::set<std::string> bodies = {"council"};
    std::string who = text::spoken(field_of(offer, "giver"));
    if (who.empty()) who = "someone";
    const std::string role = text::spaced(text::spoken(field_of(offer, "giver_role")));
    if (!role.empty()) who += (bodies.count(role) ? " of the " : " the ") + role;
    const std::string purpose = retold(mid_sentence(ask_of(text::spoken(field_of(offer, "purpose")))));
    return purpose.empty() ? who + " is hiring" : who + " wants someone to " + purpose;
}

std::string joined(const std::vector<std::string>& parts, std::string_view between, std::string_view last) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i)
        out += (i == 0 ? "" : std::string(i + 1 == parts.size() ? last : between)) + parts[i];
    return out;
}

// The places after the nearest, as one phrase. Two at the same range share it, said once: "the
// burnt works, held by squatters, and the hamlet of Wenweg, both an hour's walk away". A place
// with details of its own goes first, closed by a comma: last, "the hamlet of Wenweg and the
// deeps, deadly, held by monsters" read as if both were deadly; unclosed, "held by monsters and
// another hamlet" as if the hamlet were held.
std::string others_phrase(const std::vector<const Fact*>& places, std::set<std::string>& said) {
    if (places.size() == 2 && !how_far(*places[0]).empty() && how_far(*places[0]) == how_far(*places[1])) {
        const auto detailed = [&said](const Fact& place) {
            std::set<std::string> dry = said;  // "another" is decided when the phrase is said
            return place_phrase(place, dry, false).find(',') != std::string::npos;
        };
        const Fact* first_place = places[0];
        const Fact* second_place = places[1];
        if (!detailed(*first_place) && detailed(*second_place)) std::swap(first_place, second_place);
        const std::string first = place_phrase(*first_place, said, false);
        const std::string and_then = first.find(',') == std::string::npos ? " and " : ", and ";
        return first + and_then + place_phrase(*second_place, said, false) + ", both " + how_far(*first_place);
    }
    std::vector<std::string> out;
    for (const Fact* place : places) out.push_back(place_phrase(*place, said));
    // Each place's phrase has commas of its own, so the last is joined by ", and".
    return joined(out, ", ", ", and ");
}

std::vector<const Fact*> of_kind(const FactPacket& p, FactKind kind) {
    std::vector<const Fact*> out;
    for (const Fact& f : p.facts)
        if (f.kind == kind) out.push_back(&f);
    return out;
}

bool own_offer(const Fact& f) { return f.kind == FactKind::QuestOffer && field_of(f, "from_speaker") == "yes"; }

// A field of the packet's first fact of `kind`, or empty.
std::string first_field(const FactPacket& p, FactKind kind, std::string_view key) {
    for (const Fact& f : p.facts)
        if (f.kind == kind) return field_of(f, key);
    return {};
}

// The weather small talk is about, as the situation it picks: fair, wet or storm.
std::string weather_class(const FactPacket& p) {
    const std::string weather = first_field(p, FactKind::Chat, "weather");
    if (weather == "stormy" || weather == "a great storm" || first_field(p, FactKind::Chat, "wind") == "a gale") return "storm";
    if (weather == "unsettled") return "wet";
    if (weather == "clear" || weather == "fair") return "fair";
    return {};
}

// The worst nest: the highest danger, the nearest of those.
const Fact* worst_nest(const FactPacket& p) {
    const Fact* worst = nullptr;
    for (const Fact& nest : p.facts)
        if (nest.kind == FactKind::Danger && (!worst || field_of(nest, "danger") > field_of(*worst, "danger"))) worst = &nest;
    return worst;
}

// Every blank this packet can fill, for its situation. `saying` is one of the speaker's people's
// sayings, or empty when their people has none.
std::map<std::string, std::string> blanks_of(const FactPacket& p, const std::string& situation, const std::string& saying) {
    std::map<std::string, std::string> out;
    const auto set = [&](const std::string& key, std::string value) {
        if (!value.empty()) out[key] = std::move(value);
    };
    set("speaker", text::spoken(p.persona.name));
    set("post", text::spaced(text::spoken(p.persona.role)));
    set("place", text::spoken(p.persona.place));
    set("saying", saying);

    // What the speaker remembers, for a greeting: when they last met, what was asked then.
    const auto starts = [&](std::string_view prefix) { return situation.rfind(prefix, 0) == 0; };
    if (starts("greeted_")) {
        const Remembered m = remembered(p);
        set("since", since_words(m.since));
        set("asked", text::spoken(m.asked_before));
    }
    if (starts("small_talk")) {
        set("season", first_field(p, FactKind::Chat, "season"));
        set("time", first_field(p, FactKind::Chat, "time"));
        if (const Fact* nest = worst_nest(p)) set("danger", danger_phrase(*nest));
    }
    if (starts("exposed")) {
        set("was", text::spaced(text::spoken(first_field(p, FactKind::Reveal, "was"))));
        set("is", text::spaced(text::spoken(first_field(p, FactKind::Reveal, "is"))));
    }
    if (starts("safety_")) {
        // The director names the places a verdict rests on first (orchestrator/contract.hpp).
        const std::vector<const Fact*> named = of_kind(p, FactKind::Place);
        std::set<std::string> said;
        if (!named.empty()) set("worst", place_phrase(*named.front(), said));
        if (named.size() > 1) set("others", others_phrase(std::vector<const Fact*>(named.begin() + 1, named.end()), said));
        if (const Fact* nest = worst_nest(p)) set("danger", danger_phrase(*nest));
    }

    const std::vector<const Fact*> people = of_kind(p, FactKind::Person);
    if (situation == "about_person" && !people.empty()) {
        set("name", text::spoken(field_of(*people.front(), "name")));
        set("role", text::spaced(text::spoken(field_of(*people.front(), "role"))));
        set("where", text::spoken(field_of(*people.front(), "where")));
    }
    // Whose the work is: the one person the refusal names.
    if ((situation == "refused_give" || situation == "refused_release") && people.size() == 1)
        set("giver", text::spoken(field_of(*people.front(), "name")));
    // Why the world would not have it, in the director's words ("the offer has lapsed").
    if (situation == "refused")
        for (const Fact* reason : of_kind(p, FactKind::Reason)) set("reason", mid_sentence(text::spoken(field_of(*reason, "reason"))));

    const std::vector<const Fact*> places = of_kind(p, FactKind::Place);
    std::set<std::string> said;
    if (situation == "about_place" && !places.empty()) set("about", place_phrase(*places.front(), said));
    if (situation == "surroundings") {
        // Where the speaker stands, by name; then the rest, nearest first, as the director lists
        // them.
        std::vector<const Fact*> away;
        for (const Fact* place : places) {
            if (field_of(*place, "range") != "here") {
                away.push_back(place);
            } else if (!out.count("here")) {
                set("here", text::spoken(label_of(*place)));
                said.insert(text::spoken(label_of(*place)));
            }
        }
        // The nearest three, and the most dangerous place the speaker knows even when it is
        // further: asked whether it is safe, a speaker names the worst of it.
        std::vector<const Fact*> named(away.begin(), away.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(away.size(), 3)));
        const Fact* deadliest = nullptr;
        for (const Fact* place : away)
            if (!danger_word(*place).empty() && (!deadliest || field_of(*place, "threat") > field_of(*deadliest, "threat")))
                deadliest = place;
        if (deadliest && std::find(named.begin(), named.end(), deadliest) == named.end()) {
            if (named.size() == 3)
                named.back() = deadliest;
            else
                named.push_back(deadliest);
        }
        if (!named.empty()) set("nearest", place_phrase(*named.front(), said));
        if (named.size() > 1) set("others", others_phrase(std::vector<const Fact*>(named.begin() + 1, named.end()), said));
        // The worst nest: the highest danger, the nearest of those.
        const Fact* worst = nullptr;
        for (const Fact* nest : of_kind(p, FactKind::Danger))
            if (!worst || field_of(*nest, "danger") > field_of(*worst, "danger")) worst = nest;
        if (worst) set("danger", danger_phrase(*worst));
    }
    if (situation == "work_own" || situation == "work_others") {
        std::vector<std::string> own, others;
        for (const Fact* offer : of_kind(p, FactKind::QuestOffer))
            (own_offer(*offer) ? own : others).push_back(own_offer(*offer) ? own_offer_phrase(*offer) : others_offer_phrase(*offer));
        if (!own.empty()) set("own", joined(own, "; ", "; and "));
        if (others.size() > 2) others.resize(2);
        if (!others.empty()) set("others", joined(others, ", ", ", and "));
    }
    return out;
}

// A line filled from a packet: its text, and which of its situation's blanks it says, each once
// however often it says it (the uncounted ones aside).
struct Filled {
    std::string text;
    std::set<std::string> said;
};

// `text` with its blanks filled, or nothing when the packet cannot fill one.
std::optional<Filled> fill_blanks(std::string_view text, const std::map<std::string, std::string>& blanks) {
    Filled out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] != '{') {
            out.text += text[i++];
            continue;
        }
        const std::size_t close = text.find('}', i);
        const std::string name(text.substr(i + 1, close - i - 1));
        const auto found = blanks.find(name);
        if (found == blanks.end()) return std::nullopt;
        out.text += found->second;
        if (!uncounted(name)) out.said.insert(name);
        i = close + 1;
    }
    return out;
}

// `line` with its blanks filled, or nothing when the packet cannot fill one. An optional part, in
// [brackets], is left out whole when the packet cannot fill a blank in it.
std::optional<Filled> filled(const std::string& line, const std::map<std::string, std::string>& blanks) {
    Filled out;
    std::size_t i = 0;
    while (i < line.size()) {
        const std::size_t open = line.find('[', i);
        const std::optional<Filled> plain =
            fill_blanks(std::string_view(line).substr(i, open == std::string::npos ? std::string::npos : open - i), blanks);
        if (!plain) return std::nullopt;
        out.text += plain->text;
        out.said.insert(plain->said.begin(), plain->said.end());
        if (open == std::string::npos) break;
        const std::size_t close = line.find(']', open);
        if (const std::optional<Filled> part = fill_blanks(std::string_view(line).substr(open + 1, close - open - 1), blanks)) {
            out.text += part->text;
            out.said.insert(part->said.begin(), part->said.end());
        }
        i = close + 1;
    }
    // Each sentence starts with a capital, however a blank that opens one began.
    bool start = true;
    for (std::size_t k = 0; k < out.text.size(); ++k) {
        const char c = out.text[k];
        if (start && c >= 'a' && c <= 'z') out.text[k] = static_cast<char>(c - 'a' + 'A');
        if (c != ' ') start = false;
        if ((c == '.' || c == '!' || c == '?') && (k + 1 == out.text.size() || out.text[k + 1] == ' ')) start = true;
    }
    return out;
}

// Why a line's optional parts cannot be read, or empty when they can: each is closed, holds a
// blank, and holds no other.
std::string optional_parts_problem(const std::string& line) {
    std::size_t open = std::string::npos;
    for (std::size_t k = 0; k < line.size(); ++k) {
        if (line[k] == '[') {
            if (open != std::string::npos) return "an optional part inside another";
            open = k;
        } else if (line[k] == ']') {
            if (open == std::string::npos) return "a ] with no [";
            if (line.find('{', open) > k) return "an optional part with no blank, which is never left out";
            open = std::string::npos;
        }
    }
    return open == std::string::npos ? std::string() : "an optional part with no ]";
}

// The blanks a line uses, by name.
std::vector<std::string> blanks_in(const std::string& line) {
    std::vector<std::string> out;
    for (std::size_t i = line.find('{'); i != std::string::npos; i = line.find('{', i + 1)) {
        const std::size_t close = line.find('}', i);
        if (close == std::string::npos) return {"{"};
        out.push_back(line.substr(i + 1, close - i - 1));
    }
    return out;
}

// FNV-1a: a hash that is the same on every machine and every run.
std::uint64_t stable_hash(std::string_view text) {
    std::uint64_t h = 1469598103934665603ull;
    for (const char c : text) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ull;
    }
    return h;
}

// A line said a second time: "As I said, " before it, its first word lowered when that word is
// one a sentence merely begins with -- never "I", never a name.
std::string repeat_lead(const std::string& line) {
    static const std::vector<std::string> leads = {"As I said", "Again", "As I told you"};
    const std::string& lead = leads[static_cast<std::size_t>(stable_hash(line) % leads.size())];
    std::size_t end = 0;
    while (end < line.size() && std::isalpha(static_cast<unsigned char>(line[end]))) ++end;
    const std::string first = line.substr(0, end);
    const std::vector<std::string>& common = common_capitals();
    std::string rest = line;
    if (!first.empty() && first != "I" && std::find(common.begin(), common.end(), first) != common.end())
        rest[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(rest[0])));
    return lead + ", " + rest;
}

}  // namespace

const std::map<std::string, std::vector<std::string>>& Lines::situations() {
    static const std::map<std::string, std::vector<std::string>> table = {
        {"about_self", {}},
        {"about_person", {"name", "role", "where"}},
        {"about_place", {"about"}},
        {"surroundings", {"here", "nearest", "others", "danger"}},
        {"work_own", {"own", "others"}},
        {"work_others", {"others"}},
        {"work_none", {}},
        {"unknown", {}},
        {"refused_give", {"giver"}},
        {"refused_release", {"giver"}},
        {"refused_gone", {}},
        {"refused", {"reason"}},
        {"accepted", {}},
        {"declined", {}},
        {"abandoned", {}},
        {"parted", {}},
        {"not_understood", {}},
        // Contract version 4: conversation. A greeting by what the speaker remembers.
        {"greeted", {}},
        {"greeted_again", {"since", "asked"}},
        {"greeted_warm", {"since", "asked"}},
        {"greeted_cold", {}},
        {"greeted_taken", {"since"}},
        {"greeted_finished", {"since"}},
        {"greeted_failed", {"since"}},
        {"greeted_abandoned", {"since"}},
        // Small talk, by the weather where the speaker stands.
        {"small_talk", {"season", "time", "danger"}},
        {"small_talk_fair", {"season", "time", "danger"}},
        {"small_talk_wet", {"season", "time", "danger"}},
        {"small_talk_storm", {"season", "time", "danger"}},
        {"small_talk_cold", {}},
        {"acknowledged", {}},
        {"thanked", {}},
        {"thanked_cold", {}},
        {"offended", {}},
        {"offended_again", {}},
        // "Is it safe", by the verdict.
        {"safety_unsafe", {"worst", "others", "danger"}},
        {"safety_wary", {"worst", "others", "danger"}},
        {"safety_safe", {"worst", "others", "danger"}},
        // A hidden face owned to, by which face.
        {"exposed", {"was", "is"}},
        {"exposed_true", {"was", "is"}},
        {"exposed_unleashed", {"was", "is"}},
        {"exposed_second", {"was", "is"}},
        {"denied", {}},
        {"denied_again", {}},
        // Contract version 5: a people's sayings, in the bank `saying:<people>`. Never a reply of
        // its own: a line says one through {saying}.
        {"saying", {}},
    };
    return table;
}

std::string Lines::situation(const FactPacket& p) {
    const std::vector<std::string> chain = situations_for(p);
    return chain.empty() ? std::string() : chain.front();
}

std::vector<std::string> Lines::situations_for(const FactPacket& p) {
    const Remembered m = remembered(p);
    switch (p.outcome) {
        case Outcome::Greeted: {
            // A cold speaker greets coldly whatever came before; otherwise a first meeting, or
            // what came of the speaker's own work, then how warmly they take the stranger. No
            // chain crosses from one greeting's openings (reply_shape) to another's.
            if (m.cold()) return {"greeted_cold"};
            if (!m.again) return {"greeted"};
            std::vector<std::string> chain;
            if (m.work == "taken" || m.work == "finished" || m.work == "failed" || m.work == "abandoned")
                chain.push_back("greeted_" + m.work);
            if (m.warm()) chain.push_back("greeted_warm");
            chain.push_back("greeted_again");
            return chain;
        }
        case Outcome::Chatted: {
            std::vector<std::string> chain;
            if (m.cold()) chain.push_back("small_talk_cold");
            if (const std::string weather = weather_class(p); !weather.empty()) chain.push_back("small_talk_" + weather);
            chain.push_back("small_talk");
            return chain;
        }
        case Outcome::Acknowledged:
            return {"acknowledged"};
        case Outcome::Thanked:
            return {m.cold() ? "thanked_cold" : "thanked"};
        case Outcome::Offended:
            if (m.repeat || m.mood == "hostile") return {"offended_again", "offended"};
            return {"offended"};
        case Outcome::Exposed: {
            const std::string face = first_field(p, FactKind::Reveal, "face");
            if (face == "true" || face == "unleashed" || face == "second") return {"exposed_" + face, "exposed"};
            return {"exposed"};
        }
        case Outcome::Denied:
            if (m.repeat) return {"denied_again", "denied"};
            return {"denied"};
        default: {
            std::string one = single_situation(p);
            if (one.empty()) return {};
            return {one};
        }
    }
}

std::string Lines::single_situation(const FactPacket& p) {
    switch (p.outcome) {
        case Outcome::Answered:
            switch (p.answering) {
                case ActKind::AskSafety: {
                    const std::string verdict = first_field(p, FactKind::Safety, "verdict");
                    if (verdict == "unsafe" || verdict == "wary" || verdict == "safe") return "safety_" + verdict;
                    return {};
                }
                case ActKind::AskSurroundings:
                    return of_kind(p, FactKind::Place).empty() ? std::string() : "surroundings";
                case ActKind::AskAboutPlace:
                    return of_kind(p, FactKind::Place).empty() ? std::string() : "about_place";
                case ActKind::AskAboutPerson: {
                    const std::vector<const Fact*> people = of_kind(p, FactKind::Person);
                    if (people.empty()) return {};
                    return people.front()->subject == p.speaker ? "about_self" : "about_person";
                }
                case ActKind::AskForWork: {
                    const std::vector<const Fact*> offers = of_kind(p, FactKind::QuestOffer);
                    if (offers.empty()) return "work_none";
                    return std::any_of(offers.begin(), offers.end(), [](const Fact* f) { return own_offer(*f); }) ? "work_own"
                                                                                                                 : "work_others";
                }
                default:
                    return {};
            }
        case Outcome::Unknown:
            return "unknown";
        case Outcome::Refused: {
            std::string why;
            for (const Fact* reason : of_kind(p, FactKind::Reason)) why = field_of(*reason, "why");
            if (why == "not_theirs_to_give") return "refused_give";
            if (why == "not_theirs_to_release") return "refused_release";
            if (why == "gone") return "refused_gone";
            return "refused";
        }
        case Outcome::Accepted:
            return "accepted";
        case Outcome::Declined:
            return "declined";
        case Outcome::Abandoned:
            return "abandoned";
        case Outcome::Parted:
            return "parted";
        case Outcome::NotUnderstood:
            return "not_understood";
        case Outcome::Greeted:
        case Outcome::Chatted:
        case Outcome::Acknowledged:
        case Outcome::Thanked:
        case Outcome::Offended:
        case Outcome::Exposed:
        case Outcome::Denied:
            break;  // situations_for chains these
    }
    return {};
}

Lines Lines::load(const std::string& path) {
    // A folder is every `.txt` in it, in name order: one file for each archetype, say.
    std::vector<std::filesystem::path> files;
    std::error_code error;
    if (std::filesystem::is_directory(path, error)) {
        for (const auto& entry : std::filesystem::directory_iterator(path, error))
            if (entry.is_regular_file() && entry.path().extension() == ".txt") files.push_back(entry.path());
        if (error) throw Error("cannot read the lines folder " + path + ": " + error.message());
        if (files.empty()) throw Error("no .txt lines files in " + path);
        std::sort(files.begin(), files.end());
    } else {
        files.push_back(path);
    }
    Lines out;
    for (const std::filesystem::path& file : files) {
        std::ifstream in(file, std::ios::binary);
        if (!in) throw Error("no lines file at " + file.string());
        std::stringstream text;
        text << in.rdbuf();
        out.read(text.str(), file.string());
    }
    return out;
}

Lines Lines::parse(std::string_view text, const std::string& source) {
    Lines out;
    out.read(text, source);
    return out;
}

void Lines::read(std::string_view text, const std::string& source) {
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);  // a byte-order mark an editor wrote
    std::stringstream in{std::string(text)};
    std::string row;
    int number = 0;
    while (std::getline(in, row)) {
        ++number;
        const std::string where = source + ":" + std::to_string(number);
        const std::string content = trimmed(row);
        if (content.empty() || content[0] == '#') continue;
        std::vector<std::string> cells;
        std::stringstream split(content);
        std::string cell;
        while (std::getline(split, cell, '|')) cells.push_back(trimmed(cell));
        if (cells.size() != 3 || cells[0].empty() || cells[2].empty())
            throw Error(where + ": a line is `voice | situation | line`");
        const auto known = situations().find(cells[1]);
        if (known == situations().end()) throw Error(where + ": no situation called " + cells[1]);
        for (const std::string& blank : blanks_in(cells[2])) {
            const bool allowed = std::find(known->second.begin(), known->second.end(), blank) != known->second.end() ||
                                 std::find(speaker_blanks().begin(), speaker_blanks().end(), blank) != speaker_blanks().end();
            if (!allowed) throw Error(where + ": no blank {" + blank + "} in " + cells[1]);
        }
        if (const std::string problem = optional_parts_problem(cells[2]); !problem.empty()) throw Error(where + ": " + problem);
        banks_[cells[0]][cells[1]].push_back(cells[2]);
    }
}

std::vector<std::string> Lines::replies(const FactPacket& p) const {
    const std::vector<std::string> chain = situations_for(p);
    if (chain.empty()) return {};
    const ReplyShape shape = reply_shape(p);
    // Asked the same thing twice in one conversation, a speaker says so before saying it again:
    // an answer with no fixed opening begins "As I said, ...".
    const bool lead = remembered(p).repeat && p.outcome == Outcome::Answered && shape.openings.empty();
    const auto finished = [&](const Filled& f) {
        // A line that named the giver ends with where they are, not with their name again.
        const std::string& closing = f.said.count("giver") ? shape.closing_after_name : shape.closing;
        std::string text = f.text;
        if (lead) text = repeat_lead(text);
        return text + (closing.empty() ? "" : " " + closing);
    };
    std::vector<std::string> voices;
    if (!p.persona.voice_key.empty()) {
        voices.push_back(p.persona.voice_key);
        // An alignment with no bank of its own speaks from its archetype's true-neutral lines,
        // where the archetype shows almost unmodified.
        const std::size_t slash = p.persona.voice_key.find('/');
        if (slash != std::string::npos) {
            const std::string core = p.persona.voice_key.substr(0, slash) + "/NN";
            if (core != p.persona.voice_key) voices.push_back(core);
        }
    }
    if (!p.persona.role.empty()) voices.push_back("post:" + p.persona.role);
    voices.push_back("any");
    // One of the speaker's people's sayings, the same all turn, another the next.
    std::string saying;
    if (const auto people = banks_.find("saying:" + p.persona.people); !p.persona.people.empty() && people != banks_.end())
        if (const auto said = people->second.find("saying"); said != people->second.end() && !said->second.empty())
            saying = said->second[static_cast<std::size_t>(stable_hash(p.speaker + "|" + std::to_string(remembered(p).turn) + "|" +
                                                                       std::to_string(remembered(p).times)) %
                                                           said->second.size())];
    // The speaker's own bank first, and in it the situation that fits most closely: a bank without
    // a line for "greeted_warm" still greets as itself, from "greeted_again".
    for (const std::string& voice : voices) {
        const auto bank = banks_.find(voice);
        if (bank == banks_.end()) continue;
        for (const std::string& s : chain) {
            const auto lines = bank->second.find(s);
            if (lines == bank->second.end()) continue;
            const std::map<std::string, std::string> blanks = blanks_of(p, s, saying);
            std::vector<Filled> fits;
            for (const std::string& line : lines->second)
                if (std::optional<Filled> reply = filled(line, blanks)) fits.push_back(std::move(*reply));
            if (fits.empty()) continue;
            // The lines that say the most of what the packet holds: a speaker who knows of a nest
            // does not leave it out for a shorter line.
            std::size_t most = 0;
            for (const Filled& f : fits) most = std::max(most, f.said.size());
            std::vector<std::string> out;
            for (const Filled& f : fits)
                if (f.said.size() == most) out.push_back(finished(f));
            return out;
        }
    }
    return {};
}

std::string Lines::speak(const FactPacket& p, std::string_view line, std::string_view previous) const {
    const std::vector<std::string> candidates = replies(p);
    if (candidates.empty()) return {};
    const std::uint64_t h = stable_hash(p.speaker + "|" + situation(p) + "|" + std::string(line));
    const auto chosen = static_cast<std::size_t>(h % candidates.size());
    if (candidates[chosen] == previous && candidates.size() > 1) return candidates[(chosen + 1) % candidates.size()];
    return candidates[chosen];
}

std::size_t Lines::size() const {
    std::size_t n = 0;
    for (const auto& [voice, bank] : banks_)
        for (const auto& [situation, lines] : bank) n += lines.size();
    return n;
}

}  // namespace communicator
