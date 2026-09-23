// Nomad invariants.
//
// These run against a hand-built world rather than a generated one, for the same reason
// the religion tests do: the properties under test are about hard gates, ordering and
// write-backs, and a generated world would make every one of them depend on fifteen
// stages upstream -- three of which are not ported.
//
// The bench is a nine-cell raster whose equatorial step is exactly 10 km, so every reach
// in the tests is a round number of graph steps and a failure says which rule moved
// rather than which float did.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fwg/nomads.hpp"

using namespace fwg;

namespace {

constexpr int kN = 9;
// 2 * pi * radius / (n - 1) == 10000 m: one equatorial graph step, and half that
// between latitudes.
constexpr double kRadius = 12732.395447351628;
constexpr double kSpacing = 12000.0;
constexpr double kStep = 10000.0;

// The equator row of a nine-cell raster, and the row above it.
constexpr int kEquatorRow = 4;

struct Bench {
    std::map<std::string, Grid> layers;
    std::vector<Grid> months;
    std::shared_ptr<const SphereGrid> grid;
    std::vector<NomadHabitatCell> cells;
    NomadRequest request;

    Bench() {
        layers["water_type"] = Grid(kN, 0.0);   // all land
        layers["slope"] = Grid(kN, 0.0);
        layers["tpi"] = Grid(kN, 0.0);
        layers["height"] = Grid(kN, 0.0);
        layers["temperature"] = Grid(kN, 10.0);
        layers["food_potential"] = Grid(kN, 0.5);
        layers["natural_food_potential"] = Grid(kN, 0.0);
        layers["natural_biome"] = Grid(kN, 3.0);
    }

    void uniform(const std::string& name, double value) { layers[name] = Grid(kN, value); }

    // Height, temperature and the like as a function of longitude column. The seam column
    // is written too, so the grid keeps the redundancy a consumer relies on.
    template <typename Fn>
    void by_x(const std::string& name, Fn&& fn) {
        Grid g(kN, 0.0);
        for (int z = 0; z < kN; ++z) {
            for (int x = 0; x < kN - 1; ++x) g.at(x, z) = fn(x);
            g.at(kN - 1, z) = g.at(0, z);
        }
        layers[name] = g;
    }

    template <typename Fn>
    void by_z(const std::string& name, Fn&& fn) {
        Grid g(kN, 0.0);
        for (int z = 0; z < kN; ++z) {
            for (int x = 0; x < kN; ++x) g.at(x, z) = fn(z);
        }
        layers[name] = g;
    }

    // Twelve months with a fixed spread, so every cell's seasonal swing is `spread / 30`.
    void seasons(double spread) {
        months.clear();
        for (int i = 0; i < 12; ++i) months.push_back(Grid(kN, i < 6 ? 0.0 : spread));
    }

    void finish() {
        grid = sphere_grid(kN, kRadius);
        cells = nomad_habitat_cells(layers, *grid);
        request.world_recipe = true;
        request.phase = 16;
        request.seed = 42;
        request.size = kN;
        request.settlement_spacing_m = kSpacing;
        request.globe_radius_m = kRadius;
        request.road_max_grade = 0.4;
        request.grid = grid.get();
        request.cells = &cells;
        request.seasonal_temperature.clear();
        for (const Grid& month : months) request.seasonal_temperature.push_back(&month);
    }

