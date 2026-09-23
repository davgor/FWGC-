// The charged non-settlement places a finished world's terrain and history imply.
//
// This is a READER, not a stage. It runs after the sixteen-stage ladder has finished, it
// takes a request rather than a World, and it writes nothing back: the same seed produces
// byte-identical layers, settlements, ruins and history with the block present or absent.
// That is why it is a reader and not a seventeenth stage -- a stage would run inside the
// seeded pipeline and move every world that came before it.
//
// The design move is that a location is not a noun from a list. It is
// (archetype, tier, state, occupant, origin). "Forgotten fortress" is a fortress that is
// ruined and empty; "dungeon" is any tier-2 complex whose occupant is hostile. State and
// occupant are DERIVED from what the simulation already recorded -- whether a ruin or a
// nest sits nearby, how unstable the local weave is, how many ages have passed -- so the
// world does the authoring and nothing is hand-written as "forgotten".
//
// `tier` is PHYSICAL SCALE AND BUILD-OUT, never danger. `threat` (1-5) is danger, and it
// is deliberately the beast catalogue's scale so a consumer ranking a dungeon against a
// lair compares like with like instead of inventing a bridge between two private scales.
//
// TWO TRAPS THAT ARE SILENT IF YOU GET THEM WRONG:
//
//   * `radius_m` is the EFFECTIVE globe radius in metres (`world.effective.globe_radius`),
//     never the design-space one. They differ by the world-scale factor, and every metre
//     threshold in the catalogue is meaningless against the wrong one.
//   * The candidate cells here are NOT `sphere_grid`'s nodes. They run z in 1..n-2 and
//     x in 0..n-2, EXCLUDING both poles, because a pole is one degenerate node covering a
//     whole latitude band and a mausoleum on the north pole is a bug that looks like
//     content. The node NUMBERING is sphere_grid's, and this file uses sphere_grid for it
//     rather than restating the formula.
//
// Rounding: every rounded output field uses half-away-from-zero (`std::round` on the
// scaled value), not the reference's banker's rounding. Picked once, here, because two
// rounding rules in one library make two builds disagree on a tie.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fwg/grid.hpp"
#include "fwg/random.hpp"
#include "fwg/vec3.hpp"

