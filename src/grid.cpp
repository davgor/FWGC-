#include "fwg/grid.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <numbers>
#include <set>
#include <stdexcept>

namespace fwg {
namespace {

constexpr double kPi = std::numbers::pi;

// Floor modulo. The neighbour scan reaches x = -1 and the seam wrap has to land on
// n-2, not on -1. C++ `%` truncates toward zero and would index out of the grid.
int floor_mod(int value, int modulus) {
    const int r = value % modulus;
    return r < 0 ? r + modulus : r;
}

}  // namespace

double band_wedge_area(int z, int n, double radius) {
    const double step = kPi / (n - 1);
    const double lat = kPi / 2.0 - z * step;
    const double north = std::min(kPi / 2.0, lat + step / 2.0);
    const double south = std::max(-kPi / 2.0, lat - step / 2.0);
    return radius * radius * 2.0 * step * (std::sin(north) - std::sin(south));
}

Vec3 direction(int x, int z, int n) {
    if (z == 0) return {0.0, 1.0, 0.0};
    if (z == n - 1) return {0.0, -1.0, 0.0};
    const double lon = 2.0 * kPi * floor_mod(x, n - 1) / (n - 1) - kPi;
    const double lat = kPi / 2.0 - kPi * z / (n - 1);
    return {std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon)};
}

void Grid::fill_row(int z, const std::vector<double>& values) {
    if (z == 0 || z == n_ - 1) {
        for (int x = 0; x < n_; ++x) at(x, z) = values.front();
        return;
    }
    for (int x = 0; x < n_ - 1; ++x) at(x, z) = values[static_cast<std::size_t>(x)];
    at(n_ - 1, z) = values.front();  // seam
}

double sample(const Grid& h, const Vec3& p) {
    const int n = h.size();
    double x = (std::atan2(p.z, p.x) + kPi) / (2.0 * kPi) * (n - 1);
    x = std::fmod(x, static_cast<double>(n - 1));
    if (x < 0.0) x += n - 1;
    double z = (kPi / 2.0 - std::asin(clamp_unit(p.y))) / kPi * (n - 1);
    z = std::max(0.0, std::min(static_cast<double>(n - 1), z));

    const int ix = static_cast<int>(x);
    const int iz = static_cast<int>(z);
    const double fx = x - ix;
    const double fz = z - iz;
    const int jz = std::min(n - 1, iz + 1);

    return ((1.0 - fx) * h.at(ix, iz) + fx * h.at(ix + 1, iz)) * (1.0 - fz) +
           ((1.0 - fx) * h.at(ix, jz) + fx * h.at(ix + 1, jz)) * fz;
}

Grid SphereGrid::to_grid(const std::vector<double>& values) const {
    Grid out(n, 0.0);
    for (std::size_t i = 0; i < values.size(); ++i) {
        out.at(point_x[i], point_z[i]) = values[i];
    }
    for (int x = 0; x < n; ++x) {
        out.at(x, 0) = values.front();
        out.at(x, n - 1) = values.back();
    }
    for (int z = 0; z < n; ++z) out.at(n - 1, z) = out.at(0, z);
    return out;
}

namespace {

std::unique_ptr<SphereGrid> build_sphere_grid(int n, double radius) {
    if (n < 3) throw std::invalid_argument("sphere grid needs size >= 3");

    auto g = std::make_unique<SphereGrid>();
    g->n = n;
    g->radius = radius;

    // Node order: north pole, then every distinct cell row by row, then south pole.
    g->point_x.push_back(0);
    g->point_z.push_back(0);
    for (int z = 1; z < n - 1; ++z) {
        for (int x = 0; x < n - 1; ++x) {
            g->point_x.push_back(x);
            g->point_z.push_back(z);
        }
    }
    g->point_x.push_back(0);
    g->point_z.push_back(n - 1);

    const std::size_t count = g->point_x.size();

    // (x, z) -> node, with the poles collapsed and longitude wrapped.
    const auto node_at = [n](int x, int z) -> int {
        if (z <= 0) return 0;
        if (z >= n - 1) return static_cast<int>(1) + (n - 2) * (n - 1);
        return 1 + (z - 1) * (n - 1) + floor_mod(x, n - 1);
    };

    std::vector<Vec3> vectors(count);
    for (std::size_t i = 0; i < count; ++i) {
        vectors[i] = direction(g->point_x[i], g->point_z[i], n);
    }

    g->areas.resize(count);
    g->neighbours.resize(count);

    for (std::size_t i = 0; i < count; ++i) {
        const int x = g->point_x[i];
        const int z = g->point_z[i];
        const bool pole = (z == 0 || z == n - 1);

        // A pole node stands in for the whole cap, so it carries every wedge of it.
        g->areas[i] = band_wedge_area(z, n, radius) * (pole ? (n - 1) : 1);

        std::set<int> ids;
        if (pole) {
            const int ring = (z == 0) ? 1 : n - 2;
            for (int xx = 0; xx < n - 1; ++xx) ids.insert(node_at(xx, ring));
        } else {
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dz = -1; dz <= 1; ++dz) {
                    ids.insert(node_at(x + dx, z + dz));
                }
            }
            ids.erase(static_cast<int>(i));
        }

        g->neighbours[i].reserve(ids.size());
        for (const int j : ids) {
            const double arc = radius * std::acos(clamp_unit(dot(vectors[i], vectors[static_cast<std::size_t>(j)])));
            g->neighbours[i].push_back({j, arc});
        }
    }

    return g;
}

struct SphereGridCache {
    std::mutex mutex;
    std::vector<std::shared_ptr<const SphereGrid>> entries;  // bounded, most-recent first
};

SphereGridCache& cache() {
    static SphereGridCache instance;
    return instance;
}

}  // namespace

std::shared_ptr<const SphereGrid> sphere_grid(int n, double radius) {
    // Rebuilt from scratch by dozens of call sites per world; at n=129 that is a
    // sixteen-thousand-node graph with an arc length per neighbour each time. Two
    // entries bound what a size-257 world can retain.
    auto& c = cache();
    std::lock_guard<std::mutex> lock(c.mutex);
    for (std::size_t i = 0; i < c.entries.size(); ++i) {
        if (c.entries[i]->n == n && c.entries[i]->radius == radius) {
            if (i != 0) std::swap(c.entries[0], c.entries[i]);
            return c.entries[0];
        }
    }
    c.entries.insert(c.entries.begin(), std::shared_ptr<const SphereGrid>(build_sphere_grid(n, radius)));
    if (c.entries.size() > 2) c.entries.resize(2);
    return c.entries.front();
}

}  // namespace fwg
