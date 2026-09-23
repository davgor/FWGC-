// What a settlement actually contains, and where each building stands.
//
// Stage 16 takes a pin on the world raster — a city site or a hamlet — and packs it as
// a schematic town plan in LOCAL METRES: a square raster of buildable ground centred on
// the pin, a seeded street network grown over that raster, an optional defensive
// enceinte, and a set of non-overlapping rotated building plots drawn from the
// civilization's preset, each with street frontage, an access corridor, a ground
// elevation and a rotation.
//
// It exists because a settlement site says only WHERE a town is and who lives in it. A
// plan is the only record that says which buildings it has, where each one stands, who
// staffs it and how many beds were built for those staff.
//
// The planner is deliberately schematic and says so in the warnings it emits on every
// plan: it resolves no groundwater, no structural feasibility and no flood extent.
//
// Three things about the shape are load-bearing and were learned from the reference:
//
//   * **Preset order decides the plan, and no random number is involved.** Rows are
//     installed greedily into the first free candidate plot and plot ids are positional,
//     so reordering a preset moves buildings and renumbers every plot after them.
//   * **The shape catalogue is hashed WHOLE and the digest is an RNG input.** Dropping a
//     citation from it changes which shape every city in every world is given. See
//     city_shape_catalogue_identity().
//   * **A condition the planner has not measured is FALSE.** That is an honest "unknown",
//     not a claim that the condition is unmet, and it is why most of the placement
//     vocabulary rejects rather than admits.
//
// WHAT A PLAN DELIBERATELY DOES NOT CARRY, and where to find it instead. The reference
// copies several catalogues into EVERY plan so that a JSON consumer needs no second
// file; a C++ consumer already has them, and duplicating them per settlement is
// megabytes of identical bytes:
//
//   * the biome and magical-variant catalogues and the magic school colours —
//     `natural_biomes()`, `biome_variant_catalogue()` and the world's magic report;
//   * the terrain-detail definition and the two fixed strings beside the elevation
//     surface — `World::terrain_detail`;
//   * the projection formula, which is the direction/position pair written out in
//     LocalSampler and reproduced by any consumer from `reference_frame`;
//   * the registry identity half of the saved-world gate. Only the shape catalogue's
//     digest is published here (see `CityPlans::shapes_sha256`); the civilization
//     registry's digest is over a merged document this library does not yet assemble,
//     so a world saved from this port can be checked against the shape catalogue and
//     NOT against the building registry.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fwg/settlement.hpp"  // CityClass
#include "fwg/vec3.hpp"

