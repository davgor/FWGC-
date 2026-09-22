#include "fwg/city_plan.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "fwg/detail.hpp"
#include "fwg/grid.hpp"
#include "fwg/random.hpp"
#include "fwg/water.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kCell = 4.0;
// Above this a cell is blocked, a plot's ground relief is refused, and a road junction
// is rejected. One number, three uses; they were never meant to drift apart.
constexpr double kSlopeLimitDegrees = 25.0;
// Half-width of the reserved channel measured from a routed river centreline.
constexpr double kRiverChannelM = 28.0;
// Within this of a centreline the point counts as water outright.
constexpr double kRiverWaterM = 12.0;
// Segments whose bounding box misses the window by more than this are dropped: it
// covers the 28 m setback plus slack.
constexpr double kRiverPadM = 40.0;
// A river node more than about 84 degrees away projects to infinity.
constexpr double kHorizonDen = 0.1;
constexpr double kMaxGrade = 0.35;
// Two uses: diagonal admissibility and the one-cell road widening.
constexpr double kCliffStepM = 1.4;
constexpr double kRoot2 = 1.4142135623730951;

double radians(double degrees) { return degrees * kPi / 180.0; }
double degrees_of(double rad) { return rad * 180.0 / kPi; }

double round_to(double value, int places) {
    double factor = 1.0;
    for (int i = 0; i < places; ++i) factor *= 10.0;
    return std::round(value * factor) / factor;
}

double clamp(double value, double low, double high) {
    return value < low ? low : (value > high ? high : value);
}

double distance2d(Point a, Point b) { return std::hypot(a.x - b.x, a.z - b.z); }

// The wooded biome ids. Feeds the wooded fraction and, through it, whether a city can
// claim a medicinal supply.
bool is_wooded(int biome) { return biome == 4 || biome == 7 || biome == 15; }

}  // namespace

// ===========================================================================
// Content accessors
// ===========================================================================

std::string_view to_string(StructurePriority priority) {
    switch (priority) {
        case StructurePriority::core: return "core";
        case StructurePriority::conditional: return "conditional";
        case StructurePriority::specialist: return "specialist";
    }
    return "core";
}

std::string_view to_string(PlacementCondition condition) {
    static constexpr std::string_view kNames[kPlacementConditionCount] = {
        "usable_groundwater", "water_collection_or_delivery", "navigable_shore",
        "flowing_water",      "reliable_wind",                "ore_and_fuel_supply",
        "food_surplus",       "working_animals",              "clay_and_fuel_supply",
        "medicinal_supply",   "clear_sky_view",               "isolation_access",
        "defended_perimeter", "route_crossing",               "grade_change",
        "retention_needed",   "farming_support",              "resource_support"};
    return kNames[static_cast<int>(condition)];
}

std::string_view to_string(ShapeFeature feature) {
    static constexpr std::string_view kNames[kShapeFeatureCount] = {
        "coast_access",       "navigable_water",    "river_access",
        "river_bend",         "bridge_feasible",    "island",
        "peninsula",          "lagoon",             "ridge",
        "hill",               "oasis",              "usable_land_fraction",
        "corridor_constraint", "aridity",           "wooded_fraction",
        "defense_priority",   "regional_threat",    "planned_foundation",
        "trade_intensity",    "local_slope_degrees", "buildable_area_m2"};
    return kNames[static_cast<int>(feature)];
}

std::string_view city_class_name(CityClass city_class) {
    switch (city_class) {
        case CityClass::Capital: return "capital";
        case CityClass::Medium: return "medium";
        case CityClass::Small: return "small";
    }
    return "small";
}

SettlementBlock city_block_for(CityClass city_class) {
    switch (city_class) {
        case CityClass::Capital: return SettlementBlock::capital_city;
        case CityClass::Medium: return SettlementBlock::medium_city;
        case CityClass::Small: return SettlementBlock::small_city;
    }
    return SettlementBlock::small_city;
}

