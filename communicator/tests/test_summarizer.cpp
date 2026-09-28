// The summarizer (contract version 12): a conversation log made into what a person would remember,
// as the director's compact() takes it. The grammar is the guarantee: a quote is a run of the
// stranger's own whole words, and the sentence of what they last spoke of names only what the log
// names. The director checks both again; these tests show the model cannot write otherwise.
#include "communicator/summarizer.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "check.hpp"
#include "communicator/voice.hpp"
#include "model.hpp"

namespace {

using communicator::LogLine;
using communicator::Summarizer;

const std::vector<LogLine>& a_log() {
    static const std::vector<LogLine> log = {
        {"", "Evening. Is there a bed going?"},
        {"Dornwen", "One, under the eaves. Where have you come from, then?"},
        {"", "From the salt marshes. My mother still keeps the ferry there."},
        {"Dornwen", "A long road. Supper's at dusk, and my \"stew\" is famous."},
        {"", "I'll not miss it. My sister says your \"stew\" is the best in Torhas."},
    };
    return log;
}

// A stranger who writes as people type: a model quoting them tidies the words up.
const std::vector<LogLine>& an_untidy_log() {
    static const std::vector<LogLine> log = {
        {"", "evenin. any beds goin?"},
        {"Dornwen", "One, under the eaves. Where have you come from, then?"},
        {"", "from the salt marshes, me mam still keeps the ferry down there"},
        {"Dornwen", "A long road. Supper's at dusk."},
        {"", "cheers, im starvin"},
    };
    return log;
}

// The quotes a summary holds, as its grammar writes them: {"quote": "..."}.
std::vector<std::string> quotes_of(const std::string& summary) {
    std::vector<std::string> out;
    const std::string mark = "{\"quote\": \"";
    for (std::size_t at = summary.find(mark); at != std::string::npos; at = summary.find(mark, at + 1)) {
        const std::size_t from = at + mark.size();
        out.push_back(summary.substr(from, summary.find('"', from) - from));
    }
    return out;
}

// The sentence of what they last spoke of.
std::string last_of(const std::string& summary) {
    const std::string mark = "\"last\": \"";
    const std::size_t at = summary.find(mark);
    if (at == std::string::npos) return {};
    const std::size_t from = at + mark.size();
    return summary.substr(from, summary.find('"', from) - from);
}

// The capitalised words of `text` past its first word.
std::vector<std::string> capitals_after_the_first(const std::string& text) {
    std::vector<std::string> out;
    std::string word;
    bool first = true;
    for (const char c : text + " ") {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '\'') {
            word += c;
            continue;
        }
        if (!word.empty() && !first && word[0] >= 'A' && word[0] <= 'Z') out.push_back(word);
        if (!word.empty()) first = false;
        word.clear();
    }
    return out;
}

}  // namespace

TEST(what_may_be_quoted_is_every_run_of_the_strangers_own_whole_words) {
    const std::vector<std::string> quotable = Summarizer::quotable(a_log());
    const auto has = [&](const std::string& q) { return std::find(quotable.begin(), quotable.end(), q) != quotable.end(); };
    check::is_true(has("My mother still keeps the ferry there"), "a run to the end of a line, its stop left off");
    check::is_true(has("mother still keeps"), "a run from within it");
    check::is_true(has("From the salt marshes"), "a run whose stop ends a sentence inside the line");
    check::is_true(!has("From the salt marshes. My mother"), "never from one sentence into the next");
    check::is_true(!has("Is there a bed going") && !has("there a bed"), "never of a question");
    check::is_true(!has("My mother still keeps the ferry there."), "never with the stop that ends it");
    check::is_true(!has("One, under the eaves"), "never the words of whoever answered");
    check::is_true(!has("Evening."), "never a single word");
    check::is_true(!has("my sister says"), "never other than as said");
    check::is_true(std::none_of(quotable.begin(), quotable.end(), [](const std::string& q) { return q.find('"') != std::string::npos; }),
                   "never a quote mark, which a quote cannot hold");
    check::is_true(has("I'll not miss it"), "the stranger's own, with an apostrophe");
    check::is_true(Summarizer::quotable({{"Dornwen", "Nothing of the stranger's here."}}).empty(), "nothing from the one who answered");
}

TEST(a_summary_quotes_only_the_strangers_own_words_and_names_only_what_the_log_does) {
    inference::Model& model = test_model::model();
    const std::string person = "Dornwen, the innkeeper";
    // The premise: free, the model quotes words the stranger never said so, tidied up.
    {
        const std::vector<LogLine>& untidy = an_untidy_log();
        inference::Sampling free_text;
        free_text.max_tokens = 200;
        const std::string free_summary = model.complete(model.chat_prompt(Summarizer::messages(person, untidy)), free_text).text;
        const std::vector<std::string> free_quotes = quotes_of(free_summary);
        const std::vector<std::string> quotable = Summarizer::quotable(untidy);
        check::is_true(std::any_of(free_quotes.begin(), free_quotes.end(),
                                   [&](const std::string& q) { return std::find(quotable.begin(), quotable.end(), q) == quotable.end(); }),
                       "the premise: free, the model quotes what the stranger never said so: " + free_summary);
        const std::string held = Summarizer(model).summarize(person, untidy);
        for (const std::string& q : quotes_of(held))
            check::is_true(std::find(quotable.begin(), quotable.end(), q) != quotable.end(), "held, the stranger's own words: " + q);
    }
    const std::vector<LogLine> log = a_log();
    const std::vector<std::string> quotable = Summarizer::quotable(log);
    // Held, every quote is the stranger's own run of words, and the sentence names only the log's.
    const std::string summary = Summarizer(model).summarize(person, log);
    check::is_true(summary.rfind("{\"said\": [", 0) == 0 && summary.find("], \"last\": \"") != std::string::npos &&
                       summary.size() > 2 && summary.compare(summary.size() - 2, 2, "\"}") == 0,
                   "the summary compact() takes: " + summary);
    for (const std::string& q : quotes_of(summary))
        check::is_true(std::find(quotable.begin(), quotable.end(), q) != quotable.end(), "a quote of the stranger's own words: " + q);
    check::is_true(quotes_of(summary).size() <= 5, "at most five quotes");
    // The log's own capitalised words, a possessive's "'s" left off: the only names its sentence may use.
    const std::set<std::string> names = {"Evening", "Is", "One", "Where", "From", "My", "A", "Supper", "I'll", "Torhas"};
    const std::string last = last_of(summary);
    check::is_true(!last.empty() && last.back() == '.', "a sentence of what they last spoke of: " + last);
    for (const std::string& word : capitals_after_the_first(last))
        check::is_true(names.count(word) != 0, "a name the log holds: " + word + " in " + last);
    check::equal(Summarizer(model).summarize(person, log), summary, "the same log, the same summary");
}
