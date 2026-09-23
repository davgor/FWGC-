#include "fwg/nomads.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>

#include "fwg/random.hpp"
#include "fwg/routing.hpp"

namespace fwg {
namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

// A decimal round to six places, which is what the reference's `round(x, 6)` is. Applied
// everywhere it is, because two of these values feed comparisons downstream rather than
// only a display.
//
// The reference rounds HALF TO EVEN and this rounds half away from zero. A deliberate
// divergence under a clean rewrite, taken once here rather than at ten call sites.
double round6(double v) { return std::round(v * 1e6) / 1e6; }

// Python's `int(x)` truncates toward zero; `round(x)` is banker's. Both appear and they
// are not the same operation, so they are named apart rather than written inline.
long long truncate_to_int(double v) { return static_cast<long long>(v); }
long long round_half_away(double v) { return std::llround(v); }

std::string uid_for(int age, int node) {
    return "nomad-" + std::to_string(age) + "-" + std::to_string(node);
}

// The reference's `_where`: a record's unit direction, from whatever it carries. Sites
// and claims carry a direction; hamlets and religion shrines carry only a node.
template <typename Record>
std::optional<Vec3> where_of(const Record& record, const SphereGrid& grid) {
    if (record.direction) return record.direction;
    if (record.x >= 0 && record.z >= 0) return direction(record.x, record.z, grid.n);
    if (record.node >= 0 && static_cast<std::size_t>(record.node) < grid.node_count()) {
        return direction(grid.point_x[static_cast<std::size_t>(record.node)],
                         grid.point_z[static_cast<std::size_t>(record.node)], grid.n);
    }
    return std::nullopt;
}

// `settled_directions` resolves by a DIFFERENT, two-arm chain and RAISES on a record
// carrying neither -- there is no node arm. Reusing `where_of` here would resolve records
// the reference crashes on, which silently moves the clearance ramp on every cell.
Vec3 settled_where(const NomadSite& site, int n) {
    if (site.direction) return *site.direction;
    if (site.x >= 0 && site.z >= 0) return direction(site.x, site.z, n);
    throw std::invalid_argument("a settled record needs a direction or a grid position: " + site.uid);
}

void push_place(std::vector<NomadGround::Place>& places, const std::optional<Vec3>& where,
                std::string key, int index) {
    if (!where) return;
    places.push_back({*where, std::move(key), index});
}

// ---------------------------------------------------------------------------
// Camp rest, its ceiling, and raid weight. Three small tables the ALGORITHM owns.
// ---------------------------------------------------------------------------

struct CampRest {
    NomadCampKind kind;
    double share;
    int ceiling;      // days
    bool has_ceiling;
};

constexpr CampRest kCampRest[] = {
    {NomadCampKind::Winter, 5.0, 0, false},
    {NomadCampKind::Summer, 4.0, 0, false},
    {NomadCampKind::Base, 3.0, 0, false},
    {NomadCampKind::Station, 1.0, 6, true},
    {NomadCampKind::Lair, 5.0, 0, false},
    {NomadCampKind::Shrine, 2.0, 21, true},
    {NomadCampKind::Terminal, 0.35, 3, true},
    // Inherited from the beast-movement sibling. No nomad builder produces one, so this
    // row is unreachable ground kept for schema compatibility.
    {NomadCampKind::Calving, 3.0, 0, false},
};

const CampRest& camp_rest_row(NomadCampKind kind) {
    for (const CampRest& row : kCampRest) {
        if (row.kind == kind) return row;
    }
    return kCampRest[0];
}

// ---------------------------------------------------------------------------
// The six gates. Each returns nullopt for INELIGIBLE, never a zero score: a
// classification whose preconditions fail does not enter the draw at all.
// ---------------------------------------------------------------------------

std::optional<NomadVerdict> gate_survivors(const NomadHabitatCell& cell, const NomadGround& g,
                                           const NomadGate& rule, const NomadRequest& request) {
    const NomadNearest ruin = nomad_nearest(cell.direction, g.fresh_ruins, g.radius);
    if (ruin.at < 0 || ruin.distance_m > rule.ruin_reach_spacings * g.spacing) return std::nullopt;
    const NomadNearest refuge = nomad_nearest(cell.direction, g.markets, g.radius);
    if (refuge.at < 0 || refuge.distance_m > rule.refuge_reach_spacings * g.spacing) return std::nullopt;

    const NomadRuin& fell = request.ruins[static_cast<std::size_t>(g.fresh_ruins[static_cast<std::size_t>(ruin.at)].index)];
    const NomadSite& safe = request.sites[static_cast<std::size_t>(g.markets[static_cast<std::size_t>(refuge.at)].index)];
    // A missing legacy defaults to 1.0, giving a scar of 0.25. The 4.0 is the ley
    // ceiling, reused because a ruin's legacy intensity lives on the same scale.
    const double scar = clamp01(fell.legacy_intensity.value_or(1.0) / 4.0);

    NomadVerdict out;
    out.score = clamp01(scar / (1.0 + refuge.distance_m / g.spacing));
    out.basis.owner = NomadClass::Survivors;
    out.basis.ruin_uid = fell.uid;
    out.basis.ruin_distance_m = ruin.distance_m;
    out.basis.refuge_uid = safe.uid;
    out.basis.refuge_distance_m = refuge.distance_m;
    return out;
}

std::optional<NomadVerdict> gate_deserters(const NomadHabitatCell& cell, const NomadGround& g,
                                           const NomadGate& rule) {
    const NomadNearest war = nomad_nearest(cell.direction, g.wars, g.radius);
    if (war.at < 0 || war.distance_m > rule.war_reach_spacings * g.spacing) return std::nullopt;
    const double rough = nomad_ruggedness(cell);
    // Compared UNCLAMPED against the floor and multiplied in CLAMPED, which are different
    // numbers above 1.0 -- ruggedness runs to 1.35.
    if (rough < rule.min_ruggedness) return std::nullopt;

    const NomadGround::WarSite& site = g.wars_of[static_cast<std::size_t>(g.wars[static_cast<std::size_t>(war.at)].index)];
    NomadVerdict out;
    // Two or more recorded engagements saturate the term at 1.0.
    out.score = clamp01(rough) * clamp01(0.6 + 0.2 * static_cast<double>(site.engagements));
    out.basis.owner = NomadClass::Deserters;
    out.basis.war_site_uid = site.uid;
    out.basis.war_distance_m = war.distance_m;
    out.basis.ruggedness = rough;
    out.basis.engagements = site.engagements;
    return out;
}

// The world's own religion block, never an authored pantheon: that file does not carry
// the four hidden gods, so a blood cult -- the exact case a villain of the blood god
// creates -- would fail to find its patron. FIRST MATCH IN LIST ORDER WINS.
std::string god_for_school(const NomadRequest& request, const std::string& school) {
    if (school.empty()) return {};
    for (const NomadGod& god : request.gods) {
        for (const std::string& name : god.schools) {
            if (name == school) return god.id;
        }
    }
    return {};
}

std::optional<NomadVerdict> gate_cultists(const NomadHabitatCell& cell, const NomadGround& g,
                                          const NomadGate& rule, const NomadRequest& request) {
    const NomadNearest ley = nomad_nearest(cell.direction, g.ley, g.radius);
    const NomadNearest shrine = nomad_nearest(cell.direction, g.shrines, g.radius);
    const NomadNearest claim = nomad_nearest(cell.direction, g.claims, g.radius);

    std::string school;
    double strength = 0.0;
    NomadBasis basis;
    basis.owner = NomadClass::Cultists;

    const NomadLeyNode* node = ley.at < 0 ? nullptr
        : &request.ley[static_cast<std::size_t>(g.ley[static_cast<std::size_t>(ley.at)].index)];

    // THE BRANCHES ARE EXCLUSIVE. A ley node in reach but BELOW intensity blocks nothing
    // -- the whole condition failed, so the chain falls through to the shrine branch. But
    // a ley node in reach and above intensity whose school has no bound god returns
    // ineligible outright; it does NOT fall back.
    if (node != nullptr && node->intensity >= rule.min_ley_intensity &&
        ley.distance_m <= rule.ley_reach_spacings * g.spacing) {
        school = node->school;
        strength = node->intensity;
        basis.ley_node_id = node->id;
        basis.ley_distance_m = ley.distance_m;
        basis.ley_intensity = strength;
    } else if (shrine.at >= 0 && shrine.distance_m <= rule.shrine_reach_spacings * g.spacing) {
        const NomadShrine& holy = request.shrines[static_cast<std::size_t>(g.shrines[static_cast<std::size_t>(shrine.at)].index)];
        basis.shrine_id = holy.id;
        basis.shrine_distance_m = shrine.distance_m;
        basis.god_id = holy.god_id;
        for (const NomadGod& record : request.gods) {
            if (record.id == holy.god_id && !record.schools.empty()) {
                school = record.schools.front();
                break;
            }
        }
        strength = 1.0;
    } else if (claim.at >= 0 && claim.distance_m <= rule.claim_reach_spacings * g.spacing) {
        const NomadClaim& held = request.claims[static_cast<std::size_t>(g.claims[static_cast<std::size_t>(claim.at)].index)];
        school = held.villain_school;
        strength = std::max(1.0, held.villain_tier) * held.influence;
        basis.claim_id = held.id;
        basis.villain_uid = held.villain_uid;
        basis.claim_distance_m = claim.distance_m;
    }

    if (school.empty()) return std::nullopt;
    const std::string god = god_for_school(request, school);
    if (god.empty()) return std::nullopt;
    basis.school = school;
    // KNOWN DEFECT, CARRIED. In the shrine branch this OVERWRITES the shrine's own god
    // with whatever `_god_for_school` found first for that school. The school came from
    // the shrine's god so they usually agree, but if another god shares the affinity and
    // sits earlier in the list, the band is recorded worshipping a god other than the one
    // whose shrine classified it. Preserved because it is the reference behaviour.
    basis.god_id = god;

    NomadVerdict out;
    // 4.0 is the intensity ceiling, so a node at the ceiling scores 1.0 and a tier-four
    // living villain saturates too.
    out.score = clamp01(strength / 4.0);
    out.basis = std::move(basis);
    return out;
}

std::optional<NomadVerdict> gate_merchants(const NomadHabitatCell& cell, const NomadGround& g,
                                           const NomadGate& rule, const NomadRequest& request) {
    const NomadNearest road = nomad_nearest(cell.direction, g.roads, g.radius);
    if (road.at < 0 || road.distance_m > rule.road_reach_spacings * g.spacing) return std::nullopt;

    const double reach = rule.market_reach_spacings * g.spacing;
    // (distance, uid, site index). Sorted on the first two ONLY: the reference sorts
    // four-tuples and would try to compare two site dicts on an exact tie of both, which
    // raises. Unreachable because uids are unique, and refused here rather than relied on.
    std::vector<std::tuple<double, std::string, int>> found;
    for (const NomadGround::Place& place : g.markets) {
        const NomadSite& site = request.sites[static_cast<std::size_t>(place.index)];
        const double people = nomad_population_of(site);
        if (people < rule.min_market_population) continue;
        const double span = nomad_distance(cell.direction, place.where, g.radius);
        if (span <= reach) found.emplace_back(span, place.key, place.index);
    }
    if (static_cast<int>(found.size()) < rule.min_markets) return std::nullopt;
    std::sort(found.begin(), found.end());

    double weight = 0.0;
    const std::size_t taken = std::min<std::size_t>(4, found.size());
    NomadVerdict out;
    out.basis.owner = NomadClass::Merchants;
    for (std::size_t i = 0; i < taken; ++i) {
        const auto& [span, uid, index] = found[i];
        const double people = nomad_population_of(request.sites[static_cast<std::size_t>(index)]);
        // Population 100000 scores 1.0; a 2000-person market scores 0.661.
        weight += clamp01(std::log10(1.0 + people) / 5.0) / (1.0 + span / g.spacing);
        out.basis.markets.push_back(uid);
    }
    out.score = clamp01(weight / static_cast<double>(taken));
    out.basis.market_count = static_cast<int>(found.size());
    out.basis.nearest_market_m = std::get<0>(found.front());
    out.basis.road_distance_m = road.distance_m;
    out.basis.road_node = g.roads[static_cast<std::size_t>(road.at)].index;
    return out;
}

std::optional<NomadVerdict> gate_bandits(const NomadHabitatCell& cell, const NomadGround& g,
                                         const NomadGate& rule) {
    const double rough = nomad_ruggedness(cell);
    if (rough < rule.min_ruggedness) return std::nullopt;
    const NomadNearest prize = nomad_nearest(cell.direction, g.wealth, g.radius);
    if (prize.at < 0 || prize.distance_m > rule.strike_reach_spacings * g.spacing) return std::nullopt;
    const NomadNearest seat = nomad_nearest(cell.direction, g.markets, g.radius);
    // THE THIRD CONDITION IS AN ABSENCE, which is what makes this a bandit gate rather
    // than a "rough ground near a town" gate. Note it does NOT test for a market at all:
    // with no markets anywhere `control` is infinite, the comparison is false, and the
    // gate passes with a saturated authority term. A world with no cities makes
    // maximally-unpoliced bandits, which is arguably correct.
    if (seat.distance_m < rule.control_clear_spacings * g.spacing) return std::nullopt;

    const NomadSite& target = *g.wealth_of[static_cast<std::size_t>(g.wealth[static_cast<std::size_t>(prize.at)].index)];
    const double worth = clamp01(std::log10(1.0 + nomad_population_of(target)) / 5.0);
    if (worth <= 0.0) return std::nullopt;

    NomadVerdict out;
    // Authority weakness saturates at three spacings from the nearest seat.
    out.score = clamp01(rough) * worth * clamp01(seat.distance_m / g.spacing / 3.0);
    out.basis.owner = NomadClass::Bandits;
    out.basis.prize_uid = target.uid.empty() ? target.id : target.uid;
    out.basis.prize_distance_m = prize.distance_m;
    out.basis.ruggedness = rough;
    out.basis.nearest_seat_m = seat.distance_m;
    return out;
}

std::optional<NomadVerdict> gate_wanderers(const NomadHabitatCell& cell, double swing,
                                           const NomadGate& rule) {
    const double forage = std::max(cell.food_potential, cell.natural_food_potential);
    if (forage < rule.min_forage) return std::nullopt;
    if (swing < rule.min_seasonal_swing) return std::nullopt;
    const double openness = clamp01(1.0 - cell.slope_deg / 30.0);

    NomadVerdict out;
    // Forage saturates at 0.25, well above the 0.07 gate floor.
    out.score = clamp01(forage / 0.25) * clamp01(0.5 + 0.5 * swing) * clamp01(0.4 + 0.6 * openness);
    out.basis.owner = NomadClass::Wanderers;
    out.basis.forage = forage;
    out.basis.seasonal_swing = swing;
    out.basis.openness = openness;
    return out;
}

// ---------------------------------------------------------------------------
// Placement.
// ---------------------------------------------------------------------------

// Cumulative walk over the eligible classifications; one uniform draw decides.
//
// The comparison is `<=`, and the fallback is the LAST eligible entry, which is what the
// walk was about to reach when float drift puts the draw a hair past the final cursor.
std::pair<NomadClass, double> pick(Rng& rng, const std::vector<std::pair<NomadClass, double>>& shares,
                                   double total) {
    const double draw = rng.random() * total;
    double cursor = 0.0;
    for (const auto& [name, share] : shares) {
        cursor += share;
        if (draw <= cursor) return {name, draw};
    }
    return {shares.back().first, draw};
}

NomadBand make_band(std::string uid, NomadClass value, const NomadHabitatCell& cell, int size,
                    int age, NomadBasis basis) {
    const NomadClassPolicy& rule = nomad_class_policy(value);
    NomadBand band;
    band.uid = std::move(uid);
    band.classification = value;
    band.origin.kind = NomadOriginKind::World;
    band.origin.age = age;
    band.god_id = basis.god_id;
    band.school = basis.school;
    band.node = cell.node;
    band.x = cell.x;
    band.z = cell.z;
    band.direction = cell.direction;
    band.biome = cell.biome;
    band.size = size;
    band.speed_m_per_day = rule.speed_m_per_day;
    band.column_length_m = round6(static_cast<double>(size) * rule.column_length_per_head_m);
    band.disposition = rule.disposition;
    band.seeks = rule.seeks;
    band.carries = rule.carries;
    NomadCamp camp;
    camp.id = band.uid + "-camp-0";
    camp.node = cell.node;
    camp.direction = cell.direction;
    // The camp a band STARTS from, before the route pass gives it a seasonal set.
    // Changing a value here changes that camp's rest share and so moves the schedule.
    switch (value) {
        case NomadClass::Survivors: camp.kind = NomadCampKind::Base; break;
        case NomadClass::Deserters: camp.kind = NomadCampKind::Lair; break;
        case NomadClass::Cultists: camp.kind = NomadCampKind::Shrine; break;
        case NomadClass::Merchants: camp.kind = NomadCampKind::Station; break;
        case NomadClass::Bandits: camp.kind = NomadCampKind::Lair; break;
        case NomadClass::Wanderers: camp.kind = NomadCampKind::Base; break;
    }
    band.camps.push_back(std::move(camp));
    band.basis = std::move(basis);
    return band;
}

// ---------------------------------------------------------------------------
// Routing.
// ---------------------------------------------------------------------------

// The walking cost, DELIBERATELY NOT the road cost. That one refuses a river without a
// bridge and caps grade at the road limit, both of which describe an engineered route
// rather than a walked one. A band fords, and takes ground a road would not. Water is
// still impassable -- these are not boats -- and the forage term makes a herd prefer the
// green way round.
class WalkCost {
public:
    WalkCost(const SphereGrid& grid, const std::vector<NomadHabitatCell>& cells, double road_max_grade)
        : grid_(grid), ceiling_(road_max_grade * kNomadGradeRelief) {
        water_.reserve(cells.size());
        height_.reserve(cells.size());
        forage_.reserve(cells.size());
        for (const NomadHabitatCell& cell : cells) {
            water_.push_back(cell.medium != NomadMedium::Land ? 1 : 0);
            height_.push_back(cell.height_m);
            forage_.push_back(clamp01(std::max(cell.food_potential, cell.natural_food_potential)));
        }
    }

