// Villain invariants.
//
// Built against a hand-made world rather than a generated one, because every property here
// is about anchors, tie-breaks, a decay recurrence and an ordering rule -- and a generated
// world would make each of them depend on eleven stages upstream. The world is small and
// explicit so that when a test fails the thing that moved is in the test.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "fwg/villains.hpp"

using namespace fwg;

namespace {

constexpr double kRadius = 1000.0;
constexpr double kSpacing = 50.0;

// A unit direction `degrees` away from the pole, in the x-y plane. One degree here is
// kRadius * pi/180 = 17.45 m, which keeps every authored distance readable.
Vec3 lean(double degrees) {
    const double a = degrees * 3.14159265358979323846 / 180.0;
    return {std::sin(a), std::cos(a), 0.0};
}

struct Bench {
    MagicReport magic;
    std::vector<VillainCity> cities;
    std::vector<VillainCulture> cultures;
    std::vector<VillainThreatReport> reports;
    std::vector<VillainRuin> ruins;
    std::optional<VillainBlock> block;

    double radius = kRadius;
    double spacing = kSpacing;
    int age = 1;
    double rise = 1.0;
    double hold = 0.7;
    double density = 3.0;

    VillainRequest request() const {
        VillainRequest r;
        r.rise = rise;
        r.hold = hold;
        r.density = density;
        r.age = age;
        r.globe_radius_m = radius;
        r.settlement_spacing_m = spacing;
        r.cities = cities;
        r.cultures = cultures;
        r.reports = reports;
        r.ruins = ruins;
        r.magic = &magic;
        r.villains = block;
        return r;
    }

    void city(const std::string& uid, int id, int node, const Vec3& where,
              const std::string& profile = "human_heartland", double people = 1000.0) {
        VillainCity c;
        c.uid = uid;
        c.id = id;
        c.node = node;
        c.direction = where;
        c.population_profile = profile;
        c.population_estimate = people;
        cities.push_back(c);
    }

    void culture(const std::string& id, const std::vector<int>& city_ids) {
        cultures.push_back({id, city_ids});
    }

    void report(const std::string& uid, double threat, double risk = 0.0, double hunger = 0.0,
                double war = 0.0, double nest = 0.0, double ley = 0.0) {
        reports.push_back({uid, threat, risk, hunger, war, nest, ley});
    }

    void network(const std::string& school, const std::vector<std::pair<std::string, Vec3>>& nodes) {
        LeyNetwork net;
        net.name = school;
        for (const auto& [id, where] : nodes) net.nodes.push_back({id, where, 1.0});
        magic.networks[school] = net;
        magic.enabled = true;
    }
};

// Two cultures, one ley node near each, and an assessment that says one is worse.
Bench two_regions() {
    Bench b;
    b.city("city-a", 1, 11, lean(0.0));
    b.city("city-b", 2, 22, lean(40.0));
    b.culture("culture-north", {1});
    b.culture("culture-south", {2});
    b.network("earth", {{"earth-node-0", lean(1.0)}, {"earth-node-1", lean(41.0)}});
    b.report("city-a", 1.0);
    b.report("city-b", 0.2);
    return b;
}

const Villain* find_villain(const VillainBlock& block, const std::string& region) {
    for (const Villain& villain : block.people) {
        if (villain.region == region) return &villain;
    }
    return nullptr;
}

double tier_of(const VillainBlock& block, const std::string& anchor) {
    const auto it = block.tiers.find(anchor);
    return it != block.tiers.end() ? it->second : -1.0;
}

std::vector<std::string> anchors_of(const std::vector<VillainRegion>& regions) {
    std::vector<std::string> out;
    for (const VillainRegion& region : regions) out.push_back(region.anchor);
    return out;
}

}  // namespace

// --- the arithmetic that everything else stands on -------------------------

TEST(the_bands_are_ordered_so_that_nothing_flickers_across_the_line) {
    // Hysteresis is the whole reason a reign is long once established. Asserted here rather
    // than described, because a stalled pretender at or above the seating band would seat
    // on the next age without the world changing at all.
    check::is_true(kStalledTier < kSuperTier, "a pretender to sit strictly under the band");
    check::is_true(VillainRequest{}.hold < kSuperTier, "the band to stay to be under the band to rise");
    check::close(kSeatTier, 1.5, 1e-12, "the seat/village boundary");
}

TEST(turmoil_carries_the_forward_looking_half_and_clamps_at_one) {
    check::close(turmoil({"c", 0.4, 0.5, 0.5, 0, 0, 0}), 0.7, 1e-12, "threat plus .3 of each outlook");
    check::close(turmoil({"c", 0.0, 0.0, 0.0, 0, 0, 0}), 0.0, 1e-12, "a quiet city");
    // A maximally tense city saturates: regional_threat is already 0..1 on its own.
    check::close(turmoil({"c", 1.0, 1.0, 1.0, 0, 0, 0}), 1.0, 1e-12, "the clamp");
    check::close(turmoil({"c", -5.0, 0.0, 0.0, 0, 0, 0}), 0.0, 1e-12, "the floor");
}

