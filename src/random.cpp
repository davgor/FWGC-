#include "fwg/random.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "fwg/sha256.hpp"

namespace fwg {
namespace {

constexpr std::uint64_t rotl(std::uint64_t v, int n) { return (v << n) | (v >> (64 - n)); }

// SplitMix64: turns one seed word into a well-distributed state, so that adjacent
// seeds (42, 43) do not produce correlated streams.
std::uint64_t splitmix64(std::uint64_t& x) {
    x += 0x9e3779b97f4a7c15ull;
    std::uint64_t z = x;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

}  // namespace

Rng::Rng(std::uint64_t seed) : seed_(seed) {
    std::uint64_t x = seed;
    for (auto& word : state_) word = splitmix64(x);
}

Rng Rng::child(std::uint64_t master, std::string_view domain, std::int64_t variation) {
    std::string material = "fwg-stream-v1:";
    material += std::to_string(master);
    material += ':';
    material.append(domain);
    material += ':';
    material += std::to_string(variation);
    const auto digest = sha256(material);
    std::uint64_t seed = 0;
    for (int i = 0; i < 8; ++i) seed = (seed << 8) | digest[static_cast<std::size_t>(i)];
    return Rng(seed);
}

std::uint64_t Rng::next_u64() {
    // xoshiro256++
    const std::uint64_t result = rotl(state_[0] + state_[3], 23) + state_[0];
    const std::uint64_t t = state_[1] << 17;
    state_[2] ^= state_[0];
    state_[3] ^= state_[1];
    state_[1] ^= state_[2];
    state_[0] ^= state_[3];
    state_[2] ^= t;
    state_[3] = rotl(state_[3], 45);
    return result;
}

double Rng::random() {
    // Top 53 bits scaled by 2^-53: every representable double in [0, 1) with the
    // right probability, and no dependence on the rounding of a division.
    return static_cast<double>(next_u64() >> 11) * 0x1.0p-53;
}

double Rng::uniform(double low, double high) {
    return low + (high - low) * random();
}

std::uint64_t Rng::below(std::uint64_t bound) {
    if (bound == 0) throw std::invalid_argument("Rng::below needs a positive bound");
    if (bound == 1) return 0;
    // Rejection sampling against the largest exact multiple of `bound`. Unbiased and
    // deterministic; the modulo shortcut is neither.
    const std::uint64_t limit = std::numeric_limits<std::uint64_t>::max() - (std::numeric_limits<std::uint64_t>::max() % bound) - 1;
    std::uint64_t draw = next_u64();
    while (draw > limit) draw = next_u64();
    return draw % bound;
}

std::int64_t Rng::randint(std::int64_t low, std::int64_t high) {
    if (high < low) throw std::invalid_argument("Rng::randint needs high >= low");
    const auto span = static_cast<std::uint64_t>(high - low) + 1u;
    return low + static_cast<std::int64_t>(below(span));
}

double Rng::gauss(double mean, double stddev) {
    // Marsaglia polar. The second deviate is discarded on purpose -- see random.hpp.
    double u = 0.0;
    double v = 0.0;
    double s = 0.0;
    do {
        u = 2.0 * random() - 1.0;
        v = 2.0 * random() - 1.0;
        s = u * u + v * v;
    } while (s >= 1.0 || s == 0.0);
    return mean + stddev * u * std::sqrt(-2.0 * std::log(s) / s);
}

std::vector<std::size_t> Rng::sample_indices(std::size_t population_size, std::size_t k) {
    if (k > population_size) throw std::invalid_argument("Rng::sample_indices needs k <= population");
    // Partial Fisher-Yates over a lazily materialised permutation: one algorithm at
    // every size, k draws exactly, and no allocation proportional to the population
    // until an index is actually displaced.
    std::vector<std::size_t> out;
    out.reserve(k);
    std::unordered_map<std::size_t, std::size_t> moved;
    for (std::size_t i = 0; i < k; ++i) {
        const std::size_t j = i + static_cast<std::size_t>(below(population_size - i));
        const auto at_j = moved.find(j);
        const std::size_t value = (at_j == moved.end()) ? j : at_j->second;
        const auto at_i = moved.find(i);
        moved[j] = (at_i == moved.end()) ? i : at_i->second;
        out.push_back(value);
    }
    return out;
}

std::uint32_t child_seed(std::uint32_t master, std::string_view domain, std::int64_t variation,
                         std::string_view name_space) {
    std::string material;
    material.append(name_space);
    material += ':';
    material += std::to_string(master);
    material += ':';
    material.append(domain);
    material += ':';
    material += std::to_string(variation);
    return sha256_seed(material);
}

}  // namespace fwg
