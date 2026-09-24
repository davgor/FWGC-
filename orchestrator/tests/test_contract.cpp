// The contract's vocabulary: every name reads back as its value, and nothing else does.
#include "orchestrator/contract.hpp"

#include "check.hpp"

using namespace orchestrator;

TEST(every_act_name_reads_back_as_its_act) {
    for (int i = 0; i <= static_cast<int>(ActKind::NoAct); ++i) {
        const auto kind = static_cast<ActKind>(i);
        const std::optional<ActKind> back = act_kind(to_string(kind));
        check::is_true(back.has_value() && *back == kind, "round trip of " + std::string(to_string(kind)));
    }
    check::is_true(!act_kind("attack").has_value(), "a name outside the set reads as nothing");
    check::is_true(!act_kind("").has_value(), "and so does an empty one");
}

TEST(only_the_acts_about_something_need_a_target) {
    check::is_true(act_needs_target(ActKind::AskAboutPlace), "ask_about_place");
    check::is_true(act_needs_target(ActKind::AskAboutPerson), "ask_about_person");
    check::is_true(act_needs_target(ActKind::AcceptQuest), "accept_quest");
    check::is_true(act_needs_target(ActKind::DeclineQuest), "decline_quest");
    check::is_true(act_needs_target(ActKind::AbandonQuest), "abandon_quest");
    check::is_true(!act_needs_target(ActKind::AskSurroundings), "ask_surroundings");
    check::is_true(!act_needs_target(ActKind::AskForWork), "ask_for_work");
    check::is_true(!act_needs_target(ActKind::Farewell), "farewell");
    check::is_true(!act_needs_target(ActKind::NoAct), "no_act");
}

TEST(only_taking_and_giving_up_work_change_the_world) {
    for (int i = 0; i <= static_cast<int>(ActKind::NoAct); ++i) {
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