const Structure* structure(std::string_view id) {
    for (const Structure& s : structures()) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

const Structure& PresetRow::structure() const { return structures()[structure_index]; }

std::string_view PresetRow::display_name() const {
    return name.empty() ? structure().name : name;
}

const HousingProfile& housing_profile(std::string_view key) {
    for (const HousingProfile& p : housing_profiles()) {
        if (p.key == key) return p;
    }
    throw std::invalid_argument("unknown housing profile: " + std::string(key));
}

const SettlementPreset& settlement_preset(std::string_view civilization_id, SettlementBlock block) {
    for (const CivilizationPresets& c : civilization_presets()) {
        if (c.id == civilization_id) return c.blocks[static_cast<std::size_t>(block)];
    }
    throw std::invalid_argument("unknown civilization: " + std::string(civilization_id));
}

std::string CitySite::key() const { return uid.empty() ? std::to_string(id) : uid; }

// ===========================================================================
// Shape selection
// ===========================================================================

void ShapeSite::set(ShapeFeature feature, double value) {
    values_[static_cast<int>(feature)] = value;
    present_ |= 1u << static_cast<int>(feature);
}

ShapeSite SiteFacts::as_shape_site() const {
    ShapeSite site;
    site.set(ShapeFeature::buildable_area_m2, buildable_area_m2);
    site.set(ShapeFeature::usable_land_fraction, usable_land_fraction);
    site.set(ShapeFeature::local_slope_degrees, local_slope_degrees);
    site.set(ShapeFeature::aridity, aridity);
    site.set(ShapeFeature::wooded_fraction, wooded_fraction);
    site.set(ShapeFeature::regional_threat, regional_threat);
    site.set(ShapeFeature::defense_priority, defense_priority);
    return site;
}

double ShapeSelection::parameter(std::string_view key, double fallback) const {
    for (const ShapeParameter& p : parameters) {
        if (p.key == key) return p.value;
    }
    return fallback;
}

namespace {

// An ABSENT feature fails, which is the whole reason a shape naming a landform the
// planner cannot measure is permanently ineligible rather than admitted on a default.
bool rule_matches(const ShapeRule& rule, const ShapeSite& site) {
    if (!site.has(rule.field)) return false;
    const double value = site.value(rule.field);
    if (rule.has_equals && value != rule.equals_value) return false;
    if (rule.has_min && value < rule.min_value) return false;
    if (rule.has_max && value > rule.max_value) return false;
    return true;
}

std::uint32_t class_bit(CityClass city_class) {
    switch (city_class) {
        case CityClass::Small: return 1u << 0;
        case CityClass::Medium: return 1u << 1;
        case CityClass::Capital: return 1u << 2;
    }
    return 0u;
}

// One uniform in [0, 1) per label.
//
// The reference hashes the catalogue identity into every draw so that editing the
// catalogue's prose moves every city's shape. This keeps that property while using the
// library's own stream: the identity is part of the domain a child seed is derived
// from, and one child per label keeps the labels independent, so adding a variation
// parameter to a shape cannot shift another shape's roulette.
Rng label_stream(std::uint32_t seed, std::string_view city_id, std::string_view label) {
    std::string domain = "city-shape:";
    domain += city_shape_catalogue_identity();
    domain += ':';
    domain.append(city_id);
    domain += ':';
    domain.append(label);
    return Rng::child(static_cast<std::uint64_t>(seed), domain);
}

}  // namespace

std::vector<ShapeCandidate> rank_shapes(const ShapeSite& site, CityClass city_class,
                                        const std::map<std::string_view, int>& nearby_counts,
                                        bool include_later) {
    // Three facts must always be supplied; without them a ranking is an opinion about a
    // site nobody measured.
    for (ShapeFeature required : {ShapeFeature::buildable_area_m2, ShapeFeature::usable_land_fraction,
                                  ShapeFeature::local_slope_degrees}) {
        if (!site.has(required)) {
            throw std::invalid_argument("missing required footprint location data: " +
                                        std::string(to_string(required)));
        }
    }
    // The catalogue declares a range for every feature and refuses anything outside it.
    // This is live rather than defensive: regional_threat is declared 0..1, so a threat
    // assessment that writes 1.2 stops city planning instead of skewing every draw in
    // the world by an amount nobody would be able to see.
    const auto specs = shape_feature_specs();
    for (int i = 0; i < kShapeFeatureCount; ++i) {
        const auto feature = static_cast<ShapeFeature>(i);
        if (!site.has(feature)) continue;
        const double value = site.value(feature);
        const ShapeFeatureSpec& spec = specs[static_cast<std::size_t>(i)];
        const bool legal = spec.is_boolean ? (value == 0.0 || value == 1.0)
                                           : (value >= spec.min_value && value <= spec.max_value);
        if (!legal) {
            throw std::invalid_argument("out-of-range site feature " +
                                        std::string(to_string(feature)) + ": " +
                                        std::to_string(value));
        }
    }
    // A count naming a shape that does not exist is a caller bug, not an empty tally.
    for (const auto& [id, count] : nearby_counts) {
        bool known = false;
        for (const CityShape& shape : city_shapes()) known = known || shape.id == id;
        if (!known) throw std::invalid_argument("unknown nearby shape id: " + std::string(id));
        if (count < 0 || count > 10000) {
            throw std::invalid_argument("nearby shape count out of range for " + std::string(id));
        }
    }

    std::vector<ShapeCandidate> ranked;
    const double penalty = shape_repetition_penalty();
    // The table is already in id order, which is what makes the cumulative weighted walk
    // below independent of where a shape sits in the catalogue file.
    for (const CityShape& shape : city_shapes()) {
        if (!shape.enabled_by_default && !include_later) continue;
        if ((shape.city_classes & class_bit(city_class)) == 0u) continue;
        bool eligible = true;
        for (const ShapeRule& rule : shape.eligibility) {
            if (!rule_matches(rule, site)) {
                eligible = false;
                break;
            }
        }
        if (!eligible) continue;

        ShapeCandidate candidate;
        candidate.id = shape.id;
        candidate.family = shape.family;
        double weight = shape.base_weight;
        for (const ShapePreference& preference : shape.preferences) {
            if (!rule_matches(preference.when, site)) continue;
            weight += preference.bonus;
            candidate.matched_preferences.push_back(preference.when.field);
        }
        const auto seen = nearby_counts.find(shape.id);
        const int count = seen == nearby_counts.end() ? 0 : seen->second;
        candidate.weight = weight / (1.0 + count * penalty);
        ranked.push_back(std::move(candidate));
    }
    return ranked;
}

ShapeSelection select_shape(const ShapeSite& site, std::uint32_t seed, std::string_view city_id,
                            CityClass city_class,
                            const std::map<std::string_view, int>& nearby_counts,
                            bool include_later) {
    if (city_id.empty() || city_id.size() > 256) {
        throw std::invalid_argument("select_shape needs a stable city id of 1..256 characters");
    }
    ShapeSelection result;
    result.catalogue_revision = city_shape_catalogue_revision();
    result.catalogue_sha256 = city_shape_catalogue_identity();
    result.candidates = rank_shapes(site, city_class, nearby_counts, include_later);
    if (result.candidates.empty()) {
        result.reason = "No compatible shape; change site or provide missing location facts.";
        return result;
    }

    double total = 0.0;
    for (const ShapeCandidate& c : result.candidates) total += c.weight;
    double target = label_stream(seed, city_id, "shape").random() * total;
    // The last candidate is the default, which catches the floating-point case where the
    // accumulated subtraction never quite goes negative.
    const ShapeCandidate* chosen = &result.candidates.back();
    for (const ShapeCandidate& c : result.candidates) {
        target -= c.weight;
        if (target < 0.0) {
            chosen = &c;
            break;
        }
    }

    const CityShape* shape = nullptr;
    for (const CityShape& s : city_shapes()) {
        if (s.id == chosen->id) shape = &s;
    }
    result.shape_id = shape->id;
    result.family = shape->family;
    result.source_ids = shape->source_ids;
    result.layout = shape->layout;
    result.reason = "Seeded weighted choice among compatible shapes.";
    for (const ShapeVariation& variation : shape->variation) {
        std::string label(shape->id);
        label += ':';
        label.append(variation.key);
        Rng stream = label_stream(seed, city_id, label);
        double value = 0.0;
        if (variation.integer) {
            // Inclusive of BOTH endpoints.
            const auto span = static_cast<std::uint64_t>(variation.max_value - variation.min_value) + 1u;
            value = variation.min_value + static_cast<double>(stream.below(span));
        } else {
            value = round_to(stream.uniform(variation.min_value, variation.max_value), 6);
        }
        result.parameters.push_back({variation.key, value});
    }
    return result;
}

// ===========================================================================
// Geometry
// ===========================================================================

CellSet::CellSet(int size)
    : size_(size), in_(static_cast<std::size_t>(size) * static_cast<std::size_t>(size), 0) {}

bool CellSet::contains(Cell cell) const {
    if (cell.first < 0 || cell.first >= size_ || cell.second < 0 || cell.second >= size_) return false;
    return in_[static_cast<std::size_t>(cell.second) * static_cast<std::size_t>(size_) +
               static_cast<std::size_t>(cell.first)] != 0;
}

bool CellSet::insert(Cell cell) {
    if (cell.first < 0 || cell.first >= size_ || cell.second < 0 || cell.second >= size_) return false;
    char& slot = in_[static_cast<std::size_t>(cell.second) * static_cast<std::size_t>(size_) +
                     static_cast<std::size_t>(cell.first)];
    if (slot != 0) return false;
    slot = 1;
    ++count_;
    return true;
}

bool CellSet::erase(Cell cell) {
    if (cell.first < 0 || cell.first >= size_ || cell.second < 0 || cell.second >= size_) return false;
    char& slot = in_[static_cast<std::size_t>(cell.second) * static_cast<std::size_t>(size_) +
                     static_cast<std::size_t>(cell.first)];
    if (slot == 0) return false;
    slot = 0;
    --count_;
    return true;
}

bool CellSet::contains_all(const std::vector<Cell>& cells) const {
    for (Cell c : cells) {
        if (!contains(c)) return false;
    }
    return true;
}

bool CellSet::intersects(const std::vector<Cell>& cells) const {
    for (Cell c : cells) {
        if (contains(c)) return true;
    }
    return false;
}

void CellSet::insert_all(const std::vector<Cell>& cells) {
    for (Cell c : cells) insert(c);
}

void CellSet::erase_all(const std::vector<Cell>& cells) {
    for (Cell c : cells) erase(c);
}

std::vector<Cell> CellSet::sorted() const {
    std::vector<Cell> out;
    out.reserve(count_);
    // Column outside row: the order every tie-break in this file is taken in.
    for (int i = 0; i < size_; ++i) {
        for (int j = 0; j < size_; ++j) {
            if (contains({i, j})) out.push_back({i, j});
        }
    }
    return out;
}

std::vector<Point> corners(double x, double z, double w, double d, double angle) {
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double hw = w / 2.0;
    const double hd = d / 2.0;
    const std::array<std::pair<double, double>, 4> offsets = {
        std::pair{-hw, -hd}, std::pair{hw, -hd}, std::pair{hw, hd}, std::pair{-hw, hd}};
    std::vector<Point> out;
    out.reserve(4);
    for (const auto& [u, v] : offsets) out.push_back({x + u * c - v * s, z + u * s + v * c});
    return out;
}

std::vector<Cell> footprint_cells(double x, double z, double w, double d, double angle, double half,
                                  double cell) {
    const std::vector<Point> points = corners(x, z, w, d, angle);
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    double min_x = points[0].x, max_x = points[0].x, min_z = points[0].z, max_z = points[0].z;
    for (const Point& p : points) {
        min_x = std::min(min_x, p.x);
        max_x = std::max(max_x, p.x);
        min_z = std::min(min_z, p.z);
        max_z = std::max(max_z, p.z);
    }
    const auto lo_i = static_cast<int>(std::floor((min_x + half) / cell));
    const auto hi_i = static_cast<int>(std::floor((max_x + half) / cell));
    const auto lo_j = static_cast<int>(std::floor((min_z + half) / cell));
    const auto hi_j = static_cast<int>(std::floor((max_z + half) / cell));

    // Minkowski dilation of the rotated rectangle by one axis-aligned cell: a cell is in
    // when the SQUARE overlaps the rectangle, not merely when its centre does. The 1e-8
    // is subtracted, so an exact boundary touch is out.
    const double r = cell / 2.0 * (std::abs(c) + std::abs(s));
    const double half_w = w / 2.0 + r - 1e-8;
    const double half_d = d / 2.0 + r - 1e-8;

    std::vector<Cell> out;
    for (int j = lo_j; j <= hi_j; ++j) {
        const double dz = -half + (j + 0.5) * cell - z;
        for (int i = lo_i; i <= hi_i; ++i) {
            const double dx = -half + (i + 0.5) * cell - x;
            if (std::abs(dx * c + dz * s) < half_w && std::abs(-dx * s + dz * c) < half_d) {
                out.push_back({i, j});
            }
        }
    }
    return out;
}

std::vector<int> clearance_field(const CellSet& free, int size) {
    const int far = size + size;
    std::vector<int> grid(static_cast<std::size_t>(size) * static_cast<std::size_t>(size), 0);
    for (int j = 0; j < size; ++j) {
        for (int i = 0; i < size; ++i) {
            grid[static_cast<std::size_t>(j) * static_cast<std::size_t>(size) +
                 static_cast<std::size_t>(i)] = free.contains({i, j}) ? far : 0;
        }
    }
    const auto at = [&](int i, int j) -> int& {
        return grid[static_cast<std::size_t>(j) * static_cast<std::size_t>(size) +
                    static_cast<std::size_t>(i)];
    };
    for (int j = 0; j < size; ++j) {
        for (int i = 0; i < size; ++i) {
            if (at(i, j) == 0) continue;
            // The raster edge is not free ground, so a free cell on the border is at
            // distance one from blocked ground. That is the whole of the `best = 1`.
            int best = i > 0 ? at(i - 1, j) + 1 : 1;
            if (j == 0) {
                if (best > 1) best = 1;
            } else {
                best = std::min(best, at(i, j - 1) + 1);
                if (i > 0) best = std::min(best, at(i - 1, j - 1) + 1);
                if (i + 1 < size) best = std::min(best, at(i + 1, j - 1) + 1);
            }
            if (best < at(i, j)) at(i, j) = best;
        }
    }
    for (int j = size - 1; j >= 0; --j) {
        for (int i = size - 1; i >= 0; --i) {
            if (at(i, j) == 0) continue;
            int best = i + 1 < size ? at(i + 1, j) + 1 : 1;
            if (j + 1 >= size) {
                if (best > 1) best = 1;
            } else {
                best = std::min(best, at(i, j + 1) + 1);
                if (i + 1 < size) best = std::min(best, at(i + 1, j + 1) + 1);
                if (i > 0) best = std::min(best, at(i - 1, j + 1) + 1);
            }
            if (best < at(i, j)) at(i, j) = best;
        }
    }
    return grid;
}

namespace {

struct HeapEntry {
    double cost;
    Cell cell;
    // Ties break on the cell, so two equal-cost frontiers expand in a fixed order rather
    // than in whatever order the heap happened to leave them.
    bool operator>(const HeapEntry& other) const {
        if (cost != other.cost) return cost > other.cost;
        return cell > other.cell;
    }
};

}  // namespace

StreetNetwork grow_streets(const CellSet& valid, const std::vector<double>& heights, int size,
                       std::uint64_t seed, double spacing_cells, const std::vector<Cell>& required,
                       int branch_floor) {
    StreetNetwork out;
    out.cells = CellSet(size);
    if (valid.empty()) return out;

    const auto index_of = [size](Cell c) {
        return static_cast<std::size_t>(c.second) * static_cast<std::size_t>(size) +
               static_cast<std::size_t>(c.first);
    };
    const std::vector<Cell> cells = valid.sorted();

    Cell root = cells.front();
    double best_root = std::numeric_limits<double>::infinity();
    for (Cell c : cells) {
        const double dx = c.first - size / 2.0;
        const double dz = c.second - size / 2.0;
        const double d2 = dx * dx + dz * dz;
        if (d2 < best_root) {
            best_root = d2;
            root = c;
        }
    }

    // A smooth travel-cost field produces persistent bends, rather than white-noise
    // zigzags. Three draws, in order.
    Rng rng(seed);
    const double p0 = rng.uniform(-kPi, kPi);
    const double p1 = rng.uniform(-kPi, kPi);
    const double p2 = rng.uniform(-kPi, kPi);

    std::vector<double> costs(heights.size(), 0.0);
    for (Cell c : cells) {
        costs[index_of(c)] = 1.0 + 0.45 * (1.0 + std::sin(c.first / 9.0 + p0) * std::cos(c.second / 11.0 + p1)) +
                             0.3 * (1.0 + std::sin((c.first + c.second) / 17.0 + p2));
    }

    const double kInfinity = std::numeric_limits<double>::infinity();
    std::vector<double> distances(heights.size(), kInfinity);
    std::vector<Cell> previous(heights.size(), Cell{-1, -1});
    distances[index_of(root)] = 0.0;
    std::priority_queue<HeapEntry, std::vector<HeapEntry>, std::greater<HeapEntry>> queue;
    queue.push({0.0, root});

    static constexpr std::array<std::pair<int, int>, 8> kNeighbours = {
        std::pair{-1, 0}, std::pair{0, -1}, std::pair{0, 1},  std::pair{1, 0},
        std::pair{-1, -1}, std::pair{-1, 1}, std::pair{1, -1}, std::pair{1, 1}};

    while (!queue.empty()) {
        const HeapEntry top = queue.top();
        queue.pop();
        const std::size_t at = index_of(top.cell);
        if (top.cost != distances[at]) continue;  // stale entry
        const int x = top.cell.first;
        const int z = top.cell.second;
        // THE NEIGHBOUR ORDER IS LOAD-BEARING: relaxation is strictly less-than, so on an
        // exact distance tie the first neighbour visited keeps the predecessor and every
        // street traced through it follows that choice.
        for (const auto& [dx, dz] : kNeighbours) {
            const Cell q{x + dx, z + dz};
            if (!valid.contains(q)) continue;
            if (dx != 0 && dz != 0) {
                // No corner cutting: a diagonal needs both orthogonal supports.
                const Cell a{x + dx, z};
                const Cell b{x, z + dz};
                if (!valid.contains(a) || !valid.contains(b)) continue;
                // The cliff guard: a diagonal must not slip between two cliff cells.
                const double ha = heights[index_of(a)];
                const double hb = heights[index_of(b)];
                const double hc = heights[at];
                const double hq = heights[index_of(q)];
                if (std::abs(ha - hc) > kCliffStepM || std::abs(ha - hq) > kCliffStepM ||
                    std::abs(hb - hc) > kCliffStepM || std::abs(hb - hq) > kCliffStepM) {
                    continue;
                }
            }
            // A HARDCODED 4 m, not the caller's cell size. Changing the raster cell means
            // changing this with it.
            const double length = std::hypot(static_cast<double>(dx), static_cast<double>(dz)) * 4.0;
            const double grade = std::abs(heights[index_of(q)] - heights[at]) / length;
            if (grade > kMaxGrade) continue;
            // At the grade ceiling the quadratic term adds 2.205 -- about the whole span
            // of the smooth cost field -- so a steep link costs roughly twice the worst
            // flat one rather than being merely discouraged.
            const double next = top.cost + length * ((costs[at] + costs[index_of(q)]) / 2.0 + 18.0 * grade * grade);
            if (next < distances[index_of(q)]) {
                distances[index_of(q)] = next;
                previous[index_of(q)] = top.cell;
                queue.push({next, q});
            }
        }
    }

    std::vector<Cell> reachable;
    for (Cell c : cells) {
        if (distances[index_of(c)] != kInfinity) reachable.push_back(c);
    }

    // Destinations represent gates and growing districts, never an imposed street grid.
    std::vector<Cell> targets;
    for (Cell c : required) {
        if (valid.contains(c) && distances[index_of(c)] != kInfinity) targets.push_back(c);
    }
    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());

    const double divisor = std::max(12.0, spacing_cells);
    const auto branches = static_cast<int>(std::max<long long>(
        branch_floor,
        std::min<long long>(36, std::llround(static_cast<double>(valid.count()) / (divisor * divisor) * 1.5))));

    const std::size_t pool = std::min<std::size_t>(
        reachable.size(), std::max<std::size_t>(256, static_cast<std::size_t>(branches) * 20));
    std::vector<Cell> candidates;
    candidates.reserve(pool);
    for (std::size_t index : rng.sample_indices(reachable.size(), pool)) {
        candidates.push_back(reachable[index]);
    }

    for (int n = 0; n < branches; ++n) {
        if (candidates.empty()) break;
        std::size_t pick = 0;
        double pick_far = -1.0;
        for (std::size_t k = 0; k < candidates.size(); ++k) {
            const Cell c = candidates[k];
            double nearest = std::numeric_limits<double>::infinity();
            const auto consider = [&](Cell q) {
                const double dx = c.first - q.first;
                const double dz = c.second - q.second;
                nearest = std::min(nearest, dx * dx + dz * dz);
            };
            consider(root);
            for (Cell q : targets) consider(q);
            // Greedy farthest point, ties broken by the cell. `targets` grows inside the
            // loop, so each pick is farthest from everything chosen so far.
            if (nearest > pick_far || (nearest == pick_far && candidates[pick] < c)) {
                pick_far = nearest;
                pick = k;
            }
        }
        targets.push_back(candidates[pick]);
        candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(pick));
    }

    CellSet network(size);
    network.insert(root);
    for (Cell target : targets) {
        std::vector<Cell> path{target};
        while (!network.contains(path.back())) path.push_back(previous[index_of(path.back())]);
        if (path.size() > 1) {
            std::reverse(path.begin(), path.end());
            network.insert_all(path);
            out.paths.push_back(std::move(path));
        }
    }

    out.cells = network;
    static constexpr std::array<std::pair<int, int>, 4> kOrthogonal = {
        std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}};
    // Widen the centreline to two cells where the step is gentle. Reads the centreline,
    // not the growing road set, so the widening cannot cascade outward.
    for (Cell c : network.sorted()) {
        for (const auto& [dx, dz] : kOrthogonal) {
            const Cell q{c.first + dx, c.second + dz};
            if (!valid.contains(q) || distances[index_of(q)] == kInfinity) continue;
            if (std::abs(heights[index_of(q)] - heights[index_of(c)]) <= kCliffStepM) out.cells.insert(q);
        }
    }
    return out;
}

double path_turn_degrees(Point a, Point b, Point c) {
    double delta = degrees_of(std::atan2(c.z - b.z, c.x - b.x) - std::atan2(b.z - a.z, b.x - a.x));
    while (delta <= -180.0) delta += 360.0;
    while (delta > 180.0) delta -= 360.0;
    return delta;
}

