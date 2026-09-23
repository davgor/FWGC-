// Stages 14 and 15: the ages a generation turns.
//
// These test the orchestration -- that every pass the age transition calls reads what the
// finished age left and writes what the next one inherits -- not the passes themselves,
// which have their own suites (wars, fates, villains, visitation).
#include "check.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>

#include "fwg/entities.hpp"
#include "fwg/heroes.hpp"
#include "fwg/time.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

World at_phase(int phase, int size = 33, std::uint32_t seed = 42) {
    Request r;
    r.seed = seed;
    r.size = size;
    r.phase = phase;
    return generate(resolve(r));
}

const SettlementSite* city(const World& w, const std::string& uid) {
    for (const SettlementSite& s : w.settlements.sites) {
        if (s.uid == uid) return &s;
    }
    return nullptr;
}

}  // namespace

TEST(a_full_ladder_turns_two_ages_and_records_each) {
    const World w = at_phase(15);
    check::equal(w.age, 2, "the world to be in its second age");
    check::equal(w.history.ages.size(), std::size_t{2}, "one record per age");
    std::size_t events = 0;
    for (std::size_t i = 0; i < w.history.ages.size(); ++i) {
        const AgeRecord& a = w.history.ages[i];
        check::equal(a.age, static_cast<int>(i) + 1, "ages numbered from one");
        check::is_true(a.moon.has_value(), "the moon read on the day the age turned");
        for (const Ruin& ruin : a.events) {
            check::equal(ruin.destroyed_age, a.age, ruin.id + " to fall in the age that records it");
            check::equal(w.ruins.at(events).id, ruin.id, "the world's ruins to be the ages' events, in order");
            ++events;
        }
    }
    check::equal(w.ruins.size(), events, "no ruin the ages did not record");
    check::is_true(events > 0, "some city to fall in two ages, or nothing is tested");
}

TEST(a_ruin_is_the_city_that_fell_there) {
    const World w = at_phase(15);
    std::set<std::string> ids;
    for (const Ruin& ruin : w.ruins) {
        check::equal(ruin.id, "ruin-" + ruin.uid, "a ruin keyed on the city it was");
        check::is_true(ids.insert(ruin.id).second, ruin.id + " to fall once");
        check::is_true(!ruin.cause.empty(), ruin.id + " to say what ended it");
        check::equal(ruin.civilization_id, ruin.population_profile, ruin.id + " to name its people");
        check::is_true(ruin.probability > 0.0 && ruin.roll < ruin.probability,
                       ruin.id + " to have lost the roll it fell to");
    }
}

TEST(the_survivors_keep_their_identity_and_the_rest_are_new) {
    const World w = at_phase(15);
    const AgeRecord& last = w.history.ages.back();
    std::set<std::string> standing;
    for (const SettlementSite& s : w.settlements.sites) standing.insert(s.uid);
    std::set<std::string> recorded(last.surviving_city_ids.begin(), last.surviving_city_ids.end());
    for (const std::string& uid : last.new_city_ids) {
        check::is_true(recorded.insert(uid).second, uid + " to be new or a survivor, not both");
    }
    check::is_true(recorded == standing, "survivors plus the new cities to be every city that stands");
    check::is_true(!last.surviving_city_ids.empty() && !last.new_city_ids.empty(),
                   "both kinds of city after an age, or the carry is untested");
    for (const std::string& uid : last.new_city_ids) {
        check::equal(city(w, uid)->founded_age, 2, uid + " founded in the age it appeared");
        check::is_true(city(w, uid)->name.find("(Age 2)") != std::string::npos, uid + " to say its age");
    }
}

TEST(nobody_rebuilds_on_a_ruin) {
    const World w = at_phase(15);
    std::set<int> ruined;
    for (const Ruin& ruin : w.ruins) ruined.insert(ruin.node);
    for (const SettlementSite& s : w.settlements.sites) {
        check::is_true(ruined.count(s.node) == 0, s.uid + " not to stand on a ruin");
    }
}

