#include "fwg/ecology.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <queue>
#include <stdexcept>
#include <string_view>

#include "fwg/content.hpp"
#include "fwg/noise.hpp"
#include "fwg/random.hpp"
#include "fwg/routing.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

constexpr double kPi = std::numbers::pi;

double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

double angle_between(const Vec3& a, const Vec3& b) { return std::acos(clamp_unit(dot(a, b))); }

// Uniform on the sphere: y uniform in [-1, 1] gives equal area per band, which a
// naive uniform latitude does not.
Vec3 random_point(Rng& rng) {
    const double y = rng.uniform(-1.0, 1.0);
    const double lon = rng.uniform(-kPi, kPi);
    const double c = std::sqrt(1.0 - y * y);
    return {c * std::cos(lon), y, c * std::sin(lon)};
}

// Weighted pick over a cumulative distribution: ONE draw, whatever the weights.
//
// The single draw is the contract, not an implementation detail. Every cluster must
// cost the same number of draws whether or not it ends up manifesting, or the stream
// every later cluster reads depends on how the earlier ones turned out. An empty
// distribution has no honest answer here, so it throws rather than returning a fixed
// index without drawing -- and WorldOptions::validate makes that unreachable.
std::size_t weighted_choice(Rng& rng, const double* weights, std::size_t count) {
    double total = 0.0;
    for (std::size_t i = 0; i < count; ++i) total += weights[i];
    if (total <= 0.0) throw std::invalid_argument("weighted_choice has no positive weight to draw from");
    const double target = rng.random() * total;
    double running = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        running += weights[i];
        if (target < running) return i;
    }
    return count - 1;
}

constexpr std::string_view kZones[] = {
    "demonic", "draconic", "pirate", "steampunk", "witch_huts", "red_sands", "dead_sea",
    "starlight_lakes", "haunted_sands", "enchanted", "fungal", "crystal", "haunted_marsh"};

Option zone_option(std::string_view zone, const char* suffix) {
    std::string name(zone);
    name += suffix;
    const auto option = option_by_name(name);
    if (!option) throw std::runtime_error("missing world option " + name);
    return *option;
}

}  // namespace

std::span<const std::string_view> zone_names() { return kZones; }

std::vector<double> monthly_temperatures(double mean, double latitude_deg, double wet, double seasonality) {
    // Seasonality is a latitude effect: the equator barely swings, the poles swing
    // hard, and moisture damps it because water holds heat.
    const double amplitude = 20.0 * std::sin(latitude_deg * kPi / 180.0) * (1.0 - 0.4 * wet) * seasonality;
    std::vector<double> out(12);
    for (int month = 0; month < 12; ++month) {
        out[static_cast<std::size_t>(month)] = mean + amplitude * std::cos(2.0 * kPi * (month - 6) / 12.0);
    }
    return out;
}

std::string cold_habitat(const std::vector<double>& temps, double wet, double accumulation) {
    const double warmest = *std::max_element(temps.begin(), temps.end());
    if (warmest < 1.0 && wet >= accumulation * 0.4) return "ice_cap";
    if (warmest < 10.0) return "tundra";
    const int warm_months = static_cast<int>(std::count_if(temps.begin(), temps.end(),
                                                           [](double t) { return t > 5.0; }));
    if (warm_months <= 6) return wet >= 0.4 ? "boreal" : "tundra";
    return {};
}