TEST(dominant_pressure_breaks_an_exact_tie_alphabetically) {
    VillainThreatReport tied{"c", 0, 0, 0, 0.5, 0.5, 0.5};
    VillainThreatReport war{"c", 0, 0, 0, 0.9, 0.5, 0.5};
    VillainThreatReport nest{"c", 0, 0, 0, 0.1, 0.9, 0.5};
    VillainThreatReport quiet{"c", 0, 0, 0, 0.0, 0.0, 0.0};

    // ley beats nest beats war on an exact tie, because the reference keeps the first
    // maximum of an alphabetically sorted key list. A std::sort or an unordered map here
    // picks a different growth function, which a hero generator then reads.
    check::equal(dominant_pressure({&tied}), std::string("ley"), "a three-way tie");
    check::equal(dominant_pressure({&war}), std::string("war"), "war leading outright");
    check::equal(dominant_pressure({&nest}), std::string("nest"), "nests leading outright");
    check::equal(dominant_pressure({&quiet}), std::string(""), "nothing pressing at all");
    check::equal(dominant_pressure({}), std::string(""), "no reports at all");
    // SUMMED across the region, not decided city by city: this pair's totals are war 1.6
    // against nest 0.9, although the first city on its own would say nest.
    VillainThreatReport warring{"c", 0, 0, 0, 1.5, 0.0, 0.0};
    VillainThreatReport haunted{"c", 0, 0, 0, 0.1, 0.9, 0.0};
    check::equal(dominant_pressure({&haunted}), std::string("nest"), "one city on its own");
    check::equal(dominant_pressure({&haunted, &warring}), std::string("war"), "totals over a region");
}

TEST(the_ceiling_is_one_villain_per_density_regions_rounded_up) {
    check::equal(villain_ceiling(0, 3.0), 0, "no regions, no villains");
    check::equal(villain_ceiling(1, 3.0), 1, "one region still allows one");
    check::equal(villain_ceiling(9, 3.0), 3, "nine regions at three each");
    check::equal(villain_ceiling(10, 3.0), 4, "ten rounds up, not down");
    check::equal(villain_ceiling(2, 12.0), 1, "the floor of one");
    // Density below one would otherwise multiply the ceiling rather than divide it.
    check::equal(villain_ceiling(4, 0.25), 4, "a density under one is clamped to one");
}

TEST(a_fallen_claim_fades_to_an_exact_zero_below_the_floor) {
    check::close(claim_influence(0), 0.6, 1e-12, "what a claim keeps at the fall");
    check::close(claim_influence(1), 0.36, 1e-12, "one age later");
    check::close(claim_influence(2), 0.216, 1e-12, "two");
    check::close(claim_influence(3), 0.1296, 1e-12, "three");
    check::close(claim_influence(4), 0.07776, 1e-12, "four");
    // EXACTLY zero, not a vanishing fraction, so a consumer scaling by this stops being
    // nudged instead of being nudged imperceptibly forever.
    check::equal(claim_influence(5), 0.0, "five, below the floor");
    check::equal(claim_influence(50), 0.0, "and it stays there");
    // A guard against a fell age ahead of the current one, not a decay step.
    check::equal(claim_influence(-1), 1.0, "a fall in the future");
}

// --- regions and anchors ---------------------------------------------------

TEST(a_region_anchors_on_a_ley_node_and_never_on_its_culture_id) {
    const Bench bench = two_regions();
    const VillainRequest request = bench.request();
    const std::vector<VillainRegion> regions = villain_regions(request);

    check::equal(regions.size(), std::size_t{2}, "one region per culture");
    // ANCHOR-ASCENDING: this is the order the advance loop walks and the outlook reports.
    check::equal(anchors_of(regions)[0], std::string("earth-node-0"), "the first anchor");
    check::equal(anchors_of(regions)[1], std::string("earth-node-1"), "the second anchor");
    for (const VillainRegion& region : regions) {
        check::is_true(region.anchor != region.id, "the anchor not to be the culture id");
        check::equal(region.school, std::string("earth"), "the school the anchor belongs to");
        check::is_true(region.seat != nullptr, "every produced region to have a seat");
    }
}

TEST(with_no_magic_at_all_a_region_anchors_on_its_seat_city) {
    Bench bench = two_regions();
    bench.magic.networks.clear();
    const VillainRequest request = bench.request();
    const std::vector<VillainRegion> regions = villain_regions(request);

    check::equal(regions.size(), std::size_t{2}, "the regions still exist without magic");
    check::equal(anchors_of(regions)[0], std::string("seat-city-a"), "the north seat");
    check::equal(anchors_of(regions)[1], std::string("seat-city-b"), "the south seat");
    for (const VillainRegion& region : regions) {
        check::equal(region.school, std::string(""), "no school to hold");
    }
}

TEST(a_villains_own_well_is_never_allowed_to_re_anchor_its_region) {
    Bench bench = two_regions();
    // A well sunk at the seat itself, which is always nearer than any node the world laid
    // down. Left in the running it re-anchors the region onto the holder's own work, the
    // advance pass looks the villain up by the old anchor and misses, and the reign is
    // stranded on the very age it began -- for every villain bound to a school god.
    bench.magic.networks["earth"].nodes.push_back({"well-villain-1-11", lean(0.0), 3.75});

    const VillainRequest request = bench.request();
    const std::vector<VillainRegion> regions = villain_regions(request);
    check::equal(regions.size(), std::size_t{2}, "two regions still");
    check::equal(anchors_of(regions)[0], std::string("earth-node-0"), "the anchor the world laid down");

    // The floor: prove the well WOULD have won. Without this the test passes on a world
    // where the well happens to be further away and asserts nothing.
    const double to_well = great_circle_m(kRadius, lean(0.0), lean(0.0));
    const double to_node = great_circle_m(kRadius, lean(0.0), lean(1.0));
    check::is_true(to_well < to_node, "the well to be strictly nearer than the real node");
}

