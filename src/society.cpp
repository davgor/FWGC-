#include "fwg/society.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <set>
#include <string>
#include <unordered_map>

#include "fwg/civilizations.hpp"
#include "fwg/ecology.hpp"
#include "fwg/entities.hpp"
#include "fwg/option_ids.hpp"
#include "fwg/random.hpp"
#include "fwg/routing.hpp"
#include "fwg/settlements_stage.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

constexpr double kPi = std::numbers::pi;

// --- authored thresholds ---------------------------------------------------
//
// Dimensionless unless marked. Every METRE constant below was authored against the
// 11.15 km reference world and is NOT scaled with world width — unlike
// `settlement_spacing` and `support_reach`, which the request builder scales. The world
// options this pass reads (fishing_reach, sea_reach, air_reach, sea_draft) are absolute
// metres too. docs/port-specs records which of them can still fire at shipped widths.

// A node must be at least this good an anchorage to compete for a harbour at all.
constexpr double kHarbourCandidateFloor = 0.1;
// Above this a cell is too good a settlement site to hide a witch hut on.
constexpr double kWitchHutSuitabilityCeiling = 0.55;
// Regional influence floor, for a hut and separately for the pirate trait.
constexpr double kZoneFloor = 0.02;
// METRES. Minimum hut-to-hut great-circle separation. On any world wider than the
// reference this is a fraction of one cell, so it never binds and huts are separated only
// by the settlement-spacing test against cities and by the count already on the map.
constexpr double kWitchHutSeparationM = 200.0;
// Weight on nearby fishing water in the harbour ranking. Raising it favours cells beside
// productive water over cells that are simply good anchorages.
constexpr double kCoastalSupportWeight = 0.5;
// Harbour quality at which a port can carry sea trade.
constexpr double kTradeTerminalQuality = 0.4;
// METRES. Fishing draft, deliberately BELOW the navigable sea draft: a fishing boat works
// water a trader cannot enter.
constexpr double kFishingDraftM = 0.1;
// METRES. Sea-route efficiency decay length and price scale, against a cost that is the
// route length plus both ground access costs.
constexpr double kSeaEfficiencyScaleM = 12000.0;
constexpr double kSeaPriceScaleM = 10000.0;
constexpr double kSeaPriceFloor = 0.2;
// METRES. The same pair for air, deliberately priced above sea.
constexpr double kAirEfficiencyScaleM = 8000.0;
constexpr double kAirPriceScaleM = 5000.0;
constexpr double kAirPriceFloor = 0.5;
// Weave potency that grants a city an air terminal without industry. The ONLY route by
// which a non-industrial people gets air transport.
constexpr double kAirTerminalWeave = 0.12;
// METRES. Terrain clearance above every sampled cell when choosing a cruise altitude.
// Raising it lengthens every air route, because the climb is charged at both ends.
constexpr double kAirClearanceM = 80.0;
// Degenerate-direction guard: two antipodal endpoints interpolate through the origin.
constexpr double kDegenerateDirection = 1e-8;
constexpr int kMinimumAirSamples = 2;
constexpr double kShortageEpsilon = 1e-8;

// make_sky's own constants. Dead on every world the generator can build.
constexpr double kSkyLeyFloor = 0.06;
constexpr double kSkyClusterSeparationRad = 0.25;
constexpr double kSkyLapseRateCPerM = 0.0065;
constexpr double kSkyRainCapture = 160.0;      // residents-equivalent per km2 per unit moisture
constexpr double kSkyFoodPerKm2 = 80.0;
constexpr double kSkyFoodOptimumC = 16.0;
constexpr double kSkyFoodHalfWidthC = 35.0;
constexpr double kSkyFoodOverWater = 1.5;
constexpr long long kSkyMinimumPopulation = 4;
constexpr int kSkyRings = 4;
constexpr int kSkySectors = 24;
constexpr double kSkyReliefFraction = 0.08;
constexpr double kSkySpoilage = 0.02;
constexpr double kSkyStorageMonths = 4.0;
constexpr double kSkyTransportPerKm2Year = 40.0;

std::vector<double> nodes_of(const World& world, const SphereGrid& grid, const std::string& layer,
                             double fallback = 0.0) {
    std::vector<double> out(grid.node_count(), fallback);
    const auto it = world.layers.find(layer);
    if (it == world.layers.end()) return out;
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = it->second.at(grid.point_x[i], grid.point_z[i]);
    return out;
}

double layer_at(const World& world, const char* name, int x, int z) {
    const auto it = world.layers.find(name);
    return it != world.layers.end() ? it->second.at(x, z) : 0.0;
}

double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }

double angle_between(const Vec3& a, const Vec3& b) { return std::acos(clamp_unit(dot(a, b))); }

const CivilizationProfile& profile_or_default(std::string_view id) {
    if (const CivilizationProfile* p = civilization_profile(id)) return *p;
    return *civilization_profile(default_profile_id());
}

double mutation_limit_of(std::string_view id) { return profile_or_default(id).traits.mutation_limit; }

const EconomyRules& economy_of(std::string_view id) {
    static const EconomyRules kNeutral{};
    const EntityRules* rules = entity_rule(id);
    return rules != nullptr ? rules->economy : kNeutral;
}

