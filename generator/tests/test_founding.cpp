// Founding invariants.
//
// These run against a synthetic world -- nodes evenly spaced along a line -- because the
// properties under test are about rounds, quotas, spacing and ceilings, not about
// terrain. A real world would make every one of them depend on the generator upstream.
#include "check.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <set>

#include "fwg/civilizations.hpp"
#include "fwg/founding.hpp"

using namespace fwg;

namespace {

constexpr int kNodes = 64;
constexpr double kStep = 1000.0;  // metres between adjacent nodes

std::function<double(int, int)> line_distance() {
    return [](int a, int b) { return std::abs(a - b) * kStep; };
}

// Two parents, one civilization each, every node eligible, suitability rising with the
// node index so the best ground is always at the far end.
FoundingRequest two_parents() {
    FoundingRequest r;
    r.parents = {{"alder", "Alder", 100.0}, {"birch", "Birch", 100.0}};
    r.owners = {{"alder_folk", "alder"}, {"birch_folk", "birch"}};
    r.quotas = {{"alder_folk", 3}, {"birch_folk", 3}};

    std::vector<int> nodes;
    std::vector<double> scores;
    for (int i = 0; i < kNodes; ++i) {
        nodes.push_back(i);
        scores.push_back(static_cast<double>(i) / kNodes);
    }
    r.candidates = {{"alder_folk", nodes}, {"birch_folk", nodes}};
    r.scores = {{"alder_folk", scores}, {"birch_folk", scores}};
    r.spacing = 2000.0;
    r.world_reach = (kNodes - 1) * kStep;
    return r;
}

long long count_of(const std::vector<FoundedSite>& sites, const std::string& profile) {
    long long n = 0;
    for (const FoundedSite& s : sites) {
        if (s.population_profile == profile) ++n;
    }
    return n;
}

}  // namespace

// --- spacing and separation -----------------------------------------------

TEST(no_two_cities_stand_closer_than_the_spacing) {
    const FoundingResult r = found_cities(42, two_parents(), line_distance());
    check::is_true(r.sites.size() >= 2, "at least two cities to have been founded");
    for (std::size_t a = 0; a < r.sites.size(); ++a) {
        for (std::size_t b = a + 1; b < r.sites.size(); ++b) {
            const double d = std::abs(r.sites[a].node - r.sites[b].node) * kStep;
            check::is_true(d >= 2000.0,
                           "cities " + std::to_string(r.sites[a].node) + " and " +
                               std::to_string(r.sites[b].node) + " to stand the spacing apart");
        }
    }
}

TEST(two_peoples_do_not_begin_in_the_same_valley) {
    // A parent's first city must stand ninety degrees from every other parent's first
    // city, or every people starts on the same good ground and the map has one region.
    const FoundingRequest request = two_parents();
    const FoundingResult r = found_cities(42, request, line_distance());

    std::vector<int> capitals;
    for (const FoundedSite& s : r.sites) {
        if (s.founding_capital) capitals.push_back(s.node);
    }
    check::equal(capitals.size(), std::size_t{2}, "one capital per parent");
    const double gap = request.world_reach * 90.0 / 180.0;
    const double actual = std::abs(capitals[0] - capitals[1]) * kStep;
    check::is_true(actual + 1e-8 >= gap,
                   "the capitals to stand " + std::to_string(gap) + " m apart, got " +
                       std::to_string(actual));
}

TEST(a_parent_with_nowhere_separated_to_start_is_recorded_not_retried) {
    // Three parents on a line cannot all stand ninety degrees apart. The third must be
    // told why it never founded, rather than silently failing every round.
    FoundingRequest request = two_parents();
    request.parents.push_back({"cedar", "Cedar", 100.0});
    request.owners["cedar_folk"] = "cedar";
    request.quotas["cedar_folk"] = 3;
    request.candidates["cedar_folk"] = request.candidates["alder_folk"];
    request.scores["cedar_folk"] = request.scores["alder_folk"];

    const FoundingResult r = found_cities(42, request, line_distance());
    check::equal(count_of(r.sites, "cedar_folk"), 0LL, "the third parent to found nothing");

    long long blocked_events = 0;
    for (const FoundingEvent& e : r.report.events) {
        if (e.status == "no_separated_origin" && e.parent_race_id == "cedar") ++blocked_events;
    }
    check::equal(blocked_events, 1LL, "the blocked parent to be recorded exactly once");
}

