// The hero-action API (heroes version 5): what a hero did after the world was made, recorded as a
// deed and a line of their log, their fame and band recomputed by generation's own formula, and
// nothing else touched.
#include "check.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "fwg/hero_action.hpp"
#include "fwg/heroes.hpp"
#include "fwg/serialize.hpp"
#include "fwg/story_web.hpp"
#include "fwg/world_time.hpp"

using namespace fwg;

namespace {

World generated(std::uint32_t seed) {
    Request r;
    r.seed = seed;
    r.size = 33;
    return generate(resolve(r));
}

const World& genesis() {
    static const World world = generated(42);
    return world;
}

std::string json_of(const World& world) {
    std::ostringstream out;
    write_world_json(out, world, /*pretty=*/false);
    return out.str();
}

std::string home_class(const World& world, const Hero& hero) {
    if (!hero.home) return "small";
    for (const auto& site : world.settlements.sites)
        if (site.uid == hero.home->uid)
            return site.city_class == CityClass::Capital ? "capital" : site.city_class == CityClass::Medium ? "medium" : "small";
    return "small";
}

std::size_t realm_cities(const World& world, const Hero& hero) {
    for (const HeroRealm& realm : world.heroes.realms)
        if (realm.uid == hero.realm_uid) return realm.city_uids.size();
    return 1;
}

// The first living castellan with a single deed, standing at their fortress.
const Hero& a_castellan(const World& world) {
    for (const Hero& hero : world.heroes.people)
        if (hero.role == "castellan" && hero.status == "living" && hero.deeds.size() == 1 && hero.presence &&
            hero.presence->site_kind == "fortress")
            return hero;
    check::fail("the premise: a living castellan at their fortress, with one deed");
}

const NestSite& a_nest(const World& world, int tier) {
    for (const NestSite& nest : world.beast_nests.sites)
        if (nest.tier == tier) return nest;
    check::fail("the premise: a nest of tier " + std::to_string(tier));
}

const Hero& hero_in(const World& world, const std::string& uid) {
    for (const Hero& hero : world.heroes.people)
        if (hero.uid == uid) return hero;
    check::fail("a hero " + uid);
}

HeroActionEntry defended(const Hero& hero, const std::string& against, const std::string& outcome, double day) {
    HeroActionEntry entry;
    entry.uid = hero.uid;
    entry.action = "defended";
    entry.outcome = outcome;
    entry.place_uid = hero.presence->uid;
    entry.against_uid = against;
    entry.day = day;
    return entry;
}

HeroActionResult act(const World& world, std::vector<HeroActionEntry> entries) {
    HeroActionRequest request;
    request.actions = std::move(entries);
    return hero_action_request(world, request);
}

std::size_t known_count(const Hero& hero) {
    return static_cast<std::size_t>(std::count_if(hero.deeds.begin(), hero.deeds.end(), [](const HeroDeed& d) { return d.known_for; }));
}

}  // namespace

// The formula a runtime change recomputes fame with is generation's: for every record of two worlds as
// generated, hero_fame_of gives the record's own fame, hero_fame_band its band, and hero_mark_deeds
// leaves every deed's scope and the one known for as they were.
TEST(hero_action_every_record_as_generated_has_the_fame_the_formula_gives) {
    std::size_t homed = 0, homeless = 0;
    static const World other = generated(99);
    for (const World* world : {&genesis(), &other}) {
        for (const Hero& hero : world->heroes.people) {
            const std::string klass = home_class(*world, hero);
            const std::size_t cities = realm_cities(*world, hero);
            check::equal(hero_fame_of(hero, klass, cities), hero.fame, "the fame of " + hero.uid);
            check::equal(hero_fame_band(hero.fame), hero.tier, "the band of " + hero.uid);
            Hero again = hero;
            hero_mark_deeds(again, klass, cities);
            for (std::size_t i = 0; i < hero.deeds.size(); ++i) {
                check::equal(again.deeds[i].scope, hero.deeds[i].scope, "a deed's scope, " + hero.uid);
                check::equal(again.deeds[i].known_for, hero.deeds[i].known_for, "the deed known for, " + hero.uid);
            }
            (hero.home ? homed : homeless) += 1;
        }
    }
    check::is_true(homed > 0 && homeless > 0, "the premise: heroes with a home and without");
}

