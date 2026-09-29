// communicator_eval: the interpreter and the voice measured against labelled cases, on a model.
//
//   communicator_eval [--model PATH] [--lines PATH] [--voice PATH] [--authored PATH] [--voice-key KEY]
//                     [--post POST] [--limit N] [--threads N] [--gpu-layers N]
//   communicator_eval --check-lines PATH
//   communicator_eval --session PACKETS [--authored PATH] [--meaning PATH] [--voice-model PATH] [--gpu-layers N]
//   communicator_eval --summarize LOGS [--model PATH] [--threads N] [--gpu-layers N]
//
// --summarize reads conversation logs a person would remember something of (summarize.txt in this
// folder: who remembers, each line, and the runs of the stranger's words a person would keep), has
// the model summarize each (communicator/summarizer.hpp), and prints what it kept beside what the
// director's rules keep without it (the stranger's answers, whole): how many of the runs worth
// keeping each finds, and how many quotes of neither.
//
// --session speaks a session the orchestrator's session tool played (orchestrator_session writes
// its packets as text) with written lines, and prints it as a transcript a person or a judge can
// read: each player line, the reply, and a note of what the speaker remembered. It loads no model,
// unless --meaning names an embedding model (Qwen3-Embedding) to choose each line by the player's
// words (communicator/meaning.hpp); then it also says how long choosing took.
//
// A measuring tool, not a test: it passes or fails nothing. For the interpreter it prints how
// often a line is read as its label says, what was mistaken for what, and every unsafe read: an
// act that would change the world which the player did not mean. For the voice it checks each
// reply's shape (its opening, its length, its closing, its names) and prints every reply, for a
// reader to judge what no check can: whether it claims anything false. With --authored, the voice's
// replies are written lines (communicator/authored.hpp) from that file instead of the model's, held
// to the same checks; the model is still loaded, for the interpreter and for the card words a
// prompt gives. --voice-key speaks every case whose speaker has a voice key as that key instead
// (a bank of written lines, say "conqueror/LE"). --post speaks those cases as a roster person of
// that post instead: no voice key, and the post as what they do (the bank "post:smith", say).
//
// --check-lines holds every row of a lines file or folder to the voice's rules, as the tests do
// (tests/authored_cases.hpp), and prints each row that breaks one. It loads no model: a writer
// runs it on their own rows. It exits 1 when any row breaks a rule.
//
// lines.txt and voice.txt (README.md in this folder) hold the cases. The contexts they name are
// what a player has been told, built from the packets the tests use.
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "communicator/authored.hpp"
#include "communicator/interpreter.hpp"
#include "communicator/known.hpp"
#include "communicator/meaning.hpp"
#include "communicator/summarizer.hpp"
#include "communicator/truth.hpp"
#include "authored_cases.hpp"
#include "communicator/voice.hpp"
#include "packets.hpp"
#include "text.hpp"
#include "reply_shape.hpp"

namespace {

using communicator::Interpretation;
using communicator::Kind;
using communicator::Known;
using orchestrator::ActKind;
using orchestrator::FactPacket;
using Clock = std::chrono::steady_clock;

std::string environment(const char* name) {
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) return {};
    std::string out(value);
    std::free(value);
    return out;
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
}

// What a player has been told, by name. Each is built from the packets the tests use.
std::map<std::string, Known> contexts() {
    std::map<std::string, Known> out;
    Known castellan;
    for (const FactPacket& p : {packets::surroundings(), packets::work(), packets::refused_brokered(), packets::about_self()})
        castellan.learn(p);
    out["castellan"] = castellan;
    Known held = castellan;
    held.learn(packets::accepted());
    out["castellan-held"] = held;
    Known gate;
    gate.learn(packets::surroundings_from_the_gate());
    out["gate"] = gate;
    Known inn;
    inn.learn(packets::surroundings_from_the_inn());
    inn.learn(packets::work_at_the_inn());
    out["inn"] = inn;
    // A conversation just begun: the speaker has only greeted the player, so all a player knows
    // is who they are talking to.
    Known opening;
    opening.learn(packets::not_understood());
    out["opening"] = opening;
    return out;
}