TEST(two_regions_sharing_a_nearest_node_leave_it_to_the_more_troubled_one) {
    Bench bench;
    bench.city("city-a", 1, 11, lean(0.0));
    bench.city("city-b", 2, 22, lean(0.5));
    bench.culture("culture-calm", {1});
    bench.culture("culture-torn", {2});
    bench.network("earth", {{"earth-node-0", lean(0.25)}});
    bench.report("city-a", 0.1);
    bench.report("city-b", 0.9);

    // Both groups are produced and both resolve to the one node, so the de-duplication is
    // the only thing that can decide -- and it must, or a tier is counted into one anchor
    // twice and the region seats a villain it never concentrated for.
    check::equal(bench.cultures.size(), std::size_t{2}, "two cultures to collide");
    const VillainRequest request = bench.request();
    const std::vector<VillainRegion> regions = villain_regions(request);
    check::equal(regions.size(), std::size_t{1}, "one anchor names one region");
    check::equal(regions[0].id, std::string("culture-torn"), "the troubled culture keeps it");
    check::equal(regions[0].anchor, std::string("earth-node-0"), "the shared anchor");
}

TEST(the_seat_is_the_most_troubled_city_with_ties_broken_by_uid) {
    Bench bench;
    bench.city("city-z", 1, 11, lean(0.0));
    bench.city("city-a", 2, 22, lean(1.0));
    bench.city("city-m", 3, 33, lean(2.0));
    bench.culture("culture-one", {1, 2, 3});
    bench.network("earth", {{"earth-node-0", lean(5.0)}});
    bench.report("city-z", 0.5);
    bench.report("city-a", 0.5);
    bench.report("city-m", 0.9);

    VillainRequest request = bench.request();
    std::vector<VillainRegion> regions = villain_regions(request);
    check::equal(regions.size(), std::size_t{1}, "one region");
    check::equal(regions[0].seat->uid, std::string("city-m"), "the most troubled city");

    // Now make the tie the deciding factor, and it must fall to the lower uid rather than
    // to the order the sites happened to be listed in.
    bench.reports.clear();
    bench.report("city-z", 0.5);
    bench.report("city-a", 0.5);
    bench.report("city-m", 0.5);
    request = bench.request();
    regions = villain_regions(request);
    check::equal(regions[0].seat->uid, std::string("city-a"), "the tie broken by ascending uid");
}

TEST(a_culture_whose_cities_all_vanished_produces_no_region) {
    Bench bench = two_regions();
    bench.culture("culture-ghost", {404});
    const VillainRequest with_ghost = bench.request();
    const std::vector<VillainRegion> regions = villain_regions(with_ghost);
    check::equal(regions.size(), std::size_t{2}, "the empty group is dropped before the anchors");

    // And a world with no cultures at all is still a world with cities in it.
    bench.cultures.clear();
    const VillainRequest no_cultures = bench.request();
    const std::vector<VillainRegion> fallback = villain_regions(no_cultures);
    check::equal(fallback.size(), std::size_t{1}, "one region for the whole world");
    check::equal(fallback[0].id, std::string("world"), "the fallback group");
    check::equal(fallback[0].cities.size(), std::size_t{2}, "holding every city");
}

TEST(the_cast_is_a_reading_of_the_world_and_not_of_container_order) {
    // Villains take no random draws at all: every decision is a deterministic reading with
    // an explicit tie-break. The observable form of that is this -- permute the inputs and
    // nothing moves.
    Bench bench = two_regions();
    Bench shuffled = two_regions();
    std::reverse(shuffled.cities.begin(), shuffled.cities.end());
    std::reverse(shuffled.cultures.begin(), shuffled.cultures.end());
    std::reverse(shuffled.reports.begin(), shuffled.reports.end());

    const VillainAdvance a = advance_villains(bench.request());
    const VillainAdvance b = advance_villains(shuffled.request());
    check::is_true(a.block.has_value() && b.block.has_value(), "both to write a block");
    check::equal(a.block->tiers.size(), b.block->tiers.size(), "the same ledger size");
    check::is_true(!a.block->tiers.empty(), "a ledger with something in it");
    for (const auto& [anchor, tier] : a.block->tiers) {
        check::close(tier_of(*b.block, anchor), tier, 1e-15, "tier of " + anchor);
    }
    check::equal(a.block->people.size(), b.block->people.size(), "the same cast");
    for (std::size_t i = 0; i < a.block->people.size(); ++i) {
        check::equal(b.block->people[i].uid, a.block->people[i].uid, "cast member " + std::to_string(i));
    }
}

// --- the advance pass ------------------------------------------------------

TEST(a_zero_rate_on_a_world_that_never_had_villains_writes_no_block_at_all) {
    Bench bench = two_regions();
    bench.rise = 0.0;
    const VillainAdvance off = advance_villains(bench.request());
    check::is_true(!off.block.has_value(), "the option to be invisible in the output");
    check::equal(off.standing.size(), std::size_t{0}, "and nobody standing");

    // A world that WAS built with villains keeps its record even if the rate is turned
    // down: the switch is about what a world has, not about what it is doing this age.
    bench.block = VillainBlock{};
    const VillainAdvance kept = advance_villains(bench.request());
    check::is_true(kept.block.has_value(), "a block that exists to survive a zero rate");
    check::equal(kept.block->version, kVillainsVersion, "and to be stamped");
    check::equal(kept.block->tiers.size(), std::size_t{2}, "with a ledger row per region");
}

