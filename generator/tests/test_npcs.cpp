// The working population a finished world already implies.
#include "check.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "fwg/npcs.hpp"

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
    check::equal(npc_verbs().size(), std::size_t(10), "the canonical verbs");
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
