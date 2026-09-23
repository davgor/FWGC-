#include "fwg/city_layout.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <utility>

#include "fwg/random.hpp"

namespace fwg {
namespace {

constexpr double kInfinity = std::numeric_limits<double>::infinity();

// The reference's two fallback reasons. The building pass's overwrites the district pass's.
constexpr std::string_view kDistrictFallback =
    "Not enough valid non-water land anchors for requested feature counts within local search band.";
constexpr std::string_view kBuildingFallback =
    "Not enough valid non-water land anchors for requested feature and building placement counts within "
    "local search band.";

template <typename T, typename V>
bool contains(std::span<const T> items, const V& value) {
    return std::find(items.begin(), items.end(), value) != items.end();
}

using Entry = std::pair<double, int>;
using MinHeap = std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>>;

// A profile's own count for a feature, or the default table's where the profile omits it.
const FeatureCount& feature_spec(const LayoutProfile& profile, const LayoutFeature& fallback) {
    for (const LayoutFeature& own : profile.features) {
        if (own.name == fallback.name) return own.count;
    }
    return fallback.count;
}

// One weighted asset, walked cumulatively against one draw.
std::string_view weighted_pick(Rng& rng, std::span<const AssetChoice> choices) {
    double total = 0.0;
    for (const AssetChoice& c : choices) total += c.weight;
    // Unreachable with shipped data -- the codegen refuses a non-positive weight -- and kept
    // as the reference writes it: no draw at all.
    if (total <= 0.0) return choices.front().asset_id;
    const double draw = rng.random() * total;
    double cursor = 0.0;
    for (const AssetChoice& c : choices) {
        cursor += c.weight;
        if (draw <= cursor) return c.asset_id;
    }
    return choices.back().asset_id;
}

// What the building draw leaves behind: the reference's building plan, reduced to what the
// record publishes. The per-option table itself is not kept (see the header).
struct BuildingDraw {
    std::map<std::string, long long> assets;  // sorted by asset id
    int node_slots = 0;
    bool missing = false;
};

BuildingDraw draw_buildings(Rng& rng, std::string_view people, std::optional<long long> residents,
                            bool bridge_recommended, const std::vector<int>& ordered,
                            const std::map<int, double>& anchor_distance, const SphereGrid& grid,
                            const std::vector<double>& river, std::set<int>& used, const BuildingPack* pack) {
    BuildingDraw out;

    // The band's nodes that touch a river, and the ones on its rim: where river gates and
    // walls go. Both keep the band's (rotated) order.
    const std::set<int> in_band(ordered.begin(), ordered.end());
    std::vector<int> frontier;
    std::vector<int> boundary;
    for (const int id : ordered) {
        bool touches_river = false;
        bool on_rim = false;
        for (const SphereNeighbour& nb : grid.neighbours[static_cast<std::size_t>(id)]) {
            if (nb.index >= 0 && static_cast<std::size_t>(nb.index) < river.size() &&
                river[static_cast<std::size_t>(nb.index)] != 0.0) {
                touches_river = true;
            }
            if (in_band.count(nb.index) == 0) on_rim = true;
        }
        if (touches_river) frontier.push_back(id);
        if (on_rim) boundary.push_back(id);
    }

    // DRAW: where the anchored and river-gate options start taking nodes. Walls keep their
    // own cursor, from zero. Skipped when there is no node at all.
    std::size_t anchor_cursor = ordered.empty() ? 0 : static_cast<std::size_t>(rng.below(ordered.size()));
    std::size_t wall_cursor = 0;

    // Walk `candidates` cyclically from `cursor`, claiming nodes nothing has claimed. Returns
    // how many it claimed; `short_of` says it wanted more.
    const auto collect = [&](const std::vector<int>& candidates, int target, std::size_t& cursor, bool& short_of) {
        short_of = false;
        if (target <= 0) return 0;
        if (candidates.empty()) {
            short_of = true;
            return 0;
        }
        const std::size_t n = candidates.size();
        const long long max_attempts = static_cast<long long>(target) * static_cast<long long>(n);
        long long attempts = 0;
        int claimed = 0;
        cursor %= n;
        while (claimed < target && attempts < max_attempts) {
            const int id = candidates[cursor];
            cursor = (cursor + 1) % n;
            ++attempts;
            if (!used.insert(id).second) continue;
            ++claimed;
        }
        short_of = claimed < target;
        return claimed;
    };

    if (pack == nullptr) return out;
    for (const BuildingOption& option : pack->building_options) {
        if (!contains(option.profiles, people)) continue;
        const int target = resolve_feature_count(option.count, residents, option.required);
        if (target == 0) continue;

        int planned = target;  // `structural` draws its whole target and claims nothing
        int claimed = 0;
        bool short_of = false;
        if (option.placement == "anchored") {
            claimed = collect(ordered, target, anchor_cursor, short_of);
            planned = claimed;
        } else if (option.placement == "river_gate") {
            if (option.requires_bridge && !bridge_recommended) {
                // No bridge, no gate: nothing claimed, nothing drawn, and a required gate
                // reports its anchor missing.
                short_of = option.required;
                planned = 0;
            } else {
                const std::vector<int> gates(frontier.rbegin(), frontier.rend());
                claimed = collect(gates, target, anchor_cursor, short_of);
                planned = claimed;
            }
        } else if (option.placement == "wall") {
            std::vector<int> walls;
            if (!boundary.empty()) {
                // Farthest first. STABLE: rim nodes at the same distance keep the band's order.
                walls = boundary;
                std::stable_sort(walls.begin(), walls.end(), [&](int a, int b) {
                    return anchor_distance.at(a) > anchor_distance.at(b);
                });
            } else {
                walls.assign(ordered.rbegin(), ordered.rend());
            }
            claimed = collect(walls, target, wall_cursor, short_of);
            planned = claimed;
        } else if (option.placement != "structural") {
            throw std::invalid_argument("Unknown placement " + std::string(option.placement) + " for building option " +
                                        std::string(option.id));
        }
        if (short_of) out.missing = true;

        // DRAW: one per planned building, in option order.
        for (int k = 0; k < planned; ++k) ++out.assets[std::string(weighted_pick(rng, option.asset_choices))];
        out.node_slots += claimed;
    }
    return out;
}

}  // namespace

std::string_view pick_building_pack(std::span<const BuildingPack> packs, std::string_view fallback_id,
                                    std::uint32_t city_seed, std::string_view people, const PackGround& ground) {
    std::vector<const BuildingPack*> candidates;
    for (const BuildingPack& pack : packs) {
        const PackCriteria& c = pack.criteria;
        if (!contains(c.population_profiles, people)) continue;
        // Gated on ground: the natural biome OR the exact magical variant must be named. An
        // empty variant (no magic here) names nothing.
        if (c.ground_gated && !contains(c.biomes, ground.biome) &&
            (ground.variant.empty() || !contains(c.biome_variants, ground.variant))) {
            continue;
        }
        if (!(c.height_min_m.value_or(-1e6) <= ground.height_m && ground.height_m <= c.height_max_m.value_or(1e6))) {
            continue;
        }
        if (ground.resource < c.resource_min.value_or(0.0) || ground.resource > c.resource_max.value_or(1.0)) continue;
        if (ground.slope_degrees > c.slope_max_degrees.value_or(1000.0)) continue;
        if (!(c.freshwater_distance_m_min.value_or(0.0) <= ground.freshwater_m &&
              ground.freshwater_m <= c.freshwater_distance_m_max.value_or(1e9))) {
            continue;
        }
        candidates.push_back(&pack);
    }
    if (candidates.empty()) return fallback_id;

    double total = 0.0;
    for (const BuildingPack* pack : candidates) total += pack->weight;
    // Its own stream, named off the city's seed: the pack is ONE draw, and the layout plan
    // draws from a second stream named off the same seed.
    Rng rng = Rng::child(city_seed, "city-building-pack");
    const double draw = rng.random() * total;
    double current = 0.0;
    for (const BuildingPack* pack : candidates) {
        current += pack->weight;
        if (draw <= current) return pack->id;
    }
    return candidates.back()->id;
}

std::string_view pick_building_pack(std::uint32_t city_seed, std::string_view people, const PackGround& ground) {
    return pick_building_pack(building_packs(), fallback_building_pack_id(), city_seed, people, ground);
}

const BuildingPack& building_pack_or_fallback(std::string_view id) {
    const auto packs = building_packs();
    for (const BuildingPack& pack : packs) {
        if (pack.id == id) return pack;
    }
    for (const BuildingPack& pack : packs) {
        if (pack.id == fallback_building_pack_id()) return pack;
    }
    throw std::logic_error("the building pack catalogue holds no fallback pack");
}

const LayoutProfile* layout_profile(std::string_view id) {
    for (const LayoutProfile& profile : layout_profiles()) {
        if (profile.id == id) return &profile;
    }
    return nullptr;
}

const LayoutProfile& pick_layout_profile(std::string_view forced_id, std::span<const LayoutProfile> profiles,
                                         std::string_view fallback_id, double river_distance_m) {
    const auto find = [&](std::string_view id) -> const LayoutProfile* {
        for (const LayoutProfile& profile : profiles) {
            if (profile.id == id) return &profile;
        }
        return nullptr;
    };
    // The pack's own choice wins, when the catalogue holds it.
    if (!forced_id.empty()) {
        if (const LayoutProfile* forced = find(forced_id)) return *forced;
    }
    const LayoutProfile* fallback = find(fallback_id);
    if (fallback == nullptr) throw std::logic_error("the layout profile catalogue holds no fallback profile");
    const LayoutProfile* riverfront = find(kRiverfrontLayoutProfileId);
    if (riverfront == nullptr) return *fallback;
    if (std::isfinite(river_distance_m) && river_distance_m <= riverfront->placement.river_buffer_m * 1.8) {
        return *riverfront;
    }
    return *fallback;
}

const LayoutProfile& pick_layout_profile(const BuildingPack& pack, double river_distance_m) {
    return pick_layout_profile(pack.layout_profile_id, layout_profiles(), fallback_layout_profile_id(),
                               river_distance_m);
}

std::vector<double> river_distances(const SphereGrid& grid, const std::vector<double>& river) {
    const std::size_t count = grid.node_count();
    std::vector<double> distance(count, kInfinity);
    MinHeap queue;
    for (std::size_t i = 0; i < count && i < river.size(); ++i) {
        if (river[i] == 0.0) continue;
        distance[i] = 0.0;
        queue.emplace(0.0, static_cast<int>(i));
    }
    while (!queue.empty()) {
        const auto [value, i] = queue.top();
        queue.pop();
        const auto ui = static_cast<std::size_t>(i);
        if (value != distance[ui]) continue;
        for (const SphereNeighbour& nb : grid.neighbours[ui]) {
            const double candidate = value + nb.distance;
            const auto j = static_cast<std::size_t>(nb.index);
            if (candidate < distance[j]) {
                distance[j] = candidate;
                queue.emplace(candidate, nb.index);
            }
        }
    }
    return distance;
}

std::vector<AnchorCandidate> land_anchor_candidates(const SphereGrid& grid, int start,
                                                    const std::vector<double>& river_distance_m,
                                                    const std::vector<double>& water,
                                                    const std::vector<double>& slope, double max_slope_degrees,
                                                    double min_river_distance_m) {
    std::vector<AnchorCandidate> out;
    if (start < 0 || static_cast<std::size_t>(start) >= grid.node_count()) return out;
    // ABSOLUTE METRES, authored for an 11 km world: see the header.
    const double max_walk =
        std::isfinite(min_river_distance_m) ? std::max(180.0, 4.0 * min_river_distance_m + 180.0) : 800.0;

    // Sparse: the walk settles only the band, and reaches one ring past it.
    std::map<int, double> distance;
    distance[start] = 0.0;
    MinHeap queue;
    queue.emplace(0.0, start);
    while (!queue.empty()) {
        const auto [value, node] = queue.top();
        queue.pop();
        if (value != distance.at(node)) continue;
        // Past the band: reached, but never collected or expanded.
        if (value > max_walk) continue;
        const auto u = static_cast<std::size_t>(node);
        if (water[u] == 0.0 && slope[u] <= max_slope_degrees) {
            if (std::isinf(min_river_distance_m) || river_distance_m[u] >= min_river_distance_m) {
                out.push_back({value, node});
            }
        }
        for (const SphereNeighbour& nb : grid.neighbours[u]) {
            const double candidate = value + nb.distance;
            const auto found = distance.find(nb.index);
            if (found == distance.end() || candidate < found->second) {
                distance[nb.index] = candidate;
                queue.emplace(candidate, nb.index);
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const AnchorCandidate& a, const AnchorCandidate& b) {
        return a.distance_m != b.distance_m ? a.distance_m < b.distance_m : a.node < b.node;
    });
    return out;
}

int resolve_feature_count(const FeatureCount& count, std::optional<long long> residents, bool required) {
    double base = count.base;
    if (residents) {
        base += count.per_100_residents.value_or(0.0) *
                (static_cast<double>(std::max(0LL, *residents)) / 100.0);
    }
    if (!std::isfinite(base)) throw std::invalid_argument("a feature count must be a finite number");
    const double floored = std::floor(base);
    long long resolved = floored >= 9.0e18 ? std::numeric_limits<long long>::max()
                         : floored <= -9.0e18 ? std::numeric_limits<long long>::min()
                                              : static_cast<long long>(floored);
    // A required option builds at least one, whatever its authored floor.
    std::optional<double> floor_count = count.min;
    if (required) floor_count = std::max(floor_count.value_or(1.0), 1.0);
    if (floor_count) resolved = std::max(resolved, static_cast<long long>(*floor_count));
    if (count.max) resolved = std::min(resolved, static_cast<long long>(*count.max));
    resolved = std::max(0LL, resolved);
    return static_cast<int>(std::min<long long>(resolved, std::numeric_limits<int>::max()));
}

bool feature_fits(const FeatureCount& count, std::string_view people) {
    return !count.profiles || contains(*count.profiles, people);
}

CityLayout plan_city_layout(std::uint32_t city_seed, int node, const LayoutProfile& profile, std::string_view people,
                            std::optional<long long> residents, double river_distance_m,
                            const std::vector<AnchorCandidate>& candidates, const SphereGrid& grid,
                            const std::vector<double>& river, const BuildingPack* pack) {
    if (node < 0 || static_cast<std::size_t>(node) >= grid.node_count()) {
        throw std::invalid_argument("plan_city_layout needs a node of this grid");
    }
    // ONE stream for the whole plan.
    Rng rng = Rng::child(city_seed, "city-layout");
    const LayoutPlacement& placement = profile.placement;

    CityLayout plan;
    plan.layout_profile_id = std::string(profile.id);
    plan.layout_profile_name = std::string(profile.name);
    plan.population_profile = std::string(people);
    plan.population_estimate = residents;

    // How many of the city node's own neighbours carry a river: a fork asks for a bridge.
    int fork_degree = 0;
    for (const SphereNeighbour& nb : grid.neighbours[static_cast<std::size_t>(node)]) {
        if (static_cast<std::size_t>(nb.index) < river.size() && river[static_cast<std::size_t>(nb.index)] != 0.0) {
            ++fork_degree;
        }
    }
    plan.river.adjacent_river_edges = fork_degree;
    if (std::isfinite(river_distance_m)) plan.river.river_distance_m = river_distance_m;
    plan.river.bridge_recommended =
        fork_degree >= placement.river_fork_bridge_threshold && placement.bridge_if_river_fork;
    plan.river.bridge_threshold = placement.river_fork_bridge_threshold;

    // The candidates in walking order, each node once. A later duplicate's distance wins,
    // as the reference's dict comprehension has it; the search yields none.
    std::vector<int> ordered;
    std::set<int> seen;
    std::map<int, double> anchor_distance;
    for (const AnchorCandidate& c : candidates) {
        if (seen.insert(c.node).second) ordered.push_back(c.node);
        anchor_distance[c.node] = c.distance_m;
    }

    // DRAW: rotate the list, so the districts do not always start at the city's own node.
    if (!ordered.empty()) {
        const auto start = static_cast<std::ptrdiff_t>(rng.below(ordered.size()));
        std::rotate(ordered.begin(), ordered.begin() + start, ordered.end());
    }

    // The districts, in the default table's order, each taking the next nodes off the list.
    std::set<int> used;
    bool missing = false;
    std::size_t cursor = 0;
    for (const LayoutFeature& fallback : layout_feature_defaults()) {
        const FeatureCount& spec = feature_spec(profile, fallback);
        const int count = feature_fits(spec, people) ? resolve_feature_count(spec, residents) : 0;
        LayoutFeaturePlan feature;
        feature.name = std::string(fallback.name);
        feature.target_count = count;
        for (std::size_t k = cursor; k < ordered.size() && k < cursor + static_cast<std::size_t>(count); ++k) {
            const int candidate = ordered[k];
            const auto u = static_cast<std::size_t>(candidate);
            feature.anchors.push_back({candidate, grid.point_x[u], grid.point_z[u], anchor_distance.at(candidate)});
            used.insert(candidate);
        }
        cursor += feature.anchors.size();
        feature.count = static_cast<int>(feature.anchors.size());
        if (feature.count < count) missing = true;
        plan.features.push_back(std::move(feature));
    }
    if (missing) plan.fallback = std::string(kDistrictFallback);

    // The asset draw, on the nodes the districts left.
    const BuildingDraw draw = draw_buildings(rng, people, residents, plan.river.bridge_recommended, ordered,
                                             anchor_distance, grid, river, used, pack);
    if (pack != nullptr) plan.building_pack_id = std::string(pack->id);
    for (const auto& [asset_id, n] : draw.assets) {
        plan.required_assets.push_back({asset_id, n});
        plan.required_asset_count += n;
    }
    plan.required_node_slots = draw.node_slots;
    plan.asset_anchors_missing = draw.missing;
    if (draw.missing) plan.fallback = std::string(kBuildingFallback);

    plan.constraints = CityLayout::Constraints{placement.max_anchor_slope_degrees, placement.river_buffer_m,
                                               placement.river_buffer_m};
    return plan;
}

}  // namespace fwg
