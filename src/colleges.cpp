#include "fwg/colleges.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <tuple>

#include "fwg/humans.hpp"
#include "fwg/routing.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

// A college stands this far from any settlement, college included.
//
// ABSOLUTE METRES, and one of the distances docs/scale-defects.md is about: authored for
// the 11.15 km reference world, whose cells are about 87 m, where it kept a college off
// the neighbouring cells of a town. On a 200 km world the smallest step is 1562 m, so it
// can only ever refuse the settlement's own node. Carried as authored; the spacing
// BETWEEN colleges, below, is the one that scales.
constexpr double kSettlementClearanceM = 150.0;

// Magic a college needs before anything else is asked. Dimensionless.
constexpr double kDensityFloor = 0.35;

// How much a cell's general suitability counts against its magic when colleges are
// ranked. Dimensionless.
constexpr double kSuitabilityRankWeight = 0.25;

std::vector<double> nodes_of(const World& world, const SphereGrid& grid, const std::string& layer,
                             double fallback = 0.0) {
    std::vector<double> out(grid.node_count(), fallback);
    const auto it = world.layers.find(layer);
    if (it == world.layers.end()) return out;
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = it->second.at(grid.point_x[i], grid.point_z[i]);
    return out;
}

std::string diagnostic_reason(int placed, int wanted, int candidates, int sources,
                              const std::array<int, kCollegeLimitCount>& refusals, int clearance,
                              int spacing) {
    if (wanted <= 0) return "no colleges were requested";
    if (placed >= wanted) {
        return std::to_string(placed) + " of " + std::to_string(candidates) +
               " candidates were seated, which is every college asked for";
    }
    if (placed > 0) {
        return std::to_string(placed) + " of " + std::to_string(wanted) +
               " colleges were seated from " + std::to_string(candidates) +
               " candidates; settlement clearance refused " + std::to_string(clearance) +
               " and regional spacing refused " + std::to_string(spacing);
    }
    if (candidates > 0) {
        return "no room: settlement clearance refused " + std::to_string(clearance) + " of " +
               std::to_string(candidates) + " candidates and regional spacing refused " +
               std::to_string(spacing);
    }
    if (sources > 0) {
        // The most refusals, then the EARLIEST limit tested: a deterministic answer to
        // "what stopped it" when two limits refused equally often.
        std::size_t worst = 0;
        for (std::size_t k = 1; k < kCollegeLimitCount; ++k) {
            if (refusals[k] > refusals[worst]) worst = k;
        }
        return std::to_string(sources) +
               " cells within a city's support reach were read and none produced a candidate; " +
               std::string(to_string(static_cast<CollegeLimit>(worst))) + " refused the most, at " +
               std::to_string(refusals[worst]);
    }
    return "no cells within a city's support reach in this world, so nothing rolled";
}

}  // namespace

std::string_view to_string(CollegeLimit limit) {
    switch (limit) {
        case CollegeLimit::Density: return "density";
        case CollegeLimit::Hazard: return "hazard";
        case CollegeLimit::Water: return "water";
        case CollegeLimit::Slope: return "slope";
        case CollegeLimit::Temperature: return "temperature";
        case CollegeLimit::Freshwater: return "freshwater";
        case CollegeLimit::Flood: return "flood";
        case CollegeLimit::Suitability: return "suitability";
    }
    return "unknown";
}

std::optional<CollegeLimit> college_refusal(double density, double hazard, double hazard_limit,
                                            double slope, double temperature, double freshwater_m,
                                            double flood, double suitability, double water,
                                            const CivilizationTraits& t) {
    // Written as negated admissions -- `!(x >= floor)` rather than `x < floor` -- so that a
    // NaN in any field refuses rather than admits, which is what the reference's `not`
    // chain does.
    if (!(density >= kDensityFloor)) return CollegeLimit::Density;
    if (!(hazard <= hazard_limit)) return CollegeLimit::Hazard;
    if (!(water == 0.0)) return CollegeLimit::Water;
    if (!(slope < t.college_slope_limit)) return CollegeLimit::Slope;
    if (!(t.college_temperature_min < temperature && temperature < t.college_temperature_max)) {
        return CollegeLimit::Temperature;
    }
    // Negative is the published "no fresh water reachable", and refuses.
    if (!(0.0 <= freshwater_m && freshwater_m <= t.college_water_reach)) return CollegeLimit::Freshwater;
    if (!(flood < t.college_flood_limit)) return CollegeLimit::Flood;
    if (!(suitability >= t.college_suitability_min)) return CollegeLimit::Suitability;
    return std::nullopt;
}

