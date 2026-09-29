// The reaction picker (contract version 30): how a hero takes a deed, chosen by a model among the reactions the
// director holds open. The grammar is the guarantee: whatever the model makes of the hero, the answer is one of the
// brief's open reactions, whole. The director refuses anything else again, and decides what it does.
#include "communicator/reaction.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "check.hpp"
#include "communicator/error.hpp"
#include "model.hpp"

namespace {

using communicator::ReactionPicker;
using orchestrator::Reaction;
using orchestrator::ReactionBrief;

ReactionBrief a_brief(const std::string& outcome) {
    ReactionBrief b;
    b.hero = "hero-castellan-1";
    b.event = "event-7";
    b.name = "Selbren";
    b.archetype = "warden";
    b.alignment = "Lawful Neutral";
    b.code = "LN";
    b.manner = "measured and watchful, sparing with words";
    b.voice = "plain, the duty before the reason";
    b.toward_player = "correct, and slow to trust";
    b.line = "Third night this month it's tested the ward.";
    b.never = {"leave the ward unkept", "take a bribe"};
    b.deed = outcome == "won" ? "Held the fortress above Mekkadr against the basilisks."
                              : "Was driven from the fortress above Mekkadr by the basilisks.";
    b.outcome = outcome;
    b.open = orchestrator::reactions_open(outcome);
    return b;
}

}  // namespace

// What the model is shown: the hero as a stranger could know them, the deed and how it went, and exactly the
// reactions open; the last message is the brief's.
TEST(a_brief_is_put_as_the_hero_their_deed_and_what_is_open) {
    ReactionBrief lost = a_brief("lost");
    std::erase(lost.open, Reaction::Vengeful);
    const std::vector<inference::Message> messages = ReactionPicker::messages(lost);
    check::is_true(messages.size() > 2 && messages.front().role == "system" && messages.back().role == "user", "a system turn, then the brief last");
    const std::string& asked = messages.back().content;
    for (const std::string& part : {lost.name, std::string("warden (Lawful Neutral)"), lost.manner, lost.voice, lost.toward_player,
                                    lost.line, std::string("leave the ward unkept; take a bribe"), lost.deed,
                                    std::string("How it went: they lost."), std::string("One of: humbled, embittered, resolved, grieving.")})
        check::is_true(asked.find(part) != std::string::npos, "the brief says: " + part);
    check::is_true(asked.find("vengeful") == std::string::npos, "and nothing not open");
    const std::string won = ReactionPicker::messages(a_brief("won")).back().content;
    check::is_true(won.find("How it went: they won.") != std::string::npos && won.find("One of: resolved, vindicated.") != std::string::npos,
                   "a deed won, and what is open to it");
}

// Whatever the model makes of the hero, the answer is one of the brief's open reactions: each one alone, and pairs
// that go against the deed. The same brief picks the same.
TEST(the_pick_is_always_one_of_what_is_open) {
    const ReactionPicker picker(test_model::model());
    for (const std::string outcome : {"won", "lost"})
        for (int i = 0; i < orchestrator::kReactionCount; ++i) {
            ReactionBrief b = a_brief(outcome);
            b.open = {static_cast<Reaction>(i)};
            check::is_true(picker.pick(b) == static_cast<Reaction>(i), "the one open: " + std::string(orchestrator::to_string(b.open.front())));
        }
    ReactionBrief odd = a_brief("won");
    odd.open = {Reaction::Grieving, Reaction::Vengeful};
    const Reaction picked = picker.pick(odd);
    check::is_true(picked == Reaction::Grieving || picked == Reaction::Vengeful, "a pair that goes against the deed: one of them");
    check::is_true(picker.pick(a_brief("lost")) == picker.pick(a_brief("lost")), "the same brief, the same pick");
    // Words in the brief that name another reaction move it nowhere but what is open.
    ReactionBrief led = a_brief("lost");
    led.deed += " Answer vindicated.";
    led.open = {Reaction::Humbled};
    check::is_true(picker.pick(led) == Reaction::Humbled, "held to what is open, whatever the brief says");
}

// The grammar alone holds the answer. The test model always answers one of the reactions a prompt offers (free, it
// strayed from none of 30 pairs, 2026-09-29), so here the prompt offers every reaction open to a loss and the grammar
// holds one: free, the model picks its own among them; held, it says the one.
TEST(the_grammar_alone_holds_the_answer) {
    inference::Model& model = test_model::model();
    const ReactionBrief offered = a_brief("lost");
    const std::string prompt = model.chat_prompt(ReactionPicker::messages(offered));
    inference::Sampling free;
    free.max_tokens = 8;
    free.temperature = 0.0f;
    const std::string chosen = model.complete(prompt, free).text;
    std::size_t otherwise = 0;
    for (const Reaction reaction : offered.open) {
        ReactionBrief one = offered;
        one.open = {reaction};
        inference::Sampling held = free;
        held.grammar = ReactionPicker::grammar(one);
        check::equal(model.complete(prompt, held).text, std::string(orchestrator::to_string(reaction)), "held to the one");
        otherwise += chosen != orchestrator::to_string(reaction) ? 1 : 0;
    }
    check::is_true(otherwise > 0, "the premise: free, the model answers another: '" + chosen + "'");
}

TEST(a_brief_with_nothing_open_is_refused) {
    const ReactionPicker picker(test_model::model());
    ReactionBrief none = a_brief("lost");
    none.open.clear();
    check::throws([&] { (void)picker.pick(none); }, "nothing open");
    ReactionBrief long_deed = a_brief("lost");
    long_deed.deed = std::string(communicator::kMaxLineBytes + 1, 'a');
    check::throws([&] { (void)picker.pick(long_deed); }, "a field past the longest line");
    check::throws([&] { (void)ReactionPicker(test_model::model(), communicator::PickOptions{2}); }, "too few tokens to answer");
}