std::vector<Point> smooth_closed_ring(const std::vector<Point>& points, double max_turn_degrees,
                                      double radius_factor, int passes) {
    if (points.size() < 8) return points;
    std::vector<Point> pts = points;
    const auto n = static_cast<int>(pts.size());
    double cx = 0.0;
    double cz = 0.0;
    for (const Point& p : pts) {
        cx += p.x;
        cz += p.z;
    }
    cx /= n;
    cz /= n;

    for (int pass = 0; pass < passes; ++pass) {
        std::vector<double> radii(pts.size());
        for (int i = 0; i < n; ++i) {
            const double r = std::hypot(pts[static_cast<std::size_t>(i)].x - cx,
                                        pts[static_cast<std::size_t>(i)].z - cz);
            // A vertex exactly at the centroid divides by one and collapses onto the
            // centroid rather than dividing by zero.
            radii[static_cast<std::size_t>(i)] = r != 0.0 ? r : 1.0;
        }
        std::vector<Point> next(pts.size());
        for (int i = 0; i < n; ++i) {
            const auto k = static_cast<std::size_t>(i);
            const double cap = radius_factor * std::max(radii[static_cast<std::size_t>((i - 1 + n) % n)],
                                                        radii[static_cast<std::size_t>((i + 1) % n)]);
            const double r = std::min(radii[k], cap);
            const double ux = (pts[k].x - cx) / radii[k];
            const double uz = (pts[k].z - cz) / radii[k];
            next[k] = {cx + ux * r, cz + uz * r};
        }
        // Jacobi, not in place: a sweep that read its own output would drag the whole
        // ring toward whichever vertex it happened to start at.
        pts = next;
        for (int i = 0; i < n; ++i) {
            const auto k = static_cast<std::size_t>(i);
            const Point prev = pts[static_cast<std::size_t>((i - 1 + n) % n)];
            const Point following = pts[static_cast<std::size_t>((i + 1) % n)];
            if (std::abs(path_turn_degrees(prev, pts[k], following)) > max_turn_degrees) {
                next[k] = {(prev.x + 2.0 * pts[k].x + following.x) / 4.0,
                           (prev.z + 2.0 * pts[k].z + following.z) / 4.0};
            } else {
                next[k] = pts[k];
            }
        }
        pts = next;
    }
    for (Point& p : pts) p = {round_to(p.x, 2), round_to(p.z, 2)};
    return pts;
}

// ===========================================================================
// Programme sizing
// ===========================================================================

namespace {

double class_base_half(CityClass city_class) {
    switch (city_class) {
        case CityClass::Capital: return 400.0;
        case CityClass::Medium: return 320.0;
        case CityClass::Small: return 240.0;
    }
    return 240.0;
}

}  // namespace

double program_half_m(const SettlementPreset& preset, CityClass city_class, double neighbour_half,
                      PlotSize house_plot, int beds_per_house) {
    const double base = class_base_half(city_class);
    double plot_area = 0.0;
    long long workers = 0;
    for (const PresetRow& row : preset.buildings) {
        const Structure& s = row.structure();
        plot_area += s.plot_m.width * s.plot_m.depth * row.count;
        workers += static_cast<long long>(s.workers) * row.count;
    }
    const auto houses = static_cast<double>(
        (workers + std::max(1, beds_per_house) - 1) / std::max(1, beds_per_house));
    plot_area += houses * house_plot.width * house_plot.depth;
    // Streets, wall reserve and access corridors roughly double service and housing
    // land; 2.4 is that doubling plus the wall reserve, and 1.15 is fifteen per cent of
    // slack on the half-edge.
    const double needed = std::sqrt(std::max(plot_area, 1.0) * 2.4) / 2.0 * 1.15;
    const double half = std::min(neighbour_half, std::max(base, needed));
    return std::max(4.0, std::floor(half / 4.0) * 4.0);
}

int ring_count_for(std::string_view shape_id, const ShapeSelection& shape, CityClass city_class) {
    if (shape_id == "concentric_enceintes") {
        return std::max(2, std::min(3, static_cast<int>(shape.parameter("ring_count", 3.0))));
    }
    if (city_class == CityClass::Medium || city_class == CityClass::Capital) return 1;
    return 0;
}

// ===========================================================================
// The local sampler
// ===========================================================================

namespace {

struct Sample {
    bool water = false;
    double slope = 0.0;
    int channel = 0;
    double flood_risk = 0.0;
    double height = 0.0;
    double moisture = 0.5;
    int biome = 3;
    int variant = -1;
};

// A gnomonic tangent-plane frame at one raster node, with the routed river centrelines
// that reach the window projected into it.
class LocalSampler {
public:
    LocalSampler(const World& world, const SphereGrid& grid, double node_x, double node_z, double half)
        : field_(world), n_(world.config.size), radius_(world.effective.globe_radius) {
        const double lat = kPi / 2.0 - kPi * node_z / (n_ - 1);
        const double lon = -kPi + 2.0 * kPi * node_x / (n_ - 1);
        up_ = {std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon)};
        east_ = {-std::sin(lon), 0.0, std::cos(lon)};
        north_ = {-std::sin(lat) * std::cos(lon), std::cos(lat), -std::sin(lat) * std::sin(lon)};

        slope_ = layer(world, "slope");
        water_type_ = layer(world, "water_type");
        river_ = layer(world, "river");
        flood_ = layer(world, "flood_risk");
        moisture_ = layer(world, "moisture");
        biome_ = layer(world, "natural_biome");
        variant_ = layer(world, "biome_variant");

        if (world.has_water) {
            for (const auto& segment : world.water.river_segments) {
                const auto a = project(grid, segment[0]);
                const auto b = project(grid, segment[1]);
                if (!a || !b) continue;
                // The pad covers the 28 m setback plus slack: a centreline just outside
                // the window still reserves ground inside it.
                const bool overlaps_x = std::min(a->x, b->x) <= half + kRiverPadM &&
                                        std::max(a->x, b->x) >= -half - kRiverPadM;
                const bool overlaps_z = std::min(a->z, b->z) <= half + kRiverPadM &&
                                        std::max(a->z, b->z) >= -half - kRiverPadM;
                if (overlaps_x && overlaps_z) lines_.push_back({*a, *b});
            }
        }
    }

    double resolution() const { return kPi * radius_ / (n_ - 1); }
    double radius() const { return radius_; }
    const Vec3& up() const { return up_; }
    const Vec3& east() const { return east_; }
    const Vec3& north() const { return north_; }

    Vec3 direction_at(double x, double z) const {
        Vec3 p{up_.x + (east_.x * x + north_.x * z) / radius_,
               up_.y + (east_.y * x + north_.y * z) / radius_,
               up_.z + (east_.z * x + north_.z * z) / radius_};
        const double length = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        return {p.x / length, p.y / length, p.z / length};
    }

    // Elevation alone, for the callers that read only the height. It builds and
    // normalises the very same vector sample() does and the height field is a pure
    // function of it, so this returns the identical metre; what it skips is the river
    // distance loop, two inverse-trigonometric grid-index calls and six layer lookups.
    // The surface grid alone asks for this (size+1) squared times per settlement.
    double height_at(double x, double z) const { return field_.height(direction_at(x, z)); }

    Sample sample(double x, double z) const {
        const Vec3 p = direction_at(x, z);
        int gx = static_cast<int>(std::llround((std::atan2(p.z, p.x) + kPi) / (2.0 * kPi) * (n_ - 1))) % (n_ - 1);
        if (gx < 0) gx += n_ - 1;
        // clamp_unit, not a bare asin: rounding pushes a unit-vector component a few ulp
        // past one often enough, and the NaN that follows indexes the grid at random.
        const int gz = static_cast<int>(
            clamp(std::round((kPi / 2.0 - std::asin(clamp_unit(p.y))) / kPi * (n_ - 1)), 0.0, n_ - 1));

        const double distance = river_distance(x, z);
        Sample out;
        // Real local slope by central difference over two metres. The reference reads the
        // coarse slope layer when the world has no continuous relief definition; this
        // HeightField derives one from the world seed unconditionally, so there is no
        // such world here and reading the coarse layer would contradict the very heights
        // this plan goes on to emit. The layer is still the base value, so a caller can
        // see it was consulted.
        out.slope = slope_ != nullptr ? slope_->at(gx, gz) : 0.0;
        {
            const double dx = (height_at(x + 1.0, z) - height_at(x - 1.0, z)) / 2.0;
            const double dz = (height_at(x, z + 1.0) - height_at(x, z - 1.0)) / 2.0;
            out.slope = degrees_of(std::atan(std::hypot(dx, dz)));
        }
        // `channel` is local: this point is inside the reserved river channel and
        // setback, in metres from a routed centreline. `flood_risk` is the REGIONAL
        // proxy, one value for a raster cell thousands of metres across -- far larger
        // than a town, so it is the same number everywhere in this footprint and could
        // only ever block all of it or none of it.
        const double river_strength = river_ != nullptr ? river_->at(gx, gz) : 0.0;
        out.channel = (!lines_.empty() && river_strength > 0.5 && distance < kRiverChannelM) ? 1 : 0;
        const double water_type = water_type_ != nullptr ? water_type_->at(gx, gz) : 0.0;
        out.water = water_type != 0.0 || distance < kRiverWaterM;
        out.flood_risk = flood_ != nullptr ? flood_->at(gx, gz) : 0.0;
        out.height = field_.height(p);
        out.moisture = moisture_ != nullptr ? moisture_->at(gx, gz) : 0.5;
        out.biome = biome_ != nullptr ? static_cast<int>(std::lround(biome_->at(gx, gz))) : 3;
        out.variant = variant_ != nullptr ? static_cast<int>(std::lround(variant_->at(gx, gz))) : -1;
        return out;
    }

private:
    struct Line {
        Point a;
        Point b;
    };

    static const Grid* layer(const World& world, const std::string& name) {
        const auto found = world.layers.find(name);
        return found == world.layers.end() ? nullptr : &found->second;
    }

    std::optional<Point> project(const SphereGrid& grid, int node) const {
        if (node < 0 || static_cast<std::size_t>(node) >= grid.node_count()) return std::nullopt;
        const Vec3 p = direction(grid.point_x[static_cast<std::size_t>(node)],
                                 grid.point_z[static_cast<std::size_t>(node)], n_);
        const double den = p.x * up_.x + p.y * up_.y + p.z * up_.z;
        // More than about 84 degrees away and the projection blows up.
        if (den <= kHorizonDen) return std::nullopt;
        return Point{radius_ * (p.x * east_.x + p.y * east_.y + p.z * east_.z) / den,
                     radius_ * (p.x * north_.x + p.y * north_.y + p.z * north_.z) / den};
    }

    double river_distance(double x, double z) const {
        double best = 1e30;
        for (const Line& line : lines_) {
            const double dx = line.b.x - line.a.x;
            const double dz = line.b.z - line.a.z;
            const double denominator = dx * dx + dz * dz;
            const double t = clamp(((x - line.a.x) * dx + (z - line.a.z) * dz) /
                                       (denominator != 0.0 ? denominator : 1.0),
                                   0.0, 1.0);
            best = std::min(best, std::hypot(x - line.a.x - t * dx, z - line.a.z - t * dz));
        }
        return best;
    }

    HeightField field_;
    int n_;
    double radius_;
    Vec3 up_{};
    Vec3 east_{};
    Vec3 north_{};
    const Grid* slope_ = nullptr;
    const Grid* water_type_ = nullptr;
    const Grid* river_ = nullptr;
    const Grid* flood_ = nullptr;
    const Grid* moisture_ = nullptr;
    const Grid* biome_ = nullptr;
    const Grid* variant_ = nullptr;
    std::vector<Line> lines_;
};

double great_circle_m(double radius, int n, double az, double ax, double bz, double bx) {
    const double lat1 = kPi / 2.0 - kPi * az / (n - 1);
    const double lat2 = kPi / 2.0 - kPi * bz / (n - 1);
    // Longitude difference only, and the seam is NOT wrapped: a pair straddling it
    // measures the long way round. That is the existing behaviour and it only ever
    // makes a crop larger, never overlapping.
    const double dl = 2.0 * kPi * (bx - ax) / (n - 1);
    return radius * std::acos(clamp(std::sin(lat1) * std::sin(lat2) +
                                        std::cos(lat1) * std::cos(lat2) * std::cos(dl),
                                    -1.0, 1.0));
}

}  // namespace

// ===========================================================================
// Fortifications
// ===========================================================================

namespace {

// Where the chosen shape says a city may spread.
//
// ONE predicate serves both the plot clip and the wall-ray cut-off. The reference has
// two, and for the rectangular families they are not complements of each other: the
// clip excludes a point exactly on the boundary and the ray walk keeps it. That only
// shows on exact equality, which the shipped extents never produce, and one predicate
// is the version that stays true if they ever do.
struct ShapeClip {
    std::string_view family;
    double half = 0.0;
    double rx = 0.0;
    double rz = 0.0;

