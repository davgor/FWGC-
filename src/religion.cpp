#include "fwg/religion.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "fwg/entities.hpp"
#include "fwg/random.hpp"

namespace fwg {
namespace {

// The two statuses that mean a god is available: to be worshipped, to be feared, to be a
// cosmology's high god, to be summoned. Written once because four places ask it and a
// fifth that forgot `walking` would quietly retire a god the moment it stepped out.
bool lives(std::string_view status) { return status == "manifest" || status == "walking"; }

bool contains(std::span<const std::string_view> haystack, std::string_view needle) {
    return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
}

bool contains(const std::vector<std::string>& haystack, std::string_view needle) {
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

// One seeded pick from positive weights, in list order.
//
// ALWAYS consumes exactly one uniform draw, even from a one-element list. That is the
// contract, not an accident: a version that short-circuited the single-item case would
// move every later draw on the same stream.
std::size_t draw_index(Rng& rng, const std::vector<double>& weights) {
    if (weights.empty()) throw std::invalid_argument("a weighted draw needs at least one item");
    double total = 0.0;
    for (const double w : weights) total += w;
    double target = rng.random() * total;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        target -= weights[i];
        if (target <= 0.0) return i;
    }
    // Float drift: the cumulative subtraction can end a hair above zero. The last item is
    // the one the walk was about to reach.
    return weights.size() - 1;
}

std::size_t draw_from(Rng& rng, const std::vector<std::pair<std::string, double>>& weighted) {
    std::vector<double> weights;
    weights.reserve(weighted.size());
    for (const auto& entry : weighted) weights.push_back(entry.second);
    return draw_index(rng, weights);
}

bool rule_holds_at(std::span<const RuleNode> rule, std::uint32_t index, const WorldFacts& facts,
                   const GodIdSet& manifest) {
    const RulePools pools = pantheon_rule_pools();
    const RuleNode& node = rule[index];
    const auto names = [&]() {
        return pools.texts.subspan(node.text_begin, node.text_end - node.text_begin);
    };
    switch (node.kind) {
        case RuleKind::always:
            return true;
        case RuleKind::magic_enabled:
            return facts.magic_enabled;
        case RuleKind::magic_disabled:
            return !facts.magic_enabled;
        case RuleKind::school: {
            const int school = magic_school_index(node.school);
            // The reference raises KeyError here rather than reading false, and so does
            // this: a rule naming a school the world does not have is a broken catalogue,
            // and a silent false would raise no god and look like a quiet world.
            if (school < 0 || school >= kKnownSchools) {
                throw std::runtime_error("existence rule names unknown school " +
                                         std::string(node.school));
            }
            return facts.schools[static_cast<std::size_t>(school)];
        }
        case RuleKind::race:
            return contains(facts.races, node.race);
        case RuleKind::races:
            return static_cast<double>(facts.races.size()) >= node.min_value;
        case RuleKind::civilization_city:
            for (const std::string_view id : names()) {
                if (contains(facts.civilizations_with_cities, id)) return true;
            }
            return false;
        case RuleKind::ocean_share:
            return facts.ocean_share >= node.min_value;
        case RuleKind::sky:
            return facts.sky > 0;
        case RuleKind::dragon:
            return facts.dragons > 0;
        case RuleKind::nest_family:
            for (const std::string_view family : names()) {
                if (contains(facts.families, family)) return true;
            }
            return false;
        case RuleKind::ruins:
            return static_cast<double>(facts.ruins) >= node.min_value;
        case RuleKind::diaspora:
            return facts.diaspora > 0;
        case RuleKind::primordial_count:
            return static_cast<double>(facts.primordial_count) >= node.min_value;
        case RuleKind::gods:
            for (const std::string_view id : names()) {
                const bool held = manifest.find(id) != manifest.end();
                if (node.all && !held) return false;
                if (!node.all && held) return true;
            }
            return node.all;
        case RuleKind::any_of:
        case RuleKind::all_of: {
            const bool want_all = node.kind == RuleKind::all_of;
            for (std::uint32_t i = node.child_begin; i < node.child_end; ++i) {
                const bool held = rule_holds_at(rule, pools.children[i], facts, manifest);
                if (want_all && !held) return false;
                if (!want_all && held) return true;
            }
            return want_all;
        }
    }
    throw std::runtime_error("unknown existence rule kind");
}

// The number a cosmology's `weight.fact` names. A LIST contributes its LENGTH --
// cos_ancestor_halls weighs on `races`, which is a sorted list of parent race ids.
double fact_value(const WorldFacts& facts, std::string_view name) {
    if (name == "primordial_count") return static_cast<double>(facts.primordial_count);
    if (name == "races") return static_cast<double>(facts.races.size());
    if (name == "civilizations_with_cities") {
        return static_cast<double>(facts.civilizations_with_cities.size());
    }
    if (name == "families") return static_cast<double>(facts.families.size());
    if (name == "ocean_share") return facts.ocean_share;
    if (name == "sky") return static_cast<double>(facts.sky);
    if (name == "dragons") return static_cast<double>(facts.dragons);
    if (name == "ruins") return static_cast<double>(facts.ruins);
    if (name == "diaspora") return static_cast<double>(facts.diaspora);
    if (name == "wars") return static_cast<double>(facts.wars);
    if (name == "lunar_influence") return facts.lunar_influence;
    // An unnamed fact leaves the weight at its base. The generator refuses a fact it does
    // not recognise, so reaching this with a non-empty name means the table was edited by
    // hand.
    return 0.0;
}

std::string_view fear_god_for(std::string_view family) {
    if (family.empty()) return {};
    for (const FearGod& row : fear_gods()) {
        if (row.family == family) return row.god_id;
    }
    return {};
}

// The school a school-family god exists on -- the one inside its existence rule, which is
// NOT the same field as its affinity list.
std::string_view existence_school(const PantheonGod& god) {
    if (god.exists.empty() || god.exists[0].kind != RuleKind::school) {
        throw std::runtime_error(std::string(god.id) +
                                 " is a school god whose rule does not name a school");
    }
    return god.exists[0].school;
}

std::vector<int> almanac_days(const Almanac* almanac, std::string_view kind, std::string_view school) {
    std::vector<int> days;
    if (almanac == nullptr) return days;
    for (const AlmanacEvent& event : almanac->events) {
        if (event.kind != kind) continue;
        if (!school.empty() && event.school != school) continue;
        days.push_back(event.day);
    }
    return days;
}

Feasts feasts_for(const std::vector<std::string>& chosen,
                  const std::map<std::string, const GodRecord*>& by_id, const Almanac* almanac) {
    Feasts out;
    for (const std::string& god_id : chosen) {
        const PantheonGod* god = pantheon_god(god_id);
        if (god == nullptr || god->family != "school") continue;
        std::vector<int> days = almanac_days(almanac, "surge", existence_school(*god));
        if (!days.empty()) out.surges.emplace_back(god_id, std::move(days));
    }
    if (contains(chosen, kMoonGodId)) out.full_moons = almanac_days(almanac, "full_moon", {});
    // Computed whether or not anyone keeps them as vigils: a faith that dreads hollow
    // nights still needs to know which nights they are.
    out.hollow_nights = almanac_days(almanac, "hollow_night", {});

    // THE PATRON ALONE decides. A people whose second or third god is the Wild Umbral
    // still keeps hollow nights as dread. docs/pantheon.md in the reference describes
    // this as a property of the cult rather than of the patron; the code is the patron,
    // and the code wins.
    bool vigil = false;
    if (!chosen.empty()) {
        const auto it = by_id.find(chosen.front());
        vigil = it != by_id.end() && it->second->aspect == "wild" &&
                std::find(std::begin(kVigilGodIds), std::end(kVigilGodIds), chosen.front()) !=
                    std::end(kVigilGodIds);
    }
    out.hollow_kept_as = vigil ? "vigil" : "dread";
    return out;
}

std::vector<GodRecord> hidden_records(const Religion* previous) {
    std::map<std::string, const GodRecord*> prior;
    if (previous != nullptr) {
        for (const GodRecord& record : previous->gods) {
            if (record.family == kOutsideFamily) prior[record.id] = &record;
        }
    }
    std::vector<GodRecord> out;
    for (const HiddenGod& god : hidden_pantheon_gods()) {
        const auto it = prior.find(std::string(god.id));
        const GodRecord* was = it != prior.end() ? it->second : nullptr;
        GodRecord record;
        record.id = std::string(god.id);
        record.name = std::string(god.name);
        record.family = std::string(kOutsideFamily);
        // Pure carry-forward: nothing here ever promotes one of these. A hidden god's
        // existence rule is evaluated by the corruption API, not by this stage.
        record.status = was != nullptr ? was->status : "absent";
        record.aspect = was != nullptr ? was->aspect : "";
        record.aspect_name = was != nullptr ? was->aspect_name : "";
        if (was != nullptr) record.evidence = was->evidence;
        for (const std::string_view domain : god.domains) record.domains.emplace_back(domain);
        // `school` singular becomes a one-element `schools` list -- the whole reason a
        // nomad blood cult can find its patron in an exported world.
        record.schools.emplace_back(god.school);
        record.rule = god.exists;
        // ALWAYS empty: no cosmology folds an Outside god into a saint.
        record.saint_of.clear();
        out.push_back(std::move(record));
    }
    return out;
}

std::vector<ReligionSite> resolve_sites(const ReligionRequest& request,
                                        const std::vector<GodRecord>& gods) {
    std::map<std::string, const GodRecord*> by_id;
    for (const GodRecord& god : gods) by_id[god.id] = &god;

    const auto status_of = [&](std::string_view id) -> std::string_view {
        const auto it = by_id.find(std::string(id));
        return it != by_id.end() ? std::string_view(it->second->status) : std::string_view();
    };

    std::vector<ReligionSite> sites;
    for (const ReligionRuin& ruin : request.ruins) {
        // A SLEEPING Weave still gets its cults: the god withdrew, the cult did not.
        const bool cult = ruin.cause == "self_magic" && status_of(kSelfMagicCultGodId) != "absent";
        if (cult) {
            std::optional<double> tide;
            for (const ReligionAge& age : request.ages) {
                if (age.age == ruin.destroyed_age) {
                    tide = age.weave_tide;
                    break;
                }
            }
            ReligionSite site;
            site.id = "cult-" + ruin.id;
            site.kind = "cult";
            site.god_id = std::string(kSelfMagicCultGodId);
            site.ruin_id = ruin.id;
            site.node = ruin.node;
            site.born_under_surge = tide.has_value() && *tide >= kSurgeBirth;
            site.weave_tide = tide;
            sites.push_back(std::move(site));
        } else if (starts_with(ruin.cause, "war_")) {
            // No status test at all on this branch, deliberately: the Red Field's rule is
            // `always`, so it cannot be absent, and a shrine on a battlefield is a fact
            // about the ruin rather than about the god.
            ReligionSite site;
            site.id = "shrine-" + ruin.id;
            site.kind = "shrine";
            site.god_id = std::string(kWarShrineGodId);
            site.ruin_id = ruin.id;
            site.node = ruin.node;
            sites.push_back(std::move(site));
        }
    }

    // Cults and shrines are rebuilt from the ruin list every time, so removing a ruin
    // removes its site. Only what a visitation wrote survives.
    if (request.previous != nullptr) {
        for (const ReligionSite& site : request.previous->sites) {
            if (site.kind == "theophany" || site.kind == "pilgrimage") sites.push_back(site);
        }
    }
    std::sort(sites.begin(), sites.end(),
              [](const ReligionSite& a, const ReligionSite& b) { return a.id < b.id; });
    return sites;
}

std::vector<Faith> choose_faiths(const ReligionRequest& request, const WorldFacts& facts,
                                 const std::vector<GodRecord>& gods,
                                 const CosmologyChoice& cosmology,
                                 const std::map<std::string, Conversion>& conversions) {
    const PantheonTuning tuning = pantheon_tuning();

    std::map<std::string, const GodRecord*> by_id;
    for (const GodRecord& god : gods) by_id[god.id] = &god;

    // CATALOGUE ORDER, and that matters twice over: it is the order the weighted list is
    // built in, and therefore the order the draw's linear walk visits. The catalogue
    // breaks every tie in every faith.
    std::vector<const GodRecord*> manifest;
    for (const GodRecord& god : gods) {
        if (lives(god.status)) manifest.push_back(&god);
    }
    const GodIdSet high(cosmology.high.begin(), cosmology.high.end());

    std::vector<Faith> faiths;
    for (const std::string& civilization : facts.civilizations_with_cities) {
        std::vector<const ReligionCity*> cities;
        for (const ReligionCity& city : request.cities) {
            if (city.population_profile == civilization) cities.push_back(&city);
        }
        const EntityRules* rules = entity_rule(civilization);
        // `civilizations_with_cities` is deliberately unfiltered, exactly as the reference
        // leaves it, so an unknown profile reaches here rather than being dropped where
        // nobody would notice. Refused loudly instead: a people with cities and no faith
        // is a bug in whatever sited them.
        if (rules == nullptr) {
            throw std::invalid_argument(civilization +
                                        " has standing cities but is not a civilization in the "
                                        "registry, so it has no parent race to take a faith from");
        }
        const std::string race(rules->parent_race_id);
        const std::array<double, kKnownSchools> potency = mean_potency(request, cities);
        const AffinityTable* table = pantheon_affinity(civilization);

        // This people's own ancestor god, if it is live. It is taken directly rather than
        // drawn, which is why a people with an ancestor consumes no draw for its patron.
        const GodRecord* ancestor = nullptr;
        for (const GodRecord* god : manifest) {
            const PantheonGod* entry = pantheon_god(god->id);
            if (god->family == "ancestor" && entry != nullptr && entry->race == race) {
                ancestor = god;
                break;
            }
        }

        Rng rng = Rng::child(request.seed, "faith-v1-" + civilization);

        std::vector<std::pair<std::string, double>> weighted;
        for (const GodRecord* god : manifest) {
            if (ancestor != nullptr && god->id == ancestor->id) continue;
            double weight = 0.0;
            if (table != nullptr) {
                // The three bonuses ADD; they do not select. A god in both `strong` and
                // `aversion` would score zero and silently leave the pool.
                if (contains(table->strong, god->id)) weight += tuning.affinity.strong;
                if (contains(table->mild, god->id)) weight += tuning.affinity.mild;
                if (contains(table->aversion, god->id)) weight += tuning.affinity.aversion;
            }
            if (high.find(god->id) != high.end()) weight += tuning.affinity.high;
            if (god->family == "civic") weight += tuning.affinity.civic_base;
            for (const std::string& school : god->schools) {
                const int index = magic_school_index(school);
                // Only the known eight have a potency grid. `moon` is not a school at all
                // and a hidden school has no field a city could stand in, so both add
                // exactly nothing -- the moon god scores on affinity and rank alone.
                if (index >= 0 && index < kKnownSchools) {
                    weight += potency[static_cast<std::size_t>(index)];
                }
            }
            if (weight > 0.0) weighted.emplace_back(god->id, weight);
        }

        std::optional<Conversion> conversion;
        const auto converted = conversions.find(civilization);
        if (converted != conversions.end()) conversion = converted->second;
        // A conversion whose god has stopped existing is still exported, but it is no
        // longer anyone's patron: the reference says "until it stops existing", and a
        // sleeping god has stopped.
        bool conversion_live = false;
        if (conversion) {
            const auto it = by_id.find(conversion->god_id);
            conversion_live = it != by_id.end() && lives(it->second->status);
        }

        std::string patron;
        if (conversion_live) {
            // A god that walked among this people is its patron until it stops existing.
            if (ancestor != nullptr && ancestor->id != conversion->god_id) {
                // The displaced ancestor rejoins the pool at `strong`, so a converted
                // people still tends to keep its own dead near the top of its faith.
                weighted.emplace_back(ancestor->id, tuning.affinity.strong);
            }
            patron = conversion->god_id;
        } else if (ancestor != nullptr) {
            patron = ancestor->id;
        } else if (!weighted.empty()) {
            // DEPARTURE FROM THE REFERENCE, deliberate: the reference draws a patron here
            // even when a conversion is about to overrule it, then throws the value away,
            // which shifts every later pick for this people. The dead draw is removed, so
            // a converted people's companion gods differ from the reference's. Recorded
            // rather than reproduced.
            patron = weighted[draw_from(rng, weighted)].first;
        }

        std::vector<std::string> chosen;
        if (!patron.empty()) chosen.push_back(patron);
        std::vector<std::pair<std::string, double>> pool;
        for (const auto& entry : weighted) {
            if (entry.first != patron) pool.push_back(entry);
        }
        while (!pool.empty() && chosen.size() < 4) {
            const std::size_t pick = draw_from(rng, pool);
            chosen.push_back(pool[pick].first);
            pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(pick));
        }

        std::vector<std::pair<std::string, std::string>> names;
        for (const std::string& god_id : chosen) {
            const PantheonGod* entry = pantheon_god(god_id);
            const GodRecord* record = by_id.at(god_id);
            // An epithet, else the aspect name, else the plain name. An EMPTY epithet
            // falls through, which is what the reference's `or` chain does.
            std::string name(entry != nullptr ? epithet_for(*entry, civilization)
                                              : std::string_view());
            if (name.empty()) name = record->aspect_name;
            if (name.empty() && entry != nullptr) name = std::string(entry->name);
            names.emplace_back(god_id, std::move(name));
        }

        std::vector<FearRecord> fear;
        for (const ReligionCity* city : cities) {
            for (const NestThreat& threat : nest_threats(*city, request.nests, request.globe_radius_m)) {
                // FAMILY FIRST: a nest named "...dragon..." whose family is undead makes a
                // city fear the Umbral, not the Wyrm.
                std::string_view god_id = fear_god_for(threat.family);
                if (god_id.empty() && threat.kind == "dragon") {
                    god_id = fear_god_for(kDraconicFearFamily);
                }
                if (god_id.empty()) continue;
                const auto it = by_id.find(std::string(god_id));
                if (it == by_id.end() || !lives(it->second->status)) continue;
                const bool already = std::any_of(fear.begin(), fear.end(), [&](const FearRecord& f) {
                    return f.god_id == god_id && f.city_uid == city->uid;
                });
                // First nest per (god, city) wins, and `nest_threats` walks nests in id
                // order, so the retained nest is the lowest id -- lexicographically, not
                // numerically.
                if (already) continue;
                fear.push_back({std::string(god_id), city->uid, threat.nest_id, "fear"});
            }
        }
        std::sort(fear.begin(), fear.end(), [](const FearRecord& a, const FearRecord& b) {
            return a.city_uid != b.city_uid ? a.city_uid < b.city_uid : a.god_id < b.god_id;
        });

        std::vector<SectRecord> sects;
        if (!patron.empty()) {
            const PantheonGod* entry = pantheon_god(patron);
            std::string rival;
            if (entry != nullptr) {
                for (const std::string_view candidate : entry->rivals) {
                    const auto it = by_id.find(std::string(candidate));
                    if (it != by_id.end() && lives(it->second->status)) {
                        rival = std::string(candidate);
                        break;
                    }
                }
            }
            // The fallback the narrative doc omits: a patron with no LIVE rival takes the
            // people's second god instead. god_first_hearth has no rivals at all, so this
            // is the branch a human schism actually runs.
            if (rival.empty() && chosen.size() > 1) rival = chosen[1];
            if (!rival.empty()) {
                // A dict DEFAULT, not an `or` chain: an empty display name for the rival
                // is kept rather than replaced. Nothing ships an empty one, but the two
                // spellings differ and the other three name lookups here use the chain.
                std::string epithet;
                const auto named = std::find_if(
                    names.begin(), names.end(),
                    [&](const std::pair<std::string, std::string>& n) { return n.first == rival; });
                if (named != names.end()) {
                    epithet = named->second;
                } else {
                    const auto it = by_id.find(rival);
                    if (it != by_id.end()) epithet = it->second->name;
                }
                for (const ReligionFoundingEvent& event : request.founding_events) {
                    if (event.status != "diaspora_founded") continue;
                    if (event.diaspora_reason != "religious_schism") continue;
                    if (event.population_profile != civilization) continue;
                    // Inverted on purpose: the sect's patron is the orthodoxy's rival and
                    // its rival is the orthodoxy's patron.
                    sects.push_back({event.node, event.founding_year, rival, patron,
                                     epithet + " of the Schism"});
                }
            }
        }

        Faith faith;
        faith.civilization = civilization;
        faith.parent_race_id = race;
        faith.patron = patron;
        faith.gods = chosen;
        faith.names = std::move(names);
        faith.conversion = conversion;
        faith.fear = std::move(fear);
        faith.sects = std::move(sects);
        faith.feasts = feasts_for(faith.gods, by_id, request.almanac);
        // Built from the weighted LIST, so it carries the appended ancestor too. The
        // ancestor is skipped by the scoring loop, so the append cannot duplicate a key.
        faith.weights = std::move(weighted);
        faiths.push_back(std::move(faith));
    }
    return faiths;
}

}  // namespace

