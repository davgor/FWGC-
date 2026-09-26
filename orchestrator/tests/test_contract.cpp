// The contract's vocabulary: every name reads back as its value, and nothing else does.
#include "orchestrator/contract.hpp"

#include <set>

#include "check.hpp"

using namespace orchestrator;

TEST(every_act_name_reads_back_as_its_act) {
    std::set<std::string_view> names;
    for (int i = 0; i < kActCount; ++i) {
        const auto kind = static_cast<ActKind>(i);
        const std::optional<ActKind> back = act_kind(to_string(kind));
        check::is_true(back.has_value() && *back == kind, "round trip of " + std::string(to_string(kind)));
        names.insert(to_string(kind));
    }
    check::equal(names.size(), static_cast<std::size_t>(kActCount), "a name of its own for every act");
    check::is_true(!act_kind("attack").has_value(), "a name outside the set reads as nothing");
    check::is_true(!act_kind("").has_value(), "and so does an empty one");
}

TEST(every_outcome_and_fact_kind_has_a_name_of_its_own) {
    std::set<std::string_view> outcomes;
    for (int i = 0; i < kOutcomeCount; ++i) outcomes.insert(to_string(static_cast<Outcome>(i)));
    check::equal(outcomes.size(), static_cast<std::size_t>(kOutcomeCount), "outcomes");
    check::is_true(outcomes.count("exposed") == 1 && outcomes.count("denied") == 1, "the last outcomes named");
    std::set<std::string_view> facts;
    for (int i = 0; i <= static_cast<int>(FactKind::Reveal); ++i) facts.insert(to_string(static_cast<FactKind>(i)));
    check::equal(facts.size(), static_cast<std::size_t>(FactKind::Reveal) + 1, "fact kinds");
    check::is_true(facts.count("memory") == 1 && facts.count("reveal") == 1, "the last fact kinds named");
}

TEST(only_the_acts_about_something_need_a_target) {
    check::is_true(act_needs_target(ActKind::AskAboutPlace), "ask_about_place");
    check::is_true(act_needs_target(ActKind::AskAboutPerson), "ask_about_person");
    check::is_true(act_needs_target(ActKind::AcceptQuest), "accept_quest");
    check::is_true(act_needs_target(ActKind::DeclineQuest), "decline_quest");
    check::is_true(act_needs_target(ActKind::AbandonQuest), "abandon_quest");
    for (const ActKind kind : {ActKind::AskSurroundings, ActKind::AskForWork, ActKind::Farewell, ActKind::NoAct, ActKind::Greet,
                               ActKind::SmallTalk, ActKind::Answer, ActKind::Thank, ActKind::Insult, ActKind::AskSafety,
                               ActKind::Confront})
        check::is_true(!act_needs_target(kind), std::string(to_string(kind)) + " needs none");
}

TEST(only_taking_and_giving_up_work_change_the_world) {
    for (int i = 0; i < kActCount; ++i) {
        const auto kind = static_cast<ActKind>(i);
        const bool expected = kind == ActKind::AcceptQuest || kind == ActKind::AbandonQuest;
        check::equal(act_changes_world(kind), expected, std::string(to_string(kind)));
    }
}

TEST(a_fact_finds_its_fields_by_key) {
    Fact f{FactKind::Place, "x", {{"kind", "city"}, {"name", "Mekkadr"}}};
    check::is_true(f.field("name") && *f.field("name") == "Mekkadr", "a present field");
    check::is_true(f.field("distance") == nullptr, "an absent one is nullptr");
}
