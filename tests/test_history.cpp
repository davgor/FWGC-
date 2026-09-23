// Stage 6 and 7 invariants: the second epoch, and the scars it left.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include <set>

#include "fwg/content.hpp"
#include "fwg/history.hpp"
#include "fwg/magic.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

World build(int size = 65, int phase = 7) {
    Request r;
    r.seed = 42;
    r.size = size;
    r.phase = phase;
    return generate(resolve(r));
}

int count_nonzero(const World& w, const char* layer) {
    const Grid& g = w.layers.at(layer);
    int n = 0;
    for (int z = 0; z < g.size(); ++z) {
        for (int x = 0; x < g.size() - 1; ++x) {
            if (g.at(x, z) != 0.0) ++n;
        }
    }
    return n;
}

}  // namespace

TEST(the_epoch_moves_every_plate_along_its_own_velocity) {
    const World world = build(33, 6);
    check::is_true(world.has_epoch, "a phase-6 world to have a past");
    const GeologicalEpoch& e = world.epoch;
    check::equal(e.plates_before.size(), e.plates_after.size(), "no plate created or destroyed");
    check::is_true(!e.plates_before.empty(), "plates to move");

    double smallest = 10.0;
    double largest = -1.0;
    for (std::size_t i = 0; i < e.plates_before.size(); ++i) {
        const Vec3& a = e.plates_before[i].center;
        const Vec3& b = e.plates_after[i].center;
        check::close(std::sqrt(dot(b, b)), 1.0, 1e-9, "a moved centre still on the sphere");
        check::equal(e.plates_before[i].id, e.plates_after[i].id, "a plate to keep its identity");
        const double moved = std::acos(clamp_unit(dot(a, b)));
        smallest = std::min(smallest, moved);
        largest = std::max(largest, moved);
    }
    check::is_true(largest > 0.0, "the plates to have moved at all");
    // Each plate carries its own angular velocity, so they must NOT all move together:
    // if they did, the boundary pattern would be unchanged and the epoch pointless.
    check::is_true(largest > smallest * 1.5,
                   "plates to move by different amounts (" + std::to_string(smallest) + " .. " +
                       std::to_string(largest) + " rad)");
    // A rotation by |omega| * duration cannot exceed the largest speed times the span.
    check::is_true(largest <= kEpochDuration * 1.0 + 1e-9, "no plate to outrun its own speed");
}

TEST(the_epoch_rewrites_the_surface_and_everything_measured_from_it) {
    const World before = build(65, 5);
    const World after = build(65, 6);

    const Grid& old_height = after.layers.at("pre_tectonic_height");
    const Grid& new_height = after.layers.at("height");
    int moved = 0;
    for (int z = 0; z < old_height.size(); ++z) {
        for (int x = 0; x < old_height.size() - 1; ++x) {
            if (std::abs(old_height.at(x, z) - new_height.at(x, z)) > 1e-9) ++moved;
        }
    }
    check::is_true(moved > old_height.size() * old_height.size() / 4,
                   "the epoch to move most of the surface (" + std::to_string(moved) + " cells)");

    // The snapshot must be the pre-epoch surface, not a copy of the new one.
    const Grid& stage5 = before.layers.at("height");
    for (int z = 0; z < stage5.size(); ++z) {
        for (int x = 0; x < stage5.size(); ++x) {
            check::close(old_height.at(x, z), stage5.at(x, z), 1e-9,
                         "pre_tectonic_height to be the stage-5 surface");
        }
    }

    // Slope, TPI, land and area are all derived from height, so they must have been
    // re-measured rather than left describing a world that no longer exists.
    const Grid& land = after.layers.at("land");
    const Grid& height = after.layers.at("height");
    for (int z = 0; z < land.size(); ++z) {
        for (int x = 0; x < land.size(); ++x) {
            const bool above = height.at(x, z) > after.effective.sea_level;
            check::equal(land.at(x, z), above ? 1.0 : 0.0, "the land mask to match the new height");
        }
    }
    check::is_true(after.area.land_fraction > 0.0 && after.area.land_fraction < 1.0,
                   "a re-measured area");
    check::is_true(after.has_water, "water to have re-routed over the new ground");
}

TEST(second_uplift_is_exactly_what_the_epoch_changed) {
    const World world = build(33, 6);
    const Grid& uplift = world.layers.at("second_uplift");
    const Grid& before = world.layers.at("pre_tectonic_height");
    // `height` has since been re-routed by water but not re-cut, so the epoch's own
    // delta must still reconcile the two surfaces it was computed between.
    const Grid& after = world.layers.at("height");
    for (int z = 0; z < uplift.size(); ++z) {
        for (int x = 0; x < uplift.size(); ++x) {
            check::close(uplift.at(x, z), after.at(x, z) - before.at(x, z), 1e-9,
                         "second_uplift to be the difference it claims");
        }
    }
}