// ---------------------------------------------------------------------------

const PantheonGod* pantheon_god(std::string_view id) {
    for (const PantheonGod& god : pantheon_gods()) {
        if (god.id == id) return &god;
    }
    return nullptr;
}

const HiddenGod* hidden_pantheon_god(std::string_view id) {
    for (const HiddenGod& god : hidden_pantheon_gods()) {
        if (god.id == id) return &god;
    }
    return nullptr;
}

const Cosmology* pantheon_cosmology(std::string_view id) {
    for (const Cosmology& cosmology : pantheon_cosmologies()) {
        if (cosmology.id == id) return &cosmology;
    }
    return nullptr;
}

const AffinityTable* pantheon_affinity(std::string_view civilization) {
    for (const AffinityTable& table : pantheon_affinities()) {
        if (table.civilization == civilization) return &table;
    }
    return nullptr;
}

std::string_view epithet_for(const PantheonGod& god, std::string_view civilization) {
    for (const Epithet& epithet : god.epithets) {
        if (epithet.civilization == civilization) return epithet.name;
    }
    return {};
}

void lint_pantheon() {
    const auto refuse = [](const std::string& why) { throw std::runtime_error("pantheon: " + why); };

    GodIdSet known;
    for (const PantheonGod& god : pantheon_gods()) {
        if (!known.insert(std::string(god.id)).second) refuse("duplicate god id " + std::string(god.id));
    }

    GodIdSet school_gods;
    for (const PantheonGod& god : pantheon_gods()) {
        if (god.aspects.sovereign.empty()) {
            refuse(std::string(god.id) + " has no sovereign aspect, which is the name every "
                                         "faith falls back to");
        }
        if (god.school_affinity.empty()) refuse(std::string(god.id) + " has no school affinity");
        for (const std::string_view school : god.school_affinity) {
            const int index = magic_school_index(school);
            if ((index < 0 || index >= kKnownSchools) && school != kMoonPseudoSchool) {
                refuse(std::string(god.id) + " has affinity for " + std::string(school) +
                       ", which is neither a known school nor the pseudo-school 'moon'");
            }
        }
        for (const std::string_view other : god.allies) {
            if (known.find(other) == known.end()) refuse(std::string(god.id) + " allies an unknown god");
        }
        for (const std::string_view other : god.rivals) {
            if (known.find(other) == known.end()) refuse(std::string(god.id) + " rivals an unknown god");
        }
        if (!god.folds_into.empty() && known.find(god.folds_into) == known.end()) {
            refuse(std::string(god.id) + " folds into an unknown god");
        }
        if (god.family == "school") school_gods.insert(std::string(existence_school(god)));
    }
    // Exactly once each, forever. A school with no god would raise nobody where the world
    // has a whole network, and two gods on one school would both answer for it.
    if (school_gods.size() != static_cast<std::size_t>(kKnownSchools)) {
        refuse("the school gods must cover every known school exactly once");
    }

    for (const Cosmology& cosmology : pantheon_cosmologies()) {
        for (const std::string_view god_id : cosmology.high) {
            if (known.find(god_id) == known.end()) {
                refuse(std::string(cosmology.id) + " names an unknown high god");
            }
        }
    }
    if (pantheon_cosmology(kFallbackCosmologyId) == nullptr) {
        refuse("the fallback cosmology " + std::string(kFallbackCosmologyId) + " is gone, so a "
               "world where nothing is eligible would have no cosmology at all");
    }
    for (const std::string_view id : fold_free_cosmologies()) {
        if (pantheon_cosmology(id) == nullptr) {
            refuse("fold_free_cosmologies names unknown cosmology " + std::string(id));
        }
    }

    for (const std::string_view civilization : civilization_ids()) {
        if (pantheon_affinity(civilization) == nullptr) {
            refuse(std::string(civilization) + " has no affinity table");
        }
    }
    if (pantheon_affinities().size() != civilization_ids().size()) {
        refuse("affinities must cover every civilization exactly");
    }
    for (const AffinityTable& table : pantheon_affinities()) {
        for (const auto& list : {table.strong, table.mild, table.aversion}) {
            for (const std::string_view god_id : list) {
                if (known.find(god_id) == known.end()) {
                    refuse(std::string(table.civilization) + " names an unknown god");
                }
            }
        }
    }

    for (const FearGod& row : fear_gods()) {
        if (known.find(row.god_id) == known.end()) refuse("fear_gods names an unknown god");
    }
    for (const WildPush& row : wild_push_families()) {
        const PantheonGod* god = pantheon_god(row.god_id);
        if (god == nullptr) refuse("wild_push_families names an unknown god");
        if (god != nullptr && god->aspects.wild.empty()) {
            refuse(std::string(row.god_id) + " is pushed toward a wild aspect it does not have");
        }
    }

    GodIdSet hidden;
    for (const HiddenGod& god : hidden_pantheon_gods()) {
        if (!hidden.insert(std::string(god.id)).second) refuse("duplicate hidden god id");
        if (known.find(god.id) != known.end()) refuse("a god is in both halves of the catalogue");
        const int index = magic_school_index(god.school);
        if (index < kKnownSchools) {
            refuse(std::string(god.id) + " must name a hidden school; the four Outside gods are "
                                         "how a corrupted world names its magic");
        }
        if (god.aspects.wild.empty()) {
            refuse(std::string(god.id) + " needs a wild aspect: a revelation pushes it there");
        }
    }
}

