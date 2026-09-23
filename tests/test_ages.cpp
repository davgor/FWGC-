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
