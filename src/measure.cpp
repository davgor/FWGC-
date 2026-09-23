#include "fwg/measure.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace fwg {
namespace {
constexpr double kPi = std::numbers::pi;
}

Relief measure_globe(const Grid& height, double radius, double neighbourhood) {
    const int n = height.size();
    const double angle = kPi / (n - 1);
    const double ring = std::min(kPi / 2.0, std::max(angle, neighbourhood / radius));

    Relief out;
    out.slope = Grid(n);
    out.tpi = Grid(n);

    std::vector<double> slope_row;
    std::vector<double> tpi_row;

    for (int z = 0; z < n; ++z) {
        const int count = distinct_count(z, n);
        slope_row.assign(static_cast<std::size_t>(count), 0.0);
        tpi_row.assign(static_cast<std::size_t>(count), 0.0);

        for (int x = 0; x < count; ++x) {
            const Vec3 p = direction(x, z, n);

            // East-pointing tangent, degenerate at the poles where longitude has no
            // meaning; any consistent direction will do there.
            const double norm = std::hypot(p.x, p.z);
            const Vec3 e = (norm > 1e-10) ? Vec3{-p.z / norm, 0.0, p.x / norm} : Vec3{1.0, 0.0, 0.0};
            const Vec3 v = cross(p, e);

            const auto around = [&](double theta, double azimuth) {
                const double c = std::cos(theta);
                const double s = std::sin(theta);
                const double ca = std::cos(azimuth);
                const double sa = std::sin(azimuth);
                const Vec3 q{p.x * c + (e.x * ca + v.x * sa) * s,
                             p.y * c + (e.y * ca + v.y * sa) * s,
                             p.z * c + (e.z * ca + v.z * sa) * s};
                return sample(height, q);
            };

            const double gx = (around(angle, 0.0) - around(angle, kPi)) / (2.0 * radius * angle);
            const double gy = (around(angle, kPi / 2.0) - around(angle, 3.0 * kPi / 2.0)) / (2.0 * radius * angle);
            slope_row[static_cast<std::size_t>(x)] = std::atan(std::hypot(gx, gy)) * 180.0 / kPi;

            double ring_sum = 0.0;
            for (int j = 0; j < 8; ++j) ring_sum += around(ring, j * kPi / 4.0);
            tpi_row[static_cast<std::size_t>(x)] = height.at(x, z) - ring_sum / 8.0;
        }

        out.slope.fill_row(z, slope_row);
        out.tpi.fill_row(z, tpi_row);
    }
    return out;
}

}  // namespace fwg