// --- the reach ------------------------------------------------------------

TEST(a_people_spreads_outward_rather_than_teleporting) {
    // The best ground is always at the far end of the line. If the reach did not bind,
    // a parent's second city would sit next to the global maximum instead of beside its
    // own capital.
    FoundingRequest request = two_parents();
    request.parents = {{"alder", "Alder", 100.0}};
    request.owners = {{"alder_folk", "alder"}};
    request.quotas = {{"alder_folk", 2}};
    request.candidates.erase("birch_folk");
    request.scores.erase("birch_folk");
    request.spacing = 2000.0;

    const FoundingResult r = found_cities(42, request, line_distance());
    check::equal(r.sites.size(), std::size_t{2}, "two cities");

    const double base = std::max({request.spacing * 2.0, request.world_reach / 32.0, 1.0});
    const double hop = std::abs(r.sites[0].node - r.sites[1].node) * kStep;
    check::is_true(hop <= base * 1.6,
                   "the second city to be within the second round's reach (" +
                       std::to_string(hop) + " m against " + std::to_string(base * 1.6) + ")");
}

// --- quotas and the ceiling -----------------------------------------------

TEST(food_caps_a_quota_below_what_the_ground_would_allow) {
    FoundingRequest request = two_parents();
    request.minimum_residents = {{"alder_folk", 100}, {"birch_folk", 100}};
    // Alder can feed one city, birch three.
    request.food_allowances = {{"alder_folk", 150}, {"birch_folk", 400}};

    const FoundingResult r = found_cities(42, request, line_distance());
    check::equal(r.report.effective_quotas.at("alder_folk"), 1LL, "alder to be cut to one city");
    check::equal(r.report.effective_quotas.at("birch_folk"), 3LL, "birch to keep its three");
    check::is_true(count_of(r.sites, "alder_folk") <= 1, "alder to found at most its quota");
}

TEST(a_caller_who_wants_no_cities_gets_none) {
    // The collision this guards: city_limit used to be zero-for-none. A caller asking
    // for an empty world zeroed every quota, and the diaspora bonus -- whose whole
    // purpose is to waive a zero quota -- then had no ceiling left to stop it and
    // settled the world anyway.
    FoundingRequest request = two_parents();
    request.quotas = {{"alder_folk", 0}, {"birch_folk", 0}};
    request.minimum_residents = {{"alder_folk", 10}, {"birch_folk", 10}};
    request.food_allowances = {{"alder_folk", 10000}, {"birch_folk", 10000}};
    request.city_limit = 0;

    const FoundingResult r = found_cities(42, request, line_distance());
    check::equal(r.sites.size(), std::size_t{0}, "an empty world to stay empty");
}

TEST(a_people_with_no_quota_still_reaches_the_map_when_there_is_no_ceiling) {
    // The other side of the same rule: with no ceiling at all, the diaspora bonus is
    // exactly what lets a people the quotas ignored exist somewhere.
    FoundingRequest request = two_parents();
    request.quotas = {{"alder_folk", 0}, {"birch_folk", 0}};
    request.minimum_residents = {{"alder_folk", 10}, {"birch_folk", 10}};
    request.food_allowances = {{"alder_folk", 10000}, {"birch_folk", 10000}};
    request.city_limit = std::nullopt;

    const FoundingResult r = found_cities(42, request, line_distance());
    check::is_true(!r.sites.empty(), "a zero-quota people to reach the map by diaspora");
    for (const FoundedSite& s : r.sites) {
        check::is_true(s.diaspora, "every city here to be a diaspora founding");
    }
}

