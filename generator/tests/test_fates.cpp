// City-fate invariants: ruins, the moon's sway, the threat a city builds walls against, the
// age lottery, and who a rebuilt city is.
//
// Hand-made ground on a 1000 m globe, three cells wide, with the city standing on (1, 1).
// Every property here is a weight, an order or a fallback, and each test says which.
#include "check.hpp"

#include <cctype>
#include <cmath>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "fwg/astrology.hpp"
#include "fwg/civilizations.hpp"
#include "fwg/fates.hpp"
#include "fwg/magic.hpp"
#include "fwg/nests.hpp"
#include "fwg/random.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

constexpr double kRadius = 1000.0;
constexpr double kSpacing = 450.0;

Vec3 at(double metres) {
    const double angle = metres / kRadius;
    return {std::cos(angle), std::sin(angle), 0.0};
}

int school(std::string_view name) { return magic_school_index(name); }

// Twelve potencies in school-table order, zero unless named.
std::vector<double> potencies(std::initializer_list<std::pair<const char*, double>> values = {}) {
    std::vector<double> out(magic_school_table().size(), 0.0);
    for (const auto& [name, value] : values) out[static_cast<std::size_t>(school(name))] = value;
    return out;
}

// Every school's field at zero on a 3x3 grid.
std::map<std::string, Grid> ley_layers() {
    std::map<std::string, Grid> out;
    for (const MagicSchool& s : magic_school_table()) out["ley_" + std::string(s.name)] = Grid(3, 0.0);
    return out;
}

WarCity city(const std::string& uid = "city", const std::string& profile = "human_heartland",
             CityClass city_class = CityClass::Small, const Vec3& where = at(0.0)) {
    WarCity c;
    c.uid = uid;
    c.id = 0;
    c.node = 4;
    c.x = 1;
    c.z = 1;
    c.direction = where;
    c.name = "Test";
    c.population_profile = profile;
    c.parent_race_id = "human";
    c.city_class = city_class;
    c.population_estimate = 100;
    return c;
}

// A monster lair. Tier 5 at the city's own cell weighs exactly the historical 0.55.
ReligionNest nest(const std::string& id, const std::string& name, const std::string& family,
                  const Vec3& where = at(0.0), int tier = 5, double spacing_m = 300.0) {
    ReligionNest n;
    n.id = id;
    n.name = name;
    n.family = family;
    n.layer = "surface";
    n.real = false;
    n.tier = tier;
    n.spacing_m = spacing_m;
    n.direction = where;
    return n;
}

Villain villain(const std::string& school_name, const Vec3& where = at(100.0), double reach_m = 500.0) {
    Villain v;
    v.uid = "villain-1-9";
    v.region = "anchor";
    v.seat_uid = "somewhere-else";
    v.direction = where;
    v.school = school_name;
    v.tier = 1.0;
    v.reach_m = reach_m;
    v.growth = "devouring";
    v.status = "living";
    return v;
}

RuinLegacy legacy(std::string_view cause, const std::vector<double>& p, const RuinSources& sources = {},
                  std::string_view profile = "human_heartland", CityClass city_class = CityClass::Small) {
    return ruin_legacy(cause, p, profile, city_class, sources, 0.35, 0.08);
}

RuinSources nest_family(const std::string& family) {
    RuinSources s;
    s.nest_family = family;
    return s;
}

// The standard lottery: fire ground at .8 (ley .36), an undead lair at the gates (.55),
// magic on with no weave (self-magic .035). Total .945, so the .9 cap binds.
struct Lottery {
    std::map<std::string, Grid> layers = ley_layers();
    CityFateRequest request;
    WarCity c = city();

    Lottery() {
        layers["ley_fire"].at(1, 1) = 0.8;
        request.seed = 42;
        request.age = 1;
        request.globe_radius_m = kRadius;
        request.magic_enabled = true;
        request.layers = &layers;
        request.nests = {nest("nest-undead-4", "Barrow wight", "undead")};
    }
};

}  // namespace

// --- authored text and tables ------------------------------------------------

TEST(every_known_school_has_a_fall_reason_and_no_hidden_school_has_one) {
    int known = 0;
    for (const MagicSchool& s : magic_school_table()) {
        const std::string_view reason = ley_destruction_reason(s.name);
        if (s.hidden) {
            check::is_true(reason.empty(), std::string("no reason for hidden ") + std::string(s.name));
        } else {
            check::is_true(!reason.empty(), std::string("a reason for ") + std::string(s.name));
            ++known;
        }
    }
    check::equal(known, 8, "all eight known schools");
    check::equal(ley_destruction_reason("weave"), std::string_view("An arcane surge tore the city apart."),
                 "the first row verbatim");
    check::equal(ley_destruction_reason("air"),
                 std::string_view("An air leyline shattered the city with storms and force."), "the last");
    check::equal(ley_destruction_reason("dragon"), std::string_view(), "a cause that is not a school");
}

TEST(every_civilization_scars_with_the_school_its_heritage_names) {
    // The reference's culture_schools(), read out of the Python at b67c99b. Resolved from the
    // heritage key traits rather than a hand table, so this pins the binding end to end.
    const std::map<std::string, std::string> expected = {
        {"dwarf", "earth"},           {"elf", "weave"},          {"frosthold_dwarf", "water"},
        {"gnome", "weave"},           {"hill_dwarf", "earth"},   {"human_cold", "water"},
        {"human_desert", "fire"},     {"human_heartland", "radiant"}, {"human_large_island", "air"},
        {"human_maritime", "water"},  {"human_rainforest", "earth"},  {"tidekin", "water"}};
    check::equal(civilization_ids().size(), expected.size(), "one row per civilization");
    for (const std::string_view id : civilization_ids()) {
        const auto it = expected.find(std::string(id));
        check::is_true(it != expected.end(), "a known civilization " + std::string(id));
        check::equal(std::string(culture_school(id)), it->second, "the school of " + std::string(id));
    }
    check::equal(culture_school("no_such_people"), std::string_view(), "an unknown people has none");
}

