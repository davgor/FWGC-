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
#include "packets.hpp"
#include "reply_shape.hpp"
#include "rules.hpp"

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
                                       "any | about_place | [{about}][ {trades}][ {who}][ {around}][ {work}]\n";

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
    return situation == "afraid_lead" || situation == "panicked_lead";
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

// Every rule of the voice's that `reply` breaks, as spoken for `p`.
inline std::vector<std::string> broken_rules(const std::string& reply, const FactPacket& p) {
    std::vector<std::string> out;
    if (reply.find_first_of("{}[]") != std::string::npos) out.push_back("a blank or a bracket left in");
    for (const std::string mark : {"\"", "\xE2\x80\x9C", "\xE2\x80\x9D"})
        if (reply.find(mark) != std::string::npos) out.push_back("a double quote");
    std::vector<std::string> ids = {p.persona.uid};
    for (const Fact& f : p.facts)
        if (!f.subject.empty()) ids.push_back(f.subject);
    for (const std::string id_start : {"hook-", "hero-", "npc-", "keyloc-", "surface-city-", "nest-"}) ids.push_back(id_start);
    for (const std::string& id : ids)
        if (reply.find(id) != std::string::npos) out.push_back("an id: " + id);

    const communicator::ReplyShape shape = communicator::reply_shape(p);
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
        // "The turn of the season" is a season: "this {season}" would say "this the turn of the
        // season". Every other article before a blank is caught in the replies, where it misfits.
        for (const std::string before : {"this {season}", "This {season}", "that {season}", "That {season}"})
            if (line.find(before) != std::string::npos) out.push_back("\"" + before + "\": the turn of the season is a season");
    }
    try {
        // A row for an answer said again is spoken to a stranger who asked before and heard all of
        // the answer lately, beside an answer that says it all (what is heard is what the answer
        // says); any other, alone, as the packet comes.
        const bool again = told_row(row);
        const bool leading = lead_row(row);
        const std::string beside = again ? "\n" + kEveryBlank : std::string();
        const communicator::Lines one = communicator::Lines::parse(row + beside, "the row");
        const auto replies_of = [&](const communicator::Lines& lines, const FactPacket& packet, const FactPacket& p) {
            // A frightened speaker's lead is said alone here, before whatever answer comes after.
            if (leading) return lines.leads(p);
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
                for (const std::string& broken : broken_rules(reply, p)) out.push_back(broken + ", in: " + reply);
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
                const std::vector<std::string> recent = again ? heard_everything(packet) : std::vector<std::string>();
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
                    for (const std::string& broken : broken_rules(reply, p)) out.push_back(broken + ", in: " + reply);
            }
        }
    } catch (const std::exception& e) {
        out.push_back(std::string("refused: ") + e.what());
    }
    return out;
}

}  // namespace authored_cases
