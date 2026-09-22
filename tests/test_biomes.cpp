// Terrain label invariants, and the content table behind them.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>

#include "fwg/biomes.hpp"
#include "fwg/content.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

World build(int size = 65, double temperature_offset = 0.0) {
    Request r;
    r.seed = 42;
    r.size = size;
    r.phase = 6;
    r.temperature_offset = temperature_offset;
    return generate(resolve(r));
}

std::map<int, int> biome_histogram(const World& w) {
    std::map<int, int> out;
    const Grid& g = w.layers.at("biome");
    for (int z = 0; z < g.size(); ++z) {
        for (int x = 0; x < g.size() - 1; ++x) ++out[static_cast<int>(g.at(x, z))];
    }
    return out;
}

}  // namespace

// --- the content table ----------------------------------------------------

TEST(the_natural_biome_catalogue_is_well_formed) {
    const auto rows = natural_biomes();
    check::equal(rows.size(), std::size_t{13}, "catalogue size");

    std::set<int> ids;
    for (const NaturalBiome& b : rows) {
        check::is_true(ids.insert(b.id).second, "biome id " + std::to_string(b.id) + " to be unique");
        check::is_true(!b.core.empty() && !b.name.empty() && !b.asset_id.empty(), "every row to be named");
        for (const int c : b.color) check::between(c, 0, 255, "colour channel");
    }
    // Position is a contract, so the first and last rows are worth pinning: an insert
    // at the front would renumber every biome_variant in every world ever saved.
    check::equal(rows.front().id, 0, "catalogue to open with ocean");
    check::equal(std::string(rows.front().core), std::string("ocean"), "row 0 core");
    check::equal(rows.back().id, 17, "catalogue to close with land ice");

    check::is_true(natural_biome(0) != nullptr, "lookup by id");
    check::is_true(natural_biome(999) == nullptr, "unknown id to return nothing");
    check::equal(std::string(natural_biome(17)->name), std::string("Persistent land ice"),
                 "the one special-cased display name");
    check::is_true(std::any_of(rows.begin(), rows.end(), [](const NaturalBiome& b) { return b.reachable; }),
                   "not every biome to be tombstoned");
}

TEST(the_variant_table_covers_every_school_and_biome) {
    const auto schools = magic_schools();
    check::equal(schools.size(), std::size_t{12}, "school count");
    for (std::size_t s = 0; s < schools.size(); ++s) {
        check::is_true(!schools[s].empty(), "school to be named");
        for (std::size_t b = 0; b < natural_biomes().size(); ++b) {
            check::is_true(!variant_name(s, b).empty(),
                           "a variant name for school " + std::to_string(s) + " biome " + std::to_string(b));
        }
    }
}

// --- classification -------------------------------------------------------

TEST(classify_walks_its_branches_in_order) {
    // The order is the specification: ice before rock before cold before dry, so a
    // frozen cliff is snow rather than exposed rock.
    check::equal(classify(-1.0, 0.0, 20.0, 0.5), 0, "below the datum is ocean");
    check::equal(classify(100.0, 0.0, -5.0, 0.5), 6, "at or below freezing is snow");
    check::equal(classify(100.0, 80.0, -5.0, 0.5), 6, "a frozen cliff is snow, not rock");
    check::equal(classify(100.0, 45.0, 10.0, 0.5), 5, "too steep to hold anything is rock");
    check::equal(classify(100.0, 0.0, 2.0, 0.5), 1, "cold but not frozen is tundra");
    check::equal(classify(100.0, 0.0, 20.0, 0.1), 2, "dry is desert");
    check::equal(classify(100.0, 0.0, 25.0, 0.9), 7, "hot and very wet is rainforest");
    check::equal(classify(100.0, 0.0, 15.0, 0.9), 4, "wet but cool is forest");
    check::equal(classify(100.0, 0.0, 15.0, 0.4), 3, "the rest is grassland");

    // Every label it can produce must exist in the catalogue.
    for (const double t : {-10.0, 0.0, 3.0, 10.0, 25.0}) {
        for (const double m : {0.0, 0.4, 0.6, 0.9}) {
            for (const double s : {0.0, 50.0}) {
                check::is_true(natural_biome(classify(100.0, s, t, m)) != nullptr,
                               "classify to return a catalogued biome");
            }
        }
    }
}

TEST(landforms_split_by_height_and_relief) {
    check::equal(classify_landform(-1.0, 0.0), kSubmerged, "below the datum");
    check::equal(classify_landform(100.0, 20.0), kMountain, "high and standing proud");
    check::equal(classify_landform(20.0, 20.0), kPlain, "proud but low is not a mountain");
    check::equal(classify_landform(100.0, -20.0), kValley, "sunk below its surroundings");
    check::equal(classify_landform(100.0, 0.0), kPlain, "everything else");
}