// --- the scar a ruin leaves --------------------------------------------------

TEST(the_destroyers_own_school_scars_first) {
    const RuinLegacy water = legacy("water", potencies({{"fire", 1.0}}));
    check::equal(water.school, std::string("water"), "a ley cause is its own school");
    check::equal(water.basis, std::string("source"), "from the source");
    check::equal(water.intensity, 1.5, "a small city's intensity");
    check::equal(legacy("self_magic", potencies()).school, std::string("weave"), "self-magic leaves weave");
    check::equal(legacy("monster", potencies(), nest_family("undead")).school, std::string("umbral"), "undead");
    check::equal(legacy("monster", potencies(), nest_family("holy")).school, std::string("radiant"), "holy");
    check::equal(legacy("monster", potencies(), nest_family("unholy")).school, std::string("infernal"), "unholy");
    check::equal(legacy("dragon", potencies(), nest_family("draconic")).school, std::string("fire"), "a dragon");

    RuinSources victor;
    victor.victor_culture = "tidekin";
    check::equal(legacy("war_civil", potencies(), victor).school, std::string("water"), "the victor's magic");

    RuinSources god;
    god.god_school = "air";
    const RuinLegacy divine = legacy("divine_god_air", potencies(), god, "human_heartland", CityClass::Small);
    check::equal(divine.school, std::string("air"), "a god's own school");
    check::equal(divine.intensity, 4.0, "forced to a god's intensity");

    RuinSources held;
    held.villain_school = "umbral";
    check::equal(legacy("villain_hoarding", potencies({{"fire", 0.9}}), held).school, std::string("umbral"),
                 "what the villain holds, not what the ground holds");

    // A cause that IS a school answers before any slot is read.
    RuinSources noisy = nest_family("undead");
    noisy.villain_school = "water";
    check::equal(legacy("fire", potencies(), noisy).school, std::string("fire"), "the cause before the slots");
}

TEST(a_primordial_nest_scars_with_the_strongest_element_and_a_dead_tie_keeps_school_order) {
    const RuinLegacy strongest =
        legacy("monster", potencies({{"earth", 0.4}, {"water", 0.9}, {"weave", 2.0}}), nest_family("primordial"));
    check::equal(strongest.school, std::string("water"), "the strongest ELEMENT; weave is not one");
    check::equal(strongest.basis, std::string("source"), "the primordial is the source");
    // School order is fire, water, earth, air. A name-order tie-break would give air twice.
    check::equal(legacy("monster", potencies(), nest_family("primordial")).school, std::string("fire"),
                 "all four dead");
    check::equal(legacy("monster", potencies({{"air", 0.5}, {"fire", 0.5}}), nest_family("primordial")).school,
                 std::string("fire"), "fire ties air");
    check::equal(legacy("monster", potencies({{"air", 0.7}, {"earth", 0.7}}), nest_family("primordial")).school,
                 std::string("earth"), "earth ties air");
}

TEST(with_no_source_of_its_own_the_region_then_the_culture_decides) {
    const RuinLegacy region =
        legacy("monster", potencies({{"umbral", 0.8}, {"fire", 0.1}}), nest_family("fantastic"), "dwarf");
    check::equal(region.school, std::string("umbral"), "the ground's dominant school");
    check::equal(region.basis, std::string("region"), "from the region");

    const RuinLegacy contested =
        legacy("monster", potencies({{"umbral", 0.8}, {"fire", 0.78}}), nest_family("fantastic"), "dwarf");
    check::equal(contested.school, std::string("earth"), "no clear region: the dwarves' own earth");
    check::equal(contested.basis, std::string("culture"), "from the culture");

    const RuinLegacy stranger = legacy("monster", potencies(), {}, "no_such_people");
    check::equal(stranger.school, std::string("weave"), "an unknown people falls back to weave");
    check::equal(stranger.basis, std::string("culture"), "still the culture step");

    const RuinLegacy orphan_war = legacy("war_regional", potencies(), {}, "gnome");
    check::equal(orphan_war.school, std::string("weave"), "a war with no victor: the gnomes' weave");
    check::equal(orphan_war.basis, std::string("culture"), "through the culture");

    const RuinLegacy empty_handed = legacy("villain_usurping", potencies({{"fire", 0.9}}), {}, "gnome");
    check::equal(empty_handed.school, std::string("fire"), "a villain holding nothing: the region");
    check::equal(empty_handed.basis, std::string("region"), "exactly as an unmagical cause");
}

TEST(intensity_follows_the_class_with_a_floor_for_self_magic_and_a_force_for_the_gods) {
    check::equal(legacy("fire", potencies(), {}, "gnome", CityClass::Small).intensity, 1.5, "small");
    check::equal(legacy("fire", potencies(), {}, "gnome", CityClass::Medium).intensity, 2.5, "medium");
    check::equal(legacy("fire", potencies(), {}, "gnome", CityClass::Capital).intensity, 3.5, "capital");
    check::equal(legacy("self_magic", potencies(), {}, "gnome", CityClass::Small).intensity, 2.5,
                 "a small city that destroyed itself is floored");
    check::equal(legacy("self_magic", potencies(), {}, "gnome", CityClass::Capital).intensity, 3.5,
                 "the floor is a floor, not a value");
    RuinSources god;
    god.god_school = "weave";
    check::equal(legacy("divine_x", potencies(), god, "gnome", CityClass::Capital).intensity, 4.0,
                 "a god's work is forced");
    check::equal(legacy("villain_devouring", potencies(), {}, "gnome", CityClass::Capital).intensity, 3.5,
                 "a villain gets no floor");
}

// --- the potencies a fate reads ----------------------------------------------