// ---------------------------------------------------------------------------

WorldFacts world_facts(const ReligionRequest& request) {
    WorldFacts facts;
    const std::span<const MagicSchool> schools = magic_school_table();
    for (std::size_t i = 0; i < static_cast<std::size_t>(kKnownSchools); ++i) {
        bool present = false;
        if (request.magic != nullptr) {
            const auto it = request.magic->networks.find(std::string(schools[i].name));
            // KEY POINTS, not the key being there: a network of edges and no nodes raises
            // no god, which is why aspect_of can meet an empty item list at all.
            present = it != request.magic->networks.end() && !it->second.nodes.empty();
        }
        facts.schools[i] = present;
    }
    for (const int primordial : kPrimordialSchools) {
        if (facts.schools[static_cast<std::size_t>(primordial)]) ++facts.primordial_count;
    }

    // Everyone who ever founded, not merely who still stands: a people whose last city
    // fell still has an ancestor god.
    std::set<std::string> ever(request.used_civilizations.begin(), request.used_civilizations.end());
    for (const ReligionCity& city : request.cities) ever.insert(city.population_profile);
    for (const ReligionRuin& ruin : request.ruins) {
        if (!ruin.population_profile.empty() && entity_rule(ruin.population_profile) != nullptr) {
            ever.insert(ruin.population_profile);
        }
    }
    std::set<std::string> races;
    for (const std::string& civilization : ever) {
        const EntityRules* rules = entity_rule(civilization);
        if (rules != nullptr) races.insert(std::string(rules->parent_race_id));
    }
    facts.races.assign(races.begin(), races.end());

    std::set<std::string> with_cities;
    for (const ReligionCity& city : request.cities) with_cities.insert(city.population_profile);
    facts.civilizations_with_cities.assign(with_cities.begin(), with_cities.end());

    const double total = request.ocean_km2 + request.lake_km2 + request.dry_km2;
    facts.ocean_share = total > 0.0 ? request.ocean_km2 / total : 0.0;
    facts.sky = request.sky_settlements;

    std::set<std::string> families;
    for (const ReligionNest& nest : request.nests) {
        if (nest.real) continue;  // real animals frighten nobody into a temple
        if (!nest.family.empty()) families.insert(nest.family);
        if (nest.family == "draconic" || lowercase(nest.name).find("dragon") != std::string::npos) {
            ++facts.dragons;
        }
    }
    facts.families.assign(families.begin(), families.end());

    facts.ruins = static_cast<int>(request.ruins.size());
    for (const ReligionCity& city : request.cities) {
        if (city.diaspora) ++facts.diaspora;
    }
    for (const ReligionFoundingEvent& event : request.founding_events) {
        if (event.status == "diaspora_founded") ++facts.diaspora;
    }
    for (const ReligionAge& age : request.ages) facts.wars += age.wars;

    // A world with no magic block at all is magic-enabled only if some school has key
    // points, which with no block it cannot -- so the fallback is false and honest.
    facts.magic_enabled = request.magic != nullptr
                              ? request.magic->enabled
                              : std::any_of(facts.schools.begin(), facts.schools.end(),
                                            [](bool on) { return on; });
    facts.lunar_influence = request.lunar_influence;
    return facts;
}