TEST(a_region_that_concentrates_past_the_band_seats_a_villain_on_its_anchor) {
    const Bench bench = two_regions();
    const VillainAdvance result = advance_villains(bench.request());

    check::is_true(result.block.has_value(), "a block");
    check::equal(result.standing.size(), std::size_t{1}, "exactly one villain seated");
    const Villain& villain = result.standing[0];
    check::equal(villain.region, std::string("earth-node-0"), "seated on the troubled anchor");
    check::equal(villain.uid, std::string("villain-1-11"), "uid is age and seat node");
    check::equal(villain.seat_uid, std::string("city-a"), "the most troubled city");
    check::equal(villain.school, std::string("earth"), "the school its anchor belongs to");
    check::equal(villain.held_nodes.size(), std::size_t{1}, "holding its anchor node");
    check::close(villain.tier, 1.0, 1e-12, "at the bottom of the band");
    // reach = tier * 4 * settlement_spacing, and settlement spacing is already in metres
    // and already scaled -- so this is a constant share of any planet, not a constant.
    check::close(villain.reach_m, 4.0 * kSpacing, 1e-9, "four spacings of reach per tier");
    check::equal(villain.status, std::string("living"), "standing");
    check::equal(villain.log.size(), std::size_t{1}, "one log line");
    check::equal(villain.log[0].event, std::string("rose"), "and it says how");
    // The calm region stayed under the band and seated nobody.
    check::is_true(find_villain(*result.block, "earth-node-1") == nullptr, "the calm region to be empty");
    check::close(tier_of(*result.block, "earth-node-1"), 0.2, 1e-12, "but to keep a ledger");
}

TEST(reach_scales_with_the_planet_rather_than_being_an_authored_metre_constant) {
    // Three systems in this codebase have now shipped a distance tuned for an 11 km
    // reference world that silently stopped firing on a 200 km one. Every reach here is a
    // multiple of settlement_spacing, which is itself resolved against the circumference,
    // so the same world at two widths produces the same reach IN SPACINGS.
    Bench small = two_regions();
    small.spacing = 50.0;
    Bench large = two_regions();
    large.spacing = 8072.500651872125;  // the 200 km reference world

    const VillainAdvance a = advance_villains(small.request());
    const VillainAdvance b = advance_villains(large.request());
    check::equal(a.standing.size(), std::size_t{1}, "a villain on the small world");
    check::equal(b.standing.size(), std::size_t{1}, "a villain on the large one");
    check::close(a.standing[0].reach_m / small.spacing, b.standing[0].reach_m / large.spacing, 1e-12,
                 "the same reach in spacings");
    check::is_true(b.standing[0].reach_m > 32000.0, "and a reach that can still bite at 200 km");
}

TEST(the_ceiling_stalls_a_pretender_in_the_ledger_and_not_only_in_a_local) {
    Bench bench = two_regions();
    bench.density = 12.0;  // ceil(2/12) -> 1 seat for two regions
    bench.reports.clear();
    bench.report("city-a", 1.0);
    bench.report("city-b", 1.0);

    const VillainAdvance result = advance_villains(bench.request());
    check::equal(result.standing.size(), std::size_t{1}, "the ceiling to bind at one");
    check::equal(result.standing[0].region, std::string("earth-node-0"), "the first anchor takes it");
    // The clamp is applied to the LEDGER. Clamping only the local would let the blocked
    // region bank 1.0 and burst straight through the moment a slot opened.
    check::close(tier_of(*result.block, "earth-node-1"), kStalledTier, 1e-12,
                 "the pretender pinned just under the line");
    check::is_true(tier_of(*result.block, "earth-node-1") < kSuperTier, "and strictly under it");
}

TEST(a_quiet_region_loses_its_villain_after_four_ages_and_keeps_a_fragment) {
    Bench bench = two_regions();
    bench.reports.clear();  // nobody has anything to say, so nothing concentrates
    VillainBlock block;
    block.tiers["earth-node-0"] = 1.0;
    Villain seated;
    seated.uid = "villain-0-11";
    seated.region = "earth-node-0";
    seated.born_age = 0;
    seated.seat_uid = "city-a";
    seated.node = 11;
    seated.direction = lean(0.0);
    seated.school = "earth";
    seated.held_nodes = {"earth-node-0"};
    seated.tier = 1.0;
    seated.reach_m = 4.0 * kSpacing;
    seated.growth = "usurping";
    seated.status = "living";
    block.people.push_back(seated);
    bench.block = block;

    // 1.0, .9, .81, .729, .6561 -- four quiet ages to cross the default hold from the
    // BOTTOM of the band, which is what "a reign is long once established" has to mean.
    const double expected[4] = {0.9, 0.81, 0.729, 0.6561};
    for (int age = 1; age <= 4; ++age) {
        bench.age = age;
        const VillainAdvance step = advance_villains(bench.request());
        check::is_true(step.block.has_value(), "a block at age " + std::to_string(age));
        const Villain* villain = find_villain(*step.block, "earth-node-0");
        check::is_true(villain != nullptr, "the villain to still be on the roster");
        check::close(villain->tier, expected[age - 1], 1e-9, "tier at age " + std::to_string(age));
        if (age < 4) {
            check::equal(step.standing.size(), std::size_t{1}, "still standing at " + std::to_string(age));
            check::close(tier_of(*step.block, "earth-node-0"), expected[age - 1], 1e-9, "the ledger");
        } else {
            check::equal(step.standing.size(), std::size_t{0}, "unseated at last");
            check::equal(villain->status, std::string("fallen"), "and recorded as fallen");
            check::is_true(villain->fell_age.has_value() && *villain->fell_age == 4, "with the age");
            // What a villain that left no mark leaves behind is the successor squabble.
            check::close(tier_of(*step.block, "earth-node-0"), 0.6561 * kFragmentShare, 1e-9,
                         "the fragment the region keeps");
            // Refreshed BEFORE the fall is decided, so a fallen villain carries a
            // live-looking reach and every consumer that means "who stands now" must say so.
            check::close(villain->reach_m, 0.6561 * 4.0 * kSpacing, 1e-6, "a live-looking reach");
        }
        bench.block = step.block;
    }
}