TEST(a_city_reads_its_own_cell_and_a_missing_field_reads_zero) {
    std::map<std::string, Grid> layers = ley_layers();
    layers.erase("ley_blood");
    layers["ley_fire"].at(2, 0) = 0.7;  // x = 2, z = 0
    layers["ley_fire"].at(0, 2) = 0.1;  // the transpose, which a swapped index would read
    WarCity c = city();
    c.x = 2;
    c.z = 0;
    const std::vector<double> p = city_potencies(&layers, c, std::nullopt);
    check::equal(p.size(), magic_school_table().size(), "all twelve schools");
    check::equal(p[static_cast<std::size_t>(school("fire"))], 0.7, "the city's own cell");
    check::equal(p[static_cast<std::size_t>(school("blood"))], 0.0, "a missing field is zero");
    const std::vector<double> none = city_potencies(nullptr, c, std::nullopt);
    for (const double v : none) check::equal(v, 0.0, "no layers at all read zero");
}

TEST(a_city_off_the_grid_is_refused_rather_than_read_as_zero) {
    std::map<std::string, Grid> layers = ley_layers();
    layers["ley_fire"].at(2, 1) = 0.5;
    WarCity c = city();
    c.x = 2;
    check::equal(city_potencies(&layers, c, std::nullopt)[static_cast<std::size_t>(school("fire"))], 0.5,
                 "the floor: the last column is on the grid and reads");
    c.x = 3;
    check::throws([&] { city_potencies(&layers, c, std::nullopt); }, "one column past a 3x3 grid");
}

TEST(the_moon_sways_only_the_known_schools_each_by_its_own_tide) {
    std::map<std::string, Grid> layers = ley_layers();
    for (auto& [name, grid] : layers) grid.at(1, 1) = 1.0;
    layers["lunar_sensitivity"] = Grid(3, 0.0);
    layers["lunar_sensitivity"].at(1, 1) = 0.8;

    CityFateLunar lunar;
    lunar.influence = 0.5;
    for (std::size_t s = 0; s < lunar.tide.size(); ++s) lunar.tide[s] = 1.0 + 0.1 * static_cast<double>(s + 1);

    const std::vector<double> p = city_potencies(&layers, city(), lunar);
    for (std::size_t s = 0; s < magic_school_table().size(); ++s) {
        const std::string name(magic_school_table()[s].name);
        if (magic_school_table()[s].hidden) {
            check::equal(p[s], 1.0, "a hidden school never swayed: " + name);
        } else {
            check::close(p[s], 1.0 + 0.5 * 0.8 * (lunar.tide[s] - 1.0), 1e-12, "swayed by its own tide: " + name);
        }
    }
    // Momentary: the exported field is untouched.
    check::equal(layers.at("ley_fire").at(1, 1), 1.0, "the field itself");
    // Without a sensitivity layer the moon has no purchase at all.
    layers.erase("lunar_sensitivity");
    for (const double v : city_potencies(&layers, city(), lunar)) check::equal(v, 1.0, "no sensitivity, no sway");
}

TEST(the_moon_is_read_on_the_founding_end_year_unless_a_day_is_given) {
    const Moon moon = seed_moon(42);
    const std::optional<CityFateLunar> founding = lunar_context(&moon, std::nullopt, 7, 0.5);
    check::is_true(founding.has_value(), "a world with a moon has a lunar context");
    check::equal(founding->day, 7LL * kDaysPerYear, "the end year, in days");
    check::equal(founding->influence, 0.5, "the world's lunar influence");
    const DayState state = day_state(moon, 7LL * kDaysPerYear);
    const auto factors = tide(moon, 7LL * kDaysPerYear);
    for (std::size_t s = 0; s < factors.size(); ++s) check::equal(founding->tide[s], factors[s], "that day's tide");
    check::equal(founding->phase_fraction, state.phase_fraction, "that day's phase");
    check::equal(founding->illumination, state.illumination, "its light");
    check::equal(founding->leaning, state.leaning, "and its lean");

    const std::optional<CityFateLunar> chosen = lunar_context(&moon, 11LL, 7, 0.5);
    check::equal(chosen->day, 11LL, "an explicit day wins");
    check::equal(chosen->tide[0], tide(moon, 11)[0], "and is the day read");
    // Day zero is a day, not an absence: the reference tests `is None`, never truthiness.
    check::equal(lunar_context(&moon, 0LL, 7, 0.5)->day, 0LL, "an explicit day zero");
    check::is_true(!lunar_context(nullptr, std::nullopt, 7, 0.5).has_value(), "no moon, no context");
}

// --- the standing threat -------------------------------------------------------

TEST(ley_pressure_is_the_dominant_potency_times_point_four_five_capped_at_point_six) {
    const ThreatLeyPressure mild = ley_threat_pressure(potencies({{"fire", 0.8}}), 0.35, 0.08);
    check::close(mild.pressure, 0.36, 1e-12, "0.8 * 0.45");
    check::is_true(mild.ley.has_value() && mild.ley->school == "fire" && mild.ley->potency == 0.8, "named");
    check::close(ley_threat_pressure(potencies({{"fire", 2.0}}), 0.35, 0.08).pressure, 0.6, 1e-12, "the cap");
    const ThreatLeyPressure contested = ley_threat_pressure(potencies({{"fire", 0.8}, {"water", 0.78}}), 0.35, 0.08);
    check::equal(contested.pressure, 0.0, "no clear school");
    check::is_true(!contested.ley.has_value(), "names none");
    check::is_true(!ley_threat_pressure(potencies({{"fire", 0.3}}), 0.35, 0.08).ley.has_value(), "under the threshold");
}