namespace fwg {

struct World;
struct SphereGrid;

// ===========================================================================
// Content: the building registry (data/buildings.json)
// ===========================================================================

struct PlotSize {
    double width = 0.0;
    double depth = 0.0;
};

struct BoxSize {
    double width = 0.0;
    double depth = 0.0;
    double height = 0.0;
};

struct Clearance {
    double front = 0.0;
    double rear = 0.0;
    double left = 0.0;
    double right = 0.0;
};

enum class StructurePriority : int { core = 0, conditional, specialist };
std::string_view to_string(StructurePriority priority);

// One post on a building's roster. Only `target` is read by the planner: it is the
// number of DISTINCT people across all shifts, which is what a bed has to be built for.
struct StaffingRole {
    std::string_view role;
    int minimum = 0;
    int target = 0;
    int maximum = 0;
};

// A measured building type. Ids are unique across all three libraries, which is what
// lets a plan row name one without also naming its library.
struct Structure {
    std::string_view id;
    std::string_view name;
    std::string_view library;  // common | rural | castle
    std::string_view block;    // the catalogue block it belongs to
    StructurePriority priority;
    std::string_view module_family;
    std::string_view geometry_type;
    BoxSize dimensions_m;
    // plot = dimensions + clearances is a registry invariant, not a computation the
    // planner redoes. It is the rectangle that must not overlap anything.
    PlotSize plot_m;
    Clearance clearance_m;
    std::span<const StaffingRole> staffing;
    int workers = 0;  // sum of role targets: ONE instance's roster
};

std::span<const Structure> structures();
// By id. Returns nullptr for an unknown one rather than a default-constructed row: a
// building that does not exist has no dimensions, and zero is a lie about that.
const Structure* structure(std::string_view id);

// Worker housing, which is not a preset row: the planner adds dwellings itself, as many
// as the staff it has already placed need.
struct HousingProfile {
    std::string_view key;  // worker_house | worker_apartment | hamlet_house
    std::string_view id;   // building.worker_house — what a plot records
    std::string_view name;
    BoxSize dimensions_m;
    PlotSize plot_m;
    int worker_beds = 0;
};
std::span<const HousingProfile> housing_profiles();
// Throws std::invalid_argument for an unknown key.
const HousingProfile& housing_profile(std::string_view key);

// --- building packs --------------------------------------------------------
//
// These are read by settlement assembly rather than by the planners, but the three
// ORDER CONTRACTS below are the reason the tables exist in this shape at all.

struct AssetChoice {
    std::string_view asset_id;
    double weight = 0.0;
};

// How many of something a city of a given size gets. A bare integer in the data means
// `base` alone.
struct FeatureCount {
    double base = 0.0;
    std::optional<double> per_100_residents;
    std::optional<double> max;
    // Peoples this feature applies to. Empty means all of them.
    std::span<const std::string_view> profiles;
};

struct BuildingOption {
    std::string_view id;
    std::string_view name;
    FeatureCount count;
    bool required = false;
    std::string_view placement;
    std::span<const std::string_view> tags;
    // ORDER CONTRACT: walked cumulatively against ONE draw. Append, never insert.
    std::span<const AssetChoice> asset_choices;
};

struct PackCriteria {
    std::span<const int> biomes;
    std::span<const std::string_view> biome_variants;
    std::optional<double> freshwater_distance_m_min;
    std::optional<double> freshwater_distance_m_max;
    std::optional<double> height_min_m;
    std::optional<double> slope_max_degrees;
    std::optional<double> resource_min;
    std::optional<double> resource_max;
};

struct BuildingPack {
    std::string_view id;
    std::string_view name;
    std::string_view layout_profile_id;
    double weight = 0.0;
    PackCriteria criteria;
    // ORDER CONTRACT, twice over: the options share one rng stream and each consumes a
    // variable number of draws, and an anchor cursor round-robins over the city's nodes
    // per option. An insertion moves every later option's asset AND the node it lands on.
    std::span<const BuildingOption> building_options;
};

// ORDER CONTRACT: filtered, then walked cumulatively against one draw, with no sort in
// the path. Swapping two packs hands a different pack to the same city.
std::span<const BuildingPack> building_packs();
std::string_view fallback_building_pack_id();

struct LayoutFeature {
    std::string_view name;
    FeatureCount count;
};

struct LayoutPlacement {
    double river_buffer_m = 0.0;
    int river_fork_bridge_threshold = 0;
    bool bridge_if_river_fork = false;
    double max_anchor_slope_degrees = 0.0;
};

struct LayoutProfile {
    std::string_view id;
    std::string_view name;
    // The profile's OWN features, in its own order. The reference iterates a hard-coded
    // seven-name tuple here instead, which silently drops an eighth feature; the codegen
    // refuses an unknown name rather than reproducing that.
    std::span<const LayoutFeature> features;
    LayoutPlacement placement;
};
std::span<const LayoutProfile> layout_profiles();
std::string_view fallback_layout_profile_id();
// What a feature falls back to when a profile omits it. Data, so a consumer reads the
// fallback instead of restating it.
std::span<const LayoutFeature> layout_feature_defaults();

// ===========================================================================
// Content: the settlement plans (data/settlement-plans.json)
// ===========================================================================

// The closed vocabulary. An unknown condition is refused when the tables are generated.
enum class PlacementCondition : int {
    usable_groundwater = 0,
    water_collection_or_delivery,
    navigable_shore,
    flowing_water,
    reliable_wind,
    ore_and_fuel_supply,
    food_surplus,
    working_animals,
    clay_and_fuel_supply,
    medicinal_supply,
    clear_sky_view,
    isolation_access,
    defended_perimeter,
    route_crossing,
    grade_change,
    retention_needed,
    farming_support,
    resource_support,
};
inline constexpr int kPlacementConditionCount = 18;
std::string_view to_string(PlacementCondition condition);

// What a site can offer, as far as the planner has actually measured it.
//
// Every unset condition is false. That is deliberate and it is not the same as "unmet":
// the planner resolves no groundwater and no wind, so a building that needs either is
// recorded as an unverified prerequisite rather than built on a guess.
class ConditionSet {
public:
    bool operator[](PlacementCondition condition) const {
        return (bits_ >> static_cast<int>(condition) & 1u) != 0u;
    }
    void set(PlacementCondition condition, bool value) {
        const std::uint32_t bit = 1u << static_cast<int>(condition);
        bits_ = value ? (bits_ | bit) : (bits_ & ~bit);
    }

private:
    std::uint32_t bits_ = 0;
};

// One row of a settlement programme: the merge the reference performs at load,
// `{**structure, **row}`. The row supplies the name, the count and the conditions; the
// structure supplies priority, plot, dimensions and staffing. Stored as an index rather
// than a copy so the geometry has exactly one home.
struct PresetRow {
    std::uint16_t structure_index = 0;
    // The row's own name. Empty on an infrastructure row, which authors none.
    std::string_view name;
    // 0 where the row authors none — infrastructure rows carry a quantity mode instead.
    // That means "none was authored", not "build none": the fortifier reads a wall's
    // dimensions and never its count.
    int count = 0;
    // SOURCE ORDER. The "Unverified prerequisite" reason lists them in it.
    std::span<const PlacementCondition> placement_conditions;

