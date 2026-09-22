// Gradient noise on a 3D lattice.
//
// The lattice hash and the twelve cube-edge gradients are ported exactly from the
// reference generator: the terrain's character is entirely in this function, and it
// is pure integer arithmetic plus a handful of float operations, so there was no
// reason to invent a new one. The one deliberate divergence is the quintic blend,
// which evaluates f*f*f here rather than the reference's pow(f, 3) -- see
// docs/determinism.md.
#pragma once

#include <cstdint>
#include <vector>

namespace fwg {

double perlin3(double x, double y, double z, std::uint32_t seed);

// Octave frequencies surviving the Nyquist filter: an octave finer than two grid
// cells is dropped rather than aliased into the terrain. `step` is the equatorial
// cell spacing.
std::vector<double> resolved_frequencies(double wavelength, int octaves, double step);

}  // namespace fwg