    std::optional<double> operator()(int i, int j, double d) const {
        const auto ui = static_cast<std::size_t>(i);
        const auto uj = static_cast<std::size_t>(j);
        if (water_[ui] || water_[uj] || d <= 0.0) return std::nullopt;
        const int xi = grid_.point_x[ui];
        const int zi = grid_.point_z[ui];
        const int xj = grid_.point_x[uj];
        const int zj = grid_.point_z[uj];
        // A diagonal cannot slip between two water-filled corner cells. Skipped at either
        // pole row, which has no meaningful corner neighbours.
        if (xi != xj && zi != zj && zi != 0 && zi != grid_.n - 1 && zj != 0 && zj != grid_.n - 1) {
            if (water_[static_cast<std::size_t>(grid_.node_index(xi, zj))] ||
                water_[static_cast<std::size_t>(grid_.node_index(xj, zi))]) {
                return std::nullopt;
            }
        }
        const double grade = std::abs(height_[uj] - height_[ui]) / d;
        if (grade > ceiling_) return std::nullopt;
        // The 8 is the grade penalty -- roads use 12 -- and the 0.25 caps the forage
        // discount, so `green` is in [0.75, 1.0].
        const double green = 1.0 - 0.25 * (forage_[ui] + forage_[uj]) / 2.0;
        return d * (1.0 + 8.0 * grade * grade) * green;
    }

private:
    const SphereGrid& grid_;
    double ceiling_;
    std::vector<char> water_;
    std::vector<double> height_;
    std::vector<double> forage_;
};

// `end == start` returns a ONE-element path, not an empty one, which is why the fission
// join guards on `size() < 2` rather than on emptiness.
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

using PathPtr = std::shared_ptr<const PathResult>;

// Everything the builders share, resolved once per route pass.
struct RouteContext {
    const NomadRequest* request = nullptr;
    const SphereGrid* grid = nullptr;
    const std::vector<NomadHabitatCell>* cells = nullptr;
    double spacing = 0.0;
    double radius = 1.0;
    WalkCost* cost = nullptr;
    std::unordered_map<int, PathPtr>* cache = nullptr;
    // The ley list WITHOUT intensity: the route pass does not re-read it.
    const std::vector<NomadLeyNode>* ley = nullptr;
    std::vector<int>* ley_node = nullptr;  // memoised nearest grid node per ley entry