// The packets voice cases name.
std::map<std::string, std::function<FactPacket()>> voice_packets() {
    return {{"surroundings", packets::surroundings},
            {"work", packets::work},
            {"about_self", packets::about_self},
            {"unknown_place", packets::unknown_place},
            {"refused_brokered", packets::refused_brokered},
            {"accepted", [] { return packets::accepted(); }},
            {"declined", packets::declined},
            {"abandoned", [] { return packets::abandoned(); }},
            {"parted", packets::parted},
            {"not_understood", packets::not_understood},
            {"gate_surroundings", packets::surroundings_from_the_gate},
            {"inn_work", packets::work_at_the_inn},
            // Contract version 4: conversation.
            {"greeted_first", packets::greeted_first},
            {"greeted_again", packets::greeted_again},
            {"greeted_warmly", packets::greeted_warmly},
            {"greeted_coldly", packets::greeted_coldly},
            {"greeted_taken", [] { return packets::greeted_after("taken", "friendly"); }},
            {"greeted_finished", [] { return packets::greeted_after("finished", "warm"); }},
            {"small_talk_fair", [] { return packets::small_talk("clear", "breezy"); }},
            {"small_talk_storm", [] { return packets::small_talk("stormy", "a gale"); }},
            {"small_talk_cold", [] { return packets::small_talk("fair", "still", "hostile"); }},
            {"acknowledged", packets::acknowledged},
            {"thanked", [] { return packets::thanked(); }},
            {"thanked_cold", [] { return packets::thanked("hostile"); }},
            {"offended", [] { return packets::offended(false); }},
            {"offended_again", [] { return packets::offended(true); }},
            {"safety_unsafe", packets::safety_unsafe},
            {"safety_wary", packets::safety_wary},
            {"safety_safe", packets::safety_safe},
            {"surroundings_again", packets::surroundings_again},
            {"exposed_mask", packets::exposed_mask},
            {"exposed_beast", packets::exposed_beast},
            {"exposed_heart", packets::exposed_heart},
            {"denied", [] { return packets::denied(false); }},
            {"denied_again", [] { return packets::denied(true); }},
            {"forgiven", [] { return packets::forgiven("neutral"); }},
            {"forgiven_cold", [] { return packets::forgiven("hostile"); }},
            {"unwronged", packets::unwronged},
            {"about_town", packets::about_the_town},
            // Contract version 9: feelings.
            {"calmed", [] { return packets::calmed("afraid"); }},
            {"still_afraid", [] { return packets::still_afraid(); }},
            {"not_afraid", [] { return packets::not_afraid(); }},
            {"greeted_panicked", [] { return packets::frightened(ActKind::Greet, orchestrator::Outcome::Greeted, "panicked"); }},
            {"surroundings_afraid", [] { return packets::frightened(ActKind::AskSurroundings, orchestrator::Outcome::Answered, "afraid"); }},
            // Contract version 10.
            {"not_afraid_eased", packets::not_afraid_eased},
            {"greeted_eased", packets::greeted_eased},
            {"joined_surroundings", packets::joined_surroundings},
            // Contract version 11: news.
            {"greeted_having_heard", packets::greeted_having_heard},
            {"small_talk_with_news", packets::small_talk_with_news},
            {"about_town_with_news", packets::about_the_town_with_news},
            // Contract version 12: what the speaker recalls of the stranger.
            {"greeted_remembering", [] { return packets::greeted_remembering(); }},
            {"about_town_told", [] { return packets::about_the_town_told(); }}};
}

std::optional<Kind> target_kind(ActKind kind) {
    switch (kind) {
        case ActKind::AskAboutPlace:
            return Kind::Place;
        case ActKind::AskAboutPerson:
            return Kind::Person;
        case ActKind::AcceptQuest:
        case ActKind::DeclineQuest:
            return Kind::Offer;
        case ActKind::AbandonQuest:
            return Kind::Held;
        default:
            return std::nullopt;
    }
}

std::string trimmed(const std::string& text) {
    const std::size_t first = text.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(' ') - first + 1);
}

// Rows of cells parted by "|", each trimmed; "#" comments and blank lines skipped; "-" is an
// empty cell. A cell cannot hold "|".
std::vector<std::pair<int, std::vector<std::string>>> rows_of(const std::string& path, std::size_t columns) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path);
    std::vector<std::pair<int, std::vector<std::string>>> out;
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (trimmed(line).empty() || trimmed(line)[0] == '#') continue;
        std::vector<std::string> cells;
        std::stringstream split(line);
        std::string cell;
        while (std::getline(split, cell, '|')) {
            cell = trimmed(cell);
            cells.push_back(cell == "-" ? std::string() : cell);
        }
        if (cells.size() != columns)
            throw std::runtime_error(path + ":" + std::to_string(number) + ": " + std::to_string(cells.size()) + " columns, not " +
                                     std::to_string(columns));
        out.emplace_back(number, cells);
    }
    return out;
}

std::string shown(const Interpretation& it) {
    return std::string(orchestrator::to_string(it.act.kind)) + (it.label.empty() ? "" : " [" + it.label + "]") +
           (it.meant ? " (meant " + std::string(orchestrator::to_string(*it.meant)) + ")" : "");
}

std::string percent(std::size_t part, std::size_t whole) {
    if (whole == 0) return "-";
    char text[16];
    std::snprintf(text, sizeof text, "%.0f%%", 100.0 * static_cast<double>(part) / static_cast<double>(whole));
    return text;
}

