// A city's node-scale layout (city_layout.hpp): the building pack it draws, the profile its
// districts follow, the district anchors and the asset requirement the draw leaves.
//
// The reference's own tests are mirrored where they apply: test_terrain_city_layout.py
// (an empty candidate set does not bypass the terrain constraints; generated layouts are
// deterministic and stand on safe land), test_settlement_records.py (no placement claim, the
// asset requirement survives, the population figure names its axis, and the survivor
// carry-back), and test_terrain_biome_contract.py (the pack's natural-or-exact-variant ground
// gate, and an explicit empty selector matching nothing). Beside them, one test per rule the
// port carries: pack eligibility and the fallback, the riverfront profile, the asset
// accounting, the re-plan after sizing and the carry across a rebuild.
//
// Every shipped world is too coarse for the anchor band to hold more than the city's own
// node (see the scale-defect test at the bottom), so the rules that need anchors are tested
// on a synthetic lattice fine enough to hold hundreds.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "fwg/city_layout.hpp"
#include "fwg/json_value.hpp"
#include "fwg/serialize.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

// Nodes on a square lattice `spacing` metres apart, 4-connected, in row-major order.
SphereGrid lattice(int side, double spacing) {
    SphereGrid g;
    g.n = side;
    g.radius = 1.0;
    const auto index = [side](int i, int j) { return j * side + i; };
    for (int j = 0; j < side; ++j) {
        for (int i = 0; i < side; ++i) {
            g.point_x.push_back(i);
            g.point_z.push_back(j);
            g.areas.push_back(1.0);
        }
    }
    g.neighbours.resize(static_cast<std::size_t>(side) * static_cast<std::size_t>(side));
    for (int j = 0; j < side; ++j) {
        for (int i = 0; i < side; ++i) {
            auto& out = g.neighbours[static_cast<std::size_t>(index(i, j))];
            if (i > 0) out.push_back({index(i - 1, j), spacing});
            if (i + 1 < side) out.push_back({index(i + 1, j), spacing});
            if (j > 0) out.push_back({index(i, j - 1), spacing});
            if (j + 1 < side) out.push_back({index(i, j + 1), spacing});
        }
    }
    return g;
}

const LayoutProfile& profile_named(std::string_view id) {
    const LayoutProfile* profile = layout_profile(id);
    if (profile == nullptr) check::fail("the catalogue to hold layout profile " + std::string(id));
    return *profile;
}

const BuildingPack& pack_named(std::string_view id) {
    const BuildingPack& pack = building_pack_or_fallback(id);
    if (pack.id != id) check::fail("the catalogue to hold building pack " + std::string(id));
    return pack;
}

// A profile's own count for a feature, or the default table's.
const FeatureCount& spec_of(const LayoutProfile& profile, std::string_view name) {
    for (const LayoutFeature& f : profile.features) {
        if (f.name == name) return f.count;
    }
    for (const LayoutFeature& f : layout_feature_defaults()) {
        if (f.name == name) return f.count;
    }
    check::fail("a count for layout feature " + std::string(name));
}

// Everything a layout record says, as one comparable string.
std::string describe(const CityLayout& l) {
    std::ostringstream out;
    out.precision(17);
    out << l.version << '|' << l.layout_profile_id << '|' << l.layout_profile_name << '|' << l.population_profile << '|'
        << (l.population_estimate ? std::to_string(*l.population_estimate) : "none") << '|'
        << l.river.adjacent_river_edges << ',' << (l.river.river_distance_m ? *l.river.river_distance_m : -1.0) << ','
        << l.river.bridge_recommended << ',' << l.river.bridge_threshold << '|' << l.building_pack_id.value_or("none")
        << '|';
    for (const RequiredAsset& a : l.required_assets) out << a.asset_id << 'x' << a.count << ';';
    out << '|' << l.required_asset_count << '|' << l.required_node_slots << '|' << l.asset_anchors_missing << '|';
    for (const LayoutFeaturePlan& f : l.features) {
        out << f.name << ':' << f.target_count << '/' << f.count << '[';
        for (const LayoutAnchor& a : f.anchors) out << a.node << '@' << a.distance_to_city_m << ',';
        out << ']';
    }
    if (l.constraints) {
        out << '|' << l.constraints->max_anchor_slope_degrees << ',' << l.constraints->river_buffer_m << ','
            << l.constraints->min_river_distance_m;
    }
    out << '|' << l.fallback.value_or("") << '|' << l.authored_by;
    return out.str();
}

// One generated phase-10 world, shared: every test below reads it and none may change it.
const World& founded_world() {
    static const World w = [] {
        Request r;
        r.seed = 42;
        r.size = 33;
        r.phase = 10;
        return generate(resolve(r));
    }();
    return w;
}

std::vector<double> nodes_of(const World& w, const SphereGrid& grid, const char* name) {
    std::vector<double> out(grid.node_count(), 0.0);
    const Grid& layer = w.layers.at(name);
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = layer.at(grid.point_x[i], grid.point_z[i]);
    return out;
}

}  // namespace

// --- the tables ------------------------------------------------------------------------