    int node(int x, int z) const { return grid->node_index(x, z); }
    Vec3 dir(int x, int z) const { return direction(x, z, kN); }
    const NomadHabitatCell& cell(int x, int z) const {
        return cells[static_cast<std::size_t>(node(x, z))];
    }
};

NomadSite site_at(const Bench& bench, const std::string& uid, int x, int z, double people) {
    NomadSite site;
    site.uid = uid;
    site.id = uid;
    site.node = bench.node(x, z);
    site.x = x;
    site.z = z;
    site.direction = bench.dir(x, z);
    site.population_estimate = people;
    return site;
}

NomadRuin ruin_at(const Bench& bench, const std::string& uid, int x, int z, int age) {
    NomadRuin ruin;
    ruin.uid = uid;
    ruin.id = uid;
    ruin.node = bench.node(x, z);
    ruin.x = x;
    ruin.z = z;
    ruin.direction = bench.dir(x, z);
    ruin.destroyed_age = age;
    return ruin;
}

// A band placed by hand, so a route test does not depend on a gate holding.
NomadBand band_at(const Bench& bench, const std::string& uid, NomadClass value, int x, int z,
                  int size) {
    const NomadClassPolicy& rule = nomad_class_policy(value);
    NomadBand band;
    band.uid = uid;
    band.classification = value;
    band.node = bench.node(x, z);
    band.x = x;
    band.z = z;
    band.direction = bench.dir(x, z);
    band.size = size;
    band.speed_m_per_day = rule.speed_m_per_day;
    band.disposition = rule.disposition;
    band.seeks = rule.seeks;
    band.carries = rule.carries;
    NomadCamp camp;
    camp.id = uid + "-camp-0";
    camp.node = band.node;
    camp.direction = band.direction;
    camp.kind = value == NomadClass::Merchants ? NomadCampKind::Station
              : (value == NomadClass::Bandits ? NomadCampKind::Lair : NomadCampKind::Base);
    band.camps.push_back(camp);
    return band;
}

const NomadCamp* camp_of_kind(const NomadBand& band, NomadCampKind kind) {
    for (const NomadCamp& camp : band.camps) {
        if (camp.kind == kind) return &camp;
    }
    return nullptr;
}

const NomadBand* band_by_uid(const NomadBlock& block, const std::string& uid) {
    for (const NomadBand& band : block.groups) {
        if (band.uid == uid) return &band;
    }
    return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
// The table and its order.
// ---------------------------------------------------------------------------

TEST(the_classification_table_is_carried_in_enum_order) {
    const char* expected[] = {"survivors", "deserters", "cultists", "merchants", "bandits", "wanderers"};
    const auto rows = nomad_policy().classifications;
    check::equal(rows.size(), std::size_t{kNomadClassCount}, "classification count");
    for (int i = 0; i < kNomadClassCount; ++i) {
        // The evaluation order lives in the enum, not in the file. The generated table is
        // indexed by the enum, so a row out of place here is a different world.
        check::equal(std::string(rows[static_cast<std::size_t>(i)].name), std::string(expected[i]),
                     "classification at index " + std::to_string(i));
        check::equal(std::string(to_string(static_cast<NomadClass>(i))), std::string(expected[i]),
                     "to_string at index " + std::to_string(i));
    }
    // The balance table is the only lever, so the numbers have to arrive.
    check::close(nomad_class_policy(NomadClass::Wanderers).weight, 2.0, 1e-12, "wanderer weight");
    check::equal(nomad_class_policy(NomadClass::Wanderers).size_high, 420, "wanderer size ceiling");
    check::close(nomad_class_policy(NomadClass::Cultists).gate.min_ley_intensity, 2.0, 1e-12,
                 "cultist ley floor");
    check::close(nomad_policy().fission.rich_forage, 0.55, 1e-12, "rich forage");
}

TEST(the_ley_list_follows_the_school_table_and_not_the_map) {
    MagicReport magic;
    // Inserted so that a std::map walk would be alphabetical -- air before weave -- while
    // the contract order is weave first.
    for (const char* name : {"weave", "air", "blood"}) {
        LeyNetwork net;
        net.name = name;
        net.nodes.push_back({std::string(name) + "-node-0", Vec3{0.0, 1.0, 0.0}, 3.0});
        magic.networks[name] = net;
    }
    const std::vector<NomadLeyNode> ley = nomad_ley_nodes(magic);
    check::equal(ley.size(), std::size_t{3}, "flattened ley node count");
    check::equal(ley[0].school, std::string("weave"), "first school");
    check::equal(ley[1].school, std::string("air"), "second school");
    check::equal(ley[2].school, std::string("blood"), "hidden school comes last");
}

// ---------------------------------------------------------------------------
// The gates.
// ---------------------------------------------------------------------------

TEST(every_gate_returns_a_score_inside_zero_and_one) {
    Bench bench;
    bench.by_x("slope", [](int x) { return x * 5.0; });
    bench.by_x("tpi", [](int x) { return x * 12.0; });
    bench.seasons(15.0);
    bench.finish();
    bench.request.ages = {2};
    bench.request.sites = {site_at(bench, "site-1", 2, kEquatorRow, 40000.0),
                           site_at(bench, "site-2", 5, kEquatorRow, 9000.0)};
    bench.request.sites[0].war_history = 3;
    bench.request.hamlets = {site_at(bench, "hamlet-1", 3, kEquatorRow, 0.0)};
    bench.request.hamlets[0].population_estimate.reset();
    bench.request.hamlets[0].delivered_food = 800.0;
    bench.request.ruins = {ruin_at(bench, "ruin-1", 4, kEquatorRow, 2)};
    bench.request.ley = {{"weave", "weave-node-0", bench.dir(6, kEquatorRow), 3.5}};
    bench.request.gods = {{"god_weave", {"weave"}}};
    bench.request.shrines = {{"shrine-1", "god_weave", bench.node(1, 3), -1, -1, std::nullopt}};
    bench.request.claims = {{"claim-1", bench.node(7, 5), -1, -1, std::nullopt, 0.6, "villain-1",
                             "weave", 3.0}};
    bench.request.roads = {{{bench.node(0, kEquatorRow), bench.node(1, kEquatorRow)}, 10000.0}};

    const NomadGround ground = nomad_ground(bench.request);
    int verdicts = 0;
    int kinds = 0;
    for (int i = 0; i < kNomadClassCount; ++i) {
        bool seen = false;
        for (const NomadHabitatCell& cell : bench.cells) {
            const auto verdict = nomad_gate(static_cast<NomadClass>(i), cell, 0.5, ground, bench.request);
            if (!verdict) continue;
            // EVERY GATE SCORES IN 0..1 SO THE WEIGHT IS THE ONLY BALANCE LEVER. With raw
            // scores ley intensity ran to 4 while a bandit sat near 0.5 and cultists won
            // every contested draw whatever the table said.
            check::between(verdict->score, 0.0, 1.0, "gate score for " +
                           std::string(to_string(static_cast<NomadClass>(i))));
            ++verdicts;
            seen = true;
        }
        if (seen) ++kinds;
    }
    // Non-vacuity: a sweep that admitted nothing would pass the bound above trivially.
    check::is_true(verdicts >= 10, "the sweep admitted at least ten cells, got " + std::to_string(verdicts));
    check::is_true(kinds >= 4, "at least four classifications held somewhere, got " + std::to_string(kinds));
}

TEST(a_survivor_needs_both_a_fresh_ruin_and_a_standing_refuge) {
    Bench bench;
    bench.finish();
    bench.request.ages = {2};
    bench.request.ruins = {ruin_at(bench, "ruin-1", 0, kEquatorRow, 2)};
    bench.request.sites = {site_at(bench, "site-1", 1, kEquatorRow, 5000.0)};

    const NomadHabitatCell& here = bench.cell(0, kEquatorRow);
    const auto held = nomad_gate(NomadClass::Survivors, here, 0.0, nomad_ground(bench.request), bench.request);
    check::is_true(held.has_value(), "a fresh ruin underfoot with a town one step away raises refugees");
    check::equal(held->basis.ruin_uid, std::string("ruin-1"), "the ruin it fled");
    check::equal(held->basis.refuge_uid, std::string("site-1"), "the town it fled toward");
    check::close(held->basis.ruin_distance_m, 0.0, 1e-6, "standing on the ruin");
    check::close(held->basis.refuge_distance_m, kStep, 1.0, "one graph step to the refuge");
    check::is_true(held->score > 0.0, "a scored verdict, not a zero one");

    // Nowhere still standing to flee toward, so there is no band -- not a weaker one.
    NomadRequest alone = bench.request;
    alone.sites.clear();
    check::is_true(!nomad_gate(NomadClass::Survivors, here, 0.0, nomad_ground(alone), alone).has_value(),
                   "a ruin with no surviving neighbour raises nobody");
}

TEST(a_ruin_that_fell_in_an_earlier_age_raises_no_refugees) {
    Bench bench;
    bench.finish();
    bench.request.ages = {1, 2, 3};
    bench.request.sites = {site_at(bench, "site-1", 1, kEquatorRow, 5000.0)};
    const NomadHabitatCell& here = bench.cell(0, kEquatorRow);

    bench.request.ruins = {ruin_at(bench, "ruin-1", 0, kEquatorRow, 3)};
    check::is_true(nomad_gate(NomadClass::Survivors, here, 0.0, nomad_ground(bench.request),
                              bench.request).has_value(),
                   "a ruin destroyed this age raises refugees");
    // Only THIS age's dead produce refugees: the displaced of five thousand years ago are
    // not still walking.
    bench.request.ruins[0].destroyed_age = 2;
    check::is_true(!nomad_gate(NomadClass::Survivors, here, 0.0, nomad_ground(bench.request),
                               bench.request).has_value(),
                   "a ruin from the previous age raises nobody");
}

TEST(a_ley_node_below_intensity_falls_through_to_the_shrine_branch) {
    Bench bench;
    bench.finish();
    bench.request.gods = {{"god_weave", {"weave"}}};
    bench.request.shrines = {{"shrine-1", "god_weave", bench.node(0, kEquatorRow), -1, -1, std::nullopt}};
    // In reach, but under the intensity floor, so the whole condition fails and the elif
    // chain walks on rather than committing.
    bench.request.ley = {{"weave", "weave-node-0", bench.dir(0, kEquatorRow), 1.0}};

    const NomadHabitatCell& here = bench.cell(0, kEquatorRow);
    const auto held = nomad_gate(NomadClass::Cultists, here, 0.0, nomad_ground(bench.request), bench.request);
    check::is_true(held.has_value(), "a shrine underfoot raises a cult");
    check::equal(held->basis.shrine_id, std::string("shrine-1"), "classified by the shrine");
    check::is_true(held->basis.ley_node_id.empty(), "and carries no ley node to deepen");
    check::close(held->score, 0.25, 1e-12, "shrine strength is 1.0 over the 4.0 ceiling");

    // Raise the same node past the floor and the first branch takes it instead.
    bench.request.ley[0].intensity = 3.0;
    const auto charged = nomad_gate(NomadClass::Cultists, here, 0.0, nomad_ground(bench.request), bench.request);
    check::is_true(charged.has_value(), "a charged node raises a cult");
    check::equal(charged->basis.ley_node_id, std::string("weave-node-0"), "classified by the ley node");
    check::close(charged->score, 0.75, 1e-12, "intensity 3 over the 4.0 ceiling");
}

TEST(a_charged_ley_node_with_no_bound_god_refuses_instead_of_falling_back) {
    Bench bench;
    bench.finish();
    // A shrine that WOULD classify this cell, so the refusal is a refusal and not an
    // absence of anything sacred.
    bench.request.gods = {{"god_radiant", {"radiant"}}};
    bench.request.shrines = {{"shrine-1", "god_radiant", bench.node(0, kEquatorRow), -1, -1, std::nullopt}};
    bench.request.ley = {{"blood", "blood-node-0", bench.dir(0, kEquatorRow), 3.0}};

    const NomadHabitatCell& here = bench.cell(0, kEquatorRow);
    check::is_true(!nomad_gate(NomadClass::Cultists, here, 0.0, nomad_ground(bench.request),
                               bench.request).has_value(),
                   "a charged node whose school has no god does NOT fall back to the shrine");

    // Give the blood school its hidden god and the same ground raises a blood cult -- the
    // exact case the world-read of the religion block exists for.
    bench.request.gods.push_back({"god_blood", {"blood"}});
    const auto held = nomad_gate(NomadClass::Cultists, here, 0.0, nomad_ground(bench.request), bench.request);
    check::is_true(held.has_value(), "the same ground raises a cult once a god answers to blood");
    check::equal(held->basis.god_id, std::string("god_blood"), "the hidden god is the patron");
    check::equal(held->basis.school, std::string("blood"), "and the school rides the basis");
}

TEST(held_ground_outlives_its_holder_and_the_claim_carries_the_decay) {
    Bench bench;
    bench.finish();
    bench.request.gods = {{"god_weave", {"weave"}}};
    // A fallen villain's claim: tier three, influence decayed to 0.6.
    bench.request.claims = {{"claim-1", bench.node(0, kEquatorRow), -1, -1, std::nullopt, 0.6,
                             "villain-1", "weave", 3.0}};

    const NomadHabitatCell& here = bench.cell(0, kEquatorRow);
    const auto held = nomad_gate(NomadClass::Cultists, here, 0.0, nomad_ground(bench.request), bench.request);
    check::is_true(held.has_value(), "a cult still stands on a fallen villain's ground");
    check::equal(held->basis.claim_id, std::string("claim-1"), "classified by the claim");
    check::equal(held->basis.villain_uid, std::string("villain-1"), "and names its holder");
    // max(1, tier) * influence, over the 4.0 ceiling.
    check::close(held->score, 3.0 * 0.6 / 4.0, 1e-12, "strength is tier times influence");

    // Decayed past the floor: the claim carries influence zero, the score is zero, and the
    // classification drops out of the draw entirely.
    bench.request.claims[0].influence = 0.0;
    const auto spent = nomad_gate(NomadClass::Cultists, here, 0.0, nomad_ground(bench.request), bench.request);
    check::is_true(spent.has_value(), "the gate still holds -- the ground is still claimed");
    check::close(spent->score, 0.0, 1e-12, "but a spent claim scores nothing and loses its share");
}

TEST(the_bandit_gate_refuses_ground_a_seat_already_polices) {
    Bench bench;
    bench.by_x("slope", [](int) { return 20.0; });  // ruggedness 0.909, over the 0.34 floor
    bench.finish();
    // Wealth one step away, and a seat of authority sitting on the same cell.
    bench.request.sites = {site_at(bench, "site-1", 1, kEquatorRow, 30000.0)};

    const NomadHabitatCell& here = bench.cell(0, kEquatorRow);
    check::is_true(!nomad_gate(NomadClass::Bandits, here, 0.0, nomad_ground(bench.request),
                               bench.request).has_value(),
                   "a market inside the control clearance refuses the lair");

    // Push the single settlement out past the clearance but inside strike range: the
    // productive pattern is refuge terrain beside prosperity with authority absent.
    bench.request.sites[0] = site_at(bench, "site-1", 1, kEquatorRow, 30000.0);
    bench.request.settlement_spacing_m = 8000.0;  // clearance 7200 m, strike 10400 m
    const auto held = nomad_gate(NomadClass::Bandits, here, 0.0, nomad_ground(bench.request), bench.request);
    check::is_true(held.has_value(), "rough ground with wealth in reach and no seat close raises bandits");
    check::equal(held->basis.prize_uid, std::string("site-1"), "the prize it would take");
    check::is_true(held->score > 0.0, "a scored verdict");
}

TEST(a_world_with_no_markets_makes_maximally_unpoliced_bandits) {
    Bench bench;
    bench.by_x("slope", [](int) { return 20.0; });
    bench.finish();
    // Wealth with no market anywhere: hamlets feed `wealth` but never `markets`, so the
    // authority arm sees an empty list, reads infinity, and saturates.
    NomadSite hamlet = site_at(bench, "hamlet-node-1", 1, kEquatorRow, 0.0);
    hamlet.id = "hamlet-1";
    hamlet.population_estimate.reset();
    hamlet.delivered_food = 900.0;
    bench.request.hamlets = {hamlet};

    const NomadHabitatCell& here = bench.cell(0, kEquatorRow);
    const auto held = nomad_gate(NomadClass::Bandits, here, 0.0, nomad_ground(bench.request), bench.request);
    check::is_true(held.has_value(), "no seat anywhere is the absence the third condition wants");
    const double rough = nomad_ruggedness(here);
    const double worth = std::log10(1.0 + 900.0) / 5.0;
    // The authority term is clamped at 1.0, so the score is ruggedness times worth alone.
    check::close(held->score, std::min(1.0, rough) * worth, 1e-12, "authority weakness saturates");
    check::equal(held->basis.prize_uid, std::string("hamlet-node-1"), "the hamlet's stable handle");
}

TEST(a_hamlet_is_sized_by_the_food_it_delivers) {
    NomadSite hamlet;
    hamlet.uid = "hamlet-node-3";
    hamlet.delivered_food = 640.0;
    // Without the fallback a hamlet scores zero and bandits ignore the rural settlements
    // that are their easiest prey.
    check::close(nomad_population_of(hamlet), 640.0, 1e-12, "a hamlet's weight is its yield");
    hamlet.rural_population_estimate = 120.0;
    check::close(nomad_population_of(hamlet), 120.0, 1e-12, "a head count wins over the yield");
    NomadSite empty;
    check::close(nomad_population_of(empty), 0.0, 1e-12, "and nothing at all is zero");
}

TEST(herders_need_a_year_that_moves_and_not_only_forage) {
    Bench bench;
    bench.finish();
    const NomadHabitatCell& here = bench.cell(0, kEquatorRow);
    const NomadGround ground = nomad_ground(bench.request);

    // Forage is 0.5 everywhere, well past the 0.07 floor, but a flat year feeds a settled
    // farm rather than a herd.
    check::is_true(!nomad_gate(NomadClass::Wanderers, here, 0.1, ground, bench.request).has_value(),
                   "a flat year raises no herders however green the ground");
    const auto held = nomad_gate(NomadClass::Wanderers, here, 0.5, ground, bench.request);
    check::is_true(held.has_value(), "forage plus a moving year raises herders");
    check::close(held->basis.seasonal_swing, 0.5, 1e-12, "the swing rides the basis");
    check::close(held->score, 1.0 * 0.75 * 1.0, 1e-12, "forage saturates, swing and openness scale");

    // And the swing itself inverts nothing and reads nothing without months.
    check::close(nomad_seasonal_swing({}, 0, kEquatorRow), 0.0, 1e-12,
                 "a world with no seasonal pass swings zero and raises no herders at all");
}

TEST(the_merchant_gate_needs_a_road_underfoot_and_two_markets) {
    Bench bench;
    bench.finish();
    bench.request.sites = {site_at(bench, "site-1", 1, kEquatorRow, 5000.0),
                           site_at(bench, "site-2", 2, kEquatorRow, 9000.0)};
    const NomadHabitatCell& here = bench.cell(0, kEquatorRow);

    // No road: a caravan cannot form off the roads however rich the markets.
    check::is_true(!nomad_gate(NomadClass::Merchants, here, 0.0, nomad_ground(bench.request),
                               bench.request).has_value(),
                   "two markets with no road underfoot raise no caravan");

    bench.request.roads = {{{bench.node(0, kEquatorRow), bench.node(1, kEquatorRow)}, kStep}};
    const auto held = nomad_gate(NomadClass::Merchants, here, 0.0, nomad_ground(bench.request), bench.request);
    check::is_true(held.has_value(), "a road underfoot and two markets raise a caravan");
    check::equal(held->basis.market_count, 2, "both markets counted");
    check::equal(held->basis.markets.size(), std::size_t{2}, "and both named, nearest first");
    check::equal(held->basis.markets[0], std::string("site-1"), "nearest market first");
    check::equal(held->basis.road_node, bench.node(0, kEquatorRow), "the road node it stands on");
    check::close(held->basis.road_distance_m, 0.0, 1e-6, "standing on the road");

    // One market past the population floor is not a trade: min_markets is a hard two.
    bench.request.sites[1].population_estimate = 100.0;
    check::is_true(!nomad_gate(NomadClass::Merchants, here, 0.0, nomad_ground(bench.request),
                               bench.request).has_value(),
                   "one market past the floor raises no caravan");
}

// ---------------------------------------------------------------------------
// Placement.
// ---------------------------------------------------------------------------

TEST(clearance_scales_the_placement_rate_instead_of_skipping_the_cell) {
    Bench bench;
    bench.seasons(15.0);
    bench.finish();
    // A clearance wide enough to cover most of the world: every land cell is inside it,
    // so a rule that SKIPPED cells inside the clearance -- or rejected an already-drawn
    // candidate -- would place nothing at all.
    bench.request.settlement_spacing_m = 60000.0;  // clearance 18000 m, nearly a quarter world
    bench.request.density = 400.0;
    bench.request.ages = {0};
    bench.request.sites = {site_at(bench, "site-1", 0, kEquatorRow, 5000.0)};
    bench.request.gods = {{"god_weave", {"weave"}}};
    bench.request.ley = {{"weave", "weave-node-0", bench.dir(4, kEquatorRow), 3.5}};

    const auto block = add_nomads(bench.request);
    check::is_true(block.has_value(), "the pass ran");
    check::is_true(!block->groups.empty(), "a world inside the clearance still holds bands, got " +
                   std::to_string(block->groups.size()));

    const double clearance = 0.3 * 60000.0;
    int inside = 0;
    for (const NomadBand& band : block->groups) {
        if (nomad_distance(band.direction, bench.dir(0, kEquatorRow), kRadius) < clearance) ++inside;
    }
    // The discriminating half: a rate that RAMPS leaves bands inside the clearance, and
    // both rejection shapes leave none there.
    check::is_true(inside > 0, "bands stand inside the clearance, got " + std::to_string(inside));
    check::is_true(inside < static_cast<int>(block->groups.size()),
                   "and the ramp still pushed some of them out past it");
}

TEST(empty_ground_records_a_roll_and_raises_nobody) {
    Bench bench;
    bench.finish();  // no ruins, no markets, no ley, flat, and no seasonal months
    bench.request.density = 200.0;

    const auto block = add_nomads(bench.request);
    check::is_true(block.has_value(), "the pass ran");
    // Roughly one candidate in eight satisfies nothing in a real world; here it is all of
    // them, and a roll records that rather than the pass going quiet.
    check::is_true(!block->rolls.empty(), "candidates were considered, got " +
                   std::to_string(block->rolls.size()));
    check::equal(block->groups.size(), std::size_t{0}, "and none of them raised a band");
    for (const NomadRoll& roll : block->rolls) {
        check::is_true(!roll.precipitated, "a roll on empty ground did not precipitate");
        check::is_true(roll.eligible.empty(), "and named no eligible classification");
    }
    for (int i = 0; i < kNomadClassCount; ++i) {
        check::equal(block->counts[static_cast<std::size_t>(i)], 0, "a quiet count is still present");
    }
}

TEST(occurrence_zero_leaves_the_world_settled) {
    Bench bench;
    bench.seasons(15.0);
    bench.finish();
    bench.request.ages = {0};
    bench.request.density = 200.0;

    const auto busy = add_nomads(bench.request);
    check::is_true(busy.has_value() && !busy->rolls.empty(), "the same world does raise candidates");

    bench.request.occurrence = 0.0;
    const auto quiet = add_nomads(bench.request);
    check::is_true(quiet.has_value(), "the block is still written, so a settled world is not a missing one");
    check::equal(quiet->groups.size(), std::size_t{0}, "no bands");
    check::equal(quiet->rolls.size(), std::size_t{0}, "and no candidate even reached the draw");
}

TEST(the_uid_sort_is_lexicographic_and_not_numeric) {
    Bench bench;
    bench.seasons(15.0);
    bench.finish();
    bench.request.ages = {0};
    bench.request.density = 400.0;
    bench.request.gods = {{"god_weave", {"weave"}}};
    bench.request.ley = {{"weave", "weave-node-0", bench.dir(4, kEquatorRow), 3.5}};

    const auto block = add_nomads(bench.request);
    check::is_true(block.has_value() && block->groups.size() > 4,
                   "enough bands to have an order at all, got " +
                   std::to_string(block.has_value() ? block->groups.size() : 0));
    int descending_nodes = 0;
    for (std::size_t i = 1; i < block->groups.size(); ++i) {
        check::is_true(block->groups[i - 1].uid <= block->groups[i].uid, "groups are sorted by uid string");
        if (block->groups[i - 1].node > block->groups[i].node) ++descending_nodes;
    }
    // `nomad-0-10` sorts before `nomad-0-2`, so a numeric sort is a different order and a
    // different set of fission suffixes. Non-vacuity: the two orders must actually differ
    // in this world, or the assertion above proves nothing.
    check::is_true(descending_nodes > 0,
                   "a lexicographic sort puts a higher node before a lower one somewhere");
}

TEST(placement_is_gated_on_the_stage_and_the_route_pass_is_not) {
    Bench bench;
    bench.finish();
    bench.request.phase = 15;
    check::is_true(!add_nomads(bench.request).has_value(), "no block below phase 16");
    bench.request.world_recipe = false;
    bench.request.phase = 16;
    check::is_true(!add_nomads(bench.request).has_value(), "and none without a recipe");
    bench.request.world_recipe = true;
    check::is_true(add_nomads(bench.request).has_value(), "at phase 16 with a recipe there is one");

    // The route pass is callable standalone by the request API and by the monthly tick, so
    // a phase gate there would break both.
    bench.request.phase = 3;
    bench.request.sites = {site_at(bench, "site-1", 1, kEquatorRow, 5000.0)};
    NomadBlock block;
    NomadBand band = band_at(bench, "nomad-0-25", NomadClass::Survivors, 0, kEquatorRow, 40);
    band.basis.refuge_uid = "site-1";
    block.groups.push_back(band);
    const NomadRoutes routes = add_nomad_routes(block, bench.request);
    check::equal(routes.routed, 1, "the route pass ran at phase 3");
    check::equal(std::string(to_string(block.groups[0].route_status)), std::string("routed"),
                 "and routed the band");

    // Effects keep placement's gate.
    check::is_true(!apply_nomad_effects(block, bench.request).has_value(), "effects refuse below phase 16");
}

// ---------------------------------------------------------------------------
// Routes.
// ---------------------------------------------------------------------------

TEST(every_band_is_routed_or_stranded_and_stranded_is_a_real_outcome) {
    Bench bench;
    bench.finish();
    bench.request.sites = {site_at(bench, "site-1", 1, kEquatorRow, 5000.0)};

    NomadBlock block;
    NomadBand reaches = band_at(bench, "nomad-0-a", NomadClass::Survivors, 0, kEquatorRow, 40);
    reaches.basis.refuge_uid = "site-1";
    NomadBand lost = band_at(bench, "nomad-0-b", NomadClass::Survivors, 4, kEquatorRow, 40);
    lost.basis.refuge_uid = "site-nowhere";  // names a refuge the world does not have
    block.groups = {reaches, lost};

    const NomadRoutes routes = add_nomad_routes(block, bench.request);
    check::equal(routes.routed + routes.stranded, static_cast<int>(block.groups.size()),
                 "routed and stranded account for every band");
    check::equal(routes.routed, 1, "one band reached its refuge");
    check::equal(routes.stranded, 1, "and one did not");
    const NomadBand* adrift = band_by_uid(block, "nomad-0-b");
    check::is_true(adrift != nullptr, "the stranded band is still in the world");
    // A band whose circuit cannot be built KEEPS ITS START CAMP and says so. It is a real
    // outcome, not an error: reachability needs this pass to exist.
    check::equal(adrift->camps.size(), std::size_t{1}, "it keeps the camp it started from");
    check::equal(adrift->legs.size(), std::size_t{0}, "and walks nothing");
    check::equal(std::string(to_string(adrift->route_status)), std::string("stranded"), "and says so");
}

TEST(a_herding_round_holds_midwinter_at_its_winter_camp) {
    Bench bench;
    bench.by_x("height", [](int x) { return x * 200.0; });
    bench.by_x("temperature", [](int x) { return 20.0 - 2.0 * x; });
    bench.seasons(15.0);
    bench.finish();

    NomadBlock block;
    block.groups = {band_at(bench, "nomad-0-a", NomadClass::Wanderers, 0, kEquatorRow, 120)};
    const NomadRoutes routes = add_nomad_routes(block, bench.request);
    check::equal(routes.routed, 1, "the herder routed");

    const NomadBand& herd = block.groups[0];
    const NomadCamp* winter = camp_of_kind(herd, NomadCampKind::Winter);
    const NomadCamp* summer = camp_of_kind(herd, NomadCampKind::Summer);
    const NomadCamp* base = camp_of_kind(herd, NomadCampKind::Base);
    check::is_true(winter != nullptr && summer != nullptr && base != nullptr,
                   "transhumance needs two grounds and a base, not one");
    check::is_true(winter->node != summer->node, "and the two seasons are different ground");
    check::is_true(base->node == herd.node && summer->node != herd.node && winter->node != herd.node,
                   "the start node is the base and cannot also be a seasonal camp");

    const int expected = nomad_midwinter_day(bench.cells[static_cast<std::size_t>(winter->node)].z, kN);
    check::is_true(winter->arrive_day.has_value(), "the winter camp has a window");
    check::equal(*winter->arrive_day, expected, "the year is built around midwinter at the winter camp");
    // The base is only ever a leg's SOURCE, so without the closing pass it would carry no
    // window at all.
    check::is_true(base->arrive_day.has_value() && base->depart_day.has_value(),
                   "the base a transhumant group holds still gets a window");
}

TEST(the_seasonal_phase_inverts_across_the_equator) {
    // Invisible on a world whose land happens to sit in one hemisphere, which is why it is
    // pinned here rather than by inspection.
    check::equal(nomad_midsummer_day(0, kN), 180, "the north pole's midsummer");
    check::equal(nomad_midsummer_day(kN - 1, kN), 0, "the south pole's midsummer is half a year away");
    check::equal(nomad_midwinter_day(0, kN), 0, "and its midwinter mirrors it");
    check::equal(nomad_midwinter_day(kN - 1, kN), 180, "in both directions");
    // The equator row, present only on an odd raster, takes the NORTHERN phase.
    check::equal(nomad_midsummer_day(kEquatorRow, kN), 180, "the equator takes the northern phase");
    int northern = 0;
    int southern = 0;
    for (int z = 0; z < kN; ++z) {
        if (nomad_midsummer_day(z, kN) == 180) ++northern;
        if (nomad_midsummer_day(z, kN) == 0) ++southern;
    }
    check::is_true(northern > 0 && southern > 0, "both halves of the year are actually reached");
    check::equal(northern + southern, kN, "and every row falls in one of them");
}

TEST(no_merchant_leg_exceeds_one_days_march) {
    Bench bench;
    bench.finish();
    // Two markets on the equator row, four and two steps out, so the closing leg of the
    // chain runs 40 km against a 35 km march.
    bench.request.sites = {site_at(bench, "site-a", 2, kEquatorRow, 5000.0),
                           site_at(bench, "site-b", 4, kEquatorRow, 5000.0)};

    NomadBlock block;
    block.groups = {band_at(bench, "nomad-0-a", NomadClass::Merchants, 0, kEquatorRow, 20)};
    const NomadRoutes routes = add_nomad_routes(block, bench.request);
    check::equal(routes.routed, 1, "the caravan routed");
    check::close(routes.day_march_m, kNomadDayMarchM, 1e-12, "the march is published with the block");

    const NomadBand& caravan = block.groups[0];
    check::is_true(caravan.round_length_m.value_or(0.0) > kNomadDayMarchM,
                   "the round is longer than one march, so the cut had something to do");
    // A leg that survives uncut is one a caravan can walk between dawn and dusk. A station
    // exists to stop a train sleeping in the unmonitored gap, which a 1.5-day segment does
    // not do at any elasticity.
    double longest = 0.0;
    for (const NomadLeg& leg : caravan.legs) longest = std::max(longest, leg.length_m);
    check::is_true(longest > 0.0, "the legs have length at all");
    check::is_true(longest <= kNomadDayMarchM + 1e-6,
                   "no segment exceeds one day's march, longest was " + std::to_string(longest));
    // The cut closes at the last node the day reaches, so it adds a station rather than
    // silently emitting the leg whole.
    check::is_true(caravan.camps.size() > 3, "a station was stood at the break, camps=" +
                   std::to_string(caravan.camps.size()));
    check::is_true(caravan.legs.size() > 3, "and the leg it cut became two");
}

TEST(a_band_crosses_a_grade_a_road_would_refuse) {
    Bench bench;
    // A 3 km wall across one latitude boundary: 0.6 rise over run against a 5 km
    // meridional step. A road capped at 0.4 refuses it; a band at the relief ceiling of
    // 0.4 * 2.2 = 0.88 fords it.
    bench.by_z("height", [](int z) { return z >= kEquatorRow ? 3000.0 : 0.0; });
    bench.finish();
    bench.request.settlement_spacing_m = 40000.0;  // a refuge reach that binds nothing
    bench.request.sites = {site_at(bench, "site-1", 0, kEquatorRow - 1, 5000.0)};

    const auto route_once = [&](double max_grade) {
        NomadBlock block;
        NomadBand band = band_at(bench, "nomad-0-a", NomadClass::Survivors, 0, kEquatorRow, 40);
        band.basis.refuge_uid = "site-1";
        block.groups = {band};
        NomadRequest request = bench.request;
        request.road_max_grade = max_grade;
        const NomadRoutes routes = add_nomad_routes(block, request);
        check::equal(routes.routed, 1, "the refugee routed at grade limit " + std::to_string(max_grade));
        check::equal(block.groups[0].legs.size(), std::size_t{1}, "one terminating move");
        return block.groups[0].legs[0].nodes.size();
    };

    // Straight over the wall.
    check::equal(route_once(0.4), std::size_t{2}, "a band fords ground a road at 0.4 would refuse");
    // The same wall at a ceiling of 0.44 is impassable, so the walk has to go round.
    check::is_true(route_once(0.2) > 2, "and a lower relief ceiling makes it detour instead");
}

// ---------------------------------------------------------------------------
// Fission.
// ---------------------------------------------------------------------------

namespace {

// A herding world with height and warmth gradients, so summer and winter resolve to
// different ground and a clan that outgrows its round has somewhere to send a daughter.
void herding_world(Bench& bench) {
    bench.by_x("height", [](int x) { return x * 200.0; });
    bench.by_x("temperature", [](int x) { return 20.0 - 2.0 * x; });
    bench.seasons(15.0);
    bench.finish();
}

}  // namespace

TEST(fission_seats_a_child_on_the_parents_summer_pasture) {
    Bench bench;
    herding_world(bench);

    NomadBlock block;
    // Round forage is 0.5 everywhere, which seats a capacity of about 380 head on the
    // authored 40..420 band. A clan of 420 has outgrown what its round carries.
    block.groups = {band_at(bench, "nomad-0-parent", NomadClass::Wanderers, 0, kEquatorRow, 420)};
    const double forage = 0.5;
    check::is_true(static_cast<double>(block.groups[0].size) >
                   nomad_round_capacity(NomadClass::Wanderers, forage),
                   "the clan is over the capacity its round carries");

    const NomadRoutes routes = add_nomad_routes(block, bench.request);
    check::equal(routes.fissioned, 1, "one daughter clan");
    check::equal(block.groups.size(), std::size_t{2}, "parent and child");
    check::is_true(routes.fissioned <= routes.routed, "fissioned is counted inside routed");

    const NomadBand* parent = band_by_uid(block, "nomad-0-parent");
    check::is_true(parent != nullptr, "the parent survived its own split");
    const NomadBand* child = nullptr;
    for (const NomadBand& band : block.groups) {
        if (!band.parent_uid.empty()) child = &band;
    }
    check::is_true(child != nullptr, "and a child was born");
    check::equal(child->parent_uid, parent->uid, "the child names its parent");
    const NomadCamp* summer = camp_of_kind(*parent, NomadCampKind::Summer);
    check::is_true(summer != nullptr, "the parent holds summer pasture");
    // SUMMER PASTURE FIRST, because high open ground in the warm months is what a herd
    // that has outgrown its range competes for. Its own start camp is never inheritable:
    // a split that leaves the child where the parent stands has divided nothing.
    check::equal(child->basis.inherited_camp, summer->id, "the daughter took the summer camp");
    check::equal(child->node, summer->node, "and is seated on its ground");
    check::is_true(child->node != parent->node, "which is not where the parent already stands");
    // A fissioned parent keeps its own head count: rewriting a placed band's size from
    // here would make the authored size band a fiction.
    check::equal(parent->size, 420, "the parent keeps its head count");
    check::between(static_cast<double>(child->size), 40.0, 420.0, "the child stays inside the size band");
    check::is_true(child->size >= static_cast<int>(420 * 0.3) && child->size <= static_cast<int>(420 * 0.45) + 1,
                   "and takes 30 to 45 percent of the parent, got " + std::to_string(child->size));
}

TEST(the_fission_leg_joins_two_rounds_and_is_no_part_of_the_year) {
    Bench bench;
    herding_world(bench);
    NomadBlock block;
    block.groups = {band_at(bench, "nomad-0-parent", NomadClass::Wanderers, 0, kEquatorRow, 420)};
    add_nomad_routes(block, bench.request);

    const NomadBand* parent = band_by_uid(block, "nomad-0-parent");
    const NomadBand* child = nullptr;
    for (const NomadBand& band : block.groups) {
        if (!band.parent_uid.empty()) child = &band;
    }
    check::is_true(parent != nullptr && child != nullptr, "there is a parent and a child to join");

    int joins = 0;
    double annual = 0.0;
    double total = 0.0;
    for (const NomadLeg& leg : child->legs) {
        total += leg.length_m;
        if (leg.branch != NomadBranch::Fission) {
            annual += leg.length_m;
            continue;
        }
        ++joins;
        // The ONLY leg whose `to` names a camp of a DIFFERENT band -- that is what makes
        // the branch a join between two rounds rather than one inside a round.
        check::equal(leg.to, parent->camps.front().id, "the join lands on the parent's own camp");
        check::equal(leg.from, child->camps.front().id, "and leaves the child's");
        // It happened once, so it is not a leg of anybody's year: giving it a window would
        // put the child at its parent's camp every spring.
        check::is_true(!leg.depart_day.has_value() && !leg.arrive_day.has_value(),
                       "the founding move carries no day window");
        check::is_true(leg.length_m > 0.0, "and it was actually walked");
    }
    check::equal(joins, 1, "exactly one fission leg");
    check::is_true(annual > 0.0, "the child walks an annual round of its own");
    check::is_true(total > annual, "and the join is longer than nothing, so the exclusion bites");
    // THE ANNUAL ROUND ONLY: counting the join would tell a time mover the clan travels
    // further than it does.
    check::close(child->round_length_m.value_or(-1.0), std::round(annual * 1e6) / 1e6, 1e-6,
                 "round_length_m excludes the fission leg");
}

TEST(running_the_route_pass_twice_is_running_it_once) {
    Bench bench;
    herding_world(bench);
    NomadBlock block;
    block.groups = {band_at(bench, "nomad-0-parent", NomadClass::Wanderers, 0, kEquatorRow, 420)};

    const NomadRoutes first = add_nomad_routes(block, bench.request);
    std::vector<std::string> after_first;
    for (const NomadBand& band : block.groups) after_first.push_back(band.uid);
    check::is_true(first.fissioned > 0, "the first pass bore a child");

    // Idempotent BY CONSTRUCTION, because it has to be: this pass runs again on every
    // request and on a monthly route cadence, so a step that added to the set instead of
    // replacing it would breed a new generation per call.
    const NomadRoutes second = add_nomad_routes(block, bench.request);
    std::vector<std::string> after_second;
    for (const NomadBand& band : block.groups) after_second.push_back(band.uid);
    check::equal(after_second.size(), after_first.size(), "the band count did not grow");
    for (std::size_t i = 0; i < after_first.size(); ++i) {
        check::equal(after_second[i], after_first[i], "and the same uids came back");
    }
    check::equal(second.fissioned, first.fissioned, "with the same number of daughters");
    check::equal(second.routed + second.stranded, static_cast<int>(block.groups.size()),
                 "and the ledger still accounts for every band");
}

TEST(every_band_either_names_a_roll_or_names_a_parent_that_does) {
    Bench bench;
    herding_world(bench);
    bench.request.ages = {0};
    bench.request.density = 60.0;

    auto block = add_nomads(bench.request);
    check::is_true(block.has_value() && !block->groups.empty(), "the world raised bands");
    add_nomad_routes(*block, bench.request);

    int rolled = 0;
    int descended = 0;
    for (const NomadBand& band : block->groups) {
        if (!band.parent_uid.empty()) {
            check::is_true(band_by_uid(*block, band.parent_uid) != nullptr,
                           "a child's parent is in the world: " + band.uid);
            ++descended;
            continue;
        }
        bool named = false;
        for (const NomadRoll& roll : block->rolls) {
            if (roll.precipitated && roll.uid == band.uid) named = true;
        }
        check::is_true(named, "a placed band names the roll that raised it: " + band.uid);
        ++rolled;
    }
    check::is_true(rolled > 0, "some bands were placed");
    // A child is NOT written into `rolls` -- it was never considered, it descends -- so
    // without one in the world the invariant above is only half tested.
    check::is_true(descended > 0, "and at least one descended rather than being rolled");
    int counted = 0;
    for (int i = 0; i < kNomadClassCount; ++i) counted += block->counts[static_cast<std::size_t>(i)];
    check::equal(counted, static_cast<int>(block->groups.size()),
                 "counts are recomputed from the band set, children included");
}

// ---------------------------------------------------------------------------
// Effects.
// ---------------------------------------------------------------------------

namespace {

NomadBand cult_at(const Bench& bench, const std::string& uid, const std::string& node_id) {
    NomadBand band = band_at(bench, uid, NomadClass::Cultists, 0, kEquatorRow, 30);
    band.school = "weave";
    band.basis.school = "weave";
    band.basis.god_id = "god_weave";
    band.basis.ley_node_id = node_id;
    return band;
}

}  // namespace

TEST(a_cult_deepens_a_ley_node_and_never_creates_one) {
    Bench bench;
    bench.finish();
    bench.request.ley = {{"weave", "weave-node-0", bench.dir(0, kEquatorRow), 3.0},
                         {"weave", "weave-node-1", bench.dir(1, kEquatorRow), 4.0}};

    NomadBlock block;
    block.groups = {cult_at(bench, "nomad-0-a", "weave-node-0"),
                    // Names a node its network does not have: a cult may only deepen.
                    cult_at(bench, "nomad-0-b", "weave-node-404"),
                    // Already at the ceiling: the lift is a no-op and is not counted.
                    cult_at(bench, "nomad-0-c", "weave-node-1"),
                    // Shrine-derived, so it carries no ley node at all and writes nothing.
                    cult_at(bench, "nomad-0-d", "")};

    const auto effects = apply_nomad_effects(block, bench.request);
    check::is_true(effects.has_value(), "the effects pass ran");
    check::equal(effects->leyline_edits.size(), std::size_t{1}, "exactly one node was deepened");
    check::equal(effects->leyline_edits[0].node_id, std::string("weave-node-0"), "the one that could be");
    // Worship DEEPENS a groove that was already there, so the gain is a fraction of the
    // node's existing charge: intensity * 1.18, absolute, shrine count ignored.
    check::close(effects->leyline_edits[0].intensity, 3.0 * 1.18, 1e-12, "the multiplicative lift");
    check::is_true(!effects->leyline_edits[0].hidden, "weave is a known school");
    check::equal(effects->leyline_edits_known, 1, "counted as a known edit");
    check::equal(effects->leyline_edits_hidden, 0, "and none hidden");

    // Magic off writes nothing at all.
    bench.request.magic_enabled = false;
    const auto dark = apply_nomad_effects(block, bench.request);
    check::is_true(dark.has_value(), "the pass still ran");
    check::equal(dark->leyline_edits.size(), std::size_t{0}, "but a world without magic is not deepened");
}

TEST(two_cults_on_one_node_ratchet_from_the_lifted_value) {
    Bench bench;
    bench.finish();
    bench.request.ley = {{"blood", "blood-node-0", bench.dir(0, kEquatorRow), 2.0}};

    NomadBlock block;
    for (const char* uid : {"nomad-0-b", "nomad-0-a"}) {
        NomadBand band = cult_at(bench, uid, "blood-node-0");
        band.school = "blood";
        band.basis.school = "blood";
        block.groups.push_back(band);
    }

    const auto effects = apply_nomad_effects(block, bench.request);
    check::is_true(effects.has_value(), "the effects pass ran");
    check::equal(effects->leyline_edits.size(), std::size_t{2}, "both cults wrote");
    // SORTED BY UID, so two bands touching the same node cannot produce two different
    // worlds depending on which happened to be placed first.
    check::equal(effects->leyline_edits[0].node_id, std::string("blood-node-0"), "the shared node");
    check::close(effects->leyline_edits[0].intensity, 2.0 * 1.18, 1e-12, "the first lift");
    check::close(effects->leyline_edits[1].intensity, 2.0 * 1.18 * 1.18, 1e-12,
                 "and the second ratchets off the lifted value rather than the stale one");
    // A cult may DEEPEN a hidden node that already exists; only the corruption API creates
    // one, and a hidden lift is counted apart.
    check::equal(effects->leyline_edits_hidden, 2, "a blood cult's writes are hidden ones");
    check::equal(effects->leyline_edits_known, 0, "and none of them known");
}

TEST(raid_pressure_falls_off_with_distance) {
    Bench bench;
    bench.finish();
    bench.request.sites = {site_at(bench, "city-near", 0, kEquatorRow, 5000.0),
                           site_at(bench, "city-mid", 1, kEquatorRow, 5000.0),
                           site_at(bench, "city-far", 4, kEquatorRow, 5000.0)};
    bench.request.threat_cities = {{"city-near", 0.2}, {"city-mid", 0.2}, {"city-far", 0.2}};

    NomadBlock block;
    block.groups = {band_at(bench, "nomad-0-a", NomadClass::Bandits, 0, kEquatorRow, 30)};

    const auto effects = apply_nomad_effects(block, bench.request);
    check::is_true(effects.has_value(), "the effects pass ran");
    check::equal(effects->raid_pressure.size(), std::size_t{3},
                 "every resolvable city gets a pressure row, even a quiet one");
    const NomadRaidPressure& near = effects->raid_pressure[0];
    const NomadRaidPressure& mid = effects->raid_pressure[1];
    const NomadRaidPressure& far = effects->raid_pressure[2];
    // A lair on the doorstep is not the same as one at the edge of the reach; a flat term
    // made every town in a region read alike.
    check::close(near.nomad_pressure, 0.22, 1e-9, "a lair on the doorstep presses at full weight");
    check::is_true(mid.nomad_pressure > 0.0 && mid.nomad_pressure < near.nomad_pressure,
                   "and one a step away presses less");
    check::close(far.nomad_pressure, 0.0, 1e-12, "a lair past the reach presses not at all");
    check::is_true(far.contributors.empty() && !far.regional_threat.has_value(),
                   "an unpressed city names nobody and keeps its own threat");
    check::equal(effects->towns_under_raid_pressure, 2, "two towns felt it");
    check::is_true(near.regional_threat.has_value(), "the pressed city's threat was raised");
    check::close(*near.regional_threat, 0.42, 1e-9, "on top of what it already carried");
    check::equal(near.contributors.size(), std::size_t{1}, "and names the band that pressed it");
}

TEST(a_caravan_needs_two_shared_nodes_to_ride_a_road) {
    Bench bench;
    bench.finish();
    const int a = bench.node(0, kEquatorRow);
    const int b = bench.node(1, kEquatorRow);
    const int c = bench.node(2, kEquatorRow);
    const int d = bench.node(5, kEquatorRow);
    bench.request.roads = {{{a, b, c}, 20000.0}, {{c, d}, 30000.0}};

    NomadBlock block;
    NomadBand caravan = band_at(bench, "nomad-0-a", NomadClass::Merchants, 0, kEquatorRow, 20);
    NomadLeg leg;
    leg.from = "nomad-0-a-camp-0";
    leg.to = "nomad-0-a-camp-1";
    leg.nodes = {a, b, c};
    leg.length_m = 20000.0;
    caravan.legs.push_back(leg);
    block.groups = {caravan};

    const auto effects = apply_nomad_effects(block, bench.request);
    check::is_true(effects.has_value(), "the effects pass ran");
    // A single shared node is a crossing, not a ride, so the second route is untouched.
    check::equal(effects->caravan_trade.size(), std::size_t{1}, "one road was ridden");
    check::equal(effects->caravan_trade[0].route_index, std::size_t{0}, "the one sharing three nodes");
    check::equal(effects->caravan_trade[0].riders.size(), std::size_t{1}, "by one caravan");
    check::equal(effects->caravan_trade[0].riders[0], std::string("nomad-0-a"), "named");
    // A SHARE OF NODES RIDDEN, not a modelled cargo volume.
    check::close(effects->caravan_trade[0].throughput, 1.0, 1e-12, "throughput is the share of nodes");
}

TEST(survivor_camps_are_candidates_and_absorption_counts_a_band_once) {
    Bench bench;
    bench.finish();
    bench.request.sites = {site_at(bench, "site-1", 1, kEquatorRow, 5000.0)};

    NomadBlock block;
    NomadBand band = band_at(bench, "nomad-3-25", NomadClass::Survivors, 0, kEquatorRow, 60);
    band.basis.refuge_uid = "site-1";
    band.basis.ruin_uid = "ruin-1";
    block.groups = {band};
    add_nomad_routes(block, bench.request);
    check::equal(std::string(to_string(block.groups[0].route_status)), std::string("routed"),
                 "the refugees reached the town");

    auto effects = apply_nomad_effects(block, bench.request);
    check::is_true(effects.has_value(), "the effects pass ran");
    check::is_true(effects->assign_settlement_candidates, "a candidate list was produced");
    check::equal(effects->settlement_candidates.size(), std::size_t{1}, "one candidate");
    NomadSettlementCandidate& candidate = effects->settlement_candidates[0];
    check::equal(candidate.from_band, std::string("nomad-3-25"), "named for the band that walked");
    // A candidate does NOT name free ground: it names the refuge city's own node, because
    // the flight routes the band to the node of the site whose uid is the refuge.
    check::equal(candidate.node, bench.request.sites[0].node, "and stands on the refuge's own node");
    check::equal(candidate.population_estimate, 60LL, "carrying the band's head count");

    std::vector<NomadSite> survivors = bench.request.sites;
    const NomadAbsorptionResult first = absorb_survivor_camps(effects->settlement_candidates,
                                                             block.groups, survivors, 4);
    check::equal(first.absorbed.size(), std::size_t{1}, "the town absorbed the band");
    check::equal(first.people, 60LL, "and its people");
    // The count MUST ride absorbed_refugees: the settlement rebuild re-derives
    // population_estimate from the budget and would erase a number written into it.
    check::equal(survivors[0].absorbed_refugees, 60LL, "the refugees ride the carried field");
    check::equal(survivors[0].absorbed_bands.size(), std::size_t{1}, "and the band is on the record");
    check::equal(*effects->settlement_candidates[0].absorbed_age, 4, "the candidate is stamped");

    // A world can carry a previous advance's candidate list unchanged, so absorbing again
    // would breed people every age out of one migration.
    const NomadAbsorptionResult again = absorb_survivor_camps(effects->settlement_candidates,
                                                              block.groups, survivors, 5);
    check::equal(again.absorbed.size(), std::size_t{0}, "a second advance absorbs nobody new");
    check::equal(survivors[0].absorbed_refugees, 60LL, "and the town did not grow twice");
}

TEST(a_refuge_that_fell_absorbs_nobody) {
    Bench bench;
    bench.finish();
    bench.request.sites = {site_at(bench, "site-1", 1, kEquatorRow, 5000.0)};

    NomadBlock block;
    NomadBand band = band_at(bench, "nomad-3-25", NomadClass::Survivors, 0, kEquatorRow, 60);
    band.basis.refuge_uid = "site-1";
    block.groups = {band};
    add_nomad_routes(block, bench.request);
    auto effects = apply_nomad_effects(block, bench.request);
    check::is_true(effects.has_value() && effects->settlement_candidates.size() == 1,
                   "there is a candidate to absorb");

    // Called with the cities that SURVIVED the boundary. The refuge is not among them.
    std::vector<NomadSite> survivors = {site_at(bench, "site-2", 3, kEquatorRow, 5000.0)};
    const NomadAbsorptionResult result = absorb_survivor_camps(effects->settlement_candidates,
                                                               block.groups, survivors, 4);
    check::equal(result.absorbed.size(), std::size_t{0}, "a refuge that fell absorbs nobody");
    check::equal(result.people, 0LL, "and nobody arrives");
    check::equal(survivors[0].absorbed_refugees, 0LL, "the surviving stranger is untouched");
    check::is_true(!effects->settlement_candidates[0].absorbed_age.has_value(),
                   "and the candidate is not stamped as absorbed");

    // The filter working, not the pass failing: hand it the refuge and it lands.
    std::vector<NomadSite> standing = bench.request.sites;
    const NomadAbsorptionResult landed = absorb_survivor_camps(effects->settlement_candidates,
                                                               block.groups, standing, 4);
    check::equal(landed.absorbed.size(), std::size_t{1}, "the same candidate absorbs into a standing refuge");
}