    bool inside(double x, double z, double scale = 1.0) const {
        if (family == "grid" || family == "compound" || family == "hybrid") {
            return std::abs(x) < rx * scale && std::abs(z) < rz * scale;
        }
        if (family == "cluster") {
            const double r = half * 0.52 * scale;
            const std::array<Point, 3> discs = {Point{-half * 0.32, 0.0}, Point{half * 0.32, 0.0},
                                                Point{0.0, half * 0.25}};
            for (Point c : discs) {
                const double dx = x - c.x;
                const double dz = z - c.z;
                if (dx * dx + dz * dz < r * r) return true;
            }
            return false;
        }
        // Organic, contour, rings and paired lobes share the elliptical clip; ring scales
        // are fortification-only.
        const double ex = x / (rx * scale);
        const double ez = z / (rz * scale);
        return ex * ex + ez * ez < 1.0;
    }
};

std::vector<Point> boundary_points(const CellSet& valid, double half, double cell,
                                   const ShapeClip& clip, double scale) {
    if (valid.empty()) return {};
    double cx = 0.0;
    double cz = 0.0;
    const std::vector<Cell> cells = valid.sorted();
    for (Cell c : cells) {
        cx += c.first;
        cz += c.second;
    }
    // The centroid is in CELL INDEX units, not metres. The rays walk the raster.
    cx /= static_cast<double>(cells.size());
    cz /= static_cast<double>(cells.size());

    const int reach = std::max(1, static_cast<int>(std::max(clip.rx, clip.rz) / cell) + 2);
    std::vector<Point> points;
    for (int k = 0; k < 48; ++k) {
        const double angle = 2.0 * kPi * k / 48.0;
        const double dx = std::cos(angle);
        const double dz = std::sin(angle);
        bool found = false;
        Point best{};
        for (int dist = 1; dist < reach; ++dist) {
            const Cell probe{static_cast<int>(std::floor(cx + dx * dist)),
                             static_cast<int>(std::floor(cz + dz * dist))};
            if (!valid.contains(probe)) break;
            const double ex = -half + (probe.first + 0.5) * cell;
            const double ez = -half + (probe.second + 0.5) * cell;
            if (!clip.inside(ex, ez, scale)) break;
            best = {round_to(ex, 2), round_to(ez, 2)};
            found = true;
        }
        if (found) points.push_back(best);
    }
    return points;
}

struct Segmentation {
    std::vector<WallSegment> segments;
    std::vector<WallNode> nodes;
};

Segmentation segmentize(const std::vector<Point>& polyline, double segment_depth,
                        std::string_view structure_id, const std::string& ring_id) {
    Segmentation out;
    if (polyline.size() < 2) return out;
    for (std::size_t i = 0; i < polyline.size(); ++i) {
        out.nodes.push_back({ring_id + "-node-" + std::to_string(i), "corner", polyline[i], ring_id});
    }
    std::vector<Point> closed = polyline;
    closed.push_back(polyline.front());

    int seg_i = 0;
    double carry = 0.0;
    std::vector<Point> path;
    for (std::size_t k = 0; k + 1 < closed.size(); ++k) {
        Point a = closed[k];
        const Point b = closed[k + 1];
        // Overwriting the leftover point with the polyline VERTEX is what makes the
        // first segment on each edge start at the corner and come out short by the
        // carried remainder, so lengths vary rather than the ring drifting off its own
        // corners.
        path.push_back(a);
        double edge = distance2d(a, b);
        while (carry + edge >= segment_depth - 0.01) {
            const double need = segment_depth - carry;
            const double t = edge != 0.0 ? need / edge : 0.0;
            const Point end{round_to(a.x + (b.x - a.x) * t, 2), round_to(a.z + (b.z - a.z) * t, 2)};
            const Point start = path.empty() ? a : path.back();
            const Point mid{(start.x + end.x) / 2.0, (start.z + end.z) / 2.0};
            const double heading = degrees_of(std::atan2(end.z - start.z, end.x - start.x));
            out.segments.push_back({ring_id + "-seg-" + std::to_string(seg_i), ring_id, structure_id,
                                    start, end, {round_to(mid.x, 2), round_to(mid.z, 2)},
                                    round_to(distance2d(start, end), 3),
                                    // A wall module's depth axis runs along the wall.
                                    round_to(heading - 90.0, 4)});
            ++seg_i;
            path.assign(1, end);
            a = end;
            edge = distance2d(a, b);
            carry = 0.0;
        }
        carry += edge;
    }
    return out;
}

struct GateSite {
    std::size_t ring_vertex = 0;
    std::optional<int> route_index;
    Point position_m;
    Point approach_m;
};

std::vector<GateSite> gate_sites(const std::vector<Point>& polyline,
                                 const std::vector<RoadConnection>& connections, int gate_budget) {
    if (polyline.empty() || gate_budget <= 0) return {};
    struct Row {
        double distance;
        std::size_t index;
        std::optional<int> route;
        Point gate;
    };
    std::vector<Row> rows;
    for (const RoadConnection& conn : connections) {
        if (conn.status != "connected") continue;
        std::size_t best = 0;
        double best_d = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < polyline.size(); ++i) {
            const double d = distance2d(polyline[i], conn.gate_local_m);
            if (d < best_d) {
                best_d = d;
                best = i;
            }
        }
        rows.push_back({best_d, best, conn.route_index, conn.gate_local_m});
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.distance != b.distance) return a.distance < b.distance;
        if (a.index != b.index) return a.index < b.index;
        // The reference sorts the raw tuple here, which raises when two rows tie on
        // distance AND vertex and one of them has no route index. Ordered explicitly
        // instead: an absent index sorts first.
        if (a.route.has_value() != b.route.has_value()) return !a.route.has_value();
        if (a.route.has_value() && *a.route != *b.route) return *a.route < *b.route;
        if (a.gate.x != b.gate.x) return a.gate.x < b.gate.x;
        return a.gate.z < b.gate.z;
    });

    std::vector<GateSite> chosen;
    std::vector<std::size_t> used;
    for (const Row& row : rows) {
        if (std::find(used.begin(), used.end(), row.index) != used.end()) continue;
        // A regional approach further than this from any ring vertex opens no gate: the
        // opening has to be where the road actually arrives.
        if (row.distance > 48.0) continue;
        used.push_back(row.index);
        chosen.push_back({row.index, row.route, polyline[row.index], row.gate});
        if (static_cast<int>(chosen.size()) >= gate_budget) break;
    }
    if (chosen.empty()) {
        // Ensure at least one schematic gate on the primary approach axis: the
        // easternmost vertex nearest the z = 0 line.
        double max_x = polyline.front().x;
        for (const Point& p : polyline) max_x = std::max(max_x, p.x);
        std::size_t best = 0;
        double best_score = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < polyline.size(); ++i) {
            const double score = std::abs(polyline[i].z) + std::abs(polyline[i].x - max_x);
            if (score < best_score) {
                best_score = score;
                best = i;
            }
        }
        chosen.push_back({best, std::nullopt, polyline[best], polyline[best]});
    }
    if (static_cast<int>(chosen.size()) > gate_budget) {
        chosen.resize(static_cast<std::size_t>(gate_budget));
    }
    return chosen;
}

// The wall and gate ROWS, not just their structures: the row's own name wins over the
// catalogue's, and a people that calls its gatehouse something else should see that on
// the plot. Only one entity in twelve renames anything today, which is exactly why a
// port that read the structure directly would look correct for years.
struct WallDefs {
    const PresetRow* wall = nullptr;
    const PresetRow* gate = nullptr;
    // A wall is looked up in infrastructure first and a gatehouse in buildings first.
    // The fallbacks are what a preset that files them the other way round relies on.
    std::string_view wall_id = "building.wall";
    std::string_view gate_id = "building.gatehouse";
};

WallDefs wall_and_gate_defs(const SettlementPreset& preset) {
    WallDefs defs;
    const auto find = [](std::span<const PresetRow> rows, std::string_view id) -> const PresetRow* {
        for (const PresetRow& row : rows) {
            if (row.structure().id == id) return &row;
        }
        return nullptr;
    };
    defs.wall = find(preset.infrastructure, "building.wall");
    if (defs.wall == nullptr) defs.wall = find(preset.buildings, "building.wall");
    defs.gate = find(preset.buildings, "building.gatehouse");
    if (defs.gate == nullptr) defs.gate = find(preset.infrastructure, "building.gatehouse");
    if (defs.wall != nullptr) defs.wall_id = defs.wall->structure().id;
    if (defs.gate != nullptr) defs.gate_id = defs.gate->structure().id;
    return defs;
}

Fortifications build_fortifications(const CellSet& valid, double half, double cell,
                                    const ShapeClip& clip, int rings_wanted,
                                    const std::vector<RoadConnection>& connections,
                                    const WallDefs& defs, int tower_budget, int gate_budget) {
    Fortifications report;
    if (rings_wanted <= 0 || valid.empty() || defs.wall == nullptr) {
        if (rings_wanted > 0) {
            report.unplaced.push_back({defs.wall_id, "", "No buildable perimeter for fortification"});
        }
        return report;
    }
    std::vector<double> scales;
    if (rings_wanted == 1) {
        scales.push_back(1.0);
    } else {
        for (int i = 0; i < rings_wanted; ++i) {
            scales.push_back(0.45 + (static_cast<double>(i) / (rings_wanted - 1)) * 0.55);
        }
    }
    // The wall module's DEPTH is the nominal run of one segment.
    const double segment_depth = defs.wall->structure().dimensions_m.depth;

    for (int ring_i = 0; ring_i < static_cast<int>(scales.size()); ++ring_i) {
        const std::string ring_id = "ring-" + std::to_string(ring_i);
        const std::string_view role =
            ring_i == rings_wanted - 1 ? "outer" : (ring_i == 0 ? "inner" : "middle");
        const std::vector<Point> points =
            smooth_closed_ring(boundary_points(valid, half, cell, clip, scales[static_cast<std::size_t>(ring_i)]));
        if (points.size() < 8) {
            report.unplaced.push_back({defs.wall_id, ring_id, "Perimeter could not close on buildable land"});
            continue;
        }
        Segmentation cut = segmentize(points, segment_depth, defs.wall_id, ring_id);
        if (cut.segments.size() < 6) {
            report.unplaced.push_back({defs.wall_id, ring_id, "Insufficient continuous wall segments"});
            continue;
        }
        // Only the outer ring sees the real approaches; inner rings get a schematic gate.
        const std::vector<RoadConnection> empty;
        const std::vector<GateSite> sites =
            gate_sites(points, role == "outer" ? connections : empty,
                       role == "outer" ? gate_budget : std::max(1, gate_budget / 2));

        std::vector<WallGate> gates;
        for (std::size_t g = 0; g < sites.size(); ++g) {
            gates.push_back({ring_id + "-gate-" + std::to_string(g), ring_id, defs.gate_id,
                             sites[g].position_m, sites[g].approach_m, sites[g].route_index, ""});
        }
        // Drop wall segments colliding with a gate centre, so the opening is real.
        const double collision = std::max(8.0, segment_depth * 0.6);
        std::vector<WallSegment> keep;
        for (const WallSegment& seg : cut.segments) {
            bool blocked = false;
            for (const WallGate& gate : gates) {
                if (distance2d(seg.center_m, gate.position_m) < collision) {
                    blocked = true;
                    break;
                }
            }
            if (!blocked) keep.push_back(seg);
        }

        const int ring_towers = role == "outer" ? tower_budget : std::max(1, tower_budget / 2);
        const auto step = std::max<std::size_t>(1, cut.nodes.size() / static_cast<std::size_t>(std::max(1, ring_towers)));
        const auto cap = static_cast<std::size_t>(role == "outer" ? std::max(1, tower_budget) : 2);
        std::vector<WallTower> towers;
        for (std::size_t i = 0; i < cut.nodes.size() && towers.size() < cap; i += step) {
            towers.push_back({ring_id + "-tower-" + std::to_string(towers.size()), ring_id,
                              "building.tower", cut.nodes[i].position_m, ""});
        }

        report.rings.push_back({ring_id, role, round_to(scales[static_cast<std::size_t>(ring_i)], 4),
                                points, "closed", static_cast<int>(keep.size()),
                                static_cast<int>(gates.size())});
        report.segments.insert(report.segments.end(), keep.begin(), keep.end());
        report.nodes.insert(report.nodes.end(), cut.nodes.begin(), cut.nodes.end());
        report.gates.insert(report.gates.end(), gates.begin(), gates.end());
        report.towers.insert(report.towers.end(), towers.begin(), towers.end());
    }
    for (const WallRing& ring : report.rings) {
        if (ring.status == "closed") report.defended_perimeter = true;
    }
    return report;
}

}  // namespace

// ===========================================================================
// The packing engine, shared by both planners
// ===========================================================================

namespace {

struct Anchor {
    double x;
    double z;
    double angle;
};

struct Candidate {
    double x = 0.0;
    double z = 0.0;
    double degrees = 0.0;
    Point anchor;
    std::vector<Cell> footprint;
    std::vector<Cell> corridor;
    double ground_max = 0.0;
    double ground_min = 0.0;
};

// A generator's output, remembered so it can be walked any number of times.
//
// `install` takes the first free option and stops, so the eager list built the whole
// anchor sweep for every plot size and never looked at most of it. Yielding on demand
// and keeping what was yielded leaves the order and the first match identical -- no plot
// moves -- and simply stops producing options past the deepest one anybody asked for.
class CandidateStream {
public:
    CandidateStream(double width, double depth, const std::vector<Anchor>& anchors, double half,
                    int size, double setback, const CellSet& valid, const CellSet& road,
                    const std::vector<int>& clearance, const std::vector<double>& heights,
                    const LocalSampler& sampler,
                    std::map<std::array<double, 4>, std::vector<Cell>>& corridor_cache)
        : w_(width), d_(depth), anchors_(anchors), half_(half), size_(size), setback_(setback),
          valid_(valid), road_(road), clearance_(clearance), heights_(heights), sampler_(sampler),
          corridor_cache_(corridor_cache) {}

    // The option at `index`, or nullptr once the sweep is exhausted.
    const Candidate* at(std::size_t index) {
        while (memo_.size() <= index) {
            if (!advance()) return nullptr;
        }
        return &memo_[index];
    }

    // Asking how many options there are is asking for all of them, so this is a full
    // pass by definition -- the same work the eager list used to do up front.
    std::size_t size() {
        while (advance()) {
        }
        return memo_.size();
    }

    const std::vector<Candidate>& memo() const { return memo_; }

private:
    bool advance() {
        while (anchor_ < anchors_.size()) {
            const Anchor a = anchors_[anchor_];
            const int side = side_ == 0 ? 1 : -1;
            ++side_;
            if (side_ > 1) {
                side_ = 0;
                ++anchor_;
            }
            Candidate option;
            if (build(a, side, option)) {
                memo_.push_back(std::move(option));
                return true;
            }
        }
        return false;
    }