TEST(city_layout_the_tables_carry_who_may_build_what) {
    // Nothing in buildings.json says which people may draw a pack or build an option: the
    // reference derives both from each civilization's bindings at load, and so does the
    // codegen. These are the derived facts a draw depends on.
    const auto people = [](std::span<const std::string_view> ids) {
        return std::set<std::string>(ids.begin(), ids.end());
    };
    check::is_true(people(pack_named("city_pack_dwarven_metalwork").criteria.population_profiles) ==
                       std::set<std::string>{"dwarf", "frosthold_dwarf"},
                   "only the two dwarven peoples may draw the forgehold");
    check::is_true(people(pack_named("city_pack_gnome_foundry").criteria.population_profiles) ==
                       std::set<std::string>{"gnome"},
                   "only gnomes may draw the foundry");
    check::equal(people(pack_named(fallback_building_pack_id()).criteria.population_profiles).size(),
                 civilization_ids().size(), "every people binds the fallback pack");
    // Ground gating is whether the criteria NAME the ground lists, not whether they are full.
    check::is_true(!pack_named("city_pack_generic_surface").criteria.ground_gated, "the generic pack is ungated");
    check::is_true(!pack_named("city_pack_gnome_foundry").criteria.ground_gated, "the foundry gates on metal only");
    check::is_true(pack_named("city_pack_tideport").criteria.ground_gated, "the tideport gates on ground");

    // The one bridge-bound gate is the rivergate's; the tideport's harbour gate says false.
    int bridge_bound = 0;
    for (const BuildingPack& pack : building_packs()) {
        for (const BuildingOption& option : pack.building_options) {
            check::is_true(!option.profiles.empty(), std::string(pack.id) + "/" + std::string(option.id) +
                                                          " to be buildable by somebody");
            if (!option.requires_bridge) continue;
            ++bridge_bound;
            check::equal(std::string(pack.id) + "/" + std::string(option.id),
                         std::string("city_pack_human_rivergate/river_gate"), "the bridge-bound gate");
        }
    }
    check::equal(bridge_bound, 1, "exactly one option needs a bridge");
    check::equal(building_pack_schema_version(), 3, "the pack catalogue schema the sites publish");

    // A declared layout feature applies to the peoples that bind it; a default one keeps the
    // default table's list, which names ids no civilization in this registry uses.
    for (const LayoutProfile& profile : layout_profiles()) {
        for (const LayoutFeature& feature : profile.features) {
            check::is_true(feature.count.profiles.has_value(),
                           std::string(profile.id) + "/" + std::string(feature.name) + " to name who it applies to");
            check::equal(feature.count.profiles->size(), civilization_ids().size(),
                         std::string(profile.id) + "/" + std::string(feature.name) + " bound by every people");
        }
    }
    const FeatureCount& religious = spec_of(LayoutProfile{}, "religious_building");
    check::is_true(religious.profiles.has_value() && people(*religious.profiles) ==
                                                         std::set<std::string>{"human", "elf", "gnome", "dwarf", "tidekin"},
                   "the default table's own religious_building list, verbatim");
    check::is_true(!spec_of(LayoutProfile{}, "leader_homes").profiles.has_value(), "a default with no list is everyone's");
}

// --- the pack --------------------------------------------------------------------------

TEST(city_layout_a_pack_is_drawn_only_by_the_peoples_bound_to_it_and_by_weight) {
    // Ground the highland and metal packs admit and the lowland ones refuse: exposed rock,
    // high, gentle, rich in metal, 50 m from fresh water.
    PackGround rock;
    rock.biome = 5;
    rock.height_m = 100.0;
    rock.slope_degrees = 5.0;
    rock.resource = 0.6;
    rock.freshwater_m = 50.0;
    constexpr std::uint32_t kDraws = 3000;
    const auto drawn = [&](std::string_view people) {
        std::map<std::string, int> seen;
        for (std::uint32_t seed = 0; seed < kDraws; ++seed) ++seen[std::string(pick_building_pack(seed, people, rock))];
        return seen;
    };
    const auto ids = [](const std::map<std::string, int>& seen) {
        std::set<std::string> out;
        for (const auto& [id, n] : seen) out.insert(id);
        return out;
    };

    // Dwarves bind the generic pack, the highland hold and their forgehold. The gnome
    // foundry admits this ground too, and is not theirs to draw.
    const auto dwarf = drawn("dwarf");
    check::is_true(ids(dwarf) == std::set<std::string>{"city_pack_generic_surface", "city_pack_human_highland",
                                                        "city_pack_dwarven_metalwork"},
                   "a dwarf draws exactly its own three packs here");
    check::is_true(ids(drawn("gnome")) == std::set<std::string>{"city_pack_generic_surface", "city_pack_gnome_foundry"},
                   "a gnome draws exactly its own two");
    // Humans bind the rivergate as well, which wants marsh or boreal forest.
    check::is_true(ids(drawn("human_heartland")) ==
                       std::set<std::string>{"city_pack_generic_surface", "city_pack_human_highland"},
                   "a human draws the generic pack and the highland hold here, never the rivergate");
    // Elves bind the woodland and the tideport, neither of which admits rock.
    check::is_true(ids(drawn("elf")) == std::set<std::string>{"city_pack_generic_surface"}, "an elf on rock draws generic");

    // One cumulative draw weighted by pack weight: 1.0 : 0.8 : 1.5 for the dwarves' three.
    check::close(dwarf.at("city_pack_dwarven_metalwork") / static_cast<double>(kDraws), 1.5 / 3.3, 0.04,
                 "the forgehold's share");
    check::close(dwarf.at("city_pack_human_highland") / static_cast<double>(kDraws), 0.8 / 3.3, 0.04,
                 "the highland hold's share");
    check::equal(std::string(pick_building_pack(7, "dwarf", rock)), std::string(pick_building_pack(7, "dwarf", rock)),
                 "a function of its seed");
}

