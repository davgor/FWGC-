// The working population a finished world already implies.
#include "check.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "fwg/city_plan.hpp"
#include "fwg/config.hpp"
#include "fwg/npcs.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

NpcPlot plot(const std::string& id, const std::string& building, int workers) {
    NpcPlot p;
    p.id = id;
    p.building_id = building;
    p.workers = workers;
    return p;
}

NpcSettlementSite settlement(const std::string& uid, const std::string& name) {
    NpcSettlementSite s;
    s.uid = uid;
    s.name = name;
    s.civilization_id = "human_heartland";
    s.parent_race_id = "human";
    s.node = 100;
    s.x = NpcNumber::of(4.0);
    s.z = NpcNumber::of(5.0);
    return s;
}

// One city with a known roster: a guildhall (the best giver rank there is), a barracks and
// a smithy, plus a house the policy has never heard of.
NpcRequest one_city() {
    NpcRequest r;
    r.seed = 42;
    r.has_city_plans = true;
    r.settlements.push_back(settlement("surface-city-1-100-human_heartland", "Bargdorn"));

    NpcCityPlan city;
    city.city_uid = "surface-city-1-100-human_heartland";
    city.name = "Bargdorn";
    city.civilization_id = "human_heartland";
    city.city_class = "capital";
    city.status = "built";
    city.plots.push_back(plot("plot-0", "building.guildhall", 2));
    city.plots.push_back(plot("plot-1", "building.barracks", 16));
    city.plots.push_back(plot("plot-2", "building.smithy", 2));
    city.plots.push_back(plot("plot-3", "building.house", 4));
    r.city_plans.push_back(std::move(city));

    // The people's life, so everyone draws an age: the owner's human, 80 years.
    NpcCivilizationEntity heartland;
    heartland.id = "human_heartland";
    heartland.parent_race_id = "human";
    heartland.life = NpcLifeStages{15.0, 46.0, 80.0};
    r.entities.push_back(heartland);
    return r;
}

