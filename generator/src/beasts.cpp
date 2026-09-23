#include "fwg/beasts.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

#include "fwg/astrology.hpp"
#include "fwg/random.hpp"

namespace fwg {
namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

// The height scale over which "higher than home" saturates, in metres. Bigger on high
// ground so a mountain herd is not pinned at lift == 1 everywhere.
constexpr double kLiftSpanM = 400.0;
// Openness fades linearly to zero at this slope, in DEGREES; shelter saturates at this
// many metres of topographic departure. Two different quantities that happen to share a
// number, which is why they are two constants.
constexpr double kOpennessSlopeDegrees = 30.0;
constexpr double kShelterTpiM = 30.0;
// -20 C .. +25 C mapped to 0..1.
constexpr double kWarmthOffsetC = 20.0;
constexpr double kWarmthSpanC = 45.0;
// Floors. A sheer summer camp still scores 40%; winter ground need not feed them, only be
// survivable; temperature is at most a 3:1 factor.
constexpr double kOpennessFloor = 0.4;
constexpr double kShelterFloor = 0.4;
constexpr double kWinterFoodFloor = 0.3;
constexpr double kWarmthFloor = 0.5;
// How sharply climbing is penalised in the walking cost. At the 0.77 ceiling the factor
// is 5.74.
constexpr double kGradePenalty = 8.0;
// The forage term's pull on the walking cost. It lies in [0.75, 1.0], so it can never
// make an edge free and never reverses a large length difference -- it only makes a herd
// prefer the green way round.
constexpr double kGreenPull = 0.25;
// At most three haunts on a drifter's circuit.
constexpr std::size_t kDriftHaunts = 3;

constexpr std::string_view kMovementsMethod =
    "Every placed site whose species declares a movement class other than nester becomes a "
    "travelling group. Herds walk a seasonal round between high summer pasture and sheltered "
    "winter ground through a calving camp; the unhoused dead circuit between ruins; and "
    "followers derive their circuit from a host's, taking nomad bands and herds alike. A swarm "
    "marches only in a year that is actually bad for it: this year's forage factor -- one draw "
    "for the whole world, keyed on the genesis seed and the absolute year so a monthly tick "
    "cannot re-roll it -- is applied to the group's own cell and compared against the mean of "
    "what a march can reach, so marginal ground in a lean year erupts and the same ground in an "
    "ordinary year stays solitary. Groups share the shape of nomads.groups so one consumer "
    "reads both.";

constexpr std::string_view kMovementsLimits =
    "A group is one site rather than a modelled population: nothing breeds, starves, is eaten or "
    "merges with another group, and a herd does not shrink when a follower attaches to it. "
    "Followers pick the nearest host and never change host. The irruption year is a draw rather "
    "than a simulated drought: seasonal_environment carries twelve climatological months that "
    "are the same in every year, so there is no modelled weather for a year to depart from, and "
    "the factor stands in for one. On a tick the year is read from world_clock as it "
    "stands when this pass runs, and a time advance writes the advanced clock after its "
    "cadence loop, so a span crossing several years builds this block for the year that "
    "span started in. A swarm that is not swarming carries route_status "
    "\"solitary\", which is its normal state; a group whose route could not be built is still "
    "reported as stranded. This block is therefore not stable from year to year, which is the "
    "point of it. Circuits are otherwise static.";

constexpr std::string_view kEncountersMethod =
    "One entry per travelling group and one occupancy record per camp it holds, indexed by month "
    "and by terrain node so a consumer answers \"what is near here, and when\" in a lookup rather "
    "than a scan over every leg of every group. Both nomad bands and travelling creatures are "
    "indexed through the same shape. Threat is on the creature-tier rubric, one to five; a band "
    "takes its tier from its classification, because what a band will do to you is a property of "
    "what kind of band it is. Each entry also carries what it is rather than only which species "
    "it is: `domain` is land, ocean or lake on the same word key_locations uses, `class` is "
    "animal, monster or people on the word wildlife and beast_nests use, and `airborne` marks a "
    "flyer. A shore species is `land`, because a shore medium is refused every water cell and a "
    "traveller can walk to the colony; flying is not a domain, so `domain == \"land\"` means "
    "reachable on foot and keeps meaning it.";

constexpr std::string_view kEncountersLimits =
    "An index, not a quest generator: no objectives, stakes, rewards or difficulty, because the "
    "consumer that would define them does not exist yet. Occupancy covers camps only -- a group "
    "in transit is derivable from its leg node path and its speed, and indexing every node of "
    "every leg would multiply the block by the length of the walking. Stranded groups appear with "
    "their start camp and no schedule, so a null day window means \"here, always\" rather than "
    "\"here, never\". Nothing here accounts for two groups meeting each other. Static sites are "
    "not indexed at all: only movers are, so the lairs in beast_nests -- the most dangerous "
    "things in the world, and the ones that do not move -- are absent, and the threat_tier "
    "histogram therefore describes travelling groups and not the world's danger profile. A "
    "world holding tier-five nests can and does produce an index with no tier-five entry. "
    "Whether to index static sites is a design decision and not a field addition.";

double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }

double great_circle(const Vec3& a, const Vec3& b, double radius) {
    return radius * std::acos(clamp_unit(dot(a, b)));
}

// Python's `%` is non-negative for a positive modulus and C++'s is not. `(depart - held)
// % 360` relies on that, so a truncating remainder puts a camp's arrival on a negative day.
int day_mod(long long value, int modulus) {
    const long long r = value % modulus;
    return static_cast<int>(r < 0 ? r + modulus : r);
}

// Six decimal places on the two published lengths and the forage factor.
//
// PYTHON ROUNDS HALF TO EVEN AND THIS ROUNDS HALF AWAY FROM ZERO. Under the clean-rewrite
// ruling that is sanctioned; it is recorded here because the two differ on an exact half
// and a reader comparing against the reference would otherwise chase it.
double round6(double v) { return std::round(v * 1e6) / 1e6; }

// The same ruling for the day arithmetic: Python's int(round(x)) is banker's rounding and
// llround is half away from zero. It appears at three sites -- the two travel-day
// computations and midsummer_day, whose argument is exactly 180.0 or 0.0 so no half case
// can arise there at all.
long long round_half_away(double v) { return std::llround(v); }

// ---------------------------------------------------------------------------
// The ground, as a struct of arrays.
//
// NOT habitat_cells: that resolves the fields the CATALOGUE weights on, and five of the
// six this pass reads -- height, slope, tpi, temperature and the two food layers -- are
// not among them. Reading the layers directly is the same values with the same defaults.
// ---------------------------------------------------------------------------

struct Ground {
    std::vector<Vec3> dir;
    std::vector<double> height;       // metres above the datum, signed
    std::vector<double> forage;       // max of the two food layers, UNCLAMPED
    std::vector<double> forage_c;     // ...and the clamped copy the walking cost uses
    std::vector<double> slope;        // DEGREES
    std::vector<double> temperature;  // degrees Celsius
    std::vector<double> tpi;          // metres, signed
    std::vector<std::uint8_t> water;  // anything that is not land is impassable
    std::vector<int> x;
    std::vector<int> z;
};

const Grid* find_layer(const std::map<std::string, Grid>& layers, std::string_view name) {
    const auto it = layers.find(std::string(name));
    return it == layers.end() ? nullptr : &it->second;
}

double layer_at(const Grid* layer, int x, int z) { return layer == nullptr ? 0.0 : layer->at(x, z); }