bool rule_holds(std::span<const RuleNode> rule, const WorldFacts& facts, const GodIdSet& manifest) {
    if (rule.empty()) throw std::invalid_argument("an existence rule has no nodes");
    return rule_holds_at(rule, 0, facts, manifest);
}

std::pair<std::string, AspectEvidence> aspect_of(const PantheonGod& god,
                                                 const ReligionRequest& request,
                                                 const WorldFacts& facts) {
    AspectEvidence evidence;
    // Sixteen of the twenty-five have one face only and never choose.
    if (god.aspects.wild.empty()) return {"sovereign", evidence};

    const PantheonTuning tuning = pantheon_tuning();
    if (god.family == "school") {
        const LeyNetwork* network = nullptr;
        if (request.magic != nullptr) {
            const auto it = request.magic->networks.find(std::string(existence_school(god)));
            if (it != request.magic->networks.end()) network = &it->second;
        }
        double sum = 0.0;
        std::size_t count = 0;
        if (network != nullptr) {
            for (const LeyNode& node : network->nodes) sum += node.intensity;
            for (const LeyEdge& edge : network->edges) sum += edge.intensity;
            count = network->nodes.size() + network->edges.size();
        }
        const double mean = count > 0 ? sum / static_cast<double>(count) : 0.0;
        const double instability = network != nullptr ? network->instability : 0.0;
        double push = 0.0;
        for (const WildPush& row : wild_push_families()) {
            if (row.god_id != god.id) continue;
            for (const std::string_view family : row.families) {
                if (contains(facts.families, family)) push = tuning.nest_aspect_push;
            }
        }
        evidence.has_network = true;
        evidence.mean_intensity = mean;
        evidence.instability = instability;
        evidence.nest_push = push;
        evidence.midpoint = tuning.aspect_midpoint;
        // STRICTLY greater: a network sitting exactly on the midpoint stays Sovereign.
        const double score = mean * (1.0 + instability) + push;
        return {score > tuning.aspect_midpoint ? "wild" : "sovereign", evidence};
    }

    if (god.id == kMoonGodId) {
        double lean = 0.0;
        if (request.almanac != nullptr && !request.almanac->months.empty()) {
            double sum = 0.0;
            for (const AlmanacMonth& month : request.almanac->months) sum += month.state.nod_degrees;
            lean = sum / static_cast<double>(request.almanac->months.size());
        }
        evidence.has_lean = true;
        evidence.mean_nod_degrees = lean;
        evidence.leaning = lean >= 0.0 ? "still" : "restless";
        return {lean >= 0.0 ? "sovereign" : "wild", evidence};
    }

    return {"sovereign", evidence};
}