const NpcPerson* person_by_uid(const NpcRoster& roster, const std::string& uid) {
    for (const NpcPerson& person : roster.people) {
        if (person.uid == uid) return &person;
    }
    return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
// The policy.
// ---------------------------------------------------------------------------

TEST(the_policy_carries_every_roster_verb_row_and_rank) {
    // The reference's ten and the cleansed refugees' two (rebuild, return; 2026-09-23).
    check::equal(npc_verbs().size(), std::size_t(12), "the canonical verbs");
    check::equal(npc_buildings().size(), std::size_t(88), "staffing rosters");
    check::equal(npc_role_verbs().size(), std::size_t(84), "verb rows");
    check::equal(npc_giver_rank().size(), std::size_t(39), "giver ranks");
    check::equal(npc_caps().size(), std::size_t(5), "caps");
    check::equal(npc_place_sources().size(), std::size_t(8), "place sources");
    check::is_true(npc_posts_revision() >= 1, "a posts revision");
    check::equal(npc_names_revision(), 2, "the names revision: the genome contract, not the tables");
    check::is_true(!npc_method().empty() && !npc_limits().empty(), "method and limits prose");
}

TEST(a_building_absent_from_the_giver_rank_is_never_a_giver_and_not_rank_zero) {
    check::equal(npc_giver_index("building.guildhall"), 0, "the guildhall to be the best giver");
    check::is_true(npc_giver_index("building.smithy") > 0, "the smithy to rank below it");
    // The inversion this test exists for. `building.house` holds no rank; a port that
    // defaults a missing one to 0 makes every unranked building the BEST giver there is.
    check::equal(npc_giver_index("building.house"), -1, "an unranked building");
    check::equal(npc_giver_index("nonsense"), -1, "an unknown building");

    // ...and the ranks are a permutation with no repeat, or one of two entries is dead.
    std::set<std::string_view> seen;
    for (std::string_view building : npc_giver_rank()) {
        check::is_true(seen.insert(building).second, "no repeated rank: " + std::string(building));
        check::is_true(npc_building(building) != nullptr,
                       std::string(building) + " to have a roster");
    }
    check::equal(seen.size(), std::size_t(39), "every rank distinct");
}

TEST(eleven_roles_hold_a_post_and_can_never_become_a_giver) {
    const std::vector<std::string> unverbed = {
        "apprentice", "assistant",  "cart_handler",       "farm_hand",      "hall_attendant",
        "maintenance_laborer", "ore_handler", "porter", "service_worker", "support_worker",
        "water_carrier"};
    for (const std::string& role : unverbed) {
        check::is_true(!npc_role_verbed(role), role + " to carry no verb");
    }
    check::is_true(npc_role_verbed("smith"), "a smith to carry one");
    check::is_true(npc_role_verbed("commander"), "a commander to carry one");
    // The floor: the eleven above are a real subset and not the whole table.
    int verbed = 0;
    for (const NpcRoleVerbs& row : npc_role_verbs()) {
        if (!row.verbs.empty()) ++verbed;
    }
    check::equal(verbed, 84, "every tagged role to have at least one verb");
}

TEST(a_roster_row_is_seniority_order_and_the_first_role_is_the_senior_post) {
    const NpcBuilding* barracks = npc_building("building.barracks");
    check::is_true(barracks != nullptr, "a barracks roster");
    check::equal(barracks->roles.size(), std::size_t(3), "three role rows");
    check::equal(std::string(barracks->roles[0].role), std::string("garrison_officer"),
                 "the officer first, because the first row is the senior post");
    check::equal(barracks->roles[0].target, 2, "two of them");
    check::equal(std::string(barracks->roles[1].role), std::string("soldier"), "then the soldiers");
    check::equal(barracks->roles[1].target, 12, "twelve of them");
    int total = 0;
    for (const NpcRole& row : barracks->roles) total += row.target;
    check::equal(total, 16, "sixteen workers in all");
}

// ---------------------------------------------------------------------------
// Posts.
// ---------------------------------------------------------------------------

TEST(plot_ten_does_not_sort_before_plot_two) {
    std::vector<NpcPlot> plots;
    plots.push_back(plot("plot-10", "building.smithy", 2));
    plots.push_back(plot("plot-2", "building.smithy", 2));
    plots.push_back(plot("plot-1", "building.smithy", 2));
    int mismatches = 0;
    const std::vector<NpcPost> posts = npc_posts_in(plots, mismatches);
    check::equal(posts.size(), std::size_t(6), "three smithies of two");
    check::equal(posts[0].plot_id, std::string("plot-1"), "plot-1 first");
    check::equal(posts[2].plot_id, std::string("plot-2"), "plot-2 second");
    check::equal(posts[4].plot_id, std::string("plot-10"), "plot-10 last, not first");
    // The ordinals follow that order, so getting the sort wrong renames every person in
    // the site rather than reordering a list.
    check::equal(posts[0].ordinal, 0, "the first ordinal");
    check::equal(posts[5].ordinal, 5, "and the last");
    check::is_true(posts[0].senior, "the senior post is on the first plot of the building");
    for (std::size_t i = 1; i < posts.size(); ++i) {
        check::is_true(!posts[i].senior, "and on nothing else");
    }
}

TEST(a_plot_that_yields_nobody_yields_nobody_silently) {
    std::vector<NpcPlot> plots;
    plots.push_back(plot("plot-0", "building.house", 8));   // not in the policy
    plots.push_back(plot("plot-1", "", 8));                 // no building at all
    plots.push_back(plot("plot-2", "building.smithy", 0));  // unstaffed
    plots.push_back(plot("plot-3", "building.smithy", -3)); // and nonsense
    int mismatches = 0;
    const std::vector<NpcPost> posts = npc_posts_in(plots, mismatches);
    check::equal(posts.size(), std::size_t(0), "no posts from any of them");
    check::equal(mismatches, 0, "and no mismatch counted: these are the intended cases");

    // The floor: a plot the policy DOES know yields people, so the zero above is the rule
    // and not a function that always returns nothing.
    std::vector<NpcPlot> staffed;
    staffed.push_back(plot("plot-0", "building.smithy", 2));
    const std::vector<NpcPost> real = npc_posts_in(staffed, mismatches);
    check::equal(real.size(), std::size_t(2), "a known building to yield its roster");
}

TEST(the_plots_own_worker_count_wins_and_nothing_is_padded_with_a_generic_worker) {
    int mismatches = 0;
    // A barracks budgeted for four rather than sixteen: TRUNCATE the tail.
    std::vector<NpcPlot> few;
    few.push_back(plot("plot-0", "building.barracks", 4));
    const std::vector<NpcPost> short_roster = npc_posts_in(few, mismatches);
    check::equal(short_roster.size(), std::size_t(4), "the plot's count to win");
    check::equal(mismatches, 1, "and the difference to be counted rather than hidden");
    check::equal(std::string(short_roster[0].role), std::string("garrison_officer"),
                 "the senior posts to survive truncation");
    check::equal(std::string(short_roster[3].role), std::string("soldier"),
                 "and the tail to be what is cut");

    // A smithy budgeted for five rather than two: continue under the LAST role.
    mismatches = 0;
    std::vector<NpcPlot> many;
    many.push_back(plot("plot-0", "building.smithy", 5));
    const std::vector<NpcPost> long_roster = npc_posts_in(many, mismatches);
    check::equal(long_roster.size(), std::size_t(5), "the plot's count to win here too");
    check::equal(mismatches, 1, "and to be counted");
    check::equal(std::string(long_roster[0].role), std::string("smith"), "the smith");
    for (std::size_t i = 1; i < long_roster.size(); ++i) {
        check::equal(std::string(long_roster[i].role), std::string("apprentice"),
                     "the last role repeated, never a generic 'worker'");
    }
}

TEST(only_the_first_plot_of_a_building_in_a_site_holds_its_senior_post) {
    std::vector<NpcPlot> plots;
    plots.push_back(plot("plot-0", "building.smithy", 2));
    plots.push_back(plot("plot-1", "building.smithy", 2));
    plots.push_back(plot("plot-2", "building.barracks", 16));
    int mismatches = 0;
    const std::vector<NpcPost> posts = npc_posts_in(plots, mismatches);
    check::equal(mismatches, 0, "no mismatch in this roster");
    int seniors = 0;
    for (const NpcPost& post : posts) {
        if (post.senior) ++seniors;
    }
    check::equal(seniors, 2, "one senior per BUILDING, not per plot");
    check::is_true(posts.front().senior, "the first smith to be senior");
    // The ordinal is scoped to the whole site, so the second smithy starts at 2.
    check::equal(posts[2].ordinal, 2, "ordinals continuing across plots of one building");
    check::is_true(!posts[2].senior, "and the second smithy's smith not to be senior");
}

TEST(a_building_id_reads_short_in_a_uid) {
    check::equal(std::string(npc_short_building("building.smithy")), std::string("smithy"),
                 "the constant prefix dropped");
    check::equal(std::string(npc_short_building("building.hamlet_food_store")),
                 std::string("hamlet_food_store"), "only up to the FIRST dot");
    check::equal(std::string(npc_short_building("smithy")), std::string("smithy"),
                 "an id with no dot unchanged");
}

// ---------------------------------------------------------------------------
// The earmark.
// ---------------------------------------------------------------------------

TEST(the_important_subset_is_bounded_by_the_authored_cap) {
    const NpcRoster roster = generate_npc_roster(one_city());
    check::is_true(roster.ok, "an ok roster: " + roster.error);
    check::equal(roster.summary.sites, 1, "one site");
    // 2 + 16 + 2 staffed, and the house yields nobody.
    check::equal(roster.summary.total, 20, "twenty posts");
    check::equal(npc_cap("capital"), 6, "a capital's authored cap");
    // Only three buildings here are both ranked and verbed, so the cap does not bind and
    // the earmark is the three seniors.
    check::equal(roster.summary.important, 3, "one important person per giver building");
    check::equal(roster.summary.notes.important_capped_out, 0, "nobody capped out");

    std::set<std::string> important;
    for (const NpcPerson& person : roster.people) {
        if (person.important) important.insert(*person.building_id);
    }
    check::is_true(important.count("building.guildhall") != 0, "the guildhall earmarked");
    check::is_true(important.count("building.barracks") != 0, "the barracks earmarked");
    check::is_true(important.count("building.smithy") != 0, "the smithy earmarked");
    check::is_true(important.count("building.house") == 0, "and the unranked house not");
}

TEST(a_cap_that_binds_counts_what_it_turned_away) {
    NpcRequest request = one_city();
    request.city_plans[0].city_class = "small";  // cap 3
    // Four ranked, verbed giver buildings against a cap of three.
    request.city_plans[0].plots.push_back(plot("plot-4", "building.inn", 5));
    const NpcRoster roster = generate_npc_roster(request);
    check::is_true(roster.ok, "an ok roster");
    check::equal(npc_cap("small"), 3, "a small city's cap");
    check::equal(roster.summary.important, 3, "the cap to bind");
    check::equal(roster.summary.notes.important_capped_out, 1, "and to count the one it refused");

    // Rank decides WHICH three: the guildhall is rank 0 and the smithy is the worst of the
    // four, so the smithy is what falls off.
    std::set<std::string> important;
    for (const NpcPerson& person : roster.people) {
        if (person.important) important.insert(*person.building_id);
    }
    check::is_true(important.count("building.guildhall") != 0, "rank 0 kept");
    check::is_true(important.count("building.smithy") == 0,
                   "and the worst-ranked giver dropped, not an arbitrary one");
}

TEST(a_site_with_no_cap_earmarks_nobody) {
    NpcRequest request = one_city();
    request.city_plans[0].city_class = "enormous";  // outside the three classes
    const NpcRoster roster = generate_npc_roster(request);
    check::is_true(roster.ok, "an ok roster");
    check::equal(roster.summary.total, 20, "the same people");
    check::equal(roster.summary.important, 0, "and none of them important");
    check::is_true(!roster.sites.front().city_class.has_value(),
                   "an unknown class to store as absent");
    check::equal(roster.summary.notes.important_capped_out, 0,
                 "a missing cap to turn nobody away rather than turning everybody away");
}

// ---------------------------------------------------------------------------
// Identity and names.
// ---------------------------------------------------------------------------

TEST(a_post_uid_spells_out_its_site_building_and_ordinal) {
    const NpcRoster roster = generate_npc_roster(one_city());
    const NpcPerson* first =
        person_by_uid(roster, "npc-surface-city-1-100-human_heartland-guildhall-0");
    check::is_true(first != nullptr, "the guildhall's senior post by its spelled-out uid");
    check::is_true(first->important, "and for it to be the earmarked one");
    check::equal(*first->site_uid, std::string("surface-city-1-100-human_heartland"), "its site");
    check::equal(*first->building_id, std::string("building.guildhall"), "its building");
    check::equal(first->plot_id, std::string("plot-0"), "its plot");
    check::is_true(first->alive, "and alive at birth");

    std::set<std::string> uids;
    for (const NpcPerson& person : roster.people) {
        check::is_true(uids.insert(person.uid).second, "a unique uid, got " + person.uid);
    }
    check::equal(uids.size(), std::size_t(20), "twenty distinct people");
}

TEST(everyone_with_a_resolved_parent_race_is_named_in_their_own_tongue) {
    const NpcRoster roster = generate_npc_roster(one_city());
    std::set<std::string> names;
    for (const NpcPerson& person : roster.people) {
        check::is_true(person.name.has_value(), person.uid + " to be named");
        check::is_true(!person.name->empty(), person.uid + " to have a non-empty name");
        names.insert(*person.name);
    }
    // Names concentrate on purpose -- a stock draw, not a uniform one -- so twenty
    // people do NOT produce twenty names. They must produce more than one, though, or
    // the draw is not reading its stream.
    check::is_true(names.size() > 1, "more than one name across the site");
    check::is_true(names.size() <= 20, "and never more than one per person");

    // One stream per person, so replaying gives the same names.
    const NpcRoster again = generate_npc_roster(one_city());
    for (std::size_t i = 0; i < roster.people.size(); ++i) {
        check::equal(*again.people[i].name, *roster.people[i].name, "a replayed name");
    }
}

TEST(an_unresolved_parent_race_leaves_the_name_absent_and_is_counted) {
    NpcRequest request = one_city();
    request.settlements[0].parent_race_id.clear();
    request.settlements[0].civilization_id.clear();
    request.city_plans[0].civilization_id.clear();
    const NpcRoster roster = generate_npc_roster(request);
    check::is_true(roster.ok, "an ok roster: an unnameable people is not a failure");
    check::equal(roster.summary.notes.unresolved_parent_race, 1, "the site counted");
    check::equal(roster.summary.total, 20, "the people still there");
    for (const NpcPerson& person : roster.people) {
        check::is_true(!person.name.has_value(),
                       "NEVER guessed as human: a guessed parent is a wrong tongue");
    }
}

TEST(a_parent_race_without_a_civilization_id_fails_the_whole_block_loudly) {
    // The asymmetry the naming path was rebuilt around. The name is guarded on
    // parent_race_id, never on civilization_id, so a settlement row carrying one and not
    // the other reaches the namer with nothing to name from -- and that must be a loud
    // failure rather than a silent fallback to somebody else's tongue.
    NpcRequest request = one_city();
    request.settlements[0].civilization_id.clear();
    request.city_plans[0].civilization_id.clear();
    const NpcRoster roster = generate_npc_roster(request);
    check::is_true(!roster.ok, "a failed block");
    check::is_true(roster.error.find("civilization_id") != std::string::npos,
                   "naming the missing field, got: " + roster.error);
    check::is_true(roster.people.empty(), "and no half-built roster left behind");
    check::equal(roster.version, kNpcRosterVersion, "the version to survive the failure");
}

TEST(a_hamlet_is_keyed_on_its_node_and_named_for_the_city_below_which_it_sits) {
    NpcRequest r;
    r.seed = 7;
    r.has_hamlet_plans = true;
    r.settlements.push_back(settlement("surface-city-1-100-human_heartland", "Bargdorn (Age 2)"));

    NpcHamletPlan hamlet;
    hamlet.node = 1062;
    hamlet.civilization_id = "human_heartland";
    hamlet.core_city_uid = "surface-city-1-100-human_heartland";
    hamlet.kind = "inland";
    hamlet.hamlet_id = "hamlet-0";
    hamlet.plots.push_back(plot("plot-0", "building.hamlet_shrine", 1));
    r.hamlet_plans.push_back(hamlet);

    NpcHamletPlan harbour = hamlet;
    harbour.node = 1063;
    harbour.kind = "coastal";
    r.hamlet_plans.push_back(harbour);

    const NpcRoster roster = generate_npc_roster(r);
    check::is_true(roster.ok, "an ok roster: " + roster.error);
    check::equal(roster.sites.size(), std::size_t(2), "two hamlets");
    // `hamlet-node-1062`, not `hamlet-1062`: two keys that read alike and mean different
    // things is how a consumer joins the wrong rows without an error.
    check::equal(roster.sites[0].uid, std::string("hamlet-node-1062"), "the spelled-out key");
    // The (Age N) cut is live; the retired ' City' strip is not carried.
    check::equal(roster.sites[0].name, std::string("the farmstead below Bargdorn"),
                 "the age suffix cut off the core's name");
    check::equal(roster.sites[1].name, std::string("the harbour below Bargdorn"),
                 "a coastal hamlet to be a harbour");
    check::equal(roster.sites[0].plan_id, std::string("hamlet-0"),
                 "the ordinal plan id carried as convenience only");
}

TEST(two_sites_that_would_read_alike_are_told_apart_by_their_node) {
    NpcRequest r;
    r.seed = 7;
    r.has_hamlet_plans = true;
    r.settlements.push_back(settlement("core", "Dunlin"));
    for (int node : {10, 11}) {
        NpcHamletPlan hamlet;
        hamlet.node = node;
        hamlet.civilization_id = "human_heartland";
        hamlet.core_city_uid = "core";
        hamlet.plots.push_back(plot("plot-0", "building.hamlet_shrine", 1));
        r.hamlet_plans.push_back(hamlet);
    }
    const NpcRoster roster = generate_npc_roster(r);
    check::equal(roster.sites.size(), std::size_t(2), "two hamlets");
    check::equal(roster.sites[0].name, std::string("the farmstead below Dunlin (10)"),
                 "every colliding instance suffixed, including the first");
    check::equal(roster.sites[1].name, std::string("the farmstead below Dunlin (11)"),
                 "and the second");

    // The floor: a name that does NOT collide is left alone.
    NpcRequest single = r;
    single.hamlet_plans.resize(1);
    const NpcRoster one = generate_npc_roster(single);
    check::equal(one.sites[0].name, std::string("the farmstead below Dunlin"),
                 "a name with nothing to collide with");
}

TEST(a_plan_carrying_a_zero_coordinate_keeps_it_rather_than_taking_the_settlements) {
    // KEY-ABSENCE, not falsy. A city plan with `x: 0` means the origin, and substituting
    // the settlement's x there moves the site.
    NpcRequest request = one_city();
    request.city_plans[0].x = NpcNumber::of(0.0);
    const NpcRoster zeroed = generate_npc_roster(request);
    check::is_true(zeroed.sites[0].x.present, "the plan's x to be present");
    check::equal(zeroed.sites[0].x.value, 0.0, "and to be the authored zero");
    check::equal(zeroed.sites[0].z.value, 5.0, "while an absent z falls back to the settlement");

    const NpcRoster absent = generate_npc_roster(one_city());
    check::equal(absent.sites[0].x.value, 4.0, "an absent x to fall back");
    check::is_true(absent.sites[0].x.present, "and to be present once resolved");
}

// ---------------------------------------------------------------------------
// The cast.
// ---------------------------------------------------------------------------

TEST(the_cast_is_folded_in_and_is_never_subject_to_the_per_site_cap) {
    NpcRequest request = one_city();
    request.heroes_ok = true;
    NpcCastMember hero;
    hero.uid = "hero-castellan-7";
    hero.display_name = "Ceol the Quiet";
    hero.role = "castellan";
    hero.status = "living";
    hero.home_uid = "surface-city-1-100-human_heartland";
    request.cast.push_back(hero);

    NpcCastMember dread;
    dread.uid = "dread-1";
    dread.name = "The Pale Thing";
    dread.status = "fallen";
    request.cast.push_back(dread);

    const NpcRoster roster = generate_npc_roster(request);
    check::is_true(roster.ok, "an ok roster");
    check::equal(roster.summary.heroes_linked, 2, "both cast records linked");
    check::equal(roster.summary.total, 22, "twenty posts plus the cast");
    check::equal(roster.summary.dead, 1, "the fallen Dread counted dead");
    check::equal(roster.summary.alive, 21, "and everyone else alive");

    const NpcPerson* castellan = person_by_uid(roster, "npc-hero-castellan-7");
    check::is_true(castellan != nullptr, "one prefix, because the cast uid already carries its own");
    check::is_true(castellan->important, "a hero always important");
    check::is_true(!castellan->building_id.has_value(), "and holding no building");
    check::equal(castellan->post, std::string("castellan"), "its role as its post");
    check::equal(*castellan->name, std::string("Ceol the Quiet"), "its display name");
    check::equal(*castellan->site_uid, std::string("surface-city-1-100-human_heartland"),
                 "filed at its home");

    // The cap was 6 and only 3 posts were earmarked, so this cannot be a coincidence of a
    // slack cap: force the cap to bind and the hero must still be important.
    NpcRequest tight = request;
    tight.city_plans[0].city_class = "small";
    const NpcRoster bound = generate_npc_roster(tight);
    check::equal(bound.summary.important, 5, "three capped post givers plus both cast");
    check::is_true(person_by_uid(bound, "npc-hero-castellan-7")->important,
                   "a cap that could demote a hero would be the bug");
}

TEST(a_cast_member_standing_somewhere_else_carries_the_node_they_actually_stand_on) {
    NpcRequest request = one_city();
    request.heroes_ok = true;
    NpcCastMember hero;
    hero.uid = "hero-abbot-3";
    hero.name = "Aldric";
    hero.status = "living";
    hero.home_uid = "surface-city-1-100-human_heartland";
    hero.presence_uid = "shrine-44";
    hero.presence_site_kind = "shrine";
    request.cast.push_back(hero);
    // A religion site: a node and no coordinates, which is the live case.
    NpcPlaceRow shrine;
    shrine.uid = "shrine-44";
    shrine.node = 777;
    request.place_religion_sites.push_back(shrine);

    const NpcRoster roster = generate_npc_roster(request);
    const NpcPerson* abbot = person_by_uid(roster, "npc-hero-abbot-3");
    check::is_true(abbot != nullptr, "the abbot");
    check::equal(*abbot->site_uid, std::string("surface-city-1-100-human_heartland"),
                 "filed at home, because the shrine is not a planned site");
    check::is_true(abbot->presence_node.has_value(), "but carrying where they really stand");
    check::equal(*abbot->presence_node, 777, "the shrine's node");
    check::is_true(!abbot->presence_x.has_value(),
                   "and omitting coordinates the source record does not carry");
    check::equal(roster.summary.notes.cast_located_by_presence, 1, "counted as located");
    check::equal(roster.summary.notes.cast_presence_unresolved, 0, "and not as unresolved");

    // A presence nothing can resolve is counted and carries NO node -- a confidently wrong
    // location is worse than none for anything measuring travel.
    NpcRequest lost = request;
    lost.place_religion_sites.clear();
    const NpcRoster nowhere = generate_npc_roster(lost);
    const NpcPerson* stranded = person_by_uid(nowhere, "npc-hero-abbot-3");
    check::is_true(!stranded->presence_node.has_value(), "no node when it cannot be resolved");
    check::equal(nowhere.summary.notes.cast_presence_unresolved, 1, "and counted");
}

TEST(the_place_index_lets_the_first_block_that_names_an_id_win) {
    NpcRequest request = one_city();
    request.heroes_ok = true;
    NpcCastMember hero;
    hero.uid = "hero-1";
    hero.status = "living";
    hero.presence_uid = "shared-id";
    request.cast.push_back(hero);
    // Settlements are walked FIRST and nests LAST, so the settlement row must win.
    NpcPlaceRow early;
    early.uid = "shared-id";
    early.node = 11;
    request.place_settlements.push_back(early);
    NpcPlaceRow late;
    late.uid = "shared-id";
    late.node = 99;
    request.place_nests.push_back(late);

    const NpcRoster roster = generate_npc_roster(request);
    const NpcPerson* person = person_by_uid(roster, "npc-hero-1");
    check::is_true(person->presence_node.has_value(), "the presence resolved");
    check::equal(*person->presence_node, 11, "the FIRST writer to win, not the last");
    check::equal(roster.summary.notes.cast_outside_a_planned_site, 1,
                 "a presence that is not a planned site to be counted");
}

TEST(a_ruin_presence_resolves_through_the_city_the_ruin_used_to_be) {
    NpcRequest request = one_city();
    request.heroes_ok = true;
    NpcCastMember hero;
    hero.uid = "hero-2";
    hero.status = "living";
    hero.presence_uid = "old-town";
    request.cast.push_back(hero);
    NpcPlaceRow ruin;
    ruin.uid = "ruin-old-town";
    ruin.node = 512;
    ruin.x = 3.0;
    ruin.z = 4.0;
    request.place_ruins.push_back(ruin);

    const NpcRoster roster = generate_npc_roster(request);
    const NpcPerson* person = person_by_uid(roster, "npc-hero-2");
    check::equal(*person->presence_node, 512, "the canonical ruin- key tried as well");
    check::is_true(person->presence_x.has_value(), "and its coordinates carried when it has them");
    check::equal(*person->presence_x, 3.0, "the x");
}

// ---------------------------------------------------------------------------
// The block.
// ---------------------------------------------------------------------------

TEST(people_sort_by_site_then_building_with_nulls_last) {
    NpcRequest request = one_city();
    request.heroes_ok = true;
    NpcCastMember hero;
    hero.uid = "hero-9";
    hero.status = "living";  // no home and no presence: site_uid stays absent
    request.cast.push_back(hero);
    const NpcRoster roster = generate_npc_roster(request);

    const auto key = [](const NpcPerson& p) {
        return std::make_tuple(p.site_uid.value_or(std::string(kNpcNullSortKey)),
                               p.building_id.value_or(std::string(kNpcNullSortKey)), p.uid);
    };
    for (std::size_t i = 1; i < roster.people.size(); ++i) {
        check::is_true(key(roster.people[i - 1]) < key(roster.people[i]),
                       "people in (site, building, uid) order");
    }
    // '~' sorts after every letter and digit, so the site-less hero lands LAST.
    check::equal(roster.people.back().uid, std::string("npc-hero-9"),
                 "a record with no site to land last, not first");
}

TEST(every_counter_is_exported_even_when_it_is_zero) {
    const NpcRoster roster = generate_npc_roster(one_city());
    const NpcNotes& n = roster.summary.notes;
    // A monitoring counter that appears only once it is non-zero cannot be told apart from
    // one that was never computed, which makes it useless on exactly the day it would have
    // earned its keep. The struct makes that structural; this pins the values.
    check::equal(n.unresolved_parent_race, 0, "unresolved_parent_race");
    check::equal(n.unanchored_sites, 0, "unanchored_sites");
    check::equal(n.roster_mismatches, 0, "roster_mismatches");
    check::equal(n.duplicate_site_keys, 0, "duplicate_site_keys");
    check::equal(n.important_capped_out, 0, "important_capped_out");
    check::equal(n.cast_outside_a_planned_site, 0, "cast_outside_a_planned_site");
    check::equal(n.cast_located_by_presence, 0, "cast_located_by_presence");
    check::equal(n.cast_presence_unresolved, 0, "cast_presence_unresolved");
    check::equal(n.appearance_unresolved, 0, "appearance_unresolved");
    check::close(roster.summary.important_fraction, 0.15, 1e-9, "the important share");
}

TEST(an_unanchored_plan_costs_one_site_and_never_the_block) {
    NpcRequest request = one_city();
    NpcCityPlan orphan;                 // no city_uid
    orphan.name = "Nowhere";
    orphan.plots.push_back(plot("plot-0", "building.smithy", 2));
    request.city_plans.push_back(orphan);

    request.has_hamlet_plans = true;
    NpcHamletPlan floating;             // no node
    floating.civilization_id = "human_heartland";
    request.hamlet_plans.push_back(floating);

    request.has_castle_plans = true;
    NpcCastlePlan castle;               // names a fortress that does not exist
    castle.fortress_id = "fortress-99";
    request.castle_plans.push_back(castle);

    const NpcRoster roster = generate_npc_roster(request);
    check::is_true(roster.ok, "the block to survive all three");
    check::equal(roster.summary.notes.unanchored_sites, 3, "each costing one site");
    check::equal(roster.summary.sites, 1, "and the real city surviving");
    check::equal(roster.summary.total, 20, "with its people intact");
}

TEST(a_duplicate_site_key_is_dropped_and_counted) {
    NpcRequest request = one_city();
    NpcCityPlan twin = request.city_plans[0];
    twin.plots.clear();
    twin.plots.push_back(plot("plot-0", "building.smithy", 2));
    request.city_plans.push_back(twin);
    const NpcRoster roster = generate_npc_roster(request);
    check::is_true(roster.ok, "an ok roster");
    check::equal(roster.summary.sites, 1, "one site for one uid");
    check::equal(roster.summary.notes.duplicate_site_keys, 1, "the duplicate counted");
    // The sort is stable, so the FIRST of the two in list order survives.
    check::equal(roster.summary.total, 20, "the first row's roster to be the one kept");
}

TEST(a_world_with_no_planned_block_reports_a_failure_rather_than_an_empty_roster) {
    NpcRequest request;
    request.seed = 1;
    const NpcRoster roster = generate_npc_roster(request);
    check::is_true(!roster.ok, "a failed block");
    check::is_true(roster.error.find("planner") != std::string::npos,
                   "saying to run the planners, got: " + roster.error);

    // But a PRESENT block with no plans is an ordinary empty world, not a failure: the
    // difference is whether the planners ran, not whether they found anywhere to build.
    NpcRequest empty;
    empty.seed = 1;
    empty.has_city_plans = true;
    const NpcRoster nothing = generate_npc_roster(empty);
    check::is_true(nothing.ok, "an empty world to be ok");
    check::equal(nothing.summary.total, 0, "with nobody in it");
    check::equal(nothing.summary.important_fraction, 0.0, "and a zero share rather than a NaN");
}

TEST(a_castle_takes_its_civilization_from_the_fortress_and_its_name_from_the_core) {
    NpcRequest r;
    r.seed = 3;
    r.has_castle_plans = true;
    r.settlements.push_back(settlement("core-city", "Frosnord"));
    NpcFortress fortress;
    fortress.id = "fortress-4";
    fortress.node = 2048;
    fortress.population_profile = "human_cold";
    fortress.core_id = 0;  // an INDEX into the settlement list, not a uid
    r.fortresses.push_back(fortress);
    NpcCastlePlan castle;
    castle.fortress_id = "fortress-4";
    castle.plots.push_back(plot("plot-0", "building.keep", 6));
    r.castle_plans.push_back(castle);

    const NpcRoster roster = generate_npc_roster(r);
    check::is_true(roster.ok, "an ok roster: " + roster.error);
    check::equal(roster.sites.size(), std::size_t(1), "one fortress");
    check::equal(roster.sites[0].uid, std::string("fortress-node-2048"), "keyed on its node");
    check::equal(roster.sites[0].name, std::string("the fortress above Frosnord"), "named for its core");
    check::equal(*roster.sites[0].civilization_id, std::string("human_cold"),
                 "the fortress's population profile as its civilization");
    check::equal(*roster.sites[0].parent_race_id, std::string("human"),
                 "and the core's parent race for its tongue");
    check::is_true(roster.summary.total > 0, "with a garrison");

    // A core_id past the end of the list leaves the name at its bare fallback.
    NpcRequest lost = r;
    lost.fortresses[0].core_id = 99;
    const NpcRoster bare = generate_npc_roster(lost);
    check::equal(bare.sites[0].name, std::string("the fortress"), "the fallback name");
}

TEST(a_short_name_cuts_the_age_suffix_and_keeps_the_rest_of_the_name) {
    check::equal(npc_short_name("Bargdorn (Age 2)"), std::string("Bargdorn"), "the age cut");
    check::equal(npc_short_name("Bargdorn"), std::string("Bargdorn"), "a bare name untouched");
    check::equal(npc_short_name(""), std::string(""), "an empty name");
    // The retired ' City' strip is NOT carried: heritage names cities now, so a name that
    // ends in those characters IS the name and cutting it would rename the place.
    check::equal(npc_short_name("Harbour City"), std::string("Harbour City"),
                 "a real name ending in City kept whole");
}

// ---------------------------------------------------------------------------
// Stores, stables and the appearance baseline: the owner's rulings of 2026-09-23.
// ---------------------------------------------------------------------------

namespace {

NpcPlot named_plot(const std::string& id, const std::string& building, int workers, const std::string& name) {
    NpcPlot p = plot(id, building, workers);
    p.name = name;
    return p;
}

// A market quarter: every store a city keeps, a smithy that keeps none, and a stable.
NpcRequest market_quarter() {
    NpcRequest r = one_city();
    NpcCityPlan& city = r.city_plans[0];
    city.plots.clear();
    city.plots.push_back(named_plot("plot-0", "building.granary", 4, "Granary"));
    city.plots.push_back(named_plot("plot-1", "building.warehouse", 5, "Winter supply warehouse"));
    city.plots.push_back(named_plot("plot-2", "building.root_cellar", 1, "Cool store and root cellar"));
    city.plots.push_back(named_plot("plot-3", "building.smithy", 2, "Smithy"));
    city.plots.push_back(named_plot("plot-4", "building.stable", 3, "Stable and farrier yard"));
    return r;
}

const NpcLifeSpan* span_of(const std::vector<NpcLifeSpan>& spans, std::string_view id) {
    for (const NpcLifeSpan& span : spans) {
        if (span.id == id) return &span;
    }
    return nullptr;
}

const NpcAgeBand* band_of(const NpcLifeSpan& span, std::string_view id) {
    for (const NpcAgeBand& band : span.bands) {
        if (band.id == id) return &band;
    }
    return nullptr;
}

bool same_appearance(const NpcAppearance& a, const NpcAppearance& b) {
    return a.gender == b.gender && a.age_band == b.age_band && a.age_years == b.age_years &&
           a.civilization_id == b.civilization_id && a.parent_race_id == b.parent_race_id;
}

}  // namespace

TEST(one_storekeeper_post_serves_every_store_and_no_store_keeper_post_remains) {
    // The reference's two spellings were one job. The merged post is told apart by the
    // store it keeps rather than by its title.
    std::set<std::string_view> stores;
    for (const NpcBuilding& building : npc_buildings()) {
        for (const NpcRole& role : building.roles) {
            check::is_true(role.role != "store_keeper", std::string(building.id) + " to staff no store_keeper");
            if (role.role == "storekeeper") stores.insert(building.id);
        }
    }
    check::is_true(stores == std::set<std::string_view>{"building.granary", "building.hamlet_food_store",
                                                        "building.root_cellar", "building.warehouse"},
                   "the four stores, and only they, to keep a storekeeper");
    check::is_true(npc_role_verbed("storekeeper"), "the merged post to carry verbs");
    check::is_true(!npc_role_verbed("store_keeper"), "and the retired one none");
}

TEST(a_storekeeper_names_the_store_it_keeps_and_everyone_names_the_building_they_work_in) {
    const NpcRoster roster = generate_npc_roster(market_quarter());
    check::is_true(roster.ok, "an ok roster: " + roster.error);
    const std::map<std::string, std::string> names = {
        {"plot-0", "Granary"}, {"plot-1", "Winter supply warehouse"}, {"plot-2", "Cool store and root cellar"},
        {"plot-3", "Smithy"},  {"plot-4", "Stable and farrier yard"}};
    std::map<std::string, int> keepers;
    for (const NpcPerson& person : roster.people) {
        check::is_true(person.workplace.has_value(), person.uid + " to name a workplace");
        check::equal(*person.workplace, names.at(person.plot_id), person.uid + "'s workplace, its plot's own name");
        if (person.post == "storekeeper") {
            check::is_true(person.store.has_value(), person.uid + " to name its store");
            check::equal(*person.store, std::string(npc_short_building(*person.building_id)),
                         person.uid + "'s store, the building it stands in");
            keepers[*person.store] += 1;
        } else {
            check::is_true(!person.store.has_value(), person.uid + " to keep no store");
        }
    }
    check::is_true(keepers == std::map<std::string, int>{{"granary", 2}, {"root_cellar", 1}, {"warehouse", 2}},
                   "two granary keepers, two warehouse keepers and one cellar keeper");

    // A plot the plan left unnamed leaves the workplace absent rather than inventing one.
    NpcRequest unnamed = market_quarter();
    unnamed.city_plans[0].plots[3].name.clear();
    const NpcRoster bare = generate_npc_roster(unnamed);
    int smiths = 0;
    for (const NpcPerson& person : bare.people) {
        if (person.plot_id != "plot-3") continue;
        ++smiths;
        check::is_true(!person.workplace.has_value(), person.uid + " to have no invented workplace");
    }
    check::equal(smiths, 2, "both smiths checked");
}

TEST(the_stable_s_senior_post_is_a_horse_keeper_who_sells_mounts_and_runs_carriages) {
    const NpcBuilding* stable = npc_building("building.stable");
    check::is_true(stable != nullptr, "a stable roster");
    check::equal(std::string(stable->roles[0].role), std::string("horse_keeper"),
                 "the horse keeper first, because the first row is the senior post");
    check::equal(stable->roles[0].target, 1, "one of them");
    check::is_true(npc_role_verbed("horse_keeper"), "a horse keeper who can give quests");

    const auto services = npc_services("horse_keeper");
    check::is_true(std::vector<std::string_view>(services.begin(), services.end()) ==
                       std::vector<std::string_view>{"mounts", "carriages"},
                   "mounts and carriages");
    check::is_true(npc_services("smith").empty(), "a smith to offer no service");
    check::is_true(npc_services("nonsense").empty(), "an unknown post none");

    const NpcRoster roster = generate_npc_roster(market_quarter());
    int keepers = 0;
    for (const NpcPerson& person : roster.people) {
        if (person.post != "horse_keeper") continue;
        ++keepers;
        check::equal(person.uid, std::string("npc-surface-city-1-100-human_heartland-stable-0"),
                     "the horse keeper holding the stable's senior post");
    }
    check::equal(keepers, 1, "one horse keeper to one stable");
}

TEST(every_city_program_queues_one_stable_that_no_condition_can_block) {
    // Every city keeps horses. The row used to wait on `working_animals`, which the city
    // planner never sets, so no city ever built one.
    int programs = 0;
    for (const CivilizationPresets& civ : civilization_presets()) {
        for (const SettlementBlock block :
             {SettlementBlock::small_city, SettlementBlock::medium_city, SettlementBlock::capital_city}) {
            int stables = 0;
            for (const PresetRow& row : settlement_preset(civ.id, block).buildings) {
                if (row.structure().id != "building.stable") continue;
                ++stables;
                check::equal(row.count, 1, std::string(civ.id) + " to build one stable");
                check::is_true(row.placement_conditions.empty(), std::string(civ.id) + "'s stable to wait on nothing");
                check::is_true(row.structure().priority == StructurePriority::core, "a core building, queued high");
            }
            check::equal(stables, 1, std::string(civ.id) + " to queue exactly one stable row");
            ++programs;
        }
    }
    check::equal(programs, 36, "twelve peoples, three city sizes");
}

TEST(a_life_span_rounds_each_onset_once_so_its_bands_meet_without_a_gap) {
    // Each onset rounds half up to a whole year FIRST and the bands are cut from those
    // years, so no year belongs to two bands and none to neither. Young ends halfway from
    // the adult onset to the elder one.
    check::close(npc_young_to_middle(), 0.5, 1e-12, "the young-to-middle fraction");
    const NpcLifeSpan span = npc_life_span("somefolk", "human", NpcLifeStages{14.6, 45.5, 80.0});
    check::equal(span.bands.size(), std::size_t(3), "three bands");
    const std::vector<std::tuple<std::string, int, int>> expected = {
        {"young", 15, 30}, {"middle", 31, 45}, {"old", 46, 80}};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto& [id, from, to] = expected[i];
        check::equal(std::string(span.bands[i].id), id, "the band order");
        check::equal(span.bands[i].from, from, id + " from");
        check::equal(span.bands[i].to, to, id + " to");
    }
    check::equal(span.parent_race_id, std::string("human"), "the parent race carried");

    // A life too short to hold three bands is a loud failure, never an empty band.
    check::throws([] { npc_life_span("mayflies", "", NpcLifeStages{10.0, 11.0, 12.0}); },
                  "a life with no room for a middle band");
}