    const Structure& structure() const;
    std::string_view display_name() const;
};

struct SettlementPreset {
    // ORDER CONTRACT, with no rng anywhere near it. Rows are installed greedily into the
    // first free plot in this order and plot ids are positional.
    std::span<const PresetRow> buildings;
    std::span<const PresetRow> infrastructure;
};

enum class SettlementBlock : int { small_city = 0, medium_city, capital_city, hamlet };

struct CivilizationPresets {
    std::string_view id;
    std::string_view parent_race_id;
    std::span<const SettlementPreset> blocks;  // in SettlementBlock order
};

// The registry's civilization_order. Append, never insert.
std::span<const CivilizationPresets> civilization_presets();
// Throws std::invalid_argument for an unknown civilization.
const SettlementPreset& settlement_preset(std::string_view civilization_id, SettlementBlock block);
SettlementBlock city_block_for(CityClass city_class);
std::string_view city_class_name(CityClass city_class);

// ===========================================================================
// Content: the city shape catalogue (data/city-shapes.json)
// ===========================================================================

// The CLOSED vocabulary of a site description: a rule naming anything outside it is
// refused when the tables are generated, and a site carrying anything outside it is a
// programming error. The planner supplies seven of these twenty-one; the other fourteen
// are the extension point a future footprint-analysis adapter fills in.
enum class ShapeFeature : int {
    coast_access = 0,
    navigable_water,
    river_access,
    river_bend,
    bridge_feasible,
    island,
    peninsula,
    lagoon,
    ridge,
    hill,
    oasis,
    usable_land_fraction,
    corridor_constraint,
    aridity,
    wooded_fraction,
    defense_priority,
    regional_threat,
    planned_foundation,
    trade_intensity,
    local_slope_degrees,
    buildable_area_m2,
};
inline constexpr int kShapeFeatureCount = 21;
std::string_view to_string(ShapeFeature feature);

struct ShapeRule {
    ShapeFeature field = ShapeFeature::coast_access;
    double min_value = 0.0;
    double max_value = 0.0;
    double equals_value = 0.0;  // a boolean feature is 1.0 / 0.0
    bool has_min = false;
    bool has_max = false;
    bool has_equals = false;
};

struct ShapePreference {
    ShapeRule when;
    double bonus = 0.0;
};

struct ShapeVariation {
    std::string_view key;
    double min_value = 0.0;
    double max_value = 0.0;
    // An integer interval is INCLUSIVE of both endpoints.
    bool integer = false;
};

// One line of a shape's authored design guidance. Prose, never interpreted — but it is
// inside the catalogue digest, so it already decides which shape every city is given.
struct ShapeLayoutNote {
    std::string_view key;
    std::string_view text;
};

struct CityShape {
    std::string_view id;
    std::string_view name;
    std::string_view family;
    // False is a period gate, not a disable flag: the shape is offered only to a caller
    // that asks for later ones.
    bool enabled_by_default = true;
    std::uint32_t city_classes = 0;  // bit 0 small, bit 1 medium, bit 2 capital
    double base_weight = 0.0;
    std::span<const ShapeRule> eligibility;   // every rule must hold
    std::span<const ShapePreference> preferences;
    std::span<const ShapeVariation> variation;  // sorted by key: the draw order
    std::span<const std::string_view> source_ids;
    // In the shape's OWN key order, and however many keys it has. One shape carries an
    // eighth note; a fixed seven-field record would drop it the way the reference's
    // hard-coded layout-feature tuple drops a feature.
    std::span<const ShapeLayoutNote> layout;
};

// What a site feature is allowed to be. The catalogue is a VALIDATION table, not
// documentation: a value outside its declared range stops shape selection rather than
// quietly skewing every draw in the world.
struct ShapeFeatureSpec {
    bool is_boolean = false;
    double min_value = 0.0;
    double max_value = 0.0;
};
// In ShapeFeature order.
std::span<const ShapeFeatureSpec> shape_feature_specs();

// SORTED BY ID, which is the ranking order and therefore the order the cumulative
// weighted walk subtracts in. The catalogue's list order is not a contract; this is.
std::span<const CityShape> city_shapes();
int city_shape_catalogue_revision();
double shape_repetition_penalty();

// sha256 over the catalogue's canonical JSON, recomputed from the embedded document.
//
// THIS IS AN RNG INPUT, not a checksum. Every per-city draw is salted with it, so the
// citations, the research date and the prose in each shape's `layout` all decide which
// shape a city is given. data/city-shapes.json records the expected value and the
// codegen refuses a mismatch.
const std::string& city_shape_catalogue_identity();

// A site description. A feature that was never set is ABSENT, and an absent feature
// fails every rule that names it — a hard requirement is not met and no preference
// bonus is earned. That is what makes the fourteen unmeasured features safe to leave out.
class ShapeSite {
public:
    void set(ShapeFeature feature, double value);
    void set_flag(ShapeFeature feature, bool value) { set(feature, value ? 1.0 : 0.0); }
    bool has(ShapeFeature feature) const {
        return (present_ >> static_cast<int>(feature) & 1u) != 0u;
    }
    double value(ShapeFeature feature) const { return values_[static_cast<int>(feature)]; }

private:
    double values_[kShapeFeatureCount] = {};
    std::uint32_t present_ = 0;
};

struct ShapeCandidate {
    std::string_view id;
    std::string_view family;
    double weight = 0.0;
    std::vector<ShapeFeature> matched_preferences;
};

struct ShapeParameter {
    std::string_view key;
    double value = 0.0;
};

struct ShapeSelection {
    int version = 1;
    int catalogue_revision = 0;
    std::string catalogue_sha256;
    std::string_view shape_id;  // empty when nothing was eligible, or the city is authored
    std::string_view family;
    std::vector<ShapeParameter> parameters;
    std::vector<ShapeCandidate> candidates;
    std::span<const std::string_view> source_ids;
    // The chosen shape's design guidance. On a PLAYER-AUTHORED town this and the
    // candidate list are the ones the generator WOULD have picked, kept deliberately
    // while `shape_id` is cleared — so a consumer must not read them as describing the
    // town the player laid out.
    std::span<const ShapeLayoutNote> layout;
    std::string reason;
    bool runtime_geometry_generated = false;

