// War invariants.
//
// Built against a hand-made front rather than a generated world, because every property here
// is a distance in spacings, a tie-break, a draw count or a skip rule -- and a generated world
// would make each of them depend on thirteen stages upstream. The globe is 1000 m in radius
// so that one metre of arc is one metre in the test.
#include "check.hpp"

#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "fwg/random.hpp"
#include "fwg/wars.hpp"

using namespace fwg;

namespace {

constexpr double kRadius = 1000.0;
constexpr double kSpacing = 450.0;  // so the proximity limit is 675 m

// A unit direction `metres` of arc along the equator from (1, 0, 0).
Vec3 at(double metres, double radius = kRadius) {
    const double angle = metres / radius;
    return {std::cos(angle), std::sin(angle), 0.0};
}

WarCity city(const std::string& uid, const std::string& profile, const std::string& parent, int id,
             const Vec3& where, long long population = 100) {
    WarCity c;
    c.uid = uid;
    c.id = id;
    c.node = id;
    c.x = id;
    c.z = 0;
    c.direction = where;
    c.name = uid;
    c.population_profile = profile;
    c.parent_race_id = parent;
    c.population_estimate = population;
    return c;
}

CityCore core(int site_id, int forts = 0, double demand = 10.0, double supply = 10.0) {
    CityCore c;
    c.site_id = site_id;
    for (int i = 0; i < forts; ++i) {
        c.fortress_ids.push_back("fortress-" + std::to_string(site_id) + "-" + std::to_string(i));
    }
    c.food_demand = demand;
    c.food_supply = supply;
    return c;
}

Road road(int from, int to) {
    Road r;
    r.from = from;
    r.to = to;
    return r;
}

// Every known school's field at zero, three cells wide: a city's x is its id, z is 0.
std::map<std::string, Grid> quiet_layers() {
    std::map<std::string, Grid> out;
    for (const char* school : {"weave", "umbral", "infernal", "radiant", "fire", "water", "earth", "air"}) {
        out["ley_" + std::string(school)] = Grid(3, 0.0);
    }
    return out;
}

Villain villain(const std::string& uid, const Vec3& where, double reach_m, const std::string& status = "living") {
    Villain v;
    v.uid = uid;
    v.region = "region-" + uid;
    v.seat_uid = "seat-of-" + uid;
    v.direction = where;
    v.tier = 1.0;
    v.reach_m = reach_m;
    v.growth = "devouring";
    v.status = status;
    return v;
}

// Everything a war reads, owned in one place so the request's pointers have something to
// point at. The request must not outlive the front.
struct Front {
    std::vector<WarCity> cities;
    std::vector<CityCore> cores;
    std::vector<Road> routes;
    std::map<std::string, Grid> layers = quiet_layers();
    std::vector<Villain> villains;
    double radius = kRadius;
    double spacing = kSpacing;

    WarGround ground() const {
        WarGround g;
        g.globe_radius_m = radius;
        g.settlement_spacing_m = spacing;
        g.cities = cities;
        g.cores = &cores;
        g.routes = &routes;
        g.villains = villains;
        return g;
    }

    WarRequest request(std::optional<double> roll = 0.0, int age = 1, std::uint32_t seed = 42) const {
        WarRequest r;
        r.ground = ground();
        r.seed = seed;
        r.age = age;
        r.layers = &layers;
        if (roll.has_value()) r.rolls = std::map<std::string, double>{{"*", *roll}};
        return r;
    }

    void add(const WarCity& c) {
        cities.push_back(c);
        cores.push_back(core(c.id));
    }
};

// A strong gnome city and a weak elven one, 200 m apart: one contested pair.
Front strong_and_weak() {
    Front f;
    f.add(city("strong", "gnome", "dwarf", 0, at(0.0), 400));
    f.add(city("weak", "elf", "elf", 1, at(200.0), 40));
    return f;
}

std::vector<WarPair> pairs_at(double metres) {
    Front f;
    f.add(city("a", "gnome", "dwarf", 0, at(0.0)));
    f.add(city("b", "elf", "elf", 1, at(metres)));
    return contested_pairs(f.ground());
}

}  // namespace

// --- what a war is ---------------------------------------------------------

TEST(a_war_is_named_for_the_relationship_between_its_cities) {
    const WarCity gnome = city("a", "gnome", "dwarf", 0, at(0.0));
    const WarCity gnome_two = city("b", "gnome", "dwarf", 1, at(0.0));
    const WarCity hill = city("c", "hill_dwarf", "dwarf", 2, at(0.0));
    const WarCity elf = city("d", "elf", "elf", 3, at(0.0));
    check::equal(war_kind(gnome, gnome_two), kWarCivil, "one civilization");
    check::equal(war_kind(gnome, hill), kWarRegional, "one parent race");
    check::equal(war_kind(gnome, elf), kWarInternational, "two parent races");
    // Symmetric: which city is asked first cannot change the kind.
    check::equal(war_kind(hill, gnome), kWarRegional, "regional either way round");
    check::equal(war_kind(elf, gnome), kWarInternational, "international either way round");
}

