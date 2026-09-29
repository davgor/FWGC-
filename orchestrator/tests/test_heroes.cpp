// Heroes' deeds (contract version 26) against a real world: seed 99, size 33, a month after founding.
// The game calls direct_heroes; each hero who is due acts by their claim against what is near, how it
// goes is their chance against a draw from their seed and the day, the world records it through the
// generator's verb, and word of it goes out as an event. Every test that looks for something first
// checks the world holds it.
#include "orchestrator/director.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "hero_direction.hpp"
#include "fwg/api/hero_action.hpp"
#include "fwg/api/read.hpp"
#include "fwg/api/serialize.hpp"
#include "fwg/api/world_time.hpp"

namespace {

using orchestrator::ActKind;
using orchestrator::Director;
using orchestrator::Event;
using orchestrator::Fact;
using orchestrator::FactKind;
using orchestrator::FactPacket;
using orchestrator::HeroDone;

const fwg::World& month_on() {
    static const fwg::World world = [] {
        fwg::Request request;
        request.seed = 99;
        request.size = 33;
        Director director(fwg::generate(fwg::resolve(request)));
        director.advance(30.0);
        return director.world();
    }();
    return world;
}

std::string json_of(const fwg::World& world) {
    std::ostringstream out;
    fwg::write_world_json(out, world, /*pretty=*/false);
    return out.str();
}

const fwg::Hero& hero_in(const fwg::World& world, const std::string& uid) {
    for (const fwg::Hero& hero : world.heroes.people)
        if (hero.uid == uid) return hero;
    check::fail("a hero " + uid);
}

bool one_face(const fwg::Hero& hero) {
    const auto has = [&](const char* label) {
        return std::any_of(hero.faces.begin(), hero.faces.end(), [&](const fwg::HeroFace& f) { return f.label == label; });
    };
    return !(has("public") && has("true")) && !(has("calm") && has("unleashed")) && !(has("first") && has("second"));
}

// Where a site stands: a roster site's node, a ruin's, else the place the read names.
std::optional<int> node_of(const fwg::World& world, const std::string& site) {
    for (const fwg::NpcSite& row : world.npcs.sites)
        if (row.uid == site) return row.node;
    for (const fwg::Ruin& ruin : world.ruins)
        if (ruin.uid == site) return ruin.node;
    fwg::PlaceReadRequest request;
    request.place_id = site;
    try {
        return fwg::read_place(world, request).place.anchor.node;
    } catch (const fwg::WorldTimeError&) {
        return std::nullopt;
    }
}

// Where the world's reads have a person stand: their anchor's node.
std::optional<int> anchor_of(const fwg::World& world, const std::string& uid) {
    fwg::PersonReadRequest who;
    who.uid = uid;
    return fwg::read_person(world, who).person.anchor.node;
}

// The worst nest within 5 km of `node`, the nearest of equals: the oracle, by the near read itself.
std::optional<fwg::NearNestRow> worst_near(const fwg::World& world, int node) {
    fwg::NearReadRequest near;
    near.node = node;
    near.radius_m = 5000.0;
    near.categories = std::vector<std::string>{"beast_nests"};
    std::optional<fwg::NearNestRow> best;
    for (const fwg::NearNestRow& nest : fwg::read_near(world, near).near.beast_nests)
        if (!best || nest.tier > best->tier || (nest.tier == best->tier && nest.distance_m < best->distance_m)) best = nest;
    return best;
}

// Four weeks of the game calling for heroes once a day.
struct Weeks {
    Director director;
    std::vector<HeroDone> done;
    std::vector<std::vector<HeroDone>> by_call;
};

Weeks four_weeks() {
    Weeks out{Director(month_on()), {}, {}};
    for (int day = 0; day < 28; ++day) {
        if (day > 0) out.director.advance(1.0);
        std::vector<HeroDone> called = out.director.direct_heroes({});
        out.done.insert(out.done.end(), called.begin(), called.end());
        out.by_call.push_back(std::move(called));
    }
    return out;
}

const Weeks& weeks() {
    static const Weeks out = four_weeks();
    return out;
}

}  // namespace

