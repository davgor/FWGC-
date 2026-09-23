// Corruption invariants.
//
// The world is hand-built and the pantheon is resolved for real, because everything under
// test here is about which god the WORLD picks, what the gate does at each end of the
// ledger, and what a cluster does to the ground -- and a resolved pantheon is the only way
// the hidden four's existence rules and aspects are the shipped ones rather than a mock.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "fwg/corruption.hpp"
#include "fwg/religion.hpp"
#include "fwg/villains.hpp"

using namespace fwg;

namespace {

constexpr int kGrid = 8;
constexpr double kRadius = 1000.0;
constexpr double kSpacing = 50.0;

Vec3 lean(double degrees) {
    const double a = degrees * 3.14159265358979323846 / 180.0;
    return {std::sin(a), std::cos(a), 0.0};
}

struct Bench {
    MagicReport magic;
    std::map<std::string, Grid> layers;
    Almanac almanac;
    std::vector<ReligionCity> religion_cities;
    std::vector<ReligionNest> religion_nests;
    std::vector<ReligionRuin> religion_ruins;
    std::vector<std::string> used{"human_heartland", "dwarf"};
    Religion religion;

    std::vector<VillainCity> cities;
    std::vector<std::string> nest_families;
    VillainBlock villains;
    std::vector<CorruptionRecord> corruptions;
    int existing_ruins = 0;

    long long encounters = 40;
    long long variation = 0;
    std::uint32_t seed = 42;
    int age = 2;

    // Every school the table names, so the cluster always has a network to land in. The
    // four hidden ones are EMPTY, which is the world's normal state: generation locks
    // their occurrence at zero and this API is the only thing that can put a node in one.
    void schools(int live_known) {
        int made = 0;
        for (const MagicSchool& school : magic_school_table()) {
            LeyNetwork net;
            net.name = std::string(school.name);
            if (!school.hidden && made < live_known) {
                net.nodes.push_back({net.name + "-node-0", lean(20.0), 1.0});
                ++made;
            }
            magic.networks[net.name] = net;
            layers["ley_" + net.name] = Grid(kGrid, school.hidden ? 0.0 : 0.3);
        }
        magic.enabled = true;
    }

    void city(const std::string& uid, int node, const Vec3& where, double people,
              const std::string& profile = "human_heartland") {
        VillainCity c;
        c.uid = uid;
        c.id = static_cast<int>(cities.size()) + 1;
        c.node = node;
        c.direction = where;
        c.population_profile = profile;
        c.population_estimate = people;
        cities.push_back(c);

        ReligionCity r;
        r.uid = uid;
        r.population_profile = profile;
        r.x = 3;
        r.z = 4;
        r.node = node;
        r.direction = where;
        religion_cities.push_back(r);
    }

    void aberrant_nests(int count) {
        for (int i = 0; i < count; ++i) {
            nest_families.emplace_back("aberrant");
            ReligionNest nest;
            nest.id = "nest-" + std::to_string(i);
            nest.name = "Thing " + std::to_string(i);
            nest.family = "aberrant";
            nest.real = false;
            nest.direction = lean(60.0);
            religion_nests.push_back(nest);
        }
    }

    void ruins(int count) {
        existing_ruins = count;
        for (int i = 0; i < count; ++i) {
            religion_ruins.push_back({"ruin-" + std::to_string(i), 40 + i, "war_border", 1, "dwarf"});
        }
    }

    void villain(const std::string& uid, const Vec3& where, double tier, double reach_m,
                 bool corrupted = false) {
        Villain v;
        v.uid = uid;
        v.region = "earth-node-0";
        v.born_age = 1;
        v.seat_uid = "city-a";
        v.node = 11;
        v.direction = where;
        v.school = "earth";
        v.held_nodes = {"earth-node-0"};
        v.tier = tier;
        v.reach_m = reach_m;
        v.growth = "hoarding";
        v.status = "living";
        v.corrupted = corrupted;
        villains.people.push_back(v);
        villains.tiers[v.region] = tier;
    }

    void resolve() {
        ReligionRequest r;
        r.seed = seed;
        r.magic = &magic;
        r.layers = &layers;
        r.almanac = &almanac;
        r.cities = religion_cities;
        r.nests = religion_nests;
        r.ruins = religion_ruins;
        r.used_civilizations = used;
        r.ocean_km2 = 300.0;
        r.lake_km2 = 100.0;
        r.dry_km2 = 600.0;
        r.globe_radius_m = kRadius;
        religion = resolve_religion(r);
    }

