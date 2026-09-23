#include "fwg/story_web.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>

namespace fwg {
namespace {

// Four decimal places, and the PRODUCT IS COMPUTED FROM THE ROUNDED PARTS so a consumer
// can re-derive the weight from the three exported factors. Rounding only at the end would
// export a breakdown that does not multiply out.
double round4(double value) { return std::round(value * 10000.0) / 10000.0; }
double round3(double value) { return std::round(value * 1000.0) / 1000.0; }

bool contains(std::span<const std::string_view> haystack, std::string_view needle) {
    return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
}

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

// ---------------------------------------------------------------------------
// One cast member, read entirely out of the exported block.
//
// People and Dreads run through the same code and differ only in what they carry, so the
// differences are flattened here once instead of branching at thirty call sites. A Dread
// has no claim, no civilization, no home, no rivals and no faces, and therefore always
// reads `claim:none` and `home:none`.
// ---------------------------------------------------------------------------

struct CastView {
    std::string uid, role, status, display_name, archetype, tier;
    HeroAlignment alignment;
    std::string claim_verb, claim_target_name;
    std::string situation, home_uid, home_name, lost_uid, lost_name;
    std::string node_id, civilization_id, realm_uid, species, lost_other;
    std::size_t faces = 1;
    bool companion_eligible = false;
    const std::vector<std::string>* selectable = nullptr;
    const std::vector<HeroDeed>* deeds = nullptr;
    const std::vector<std::string>* rivals = nullptr;

    bool is_dread() const { return role == "dread"; }
    std::string cast_kind() const { return is_dread() ? "dread" : "person"; }
};

CastView view_of(const Hero& person) {
    CastView v;
    v.uid = person.uid;
    v.role = person.role;
    v.status = person.status;
    v.display_name = person.display_name;
    v.archetype = person.archetype;
    v.tier = person.tier;
    v.alignment = person.alignment;
    v.claim_verb = person.claim.verb;
    v.claim_target_name = person.claim.target_name;
    if (person.presence.has_value()) v.situation = person.presence->situation;
    if (person.home.has_value()) {
        v.home_uid = person.home->uid;
        v.home_name = person.home->name;
    }
    if (person.lost.has_value()) {
        v.lost_uid = person.lost->uid;
        v.lost_name = person.lost->name;
    }
    v.node_id = person.node_id;
    v.civilization_id = person.civilization_id;
    v.realm_uid = person.realm_uid;
    if (!person.defeated.empty()) v.lost_other = person.defeated.front().name;
    v.faces = person.faces.empty() ? 1 : person.faces.size();
    v.companion_eligible = person.companion.eligible;
    v.selectable = &person.selectable;
    v.deeds = &person.deeds;
    v.rivals = &person.rivals;
    return v;
}

CastView view_of(const HeroDread& dread) {
    CastView v;
    v.uid = dread.uid;
    v.role = dread.role;
    v.status = dread.status;
    v.display_name = dread.display_name;
    v.archetype = dread.archetype;
    v.tier = dread.tier;
    v.alignment = dread.alignment;
    if (dread.presence.has_value()) v.situation = dread.presence->situation;
    v.species = dread.species;
    v.faces = 1;
    v.selectable = &dread.selectable;
    v.deeds = &dread.deeds;
    return v;
}

// ---------------------------------------------------------------------------
// Facts.
// ---------------------------------------------------------------------------

std::set<std::string> person_facts(const CastView& hero, const std::vector<HeroBond>& bonds,
                                   int final_age) {
    std::set<std::string> facts(hero.selectable->begin(), hero.selectable->end());
    for (const HeroDeed& deed : *hero.deeds) {
        if (deed.event_kind != "war") continue;
        facts.insert("war:veteran");
        if (deed.age == final_age) {
            facts.insert(deed.role == "credited" ? "war:victor_recent" : "war:defeated_recent");
        }
    }
    facts.insert("role:" + hero.role);
    facts.insert("status:" + hero.status);
    if (contains(story_tiers(), hero.tier)) facts.insert("tier:" + hero.tier);
    if (!hero.archetype.empty()) facts.insert("archetype:" + hero.archetype);
    facts.insert("claim:" + (hero.claim_verb.empty() ? std::string("none") : hero.claim_verb));
    for (const HeroBond& edge : bonds) {
        if (edge.a != hero.uid && edge.b != hero.uid) continue;
        if (contains(story_bond_kinds(), edge.kind)) facts.insert("bond:" + edge.kind);
    }
    facts.insert(hero.home_uid.empty() ? "home:none" : "home:set");
    if (!hero.lost_uid.empty()) facts.insert("lost:set");
    // ONLY when the situation is one of the nineteen. A word outside the vocabulary costs
    // the hero its presence fact silently, which is why the list is published.
    if (contains(story_situations(), hero.situation)) facts.insert("presence:" + hero.situation);
    if (hero.faces > 1) facts.insert("faces:two");
    if (hero.companion_eligible) facts.insert("companion:eligible");
    return facts;
}

// World-side lookups computed once per compile, then asked per hero.
struct WorldFacts {
    std::map<std::string, const StoryCityThreat*> threat;
    std::map<std::string, double> deficit, coverage;
    std::map<std::string, double> node_intensity;
    bool surge = false, hollow = false;
    std::set<std::string> living, dread_uids;
    bool dread_living = false;
    std::set<std::string> resting_relics, camp_ruins;
    std::map<std::string, std::set<std::string>> mantles;  // civilization -> predecessor uids
    std::size_t realm_count = 0;
    std::set<std::string> sovereign_civs, tyrant_civs;