    PathPtr paths(int start) const {
        auto found = cache->find(start);
        if (found != cache->end()) return found->second;
        auto result = std::make_shared<const PathResult>(shortest_paths(*grid, start, *cost));
        cache->emplace(start, result);
        return result;
    }
};

// An exact hit and a full scan agree, because an exact match is distance zero and ties
// break on the lowest index -- so the scan alone is the safe implementation and a
// float-keyed map is not needed.
int nearest_grid_node(const Vec3& where, const std::vector<NomadHabitatCell>& cells, double radius) {
    int best = -1;
    double best_span = 0.0;
    for (const NomadHabitatCell& cell : cells) {
        const double span = nomad_distance(where, cell.direction, radius);
        if (best < 0 || span < best_span) {
            best = cell.node;
            best_span = span;
        }
    }
    return best;
}

struct Built {
    std::vector<NomadCamp> camps;
    std::vector<std::tuple<int, int, NomadBranch>> order;
    PathPtr from_start;
};

NomadCamp make_camp(const NomadBand& band, std::size_t index, int node,
                    const std::vector<NomadHabitatCell>& cells, NomadCampKind kind) {
    NomadCamp camp;
    camp.id = band.uid + "-camp-" + std::to_string(index);
    camp.node = node;
    camp.direction = cells[static_cast<std::size_t>(node)].direction;
    camp.kind = kind;
    return camp;
}

// Highest score, then the LOWEST node index. Strict `>` on (score, -node).
template <typename Score>
int best_of(const std::vector<int>& candidates, Score&& score) {
    int best = -1;
    double best_score = 0.0;
    for (const int node : candidates) {
        const double value = score(node);
        if (best < 0 || value > best_score || (value == best_score && node < best)) {
            best = node;
            best_score = value;
        }
    }
    return best;
}

std::vector<int> within(const std::vector<double>& distances, double limit) {
    std::vector<int> out;
    for (std::size_t i = 0; i < distances.size(); ++i) {
        if (distances[i] <= limit && std::isfinite(distances[i])) out.push_back(static_cast<int>(i));
    }
    return out;
}

// BUILDER A -- summer pasture and winter shelter, the two grounds a herding round needs.
//
// Summer wants HEIGHT and OPENNESS: ground that cannot be used in the cold months, so it
// is not competing with anyone. Winter wants the opposite and one thing more -- snow
// settles on the flat, so the camps that work are tucked against relief where sun and
// wind strip the pasture clear. That is what the shelter term on |tpi| buys.
std::optional<Built> seasonal_camps(const NomadBand& band, const RouteContext& ctx) {
    const std::vector<NomadHabitatCell>& cells = *ctx.cells;
    const NomadGate& gate = nomad_class_policy(band.classification).gate;
    const double reach_s = gate.summer_reach_spacings * ctx.spacing;
    const double reach_w = gate.winter_reach_spacings * ctx.spacing;
    const PathPtr paths = ctx.paths(band.node);
    const double base = cells[static_cast<std::size_t>(band.node)].height_m;
    // 400 m is the floor that keeps a sea-level band from dividing by a tiny number.
    const double span = std::max(1.0, std::abs(base) + 400.0);

    const auto summer = [&](int node) {
        const NomadHabitatCell& c = cells[static_cast<std::size_t>(node)];
        const double openness = clamp01(1.0 - c.slope_deg / 30.0);
        const double lift = clamp01((c.height_m - base) / span);
        const double forage = std::max(c.food_potential, c.natural_food_potential);
        return forage * (1.0 + lift) * (0.4 + openness);
    };
    const auto winter = [&](int node) {
        const NomadHabitatCell& c = cells[static_cast<std::size_t>(node)];
        const double shelter = clamp01(std::abs(c.tpi_m) / 30.0);
        const double drop = clamp01((base - c.height_m) / span);
        const double warmth = clamp01((c.temperature_c + 20.0) / 45.0);
        const double forage = std::max(c.food_potential, c.natural_food_potential);
        return (0.3 + forage) * (1.0 + drop) * (0.4 + shelter) * (0.5 + warmth);
    };

    // THE START NODE IS THE BASE AND CANNOT ALSO BE A SEASONAL CAMP. If it were, the
    // base_limb leg would collapse to zero length, be dropped by the leg tracer, and
    // leave the base camp with no leg referencing it and therefore no day window at all.
    const auto drop_start = [&](std::vector<int> nodes) {
        nodes.erase(std::remove(nodes.begin(), nodes.end(), band.node), nodes.end());
        return nodes;
    };
    const int s = best_of(drop_start(within(paths->distance, reach_s)), summer);
    const int w = best_of(drop_start(within(paths->distance, reach_w)), winter);
    // A round with one ground is not a round.
    if (s < 0 || w < 0 || s == w) return std::nullopt;

    Built built;
    built.camps = {make_camp(band, 0, band.node, cells, NomadCampKind::Base),
                   make_camp(band, 1, s, cells, NomadCampKind::Summer),
                   make_camp(band, 2, w, cells, NomadCampKind::Winter)};
    built.order = {{0, 1, NomadBranch::BaseLimb}, {1, 2, NomadBranch::Trunk}, {2, 1, NomadBranch::Trunk}};
    built.from_start = paths;
    return built;
}

// BUILDER B -- a closed chain between markets, last market back to the start station.
//
// NOTE the reach here is the DIJKSTRA COST, not the great-circle distance the gate used.
// The two share a constant and not a metric, so a band can pass the gate on two markets
// and strand here because neither is reachable inside the same number.
std::optional<Built> market_stations(const NomadBand& band, const RouteContext& ctx) {
    const std::vector<NomadHabitatCell>& cells = *ctx.cells;
    const NomadGate& gate = nomad_class_policy(band.classification).gate;
    const double reach = gate.market_reach_spacings * ctx.spacing;
    const double floor = nomad_class_policy(NomadClass::Merchants).gate.min_market_population;
    const PathPtr paths = ctx.paths(band.node);

    std::vector<std::tuple<double, std::string, int>> found;
    for (const NomadSite& site : ctx.request->sites) {
        if (site.node < 0 || static_cast<std::size_t>(site.node) >= cells.size()) continue;
        const double cost = paths->distance[static_cast<std::size_t>(site.node)];
        if (nomad_population_of(site) < floor || !std::isfinite(cost) || cost > reach) continue;
        found.emplace_back(cost, site.uid, site.node);
    }
    if (found.size() < 2) return std::nullopt;
    std::sort(found.begin(), found.end());

    Built built;
    built.camps.push_back(make_camp(band, 0, band.node, cells, NomadCampKind::Station));
    const std::size_t taken = std::min<std::size_t>(4, found.size());
    for (std::size_t i = 0; i < taken; ++i) {
        built.camps.push_back(make_camp(band, i + 1, std::get<2>(found[i]), cells, NomadCampKind::Station));
    }
    for (std::size_t i = 0; i + 1 < built.camps.size(); ++i) {
        built.order.emplace_back(static_cast<int>(i), static_cast<int>(i + 1), NomadBranch::Trunk);
    }
    built.order.emplace_back(static_cast<int>(built.camps.size()) - 1, 0, NomadBranch::Trunk);
    built.from_start = paths;
    return built;
}

// BUILDER C -- a closed loop over one god's sacred ground, nearest first, at most three.
//
// Cultists snap to the world's EXISTING ley networks rather than inventing geometry,
// because pilgrimage is a fixed repeated circuit widely read as tracing ley lines.
//
// The reach uses the CLAIM key regardless of which branch of the gate classified the
// band -- a ley-derived cult classified within 0.35 spacings gets a circuit reach keyed
// off a constant it never met. Carried verbatim and flagged rather than "fixed".
std::optional<Built> sacred_circuit(const NomadBand& band, const RouteContext& ctx) {
    const std::vector<NomadHabitatCell>& cells = *ctx.cells;
    const NomadGate& gate = nomad_class_policy(band.classification).gate;
    const double reach = gate.claim_reach_spacings * ctx.spacing * 2.5;
    const PathPtr paths = ctx.paths(band.node);

    std::vector<std::tuple<double, std::string, int>> nodes;
    for (std::size_t i = 0; i < ctx.ley->size(); ++i) {
        const NomadLeyNode& entry = (*ctx.ley)[i];
        if (entry.school != band.school) continue;
        int& memo = (*ctx.ley_node)[i];
        if (memo < -1) memo = nearest_grid_node(entry.direction, cells, ctx.radius);
        if (memo < 0) continue;
        const double cost = paths->distance[static_cast<std::size_t>(memo)];
        if (!std::isfinite(cost) || cost > reach) continue;
        nodes.emplace_back(cost, entry.id, memo);
    }
    std::sort(nodes.begin(), nodes.end());

    std::vector<int> picked;
    std::vector<int> seen{band.node};
    for (const auto& [cost, id, node] : nodes) {
        if (std::find(seen.begin(), seen.end(), node) != seen.end()) continue;
        seen.push_back(node);
        picked.push_back(node);
        if (picked.size() == 3) break;
    }
    if (picked.empty()) return std::nullopt;

    Built built;
    built.camps.push_back(make_camp(band, 0, band.node, cells, NomadCampKind::Shrine));
    for (std::size_t i = 0; i < picked.size(); ++i) {
        built.camps.push_back(make_camp(band, i + 1, picked[i], cells, NomadCampKind::Shrine));
    }
    for (std::size_t i = 0; i + 1 < built.camps.size(); ++i) {
        built.order.emplace_back(static_cast<int>(i), static_cast<int>(i + 1), NomadBranch::Trunk);
    }
    built.order.emplace_back(static_cast<int>(built.camps.size()) - 1, 0, NomadBranch::Trunk);
    built.from_start = paths;
    return built;
}

// BUILDER D -- a lair with out-and-back spurs onto what is worth taking.
//
// A raiding band does not migrate. The spur returning to the lair IS the whole shape:
// strike, vanish, and be somewhere defensible before anyone organises.
std::optional<Built> raid_orbit(const NomadBand& band, const RouteContext& ctx) {
    const std::vector<NomadHabitatCell>& cells = *ctx.cells;
    const NomadGate& gate = nomad_class_policy(band.classification).gate;
    const double reach = gate.strike_reach_spacings * ctx.spacing;
    const PathPtr paths = ctx.paths(band.node);

    // Richest first, then nearest, then the lowest node. `population_of` returns a food
    // yield for a hamlet and a head count for a city, so the two are not commensurable
    // and a high-yield hamlet can outrank a small city.
    std::vector<std::tuple<double, double, int>> prizes;
    const auto consider = [&](const std::vector<NomadSite>& places) {
        for (const NomadSite& site : places) {
            if (site.node < 0 || static_cast<std::size_t>(site.node) >= cells.size()) continue;
            if (site.node == band.node) continue;
            const double cost = paths->distance[static_cast<std::size_t>(site.node)];
            if (!std::isfinite(cost) || cost > reach) continue;
            prizes.emplace_back(-nomad_population_of(site), cost, site.node);
        }
    };
    consider(ctx.request->sites);
    consider(ctx.request->hamlets);
    if (prizes.empty()) return std::nullopt;
    std::sort(prizes.begin(), prizes.end());

    Built built;
    built.camps.push_back(make_camp(band, 0, band.node, cells, NomadCampKind::Lair));
    const std::size_t taken = std::min<std::size_t>(2, prizes.size());
    for (std::size_t i = 0; i < taken; ++i) {
        built.camps.push_back(make_camp(band, i + 1, std::get<2>(prizes[i]), cells, NomadCampKind::Terminal));
        built.order.emplace_back(0, static_cast<int>(i) + 1, NomadBranch::Spur);
        built.order.emplace_back(static_cast<int>(i) + 1, 0, NomadBranch::Spur);
    }
    built.from_start = paths;
    return built;
}

// Unit vector pointing away from the war site the band fled, or none.
//
// Reads the record's RAW direction as the reference does, not the wider `_where` chain,
// and returns on the FIRST uid match even when that record carries no direction. Scans
// ruins then sites, in that concatenation order.
std::optional<Vec3> war_direction(const NomadBand& band, const NomadRequest& request) {
    const auto away_from = [&](const std::optional<Vec3>& there) -> std::optional<Vec3> {
        if (!there) return std::nullopt;
        const Vec3 away = band.direction - *there;
        const double norm = std::sqrt(dot(away, away));
        if (norm <= 1e-9) return std::nullopt;
        return Vec3{away.x / norm, away.y / norm, away.z / norm};
    };
    for (const NomadRuin& ruin : request.ruins) {
        if (ruin.uid == band.basis.war_site_uid) return away_from(ruin.direction);
    }
    for (const NomadSite& site : request.sites) {
        if (site.uid == band.basis.war_site_uid) return away_from(site.direction);
    }
    return std::nullopt;
}

// BUILDER E -- one terminating move. Deserters run from a decided war into cover;
// survivors run from a ruin toward the nearest place still standing. Neither is a circuit.
std::optional<Built> flight(const NomadBand& band, const RouteContext& ctx, bool survivors) {
    const std::vector<NomadHabitatCell>& cells = *ctx.cells;
    const NomadGate& gate = nomad_class_policy(band.classification).gate;
    const PathPtr paths = ctx.paths(band.node);

    Built built;
    if (survivors) {
        const double reach = gate.refuge_reach_spacings * ctx.spacing;
        // THE LAST match wins: the reference's loop does not break. Uids are unique in
        // practice, so this is first-match in every real world -- carried anyway.
        int target = -1;
        for (const NomadSite& site : ctx.request->sites) {
            if (site.node < 0 || site.uid != band.basis.refuge_uid) continue;
            target = site.node;
        }
        if (target < 0) return std::nullopt;
        const double cost = paths->distance[static_cast<std::size_t>(target)];
        // The refuge must be Dijkstra-reachable inside the reach, which the gate, using a
        // great-circle distance, did not test.
        if (!std::isfinite(cost) || cost > reach) return std::nullopt;
        built.camps = {make_camp(band, 0, band.node, cells, NomadCampKind::Base),
                       make_camp(band, 1, target, cells, NomadCampKind::Terminal)};
    } else {
        const double reach = gate.war_reach_spacings * ctx.spacing * 3.0;
        const std::optional<Vec3> away = war_direction(band, *ctx.request);
        const auto cover = [&](int node) {
            if (node == band.node) return -1.0;
            double lead = 0.0;
            // 1.0 directly away from the war, 0.0 directly toward it. The 0.4 floor means
            // cover still counts when the direction is unknown.
            if (away) lead = clamp01((1.0 + dot(*away, cells[static_cast<std::size_t>(node)].direction)) / 2.0);
            return nomad_ruggedness(cells[static_cast<std::size_t>(node)]) * (0.4 + lead);
        };
        const int target = best_of(within(paths->distance, reach), cover);
        if (target < 0 || target == band.node) return std::nullopt;
        built.camps = {make_camp(band, 0, band.node, cells, NomadCampKind::Base),
                       make_camp(band, 1, target, cells, NomadCampKind::Lair)};
    }
    built.order = {{0, 1, NomadBranch::Trunk}};
    built.from_start = paths;
    return built;
}

std::optional<Built> build_route(const NomadBand& band, const RouteContext& ctx) {
    switch (band.classification) {
        case NomadClass::Survivors: return flight(band, ctx, true);
        case NomadClass::Deserters: return flight(band, ctx, false);
        case NomadClass::Cultists: return sacred_circuit(band, ctx);
        case NomadClass::Merchants: return market_stations(band, ctx);
        case NomadClass::Bandits: return raid_orbit(band, ctx);
        case NomadClass::Wanderers: return seasonal_camps(band, ctx);
    }
    return std::nullopt;
}

// One body, two callers: the main loop and the fission child.
//
// A leg whose endpoints share a node is DROPPED silently, and so is one whose trace
// fails. A partial circuit is emitted rather than refused.
std::vector<NomadLeg> legs_for(const NomadBand& band, const std::vector<NomadCamp>& camps,
                               const std::vector<std::tuple<int, int, NomadBranch>>& order,
                               const PathResult& from_start, const RouteContext& ctx) {
    const std::vector<NomadHabitatCell>& cells = *ctx.cells;
    std::vector<NomadLeg> legs;
    for (const auto& [a, b, branch] : order) {
        const NomadCamp& src = camps[static_cast<std::size_t>(a)];
        const NomadCamp& dst = camps[static_cast<std::size_t>(b)];
        if (src.node == dst.node) continue;
        std::vector<int> nodes;
        if (src.node == band.node) nodes = trace(from_start.parent, band.node, dst.node);
        if (nodes.empty()) {
            const PathPtr sub = ctx.paths(src.node);
            nodes = trace(sub->parent, src.node, dst.node);
        }
        if (nodes.empty()) continue;
        // THE GREAT-CIRCLE WALK LENGTH, not the Dijkstra cost: the two differ by the grade
        // and forage factors, and reading one as the other overstates the walk.
        double length = 0.0;
        for (std::size_t i = 0; i + 1 < nodes.size(); ++i) {
            length += nomad_distance(cells[static_cast<std::size_t>(nodes[i])].direction,
                                     cells[static_cast<std::size_t>(nodes[i + 1])].direction, ctx.radius);
        }
        NomadLeg leg;
        leg.from = src.id;
        leg.to = dst.id;
        leg.nodes = std::move(nodes);
        leg.length_m = length;
        leg.branch = branch;
        legs.push_back(std::move(leg));
    }
    return legs;
}

// MERCHANTS ONLY. Cut any leg longer than a day's march, putting a station at the break.
//
// THE CUT CLOSES AT `a`, the last node the day's walk reaches, NOT at `b`, the first node
// past it. Owner ruling 2026-09-21: NO SEGMENT EXCEEDS ONE DAY'S MARCH. Cutting on the
// crossing overshoots by up to one graph step -- 6250 m at size 33, 17.9% of the march --
// and would still break the assertion it is meant to satisfy. The older tail condition
// (`remaining > day_march * 0.5`) was worse than either: it absorbed the remainder
// backwards and a segment could reach 1.5 marches. Measured whole at 1.171.
//
// `run > 0.0` is NOT defensive: at the head of a segment there is no earlier node to fall
// back to, so a single over-long hop is WALKED rather than refused. And `nodes.size() < 3`
// stays because a leg with no intermediate node has nowhere to stand a station.
void subdivide(const NomadBand& band, std::vector<NomadCamp>& camps, std::vector<NomadLeg>& legs,
               const std::vector<NomadHabitatCell>& cells, double radius, double day_march) {
    std::vector<NomadLeg> out;
    for (NomadLeg& leg : legs) {
        if (leg.length_m <= day_march || leg.nodes.size() < 3) {
            out.push_back(std::move(leg));
            continue;
        }
        std::string cut_from = leg.from;
        double run = 0.0;
        std::vector<int> segment{leg.nodes.front()};
        for (std::size_t i = 0; i + 1 < leg.nodes.size(); ++i) {
            const int a = leg.nodes[i];
            const int b = leg.nodes[i + 1];
            const double step = nomad_distance(cells[static_cast<std::size_t>(a)].direction,
                                               cells[static_cast<std::size_t>(b)].direction, radius);
            if (run > 0.0 && run + step > day_march) {
                NomadCamp station = make_camp(band, camps.size(), a, cells, NomadCampKind::Station);
                NomadLeg cut;
                cut.from = cut_from;
                cut.to = station.id;
                cut.nodes = segment;
                cut.length_m = run;
                cut.branch = leg.branch;
                out.push_back(std::move(cut));
                cut_from = station.id;
                camps.push_back(std::move(station));
                segment.assign(1, a);
                run = 0.0;
            }
            run += step;
            segment.push_back(b);
        }
        // The tail always holds at least one step, because the cut closes at `a` and `b`
        // is appended after it -- so the last node is never a cut point.
        NomadLeg tail;
        tail.from = cut_from;
        tail.to = leg.to;
        tail.nodes = std::move(segment);
        tail.length_m = run;
        tail.branch = leg.branch;
        out.push_back(std::move(tail));
    }
    legs = std::move(out);
}

// Walk the circuit against the calendar.
//
// A herding round is anchored so the WINTER camp is occupied at midwinter -- that is the
// camp survival depends on, so it is the one the year is built around. Everything else
// starts at its own midsummer and runs on travel time, which is length over speed.
void assign_days(const NomadBand& band, std::vector<NomadCamp>& camps, std::vector<NomadLeg>& legs,
                 const RouteContext& ctx) {
    const int n = ctx.request->size;
    const std::vector<NomadHabitatCell>& cells = *ctx.cells;
    const double speed = std::max(1.0, band.speed_m_per_day);

    long long total_travel = 0;
    for (const NomadLeg& leg : legs) total_travel += std::max<long long>(1, round_half_away(leg.length_m / speed));

    double weight = 0.0;
    std::vector<double> shares;
    shares.reserve(camps.size());
    for (const NomadCamp& camp : camps) {
        shares.push_back(nomad_camp_rest(camp.kind));
        weight += shares.back();
    }
    // With total_travel at or past the year, `spare` falls back to the camp count and
    // every camp gets one day. The schedule still closes; it just stops being a year.
    const double spare = static_cast<double>(std::max<long long>(static_cast<long long>(camps.size()),
                                                                 kNomadDaysPerYear - total_travel));
    std::unordered_map<std::string, int> rest_of;
    for (std::size_t i = 0; i < camps.size(); ++i) {
        const auto held = static_cast<int>(std::max<long long>(1, truncate_to_int(spare * shares[i] / weight)));
        const std::optional<int> ceiling = nomad_camp_rest_ceiling(camps[i].kind);
        rest_of[camps[i].id] = ceiling ? std::min(held, *ceiling) : held;
    }

    const NomadCamp* winter = nullptr;
    for (const NomadCamp& camp : camps) {
        if (camp.kind == NomadCampKind::Winter) {
            winter = &camp;
            break;
        }
    }
    long long day = winter != nullptr
        ? nomad_midwinter_day(cells[static_cast<std::size_t>(winter->node)].z, n)
        : nomad_midsummer_day(cells[static_cast<std::size_t>(band.node)].z, n);

    std::unordered_map<std::string, NomadCamp*> by_id;
    for (NomadCamp& camp : camps) by_id[camp.id] = &camp;

    // Start the clock at the winter camp so it HOLDS midwinter, then walk forward. The
    // rotation is over POINTERS, so the legs it stamps are the caller's own.
    std::vector<NomadLeg*> order;
    order.reserve(legs.size());
    for (NomadLeg& leg : legs) order.push_back(&leg);
    if (winter != nullptr) {
        std::size_t start_index = 0;
        for (std::size_t k = 0; k < order.size(); ++k) {
            if (order[k]->from == winter->id) {
                start_index = k;
                break;
            }
        }
        std::rotate(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(start_index), order.end());
    }

    const auto rest = [&](const std::string& id) {
        auto found = rest_of.find(id);
        return found == rest_of.end() ? 1 : found->second;
    };

    if (!order.empty()) {
        auto found = by_id.find(order.front()->from);
        if (found != by_id.end()) {
            found->second->arrive_day = static_cast<int>(day % kNomadDaysPerYear);
            found->second->depart_day = static_cast<int>((day + rest(found->first)) % kNomadDaysPerYear);
        }
    }

    for (NomadLeg* leg : order) {
        const long long travel = std::max<long long>(1, round_half_away(leg->length_m / speed));
        const int held = rest(leg->from);
        leg->depart_day = static_cast<int>((day + held) % kNomadDaysPerYear);
        // NOT reduced mod 360: it accumulates, and only the stamps are wrapped.
        day += held + travel;
        leg->arrive_day = static_cast<int>(day % kNomadDaysPerYear);
        auto found = by_id.find(leg->to);
        // FIRST WRITE WINS: a camp reached twice keeps the window of its first arrival.
        if (found != by_id.end() && !found->second->arrive_day) {
            found->second->arrive_day = static_cast<int>(day % kNomadDaysPerYear);
            found->second->depart_day = static_cast<int>((day + rest(leg->to)) % kNomadDaysPerYear);
        }
    }

    // A camp that is only ever a leg's SOURCE -- the base a transhumant group holds while
    // the herd moves -- is never reached by the loop above and would carry no window at
    // all. It is occupied from when the moving limb leaves until the limb returns.
    for (NomadLeg* leg : order) {
        auto found = by_id.find(leg->from);
        if (found == by_id.end() || found->second->arrive_day || !leg->depart_day) continue;
        const int held = rest(leg->from);
        found->second->depart_day = *leg->depart_day;
        found->second->arrive_day = static_cast<int>(((*leg->depart_day - held) % kNomadDaysPerYear +
                                                     kNomadDaysPerYear) % kNomadDaysPerYear);
    }
}

// The camps of the parent's round a daughter segment could take over.
//
// Its own start camp is NOT one of them: a split that leaves the child standing where the
// parent already stands has divided nothing. SUMMER PASTURE FIRST, because high open
// ground in the warm months is what a herd that has outgrown its range competes for.
std::vector<const NomadCamp*> inheritable(const NomadBand& band) {
    std::vector<const NomadCamp*> rest;
    for (const NomadCamp& camp : band.camps) {
        if (camp.node != band.node) rest.push_back(&camp);
    }
    std::sort(rest.begin(), rest.end(), [](const NomadCamp* a, const NomadCamp* b) {
        const bool a_summer = a->kind == NomadCampKind::Summer;
        const bool b_summer = b->kind == NomadCampKind::Summer;
        if (a_summer != b_summer) return a_summer;
        return a->id < b->id;  // the camp id STRING, so -camp-10 sorts before -camp-2
    });
    return rest;
}

}  // namespace