Ground build_ground(const std::map<std::string, Grid>& layers, const SphereGrid& grid) {
    const Grid* height = find_layer(layers, "height");
    const Grid* slope = find_layer(layers, "slope");
    const Grid* tpi = find_layer(layers, "tpi");
    const Grid* temperature = find_layer(layers, "temperature");
    const Grid* water_type = find_layer(layers, "water_type");
    // The farming pass that writes these two is not ported. A missing layer reads 0.0
    // rather than erroring -- the reference's own `.get(name, 0.)` -- so on a world
    // generated by the stages that exist today every forage term is zero, the summer
    // scorer degenerates to "the lowest reachable node index" and every irruptive group
    // reports solitary. That is the honest consequence of the missing input, not a bug in
    // the rules below, and it disappears the moment the layers exist.
    const Grid* food = find_layer(layers, "food_potential");
    const Grid* natural_food = find_layer(layers, "natural_food_potential");

    const std::size_t count = grid.node_count();
    Ground out;
    out.dir.resize(count);
    out.height.resize(count);
    out.forage.resize(count);
    out.forage_c.resize(count);
    out.slope.resize(count);
    out.temperature.resize(count);
    out.tpi.resize(count);
    out.water.resize(count);
    out.x.resize(count);
    out.z.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const int x = grid.point_x[i];
        const int z = grid.point_z[i];
        out.x[i] = x;
        out.z[i] = z;
        out.dir[i] = direction(x, z, grid.n);
        out.height[i] = layer_at(height, x, z);
        out.slope[i] = layer_at(slope, x, z);
        out.tpi[i] = layer_at(tpi, x, z);
        out.temperature[i] = layer_at(temperature, x, z);
        const double forage = std::max(layer_at(food, x, z), layer_at(natural_food, x, z));
        // The scorers read this UNCLAMPED and the walking cost reads it clamped. The
        // asymmetry is the reference's and is real: a food layer above one lifts a summer
        // camp's score but cannot make an edge cheaper than the green term allows.
        out.forage[i] = forage;
        out.forage_c[i] = clamp01(forage);
        // Salinity and the marine/freshwater/salt-lake distinction never affect
        // passability; they only decide what a cell is CALLED. The mask is water_type.
        out.water[i] = layer_at(water_type, x, z) != 0.0 ? 1 : 0;
    }
    return out;
}

// ---------------------------------------------------------------------------
// The priced graph.
// ---------------------------------------------------------------------------

struct PricedEdge {
    int node = 0;
    double cost = 0.0;
};

// Every passable step priced once, instead of once per search that crosses it. The search
// below is run thousands of times over one static graph and the cost is the expensive part
// of it; the graph and the ground are both fixed for the whole pass, so the answer cannot
// change.
//
// Impassable edges are DROPPED rather than priced as infinite, so the search never sees
// them -- the same set, in the same order, as a form that skipped them at relaxation time.
std::vector<std::vector<PricedEdge>> price_edges(const SphereGrid& grid, const Ground& ground,
                                                 double road_max_grade) {
    // A band fords rather than bridges and takes ground a road would not, which is what
    // the relief factor buys. Water is still impassable -- these are not boats.
    const double ceiling = road_max_grade * kGradeRelief;
    const int n = grid.n;
    std::vector<std::vector<PricedEdge>> priced(grid.node_count());
    for (std::size_t i = 0; i < grid.node_count(); ++i) {
        if (ground.water[i] != 0) continue;  // every edge out of water is dropped anyway
        const int xi = ground.x[i];
        const int zi = ground.z[i];
        std::vector<PricedEdge>& row = priced[i];
        for (const SphereNeighbour& edge : grid.neighbours[i]) {
            const std::size_t j = static_cast<std::size_t>(edge.index);
            const double d = edge.distance;
            if (ground.water[j] != 0 || d <= 0.0) continue;
            const int xj = ground.x[j];
            const int zj = ground.z[j];
            // A diagonal cannot slip between two water-filled corner cells. There is no
            // corner cell to slip between at a pole, so either endpoint on a pole row
            // exempts the step.
            if (xi != xj && zi != zj && zi != 0 && zi != n - 1 && zj != 0 && zj != n - 1) {
                if (ground.water[grid.node_index(xi, zj)] != 0 ||
                    ground.water[grid.node_index(xj, zi)] != 0) {
                    continue;
                }
            }
            const double grade = std::abs(ground.height[j] - ground.height[i]) / d;
            if (grade > ceiling) continue;
            const double green = 1.0 - kGreenPull * (ground.forage_c[i] + ground.forage_c[j]) / 2.0;
            row.push_back({static_cast<int>(j), d * (1.0 + kGradePenalty * grade * grade) * green});
        }
    }
    return priced;
}

// ---------------------------------------------------------------------------
// A bounded, targetable Dijkstra that keeps its arrays between calls.
// ---------------------------------------------------------------------------

class BoundedSearch {
public:
    explicit BoundedSearch(std::vector<std::vector<PricedEdge>> priced)
        : priced_(std::move(priced)),
          distances_(priced_.size(), kInf),
          parent_(priced_.size(), -1) {}

    // `target` of -1 searches out to `limit`; a target stops the search the moment it is
    // popped, which is sound because every node on the shortest path to it has a STRICTLY
    // smaller distance (strictly, because no edge here is free) and is therefore already
    // settled with its final parent.
    //
    // Stopping at the reach is not an approximation either: edge costs are strictly
    // positive, so every prefix of a path costing `limit` or less also costs `limit` or
    // less, and a node inside the bound keeps the distance and parent an unbounded search
    // would have given it.
    void run(int start, double limit = kInf, int target = -1);

    // ALL THREE ARE SCRATCH. They hold for exactly as long as it takes to call `run`
    // again, which every caller in this file respects: two read only the reached list,
    // the drifter builder reads the distances before returning, and the route cache
    // traces the parents before returning. Nothing here is thread-safe.
    const std::vector<double>& distances() const { return distances_; }
    const std::vector<int>& parent() const { return parent_; }
    // ASCENDING NODE ORDER, and that order is load-bearing: the irruption builder sums
    // forage over it and float addition is not associative.
    const std::vector<int>& reached() const { return reached_; }

private:
    struct Entry {
        double d = 0.0;
        int node = 0;
    };
    // A min-heap on the PAIR, so equal distances break on the lower node index. A queue
    // keyed on distance alone diverges on equal-cost paths.
    struct Later {
        bool operator()(const Entry& a, const Entry& b) const {
            return a.d != b.d ? a.d > b.d : a.node > b.node;
        }
    };

    std::vector<std::vector<PricedEdge>> priced_;
    std::vector<double> distances_;
    std::vector<int> parent_;
    std::vector<int> reached_;
    std::vector<Entry> queue_;
};