    bool build(const Anchor& anchor, int side, Candidate& out) {
        // The street's left/right normal.
        const double dx = -std::sin(anchor.angle) * side;
        const double dz = std::cos(anchor.angle) * side;
        const double x = round_to(anchor.x + dx * (d_ / 2.0 + setback_), 2);
        const double z = round_to(anchor.z + dz * (d_ / 2.0 + setback_), 2);
        const double degrees = round_to(degrees_of(anchor.angle), 4);
        // The rotation the plot RECORDS is the rounded one, so the footprint is
        // rasterised at the angle the consumer will place the building at, not at the
        // anchor's own. Round-tripping through degrees here is what keeps those two the
        // same rectangle.
        //
        // The reference rebinds its loop variable at this point, so the SECOND side of
        // every street takes its normal from the rounded angle while the first takes it
        // from the raw one -- the two sides of a street are not mirror images there.
        // Both sides use the raw angle here, deliberately: the difference is under a
        // microradian and moves about one candidate in three hundred by a centimetre,
        // and an asymmetry nobody meant is not worth reproducing.
        const double angle = radians(degrees);

        const auto ci = static_cast<int>(std::floor((x + half_) / kCell));
        const auto cj = static_cast<int>(std::floor((z + half_) / kCell));
        if (ci >= 0 && ci < size_ && cj >= 0 && cj < size_) {
            // The nearest cell that is NOT free ground has its centre within (k+1) cells
            // of this plot centre in Chebyshev terms, so within (k+1)*cell*sqrt(2)
            // Euclidean. When that lands inside the inscribed circle it is inside the
            // rectangle and therefore inside the footprint, so the two tests below are
            // already decided and the rasterization is skipped. It can never fire on a
            // candidate they would have accepted.
            const int k = clearance_[static_cast<std::size_t>(cj) * static_cast<std::size_t>(size_) +
                                     static_cast<std::size_t>(ci)];
            if ((k + 1) * kCell * kRoot2 <= std::min(w_, d_) / 2.0) return false;
        }

        std::vector<Cell> footprint = footprint_cells(x, z, w_, d_, angle, half_, kCell);
        if (!valid_.contains_all(footprint) || road_.intersects(footprint)) return false;

        // The actual interpolated ground, including within a coarse raster cell: four
        // rotated corner elevations from the continuous field, plus the discrete cell
        // elevations already sampled for every footprint cell.
        double ground_max = -std::numeric_limits<double>::infinity();
        double ground_min = std::numeric_limits<double>::infinity();
        for (const Point& p : corners(x, z, w_, d_, angle)) {
            const double h = sampler_.height_at(p.x, p.z);
            ground_max = std::max(ground_max, h);
            ground_min = std::min(ground_min, h);
        }
        for (Cell c : footprint) {
            const double h = heights_[static_cast<std::size_t>(c.second) * static_cast<std::size_t>(size_) +
                                      static_cast<std::size_t>(c.first)];
            ground_max = std::max(ground_max, h);
            ground_min = std::min(ground_min, h);
        }
        if (ground_max - ground_min > std::hypot(w_, d_) * std::tan(radians(kSlopeLimitDegrees))) return false;

        // An access corridor depends only on its two endpoints, so every building that
        // shares a plot depth reuses the same one. Keyed on the endpoints AS COMPUTED and
        // not a tidied version of them, so this can only ever return what the eleven
        // rasterizations would have produced.
        const double ex = x - dx * d_ / 2.0;
        const double ez = z - dz * d_ / 2.0;
        const std::array<double, 4> key{anchor.x, anchor.z, ex, ez};
        auto cached = corridor_cache_.find(key);
        if (cached == corridor_cache_.end()) {
            // Eleven 4 m footprints along the spur, unioned. Cells OFF the raster are
            // kept like any other, so a corridor that leaves the window fails the
            // containment test below rather than being quietly trimmed to fit.
            std::vector<Cell> corridor;
            for (int t = 0; t <= 10; ++t) {
                const double fraction = t / 10.0;
                for (Cell c : footprint_cells(anchor.x + (ex - anchor.x) * fraction,
                                              anchor.z + (ez - anchor.z) * fraction, kCell, kCell, 0.0,
                                              half_, kCell)) {
                    corridor.push_back(c);
                }
            }
            std::sort(corridor.begin(), corridor.end());
            corridor.erase(std::unique(corridor.begin(), corridor.end()), corridor.end());
            cached = corridor_cache_.emplace(key, std::move(corridor)).first;
        }
        if (!valid_.contains_all(cached->second)) return false;

        out.x = x;
        out.z = z;
        out.degrees = degrees;
        out.anchor = {anchor.x, anchor.z};
        out.footprint = std::move(footprint);
        out.corridor = cached->second;
        out.ground_max = ground_max;
        out.ground_min = ground_min;
        return true;
    }

    double w_;
    double d_;
    const std::vector<Anchor>& anchors_;
    double half_;
    int size_;
    double setback_;
    const CellSet& valid_;
    const CellSet& road_;
    const std::vector<int>& clearance_;
    const std::vector<double>& heights_;
    const LocalSampler& sampler_;
    std::map<std::array<double, 4>, std::vector<Cell>>& corridor_cache_;
    std::vector<Candidate> memo_;
    std::size_t anchor_ = 0;
    int side_ = 0;
};

// What install() needs to know about the thing being placed. Preset rows and housing
// profiles both reduce to this, which is why housing needs no preset row of its own.
struct Installable {
    std::string_view building_id;
    std::string_view name;
    PlotSize plot_m;
    BoxSize dimensions_m;
    int workers = 0;
    int beds = 0;
};

Installable from_row(const PresetRow& row) {
    const Structure& s = row.structure();
    return {s.id, row.display_name(), s.plot_m, s.dimensions_m, s.workers, 0};
}

Installable from_house(const HousingProfile& house) {
    return {house.id, house.name, house.plot_m, house.dimensions_m, 0, house.worker_beds};
}

// Street frontage anchors, centre-outward.
//
// The +/-3 step window gives a locally smoothed street heading and the +/-5 degree
// sinusoidal term is stable variation that FOLLOWS the street -- deterministic in
// position and the street seed, not random per call. Ordering by squared distance from
// the local origin is the entire placement priority for the settlement: the first
// buildings installed take the most central frontage.
std::vector<Anchor> street_anchors(const std::vector<std::vector<Point>>& paths, std::uint64_t seed) {
    std::vector<Anchor> anchors;
    const double jitter_phase = static_cast<double>(seed % 1000);
    for (const std::vector<Point>& path : paths) {
        for (std::size_t i = 0; i < path.size(); ++i) {
            const Point a = path[i >= 3 ? i - 3 : 0];
            const Point b = path[std::min(path.size() - 1, i + 3)];
            if (a.x == b.x && a.z == b.z) continue;
            const Point at = path[i];
            double angle = std::atan2(b.z - a.z, b.x - a.x);
            angle += radians(5.0 * std::sin(at.x * 0.13 + at.z * 0.17 + jitter_phase));
            anchors.push_back({at.x, at.z, angle});
        }
    }
    const auto key = [](const Anchor& p) {
        return std::array<double, 4>{p.x * p.x + p.z * p.z, p.x, p.z, p.angle};
    };
    std::stable_sort(anchors.begin(), anchors.end(),
              [&](const Anchor& a, const Anchor& b) { return key(a) < key(b); });
    anchors.erase(std::unique(anchors.begin(), anchors.end(),
                              [](const Anchor& a, const Anchor& b) {
                                  return a.x == b.x && a.z == b.z && a.angle == b.angle;
                              }),
                  anchors.end());
    return anchors;
}

// Everything the two planners share: the occupancy sets, the candidate streams, and the
// install / reserve / house protocol built on them.
class Packer {
public:
    Packer(int size, const std::vector<Anchor>& anchors, double half, double setback,
           const CellSet& valid, const CellSet& road, const std::vector<int>& clearance,
           const std::vector<double>& heights, const LocalSampler& sampler,
           std::vector<Plot>& plots, std::vector<Pass>& passes)
        : size_(size), anchors_(anchors), half_(half), setback_(setback), valid_(valid), road_(road),
          clearance_(clearance), heights_(heights), sampler_(sampler), plots_(plots), passes_(passes),
          occupied_(size), access_(size), reserved_(size) {}

    CandidateStream& stream(PlotSize plot) {
        const std::array<double, 2> key{plot.width, plot.depth};
        auto found = streams_.find(key);
        if (found == streams_.end()) {
            found = streams_
                        .emplace(key, CandidateStream(plot.width, plot.depth, anchors_, half_, size_,
                                                      setback_, valid_, road_, clearance_, heights_,
                                                      sampler_, corridor_cache_))
                        .first;
        }
        return found->second;
    }

    bool free_for(const Candidate& option, bool housing) const {
        if (occupied_.intersects(option.footprint) || access_.intersects(option.footprint) ||
            occupied_.intersects(option.corridor)) {
            return false;
        }
        // Housing is the one kind allowed to take reserved ground: the reservations exist
        // for it.
        if (!housing && (reserved_.intersects(option.footprint) || reserved_.intersects(option.corridor))) {
            return false;
        }
        return true;
    }

    void claim(const Candidate& option) {
        occupied_.insert_all(option.footprint);
        // The corridor MINUS this plot's own footprint. Nothing else can be in the way:
        // free_for has already refused any option whose corridor meets occupied ground,
        // so anything occupied here is this footprint. A corridor may cross another
        // corridor -- that is the point of keeping access separate from occupied.
        for (Cell c : option.corridor) {
            if (!occupied_.contains(c)) access_.insert(c);
        }
        reserved_.erase_all(option.footprint);
        reserved_.erase_all(option.corridor);
    }

    void record(const Candidate& option, const Installable& what, std::string_view phase,
                std::string_view kind) {
        Plot plot;
        plot.id = "plot-" + std::to_string(plots_.size());
        plot.building_id = what.building_id;
        plot.name = what.name;
        plot.kind = kind;
        plot.phase = phase;
        plot.x_m = option.x;
        plot.z_m = option.z;
        plot.rotation_degrees = option.degrees;
        plot.ground_elevation_m = round_to(option.ground_max, 4);
        plot.foundation_bottom_m = round_to(option.ground_min, 4);
        plot.plot_m = what.plot_m;
        plot.dimensions_m = what.dimensions_m;
        plot.road_access = option.anchor;
        plot.workers = what.workers;
        plot.beds = what.beds;
        plots_.push_back(std::move(plot));
        for (Pass& pass : passes_) {
            if (pass.id == phase) ++pass.placed;
        }
    }

    bool install(const Installable& what, std::string_view phase, std::string_view kind) {
        const bool housing = kind == "housing";
        CandidateStream& options = stream(what.plot_m);
        for (std::size_t k = 0;; ++k) {
            const Candidate* option = options.at(k);
            if (option == nullptr) return false;
            if (!free_for(*option, housing)) continue;
            claim(*option);
            if (housing) drop_reserved(k);
            record(*option, what, phase, kind);
            return true;
        }
    }

    // A placement handed in explicitly skips the terrain tests entirely -- only the
    // occupancy tests apply. That is intentional for a gatehouse: the wall opening is
    // where the wall says it is, even if that is on a street or on blocked ground.
    bool install_at(const Candidate& option, const Installable& what, std::string_view phase,
                    std::string_view kind) {
        if (!free_for(option, kind == "housing")) return false;
        claim(option);
        record(option, what, phase, kind);
        return true;
    }

    // Hold street-front plots so later service packing cannot starve worker houses.
    int reserve_housing(int worker_budget, const HousingProfile& house) {
        int beds = 0;
        for (const Plot& plot : plots_) beds += plot.beds;
        const int shortfall = std::max(0, worker_budget - beds);
        int need = std::max(0, (shortfall + house.worker_beds - 1) / house.worker_beds -
                                   static_cast<int>(reserve_.size()));
        CandidateStream& options = stream(house.plot_m);
        for (std::size_t k = 0; need > 0; ++k) {
            const Candidate* option = options.at(k);
            if (option == nullptr) break;
            if (occupied_.intersects(option->footprint) || access_.intersects(option->footprint) ||
                occupied_.intersects(option->corridor)) {
                continue;
            }
            if (reserved_.intersects(option->footprint) || reserved_.intersects(option->corridor)) continue;
            reserved_.insert_all(option->footprint);
            reserved_.insert_all(option->corridor);
            reserve_.push_back(k);
            --need;
        }
        return static_cast<int>(reserve_.size());
    }

    std::size_t reserved_slots() const { return reserve_.size(); }

    const std::vector<std::size_t>& reserve() const { return reserve_; }

    bool install_first_reserved(const HousingProfile& house, std::string_view phase) {
        CandidateStream& options = stream(house.plot_m);
        const Candidate* option = options.at(reserve_.front());
        if (option == nullptr || !free_for(*option, true)) return false;
        claim(*option);
        drop_reserved(reserve_.front());
        record(*option, from_house(house), phase, "housing");
        return true;
    }

    void abandon_first_reserved(const HousingProfile& house) {
        CandidateStream& options = stream(house.plot_m);
        const Candidate* option = options.at(reserve_.front());
        if (option != nullptr) {
            reserved_.erase_all(option->footprint);
            reserved_.erase_all(option->corridor);
        }
        reserve_.erase(reserve_.begin());
    }

    // Unused holds are released from the END so later lower-priority services may use
    // the land.
    void release_reserved(const HousingProfile& house) {
        CandidateStream& options = stream(house.plot_m);
        while (!reserve_.empty()) {
            const Candidate* option = options.at(reserve_.back());
            if (option != nullptr) {
                reserved_.erase_all(option->footprint);
                reserved_.erase_all(option->corridor);
            }
            reserve_.pop_back();
        }
    }

    // Every memoised option not blocked by a placed building, its corridor or a hold.
    // Forces the whole sweep: it is a debug figure that costs the full enumeration.
    std::size_t free_frontage(PlotSize plot) {
        CandidateStream& options = stream(plot);
        const std::size_t total = options.size();
        std::size_t free = 0;
        for (std::size_t k = 0; k < total; ++k) {
            if (free_for(*options.at(k), false)) ++free;
        }
        return free;
    }

    std::size_t total_frontage(PlotSize plot) { return stream(plot).size(); }

