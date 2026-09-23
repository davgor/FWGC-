// Pure world-shape arithmetic: a circumference and a relief budget in, config terms
// out. No state, no I/O, no Config dependency -- floats in, floats out.
//
// The scale stays at the recipe's constant so that a metre of relief means the same
// thing at every world width, and the *design radius* carries the width instead.
// Wavelength is derived along with it: leave it fixed and surface detail silently
// vanishes as the world grows, because every octave falls below Nyquist.
#pragma once

namespace fwg::scale {

// Amplitude relative to tectonic relief (1100/800), the authoring ratio.
inline constexpr double kAmplitudeRatio = 1100.0 / 800.0;

// The active relief budget is bounded at 90% of the design radius:
// 5*tectonic_relief + 2*amplitude < 0.9*globe_radius.
inline constexpr double kReliefBudgetFactor = 5.0;
inline constexpr double kDetailBudgetFactor = 2.0;
inline constexpr double kBudgetHeadroom = 0.9;

// Physical size of the reference world: design radius 10000 at the recipe scale.
inline constexpr double kReferenceRadiusM = 1774.4123532462844;
double reference_circumference_m();

// Factor for reaches, spacings and influence radii authored against the reference
// world. Left absolute, a 1000 m support reach is a sixth of one cell on a 200 km
// world and the whole rural layer disappears.
double reach_scale(double circumference_m);

// The area analogue: catchments take the square of the length factor.
double runoff_scale(double circumference_m);

double design_radius(double circumference_m, double world_scale);
double physical_radius(double circumference_m);
double tectonic_relief_for(double relief_m, double world_scale);
double amplitude_for(double tectonic_relief, double amplitude_ratio = kAmplitudeRatio);

// Surface-noise wavelength that holds detail at a constant fraction of the world.
double detail_wavelength(double radius, int plate_count);

// Boundary belt width that keeps belts a constant share of a plate's span.
double belt_width_for(int plate_count);

// Largest physical relief the budget guard admits for a circumference. world_scale
// cancels out: relief cannot approach the planet's own size.
double max_relief_m(double circumference_m, double amplitude_ratio = kAmplitudeRatio);

struct OctaveReport {
    int resolved_octaves = 0;
    int requested_octaves = 0;
    double resolved_octave_fraction = 0.0;
    double cell_step_m = 0.0;
};

// `radius` is the DESIGN radius. The Nyquist filter runs before the world is scaled
// to metres, and the two radii differ by 1/world_scale; reading the physical one
// here overstates the surviving octaves badly.
OctaveReport resolved_octaves(double radius, int size, double wavelength, int octaves);

struct ShapeOverrides {
    double globe_radius = 0.0;
    double world_scale = 0.0;
    double tectonic_relief = 0.0;
    double amplitude = 0.0;
    double wavelength = 0.0;
    int plate_count = 0;
    double orogeny = 0.0;
    double belt_width = 0.0;  // only set when scale_belt_width was requested
    bool has_belt_width = false;
};

// Throws std::invalid_argument when the relief budget cannot be satisfied, rather
// than letting config validation refuse a config the caller cannot diagnose.
ShapeOverrides shape_overrides(double circumference_m, double relief_m, double orogeny,
                               int plate_count, double world_scale,
                               double amplitude_ratio = kAmplitudeRatio,
                               bool scale_belt_width = false);

}  // namespace fwg::scale
