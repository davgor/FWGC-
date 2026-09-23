// A city's node-scale layout: which building pack its assets are drawn from, which layout
// profile its districts follow, which terrain nodes those districts claim, and the asset
// requirement that follows. Stage 10 writes one onto every city it founds, as
// `settlements.sites[].city_layout` (version 2) beside `building_pack_id`,
// `building_pack_seed` and `building_pack_version`.
//
// The reference's terrain_settlements: _pick_city_building_pack, _pick_city_layout_profile,
// _dijkstra_distances, _collect_land_anchor_candidates, _build_city_layout_plan and
// _build_city_building_plan, with _resolve_feature_count and _weighted_pick.
//
// IT MAKES NO BUILDING-PLACEMENT CLAIM. city_plans (stage 16) is the one block that says
// which buildings a city has and where each stands. Version 2 of this record dropped the
// per-option `buildings` table, whose placed counts read as "this city placed N buildings"
// beside a plan that had actually placed them -- two answers to one question, and the
// wrong one reported a shortfall in every city, which hid the real ones. What the asset
// draw produces survives as three roll-ups: required_assets, required_asset_count and
// required_node_slots, plus whether the draw ran short of anchors.
//
// ONE SEED, PUBLISHED. A city's pack is drawn, and its layout planned, from streams named
// off `building_pack_seed`, so the layout can be re-planned from the site alone -- which
// stage 10 does once the city has been sized (settlements_stage.cpp). The seed is keyed on
// the city's node AND its position in the founding list, as the reference keys it, so an
// age that reorders the founding list redraws a standing city's pack.
//
// A KNOWN SCALE DEFECT, INHERITED: the anchor search walks max(180, 4 * river_buffer_m + 180)
// METRES from the city, authored for an 11 km world. At every shipped width a raster cell
// is kilometres across, so the search never leaves the city's own node: a city claims at
// most one district anchor and draws no building asset at all. See docs/scale-defects.md;
// pinned by a test.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/city_plan.hpp"
#include "fwg/grid.hpp"

namespace fwg {

// The version stamped on a city's node-scale layout record, the reference's
// CITY_LAYOUT_VERSION. Bumping it is a world-output schema change.
inline constexpr int kCityLayoutVersion = 2;

// The profile a river-side city takes when its pack forces none. Named by the reference.
inline constexpr std::string_view kRiverfrontLayoutProfileId = "riverfront_city_layout";

// A terrain node a district claims.
struct LayoutAnchor {
    int node = 0;
    int x = 0;
    int z = 0;
    double distance_to_city_m = 0.0;  // along the walk the anchor search took
};

// One district feature: how many the profile asks for at this size, and how many nodes it got.
struct LayoutFeaturePlan {
    std::string name;
    int target_count = 0;
    int count = 0;  // == anchors.size()
    std::vector<LayoutAnchor> anchors;
};

struct RequiredAsset {
    std::string asset_id;  // a building pack asset id, e.g. building_temple_generic
    long long count = 0;
};

// The one published shape of `city_layout`, whether the generator planned it or the player
// authored it (settlement_api.hpp, layout "player"). An authored record says the profile is
// the player's, that no district was claimed and no asset drawn: no pack, no river reading,
// empty features and constraints, and `authored_by` "player".
struct CityLayout {
    struct River {
        int adjacent_river_edges = 0;            // the city node's neighbours that carry a river
        std::optional<double> river_distance_m;  // absent: no river anywhere, or none read
        bool bridge_recommended = false;
        int bridge_threshold = 0;
    };
    struct Constraints {
        double max_anchor_slope_degrees = 0.0;
        double river_buffer_m = 0.0;
        double min_river_distance_m = 0.0;  // the buffer again, under the name a reader checks
    };