void BoundedSearch::run(int start, double limit, int target) {
    // The arrays are the width of the world and a search touches a few hundred cells of
    // it, so clearing what the last call dirtied is the difference between work that grows
    // with the route and work that grows with the map.
    for (const int node : reached_) {
        distances_[node] = kInf;
        parent_[node] = -1;
    }
    reached_.clear();
    queue_.clear();

    distances_[start] = 0.0;
    // `start` keeps parent -1 forever -- no positive-cost edge can relax a node at
    // distance 0 -- so it is pushed explicitly here and can never be double-counted.
    reached_.push_back(start);
    queue_.push_back({0.0, start});
    while (!queue_.empty()) {
        std::pop_heap(queue_.begin(), queue_.end(), Later{});
        const Entry top = queue_.back();
        queue_.pop_back();
        if (top.d != distances_[top.node]) continue;  // stale entry, lazy deletion
        if (top.node == target) break;
        for (const PricedEdge& edge : priced_[static_cast<std::size_t>(top.node)]) {
            const double candidate = top.d + edge.cost;
            // Distance first, reach second, and not the other way round: the reach test
            // passes on almost every edge a bounded search looks at and is pure overhead
            // there, while most neighbours of a settled cell are already settled better.
            if (candidate < distances_[static_cast<std::size_t>(edge.node)] && candidate <= limit) {
                // A node enters the reached list the first time it is given a finite
                // distance, which is the first time its parent stops being -1.
                if (parent_[static_cast<std::size_t>(edge.node)] < 0) reached_.push_back(edge.node);
                distances_[static_cast<std::size_t>(edge.node)] = candidate;
                parent_[static_cast<std::size_t>(edge.node)] = top.node;
                queue_.push_back({candidate, edge.node});
                std::push_heap(queue_.begin(), queue_.end(), Later{});
            }
        }
    }
    std::sort(reached_.begin(), reached_.end());
}

// Walk `end` backwards to `start`. Returns the path inclusive of both endpoints, empty the
// moment a parent is negative -- and a ONE-element path when start == end, which the leg
// builder never asks for because it drops a zero-length leg first.
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

// ---------------------------------------------------------------------------
// Hosts, sorted by latitude.
// ---------------------------------------------------------------------------

// A nomad band and a routed herd, read through one shape. Pointers, because the host walk
// runs over a list that is rebuilt once and the underlying records outlive it.
struct Host {
    const std::string* uid = nullptr;
    const std::vector<BeastCamp>* camps = nullptr;
    int node = 0;
};

struct HostIndex {
    std::vector<double> lats;
    std::vector<Host> hosts;
    std::vector<Vec3> dirs;
};

// Ties break on POSITION IN THE CALLER'S LIST so the order is a function of the world and
// not of the sort's stability.
HostIndex build_host_index(const std::vector<Host>& hosts, const Ground& ground) {
    std::vector<double> lats(hosts.size());
    for (std::size_t i = 0; i < hosts.size(); ++i) {
        lats[i] = std::asin(clamp_unit(ground.dir[static_cast<std::size_t>(hosts[i].node)].y));
    }
    std::vector<std::size_t> order(hosts.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return lats[a] != lats[b] ? lats[a] < lats[b] : a < b;
    });
    HostIndex index;
    index.lats.reserve(hosts.size());
    index.hosts.reserve(hosts.size());
    index.dirs.reserve(hosts.size());
    for (const std::size_t i : order) {
        index.lats.push_back(lats[i]);
        index.hosts.push_back(hosts[i]);
        index.dirs.push_back(ground.dir[static_cast<std::size_t>(hosts[i].node)]);
    }
    return index;
}

// ---------------------------------------------------------------------------
// What a builder returns.
// ---------------------------------------------------------------------------

enum class ShapeOutcome : int { Stranded = 0, Solitary, Built };

struct CampSpec {
    int node = 0;
    std::string_view kind;
};

struct LegSpec {
    int from = 0;
    int to = 0;
    std::string_view branch;
};

struct Shape {
    ShapeOutcome outcome = ShapeOutcome::Stranded;
    std::vector<CampSpec> camps;
    std::vector<LegSpec> order;
};

// A follower additionally reports which host it attached to, and claims it even when the
// shape came back empty.
struct BuiltShape {
    std::string host_uid;
    Shape shape;
};

struct Ctx {
    const Ground* ground = nullptr;
    BoundedSearch* search = nullptr;
    double spacing = 0.0;
    double radius = 0.0;
    std::span<const BeastRuin> ruins;
    double year_forage = 1.0;
    HostIndex host_index;
};

// The lexicographic (score, -node) maximum: equal scores pick the LOWEST node index.
bool outscores(double score, int node, double best_score, int best_node) {
    return score != best_score ? score > best_score : node < best_node;
}

// The green wave: high and open for the warm months, sheltered and low for the cold.
//
// Both scores are taken in one pass over the reachable ground, and clamp is spelled out
// rather than called -- it is the same arithmetic in the same order, and these two scorers
// are the innermost loop of the whole pass.
Shape build_seasonal(const BeastGroup& group, Ctx& ctx) {
    const Ground& g = *ctx.ground;
    const int start = group.node;
    const double base = g.height[static_cast<std::size_t>(start)];
    // Loop-invariant: the denominator is the GROUP'S own ground, not the candidate's. The
    // max guards a divide by zero.
    const double span = std::max(1.0, std::abs(base) + kLiftSpanM);
    ctx.search->run(start, kSeasonalReach * ctx.spacing);

    int summer = -1;
    int winter = -1;
    double best_s = 0.0;
    double best_w = 0.0;
    for (const int node : ctx.search->reached()) {
        // The start node is the calving camp and cannot also be a seasonal one: if it
        // were, its leg would collapse to zero length, be dropped, and leave the calving
        // camp with no leg referencing it and so no day window at all.
        if (node == start) continue;
        const std::size_t at = static_cast<std::size_t>(node);
        const double food = g.forage[at];
        const double height = g.height[at];
        // Summer wants height and openness: ground that cannot be used in the cold
        // months, so it is not competing with anyone.
        const double lift = clamp01((height - base) / span);
        const double openness = clamp01(1.0 - g.slope[at] / kOpennessSlopeDegrees);
        const double mark_s = food * (1.0 + lift) * (kOpennessFloor + openness);
        if (summer < 0 || outscores(mark_s, node, best_s, summer)) {
            best_s = mark_s;
            summer = node;
        }
        // Winter wants the opposite and one thing more: snow settles on the flat, so the
        // camps that work are tucked against relief where sun and wind strip the pasture
        // clear.
        const double drop = clamp01((base - height) / span);
        const double warmth = clamp01((g.temperature[at] + kWarmthOffsetC) / kWarmthSpanC);
        const double shelter = clamp01(std::abs(g.tpi[at]) / kShelterTpiM);
        const double mark_w = (kWinterFoodFloor + food) * (1.0 + drop) *
                              (kShelterFloor + shelter) * (kWarmthFloor + warmth);
        if (winter < 0 || outscores(mark_w, node, best_w, winter)) {
            best_w = mark_w;
            winter = node;
        }
    }
    // Returning stranded when the two coincide prevents a degenerate two-camp round, and
    // is the reason the two scorers must differ.
    if (summer < 0 || winter < 0 || summer == winter) return {};
    // Calving is the reason the round has a third stop: moving to it is what buys the
    // young their distance from the predators that follow the herd.
    Shape shape;
    shape.outcome = ShapeOutcome::Built;
    shape.camps = {{start, "calving"}, {summer, "summer"}, {winter, "winter"}};
    shape.order = {{0, 1, "trunk"}, {1, 2, "trunk"}, {2, 0, "trunk"}};
    return shape;
}

