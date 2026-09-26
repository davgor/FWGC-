#include "orchestrator/contract.hpp"

#include <array>

namespace orchestrator {

namespace {

constexpr std::array<std::string_view, kActCount> kActNames = {
    "ask_surroundings", "ask_about_place", "ask_about_person", "ask_for_work", "accept_quest", "decline_quest",
    "abandon_quest",    "farewell",        "no_act",           "greet",        "small_talk",   "answer",
    "thank",            "insult",          "ask_safety",       "confront"};

constexpr std::array<std::string_view, kOutcomeCount> kOutcomeNames = {
    "answered", "accepted", "declined",     "abandoned", "unknown",  "refused",  "parted",
    "not_understood", "greeted", "chatted", "acknowledged", "thanked", "offended", "exposed", "denied"};

constexpr std::array<std::string_view, 10> kFactNames = {"place",  "person", "quest_offer", "quest_state", "danger",
                                                         "reason", "memory", "chat",        "safety",      "reveal"};

static_assert(kFactNames.size() == static_cast<std::size_t>(FactKind::Reveal) + 1, "one name per fact kind");

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

}  // namespace orchestrator