    int version = kCityLayoutVersion;
    std::string layout_profile_id;
    std::string layout_profile_name;
    std::string population_profile;
    // The site's own catchment estimate, copied verbatim; it drives the district counts. It
    // was `residents` before version 2, which read as people standing in the city. Absent
    // only on a plan drawn before the city was sized, which no finished site keeps.
    std::optional<long long> population_estimate;
    River river;
    std::optional<std::string> building_pack_id;  // absent: no pack was drawn
    std::vector<RequiredAsset> required_assets;    // sorted by asset id
    long long required_asset_count = 0;           // the sum of the counts above
    int required_node_slots = 0;                  // nodes the building draw claimed
    bool asset_anchors_missing = false;           // the building draw ran short of anchors
    // The seven features, in the order the district pass walks them. Empty on an authored
    // record.
    std::vector<LayoutFeaturePlan> features;
    std::optional<Constraints> constraints;  // absent on an authored record, published `{}`
    // Why the plan fell back, when either pass ran short of anchors. The building pass's
    // reason overwrites the district pass's.
    std::optional<std::string> fallback;
    std::string authored_by;  // "player" on an authored record; empty on a generated one
};

// The pack a site drew: which, the seed it was drawn with -- which every re-plan of the
// city's layout draws from again -- and the pack catalogue's schema version.
struct BuildingPackDraw {
    std::string id;
    std::uint32_t seed = 0;
    int version = 0;
};

// What the pack chooser reads at a city's node.
struct PackGround {
    int biome = 0;             // the natural biome id
    std::string_view variant;  // the exact magical variant id at the cell; empty where none
    double height_m = 0.0;
    double slope_degrees = 0.0;
    double resource = 0.0;
    // Metres to fresh water, or -1 where none is reachable. No pack admits -1 -- every
    // pack's minimum is at least zero -- so such a city always takes the fallback pack.
    double freshwater_m = -1.0;
};

// Which pack a city of `people` draws on this ground: the packs whose criteria admit it,
// in catalogue order, walked cumulatively against ONE draw weighted by pack weight; the
// fallback pack when none does.
std::string_view pick_building_pack(std::uint32_t city_seed, std::string_view people, const PackGround& ground);
// The same over any catalogue, which is what lets a test build one.
std::string_view pick_building_pack(std::span<const BuildingPack> packs, std::string_view fallback_id,
                                    std::uint32_t city_seed, std::string_view people, const PackGround& ground);

// A pack by id, or the fallback pack for an id the catalogue does not hold.
const BuildingPack& building_pack_or_fallback(std::string_view id);

// A layout profile by id; nullptr for an unknown one.
const LayoutProfile* layout_profile(std::string_view id);

// Which profile a city's districts follow: the one its pack forces, when the catalogue
// holds it; otherwise the riverfront profile within 1.8 riverfront buffers of a river;
// otherwise the fallback profile.
const LayoutProfile& pick_layout_profile(const BuildingPack& pack, double river_distance_m);
// The same over any catalogue.
const LayoutProfile& pick_layout_profile(std::string_view forced_id, std::span<const LayoutProfile> profiles,
                                         std::string_view fallback_id, double river_distance_m);

// Distance from every node to the nearest river node over the node graph: an unconstrained
// multi-source search that crosses water. Infinity where the world has no river at all.
std::vector<double> river_distances(const SphereGrid& grid, const std::vector<double>& river);

// A node a district could stand on, with its walking distance from the city.
struct AnchorCandidate {
    double distance_m = 0.0;
    int node = 0;
};

// Dry land within the walking band of `start` whose slope is at most `max_slope_degrees`
// and which stands at least `min_river_distance_m` from a river, nearest first (node index
// breaks ties). The band is max(180, 4 * buffer + 180) metres, or 800 m for an infinite
// buffer. A node past it is reached but never collected or expanded.
std::vector<AnchorCandidate> land_anchor_candidates(const SphereGrid& grid, int start,
                                                    const std::vector<double>& river_distance_m,
                                                    const std::vector<double>& water,
                                                    const std::vector<double>& slope, double max_slope_degrees,
                                                    double min_river_distance_m);

// How many of something a city of `residents` gets: floor(base + per_100 * residents / 100),
// then the floor and the ceiling, never below zero. Unsized (`residents` absent) is `base`
// alone. A required option's floor is at least one.
int resolve_feature_count(const FeatureCount& count, std::optional<long long> residents, bool required = false);

// Whether a count applies to this people at all.
bool feature_fits(const FeatureCount& count, std::string_view people);

// Plan one city's layout. `candidates` are land_anchor_candidates for the city's node and
// this profile; `river` is one value per node, non-zero on a river; `pack` is the pack the
// city drew, or null for none (then no asset is drawn). `residents` absent plans the city
// unsized, as founding does before the population is split.
//
// ONE stream for the whole plan, named off `city_seed`: a rotation of the candidate list,
// then the building draw's starting cursor, then one draw per asset, in feature and option
// order. Both cursor draws are skipped when there is no candidate at all.
CityLayout plan_city_layout(std::uint32_t city_seed, int node, const LayoutProfile& profile, std::string_view people,
                            std::optional<long long> residents, double river_distance_m,
                            const std::vector<AnchorCandidate>& candidates, const SphereGrid& grid,
                            const std::vector<double>& river, const BuildingPack* pack);

}  // namespace fwg