TEST(a_person_s_gender_and_age_come_from_their_own_stream) {
    const NpcRequest request = one_city();
    const NpcRoster roster = generate_npc_roster(request);
    const NpcLifeSpan* span = span_of(roster.life_spans, "human_heartland");
    check::is_true(span != nullptr, "the people's span published");
    for (const NpcPerson& person : roster.people) {
        check::is_true(person.appearance.has_value(), person.uid + " to have an appearance");
        const NpcAppearance& a = *person.appearance;
        check::is_true(a.gender == "female" || a.gender == "male", person.uid + "'s gender, got " + std::string(a.gender));
        const NpcAgeBand* band = band_of(*span, a.age_band);
        check::is_true(band != nullptr, person.uid + "'s band, got " + std::string(a.age_band));
        check::is_true(band->from <= a.age_years && a.age_years <= band->to,
                       person.uid + "'s age inside its band, got " + std::to_string(a.age_years));
        check::equal(a.civilization_id, std::string("human_heartland"), "drawn for the site's people");
        check::equal(a.parent_race_id, std::string("human"), "of the human race");
    }

    // Keyed on the uid alone: a building joining the city moves nobody already in it, and
    // one leaving from the FRONT of the order moves nobody after it -- which a single stream
    // walked in roster order could not survive.
    NpcRequest grown = request;
    grown.city_plans[0].plots.push_back(plot("plot-9", "building.smithy", 2));
    const NpcRoster more = generate_npc_roster(grown);
    check::equal(more.summary.total, roster.summary.total + 2, "two more people");
    for (const NpcPerson& person : roster.people) {
        const NpcPerson* same = person_by_uid(more, person.uid);
        check::is_true(same != nullptr && same->appearance.has_value(), person.uid + " still there");
        check::is_true(same_appearance(*same->appearance, *person.appearance), person.uid + " unmoved by a newcomer");
    }
    NpcRequest shrunk = request;
    shrunk.city_plans[0].plots.erase(shrunk.city_plans[0].plots.begin());  // the guildhall, plot-0
    const NpcRoster fewer = generate_npc_roster(shrunk);
    check::equal(fewer.summary.total, roster.summary.total - 2, "two fewer people");
    for (const NpcPerson& person : fewer.people) {
        const NpcPerson* before = person_by_uid(roster, person.uid);
        check::is_true(before != nullptr && before->appearance.has_value(), person.uid + " there before");
        check::is_true(same_appearance(*person.appearance, *before->appearance), person.uid + " unmoved by a departure");
    }

    // ...and the stream is the seed's: another world draws other people.
    NpcRequest other = request;
    other.seed = 43;
    const NpcRoster elsewhere = generate_npc_roster(other);
    int differ = 0;
    for (const NpcPerson& person : roster.people) {
        const NpcPerson* there = person_by_uid(elsewhere, person.uid);
        check::is_true(there != nullptr && there->appearance.has_value(), person.uid + " in the other world");
        if (!same_appearance(*there->appearance, *person.appearance)) ++differ;
    }
    check::is_true(differ > 0, "another seed to draw other people");
}