// Held against a tier-5 nest: one deed and one line more, the fame and band recomputed, the scopes and the
// deed known for marked again, the hero standing where they stood, one operation logged, and the caller's
// world untouched.
TEST(hero_action_a_fight_is_a_deed_and_a_line_of_the_log) {
    const World& world = genesis();
    const std::string before_json = json_of(world);
    const Hero& castellan = a_castellan(world);
    const NestSite& nest = a_nest(world, 5);
    const double today = world_clock_of(world).day;
    const HeroActionResult result = act(world, {defended(castellan, nest.id, "won", today)});
    check::equal(json_of(world), before_json, "the caller's world is untouched");

    const Hero& after = hero_in(result.world, castellan.uid);
    check::equal(after.deeds.size(), castellan.deeds.size() + 1, "one deed more");
    check::equal(after.log.size(), castellan.log.size() + 1, "one line more");
    const HeroDeed& deed = after.deeds.back();
    check::equal(deed.event_kind, std::string("action"), "an action");
    check::equal(deed.event_id, std::string("action-1"), "the world's first");
    check::equal(deed.kind, std::string("defended"), "what they did");
    check::equal(deed.role, std::string("credited"), "it went their way");
    check::is_true(deed.has_tier && deed.tier == nest.tier, "the danger they faced");
    check::equal(deed.place_uid, castellan.presence->uid, "where");
    check::equal(deed.against_uid, nest.id, "against what");
    check::is_true(deed.has_day && deed.day == today, "the day");
    check::equal(deed.age, static_cast<int>(world_clock_of(world).age), "the age");
    const std::string nest_name = "the " + std::string(1, static_cast<char>(std::tolower(static_cast<unsigned char>(nest.name[0])))) +
                                  std::string(nest.name.substr(1));
    check::equal(deed.against_name, nest_name, "the nest as a line says it");
    const HeroLogEntry& line = after.log.back();
    check::equal(line.text, "Held " + deed.place_name + " against " + nest_name + ".", "the line");
    check::is_true(line.event_id == deed.event_id && line.event_kind == "action" && line.role == "credited", "the line is the deed's");
    check::is_true(deed.place_name.find('(') == std::string::npos, "a fortress said without its node number: " + deed.place_name);

    check::equal(after.fame, hero_fame_of(after, home_class(world, after), realm_cities(world, after)), "fame by the formula");
    check::is_true(after.fame > castellan.fame, "a fight won adds to fame");
    check::equal(after.tier, hero_fame_band(after.fame), "the band of the new fame");
    check::is_true(after.tier != castellan.tier, "the premise: a tier-5 fight won lifts a notable castellan's band: " + after.tier);
    check::equal(known_count(after), std::size_t{1}, "one deed known for");
    check::is_true(deed.known_for && !after.deeds.front().known_for, "the fight is now the widest");
    check::is_true(after.deeds.front().scope == castellan.deeds.front().scope, "an old deed's own scope is as it was");
    check::is_true(after.presence->uid == castellan.presence->uid, "they stand where they stood");

    check::equal(result.actions.size(), std::size_t{1}, "one report");
    const HeroActionReport& report = result.actions.front();
    check::is_true(report.uid == castellan.uid && report.event_id == deed.event_id && report.text == line.text, "the report says it");
    check::is_true(report.fame_before == castellan.fame && report.fame_after == after.fame, "the fame, before and after");
    check::is_true(report.band_before == castellan.tier && report.band_after == after.tier, "the band, before and after");
    check::is_true(report.scope == deed.scope && report.known_for, "its scope, and that it is known");

    check::equal(result.world.history.operations.size(), world.history.operations.size() + 1, "one operation");
    const auto* op = std::get_if<HeroOperation>(&result.world.history.operations.back());
    check::is_true(op != nullptr && op->actions.size() == 1 && op->actions.front().uid == castellan.uid, "the hero route's");
    const std::string after_json = json_of(result.world);
    check::is_true(after_json.find("\"kind\":\"hero\"") != std::string::npos, "the operation is written");
    check::is_true(after_json.find("\"against_uid\":\"" + nest.id + "\"") != std::string::npos, "the deed's new keys are written");
    check::is_true(before_json.find("\"against_uid\"") == std::string::npos, "and no world as generated has them");
    // Everyone else's record is as it was.
    for (const Hero& hero : world.heroes.people)
        if (hero.uid != castellan.uid)
            check::is_true(hero_in(result.world, hero.uid).deeds.size() == hero.deeds.size() &&
                               hero_in(result.world, hero.uid).fame == hero.fame,
                           "untouched: " + hero.uid);
}