namespace fwg {

// 2: a percentile requirement resolving to a cut that admits every cell in its own domain
// is refused rather than honoured. A consumer diffing counts across this boundary is
// reading two contracts, not two worlds.
inline constexpr int kKeyLocationsVersion = 2;
inline constexpr int kKeyLocationPlansVersion = 1;
inline constexpr int kKeyLocationInteriorVersion = 1;

// Multiples of the ACTUAL raster spacing, unlike archetype spacing below. Raising
// kRemoteCells empties fewer sites; lowering kNearRuinCells ages fewer.
inline constexpr double kNearRuinCells = 3.0;
inline constexpr double kNearNestCells = 2.0;
inline constexpr double kNearVillainCells = 3.0;
inline constexpr double kRemoteCells = 4.0;

// The top fifth of magic_hazard over land counts as an unstable weave.
inline constexpr double kUnstablePercentile = 0.8;

// The FIXED raster every `spacing_cells` and `settlement_clearance_cells` resolves
// against. Tied to the raster in hand, "two cells apart" would mean 25 km at size 17 and
// 6 km at size 129 -- the same world spacing its caves differently depending only on how
// finely it was sampled.
inline constexpr int kKeyLocationReferenceRaster = 65;

// The neutral score for a missing layer, a flat layer, or an archetype with no `prefers`.
inline constexpr double kKeyLocationDefaultScore = 0.5;
// The suitability multiplier is uniform on [1 - kKeyLocationJitter, 1 + kKeyLocationJitter).
// 0 is pure greed and piles every site on the best ridge; 1 makes the suitability field
// nearly irrelevant.
inline constexpr double kKeyLocationJitter = 0.35;

inline constexpr int kKeyLocationMinThreat = 1;
inline constexpr int kKeyLocationMaxThreat = 5;

// A 17 x 17 grid of posts over 16 cells. Both numbers are emitted and they are not the
// same number; `terrain.size` is the cell count and `terrain.surface.size` the post count.
inline constexpr int kKeyLocationSurfacePosts = 17;
// Guarantees a non-degenerate surface grid even for a single 1 x 0.8 m waystone.
inline constexpr double kKeyLocationMinHalfExtentM = 6.0;

inline constexpr double kKeyLocationGoldenAngle = 2.39996322972865332;
// The platform's edge band as a fraction of its radius.
inline constexpr double kKeyLocationPlatformEdge = 0.22;

// ---------------------------------------------------------------------------
// Closed vocabularies. An unknown value is a generation-time failure rather than a
// silent default, which is what these enums buy over the reference's strings.
// ---------------------------------------------------------------------------

enum class KeyLocationDomain : int { Land = 0, Water, Ocean, Lake, Any };
enum class KeyLocationPlacement : int { Node = 0, Path, Cluster };
enum class KeyLocationAlong : int { Road = 0, Frontier, Approach };
// The interior chamber graph's shape. Parallel to the plan-note table.
enum class KeyLocationPlan : int { Linear = 0, Branching, Radial, Warren, Vault };
enum class KeyLocationPartKind : int { Structure = 0, Terrain, Opening, Path };
enum class KeyLocationArrangement : int { Single = 0, Ring, Scatter, Row, Courtyard, Terrace, Mouth };
enum class KeyLocationShape : int { Mound = 0, Cut, Rim, Platform, Bank, Trench, Level };

// The eight states a location can be left in that count as untended. `active`, `occupied`
// and `restored` are NOT here, and that gate decides both the builders/descendants penalty
// and whether succession runs at all.
bool key_location_untended(std::string_view state);
// What `dungeons` counts and what feeds the tier-2 threat bump.
bool key_location_hostile(std::string_view occupant);

// ---------------------------------------------------------------------------
// The catalogue, generated from data/key-locations-catalogue.json,
// data/key-location-exteriors.json and data/key-locations-code-tables.json into
// src/generated/content_key_locations.cpp by tools/embed/key_locations.py.
// ---------------------------------------------------------------------------

// One `requires` term. A missing bound is UNBOUNDED on that side; `min: 0` is a live
// constraint that rejects negatives, so absence and zero cannot share a sentinel.
struct KeyLocationRequire {
    std::string_view layer;
    bool has_min; double min_value;
    bool has_max; double max_value;
    bool has_equals; double equals_value;
    bool has_not_equals; double not_equals_value;
    bool has_above_percentile; double above_percentile;
    bool has_below_percentile; double below_percentile;
    bool has_in;
    // True when either percentile key is present. A term with no percentile is exempt from
    // the "this cut admits the whole domain" refusal, and an authored 0.0 percentile is
    // meaningful (nearest-rank index 0), so this cannot be inferred from the value.
    bool has_percentile;
    std::span<const double> in_values;
};

struct KeyLocationPrefer {
    std::string_view layer;
    double weight;  // absent defaults to 1.0, never 0
    bool invert;
};

struct KeyLocationWeight {
    std::string_view name;
    double weight;
};

struct KeyLocationFamily {
    std::string_view id;
    std::string_view name;
    std::string_view glyph;
    int color[3];
    double family_spacing_cells;
};

struct KeyLocationArchetype {
    std::string_view id;
    std::string_view name;
    std::string_view family;
    int tier;
    KeyLocationDomain domain;
    KeyLocationPlacement placement;  // absent defaults to Node: 90 of 97 rely on it
    bool also_scatters;

    std::span<const KeyLocationRequire> requirements;
    std::span<const KeyLocationPrefer> prefers;
    std::span<const KeyLocationWeight> states;
    std::span<const KeyLocationWeight> occupants;
    // POSITIONAL: drawn as templates[randrange(len)], and the two filtered sublists
    // preserve this order.
    std::span<const std::string_view> names;

    double per_1000_km2;  // a RATE, never a count; zero is banned for a node placement
    double occurrence;    // absent defaults to 1.0

    // `spacing_m` beats `spacing_cells * reference_cell`; absence of both is 0, which is
    // then falsy-skipped, so here absence, zero and "rule off" really are the same thing.
    bool has_spacing_m; double spacing_m;
    double spacing_cells;
    bool has_settlement_clearance_m; double settlement_clearance_m;
    double settlement_clearance_cells;