TEST(a_people_with_no_life_leaves_the_appearance_absent_and_counts_it) {
    NpcRequest request = one_city();
    request.entities.clear();
    const NpcRoster roster = generate_npc_roster(request);
    check::is_true(roster.ok, "an ok roster: an ageless people is not a failure");
    check::equal(roster.summary.notes.appearance_unresolved, 20, "each person counted");
    for (const NpcPerson& person : roster.people) {
        check::is_true(!person.appearance.has_value(), person.uid + " to have no guessed age");
    }
    check::is_true(roster.life_spans.empty(), "and no span published for it");
}

TEST(the_gender_and_band_weights_hold_across_a_garrison) {
    NpcRequest request = one_city();
    request.city_plans[0].plots.clear();
    for (int i = 0; i < 60; ++i) {
        request.city_plans[0].plots.push_back(plot("plot-" + std::to_string(i), "building.barracks", 16));
    }
    const NpcRoster roster = generate_npc_roster(request);
    check::equal(roster.summary.total, 960, "sixty barracks of sixteen");
    std::map<std::string_view, int> genders;
    std::map<std::string_view, int> bands;
    for (const NpcPerson& person : roster.people) {
        genders[person.appearance->gender] += 1;
        bands[person.appearance->age_band] += 1;
    }
    const auto share = [](const std::map<std::string_view, int>& counts, std::string_view key) {
        const auto found = counts.find(key);
        return found == counts.end() ? 0.0 : static_cast<double>(found->second) / 960.0;
    };
    // Genders 1:1 and young, middle and old 2:2:1, each well past sampling error.
    check::between(share(genders, "female"), 0.44, 0.56, "half women");
    check::between(share(genders, "male"), 0.44, 0.56, "half men");
    check::between(share(bands, "young"), 0.34, 0.46, "two fifths young");
    check::between(share(bands, "middle"), 0.34, 0.46, "two fifths middle-aged");
    check::between(share(bands, "old"), 0.14, 0.26, "one fifth old");
}

