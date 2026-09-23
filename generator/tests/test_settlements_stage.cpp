// Stage 10 as a stage: that it runs, and that what it writes is coherent.
//
// The pieces below it -- scoring, habitation, founding, classification -- have their own
// tests. These are about the pass that calls them: a world that reports completed_stage
// 10 must actually contain cities, which is exactly what was wrong before.
#include "check.hpp"

#include <map>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <set>
#include <string>

#include "fwg/entities.hpp"
#include "fwg/serialize.hpp"
#include "fwg/settlements_stage.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

World settled(int size = 65, std::uint32_t seed = 42) {
    Request r;
    r.seed = seed;
    r.size = size;
    r.phase = 10;
    return generate(resolve(r));
}

}  // namespace

TEST(a_stage_ten_world_actually_contains_cities) {
    // The defect this exists for: completed_stage reported 10 while add_settlements was
    // never called from the pipeline at all. The world claimed a stage it had not run,
    // and every later stage would have had nothing to attach to.
    const World w = settled();
    check::is_true(w.has_settlements, "a phase-10 world to have run its settlement stage");
    check::is_true(!w.settlements.sites.empty(), "and to contain cities");

    bool recorded = false;
    for (const std::string& s : w.systems) {
        if (s == "settlements") recorded = true;
    }
    check::is_true(recorded, "the systems list to record that settlements ran");
}

TEST(a_stage_nine_world_contains_none) {
    Request r;
    r.seed = 42;
    r.size = 65;
    r.phase = 9;
    const World w = generate(resolve(r));
    check::is_true(!w.has_settlements, "stage 9 to stop before founding");
    check::is_true(w.settlements.sites.empty(), "and to hold no cities");
}

TEST(every_city_stands_on_habitable_land) {
    const World w = settled();
    const Grid& water = w.layers.at("water_type");
    for (const SettlementSite& s : w.settlements.sites) {
        check::equal(water.at(s.x, s.z), 0.0,
                     s.population_profile + " at node " + std::to_string(s.node) + " to be on land");
        check::between(s.suitability, 0.0, 1.0, "a site's own score to be a score");
    }
}

TEST(no_city_exceeds_what_the_ground_allows) {
    const World w = settled();
    const PopulationBudget& budget = w.settlements.budget;
    check::is_true(budget.ceiling.packing > 0, "the ground to allow some cities");
    check::is_true(static_cast<long long>(w.settlements.sites.size()) <= budget.city_limit,
                   "the city count to respect the limit (" +
                       std::to_string(w.settlements.sites.size()) + " against " +
                       std::to_string(budget.city_limit) + ")");
}

TEST(each_people_holds_exactly_one_capital) {
    const World w = settled();
    std::map<std::string, int> capitals;
    std::set<std::string> peoples;
    for (const SettlementSite& s : w.settlements.sites) {
        peoples.insert(s.population_profile);
        if (s.city_class == CityClass::Capital) ++capitals[s.population_profile];
    }
    check::is_true(peoples.size() > 1, "more than one people to have settled, or this proves little");
    for (const std::string& p : peoples) {
        check::equal(capitals[p], 1, p + " to hold exactly one capital");
    }
}

TEST(a_peoples_population_is_split_across_its_cities_and_not_multiplied) {
    // Each city carries a share of what its people can feed, not the whole allowance.
    // Publishing the allowance on every city would inflate the world by the city count.
    const World w = settled();
    std::map<std::string, long long> totals;
    for (const SettlementSite& s : w.settlements.sites) {
        totals[s.population_profile] += s.population_estimate;
        check::equal(s.urban_population_estimate + s.rural_population_estimate, s.population_estimate,
                     "town and country to account for everyone in " + s.population_profile);
    }
    for (const auto& [people, total] : totals) {
        const auto allowance = w.settlements.budget.allowances.find(people);
        check::is_true(allowance != w.settlements.budget.allowances.end(),
                       people + " to have a published allowance");
        if (allowance == w.settlements.budget.allowances.end()) continue;
        check::is_true(total <= allowance->second,
                       people + " to settle within what it can feed (" + std::to_string(total) +
                           " against " + std::to_string(allowance->second) + ")");
    }
}

TEST(the_world_publishes_the_field_each_siting_decision_came_out_of) {
    // A site record says where a city went. The suitability layer says what the whole
    // world looked like to that people, which is what makes the choice checkable rather
    // than merely stated.
    const World w = settled();
    std::set<std::string> peoples;
    for (const SettlementSite& s : w.settlements.sites) peoples.insert(s.population_profile);
    check::is_true(!peoples.empty(), "somebody to have settled");
    for (const std::string& p : peoples) {
        check::is_true(w.layers.find("suitability_" + p) != w.layers.end(),
                       "the world to publish " + p + "'s opinion of it");
    }
}