TEST(the_diaspora_bonus_is_spent_once_per_parent) {
    FoundingRequest request = two_parents();
    // Two civilizations under one parent, neither with a quota.
    request.parents = {{"alder", "Alder", 100.0}};
    request.owners = {{"alder_folk", "alder"}, {"alder_kin", "alder"}};
    request.quotas = {{"alder_folk", 0}, {"alder_kin", 0}};
    request.candidates["alder_kin"] = request.candidates["alder_folk"];
    request.scores["alder_kin"] = request.scores["alder_folk"];
    request.candidates.erase("birch_folk");
    request.scores.erase("birch_folk");
    request.minimum_residents = {{"alder_folk", 10}, {"alder_kin", 10}};
    request.food_allowances = {{"alder_folk", 10000}, {"alder_kin", 10000}};

    const FoundingResult r = found_cities(42, request, line_distance());
    long long bonuses = 0;
    for (const FoundedSite& s : r.sites) {
        if (s.diaspora_bonus) ++bonuses;
    }
    check::equal(bonuses, 1LL, "one parent to spend exactly one bonus");
    check::equal(r.report.diaspora_bonus_used.size(), std::size_t{1}, "the parent to be recorded");
}

TEST(forbidden_ground_is_never_settled) {
    FoundingRequest request = two_parents();
    std::vector<int> ruins;
    for (int i = 32; i < kNodes; ++i) ruins.push_back(i);  // the whole good half
    request.forbidden = ruins;

    const FoundingResult r = found_cities(42, request, line_distance());
    for (const FoundedSite& s : r.sites) {
        check::is_true(s.node < 32, "no city on ruined ground (node " + std::to_string(s.node) + ")");
    }
}

// --- participation --------------------------------------------------------

TEST(a_parent_that_never_participates_still_founds_its_capital) {
    // The opening round skips the draw on purpose. A parent at zero percent that never
    // placed a first city would not exist at all, which is not what zero means.
    FoundingRequest request = two_parents();
    request.parents = {{"alder", "Alder", 0.0}, {"birch", "Birch", 100.0}};

    const FoundingResult r = found_cities(42, request, line_distance());
    check::equal(count_of(r.sites, "alder_folk"), 1LL,
                 "a never-participating parent to hold exactly its capital");
    check::is_true(count_of(r.sites, "birch_folk") > 1, "the other parent to expand");
}

TEST(a_declined_draw_is_recorded_with_its_number) {
    FoundingRequest request = two_parents();
    request.parents = {{"alder", "Alder", 1.0}, {"birch", "Birch", 1.0}};  // almost never participate

    const FoundingResult r = found_cities(42, request, line_distance());
    long long declined = 0;
    for (const FoundingEvent& e : r.report.events) {
        if (e.status != "did_not_participate") continue;
        ++declined;
        check::between(e.draw_percent, 1.0, 100.0, "a declined draw to be at or above the percent");
    }
    check::is_true(declined > 0, "a one-percent parent to decline at least once");
}

// --- order independence ---------------------------------------------------

TEST(the_same_seed_founds_the_same_cities) {
    const FoundingResult a = found_cities(42, two_parents(), line_distance());
    const FoundingResult b = found_cities(42, two_parents(), line_distance());
    check::equal(a.sites.size(), b.sites.size(), "the same number of cities");
    for (std::size_t i = 0; i < a.sites.size(); ++i) {
        check::equal(a.sites[i].node, b.sites[i].node, "city " + std::to_string(i) + " on the same node");
        check::equal(a.sites[i].population_profile, b.sites[i].population_profile,
                     "city " + std::to_string(i) + " settled by the same people");
    }
    check::equal(a.report.stop_reason, b.report.stop_reason, "the same stop reason");
}