std::vector<GodRecord> resolve_gods(const ReligionRequest& request, const WorldFacts& facts) {
    std::map<std::string, const GodRecord*> before;
    if (request.previous != nullptr) {
        for (const GodRecord& god : request.previous->gods) before[god.id] = &god;
    }

    std::vector<GodRecord> gods;
    for (const PantheonGod& god : pantheon_gods()) {
        // No manifest set, so a `gods` rule can never hold here. Only cosmologies get one.
        const bool holds = rule_holds(god.exists, facts);
        const auto it = before.find(std::string(god.id));
        const GodRecord* prior = it != before.end() ? it->second : nullptr;

        std::string status;
        if (prior != nullptr && prior->status == "walking") {
            // Sticky. Nothing in this stage can end a walk; only depart_god or a cleanse.
            status = "walking";
        } else if (holds) {
            status = "manifest";
        } else if (prior != nullptr &&
                   (prior->status == "manifest" || prior->status == "sleeping" ||
                    prior->status == "walking")) {
            // A god that stops qualifying sleeps with its last aspect. It never returns to
            // absent: a world does not unremember a god it once had.
            status = "sleeping";
        } else {
            status = "absent";
        }

        GodRecord record;
        record.id = std::string(god.id);
        record.name = std::string(god.name);
        record.family = std::string(god.family);
        record.status = status;
        if (lives(status)) {
            auto [aspect, evidence] = aspect_of(god, request, facts);
            record.aspect = std::move(aspect);
            record.evidence = std::move(evidence);
        } else if (status == "sleeping") {
            record.aspect = prior->aspect;
            // ONE level. The reference wraps the whole of the previous evidence in a fresh
            // {'last_seen': ...} on every resolution, so a god asleep through many age
            // turns grows the export without bound. Kept flat, and the flag says the
            // measurement is historic.
            record.evidence = prior->evidence;
            record.evidence.last_seen = true;
        }
        if (!record.aspect.empty()) {
            record.aspect_name = std::string(record.aspect == "wild" ? god.aspects.wild
                                                                     : god.aspects.sovereign);
        }
        for (const std::string_view domain : god.domains) record.domains.emplace_back(domain);
        for (const std::string_view school : god.school_affinity) record.schools.emplace_back(school);
        record.rule = god.exists;
        if (prior != nullptr && prior->status == "walking" && prior->avatar) {
            record.avatar = prior->avatar;
        }
        gods.push_back(std::move(record));
    }
    return gods;
}