TEST(a_report_sums_nests_ley_and_veteran_wars_and_caps_at_one) {
    std::map<std::string, Grid> layers = ley_layers();
    layers["ley_fire"].at(1, 1) = 1.0;
    ThreatRequest request;
    request.ground.globe_radius_m = kRadius;
    request.ground.settlement_spacing_m = kSpacing;
    request.layers = &layers;
    request.nests = {nest("nest-red-dragon-4", "Red dragon", "draconic")};

    const ThreatReport report = assess_city_threat(request, city(), WarOutlook{});
    check::close(report.nest_pressure, 0.55, 1e-9, "a tier-five lair at the gates");
    check::close(report.ley_pressure, 0.45, 1e-9, "fire at 1.0");
    check::equal(report.war_pressure, 0.0, "no wars");
    check::equal(report.regional_threat, 1.0, "capped at one");
    check::equal(report.city_uid, std::string("city"), "keyed on the uid");
    check::equal(report.city_name, std::string("Test"), "named");
    check::equal(report.contributors.size(), std::size_t{2}, "the lair and the ley; never self-magic");
    const ThreatContributor& lair = report.contributors[0];
    check::equal(lair.kind, std::string("dragon"), "a dragon by name");
    check::equal(lair.nest_id, std::string("nest-red-dragon-4"), "which one");
    check::equal(lair.family, std::string("draconic"), "and its family");
    check::equal(lair.distance_m, 0.0, "at the gates");
    // min(radius / 2, max(350, 2 * 300)): the radius cap binds on a 1000 m globe.
    check::equal(lair.reach_m, 500.0, "the reach it was measured against");
    check::equal(report.contributors[1].kind, std::string("ley"), "then the ley");
    check::equal(report.contributors[1].school, std::string("fire"), "fire");
    check::is_true(report.ley.has_value() && report.ley->school == "fire", "and the report's own ley");
    for (const ThreatContributor& c : report.contributors) {
        check::is_true(c.kind != "self_magic", "a city does not wall itself against its own experiments");
    }
}

TEST(contributors_list_the_lairs_by_id_then_the_ley_then_the_wars_at_their_published_precision) {
    std::map<std::string, Grid> layers = ley_layers();
    layers["ley_fire"].at(1, 1) = 1.0;
    ThreatRequest request;
    request.ground.globe_radius_m = kRadius;
    request.layers = &layers;
    // Listed heaviest first, so an order by weight or by list would put nest-9 first.
    request.nests = {nest("nest-9", "Ghoul", "undead", at(0.0)),
                     nest("nest-10", "Imp", "infernal", at(123.456789), 3)};
    WarCity veteran = city();
    veteran.war_history.assign(1, WarParticipation{});
    const ThreatReport report = assess_city_threat(request, veteran, WarOutlook{});
    check::equal(report.contributors.size(), std::size_t{4}, "two lairs, the ley, the war");
    check::equal(report.contributors[0].nest_id, std::string("nest-10"), "lairs in STRING id order");
    check::equal(report.contributors[1].nest_id, std::string("nest-9"), "then the other");
    check::equal(report.contributors[2].kind, std::string("ley"), "then the ley");
    check::equal(report.contributors[3].kind, std::string("war"), "then the wars");
    // Weights to 6 dp, distances and reaches to 4.
    const double raw = 0.11 * 3 * (1.0 - 123.456789 / 500.0);
    check::close(report.contributors[0].weight, round_dp(raw, 6), 1e-15, "a weight to six places");
    check::close(report.contributors[0].distance_m, 123.4568, 1e-9, "a distance to four");
    check::close(report.nest_pressure, round_dp(raw + 0.55, 6), 1e-12, "the lairs summed, then rounded");
}

TEST(a_zero_spacing_withholds_the_whole_forecast_as_the_reference_does) {
    // The reference gates the outlook on a truthy spacing: absent is zero, and zero withholds
    // even the supply half, which does not depend on the spacing at all. Unreachable through a
    // resolved config; pinned so a change to the gate is a decision.
    const std::map<std::string, Grid> layers = ley_layers();
    std::vector<CityCore> cores(2);
    cores[0].site_id = 0;
    cores[0].food_demand = 10.0;
    cores[0].food_supply = 10.0;
    cores[1].site_id = 1;
    cores[1].food_demand = 10.0;
    cores[1].food_supply = 2.0;
    std::vector<Road> routes(1);
    routes[0].from = 0;
    routes[0].to = 1;
    ThreatRequest request;
    request.ground.globe_radius_m = kRadius;
    request.ground.cores = &cores;
    request.ground.routes = &routes;
    request.layers = &layers;
    WarCity fed = city("fed", "gnome");
    WarCity hungry = city("hungry", "gnome", CityClass::Small, at(1000.0));
    hungry.id = 1;
    request.ground.cities = {fed, hungry};

    request.ground.settlement_spacing_m = kSpacing;
    check::is_true(assess_threats(request).cities[1].outlook.war_hunger > 0.0,
                   "the floor: with a spacing the hungry city presses its neighbour");
    request.ground.settlement_spacing_m = 0.0;
    const ThreatAssessmentBlock withheld = assess_threats(request);
    check::equal(withheld.cities[1].outlook.war_hunger, 0.0, "no spacing, no forecast at all");
    check::equal(withheld.cities[0].outlook.war_risk, 0.0, "on either side");
}

TEST(a_veteran_expects_the_next_war_and_the_expectation_saturates_at_three) {
    const std::map<std::string, Grid> layers = ley_layers();
    ThreatRequest request;
    request.ground.globe_radius_m = kRadius;
    request.layers = &layers;
    const double expected[] = {0.0, 0.15, 0.3, 0.4, 0.4};
    for (int wars = 0; wars <= 4; ++wars) {
        WarCity veteran = city();
        veteran.war_history.assign(static_cast<std::size_t>(wars), WarParticipation{});
        const ThreatReport report = assess_city_threat(request, veteran, WarOutlook{});
        const std::string at_n = " after " + std::to_string(wars) + " wars";
        check::close(report.war_pressure, expected[wars], 1e-12, "war pressure" + at_n);
        check::close(report.regional_threat, expected[wars], 1e-12, "which is all the threat" + at_n);
        check::equal(report.wars_fought, wars, "wars fought" + at_n);
        if (wars == 0) {
            check::equal(report.contributors.size(), std::size_t{0}, "no contributor for no wars");
        } else {
            check::equal(report.contributors.back().kind, std::string("war"), "a war contributor" + at_n);
            check::equal(report.contributors.back().wars_fought, wars, "naming the count" + at_n);
        }
    }
}

