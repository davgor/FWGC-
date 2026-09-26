// A scripted session with the director, written out as packets for another tool to read:
// `communicator_eval --session` speaks each one with the written lines, for a reader or a judge
// to hear a whole conversation. A measuring tool, not a test, and nothing a game ships.
//
//   orchestrator_session SCRIPT > session.txt
//
// The script, one command a line; `#` starts a comment:
//   world SEED SIZE DAYS            generate, then let DAYS pass (a month opens the quest board)
//   who NAME PICK N                 name a speaker: the Nth, from 0, of PICK --
//                                   townsfolk: a living roster person who gives no work, one to
//                                              a node, city folk first;
//                                   giver:     a hero with open work of their own;
//                                   mask, beast, heart: a living hero with that kind of hidden
//                                              face (public and true; calm and unleashed; first
//                                              and second);
//                                   plain:     a living hero with one face
//   say NAME ACT [TARGET] | LINE    the player says LINE to NAME, read as ACT. TARGET is @place
//                                   (the first place the last packet named), @own (the first of
//                                   NAME's own offers the player was told of), @held (the work
//                                   taken from NAME), or another speaker's NAME
//   advance DAYS                    time passes
//   resolve NAME finished|failed    the game reports the work taken from NAME
//   fullmoon                        time passes to the middle of the next full-moon day
//   calmday                         time passes to the middle of the next day at least two days
//                                   from a full moon
//   fright NAME LEVEL               the game reports something frightening NAME: 1 uneasy, 2
//                                   afraid, 3 panicked
//   gather NAME NAME...             the game reports these speakers together: fear spreads
//   minutes N                       N minutes pass
//
// Written out, in order: for each `say`, `turn NAME`, `line LINE` and the packet
// (orchestrator::packet_text); for anything else that happens, `event TEXT`.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "fwg/api/quests.hpp"
#include "fwg/api/read.hpp"
#include "fwg/api/weather.hpp"
#include "fwg/api/world.hpp"
#include "fwg/api/world_time.hpp"
#include "orchestrator/director.hpp"