    double parameter(std::string_view key, double fallback) const;
};

// Eligible weighted alternatives in stable id order — not a forced winner.
std::vector<ShapeCandidate> rank_shapes(const ShapeSite& site, CityClass city_class,
                                        const std::map<std::string_view, int>& nearby_counts,
                                        bool include_later = false);

// `nearby_counts` is how many neighbours already wear each shape; it divides the weight
// so a region does not become one repeated pattern.
ShapeSelection select_shape(const ShapeSite& site, std::uint32_t seed, std::string_view city_id,
                            CityClass city_class,
                            const std::map<std::string_view, int>& nearby_counts,
                            bool include_later = false);

// ===========================================================================
// Geometry
// ===========================================================================

// (i, j): column then row. Every cell set here is keyed and ordered on this pair, i
// first, which is the order plot placement and street tracing both break ties on.
using Cell = std::pair<int, int>;

struct Point {
    double x = 0.0;
    double z = 0.0;
};

// A size x size membership mask.
//
// A bitset rather than a hash set because the candidate sweep asks "is this cell free"
// several million times per world. Anything OFF the raster is simply not a member,
// which is what lets a plot hanging over the edge be rejected by the same subset test
// that rejects one on a lake.
class CellSet {
public:
    CellSet() = default;
    explicit CellSet(int size);