// Lost, the deed weighs a third of won, and the line says so.
TEST(hero_action_a_fight_lost_weighs_a_third_of_one_won) {
    const World& world = genesis();
    const Hero& castellan = a_castellan(world);
    const NestSite& nest = a_nest(world, 5);
    const double today = world_clock_of(world).day;
    const HeroActionResult won_result = act(world, {defended(castellan, nest.id, "won", today)});
    const Hero& won = hero_in(won_result.world, castellan.uid);
    const HeroActionResult lost_result = act(world, {defended(castellan, nest.id, "lost", today)});
    const Hero& lost = hero_in(lost_result.world, castellan.uid);
    check::equal(lost.deeds.back().role, std::string("victim"), "it went against them");
    check::equal(lost.log.back().text, "Fought " + lost.deeds.back().against_name + " at " + lost.deeds.back().place_name + ", and lost.",
                 "the line of a fight lost");
    const double gain_won = won.fame - castellan.fame, gain_lost = lost.fame - castellan.fame;
    check::is_true(gain_lost > 0.0 && std::abs(gain_won - 3.0 * gain_lost) < 0.01,
                   "a third: " + std::to_string(gain_won) + " against " + std::to_string(gain_lost));
}

// A hunt names its quarry, and where it went if anywhere; an assault takes a place, or is thrown back.
TEST(hero_action_each_action_is_said_in_its_own_words) {
    const World& world = genesis();
    const Hero& castellan = a_castellan(world);
    const NestSite& nest = a_nest(world, 3);
    const double today = world_clock_of(world).day;
    HeroActionEntry hunt;
    hunt.uid = castellan.uid;
    hunt.action = "hunted";
    hunt.outcome = "won";
    hunt.against_uid = nest.id;
    hunt.day = today;
    HeroActionEntry assault = hunt;
    assault.action = "assaulted";
    assault.outcome = "lost";
    assault.against_uid.clear();
    assault.place_uid = world.ruins.front().uid;
    const HeroActionResult result = act(world, {hunt, assault});
    const Hero& after = hero_in(result.world, castellan.uid);
    const std::string quarry = after.deeds[after.deeds.size() - 2].against_name;
    check::equal(after.log[after.log.size() - 2].text, "Hunted down " + quarry + ".", "a hunt won, nowhere named");
    check::equal(after.log.back().text, "Assaulted the ruins of " + world.ruins.front().name + " and was thrown back.",
                 "an assault lost, against no one named");
    check::is_true(!after.deeds.back().has_tier, "no foe, no tier");
    // A place defended and lost is a fight lost there, never the place: they still stand in it.
    HeroActionEntry held_on = defended(castellan, "", "lost", today);
    const HeroActionResult lost_there = act(world, {held_on});
    check::equal(hero_in(lost_there.world, castellan.uid).log.back().text,
                 "Lost a fight at " + hero_in(lost_there.world, castellan.uid).deeds.back().place_name + ".", "a fight lost, no foe named");
    // In order: numbered across the world, the second seeing the first's fame; a later call goes on counting.
    check::equal(result.actions[0].event_id, std::string("action-1"), "the first");
    check::equal(result.actions[1].event_id, std::string("action-2"), "the second");
    check::equal(result.actions[1].fame_before, result.actions[0].fame_after, "the second sees the first");
    const HeroActionResult later = act(result.world, {defended(after, nest.id, "won", today)});
    check::equal(later.actions.front().event_id, std::string("action-3"), "a later call goes on counting");
    check::equal(later.world.history.operations.size(), world.history.operations.size() + 2, "one operation a call");
}