// Who acts, and what they do: a living hero of one face whose claim is to hold, protect, kill or retake,
// and by that claim: a place held defended against the worst nest within 5 km of it (never with nothing
// there), a nest hunted, a ruin assaulted. The world keeps each as the hero's deed and log line.
TEST(a_hero_acts_by_their_claim_against_what_is_near) {
    const Weeks& w = weeks();
    check::is_true(!w.done.empty(), "the premise: heroes acted in four weeks");
    std::map<std::string, std::size_t> by_action;
    std::size_t apart = 0;  // deeds where the hero stands elsewhere than what they hold
    std::set<std::string> took;  // heroes who took the ruin they would retake
    for (const HeroDone& done : w.done) {
        const fwg::Hero& hero = hero_in(month_on(), done.hero);
        check::is_true(hero.status == "living" && one_face(hero), "a living hero of one face: " + done.hero);
        const std::string& verb = hero.claim.verb;
        ++by_action[done.action];
        if (verb == "hold" || verb == "protect") {
            check::equal(done.action, std::string("defended"), "holding, they defend: " + done.hero);
            const bool stands = hero.presence && (hero.presence->site_kind == "city" || hero.presence->site_kind == "hamlet" ||
                                                  hero.presence->site_kind == "fortress" || hero.presence->site_kind == "ruin");
            check::equal(done.place, stands ? hero.presence->uid : hero.home->uid, "where they stand, else home: " + done.hero);
            const std::optional<int> node = node_of(month_on(), done.place);
            check::is_true(node.has_value(), "a place the world names: " + done.place);
            // The threat near where the world has them stand (their anchor, as the reads give it), else near
            // what they hold.
            const std::optional<int> anchor = anchor_of(month_on(), done.hero);
            const std::optional<fwg::NearNestRow> nest = worst_near(month_on(), anchor ? *anchor : *node);
            check::is_true(nest.has_value() && nest->id == done.against, "the worst nest within 5 km of where they stand: " + done.against);
            if (anchor && *anchor != *node) ++apart;
        } else if (verb == "kill") {
            check::is_true(done.action == "hunted" && done.against == hero.claim.target_uid, "sworn to kill, they hunt it: " + done.hero);
        } else if (took.count(done.hero)) {
            // A ruin taken is theirs to hold: defended from then on, where they stand now.
            check::is_true(verb == "retake" && done.action == "defended" && done.place == hero.claim.target_uid,
                           "a ruin retaken, they hold it: " + done.hero);
        } else {
            check::is_true(verb == "retake" && done.action == "assaulted" && done.place == hero.claim.target_uid,
                           "to retake a ruin, they assault it: " + done.hero);
            if (done.outcome == "won") took.insert(done.hero);
        }
        check::is_true(done.outcome == "won" || done.outcome == "lost", "won or lost");
        // The world keeps it: their deed and their log's line.
        const fwg::Hero& now = hero_in(w.director.world(), done.hero);
        check::is_true(std::any_of(now.log.begin(), now.log.end(), [&](const fwg::HeroLogEntry& l) { return l.text == done.text; }),
                       "the log keeps it: " + done.text);
    }
    check::is_true(by_action["defended"] > 0, "the premise: someone defended a place");
    // A ruin taken is where they stand after, warding it.
    check::is_true(!took.empty(), "the premise: a pretender took the ruin they would retake");
    for (const std::string& uid : took) {
        const fwg::Hero& now = hero_in(w.director.world(), uid);
        check::is_true(now.presence && now.presence->uid == now.claim.target_uid && now.presence->situation == "warding",
                       "at the ruin taken, warding it: " + uid);
    }
    // One fight at a place: heroes defending it against one foe on one call share how it went.
    std::size_t shared = 0;
    for (const std::vector<HeroDone>& call : w.by_call) {
        std::map<std::string, std::string> fights;
        for (const HeroDone& done : call) {
            if (done.action != "defended") continue;
            const auto [at, fresh] = fights.emplace(done.place + "|" + done.against, done.outcome);
            if (!fresh) {
                ++shared;
                check::equal(done.outcome, at->second, "one fight, one outcome, at " + done.place);
            }
        }
    }
    check::is_true(shared > 0, "the premise: two heroes defended one place against one foe on one call");
    check::is_true(apart > 0, "the premise: a hero who stands elsewhere than what they defend, a castellan in their city");
    // Nobody with nothing near acts: every hero holding a place with no nest within 5 km of it stays still.
    std::size_t quiet = 0;
    for (const fwg::Hero& hero : month_on().heroes.people) {
        if (hero.status != "living" || !one_face(hero) || (hero.claim.verb != "hold" && hero.claim.verb != "protect")) continue;
        const std::optional<int> node = anchor_of(month_on(), hero.uid);
        if (!node || worst_near(month_on(), *node)) continue;
        ++quiet;
        check::is_true(std::none_of(w.done.begin(), w.done.end(), [&](const HeroDone& d) { return d.hero == hero.uid; }),
                       "nothing near, nothing done: " + hero.uid);
    }
    (void)quiet;  // a world where every holder has a nest near proves the rest, not this
    // And every one with a nest near who holds a place the world can find acts in four weeks: a domain holder
    // warding a ruin among them, whose ruin no roster site names.
    std::size_t holders = 0, at_ruins = 0;
    for (const fwg::Hero& hero : month_on().heroes.people) {
        if (hero.status != "living" || !one_face(hero) || (hero.claim.verb != "hold" && hero.claim.verb != "protect")) continue;
        const bool placed = hero.presence && (hero.presence->site_kind == "city" || hero.presence->site_kind == "hamlet" ||
                                              hero.presence->site_kind == "fortress" || hero.presence->site_kind == "ruin");
        const std::string site = placed ? hero.presence->uid : hero.home ? hero.home->uid : std::string();
        const std::optional<int> anchor = anchor_of(month_on(), hero.uid);
        if (site.empty() || !node_of(month_on(), site) || !anchor || !worst_near(month_on(), *anchor)) continue;
        ++holders;
        at_ruins += placed && hero.presence->site_kind == "ruin" ? 1 : 0;
        check::is_true(std::any_of(w.done.begin(), w.done.end(), [&](const HeroDone& d) { return d.hero == hero.uid; }),
                       "a threat near, they act: " + hero.uid);
    }
    check::is_true(holders > 0 && at_ruins > 0, "the premise: holders with a threat near, one warding a ruin");
}