    int size() const { return size_; }
    std::size_t count() const { return count_; }
    bool empty() const { return count_ == 0; }

    bool contains(Cell cell) const;
    bool insert(Cell cell);
    bool erase(Cell cell);

    bool contains_all(const std::vector<Cell>& cells) const;
    bool intersects(const std::vector<Cell>& cells) const;
    void insert_all(const std::vector<Cell>& cells);
    void erase_all(const std::vector<Cell>& cells);

    // Ascending (i, j), i primary. NOT storage order: storage is row-major, so this
    // walks columns outside rows on purpose.
    std::vector<Cell> sorted() const;

private:
    int size_ = 0;
    std::size_t count_ = 0;
    std::vector<char> in_;  // [j * size + i]
};

// The four corners of a rotated rectangle, anticlockwise from the near-left.
std::vector<Point> corners(double x, double z, double w, double d, double angle);

// Conservative rotated-rect rasterization: a cell is in the footprint if the cell SQUARE
// overlaps the rectangle, not merely if its centre does.
//
// Cells outside [0, size) are produced and NOT clamped. That is deliberate — a plot
// hanging over the raster edge yields cells that are not in the valid set, so the subset
// test rejects it. Clamping here silently admits plots off the edge of the world.
std::vector<Cell> footprint_cells(double x, double z, double w, double d, double angle,
                                  double half, double cell = 4.0);

// Chebyshev cell distance from each cell to the nearest cell that is NOT free ground,
// where the raster edge counts as not free. Blocked cells hold 0. Indexed [j*size + i].
//
// Two sweeps once per settlement let the candidate sweep decide most positions without
// rasterizing anything: roughly four candidate positions in five are rejected because
// the plot overlaps blocked ground or a street.
std::vector<int> clearance_field(const CellSet& free, int size);

// The STREETS inside one city, not the roads between cities -- roads.hpp owns
// RoadNetwork for the latter, and the two were colliding.
struct StreetNetwork {
    CellSet cells;                         // the widened street cells
    std::vector<std::vector<Cell>> paths;  // centrelines, each running outward from the root
};

// A seeded least-cost street tree: an 8-connected Dijkstra from the cell nearest the
// raster centre, then greedy farthest-point branch targets, then a one-cell orthogonal
// widening where the step is gentle.
//
// `heights` is one metre value per raster cell, indexed [j*size + i]; only cells in
// `valid` are read. `required` are gate cells the tree must reach if it can.
StreetNetwork grow_streets(const CellSet& valid, const std::vector<double>& heights, int size,
                       std::uint64_t seed, double spacing_cells,
                       const std::vector<Cell>& required, int branch_floor = 12);

// Signed heading change of the polyline a->b->c, degrees in (-180, 180].
double path_turn_degrees(Point a, Point b, Point c);

// Pull in needle vertices so an enceinte cannot form one giant outward point. Fewer than
// eight points are returned untouched — there is no ring to smooth.
std::vector<Point> smooth_closed_ring(const std::vector<Point>& points,
                                      double max_turn_degrees = 95.0,
                                      double radius_factor = 1.12, int passes = 12);

// ===========================================================================
// Fortifications
// ===========================================================================

struct WallRing {
    std::string id;           // ring-0
    std::string_view role;    // inner | middle | outer
    double scale = 0.0;
    std::vector<Point> polyline_m;
    std::string_view status = "closed";
    int segment_count = 0;
    int gate_count = 0;
};

struct WallSegment {
    std::string id;  // ring-0-seg-3
    std::string ring_id;
    std::string_view structure_id;
    Point from_m;
    Point to_m;
    Point center_m;
    double length_m = 0.0;
    // The heading MINUS 90: a wall module's depth axis runs along the wall.
    double rotation_degrees = 0.0;
};

struct WallNode {
    std::string id;
    std::string_view kind = "corner";
    Point position_m;
    std::string ring_id;
};

struct WallGate {
    std::string id;
    std::string ring_id;
    std::string_view structure_id;
    Point position_m;
    Point approach_m;
    std::optional<int> route_index;
    std::string plot_id;  // set by the planner if a gatehouse was actually built
};

struct WallTower {
    std::string id;
    std::string ring_id;
    std::string_view structure_id;
    Point position_m;
    std::string plot_id;
};

struct UnplacedWall {
    std::string_view building_id;
    std::string ring_id;  // empty when the whole enceinte failed rather than one ring
    std::string_view reason;
};

struct Fortifications {
    int version = 1;
    // True only when a ring actually closed. It is also a placement condition, so a city
    // whose walls failed cannot build what a wall would have justified.
    bool defended_perimeter = false;
    std::vector<WallRing> rings;
    std::vector<WallSegment> segments;
    std::vector<WallNode> nodes;
    std::vector<WallGate> gates;
    std::vector<WallTower> towers;
    std::vector<UnplacedWall> unplaced;
    // What this geometry is and is not. Emitted because a consumer reading a wall
    // network needs to know it is a schematic enceinte and not structural engineering.
    std::string_view method = "Angular boundary samples of the buildable shape; city-only wall network.";
    std::string_view limits =
        "Schematic enceinte, not structural engineering. Castle baileys use a separate generator.";
};

// How many enceintes a city of this class and shape gets: 2..3 for concentric
// enceintes, one for a medium or capital city, none for a small one.
int ring_count_for(std::string_view shape_id, const ShapeSelection& shape, CityClass city_class);

// ===========================================================================
// Plans
// ===========================================================================

// A junction between a regional road and the local street network.
struct RoadConnection {
    std::string route_id;
    int route_index = -1;
    std::string_view end = "to";
    int outside_index = 0;
    std::string junction_id;
    Point gate_local_m;
    Vec3 direction;  // unit globe direction of the gate point
    std::string_view status = "unreachable";
    std::string_view reason;
    Cell cell{0, 0};
    bool has_cell = false;
    std::vector<Point> local_path_m;  // only when the junction is terrain-safe
};

struct HousingUpgrade {
    std::string_view phase;
    std::string_view building_id;
    std::string_view name;
    BoxSize dimensions_m;
    int beds = 0;
};

struct Plot {
    std::string id;  // plot-<n>, n the index at insertion time
    std::string_view building_id;
    std::string_view name;
    std::string_view kind;   // service | housing
    std::string_view phase;  // fortification | high | high_housing | low | low_housing
    double x_m = 0.0;
    double z_m = 0.0;
    double rotation_degrees = 0.0;
    double ground_elevation_m = 0.0;   // max of the sampled ground
    double foundation_bottom_m = 0.0;  // min of it
    PlotSize plot_m;
    BoxSize dimensions_m;
    Point road_access;  // the street anchor this plot fronts
    int workers = 0;
    int beds = 0;
    // Only on a house that was densified into apartments. The FIRST upgrade wins: the
    // record is what the plot used to be, not what it was a moment ago.
    std::optional<HousingUpgrade> housing_upgrade;
};

struct Unplaced {
    std::string_view building_id;
    int count = 0;
    std::string reason;
};

struct Pass {
    std::string_view id;
    long long placed = 0;
};

struct HousingAudit {
    std::string_view phase;
    int workers = 0;
    int starting_beds = 0;
    int houses_placed = 0;
    int upgrades = 0;
    bool plots_exhausted = false;
    // Cities only, like `upgrades`. A hamlet reserves nothing and densifies nothing, so
    // both stay at zero; they exist so the rural audit has the same shape as a city's.
    int reserved_slots = 0;
    int final_beds = 0;
    int shortfall = 0;
};

struct HousingReservation {
    std::string_view phase;
    int workers_budgeted = 0;
    int slots = 0;
};

// The local ground, as a raster of 4 m cells and a corner-sampled elevation surface.
struct TerrainRaster {
    double cell_m = 4.0;
    int size = 0;
    std::vector<int> codes;  // [j*size + i]: 0 buildable, 1 water, 2 steep or channel
    std::vector<int> natural_biome;
    std::vector<int> biome_variant;
    int surface_size = 0;  // size + 1: heights sit on cell CORNERS
    double surface_step_m = 0.0;
    std::vector<double> surface_heights_m;  // [j*surface_size + i]