TEST(an_absent_status_reads_as_living) {
    // A record written before the fall was recorded at all described someone who stood.
    Villain old;
    old.status.clear();
    check::is_true(is_standing(old), "an empty status");
    old.status = "living";
    check::is_true(is_standing(old), "an explicit living");
    old.status = "fallen";
    check::is_true(!is_standing(old), "a fall");
    // And the filter must not quietly resurrect one.
    std::vector<Villain> people(3);
    people[0].uid = "a";
    people[1].uid = "b";
    people[1].status = "fallen";
    people[2].uid = "c";
    people[2].status = "living";
    check::equal(standing_villains(people).size(), std::size_t{2}, "two of three still hold ground");
}

TEST(a_stranded_reign_ending_does_not_free_a_seat_in_the_age_it_ends) {
    // The ordering rule, stated as a behaviour. A villain whose anchor no region was
    // produced for is visited AFTER the produced regions, so which regions seat a villain
    // this age is unchanged even when the stranded reign ends in the same age.
    Bench bench = two_regions();
    bench.cultures.clear();
    bench.cities.erase(bench.cities.begin() + 1);
    bench.culture("culture-north", {1});
    bench.hold = 0.95;   // high enough that one quiet age ends a reign at the band
    bench.density = 12.0;  // one region, one seat

    VillainBlock block;
    block.tiers["ghost-node"] = 1.0;
    block.tiers["earth-node-0"] = 0.0;
    Villain ghost;
    ghost.uid = "villain-0-99";
    ghost.region = "ghost-node";  // an anchor this age produces no region for
    ghost.born_age = 0;
    ghost.seat_uid = "city-gone";
    ghost.tier = 1.0;
    ghost.reach_m = 4.0 * kSpacing;
    ghost.status = "living";
    ghost.direction = lean(80.0);
    block.people.push_back(ghost);
    bench.block = block;

    const VillainAdvance result = advance_villains(bench.request());
    check::is_true(result.block.has_value(), "a block");
    const Villain* fell = find_villain(*result.block, "ghost-node");
    check::is_true(fell != nullptr, "the stranded villain to still be on the roster");
    check::equal(fell->status, std::string("fallen"), "its reign to have ended this age");
    // The produced region concentrated all the way to the band and STILL stalled, because
    // the seat was occupied at the moment it was considered. Visiting the stranded first
    // would have freed it and seated a second villain.
    check::is_true(find_villain(*result.block, "earth-node-0") == nullptr,
                   "the produced region to have seated nobody");
    check::close(tier_of(*result.block, "earth-node-0"), kStalledTier, 1e-12, "pinned at the line");
    check::equal(result.standing.size(), std::size_t{0}, "nobody left standing");
}

TEST(a_fall_marks_the_world_only_when_the_reign_left_something_behind) {
    Bench bench = two_regions();
    bench.reports.clear();
    bench.age = 3;
    bench.hold = 0.95;

    const auto seat = [&](const std::string& uid, const std::string& region, bool with_claim) {
        Villain villain;
        villain.uid = uid;
        villain.region = region;
        villain.born_age = 0;
        villain.seat_uid = "city-a";
        villain.node = 11;
        villain.direction = lean(0.0);
        villain.school = "earth";
        villain.held_nodes = {region};
        villain.tier = 1.0;
        villain.reach_m = 4.0 * kSpacing;
        villain.growth = "hoarding";
        villain.status = "living";
        if (with_claim) {
            VillainClaim claim;
            claim.id = "claim-" + uid;
            claim.villain_uid = uid;
            claim.age = 0;
            villain.claims.push_back(claim);
        }
        return villain;
    };

    VillainBlock block;
    block.tiers["earth-node-0"] = 1.0;
    block.tiers["earth-node-1"] = 1.0;
    block.people.push_back(seat("villain-0-11", "earth-node-0", true));
    block.people.push_back(seat("villain-0-22", "earth-node-1", false));
    bench.block = block;

    const VillainAdvance result = advance_villains(bench.request());
    check::equal(result.standing.size(), std::size_t{0}, "both reigns to have ended");
    // HELD NODES ALONE ARE NOT A MARK: a villain that reached the band, held nothing, took
    // no city and sank no well did not mark the world, and inventing a monument for it
    // would make the record of the dead less useful rather than more.
    check::equal(result.block->fallen.size(), std::size_t{1}, "one mark, not two");
    const FallenVillainMark& mark = result.block->fallen[0];
    check::equal(mark.id, std::string("fallen-villain-0-11"), "the one that left a claim");
    check::equal(mark.kind, std::string("fallen_villains"), "its kind");
    check::equal(mark.fell_age, 3, "the age it fell");
    // `born or age` would be wrong: age 0 is falsy and the first age is exactly when a
    // villain is most likely to have risen.
    check::equal(mark.born_age, 0, "born in age zero");
    check::equal(mark.reigned_ages, 3, "three ages, not zero");
    check::is_true(mark.tier_at_fall > 0.0, "the tier it fell at");
    check::equal(mark.left_claims.size(), std::size_t{1}, "the claim it left");
    check::equal(mark.left_held_nodes.size(), std::size_t{1}, "the nodes it held");

    // The claim is restamped with the holder's state as it changes, so a leaf package
    // reads how hard it presses off the claim itself and needs no join against the holder.
    const Villain* fallen = find_villain(*result.block, "earth-node-0");
    check::equal(fallen->claims[0].holder_status, std::string("fallen"), "the claim's holder");
    check::is_true(fallen->claims[0].holder_fell_age.has_value(), "and when");
    check::close(fallen->claims[0].influence, 0.6, 1e-12, "and how hard it still presses");
}