// A hero acts at most once a call and a week after they last did; heroes named are the only ones who act;
// a uid that names no hero is an error.
TEST(a_hero_acts_once_a_week_and_only_when_called) {
    const Weeks& w = weeks();
    for (const std::vector<HeroDone>& call : w.by_call) {
        std::set<std::string> once;
        for (const HeroDone& done : call) check::is_true(once.insert(done.hero).second, "once a call: " + done.hero);
    }
    std::map<std::string, std::vector<double>> days;
    for (const HeroDone& done : w.done) days[done.hero].push_back(done.day);
    std::size_t again = 0;
    for (const auto& [hero, when] : days)
        for (std::size_t i = 1; i < when.size(); ++i) {
            check::is_true(when[i] - when[i - 1] >= 7.0 - 1e-9, "a week between: " + hero);
            ++again;
        }
    check::is_true(again > 0, "the premise: heroes acted again after a week");
    // The first acts fall on different days: not everyone at the first call.
    check::is_true(w.by_call.front().size() < days.size(), "staggered: " + std::to_string(w.by_call.front().size()) + " of " +
                                                                std::to_string(days.size()) + " on the first call");
    // Named, only them.
    Director d(month_on());
    const std::string one = w.done.front().hero;
    for (int day = 0; day < 8; ++day) {
        if (day > 0) d.advance(1.0);
        for (const HeroDone& done : d.direct_heroes({one})) check::equal(done.hero, one, "only the hero named");
    }
    check::throws([&] { (void)d.direct_heroes({"hero-nobody"}); }, "a uid that names no hero");
}

// A hero whose own work the player holds waits on it: that work is their fight, and the game says how it went.
// Drawn apart, a castellan held their fortress against the basilisks on the day the player, holding it with
// them, came back to say it was done, and the one fight was told twice (2026-09-29). Waiting keeps their day.
TEST(a_hero_whose_work_the_player_holds_waits_on_it) {
    const Weeks& w = weeks();
    // The premise: a hero who first acted after the first call of a week called each day, and gives work of
    // their own; one who acted at the first call is due again at once however their day is kept.
    Director probe(month_on());
    std::string giver, quest;
    std::size_t acted = 0;
    for (std::size_t k = 1; k < 7 && giver.empty(); ++k)
        for (const HeroDone& one : w.by_call[k]) {
            const FactPacket asked = probe.respond(one.hero, {ActKind::AskForWork, ""});
            for (const Fact& f : asked.facts)
                if (giver.empty() && f.kind == FactKind::QuestOffer && f.field("from_speaker") && *f.field("from_speaker") == "yes") {
                    giver = one.hero;
                    quest = f.subject;
                    acted = k;
                }
            if (!giver.empty()) break;
        }
    check::is_true(!giver.empty(), "the premise: a hero who first acted after the first day gives work of their own");
    if (giver.empty()) return;
    Director d(month_on());
    (void)d.respond(giver, {ActKind::AskForWork, ""});
    check::is_true(d.respond(giver, {ActKind::AcceptQuest, quest}).outcome == orchestrator::Outcome::Accepted, "the work taken");
    for (std::size_t k = 0; k <= acted; ++k) {
        if (k > 0) d.advance(1.0);
        for (const HeroDone& one : d.direct_heroes({}))
            check::is_true(one.hero != giver, "waits on the work the player holds, the day they would have acted: " + std::to_string(k));
    }
    // Once the game says how it went, they act at the next call, due since.
    d.resolve(quest, true);
    d.advance(1.0);
    const std::vector<HeroDone> after = d.direct_heroes({});
    check::is_true(std::any_of(after.begin(), after.end(), [&](const HeroDone& one) { return one.hero == giver; }),
                   "acts at the next call once the work is done");
}

