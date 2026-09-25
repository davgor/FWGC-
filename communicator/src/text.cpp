#include "text.hpp"

#include <algorithm>

#include "communicator/error.hpp"

namespace communicator::text {

std::string one_line(std::string_view text) {
    std::string out;
    for (const char c : text) out += static_cast<unsigned char>(c) < 0x20 ? ' ' : c;
    const std::size_t first = out.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    return out.substr(first, out.find_last_not_of(' ') - first + 1);
}

std::string spaced(std::string_view text) {
    std::string out(text);
    std::replace(out.begin(), out.end(), '_', ' ');
    return out;
}

std::string spoken(std::string_view text) {
    std::string unbracketed;
    int depth = 0;
    for (const char c : one_line(text)) {
        if (c == '(') {
            ++depth;
        } else if (c == ')' && depth > 0) {
            --depth;
        } else if (depth == 0) {
            // A space never doubles, and never sits before punctuation a bracket left behind.
            if (c == ' ' && (unbracketed.empty() || unbracketed.back() == ' ')) continue;
            if ((c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?') && !unbracketed.empty() &&
                unbracketed.back() == ' ')
                unbracketed.pop_back();
            unbracketed += c;
        }
    }
    while (!unbracketed.empty() && unbracketed.back() == ' ') unbracketed.pop_back();
    return unbracketed;
}

std::string json_escaped(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

std::string plain_punctuation(std::string_view text) {
    struct Swap {
        std::string_view typed, plain;
    };
    static constexpr Swap kSwaps[] = {
        {"\xE2\x80\x98", "'"}, {"\xE2\x80\x99", "'"}, {"\xE2\x80\x9C", "\""}, {"\xE2\x80\x9D", "\""},
        {"\xE2\x80\x93", "-"}, {"\xE2\x80\x94", "-"}, {"\xE2\x80\xA6", "..."},
    };
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        const Swap* swap = nullptr;
        for (const Swap& s : kSwaps)
            if (text.substr(i, s.typed.size()) == s.typed) swap = &s;
        if (swap) {
            out += swap->plain;
            i += swap->typed.size();
        } else {
            out += text[i++];
        }
    }
    return out;
}

void check_line(std::string_view line) {
    if (line.size() > kMaxLineBytes)
        throw Error("a line of " + std::to_string(line.size()) + " bytes; the most a player may say at once is " +
                    std::to_string(kMaxLineBytes));
}

std::string scale_word(std::string_view key, std::string_view value) {
    static const char* const kDifficulty[] = {"very easy", "easy", "fair", "hard", "very hard"};
    static const char* const kDanger[] = {"slight", "low", "real", "high", "deadly"};
    if (value.size() != 1 || value[0] < '1' || value[0] > '5') return {};
    const auto step = static_cast<std::size_t>(value[0] - '1');
    if (key == "difficulty") return kDifficulty[step];
    if (key == "threat" || key == "danger") return kDanger[step];
    return {};
}

std::string range_words(std::string_view range) {
    if (range == "here") return "right here";
    if (range == "close") return "close by";
    if (range == "near") return "a short walk away";
    if (range == "walk") return "an hour's walk away";
    if (range == "far") return "a long walk away";
    if (range == "distant") return "a long way off";
    return {};
}

bool for_the_game(std::string_view key) { return key == "marker" || key == "marker_node"; }

}  // namespace communicator::text
