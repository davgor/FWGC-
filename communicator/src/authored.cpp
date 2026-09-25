#include "communicator/authored.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>

#include "communicator/error.hpp"
#include "communicator/known.hpp"
#include "reply_shape.hpp"
#include "text.hpp"

namespace communicator {

namespace {

using orchestrator::ActKind;
using orchestrator::Fact;
using orchestrator::FactKind;
using orchestrator::FactPacket;
using orchestrator::Outcome;

// The blanks every situation may use: who the speaker is, and where they stand.
const std::vector<std::string>& speaker_blanks() {
    static const std::vector<std::string> blanks = {"speaker", "post", "place"};
    return blanks;
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

// How far something is, as said: "right here", "2.5 km off".
std::string how_far(const Fact& fact) {
    const std::string distance = field_of(fact, "distance");
    if (distance.empty()) return {};
    return distance == "here" ? "right here" : distance + " off";
}

// A place as one phrase: its name, how far, and who holds it or what became of it.
std::string place_phrase(const Fact& place) {
    std::string out = text::spoken(label_of(place));
    const std::string far = how_far(place);
    if (!far.empty()) out += ", " + far;
    const std::string occupant = text::spaced(field_of(place, "occupant"));
    const std::string state = field_of(place, "state");
    if (!occupant.empty())
        out += ", held by " + occupant;
    else if (state == "ruined" || state == "abandoned")
        out += ", " + state;
    return out;
}

// A nest as one phrase: "Hippogryphs nesting 4.9 km off".
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

// Someone else's offer: who wants it done, and what.
std::string others_offer_phrase(const Fact& offer) {
    std::string who = text::spoken(field_of(offer, "giver"));
    if (who.empty()) who = "someone";
    const std::string role = text::spaced(text::spoken(field_of(offer, "giver_role")));
    if (!role.empty()) who += " the " + role;
    const std::string purpose = retold(mid_sentence(ask_of(text::spoken(field_of(offer, "purpose")))));
    return purpose.empty() ? who + " is hiring" : who + " wants someone to " + purpose;
}

std::string joined(const std::vector<std::string>& parts, std::string_view between, std::string_view last) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i)
        out += (i == 0 ? "" : std::string(i + 1 == parts.size() ? last : between)) + parts[i];
    return out;
}

std::vector<const Fact*> of_kind(const FactPacket& p, FactKind kind) {
    std::vector<const Fact*> out;
    for (const Fact& f : p.facts)
        if (f.kind == kind) out.push_back(&f);
    return out;
}

bool own_offer(const Fact& f) { return f.kind == FactKind::QuestOffer && field_of(f, "from_speaker") == "yes"; }

// Every blank this packet can fill, for its situation.
std::map<std::string, std::string> blanks_of(const FactPacket& p, const std::string& situation) {
    std::map<std::string, std::string> out;
    const auto set = [&](const std::string& key, std::string value) {
        if (!value.empty()) out[key] = std::move(value);
    };
    set("speaker", text::spoken(p.persona.name));
    set("post", text::spaced(text::spoken(p.persona.role)));
    set("place", text::spoken(p.persona.place));

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
    if (situation == "about_place" && !places.empty()) set("about", place_phrase(*places.front()));
    if (situation == "surroundings") {
        // Nearest first, as the director lists them, leaving out where the speaker stands.
        std::vector<std::string> away;
        for (const Fact* place : places)
            if (field_of(*place, "distance") != "here") away.push_back(place_phrase(*place));
        if (!away.empty()) set("nearest", away.front());
        // Each place's phrase has commas of its own, so the last is joined by ", and".
        if (away.size() > 1)
            set("others", joined(std::vector<std::string>(away.begin() + 1, away.begin() + std::min<std::size_t>(away.size(), 3)),
                                 ", ", ", and "));
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

// `line` with its blanks filled, or nothing when the packet cannot fill one.
std::optional<std::string> filled(const std::string& line, const std::map<std::string, std::string>& blanks) {
    std::string out;
    std::size_t i = 0;
    while (i < line.size()) {
        if (line[i] != '{') {
            out += line[i++];
            continue;
        }
        const std::size_t close = line.find('}', i);
        const auto found = blanks.find(line.substr(i + 1, close - i - 1));
        if (found == blanks.end()) return std::nullopt;
        out += found->second;
        i = close + 1;
    }
    // Each sentence starts with a capital, however a blank that opens one began.
    bool start = true;
    for (std::size_t k = 0; k < out.size(); ++k) {
        const char c = out[k];
        if (start && c >= 'a' && c <= 'z') out[k] = static_cast<char>(c - 'a' + 'A');
        if (c != ' ') start = false;
        if ((c == '.' || c == '!' || c == '?') && (k + 1 == out.size() || out[k + 1] == ' ')) start = true;
    }
    return out;
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

}  // namespace

const std::map<std::string, std::vector<std::string>>& Lines::situations() {
    static const std::map<std::string, std::vector<std::string>> table = {
        {"about_self", {}},
        {"about_person", {"name", "role", "where"}},
        {"about_place", {"about"}},
        {"surroundings", {"nearest", "others", "danger"}},
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
    };
    return table;
}

std::string Lines::situation(const FactPacket& p) {
    switch (p.outcome) {
        case Outcome::Answered:
            switch (p.answering) {
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
    }
    return {};
}

Lines Lines::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw Error("no lines file at " + path);
    std::stringstream text;
    text << in.rdbuf();
    return parse(text.str(), path);
}

Lines Lines::parse(std::string_view text, const std::string& source) {
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);  // a byte-order mark an editor wrote
    std::stringstream in{std::string(text)};
    Lines out;
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
        out.banks_[cells[0]][cells[1]].push_back(cells[2]);
    }
    return out;
}

std::vector<std::string> Lines::replies(const FactPacket& p) const {
    const std::string s = situation(p);
    if (s.empty()) return {};
    const std::map<std::string, std::string> blanks = blanks_of(p, s);
    const std::string closing = reply_shape(p).closing;
    std::vector<std::string> voices;
    if (!p.persona.voice_key.empty()) voices.push_back(p.persona.voice_key);
    if (!p.persona.role.empty()) voices.push_back("post:" + p.persona.role);
    voices.push_back("any");
    for (const std::string& voice : voices) {
        const auto bank = banks_.find(voice);
        if (bank == banks_.end()) continue;
        const auto lines = bank->second.find(s);
        if (lines == bank->second.end()) continue;
        std::vector<std::string> out;
        for (const std::string& line : lines->second)
            if (const std::optional<std::string> reply = filled(line, blanks)) out.push_back(*reply + (closing.empty() ? "" : " " + closing));
        if (!out.empty()) return out;
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
