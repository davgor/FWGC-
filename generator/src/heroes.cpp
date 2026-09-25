#include "fwg/heroes.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <utility>

#include "fwg/heritage.hpp"
#include "fwg/json.hpp"
#include "fwg/religion.hpp"

namespace fwg {
namespace {

// ---------------------------------------------------------------------------
// Small shared helpers.
// ---------------------------------------------------------------------------

// Half-away-from-zero, deliberately. The reference rounds half to even, which the quest
// rubric itself refuses ("a rubric that sends 2.5 down and 3.5 up reads as arbitrary");
// this is a clean rewrite, so the whole library rounds the readable way instead of
// carrying one convention for the numbers and another for the bands.
double round_to(double value, double scale) { return std::round(value * scale) / scale; }
double round3(double value) { return round_to(value, 1000.0); }
double round6(double value) { return round_to(value, 1000000.0); }

bool contains(std::span<const std::string_view> haystack, std::string_view needle) {
    return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
}

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

std::string lowercase(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::string capitalise_first(std::string_view text) {
    std::string out(text);
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] = static_cast<char>(out[0] - 'a' + 'A');
    return out;
}

std::string number_text(double value) { return format_double(value); }

Rng stream(std::uint64_t seed, const std::string& domain) { return Rng::child(seed, domain); }

// One positional pick from a list of authored strings.
//
// Every list in the names document is drawn this way, and the epithet filter that runs
// before it preserves relative order, so both the full list and every surviving
// subsequence are contracts.
template <typename T>
const T& pick(Rng& draw, const std::vector<T>& items) {
    return items[static_cast<std::size_t>(draw.below(items.size()))];
}

// ---------------------------------------------------------------------------
// Alignment.
// ---------------------------------------------------------------------------

constexpr double kPoleThreshold = 0.34;
// The minimum distance from zero a deed-decided sign is held at. Jitter may move a value
// but never across zero once a deed put it on one side.
constexpr double kDeedSignFloor = 0.01;

double clamp_axis(double value) { return std::max(-1.0, std::min(1.0, value)); }

int pole_index(double value) {
    if (value > kPoleThreshold) return 2;
    if (value < -kPoleThreshold) return 0;
    return 1;
}

double bounded(double value, double decided) {
    if (decided > 0.0) return std::max(kDeedSignFloor, value);
    if (decided < 0.0) return std::min(-kDeedSignFloor, value);
    return value;
}

// ---------------------------------------------------------------------------
// The world, indexed once.
// ---------------------------------------------------------------------------

struct Ctx {
    const HeroesRequest* req = nullptr;
    const HeroWellsPolicy* wells = nullptr;
    const HeroWordTables* words = nullptr;
    int final_age = 0;
    double radius = 1.0;
    double spacing = 1000.0;

    std::map<std::string, const HeroCity*> city_by_uid, city_by_id;
    std::map<std::string, const HeroRuin*> ruin_by_uid, ruin_by_id;
    std::map<std::string, const HeroNest*> nest_by_id;
    std::map<std::string, const HeroCivilization*> civ_by_id;
    std::map<std::string, const HeroReligionSite*> shrine_by_id;
    std::map<std::string, const HeroPort*> port_by_id;
    std::map<std::string, const HeroCollege*> college_by_id;
    std::map<std::string, int> lost_per_civilization;
    std::map<std::string, const HeroWar*> war_by_defeated;
    // Both built from `request.networks` IN THE CALLER'S ORDER. The reference builds the
    // first by walking the networks dict and the second by walking it SORTED, so a node id
    // present in two schools resolves to a different school in the two passes; that
    // disagreement is designed out here rather than reproduced.
    std::map<std::string, std::pair<std::string, const HeroLeyNodeRef*>> ley_by_node;
    std::map<std::string, double> intensity_by_node;
    // Resolved peoples, cached. The reference resolves one per person named; resolution is
    // pure, so caching changes nothing but the cost.
    std::map<std::string, heritage::Resolved> peoples;

