// The written lines' rules as a check over rows, shared by the tests and by `communicator_eval
// --check-lines`, which a writer runs on their own rows. Each row is tried alone against every
// packet here, as its bank's speaker, and every reply it gives is held to the voice's rules.
#pragma once

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "communicator/authored.hpp"
#include "communicator/truth.hpp"
#include "news.hpp"
#include "packets.hpp"
#include "reply_shape.hpp"
#include "rules.hpp"
#include "text.hpp"

namespace authored_cases {

using orchestrator::ActKind;
using orchestrator::Fact;
using orchestrator::FactKind;
using orchestrator::FactPacket;
using orchestrator::Outcome;

inline Fact halbrann() {
    return {FactKind::Person, "hero-pretender-surface-city-0-243-human_cold",
            {{"name", "Halbrann"}, {"role", "pretender"}, {"where", "the ruins of Linbrig"}, {"status", "about"}}};
}

inline Fact reason(const std::string& why, const std::string& text) {
    return {FactKind::Reason, "", {{"reason", text}, {"why", why}}};
}

// The situations packets.hpp has no packet for, built from its facts.
inline FactPacket about_halbrann() {
    return packets::packet(packets::castellan(), ActKind::AskAboutPerson, Outcome::Answered, {halbrann()});
}

inline FactPacket about_the_works() {
    return packets::packet(packets::castellan(), ActKind::AskAboutPlace, Outcome::Answered,
                           {{FactKind::Place, "keyloc-smelter_ruin-180",
                             {{"kind", "smelter_ruin"}, {"name", "the burnt works"}, {"family", "extractive"}, {"state", "ruined"},
                              {"threat", "2"}, {"occupant", "squatters"}, {"range", "walk"}}}});
}

inline FactPacket no_work() { return packets::packet(packets::castellan(), ActKind::AskForWork, Outcome::Answered); }

inline FactPacket refused_release() {
    return packets::packet(packets::castellan(), ActKind::AbandonQuest, Outcome::Refused,
                           {reason("not_theirs_to_release", "the work is Halbrann's to release"), halbrann()});
}

inline FactPacket refused_with_no_giver() {
    return packets::packet(packets::castellan(), ActKind::AcceptQuest, Outcome::Refused,
                           {reason("not_theirs_to_give", "the work is someone else's to give")});
}

inline FactPacket refused_gone() {
    return packets::packet(packets::castellan(), ActKind::NoAct, Outcome::Refused, {reason("gone", "Selbren is gone")});
}

// Refused by the world, in the director's words for a lapsed offer.
inline FactPacket refused_by_the_world() {
    return packets::packet(packets::castellan(), ActKind::AcceptQuest, Outcome::Refused,
                           {reason("refused_by_world", "the offer has lapsed")});
}

// Every packet the rows are tried against.
inline std::vector<FactPacket> cases() {
    std::vector<FactPacket> out = packets::all();
    for (FactPacket p : {about_halbrann(), about_the_works(), no_work(), refused_release(), refused_with_no_giver(), refused_gone(),
                         refused_by_the_world()})
        out.push_back(std::move(p));
    for (FactPacket p : packets::conversation()) out.push_back(std::move(p));
    return out;
}

// Whether a packet's situations include `situation`: a row written for it may speak the packet.
// An answer said again as told ("surroundings_told") is reached by the packets its answer is.
inline bool reaches(const FactPacket& p, const std::string& situation) {
    static const std::string told = "_told";
    const bool retold = situation.size() > told.size() && situation.compare(situation.size() - told.size(), told.size(), told) == 0;
    const std::string answer = retold ? situation.substr(0, situation.size() - told.size()) : situation;
    const std::vector<std::string> chain = communicator::Lines::situations_for(p);
    return std::find(chain.begin(), chain.end(), answer) != chain.end();
}

// `p` asked before: its Memory fact's `heard` at least one, as an answer said again as told needs.
inline FactPacket asked_before(FactPacket p) {
    for (orchestrator::Fact& fact : p.facts)
        if (fact.kind == FactKind::Memory)
            for (auto& [key, value] : fact.fields)
                if (key == "heard" && value == "0") value = "1";
    return p;
}

// What a speaker would have said lately for all of an answer to `p` to have been heard: every
// blank's words, from lines that say each blank there is. Empty for a packet no answer is heard of.
// Answers that say every blank there is, one for each answer a told row says again.
inline const std::string kEveryBlank = "any | surroundings | [{here}][ {nearest}][ {others}][ {danger}]\n"
                                       "any | safety_unsafe | [{worst}][ {others}][ {danger}]\n"
                                       "any | safety_wary | [{worst}][ {others}][ {danger}]\n"
                                       "any | safety_safe | [{worst}][ {others}][ {danger}]\n"
                                       "any | about_place | [{about}][ {trades}][ {who}][ {around}][ {work}]\n"
                                       "any | work_own | [{own}][ {others}]\n"
                                       "any | work_others | [{others}]\n";

inline std::vector<std::string> heard_everything(const FactPacket& p) {
    static const communicator::Lines everything = communicator::Lines::parse(kEveryBlank, "every blank");
    std::vector<std::string> out;
    for (std::string& reply : everything.replies(p))
        if (!reply.empty()) out.push_back(std::move(reply));
    return out;
}

// A row of a lines file: where it stands, and its text.
struct Row {
    std::string where;  // file:line
    std::string text;
};

// The files a lines path names: the file itself, or every `.txt` in a folder, in name order.
inline std::vector<std::filesystem::path> files_of(const std::string& path) {
    std::vector<std::filesystem::path> out;
    if (!std::filesystem::is_directory(path)) return {std::filesystem::path(path)};
    for (const auto& entry : std::filesystem::directory_iterator(path))
        if (entry.is_regular_file() && entry.path().extension() == ".txt") out.push_back(entry.path());
    std::sort(out.begin(), out.end());
    return out;
}

// Every row that is not blank or a comment, as a reader apart from Lines finds them. Empty when
// the path cannot be read.
inline std::vector<Row> rows_of(const std::string& path) {
    std::vector<Row> out;
    for (const std::filesystem::path& file : files_of(path)) {
        std::ifstream in(file, std::ios::binary);
        std::string row;
        int number = 0;
        while (std::getline(in, row)) {
            ++number;
            if (number == 1 && row.rfind("\xEF\xBB\xBF", 0) == 0) row.erase(0, 3);
            const std::size_t first = row.find_first_not_of(" \t\r");
            if (first == std::string::npos || row[first] == '#') continue;
            out.push_back({file.filename().string() + ":" + std::to_string(number), row});
        }
    }
    return out;
}

inline std::string voice_of(const std::string& row) {
    std::string voice = row.substr(0, row.find('|'));
    while (!voice.empty() && voice.back() == ' ') voice.pop_back();
    return voice;
}

// Whether a row is written for an answer said again ("surroundings_told").
inline bool told_row(const std::string& row) {
    const std::size_t first = row.find('|');
    const std::size_t second = first == std::string::npos ? first : row.find('|', first + 1);
    if (second == std::string::npos) return false;
    std::string situation = row.substr(first + 1, second - first - 1);
    while (!situation.empty() && situation.back() == ' ') situation.pop_back();
    static const std::string told = "_told";
    return situation.size() > told.size() && situation.compare(situation.size() - told.size(), told.size(), told) == 0;
}

// Whether a row is a frightened speaker's lead before an answer ("afraid_lead"): said alone,
// before whatever answer comes after (Lines::lead).
inline bool lead_row(const std::string& row) {
    const std::size_t first = row.find('|');
    const std::size_t second = first == std::string::npos ? first : row.find('|', first + 1);
    if (second == std::string::npos) return false;
    std::string situation = row.substr(first + 1, second - first - 1);
    situation.erase(0, situation.find_first_not_of(' '));
    while (!situation.empty() && situation.back() == ' ') situation.pop_back();
    return situation == "afraid_lead" || situation == "panicked_lead" || situation == "joined_lead";
}

// Whether a row is said after a reply (Lines::follow): news ("heard_deed", "news"), or the stranger's
// own words said back after a greeting ("recalled"); said alone, after whatever reply comes before it.
inline bool follow_row(const std::string& row) {
    const std::size_t first = row.find('|');
    const std::size_t second = first == std::string::npos ? first : row.find('|', first + 1);
    if (second == std::string::npos) return false;
    std::string situation = row.substr(first + 1, second - first - 1);
    situation.erase(0, situation.find_first_not_of(' '));
    while (!situation.empty() && situation.back() == ' ') situation.pop_back();
    return situation == "heard_deed" || situation == "news" || situation == "recalled" || situation == "greeted_who";
}

// The situation a row is written for, trimmed; empty for a row without one.
inline std::string situation_of(const std::string& row) {
    const std::size_t first = row.find('|');
    const std::size_t second = first == std::string::npos ? first : row.find('|', first + 1);
    if (second == std::string::npos) return {};
    std::string situation = row.substr(first + 1, second - first - 1);
    situation.erase(0, situation.find_first_not_of(' '));
    while (!situation.empty() && situation.back() == ' ') situation.pop_back();
    return situation;
}

// Whether a row answers small talk that asks for news with news told lately or none ("news_told",
// "heard_deed_told", "no_news"; Lines::news_asked).
inline bool asked_news_row(const std::string& row) {
    const std::string situation = situation_of(row);
    return situation == "news_told" || situation == "heard_deed_told" || situation == "no_news";
}

// What a speaker said lately for the news `p` would pass on to have been told: its words, as a
// `news` line says them. Empty for a packet with none to pass on.
inline std::vector<std::string> news_told(const FactPacket& p) {
    const Fact* news = communicator::news_to_pass_on(p, false);
    if (news == nullptr) return {};
    namespace text = communicator::text;
    return {"The word is " + communicator::news_words(*news, text::spoken(p.persona.name), "you", text::spoken(p.persona.place)) + "."};
}

// What is wrong with where a news row puts its blanks (contract version 11): `{news}` may open with
// "you" or a name, so it never opens a sentence; `{deed}` is what the stranger did, so it follows
// "you", "who" or "you that", and nothing between ("you would {deed}" says "you would helped").
// Writers found both let through (2026-09-27).
inline std::vector<std::string> news_blank_problems(const std::string& line) {
    std::vector<std::string> out;
    const auto before = [&](std::size_t at) {
        std::string text = line.substr(0, at);
        while (!text.empty() && (text.back() == ' ' || text.back() == '[')) text.pop_back();
        return text;
    };
    const auto last_word = [](std::string text) {
        while (!text.empty() && !std::isalpha(static_cast<unsigned char>(text.back())) && text.back() != '\'') text.pop_back();
        const std::size_t space = text.find_last_of(" ,;:");
        std::string word = space == std::string::npos ? text : text.substr(space + 1);
        for (char& c : word) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return word;
    };
    for (std::size_t at = line.find("{news}"); at != std::string::npos; at = line.find("{news}", at + 1)) {
        const std::string lead = before(at);
        if (lead.empty() || lead.back() == '.' || lead.back() == '!' || lead.back() == '?')
            out.push_back("{news} opens a sentence: it may begin with \"you\" or a name");
        // "They say you {news}." says "They say you you beat off the threat"; "I told you: {news}" is
        // two parts, and fine.
        else if (std::isalpha(static_cast<unsigned char>(lead.back())) && last_word(lead) == "you")
            out.push_back("{news} straight after \"you\": it may begin with \"you\"");
    }
    for (std::size_t at = line.find("{deed}"); at != std::string::npos; at = line.find("{deed}", at + 1)) {
        const std::string lead = before(at);
        const std::string word = last_word(lead);
        const std::size_t cut = lead.find_last_of(' ');
        const std::string earlier = cut == std::string::npos ? std::string() : last_word(lead.substr(0, cut));
        if (!(word == "you" || word == "who" || (word == "that" && earlier == "you")))
            out.push_back("{deed} not straight after \"you\", \"who\" or \"you that\": it is what the stranger did");
    }
    return out;
}

// Whether `lead`, in lower case, says that the words after it are the stranger's, told by them: "you"
// with a word of telling after it ("you told me", "what you said", "you let me know"), or "your
// words", "yours", "from you", "by you", "heard you", "listened to you". Said after "If I may:" or
// "I kept this:", "you'd be back before the frost" is heard as the speaker's own assurance, and a
// fragment ("to the coast, by the high road") as nonsense; after "what I'm told" or "what's said to
// me", as word that reached the speaker about the stranger; after "keeping what you're told", as the
// speaker telling them (refuters, 2026-09-27).
inline bool marks_them_as_told(const std::string& lead) {
    static const std::vector<std::string> telling = {"told", "tell",      "telling", "said",    "saying", "spoke",
                                                     "mentioned", "informed", "stated", "passed", "shared", "gave",
                                                     "left", "let"};
    std::vector<std::string> words;
    std::string word;
    for (const char c : lead + " ") {
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '\'') {
            word += c;
        } else if (!word.empty()) {
            words.push_back(word);
            word.clear();
        }
    }
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (words[i] == "yours") return true;
        // "you" as the one who told: not "you are told", nor "you learn" as anyone does.
        if (words[i] != "you" || (i + 1 < words.size() && (words[i + 1] == "are" || words[i + 1] == "learn"))) continue;
        for (std::size_t j = i + 1; j < words.size(); ++j)
            if (std::find(telling.begin(), telling.end(), words[j]) != telling.end()) return true;
    }
    for (const std::string phrase : {"your words", "your own words", "your telling", "from you", "by you", "heard you", "listened to you",
                                     "listen to you"})
        if (lead.find(phrase) != std::string::npos) return true;
    return false;
}

