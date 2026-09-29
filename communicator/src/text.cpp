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

std::string lowered_word(const std::string& word) {
    std::string lower = word;
    for (char& c : lower) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return lower;
}

// Whether `next` makes what comes before it a plan or a thing under way: "on my way", "headed for",
// "looking for". Said back days later such a thing is past ("you were on your way"); who someone is
// stays as it is ("you're Aldric").
bool under_way(const std::string& next) {
    static const std::set<std::string> words = {"on", "off", "headed", "bound", "after", "back", "away", "out", "about"};
    return words.count(next) != 0 || (next.size() > 4 && next.compare(next.size() - 3, 3, "ing") == 0);
}

// The stranger's own words with the first person turned: to the stranger ("you") or about them
// ("they"), what was under way or to come shifted back, as words told days before are ("I'm on my
// way" as "you were on your way", "I'll be back" as "you'd be back"; a judge heard "you're on your
// way" three days on as still true, 2026-09-27). Empty, turned to the stranger, when they speak to
// someone or of "we".
std::string turned(const std::string& words, bool to_them) {
    static const std::map<std::string, std::string> to_you = {
        {"i", "you"},      {"me", "you"},      {"my", "your"},  {"mine", "yours"}, {"myself", "yourself"},
        {"i'm", "you're"}, {"i've", "you've"}, {"i'll", "you'd"}, {"i'd", "you'd"}};
    static const std::map<std::string, std::string> about_them = {
        {"i", "they"},        {"me", "them"},       {"my", "their"},      {"mine", "theirs"},   {"myself", "themselves"},
        {"we", "they"},       {"us", "them"},       {"our", "their"},     {"ours", "theirs"},   {"ourselves", "themselves"},
        {"i'm", "they're"},   {"i've", "they've"},  {"i'll", "they'd"},   {"i'd", "they'd"},    {"we're", "they're"},
        {"we've", "they've"}, {"we'll", "they'd"},  {"we'd", "they'd"}};
    static const std::set<std::string> addressing = {"you",     "your", "yours", "yourself", "yourselves", "you're", "you've",
                                                     "you'll",  "you'd", "ye",    "thee",     "thou",       "thy",    "thine"};
    // Nor words of "we": whether the speaker was one of them the words do not say, and "We lost two good
    // people out there", said back as "you lost two good people", took a loss the speaker had called
    // their own and handed it back (two judges of one hero session, 2026-09-28).
    static const std::set<std::string> plural = {"we", "us", "our", "ours", "ourselves", "we're", "we've", "we'll", "we'd"};
    const std::map<std::string, std::string>& turns = to_them ? about_them : to_you;
    const std::string were = to_them ? "they were" : "you were";
    std::string text = one_line(plain_punctuation(words));
    while (!text.empty() && (text.back() == '.' || text.back() == '!' || text.back() == '?' || text.back() == ' ')) text.pop_back();
    // The words, and what stands between them.
    std::vector<std::string> pieces;
    std::vector<std::size_t> word_at;  // which pieces are words
    std::string word, between;
    for (const char c : text) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '\'' || static_cast<unsigned char>(c) >= 0x80) {
            if (!between.empty()) pieces.push_back(std::move(between)), between.clear();
            word += c;
        } else {
            if (!word.empty()) word_at.push_back(pieces.size()), pieces.push_back(std::move(word)), word.clear();
            between += c;
        }
    }
    if (!word.empty()) word_at.push_back(pieces.size()), pieces.push_back(std::move(word));
    if (!between.empty()) pieces.push_back(std::move(between));
    // Each word as said, lowered, read before any is turned.
    std::vector<std::string> said;
    for (const std::size_t at : word_at) said.push_back(lowered_word(pieces[at]));
    const auto word_after = [&](std::size_t k) { return k + 1 < said.size() ? said[k + 1] : std::string(); };
    bool refused = false;
    for (std::size_t k = 0; k < word_at.size(); ++k) {
        std::string& piece = pieces[word_at[k]];
        const std::string& lower = said[k];
        const std::string before = k > 0 ? said[k - 1] : std::string();
        const bool subject = before == "i" || before == "we";
        if (!to_them && (addressing.count(lower) || plural.count(lower))) refused = true;
        if ((lower == "i'm" || lower == "we're") && under_way(word_after(k))) {
            piece = were;
        } else if (const auto turn = turns.find(lower); turn != turns.end()) {
            piece = turn->second;
        } else if (subject && (lower == "am" || lower == "are")) {
            piece = under_way(word_after(k)) ? "were" : "are";
        } else if (subject && lower == "was") {
            piece = "were";
        } else if (subject && lower == "will") {
            piece = "would";
        } else if (k == 0) {
            // A word any sentence may begin with is lowered; a name keeps its capital.
            const std::vector<std::string>& common = common_capitals();
            if (std::find(common.begin(), common.end(), piece) != common.end()) piece = lower;
        }
    }
    if (refused) return {};
    std::string out;
    for (const std::string& piece : pieces) out += piece;
    return out;
}

}  // namespace

std::string said_back(const std::string& words) { return turned(words, false); }

std::string of_them(const std::string& words) { return turned(words, true); }

}  // namespace communicator::text