    // Absent is UNBOUNDED, not zero. An authored max_count of 0 would clamp to zero.
    bool has_min_count; int min_count;
    bool has_max_count; int max_count;

    // Mandatory at tier 2, forbidden at tier 0, and SILENTLY IGNORED at tiers 1 and 3 --
    // `titan_skeleton` declares one that is never built. Ported as it stands.
    bool has_interior;
    KeyLocationPlan interior_plan;
    int interior_levels_low, interior_levels_high;
    int interior_chambers_low, interior_chambers_high;

    std::string_view reason;
    std::string_view note;  // authoring history; nothing reads it
};

struct KeyLocationSuccessionRule {
    std::string_view family;
    double chance;
    std::span<const KeyLocationWeight> occupants;
    std::string_view note;
};

struct KeyLocationChainSpec {
    std::string_view id;
    std::string_view name;
    std::string_view archetype;
    KeyLocationAlong along;
    // ABSOLUTE metres, deliberately unscaled: how far apart people set waystones is a fact
    // about people, so a larger world gets more of them, not longer gaps between them.
    double interval_m;
    bool line_of_sight;
    std::string_view anchor_family;
    int anchor_tier;  // absent defaults to 2
    bool has_max_chains; int max_chains;  // absent means unlimited, never zero
    std::string_view reason;
};

struct KeyLocationClusterSpec {
    std::string_view id;
    std::string_view name;
    std::string_view archetype;
    std::span<const std::string_view> anchor_kinds;
    std::span<const std::string_view> anchor_families;
    // EMPTY MATCHES ANY STATE, not none.
    std::span<const std::string_view> anchor_states;
    int count_low, count_high;
    double offset_near_m, offset_far_m;
    double chance;  // absent defaults to 1.0 and always fires
    std::string_view reason;
};

// An authored ground deformation. `radius_m` absent defaults to 1 METRE, not 0;
// `length_m` to radius * 2 and `width_m` to radius, so absence is never "no extent".
struct KeyLocationShaping {
    bool present;
    KeyLocationShape shape;
    bool has_radius; double radius_m;
    bool has_length; double length_m;
    bool has_width; double width_m;
    double height_m;
    double depth_m;
};

struct KeyLocationPart {
    std::string_view id;
    std::string_view name;
    KeyLocationPartKind kind;
    double plot_width_m, plot_depth_m;
    double dimension_width_m, dimension_depth_m, dimension_height_m;
    // Absent means the part does not move the ground AT ALL, which is distinct from an
    // authored `level` with height 0 -- that one means "flat, no offset".
    KeyLocationShaping shaping;
};

struct KeyLocationKitEntry {
    int part;  // index into key_location_parts()
    int count_low, count_high;  // absent is [1, 1]: ONE, not none
    bool has_arrangement;
    KeyLocationArrangement arrangement;
};

struct KeyLocationKit {
    // The lookup key: a BARE family name for a family kit, an archetype id for an
    // archetype kit. NOT the kit's own `id`, which is not unique -- iron_mine, silver_mine
    // and gem_mine all ship as `mine_head`.
    std::string_view key;
    std::string_view id;
    KeyLocationArrangement arrangement;
    bool has_spread_m; double spread_m;  // absent inherits 12 m, never 0
    // ORDER IS THE HARDEST CONTRACT IN THE CATALOGUE. See content_key_locations.cpp.
    std::span<const KeyLocationKitEntry> parts;
};

struct KeyLocationRoleSet {
    std::string_view family;
    std::span<const std::string_view> roles;
};

struct KeyLocationEntranceKind {
    std::string_view family;
    std::string_view kind;
};

struct KeyLocationThreatBase {
    std::string_view occupant;
    int threat;
};

std::span<const KeyLocationArchetype> key_location_archetypes();  // sorted by id
std::span<const KeyLocationFamily> key_location_families();
std::span<const KeyLocationSuccessionRule> key_location_succession();
// DOCUMENT order: the diagnostics tail walks chains then clusters in raw order.
std::span<const KeyLocationChainSpec> key_location_chain_specs();
std::span<const KeyLocationClusterSpec> key_location_cluster_specs();
std::span<const KeyLocationPart> key_location_parts();
std::span<const KeyLocationKit> key_location_family_kits();
std::span<const KeyLocationKit> key_location_archetype_kits();
std::span<const KeyLocationRoleSet> key_location_chamber_roles();
std::span<const std::string_view> key_location_fallback_roles();
std::span<const KeyLocationEntranceKind> key_location_entrance_kinds();
std::span<const std::string_view> key_location_adjectives();
std::span<const std::string_view> key_location_qualifiers();
std::span<const std::string_view> key_location_ordinals();
std::span<const KeyLocationThreatBase> key_location_threat_base();
std::string_view key_location_plan_note(KeyLocationPlan plan);
int key_location_catalogue_revision();
int key_location_exteriors_revision();
std::string_view key_location_method();
std::string_view key_location_limits();
std::string_view key_location_plans_method();
std::string_view key_location_plans_limits();
std::string_view key_location_interior_method();
std::string_view key_location_interior_limits();

// The archetype with this id, or nullptr.
const KeyLocationArchetype* key_location_archetype(std::string_view id);
const KeyLocationFamily* key_location_family(std::string_view id);
// Archetype kit first, family kit second, never nothing: all 97 resolve, 91 by archetype.
const KeyLocationKit* key_location_kit_for(std::string_view archetype_id, std::string_view family);

// ---------------------------------------------------------------------------
// The request. Everything this pass reads out of a finished world, named explicitly so a
// saved world.json and a live result are the same input.
// ---------------------------------------------------------------------------

// One record the world already published: a ruin, a city, a nest, a landmark, a claim.
// Every field is optional because the eleven blocks this reads publish different subsets,
// and an absent one has to stay absent rather than become a zero that reads as a position.
struct KeyLocationPlace {
    std::string id;    // uid where the block has one, else id
    std::string name;
    std::optional<int> node;
    std::optional<int> x, z;
    std::optional<Vec3> direction;
    int founded_age = 0;
    std::string source_culture;
    std::string civilization_id;
    std::optional<int> tier;  // beast nests only: creature tier 1-5
};

// A seated villain and the ground it took. Read asymmetrically ON PURPOSE: a fallen
// villain no longer stands anywhere, so its own seat is dropped, but its claims are kept,
// because ground a villain took stays taken and the places that grew around it do not
// become unremarkable because it died.
struct KeyLocationVillain {
    KeyLocationPlace seat;
    std::string status;  // empty reads as living
    std::vector<KeyLocationPlace> claims;
};

struct KeyLocationRoute {
    std::string from;  // '?' when the endpoint is missing; only the path KEY uses these
    std::string to;
    std::vector<int> nodes;
};

struct KeyLocationGod {
    std::string id;
    std::string name;
};

// `{id, name}` for the natural biomes and `{asset_id, name}` for the magical variants.
// The two are keyed DIFFERENTLY and that is the world's own rule, not a choice made here:
// natural by catalogue ID (a SPARSE space -- 0..8, then 13, 15, 16, 17), variants by ARRAY
// POSITION. Keying the natural map by position silently loses marsh, boreal forest, cold
// tundra and land ice while positions 9..12 hold names no layer value can ask for.
struct KeyLocationBiomeName {
    int id = 0;  // ignored for variants, which are positional
    std::string name;
    std::string asset_id;
};

struct KeyLocationRequest {
    std::uint64_t seed = 0;
    // Raster edge. Zero falls back to the height layer's own size.
    int n = 0;
    // EFFECTIVE globe radius, metres. See the header note.
    double radius_m = 0.0;
    // Zero falls back to 2*pi*R / max(1, n-1).
    double spacing_m = 0.0;
    // The final age count, = len(history.ages). Drives ageing, succession and ages_since.
    int ages = 0;