TEST(the_candidate_order_does_not_change_the_choice) {
    // The tie-break is what makes this hold: best score, then civilization, then node.
    // Without it the winner would be whichever equal-scoring cell the pool listed first.
    //
    // Every score is FLAT here on purpose. With the rising scores the other tests use,
    // one cell always wins outright and the tie-break is never reached -- the test would
    // pass against a `min_element` that just took the head of the pool.
    FoundingRequest plain = two_parents();
    for (auto& [key, field] : plain.scores) std::fill(field.begin(), field.end(), 0.5);
    FoundingRequest reversed = plain;
    for (auto& [key, nodes] : reversed.candidates) std::reverse(nodes.begin(), nodes.end());

    const FoundingResult a = found_cities(42, plain, line_distance());
    const FoundingResult b = found_cities(42, reversed, line_distance());
    check::equal(a.sites.size(), b.sites.size(), "the same number of cities");
    for (std::size_t i = 0; i < a.sites.size(); ++i) {
        check::equal(b.sites[i].node, a.sites[i].node,
                     "city " + std::to_string(i) + " to ignore candidate order");
    }
}

TEST(an_unsettled_people_is_preferred_over_a_second_city) {
    // Two civilizations under one parent. The variety the registry describes only
    // reaches the map if a people with no city yet outranks a people wanting another.
    FoundingRequest request = two_parents();
    request.parents = {{"alder", "Alder", 100.0}};
    request.owners = {{"alder_folk", "alder"}, {"alder_kin", "alder"}};
    request.quotas = {{"alder_folk", 4}, {"alder_kin", 4}};
    request.candidates["alder_kin"] = request.candidates["alder_folk"];
    request.scores["alder_kin"] = request.scores["alder_folk"];
    request.candidates.erase("birch_folk");
    request.scores.erase("birch_folk");

    const FoundingResult r = found_cities(42, request, line_distance());
    check::is_true(r.sites.size() >= 2, "at least two cities");
    check::is_true(r.sites[0].population_profile != r.sites[1].population_profile,
                   "the second city to go to the other people");
}

// --- the record -----------------------------------------------------------

TEST(a_migration_names_where_its_settlers_came_from) {
    FoundingRequest request = two_parents();
    request.parents = {{"alder", "Alder", 100.0}};
    request.owners = {{"alder_folk", "alder"}};
    request.quotas = {{"alder_folk", 3}};
    request.candidates.erase("birch_folk");
    request.scores.erase("birch_folk");

    const FoundingResult r = found_cities(42, request, line_distance());
    check::is_true(r.sites.size() >= 2, "at least two cities");
    check::equal(r.sites[0].migration_source_node, -1, "a capital to have no source");
    check::equal(r.sites[0].migration_distance_m, 0.0, "a capital to have travelled nowhere");
    for (std::size_t i = 1; i < r.sites.size(); ++i) {
        check::is_true(r.sites[i].migration_source_node >= 0,
                       "city " + std::to_string(i) + " to name its source");
        check::close(r.sites[i].migration_distance_m,
                     std::abs(r.sites[i].node - r.sites[i].migration_source_node) * kStep, 1e-9,
                     "the migration distance to be the real one");
    }
}

TEST(a_later_age_resumes_after_the_cities_that_survived_it) {
    FoundingRequest request = two_parents();
    FoundedSite survivor;
    survivor.node = 4;
    survivor.population_profile = "alder_folk";
    survivor.parent_race_id = "alder";
    survivor.founding_capital = true;
    survivor.founding_year = 7000;
    request.survivors = {survivor};

    const FoundingResult r = found_cities(42, request, line_distance());
    check::equal(r.report.start_year, 7250LL, "the new age to start a round after the last city");
    for (const FoundedSite& s : r.sites) {
        if (s.node == 4) continue;
        check::is_true(s.founding_year >= 7250,
                       "no new city to predate the survivors it was founded beside");
    }
}

