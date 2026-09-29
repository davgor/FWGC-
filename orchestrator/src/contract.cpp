#include "orchestrator/contract.hpp"

#include <array>
#include <stdexcept>

namespace orchestrator {

namespace {

constexpr std::array<std::string_view, kActCount> kActNames = {
    "ask_surroundings", "ask_about_place", "ask_about_person", "ask_for_work", "accept_quest", "decline_quest",
    "abandon_quest",    "farewell",        "no_act",           "greet",        "small_talk",   "answer",
    "thank",            "insult",          "ask_safety",       "confront",     "apologize",    "calm",
    "ask_about_them",   "complain"};

constexpr std::array<std::string_view, kOutcomeCount> kOutcomeNames = {
    "answered", "accepted", "declined",     "abandoned", "unknown",  "refused",  "parted",
    "not_understood", "greeted", "chatted", "acknowledged", "thanked", "offended", "exposed", "denied",
    "forgiven", "unwronged", "calmed", "still_afraid", "not_afraid", "heard"};

constexpr std::array<std::string_view, kFactKindCount> kFactNames = {
    "place", "person", "quest_offer", "quest_state", "danger", "reason", "memory", "chat", "safety", "reveal", "news", "recall",
    "self",  "deed",   "account"};

}  // namespace

std::string_view to_string(ActKind kind) { return kActNames[static_cast<std::size_t>(kind)]; }

std::optional<ActKind> act_kind(std::string_view name) {
    for (std::size_t i = 0; i < kActNames.size(); ++i)
        if (kActNames[i] == name) return static_cast<ActKind>(i);
    return std::nullopt;
}

bool act_needs_target(ActKind kind) {
    switch (kind) {
        case ActKind::AskAboutPlace:
        case ActKind::AskAboutPerson:
        case ActKind::AcceptQuest:
        case ActKind::DeclineQuest:
        case ActKind::AbandonQuest:
            return true;
        case ActKind::AskSurroundings:
        case ActKind::AskForWork:
        case ActKind::Farewell:
        case ActKind::NoAct:
        case ActKind::Greet:
        case ActKind::SmallTalk:
        case ActKind::Answer:
        case ActKind::Thank:
        case ActKind::Insult:
        case ActKind::AskSafety:
        case ActKind::Confront:
        case ActKind::Apologize:
        case ActKind::Calm:
        case ActKind::AskAboutThem:
        case ActKind::Complain:
            return false;
    }
    return false;
}

bool act_changes_world(ActKind kind) {
    switch (kind) {
        case ActKind::AcceptQuest:
        case ActKind::AbandonQuest:
            return true;
        case ActKind::AskSurroundings:
        case ActKind::AskAboutPlace:
        case ActKind::AskAboutPerson:
        case ActKind::AskForWork:
        case ActKind::DeclineQuest:
        case ActKind::Farewell:
        case ActKind::NoAct:
        case ActKind::Greet:
        case ActKind::SmallTalk:
        case ActKind::Answer:
        case ActKind::Thank:
        case ActKind::Insult:
        case ActKind::AskSafety:
        case ActKind::Confront:
        case ActKind::Apologize:
        case ActKind::Calm:
        case ActKind::AskAboutThem:
        case ActKind::Complain:
            return false;
    }
    return false;
}

std::string_view to_string(Outcome outcome) { return kOutcomeNames[static_cast<std::size_t>(outcome)]; }

std::string_view to_string(FactKind kind) { return kFactNames[static_cast<std::size_t>(kind)]; }

const std::string* Fact::field(std::string_view key) const {
    for (const auto& [k, v] : fields)
        if (k == key) return &v;
    return nullptr;
}

std::optional<Outcome> outcome_named(std::string_view name) {
    for (std::size_t i = 0; i < kOutcomeNames.size(); ++i)
        if (kOutcomeNames[i] == name) return static_cast<Outcome>(i);
    return std::nullopt;
}

std::optional<FactKind> fact_kind_named(std::string_view name) {
    for (std::size_t i = 0; i < kFactNames.size(); ++i)
        if (kFactNames[i] == name) return static_cast<FactKind>(i);
    return std::nullopt;
}

namespace {

std::string escaped(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '\\')
            out += "\\\\";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else
            out += c;
    }
    return out;
}

std::string unescaped(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 == text.size()) {
            out += text[i];
            continue;
        }
        const char next = text[++i];
        out += next == 'n' ? '\n' : next == 'r' ? '\r' : next;
    }
    return out;
}

