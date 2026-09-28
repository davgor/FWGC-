#include "text.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include "communicator/error.hpp"
#include "communicator/voice.hpp"

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

std::string ask_of(const std::string& purpose) {
    const std::size_t colon = purpose.rfind(": ");
    return colon == std::string::npos ? purpose : purpose.substr(colon + 2);
}

std::string mid_sentence(std::string text) {
    while (!text.empty() && (text.back() == '.' || text.back() == ' ')) text.pop_back();
    if (!text.empty() && text[0] >= 'A' && text[0] <= 'Z' && !(text.size() > 1 && text[1] >= 'A' && text[1] <= 'Z'))
        text[0] = static_cast<char>(text[0] - 'A' + 'a');
    return text;
}

std::string retold(const std::string& text) {
    static const std::map<std::string, std::string> shifts = {
        {"me", "them"}, {"my", "their"}, {"mine", "theirs"}, {"myself", "themselves"}};
    std::string out, word;
    const auto flush = [&] {
        const auto shift = shifts.find(word);
        out += shift == shifts.end() ? word : shift->second;
        word.clear();
    };
    for (const char c : text) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            word += c;
        } else {
            flush();
            out += c;
        }
    }
    flush();
    return out;
}

namespace {

// The stranger's own words with the first person turned: to the stranger ("you") or about them
// ("they"). Empty, turned to the stranger, when they speak to someone.
std::string turned(const std::string& words, bool to_them) {
    static const std::map<std::string, std::string> to_you = {
        {"i", "you"},       {"me", "you"},      {"my", "your"},       {"mine", "yours"},    {"myself", "yourself"},
        {"we", "you"},      {"us", "you"},      {"our", "your"},      {"ours", "yours"},    {"ourselves", "yourselves"},
        {"i'm", "you're"},  {"i've", "you've"}, {"i'll", "you'll"},   {"i'd", "you'd"},     {"we're", "you're"},
        {"we've", "you've"}, {"we'll", "you'll"}, {"we'd", "you'd"}};
    static const std::map<std::string, std::string> about_them = {
        {"i", "they"},        {"me", "them"},       {"my", "their"},      {"mine", "theirs"},   {"myself", "themselves"},
        {"we", "they"},       {"us", "them"},       {"our", "their"},     {"ours", "theirs"},   {"ourselves", "themselves"},
        {"i'm", "they're"},   {"i've", "they've"},  {"i'll", "they'll"},  {"i'd", "they'd"},    {"we're", "they're"},
        {"we've", "they've"}, {"we'll", "they'll"}, {"we'd", "they'd"}};
    static const std::map<std::string, std::string> agreeing = {{"am", "are"}, {"was", "were"}};
    static const std::set<std::string> addressing = {"you",     "your", "yours", "yourself", "yourselves", "you're", "you've",
                                                     "you'll",  "you'd", "ye",    "thee",     "thou",       "thy",    "thine"};
    const std::map<std::string, std::string>& turns = to_them ? about_them : to_you;
    std::string text = one_line(plain_punctuation(words));
    while (!text.empty() && (text.back() == '.' || text.back() == '!' || text.back() == '?' || text.back() == ' ')) text.pop_back();
    std::string out, word;
    bool subject = false;  // the word before was "I": "am" and "was" agree with "you" and "they"
    bool first = true;
    bool refused = false;
    const auto flush = [&] {
        if (word.empty()) return;
        std::string lower = word;
        for (char& c : lower) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        if (!to_them && addressing.count(lower)) refused = true;
        std::string said = word;
        if (const auto turn = turns.find(lower); turn != turns.end()) {
            said = turn->second;
        } else if (const auto agree = agreeing.find(lower); subject && agree != agreeing.end()) {
            said = agree->second;
        } else if (first) {
            // A word any sentence may begin with is lowered; a name keeps its capital.
            const std::vector<std::string>& common = common_capitals();
            if (std::find(common.begin(), common.end(), word) != common.end()) said = lower;
        }
        subject = lower == "i";
        first = false;
        out += said;
        word.clear();
    };
    for (const char c : text) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '\'' || static_cast<unsigned char>(c) >= 0x80) {
            word += c;
        } else {
            flush();
            out += c;
        }
    }
    flush();
    return refused ? std::string() : out;
}

}  // namespace

std::string said_back(const std::string& words) { return turned(words, false); }

std::string of_them(const std::string& words) { return turned(words, true); }

}  // namespace communicator::text