// After a gap longer than a week every hero is due at once, more than the world's verb takes in one call:
// all of them act, each once, and each has their event.
TEST(every_hero_due_acts_though_more_than_one_call_of_the_verb_holds) {
    Director d(month_on());
    (void)d.direct_heroes({});
    d.advance(8.0);
    const std::size_t events_before = d.events().size();
    const std::vector<HeroDone> done = d.direct_heroes({});
    check::is_true(done.size() > fwg::kMaxHeroActions, "the premise: more due than one call of the verb holds: " + std::to_string(done.size()));
    std::set<std::string> heroes;
    for (const HeroDone& one : done) check::is_true(heroes.insert(one.hero).second, "each once: " + one.hero);
    check::equal(d.events().size(), events_before + done.size(), "an event each");
    for (const HeroDone& one : done) {
        const fwg::Hero& now = hero_in(d.world(), one.hero);
        check::is_true(std::any_of(now.log.begin(), now.log.end(), [&](const fwg::HeroLogEntry& l) { return l.text == one.text; }),
                       "the world keeps it: " + one.hero);
    }
}

// The chance, as README.md says it, and the draw it is set against: the same for the same hero, day and
// deed, another for another day, and even over many.
TEST(a_heros_chance_is_their_band_against_the_danger_and_the_draw_is_even) {
    using orchestrator::direction::hero_chance;
    using orchestrator::direction::hero_draw;
    const auto near = [](double a, double b) { return std::abs(a - b) < 1e-9; };
    check::is_true(near(hero_chance("notable", 3, false), 0.45), "notable");
    check::is_true(near(hero_chance("renowned", 3, false), 0.60), "renowned");
    check::is_true(near(hero_chance("legendary", 3, false), 0.75), "legendary");
    check::is_true(near(hero_chance("notable", 5, false), 0.29), "a worse danger, less");
    check::is_true(near(hero_chance("notable", 1, false), 0.61), "a lesser one, more");
    check::is_true(near(hero_chance("renowned", 4, true), 0.37), "an assault, 15 less");
    check::is_true(near(hero_chance("legendary", 1, false), 0.9), "never past 90%");
    check::is_true(near(hero_chance("notable", 5, true), 0.14), "the least it can be");
    check::equal(hero_draw(12345, 40.25, "defended"), hero_draw(12345, 40.75, "defended"), "the same day draws the same");
    check::is_true(hero_draw(12345, 40.25, "defended") != hero_draw(12345, 41.25, "defended"), "another day, another draw");
    check::is_true(hero_draw(12345, 40.25, "defended") != hero_draw(12345, 40.25, "hunted"), "another deed, another draw");
    double sum = 0.0;
    std::size_t below = 0;
    const std::uint32_t n = 20000;
    for (std::uint32_t i = 0; i < n; ++i) {
        const double draw = hero_draw(i * 2654435761u, 30.0 + static_cast<double>(i % 97), "defended");
        check::is_true(draw >= 0.0 && draw < 1.0, "in [0, 1)");
        sum += draw;
        below += draw < 0.29 ? 1 : 0;
    }
    check::is_true(std::abs(sum / n - 0.5) < 0.01, "even: a mean near a half");
    check::is_true(std::abs(static_cast<double>(below) / n - 0.29) < 0.015, "a chance of 29% comes up 29% of the time");
}

// How it goes follows the chance: of the heroes' deeds, those with the better chance (their fame's band
// against the danger) go their way more often, and both outcomes happen.
TEST(a_heros_deed_goes_their_way_more_often_the_better_their_chance) {
    const Weeks& w = weeks();
    std::size_t won = 0, lost = 0, strong = 0, strong_won = 0, weak = 0, weak_won = 0;
    for (const HeroDone& done : w.done) {
        (done.outcome == "won" ? won : lost) += 1;
        const fwg::Hero& hero = hero_in(month_on(), done.hero);
        int danger = 3;
        for (const fwg::NestSite& nest : month_on().beast_nests.sites)
            if (nest.id == done.against) danger = nest.tier;
        const double band = hero.tier == "legendary" ? 0.75 : hero.tier == "renowned" ? 0.6 : 0.45;
        const double chance = std::clamp(band - 0.08 * (danger - 3) - (done.action == "assaulted" ? 0.15 : 0.0), 0.1, 0.9);
        if (chance >= 0.5) {
            ++strong;
            strong_won += done.outcome == "won" ? 1 : 0;
        } else {
            ++weak;
            weak_won += done.outcome == "won" ? 1 : 0;
        }
    }
    check::is_true(won > 0 && lost > 0, "both outcomes: " + std::to_string(won) + " won, " + std::to_string(lost) + " lost");
    check::is_true(strong >= 10 && weak >= 10, "the premise: ten deeds or more each side of an even chance");
    const double strong_rate = static_cast<double>(strong_won) / static_cast<double>(strong);
    const double weak_rate = static_cast<double>(weak_won) / static_cast<double>(weak);
    check::is_true(strong_rate > weak_rate + 0.1,
                   "the better chance wins more: " + std::to_string(strong_rate) + " against " + std::to_string(weak_rate));
}

