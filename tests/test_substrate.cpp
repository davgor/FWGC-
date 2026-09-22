// The layer everything else is built on: hashing, the random stream, the noise
// field and the sphere grid. A defect here is invisible until it is a world.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <set>

#include "fwg/grid.hpp"
#include "fwg/noise.hpp"
#include "fwg/random.hpp"
#include "fwg/scale.hpp"
#include "fwg/json.hpp"
#include "fwg/sha256.hpp"

#include <limits>

using namespace fwg;

namespace {
constexpr double kPi = std::numbers::pi;
}

// --- SHA-256 -------------------------------------------------------------
// Pinned values are correct here and nowhere else in this suite: these are the
// published FIPS vectors, not this generator's output.

TEST(sha256_matches_the_published_vectors) {
    check::equal(sha256_hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"),
                 "empty string digest");
    check::equal(sha256_hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
                 "abc digest");
    check::equal(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
                 std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"),
                 "two-block digest");
}

TEST(sha256_spans_the_block_boundary) {
    // 55, 56, 63, 64 and 65 bytes exercise every padding branch. A length-field
    // mistake shows up at exactly 56 and nowhere else.
    for (const std::size_t length : {std::size_t{55}, std::size_t{56}, std::size_t{63}, std::size_t{64}, std::size_t{65}}) {
        const std::string data(length, 'a');
        check::equal(sha256_hex(data).size(), std::size_t{64}, "digest length at " + std::to_string(length));
    }
    // Known vector for one million 'a' would be slow; 64 'a' is enough to pin the
    // exact-block-multiple branch.
    check::equal(sha256_hex(std::string(64, 'a')),
                 std::string("ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"),
                 "64 byte digest");
}

TEST(child_seeds_separate_by_domain_and_variation) {
    const std::uint32_t plates = child_seed(42, "plates", 0);
    const std::uint32_t crust = child_seed(42, "crust", 0);
    const std::uint32_t surface = child_seed(42, "surface", 0);
    check::is_true(plates != crust && crust != surface && plates != surface, "three domains to differ");
    check::is_true(child_seed(42, "plates", 1) != plates, "a variation to move the seed");
    check::is_true(child_seed(43, "plates", 0) != plates, "a different master to move the seed");
    // Same inputs, same seed -- the property the whole generator rests on.
    check::equal(child_seed(42, "plates", 0), plates, "child_seed to be a pure function");
}

// --- the random stream ---------------------------------------------------

TEST(rng_replays_from_its_seed) {
    Rng a(12345);
    Rng b(12345);
    for (int i = 0; i < 1000; ++i) check::equal(a.next_u64(), b.next_u64(), "replayed word " + std::to_string(i));
}

TEST(rng_adjacent_seeds_are_not_correlated) {
    // A raw LCG-style seeding would make these two streams shadow each other. The
    // SplitMix64 expansion exists to stop that, so it is worth asserting.
    Rng a(1);
    Rng b(2);
    int agreements = 0;
    for (int i = 0; i < 512; ++i) {
        if ((a.next_u64() >> 60) == (b.next_u64() >> 60)) ++agreements;
    }
    // ~32 expected out of 512 at one in sixteen; anything near 512 means the streams
    // are the same sequence offset by a step.
    check::is_true(agreements < 100, "adjacent seeds to diverge (agreements=" + std::to_string(agreements) + ")");
}

TEST(rng_random_stays_in_the_unit_interval) {
    Rng rng(7);
    double low = 1.0;
    double high = 0.0;
    double total = 0.0;
    const int draws = 200000;
    for (int i = 0; i < draws; ++i) {
        const double v = rng.random();
        check::is_true(v >= 0.0 && v < 1.0, "random() in [0,1)");
        low = std::min(low, v);
        high = std::max(high, v);
        total += v;
    }
    check::close(total / draws, 0.5, 0.01, "mean of random()");
    check::is_true(low < 0.01 && high > 0.99, "random() to reach both ends");
}

