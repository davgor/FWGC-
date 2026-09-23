#include "fwg/key_locations.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <stdexcept>
#include <string>

#include "fwg/random.hpp"

namespace fwg {
namespace {

constexpr double kPi = std::numbers::pi;

// The golden angle as the CLUSTER pass spells it. Deliberately not
// kKeyLocationGoldenAngle: the reference writes 2.399963 here and the full-precision
// constant in the exterior scatter, and the two are different bearings. Unifying them
// would move every cluster member.
constexpr double kClusterGoldenAngle = 2.399963;
// pi/4, the retry step when a bearing lands in water.
constexpr double kClusterAttemptStep = 0.7854;
constexpr int kClusterAttempts = 8;

// 11 interior samples between the two endpoints.
constexpr int kLineOfSightSamples = 12;

// Floor modulo. Python's `%` is always non-negative and C++'s is not; every seam wrap in
// this file goes through here. It is the single most likely silent porting bug in the
// package -- a negative column silently reads another row or walks off the grid.
int floor_mod(int value, int modulus) {
    const int r = value % modulus;
    return r < 0 ? r + modulus : r;
}

// Half-away-from-zero at `places` decimals. The reference rounds half-to-even; this
// library picks one rule and states it rather than letting two printers disagree on a tie.
double round_to(double value, int places) {
    double scale = 1.0;
    for (int i = 0; i < places; ++i) scale *= 10.0;
    return std::round(value * scale) / scale;
}

// `%g`: six significant digits, matching the two gap diagnostics.
std::string format_g(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%g", value);
    return buffer;
}

double great_circle_m(const Vec3& a, const Vec3& b, double radius) {
    return radius * std::acos(clamp_unit(dot(a, b)));
}

Vec3 normalise(const Vec3& v) {
    const double length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return length != 0.0 ? Vec3{v.x / length, v.y / length, v.z / length} : Vec3{0.0, 1.0, 0.0};
}

// ---------------------------------------------------------------------------
// Layers.
// ---------------------------------------------------------------------------

// The world's layers with the seven derived ones laid over them. A derived name WINS a
// clash, which is how `settlement_distance` means this pass's sweep even if the world
// later publishes one of its own.
class LayerSet {
public:
    explicit LayerSet(const std::map<std::string, Grid>* base) : base_(base) {}

    void set(std::string name, Grid grid) { derived_.emplace(std::move(name), std::move(grid)); }

    const Grid* find(std::string_view name) const {
        const auto derived = derived_.find(std::string(name));
        if (derived != derived_.end()) return &derived->second;
        if (base_ == nullptr) return nullptr;
        const auto base = base_->find(std::string(name));
        return base != base_->end() ? &base->second : nullptr;
    }

    // Throws rather than returning a zero: the four distance fields are derived by this
    // pass and an absent one means the derivation did not run, not that the world is
    // featureless.
    const Grid& require(std::string_view name) const {
        const Grid* grid = find(name);
        if (grid == nullptr) throw std::runtime_error("key locations need the " + std::string(name) + " layer");
        return *grid;
    }

private:
    const std::map<std::string, Grid>* base_;
    std::map<std::string, Grid> derived_;
};

// ---------------------------------------------------------------------------
// Geometry. The node NUMBERING is sphere_grid's and is taken from it rather than
// restated: two copies of a formula that must agree is how the two stop agreeing.
// ---------------------------------------------------------------------------

struct Cell {
    int x = 0;
    int z = 0;
    int node = 0;
    Vec3 direction{};
    double area_m2 = 0.0;
    double height_m = 0.0;
};

// The eight-neighbour stencil, in the order the sweep relaxes them. Diagonals cost
// sqrt(2); the order is part of the FIFO sweep's arrival order and therefore of the
// distances it settles on.
struct Step {
    int dx, dz;
    double weight;
};
const Step kSteps[8] = {{-1, -1, std::numbers::sqrt2}, {0, -1, 1.0},  {1, -1, std::numbers::sqrt2},
                        {-1, 0, 1.0},                  {1, 0, 1.0},   {-1, 1, std::numbers::sqrt2},
                        {0, 1, 1.0},                   {1, 1, std::numbers::sqrt2}};

// Column n-1 mirrors column 0; a pole is one node, so its whole row is that one value.
void seal_seam(Grid& grid, int n) {
    for (int z = 0; z < n; ++z) grid.at(n - 1, z) = grid.at(0, z);
    const double north = grid.at(0, 0);
    const double south = grid.at(0, n - 1);
    for (int x = 0; x < n; ++x) {
        grid.at(x, 0) = north;
        grid.at(x, n - 1) = south;
    }
}

}  // namespace

std::pair<int, int> key_location_cell_of_direction(const Vec3& point, int n) {
    const double latitude = std::asin(clamp_unit(point.y));
    const double longitude = std::atan2(point.z, point.x);
    int z = static_cast<int>(std::llround((kPi / 2.0 - latitude) * (n - 1) / kPi));
    int x = static_cast<int>(std::llround((longitude + kPi) * (n - 1) / (2.0 * kPi)));
    x = std::max(0, std::min(n - 1, x));
    z = std::max(0, std::min(n - 1, z));
    return {floor_mod(x, n - 1), z};
}

Vec3 key_location_offset(const Vec3& origin, double bearing, double distance_m, double radius) {
    const Vec3 up{0.0, 1.0, 0.0};
    const Vec3 east = normalise(cross(up, origin));
    const Vec3 north = normalise(cross(origin, east));
    const double angle = distance_m / radius;
    const double across = std::sin(bearing);
    const double along = std::cos(bearing);
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    return normalise({origin.x * c + (east.x * across + north.x * along) * s,
                      origin.y * c + (east.y * across + north.y * along) * s,
                      origin.z * c + (east.z * across + north.z * along) * s});
}

Vec3 key_location_slerp(const Vec3& a, const Vec3& b, double t) {
    const double angle = std::acos(clamp_unit(dot(a, b)));
    if (angle < 1e-9) return a;
    const double sine = std::sin(angle);
    const double first = std::sin((1.0 - t) * angle) / sine;
    const double second = std::sin(t * angle) / sine;
    return normalise({first * a.x + second * b.x, first * a.y + second * b.y,
                      first * a.z + second * b.z});
}

double key_location_percentile(std::vector<double> values, double q) {
    std::sort(values.begin(), values.end());
    const long long index = std::llround(q * static_cast<double>(values.size() - 1));
    const long long clamped =
        std::max<long long>(0, std::min<long long>(static_cast<long long>(values.size()) - 1, index));
    return values[static_cast<std::size_t>(clamped)];
}

Grid key_location_distance_field(const std::vector<std::pair<int, int>>& sources, int n,
                                 double spacing_m, std::optional<double> ceiling_m) {
    const double limit = ceiling_m ? *ceiling_m : spacing_m * n;
    const double unreached = std::numeric_limits<double>::infinity();
    Grid grid(n, unreached);
    std::deque<std::pair<int, int>> queue;
    for (const auto& [x, z] : sources) {
        if (z < 0 || z >= n) continue;
        const int column = floor_mod(x, n - 1);
        // Not already a source: a repeated source cell must not be queued twice.
        if (grid.at(column, z) != 0.0) {
            grid.at(column, z) = 0.0;
            queue.emplace_back(column, z);
        }
    }
    while (!queue.empty()) {
        const auto [x, z] = queue.front();
        queue.pop_front();
        const double base = grid.at(x, z);
        for (const Step& step : kSteps) {
            const int nz = z + step.dz;
            if (nz < 0 || nz >= n) continue;
            const int nx = floor_mod(x + step.dx, n - 1);
            const double stepped = base + step.weight;
            if (stepped < grid.at(nx, nz)) {
                grid.at(nx, nz) = stepped;
                queue.emplace_back(nx, nz);
            }
        }
    }
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            const double steps = grid.at(x, z);
            grid.at(x, z) = std::isinf(steps) ? limit : std::min(limit, steps * spacing_m);
        }
    }
    seal_seam(grid, n);
    return grid;
}

Grid key_location_frontier_field(const Grid* culture_region, int n) {
    Grid grid(n, 0.0);
    if (culture_region == nullptr || culture_region->empty()) return grid;
    for (int z = 1; z < n - 1; ++z) {
        for (int x = 0; x < n - 1; ++x) {
            const double mine = culture_region->at(x, z);
            if (mine < 0.0) continue;
            for (const Step& step : kSteps) {
                const int nz = z + step.dz;
                if (nz < 0 || nz >= n) continue;
                const int nx = floor_mod(x + step.dx, n - 1);
                const double other = culture_region->at(nx, nz);
                if (other >= 0.0 && other != mine) {
                    grid.at(x, z) = 1.0;
                    break;
                }
            }
        }
    }
    seal_seam(grid, n);
    return grid;
}

Grid key_location_coastal_water_field(const Grid* source, const Grid* water_type, int n) {
    Grid grid(n, 0.0);
    if (source == nullptr || source->empty() || water_type == nullptr || water_type->empty()) return grid;
    for (int z = 1; z < n - 1; ++z) {
        for (int x = 0; x < n - 1; ++x) {
            if (water_type->at(x, z) != 0.0) continue;
            double best = 0.0;
            for (const Step& step : kSteps) {
                const int nz = z + step.dz;
                if (nz < 0 || nz >= n) continue;
                const int nx = floor_mod(x + step.dx, n - 1);
                if (water_type->at(nx, nz) != 0.0) best = std::max(best, source->at(nx, nz));
            }
            grid.at(x, z) = best;
        }
    }
    seal_seam(grid, n);
    return grid;
}

// ---------------------------------------------------------------------------
// Vocabularies.
// ---------------------------------------------------------------------------

bool key_location_untended(std::string_view state) {
    return state == "natural" || state == "abandoned" || state == "ruined" || state == "sealed" ||
           state == "buried" || state == "drowned" || state == "corrupted" || state == "reclaimed";
}

bool key_location_hostile(std::string_view occupant) {
    return occupant == "monsters" || occupant == "undead" || occupant == "cult" || occupant == "beasts";
}

const KeyLocationArchetype* key_location_archetype(std::string_view id) {
    const auto rows = key_location_archetypes();
    const auto found = std::lower_bound(rows.begin(), rows.end(), id,
                                        [](const KeyLocationArchetype& row, std::string_view key) {
                                            return row.id < key;
                                        });
    return (found != rows.end() && found->id == id) ? &*found : nullptr;
}

const KeyLocationFamily* key_location_family(std::string_view id) {
    for (const KeyLocationFamily& row : key_location_families()) {
        if (row.id == id) return &row;
    }
    return nullptr;
}