TEST(city_layout_ground_no_pack_admits_falls_back) {
    // A city with no fresh water in reach reads -1, below every pack's minimum -- the
    // unconditional generic pack's included -- so it FALLS BACK rather than drawing.
    PackGround dry;
    dry.biome = 3;
    dry.height_m = 10.0;
    dry.resource = 0.5;
    dry.freshwater_m = -1.0;
    static const std::string_view kHuman[] = {"human_heartland"};
    BuildingPack open{};
    open.id = "open";
    open.weight = 1.0;
    open.criteria.population_profiles = kHuman;
    const BuildingPack one[] = {open};
    check::equal(std::string(pick_building_pack(one, "the-fallback", 1, "human_heartland", dry)),
                 std::string("the-fallback"), "unreachable water refuses even an unconditional pack");
    PackGround wet = dry;
    wet.freshwater_m = 0.0;
    check::equal(std::string(pick_building_pack(one, "the-fallback", 1, "human_heartland", wet)), std::string("open"),
                 "and water at the door admits it");
    check::equal(std::string(pick_building_pack(one, "the-fallback", 1, "dwarf", wet)), std::string("the-fallback"),
                 "a people the pack is not bound to falls back");
    for (const std::string_view people : civilization_ids()) {
        check::equal(std::string(pick_building_pack(5, people, dry)), std::string(fallback_building_pack_id()),
                     std::string(people) + " without water takes the fallback pack");
    }

    // Each numeric gate refuses on its own side of its bound.
    const auto admits = [&](BuildingPack pack, const PackGround& ground) {
        const BuildingPack packs[] = {pack};
        return pick_building_pack(packs, "the-fallback", 3, "human_heartland", ground) == "open";
    };
    BuildingPack gated = open;
    gated.criteria.height_max_m = 5.0;
    check::is_true(!admits(gated, wet), "above the height ceiling");
    gated = open;
    gated.criteria.height_min_m = 20.0;
    check::is_true(!admits(gated, wet), "below the height floor");
    gated = open;
    gated.criteria.slope_max_degrees = -1.0;
    check::is_true(!admits(gated, wet), "steeper than the slope ceiling");
    gated = open;
    gated.criteria.resource_min = 0.6;
    check::is_true(!admits(gated, wet), "poorer than the resource floor");
    gated = open;
    gated.criteria.resource_max = 0.4;
    check::is_true(!admits(gated, wet), "richer than the resource ceiling");
    gated = open;
    gated.criteria.freshwater_distance_m_min = 100.0;
    check::is_true(!admits(gated, wet), "nearer water than the pack wants");
    gated = open;
    gated.criteria.freshwater_distance_m_max = -0.5;
    check::is_true(!admits(gated, wet), "farther from water than the pack allows");
}

TEST(city_layout_a_pack_matches_the_natural_biome_or_the_exact_variant) {
    // The reference's test_building_selection_matches_natural_or_exact_variant.
    static const std::string_view kHuman[] = {"human_heartland"};
    static const int kDesert[] = {2};
    static const std::string_view kForestEarth[] = {"forest.earth"};
    BuildingPack state{};
    state.id = "explicit-state-pack";
    state.weight = 1.0;
    state.criteria.ground_gated = true;
    state.criteria.biomes = kDesert;
    state.criteria.biome_variants = kForestEarth;
    state.criteria.population_profiles = kHuman;
    const BuildingPack packs[] = {state};
    PackGround forest;
    forest.biome = 4;
    forest.height_m = 20.0;
    forest.slope_degrees = 2.0;
    forest.resource = 0.5;
    forest.freshwater_m = 50.0;
    forest.variant = "forest.earth";
    check::equal(std::string(pick_building_pack(packs, "fallback", 42, "human_heartland", forest)),
                 std::string("explicit-state-pack"), "the exact variant admits it");
    forest.variant = "forest.weave";
    check::equal(std::string(pick_building_pack(packs, "fallback", 42, "human_heartland", forest)),
                 std::string("fallback"), "another variant of the same ground does not");
    PackGround desert = forest;
    desert.biome = 2;
    desert.variant = {};
    check::equal(std::string(pick_building_pack(packs, "fallback", 42, "human_heartland", desert)),
                 std::string("explicit-state-pack"), "the natural biome admits it with no variant at all");

    // The reference's test_explicit_empty_building_variant_selector_matches_nothing: a pack
    // that NAMES its ground lists, both empty, matches no ground anywhere.
    BuildingPack empty_selector = state;
    empty_selector.criteria.biomes = {};
    empty_selector.criteria.biome_variants = {};
    const BuildingPack empty_packs[] = {empty_selector};
    forest.variant = "forest.earth";
    check::equal(std::string(pick_building_pack(empty_packs, "fallback", 42, "human_heartland", forest)),
                 std::string("fallback"), "an explicit empty selector matches nothing");
    empty_selector.criteria.ground_gated = false;
    const BuildingPack ungated[] = {empty_selector};
    check::equal(std::string(pick_building_pack(ungated, "fallback", 42, "human_heartland", forest)),
                 std::string("explicit-state-pack"), "while a pack that names no ground list is ungated");
}

// --- the profile -----------------------------------------------------------------------

TEST(city_layout_a_river_city_takes_the_riverfront_profile_unless_its_pack_names_one) {
    const auto profiles = layout_profiles();
    const std::string fallback(fallback_layout_profile_id());
    const auto pick = [&](std::string_view forced, double river_m) {
        return std::string(pick_layout_profile(forced, profiles, fallback, river_m).id);
    };
    // A pack that names a profile gets it, however near or far the river.
    check::equal(std::string(pick_layout_profile(pack_named("city_pack_human_rivergate"), kInf).id),
                 std::string("riverfront_city_layout"), "the rivergate's own profile, far from any river");
    check::equal(std::string(pick_layout_profile(pack_named("city_pack_generic_surface"), 0.0).id),
                 std::string("standard_city_layout"), "the generic pack's own profile, on the river itself");

    // A pack that names none: within 1.8 riverfront buffers of a river, the riverfront.
    const double band = profile_named(kRiverfrontLayoutProfileId).placement.river_buffer_m * 1.8;
    check::equal(pick("", 0.0), std::string("riverfront_city_layout"), "on the river");
    check::equal(pick("", band), std::string("riverfront_city_layout"), "at the edge of the band, inclusive");
    check::equal(pick("", std::nextafter(band, kInf)), fallback, "just past it");
    check::equal(pick("", kInf), fallback, "on a world with no river");
    // A named profile the catalogue does not hold is no choice at all.
    check::equal(pick("no-such-profile", 10.0), std::string("riverfront_city_layout"), "an unknown name falls through");
    // With no riverfront profile to give, the fallback, even beside the river.
    const LayoutProfile only[] = {profile_named("standard_city_layout")};
    check::equal(std::string(pick_layout_profile("", only, "standard_city_layout", 0.0).id),
                 std::string("standard_city_layout"), "no riverfront profile in the catalogue");
}

// --- the anchors and the plan ----------------------------------------------------------