    int code(int i, int j) const { return codes[static_cast<std::size_t>(j) * static_cast<std::size_t>(size) + static_cast<std::size_t>(i)]; }
};

struct ReferenceFrame {
    double radius_m = 0.0;
    double latitude_degrees = 0.0;
    double longitude_degrees = 0.0;
};

// The seven facts the planner can actually measure about a site. Everything else the
// shape catalogue can test is left absent, which fails its rules rather than guessing.
struct SiteFacts {
    double buildable_area_m2 = 0.0;
    double usable_land_fraction = 0.0;
    // 90 is the sentinel for "no cell was buildable", not a measured slope.
    double local_slope_degrees = 90.0;
    double aridity = 0.0;
    double wooded_fraction = 0.0;
    double regional_threat = 0.0;
    // Deliberately the same number as regional_threat: the planner has no separate
    // measure of how much a city wants to defend itself.
    double defense_priority = 0.0;

    ShapeSite as_shape_site() const;
};

struct PlanStats {
    int workers = 0;
    int worker_beds = 0;
    int housing_shortfall = 0;
    int service_buildings = 0;
    int houses = 0;
    int apartments = 0;
    int unplaced_core = 0;
    int unplaced_total = 0;
    double reserved_plot_area_m2 = 0.0;
    // Cities only. A hamlet raises no wall, so these stay at zero rather than being
    // absent — the reference omits the keys entirely on a rural plan.
    int wall_segments = 0;
    bool defended_perimeter = false;
    std::optional<long long> population_estimate;
    std::optional<long long> urban_population_estimate;
};

struct PlanDebug {
    std::size_t terrain_safe_cells = 0;
    std::size_t total_cells = 0;
    std::vector<HousingAudit> housing_passes;
    std::string_view apartment_policy;
    std::vector<HousingReservation> housing_reservations;
    std::size_t shape_safe_cells = 0;
    std::size_t road_cells = 0;
    std::size_t occupied_cells = 0;
    std::size_t access_cells = 0;
    std::size_t vacant_shape_cells = 0;
    std::size_t housing_frontage_candidates = 0;
    std::size_t fortification_rings = 0;
    double program_half_m = 0.0;
};

// One city site, as stage 9 leaves it.
struct CitySite {
    std::string uid;  // the plan's key; when empty the string form of `id` stands in
    int id = 0;
    std::string name;
    double x = 0.0;  // raster node units, 0..n-1
    double z = 0.0;
    CityClass city_class = CityClass::Small;
    std::string population_profile;  // a civilization id
    std::optional<long long> population_estimate;
    std::optional<long long> urban_population_estimate;
    // Present means the site has water it can collect or have delivered. The DISTANCE is
    // never read — only whether one was measured at all.
    std::optional<double> freshwater_distance_m;
    double resource_potential = 0.0;
    double regional_threat = 0.0;
    bool founded_by_player = false;
    // Junctions with the regional road network, projected into the local window by the
    // caller. The planner upgrades a reachable one in place and never invents any.
    std::vector<RoadConnection> road_connections;

