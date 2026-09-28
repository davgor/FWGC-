#include "communicator/summarizer.hpp"

#include <algorithm>
#include <set>
#include <utility>

#include "communicator/error.hpp"
#include "communicator/voice.hpp"
#include "gbnf.hpp"
#include "text.hpp"

namespace communicator {

namespace {

// What a director keeps of a summary at most (orchestrator/README.md): quotes of this many bytes,
// this many of them. Runs of words are quoted from two words to this many.
constexpr std::size_t kMostQuotes = 5;
constexpr std::size_t kMaxQuoteBytes = 240;
constexpr std::size_t kMostQuoteWords = 20;

bool blank(char c) { return c == ' ' || c == '\t'; }

bool stop(char c) { return c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?'; }

bool in_word(char c) {
    const auto u = static_cast<unsigned char>(c);
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '\'' || u >= 0x80;
}

// The capitalised words of the log's lines, as a director reads a name: a possessive's "'s" left
// off. Never "I": no summary of the stranger says it.
std::set<std::string> names_in(const std::vector<LogLine>& log) {
    std::set<std::string> out;
    for (const LogLine& line : log) {
        std::string word;
        const auto flush = [&] {
            while (!word.empty() && word.back() == '\'') word.pop_back();
            if (word.size() > 2 && word.compare(word.size() - 2, 2, "'s") == 0) word.resize(word.size() - 2);
            if (!word.empty() && word[0] >= 'A' && word[0] <= 'Z' && word != "I") out.insert(word);
            word.clear();
        };
        for (const char c : line.words) {
            if (in_word(c)) {
                word += c;
            } else {
                flush();
            }
        }
        flush();
    }
    return out;
}

std::string transcript(std::string_view person, const std::vector<LogLine>& log) {
    const std::string who = text::one_line(person);
    const std::string name = who.substr(0, who.find(','));
    std::string out;
    for (const LogLine& line : log) {
        const std::string speaker = text::one_line(line.speaker);
        out += (speaker.empty() ? std::string("The stranger") : speaker) + ": " + text::one_line(line.words) + "\n";
    }
    return out + "\nWrite what " + name + " would remember.";
}

}  // namespace

Summarizer::Summarizer(inference::Model& model, SummaryOptions options) : model_(model), options_(options) {
    if (options_.max_tokens < 64 || options_.max_tokens > 2048) throw Error("a summary's max_tokens is 64 to 2048");
}

std::vector<std::string> Summarizer::quotable(const std::vector<LogLine>& log) {
    std::set<std::string> out;
    for (const LogLine& line : log) {
        if (!line.speaker.empty()) continue;
        const std::string& said = line.words;
        // Each sentence of the line, a quote never running from one into the next; and none of a
        // question, which asks and tells nothing of the one asking: the questions-only log of
        // eval/summarize.txt drew five quotes of its questions (2026-09-27).
        for (std::size_t from = 0; from < said.size();) {
            std::size_t end = from;
            while (end < said.size() &&
                   !((said[end] == '.' || said[end] == '!' || said[end] == '?') && (end + 1 == said.size() || blank(said[end + 1]))))
                ++end;
            const std::size_t begin = from;
            const std::size_t after = std::min(end + 1, said.size());
            from = after;
            if (end < said.size() && said[end] == '?') continue;
            // Where each run of what is not blank begins and ends, in the sentence.
            std::vector<std::pair<std::size_t, std::size_t>> tokens;
            for (std::size_t i = begin; i < after;) {
                if (blank(said[i])) {
                    ++i;
                    continue;
                }
                const std::size_t start = i;
                while (i < after && !blank(said[i])) ++i;
                tokens.emplace_back(start, i);
            }
            for (std::size_t first = 0; first < tokens.size(); ++first)
                for (std::size_t last = first + 1; last < tokens.size() && last - first < kMostQuoteWords; ++last) {
                    std::size_t stop_at = tokens[last].second;
                    while (stop_at > tokens[last].first && stop(said[stop_at - 1])) --stop_at;
                    if (stop_at == tokens[last].first) continue;  // a last word of nothing but stops
                    const std::string quote = said.substr(tokens[first].first, stop_at - tokens[first].first);
                    if (quote.size() > kMaxQuoteBytes) break;
                    const bool plain = std::none_of(quote.begin(), quote.end(), [](char c) {
                        return c == '"' || c == '\\' || static_cast<unsigned char>(c) < 0x20;
                    });
                    if (plain) out.insert(quote);
                }
        }
    }
    return {out.begin(), out.end()};
}

std::string Summarizer::grammar(const std::vector<LogLine>& log) {
    const std::vector<std::string> quotes = quotable(log);
    const std::set<std::string> names = names_in(log);
    std::vector<std::string> openers(common_capitals().begin(), common_capitals().end());
    openers.insert(openers.end(), names.begin(), names.end());
    std::string g = "root ::= \"{\\\"said\\\": [\" ";
    if (!quotes.empty()) g += "(item (\", \" item){0," + std::to_string(kMostQuotes - 1) + "})? ";
    g += "\"], \\\"last\\\": \\\"\" last \"\\\"}\"\n";
    if (!quotes.empty()) {
        g += "item ::= \"{\\\"quote\\\": \\\"\" quote \"\\\", \\\"about\\\": \\\"\" about \"\\\"}\"\n";
        g += "quote ::= " + gbnf::one_of(quotes) + "\n";
        g += "about ::= lower (\" \" lower){0,23}\n";
    }
    g += "last ::= opener ((\" \" | \", \") word){0,47} \".\"\n";
    g += "opener ::= " + gbnf::one_of(openers) + "\n";
    g += names.empty() ? "word ::= lower\n" : "word ::= lower | name\nname ::= " + gbnf::one_of({names.begin(), names.end()}) + "\n";
    g += "lower ::= [a-z] [a-z']{0,23}\n";
    return g;
}

std::vector<inference::Message> Summarizer::messages(std::string_view person, const std::vector<LogLine>& log) {
    const std::string who = text::one_line(person);
    const std::string name = who.substr(0, who.find(','));
    std::vector<inference::Message> out;
    out.push_back({"system",
                   "You keep the memory of " + who + ". You read what they and a stranger said to each other, and write "
                   "down what " + name + " would remember of it, as JSON.\n"
                   "- \"said\": up to five things the stranger said of themselves that are worth remembering: who they are, "
                   "their people, where they come from or are going, what they are after or afraid of. Each is the "
                   "stranger's own words, exactly as they said them, and a few words on what it is about. Leave out "
                   "greetings, thanks, questions, insults, and what they asked of places, people or work. A conversation "
                   "with nothing of the kind keeps nothing.\n"
                   "- \"last\": one short sentence on what they last spoke of, as " + name + " would put it to themselves. The "
                   "stranger is \"they\". Add nothing that was not said."});
    // A worked example, as chat turns: a model reads the kind of answer from one.
    out.push_back({"user",
                   "Their conversation:\n"
                   "The stranger: Evening. Any rooms left?\n"
                   "Dornwen: One, over the stable. Where are you bound?\n"
                   "The stranger: To Torhas. My father is sick there and I mean to see him before the frost.\n"
                   "Dornwen: Then rest well tonight.\n"
                   "The stranger: Is it safe on the pass?\n"
                   "Dornwen: Safe enough, with care.\n"
                   "\nWrite what Dornwen would remember."});
    out.push_back({"assistant",
                   "{\"said\": [{\"quote\": \"My father is sick there and I mean to see him before the frost\", \"about\": \"the "
                   "stranger's sick father\"}], \"last\": \"They asked whether the pass was safe.\"}"});
    // And one with nothing of the stranger in it: nothing kept is an answer too.
    out.push_back({"user",
                   "Their conversation:\n"
                   "The stranger: Where's the nearest well?\n"
                   "Dornwen: By the gate, a short walk.\n"
                   "The stranger: Any work around here? You look useless.\n"
                   "Dornwen: Ask at the hall.\n"
                   "\nWrite what Dornwen would remember."});
    out.push_back({"assistant", "{\"said\": [], \"last\": \"They asked after the well and for work, and were rude about it.\"}"});
    out.push_back({"user", "Their conversation:\n" + transcript(person, log)});
    return out;
}

std::string Summarizer::summarize(std::string_view person, const std::vector<LogLine>& log) const {
    for (const LogLine& line : log) text::check_line(line.words);
    inference::Sampling sampling;
    sampling.max_tokens = options_.max_tokens;
    sampling.temperature = 0.0f;
    sampling.grammar = grammar(log);
    return model_.complete(model_.chat_prompt(messages(person, log)), sampling).text;
}

}  // namespace communicator