TEST(both_sides_of_a_war_carry_it) {
    const World w = at_phase(15);
    std::map<std::string, const std::vector<WarParticipation>*> histories;
    for (const SettlementSite& s : w.settlements.sites) histories[s.uid] = &s.war_history;
    for (const Ruin& r : w.ruins) histories[r.uid] = &r.war_history;
    std::size_t wars = 0;
    for (const AgeRecord& a : w.history.ages) {
        for (const WarRecord& war : a.wars) {
            ++wars;
            for (const std::string& uid : war.participants) {
                const auto it = histories.find(uid);
                // A participant that fell in a later age to something else is a ruin, and a
                // city standing now is a city: either way it is somewhere with its history.
                check::is_true(it != histories.end(), uid + " to be a city or a ruin");
                const bool carried = std::any_of(it->second->begin(), it->second->end(),
                                                 [&](const WarParticipation& p) { return p.war_id == war.id; });
                check::is_true(carried, uid + " to carry " + war.id);
            }
        }
    }
    check::is_true(wars > 0, "some war in two ages, or nothing is tested");
}

TEST(every_ruin_scars_the_ley_field_with_its_key_point) {
    const World w = at_phase(15);
    for (const Ruin& ruin : w.ruins) {
        check::is_true(!ruin.new_node_school.empty(), ruin.id + " to name the school it scars");
        const LeyNetwork& net = w.magic.networks.at(ruin.new_node_school);
        const auto key = std::find_if(net.nodes.begin(), net.nodes.end(),
                                      [&](const LeyNode& n) { return n.id == ruin.id + "-key"; });
        check::is_true(key != net.nodes.end(), ruin.id + "-key to stand in the " + ruin.new_node_school + " network");
    }
}

TEST(the_ley_field_drifts_inside_its_band_across_one_age) {
    // Stage 14 against stage 13 of the same seed: every node and line that stood before the
    // turn moved by a factor inside [0.65, 1.35], or hit the network ceiling.
    const World before = at_phase(13);
    const World after = at_phase(14);
    std::size_t compared = 0;
    std::size_t moved = 0;
    for (const auto& [name, net] : before.magic.networks) {
        const LeyNetwork& next = after.magic.networks.at(name);
        std::map<std::string, double> now;
        for (const LeyNode& n : next.nodes) now[n.id] = n.intensity;
        for (const LeyEdge& e : next.edges) now[e.id] = e.intensity;
        const auto band = [&](const std::string& id, double was) {
            const auto it = now.find(id);
            check::is_true(it != now.end(), id + " to survive the turn");
            ++compared;
            if (it->second != was) ++moved;
            if (it->second == kLeyIntensityMax) return;
            if (was == 0.0) {
                check::equal(it->second, 0.0, id + " to stay at zero: the drift multiplies");
                return;
            }
            // The factor is drawn in [low, high); dividing it back out may land an ulp outside.
            const double ratio = it->second / was;
            check::between(ratio, kLeyAgeLow - 1e-12, kLeyAgeHigh + 1e-12, id + " drift factor");
        };
        for (const LeyNode& n : net.nodes) band(n.id, n.intensity);
        for (const LeyEdge& e : net.edges) band(e.id, e.intensity);
    }
    check::is_true(compared > 0 && moved * 2 > compared, "the field to have drifted, not held still");
}

TEST(a_shorter_run_agrees_with_a_longer_one_at_the_first_age) {
    // `final` is stage 15 and never min(15, phase), so stage 14 of a longer run is the
    // same age as a phase-14 run turns.
    const World fourteen = at_phase(14);
    const World fifteen = at_phase(15);
    const AgeRecord& a = fourteen.history.ages.at(0);
    const AgeRecord& b = fifteen.history.ages.at(0);
    check::equal(a.events.size(), b.events.size(), "the same ruins in age one");
    for (std::size_t i = 0; i < a.events.size(); ++i) check::equal(a.events[i].id, b.events[i].id, "ruin order");
    check::equal(a.wars.size(), b.wars.size(), "the same wars in age one");
    for (std::size_t i = 0; i < a.wars.size(); ++i) check::equal(a.wars[i].id, b.wars[i].id, "war order");
    check::is_true(a.new_city_ids == b.new_city_ids, "the same new cities in age one");
}