// --- contention --------------------------------------------------------------

TEST(nearer_than_one_and_a_half_spacings_is_contested_and_farther_is_not) {
    const std::vector<WarPair> inside = pairs_at(300.0);
    check::equal(inside.size(), std::size_t{1}, "a pair inside the limit");
    // 1 - 300 / (1.5 * 450): the limit is a multiple of the spacing, not a metre figure.
    check::close(inside[0].proximity_pressure, 1.0 - 300.0 / 675.0, 1e-9, "proximity pressure");
    check::close(inside[0].pressure, inside[0].proximity_pressure, 1e-12, "and nothing else pressing");
    check::close(inside[0].distance_m, 300.0, 1e-6, "the separation, in metres of arc");
    check::equal(inside[0].kind, std::string(kWarInternational), "the relationship");
    check::equal(pairs_at(674.0).size(), std::size_t{1}, "just inside the limit");
    // Beyond the limit proximity alone is not a quarrel.
    check::equal(pairs_at(676.0).size(), std::size_t{0}, "just outside it");
    check::equal(pairs_at(700.0).size(), std::size_t{0}, "well outside it");
}

TEST(a_hungry_city_contests_only_the_neighbour_its_road_reaches) {
    Front f;
    f.cities = {city("a", "gnome", "dwarf", 0, at(0.0)), city("b", "gnome", "dwarf", 1, at(5000.0))};
    f.cores = {core(0), core(1, 0, 10.0, 4.0)};
    f.routes = {road(0, 1)};
    const std::vector<WarPair> linked = contested_pairs(f.ground());
    check::equal(linked.size(), std::size_t{1}, "the road-linked hungry pair");
    check::equal(linked[0].proximity_pressure, 0.0, "far beyond the proximity limit");
    check::close(linked[0].supply_pressure, 0.6, 1e-12, "the hungrier city's shortfall");
    check::equal(linked[0].kind, std::string(kWarCivil), "one civilization");

    // Roads are listed one way; hunger reaches along them both ways.
    f.routes = {road(1, 0)};
    check::equal(contested_pairs(f.ground()).size(), std::size_t{1}, "a road listed the other way");
    // Hunger with no road between them is nothing they can take from each other.
    f.routes.clear();
    check::equal(contested_pairs(f.ground()).size(), std::size_t{0}, "no road, no quarrel");
    // A road between two cities that both feed themselves is just a road.
    f.routes = {road(0, 1)};
    f.cores = {core(0), core(1)};
    check::equal(contested_pairs(f.ground()).size(), std::size_t{0}, "both fed");
}

TEST(a_road_joins_cities_by_site_id_and_never_by_list_position_or_node) {
    // Site ids 5 and 9 at list positions 1 and 0, on terrain nodes 0 and 1. A road names
    // SITES, so a lookup by position or by node would link the wrong pair and read the wrong
    // city's hunger.
    Front f;
    f.cities = {city("b", "gnome", "dwarf", 9, at(5000.0)), city("a", "gnome", "dwarf", 5, at(0.0))};
    f.cities[0].node = 1;
    f.cities[1].node = 0;
    f.cores = {core(5), core(9, 0, 10.0, 4.0)};
    f.routes = {road(0, 1)};  // positions, not sites: links nothing here
    check::equal(contested_pairs(f.ground()).size(), std::size_t{0}, "no road between sites 5 and 9");
    f.routes = {road(9, 5)};
    const std::vector<WarPair> linked = contested_pairs(f.ground());
    check::equal(linked.size(), std::size_t{1}, "the road between sites 5 and 9");
    check::close(linked[0].supply_pressure, 0.6, 1e-12, "site 9's shortfall, wherever it is listed");
}

TEST(shortfall_is_the_pre_trade_share_a_city_cannot_feed) {
    const CityCore fed = core(0, 0, 10.0, 10.0);
    const CityCore short_quarter = core(0, 0, 10.0, 2.5);
    const CityCore surplus = core(0, 0, 10.0, 40.0);
    const CityCore idle = core(0, 0, 0.0, 0.0);
    check::equal(war_shortfall(&fed), 0.0, "a city that feeds itself");
    check::close(war_shortfall(&short_quarter), 0.75, 1e-12, "three quarters unfed");
    check::equal(war_shortfall(&surplus), 0.0, "a surplus is not negative pressure");
    check::equal(war_shortfall(&idle), 0.0, "no demand is not hunger");
    check::equal(war_shortfall(nullptr), 0.0, "no hinterland at all");
    // PRE-TRADE: imports that close the gap do not hide it. The city living off its
    // neighbour's grain is exactly the city under pressure.
    CityCore fed_by_trade = short_quarter;
    fed_by_trade.food_imports = 7.5;
    fed_by_trade.food_deficit = 0.0;
    check::close(war_shortfall(&fed_by_trade), 0.75, 1e-12, "the shortfall before trade");
}