    void build(const StoryWebRequest& request) {
        const HeroesBlock& heroes = *request.heroes;
        std::map<std::string, std::string> uid_by_site_id;
        for (const StorySiteId& row : request.site_ids) uid_by_site_id[row.id] = row.uid;
        for (const StoryCityThreat& row : request.threats) threat[row.city_uid] = &row;
        for (const StoryRuralCore& row : request.cores) {
            auto uid = uid_by_site_id.find(row.site_id);
            if (uid != uid_by_site_id.end()) deficit[uid->second] = row.food_deficit;
        }
        for (const StoryFoodCoverage& row : request.coverage) {
            auto uid = uid_by_site_id.find(row.site_id);
            if (uid != uid_by_site_id.end()) coverage[uid->second] = row.food_coverage;
        }
        for (const StoryNodeIntensity& row : request.node_intensity) {
            node_intensity[row.node_id] = row.intensity;
        }
        surge = request.lunar_surge;
        hollow = request.lunar_hollow_night;
        for (const Hero& person : heroes.people) {
            if (person.status == "living") living.insert(person.uid);
        }
        for (const HeroDread& dread : heroes.dreads) {
            dread_uids.insert(dread.uid);
            if (dread.status == "living") {
                living.insert(dread.uid);
                dread_living = true;
            }
        }
        for (const HeroRelic& relic : heroes.relics) {
            if (relic.holder_uid.empty() && !relic.resting_at.empty()) {
                resting_relics.insert(relic.resting_at);
            }
        }
        for (const HeroCamp& camp : heroes.camps) camp_ruins.insert(camp.ruin_uid);
        for (const HeroMantle& mantle : heroes.mantles) {
            mantles[mantle.civilization_id].insert(mantle.predecessor_uid);
        }
        realm_count = heroes.realms.size();
        for (const Hero& person : heroes.people) {
            if (person.status != "living") continue;
            if (person.role == "sovereign") sovereign_civs.insert(person.civilization_id);
            if ((person.role == "sovereign" || person.role == "warlord") &&
                person.archetype == "tyrant") {
                tyrant_civs.insert(person.civilization_id);
            }
        }
    }

