#include "communicator/truth.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

#include "communicator/voice.hpp"
#include "memory.hpp"
#include "news.hpp"
#include "reply_shape.hpp"
#include "text.hpp"

namespace communicator {

namespace {

using orchestrator::ActKind;
using orchestrator::Fact;
using orchestrator::FactKind;
using orchestrator::FactPacket;
using orchestrator::Outcome;

// Kinds of problem. The first two are never backed by any packet (never_backed()).
constexpr std::string_view kAcquaintance = "an acquaintance the packet does not hold";
constexpr std::string_view kCounted = "a distance or a time counted out";
constexpr std::string_view kName = "a name no fact gives";
constexpr std::string_view kRange = "a distance no fact holds";
constexpr std::string_view kDanger = "a danger no place or nest has";
constexpr std::string_view kSince = "when they met, which the speaker's memory does not say";
constexpr std::string_view kMet = "a meeting the speaker's memory does not hold";
constexpr std::string_view kFirst = "a first meeting, which the speaker's memory contradicts";
constexpr std::string_view kTold = "something told before, which the speaker has not said";
constexpr std::string_view kWork = "the speaker's own work, which their memory does not say";
constexpr std::string_view kVerdict = "words against the verdict";
constexpr std::string_view kWeather = "weather the packet does not report";
constexpr std::string_view kTime = "a time of day or season the packet does not report";
constexpr std::string_view kEased = "being talked down, which the speaker's memory does not hold";
constexpr std::string_view kOverheard = "overhearing the stranger, which the speaker's memory does not hold";
constexpr std::string_view kNews = "news the speaker has not heard";
constexpr std::string_view kNoNews = "no news, from a speaker who has heard some";
constexpr std::string_view kSaid = "the stranger's own words, which the speaker's memory does not hold";

std::string lowered(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool letter(char c) {
    const auto u = static_cast<unsigned char>(c);
    return std::isalpha(u) != 0 || u >= 0x80;
}

// Where `phrase` stands in `text` as whole words, at or after `from`; npos when it does not, or
// when it is empty (which would stand everywhere, and never let blank() finish).
std::size_t find_words(const std::string& text, std::string_view phrase, std::size_t from = 0) {
    if (phrase.empty()) return std::string::npos;
    for (std::size_t at = text.find(phrase, from); at != std::string::npos; at = text.find(phrase, at + 1)) {
        const std::size_t end = at + phrase.size();
        if ((at == 0 || !letter(text[at - 1])) && (end >= text.size() || !letter(text[end]))) return at;
    }
    return std::string::npos;
}

bool says(const std::string& text, std::string_view phrase) { return find_words(text, phrase) != std::string::npos; }

// Blanks every whole-word `phrase` in `text`, so later checks do not read it; whether there was one.
bool blank(std::string& text, std::string_view phrase) {
    bool found = false;
    for (std::size_t at = find_words(text, phrase); at != std::string::npos; at = find_words(text, phrase, at + phrase.size())) {
        text.replace(at, phrase.size(), phrase.size(), ' ');
        found = true;
    }
    return found;
}

bool word_char(char c) { return letter(c) || c == '\''; }

// The word before `at`, past spaces and commas ("me, right here" gives "me").
std::string word_before(const std::string& text, std::size_t at) {
    std::size_t end = at;
    while (end > 0 && (text[end - 1] == ' ' || text[end - 1] == ',')) --end;
    std::size_t begin = end;
    while (begin > 0 && word_char(text[begin - 1])) --begin;
    return text.substr(begin, end - begin);
}

// The word after `at`, past spaces.
std::string word_after(const std::string& text, std::size_t at) {
    std::size_t begin = at;
    while (begin < text.size() && text[begin] == ' ') ++begin;
    std::size_t end = begin;
    while (end < text.size() && word_char(text[end])) ++end;
    return text.substr(begin, end - begin);
}

// What stands before `at` in its sentence, without the spaces and commas that end it.
std::string lead_of(const std::string& text, std::size_t at) {
    std::size_t begin = 0;
    for (std::size_t i = at; i > 0; --i)
        if (text[i - 1] == '.' || text[i - 1] == '!' || text[i - 1] == '?') {
            begin = i;
            break;
        }
    std::string lead = text.substr(begin, at - begin);
    const std::size_t first = lead.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    return lead.substr(first, lead.find_last_not_of(" ,;:-") + 1 - first);
}

std::vector<std::string> sentences(const std::string& text) {
    std::vector<std::string> out;
    std::string sentence;
    for (const char c : text) {
        sentence += c;
        if (c == '.' || c == '!' || c == '?') {
            out.push_back(sentence);
            sentence.clear();
        }
    }
    if (!sentence.empty()) out.push_back(sentence);
    return out;
}

std::string field_of(const Fact& fact, std::string_view key) {
    const std::string* value = fact.field(key);
    return value ? *value : std::string();
}

void add(std::vector<std::string>& out, std::string_view kind, std::string_view what) {
    std::string problem = std::string(kind) + ": " + std::string(what);
    if (std::find(out.begin(), out.end(), problem) == out.end()) out.push_back(std::move(problem));
}

// The words of `text` that begin with a capital or a letter outside ASCII, which no reply can
// write in lower case.
void capitals_of(std::string_view text, std::set<std::string>& out) {
    std::size_t i = 0;
    while (i < text.size()) {
        if (!letter(text[i])) {
            ++i;
            continue;
        }
        const std::size_t start = i;
        while (i < text.size() && letter(text[i])) ++i;
        const auto first = static_cast<unsigned char>(text[start]);
        if (!(first >= 'a' && first <= 'z')) out.emplace(text.substr(start, i - start));
    }
}

// Names: a capital past a sentence's start must be one of the packet's own, or "I"; at a
// sentence's start it may also be a word any sentence begins with, or the reply's fixed opening.
void names(std::string_view reply, const FactPacket& p, std::vector<std::string>& out) {
    std::set<std::string> own = {"I"};
    for (const std::string* said : {&p.persona.name, &p.persona.role, &p.persona.place}) capitals_of(*said, own);
    for (const Fact& fact : p.facts)
        for (const auto& [key, value] : fact.fields)
            if (!text::for_the_game(key)) capitals_of(value, own);
    // A sentence may begin with a common capital, or with a word of the reply's fixed opening or
    // of the stance a model's reply opens with ("Hmph", from a cold speaker making small talk).
    std::set<std::string> openers(common_capitals().begin(), common_capitals().end());
    const ReplyShape shape = reply_shape(p);
    for (const std::vector<std::string>* list : {&shape.openings, &shape.stance})
        for (const std::string& opening : *list) {
            std::set<std::string> words;
            capitals_of(opening, words);
            openers.insert(words.begin(), words.end());
        }
    bool start = true;
    std::size_t i = 0;
    while (i < reply.size()) {
        const char c = reply[i];
        if (!letter(c)) {
            if (c == '.' || c == '!' || c == '?') start = true;
            ++i;
            continue;
        }
        const std::size_t begin = i;
        while (i < reply.size() && letter(reply[i])) ++i;
        const std::string word(reply.substr(begin, i - begin));
        const auto first = static_cast<unsigned char>(word[0]);
        if (!(first >= 'a' && first <= 'z') && !own.count(word) && !(start && openers.count(word))) add(out, kName, word);
        start = false;
    }
}

// Distances and lengths of time counted out: no packet holds a number of miles or days, and the
// ranges it holds are said in words ("an hour's walk away"). What the speaker's memory says of
// when they met ("a few days ago") is left for since().
void counted(const std::string& said, const FactPacket& p, std::vector<std::string>& out) {
    static const std::set<std::string> numbers = {"one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
                                                  "eleven", "twelve", "twenty", "thirty", "forty", "fifty", "hundred", "dozen",
                                                  "several", "few"};
    static const std::set<std::string> units = {"mile",  "miles", "league", "leagues", "kilometre", "kilometres", "kilometer",
                                                "kilometers", "km", "pace", "paces", "foot", "feet", "yard", "yards", "day",
                                                "days", "night", "nights", "hour", "hours", "week", "weeks", "month", "months",
                                                "year", "years"};
    std::string text = said;
    const std::string since = since_words(remembered(p).since);
    if (!since.empty()) blank(text, since);
    blank(text, "own two feet");  // standing on them, not walking them
    std::vector<std::string> words;
    std::string word;
    for (const char c : text + " ") {
        if (letter(c) || std::isdigit(static_cast<unsigned char>(c))) {
            word += c;
        } else {
            if (!word.empty()) words.push_back(word);
            word.clear();
        }
    }
    for (std::size_t k = 0; k + 1 < words.size(); ++k) {
        const std::string& number = words[k];
        const std::string& unit = words[k + 1];
        const bool digits = std::all_of(number.begin(), number.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
        if (!(digits || numbers.count(number)) || !units.count(unit)) continue;
        if (number == "one" && unit == "day") continue;  // "one day", the idiom
        add(out, kCounted, number + " " + unit);
    }
    // "A day's march", "a week's ride": a journey's length. "An hour's walk" is the packet's own
    // range, which ranges() holds to a fact; "a day's work" goes nowhere.
    static const std::set<std::string> journeys = {"walk",  "walking", "ride",  "riding", "march", "marching", "journey",
                                                   "sail",  "sailing", "travel", "trek",  "climb", "flight",   "crossing",
                                                   "hike",  "trip",    "drive", "gallop", "row",  "going"};
    for (const std::string lead : {"a day's", "an hour's", "a week's", "a night's", "a month's", "a morning's", "an afternoon's",
                                   "an evening's", "a year's"})
        for (std::size_t at = find_words(said, lead); at != std::string::npos; at = find_words(said, lead, at + 1)) {
            const std::string journey = word_after(said, at + lead.size());
            if (journeys.count(journey) && !(lead == "an hour's" && journey == "walk")) add(out, kCounted, lead + " " + journey);
        }
}

// A range said in the packet's words must be one a fact holds. Each is read by its stem, "an
// hour's walk" with or without "away". "I'm right here" and "you're all right here" say where a
// person is, and "wave from a long way off" how far to keep.
void ranges(const std::string& said, const FactPacket& p, std::vector<std::string>& out) {
    static const std::vector<std::pair<std::string, std::string>> stems = {
        {"here", "right here"},     {"close", "close by"},  {"near", "a short walk"},
        {"walk", "an hour's walk"}, {"far", "a long walk"}, {"distant", "a long way off"}};
    static const std::set<std::string> people = {"i'm", "am", "i'll", "be", "you're", "you", "are", "we're", "we", "stay", "wait",
                                                 "all", "me", "us"};
    std::set<std::string> held;
    for (const Fact& fact : p.facts)
        if (const std::string range = field_of(fact, "range"); !text::range_words(range).empty()) held.insert(range);
    for (const auto& [range, stem] : stems)
        for (std::size_t at = find_words(said, stem); at != std::string::npos; at = find_words(said, stem, at + 1)) {
            if (range == "here" && people.count(word_before(said, at))) continue;
            if (range == "distant" && word_before(said, at) == "from") continue;
            if (!held.count(range)) add(out, kRange, stem);
            break;
        }
}

int highest(const FactPacket& p) {
    int most = 0;
    for (const Fact& fact : p.facts)
        for (const std::string key : {"threat", "danger"})
            if (const std::string value = field_of(fact, key); value.size() == 1 && value[0] >= '1' && value[0] <= '5')
                most = std::max(most, value[0] - '0');
    return most;
}

std::string verdict_of(const FactPacket& p) {
    for (const Fact& fact : p.facts)
        if (fact.kind == FactKind::Safety) return field_of(fact, "verdict");
    return {};
}

// "Deadly" needs a place or nest at the top of the scale, "dangerous" one near it or a verdict
// that it is not safe.
void dangers(const std::string& said, const FactPacket& p, std::vector<std::string>& out) {
    const int most = highest(p);
    if (says(said, "deadly") && most < 5) add(out, kDanger, "deadly");
    if (says(said, "dangerous") && most < 4 && verdict_of(p) != "unsafe") add(out, kDanger, "dangerous");
}

// When they met, against the memory's band ("some weeks ago" for weeks): a time said in a
// sentence about the stranger. "My word stands as it stood yesterday" says nothing of a meeting.
void since(const std::string& said, const FactPacket& p, std::vector<std::string>& out) {
    static const std::vector<std::pair<std::string, std::string>> phrases = {
        {"earlier today", "today"},  {"yesterday", "yesterday"}, {"a few days ago", "days"},     {"the other day", "days"},
        {"days ago", "days"},        {"last week", "weeks"},     {"a week ago", "weeks"},        {"some weeks ago", "weeks"},
        {"weeks ago", "weeks"},      {"last month", "months"},   {"a month ago", "months"},      {"some months ago", "months"},
        {"months ago", "months"},    {"last year", "years"},     {"a year ago", "years"},        {"years ago", "years"}};
    const std::string band = remembered(p).since;
    for (const std::string& sentence : sentences(said)) {
        if (!says(sentence, "you") && !says(sentence, "your")) continue;
        for (const auto& [phrase, when] : phrases)
            if (says(sentence, phrase) && when != band) add(out, kSince, phrase);
    }
}

// A word before "you finished", "you came back" that makes it a wish or a time to come: "I'd
// sooner you finished what you started", "the fire will be going when you're back".
const std::set<std::string>& wishes() {
    static const std::set<std::string> words = {"sooner", "rather", "if", "until", "till", "unless", "once", "when"};
    return words;
}

// Met before, or a first meeting, against the memory. Asking is not claiming ("have we met?"),
// "I don't think we've met" says they have not, and an invitation ("you'll be welcome back",
// "do come back again", "when you're back") or "I'll tell you again" says nothing of a meeting.
void met(const std::string& said, const FactPacket& p, std::vector<std::string>& out) {
    const Remembered m = remembered(p);
    std::string text = said;
    for (const std::string asked : {"have we met", "haven't we met", "had we met", "do i know you", "have i seen you"}) blank(text, asked);
    for (const std::string phrase : {"a new face", "never seen you", "haven't seen you before", "not seen you before",
                                     "we've never met", "we have never met", "never met you", "first time here", "your first time",
                                     "don't think we've met", "don't believe we've met", "don't think we have met",
                                     "don't believe we have met", "we haven't met", "we've not met", "we have not met"})
        if (blank(text, phrase) && m.again) add(out, kFirst, phrase);
    if (m.again) return;
    for (const std::string phrase : {"good to see you again", "nice to see you again", "glad to see you again", "you're back",
                                     "you came back", "you've come back", "we've met", "we have met", "we met", "met before",
                                     "hello again", "well met again"})
        for (std::size_t at = find_words(text, phrase); at != std::string::npos; at = find_words(text, phrase, at + 1))
            if (!wishes().count(word_before(text, at))) {
                add(out, kMet, phrase);
                break;
            }
    for (std::size_t at = find_words(text, "back again"); at != std::string::npos; at = find_words(text, "back again", at + 1))
        if (const std::string before = word_before(text, at); before != "come" && before != "coming") {
            add(out, kMet, "back again");
            break;
        }
    for (std::size_t at = find_words(text, "welcome back"); at != std::string::npos; at = find_words(text, "welcome back", at + 1)) {
        const std::string before = word_before(text, at);
        const bool invited = before == "are" || before == "be" || before == "is" || before == "always" || before == "ever" ||
                             (before.size() > 3 && before.compare(before.size() - 3, 3, "'re") == 0);
        if (!invited) {
            add(out, kMet, "welcome back");
            break;
        }
    }
    static const std::set<std::string> openers = {"", "oh", "ah", "well", "so", "hm", "hmm", "ha", "hah", "what", "it's", "oh, it's",
                                                  "ah, it's", "well, it's"};
    for (std::size_t at = find_words(text, "you again"); at != std::string::npos; at = find_words(text, "you again", at + 1))
        if (openers.count(lead_of(text, at))) {
            add(out, kMet, "you again");
            break;
        }
    // The same act asked before in this conversation is a last time too.
    if (says(text, "last time") && !m.repeat) add(out, kMet, "last time");
}

// "As I told you": asked twice in this conversation, or something of the packet's own said lately;
// "as before" may point back to a meeting too ("you are as welcome as before"). Only words that
// point back count: "I told you" alone can mean what is being said ("remember I told you"), and
// "exactly as I said it" the words just said.
void told(const std::string& said, const FactPacket& p, const std::vector<std::string>& recent, std::vector<std::string>& out) {
    static const std::vector<std::string> phrases = {"as i told you",   "like i told you",  "as i've told you", "as i said",
                                                     "like i said",     "as i've said",     "told you already", "already told you",
                                                     "told you before", "told you earlier", "said before",      "i did tell you",
                                                     "as before",       "as i mentioned",   "since i told you"};
    std::string claimed;
    for (const std::string& phrase : phrases) {
        for (std::size_t at = find_words(said, phrase); at != std::string::npos; at = find_words(said, phrase, at + 1))
            if (!(phrase == "as i said" && word_after(said, at + phrase.size()) == "it")) {
                claimed = phrase;
                break;
            }
        if (!claimed.empty()) break;
    }
    const Remembered m = remembered(p);
    if (claimed.empty() || m.repeat || (claimed == "as before" && m.again)) return;
    // Told in an earlier conversation, as the speaker remembers (contract version 12, a Recall fact).
    if (recall_of(recalls(p), "told") != nullptr) return;
    std::vector<std::string> words;
    for (const Fact& fact : p.facts)
        for (const std::string key : {"name", "of", "creature"})
            if (const std::string value = lowered(text::spoken(field_of(fact, key))); !value.empty()) words.push_back(value);
    // Work told lately (contract version 13, a work answer said again): another's by its giver's name,
    // the speaker's own by its purpose in their words.
    for (const Fact& fact : p.facts) {
        if (fact.kind != FactKind::QuestOffer) continue;
        if (field_of(fact, "from_speaker") == "yes") {
            if (const std::string purpose = lowered(text::mid_sentence(text::ask_of(text::spoken(field_of(fact, "purpose")))));
                !purpose.empty())
                words.push_back(purpose);
        } else if (const std::string giver = lowered(text::spoken(field_of(fact, "giver"))); !giver.empty()) {
            words.push_back(giver);
        }
    }
    // News told lately, in any of the ways the speaker may have put it (news.hpp).
    for (const Fact& fact : p.facts)
        if (fact.kind == FactKind::News)
            for (const std::string& form : news_forms(fact, text::spoken(p.persona.name), text::spoken(p.persona.place)))
                words.push_back(lowered(form));
    for (const std::string& reply : recent) {
        const std::string earlier = lowered(reply);
        for (const std::string& word : words)
            if (says(earlier, word)) return;
    }
    add(out, kTold, claimed);
}

// What came of the work, against the memory and what the packet says of the work in hand: taken
// or given up now (a QuestState fact), or held by the stranger when they ask the wrong speaker to
// release it. "I'd sooner you finished what you started" wishes; it does not claim.
void work(const std::string& said, const FactPacket& p, std::vector<std::string>& out) {
    static const std::vector<std::pair<std::string, std::string>> phrases = {
        {"you finished", "finished"},     {"the work's done", "finished"},  {"the work is done", "finished"},
        {"you got it done", "finished"},  {"you saw it through", "finished"}, {"you gave up", "abandoned"},
        {"gave it up", "abandoned"},      {"you abandoned", "abandoned"},   {"you failed", "failed"},
        {"you took it on", "taken"},      {"how goes the work", "taken"},   {"how's the work", "taken"}};
    std::set<std::string> held = {remembered(p).work};
    for (const Fact& fact : p.facts) {
        if (fact.kind == FactKind::QuestState) held.insert(field_of(fact, "state"));
        if (fact.kind == FactKind::Reason && field_of(fact, "why") == "not_theirs_to_release") held.insert("taken");
        // News of what came of someone's work at the stranger's hands (contract version 11): "They
        // say you failed Halbrann" was refused as the speaker's own work, so no line could pass on
        // a failure (writers found it, 2026-09-27).
        if (fact.kind == FactKind::News && field_of(fact, "event") == "work" && field_of(fact, "player") == "yes") {
            const std::string state = field_of(fact, "state");
            held.insert(state == "given_up" ? "abandoned" : state);
        }
    }
    for (const auto& [phrase, state] : phrases)
        for (std::size_t at = find_words(said, phrase); at != std::string::npos; at = find_words(said, phrase, at + 1)) {
            if (phrase.rfind("you ", 0) == 0 && wishes().count(word_before(said, at))) continue;
            if (!held.count(state)) add(out, kWork, phrase);
            break;
        }
}

// "Is it safe": words that say the opposite of the verdict.
void verdict(const std::string& said, const FactPacket& p, std::vector<std::string>& out) {
    if (p.answering != ActKind::AskSafety) return;
    const std::string verdict = verdict_of(p);
    std::vector<std::string> against;
    if (verdict == "unsafe")
        against = {"it's safe", "it is safe", "quite safe", "perfectly safe", "nothing to fear", "no danger", "safe enough"};
    else if (verdict == "safe")
        against = {"not safe", "unsafe", "isn't safe", "far from safe", "far from it"};
    else if (verdict == "wary")
        against = {"perfectly safe", "nothing to fear", "completely safe", "not safe at all"};
    for (const std::string& phrase : against)
        if (says(said, phrase)) add(out, kVerdict, phrase);
}

// Small talk's weather, time of day and season, against the packet's Chat fact. Only small talk
// reports them: elsewhere a speaker's rain and storms are figures of speech.
void weather(const std::string& said, const FactPacket& p, std::vector<std::string>& out) {
    const Fact* chat = nullptr;
    for (const Fact& fact : p.facts)
        if (fact.kind == FactKind::Chat) chat = &fact;
    if (chat == nullptr) return;
    const std::string sky = field_of(*chat, "weather"), wind = field_of(*chat, "wind");
    const bool storm = sky == "stormy" || sky == "a great storm" || wind == "a gale";
    const bool wet = storm || sky == "unsettled";
    const bool fair = !storm && (sky == "clear" || sky == "fair");
    for (const std::string word : {"rain", "raining", "rainy", "drizzle", "downpour", "showers"})
        if (says(said, word) && !wet) add(out, kWeather, word);
    for (const std::string word : {"storm", "stormy", "gale", "thunder", "lightning", "tempest"})
        if (says(said, word) && !storm) add(out, kWeather, word);
    for (const std::string word : {"sunshine", "sunny", "clear skies", "clear sky"})
        if (says(said, word) && !fair) add(out, kWeather, word);
    const std::string time = field_of(*chat, "time"), season = field_of(*chat, "season");
    for (const std::string when : {"dawn", "morning", "midday", "afternoon", "evening", "night"})
        for (const std::string lead : {"this ", "good "})
            if (says(said, lead + when) && when != time) add(out, kTime, lead + when);
    const std::vector<std::pair<std::string, bool>> seasons = {
        {"this winter", season.find("winter") != std::string::npos},
        {"this summer", season.find("summer") != std::string::npos},
        {"this spring", season == "the turn of the season"},
        {"this autumn", season == "the turn of the season"}};
    for (const auto& [phrase, holds] : seasons)
        if (says(said, phrase) && !holds) add(out, kTime, phrase);
}

// Knowing of someone is not knowing them: no packet says the speaker knows the people it names,
// so greetings sent or news asked of them are invented (writers drifted into them twice, 2026-09-26).
void acquaintance(const std::string& said, std::vector<std::string>& out) {
    for (const std::string phrase : {"give them my regards", "give them my best", "my regards to", "tell them i said",
                                     "tell them i asked", "say hello from me", "say hello for me", "hello from me",
                                     "ask after them for me", "asked after them", "tell me how they're", "tell me how they are",
                                     "tell them i sent you", "say i sent you"})
        if (says(said, phrase)) add(out, kAcquaintance, phrase);
}

// Talked down, and overheard (contract version 10), against the memory: "you steadied me" only
// where the stranger eased the speaker lately, this reply's calming words among them; "I couldn't
// help overhearing" only from someone who joined in unasked. Hearing things in general claims
// nothing of the stranger ("only what I overhear").
void eased_or_overheard(const std::string& said, const FactPacket& p, std::vector<std::string>& out) {
    const Remembered m = remembered(p);
    if (!m.eased)
        for (const std::string phrase : {"talked me down", "calmed me down", "steadied me", "you steadied", "you calmed me",
                                         "you settled me", "settled my nerves", "calmed my nerves"})
            if (says(said, phrase)) add(out, kEased, phrase);
    if (!m.joined)
        for (const std::string phrase : {"couldn't help overhearing", "could not help overhearing", "couldn't help hearing",
                                         "could not help hearing", "overheard you", "i overheard", "heard what you asked",
                                         "heard you ask", "heard you asking", "heard your question"})
            if (says(said, phrase)) add(out, kOverheard, phrase);
}

// The stranger's own words from before (contract version 12), only where the speaker kept some: words
// that point back past this exchange ("you told me once", "last time you said", "what you told me:").
// "I'm glad you said so" and "you told me straight" are of what the stranger just said: the first
// list read 115 sound rows so (2026-09-27).
void said_before(const std::string& said, const FactPacket& p, std::vector<std::string>& out) {
    if (recall_of(recalls(p), "said") != nullptr) return;
    for (const std::string phrase : {"you told me once", "you once told me", "you told me before", "you told me last time",
                                     "last time you told me", "you said once", "you once said", "you said before", "you said last time",
                                     "last time you said", "you told me:", "you said:", "you once mentioned", "you mentioned once"})
        if (says(said, phrase)) add(out, kSaid, phrase);
}

// News (contract version 11), only with a News fact: words that pass on what is said about only
// where the packet holds news, and words that say what is said of the stranger only where it holds
// news of what they did. "Word is" only opening a sentence: "my word is given" is a promise, not
// news. "You're the one who" is left alone: "you're the one who talked me down" is the memory's.
void heard(const std::string& said, const std::string& reply, const FactPacket& p, std::vector<std::string>& out) {
    bool any = false, theirs = false;
    for (const Fact& fact : p.facts)
        if (fact.kind == FactKind::News) {
            any = true;
            theirs = theirs || field_of(fact, "player") == "yes";
        }
    if (!theirs)
        for (const std::string phrase : {"they say you", "word is you", "word has it you", "word got round that you",
                                         "heard what you did", "heard of what you did", "word of what you did", "said of you",
                                         "talk of you", "talk of what you did", "talk about you"})
            if (says(said, phrase)) add(out, kNews, phrase);
    // No news, from someone who has news to pass on (news_to_pass_on: heard, not their own).
    if (news_to_pass_on(p, false) != nullptr)
        for (const std::string phrase : {"no news", "nothing's reached me", "nothing has reached me", "not a word's reached me",
                                         "not heard a thing", "haven't heard a thing", "heard nothing new"})
            if (says(said, phrase)) add(out, kNoNews, phrase);
    if (any) return;
    for (const std::string phrase : {"word has it", "the word is", "folk are saying", "people are saying", "some are saying",
                                     "rumour has it", "rumor has it", "the talk is", "word got round", "word's gone round",
                                     "word's got round", "there's talk of"})
        if (says(said, phrase)) add(out, kNews, phrase);
    // "I heard that" passes news on only when a clause follows, opening with a name, a pronoun,
    // "the" or "there": "Sorry, I heard that." is overhearing, and "I've heard that excuse before"
    // or "I heard that one" a thing heard (writers found such rows silenced, 2026-09-27).
    static const std::set<std::string> clause = {"you", "he", "she", "they", "we", "it", "someone", "somebody", "nobody",
                                                 "everyone", "there", "the"};
    for (const std::string phrase : {"i heard that", "i've heard that"})
        for (std::size_t at = find_words(said, phrase); at != std::string::npos; at = find_words(said, phrase, at + 1)) {
            std::size_t next = at + phrase.size();
            while (next < said.size() && said[next] == ' ') ++next;
            const bool named = next < reply.size() && std::isupper(static_cast<unsigned char>(reply[next])) != 0;
            if (named || clause.count(word_after(said, at + phrase.size())) != 0) {
                add(out, kNews, phrase);
                break;
            }
        }
    for (std::size_t at = find_words(said, "word is"); at != std::string::npos; at = find_words(said, "word is", at + 1))
        if (lead_of(said, at).empty()) add(out, kNews, "word is");
}

}  // namespace

std::vector<std::string> untrue(std::string_view typed, const FactPacket& packet, const std::vector<std::string>& recent) {
    std::vector<std::string> out;
    // Curly quotes and dashes as plain ones: "Tornar’s" is Tornar's.
    const std::string reply = text::plain_punctuation(typed);
    const std::string said = lowered(reply);
    names(reply, packet, out);
    ranges(said, packet, out);
    dangers(said, packet, out);
    // A News fact's own dated words ("you helped us hold a few days back") say when the news came
    // about, backed by the fact: not when the speaker last met the stranger, and no time counted out.
    // Read as counted, "a few days back" had silenced every dated line of news in that band (the
    // memory session, 2026-09-27).
    std::string undated = said;
    for (const Fact& fact : packet.facts)
        if (fact.kind == FactKind::News) {
            const std::string self = text::spoken(packet.persona.name), here = text::spoken(packet.persona.place);
            for (const std::string& words : {dated_news_words(fact, self, "you", here), dated_deed_words(fact, self, here)})
                if (!words.empty()) blank(undated, lowered(words));
        }
    // "As I told you a few days ago" says when the speaker told it, backed by a Recall fact, not when
    // they last met.
    for (const Recall& r : recalls(packet))
        if (r.memory == "told")
            if (const std::string when = since_words(r.since); !when.empty()) blank(undated, "told you " + when);
    counted(undated, packet, out);
    since(undated, packet, out);
    met(said, packet, out);
    told(said, packet, recent, out);
    work(said, packet, out);
    verdict(said, packet, out);
    weather(said, packet, out);
    acquaintance(said, out);
    eased_or_overheard(said, packet, out);
    heard(said, reply, packet, out);
    said_before(said, packet, out);
    return out;
}

bool never_backed(const std::string& problem) {
    return problem.rfind(std::string(kAcquaintance) + ":", 0) == 0 || problem.rfind(std::string(kCounted) + ":", 0) == 0;
}

}  // namespace communicator