// One march out of ground that stopped supporting them, until something does. A swarm does
// not form where conditions are good; it forms where they are marginal and then leaves --
// so the destination is the best forage in reach and the route is ONE WAY.
Shape build_irruption(const BeastGroup& group, Ctx& ctx) {
    const Ground& g = *ctx.ground;
    const int start = group.node;
    ctx.search->run(start, kIrruptionReach * ctx.spacing);
    const std::vector<int>& within = ctx.search->reached();
    if (within.empty()) return {};

    // Plain accumulation, over a list in ascending node order, which is what makes this
    // total reproducible.
    double total = 0.0;
    for (const int node : within) total += g.forage[static_cast<std::size_t>(node)];
    const double local_mean = total / static_cast<double>(within.size());

    // TWO CONDITIONS, and neither on its own is enough. The year scales this year's forage
    // against an ordinary one, one draw for the whole world, so a bad year is a bad year
    // for everybody. The ground is the group's own cell against the mean of what a march
    // can reach: marginal ground is ground that falls short of its NEIGHBOURHOOD, because
    // a swarm forms where conditions turn rather than where they were always poor -- and
    // because an absolute floor is a forage constant, which would mean something different
    // in every biome.
    //
    // `within` includes `start`, so `start` contributes to the mean it is compared against.
    if (g.forage[static_cast<std::size_t>(start)] * ctx.year_forage >= kIrruptionMargin * local_mean) {
        // Sparse and not going anywhere, which is the NORMAL state for this class. It is
        // not `stranded`: that means no reachable ground to march to.
        Shape shape;
        shape.outcome = ShapeOutcome::Solitary;
        return shape;
    }

    int target = -1;
    double best = 0.0;
    for (const int node : within) {
        if (node == start) continue;
        const double mark = g.forage[static_cast<std::size_t>(node)];
        if (target < 0 || outscores(mark, node, best, target)) {
            best = mark;
            target = node;
        }
    }
    if (target < 0) return {};
    Shape shape;
    shape.outcome = ShapeOutcome::Built;
    shape.camps = {{start, "base"}, {target, "terminal"}};
    shape.order = {{0, 1, "trunk"}};
    return shape;
}

// A circuit between the places that made them: ruins, and ground that remembers. These are
// the dead nobody buried, so the route has no seasonal logic at all.
Shape build_drift(const BeastGroup& group, Ctx& ctx) {
    const int start = group.node;
    const double reach = kDriftReach * ctx.spacing;
    ctx.search->run(start, reach);
    const std::vector<double>& distances = ctx.search->distances();
    const std::size_t nodes = ctx.ground->dir.size();

    // (walk distance, ruin uid as a STRING, node). The uid is an explicit tie-break, so
    // two ruins the same walk away resolve by name rather than by list order.
    struct Haunt {
        double distance = 0.0;
        std::string_view uid;
        int node = 0;
    };
    std::vector<Haunt> haunts;
    for (const BeastRuin& ruin : ctx.ruins) {
        const int node = ruin.node;
        // A stale ruin from a different raster is dropped rather than crashing.
        if (node < 0 || node == start || static_cast<std::size_t>(node) >= nodes) continue;
        const double d = distances[static_cast<std::size_t>(node)];
        if (std::isfinite(d) && d <= reach) haunts.push_back({d, ruin.uid, node});
    }
    std::sort(haunts.begin(), haunts.end(), [](const Haunt& a, const Haunt& b) {
        return std::tie(a.distance, a.uid, a.node) < std::tie(b.distance, b.uid, b.node);
    });
    if (haunts.empty()) return {};

    Shape shape;
    shape.outcome = ShapeOutcome::Built;
    shape.camps.push_back({start, "base"});
    for (std::size_t i = 0; i < haunts.size() && i < kDriftHaunts; ++i) {
        shape.camps.push_back({haunts[i].node, "terminal"});
    }
    const int last = static_cast<int>(shape.camps.size()) - 1;
    for (int i = 0; i < last; ++i) shape.order.push_back({i, i + 1, "trunk"});
    shape.order.push_back({last, 0, "trunk"});  // the circuit closes back to the base
    return shape;
}

// A follower does not search; it derives. This is the class that closes the loop the whole
// feature is for: predators and scavengers walking the routes that nomads and herds
// already walk.
//
// Finding the host is an outward LATITUDE WALK rather than a scan. By the time followers
// are built nearly everything else in the block is a host, so comparing each follower
// against every host is quadratic in the size of the block. The bound is that the angle
// between two directions is at least the difference of their latitudes, so with the hosts
// sorted by latitude and taken nearest-latitude first, from whichever side has the smaller
// remaining gap, everything still unvisited is at least that far away.
BuiltShape build_follow(const BeastGroup& group, Ctx& ctx) {
    const Ground& g = *ctx.ground;
    const int start = group.node;
    const double reach = kFollowReach * ctx.spacing;
    const double bias_nomad = beast_host_bias(group.role, "nomad");
    const double bias_beast = beast_host_bias(group.role, "beastmove");
    // An unknown host kind falls back to one, so the floor has to admit one as well.
    const double floor = std::min({bias_nomad, bias_beast, 1.0});

    const HostIndex& index = ctx.host_index;
    const std::size_t total = index.lats.size();
    const Vec3 here = g.dir[static_cast<std::size_t>(start)];
    const double phi = std::asin(clamp_unit(here.y));
    std::size_t left =
        static_cast<std::size_t>(std::lower_bound(index.lats.begin(), index.lats.end(), phi) -
                                 index.lats.begin());
    std::size_t right = left;
    const double slack = ctx.radius * kBeastWalkSlack;

    bool have_best = false;
    double best_mark = 0.0;
    std::string_view best_uid;
    const Host* best_host = nullptr;
    while (true) {
        const bool has_left = left > 0;
        const bool has_right = right < total;
        if (!has_left && !has_right) break;
        const double gap_l = has_left ? phi - index.lats[left - 1] : 0.0;
        const double gap_r = has_right ? index.lats[right] - phi : 0.0;
        double gap = 0.0;
        std::size_t at = 0;
        // Ties go LEFT. The winner is order-independent -- it is the unique minimum of
        // (span * bias, uid) -- but the STOPPING POINT is not: a different walk order can
        // stop later and cost more, never less.
        if (!has_right || (has_left && gap_l <= gap_r)) {
            gap = gap_l;
            --left;
            at = left;
        } else {
            gap = gap_r;
            at = right;
            ++right;
        }
        const double floor_span = ctx.radius * gap;
        // Stop one: the reach. A host whose latitude alone puts it past the reach cannot
        // pass the cut, and neither can anything behind it.
        if (floor_span > reach + slack) break;
        // Stop two: the mark. The winner is the smallest span * bias and bias is never
        // above one, so a host at latitude gap g cannot mark below radius * g * bias_min;
        // once that floor passes the best mark already found, nothing left can win.
        if (have_best && floor_span * floor > best_mark + slack) break;

        // Great-circle distance, NOT walking distance: a follower can attach to a host it
        // could not walk to. And the cut is on the RAW span, before the bias -- a bias
        // below one shortens the effective distance, so a preferred host wins from further
        // away without ever letting an unreachable one through.
        const double span = great_circle(here, index.dirs[at], ctx.radius);
        if (span > reach) continue;  // no slack on this one
        const Host& host = index.hosts[at];
        const bool is_nomad = host.uid->starts_with("nomad-");
        const double mark = span * (is_nomad ? bias_nomad : bias_beast);
        const std::string_view uid = *host.uid;
        // A STRING tie-break on host uid: "beastmove-..." sorts before "nomad-" in ASCII,
        // so a tie between a herd and a band goes to the herd.
        const bool wins = !have_best || (mark != best_mark ? mark < best_mark : uid < best_uid);
        if (wins) {
            have_best = true;
            best_mark = mark;
            best_uid = uid;
            best_host = &host;
        }
    }
    if (!have_best) return {};

    BuiltShape built;
    built.host_uid = *best_host->uid;
    // The remap folds summer, winter, lair, shrine, base and calving all to `base`, and
    // only station/terminal to `terminal`. The follower's OWN start node is never a camp.
    std::vector<CampSpec> camps;
    camps.reserve(best_host->camps->size());
    for (const BeastCamp& camp : *best_host->camps) {
        const bool terminal = camp.kind == "station" || camp.kind == "terminal";
        camps.push_back({camp.node, terminal ? "terminal" : "base"});
    }
    // The uid is claimed even when the circuit is too short to derive from, because the
    // follower did find a host -- it is the host's route that failed it.
    if (camps.size() < 2) return built;
    built.shape.outcome = ShapeOutcome::Built;
    built.shape.camps = std::move(camps);
    const int count = static_cast<int>(built.shape.camps.size());
    for (int i = 0; i < count; ++i) {
        built.shape.order.push_back({i, (i + 1) % count, "trunk"});  // a closed cycle
    }
    return built;
}

