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
//   who NAME near OTHER N           name a speaker: the Nth, from 0, of the living roster people
//                                   standing where OTHER, a speaker named before, stands
//   say NAME ACT [TARGET] | LINE    the player says LINE to NAME, read as ACT. TARGET is @place
//                                   (the first place named by the latest packet to name any),
//                                   @said (the place, of those any packet named, whose name LINE
//                                   says), @own (the first of NAME's own offers the player was
//                                   told of), @held (the work taken from NAME), =TEXT (the text
//                                   itself: the name the player gives, for introduce), or another
//                                   speaker's NAME
//   advance DAYS                    time passes
//   resolve NAME finished|failed    the game reports the work taken from NAME
//   account NAME CAME MANY COST PART  the game says how the work taken from NAME went: CAME @nest (the
//                                   first nest named by the latest packet to name any), a speaker's
//                                   NAME, or -; MANY one, few, many, horde; COST none, few, many;
//                                   PART fought, led, turned, held, hurt; - for any not said
//   fullmoon                        time passes to the middle of the next full-moon day
//   calmday                         time passes to the middle of the next day at least two days
//                                   from a full moon
//   fright NAME LEVEL               the game reports something frightening NAME: 1 uneasy, 2
//                                   afraid, 3 panicked
//   gather NAME NAME...             the game reports these speakers together: fear spreads
//   minutes N                       N minutes pass
//   overhear NAME...                these speakers overheard the last `say`: each is calmed by it,
//                                   joins in (their reply is written as their turn), or stays out
//   share [NAME...]                 news reaches these speakers, or everyone when none are named
//   heroes [NAME...]                these heroes, or every hero when none are named, act if due
//                                   (contract version 26); what a speaker did written as an event in
//                                   the world's words, and the rest of the world's heroes counted
//   briefs                          what a picker would be shown for each deed of the last `heroes`
//                                   not yet taken, written as text (orchestrator::brief_text)
//   react [NAME [REACTION]]         how heroes took what they did at the last `heroes` (contract
//                                   version 30): NAME's deeds, as REACTION or by the rules; with no
//                                   NAME, every hero's by the rules, as a game with no picker would
//   report NAME OUTCOME [BY]        the game says how a threat where NAME stands ended: beaten,
//                                   lost or passed; BY player, a speaker's NAME, or nobody
//   sql QUERY                       read the brain database; each row written as an event
//   compact NAME [FILE]             NAME's conversation log made into long memory, by the rules, or
//                                   with FILE's outcomes (JSON, as compact takes them); what
//                                   it kept written as events
//
// Every `say` hands the director the player's line as the exchange's words (the reply is spoken
// later, by another tool, so none is handed), and so does a bystander who joins in.
//
// Written out, in order: for each `say`, `turn NAME`, `line LINE` and the packet
// (orchestrator::packet_text); for anything else that happens, `event TEXT`.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
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

