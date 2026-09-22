// Ecology invariants: archipelagos, habitats, the cold relabel, and the regions.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>

#include "fwg/detail.hpp"
#include "fwg/ecology.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

World build(int size = 65) {
    Request r;
    r.seed = 42;
    r.size = size;
    r.phase = 6;
    return generate(resolve(r));
}

double layer_max(const World& w, const char* name) {
    const Grid& g = w.layers.at(name);
    return *std::max_element(g.cells().begin(), g.cells().end());
}

}  // namespace

// --- cold habitats --------------------------------------------------------

TEST(monthly_temperatures_swing_more_toward_the_poles) {
    const auto equator = monthly_temperatures(20.0, 0.0, 0.5, 1.0);
    const auto polar = monthly_temperatures(20.0, 80.0, 0.5, 1.0);
    const auto spread = [](const std::vector<double>& t) {
        return *std::max_element(t.begin(), t.end()) - *std::min_element(t.begin(), t.end());
    };
    check::equal(equator.size(), std::size_t{12}, "twelve months");
    check::close(spread(equator), 0.0, 1e-9, "the equator barely swings");
    check::is_true(spread(polar) > 10.0, "a high latitude to swing hard");

    // Moisture damps the swing, because water holds heat.
    check::is_true(spread(monthly_temperatures(20.0, 60.0, 1.0, 1.0)) <
                       spread(monthly_temperatures(20.0, 60.0, 0.0, 1.0)),
                   "a wet place to swing less than a dry one");
    // And seasonality scales it outright.
    check::close(spread(monthly_temperatures(20.0, 60.0, 0.5, 0.0)), 0.0, 1e-9,
                 "zero seasonality to leave a flat year");

    // The annual mean is the mean whatever the amplitude.
    double total = 0.0;
    for (const double t : polar) total += t;
    check::close(total / 12.0, 20.0, 1e-9, "the year to average the mean");
}

TEST(cold_habitat_grades_by_the_warmest_month) {
    const auto year = [](double mean) { return monthly_temperatures(mean, 0.0, 0.5, 1.0); };
    check::equal(cold_habitat(year(-20.0), 1.0, 0.5), std::string("ice_cap"), "frozen and wet is ice");
    check::equal(cold_habitat(year(-20.0), 0.0, 0.5), std::string("tundra"), "frozen and dry is tundra");
    check::equal(cold_habitat(year(5.0), 0.5, 0.5), std::string("tundra"), "cool all year is tundra");
    check::equal(cold_habitat(year(30.0), 0.5, 0.5), std::string(), "a warm year is not relabelled");

    // The boreal branch needs a genuinely seasonal year: a summer above 10 C, but six
    // months or fewer above 5 C. Mean 3 at 60 degrees gives a 16.9 C summer and five
    // warm months. (Mean 6 at 55 gives SEVEN warm months and is correctly declined --
    // an earlier version of this test asserted otherwise and was simply wrong about
    // the arithmetic.)
    const auto seasonal = monthly_temperatures(3.0, 60.0, 0.5, 1.0);
    check::is_true(*std::max_element(seasonal.begin(), seasonal.end()) > 10.0, "a real summer");
    check::equal(cold_habitat(seasonal, 0.5, 0.5), std::string("boreal"), "a wet seasonal year is boreal");
    check::equal(cold_habitat(seasonal, 0.1, 0.5), std::string("tundra"), "a dry one is tundra");
}

TEST(the_cold_pass_is_what_puts_cold_ground_on_the_map) {
    // classify() emits tundra (1) and snow (6) and NEVER 15/16/17. The ecology pass
    // relabels almost all of them. If this ever reverses, the two tombstoned rows are
    // suddenly reachable again and the art commissioning note is wrong.
    const World world = build();
    std::map<int, int> histogram;
    const Grid& biome = world.layers.at("biome");
    for (int z = 0; z < biome.size(); ++z) {
        for (int x = 0; x < biome.size() - 1; ++x) ++histogram[static_cast<int>(biome.at(x, z))];
    }
    const auto count = [&](int id) {
        const auto it = histogram.find(id);
        return it == histogram.end() ? 0 : it->second;
    };
    const int cold_family = count(15) + count(16) + count(17);
    check::is_true(cold_family > 0, "a world with poles to have cold ground");
    check::is_true(cold_family > count(1) + count(6),
                   "the cold pass to have relabelled most of the raw tundra and snow");
}

// --- habitat fields -------------------------------------------------------

TEST(the_habitat_fields_are_present_and_bounded) {
    const World world = build();
    check::is_true(world.has_ecology, "a phase-6 world to have ecology");
    for (const char* name : {"metal_richness", "salinity", "coastal_exposure", "harbor_suitability",
                             "fishing_productivity", "reef", "lagoon", "estuary", "sheltered_bay",
                             "rocky_coast", "kelp", "fjord", "open_ocean", "maritime", "boreal",
                             "tundra", "ice_cap", "coastal_support", "island_habitat"}) {
        const auto it = world.layers.find(name);
        check::is_true(it != world.layers.end(), std::string("layer ") + name + " to exist");
        for (const double v : it->second.cells()) {
            check::between(v, 0.0, 1.0, std::string(name) + " is a 0..1 proxy");
        }
    }
}