TEST(the_dead_keep_fading_on_every_later_age) {
    Bench bench = two_regions();
    bench.reports.clear();
    VillainBlock block;
    Villain gone;
    gone.uid = "villain-0-11";
    gone.region = "earth-node-0";
    gone.status = "fallen";
    gone.fell_age = 2;
    gone.tier = 0.5;
    VillainClaim claim;
    claim.id = "claim-villain-0-11";
    claim.holder_status = "fallen";
    claim.influence = 0.6;
    gone.claims.push_back(claim);
    block.people.push_back(gone);
    bench.block = block;

    bench.age = 4;  // two ages after the fall
    const VillainAdvance result = advance_villains(bench.request());
    const Villain* fallen = find_villain(*result.block, "earth-node-0");
    check::is_true(fallen != nullptr, "the record to be kept and never pruned");
    check::equal(fallen->status, std::string("fallen"), "still fallen");
    check::close(fallen->claims[0].influence, 0.216, 1e-12, "two ages of decay");
    check::equal(result.standing.size(), std::size_t{0}, "a fallen villain is not reloaded as living");
}

// --- the fate lottery ------------------------------------------------------

TEST(a_villain_reaches_past_its_own_seat_but_never_takes_it) {
    Villain villain;
    villain.uid = "villain-1-11";
    villain.seat_uid = "city-a";
    villain.direction = lean(0.0);
    villain.tier = 1.0;
    villain.reach_m = 4.0 * kSpacing;  // 200 m; spent at 1.5x = 300 m
    villain.growth = "devouring";
    villain.school = "earth";
    villain.status = "living";
    const std::vector<Villain> cast{villain};

    VillainCity seat;
    seat.uid = "city-a";
    seat.direction = lean(0.0);
    VillainCity near;
    near.uid = "city-b";
    near.direction = lean(5.0);  // 87 m away
    VillainCity far;
    far.uid = "city-c";
    far.direction = lean(30.0);  // 524 m away, past the spent distance

    // THE SEAT ITSELF IS IMMUNE: a power does not unmake the city it sits in.
    check::equal(villain_causes(cast, seat, kRadius).size(), std::size_t{0}, "its own seat");
    const std::vector<VillainCause> hit = villain_causes(cast, near, kRadius);
    check::equal(hit.size(), std::size_t{1}, "a neighbour inside the shadow");
    check::equal(hit[0].kind, std::string("villain_devouring"), "the growth names the cause");
    check::is_true(hit[0].weight > 0.0 && hit[0].weight < kVillainWeight,
                   "a weight under the full share at the seat");
    check::equal(hit[0].villain_uid, std::string("villain-1-11"), "the attribution a ruin carries");
    check::equal(hit[0].school, std::string("earth"), "the scar school a ruin resolves from");
    check::equal(villain_causes(cast, far, kRadius).size(), std::size_t{0}, "a city past the falloff");

    // Stated rather than relied upon: a fallen villain falls under `hold`, which is under
    // the band -- but that is a coupling between two constants, and one reaching this
    // lottery would move every world.
    std::vector<Villain> fallen = cast;
    fallen[0].status = "fallen";
    fallen[0].tier = 1.0;
    check::equal(villain_causes(fallen, near, kRadius).size(), std::size_t{0}, "a fallen villain");
    std::vector<Villain> weak = cast;
    weak[0].tier = 0.99;
    check::equal(villain_causes(weak, near, kRadius).size(), std::size_t{0}, "one under the band");
}

// --- the outlook -----------------------------------------------------------

TEST(the_outlook_standing_count_equals_the_rows_it_marks_seated) {
    Bench bench = two_regions();
    VillainBlock block;
    block.tiers["earth-node-0"] = 1.2;
    block.tiers["ghost-node"] = 1.1;
    Villain seated;
    seated.uid = "villain-0-11";
    seated.region = "earth-node-0";
    seated.tier = 1.2;
    seated.status = "living";
    Villain stranded = seated;
    stranded.uid = "villain-0-99";
    stranded.region = "ghost-node";
    stranded.tier = 1.1;
    block.people.push_back(seated);
    block.people.push_back(stranded);
    bench.block = block;

    const VillainOutlook outlook = villain_outlook(bench.request());
    check::equal(outlook.standing, 2, "two villains hold ground");
    check::equal(outlook.regions.size(), std::size_t{3}, "two produced rows and one stranded");
    int seated_rows = 0;
    for (const VillainOutlookRow& row : outlook.regions) {
        if (row.seated) ++seated_rows;
    }
    // The invariant version 2 exists for: `standing: 1` beside zero regions seated made one
    // document give two answers, with no way for a reader to tell which was right.
    check::equal(seated_rows, outlook.standing, "one answer, not two");

    const VillainOutlookRow& ghost = outlook.regions[2];
    check::equal(ghost.region, std::string("ghost-node"), "the stranded row, appended and sorted");
    check::is_true(!ghost.has_culture, "with no culture at all");
    check::equal(ghost.cities, 0, "and no cities");
    check::is_true(ghost.seated && !ghost.stalled, "seated, and a holder is never stalled");
    check::equal(outlook.ceiling, 1, "the ceiling over two regions at density three");
}