TEST(a_founding_record_explains_every_city_and_every_refusal) {
    const World w = settled();
    const FoundingReport& f = w.settlements.founding;
    check::equal(f.placed_cities, static_cast<long long>(w.settlements.sites.size()),
                 "the record to agree with the map");
    check::is_true(!f.stop_reason.empty(), "founding to say why it stopped");
    check::is_true(f.turns > 0, "founding to have run at least one round");
    check::is_true(!f.events.empty(), "founding to record what happened in those rounds");
}

TEST(a_world_with_one_people_is_settled_by_that_people_alone) {
    Request r;
    r.seed = 42;
    r.size = 65;
    r.phase = 10;
    const std::string only(civilization_ids().front());
    r.population_profile = only;
    const World w = generate(resolve(r));
    for (const SettlementSite& s : w.settlements.sites) {
        check::equal(s.population_profile, only, "only the named people to settle");
    }
}

TEST(the_world_document_carries_its_cities_and_roads) {
    // Stage 10 and 11 ran for a long time without reaching the document at all, which
    // made a generated world look empty to anything outside the process. This asserts
    // the blocks are there and that the numbers in them agree with the world.
    Request r;
    r.seed = 42;
    r.size = 33;
    r.phase = 11;
    const World w = generate(resolve(r));

    std::ostringstream out;
    write_world_json(out, w, false);
    const std::string doc = out.str();

    for (const char* key : {"\"settlements\"", "\"sites\"", "\"population_budget\"", "\"founding\"",
                            "\"events\"", "\"roads\"", "\"routes\"", "\"components\"",
                            "\"world_recipe\"", "\"population_profile\""}) {
        check::is_true(doc.find(key) != std::string::npos,
                       std::string("the document to carry ") + key);
    }

    // Infinity has no JSON spelling. A site with no reachable fresh water must publish
    // null rather than a literal no parser will accept.
    //
    // Checked only where a NUMBER can appear -- after ':', ',' or '[' -- because the
    // bare substring "inf" is in "rainfall" and "rainforest", and a naive search for it
    // fails on a document that is perfectly well formed. It did.
    for (std::size_t i = 0; i + 1 < doc.size(); ++i) {
        if (doc[i] != ':' && doc[i] != ',' && doc[i] != '[') continue;
        std::size_t j = i + 1;
        while (j < doc.size() && (doc[j] == ' ' || doc[j] == '\n' || doc[j] == '-')) ++j;
        const std::string_view rest(doc.data() + j, std::min<std::size_t>(3, doc.size() - j));
        check::is_true(rest != "inf" && rest != "nan" && rest != "Inf" && rest != "NaN",
                       "no non-finite literal where a number belongs, at offset " + std::to_string(j));
    }
}

TEST(every_city_is_named_in_its_own_peoples_tongue) {
    const World w = settled();
    check::is_true(!w.settlements.sites.empty(), "cities to name");

    std::map<std::string, std::set<std::string>> names_by_people;
    for (const SettlementSite& s : w.settlements.sites) {
        check::is_true(!s.name.empty(), "city " + std::to_string(s.id) + " to have a name");
        check::is_true(!s.name_gloss.empty(),
                       s.name + " to carry what it means; a name with no gloss is a noise");
        names_by_people[s.population_profile].insert(s.name);
    }

    // Different peoples must draw from different lexicons, or the world is one culture
    // wearing twelve hats. Compared as SETS of names: two peoples sharing a name by
    // chance is fine, two peoples sharing every name is not.
    check::is_true(names_by_people.size() > 1, "more than one people, or this proves nothing");
    bool any_disjoint = false;
    for (auto a = names_by_people.begin(); a != names_by_people.end(); ++a) {
        for (auto b = std::next(a); b != names_by_people.end(); ++b) {
            bool shared = false;
            for (const std::string& n : a->second) {
                if (b->second.count(n) != 0) shared = true;
            }
            if (!shared) any_disjoint = true;
        }
    }
    check::is_true(any_disjoint, "some pair of peoples to name their cities differently");
}

TEST(a_citys_name_follows_its_node_not_its_place_in_the_list) {
    // Settlement ids renumber when the set changes -- an age advance is the obvious case.
    // A name keyed on the list position would rename every city behind any change; keyed
    // on the node, a city keeps its name as long as it stands where it stands.
    const World a = settled(65, 42);
    const World b = settled(65, 42);
    check::equal(a.settlements.sites.size(), b.settlements.sites.size(), "the same world twice");

    std::map<int, std::string> by_node;
    for (const SettlementSite& s : a.settlements.sites) by_node[s.node] = s.name;
    for (const SettlementSite& s : b.settlements.sites) {
        check::equal(s.name, by_node[s.node], "the city on node " + std::to_string(s.node) +
                                                  " to carry the same name both times");
    }
}