TEST(pairs_come_strongest_first_and_an_exact_tie_goes_to_uid_order) {
    Front f;
    f.add(city("a", "gnome", "dwarf", 0, at(0.0)));
    f.add(city("b", "gnome", "dwarf", 1, at(100.0)));
    f.add(city("c", "gnome", "dwarf", 2, at(400.0)));
    const std::vector<WarPair> pairs = contested_pairs(f.ground());
    check::equal(pairs.size(), std::size_t{3}, "all three pairs within the limit");
    check::equal(pairs[0].a_uid + pairs[0].b_uid, std::string("ab"), "100 m first");
    check::equal(pairs[1].a_uid + pairs[1].b_uid, std::string("bc"), "300 m second");
    check::equal(pairs[2].a_uid + pairs[2].b_uid, std::string("ac"), "400 m last");

    // Two pairs at exactly the same separation, generated in the wrong order: (a, c) is
    // examined before (a, b). Only the uid tie-break can put (a, b) first.
    Front tie;
    tie.add(city("a", "gnome", "dwarf", 0, at(0.0)));
    tie.add(city("c", "gnome", "dwarf", 1, at(-500.0)));
    tie.add(city("b", "gnome", "dwarf", 2, at(500.0)));
    const std::vector<WarPair> tied = contested_pairs(tie.ground());
    check::equal(tied.size(), std::size_t{2}, "two pairs; c and b are 1000 m apart");
    check::equal(tied[0].pressure, tied[1].pressure, "the floor: an exact tie on pressure");
    check::equal(tied[0].b_uid, std::string("b"), "the lower uid first");
    check::equal(tied[1].b_uid, std::string("c"), "the higher uid second");
}

TEST(a_villain_presses_together_only_a_pair_its_shadow_covers) {
    const WarCity a = city("a", "gnome", "dwarf", 0, at(100.0));
    const WarCity b = city("b", "elf", "elf", 1, at(300.0));
    const WarCity beyond = city("beyond", "elf", "elf", 2, at(600.0));
    const Villain seated = villain("v", at(0.0), 500.0);
    // The FARTHER of the two decides: .35 * (1 - 300 / 500).
    check::close(war_villain_pressure(a, b, {seated}, kRadius), 0.35 * (1.0 - 300.0 / 500.0), 1e-9,
                 "contention under one shadow");
    check::equal(war_villain_pressure(a, beyond, {seated}, kRadius), 0.0, "one city outside the reach");
    check::equal(war_villain_pressure(a, b, {villain("v", at(0.0), 0.0)}, kRadius), 0.0, "no reach");
    // The worst single villain, not a sum.
    const Villain nearer = villain("w", at(200.0), 500.0);
    const double worst = war_villain_pressure(a, b, {seated, nearer}, kRadius);
    check::close(worst, 0.35 * (1.0 - 100.0 / 500.0), 1e-9, "the worst of two, not their sum");

    Front f;
    f.add(a);
    f.add(b);
    f.villains = {seated};
    const std::vector<WarPair> pressed = contested_pairs(f.ground());
    check::equal(pressed.size(), std::size_t{1}, "the pair");
    check::close(pressed[0].villain_pressure, 0.14, 1e-9, "the villain's share");
    check::close(pressed[0].pressure, 1.0 - 200.0 / 675.0 + 0.14, 1e-9, "added to proximity");

    // A villain presses a pair together; it cannot create one.
    Front apart;
    apart.add(city("x", "gnome", "dwarf", 0, at(0.0)));
    apart.add(city("y", "elf", "elf", 1, at(2000.0)));
    apart.villains = {villain("v", at(1000.0), 5000.0)};
    check::equal(contested_pairs(apart.ground()).size(), std::size_t{0}, "no pair from a shadow alone");
}

// --- fighting ------------------------------------------------------------

TEST(the_chance_to_fight_rises_with_pressure_and_the_authored_cap_never_binds) {
    check::close(war_chance(0.0), 0.5, 1e-12, "a bare quarrel is a coin toss");
    check::close(war_chance(0.5), 0.7, 1e-12, "half pressure");
    check::close(war_chance(1.0), 0.9, 1e-12, "full pressure");
    check::close(war_chance(5.0), 0.9, 1e-12, "pressure is clamped before it is scaled");
    check::close(war_chance(-1.0), 0.5, 1e-12, "and floored");
    // Pinned so that raising the gain to where the cap bites cannot land silently: the cap is
    // authored at .92 and nothing can reach it.
    check::is_true(war_chance(1.0) < kWarMaxChance, "the authored cap to be unreachable");
}

TEST(strength_is_people_plus_fifty_residents_a_standing_fort) {
    const WarCity c = city("a", "gnome", "dwarf", 0, at(0.0), 100);
    const CityCore bare = core(0);
    const CityCore walled = core(0, 2);
    check::equal(war_strength(c, &bare), 100LL, "people alone");
    check::equal(war_strength(c, &walled), 200LL, "two forts");
    check::equal(war_strength(c, nullptr), 100LL, "no hinterland, no forts");
}