// Fame is recomputed with the hero's own realm: a hero of a realm of several cities reaches further, and
// their fame after a hunt is the formula's with that realm's size.
TEST(hero_action_fame_counts_the_realm_the_hero_belongs_to) {
    const World& world = genesis();
    const Hero* wide = nullptr;
    for (const Hero& hero : world.heroes.people)
        if (wide == nullptr && hero.status == "living" && hero.home && realm_cities(world, hero) >= 2) wide = &hero;
    check::is_true(wide != nullptr, "the premise: a living hero of a realm of two cities or more");
    HeroActionEntry hunt;
    hunt.uid = wide->uid;
    hunt.action = "hunted";
    hunt.outcome = "won";
    hunt.against_uid = a_nest(world, 4).id;
    hunt.day = world_clock_of(world).day;
    const HeroActionResult result = act(world, {hunt});
    const Hero& after = hero_in(result.world, wide->uid);
    check::equal(after.fame, hero_fame_of(after, home_class(world, after), realm_cities(world, after)),
                 "the formula's, with a realm of " + std::to_string(realm_cities(world, after)));
    check::is_true(after.fame != hero_fame_of(after, home_class(world, after), 1), "which a realm of one would not give");
}

// Moved, a hero stands where they were told, as they were told.
TEST(hero_action_a_hero_who_moved_stands_where_they_went) {
    const World& world = genesis();
    const Hero& castellan = a_castellan(world);
    HeroActionEntry entry = defended(castellan, a_nest(world, 5).id, "lost", world_clock_of(world).day);
    entry.moved_to_uid = castellan.home->uid;
    entry.moved_to_situation = std::string(story_situations().front());
    const HeroActionResult result = act(world, {entry});
    const Hero& after = hero_in(result.world, castellan.uid);
    check::is_true(after.presence && after.presence->uid == castellan.home->uid && after.presence->site_kind == "city" &&
                       after.presence->situation == entry.moved_to_situation,
                   "at their home city, as told");
}

// Every refusal names its field, and nothing is written.
TEST(hero_action_what_the_world_cannot_hold_is_refused) {
    const World& world = genesis();
    const Hero& castellan = a_castellan(world);
    const std::string nest = a_nest(world, 5).id;
    const double today = world_clock_of(world).day;
    const HeroActionEntry good = defended(castellan, nest, "won", today);
    const auto refused = [&](HeroActionEntry entry, const std::string& what) {
        check::throws([&] { (void)act(world, {entry}); }, what);
    };
    HeroActionEntry e = good;
    e.uid = "hero-nobody";
    refused(e, "a uid no hero has");
    const auto legend = std::find_if(world.heroes.people.begin(), world.heroes.people.end(), [](const Hero& h) { return h.status == "legend"; });
    check::is_true(legend != world.heroes.people.end(), "the premise: a legend");
    e = good;
    e.uid = legend->uid;
    refused(e, "a legend");
    e = good;
    e.action = "defend";
    refused(e, "an action not in its words");
    e = good;
    e.outcome = "draw";
    refused(e, "an outcome not in its words");
    e = good;
    e.place_uid = "nowhere-at-all";
    refused(e, "a place the world does not name");
    e = good;
    e.place_uid.clear();
    refused(e, "a place defended must be named");
    e = good;
    e.against_uid = "nest-of-nothing";
    refused(e, "a foe the world does not name");
    e = good;
    e.against_uid = castellan.uid;
    refused(e, "a hero against themself");
    e = good;
    e.action = "hunted";
    e.against_uid.clear();
    refused(e, "a hunt must name its quarry");
    e = good;
    e.day = -1.0;
    refused(e, "a day before the founding");
    e = good;
    e.day = today + 1.0;
    refused(e, "a day past the clock");
    e = good;
    e.moved_to_uid = castellan.home->uid;
    refused(e, "moved, and no situation");
    e = good;
    e.moved_to_uid = castellan.home->uid;
    e.moved_to_situation = "sightseeing";
    refused(e, "a situation outside the story web's");
    e = good;
    e.moved_to_uid = world.key_locations.sites.front().id;
    e.moved_to_situation = std::string(story_situations().front());
    refused(e, "a hero does not stand at a key location");
    HeroActionRequest request;
    check::throws([&] { (void)hero_action_request(world, request); }, "no action");
    request.actions.assign(kMaxHeroActions + 1, good);
    check::throws([&] { (void)hero_action_request(world, request); }, "too many");
    request.actions.assign(1, good);
    request.api_version = kHeroActionApiVersion + 1;
    check::throws([&] { (void)hero_action_request(world, request); }, "another api version");
    // A bad action after a good one: nothing is written for either.
    HeroActionEntry bad = good;
    bad.outcome = "draw";
    check::throws([&] { (void)act(world, {good, bad}); }, "judged whole before anything is written");
    check::equal(act(world, {good}).actions.front().event_id, std::string("action-1"), "and the world counts none");
}