TEST(rng_below_is_in_range_and_covers_it) {
    Rng rng(99);
    std::map<std::uint64_t, int> counts;
    for (int i = 0; i < 60000; ++i) {
        const std::uint64_t v = rng.below(6);
        check::is_true(v < 6, "below(6) < 6");
        ++counts[v];
    }
    check::equal(counts.size(), std::size_t{6}, "every value of below(6) to occur");
    for (const auto& [value, count] : counts) {
        // 10000 expected; a modulo bias on a 64-bit draw would be far too small to
        // see here, so this is a coverage check, not a bias check.
        check::between(count, 9000, 11000, "count for " + std::to_string(value));
    }
    check::equal(rng.below(1), std::uint64_t{0}, "below(1)");
    check::throws([&] { rng.below(0); }, "below(0)");
}

TEST(rng_gauss_has_the_requested_shape) {
    Rng rng(2024);
    const int draws = 200000;
    double sum = 0.0;
    double sum_sq = 0.0;
    for (int i = 0; i < draws; ++i) {
        const double v = rng.gauss(3.0, 2.0);
        sum += v;
        sum_sq += v * v;
    }
    const double mean = sum / draws;
    const double variance = sum_sq / draws - mean * mean;
    check::close(mean, 3.0, 0.05, "gauss mean");
    check::close(std::sqrt(variance), 2.0, 0.05, "gauss stddev");
}

TEST(rng_gauss_does_not_cache_a_spare) {
    // This replaced a test that compared two Rngs which had done the SAME thing and
    // asserted they agreed. That holds for any implementation, cached spare or not, so
    // it could not fail and pinned nothing -- the exact defect this suite's header
    // warns about, found in the suite itself by an adversarial review.
    //
    // The distinguishing property: with a cached spare the SECOND gauss call consumes
    // zero words, so the stream position after two calls equals the position after one.
    // Without a cache the second call pays again.
    Rng one(5);
    one.gauss(0.0, 1.0);
    Rng two(5);
    two.gauss(0.0, 1.0);
    two.gauss(0.0, 1.0);
    check::is_true(two.draws() > one.draws(),
                   "a second gauss to cost stream (one=" + std::to_string(one.draws()) +
                       " two=" + std::to_string(two.draws()) + ")");
    check::is_true(one.draws() >= 2, "polar method to cost at least two words per deviate");
}

TEST(rng_below_costs_the_same_stream_at_every_bound) {
    // The defect this library exists to avoid, found in its own code: a shortcut like
    // `if (bound == 1) return 0` is free and correct in isolation, and makes the words
    // a call consumes depend on the VALUE of its argument. A caller whose bound reaches
    // 1 on some seeds then shifts every later draw on exactly those seeds and no others.
    // That shortcut was in Rng::below until a review found it.
    const std::uint64_t bounds[] = {1u, 2u, 7u, std::uint64_t{1} << 40,
                                    std::numeric_limits<std::uint64_t>::max()};
    for (const std::uint64_t bound : bounds) {
        Rng rng(1234);
        const std::uint64_t before = rng.draws();
        const std::uint64_t value = rng.below(bound);
        check::is_true(value < bound, "below in range");
        check::equal(rng.draws() - before, std::uint64_t{1}, "one word at bound " + std::to_string(bound));
    }
}

TEST(rng_sample_costs_one_draw_per_pick_at_every_population) {
    // The C++ analogue of CPython's random.sample switching algorithm on population
    // size: if this implementation ever grows a small-population branch, the cost per
    // pick changes and the choice of algorithm becomes observable in the world.
    const std::size_t populations[] = {2u, 5u, 64u, 5000u, 200000u};
    for (const std::size_t population : populations) {
        Rng rng(77);
        const std::uint64_t before = rng.draws();
        rng.sample_indices(population, 2);
        check::equal(rng.draws() - before, std::uint64_t{2},
                     "two words for two picks at population " + std::to_string(population));
    }
}