TEST(the_stronger_city_wins_and_the_loser_falls_to_the_war) {
    const Front f = strong_and_weak();
    const WarResolution result = resolve_wars(f.request(0.0));
    check::equal(result.wars.size(), std::size_t{1}, "one war");
    const WarRecord& war = result.wars[0];
    check::equal(war.id, std::string("war-1-0"), "the first war of age 1");
    check::equal(war.kind, std::string(kWarInternational), "gnome against elf");
    check::equal(war.victor_uid, std::string("strong"), "the victor");
    check::equal(war.defeated_uid, std::string("weak"), "the defeated");
    check::equal(war.participants[0], std::string("strong"), "participants in pair order");
    check::equal(war.participants[1], std::string("weak"), "both of them");
    check::equal(war.victor_strength, 400LL, "the victor's strength");
    check::equal(war.defeated_strength, 40LL, "the loser's");
    check::close(war.chance, war_chance(war.pressure), 1e-12, "the chance it was drawn against");
    check::equal(war.roll, 0.0, "the seam's roll");
    check::is_true(war.outcome.empty() && !war.survival_chance.has_value(),
                   "no survival fields at zero survival");

    check::equal(result.fates.size(), std::size_t{1}, "one city lost");
    check::is_true(result.fates.count("strong") == 0, "and not the victor");
    const WarFate& fate = result.fates.at("weak");
    check::equal(fate.cause, std::string("war_international"), "the cause names the war");
    check::equal(fate.war_id, war.id, "and which war");
    check::equal(fate.opponent_uid, std::string("strong"), "who took it");
    check::equal(fate.opponent_civilization_id, std::string("gnome"), "the victor's people");
    check::equal(fate.opponent_parent_race_id, std::string("dwarf"), "and race");
    check::close(fate.probability, war.chance, 1e-12, "the fate's probability is the war's chance");
    check::equal(fate.roll, war.roll, "and its roll the war's roll");
    check::equal(fate.reason, war.reason, "one sentence for the war and the ruin");
    check::close(fate.distance_m, 200.0, 1e-6, "the separation the pair was measured at");
}

TEST(each_kind_of_war_is_told_in_its_own_sentence) {
    // The sentence is the ruin's `reason`, read by every story layer. Swapping two would pass
    // every structural test while telling the wrong war.
    Front civil;
    civil.add(city("a", "gnome", "dwarf", 0, at(0.0), 400));
    civil.add(city("b", "gnome", "dwarf", 1, at(200.0), 40));
    Front regional;
    regional.add(city("a", "gnome", "dwarf", 0, at(0.0), 400));
    regional.add(city("b", "hill_dwarf", "dwarf", 1, at(200.0), 40));
    const Front international = strong_and_weak();
    check::equal(resolve_wars(civil.request(0.0)).wars.at(0).reason,
                 std::string("Two cities of one civilization fell out over contested ground and supply, "
                             "and only one survived the war."),
                 "a civil war");
    check::equal(resolve_wars(regional.request(0.0)).wars.at(0).reason,
                 std::string("Kindred civilizations of one parent race fought a regional war over "
                             "contested ground and supply."),
                 "a regional war");
    check::equal(resolve_wars(international.request(0.0)).wars.at(0).reason,
                 std::string("Cities of different parent races fought an international war over "
                             "contested ground and supply."),
                 "an international war");
}

TEST(standing_forts_can_turn_a_war_the_people_alone_would_lose) {
    Front f = strong_and_weak();
    f.cores[1] = core(1, 8);  // 40 people and eight forts: 440 against 400
    const WarResolution result = resolve_wars(f.request(0.0));
    check::equal(result.wars.size(), std::size_t{1}, "one war");
    check::equal(result.wars[0].victor_uid, std::string("weak"), "the walled city holds");
    check::equal(result.wars[0].victor_strength, 440LL, "forty people and eight forts");
}

TEST(an_exact_tie_in_strength_goes_to_the_lower_uid_wherever_it_is_listed) {
    Front f;
    f.add(city("b-city", "gnome", "dwarf", 0, at(0.0), 100));
    f.add(city("a-city", "elf", "elf", 1, at(200.0), 100));
    const WarResolution result = resolve_wars(f.request(0.0));
    check::equal(result.wars.size(), std::size_t{1}, "one war");
    check::equal(result.wars[0].victor_strength, result.wars[0].defeated_strength, "the floor: a tie");
    check::equal(result.wars[0].victor_uid, std::string("a-city"), "the lower uid, listed second");
}

TEST(a_city_is_lost_once_however_many_neighbours_it_angers) {
    Front f;
    f.add(city("left", "gnome", "dwarf", 0, at(0.0), 500));
    f.add(city("middle", "elf", "elf", 1, at(200.0), 10));
    f.add(city("right", "gnome", "dwarf", 2, at(400.0), 500));
    const WarResolution result = resolve_wars(f.request(0.0));
    int middle_lost = 0;
    int involving_middle = 0;
    for (const WarRecord& war : result.wars) {
        if (war.defeated_uid == "middle") ++middle_lost;
        if (war.participants[0] == "middle" || war.participants[1] == "middle") ++involving_middle;
    }
    check::equal(middle_lost, 1, "lost exactly once");
    // The second pair it belongs to is SKIPPED, not fought and ignored.
    check::equal(involving_middle, 1, "never fought again once lost");
    // The floor: the third pair still fights, so the skip is per city and not a stop.
    check::equal(result.wars.size(), std::size_t{2}, "left and right still meet");
    check::equal(result.fates.count("middle"), std::size_t{1}, "one fate for it");
    check::equal(result.fates.at("right").opponent_uid, std::string("left"),
                 "an exact tie goes to the lower uid");
}