// ---------------------------------------------------------------------------
// Names and small tables.
// ---------------------------------------------------------------------------

std::string_view to_string(NomadClass value) {
    return nomad_policy().classifications[static_cast<std::size_t>(value)].name;
}

int nomad_class_index(std::string_view name) {
    const auto rows = nomad_policy().classifications;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

const NomadClassPolicy& nomad_class_policy(NomadClass value) {
    return nomad_policy().classifications[static_cast<std::size_t>(value)];
}

std::string_view to_string(NomadCampKind value) {
    switch (value) {
        case NomadCampKind::Winter: return "winter";
        case NomadCampKind::Summer: return "summer";
        case NomadCampKind::Base: return "base";
        case NomadCampKind::Station: return "station";
        case NomadCampKind::Lair: return "lair";
        case NomadCampKind::Shrine: return "shrine";
        case NomadCampKind::Terminal: return "terminal";
        case NomadCampKind::Calving: return "calving";
    }
    return "base";
}

std::string_view to_string(NomadBranch value) {
    switch (value) {
        case NomadBranch::Trunk: return "trunk";
        case NomadBranch::BaseLimb: return "base_limb";
        case NomadBranch::Fission: return "fission";
        case NomadBranch::Spur: return "spur";
    }
    return "trunk";
}

std::string_view to_string(NomadDisposition value) {
    switch (value) {
        case NomadDisposition::Hostile: return "hostile";
        case NomadDisposition::Wary: return "wary";
        case NomadDisposition::Tradeable: return "tradeable";
        case NomadDisposition::Devout: return "devout";
        case NomadDisposition::Fleeing: return "fleeing";
    }
    return "wary";
}

std::string_view to_string(NomadRouteStatus value) {
    return value == NomadRouteStatus::Routed ? "routed" : "stranded";
}

std::string_view to_string(NomadOriginKind value) {
    switch (value) {
        case NomadOriginKind::World: return "world";
        case NomadOriginKind::God: return "god";
        case NomadOriginKind::Villain: return "villain";
    }
    return "world";
}

double nomad_camp_rest(NomadCampKind kind) { return camp_rest_row(kind).share; }

std::optional<int> nomad_camp_rest_ceiling(NomadCampKind kind) {
    const CampRest& row = camp_rest_row(kind);
    return row.has_ceiling ? std::optional<int>(row.ceiling) : std::nullopt;
}

// Deserters press less than bandits because they are running, not hunting.
double nomad_raid_weight(NomadClass value) {
    switch (value) {
        case NomadClass::Bandits: return 0.22;
        case NomadClass::Deserters: return 0.14;
        default: return 0.0;
    }
}

// ---------------------------------------------------------------------------
// Ground.
// ---------------------------------------------------------------------------

double nomad_distance(const Vec3& a, const Vec3& b, double radius) {
    return radius * std::acos(clamp_unit(dot(a, b)));
}

// Placement's own two, exported for the band request API rather than copied into it.
std::pair<NomadClass, double> nomad_pick(Rng& rng, const std::vector<std::pair<NomadClass, double>>& shares,
                                         double total) {
    return pick(rng, shares, total);
}

NomadBand nomad_make_band(std::string uid, NomadClass value, const NomadHabitatCell& cell, int size, int age,
                          NomadBasis basis) {
    return make_band(std::move(uid), value, cell, size, age, std::move(basis));
}

double nomad_ruggedness(const NomadHabitatCell& cell) {
    return clamp01(cell.slope_deg / 22.0) * (1.0 + 0.35 * clamp01(std::abs(cell.tpi_m) / 40.0));
}

double nomad_population_of(const NomadSite& site) {
    for (const std::optional<double>* key : {&site.population_estimate, &site.urban_population_estimate,
                                             &site.rural_population_estimate}) {
        if (*key && **key > 0.0) return **key;
    }
    // A hamlet carries no head count at all, so the chain ends at the food it delivers.
    // The units are therefore inconsistent by construction, and deliberately so.
    if (site.delivered_food && *site.delivered_food > 0.0) return *site.delivered_food;
    return 0.0;
}

NomadNearest nomad_nearest(const Vec3& point, const std::vector<NomadGround::Place>& places,
                           double radius) {
    NomadNearest out;
    out.distance_m = kInf;
    const std::string* best_key = nullptr;
    for (std::size_t i = 0; i < places.size(); ++i) {
        const double span = nomad_distance(point, places[i].where, radius);
        // (distance, key string) with a STRICT `<`, so on an exact tie of both the first
        // candidate in list order wins.
        if (out.at < 0 || span < out.distance_m || (span == out.distance_m && places[i].key < *best_key)) {
            out.distance_m = span;
            out.at = static_cast<int>(i);
            best_key = &places[i].key;
        }
    }
    if (out.at < 0) out.distance_m = kInf;
    return out;
}

std::vector<NomadLeyNode> nomad_ley_nodes(const MagicReport& magic) {
    std::vector<NomadLeyNode> out;
    std::vector<std::string_view> emitted;
    const auto take = [&](const std::string& name, const LeyNetwork& net) {
        for (const LeyNode& node : net.nodes) out.push_back({name, node.id, node.direction, node.intensity});
    };
    // The school table's order, which is the order the networks were created in -- a
    // std::map walk would be alphabetical, and that is a different list.
    for (const MagicSchool& school : magic_school_table()) {
        auto found = magic.networks.find(std::string(school.name));
        if (found == magic.networks.end()) continue;
        emitted.push_back(school.name);
        take(found->first, found->second);
    }
    for (const auto& [name, net] : magic.networks) {
        if (std::find(emitted.begin(), emitted.end(), name) != emitted.end()) continue;
        take(name, net);
    }
    return out;
}

std::vector<NomadHabitatCell> nomad_habitat_cells(const std::map<std::string, Grid>& layers,
                                                  const SphereGrid& grid) {
    const auto find = [&](const char* name) -> const Grid* {
        auto found = layers.find(name);
        return found == layers.end() ? nullptr : &found->second;
    };
    const Grid* height = find("height");
    const Grid* slope = find("slope");
    const Grid* tpi = find("tpi");
    const Grid* temperature = find("temperature");
    const Grid* food = find("food_potential");
    const Grid* natural_food = find("natural_food_potential");
    const Grid* water_type = find("water_type");
    const Grid* salinity = find("salinity");
    const Grid* biome = find("biome");
    const Grid* natural_biome = find("natural_biome");

    const auto at = [](const Grid* layer, int x, int z) { return layer == nullptr ? 0.0 : layer->at(x, z); };

    std::vector<NomadHabitatCell> cells;
    cells.reserve(grid.node_count());
    for (std::size_t i = 0; i < grid.node_count(); ++i) {
        const int x = grid.point_x[i];
        const int z = grid.point_z[i];
        NomadHabitatCell cell;
        cell.node = static_cast<int>(i);
        cell.x = x;
        cell.z = z;
        cell.area_km2 = grid.areas[i] / 1e6;
        cell.direction = direction(x, z, grid.n);
        const double water = at(water_type, x, z);
        cell.medium = water == 1.0 ? NomadMedium::Marine
                    : (water != 0.0 ? (at(salinity, x, z) < 0.2 ? NomadMedium::Freshwater
                                                                : NomadMedium::SaltLake)
                                    : NomadMedium::Land);
        cell.height_m = at(height, x, z);
        cell.slope_deg = at(slope, x, z);
        cell.tpi_m = at(tpi, x, z);
        cell.temperature_c = at(temperature, x, z);
        cell.food_potential = at(food, x, z);
        cell.natural_food_potential = at(natural_food, x, z);
        // A LOOKUP WITH A DEFAULT, not an `or`. Where the natural_biome LAYER exists and
        // reads zero, zero is the answer -- falling through to `biome` there would emit a
        // different biome on every such cell, and that value rides onto every band.
        const Grid* source = natural_biome != nullptr ? natural_biome : biome;
        cell.biome = static_cast<int>(at(source, x, z));
        cells.push_back(cell);
    }
    return cells;
}

double nomad_seasonal_swing(const std::vector<const Grid*>& months, int x, int z) {
    if (months.size() < 2) return 0.0;
    double low = 0.0;
    double high = 0.0;
    for (std::size_t i = 0; i < months.size(); ++i) {
        if (months[i] == nullptr || months[i]->empty()) return 0.0;
        const double value = months[i]->at(x, z);
        if (i == 0 || value < low) low = value;
        if (i == 0 || value > high) high = value;
    }
    return clamp01((high - low) / 30.0);
}

NomadGround nomad_ground(const NomadRequest& request) {
    const SphereGrid& grid = *request.grid;
    NomadGround g;
    g.spacing = request.settlement_spacing_m;
    g.radius = request.globe_radius_m;
    for (const int age : request.ages) g.latest_age = std::max(g.latest_age, age);

    // Sites first then hamlets. Hamlets key on the ORDINAL id, which is re-sorted by
    // defence score at every age -- so that key names different ground after an advance.
    for (std::size_t i = 0; i < request.sites.size(); ++i) {
        const NomadSite& site = request.sites[i];
        g.wealth_of.push_back(&site);
        push_place(g.wealth, where_of(site, grid), site.uid, static_cast<int>(g.wealth_of.size()) - 1);
        push_place(g.markets, where_of(site, grid), site.uid, static_cast<int>(i));
    }
    for (const NomadSite& hamlet : request.hamlets) {
        g.wealth_of.push_back(&hamlet);
        push_place(g.wealth, where_of(hamlet, grid), hamlet.id, static_cast<int>(g.wealth_of.size()) - 1);
    }

    for (std::size_t i = 0; i < request.ruins.size(); ++i) {
        const NomadRuin& ruin = request.ruins[i];
        if (ruin.destroyed_age == g.latest_age) {
            push_place(g.fresh_ruins, where_of(ruin, grid), ruin.uid, static_cast<int>(i));
        }
    }

    // Ruins first, then sites: a ruin carries the loser's history and a surviving city
    // carries its own, and a tie between the two resolves to the ruin.
    for (const NomadRuin& ruin : request.ruins) {
        if (ruin.war_history == 0) continue;
        g.wars_of.push_back({ruin.uid, ruin.war_history});
        push_place(g.wars, where_of(ruin, grid), ruin.uid, static_cast<int>(g.wars_of.size()) - 1);
    }
    for (const NomadSite& site : request.sites) {
        if (site.war_history == 0) continue;
        g.wars_of.push_back({site.uid, site.war_history});
        push_place(g.wars, where_of(site, grid), site.uid, static_cast<int>(g.wars_of.size()) - 1);
    }

    for (std::size_t i = 0; i < request.ley.size(); ++i) {
        g.ley.push_back({request.ley[i].direction, request.ley[i].id, static_cast<int>(i)});
    }
    for (std::size_t i = 0; i < request.shrines.size(); ++i) {
        push_place(g.shrines, where_of(request.shrines[i], grid), request.shrines[i].id, static_cast<int>(i));
    }
    for (std::size_t i = 0; i < request.claims.size(); ++i) {
        push_place(g.claims, where_of(request.claims[i], grid), request.claims[i].id, static_cast<int>(i));
    }

    // First occurrence wins, in route order then node-within-route order. The bounds
    // check SHARES the skip with the dedupe, so an out-of-range node is never memoised as
    // seen -- it is simply skipped every time it appears.
    std::vector<char> seen(grid.node_count(), 0);
    for (const NomadRoadRoute& route : request.roads) {
        for (const int node : route.nodes) {
            if (node < 0 || static_cast<std::size_t>(node) >= grid.node_count()) continue;
            if (seen[static_cast<std::size_t>(node)]) continue;
            seen[static_cast<std::size_t>(node)] = 1;
            g.roads.push_back({direction(grid.point_x[static_cast<std::size_t>(node)],
                                         grid.point_z[static_cast<std::size_t>(node)], grid.n),
                               std::to_string(node), node});
        }
    }

    // Sites, then ports, then hamlets, in that concatenation order. The enumeration index
    // that becomes the tie-break key is taken over the UNFILTERED list.
    int index = 0;
    const auto settle = [&](const std::vector<NomadSite>& places) {
        for (const NomadSite& site : places) {
            g.settled.push_back({settled_where(site, grid.n), std::to_string(index), -1});
            ++index;
        }
    };
    settle(request.sites);
    settle(request.ports);
    settle(request.hamlets);
    return g;
}

std::optional<NomadVerdict> nomad_gate(NomadClass value, const NomadHabitatCell& cell, double swing,
                                       const NomadGround& ground, const NomadRequest& request) {
    const NomadGate& rule = nomad_class_policy(value).gate;
    switch (value) {
        case NomadClass::Survivors: return gate_survivors(cell, ground, rule, request);
        case NomadClass::Deserters: return gate_deserters(cell, ground, rule);
        case NomadClass::Cultists: return gate_cultists(cell, ground, rule, request);
        case NomadClass::Merchants: return gate_merchants(cell, ground, rule, request);
        case NomadClass::Bandits: return gate_bandits(cell, ground, rule);
        case NomadClass::Wanderers: return gate_wanderers(cell, swing, rule);
    }
    return std::nullopt;
}

int nomad_midsummer_day(int z, int n) {
    const double latitude = 90.0 - 180.0 * static_cast<double>(z) / static_cast<double>(n - 1);
    // The equator row, present only on an odd raster, reads latitude exactly 0.0 and takes
    // the NORTHERN phase.
    const double phase = latitude >= 0.0 ? kNomadMidsummerNorth : kNomadMidsummerNorth + 0.5;
    const double fraction = phase - std::floor(phase);
    return static_cast<int>(round_half_away(fraction * kNomadDaysPerYear) % kNomadDaysPerYear);
}

int nomad_midwinter_day(int z, int n) {
    return (nomad_midsummer_day(z, n) + kNomadDaysPerYear / 2) % kNomadDaysPerYear;
}

double nomad_round_forage(const NomadBand& band, const std::vector<NomadHabitatCell>& cells) {
    if (band.camps.empty()) return 0.0;
    double total = 0.0;
    for (const NomadCamp& camp : band.camps) {
        const NomadHabitatCell& cell = cells[static_cast<std::size_t>(camp.node)];
        total += std::max(cell.food_potential, cell.natural_food_potential);
    }
    return total / static_cast<double>(band.camps.size());
}

double nomad_round_capacity(NomadClass value, double forage) {
    const NomadClassPolicy& rule = nomad_class_policy(value);
    const double low = rule.size_low;
    const double high = rule.size_high;
    const double floor = rule.gate.min_forage;
    const double rich = nomad_policy().fission.rich_forage;
    return low + (high - low) * clamp01((forage - floor) / (rich - floor));
}

// ---------------------------------------------------------------------------
// PASS 1 -- place and classify.
// ---------------------------------------------------------------------------

std::optional<NomadBlock> add_nomads(const NomadRequest& request) {
    if (!request.world_recipe || request.phase < 16) return std::nullopt;
    const auto started = std::chrono::steady_clock::now();

    const std::vector<NomadHabitatCell>& cells = *request.cells;
    const NomadGround g = nomad_ground(request);
    const NomadPolicy& pol = nomad_policy();
    const double density = request.density.value_or(pol.candidate.density_per_1000km2);
    const double clearance = pol.candidate.settlement_clearance_spacings * g.spacing;
    Rng rng = Rng::child(request.seed, "nomads-v1", request.variation);

    NomadBlock block;
    block.version = kNomadsVersion;

    // NODE ORDER IS THE RNG STREAM ORDER and is therefore load-bearing: any reordering
    // changes every band in the world.
    for (const NomadHabitatCell& cell : cells) {
        if (cell.medium != NomadMedium::Land) continue;  // no draw, no roll record
        // 45 degrees is the absolute slope at which a cell contributes nothing.
        double room = clamp01(1.0 - cell.slope_deg / 45.0);
        if (room <= 0.0) continue;
        if (!g.settled.empty() && clearance > 0.0) {
            // CLEARANCE SCALES THE RATE; IT DOES NOT SKIP THE CELL. A skip discards whole
            // cells, and a cell is 52 km2 at raster 17 against 13 km2 at raster 33, so the
            // same world would quietly hold a different number of bands at a different
            // resolution. Scaling removes AREA instead of cells and is resolution-
            // independent. For the same reason clearance is not a rejection after the
            // draw: rejecting an already-drawn candidate holds fewer bands than asked.
            const NomadNearest near = nomad_nearest(cell.direction, g.settled, g.radius);
            room *= clamp01(near.distance_m / clearance);
        }
        const double rate = request.occurrence * density * cell.area_km2 / 1000.0 * room;
        if (rate <= 0.0) continue;

        long long drawn = truncate_to_int(rate);
        // ONE uniform draw, ALWAYS TAKEN once the rate is positive -- even when it results
        // in zero bands. The stream position depends on how many cells got this far.
        if (rng.random() < rate - static_cast<double>(drawn)) ++drawn;
        if (drawn == 0) continue;

        const double swing = nomad_seasonal_swing(request.seasonal_temperature, cell.x, cell.z);

        for (long long index = 0; index < drawn; ++index) {
            std::vector<std::pair<NomadClass, double>> shares;
            std::vector<NomadBasis> evidence;
            double total = 0.0;
            // The loop walks the ENUM, so the evaluation order is the contract's order by
            // construction rather than by memory.
            for (int i = 0; i < kNomadClassCount; ++i) {
                const auto value = static_cast<NomadClass>(i);
                const std::optional<NomadVerdict> verdict = nomad_gate(value, cell, swing, g, request);
                if (!verdict) continue;  // hard gate failed: ineligible outright
                const double share = verdict->score * nomad_class_policy(value).weight;
                if (share <= 0.0) continue;
                shares.emplace_back(value, share);
                evidence.push_back(verdict->basis);
                total += share;  // plain accumulation, in CLASSIFICATIONS order
            }

            NomadRoll record;
            record.node = cell.node;
            record.x = cell.x;
            record.z = cell.z;
            for (const auto& entry : shares) record.eligible.push_back(entry.first);
            record.evidence = evidence;
            record.precipitated = !shares.empty();
            if (shares.empty()) {
                // Empty ground is the honest answer, and the ledger says so.
                block.rolls.push_back(std::move(record));
                continue;
            }

            const auto [chosen, draw] = pick(rng, shares, total);
            const NomadClassPolicy& rule = nomad_class_policy(chosen);
            const int size = static_cast<int>(rng.randint(rule.size_low, rule.size_high));
            std::string uid = uid_for(g.latest_age, cell.node);
            if (index > 0) uid += "-" + std::to_string(index);

            std::size_t which = 0;
            while (which < shares.size() && shares[which].first != chosen) ++which;
            block.groups.push_back(make_band(uid, chosen, cell, size, g.latest_age, evidence[which]));
            ++block.counts[static_cast<std::size_t>(chosen)];
            record.roll = draw;
            record.chose = chosen;
            record.uid = block.groups.back().uid;
            block.rolls.push_back(std::move(record));
        }
    }

    // A LEXICOGRAPHIC string sort, not a numeric one: `nomad-3-10` sorts before
    // `nomad-3-9`. That order then decides which uid suffix a fission child takes, the
    // raid-pressure accumulation order and the cult ley-write order.
    std::sort(block.groups.begin(), block.groups.end(),
              [](const NomadBand& a, const NomadBand& b) { return a.uid < b.uid; });

    block.method =
        "A thinned point process over habitable land proposes start points, and the ground around each one "
        "decides the band. Six classifications are gated on hard geographic preconditions -- a ruin destroyed "
        "this age with a survivor settlement to flee to, a decided war with cover nearby, sacred ground with "
        "a god bound to its school, a road between two markets, rough country within strike of wealth and "
        "clear of authority, or forage with a seasonal swing -- and only eligible classifications enter a "
        "single weighted draw. A candidate that satisfies no gate produces no band. Every reach is a multiple "
        "of settlement spacing; speed is absolute metres per day because a walking pace belongs to the "
        "traveller rather than the world.";
    block.limits =
        "Placement and classification only. Routes, seasonal camps, day windows and branch topology are a "
        "separate pass, so a band here carries the camp it starts from and nothing it walks to. Sizes are "
        "drawn from authored bands rather than from local carrying capacity, and no band consumes forage, "
        "raids, trades or dies. Nothing here writes back into the world yet.";
    block.elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return block;
}

// ---------------------------------------------------------------------------
// PASS 2 -- circuits, day windows, and lineage fission.
// ---------------------------------------------------------------------------

NomadRoutes add_nomad_routes(NomadBlock& block, const NomadRequest& request) {
    NomadRoutes routes;
    routes.version = kNomadRoutesVersion;
    routes.day_march_m = kNomadDayMarchM;
    routes.method =
        "Each classification builds the circuit its behaviour implies -- a two-move seasonal round for "
        "herders, a market chain subdivided by day marches for caravans, a closed circuit over one god's "
        "sacred ground for cults, a lair with out-and-back spurs for raiders, and a single terminating move "
        "for the bands running from something. Legs are traced with Dijkstra over a walking cost that "
        "permits fords and steeper ground than a road, and prefers forage. Day windows come from leg length "
        "over speed, anchored so a winter camp holds midwinter; the seasonal phase inverts across the "
        "equator. A clan whose head count passes what its own round carries then splits: the daughter takes "
        "a camp of the parent, names it in parent_uid, and a fission leg joins the two rounds. Capacity is "
        "read off the classification's authored size band by the forage of the round, never as head per "
        "square kilometre, so it does not move with the raster.";
    routes.limits =
        "Fission runs once, when a band is placed, and never again -- at most one child per band, and a "
        "child does not split. That is a ceiling rather than a model: nothing kills a band, and a band does "
        "not survive an age turn either, because add_nomads replaces the block wholesale. So a clan has no "
        "legible descent across ages and no band has a grandchild. The fission leg carries no day window: it "
        "happened once and is not part of anybody's year, and its `to` names a camp of the band in "
        "parent_uid rather than one of the child's own. A fissioned parent keeps its own head count, because "
        "this pass has no demographic model and rewriting a placed band's size from here would make the "
        "authored size band a fiction. Circuits are static: a band walks the same round every year and "
        "nothing re-routes it when the world changes underneath. Rest is split evenly across camps rather "
        "than earned from forage, so a rich camp holds no longer than a poor one. A band whose circuit could "
        "not be built carries route_status \"stranded\" and keeps its start camp -- a herder with nowhere to "
        "winter is the case the classification gate cannot see on its own.";

    // NO PHASE GATE. The pass is callable standalone by the request API and by the monthly
    // tick, and a gate here would break both.
    if (block.groups.empty()) return routes;
    const auto started = std::chrono::steady_clock::now();

    const std::vector<NomadHabitatCell>& cells = *request.cells;
    WalkCost cost(*request.grid, cells, request.road_max_grade);
    std::unordered_map<int, PathPtr> cache;
    // -2 is "not yet resolved", because -1 is a real answer from an empty grid.
    std::vector<int> ley_node(request.ley.size(), -2);

    RouteContext ctx;
    ctx.request = &request;
    ctx.grid = request.grid;
    ctx.cells = &cells;
    ctx.spacing = request.settlement_spacing_m;
    ctx.radius = request.globe_radius_m;
    ctx.cost = &cost;
    ctx.cache = &cache;
    ctx.ley = &request.ley;
    ctx.ley_node = &ley_node;

    // DAUGHTER CLANS ARE DERIVED FROM THE PARENTS BELOW, NEVER CARRIED IN. This pass runs
    // again on every request and on a monthly cadence, so a fission step that added to the
    // set instead of replacing it would breed a new generation per call.
    block.groups.erase(std::remove_if(block.groups.begin(), block.groups.end(),
                                      [](const NomadBand& band) { return !band.parent_uid.empty(); }),
                       block.groups.end());

    for (NomadBand& band : block.groups) {
        const std::optional<Built> built = build_route(band, ctx);
        if (!built) {
            band.route_status = NomadRouteStatus::Stranded;
            ++routes.stranded;
            continue;
        }
        std::vector<NomadCamp> camps = built->camps;
        std::vector<NomadLeg> legs = legs_for(band, camps, built->order, *built->from_start, ctx);
        if (legs.empty()) {
            band.route_status = NomadRouteStatus::Stranded;
            ++routes.stranded;
            continue;
        }
        if (band.classification == NomadClass::Merchants) {
            subdivide(band, camps, legs, cells, ctx.radius, kNomadDayMarchM);
        }
        assign_days(band, camps, legs, ctx);
        double round_length = 0.0;
        for (const NomadLeg& leg : legs) round_length += leg.length_m;
        band.camps = std::move(camps);
        band.legs = std::move(legs);
        band.route_status = NomadRouteStatus::Routed;
        band.round_length_m = round6(round_length);
        ++routes.routed;
    }

    // --- lineage fission -----------------------------------------------------
    //
    // ONCE AT PLACEMENT, NOT ONCE AN AGE, and the reasons are recorded so the ruling can be
    // reversed knowingly: nothing kills a band, so per-age fission grows the population
    // without a ceiling; a band does not survive an age anyway, because placement replaces
    // the whole block; and an age is five thousand years, so a per-age rule fires once per
    // two hundred generations of herders.
    const NomadPolicy& pol = nomad_policy();
    std::vector<NomadBand> born;
    std::vector<std::string> taken;
    for (const NomadBand& band : block.groups) taken.push_back(band.uid);

    std::vector<const NomadBand*> candidates;
    for (const NomadBand& band : block.groups) {
        if (!band.parent_uid.empty()) continue;  // a child never splits
        if (std::find(pol.fission.classifications.begin(), pol.fission.classifications.end(),
                      band.classification) == pol.fission.classifications.end()) {
            continue;
        }
        if (band.route_status != NomadRouteStatus::Routed) continue;
        const double capacity = nomad_round_capacity(band.classification, nomad_round_forage(band, cells));
        if (static_cast<double>(band.size) > capacity) candidates.push_back(&band);
    }

    // THE REAL GATE, rebuilt once for the whole pass, not a restatement of it: a child is
    // only seated on ground that would raise a band of its kind on its own merits, so
    // every precondition the classification asserts of a placed band holds of a daughter.
    const NomadGround fission_ground = candidates.empty() ? NomadGround() : nomad_ground(request);

    for (const NomadBand* parent : candidates) {
        // A FRESH STREAM PER CANDIDATE, so the number of candidates cannot shift any other
        // band's draws.
        Rng split = Rng::child(request.seed, "nomad-fission-" + parent->uid, request.variation);
        const NomadClassPolicy& rule = nomad_class_policy(parent->classification);
        // EXACTLY ONE DRAW PER CANDIDATE, taken before the inheritance loop, so the child's
        // size does not depend on which camp it ends up seated on and a camp that fails
        // costs nothing.
        const double share = split.uniform(pol.fission.child_share_low, pol.fission.child_share_high);
        const double wanted = static_cast<double>(round_half_away(static_cast<double>(parent->size) * share));
        const int size = static_cast<int>(truncate_to_int(
            std::min<double>(rule.size_high, std::max<double>(rule.size_low, wanted))));

        for (const NomadCamp* inherited : inheritable(*parent)) {
            const NomadHabitatCell& cell = cells[static_cast<std::size_t>(inherited->node)];
            const double swing = nomad_seasonal_swing(request.seasonal_temperature, cell.x, cell.z);
            const std::optional<NomadVerdict> verdict =
                nomad_gate(parent->classification, cell, swing, fission_ground, request);
            if (!verdict) continue;

            std::string uid = uid_for(parent->origin.age, inherited->node);
            int suffix = 0;
            while (std::find(taken.begin(), taken.end(), uid) != taken.end()) {
                ++suffix;
                uid = uid_for(parent->origin.age, inherited->node) + "-" + std::to_string(suffix);
            }

            NomadBand child = make_band(uid, parent->classification, cell, size, parent->origin.age,
                                        verdict->basis);
            child.parent_uid = parent->uid;
            child.origin = parent->origin;
            child.basis.inherited_camp = inherited->id;

            const std::optional<Built> built = build_route(child, ctx);
            // A segment with nowhere of its own to winter has not split, it has STARVED.
            // Refused rather than recorded as a stranded child.
            if (!built) continue;
            std::vector<NomadCamp> camps = built->camps;
            std::vector<NomadLeg> legs = legs_for(child, camps, built->order, *built->from_start, ctx);
            if (legs.empty()) continue;
            assign_days(child, camps, legs, ctx);

            const PathPtr back = ctx.paths(child.node);
            const std::vector<int> joined = trace(back->parent, child.node, parent->node);
            if (joined.size() < 2) continue;
            double span = 0.0;
            for (std::size_t i = 0; i + 1 < joined.size(); ++i) {
                span += nomad_distance(cells[static_cast<std::size_t>(joined[i])].direction,
                                       cells[static_cast<std::size_t>(joined[i + 1])].direction, ctx.radius);
            }

            double round_length = 0.0;
            for (const NomadLeg& leg : legs) round_length += leg.length_m;
            child.camps = std::move(camps);
            child.legs = std::move(legs);
            // The move that FOUNDED the child, appended after its round is scheduled and
            // deliberately carrying NO day window: it happened once, so it is not a leg of
            // anybody's year, and giving it one would put the child at its parent's camp
            // every spring. Its `to` names a camp of ANOTHER band, which is what makes the
            // branch a join between two rounds rather than one inside a round.
            NomadLeg join;
            join.from = child.camps.front().id;
            join.to = parent->camps.front().id;
            join.nodes = joined;
            join.length_m = span;
            join.branch = NomadBranch::Fission;
            child.legs.push_back(std::move(join));
            child.route_status = NomadRouteStatus::Routed;
            // THE ANNUAL ROUND ONLY.
            child.round_length_m = round6(round_length);
            taken.push_back(child.uid);
            born.push_back(std::move(child));
            break;  // the first camp that works wins
        }
    }

    routes.fissioned = static_cast<int>(born.size());
    if (!born.empty()) {
        for (NomadBand& child : born) block.groups.push_back(std::move(child));
        std::sort(block.groups.begin(), block.groups.end(),
                  [](const NomadBand& a, const NomadBand& b) { return a.uid < b.uid; });
        // `fissioned` is counted INSIDE `routed`, so routed + stranded still equals the
        // band count -- an invariant a test pins.
        routes.routed += routes.fissioned;
    }

    // RECOMPUTED from the band set rather than incremented, because three callers add to
    // it and a tally that is only ever incremented cannot survive a pass that removes
    // anybody -- which step one of this pass does, every time.
    block.counts.fill(0);
    for (const NomadBand& band : block.groups) ++block.counts[static_cast<std::size_t>(band.classification)];

    routes.elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    block.routes = routes;
    return routes;
}

// ---------------------------------------------------------------------------
// PASS 3 -- the write-backs.
// ---------------------------------------------------------------------------

std::optional<NomadEffects> apply_nomad_effects(const NomadBlock& block, const NomadRequest& request) {
    if (!request.world_recipe || request.phase < 16) return std::nullopt;
    const auto started = std::chrono::steady_clock::now();

    NomadEffects out;
    out.version = kNomadEffectsVersion;
    out.method =
        "Every cult deepens its circuit node directly through the network edit, hidden school or known, by "
        "the same multiplicative lift. A cult can only deepen a node that already exists; only the "
        "corruption API creates one in a hidden network, so a hidden lift counts separately. Raiders add a "
        "distance-weighted nomad_pressure beside the nest, ley and war pressures a town already tracks. "
        "Roads a caravan actually rides record their riders and a throughput share. Refugee bands that "
        "reached safety are recorded as settlement candidates.";
    out.limits =
        "A cult deepens ground, it does not open it: a band naming a node that does not exist in its network "
        "writes nothing, and a band with no ley node in its basis writes nothing at all. This pass runs on "
        "every request as well as every age advance, so a node a cult holds ratchets toward the 4.0 ceiling "
        "across calls rather than settling. Survivor camps are candidates, not settlements: founding one "
        "here would mean re-running settlement generation after every downstream block has read the "
        "settlements it produced. Caravan throughput is a share of nodes ridden, not a modelled cargo "
        "volume, and nothing consumes it yet. Raid pressure is added after the threat assessment was "
        "evaluated, so it widens regional_threat without having influenced anything that already read it.";

    // --- cultist leylines ----------------------------------------------------
    //
    // A pilgrimage circuit is worship, and worship on charged ground leaves a mark. It
    // DEEPENS a groove that was already there rather than ripping the land open, so the
    // gain is a fraction of the node's existing charge.
    if (request.magic_enabled) {
        std::vector<const NomadBand*> cults;
        for (const NomadBand& band : block.groups) {
            if (band.classification == NomadClass::Cultists && !band.school.empty()) cults.push_back(&band);
        }
        // SORTED so two bands touching the same node cannot produce two different worlds
        // depending on which happened to be placed first.
        std::sort(cults.begin(), cults.end(),
                  [](const NomadBand* a, const NomadBand* b) { return a->uid < b->uid; });
        // The reference writes the lifted intensity back into the network, so a second
        // cult on the same node lifts the ALREADY LIFTED value. Tracked here for the same
        // reason: without it the second write would be computed off a stale charge.
        std::vector<std::pair<std::string, double>> current;
        for (const NomadLeyNode& node : request.ley) current.emplace_back(node.school + "\n" + node.id, node.intensity);

        for (const NomadBand* band : cults) {
            // A claim-derived or shrine-derived band carries NO ley_node_id, so there is
            // nothing to deepen.
            if (band->basis.ley_node_id.empty()) continue;
            const std::string key = band->school + "\n" + band->basis.ley_node_id;
            auto found = std::find_if(current.begin(), current.end(),
                                      [&](const auto& row) { return row.first == key; });
            // DEEPEN, NEVER CREATE. Only the corruption API seats a node in a network.
            if (found == current.end()) continue;
            const double lifted = std::min(kNomadIntensityCeiling, found->second * (1.0 + kNomadDevotionGain) - 0.0);
            if (lifted == found->second) continue;  // already at the ceiling
            found->second = lifted;
            const int index = magic_school_index(band->school);
            const bool hidden = index < 0 || magic_school_table()[static_cast<std::size_t>(index)].hidden;
            out.leyline_edits.push_back({band->school, band->basis.ley_node_id, lifted, hidden});
            if (hidden) {
                ++out.leyline_edits_hidden;
            } else {
                ++out.leyline_edits_known;
            }
        }
    }

    // --- raid pressure -------------------------------------------------------
    if (!request.threat_cities.empty() && !request.sites.empty()) {
        const double reach = kNomadStrikeSpacings * request.settlement_spacing_m;
        std::vector<const NomadBand*> raiders;
        for (const NomadBand& band : block.groups) {
            if (nomad_raid_weight(band.classification) > 0.0) raiders.push_back(&band);
        }
        for (const NomadThreatCity& city : request.threat_cities) {
            const NomadSite* site = nullptr;
            for (const NomadSite& row : request.sites) {
                if (row.uid == city.city_uid) {
                    site = &row;
                    break;
                }
            }
            // A city that does not resolve, or whose site carries no direction, gets no
            // `nomad_pressure` row at all -- not a zero one.
            if (site == nullptr || !site->direction) continue;
            double pressure = 0.0;
            std::vector<std::string> named;
            for (const NomadBand* band : raiders) {
                const double span = nomad_distance(*site->direction, band->direction, request.globe_radius_m);
                if (span > reach) continue;
                // FALLS OFF WITH DISTANCE: a lair on the doorstep is not the same as one at
                // the edge of the reach, and a flat term made every town in a region read
                // alike. Note this reads the BAND'S START DIRECTION, not its lair camp.
                pressure += nomad_raid_weight(band->classification) * (1.0 - span / reach);
                named.push_back(band->uid);
            }
            NomadRaidPressure row;
            row.city_uid = city.city_uid;
            row.nomad_pressure = round6(pressure - 0.0);
            if (!named.empty()) {
                std::sort(named.begin(), named.end());
                row.contributors = std::move(named);
                row.regional_threat = round6(std::min(1.0, city.regional_threat + pressure));
                ++out.towns_under_raid_pressure;
            }
            out.raid_pressure.push_back(std::move(row));
        }
    }

    // --- caravan trade -------------------------------------------------------
    //
    // A single shared node is a crossing, not a ride, which is why two are needed.
    if (!request.roads.empty()) {
        std::vector<std::size_t> order;  // route indices in first-rider order
        std::vector<NomadCaravanTrade> ridden;
        std::vector<double> carried;
        for (const NomadBand& band : block.groups) {
            if (band.classification != NomadClass::Merchants) continue;
            std::vector<int> walked;
            for (const NomadLeg& leg : band.legs) walked.insert(walked.end(), leg.nodes.begin(), leg.nodes.end());
            std::sort(walked.begin(), walked.end());
            walked.erase(std::unique(walked.begin(), walked.end()), walked.end());
            for (std::size_t index = 0; index < request.roads.size(); ++index) {
                std::size_t shared = 0;
                for (const int node : request.roads[index].nodes) {
                    if (std::binary_search(walked.begin(), walked.end(), node)) ++shared;
                }
                if (shared < 2) continue;
                auto at = std::find(order.begin(), order.end(), index);
                if (at == order.end()) {
                    order.push_back(index);
                    ridden.push_back({index, {}, 0.0});
                    carried.push_back(0.0);
                    at = order.end() - 1;
                }
                const auto slot = static_cast<std::size_t>(at - order.begin());
                ridden[slot].riders.push_back(band.uid);
                carried[slot] += static_cast<double>(shared);
            }
        }
        for (std::size_t i = 0; i < ridden.size(); ++i) {
            std::sort(ridden[i].riders.begin(), ridden[i].riders.end());
            const std::size_t count = request.roads[ridden[i].route_index].nodes.size();
            ridden[i].throughput = round6(carried[i] / static_cast<double>(std::max<std::size_t>(1, count)));
        }
        out.caravan_trade = std::move(ridden);
    }

    // --- survivor camps ------------------------------------------------------
    for (const NomadBand& band : block.groups) {
        if (band.classification != NomadClass::Survivors || band.route_status != NomadRouteStatus::Routed) {
            continue;
        }
        const NomadCamp* terminal = nullptr;
        for (const NomadCamp& camp : band.camps) {
            if (camp.kind == NomadCampKind::Terminal) {
                terminal = &camp;
                break;
            }
        }
        if (terminal == nullptr) continue;
        NomadSettlementCandidate candidate;
        candidate.id = "settlement-candidate-" + band.uid;
        candidate.from_band = band.uid;
        candidate.node = terminal->node;
        candidate.direction = terminal->direction;
        candidate.migration_source_node = band.node;
        candidate.migration_distance_m = round6(band.round_length_m.value_or(0.0));
        candidate.population_estimate = band.size;
        candidate.reason = "Refugees from " + band.basis.ruin_uid + " who reached " + band.basis.refuge_uid +
                           " and stayed.";
        out.settlement_candidates.push_back(std::move(candidate));
    }
    // ONLY ASSIGNED WHEN NON-EMPTY, so a world can carry a previous pass's list unchanged
    // -- which is exactly why absorption has to be idempotent by band uid.
    if (!out.settlement_candidates.empty()) {
        std::sort(out.settlement_candidates.begin(), out.settlement_candidates.end(),
                  [](const NomadSettlementCandidate& a, const NomadSettlementCandidate& b) { return a.id < b.id; });
        out.assign_settlement_candidates = true;
    }

    out.elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return out;
}

NomadAbsorptionResult absorb_survivor_camps(std::vector<NomadSettlementCandidate>& candidates,
                                            const std::vector<NomadBand>& groups,
                                            std::vector<NomadSite>& survivors, int age) {
    NomadAbsorptionResult out;
    if (candidates.empty()) return out;

    std::vector<NomadSettlementCandidate*> order;
    order.reserve(candidates.size());
    for (NomadSettlementCandidate& candidate : candidates) order.push_back(&candidate);
    std::sort(order.begin(), order.end(),
              [](const NomadSettlementCandidate* a, const NomadSettlementCandidate* b) { return a->id < b->id; });

    for (NomadSettlementCandidate* candidate : order) {
        const NomadBand* band = nullptr;
        for (const NomadBand& row : groups) {
            if (row.uid == candidate->from_band) {
                band = &row;
                break;
            }
        }
        // A candidate whose band no longer resolves -- the block was replaced by a
        // reclassification -- is skipped rather than guessed at.
        if (band == nullptr) continue;
        NomadSite* site = nullptr;
        for (NomadSite& row : survivors) {
            if (!row.uid.empty() && row.uid == band->basis.refuge_uid) {
                site = &row;
                break;
            }
        }
        // A refuge that FELL at this boundary is not in `survivors`, so the band absorbs
        // nowhere and its arrival is simply not recorded. The filter working, not the pass
        // failing.
        if (site == nullptr) continue;
        // IDEMPOTENT BY BAND UID, which is never reused by a later age.
        if (std::find(site->absorbed_bands.begin(), site->absorbed_bands.end(), band->uid) !=
            site->absorbed_bands.end()) {
            continue;
        }
        const long long count = candidate->population_estimate;
        if (count <= 0) continue;

        site->absorbed_bands.push_back(band->uid);
        std::sort(site->absorbed_bands.begin(), site->absorbed_bands.end());
        // Rides `absorbed_refugees` rather than `population_estimate`, because the
        // settlement rebuild re-derives the estimate from the budget every time and would
        // erase a number written into it. The budget adds this back AFTER the split.
        site->absorbed_refugees += count;
        candidate->absorbed_age = age;
        candidate->absorbed_into = band->basis.refuge_uid;
        out.absorbed.push_back({candidate->id, band->uid, band->basis.refuge_uid, count});
        out.people += count;
    }
    return out;
}

}  // namespace fwg