void interpreter_eval(inference::Model& model, const std::string& path, std::size_t limit) {
    std::map<std::string, Known> known = contexts();
    communicator::Interpreter interpreter(model);
    std::size_t cases = 0, acts_right = 0, fully_right = 0, unsafe = 0, bad_rows = 0;
    std::map<std::string, std::pair<std::size_t, std::size_t>> by_act;  // expected act: right, all
    std::map<std::string, std::map<std::string, std::size_t>> confusion;
    std::map<std::string, std::pair<std::size_t, std::size_t>> by_tag;
    std::vector<std::string> misses, unsafe_reads;
    double seconds = 0.0, slowest = 0.0;

    for (const auto& [number, cells] : rows_of(path, 6)) {
        if (limit && cases == limit) break;
        const std::string& context = cells[0];
        const std::string &last_reply = cells[1], &line = cells[2], &act_name = cells[3], &target = cells[4], &tags = cells[5];
        const std::string where = path + ":" + std::to_string(number);
        // A context ending "-afraid" is its speaker frightened: calming words are offered. One ending
        // "-worked" has the speaker's own work between them (contract version 18): a complaint is offered.
        const auto ends = [&](const std::string& suffix) {
            return context.size() > suffix.size() && context.compare(context.size() - suffix.size(), suffix.size(), suffix) == 0;
        };
        static const std::string afraid = "-afraid", worked = "-worked";
        const bool frightened = ends(afraid);
        const bool worked_for = ends(worked);
        const auto k = known.find(frightened ? context.substr(0, context.size() - afraid.size())
                                  : worked_for ? context.substr(0, context.size() - worked.size())
                                               : context);
        const std::optional<ActKind> expected = orchestrator::act_kind(act_name);
        if (k == known.end() || !expected) {
            std::cout << "BAD ROW " << where << ": context or act unknown\n";
            ++bad_rows;
            continue;
        }
        const std::optional<Kind> kind = target_kind(*expected);
        const bool unnamed = target == "?";
        if (kind.has_value() != !target.empty() || (kind && !unnamed && !k->second.resolve(*kind, target))) {
            std::cout << "BAD ROW " << where << ": the target does not fit the act or the context: " << target << "\n";
            ++bad_rows;
            continue;
        }
        ++cases;
        const Clock::time_point start = Clock::now();
        const Interpretation it = interpreter.interpret(line, k->second, last_reply, frightened, worked_for);
        const double took = std::chrono::duration<double>(Clock::now() - start).count();
        seconds += took;
        slowest = std::max(slowest, took);

        // An unnamed target is read right as no_act, meaning the act; a named one needs its label.
        const bool act_right = unnamed ? it.act.kind == ActKind::NoAct && it.meant == expected : it.act.kind == *expected;
        const bool right = act_right && (unnamed || !kind || it.label == target);
        acts_right += act_right;
        fully_right += right;
        auto& tally = by_act[act_name + (unnamed ? " (unnamed)" : "")];
        tally.first += right;
        ++tally.second;
        ++confusion[act_name][unnamed && it.act.kind == ActKind::NoAct && it.meant ? "no_act (meant)"
                                                                                   : std::string(orchestrator::to_string(it.act.kind))];
        std::stringstream tag_list(tags);
        for (std::string tag; std::getline(tag_list, tag, ',');)
            if (!tag.empty()) {
                by_tag[tag].first += right;
                ++by_tag[tag].second;
            }
        if (!right)
            misses.push_back("[" + context + "] " + line + (last_reply.empty() ? "" : "  (after: " + last_reply + ")") +
                             "\n      got " + shown(it) + "\n      want " + act_name + (target.empty() ? "" : " [" + target + "]"));
        // Unsafe: an act that would change the world, other than the one the player meant.
        if (orchestrator::act_changes_world(it.act.kind) && !right) {
            ++unsafe;
            unsafe_reads.push_back("[" + context + "] " + line + "  ->  " + shown(it));
        }
    }

    std::cout << "\n## Interpreter\n\n";
    std::cout << cases << " lines; act right " << acts_right << " (" << percent(acts_right, cases) << "), act and target right "
              << fully_right << " (" << percent(fully_right, cases) << "); unsafe reads " << unsafe << "; bad rows " << bad_rows
              << "\n";
    if (cases)
        std::cout << "time per line: mean " << seconds / static_cast<double>(cases) << " s, slowest " << slowest << " s\n";
    std::cout << "\nBy the act meant (right / all):\n";
    for (const auto& [act, t] : by_act) std::cout << "  " << act << ": " << t.first << " / " << t.second << "\n";
    std::cout << "\nBy tag (right / all):\n";
    for (const auto& [tag, t] : by_tag) std::cout << "  " << tag << ": " << t.first << " / " << t.second << "\n";
    std::cout << "\nConfusion (meant -> read as: count):\n";
    for (const auto& [meant, reads] : confusion) {
        std::cout << "  " << meant << " ->";
        for (const auto& [read, count] : reads) std::cout << " " << read << ": " << count << ";";
        std::cout << "\n";
    }
    std::cout << "\nUnsafe reads:\n";
    if (unsafe_reads.empty()) std::cout << "  none\n";
    for (const std::string& u : unsafe_reads) std::cout << "  " << u << "\n";
    std::cout << "\nMisses:\n";
    for (const std::string& m : misses) std::cout << "  " << m << "\n";
    std::cout << "\nsummary: lines=" << cases << " acts=" << acts_right << " full=" << fully_right << " unsafe=" << unsafe
              << " bad_rows=" << bad_rows << "\n";
}

// The sentences in `text`: runs of . ! ? that close it, or come before a space, a bracket or a
// quote, straight or curly (a curly one starts with the byte E2).
std::size_t sentences_in(const std::string& text) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '.' && text[i] != '!' && text[i] != '?') continue;
        while (i + 1 < text.size() && (text[i + 1] == '.' || text[i + 1] == '!' || text[i + 1] == '?')) ++i;
        const auto next = i + 1 < text.size() ? static_cast<unsigned char>(text[i + 1]) : static_cast<unsigned char>(' ');
        if (next == ' ' || next == '"' || next == '\'' || next == ')' || next == 0xE2) ++count;
    }
    return count;
}

// The lower-case words of `text`: runs of ASCII letters, lowered.
std::vector<std::string> lower_words(const std::string& text) {
    std::vector<std::string> out;
    std::string word;
    for (const char c : text) {
        if (c >= 'A' && c <= 'Z') {
            word += static_cast<char>(c - 'A' + 'a');
        } else if (c >= 'a' && c <= 'z') {
            word += c;
        } else if (!word.empty()) {
            out.push_back(word);
            word.clear();
        }
    }
    if (!word.empty()) out.push_back(word);
    return out;
}

// Every word the packet gives the voice as something to say: the speaker's name, post and place,
// and every fact's text.
std::set<std::string> sayable_words(const FactPacket& p) {
    std::set<std::string> out;
    for (const std::string* text : {&p.persona.name, &p.persona.role, &p.persona.place})
        for (const std::string& w : lower_words(*text)) out.insert(w);
    for (const orchestrator::Fact& f : p.facts)
        for (const auto& [key, value] : f.fields)
            for (const std::string& w : lower_words(value)) out.insert(w);
    return out;
}