const KeyLocationKit* key_location_kit_for(std::string_view archetype_id, std::string_view family) {
    for (const KeyLocationKit& kit : key_location_archetype_kits()) {
        if (kit.key == archetype_id) return &kit;
    }
    for (const KeyLocationKit& kit : key_location_family_kits()) {
        if (kit.key == family) return &kit;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Shaping. Emitted twice over on purpose: as a parametric primitive, which is exact and
// is what an engine with its own landscape should apply, and as a sampled grid for a
// consumer that only reads grids. Where they disagree the primitive is right.
// ---------------------------------------------------------------------------

namespace {

double smoothstep(double t) {
    t = std::max(0.0, std::min(1.0, t));
    return t * t * (3.0 - 2.0 * t);
}

// 1 at the centre, 0 at the radius. Zero radius is a no-op rather than a divide.
double radial(double distance, double radius) {
    if (radius <= 0.0) return 0.0;
    return smoothstep(1.0 - distance / radius);
}

double shaping_radius(const KeyLocationShaping& s) { return s.has_radius ? s.radius_m : 1.0; }

// Distance from the axis of a linear feature as a fraction of its half width, or nothing
// when the point is past either end of the run. A zero width returns 1 -- the edge, zero
// profile -- rather than dividing.
std::optional<double> axis_offset(double x, double z, double cx, double cz, double degrees,
                                  double length, double width) {
    const double rad = degrees * kPi / 180.0;
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    const double dx = x - cx;
    const double dz = z - cz;
    const double along = dx * c + dz * s;
    const double across = -dx * s + dz * c;
    if (std::abs(along) > length / 2.0) return std::nullopt;
    return width > 0.0 ? std::abs(across) / (width / 2.0) : 1.0;
}

}  // namespace

double key_location_shaping_delta(const KeyLocationShapingPlacement& entry, double x, double z) {
    const KeyLocationShaping& s = entry.spec;
    const double distance = std::hypot(x - entry.x_m, z - entry.z_m);
    const double radius = shaping_radius(s);
    switch (s.shape) {
        case KeyLocationShape::Mound:
            return s.height_m * radial(distance, radius);
        case KeyLocationShape::Cut:
            return -s.depth_m * radial(distance, radius);
        case KeyLocationShape::Platform: {
            if (distance >= radius) return 0.0;
            const double inner = radius * (1.0 - kKeyLocationPlatformEdge);
            const double held =
                distance <= inner ? 1.0 : smoothstep((radius - distance) / (radius - inner));
            return s.height_m * held;
        }
        case KeyLocationShape::Rim: {
            // A raised ring with a bowl inside it: the crater profile, and the reason a rim
            // is not a mound with a hole punched in it.
            if (distance > radius * 1.4) return 0.0;
            const double ring =
                std::exp(-std::pow((distance - radius) / std::max(1e-6, radius * 0.28), 2.0));
            const double inside = s.depth_m != 0.0 ? radial(distance, radius * 0.82) : 0.0;
            return s.height_m * ring - s.depth_m * inside;
        }
        case KeyLocationShape::Bank:
        case KeyLocationShape::Trench: {
            const double length = s.has_length ? s.length_m : radius * 2.0;
            const double width = s.has_width ? s.width_m : radius;
            const auto across =
                axis_offset(x, z, entry.x_m, entry.z_m, entry.rotation_degrees, length, width);
            if (!across || *across > 1.0) return 0.0;
            const double profile = smoothstep(1.0 - *across);
            return profile * (s.shape == KeyLocationShape::Bank ? s.height_m : -s.depth_m);
        }
        case KeyLocationShape::Level:
            return 0.0;  // handled against the base surface, not as an additive delta
    }
    throw std::runtime_error("unknown key location shape");
}

double key_location_shaped_height(std::span<const KeyLocationShapingPlacement> entries, double x,
                                  double z, double base, double centre_base) {
    // Flattening pulls ground toward the height at the feature's centre. A courtyard, a
    // hearth or a foundation line sits on ground somebody made flat, and that cannot be
    // expressed as a delta independent of the terrain.
    double height = base;
    for (const KeyLocationShapingPlacement& entry : entries) {
        if (entry.spec.shape != KeyLocationShape::Level) continue;
        const double strength =
            radial(std::hypot(x - entry.x_m, z - entry.z_m), shaping_radius(entry.spec));
        const double target = centre_base + entry.spec.height_m;
        height += (target - height) * strength;
    }
    for (const KeyLocationShapingPlacement& entry : entries) {
        if (entry.spec.shape != KeyLocationShape::Level) {
            height += key_location_shaping_delta(entry, x, z);
        }
    }
    return height;
}

namespace {

// ---------------------------------------------------------------------------
// Placement.
// ---------------------------------------------------------------------------

// A `requires` term with its percentiles already resolved against this world's own
// distribution, and its grid resolved once. The grid pointer is what stops `satisfies`
// doing a map lookup per cell per term.
struct ResolvedTerm {
    const Grid* grid = nullptr;
    std::string_view layer;
    bool has_min = false; double min_value = 0.0;
    bool has_max = false; double max_value = 0.0;
    bool has_equals = false; double equals_value = 0.0;
    bool has_not_equals = false; double not_equals_value = 0.0;
    bool has_in = false;
    std::span<const double> in_values;
};

ResolvedTerm bare(const KeyLocationRequire& term, const Grid* grid) {
    ResolvedTerm out;
    out.grid = grid;
    out.layer = term.layer;
    out.has_min = term.has_min;
    out.min_value = term.min_value;
    out.has_max = term.has_max;
    out.max_value = term.max_value;
    out.has_equals = term.has_equals;
    out.equals_value = term.equals_value;
    out.has_not_equals = term.has_not_equals;
    out.not_equals_value = term.not_equals_value;
    out.has_in = term.has_in;
    out.in_values = term.in_values;
    return out;
}

// An ABSENT LAYER NEVER SATISFIES ANYTHING. That is what makes an absolute gate on a
// layer the world never generated fail closed and loudly.
bool satisfies(const ResolvedTerm& term, int x, int z) {
    if (term.grid == nullptr) return false;
    const double raw = term.grid->at(x, z);
    if (term.has_min && raw < term.min_value) return false;
    if (term.has_max && raw > term.max_value) return false;
    if (term.has_equals && raw != term.equals_value) return false;
    if (term.has_not_equals && raw == term.not_equals_value) return false;
    if (term.has_in && std::find(term.in_values.begin(), term.in_values.end(), raw) ==
                           term.in_values.end()) {
        return false;
    }
    return true;
}

// The absolute rule a percentile term resolves to on this domain, and the cut it used.
// ONE statement of the arithmetic, read by both the gate and the diagnostic: the two used
// to describe the same resolution in their own words, and that is how a diagnostic starts
// naming a different question from the one the gate asked.
std::pair<ResolvedTerm, double> percentile_rule(const KeyLocationRequire& term, const Grid* grid,
                                                const std::vector<double>& values) {
    ResolvedTerm rule = bare(term, grid);
    double cut = 0.0;
    if (term.has_above_percentile) {
        cut = key_location_percentile(values, term.above_percentile);
        // Whichever of the floor and the cut is STRICTER wins.
        rule.min_value = term.has_min ? std::max(cut, term.min_value) : cut;
        rule.has_min = true;
    }
    if (term.has_below_percentile) {
        cut = key_location_percentile(values, term.below_percentile);
        rule.max_value = term.has_max ? std::min(cut, term.max_value) : cut;
        rule.has_max = true;
    }
    return {rule, cut};
}

bool admits_everything(const ResolvedTerm& rule, const std::vector<Cell>& cells) {
    // Calls the same `satisfies` the gate will use. A near-miss reimplementation here
    // would answer a slightly different question and would refuse a term that does
    // discriminate or keep one that does not.
    for (const Cell& c : cells) {
        if (!satisfies(rule, c.x, c.z)) return false;
    }
    return true;
}

std::vector<double> term_values(const Grid& grid, const std::vector<Cell>& cells) {
    std::vector<double> values;
    values.reserve(cells.size());
    for (const Cell& c : cells) values.push_back(grid.at(c.x, c.z));
    return values;
}

// Turn percentile requirements into absolute ones against this world's own distribution,
// or refuse the archetype outright.
//
// An absolute floor on an empty field admits no cell, so the archetype disappears and the
// diagnostic says so -- it fails CLOSED, loudly. A percentile on an empty field computes a
// cut equal to the one value present, every cell clears it, and the gate silently admits
// the entire domain -- it fails OPEN and reports "placed". Measured: holy_well asks for
// the nearest 35 per cent of freshwater_distance, that layer reads a flat 0.0 on all 47
// land cells of the size-17 reference world, and the archetype was eligible on all 47 with
// its defining requirement voided.
//
// Zero variance is only the degenerate case. A field that varies can still resolve to a
// cut every cell clears -- saturation, where `river` read a flat 1.0 on land, and sparsity,
// where a nearest-rank cut over a field that is zero on 92 per cent of the domain lands ON
// zero. So the test is not "does the field vary" but "does the resolved term separate the
// domain".
std::optional<std::vector<ResolvedTerm>> resolve_terms(const KeyLocationArchetype& archetype,
                                                       const LayerSet& layers,
                                                       const std::vector<Cell>& cells) {
    std::vector<ResolvedTerm> resolved;
    for (const KeyLocationRequire& term : archetype.requirements) {
        const Grid* grid = layers.find(term.layer);
        if (grid == nullptr) return std::nullopt;
        if (!term.has_percentile) {
            resolved.push_back(bare(term, grid));
            continue;
        }
        const std::vector<double> values = term_values(*grid, cells);
        if (values.empty()) return std::nullopt;
        const auto [low, high] = std::minmax_element(values.begin(), values.end());
        if (*low == *high) return std::nullopt;
        auto [rule, cut] = percentile_rule(term, grid, values);
        (void)cut;
        if (admits_everything(rule, cells)) return std::nullopt;
        resolved.push_back(rule);
    }
    return resolved;
}

// Why this world cannot serve the archetype at all, or empty when it can.
//
// Four different impossibilities point at four different people. A missing layer is a
// world generated to too early a phase. A field identically constant inside the
// archetype's own domain is a CATALOGUE mistake -- no raster, seed or threshold will
// change it. A cut that admits the whole domain is a distribution too piled up at one end
// for a nearest-rank cut to divide. Everything else is an ordinary world that happens not
// to have the ground this time.
std::string gap_reason(const KeyLocationArchetype& archetype, const LayerSet& layers,
                       const std::vector<Cell>& cells, std::string_view domain_name) {
    for (const KeyLocationRequire& term : archetype.requirements) {
        const Grid* grid = layers.find(term.layer);
        if (grid == nullptr) {
            return "the world never generated the " + std::string(term.layer) + " layer";
        }
        const std::vector<double> values = term_values(*grid, cells);
        if (values.empty()) {
            return "this world has no " + std::string(domain_name) + " cells for it to stand on";
        }
        if (!term.has_percentile) continue;
        const auto [low, high] = std::minmax_element(values.begin(), values.end());
        if (*low == *high) {
            return std::string(term.layer) + " is identically " + format_g(*low) + " across the " +
                   std::string(domain_name) +
                   " domain, so a percentile over it selects nothing - the archetype names a "
                   "field that carries no data where it is allowed to stand";
        }
        auto [rule, cut] = percentile_rule(term, grid, values);
        if (admits_everything(rule, cells)) {
            const double share =
                term.has_above_percentile ? term.above_percentile : term.below_percentile;
            return std::string(term.layer) + " resolves to a cut of " + format_g(cut) +
                   " at the " + format_g(share) + " percentile, which admits all " +
                   std::to_string(values.size()) + " " + std::string(domain_name) +
                   " cells - the gate separates nothing, so the archetype would stand on "
                   "ground its own requirement never tested";
        }
    }
    return {};
}

std::vector<const Cell*> eligible_cells(const std::vector<ResolvedTerm>& resolved,
                                        const std::vector<Cell>& cells) {
    std::vector<const Cell*> out;
    for (const Cell& c : cells) {
        bool all = true;
        for (const ResolvedTerm& term : resolved) {
            if (!satisfies(term, c.x, c.z)) {
                all = false;
                break;
            }
        }
        if (all) out.push_back(&c);
    }
    return out;
}

using Normalisers = std::map<std::string, std::pair<double, double>>;

// Per-layer (low, high) over LAND, so metres and unit fractions score comparably. A layer
// flat across the whole world gets no entry and is scored as neutral rather than dividing
// by zero -- a constant field carries no information about where to put anything.
//
// Built over land cells and then used for water-domain archetypes too. That is the
// reference's behaviour and it looks like a defect rather than a decision: `water_depth`
// on an ocean cell is normalised against the land-cell range and clamped, so the term is
// saturated or neutral for every water candidate. Reproduced deliberately -- changing it
// would silently re-site every drowned temple, wreck field, crannog and maelstrom.
Normalisers build_normalisers(const LayerSet& layers, const std::set<std::string>& names,
                              const std::vector<Cell>& land) {
    Normalisers result;
    for (const std::string& name : names) {
        const Grid* grid = layers.find(name);
        if (grid == nullptr) continue;
        if (land.empty()) continue;
        double low = grid->at(land.front().x, land.front().z);
        double high = low;
        for (const Cell& c : land) {
            const double v = grid->at(c.x, c.z);
            low = std::min(low, v);
            high = std::max(high, v);
        }
        if (high > low) result.emplace(name, std::make_pair(low, high));
    }
    return result;
}

double normalised(const LayerSet& layers, const Normalisers& norms, std::string_view name, int x,
                  int z, bool invert) {
    const auto span = norms.find(std::string(name));
    if (span == norms.end()) return kKeyLocationDefaultScore;
    const Grid* grid = layers.find(name);
    if (grid == nullptr) return kKeyLocationDefaultScore;
    const double raw = grid->at(x, z);
    double unit = (raw - span->second.first) / (span->second.second - span->second.first);
    unit = std::max(0.0, std::min(1.0, unit));
    return invert ? 1.0 - unit : unit;
}

struct Score {
    double value = kKeyLocationDefaultScore;
    std::vector<KeyLocationFactor> factors;
};

// Weighted mean of the archetype's preference terms. The factor list is built in `prefers`
// order with LAST-WINS on a repeated layer key, which is what a dict assignment does, so a
// repeated key keeps its first position and takes the last value.
Score score_cell(const KeyLocationArchetype& archetype, const LayerSet& layers,
                 const Normalisers& norms, int x, int z) {
    Score out;
    if (archetype.prefers.empty()) return out;
    double total = 0.0;
    double weighted = 0.0;
    for (const KeyLocationPrefer& term : archetype.prefers) {
        const double unit = normalised(layers, norms, term.layer, x, z, term.invert);
        const double weight = term.weight;
        std::string key = std::string(term.layer) + (term.invert ? "_low" : "");
        const auto found = std::find_if(out.factors.begin(), out.factors.end(),
                                        [&key](const KeyLocationFactor& f) { return f.layer == key; });
        if (found != out.factors.end()) {
            found->value = round_to(unit, 4);
        } else {
            out.factors.push_back({std::move(key), round_to(unit, 4)});
        }
        weighted += unit * weight;
        total += weight;
    }
    out.value = total != 0.0 ? weighted / total : kKeyLocationDefaultScore;
    return out;
}

}  // namespace

// How many of this archetype the world's land supports, as a THINNED draw.
//
// Rounding the expectation to the nearest integer looks harmless and is not: on a small
// world every archetype's expectation falls below a half and the whole catalogue rounds to
// zero at once, so the world reports "too small to support one" ninety times and grows
// nothing. ONE random() is always consumed, whatever the outcome, and `min_count` is
// applied BEFORE `max_count`.
int key_location_budget(const KeyLocationArchetype& archetype, double land_km2, Rng& draw) {
    const double expected = archetype.per_1000_km2 * land_km2 / 1000.0 * archetype.occurrence;
    const int whole = static_cast<int>(expected);
    int count = whole + (draw.random() < expected - whole ? 1 : 0);
    if (archetype.has_min_count) count = std::max(count, archetype.min_count);
    if (archetype.has_max_count) count = std::min(count, archetype.max_count);
    return std::max(0, count);
}

namespace {

// Every `other` must be at least `minimum` away. An EMPTY list passes vacuously, which is
// what makes the first site of an archetype unconstrained.
bool clear_of(const Vec3& point, const std::vector<Vec3>& taken, double radius, double minimum) {
    for (const Vec3& other : taken) {
        if (great_circle_m(point, other, radius) < minimum) return false;
    }
    return true;
}

struct Chosen {
    const Cell* cell = nullptr;
    double suitability = 0.0;
    std::vector<KeyLocationFactor> factors;
};

// Thin the eligible cells down to `wanted` placements that respect every spacing rule.
//
// Greedy over JITTERED score. The jitter matters: pure greed clusters every site on the
// single best ridge and the map reads as a gradient rather than a world, while pure random
// ignores the suitability field the archetype just declared.
std::vector<Chosen> select_cells(const KeyLocationArchetype& archetype, double spacing_m,
                                 double clearance_m, double family_spacing_m,
                                 const LayerSet& layers, const std::vector<const Cell*>& cells,
                                 const Normalisers& norms, double radius, Rng& draw, int wanted,
                                 const std::vector<Vec3>& settled, const std::set<int>& claimed,
                                 const std::vector<Vec3>& family_taken) {
    if (wanted <= 0) return {};
    struct Ranked {
        double negative_jitter;
        int node;
        const Cell* cell;
        double value;
        std::vector<KeyLocationFactor> factors;
    };
    std::vector<Ranked> ranked;
    for (const Cell* candidate : cells) {
        // The claimed skip comes BEFORE the jitter draw, so an already-taken node costs no
        // stream. Moving it after would re-roll every later candidate.
        if (claimed.count(candidate->node) != 0) continue;
        Score scored = score_cell(archetype, layers, norms, candidate->x, candidate->z);
        const double jittered =
            scored.value * (1.0 - kKeyLocationJitter + 2.0 * kKeyLocationJitter * draw.random());
        ranked.push_back({-jittered, candidate->node, candidate, scored.value, std::move(scored.factors)});
    }
    // On (-jittered, node) only. `node` is the deterministic tie-break and is why replay is
    // exact; sorting on anything set- or map-ordered would not be.
    std::sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
        if (a.negative_jitter != b.negative_jitter) return a.negative_jitter < b.negative_jitter;
        return a.node < b.node;
    });

    std::vector<Vec3> taken = family_taken;  // a LOCAL copy; the caller appends the winners
    std::vector<Vec3> own;
    std::vector<Chosen> placed;
    for (Ranked& row : ranked) {
        if (static_cast<int>(placed.size()) >= wanted) break;
        const Vec3& point = row.cell->direction;
        if (clearance_m != 0.0 && !clear_of(point, settled, radius, clearance_m)) continue;
        if (spacing_m != 0.0 && !clear_of(point, own, radius, spacing_m)) continue;
        if (family_spacing_m != 0.0 && !clear_of(point, taken, radius, family_spacing_m)) continue;
        own.push_back(point);
        taken.push_back(point);
        placed.push_back({row.cell, round_to(row.value, 4), std::move(row.factors)});
    }
    return placed;
}

// ---------------------------------------------------------------------------
// Succession: what happened to a place after it was built, and who is in it now.
//
// The catalogue says what an archetype IS; this decides what the world did to it, which is
// where "forgotten fortress" and "dungeon" come from -- neither is an archetype, both are
// outcomes. A fortress raised by a culture that no longer exists, left long enough, with a
// beast lair nearby, is a ruined fortress full of monsters, and nothing had to author that.
// ---------------------------------------------------------------------------

using WeightMap = std::vector<std::pair<std::string_view, double>>;

WeightMap copy_weights(std::span<const KeyLocationWeight> weights) {
    WeightMap out;
    out.reserve(weights.size());
    for (const KeyLocationWeight& w : weights) out.emplace_back(w.name, w.weight);
    return out;
}

// A bend applies only if the name is already a key: the catalogue supplies the vocabulary
// and this only reweights it.
void bend(WeightMap& weights, std::string_view name, double factor) {
    for (auto& [key, value] : weights) {
        if (key == name) value *= factor;
    }
}

// Weighted pick over SORTED keys, so the draw order never depends on insertion order. The
// sort is done here rather than baked into the generated table precisely so that removing
// it is a change a test can see.
std::string_view choose(WeightMap weights, Rng& draw) {
    std::sort(weights.begin(), weights.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    double total = 0.0;
    for (const auto& [name, weight] : weights) total += weight;
    if (total <= 0.0) return weights.front().first;  // no draw spent
    const double cut = draw.random() * total;
    double running = 0.0;
    for (const auto& [name, weight] : weights) {
        running += weight;
        if (cut < running) return name;
    }
    return weights.back().first;
}

bool water_domain(KeyLocationDomain domain) {
    return domain == KeyLocationDomain::Water || domain == KeyLocationDomain::Ocean ||
           domain == KeyLocationDomain::Lake;
}

}  // namespace

std::string_view key_location_derive_state(const KeyLocationArchetype& archetype,
                                           const KeyLocationContext& context, Rng& draw) {
    WeightMap weights = copy_weights(archetype.states);
    if (water_domain(archetype.domain)) {
        // A genuine branch in the STREAM LENGTH: the early return spends no draw at all.
        for (const auto& [name, weight] : weights) {
            if (name == "drowned") return name;
        }
        return choose(std::move(weights), draw);
    }
    if (context.near_ruin) {
        bend(weights, "ruined", 2.2);
        bend(weights, "abandoned", 1.6);
        bend(weights, "active", 0.35);
    }
    if (context.remote) {
        bend(weights, "active", 0.5);
        bend(weights, "abandoned", 1.4);
    }
    if (context.unstable) bend(weights, "corrupted", 2.5);
    if (context.ages >= 2) {
        bend(weights, "active", 0.7);
        bend(weights, "ruined", 1.3);
    }
    return choose(std::move(weights), draw);
}

std::string_view key_location_derive_occupant(const KeyLocationArchetype& archetype,
                                              std::string_view state,
                                              const KeyLocationContext& context, Rng& draw) {
    WeightMap weights = copy_weights(archetype.occupants);
    if (state == "active") {
        bend(weights, "builders", 2.0);
        bend(weights, "descendants", 1.6);
        for (std::string_view hostile : {"monsters", "undead", "cult", "beasts"}) {
            bend(weights, hostile, 0.4);
        }
    }
    if (key_location_untended(state)) {
        bend(weights, "builders", 0.2);
        bend(weights, "descendants", 0.4);
    }
    if (state == "sealed") {
        bend(weights, "none", 1.8);
        bend(weights, "squatters", 0.3);
    }
    if (state == "corrupted") {
        bend(weights, "cult", 2.0);
        bend(weights, "undead", 1.8);
        bend(weights, "monsters", 1.5);
        bend(weights, "none", 0.5);
    }
    if (context.near_nest) {
        bend(weights, "beasts", 2.0);
        bend(weights, "monsters", 1.8);
        bend(weights, "none", 0.6);
    }
    if (context.remote) {
        bend(weights, "squatters", 0.5);
    } else {
        bend(weights, "monsters", 0.6);
    }
    return choose(std::move(weights), draw);
}

// Something else moved in after the builders left. MUTATES the site's occupant and the
// threat below is computed from the mutated value -- recorded rather than silently
// overwritten, because "a goblin warren that used to be a silver mine" is a better
// location than either half and a consumer should be able to say so.
namespace {

std::optional<KeyLocationSuccessionRecord> succeed(KeyLocationSite& site,
                                                   const KeyLocationContext& context, Rng& draw) {
    if (!key_location_untended(site.state) || context.ages < 1) return std::nullopt;
    const KeyLocationSuccessionRule* rule = nullptr;
    for (const KeyLocationSuccessionRule& row : key_location_succession()) {
        if (row.family == site.family) rule = &row;
    }
    if (rule == nullptr) return std::nullopt;  // lair and wonder: nothing moves into them
    if (draw.random() >= rule->chance) return std::nullopt;
    const std::string_view became = choose(copy_weights(rule->occupants), draw);
    if (became == site.occupant) return std::nullopt;  // never record a no-op
    KeyLocationSuccessionRecord record;
    record.from_occupant = site.occupant;
    record.to_occupant = became;
    record.note = rule->note;
    site.occupant = became;
    return record;
}

}  // namespace

// An integer 1-5 on the creature-tier rubric. Travel distance is deliberately not in it --
// that is a function of where the party is, not of the place.
int key_location_threat(const KeyLocationSite& site, const KeyLocationContext& context) {
    int value = 1;
    for (const KeyLocationThreatBase& row : key_location_threat_base()) {
        if (row.occupant == site.occupant) value = row.threat;
    }
    if (site.state == "corrupted") value += 1;
    if (site.tier >= 2 && key_location_hostile(site.occupant)) value += 1;
    if (site.state == "active" && (site.occupant == "builders" || site.occupant == "descendants")) {
        value -= 1;
    }
    if (context.near_nest && context.nest_tier && *context.nest_tier != 0) {
        value = std::max(value, std::min(kKeyLocationMaxThreat, *context.nest_tier));
    }
    if (context.unstable) value += 1;
    if (context.near_villain) value += 1;
    return std::max(kKeyLocationMinThreat, std::min(kKeyLocationMaxThreat, value));
}

// ---------------------------------------------------------------------------
// Naming.
// ---------------------------------------------------------------------------

namespace {

std::string fill_template(std::string_view templ, const KeyLocationNameFacts& context, Rng& draw) {
    // The {adj} draw FIRES WHETHER OR NOT the template uses the token. Making it lazy is an
    // obvious-looking optimisation and it changes the stream length for every archetype
    // whose templates are mixed, which moves every later draw for that site.
    const auto adjectives = key_location_adjectives();
    const std::string adjective(adjectives[static_cast<std::size_t>(draw.below(adjectives.size()))]);
    const std::pair<std::string_view, std::string> replacements[] = {
        {"{near}", context.near.empty() ? "the waste" : context.near},
        {"{culture}", context.culture.empty() ? "the old people" : context.culture},
        {"{school}", context.school.empty() ? "unquiet" : context.school},
        {"{god}", context.god.empty() ? "the nameless" : context.god},
        {"{adj}", adjective},
        {"{ordinal}", context.ordinal.empty() ? "lone" : context.ordinal},
    };
    std::string text(templ);
    for (const auto& [token, value] : replacements) {
        std::size_t at = 0;
        while ((at = text.find(token, at)) != std::string::npos) {
            text.replace(at, token.size(), value);
            at += value.size();
        }
    }
    return text;
}

bool contains(std::string_view text, std::string_view token) {
    return text.find(token) != std::string_view::npos;
}

}  // namespace

std::string key_location_name_for(const KeyLocationArchetype& archetype,
                                  const KeyLocationNameFacts& context, Rng& draw) {
    std::vector<std::string_view> templates(archetype.names.begin(), archetype.names.end());
    // A chain member is named by its place in the chain when the archetype offers that, so
    // a run of waystones counts itself out instead of reading as eleven unrelated stones.
    std::vector<std::string_view> numbered;
    for (std::string_view t : templates) {
        if (contains(t, "{ordinal}")) numbered.push_back(t);
    }
    if (!context.ordinal.empty() && !numbered.empty()) templates = numbered;
    std::string_view chosen = templates[static_cast<std::size_t>(draw.below(templates.size()))];
    std::vector<std::string_view> usable;
    for (std::string_view t : templates) {
        if (!contains(t, "{near}")) usable.push_back(t);
    }
    if (contains(chosen, "{near}") && context.near.empty() && !usable.empty()) {
        chosen = usable[static_cast<std::size_t>(draw.below(usable.size()))];
    }
    return fill_template(chosen, context, draw);
}

namespace {

bool starts_with(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

// A name no other location in this world already carries.
//
// Templates collide constantly -- three silver mines near the same city all want to be
// "the deeps under Bracken" -- and a duplicate name makes two different places
// indistinguishable to anything that refers to them by name. Ids stay the real key, but a
// name that cannot tell two locations apart has failed at its one job.
std::string unique_name(const KeyLocationArchetype& archetype, const KeyLocationNameFacts& context,
                        Rng& draw, std::set<std::string>& used, int attempts = 6) {
    for (int attempt = 0; attempt < attempts; ++attempt) {
        std::string candidate = key_location_name_for(archetype, context, draw);
        if (used.insert(candidate).second) return candidate;
    }
    const std::string article = "the ";
    std::string base = key_location_name_for(archetype, context, draw);
    if (starts_with(base, article)) base = base.substr(article.size());

    const auto adjectives = key_location_adjectives();
    std::vector<std::string> order(adjectives.begin(), adjectives.end());
    draw.shuffle(order);
    for (const std::string& prefix : order) {
        if (starts_with(base, prefix + " ")) continue;
        std::string candidate = article + prefix + " " + base;
        if (used.insert(candidate).second) return candidate;
    }
    // QUALIFIERS is disjoint from ADJECTIVES on purpose: an overlap silently shrinks the
    // variant pool, which only shows up as a duplicate name once a world is crowded enough
    // to exhaust it. The bare qualifier is retried inside the adjective loop, which is why
    // this is 312 string tests rather than 168.
    for (std::string_view qualifier : key_location_qualifiers()) {
        for (const std::string& adjective : order) {
            const std::string prefixes[2] = {std::string(qualifier),
                                             std::string(qualifier) + " " + adjective};
            for (const std::string& prefix : prefixes) {
                const std::size_t space = prefix.rfind(' ');
                const std::string last = space == std::string::npos ? prefix : prefix.substr(space + 1);
                if (starts_with(base, last + " ")) continue;
                std::string candidate = article + prefix + " " + base;
                if (used.insert(candidate).second) return candidate;
            }
        }
    }
    used.insert(article + base);
    return article + base;  // accepted as a duplicate, the last resort
}

// The record closest to `point`, or nullptr. Ties break on name for replay stability.
const KeyLocationPlace* nearest(const std::vector<KeyLocationPlace>& records, const Vec3& point,
                                double radius) {
    const KeyLocationPlace* best = nullptr;
    std::pair<double, std::string> best_key;
    for (const KeyLocationPlace& record : records) {
        if (!record.direction) continue;
        const double gap = great_circle_m(point, *record.direction, radius);
        std::pair<double, std::string> key{gap, !record.name.empty() ? record.name : record.id};
        if (best == nullptr || key < best_key) {
            best = &record;
            best_key = std::move(key);
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Interiors: a GRAPH, not geometry. Chambers have a depth, a role, a rough size and a list
// of what they connect to; they have no metre position, no walls and no contents. What the
// graph is good for is knowing that a mine goes four levels down and a barrow does not,
// that the flooded sump is between you and the vault, and that this warren has three ways
// in while that tower has one.
// ---------------------------------------------------------------------------

std::vector<int> connect_chamber(KeyLocationPlan plan, int index, Rng& draw) {
    if (index == 0) return {};  // the entrance chamber
    switch (plan) {
        case KeyLocationPlan::Linear:
            return {index - 1};
        case KeyLocationPlan::Radial:
            // The reference's two branches here are identical; the conditional is dead code
            // and is collapsed rather than reproduced, because it spends no draw either way.
            return {0};
        case KeyLocationPlan::Vault:
            return index <= 2 ? std::vector<int>{0}
                              : std::vector<int>{static_cast<int>(draw.randint(0, index - 1))};
        case KeyLocationPlan::Branching:
            // Two draws on the else branch, one on the spine branch.
            return draw.random() < 0.6 ? std::vector<int>{index - 1}
                                       : std::vector<int>{static_cast<int>(draw.randint(0, index - 1))};
        case KeyLocationPlan::Warren:
            break;
    }
    return {static_cast<int>(draw.randint(std::max(0, index - 3), index - 1))};
}

std::span<const std::string_view> chamber_roles(std::string_view family) {
    for (const KeyLocationRoleSet& row : key_location_chamber_roles()) {
        if (row.family == family) return row.roles;
    }
    return key_location_fallback_roles();
}

std::string_view entrance_kind_for(std::string_view family) {
    for (const KeyLocationEntranceKind& row : key_location_entrance_kinds()) {
        if (row.family == family) return row.kind;
    }
    return "door";
}

}  // namespace

std::optional<KeyLocationInterior> key_location_build_interior(const KeyLocationArchetype& archetype,
                                                               std::string_view state, Rng& draw) {
    if (!archetype.has_interior) return std::nullopt;
    KeyLocationInterior interior;
    interior.plan = archetype.interior_plan;
    interior.plan_note = key_location_plan_note(archetype.interior_plan);
    interior.levels =
        static_cast<int>(draw.randint(archetype.interior_levels_low, archetype.interior_levels_high));
    const int count = static_cast<int>(
        draw.randint(archetype.interior_chambers_low, archetype.interior_chambers_high));
    const auto roles = chamber_roles(archetype.family);
    const double flooded_bias = state == "drowned" ? 0.55 : 0.08;
    const double collapse_bias = (state == "ruined" || state == "buried") ? 0.30 : 0.05;

    for (int index = 0; index < count; ++index) {
        const std::vector<int> connects = connect_chamber(interior.plan, index, draw);
        KeyLocationChamber chamber;
        chamber.id = "chamber-" + std::to_string(index);
        // min(levels - 1, ...) caps depth, so a graph with levels: 1 is entirely at depth 0
        // and the flooded probability never scales.
        chamber.depth = index == 0 ? 0
                                   : std::min(interior.levels - 1,
                                              interior.chambers[static_cast<std::size_t>(connects[0])].depth +
                                                  (draw.random() < 0.45 ? 1 : 0));
        // Index 0 spends NO draw on its role -- the short-circuit is part of the stream.
        chamber.role = index != 0
                           ? std::string(roles[static_cast<std::size_t>(draw.below(roles.size()))])
                           : "entrance chamber";
        chamber.size_w_m = round_to(3.0 + draw.random() * 9.0, 1);
        chamber.size_d_m = round_to(3.0 + draw.random() * 9.0, 1);
        for (int c : connects) chamber.connects.push_back("chamber-" + std::to_string(c));
        chamber.flooded = draw.random() < flooded_bias * (1.0 + chamber.depth * 0.3);
        // SHORT-CIRCUITS at index 0, which spends no draw here either.
        chamber.collapsed = index > 0 && draw.random() < collapse_bias;
        interior.chambers.push_back(std::move(chamber));
    }

    if (interior.plan == KeyLocationPlan::Warren) {
        // Loops back on itself, the way something dug without a plan over a long time does.
        for (std::size_t i = 2; i < interior.chambers.size(); ++i) {
            if (draw.random() >= 0.3) continue;
            const auto other = static_cast<std::size_t>(draw.below(interior.chambers.size()));
            const std::string link = "chamber-" + std::to_string(other);
            KeyLocationChamber& chamber = interior.chambers[i];
            if (link != chamber.id &&
                std::find(chamber.connects.begin(), chamber.connects.end(), link) ==
                    chamber.connects.end()) {
                chamber.connects.push_back(link);
            }
        }
    }

    const std::string_view kind = entrance_kind_for(archetype.family);
    interior.entrances.push_back(
        {"entrance-0", "chamber-0", std::string(kind), state == "buried" || state == "sealed"});
    // The random() is only evaluated for these two plans, so the other three spend nothing.
    const int extra =
        (interior.plan == KeyLocationPlan::Warren || interior.plan == KeyLocationPlan::Branching) &&
                draw.random() < 0.45
            ? 1
            : 0;
    for (int index = 0; index < extra; ++index) {
        // No self or duplicate guard here, unlike the warren link above: a graph can carry
        // two entrances into chamber-0, one visible and one hidden.
        const KeyLocationChamber& target =
            interior.chambers[static_cast<std::size_t>(draw.below(interior.chambers.size()))];
        interior.entrances.push_back({"entrance-" + std::to_string(index + 1), target.id,
                                      target.collapsed ? "collapse" : std::string(kind), true});
    }
    return interior;
}

// ---------------------------------------------------------------------------
// The generation pass.
// ---------------------------------------------------------------------------

namespace {

std::string_view domain_name(KeyLocationDomain domain) {
    switch (domain) {
        case KeyLocationDomain::Land: return "land";
        case KeyLocationDomain::Water: return "water";
        case KeyLocationDomain::Ocean: return "ocean";
        case KeyLocationDomain::Lake: return "lake";
        case KeyLocationDomain::Any: return "any";
    }
    return "any";
}

bool in_domain(const Grid* water, int x, int z, KeyLocationDomain domain) {
    if (domain == KeyLocationDomain::Any || water == nullptr || water->empty()) return true;
    const double value = water->at(x, z);
    switch (domain) {
        case KeyLocationDomain::Land: return value == 0.0;
        case KeyLocationDomain::Water: return value != 0.0;
        case KeyLocationDomain::Ocean: return value == 1.0;
        case KeyLocationDomain::Lake: return value == 2.0;
        case KeyLocationDomain::Any: return true;
    }
    return true;
}

// Source cells for a distance sweep: the record's own (x, z) when it has both, else the
// cell its node addresses.
void push_cells(const std::vector<KeyLocationPlace>& records, const SphereGrid& grid,
                std::vector<std::pair<int, int>>& out) {
    for (const KeyLocationPlace& record : records) {
        if (record.x && record.z) {
            out.emplace_back(*record.x, *record.z);
        } else if (record.node) {
            const auto node = static_cast<std::size_t>(*record.node);
            if (node < grid.node_count()) out.emplace_back(grid.point_x[node], grid.point_z[node]);
        }
    }
}

// Where seated villains stand, plus the settlements they have claimed. A fallen villain no
// longer stands anywhere, so its seat is dropped; its CLAIMS are kept, because ground a
// villain took stays taken. That asymmetry is a decision, not an oversight.
std::vector<KeyLocationPlace> villain_holdings(const std::vector<KeyLocationVillain>& villains) {
    std::vector<KeyLocationPlace> out;
    for (const KeyLocationVillain& villain : villains) {
        const bool standing = villain.status.empty() || villain.status == "living";
        if (villain.seat.direction && standing) out.push_back(villain.seat);
        for (const KeyLocationPlace& claim : villain.claims) {
            if (claim.direction) out.push_back(claim);
        }
    }
    return out;
}

struct Machine {
    const KeyLocationRequest& request;
    int n = 0;
    double radius = 0.0;
    double spacing = 0.0;
    double reference_cell = 0.0;
    int ages = 0;
    std::shared_ptr<const SphereGrid> grid;
    LayerSet layers;
    std::map<KeyLocationDomain, std::vector<Cell>> cells_by_domain;
    double land_km2 = 0.0;
    Normalisers norms;
    std::optional<double> hazard_cut;
    std::map<int, const KeyLocationPlace*> nests_by_node;
    std::vector<std::string> god_ids;  // sorted
    std::map<std::string, std::string> god_names;
    std::set<int> claimed;
    std::vector<Vec3> settled;
    std::map<std::string, std::vector<Vec3>> family_taken;
    std::set<std::string> used_names;

    explicit Machine(const KeyLocationRequest& r) : request(r), layers(r.layers) {}

    KeyLocationContext context_for(const Cell& cell) const {
        KeyLocationContext out;
        out.near_ruin = layers.require("ruin_distance").at(cell.x, cell.z) <= kNearRuinCells * spacing;
        out.near_nest = layers.require("nest_distance").at(cell.x, cell.z) <= kNearNestCells * spacing;
        out.remote = layers.require("settlement_distance").at(cell.x, cell.z) >= kRemoteCells * spacing;
        const Grid* hazard = layers.find("magic_hazard");
        out.unstable = hazard != nullptr && hazard_cut && hazard->at(cell.x, cell.z) >= *hazard_cut;
        out.ages = ages;
        const auto nest = nests_by_node.find(cell.node);
        if (nest != nests_by_node.end()) out.nest_tier = nest->second->tier;
        out.near_villain =
            layers.require("villain_distance").at(cell.x, cell.z) <= kNearVillainCells * spacing;
        return out;
    }

    KeyLocationEnvironment environment_for(const Cell& cell) const {
        KeyLocationEnvironment out;
        const auto at = [&](std::string_view name) -> std::optional<double> {
            const Grid* grid = layers.find(name);
            return grid == nullptr ? std::nullopt : std::optional<double>(grid->at(cell.x, cell.z));
        };
        const auto natural = at("natural_biome");
        const auto variant = at("biome_variant");
        const auto coastal = at("coastal_exposure");
        if (natural) {
            out.natural_biome = static_cast<int>(*natural);
            // Keyed by catalogue ID, which is a SPARSE space. Keying by position silently
            // loses marsh, boreal forest, cold tundra and land ice.
            for (const KeyLocationBiomeName& row : request.natural_biomes) {
                if (row.id == *out.natural_biome) out.natural_biome_name = row.name;
            }
        }
        if (variant && *variant >= 0.0) {
            out.biome_variant = static_cast<int>(*variant);
            // Keyed by ARRAY POSITION, which is the world's own rule for the variants.
            const auto index = static_cast<std::size_t>(*out.biome_variant);
            if (index < request.magical_biomes.size()) {
                out.variant_asset_id = request.magical_biomes[index].asset_id;
                out.variant_name = request.magical_biomes[index].name;
            }
        }
        if (const auto t = at("temperature")) out.temperature_c = round_to(*t, 2);
        if (const auto m = at("moisture")) out.moisture = round_to(*m, 4);
        if (const auto s = at("slope")) out.slope = round_to(*s, 4);
        out.coastal = coastal && *coastal > 0.05;
        return out;
    }

    // Who built it and when, read off the nearest recorded people rather than invented.
    KeyLocationOrigin origin_for(const Cell& cell, const KeyLocationPlace* gated_ruin,
                                 const KeyLocationPlace* nearest_city) const {
        const KeyLocationPlace* source = gated_ruin != nullptr ? gated_ruin : nearest_city;
        KeyLocationOrigin out;
        out.age = source != nullptr ? source->founded_age : 0;
        if (source != nullptr && !source->source_culture.empty()) out.culture_id = source->source_culture;
        if (source != nullptr && !source->civilization_id.empty()) {
            out.civilization_id = source->civilization_id;
        }
        // Gated exactly as the rest are: the parameter IS the near-gated ruin.
        if (gated_ruin != nullptr) out.ruin_id = gated_ruin->id;
        const Grid* dominant = layers.find("dominant_magic");
        if (dominant != nullptr) {
            const int index = static_cast<int>(dominant->at(cell.x, cell.z));
            if (index >= 0 && index < static_cast<int>(request.magic_school_order.size())) {
                out.school = request.magic_school_order[static_cast<std::size_t>(index)];
            }
        }
        out.ages_since = std::max(0, ages - (source != nullptr ? source->founded_age : 0));
        return out;
    }

    struct ExtraName {
        std::string ordinal;
        std::string near;
    };

    // One site record, however it was positioned. Scatter, chain and cluster members differ
    // only in where they stand and what id they carry; everything downstream is decided the
    // same way, so a consumer never has to ask which pass produced a location.
    KeyLocationSite build(const KeyLocationArchetype& archetype, const Cell& cell,
                          double suitability, std::vector<KeyLocationFactor> factors,
                          KeyLocationPlacement placement_kind, std::string site_id, Rng& draw,
                          const ExtraName* extra) {
        const KeyLocationContext context = context_for(cell);
        const KeyLocationPlace* nearest_ruin = nearest(request.ruins, cell.direction, radius);
        const KeyLocationPlace* nearest_city = nearest(request.cities, cell.direction, radius);
        const KeyLocationOrigin origin =
            origin_for(cell, context.near_ruin ? nearest_ruin : nullptr, nearest_city);
        const std::string_view state = key_location_derive_state(archetype, context, draw);
        const std::string_view occupant = key_location_derive_occupant(archetype, state, context, draw);
        const KeyLocationPlace* near_record =
            (context.near_ruin && nearest_ruin != nullptr) ? nearest_ruin : nearest_city;

        KeyLocationNameFacts name_context;
        if (!god_ids.empty()) {
            const std::string& god = god_ids[static_cast<std::size_t>(draw.below(god_ids.size()))];
            name_context.god = god_names.at(god);
        }
        if (near_record != nullptr) {
            name_context.near = near_record->name;
            name_context.culture = near_record->civilization_id;
        }
        if (origin.school) name_context.school = *origin.school;
        if (extra != nullptr) {
            // Only the TRUTHY entries overwrite; an unnamed anchor leaves `near` alone.
            if (!extra->ordinal.empty()) name_context.ordinal = extra->ordinal;
            if (!extra->near.empty()) name_context.near = extra->near;
        }

        KeyLocationSite site;
        site.id = std::move(site_id);
        site.placement = placement_kind;
        site.kind = archetype.id;
        site.name = unique_name(archetype, name_context, draw, used_names);
        site.family = archetype.family;
        site.tier = archetype.tier;
        site.state = state;
        site.occupant = occupant;
        site.node = cell.node;
        site.x = cell.x;
        site.z = cell.z;
        site.direction = cell.direction;
        site.height_m = round_to(cell.height_m, 2);
        site.domain = archetype.domain;
        site.suitability = suitability;
        site.suitability_factors = std::move(factors);
        site.origin = origin;
        site.environment = environment_for(cell);
        site.asset_id = "marker.key_location." + std::string(archetype.id);
        site.reason = archetype.reason;
        site.succession = succeed(site, context, draw);
        site.threat = key_location_threat(site, context);
        // Gated on tier 2 alone, so titan_skeleton's declared interior is never built.
        site.interior = archetype.tier == 2 ? key_location_build_interior(archetype, site.state, draw)
                                            : std::nullopt;
        return site;
    }
};

double height_at(const LayerSet& layers, const Vec3& point, int n) {
    const auto [x, z] = key_location_cell_of_direction(point, n);
    return layers.require("height").at(x, z);
}

struct Span {
    double start, end;
    Vec3 a, b;
};

std::pair<std::vector<Span>, double> walk_polyline(const std::vector<Vec3>& points, double radius) {
    std::vector<Span> spans;
    double total = 0.0;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const double step = great_circle_m(points[i], points[i + 1], radius);
        spans.push_back({total, total + step, points[i], points[i + 1]});
        total += step;
    }
    return {spans, total};
}

// The direction at `distance` along a walked polyline.
//
// The reference's second guard -- "or this is the last span" -- compares a freshly built
// tuple by identity and is therefore ALWAYS FALSE, so only the first test ever fires. The
// dead branch is not reproduced; the fallback below is the path the reference actually
// takes when no span matches, and it returns the last span's START point rather than its
// end, which is what a "fix" to a real last-span test would quietly change.
Vec3 at_distance(const std::vector<Span>& spans, double distance) {
    for (const Span& span : spans) {
        if (distance <= span.end) {
            const double fraction =
                span.end <= span.start ? 0.0 : (distance - span.start) / (span.end - span.start);
            return key_location_slerp(span.a, span.b, std::max(0.0, std::min(1.0, fraction)));
        }
    }
    return spans.back().a;
}

// Whether nothing between two points stands above the line joining them. A beacon chain
// that cannot see itself is decoration.
bool line_of_sight(const Vec3& a, const Vec3& b, const LayerSet& layers, int n, double height_a,
                   double height_b) {
    for (int step = 1; step < kLineOfSightSamples; ++step) {
        const double fraction = static_cast<double>(step) / kLineOfSightSamples;
        const Vec3 between = key_location_slerp(a, b, fraction);
        const double sightline = height_a + (height_b - height_a) * fraction;
        if (height_at(layers, between, n) > sightline) return false;
    }
    return true;
}

struct Path {
    std::string key;
    std::vector<Vec3> points;
    const KeyLocationSite* anchor = nullptr;
};

}  // namespace

// ---------------------------------------------------------------------------

KeyLocationBlock generate_key_locations(const KeyLocationRequest& request) {
    KeyLocationBlock block;
    block.catalogue_revision = key_location_catalogue_revision();
    block.catalogue_count = static_cast<int>(key_location_archetypes().size());
    block.method = key_location_method();
    block.limits = key_location_limits();
    try {
        const Grid* height = request.layers == nullptr ? nullptr : [&] {
            const auto found = request.layers->find("height");
            return found != request.layers->end() ? &found->second : nullptr;
        }();
        if (height == nullptr || height->empty()) {
            throw std::runtime_error(
                "key locations need the height layer; generate a world past the terrain stages");
        }
        if (!(request.radius_m > 0.0)) {
            throw std::runtime_error(
                "key locations need the EFFECTIVE globe radius in metres, not the design-space one");
        }

        Machine m(request);
        m.n = request.n > 0 ? request.n : height->size();
        m.radius = request.radius_m;
        m.spacing = request.spacing_m > 0.0 ? request.spacing_m
                                            : 2.0 * kPi * m.radius / std::max(1, m.n - 1);
        m.reference_cell = 2.0 * kPi * m.radius / (kKeyLocationReferenceRaster - 1);
        m.ages = request.ages;
        m.grid = sphere_grid(m.n, m.radius);

        // Seven derived fields the generator does not publish but archetypes need: nothing
        // upstream had to ask how far a cell is from a road, a settlement or a ruin.
        {
            std::vector<std::pair<int, int>> road_cells;
            for (const KeyLocationRoute& route : request.routes) {
                for (int node : route.nodes) {
                    const auto index = static_cast<std::size_t>(node);
                    if (index < m.grid->node_count()) {
                        road_cells.emplace_back(m.grid->point_x[index], m.grid->point_z[index]);
                    }
                }
            }
            std::vector<std::pair<int, int>> settled_cells;
            push_cells(request.cities, *m.grid, settled_cells);
            push_cells(request.hamlets, *m.grid, settled_cells);
            push_cells(request.fortresses, *m.grid, settled_cells);
            push_cells(request.ports, *m.grid, settled_cells);
            std::vector<std::pair<int, int>> ruin_cells, nest_cells, villain_cells;
            push_cells(request.ruins, *m.grid, ruin_cells);
            push_cells(request.nests, *m.grid, nest_cells);
            push_cells(villain_holdings(request.villains), *m.grid, villain_cells);

            m.layers.set("road_distance", key_location_distance_field(road_cells, m.n, m.spacing));
            m.layers.set("settlement_distance",
                         key_location_distance_field(settled_cells, m.n, m.spacing));
            m.layers.set("ruin_distance", key_location_distance_field(ruin_cells, m.n, m.spacing));
            m.layers.set("nest_distance", key_location_distance_field(nest_cells, m.n, m.spacing));
            m.layers.set("villain_distance",
                         key_location_distance_field(villain_cells, m.n, m.spacing));
            m.layers.set("frontier", key_location_frontier_field(m.layers.find("culture_region"), m.n));
            // What the sea next door is worth, carried onto the shore. The underlying layer
            // is zero on every land cell at every raster measured, so this is the only form
            // of the question a `domain: land` archetype can actually ask.
            m.layers.set("coastal_fishing_productivity",
                         key_location_coastal_water_field(m.layers.find("fishing_productivity"),
                                                          m.layers.find("water_type"), m.n));
        }

        const Grid* water = m.layers.find("water_type");
        const Grid& height_layer = m.layers.require("height");
        for (KeyLocationDomain domain :
             {KeyLocationDomain::Land, KeyLocationDomain::Water, KeyLocationDomain::Ocean,
              KeyLocationDomain::Lake, KeyLocationDomain::Any}) {
            std::vector<Cell>& out = m.cells_by_domain[domain];
            // POLES ARE EXCLUDED: a pole is one degenerate node covering a whole latitude
            // band, and a mausoleum on the north pole is a bug that looks like content. The
            // seam column is excluded by construction because x stops at n-2.
            for (int z = 1; z < m.n - 1; ++z) {
                const double area = band_wedge_area(z, m.n, m.radius);
                for (int x = 0; x < m.n - 1; ++x) {
                    if (!in_domain(water, x, z, domain)) continue;
                    out.push_back({x, z, m.grid->node_index(x, z), direction(x, z, m.n), area,
                                   height_layer.at(x, z)});
                }
            }
        }
        const std::vector<Cell>& land = m.cells_by_domain[KeyLocationDomain::Land];
        for (const Cell& c : land) m.land_km2 += c.area_m2;
        m.land_km2 /= 1e6;

        std::set<std::string> referenced;
        for (const KeyLocationArchetype& a : key_location_archetypes()) {
            for (const KeyLocationRequire& t : a.requirements) referenced.emplace(t.layer);
            for (const KeyLocationPrefer& t : a.prefers) referenced.emplace(t.layer);
        }
        m.norms = build_normalisers(m.layers, referenced, land);

        if (const Grid* hazard = m.layers.find("magic_hazard"); hazard != nullptr && !land.empty()) {
            m.hazard_cut = key_location_percentile(term_values(*hazard, land), kUnstablePercentile);
        }

        for (const KeyLocationPlace& nest : request.nests) {
            if (nest.node) m.nests_by_node.emplace(*nest.node, &nest);
        }
        for (const KeyLocationGod& god : request.gods) {
            m.god_ids.push_back(god.id);
            m.god_names.emplace(god.id, god.name);
        }
        std::sort(m.god_ids.begin(), m.god_ids.end());

        // Ground already carrying a ruin, nest, shrine, college or landmark is left to the
        // module that placed it.
        for (const auto* group : {&request.ruins, &request.nests, &request.religion_sites,
                                  &request.colleges, &request.landmarks}) {
            for (const KeyLocationPlace& record : *group) {
                if (record.node) m.claimed.insert(*record.node);
            }
        }
        for (const auto* group : {&request.cities, &request.hamlets, &request.fortresses,
                                  &request.ports, &request.colleges}) {
            for (const KeyLocationPlace& record : *group) {
                if (record.direction) m.settled.push_back(*record.direction);
            }
        }

        // ------------------------------------------------------------------
        // Scatter pass. Biggest tiers claim ground first, so a wonder gets its ground
        // before a waystone; the id is the deterministic tie-break.
        // ------------------------------------------------------------------
        std::vector<const KeyLocationArchetype*> scattered;
        for (const KeyLocationArchetype& a : key_location_archetypes()) {
            if (a.placement == KeyLocationPlacement::Node) scattered.push_back(&a);
        }
        std::sort(scattered.begin(), scattered.end(),
                  [](const KeyLocationArchetype* a, const KeyLocationArchetype* b) {
                      if (a->tier != b->tier) return a->tier > b->tier;
                      return a->id < b->id;
                  });

        for (const KeyLocationArchetype* archetype : scattered) {
            const std::vector<Cell>& cells = m.cells_by_domain[archetype->domain];
            const KeyLocationFamily* family = key_location_family(archetype->family);
            if (family == nullptr) throw std::runtime_error("archetype names an unknown family");
            // POST-scaling absolute metres against a FIXED reference raster, never the
            // raster in hand. Where the real raster is coarser, the node grid binds first,
            // which is honest: you cannot place two distinct points inside one cell.
            const double spacing_m = archetype->has_spacing_m
                                         ? archetype->spacing_m
                                         : archetype->spacing_cells * m.reference_cell;
            const double clearance_m = archetype->has_settlement_clearance_m
                                           ? archetype->settlement_clearance_m
                                           : archetype->settlement_clearance_cells * m.reference_cell;
            const double family_spacing_m = family->family_spacing_cells * m.reference_cell;

            Rng draw = Rng::child(request.seed, "keyloc-place-" + std::string(archetype->id));
            const int wanted = key_location_budget(*archetype, m.land_km2, draw);
            const auto resolved = resolve_terms(*archetype, m.layers, cells);
            std::vector<const Cell*> candidates;
            if (resolved) candidates = eligible_cells(*resolved, cells);

            if (candidates.empty() || wanted <= 0) {
                // Three absences that look identical in a count and are not the same
                // problem: a catalogue naming a field that carries no data in its own
                // domain, a world that lacks the ground this time, and a world too small.
                std::string reason;
                if (candidates.empty()) {
                    reason = gap_reason(*archetype, m.layers, cells, domain_name(archetype->domain));
                    if (reason.empty()) reason = "no ground in this world satisfies the requirements";
                } else {
                    reason = "the world is too small to support one";
                }
                block.diagnostics.push_back({std::string(archetype->id), 0, wanted,
                                             static_cast<int>(candidates.size()), std::move(reason)});
                continue;
            }

            std::vector<Vec3>& taken = m.family_taken[std::string(archetype->family)];
            const std::vector<Chosen> chosen =
                select_cells(*archetype, spacing_m, clearance_m, family_spacing_m, m.layers,
                             candidates, m.norms, m.radius, draw, wanted, m.settled, m.claimed, taken);
            for (const Chosen& entry : chosen) {
                m.claimed.insert(entry.cell->node);  // affects later archetypes, not this one
                taken.push_back(entry.cell->direction);
                Rng site_draw = Rng::child(request.seed, "keyloc-site-" + std::string(archetype->id) +
                                                             "-" + std::to_string(entry.cell->node));
                block.sites.push_back(m.build(*archetype, *entry.cell, entry.suitability,
                                              entry.factors, KeyLocationPlacement::Node,
                                              "keyloc-" + std::string(archetype->id) + "-" +
                                                  std::to_string(entry.cell->node),
                                              site_draw, nullptr));
            }

            std::string reason;
            if (static_cast<int>(chosen.size()) == wanted) {
                reason = "placed";
            } else if (chosen.empty()) {
                // "left room for fewer" is a false statement when the answer is none, and it
                // sent a reader looking at spacing constants. Count the INTERSECTION with the
                // domain, never the bare length of the claim list -- most claimed nodes are
                // water, pole or seam nodes that never enter a land archetype's candidates.
                int occupied = 0;
                for (const Cell* c : candidates) {
                    if (m.claimed.count(c->node) != 0) ++occupied;
                }
                reason = "no room: " + std::to_string(occupied) + " of " +
                         std::to_string(candidates.size()) +
                         " qualifying cells already carry another feature, and spacing or "
                         "settlement clearance refused the remaining " +
                         std::to_string(candidates.size() - static_cast<std::size_t>(occupied));
            } else {
                reason = "spacing and clearance left room for fewer";
            }
            block.diagnostics.push_back({std::string(archetype->id), static_cast<int>(chosen.size()),
                                         wanted, static_cast<int>(candidates.size()),
                                         std::move(reason)});
        }

        // ------------------------------------------------------------------
        // Composition. A lone waystone is noise; eleven of them counting down to a shrine is
        // a story. Most of what makes a map read as authored is not the individual places
        // but the relationships between them, and a per-cell suitability field has no way to
        // say "and then another one, two kilometres on".
        //
        // Both passes see only the SCATTER sites: a cluster cannot anchor on a chain member.
        // ------------------------------------------------------------------
        const std::size_t scatter_count = block.sites.size();
        std::vector<KeyLocationSite> chain_members;
        std::vector<KeyLocationSite> cluster_members;

        {
            std::vector<const KeyLocationChainSpec*> specs;
            for (const KeyLocationChainSpec& spec : key_location_chain_specs()) specs.push_back(&spec);
            std::sort(specs.begin(), specs.end(),
                      [](const KeyLocationChainSpec* a, const KeyLocationChainSpec* b) {
                          return a->id < b->id;
                      });
            for (const KeyLocationChainSpec* spec : specs) {
                const double interval = spec->interval_m;
                std::vector<Path> paths;
                if (spec->along == KeyLocationAlong::Road) {
                    for (const KeyLocationRoute& route : request.routes) {
                        if (route.nodes.size() < 2) continue;
                        Path path;
                        path.key = "r" + (route.from.empty() ? std::string("?") : route.from) + "-" +
                                   (route.to.empty() ? std::string("?") : route.to);
                        for (int node : route.nodes) {
                            const auto index = static_cast<std::size_t>(node);
                            const int x = index < m.grid->node_count() ? m.grid->point_x[index] : 0;
                            const int z = index < m.grid->node_count() ? m.grid->point_z[index] : 0;
                            path.points.push_back(direction(x, z, m.n));
                        }
                        paths.push_back(std::move(path));
                    }
                    std::sort(paths.begin(), paths.end(),
                              [](const Path& a, const Path& b) { return a.key < b.key; });
                } else if (spec->along == KeyLocationAlong::Frontier) {
                    // Territory is a raster frontier rather than a polygon, so there is no
                    // border to walk and it has to be recovered: start at the westernmost
                    // border cell and always step to the nearest unvisited one.
                    const Grid* frontier = m.layers.find("frontier");
                    std::vector<const Cell*> border;
                    if (frontier != nullptr) {
                        for (const Cell& c : land) {
                            if (frontier->at(c.x, c.z) >= 0.5) border.push_back(&c);
                        }
                    }
                    if (border.size() >= 3) {
                        std::sort(border.begin(), border.end(), [](const Cell* a, const Cell* b) {
                            if (a->x != b->x) return a->x < b->x;
                            return a->z < b->z;
                        });
                        std::vector<const Cell*> ordered{border.front()};
                        std::vector<const Cell*> remaining(border.begin() + 1, border.end());
                        while (!remaining.empty()) {
                            const Vec3& last = ordered.back()->direction;
                            auto best = remaining.begin();
                            double best_distance = great_circle_m(last, (*best)->direction, m.radius);
                            for (auto it = remaining.begin(); it != remaining.end(); ++it) {
                                const double d = great_circle_m(last, (*it)->direction, m.radius);
                                if (d < best_distance ||
                                    (d == best_distance && (*it)->node < (*best)->node)) {
                                    best = it;
                                    best_distance = d;
                                }
                            }
                            if (best_distance > interval * 3.0) break;
                            ordered.push_back(*best);
                            remaining.erase(best);
                        }
                        if (ordered.size() >= 3) {
                            Path path;
                            path.key = "frontier";
                            for (const Cell* c : ordered) path.points.push_back(c->direction);
                            paths.push_back(std::move(path));
                        }
                    }
                } else {
                    // Only the places actually worth walking to: every shrine having its own
                    // pilgrim road makes none of them a pilgrimage.
                    std::vector<const KeyLocationSite*> anchors;
                    for (std::size_t i = 0; i < scatter_count; ++i) {
                        const KeyLocationSite& site = block.sites[i];
                        if (site.family == spec->anchor_family && site.tier >= spec->anchor_tier) {
                            anchors.push_back(&site);
                        }
                    }
                    std::sort(anchors.begin(), anchors.end(),
                              [](const KeyLocationSite* a, const KeyLocationSite* b) {
                                  if (a->tier != b->tier) return a->tier > b->tier;
                                  if (a->suitability != b->suitability) {
                                      return a->suitability > b->suitability;
                                  }
                                  return a->id < b->id;
                              });
                    if (spec->has_max_chains &&
                        static_cast<int>(anchors.size()) > spec->max_chains) {
                        anchors.resize(static_cast<std::size_t>(std::max(0, spec->max_chains)));
                    }
                    std::sort(anchors.begin(), anchors.end(),
                              [](const KeyLocationSite* a, const KeyLocationSite* b) {
                                  return a->id < b->id;
                              });
                    for (const KeyLocationSite* anchor : anchors) {
                        const KeyLocationPlace* start = nullptr;
                        std::pair<double, std::string> best_key;
                        for (const KeyLocationPlace& city : request.cities) {
                            if (!city.direction) continue;
                            std::pair<double, std::string> key{
                                great_circle_m(*city.direction, anchor->direction, m.radius), city.id};
                            if (start == nullptr || key < best_key) {
                                start = &city;
                                best_key = std::move(key);
                            }
                        }
                        if (start == nullptr) break;  // no cities: nothing to walk in from
                        Path path;
                        path.key = "to-" + anchor->id;
                        path.points = {*start->direction, anchor->direction};
                        path.anchor = anchor;
                        paths.push_back(std::move(path));
                    }
                }

                const KeyLocationArchetype* archetype = key_location_archetype(spec->archetype);
                if (archetype == nullptr) throw std::runtime_error("chain names an unknown archetype");
                for (const Path& path : paths) {
                    const auto [spans, total] = walk_polyline(path.points, m.radius);
                    if (spans.empty() || total < interval * 2.0) continue;
                    // ONE stream shared by every member of the path, so member order along
                    // the path is load-bearing.
                    Rng draw = Rng::child(request.seed, "keyloc-chain-" + std::string(spec->id) +
                                                            "-" + path.key);
                    std::vector<KeyLocationSite> placed;
                    std::optional<std::pair<Vec3, double>> previous;
                    double distance = interval;
                    int index = 0;
                    while (distance < total - interval * 0.5) {
                        const Vec3 point = at_distance(spans, distance);
                        const double point_height = height_at(m.layers, point, m.n);
                        if (spec->line_of_sight && previous) {
                            if (!line_of_sight(previous->first, point, m.layers, m.n, previous->second,
                                               point_height)) {
                                // Step forward and try again. NO member and NO draw.
                                distance += interval * 0.25;
                                continue;
                            }
                        }
                        const auto [x, z] = key_location_cell_of_direction(point, m.n);
                        Cell cell{x, z, m.grid->node_index(x, z), point, 0.0, point_height};
                        Machine::ExtraName extra;
                        const auto ordinals = key_location_ordinals();
                        extra.ordinal = index < static_cast<int>(ordinals.size())
                                            ? std::string(ordinals[static_cast<std::size_t>(index)])
                                            : std::to_string(index + 1) + "th";
                        if (path.anchor != nullptr) extra.near = path.anchor->name;
                        placed.push_back(m.build(*archetype, cell, 0.0, {}, KeyLocationPlacement::Path,
                                                 "keyloc-" + std::string(spec->archetype) + "-" +
                                                     std::string(spec->id) + "-" + path.key + "-" +
                                                     std::to_string(std::llround(distance)),
                                                 draw, &extra));
                        previous = std::make_pair(point, point_height);
                        distance += interval;
                        ++index;
                    }
                    if (placed.size() < 2) continue;  // and its members never reach `sites`
                    const std::string chain_id = "chain-" + std::string(spec->id) + "-" + path.key;
                    KeyLocationChain chain;
                    chain.id = chain_id;
                    chain.kind = spec->id;
                    chain.name = spec->name;
                    chain.archetype = spec->archetype;
                    chain.along = spec->along;
                    chain.interval_m = round_to(interval, 1);
                    chain.length_m = round_to(total, 1);
                    if (path.anchor != nullptr) chain.anchor = path.anchor->id;
                    for (std::size_t position = 0; position < placed.size(); ++position) {
                        placed[position].links.chain = chain_id;
                        placed[position].links.chain_index = static_cast<int>(position);
                        if (path.anchor != nullptr) placed[position].links.anchor = path.anchor->id;
                        chain.members.push_back(placed[position].id);
                    }
                    chain.reason = spec->reason;
                    block.chains.push_back(std::move(chain));
                    for (KeyLocationSite& member : placed) chain_members.push_back(std::move(member));
                }
            }
        }

        {
            std::vector<const KeyLocationClusterSpec*> specs;
            for (const KeyLocationClusterSpec& spec : key_location_cluster_specs()) {
                specs.push_back(&spec);
            }
            std::sort(specs.begin(), specs.end(),
                      [](const KeyLocationClusterSpec* a, const KeyLocationClusterSpec* b) {
                          return a->id < b->id;
                      });
            const Grid* water_layer = m.layers.find("water_type");
            for (const KeyLocationClusterSpec* spec : specs) {
                const KeyLocationArchetype* archetype = key_location_archetype(spec->archetype);
                if (archetype == nullptr) throw std::runtime_error("cluster names an unknown archetype");
                std::vector<const KeyLocationSite*> anchors;
                for (std::size_t i = 0; i < scatter_count; ++i) {
                    const KeyLocationSite& site = block.sites[i];
                    const bool by_kind =
                        std::find(spec->anchor_kinds.begin(), spec->anchor_kinds.end(), site.kind) !=
                        spec->anchor_kinds.end();
                    const bool by_family = std::find(spec->anchor_families.begin(),
                                                     spec->anchor_families.end(),
                                                     site.family) != spec->anchor_families.end();
                    if (!by_kind && !by_family) continue;
                    // An EMPTY state list matches any state, not none.
                    if (!spec->anchor_states.empty() &&
                        std::find(spec->anchor_states.begin(), spec->anchor_states.end(),
                                  site.state) == spec->anchor_states.end()) {
                        continue;
                    }
                    anchors.push_back(&site);
                }
                std::sort(anchors.begin(), anchors.end(),
                          [](const KeyLocationSite* a, const KeyLocationSite* b) {
                              return a->id < b->id;
                          });
                for (const KeyLocationSite* anchor : anchors) {
                    // Created BEFORE the chance roll, so an anchor that fails still isolates
                    // its own stream and nothing downstream shifts.
                    Rng draw = Rng::child(request.seed, "keyloc-cluster-" + std::string(spec->id) +
                                                            "-" + anchor->id);
                    if (draw.random() >= spec->chance) continue;
                    const int wanted =
                        static_cast<int>(draw.randint(spec->count_low, spec->count_high));
                    std::vector<KeyLocationSite> produced;
                    for (int index = 0; index < wanted; ++index) {
                        const double gap = spec->offset_near_m +
                                           (spec->offset_far_m - spec->offset_near_m) * draw.random();
                        // Walking the offset rather than snapping to a node is the point: a
                        // camp meant to be 1.5 km from the wall would otherwise land wherever
                        // the raster allows, which on a coarse one is ten times too far.
                        std::optional<KeyLocationSite> member;
                        for (int attempt = 0; attempt < kClusterAttempts; ++attempt) {
                            const double bearing =
                                std::fmod(index * kClusterGoldenAngle +
                                              attempt * kClusterAttemptStep + draw.random() * 0.5,
                                          2.0 * kPi);
                            const Vec3 point =
                                key_location_offset(anchor->direction, bearing, gap, m.radius);
                            const auto [x, z] = key_location_cell_of_direction(point, m.n);
                            if (archetype->domain == KeyLocationDomain::Land &&
                                water_layer != nullptr && !water_layer->empty() &&
                                water_layer->at(x, z) != 0.0) {
                                continue;  // the draw is already spent
                            }
                            Cell cell{x, z, m.grid->node_index(x, z), point, 0.0,
                                      m.layers.require("height").at(x, z)};
                            member = m.build(*archetype, cell, 0.0, {}, KeyLocationPlacement::Cluster,
                                             "keyloc-" + std::string(spec->archetype) + "-" +
                                                 std::to_string(anchor->node) + "-" +
                                                 std::to_string(std::llround(gap)) + "-" +
                                                 std::to_string(index),
                                             draw, nullptr);
                            break;
                        }
                        if (!member) continue;  // eight water rejections: silently skipped
                        member->links.cluster =
                            "cluster-" + std::string(spec->id) + "-" + anchor->id;
                        member->links.anchor = anchor->id;
                        produced.push_back(std::move(*member));
                    }
                    if (produced.empty()) continue;  // and no cluster record is emitted
                    KeyLocationCluster cluster;
                    cluster.id = "cluster-" + std::string(spec->id) + "-" + anchor->id;
                    cluster.kind = spec->id;
                    cluster.name = spec->name;
                    cluster.anchor = anchor->id;
                    cluster.anchor_kind = anchor->kind;
                    cluster.anchor_name = anchor->name;
                    for (const KeyLocationSite& member : produced) cluster.members.push_back(member.id);
                    cluster.reason = spec->reason;
                    block.clusters.push_back(std::move(cluster));
                    for (KeyLocationSite& member : produced) {
                        cluster_members.push_back(std::move(member));
                    }
                }
            }
        }

        for (KeyLocationSite& site : chain_members) block.sites.push_back(std::move(site));
        for (KeyLocationSite& site : cluster_members) block.sites.push_back(std::move(site));
        // Interleaves scatter, chain and cluster members deliberately: this is the emitted
        // order and a consumer reads one list.
        std::sort(block.sites.begin(), block.sites.end(),
                  [](const KeyLocationSite& a, const KeyLocationSite& b) {
                      if (a.family != b.family) return a.family < b.family;
                      if (a.kind != b.kind) return a.kind < b.kind;
                      if (a.links.chain != b.links.chain) return a.links.chain < b.links.chain;
                      if (a.links.chain_index != b.links.chain_index) {
                          return a.links.chain_index < b.links.chain_index;
                      }
                      return a.id < b.id;
                  });

        // One diagnostic per chain spec then per cluster spec, in DOCUMENT order.
        const auto composed_diagnostic = [&](std::string_view archetype, std::string_view spec_id) {
            int produced = 0;
            for (const KeyLocationSite& site : block.sites) {
                if (site.kind == archetype && (!site.links.chain.empty() || !site.links.cluster.empty())) {
                    ++produced;
                }
            }
            block.diagnostics.push_back(
                {std::string(archetype), produced, produced, produced,
                 produced > 0 ? "composed by " + std::string(spec_id)
                              : "nothing in this world to run " + std::string(spec_id) + " along"});
        };
        for (const KeyLocationChainSpec& spec : key_location_chain_specs()) {
            composed_diagnostic(spec.archetype, spec.id);
        }
        for (const KeyLocationClusterSpec& spec : key_location_cluster_specs()) {
            composed_diagnostic(spec.archetype, spec.id);
        }

        block.summary.sites = static_cast<int>(block.sites.size());
        for (const KeyLocationSite& site : block.sites) {
            ++block.summary.by_tier[site.tier];
            ++block.summary.by_family[site.family];
            ++block.summary.by_state[site.state];
            ++block.summary.by_occupant[site.occupant];
            if (site.tier >= 2 && key_location_hostile(site.occupant)) ++block.summary.dungeons;
        }
        block.ok = true;
    } catch (const std::exception& exc) {
        // A failure is a REPORTED block, never a crashed world.
        block.ok = false;
        block.error = exc.what();
        block.sites.clear();
        block.chains.clear();
        block.clusters.clear();
        block.diagnostics.clear();
        block.summary = {};
    }
    return block;
}

// ---------------------------------------------------------------------------
// Exterior plans.
// ---------------------------------------------------------------------------

namespace {

// Height at a local metre offset from a site, bilinear over the raster. Local metres
// convert to a fraction of a cell through the raster's own spacing, so this stays correct
// at every world size without carrying a metres-per-cell constant. NOT `fwg::sample`,
// which takes a direction rather than a local offset and has its own clamp rule.
double sample_local_height(const Grid* height, const Vec3& point, double offset_x_m,
                           double offset_z_m, int n, double radius) {
    if (height == nullptr || height->empty()) return 0.0;
    const double cell_m = 2.0 * kPi * radius / std::max(1, n - 1);
    const auto [base_x, base_z] = key_location_cell_of_direction(point, n);
    const double fx = base_x + offset_x_m / cell_m;
    const double fz = base_z + offset_z_m / cell_m;
    const auto x0 = static_cast<int>(std::floor(fx));
    const auto z0 = static_cast<int>(std::floor(fz));
    const double tx = fx - x0;
    const double tz = fz - z0;
    // Clamps z and wraps x, so a plot offset that walks off the raster reads the nearest
    // row rather than failing.
    const auto at = [&](int x, int z) {
        return height->at(floor_mod(x, n - 1), std::max(0, std::min(n - 1, z)));
    };
    const double top = at(x0, z0) * (1.0 - tx) + at(x0 + 1, z0) * tx;
    const double bottom = at(x0, z0 + 1) * (1.0 - tx) + at(x0 + 1, z0 + 1) * tx;
    return top * (1.0 - tz) + bottom * tz;
}

// Downhill direction in the local frame, so working sites can follow the fall. Measured in
// DEGREES FROM EAST, which is a different convention from `key_location_offset`'s bearing;
// the two are never converted into each other.
double slope_bearing(const Grid* height, const Vec3& point, int n, double radius) {
    const double step = 2.0 * kPi * radius / std::max(1, n - 1) * 0.5;
    const double east = sample_local_height(height, point, step, 0.0, n, radius) -
                        sample_local_height(height, point, -step, 0.0, n, radius);
    const double north = sample_local_height(height, point, 0.0, step, n, radius) -
                         sample_local_height(height, point, 0.0, -step, n, radius);
    // Exactly 0.0 on perfectly flat ground, which makes row, terrace and mouth align east.
    if (std::abs(east) < 1e-9 && std::abs(north) < 1e-9) return 0.0;
    double degrees = std::atan2(-north, -east) * 180.0 / kPi;
    degrees = std::fmod(degrees, 360.0);
    return degrees < 0.0 ? degrees + 360.0 : degrees;
}

struct Placement {
    double x, z, degrees;
};

// Local metre offsets and rotations for `count` parts under one arrangement rule.
//
// THE DRAW COUNTS ARE PART OF THE CONTRACT even where the geometry is deterministic: `row`
// and `courtyard` spend NONE, `single`, `terrace` and `mouth`-after-the-first spend one,
// and `ring` and `scatter` spend two. (data/key-locations-code-tables.json records scatter
// as one; the source spends two, one for the angle jitter and one for the rotation, and the
// source is what the stream follows.)
std::vector<Placement> positions(KeyLocationArrangement arrangement, int count, double spread_m,
                                 Rng& draw, double bearing_degrees) {
    std::vector<Placement> out;
    if (count <= 0) return out;
    const double bearing = bearing_degrees * kPi / 180.0;
    switch (arrangement) {
        case KeyLocationArrangement::Single:
            out.push_back({0.0, 0.0, draw.random() * 360.0});
            return out;
        case KeyLocationArrangement::Ring: {
            // Evenly spaced with a small jitter, so a circle reads as raised by hand rather
            // than stamped.
            const double step = 360.0 / count;
            for (int i = 0; i < count; ++i) {
                const double angle = (i * step + (draw.random() - 0.5) * step * 0.3) * kPi / 180.0;
                const double r = spread_m * (0.9 + draw.random() * 0.2);
                out.push_back({r * std::cos(angle), r * std::sin(angle),
                               angle * 180.0 / kPi + 90.0});
            }
            return out;
        }
        case KeyLocationArrangement::Scatter: {
            // Golden-angle spiral, so parts never clump the way uniform random does at low
            // counts.
            for (int i = 0; i < count; ++i) {
                const double angle = i * kKeyLocationGoldenAngle + draw.random() * 0.4;
                const double r = spread_m * std::sqrt((i + 0.5) / count);
                out.push_back({r * std::cos(angle), r * std::sin(angle), draw.random() * 360.0});
            }
            return out;
        }
        case KeyLocationArrangement::Row: {
            const double step = count > 1 ? (spread_m * 2.0) / std::max(1, count - 1) : 0.0;
            for (int i = 0; i < count; ++i) {
                const double d = -spread_m + step * i;
                out.push_back({d * std::cos(bearing), d * std::sin(bearing), bearing_degrees});
            }
            return out;
        }
        case KeyLocationArrangement::Courtyard: {
            for (int i = 0; i < count; ++i) {
                const double angle = (360.0 * i / count) * kPi / 180.0;
                out.push_back({spread_m * std::cos(angle), spread_m * std::sin(angle),
                               angle * 180.0 / kPi + 180.0});
            }
            return out;
        }
        case KeyLocationArrangement::Terrace: {
            // Stepped along the fall of the ground: working sites follow the slope.
            for (int i = 0; i < count; ++i) {
                const double d = spread_m * (static_cast<double>(i) / std::max(1, count - 1) - 0.5) * 2.0;
                const double lateral = (draw.random() - 0.5) * spread_m * 0.5;
                out.push_back({d * std::cos(bearing) - lateral * std::sin(bearing),
                               d * std::sin(bearing) + lateral * std::cos(bearing), bearing_degrees});
            }
            return out;
        }
        case KeyLocationArrangement::Mouth: {
            // One part on the entrance and the rest fanned across the approach in front.
            out.push_back({0.0, 0.0, bearing_degrees});
            for (int i = 1; i < count; ++i) {
                const double angle = (bearing_degrees + (i - count / 2.0) * 28.0) * kPi / 180.0;
                const double r = spread_m * (0.5 + draw.random() * 0.5);
                out.push_back({r * std::cos(angle), r * std::sin(angle), angle * 180.0 / kPi + 180.0});
            }
            return out;
        }
    }
    throw std::runtime_error("unknown key location arrangement");
}

double positive_mod_360(double degrees) {
    double out = std::fmod(degrees, 360.0);
    return out < 0.0 ? out + 360.0 : out;
}

// A kit entry with the kit's own defaults folded in, plus the runtime weathering entry.
struct RuntimeEntry {
    const KeyLocationPart* part;
    int count_low, count_high;
    KeyLocationArrangement arrangement;
    double spread_m;
    KeyLocationPartKind kind;
};

// A kit plus what time did to it. The same fort is a different place ruined and held, and
// the difference a consumer sees should not be only a `state` string next to identical
// geometry. APPENDED, so the rubble is always the last part and the last stream consumer.
std::vector<RuntimeEntry> weathered_kit(const KeyLocationKit& kit, std::string_view state) {
    std::vector<RuntimeEntry> out;
    const double kit_spread = kit.has_spread_m ? kit.spread_m : 12.0;
    const auto parts = key_location_parts();
    for (const KeyLocationKitEntry& entry : kit.parts) {
        out.push_back({&parts[static_cast<std::size_t>(entry.part)], entry.count_low, entry.count_high,
                       entry.has_arrangement ? entry.arrangement : kit.arrangement, kit_spread,
                       // No shipped kit entry authors a `kind`, so every catalogue plot is a
                       // `structure` even where the PART is terrain. The asset list reads the
                       // part's kind instead and the two consumers disagree; the plan follows
                       // the entry, which is what the reference's exterior pass does.
                       KeyLocationPartKind::Structure});
    }
    int extra = 0;
    if (state == "ruined") extra = 4;
    else if (state == "buried" || state == "reclaimed") extra = 3;
    else if (state == "drowned" || state == "abandoned") extra = 2;
    else if (state == "corrupted") extra = 1;
    if (extra == 0) return out;  // only six of the eleven states are keyed
    const std::string_view id = state == "reclaimed" ? "keyloc.part.overgrown_mound"
                                                     : "keyloc.part.rubble_pile";
    const KeyLocationPart* part = nullptr;
    for (const KeyLocationPart& row : parts) {
        if (row.id == id) part = &row;
    }
    if (part == nullptr) throw std::runtime_error("the weathering pass names a part the catalogue lacks");
    out.push_back({part, std::max(1, extra - 2), extra, KeyLocationArrangement::Scatter, kit_spread,
                   KeyLocationPartKind::Terrain});
    return out;
}

KeyLocationSitePlan plan_for(const KeyLocationSite& site, const KeyLocationKit& kit,
                             const std::vector<RuntimeEntry>& entries, const Grid* height, int n,
                             double radius, Rng& draw) {
    const Vec3 point = site.direction;
    const double bearing = slope_bearing(height, point, n, radius);

    // PASS ONE: positions and ground moves only. Elevations are deliberately not resolved
    // yet, because a grave marker standing on a barrow has to sit on the barrow, and the
    // barrow does not exist until every shaping entry is known.
    struct Placed {
        const RuntimeEntry* entry;
        double x, z, degrees;
    };
    std::vector<Placed> placements;
    std::vector<KeyLocationShapingPlacement> shapes;
    double extent = 0.0;
    for (const RuntimeEntry& entry : entries) {
        const int count = static_cast<int>(draw.randint(entry.count_low, entry.count_high));
        for (const Placement& p : positions(entry.arrangement, count, entry.spread_m, draw, bearing)) {
            const double degrees = positive_mod_360(p.degrees);
            placements.push_back({&entry, p.x, p.z, degrees});
            extent = std::max({extent, std::abs(p.x) + entry.part->plot_width_m,
                               std::abs(p.z) + entry.part->plot_depth_m});
            if (entry.part->shaping.present) {
                KeyLocationShapingPlacement shape;
                shape.spec = entry.part->shaping;
                // ROUNDED before it is used: the deltas below read these rounded centres,
                // so the rounding is load-bearing rather than cosmetic.
                shape.x_m = round_to(p.x, 2);
                shape.z_m = round_to(p.z, 2);
                shape.rotation_degrees = round_to(degrees, 4);
                shape.part = entry.part->id;
                shapes.push_back(shape);
            }
        }
    }

    const auto base = [&](double x, double z) {
        return sample_local_height(height, point, x, z, n, radius);
    };
    const double centre_base = base(0.0, 0.0);

    // PASS TWO: plots, seated on the SHAPED ground.
    KeyLocationSitePlan plan;
    for (const Placed& placed : placements) {
        const KeyLocationPart& part = *placed.entry->part;
        const double rad = placed.degrees * kPi / 180.0;
        const double c = std::cos(rad);
        const double s = std::sin(rad);
        const double w = part.plot_width_m;
        const double d = part.plot_depth_m;
        const double corners[4][2] = {{-w / 2, -d / 2}, {w / 2, -d / 2}, {w / 2, d / 2}, {-w / 2, d / 2}};
        double highest = 0.0;
        double lowest = 0.0;
        for (int i = 0; i < 4; ++i) {
            const double cx = placed.x + corners[i][0] * c - corners[i][1] * s;
            const double cz = placed.z + corners[i][0] * s + corners[i][1] * c;
            const double ground = key_location_shaped_height(shapes, cx, cz, base(cx, cz), centre_base);
            if (i == 0 || ground > highest) highest = ground;
            if (i == 0 || ground < lowest) lowest = ground;
        }
        KeyLocationPlot plot;
        plot.id = "plot-" + std::to_string(plan.plots.size());
        plot.building_id = part.id;
        plot.name = part.name;
        plot.kind = placed.entry->kind;
        plot.x_m = round_to(placed.x, 2);
        plot.z_m = round_to(placed.z, 2);
        plot.rotation_degrees = round_to(placed.degrees, 4);
        plot.ground_elevation_m = round_to(highest, 4);
        plot.foundation_bottom_m = round_to(lowest, 4);
        plot.plot_width_m = part.plot_width_m;
        plot.plot_depth_m = part.plot_depth_m;
        plot.dimension_width_m = part.dimension_width_m;
        plot.dimension_depth_m = part.dimension_depth_m;
        plot.dimension_height_m = part.dimension_height_m;
        plot.road_access_x_m = plot.x_m;
        plot.road_access_z_m = plot.z_m;
        plan.plots.push_back(std::move(plot));
    }

    const double half = std::max(extent, kKeyLocationMinHalfExtentM);
    plan.terrain.cell_m = round_to(2.0 * half / (kKeyLocationSurfacePosts - 1), 4);
    plan.terrain.size = kKeyLocationSurfacePosts - 1;
    plan.terrain.bounds_m[0] = -round_to(half, 2);
    plan.terrain.bounds_m[1] = -round_to(half, 2);
    plan.terrain.bounds_m[2] = round_to(half, 2);
    plan.terrain.bounds_m[3] = round_to(half, 2);
    plan.terrain.shaping = shapes;

    if (!shapes.empty()) {
        // Sampled on a half/6 lattice by REPEATED ADDITION, which is what decides how many
        // samples the loop actually takes at the far edge.
        double highest = 0.0;
        double lowest = 0.0;
        const double step = half / 6.0;
        for (double j = -half; j <= half; j += step) {
            for (double i = -half; i <= half; i += step) {
                const double ground = base(i, j);
                const double change =
                    key_location_shaped_height(shapes, i, j, ground, centre_base) - ground;
                highest = std::max(highest, change);
                lowest = std::min(lowest, change);
            }
        }
        plan.terrain.raised_m = round_to(highest, 3);
        plan.terrain.lowered_m = round_to(lowest, 3);
    }

    const double surface_step = 2.0 * half / (kKeyLocationSurfacePosts - 1);
    plan.terrain.surface.size = kKeyLocationSurfacePosts;
    plan.terrain.surface.step_m = round_to(surface_step, 4);
    for (int j = 0; j < kKeyLocationSurfacePosts; ++j) {
        std::vector<double> row;
        const double z = -half + j * surface_step;
        for (int i = 0; i < kKeyLocationSurfacePosts; ++i) {
            const double x = -half + i * surface_step;
            row.push_back(round_to(key_location_shaped_height(shapes, x, z, base(x, z), centre_base), 4));
        }
        plan.terrain.surface.heights_m.push_back(std::move(row));
    }

    plan.location_id = site.id;
    plan.kind = site.kind;
    plan.family = site.family;
    plan.tier = site.tier;
    plan.kit_id = kit.id;
    plan.complete = !plan.plots.empty();
    plan.arrangement = kit.arrangement;
    plan.origin_direction = point;
    plan.origin_height_m = site.height_m;
    plan.origin_node = site.node;
    plan.bounds_width_m = round_to(extent * 2.0, 2);
    plan.bounds_depth_m = round_to(extent * 2.0, 2);
    plan.slope_bearing_degrees = round_to(bearing, 2);
    plan.stat_plots = static_cast<int>(plan.plots.size());
    std::set<std::string_view> distinct;
    for (const KeyLocationPlot& plot : plan.plots) distinct.insert(plot.building_id);
    plan.stat_parts = static_cast<int>(distinct.size());
    plan.stat_shaping = static_cast<int>(shapes.size());
    return plan;
}

}  // namespace

KeyLocationPlanBlock generate_key_location_plans(const KeyLocationRequest& request,
                                                 const KeyLocationBlock& block) {
    KeyLocationPlanBlock plans;
    plans.catalogue_revision = key_location_exteriors_revision();
    plans.part_count = static_cast<int>(key_location_parts().size());
    plans.kit_count = static_cast<int>(key_location_archetype_kits().size() +
                                       key_location_family_kits().size());
    plans.method = key_location_plans_method();
    plans.limits = key_location_plans_limits();
    if (!block.ok) {
        plans.error = "key locations did not produce an ok block";
        return plans;
    }
    try {
        const Grid* height = nullptr;
        if (request.layers != nullptr) {
            const auto found = request.layers->find("height");
            if (found != request.layers->end()) height = &found->second;
        }
        if (height == nullptr || height->empty()) {
            throw std::runtime_error("key location plans need the height layer");
        }
        const int n = request.n > 0 ? request.n : height->size();
        if (!(request.radius_m > 0.0)) {
            throw std::runtime_error("key location plans need the effective globe radius in metres");
        }
        int total = 0;
        for (const KeyLocationSite& site : block.sites) {  // BLOCK ORDER
            const KeyLocationKit* kit = key_location_kit_for(site.kind, site.family);
            if (kit == nullptr) {
                throw std::runtime_error("no exterior kit for " + site.kind + " and no family kit for " +
                                         site.family);
            }
            const std::vector<RuntimeEntry> entries = weathered_kit(*kit, site.state);
            Rng draw = Rng::child(request.seed, "keyloc-exterior-" + site.id);
            KeyLocationSitePlan plan =
                plan_for(site, *kit, entries, height, n, request.radius_m, draw);
            total += plan.stat_plots;
            plans.plans.push_back(std::move(plan));
        }
        plans.summary_plans = static_cast<int>(plans.plans.size());
        plans.summary_plots = total;
        plans.summary_plots_per_plan =
            plans.plans.empty() ? 0.0
                                : round_to(static_cast<double>(total) /
                                               static_cast<double>(plans.plans.size()), 2);
        plans.ok = true;
    } catch (const std::exception& exc) {
        plans.ok = false;
        plans.error = exc.what();
        plans.plans.clear();
    }
    return plans;
}

}  // namespace fwg
