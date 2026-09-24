#include "orchestrator/contract.hpp"

#include <array>

namespace orchestrator {

namespace {

constexpr std::array<std::string_view, 9> kActNames = {
    "ask_surroundings", "ask_about_place", "ask_about_person", "ask_for_work", "accept_quest",
    "decline_quest",    "abandon_quest",   "farewell",         "no_act"};

constexpr std::array<std::string_view, 8> kOutcomeNames = {"answered", "accepted", "declined",  "abandoned",
                                                           "unknown",  "refused",  "parted",    "not_understood"};

constexpr std::array<std::string_view, 6> kFactNames = {"place", "person", "quest_offer", "quest_state", "danger", "reason"};

static_assert(kActNames.size() == static_cast<std::size_t>(ActKind::NoAct) + 1, "one name per act");
static_assert(kOutcomeNames.size() == static_cast<std::size_t>(Outcome::NotUnderstood) + 1, "one name per outcome");
static_assert(kFactNames.size() == static_cast<std::size_t>(FactKind::Reason) + 1, "one name per fact kind");

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