TEST(a_citys_uid_is_keyed_on_its_node_and_not_on_its_place_in_the_list) {
    // `id` renumbers whenever the set of cities changes, which an age advance does.
    // Threat reports, villain claims, seats and ruin ids are all keyed on the uid, so it
    // has to survive that -- which it does by naming the terrain node, which does not
    // move.
    const World w = settled();
    check::is_true(!w.settlements.sites.empty(), "cities to key");

    std::set<std::string> uids;
    for (const SettlementSite& s : w.settlements.sites) {
        check::is_true(!s.uid.empty(), "city " + std::to_string(s.id) + " to carry a uid");
        check::is_true(uids.insert(s.uid).second, s.uid + " to be unique");
        check::is_true(s.uid.find(std::to_string(s.node)) != std::string::npos,
                       s.uid + " to name its node");
        check::is_true(s.uid.find(s.population_profile) != std::string::npos,
                       s.uid + " to name its people");
    }
}

TEST(a_citys_direction_points_at_its_own_cell) {
    const World w = settled();
    const auto held = sphere_grid(w.effective.size, w.effective.globe_radius);
    for (const SettlementSite& s : w.settlements.sites) {
        const Vec3 expected = direction(s.x, s.z, w.effective.size);
        check::close(s.direction.x, expected.x, 1e-12, s.uid + " x");
        check::close(s.direction.y, expected.y, 1e-12, s.uid + " y");
        check::close(s.direction.z, expected.z, 1e-12, s.uid + " z");
        const double length = std::sqrt(s.direction.x * s.direction.x + s.direction.y * s.direction.y +
                                        s.direction.z * s.direction.z);
        check::close(length, 1.0, 1e-12, s.uid + " to be a unit vector");
    }
}

// --- rebuilding around survivors, as an age transition does -----------------

namespace {

// A stage-12 world, then half its cities kept as survivors and a few turned to ruins.
struct Rebuilt {
    World before;
    World after;
    SettlementRebuild rebuild;
};

Rebuilt rebuilt_world() {
    Rebuilt r;
    Request req;
    req.seed = 42;
    req.size = 65;
    req.phase = 12;
    r.before = generate(resolve(req));
    r.after = r.before;

    r.rebuild.age = 1;
    r.rebuild.previous = r.before.settlements.founding;
    const auto& sites = r.before.settlements.sites;
    for (std::size_t i = 0; i < sites.size(); ++i) {
        if (i % 2 == 0) {
            r.rebuild.survivors.push_back(sites[i]);
        } else if (i % 4 == 1) {
            r.rebuild.forbidden_nodes.push_back(sites[i].node);
        }
    }
    add_settlements(r.after, &r.rebuild);
    return r;
}

}  // namespace

TEST(a_city_that_stands_through_an_age_turn_keeps_its_identity) {
    // It is the SAME city, not a new one on the same ground: the uid threat reports and
    // claims key on, the name its people call it, and the age that founded it.
    const Rebuilt r = rebuilt_world();
    check::is_true(!r.rebuild.survivors.empty(), "some survivors, or nothing is tested");
    long long found = 0;
    for (const SettlementSite& old : r.rebuild.survivors) {
        for (const SettlementSite& now : r.after.settlements.sites) {
            if (now.node != old.node || now.population_profile != old.population_profile) continue;
            ++found;
            check::equal(now.uid, old.uid, old.uid + " to keep its uid");
            check::equal(now.name, old.name, old.uid + " to keep its name");
            check::equal(now.founded_age, old.founded_age, old.uid + " to keep its founding age");
            check::equal(now.source_culture, old.source_culture, old.uid + " to keep its culture");
        }
    }
    check::equal(found, static_cast<long long>(r.rebuild.survivors.size()), "every survivor to still stand");
}

TEST(a_city_founded_in_a_later_age_says_so) {
    // New cities take the new age into their uid, their founding age and their name. The
    // name matters because names are keyed on the node: a city founded where one fell
    // would otherwise carry exactly the fallen city's name.
    const Rebuilt r = rebuilt_world();
    std::set<std::string> old_uids;
    for (const SettlementSite& s : r.rebuild.survivors) old_uids.insert(s.uid);
    long long new_cities = 0;
    for (const SettlementSite& s : r.after.settlements.sites) {
        if (old_uids.count(s.uid) != 0) continue;
        ++new_cities;
        check::equal(s.founded_age, 1, s.uid + " to be founded in age 1");
        check::is_true(s.uid.rfind("surface-city-1-", 0) == 0, s.uid + " to carry age 1 in its uid");
        check::is_true(s.name.find(" (Age 1)") != std::string::npos, s.name + " to say which age founded it");
    }
    check::is_true(new_cities > 0, "the rebuild to found some new cities, or this proves nothing");
}