TEST(the_stop_reason_says_why_founding_ended) {
    const FoundingResult met = found_cities(42, two_parents(), line_distance());
    check::equal(met.report.stop_reason, std::string("capacity_target_reached"),
                 "a world with room to reach its target");
    check::equal(met.report.placed_cities, met.report.target_cities, "every quota filled");

    // Spacing wider than the world leaves room for one city per parent and no more.
    FoundingRequest cramped = two_parents();
    cramped.spacing = cramped.world_reach;
    const FoundingResult stuck = found_cities(42, cramped, line_distance());
    check::equal(stuck.report.stop_reason, std::string("no_eligible_expansion"),
                 "a world with no room left");
    check::is_true(stuck.report.placed_cities < stuck.report.target_cities,
                   "a cramped world to fall short of its target");
}

TEST(the_elapsed_years_match_the_rounds_that_ran) {
    const FoundingResult r = found_cities(42, two_parents(), line_distance());
    check::equal(r.report.elapsed_years, (r.report.turns - 1) * 250LL,
                 "the clock to advance one round's years per round");
    check::equal(r.report.end_year, r.report.start_year + r.report.elapsed_years,
                 "the end year to be the start plus what elapsed");
}

TEST(an_interval_that_could_never_fire_is_refused) {
    // The clock only ever lands on multiples of the round length, so `year % interval`
    // with a non-multiple interval is zero on no round at all: no diaspora, no error,
    // and every people that needed the bonus quietly missing from the map.
    FoundingRequest request = two_parents();
    request.rules.years_per_round = 250;
    request.rules.diaspora_interval_years = 400;
    check::throws([&] { found_cities(42, request, line_distance()); },
                  "an interval that is not a multiple of the round");

    request.rules.diaspora_interval_years = 1000;
    found_cities(42, request, line_distance());  // a multiple is accepted
}

// --- the authored tables --------------------------------------------------

TEST(the_authored_parent_races_could_found_a_world) {
    const auto races = parent_races();
    check::is_true(!races.empty(), "at least one parent race, or nobody founds anything");

    std::set<std::string_view> ids;
    bool any_participates = false;
    for (const ParentRace& r : races) {
        check::is_true(ids.insert(r.id).second,
                       "parent race id " + std::string(r.id) + " to appear once");
        check::is_true(!r.name.empty(), "parent race " + std::string(r.id) + " to have a name");
        check::between(r.founding_participation_percent, 0.0, 100.0,
                       "the participation of " + std::string(r.id) + " to be a percentage");
        if (r.founding_participation_percent > 0.0) any_participates = true;
    }
    check::is_true(any_participates,
                   "some parent to expand past its capital, or every world is three cities");
}

TEST(the_authored_founding_rules_can_actually_fire_a_diaspora) {
    // The rules are data, so this asserts the data rather than the code: an interval
    // that is not a multiple of the round length fires on no round at all.
    const FoundingRules r = authored_founding_rules();
    check::is_true(r.years_per_round > 0, "a positive round length");
    check::equal(r.diaspora_interval_years % r.years_per_round, 0LL,
                 "the diaspora interval to land on a round boundary");
    check::between(r.origin_min_separation_degrees, 0.0, 180.0, "a separation in degrees");
}

TEST(the_registry_defaults_name_a_people_that_exists) {
    // registry-sections.json and civilizations.json are separate files, and nothing in
    // the codegen makes one refer to the other. A default naming a deleted profile would
    // otherwise only surface when a non-mixed world was generated.
    const RegistryDefaults d = registry_defaults();
    check::is_true(civilization_profile(d.profile_id) != nullptr,
                   "the default profile '" + std::string(d.profile_id) + "' to be in the registry");
    check::is_true(!d.resource_score_factor_label.empty(),
                   "the resource factor label to be named");
    check::between(d.medium_suitability_min, 0.0, 1.0, "the medium-city threshold to be a score");
}