TEST(the_roster_publishes_each_parent_race_s_life_beside_each_people_s) {
    NpcRequest request = one_city();
    NpcCivilizationEntity gnome;
    gnome.id = "gnome";
    gnome.parent_race_id = "dwarf";
    gnome.life = NpcLifeStages{138.0, 438.0, 750.0};
    NpcCivilizationEntity ageless;
    ageless.id = "ageless";
    ageless.parent_race_id = "dwarf";
    request.entities.push_back(gnome);
    request.entities.push_back(ageless);
    request.parent_races.push_back({"human", NpcLifeStages{15.0, 46.0, 80.0}});
    request.parent_races.push_back({"dwarf", NpcLifeStages{43.0, 257.0, 500.0}});
    const NpcRoster roster = generate_npc_roster(request);
    check::is_true(roster.ok, "an ok roster: " + roster.error);
    check::equal(roster.life_spans.size(), std::size_t(2), "a span for each people with a life, none for the ageless");
    check::equal(roster.life_spans[0].id, std::string("gnome"), "peoples sorted by id");
    check::equal(roster.life_spans[1].id, std::string("human_heartland"), "peoples sorted by id");
    check::equal(roster.life_spans[0].parent_race_id, std::string("dwarf"), "the gnome's race named");
    check::equal(band_of(roster.life_spans[0], "old")->to, 750, "a gnome living to 750");
    check::equal(roster.parent_race_life_spans.size(), std::size_t(2), "each parent race");
    check::equal(roster.parent_race_life_spans[0].id, std::string("dwarf"), "races sorted by id");
    check::is_true(roster.parent_race_life_spans[0].parent_race_id.empty(), "a race naming no race above it");
    check::equal(band_of(roster.parent_race_life_spans[0], "old")->to, 500, "a dwarf living to 500");
}