// Word of it: the hero knows it at once; at the next share, whoever stands where it happened hears it, and
// so do the hero's rivals, kin and allies wherever they are; someone far off and untied does not.
TEST(word_of_a_heros_deed_is_heard_where_it_was_and_by_their_ties) {
    Director d(month_on());
    std::vector<HeroDone> done;
    for (int day = 0; day < 8 && done.empty(); ++day) {
        if (day > 0) d.advance(1.0);
        for (const HeroDone& one : d.direct_heroes({}))
            if (!hero_in(month_on(), one.hero).allies.empty() || !hero_in(month_on(), one.hero).kin.empty() ||
                !hero_in(month_on(), one.hero).rivals.empty())
                done.push_back(one);
    }
    check::is_true(!done.empty(), "the premise: a hero with ties acted");
    const HeroDone& deed = done.front();
    const fwg::Hero& hero = hero_in(month_on(), deed.hero);
    std::vector<Event> events = d.events();
    const auto event = std::find_if(events.begin(), events.end(), [&](const Event& e) { return e.id == deed.event; });
    check::is_true(event != events.end() && event->kind == "hero" && event->person == deed.hero && event->verb == deed.action &&
                       event->state == deed.outcome && event->against == deed.against,
                   "the event says it");
    check::is_true(!event->against_name.empty() || deed.against.empty(), "and what they faced, by name");
    const orchestrator::Related before = d.related(deed.event);
    check::is_true(std::find(before.knows.begin(), before.knows.end(), deed.hero) != before.knows.end(), "the hero knows at once");
    std::vector<std::string> tied(hero.allies.begin(), hero.allies.end());
    tied.insert(tied.end(), hero.kin.begin(), hero.kin.end());
    tied.insert(tied.end(), hero.rivals.begin(), hero.rivals.end());
    (void)d.share({});
    const orchestrator::Related after = d.related(deed.event);
    for (const std::string& uid : tied) {
        fwg::PersonReadRequest request;
        request.uid = uid;
        bool present = true;
        try {
            present = fwg::read_person(d.world(), request).person.liveness == fwg::Liveness::Present;
        } catch (const fwg::WorldTimeError&) {
            present = false;
        }
        if (present)
            check::is_true(std::find(after.knows.begin(), after.knows.end(), uid) != after.knows.end(), "their tie hears: " + uid);
    }
    check::is_true(after.knows.size() > before.knows.size(), "people hear of it at the share");
    // The same day, word has gone nowhere yet: someone standing elsewhere and untied is not told.
    std::size_t untold = 0;
    for (const fwg::NpcPerson& p : d.world().npcs.people) {
        if (p.is_cast || !p.alive || !p.site_uid || !event->node) continue;
        const std::optional<int> there = node_of(d.world(), *p.site_uid);
        if (!there || *there == *event->node) continue;
        check::is_true(std::find(after.knows.begin(), after.knows.end(), p.uid) == after.knows.end(), "elsewhere and untied: " + p.uid);
        if (++untold >= 3) break;
    }
    check::is_true(untold > 0, "the premise: someone standing elsewhere");
    // Two days on, word has travelled: someone at a place within 20 km of it, standing elsewhere, has heard.
    d.advance(2.0);
    (void)d.share({});
    const orchestrator::Related later = d.related(deed.event);
    fwg::NearReadRequest near;
    near.node = *event->node;
    near.radius_m = std::min(20000.0, d.world().effective.globe_radius * fwg::kNearRadiusFraction);
    near.categories = std::vector<std::string>{"settlements", "hamlets", "fortresses"};
    const fwg::NearRead read = fwg::read_near(d.world(), near);
    std::set<int> within;
    for (const auto& r : read.near.settlements) within.insert(r.node);
    for (const auto& r : read.near.hamlets) within.insert(r.node);
    for (const auto& r : read.near.fortresses) within.insert(r.node);
    within.erase(*event->node);
    std::size_t travelled = 0;
    for (const fwg::NpcPerson& p : d.world().npcs.people) {
        if (p.is_cast || !p.alive || !p.site_uid || p.presence_node) continue;
        const std::optional<int> there = node_of(d.world(), *p.site_uid);
        if (!there || !within.count(*there) || std::find(tied.begin(), tied.end(), p.uid) != tied.end()) continue;
        check::is_true(std::find(later.knows.begin(), later.knows.end(), p.uid) != later.knows.end(), "word travelled to " + p.uid);
        if (++travelled >= 3) break;
    }
    check::is_true(travelled > 0, "the premise: someone within 20 km of it, standing elsewhere");
}

