#include "fwg/climate.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fwg {
namespace {
constexpr double kPi = std::numbers::pi;
}

std::vector<Stencil> upwind_stencils(const SphereGrid& grid, double bearing) {
    const int n = grid.n;
    const double step = 2.0 * kPi / (n - 1);  // one cell of arc, at the equator
    const double sin_b = std::sin(bearing);
    const double cos_b = std::cos(bearing);

    std::vector<Stencil> out(grid.node_count());
    for (std::size_t i = 0; i < grid.node_count(); ++i) {
        const int x = grid.point_x[i];
        const int z = grid.point_z[i];
        const Vec3 p = direction(x, z, n);

        const double lon = 2.0 * kPi * x / (n - 1) - kPi;
        const double lat = kPi / 2.0 - kPi * z / (n - 1);

        // Local tangent basis. Wind is a compass bearing resolved onto it, so the
        // same bearing means the same thing at every latitude.
        const Vec3 east{-std::sin(lon), 0.0, std::cos(lon)};
        const Vec3 north{-std::sin(lat) * std::cos(lon), std::cos(lat), -std::sin(lat) * std::sin(lon)};
        const Vec3 wind{sin_b * east.x + cos_b * north.x,
                        sin_b * east.y + cos_b * north.y,
                        sin_b * east.z + cos_b * north.z};

        // Step one cell AGAINST the wind: this is where the air arriving here was.
        const double ca = std::cos(step);
        const double sa = std::sin(step);
        const Vec3 q{p.x * ca - wind.x * sa, p.y * ca - wind.y * sa, p.z * ca - wind.z * sa};

        const double xx = (std::atan2(q.z, q.x) + kPi) / (2.0 * kPi) * (n - 1);
        const double zz = std::max(0.0, std::min(static_cast<double>(n - 1),
                                                 (kPi / 2.0 - std::asin(clamp_unit(q.y))) / kPi * (n - 1)));
        const int ix = static_cast<int>(xx);
        const int iz = static_cast<int>(zz);
        const double fx = xx - ix;
        const double fz = zz - iz;
        const int jz = std::min(n - 1, iz + 1);

        Stencil& s = out[i];
        s.node[0] = grid.node_index(ix, iz);
        s.weight[0] = (1.0 - fx) * (1.0 - fz);
        s.node[1] = grid.node_index(ix + 1, iz);
        s.weight[1] = fx * (1.0 - fz);
        s.node[2] = grid.node_index(ix, jz);
        s.weight[2] = (1.0 - fx) * fz;
        s.node[3] = grid.node_index(ix + 1, jz);
        s.weight[3] = fx * fz;
    }
    return out;
}

MoistureField transport_moisture(const std::vector<double>& surface, const std::vector<char>& is_water,
                                 const std::vector<Stencil>& upwind, int passes, double strength) {
    const std::size_t count = surface.size();

    MoistureField out;
    out.humidity.assign(count, 0.0);
    out.rain.assign(count, 0.0);
    out.uplift.resize(count);

    // How far the ground climbs between where the air was and where it is. Only a
    // rise counts: descending air is not wrung out, which is the dry lee.
    std::vector<double> loss(count);
    for (std::size_t i = 0; i < count; ++i) {
        double upstream = 0.0;
        for (int k = 0; k < 4; ++k) {
            upstream += surface[static_cast<std::size_t>(upwind[i].node[k])] * upwind[i].weight[k];
        }
        out.uplift[i] = std::max(0.0, surface[i] - upstream);
        // 150 m of rise roughly doubles the base loss at unit strength; the 0.85 cap
        // stops a cliff from taking everything the air has in a single cell.
        loss[i] = std::min(0.85, 0.06 + strength * out.uplift[i] / 150.0);
    }

    std::vector<double> following(count, 0.0);
    for (int pass = 0; pass < passes; ++pass) {
        for (std::size_t i = 0; i < count; ++i) {
            double incoming = 0.0;
            for (int k = 0; k < 4; ++k) {
                incoming += out.humidity[static_cast<std::size_t>(upwind[i].node[k])] * upwind[i].weight[k];
            }
            // Over water the air tops up toward saturation; over land it only spends.
            const double available = is_water[i] ? incoming + 0.45 * (1.0 - incoming) : incoming;
            out.rain[i] = available * loss[i];
            following[i] = available - out.rain[i];
        }
        // Reported, never fed back — so computing it on every pass was 47 wasted
        // whole-grid passes out of 48 at the default count.
        if (pass == passes - 1) {
            double residual = 0.0;
            for (std::size_t i = 0; i < count; ++i) {
                residual = std::max(residual, std::abs(following[i] - out.humidity[i]));
            }
            out.residual = residual;
        }
        out.humidity.swap(following);
    }
    return out;
}

double climate_wetness(double rain, double temperature) {
    // Demand rises with heat: the same rainfall supports less life at 30 C than at 5.
    const double demand = 0.025 * (1.0 + std::max(0.0, temperature - 10.0) / 15.0);
    return rain / (rain + demand);
}

double surface_temperature(const Vec3& p, double height_m, double sea_level_m, double offset_c) {
    // 28 C at the equator falling to -17 C at the poles by sin^2(latitude), less a
    // 6.5 C/km lapse above the datum. Depth below the datum does not warm anything.
    return 28.0 - 45.0 * p.y * p.y - std::max(0.0, height_m - sea_level_m) * 0.0065 + offset_c;
}

}  // namespace fwg