// Words a reply said that came from the card (its manner, voice, stance and never-lines) and from
// nothing else the model was given: the card's descriptions taken for facts. A card word the
// prompt holds anyway (in a fact, the stranger's line, "fair work") is not counted. A proxy for a
// reader's judgement: it counts the words, not whether they were used as facts, and leaves out
// short and common words.
std::set<std::string> card_echoes(const communicator::Voice& voice, const FactPacket& p, const std::string& line,
                                  const std::string& reply) {
    static const std::set<std::string> common = {
        "about", "also", "anything", "before", "cannot", "could", "does", "even", "from", "have", "hold", "into", "just",
        "leave", "make", "matters", "more", "most", "must", "only", "over", "same", "shall", "should", "some", "such",
        "take", "than", "that", "their", "them", "then", "they", "this", "those", "very", "what", "when", "will", "with",
        "without", "word", "words", "would", "your"};
    FactPacket bare = p;
    bare.persona.manner.clear();
    bare.persona.voice.clear();
    bare.persona.toward_player.clear();
    bare.persona.never.clear();
    std::set<std::string> given;
    for (const inference::Message& m : voice.messages(bare, line))
        for (const std::string& w : lower_words(m.content)) given.insert(w);
    std::set<std::string> card;
    std::vector<const std::string*> sources = {&p.persona.manner, &p.persona.voice, &p.persona.toward_player};
    for (const std::string& n : p.persona.never) sources.push_back(&n);
    for (const std::string* text : sources)
        for (const std::string& w : lower_words(*text))
            if (w.size() >= 4 && !common.count(w) && !given.count(w)) card.insert(w);
    std::set<std::string> out;
    for (const std::string& w : lower_words(reply))
        if (card.count(w)) out.insert(w);
    return out;
}

// Whether `word` is a forbidden word, or begins with a forbidden stem. Restated here, not asked
// of the grammar, so that a build without the ban can be counted too.
bool forbidden_word(const std::string& word) {
    for (const std::string& f : communicator::forbidden_words()) {
        const bool stem = !f.empty() && f.back() == '*';
        if (stem ? word.rfind(f.substr(0, f.size() - 1), 0) == 0 : word == f) return true;
    }
    return false;
}

// The words of `text` that begin with a capital A-Z, as written.
void capitals_in(const std::string& text, std::set<std::string>& out) {
    std::string word;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        const char c = i < text.size() ? text[i] : ' ';
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            word += c;
            continue;
        }
        if (!word.empty() && word[0] >= 'A' && word[0] <= 'Z') out.insert(word);
        word.clear();
    }
}

// The common capitals a reply used past the start of a sentence, "I" and the packet's own
// capitals aside: common words made into names ("a castle named Greetings"). The grammar lets
// them begin a sentence only, so this counts when a build without that rule is measured.
std::set<std::string> stray_capitals(const FactPacket& p, const std::string& reply) {
    std::set<std::string> own;
    for (const std::string* text : {&p.persona.name, &p.persona.role, &p.persona.place}) capitals_in(*text, own);
    for (const orchestrator::Fact& f : p.facts)
        for (const auto& [key, value] : f.fields) capitals_in(value, own);
    const std::vector<std::string>& common = communicator::common_capitals();
    std::set<std::string> out;
    bool starts = true;
    std::string word;
    for (std::size_t i = 0; i <= reply.size(); ++i) {
        const char c = i < reply.size() ? reply[i] : ' ';
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            word += c;
            continue;
        }
        if (!word.empty()) {
            const bool listed = std::find(common.begin(), common.end(), word) != common.end();
            if (!starts && listed && word != "I" && !own.count(word)) out.insert(word);
            starts = false;
            word.clear();
        }
        const bool stop = c == '.' || c == '!' || c == '?';
        const char next = i + 1 < reply.size() ? reply[i + 1] : ' ';
        if (stop && !((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z') || (next >= '0' && next <= '9'))) starts = true;
    }
    return out;
}

std::string listed(const std::set<std::string>& words) {
    std::string out;
    for (const std::string& w : words) out += (out.empty() ? "" : ", ") + w;
    return out;
}

// Every row of `path` held to the voice's rules; the count of rows that break one.
int check_lines(const std::string& path) {
    const std::vector<authored_cases::Row> rows = authored_cases::rows_of(path);
    if (rows.empty()) throw std::runtime_error("no rows of lines at " + path);
    int broken = 0;
    std::size_t unsaid = 0;
    for (const authored_cases::Row& row : rows) {
        std::vector<std::string> claims;
        const std::vector<std::string> problems = authored_cases::row_problems(row.text, &claims);
        if (!claims.empty()) ++unsaid;
        if (problems.empty() && claims.empty()) continue;
        if (!problems.empty()) ++broken;
        std::cout << row.where << ": " << row.text << "\n";
        for (const std::string& problem : problems) std::cout << "    " << problem << "\n";
        // Not the row's fault: a claim some test packet does not back keeps the line unsaid there.
        for (const std::string& claim : claims) std::cout << "    (unsaid for this packet) " << claim << "\n";
    }
    if (unsaid) std::cout << unsaid << " rows make claims some test packet does not back: unsaid for those packets\n";
    std::cout << rows.size() - static_cast<std::size_t>(broken) << " of " << rows.size() << " rows sound\n";
    return broken;
}

// `text` as a JSON string, quotes included.
std::string json_string(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char code[8];
                    std::snprintf(code, sizeof code, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += code;
                } else {
                    out += c;
                }
        }
    }
    return out + "\"";
}