TEST(city_layout_an_empty_candidate_set_does_not_bypass_the_terrain_constraints) {
    // The reference's test_empty_candidate_set_does_not_bypass_terrain_constraints, on its
    // own three-node row.
    SphereGrid row;
    row.n = 3;
    row.radius = 1.0;
    row.point_x = {0, 1, 2};
    row.point_z = {0, 0, 0};
    row.areas = {1.0, 1.0, 1.0};
    row.neighbours = {{{1, 10.0}}, {{0, 10.0}, {2, 10.0}}, {{1, 10.0}}};
    const std::vector<double> near_river = {0.0, 20.0, 40.0};
    const std::vector<double> water = {0.0, 0.0, 0.0};
    const std::vector<double> steep = {30.0, 30.0, 30.0};
    const std::vector<AnchorCandidate> candidates = land_anchor_candidates(row, 0, near_river, water, steep, 12.0, 50.0);
    check::is_true(candidates.empty(), "every node too steep and too near the river");
    // Each constraint alone empties the set: neither is standing in for the other.
    const std::vector<double> far_river = {kInf, kInf, kInf};
    const std::vector<double> gentle = {0.0, 0.0, 0.0};
    check::is_true(land_anchor_candidates(row, 0, far_river, water, steep, 12.0, 50.0).empty(), "too steep alone");
    check::is_true(land_anchor_candidates(row, 0, near_river, water, gentle, 12.0, 50.0).empty(), "too near the river alone");
    check::equal(land_anchor_candidates(row, 0, far_river, water, gentle, 12.0, 50.0).size(), std::size_t{3},
                 "and the same row admits all three when neither applies");

    const std::vector<double> river = {1.0, 1.0, 0.0};
    const CityLayout plan =
        plan_city_layout(7, 0, profile_named("standard_city_layout"), "human_heartland", 200, 0.0, candidates, row, river, nullptr);
    check::is_true(plan.fallback.has_value(), "a stated fallback rather than an anchor on forbidden ground");
    // No placement claim, and the requirement survives: here the plan drew from no pack, so
    // nothing could go unanchored and the fallback comes from the district pass.
    check::is_true(!plan.asset_anchors_missing, "no asset draw ran short");
    check::is_true(plan.required_assets.empty() && plan.required_asset_count == 0, "no asset required");
    check::is_true(!plan.building_pack_id.has_value(), "no pack");
    bool any_target = false;
    for (const LayoutFeaturePlan& f : plan.features) {
        check::equal(f.count, 0, f.name + " claimed nothing");
        any_target = any_target || f.target_count > 0;
    }
    check::is_true(any_target, "while the profile asked for districts");
}

TEST(city_layout_the_anchor_band_is_dry_gentle_ground_off_the_river_nearest_first) {
    // 40 m steps: the standard profile's 540 m band is 13 steps each way.
    const int side = 41;
    const SphereGrid grid = lattice(side, 40.0);
    const int centre = 20 * side + 20;
    std::vector<double> water(grid.node_count(), 0.0);
    std::vector<double> slope(grid.node_count(), 0.0);
    std::vector<double> river(grid.node_count(), 0.0);
    for (int j = 0; j < side; ++j) {
        river[static_cast<std::size_t>(j * side + 26)] = 1.0;  // a river six steps east
        water[static_cast<std::size_t>(j * side + 14)] = 1.0;  // a lake shore six steps west
        slope[static_cast<std::size_t>(j * side + 20)] = j < 20 ? 30.0 : 0.0;  // a cliff due north
    }
    const std::vector<double> to_river = river_distances(grid, river);
    const LayoutProfile& profile = profile_named("standard_city_layout");
    const double buffer = profile.placement.river_buffer_m;
    const double max_slope = profile.placement.max_anchor_slope_degrees;
    const std::vector<AnchorCandidate> band = land_anchor_candidates(grid, centre, to_river, water, slope, max_slope, buffer);
    check::is_true(band.size() > 50, "a band holding many nodes");
    const double walk = std::max(180.0, 4.0 * buffer + 180.0);
    std::set<int> seen;
    for (std::size_t k = 0; k < band.size(); ++k) {
        const auto u = static_cast<std::size_t>(band[k].node);
        check::is_true(seen.insert(band[k].node).second, "each node once");
        check::equal(water[u], 0.0, "on dry ground");
        check::is_true(slope[u] <= max_slope, "gentle enough");
        check::is_true(to_river[u] >= buffer, "clear of the river buffer");
        check::is_true(band[k].distance_m <= walk, "inside the walking band");
        if (k > 0) {
            check::is_true(band[k - 1].distance_m < band[k].distance_m ||
                               (band[k - 1].distance_m == band[k].distance_m && band[k - 1].node < band[k].node),
                           "nearest first, node breaking ties");
        }
    }
    // What the filters took: the lake shore and the river margin are unreached, not merely
    // skipped. The walk crosses water -- the relaxation has no terrain filter -- so ground
    // beyond the lake is still in the band.
    const auto has = [&](int i, int j) { return seen.count(j * side + i) != 0; };
    check::is_true(!has(14, 20) && !has(26, 20) && !has(24, 20), "no lake, no river, no margin within the buffer");
    check::is_true(has(13, 20), "ground beyond the lake is reached across it");
    check::is_true(!has(20, 15) && has(20, 25), "the cliff refused, the flat south admitted");
}

