#include "fwg/noise.hpp"

#include <cmath>

namespace fwg {
namespace {

constexpr double kInvRoot2 = 0.70710678118654752440084436210485;

constexpr std::uint32_t kHashX = 374761393u;
constexpr std::uint32_t kHashY = 668265263u;
constexpr std::uint32_t kHashZ = 2147483647u;
constexpr std::uint32_t kHashSeed = 1442695041u;
constexpr std::uint32_t kMix = 1274126177u;

}  // namespace

double perlin3(double x, double y, double z, std::uint32_t seed) {
    // Millions of calls per world, so the corner hash and the blend weights are built
    // up incrementally rather than from scratch in the innermost loop.
    const double fx = std::floor(x);
    const double fy = std::floor(y);
    const double fz = std::floor(z);
    const auto ix = static_cast<std::uint32_t>(static_cast<std::int64_t>(fx));
    const auto iy = static_cast<std::uint32_t>(static_cast<std::int64_t>(fy));
    const auto iz = static_cast<std::uint32_t>(static_cast<std::int64_t>(fz));

    const double f0 = x - fx;
    const double f1 = y - fy;
    const double f2 = z - fz;

    // Quintic smoothstep: f^3 * (f * (6f - 15) + 10).
    const double b0 = f0 * f0 * f0 * (f0 * (f0 * 6.0 - 15.0) + 10.0);
    const double b1 = f1 * f1 * f1 * (f1 * (f1 * 6.0 - 15.0) + 10.0);
    const double b2 = f2 * f2 * f2 * (f2 * (f2 * 6.0 - 15.0) + 10.0);

    const double weight0[2] = {1.0 - b0, b0};
    const double weight1[2] = {1.0 - b1, b1};
    const double weight2[2] = {1.0 - b2, b2};

    // Unsigned wraparound is modulo 2^32, which is exactly the masking the reference
    // applies to its arbitrary-precision integers.
    const std::uint32_t hash_x = ix * kHashX;
    const std::uint32_t hash_y = iy * kHashY;
    const std::uint32_t hash_z = iz * kHashZ + seed * kHashSeed;

    double value = 0.0;
    for (int a = 0; a < 2; ++a) {
        const std::uint32_t hash_a = hash_x + static_cast<std::uint32_t>(a) * kHashX + hash_y;
        const double offset0 = f0 - a;
        const double blend_a = weight0[a];
        for (int b = 0; b < 2; ++b) {
            const std::uint32_t hash_b = hash_a + static_cast<std::uint32_t>(b) * kHashY + hash_z;
            const double offset1 = f1 - b;
            const double blend_ab = blend_a * weight1[b];
            for (int c = 0; c < 2; ++c) {
                std::uint32_t h = hash_b + static_cast<std::uint32_t>(c) * kHashZ;
                h = (h ^ (h >> 13)) * kMix;
                h ^= h >> 16;

                // Twelve unit edge gradients of a cube.
                const std::uint32_t axis = h % 3u;
                const std::uint32_t u = (axis + 1u) % 3u;
                const std::uint32_t v = (axis + 2u) % 3u;
                const double offsets[3] = {offset0, offset1, f2 - c};
                const double su = (h & 4u) ? 1.0 : -1.0;
                const double sv = (h & 8u) ? 1.0 : -1.0;
                const double d = (offsets[u] * su + offsets[v] * sv) * kInvRoot2;

                value += d * (blend_ab * weight2[c]);
            }
        }
    }
    return value;
}

std::vector<double> resolved_frequencies(double wavelength, int octaves, double step) {
    std::vector<double> out;
    double scale = 1.0;
    for (int k = 0; k < octaves; ++k) {
        if (wavelength / scale >= 2.0 * step) out.push_back(scale / wavelength);
        scale *= 2.0;
    }
    return out;
}

}  // namespace fwg
