#include "fwg/routing.hpp"

#include <cmath>

namespace fwg {

RoadCost::RoadCost(const SphereGrid& grid, const Config& cfg, const std::vector<double>& water,
                   const std::vector<double>& height, const std::vector<double>& flood,
                   const std::vector<double>& river, const std::vector<double>* hazard)
    : grid_(grid),
      water_(water),
      height_(height),
      flood_(flood),
      river_(river),
      hazard_(hazard),
      max_grade_(cfg.road_max_grade),
      bridge_cost_(cfg.bridge_cost),
      magic_limit_(cfg.human_magic_limit) {}

bool RoadCost::blocked(std::size_t i) const {
    if (water_[i] != 0.0) return true;
    return hazard_ && (*hazard_)[i] > magic_limit_;
}

std::optional<double> RoadCost::operator()(int i, int j, double distance) const {
    const auto ui = static_cast<std::size_t>(i);
    const auto uj = static_cast<std::size_t>(j);
    if (distance <= 0.0 || blocked(ui) || blocked(uj)) return std::nullopt;

    // A diagonal step cannot slip between two water-filled corner cells: a road may not
    // cut the corner of a lake merely because both its own endpoints are dry. Pole rows
    // are exempt because a pole node is adjacent to its whole ring and has no corners.
    const int n = grid_.n;
    const int xi = grid_.point_x[ui];
    const int zi = grid_.point_z[ui];
    const int xj = grid_.point_x[uj];
    const int zj = grid_.point_z[uj];
    if (xi != xj && zi != zj && zi != 0 && zi != n - 1 && zj != 0 && zj != n - 1) {
        const auto a = static_cast<std::size_t>(grid_.node_index(xi, zj));
        const auto b = static_cast<std::size_t>(grid_.node_index(xj, zi));
        if (blocked(a) || blocked(b)) return std::nullopt;
    }

    const double grade = std::abs(height_[uj] - height_[ui]) / distance;
    if (grade > max_grade_) return std::nullopt;

    // Quadratic in grade, so a gentle detour beats a short climb; linear in flood risk;
    // plus a bridge wherever a river has to be crossed.
    const double bridge = (river_[ui] != 0.0 || river_[uj] != 0.0) ? bridge_cost_ : 0.0;
    return distance * (1.0 + 12.0 * grade * grade + 0.5 * (flood_[ui] + flood_[uj])) + bridge;
}

long long habitat_capacity(const std::vector<double>& areas, const std::vector<double>& scores,
                           const std::vector<int>& eligible, double land_per_city_km2) {
    double weighted = 0.0;
    for (const int i : eligible) {
        const auto ui = static_cast<std::size_t>(i);
        weighted += areas[ui] * scores[ui];
    }
    return static_cast<long long>(std::floor(weighted / 1e6 / land_per_city_km2));
}

long long city_capacity(double weighted_habitat_km2, long long allowance, double land_per_city_km2,
                        double minimum_founding_residents) {
    // Two independent ceilings: how much good ground there is, and how many people
    // exist to found with. The smaller binds.
    const long long by_land = static_cast<long long>(std::floor(weighted_habitat_km2 / land_per_city_km2));
    const auto minimum = static_cast<long long>(minimum_founding_residents);
    const long long by_people = minimum > 0 ? allowance / minimum : 0;
    return std::min(by_land, by_people);
}

LifeCapacity life_capacity(const std::vector<double>& areas,
                           const std::vector<std::vector<double>>& potentials) {
    LifeCapacity out;
    out.by_people.assign(potentials.size(), 0);
    std::vector<double> totals(potentials.size(), 0.0);
    double total = 0.0;

    for (std::size_t i = 0; i < areas.size(); ++i) {
        double weight = 0.0;
        double best = 0.0;
        for (const std::vector<double>& field : potentials) {
            weight += field[i];
            best = std::max(best, field[i]);
        }
        if (weight <= 0.0) continue;
        // The cell supports what its BEST claimant could support, and that one capacity
        // is then split between the claimants in proportion. Overlapping claims share a
        // cell; they never multiply it.
        const double capacity = areas[i] / 1e6 * 100.0 * best;
        total += capacity;
        for (std::size_t p = 0; p < potentials.size(); ++p) {
            totals[p] += capacity * potentials[p][i] / weight;
        }
    }

    out.total = static_cast<long long>(std::floor(total));
    for (std::size_t p = 0; p < totals.size(); ++p) {
        // Floor, not round: a fraction of a resident is not a resident.
        out.by_people[p] = static_cast<long long>(std::floor(totals[p]));
    }
    return out;
}

}  // namespace fwg