TEST(city_layout_the_asset_draw_accounts_for_every_asset_it_requires) {
    // Ground wide enough that the 540 m band sits wholly inside it, so its rim -- where the
    // walls go -- is the whole 108-node diamond edge, and every option finds its nodes.
    const int side = 57;
    const SphereGrid grid = lattice(side, 20.0);
    const int centre = 28 * side + 28;
    const std::vector<double> dry(grid.node_count(), 0.0);
    const std::vector<double> flat(grid.node_count(), 0.0);
    const std::vector<double> no_river(grid.node_count(), 0.0);
    const std::vector<double> far = river_distances(grid, no_river);
    const LayoutProfile& profile = profile_named("standard_city_layout");
    const std::vector<AnchorCandidate> band =
        land_anchor_candidates(grid, centre, far, dry, flat, profile.placement.max_anchor_slope_degrees,
                               profile.placement.river_buffer_m);
    check::is_true(band.size() > 1000, "a band of over a thousand nodes");

    const BuildingPack& pack = pack_named("city_pack_generic_surface");
    const std::string people = "human_heartland";
    const long long residents = 20000;
    const CityLayout plan = plan_city_layout(99, centre, profile, people, residents, kInf, band, grid, no_river, &pack);

    // The roll-ups agree with themselves.
    long long sum = 0;
    for (std::size_t k = 0; k < plan.required_assets.size(); ++k) {
        sum += plan.required_assets[k].count;
        check::is_true(plan.required_assets[k].count > 0, "no zero row");
        if (k > 0) {
            check::is_true(plan.required_assets[k - 1].asset_id < plan.required_assets[k].asset_id,
                           "sorted by asset id, each once");
        }
    }
    check::equal(plan.required_asset_count, sum, "the count is the rows' sum");

    // Every asset is one the pack offers this people.
    std::set<std::string> offered;
    for (const BuildingOption& option : pack.building_options) {
        if (std::find(option.profiles.begin(), option.profiles.end(), people) == option.profiles.end()) continue;
        for (const AssetChoice& c : option.asset_choices) offered.insert(std::string(c.asset_id));
    }
    for (const RequiredAsset& a : plan.required_assets) {
        check::is_true(offered.count(a.asset_id) == 1, a.asset_id + " to be one the pack offers");
    }

    // With ground to spare every anchored and wall option takes its whole target, one asset
    // per node it claims. A river gate has no river to stand on and takes nothing, which is
    // the shortfall the record reports.
    long long anchored = 0;
    bool gates = false;
    for (const BuildingOption& option : pack.building_options) {
        if (std::find(option.profiles.begin(), option.profiles.end(), people) == option.profiles.end()) continue;
        const int target = resolve_feature_count(option.count, residents, option.required);
        if (option.placement == "anchored" || option.placement == "wall") anchored += target;
        if (option.placement == "river_gate" && target > 0) gates = true;
    }
    check::equal(static_cast<long long>(plan.required_node_slots), anchored, "one node per anchored or wall building");
    check::equal(plan.required_asset_count, anchored, "one asset per building placed, none for a gate with no river");
    check::is_true(gates && plan.asset_anchors_missing, "the gates' shortfall reported");
    check::equal(plan.fallback.value_or(""),
                 std::string("Not enough valid non-water land anchors for requested feature and building placement "
                             "counts within local search band."),
                 "the building pass's reason");

    // No node is claimed twice: the districts first, the buildings from what they leave.
    long long districts = 0;
    std::set<int> district_nodes;
    for (const LayoutFeaturePlan& f : plan.features) {
        districts += f.count;
        check::equal(f.count, f.target_count, f.name + " fully anchored on ground this wide");
        for (const LayoutAnchor& a : f.anchors) check::is_true(district_nodes.insert(a.node).second, "a district node once");
    }
    check::is_true(districts + plan.required_node_slots <= static_cast<long long>(band.size()), "the band is not overdrawn");
    check::equal(describe(plan_city_layout(99, centre, profile, people, residents, kInf, band, grid, no_river, &pack)),
                 describe(plan), "a function of its seed");

    // Scarce ground: 150 m steps leave the band 25 nodes, fewer than the districts alone want.
    // They take every one, and the buildings -- which take only what the districts left --
    // claim nothing, draw nothing, and report the shortfall.
    const SphereGrid coarse = lattice(side, 150.0);
    const std::vector<AnchorCandidate> few =
        land_anchor_candidates(coarse, centre, far, dry, flat, profile.placement.max_anchor_slope_degrees,
                               profile.placement.river_buffer_m);
    check::equal(few.size(), std::size_t{25}, "a band of 25 nodes");
    const CityLayout crowded = plan_city_layout(99, centre, profile, people, residents, kInf, few, coarse, no_river, &pack);
    long long claimed = 0;
    for (const LayoutFeaturePlan& f : crowded.features) claimed += f.count;
    check::equal(claimed, 25LL, "the districts take the whole band");
    check::equal(crowded.required_node_slots, 0, "leaving the buildings no node");
    check::equal(crowded.required_asset_count, 0LL, "and so no asset");
    check::is_true(crowded.asset_anchors_missing, "which the record reports");
}

