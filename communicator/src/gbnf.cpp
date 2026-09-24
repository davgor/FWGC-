#include "gbnf.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>

namespace communicator::gbnf {

namespace {

// The UTF-8 characters of `text`, each whole. A stray byte stands alone.
std::vector<std::string> characters_of(std::string_view text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::size_t width = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 1;
        if (i + width > text.size()) width = 1;
        out.emplace_back(text.substr(i, width));
        i += width;
    }
    return out;
}

struct Node {
    bool ends = false;  // a word ends here
    std::map<std::string, std::unique_ptr<Node>> next;
};

std::string expression(const Node& node);

// The branch through `character` from a node: the characters of a chain with no fork and no
// word ending run together into one literal.
std::string branch(const std::string& character, const Node& child) {
    std::string run = character;
    const Node* at = &child;
    while (!at->ends && at->next.size() == 1) {
        run += at->next.begin()->first;
        at = at->next.begin()->second.get();
    }
    const std::string rest = at->next.empty() ? std::string() : " " + expression(*at);
    return literal(run) + rest;
}

// The words below a node: their branches as alternatives, optional where a word may also end.
std::string expression(const Node& node) {
    std::string alternatives;
    for (const auto& [character, child] : node.next)
        alternatives += (alternatives.empty() ? "" : " | ") + branch(character, *child);
    return "(" + alternatives + (node.ends ? ")?" : ")");
}

}  // namespace

std::string literal(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (byte < 0x20 || byte == 0x7F) {
            char escaped[8];
            std::snprintf(escaped, sizeof escaped, "\\x%02X", byte);
            out += escaped;
        } else {
            out += c;
        }
    }
    return out + "\"";
}

namespace {

// The letters a-z not in `taken`, as the body of a character class: "a-mo-z".
std::string ascii_letters_except(const std::string& taken) {
    std::string out;
    char c = 'a';
    while (c <= 'z') {
        if (taken.find(c) != std::string::npos) {
            ++c;
            continue;
        }
        const char first = c;
        while (c + 1 <= 'z' && taken.find(static_cast<char>(c + 1)) == std::string::npos) ++c;
        out += first;
        if (c > first) out += std::string("-") + c;
        ++c;
    }
    return out;
}

struct Ban {
    bool ends = false;     // a banned word ends here
    bool stem = false;     // a banned stem ends here: past it, only an allowed word goes on
    bool allowed = false;  // an allowed word ends here, whatever covers it
    std::map<char, std::unique_ptr<Ban>> next;
};

bool all_lower_ascii(const std::string& word) {
    return !word.empty() && std::all_of(word.begin(), word.end(), [](char c) { return c >= 'a' && c <= 'z'; });
}

Ban& grow(Ban& root, const std::string& word) {
    Ban* at = &root;
    for (const char c : word) {
        std::unique_ptr<Ban>& child = at->next[c];
        if (!child) child = std::make_unique<Ban>();
        at = child.get();
    }
    return *at;
}

// Whether an entry covers `word`: it is a banned word, or begins with a stem.
bool covered(const Ban& root, const std::string& word) {
    const Ban* at = &root;
    for (const char c : word) {
        const auto found = at->next.find(c);
        if (found == at->next.end()) return false;
        at = found->second.get();
        if (at->stem) return true;
    }
    return at->ends;
}

// The words below a node of the entries' trie: a letter that leaves every entry, then any run;
// or a letter that follows one further. Past a stem (`blocked`), only the letters of an allowed
// word. `first` is the root, whose letter comes from a-z alone and which cannot end the word.
// Any other node may end it, unless a banned word ends there or a stem has been passed, and an
// allowed word may end there all the same. Empty when the word must end at the node; nothing
// when no word may pass through it.
std::optional<std::string> banned_expression(const Ban& node, bool first, bool blocked, std::string_view more_letters,
                                             std::string_view rest) {
    std::string alternatives;
    if (!blocked) {
        std::string taken;
        for (const auto& [letter, child] : node.next) taken += letter;
        const std::string others = ascii_letters_except(taken) + (first ? std::string() : std::string(more_letters));
        if (!others.empty()) alternatives = "[" + others + "] " + std::string(rest);
    }
    for (const auto& [letter, child] : node.next) {
        const std::optional<std::string> after = banned_expression(*child, false, blocked || child->stem, more_letters, rest);
        if (!after) continue;
        alternatives += (alternatives.empty() ? "" : " | ") + literal(std::string(1, letter)) + (after->empty() ? "" : " " + *after);
    }
    const bool may_end = !first && (node.allowed || (!blocked && !node.ends));
    if (alternatives.empty()) return may_end ? std::optional<std::string>(std::string()) : std::nullopt;
    return "(" + alternatives + (may_end ? ")?" : ")");
}

}  // namespace

std::string word_except(const std::vector<std::string>& banned, const std::vector<std::string>& allowed,
                        std::string_view more_letters, std::string_view rest) {
    Ban root;
    for (const std::string& entry : banned) {
        const bool stem = !entry.empty() && entry.back() == '*';
        const std::string word = stem ? entry.substr(0, entry.size() - 1) : entry;
        if (!all_lower_ascii(word)) continue;
        Ban& end = grow(root, word);
        (stem ? end.stem : end.ends) = true;
    }
    // Only a covered word changes what may be written; the rest are free already.
    for (const std::string& word : allowed)
        if (all_lower_ascii(word) && covered(root, word)) grow(root, word).allowed = true;
    return banned_expression(root, true, false, more_letters, rest).value_or(std::string());
}

std::string one_of(std::vector<std::string> words) {
    Node root;
    for (const std::string& word : words) {
        if (word.empty()) continue;
        Node* at = &root;
        for (const std::string& character : characters_of(word)) {
            std::unique_ptr<Node>& child = at->next[character];
            if (!child) child = std::make_unique<Node>();
            at = child.get();
        }
        at->ends = true;
    }
    if (root.next.empty()) return {};
    return expression(root);
}

}  // namespace communicator::gbnf