    const CellSet& occupied() const { return occupied_; }
    const CellSet& access() const { return access_; }

private:
    void drop_reserved(std::size_t index) {
        const auto found = std::find(reserve_.begin(), reserve_.end(), index);
        if (found != reserve_.end()) reserve_.erase(found);
    }

    int size_;
    const std::vector<Anchor>& anchors_;
    double half_;
    double setback_;
    const CellSet& valid_;
    const CellSet& road_;
    const std::vector<int>& clearance_;
    const std::vector<double>& heights_;
    const LocalSampler& sampler_;
    std::vector<Plot>& plots_;
    std::vector<Pass>& passes_;
    CellSet occupied_;
    CellSet access_;
    CellSet reserved_;
    std::vector<std::size_t> reserve_;
    std::map<std::array<double, 2>, CandidateStream> streams_;
    std::map<std::array<double, 4>, std::vector<Cell>> corridor_cache_;
};

std::string unverified_reason(std::span<const PlacementCondition> missing) {
    std::string reason = "Unverified prerequisite: ";
    for (std::size_t i = 0; i < missing.size(); ++i) {
        if (i != 0) reason += ", ";
        reason.append(to_string(missing[i]));
    }
    return reason;
}

// Project the routed road path and take the window-boundary crossing as a gate.
std::vector<RoadConnection> support_road_entries(const SphereGrid& grid, const LocalSampler& sampler,
                                                 int n, const std::string& pin_id,
                                                 const std::vector<int>& access_nodes, double half) {
    std::vector<RoadConnection> entries;
    if (access_nodes.size() < 2 || grid.node_count() == 0) return entries;
    Point prev{0.0, 0.0};
    for (std::size_t k = 1; k < access_nodes.size(); ++k) {
        const int node = access_nodes[k];
        if (node < 0 || static_cast<std::size_t>(node) >= grid.node_count()) break;
        const Vec3 p = direction(grid.point_x[static_cast<std::size_t>(node)],
                                 grid.point_z[static_cast<std::size_t>(node)], n);
        const Vec3& up = sampler.up();
        const double den = p.x * up.x + p.y * up.y + p.z * up.z;
        // NOT the river projection's 0.1. The two thresholds mean different things and
        // were never meant to be unified.
        if (den <= 0.0) break;
        const Point q{sampler.radius() * (p.x * sampler.east().x + p.y * sampler.east().y +
                                          p.z * sampler.east().z) / den,
                      sampler.radius() * (p.x * sampler.north().x + p.y * sampler.north().y +
                                          p.z * sampler.north().z) / den};
        if (std::max(std::abs(q.x), std::abs(q.z)) > half) {
            double t = std::numeric_limits<double>::infinity();
            if (std::abs(q.x) > half) t = std::min(t, ((q.x > 0 ? half : -half) - prev.x) / (q.x - prev.x));
            if (std::abs(q.z) > half) t = std::min(t, ((q.z > 0 ? half : -half) - prev.z) / (q.z - prev.z));
            const Point gate{prev.x + t * (q.x - prev.x), prev.z + t * (q.z - prev.z)};
            RoadConnection entry;
            entry.route_id = "support:" + pin_id;
            entry.route_index = -1;
            entry.junction_id = "junction:" + pin_id + ":support";
            entry.gate_local_m = gate;
            entry.direction = sampler.direction_at(gate.x, gate.z);
            entry.status = "unreachable";
            entry.reason = "No terrain-safe street connection to parent-city approach";
            entries.push_back(std::move(entry));
            break;
        }
        prev = q;
    }
    return entries;
}

// The raster of local ground, and the cells a building may stand on.
struct Ground {
    TerrainRaster terrain;
    CellSet valid;
    std::vector<double> heights;  // [j*size + i]
    double mean_slope = 90.0;
    std::size_t wooded = 0;
};

Ground raster_ground(const LocalSampler& sampler, double half, int size, bool block_channels) {
    Ground out;
    out.terrain.size = size;
    out.terrain.cell_m = kCell;
    const auto area = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    out.terrain.codes.resize(area);
    out.terrain.natural_biome.resize(area);
    out.terrain.biome_variant.resize(area);
    out.heights.resize(area);
    out.valid = CellSet(size);
    double slope_total = 0.0;

    for (int j = 0; j < size; ++j) {
        for (int i = 0; i < size; ++i) {
            const Sample v = sampler.sample(-half + (i + 0.5) * kCell, -half + (j + 0.5) * kCell);
            const auto at = static_cast<std::size_t>(j) * static_cast<std::size_t>(size) +
                            static_cast<std::size_t>(i);
            out.heights[at] = v.height;
            // Standing water, real steepness and (for a town that reserves them) the
            // routed river channel block a cell. The REGIONAL flood proxy does not: it is
            // one value for a raster cell far larger than the whole settlement, so
            // honouring it here disqualifies entire towns on ground under 21 degrees.
            const int code = v.water ? 1 : ((v.slope > kSlopeLimitDegrees || (block_channels && v.channel != 0)) ? 2 : 0);
            out.terrain.codes[at] = code;
            out.terrain.natural_biome[at] = v.biome;
            out.terrain.biome_variant[at] = v.variant;
            if (code == 0) {
                out.valid.insert({i, j});
                slope_total += v.slope;
                if (is_wooded(v.biome)) ++out.wooded;
            }
        }
    }
    // 90 is a sentinel meaning nothing was buildable, not a measured slope.
    out.mean_slope = out.valid.empty() ? 90.0 : slope_total / static_cast<double>(out.valid.count());
    return out;
}

void sample_surface(Ground& ground, const LocalSampler& sampler, double half, int size) {
    const int surface_size = size + 1;
    ground.terrain.surface_size = surface_size;
    const double step = 2.0 * half / (surface_size - 1);
    ground.terrain.surface_step_m = step;
    ground.terrain.surface_heights_m.resize(static_cast<std::size_t>(surface_size) *
                                            static_cast<std::size_t>(surface_size));
    for (int j = 0; j < surface_size; ++j) {
        for (int i = 0; i < surface_size; ++i) {
            ground.terrain.surface_heights_m[static_cast<std::size_t>(j) *
                                                 static_cast<std::size_t>(surface_size) +
                                             static_cast<std::size_t>(i)] =
                round_to(sampler.height_at(-half + i * step, -half + j * step), 4);
        }
    }
}

// Promote a gate to `connected` when the ground between the street cell and the
// regional approach is safe to drive a road over.
void connect_gates(std::vector<RoadConnection>& entries, const CellSet& road,
                   const std::vector<std::vector<Cell>>& paths, const LocalSampler& sampler,
                   const std::vector<double>& heights, double half, int size,
                   std::string_view reason) {
    for (RoadConnection& c : entries) {
        const Cell cell = c.cell;
        const Point point{-half + (cell.first + 0.5) * kCell, -half + (cell.second + 0.5) * kCell};
        const Sample v = sampler.sample(c.gate_local_m.x, c.gate_local_m.z);
        const double length = distance2d(point, c.gate_local_m);
        const double h = heights[static_cast<std::size_t>(cell.second) * static_cast<std::size_t>(size) +
                                 static_cast<std::size_t>(cell.first)];
        // The same maximum grade the street tree enforces; the epsilon absorbs float
        // noise when the gate sits on the cell centre.
        if (!road.contains(cell) || v.water || v.channel != 0 || v.slope > kSlopeLimitDegrees ||
            std::abs(v.height - h) > kMaxGrade * length + 1e-6) {
            continue;
        }
        const std::vector<Cell>* approach = nullptr;
        for (const std::vector<Cell>& path : paths) {
            if (path.back() == cell) {
                approach = &path;
                break;
            }
        }
        c.status = "connected";
        c.reason = reason;
        c.local_path_m.clear();
        if (approach != nullptr) {
            for (Cell a : *approach) {
                c.local_path_m.push_back({-half + (a.first + 0.5) * kCell, -half + (a.second + 0.5) * kCell});
            }
        } else {
            c.local_path_m.push_back(point);
        }
        c.local_path_m.push_back(c.gate_local_m);
    }
}

std::vector<std::vector<Point>> metre_paths(const std::vector<std::vector<Cell>>& paths, double half) {
    std::vector<std::vector<Point>> out;
    out.reserve(paths.size());
    for (const std::vector<Cell>& path : paths) {
        std::vector<Point> metres;
        metres.reserve(path.size());
        for (Cell c : path) {
            metres.push_back({round_to(-half + (c.first + 0.5) * kCell, 2),
                              round_to(-half + (c.second + 0.5) * kCell, 2)});
        }
        out.push_back(std::move(metres));
    }
    return out;
}

std::uint64_t street_seed(std::uint32_t world_seed, const std::string& id, std::string_view suffix) {
    std::string domain = id;
    domain += ':';
    domain.append(suffix);
    return child_seed64(static_cast<std::uint64_t>(world_seed), domain);
}

}  // namespace

// ===========================================================================
// plan_city
// ===========================================================================