// ---------------------------------------------------------------------------
// The same, in a generated world.
// ---------------------------------------------------------------------------

namespace {

const World& world() {
    static const World w = [] {
        Request r;
        r.seed = 42;
        r.size = 33;
        r.phase = 16;
        return generate(resolve(r));
    }();
    return w;
}

}  // namespace

TEST(in_a_world_every_city_keeps_one_horse_keeper_and_an_unplaced_stable_says_why) {
    // Ruled 2026-09-23: a horse keeper in every city. Ruled 2026-09-26 ("finish this"): a
    // stable the plan could not fit is still the city's, listed unplaced for the game to place,
    // and staffed at a workplace marked unplaced.
    const World& w = world();
    check::is_true(w.npcs.ok, "an ok roster: " + w.npcs.error);
    std::map<std::string, const NpcPerson*> keeper_at;  // "<site>|<plot>"
    for (const NpcPerson& person : w.npcs.people) {
        if (person.post != "horse_keeper") continue;
        check::equal(person.site_kind.value_or(""), std::string("city"), person.uid + " to keep a city's horses");
        check::is_true(keeper_at.emplace(*person.site_uid + "|" + person.plot_id, &person).second,
                       person.uid + " to be its stable's only horse keeper");
    }
    int built = 0;
    int placed = 0;
    for (const CityPlan& city : w.city_plans.cities) {
        if (city.status == "unbuildable") continue;
        ++built;
        std::vector<const Plot*> stables;
        for (const Plot& p : city.plots) {
            if (p.building_id == "building.stable") stables.push_back(&p);
        }
        check::is_true(stables.size() <= 1, city.city_uid + " to build at most one stable");
        if (stables.size() == 1) {
            ++placed;
            const auto keeper = keeper_at.find(city.city_uid + "|" + stables[0]->id);
            check::is_true(keeper != keeper_at.end(), city.city_uid + "'s stable to keep a horse keeper");
            check::equal(keeper->second->workplace.value_or(""), std::string(stables[0]->name),
                         "the horse keeper working in the stable itself");
            check::is_true(keeper->second->workplace_placed, "a placed workplace");
            continue;
        }
        // A city that could not fit its stable says so, and for want of room: never for want
        // of a condition, which is how no city built one before the ruling.
        bool reported = false;
        for (const Unplaced& u : city.unplaced) {
            if (u.building_id != "building.stable") continue;
            reported = true;
            check::equal(u.reason, std::string("No terrain-safe plot with road access remaining"),
                         city.city_uid + "'s stable lost for want of room");
        }
        check::is_true(reported, city.city_uid + " to report the stable it could not place");
        const auto keeper = keeper_at.find(city.city_uid + "|unplaced-building.stable");
        check::is_true(keeper != keeper_at.end(), city.city_uid + " to keep a horse keeper all the same");
        if (keeper == keeper_at.end()) continue;
        check::is_true(!keeper->second->workplace_placed, "at a workplace marked unplaced");
        check::equal(keeper->second->workplace.value_or(""), std::string("Stable and farrier yard"),
                     "the stable they keep, by name");
    }
    check::is_true(placed < built, "the premise: a city that could not fit its stable");
    check::equal(static_cast<int>(keeper_at.size()), built, "a horse keeper for every city, none besides");
    // Size 33 is cramped -- every city there is partial -- and most still keep horses.
    check::is_true(placed * 2 > built,
                   "most cities to place their stable, got " + std::to_string(placed) + " of " + std::to_string(built));
}