std::vector<Archipelago> island_layout(const Config& cfg) {
    const WorldOptions& o = cfg.options;
    Rng rng(child_seed64(cfg.seed, "ocean-archipelagos-v1"));

    const int count = o.as_int(Option::archipelago_count);
    const int per_cluster = o.as_int(Option::islands_per_cluster);
    const double radius = o[Option::island_radius];
    const double spacing = o[Option::island_spacing];
    const double extent = o[Option::archipelago_extent];
    const double occurrence = o[Option::archipelago_occurrence];
    const double weights[4] = {o[Option::volcanic_island_weight], o[Option::atoll_weight],
                               o[Option::continental_island_weight], o[Option::cold_island_weight]};
    static constexpr const char* kKinds[4] = {"volcanic", "atoll", "continental", "cold"};

    std::vector<Archipelago> clusters;
    for (int k = 0; k < count; ++k) {
        Vec3 center = random_point(rng);
        const std::size_t kind = weighted_choice(rng, weights, 4);

        // A cold archipelago is pulled toward a pole before the occurrence roll, so
        // the roll does not depend on where it landed.
        if (kind == 3) {
            center = unit({center.x * 0.4, std::copysign(0.9, center.y), center.z * 0.4});
        }
        // The roll happens AFTER the kind and the cold adjustment, so a refused
        // cluster still costs the same draws as one that manifests.
        if (rng.random() > occurrence) continue;

        Archipelago cluster;
        cluster.id = "ocean-" + std::to_string(k);
        cluster.kind = kKinds[kind];
        cluster.direction = center;
        for (int j = 0; j < per_cluster; ++j) {
            const Vec3 q = random_point(rng);
            // sqrt(j) spreads islands out at a falling rate, so a cluster is dense at
            // its heart and thins toward the rim rather than being a uniform disc.
            const double scale = radius * spacing * extent * std::sqrt(static_cast<double>(j));
            const Vec3 p = unit({center.x + scale * q.x, center.y + scale * q.y, center.z + scale * q.z});
            cluster.islands.push_back({p, radius * rng.uniform(0.7, 1.3)});
        }
        clusters.push_back(std::move(cluster));
    }
    return clusters;
}

std::vector<Archipelago> shape_islands(std::map<std::string, Grid>& layers, const Config& cfg) {
    std::vector<Archipelago> clusters = island_layout(cfg);
    const int n = cfg.size;

    Grid relief(n);
    const Grid& continental = layers.at("continental");

    // A cluster manifests only where the crust beneath its centre is ocean floor:
    // archipelagos are raised from the deep, not stamped onto a continent.
    for (Archipelago& cluster : clusters) {
        const Vec3& p = cluster.direction;
        // Round-half-to-even, matching the reference's round(); the cell this picks
        // decides eligibility outright, so the tie rule is not cosmetic.
        const auto x = static_cast<int>(std::nearbyint((std::atan2(p.z, p.x) + kPi) / (2.0 * kPi) * (n - 1)));
        const auto z = static_cast<int>(std::nearbyint(std::acos(clamp_unit(p.y)) / kPi * (n - 1)));
        const int cx = std::max(0, std::min(n - 1, x));
        const int cz = std::max(0, std::min(n - 1, z));
        cluster.eligible = continental.at(cx, cz) < cfg.sea_level;
    }

    Grid& structure = layers.at("structure");
    Grid& height = layers.at("height");
    Grid& base = layers.at("base");

    std::vector<double> relief_row;
    for (int z = 0; z < n; ++z) {
        const int count = distinct_count(z, n);
        relief_row.assign(static_cast<std::size_t>(count), 0.0);
        for (int x = 0; x < count; ++x) {
            const Vec3 p = direction(x, z, n);
            const double original = structure.at(x, z);
            double value = original;

            for (const Archipelago& cluster : clusters) {
                if (!cluster.eligible) continue;
                for (const Island& island : cluster.islands) {
                    const double d = angle_between(p, island.direction) / island.angular_radius;
                    if (d >= 2.0) continue;  // beyond twice its radius an island does nothing
                    const double support = clamp01((2.0 - d) / 0.7);
                    double target;
                    if (cluster.kind == "atoll") {
                        // A ring, not a dome: the peak sits at 0.7 radii with a dip inside.
                        const double ring = (d - 0.7) / 0.24;
                        target = cfg.sea_level + cfg.tectonic_relief * (0.22 * std::exp(-ring * ring) - 0.07);
                    } else {
                        const double peak = (cluster.kind == "volcanic") ? 0.9 : 0.45;
                        target = cfg.sea_level + cfg.tectonic_relief * (peak * std::exp(-d * d * 2.0) - 0.1);
                    }
                    // max, never sum: overlapping islands make one larger island rather
                    // than a stack, and a cluster cannot lift continental foundations.
                    value = std::max(value, original + (target - original) * support);
                }
            }
            relief_row[static_cast<std::size_t>(x)] = value - original;
        }
        relief.fill_row(z, relief_row);
    }

    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            const double delta = relief.at(x, z);
            structure.at(x, z) += delta;
            height.at(x, z) += delta;
            base.at(x, z) += delta;
        }
    }
    layers["archipelago_relief"] = relief;
    return clusters;
}