CityPlan plan_city(const World& world, const SphereGrid& grid, const CitySite& site,
                   const std::vector<CitySite>& all_sites,
                   const std::map<std::string_view, int>& nearby_counts) {
    const SettlementPreset& preset =
        settlement_preset(site.population_profile, city_block_for(site.city_class));
    const HousingProfile& house = housing_profile("worker_house");
    const HousingProfile& apartment = housing_profile("worker_apartment");

    const int n = world.config.size;
    const double radius = world.effective.globe_radius;

    // Bound the footprint by neighbouring anchors WITHOUT moving any of them: 0.28 of
    // the gap leaves two adjacent cities forty-four per cent of it between their crops.
    double neighbour_half = class_base_half(site.city_class);
    for (const CitySite& other : all_sites) {
        // Compared on the RESOLVED key, which always exists. The reference compares the
        // raw uid, so on a world where no site carries one every pair matches, the loop
        // skips everything and the crop bound silently switches itself off.
        if (&other == &site || other.key() == site.key()) continue;
        neighbour_half = std::min(neighbour_half,
                                  great_circle_m(radius, n, site.z, site.x, other.z, other.x) * 0.28);
    }
    const double half = program_half_m(preset, site.city_class, neighbour_half, house.plot_m,
                                       house.worker_beds);
    const auto size = static_cast<int>(2.0 * half / kCell);

    const LocalSampler sampler(world, grid, site.x, site.z, half);
    Ground ground = raster_ground(sampler, half, size, /*block_channels=*/true);
    const Sample centre = sampler.sample(0.0, 0.0);

    CityPlan plan;
    plan.city_uid = site.key();
    plan.site_id = site.id;
    plan.name = site.name;
    plan.civilization_id = site.population_profile;
    plan.city_class = site.city_class;
    plan.half_m = half;
    plan.reference_frame = {radius, 90.0 - 180.0 * site.z / (n - 1), -180.0 + 360.0 * site.x / (n - 1)};
    plan.location.buildable_area_m2 = static_cast<double>(ground.valid.count()) * kCell * kCell;
    plan.location.usable_land_fraction =
        static_cast<double>(ground.valid.count()) / (static_cast<double>(size) * size);
    plan.location.local_slope_degrees = ground.mean_slope;
    plan.location.aridity = 1.0 - clamp(centre.moisture, 0.0, 1.0);
    plan.location.wooded_fraction =
        static_cast<double>(ground.wooded) / std::max<double>(1.0, static_cast<double>(ground.valid.count()));
    plan.location.regional_threat = site.regional_threat;
    plan.location.defense_priority = site.regional_threat;

    sample_surface(ground, sampler, half, size);
    plan.terrain = ground.terrain;

    plan.shape = select_shape(plan.location.as_shape_site(), world.config.seed, plan.city_uid,
                              site.city_class, nearby_counts);
    if (site.founded_by_player) {
        plan.authored_by_player = true;
        plan.shape.shape_id = {};
        plan.shape.family = {};
        plan.shape.parameters.clear();
        plan.shape.reason =
            "Layout authored by the player; the generator chooses no shape for an authored city.";
    }

    static constexpr std::string_view kPhases[] = {"map",  "shape",       "fortification", "high",
                                                   "high_housing", "low", "low_housing"};
    for (std::string_view phase : kPhases) plan.passes.push_back({phase, 0});
    plan.warnings = {
        "Schematic packing over canonical detailed terrain; groundwater and structural "
        "feasibility are not resolved.",
        "Routed river centerlines reserve a provisional 24 m channel and 28 m setback from "
        "centerline, not a simulated flood extent.",
        "Four worker beds per house; 16 per apartment building when land is constrained. "
        "Dependents, commuters and households are not modeled.",
        "City walls are schematic enceintes separate from castle bailey generation."};
    plan.source_resolution_m = round_to(sampler.resolution(), 2);
    plan.debug.terrain_safe_cells = ground.valid.count();
    plan.debug.total_cells = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    plan.debug.apartment_policy = "houses_first_then_upgrade_on_plot_exhaustion";
    plan.stats.population_estimate = site.population_estimate;
    plan.stats.urban_population_estimate = site.urban_population_estimate;
    plan.road_connections = site.road_connections;
    if (sampler.resolution() > half) {
        plan.warnings.push_back(
            "Regional land categories repeat coarse samples; canonical local relief supplies "
            "finer elevation detail.");
    }

    if (plan.shape.shape_id.empty()) {
        const std::string reason = plan.authored_by_player ? "Layout authored by the player"
                                                           : "No compatible buildable footprint";
        for (const PresetRow& row : preset.buildings) {
            plan.unplaced.push_back({row.structure().id, row.count, reason});
        }
        return plan;
    }

    plan.passes[0].placed = static_cast<long long>(ground.valid.count());
    plan.passes[1].placed = 1;

    const ShapeClip clip{plan.shape.family, half, half * 0.94,
                         half * 0.94 / std::min(plan.shape.parameter("aspect_ratio", 1.0), 2.0)};
    const double spacing = std::max(72.0, plan.shape.parameter("block_spacing_m", 72.0));

    // The unclipped set: the street tree grows over it when the city has regional road
    // connections, so a street may run outside the shape and therefore outside the
    // ground plots are eligible for.
    CellSet terrain_valid = ground.valid;
    CellSet valid(size);
    for (Cell c : ground.valid.sorted()) {
        if (clip.inside(-half + (c.first + 0.5) * kCell, -half + (c.second + 0.5) * kCell)) valid.insert(c);
    }

    const std::uint64_t seed = street_seed(world.config.seed, plan.city_uid, "streets-v2");
    std::vector<Cell> required;
    for (RoadConnection& c : plan.road_connections) {
        c.cell = {std::clamp(static_cast<int>(std::floor((c.gate_local_m.x + half) / kCell)), 0, size - 1),
                  std::clamp(static_cast<int>(std::floor((c.gate_local_m.z + half) / kCell)), 0, size - 1)};
        c.has_cell = true;
        required.push_back(c.cell);
    }
    const StreetNetwork network =
        grow_streets(plan.road_connections.empty() ? valid : terrain_valid, ground.heights, size, seed,
                   spacing / kCell, required);
    connect_gates(plan.road_connections, network.cells, network.paths, sampler, ground.heights, half,
                  size, "Terrain-safe junction to regional road");
    plan.roads = network.cells.sorted();
    plan.street_paths_m = metre_paths(network.paths, half);

    CellSet free_ground = valid;
    for (Cell c : network.cells.sorted()) free_ground.erase(c);
    const std::vector<int> clearance = clearance_field(free_ground, size);

    const WallDefs defs = wall_and_gate_defs(preset);
    int gate_budget = 0;
    bool gate_row_seen = false;
    int tower_budget = 0;
    const PresetRow* tower_row = nullptr;
    for (const PresetRow& row : preset.buildings) {
        if (row.structure().id == "building.gatehouse" && !gate_row_seen) {
            gate_budget = row.count;
            gate_row_seen = true;
        }
        if (row.structure().id == "building.tower" && tower_row == nullptr) {
            tower_budget = row.count;
            tower_row = &row;
        }
    }
    // A wall always gets at least one opening, even when the preset lists no gatehouse.
    Fortifications fortifications = build_fortifications(
        valid, half, kCell, clip, ring_count_for(plan.shape.shape_id, plan.shape, site.city_class),
        plan.road_connections, defs, tower_budget, std::max(1, gate_budget));
    plan.passes[2].placed = static_cast<long long>(fortifications.segments.size());

    const std::vector<Anchor> anchors = street_anchors(plan.street_paths_m, seed);
    Packer packer(size, anchors, half, /*setback=*/10.0, valid, network.cells, clearance,
                  ground.heights, sampler, plan.plots, plan.passes);

    // Reserve the enceinte openings before street-front packing, so a gatehouse is not
    // crowded out of the gap the wall left for it.
    int placed_gates = 0;
    int placed_towers = 0;
    if (fortifications.defended_perimeter && defs.gate != nullptr) {
        const PlotSize gate_plot = defs.gate->structure().plot_m;
        for (WallGate& gate : fortifications.gates) {
            const double angle = std::atan2(gate.approach_m.z - gate.position_m.z,
                                            gate.approach_m.x - gate.position_m.x);
            Candidate option;
            option.x = gate.position_m.x;
            option.z = gate.position_m.z;
            option.degrees = round_to(degrees_of(angle), 4);
            option.anchor = gate.position_m;
            option.footprint = footprint_cells(gate.position_m.x, gate.position_m.z,
                                               gate_plot.width, gate_plot.depth, angle,
                                               half, kCell);
            option.corridor = option.footprint;
            option.ground_max = -std::numeric_limits<double>::infinity();
            option.ground_min = std::numeric_limits<double>::infinity();
            for (const Point& p : corners(gate.position_m.x, gate.position_m.z, gate_plot.width,
                                          gate_plot.depth, angle)) {
                const double h = sampler.height_at(p.x, p.z);
                option.ground_max = std::max(option.ground_max, h);
                option.ground_min = std::min(option.ground_min, h);
            }
            bool sampled = false;
            for (Cell c : option.footprint) {
                if (c.first < 0 || c.first >= size || c.second < 0 || c.second >= size) continue;
                const double h = ground.heights[static_cast<std::size_t>(c.second) *
                                                    static_cast<std::size_t>(size) +
                                                static_cast<std::size_t>(c.first)];
                option.ground_max = std::max(option.ground_max, h);
                option.ground_min = std::min(option.ground_min, h);
                sampled = true;
            }
            if (!sampled) {
                // The whole footprint hangs off the raster; one sample at the gate itself
                // is the only ground there is to report.
                const double h = sampler.height_at(gate.position_m.x, gate.position_m.z);
                option.ground_max = std::max(option.ground_max, h);
                option.ground_min = std::min(option.ground_min, h);
            }
            const Installable what = from_row(*defs.gate);
            if (packer.install_at(option, what, "fortification", "service")) {
                ++placed_gates;
                gate.plot_id = plan.plots.back().id;
            }
        }
    }
    if (fortifications.defended_perimeter && tower_row != nullptr) {
        const Structure& tower = tower_row->structure();
        for (WallTower& record : fortifications.towers) {
            if (placed_towers >= tower_budget) break;
            Candidate option;
            option.x = record.position_m.x;
            option.z = record.position_m.z;
            option.degrees = 0.0;
            option.anchor = record.position_m;
            option.footprint = footprint_cells(record.position_m.x, record.position_m.z,
                                               tower.plot_m.width, tower.plot_m.depth, 0.0, half, kCell);
            // Towers, unlike gates, ARE terrain-tested: a tower is a building, not an
            // opening the wall has already committed to.
            if (!valid.contains_all(option.footprint) || network.cells.intersects(option.footprint) ||
                packer.occupied().intersects(option.footprint)) {
                continue;
            }
            option.corridor = option.footprint;
            option.ground_max = -std::numeric_limits<double>::infinity();
            option.ground_min = std::numeric_limits<double>::infinity();
            for (const Point& p : corners(record.position_m.x, record.position_m.z, tower.plot_m.width,
                                          tower.plot_m.depth, 0.0)) {
                const double h = sampler.height_at(p.x, p.z);
                option.ground_max = std::max(option.ground_max, h);
                option.ground_min = std::min(option.ground_min, h);
            }
            for (Cell c : option.footprint) {
                const double h = ground.heights[static_cast<std::size_t>(c.second) *
                                                    static_cast<std::size_t>(size) +
                                                static_cast<std::size_t>(c.first)];
                option.ground_max = std::max(option.ground_max, h);
                option.ground_min = std::min(option.ground_min, h);
            }
            Installable what{tower.id, tower_row->display_name(), tower.plot_m, tower.dimensions_m,
                             tower.workers, 0};
            if (packer.install_at(option, what, "fortification", "service")) {
                ++placed_towers;
                record.plot_id = plan.plots.back().id;
            }
        }
    }

    // Every hard-coded false below is an honest "the planner has not measured this", not
    // a claim the condition is unmet. The five conditions absent entirely -- route
    // crossing, grade change, retention, farming and resource support -- read false for
    // the same reason.
    ConditionSet conditions;
    conditions.set(PlacementCondition::water_collection_or_delivery,
                   site.freshwater_distance_m.has_value());
    conditions.set(PlacementCondition::ore_and_fuel_supply, site.resource_potential >= 0.5);
    conditions.set(PlacementCondition::medicinal_supply, plan.location.wooded_fraction > 0.1);
    conditions.set(PlacementCondition::clear_sky_view, true);
    conditions.set(PlacementCondition::isolation_access, true);
    conditions.set(PlacementCondition::defended_perimeter, fortifications.defended_perimeter);

    struct Queued {
        const PresetRow* row;
        int count;
    };
    std::vector<Queued> high;
    std::vector<Queued> low;
    for (const PresetRow& row : preset.buildings) {
        int remaining = row.count;
        if (row.structure().id == "building.gatehouse") remaining = std::max(0, remaining - placed_gates);
        if (row.structure().id == "building.tower") remaining = std::max(0, remaining - placed_towers);
        if (remaining <= 0) continue;
        std::vector<PlacementCondition> missing;
        for (PlacementCondition condition : row.placement_conditions) {
            if (!conditions[condition]) missing.push_back(condition);
        }
        if (!missing.empty()) {
            plan.unplaced.push_back({row.structure().id, remaining, unverified_reason(missing)});
            continue;
        }
        (row.structure().priority == StructurePriority::core ? high : low).push_back({&row, remaining});
    }

    const auto workers_now = [&] {
        int total = 0;
        for (const Plot& p : plan.plots) total += p.workers;
        return total;
    };
    const auto beds_now = [&] {
        int total = 0;
        for (const Plot& p : plan.plots) total += p.beds;
        return total;
    };

    const auto house_workers = [&](std::string_view phase) {
        HousingAudit audit;
        audit.phase = phase;
        audit.workers = workers_now();
        audit.starting_beds = beds_now();
        audit.reserved_slots = static_cast<int>(packer.reserved_slots());
        int beds = audit.starting_beds;
        while (beds < audit.workers) {
            if (!packer.reserve().empty()) {
                if (!packer.install_first_reserved(house, phase)) {
                    // A reserved slot became unusable; drop it and keep seeking frontage.
                    packer.abandon_first_reserved(house);
                    continue;
                }
            } else if (!packer.install(from_house(house), phase, "housing")) {
                audit.plots_exhausted = true;
                break;
            }
            ++audit.houses_placed;
            beds += house.worker_beds;
        }
        // Replace existing dwellings IN PLACE. The apartment sits on the same plot, which
        // is exactly why the access and occupied land stay valid without re-testing.
        for (Plot& plot : plan.plots) {
            if (beds >= audit.workers) break;
            if (plot.building_id != house.id) continue;
            ++audit.upgrades;
            if (!plot.housing_upgrade.has_value()) {
                // The FIRST upgrade wins: the record says what the plot was built as.
                plot.housing_upgrade =
                    HousingUpgrade{phase, plot.building_id, plot.name, plot.dimensions_m, plot.beds};
            }
            beds += apartment.worker_beds - plot.beds;
            plot.building_id = apartment.id;
            plot.name = apartment.name;
            plot.dimensions_m = apartment.dimensions_m;
            plot.beds = apartment.worker_beds;
        }
        packer.release_reserved(house);
        audit.final_beds = beds;
        audit.shortfall = std::max(0, audit.workers - beds);
        plan.debug.housing_passes.push_back(audit);
    };

    const std::array<std::pair<std::string_view, std::vector<Queued>*>, 2> phases = {
        std::pair<std::string_view, std::vector<Queued>*>{"high", &high},
        std::pair<std::string_view, std::vector<Queued>*>{"low", &low}};
    for (const auto& [priority, rows] : phases) {
        int shortfall = 0;
        for (const Plot& p : plan.plots) shortfall += p.workers - p.beds;
        if (priority == "low" && shortfall > 0) {
            for (const Queued& queued : *rows) {
                plan.unplaced.push_back({queued.row->structure().id, queued.count,
                                         "High-priority worker housing must be completed first"});
            }
            break;
        }
        int demand = workers_now();
        for (const Queued& queued : *rows) demand += queued.row->structure().workers * queued.count;
        packer.reserve_housing(demand, house);
        plan.debug.housing_reservations.push_back(
            {priority, demand, static_cast<int>(packer.reserved_slots())});
        for (const Queued& queued : *rows) {
            int failed = 0;
            for (int k = 0; k < queued.count; ++k) {
                if (!packer.install(from_row(*queued.row), priority, "service")) ++failed;
            }
            if (failed != 0) {
                plan.unplaced.push_back({queued.row->structure().id, failed,
                                         "No terrain-safe plot with road access remaining"});
            }
        }
        house_workers(priority == "high" ? "high_housing" : "low_housing");
    }

    plan.debug.shape_safe_cells = valid.count();
    plan.debug.road_cells = network.cells.count();
    plan.debug.occupied_cells = packer.occupied().count();
    plan.debug.access_cells = packer.access().count();
    std::size_t vacant = 0;
    for (Cell c : valid.sorted()) {
        if (!network.cells.contains(c) && !packer.occupied().contains(c) && !packer.access().contains(c)) {
            ++vacant;
        }
    }
    plan.debug.vacant_shape_cells = vacant;
    plan.debug.housing_frontage_candidates = packer.free_frontage(house.plot_m);
    plan.debug.fortification_rings = fortifications.rings.size();
    plan.debug.program_half_m = half;

    const int workers = workers_now();
    const int beds = beds_now();
    int failed_core = 0;
    int unplaced_total = 0;
    for (const Unplaced& row : plan.unplaced) {
        unplaced_total += row.count;
        const Structure* s = structure(row.building_id);
        if (s != nullptr && s->priority == StructurePriority::core) failed_core += row.count;
    }
    plan.status = (failed_core == 0 && beds >= workers) ? "complete" : "partial";
    plan.stats.workers = workers;
    plan.stats.worker_beds = beds;
    plan.stats.housing_shortfall = std::max(0, workers - beds);
    for (const Plot& p : plan.plots) {
        if (p.kind == "service") ++plan.stats.service_buildings;
        if (p.building_id == house.id) ++plan.stats.houses;
        if (p.building_id == apartment.id) ++plan.stats.apartments;
        plan.stats.reserved_plot_area_m2 += p.plot_m.width * p.plot_m.depth;
    }
    plan.stats.unplaced_core = failed_core;
    plan.stats.unplaced_total = unplaced_total;
    plan.stats.wall_segments = static_cast<int>(fortifications.segments.size());
    plan.stats.defended_perimeter = fortifications.defended_perimeter;
    plan.fortifications = std::move(fortifications);
    return plan;
}

// ===========================================================================
// plan_hamlet
// ===========================================================================