// The hero's own side: their greeting, small talk and answer about themself carry their newest deed, as they
// know it, being there; not someone else's.
TEST(a_hero_tells_their_own_deed) {
    Director d(month_on());
    std::vector<HeroDone> done;
    for (int day = 0; day < 8 && done.empty(); ++day) {
        if (day > 0) d.advance(1.0);
        done = d.direct_heroes({});
    }
    // A deed where the world has the hero stand (their anchor, as the reads have it: a castellan's is not
    // always their fortress), so small talk's news of that place is this deed too, and is not carried twice.
    const std::vector<Event> events = d.events();
    const auto defended = std::find_if(done.begin(), done.end(), [&](const HeroDone& one) {
        fwg::PersonReadRequest who;
        who.uid = one.hero;
        const std::optional<int> stands = fwg::read_person(d.world(), who).person.anchor.node;
        const auto e = std::find_if(events.begin(), events.end(), [&](const Event& ev) { return ev.id == one.event; });
        return e != events.end() && e->node && stands == e->node;
    });
    check::is_true(defended != done.end(), "the premise: a hero whose deed was where they stand");
    if (defended == done.end()) return;
    const HeroDone& deed = *defended;
    // Another who acted at that call, met at once, the fight just before the meeting.
    const auto at_once = std::find_if(done.begin(), done.end(), [&](const HeroDone& one) { return one.hero != deed.hero; });
    check::is_true(at_once != done.end(), "the premise: another hero acted at that call");
    if (at_once == done.end()) return;
    (void)d.respond(at_once->hero, {ActKind::Greet, ""});
    (void)d.respond(at_once->hero, {ActKind::Farewell, ""});
    // Met first the day after, any deed of theirs is theirs to tell.
    d.advance(1.0);
    for (const ActKind kind : {ActKind::Greet, ActKind::SmallTalk, ActKind::AskAboutThem}) {
        const FactPacket p = d.respond(deed.hero, {kind, ""});
        const auto own = std::find_if(p.facts.begin(), p.facts.end(),
                                      [&](const Fact& f) { return f.kind == FactKind::News && f.subject == deed.event; });
        check::is_true(own != p.facts.end(), "their own deed, in a " + std::string(orchestrator::to_string(kind)));
        if (own == p.facts.end()) continue;
        check::equal(own->field("event") ? *own->field("event") : std::string(), std::string("hero"), "a hero's deed");
        check::equal(own->field("how") ? *own->field("how") : std::string(), std::string("there"), "known by being there");
        check::equal(own->field("person") ? *own->field("person") : std::string(), p.persona.name, "their own");
        check::equal(std::count_if(p.facts.begin(), p.facts.end(), [&](const Fact& f) { return f.subject == deed.event; }),
                     std::ptrdiff_t{1}, "once");
    }
    // Their work finished by the player at that first meeting, a day after the fight, newer news of them: still
    // the fight is their own deed to tell, never the work.
    std::string giver, quest;
    for (const HeroDone& one : done) {
        const FactPacket asked = d.respond(one.hero, {ActKind::AskForWork, ""});
        for (const Fact& f : asked.facts)
            if (giver.empty() && f.kind == FactKind::QuestOffer && f.field("from_speaker") && *f.field("from_speaker") == "yes") {
                giver = one.hero;
                quest = f.subject;
            }
        if (!giver.empty()) break;
    }
    if (!giver.empty()) {
        std::string fight;
        for (const HeroDone& one : done)
            if (one.hero == giver) fight = one.event;
        (void)d.respond(giver, {ActKind::AcceptQuest, quest});
        d.resolve(quest, true);
        const FactPacket p = d.respond(giver, {ActKind::SmallTalk, ""});
        check::is_true(std::any_of(p.facts.begin(), p.facts.end(), [&](const Fact& f) { return f.kind == FactKind::News && f.subject == fight; }),
                       "their fight, though their work is newer news");
    }
    check::is_true(!giver.empty(), "the premise: a hero who acted gives work of their own");
    // Met again, a deed from before their last meeting was theirs to tell then (contract version 27): told at a
    // second visit, a fight lost before the first read as the fight the player had just won beside them. A
    // deed since then rides.
    d.advance(1.0);
    const FactPacket later = d.respond(deed.hero, {ActKind::Greet, ""});
    check::is_true(std::none_of(later.facts.begin(), later.facts.end(), [&](const Fact& f) { return f.subject == deed.event; }),
                   "a deed from before the last meeting: not again");
    const FactPacket again = d.respond(at_once->hero, {ActKind::Greet, ""});
    check::is_true(std::none_of(again.facts.begin(), again.facts.end(), [&](const Fact& f) { return f.subject == at_once->event; }),
                   "a deed the moment before the last meeting: not again");
    (void)d.respond(deed.hero, {ActKind::Farewell, ""});
    std::string next;
    for (int day = 0; day < 8 && next.empty(); ++day) {
        d.advance(1.0);
        for (const HeroDone& one : d.direct_heroes({deed.hero})) next = one.event;
    }
    check::is_true(!next.empty(), "the premise: they act again within the week");
    const FactPacket since = d.respond(deed.hero, {ActKind::Greet, ""});
    check::is_true(std::any_of(since.facts.begin(), since.facts.end(), [&](const Fact& f) { return f.kind == FactKind::News && f.subject == next; }),
                   "a deed since the last meeting rides");
    // Replayed, when each last met the stranger is folded again from the inputs: the same deeds ride.
    Director replayed = orchestrator::replay(month_on(), d.inputs());
    const auto news_in = [](const FactPacket& p) {
        std::vector<std::string> out;
        for (const Fact& f : p.facts)
            if (f.kind == FactKind::News) out.push_back(f.subject);
        return out;
    };
    for (const std::string& who : {deed.hero, at_once->hero})
        check::is_true(news_in(d.respond(who, {ActKind::SmallTalk, ""})) == news_in(replayed.respond(who, {ActKind::SmallTalk, ""})),
                       "replayed, the same news rides: " + who);
    // Another hero's deed is never theirs to tell as their own.
    for (const HeroDone& other : done)
        if (other.hero != deed.hero) {
            const FactPacket p = d.respond(deed.hero, {ActKind::Greet, ""});
            check::is_true(std::none_of(p.facts.begin(), p.facts.end(), [&](const Fact& f) { return f.subject == other.event; }),
                           "not another's deed: " + other.hero);
            break;
        }
}

