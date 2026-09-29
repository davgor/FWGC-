// The hero-action API (hero_action.hpp): what a hero did after the world was made, written into
// their record.
#include "fwg/hero_action.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fwg/city_plan.hpp"
#include "fwg/heroes.hpp"
#include "fwg/story_web.hpp"
#include "fwg/world_time.hpp"

namespace fwg {
namespace {

// The shared refusal builders (world_time.hpp), under a name that fits on a line.
namespace refuse = ::fwg::request_errors;

const std::vector<std::string>& actions() {
    static const std::vector<std::string> words = {"defended", "hunted", "assaulted"};
    return words;
}

const std::vector<std::string>& outcomes() {
    static const std::vector<std::string> words = {"won", "lost"};
    return words;
}

bool one_of(const std::vector<std::string>& words, const std::string& word) {
    return std::find(words.begin(), words.end(), word) != words.end();
}

std::string lower_first(std::string text) {
    if (!text.empty()) text[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[0])));
    return text;
}

// A roster site's name without the node number that tells two of one name apart: "the fortress
// above Bargok (108)" is said "the fortress above Bargok".
std::string spoken_site(std::string name) {
    if (name.size() > 3 && name.back() == ')') {
        const std::size_t open = name.rfind(" (");
        if (open != std::string::npos &&
            std::all_of(name.begin() + static_cast<std::ptrdiff_t>(open) + 2, name.end() - 1,
                        [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }))
            name.erase(open);
    }
    return name;
}

// A place the world names, as a log line says it, and the kind of site it is.
struct Place {
    std::string kind;  // city | hamlet | fortress | ruin | key_location
    std::string name;
};

std::optional<Place> place_named(const World& world, const std::string& uid) {
    if (world.has_settlements)
        for (const auto& site : world.settlements.sites)
            if (site.uid == uid) return Place{"city", site.name};
    if (world.has_npcs)
        for (const NpcSite& site : world.npcs.sites)
            if (site.uid == uid && site.kind != "city") return Place{site.kind, spoken_site(site.name)};
    for (const Ruin& ruin : world.ruins)
        if (ruin.uid == uid) return Place{"ruin", "the ruins of " + ruin.name};
    if (world.has_key_locations)
        for (const KeyLocationSite& site : world.key_locations.sites)
            if (site.id == uid) return Place{"key_location", site.name};
    return std::nullopt;
}

// What a hero faced, as a log line says it, and how dangerous it was (0 for no tier).
struct Foe {
    std::string name;
    int tier = 0;
};

std::optional<Foe> foe_named(const World& world, const std::string& uid, const std::string& self) {
    for (const NestSite& nest : world.beast_nests.sites)
        if (nest.id == uid) return Foe{"the " + lower_first(std::string(nest.name)), nest.tier};
    for (const HeroDread& dread : world.heroes.dreads)
        if (dread.uid == uid) return Foe{dread.name, dread.nest_tier};
    for (const Hero& hero : world.heroes.people)
        if (hero.uid == uid && hero.uid != self) return Foe{hero.name, 0};
    return std::nullopt;
}

// The words of the log's line: what they did, where, against what, and how it went.
std::string line_of(const std::string& action, bool won, const std::optional<Place>& place, const std::optional<Foe>& foe) {
    const std::string where = place ? place->name : std::string();
    const std::string who = foe ? foe->name : std::string();
    if (action == "defended") {
        if (won) return "Held " + where + (who.empty() ? "" : " against " + who) + ".";
        return "Was driven from " + where + (who.empty() ? "" : " by " + who) + ".";
    }
    if (action == "hunted") {
        if (won) return "Hunted down " + who + (where.empty() ? "" : " near " + where) + ".";
        return "Hunted " + who + (where.empty() ? "" : " near " + where) + ", and was beaten back.";
    }
    if (won) return "Took " + where + (who.empty() ? "" : " from " + who) + ".";
    return "Assaulted " + where + " and was thrown back" + (who.empty() ? "" : " by " + who) + ".";
}

std::string home_class_of(const World& world, const Hero& hero) {
    if (!hero.home.has_value() || !world.has_settlements) return "small";
    for (const auto& site : world.settlements.sites)
        if (site.uid == hero.home->uid) return std::string(city_class_name(site.city_class));
    return "small";
}

std::size_t realm_cities_of(const World& world, const Hero& hero) {
    for (const HeroRealm& realm : world.heroes.realms)
        if (realm.uid == hero.realm_uid) return realm.city_uids.size();
    return 1;
}

std::string field(std::size_t index, std::string_view name) {
    return "actions[" + std::to_string(index) + "]." + std::string(name);
}

// Everything judged against the world before anything is written.
struct Judged {
    const HeroActionEntry* entry = nullptr;
    std::size_t hero = 0;  // index into heroes.people
    std::optional<Place> place;
    std::optional<Foe> foe;
    std::optional<Place> moved_to;
};

}  // namespace

