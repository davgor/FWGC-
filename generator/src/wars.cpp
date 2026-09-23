#include "fwg/wars.hpp"

#include <algorithm>
#include <set>
#include <utility>

#include "fwg/magic.hpp"
#include "fwg/random.hpp"

namespace fwg {
namespace {

std::string war_reason(std::string_view kind) {
    if (kind == kWarCivil) {
        return "Two cities of one civilization fell out over contested ground and supply, and only "
               "one survived the war.";
    }
    if (kind == kWarRegional) {
        return "Kindred civilizations of one parent race fought a regional war over contested ground "
               "and supply.";
    }
    return "Cities of different parent races fought an international war over contested ground and "
           "supply.";
}

// By `site_id`. A later duplicate replaces an earlier one, as the reference's dict does.
std::map<int, const CityCore*> cores_by_site(const std::vector<CityCore>* cores) {
    std::map<int, const CityCore*> out;
    if (cores == nullptr) return out;
    for (const CityCore& core : *cores) out[core.site_id] = &core;
    return out;
}

const CityCore* core_of(const std::map<int, const CityCore*>& by_site, int id) {
    const auto it = by_site.find(id);
    return it != by_site.end() ? it->second : nullptr;
}

// Directed as the roads list them; the pressure test looks both ways.
std::set<std::pair<int, int>> linked_pairs(const std::vector<Road>* routes) {
    std::set<std::pair<int, int>> out;
    if (routes == nullptr) return out;
    for (const Road& road : *routes) out.insert({road.from, road.to});
    return out;
}

// How hard two road-linked cities lean on the same food. Only cities a road already joins
// can share a harvest, so an unlinked pair has nothing to divide however hungry either is.
// Between linked cities the hungrier one sets the pressure: it is the one that needs the
// other's ground.
double supply_pressure(const WarCity& a, const WarCity& b,
                       const std::map<int, const CityCore*>& by_site,
                       const std::set<std::pair<int, int>>& linked) {
    if (linked.count({a.id, b.id}) == 0 && linked.count({b.id, a.id}) == 0) return 0.0;
    return std::max(war_shortfall(core_of(by_site, a.id)), war_shortfall(core_of(by_site, b.id)));
}

// The school a fallen city leaves behind where its ground was already charged: the same
// dominance test the ley threat uses, so a war seeds a key point only where a clear school
// already held the ground.
std::string key_point_school(const WarCity& city, const WarRequest& request) {
    if (!request.magic_enabled) return {};
    std::vector<double> potencies;
    bool charged = false;
    for (const MagicSchool& school : magic_school_table()) {
        double value = 0.0;
        if (request.layers != nullptr) {
            // A hidden school's field may be absent from a hand-built layer set. Missing is
            // zero potency, not "this city has no school", which would drop the key point a
            // magically charged city is owed when it falls.
            const auto it = request.layers->find("ley_" + std::string(school.name));
            if (it != request.layers->end()) {
                const Grid& grid = it->second;
                // Off the grid is the reference's caught IndexError: no key point at all.
                if (city.x < 0 || city.z < 0 || city.x >= grid.size() || city.z >= grid.size()) {
                    return {};
                }
                value = grid.at(city.x, city.z);
            }
        }
        if (value != 0.0) charged = true;
        potencies.push_back(value);
    }
    // An all-zero field names no school however the test is tuned: with a zero threshold and
    // margin the scan would otherwise hand dead ground to the first school in the table.
    if (!charged) return {};
    const int winner = dominant_school(potencies, request.dominance_threshold, request.dominance_margin);
    if (winner < 0) return {};
    return std::string(magic_school_table()[static_cast<std::size_t>(winner)].name);
}

// A test seam's roll: the war's own id, else "*", else 1.0 -- which fights nothing.
double seam_roll(const std::map<std::string, double>& rolls, const std::string& war_id) {
    if (const auto it = rolls.find(war_id); it != rolls.end()) return it->second;
    if (const auto it = rolls.find("*"); it != rolls.end()) return it->second;
    return 1.0;
}

}  // namespace

std::string_view war_kind(const WarCity& a, const WarCity& b) {
    if (a.population_profile == b.population_profile) return kWarCivil;
    if (a.parent_race_id == b.parent_race_id) return kWarRegional;
    return kWarInternational;
}

double war_shortfall(const CityCore* core) {
    if (core == nullptr) return 0.0;
    const double demand = core->food_demand;
    if (demand <= 0.0) return 0.0;
    return std::max(0.0, std::min(1.0, (demand - core->food_supply) / demand));
}

double war_chance(double pressure) {
    return std::min(kWarMaxChance,
                    kWarBaseChance + kWarPressureGain * std::max(0.0, std::min(1.0, pressure)));
}

long long war_strength(const WarCity& city, const CityCore* core) {
    const long long forts = core != nullptr ? static_cast<long long>(core->fortress_ids.size()) : 0;
    return city.population_estimate + kWarFortStrength * forts;
}

double war_villain_pressure(const WarCity& a, const WarCity& b, const std::vector<Villain>& villains,
                            double radius_m) {
    double worst = 0.0;
    for (const Villain& villain : villains) {
        const double reach = villain.reach_m;
        if (reach <= 0.0) continue;
        // The FARTHER of the two decides: a villain presses a pair together only where its
        // shadow covers both, and it presses hardest on a pair gathered under its seat.
        const double farther = std::max(great_circle_m(radius_m, a.direction, villain.direction),
                                        great_circle_m(radius_m, b.direction, villain.direction));
        if (farther > reach) continue;
        worst = std::max(worst, kWarVillainContention * (1.0 - farther / reach));
    }
    return worst;
}

std::vector<WarPair> contested_pairs(const WarGround& ground) {
    const auto by_site = cores_by_site(ground.cores);
    const auto linked = linked_pairs(ground.routes);
    const double limit = kWarProximityFactor * ground.settlement_spacing_m;
    const std::vector<WarCity>& cities = ground.cities;

    std::vector<WarPair> pairs;
    for (std::size_t i = 0; i < cities.size(); ++i) {
        for (std::size_t j = i + 1; j < cities.size(); ++j) {
            const WarCity& a = cities[i];
            const WarCity& b = cities[j];
            const double distance = great_circle_m(ground.globe_radius_m, a.direction, b.direction);
            const double proximity = limit > 0.0 ? std::max(0.0, 1.0 - distance / limit) : 0.0;
            const double supply = supply_pressure(a, b, by_site, linked);
            if (proximity <= 0.0 && supply <= 0.0) continue;
            // A villain whose reach covers both presses them together: neighbours under one
            // shadow quarrel over what is left, which is how its presence reaches the
            // forecast and, through it, how thick the walls are built.
            const double villain = war_villain_pressure(a, b, ground.villains, ground.globe_radius_m);

            WarPair pair;
            pair.a = static_cast<int>(i);
            pair.b = static_cast<int>(j);
            pair.a_uid = a.uid;
            pair.b_uid = b.uid;
            pair.distance_m = distance;
            pair.proximity_pressure = proximity;
            pair.supply_pressure = supply;
            pair.villain_pressure = villain;
            pair.pressure = std::min(1.0, proximity + supply + villain);
            pair.kind = std::string(war_kind(a, b));
            pairs.push_back(std::move(pair));
        }
    }
    // Strongest first, and it decides who is still standing to fight the later pairs.
    std::stable_sort(pairs.begin(), pairs.end(), [](const WarPair& x, const WarPair& y) {
        if (x.pressure != y.pressure) return x.pressure > y.pressure;
        if (x.a_uid != y.a_uid) return x.a_uid < y.a_uid;
        return x.b_uid < y.b_uid;
    });
    return pairs;
}

std::map<std::string, WarOutlook> war_outlook(const WarGround& ground, std::optional<int> age) {
    std::set<std::string> living;
    for (const WarCity& city : ground.cities) living.insert(city.uid);
    const auto by_site = cores_by_site(ground.cores);

    std::map<std::string, WarOutlook> out;
    for (const WarCity& city : ground.cities) {
        WarOutlook row;
        for (const WarParticipation& entry : city.war_history) {
            if (age.has_value() && entry.age == *age) ++row.wars_recent;
            if (living.count(entry.opponent_uid) > 0) row.enemy_living = true;
        }
        out[city.uid] = row;
    }

    for (const WarPair& pair : contested_pairs(ground)) {
        const WarCity& a = ground.cities[static_cast<std::size_t>(pair.a)];
        const WarCity& b = ground.cities[static_cast<std::size_t>(pair.b)];
        // A pair pressed by supply has a direction: the hungrier city needs the other's
        // ground, so it carries the hunger and the other carries the risk. Pressed by
        // proximity alone it has none, and both carry the risk.
        const WarCity* hungry = nullptr;
        if (pair.supply_pressure > pair.proximity_pressure) {
            const double need_a = war_shortfall(core_of(by_site, a.id));
            const double need_b = war_shortfall(core_of(by_site, b.id));
            if (need_a != need_b) hungry = need_a > need_b ? &a : &b;
        }
        const std::pair<const WarCity*, const WarCity*> sides[2] = {{&a, &b}, {&b, &a}};
        for (const auto& [city, other] : sides) {
            WarOutlook& row = out[city->uid];
            // The UNROUNDED pressure is compared with the ROUNDED stored value, faithfully:
            // pairs arrive strongest first, but a later, weaker pair whose pressure lies
            // inside the rounding of the first one replaces its neighbour. Kept because a fix
            // renames the opponent a forecast names in exactly those worlds.
            if (hungry == city) {
                if (pair.pressure > row.war_hunger) {
                    row.war_hunger = round_dp(pair.pressure, 6);
                    row.war_hunger_target_uid = other->uid;
                }
            } else if (pair.pressure > row.war_risk) {
                row.war_risk = round_dp(pair.pressure, 6);
                row.war_risk_kind = pair.kind;
                row.war_risk_opponent_uid = other->uid;
            }
        }
    }
    return out;
}

WarResolution resolve_wars(const WarRequest& request) {
    const WarGround& ground = request.ground;
    const auto by_site = cores_by_site(ground.cores);

    WarResolution out;
    std::set<std::string> spared;
    for (const WarPair& pair : contested_pairs(ground)) {
        const WarCity& a = ground.cities[static_cast<std::size_t>(pair.a)];
        const WarCity& b = ground.cities[static_cast<std::size_t>(pair.b)];
        // Lost once, and a vassal cannot be fought again this age. Skipped WITHOUT a draw.
        if (out.fates.count(a.uid) > 0 || out.fates.count(b.uid) > 0 || spared.count(a.uid) > 0 ||
            spared.count(b.uid) > 0) {
            continue;
        }
        const std::string war_id = "war-" + std::to_string(request.age) + "-" + std::to_string(out.wars.size());
        const double chance = war_chance(pair.pressure);
        const double draw = request.rolls.has_value()
                                ? seam_roll(*request.rolls, war_id)
                                : Rng::child(request.seed, "war-" + a.uid + "-" + b.uid, request.age).random();
        if (draw >= chance) continue;

        const CityCore* a_core = core_of(by_site, a.id);
        const CityCore* b_core = core_of(by_site, b.id);
        const long long a_strength = war_strength(a, a_core);
        const long long b_strength = war_strength(b, b_core);
        // Strength decides it. A genuine tie goes to the lower uid, which is stable; a
        // city's suitability is not carried this far and must not become the tie-break.
        const bool a_wins = a_strength != b_strength ? a_strength > b_strength : !(b.uid < a.uid);
        const WarCity& victor = a_wins ? a : b;
        const WarCity& defeated = a_wins ? b : a;

        WarRecord war;
        war.id = war_id;
        war.age = request.age;
        war.kind = pair.kind;
        war.participants = {a.uid, b.uid};
        war.victor_uid = victor.uid;
        war.defeated_uid = defeated.uid;
        war.victor_civilization_id = victor.population_profile;
        war.defeated_civilization_id = defeated.population_profile;
        war.victor_parent_race_id = victor.parent_race_id;
        war.defeated_parent_race_id = defeated.parent_race_id;
        war.distance_m = pair.distance_m;
        war.proximity_pressure = pair.proximity_pressure;
        war.supply_pressure = pair.supply_pressure;
        war.pressure = pair.pressure;
        war.victor_strength = a_wins ? a_strength : b_strength;
        war.defeated_strength = a_wins ? b_strength : a_strength;
        war.chance = chance;
        war.roll = draw;
        war.reason = war_reason(pair.kind);

        if (request.survival > 0.0) {
            // A SECOND stream, so turning the option on cannot move which pairs fight.
            const double second =
                request.survival_rolls.has_value()
                    ? seam_roll(*request.survival_rolls, war_id)
                    : Rng::child(request.seed, "war-survival-" + a.uid + "-" + b.uid, request.age).random();
            const bool vassal = second < request.survival;
            war.outcome = vassal ? "vassalage" : "ruin";
            war.survival_chance = request.survival;
            war.survival_roll = second;
            out.wars.push_back(war);
            // The loser lives on as a vassal: a standing enemy in the victor's history and a
            // claim left open in its own.
            if (vassal) {
                spared.insert(defeated.uid);
                continue;
            }
        } else {
            out.wars.push_back(war);
        }

        WarFate fate;
        fate.cause = "war_" + pair.kind;
        fate.reason = war.reason;
        fate.war_id = war_id;
        fate.kind = pair.kind;
        fate.opponent_uid = victor.uid;
        fate.opponent_civilization_id = victor.population_profile;
        fate.opponent_parent_race_id = victor.parent_race_id;
        fate.distance_m = pair.distance_m;
        fate.pressure = pair.pressure;
        fate.probability = chance;
        fate.roll = draw;
        fate.new_node_school = key_point_school(defeated, request);
        out.fates[defeated.uid] = std::move(fate);
    }
    return out;
}

WarParticipation war_participation(const WarRecord& war, std::string_view uid) {
    const bool victor = war.victor_uid == uid;
    WarParticipation line;
    line.war_id = war.id;
    line.age = war.age;
    line.kind = war.kind;
    line.outcome = victor ? "victor" : "defeated";
    line.opponent_uid = victor ? war.defeated_uid : war.victor_uid;
    line.opponent_civilization_id = victor ? war.defeated_civilization_id : war.victor_civilization_id;
    line.opponent_parent_race_id = victor ? war.defeated_parent_race_id : war.victor_parent_race_id;
    return line;
}

int veteran_wars(const WarCity& city) { return static_cast<int>(city.war_history.size()); }

}  // namespace fwg