TEST(a_charged_city_leaves_the_school_that_already_held_its_ground) {
    Front plain = strong_and_weak();
    const WarResolution quiet = resolve_wars(plain.request(0.0));
    check::equal(quiet.fates.at("weak").new_node_school, std::string(), "quiet ground names no school");

    Front charged = strong_and_weak();
    charged.layers["ley_infernal"].at(1, 0) = 0.8;  // under the loser only
    const WarResolution magical = resolve_wars(charged.request(0.0));
    check::equal(magical.fates.at("weak").new_node_school, std::string("infernal"), "the school that held it");

    // Contested ground held by nobody: two schools within the margin.
    Front contested = charged;
    contested.layers["ley_fire"].at(1, 0) = 0.78;
    check::equal(resolve_wars(contested.request(0.0)).fates.at("weak").new_node_school, std::string(),
                 "no clear school, no key point");

    // Magic off means no key point, however charged the ground.
    WarRequest mundane = charged.request(0.0);
    mundane.magic_enabled = false;
    check::equal(resolve_wars(mundane).fates.at("weak").new_node_school, std::string(), "magic off");
}

TEST(a_high_roll_keeps_the_peace_and_a_roll_exactly_at_the_chance_does_too) {
    const Front f = strong_and_weak();
    const WarResolution peace = resolve_wars(f.request(1.0));
    check::equal(peace.wars.size(), std::size_t{0}, "no war");
    check::equal(peace.fates.size(), std::size_t{0}, "no fates");

    const double chance = war_chance(contested_pairs(f.ground())[0].pressure);
    check::equal(resolve_wars(f.request(chance)).wars.size(), std::size_t{0}, "a roll AT the chance");
    check::equal(resolve_wars(f.request(std::nextafter(chance, 0.0))).wars.size(), std::size_t{1},
                 "one ulp under it fights");
}

TEST(a_war_id_counts_wars_fought_and_not_pairs_examined) {
    // Two quarrels a world apart. The first pair draws a peace, so the second is examined
    // under the SAME id -- the ordinal has not moved -- and the seam's peace applies to it too.
    Front f;
    f.add(city("a", "gnome", "dwarf", 0, at(0.0)));
    f.add(city("b", "elf", "elf", 1, at(100.0)));
    f.add(city("c", "gnome", "dwarf", 2, at(3000.0)));
    f.add(city("d", "elf", "elf", 3, at(3200.0)));
    check::equal(contested_pairs(f.ground()).size(), std::size_t{2}, "the floor: two pairs");

    WarRequest shared = f.request(std::nullopt);
    shared.rolls = std::map<std::string, double>{{"war-1-0", 1.0}, {"*", 0.0}};
    check::equal(resolve_wars(shared).wars.size(), std::size_t{0},
                 "both pairs drew as war-1-0, because no war had been fought");

    WarRequest first = f.request(std::nullopt);
    first.rolls = std::map<std::string, double>{{"war-1-0", 0.0}, {"*", 1.0}};
    const WarResolution one = resolve_wars(first);
    check::equal(one.wars.size(), std::size_t{1}, "the first war fought, then the default peace");
    check::equal(one.wars[0].participants[0], std::string("a"), "the stronger pair drew first");

    const WarResolution both = resolve_wars(f.request(0.0));
    check::equal(both.wars.size(), std::size_t{2}, "two wars");
    check::equal(both.wars[1].id, std::string("war-1-1"), "the second war is the second ordinal");
}

TEST(a_present_seam_that_names_nothing_is_a_peace_and_not_the_seeded_draw) {
    const Front f = strong_and_weak();
    check::equal(resolve_wars(f.request(0.0)).wars.size(), std::size_t{1}, "the floor: this pair can fight");
    // A seed whose word for this pair is under its chance, so falling back to the seeded draw
    // would fight -- which is exactly what the empty seam must not do.
    const double chance = war_chance(contested_pairs(f.ground())[0].pressure);
    std::uint32_t fighting_seed = 0;
    for (std::uint32_t s = 1; s <= 50 && fighting_seed == 0; ++s) {
        if (Rng::child(s, "war-strong-weak", 1).random() < chance) fighting_seed = s;
    }
    check::is_true(fighting_seed != 0, "the floor: a seed whose stream would fight");
    check::equal(resolve_wars(f.request(std::nullopt, 1, fighting_seed)).wars.size(), std::size_t{1},
                 "and does, without a seam");
    WarRequest silent = f.request(std::nullopt, 1, fighting_seed);
    silent.rolls = std::map<std::string, double>{};
    check::equal(resolve_wars(silent).wars.size(), std::size_t{0}, "an empty seam rolls 1.0");
}

