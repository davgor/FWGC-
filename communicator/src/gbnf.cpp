#include "gbnf.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>

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