    std::string key() const;
};

struct CityPlan {
    int version = 8;
    std::string city_uid;
    int site_id = 0;
    std::string name;
    std::string civilization_id;
    CityClass city_class = CityClass::Small;
    double half_m = 0.0;  // bounds are [-half, -half, half, half]
    TerrainRaster terrain;
    ReferenceFrame reference_frame;
    SiteFacts location;
    ShapeSelection shape;
    std::vector<Plot> plots;
    std::vector<Cell> roads;  // street cells, ascending
    std::vector<Unplaced> unplaced;
    // Absent for an unbuildable or authored city, which is a state the status already
    // reports — no consumer needs a new branch for it.
    std::optional<Fortifications> fortifications;
    std::vector<Pass> passes;
    std::vector<std::string_view> warnings;
    double source_resolution_m = 0.0;
    std::string_view status = "unbuildable";  // unbuildable | partial | complete
    PlanStats stats;
    PlanDebug debug;
    std::vector<RoadConnection> road_connections;
    std::vector<std::vector<Point>> street_paths_m;
    // The generator chooses no shape for a town the player laid out. It still gets the
    // full terrain record, because a consumer rendering it needs the ground it stands on.
    bool authored_by_player = false;
};

// A rural support pin.
struct HamletPin {
    // The join key. ORDINAL: it renumbers on an age advance, so nothing downstream may
    // key long-lived state on it.
    std::string id;
    double x = 0.0;
    double z = 0.0;
    int node = -1;
    // A POSITION in the `sites` vector AS PASSED to plan_hamlet or fill_hamlets, not a
    // uid and not a position in any sorted view of it. `fill_cities` sorts a COPY, so a
    // caller's own order survives that call — but handing a re-sorted list here
    // attributes every hamlet to the wrong parent city, and the only symptom is a
    // `core_city_uid` that names a plausible neighbour. Out of range leaves it empty.
    int core_id = -1;
    std::string population_profile;
    std::string role;  // farming | resource | harbor + fishing | ...
    // The node path running from the hamlet TOWARD its parent city; path[0] is the
    // hamlet's own node.
    std::vector<int> access_nodes;
};

struct HamletPlan {
    int version = 3;
    std::string hamlet_id;
    std::string_view kind = "hamlet";
    std::string role;
    std::string core_city_uid;
    int core_id = -1;
    double x = 0.0;
    double z = 0.0;
    int node = -1;
    std::string civilization_id;
    double half_m = 0.0;
    TerrainRaster terrain;
    ReferenceFrame reference_frame;
    // Fixed: a hamlet never consults the city shape catalogue. The two parameters are
    // emitted rather than only used, because they are what a consumer needs to redraw
    // the window — the street spacing the tree was grown at and the ellipse's aspect.
    std::string_view shape_id = "hamlet_compact";
    std::string_view shape_family = "organic";
    double shape_block_spacing_m = 48.0;
    double shape_aspect_ratio = 1.1;
    std::vector<Plot> plots;
    std::vector<Cell> roads;
    std::vector<Unplaced> unplaced;
    std::vector<Pass> passes;
    std::vector<std::string_view> warnings;
    double source_resolution_m = 0.0;
    std::string_view status = "unbuildable";
    PlanStats stats;
    PlanDebug debug;
    std::vector<RoadConnection> road_connections;
    std::vector<std::vector<Point>> street_paths_m;
};

// The city crop's half-extent: class minimum grown toward programme demand, capped by
// the nearest neighbour, truncated toward zero to a multiple of the 4 m cell.
//
// With the shipped registry the demand term never binds — it would take roughly a
// fourfold programme — so the three class minimums alone set every city's crop today.
// That is a designed growth path rather than dead code; a port that gets it wrong is
// not caught by any world that currently exists.
double program_half_m(const SettlementPreset& preset, CityClass city_class, double neighbour_half,
                      PlotSize house_plot, int beds_per_house);

// Plan one city. `nearby_counts` is the running tally of shapes already given to cities
// planned before this one — the only thing that serialises the cities at all.
//
// `grid` MUST be built at world.effective.globe_radius. Every reach here is absolute
// metres, and a design-space grid silently measures a different planet.
CityPlan plan_city(const World& world, const SphereGrid& grid, const CitySite& site,
                   const std::vector<CitySite>& all_sites,
                   const std::map<std::string_view, int>& nearby_counts);

HamletPlan plan_hamlet(const World& world, const SphereGrid& grid, const HamletPin& pin,
                       const std::vector<CitySite>& sites, const std::vector<HamletPin>& all_pins);

struct CityPlans {
    int version = 8;
    std::string shapes_sha256;
    std::vector<CityPlan> cities;  // one per site, in sorted-uid order
    std::vector<std::string_view> phase_order;
    std::string_view scope;
};

struct HamletPlans {
    int version = 3;
    std::vector<HamletPlan> hamlets;  // one per pin, in sorted-id order
    std::vector<std::string_view> phase_order;
    std::string_view scope;
};

// Sites are planned in ascending order of the STRING form of their key, which is
// lexicographic and not numeric: "city-10" comes before "city-2". The order is what the
// shape repetition counter walks, so it decides which city gets which shape.
CityPlans fill_cities(const World& world, const SphereGrid& grid, std::vector<CitySite> sites);

// A hamlet plan carries no cross-hamlet state at all, so unlike a city nothing here
// depends on the order — but it is sorted anyway, because the output order is a record
// consumers read.
HamletPlans fill_hamlets(const World& world, const SphereGrid& grid,
                         const std::vector<CitySite>& sites, std::vector<HamletPin> pins);

}  // namespace fwg