TEST(in_a_world_every_storekeeper_stands_in_the_store_it_names) {
    const World& w = world();
    std::map<std::string, const std::vector<Plot>*> plans;  // by the roster's site uid
    for (const CityPlan& city : w.city_plans.cities) plans.emplace(city.city_uid, &city.plots);
    for (const HamletPlan& hamlet : w.hamlet_plans.hamlets) {
        plans.emplace("hamlet-node-" + std::to_string(hamlet.node), &hamlet.plots);
    }
    std::map<std::string, int> by_store;
    for (const NpcPerson& person : w.npcs.people) {
        check::is_true(person.post != "store_keeper", person.uid + " to hold no retired post");
        if (person.post != "storekeeper") {
            check::is_true(!person.store.has_value(), person.uid + " to keep no store");
            continue;
        }
        const std::string store = person.store.value_or("");
        check::equal(store, std::string(npc_short_building(person.building_id.value_or(""))),
                     person.uid + " naming the store it stands in");
        by_store[store] += 1;
        const auto plan = plans.find(person.site_uid.value_or(""));
        check::is_true(plan != plans.end(), person.uid + "'s site to have a plan");
        const Plot* at = nullptr;
        for (const Plot& p : *plan->second) {
            if (p.id == person.plot_id) at = &p;
        }
        check::is_true(at != nullptr, person.uid + "'s plot to stand in its site's plan");
        check::equal(std::string(at->building_id), *person.building_id, person.uid + "'s plot to be that store");
        check::equal(person.workplace.value_or(""), std::string(at->name), person.uid + "'s workplace, that store's name");
    }
    for (const std::string store : {"granary", "warehouse", "root_cellar", "hamlet_food_store"}) {
        check::is_true(by_store[store] > 0, "some " + store + " keepers in the world");
    }
}