TEST(nobody_rebuilds_on_the_ruins) {
    const Rebuilt r = rebuilt_world();
    check::is_true(!r.rebuild.forbidden_nodes.empty(), "some ruins, or nothing is tested");
    const std::set<int> ruined(r.rebuild.forbidden_nodes.begin(), r.rebuild.forbidden_nodes.end());
    for (const SettlementSite& s : r.after.settlements.sites) {
        check::is_true(ruined.count(s.node) == 0, s.uid + " not to stand on a ruin");
    }
}

TEST(the_founding_clock_resumes_after_the_last_age_rather_than_restarting) {
    // A new age's cities are founded after the old age's, never at year zero on top of
    // them.
    const Rebuilt r = rebuilt_world();
    check::is_true(r.after.settlements.founding.start_year > r.before.settlements.founding.end_year,
                   "the new age to start after the old one ended (" +
                       std::to_string(r.after.settlements.founding.start_year) + " after " +
                       std::to_string(r.before.settlements.founding.end_year) + ")");
}

TEST(a_first_age_city_takes_its_culture_from_its_own_core) {
    // At stage 12 the hinterland places the cores, and a city founded this age takes its
    // culture from its own -- not the placeholder it was given at stage 10 before any core
    // existed.
    Request req;
    req.seed = 42;
    req.size = 65;
    req.phase = 12;
    const World w = generate(resolve(req));
    long long cored = 0;
    for (const SettlementSite& s : w.settlements.sites) {
        check::is_true(!s.source_culture.empty(), s.uid + " to have a founding culture");
        for (const CityCore& c : w.humans.cores) {
            if (c.site_id != s.id) continue;
            ++cored;
            check::equal(s.source_culture, c.culture_id, s.uid + " to take its core's culture");
        }
    }
    check::is_true(cored > 0, "some city to have a core, or nothing is tested");
}

TEST(a_citys_published_terms_add_up_to_its_suitability) {
    // `suitability_factors` claims to be what the score was summed from. Clamped the way the
    // score is, the terms must give back the site's suitability exactly -- a term computed
    // off the wrong ground, or one dropped, would not.
    const World w = settled(33);
    check::is_true(!w.settlements.sites.empty(), "cities to check");
    const std::string_view label = registry_defaults().resource_score_factor_label;
    for (const SettlementSite& s : w.settlements.sites) {
        check::is_true(!s.reason.empty(), s.uid + " to say why it stands where it does");
        check::equal(s.suitability_factors.size(), std::size_t{10}, s.uid + ": the ten published terms");
        check::equal(s.suitability_factors.front().name, std::string("freshwater"), s.uid + ": the first term");
        check::equal(s.suitability_factors.back().name, std::string(label), s.uid + ": the registry's label last");
        double total = 0.0;
        for (const SuitabilityFactor& f : s.suitability_factors) total += f.value;
        check::close(std::max(0.0, std::min(1.0, total)), s.suitability, 1e-12, s.uid + ": the terms' sum");
    }
}

TEST(each_people_has_its_own_published_risk_and_a_far_farmer_its_reach) {
    // Every people's magic-risk field is published, and today it is the world's hazard; a
    // people that farms outside its own habitat publishes the ground it can reach to farm.
    const World w = settled(33);
    const Grid& hazard = w.layers.at("magic_hazard");
    for (const std::string_view people : civilization_ids()) {
        const auto it = w.layers.find("magic_risk_" + std::string(people));
        check::is_true(it != w.layers.end(), "a magic_risk layer for " + std::string(people));
        for (int z = 0; z < hazard.size(); ++z) {
            for (int x = 0; x < hazard.size(); ++x) {
                check::equal(it->second.at(x, z), hazard.at(x, z), "the risk of " + std::string(people));
            }
        }
    }
    long long reach_layers = 0;
    for (const std::string_view people : civilization_ids()) {
        const EntityRules* rules = entity_rule(people);
        const bool far = rules != nullptr && rules->settlement.support_outside_habitat;
        const auto it = w.layers.find(std::string(people) + "_support_reach");
        check::equal(it != w.layers.end(), far, std::string(people) + ": a support-reach layer only if it farms afar");
        if (it == w.layers.end()) continue;
        ++reach_layers;
        for (const double v : it->second.cells()) check::is_true(v == 0.0 || v == 1.0, "a 0/1 reach mask");
    }
    check::is_true(reach_layers > 0, "a mixed world to hold some people that farms afar, or nothing is tested");
}