// ---- How a hero's alignment moved after a deed of theirs (hero_shift_request) -----------------------------------

namespace {

HeroShiftResult shift(const World& world, std::vector<HeroShiftEntry> entries) {
    HeroShiftRequest request;
    request.shifts = std::move(entries);
    return hero_shift_request(world, request);
}

HeroShiftEntry shifted(const Hero& hero, const std::string& after, double law, double good, double day) {
    HeroShiftEntry entry;
    entry.uid = hero.uid;
    entry.law = law;
    entry.good = good;
    entry.after_event_id = after;
    entry.day = day;
    return entry;
}

bool same_persona(const HeroPersona& a, const HeroPersona& b) {
    return a.archetype == b.archetype && a.belief == b.belief && a.wants == b.wants && a.fears == b.fears && a.tells == b.tells &&
           a.mechanic == b.mechanic && a.manner == b.manner && a.voice == b.voice && a.lies == b.lies && a.toward_player == b.toward_player &&
           a.cornered == b.cornered && a.never == b.never && a.line == b.line;
}

}  // namespace

// A castellan who lost a fight, hardened after it: a step toward evil, their persona and their face composed again
// from their card for the alignment they now have, a line of their log after the deed, one operation logged,
// nothing else touched, and the caller's world untouched.
TEST(hero_shift_a_hero_hardened_after_a_fight) {
    const World& world = genesis();
    // A castellan a step carries across a boundary, so what is composed again for it differs from what they had.
    const Hero* near = nullptr;
    for (const Hero& hero : world.heroes.people)
        if (!near && hero.role == "castellan" && hero.status == "living" && hero.faces.size() == 1 && hero.presence &&
            hero.presence->site_kind == "fortress" &&
            hero_alignment_record(hero.alignment.law, std::clamp(hero.alignment.good - kHeroShiftStep, -1.0, 1.0)).code != hero.alignment.code)
            near = &hero;
    check::is_true(near != nullptr, "the premise: a castellan a step carries across a boundary");
    if (near == nullptr) return;
    const Hero& castellan = *near;
    const double today = world_clock_of(world).day;
    const HeroActionResult fought = act(world, {defended(castellan, a_nest(world, 5).id, "lost", today)});
    const std::string deed_id = fought.actions.front().event_id;
    const std::string before_json = json_of(fought.world);
    const HeroShiftResult result = shift(fought.world, {shifted(castellan, deed_id, 0.0, -kHeroShiftStep, today)});
    check::equal(json_of(fought.world), before_json, "the caller's world is untouched");

    const Hero& was = hero_in(fought.world, castellan.uid);
    const Hero& now = hero_in(result.world, castellan.uid);
    check::is_true(std::abs(now.alignment.good - std::max(-1.0, was.alignment.good - kHeroShiftStep)) < 1e-3,
                   "a step toward evil: " + std::to_string(was.alignment.good) + " to " + std::to_string(now.alignment.good));
    check::is_true(std::abs(now.alignment.law - was.alignment.law) < 1e-3, "law as it was");
    check::is_true(same_persona(now.persona, compose_persona(*hero_archetype_card(now.archetype), now.alignment)),
                   "their persona composed again from their card for the alignment they now have");
    check::is_true(now.alignment.code != was.alignment.code && !same_persona(now.persona, was.persona),
                   "the premise: across a boundary, another persona: " + was.alignment.code + " to " + now.alignment.code);
    check::equal(now.faces.size(), std::size_t{1}, "one face");
    check::equal(now.faces.front().alignment.code, now.alignment.code, "their face moved with them");
    check::is_true(same_persona(now.faces.front().persona, now.persona), "and speaks as they now do");
    check::equal(now.log.size(), was.log.size() + 1, "one line more");
    check::equal(now.log.back().event_kind, std::string("shift"), "a shift");
    check::equal(now.log.back().event_id, deed_id, "after that deed");
    check::is_true(now.log.back().text.rfind("Hardened after the fight at ", 0) == 0, "in the world's words: " + now.log.back().text);
    check::equal(now.deeds.size(), was.deeds.size(), "no deed more");
    check::equal(now.fame, was.fame, "their fame as it was");
    check::equal(result.shifts.front().code_before, was.alignment.code, "reported as they were");
    check::equal(result.shifts.front().code_after, now.alignment.code, "and as they are");
    check::equal(result.world.history.operations.size(), fought.world.history.operations.size() + 1, "one operation more");
    const auto* op = std::get_if<HeroShiftOperation>(&result.world.history.operations.back());
    check::is_true(op != nullptr && op->shifts.size() == 1 && op->shifts.front().uid == castellan.uid && op->shifts.front().after_event_id == deed_id,
                   "the shift as the request said it");
    const std::string written = json_of(result.world);
    check::is_true(written.find("\"kind\":\"hero shift\"") != std::string::npos && written.find("\"after_event_id\":\"" + deed_id + "\"") != std::string::npos,
                   "and the document says it");
    std::size_t others = 0;
    for (const Hero& other : fought.world.heroes.people)
        if (other.uid != castellan.uid) {
            const Hero& same = hero_in(result.world, other.uid);
            check::is_true(same.alignment.code == other.alignment.code && same_persona(same.persona, other.persona) &&
                               same.log.size() == other.log.size(),
                           "another hero as they were: " + other.uid);
            ++others;
        }
    check::is_true(others > 0, "the premise: other heroes");
    // The same request, the same world.
    check::equal(json_of(shift(fought.world, {shifted(castellan, deed_id, 0.0, -kHeroShiftStep, today)}).world), json_of(result.world),
                 "the same request, the same world");
}