// ---------------------------------------------------------------------------
// Camps and the calendar.
// ---------------------------------------------------------------------------

BeastCamp make_camp(const BeastGroup& group, int index, int node, std::string_view kind,
                    const Ground& ground) {
    BeastCamp camp;
    camp.id = group.uid + "-camp-" + std::to_string(index);
    camp.node = node;
    camp.direction = ground.dir[static_cast<std::size_t>(node)];
    camp.kind = std::string(kind);
    return camp;
}

// Walk the circuit assigning arrival and departure, anchored on the calendar.
//
// A herding round is anchored so the WINTER camp is occupied at midwinter -- that is the
// camp survival depends on, so it is the one the year is built around. Everything else
// starts at its own midsummer and runs on travel time, which is length over speed.
void assign_days(const BeastGroup& group, std::vector<BeastCamp>& camps,
                 std::vector<BeastLeg>& legs, const Ground& ground, int n) {
    const double speed = std::max(1.0, group.speed_m_per_day);  // guards a divide by zero
    long long total_travel = 0;
    for (const BeastLeg& leg : legs) {
        // Every leg costs at least one day, even a ten-metre one.
        total_travel += std::max(1LL, round_half_away(leg.length_m / speed));
    }
    double weight = 0.0;
    std::vector<double> shares(camps.size());
    for (std::size_t i = 0; i < camps.size(); ++i) {
        shares[i] = beast_camp_rest(camps[i].kind);
        weight += shares[i];
    }
    // A circuit whose travel exceeds a year still leaves at least one day per camp, so the
    // rest table is never empty.
    const double spare =
        std::max(static_cast<double>(camps.size()), static_cast<double>(kDaysPerYear) - static_cast<double>(total_travel));
    std::map<std::string, int> rest_of;
    for (std::size_t i = 0; i < camps.size(); ++i) {
        const int held = std::max(1, static_cast<int>(spare * shares[i] / weight));
        const std::optional<int> ceiling = beast_camp_rest_ceiling(camps[i].kind);
        rest_of[camps[i].id] = ceiling ? std::min(held, *ceiling) : held;
    }

    const BeastCamp* winter = nullptr;
    for (const BeastCamp& camp : camps) {
        if (camp.kind == "winter") {
            winter = &camp;
            break;
        }
    }
    const long long anchor =
        winter != nullptr
            ? midwinter_day(ground.z[static_cast<std::size_t>(winter->node)], n)
            : midsummer_day(ground.z[static_cast<std::size_t>(group.node)], n);

    std::map<std::string, BeastCamp*> by_id;
    for (BeastCamp& camp : camps) by_id[camp.id] = &camp;

    // Rotate so the clock starts at the winter camp. With duplicate camps this could match
    // more than one leg; the first wins.
    std::vector<BeastLeg*> order;
    order.reserve(legs.size());
    std::size_t rotate = 0;
    if (winter != nullptr) {
        for (std::size_t k = 0; k < legs.size(); ++k) {
            if (legs[k].from == winter->id) {
                rotate = k;
                break;
            }
        }
    }
    for (std::size_t k = 0; k < legs.size(); ++k) order.push_back(&legs[(k + rotate) % legs.size()]);

    long long day = anchor;
    if (!order.empty()) {
        const auto it = by_id.find(order.front()->from);
        if (it != by_id.end()) {
            BeastCamp& first = *it->second;
            first.arrive_day = day_mod(day, kDaysPerYear);
            first.depart_day = day_mod(day + rest_of.at(first.id), kDaysPerYear);
        }
    }
    for (BeastLeg* leg : order) {
        const long long travel = std::max(1LL, round_half_away(leg->length_m / speed));
        const auto held_it = rest_of.find(leg->from);
        const int held = held_it == rest_of.end() ? 1 : held_it->second;
        leg->depart_day = day_mod(day + held, kDaysPerYear);
        day += held + travel;
        leg->arrive_day = day_mod(day, kDaysPerYear);
        const auto it = by_id.find(leg->to);
        if (it != by_id.end() && !it->second->arrive_day.has_value()) {
            BeastCamp& camp = *it->second;
            const auto rest_it = rest_of.find(camp.id);
            camp.arrive_day = day_mod(day, kDaysPerYear);
            camp.depart_day = day_mod(day + (rest_it == rest_of.end() ? 1 : rest_it->second), kDaysPerYear);
        }
    }
    // A camp that is only ever a leg's SOURCE -- the base a transhumant group holds while
    // the herd moves -- is never reached by the loop above and would carry no window at
    // all. It is occupied from when the moving limb leaves it until the limb returns.
    for (BeastLeg* leg : order) {
        const auto it = by_id.find(leg->from);
        if (it == by_id.end()) continue;
        BeastCamp& camp = *it->second;
        if (camp.arrive_day.has_value() || !leg->depart_day.has_value()) continue;
        const auto rest_it = rest_of.find(camp.id);
        const int held = rest_it == rest_of.end() ? 1 : rest_it->second;
        camp.depart_day = *leg->depart_day;
        camp.arrive_day = day_mod(static_cast<long long>(*leg->depart_day) - held, kDaysPerYear);
    }
}

// ---------------------------------------------------------------------------
// Which sites move, and in what order.
// ---------------------------------------------------------------------------

struct EligibleSite {
    const NestSite* site = nullptr;
    const CreatureProfile* profile = nullptr;
    std::string_view source;
    BeastMovement movement = BeastMovement::Migratory;
};

const CreatureProfile* profile_for(std::string_view species_id) {
    static const std::map<std::string_view, const CreatureProfile*> catalogue = [] {
        std::map<std::string_view, const CreatureProfile*> out;
        for (const CreatureProfile& p : creature_profiles()) out.emplace(p.id, &p);
        return out;
    }();
    const auto it = catalogue.find(species_id);
    return it == catalogue.end() ? nullptr : it->second;
}