namespace {

using orchestrator::ActKind;
using orchestrator::Director;
using orchestrator::Fact;
using orchestrator::FactKind;
using orchestrator::FactPacket;

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

std::string trimmed(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

std::vector<std::string> words(const std::string& text) {
    std::istringstream in(text);
    std::vector<std::string> out;
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

std::optional<int> node_of(const fwg::World& world, const std::string& uid) {
    fwg::PersonReadRequest request;
    request.uid = uid;
    return fwg::read_person(world, request).person.anchor.node;
}

bool has_face(const fwg::Hero& hero, std::string_view label) {
    for (const fwg::HeroFace& face : hero.faces)
        if (face.label == label) return true;
    return false;
}

std::vector<fwg::QuestRow> offered(const fwg::World& world) {
    fwg::QuestsReadRequest request;
    request.states = std::vector<fwg::QuestState>{fwg::QuestState::Offered};
    return fwg::read_quests(world, request).quests;
}

// The Nth speaker of a kind, as `who` picks them.
std::string pick(const fwg::World& world, const std::string& kind, std::size_t n) {
    std::vector<std::string> found;
    std::set<std::string> givers;
    for (const fwg::QuestRow& q : offered(world))
        if (q.giver_uid) givers.insert(*q.giver_uid);
    if (kind == "townsfolk") {
        std::set<int> nodes;
        for (const bool city : {true, false})
            for (const fwg::NpcPerson& p : world.npcs.people) {
                if (!p.alive || p.is_cast || givers.count(p.uid)) continue;
                if (city != (p.site_kind && *p.site_kind == "city")) continue;
                const std::optional<int> node = node_of(world, p.uid);
                if (node && nodes.insert(*node).second) found.push_back(p.uid);
            }
    } else if (kind == "giver") {
        for (const fwg::QuestRow& q : offered(world))
            if (q.giver_uid && q.giver && q.giver->anchor.node && q.giver->liveness == fwg::Liveness::Present &&
                std::find(found.begin(), found.end(), *q.giver_uid) == found.end())
                found.push_back(*q.giver_uid);
    } else {
        const std::map<std::string, std::pair<std::string, std::string>> faces = {
            {"mask", {"public", "true"}}, {"beast", {"calm", "unleashed"}}, {"heart", {"first", "second"}}};
        for (const fwg::Hero& h : world.heroes.people) {
            if (h.status != "living" || !node_of(world, h.uid)) continue;
            if (kind == "plain" ? h.faces.size() == 1
                                : faces.count(kind) && has_face(h, faces.at(kind).first) && has_face(h, faces.at(kind).second))
                found.push_back(h.uid);
        }
        if (kind != "plain" && !faces.count(kind)) throw Failure("who: no kind of speaker called " + kind);
    }
    if (n >= found.size()) throw Failure("who: the world holds " + std::to_string(found.size()) + " " + kind + ", not " + std::to_string(n + 1));
    return found[n];
}

bool full_moon_near(const fwg::World& world, double day) {
    for (const double d : {day - 1.0, day, day + 1.0}) {
        const std::optional<fwg::MoonPhase> moon = fwg::moon_phase_on(world, std::floor(std::max(0.0, d)));
        if (!moon) throw Failure("the world has no moon");
        if (moon->full) return true;
    }
    return false;
}

// Time to the middle of the first whole day, from tomorrow, that `wanted` accepts.
double days_to(const Director& d, const std::function<bool(double)>& wanted) {
    const double now = fwg::world_clock_of(d.world()).day;
    for (double day = std::floor(now) + 1.0; day < now + 120.0; day += 1.0)
        if (wanted(day)) return day + 0.5 - now;
    throw Failure("no such day within 120 days");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: orchestrator_session SCRIPT\n";
        return 2;
    }
    std::ifstream in(argv[1]);
    if (!in) {
        std::cerr << "orchestrator_session: cannot read " << argv[1] << "\n";
        return 2;
    }
    std::optional<Director> director;
    std::map<std::string, std::string> speakers;           // name -> uid
    std::map<std::string, std::string> own_offer, held;    // name -> quest id
    std::optional<FactPacket> last;
    int number = 0;
    try {
        for (std::string raw; std::getline(in, raw);) {
            ++number;
            const std::string row = trimmed(raw.substr(0, raw.find('#')));
            if (row.empty()) continue;
            const std::string where = std::string(argv[1]) + ":" + std::to_string(number) + ": ";
            const std::size_t bar = row.find(" | ");
            const std::vector<std::string> w = words(row.substr(0, bar));
            const auto need = [&](std::size_t count) {
                if (w.size() < count) throw Failure(where + "too few words: " + row);
            };
            const auto speaker = [&](const std::string& name) {
                const auto found = speakers.find(name);
                if (found == speakers.end()) throw Failure(where + "no speaker called " + name);
                return found->second;
            };
            if (w[0] == "world") {
                need(4);
                fwg::Request request;
                request.seed = static_cast<std::uint32_t>(std::stoul(w[1]));
                request.size = std::stoi(w[2]);
                director.emplace(fwg::generate(fwg::resolve(request)));
                director->advance(std::stod(w[3]));
                std::cout << "event the world, seed " << w[1] << " size " << w[2] << ", " << w[3] << " days on\n";
                continue;
            }
            if (!director) throw Failure(where + "no world yet");
            if (w[0] == "who") {
                need(4);
                speakers[w[1]] = pick(director->world(), w[2], std::stoul(w[3]));
            } else if (w[0] == "say") {
                need(3);
                if (bar == std::string::npos) throw Failure(where + "a say needs `| LINE`");
                const std::string name = w[1];
                const std::optional<ActKind> act = orchestrator::act_kind(w[2]);
                if (!act) throw Failure(where + "no act called " + w[2]);
                std::string target;
                if (w.size() > 3) {
                    const std::string& t = w[3];
                    if (t == "@place") {
                        if (last)
                            for (const Fact& f : last->facts)
                                if (target.empty() && f.kind == FactKind::Place) target = f.subject;
                    } else if (t == "@own") {
                        target = own_offer.count(name) ? own_offer.at(name) : std::string();
                    } else if (t == "@held") {
                        target = held.count(name) ? held.at(name) : std::string();
                    } else {
                        target = speaker(t);
                    }
                    if (target.empty()) throw Failure(where + "nothing for " + t + " to point at yet");
                }
                const std::string line = trimmed(row.substr(bar + 3));
                FactPacket p = director->respond(speaker(name), {*act, target});
                for (const Fact& f : p.facts) {
                    if (f.kind == FactKind::QuestOffer && f.field("from_speaker") && *f.field("from_speaker") == "yes" &&
                        !own_offer.count(name))
                        own_offer[name] = f.subject;
                    if (f.kind == FactKind::QuestState && f.field("state") && *f.field("state") == "taken") held[name] = f.subject;
                }
                std::cout << "turn " << name << "\nline " << line << "\n" << orchestrator::packet_text(p);
                last = std::move(p);
            } else if (w[0] == "advance") {
                need(2);
                director->advance(std::stod(w[1]));
                std::cout << "event " << w[1] << " days pass\n";
            } else if (w[0] == "resolve") {
                need(3);
                if (!held.count(w[1])) throw Failure(where + "no work taken from " + w[1]);
                director->resolve(held.at(w[1]), w[2] == "finished");
                std::cout << "event the work taken from " << w[1] << " is " << (w[2] == "finished" ? "finished" : "failed") << "\n";
            } else if (w[0] == "fright") {
                need(3);
                director->fright(speaker(w[1]), std::stoi(w[2]));
                std::cout << "event something frightens " << w[1] << ": " << director->feeling(speaker(w[1])).now << "\n";
            } else if (w[0] == "gather") {
                need(3);
                std::vector<std::string> together;
                for (std::size_t i = 1; i < w.size(); ++i) together.push_back(speaker(w[i]));
                director->gather(together);
                std::cout << "event together:";
                for (std::size_t i = 1; i < w.size(); ++i) std::cout << " " << w[i] << " " << director->feeling(together[i - 1]).now;
                std::cout << "\n";
            } else if (w[0] == "minutes") {
                need(2);
                director->advance(std::stod(w[1]) / 1440.0);
                std::cout << "event " << w[1] << " minutes pass\n";
            } else if (w[0] == "fullmoon" || w[0] == "calmday") {
                const bool full = w[0] == "fullmoon";
                const fwg::World& world = director->world();
                const double days = days_to(*director, [&](double day) {
                    if (full) {
                        const std::optional<fwg::MoonPhase> moon = fwg::moon_phase_on(world, day);
                        return moon && moon->full;
                    }
                    return !full_moon_near(world, day);
                });
                director->advance(days);
                std::cout << "event " << std::lround(days) << " days pass, to " << (full ? "the full moon" : "a day far from it") << "\n";
            } else {
                throw Failure(where + "no command called " + w[0]);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "orchestrator_session: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
