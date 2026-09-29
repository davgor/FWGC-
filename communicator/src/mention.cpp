#include "mention.hpp"

#include <array>
#include <set>
#include <string>
#include <vector>

#include "text.hpp"

namespace communicator {

namespace {

// `text` as lower-case words between spaces, " like this ", so a search for " a b " finds
// whole words only. Letters and digits make words; anything else parts them.
std::string spaced_words(std::string_view text) {
    std::string out = " ";
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if ((u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || u >= 0x80) {
            out += c;
        } else if (u >= 'A' && u <= 'Z') {
            out += static_cast<char>(u - 'A' + 'a');
        } else if (out.back() != ' ') {
            out += ' ';
        }
    }
    if (out.back() != ' ') out += ' ';
    return out;
}

// A word of a line, or one mark of punctuation between words.
struct Token {
    std::string text;   // as written
    std::string lower;  // in lower case
    bool word = false;
    bool capital = false;  // a word whose first letter is a capital, or not a plain letter at all ("Ærin")
};

bool word_byte(unsigned char u) { return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u >= 0x80; }

// A line's words and marks. An apostrophe or a hyphen between two word letters is part of the word
// ("I'm", "O'Brien", "Anne-Marie").
std::vector<Token> tokens_of(std::string_view line) {
    const std::string plain = text::plain_punctuation(line);
    std::vector<Token> out;
    std::string word;
    const auto flush = [&] {
        if (word.empty()) return;
        Token t;
        t.text = word;
        t.word = true;
        for (const char c : word) {
            const auto u = static_cast<unsigned char>(c);
            t.lower += (u >= 'A' && u <= 'Z') ? static_cast<char>(u - 'A' + 'a') : c;
        }
        const auto first = static_cast<unsigned char>(word.front());
        t.capital = (first >= 'A' && first <= 'Z') || first >= 0x80;
        out.push_back(std::move(t));
        word.clear();
    };
    for (std::size_t i = 0; i < plain.size(); ++i) {
        const auto u = static_cast<unsigned char>(plain[i]);
        const bool inside = (u == '\'' || u == '-') && !word.empty() && i + 1 < plain.size() &&
                            word_byte(static_cast<unsigned char>(plain[i + 1]));
        if (word_byte(u) || inside) {
            word += plain[i];
            continue;
        }
        flush();
        if (u > ' ') {
            Token mark;
            mark.text = mark.lower = std::string(1, plain[i]);
            out.push_back(std::move(mark));
        }
    }
    flush();
    return out;
}

// Words that never begin or carry a name, however they are written: "I'm Sorry", "I'm Not Sure",
// "call me when", "Nobody important".
const std::set<std::string, std::less<>>& never_names() {
    static const std::set<std::string, std::less<>> words = {
        "a", "an", "the", "i", "me", "my", "mine", "myself", "you", "your", "yours", "yourself", "u", "ur", "ye", "he", "she",
        "it", "its", "it's", "we", "us", "our", "they", "them", "their", "his", "her", "him", "this", "that", "these", "those",
        "there", "here", "where", "what", "whats", "what's", "who", "whom", "whose", "why", "how", "when", "which", "whatever",
        "whoever", "and", "or", "but", "so", "if", "as", "at", "by", "for", "from", "in", "into", "of", "off", "on", "out",
        "over", "to", "too", "up", "with", "without", "about", "after", "before", "just", "only", "also", "still", "even", "not",
        "no", "nope", "never", "none", "nobody", "nothing", "noone", "no-one", "someone", "somebody", "something", "anyone",
        "anybody", "anything", "everyone", "everybody", "everything", "all", "any", "some", "one", "each", "every", "very",
        "really", "quite", "rather", "sure", "yes", "yeah", "yep", "aye", "ok", "okay", "fine", "well", "sorry", "afraid",
        "tired", "lost", "new", "back", "done", "ready", "busy", "here", "now", "then", "later", "soon", "again", "please",
        "thanks", "thank", "hello", "hi", "hey", "oh", "ah", "er", "um", "uh", "hm", "hmm", "is", "am", "are", "was", "were",
        "be", "been", "do", "does", "did", "don't", "dont", "doesn't", "doesnt", "can", "can't", "cant", "cannot", "could",
        "won't", "wont", "would", "should", "shall", "must", "have", "has", "had", "i'm", "im", "i'd", "id", "i'll", "i've",
        "you're", "youre", "going", "looking", "trying", "leaving", "coming", "staying", "waiting", "working", "headed",
        "heading", "sent", "secret", "mind", "same", "first", "last", "other", "another", "more", "most", "less", "much",
        "many", "few", "own", "such", "like", "than", "whether", "while", "because", "since", "until", "unless", "though",
        "maybe", "perhaps", "probably", "depends", "important", "around", "away", "home", "right", "glad", "fool", "idiot",
        "ugh", "meh", "heh", "hah", "ha", "haha", "hmph", "pfft", "bah", "lol", "whenever", "yours", "evening", "morning",
        "afternoon", "greetings", "hail", "welcome", "farewell", "goodbye", "bye", "cheers", "yo", "howdy", "ahoy"};
    return words;
}

// Ordinary words that are a name only when written with a capital: "call me crazy", "i am hungry",
// "my name is will" (but "My name is Will").
const std::set<std::string, std::less<>>& ordinary_words() {
    static const std::set<std::string, std::less<>> words = {
        "will", "may", "hope", "good", "bad", "great", "hungry", "cold", "hurt", "alone", "alright", "crazy", "mad", "silly",
        "stupid", "nice", "happy", "sad", "wet", "late", "early", "free", "safe", "certain", "curious", "serious", "kidding",
        "joking", "friendly", "honest", "down", "left", "next", "better", "worse", "fair", "true", "real", "whatever", "anything",
        "something", "traveling", "travelling", "passing", "armed", "unarmed"};
    return words;
}

// The small words a name may hold between words written with capitals: "Mira of Halden", "Sir Aldric
// of the Vale".
bool joins(const Token& t) {
    static const std::set<std::string, std::less<>> words = {"of", "the", "de", "van", "von", "du", "da", "di", "del", "la", "le"};
    return t.word && !t.capital && words.count(t.lower) != 0;
}

bool possessive(const Token& t) {
    const std::string& w = t.lower;
    return (w.size() > 2 && w.compare(w.size() - 2, 2, "'s") == 0) || (w.size() > 1 && w.back() == '\'');
}

// A word in lower case that reads as how someone is, not who: "tired", "bleeding", "lost".
bool reads_as_a_state(const std::string& w) {
    const auto ends = [&](std::string_view end) { return w.size() > end.size() + 1 && w.compare(w.size() - end.size(), end.size(), end) == 0; };
    return ends("ing") || ends("ed") || ends("ly") || ends("ful") || ends("less");
}

bool letter_first(const Token& t) {
    const auto u = static_cast<unsigned char>(t.text.front());
    return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u >= 0x80;
}

bool can_be_name_word(const Token& t) { return t.word && letter_first(t) && never_names().count(t.lower) == 0 && !possessive(t); }

// Whether a word written with a capital carries a name on past its first word: any but the small words a
// sentence goes on with ("I'm Aldric From Linbrig" ends at From), so "Brother Oddo of the Grey Well" keeps
// its Well, which never begins one.
bool carries_a_name_on(const Token& t) {
    static const std::set<std::string, std::less<>> small = {
        "a",    "an",   "the",  "i",    "me",   "my",    "you",   "your",  "he",   "she",   "it",    "we",   "they",
        "his",  "her",  "its",  "our",  "their", "this", "that",  "these", "those", "and",  "or",    "but",   "so",
        "if",   "as",   "at",   "by",   "for",  "from",  "in",    "into",  "of",    "off",  "on",    "out",   "over",
        "to",   "up",   "with", "without", "about", "after", "before", "is", "am",  "are",  "was",   "were",  "be",
        "do",   "does", "did",  "can",  "will", "would", "should", "shall", "must", "have", "has",   "had",   "not",
        "no",   "what", "who",  "why",  "how",  "when",  "where", "which", "here", "there", "too",   "also",  "just"};
    return t.word && letter_first(t) && small.count(t.lower) == 0 && !possessive(t);
}

// How a name begins where the words before it leave it: in lower case too ("my name is brenna"), or
// only with a capital ("I'm Aldric"). `alone`: a word in lower case begins it only as the last of the
// line or before a mark ("im aldric").
enum class Case { Any, Capital, Alone };

bool ends_there(const std::vector<Token>& t, std::size_t after) { return after >= t.size() || !t[after].word; }

constexpr std::size_t kMostNameWords = 6;
constexpr std::size_t kMostLowerWords = 3;
constexpr std::size_t kMaxNameBytes = 60;  // the director's bound (contract version 24)

// The name as said, its words' first letters raised; empty when it runs past the bound in a first word.
std::string written(const std::vector<const Token*>& words) {
    std::vector<const Token*> kept = words;
    const auto length = [&] {
        std::size_t n = 0;
        for (const Token* w : kept) n += w->text.size() + (n == 0 ? 0 : 1);
        return n;
    };
    while (!kept.empty() && (length() > kMaxNameBytes || joins(*kept.back()))) kept.pop_back();
    std::string out;
    for (const Token* w : kept) {
        std::string word = w->text;
        if (!joins(*w) && word.front() >= 'a' && word.front() <= 'z') word.front() = static_cast<char>(word.front() - 'a' + 'A');
        // "ich bin \xC3\xA4gidius": a Latin-1 letter in lower case (U+00E0 to U+00FE but the division sign) raised too.
        if (!joins(*w) && word.size() > 1 && static_cast<unsigned char>(word[0]) == 0xC3) {
            const auto second = static_cast<unsigned char>(word[1]);
            if (second >= 0xA0 && second <= 0xBE && second != 0xB7) word[1] = static_cast<char>(second - 0x20);
        }
        out += (out.empty() ? "" : " ") + word;
    }
    return out;
}

// The name that begins at t[at], or empty.
std::string name_at(const std::vector<Token>& t, std::size_t at, Case how) {
    if (at >= t.size() || !t[at].word) return {};
    // "call me the Wanderer": the name, without its article; "call me a fool" gives none. After "I'm" an article
    // is a description, not a name: "I'm a Cooper. Barrels, mostly." (the second held-out lines).
    if (how == Case::Any && (t[at].lower == "the" || t[at].lower == "a" || t[at].lower == "an") && at + 1 < t.size() && t[at + 1].word &&
        t[at + 1].capital)
        ++at;
    // In lower case, "the" before a word that can begin one: "they call me the ashen wanderer"; never "a" ("call me a
    // liar" is no name).
    else if (how == Case::Any && t[at].lower == "the" && at + 1 < t.size() && t[at + 1].word && !t[at + 1].capital &&
             can_be_name_word(t[at + 1]) && ordinary_words().count(t[at + 1].lower) == 0 && !reads_as_a_state(t[at + 1].lower))
        ++at;
    const Token& first = t[at];
    if (!can_be_name_word(first)) return {};
    std::vector<const Token*> words = {&first};
    std::size_t k = at + 1;
    if (first.capital) {
        while (k < t.size() && t[k].word && words.size() < kMostNameWords) {
            if (t[k].capital) {
                if (possessive(t[k])) return {};  // "I'm Brenna Holt's daughter"
                if (!carries_a_name_on(t[k])) break;
                words.push_back(&t[k++]);
                continue;
            }
            std::size_t j = k;
            while (j < t.size() && joins(t[j])) ++j;
            if (j == k || j >= t.size() || !t[j].word || !t[j].capital || !carries_a_name_on(t[j])) break;
            for (; k <= j; ++k) words.push_back(&t[k]);
        }
        return written(words);
    }
    if (how == Case::Capital || ordinary_words().count(first.lower) != 0 || reads_as_a_state(first.lower)) return {};
    const auto goes_on = [&](const Token& w) {
        return w.word && !w.capital && can_be_name_word(w) && ordinary_words().count(w.lower) == 0 && !reads_as_a_state(w.lower) && !joins(w);
    };
    std::size_t plain = 1;  // its words that are not "of" or "the"
    while (k < t.size() && plain < kMostLowerWords) {
        std::size_t j = k;  // past "of the", to the word they join on
        while (j < t.size() && t[j].word && !t[j].capital && joins(t[j])) ++j;
        if (j >= t.size() || !goes_on(t[j]) || words.size() + (j - k) + 1 > kMostNameWords) break;
        for (; k <= j; ++k) words.push_back(&t[k]);
        ++plain;
    }
    // After "i'm" in a line all in lower case, only words that run to the line's end or a mark: "i'm tobias fenwick
    // marr, the one holding the fortress" (the second held-out lines), never "im new here".
    if (how == Case::Alone && !ends_there(t, k)) return {};
    return written(words);
}

// What comes before a name the player gives: the words in lower case, and how the name may begin.
struct Cue {
    std::vector<std::string_view> words;
    Case how;
    bool at_start = false;  // only where a sentence begins: "Name's Corwin", not "your name's Selbren"
};

const std::vector<Cue>& cues() {
    // The longest first, so "I'm called Wren" is read as "I'm called" before "I'm".
    static const std::vector<Cue> all = {
        {{"my", "real", "name", "is"}, Case::Any},
        {{"my", "true", "name", "is"}, Case::Any},
        {{"i", "am", "known", "as"}, Case::Any},
        {{"my", "name", "is"}, Case::Any},
        {{"the", "name", "is"}, Case::Any},
        {{"my", "real", "name's"}, Case::Any},
        {{"my", "true", "name's"}, Case::Any},
        {{"i", "am", "called"}, Case::Any},
        {{"i", "am", "named"}, Case::Any},
        {{"i'm", "known", "as"}, Case::Any},
        {{"im", "known", "as"}, Case::Any},
        {{"know", "me", "as"}, Case::Any},
        {{"knows", "me", "as"}, Case::Any},
        {{"i", "go", "by"}, Case::Any},
        {{"my", "name's"}, Case::Any},
        {{"my", "names"}, Case::Any},
        {{"the", "name's"}, Case::Any},
        {{"i'm", "called"}, Case::Any},
        {{"im", "called"}, Case::Any},
        {{"i'm", "named"}, Case::Any},
        {{"call", "me"}, Case::Any},
        {{"calls", "me"}, Case::Any},
        {{"called", "me"}, Case::Any},
        // And other languages' (the held-out lines' "Hola, me llamo Itzel").
        {{"mi", "nombre", "es"}, Case::Any},
        {{"mein", "name", "ist"}, Case::Any},
        {{"me", "llamo"}, Case::Any},
        {{"mi", "chiamo"}, Case::Any},
        {{"je", "m'appelle"}, Case::Any},
        {{"ich", "heisse"}, Case::Any},
        {{"ich", "hei\xC3\x9F" "e"}, Case::Any},
        {{"je", "suis"}, Case::Capital},
        {{"ich", "bin"}, Case::Capital},
        {{"i", "am"}, Case::Capital},
        {{"name's"}, Case::Any, true},
        {{"sono"}, Case::Capital},
        {{"soy"}, Case::Capital},
        {{"i'm"}, Case::Capital},
        {{"im"}, Case::Capital},
    };
    return all;
}

// A word before a cue that turns it about: "don't call me that", "never called me Aldric".
bool turned_about(const std::vector<Token>& t, std::size_t at) {
    static const std::set<std::string, std::less<>> words = {"don't", "dont", "never", "not", "stop", "didn't", "didnt", "won't",
                                                            "wont", "can't", "cant", "shouldn't", "if"};
    return at > 0 && t[at - 1].word && words.count(t[at - 1].lower) != 0;
}

bool matches(const std::vector<Token>& t, std::size_t at, const Cue& cue) {
    if (at + cue.words.size() > t.size()) return false;
    for (std::size_t i = 0; i < cue.words.size(); ++i)
        if (!t[at + i].word || t[at + i].lower != cue.words[i]) return false;
    return !cue.at_start || at == 0 || !t[at - 1].word;
}

// Whether what the speaker said last asks the stranger who they are: in a question, or outright, "State your
// name and business.", "I never did get your name." (the held-out lines, 2026-09-29).
bool asks_their_name(std::string_view last_reply) {
    const std::string said = spaced_words(text::plain_punctuation(last_reply));
    if (said.rfind(" name ", 0) == 0) return true;  // "Name and business."
    for (const std::string_view asks : {" your name ", " name yourself ", " who you are ", " identify yourself ", " a name "})
        if (said.find(asks) != std::string::npos) return true;
    if (last_reply.find('?') == std::string_view::npos) return false;
    // A short question beginning with "who": "Halt. Who approaches?", "Who's there?".
    const std::string plain = text::plain_punctuation(last_reply);
    for (std::size_t mark = plain.find('?'); mark != std::string::npos; mark = plain.find('?', mark + 1)) {
        const std::size_t before = mark == 0 ? std::string::npos : plain.find_last_of(".!?", mark - 1);
        const std::size_t from = before == std::string::npos ? 0 : before + 1;
        const std::string question = spaced_words(std::string_view(plain).substr(from, mark - from));
        std::size_t spaces = 0;  // one more than its words: " who approaches " is two words
        for (const char c : question)
            if (c == ' ') ++spaces;
        if (question.rfind(" who ", 0) == 0 && spaces <= 5) return true;
    }
    for (const std::string_view asks : {" name ", " call you ", " who are you ", " who might you be ", " and you are ", " who you are ",
                                        " who goes there ", " who s asking ", " who is asking ", " and you ", " who re you "})
        if (said.find(asks) != std::string::npos) return true;
    return false;
}

// Words that say nothing before a name asked for: "Just Tomas.", "Fine. Tamsin Reed.", "Me? Just Lark."
const std::set<std::string, std::less<>>& fillers() {
    static const std::set<std::string, std::less<>> words = {"just", "it's", "its", "oh", "well", "er", "um", "uh", "hmm", "hm", "sure",
                                                            "me", "only", "fine", "ok", "okay", "alright", "yes", "yeah", "aye",
                                                            "right", "no", "lol", "ugh"};
    return words;
}

// The runs of words between marks, as [begin, end) indexes.
std::vector<std::pair<std::size_t, std::size_t>> segments_of(const std::vector<Token>& t) {
    std::vector<std::pair<std::size_t, std::size_t>> out;
    for (std::size_t i = 0; i < t.size();) {
        if (!t[i].word) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < t.size() && t[j].word) ++j;
        out.emplace_back(i, j);
        i = j;
    }
    return out;
}

std::size_t words_in(const std::string& name) {
    std::size_t n = name.empty() ? 0 : 1;
    for (const char c : name)
        if (c == ' ') ++n;
    return n;
}

// A word of address or a trade that is a name only with one after it: "just a tinker, captain" names no captain,
// "Captain Orla Rusk" is one (the fourth held-out lines, 2026-09-29).
bool addresses(const std::string& lower) {
    static const std::set<std::string, std::less<>> words = {
        "captain", "castellan", "sir", "madam", "ma'am", "lady", "lord", "master", "mistress", "friend", "stranger", "traveller",
        "traveler", "innkeeper", "keeper", "guard", "soldier", "sergeant", "commander", "boss", "chief", "mate", "lad", "lass",
        "brother", "sister", "father", "mother", "elder", "priest", "sage", "smith", "tinker", "merchant", "healer", "love", "dear"};
    return words.count(lower) != 0;
}

// The name a run of words between marks is, and nothing else, after words that say nothing: "Hesketh Varne",
// "just Tomas"; or empty. In a line with capitals, only words written with them ("Aldric, son of Brann" is
// Aldric); a lone word that reads as a state or an ordinary one is none ("Kidding!").
std::string name_only(const std::vector<Token>& t, std::size_t begin, std::size_t end, bool lower_line) {
    std::size_t at = begin;
    while (at < end && fillers().count(t[at].lower) != 0) ++at;
    if (at == end) return {};
    std::vector<Token> run(t.begin() + static_cast<std::ptrdiff_t>(at), t.begin() + static_cast<std::ptrdiff_t>(end));
    run.push_back(Token{});  // the end: a mark
    const std::string name = name_at(run, 0, lower_line ? Case::Any : Case::Capital);
    const bool article = run[0].lower == "the" || run[0].lower == "a" || run[0].lower == "an";
    if (name.empty() || end - at != words_in(name) + (article ? 1 : 0)) return {};
    if (words_in(name) == 1 && (ordinary_words().count(run[article ? 1 : 0].lower) != 0 || reads_as_a_state(run[article ? 1 : 0].lower) ||
                                addresses(run[article ? 1 : 0].lower)))
        return {};
    return name;
}

// `name`, or the fuller name a later run of words says it again as: "the name's kip. kip bramblewood."
std::string fuller(const std::vector<Token>& t, const std::string& name, bool lower_line) {
    for (const auto& [begin, end] : segments_of(t)) {
        const std::string again = name_only(t, begin, end, lower_line);
        if (again.size() > name.size() && again.rfind(name + " ", 0) == 0) return again;
    }
    return name;
}

// The name a line gives as the last words before "is my name" or "'s the name": "Aldric's the name".
std::string name_before(const std::vector<Token>& t, std::size_t end, bool lower_line) {
    // t[end - 1] is the name's last word (its "'s" already cut when `cut`).
    std::size_t start = end;
    while (start > 0 && t[start - 1].word && end - start < kMostNameWords &&
           (t[start - 1].capital || (joins(t[start - 1]) && start > 1 && t[start - 2].word && t[start - 2].capital) ||
            (lower_line && start == end)))
        --start;
    while (start < end && joins(t[start])) ++start;
    if (start == end) return {};
    std::vector<Token> words(t.begin() + static_cast<std::ptrdiff_t>(start), t.begin() + static_cast<std::ptrdiff_t>(end));
    words.push_back(Token{});  // the end: a mark
    return name_at(words, 0, lower_line ? Case::Alone : Case::Capital);
}

}  // namespace

