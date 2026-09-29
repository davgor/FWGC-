#include "communicator/asked.hpp"

#include <cctype>
#include <string>
#include <vector>

namespace communicator {

namespace {

// The last sentence of `reply` that ends in "?", trimmed, or empty. A sentence begins after ". ", "! " or
// "? ", or at the start.
std::string last_question(std::string_view reply) {
    const std::size_t mark = reply.rfind('?');
    if (mark == std::string_view::npos) return {};
    std::size_t start = 0;
    for (std::size_t i = mark; i > 0; --i)
        if ((reply[i - 1] == '.' || reply[i - 1] == '!' || reply[i - 1] == '?') && reply[i] == ' ') {
            start = i;
            break;
        }
    while (start < mark && reply[start] == ' ') ++start;
    return std::string(reply.substr(start, mark - start + 1));
}

// Whether `question` asks no fact by its shape: fewer than three words ("Leave?", "Still here?"), an echo;
// or a tag after a clause ("Mind the Frost owls, won't you?"), which asks the stranger to agree.
bool asks_no_fact(const std::string& question) {
    std::size_t words = 0;
    bool in_word = false;
    for (const char c : question) {
        const bool letter = std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '\'';
        if (letter && !in_word) ++words;
        in_word = letter;
    }
    if (words < 3) return true;
    static const std::vector<std::string> tags = {", won't you?",    ", will you?",  ", yes?",       ", all right?", ", aren't you?",
                                                  ", isn't it?",     ", don't you?", ", right?",     ", eh?",        ", no?",
                                                  ", wouldn't you?", ", can't you?", ", do you?",    ", would you?", ", shall we?"};
    for (const std::string& tag : tags)
        if (question.size() > tag.size() && question.compare(question.size() - tag.size(), tag.size(), tag) == 0) return true;
    return false;
}

}  // namespace

std::string question_to_keep(std::string_view reply, Meaning& meaning) {
    const std::string question = last_question(reply);
    if (question.empty() || asks_no_fact(question)) return {};
    // Chosen of two wordings on the development set (15, then 16 of 20); the held-out set read 35, then 36
    // of 40, and chose nothing. With the shapes above, 17 and 38: no question kept that should not be.
    static const std::vector<std::string> kinds = {
        "A question about the stranger with a lasting answer, worth asking again another day: their name, who they are, where they "
        "come from, what brought them here, what they are good at, what they are after, who someone was to them, what someone was "
        "like, where they are going.",
        "A question about this moment, or no real question: how they are, whether they are well, whether it hurts, what they want "
        "right now, what to pour or fetch, who is there, is that so, a single word said back as a question."};
    return meaning.subject_of(question, kinds) == 0 ? question : std::string();
}

}  // namespace communicator