// --- the relics -----------------------------------------------------------

TEST(relics_only_mark_ground_the_water_actually_left) {
    const World world = build(65, 7);
    const Grid& relic_gorge = world.layers.at("relic_gorge");
    const Grid& relic_valley = world.layers.at("relic_valley");
    const Grid& old_river = world.layers.at("old_river");
    const Grid& old_type = world.layers.at("old_water_type");

    for (int z = 0; z < relic_gorge.size(); ++z) {
        for (int x = 0; x < relic_gorge.size() - 1; ++x) {
            if (relic_gorge.at(x, z) != 0.0) {
                check::is_true(old_river.at(x, z) != 0.0, "a gorge to have carried a river");
            }
            if (relic_valley.at(x, z) != 0.0) {
                check::is_true(old_type.at(x, z) != 0.0, "a dry valley to have held water");
            }
        }
    }
    check::is_true(count_nonzero(world, "relic_valley") > 0, "an epoch to strand some water");
}

TEST(a_cut_bed_that_refills_stops_being_a_gorge) {
    // Cutting a bed can let water back into it. The distinction is the point of having
    // both pairs of layers: relic_* is what the epoch stranded, gorge/dry_valley is
    // what is STILL dry after the incision re-routed the water.
    const World world = build(65, 7);
    const int relic_g = count_nonzero(world, "relic_gorge");
    const int relic_v = count_nonzero(world, "relic_valley");
    const int now_g = count_nonzero(world, "gorge");
    const int now_v = count_nonzero(world, "dry_valley");

    check::is_true(now_g <= relic_g, "no gorge to appear from nowhere");
    check::is_true(now_v <= relic_v, "no dry valley to appear from nowhere");
    check::is_true(relic_v > 0, "some stranded basins to begin with");

    // And every current gorge must still be dry.
    const Grid& gorge = world.layers.at("gorge");
    const Grid& type = world.layers.at("water_type");
    for (int z = 0; z < gorge.size(); ++z) {
        for (int x = 0; x < gorge.size() - 1; ++x) {
            if (gorge.at(x, z) != 0.0) check::equal(type.at(x, z), 0.0, "a current gorge to be dry");
        }
    }
}

TEST(the_incision_only_ever_cuts_down_and_within_its_bounds) {
    const World world = build(65, 7);
    const Grid& incision = world.layers.at("relic_incision");
    for (const double v : incision.cells()) {
        check::is_true(v >= 0.0, "an incision never raises ground");
        check::is_true(v <= 100.0 + 1e-9, "an incision within its published ceiling");
    }
    check::is_true(count_nonzero(world, "relic_incision") > 0, "some ground to be cut");

    // Shoulders: a cut cell's neighbours may take up to a quarter of its depth, so the
    // number of touched cells exceeds the number of relic beds.
    const int cut = count_nonzero(world, "relic_incision");
    const int beds = count_nonzero(world, "relic_gorge") + count_nonzero(world, "relic_valley");
    check::is_true(cut >= beds, "shoulders to widen the cut beyond the beds themselves");
}

TEST(stage_seven_is_optional_and_stage_six_stands_alone) {
    const World six = build(33, 6);
    check::equal(six.layers.count("relic_incision"), std::size_t{0}, "no incision before stage 7");
    check::equal(six.layers.count("gorge"), std::size_t{0}, "no gorge before stage 7");
    check::is_true(six.has_epoch, "but the epoch has run");

    const World seven = build(33, 7);
    check::equal(seven.layers.count("relic_incision"), std::size_t{1}, "stage 7 cuts");
    check::equal(seven.layers.count("gorge"), std::size_t{1}, "and names what stayed dry");
}

TEST(the_ladder_runs_contiguously_to_nine) {
    // Stages 6 and 7 were the hole below stage 8. With them in, completed_stage is a
    // real high-water mark again rather than a floor with work stacked above it.
    const World world = build(33, 9);
    check::equal(world.completed_stage, 9, "a contiguous ladder to nine");
    for (const char* system : {"tectonics", "erosion", "water", "tectonic_transition", "relic_carving",
                               "climate", "terrain_labels", "leylines", "astrology", "ecology"}) {
        const bool ran = std::find(world.systems.begin(), world.systems.end(), std::string(system)) !=
                         world.systems.end();
        check::is_true(ran, std::string(system) + " to have run");
    }
    // And they run in dependency order: the epoch reshapes the ground before climate
    // reads it, not after.
    const auto position = [&](const char* name) {
        return std::find(world.systems.begin(), world.systems.end(), std::string(name)) -
               world.systems.begin();
    };
    check::is_true(position("tectonic_transition") < position("climate"), "the epoch before climate");
    check::is_true(position("relic_carving") < position("climate"), "the relics before climate");
    check::is_true(position("leylines") < position("ecology"), "leylines before the regions read them");
    check::is_true(position("astrology") < position("ecology"), "the sky before ecology");
}