// Heroes who fought one fight were there for each other's part of it (contract version 28): none hears another's
// as word. Heard as word, a castellan's own lost fight came back to him as a stranger's rumour (the golden
// benchmark's held-out eighth session, 2026-09-29). Heroes who fought apart were not.
TEST(heroes_in_one_fight_were_there_for_each_others_part) {
    const Weeks& w = weeks();
    const auto how = [&](const std::string& person, const std::string& event) {
        const orchestrator::QueryResult rows =
            w.director.query("SELECT how FROM facts WHERE person = '" + person + "' AND event = '" + event + "'");
        return rows.rows.empty() ? std::string() : std::get<std::string>(rows.rows.front().front());
    };
    std::size_t together = 0, apart = 0;
    for (const std::vector<HeroDone>& call : w.by_call)
        for (std::size_t i = 0; i < call.size(); ++i)
            for (std::size_t j = 0; j < call.size(); ++j) {
                if (i == j) continue;
                const HeroDone& one = call[i];
                const HeroDone& other = call[j];
                if (one.action == "defended" && other.action == "defended" && one.place == other.place && one.against == other.against) {
                    ++together;
                    check::equal(how(one.hero, other.event), std::string("there"), one.hero + " was there for " + other.hero + "'s part");
                } else if (one.place != other.place) {
                    ++apart;
                    check::is_true(how(one.hero, other.event) != "there", one.hero + " was not there for " + other.hero + "'s fight elsewhere");
                }
            }
    check::is_true(together > 0, "the premise: two heroes fought one fight");
    check::is_true(apart > 0, "the premise: heroes fought apart on one call");
}

// A hero's deed told to someone they are tied to names the tie (contract version 28): their sovereign, or a
// rival, kin or ally of theirs. Passed on with no tie, a sovereign's lost fight in the hearer's own town was
// hearsay he would not vouch for (the golden benchmark's round 30). Told to a stranger, none.
TEST(news_of_a_heros_deed_says_how_they_stand_to_the_hearer) {
    Director d(month_on());
    std::vector<HeroDone> done;
    for (int day = 0; day < 8; ++day) {
        if (day > 0) d.advance(1.0);
        for (const HeroDone& one : d.direct_heroes({})) done.push_back(one);
    }
    (void)d.share({});
    // How a hero stands to another by the world's own records: the sovereign of their realm first (a castellan's
    // liege is theirs), then a rival, kin or ally.
    const auto sovereign_of = [&](const fwg::Hero& h) -> std::string {
        if (h.realm_uid.empty()) return {};
        for (const fwg::HeroRealm& realm : d.world().heroes.realms)
            if (realm.uid == h.realm_uid) return realm.sovereign_uid;
        return {};
    };
    const auto oracle = [&](const fwg::Hero& hearer, const std::string& hero) -> std::string {
        if (hearer.liege_uid == hero || sovereign_of(hearer) == hero) return "sovereign";
        for (const auto& [tie, list] : {std::pair<std::string, const std::vector<std::string>*>{"rival", &hearer.rivals},
                                        {"kin", &hearer.kin},
                                        {"ally", &hearer.allies}})
            if (std::find(list->begin(), list->end(), hero) != list->end()) return tie;
        return {};
    };
    std::set<std::string> ties;
    std::size_t strangers = 0;
    for (const HeroDone& deed : done) {
        const fwg::Hero& hero = hero_in(d.world(), deed.hero);
        if (!one_face(hero)) continue;
        for (const std::string& uid : d.related(deed.event).knows) {
            if (uid == deed.hero) continue;
            const auto hearer = std::find_if(d.world().heroes.people.begin(), d.world().heroes.people.end(),
                                             [&](const fwg::Hero& h) { return h.uid == uid; });
            if (hearer == d.world().heroes.people.end() || !one_face(*hearer) || hearer->status == "legend") continue;
            const std::string expected = oracle(*hearer, deed.hero);
            if (expected.empty() && strangers >= 3) continue;
            const FactPacket p = d.respond(uid, {ActKind::AskAboutPerson, deed.hero});
            for (const Fact& f : p.facts)
                if (f.kind == FactKind::News && f.subject == deed.event) {
                    const std::string* tie = f.field("tie");
                    // The oracle knows ties by the records alone; a stranger by them may still share a realm
                    // whose sovereign the deed's hero is.
                    if (!expected.empty()) {
                        check::equal(tie ? *tie : std::string(), expected, "the tie of " + uid + " to " + deed.hero);
                        ties.insert(expected);
                    } else {
                        check::is_true(tie == nullptr || (*tie == "sovereign" && hero.role == "sovereign"),
                                       "no tie by the records, none said: " + uid + " to " + deed.hero + ": " + (tie ? *tie : std::string()));
                        if (tie == nullptr) ++strangers;
                    }
                }
        }
    }
    check::is_true(!ties.empty(), "the premise: a deed told to someone tied to its hero");
    check::is_true(strangers > 0, "the premise: a deed told to a stranger");
}

