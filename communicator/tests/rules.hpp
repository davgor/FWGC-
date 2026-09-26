// The voice's rules as a reader checks them in finished text: the names a reply may use, where a
// common capital may stand, its sentences, its opening, and its barred words. The voice's tests
// hold a model's replies to them, and the written lines' tests hold every filled line to them.
#pragma once

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>

#include "communicator/voice.hpp"
#include "orchestrator/contract.hpp"

namespace rules {

inline std::string lowered(const std::string& text) {
    std::string out;
    for (const char c : text) out += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    return out;
}

// The words of `text`: runs of letters, ASCII or not. Typographic punctuation (curly quotes,
// dashes, the ellipsis: U+2000 to U+207F, bytes E2 80 or E2 81 and one more) parts words, as
// ASCII punctuation does, so "I’m" is "I" and "m".
inline std::vector<std::string> words_of(const std::string& text) {
    std::vector<std::string> out;
    std::string word;
    const auto flush = [&] {
        if (!word.empty()) out.push_back(word);
        word.clear();
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        const bool punctuation = c == 0xE2 && i + 2 < text.size() && (static_cast<unsigned char>(text[i + 1]) & 0xFE) == 0x80;
        if (punctuation) {
            flush();
            i += 2;
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80) {
            word += text[i];
        } else {
            flush();
        }
    }
    flush();
    return out;
}

// Every word the packet puts in front of the voice as something to say: the speaker's name,
// post, place and line, and every fact's text.
inline std::set<std::string> packet_words(const orchestrator::FactPacket& p) {
    std::set<std::string> out;
    for (const std::string* text : {&p.persona.name, &p.persona.role, &p.persona.place, &p.persona.line})
        for (const std::string& w : words_of(*text)) out.insert(w);
    for (const orchestrator::Fact& f : p.facts)
        for (const auto& [key, value] : f.fields)
            for (const std::string& w : words_of(value)) out.insert(w);
    return out;
}

// The capitalised words of a reply that are neither the packet's nor common words: names the
// packet does not give.
inline std::vector<std::string> unlisted_names(const std::string& reply, const orchestrator::FactPacket& p) {
    const std::set<std::string> known = packet_words(p);
    const std::vector<std::string>& common = communicator::common_capitals();
    std::vector<std::string> out;
    for (const std::string& w : words_of(reply)) {
        const auto first = static_cast<unsigned char>(w[0]);
        const bool capital = (first >= 'A' && first <= 'Z') || first >= 0x80;
        if (capital && !known.count(w) && std::find(common.begin(), common.end(), w) == common.end()) out.push_back(w);
    }
    return out;
}

// The common capitals a reply uses past the start of a sentence, "I" and the packet's own words
// aside: common words made into names ("a castle named Greetings"). A sentence starts the reply
// and follows a stop that ends a word run (the point in "2.5 km" ends nothing).
inline std::vector<std::string> stray_capitals(const std::string& reply, const orchestrator::FactPacket& p) {
    const std::set<std::string> known = packet_words(p);
    const std::vector<std::string>& common = communicator::common_capitals();
    std::vector<std::string> out;
    bool starts = true;
    std::string word;
    for (std::size_t i = 0; i <= reply.size(); ++i) {
        const char c = i < reply.size() ? reply[i] : ' ';
        if (std::isalpha(static_cast<unsigned char>(c))) {
            word += c;
            continue;
        }
        if (!word.empty()) {
            const bool listed = std::find(common.begin(), common.end(), word) != common.end();
            if (!starts && listed && word != "I" && !known.count(word)) out.push_back(word);
            starts = false;
            word.clear();
        }
        const bool stop = c == '.' || c == '!' || c == '?';
        if (stop && (i + 1 >= reply.size() || !std::isalnum(static_cast<unsigned char>(reply[i + 1])))) starts = true;
    }
    return out;
}

// The words of a reply that forbidden_words() bars, in any case, but for the packet's own: a
// speaker who knows the silver mine may say "silver".
inline std::vector<std::string> barred_words(const std::string& reply, const orchestrator::FactPacket& p) {
    std::set<std::string> own;
    for (const std::string& w : packet_words(p)) own.insert(lowered(w));
    std::vector<std::string> out;
    for (const std::string& w : words_of(reply)) {
        const std::string word = lowered(w);
        if (own.count(word)) continue;
        for (const std::string& f : communicator::forbidden_words()) {
            const bool stem = !f.empty() && f.back() == '*';
            if (stem ? word.rfind(f.substr(0, f.size() - 1), 0) == 0 : word == f) {
                out.push_back(w);
                break;
            }
        }
    }
    return out;
}

// The article and the word after it wherever the two do not fit, as a blank can fill in: "a
// evening", "an market", "every the turn of the season". Only words one space apart pair, and only
// articles that are never a word of their own: "after that the deeps" is sound.
inline std::vector<std::string> misfit_articles(const std::string& reply) {
    static const std::set<std::string> determiners = {"a", "an", "the", "every", "each", "another"};
    const auto letters = [](const std::string& token, bool whole) {
        std::string out;
        for (const char c : token) {
            if (!std::isalpha(static_cast<unsigned char>(c))) {
                if (whole) return std::string();  // punctuation after it: the pair is parted
                break;
            }
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return out;
    };
    std::vector<std::string> tokens;
    std::string token;
    for (const char c : reply + " ") {
        if (c == ' ') {
            if (!token.empty()) tokens.push_back(token);
            token.clear();
        } else {
            token += c;
        }
    }
    std::vector<std::string> out;
    for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
        const std::string first = letters(tokens[i], true);
        const std::string next = letters(tokens[i + 1], false);
        if (first.empty() || next.empty()) continue;
        // Words that begin with a vowel's letter and a consonant's sound take "a": "a one-horse
        // cart", "a ewe".
        const bool sounds_consonant = next.rfind("one", 0) == 0 || next.rfind("once", 0) == 0 || next.rfind("ewe", 0) == 0 ||
                                      next.rfind("eu", 0) == 0;
        const bool vowel = std::string("aeio").find(next[0]) != std::string::npos && !sounds_consonant;
        const bool consonant = std::string("aeiouh").find(next[0]) == std::string::npos;
        if ((first == "a" && vowel) || (first == "an" && consonant) || (determiners.count(first) && next == "the"))
            out.push_back(tokens[i] + " " + tokens[i + 1]);
    }
    return out;
}

inline bool opens_with_one_of(const std::string& reply, const std::vector<std::string>& openings) {
    return std::any_of(openings.begin(), openings.end(), [&](const std::string& o) { return reply.rfind(o, 0) == 0; });
}

// The sentences in `text`: runs of . ! ? that close it, or come before a space, a bracket or a
// quote, straight or curly (a curly one starts with the byte E2). The point in "2.5 km" ends
// nothing.
inline std::size_t sentences_in(const std::string& text) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '.' && text[i] != '!' && text[i] != '?') continue;
        while (i + 1 < text.size() && (text[i + 1] == '.' || text[i + 1] == '!' || text[i + 1] == '?')) ++i;
        const auto next = i + 1 < text.size() ? static_cast<unsigned char>(text[i + 1]) : static_cast<unsigned char>(' ');
        if (next == ' ' || next == '"' || next == '\'' || next == ')' || next == 0xE2) ++count;
    }
    return count;
}

}  // namespace rules