CosmologyChoice choose_cosmology(const WorldFacts& facts, const GodIdSet& manifest,
                                 std::uint32_t seed) {
    std::vector<const Cosmology*> eligible;
    std::vector<double> weights;
    for (const Cosmology& cosmology : pantheon_cosmologies()) {
        if (!rule_holds(cosmology.requirement, facts, manifest)) continue;
        const double weight = cosmology.weight.base +
                              cosmology.weight.per * fact_value(facts, cosmology.weight.fact);
        // Four templates have a zero base, so a world where their fact is zero drops them
        // even though the requirement holds.
        if (weight > 0.0) {
            eligible.push_back(&cosmology);
            weights.push_back(weight);
        }
    }
    if (eligible.empty()) {
        // Dead in practice -- cos_mosaic requires `always` and weighs 0.5 -- and kept
        // anyway, because a catalogue edit that took its `always` away would otherwise
        // leave the draw with nothing to walk.
        const Cosmology* fallback = pantheon_cosmology(kFallbackCosmologyId);
        if (fallback == nullptr) {
            throw std::runtime_error("no cosmology is eligible and the fallback " +
                                     std::string(kFallbackCosmologyId) + " is not in the catalogue");
        }
        eligible.push_back(fallback);
        weights.push_back(1.0);
    }

    Rng rng = Rng::child(seed, "pantheon-v1");
    const Cosmology* chosen = eligible[draw_index(rng, weights)];

    CosmologyChoice out;
    out.id = std::string(chosen->id);
    out.name = std::string(chosen->name);
    out.arrangement = std::string(chosen->arrangement);
    // Filtered to what actually exists, so a cosmology can be chosen and still export an
    // empty high list.
    for (const std::string_view god_id : chosen->high) {
        if (manifest.find(god_id) != manifest.end()) out.high.emplace_back(god_id);
    }
    for (std::size_t i = 0; i < eligible.size(); ++i) {
        out.eligible.emplace_back(std::string(eligible[i]->id), weights[i]);
    }
    return out;
}

