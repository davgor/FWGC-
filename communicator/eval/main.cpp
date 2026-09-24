// communicator_eval: the interpreter and the voice measured against labelled cases, on a model.
//
//   communicator_eval [--model PATH] [--lines PATH] [--voice PATH] [--limit N] [--threads N]
//
// A measuring tool, not a test: it passes or fails nothing. For the interpreter it prints how
// often a line is read as its label says, what was mistaken for what, and every unsafe read: an
// act that would change the world which the player did not mean. For the voice it checks each
// reply's shape (its opening, its length, its closing, its names) and prints every reply, for a
// reader to judge what no check can: whether it claims anything false.
//
// lines.txt and voice.txt (README.md in this folder) hold the cases. The contexts they name are
// what a player has been told, built from the packets the tests use.
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "communicator/interpreter.hpp"
#include "communicator/known.hpp"
#include "communicator/voice.hpp"
#include "packets.hpp"
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
            {"inn_work", packets::work_at_the_inn}};
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
        const auto k = known.find(context);
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
        const Interpretation it = interpreter.interpret(line, k->second, last_reply);
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

void voice_eval(inference::Model& model, const std::string& path) {
    communicator::Voice voice(model);
    const auto packets_by_name = voice_packets();
    std::size_t cases = 0, shaped = 0;
    double seconds = 0.0, slowest = 0.0;
    std::cout << "\n## Voice\n\n";
    for (const auto& [number, cells] : rows_of(path, 2)) {
        const auto found = packets_by_name.find(cells[0]);
        if (found == packets_by_name.end()) {
            std::cout << "BAD ROW " << path << ":" << number << ": no packet " << cells[0] << "\n";
            continue;
        }
        const FactPacket packet = found->second();
        const communicator::ReplyShape shape = communicator::reply_shape(packet);
        const Clock::time_point start = Clock::now();
        const communicator::Reply reply = voice.speak(packet, cells[1]);
        const double took = std::chrono::duration<double>(Clock::now() - start).count();
        seconds += took;
        slowest = std::max(slowest, took);
        ++cases;

        std::string said = reply.text;
        const bool closed = shape.closing.empty() || (said.size() > shape.closing.size() &&
                                                      said.compare(said.size() - shape.closing.size(), shape.closing.size(),
                                                                   shape.closing) == 0);
        if (!shape.closing.empty() && closed) said = said.substr(0, said.size() - shape.closing.size());
        const bool opens = shape.openings.empty() || std::any_of(shape.openings.begin(), shape.openings.end(),
                                                                 [&](const std::string& o) { return said.rfind(o, 0) == 0; });
        const std::size_t sentences = sentences_in(said);
        const bool short_enough = sentences >= 1 && sentences <= static_cast<std::size_t>(shape.sentences);
        const bool ok = opens && closed && short_enough && reply.finished;
        shaped += ok;
        std::cout << (ok ? "  ok   " : "  OFF  ") << "[" << cells[0] << " / " << orchestrator::to_string(packet.outcome) << "] "
                  << cells[1] << "\n         " << reply.text << "\n         (" << took << " s, " << reply.tokens << " tokens"
                  << (opens ? "" : ", wrong opening") << (closed ? "" : ", no closing") << (short_enough ? "" : ", too long")
                  << (reply.finished ? "" : ", cut off") << ")\n";
    }
    if (cases)
        std::cout << "\n" << shaped << " of " << cases << " replies hold their shape; mean " << seconds / static_cast<double>(cases)
                  << " s, slowest " << slowest << " s\n";
    std::cout << "summary: replies=" << cases << " shaped=" << shaped << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string model_path = environment("FWGC_TEST_MODEL");
    if (model_path.empty()) model_path = COMMUNICATOR_TEST_MODEL;
    std::string lines = std::string(COMMUNICATOR_EVAL_DIR) + "/lines.txt";
    std::string voice = std::string(COMMUNICATOR_EVAL_DIR) + "/voice.txt";
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
        else if (arg == "--limit") limit = static_cast<std::size_t>(std::stoul(value()));
        else if (arg == "--threads") options.threads = std::stoi(value());
        else {
            std::cerr << "usage: communicator_eval [--model PATH] [--lines PATH] [--voice PATH] [--limit N] [--threads N]\n";
            return 2;
        }
    }
    try {
        std::unique_ptr<inference::Model> model = inference::Model::load(model_path, options);
        std::cout << "# communicator_eval\n\nmodel: " << model_path << "\nthreads: " << options.threads << "\n";
        if (!lines.empty()) interpreter_eval(*model, lines, limit);
        if (!voice.empty()) voice_eval(*model, voice);
    } catch (const std::exception& e) {
        std::cerr << "communicator_eval: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