TEST(marsh_needs_every_one_of_its_conditions) {
    // True case, then each condition broken alone — so a test cannot pass because
    // some unrelated clause happens to fail.
    check::is_true(marsh_suitable(0.6, 10.0, 2.0, 100.0, 2.0), "a marsh");
    check::is_true(!marsh_suitable(0.4, 10.0, 2.0, 100.0, 2.0), "too dry");
    check::is_true(!marsh_suitable(0.6, -1.0, 2.0, 100.0, 2.0), "frozen");
    check::is_true(!marsh_suitable(0.6, 10.0, 10.0, 100.0, 2.0), "too steep");
    check::is_true(!marsh_suitable(0.6, 10.0, 2.0, 200.0, 2.0), "too far from water");
    check::is_true(!marsh_suitable(0.6, 10.0, 2.0, -1.0, 2.0), "no water in range");
    check::is_true(!marsh_suitable(0.6, 10.0, 2.0, 100.0, 10.0), "too far above the water");
    check::is_true(!marsh_suitable(0.6, 10.0, 2.0, 100.0, -1.0), "below the water");
}

// --- the labelled world ---------------------------------------------------

TEST(every_labelled_cell_carries_a_catalogued_biome) {
    const World world = build();
    check::is_true(world.has_terrain, "a phase-6 world to be labelled");
    const auto histogram = biome_histogram(world);
    check::is_true(histogram.size() >= 5, "a world to use more than a handful of biomes");
    for (const auto& [id, count] : histogram) {
        check::is_true(natural_biome(id) != nullptr, "biome id " + std::to_string(id) + " to be catalogued");
        check::is_true(count > 0, "a counted biome to occur");
    }
}

TEST(water_overrides_the_climate_classification) {
    const World world = build();
    const Grid& biome = world.layers.at("biome");
    const Grid& type = world.layers.at("water_type");
    for (int z = 0; z < biome.size(); ++z) {
        for (int x = 0; x < biome.size() - 1; ++x) {
            const int kind = static_cast<int>(type.at(x, z));
            if (kind == 1) check::equal(biome.at(x, z), 0.0, "an ocean cell is ocean whatever its climate");
            if (kind == 2) check::equal(biome.at(x, z), 8.0, "a lake cell is lake whatever its climate");
        }
    }
}

TEST(natural_biome_starts_as_a_copy_of_biome) {
    // Later stages mutate `biome` and leave `natural_biome` as the record of what the
    // ground was before magic touched it. At this stage they must agree exactly.
    const World world = build(33);
    const Grid& a = world.layers.at("biome");
    const Grid& b = world.layers.at("natural_biome");
    for (int z = 0; z < a.size(); ++z) {
        for (int x = 0; x < a.size(); ++x) check::equal(a.at(x, z), b.at(x, z), "natural_biome to match");
    }
}

TEST(wetland_distance_is_bounded_or_absent) {
    const World world = build();
    const Grid& d = world.layers.at("water_distance");
    for (const double v : d.cells()) {
        check::is_true(v == -1.0 || (v >= 0.0 && v <= kWetlandMarginM),
                       "wetland distance to be -1 or within the margin, got " + std::to_string(v));
    }
}

TEST(features_are_balanced_and_separated) {
    const World world = build(129);
    const auto& features = world.terrain.features;
    check::is_true(!features.empty(), "a world to have feature markers");

    std::map<std::string, int> per_kind;
    for (const TerrainFeature& f : features) ++per_kind[f.kind];
    for (const auto& [kind, count] : per_kind) {
        check::between(count, 1, kFeaturesPerKind, "features of kind " + kind);
    }

    const double separation = std::cos(kFeatureSeparationRad);
    for (std::size_t i = 0; i < features.size(); ++i) {
        for (std::size_t j = i + 1; j < features.size(); ++j) {
            check::is_true(dot(features[i].direction, features[j].direction) <= separation,
                           "features " + std::to_string(i) + " and " + std::to_string(j) + " to be apart");
        }
        check::close(std::sqrt(dot(features[i].direction, features[i].direction)), 1.0, 1e-12,
                     "feature direction to be a unit vector");
    }
}

// --- the metrics move with their knobs -----------------------------------

TEST(the_biome_mix_answers_to_temperature) {
    const auto cold = biome_histogram(build(65, -25.0));
    const auto hot = biome_histogram(build(65, 25.0));
    const auto count = [](const std::map<int, int>& h, int id) {
        const auto it = h.find(id);
        return it == h.end() ? 0 : it->second;
    };
    // Snow is 6, rainforest is 7.
    check::is_true(count(cold, 6) > count(hot, 6), "a cold world to have more snow");
    check::is_true(count(hot, 7) > count(cold, 7), "a hot world to have more rainforest");
}

TEST(moisture_comes_from_climate_once_climate_exists) {
    // Before climate the wetness is a latitude band plus seeded noise; after it, the
    // transported rainfall supersedes it entirely. Confusing the two would make the
    // biome map ignore the wind, which is the whole reason climate runs first.
    const World world = build(33);
    const Grid& moisture = world.layers.at("moisture");
    const Grid& climate = world.layers.at("climate_moisture");
    for (int z = 0; z < moisture.size(); ++z) {
        for (int x = 0; x < moisture.size(); ++x) {
            check::equal(moisture.at(x, z), climate.at(x, z), "moisture to be the climate's");
        }
    }
}