TEST(city_layout_a_bridge_gate_needs_a_fork_and_a_structural_option_needs_no_node) {
    // 100 m steps: a node beside a river stands clear of the 90 m buffer, so the river's own
    // bank is a gate frontier. Rivers two columns apart put the city between them: two river
    // neighbours, which is a fork.
    const int side = 11;
    const SphereGrid grid = lattice(side, 100.0);
    const int centre = 5 * side + 5;
    const std::vector<double> dry(grid.node_count(), 0.0);
    const std::vector<double> flat(grid.node_count(), 0.0);
    std::vector<double> forked(grid.node_count(), 0.0);
    std::vector<double> single(grid.node_count(), 0.0);
    for (int j = 0; j < side; ++j) {
        forked[static_cast<std::size_t>(j * side + 4)] = 1.0;
        forked[static_cast<std::size_t>(j * side + 6)] = 1.0;
        single[static_cast<std::size_t>(j * side + 6)] = 1.0;
    }
    const LayoutProfile& profile = profile_named("standard_city_layout");

    static const std::string_view kHuman[] = {"human_heartland"};
    static const AssetChoice kGate[] = {{"asset_gate", 1.0}};
    static const AssetChoice kHall[] = {{"asset_hall", 1.0}};
    FeatureCount two{};
    two.base = 2.0;
    BuildingOption gate{};
    gate.id = "gate";
    gate.name = "Gate";
    gate.count = two;
    gate.required = true;
    gate.requires_bridge = true;
    gate.placement = "river_gate";
    gate.asset_choices = kGate;
    gate.profiles = kHuman;
    BuildingOption hall = gate;
    hall.id = "hall";
    hall.requires_bridge = false;
    hall.required = false;
    hall.placement = "structural";
    hall.asset_choices = kHall;
    const BuildingOption options[] = {gate, hall};
    BuildingPack pack{};
    pack.id = "test-pack";
    pack.weight = 1.0;
    pack.criteria.population_profiles = kHuman;
    pack.building_options = options;

    const auto plan_on = [&](const std::vector<double>& river) {
        const std::vector<double> to_river = river_distances(grid, river);
        const auto band = land_anchor_candidates(grid, centre, to_river, dry, flat, profile.placement.max_anchor_slope_degrees,
                                                 profile.placement.river_buffer_m);
        return plan_city_layout(5, centre, profile, "human_heartland", 1000, to_river[static_cast<std::size_t>(centre)],
                                band, grid, river, &pack);
    };
    const auto count_of = [](const CityLayout& plan, const std::string& id) {
        for (const RequiredAsset& a : plan.required_assets) {
            if (a.asset_id == id) return a.count;
        }
        return 0LL;
    };

    const CityLayout fork = plan_on(forked);
    check::equal(fork.river.adjacent_river_edges, 2, "two river neighbours");
    check::is_true(fork.river.bridge_recommended, "a fork recommends a bridge");
    check::equal(count_of(fork, "asset_gate"), 2LL, "so both gates stand on the bank");
    check::equal(count_of(fork, "asset_hall"), 2LL, "a structural option draws its whole target");
    check::equal(fork.required_node_slots, 2, "and claims no node: only the gates took one");
    check::is_true(!fork.asset_anchors_missing, "nothing short");

    const CityLayout bank = plan_on(single);
    check::equal(bank.river.adjacent_river_edges, 1, "one river neighbour");
    check::is_true(!bank.river.bridge_recommended, "is no fork");
    check::equal(count_of(bank, "asset_gate"), 0LL, "so the bridge gate is skipped");
    check::is_true(bank.asset_anchors_missing, "and, being required, reports its anchor missing");
    check::equal(bank.required_node_slots, 0, "nothing claimed");
    gate.required = false;
    const BuildingOption optional_options[] = {gate, hall};
    pack.building_options = optional_options;
    check::is_true(!plan_on(single).asset_anchors_missing, "an optional bridge gate skipped is no shortfall");
}

// --- the stage -------------------------------------------------------------------------

TEST(city_layout_every_founded_city_draws_a_pack_and_publishes_a_layout) {
    const World& w = founded_world();
    check::is_true(!w.settlements.sites.empty(), "cities to lay out");
    for (const SettlementSite& s : w.settlements.sites) {
        check::is_true(s.building_pack.has_value(), s.uid + " to have drawn a pack");
        check::is_true(s.city_layout.has_value(), s.uid + " to carry a layout");
        const CityLayout& l = *s.city_layout;
        check::equal(s.building_pack->version, 3, "the pack catalogue's version");
        check::equal(s.building_pack->id, std::string(building_pack_or_fallback(s.building_pack->id).id),
                     s.uid + "'s pack to be one the catalogue holds");
        check::equal(l.building_pack_id.value_or(""), s.building_pack->id, s.uid + "'s layout to name the same pack");
        check::equal(l.version, kCityLayoutVersion, "layout version 2");
        check::equal(l.population_profile, s.population_profile, "the city's own people");
        check::is_true(layout_profile(l.layout_profile_id) != nullptr, s.uid + "'s profile to be a catalogue profile");
        check::is_true(l.constraints.has_value() && l.authored_by.empty(), "a generated record");
        std::vector<std::string> names;
        for (const LayoutFeaturePlan& f : l.features) names.push_back(f.name);
        check::is_true(names == std::vector<std::string>{"leader_homes", "barracks", "noble_homes", "worker_housing",
                                                         "apartments", "market_district", "religious_building"},
                       "the seven features, in the district pass's order");
        // The pack a city's layout draws from decides its profile, when the pack names one.
        const BuildingPack& pack = building_pack_or_fallback(s.building_pack->id);
        if (!pack.layout_profile_id.empty()) {
            check::equal(l.layout_profile_id, std::string(pack.layout_profile_id), s.uid + " to follow its pack's profile");
        }
    }
}

TEST(city_layout_a_city_is_planned_from_the_population_it_was_sized_to) {
    // Founding plans every city before its people's allowance is split, then plans it again
    // once it is sized. The second plan is the one the city keeps: its districts follow the
    // population, and it is reproducible from the published seed alone.
    const World& w = founded_world();
    const auto held = sphere_grid(w.effective.size, w.effective.globe_radius);
    const SphereGrid& grid = *held;
    const std::vector<double> water = nodes_of(w, grid, "water_type");
    const std::vector<double> slope = nodes_of(w, grid, "slope");
    const std::vector<double> river = nodes_of(w, grid, "rain_river");
    const std::vector<double> to_river = river_distances(grid, river);
    int sized_differently = 0;
    for (const SettlementSite& s : w.settlements.sites) {
        const CityLayout& l = *s.city_layout;
        check::is_true(l.population_estimate.has_value(), s.uid + "'s layout to be sized");
        check::equal(*l.population_estimate, s.population_estimate, s.uid + "'s layout to carry the site's estimate");
        const LayoutProfile& profile = profile_named(l.layout_profile_id);
        for (const LayoutFeaturePlan& f : l.features) {
            const FeatureCount& spec = spec_of(profile, f.name);
            check::equal(f.target_count, resolve_feature_count(spec, s.population_estimate),
                         s.uid + " " + f.name + " sized to the city");
            if (resolve_feature_count(spec, std::nullopt) != f.target_count) ++sized_differently;
        }
        const auto u = static_cast<std::size_t>(s.node);
        const auto band = land_anchor_candidates(grid, s.node, to_river, water, slope,
                                                 profile.placement.max_anchor_slope_degrees,
                                                 profile.placement.river_buffer_m);
        const BuildingPack& pack = building_pack_or_fallback(s.building_pack->id);
        check::equal(describe(plan_city_layout(s.building_pack->seed, s.node, profile, s.population_profile,
                                               s.population_estimate, to_river[u], band, grid, river, &pack)),
                     describe(l), s.uid + "'s layout re-planned from its published seed");
    }
    check::is_true(sized_differently > 0, "the population to move some district count, or this proves nothing");
}