// What a packet's speaker remembers, as a note for a reader (never said): "met again, 1 before,
// days; friendly; their work: taken; asked about the burnt works last time".
std::string memory_note(const FactPacket& p) {
    for (const orchestrator::Fact& f : p.facts) {
        if (f.kind != orchestrator::FactKind::Memory) continue;
        const auto v = [&](std::string_view key) { return f.field(key) ? *f.field(key) : std::string(); };
        std::string out = v("met") == "again" ? "met again, " + v("times") + " before, last " + v("since") : std::string("first meeting");
        out += "; " + v("mood");
        if (!v("work").empty()) out += "; their work: " + v("work");
        if (!v("asked_before").empty()) out += "; asked about " + v("asked_before") + " last time";
        if (v("repeat") == "yes") out += "; asked this already";
        if (v("exposed") == "yes") out += "; exposed";
        // How they feel (contract version 9), when it is not calm: the game shows it on their face.
        if (!v("feeling").empty() && v("feeling") != "calm") out += "; feels " + v("feeling");
        if (v("eased") == "yes") out += "; talked down by the player lately";
        if (v("joined") == "yes") out += "; overheard, and joined in unasked";
        return out;
    }
    return "no memory";
}

// How many of a speaker's replies the session keeps for them, as a game would: a few visits'
// worth, more than a bank has of any situation, so a line comes round only once the others have
// (8, about two visits, let a regular hear one line of small talk every third visit).
constexpr std::size_t kLately = 32;

// A session the orchestrator's session tool played, spoken with written lines: each turn's player
// line and the speaker's reply, the time passing between, and a note of how the game read the line
// and what the speaker remembered (for a reader; never said). Each speaker is handed their own last
// reply, as a game hands it. With `meaning`, the player's words choose among the lines. With `voice`,
// the model speaks each turn, and where the truth check finds a claim its packet and what the
// speaker said lately do not back, the written line is said instead, as README.md tells a game to
// do; why goes to stderr, so a blind reader of the transcript is not told which spoke.
void session_eval(const std::string& path, const communicator::Lines& written, communicator::Meaning* meaning,
                  std::ostream* pairs, communicator::Voice* voice = nullptr) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("no session at " + path);
    std::stringstream text;
    text << in.rdbuf();
    std::istringstream rows(text.str());
    // What each speaker said lately, as a game keeps it for them.
    std::map<std::string, std::vector<std::string>> said_lately;
    std::string name, line, block, shown_speaker, spoken_to;
    bool in_packet = false;
    int turn = 0;
    double seconds = 0.0, slowest = 0.0;
    std::cout << "\n## Session (" << (voice ? "the voice, written lines where it is untrue" : "written lines")
              << (meaning ? ", chosen by meaning" : "") << ")\n";
    int voiced = 0, refused = 0;
    for (std::string row; std::getline(rows, row);) {
        if (!row.empty() && row.back() == '\r') row.pop_back();
        if (in_packet) {
            block += row + "\n";
            if (row != "end") continue;
            in_packet = false;
            const std::vector<FactPacket> packets = orchestrator::packets_from_text(block);
            if (packets.size() != 1) throw std::runtime_error("a turn of " + name + " without one packet");
            const FactPacket& p = packets.front();
            const std::string who = communicator::text::spoken(p.persona.name) + ", " +
                                    communicator::text::spaced(communicator::text::spoken(p.persona.role)) +
                                    (p.persona.face.empty() ? "" : " (" + p.persona.face + " face)");
            if (who != shown_speaker)
                std::cout << "\n== " << name << ": " << who << ", at " << communicator::text::spoken(p.persona.place) << "\n";
            shown_speaker = who;
            const auto started = std::chrono::steady_clock::now();
            std::vector<std::string>& lately = said_lately[p.speaker];
            // What the speaker said lately is part of what they remember: an answer they gave all of
            // lately is said again as told, and the note says so.
            const bool told = written.told(p, lately);
            std::string reply = written.speak(p, line, lately, meaning);
            if (voice) {
                const communicator::Reply spoken = voice->speak(p, line);
                const std::vector<std::string> wrong = communicator::untrue(spoken.text, p, lately);
                if (!spoken.text.empty() && wrong.empty()) {
                    reply = spoken.text;
                    ++voiced;
                } else {
                    ++refused;
                    std::cerr << "turn " << turn + 1 << ": the voice's reply was not said ("
                              << (wrong.empty() ? std::string("empty") : wrong.front()) << "): " << spoken.text << "\n";
                }
            }
            const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            seconds += took;
            slowest = std::max(slowest, took);
            lately.push_back(reply);
            if (lately.size() > kLately) lately.erase(lately.begin());
            // A training pair: the voice's own messages for this turn, and the written reply.
            if (pairs && !reply.empty()) {
                const std::vector<inference::Message> messages = communicator::Voice::messages(p, line);
                *pairs << "{\"system\": " << json_string(messages.at(0).content) << ", \"user\": " << json_string(messages.at(1).content)
                       << ", \"response\": " << json_string(reply) << ", \"outcome\": " << json_string(orchestrator::to_string(p.outcome))
                       << ", \"act\": " << json_string(orchestrator::to_string(p.answering)) << ", \"voice_key\": "
                       << json_string(p.persona.voice_key) << ", \"role\": " << json_string(p.persona.role) << "}\n";
            }
            // A bystander who joined in answers what the player said to someone else.
            const bool joined = std::any_of(p.facts.begin(), p.facts.end(), [](const orchestrator::Fact& f) {
                return f.kind == orchestrator::FactKind::Memory && f.field("joined") && *f.field("joined") == "yes";
            });
            std::cout << ++turn << ". " << (joined ? "Overheard, the player to " + spoken_to : std::string("Player")) << ": " << line
                      << "\n   " << communicator::text::spoken(p.persona.name) << ": "
                      << (reply.empty() ? "(no line fits)" : reply) << "\n   [" << orchestrator::to_string(p.answering) << " -> "
                      << orchestrator::to_string(p.outcome) << "; " << memory_note(p) << (told ? "; told this lately" : "") << "]\n";
            spoken_to = communicator::text::spoken(p.persona.name);
            continue;
        }
        if (row.rfind("turn ", 0) == 0) {
            name = row.substr(5);
        } else if (row.rfind("line ", 0) == 0) {
            line = row.substr(5);
        } else if (row.rfind("event ", 0) == 0) {
            std::cout << "\n-- " << row.substr(6) << " --\n";
            shown_speaker.clear();
        } else if (row.rfind("packet ", 0) == 0) {
            in_packet = true;
            block = row + "\n";
        }
    }
    if (meaning && turn > 0 && !voice)
        std::cout << "\nchoosing by meaning: " << seconds / turn << " s a turn on average, " << slowest << " s at most\n";
    if (voice) std::cerr << voiced << " replies the voice's, " << refused << " written where the voice's was untrue or empty\n";
}