// The node list from `start` to `end` inclusive, walked back up a parent tree. A broken
// chain returns nothing rather than a path that does not exist.
std::vector<int> trace(const std::vector<int>& parent, int start, int end) {
    std::vector<int> path{end};
    while (path.back() != start) {
        const int j = parent[static_cast<std::size_t>(path.back())];
        if (j < 0) return {};
        path.push_back(j);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

// Monthly sea ice at one cell, or open water where the seasonal months were never built.
double ice_at(const World& world, std::size_t month, int x, int z) {
    if (month >= world.seasonal_months.size()) return 0.0;
    const Grid& field = world.seasonal_months[month].water_ice;
    if (field.empty()) return 0.0;
    return field.at(x, z);
}

// An insertion-ordered map. Python's dict keeps the position of the FIRST write even when
// a later, cheaper value replaces it, and both the fishery claims and the sea pairs are
// read back in that order.
template <typename Key, typename Value>
class OrderedMap {
public:
    Value* find(const Key& key) {
        const auto slot = index_.find(key);
        return slot == index_.end() ? nullptr : &values_[slot->second].second;
    }
    void set(const Key& key, Value value) {
        const auto slot = index_.find(key);
        if (slot != index_.end()) {
            values_[slot->second].second = std::move(value);
            return;
        }
        index_.emplace(key, values_.size());
        values_.emplace_back(key, std::move(value));
    }
    const std::vector<std::pair<Key, Value>>& entries() const { return values_; }

private:
    std::map<Key, std::size_t> index_;
    std::vector<std::pair<Key, Value>> values_;
};

}  // namespace

// ---------------------------------------------------------------------------

double fishing_access_factor(double ice, double winter_fraction) {
    const double covered = clamp01(ice);
    const double retained = clamp01(winter_fraction);
    return 1.0 - covered + covered * retained;
}

std::optional<double> WaterCost::operator()(int i, int j, double distance) const {
    const auto ui = static_cast<std::size_t>(i);
    const auto uj = static_cast<std::size_t>(j);
    const std::vector<double>& water = *water_;
    const std::vector<double>& depth = *depth_;
    const std::vector<double>& hazard = *hazard_;

    // The three-comparison passability test is written out here rather than called: the
    // routing search runs this several million times per world and the call overhead
    // dominated the three comparisons. Ocean only — water_type 2 is a lake.
    if (!(water[ui] == 1.0 && depth[ui] >= draft_ && hazard[ui] <= limit_)) return std::nullopt;
    if (!(water[uj] == 1.0 && depth[uj] >= draft_ && hazard[uj] <= limit_)) return std::nullopt;

    const int n = grid_->n;
    const int x = grid_->point_x[ui];
    const int z = grid_->point_z[ui];
    const int xx = grid_->point_x[uj];
    const int zz = grid_->point_z[uj];
    // A diagonal step may not slip between two impassable corner cells. The pole rows are
    // exempt because a pole is one node adjacent to a whole ring and has no corners.
    if (x != xx && z != zz && z != 0 && z != n - 1 && zz != 0 && zz != n - 1) {
        const auto a = static_cast<std::size_t>(grid_->node_index(x, zz));
        if (!(water[a] == 1.0 && depth[a] >= draft_ && hazard[a] <= limit_)) return std::nullopt;
        const auto b = static_cast<std::size_t>(grid_->node_index(xx, z));
        if (!(water[b] == 1.0 && depth[b] >= draft_ && hazard[b] <= limit_)) return std::nullopt;
    }
    return distance;
}

namespace {

// --- the sky pass ----------------------------------------------------------

struct SkyResult {
    std::vector<SkyIsland> islands;
    std::vector<SkySettlement> settlements;
    std::vector<FoodModel> models;
};

SkyResult make_sky(const World& world, const Config& cfg) {
    SkyResult out;
    // Recipe 3 is the only recipe the pipeline ships, so this is where every live world
    // leaves. The rest is ported because recipe 0 exists, not because it runs.
    if (cfg.world_recipe == 3) return out;
    if (cfg.magic_enabled == 0) return out;

    const int n = cfg.size;
    const auto held = sphere_grid(n, cfg.globe_radius);
    const SphereGrid& grid = *held;
    const int clusters = cfg.options.as_int(Option::sky_clusters);
    const double occurrence = cfg.options[Option::sky_occurrence];
    const double seasonality = cfg.options[Option::seasonality];
    const double accumulation = cfg.options[Option::ice_accumulation];

    Rng rng = Rng::child(cfg.seed, "sky-archipelagos-v1");
    std::vector<std::pair<int, int>> candidates;
    for (std::size_t i = 0; i < grid.node_count(); ++i) {
        const int x = grid.point_x[i];
        const int z = grid.point_z[i];
        const double ley = std::max(layer_at(world, "ley_weave", x, z), layer_at(world, "ley_primordial", x, z));
        if (ley > kSkyLeyFloor) candidates.emplace_back(x, z);
    }
    rng.shuffle(candidates);

    std::vector<Vec3> centers;
    for (int k = 0; k < clusters; ++k) {
        if (candidates.empty()) break;
        const std::pair<int, int>* center = nullptr;
        for (const auto& cell : candidates) {
            const Vec3 p = direction(cell.first, cell.second, n);
            bool clear = true;
            for (const Vec3& c : centers) {
                if (angle_between(p, c) <= kSkyClusterSeparationRad) clear = false;
            }
            if (clear) {
                center = &cell;
                break;
            }
        }
        if (center == nullptr) break;
        const int cx = center->first;
        const int cz = center->second;
        // The centre is recorded BEFORE the occurrence roll, so a cluster that does not
        // manifest still holds its ground against the next one.
        centers.push_back(direction(cx, cz, n));
        if (rng.random() > occurrence) continue;

        for (int j = 0; j < 3; ++j) {
            const int x = ((cx + (j - 1) * 2) % (n - 1) + (n - 1)) % (n - 1);
            const int z = std::max(1, std::min(n - 2, cz + static_cast<int>(rng.randint(-2, 2))));
            const double radius = cfg.options[Option::sky_radius] * rng.uniform(0.7, 1.3);
            const double ground = layer_at(world, "height", x, z);
            const double altitude = std::max(ground, layer_at(world, "water_surface", x, z)) +
                                    cfg.options[Option::sky_altitude] * rng.uniform(0.8, 1.2);
            // Dry adiabatic lapse: the island is colder than the ground it floats over.
            const double temp = layer_at(world, "temperature", x, z) - kSkyLapseRateCPerM * (altitude - ground);
            const double wet = layer_at(world, "moisture", x, z);
            const double latitude = 90.0 - 180.0 * z / (n - 1);
            const std::vector<double> temps = monthly_temperatures(temp, latitude, wet, seasonality);
            const std::string cold = cold_habitat(temps, wet, accumulation);
            const double area = kPi * radius * radius / 1e6;

            // Provisional rain-capture residents-equivalent budget.
            const double freshwater = area * wet * kSkyRainCapture;
            const double food = area * kSkyFoodPerKm2 *
                                clamp01(1.0 - std::abs(temp - kSkyFoodOptimumC) / kSkyFoodHalfWidthC) * wet *
                                (cold == "ice_cap" ? 0.0 : 1.0);
            // Food may support half again as many residents as water before water binds.
            const auto potential =
                static_cast<long long>(std::floor(std::min(freshwater, food * kSkyFoodOverWater)));

            HabitatSample sky_environment;
            sky_environment.biome = (wet >= 0.7 && temp > 20.0) ? 7.0 : 3.0;
            sky_environment.temperature = temp;
            sky_environment.moisture = wet;
            sky_environment.maritime = 0.0;
            sky_environment.landmass_area_m2 = area * 1e6;
            sky_environment.landmass_fraction = 1.0;
            sky_environment.largest_landmass = 1.0;

            std::vector<std::string_view> eligible = eligible_civilizations(sky_environment);
            std::vector<std::string_view> people;
            for (const std::string_view id : eligible) {
                if (cfg.population_profile == "mixed" || id == cfg.population_profile) people.push_back(id);
            }

            Rng people_rng = Rng::child(
                cfg.seed, "sky-population-" + std::to_string(k) + "-" + std::to_string(j) + "-v1");
            std::string_view chosen;
            double best = 0.0;
            for (const std::string_view id : people) {
                const SkyRules sky = entity_rule(id) != nullptr ? entity_rule(id)->sky : SkyRules{};
                const double score = -std::abs(profile_or_default(id).traits.temperature_ideal - temp) +
                                     people_rng.uniform(0.0, 8.0) + sky.score_bias +
                                     sky.moisture_score_weight * wet;
                // The FIRST maximum keeps the slot, so the registry's order breaks a tie.
                if (chosen.empty() || score > best) {
                    chosen = id;
                    best = score;
                }
            }

            double hazard_here = 0.0;
            if (!chosen.empty()) {
                const std::string risk = "magic_risk_" + std::string(chosen);
                hazard_here = world.layers.count(risk) != 0 ? layer_at(world, risk.c_str(), x, z)
                                                            : layer_at(world, "magic_hazard", x, z);
            }
            const long long population =
                (!chosen.empty() && hazard_here <= mutation_limit_of(chosen)) ? potential : 0;

            const std::string island_id = "sky-" + std::to_string(k) + "-" + std::to_string(j);
            SkyIsland island;
            island.id = island_id;
            island.cluster_id = "sky-" + std::to_string(k);
            island.layer = island_id;
            island.x = x;
            island.z = z;
            island.direction = direction(x, z, n);
            island.radius_m = radius;
            island.altitude_m = altitude;
            island.area_km2 = area;
            island.temperature_c = temp;
            island.moisture = wet;
            island.cold_habitat = cold;
            island.food_potential = food;
            island.freshwater_capacity = freshwater;
            island.magic_support =
                std::max(layer_at(world, "ley_weave", x, z), layer_at(world, "ley_primordial", x, z));
            for (std::size_t m = 0; m < 12 && m < temps.size(); ++m) {
                island.months[m].temperature = temps[m];
                island.months[m].snow = clamp01(-temps[m] / 8.0) * clamp01(wet * 2.0);
            }

            // A small independent disk, never a displacement of the ground below.
            island.mesh.vertices.push_back({0.0, altitude, 0.0});
            for (int ring = 1; ring <= kSkyRings; ++ring) {
                for (int a = 0; a < kSkySectors; ++a) {
                    const double theta = 2.0 * kPi * a / kSkySectors;
                    const double rr = radius * ring / kSkyRings;
                    const double relief = radius * kSkyReliefFraction * std::sin(theta * 3.0 + j) *
                                          std::sin(kPi * ring / kSkyRings);
                    island.mesh.vertices.push_back(
                        {rr * std::cos(theta), altitude + relief, rr * std::sin(theta)});
                }
            }
            for (int a = 0; a < kSkySectors; ++a) {
                island.mesh.triangles.push_back({0, 1 + a, 1 + (a + 1) % kSkySectors});
            }
            for (int ring = 0; ring < kSkyRings - 1; ++ring) {
                for (int a = 0; a < kSkySectors; ++a) {
                    const int a0 = 1 + ring * kSkySectors + a;
                    const int a1 = 1 + ring * kSkySectors + (a + 1) % kSkySectors;
                    island.mesh.triangles.push_back({a0, a0 + kSkySectors, a1 + kSkySectors});
                    island.mesh.triangles.push_back({a0, a1 + kSkySectors, a1});
                }
            }
            out.islands.push_back(std::move(island));
            if (population < kSkyMinimumPopulation) continue;

            const CivilizationProfile& p = profile_or_default(chosen);
            SkySettlement settlement;
            settlement.id = island_id + "-settlement";
            settlement.layer = island_id;
            settlement.kind = "sky_settlement";
            settlement.population_profile = std::string(chosen);
            settlement.civilization_id = settlement.population_profile;
            settlement.x = x;
            settlement.z = z;
            settlement.direction = direction(x, z, n);
            settlement.altitude_m = altitude;
            settlement.population_estimate = population;
            settlement.name = std::string(p.name) + " Skyhaven " + std::to_string(k + 1) + "." +
                              std::to_string(j + 1);
            settlement.freshwater_capacity = freshwater;
            settlement.reason =
                "Independent rain capture, growing climate and magical tolerance support this island "
                "community.";
            out.settlements.push_back(std::move(settlement));

            FoodModel model;
            model.sky_id = island_id + "-settlement";
            model.harvest = seasonal_harvest(food, wet, latitude, temp, p.traits.food_temperature_tolerance,
                                             p.traits.food_temperature_ideal, seasonality);
            model.demand = static_cast<double>(population) * p.traits.food_demand / 100.0 / 12.0;
            model.storage = model.demand * kSkyStorageMonths;
            model.spoilage = kSkySpoilage;
            model.transport_budget = area * kSkyTransportPerKm2Year / 12.0;
            out.models.push_back(std::move(model));
        }
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------

SocietyReport add_world_society(const SocietyRequest& request) {
    SocietyReport out;
    if (request.world == nullptr) return out;
    const World& world = *request.world;
    const Config& cfg = world.effective;
    // The first gate writes nothing at all, not even the sky block.
    if (cfg.phase < 6 || !world.has_ecology) return out;

    const SkyResult sky = make_sky(world, cfg);
    out.ran = true;
    out.sky.islands = sky.islands;
    out.sky.settlements = cfg.phase >= 7 ? sky.settlements : std::vector<SkySettlement>{};
    for (const SkyIsland& island : sky.islands) out.sky.area_km2 += island.area_km2;
    out.sky.enabled = cfg.world_recipe != 3;
    out.sky.disabled_reason = cfg.world_recipe == 3 ? "Sky islands suspended in recipe 3" : "";
    out.sky.method =
        "Separate disk surfaces with rain capture, altitude climate and bounded food; static magical "
        "support.";

    if (cfg.phase < 9 || request.humans == nullptr || request.seasonal == nullptr) return out;
    const HumanHinterland& humans = *request.humans;
    const SeasonalFood& seasonal = *request.seasonal;
    if (!world.has_roads || humans.cores.empty()) return out;

    const int n = cfg.size;
    const auto held = sphere_grid(n, cfg.globe_radius);
    const SphereGrid& grid = *held;
    const std::size_t count = grid.node_count();
    const double radius = cfg.globe_radius;
    const std::vector<SettlementSite>& sites = world.settlements.sites;

    const std::vector<double> water = nodes_of(world, grid, "water_type");
    const std::vector<double> depth = nodes_of(world, grid, "water_depth");
    const std::vector<double> height = nodes_of(world, grid, "height");
    const std::vector<double> river = nodes_of(world, grid, "rain_river");
    const std::vector<double> harbor = nodes_of(world, grid, "harbor_suitability");
    const std::vector<double> fish = nodes_of(world, grid, "fishing_productivity");
    const std::vector<double> hazard = nodes_of(world, grid, "magic_hazard");

    const FreshwaterAccess water_access = freshwater_access(world, grid);
    const std::vector<double> flood = flood_risk(world, grid, water_access);

    std::map<std::string, std::vector<double>> risk_by_people;
    const auto risk_for = [&](const std::string& people) -> const std::vector<double>& {
        const auto cached = risk_by_people.find(people);
        if (cached != risk_by_people.end()) return cached->second;
        const std::string layer = "magic_risk_" + people;
        std::vector<double> field = world.layers.count(layer) != 0 ? nodes_of(world, grid, layer) : hazard;
        return risk_by_people.emplace(people, std::move(field)).first->second;
    };

    std::vector<FoodModel> models = seasonal.models;  // copied; the seasonal block is not ours

    // --- witch huts --------------------------------------------------------
    // Huts follow their own preference for isolation and poor conventional suitability.
    const Grid* suitability = request.suitability;
    Grid resolved_suitability;
    if (suitability == nullptr) {
        const auto named = world.layers.find("suitability_" + cfg.population_profile);
        if (named != world.layers.end()) {
            suitability = &named->second;
        } else {
            resolved_suitability = Grid(n, 0.0);
            suitability = &resolved_suitability;
        }
    }

    int wanted = 0;
    for (const Landmark& mark : world.ecology.landmarks) {
        if (mark.kind == "witch_hut") ++wanted;
    }
    std::vector<int> by_influence(count);
    for (std::size_t i = 0; i < count; ++i) by_influence[i] = static_cast<int>(i);
    const auto zone_at = [&](int node, const char* zone) {
        const auto ui = static_cast<std::size_t>(node);
        return layer_at(world, zone, grid.point_x[ui], grid.point_z[ui]);
    };
    // Stable, so a tie keeps ascending node order. One full sort even when nothing is
    // wanted, which is what the reference pays too.
    std::stable_sort(by_influence.begin(), by_influence.end(), [&](int a, int b) {
        return zone_at(a, "zone_witch_huts") > zone_at(b, "zone_witch_huts");
    });

    std::vector<int> hut_nodes;
    for (const int i : by_influence) {
        if (static_cast<int>(hut_nodes.size()) >= wanted) break;
        const auto ui = static_cast<std::size_t>(i);
        const int x = grid.point_x[ui];
        const int z = grid.point_z[ui];
        if (water[ui] != 0.0) continue;  // rejects ocean and lake alike
        const double conventional = suitability->at(x, z);
        if (conventional >= kWitchHutSuitabilityCeiling) continue;
        const double intensity = layer_at(world, "zone_witch_huts", x, z);
        if (intensity < kZoneFloor) continue;

        const Vec3 p = direction(x, z, n);
        bool clear = true;
        for (const SettlementSite& site : sites) {
            if (radius * angle_between(p, direction(site.x, site.z, n)) < cfg.settlement_spacing) clear = false;
        }
        if (!clear) continue;
        for (const int j : hut_nodes) {
            const auto uj = static_cast<std::size_t>(j);
            if (radius * angle_between(p, direction(grid.point_x[uj], grid.point_z[uj], n)) <
                kWitchHutSeparationM) {
                clear = false;
            }
        }
        if (!clear) continue;

        hut_nodes.push_back(i);
        SocietyWitchHut hut;
        hut.id = "witch-hut-" + std::to_string(i);
        hut.kind = "witch_hut";
        hut.x = x;
        hut.z = z;
        hut.layer = "surface";
        hut.intensity = intensity;
        hut.conventional_suitability = conventional;
        hut.reason = "Umbral influence, isolation and low conventional settlement suitability.";
        out.witch_huts.push_back(std::move(hut));
    }

    // --- coastal harbours --------------------------------------------------
    // Fortresses belong in this set as much as cities and hamlets do. Leaving them out let
    // a harbour be founded on a node a fort already held — 28 of 56 forts in one reference
    // world shared their exact node with a coastal hamlet, which draws as one icon on top
    // of another and means two settlements occupy one place.
    std::set<int> occupied;
    for (const SettlementSite& site : sites) occupied.insert(site.node);
    for (const RuralSite& hamlet : humans.hamlets) occupied.insert(hamlet.node);
    for (const RuralSite& fort : humans.fortresses) occupied.insert(fort.node);

    std::vector<int> ground_candidates;
    for (std::size_t i = 0; i < count; ++i) {
        if (harbor[i] > kHarbourCandidateFloor) ground_candidates.push_back(static_cast<int>(i));
    }

    const int coastal_hamlets = cfg.options.as_int(Option::coastal_hamlets);
    const double sea_draft = cfg.options[Option::sea_draft];
    std::vector<HarbourPort> ports;
    out.community_traits.assign(sites.size(), {});

    for (const SettlementSite& site : sites) {
        const CivilizationProfile& p = profile_or_default(site.population_profile);
        const std::vector<double>& risk = risk_for(site.population_profile);
        Config theirs = cfg;
        theirs.human_magic_limit = p.traits.mutation_limit;
        theirs.road_max_grade = p.traits.road_grade_limit;
        const RoadCost cost(grid, theirs, water, height, flood, river, &risk);

        // Only landings inside the support reach are ever eligible, so the search stops
        // there rather than crossing the whole landmass to price candidates it discards.
        const PathResult routes =
            shortest_paths(grid, site.node, cost, &ground_candidates, cfg.support_reach);

        std::vector<int> eligible;
        for (const int i : ground_candidates) {
            if (routes.distance[static_cast<std::size_t>(i)] > cfg.support_reach) continue;
            if (occupied.count(i) != 0) continue;
            eligible.push_back(i);
        }
        std::stable_sort(eligible.begin(), eligible.end(), [&](int a, int b) {
            const auto ua = static_cast<std::size_t>(a);
            const auto ub = static_cast<std::size_t>(b);
            const double sa = (harbor[ua] + kCoastalSupportWeight *
                                                layer_at(world, "coastal_support", grid.point_x[ua],
                                                         grid.point_z[ua])) *
                              std::exp(-routes.distance[ua] / cfg.support_reach);
            const double sb = (harbor[ub] + kCoastalSupportWeight *
                                                layer_at(world, "coastal_support", grid.point_x[ub],
                                                         grid.point_z[ub])) *
                              std::exp(-routes.distance[ub] / cfg.support_reach);
            return sa > sb;
        });

        const std::size_t take = std::min(eligible.size(), static_cast<std::size_t>(std::max(0, coastal_hamlets)));
        for (std::size_t slot = 0; slot < take; ++slot) {
            const int node = eligible[slot];
            // A harbour needs water it can actually launch into. Without one the slot is
            // consumed and no port results — the reference does not fall through to the
            // next candidate.
            int sea_node = -1;
            double landing = 0.0;
            for (const SphereNeighbour& nb : grid.neighbours[static_cast<std::size_t>(node)]) {
                const auto uj = static_cast<std::size_t>(nb.index);
                if (water[uj] != 1.0 || depth[uj] < sea_draft || risk[uj] > p.traits.mutation_limit) continue;
                if (sea_node < 0 || nb.distance < landing) {
                    sea_node = nb.index;
                    landing = nb.distance;
                }
            }
            if (sea_node < 0) continue;
            occupied.insert(node);

            const auto un = static_cast<std::size_t>(node);
            HarbourPort port;
            port.id = "coastal-" + std::to_string(site.id) + "-" + std::to_string(ports.size());
            port.kind = "hamlet";
            port.layer = "surface";
            port.node = node;
            port.sea_node = sea_node;
            port.landing_distance_m = landing;
            port.x = grid.point_x[un];
            port.z = grid.point_z[un];
            port.core_id = site.id;
            port.population_profile = site.population_profile;
            if (static_cast<std::size_t>(site.id) < humans.cores.size()) {
                port.culture_id = humans.cores[static_cast<std::size_t>(site.id)].culture_id;
            }
            port.height_m = height[un];
            port.harbor_quality = harbor[un];
            port.trade_terminal = harbor[un] >= kTradeTerminalQuality;
            port.access_nodes = trace(routes.parent, site.node, node);
            port.access_cost = routes.distance[un];
            port.reason =
                "Safe ground connection to city; navigable landing; fishing allocated from exclusive "
                "grounds.";
            port.winter_fishing_fraction = economy_of(site.population_profile).winter_fishing_fraction;
            ports.push_back(std::move(port));
        }

        bool has_port = false;
        for (const HarbourPort& port : ports) {
            if (port.core_id == site.id) has_port = true;
        }
        std::vector<std::string>& traits = out.community_traits[static_cast<std::size_t>(site.id)];
        if (has_port) {
            traits.push_back(layer_at(world, "island_habitat", site.x, site.z) != 0.0 ? "islander" : "maritime");
        }
        const std::optional<double>& industrial = economy_of(site.population_profile).industrial_resource_min;
        // ABSENT MEANS NEVER, not zero: a minimum of zero would make everyone industrial.
        if (industrial && site.resource_potential > *industrial) traits.push_back("industrial");
    }

    // --- exclusive fisheries ------------------------------------------------
    // Each port searches only accessible water. Globally award each cell once.
    const double fishing_reach = cfg.options[Option::fishing_reach];
    OrderedMap<int, std::pair<double, int>> claims;
    std::vector<std::map<int, std::array<double, 12>>> fishing_ice(ports.size());

    for (std::size_t k = 0; k < ports.size(); ++k) {
        const HarbourPort& port = ports[k];
        const CivilizationProfile& p = profile_or_default(port.population_profile);
        const double reach = fishing_reach * economy_of(port.population_profile).fishing_reach_multiplier;
        const std::vector<double>& risk = risk_for(port.population_profile);
        // NOTE THE DRAFT: fishing uses its own, below the navigable one.
        const WaterCost cost(grid, water, depth, risk, p.traits.mutation_limit, kFishingDraftM);
        const AccessResult reached =
            allocate_access(grid, {{port.sea_node, 0}}, cost, reach);

        std::vector<int> inside;
        for (std::size_t i = 0; i < count; ++i) {
            if (reached.distance[i] <= reach) inside.push_back(static_cast<int>(i));
        }
        std::stable_sort(inside.begin(), inside.end(), [&reached](int a, int b) {
            return reached.distance[static_cast<std::size_t>(a)] < reached.distance[static_cast<std::size_t>(b)];
        });

        // A running MAXIMUM of monthly ice along the allocation tree: a route through a
        // frozen strait is frozen that month even where the fishing ground itself is open.
        // Parent before child is guaranteed because every edge cost is positive, so tree
        // distances strictly increase.
        std::map<int, std::array<double, 12>>& path_ice = fishing_ice[k];
        for (const int i : inside) {
            const auto ui = static_cast<std::size_t>(i);
            const int up = reached.parent[ui];
            std::array<double, 12> upstream{};
            const auto found = path_ice.find(up);
            if (found != path_ice.end()) upstream = found->second;
            std::array<double, 12> here{};
            for (std::size_t m = 0; m < 12; ++m) {
                here[m] = std::max(upstream[m], ice_at(world, m, grid.point_x[ui], grid.point_z[ui]));
            }
            path_ice.emplace(i, here);
        }

        for (const int i : inside) {
            const double d = reached.distance[static_cast<std::size_t>(i)];
            const std::pair<double, int> candidate{d, static_cast<int>(k)};
            const std::pair<double, int>* held_claim = claims.find(i);
            // Nearer wins; on an exact tie the LOWER port index does, which is the port of
            // the earlier-founded city.
            if (held_claim == nullptr || candidate < *held_claim) claims.set(i, candidate);
        }
    }

    std::vector<double> owner(count, -1.0);
    double production = 0.0;
    for (const auto& [node, claim] : claims.entries()) {
        const auto ui = static_cast<std::size_t>(node);
        const auto k = static_cast<std::size_t>(claim.second);
        HarbourPort& port = ports[k];
        owner[ui] = static_cast<double>(k);
        port.fishing_nodes.push_back(node);

        const double area = grid.areas[ui] / 1e6;
        const double annual = area * fish[ui] * cfg.options[Option::fish_productivity];
        production += annual;
        const double delivery =
            std::exp(-(claim.first + port.access_cost) / std::max(1.0, cfg.support_reach));
        port.worked_area_km2 += area;
        const std::array<double, 12>& ice = fishing_ice[k].at(node);
        for (std::size_t m = 0; m < 12; ++m) {
            port.monthly_fish[m] +=
                annual / 12.0 * delivery * fishing_access_factor(ice[m], port.winter_fishing_fraction);
        }
    }

    for (HarbourPort& port : ports) {
        port.delivered_food = 0.0;
        for (const double month : port.monthly_fish) port.delivered_food += month;
        port.role = port.trade_terminal ? (port.delivered_food > 0.0 ? "harbor + fishing" : "harbor")
                                        : (port.delivered_food > 0.0 ? "fishing" : "landing");
        const auto core = static_cast<std::size_t>(port.core_id);
        if (core < models.size()) {
            for (std::size_t m = 0; m < 12; ++m) models[core].harvest[m] += port.monthly_fish[m];
        }
    }

    out.fishing_ground_owner = grid.to_grid(owner);
    out.fisheries.ports = ports;
    out.fisheries.potential_annual_food = production;
    for (const HarbourPort& port : ports) out.fisheries.delivered_annual_food += port.delivered_food;
    out.fisheries.method =
        "One owner per reachable ocean cell; distance losses and monthly ice reduce delivery; "
        "civilization winter fishing retains a bounded fraction under ice. Potential capacity is never "
        "credited before allocation.";

    // --- ground routes ------------------------------------------------------
    std::vector<TransportRoute>& routes = out.transport.routes;
    for (std::size_t i = 0; i < world.roads.routes.size() && i < seasonal.routes.size(); ++i) {
        TransportRoute route;
        route.food = seasonal.routes[i];
        route.mode = "ground";
        route.nodes = world.roads.routes[i].nodes;
        route.length_m = world.roads.routes[i].length_m;
        routes.push_back(std::move(route));
    }

    // --- sea routes ---------------------------------------------------------
    // A route is priced pair by pair, but the search behind it depends only on where it
    // starts and which mutation limit it respects, and nothing past the sea reach can
    // survive the length test — a landing distance is never negative. So the search runs
    // once per (origin, limit), bounded, and the pair loop reads it. `sea_costs` outlives
    // the outer port because a cost closure is reusable across origins; `searches` does
    // not, because a distance array is not.
    const double sea_reach = cfg.options[Option::sea_reach];
    const double sea_capacity = cfg.options[Option::sea_capacity];
    OrderedMap<std::pair<int, int>, TransportRoute> sea_pairs;
    std::map<double, WaterCost> sea_costs;

    for (std::size_t a = 0; a < ports.size(); ++a) {
        const HarbourPort& port = ports[a];
        std::map<double, PathResult> searches;
        for (std::size_t b = a + 1; b < ports.size(); ++b) {
            const HarbourPort& other = ports[b];
            if (!port.trade_terminal || !other.trade_terminal) continue;
            // Two ports of the same city never route to each other.
            if (port.core_id == other.core_id) continue;

            const double limit = std::min(mutation_limit_of(port.population_profile),
                                          mutation_limit_of(other.population_profile));
            if (searches.find(limit) == searches.end()) {
                const auto cached = sea_costs.find(limit);
                if (cached == sea_costs.end()) {
                    // The sea pass uses the GENERIC hazard field, not the per-civilization
                    // one that steps 3 and 4 use. Today they hold identical data.
                    sea_costs.emplace(limit, WaterCost(grid, water, depth, hazard, limit, sea_draft));
                }
                searches.emplace(limit, shortest_paths(grid, port.sea_node, sea_costs.at(limit), nullptr,
                                                       sea_reach));
            }
            const PathResult& routed = searches.at(limit);

            const double length = routed.distance[static_cast<std::size_t>(other.sea_node)] +
                                  port.landing_distance_m + other.landing_distance_m;
            if (!(length <= sea_reach)) continue;  // also catches an unreached sea node
            const std::vector<int> path = trace(routed.parent, port.sea_node, other.sea_node);
            const std::pair<int, int> pair_key{std::min(port.core_id, other.core_id),
                                               std::max(port.core_id, other.core_id)};
            const double value = length + port.access_cost + other.access_cost;
            // One route per CITY pair. The cheapest pair of ports wins, and a strict <=
            // means the first writer keeps the slot on an exact tie.
            const TransportRoute* existing = sea_pairs.find(pair_key);
            if (existing != nullptr && existing->cost <= value) continue;

            TransportRoute route;
            route.food.from = pair_key.first;
            route.food.to = pair_key.second;
            for (std::size_t m = 0; m < 12; ++m) {
                double ice = 0.0;
                for (const int node : path) {
                    const auto un = static_cast<std::size_t>(node);
                    ice = std::max(ice, ice_at(world, m, grid.point_x[un], grid.point_z[un]));
                }
                route.food.capacity[m] =
                    sea_capacity * std::min(port.harbor_quality, other.harbor_quality) * (1.0 - ice);
            }
            route.food.efficiency = std::exp(-value / kSeaEfficiencyScaleM);
            route.food.price = kSeaPriceFloor + value / kSeaPriceScaleM;
            route.mode = "sea";
            route.nodes = path;
            route.length_m = length;
            route.cost = value;
            route.port_a = port.id;
            route.port_b = other.id;
            sea_pairs.set(pair_key, std::move(route));
        }
    }
    for (const auto& entry : sea_pairs.entries()) routes.push_back(entry.second);

    // --- air routes ---------------------------------------------------------
    // Air terminals require industry or stable Weave access; a sky settlement has a
    // landing surface by construction.
    const std::size_t surface_count = sites.size();
    for (const FoodModel& model : sky.models) models.push_back(model);

    const auto site_direction = [&](std::size_t i) {
        return i < surface_count ? direction(sites[i].x, sites[i].z, n)
                                 : sky.settlements[i - surface_count].direction;
    };
    const auto site_profile_id = [&](std::size_t i) -> std::string {
        return i < surface_count ? sites[i].population_profile
                                 : sky.settlements[i - surface_count].population_profile;
    };
    const auto site_altitude = [&](std::size_t i) {
        return i < surface_count ? sites[i].height_m : sky.settlements[i - surface_count].altitude_m;
    };
    const std::size_t all_count = surface_count + sky.settlements.size();

    std::vector<int>& terminals = out.transport.terminals;
    for (std::size_t i = 0; i < all_count; ++i) {
        const bool is_sky = i >= surface_count;
        if (is_sky) {
            terminals.push_back(static_cast<int>(i));
            continue;
        }
        const std::optional<double>& air_min = economy_of(sites[i].population_profile).air_terminal_resource_min;
        const bool industrial = air_min && sites[i].resource_potential > *air_min;
        const bool weave = layer_at(world, "ley_weave", sites[i].x, sites[i].z) > kAirTerminalWeave;
        if (industrial || weave) terminals.push_back(static_cast<int>(i));
    }

    const double air_reach = cfg.options[Option::air_reach];
    const double air_capacity = cfg.options[Option::air_capacity];
    for (std::size_t index = 0; index < terminals.size(); ++index) {
        for (std::size_t other = index + 1; other < terminals.size(); ++other) {
            const auto a = static_cast<std::size_t>(terminals[index]);
            const auto b = static_cast<std::size_t>(terminals[other]);
            const Vec3 va = site_direction(a);
            const Vec3 vb = site_direction(b);
            const double altitude_a = site_altitude(a);
            const double altitude_b = site_altitude(b);
            double length = std::hypot(radius * angle_between(va, vb), altitude_b - altitude_a);
            if (length > air_reach) continue;

            const double limit =
                std::min(mutation_limit_of(site_profile_id(a)), mutation_limit_of(site_profile_id(b)));
            const std::string risk_a = "magic_risk_" + site_profile_id(a);
            const std::string risk_b = "magic_risk_" + site_profile_id(b);

            bool safe = true;
            std::vector<std::pair<int, int>> path;
            const int steps = std::max(kMinimumAirSamples,
                                       static_cast<int>(std::ceil(length / std::max(world.spacing_m, 1.0))));
            for (int j = 0; j <= steps && safe; ++j) {
                const double t = static_cast<double>(j) / steps;
                // Normalised linear interpolation, NOT constant-speed slerp: samples bunch
                // toward the midpoint on a long arc. That is what the reference does.
                Vec3 p = va * (1.0 - t) + vb * t;
                const double norm = std::sqrt(dot(p, p));
                if (norm < kDegenerateDirection) {  // antipodal endpoints
                    safe = false;
                    break;
                }
                p = p * (1.0 / norm);
                const int x = std::max(0, std::min(n - 1, static_cast<int>(std::lround(
                                                              (std::atan2(p.z, p.x) + kPi) / (2.0 * kPi) *
                                                              (n - 1)))));
                const int z = std::max(0, std::min(n - 1, static_cast<int>(std::lround(
                                                              std::acos(clamp_unit(p.y)) / kPi * (n - 1)))));
                // The magic test is evaluated at the SAMPLE cell, not at the endpoints,
                // and takes the worse of the two peoples' views of it.
                const double seen = std::max(
                    world.layers.count(risk_a) != 0 ? layer_at(world, risk_a.c_str(), x, z)
                                                    : layer_at(world, "magic_hazard", x, z),
                    world.layers.count(risk_b) != 0 ? layer_at(world, risk_b.c_str(), x, z)
                                                    : layer_at(world, "magic_hazard", x, z));
                if (seen > limit) {
                    safe = false;
                    break;
                }
                path.emplace_back(x, z);
            }

            double cruise = std::max(altitude_a, altitude_b);
            for (const auto& [x, z] : path) {
                cruise = std::max(cruise, layer_at(world, "height", x, z) + kAirClearanceM);
            }
            // Recomputed once the cruise altitude is known: the horizontal great circle
            // plus the climb at each end, so the reach test runs twice against two
            // different lengths.
            length = radius * angle_between(va, vb) + std::max(0.0, cruise - altitude_a) +
                     std::max(0.0, cruise - altitude_b);
            if (!safe || length > air_reach) continue;

            TransportRoute route;
            route.food.from = static_cast<int>(a);
            route.food.to = static_cast<int>(b);
            route.food.capacity.fill(air_capacity);
            route.food.efficiency = std::exp(-length / kAirEfficiencyScaleM);
            route.food.price = kAirPriceFloor + length / kAirPriceScaleM;
            route.mode = "air";
            route.path = std::move(path);
            route.length_m = length;
            route.cruise_altitude_m = cruise;
            routes.push_back(std::move(route));
        }
    }

    // --- piracy -------------------------------------------------------------
    // Piracy needs BOTH a pirate region and a live sea link: a pirate coast with no trade
    // to prey on gets no trait.
    for (const SettlementSite& site : sites) {
        if (layer_at(world, "zone_pirate", site.x, site.z) <= kZoneFloor) continue;
        bool linked = false;
        for (const TransportRoute& route : routes) {
            if (route.mode == "sea" && (route.food.from == site.id || route.food.to == site.id)) linked = true;
        }
        if (linked) out.community_traits[static_cast<std::size_t>(site.id)].push_back("pirate");
    }

    // --- simulate and summarise ---------------------------------------------
    std::vector<FoodRoute> food_routes;
    food_routes.reserve(routes.size());
    for (const TransportRoute& route : routes) food_routes.push_back(route.food);
    const FoodRun run = simulate_food(models, food_routes, 4);

    for (std::size_t i = 0; i < all_count; ++i) {
        TransportSite row;
        if (i < surface_count) {
            row.site_id = sites[i].id;
            row.layer = "surface";
        } else {
            row.sky_site_id = sky.settlements[i - surface_count].id;
            row.layer = sky.settlements[i - surface_count].layer;
        }
        out.transport.sites.push_back(std::move(row));
    }

    out.world_economy.models = models;
    out.world_economy.months = run.months;
    // Only the FOURTH simulated year is summarised: the first three are the stores
    // filling, not the world's answer.
    const std::size_t final_start = run.months.size() >= 12 ? run.months.size() - 12 : 0;
    for (std::size_t i = 0; i < all_count && i < models.size(); ++i) {
        EconomySite row;
        if (i < surface_count) {
            row.site_id = sites[i].id;
            row.layer = "surface";
            if (i < request.site_names.size()) row.name = request.site_names[i];
        } else {
            row.sky_site_id = sky.settlements[i - surface_count].id;
            row.layer = sky.settlements[i - surface_count].layer;
            row.name = sky.settlements[i - surface_count].name;
        }
        for (std::size_t m = final_start; m < run.months.size(); ++m) {
            const FoodMonthCity& v = run.months[m].cities[i];
            row.annual_demand += v.demand;
            row.annual_harvest += v.harvest;
            row.annual_shortage += v.shortage;
            if (v.shortage > kShortageEpsilon) {
                row.lean_months.push_back(static_cast<int>(m - final_start) + 1);
            }
        }
        row.food_coverage =
            row.annual_demand != 0.0 ? 1.0 - row.annual_shortage / row.annual_demand : 1.0;
        if (!run.months.empty()) row.ending_reserve = run.months.back().cities[i].closing;
        out.world_economy.sites.push_back(std::move(row));
    }

    out.world_economy.method =
        "Authoritative new-recipe four-year monthly stress test: existing farm surplus plus allocated "
        "fisheries and independent sky production; shared finite ground/sea/air throughput, material "
        "budgets, spoilage and loss. No same-month re-export.";
    out.world_economy.limits =
        "Artistic food units; static communities and routes. Surface farm production is rural export "
        "surplus; this is not demographic equilibrium or a vessel simulator.";
    out.has_economy = true;
    return out;
}

}  // namespace fwg