// What is wrong with a `recalled` row (contract version 12): `{words}` may be a fragment ("to the
// coast, by the high road"), so only a colon brings it in, never "that" or a sentence of its own; and
// the line never says when the words were said, which may have been several visits back, and never
// asks after them ("Is she well, the one you told me of: ..."), which would need what the words do
// not hold. Writers found the checker let all of it through (2026-09-27). And before the words begin,
// the line says they are the stranger's (`marks_them_as_told`).
inline std::vector<std::string> recalled_problems(const std::string& line) {
    std::vector<std::string> out;
    if (line.find('?') != std::string::npos) out.push_back("asks after the words: a recalled line is never a question");
    for (std::size_t at = line.find("{words}"); at != std::string::npos; at = line.find("{words}", at + 1)) {
        std::string lead = line.substr(0, at);
        while (!lead.empty() && (lead.back() == ' ' || lead.back() == '[')) lead.pop_back();
        if (lead.empty() || lead.back() != ':') out.push_back("{words} not after a colon: the words may be a fragment");
    }
    std::string lower = line;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const std::string when : {"last time", "the other day", "last we", "yesterday", "since we", "you always", "earlier today",
                                   "days ago", "weeks ago", "months ago", "years ago"})
        if (lower.find(when) != std::string::npos) out.push_back("says when, which the words may not have been: \"" + when + "\"");
    if (const std::size_t at = lower.find("{words}"); at != std::string::npos && !marks_them_as_told(lower.substr(0, at)))
        out.push_back("the words are not the stranger's before they begin: say they told it (\"what you told me\")");
    return out;
}