namespace {

bool coastal_role(const std::string& role) {
    return role.find("harbor") != std::string::npos || role.find("fishing") != std::string::npos ||
           role == "landing";
}

ConditionSet hamlet_conditions(const std::string& role) {
    ConditionSet conditions;
    conditions.set(PlacementCondition::farming_support, role == "farming");
    conditions.set(PlacementCondition::resource_support, role == "resource");
    conditions.set(PlacementCondition::navigable_shore, coastal_role(role));
    conditions.set(PlacementCondition::water_collection_or_delivery, true);
    conditions.set(PlacementCondition::ore_and_fuel_supply, role == "resource");
    conditions.set(PlacementCondition::food_surplus, role == "farming");
    conditions.set(PlacementCondition::working_animals, role == "farming");
    conditions.set(PlacementCondition::clear_sky_view, true);
    conditions.set(PlacementCondition::isolation_access, true);
    return conditions;
}

}  // namespace

HamletPlan plan_hamlet(const World& world, const SphereGrid& grid, const HamletPin& pin,
                       const std::vector<CitySite>& sites, const std::vector<HamletPin>& all_pins) {
    const SettlementPreset& preset = settlement_preset(pin.population_profile, SettlementBlock::hamlet);
    const HousingProfile& house = housing_profile("hamlet_house");

    const int n = world.config.size;
    const double radius = world.effective.globe_radius;

    // A hamlet crops to half the metre distance to its nearest neighbour minus an 8 m
    // gap, rather than the city's 0.28 factor: that factor left clustered rural sites
    // too small for a cottage. These are ABSOLUTE metres, tuned for the reference world
    // and deliberately not scaled with the planet.
    double half = 100.0;
    const auto bound = [&](double other_x, double other_z) {
        const double distance = great_circle_m(radius, n, pin.z, pin.x, other_z, other_x);
        half = std::min(half, std::max(kCell, distance * 0.5 - 8.0));
    };
    for (const CitySite& site : sites) bound(site.x, site.z);
    for (const HamletPin& other : all_pins) {
        if (&other == &pin || other.id == pin.id) continue;
        bound(other.x, other.z);
    }
    half = std::max(kCell, std::floor(half / kCell) * kCell);
    const auto size = static_cast<int>(2.0 * half / kCell);

    const LocalSampler sampler(world, grid, pin.x, pin.z, half);
    // THE HAMLET MASK HAS NO CHANNEL TERM. Only standing water -- which already includes
    // ground within 12 m of a routed centreline -- and slope block a cell; a coarse
    // regional flood layer would otherwise empty every coast.
    Ground ground = raster_ground(sampler, half, size, /*block_channels=*/false);
    sample_surface(ground, sampler, half, size);

    HamletPlan plan;
    plan.hamlet_id = pin.id;
    plan.role = pin.role;
    plan.core_id = pin.core_id;
    if (pin.core_id >= 0 && static_cast<std::size_t>(pin.core_id) < sites.size()) {
        plan.core_city_uid = sites[static_cast<std::size_t>(pin.core_id)].key();
    }
    plan.x = pin.x;
    plan.z = pin.z;
    plan.node = pin.node;
    plan.civilization_id = pin.population_profile;
    plan.half_m = half;
    plan.terrain = ground.terrain;
    plan.reference_frame = {radius, 90.0 - 180.0 * pin.z / (n - 1), -180.0 + 360.0 * pin.x / (n - 1)};

    static constexpr std::string_view kPhases[] = {"map", "shape", "high", "high_housing", "low",
                                                  "low_housing"};
    for (std::string_view phase : kPhases) plan.passes.push_back({phase, 0});
    plan.warnings = {
        "Schematic rural packing; groundwater and structural feasibility are not resolved.",
        "Hamlet cottages use dedicated rural housing IDs, not city worker houses.",
        "Compact elliptical boundary is provisional; historical village morphology is future work.",
        "Regional flood_risk does not empty hamlet plots; standing water and slopes above 25 "
        "degrees still block cells."};
    plan.source_resolution_m = round_to(sampler.resolution(), 2);
    plan.debug.terrain_safe_cells = ground.valid.count();
    plan.debug.total_cells = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    plan.debug.apartment_policy = "hamlet_houses_only";
    plan.road_connections = support_road_entries(grid, sampler, n, pin.id, pin.access_nodes, half);
    if (sampler.resolution() > half) {
        plan.warnings.push_back(
            "Regional land categories repeat coarse samples; canonical local relief supplies "
            "finer elevation detail.");
    }

    const auto give_up = [&]() {
        for (const PresetRow& row : preset.buildings) {
            plan.unplaced.push_back({row.structure().id, row.count, "No compatible buildable footprint"});
        }
    };
    if (ground.valid.count() < 8) {
        give_up();
        return plan;
    }
    plan.passes[0].placed = static_cast<long long>(ground.valid.count());
    plan.passes[1].placed = 1;

    const double rx = half * 0.9;
    const double rz = half * 0.9 / 1.1;
    const std::uint64_t seed = street_seed(world.config.seed, pin.id, "hamlet-streets-v1");
    // A seeded irregular ellipse, which is what keeps hamlets independent of the city
    // shape catalogue. The wobble phases come from the stream rather than from the raw
    // 64-bit seed: the reference adds that integer to the sine argument, where at ~2e18
    // one ULP is 256 and every point in a 200 m window rounds to the same argument, so
    // its "wobble" is a constant and its ellipse is a plain axis-aligned one.
    Rng wobble_stream = Rng::child(seed, "hamlet-ellipse");
    const double wobble = 0.08 + 0.04 * wobble_stream.random();
    const double phase_x = wobble_stream.uniform(-kPi, kPi);
    const double phase_z = wobble_stream.uniform(-kPi, kPi);
    CellSet valid(size);
    for (Cell c : ground.valid.sorted()) {
        const double x = -half + (c.first + 0.5) * kCell;
        const double z = -half + (c.second + 0.5) * kCell;
        const double ex = x / (rx * (1.0 + wobble * std::sin(x * 0.07 + phase_x)));
        const double ez = z / (rz * (1.0 + wobble * std::cos(z * 0.09 + phase_z)));
        if (ex * ex + ez * ez < 1.0) valid.insert(c);
    }
    if (valid.count() < 8) {
        give_up();
        return plan;
    }

    std::vector<Cell> gates;
    for (RoadConnection& c : plan.road_connections) {
        c.cell = {std::clamp(static_cast<int>(std::floor((c.gate_local_m.x + half) / kCell)), 0, size - 1),
                  std::clamp(static_cast<int>(std::floor((c.gate_local_m.z + half) / kCell)), 0, size - 1)};
        c.has_cell = true;
        if (valid.contains(c.cell)) gates.push_back(c.cell);
    }
    // Rural streets stay INSIDE the ellipse, with a low branch floor. A city grows its
    // twelve districts over the unclipped terrain instead.
    const StreetNetwork network =
        grow_streets(valid, ground.heights, size, seed, 48.0 / kCell, gates, /*branch_floor=*/2);
    connect_gates(plan.road_connections, network.cells, network.paths, sampler, ground.heights, half,
                  size, "Terrain-safe junction to parent-city approach");
    plan.roads = network.cells.sorted();
    plan.street_paths_m = metre_paths(network.paths, half);

    CellSet free_ground = valid;
    for (Cell c : network.cells.sorted()) free_ground.erase(c);
    const std::vector<int> clearance = clearance_field(free_ground, size);

    const std::vector<Anchor> anchors = street_anchors(plan.street_paths_m, seed);
    // An 8 m setback, where a city uses 10: a rural lane is narrower than a street.
    Packer packer(size, anchors, half, /*setback=*/8.0, valid, network.cells, clearance, ground.heights,
                  sampler, plan.plots, plan.passes);

    const ConditionSet conditions = hamlet_conditions(pin.role);
    std::vector<const PresetRow*> high;
    std::vector<const PresetRow*> low;
    for (const PresetRow& row : preset.buildings) {
        std::vector<PlacementCondition> missing;
        for (PlacementCondition condition : row.placement_conditions) {
            if (!conditions[condition]) missing.push_back(condition);
        }
        if (!missing.empty()) {
            plan.unplaced.push_back({row.structure().id, row.count, unverified_reason(missing)});
            continue;
        }
        (row.structure().priority == StructurePriority::core ? high : low).push_back(&row);
    }

    const auto house_workers = [&](std::string_view phase) {
        HousingAudit audit;
        audit.phase = phase;
        for (const Plot& p : plan.plots) {
            audit.workers += p.workers;
            audit.starting_beds += p.beds;
        }
        int beds = audit.starting_beds;
        while (beds < audit.workers) {
            if (!packer.install(from_house(house), phase, "housing")) {
                audit.plots_exhausted = true;
                break;
            }
            ++audit.houses_placed;
            beds += house.worker_beds;
        }
        // `upgrades` stays zero: a hamlet never densifies. The key exists only so the
        // rural audit has the same shape as a city's.
        audit.final_beds = beds;
        audit.shortfall = std::max(0, audit.workers - beds);
        plan.debug.housing_passes.push_back(audit);
    };

    const std::array<std::pair<std::string_view, std::vector<const PresetRow*>*>, 2> phases = {
        std::pair<std::string_view, std::vector<const PresetRow*>*>{"high", &high},
        std::pair<std::string_view, std::vector<const PresetRow*>*>{"low", &low}};
    for (const auto& [priority, rows] : phases) {
        int shortfall = 0;
        for (const Plot& p : plan.plots) shortfall += p.workers - p.beds;
        if (priority == "low" && shortfall > 0) {
            for (const PresetRow* row : *rows) {
                plan.unplaced.push_back({row->structure().id, row->count,
                                         "High-priority worker housing must be completed first"});
            }
            break;
        }
        for (const PresetRow* row : *rows) {
            int failed = 0;
            for (int k = 0; k < row->count; ++k) {
                if (!packer.install(from_row(*row), priority, "service")) ++failed;
            }
            if (failed != 0) {
                plan.unplaced.push_back(
                    {row->structure().id, failed, "No terrain-safe plot with road access remaining"});
            }
        }
        house_workers(priority == "high" ? "high_housing" : "low_housing");
    }

    plan.debug.shape_safe_cells = valid.count();
    plan.debug.road_cells = network.cells.count();
    plan.debug.occupied_cells = packer.occupied().count();
    plan.debug.access_cells = packer.access().count();
    std::size_t vacant = 0;
    for (Cell c : valid.sorted()) {
        if (!network.cells.contains(c) && !packer.occupied().contains(c) && !packer.access().contains(c)) {
            ++vacant;
        }
    }
    plan.debug.vacant_shape_cells = vacant;
    // Counts EVERY geometrically valid option, occupied or not. The city computes a
    // free-frontage figure instead; the two numbers do not mean the same thing.
    plan.debug.housing_frontage_candidates = packer.total_frontage(house.plot_m);

    int workers = 0;
    int beds = 0;
    for (const Plot& p : plan.plots) {
        workers += p.workers;
        beds += p.beds;
        if (p.kind == "service") ++plan.stats.service_buildings;
        if (p.building_id == house.id) ++plan.stats.houses;
        plan.stats.reserved_plot_area_m2 += p.plot_m.width * p.plot_m.depth;
    }
    int failed_core = 0;
    int unplaced_total = 0;
    for (const Unplaced& row : plan.unplaced) {
        unplaced_total += row.count;
        const Structure* s = structure(row.building_id);
        if (s != nullptr && s->priority == StructurePriority::core) failed_core += row.count;
    }
    plan.status = (failed_core == 0 && beds >= workers) ? "complete" : "partial";
    plan.stats.workers = workers;
    plan.stats.worker_beds = beds;
    plan.stats.housing_shortfall = std::max(0, workers - beds);
    plan.stats.unplaced_core = failed_core;
    plan.stats.unplaced_total = unplaced_total;
    return plan;
}

// ===========================================================================
// Drivers
// ===========================================================================

CityPlans fill_cities(const World& world, const SphereGrid& grid, std::vector<CitySite> sites) {
    // Lexicographic on the STRING form of the key, not numeric: "city-10" comes before
    // "city-2". That is the order the repetition counter walks, so it decides which city
    // gets which shape. Stable, so two sites that somehow share a key keep the order the
    // caller listed them in rather than one the sort happened to leave them in.
    std::stable_sort(sites.begin(), sites.end(),
                     [](const CitySite& a, const CitySite& b) { return a.key() < b.key(); });

    CityPlans out;
    out.shapes_sha256 = city_shape_catalogue_identity();
    out.phase_order = {"map", "shape", "fortification", "high", "high_housing", "low", "low_housing"};
    out.scope = "Schematic local metres from final world raster; does not change simulated population.";

    // The only thing serialising the cities: each reads the tally and the next sees it
    // updated. An authored city is given no shape, so it does not count.
    std::map<std::string_view, int> counts;
    for (const CitySite& site : sites) {
        CityPlan plan = plan_city(world, grid, site, sites, counts);
        if (!plan.shape.shape_id.empty()) ++counts[plan.shape.shape_id];
        out.cities.push_back(std::move(plan));
    }
    return out;
}

HamletPlans fill_hamlets(const World& world, const SphereGrid& grid,
                         const std::vector<CitySite>& sites, std::vector<HamletPin> pins) {
    std::stable_sort(pins.begin(), pins.end(),
              [](const HamletPin& a, const HamletPin& b) { return a.id < b.id; });
    HamletPlans out;
    out.phase_order = {"map", "shape", "high", "high_housing", "low", "low_housing"};
    out.scope =
        "Schematic local metres for support hamlets; independent of city_plans; does not change "
        "simulated population.";
    for (const HamletPin& pin : pins) out.hamlets.push_back(plan_hamlet(world, grid, pin, sites, pins));
    return out;
}

}  // namespace fwg
