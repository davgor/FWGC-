// The written lines' rules as a check over rows, shared by the tests and by `communicator_eval
// --check-lines`, which a writer runs on their own rows. Each row is tried alone against every
// packet here, as its bank's speaker, and every reply it gives is held to the voice's rules.
#pragma once

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "communicator/authored.hpp"
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
                              {"threat", "2"}, {"occupant", "squatters"}, {"distance", "3.1 km"}}}});
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
        const std::string closing = " " + shape.closing;
        if (own.size() < closing.size() || own.compare(own.size() - closing.size(), closing.size(), closing) != 0)
            out.push_back("not the closing: " + shape.closing);
        else
            own.resize(own.size() - closing.size());
    }
    std::string past_opening = own;
    if (!shape.openings.empty()) {
        const auto opening = std::find_if(shape.openings.begin(), shape.openings.end(),
                                          [&](const std::string& o) { return own.rfind(o, 0) == 0; });
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

// What is wrong with one row, tried alone against every case as its bank's speaker: a row the
// loader refuses, a row no packet speaks, a capital the line writes itself, or each reply that
// breaks a rule, quoted with the rule. Empty when the row is sound.
inline std::vector<std::string> row_problems(const std::string& row) {
    std::vector<std::string> out;
    const std::size_t first_bar = row.find('|');
    const std::size_t second_bar = first_bar == std::string::npos ? first_bar : row.find('|', first_bar + 1);
    if (second_bar != std::string::npos) {
        // The row's situation, and the openings a case in that situation must use.
        std::string situation = row.substr(first_bar + 1, second_bar - first_bar - 1);
        situation.erase(0, situation.find_first_not_of(' '));
        situation.erase(situation.find_last_not_of(' ') + 1);
        std::vector<std::string> openings;
        for (const FactPacket& p : cases())
            if (communicator::Lines::situation(p) == situation) {
                openings = communicator::reply_shape(p).openings;
                break;
            }
        std::string line = row.substr(second_bar + 1);
        line.erase(0, line.find_first_not_of(" \t"));
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        for (const std::string& capital : own_capitals(line, openings))
            out.push_back("a capital the line writes itself: " + capital);
    }
    try {
        const communicator::Lines one = communicator::Lines::parse(row, "the row");
        std::size_t spoken = 0;
        for (const FactPacket& packet : cases()) {
            const FactPacket p = as_voiced_by(packet, voice_of(row));
            for (const std::string& reply : one.replies(p)) {
                ++spoken;
                for (const std::string& broken : broken_rules(reply, p)) out.push_back(broken + ", in: " + reply);
            }
        }
        if (spoken == 0) out.push_back("no test packet speaks it");
    } catch (const std::exception& e) {
        out.push_back(std::string("refused: ") + e.what());
    }
    return out;
}

}  // namespace authored_cases