TEST(the_seeded_draw_is_one_word_from_the_pairs_own_stream) {
    const Front f = strong_and_weak();
    const double chance = war_chance(contested_pairs(f.ground())[0].pressure);
    int fought = 0;
    int kept = 0;
    for (std::uint32_t seed = 1; seed <= 40; ++seed) {
        const double expected = Rng::child(seed, "war-strong-weak", 1).random();
        const WarResolution result = resolve_wars(f.request(std::nullopt, 1, seed));
        if (expected < chance) {
            ++fought;
            check::equal(result.wars.size(), std::size_t{1}, "a war where the stream says so");
            check::equal(result.wars[0].roll, expected, "the war's roll is that word");
        } else {
            ++kept;
            check::equal(result.wars.size(), std::size_t{0}, "a peace where the stream says so");
        }
    }
    // Both outcomes must actually occur, or the comparison above proved nothing.
    check::is_true(fought > 0 && kept > 0, "the sample to contain both wars and peace");

    // The age is the variation: the same pair cannot repeat its roll in the next age.
    const WarResolution age1 = resolve_wars(f.request(std::nullopt, 1, 7));
    const WarResolution age2 = resolve_wars(f.request(std::nullopt, 2, 7));
    const double roll1 = Rng::child(7, "war-strong-weak", 1).random();
    const double roll2 = Rng::child(7, "war-strong-weak", 2).random();
    check::is_true(roll1 != roll2, "two ages, two rolls");
    if (!age1.wars.empty()) check::equal(age1.wars[0].roll, roll1, "age one's word");
    if (!age2.wars.empty()) check::equal(age2.wars[0].roll, roll2, "age two's word");
}

// --- survival --------------------------------------------------------------

TEST(zero_survival_draws_nothing_extra_and_keeps_the_old_record_shape) {
    const Front f = strong_and_weak();
    WarRequest plain = f.request(0.0);
    const WarResolution result = resolve_wars(plain);
    check::equal(result.wars.size(), std::size_t{1}, "the war");
    check::is_true(result.wars[0].outcome.empty(), "no outcome field");
    check::is_true(!result.wars[0].survival_chance.has_value(), "no survival chance");
    check::is_true(!result.wars[0].survival_roll.has_value(), "no survival roll");
    check::equal(result.fates.count("weak"), std::size_t{1}, "the loser falls");
}

TEST(a_spared_loser_stands_as_a_vassal_and_is_not_fought_again_this_age) {
    Front f = strong_and_weak();
    // A third city beside the loser. Were the vassal fair game it would be fought again.
    f.add(city("neighbour", "gnome", "dwarf", 2, at(350.0), 1000));

    WarRequest spared = f.request(0.0);
    spared.survival = 0.5;
    spared.survival_rolls = std::map<std::string, double>{{"*", 0.1}};
    const WarResolution vassal = resolve_wars(spared);
    const WarRecord* first = nullptr;
    int involving_weak = 0;
    for (const WarRecord& war : vassal.wars) {
        if (war.defeated_uid == "weak" && first == nullptr) first = &war;
        if (war.participants[0] == "weak" || war.participants[1] == "weak") ++involving_weak;
    }
    check::is_true(first != nullptr, "weak to lose a war");
    check::equal(first->outcome, std::string("vassalage"), "and survive it");
    check::is_true(first->survival_chance == 0.5, "the option recorded");
    check::is_true(first->survival_roll == 0.1, "and the roll");
    check::equal(vassal.fates.count("weak"), std::size_t{0}, "a vassal has no fate");
    check::equal(involving_weak, 1, "and is not fought again this age");

    WarRequest ruined = f.request(0.0);
    ruined.survival = 0.5;
    ruined.survival_rolls = std::map<std::string, double>{{"*", 0.5}};  // AT the chance
    const WarResolution fallen = resolve_wars(ruined);
    check::equal(fallen.wars[0].outcome, std::string("ruin"), "a roll at the chance is a ruin");
    check::is_true(!fallen.fates.empty(), "and the loser falls");
}

TEST(the_seeded_survival_draw_is_its_own_pair_stream) {
    const Front f = strong_and_weak();
    WarRequest request = f.request(0.0, 1, 42);
    request.survival = 0.5;
    const WarResolution result = resolve_wars(request);
    check::equal(result.wars.size(), std::size_t{1}, "the war");
    const double expected = Rng::child(42, "war-survival-strong-weak", 1).random();
    check::is_true(result.wars[0].survival_roll == expected, "the second word, from its own stream");
    check::equal(result.wars[0].outcome, std::string(expected < 0.5 ? "vassalage" : "ruin"), "and the outcome it gives");
}

// --- history -----------------------------------------------------------------