    const heritage::Resolved& people_of(const std::string& civilization_id,
                                        const std::string& parent_race_id) {
        const std::string key = civilization_id + "\x1f" + parent_race_id;
        auto it = peoples.find(key);
        if (it == peoples.end()) {
            it = peoples.emplace(key, heritage::resolve(civilization_id, parent_race_id)).first;
        }
        return it->second;
    }
};

std::string parent_race_of(const Ctx& ctx, std::string_view own, std::string_view profile) {
    if (!own.empty()) return std::string(own);
    auto it = ctx.civ_by_id.find(std::string(profile));
    if (it != ctx.civ_by_id.end() && !it->second->parent_race_id.empty()) {
        return it->second->parent_race_id;
    }
    return "human";
}

std::string civilization_name(const Ctx& ctx, const std::string& civilization_id) {
    auto it = ctx.civ_by_id.find(civilization_id);
    return it != ctx.civ_by_id.end() ? it->second->name : civilization_id;
}

// Closest candidate to `target`, ties broken by uid so the answer never depends on list
// order. Null on an empty candidate list -- and on a target with no globe direction, which
// the reference reaches only through the shrines well's explicit guard.
const HeroCity* nearest_city(const Vec3& direction, bool has_direction,
                             const std::vector<const HeroCity*>& candidates, double radius) {
    if (!has_direction) return nullptr;
    const HeroCity* best = nullptr;
    double best_distance = 0.0;
    for (const HeroCity* city : candidates) {
        const double distance = hero_separation_m(direction, city->direction, radius);
        if (best == nullptr || distance < best_distance ||
            (distance == best_distance && city->uid < best->uid)) {
            best = city;
            best_distance = distance;
        }
    }
    return best;
}

std::vector<const HeroCity*> all_cities(const Ctx& ctx) {
    std::vector<const HeroCity*> out;
    out.reserve(ctx.req->cities.size());
    for (const HeroCity& city : ctx.req->cities) out.push_back(&city);
    return out;
}

double grid_metres(int ax, int az, int bx, int bz, double spacing) {
    // Plain Euclidean on node coordinates: it does NOT wrap at the longitude seam, so a
    // hamlet near column 0 and a nest near column n-1 are measured the long way round.
    // That is the reference behaviour and the quest-pressure term reuses this exact
    // measure, so "fixing" one and not the other would let the two passes disagree about
    // the same nest.
    const double dx = static_cast<double>(ax - bx);
    const double dz = static_cast<double>(az - bz);
    return std::hypot(dx, dz) * spacing;
}

bool dangerous_nest(const HeroNest& nest, const HeroWordTables& words) {
    return lowercase(nest.name).find("dragon") != std::string::npos ||
           contains(words.dark_families, nest.family) || nest.family == "draconic";
}

// `nest-abyssal-choir-worms-156` names its species even when the nest itself is gone.
std::string species_from_nest_id(const std::string& nest_id) {
    std::vector<std::string> parts;
    std::string current;
    for (const char c : nest_id) {
        if (c == '-') {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(current);
    if (parts.size() > 2 && parts.front() == "nest") {
        parts.erase(parts.begin());
        parts.pop_back();
    }
    std::string joined;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) joined.push_back(' ');
        joined += parts[i];
    }
    // `str.capitalize`, not the first-letter-only raise the epithet filler uses: this one
    // deliberately lowers the rest.
    for (char& c : joined) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return capitalise_first(joined);
}

std::string cause_kind(std::string_view cause, const HeroWordTables& words) {
    if (starts_with(cause, "war_")) return "war";
    if (contains(words.beast_causes, cause)) return "beast";
    if (cause == words.self_magic_cause) return "self_magic";
    return "ley";
}

std::string cause_phrase(std::string_view cause, const HeroWordTables& words) {
    if (starts_with(cause, "war_")) return "the " + std::string(cause.substr(4)) + " war";
    const std::string_view phrase = hero_word(words.cause_phrases, cause);
    if (!phrase.empty()) return std::string(phrase);
    const bool vowel = !cause.empty() && std::string_view("aeiou").find(cause[0]) != std::string_view::npos;
    return std::string(vowel ? "an " : "a ") + std::string(cause) + " leyline surge";
}

double class_weight(std::string_view city_class) {
    const HeroFameTuning& fame = hero_fame_tuning();
    if (city_class == "medium") return fame.medium;
    if (city_class == "capital") return fame.capital;
    return fame.small;
}

double war_weight(std::string_view kind) {
    const HeroFameTuning& fame = hero_fame_tuning();
    if (kind == "regional") return fame.regional;
    if (kind == "international") return fame.international;
    return fame.civil;
}

double site_weight(std::string_view event_kind, bool& known) {
    const HeroFameTuning& fame = hero_fame_tuning();
    known = true;
    if (event_kind == "hamlet") return fame.hamlet;
    if (event_kind == "fortress") return fame.fortress;
    if (event_kind == "port") return fame.port;
    if (event_kind == "shrine") return fame.shrine;
    known = false;
    return 0.0;
}

HeroSiteRef city_ref(const HeroCity& city) { return {"city", city.uid, city.name}; }

HeroLost lost_ref(const HeroRuin& ruin) {
    return {ruin.uid, ruin.id, ruin.name, ruin.city_class, ruin.cause, ruin.destroyed_age};
}

// ---------------------------------------------------------------------------
// The shared roll. A candidate rolls exactly once, seeded from its uid, and the ledger
// keeps the outcome either way -- which is what lets the block record what history could
// have produced and did not. A fresh named stream per candidate is what makes the ledger
// replay-stable regardless of how many candidates precede it.
// ---------------------------------------------------------------------------

double chance_of(double value) {
    return round3(std::max(0.0, std::min(hero_wells_policy().cap, value)));
}

bool roll_once(const std::string& uid, const std::vector<HeroDeed>& deeds, const std::string& kind,
               double chance, std::uint64_t seed, std::vector<HeroRoll>& rolls,
               std::string_view event_id = {}, int age = 0, bool has_age = false) {
    Rng draw = stream(seed, "hero-roll-" + uid);
    const double value = draw.random();
    HeroRoll row;
    row.candidate_uid = uid;
    row.kind = kind;
    row.event_id = event_id.empty() && !deeds.empty() ? deeds.front().event_id : std::string(event_id);
    if (has_age) {
        row.age = age;
        row.has_age = true;
    } else if (!deeds.empty()) {
        row.age = deeds.front().age;
        row.has_age = true;
    }
    row.chance = chance;
    row.roll = round6(value);
    row.precipitated = value < chance;
    rolls.push_back(row);
    return row.precipitated;
}

// ---------------------------------------------------------------------------
// Realms.
// ---------------------------------------------------------------------------

std::vector<HeroRealm> build_realms(Ctx& ctx) {
    std::vector<HeroRealm> realms;
    for (const HeroCulture& culture : ctx.req->cultures) {
        std::vector<const HeroCity*> members;
        for (const std::string& id : culture.city_ids) {
            auto it = ctx.city_by_id.find(id);
            if (it != ctx.city_by_id.end()) members.push_back(it->second);
        }
        if (members.empty()) continue;
        const HeroCity* capital = nullptr;
        for (const HeroCity* city : members) {
            if (city->city_class == "capital") {
                capital = city;
                break;
            }
        }
        if (capital == nullptr) {
            // max by (suitability, -node): on a suitability tie the LOWEST node wins, and
            // on a tie of the whole key the FIRST member does.
            capital = members.front();
            for (const HeroCity* city : members) {
                if (city->suitability > capital->suitability ||
                    (city->suitability == capital->suitability && -city->node > -capital->node)) {
                    capital = city;
                }
            }
        }
        const std::string parent_race =
            !capital->parent_race_id.empty() ? capital->parent_race_id
                                             : (ctx.civ_by_id.count(culture.civilization_id)
                                                    ? ctx.civ_by_id[culture.civilization_id]->parent_race_id
                                                    : std::string());
        const std::string english = "Realm of " + hero_short_name(capital->name);
        // The realm's name in its own tongue, falling back to the English form: a map
        // label and a chronicle line want different things from the same realm, so both
        // ship. The fallback fires when the tongue has no realm morpheme.
        std::string own = heritage::realm_name(
            ctx.people_of(culture.civilization_id,
                          parent_race.empty() ? std::string("human") : parent_race),
            hero_short_name(capital->name));
        if (own.empty()) own = english;

        HeroRealm realm;
        realm.uid = "realm-" + culture.id;
        realm.name = own;
        realm.english_name = english;
        realm.civilization_id = culture.civilization_id;
        realm.civilization_name = civilization_name(ctx, culture.civilization_id);
        realm.parent_race_id = parent_race;
        realm.capital_uid = capital->uid;
        for (const HeroCity* city : members) realm.city_uids.push_back(city->uid);
        realm.culture_id = culture.id;
        realms.push_back(std::move(realm));
    }
    return realms;
}

// City uid -> realm. Built by iterating realms then their city_uids, so a city in two
// cultures resolves to the LAST realm written.
std::map<std::string, std::size_t> realm_by_city(const std::vector<HeroRealm>& realms) {
    std::map<std::string, std::size_t> out;
    for (std::size_t i = 0; i < realms.size(); ++i) {
        for (const std::string& uid : realms[i].city_uids) out[uid] = i;
    }
    return out;
}

std::string realm_uid_of(const std::map<std::string, std::size_t>& by_city,
                         const std::vector<HeroRealm>& realms, const std::string& city_uid) {
    auto it = by_city.find(city_uid);
    return it == by_city.end() ? std::string() : realms[it->second].uid;
}

// ---------------------------------------------------------------------------
// Well 1 -- ruins. A lost city may leave an heir, a war a remembered victor, a beast kill
// a Dread. Nothing here invents an event.
// ---------------------------------------------------------------------------

std::vector<HeroDeed> war_deeds_of(const std::vector<HeroWarRecord>& history) {
    std::vector<HeroDeed> deeds;
    for (const HeroWarRecord& entry : history) {
        HeroDeed deed;
        deed.event_kind = "war";
        deed.event_id = entry.war_id;
        deed.age = entry.age;
        deed.role = entry.outcome == "victor" ? "credited" : "victim";
        deed.kind = entry.kind;
        deed.opponent_uid = entry.opponent_uid;
        deeds.push_back(std::move(deed));
    }
    return deeds;
}

Hero build_pretender(Ctx& ctx, const HeroRuin& ruin,
                     const std::vector<const HeroCity*>& same_people,
                     const std::map<std::string, std::size_t>& by_city,
                     const std::vector<HeroRealm>& realms) {
    const int age = ruin.destroyed_age;
    const HeroCity* home = nearest_city(ruin.direction, ruin.has_direction, same_people, ctx.radius);
    const bool alive = home != nullptr && ctx.final_age - age <= 1;
    std::vector<HeroDeed> war = war_deeds_of(ruin.war_history);

    Hero p;
    p.uid = "hero-pretender-" + ruin.uid;
    p.role = "pretender";
    p.well = "ruins";
    p.civilization_id = ruin.civilization_id;
    p.race_id = parent_race_of(ctx, ruin.parent_race_id, ruin.population_profile);
    p.born_age = age;
    p.status = alive ? "living" : "legend";
    if (alive) {
        p.home = city_ref(*home);
        p.presence = HeroPresence{"city", home->uid, "court"};
        p.realm_uid = realm_uid_of(by_city, realms, home->uid);
    }
    p.lost = lost_ref(ruin);
    HeroDeed heir;
    heir.event_kind = "ruin";
    heir.event_id = ruin.id;
    heir.age = age;
    heir.role = "heir";
    p.deeds.push_back(heir);
    p.deeds.insert(p.deeds.end(), war.begin(), war.end());
    p.claim = {"retake", ruin.uid, ruin.name};

    p.features = {"role:pretender", "home:fell", "stake:lost", "heir:" + ruin.city_class};
    bool credited = false, victim = false;
    for (const HeroDeed& deed : war) {
        credited |= deed.role == "credited";
        victim |= deed.role == "victim";
    }
    if (credited && victim) p.features.insert("sides:both");
    auto lost_count = ctx.lost_per_civilization.find(ruin.civilization_id);
    if (lost_count != ctx.lost_per_civilization.end() && lost_count->second >= 2) {
        p.features.insert("civ:lost_many");
    }
    if (contains(ctx.words->beast_causes, ruin.cause) && !ruin.evidence_nest_id.empty() &&
        ctx.nest_by_id.count(ruin.evidence_nest_id)) {
        p.features.insert("dread:living");
    }
    return p;
}

// A realm is contested while any of its cities has a living former opponent.
bool realm_contested(const Ctx& ctx, const HeroCity& home,
                     const std::map<std::string, std::size_t>& by_city,
                     const std::vector<HeroRealm>& realms) {
    std::vector<const HeroCity*> members;
    auto it = by_city.find(home.uid);
    if (it != by_city.end()) {
        for (const std::string& uid : realms[it->second].city_uids) {
            auto found = ctx.city_by_uid.find(uid);
            if (found != ctx.city_by_uid.end()) members.push_back(found->second);
        }
    } else {
        members.push_back(&home);
    }
    for (const HeroCity* city : members) {
        for (const HeroWarRecord& entry : city->war_history) {
            if (!entry.opponent_uid.empty() && ctx.city_by_uid.count(entry.opponent_uid)) return true;
        }
    }
    return false;
}

void fold_warlord(Ctx& ctx, const HeroWar& war, const HeroRuin& ruin,
                  std::map<std::string, Hero>& warlords,
                  const std::map<std::string, std::size_t>& by_city,
                  const std::vector<HeroRealm>& realms) {
    const std::string key = std::to_string(war.age) + "-" + war.victor_uid;
    auto found = ctx.city_by_uid.find(war.victor_uid);
    const HeroCity* home = found != ctx.city_by_uid.end() ? found->second : nullptr;
    if (!warlords.count(key)) {
        const bool alive = home != nullptr && ctx.final_age - war.age <= 1;
        Hero p;
        p.uid = "hero-warlord-" + key;
        p.role = "warlord";
        p.well = "ruins";
        p.civilization_id = war.civilization_id;
        p.race_id = !war.victor_parent_race_id.empty()
                        ? war.victor_parent_race_id
                        : (home != nullptr ? parent_race_of(ctx, home->parent_race_id, home->population_profile)
                                           : parent_race_of(ctx, ruin.parent_race_id, ruin.population_profile));
        p.born_age = war.age;
        p.status = alive ? "living" : "legend";
        p.features = {"role:warlord", "war:" + war.kind + "_victor"};
        if (home == nullptr) p.features.insert("home:fell_later");
        if (home != nullptr && realm_contested(ctx, *home, by_city, realms)) {
            p.features.insert("realm:contested");
        }
        auto lost_count = ctx.lost_per_civilization.find(war.civilization_id);
        if (lost_count != ctx.lost_per_civilization.end() && lost_count->second >= 2) {
            p.features.insert("civ:lost_many");
        }
        if (home != nullptr) {
            bool victor = false, defeated = false;
            for (const HeroWarRecord& entry : home->war_history) {
                victor |= entry.outcome == "victor";
                defeated |= entry.outcome == "defeated";
            }
            if (victor && defeated) p.features.insert("sides:both");
        }
        std::string victor_name;
        if (home != nullptr) {
            victor_name = home->name;
        } else {
            auto fallen = ctx.ruin_by_uid.find(war.victor_uid);
            victor_name = fallen != ctx.ruin_by_uid.end() ? fallen->second->name : war.victor_uid;
        }
        if (alive) {
            p.home = city_ref(*home);
            // 'command', which the port specification never names anywhere. An unknown
            // situation silently costs every warlord its `presence:` fact downstream.
            p.presence = HeroPresence{"city", home->uid, "command"};
            p.realm_uid = realm_uid_of(by_city, realms, home->uid);
        }
        p.victor_uid = war.victor_uid;
        p.victor_name = victor_name;
        p.victor_fell = home == nullptr;
        p.claim = {"hold", war.victor_uid, victor_name};
        warlords.emplace(key, std::move(p));
    }
    Hero& person = warlords.at(key);
    HeroDeed deed;
    deed.event_kind = "war";
    deed.event_id = war.id;
    deed.age = war.age;
    deed.role = "credited";
    deed.kind = war.kind;
    deed.defeated_uid = ruin.uid;
    deed.defeated_name = ruin.name;
    deed.defeated_class = ruin.city_class;
    person.deeds.push_back(std::move(deed));
    person.defeated.push_back({ruin.uid, ruin.name, war.id, war.kind});
}

void fold_dread(Ctx& ctx, const HeroRuin& ruin, std::map<std::string, HeroDread>& dreads) {
    const std::string& nest_id = ruin.evidence_nest_id;
    if (nest_id.empty()) return;
    auto found = ctx.nest_by_id.find(nest_id);
    const HeroNest* nest = found != ctx.nest_by_id.end() ? found->second : nullptr;
    auto it = dreads.find(nest_id);
    if (it == dreads.end()) {
        HeroDread d;
        d.uid = "dread-" + nest_id;
        d.nest_id = nest_id;
        d.species = nest != nullptr ? nest->name : species_from_nest_id(nest_id);
        d.family = nest != nullptr ? nest->family : std::string();
        d.nest_tier = nest != nullptr ? (nest->has_tier ? nest->tier : 5) : 5;
        d.status = nest != nullptr ? "living" : "legend";
        if (nest != nullptr) d.presence = HeroPresence{"nest", nest_id, "lair"};
        d.features = {"role:dread"};
        it = dreads.emplace(nest_id, std::move(d)).first;
    }
    HeroDread& dread = it->second;
    HeroDeed deed;
    deed.event_kind = "ruin";
    deed.event_id = ruin.id;
    deed.age = ruin.destroyed_age;
    deed.role = "actual";
    deed.cause = ruin.cause;
    deed.city_name = ruin.name;
    deed.city_class = ruin.city_class;
    dread.deeds.push_back(std::move(deed));
    dread.destroyed.push_back({ruin.uid, ruin.name, ruin.destroyed_age});
    if (!dread.family.empty() && contains(ctx.words->long_lived_families, dread.family)) {
        dread.features.insert("dread:long_lived");
    }
    std::set<int> ages;
    for (const HeroDeed& d : dread.deeds) ages.insert(d.age);
    if (ages.size() >= 2) dread.features.insert("dread:ages_2");
}

void run_ruins_well(Ctx& ctx, const std::map<std::string, std::size_t>& by_city,
                    const std::vector<HeroRealm>& realms, std::vector<Hero>& people,
                    std::vector<HeroDread>& dreads_out, std::vector<HeroRoll>& rolls) {
    const HeroWellsPolicy& w = *ctx.wells;
    std::vector<Hero> heirs;
    std::map<std::string, Hero> warlords;
    std::map<std::string, HeroDread> dreads;

    for (const HeroRuin& ruin : ctx.req->ruins) {
        std::vector<const HeroCity*> same;
        for (const HeroCity& city : ctx.req->cities) {
            if (city.population_profile == ruin.population_profile) same.push_back(&city);
        }
        heirs.push_back(build_pretender(ctx, ruin, same, by_city, realms));
        auto war = ctx.war_by_defeated.find(ruin.uid);
        if (war != ctx.war_by_defeated.end()) {
            fold_warlord(ctx, *war->second, ruin, warlords, by_city, realms);
        }
        if (contains(ctx.words->beast_causes, ruin.cause)) fold_dread(ctx, ruin, dreads);
    }

    for (Hero& heir : heirs) {
        const std::string& klass = heir.lost->city_class;
        const double bonus = klass == "capital"   ? w.heir_per_city_class.capital
                             : klass == "medium"  ? w.heir_per_city_class.medium
                                                  : w.heir_per_city_class.small;
        if (roll_once(heir.uid, heir.deeds, "heir", chance_of(w.heir_base + bonus),
                      ctx.req->seed, rolls)) {
            people.push_back(std::move(heir));
        }
    }

    if (w.warlord_one_per_city) {
        // A city remembers one warlord: the most recent victor. Earlier victors' wars stay
        // in the city's history and the survivor's deeds; they do not each precipitate a
        // person. The comparison is the tuple (born_age, key), which mixes a numeric and a
        // lexicographic component on purpose.
        std::map<std::string, std::string> latest;
        for (const auto& [key, person] : warlords) {
            auto it = latest.find(person.victor_uid);
            if (it == latest.end()) {
                latest.emplace(person.victor_uid, key);
                continue;
            }
            const Hero& held = warlords.at(it->second);
            if (person.born_age > held.born_age ||
                (person.born_age == held.born_age && key > it->second)) {
                it->second = key;
            }
        }
        std::map<std::string, Hero> kept;
        for (const auto& [victor, key] : latest) kept.emplace(key, std::move(warlords.at(key)));
        warlords = std::move(kept);
    }
    for (auto& [key, person] : warlords) {
        double worst = 0.0;
        for (const HeroDeed& deed : person.deeds) {
            const double value = deed.kind == "international" ? w.warlord_international
                                 : deed.kind == "regional"    ? w.warlord_regional
                                 : deed.kind == "civil"       ? w.warlord_civil
                                                              : 0.0;
            worst = std::max(worst, value);
        }
        const double extra = w.warlord_per_extra_war * static_cast<double>(person.deeds.size() - 1);
        if (roll_once(person.uid, person.deeds, "warlord", chance_of(w.warlord_base + worst + extra),
                      ctx.req->seed, rolls)) {
            people.push_back(std::move(person));
        }
    }
    for (auto& [nest_id, dread] : dreads) {
        const double value = w.dread_base + w.dread_per_nest_tier * static_cast<double>(dread.nest_tier);
        if (roll_once(dread.uid, dread.deeds, "dread", chance_of(value), ctx.req->seed, rolls)) {
            dreads_out.push_back(std::move(dread));
        }
    }
}

// ---------------------------------------------------------------------------
// Well 2 -- magic. Key points keep wardens, dead colleges leave masters, living colleges
// have heads.
// ---------------------------------------------------------------------------

// The layer key with its `ley_` prefix REMOVED.
//
// Returning the raw key would (a) never match `dark_schools`, so no college magister is
// ever `school:dark` and the Deceiver card becomes unreachable, (b) render "the ley_umbral
// key point at X" in every claim and hook, and (c) never match `opposites`, killing the
// second ley hook. Ties go to the alphabetically first school, because the scan is a
// STRICT greater-than over the sorted layer keys.
std::string dominant_school(const Ctx& ctx, const HeroCollege& college) {
    const HeroWordTables& words = *ctx.words;
    std::string best(words.default_school);
    double best_value = -1.0;
    if (ctx.req->layers == nullptr) return best;
    for (const auto& [key, grid] : *ctx.req->layers) {
        if (!starts_with(key, "ley_") || contains(words.school_scoring_excludes, key)) continue;
        if (grid.empty() || college.x < 0 || college.z < 0 || college.x >= grid.size() ||
            college.z >= grid.size()) {
            continue;  // every ley read is wrapped and swallowed; it costs one feature
        }
        const double value = grid.at(college.x, college.z);
        if (value > best_value) {
            best = key.substr(4);
            best_value = value;
        }
    }
    return best;
}

void run_magic_well(Ctx& ctx, const std::map<std::string, std::size_t>& by_city,
                    const std::vector<HeroRealm>& realms, std::vector<Hero>& people,
                    std::vector<HeroRoll>& rolls) {
    const HeroWellsPolicy& w = *ctx.wells;
    for (const HeroRuin& ruin : ctx.req->ruins) {
        const std::string key_id = ruin.id + "-key";
        auto node = ctx.ley_by_node.find(key_id);
        if (node != ctx.ley_by_node.end()) {
            const std::string& school = node->second.first;
            const HeroLeyNodeRef& ley = *node->second.second;
            const int age = ruin.destroyed_age;
            Hero p;
            p.uid = "hero-domain-" + ruin.uid;
            p.role = "domain_holder";
            p.well = "magic";
            p.civilization_id = ruin.civilization_id;
            p.race_id = parent_race_of(ctx, ruin.parent_race_id, ruin.population_profile);
            p.born_age = age;
            p.status = ctx.final_age - age <= 1 ? "living" : "legend";
            if (p.status == "living") p.presence = HeroPresence{"ruin", ruin.uid, "warding"};
            p.lost = lost_ref(ruin);
            p.school = school;
            p.node_id = ley.id;
            HeroDeed deed;
            deed.event_kind = "ruin";
            deed.event_id = ruin.id;
            deed.age = age;
            deed.role = std::string(ctx.words->deed_keeper);
            p.deeds.push_back(std::move(deed));
            p.claim = {"hold", ley.id,
                       "the " + school + " key point at " + hero_short_name(ruin.name)};
            p.features = {"role:domain_holder", "keypoint:ruin_born", "seat:none"};
            if (contains(hero_dark_schools(), school)) p.features.insert("school:dark");
            const double value = w.domain_holder_base + w.domain_holder_per_intensity * ley.intensity;
            if (roll_once(p.uid, p.deeds, "domain_holder", chance_of(value), ctx.req->seed, rolls)) {
                people.push_back(std::move(p));
            }
        }
        if (ruin.cause == ctx.words->self_magic_cause) {
            const std::string school = !ruin.new_node_school.empty()
                                           ? ruin.new_node_school
                                           : std::string(ctx.words->default_school);
            const bool keyed = ctx.ley_by_node.count(key_id) != 0;
            const int age = ruin.destroyed_age;
            Hero p;
            p.uid = "hero-magister-" + ruin.uid;
            p.role = "magister";
            p.well = "magic";
            p.civilization_id = ruin.civilization_id;
            p.race_id = parent_race_of(ctx, ruin.parent_race_id, ruin.population_profile);
            p.born_age = age;
            p.status = ctx.final_age - age <= 1 ? "living" : "legend";
            p.dispossessed = true;
            p.lost = lost_ref(ruin);
            p.school = school;
            p.node_id = keyed ? key_id : std::string();
            HeroDeed deed;
            deed.event_kind = "ruin";
            deed.event_id = ruin.id;
            deed.age = age;
            deed.role = "actual";
            p.deeds.push_back(std::move(deed));
            p.claim = {"exploit", keyed ? key_id : ruin.uid,
                       keyed ? "the " + school + " key point at " + hero_short_name(ruin.name)
                             : ruin.name};
            p.features = {"role:magister", "college:destroyed", "stake:lost"};
            if (contains(hero_dark_schools(), school)) p.features.insert("school:dark");
            if (roll_once(p.uid, p.deeds, "magister", chance_of(w.magister_base), ctx.req->seed, rolls)) {
                people.push_back(std::move(p));
            }
        }
    }
    for (const HeroCollege& college : ctx.req->colleges) {
        auto found = ctx.city_by_id.find(college.core_id);
        if (found == ctx.city_by_id.end()) continue;
        const HeroCity& home = *found->second;
        const std::string school = dominant_school(ctx, college);
        Hero p;
        p.uid = "hero-magister-" + college.id;
        p.role = "magister";
        p.well = "magic";
        p.civilization_id = home.civilization_id;
        p.race_id = parent_race_of(ctx, home.parent_race_id, home.population_profile);
        p.born_age = std::max(1, ctx.final_age);
        p.status = "living";
        p.home = city_ref(home);
        p.presence = HeroPresence{"college", college.id, "teaching"};
        p.realm_uid = realm_uid_of(by_city, realms, home.uid);
        p.school = school;
        p.college = HeroCollegeRef{college.id, college.node, college.x, college.z};
        p.claim = {"hold", college.id, "the college near " + hero_short_name(home.name)};
        p.features = {"role:magister", "seat:college"};
        if (contains(hero_dark_schools(), school)) p.features.insert("school:dark");
        if (roll_once(p.uid, p.deeds, "college_magister", chance_of(w.college_magister_base),
                      ctx.req->seed, rolls, college.id, ctx.final_age, true)) {
            people.push_back(std::move(p));
        }
    }
}

// ---------------------------------------------------------------------------
// Well 3 -- cities. Sovereigns, councils, dynasties and the leaders a diaspora carries.
// ---------------------------------------------------------------------------

int cold_conflicts(const Ctx& ctx, const HeroCity& city) {
    int count = 0;
    for (const HeroWarRecord& entry : city.war_history) {
        if (!entry.opponent_uid.empty() && ctx.city_by_uid.count(entry.opponent_uid)) ++count;
    }
    return count;
}

void run_cities_well(Ctx& ctx, std::vector<HeroRealm>& realms,
                     const std::map<std::string, std::size_t>& by_city, std::vector<Hero>& people,
                     std::vector<HeroRoll>& rolls, std::vector<HeroOrg>& orgs) {
    const HeroWellsPolicy& w = *ctx.wells;
    std::map<std::string, int> trade;
    for (const HeroShipment& shipment : ctx.req->shipments) {
        ++trade[shipment.from];
        ++trade[shipment.to];
    }

    for (HeroRealm& realm : realms) {
        auto found = ctx.city_by_uid.find(realm.capital_uid);
        if (found == ctx.city_by_uid.end()) continue;
        const HeroCity& capital = *found->second;
        int cold = 0;
        for (const std::string& uid : realm.city_uids) {
            auto member = ctx.city_by_uid.find(uid);
            if (member != ctx.city_by_uid.end()) cold += cold_conflicts(ctx, *member->second);
        }
        Hero p;
        p.uid = "hero-sovereign-" + capital.uid;
        p.role = "sovereign";
        p.well = "cities";
        p.civilization_id = capital.civilization_id;
        p.race_id = parent_race_of(ctx, capital.parent_race_id, capital.population_profile);
        p.born_age = std::max(1, ctx.final_age);
        p.status = "living";
        p.home = city_ref(capital);
        p.presence = HeroPresence{"city", capital.uid, "throne"};
        p.realm_uid = realm.uid;
        p.deeds = war_deeds_of(capital.war_history);
        p.claim = {"hold", realm.uid, realm.name};
        p.features = {"role:sovereign"};
        if (cold) p.features.insert("realm:contested");
        if (cold >= 2) p.features.insert("realm:cold_conflicts_2");
        if (capital.founding_capital) p.features.insert("dynasty:founding");
        if (roll_once(p.uid, p.deeds, "sovereign", chance_of(w.sovereign_base), ctx.req->seed, rolls,
                      capital.uid, ctx.final_age, true)) {
            realm.sovereign_uid = p.uid;
            if (capital.founding_capital) {
                HeroOrg org;
                org.uid = "dynasty-" + capital.uid;
                org.kind = "dynasty";
                org.name = "the line of " + hero_short_name(capital.name);
                org.home = city_ref(capital);
                org.members.push_back(p.uid);
                org.holds = realm.uid;
                orgs.push_back(std::move(org));
            }
            people.push_back(std::move(p));
        }
    }

    for (const HeroCity& city : ctx.req->cities) {
        std::vector<std::string> members;
        for (const std::string_view seat : hero_seats_for(city.city_class)) {
            Hero p;
            p.uid = "hero-council-" + city.uid + "-" + std::string(seat);
            p.role = "council";
            p.seat = std::string(seat);
            p.well = "cities";
            p.civilization_id = city.civilization_id;
            p.race_id = parent_race_of(ctx, city.parent_race_id, city.population_profile);
            p.born_age = std::max(1, ctx.final_age);
            p.status = "living";
            p.home = city_ref(city);
            p.presence = HeroPresence{"city", city.uid, "council"};
            p.realm_uid = realm_uid_of(by_city, realms, city.uid);
            p.claim = {seat == "market_steward" ? "exploit" : "hold", city.uid, city.name};
            p.features = {"role:council", "seat:" + std::string(seat)};
            if (seat == "market_steward") {
                p.features.insert("seat:trade");
                auto endpoints = trade.find(city.id);
                if (endpoints != trade.end() && endpoints->second >= 2) p.features.insert("routes:2");
            }
            if (cold_conflicts(ctx, city)) {
                p.features.insert("role:schemer");
                auto realm = by_city.find(city.uid);
                if (realm != by_city.end() && realms[realm->second].city_uids.size() >= 2) {
                    p.features.insert("stakes:2");
                }
            }
            const double value =
                w.council_base + (city.city_class == "capital" ? w.council_capital_bonus : 0.0);
            if (roll_once(p.uid, p.deeds, "council", chance_of(value), ctx.req->seed, rolls, city.uid,
                          ctx.final_age, true)) {
                members.push_back(p.uid);
                people.push_back(std::move(p));
            }
        }
        if (!members.empty()) {
            HeroOrg org;
            org.uid = "council-" + city.uid;
            org.kind = "council";
            org.name = "the council of " + hero_short_name(city.name);
            org.home = city_ref(city);
            org.members = members;
            org.holds = city.uid;
            orgs.push_back(std::move(org));
        }

        const std::string_view mapped = hero_word(ctx.words->diaspora_roles, city.diaspora_reason);
        if (mapped.empty()) continue;
        std::string role(mapped);
        std::vector<const HeroCity*> mother_alive;
        for (const HeroCity& other : ctx.req->cities) {
            if (other.civilization_id == city.source_civilization_id) mother_alive.push_back(&other);
        }
        if (role == "prophet" && !mother_alive.empty()) role = "heresiarch";
        const HeroCity* mother = nullptr;
        for (const HeroCity* candidate : mother_alive) {
            if (candidate->city_class == "capital") {
                mother = candidate;
                break;
            }
        }
        if (mother == nullptr && !mother_alive.empty()) mother = mother_alive.front();
        const int born = std::max(1, city.founded_age != 0 ? city.founded_age : 1);
        const bool alive = ctx.final_age - born <= 1;

        Hero p;
        p.uid = "hero-" + role + "-" + city.uid;
        p.role = role;
        p.well = "cities";
        p.diaspora_reason = city.diaspora_reason;
        p.civilization_id = city.civilization_id;
        p.race_id = parent_race_of(ctx, city.parent_race_id, city.population_profile);
        p.born_age = born;
        p.status = alive ? "living" : "legend";
        if (alive) {
            p.home = city_ref(city);
            p.presence = HeroPresence{
                "city", city.uid, role == "prophet" || role == "heresiarch" ? "pulpit" : "hall"};
            p.realm_uid = realm_uid_of(by_city, realms, city.uid);
        }
        p.mother_civilization_id = city.source_civilization_id;
        HeroDeed deed;
        deed.event_kind = "founding";
        deed.event_id = "founding-" + std::to_string(city.node);
        deed.age = born;
        deed.role = "credited";
        deed.reason = city.diaspora_reason;
        p.deeds.push_back(deed);
        p.claim = role == "heresiarch" && mother != nullptr
                      ? HeroClaim{"convert", mother->uid, mother->name}
                      : HeroClaim{"protect", city.uid, city.name};
        p.features = {"role:" + role};
        // The roll is minted under the PERSON'S own word, not the policy key. `heresiarch`
        // is one of those words and the diagnostics table maps only three of the four, so
        // a precipitated heresiarch is counted nowhere. Ported as it stands; see the report.
        if (roll_once(p.uid, p.deeds, role, chance_of(w.diaspora_base), ctx.req->seed, rolls,
                      deed.event_id, born, true)) {
            people.push_back(std::move(p));
        }
    }
}

// ---------------------------------------------------------------------------
// Well 4 -- guilds. Every guild hall is an Order, and an Order under threat has a Champion.
// ---------------------------------------------------------------------------

HeroOrg build_order(const std::string& uid, const std::string& name, const std::string& kind,
                    const HeroNest* nest, std::string_view fallback_family) {
    HeroOrg org;
    org.uid = kind + "-" + uid;
    org.kind = kind;
    org.name = "the Order of " + hero_short_name(name);
    org.home = {kind == "order" ? "city" : "ruin", uid, name};
    org.against = nest != nullptr && !nest->family.empty() ? nest->family : std::string(fallback_family);
    org.nest_id = nest != nullptr ? nest->id : std::string();
    return org;
}

void run_guilds_well(Ctx& ctx, const std::map<std::string, std::size_t>& by_city,
                     const std::vector<HeroRealm>& realms, std::vector<Hero>& people,
                     std::vector<HeroRoll>& rolls, std::vector<HeroOrg>& orgs) {
    const HeroWellsPolicy& w = *ctx.wells;
    std::vector<const HeroNest*> dangerous;
    for (const HeroNest& nest : ctx.req->nests) {
        if (dangerous_nest(nest, *ctx.words)) dangerous.push_back(&nest);
    }

    for (const HeroCity& city : ctx.req->cities) {
        const HeroNest* nearest = nullptr;
        double distance = 0.0;
        for (const HeroNest* nest : dangerous) {
            const double d = hero_separation_m(city.direction, nest->direction, ctx.radius);
            if (nearest == nullptr || d < distance || (d == distance && nest->id < nearest->id)) {
                nearest = nest;
                distance = d;
            }
        }
        // The guilds well uses a DICT DEFAULT, not an `or`: a nest with `range_m: 0` gets
        // reach 0 and admits no city at all, where the reeve well's `or` would read 1000.
        // The two wells genuinely differ and unifying them changes which cities admit a
        // champion.
        const double reach = nearest != nullptr && nearest->has_range ? nearest->range_m : 1000.0;
        if (nearest == nullptr || distance > w.champion_reach_factor * reach) continue;

        const bool dark = contains(ctx.words->dark_families, nearest->family);
        Hero p;
        p.uid = "hero-champion-" + city.uid;
        p.role = "champion";
        p.well = "guilds";
        p.civilization_id = city.civilization_id;
        p.race_id = parent_race_of(ctx, city.parent_race_id, city.population_profile);
        p.born_age = std::max(1, ctx.final_age);
        p.status = "living";
        p.home = city_ref(city);
        p.presence = HeroPresence{"city", city.uid, "guildhall"};
        p.realm_uid = realm_uid_of(by_city, realms, city.uid);
        p.nest_id = nearest->id;
        p.nest_name = nearest->name;
        HeroDeed deed;
        deed.event_kind = "nest";
        deed.event_id = nearest->id;
        deed.age = std::max(1, ctx.final_age);
        deed.role = "credited";
        deed.tier = nearest->has_tier ? nearest->tier : 1;
        deed.has_tier = true;
        p.deeds.push_back(deed);
        p.claim = {"kill", nearest->id,
                   "the " + lowercase(nearest->name) + " near " + hero_short_name(city.name)};
        p.features = {"role:champion", "order:member", "warden:post"};
        if (dark) {
            p.features.insert("order:against_dark");
            p.features.insert("deed:dark_nest");
        }
        // The org is emitted whether or not the champion precipitates; an empty one is
        // dropped at the very end. That is how a world records that a guild hall stands
        // against a lair with nobody named to hold it.
        orgs.push_back(build_order(city.uid, city.name, "order", nearest, "beast"));
        const double tier = static_cast<double>(nearest->has_tier ? nearest->tier : 1);
        if (roll_once(p.uid, p.deeds, "champion", chance_of(w.champion_base + w.champion_per_nest_tier * tier),
                      ctx.req->seed, rolls, nearest->id, ctx.final_age, true)) {
            orgs.back().members.push_back(p.uid);
            people.push_back(std::move(p));
        }
    }

    for (const HeroRuin& ruin : ctx.req->ruins) {
        if (!contains(ctx.words->beast_causes, ruin.cause)) continue;
        auto found = ctx.nest_by_id.find(ruin.evidence_nest_id);
        const HeroNest* nest =
            ruin.evidence_nest_id.empty() || found == ctx.nest_by_id.end() ? nullptr : found->second;
        const int age = ruin.destroyed_age;
        const std::string name = nest != nullptr ? nest->name : "the beast";
        Hero p;
        p.uid = "hero-champion-" + ruin.uid;
        p.role = "champion";
        p.well = "guilds";
        p.civilization_id = ruin.civilization_id;
        p.race_id = parent_race_of(ctx, ruin.parent_race_id, ruin.population_profile);
        p.born_age = age;
        p.status = ctx.final_age - age <= 1 ? "living" : "legend";
        p.dispossessed = true;
        p.lost = lost_ref(ruin);
        p.nest_id = ruin.evidence_nest_id.empty() ? std::string() : ruin.evidence_nest_id;
        p.nest_name = name;
        HeroDeed deed;
        deed.event_kind = "ruin";
        deed.event_id = ruin.id;
        deed.age = age;
        deed.role = "credited";
        deed.tier = nest != nullptr ? (nest->has_tier ? nest->tier : 5) : 5;
        deed.has_tier = true;
        p.deeds.push_back(deed);
        p.claim = {"kill", p.nest_id.empty() ? ruin.uid : p.nest_id,
                   lowercase(name) + ", the doom of " + hero_short_name(ruin.name)};
        p.features = {"role:champion", "order:member", "order:remnant", "defence:failed", "stake:lost"};
        if (nest != nullptr) p.features.insert("dread:failed_against");
        if (nest != nullptr && contains(ctx.words->dark_families, nest->family)) {
            p.features.insert("order:against_dark");
            p.features.insert("deed:dark_nest");
        }
        // The remnant's charter falls back to family `beast` with a null nest id when the
        // evidence names no placed nest -- and its home kind is `ruin`, not `city`.
        orgs.push_back(build_order(ruin.uid, ruin.name, "remnant", nest, "beast"));
        const double tier = static_cast<double>(deed.tier);
        if (roll_once(p.uid, p.deeds, "champion", chance_of(w.champion_base + w.champion_per_nest_tier * tier),
                      ctx.req->seed, rolls)) {
            orgs.back().members.push_back(p.uid);
            people.push_back(std::move(p));
        }
    }
}

// ---------------------------------------------------------------------------
// Well 5 -- countryside. Distances here are GRID metres, never great-circle, because
// hamlets and fortresses carry no globe direction.
// ---------------------------------------------------------------------------

std::string first_token(const std::string& text) {
    const std::size_t space = text.find(' ');
    return space == std::string::npos ? text : text.substr(0, space);
}

void run_countryside_well(Ctx& ctx, const std::map<std::string, std::size_t>& by_city,
                          const std::vector<HeroRealm>& realms, const std::vector<HeroDread>& dreads,
                          std::vector<Hero>& people, std::vector<HeroRoll>& rolls) {
    const HeroWellsPolicy& w = *ctx.wells;
    std::map<std::string, double> deficit;
    for (const HeroRuralCore& core : ctx.req->cores) deficit[core.site_id] = core.food_deficit;
    std::map<std::string, double> pressure;
    for (const HeroCityThreat& threat : ctx.req->threats) pressure[threat.city_uid] = threat.war_pressure;
    std::vector<const HeroNest*> dangerous;
    for (const HeroNest& nest : ctx.req->nests) {
        if (dangerous_nest(nest, *ctx.words)) dangerous.push_back(&nest);
    }
    // A living dread's lair threatens every hamlet within the reach it razed cities from
    // (2026-09-23), and it is the reeve's nest ahead of any other: the monster with a name is
    // the one the fields fear. The well's own reach is metres of the reference world, so on
    // a shipped world it finds no lair at all; the dread's is scaled to the world.
    std::vector<std::pair<const HeroNest*, double>> dread_lairs;
    for (const HeroDread& dread : dreads) {
        if (dread.status != "living") continue;
        const auto found = ctx.nest_by_id.find(dread.nest_id);
        if (found == ctx.nest_by_id.end() || !found->second->has_grid) continue;
        const HeroNest& lair = *found->second;
        dread_lairs.emplace_back(&lair, nest_threat_reach_m(lair.has_range ? lair.range_m : 350.0, ctx.radius));
    }

    for (const HeroHamlet& hamlet : ctx.req->hamlets) {
        auto found = ctx.city_by_id.find(hamlet.core_id);
        if (found == ctx.city_by_id.end() || hamlet.kind != "hamlet" ||
            hamlet.role.find("harbor") != std::string::npos || starts_with(hamlet.id, "coastal")) {
            continue;  // harbours belong to the ports well
        }
        const HeroCity& city = *found->second;
        const HeroNest* nearest = nullptr;
        double distance = 0.0;
        for (const HeroNest* nest : dangerous) {
            if (!nest->has_grid) continue;  // a nest with no x/z is invisible to a grid search
            const double d = grid_metres(hamlet.x, hamlet.z, nest->x, nest->z, ctx.spacing);
            if (nearest == nullptr || d < distance || (d == distance && nest->id < nearest->id)) {
                nearest = nest;
                distance = d;
            }
        }
        // `or` semantics here, unlike the guilds well: a nest with range 0 reads 1000.
        const double reach = nearest != nullptr && nearest->has_range && nearest->range_m != 0.0
                                 ? nearest->range_m
                                 : 1000.0;
        // The nearest living dread's lair in reach, ties to the lowest nest id, displaces it.
        const HeroNest* lair = nullptr;
        double lair_distance = 0.0;
        for (const auto& [candidate, lair_reach] : dread_lairs) {
            const double d = grid_metres(hamlet.x, hamlet.z, candidate->x, candidate->z, ctx.spacing);
            if (d > lair_reach) continue;
            if (lair == nullptr || d < lair_distance || (d == lair_distance && candidate->id < lair->id)) {
                lair = candidate;
                lair_distance = d;
            }
        }
        if (lair != nullptr) nearest = lair;
        const bool threatened = lair != nullptr || (nearest != nullptr && distance <= w.reeve_reach_factor * reach);
        const double starving = deficit.count(hamlet.core_id) ? deficit[hamlet.core_id] : 0.0;
        const std::string word(hero_word(ctx.words->role_word, first_token(hamlet.role),
                                         ctx.words->role_word_default));
        const std::string site_name = "the " + word + " below " + hero_short_name(city.name);
        const std::string key = hero_small_site_anchor("hamlet", hamlet.node, hamlet.id);

        Hero p;
        p.uid = "hero-reeve-" + key;
        p.role = "reeve";
        p.well = "hamlets";
        p.civilization_id = city.civilization_id;
        p.race_id = parent_race_of(ctx, city.parent_race_id, city.population_profile);
        p.born_age = std::max(1, ctx.final_age);
        p.status = "living";
        p.home = city_ref(city);
        p.site_name = site_name;
        p.presence = HeroPresence{"hamlet", key, "reeve"};
        p.realm_uid = realm_uid_of(by_city, realms, city.uid);
        if (threatened) {
            p.nest_id = nearest->id;
            p.nest_name = nearest->name;
        }
        HeroDeed deed;
        deed.event_kind = "hamlet";
        deed.event_id = key;
        deed.age = std::max(1, ctx.final_age);
        deed.role = "keeper";
        p.deeds.push_back(deed);
        p.claim = {"protect", key, site_name};
        const std::string role_word = first_token(hamlet.role);
        p.features = {"role:reeve", "hamlet:" + (role_word.empty() ? std::string("farming") : role_word)};
        if (starving > 0.0) p.features.insert("hamlet:starving");
        if (threatened) p.features.insert("hamlet:threatened");
        const double value = w.reeve_base + (starving > 0.0 ? w.reeve_starving_bonus : 0.0) +
                             (threatened ? w.reeve_threatened_bonus : 0.0);
        if (roll_once(p.uid, p.deeds, "reeve", chance_of(value), ctx.req->seed, rolls)) {
            people.push_back(std::move(p));
        }
    }

    for (const HeroFortress& fortress : ctx.req->fortresses) {
        auto found = ctx.city_by_id.find(fortress.core_id);
        if (found == ctx.city_by_id.end()) continue;
        const HeroCity& city = *found->second;
        const bool pressed = (pressure.count(city.uid) ? pressure[city.uid] : 0.0) > 0.0;
        const std::string site_name = "the fortress above " + hero_short_name(city.name);
        const std::string key = hero_small_site_anchor("fortress", fortress.node, fortress.id);

        Hero p;
        p.uid = "hero-castellan-" + key;
        p.role = "castellan";
        p.well = "fortresses";
        p.civilization_id = city.civilization_id;
        p.race_id = parent_race_of(ctx, city.parent_race_id, city.population_profile);
        p.born_age = std::max(1, ctx.final_age);
        p.status = "living";
        p.home = city_ref(city);
        p.site_name = site_name;
        p.presence = HeroPresence{"fortress", key, "garrison"};
        p.realm_uid = realm_uid_of(by_city, realms, city.uid);
        HeroDeed deed;
        deed.event_kind = "fortress";
        deed.event_id = key;
        deed.age = std::max(1, ctx.final_age);
        deed.role = "keeper";
        p.deeds.push_back(deed);
        p.claim = {"hold", key, site_name};
        p.features = {"role:castellan", "order:member", "warden:post"};
        if (pressed) p.features.insert("fortress:pressed");
        const double value = w.castellan_base + (pressed ? w.castellan_pressed_bonus : 0.0) +
                             w.castellan_per_defence * fortress.defence_score;
        if (roll_once(p.uid, p.deeds, "castellan", chance_of(value), ctx.req->seed, rolls)) {
            people.push_back(std::move(p));
        }
    }
}

// ---------------------------------------------------------------------------
// Well 6 -- ports.
// ---------------------------------------------------------------------------

void run_ports_well(Ctx& ctx, const std::map<std::string, std::size_t>& by_city,
                    const std::vector<HeroRealm>& realms, std::vector<Hero>& people,
                    std::vector<HeroRoll>& rolls) {
    const HeroWellsPolicy& w = *ctx.wells;
    for (const HeroPort& port : ctx.req->ports) {
        auto found = ctx.city_by_id.find(port.core_id);
        if (found == ctx.city_by_id.end() || port.id.empty()) continue;
        const HeroCity& city = *found->second;
        int touching = 0;
        for (const HeroSeaRoute& route : ctx.req->sea_routes) {
            bool touches = false;
            for (const std::string& end : route.endpoints) {
                if (end == port.id || (port.node >= 0 && end == std::to_string(port.node)) ||
                    (port.sea_node >= 0 && end == std::to_string(port.sea_node))) {
                    touches = true;
                    break;
                }
            }
            if (touches) ++touching;
        }
        const std::string site_name = "the harbour of " + hero_short_name(city.name);
        Hero p;
        p.uid = "hero-harbourmaster-" + port.id;
        p.role = "harbourmaster";
        p.well = "ports";
        p.civilization_id = city.civilization_id;
        p.race_id = parent_race_of(ctx, city.parent_race_id, city.population_profile);
        p.born_age = std::max(1, ctx.final_age);
        p.status = "living";
        p.home = city_ref(city);
        p.site_name = site_name;
        p.routes = touching;
        p.has_routes = true;
        p.presence = HeroPresence{"port", port.id, "quay"};
        p.realm_uid = realm_uid_of(by_city, realms, city.uid);
        HeroDeed deed;
        deed.event_kind = "port";
        deed.event_id = port.id;
        deed.age = std::max(1, ctx.final_age);
        deed.role = "keeper";
        p.deeds.push_back(deed);
        p.claim = {"exploit", port.id, site_name};
        p.features = {"role:harbourmaster", "seat:trade"};
        if (touching >= 2 || (port.trade_terminal && touching >= 1)) p.features.insert("routes:2");
        if (port.trade_terminal) p.features.insert("port:terminal");
        const double value = w.harbourmaster_base + w.harbourmaster_per_route * touching +
                             (port.trade_terminal ? w.harbourmaster_terminal_bonus : 0.0);
        if (roll_once(p.uid, p.deeds, "harbourmaster", chance_of(value), ctx.req->seed, rolls)) {
            people.push_back(std::move(p));
        }
    }
}

// ---------------------------------------------------------------------------
// Well 7 -- shrines. A shrine keeps a keeper, a cult in a ruin keeps a heresiarch.
// ---------------------------------------------------------------------------

void run_shrines_well(Ctx& ctx, const std::map<std::string, std::size_t>& by_city,
                      const std::vector<HeroRealm>& realms, std::vector<Hero>& people,
                      std::vector<HeroRoll>& rolls) {
    const HeroWellsPolicy& w = *ctx.wells;
    std::map<std::string, const HeroGod*> gods;
    for (const HeroGod& god : ctx.req->gods) gods[god.id] = &god;
    const std::vector<const HeroCity*> living = all_cities(ctx);

    for (const HeroReligionSite& site : ctx.req->religion_sites) {
        const std::string_view mapped = hero_word(ctx.words->site_roles, site.kind);
        if (mapped.empty() || site.id.empty()) continue;
        auto found = ctx.ruin_by_id.find(site.ruin_id);
        if (found == ctx.ruin_by_id.end()) continue;
        const HeroRuin& ruin = *found->second;
        const std::string role(mapped);

        std::vector<const HeroCity*> same;
        for (const HeroCity& city : ctx.req->cities) {
            if (city.population_profile == ruin.population_profile) same.push_back(&city);
        }
        const HeroCity* home =
            same.empty() ? nullptr : nearest_city(ruin.direction, ruin.has_direction, same, ctx.radius);

        std::string god_name;
        auto god = gods.find(site.god_id);
        if (god != gods.end() && !god->second->name.empty()) {
            god_name = god->second->name;
        } else if (site.god_id.empty()) {
            god_name = "a nameless god";
        } else {
            // The reference's unbounded `.replace`: `god_` goes wherever it occurs and
            // every underscore becomes a space, not just a leading prefix.
            god_name = site.god_id;
            for (std::size_t at = god_name.find("god_"); at != std::string::npos;
                 at = god_name.find("god_", at)) {
                god_name.erase(at, 4);
            }
            for (char& c : god_name) {
                if (c == '_') c = ' ';
            }
        }
        const std::string name = "the " + std::string(site.kind == "cult" ? "cult" : "shrine") +
                                 " of " + god_name + " at " + hero_short_name(ruin.name);

        // A heresiarch converts the NEAREST LIVING CITY OF ANY PEOPLE -- a different
        // nearest from `home`, which is restricted to the ruin's own people.
        const HeroCity* target = role == "heresiarch" && !living.empty()
                                     ? nearest_city(ruin.direction, ruin.has_direction, living, ctx.radius)
                                     : nullptr;

        Hero p;
        p.uid = "hero-" + role + "-" + site.id;
        p.role = role;
        p.well = "shrines";
        // From the RUIN, not the home city: the keeper belongs to the people who died here.
        p.civilization_id = ruin.civilization_id;
        p.race_id = parent_race_of(ctx, ruin.parent_race_id, ruin.population_profile);
        p.born_age = std::max(1, ctx.final_age);
        p.status = "living";
        if (home != nullptr) {
            p.home = city_ref(*home);
            p.realm_uid = realm_uid_of(by_city, realms, home->uid);
        }
        p.site_name = name;
        p.god_id = site.god_id;
        p.presence = HeroPresence{"shrine", site.id, role == "heresiarch" ? "cult" : "altar"};
        HeroDeed deed;
        deed.event_kind = "shrine";
        deed.event_id = site.id;
        deed.age = std::max(1, ctx.final_age);
        deed.role = "keeper";
        deed.reason = site.kind;
        p.deeds.push_back(deed);
        p.claim = target != nullptr ? HeroClaim{"convert", target->uid, target->name}
                                    : HeroClaim{"hold", site.id, name};
        p.features = {"role:" + role, "shrine:" + std::string(role == "heresiarch" ? "cult" : "ruin")};
        if (site.born_under_surge) p.features.insert("school:dark");
        const std::string kind = role == "keeper" ? "keeper" : "cult";
        const double value = role == "keeper" ? w.keeper_base : w.cult_base;
        if (roll_once(p.uid, p.deeds, kind, chance_of(value), ctx.req->seed, rolls)) {
            people.push_back(std::move(p));
        }
    }
}

// ---------------------------------------------------------------------------
// Camps, relics and placement.
// ---------------------------------------------------------------------------

std::vector<HeroCamp> build_camps(Ctx& ctx, std::vector<HeroRoll>& rolls) {
    const HeroWellsPolicy& w = *ctx.wells;
    std::vector<HeroCamp> found;
    for (const HeroRuin& ruin : ctx.req->ruins) {
        HeroCamp camp;
        camp.uid = "camp-" + ruin.uid;
        camp.ruin_uid = ruin.uid;
        camp.name = "the camp in the ruins of " + hero_short_name(ruin.name);
        camp.node = ruin.node;
        camp.x = ruin.x;
        camp.z = ruin.z;
        camp.direction = ruin.direction;
        camp.civilization_id = ruin.civilization_id;
        camp.since_age = ruin.destroyed_age;
        // A camp is a PLACE, not a person, and the deed exists only so the ledger row can
        // carry the ruin's id and age. It is dropped before export.
        std::vector<HeroDeed> deeds(1);
        deeds[0].event_kind = "ruin";
        deeds[0].event_id = ruin.id;
        deeds[0].age = ruin.destroyed_age;
        const double bonus = ruin.city_class == "capital"  ? w.camp_per_city_class.capital
                             : ruin.city_class == "medium" ? w.camp_per_city_class.medium
                                                           : w.camp_per_city_class.small;
        if (roll_once(camp.uid, deeds, "camp", chance_of(w.camp_base + bonus), ctx.req->seed, rolls)) {
            found.push_back(std::move(camp));
        }
    }
    return found;
}

std::vector<HeroRelic> build_relics(Ctx& ctx) {
    std::vector<HeroRelic> out;
    for (const HeroRuin& ruin : ctx.req->ruins) {
        const std::string kind_key = cause_kind(ruin.cause, *ctx.words);
        std::string_view relic_kind = "banner";
        std::string_view name_template = "the banner of {name}";
        for (const HeroRelicRule& rule : ctx.words->relic_by_cause) {
            if (rule.cause_kind == kind_key) {
                relic_kind = rule.relic_kind;
                name_template = rule.name_template;
                break;
            }
        }
        if (ruin.city_class == "capital" && relic_kind == "banner") {
            relic_kind = "crown";
            name_template = "the crown of {name}";
        }
        const std::string school = relic_kind == "focus" ? ruin.new_node_school : std::string();
        std::string name(name_template);
        const std::string rendered_school = school.empty() ? std::string(ctx.words->default_school) : school;
        for (const auto& [token, value] :
             std::vector<std::pair<std::string, std::string>>{{"{name}", hero_short_name(ruin.name)},
                                                              {"{school}", rendered_school}}) {
            for (std::size_t at = name.find(token); at != std::string::npos; at = name.find(token, at)) {
                name.replace(at, token.size(), value);
                at += value.size();
            }
        }
        out.push_back({"relic-" + ruin.uid, std::string(relic_kind), name, ruin.id, ruin.uid, {}, school});
    }
    return out;
}

// Give the dispossessed somewhere to stand; a person with nowhere at all stays a legend.
// None of this is a script; it is where the person is standing when the story starts.
void place_people(Ctx& ctx, std::vector<Hero>& people, std::vector<HeroCamp>& camps) {
    std::map<std::string, HeroCamp*> by_ruin;
    for (HeroCamp& camp : camps) by_ruin[camp.ruin_uid] = &camp;
    const std::vector<const HeroCity*> living = all_cities(ctx);

    for (Hero& person : people) {
        if (ctx.final_age - person.born_age > 1 || person.home.has_value()) continue;
        HeroCamp* camp = nullptr;
        if (person.lost.has_value()) {
            auto it = by_ruin.find(person.lost->uid);
            if (it != by_ruin.end()) camp = it->second;
        }
        if (person.role == "pretender" && camp != nullptr && ctx.wells->heir_leads_own_camp &&
            person.status == "legend") {
            person.status = "living";
            person.presence = HeroPresence{"camp", camp->uid, "leader"};
            person.features.insert("role:camp_leader");
            person.features.insert("stake:none");
            camp->leader_uid = person.uid;
        } else if (person.dispossessed) {
            if (camp != nullptr) {
                Rng draw = stream(ctx.req->seed, "hero-place-" + person.uid);
                const auto options = hero_magister_at_camp();
                const std::string situation(options[static_cast<std::size_t>(draw.below(options.size()))]);
                person.status = "living";
                person.presence = HeroPresence{"camp", camp->uid, situation};
            } else {
                const HeroRuin* ruin = nullptr;
                auto it = ctx.ruin_by_uid.find(person.lost->uid);
                if (it != ctx.ruin_by_uid.end()) ruin = it->second;
                const HeroCity* city =
                    ruin == nullptr || living.empty()
                        ? nullptr
                        : nearest_city(ruin->direction, ruin->has_direction, living, ctx.radius);
                if (city == nullptr) {
                    person.status = "legend";
                    continue;
                }
                person.status = "living";
                person.home = city_ref(*city);
                person.presence = HeroPresence{"city", city->uid, "refugee"};
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Fame: deed magnitude times stake reach, from numbers the world already carries.
// ---------------------------------------------------------------------------

double person_magnitude(const Hero& person) {
    const std::string lost_class = person.lost.has_value() ? person.lost->city_class : "small";
    double total = 0.0;
    for (const HeroDeed& deed : person.deeds) {
        if (deed.event_kind == "ruin" && deed.role == "heir") {
            total += class_weight(lost_class);
        } else if (deed.event_kind == "ruin" &&
                   (deed.role == "actual" || deed.role == "keeper" || deed.role == "credited")) {
            total += (deed.role == "actual" ? 1.0 : 0.5) * class_weight(lost_class);
        } else if (deed.event_kind == "war" && deed.role == "credited") {
            total += war_weight(deed.kind) *
                     class_weight(deed.defeated_class.empty() ? "small" : deed.defeated_class);
        } else if (deed.event_kind == "war") {
            total += 0.5 * war_weight(deed.kind);
        } else if (deed.event_kind == "nest") {
            total += 0.6 * static_cast<double>(deed.has_tier ? deed.tier : 1);
        } else if (deed.event_kind == "founding") {
            total += 1.5;
        } else {
            bool known = false;
            const double weight = site_weight(deed.event_kind, known);
            if (known) total += weight;
        }
    }
    // ALWAYS the capital weight. The reference reads `person['home'].get('city_class',
    // 'capital')` and a home reference carries only {kind, uid, name}, so the default is
    // the only branch that can ever fire -- a sovereign of a medium seat would still
    // score 3. Ported as it stands; see the report.
    if (person.role == "sovereign") {
        total += class_weight("capital");
    } else if (person.role == "council") {
        total += hero_seat_weight(person.seat);
    }
    return total;
}

double person_reach(const Ctx& ctx, const Hero& person, const std::vector<HeroRealm>& realms,
                    const std::map<std::string, std::size_t>& realm_index) {
    const HeroFameTuning& fame = hero_fame_tuning();
    if (!person.home.has_value()) return fame.legend_reach;
    auto city = ctx.city_by_uid.find(person.home->uid);
    const std::string klass = city != ctx.city_by_uid.end() ? city->second->city_class : "small";
    std::size_t cities = 1;
    auto realm = realm_index.find(person.realm_uid);
    if (realm != realm_index.end()) cities = realms[realm->second].city_uids.size();
    return class_weight(klass) + 0.25 * static_cast<double>(cities);
}

std::string fame_tier(double value) {
    const HeroFameTuning& fame = hero_fame_tuning();
    if (value >= fame.legendary_floor) return "legendary";
    if (value >= fame.renowned_floor) return "renowned";
    return "notable";
}

// ---------------------------------------------------------------------------
// Names. A given name from the people's own language, an epithet from the deeds.
//
// The two share ONE stream, in that order, so the number of draws the heritage namer
// takes shifts the epithet. That coupling is the reference's and is kept deliberately:
// splitting the stream would be a different cast, not a tidier one.
//
// Epithets stay English on purpose. They are what the player reads about a hero, and
// `Torsunn the Unyielding` is the intended effect.
// ---------------------------------------------------------------------------

using Context = std::vector<std::pair<std::string, std::string>>;

std::string context_value(const Context& context, const std::string& key) {
    for (const auto& [k, v] : context) {
        if (k == key) return v;
    }
    return {};
}

// Substitute placeholders; a capitalised token asks for a capitalised value.
//
// `{school}` renders "the fire focus", `{School}` renders "the Fire Stone"; without the
// second form a school name filled mid-title reads as a typo. ONLY THE FIRST LETTER is
// raised, because a `capitalize` would lower the rest and turn a seat like `Court Mage`
// into `Court mage`.
std::string fill_epithet(std::string_view templ, const Context& context) {
    std::string out(templ);
    for (const auto& [key, value] : context) {
        for (const std::pair<std::string, std::string>& form :
             {std::pair<std::string, std::string>{"{" + key + "}", value},
              std::pair<std::string, std::string>{"{" + capitalise_first(key) + "}",
                                                  capitalise_first(value)}}) {
            for (std::size_t at = out.find(form.first); at != std::string::npos;
                 at = out.find(form.first, at)) {
                out.replace(at, form.first.size(), form.second);
                at += form.second.size();
            }
        }
    }
    return out;
}

// Context keys a template needs, CASE-FOLDED so `{School}` still finds `school`.
//
// Without the fold a capitalised token resolves to no value, the template is judged
// unusable and is quietly dropped from the draw -- the epithet would not render wrong, it
// would never be chosen.
std::vector<std::string> placeholders_of(std::string_view templ) {
    std::vector<std::string> keys;
    std::size_t at = templ.find('{');
    while (at != std::string_view::npos) {
        const std::size_t close = templ.find('}', at);
        if (close == std::string_view::npos) break;
        keys.push_back(lowercase(templ.substr(at + 1, close - at - 1)));
        at = templ.find('{', close);
    }
    return keys;
}

std::string draw_epithet(Rng& draw, const std::vector<std::string_view>& candidates,
                         const Context& context) {
    std::vector<std::string_view> usable;
    for (const std::string_view templ : candidates) {
        bool ok = true;
        for (const std::string& key : placeholders_of(templ)) {
            if (context_value(context, key).empty()) {
                ok = false;
                break;
            }
        }
        if (ok) usable.push_back(templ);
    }
    // When the filter empties the list the whole list comes back and `fill` substitutes
    // empty strings: a readable epithet is preferred to none.
    const std::vector<std::string_view>& pool = usable.empty() ? candidates : usable;
    return fill_epithet(pick(draw, pool), context);
}

std::pair<std::string, std::string> person_name(Ctx& ctx, const Hero& person) {
    Rng draw = stream(ctx.req->seed, "hero-name-" + person.uid);
    // A record with no civilization_id RAISES rather than falling back. The silent
    // fallback this replaces is the defect: it gave four peoples human names for as long
    // as they had existed, and a loud failure is the only way that does not happen again.
    if (person.civilization_id.empty()) {
        throw std::runtime_error("person " + person.uid + " has no civilization_id to name it from");
    }
    const heritage::Resolved& people =
        ctx.people_of(person.civilization_id, person.race_id.empty() ? std::string("human") : person.race_id);
    // The FILTERED path. `person_name_with_gloss` skips the forbidden-form redraw and is
    // recorded in heritage.hpp as shipping a spelling nobody meant to publish; this is one
    // of the two paths that puts a name in front of a player, so it takes the filter.
    const std::string name = heritage::person_name(people, heritage::lexicon(), draw);

    Context context;
    context.emplace_back("lost", person.lost.has_value() ? hero_short_name(person.lost->name) : "");
    context.emplace_back("home", person.home.has_value() ? hero_short_name(person.home->name) : "");
    context.emplace_back("victor", person.victor_name.empty() ? "" : hero_short_name(person.victor_name));
    context.emplace_back("defeated",
                         person.defeated.empty() ? "" : hero_short_name(person.defeated.front().name));
    context.emplace_back("school", person.school.empty() ? std::string(ctx.words->default_school)
                                                         : person.school);
    std::string site = person.site_name;
    const std::size_t the = site.find("the ");
    if (the != std::string::npos) site.erase(the, 4);  // the FIRST occurrence only, unlike short_name
    context.emplace_back("site", site);
    // Never empty: an unlisted seat -- including the absent seat of a non-council person --
    // falls back to a named title, so a `{seat}` template is always usable.
    context.emplace_back("seat", std::string(hero_word(ctx.words->seat_titles, person.seat,
                                                       ctx.words->seat_title_default)));

    const std::vector<std::string_view> candidates =
        hero_epithet_candidates(person.role, person.status == "legend");
    if (candidates.empty()) {
        throw std::runtime_error("no epithet list for role " + person.role);
    }
    return {name, draw_epithet(draw, candidates, context)};
}

// The city a dread is named for, without the " (Age N)" a refounded city carries: the
// epithet makes a possessive of it, and "Frosstrand (Age 1)'s Doom" is no name.
Context dread_context(const HeroDread& dread) {
    std::string city = dread.destroyed.empty() ? "" : hero_short_name(dread.destroyed.front().name);
    if (const std::size_t age = city.find(" (Age "); age != std::string::npos) city.erase(age);
    Context context;
    context.emplace_back("city", city);
    context.emplace_back("species", dread.species);
    return context;
}

std::string dread_name(Ctx& ctx, const HeroDread& dread) {
    Rng draw = stream(ctx.req->seed, "hero-name-" + dread.uid);
    const std::vector<std::string_view> candidates = hero_epithet_candidates("dread", false);
    // No usability filter on this path: the reference picks straight off the list.
    return fill_epithet(pick(draw, candidates), dread_context(dread));
}

// A dread's name is the handle a player hunts it by, so no two share one (2026-09-23). A
// dread whose drawn name another already holds takes the first epithet, in list order, that
// makes a free one; the epithets naming its first razed city always do, since every city is
// razed once.
void rename_dread_if_taken(HeroDread& dread, std::set<std::string>& taken) {
    if (taken.insert(dread.display_name).second) return;
    for (const std::string_view candidate : hero_epithet_candidates("dread", false)) {
        const std::string epithet = fill_epithet(candidate, dread_context(dread));
        const std::string display = dread.species + ", " + epithet;
        if (taken.insert(display).second) {
            dread.name = epithet;
            dread.display_name = display;
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Alignment derivation.
// ---------------------------------------------------------------------------

HeroAlignment derive_person_alignment(const Ctx& ctx, const Hero& person) {
    const HeroAxisPair bias = hero_civilization_bias(person.civilization_id);
    double law = bias.law, good = bias.good;
    double decided_law = 0.0, decided_good = 0.0;
    for (const HeroDeed& deed : person.deeds) {
        std::string key;
        if (deed.event_kind == "ruin" && deed.role == "heir") {
            key = person.lost.has_value() && person.lost->city_class == "capital" ? "ruin_heir_capital"
                                                                                 : "ruin_heir";
        } else if (deed.event_kind == "war") {
            // A war deed with no kind reads as CIVIL, not as unknown.
            key = "war_" + (deed.kind.empty() ? std::string("civil") : deed.kind) + "_" +
                  (deed.role == "credited" ? "victor" : "defeated");
        } else {
            continue;
        }
        const HeroAxisPair shift = hero_deed_shift(key);
        law += shift.law;
        good += shift.good;
        decided_law += shift.law;
        decided_good += shift.good;
    }
    Rng draw = stream(ctx.req->seed, "hero-alignment-" + person.uid);
    const double spread = hero_alignment_policy().jitter;
    // LAW FIRST, then good. Three different axis orders are observable in this package --
    // this one, `intensify`'s good-before-law, and the first/second face's law-before-good.
    law = bounded(clamp_axis(law + draw.uniform(-spread, spread)), decided_law);
    good = bounded(clamp_axis(good + draw.uniform(-spread, spread)), decided_good);
    return hero_alignment_record(law, good);
}

HeroAlignment derive_dread_alignment(const Ctx& ctx, const HeroDread& dread) {
    const HeroAxisPair base = hero_alignment_policy().dread_base;
    const HeroAxisPair family = hero_dread_family_shift(dread.family);
    Rng draw = stream(ctx.req->seed, "hero-alignment-" + dread.uid);
    const double spread = hero_alignment_policy().jitter;
    // No deed shifts and no sign locking: the family table is a monster-family colouring,
    // not a villainy scale, which is why a holy "Dread" lands Lawful Good.
    const double law = clamp_axis(base.law + family.law + draw.uniform(-spread, spread));
    const double good = clamp_axis(base.good + family.good + draw.uniform(-spread, spread));
    return hero_alignment_record(law, good);
}

HeroAlignment intensify(const HeroAlignment& alignment, HeroIntensity intensity, Rng& draw) {
    if (intensity == HeroIntensity::None) return alignment;
    // An axis that is EXACTLY 0.0 consumes a coin flip. GOOD IS SIGNED FIRST, so when both
    // axes are zero the draw order is good-then-law.
    auto sign = [&draw](double v) {
        if (v > 0.0) return 1.0;
        if (v < 0.0) return -1.0;
        return draw.random() < 0.5 ? 1.0 : -1.0;
    };
    const double good = sign(alignment.good);
    const double law = intensity == HeroIntensity::Corner ? sign(alignment.law) : alignment.law;
    HeroAlignment out = hero_alignment_record(law, good);
    out.has_drift = alignment.has_drift;
    out.drift = alignment.drift;
    return out;
}

// ---------------------------------------------------------------------------
// Faces: what the world sees first, and what is true.
// ---------------------------------------------------------------------------

std::vector<HeroFace> build_faces(Ctx& ctx, const HeroArchetypeCard& card, const Hero& person,
                                  const HeroAlignment& alignment) {
    HeroFace base;
    base.label = "public";
    base.name = person.name;
    base.apparent_role = person.role;
    base.alignment = alignment;
    base.claim = person.claim;
    base.persona = compose_persona(card, alignment);

    if (card.faces == HeroFaceSpec::Single) return {base};
    if (card.faces == HeroFaceSpec::PublicTrue) {
        Rng draw = stream(ctx.req->seed, "hero-face-" + person.uid);
        // The mask is always visibly good.
        const HeroAlignment shown =
            hero_alignment_record(alignment.law, std::max(0.5, std::abs(alignment.good)));
        const auto covers = hero_covers_for(person.role);
        const auto pool = covers.empty() ? hero_covers_for("default") : covers;
        const std::string cover(pool[static_cast<std::size_t>(draw.below(pool.size()))]);
        HeroFace shown_face;
        shown_face.label = "public";
        shown_face.name = person.name;
        shown_face.apparent_role = cover;
        shown_face.alignment = shown;
        shown_face.claim = {"protect", person.claim.target_uid, person.claim.target_name};
        shown_face.persona = compose_persona(card, shown);
        HeroFace truth = base;
        truth.label = "true";
        return {shown_face, truth};
    }
    if (card.faces == HeroFaceSpec::CalmUnleashed) {
        // No draw at all on this path.
        const HeroAlignment loosed = hero_alignment_record(-1.0, std::min(alignment.good, -0.5));
        HeroFace calm = base;
        calm.label = "calm";
        HeroFace unleashed = base;
        unleashed.label = "unleashed";
        unleashed.alignment = loosed;
        unleashed.persona = compose_persona(card, loosed);
        return {calm, unleashed};
    }
    Rng draw = stream(ctx.req->seed, "hero-face-" + person.uid);
    // LAW FIRST here, where derive_person also draws law first but `intensify` signs good
    // first. All three orders are observable; none may be tidied into agreement.
    const double law = -alignment.law + draw.uniform(-0.3, 0.3);
    const double good = -alignment.good + draw.uniform(-0.3, 0.3);
    const HeroAlignment other = hero_alignment_record(law, good);
    HeroFace first = base;
    first.label = "first";
    HeroFace second = base;
    second.label = "second";
    second.alignment = other;
    second.persona = compose_persona(card, other);
    return {first, second};
}

// ---------------------------------------------------------------------------
// The event log and the present situation. Derived from deeds, never invented, never
// planned: that is all an orchestrator gets, and what they do next is its call.
// ---------------------------------------------------------------------------

HeroLogEntry log_entry(const HeroDeed& deed, std::string text) {
    return {deed.age, deed.event_kind, deed.event_id, deed.role, std::move(text)};
}

std::vector<HeroLogEntry> person_log(const Ctx& ctx, const Hero& person) {
    std::vector<HeroLogEntry> entries;
    const std::string lost_name = person.lost.has_value() ? person.lost->name : std::string();
    const std::string home_name = person.home.has_value() ? person.home->name : std::string();
    std::string lost_cause;
    if (person.lost.has_value()) {
        auto it = ctx.ruin_by_uid.find(person.lost->uid);
        if (it != ctx.ruin_by_uid.end()) lost_cause = cause_phrase(it->second->cause, *ctx.words);
    }
    for (const HeroDeed& deed : person.deeds) {
        if (deed.event_kind == "hamlet") {
            entries.push_back(log_entry(
                deed, "Speaks for " + person.site_name + ", which feeds " + home_name +
                          (person.nest_name.empty()
                               ? ""
                               : "; the " + lowercase(person.nest_name) + " hunts its fields") +
                          "."));
        } else if (deed.event_kind == "fortress") {
            entries.push_back(log_entry(deed, "Commands " + person.site_name + ", on the road to " +
                                                  home_name + "."));
        } else if (deed.event_kind == "port") {
            entries.push_back(log_entry(deed, "Runs " + person.site_name + "; every hull bound for " +
                                                  home_name + " pays its due."));
        } else if (deed.event_kind == "shrine") {
            entries.push_back(log_entry(
                deed, std::string(person.role == "heresiarch" ? "Leads " : "Tends ") + person.site_name + "."));
        } else if (person.role == "pretender" && deed.event_kind == "ruin") {
            entries.push_back(log_entry(
                deed, lost_name + ", " +
                          std::string(hero_word(ctx.words->class_word, person.lost->city_class, "city")) +
                          " of the " + civilization_name(ctx, person.civilization_id) +
                          ", was destroyed by " + lost_cause + "; its heir survived."));
        } else if (person.role == "pretender" && deed.role == "victim") {
            entries.push_back(log_entry(deed, "Lost the " + deed.kind + " war that ended " + lost_name + "."));
        } else if (person.role == "pretender") {
            entries.push_back(log_entry(deed, lost_name + " won a " + deed.kind + " war."));
        } else if (person.role == "warlord") {
            entries.push_back(log_entry(deed, "Led " + person.victor_name + " to victory over " +
                                                  deed.defeated_name + " in a " + deed.kind + " war; " +
                                                  deed.defeated_name + " is a ruin."));
        } else if (person.role == "magister") {
            entries.push_back(log_entry(
                deed, lost_name + " destroyed itself in a magical experiment; the master of its "
                                  "college walked out of the ash, and a " +
                          person.school + " key point burns where the city stood."));
        } else if (person.role == "domain_holder") {
            entries.push_back(log_entry(deed, "Took up the " + person.school + " key point left where " +
                                                  lost_name + " fell to " + lost_cause + "."));
        } else if (person.role == "sovereign") {
            entries.push_back(log_entry(deed, home_name + (deed.role == "credited" ? " won a " : " lost a ") +
                                                  deed.kind + " war."));
        } else if (person.role == "champion" && deed.event_kind == "nest") {
            entries.push_back(log_entry(deed, "Holds the line for " + home_name + " against the " +
                                                  lowercase(person.nest_name) + "."));
        } else if (person.role == "champion") {
            entries.push_back(log_entry(deed, "Stood for " + lost_name + " against the " +
                                                  lowercase(person.nest_name) +
                                                  ", and the city fell anyway."));
        } else if (person.role == "prophet" || person.role == "heresiarch" || person.role == "exile" ||
                   person.role == "founder") {
            std::string reason = deed.reason;
            for (char& c : reason) {
                if (c == '_') c = ' ';
            }
            const bool names_city = person.role == "exile" || person.role == "founder";
            entries.push_back(log_entry(deed, "Led the " + reason + " that founded " +
                                                  (names_city ? person.claim.target_name : "a new city") + "."));
        }
    }
    std::sort(entries.begin(), entries.end(), [](const HeroLogEntry& a, const HeroLogEntry& b) {
        return a.age != b.age ? a.age < b.age : a.event_id < b.event_id;
    });
    return entries;
}

std::string person_situation(const Ctx& ctx, const Hero& person,
                             const std::map<std::string, const HeroCamp*>& camps) {
    const std::string site = person.presence.has_value() ? person.presence->site_kind : std::string();
    const std::string situation = person.presence.has_value() ? person.presence->situation : std::string();
    const std::string home_name = person.home.has_value() ? person.home->name : std::string();
    const std::string lost_name = person.lost.has_value() ? person.lost->name : std::string();
    auto camp_name = [&]() -> std::string {
        auto it = camps.find(person.presence->uid);
        return it == camps.end() ? std::string() : it->second->name;
    };

    if (person.role == "reeve") {
        // Reads the LIVE feature set, before it is emptied into `selectable`. A port that
        // emptied it first and dropped the fallback would give every reeve "the harvest is
        // in", silently, whatever the granary said.
        const bool starving = person.features.count("hamlet:starving") != 0;
        return "Reeve of " + person.site_name + "; " +
               (starving ? "the granary is short" : "the harvest is in") +
               (person.nest_name.empty() ? "." : ", and the " + lowercase(person.nest_name) + " is close.");
    }
    if (person.role == "castellan") return "Holds " + person.site_name + " for " + home_name + ".";
    if (person.role == "harbourmaster") return "Keeps the ledger of " + person.site_name + ".";
    if (person.role == "keeper") {
        return "Tends " + person.site_name +
               (person.home.has_value() ? " and shelters at " + home_name + "."
                                        : "; no city of the old people stands near.");
    }
    if (person.role == "heresiarch" && site == "shrine") {
        return "Preaches from " + person.site_name +
               (person.claim.verb == "convert" ? " toward " + person.claim.target_name + "." : ".");
    }
    if (person.role == "pretender") {
        if (site == "camp") return "Leads " + camp_name() + "; still claims " + lost_name + ".";
        if (person.home.has_value()) return "Sheltered at " + home_name + "; still claims " + lost_name + ".";
        return "No city of the " + civilization_name(ctx, person.civilization_id) +
               " survives to shelter the heir; the claim on " + lost_name + " passed into legend.";
    }
    if (person.role == "warlord") {
        if (person.victor_fell) {
            return person.victor_name + " itself fell in a later age; the victor is remembered, not seated.";
        }
        return "Holds " + person.victor_name + ".";
    }
    if (person.role == "magister") {
        if (site == "camp") {
            return situation == "captive" ? "Held captive at " + camp_name() + "."
                                          : "Hiding at " + camp_name() + ", unrecognised.";
        }
        if (site == "city") {
            return "A refugee at " + home_name + "; the college is ash, the " + person.school +
                   " key point is not.";
        }
        if (site == "college") return "Heads the college near " + hero_short_name(home_name) + ".";
        return "Gone; the last master of " + lost_name + " is a name in the ash.";
    }
    if (person.role == "domain_holder") {
        if (site == "ruin") {
            return "Wards the " + person.school + " key point in the ruins of " +
                   hero_short_name(lost_name) + ".";
        }
        return "The " + person.school + " key point at " + hero_short_name(lost_name) + " has no keeper now.";
    }
    if (person.role == "sovereign") {
        return "Sits the throne of " + person.claim.target_name + " at " + home_name + ".";
    }
    if (person.role == "council") {
        std::string seat = person.seat.empty() ? "council" : person.seat;
        for (char& c : seat) {
            if (c == '_') c = ' ';
        }
        return "Holds the " + seat + " seat at " + home_name + ".";
    }
    if (person.role == "champion") {
        if (site == "city") {
            return "Champion of the Order at " + home_name + "; the " + lowercase(person.nest_name) +
                   " is still out there.";
        }
        if (site == "camp") {
            return std::string(situation == "captive" ? "Held captive at " : "Hiding at ") + camp_name() +
                   "; the Order is a remnant.";
        }
        if (person.home.has_value()) {
            return "A refugee at " + home_name + "; the Order of " + hero_short_name(lost_name) +
                   " is a remnant.";
        }
        return "The Order of " + hero_short_name(lost_name) + " is a remnant; its champion is a name.";
    }
    if (person.role == "prophet" || person.role == "heresiarch") {
        return person.home.has_value() ? "Preaches at " + home_name + "."
                                       : "The schism outlived its prophet.";
    }
    if (person.role == "exile" || person.role == "founder") {
        return person.home.has_value() ? "Leads " + home_name + "." : "The founding outlived its leader.";
    }
    return {};
}

std::vector<HeroLogEntry> dread_log(const HeroDread& dread) {
    std::vector<HeroDeed> deeds = dread.deeds;
    std::sort(deeds.begin(), deeds.end(), [](const HeroDeed& a, const HeroDeed& b) {
        return a.age != b.age ? a.age < b.age : a.event_id < b.event_id;
    });
    std::vector<HeroLogEntry> entries;
    for (const HeroDeed& deed : deeds) entries.push_back(log_entry(deed, "Destroyed " + deed.city_name + "."));
    return entries;
}

std::string dread_situation(const HeroDread& dread) {
    return dread.status == "living"
               ? "The lair still stands; the " + dread.species + " has not been driven out."
               : "The lair is gone, but the name of the " + dread.species + " is not.";
}

// ---------------------------------------------------------------------------
// Cross-cutting features: facts about a person that only exist once the whole cast is
// known. Wells see one record at a time.
// ---------------------------------------------------------------------------

void apply_cross_cutting(Ctx& ctx, std::vector<Hero>& people) {
    const double threshold = ctx.wells->tainted_threshold;
    std::vector<Hero*> living;
    for (Hero& person : people) {
        if (person.status == "living") living.push_back(&person);
    }
    // Grouped in CAST ORDER, and the first sovereign in each group is the one compared
    // against: with two sovereigns in one realm the earlier in cast order wins.
    std::vector<std::pair<std::string, std::vector<Hero*>>> by_realm;
    for (Hero* person : living) {
        auto it = std::find_if(by_realm.begin(), by_realm.end(),
                               [&](const auto& row) { return row.first == person->realm_uid; });
        if (it == by_realm.end()) {
            by_realm.emplace_back(person->realm_uid, std::vector<Hero*>{person});
        } else {
            it->second.push_back(person);
        }
    }
    std::map<std::string, int> latest_founding;
    for (const HeroCity& city : ctx.req->cities) {
        int& held = latest_founding[city.civilization_id];
        held = std::max(held, city.founded_age);
    }
    std::set<std::string> camp_leader_civs;
    for (const Hero* person : living) {
        if (person->presence.has_value() && person->presence->situation == "leader") {
            camp_leader_civs.insert(person->civilization_id);
        }
    }
    // The warlord's OWN SEAT -- the city that won -- not the city it took. The port
    // specification states this backwards in one section and correctly in another.
    std::set<std::string> warlord_seats;
    for (const Hero& person : people) {
        if (person.role == "warlord") warlord_seats.insert(person.victor_uid);
    }

    for (Hero& person : people) {
        std::set<std::string>& f = person.features;
        const HeroCity* home = nullptr;
        if (person.home.has_value()) {
            auto it = ctx.city_by_uid.find(person.home->uid);
            if (it != ctx.city_by_uid.end()) home = it->second;
        }
        if (home != nullptr && ctx.req->layers != nullptr) {
            for (const std::string_view layer : ctx.words->tainting_layers) {
                auto it = ctx.req->layers->find(std::string(layer));
                if (it == ctx.req->layers->end() || it->second.empty()) continue;
                const Grid& grid = it->second;
                if (home->x < 0 || home->z < 0 || home->x >= grid.size() || home->z >= grid.size()) continue;
                if (grid.at(home->x, home->z) > threshold) f.insert("ley:tainted_home");
            }
        }
        std::set<std::string> opponents;
        for (const HeroDeed& deed : person.deeds) {
            if (deed.opponent_uid.empty()) continue;
            auto city = ctx.city_by_uid.find(deed.opponent_uid);
            if (city != ctx.city_by_uid.end()) {
                opponents.insert(city->second->civilization_id);
                continue;
            }
            auto ruin = ctx.ruin_by_uid.find(deed.opponent_uid);
            if (ruin != ctx.ruin_by_uid.end()) opponents.insert(ruin->second->civilization_id);
        }
        if (opponents.size() >= 2) f.insert("deeds:two_civilizations");
        // `villain:prior_age` is NOT a reference to a super villain. The world model owns
        // that word in its own `villains` block -- a continuous tier, a reach in metres, a
        // seat and held ley nodes, at most one per cultural region. This is a feature token
        // on an ordinary person and it means only what the line below tests: a pretender
        // whose people founded a city again after they were born, so their claim was
        // overtaken while they lived. It carries no tier, no reach, no region and no seat;
        // a world can be full of these with an empty villains block; and filtering for it
        // to find "people connected to a super villain" returns dispossessed claimants and
        // nothing about super villains at all, with no error.
        if (person.role == "pretender" && person.born_age < ctx.final_age) {
            auto founded = latest_founding.find(person.civilization_id);
            if (founded != latest_founding.end() && founded->second > person.born_age) {
                f.insert("villain:prior_age");
                f.insert("stake:regained");
            }
        }
        if (contains(ctx.words->seated_roles, person.role) &&
            camp_leader_civs.count(person.civilization_id)) {
            f.insert("rival:spared");
        }
        if (person.role == "champion" && person.home.has_value() &&
            warlord_seats.count(person.home->uid)) {
            f.insert("deed:credited_not_actual");
        }
        if (!contains(ctx.words->seat_exempt_roles, person.role) && person.status == "living") {
            f.insert("seat:none");
        }
        if (f.count("legend:predecessor") && person.role == "pretender") f.insert("heir:unclaimed");
    }

    for (auto& [realm_uid, members] : by_realm) {
        Hero* sovereign = nullptr;
        for (Hero* person : members) {
            if (person->role == "sovereign") {
                sovereign = person;
                break;
            }
        }
        if (sovereign == nullptr) continue;
        for (Hero* person : members) {
            if (person->role == "council" && person->fame > sovereign->fame) {
                person->features.insert("council:outranks_sovereign");
                sovereign->features.insert("sovereign:advised");
            }
        }
    }
    for (Hero* person : living) {
        if (person->tier != "renowned" && person->tier != "legendary") continue;
        for (const Hero& other : people) {
            if (&other != person && other.status == "living" &&
                other.civilization_id == person->civilization_id && other.born_age > person->born_age) {
                person->features.insert("mentor:successor");
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Bonds and mantles.
// ---------------------------------------------------------------------------

std::vector<HeroBond> build_bonds(const std::vector<Hero>& people, const std::vector<HeroDread>& dreads) {
    std::vector<HeroBond> edges;
    std::map<std::string, const Hero*> pretenders;  // LAST pretender per ruin wins
    for (const Hero& person : people) {
        if (person.role == "pretender" && person.lost.has_value()) pretenders[person.lost->uid] = &person;
    }
    for (const Hero& person : people) {
        if (person.role == "warlord") {
            for (const HeroDefeatedCity& taken : person.defeated) {
                auto heir = pretenders.find(taken.uid);
                if (heir != pretenders.end()) {
                    edges.push_back({heir->second->uid, person.uid, "rival", taken.war_id});
                }
            }
        } else if ((person.role == "domain_holder" || person.role == "magister") && person.lost.has_value()) {
            auto heir = pretenders.find(person.lost->uid);
            if (heir != pretenders.end()) {
                edges.push_back({heir->second->uid, person.uid,
                                 person.role == "domain_holder" ? "rival" : "kin", person.lost->ruin_id});
            }
        }
    }
    for (const HeroDread& dread : dreads) {
        for (const HeroDreadKill& kill : dread.destroyed) {
            auto heir = pretenders.find(kill.uid);
            if (heir != pretenders.end()) {
                edges.push_back({heir->second->uid, dread.uid, "rival", "ruin-" + kill.uid});
            }
        }
    }
    std::map<std::string, const Hero*> warlords_by_city, sovereigns_by_realm;
    for (const Hero& person : people) {
        if (person.role == "warlord") warlords_by_city[person.victor_uid] = &person;
        // A sovereign with an empty realm_uid puts an empty key here, and every council
        // member with no realm then bonds to it. In practice a sovereign always carries
        // its realm's uid; the edge case is inherited rather than designed.
        if (person.role == "sovereign") sovereigns_by_realm[person.realm_uid] = &person;
    }
    for (const Hero& person : people) {
        const std::string home_uid = person.home.has_value() ? person.home->uid : std::string();
        if (person.role == "champion" && warlords_by_city.count(home_uid)) {
            const Hero* warlord = warlords_by_city[home_uid];
            edges.push_back({person.uid, warlord->uid, "betrayed",
                             warlord->deeds.empty() ? std::string() : warlord->deeds.front().event_id});
        }
        if (person.role == "council" && sovereigns_by_realm.count(person.realm_uid)) {
            // `from_event` is THE HOME CITY UID here, where every other edge carries a war
            // id, a ruin id or a deed's event id. Carried as-is; see the report.
            edges.push_back({person.uid, sovereigns_by_realm[person.realm_uid]->uid, "ally", home_uid});
        }
        if (std::find(person.selectable.begin(), person.selectable.end(), "mentor:successor") !=
            person.selectable.end()) {
            const Hero* pupil = nullptr;
            for (const Hero& other : people) {
                if (&other == &person || other.status != "living" ||
                    other.civilization_id != person.civilization_id || other.born_age <= person.born_age) {
                    continue;
                }
                if (pupil == nullptr || other.born_age < pupil->born_age ||
                    (other.born_age == pupil->born_age && other.uid < pupil->uid)) {
                    pupil = &other;
                }
            }
            if (pupil != nullptr) {
                edges.push_back({person.uid, pupil->uid, "mentor",
                                 person.deeds.empty() ? person.uid : person.deeds.front().event_id});
            }
        }
    }
    return edges;
}

std::vector<HeroMantle> build_mantles(const std::vector<Hero>& people) {
    std::vector<HeroMantle> out;
    for (const Hero& person : people) {
        if (person.status != "legend") continue;
        if (person.claim.verb != "retake" && person.claim.verb != "hold") continue;
        // `predecessor_uid` here is THE LEGEND'S OWN uid. On a person record the same field
        // name means the legend they inherit FROM.
        out.push_back({person.claim, person.uid, person.civilization_id, person.born_age});
    }
    return out;
}

// ---------------------------------------------------------------------------
// Quest hooks: a living person's claim, compiled into typed effects on things the world
// has. A hook names an anchor, says what the player does to it, and says how hard it is.
// It never names a reward.
// ---------------------------------------------------------------------------

struct HookIndex {
    std::map<std::string, const HeroRuin*> ruins_by_uid, ruins_by_id;
    std::map<std::string, const HeroCity*> city;
    std::map<std::string, const HeroHamlet*> hamlet;
    std::map<std::string, const HeroFortress*> fortress;
    std::map<std::string, const HeroPort*> port;
    std::map<std::string, const HeroReligionSite*> shrine;
    std::map<std::string, const HeroCollege*> college;
    std::map<std::string, const HeroNest*> nest;
    std::map<std::string, std::string> ruin_of_relic;
    std::map<std::string, double> intensity;
};

struct HookTarget {
    bool has_grid = false;
    int x = 0, z = 0;
    int node = -1;
    bool has_node = false;
    double opposition = 0.0;  // a nest's tier or a ley node's intensity
    std::string unsited;
};

HookIndex build_hook_index(const Ctx& ctx, const std::vector<HeroRelic>& relics) {
    HookIndex index;
    index.ruins_by_uid = ctx.ruin_by_uid;
    index.ruins_by_id = ctx.ruin_by_id;
    index.city = ctx.city_by_uid;
    index.port = ctx.port_by_id;
    index.shrine = ctx.shrine_by_id;
    index.college = ctx.college_by_id;
    index.nest = ctx.nest_by_id;
    index.intensity = ctx.intensity_by_node;
    // The ORDINAL id is written first and the node anchor second, so a collision between
    // the two spellings resolves to the anchor -- the one that survives an age. Reversing
    // these two loops silently breaks every countryside hook.
    for (const HeroHamlet& h : ctx.req->hamlets) {
        if (!h.id.empty()) index.hamlet[h.id] = &h;
    }
    for (const HeroHamlet& h : ctx.req->hamlets) {
        index.hamlet[hero_small_site_anchor("hamlet", h.node, h.id)] = &h;
    }
    for (const HeroFortress& f : ctx.req->fortresses) {
        if (!f.id.empty()) index.fortress[f.id] = &f;
    }
    for (const HeroFortress& f : ctx.req->fortresses) {
        index.fortress[hero_small_site_anchor("fortress", f.node, f.id)] = &f;
    }
    for (const HeroRelic& relic : relics) index.ruin_of_relic[relic.uid] = relic.resting_at;
    return index;
}

HookTarget resolve_target(const HookIndex& index, const HeroEffect& effect) {
    HookTarget out;
    auto from_ruin = [&out](const HeroRuin* ruin) {
        out.has_grid = true;
        out.x = ruin->x;
        out.z = ruin->z;
        out.node = ruin->node;
        out.has_node = ruin->node >= 0;
    };
    if (effect.kind == "ley_node") {
        const std::string& id = effect.node_id;
        const HeroRuin* ruin = nullptr;
        if (id.size() > 4 && id.compare(id.size() - 4, 4, "-key") == 0) {
            auto it = index.ruins_by_id.find(id.substr(0, id.size() - 4));
            if (it != index.ruins_by_id.end()) ruin = it->second;
        }
        if (ruin == nullptr) {
            // The honest one: a bare ley node carries a direction and an intensity and
            // nothing a player can stand in front of. It is reported rather than given an
            // invented position.
            out.unsited = "ley_node_has_no_site";
            return out;
        }
        from_ruin(ruin);
        auto intensity = index.intensity.find(id);
        out.opposition = intensity == index.intensity.end() ? 0.0 : intensity->second;
        return out;
    }
    if (effect.kind == "relic" || effect.kind == "ruin") {
        std::string uid = effect.uid;
        if (effect.kind == "relic") {
            auto it = index.ruin_of_relic.find(effect.uid);
            uid = it == index.ruin_of_relic.end() ? std::string() : it->second;
        }
        auto it = index.ruins_by_uid.find(uid);
        if (it == index.ruins_by_uid.end()) {
            out.unsited = "target_not_found";
            return out;
        }
        from_ruin(it->second);
        if (!out.has_node) out.unsited = "target_has_no_node";
        return out;
    }
    if (effect.kind == "nest") {
        auto it = index.nest.find(effect.nest_id.empty() ? effect.uid : effect.nest_id);
        if (it == index.nest.end()) {
            out.unsited = "target_not_found";
            return out;
        }
        const HeroNest& nest = *it->second;
        out.opposition = nest.has_tier ? static_cast<double>(nest.tier) : 0.0;
        out.has_grid = nest.has_grid;
        out.x = nest.x;
        out.z = nest.z;
        out.node = nest.node;
        out.has_node = nest.node >= 0;
        // A reference `beast_nests.sites[]` row carries no node, so this fires for every
        // slay hook ever generated: the lair is somewhere on the grid but nothing in the
        // record says which node, and the hook says so rather than inventing one.
        if (!out.has_node) out.unsited = "target_has_no_node";
        return out;
    }
    if (effect.kind == "city") {
        auto it = index.city.find(effect.uid);
        if (it == index.city.end()) {
            out.unsited = "target_not_found";
            return out;
        }
        out.has_grid = true;
        out.x = it->second->x;
        out.z = it->second->z;
        out.node = it->second->node;
        out.has_node = out.node >= 0;
        if (!out.has_node) out.unsited = "target_has_no_node";
        return out;
    }
    if (effect.kind == "hamlet" || effect.kind == "fortress") {
        int node = -1, x = 0, z = 0;
        bool found = false;
        if (effect.kind == "hamlet") {
            auto it = index.hamlet.find(effect.uid);
            if (it != index.hamlet.end()) {
                found = true;
                node = it->second->node;
                x = it->second->x;
                z = it->second->z;
            }
        } else {
            auto it = index.fortress.find(effect.uid);
            if (it != index.fortress.end()) {
                found = true;
                node = it->second->node;
                x = it->second->x;
                z = it->second->z;
            }
        }
        if (!found) {
            out.unsited = "target_not_found";
            return out;
        }
        out.has_grid = true;
        out.x = x;
        out.z = z;
        out.node = node;
        out.has_node = node >= 0;
        if (!out.has_node) out.unsited = "target_has_no_node";
        return out;
    }
    if (effect.kind == "port") {
        auto it = index.port.find(effect.uid);
        if (it == index.port.end()) {
            out.unsited = "target_not_found";
            return out;
        }
        // A port carries a node and no grid position, and nothing substitutes for it, so
        // its hook is sited but its pressure reads 0.0.
        out.node = it->second->node;
        out.has_node = out.node >= 0;
        if (!out.has_node) out.unsited = "target_has_no_node";
        return out;
    }
    if (effect.kind == "shrine") {
        auto it = index.shrine.find(effect.uid);
        if (it == index.shrine.end()) {
            out.unsited = "target_not_found";
            return out;
        }
        out.node = it->second->node;
        out.has_node = out.node >= 0;
        if (!out.has_node) {
            // The node check runs BEFORE the substitution, so a shrine with no node
            // reports against the shrine itself and never reaches its ruin.
            out.unsited = "target_has_no_node";
            return out;
        }
        // `religion.sites` carries a node and no grid position; the ruin it was born of
        // carries both, and is the same ground. A shrine whose ruin_id names nothing gives
        // a sited hook with no measuring record, which reads as 0.0 pressure.
        auto ruin = index.ruins_by_id.find(it->second->ruin_id);
        if (ruin != index.ruins_by_id.end()) {
            out.has_grid = true;
            out.x = ruin->second->x;
            out.z = ruin->second->z;
        }
        return out;
    }
    if (effect.kind == "college") {
        auto it = index.college.find(effect.uid);
        if (it == index.college.end()) {
            out.unsited = "target_not_found";
            return out;
        }
        out.has_grid = true;
        out.x = it->second->x;
        out.z = it->second->z;
        out.node = it->second->node;
        out.has_node = out.node >= 0;
        if (!out.has_node) out.unsited = "target_has_no_node";
        return out;
    }
    out.unsited = "target_not_found";
    return out;
}

// A ley node as a person names it, never by its id: a stated purpose is what a hero asks
// for, in their own words, and until 2026-09-25 a bare node's read "the water node
// water-node-4". A key point stands at its ruin: "the radiant key point at Korpat". Any other
// node, a school's own or a villain's well, is named by a place (`hero_ley_node_name`).
std::string node_name(const Ctx& ctx, const HookIndex& index, const std::string& node_id, const std::string& school) {
    if (node_id.size() > 4 && node_id.compare(node_id.size() - 4, 4, "-key") == 0) {
        auto ruin = index.ruins_by_id.find(node_id.substr(0, node_id.size() - 4));
        if (ruin != index.ruins_by_id.end()) {
            return "the " + school + " key point at " + hero_short_name(ruin->second->name);
        }
    }
    const std::string named = hero_ley_node_name(*ctx.req, node_id, school);
    return named.empty() ? "the " + school + " node" : named;
}

// The node a ley hook proposes to weaken: the nearest node of the opposing school to the
// ground the person stands on, ties broken by node id.
std::pair<std::string, const HeroLeyNodeRef*> nearest_opposing(const Ctx& ctx, const Hero& person,
                                                               const std::string& school) {
    const std::string_view opposite = hero_opposite_school(school);
    if (opposite.empty() || !person.anchor_direction.has_value()) return {{}, nullptr};
    const HeroLeyNetwork* network = nullptr;
    for (const HeroLeyNetwork& net : ctx.req->networks) {
        if (net.school == opposite) network = &net;
    }
    if (network == nullptr) return {{}, nullptr};
    const HeroLeyNodeRef* best = nullptr;
    double best_key = 0.0;
    for (const HeroLeyNodeRef& node : network->nodes) {
        const double angle = hero_separation_m(*person.anchor_direction, node.direction, 1.0);
        if (best == nullptr || angle < best_key || (angle == best_key && node.id < best->id)) {
            best = &node;
            best_key = angle;
        }
    }
    if (best == nullptr) return {{}, nullptr};
    return {std::string(opposite), best};
}

struct HookDraft {
    std::string stated;
    HeroEffect effect;
    bool unwitting = false;
};

std::vector<HeroQuestHook> build_quest_hooks(const Ctx& ctx, const std::vector<Hero>& people,
                                             const std::vector<HeroRelic>& relics,
                                             const std::vector<HeroDread>& dreads) {
    const HeroWellsPolicy& w = *ctx.wells;
    const HookIndex index = build_hook_index(ctx, relics);
    std::map<std::string, const HeroRelic*> relic_by_ruin;
    for (const HeroRelic& relic : relics) relic_by_ruin[relic.resting_at] = &relic;
    // The hunt (2026-09-23): a living dread is named in every slay hook against its lair, and
    // the heir of each city it razed asks for it by name.
    std::map<std::string, const HeroDread*> dread_at_lair;
    std::map<std::string, const HeroDread*> dread_of_ruin;
    for (const HeroDread& dread : dreads) {
        if (dread.status != "living") continue;
        dread_at_lair.emplace(dread.nest_id, &dread);
        for (const HeroDreadKill& kill : dread.destroyed) dread_of_ruin.emplace(kill.uid, &dread);
    }
    const auto named = [&](const std::string& nest_id) -> const HeroDread* {
        const auto found = dread_at_lair.find(nest_id);
        return found != dread_at_lair.end() ? found->second : nullptr;
    };

    std::vector<HeroQuestHook> hooks;
    for (const Hero& person : people) {
        if (person.status != "living") continue;
        const bool covert = person.faces.size() > 1 && person.faces.front().label == "public";
        const std::string face = covert ? "public" : person.faces.front().label;
        const HeroClaim& claim = person.claim;
        std::vector<HookDraft> drafts;

        // A FIRST-MATCH CHAIN, and the order is load-bearing.
        if (claim.verb == "retake") {
            HeroEffect e;
            e.kind = "ruin";
            e.uid = claim.target_uid;
            e.action = "reclaim";
            drafts.push_back({"Help me take back " + claim.target_name + ".", e, false});
            auto relic = relic_by_ruin.find(claim.target_uid);
            if (relic != relic_by_ruin.end()) {
                HeroEffect r;
                r.kind = "relic";
                r.uid = relic->second->uid;
                r.action = "recover";
                drafts.push_back({"Recover " + relic->second->name + " from the ruins.", r, false});
            }
        } else if (claim.verb == "hold" && person.role == "warlord") {
            HeroEffect e;
            e.kind = "city";
            e.uid = claim.target_uid;
            e.action = "defend";
            drafts.push_back({"Stand with " + claim.target_name + " when they come for it.", e, false});
        } else if (!person.node_id.empty() && (claim.verb == "hold" || claim.verb == "exploit")) {
            const std::string& school = person.school;
            HeroEffect own;
            own.kind = "ley_node";
            own.node_id = person.node_id;
            own.school = school;
            own.intensity_delta = w.hook_intensity_delta;
            own.has_delta = true;
            const std::string honest = "Strengthen " + node_name(ctx, index, person.node_id, school) + ".";
            const std::string cover =
                "Restore the old shrine at " +
                (person.lost.has_value() ? hero_short_name(person.lost->name) : claim.target_name) +
                "; the land around it is sick.";
            drafts.push_back({covert ? cover : honest, own, covert});
            const auto [opposite, node] = nearest_opposing(ctx, person, school);
            if (node != nullptr) {
                HeroEffect weaken;
                weaken.kind = "ley_node";
                weaken.node_id = node->id;
                weaken.school = opposite;
                weaken.intensity_delta = -w.hook_intensity_delta;
                weaken.has_delta = true;
                const std::string weaken_honest =
                    "Weaken " + node_name(ctx, index, node->id, opposite) + "; it chokes the " + school + ".";
                const std::string weaken_cover = "Cleanse " + node_name(ctx, index, node->id, opposite) +
                                                 "; something there is poisoning the valley.";
                drafts.push_back({covert ? weaken_cover : weaken_honest, weaken, covert});
            }
        } else if (person.role == "reeve") {
            HeroEffect e;
            e.kind = "hamlet";
            e.uid = claim.target_uid;
            e.action = "defend";
            drafts.push_back({"See " + claim.target_name + " through the season.", e, false});
            if (!person.nest_id.empty()) {
                HeroEffect slay;
                slay.kind = "nest";
                slay.nest_id = person.nest_id;
                slay.action = "slay";
                const HeroDread* dread = named(person.nest_id);
                // The name comes last: an epithet like "Who Ate Nerdun" is an appositive, and
                // a sentence that carries on after it needs commas nobody reads well.
                drafts.push_back({dread != nullptr
                                      ? "Our fields lie in its reach: drive off " + dread->display_name + "."
                                      : "Drive the " + lowercase(person.nest_name) + " from our fields.",
                                  slay, false});
            }
        } else if (person.role == "castellan") {
            HeroEffect e;
            e.kind = "fortress";
            e.uid = claim.target_uid;
            e.action = "defend";
            drafts.push_back({"Hold " + claim.target_name + " with me.", e, false});
        } else if (person.role == "harbourmaster") {
            HeroEffect e;
            e.kind = "port";
            e.uid = claim.target_uid;
            e.action = "trade";
            drafts.push_back({"Keep the sea lanes to " + claim.target_name + " open.", e, false});
        } else if (person.role == "keeper") {
            HeroEffect e;
            e.kind = "shrine";
            e.uid = claim.target_uid;
            e.action = "tend";
            drafts.push_back({"Bring an offering to " + claim.target_name + ".", e, false});
        } else if (person.role == "sovereign") {
            HeroEffect e;
            e.kind = "city";
            e.uid = person.home->uid;
            e.action = "defend";
            drafts.push_back({"Stand with " + person.home->name + " when they come for it.", e, false});
        } else if (person.role == "council" && person.seat == "market_steward") {
            HeroEffect e;
            e.kind = "city";
            e.uid = person.home->uid;
            e.action = "trade";
            drafts.push_back({"Keep the roads to " + person.home->name +
                                  " open; the granary is my ledger.",
                              e, false});
        } else if (person.role == "council" && person.seat == "commander") {
            HeroEffect e;
            e.kind = "city";
            e.uid = person.home->uid;
            e.action = "defend";
            drafts.push_back({"Walk the walls of " + person.home->name + " with me.", e, false});
        } else if (person.role == "champion" && !person.nest_id.empty() && index.nest.count(person.nest_id) != 0) {
            // A lair that no longer stands cannot be hunted: the champion of a city whose
            // beast is gone -- a legend now -- offers nothing to slay.
            HeroEffect e;
            e.kind = "nest";
            e.nest_id = person.nest_id;
            e.action = "slay";
            const HeroDread* dread = named(person.nest_id);
            drafts.push_back({"Help me end " + (dread != nullptr ? dread->display_name : claim.target_name) + ".", e,
                              false});
        } else if ((person.role == "heresiarch" || person.role == "prophet") && claim.verb == "convert") {
            HeroEffect e;
            e.kind = "city";
            e.uid = claim.target_uid;
            e.action = "convert";
            drafts.push_back({"Carry the word to " + claim.target_name + ".", e, false});
        } else if (person.role == "exile" || person.role == "founder" || person.role == "prophet") {
            HeroEffect e;
            e.kind = "city";
            e.uid = claim.target_uid;
            e.action = "defend";
            drafts.push_back({"Keep " + claim.target_name + " standing.", e, false});
        } else if (person.role == "magister" && person.college.has_value()) {
            HeroEffect e;
            e.kind = "college";
            e.uid = person.college->id;
            e.action = "supply";
            drafts.push_back({"Bring the college near " + hero_short_name(person.home->name) +
                                  " what it asks for.",
                              e, false});
        }

        // Outside the chain, after whatever it drafted: the heir of a city a living dread
        // razed wants the city back AND the beast dead, and asks for the beast by name.
        if (person.role == "pretender" && person.lost.has_value()) {
            if (const auto found = dread_of_ruin.find(person.lost->uid); found != dread_of_ruin.end()) {
                const HeroDread& dread = *found->second;
                HeroEffect slay;
                slay.kind = "nest";
                slay.nest_id = dread.nest_id;
                slay.action = "slay";
                drafts.push_back({"Avenge " + hero_short_name(person.lost->name) + ": slay " + dread.display_name + ".",
                                  slay, false});
            }
        }

        for (std::size_t i = 0; i < drafts.size(); ++i) {
            const HookDraft& draft = drafts[i];
            const HookTarget target = resolve_target(index, draft.effect);
            std::string verb = draft.effect.action;
            if (verb.empty()) {
                // A ley effect carries no action; its verb is the SIGN of the delta.
                verb = draft.effect.intensity_delta >= 0.0 ? std::string(w.ley_verb_strengthen)
                                                           : std::string(w.ley_verb_weaken);
            }
            if (!contains(hero_quest_verbs(), verb)) {
                throw std::runtime_error("quest hook verb '" + verb + "' is outside the vocabulary");
            }
            HeroQuestHook hook;
            hook.hook_id = "hook-" + person.uid + "-" + std::to_string(i);
            hook.giver_uid = person.uid;
            hook.offered_from_face = face;
            hook.stated_purpose = draft.stated;
            hook.actual_effect = draft.effect;
            hook.unwitting = draft.unwitting;
            // uid, then node_id, then nest_id, IN THAT ORDER: reading uid or node_id alone
            // left every slay hook pointing at nothing.
            hook.target = !draft.effect.uid.empty()      ? draft.effect.uid
                          : !draft.effect.node_id.empty() ? draft.effect.node_id
                                                          : draft.effect.nest_id;
            hook.verb = verb;
            hook.difficulty = hero_hook_difficulty(draft.effect, target.opposition, target.has_grid,
                                                   target.x, target.z, ctx.req->nests, ctx.spacing);
            hook.target_node = target.node;
            hook.has_target_node = target.has_node;
            hook.unsited_reason = target.unsited;
            hooks.push_back(std::move(hook));
        }
    }
    return hooks;
}

// ---------------------------------------------------------------------------
// Diagnostics. The half `rolls` cannot answer: a role with no row in the ledger is
// indistinguishable from a role that does not exist.
// ---------------------------------------------------------------------------

struct SourceRow {
    std::string_view role;
    // The published phrase the reason sentence is built from. It names WHAT WAS COUNTED
    // rather than restating the well's gate -- a second copy of a predicate answers a
    // slightly different question sooner or later.
    std::string_view phrase;
    int count;
};

std::vector<HeroDiagnostic> build_diagnostics(const Ctx& ctx, const std::vector<HeroRoll>& rolls,
                                              std::size_t realm_count) {
    int beast_ruins = 0, self_magic_ruins = 0, keyed_ruins = 0, defeated_ruins = 0;
    std::set<std::string> defeated;
    for (const HeroWar& war : ctx.req->wars) defeated.insert(war.defeated_uid);
    for (const HeroRuin& ruin : ctx.req->ruins) {
        if (contains(ctx.words->beast_causes, ruin.cause)) ++beast_ruins;
        if (ruin.cause == ctx.words->self_magic_cause) ++self_magic_ruins;
        if (ctx.ley_by_node.count(ruin.id + "-key")) ++keyed_ruins;
        if (defeated.count(ruin.uid)) ++defeated_ruins;
    }
    int council_seats = 0, diaspora_cities = 0;
    for (const HeroCity& city : ctx.req->cities) {
        council_seats += static_cast<int>(hero_seats_for(city.city_class).size());
        if (!hero_word(ctx.words->diaspora_roles, city.diaspora_reason).empty()) ++diaspora_cities;
    }
    int shrines = 0, cults = 0;
    for (const HeroReligionSite& site : ctx.req->religion_sites) {
        if (site.kind == "shrine") ++shrines;
        if (site.kind == "cult") ++cults;
    }
    const int ruin_count = static_cast<int>(ctx.req->ruins.size());
    const int city_count = static_cast<int>(ctx.req->cities.size());
    const std::vector<SourceRow> sources = {
        {"heir", "ruined cities", ruin_count},
        {"warlord", "ruined cities that lost a recorded war", defeated_ruins},
        {"dread", "ruins a beast destroyed", beast_ruins},
        {"domain_holder", "ruin key points standing in a ley network", keyed_ruins},
        {"magister", "cities that destroyed themselves in a magical experiment", self_magic_ruins},
        {"college_magister", "living magic colleges", static_cast<int>(ctx.req->colleges.size())},
        {"camp", "ruined cities", ruin_count},
        {"sovereign", "realms", static_cast<int>(realm_count)},
        {"council", "council seats the city classes of the living cities declare", council_seats},
        {"diaspora", "living cities founded by a diaspora", diaspora_cities},
        {"champion", "living cities and cities a beast took", city_count + beast_ruins},
        {"reeve", "hamlets", static_cast<int>(ctx.req->hamlets.size())},
        {"castellan", "fortresses", static_cast<int>(ctx.req->fortresses.size())},
        {"harbourmaster", "ports", static_cast<int>(ctx.req->ports.size())},
        {"keeper", "shrines", shrines},
        {"cult", "cults", cults},
    };

    struct Tally {
        int candidates = 0;
        int placed = 0;
        double best = 0.0;
    };
    std::map<std::string, Tally> by_kind;
    for (const HeroRoll& row : rolls) {
        Tally& tally = by_kind[row.kind];
        ++tally.candidates;
        if (row.precipitated) ++tally.placed;
        tally.best = std::max(tally.best, row.chance);
    }

    std::vector<HeroDiagnostic> table;
    // `role` is the key in the wells policy's own precipitation table, so a role that
    // stops firing cannot fall out of the report by falling out of the output. Sorted by
    // role, so the table is stable however the wells run.
    std::vector<const SourceRow*> ordered;
    for (const SourceRow& row : sources) ordered.push_back(&row);
    std::sort(ordered.begin(), ordered.end(),
              [](const SourceRow* a, const SourceRow* b) { return a->role < b->role; });

    for (const SourceRow* row : ordered) {
        // Four roles mint their roll under the PERSON'S own word rather than the policy
        // key. `diaspora` maps to three of them -- and NOT to `heresiarch`, which a
        // prophet becomes when the mother civilization still stands, so a precipitated
        // heresiarch is counted in no row at all. Ported as it stands; see the report.
        std::vector<std::string> kinds;
        if (row->role == "diaspora") {
            kinds = {"prophet", "exile", "founder"};
        } else {
            kinds = {std::string(row->role)};
        }
        Tally total;
        for (const std::string& kind : kinds) {
            auto it = by_kind.find(kind);
            if (it == by_kind.end()) continue;
            total.candidates += it->second.candidates;
            total.placed += it->second.placed;
            total.best = std::max(total.best, it->second.best);
        }
        const bool one = total.candidates == 1;
        std::string reason;
        if (total.placed) {
            reason = std::to_string(total.placed) + " of " + std::to_string(total.candidates) +
                     " candidates precipitated";
        } else if (total.candidates) {
            reason = std::to_string(total.candidates) + " candidate" + (one ? "" : "s") +
                     " rolled and " + (one ? "it" : "none") +
                     " did not precipitate; the best chance offered was " + number_text(total.best);
        } else if (row->count) {
            reason = std::to_string(row->count) + " " + std::string(row->phrase) +
                     " were read and none produced a candidate";
        } else {
            reason = "no " + std::string(row->phrase) + " in this world, so nothing rolled";
        }
        table.push_back({std::string(row->role), total.placed, total.candidates, row->count,
                         std::string(row->phrase), reason});
    }
    return table;
}

// ---------------------------------------------------------------------------
// Finishing a person: alignment, card, name, faces, persona, log, situation.
// ---------------------------------------------------------------------------

void finish_person(Ctx& ctx, Hero& person, const std::map<std::string, const HeroCamp*>& camps) {
    // ONE stream, opened first and used by the card choice and then by `intensify`.
    Rng draw = stream(ctx.req->seed, "hero-archetype-" + person.uid);
    // Derived BEFORE the card, so a card is chosen for a person who already has a moral
    // colour; the intensity push is applied after.
    HeroAlignment alignment = derive_person_alignment(ctx, person);
    const HeroArchetypeCard* card = choose_archetype(person.features, alignment, draw);
    if (card == nullptr) {
        std::string listed;
        for (const std::string& feature : person.features) {
            if (!listed.empty()) listed += ", ";
            listed += feature;
        }
        throw std::runtime_error("no archetype precondition holds for " + person.uid + ": " + listed);
    }
    alignment = intensify(alignment, card->intensity, draw);
    alignment.has_drift = card->has_drift;
    alignment.drift = card->drift;

    const auto [name, epithet] = person_name(ctx, person);
    person.name = name;
    person.epithet = epithet;
    person.display_name = name + " " + epithet;
    person.archetype = std::string(card->id);
    person.archetype_name = std::string(card->name);
    person.alignment = alignment;

    person.faces = build_faces(ctx, *card, person, alignment);
    const std::string situation =
        person.presence.has_value() ? person.presence->situation : std::string();
    // One face -> that one; public/true -> the TRUE persona; calm/unleashed -> calm;
    // first/second -> first. The mask is never what the orchestrator plays from.
    for (const HeroFace& face : person.faces) {
        if (person.faces.size() == 1 ||
            (face.label != "public" && face.label != "unleashed" && face.label != "second")) {
            person.persona = face.persona;
            break;
        }
    }
    person.log = person_log(ctx, person);
    person.situation = person_situation(ctx, person, camps);
    const std::string_view join = hero_word(ctx.words->companion_join, situation);
    person.companion.eligible = person.status == "living" && !join.empty();
    person.companion.join_condition = person.status == "living" ? std::string(join) : std::string();
    person.companion.offered_from_face = join.empty() ? std::string() : person.faces.front().label;
    person.seed = static_cast<std::uint32_t>(stream(ctx.req->seed, "hero-seed-" + person.uid).next_u64() >> 32);
    person.selectable.assign(person.features.begin(), person.features.end());
}

void finish_dread(Ctx& ctx, HeroDread& dread) {
    Rng draw = stream(ctx.req->seed, "hero-archetype-" + dread.uid);
    HeroAlignment alignment = derive_dread_alignment(ctx, dread);
    const HeroArchetypeCard* card = choose_archetype(dread.features, alignment, draw);
    if (card != nullptr) {
        // A NULL CARD IS TOLERATED here where it is a hard error for a person: a monster
        // with no archetype simply has none.
        alignment = intensify(alignment, card->intensity, draw);
        alignment.has_drift = card->has_drift;
        alignment.drift = card->drift;
        dread.archetype = std::string(card->id);
        dread.archetype_name = std::string(card->name);
        dread.persona = compose_persona(*card, alignment);
        dread.has_persona = true;
    }
    const std::string epithet = dread_name(ctx, dread);
    dread.name = epithet;
    dread.display_name = dread.species + ", " + epithet;
    dread.alignment = alignment;
    dread.log = dread_log(dread);
    dread.situation = dread_situation(dread);
    dread.seed = static_cast<std::uint32_t>(stream(ctx.req->seed, "hero-seed-" + dread.uid).next_u64() >> 32);
    dread.selectable.assign(dread.features.begin(), dread.features.end());
}

}  // namespace

// ---------------------------------------------------------------------------
// Published helpers.
// ---------------------------------------------------------------------------

std::string hero_small_site_anchor(std::string_view kind, int node, std::string_view ordinal_id) {
    if (node >= 0) return std::string(kind) + "-node-" + std::to_string(node);
    // A site with no node keeps its ordinal id: there is nothing stabler to key it on, and
    // a world whose small-site records have lost their node is broken upstream of here.
    return std::string(ordinal_id);
}

std::vector<std::string_view> hero_epithet_candidates(std::string_view role, bool legend) {
    std::vector<std::string_view> candidates;
    for (const std::string_view entry : hero_epithets_for(role)) candidates.push_back(entry);
    if (legend) {
        // APPENDED AT THE TAIL, never merged: the role's own entries keep indices 0..n-1,
        // so adding the legend list does not re-map the draws a living person would make.
        for (const std::string_view entry : hero_epithets_for("legend")) candidates.push_back(entry);
    }
    return candidates;
}

double hero_separation_m(const Vec3& a, const Vec3& b, double radius_m) {
    return radius_m * std::acos(std::max(-1.0, std::min(1.0, dot(a, b))));
}

std::string hero_short_name(std::string_view name) {
    std::string out(name);
    const std::string_view suffix = " City";
    for (std::size_t at = out.find(suffix); at != std::string::npos; at = out.find(suffix, at)) {
        out.erase(at, suffix.size());
    }
    return out;
}

std::string hero_ley_node_name(const HeroesRequest& request, const std::string& node_id, const std::string& school) {
    const HeroLeyNetwork* network = nullptr;
    for (const HeroLeyNetwork& net : request.networks) {
        if (net.school == school) network = &net;
    }
    const HeroLeyNodeRef* self = nullptr;
    if (network != nullptr) {
        for (const HeroLeyNodeRef& node : network->nodes) {
            if (node.id == node_id) self = &node;
        }
    }
    if (self == nullptr) return {};
    // How many of the school's nodes lie nearer `from` than this one, plus one.
    const auto rank_from = [&](const Vec3& from) {
        const double own = hero_separation_m(from, self->direction, 1.0);
        int rank = 1;
        for (const HeroLeyNodeRef& other : network->nodes) {
            if (&other == self) continue;
            const double angle = hero_separation_m(from, other.direction, 1.0);
            if (angle < own || (angle == own && other.id < node_id)) ++rank;
        }
        return rank;
    };
    // Every city and ruin a player could be pointed to, by the words that name it.
    struct Place {
        const Vec3* direction = nullptr;
        int kind = 0;  // 0 a city, 1 a ruin
        const std::string* key = nullptr;
        std::string words;
        int rank = 0;
        double angle = 0.0;
    };
    std::vector<Place> places;
    for (const HeroCity& city : request.cities) places.push_back({&city.direction, 0, &city.uid, hero_short_name(city.name)});
    for (const HeroRuin& ruin : request.ruins) {
        if (ruin.has_direction) places.push_back({&ruin.direction, 1, &ruin.uid, "the ruins of " + hero_short_name(ruin.name)});
    }
    std::map<std::string, int> sharing;
    for (const Place& place : places) ++sharing[place.words];
    // The place a player is told of: the lowest count, then the place nearest the node, then
    // cities before ruins, then the key.
    const auto order = [](const Place& p) { return std::tie(p.rank, p.angle, p.kind, *p.key); };
    const Place* best = nullptr;
    for (Place& place : places) {
        if (sharing[place.words] != 1) continue;
        place.rank = rank_from(*place.direction);
        place.angle = hero_separation_m(*place.direction, self->direction, 1.0);
        if (best == nullptr || order(place) < order(*best)) best = &place;
    }
    if (best == nullptr) return {};
    if (best->rank == 1) return "the " + school + " node nearest " + best->words;
    static constexpr std::string_view kCounted[] = {"second", "third", "fourth", "fifth", "sixth",
                                                    "seventh", "eighth", "ninth", "tenth"};
    if (best->rank > 10) return {};
    return "the " + std::string(kCounted[best->rank - 2]) + " " + school + " node from " + best->words;
}

HeroAlignment hero_alignment_record(double law, double good) {
    static constexpr const char* kLawNames[] = {"Chaotic", "Neutral", "Lawful"};
    static constexpr const char* kGoodNames[] = {"Evil", "Neutral", "Good"};
    HeroAlignment out;
    out.law = round3(clamp_axis(law));
    out.good = round3(clamp_axis(good));
    const int l = pole_index(out.law);
    const int g = pole_index(out.good);
    out.label = l == 1 && g == 1 ? std::string("True Neutral")
                                 : std::string(kLawNames[l]) + " " + kGoodNames[g];
    out.code = std::string(1, "CNL"[l]) + std::string(1, "ENG"[g]);
    return out;
}

const HeroArchetypeCard* choose_archetype(const std::set<std::string>& features,
                                          const HeroAlignment& alignment, Rng& draw) {
    std::vector<std::pair<const HeroArchetypeCard*, double>> options;
    double total = 0.0;
    for (const HeroArchetypeCard& card : hero_archetype_cards()) {
        bool eligible = false;
        for (const HeroClause& clause : card.requires_any) {
            bool holds = true;
            for (const std::string_view token : clause.tokens) {
                if (!features.count(std::string(token))) {
                    holds = false;
                    break;
                }
            }
            if (holds) {
                eligible = true;
                break;
            }
        }
        if (!eligible) continue;
        double value = 1.0;
        for (const HeroBoost& boost : card.boosts) {
            if (features.count(std::string(boost.feature))) value *= boost.factor;
        }
        // Nothing is clamped by alignment: a prior only changes frequency. The 0.75 gain
        // and the 0.25 floor are code and not policy.
        const double match = card.prior.law * alignment.law + card.prior.good * alignment.good;
        value *= std::max(0.25, 1.0 + 0.75 * match);
        options.emplace_back(&card, value);
        total += value;
    }
    if (total <= 0.0) return nullptr;
    double selector = draw.random() * total;
    for (const auto& [card, weight] : options) {
        selector -= weight;
        if (selector <= 0.0) return card;
    }
    // Float drift: the cumulative subtraction can end a hair above zero. This is a guard,
    // not a branch that can be dropped.
    return options.back().first;
}

HeroPersona compose_persona(const HeroArchetypeCard& card, const HeroAlignment& alignment) {
    const int l = pole_index(alignment.law);
    const int g = pole_index(alignment.good);
    const HeroOverlay& law = hero_law_overlays()[static_cast<std::size_t>(l)];
    const HeroOverlay& good = hero_good_overlays()[static_cast<std::size_t>(g)];

    HeroPersona persona;
    persona.archetype = std::string(card.id);
    persona.belief = std::string(card.core.belief);
    persona.wants = std::string(card.core.wants);
    persona.fears = std::string(card.core.fears);
    persona.tells = std::string(card.core.tells);
    persona.mechanic = std::string(card.core.mechanic);

    std::vector<std::string_view> manner;
    if (l == 2) manner.push_back(card.pole_notes.lawful);
    if (l == 0) manner.push_back(card.pole_notes.chaotic);
    if (g == 2) manner.push_back(card.pole_notes.good);
    if (g == 0) manner.push_back(card.pole_notes.evil);
    for (const std::string_view note : manner) {
        if (!persona.manner.empty()) persona.manner.push_back(' ');
        persona.manner += std::string(note);
    }
    if (persona.manner.empty()) {
        persona.manner =
            "Neither law nor morality pulls harder than the other; the archetype shows almost unmodified.";
    }
    // NOT uniform, and copied literally: voice and cornered are LAW FIRST, lies and
    // toward_player are GOOD FIRST.
    persona.voice = std::string(law.voice) + " " + std::string(good.voice);
    persona.lies = std::string(good.lies) + " " + std::string(law.lies);
    persona.toward_player = std::string(good.toward_player) + " " + std::string(law.toward_player);
    persona.cornered = std::string(law.cornered) + " " + std::string(good.cornered);

    for (const std::string_view entry : card.core.never) persona.never.push_back(std::string(entry));
    for (const std::string_view entry : law.never) {
        if (std::find(persona.never.begin(), persona.never.end(), entry) == persona.never.end()) {
            persona.never.push_back(std::string(entry));
        }
    }
    for (const std::string_view entry : good.never) {
        if (std::find(persona.never.begin(), persona.never.end(), entry) == persona.never.end()) {
            persona.never.push_back(std::string(entry));
        }
    }

    for (const HeroLine& line : card.lines) {
        if (line.code == alignment.code) {
            persona.line = std::string(line.text);
            break;
        }
    }
    if (persona.line.empty()) {
        for (const HeroLine& line : card.lines) {
            if (line.code.size() == 2 && line.code[1] == alignment.code[1]) {
                persona.line = std::string(line.text);
                break;
            }
        }
    }
    // The first line IN FILE ORDER. 27 of the 32 cards carry no neutral-good line, so this
    // branch is what every neutral-good person in every world says.
    if (persona.line.empty() && !card.lines.empty()) persona.line = std::string(card.lines[0].text);
    return persona;
}

int hero_hook_difficulty(const HeroEffect& effect, double opposition_intensity, bool target_has_grid,
                         int target_x, int target_z, const std::vector<HeroNest>& nests,
                         double spacing_m) {
    const HeroWellsPolicy& w = hero_wells_policy();
    const HeroWordTables& words = hero_word_tables();
    double opposition = 0.0;
    if (effect.kind == "nest") {
        // `or` semantics: a nest with tier 0 reads as the floor, not as 0.
        opposition = opposition_intensity > 0.0 ? opposition_intensity
                                                : static_cast<double>(w.difficulty_min);
    } else if (effect.kind == "ley_node") {
        opposition = w.ley_floor + w.ley_intensity_weight * opposition_intensity;
    } else {
        opposition = hero_opposition_for(effect.kind);
    }
    double pressure = 0.0;
    if (!contains(words.pressure_exempt, effect.kind) && target_has_grid) {
        int worst = 0;
        for (const HeroNest& nest : nests) {
            if (!nest.has_grid) continue;
            const double reach = nest.has_range ? nest.range_m : 0.0;
            if (reach <= 0.0) continue;
            if (grid_metres(target_x, target_z, nest.x, nest.z, spacing_m) <= reach) {
                worst = std::max(worst, nest.has_tier ? nest.tier : 0);
            }
        }
        // Nothing reaching it is 0.0, which is a statement about the ground rather than a
        // missing value: quiet country is the common case and it should read as quiet.
        pressure = std::min(w.pressure_cap, w.pressure_per_tier * static_cast<double>(worst));
    }
    const int banded = static_cast<int>(std::floor(opposition + pressure + 0.5));
    return std::max(w.difficulty_min, std::min(w.difficulty_max, banded));
}

HeroesBlock resolve_heroes(const HeroesRequest& request) {
    lint_hero_catalogue();
    if (request.final_age < 0) throw std::runtime_error("hero generation needs a non-negative final age");
    // A DELIBERATE DEPARTURE. The reference defaults a missing globe radius to 1.0, which
    // turns every great-circle "distance" into an angle in radians and then compares it
    // against a nest's range in metres -- so the champion well silently produces nobody.
    // A radius is not a thing to guess at in a metre-denominated world.
    if (!(request.globe_radius_m > 0.0)) {
        throw std::runtime_error("hero generation needs a positive globe radius in metres "
                                 "(world.effective.globe_radius, not world.config's)");
    }
    if (!(request.spacing_m > 0.0)) {
        throw std::runtime_error("hero generation needs a positive spacing_m");
    }

    Ctx ctx;
    ctx.req = &request;
    ctx.wells = &hero_wells_policy();
    ctx.words = &hero_word_tables();
    ctx.final_age = request.final_age;
    ctx.radius = request.globe_radius_m;
    ctx.spacing = request.spacing_m;
    for (const HeroCity& city : request.cities) {
        ctx.city_by_uid[city.uid] = &city;
        if (!city.id.empty()) ctx.city_by_id[city.id] = &city;
    }
    for (const HeroRuin& ruin : request.ruins) {
        ctx.ruin_by_uid[ruin.uid] = &ruin;
        if (!ruin.id.empty()) ctx.ruin_by_id[ruin.id] = &ruin;
        ++ctx.lost_per_civilization[ruin.civilization_id];
    }
    for (const HeroNest& nest : request.nests) ctx.nest_by_id[nest.id] = &nest;
    for (const HeroCivilization& civ : request.civilizations) ctx.civ_by_id[civ.id] = &civ;
    for (const HeroReligionSite& site : request.religion_sites) {
        if (!site.id.empty()) ctx.shrine_by_id[site.id] = &site;
    }
    for (const HeroPort& port : request.ports) {
        if (!port.id.empty()) ctx.port_by_id[port.id] = &port;
    }
    for (const HeroCollege& college : request.colleges) {
        if (!college.id.empty()) ctx.college_by_id[college.id] = &college;
    }
    // LAST war naming a given defeated uid wins.
    for (const HeroWar& war : request.wars) ctx.war_by_defeated[war.defeated_uid] = &war;
    for (const HeroLeyNetwork& network : request.networks) {
        for (const HeroLeyNodeRef& node : network.nodes) {
            ctx.ley_by_node[node.id] = {network.school, &node};
            ctx.intensity_by_node[node.id] = node.intensity;
        }
    }

    std::vector<HeroRealm> realms = build_realms(ctx);
    const std::map<std::string, std::size_t> by_city = realm_by_city(realms);

    std::vector<Hero> people;
    std::vector<HeroDread> dreads;
    std::vector<HeroRoll> rolls;
    std::vector<HeroOrg> orgs;
    // THE WELL ORDER IS THE LEDGER ORDER, and `rolls` is exported and aggregated by the
    // diagnostics, so it is output and not scratch.
    run_ruins_well(ctx, by_city, realms, people, dreads, rolls);
    run_magic_well(ctx, by_city, realms, people, rolls);
    run_cities_well(ctx, realms, by_city, people, rolls, orgs);
    run_guilds_well(ctx, by_city, realms, people, rolls, orgs);
    run_countryside_well(ctx, by_city, realms, dreads, people, rolls);
    run_ports_well(ctx, by_city, realms, people, rolls);
    run_shrines_well(ctx, by_city, realms, people, rolls);
    // Camps roll AFTER all seven wells, not in well order.
    std::vector<HeroCamp> camps = build_camps(ctx, rolls);
    place_people(ctx, people, camps);

    std::map<std::string, const HeroCamp*> camps_by_uid;
    for (const HeroCamp& camp : camps) camps_by_uid[camp.uid] = &camp;
    std::map<std::string, std::size_t> realm_index;
    for (std::size_t i = 0; i < realms.size(); ++i) realm_index[realms[i].uid] = i;

    // EVERYTHING DOWNSTREAM DEPENDS ON THIS ORDER: fame, the cross-cutting features, the
    // seated/unseated archetype passes, bonds, hooks and the exported array all inherit it.
    std::sort(people.begin(), people.end(), [](const Hero& a, const Hero& b) {
        return a.born_age != b.born_age ? a.born_age < b.born_age : a.uid < b.uid;
    });

    for (Hero& person : people) {
        person.fame = round3(person_magnitude(person) * (1.0 + person_reach(ctx, person, realms, realm_index)));
        person.tier = fame_tier(person.fame);
        if (person.tier == "renowned" || person.tier == "legendary") person.features.insert("fame:renowned");
        if (person.tier == "legendary") person.features.insert("fame:legendary");
        // Where the person stands, as a globe direction, for the opposing-node lookup only.
        const HeroPresence* presence = person.presence.has_value() ? &*person.presence : nullptr;
        if (presence != nullptr && presence->site_kind == "city") {
            auto it = ctx.city_by_uid.find(presence->uid);
            if (it != ctx.city_by_uid.end()) person.anchor_direction = it->second->direction;
        } else if (presence != nullptr && presence->site_kind == "ruin") {
            auto it = ctx.ruin_by_uid.find(presence->uid);
            if (it != ctx.ruin_by_uid.end()) person.anchor_direction = it->second->direction;
        } else if (presence != nullptr && presence->site_kind == "camp") {
            auto it = camps_by_uid.find(presence->uid);
            if (it != camps_by_uid.end()) person.anchor_direction = it->second->direction;
        }
        if (!person.anchor_direction.has_value() && person.lost.has_value()) {
            auto it = ctx.ruin_by_uid.find(person.lost->uid);
            if (it != ctx.ruin_by_uid.end()) person.anchor_direction = it->second->direction;
        }
        if (!person.anchor_direction.has_value() && person.home.has_value()) {
            auto it = ctx.city_by_uid.find(person.home->uid);
            if (it != ctx.city_by_uid.end()) person.anchor_direction = it->second->direction;
        }
    }

    // A living heir inherits when a legend of their own people left the same claim open.
    std::vector<const Hero*> legends;
    for (const Hero& person : people) {
        if (person.status == "legend" && person.claim.verb == "retake") legends.push_back(&person);
    }
    for (Hero& person : people) {
        if (person.status != "living") continue;
        const Hero* best = nullptr;
        for (const Hero* legend : legends) {
            if (legend->civilization_id != person.civilization_id ||
                legend->born_age >= person.born_age) {
                continue;
            }
            // The most famous, uid-tie-broken.
            if (best == nullptr || legend->fame > best->fame ||
                (legend->fame == best->fame && legend->uid < best->uid)) {
                best = legend;
            }
        }
        if (best != nullptr) {
            person.features.insert("legend:predecessor");
            person.predecessor_uid = best->uid;
        }
    }

    apply_cross_cutting(ctx, people);

    // TWO PASSES, so a Tyrant can make Rebels: a Rebel card requires realm:tyrant AND
    // seat:none, and realm:tyrant is only knowable once the seated have their cards.
    std::vector<Hero*> seated;
    for (Hero& person : people) {
        if (contains(ctx.words->seated_roles, person.role)) seated.push_back(&person);
    }
    for (Hero* person : seated) finish_person(ctx, *person, camps_by_uid);
    std::set<std::string> tyrant_civs;
    for (const Hero* person : seated) {
        if (person->archetype == "tyrant") tyrant_civs.insert(person->civilization_id);
    }
    for (Hero& person : people) {
        if (person.archetype.empty() && tyrant_civs.count(person.civilization_id) &&
            person.features.count("seat:none")) {
            person.features.insert("realm:tyrant");
        }
    }
    for (Hero& person : people) {
        if (person.archetype.empty()) finish_person(ctx, person, camps_by_uid);
    }

    std::map<std::string, const HeroNest*> nests_by_id = ctx.nest_by_id;
    std::set<std::string> dread_names;
    for (HeroDread& dread : dreads) {
        double magnitude = 0.0;
        for (const HeroDeed& deed : dread.deeds) {
            magnitude += 0.6 * static_cast<double>(dread.nest_tier) *
                         class_weight(deed.city_class.empty() ? "small" : deed.city_class);
        }
        auto nest = nests_by_id.find(dread.nest_id);
        // RANGE_M IS ABSOLUTE METRES, so this is "reach in kilometres, capped at 3".
        const double reach = nest != nests_by_id.end()
                                 ? std::min(3.0, (nest->second->has_range ? nest->second->range_m : 1000.0) / 1000.0)
                                 : hero_fame_tuning().legend_reach;
        dread.fame = round3(magnitude * (1.0 + reach));
        dread.tier = fame_tier(dread.fame);
        if (dread.tier == "renowned" || dread.tier == "legendary") dread.features.insert("fame:renowned");
        if (dread.tier == "legendary") dread.features.insert("fame:legendary");
        finish_dread(ctx, dread);
        rename_dread_if_taken(dread, dread_names);
    }

    std::vector<HeroRelic> relics = build_relics(ctx);
    std::vector<HeroQuestHook> hooks = build_quest_hooks(ctx, people, relics, dreads);
    for (Hero& person : people) {
        person.anchor_direction.reset();
        person.dispossessed = false;
    }

    std::vector<HeroBond> bonds = build_bonds(people, dreads);
    for (Hero& person : people) {
        std::set<std::string> rivals, kin, allies;
        for (const HeroBond& edge : bonds) {
            if (edge.a != person.uid && edge.b != person.uid) continue;
            const std::string& other = edge.a == person.uid ? edge.b : edge.a;
            if (edge.kind == "rival" || edge.kind == "betrayed") rivals.insert(other);
            if (edge.kind == "kin") kin.insert(other);
            if (edge.kind == "ally" || edge.kind == "mentor") allies.insert(other);
        }
        person.rivals.assign(rivals.begin(), rivals.end());
        person.kin.assign(kin.begin(), kin.end());
        person.allies.assign(allies.begin(), allies.end());
    }

    // An org with no members is dropped; relative order is preserved.
    orgs.erase(std::remove_if(orgs.begin(), orgs.end(),
                              [](const HeroOrg& org) { return org.members.empty(); }),
               orgs.end());
    // A DELIBERATE DEPARTURE. The reference sets `org_uid` to null and never writes it
    // again, so the person->org back-reference is dead and a consumer asking "which council
    // is this person on" has to scan every org. Filling it from the orgs' own member lists
    // is evidently what the field was for; the roles are disjoint, so nobody gets two.
    std::map<std::string, std::string> org_of;
    for (const HeroOrg& org : orgs) {
        for (const std::string& member : org.members) org_of.try_emplace(member, org.uid);
    }
    for (Hero& person : people) {
        auto it = org_of.find(person.uid);
        if (it != org_of.end()) person.org_uid = it->second;
    }

    HeroesBlock block;
    block.policy_revision = hero_catalogue_identity();
    block.final_age = request.final_age;
    for (const Hero& person : people) {
        if (person.status == "living") ++block.summary.living;
        if (person.status == "legend") ++block.summary.legends;
    }
    block.summary.dreads = static_cast<int>(dreads.size());
    block.summary.realms = static_cast<int>(realms.size());
    block.summary.orgs = static_cast<int>(orgs.size());
    block.summary.camps = static_cast<int>(camps.size());
    block.summary.hooks = static_cast<int>(hooks.size());
    block.summary.candidates = static_cast<int>(rolls.size());
    for (const HeroRoll& row : rolls) {
        if (row.precipitated) ++block.summary.precipitated;
    }
    block.diagnostics = build_diagnostics(ctx, rolls, realms.size());
    block.mantles = build_mantles(people);
    block.people = std::move(people);
    block.orgs = std::move(orgs);
    block.realms = std::move(realms);
    block.dreads = std::move(dreads);
    block.camps = std::move(camps);
    block.quest_hooks = std::move(hooks);
    block.bonds = std::move(bonds);
    block.relics = std::move(relics);
    block.rolls = std::move(rolls);
    block.method = std::string(heroes_method());
    block.limits = std::string(heroes_limits());
    return block;
}

HeroesBlock attach_heroes(const HeroesRequest& request) {
    try {
        return resolve_heroes(request);
    } catch (const std::exception& error) {
        // The whole point is to report, not to crash the world. A raising revision yields
        // a failed block and the world still returns.
        HeroesBlock failed;
        failed.status = "failed";
        failed.error = std::string("error: ") + error.what();
        return failed;
    }
}

void lint_hero_catalogue() {
    const auto cards = hero_archetype_cards();
    if (cards.empty()) throw std::runtime_error("the archetype catalogue is empty");
    const auto features = hero_feature_vocabulary();
    for (const HeroArchetypeCard& card : cards) {
        if (card.requires_any.empty()) {
            throw std::runtime_error("archetype " + std::string(card.id) + " has no precondition");
        }
        if (card.lines.empty()) {
            throw std::runtime_error("archetype " + std::string(card.id) + " has no sample line");
        }
        for (const HeroClause& clause : card.requires_any) {
            if (clause.tokens.empty()) {
                throw std::runtime_error("archetype " + std::string(card.id) + " has an empty clause");
            }
            for (const std::string_view token : clause.tokens) {
                if (!contains(features, token)) {
                    throw std::runtime_error("archetype " + std::string(card.id) + " requires " +
                                             std::string(token) + ", which is not a hero feature");
                }
            }
        }
    }
    if (hero_law_overlays().size() != 3 || hero_good_overlays().size() != 3) {
        throw std::runtime_error("each axis needs exactly three pole overlays");
    }
    if (hero_covers_for("default").empty()) {
        throw std::runtime_error("cover_roles.default is the required fallback");
    }
    // The reference's ten, and the two a cleansed city's refugees offer (rebuild, return;
    // 2026-09-23). The hero pass itself never writes those two.
    if (hero_quest_verbs().size() != 12) {
        throw std::runtime_error("the quest verb vocabulary is closed at twelve words");
    }
}

}  // namespace fwg