TEST(villains_are_seated_at_the_last_age_and_not_before) {
    // Accumulation alone cannot seat anybody in two ages; the final age is where the world
    // names its antagonists, at exactly the bottom of the band.
    const World fourteen = at_phase(14, 65);
    for (const Villain& v : fourteen.villains.people) {
        check::is_true(v.tier < kSuperTier, v.uid + " not to be seated before the final age");
    }
    const World fifteen = at_phase(15, 65);
    check::is_true(fifteen.has_villains, "a villain block after the final age");
    std::size_t seated = 0;
    for (const Villain& v : fifteen.villains.people) {
        if (v.tier < kSuperTier) continue;
        ++seated;
        check::equal(v.tier, kSuperTier, v.uid + " promoted at exactly the band's floor");
        check::equal(v.born_age, 2, v.uid + " born in the age that seated it");
    }
    check::is_true(seated > 0, "some villain seated at the final age");
}

TEST(veterans_ask_for_a_road_fort_each_however_many_wars_they_fought) {
    const World w = at_phase(15);
    const FortressDemand& demand = w.humans.fortress_demand;
    std::map<std::string, const ThreatReport*> reports;
    for (const ThreatReport& r : w.threat_assessments.cities) reports[r.city_uid] = &r;
    std::set<int> on_road;
    for (const Road& road : w.roads.routes) {
        on_road.insert(road.from);
        on_road.insert(road.to);
    }
    int veterans = 0;
    int asked = 0;
    for (const SettlementSite& s : w.settlements.sites) {
        if (s.war_history.empty()) continue;
        ++veterans;
        // A capital asks anyway; war adds a fort only to a city that would not otherwise ask.
        if (on_road.count(s.id) != 0 && s.city_class != CityClass::Capital) ++asked;
        // And they expect the next war, which is what thickens their walls.
        const auto it = reports.find(s.uid);
        check::is_true(it != reports.end(), s.uid + " to be assessed");
        check::equal(it->second->wars_fought, static_cast<int>(s.war_history.size()), s.uid + " wars fought");
        check::is_true(it->second->war_pressure > 0.0, s.uid + " to feel its wars");
        check::is_true(it->second->regional_threat >= it->second->war_pressure, s.uid + " threat covers its wars");
    }
    check::is_true(veterans > 0, "a city standing after two ages that fought in them, or nothing is tested");
    check::equal(demand.veteran_cities, veterans, "every veteran city counted");
    check::equal(demand.veteran_demand, asked, "each asks for one road fort, however many wars");
    std::map<int, int> road_forts;
    for (const RuralSite& f : w.humans.fortresses) {
        if (f.watch == "road") ++road_forts[f.core_id];
    }
    for (const auto& [city, held] : road_forts) {
        check::is_true(held <= 1, "one road fort to a city, however long its wars");
    }
}

TEST(the_rebuilt_world_is_assessed_as_of_the_last_age) {
    const World w = at_phase(15);
    check::equal(w.beast_nests.evaluated_age.value_or(-1), 2, "the nests placed on the rebuilt ground");
    check::equal(w.threat_assessments.evaluated_after, std::string("age"), "the threat re-assessed by the age");
    check::equal(w.threat_assessments.age.value_or(-1), 2, "as of the second age");
    check::equal(w.threat_assessments.cities.size(), w.settlements.sites.size(), "every standing city assessed");
}

TEST(religion_raises_a_cult_or_a_shrine_where_the_ages_left_one) {
    // A city that destroyed itself leaves a Weave cult, a city a war took a shrine to the Red
    // Field. Both keyed on the ruin, and neither anywhere else.
    const World w = at_phase(15);
    std::set<std::string> sites;
    for (const ReligionSite& s : w.religion.sites) sites.insert(s.id);
    std::size_t expected = 0;
    for (const Ruin& ruin : w.ruins) {
        if (ruin.cause.rfind("war_", 0) == 0) {
            ++expected;
            check::is_true(sites.count("shrine-" + ruin.id) != 0, ruin.id + " to have its war shrine");
        }
    }
    check::is_true(expected > 0, "some war ruin, or the shrines are untested");
}

// --- advancing a finished world ----------------------------------------------