TEST(reports_come_in_uid_order_with_the_forecast_beside_a_threat_it_does_not_move) {
    const std::map<std::string, Grid> layers = ley_layers();
    ThreatRequest request;
    request.evaluated_after = "age";
    request.age = 2;
    request.ground.globe_radius_m = kRadius;
    request.ground.settlement_spacing_m = kSpacing;
    request.layers = &layers;
    WarCity zeta = city("zeta", "gnome");
    zeta.id = 0;
    WarCity alpha = city("alpha", "elf", CityClass::Small, at(200.0));
    alpha.id = 1;
    alpha.parent_race_id = "elf";
    request.ground.cities = {zeta, alpha};

    const ThreatAssessmentBlock block = assess_threats(request);
    check::equal(block.version, 3, "version three");
    check::equal(block.evaluated_after, std::string("age"), "evaluated after");
    check::is_true(block.age == 2, "the age");
    check::equal(block.cities.size(), std::size_t{2}, "one report per city");
    check::equal(block.cities[0].city_uid, std::string("alpha"), "uid order, not list order");
    check::equal(block.cities[1].city_uid, std::string("zeta"), "then zeta");
    check::equal(block.cities[0].site_id, 1, "the site id is carried");
    check::close(block.cities[0].outlook.war_risk, round_dp(1.0 - 200.0 / 675.0, 6), 1e-12, "the forecast");
    check::equal(block.cities[0].outlook.war_risk_opponent_uid, std::string("zeta"), "names its neighbour");
    check::equal(block.cities[1].outlook.war_risk_opponent_uid, std::string("alpha"), "both ways");
    // A forecast does not build walls yet.
    check::equal(block.cities[0].regional_threat, 0.0, "the forecast does not move the threat");
    check::is_true(block.method.rfind("Per-city standing pressure", 0) == 0, "the method, verbatim");
    check::is_true(block.limits.rfind("Artistic regional threat", 0) == 0, "the limits, verbatim");
}

TEST(a_fallen_villain_does_not_press_the_forecast) {
    const std::map<std::string, Grid> layers = ley_layers();
    ThreatRequest request;
    request.evaluated_after = "age";
    request.age = 1;
    request.ground.globe_radius_m = kRadius;
    request.ground.settlement_spacing_m = kSpacing;
    request.layers = &layers;
    WarCity a = city("a", "gnome");
    WarCity b = city("b", "gnome", CityClass::Small, at(600.0));
    b.id = 1;
    request.ground.cities = {a, b};
    const double bare = assess_threats(request).cities[0].outlook.war_risk;

    request.ground.villains = {villain("umbral", at(300.0), 1000.0)};
    const double shadowed = assess_threats(request).cities[0].outlook.war_risk;
    check::is_true(shadowed > bare, "the floor: a standing villain presses the pair together");

    request.ground.villains[0].status = "fallen";
    check::equal(assess_threats(request).cities[0].outlook.war_risk, bare,
                 "a fallen one, with its live-looking reach, presses nothing");
}

TEST(the_threat_names_the_same_nest_weight_the_lottery_draws) {
    const std::map<std::string, Grid> layers = ley_layers();
    const ReligionNest lair = nest("threat", "Threat", "infernal", at(20.0));
    ThreatRequest threat;
    threat.ground.globe_radius_m = kRadius;
    threat.layers = &layers;
    threat.nests = {lair};
    const ThreatReport report = assess_city_threat(threat, city(), WarOutlook{});

    CityFateRequest fate_request;
    fate_request.globe_radius_m = kRadius;
    fate_request.magic_enabled = false;
    fate_request.layers = &layers;
    fate_request.nests = {lair};
    const std::optional<CityFate> fate = city_fate(fate_request, city(), 0.0);
    check::is_true(fate.has_value(), "a roll of zero falls to the only cause");
    check::equal(fate->cause, std::string("monster"), "the lair");
    check::is_true(report.nest_pressure > 0.0, "the floor: the lair is in reach");
    check::close(fate->probability, report.nest_pressure, 1e-6, "one weight for the walls and the lottery");
    check::close(*fate->evidence.distance_m, report.contributors[0].distance_m, 1e-4, "one distance");
}

// --- the lottery -----------------------------------------------------------------

TEST(the_causes_walk_ley_then_nests_by_string_id_then_self_magic_then_villains) {
    Lottery l;
    l.request.nests = {nest("nest-2", "Ghoul", "undead"), nest("nest-10", "Imp", "infernal")};
    l.request.villains = {villain("umbral")};
    const std::vector<CityFateCause> causes =
        city_fate_causes(l.request, l.c, city_potencies(&l.layers, l.c, std::nullopt));
    check::equal(causes.size(), std::size_t{5}, "five causes");
    check::equal(causes[0].kind, std::string("fire"), "the ley first");
    check::equal(causes[0].reason, std::string(ley_destruction_reason("fire")), "with its authored reason");
    // STRING order: "nest-10" sorts before "nest-2".
    check::equal(causes[1].evidence.nest_id, std::string("nest-10"), "nests by string id");
    check::equal(causes[2].evidence.nest_id, std::string("nest-2"), "not by number");
    check::equal(causes[3].kind, std::string("self_magic"), "then self-magic");
    check::equal(causes[4].kind, std::string("villain_devouring"), "villains appended last");
    check::close(causes[0].weight, 0.36, 1e-12, "ley .8 * .45");
    check::close(causes[3].weight, 0.035, 1e-12, "self-magic on dead weave ground");
    check::equal(causes[1].reason, std::string("Imp drove the inhabitants away."), "a lair's sentence");
    check::equal(causes[3].reason,
                 std::string("The city destroyed itself in a magical experiment, leaving a new Weave key point."),
                 "self-magic's sentence, verbatim");
}