TEST(rng_stream_is_pinned_by_golden_values) {
    // The only place in this suite besides the FIPS vectors where a pinned number is
    // right. Every other test here asserts a PROPERTY of the stream -- and a changed
    // rotl constant, a changed SplitMix64 multiplier or a changed 53-bit scaling
    // satisfies every one of those properties while producing a different planet from
    // every seed. Nothing else in the repository would notice.
    //
    // These are generated by the engine, so they cannot prove it "correct"; what they
    // pin is that it has not CHANGED. Regenerate only with a deliberate stream version
    // bump, and understand that doing so invalidates every world ever made.
    // Cross-checked against an independent reimplementation of xoshiro256++ and
    // SplitMix64 written in Python, which reproduced all eight words exactly. So these
    // are not merely this engine's own output written down: two implementations agree.
    Rng words(42);
    const std::uint64_t golden[8] = {
        15021278609987233951ull, 5881210131331364753ull, 18149643915985481100ull,
        12933668939759105464ull, 14637574242682825331ull, 10848501901068131965ull,
        2312344417745909078ull,  11162538943635311430ull};
    for (int i = 0; i < 8; ++i) {
        check::equal(words.next_u64(), golden[i], "golden word " + std::to_string(i));
    }

    // One value per distribution, so a change to the scaling, the rejection bound or
    // the polar transform cannot pass by leaving the raw words alone.
    Rng r(42);
    check::equal(format_double(r.random()), std::string("0.8143051451229099"), "first random()");
    Rng g(42);
    check::equal(format_double(g.gauss(0.0, 1.0)), std::string("0.9813983900724986"), "first gauss()");
    Rng b(42);
    check::equal(b.below(1000), std::uint64_t{951}, "first below(1000)");
}

TEST(rng_sample_indices_are_distinct_and_in_range) {
    Rng rng(31337);
    for (const std::size_t population : {std::size_t{5}, std::size_t{50}, std::size_t{5000}}) {
        const std::size_t k = population / 2;
        const auto picked = rng.sample_indices(population, k);
        check::equal(picked.size(), k, "sample size");
        const std::set<std::size_t> unique(picked.begin(), picked.end());
        check::equal(unique.size(), k, "sample to be distinct at population " + std::to_string(population));
        for (const std::size_t index : picked) {
            check::is_true(index < population, "sampled index in range");
        }
    }
    check::equal(rng.sample_indices(10, 0).size(), std::size_t{0}, "empty sample");
    check::equal(rng.sample_indices(10, 10).size(), std::size_t{10}, "full sample");
    check::throws([&] { rng.sample_indices(3, 4); }, "oversized sample");
}

TEST(rng_shuffle_is_a_permutation) {
    Rng rng(808);
    std::vector<int> items(200);
    for (int i = 0; i < 200; ++i) items[static_cast<std::size_t>(i)] = i;
    const std::vector<int> before = items;
    rng.shuffle(items);
    check::is_true(items != before, "shuffle to move something");
    std::sort(items.begin(), items.end());
    check::is_true(items == before, "shuffle to preserve the multiset");
}

// --- noise ---------------------------------------------------------------

TEST(perlin3_is_a_pure_function) {
    check::equal(perlin3(1.25, -3.5, 7.75, 42), perlin3(1.25, -3.5, 7.75, 42), "repeated call");
    check::is_true(perlin3(1.25, -3.5, 7.75, 42) != perlin3(1.25, -3.5, 7.75, 43), "seed to matter");
}

TEST(perlin3_vanishes_on_the_lattice) {
    // Gradient noise is zero at integer lattice points by construction. If this ever
    // fails, the corner offsets or the blend weights are wrong -- and the terrain
    // would still look plausible, which is why it is worth an explicit test.
    for (int x = -3; x <= 3; ++x) {
        for (int y = -3; y <= 3; ++y) {
            for (int z = -3; z <= 3; ++z) {
                check::close(perlin3(x, y, z, 11), 0.0, 1e-12,
                             "lattice value at " + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z));
            }
        }
    }
}