// `p` spoken by someone the row's bank answers for: a hero with that voice key, someone at that
// post, or, for "any", the packet's own speaker.
inline FactPacket as_voiced_by(FactPacket p, const std::string& voice) {
    if (voice.rfind("post:", 0) == 0) {
        p.persona.voice_key.clear();
        p.persona.role = voice.substr(5);
    } else if (voice != "any") {
        p.persona.voice_key = voice;
    }
    return p;
}

// Every rule of the voice's that `reply` breaks, as spoken for `p`. `shaped`: whether the reply is
// the packet's own, held to its outcome's opening, closing and length; news said after it is not.
inline std::vector<std::string> broken_rules(const std::string& reply, const FactPacket& p, bool shaped = true) {
    std::vector<std::string> out;
    // Round brackets too: nobody says one (a writer found them let through, 2026-09-27).
    if (reply.find_first_of("{}[]()") != std::string::npos) out.push_back("a blank or a bracket left in");
    for (const std::string mark : {"\"", "\xE2\x80\x9C", "\xE2\x80\x9D"})
        if (reply.find(mark) != std::string::npos) out.push_back("a double quote");
    std::vector<std::string> ids = {p.persona.uid};
    for (const Fact& f : p.facts)
        if (!f.subject.empty()) ids.push_back(f.subject);
    for (const std::string id_start : {"hook-", "hero-", "npc-", "keyloc-", "surface-city-", "nest-"}) ids.push_back(id_start);
    for (const std::string& id : ids)
        if (reply.find(id) != std::string::npos) out.push_back("an id: " + id);

    communicator::ReplyShape shape = communicator::reply_shape(p);
    if (!shaped) {
        shape.openings.clear();
        shape.closing.clear();
        shape.closing_after_name.clear();
        shape.sentences = 1;
    }
    std::string own = reply;
    if (!shape.closing.empty()) {
        // The closing C++ writes, or, after a line that named the giver, where they are without the
        // name again; or nothing more when the packet says nowhere.
        const auto ends_with = [&](const std::string& end) {
            return !end.empty() && own.size() >= end.size() + 1 && own.compare(own.size() - end.size() - 1, end.size() + 1, " " + end) == 0;
        };
        std::string giver;
        for (const Fact& f : p.facts)
            if (f.kind == FactKind::Person && f.field("name")) giver = *f.field("name");
        const std::size_t cut = giver.find(" (");
        if (cut != std::string::npos) giver.resize(cut);
        const bool named = !giver.empty() && own.find(giver) != std::string::npos;
        if (ends_with(shape.closing))
            own.resize(own.size() - shape.closing.size() - 1);
        else if (named && ends_with(shape.closing_after_name))
            own.resize(own.size() - shape.closing_after_name.size() - 1);
        else if (!(named && shape.closing_after_name.empty()))
            out.push_back("not the closing: " + shape.closing);
    }
    std::string past_opening = own;
    if (!shape.openings.empty()) {
        // Whole words: "No" does not open "Nothing to worry about" (rules::opens_with_one_of).
        const auto opening = std::find_if(shape.openings.begin(), shape.openings.end(),
                                          [&](const std::string& o) { return rules::opens_with_one_of(own, {o}); });
        if (opening == shape.openings.end())
            out.push_back("not an opening of its outcome's");
        else
            past_opening = own.substr(opening->size());  // C++'s own words: "Suit yourself"
    }
    const std::size_t sentences = rules::sentences_in(own);
    if (sentences < 1 || sentences > static_cast<std::size_t>(shape.sentences))
        out.push_back(std::to_string(sentences) + " sentences, of at most " + std::to_string(shape.sentences));
    for (const std::string& name : rules::unlisted_names(past_opening, p)) out.push_back("a capital no packet gave: " + name);
    for (const std::string& stray : rules::stray_capitals(reply, p)) out.push_back("a common capital mid-sentence: " + stray);
    for (const std::string& barred : rules::barred_words(reply, p)) out.push_back("a barred word: " + barred);
    for (const std::string& misfit : rules::misfit_articles(reply)) out.push_back("an article that does not fit: " + misfit);
    return out;
}