namespace {

const LeyNode* node_named(const World& w, const std::string& school, const std::string& id) {
    for (const LeyNode& n : w.magic.networks.at(school).nodes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

std::string refusal_field(const World& w, const AgeAdvanceRequest& request) {
    try {
        (void)advance_ages(w, request);
    } catch (const AgeAdvanceError& refused) {
        return refused.field();
    }
    return "<accepted>";
}

}  // namespace

TEST(an_advance_turns_the_ages_it_asks_for_and_records_the_operation) {
    const World genesis = at_phase(16);
    AgeAdvanceRequest request;
    request.steps = 2;
    const World later = advance_ages(genesis, request);
    check::equal(genesis.history.ages.size(), std::size_t{2}, "the caller's world untouched");
    check::equal(later.history.ages.size(), std::size_t{4}, "two more ages");
    check::equal(later.history.ages[2].age, 3, "numbered on from the last");
    check::equal(later.age, 4, "the world in its fourth age");
    check::equal(later.history.operations.size(), std::size_t{1}, "the advance recorded");
    const auto& advanced = std::get<AgeOperation>(later.history.operations[0]);
    check::equal(advanced.after_age, 2, "after the ages it inherited");
    check::equal(advanced.steps, 2, "with the steps it took");
    check::is_true(later.config.seed == genesis.config.seed, "genesis config kept");
    // Stage 16 is redrawn over the world the ages left: one plan per standing city.
    check::equal(later.city_plans.cities.size(), later.settlements.sites.size(), "plans for the cities that stand");
    for (const CityPlan& plan : later.city_plans.cities) {
        check::is_true(city(later, plan.city_uid) != nullptr, plan.city_uid + " to be a standing city");
    }
}

TEST(heroes_survive_an_age_that_ends_a_whole_people) {
    // Seed 42 at size 17: the third age leaves ruins of peoples with no city standing. Their
    // heroes still resolve their own race; a fallback race refuses the heritage merge and
    // the whole package with it.
    const World w = at_phase(16, 17);
    AgeAdvanceRequest advance;
    advance.steps = 1;
    const World next = advance_ages(w, advance);
    check::equal(next.heroes.status, std::string("ok"), "the heroes package after the age: " + next.heroes.error);
    std::set<std::string> standing;
    for (const SettlementSite& s : next.settlements.sites) standing.insert(s.population_profile);
    int fallen = 0;
    for (const Hero& h : next.heroes.people) {
        const EntityRules* rules = entity_rule(h.civilization_id);
        check::is_true(rules != nullptr, h.uid + " to name a registry civilization");
        check::equal(h.race_id, std::string(rules->parent_race_id), h.uid + " to carry its people's race");
        if (standing.count(h.civilization_id) == 0) ++fallen;
    }
    check::is_true(fallen > 0, "a hero of a people with no city standing, or the case is not exercised");
}

TEST(a_city_the_lottery_gives_a_hidden_school_stands_held) {
    // Ruled 2026-09-23. Blood made to dominate every city's ground, then one age turned: every
    // city the lottery gives to blood stands, marked held by it in that age, and no ruin is
    // blood's. The fates read the field before the rebuild re-evaluates it, so the edit bites.
    World w = at_phase(16);
    Grid& blood = w.layers.at("ley_blood");
    for (double& v : blood.cells()) v = 10.0;
    const int age = w.age + 1;
    const std::size_t ruins_before = w.ruins.size();
    age_transition(w, age);
    std::size_t fallen = 0;
    for (const SettlementSite& s : w.settlements.sites) {
        if (!s.corruption || s.corruption->cause != "fallen") continue;
        ++fallen;
        check::equal(s.corruption->school, std::string("blood"), s.uid + " held by blood");
        check::equal(s.corruption->age, age, s.uid + " in the age it fell");
        check::is_true(!s.corruption->corruption.has_value(), s.uid + " by no corruption record");
    }
    for (std::size_t i = ruins_before; i < w.ruins.size(); ++i) {
        check::is_true(w.ruins[i].cause != "blood", w.ruins[i].id + " not a blood ruin");
    }
    check::is_true(fallen > 0, "some city given to blood, or the path is untested");
}

TEST(an_advance_is_a_function_of_its_request) {
    const World genesis = at_phase(15);
    AgeAdvanceRequest request;
    request.moon_day = 12345;
    const World a = advance_ages(genesis, request);
    const World b = advance_ages(genesis, request);
    check::equal(a.ruins.size(), b.ruins.size(), "the same ruins");
    for (std::size_t i = 0; i < a.ruins.size(); ++i) check::equal(a.ruins[i].id, b.ruins[i].id, "ruin order");
    check::equal(a.settlements.sites.size(), b.settlements.sites.size(), "the same cities");
    for (std::size_t i = 0; i < a.settlements.sites.size(); ++i) {
        check::equal(a.settlements.sites[i].uid, b.settlements.sites[i].uid, "city order");
    }
    check::equal(a.history.ages.back().moon->day, 12345LL, "turned on the day asked for");
}

TEST(a_refused_advance_names_the_field_it_refused) {
    const World genesis = at_phase(15);
    const auto with = [](auto change) {
        AgeAdvanceRequest r;
        change(r);
        return r;
    };
    check::equal(refusal_field(genesis, with([](AgeAdvanceRequest& r) { r.steps = 0; })), std::string("steps"), "no steps");
    check::equal(refusal_field(genesis, with([](AgeAdvanceRequest& r) { r.steps = 11; })), std::string("steps"), "too many");
    check::equal(refusal_field(genesis, with([](AgeAdvanceRequest& r) { r.moon_day = -1; })), std::string("moon_day"),
                 "a day before time");
    check::equal(refusal_field(genesis, with([](AgeAdvanceRequest& r) { r.api_version = 2; })),
                 std::string("api_version"), "a future API");
    check::equal(refusal_field(at_phase(12), AgeAdvanceRequest{}), std::string("config"), "a world before creatures");

    const auto edit = [&](LeylineEdit e) {
        AgeAdvanceRequest r;
        r.leyline_edits.push_back(std::move(e));
        return refusal_field(genesis, r);
    };
    const std::string fire_node = genesis.magic.networks.at("fire").nodes.at(0).id;
    check::equal(edit({"thaumaturgy", fire_node, std::nullopt, 1.0, std::nullopt}), std::string("leyline_edits[].school"),
                 "an unknown school");
    check::equal(edit({"blood", fire_node, std::nullopt, 1.0, std::nullopt}), std::string("leyline_edits[].school"),
                 "a hidden school players cannot see");
    check::equal(edit({"fire", std::string("no-such-node"), std::nullopt, 1.0, std::nullopt}),
                 std::string("leyline_edits[]"), "a node that does not exist");
    check::equal(edit({"fire", fire_node, std::nullopt, 5.0, std::nullopt}), std::string("leyline_edits[].intensity"),
                 "an intensity past the ceiling");
    check::equal(edit({"fire", std::nullopt, std::nullopt, std::nullopt, std::nullopt}), std::string("leyline_edits[]"),
                 "no target at all");
    const LeyNode twin{fire_node, genesis.magic.networks.at("fire").nodes.at(0).direction, 1.0};
    check::equal(edit({"fire", std::nullopt, std::nullopt, std::nullopt, twin}), std::string("leyline_edits[].new_node"),
                 "a new node reusing an id");
    const LeyNode fresh{"player-well", genesis.magic.networks.at("fire").nodes.at(0).direction, 1.0};
    check::equal(edit({"fire", std::nullopt, std::nullopt, 1.0, fresh}), std::string("leyline_edits[].new_node"),
                 "an outer intensity beside a new node");
}

TEST(a_players_ley_edits_land_before_the_first_age_turns) {
    const World genesis = at_phase(15);
    const LeyNode& quenched = genesis.magic.networks.at("fire").nodes.at(0);
    AgeAdvanceRequest request;
    request.leyline_edits.push_back({"fire", quenched.id, std::nullopt, 0.0, std::nullopt});
    request.leyline_edits.push_back(
        {"water", std::nullopt, std::nullopt, std::nullopt, LeyNode{"player-spring", quenched.direction, 2.0}});
    const World later = advance_ages(genesis, request);
    // The drift multiplies, so a node set to zero before the turn is zero after it, and a
    // new key point drifts from where the player put it.
    check::equal(node_named(later, "fire", quenched.id)->intensity, 0.0, "the quenched node stays quenched");
    const LeyNode* spring = node_named(later, "water", "player-spring");
    check::is_true(spring != nullptr, "the player's key point to stand after the age");
    check::between(spring->intensity, 2.0 * kLeyAgeLow - 1e-12, 2.0 * kLeyAgeHigh + 1e-12, "and to have drifted once");
    check::equal(std::get<AgeOperation>(later.history.operations.at(0)).leyline_edits.size(), std::size_t{2},
                 "the edits recorded");
}