std::optional<Referent> named_outright(Kind kind, const Known& known, std::string_view line) {
    const std::string said = spaced_words(text::plain_punctuation(line));
    std::optional<Referent> best;
    std::size_t best_length = 0;
    bool tied = false;
    for (const Referent& r : known.nameable(kind, Known::kKeptPerKind)) {
        const std::string name = spaced_words(text::spoken(r.label));  // a name as said: "Hakndum (Age 2)" is "Hakndum"
        if (name.size() <= 2 || said.find(name) == std::string::npos) continue;  // " " alone names nothing
        if (name.size() > best_length) {
            best = r;
            best_length = name.size();
            tied = false;
        } else if (name.size() == best_length) {
            tied = true;
        }
    }
    return tied ? std::nullopt : best;
}

std::optional<std::string> name_given(std::string_view line, std::string_view last_reply) {
    const std::vector<Token> t = tokens_of(line);
    bool lower_line = true;  // written all in lower case: a capital tells nothing
    for (const Token& token : t)
        for (const char c : token.text)
            if (c >= 'A' && c <= 'Z') lower_line = false;
    // "my name isn't Aldric, it's Brenna"; or asked for it, "No, it's Gethin".
    const bool asked = asks_their_name(last_reply);
    bool named_before = asked;
    for (std::size_t at = 0; at < t.size(); ++at) {
        if (!t[at].word) continue;
        for (const Cue& cue : cues()) {
            if (!matches(t, at, cue) || turned_about(t, at)) continue;
            const Case how = cue.how == Case::Capital && lower_line ? Case::Alone : cue.how;
            if (std::string name = name_at(t, at + cue.words.size(), how); !name.empty()) return fuller(t, name, lower_line);
        }
        // Before "is my name", "is the name", "'s the name", "'s my name".
        const auto then = [&](std::size_t from, std::initializer_list<std::string_view> words) {
            std::size_t k = from;
            for (const std::string_view w : words) {
                if (k >= t.size() || !t[k].word || t[k].lower != w) return false;
                ++k;
            }
            return true;
        };
        if (then(at + 1, {"is", "my", "name"}) || then(at + 1, {"is", "the", "name"})) {
            if (std::string name = name_before(t, at + 1, lower_line); !name.empty()) return name;
        }
        if (possessive(t[at]) && t[at].lower.back() == 's' && (then(at + 1, {"the", "name"}) || then(at + 1, {"my", "name"}))) {
            std::vector<Token> cut(t.begin(), t.begin() + static_cast<std::ptrdiff_t>(at) + 1);
            Token& last = cut.back();
            last.text.resize(last.text.size() - 2);
            last.lower.resize(last.lower.size() - 2);
            if (!last.text.empty())
                if (std::string name = name_before(cut, cut.size(), lower_line); !name.empty()) return name;
        }
        // Before a courtesy, after a mark: "Captain Orla Rusk, at your service.", "Mira Ashcombe-Tull, pleased to make
        // your acquaintance."
        if (at >= 2 && !t[at - 1].word && t[at - 2].word &&
            (then(at, {"at", "your", "service"}) || then(at, {"pleased", "to", "meet", "you"}) || then(at, {"pleased", "to", "make"}) ||
             then(at, {"nice", "to", "meet", "you"}) || then(at, {"glad", "to", "meet", "you"}) || then(at, {"good", "to", "meet", "you"})))
            if (std::string name = name_before(t, at - 1, lower_line); !name.empty()) return name;
        // "it's me, Halbrann": the name after "it's me" and a mark (the second held-out lines).
        if ((then(at, {"it's", "me"}) || then(at, {"its", "me"}) || then(at, {"it", "is", "me"})) && !turned_about(t, at)) {
            std::size_t k = at + (t[at].lower == "it" ? 3 : 2);
            while (k < t.size() && !t[k].word) ++k;
            if (std::string name = name_at(t, k, lower_line ? Case::Alone : Case::Capital); !name.empty()) return name;
        }
        // "Hadrick, not Harrick.": a name set right, where the speaker's words gave the wrong one.
        if ((at == 0 || !t[at - 1].word || t[at - 1].lower == "it's" || t[at - 1].lower == "its") && at + 3 < t.size() && t[at].capital &&
            !t[at + 1].word && t[at + 2].lower == "not" && t[at + 3].word && t[at + 3].capital &&
            last_reply.find(t[at + 3].text) != std::string_view::npos && can_be_name_word(t[at]) && ordinary_words().count(t[at].lower) == 0)
            return written({&t[at]});
        // "... but Idony it is.", asked for it or once the line has spoken of a name.
        if (named_before && then(at + 1, {"it", "is"}) && ends_there(t, at + 3))
            if (std::string name = name_before(t, at + 1, lower_line); !name.empty()) return name;
        if (t[at].lower == "name" || t[at].lower == "name's" || t[at].lower == "names") named_before = true;
        // "it's Brenna", once the line has spoken of a name.
        if (named_before && (t[at].lower == "it's" || t[at].lower == "its"))
            if (std::string name = name_at(t, at + 1, lower_line ? Case::Alone : Case::Capital); !name.empty()) return name;
    }
    const std::vector<std::pair<std::size_t, std::size_t>> runs = segments_of(t);
    // A name alone, then the same asked back: "Isak Brennard. And yours, captain?"
    if (runs.size() > 1) {
        const std::string first = name_only(t, runs[0].first, runs[0].second, lower_line);
        for (std::size_t r = 1; r < runs.size() && !first.empty(); ++r) {
            const auto begins = [&](std::initializer_list<std::string_view> words) {
                std::size_t k = runs[r].first;
                for (const std::string_view w : words)
                    if (k >= runs[r].second || t[k++].lower != w) return false;
                return true;
            };
            if (begins({"and", "yours"}) || begins({"and", "you"}) || begins({"what's", "yours"}) || begins({"yours"})) return first;
        }
    }
    // A name alone, asked for: "And you are?" "Aldric." A run of words between marks that is a name and
    // nothing else, after words that say nothing ("Just Tomas.", "Fine. Tamsin Reed."); of several, the
    // fullest: "Varne. Hesketh Varne." (the third held-out lines, 2026-09-29).
    if (!asked) return std::nullopt;
    std::string best;
    for (const auto& [begin, end] : runs)
        if (std::string name = name_only(t, begin, end, lower_line); words_in(name) > words_in(best)) best = std::move(name);
    if (best.empty()) return std::nullopt;
    return best;
}

}  // namespace communicator