TEST(the_selector_walks_forward_over_the_draw_rescaled_to_the_uncapped_sum) {
    const Lottery l;
    const std::vector<CityFateCause> causes =
        city_fate_causes(l.request, l.c, city_potencies(&l.layers, l.c, std::nullopt));
    double total = 0.0;
    for (const CityFateCause& c : causes) total += c.weight;
    check::is_true(total > kFateChanceCap, "the floor: the cap binds, so the rescale matters");

    // Golden cases. Walked backwards, .1 names the lair; without the rescale, .89 names the
    // lair too; only a forward walk over draw / chance * total gives this answer at all three.
    const std::pair<double, const char*> golden[] = {{0.1, "fire"}, {0.5, "monster"}, {0.89, "self_magic"}};
    for (const auto& [roll, cause] : golden) {
        const std::optional<CityFate> fate = city_fate(l.request, l.c, roll);
        check::is_true(fate.has_value(), "a fall at roll " + std::to_string(roll));
        check::equal(fate->cause, std::string(cause), "the cause at roll " + std::to_string(roll));
        check::close(fate->probability, kFateChanceCap, 1e-12, "drawn against the capped chance");
        check::equal(fate->roll, roll, "the roll recorded");
    }
}

TEST(a_roll_at_the_chance_survives_and_one_ulp_under_it_falls) {
    const Lottery l;
    check::is_true(!city_fate(l.request, l.c, kFateChanceCap).has_value(), "a roll at the chance stands");
    const std::optional<CityFate> fall = city_fate(l.request, l.c, std::nextafter(kFateChanceCap, 0.0));
    check::is_true(fall.has_value(), "one ulp under it falls");
    check::equal(fall->cause, std::string("self_magic"), "to the last cause the walk reaches");
}

TEST(a_city_with_nothing_against_it_stands_without_dividing_by_zero) {
    const std::map<std::string, Grid> layers = ley_layers();
    CityFateRequest request;
    request.globe_radius_m = kRadius;
    request.magic_enabled = false;
    request.layers = &layers;
    check::equal(city_fate_causes(request, city(), potencies()).size(), std::size_t{0}, "no causes");
    check::is_true(!city_fate(request, city(), 0.0).has_value(), "stands at a roll of zero");
    // The floor: the same city with magic on has self-magic against it and falls to that roll,
    // so the survival above is the empty lottery and not an immune city.
    request.magic_enabled = true;
    const std::optional<CityFate> fall = city_fate(request, city(), 0.0);
    check::is_true(fall.has_value() && fall->cause == "self_magic", "the same city falls to its own magic");
}

TEST(a_war_ruins_region_fallback_reads_the_field_the_moon_swayed_that_day) {
    // Reached only when the victor's culture names no school. Then the ground decides, and it
    // decides on what the lottery saw that day rather than on the exported field.
    std::map<std::string, Grid> layers = ley_layers();
    layers["ley_fire"].at(1, 1) = 0.3;  // under the threshold on its own
    layers["lunar_sensitivity"] = Grid(3, 0.0);
    layers["lunar_sensitivity"].at(1, 1) = 1.0;
    CityFateLunar lunar;
    lunar.influence = 0.5;
    lunar.tide.fill(1.0);
    lunar.tide[static_cast<std::size_t>(school("fire"))] = 1.5;  // .3 * 1.25 = .375

    CityFateRequest request;
    request.globe_radius_m = kRadius;
    request.layers = &layers;
    request.lunar = lunar;
    WarFate war;
    war.cause = "war_civil";
    war.opponent_civilization_id = "no_such_people";

    const CityFate surged = war_ruin_fate(request, city(), war);
    check::equal(surged.legacy.school, std::string("fire"), "the swayed ground holds fire");
    check::equal(surged.legacy.basis, std::string("region"), "through the region");
    request.lunar.reset();
    const CityFate plain = war_ruin_fate(request, city(), war);
    check::equal(plain.legacy.school, std::string("radiant"), "unswayed, the heartland's own radiant");
    check::equal(plain.legacy.basis, std::string("culture"), "through the culture");
}

TEST(the_seeded_roll_is_one_word_from_the_citys_own_stream) {
    Lottery l;
    l.layers["ley_fire"].at(1, 1) = 0.0;
    l.request.magic_enabled = false;
    l.request.nests = {nest("nest-undead-4", "Barrow wight", "undead", at(200.0))};
    const double chance = city_fate_causes(l.request, l.c, potencies())[0].weight;
    check::between(chance, 0.05, 0.6, "the floor: a chance both outcomes can draw");
    int fell = 0;
    int stood = 0;
    for (std::uint32_t seed = 1; seed <= 40; ++seed) {
        l.request.seed = seed;
        const double expected = Rng::child(seed, "city-fate-city", 1).random();
        const std::optional<CityFate> fate = city_fate(l.request, l.c);
        check::equal(fate.has_value(), expected < chance, "the stream decides at seed " + std::to_string(seed));
        if (fate.has_value()) {
            ++fell;
            check::equal(fate->roll, expected, "the fate's roll is that word");
        } else {
            ++stood;
        }
    }
    check::is_true(fell > 0 && stood > 0, "the sample to contain both falls and survivals");
}

TEST(the_age_is_the_variation_so_a_city_cannot_repeat_its_roll) {
    Lottery l;
    // Two lairs at the gates: the chance is the .9 cap, so a roll is almost always published.
    l.request.nests = {nest("nest-a", "Lich", "undead"), nest("nest-b", "Pit fiend", "infernal")};
    l.request.age = 1;
    const std::optional<CityFate> first = city_fate(l.request, l.c);
    l.request.age = 2;
    const std::optional<CityFate> second = city_fate(l.request, l.c);
    check::is_true(first.has_value() || second.has_value(), "at least one published roll");
    const double one = Rng::child(42, "city-fate-city", 1).random();
    const double two = Rng::child(42, "city-fate-city", 2).random();
    if (first.has_value()) check::equal(first->roll, one, "age one draws variation one");
    if (second.has_value()) check::equal(second->roll, two, "age two draws variation two");
    check::is_true(one != two, "and the two words differ");
}