    CorruptionRequest request() const {
        CorruptionRequest r;
        r.seed = seed;
        r.age = age;
        r.globe_radius_m = kRadius;
        r.settlement_spacing_m = kSpacing;
        r.encounters = encounters;
        r.variation = variation;
        r.cities = cities;
        r.existing_ruins = existing_ruins;
        r.nest_families = nest_families;
        r.magic = magic;
        r.religion = religion;
        r.villains = villains;
        r.corruptions = corruptions;
        return r;
    }
};

// Three cities around one seated villain, and a ledger well past every climb.
void populate(Bench& b) {
    b.city("city-a", 11, lean(0.0), 1000.0);        // the seat, 0 m away
    b.city("city-b", 22, lean(5.0), 1000.0);        // 87 m, inside a 200 m reach
    b.city("city-c", 33, lean(30.0), 1000.0, "dwarf");  // 524 m, well outside
    b.villain("villain-1-11", lean(0.0), 1.0, 4.0 * kSpacing);
}

// A rich world with every known school alive: The Long Thirst wants the people in it.
Bench blood_world() {
    Bench b;
    b.schools(8);
    populate(b);
    b.cities[0].population_estimate = 300000.0;
    b.resolve();
    return b;
}

// A world mostly in ruins: The Glad Mother wants the decay.
Bench rot_world() {
    Bench b;
    b.schools(8);
    populate(b);
    b.ruins(9);
    b.resolve();
    return b;
}

// A world crawling with wrong things: The Shape Beneath wants the minds in it.
Bench eldritch_world() {
    Bench b;
    b.schools(8);
    populate(b);
    b.aberrant_nests(24);
    b.ruins(1);
    b.resolve();
    return b;
}

// A world whose magic has nearly gone out: The Unwriting wants the silence.
Bench void_world() {
    Bench b;
    b.schools(1);
    populate(b);
    b.ruins(1);
    b.resolve();
    return b;
}

const HiddenGod& hidden(std::string_view id) {
    const HiddenGod* god = hidden_pantheon_god(id);
    if (god == nullptr) check::fail("no hidden god called " + std::string(id));
    return *god;
}

std::string status_of(const Religion& religion, std::string_view id) {
    for (const GodRecord& god : religion.gods) {
        if (god.id == id) return god.status;
    }
    return "<missing>";
}

const GodRecord* record_of(const Religion& religion, std::string_view id) {
    for (const GodRecord& god : religion.gods) {
        if (god.id == id) return &god;
    }
    return nullptr;
}

const LeyNode* node_of(const LeyNetwork& net, const std::string& id) {
    for (const LeyNode& node : net.nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

}  // namespace

// --- the gate --------------------------------------------------------------

TEST(the_four_noticing_points_are_distinct_and_span_the_ledger) {
    const auto gods = hidden_pantheon_gods();
    check::equal(gods.size(), std::size_t{4}, "four gods nobody speaks of");
    std::set<int> noticing;
    for (const HiddenGod& god : gods) {
        noticing.insert(god.noticing);
        check::is_true(god.climb > god.noticing, std::string(god.id) + " to have somewhere to climb");
        check::is_true(!god.school.empty(), std::string(god.id) + " to own a hidden school");
    }
    // Each god needs its own noticing point, and the last should be far behind the first:
    // a player who has met three villains is in a different world from one who has met
    // fifteen, and that ordering is the escalation the ledger buys.
    check::equal(noticing.size(), std::size_t{4}, "four distinct noticing points");
    check::is_true(*noticing.rbegin() > 2 * *noticing.begin(), "the last far behind the first");
}

TEST(a_gods_chance_is_exactly_zero_below_its_noticing_point) {
    for (const HiddenGod& god : hidden_pantheon_gods()) {
        const std::string name(god.id);
        // EXACTLY zero, not merely small. A guaranteed-clean early playthrough is what
        // makes the first corruption read as something the player caused.
        check::equal(hidden_chance(god, 0), 0.0, name + " at an empty ledger");
        check::equal(hidden_chance(god, god.noticing - 1), 0.0, name + " one short");
        check::equal(hidden_chance(god, god.noticing), 0.0, name + " at the point itself");
        const double halfway = hidden_chance(god, (god.noticing + god.climb) / 2);
        check::is_true(halfway > 0.0 && halfway < 1.0, name + " to climb between the two");
        check::equal(hidden_chance(god, god.climb), 1.0, name + " certain at its climb");
        check::equal(hidden_chance(god, god.climb + 1000), 1.0, name + " and no further");
    }
}

TEST(the_ledger_gates_the_act_at_both_ends) {
    // A sweep rather than a single seed, because one seed proves only what one roll did.
    const Bench bench = blood_world();
    const auto acted_over = [&bench](long long encounters) {
        int acted = 0;
        for (std::uint32_t seed = 1; seed <= 200; ++seed) {
            CorruptionRequest request = bench.request();
            request.seed = seed;
            request.encounters = encounters;
            if (corrupt_world(request).acted) ++acted;
        }
        return acted;
    };

    // Below every noticing point nothing is watching at all, so no roll is taken and the
    // world comes back untouched however the dice would have fallen.
    check::equal(acted_over(2), 0, "a clean early playthrough");
    // Just past the first noticing point the chance is 1/9: some worlds turn, most do not.
    const int early = acted_over(4);
    check::is_true(early > 0, "a ledger past the first point to sometimes bite");
    check::is_true(early < 200, "and to be nowhere near certain");
    // At the climb it is certain, for every seed.
    check::equal(acted_over(12), 200, "a ledger at the climb");
}

TEST(a_world_with_no_watching_god_says_so_and_is_returned_untouched) {
    Bench bench = blood_world();
    bench.encounters = 0;
    const CorruptionRequest request = bench.request();
    const CorruptionOutcome outcome = corrupt_world(request);

    check::is_true(!outcome.acted, "nothing acted");
    check::equal(outcome.reason, std::string("no hidden god is watching yet"), "and it says why");
    check::equal(outcome.watching.size(), std::size_t{0}, "nobody is watching");
    check::is_true(!outcome.rebuild_required, "so the caller owes nothing");
    check::equal(outcome.survivors.size(), request.cities.size(), "every city still stands");
    check::equal(outcome.new_ruins.size(), std::size_t{0}, "and nothing is ruined");
    check::equal(outcome.corruptions.size(), std::size_t{0}, "and no record was written");
    // The hidden four leave no trace anywhere until one of them returns.
    for (const HiddenGod& god : hidden_pantheon_gods()) {
        check::equal(status_of(outcome.religion, god.id), std::string("absent"),
                     std::string(god.id) + " to be nowhere in the world");
    }
}

// --- which god, which host -------------------------------------------------

TEST(which_god_takes_an_interest_is_read_from_the_world_and_never_rolled) {
    const std::pair<Bench, std::string> worlds[4] = {
        {blood_world(), "god_long_thirst"},
        {rot_world(), "god_glad_mother"},
        {eldritch_world(), "god_shape_beneath"},
        {void_world(), "god_unwriting"},
    };
    for (const auto& [bench, expected] : worlds) {
        const CorruptionRequest request = bench.request();
        const std::vector<const HiddenGod*> ranked = watching_gods(request);
        check::is_true(!ranked.empty(), "somebody to be watching a world at ledger 40");
        check::equal(std::string(ranked.front()->id), expected, "the god this world calls");
        // Ranked, not merely filtered: the head must beat everyone behind it.
        const double head = hidden_interest(ranked.front()->interest, request);
        for (std::size_t i = 1; i < ranked.size(); ++i) {
            const double behind = hidden_interest(ranked[i]->interest, request);
            check::is_true(head > behind || (head == behind && ranked.front()->id < ranked[i]->id),
                           "most interested first, ties by id");
        }
    }
}

TEST(interest_saturates_at_one_and_floors_at_zero) {
    Bench bench = blood_world();
    CorruptionRequest request = bench.request();

    // living: a world of 200 000 people is as appetising as a world of ten million.
    check::close(hidden_interest("living", request), 1.0, 1e-12, "a rich world");
    for (VillainCity& city : request.cities) city.population_estimate = 0.0;
    check::equal(hidden_interest("living", request), 0.0, "an empty one");

    // decay: ruins against everything that still stands plus everything that does not.
    request.existing_ruins = 0;
    check::equal(hidden_interest("decay", request), 0.0, "a world with no ruins");
    request.existing_ruins = 3;
    check::close(hidden_interest("decay", request), 0.5, 1e-12, "three cities and three ruins");

    // aberrant: two dozen wrong things saturate it.
    check::equal(hidden_interest("aberrant", request), 0.0, "a world with nothing wrong in it");
    request.nest_families.assign(24, "undead");
    check::close(hidden_interest("aberrant", request), 1.0, 1e-12, "undead count as wrong too");

    // dead_magic: the inverse of how many of the eight known schools still have key points.
    check::equal(hidden_interest("dead_magic", request), 0.0, "all eight schools alive");
    request.magic.networks["weave"].nodes.clear();
    check::close(hidden_interest("dead_magic", request), 0.125, 1e-12, "one school gone out");
    for (auto& [name, net] : request.magic.networks) net.nodes.clear();
    check::close(hidden_interest("dead_magic", request), 1.0, 1e-12, "the magic gone entirely");

    check::throws([&] { hidden_interest("nonsense", request); }, "an interest nobody declares");
}

TEST(the_host_is_the_furthest_reaching_uncorrupted_villain) {
    VillainBlock block;
    const auto add = [&block](const std::string& uid, double tier, bool corrupted) {
        Villain v;
        v.uid = uid;
        v.tier = tier;
        v.status = "living";
        v.corrupted = corrupted;
        block.people.push_back(v);
    };
    add("villain-1-10", 1.0, false);
    add("villain-1-40", 1.8, false);
    add("villain-1-20", 1.2, false);
    check::equal(corruption_host(block, {})->uid, std::string("villain-1-40"), "the highest tier");

    // Ties on tier go to the lexicographically GREATEST uid, because the reference's key is
    // the pair and not the tier with a tie-break.
    block.people.clear();
    add("villain-1-10", 1.5, false);
    add("villain-1-40", 1.5, false);
    add("villain-1-20", 1.5, false);
    check::equal(corruption_host(block, {})->uid, std::string("villain-1-40"), "the greatest uid");

    // Already taken by a hidden god is the ONLY disqualification. Being sworn to an old one
    // is not -- and sinking a well is what swears a villain to one.
    block.people.clear();
    add("villain-1-40", 1.8, true);
    add("villain-1-20", 1.2, false);
    check::equal(corruption_host(block, {})->uid, std::string("villain-1-20"), "the one still free");
    // An explicit uid searches the candidates rather than the filtered set, so a second god
    // can take a host the first already has.
    check::equal(corruption_host(block, std::string("villain-1-40"))->uid, std::string("villain-1-40"),
                 "a host named outright");
    check::is_true(corruption_host(block, std::string("villain-9-99")) == nullptr, "a stranger");

    // A fallen villain holds no ground, so no god reaches for one.
    block.people[0].corrupted = false;
    block.people[0].status = "fallen";
    block.people[1].status = "fallen";
    check::is_true(corruption_host(block, {}) == nullptr, "a world whose villains have all fallen");
}

TEST(a_world_with_no_standing_villain_is_refused_rather_than_ignored) {
    Bench bench = blood_world();
    bench.villains.people[0].status = "fallen";
    check::throws([&] { corrupt_world(bench.request()); }, "a world with nobody to seat");

    Bench mismatched = blood_world();
    mismatched.religion.catalogue.sha256 = "not the catalogue this build carries";
    check::throws([&] { corrupt_world(mismatched.request()); }, "a world built on another pantheon");

    Bench old = blood_world();
    old.religion.version = 0;
    check::throws([&] { corrupt_world(old.request()); }, "a world with no religion contract");

    Bench huge = blood_world();
    huge.encounters = 4294967296LL;
    check::throws([&] { corrupt_world(huge.request()); }, "a ledger past a uint32");
}

TEST(every_standing_villain_already_taken_leaves_the_world_alone) {
    Bench bench = blood_world();
    bench.villains.people[0].corrupted = true;
    const CorruptionOutcome outcome = corrupt_world(bench.request());
    check::is_true(!outcome.acted, "nothing to seat");
    check::is_true(!outcome.watching.empty(), "although somebody is watching");
    // The reference's own wording, reproduced: nothing was rolled at all, and the message
    // says otherwise. Recorded rather than quietly reworded, because a consumer may match
    // on it.
    check::equal(outcome.reason, std::string("a watching god did not act this time"),
                 "the reference's misleading reason");
}

// --- the cluster -----------------------------------------------------------

TEST(a_corruption_plants_four_nodes_in_a_school_nothing_else_can_reach) {
    Bench bench = blood_world();
    const CorruptionOutcome outcome = corrupt_world(bench.request());
    check::is_true(outcome.acted, "the ledger is past every climb, so it acts");
    check::equal(outcome.god_id, std::string("god_long_thirst"), "the god this world called");

    const LeyNetwork& blood = outcome.magic.networks.at("blood");
    check::equal(bench.magic.networks.at("blood").nodes.size(), std::size_t{0},
                 "the school to have been empty before");
    check::equal(blood.nodes.size(), std::size_t{4}, "a seat and three ring points");
    check::close(blood.nodes[0].intensity, kCorruptionSeatIntensity, 1e-12, "a maximal source");
    for (std::size_t k = 1; k < 4; ++k) {
        check::close(blood.nodes[k].intensity, kCorruptionRingIntensity, 1e-12, "a ring point");
        // The ring sits at RING_SPACINGS of settlement spacing from the seat, which scales
        // with the planet: 0.4 * 50 / 1000 rad on this world.
        const double out = great_circle_m(kRadius, blood.nodes[0].direction, blood.nodes[k].direction);
        check::close(out, kCorruptionRingSpacings * kSpacing, 1e-6, "the ring radius in metres");
        check::close(dot(blood.nodes[k].direction, blood.nodes[k].direction), 1.0, 1e-9,
                     "a unit direction");
    }
    // The three are evenly spaced around the seat, so the whole cluster is one fixed shape
    // rotated by a single draw.
    const double a = great_circle_m(kRadius, blood.nodes[1].direction, blood.nodes[2].direction);
    const double b = great_circle_m(kRadius, blood.nodes[2].direction, blood.nodes[3].direction);
    check::close(a, b, 1e-6, "evenly spaced");

    check::equal(outcome.record->nodes.size(), std::size_t{4}, "and the record names them");
    check::equal(outcome.record->nodes[0], std::string("corrupt-god_long_thirst-0-0"), "the seat id");
    check::equal(corruption_order().size(), std::size_t{11}, "eleven ordered steps are published");
}

TEST(the_gate_stream_and_the_cluster_stream_are_derived_separately) {
    // Two distinct derivations, and the difference is the point. The escalation roll varies
    // on the LEDGER, so retrying with a higher one gives a different roll and a higher
    // threshold; the cluster's bearing varies on VARIATION, so a caller can reroll the
    // shape without moving the gate.
    Bench base = blood_world();
    base.encounters = 12;  // already certain, so the gate cannot be what differs
    const CorruptionOutcome first = corrupt_world(base.request());

    Bench later = blood_world();
    later.encounters = 40;
    const CorruptionOutcome higher = corrupt_world(later.request());
    check::is_true(first.acted && higher.acted, "both to act");
    const LeyNetwork& a = first.magic.networks.at("blood");
    const LeyNetwork& b = higher.magic.networks.at("blood");
    for (std::size_t k = 0; k < 4; ++k) {
        check::close(a.nodes[k].direction.x, b.nodes[k].direction.x, 1e-15,
                     "the ledger not to move the cluster");
        check::close(a.nodes[k].direction.z, b.nodes[k].direction.z, 1e-15, "in any component");
    }

    Bench varied = blood_world();
    varied.variation = 7;
    const CorruptionOutcome other = corrupt_world(varied.request());
    check::is_true(other.acted, "variation not to move the gate");
    const LeyNetwork& c = other.magic.networks.at("blood");
    // The seat sits on the host either way; only the ring turns.
    check::close(c.nodes[0].direction.x, b.nodes[0].direction.x, 1e-15, "the seat to be the host");
    bool moved = false;
    for (std::size_t k = 1; k < 4; ++k) {
        if (std::abs(c.nodes[k].direction.x - b.nodes[k].direction.x) > 1e-9) moved = true;
    }
    check::is_true(moved, "but the ring to have turned");
}

TEST(variation_rerolls_the_shape_and_never_the_gate) {
    // The escalation roll is drawn on a stream that varies on the LEDGER and not on
    // `variation`, so a caller may reroll a corruption's shape as often as it likes without
    // being able to reroll the gate into opening. A porter who wires `variation` into both
    // streams turns the gate into a retry button, and nothing else in this suite would
    // notice: every other case sits at a ledger where the chance is already one.
    const Bench bench = blood_world();
    std::uint32_t opens = 0;
    std::uint32_t shuts = 0;
    for (std::uint32_t seed = 1; seed <= 200 && (opens == 0 || shuts == 0); ++seed) {
        CorruptionRequest probe = bench.request();
        probe.seed = seed;
        probe.encounters = 4;  // one past the first noticing point, where the chance is 1/9
        (corrupt_world(probe).acted ? opens : shuts) = seed;
    }
    // The floor: without both of these the sweep below asserts nothing at all.
    check::is_true(opens != 0, "a seed whose roll opens the gate at a ledger of four");
    check::is_true(shuts != 0, "and one whose roll does not");

    for (const std::uint32_t seed : {opens, shuts}) {
        const bool expected = seed == opens;
        for (long long variation = 0; variation < 50; ++variation) {
            CorruptionRequest probe = bench.request();
            probe.seed = seed;
            probe.encounters = 4;
            probe.variation = variation;
            check::equal(corrupt_world(probe).acted, expected,
                         "seed " + std::to_string(seed) + " at variation " +
                             std::to_string(variation));
        }
    }
}

TEST(the_reach_floor_carries_a_villain_whose_seat_city_has_fallen) {
    Bench bench = blood_world();
    bench.villains.people[0].reach_m = 0.0;
    const CorruptionOutcome outcome = corrupt_world(bench.request());
    check::is_true(outcome.acted, "it still acts");
    // Without the floor the failure mode divides by zero and the corruption reaches
    // nothing at all, which is the wrong answer for a villain that still sits where its
    // node is.
    check::close(outcome.record->reach_m, kCorruptionReachSpacings * kSpacing, 1e-12,
                 "the floor under a spent villain");

    Bench reaching = blood_world();
    reaching.villains.people[0].reach_m = 1000.0;
    // A corrupted villain reaches as far as it already did, when that is further.
    check::close(corrupt_world(reaching.request()).record->reach_m, 1000.0, 1e-12, "its own reach");
}

// --- the four failure modes ------------------------------------------------

TEST(blood_farms_the_cities_in_reach_and_leaves_them_standing) {
    Bench bench = blood_world();
    const CorruptionOutcome outcome = corrupt_world(bench.request());
    check::is_true(outcome.acted, "the Long Thirst acts");
    check::equal(outcome.survivors.size(), std::size_t{3}, "the city stands and its people do not");
    check::equal(outcome.new_ruins.size(), std::size_t{0}, "nothing is destroyed");
    check::equal(outcome.record->touched.size(), std::size_t{2}, "two cities inside the reach");

    // At the seat the share is exactly one, so 45% of its people are taken.
    check::equal(outcome.record->touched[0].city_uid, std::string("city-a"), "the seat first");
    check::equal(outcome.record->touched[0].effect, std::string("farmed"), "farmed");
    check::close(outcome.survivors[0].population_estimate, 300000.0 * (1.0 - kCorruptionFarmShare),
                 1e-6, "the seat loses the full share");
    check::close(outcome.record->touched[0].people_lost, 300000.0 * kCorruptionFarmShare, 1e-6,
                 "and the record says how many");
    // Linear falloff: a city 87 m into a 200 m reach loses less.
    const double edge = outcome.survivors[1].population_estimate;
    check::is_true(edge > 1000.0 * (1.0 - kCorruptionFarmShare) && edge < 1000.0,
                   "a city near the edge loses less than the seat");
    // A city past the reach is untouched.
    check::close(outcome.survivors[2].population_estimate, 1000.0, 1e-12, "a city out of reach");
}

TEST(rot_ends_the_cities_in_reach_and_leaves_ruins_that_name_the_god) {
    Bench bench = rot_world();
    const CorruptionOutcome outcome = corrupt_world(bench.request());
    check::equal(outcome.god_id, std::string("god_glad_mother"), "the Glad Mother");
    check::equal(outcome.survivors.size(), std::size_t{1}, "only the city out of reach survives");
    check::equal(outcome.survivors[0].uid, std::string("city-c"), "and it is the far one");
    check::equal(outcome.new_ruins.size(), std::size_t{2}, "two cities ended");
    check::equal(outcome.record->touched.size(), std::size_t{0}, "rot touches nothing, it ends it");

    const CorruptionRuin& seat = outcome.new_ruins[0];
    check::equal(seat.id, std::string("ruin-city-a"), "named for the city");
    check::equal(seat.cause, std::string("rot_plague"), "the cause rot leaves");
    check::equal(seat.god_id, std::string("god_glad_mother"), "the evidence names the god");
    check::equal(seat.school, std::string("rot"), "and its school");
    check::close(seat.share, 1.0, 1e-9, "the seat takes the full share");
    check::close(seat.probability, 1.0, 1e-12, "certain, not rolled");
    check::close(seat.roll, 0.0, 1e-12, "and no draw was taken for it");
    check::is_true(!seat.reason.empty(), "a reason a reader can print");
    check::is_true(seat.reason.find(std::string(hidden("god_glad_mother").name)) == 0,
                   "which opens with the god's name");
    // The god IS the source: the school and the basis are stated rather than resolved from
    // the region, which can never hold a hidden school.
    check::equal(seat.legacy.school, std::string("rot"), "the legacy school");
    check::equal(seat.legacy.basis, std::string("source"), "stated, not inferred");
    check::close(seat.legacy.intensity, kClassIntensitySmall, 1e-12, "the class intensity survives");
    check::equal(seat.new_node_school, std::string("rot"), "and the field the reference writes");
    check::equal(outcome.record->ruins.size(), std::size_t{2}, "the record lists them");

    // A capital leaves a stronger scar than a hamlet, which is the ONE thing the ruin
    // legacy is still asked for.
    Bench capital = rot_world();
    capital.cities[0].city_class = CityClass::Capital;
    check::close(corrupt_world(capital.request()).new_ruins[0].legacy.intensity,
                 kClassIntensityCapital, 1e-12, "a capital's scar");
}

TEST(eldritch_takes_a_city_without_changing_anything_anyone_can_see) {
    Bench bench = eldritch_world();
    const CorruptionOutcome outcome = corrupt_world(bench.request());
    check::equal(outcome.god_id, std::string("god_shape_beneath"), "the Shape Beneath");
    check::equal(outcome.survivors.size(), std::size_t{3}, "nothing is destroyed");
    check::equal(outcome.new_ruins.size(), std::size_t{0}, "and nothing falls");
    check::equal(outcome.record->touched.size(), std::size_t{2}, "two cities taken");
    check::equal(outcome.record->touched[0].effect, std::string("taken"), "taken, not farmed");
    check::equal(outcome.survivors[0].taken_by, std::string("god_shape_beneath"), "the seat is its");
    check::equal(outcome.survivors[1].taken_by, std::string("god_shape_beneath"), "and its neighbour");
    check::equal(outcome.survivors[2].taken_by, std::string(""), "the city out of reach is free");
    check::close(outcome.survivors[0].population_estimate, 1000.0, 1e-12,
                 "and nobody is missing, which is the horror");
}

TEST(void_unmakes_a_city_and_the_four_modes_are_all_distinct) {
    Bench bench = void_world();
    const CorruptionOutcome outcome = corrupt_world(bench.request());
    check::equal(outcome.god_id, std::string("god_unwriting"), "the Unwriting");
    check::equal(outcome.new_ruins.size(), std::size_t{2}, "two cities unmade");
    check::equal(outcome.new_ruins[0].cause, std::string("void_unmade"), "a different cause from rot");
    check::equal(outcome.new_ruins[0].school, std::string("void"), "and a different school");
    check::equal(outcome.survivors.size(), std::size_t{1}, "one city out of reach");
}

// --- the revelation --------------------------------------------------------

TEST(a_return_makes_the_intruder_walk_and_rouses_the_gods_that_oppose_it) {
    Bench bench = blood_world();
    const HiddenGod& thirst = hidden("god_long_thirst");
    check::equal(status_of(bench.religion, thirst.id), std::string("absent"),
                 "no trace of it before the return");

    const CorruptionOutcome outcome = corrupt_world(bench.request());
    const GodRecord* intruder = record_of(outcome.religion, thirst.id);
    check::is_true(intruder != nullptr, "the record to exist");
    check::equal(intruder->status, std::string("walking"), "it walks");
    // Pushed Wild, which is what makes the cure able to become worse than the disease: the
    // aspect factor doubles for Wild and halves for Sovereign, so a Wild god destroys four
    // times as much as a Sovereign one.
    check::equal(intruder->aspect, std::string("wild"), "in its Wild aspect");
    check::equal(intruder->aspect_name, std::string(thirst.aspects.wild), "and under its Wild name");

    check::is_true(!outcome.record->roused.empty(), "the pantheon to have noticed");
    for (const std::string& id : outcome.record->roused) {
        bool is_rival = false;
        for (const std::string_view rival : thirst.rivals) {
            if (rival == id) is_rival = true;
        }
        check::is_true(is_rival, id + " to be a rival of the intruder");
        const GodRecord* roused = record_of(outcome.religion, id);
        const PantheonGod* known = pantheon_god(id);
        if (known != nullptr && !known->aspects.wild.empty()) {
            check::equal(roused->aspect, std::string("wild"), id + " pushed to its Wild face");
        }
    }
    // A god that is absent from this world is not roused by anything.
    for (const GodRecord& god : outcome.religion.gods) {
        if (god.status != "absent") continue;
        const bool listed = std::find(outcome.record->roused.begin(), outcome.record->roused.end(),
                                      god.id) != outcome.record->roused.end();
        check::is_true(!listed, god.id + " is absent and cannot be roused");
    }
}

TEST(the_revelation_is_idempotent_so_it_can_be_replayed_after_a_rebuild) {
    // The reference runs the revelation AFTER the rebuild re-resolves the pantheon,
    // precisely so the resolve cannot overwrite it. A caller here owes the rebuild, so it
    // has to be able to replay this -- and replaying it must change nothing.
    Bench bench = blood_world();
    CorruptionOutcome outcome = corrupt_world(bench.request());
    const std::vector<std::string> first = outcome.record->roused;

    const std::vector<std::string> again =
        reveal_hidden_god(outcome.religion, "god_long_thirst", "villain-1-11", bench.age);
    check::is_true(!again.empty(), "something to have been roused at all");
    check::equal(again.size(), first.size(), "the same gods");
    for (std::size_t i = 0; i < again.size(); ++i) {
        check::equal(again[i], first[i], "rousing " + std::to_string(i));
    }
    check::equal(status_of(outcome.religion, "god_long_thirst"), std::string("walking"), "still walking");
    check::throws([&] { reveal_hidden_god(outcome.religion, "god_weave", "villain-1-11", 2); },
                  "a known god cannot be revealed as a hidden one");
}

TEST(the_host_is_bound_to_the_hidden_god_and_its_school_becomes_hidden) {
    Bench bench = blood_world();
    const CorruptionOutcome outcome = corrupt_world(bench.request());
    const Villain& host = outcome.villains.people[0];

    check::equal(host.uid, outcome.villain_uid, "the host the record names");
    check::equal(host.god, std::string("god_long_thirst"), "sworn to its new patron");
    // The host's school BECOMES a hidden school, which is what makes its next well sink
    // into a network nothing else in the library can reach.
    check::equal(host.school, std::string("blood"), "and its ground with it");
    check::is_true(host.corrupted, "and marked, so no second god takes it by accident");
    check::equal(host.held_nodes.size(), std::size_t{5}, "its old anchor plus four new nodes");
    check::is_true(!host.log.empty(), "a log entry");
    check::equal(host.log.back().event, std::string("corrupted"), "saying what happened");
    check::is_true(host.log.back().encounters.has_value(), "and at what point in the ledger");

    check::equal(outcome.corruptions.size(), std::size_t{1}, "one record on the world");
    check::equal(outcome.corruptions[0].index, 0, "keyed by its own position");
    check::is_true(!outcome.corruptions[0].cleansed_age.has_value(), "and standing");
    check::is_true(outcome.rebuild_required, "and the caller owes the tail rebuild");
}

// --- cleansing -------------------------------------------------------------

namespace {

// A corrupted world, ready to be cleansed.
CleanseRequest corrupted_world(int age = 3, double power = kCleanseDefaultPower) {
    Bench bench = blood_world();
    const CorruptionOutcome outcome = corrupt_world(bench.request());
    check::is_true(outcome.acted, "the fixture to have corrupted something");
    CleanseRequest r;
    r.age = age;
    r.power = power;
    r.corruption = 0;
    r.magic = outcome.magic;
    r.religion = outcome.religion;
    r.villains = outcome.villains;
    r.corruptions = outcome.corruptions;
    return r;
}

}  // namespace

TEST(the_default_power_drains_the_seat_in_four_ticks_and_the_ring_in_three) {
    CleanseRequest request = corrupted_world();
    check::equal(request.magic.networks.at("blood").nodes.size(), std::size_t{4}, "four to start");

    const int expected_remaining[4] = {4, 4, 1, 0};
    for (int tick = 0; tick < 4; ++tick) {
        const CleanseOutcome outcome = cleanse_world(request);
        check::equal(outcome.remaining, expected_remaining[tick],
                     "nodes left after tick " + std::to_string(tick + 1));
        check::equal(outcome.magic.networks.at("blood").nodes.size(),
                     static_cast<std::size_t>(expected_remaining[tick]),
                     "and the network agrees at tick " + std::to_string(tick + 1));
        check::equal(outcome.cleansed, tick == 3, "cleansed at tick " + std::to_string(tick + 1));
        request.magic = outcome.magic;
        request.religion = outcome.religion;
        request.villains = outcome.villains;
        request.corruptions = outcome.corruptions;
    }
    // Driven down rather than out-competed: potency saturates at a network's strength, so a
    // counter-node of equal strength could never reach a node at full intensity.
    check::equal(request.corruptions[0].nodes.size(), std::size_t{0}, "the record emptied");
    check::is_true(request.corruptions[0].cleansed_age.has_value(), "and closed");
    check::equal(*request.corruptions[0].cleansed_age, 3, "in the age it ended");
}

TEST(the_holder_is_unseated_with_its_last_node_and_not_before) {
    CleanseRequest request = corrupted_world();
    const CleanseOutcome partial = cleanse_world(request);
    check::is_true(!partial.cleansed, "one tick is not enough");
    check::is_true(partial.villains.people[0].corrupted, "so the holder is still held");
    check::equal(partial.villains.people[0].god, std::string("god_long_thirst"), "and still sworn");
    check::equal(status_of(partial.religion, "god_long_thirst"), std::string("walking"), "and it walks");

    // Power one ends a full-intensity node in a single tick, which is what the drain
    // ceiling is for.
    CleanseRequest whole = corrupted_world(3, 1.0);
    const CleanseOutcome done = cleanse_world(whole);
    check::is_true(done.cleansed, "power one ends the whole cluster at once");
    check::equal(done.removed.size(), std::size_t{4}, "every node driven to zero");
    check::equal(done.magic.networks.at("blood").nodes.size(), std::size_t{0}, "the school empty again");

    const Villain& freed = done.villains.people[0];
    check::is_true(!freed.corrupted, "the holder unseated with its last node");
    check::equal(freed.god, std::string(""), "and released from its patron");
    check::equal(freed.held_nodes.size(), std::size_t{1}, "holding only what it held before");
    check::equal(freed.log.back().event, std::string("cleansed"), "and the reign records it");
    check::equal(status_of(done.religion, "god_long_thirst"), std::string("sleeping"),
                 "the god put back to sleep rather than unmade");
    check::is_true(record_of(done.religion, "god_long_thirst")->evidence.last_seen,
                   "with the evidence marked historic");
}

TEST(draining_a_node_remaps_the_edges_that_survive_it) {
    // A DEPARTURE FROM THE REFERENCE, and the reason is here. Its guard tests whether an
    // edge endpoint is a string; endpoints are integer positions everywhere, so the guard
    // never fires -- while a corrupted host's next well sinks into its now-hidden school
    // and joins edges to exactly these nodes. Removing them by filter leaves every later
    // endpoint index pointing at the wrong node.
    Bench bench = blood_world();
    bench.magic.networks["blood"].nodes.push_back({"blood-anchor", lean(10.0), 2.0});
    bench.villains.people[0].held_nodes.push_back("blood-anchor");
    const CorruptionOutcome corrupted = corrupt_world(bench.request());
    check::is_true(corrupted.acted, "the fixture to have corrupted something");

    // The host now holds a hidden school, so its next well sinks into it and joins every
    // node it holds -- including the four that are about to be drained away.
    VillainRequest works_request;
    works_request.age = 4;
    works_request.globe_radius_m = kRadius;
    works_request.settlement_spacing_m = kSpacing;
    works_request.cities = bench.cities;
    works_request.magic = &corrupted.magic;
    works_request.villains = corrupted.villains;
    const VillainWorks works = build_villain_works(works_request);
    const LeyNetwork& before = works.magic.networks.at("blood");
    check::equal(before.nodes.size(), std::size_t{6}, "an anchor, four corrupt nodes and a well");
    check::equal(before.edges.size(), std::size_t{5}, "joined to every node the host holds");

    CleanseRequest request;
    request.age = 5;
    request.power = 1.0;
    request.corruption = 0;
    request.magic = works.magic;
    request.religion = corrupted.religion;
    request.villains = works.block;
    request.corruptions = corrupted.corruptions;
    const CleanseOutcome outcome = cleanse_world(request);

    const LeyNetwork& after = outcome.magic.networks.at("blood");
    check::equal(after.nodes.size(), std::size_t{2}, "the anchor and the well are left");
    check::equal(after.edges.size(), std::size_t{1}, "and the one line between them");
    for (const LeyEdge& edge : after.edges) {
        check::is_true(edge.from >= 0 && edge.from < static_cast<int>(after.nodes.size()),
                       "an endpoint inside the node list");
        check::is_true(edge.to >= 0 && edge.to < static_cast<int>(after.nodes.size()),
                       "at both ends");
        // The remap is only correct if the edge still joins the SAME two nodes it did.
        check::equal(after.nodes[static_cast<std::size_t>(edge.from)].id,
                     std::string("well-villain-1-11"), "still leaving the well");
        check::equal(after.nodes[static_cast<std::size_t>(edge.to)].id, std::string("blood-anchor"),
                     "and still landing on the anchor");
    }
    check::is_true(node_of(after, "blood-anchor") != nullptr, "the node the world laid down survives");
    check::close(node_of(after, "blood-anchor")->intensity, 2.0, 1e-12, "at its own intensity");
}

TEST(a_god_revealed_by_corruption_is_opposed_through_its_corruption) {
    CleanseRequest request = corrupted_world();
    check::equal(status_of(request.religion, "god_long_thirst"), std::string("walking"),
                 "the fixture to have a god on the plane");
    request.corruption.reset();
    request.god_id = "god_long_thirst";
    // Two different things set a god walking and only one of them writes a visitation
    // record. Departure needs the record, so this is refused BY NAME rather than reaching
    // an unguarded lookup.
    check::throws([&] { cleanse_world(request); }, "a corrupted god sent home");

    CleanseRequest sleeping = corrupted_world();
    sleeping.corruption.reset();
    sleeping.god_id = "god_weave";
    check::throws([&] { cleanse_world(sleeping); }, "a god that does not walk at all");

    CleanseRequest stranger = corrupted_world();
    stranger.corruption.reset();
    stranger.god_id = "god_nobody";
    check::throws([&] { cleanse_world(stranger); }, "a god this world has never heard of");
}

TEST(a_cleanse_refuses_a_target_it_cannot_act_on) {
    CleanseRequest both = corrupted_world();
    both.god_id = "god_long_thirst";
    check::throws([&] { cleanse_world(both); }, "a target naming two things");

    CleanseRequest neither = corrupted_world();
    neither.corruption.reset();
    check::throws([&] { cleanse_world(neither); }, "a target naming nothing");

    CleanseRequest missing = corrupted_world();
    missing.corruption = 4;
    check::throws([&] { cleanse_world(missing); }, "a record this world does not carry");

    for (const double power : {0.0, -0.5, 1.5}) {
        CleanseRequest bad = corrupted_world();
        bad.power = power;
        check::throws([&] { cleanse_world(bad); }, "a power outside its range");
    }

    CleanseRequest twice = corrupted_world(3, 1.0);
    const CleanseOutcome done = cleanse_world(twice);
    check::is_true(done.cleansed, "the fixture to have finished the job");
    twice.corruptions = done.corruptions;
    check::throws([&] { cleanse_world(twice); }, "a record that was already cleansed");
}

// --- the budget ------------------------------------------------------------

TEST(the_node_budget_refuses_a_world_that_is_already_full) {
    Bench bench = blood_world();
    for (int i = 0; i < 24; ++i) {
        CorruptionRecord record;
        record.index = i;
        record.god_id = "god_long_thirst";
        record.school = "blood";
        record.nodes = {"a", "b", "c", "d"};
        bench.corruptions.push_back(record);
    }
    check::equal(24 * 4, kCorruptionNodeBudget, "twenty-four concurrent corruptions fill it");
    check::throws([&] { corrupt_world(bench.request()); }, "a world already at the budget");

    // Cleansing frees them, which is the whole reason the budget is a count of live nodes
    // rather than a count of records.
    bench.corruptions[0].cleansed_age = 7;
    const CorruptionOutcome freed = corrupt_world(bench.request());
    check::is_true(freed.acted, "a world with room again");
    check::equal(freed.corruption, 24, "and the new record takes the next index");

    // The reference's budget counter tests the age for TRUTH rather than for presence, so a
    // record cleansed in age 0 still counts against it -- while its own cleanse validator
    // two hundred lines later tests for presence. They disagreed; this is the one that is
    // right, and it is a deliberate departure.
    bench.corruptions[0].cleansed_age = 0;
    check::is_true(corrupt_world(bench.request()).acted, "a record cleansed in age zero frees its nodes");
}