// What someone who heard says: a News fact of event "hero", the hero by name, what they did, how it went,
// against what; and replay makes the same world and the same events.
TEST(news_of_a_heros_deed_and_replay) {
    Director d(month_on());
    std::vector<HeroDone> done;
    for (int day = 0; day < 8 && done.empty(); ++day) {
        if (day > 0) d.advance(1.0);
        for (const HeroDone& one : d.direct_heroes({}))
            if (one.action == "defended") done.push_back(one);
    }
    check::is_true(!done.empty(), "the premise: a place defended");
    (void)d.share({});
    // A place people live: someone of the roster there heard it.
    const auto lived_in = std::find_if(done.begin(), done.end(), [&](const HeroDone& one) {
        for (const std::string& uid : d.related(one.event).knows)
            for (const fwg::NpcPerson& p : d.world().npcs.people)
                if (p.uid == uid && !p.is_cast && p.alive) return true;
        return false;
    });
    check::is_true(lived_in != done.end(), "the premise: a place defended where people live");
    if (lived_in == done.end()) return;
    const HeroDone& deed = *lived_in;
    // Someone who stands where it happened, asked for small talk, passes it on.
    const std::vector<Event> events = d.events();
    const auto event = std::find_if(events.begin(), events.end(), [&](const Event& e) { return e.id == deed.event; });
    check::is_true(event != events.end() && event->node.has_value(), "the event, where it was");
    std::string local;
    const orchestrator::Related knows = d.related(deed.event);
    for (const std::string& uid : knows.knows)
        for (const fwg::NpcPerson& p : d.world().npcs.people)
            if (local.empty() && p.uid == uid && !p.is_cast && p.alive) local = uid;
    check::is_true(!local.empty(), "the premise: someone who stands there heard it");
    const FactPacket chat = d.respond(local, {ActKind::SmallTalk, ""});
    bool said = false;
    for (const Fact& f : chat.facts)
        if (f.kind == FactKind::News && f.subject == deed.event) {
            said = true;
            check::equal(f.field("event") ? *f.field("event") : std::string(), std::string("hero"), "a hero's deed");
            check::equal(f.field("state") ? *f.field("state") : std::string(), deed.outcome, "how it went");
            check::equal(f.field("verb") ? *f.field("verb") : std::string(), deed.action, "what they did");
            check::is_true(f.field("person") && !f.field("person")->empty(), "who, by name");
            check::equal(f.field("against") ? *f.field("against") : std::string(), event->against_name, "against what, by name");
            check::equal(f.field("player") ? *f.field("player") : std::string(), std::string("no"), "not the player's");
        }
    check::is_true(said, "small talk there carries it");
    // Replayed: the same world, the same events.
    Director again = orchestrator::replay(month_on(), d.inputs());
    check::equal(json_of(again.world()), json_of(d.world()), "the same world");
    const std::vector<Event> replayed = again.events();
    check::equal(replayed.size(), events.size(), "the same events");
    for (std::size_t i = 0; i < events.size() && i < replayed.size(); ++i)
        check::is_true(replayed[i].id == events[i].id && replayed[i].state == events[i].state && replayed[i].against == events[i].against,
                       "event " + events[i].id);
}