TEST(city_layout_a_rebuild_draws_afresh_and_carries_only_the_players_own_layout) {
    // An age turn re-founds around the survivors. What a survivor carries is its identity,
    // and a layout only where the player authored it: its pack and a generated layout are
    // drawn again. Sentinels on the old records make any carry visible.
    World w = founded_world();
    check::is_true(w.settlements.sites.size() >= 3, "three cities to work with");
    SettlementRebuild rebuild;
    rebuild.age = 1;
    rebuild.previous = w.settlements.founding;
    rebuild.survivors = w.settlements.sites;
    SettlementSite& generated = rebuild.survivors[0];
    generated.building_pack = BuildingPackDraw{"sentinel-pack", 7u, 99};
    generated.city_layout->layout_profile_id = "sentinel-profile";
    generated.city_layout->building_pack_id = "sentinel-pack";
    SettlementSite& authored = rebuild.survivors[1];
    authored.founded_by = "player";
    CityLayout own;
    own.layout_profile_id = "player";
    own.layout_profile_name = "Player-authored layout";
    own.population_profile = authored.population_profile;
    own.population_estimate = 123;
    own.authored_by = "player";
    authored.city_layout = own;
    const std::string generated_uid = generated.uid;
    const std::string authored_uid = authored.uid;
    add_settlements(w, &rebuild);

    const auto find = [&](const std::string& uid) -> const SettlementSite& {
        for (const SettlementSite& s : w.settlements.sites) {
            if (s.uid == uid) return s;
        }
        check::fail(uid + " to still stand");
    };
    const SettlementSite& g = find(generated_uid);
    check::is_true(g.building_pack.has_value() && g.building_pack->id != "sentinel-pack" && g.building_pack->version == 3,
                   "a survivor's pack is drawn again, not carried");
    check::is_true(g.city_layout.has_value() && g.city_layout->layout_profile_id != "sentinel-profile" &&
                       layout_profile(g.city_layout->layout_profile_id) != nullptr,
                   "and so is its generated layout");
    check::equal(g.city_layout->building_pack_id.value_or(""), g.building_pack->id, "the fresh layout names the fresh pack");
    check::equal(g.city_layout->population_estimate.value_or(-1), g.population_estimate, "sized at the rebuild");

    const SettlementSite& a = find(authored_uid);
    check::equal(a.founded_by, std::string("player"), "the player's city stays the player's");
    check::is_true(a.city_layout.has_value(), "with a layout");
    check::equal(a.city_layout->layout_profile_id, std::string("player"), "its own, carried");
    check::equal(a.city_layout->authored_by, std::string("player"), "authored by the player");
    check::is_true(a.city_layout->features.empty() && !a.city_layout->building_pack_id && !a.city_layout->constraints,
                   "with no district, pack or constraint the planner would have added");
    check::equal(a.city_layout->population_estimate.value_or(-1), a.population_estimate,
                 "its population following the site's, not the 123 it carried in");
    check::is_true(a.building_pack.has_value(), "while the SITE draws a pack like any city rebuilt");

    for (const SettlementSite& s : w.settlements.sites) {
        check::is_true(s.building_pack.has_value() && s.city_layout.has_value(), s.uid + " laid out after the rebuild");
    }
}

TEST(city_layout_the_survivor_carry_back_is_the_references) {
    // The reference's SurvivorCarryBackTests, against the function the rebuild calls rather
    // than against a world: whether a given town survives an age is the war model's call.
    const auto authored_old = [] {
        SettlementSite old;
        old.population_profile = "human_heartland";
        old.uid = "surface-city-0-1-human";
        old.name = "Player Town";
        old.founded_by = "player";
        CityLayout own;
        own.layout_profile_id = "player";
        own.authored_by = "player";
        old.city_layout = own;
        old.building_pack = BuildingPackDraw{"old-pack", 1u, 3};
        return old;
    };
    const auto derived = [] {
        SettlementSite site;
        CityLayout layout;
        layout.layout_profile_id = "standard_city_layout";
        site.city_layout = layout;
        site.building_pack = BuildingPackDraw{"new-pack", 2u, 3};
        return site;
    };

    // test_a_player_authored_layout_is_carried_onto_the_rebuilt_site
    SettlementSite rebuilt = derived();
    const SettlementSite old = authored_old();
    carry_survivor(rebuilt, old);
    check::equal(rebuilt.city_layout->layout_profile_id, std::string("player"), "the authored layout carried");
    check::equal(rebuilt.founded_by, std::string("player"), "the mark carried");
    check::equal(rebuilt.uid, old.uid, "the uid carried");
    check::equal(rebuilt.name, old.name, "the name carried");
    check::equal(rebuilt.building_pack->id, std::string("new-pack"), "the pack never carried");

    // test_a_generated_survivor_keeps_the_layout_the_rebuild_derived
    SettlementSite generated_old = authored_old();
    generated_old.founded_by.clear();
    rebuilt = derived();
    carry_survivor(rebuilt, generated_old);
    check::equal(rebuilt.city_layout->layout_profile_id, std::string("standard_city_layout"),
                 "a generated survivor keeps what the rebuild derived");
    check::is_true(rebuilt.founded_by.empty(), "and gains no player mark");

    // test_a_player_site_with_no_layout_yet_is_not_given_one
    SettlementSite bare_old = authored_old();
    bare_old.city_layout.reset();
    rebuilt = derived();
    carry_survivor(rebuilt, bare_old);
    check::equal(rebuilt.city_layout->layout_profile_id, std::string("standard_city_layout"),
                 "a player's site with no layout yet keeps the derived one");
    check::is_true(authored_layout(rebuilt), "and from here on the reference's test calls it authored (pinned defect)");
}