TEST(in_a_world_everyone_has_an_age_inside_their_people_s_band) {
    const World& w = world();
    check::equal(w.npcs.summary.notes.appearance_unresolved, 0, "everyone aged");
    std::map<std::string, const NpcLifeSpan*> spans;
    for (const NpcLifeSpan& span : w.npcs.life_spans) spans.emplace(span.id, &span);
    std::map<std::string, std::string> site_people;
    for (const NpcSite& site : w.npcs.sites) site_people.emplace(site.uid, site.civilization_id.value_or(""));
    std::map<std::string, std::string> hero_people;
    for (const Hero& hero : w.heroes.people) hero_people.emplace(hero.uid, hero.civilization_id);
    std::set<std::string> dreads;
    for (const HeroDread& dread : w.heroes.dreads) dreads.insert(dread.uid);

    std::set<std::string_view> genders;
    std::set<std::string_view> bands;
    int cast = 0;
    int monsters = 0;
    for (const NpcPerson& person : w.npcs.people) {
        // A dread is a monster: no gender, no age, by the owner's ruling.
        if (person.is_cast && dreads.count(person.hero_uid) != 0) {
            check::is_true(!person.appearance.has_value(), person.uid + " a monster, with no gender or age");
            ++monsters;
            continue;
        }
        check::is_true(person.appearance.has_value(), person.uid + " to have an appearance");
        const NpcAppearance& a = *person.appearance;
        const auto span = spans.find(a.civilization_id);
        check::is_true(span != spans.end(), person.uid + "'s people to have a span");
        const NpcAgeBand* band = band_of(*span->second, a.age_band);
        check::is_true(band != nullptr && band->from <= a.age_years && a.age_years <= band->to,
                       person.uid + " aged inside its band");
        check::equal(a.parent_race_id, span->second->parent_race_id, person.uid + "'s race, its people's");
        // A post is aged as its site's people; a hero as their own.
        const std::string& people = person.is_cast ? hero_people.at(person.hero_uid) : site_people.at(*person.site_uid);
        check::equal(a.civilization_id, people, person.uid + " aged as its own people");
        if (person.is_cast) ++cast;
        genders.insert(a.gender);
        bands.insert(a.age_band);
    }
    check::is_true(cast > 0, "the cast aged too");
    check::equal(monsters, static_cast<int>(dreads.size()), "every dread in the roster, none of them aged");
    check::equal(genders.size(), std::size_t(2), "both genders");
    check::equal(bands.size(), std::size_t(3), "all three bands");
}

TEST(the_owner_s_lifespans_reach_every_people_through_its_race) {
    // A human lives to 80, an elf to 1000, a dwarf to 500 and a gnome to 750. The stage
    // onsets scale with the life, and every other people keeps its authored ratio to its
    // race: the cold and rainforest humans, the hill dwarves and the tidekin.
    struct Life {
        std::string parent;
        double adult, elder, max;
    };
    const std::map<std::string, Life> races = {
        {"dwarf", {"", 43, 257, 500}}, {"elf", {"", 111, 667, 1000}}, {"human", {"", 15, 46, 80}}};
    const std::map<std::string, Life> peoples = {
        {"dwarf", {"dwarf", 43, 257, 500}},          {"elf", {"elf", 111, 667, 1000}},
        {"frosthold_dwarf", {"dwarf", 43, 257, 500}}, {"gnome", {"dwarf", 138, 438, 750}},
        {"hill_dwarf", {"dwarf", 43, 214, 400}},      {"human_cold", {"human", 15, 42, 72}},
        {"human_desert", {"human", 15, 46, 80}},      {"human_heartland", {"human", 15, 46, 80}},
        {"human_large_island", {"human", 15, 46, 80}}, {"human_maritime", {"human", 15, 46, 80}},
        {"human_rainforest", {"human", 13, 38, 66}},  {"tidekin", {"elf", 111, 500, 778}}};
    const World& w = world();
    const auto compare = [](const std::vector<NpcLifeSpan>& spans, const std::map<std::string, Life>& expected,
                            const std::string& what) {
        check::equal(spans.size(), expected.size(), what + " published");
        for (const NpcLifeSpan& span : spans) {
            const auto found = expected.find(span.id);
            check::is_true(found != expected.end(), what + ": an unexpected " + span.id);
            const Life& life = found->second;
            check::equal(span.parent_race_id, life.parent, span.id + "'s race");
            check::close(span.life.adult, life.adult, 1e-9, span.id + "'s adult onset");
            check::close(span.life.elder, life.elder, 1e-9, span.id + "'s elder onset");
            check::close(span.life.max, life.max, 1e-9, span.id + "'s longest life");
            check::equal(band_of(span, "old")->to, static_cast<int>(life.max), span.id + "'s old band ending with its life");
        }
    };
    compare(w.npcs.parent_race_life_spans, races, "the parent races");
    compare(w.npcs.life_spans, peoples, "the peoples");
}

// ---------------------------------------------------------------------------
// Dreads in the roster (2026-09-23).
// ---------------------------------------------------------------------------

TEST(a_dread_joins_the_cast_at_its_lair_with_no_gender_or_age_and_nothing_unresolved) {
    NpcRequest request = one_city();
    request.heroes_ok = true;
    NpcCastMember hero;
    hero.uid = "hero-sovereign-1";
    hero.display_name = "Aldric Who Holds Bargdorn";
    hero.role = "sovereign";
    hero.status = "living";
    hero.home_uid = "surface-city-1-100-human_heartland";
    hero.civilization_id = "human_heartland";
    NpcCastMember dread;
    dread.uid = "dread-nest-red-dragons-500";
    dread.display_name = "Red dragons, Bargdorn's Doom";
    dread.role = "dread";
    dread.status = "living";
    dread.presence_uid = "nest-red-dragons-500";
    dread.presence_site_kind = "nest";
    dread.monster = true;
    request.cast = {hero, dread};
    NpcPlaceRow lair;
    lair.uid = "nest-red-dragons-500";
    lair.node = 500;
    lair.x = 12.0;
    lair.z = 30.0;
    request.place_nests.push_back(lair);

    const NpcRoster roster = generate_npc_roster(request);
    check::is_true(roster.ok, "an ok roster: " + roster.error);
    const NpcPerson* monster = person_by_uid(roster, "npc-dread-nest-red-dragons-500");
    check::is_true(monster != nullptr, "the dread in the roster under the prefixed uid");
    check::equal(monster->post, std::string("dread"), "its role as its post");
    check::is_true(monster->alive && monster->important, "living, and never capped out");
    check::is_true(!monster->appearance.has_value(), "no gender and no age: a monster");
    check::is_true(!monster->site_uid.has_value(), "standing at no planned site");
    check::is_true(monster->presence_node.has_value() && *monster->presence_node == 500, "but at its lair");
    const NpcPerson* sovereign = person_by_uid(roster, "npc-hero-sovereign-1");
    check::is_true(sovereign != nullptr && sovereign->appearance.has_value(), "while the hero beside it is aged");
    check::equal(roster.summary.notes.appearance_unresolved, 0, "and a monster is nothing unresolved");
}

TEST(in_a_world_every_dread_stands_in_the_roster_and_a_living_one_at_its_lair) {
    const World& w = world();
    check::is_true(!w.heroes.dreads.empty(), "beasts to have razed a city and named a dread, or nothing is tested");
    std::map<std::string, const NestSite*> lairs;
    for (const NestSite& site : w.beast_nests.sites) lairs.emplace(site.id, &site);
    int living = 0;
    for (const HeroDread& dread : w.heroes.dreads) {
        const NpcPerson* row = person_by_uid(w.npcs, std::string(kNpcUidPrefix) + dread.uid);
        check::is_true(row != nullptr, dread.uid + " in the roster");
        check::equal(row->alive, dread.status == "living", dread.uid + " alive exactly when living");
        check::is_true(!row->appearance.has_value(), dread.uid + " unaged");
        if (dread.status != "living") continue;
        ++living;
        const auto lair = lairs.find(dread.nest_id);
        check::is_true(lair != lairs.end(), dread.uid + "'s lair still standing");
        check::is_true(row->presence_node.has_value() && *row->presence_node == lair->second->node,
                       dread.uid + " placed at its lair");
    }
    check::is_true(living > 0, "a living dread, or the lair is not tested");
}