TEST(the_outlook_names_a_pretender_pinned_at_the_line) {
    Bench bench = two_regions();
    VillainBlock block;
    block.tiers["earth-node-1"] = kStalledTier;
    bench.block = block;

    const VillainOutlook outlook = villain_outlook(bench.request());
    check::equal(outlook.regions.size(), std::size_t{2}, "two rows");
    check::is_true(!outlook.regions[1].seated, "the pretender holds nothing");
    check::is_true(outlook.regions[1].stalled, "but is stalled at the line");
    check::close(outlook.regions[1].to_threshold, 0.001, 1e-9, "a hair under the band");
    check::is_true(!outlook.regions[0].stalled, "a region nowhere near the line is not stalled");
    check::equal(outlook.standing, 0, "and nobody is standing");
}

// --- promotion -------------------------------------------------------------

TEST(promotion_enters_at_the_bottom_of_the_band_and_is_idempotent) {
    Bench bench = two_regions();
    bench.block = VillainBlock{};
    bench.age = 2;

    const VillainPromotion first = promote_villains(bench.request());
    check::is_true(first.block.has_value(), "a block to be written");
    check::equal(first.promoted.size(), std::size_t{1}, "the ceiling allows one");
    const Villain& risen = first.promoted[0];
    check::equal(risen.region, std::string("earth-node-0"), "the most concentrated region");
    // Deliberately NOT an accumulation. The tier it is given is the tier it would have to
    // hold, because a villain that has just crossed the line is a threat that is growing.
    check::close(risen.tier, kSuperTier, 1e-12, "exactly at the bottom of the band");
    check::close(risen.reach_m, kSuperTier * 4.0 * kSpacing, 1e-9, "and the reach that goes with it");
    check::equal(risen.log[0].event, std::string("promoted"), "logged as an arrival");
    check::close(tier_of(*first.block, "earth-node-0"), 1.0, 1e-12, "and banked in the ledger");

    // The hook fires on the final age of a generation AND on the final age of an age
    // advance, and a caller may advance a world that was already promoted.
    bench.block = first.block;
    const VillainPromotion again = promote_villains(bench.request());
    check::equal(again.promoted.size(), std::size_t{0}, "nobody promoted twice");
    check::is_true(!again.block.has_value(), "and the block not touched at all");
}

TEST(promotion_takes_the_most_concentrated_regions_first) {
    Bench bench = two_regions();
    bench.density = 1.0;  // two regions, two seats
    bench.block = VillainBlock{};
    const VillainPromotion result = promote_villains(bench.request());
    check::equal(result.promoted.size(), std::size_t{2}, "both seats filled");
    check::equal(result.promoted[0].region, std::string("earth-node-0"), "the worse region first");
    check::equal(result.promoted[1].region, std::string("earth-node-1"), "then the calmer one");
    check::equal(result.block->people.size(), std::size_t{2}, "both on the roster");
}

TEST(a_world_that_never_raised_anybody_is_not_given_one_at_the_end) {
    Bench bench = two_regions();
    check::is_true(!bench.request().villains.has_value(), "the fixture to carry no block");
    const VillainPromotion result = promote_villains(bench.request());
    check::is_true(!result.block.has_value(), "the end of the simulation must not invent a block");
    check::equal(result.promoted.size(), std::size_t{0}, "and promote nobody");
}

// --- what a seat puts on the ground ----------------------------------------

TEST(a_well_joins_the_nodes_its_villain_holds_and_is_sunk_exactly_once) {
    Bench bench = two_regions();
    bench.network("earth", {{"earth-node-0", lean(1.0)},
                            {"earth-node-1", lean(41.0)},
                            {"earth-node-2", lean(2.0)}});
    VillainBlock block;
    Villain villain;
    villain.uid = "villain-1-11";
    villain.region = "earth-node-0";
    villain.seat_uid = "city-a";
    villain.node = 11;
    villain.direction = lean(0.0);
    villain.school = "earth";
    villain.held_nodes = {"earth-node-0", "earth-node-2"};
    villain.tier = 1.0;
    villain.reach_m = 4.0 * kSpacing;
    villain.status = "living";
    block.people.push_back(villain);
    bench.block = block;

    const VillainWorks works = build_villain_works(bench.request());
    check::equal(works.works.size(), std::size_t{1}, "one villain built something");
    check::is_true(works.works[0].well, "it sank a well");
    check::is_true(works.works[0].claim, "and staked a claim");

    const Villain& after = works.block.people[0];
    // Bound to an old god by the ground it holds, which is also why being sworn to one is
    // no disqualification when a hidden god comes for it later.
    check::equal(after.god, std::string("god_earth"), "bound to its school's god");
    check::is_true(after.well.has_value(), "a well record");
    check::equal(after.well->node, std::string("well-villain-1-11"), "named for its holder");
    // The EDGES are the point: they change what the network carries between the villain's
    // holdings, which a lone key point cannot do.
    check::equal(after.well->lines.size(), std::size_t{2}, "one line per held node");
    check::equal(after.well->lines[0], std::string("well-villain-1-11-line-0"), "named in node order");
    check::equal(after.held_nodes.size(), std::size_t{3}, "the well joins the holdings");

    const LeyNetwork& net = works.magic.networks.at("earth");
    check::equal(net.nodes.size(), std::size_t{4}, "three nodes and the well");
    check::close(net.nodes[3].intensity, kWellIntensity, 1e-12, "a source, not a marker");
    check::equal(net.edges.size(), std::size_t{2}, "two joining lines");
    for (const LeyEdge& edge : net.edges) {
        // Edges index their endpoints by POSITION, so the node is appended and never
        // inserted -- an insert would renumber every edge after it.
        check::equal(edge.from, 3, "every line leaves the well");
        check::is_true(edge.to >= 0 && edge.to < 3, "and lands on a node that was already there");
        check::close(edge.intensity, kWellEdgeIntensity, 1e-12, "the line's carrying strength");
    }

    // ONE WELL PER VILLAIN, EVER.
    VillainRequest second = bench.request();
    second.villains = works.block;
    second.magic = &works.magic;
    second.age = 2;
    const VillainWorks twice = build_villain_works(second);
    check::equal(twice.magic.networks.at("earth").nodes.size(), std::size_t{4}, "no second well");
    check::equal(twice.magic.networks.at("earth").edges.size(), std::size_t{2}, "and no second line");
    check::equal(twice.works.size(), std::size_t{1}, "the claim is still work done");
    check::is_true(!twice.works[0].well, "but the well is not");
}

