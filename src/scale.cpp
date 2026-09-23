#include "fwg/scale.hpp"

#include <cmath>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace fwg::scale {
namespace {
constexpr double kPi = std::numbers::pi;
}

double reference_circumference_m() { return 2.0 * kPi * kReferenceRadiusM; }

double reach_scale(double circumference_m) { return circumference_m / reference_circumference_m(); }

double runoff_scale(double circumference_m) {
    const double factor = reach_scale(circumference_m);
    return factor * factor;
}

double design_radius(double circumference_m, double world_scale) {
    return circumference_m / (2.0 * kPi * world_scale);
}

double physical_radius(double circumference_m) { return circumference_m / (2.0 * kPi); }

double tectonic_relief_for(double relief_m, double world_scale) { return relief_m / world_scale; }

double amplitude_for(double tectonic_relief, double amplitude_ratio) {
    return tectonic_relief * amplitude_ratio;
}

double detail_wavelength(double radius, int plate_count) {
    return radius * 1.6 / std::sqrt(static_cast<double>(plate_count));
}

double belt_width_for(int plate_count) { return 0.28 / std::sqrt(static_cast<double>(plate_count)); }

double max_relief_m(double circumference_m, double amplitude_ratio) {
    const double divisor = 2.0 * kPi * (kReliefBudgetFactor + kDetailBudgetFactor * amplitude_ratio);
    return kBudgetHeadroom * circumference_m / divisor;
}

OctaveReport resolved_octaves(double radius, int size, double wavelength, int octaves) {
    OctaveReport report;
    report.cell_step_m = 2.0 * kPi * radius / (size - 1);
    report.requested_octaves = octaves;
    double scale = 1.0;
    for (int k = 0; k < octaves; ++k) {
        if (wavelength / scale >= 2.0 * report.cell_step_m) ++report.resolved_octaves;
        scale *= 2.0;
    }
    report.resolved_octave_fraction =
        octaves ? static_cast<double>(report.resolved_octaves) / octaves : 0.0;
    return report;
}

ShapeOverrides shape_overrides(double circumference_m, double relief_m, double orogeny,
                               int plate_count, double world_scale, double amplitude_ratio,
                               bool scale_belt_width) {
    if (!(std::isfinite(circumference_m) && circumference_m > 0.0))
        throw std::invalid_argument("circumference must be a positive number of metres");
    if (!(std::isfinite(relief_m) && relief_m > 0.0))
        throw std::invalid_argument("relief must be a positive number of metres");
    if (plate_count < 2) throw std::invalid_argument("plate_count must be at least 2");

    const double ceiling = max_relief_m(circumference_m, amplitude_ratio);
    if (relief_m >= ceiling) {
        // Round both printed bounds OUTWARD. Rounding the needed width to nearest can
        // round it down, and a caller who follows the advice literally builds the width
        // the message named and is refused again.
        const double needed_km = std::ceil(circumference_m * relief_m / ceiling / 10.0) / 100.0;
        const double admitted_m = std::floor(ceiling * 100.0) / 100.0;
        std::ostringstream message;
        message << "relief of " << relief_m << " m needs a circumference above " << needed_km
                << " km; " << circumference_m / 1000.0 << " km admits at most " << admitted_m << " m";
        throw std::invalid_argument(message.str());
    }

    ShapeOverrides out;
    out.globe_radius = design_radius(circumference_m, world_scale);
    out.world_scale = world_scale;
    out.tectonic_relief = tectonic_relief_for(relief_m, world_scale);
    out.amplitude = amplitude_for(out.tectonic_relief, amplitude_ratio);
    out.wavelength = detail_wavelength(out.globe_radius, plate_count);
    out.plate_count = plate_count;
    out.orogeny = orogeny;
    if (scale_belt_width) {
        out.belt_width = belt_width_for(plate_count);
        out.has_belt_width = true;
    }
    return out;
}

}  // namespace fwg::scale