// The two source lists are merged and then sorted TOGETHER. Because a species id belongs
// to exactly one catalogue half, `source` never affects the order -- but the order decides
// which sites survive the thinning, so it is a contract either way.
std::vector<EligibleSite> site_groups(const BeastMovementRequest& request) {
    std::vector<EligibleSite> out;
    const auto collect = [&](std::span<const NestSite> sites, std::string_view source) {
        for (const NestSite& site : sites) {
            const CreatureProfile* profile = profile_for(site.species_id);
            if (profile == nullptr) continue;  // not in the catalogue: silently skipped
            const std::optional<BeastMovement> movement = beast_movement_of(profile->movement);
            if (!movement) continue;
            out.push_back({&site, profile, source, *movement});
        }
    };
    collect(request.wildlife, "wildlife");
    collect(request.beast_nests, "beast_nests");
    std::sort(out.begin(), out.end(), [](const EligibleSite& a, const EligibleSite& b) {
        // A STRING comparison then an integer.
        return a.site->species_id != b.site->species_id ? a.site->species_id < b.site->species_id
                                                        : a.site->node < b.site->node;
    });
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// The authored tables.
// ---------------------------------------------------------------------------

double beast_speed_m_per_day(std::string_view size) {
    if (size == "tiny") return 26000.0;
    if (size == "small") return 21000.0;
    if (size == "medium") return 17000.0;
    if (size == "large") return 14000.0;
    if (size == "huge") return 10000.0;
    if (size == "colossal") return 7000.0;
    return 15000.0;  // unreachable: all six are authored on every row
}

int beast_herd_size(int tier) {
    switch (tier) {
        case 1: return 900;
        case 2: return 400;
        case 3: return 160;
        case 4: return 60;
        case 5: return 12;
        default: return 40;  // unreachable: tiers are validated 1..5
    }
}

double beast_spread_per_head_m(std::string_view size) {
    if (size == "tiny") return 0.05;
    if (size == "small") return 0.4;
    if (size == "medium") return 1.2;
    if (size == "large") return 3.0;
    if (size == "huge") return 8.0;
    if (size == "colossal") return 20.0;
    return 1.0;
}

double beast_host_bias(std::string_view role, std::string_view host_kind) {
    // Every row biases `beastmove` at 1.0; only the pull toward people varies.
    //
    // PORTED VERBATIM, INCLUDING ITS TWO DEFECTS. `bird_of_prey` is authored but no
    // follower in the catalogue carries that role, so the row is dead. `pursuit_predator`
    // -- 7 of the 17 followers, the second-largest group -- is NOT in the table and falls
    // through to the default 0.85, although the stated intent is that hunters attach to
    // herds. Whether that is deliberate or an authoring oversight is recorded nowhere, so
    // it is not silently "fixed" here.
    if (host_kind != "nomad") return 1.0;
    if (role == "scavenger") return 0.45;
    if (role == "bird_of_prey") return 0.8;
    if (role == "pack_predator") return 0.7;
    return 0.85;
}

double beast_camp_rest(std::string_view kind) {
    if (kind == "winter") return 5.0;
    if (kind == "summer") return 4.0;
    if (kind == "base") return 3.0;
    if (kind == "lair") return 5.0;
    if (kind == "shrine") return 2.0;
    if (kind == "station") return 1.0;
    if (kind == "terminal") return 0.35;
    if (kind == "calving") return 3.0;
    return 1.0;
}

std::optional<int> beast_camp_rest_ceiling(std::string_view kind) {
    if (kind == "terminal") return 3;
    if (kind == "station") return 6;
    if (kind == "shrine") return 21;
    return std::nullopt;
}

int midsummer_day(int z, int n) {
    const double latitude = 90.0 - 180.0 * static_cast<double>(z) / static_cast<double>(n - 1);
    const double phase = latitude >= 0.0 ? kMidsummerNorth : kMidsummerNorth + 0.5;
    const double fraction = phase - std::floor(phase);
    return day_mod(round_half_away(fraction * kDaysPerYear), kDaysPerYear);
}

int midwinter_day(int z, int n) {
    return (midsummer_day(z, n) + kDaysPerYear / 2) % kDaysPerYear;
}

std::optional<BeastMovement> beast_movement_of(std::string_view movement) {
    if (movement == "migratory") return BeastMovement::Migratory;
    if (movement == "irruptive") return BeastMovement::Irruptive;
    if (movement == "follower") return BeastMovement::Follower;
    if (movement == "drifter") return BeastMovement::Drifter;
    return std::nullopt;  // `nester`, and anything the catalogue does not name
}

std::string_view to_string(BeastMovement movement) {
    switch (movement) {
        case BeastMovement::Migratory: return "migratory";
        case BeastMovement::Irruptive: return "irruptive";
        case BeastMovement::Follower: return "follower";
        case BeastMovement::Drifter: break;
    }
    return "drifter";
}

std::string_view to_string(BeastRouteStatus status) {
    switch (status) {
        case BeastRouteStatus::Routed: return "routed";
        case BeastRouteStatus::Solitary: return "solitary";
        case BeastRouteStatus::Stranded: break;
    }
    return "stranded";
}

bool beast_counts_balance(const BeastMovements& block) {
    return static_cast<std::size_t>(block.routed + block.stranded + block.solitary) ==
           block.groups.size();
}

int nomad_threat_tier(std::string_view classification) {
    if (classification == "merchants") return 1;
    if (classification == "survivors") return 1;
    if (classification == "wanderers") return 2;
    if (classification == "cultists") return 3;
    if (classification == "deserters") return 3;
    if (classification == "bandits") return 4;
    return 2;
}

std::string_view medium_domain(CreatureMedium medium) {
    switch (medium) {
        case CreatureMedium::Marine: return "ocean";
        case CreatureMedium::Freshwater: return "lake";
        case CreatureMedium::Shore:
        case CreatureMedium::Land: break;
    }
    return "land";
}

std::vector<int> window_months(std::optional<int> start, std::optional<int> end, int days_per_year) {
    if (!start.has_value() || !end.has_value()) return {};
    // Both endpoints are INCLUSIVE, so a camp held for one day still occupies a whole month.
    const auto month_of = [&](int day) {
        return std::min(11, static_cast<int>(static_cast<long long>(day) * 12 / days_per_year));
    };
    const int first = month_of(day_mod(*start, days_per_year));
    const int last = month_of(day_mod(*end, days_per_year));
    std::vector<int> months;
    if (first <= last) {
        for (int m = first; m <= last; ++m) months.push_back(m);
        return months;
    }
    for (int m = first; m < 12; ++m) months.push_back(m);  // wraps the turn of the year
    for (int m = 0; m <= last; ++m) months.push_back(m);
    return months;
}

// ---------------------------------------------------------------------------
// The movement pass.
// ---------------------------------------------------------------------------

std::optional<BeastMovements> add_beast_movements(const BeastMovementRequest& request) {
    if (request.world_recipe == 0 || request.phase < 16) return std::nullopt;
    if (request.grid == nullptr || request.layers == nullptr) {
        throw std::invalid_argument(
            "add_beast_movements needs both a sphere grid and the world's layers; the "
            "priced graph is built from the two together");
    }
    const SphereGrid& grid = *request.grid;
    if (std::abs(grid.radius - request.globe_radius_m) >
        1e-6 * std::max(1.0, request.globe_radius_m)) {
        throw std::invalid_argument(
            "add_beast_movements was given a grid of radius " + std::to_string(grid.radius) +
            " m but this world is " + std::to_string(request.globe_radius_m) +
            " m across; a design-space grid would compare the four reaches -- all absolute "
            "metres against an already-scaled settlement spacing -- with design units");
    }

    const Ground ground = build_ground(*request.layers, grid);
    BoundedSearch search(price_edges(grid, ground, request.road_max_grade));

    Ctx ctx;
    ctx.ground = &ground;
    ctx.search = &search;
    ctx.spacing = request.settlement_spacing_m;
    ctx.radius = request.globe_radius_m;
    ctx.ruins = request.ruins;
    // ONE DRAW FOR THE WHOLE WORLD, keyed on the GENESIS seed. This pass runs monthly, so
    // a factor drawn from the stepped seed would be twelve different weathers inside one
    // year and a swarm that formed and dissolved with the call rather than with the world.
    ctx.year_forage = Rng::child(request.genesis_seed, "beast-year", request.year)
                          .uniform(kIrruptionYearLow, kIrruptionYearHigh);
    // The host index is deliberately NOT built here. Every follower is deferred until
    // after the herds exist, and the index is rebuilt at that point -- so a nomads-only
    // index built now would be constructed and discarded unread. (The reference builds it
    // anyway; that is dead work, not a behaviour.)

    // Wildlife is dense -- thousands of sites -- so routing every eligible one makes the
    // block enormous for little added meaning. The share is a DETERMINISTIC THINNING over
    // an ordered list, not a cap, so it stays proportional to the ground rather than to
    // whatever happened to be placed first.
    Rng rng = Rng::child(request.seed, "beast-movement-v1", request.nomad_variation);
    const double share = request.share;

    std::vector<std::pair<BeastGroup, BeastMovement>> groups;
    std::vector<std::pair<BeastGroup, BeastMovement>> deferred;
    int considered = 0;
    int skipped = 0;
    for (const EligibleSite& eligible : site_groups(request)) {
        ++considered;
        // NOTE the short circuit: at share 1.0 no draw is made at all, so the stream with
        // share 1.0 and the stream with 1.0 minus epsilon are not the same sequence.
        if (share < 1.0 && rng.random() >= share) {
            ++skipped;
            continue;
        }
        const NestSite& site = *eligible.site;
        const CreatureProfile& profile = *eligible.profile;
        const int size = std::max(2, beast_herd_size(profile.tier));
        BeastGroup group;
        group.uid = "beastmove-" + std::string(site.species_id) + "-" + std::to_string(site.node);
        group.species_id = site.species_id;
        group.name = site.name;
        group.movement = eligible.movement;
        // Everything below the name comes from the CATALOGUE PROFILE, not from the site:
        // the site carries its own copies and this pass ignores them.
        group.family = profile.family;
        group.tier = profile.tier;
        group.kind = profile.creature_class;
        group.source = eligible.source;
        group.node = site.node;
        group.x = site.x;
        group.z = site.z;
        group.direction = site.direction;
        group.size = size;
        group.speed_m_per_day = beast_speed_m_per_day(profile.size);
        group.column_length_m = round6(size * beast_spread_per_head_m(profile.size));
        group.disposition = profile.creature_class == CreatureClass::Monster ? "hostile" : "wary";
        group.role = profile.role;
        group.route_status = BeastRouteStatus::Stranded;
        // Followers derive from whatever already walks, and the herds do not exist yet.
        (eligible.movement == BeastMovement::Follower ? deferred : groups)
            .emplace_back(std::move(group), eligible.movement);
    }

    // A circuit is a property of the GROUND a group stands on, not of the group: nothing
    // any builder reads varies between two groups on one cell of one movement class -- a
    // follower additionally reads its role, because that is what biases its choice of
    // host. What cannot be shared is the naming: the camp ids carry the group's own uid
    // and the calendar runs on the group's own speed.
    //
    // The map distinguishes "not built" from "built and returned nothing" by presence
    // alone. Getting that wrong makes a stranded group rebuild its failing search once per
    // group, which is the quadratic this cache exists to remove.
    std::map<std::tuple<int, int, std::string_view>, BuiltShape> shapes;
    // One traced route per ORDERED pair of camps, so (a,b) and (b,a) are separate entries
    // and are traced independently -- they can pick different equal-cost paths, though
    // their lengths are equal because the cost function is symmetric.
    struct Route {
        std::vector<int> nodes;
        double length = 0.0;
    };
    std::map<std::pair<int, int>, Route> routes;

    int stranded = 0;
    int solitary = 0;

    const auto route_between = [&](int from, int to) -> const Route& {
        const auto key = std::make_pair(from, to);
        const auto found = routes.find(key);
        if (found != routes.end()) return found->second;
        search.run(from, kInf, to);  // no reach limit
        Route built;
        built.nodes = trace(search.parent(), from, to);
        // Great-circle metres along the node path, accumulated plainly.
        for (std::size_t i = 0; i + 1 < built.nodes.size(); ++i) {
            built.length += great_circle(ground.dir[static_cast<std::size_t>(built.nodes[i])],
                                         ground.dir[static_cast<std::size_t>(built.nodes[i + 1])],
                                         request.globe_radius_m);
        }
        return routes.emplace(key, std::move(built)).first->second;
    };

    const auto build = [&](BeastGroup& group, BeastMovement movement) {
        const std::string_view role_key =
            movement == BeastMovement::Follower ? group.role : std::string_view{};
        const auto key = std::make_tuple(static_cast<int>(movement), group.node, role_key);
        auto cached = shapes.find(key);
        if (cached == shapes.end()) {
            BuiltShape built;
            switch (movement) {
                case BeastMovement::Migratory: built.shape = build_seasonal(group, ctx); break;
                case BeastMovement::Irruptive: built.shape = build_irruption(group, ctx); break;
                case BeastMovement::Drifter: built.shape = build_drift(group, ctx); break;
                case BeastMovement::Follower: built = build_follow(group, ctx); break;
            }
            cached = shapes.emplace(key, std::move(built)).first;
        }
        const BuiltShape& built = cached->second;
        if (movement == BeastMovement::Follower) group.host_uid = built.host_uid;

        const auto strand_at_home = [&](BeastRouteStatus status) {
            // A solitary or stranded group keeps a base camp so the encounter index still
            // places it: a player can meet the solitary phase, just not a swarm on the
            // march.
            group.route_status = status;
            group.legs.clear();
            group.camps.clear();
            group.camps.push_back(make_camp(group, 0, group.node, "base", ground));
        };
        if (built.shape.outcome == ShapeOutcome::Solitary) {
            strand_at_home(BeastRouteStatus::Solitary);
            ++solitary;
            return;
        }
        if (built.shape.outcome != ShapeOutcome::Built) {
            strand_at_home(BeastRouteStatus::Stranded);
            ++stranded;
            return;
        }

        std::vector<BeastCamp> camps;
        camps.reserve(built.shape.camps.size());
        for (std::size_t i = 0; i < built.shape.camps.size(); ++i) {
            camps.push_back(make_camp(group, static_cast<int>(i), built.shape.camps[i].node,
                                      built.shape.camps[i].kind, ground));
        }
        std::vector<BeastLeg> legs;
        for (const LegSpec& spec : built.shape.order) {
            const BeastCamp& src = camps[static_cast<std::size_t>(spec.from)];
            const BeastCamp& dst = camps[static_cast<std::size_t>(spec.to)];
            if (src.node == dst.node) continue;  // a zero-length leg is dropped
            const Route& traced = route_between(src.node, dst.node);
            if (traced.nodes.empty()) continue;  // the destination was unreachable
            BeastLeg leg;
            leg.from = src.id;
            leg.to = dst.id;
            leg.nodes = traced.nodes;  // copied per leg, so two groups never share one list
            leg.length_m = traced.length;
            leg.branch = std::string(spec.branch);
            legs.push_back(std::move(leg));
        }
        if (legs.empty()) {
            strand_at_home(BeastRouteStatus::Stranded);
            ++stranded;
            return;
        }
        assign_days(group, camps, legs, ground, grid.n);
        double round_length = 0.0;
        for (const BeastLeg& leg : legs) round_length += leg.length_m;
        group.camps = std::move(camps);
        group.legs = std::move(legs);
        group.route_status = BeastRouteStatus::Routed;
        group.round_length_m = round6(round_length);
    };

    for (auto& [group, movement] : groups) build(group, movement);

    // Herds now exist, so a follower may attach to one as readily as to a nomad band. The
    // concatenation order -- BANDS FIRST, in their own order, then non-follower beast
    // groups in site order (NOT uid order) -- is the tie-break inside the host index.
    // Followers are never hosts (they are all deferred), and solitary or stranded groups
    // are never hosts (no legs).
    std::vector<Host> hosts;
    for (const EncounterBand& band : request.bands) {
        if (band.has_legs) hosts.push_back({&band.uid, &band.camps, band.node});
    }
    for (const auto& [group, movement] : groups) {
        (void)movement;
        if (!group.legs.empty()) hosts.push_back({&group.uid, &group.camps, group.node});
    }
    ctx.host_index = build_host_index(hosts, ground);
    for (auto& [group, movement] : deferred) build(group, movement);

    BeastMovements block;
    block.groups.reserve(groups.size() + deferred.size());
    for (auto& [group, movement] : groups) {
        (void)movement;
        block.groups.push_back(std::move(group));
    }
    for (auto& [group, movement] : deferred) {
        (void)movement;
        block.groups.push_back(std::move(group));
    }
    // A LEXICOGRAPHIC STRING sort, so node numbers sort as text: beastmove-wolf-10
    // precedes beastmove-wolf-9. This is the published order and it propagates into the
    // encounter entries and thence into every occupancy index. Stable, so a duplicate uid
    // -- which the site ids make impossible, but nothing here proves -- keeps site order.
    std::stable_sort(block.groups.begin(), block.groups.end(),
                     [](const BeastGroup& a, const BeastGroup& b) { return a.uid < b.uid; });

    // Built by walking the uid-sorted list, so the key order is the order of FIRST
    // APPEARANCE and not an alphabetical one.
    for (const BeastGroup& group : block.groups) {
        const std::string_view name = to_string(group.movement);
        const auto it = std::find_if(block.counts.begin(), block.counts.end(),
                                     [&](const auto& row) { return row.first == name; });
        if (it == block.counts.end()) {
            block.counts.emplace_back(name, 1);
        } else {
            ++it->second;
        }
        if (group.route_status == BeastRouteStatus::Routed) ++block.routed;
    }
    block.considered = considered;
    block.skipped = skipped;
    block.share = share;
    block.stranded = stranded;
    block.solitary = solitary;
    block.year = request.year;
    block.year_forage_factor = round6(ctx.year_forage);
    block.method = kMovementsMethod;
    block.limits = kMovementsLimits;
    return block;
}

// ---------------------------------------------------------------------------
// The encounter index.
// ---------------------------------------------------------------------------

namespace {

struct SpeciesFacts {
    std::string_view domain;
    CreatureClass creature_class = CreatureClass::Animal;
    bool airborne = false;
};

const SpeciesFacts* species_facts(std::string_view species_id) {
    static const std::map<std::string_view, SpeciesFacts> facts = [] {
        std::map<std::string_view, SpeciesFacts> out;
        for (const CreatureProfile& p : creature_profiles()) {
            out.emplace(p.id, SpeciesFacts{medium_domain(p.medium), p.creature_class, p.sky});
        }
        return out;
    }();
    const auto it = facts.find(species_id);
    return it == facts.end() ? nullptr : &it->second;
}

std::string_view class_name(CreatureClass which) {
    return which == CreatureClass::Monster ? "monster" : "animal";
}

EncounterEntry band_entry(const EncounterBand& band) {
    EncounterEntry entry;
    entry.group_uid = band.uid;
    entry.source = "nomads";
    entry.kind = band.classification;
    entry.domain = "land";
    entry.entity_class = "people";
    entry.airborne = false;
    entry.disposition = band.disposition;
    entry.threat_tier = nomad_threat_tier(band.classification);
    entry.size = band.size;
    entry.carries = band.carries;
    entry.seeks = band.seeks;
    entry.god_id = band.god_id;
    entry.speed_m_per_day = band.speed_m_per_day;
    entry.column_length_m = band.column_length_m;
    return entry;
}

EncounterEntry beast_entry(const BeastGroup& group) {
    EncounterEntry entry;
    entry.group_uid = group.uid;
    entry.source = "beast_movements";
    entry.kind = std::string(group.species_id);
    const SpeciesFacts* facts = species_facts(group.species_id);
    // The DOMAIN is resolved from the species' authored medium, not from the terrain under
    // the camp: a group's camps move, what it can live in does not, and a marine group
    // camped at a coastal node is still not something a party walks up to.
    //
    // The fallback uses the group's own class rather than a constant, so a species the
    // catalogue no longer names does not silently read as an animal.
    entry.domain = facts != nullptr ? std::string(facts->domain) : "land";
    entry.entity_class =
        std::string(class_name(facts != nullptr ? facts->creature_class : group.kind));
    entry.airborne = facts != nullptr && facts->airborne;
    entry.disposition = std::string(group.disposition);
    entry.threat_tier = group.tier;
    entry.size = group.size;
    // A herd is worth something to whoever can take it; a swarm is worth avoiding.
    if (group.kind == CreatureClass::Animal) entry.carries = {"hides", "meat"};
    switch (group.movement) {
        case BeastMovement::Migratory: entry.seeks = {"pasture"}; break;
        case BeastMovement::Irruptive: entry.seeks = {"anything"}; break;
        case BeastMovement::Follower: entry.seeks = {"the group ahead"}; break;
        case BeastMovement::Drifter: entry.seeks = {"rest"}; break;
    }
    entry.host_uid = group.host_uid;
    entry.speed_m_per_day = group.speed_m_per_day;
    entry.column_length_m = group.column_length_m;
    return entry;
}

}  // namespace

std::optional<EncounterIndex> add_encounters(const EncounterRequest& request) {
    if (request.world_recipe == 0 || request.phase < 16) return std::nullopt;

    EncounterIndex index;
    index.method = kEncountersMethod;
    index.limits = kEncountersLimits;

    // Occupancy is recorded at CAMPS, where a group dwells, not along legs. A group in
    // transit is derivable from its leg node path and its speed; recording every node of
    // every leg would multiply the block by the length of the walking. Where a group is
    // camped is the thing you cannot derive.
    const auto record = [&](EncounterEntry entry, const std::vector<BeastCamp>& camps) {
        if (camps.empty()) return;  // no camps -> no entry, and no index slot consumed
        const int at = static_cast<int>(index.entries.size());
        index.entries.push_back(std::move(entry));
        for (const BeastCamp& camp : camps) {
            EncounterOccupancy occupancy;
            occupancy.entry = at;
            occupancy.node = camp.node;
            occupancy.camp_id = camp.id;
            occupancy.camp_kind = camp.kind;
            occupancy.from_day = camp.arrive_day;
            occupancy.to_day = camp.depart_day;
            occupancy.months = window_months(camp.arrive_day, camp.depart_day, kDaysPerYear);
            index.occupancy.push_back(std::move(occupancy));
        }
    };
    // Nomads FIRST, then beast movements. Entry indices are assigned in that order, so
    // every index below depends on it.
    for (const EncounterBand& band : request.bands) record(band_entry(band), band.camps);
    for (const BeastGroup& group : request.groups) record(beast_entry(group), group.camps);

    std::map<int, std::vector<int>> by_node;
    for (std::size_t i = 0; i < index.occupancy.size(); ++i) {
        const EncounterOccupancy& occupancy = index.occupancy[i];
        for (const int month : occupancy.months) {
            index.by_month[static_cast<std::size_t>(month)].push_back(static_cast<int>(i));
        }
        by_node[occupancy.node].push_back(static_cast<int>(i));
    }
    // Ascending by NODE. Keyed on the published string and ordered lexicographically,
    // "10" would come before "9".
    index.by_node.reserve(by_node.size());
    for (auto& [node, list] : by_node) index.by_node.emplace_back(node, std::move(list));
    return index;
}

}  // namespace fwg