// One log of summarize.txt: who remembers it, its lines, which of the stranger's lines answered a
// question of theirs (what the director's rules keep), and the runs worth keeping.
struct LabelledLog {
    std::string person;
    std::vector<communicator::LogLine> lines;
    std::vector<std::string> answers;
    std::vector<std::string> keep;
    int number = 0;
};

std::vector<LabelledLog> labelled_logs(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path);
    std::vector<LabelledLog> out;
    std::optional<LabelledLog> open;
    int number = 0;
    for (std::string row; std::getline(in, row);) {
        ++number;
        if (!row.empty() && row.back() == '\r') row.pop_back();
        if (row.empty() || row[0] == '#') continue;
        const std::string where = path + ":" + std::to_string(number) + ": ";
        if (row.rfind("log ", 0) == 0) {
            if (open) throw std::runtime_error(where + "a log inside another");
            open.emplace();
            open->person = row.substr(4);
            open->number = number;
            continue;
        }
        if (!open) throw std::runtime_error(where + "a line outside a log");
        if (row == "end") {
            out.push_back(std::move(*open));
            open.reset();
            continue;
        }
        const std::size_t bar = row.find(" | ");
        if (bar == std::string::npos) throw std::runtime_error(where + "a line is `who | words`");
        const std::string who = row.substr(0, bar), words = row.substr(bar + 3);
        if (who == "keep") {
            open->keep.push_back(words);
        } else if (who == "stranger" || who == "answer") {
            open->lines.push_back({"", words});
            if (who == "answer") open->answers.push_back(words);
        } else {
            open->lines.push_back({who, words});
        }
    }
    if (open) throw std::runtime_error(path + ": a log with no end");
    return out;
}

// The quotes of a summary, as its grammar writes them: {"quote": "..."}, never holding a quote mark.
std::vector<std::string> quotes_of(const std::string& summary) {
    std::vector<std::string> out;
    const std::string mark = "{\"quote\": \"";
    for (std::size_t at = summary.find(mark); at != std::string::npos; at = summary.find(mark, at + 1)) {
        const std::size_t from = at + mark.size();
        const std::size_t to = summary.find('"', from);
        if (to == std::string::npos) break;
        out.push_back(summary.substr(from, to - from));
    }
    return out;
}

// How many of `keep` some quote finds (either holds the other whole), and how many quotes find none.
std::pair<std::size_t, std::size_t> scored(const std::vector<std::string>& quotes, const std::vector<std::string>& keep) {
    const auto overlap = [](const std::string& a, const std::string& b) {
        return a.find(b) != std::string::npos || b.find(a) != std::string::npos;
    };
    std::size_t found = 0, junk = 0;
    for (const std::string& k : keep)
        if (std::any_of(quotes.begin(), quotes.end(), [&](const std::string& q) { return overlap(q, k); })) ++found;
    for (const std::string& q : quotes)
        if (std::none_of(keep.begin(), keep.end(), [&](const std::string& k) { return overlap(q, k); })) ++junk;
    return {found, junk};
}

void summarize_eval(inference::Model& model, const std::string& path) {
    const std::vector<LabelledLog> logs = labelled_logs(path);
    const communicator::Summarizer summarizer(model);
    std::size_t keeps = 0, model_found = 0, model_junk = 0, model_quotes = 0, rules_found = 0, rules_junk = 0, rules_quotes = 0;
    std::size_t empty_logs = 0, model_empty = 0, rules_empty = 0;
    double seconds = 0.0, slowest = 0.0;
    std::cout << "\n## the summarizer (" << logs.size() << " logs)\n";
    for (const LabelledLog& log : logs) {
        const auto started = Clock::now();
        const std::string summary = summarizer.summarize(log.person, log.lines);
        const double took = std::chrono::duration<double>(Clock::now() - started).count();
        seconds += took;
        slowest = std::max(slowest, took);
        const std::vector<std::string> quotes = quotes_of(summary);
        // The rules keep the stranger's answers whole, the latest five, as the director does.
        std::vector<std::string> rules;
        for (const std::string& answer : log.answers) {
            std::string kept = answer;
            while (!kept.empty() && (kept.back() == ' ')) kept.pop_back();
            if (!kept.empty() && kept.size() <= 240) rules.push_back(kept);
        }
        if (rules.size() > 5) rules.erase(rules.begin(), rules.end() - 5);
        const auto [found, junk] = scored(quotes, log.keep);
        const auto [by_rules, rules_off] = scored(rules, log.keep);
        keeps += log.keep.size();
        model_found += found;
        model_junk += junk;
        model_quotes += quotes.size();
        rules_found += by_rules;
        rules_junk += rules_off;
        rules_quotes += rules.size();
        if (log.keep.empty()) {
            ++empty_logs;
            model_empty += quotes.empty() ? 1 : 0;
            rules_empty += rules.empty() ? 1 : 0;
        }
        std::cout << "\nlog " << log.person << " (line " << log.number << "): " << summary << "\n  kept " << found << " of "
                  << log.keep.size() << ", " << junk << " of " << quotes.size() << " quotes worth nothing; the rules " << by_rules
                  << " of " << log.keep.size() << ", " << rules_off << " of " << rules.size() << "; " << took << " s\n";
    }
    std::cout << "\nsummarizer: runs worth keeping found " << model_found << " of " << keeps << "; quotes worth nothing " << model_junk
              << " of " << model_quotes << "; logs with nothing worth keeping kept empty " << model_empty << " of " << empty_logs
              << "\nrules (answers whole): found " << rules_found << " of " << keeps << "; quotes worth nothing " << rules_junk << " of "
              << rules_quotes << "; logs with nothing worth keeping kept empty " << rules_empty << " of " << empty_logs
              << "\ntime: " << seconds << " s in all, slowest " << slowest << " s\n";
}