TEST(a_claim_refreshes_the_holders_standing_and_freezes_what_it_took) {
    Bench bench = two_regions();
    bench.city("city-c", 3, 33, lean(2.0), "dwarf");
    VillainBlock block;
    Villain villain;
    villain.uid = "villain-1-11";
    villain.region = "earth-node-0";
    villain.seat_uid = "city-a";
    villain.node = 11;
    villain.direction = lean(0.0);
    villain.school = "earth";
    villain.held_nodes = {"earth-node-0"};
    villain.tier = 1.0;
    villain.reach_m = 4.0 * kSpacing;
    villain.status = "living";
    block.people.push_back(villain);
    bench.block = block;

    const VillainWorks first = build_villain_works(bench.request());
    const Villain& staked = first.block.people[0];
    check::equal(staked.claims.size(), std::size_t{1}, "one claim");
    const VillainClaim& claim = staked.claims[0];
    check::equal(claim.id, std::string("claim-villain-1-11"), "one constant id per villain, ever");
    check::equal(claim.kind, std::string("village"), "a village at the bottom of the band");
    check::equal(claim.age, 1, "the age it was taken");
    check::equal(claim.drawn_from.size(), std::size_t{3}, "drawn from the three nearest cities");
    check::equal(claim.drawn_from[0], std::string("city-a"), "nearest first");
    // Mixed by construction: a villain takes whoever is close, not whoever shares its
    // blood, which is what makes its holdings look wrong to everyone.
    check::equal(claim.factions.size(), std::size_t{2}, "two peoples, sorted and deduplicated");
    check::equal(claim.factions[0], std::string("dwarf"), "sorted");
    check::equal(claim.holder_status, std::string("living"), "stated on both sides");

    // A second age with a grown holder. Only the standing half moves.
    VillainBlock grown = first.block;
    grown.people[0].tier = 1.6;
    VillainRequest next = bench.request();
    next.villains = grown;
    next.magic = &first.magic;
    next.age = 5;
    const VillainWorks later = build_villain_works(next);
    const VillainClaim& refreshed = later.block.people[0].claims[0];
    check::equal(later.block.people[0].claims.size(), std::size_t{1}, "still one claim, not two");
    check::equal(refreshed.kind, std::string("seat"), "a tier band is not a thing to freeze");
    check::equal(refreshed.refreshed_age, 5, "restamped");
    check::equal(refreshed.age, 1, "but the age it was taken does not move");
    check::equal(refreshed.drawn_from[0], std::string("city-a"),
                 "and a city founded later does not retroactively join it");
}

TEST(a_villain_with_no_school_sinks_no_well_and_still_claims_ground) {
    Bench bench = two_regions();
    bench.magic.networks.clear();
    VillainBlock block;
    Villain villain;
    villain.uid = "villain-1-11";
    villain.region = "seat-city-a";
    villain.seat_uid = "city-a";
    villain.node = 11;
    villain.direction = lean(0.0);
    villain.tier = 1.0;
    villain.reach_m = 4.0 * kSpacing;
    villain.status = "living";
    block.people.push_back(villain);
    bench.block = block;

    const VillainWorks works = build_villain_works(bench.request());
    check::equal(works.works.size(), std::size_t{1}, "work was still done");
    check::is_true(!works.works[0].well, "no school, no well");
    check::is_true(works.works[0].claim, "but ground is still taken");
    check::is_true(!works.block.people[0].well.has_value(), "and no well record invented");
    check::equal(works.block.people[0].god, std::string(""), "and no god to be bound to");
}

TEST(a_villain_under_the_band_builds_nothing) {
    Bench bench = two_regions();
    VillainBlock block;
    Villain villain;
    villain.uid = "villain-1-11";
    villain.region = "earth-node-0";
    villain.seat_uid = "city-a";
    villain.direction = lean(0.0);
    villain.school = "earth";
    villain.held_nodes = {"earth-node-0"};
    villain.tier = 0.99;  // still standing, no longer super
    villain.reach_m = 0.99 * 4.0 * kSpacing;
    villain.status = "living";
    block.people.push_back(villain);
    bench.block = block;

    const VillainWorks works = build_villain_works(bench.request());
    check::equal(works.works.size(), std::size_t{0}, "nothing built under the band");
    check::equal(works.magic.networks.at("earth").nodes.size(), std::size_t{2}, "and no node added");
    check::equal(works.block.people[0].claims.size(), std::size_t{0}, "and no ground taken");
}