// A step at most moves a hero across one boundary of their alignment, never two; two in one call move them from
// where the first left them; each axis stops at the end of its scale.
TEST(hero_shift_steps_are_bounded_and_follow_one_another) {
    const World& world = genesis();
    const Hero& castellan = a_castellan(world);
    const double today = world_clock_of(world).day;
    const HeroActionResult fought = act(world, {defended(castellan, a_nest(world, 5).id, "lost", today)});
    const std::string deed_id = fought.actions.front().event_id;
    const Hero& was = hero_in(fought.world, castellan.uid);
    const HeroShiftResult twice =
        shift(fought.world, {shifted(castellan, deed_id, 0.0, -kHeroShiftStep, today), shifted(castellan, deed_id, 0.0, -kHeroShiftStep, today)});
    const Hero& now = hero_in(twice.world, castellan.uid);
    check::is_true(std::abs(now.alignment.good - std::max(-1.0, was.alignment.good - 2 * kHeroShiftStep)) < 2e-3, "two steps, one after the other");
    check::equal(twice.shifts.back().code_before, twice.shifts.front().code_after, "the second from where the first left them");
    check::equal(now.log.size(), was.log.size() + 2, "a line each");
    // Every one-faced hero's good letter moves at most one place a step: G to N, N to E, never G to E.
    std::size_t moved = 0;
    for (const Hero& hero : world.heroes.people) {
        if (hero.faces.size() != 1 || hero.status == "legend" || hero_archetype_card(hero.archetype) == nullptr) continue;
        const HeroAlignment one = hero_alignment_record(hero.alignment.law, std::clamp(hero.alignment.good - kHeroShiftStep, -1.0, 1.0));
        const std::string order = "GNE";
        const std::size_t from = order.find(hero.alignment.code.back()), to = order.find(one.code.back());
        check::is_true(from != std::string::npos && to != std::string::npos && to - from <= 1, "one boundary at most: " + hero.uid);
        moved += to != from ? 1 : 0;
    }
    check::is_true(moved > 0, "the premise: a step carried someone across a boundary");
    // To the end of the scale and no further.
    HeroShiftRequest many;
    for (int i = 0; i < 8; ++i) many.shifts.push_back(shifted(castellan, deed_id, 0.0, -kHeroShiftStep, today));
    check::equal(hero_in(hero_shift_request(fought.world, many).world, castellan.uid).alignment.good, -1.0, "stopped at the end of the scale");
}