    void for_hero(const CastView& hero, std::set<std::string>& facts) const {
        const StoryWeightPolicy& w = story_weight_policy();
        if (!hero.home_uid.empty()) {
            auto report = threat.find(hero.home_uid);
            if (report != threat.end()) {
                const StoryCityThreat& t = *report->second;
                if (t.war_pressure > 0.0) facts.insert("world:war_pressure");
                if (t.wars_recent > 0.0) facts.insert("world:war_recent");
                if (t.enemy_living) facts.insert("world:enemy_living");
                if (t.war_risk > 0.0) facts.insert("world:war_risk");
                if (t.war_hunger > 0.0) facts.insert("world:war_hunger");
                if (t.regional_threat >= 0.5) facts.insert("world:threatened");
            }
            auto rural = deficit.find(hero.home_uid);
            if (rural != deficit.end() && rural->second > 0.0) facts.insert("world:food_deficit");
            // A famine is a year the city cannot feed itself, read from the seasonal food
            // model -- NOT the structural deficit above, which marks nearly every city
            // that lacks farms. A city with no coverage row is not in famine.
            auto fed = coverage.find(hero.home_uid);
            if (fed != coverage.end() && fed->second < w.famine_coverage) facts.insert("world:famine");
        }
        if (surge) facts.insert("world:surge");
        if (hollow) facts.insert("world:hollow_night");
        if (!hero.node_id.empty()) {
            auto node = node_intensity.find(hero.node_id);
            if (node != node_intensity.end() && node->second >= w.node_intense_threshold) {
                facts.insert("world:node_intense");
            }
        }
        if (hero.rivals != nullptr) {
            for (const std::string& rival : *hero.rivals) {
                if (living.count(rival)) facts.insert("world:rival_living");
                if (living.count(rival) && dread_uids.count(rival)) facts.insert("world:rival_dread");
            }
        }
        if (!hero.lost_uid.empty()) {
            if (resting_relics.count(hero.lost_uid)) facts.insert("world:relic_resting");
            if (camp_ruins.count(hero.lost_uid)) facts.insert("world:camp_at_lost");
        }
        auto open = mantles.find(hero.civilization_id);
        if (open != mantles.end()) {
            for (const std::string& held : open->second) {
                // Held by SOMEBODY ELSE: a legend does not leave a mantle open for itself.
                if (held != hero.uid) {
                    facts.insert("world:mantle_open");
                    break;
                }
            }
        }
        if (realm_count >= 2) facts.insert("world:other_realm");
        if (dread_living) facts.insert("world:dread_living");
        if (sovereign_civs.count(hero.civilization_id)) facts.insert("world:realm_has_sovereign");
        if (tyrant_civs.count(hero.civilization_id)) facts.insert("world:realm_tyrant");
    }
};

// ---------------------------------------------------------------------------
// The weight equation. Nothing is rolled: the heaviest spoke is the offer.
// ---------------------------------------------------------------------------

struct Weighted {
    double value = 0.0;
    StoryBreakdown breakdown;
};

Weighted weigh(const StoryTrope& trope, const std::set<std::string>& facts, const CastView& hero,
               const std::string& claim_verb) {
    const StoryWeightPolicy& w = story_weight_policy();
    Weighted out;
    double boosts = 1.0;
    for (const HeroBoost& boost : trope.boosts) {
        if (facts.count(std::string(boost.feature))) {
            boosts *= boost.factor;
            out.breakdown.boosted_by.push_back(std::string(boost.feature));
        }
    }
    const double fit = story_alignment_fit(trope.pull, hero.alignment);
    const double archetype =
        !hero.archetype.empty() && contains(trope.fits, hero.archetype) ? w.fits_factor : 1.0;
    const std::string verb = claim_verb.empty() ? std::string("none") : claim_verb;
    const double claim = contains(trope.claims, verb) ? w.claim_factor : 1.0;
    out.breakdown.boosts = round4(boosts);
    out.breakdown.alignment_fit = round4(fit);
    out.breakdown.archetype_fit = archetype;
    out.breakdown.claim_fit = claim;
    out.value = round4(out.breakdown.boosts * out.breakdown.alignment_fit * archetype * claim);
    return out;
}

// ---------------------------------------------------------------------------
// Binding: prompt and option templates filled with the names the hero's own record gives.
// ---------------------------------------------------------------------------

using Slots = std::map<std::string, std::string>;

Slots slots_for(const CastView& hero, const HeroesBlock& heroes) {
    std::map<std::string, std::string> names;
    for (const Hero& person : heroes.people) {
        names[person.uid] = person.display_name.empty() ? person.uid : person.display_name;
    }
    for (const HeroDread& dread : heroes.dreads) {
        names[dread.uid] = dread.display_name.empty() ? dread.uid : dread.display_name;
    }
    std::map<std::string, std::string> realms;
    for (const HeroRealm& realm : heroes.realms) {
        if (!realm.uid.empty() && !realm.name.empty()) realms[realm.uid] = realm.name;
    }
    std::map<std::string, std::string> relics;
    for (const HeroRelic& relic : heroes.relics) {
        if (relic.holder_uid.empty() && !relic.resting_at.empty() && !relic.name.empty()) {
            relics[relic.resting_at] = relic.name;
        }
    }

    Slots slots;
    slots["hero"] = hero.display_name.empty() ? hero.uid : hero.display_name;
    if (!hero.claim_target_name.empty()) slots["target"] = hero.claim_target_name;
    if (hero.rivals != nullptr && !hero.rivals->empty()) {
        // rivals[0] is the alphabetically first, because the list was sorted upstream.
        const std::string& first = hero.rivals->front();
        auto named = names.find(first);
        slots["rival"] = named == names.end() ? first : named->second;
    } else if (!hero.claim_target_name.empty() &&
               (hero.claim_verb == "kill" || hero.claim_verb == "avenge" ||
                hero.claim_verb == "expose" || hero.claim_verb == "overthrow")) {
        slots["rival"] = hero.claim_target_name;
    }
    if (!hero.home_name.empty()) slots["home"] = hero.home_name;
    if (!hero.lost_name.empty()) {
        slots["lost"] = hero.lost_name;
        auto relic = relics.find(hero.lost_uid);
        if (relic != relics.end()) slots["relic"] = relic->second;
    }
    auto own = realms.find(hero.realm_uid);
    if (!hero.realm_uid.empty() && own != realms.end()) {
        slots["realm"] = own->second;
        std::vector<std::string> others;
        for (const auto& [uid, name] : realms) {
            if (uid != hero.realm_uid) others.push_back(name);
        }
        std::sort(others.begin(), others.end());
        if (!others.empty()) slots["realm_other"] = others.front();
    } else if (!realms.empty()) {
        std::vector<std::string> all;
        for (const auto& [uid, name] : realms) all.push_back(name);
        std::sort(all.begin(), all.end());
        slots["realm_other"] = all.front();
    }
    if (!hero.species.empty()) slots["species"] = hero.species;
    if (!hero.lost_other.empty()) slots["lost_other"] = hero.lost_other;
    return slots;
}

// Substitute `{slot}` and raise the first character of the result.
//
// A slot the record cannot fill is left READABLE ("their rival") rather than invented, and
// a slot nobody has heard of renders back as the literal `{name}` -- which is why this is
// a hand-written scanner and not a format library: the templates are authored data full of
// apostrophes and an unknown token must survive intact.
std::string fill(std::string_view templ, const Slots& slots) {
    std::string out;
    out.reserve(templ.size() + 32);
    std::size_t at = 0;
    while (at < templ.size()) {
        const std::size_t open = templ.find('{', at);
        if (open == std::string_view::npos) {
            out += templ.substr(at);
            break;
        }
        out += templ.substr(at, open - at);
        const std::size_t close = templ.find('}', open);
        if (close == std::string_view::npos) {
            out += templ.substr(open);
            break;
        }
        const std::string key(templ.substr(open + 1, close - open - 1));
        auto filled = slots.find(key);
        if (filled != slots.end()) {
            out += filled->second;
        } else {
            std::string_view fallback;
            for (const StoryFallback& row : story_bind_fallbacks()) {
                if (row.slot == key) fallback = row.text;
            }
            out += fallback.empty() ? "{" + key + "}" : std::string(fallback);
        }
        at = close + 1;
    }
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') {
        out[0] = static_cast<char>(out[0] - 'a' + 'A');
    }
    return out;
}

// ---------------------------------------------------------------------------
// Threads: where a hero can step at an act exit, and the one change each needs.
// ---------------------------------------------------------------------------

struct WayIn {
    bool found = false;
    std::string kind;  // open | claim | feature
    std::string verb;
    std::vector<std::string> facts;
    std::set<std::string> after;
};

WayIn way_in(const HeroClause& clause, const std::set<std::string>& facts,
             const std::set<std::string>& gainable) {
    WayIn out;
    std::vector<std::string> gaps;
    for (const std::string_view token : clause.tokens) {
        if (!story_holds(token, facts)) gaps.emplace_back(token);
    }
    if (gaps.empty()) {
        out.found = true;
        out.kind = "open";
        out.after = facts;
        return out;
    }
    std::vector<std::string> verbs, others;
    for (const std::string& gap : gaps) {
        if (starts_with(gap, "claim:")) {
            verbs.push_back(gap.substr(std::string_view("claim:").size()));
        } else {
            others.push_back(gap);
        }
    }
    if (verbs.size() == 1 && others.empty()) {
        out.found = true;
        out.kind = "claim";
        out.verb = verbs.front();
        // A hero holds exactly one claim, so the new verb REPLACES the old one rather than
        // joining it.
        for (const std::string& fact : facts) {
            if (!starts_with(fact, "claim:")) out.after.insert(fact);
        }
        out.after.insert("claim:" + out.verb);
        return out;
    }
    // At most TWO gainable facts, none of them negated: a runtime event can add a fact but
    // cannot take one away.
    if (verbs.empty() && !others.empty() && others.size() <= 2) {
        for (const std::string& gap : others) {
            if (starts_with(gap, "!") || !gainable.count(gap)) return out;
        }
        out.found = true;
        out.kind = "feature";
        out.facts = others;
        out.after = facts;
        for (const std::string& gap : others) out.after.insert(gap);
        return out;
    }
    return out;
}

std::vector<StoryThread> threads_from(const StoryTrope& offered, const CastView& hero,
                                      const std::set<std::string>& facts,
                                      const StoryConstraint& rules) {
    const StoryWeightPolicy& w = story_weight_policy();
    std::set<std::string> gainable;
    for (const std::string_view fact : story_gainable_features()) gainable.emplace(fact);
    const std::string kind = hero.cast_kind();

    std::vector<StoryThread> out;
    for (const StoryTrope& trope : story_tropes()) {
        if (trope.id == offered.id || !contains(trope.cast, kind)) continue;
        WayIn best;
        int best_rank = 0;
        std::size_t best_size = 0;
        for (const HeroClause& clause : trope.requires_any) {
            const WayIn candidate = way_in(clause, facts, gainable);
            if (!candidate.found) continue;
            const int rank = story_thread_rank(candidate.kind);
            const std::size_t size = candidate.facts.size();
            // A strict less-than on (rank, size), so the FIRST cheapest conjunction wins.
            if (!best.found || rank < best_rank || (rank == best_rank && size < best_size)) {
                best = candidate;
                best_rank = rank;
                best_size = size;
            }
        }
        if (!best.found) continue;

        StoryThread thread;
        thread.to = std::string(trope.id);
        // The weight AFTER the condition holds -- the changed verb or the gained facts
        // applied -- so the order reflects where the hero would land, not where they are.
        thread.weight =
            weigh(trope, best.after, hero, best.kind == "claim" ? best.verb : hero.claim_verb).value;
        thread.condition_kind = best.kind;
        thread.verb = best.verb;
        thread.facts = best.facts;
        if (best.kind == "open") {
            // ONLY an open way in is checked against the pull: a claim or feature thread
            // already names the one change it needs, and the walker re-evaluates alignment
            // when it arrives.
            const auto opposed = story_opposing_axis(trope.pull, hero.alignment);
            if (opposed.has_value()) {
                thread.condition_kind = "alignment";
                thread.axis = opposed->first;
                thread.toward = opposed->second;
                if (rules.drift_locked) thread.cut = "drift locked by archetype";
            }
        } else if (best.kind == "claim") {
            if (rules.claim_locked) {
                thread.cut = "claim locked by archetype";
            } else if (rules.has_allowed_verbs && !contains(rules.allowed_verbs, best.verb)) {
                thread.cut = "claim verb " + best.verb + " not allowed by archetype";
            }
        }
        out.push_back(std::move(thread));
    }
    // Ranked on the FINAL condition, after an open thread may have been rewritten to
    // `alignment` -- so the rewrite moves the thread's sort position.
    std::sort(out.begin(), out.end(), [](const StoryThread& a, const StoryThread& b) {
        const int ra = story_thread_rank(a.condition_kind);
        const int rb = story_thread_rank(b.condition_kind);
        if (ra != rb) return ra < rb;
        if (a.weight != b.weight) return a.weight > b.weight;
        return a.to < b.to;
    });
    if (out.size() > static_cast<std::size_t>(w.max_threads_per_act)) {
        // Note the policy field's NAME: threads belong to the WEB, not to one act, and the
        // cap is applied ONCE PER HERO. Both prose descriptions in the reference say "per
        // act" and the code does not; the code is what is ported.
        out.resize(static_cast<std::size_t>(w.max_threads_per_act));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Reachability. A trope whose every conjunction needs a feature nothing computes is
// documented but cannot fire, and is listed in the ledger rather than hidden.
// ---------------------------------------------------------------------------

bool reachable(const StoryTrope& trope) {
    for (const HeroClause& clause : trope.requires_any) {
        bool all_live = true;
        for (const std::string_view token : clause.tokens) {
            if (starts_with(token, "archetype:")) continue;
            std::string_view bare = token;
            while (starts_with(bare, "!")) bare.remove_prefix(1);
            if (contains(story_never_computed(), bare)) {
                all_live = false;
                break;
            }
        }
        if (all_live) return true;
    }
    return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// Published helpers.
// ---------------------------------------------------------------------------

bool story_holds(std::string_view token, const std::set<std::string>& facts) {
    if (starts_with(token, "!")) return facts.count(std::string(token.substr(1))) == 0;
    return facts.count(std::string(token)) != 0;
}

bool story_eligible(const StoryTrope& trope, const std::set<std::string>& facts) {
    for (const HeroClause& clause : trope.requires_any) {
        bool holds = true;
        for (const std::string_view token : clause.tokens) {
            if (!story_holds(token, facts)) {
                holds = false;
                break;
            }
        }
        if (holds) return true;
    }
    return false;
}

double story_angle_degrees(double law, double good) {
    // Good to the right, lawful up; counter-clockwise from good.
    double degrees = std::atan2(law, good) * 180.0 / 3.14159265358979323846;
    degrees = std::fmod(degrees, 360.0);
    if (degrees < 0.0) degrees += 360.0;
    return std::round(degrees * 100.0) / 100.0;
}

std::optional<std::pair<std::string, std::string>> story_opposing_axis(
    const HeroAxisPair& pull, const HeroAlignment& alignment) {
    const double good = pull.good * alignment.good;
    const double law = pull.law * alignment.law;
    // The SUM decides whether the pull opposes at all: a spoke that drags one axis and
    // lifts the other by as much agrees, and stays open.
    if (good + law >= 0.0) return std::nullopt;
    // The SMALLER contribution, compared ALPHABETICALLY on an exact tie -- so `good` wins.
    const std::string axis = good <= law ? "good" : "law";
    const double pull_on_axis = axis == "good" ? pull.good : pull.law;
    return std::make_pair(axis, pull_on_axis > 0.0 ? "up" : "down");
}

double story_alignment_fit(const HeroAxisPair& pull, const HeroAlignment& alignment) {
    const StoryWeightPolicy& w = story_weight_policy();
    const double dot = pull.law * alignment.law + pull.good * alignment.good;
    // The divisor is floored so a nearly-neutral pull cannot blow the fit up, and a pull
    // of exactly zero therefore gives a fit of exactly 1.0: never favoured, never damped.
    const double norm = std::max(std::hypot(pull.law, pull.good), w.pull_floor);
    return std::max(w.fit_floor, 1.0 + w.fit_gain * dot / norm);
}

void lint_story_catalogue() {
    const auto tropes = story_tropes();
    if (tropes.empty()) throw std::runtime_error("the trope catalogue is empty");
    for (const StoryTrope& trope : tropes) {
        if (trope.acts.size() != 3) {
            throw std::runtime_error("trope " + std::string(trope.id) + " does not carry three acts");
        }
        if (trope.requires_any.empty() || trope.cast.empty()) {
            throw std::runtime_error("trope " + std::string(trope.id) + " has no entry or no cast");
        }
        for (const HeroClause& clause : trope.requires_any) {
            for (const std::string_view token : clause.tokens) {
                if (!story_known_fact(token[0] == '!' ? token.substr(1) : token)) {
                    throw std::runtime_error("trope " + std::string(trope.id) + " requires " +
                                             std::string(token) + ", which is not a known fact");
                }
            }
        }
    }
    for (const std::string_view kind : {"open", "alignment", "claim", "feature"}) {
        if (story_thread_rank(kind) < 0) {
            throw std::runtime_error("the thread rank table lost " + std::string(kind));
        }
    }
    if (story_bind_fallbacks().empty()) {
        throw std::runtime_error("an unfilled slot would render as a brace without the fallbacks");
    }
}

StoryWebBlock resolve_story_web(const StoryWebRequest& request) {
    lint_story_catalogue();
    if (request.heroes == nullptr) {
        throw std::runtime_error("story web needs the heroes block; run the hero generator first");
    }
    const HeroesBlock& heroes = *request.heroes;
    if (heroes.status != "ok") {
        throw std::runtime_error("heroes block is not ok: " + heroes.error);
    }
    const StoryWeightPolicy& w = story_weight_policy();

    WorldFacts world;
    world.build(request);

    // People first, then dreads, and then SORTED BY UID -- the sort makes the concatenation
    // order irrelevant, but the sort itself is required.
    std::vector<CastView> cast;
    for (const Hero& person : heroes.people) {
        if (person.status == "living") cast.push_back(view_of(person));
    }
    for (const HeroDread& dread : heroes.dreads) {
        if (dread.status == "living") cast.push_back(view_of(dread));
    }
    std::sort(cast.begin(), cast.end(),
              [](const CastView& a, const CastView& b) { return a.uid < b.uid; });

    StoryWebBlock block;
    block.policy_revision = story_catalogue_identity();
    block.heroes_revision = heroes.policy_revision;
    std::map<std::string, int> histogram;

    for (const CastView& hero : cast) {
        std::set<std::string> facts = person_facts(hero, heroes.bonds, heroes.final_age);
        world.for_hero(hero, facts);
        const std::string kind = hero.cast_kind();

        std::vector<StorySpoke> spokes;
        std::vector<StoryConsidered> considered;
        for (const StoryTrope& trope : story_tropes()) {
            if (!contains(trope.cast, kind)) continue;
            if (story_eligible(trope, facts)) {
                const Weighted weighted = weigh(trope, facts, hero, hero.claim_verb);
                StorySpoke spoke;
                spoke.trope_id = std::string(trope.id);
                spoke.name = std::string(trope.name);
                spoke.weight = weighted.value;
                spoke.breakdown = weighted.breakdown;
                spoke.angle = story_angle_degrees(trope.pull.law, trope.pull.good);
                spokes.push_back(std::move(spoke));
                continue;
            }
            // The failing tokens of the conjunction closest to holding: fewest failures,
            // then CATALOGUE ORDER, the first such conjunction winning on a strict <.
            std::vector<std::string> missing;
            bool have = false;
            for (const HeroClause& clause : trope.requires_any) {
                std::vector<std::string> failed;
                for (const std::string_view token : clause.tokens) {
                    if (!story_holds(token, facts)) failed.emplace_back(token);
                }
                if (!have || failed.size() < missing.size()) {
                    missing = std::move(failed);
                    have = true;
                }
            }
            considered.push_back({std::string(trope.id), std::move(missing)});
        }
        std::sort(considered.begin(), considered.end(),
                  [](const StoryConsidered& a, const StoryConsidered& b) {
                      if (a.missing.size() != b.missing.size()) return a.missing.size() < b.missing.size();
                      return a.trope_id < b.trope_id;
                  });

        if (spokes.empty()) {
            StoryUnwoven row;
            row.uid = hero.uid;
            row.display_name = hero.display_name;
            row.archetype = hero.archetype;
            row.facts.assign(facts.begin(), facts.end());
            row.nearest.assign(considered.begin(),
                               considered.begin() + std::min<std::size_t>(3, considered.size()));
            block.unwoven.push_back(std::move(row));
            continue;  // an unwoven hero consumes no draw at all
        }

        // Ties break on trope id, which is what makes the offered hook stable when two
        // weights round to the same four decimal places.
        std::sort(spokes.begin(), spokes.end(), [](const StorySpoke& a, const StorySpoke& b) {
            if (a.weight != b.weight) return a.weight > b.weight;
            return a.trope_id < b.trope_id;
        });
        const StoryTrope& offered = *story_trope(spokes.front().trope_id);
        const Slots slots = slots_for(hero, heroes);
        const StoryConstraint& rules = story_constraints_for(hero.archetype);

        StoryWebEntry entry;
        entry.uid = hero.uid;
        entry.display_name = hero.display_name;
        entry.role = hero.role;
        entry.archetype = hero.archetype;
        entry.law = hero.alignment.law;
        entry.good = hero.alignment.good;
        entry.code = hero.alignment.code;
        entry.angle = story_angle_degrees(hero.alignment.law, hero.alignment.good);
        entry.claim = hero.claim_verb.empty() ? "none" : hero.claim_verb;
        entry.facts.assign(facts.begin(), facts.end());
        entry.claim_locked = rules.claim_locked;
        entry.drift_locked = rules.drift_locked;
        entry.has_allowed_verbs = rules.has_allowed_verbs;
        for (const std::string_view verb : rules.allowed_verbs) entry.allowed_verbs.emplace_back(verb);
        entry.offered = spokes.front().trope_id;
        entry.threads = threads_from(offered, hero, facts, rules);
        for (const std::string_view example : offered.examples) entry.examples.emplace_back(example);

        for (std::size_t i = 0; i < offered.acts.size(); ++i) {
            const StoryAct& act = offered.acts[i];
            StoryBoundAct bound;
            bound.id = std::string(act.id);
            bound.title = std::string(act.title);
            bound.ring = static_cast<int>(i) + 1;
            bound.prompt = fill(act.prompt, slots);
            bound.completion_kind = std::string(kStoryCompletionKind);
            bound.completion_target = std::string(act.completion_target);
            bound.completion_state = std::string(act.completion_state);
            bound.abandonment_kind = std::string(kStoryAbandonmentKind);
            bound.abandonment_days = act.abandonment_days;
            for (const std::string_view implicit : kStoryImplicitAbandonment) {
                bound.implicit.emplace_back(implicit);
            }
            for (const StoryOption& option : act.options) {
                bound.options.push_back({std::string(option.id), fill(option.text, slots),
                                         option.delta, std::string(option.effect)});
            }
            entry.acts.push_back(std::move(bound));
        }

        // The ONLY draw in the package, and it decides nothing but the deadline stagger.
        const int initiative =
            w.initiative_days +
            static_cast<int>(Rng::child(request.seed, "web-initiative-" + hero.uid).below(30));
        entry.rest.hook_trope_id = std::string(offered.id);
        entry.rest.hook_act_id = std::string(offered.acts[0].id);
        entry.rest.hook_prompt = entry.acts.front().prompt;
        entry.rest.expires_day = initiative;
        entry.rest.initiative_day = initiative;
        for (std::size_t i = 1; i < spokes.size(); ++i) {
            entry.rest.armed.emplace_back(spokes[i].trope_id, spokes[i].weight);
        }
        entry.spokes = std::move(spokes);
        entry.considered = std::move(considered);
        ++histogram[entry.offered];
        block.webs.push_back(std::move(entry));
    }

    std::size_t reachable_count = 0;
    for (const StoryTrope& trope : story_tropes()) {
        StoryTropeSummary row;
        row.id = std::string(trope.id);
        row.name = std::string(trope.name);
        row.pull = trope.pull;
        for (const std::string_view member : trope.cast) row.cast.emplace_back(member);
        row.angle = story_angle_degrees(trope.pull.law, trope.pull.good);
        block.tropes.push_back(std::move(row));
        if (reachable(trope)) {
            ++reachable_count;
        } else {
            block.unreachable_tropes.emplace_back(trope.id);
        }
    }
    std::sort(block.unreachable_tropes.begin(), block.unreachable_tropes.end());

    block.summary.woven = static_cast<int>(block.webs.size());
    block.summary.unwoven = static_cast<int>(block.unwoven.size());
    block.summary.tropes = static_cast<int>(story_tropes().size());
    block.summary.reachable_tropes = static_cast<int>(reachable_count);
    for (const auto& [trope_id, count] : histogram) block.summary.offered.emplace_back(trope_id, count);
    if (!block.webs.empty()) {
        std::size_t total = 0;
        for (const StoryWebEntry& entry : block.webs) total += entry.spokes.size();
        block.summary.mean_spokes =
            round3(static_cast<double>(total) / static_cast<double>(block.webs.size()));
    }
    block.method = std::string(story_web_method());
    block.limits = std::string(story_web_limits());
    return block;
}

StoryWebBlock attach_story_web(const StoryWebRequest& request) {
    try {
        return resolve_story_web(request);
    } catch (const std::exception& error) {
        StoryWebBlock failed;
        failed.status = "failed";
        failed.error = std::string("error: ") + error.what();
        return failed;
    }
}

}  // namespace fwg