std::array<double, kKnownSchools> mean_potency(const ReligionRequest& request,
                                               const std::vector<const ReligionCity*>& cities) {
    std::array<double, kKnownSchools> out{};
    const std::span<const MagicSchool> schools = magic_school_table();
    for (std::size_t s = 0; s < static_cast<std::size_t>(kKnownSchools); ++s) {
        const Grid* grid = nullptr;
        if (request.layers != nullptr) {
            const auto it = request.layers->find("ley_" + std::string(schools[s].name));
            if (it != request.layers->end() && !it->second.empty()) grid = &it->second;
        }
        if (grid == nullptr || cities.empty()) {
            out[s] = 0.0;
            continue;
        }
        double sum = 0.0;
        for (const ReligionCity* city : cities) {
            if (city->x < 0 || city->x >= grid->size() || city->z < 0 || city->z >= grid->size()) {
                throw std::out_of_range("city " + city->uid + " stands outside the ley grid");
            }
            sum += grid->at(city->x, city->z);
        }
        out[s] = sum / static_cast<double>(cities.size());
    }
    return out;
}

std::vector<NestThreat> nest_threats(const ReligionCity& city,
                                     const std::vector<ReligionNest>& nests, double radius_m) {
    // STRING order on the id, and it is load-bearing: the fear dedupe keeps the first
    // nest per (god, city), so this sort decides which nest id a faith records.
    std::vector<const ReligionNest*> ordered;
    ordered.reserve(nests.size());
    for (const ReligionNest& nest : nests) ordered.push_back(&nest);
    std::sort(ordered.begin(), ordered.end(),
              [](const ReligionNest* a, const ReligionNest* b) { return a->id < b->id; });

    std::vector<NestThreat> threats;
    for (const ReligionNest* nest : ordered) {
        if (nest->layer != "surface") continue;
        if (nest->real) continue;
        const bool dragon = lowercase(nest->name).find("dragon") != std::string::npos;
        const bool dangerous = dragon || nest->family == "infernal" || nest->family == "undead" ||
                               nest->family == "aberrant";
        if (!dangerous) continue;

        // Great circle. The clamp guards acos against floating-point overshoot on
        // coincident or antipodal directions, and makes the measure wrap-correct by
        // construction -- there is no seam to cross.
        const double cosine = clamp_unit(dot(city.direction, nest->direction));
        const double distance = radius_m * std::acos(cosine);
        // 350 m and radius/2 are ABSOLUTE metres authored against the reference world, not
        // fractions of this one. The doubling applies to the nest's own range, which is why
        // an absent range falls through to the spacing before it falls back to 350.
        const double span = nest->range_m ? *nest->range_m : (nest->spacing_m ? *nest->spacing_m : 350.0);
        const double reach = std::min(radius_m * 0.5, std::max(350.0, span * 2.0));
        if (distance > reach) continue;

        NestThreat threat;
        threat.kind = dragon ? "dragon" : "monster";
        // Scales with the danger tier, so a greater lair at the gates is the reason a city
        // falls and a tier one nuisance is not.
        threat.weight = 0.11 * static_cast<double>(nest->tier) * (1.0 - distance / reach);
        threat.reason = nest->name + " drove the inhabitants away.";
        threat.nest_id = nest->id;
        threat.name = nest->name;
        threat.family = nest->family;
        threat.distance_m = distance;
        threat.reach_m = reach;
        threats.push_back(std::move(threat));
    }
    return threats;
}