// --- stage 10: the magical state of the ground ---------------------------

TEST(the_variant_catalogue_is_the_full_cross_product_in_contract_order) {
    const auto variants = biome_variant_catalogue();
    const auto biomes = natural_biomes();
    const auto schools = magic_school_table();
    check::equal(variants.size(), biomes.size() * schools.size(), "every core crossed with every school");

    // The known block comes first and the hidden block is appended, so a generated
    // world's indices all fall below the hidden boundary. A single nested loop would
    // interleave them and renumber every variant in every saved world.
    const std::size_t known = std::count_if(schools.begin(), schools.end(),
                                            [](const MagicSchool& s) { return !s.hidden; });
    const std::size_t boundary = biomes.size() * known;
    for (std::size_t i = 0; i < variants.size(); ++i) {
        check::equal(variants[i].hidden, i >= boundary,
                     "variant " + std::to_string(i) + " on the right side of the boundary");
    }

    std::set<std::string_view> ids;
    for (const BiomeVariant& v : variants) {
        check::is_true(ids.insert(v.id).second, std::string(v.id) + " to be unique");
        check::is_true(natural_biome(v.core_biome_id) != nullptr, "a real core biome");
        check::is_true(!v.name.empty() && !v.asset_id.empty(), std::string(v.id) + " to be named");
        check::equal(biome_variant_index(v.core, v.magic_school), static_cast<int>(&v - variants.data()),
                     "lookup to agree with position");
    }
    check::equal(biome_variant_index("nowhere", "weave"), -1, "an unknown core");
    check::equal(biome_variant_index("ocean", "not_a_school"), -1, "an unknown school");
}

TEST(biome_variant_is_an_index_and_never_names_hidden_ground) {
    const World world = build(65, 9);
    const Grid& variant = world.layers.at("biome_variant");
    const auto variants = biome_variant_catalogue();

    int mutated = 0;
    int unowned = 0;
    for (int z = 0; z < variant.size(); ++z) {
        for (int x = 0; x < variant.size() - 1; ++x) {
            const int index = static_cast<int>(variant.at(x, z));
            if (index < 0) {
                check::equal(index, -1, "unowned ground is exactly -1");
                ++unowned;
                continue;
            }
            check::is_true(index < static_cast<int>(variants.size()), "an index into the catalogue");
            // Generation can never raise a hidden school, so it can never mark ground
            // as held by one. If this fails, either the occurrence lock has slipped or
            // the catalogue's two blocks have been interleaved.
            check::is_true(!variants[static_cast<std::size_t>(index)].hidden,
                           "generation never produces hidden ground");
            ++mutated;
        }
    }
    check::is_true(mutated > 0, "a magical world to have some mutated ground");
    check::is_true(unowned > mutated, "most ground to belong to nobody, as dominance requires");
}

TEST(a_variant_names_the_core_the_cell_actually_has) {
    const World world = build(33, 9);
    const Grid& variant = world.layers.at("biome_variant");
    const Grid& natural = world.layers.at("natural_biome");
    const Grid& biome = world.layers.at("biome");
    const auto variants = biome_variant_catalogue();

    for (int z = 0; z < variant.size(); ++z) {
        for (int x = 0; x < variant.size() - 1; ++x) {
            // biome follows the natural ground; the variant layer carries the magic.
            check::equal(biome.at(x, z), natural.at(x, z), "biome to follow natural_biome");
            const int index = static_cast<int>(variant.at(x, z));
            if (index < 0) continue;
            check::equal(variants[static_cast<std::size_t>(index)].core_biome_id,
                         static_cast<int>(natural.at(x, z)),
                         "a variant to name the core its cell actually has");
        }
    }
}

TEST(no_magic_means_no_variants) {
    Request r;
    r.seed = 42;
    r.size = 33;
    r.phase = 9;
    r.magic_enabled = 0;
    const World world = generate(resolve(r));
    for (const double v : world.layers.at("biome_variant").cells()) {
        check::equal(v, -1.0, "unmagical ground belongs to no school");
    }
}