TEST(only_a_ley_cause_of_a_known_school_records_the_moon_tide) {
    Lottery l;
    CityFateLunar lunar;
    lunar.influence = 0.5;
    for (std::size_t s = 0; s < lunar.tide.size(); ++s) lunar.tide[s] = 1.0 + 0.1 * static_cast<double>(s);
    l.request.lunar = lunar;  // no lunar_sensitivity layer: the moon is read but sways nothing

    const std::optional<CityFate> ley = city_fate(l.request, l.c, 0.1);
    check::equal(ley->cause, std::string("fire"), "the ley cause");
    check::is_true(ley->evidence.moon_tide.has_value(), "records the tide");
    check::equal(*ley->evidence.moon_tide, lunar.tide[static_cast<std::size_t>(school("fire"))],
                 "fire's own tide, by school position");
    const std::optional<CityFate> lair = city_fate(l.request, l.c, 0.5);
    check::equal(lair->cause, std::string("monster"), "a lair");
    check::is_true(!lair->evidence.moon_tide.has_value(), "records no tide");
    l.request.lunar.reset();
    check::is_true(!city_fate(l.request, l.c, 0.1)->evidence.moon_tide.has_value(), "no moon, no tide");
}

TEST(the_lottery_reads_the_swayed_field_and_the_walls_read_the_unswayed_one) {
    std::map<std::string, Grid> layers = ley_layers();
    layers["ley_fire"].at(1, 1) = 0.3;  // under the .35 threshold on its own
    layers["lunar_sensitivity"] = Grid(3, 0.0);
    layers["lunar_sensitivity"].at(1, 1) = 1.0;
    CityFateLunar lunar;
    lunar.influence = 0.5;
    lunar.tide.fill(1.0);
    lunar.tide[static_cast<std::size_t>(school("fire"))] = 1.5;  // .3 * 1.25 = .375

    CityFateRequest request;
    request.globe_radius_m = kRadius;
    request.magic_enabled = false;
    request.layers = &layers;
    request.lunar = lunar;
    const std::vector<CityFateCause> surged =
        city_fate_causes(request, city(), city_potencies(&layers, city(), lunar));
    check::equal(surged.size(), std::size_t{1}, "a surge raises a ley cause");
    check::equal(surged[0].kind, std::string("fire"), "fire");
    check::close(*surged[0].evidence.potency, 0.375, 1e-12, "at the swayed potency");
    check::equal(city_fate(request, city(), 0.0)->cause, std::string("fire"), "and the lottery draws it");

    request.lunar.reset();
    check::is_true(!city_fate(request, city(), 0.0).has_value(), "no moon, no cause, no fall");

    ThreatRequest threat;
    threat.ground.globe_radius_m = kRadius;
    threat.layers = &layers;
    check::equal(assess_city_threat(threat, city(), WarOutlook{}).ley_pressure, 0.0,
                 "the walls never answer a surge");
}

TEST(a_hidden_school_that_holds_a_city_corrupts_it_rather_than_ruining_it) {
    // Ruled 2026-09-23; the reference raised a KeyError here. A hidden school has no fall
    // reason, keeps its place and weight in the walk, and chosen, holds the city: no reason,
    // no legacy, no key point.
    std::map<std::string, Grid> layers = ley_layers();
    layers["ley_blood"].at(1, 1) = 1.0;
    CityFateRequest request;
    request.globe_radius_m = kRadius;
    request.layers = &layers;
    const std::vector<CityFateCause> causes =
        city_fate_causes(request, city(), city_potencies(&layers, city(), std::nullopt));
    check::is_true(!causes.empty() && causes.front().kind == "blood", "blood's cause first in the walk");
    check::is_true(causes.front().corrupts && causes.front().reason.empty(), "a cause that corrupts, with no reason");
    check::is_true(causes.front().weight > 0.0, "and its full weight");
    const std::optional<CityFate> fate = city_fate(request, city(), 0.0);
    check::is_true(fate.has_value() && fate->corrupts, "chosen, it corrupts");
    check::equal(fate->cause, std::string("blood"), "held by blood");
    check::is_true(fate->new_node_school.empty() && fate->legacy.school.empty(), "and leaves no key point");
    // The walls still see it: the assessment reads no prose.
    ThreatRequest threat;
    threat.ground.globe_radius_m = kRadius;
    threat.layers = &layers;
    const ThreatReport report = assess_city_threat(threat, city(), WarOutlook{});
    check::is_true(report.ley.has_value() && report.ley->school == "blood", "the walls name blood");
}

TEST(magic_off_takes_self_destruction_out_of_the_lottery) {
    Lottery l;
    auto kinds = [&] {
        std::vector<std::string> out;
        for (const CityFateCause& c : city_fate_causes(l.request, l.c, potencies({{"fire", 0.8}}))) out.push_back(c.kind);
        return out;
    };
    const std::vector<std::string> on = kinds();
    check::equal(on.back(), std::string("self_magic"), "present with magic on");
    l.request.magic_enabled = false;
    for (const std::string& kind : kinds()) check::is_true(kind != "self_magic", "absent with magic off");
    // Self-magic rises with the Weave under the city.
    l.request.magic_enabled = true;
    const std::vector<CityFateCause> weave_ground =
        city_fate_causes(l.request, l.c, potencies({{"weave", 0.5}}));
    check::close(weave_ground.back().weight, 0.035 + 0.06 * 0.5, 1e-12, "3.5% plus 6% of the weave");
}

TEST(a_villain_cause_scars_with_what_the_villain_holds_or_falls_through) {
    Lottery l;
    l.request.nests.clear();
    l.layers["ley_fire"].at(1, 1) = 0.0;
    l.request.magic_enabled = false;
    l.request.villains = {villain("umbral", at(0.0))};
    l.request.villains[0].seat_uid = "elsewhere";
    const std::optional<CityFate> held = city_fate(l.request, l.c, 0.0);
    check::is_true(held.has_value(), "the villain takes the city");
    check::equal(held->cause, std::string("villain_devouring"), "in its own name");
    check::equal(held->legacy.school, std::string("umbral"), "scarring with what it holds");
    check::equal(held->legacy.basis, std::string("source"), "as the source");
    check::equal(held->evidence.villain_uid, std::string("villain-1-9"), "the villain named");

    l.request.villains[0].school.clear();
    const std::optional<CityFate> empty = city_fate(l.request, l.c, 0.0);
    check::equal(empty->legacy.school, std::string("radiant"), "holding nothing: the heartland's radiant");
    check::equal(empty->legacy.basis, std::string("culture"), "through region to culture");
}