TEST(perlin3_stays_bounded_and_uses_its_range) {
    Rng rng(4);
    double low = 1.0;
    double high = -1.0;
    for (int i = 0; i < 100000; ++i) {
        const double v = perlin3(rng.uniform(-50, 50), rng.uniform(-50, 50), rng.uniform(-50, 50), 77);
        check::between(v, -1.0, 1.0, "perlin3 range");
        low = std::min(low, v);
        high = std::max(high, v);
    }
    check::is_true(low < -0.4 && high > 0.4, "perlin3 to use its range (low=" + std::to_string(low) +
                                                 " high=" + std::to_string(high) + ")");
}

TEST(perlin3_is_continuous_across_a_cell_boundary) {
    // Approaching an integer plane from both sides must agree, or the terrain has a
    // grid-aligned crack through it.
    const double eps = 1e-7;
    for (const double at : {-2.0, 0.0, 3.0}) {
        const double before = perlin3(at - eps, 0.37, 0.61, 5);
        const double after = perlin3(at + eps, 0.37, 0.61, 5);
        check::close(before, after, 1e-5, "continuity across x=" + std::to_string(at));
    }
}

TEST(resolved_frequencies_drop_sub_nyquist_octaves) {
    // wavelength 100, step 10: octaves at 100, 50, 25, 12.5, 6.25 -- the first three
    // clear 2*step=20, the rest do not.
    check::equal(resolved_frequencies(100.0, 5, 10.0).size(), std::size_t{3}, "octaves surviving at step 10");
    check::equal(resolved_frequencies(100.0, 5, 0.001).size(), std::size_t{5}, "octaves surviving at a fine step");
    check::equal(resolved_frequencies(100.0, 5, 1000.0).size(), std::size_t{0}, "octaves surviving at a coarse step");
}

// --- the sphere grid -----------------------------------------------------

TEST(direction_is_a_unit_vector_everywhere) {
    for (const int n : {3, 17, 33}) {
        for (int z = 0; z < n; ++z) {
            for (int x = 0; x < n; ++x) {
                const Vec3 p = direction(x, z, n);
                check::close(std::sqrt(dot(p, p)), 1.0, 1e-12, "unit length");
            }
        }
    }
}

TEST(direction_closes_the_seam_and_collapses_the_poles) {
    const int n = 17;
    for (int z = 1; z < n - 1; ++z) {
        const Vec3 first = direction(0, z, n);
        const Vec3 seam = direction(n - 1, z, n);
        check::close(first.x, seam.x, 1e-12, "seam x");
        check::close(first.y, seam.y, 1e-12, "seam y");
        check::close(first.z, seam.z, 1e-12, "seam z");
    }
    for (int x = 0; x < n; ++x) {
        check::close(direction(x, 0, n).y, 1.0, 1e-12, "north pole");
        check::close(direction(x, n - 1, n).y, -1.0, 1e-12, "south pole");
    }
    // A negative x reaches the far side of the seam, not out of the grid.
    const Vec3 wrapped = direction(-1, 5, n);
    const Vec3 expected = direction(n - 2, 5, n);
    check::close(wrapped.x, expected.x, 1e-12, "negative x wraps");
}

TEST(sphere_grid_areas_sum_to_the_sphere) {
    for (const int n : {9, 17, 33}) {
        const auto held = sphere_grid(n, 1000.0);
        double total = 0.0;
        for (const double area : held->areas) total += area;
        // Pole nodes carry their whole cap, other cells one wedge each, so the node
        // areas must tile the sphere exactly.
        const double sphere = 4.0 * kPi * 1000.0 * 1000.0;
        check::close(total / sphere, 1.0, 1e-9, "area sum at n=" + std::to_string(n));
    }
}