HeroActionResult hero_action_request(const World& world, const HeroActionRequest& request) {
    if (request.api_version != kHeroActionApiVersion) throw refuse::unsupported_api(request.api_version, kHeroActionApiVersion);
    if (request.actions.empty()) throw refuse::out_of_range("actions", 0.0, 1.0, static_cast<double>(kMaxHeroActions), "");
    if (request.actions.size() > kMaxHeroActions)
        throw refuse::over_capacity("actions", request.actions.size(), kMaxHeroActions, "hero action");
    if (!world.has_heroes || world.heroes.status != "ok")
        throw refuse::missing_block("heroes", "This world carries no heroes to act: the cast is written at generation, "
                                              "and a world whose heroes package failed has none.");
    const double today = world_clock_of(world).day;

    std::vector<Judged> judged;
    for (std::size_t i = 0; i < request.actions.size(); ++i) {
        const HeroActionEntry& entry = request.actions[i];
        Judged j;
        j.entry = &entry;
        const auto hero = std::find_if(world.heroes.people.begin(), world.heroes.people.end(),
                                       [&](const Hero& h) { return h.uid == entry.uid; });
        if (hero == world.heroes.people.end()) {
            std::vector<std::string> uids;
            for (const Hero& h : world.heroes.people) uids.push_back(h.uid);
            throw refuse::unknown_name(entry.uid, uids, "hero");
        }
        if (hero->status == "legend")
            throw refuse::refused_by_world(field(i, "uid"), entry.uid, "A legend acts in stories only, not in the world.", {});
        j.hero = static_cast<std::size_t>(hero - world.heroes.people.begin());
        if (!one_of(actions(), entry.action)) throw refuse::invalid_choice(field(i, "action"), entry.action, actions());
        if (!one_of(outcomes(), entry.outcome)) throw refuse::invalid_choice(field(i, "outcome"), entry.outcome, outcomes());
        if (!entry.place_uid.empty()) {
            j.place = place_named(world, entry.place_uid);
            if (!j.place)
                throw refuse::refused_by_world(field(i, "place_uid"), entry.place_uid,
                                               "The world names no city, hamlet, fortress, ruin or key location by that id.", {});
        } else if (entry.action != "hunted") {
            throw refuse::refused_by_world(field(i, "place_uid"), "", "To " + entry.action.substr(0, entry.action.size() - 2) +
                                                                          " is to do it somewhere: name the place.", {});
        }
        if (!entry.against_uid.empty()) {
            j.foe = foe_named(world, entry.against_uid, entry.uid);
            if (!j.foe)
                throw refuse::refused_by_world(field(i, "against_uid"), entry.against_uid,
                                               "The world names no nest, dread or other hero by that id.", {});
        } else if (entry.action == "hunted") {
            throw refuse::refused_by_world(field(i, "against_uid"), "", "A hunt is after something: name what.", {});
        }
        if (!std::isfinite(entry.day) || entry.day < 0.0 || entry.day > today + 1e-9)
            throw refuse::out_of_range(field(i, "day"), entry.day, 0.0, today, "days");
        if (entry.moved_to_uid.empty() != entry.moved_to_situation.empty())
            throw refuse::refused_by_world(field(i, entry.moved_to_uid.empty() ? "moved_to_uid" : "moved_to_situation"), "",
                                           "Where they moved to, and how they stand there, come together.", {});
        if (!entry.moved_to_uid.empty()) {
            j.moved_to = place_named(world, entry.moved_to_uid);
            if (!j.moved_to || j.moved_to->kind == "key_location")
                throw refuse::refused_by_world(field(i, "moved_to_uid"), entry.moved_to_uid,
                                               "A hero stands at a city, hamlet, fortress or ruin the world names.", {});
            const auto situations = story_situations();
            if (std::find(situations.begin(), situations.end(), entry.moved_to_situation) == situations.end())
                throw refuse::invalid_choice(field(i, "moved_to_situation"), entry.moved_to_situation,
                                             std::vector<std::string>(situations.begin(), situations.end()));
        }
        judged.push_back(std::move(j));
    }

    // Written to a private copy, in order: a later action sees an earlier one's fame and deeds.
    HeroActionResult result{world, {}};
    std::size_t actions_done = 0;
    for (const Hero& h : world.heroes.people)
        for (const HeroDeed& d : h.deeds)
            if (d.event_kind == "action") ++actions_done;
    const auto age = static_cast<int>(world_clock_of(world).age);
    HeroOperation operation;
    for (const Judged& j : judged) {
        const HeroActionEntry& entry = *j.entry;
        Hero& hero = result.world.heroes.people[j.hero];
        const std::string home_class = home_class_of(result.world, hero);
        const std::size_t realm_cities = realm_cities_of(result.world, hero);
        HeroActionReport report;
        report.uid = hero.uid;
        report.fame_before = hero.fame;
        report.band_before = hero.tier;

        HeroDeed deed;
        deed.event_kind = "action";
        deed.event_id = "action-" + std::to_string(++actions_done);
        deed.age = age;
        deed.role = entry.outcome == "won" ? "credited" : "victim";
        deed.kind = entry.action;
        if (j.foe && j.foe->tier > 0) {
            deed.tier = j.foe->tier;
            deed.has_tier = true;
        }
        if (j.place) {
            deed.place_uid = entry.place_uid;
            deed.place_name = j.place->name;
        }
        if (j.foe) {
            deed.against_uid = entry.against_uid;
            deed.against_name = j.foe->name;
        }
        deed.day = entry.day;
        deed.has_day = true;
        const std::string text = line_of(entry.action, entry.outcome == "won", j.place, j.foe);
        hero.deeds.push_back(deed);
        hero.log.push_back(HeroLogEntry{age, deed.event_kind, deed.event_id, deed.role, text});
        hero.fame = hero_fame_of(hero, home_class, realm_cities);
        hero.tier = hero_fame_band(hero.fame);
        hero_mark_deeds(hero, home_class, realm_cities);
        if (j.moved_to) hero.presence = HeroPresence{j.moved_to->kind, entry.moved_to_uid, entry.moved_to_situation};

        report.event_id = deed.event_id;
        report.text = text;
        report.scope = hero.deeds.back().scope;
        report.known_for = hero.deeds.back().known_for;
        report.fame_after = hero.fame;
        report.band_after = hero.tier;
        result.actions.push_back(std::move(report));
        operation.actions.push_back(entry);
    }
    result.world.history.operations.push_back(std::move(operation));
    return result;
}

}  // namespace fwg
