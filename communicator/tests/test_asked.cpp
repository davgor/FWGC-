// The question in a reply worth an answer (contract version 21): chosen by meaning, measured on questions
// labelled before any description was tried. The development set chose the descriptions; the held-out
// set was measured once and never tuned to.
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "check.hpp"
#include "communicator/asked.hpp"
#include "embedder.hpp"

namespace {

// Labelled 2026-09-28, before any description was tried: `true` for a fact of the stranger that stays
// true, worth asking again next visit if never answered; `false` for a question about now, a
// pleasantry, an offer, an echo or a rhetorical one.
const std::vector<std::pair<std::string, bool>> kDevelopment = {
    {"Who are you, and what are you doing in my barracks?", true}, {"What brings you to the chapel?", true},
    {"Who are you?", true}, {"What are you good for?", true}, {"What's your angle?", true},
    {"What can you do, and what do you need?", true}, {"Where are your people from?", true}, {"What did your father do?", true},
    {"Why did you leave home?", true}, {"What's her name?", true}, {"Indeed?", false}, {"Aye?", false},
    {"What can I spot for you?", false}, {"Are you well?", false}, {"What's the matter?", false}, {"No?", false},
    {"Who's there?", false}, {"Mine?", false}, {"Leave?", false}, {"How are you keeping?", false}};

const std::vector<std::pair<std::string, bool>> kHeldOut = {
    {"Will you tell me the name?", true}, {"Who were they to you?", true}, {"What were they like?", true},
    {"What takes you there?", true}, {"What draws you to it?", true}, {"What brings you here?", true},
    {"What's your name, then?", true}, {"Who are you, then?", true}, {"What can you do?", true},
    {"Where do you hail from?", true}, {"Who taught you to fight like that?", true}, {"What was your brother's name?", true},
    {"Where did you learn your trade?", true}, {"Who sent you to me?", true}, {"What business brings you to Iskmor?", true},
    {"How did you come by that scar?", true}, {"Where are you headed after this?", true}, {"What happened to your family?", true},
    {"Who's in it with you?", true}, {"What are you after?", true}, {"Is that so?", false}, {"Are you keeping well?", false},
    {"What can I pour you?", false}, {"Where does it hurt?", false}, {"Is everything all right with you?", false},
    {"What now?", false}, {"Me?", false}, {"Whatever for?", false}, {"What do you mean, precisely?", false},
    {"Hear that wind?", false}, {"Have you eaten?", false}, {"Is it any easier now?", false}, {"Don't fancy it?", false},
    {"You want work?", false}, {"Mind the Frost owls, won't you?", false}, {"Would you wipe your boots, please?", false},
    {"What'll it be, then?", false}, {"Back again?", false}, {"You're all right, aren't you, nothing wrong?", false},
    {"Still here?", false}};

// Two wordings tried, chosen on the development set (15, then 16 of 20); the held-out set read 35, then 36,
// and chose nothing. With a question's shape first (under three words, a tag after a clause): 17 and 38,
// and every one missed a question worth asking again that is not kept, never one kept that should not be.
constexpr std::size_t kDevelopmentFloor = 17;
constexpr std::size_t kHeldOutFloor = 38;

std::size_t right_of(communicator::Meaning& meaning, const std::vector<std::pair<std::string, bool>>& set, std::string& missed) {
    std::size_t right = 0;
    for (const auto& [question, worth] : set) {
        // Said as the end of a reply, as a game hands it over.
        const std::string reply = "Well met. " + question;
        const std::string kept = communicator::question_to_keep(reply, meaning);
        if (!kept.empty() && kept != question) check::fail("the question as the reply says it: " + kept);
        if (!kept.empty() == worth)
            ++right;
        else
            missed += "\n  " + question + (worth ? ": not kept" : ": kept");
    }
    return right;
}

}  // namespace

TEST(the_question_worth_an_answer_is_the_replys_own_by_meaning) {
    communicator::Meaning meaning(test_model::embedder());
    check::equal(communicator::question_to_keep("Well met.", meaning), std::string(), "no question, nothing kept");
    check::equal(communicator::question_to_keep("I'm sorry. Who were they to you? Take your time.", meaning), std::string("Who were they to you?"),
                 "the question, word for word, from the middle of a reply");
    check::equal(communicator::question_to_keep("Who were they to you?", meaning), std::string("Who were they to you?"), "a reply that is the question");
    // Asked again (an `asked_again` line), the question kept is the one first asked, word for word: the
    // director counts it as asked twice, never as another question.
    check::equal(communicator::question_to_keep("Hello again. You never did answer me, last time. Who were they to you?", meaning),
                 std::string("Who were they to you?"), "asked again: the same question");
    std::string missed;
    const std::size_t development = right_of(meaning, kDevelopment, missed);
    const std::size_t held = right_of(meaning, kHeldOut, missed);
    std::cout << "  questions worth an answer: development " << development << " of " << kDevelopment.size() << ", held out " << held << " of "
              << kHeldOut.size() << missed << "\n";
    check::is_true(development >= kDevelopmentFloor && held >= kHeldOutFloor, std::to_string(development) + ", " + std::to_string(held) + missed);
}