Religion resolve_religion(const ReligionRequest& request) {
    const WorldFacts facts = world_facts(request);
    std::vector<GodRecord> gods = resolve_gods(request, facts);

    // COMPUTED BEFORE the hidden records are appended, and that is the whole reason a
    // hidden god can never satisfy a `gods` rule, be a cosmology's high god or be
    // worshipped by anyone.
    GodIdSet manifest;
    for (const GodRecord& god : gods) {
        if (lives(god.status)) manifest.insert(god.id);
    }

    const CosmologyChoice cosmology = choose_cosmology(facts, manifest, request.seed);

    // AFTER the cosmology, because folding is cosmology-dependent: under the Mosaic or the
    // Ancestor Halls nobody is anybody's saint.
    bool fold_free = false;
    for (const std::string_view id : fold_free_cosmologies()) {
        if (cosmology.id == id) fold_free = true;
    }
    for (GodRecord& god : gods) {
        const PantheonGod* entry = pantheon_god(god.id);
        const std::string_view target = entry != nullptr ? entry->folds_into : std::string_view();
        const bool folds = !target.empty() && manifest.find(target) != manifest.end() &&
                           !fold_free && god.status != "absent";
        god.saint_of = folds ? std::string(target) : std::string();
    }

    // Read from the previous block BEFORE this run's faiths replace it.
    std::map<std::string, Conversion> conversions;
    if (request.previous != nullptr) {
        for (const Faith& faith : request.previous->faiths) {
            if (faith.conversion) conversions[faith.civilization] = *faith.conversion;
        }
    }

    Religion out;
    out.version = kReligionVersion;
    out.catalogue = pantheon_identity();
    out.facts = facts;
    out.cosmology = cosmology;
    out.faiths = choose_faiths(request, facts, gods, cosmology, conversions);
    out.sites = resolve_sites(request, gods);
    for (GodRecord& hidden : hidden_records(request.previous)) gods.push_back(std::move(hidden));
    out.gods = std::move(gods);
    if (request.previous != nullptr) {
        out.visitations = request.previous->visitations;
        // Carried forward like visitations: resolving religion rebuilds this block, and a
        // corruption record outliving its rebuild is the whole point of a persistent node.
        out.corruptions = request.previous->corruptions;
    }
    out.method =
        "Every god has an independent existence rule read from exported world state; a god that "
        "stops qualifying sleeps with its last aspect. One cosmology is drawn from the eligible "
        "templates by fit weight. Each civilization with a live city takes its ancestor god as "
        "patron and draws up to three more by affinity, cosmology rank and the leyline potency "
        "under its cities. Feasts follow the almanac.";
    out.limits =
        "Gods are narrative provenance only: no terrain, food, threat or influence effect until "
        "the orchestrator summons one through the visitation API. Names and epithets are authored "
        "English.";
    return out;
}

}  // namespace fwg