// The capitals a line writes itself, outside its blanks: a name the writer typed ("Wenweg") that a
// test packet happens to hold passes the check on replies, and names the wrong town in any other
// world. Each blank stands in as a lower-case word, so only the writer's own capitals are left:
// a sentence may begin with a common capital, "I" may stand anywhere, and the line may open with
// its outcome's opening ("Suit yourself"), which is C++'s.
inline std::vector<std::string> own_capitals(std::string line, const std::vector<std::string>& openings) {
    for (const std::string& opening : openings)
        if (line.rfind(opening, 0) == 0) {
            line = "x" + line.substr(opening.size());
            break;
        }
    std::string bare;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '{') {
            const std::size_t close = line.find('}', i);
            if (close == std::string::npos) break;
            bare += "x";
            i = close;
        } else if (line[i] != '[' && line[i] != ']') {
            bare += line[i];
        }
    }
    const FactPacket nothing;  // no packet words: every capital must be the line's to write
    std::vector<std::string> out = rules::unlisted_names(bare, nothing);
    for (const std::string& stray : rules::stray_capitals(bare, nothing)) out.push_back(stray);
    return out;
}

// What is wrong with a people's saying: it goes into a line mid-sentence ("as we say, {saying}.")
// or starts one, so it opens in lower case and has no stop of its own; and, as a sentence, it keeps
// the voice's rules: no capital a line may not write, no barred word, no quote, no bracket.
inline std::vector<std::string> saying_problems(const std::string& voice, const std::string& saying) {
    std::vector<std::string> out;
    if (voice.rfind("saying:", 0) != 0) out.push_back("a saying in a bank that is not a people's (saying:<people>)");
    if (saying.empty() || !(saying[0] >= 'a' && saying[0] <= 'z'))
        out.push_back("a saying that does not open in lower case, to go mid-sentence");
    if (saying.find_first_of(".!?") != std::string::npos) out.push_back("a stop in a saying: the line ends it");
    if (saying.find_first_of("{}[]\"") != std::string::npos) out.push_back("a blank, a bracket or a quote in a saying");
    std::string sentence = saying;
    if (!sentence.empty()) sentence[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(sentence[0])));
    sentence += ".";
    const FactPacket nothing;
    for (const std::string& name : rules::unlisted_names(sentence, nothing)) out.push_back("a capital no packet gave: " + name);
    for (const std::string& stray : rules::stray_capitals(sentence, nothing)) out.push_back("a common capital mid-sentence: " + stray);
    for (const std::string& barred : rules::barred_words(sentence, nothing)) out.push_back("a barred word: " + barred);
    return out;
}

