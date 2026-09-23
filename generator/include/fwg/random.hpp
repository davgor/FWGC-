// The generator's random stream.
//
// This is a deliberate replacement for the reference implementation's dependence on
// CPython's `random` module, and the reasons are worth stating because they are the
// reasons this file does not use <random> either.
//
//  * `std::uniform_real_distribution` and `std::normal_distribution` are NOT
//    specified to produce the same values across standard library implementations.
//    A world generated with libstdc++ would not match one generated with MSVC's
//    STL. Only the *engines* in <random> are portable, not the distributions, so
//    every distribution here is hand-rolled.
//
//  * `gauss` does not cache the spare normal deviate that the polar method produces
//    for free. Caching it makes the number of stream words a call consumes depend on
//    whether the previous call happened, which turns an early `continue` in a
//    rejection loop into a silent world change. The wasted deviate is cheaper than
//    that class of bug.
//
//  * `sample` uses one algorithm at every population size. The reference switches
//    between a selection set and a pool depending on how large the population is,
//    and the two consume different amounts of stream, so the choice of algorithm is
//    observable in the output.
//
// The engine is xoshiro256++ seeded through SplitMix64: 64-bit integer operations
// only, no 128-bit intrinsics, identical on every compiler.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fwg {

class Rng {
public:
    explicit Rng(std::uint64_t seed);

    // A named, independent stream derived from this one's seed. Two subsystems that
    // take children of the same world seed never interleave, so adding a draw to one
    // cannot move the other.
    static Rng child(std::uint64_t master, std::string_view domain, std::int64_t variation = 0);

    std::uint64_t next_u64();

    // [0, 1), 53 significant bits.
    double random();

    // [low, high)
    double uniform(double low, double high);

    // [0, bound), unbiased. bound must be positive.
    std::uint64_t below(std::uint64_t bound);

    // [low, high] inclusive, as the reference's randint.
    std::int64_t randint(std::int64_t low, std::int64_t high);

    // Standard normal scaled and shifted. Marsaglia polar, spare discarded.
    double gauss(double mean, double stddev);

    template <typename T>
    const T& choice(const std::vector<T>& items) {
        return items[static_cast<std::size_t>(below(items.size()))];
    }

    template <typename T>
    void shuffle(std::vector<T>& items) {
        for (std::size_t i = items.size(); i > 1; --i) {
            const auto j = static_cast<std::size_t>(below(i));
            std::swap(items[i - 1], items[j]);
        }
    }

    // k distinct indices of `population_size`, in draw order.
    std::vector<std::size_t> sample_indices(std::size_t population_size, std::size_t k);

    std::uint64_t seed() const { return seed_; }

    // Words drawn from the engine since construction.
    //
    // Exists so tests can assert STREAM DISCIPLINE directly instead of describing it
    // in a comment: that one call costs one word plus rejections, that gauss does not
    // cache a spare, that no distribution short-circuits on the value of its argument.
    // Every defect this class is built to avoid is a defect in this number.
    std::uint64_t draws() const { return draws_; }

private:
    std::uint64_t seed_;
    std::uint64_t draws_ = 0;
    std::uint64_t state_[4];
};

// ---------------------------------------------------------------------------
// Seed derivation. There are exactly THREE derivations in this library and they
// are all named here or in noise.hpp. That is not tidiness: the reference
// implementation had four undeclared ones, only one of which any record described,
// and a porter who implements three of four gets a world that is correct except
// where it is not. If you need a fourth, name it here.
//
//   child_seed    32-bit, for a NOISE FIELD seed (perlin3 takes a uint32)
//   child_seed64  64-bit, for an RNG STREAM seed (what Rng::child uses)
//   octave_seed   the per-octave offset within one noise ladder (noise.hpp)
// ---------------------------------------------------------------------------

// First four bytes of sha256("<namespace>:<master>:<domain>:<variation>"), big-endian.
// Kept from the reference because it is cheap, portable, and already the meaning of a
// seed for the noise fields.
std::uint32_t child_seed(std::uint32_t master, std::string_view domain, std::int64_t variation = 0,
                         std::string_view name_space = "tectonics-v1");

// First eight bytes of the same digest under this library's own namespace. This is the
// derivation behind Rng::child; it is exposed so a caller that wants the seed without
// the engine uses the same one rather than inventing a fourth.
std::uint64_t child_seed64(std::uint64_t master, std::string_view domain, std::int64_t variation = 0);

}  // namespace fwg