TEST(sphere_grid_neighbours_are_symmetric) {
    const int n = 17;
    const auto held = sphere_grid(n, 500.0);
    const SphereGrid& grid = *held;
    // Floor first. Every assertion below is vacuously true if the neighbour lists are
    // empty, and the whole erosion routing is built on these lists.
    check::is_true(grid.node_count() > 0, "the grid to have nodes");
    for (std::size_t i = 0; i < grid.node_count(); ++i) {
        check::is_true(grid.neighbours[i].size() >= 3, "node " + std::to_string(i) + " to have neighbours");
    }
    for (std::size_t i = 0; i < grid.node_count(); ++i) {
        for (const SphereNeighbour& neighbour : grid.neighbours[i]) {
            const auto j = static_cast<std::size_t>(neighbour.index);
            check::is_true(j != i, "no self loop");
            const auto& back = grid.neighbours[j];
            const bool mutual = std::any_of(back.begin(), back.end(), [i](const SphereNeighbour& candidate) {
                return static_cast<std::size_t>(candidate.index) == i;
            });
            check::is_true(mutual, "node " + std::to_string(i) + " <-> " + std::to_string(j));
        }
    }
}

TEST(sphere_grid_is_cached_not_rebuilt) {
    const auto first = sphere_grid(17, 1234.0);
    const auto second = sphere_grid(17, 1234.0);
    check::is_true(first.get() == second.get(), "the same grid to be returned");
    const auto other = sphere_grid(33, 1234.0);
    check::is_true(other.get() != first.get(), "a different size to build a different grid");
    // The cache holds two entries and evicts. A holder must survive that, or every
    // caller is one unrelated sphere_grid call away from a dangling reference -- which
    // is why this returns a shared_ptr rather than a reference.
    sphere_grid(65, 1234.0);
    sphere_grid(129, 1234.0);
    check::equal(first->n, 17, "a held grid to survive eviction");
}

TEST(grid_to_grid_round_trips_through_the_node_list) {
    const int n = 17;
    const auto held = sphere_grid(n, 100.0);
    const SphereGrid& grid = *held;
    std::vector<double> values(grid.node_count());
    for (std::size_t i = 0; i < values.size(); ++i) values[i] = static_cast<double>(i);
    const Grid out = grid.to_grid(values);
    for (std::size_t i = 0; i < values.size(); ++i) {
        check::equal(out.at(grid.point_x[i], grid.point_z[i]), values[i], "node " + std::to_string(i));
    }
    for (int z = 0; z < n; ++z) check::equal(out.at(n - 1, z), out.at(0, z), "seam closed at row " + std::to_string(z));
}

TEST(sample_reproduces_grid_values_at_grid_points) {
    const int n = 17;
    Grid h(n);
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) h.at(x, z) = std::sin(x * 0.7) + std::cos(z * 0.4);
    }
    for (int z = 1; z < n - 1; ++z) {
        for (int x = 0; x < n - 1; ++x) {
            check::close(sample(h, direction(x, z, n)), h.at(x, z), 1e-9,
                         "sample at (" + std::to_string(x) + "," + std::to_string(z) + ")");
        }
    }
}

// --- world shape arithmetic ----------------------------------------------

TEST(design_radius_round_trips_to_the_authored_width) {
    const double scale = 0.17744123532462844;
    for (const double km : {200.0, 400.0, 600.0, 1000.0}) {
        const double radius = scale::design_radius(km * 1000.0, scale);
        check::close(2.0 * kPi * radius * scale, km * 1000.0, 1e-6, "round trip at " + std::to_string(km) + " km");
    }
}