// What the world cannot hold is refused, and nothing is written when any shift is refused.
TEST(hero_shift_what_the_world_cannot_hold_is_refused) {
    const World& world = genesis();
    const Hero& castellan = a_castellan(world);
    const double today = world_clock_of(world).day;
    const HeroActionResult fought = act(world, {defended(castellan, a_nest(world, 5).id, "lost", today)});
    const std::string deed_id = fought.actions.front().event_id;
    const HeroShiftEntry good = shifted(castellan, deed_id, 0.0, -kHeroShiftStep, today);
    // Refused for the field that is wrong: a legend or a hero of two faces holds no deed done since the world was
    // made either, and a refusal of that deed instead would pass over a guard that is gone.
    const auto refused = [&](HeroShiftEntry entry, const std::string& what, const std::string& field) {
        try {
            (void)shift(fought.world, {entry});
        } catch (const WorldTimeError& error) {
            check::equal(error.field(), field, what + ", refused for it");
            return;
        }
        check::fail(what + " was not refused");
    };
    HeroShiftEntry e = good;
    e.uid = "hero-nobody";
    check::throws([&] { (void)shift(fought.world, {e}); }, "a uid no hero has");
    // A legend of one face and a card, so only their being a legend stands in the way.
    const auto legend = std::find_if(world.heroes.people.begin(), world.heroes.people.end(), [](const Hero& h) {
        return h.status == "legend" && h.faces.size() == 1 && hero_archetype_card(h.archetype) != nullptr;
    });
    check::is_true(legend != world.heroes.people.end(), "the premise: a legend of one face");
    e = good;
    e.uid = legend->uid;
    refused(e, "a legend", "shifts[0].uid");
    const auto faced = std::find_if(world.heroes.people.begin(), world.heroes.people.end(),
                                    [](const Hero& h) { return h.faces.size() > 1 && h.status != "legend"; });
    check::is_true(faced != world.heroes.people.end(), "the premise: a hero of more than one face");
    e = good;
    e.uid = faced->uid;
    refused(e, "a hero of more than one face", "shifts[0].uid");
    e = good;
    e.good = -(kHeroShiftStep + 0.01);
    refused(e, "past a step toward evil", "shifts[0].good");
    e = good;
    e.law = kHeroShiftStep + 0.01;
    refused(e, "past a step toward law", "shifts[0].law");
    e = good;
    e.good = std::nan("");
    refused(e, "not a number", "shifts[0].good");
    e = good;
    e.good = 0.0;
    refused(e, "no move at all", "shifts[0].good");
    e = good;
    e.after_event_id = "action-999";
    refused(e, "no such deed", "shifts[0].after_event_id");
    e = good;
    e.after_event_id = castellan.deeds.front().event_id;
    refused(e, "a deed of generation, not one done since", "shifts[0].after_event_id");
    e = good;
    e.day = -1.0;
    refused(e, "a day before the founding", "shifts[0].day");
    e = good;
    e.day = today + 1.0;
    refused(e, "a day past the clock", "shifts[0].day");
    HeroShiftRequest request;
    check::throws([&] { (void)hero_shift_request(fought.world, request); }, "no shift");
    request.shifts.assign(kMaxHeroShifts + 1, good);
    check::throws([&] { (void)hero_shift_request(fought.world, request); }, "too many");
    request.shifts.assign(1, good);
    request.api_version = kHeroShiftApiVersion + 1;
    check::throws([&] { (void)hero_shift_request(fought.world, request); }, "another api version");
    HeroShiftEntry bad = good;
    bad.good = 0.0;
    check::throws([&] { (void)shift(fought.world, {good, bad}); }, "judged whole before anything is written");
    check::throws([&] { (void)shift(world, {good}); }, "a deed the world does not hold yet");
}