void add_environment(World& world) {
    const Config& cfg = world.effective;
    const WorldOptions& o = cfg.options;
    const int n = cfg.size;

    const auto held = sphere_grid(n, cfg.globe_radius);
    const SphereGrid& grid = *held;
    const std::size_t count = grid.node_count();

    const auto node_values = [&](const char* name) {
        const Grid& g = world.layers.at(name);
        std::vector<double> out(count);
        for (std::size_t i = 0; i < count; ++i) out[i] = g.at(grid.point_x[i], grid.point_z[i]);
        return out;
    };

    const std::vector<double> water = node_values("water_type");
    const std::vector<double> temp = node_values("temperature");
    const std::vector<double> wet = node_values("moisture");
    const std::vector<double> slope = node_values("slope");
    const std::vector<double> depth = node_values("water_depth");
    const std::vector<double> river = node_values("rain_river");
    const std::vector<double> height = node_values("height");
    const std::vector<double> volcanic = node_values("volcanic");

    std::vector<Vec3> vectors(count);
    for (std::size_t i = 0; i < count; ++i) vectors[i] = direction(grid.point_x[i], grid.point_z[i], n);

    // Distance to the open ocean, over terrain, from every ocean cell at once. Uses
    // the shared allocator rather than a second Dijkstra written out here: two copies
    // of a search are two places for a tie rule or a bound to drift apart.
    std::vector<std::pair<int, int>> ocean_seeds;
    for (std::size_t i = 0; i < count; ++i) {
        if (water[i] == 1.0) ocean_seeds.emplace_back(static_cast<int>(i), 0);
    }
    const std::vector<double> ocean_distance =
        allocate_access(grid, ocean_seeds, [](int, int, double d) { return std::optional<double>(d); })
            .distance;

    static constexpr const char* kFieldNames[] = {
        "metal_richness", "salinity", "coastal_exposure", "harbor_suitability", "fishing_productivity",
        "reef", "lagoon", "estuary", "sheltered_bay", "rocky_coast", "kelp", "fjord", "open_ocean",
        "maritime", "boreal", "tundra", "ice_cap", "coastal_support"};
    constexpr std::size_t kFieldCount = sizeof(kFieldNames) / sizeof(kFieldNames[0]);
    std::vector<std::vector<double>> fields(kFieldCount, std::vector<double>(count, 0.0));

    struct MonthField {
        std::vector<double> temperature;
        std::vector<double> snow;
        std::vector<double> water_ice;
    };
    std::vector<MonthField> months(12);
    for (MonthField& m : months) {
        m.temperature.resize(count);
        m.snow.resize(count);
        m.water_ice.resize(count);
    }

    const std::uint32_t metal_seed = child_seed(cfg.seed, "metals-v1");
    const double sea_draft = o[Option::sea_draft];
    const double salinity_gain = o[Option::salinity];
    const double seasonality = o[Option::seasonality];
    const double ice_accumulation = o[Option::ice_accumulation];
    const double metal_abundance = o[Option::metal_abundance];
    const double coast_reach = std::max(world.spacing_m, 120.0);

    Grid& biome = world.layers.at("biome");
    std::vector<std::string> cold_label(count);

    for (std::size_t i = 0; i < count; ++i) {
        const int x = grid.point_x[i];
        const int z = grid.point_z[i];
        const Vec3& p = vectors[i];
        const auto& near = grid.neighbours[i];
        const double neighbours = static_cast<double>(std::max<std::size_t>(1, near.size()));

        double ocean_neighbours = 0.0;
        double any_water_neighbours = 0.0;
        double steep_dry_neighbours = 0.0;
        bool river_mouth = false;
        bool deep_enough = false;
        for (const SphereNeighbour& nb : near) {
            const auto j = static_cast<std::size_t>(nb.index);
            if (water[j] == 1.0) ocean_neighbours += 1.0;
            if (water[j] > 0.0) any_water_neighbours += 1.0;
            if (slope[j] > 12.0 && water[j] == 0.0) steep_dry_neighbours += 1.0;
            if (river[j] != 0.0 && water[j] == 0.0) river_mouth = true;
            if (water[j] == 1.0 && depth[j] >= sea_draft) deep_enough = true;
        }

        const bool is_water = water[i] != 0.0;
        const bool is_ocean = water[i] == 1.0;
        const bool coast = !is_water && ocean_distance[i] <= coast_reach;
        const double exposure = clamp01(coast ? ocean_neighbours / neighbours
                                               : any_water_neighbours / neighbours);
        const double shallow = is_water ? clamp01(1.0 - depth[i] / 80.0) : 0.0;
        const double salt = is_ocean ? 1.0
                                     : (water[i] == 2.0 ? clamp01((0.6 - wet[i]) * 2.0 * salinity_gain) : 0.0);

        const double reef = is_ocean ? shallow * clamp01((temp[i] - 15.0) / 12.0) * (1.0 - 0.5 * exposure) : 0.0;
        const double lagoon = is_ocean ? shallow * (1.0 - exposure) : 0.0;
        const double estuary = (is_ocean && river_mouth) ? 1.0 : 0.0;
        const double kelp = is_ocean ? shallow * clamp01(1.0 - std::abs(temp[i] - 10.0) / 15.0) : 0.0;
        const double fjord = (is_ocean ? 1.0 : 0.0) * clamp01(steep_dry_neighbours / 3.0) *
                             clamp01((15.0 - temp[i]) / 15.0);
        const double harbor = (coast && deep_enough)
                                  ? clamp01((1.0 - exposure) * 0.7 + 0.3) * std::exp(-slope[i] / 22.0)
                                  : 0.0;
        const double fish = is_water ? clamp01(0.12 + 0.55 * shallow + 0.3 * estuary + 0.2 * kelp + 0.15 * reef)
                                     : 0.0;

        const double latitude = 90.0 - 180.0 * z / (n - 1);
        const std::vector<double> temps = monthly_temperatures(temp[i], latitude, wet[i], seasonality);
        const std::string cold = is_water ? std::string() : cold_habitat(temps, wet[i], ice_accumulation);
        cold_label[i] = cold;
        // THIS is where boreal forest, cold tundra and land ice reach the map. classify()
        // emits tundra and snow and never these; the cold pass relabels almost every cell
        // it touches, which is why those two rows are tombstoned for art.
        if (!cold.empty()) {
            biome.at(x, z) = cold == "boreal" ? 15.0 : (cold == "tundra" ? 16.0 : 17.0);
        }

        const double metal = clamp01((0.45 + 0.7 * perlin3(p.x * 5.0, p.y * 5.0, p.z * 5.0, metal_seed) +
                                      0.2 * volcanic[i]) * metal_abundance);

        const double values[kFieldCount] = {
            metal, salt, exposure, harbor, fish, reef, lagoon, estuary,
            harbor,  // sheltered_bay shares the harbour term
            (coast ? 1.0 : 0.0) * exposure * clamp01(slope[i] / 25.0),
            kelp, fjord,
            (is_ocean ? 1.0 : 0.0) * (1.0 - shallow),
            !is_water ? std::exp(-ocean_distance[i] / 300.0) : 0.0,
            cold == "boreal" ? 1.0 : 0.0,
            cold == "tundra" ? 1.0 : 0.0,
            cold == "ice_cap" ? 1.0 : 0.0,
            harbor * 0.5};
        for (std::size_t f = 0; f < kFieldCount; ++f) fields[f][i] = values[f];

        for (std::size_t m = 0; m < 12; ++m) {
            const double t = temps[m];
            months[m].temperature[i] = t;
            months[m].snow[i] = !is_water ? clamp01(-t / 8.0) * clamp01(wet[i] * 2.0) : 0.0;
            months[m].water_ice[i] = is_water ? clamp01((-1.8 * salt - t) / 6.0) : 0.0;
        }
    }

    // Coastal support is OPPORTUNITY, never credited as harvested food: a dry cell next
    // to a productive shore can reach it, which is not the same as having eaten from it.
    constexpr std::size_t kHarbor = 3, kFish = 4, kSupport = 17;
    for (std::size_t i = 0; i < count; ++i) {
        if (water[i] != 0.0) continue;
        double best = 0.0;
        for (const SphereNeighbour& nb : grid.neighbours[i]) {
            best = std::max(best, fields[kFish][static_cast<std::size_t>(nb.index)]);
        }
        fields[kSupport][i] = fields[kHarbor][i] * 0.5 + 0.5 * best;
    }

    for (std::size_t f = 0; f < kFieldCount; ++f) world.layers[kFieldNames[f]] = grid.to_grid(fields[f]);

    // Islandness: a land component small RELATIVE TO THIS WORLD'S LAND, not against an
    // absolute area. An absolute floor was authored for a world 18x narrower and never
    // bound at the default raster, which left the layer identically zero.
    std::vector<double> islandness(count, 0.0);
    {
        double land_total = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            if (water[i] == 0.0) land_total += grid.areas[i];
        }
        std::vector<char> seen(count, 0);
        std::vector<int> stack;
        for (std::size_t start = 0; start < count; ++start) {
            if (seen[start] || water[start] != 0.0) continue;
            std::vector<int> component;
            stack.assign(1, static_cast<int>(start));
            seen[start] = 1;
            double area = 0.0;
            while (!stack.empty()) {
                const int i = stack.back();
                stack.pop_back();
                component.push_back(i);
                area += grid.areas[static_cast<std::size_t>(i)];
                for (const SphereNeighbour& nb : grid.neighbours[static_cast<std::size_t>(i)]) {
                    const auto j = static_cast<std::size_t>(nb.index);
                    if (seen[j] || water[j] != 0.0) continue;
                    seen[j] = 1;
                    stack.push_back(nb.index);
                }
            }
            const double flag = area < land_total * 0.15 ? 1.0 : 0.0;
            for (const int i : component) islandness[static_cast<std::size_t>(i)] = flag;
        }
    }
    world.layers["island_habitat"] = grid.to_grid(islandness);

    // The cold pass wrote categorical values straight into the biome grid, so the seam
    // and the poles have to be closed again by hand.
    for (int z = 0; z < n; ++z) {
        if (z == 0 || z == n - 1) {
            for (int x = 1; x < n; ++x) biome.at(x, z) = biome.at(0, z);
        } else {
            biome.at(n - 1, z) = biome.at(0, z);
        }
    }
    world.layers["natural_biome"] = biome;

    // --- the thirteen regions ---------------------------------------------
    // Magical eligibility reads the ley layers. Those arrive at stage 9, which is not
    // written, so `magic` is zero everywhere and only the three non-magical regions --
    // pirate, red_sands and dead_sea -- can manifest today. That is the reference's own
    // behaviour when leylines are absent, not a stub.
    const auto magic = [&](const char* school, std::size_t i) {
        const auto it = world.layers.find(std::string("ley_") + school);
        if (it == world.layers.end()) return 0.0;
        return it->second.at(grid.point_x[i], grid.point_z[i]);
    };

    EcologyReport report;
    for (const std::string_view zone : kZones) {
        std::vector<double> scores(count, 0.0);
        for (std::size_t i = 0; i < count; ++i) {
            const double dry = water[i] == 0.0 ? 1.0 : 0.0;
            const double arid = clamp01((0.5 - wet[i]) * 3.0);
            const double mountain = clamp01(std::max(0.0, height[i]) / 100.0) * clamp01(slope[i] / 20.0);
            const double harbour = fields[kHarbor][i];
            double v = 0.0;
            if (zone == "demonic") v = dry * magic("infernal", i);
            else if (zone == "draconic") v = dry * mountain * std::max(magic("primordial", i), magic("weave", i));
            else if (zone == "pirate") v = harbour;
            else if (zone == "steampunk")
                v = dry * fields[0][i] * std::max({fields[13][i], volcanic[i], 0.2});
            else if (zone == "witch_huts") v = dry * magic("umbral", i) * (1.0 - fields[kSupport][i]);
            else if (zone == "red_sands") v = dry * arid;
            else if (zone == "dead_sea") v = (water[i] == 2.0 ? 1.0 : 0.0) * fields[1][i];
            else if (zone == "starlight_lakes")
                v = (water[i] == 2.0 ? 1.0 : 0.0) * std::max(magic("weave", i), magic("holy", i));
            else if (zone == "haunted_sands") v = dry * arid * magic("umbral", i);
            else if (zone == "enchanted") v = dry * wet[i] * magic("weave", i);
            else if (zone == "fungal") v = dry * wet[i] * magic("primordial", i);
            else if (zone == "crystal") v = dry * arid * magic("primordial", i);
            else if (zone == "haunted_marsh")
                v = dry * wet[i] * std::exp(-slope[i] / 6.0) * magic("umbral", i);
            scores[i] = v;
        }

        const double occurrence = o[zone_option(zone, "_occurrence")];
        const double extent = o[zone_option(zone, "_extent")];
        const double intensity = o[zone_option(zone, "_intensity")];

        Rng rng(child_seed64(cfg.seed, "region-" + std::string(zone) + "-v1"));
        std::vector<int> candidates;
        for (std::size_t i = 0; i < count; ++i) {
            if (scores[i] > 0.04) candidates.push_back(static_cast<int>(i));
        }
        const bool active = !candidates.empty() && rng.random() < occurrence;

        std::vector<int> centers;
        if (active) {
            // One jitter draw per candidate, in candidate order, so the ranking is a
            // function of the stream rather than of the sort's comparison order.
            std::vector<std::pair<double, int>> ranked;
            ranked.reserve(candidates.size());
            for (const int i : candidates) {
                const double jitter = 0.6 + 0.4 * rng.random();
                ranked.emplace_back(-(scores[static_cast<std::size_t>(i)] * jitter), i);
            }
            std::stable_sort(ranked.begin(), ranked.end(),
                             [](const auto& a, const auto& b) { return a.first < b.first; });
            for (const auto& [key, i] : ranked) {
                (void)key;
                const bool apart = std::all_of(centers.begin(), centers.end(), [&](int j) {
                    return angle_between(vectors[static_cast<std::size_t>(i)],
                                         vectors[static_cast<std::size_t>(j)]) > extent * 1.5;
                });
                if (apart) centers.push_back(i);
                if (centers.size() >= 3) break;
            }
        }

        std::vector<double> values(count, 0.0);
        double maximum = 0.0;
        double area = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            double best = 0.0;
            for (const int j : centers) {
                const double a = angle_between(vectors[i], vectors[static_cast<std::size_t>(j)]) / extent;
                best = std::max(best, std::exp(-a * a));
            }
            values[i] = intensity * scores[i] * best;
            maximum = std::max(maximum, values[i]);
            if (values[i] > 0.05) area += grid.areas[i];
        }
        world.layers["zone_" + std::string(zone)] = grid.to_grid(values);

        RegionInfluence influence;
        influence.id = std::string(zone);
        influence.centers = centers;
        influence.maximum = maximum;
        influence.area_km2 = area / 1e6;
        influence.reason = !centers.empty() ? "manifested"
                                            : (candidates.empty() ? "insufficient suitable habitat"
                                                                  : "not selected by occurrence seed");
        report.regions.push_back(std::move(influence));

        if (zone == "witch_huts" || zone == "haunted_sands") {
            for (const int i : centers) {
                const auto ui = static_cast<std::size_t>(i);
                report.landmarks.push_back({std::string(zone) + "-" + std::to_string(i),
                                            zone == "witch_huts" ? "witch_hut" : "necropolis",
                                            grid.point_x[ui], grid.point_z[ui], "surface", values[ui]});
            }
        }
    }

    // --- seasonal environment ---------------------------------------------
    world.seasonal_months.clear();
    for (const MonthField& m : months) {
        SeasonalMonth out;
        out.temperature = grid.to_grid(m.temperature);
        out.snow = grid.to_grid(m.snow);
        out.water_ice = grid.to_grid(m.water_ice);
        world.seasonal_months.push_back(std::move(out));
    }

    // Assign the fields this pass owns, NOT the whole report: `archipelagos` was
    // filled by the tectonic stage long before this runs, and a wholesale assignment
    // silently threw them away.
    world.ecology.regions = std::move(report.regions);
    world.ecology.landmarks = std::move(report.landmarks);
    world.has_ecology = true;
    world.systems.push_back("ecology");
    world.warnings.push_back(
        "Depth, coast exposure, salinity and climate proxies; no tides, currents or biological reef simulation. "
        "Monthly latitude/elevation temperature proxy; snow and water ice do not modify bedrock. Regions are "
        "independent overlapping fields; low suitability can still be valuable to a specialist site.");
}

}  // namespace fwg