TEST(both_sides_carry_the_war_each_naming_the_other) {
    const Front f = strong_and_weak();
    const WarRecord war = resolve_wars(f.request(0.0)).wars.at(0);
    const WarParticipation won = war_participation(war, "strong");
    const WarParticipation lost = war_participation(war, "weak");
    check::equal(won.outcome, std::string("victor"), "the victor's line");
    check::equal(won.opponent_uid, std::string("weak"), "names the loser");
    check::equal(won.opponent_civilization_id, std::string("elf"), "and its people");
    check::equal(lost.outcome, std::string("defeated"), "the loser's line");
    check::equal(lost.opponent_uid, std::string("strong"), "names the victor");
    check::equal(lost.opponent_parent_race_id, std::string("dwarf"), "and its race");
    check::equal(won.war_id, war.id, "one war");
    check::equal(lost.age, 1, "one age");

    WarCity veteran = f.cities[0];
    veteran.war_history = {won, lost, won};
    check::equal(veteran_wars(veteran), 3, "every war, whatever the outcome");
}

// --- the forecast --------------------------------------------------------------

TEST(the_outlook_reports_recent_wars_living_enemies_and_the_strongest_contention) {
    Front f = strong_and_weak();
    f.add(city("far", "elf", "elf", 2, at(2000.0)));
    WarParticipation old;
    old.war_id = "war-1-0";
    old.age = 1;
    old.kind = "civil";
    old.outcome = "victor";
    old.opponent_uid = "gone";
    WarParticipation fresh;
    fresh.war_id = "war-2-0";
    fresh.age = 2;
    fresh.kind = "civil";
    fresh.outcome = "defeated";
    fresh.opponent_uid = "strong";
    f.cities[0].war_history = {old};
    f.cities[1].war_history = {fresh};

    const auto outlook = war_outlook(f.ground(), 2);
    check::equal(outlook.at("strong").wars_recent, 0, "its war was an age ago");
    check::equal(outlook.at("weak").wars_recent, 1, "one this age");
    check::is_true(!outlook.at("strong").enemy_living, "its opponent is a ruin");
    check::is_true(outlook.at("weak").enemy_living, "strong still stands");
    check::is_true(outlook.at("strong").war_risk > 0.0, "a risk from its neighbour");
    check::equal(outlook.at("strong").war_risk_opponent_uid, std::string("weak"), "named");
    check::equal(outlook.at("weak").war_risk_opponent_uid, std::string("strong"), "both ways");
    check::equal(outlook.at("strong").war_risk_kind, std::string(kWarInternational), "and the kind");
    check::equal(outlook.at("far").war_risk, 0.0, "a city nobody contests");
    check::equal(outlook.at("far").war_risk_opponent_uid, std::string(), "names nobody");
    // Proximity has no direction.
    check::equal(outlook.at("strong").war_hunger, 0.0, "no hunger from proximity");
    check::equal(outlook.at("weak").war_hunger, 0.0, "on either side");
    // Before any age has turned nothing is recent.
    check::equal(war_outlook(f.ground(), std::nullopt).at("weak").wars_recent, 0, "no age, nothing recent");
}

TEST(a_hungry_city_carries_the_hunger_and_its_road_neighbour_the_risk) {
    Front f;
    f.cities = {city("fed", "gnome", "dwarf", 0, at(0.0)), city("hungry", "gnome", "dwarf", 1, at(1000.0))};
    f.cores = {core(0, 0, 10.0, 10.0), core(1, 0, 10.0, 2.0)};
    f.routes = {road(0, 1)};
    const auto outlook = war_outlook(f.ground(), 2);
    check::close(outlook.at("hungry").war_hunger, 0.8, 1e-12, "the hungry city presses");
    check::equal(outlook.at("hungry").war_hunger_target_uid, std::string("fed"), "on its neighbour");
    check::equal(outlook.at("hungry").war_risk, 0.0, "and is not itself at risk from it");
    check::close(outlook.at("fed").war_risk, 0.8, 1e-12, "the fed city is at risk");
    check::equal(outlook.at("fed").war_risk_opponent_uid, std::string("hungry"), "from the hungry one");
    check::equal(outlook.at("fed").war_hunger, 0.0, "and hungers for nothing");
}