TEST(city_layout_the_document_publishes_the_layout_and_the_pack_under_the_references_keys) {
    const World& w = founded_world();
    std::ostringstream text;
    write_world_json(text, w, /*pretty=*/false, /*include_timings=*/false);
    const JsonValue doc = parse_json(text.str());
    const auto keys = [](const JsonValue& object) {
        std::set<std::string> out;
        for (const auto& [key, value] : object.as_object()) out.insert(key);
        return out;
    };
    const auto& sites = doc.at("settlements").at("sites").as_array();
    check::equal(sites.size(), w.settlements.sites.size(), "every site written");
    for (std::size_t i = 0; i < sites.size(); ++i) {
        const JsonValue& site = sites[i];
        const SettlementSite& s = w.settlements.sites[i];
        check::equal(site.at("building_pack_id").as_string(), s.building_pack->id, "building_pack_id");
        check::equal(site.at("building_pack_seed").as_int(), static_cast<std::int64_t>(s.building_pack->seed),
                     "building_pack_seed, an integer");
        check::equal(site.at("building_pack_version").as_int(), std::int64_t{3}, "building_pack_version");
        const JsonValue& layout = site.at("city_layout");
        std::set<std::string> expected{"version", "layout_profile_id", "layout_profile_name", "population_profile",
                                       "population_estimate", "river", "building_pack_id", "required_assets",
                                       "required_asset_count", "required_node_slots", "asset_anchors_missing",
                                       "features", "constraints"};
        if (layout.contains("fallback")) {
            expected.insert("fallback");
            check::is_true(keys(layout.at("fallback")) == std::set<std::string>{"reason"}, "a fallback states a reason");
        }
        check::is_true(keys(layout) == expected, "the version-2 keys and nothing else: no buildings, no residents");
        check::equal(layout.at("version").as_int(), std::int64_t{2}, "version 2");
        check::equal(layout.at("population_estimate").as_int(), static_cast<std::int64_t>(s.population_estimate),
                     "the site's estimate");
        check::is_true(keys(layout.at("river")) == std::set<std::string>{"adjacent_river_edges", "river_distance_m",
                                                                            "bridge_recommended", "bridge_threshold"},
                       "the river reading");
        check::is_true(keys(layout.at("constraints")) ==
                           std::set<std::string>{"max_anchor_slope_degrees", "river_buffer_m", "min_river_distance_m"},
                       "the constraints");
        check::is_true(keys(layout.at("features")) ==
                           std::set<std::string>{"leader_homes", "barracks", "noble_homes", "worker_housing",
                                                 "apartments", "market_district", "religious_building"},
                       "the seven features");
        for (const auto& [name, feature] : layout.at("features").as_object()) {
            check::is_true(keys(feature) == std::set<std::string>{"target_count", "count", "anchors"}, name);
            for (const JsonValue& anchor : feature.at("anchors").as_array()) {
                check::is_true(keys(anchor) == std::set<std::string>{"node", "x", "z", "distance_to_city_m"}, "an anchor");
            }
        }
        check::is_true(layout.at("required_assets").is_array() && layout.at("required_asset_count").is_integer() &&
                           layout.at("required_node_slots").is_integer() && layout.at("asset_anchors_missing").is_bool(),
                       "the asset requirement survives the removal of the placement claim");
    }
}

// --- the scale defect ------------------------------------------------------------------

TEST(city_layout_generated_anchors_stand_on_safe_land_and_the_band_never_leaves_the_city) {
    // The reference's test_generated_layouts_are_deterministic_and_use_safe_land_anchors:
    // the same request lays every city out the same way; every anchor is dry land within the
    // profile's slope and clear of the river buffer; and no node is claimed twice.
    const World& w = founded_world();
    {
        Request again;
        again.seed = 42;
        again.size = 33;
        again.phase = 10;
        const World twin = generate(resolve(again));
        check::equal(twin.settlements.sites.size(), w.settlements.sites.size(), "the same cities");
        for (std::size_t i = 0; i < w.settlements.sites.size(); ++i) {
            const SettlementSite& a = w.settlements.sites[i];
            const SettlementSite& b = twin.settlements.sites[i];
            check::equal(describe(*b.city_layout), describe(*a.city_layout), a.uid + " laid out identically");
            check::is_true(b.building_pack->id == a.building_pack->id && b.building_pack->seed == a.building_pack->seed,
                           a.uid + " drew the same pack from the same seed");
        }
    }
    const auto held = sphere_grid(w.effective.size, w.effective.globe_radius);
    const std::vector<double> water = nodes_of(w, *held, "water_type");
    const std::vector<double> slope = nodes_of(w, *held, "slope");
    const std::vector<double> to_river = river_distances(*held, nodes_of(w, *held, "rain_river"));
    for (const SettlementSite& s : w.settlements.sites) {
        const CityLayout& l = *s.city_layout;
        std::set<int> seen;
        int anchors = 0;
        for (const LayoutFeaturePlan& f : l.features) {
            check::equal(static_cast<std::size_t>(f.count), f.anchors.size(), s.uid + " " + f.name + " counts its anchors");
            for (const LayoutAnchor& a : f.anchors) {
                ++anchors;
                const auto u = static_cast<std::size_t>(a.node);
                check::is_true(seen.insert(a.node).second, s.uid + " claims a node once");
                check::equal(water[u], 0.0, s.uid + " anchors on land");
                check::is_true(slope[u] <= l.constraints->max_anchor_slope_degrees, s.uid + " anchors on gentle ground");
                if (std::isfinite(to_river[u])) {
                    check::is_true(to_river[u] >= l.constraints->min_river_distance_m, s.uid + " anchors off the river");
                }
                // KNOWN SCALE DEFECT, PINNED (docs/scale-defects.md): the band is at most 540
                // metres and a size-33 cell is kilometres across, so the only node it ever
                // holds is the city's own -- one district anchor at most, and no building asset.
                check::equal(a.node, s.node, s.uid + " anchors only on its own node");
            }
        }
        check::is_true(anchors <= 1, s.uid + " claims at most one district anchor");
        check::equal(l.required_asset_count, 0LL, s.uid + " draws no building asset");
        check::is_true(l.fallback.has_value(), s.uid + " says its anchors ran short");
    }
}