// A line's first word, and the rest after the one space that follows it.
std::pair<std::string_view, std::string_view> split_first(std::string_view line) {
    const std::size_t space = line.find(' ');
    if (space == std::string_view::npos) return {line, {}};
    return {line.substr(0, space), line.substr(space + 1)};
}

}  // namespace

std::string packet_text(const FactPacket& packet) {
    std::string out = "packet " + std::to_string(packet.contract_version) + "\n";
    const auto line = [&](std::string_view key, std::string_view value) { out += std::string(key) + " " + escaped(value) + "\n"; };
    line("speaker", packet.speaker);
    line("answering", to_string(packet.answering));
    line("outcome", to_string(packet.outcome));
    const Persona& p = packet.persona;
    line("persona.uid", p.uid);
    line("persona.name", p.name);
    line("persona.role", p.role);
    line("persona.place", p.place);
    line("persona.manner", p.manner);
    line("persona.voice", p.voice);
    line("persona.toward_player", p.toward_player);
    line("persona.line", p.line);
    for (const std::string& never : p.never) line("persona.never", never);
    line("persona.voice_key", p.voice_key);
    line("persona.face", p.face);
    line("persona.people", p.people);
    for (const Fact& fact : packet.facts) {
        out += "fact " + std::string(to_string(fact.kind)) + " " + escaped(fact.subject) + "\n";
        for (const auto& [key, value] : fact.fields) out += "field " + escaped(key) + " " + escaped(value) + "\n";
    }
    return out + "end\n";
}

std::vector<FactPacket> packets_from_text(std::string_view text) {
    std::vector<FactPacket> out;
    std::optional<FactPacket> open;
    std::size_t number = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        std::string_view line = text.substr(start, newline == std::string_view::npos ? std::string_view::npos : newline - start);
        start = newline == std::string_view::npos ? text.size() + 1 : newline + 1;
        ++number;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const auto refuse = [&](const std::string& why) {
            throw std::invalid_argument("packet text, line " + std::to_string(number) + ": " + why);
        };
        const auto [key, rest] = split_first(line);
        if (!open) {
            if (key != "packet") continue;
            open.emplace();
            try {
                open->contract_version = std::stoi(std::string(rest));
            } catch (const std::exception&) {
                refuse("a packet's version is a number, not " + std::string(rest));
            }
            continue;
        }
        FactPacket& p = *open;
        const std::string value = unescaped(rest);
        if (key == "end") {
            out.push_back(std::move(p));
            open.reset();
        } else if (key == "speaker") {
            p.speaker = value;
        } else if (key == "answering") {
            const std::optional<ActKind> kind = act_kind(value);
            if (!kind) refuse("no act called " + value);
            p.answering = *kind;
        } else if (key == "outcome") {
            const std::optional<Outcome> outcome = outcome_named(value);
            if (!outcome) refuse("no outcome called " + value);
            p.outcome = *outcome;
        } else if (key == "persona.uid") {
            p.persona.uid = value;
        } else if (key == "persona.name") {
            p.persona.name = value;
        } else if (key == "persona.role") {
            p.persona.role = value;
        } else if (key == "persona.place") {
            p.persona.place = value;
        } else if (key == "persona.manner") {
            p.persona.manner = value;
        } else if (key == "persona.voice") {
            p.persona.voice = value;
        } else if (key == "persona.toward_player") {
            p.persona.toward_player = value;
        } else if (key == "persona.line") {
            p.persona.line = value;
        } else if (key == "persona.never") {
            p.persona.never.push_back(value);
        } else if (key == "persona.voice_key") {
            p.persona.voice_key = value;
        } else if (key == "persona.face") {
            p.persona.face = value;
        } else if (key == "persona.people") {
            p.persona.people = value;
        } else if (key == "fact") {
            const auto [kind_name, subject] = split_first(rest);
            const std::optional<FactKind> kind = fact_kind_named(kind_name);
            if (!kind) refuse("no fact kind called " + std::string(kind_name));
            p.facts.push_back({*kind, unescaped(subject), {}});
        } else if (key == "field") {
            if (p.facts.empty()) refuse("a field before any fact");
            const auto [field_key, field_value] = split_first(rest);
            p.facts.back().fields.emplace_back(unescaped(field_key), unescaped(field_value));
        } else {
            refuse("no packet line called " + std::string(key));
        }
    }
    if (open) throw std::invalid_argument("packet text: a packet with no end");
    return out;
}

}  // namespace orchestrator