void voice_eval(inference::Model& model, const std::string& path, const communicator::Lines* written,
                const std::string& voice_key, const std::string& post) {
    communicator::Voice voice(model);
    const auto packets_by_name = voice_packets();
    // A written reply's speaker is handed their last reply, as a game hands it: cases on one
    // packet are turns with one speaker.
    std::map<std::string, std::string> last_reply;
    std::size_t cases = 0, shaped = 0, echoing = 0, echoes = 0, forbidden_said = 0, days_said = 0, straying = 0, strays = 0,
                untrue_replies = 0, untrue_claims = 0;
    double seconds = 0.0, slowest = 0.0;
    std::cout << "\n## Voice" << (written ? " (written lines)" : "") << "\n\n";
    for (const auto& [number, cells] : rows_of(path, 2)) {
        const auto found = packets_by_name.find(cells[0]);
        if (found == packets_by_name.end()) {
            std::cout << "BAD ROW " << path << ":" << number << ": no packet " << cells[0] << "\n";
            continue;
        }
        FactPacket packet = found->second();
        if (!post.empty() && !packet.persona.voice_key.empty()) {
            packet.persona.voice_key.clear();
            packet.persona.role = post;
        } else if (!voice_key.empty() && !packet.persona.voice_key.empty()) {
            packet.persona.voice_key = voice_key;
        }
        const communicator::ReplyShape shape = communicator::reply_shape(packet);
        const Clock::time_point start = Clock::now();
        communicator::Reply reply;
        const std::vector<std::string> recent = {last_reply[cells[0]]};
        if (written) {
            reply.text = written->speak(packet, cells[1], recent);
            reply.finished = !reply.text.empty();
        } else {
            reply = voice.speak(packet, cells[1]);
        }
        last_reply[cells[0]] = reply.text;
        const double took = std::chrono::duration<double>(Clock::now() - start).count();
        seconds += took;
        slowest = std::max(slowest, took);
        ++cases;

        std::string said = reply.text;
        // A refusal closes on who to see; a written line that named them already, on where they are.
        const auto ends_with = [&](const std::string& closing) {
            return !closing.empty() && said.size() > closing.size() &&
                   said.compare(said.size() - closing.size(), closing.size(), closing) == 0;
        };
        const std::string& closing = written && ends_with(shape.closing_after_name) ? shape.closing_after_name : shape.closing;
        const bool closed = closing.empty() || ends_with(closing);
        if (!closing.empty() && closed) said = said.substr(0, said.size() - closing.size());
        const bool opens = shape.openings.empty() || std::any_of(shape.openings.begin(), shape.openings.end(),
                                                                 [&](const std::string& o) { return said.rfind(o, 0) == 0; });
        const std::size_t sentences = sentences_in(said);
        const bool short_enough = sentences >= 1 && sentences <= static_cast<std::size_t>(shape.sentences);
        const bool ok = opens && closed && short_enough && reply.finished;
        shaped += ok;

        // What a reply said that its packet did not give it: the card's words, forbidden words,
        // and talk of days (an offer's deadline, misread as the work's length before).
        const std::set<std::string> echo = card_echoes(voice, packet, cells[1], said);
        const std::set<std::string> sayable = sayable_words(packet);
        std::set<std::string> banned;
        bool days = false;
        for (const std::string& w : lower_words(said)) {
            if (forbidden_word(w) && !sayable.count(w)) banned.insert(w);
            if (w == "day" || w == "days") days = true;
        }
        const std::set<std::string> stray = stray_capitals(packet, said);
        // What the truth check finds claimed without ground (truth.hpp): a game falls back to the
        // written lines for such a reply.
        const std::vector<std::string> claims = communicator::untrue(reply.text, packet, recent);
        untrue_replies += !claims.empty();
        untrue_claims += claims.size();
        echoing += !echo.empty();
        echoes += echo.size();
        forbidden_said += banned.size();
        days_said += days;
        straying += !stray.empty();
        strays += stray.size();
        std::cout << (ok ? "  ok   " : "  OFF  ") << "[" << cells[0] << " / " << orchestrator::to_string(packet.outcome) << "] "
                  << cells[1] << "\n         " << reply.text << "\n         (" << took << " s, " << reply.tokens << " tokens"
                  << (opens ? "" : ", wrong opening") << (closed ? "" : ", no closing") << (short_enough ? "" : ", too long")
                  << (reply.finished ? "" : ", cut off") << (echo.empty() ? "" : "; card words: " + listed(echo))
                  << (banned.empty() ? "" : "; forbidden: " + listed(banned)) << (days ? "; days" : "")
                  << (stray.empty() ? "" : "; mid-sentence capitals: " + listed(stray))
                  << (claims.empty() ? "" : "; untrue: " + listed(std::set<std::string>(claims.begin(), claims.end()))) << ")\n";
    }
    if (cases) {
        std::cout << "\n" << shaped << " of " << cases << " replies hold their shape; mean " << seconds / static_cast<double>(cases)
                  << " s, slowest " << slowest << " s\n";
        std::cout << "card words said in " << echoing << " of " << cases << " replies (" << echoes << " words); forbidden words said: "
                  << forbidden_said << "; replies talking of days: " << days_said << "; common capitals mid-sentence: " << strays
                  << " in " << straying << " replies\n";
        std::cout << "untrue claims (truth.hpp): " << untrue_claims << " in " << untrue_replies << " of " << cases << " replies\n";
    }
    std::cout << "summary: replies=" << cases << " shaped=" << shaped << " card_echo_replies=" << echoing << " card_echo_words=" << echoes
              << " forbidden=" << forbidden_said << " days=" << days_said << " stray_capital_replies=" << straying
              << " stray_capitals=" << strays << " untrue_replies=" << untrue_replies << " untrue_claims=" << untrue_claims << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string model_path = environment("FWGC_TEST_MODEL");
    if (model_path.empty()) model_path = COMMUNICATOR_TEST_MODEL;
    std::string lines = std::string(COMMUNICATOR_EVAL_DIR) + "/lines.txt";
    std::string voice = std::string(COMMUNICATOR_EVAL_DIR) + "/voice.txt";
    std::string authored, voice_key, post, check, session, meaning_path, export_pairs, summarize, voice_model;
    std::size_t limit = 0;
    inference::ModelOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error(arg + " needs a value");
            return argv[++i];
        };
        if (arg == "--model") model_path = value();
        else if (arg == "--lines") lines = value();
        else if (arg == "--voice") voice = value();
        else if (arg == "--authored") authored = value();
        else if (arg == "--voice-key") voice_key = value();
        else if (arg == "--post") post = value();
        else if (arg == "--check-lines") check = value();
        else if (arg == "--session") session = value();
        else if (arg == "--export-pairs") export_pairs = value();
        else if (arg == "--meaning") meaning_path = value();
        else if (arg == "--voice-model") voice_model = value();
        else if (arg == "--summarize") summarize = value();
        else if (arg == "--limit") limit = static_cast<std::size_t>(std::stoul(value()));
        else if (arg == "--threads") options.threads = std::stoi(value());
        else if (arg == "--gpu-layers") options.gpu_layers = std::stoi(value());
        else {
            std::cerr << "usage: communicator_eval [--model PATH] [--lines PATH] [--voice PATH] [--authored PATH]"
                         " [--voice-key KEY] [--post POST] [--limit N] [--threads N] [--gpu-layers N]\n"
                         "       communicator_eval --check-lines PATH\n"
                         "       communicator_eval --session PACKETS [--authored PATH] [--meaning PATH] [--voice-model PATH] [--gpu-layers N]\n"
                         "       communicator_eval --summarize LOGS [--model PATH] [--threads N] [--gpu-layers N]\n";
            return 2;
        }
    }
    try {
        if (!check.empty()) return check_lines(check) == 0 ? 0 : 1;
        if (!session.empty()) {
            const communicator::Lines written = communicator::Lines::load(authored.empty() ? std::string(COMMUNICATOR_LINES) : authored);
            std::cout << "# communicator_eval\n\nwritten lines: " << (authored.empty() ? COMMUNICATOR_LINES : authored.c_str())
                      << " (" << written.size() << ")\nsession: " << session << "\n";
            std::unique_ptr<inference::Model> embedder;
            std::optional<communicator::Meaning> meaning;
            if (!meaning_path.empty()) {
                inference::ModelOptions embedding = options;
                embedding.context_tokens = 512;
                embedding.embeddings = true;
                embedder = inference::Model::load(meaning_path, embedding);
                meaning.emplace(*embedder);
                std::cout << "meaning: " << meaning_path << " on " << embedder->device() << "\n";
            }
            std::ofstream pairs;
            if (!export_pairs.empty()) {
                pairs.open(export_pairs, std::ios::binary);
                if (!pairs) throw std::runtime_error("cannot write " + export_pairs);
            }
            std::unique_ptr<inference::Model> speaker;
            std::optional<communicator::Voice> speaking;
            if (!voice_model.empty()) {
                speaker = inference::Model::load(voice_model, options);
                speaking.emplace(*speaker);
                std::cerr << "voice: " << voice_model << " on " << speaker->device() << "\n";
            }
            session_eval(session, written, meaning ? &*meaning : nullptr, export_pairs.empty() ? nullptr : &pairs, speaking ? &*speaking : nullptr);
            return 0;
        }
        std::unique_ptr<inference::Model> model = inference::Model::load(model_path, options);
        std::cout << "# communicator_eval\n\nmodel: " << model_path << "\nthreads: " << options.threads
                  << "\ndevice: " << model->device() << "\n";
        if (!summarize.empty()) {
            summarize_eval(*model, summarize);
            return 0;
        }
        if (!lines.empty()) interpreter_eval(*model, lines, limit);
        if (!voice.empty()) {
            std::optional<communicator::Lines> written;
            if (!authored.empty()) {
                written = communicator::Lines::load(authored);
                std::cout << "written lines: " << authored << " (" << written->size() << ")\n";
            }
            if (!voice_key.empty()) std::cout << "voice key: " << voice_key << "\n";
            if (!post.empty()) std::cout << "post: " << post << "\n";
            voice_eval(*model, voice, written ? &*written : nullptr, voice_key, post);
        }
    } catch (const std::exception& e) {
        std::cerr << "communicator_eval: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