TEST(the_fate_carries_the_scar_it_leaves) {
    Lottery l;
    l.c.city_class = CityClass::Capital;
    const std::optional<CityFate> fate = city_fate(l.request, l.c, 0.1);
    check::equal(fate->cause, std::string("fire"), "a ley fall");
    check::equal(fate->new_node_school, fate->legacy.school, "the node school IS the legacy's");
    check::equal(fate->legacy.school, std::string("fire"), "its own school");
    check::equal(fate->legacy.intensity, 3.5, "a capital's scar");
    check::equal(fate->reason, std::string(ley_destruction_reason("fire")), "the authored sentence");
}

TEST(a_war_ruin_scars_with_the_victors_school_and_replaces_the_wars_own) {
    const Lottery l;
    WarFate war;
    war.cause = "war_regional";
    war.reason = "Kindred civilizations fought.";
    war.war_id = "war-1-0";
    war.kind = "regional";
    war.opponent_uid = "victor";
    war.opponent_civilization_id = "tidekin";
    war.opponent_parent_race_id = "elf";
    war.distance_m = 123.0;
    war.pressure = 0.7;
    war.probability = 0.78;
    war.roll = 0.25;
    war.new_node_school = "infernal";  // what the ground held, which the victor overrides

    const CityFate fate = war_ruin_fate(l.request, l.c, war);
    check::equal(fate.cause, std::string("war_regional"), "the war's cause");
    check::equal(fate.legacy.school, std::string("water"), "the tidekin victor's water");
    check::equal(fate.legacy.basis, std::string("source"), "the victor is the source");
    check::equal(fate.new_node_school, std::string("water"), "REPLACING the wars module's infernal");
    check::equal(fate.evidence.war_id, std::string("war-1-0"), "the war named");
    check::equal(fate.evidence.opponent_civilization_id, std::string("tidekin"), "and its victor");
    check::is_true(fate.evidence.pressure == 0.7 && fate.evidence.distance_m == 123.0, "the pair measured");
    check::is_true(!fate.evidence.moon_tide.has_value(), "a war records no tide");
    check::equal(fate.probability, 0.78, "the war's chance");
    check::equal(fate.roll, 0.25, "and its roll");

    war.opponent_civilization_id = "no_such_people";
    const CityFate orphan = war_ruin_fate(l.request, l.c, war);
    check::equal(orphan.legacy.school, std::string("fire"), "an unknown victor: the region's fire");
    check::equal(orphan.legacy.basis, std::string("region"), "so a war ruin always seeds a key point");
}

// --- an inherited defect, pinned so a fix cannot land silently --------------

TEST(a_monster_lair_threatens_almost_no_city_at_the_shipped_width) {
    // A lair's reach is min(radius / 2, max(350 m, 2 * range_m)), and range_m is the
    // catalogue's spacing_m * nest_spacing / 2 -- ABSOLUTE metres authored for the 11.15 km
    // reference world, never passed through reach_scale. There the half-radius cap bound: a
    // lair reached 887 m, about ten grid steps. On the 200 km world the largest eligible
    // species reaches 3500 m and the median 400 m, against a latitude row of 3125 m at size
    // 33 -- and a lair never stands on a city's own cell.
    //
    // The consequence, measured: the monster and dragon fates, and the nest half of every
    // city's walls, almost never exist, while wars -- whose reach is a multiple of the
    // settlement spacing -- reach most cities of the same world. Seed 42 measured 0 of 34
    // cities threatened by 149 eligible lairs at size 33, 4 of 43 at 65 and 1 of 47 at 129.
    // PINNED rather than fixed, because scaling the reach changes every world.
    Request r;
    r.seed = 42;
    r.size = 33;
    r.phase = 13;
    const World w = generate(resolve(r));
    const double radius = w.effective.globe_radius;

    std::vector<ReligionNest> lairs;
    for (const NestSite& site : w.beast_nests.sites) {
        ReligionNest n;
        n.id = site.id;
        n.name = std::string(site.name);
        n.family = std::string(site.family);
        n.real = site.real;
        n.tier = site.tier;
        n.range_m = site.range_m;
        n.direction = site.direction;
        std::string lower = n.name;
        for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        const bool eligible = !n.real && (lower.find("dragon") != std::string::npos || n.family == "infernal" ||
                                          n.family == "undead" || n.family == "aberrant");
        if (eligible) lairs.push_back(n);
    }

    WarGround ground;
    ground.globe_radius_m = radius;
    ground.settlement_spacing_m = w.effective.settlement_spacing;
    ground.cores = &w.humans.cores;
    ground.routes = &w.roads.routes;
    for (const SettlementSite& s : w.settlements.sites) {
        WarCity c;
        c.uid = s.uid;
        c.id = s.id;
        c.x = s.x;
        c.z = s.z;
        c.direction = s.direction;
        c.population_profile = s.population_profile;
        c.parent_race_id = s.parent_race_id;
        ground.cities.push_back(c);
    }

    check::close(2.0 * 3.14159265358979323846 * radius, 200000.0, 1.0, "the 200 km shipped world");
    check::is_true(ground.cities.size() >= 20, "a world with cities to threaten");
    check::is_true(lairs.size() >= 50, "and eligible lairs to threaten them, or nothing is measured");

    long long threatened = 0;
    for (const WarCity& c : ground.cities) {
        ReligionCity view;
        view.uid = c.uid;
        view.direction = c.direction;
        if (!nest_threats(view, lairs, radius).empty()) ++threatened;
    }
    std::set<std::string> contested;
    for (const WarPair& pair : contested_pairs(ground)) {
        contested.insert(pair.a_uid);
        contested.insert(pair.b_uid);
    }
    const auto cities = static_cast<long long>(ground.cities.size());
    check::is_true(threatened * 20 <= cities,
                   "lairs to threaten at most one city in twenty, got " + std::to_string(threatened));
    check::is_true(static_cast<long long>(contested.size()) * 2 > cities,
                   "while war contention, which scales, reaches most of them: " + std::to_string(contested.size()));
}