std::string lowered(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
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
    std::vector<std::string> named;  // the places the latest packet to name any named
    std::vector<std::string> nests;  // the nests the latest packet to name any named
    std::map<std::string, std::string> names;  // every place name any packet gave, lowered -> uid
    std::string last_line;  // what the player said last, which a bystander who joins answers
    std::vector<orchestrator::HeroDone> last_done;  // what heroes did at the last `heroes`
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
            const auto remember = [&](const FactPacket& p) {
                std::vector<std::string> places;
                for (const Fact& f : p.facts)
                    if (f.kind == FactKind::Place) {
                        places.push_back(f.subject);
                        if (const std::string* name = f.field("name")) names[lowered(*name)] = f.subject;
                    }
                if (!places.empty()) named = std::move(places);
                std::vector<std::string> dangers;
                for (const Fact& f : p.facts)
                    if (f.kind == FactKind::Danger) dangers.push_back(f.subject);
                if (!dangers.empty()) nests = std::move(dangers);
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
            if (w[0] == "who" && w.size() > 2 && w[2] == "near") {
                need(5);
                const std::string other = speaker(w[3]);
                const fwg::World& world = director->world();
                const std::optional<int> at = node_of(world, other);
                if (!at) throw Failure(where + w[3] + " stands nowhere");
                std::vector<std::string> near;
                for (const fwg::NpcPerson& p : world.npcs.people)
                    if (p.alive && !p.is_cast && p.uid != other && node_of(world, p.uid) == at) near.push_back(p.uid);
                const std::size_t n = std::stoul(w[4]);
                if (n >= near.size()) throw Failure(where + std::to_string(near.size()) + " people where " + w[3] + " stands, not " + std::to_string(n + 1));
                speakers[w[1]] = near[n];
            } else if (w[0] == "who") {
                need(4);
                speakers[w[1]] = pick(director->world(), w[2], std::stoul(w[3]));
            } else if (w[0] == "say") {
                need(3);
                if (bar == std::string::npos) throw Failure(where + "a say needs `| LINE`");
                const std::string name = w[1];
                const std::optional<ActKind> act = orchestrator::act_kind(w[2]);
                if (!act) throw Failure(where + "no act called " + w[2]);
                const std::string line = trimmed(row.substr(bar + 3));
                std::string target;
                if (w.size() > 3) {
                    const std::string& t = w[3];
                    if (t == "@place") {
                        if (!named.empty()) target = named.front();
                    } else if (t == "@said") {
                        std::size_t longest = 0;
                        for (const auto& [place, uid] : names)
                            if (place.size() > longest && lowered(line).find(place) != std::string::npos) {
                                longest = place.size();
                                target = uid;
                            }
                    } else if (t == "@own") {
                        target = own_offer.count(name) ? own_offer.at(name) : std::string();
                    } else if (t == "@held") {
                        target = held.count(name) ? held.at(name) : std::string();
                    } else if (t.size() > 1 && t[0] == '=') {
                        target = t.substr(1);
                    } else {
                        target = speaker(t);
                    }
                    if (target.empty()) throw Failure(where + "nothing for " + t + " to point at yet");
                }
                FactPacket p = director->respond(speaker(name), {*act, target});
                director->words(line, "");
                for (const Fact& f : p.facts) {
                    if (f.kind == FactKind::QuestOffer && f.field("from_speaker") && *f.field("from_speaker") == "yes" &&
                        !own_offer.count(name))
                        own_offer[name] = f.subject;
                    if (f.kind == FactKind::QuestState && f.field("state") && *f.field("state") == "taken") held[name] = f.subject;
                }
                std::cout << "turn " << name << "\nline " << line << "\n" << orchestrator::packet_text(p);
                remember(p);
                last_line = line;
            } else if (w[0] == "advance") {
                need(2);
                director->advance(std::stod(w[1]));
                std::cout << "event " << w[1] << " days pass\n";
            } else if (w[0] == "resolve") {
                need(3);
                if (!held.count(w[1])) throw Failure(where + "no work taken from " + w[1]);
                director->resolve(held.at(w[1]), w[2] == "finished");
                std::cout << "event the work taken from " << w[1] << " is " << (w[2] == "finished" ? "finished" : "failed") << "\n";
            } else if (w[0] == "account") {
                need(6);
                if (!held.count(w[1])) throw Failure(where + "no work taken from " + w[1]);
                const auto said = [](const std::string& word) { return word == "-" ? std::string() : word; };
                std::string came;
                if (w[2] == "@nest") {
                    if (nests.empty()) throw Failure(where + "no nest named yet");
                    came = nests.front();
                } else if (w[2] != "-") {
                    came = speaker(w[2]);
                }
                director->account(held.at(w[1]), came, said(w[3]), said(w[4]), said(w[5]));
                std::cout << "event the game's account of the work taken from " << w[1] << ": " << w[2] << " " << w[3] << " " << w[4] << " "
                          << w[5] << "\n";
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
            } else if (w[0] == "overhear") {
                need(2);
                std::vector<std::string> people;
                for (std::size_t i = 1; i < w.size(); ++i) people.push_back(speaker(w[i]));
                // What each has heard by overhearing, before and after, so the news this exchange
                // carried to them is said (contract version 12).
                const auto overheard_news = [&](const std::string& person) {
                    std::vector<std::string> out;
                    for (const std::vector<orchestrator::Cell>& fact :
                         director->query("SELECT e.kind, e.place_name, f.state FROM facts AS f JOIN events AS e ON e.id = f.event WHERE "
                                         "f.person = '" + person + "' AND f.how = 'overheard' ORDER BY e.seq").rows) {
                        const auto text = [&](std::size_t c) {
                            const auto* value = std::get_if<std::string>(&fact[c]);
                            return value ? *value : std::string();
                        };
                        out.push_back("the " + text(0) + (text(1).empty() ? "" : " at " + text(1)) + ", " + text(2));
                    }
                    return out;
                };
                std::vector<std::vector<std::string>> known_before;
                for (const std::string& person : people) known_before.push_back(overheard_news(person));
                const std::vector<orchestrator::Bystander> heard = director->overhear(people);
                for (std::size_t i = 0; i < heard.size(); ++i) {
                    const orchestrator::Bystander& b = heard[i];
                    if (b.reaction == orchestrator::Overheard::joins && b.packet) {
                        std::cout << "event " << w[i + 1] << " overheard, and joins in\n";
                        std::cout << "turn " << w[i + 1] << "\nline " << last_line << "\n" << orchestrator::packet_text(*b.packet);
                        director->words(last_line, "");
                        remember(*b.packet);
                    } else {
                        std::cout << "event " << w[i + 1] << " overheard: " << orchestrator::to_string(b.reaction) << ", "
                                  << director->feeling(b.person).now << "\n";
                    }
                    for (const std::string& news : overheard_news(b.person))
                        if (std::find(known_before[i].begin(), known_before[i].end(), news) == known_before[i].end())
                            std::cout << "event " << w[i + 1] << " heard, overhearing it: " << news << "\n";
                }
            } else if (w[0] == "share") {
                std::vector<std::string> people;
                for (std::size_t i = 1; i < w.size(); ++i) people.push_back(speaker(w[i]));
                const std::vector<orchestrator::Learned> learned = director->share(people);
                std::cout << "event news goes round: " << learned.size() << " heard something\n";
            } else if (w[0] == "heroes") {
                std::vector<std::string> heroes;
                for (std::size_t i = 1; i < w.size(); ++i) heroes.push_back(speaker(w[i]));
                // What the session's own speakers did, in the world's words; the rest of the world's heroes counted.
                std::size_t others = 0;
                last_done = director->direct_heroes(heroes);
                for (const orchestrator::HeroDone& done : last_done) {
                    const auto alias = std::find_if(speakers.begin(), speakers.end(), [&](const auto& s) { return s.second == done.hero; });
                    if (alias == speakers.end())
                        ++others;
                    else
                        std::cout << "event " << alias->first << " acted: " << done.text << "\n";
                }
                if (others > 0) std::cout << "event " << others << (others == 1 ? " other hero acted" : " other heroes acted") << "\n";
            } else if (w[0] == "briefs") {
                // What a picker would be shown for each deed of the last `heroes` not yet taken, as text
                // (orchestrator::brief_text), for a measure of the picker to read.
                const std::vector<orchestrator::Event> events = director->events();
                for (const orchestrator::HeroDone& done : last_done) {
                    const auto e = std::find_if(events.begin(), events.end(), [&](const orchestrator::Event& one) { return one.id == done.event; });
                    if (e != events.end() && e->reaction.empty()) std::cout << orchestrator::brief_text(director->reaction_brief(done.hero, done.event));
                }
            } else if (w[0] == "react") {
                std::optional<std::string> only;
                std::optional<orchestrator::Reaction> as;
                if (w.size() > 1) only = speaker(w[1]);
                if (w.size() > 2) {
                    as = orchestrator::reaction_named(w[2]);
                    if (!as) throw Failure(where + "no reaction called " + w[2]);
                }
                std::size_t others = 0, took = 0;
                for (const orchestrator::HeroDone& done : last_done) {
                    if (only && done.hero != *only) continue;
                    const orchestrator::Reaction taken = director->react(done.hero, done.event, as);
                    ++took;
                    const auto alias = std::find_if(speakers.begin(), speakers.end(), [&](const auto& s) { return s.second == done.hero; });
                    if (alias == speakers.end())
                        ++others;
                    else
                        std::cout << "event " << alias->first << " took it: " << orchestrator::to_string(taken) << "\n";
                }
                if (only && took == 0) throw Failure(where + w[1] + " did nothing at the last heroes");
                if (others > 0) std::cout << "event " << others << (others == 1 ? " other hero took theirs" : " other heroes took theirs") << "\n";
            } else if (w[0] == "report") {
                need(3);
                const fwg::World& world = director->world();
                const std::optional<int> at = node_of(world, speaker(w[1]));
                std::string site;
                for (const fwg::NpcSite& s : world.npcs.sites)
                    if (at && s.node == *at && site.empty()) site = s.uid;
                if (site.empty()) throw Failure(where + w[1] + " stands at no roster site");
                const std::string by = w.size() > 3 ? (w[3] == "player" ? std::string("player") : speaker(w[3])) : std::string();
                director->report_event(site, w[2], by);
                std::cout << "event the threat where " << w[1] << " stands: " << w[2] << (w.size() > 3 ? " by " + w[3] : std::string()) << "\n";
            } else if (w[0] == "sql") {
                need(2);
                const orchestrator::QueryResult rows = director->query(row.substr(row.find("sql") + 3));
                for (const std::vector<orchestrator::Cell>& one : rows.rows) {
                    std::cout << "event sql:";
                    for (std::size_t c = 0; c < one.size(); ++c) {
                        std::cout << " " << rows.columns[c] << "=";
                        std::visit(
                            [](const auto& value) {
                                using T = std::decay_t<decltype(value)>;
                                if constexpr (std::is_same_v<T, std::monostate>) std::cout << "NULL";
                                else std::cout << value;
                            },
                            one[c]);
                    }
                    std::cout << "\n";
                }
            } else if (w[0] == "compact") {
                need(2);
                std::string outcomes;
                if (w.size() > 2) {
                    std::ifstream file(w[2], std::ios::binary);
                    if (!file) throw Failure(where + "cannot read " + w[2]);
                    outcomes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
                }
                const orchestrator::Compacted done = director->compact(speaker(w[1]), outcomes);
                std::cout << "event " << w[1] << " compacted: " << done.cleared << " exchanges into " << done.kept.size()
                          << " memories kept or brought up to date\n";
                for (const orchestrator::Remembered& r : done.kept)
                    std::cout << "event memory " << w[1] << ": " << r.memory << " " << r.act << (r.about.empty() ? "" : " (" + r.about + ")")
                              << " x" << r.times << (r.words.empty() ? "" : ": " + r.words) << " [" << r.source << "]\n";
                for (const std::string& why : done.refused) std::cout << "event refused " << w[1] << ": " << why << "\n";
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