TEST(hunger_has_a_direction_only_where_supply_outweighs_proximity_and_one_city_is_hungrier) {
    // Two equally hungry road neighbours: supply presses them, but neither needs the other's
    // ground more, so both carry the risk and neither the hunger.
    Front even;
    even.cities = {city("p", "gnome", "dwarf", 0, at(0.0)), city("q", "gnome", "dwarf", 1, at(1000.0))};
    even.cores = {core(0, 0, 10.0, 5.0), core(1, 0, 10.0, 5.0)};
    even.routes = {road(0, 1)};
    const auto level = war_outlook(even.ground(), 1);
    check::close(level.at("p").war_risk, 0.5, 1e-12, "a supply pressure");
    check::close(level.at("q").war_risk, 0.5, 1e-12, "on both");
    check::equal(level.at("p").war_hunger + level.at("q").war_hunger, 0.0, "and no hunger either way");

    // Close neighbours, one of them a little hungry: proximity outweighs supply, so the pair
    // has no direction however unequal the need.
    Front crowded;
    crowded.cities = {city("p", "gnome", "dwarf", 0, at(0.0)), city("q", "gnome", "dwarf", 1, at(100.0))};
    crowded.cores = {core(0, 0, 10.0, 10.0), core(1, 0, 10.0, 8.0)};
    crowded.routes = {road(0, 1)};
    const std::vector<WarPair> pairs = contested_pairs(crowded.ground());
    check::is_true(pairs[0].proximity_pressure > pairs[0].supply_pressure && pairs[0].supply_pressure > 0.0,
                   "the floor: proximity outweighs a real supply pressure");
    // .85 of proximity and .2 of supply is more than one, and a pair's pressure is capped there.
    check::is_true(pairs[0].proximity_pressure + pairs[0].supply_pressure > 1.0, "the floor: an overflow");
    check::equal(pairs[0].pressure, 1.0, "the pair's pressure capped at one");
    const auto pressed = war_outlook(crowded.ground(), 1);
    check::equal(pressed.at("q").war_hunger, 0.0, "the hungrier city carries no hunger");
    check::equal(pressed.at("q").war_risk, 1.0, "both carry the capped risk");
    check::equal(pressed.at("p").war_risk, 1.0, "on either side");
}

TEST(a_near_tie_inside_the_rounding_names_the_later_weaker_neighbour_as_the_reference_does) {
    // The stored risk is ROUNDED and the comparison is not. A weaker pair whose pressure lies
    // inside the rounding of the stronger one replaces its neighbour. Pinned so a fix -- which
    // renames the forecast's opponent in exactly these worlds -- cannot land silently.
    const double limit = kWarProximityFactor * kSpacing;
    Front f;
    f.add(city("x", "gnome", "dwarf", 0, at(0.0)));
    f.add(city("y", "gnome", "dwarf", 1, at((1.0 - 0.3000004) * limit)));
    f.add(city("z", "gnome", "dwarf", 2, at(-(1.0 - 0.3000002) * limit)));
    const std::vector<WarPair> pairs = contested_pairs(f.ground());
    check::equal(pairs.size(), std::size_t{2}, "x-y and x-z; y and z are too far apart");
    const double stronger = pairs[0].pressure;
    const double weaker = pairs[1].pressure;
    // The precondition, measured rather than assumed from the authored distances.
    check::equal(pairs[0].b_uid, std::string("y"), "x-y is the stronger pair");
    check::is_true(stronger > weaker, "a strict order");
    check::is_true(weaker > round_dp(stronger, 6), "the weaker inside the stronger's rounding");

    const auto outlook = war_outlook(f.ground(), 1);
    check::equal(outlook.at("x").war_risk_opponent_uid, std::string("z"), "the later, weaker neighbour");
    check::equal(outlook.at("x").war_risk, round_dp(weaker, 6), "at its rounded pressure");
}

// --- scale ---------------------------------------------------------------------

TEST(contention_is_measured_in_spacings_so_it_scales_with_the_planet) {
    // The same front in spacings, on the 11 km reference width and on the 200 km world.
    const double reference_radius = kRadius;
    const double reference_spacing = kSpacing;
    const double world_radius = 200000.0 / (2.0 * 3.14159265358979323846);
    const double world_spacing = 8072.500651872125;

    Front small;
    small.radius = reference_radius;
    small.spacing = reference_spacing;
    small.add(city("a", "gnome", "dwarf", 0, at(0.0, reference_radius)));
    small.add(city("b", "elf", "elf", 1, at(0.6 * reference_spacing, reference_radius)));

    Front large;
    large.radius = world_radius;
    large.spacing = world_spacing;
    large.add(city("a", "gnome", "dwarf", 0, at(0.0, world_radius)));
    large.add(city("b", "elf", "elf", 1, at(0.6 * world_spacing, world_radius)));

    const std::vector<WarPair> p = contested_pairs(small.ground());
    const std::vector<WarPair> q = contested_pairs(large.ground());
    check::equal(p.size(), std::size_t{1}, "contested at the reference width");
    check::equal(q.size(), std::size_t{1}, "and at 200 km");
    check::close(p[0].proximity_pressure, q[0].proximity_pressure, 1e-9, "the same pressure in spacings");

    // Two cells apart at size 65 on the 200 km world is 6250 m: inside the 12 109 m limit, so
    // the rule still fires at shipped widths.
    Front cells;
    cells.radius = world_radius;
    cells.spacing = world_spacing;
    cells.add(city("a", "gnome", "dwarf", 0, at(0.0, world_radius)));
    cells.add(city("b", "elf", "elf", 1, at(6250.0, world_radius)));
    const std::vector<WarPair> two_cells = contested_pairs(cells.ground());
    check::equal(two_cells.size(), std::size_t{1}, "two cells apart is contested at 200 km");
    check::close(two_cells[0].proximity_pressure, 1.0 - 6250.0 / (1.5 * world_spacing), 1e-9, "by how much");
}
