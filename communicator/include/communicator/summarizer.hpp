// The summarizer: a person's conversation log with the stranger, made into what they would remember
// of it -- the outcomes the director's compact() takes (orchestrator/README.md, "Short and long
// memory").
//
// A model reads the log and answers in a grammar: up to five things the stranger said of
// themselves, each only as a run of whole words from one of the stranger's own lines, with a few
// lower-case words on what it is about; and one sentence on what they last spoke of, in lower-case
// words and the capitalised words the log itself holds. The director checks every word of it again
// when it compacts, and keeps deeds, work and what is open by its own rules: a summary can only add
// the stranger's words and a sentence, and decides nothing.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "inference/runtime.hpp"

namespace communicator {

// One line of a log: who said it -- empty for the stranger, else whoever answered them, by name --
// and the words, exactly as the game handed them to the director (Director::words).
struct LogLine {
    std::string speaker;
    std::string words;
};

struct SummaryOptions {
    int max_tokens = 400;  // room for five quotes and a sentence, closed
};

class Summarizer {
public:
    // The model is borrowed, not owned, and must outlive the summarizer. Throws Error for options
    // out of range.
    explicit Summarizer(inference::Model& model, SummaryOptions options = {});

    // What `person` (their name, and what they are: "Selbren, the castellan") would remember of
    // `log`, as compact()'s outcomes: {"said": [{"quote": "...", "about": "..."}], "last": "..."}.
    // Greedy: the same log gives the same summary. Throws Error for a line past kMaxLineBytes.
    std::string summarize(std::string_view person, const std::vector<LogLine>& log) const;

    // What summarize() puts to the model, and the grammar it holds the answer to: for a log, a test,
    // or a measure. Neither needs a model.
    static std::vector<inference::Message> messages(std::string_view person, const std::vector<LogLine>& log);
    static std::string grammar(const std::vector<LogLine>& log);

    // Every quote the grammar allows: each run of two to twenty whole words within one sentence of
    // one of the stranger's lines, never of a question, at most 240 bytes, without the stop that
    // ends it and without a double quote or a backslash. The runs a director finds word for word in
    // the log.
    static std::vector<std::string> quotable(const std::vector<LogLine>& log);

private:
    inference::Model& model_;
    SummaryOptions options_;
};

}  // namespace communicator