double college_spacing_m(double globe_radius_m) { return std::max(1500.0, globe_radius_m * 0.15); }

CollegeReport add_colleges(const World& world, const HumanHinterland& humans) {
    CollegeReport report;
    const Config& cfg = world.effective;
    report.spacing_m = college_spacing_m(cfg.globe_radius);
    report.diagnostic.wanted = cfg.college_count;

    const std::vector<SettlementSite>& cities = world.settlements.sites;
    if (!world.has_magic || cities.empty()) {
        report.diagnostic.reason = diagnostic_reason(0, cfg.college_count, 0, 0, report.refusals, 0, 0);
        return report;
    }

    const auto held = sphere_grid(cfg.size, cfg.globe_radius);
    const SphereGrid& grid = *held;
    const std::size_t count = grid.node_count();

    const std::vector<double> density = nodes_of(world, grid, "magic_density");
    const std::vector<double> world_hazard = nodes_of(world, grid, "magic_hazard");
    const std::vector<double> slope = nodes_of(world, grid, "slope");
    const std::vector<double> temperature = nodes_of(world, grid, "temperature");
    const std::vector<double> freshwater = nodes_of(world, grid, "freshwater_distance", -1.0);
    const std::vector<double> flood = nodes_of(world, grid, "flood_risk");
    const std::vector<double> water = nodes_of(world, grid, "water_type");
    const std::vector<double> height = nodes_of(world, grid, "height");
    const std::vector<double> river = nodes_of(world, grid, "rain_river");

    // --- which city owns each cell --------------------------------------------
    // Each city searches under its OWN people's road tolerances, and the cheapest reach
    // owns the cell; a tie goes to the earlier city because the comparison is strict.
    //
    // The search is BOUNDED at the support reach rather than run to the whole planet and
    // filtered afterwards. The reference does the latter, and for non-negative edges the
    // two are exact equals: every node the bound admits is settled with its final distance
    // and parent before the bound cuts anything off.
    std::vector<double> distance(count, kUnreachable);
    std::vector<int> owner(count, -1);
    std::vector<std::vector<int>> parent_by_city(cities.size());
    std::vector<const CivilizationProfile*> profiles(cities.size(), nullptr);

    for (std::size_t c = 0; c < cities.size(); ++c) {
        const CivilizationProfile* profile = civilization_profile(cities[c].population_profile);
        profiles[c] = profile;
        if (profile == nullptr) continue;

        Config theirs = cfg;
        theirs.human_magic_limit = profile->traits.mutation_limit;
        theirs.road_max_grade = profile->traits.road_grade_limit;
        const std::string risk_layer = "magic_risk_" + cities[c].population_profile;
        const std::vector<double> risk =
            world.layers.count(risk_layer) != 0 ? nodes_of(world, grid, risk_layer) : world_hazard;
        const RoadCost cost(grid, theirs, water, height, flood, river, &risk);

        PathResult own = shortest_paths(grid, cities[c].node, cost, nullptr, cfg.support_reach);
        for (std::size_t i = 0; i < count; ++i) {
            if (own.distance[i] < distance[i] && own.distance[i] <= cfg.support_reach) {
                distance[i] = own.distance[i];
                owner[i] = static_cast<int>(c);
            }
        }
        parent_by_city[c] = std::move(own.parent);
    }

    // --- what the owner's people make of the cell -------------------------------
    // Hazard and suitability are read through the OWNER's eyes: its own risk field and
    // its own opinion of the ground. An unowned cell is maximally hazardous and never
    // suitable, which keeps it out of the ranking without a special case.
    std::vector<double> hazard(count, 1.0);
    std::vector<double> suitability(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        const int o = owner[i];
        if (o < 0) continue;
        const std::string& people = cities[static_cast<std::size_t>(o)].population_profile;
        const auto risk_it = world.layers.find("magic_risk_" + people);
        hazard[i] = risk_it != world.layers.end()
                        ? risk_it->second.at(grid.point_x[i], grid.point_z[i])
                        : world_hazard[i];
        auto suit_it = world.layers.find("suitability_" + people);
        if (suit_it == world.layers.end()) suit_it = world.layers.find("suitability");
        suitability[i] = suit_it != world.layers.end()
                             ? suit_it->second.at(grid.point_x[i], grid.point_z[i])
                             : 0.0;
    }

    // --- the ground a college must stand clear of -------------------------------
    std::vector<Vec3> occupied;
    const auto occupy = [&](int x, int z) { occupied.push_back(direction(x, z, cfg.size)); };
    for (const SettlementSite& s : cities) occupy(s.x, s.z);
    for (const RuralSite& h : humans.hamlets) occupy(h.x, h.z);
    for (const RuralSite& f : humans.fortresses) occupy(f.x, f.z);

    // Best magic first, tempered by how much a people would want to live there; the node
    // index breaks ties so the ranking cannot depend on the order cells were enumerated.
    std::vector<std::size_t> order(count);
    std::iota(order.begin(), order.end(), std::size_t{0});
    const auto rank = [&](std::size_t i) {
        return -(density[i] * (1.0 - hazard[i]) + kSuitabilityRankWeight * suitability[i]);
    };
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const double ra = rank(a);
        const double rb = rank(b);
        if (ra != rb) return ra < rb;
        return a < b;
    });

    std::vector<Vec3> college_points;
    for (const std::size_t i : order) {
        const int o = owner[i];
        if (o < 0) continue;
        ++report.diagnostic.sources;
        const CivilizationProfile* profile = profiles[static_cast<std::size_t>(o)];
        if (profile == nullptr) continue;

        // On the full world a people is limited only by what it can itself tolerate.
        const auto refusal = college_refusal(density[i], hazard[i], profile->traits.mutation_limit,
                                             slope[i], temperature[i], freshwater[i], flood[i],
                                             suitability[i], water[i], profile->traits);
        if (refusal) {
            ++report.refusals[static_cast<std::size_t>(*refusal)];
            continue;
        }
        // Counted before the budget check, so a candidate the budget never reached still
        // counts: that is what separates "no ground" from "no budget" in the diagnostic.
        ++report.diagnostic.candidates;
        if (static_cast<int>(report.colleges.size()) >= cfg.college_count) continue;

        const int x = grid.point_x[i];
        const int z = grid.point_z[i];
        const Vec3 here = direction(x, z, cfg.size);

        bool crowded = false;
        for (const Vec3& q : occupied) {
            if (great_circle_m(cfg.globe_radius, here, q) < kSettlementClearanceM) crowded = true;
        }
        if (crowded) {
            ++report.refused_clearance;
            continue;
        }
        bool near_another = false;
        for (const Vec3& q : college_points) {
            if (great_circle_m(cfg.globe_radius, here, q) < report.spacing_m) near_another = true;
        }
        if (near_another) {
            ++report.refused_spacing;
            continue;
        }

        College college;
        college.id = "college-" + std::to_string(report.colleges.size() + 1);
        college.node = static_cast<int>(i);
        college.x = x;
        college.z = z;
        college.core_id = o;
        college.population_profile = cities[static_cast<std::size_t>(o)].population_profile;
        if (static_cast<std::size_t>(o) < humans.cores.size()) {
            college.culture_id = humans.cores[static_cast<std::size_t>(o)].culture_id;
        }
        college.density = density[i];
        college.hazard = hazard[i];
        college.suitability = suitability[i];
        college.access_cost = distance[i];
        const std::vector<int>& parent = parent_by_city[static_cast<std::size_t>(o)];
        for (int step = static_cast<int>(i); step >= 0; step = parent[static_cast<std::size_t>(step)]) {
            college.access_nodes.push_back(step);
        }

        report.colleges.push_back(std::move(college));
        occupied.push_back(here);
        college_points.push_back(here);
    }

    report.diagnostic.placed = static_cast<int>(report.colleges.size());
    report.diagnostic.reason =
        diagnostic_reason(report.diagnostic.placed, report.diagnostic.wanted, report.diagnostic.candidates,
                          report.diagnostic.sources, report.refusals, report.refused_clearance,
                          report.refused_spacing);
    return report;
}

}  // namespace fwg