    // Every layer the world carries. Seven more are derived per call and merged over these
    // for the duration; a derived name wins a clash.
    const std::map<std::string, Grid>* layers = nullptr;

    std::vector<KeyLocationPlace> ruins;
    std::vector<KeyLocationPlace> cities;  // settlements.sites
    std::vector<KeyLocationPlace> hamlets;
    std::vector<KeyLocationPlace> fortresses;
    std::vector<KeyLocationPlace> ports;
    std::vector<KeyLocationPlace> colleges;
    std::vector<KeyLocationPlace> nests;
    std::vector<KeyLocationPlace> religion_sites;
    std::vector<KeyLocationPlace> landmarks;
    std::vector<KeyLocationVillain> villains;
    std::vector<KeyLocationRoute> routes;
    std::vector<KeyLocationGod> gods;
    // Indexed by the `dominant_magic` layer.
    std::vector<std::string> magic_school_order;
    std::vector<KeyLocationBiomeName> natural_biomes;
    std::vector<KeyLocationBiomeName> magical_biomes;
};

// ---------------------------------------------------------------------------
// The block.
// ---------------------------------------------------------------------------

struct KeyLocationOrigin {
    int age = 0;
    std::optional<std::string> culture_id;
    std::optional<std::string> civilization_id;
    // Gated on near_ruin exactly as age and culture_id are: a site far from every ruin
    // names none. The port specification claims otherwise and is wrong; `_origin` is a
    // module-level function whose parameter IS the gated value.
    std::optional<std::string> ruin_id;
    std::optional<std::string> school;
    int ages_since = 0;
};

struct KeyLocationEnvironment {
    std::optional<int> natural_biome;
    std::string natural_biome_name;
    std::optional<int> biome_variant;
    std::string variant_asset_id;
    std::string variant_name;
    std::optional<double> temperature_c;
    std::optional<double> moisture;
    std::optional<double> slope;
    bool coastal = false;  // a real boolean, not a tri-state
};

struct KeyLocationSuccessionRecord {
    std::string from_occupant;
    std::string to_occupant;
    std::string note;
};

struct KeyLocationChamber {
    std::string id;
    int depth = 0;
    std::string role;
    double size_w_m = 0.0, size_d_m = 0.0;
    std::vector<std::string> connects;
    bool flooded = false;
    bool collapsed = false;
};

struct KeyLocationEntrance {
    std::string id;
    std::string chamber;
    std::string kind;
    bool hidden = false;
};

struct KeyLocationInterior {
    int version = kKeyLocationInteriorVersion;
    KeyLocationPlan plan = KeyLocationPlan::Linear;
    std::string_view plan_note;
    int levels = 0;
    std::vector<KeyLocationChamber> chambers;
    std::vector<KeyLocationEntrance> entrances;
};

struct KeyLocationLinks {
    std::string chain;
    int chain_index = -1;  // -1 is absent, and is also what the final sort uses
    std::string cluster;
    std::string anchor;
};

// One scored preference term as it reached the output. Built in `prefers` order with
// last-wins on a repeated layer key, so this is a list rather than a map.
struct KeyLocationFactor {
    std::string layer;  // the layer name, plus "_low" when the term inverts
    double value = 0.0;
};

struct KeyLocationSite {
    std::string id;
    KeyLocationPlacement placement = KeyLocationPlacement::Node;
    std::string kind;  // the archetype id
    std::string name;  // unique in the world
    std::string family;
    int tier = 0;
    std::string state;
    std::string occupant;
    int node = 0;
    int x = 0, z = 0;
    Vec3 direction{};
    double height_m = 0.0;
    std::string_view layer = "surface";
    KeyLocationDomain domain = KeyLocationDomain::Land;
    double suitability = 0.0;
    std::vector<KeyLocationFactor> suitability_factors;
    KeyLocationOrigin origin;
    KeyLocationEnvironment environment;
    KeyLocationLinks links;
    std::string asset_id;  // marker.key_location.<kind>
    std::string_view reason;
    std::optional<KeyLocationSuccessionRecord> succession;
    int threat = 1;
    std::optional<KeyLocationInterior> interior;
};

struct KeyLocationChain {
    std::string id;
    std::string kind;  // the spec id
    std::string_view name;
    std::string_view archetype;
    KeyLocationAlong along = KeyLocationAlong::Road;
    double interval_m = 0.0;
    double length_m = 0.0;
    std::string anchor;
    std::vector<std::string> members;  // site ids in WALK ORDER
    std::string_view reason;
};

struct KeyLocationCluster {
    std::string id;
    std::string kind;
    std::string_view name;
    std::string anchor;
    std::string anchor_kind;
    std::string anchor_name;
    std::vector<std::string> members;
    std::string_view reason;
};

// One row PER PASS, not per archetype: an also_scatters archetype yields two. Rows do not
// sum to the site count; `sites` is ground truth.
struct KeyLocationDiagnostic {
    std::string archetype;
    int placed = 0;
    int wanted = 0;
    int candidates = 0;
    std::string reason;
};

struct KeyLocationSummary {
    int sites = 0;
    std::map<int, int> by_tier;
    std::map<std::string, int> by_family;
    std::map<std::string, int> by_state;
    std::map<std::string, int> by_occupant;
    // tier >= 2 and a hostile occupant. This is where dungeons come from: no archetype is
    // called a dungeon, and none needs to be.
    int dungeons = 0;
};

struct KeyLocationBlock {
    int version = kKeyLocationsVersion;
    bool ok = false;
    std::string error;  // set only when !ok
    int catalogue_revision = 0;
    int catalogue_count = 0;  // archetypes, not sites
    KeyLocationSummary summary;
    std::vector<KeyLocationSite> sites;
    std::vector<KeyLocationChain> chains;
    std::vector<KeyLocationCluster> clusters;
    std::vector<KeyLocationDiagnostic> diagnostics;
    // `families` is the static key_location_families() span rather than a copy: the
    // reference republishes the catalogue's own map here, and a second copy of an
    // immutable table is a second thing to keep in step.
    std::string_view method;
    std::string_view limits;
};

// ---------------------------------------------------------------------------
// The plans, a SEPARATE top-level block deliberately: the city, hamlet and castle plan
// blocks are all top-level, and a consumer scanning for plan blocks would not find these
// nested inside a sited-feature block.
// ---------------------------------------------------------------------------

// A part's authored shaping plus where it ended up. Emitted as a parametric primitive
// because a sampled grid at three metres a post cannot describe a barrow; the grid beside
// it is the derived form, and where the two disagree the primitive is right.
struct KeyLocationShapingPlacement {
    KeyLocationShaping spec;
    double x_m = 0.0, z_m = 0.0;
    // Always the placement's rotation. An authored `shaping.rotation_degrees` would be
    // overwritten here before anything read it -- a latent defect in the reference, kept
    // because no shipped part authors one and "fixing" it would rotate banks and trenches.
    double rotation_degrees = 0.0;
    std::string_view part;
};

struct KeyLocationPlot {
    std::string id;  // plot-<k>, positional in kit order
    // The PART id. Named building_id so the city-plot importer needs no second path.
    std::string_view building_id;
    std::string_view name;
    // From the KIT ENTRY, never the part: no shipped entry authors one, so every plot
    // reads `structure` and only the appended weathering entry reads `terrain`. The asset
    // list uses the PART's kind instead, so the two consumers disagree. Reproduced.
    KeyLocationPartKind kind = KeyLocationPartKind::Structure;
    int phase = 0;
    double x_m = 0.0, z_m = 0.0;
    double rotation_degrees = 0.0;
    double ground_elevation_m = 0.0;   // highest of the four rotated corners
    double foundation_bottom_m = 0.0;  // lowest of the four
    double plot_width_m = 0.0, plot_depth_m = 0.0;
    double dimension_width_m = 0.0, dimension_depth_m = 0.0, dimension_height_m = 0.0;
    double road_access_x_m = 0.0, road_access_z_m = 0.0;
    int workers = 0;
    int beds = 0;
};

struct KeyLocationSurface {
    int size = kKeyLocationSurfacePosts;
    double step_m = 0.0;
    // size x size, rows indexed by j (the z axis).
    std::vector<std::vector<double>> heights_m;
};

struct KeyLocationTerrain {
    double cell_m = 0.0;
    int size = kKeyLocationSurfacePosts - 1;
    double bounds_m[4] = {0.0, 0.0, 0.0, 0.0};
    std::vector<KeyLocationShapingPlacement> shaping;
    double raised_m = 0.0;
    double lowered_m = 0.0;
    KeyLocationSurface surface;
};

struct KeyLocationSitePlan {
    std::string location_id;  // joins key_locations.sites[].id
    std::string kind;
    std::string family;
    int tier = 0;
    std::string_view kit_id;
    int version = kKeyLocationPlansVersion;
    bool complete = false;  // 'complete' when it has plots, 'empty' when it does not
    KeyLocationArrangement arrangement = KeyLocationArrangement::Single;
    Vec3 origin_direction{};
    double origin_height_m = 0.0;
    int origin_node = 0;
    // extent * 2 in each direction, which is NOT terrain.bounds_m: that one floors at
    // kKeyLocationMinHalfExtentM, so for a lone waystone the two describe different
    // rectangles. Both are emitted and the reference does not reconcile them.
    double bounds_width_m = 0.0, bounds_depth_m = 0.0;
    double slope_bearing_degrees = 0.0;
    KeyLocationTerrain terrain;
    std::vector<KeyLocationPlot> plots;
    int stat_plots = 0;
    int stat_parts = 0;  // distinct building_id
    int stat_shaping = 0;
};

struct KeyLocationPlanBlock {
    int version = kKeyLocationPlansVersion;
    bool ok = false;
    std::string error;
    int catalogue_revision = 0;
    int part_count = 0;
    int kit_count = 0;
    std::string_view unit = "metres";
    std::string_view coordinates =
        "local to each location; place the frame at the location direction and height";
    int summary_plans = 0;
    int summary_plots = 0;
    double summary_plots_per_plan = 0.0;
    std::vector<KeyLocationSitePlan> plans;
    std::string_view method;
    std::string_view limits;
};

// ---------------------------------------------------------------------------
// Entry points. NEITHER THROWS: a failure is a reported block with `ok == false` and an
// error string, never a crashed world. That is the contract the generator depends on.
// ---------------------------------------------------------------------------

KeyLocationBlock generate_key_locations(const KeyLocationRequest& request);

// Returns a block with `ok == false` and an empty plan list when `block` is not ok.
KeyLocationPlanBlock generate_key_location_plans(const KeyLocationRequest& request,
                                                 const KeyLocationBlock& block);

// ---------------------------------------------------------------------------
// The pieces worth pinning on their own. Every one of these is a rule a test can state
// without generating a world, and several are rules a plausible wrong implementation gets
// subtly wrong rather than loudly.
// ---------------------------------------------------------------------------

// NEAREST-RANK, so the answer is always a real sample and never an interpolation:
// sort, index = round(q * (len - 1)), clamped. `values` is taken by value and sorted.
double key_location_percentile(std::vector<double> values, double q);

// Metres from every cell to the nearest source, by weighted cell-step sweep over the eight
// neighbours. A FIFO SPFA, not Dijkstra: a cell may be pushed more than once, and the
// weighted-step relaxation produces slightly non-Euclidean distances that every threshold
// in the catalogue was tuned against. WITH NO SOURCES every cell reads the ceiling, which
// makes a `prefers` term neutral rather than undefined -- a world with no roads should not
// make every cell equally GOOD for a tollhouse, it should make the road term carry no
// information.
Grid key_location_distance_field(const std::vector<std::pair<int, int>>& sources, int n,
                                 double spacing_m, std::optional<double> ceiling_m = std::nullopt);

// 1 where owners differ across a step, 0 deep inside. Unowned ground (-1) does not count
// as a different owner, or every coastline would read as a political border.
Grid key_location_frontier_field(const Grid* culture_region, int n);

// The BEST value of a water layer among a land cell's neighbours; 0 inland and at sea.
// Max rather than a mean on purpose: a station is sited for the richest water in reach of
// a boat, and averaging over eight neighbours would penalise exactly the ragged inlet a
// whaler wants.
Grid key_location_coastal_water_field(const Grid* source, const Grid* water_type, int n);

// The nearest cell to an arbitrary direction. Clamps z and takes x mod (n-1), so a chain
// member CAN land on a pole row even though no candidate cell ever does.
std::pair<int, int> key_location_cell_of_direction(const Vec3& point, int n);

// Walk `distance_m` from `origin` along a great circle. `bearing` is RADIANS CLOCKWISE
// FROM LOCAL NORTH -- a from-east implementation puts every cluster member on a different
// arc, and this convention is NOT the one `slope_bearing` uses.
Vec3 key_location_offset(const Vec3& origin, double bearing, double distance_m, double radius);

// Great-circle interpolation, falling back to the first endpoint below 1e-9 of angle.
Vec3 key_location_slerp(const Vec3& a, const Vec3& b, double t);

// The height one shaping entry adds at a local point. `level` returns 0 here: flattening
// cannot be expressed as a delta independent of the terrain, so it is applied against the
// base instead.
double key_location_shaping_delta(const KeyLocationShapingPlacement& entry, double x, double z);

// Levelled first, then every additive shape in LIST ORDER. Float addition is not
// associative, so that order is numerically load-bearing.
double key_location_shaped_height(std::span<const KeyLocationShapingPlacement> entries,
                                  double x, double z, double base, double centre_base);

// What the world did to one patch of ground, as the five facts that bend a site. Published
// because state, occupant and threat are the interesting half of this package and they are
// a pure function of (archetype, this, stream) -- a caller can ask what a ruined fortress
// beside a dragon lair would become without generating a world to put one in.
struct KeyLocationContext {
    bool near_ruin = false;
    bool near_nest = false;
    bool remote = false;
    bool unstable = false;
    int ages = 0;
    // The CREATURE tier of a nest on this node, 1-5. Absent is not zero: a zero would read
    // as a nest that raises nothing.
    std::optional<int> nest_tier;
    bool near_villain = false;
};

// How many of this archetype the land supports, as a thinned draw. Consumes EXACTLY ONE
// random() whatever the outcome. Floors and draws for the remainder rather than rounding:
// on a small world every expectation falls below a half and the whole catalogue rounds to
// zero at once, and the world reports "too small to support one" ninety times.
int key_location_budget(const KeyLocationArchetype& archetype, double land_km2, Rng& draw);

// Weighted pick over the archetype's state weights, bent by what the world did. A
// water-domain archetype that declares `drowned` returns it WITHOUT SPENDING A DRAW, which
// is a genuine branch in the stream length rather than a shortcut.
std::string_view key_location_derive_state(const KeyLocationArchetype& archetype,
                                           const KeyLocationContext& context, Rng& draw);

std::string_view key_location_derive_occupant(const KeyLocationArchetype& archetype,
                                              std::string_view state,
                                              const KeyLocationContext& context, Rng& draw);

// An integer 1-5 on the creature-tier rubric, deliberately the beast catalogue's scale so
// a consumer ranking a dungeon against a lair compares like with like.
int key_location_threat(const KeyLocationSite& site, const KeyLocationContext& context);

// The world facts a name template can draw on. An EMPTY field resolves to authored
// English -- "the waste", "the old people", "unquiet", "the nameless", "lone" -- rather
// than to a hole in the name.
struct KeyLocationNameFacts {
    std::string near;
    std::string culture;
    std::string school;
    std::string god;
    std::string ordinal;
};

// One name from the archetype's own templates, filled from those facts.
//
// SPENDS TWO OR THREE WORDS, and the third is the interesting one: the template pick, an
// optional redraw when the chosen template needs a `{near}` the caller has not got, and
// ALWAYS an adjective. The `{adj}` draw fires whether or not the template uses the token,
// so an archetype whose templates are mixed keeps ONE stream length rather than two.
// Making it lazy looks like an optimisation and moves every later draw for the site.
std::string key_location_name_for(const KeyLocationArchetype& archetype,
                                  const KeyLocationNameFacts& facts, Rng& draw);

// The chamber graph for one tier-2 complex, or nothing when the archetype declares none.
// The CALLER gates on tier == 2; this does not, which is why titan_skeleton's declared
// interior is legal, dead and reachable only from here.
std::optional<KeyLocationInterior> key_location_build_interior(const KeyLocationArchetype& archetype,
                                                               std::string_view state, Rng& draw);

}  // namespace fwg