// A saying of the kind a people's bank holds, dropped into every row that says one.
inline const std::string kTestSaying = "the well remembers who dug it";

// What is wrong with one row, tried alone against every case as its bank's speaker: a row the
// loader refuses, a row no packet speaks, a capital the line writes itself, each reply that
// breaks a rule, quoted with the rule, and each claim no packet could back (communicator/truth.hpp).
// Empty when the row is sound. With `claims`, the claims a case's packet does not back are added
// to it: they keep the line unsaid for such a packet, and are not the row's fault.
// The first person written in lower case ("i", "i'd", "i'm"...): "And i'd keep off {worst}" stood in
// four banks, which no check read (a writer, 2026-09-28).
inline std::vector<std::string> lowercase_i(const std::string& line) {
    std::vector<std::string> out;
    std::string word;
    for (const char c : line + " ") {
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '\'') {
            word += c;
        } else {
            if (word == "i" || word == "i'd" || word == "i'm" || word == "i've" || word == "i'll") out.push_back(word);
            word.clear();
        }
    }
    return out;
}

inline std::vector<std::string> row_problems(const std::string& row, std::vector<std::string>* claims = nullptr) {
    std::vector<std::string> out;
    const std::size_t first_bar = row.find('|');
    const std::size_t second_bar = first_bar == std::string::npos ? first_bar : row.find('|', first_bar + 1);
    if (second_bar != std::string::npos) {
        std::string situation = row.substr(first_bar + 1, second_bar - first_bar - 1);
        situation.erase(0, situation.find_first_not_of(' '));
        situation.erase(situation.find_last_not_of(' ') + 1);
        const std::string voice = voice_of(row);
        if (situation == "saying" || voice.rfind("saying:", 0) == 0) {
            std::string saying = row.substr(second_bar + 1);
            saying.erase(0, saying.find_first_not_of(" \t"));
            while (!saying.empty() && (saying.back() == '\r' || saying.back() == ' ')) saying.pop_back();
            if (situation != "saying") out.push_back("a people's bank holds only sayings");
            for (const std::string& problem : saying_problems(voice, saying)) out.push_back(problem);
            try {
                (void)communicator::Lines::parse(row, "the row");
            } catch (const std::exception& e) {
                out.push_back(std::string("refused: ") + e.what());
            }
            return out;
        }
    }
    if (second_bar != std::string::npos) {
        // The row's situation, and the openings a case in that situation must use.
        std::string situation = row.substr(first_bar + 1, second_bar - first_bar - 1);
        situation.erase(0, situation.find_first_not_of(' '));
        situation.erase(situation.find_last_not_of(' ') + 1);
        std::vector<std::string> openings;
        for (const FactPacket& p : cases())
            if (reaches(p, situation)) {
                openings = communicator::reply_shape(p).openings;
                break;
            }
        std::string line = row.substr(second_bar + 1);
        line.erase(0, line.find_first_not_of(" \t"));
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        for (const std::string& capital : own_capitals(line, openings))
            out.push_back("a capital the line writes itself: " + capital);
        for (const std::string& problem : news_blank_problems(line)) out.push_back(problem);
        if (situation == "recalled")
            for (const std::string& problem : recalled_problems(line)) out.push_back(problem);
        // "The turn of the season" is a season: "this {season}" would say "this the turn of the
        // season". Every other article before a blank is caught in the replies, where it misfits.
        for (const std::string before : {"this {season}", "This {season}", "that {season}", "That {season}"})
            if (line.find(before) != std::string::npos) out.push_back("\"" + before + "\": the turn of the season is a season");
        for (const std::string& word : lowercase_i(line)) out.push_back("\"" + word + "\": I is written as a capital");
    }
    try {
        // A row for an answer said again is spoken to a stranger who asked before and heard all of
        // the answer lately, beside an answer that says it all (what is heard is what the answer
        // says); any other, alone, as the packet comes.
        // News told lately ("news_told", "heard_deed_told") is small talk, not an answer said again:
        // it has its own stranger to say it to.
        const bool asked_news = asked_news_row(row);
        const bool news_again = situation_of(row) == "news_told" || situation_of(row) == "heard_deed_told";
        const bool again = told_row(row) && !news_again;
        const bool leading = lead_row(row);
        const bool following = follow_row(row);
        const std::string beside = again ? "\n" + kEveryBlank : std::string();
        const communicator::Lines one = communicator::Lines::parse(row + beside, "the row");
        const auto replies_of = [&](const communicator::Lines& lines, const FactPacket& packet, const FactPacket& p) {
            // A frightened speaker's lead is said alone here, before whatever answer comes after; news,
            // alone, after it; and news asked for in small talk, to a stranger told it lately or not.
            if (leading) return lines.leads(p);
            if (following) return lines.follows(p);
            if (asked_news) return lines.news_asked(p, news_again ? news_told(packet) : std::vector<std::string>());
            if (!again) return lines.replies(p);
            const std::vector<std::string> heard = heard_everything(packet);
            if (heard.empty()) return std::vector<std::string>();
            // The row's, said as told; not the answer beside it, said when the row is not.
            static const communicator::Lines everything = communicator::Lines::parse(kEveryBlank, "every blank");
            std::vector<std::string> said = lines.replies(asked_before(p), heard);
            return said == everything.replies(asked_before(p), heard) ? std::vector<std::string>() : said;
        };
        std::size_t spoken = 0;
        for (const FactPacket& packet : cases()) {
            const FactPacket p = as_voiced_by(packet, voice_of(row));
            for (const std::string& reply : replies_of(one, packet, p)) {
                ++spoken;
                // News said after a reply is one sentence of its own, held to every rule but the
                // reply's shape.
                for (const std::string& broken : broken_rules(reply, p, !following)) out.push_back(broken + ", in: " + reply);
                // A lead goes before an answer the program adds: one sentence, and no question.
                if (leading && (rules::sentences_in(reply) != 1 || reply.back() == '?'))
                    out.push_back("a lead is one sentence, not a question, in: " + reply);
                // Nor a yes or a no: before "is it safe", whose answer opens with its verdict, "Yes,
                // yes, quickly! No, ..." says both (a writer found it, 2026-09-26).
                if (leading && rules::opens_with_one_of(reply, {"Yes", "No", "Aye", "Nay"}))
                    out.push_back("a lead opens with no yes or no, in: " + reply);
                // Nor ends on one: "Let me think, yes." before "No. ..." says both too.
                if (leading) {
                    std::string last = reply;
                    while (!last.empty() && !std::isalpha(static_cast<unsigned char>(last.back()))) last.pop_back();
                    const std::size_t space = last.find_last_of(" ,;:");
                    last = rules::lowered(space == std::string::npos ? last : last.substr(space + 1));
                    if (last == "yes" || last == "no" || last == "aye" || last == "nay")
                        out.push_back("a lead ends on no yes or no, in: " + reply);
                }
                // What the reply claims (communicator/truth.hpp): a claim no packet can back is the
                // row's fault; one this packet does not back only keeps the line unsaid here.
                const std::vector<std::string> recent =
                    again ? heard_everything(packet) : news_again ? news_told(packet) : std::vector<std::string>();
                for (const std::string& claim : communicator::untrue(reply, again ? asked_before(p) : p, recent)) {
                    if (communicator::never_backed(claim))
                        out.push_back(claim + ", in: " + reply);
                    else if (claims != nullptr)
                        claims->push_back(claim + ", in: " + reply);
                }
            }
        }
        if (spoken == 0) out.push_back("no test packet speaks it");
        // A saying ends its sentence: it has no stop of its own, and words after it read as its own.
        for (std::size_t at = row.find("{saying}"); at != std::string::npos; at = row.find("{saying}", at + 1)) {
            std::size_t next = at + std::string("{saying}").size();
            while (next < row.size() && row[next] == ']') ++next;
            if (next >= row.size() || std::string(".!?").find(row[next]) == std::string::npos)
                out.push_back("a saying ends its sentence, its stop straight after it");
        }
        // A row that says a saying, said once more by a speaker whose people has one: the test
        // packets' speakers have none, so without this the saying is always left out.
        if (row.find("{saying}") != std::string::npos) {
            const communicator::Lines with = communicator::Lines::parse(row + beside + "\nsaying:test | saying | " + kTestSaying, "the row");
            for (FactPacket packet : cases()) {
                packet.persona.people = "test";
                const FactPacket p = as_voiced_by(packet, voice_of(row));
                for (const std::string& reply : replies_of(with, packet, p))
                    for (const std::string& broken : broken_rules(reply, p, !following)) out.push_back(broken + ", in: " + reply);
            }
        }
    } catch (const std::exception& e) {
        out.push_back(std::string("refused: ") + e.what());
    }
    return out;
}

}  // namespace authored_cases