TEST(marine_habitats_only_occur_in_water_and_harbours_only_on_land) {
    const World world = build();
    const Grid& type = world.layers.at("water_type");
    const Grid& reef = world.layers.at("reef");
    const Grid& kelp = world.layers.at("kelp");
    const Grid& harbor = world.layers.at("harbor_suitability");
    for (int z = 0; z < type.size(); ++z) {
        for (int x = 0; x < type.size() - 1; ++x) {
            const int kind = static_cast<int>(type.at(x, z));
            if (kind != 1) {
                check::equal(reef.at(x, z), 0.0, "no reef outside the ocean");
                check::equal(kelp.at(x, z), 0.0, "no kelp outside the ocean");
            }
            if (kind != 0) check::equal(harbor.at(x, z), 0.0, "no harbour on water");
        }
    }
    check::is_true(layer_max(world, "reef") > 0.0, "a warm shallow world to grow some reef");
    check::is_true(layer_max(world, "harbor_suitability") > 0.0, "a coastal world to have a harbour");
}

TEST(island_habitat_is_relative_to_this_worlds_land) {
    // The reference used an ABSOLUTE 3 km2 floor authored for a world eighteen times
    // narrower. At the default width no component of any seed could reach it, so the
    // layer was a published constant of zero. The fraction arm is what restores it.
    const World world = build(129);
    const Grid& island = world.layers.at("island_habitat");
    int flagged = 0;
    for (const double v : island.cells()) {
        check::is_true(v == 0.0 || v == 1.0, "island habitat is a flag");
        if (v == 1.0) ++flagged;
    }
    check::is_true(flagged > 0, "a world with islands to flag some of them");
    check::is_true(flagged < island.size() * island.size(), "not every cell to be an island");
}

// --- archipelagos ---------------------------------------------------------

TEST(island_layout_is_seeded_and_bounded) {
    Request r;
    r.seed = 42;
    r.size = 33;
    r.phase = 2;
    const Config cfg = resolve(r);

    const auto a = island_layout(cfg);
    const auto b = island_layout(cfg);
    check::equal(a.size(), b.size(), "the layout to replay from its seed");

    const int requested = cfg.options.as_int(Option::archipelago_count);
    check::is_true(static_cast<int>(a.size()) <= requested,
                   "no more clusters than were asked for (occurrence can refuse some)");
    for (const Archipelago& cluster : a) {
        check::close(std::sqrt(dot(cluster.direction, cluster.direction)), 1.0, 1e-12,
                     "a cluster centre to be a unit vector");
        check::equal(static_cast<int>(cluster.islands.size()),
                     cfg.options.as_int(Option::islands_per_cluster), "islands per cluster");
        for (const Island& island : cluster.islands) {
            check::close(std::sqrt(dot(island.direction, island.direction)), 1.0, 1e-12,
                         "an island to be a unit vector");
            check::is_true(island.angular_radius > 0.0, "an island to have a radius");
        }
        check::is_true(cluster.kind == "volcanic" || cluster.kind == "atoll" ||
                           cluster.kind == "continental" || cluster.kind == "cold",
                       "a known cluster kind");
    }
}

TEST(archipelago_count_answers_to_its_option) {
    const auto layout_for = [](int count) {
        Request r;
        r.seed = 42;
        r.size = 33;
        r.phase = 2;
        r.options["archipelago_count"] = count;
        r.options["archipelago_occurrence"] = 1.0;  // take the count out of the roll's hands
        return island_layout(resolve(r)).size();
    };
    check::equal(layout_for(0), std::size_t{0}, "no clusters requested, none laid out");
    check::equal(layout_for(3), std::size_t{3}, "three requested, three laid out");
    check::equal(layout_for(9), std::size_t{9}, "nine requested, nine laid out");
}

TEST(an_unknown_or_out_of_range_option_is_refused) {
    Request r;
    r.seed = 42;
    r.size = 33;
    r.phase = 2;
    r.options["not_a_real_option"] = 1.0;
    check::throws([&] { resolve(r); }, "an unknown option");

    Request bad;
    bad.seed = 42;
    bad.size = 33;
    bad.phase = 2;
    bad.options["island_radius"] = 99.0;  // far above its published maximum
    check::throws([&] { resolve(bad); }, "an out-of-range option");
}

// --- the continuous height field -----------------------------------------

