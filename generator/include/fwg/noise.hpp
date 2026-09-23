// Gradient noise on a 3D lattice.
//
// The lattice hash and the twelve cube-edge gradients are ported exactly from the
// reference generator: the terrain's character is entirely in this function, and it
// is pure integer arithmetic plus a handful of float operations, so there was no
// reason to invent a new one. The one deliberate divergence is the quintic blend,
// which evaluates f*f*f here rather than the reference's pow(f, 3) -- see
// docs/determinism.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fwg {

double perlin3(double x, double y, double z, std::uint32_t seed);

// Octave frequencies surviving the Nyquist filter: an octave finer than two grid
// cells is dropped rather than aliased into the terrain. `step` is the equatorial
// cell spacing.
std::vector<double> resolved_frequencies(double wavelength, int octaves, double step);

// Seed for octave k of a noise ladder rooted at `base`.
//
// This is the THIRD and last seed derivation in the library -- see the block comment
// in random.hpp. It was an unnamed inline expression until a review pointed out that
// an unnamed derivation is exactly what the reference implementation had four of.
//
// Note it is an OFFSET, not a hash: octave seeds of one field are 1013 apart, so two
// fields whose base seeds differ by a multiple of 1013 share octaves. The bases here
// come from sha256, so that is vanishingly unlikely rather than impossible, and it is
// why a new noise field must take a fresh child_seed rather than base + 1.
inline std::uint32_t octave_seed(std::uint32_t base, std::size_t k) {
    return base + static_cast<std::uint32_t>(k) * 1013u;
}

}  // namespace fwg