TEST(derived_wavelength_holds_the_octave_count_across_widths) {
    // The property the derivation exists for: leave wavelength fixed and a world an
    // order of magnitude wider resolves no surface noise at all. Deriving it from the
    // radius makes the resolved octave count scale-invariant.
    //
    // Measured at a raster fine enough for the question to be about the wavelength.
    // At size 33 the answer is 1 octave at EVERY width and for BOTH wavelengths,
    // because the raster starves the ladder before the wavelength gets a say -- see
    // the_raster_is_what_starves_the_octave_ladder below.
    const double scale = 0.17744123532462844;
    int reference = -1;
    for (const double km : {200.0, 400.0, 600.0, 2000.0}) {
        const auto shape = scale::shape_overrides(km * 1000.0, 1667.0, 10.0, 12, scale);
        const auto report = scale::resolved_octaves(shape.globe_radius, 1025, shape.wavelength, 5);
        if (reference < 0) reference = report.resolved_octaves;
        check::equal(report.resolved_octaves, reference, "octaves at " + std::to_string(km) + " km");
    }
    check::equal(reference, 5, "a fine raster with a derived wavelength to resolve the whole ladder");

    // And the contrast: a FIXED wavelength loses octaves as the world grows.
    const auto narrow = scale::resolved_octaves(scale::design_radius(200000.0, scale), 1025, 4300.0, 5);
    const auto wide = scale::resolved_octaves(scale::design_radius(2000000.0, scale), 1025, 4300.0, 5);
    check::is_true(wide.resolved_octaves < narrow.resolved_octaves,
                   "a fixed wavelength to lose octaves as the world widens (" +
                       std::to_string(narrow.resolved_octaves) + " -> " + std::to_string(wide.resolved_octaves) + ")");
}

TEST(the_raster_is_what_starves_the_octave_ladder_at_small_sizes) {
    // Worth an explicit test because it is the opposite of the intuition: a
    // size-17 or size-33 world does not resolve fine terrain no matter how the
    // wavelength is chosen, so a coarse preview world is not a small version of the
    // real one -- it is a world with almost no surface detail. The derived
    // wavelength cannot fix that; only a finer raster can.
    const double scale = 0.17744123532462844;
    const auto shape = scale::shape_overrides(200000.0, 1667.0, 10.0, 12, scale);
    const int ladder[] = {17, 33, 65, 129, 257, 513, 1025};
    int previous = -1;
    for (const int size : ladder) {
        const auto report = scale::resolved_octaves(shape.globe_radius, size, shape.wavelength, 5);
        check::is_true(report.resolved_octaves >= previous,
                       "a finer raster never to resolve fewer octaves (size " + std::to_string(size) + ")");
        previous = report.resolved_octaves;
    }
    check::equal(scale::resolved_octaves(shape.globe_radius, 33, shape.wavelength, 5).resolved_octaves, 1,
                 "octaves at size 33");
    check::equal(previous, 5, "octaves at size 1025");
}

TEST(relief_budget_refuses_a_world_it_cannot_hold) {
    const double scale = 0.17744123532462844;
    const double ceiling = scale::max_relief_m(200000.0);
    check::throws([&] { scale::shape_overrides(200000.0, ceiling * 1.01, 10.0, 12, scale); },
                  "relief above the ceiling");
    // Just under the ceiling is admitted, so the guard is a boundary and not a wall.
    const auto shape = scale::shape_overrides(200000.0, ceiling * 0.99, 10.0, 12, scale);
    check::is_true(shape.tectonic_relief > 0.0, "a relief just under the ceiling to be admitted");
}

TEST(reach_and_runoff_scale_as_length_and_area) {
    const double one = scale::reach_scale(scale::reference_circumference_m());
    check::close(one, 1.0, 1e-12, "reference world scales by one");
    check::close(scale::runoff_scale(scale::reference_circumference_m()), 1.0, 1e-12, "reference runoff");
    const double doubled = scale::reach_scale(2.0 * scale::reference_circumference_m());
    check::close(doubled, 2.0, 1e-12, "double width doubles a reach");
    check::close(scale::runoff_scale(2.0 * scale::reference_circumference_m()), 4.0, 1e-12,
                 "double width quadruples a catchment");
}