TEST(the_height_field_agrees_with_the_grid_it_came_from) {
    const World world = build(65);
    check::is_true(world.has_terrain_detail, "a phase-6 world to define its detail field");
    const HeightField field(world);
    const Grid& height = world.layers.at("height");
    const int n = height.size();

    // At a grid point with detail fully suppressed the field must return the grid
    // value exactly; elsewhere it must stay within the bands' total amplitude.
    double total_amplitude = 0.0;
    for (const DetailBand& b : field.detail().bands) total_amplitude += b.amplitude_m;

    for (int z = 1; z < n - 1; z += 3) {
        for (int x = 0; x < n - 1; x += 3) {
            const double sampled = field.height(direction(x, z, n));
            check::is_true(std::abs(sampled - height.at(x, z)) <= total_amplitude + 1e-6,
                           "the field to stay within its own amplitude of the grid");
        }
    }
}

TEST(the_height_field_leaves_water_alone) {
    const World world = build(65);
    const HeightField field(world);
    const Grid& height = world.layers.at("height");
    const Grid& type = world.layers.at("water_type");
    const int n = height.size();
    int checked = 0;
    for (int z = 1; z < n - 1; ++z) {
        for (int x = 0; x < n - 1; ++x) {
            if (type.at(x, z) == 0.0) continue;
            check::close(field.height(direction(x, z, n)), height.at(x, z), 1e-9,
                         "water to take no detail");
            ++checked;
        }
    }
    check::is_true(checked > 0, "a world to have water to check");
}

TEST(level_of_detail_never_changes_the_height) {
    // The property that makes this usable as an engine terrain source: the same
    // direction sampled from two different tile levels is the same metre, so there is
    // no LOD blending and no seam to hide.
    const World world = build(33);
    const HeightTile coarse = export_height_tile(world, 3, 0, 0, 4);
    const HeightTile fine = export_height_tile(world, 4, 0, 0, 8);
    check::equal(coarse.size, 5, "a 4-cell tile has 5 samples a side");
    check::equal(fine.size, 9, "an 8-cell tile has 9");

    const HeightField field(world);
    // Level 3 index (i, j) and level 4 index (2i, 2j) are the same point on the globe.
    for (int j = 0; j <= 4; ++j) {
        for (int i = 0; i <= 4; ++i) {
            check::close(coarse.heights_m[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)],
                         fine.heights_m[static_cast<std::size_t>(2 * j)][static_cast<std::size_t>(2 * i)],
                         1e-9, "the same direction to give the same height at two levels");
        }
    }
    (void)field;
}

TEST(tiles_of_one_world_share_a_source_digest) {
    const World world = build(33);
    const HeightTile a = export_height_tile(world, 3, 0, 0, 4);
    const HeightTile b = export_height_tile(world, 3, 4, 0, 4);
    check::equal(a.source_sha256, b.source_sha256,
                 "two windows on one world to carry the same source hash");
    check::equal(a.source_sha256.size(), std::size_t{64}, "a sha256 digest");

    Request other;
    other.seed = 43;
    other.size = 33;
    other.phase = 6;
    const HeightTile elsewhere = export_height_tile(generate(resolve(other)), 3, 0, 0, 4);
    check::is_true(elsewhere.source_sha256 != a.source_sha256, "a different world to hash differently");
}

TEST(a_tile_outside_the_grid_is_refused) {
    const World world = build(33);
    check::throws([&] { export_height_tile(world, 0, 0, 0, 4); }, "level below one");
    check::throws([&] { export_height_tile(world, 25, 0, 0, 4); }, "level above 24");
    check::throws([&] { export_height_tile(world, 3, 0, 0, 0); }, "zero cells");
    check::throws([&] { export_height_tile(world, 3, 0, 0, 300); }, "too many cells");
    check::throws([&] { export_height_tile(world, 3, -1, 0, 4); }, "a negative origin");
    check::throws([&] { export_height_tile(world, 3, 0, 8, 4); }, "a tile running off the pole");
}

TEST(the_archipelago_uplift_reaches_the_height_field) {
    // shape_islands raises the islands into the PUBLISHED structure layer, and stage 3
    // then rebuilds height as structure + noise. Reading a copy of structure taken
    // before the uplift discarded every island at phase 3 and above -- which is every
    // world built at the default phase -- while the published structure, base and
    // archipelago_relief layers all went on describing them.
    //
    // Two assertions, and the second is what stops the first being vacuous: on a world
    // with no eligible clusters, `height == structure + noise` holds trivially.
    Request r;
    r.seed = 42;
    r.size = 65;
    r.phase = 3;
    const World world = generate(resolve(r));

    const auto& height = world.layers.at("height");
    const auto& structure = world.layers.at("structure");
    const auto& noise = world.layers.at("noise");
    const auto& relief = world.layers.at("archipelago_relief");

    long long raised = 0;
    for (int z = 0; z < world.config.size; ++z) {
        for (int x = 0; x < world.config.size; ++x) {
            if (relief.at(x, z) != 0.0) ++raised;
            check::close(height.at(x, z), structure.at(x, z) + noise.at(x, z), 1e-9,
                         "height at (" + std::to_string(x) + "," + std::to_string(z) +
                             ") to be the RAISED structure plus noise");
        }
    }
    check::is_true(raised > 0,
                   "this world to raise some archipelago, or the assertion above holds "
                   "trivially and proves nothing");
}
