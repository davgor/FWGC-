// The things that walk, and the index that finds them.
//
// Most of what this file asserts is checked against a BRUTE-FORCE ORACLE built in the
// test: a dense, unbounded, heap-free Dijkstra over the same ground, and the two scorers
// written out again over its whole reachable set. That is deliberate. The pass's search is
// bounded, targeted and keeps its arrays between calls, and a test that only asserted "a
// route came out" would pass against every one of those three being wrong.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <string>
#include <vector>

#include "fwg/beasts.hpp"
#include "fwg/scale.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kInf = std::numeric_limits<double>::infinity();

// A 200 km world: the smallest shipped preset, and the one whose scaled settlement
// spacing is smaller than a cell at this size, so the reaches bite.
constexpr double kCircumferenceM = 200000.0;
constexpr int kSize = 33;

Grid make_grid(int n, double (*f)(int, int)) {
    Grid g(n, 0.0);
    for (int z = 0; z < n; ++z) {
        std::vector<double> row(static_cast<std::size_t>(distinct_count(z, n)));
        for (std::size_t x = 0; x < row.size(); ++x) row[x] = f(static_cast<int>(x), z);
        g.fill_row(z, row);
    }
    return g;
}

double lon_of(int x) { return 2.0 * kPi * static_cast<double>(x) / static_cast<double>(kSize - 1); }

double ground_height(int x, int z) { return 400.0 * std::sin(lon_of(x)) + 5.0 * z; }
double ground_forage(int x, int) { return 0.5 + 0.45 * std::sin(lon_of(x) + 1.0); }
double ground_slope(int x, int) { return 10.0 + 10.0 * std::cos(lon_of(x)); }
double ground_temperature(int, int z) {
    return 30.0 - 50.0 * std::abs(static_cast<double>(z) - 16.0) / 16.0;
}
double ground_tpi(int x, int) { return 25.0 * std::sin(2.0 * lon_of(x)); }
double all_land(int, int) { return 0.0; }
double flat(int, int) { return 0.0; }
double uniform_forage(int, int) { return 0.6; }

// The ground as the pass reads it, sampled again here so the oracle below is not reading
// the pass's own arrays.
struct Sampled {
    std::vector<Vec3> dir;
    std::vector<double> height, forage, forage_c, slope, temperature, tpi;
    std::vector<int> water, x, z;
};

struct Fixture {
    std::shared_ptr<const SphereGrid> grid;
    std::map<std::string, Grid> layers;
    double radius = 0.0;
    double spacing = 0.0;
    Sampled ground;
};

double layer_at(const std::map<std::string, Grid>& layers, const char* name, int x, int z) {
    const auto it = layers.find(name);
    return it == layers.end() ? 0.0 : it->second.at(x, z);
}

Fixture make_fixture(double (*height)(int, int) = ground_height,
                     double (*forage)(int, int) = ground_forage,
                     double (*slope)(int, int) = ground_slope,
                     double (*tpi)(int, int) = ground_tpi,
                     double (*temperature)(int, int) = ground_temperature,
                     double (*water)(int, int) = all_land) {
    Fixture f;
    f.radius = scale::physical_radius(kCircumferenceM);
    // The authored 450 m base, scaled the way resolve() scales it. Every reach in the pass
    // is a multiple of this and of nothing else.
    f.spacing = 450.0 * scale::reach_scale(kCircumferenceM);
    f.grid = sphere_grid(kSize, f.radius);
    f.layers["height"] = make_grid(kSize, height);
    f.layers["food_potential"] = make_grid(kSize, forage);
    f.layers["slope"] = make_grid(kSize, slope);
    f.layers["temperature"] = make_grid(kSize, temperature);
    f.layers["tpi"] = make_grid(kSize, tpi);
    f.layers["water_type"] = make_grid(kSize, water);

    const std::size_t count = f.grid->node_count();
    Sampled& g = f.ground;
    g.dir.resize(count);
    g.height.resize(count);
    g.forage.resize(count);
    g.forage_c.resize(count);
    g.slope.resize(count);
    g.temperature.resize(count);
    g.tpi.resize(count);
    g.water.resize(count);
    g.x.resize(count);
    g.z.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const int x = f.grid->point_x[i];
        const int z = f.grid->point_z[i];
        g.x[i] = x;
        g.z[i] = z;
        g.dir[i] = direction(x, z, kSize);
        g.height[i] = layer_at(f.layers, "height", x, z);
        g.forage[i] = std::max(layer_at(f.layers, "food_potential", x, z),
                               layer_at(f.layers, "natural_food_potential", x, z));
        g.forage_c[i] = std::max(0.0, std::min(1.0, g.forage[i]));
        g.slope[i] = layer_at(f.layers, "slope", x, z);
        g.temperature[i] = layer_at(f.layers, "temperature", x, z);
        g.tpi[i] = layer_at(f.layers, "tpi", x, z);
        g.water[i] = layer_at(f.layers, "water_type", x, z) != 0.0 ? 1 : 0;
    }
    return f;
}

BeastMovementRequest make_request(const Fixture& f) {
    BeastMovementRequest r;
    r.layers = &f.layers;
    r.grid = f.grid.get();
    r.globe_radius_m = f.radius;
    r.settlement_spacing_m = f.spacing;
    return r;
}

NestSite site_at(std::string_view species, int node, const SphereGrid& g) {
    NestSite s;
    s.species_id = species;
    s.name = species;
    s.node = node;
    s.x = g.point_x[static_cast<std::size_t>(node)];
    s.z = g.point_z[static_cast<std::size_t>(node)];
    s.direction = direction(s.x, s.z, g.n);
    return s;
}

double great_circle(const Vec3& a, const Vec3& b, double radius) {
    return radius * std::acos(clamp_unit(dot(a, b)));
}

// The walking cost, written out again. If this and the pass ever disagree the oracle tests
// below go red, which is the whole point of writing it twice.
double oracle_cost(const Fixture& f, std::size_t i, std::size_t j, double d, double ceiling) {
    const Sampled& g = f.ground;
    if (g.water[i] != 0 || g.water[j] != 0 || d <= 0.0) return -1.0;
    const int n = kSize;
    const int xi = g.x[i], zi = g.z[i], xj = g.x[j], zj = g.z[j];
    if (xi != xj && zi != zj && zi != 0 && zi != n - 1 && zj != 0 && zj != n - 1) {
        if (g.water[static_cast<std::size_t>(f.grid->node_index(xi, zj))] != 0 ||
            g.water[static_cast<std::size_t>(f.grid->node_index(xj, zi))] != 0) {
            return -1.0;
        }
    }
    const double grade = std::abs(g.height[j] - g.height[i]) / d;
    if (grade > ceiling) return -1.0;
    return d * (1.0 + 8.0 * grade * grade) * (1.0 - 0.25 * (g.forage_c[i] + g.forage_c[j]) / 2.0);
}

// Dense, unbounded, no heap: an O(V^2) settle-the-nearest scan. A different algorithm
// shape from the one under test on purpose.
std::vector<double> oracle_distances(const Fixture& f, int start, double road_max_grade = 0.35) {
    const double ceiling = road_max_grade * 2.2;
    const std::size_t count = f.grid->node_count();
    std::vector<double> dist(count, kInf);
    std::vector<bool> done(count, false);
    dist[static_cast<std::size_t>(start)] = 0.0;
    for (std::size_t pass = 0; pass < count; ++pass) {
        std::size_t at = count;
        double best = kInf;
        for (std::size_t i = 0; i < count; ++i) {
            if (!done[i] && dist[i] < best) {
                best = dist[i];
                at = i;
            }
        }
        if (at == count) break;
        done[at] = true;
        for (const SphereNeighbour& edge : f.grid->neighbours[at]) {
            const std::size_t j = static_cast<std::size_t>(edge.index);
            const double cost = oracle_cost(f, at, j, edge.distance, ceiling);
            if (cost < 0.0) continue;
            dist[j] = std::min(dist[j], best + cost);
        }
    }
    return dist;
}

std::vector<int> oracle_within(const std::vector<double>& dist, double limit) {
    std::vector<int> out;
    for (std::size_t i = 0; i < dist.size(); ++i) {
        if (dist[i] <= limit) out.push_back(static_cast<int>(i));
    }
    return out;
}

const BeastGroup* group_named(const BeastMovements& block, const std::string& uid) {
    for (const BeastGroup& g : block.groups) {
        if (g.uid == uid) return &g;
    }
    return nullptr;
}

std::string uid_of(std::string_view species, int node) {
    return "beastmove-" + std::string(species) + "-" + std::to_string(node);
}

const BeastCamp* camp_of_kind(const BeastGroup& g, std::string_view kind) {
    for (const BeastCamp& c : g.camps) {
        if (c.kind == kind) return &c;
    }
    return nullptr;
}

EncounterBand band_at(std::string uid, int node, const SphereGrid& g,
                      const std::vector<std::string>& camp_kinds) {
    EncounterBand b;
    b.uid = std::move(uid);
    b.node = node;
    b.classification = "merchants";
    b.disposition = "wary";
    b.size = 40;
    b.speed_m_per_day = 30000.0;
    b.column_length_m = 120.0;
    b.has_legs = true;
    for (std::size_t i = 0; i < camp_kinds.size(); ++i) {
        BeastCamp c;
        c.id = b.uid + "-camp-" + std::to_string(i);
        // The camps sit on the band's own node plus an offset, so a follower that copies
        // them lands on ground that is not its own.
        c.node = g.node_index(g.point_x[static_cast<std::size_t>(node)] + static_cast<int>(i),
                              g.point_z[static_cast<std::size_t>(node)]);
        c.direction = direction(g.point_x[static_cast<std::size_t>(c.node)],
                                g.point_z[static_cast<std::size_t>(c.node)], g.n);
        c.kind = camp_kinds[i];
        c.arrive_day = static_cast<int>(30 * i);
        c.depart_day = static_cast<int>(30 * i + 20);
        b.camps.push_back(std::move(c));
    }
    return b;
}

}  // namespace

// --- the catalogue this pass reads -----------------------------------------

TEST(the_catalogue_names_four_moving_classes_and_the_counts_are_the_authored_ones) {
    // The counts decide how much work the pass does at all, and a movement field that
    // silently stopped being carried through the codegen would leave this pass with
    // nothing to do and no error.
    std::map<std::string, int> by_movement;
    std::map<std::string, int> follower_roles;
    for (const CreatureProfile& p : creature_profiles()) {
        by_movement[std::string(p.movement)]++;
        if (p.movement == "follower") follower_roles[std::string(p.role)]++;
    }
    check::equal(by_movement["nester"], 594, "nesters");
    check::equal(by_movement["migratory"], 34, "migratory species");
    check::equal(by_movement["drifter"], 20, "drifters");
    check::equal(by_movement["follower"], 17, "followers");
    check::equal(by_movement["irruptive"], 15, "irruptive species");

    // Ported verbatim, defects and all: bird_of_prey is in the bias table and on no
    // follower, and pursuit_predator is on seven followers and in no row.
    check::equal(follower_roles["pack_predator"], 8, "pack predators");
    check::equal(follower_roles["pursuit_predator"], 7, "pursuit predators");
    check::equal(follower_roles["scavenger"], 1, "scavengers");
    check::equal(follower_roles.count("bird_of_prey"), std::size_t{0}, "birds of prey following");
    check::close(beast_host_bias("pursuit_predator", "nomad"), 0.85, 1e-12,
                 "a pursuit predator to take the default bias");
    check::close(beast_host_bias("pack_predator", "nomad"), 0.7, 1e-12, "a pack predator's bias");
    check::close(beast_host_bias("scavenger", "nomad"), 0.45, 1e-12, "a scavenger's bias");
    check::close(beast_host_bias("scavenger", "beastmove"), 1.0, 1e-12,
                 "every row to leave a herd unbiased");

    check::is_true(!beast_movement_of("nester").has_value(), "a nester not to be a class");
    check::is_true(beast_movement_of("drifter") == BeastMovement::Drifter, "the drifter class");
}

// --- the migratory builder --------------------------------------------------

TEST(a_herd_picks_the_two_seasonal_camps_a_brute_force_search_picks) {
    const Fixture f = make_fixture();
    const int start = f.grid->node_index(8, 16);
    BeastMovementRequest r = make_request(f);
    const NestSite site = site_at("american-bison", start, *f.grid);
    r.wildlife = std::span<const NestSite>(&site, 1);
    const BeastMovements block = *add_beast_movements(r);

    const BeastGroup* herd = group_named(block, uid_of("american-bison", start));
    check::is_true(herd != nullptr, "the herd to exist");
    check::is_true(herd->route_status == BeastRouteStatus::Routed, "the herd to have routed");

    // The oracle: every node an unbounded search reaches for the same cost inside the
    // 3-spacing reach, scored again here.
    const std::vector<double> dist = oracle_distances(f, start);
    const std::vector<int> reached = oracle_within(dist, 3.0 * f.spacing);
    check::is_true(reached.size() > 8, "the reach to settle enough cells to have a choice");

    const Sampled& g = f.ground;
    const double base = g.height[static_cast<std::size_t>(start)];
    const double span = std::max(1.0, std::abs(base) + 400.0);
    const auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };
    int summer = -1, winter = -1;
    double best_s = 0.0, best_w = 0.0;
    for (const int node : reached) {
        if (node == start) continue;
        const std::size_t at = static_cast<std::size_t>(node);
        const double food = g.forage[at];
        const double s = food * (1.0 + clamp01((g.height[at] - base) / span)) *
                         (0.4 + clamp01(1.0 - g.slope[at] / 30.0));
        // (score, -node): equal scores take the LOWEST node index.
        if (summer < 0 || s > best_s || (s == best_s && node < summer)) {
            best_s = s;
            summer = node;
        }
        const double w = (0.3 + food) * (1.0 + clamp01((base - g.height[at]) / span)) *
                         (0.4 + clamp01(std::abs(g.tpi[at]) / 30.0)) *
                         (0.5 + clamp01((g.temperature[at] + 20.0) / 45.0));
        if (winter < 0 || w > best_w || (w == best_w && node < winter)) {
            best_w = w;
            winter = node;
        }
    }
    check::is_true(summer != winter, "the two scorers to have chosen different cells");

    check::equal(herd->camps.size(), std::size_t{3}, "a three-stop round");
    check::equal(std::string(herd->camps[0].kind), std::string("calving"), "the first camp");
    check::equal(herd->camps[0].node, start, "calving to be the group's own ground");
    check::equal(std::string(herd->camps[1].kind), std::string("summer"), "the second camp");
    check::equal(herd->camps[1].node, summer, "the summer camp the oracle picked");
    check::equal(std::string(herd->camps[2].kind), std::string("winter"), "the third camp");
    check::equal(herd->camps[2].node, winter, "the winter camp the oracle picked");
    check::equal(herd->legs.size(), std::size_t{3}, "a closed three-leg circuit");
    check::equal(herd->legs[2].to, herd->camps[0].id, "the last leg to close on the calving camp");
    check::is_true(herd->round_length_m.has_value() && *herd->round_length_m > 0.0,
                   "a routed group to publish a round length");
}

TEST(a_world_with_nothing_to_choose_between_strands_the_herd) {
    // Flat, uniform ground scores every candidate identically -- so both scorers take the
    // same lowest-index cell and the round would collapse to two camps and one degenerate
    // leg. That is the guard, and it is the reason the two scorers must differ. Note the
    // temperature has to be flattened too: warmth is a winter-only term, so leaving it
    // varying is enough on its own to separate the two camps.
    const Fixture f = make_fixture(flat, uniform_forage, flat, flat, flat);
    const int start = f.grid->node_index(8, 16);
    BeastMovementRequest r = make_request(f);
    const NestSite site = site_at("american-bison", start, *f.grid);
    r.wildlife = std::span<const NestSite>(&site, 1);
    const BeastMovements block = *add_beast_movements(r);
    const BeastGroup* herd = group_named(block, uid_of("american-bison", start));
    check::is_true(herd != nullptr, "the herd to exist");

    // ...but only because the ground really is uniform and really does reach somewhere.
    const std::vector<int> reached = oracle_within(oracle_distances(f, start), 3.0 * f.spacing);
    check::is_true(reached.size() > 8, "the flat world to reach somewhere at all");
    check::is_true(herd->route_status == BeastRouteStatus::Stranded, "the herd to strand");
    check::equal(herd->camps.size(), std::size_t{1}, "a stranded group to keep one camp");
    check::equal(std::string(herd->camps[0].kind), std::string("base"), "that camp to be a base");
    check::equal(herd->camps[0].node, start, "and to stand on its own ground");
    check::is_true(herd->legs.empty(), "a stranded group to walk nothing");
    check::is_true(!herd->round_length_m.has_value(), "and to publish no round length");
}

// --- the irruptive builder --------------------------------------------------

namespace {

// Forage that is poor exactly at one longitude, so a group placed there falls short of its
// neighbourhood and a group placed elsewhere does not.
double pit_forage(int x, int) { return x == 8 ? 0.01 : 0.9; }

}  // namespace

TEST(a_swarm_marches_only_when_its_own_cell_falls_short_of_its_neighbourhood) {
    const Fixture f = make_fixture(flat, pit_forage, flat, flat);
    const int lean = f.grid->node_index(8, 16);
    const int fat = f.grid->node_index(2, 16);
    BeastMovementRequest r = make_request(f);
    const std::vector<NestSite> sites{site_at("desert-locust", lean, *f.grid),
                                      site_at("desert-locust", fat, *f.grid)};
    r.wildlife = sites;
    const BeastMovements block = *add_beast_movements(r);

    const BeastGroup* poor = group_named(block, uid_of("desert-locust", lean));
    const BeastGroup* rich = group_named(block, uid_of("desert-locust", fat));
    check::is_true(poor != nullptr && rich != nullptr, "both swarms to exist");

    // Recompute the trigger. `within` INCLUDES the start, so the start contributes to the
    // mean it is compared against.
    const std::vector<int> within = oracle_within(oracle_distances(f, lean), 4.0 * f.spacing);
    check::is_true(within.size() > 8, "the march bound to reach a neighbourhood at all");
    double total = 0.0;
    for (const int node : within) total += f.ground.forage[static_cast<std::size_t>(node)];
    const double mean = total / static_cast<double>(within.size());
    const double own = f.ground.forage[static_cast<std::size_t>(lean)];
    check::is_true(own * block.year_forage_factor < 0.18 * mean,
                   "the lean cell to be below the margin on this year's factor");

    check::is_true(poor->route_status == BeastRouteStatus::Routed, "the lean swarm to march");
    check::equal(poor->camps.size(), std::size_t{2}, "a one-way march");
    check::equal(std::string(poor->camps[0].kind), std::string("base"), "the origin");
    check::equal(std::string(poor->camps[1].kind), std::string("terminal"), "the destination");
    check::equal(poor->legs.size(), std::size_t{1}, "one leg, and no return");

    // The best forage in reach, by the same (score, -node) maximum.
    int target = -1;
    double best = 0.0;
    for (const int node : within) {
        if (node == lean) continue;
        const double mark = f.ground.forage[static_cast<std::size_t>(node)];
        if (target < 0 || mark > best || (mark == best && node < target)) {
            best = mark;
            target = node;
        }
    }
    check::equal(poor->camps[1].node, target, "the march to head for the best forage in reach");

    check::is_true(rich->route_status == BeastRouteStatus::Solitary,
                   "the swarm on ordinary ground to stay solitary");
    check::equal(rich->camps.size(), std::size_t{1}, "and to keep a camp a player can meet");
    check::is_true(rich->legs.empty(), "and to walk nothing");
    check::equal(block.solitary, 1, "exactly one solitary group");
    check::equal(block.routed, 1, "exactly one routed group");
}

TEST(a_swarm_on_ground_with_no_forage_at_all_reads_as_solitary_rather_than_stranded) {
    // RECORDED, NOT FIXED. With zero forage everywhere the trigger is 0 >= 0.18 * 0, which
    // holds, so a group on barren ground reports "sparse and not marching" rather than
    // "nowhere worth marching to". The reference picks that reading by arithmetic accident
    // and not by decision -- and it is also what every irruptive group on a world built by
    // the stages ported so far does, because the two food layers do not exist yet.
    const Fixture f = make_fixture(ground_height, flat, ground_slope, ground_tpi);
    const int start = f.grid->node_index(8, 16);
    BeastMovementRequest r = make_request(f);
    const NestSite site = site_at("desert-locust", start, *f.grid);
    r.wildlife = std::span<const NestSite>(&site, 1);
    const BeastMovements block = *add_beast_movements(r);
    const BeastGroup* swarm = group_named(block, uid_of("desert-locust", start));
    check::is_true(swarm != nullptr, "the swarm to exist");
    check::is_true(swarm->route_status == BeastRouteStatus::Solitary, "it to read as solitary");
    check::equal(block.stranded, 0, "and not as stranded");
}

TEST(the_year_forage_factor_ignores_a_stepped_seed_and_follows_the_year) {
    // The whole irruption trigger rests on this. The pass runs on a MONTHLY cadence with a
    // stepped seed, so a factor keyed on that seed would be twelve different weathers
    // inside one year.
    const Fixture f = make_fixture();
    BeastMovementRequest r = make_request(f);
    r.genesis_seed = 42;
    r.year = 5250;

    std::set<double> under_stepped_seeds;
    for (const std::uint32_t stepped : {42u, 998877u, 3u}) {
        r.seed = stepped;
        under_stepped_seeds.insert(add_beast_movements(r)->year_forage_factor);
    }
    check::equal(under_stepped_seeds.size(), std::size_t{1}, "one factor across three ticks");
    const double factor = *under_stepped_seeds.begin();
    check::between(factor, 0.55, 1.25, "the factor to sit in its authored band");

    // ...and it is not simply a constant: the year moves it, and so does the genesis seed.
    std::set<double> across_years;
    r.seed = 42;
    for (const long long year : {5250, 5251, 5252, 5253, 5254}) {
        r.year = year;
        across_years.insert(add_beast_movements(r)->year_forage_factor);
    }
    check::is_true(across_years.size() >= 4, "five years to draw at least four weathers");
    r.year = 5250;
    r.genesis_seed = 7;
    check::is_true(add_beast_movements(r)->year_forage_factor != factor,
                   "a different world to have a different year");
}

// --- the drifter builder ----------------------------------------------------

TEST(the_unburied_dead_haunt_the_three_nearest_ruins_and_close_on_their_base) {
    const Fixture f = make_fixture();
    const int start = f.grid->node_index(8, 16);
    const std::vector<double> dist = oracle_distances(f, start);
    const double reach = 2.5 * f.spacing;

    // Five candidate ruins spread over the reach, plus two the guards must drop: one on
    // the group's own node and one from a raster that no longer exists.
    std::vector<BeastRuin> ruins;
    for (const int x : {9, 10, 11, 12, 6}) {
        ruins.push_back({f.grid->node_index(x, 16), "city-" + std::to_string(x)});
    }
    ruins.push_back({start, "city-here"});
    ruins.push_back({999999, "city-from-another-world"});

    BeastMovementRequest r = make_request(f);
    const NestSite site = site_at("banshees", start, *f.grid);
    r.beast_nests = std::span<const NestSite>(&site, 1);
    r.ruins = ruins;
    const BeastMovements block = *add_beast_movements(r);
    const BeastGroup* dead = group_named(block, uid_of("banshees", start));
    check::is_true(dead != nullptr, "the drifters to exist");

    // (walk distance, ruin uid, node), nearest three.
    struct Haunt {
        double d;
        std::string uid;
        int node;
    };
    std::vector<Haunt> haunts;
    for (const BeastRuin& ruin : ruins) {
        if (ruin.node == start || static_cast<std::size_t>(ruin.node) >= dist.size()) continue;
        const double d = dist[static_cast<std::size_t>(ruin.node)];
        if (std::isfinite(d) && d <= reach) haunts.push_back({d, ruin.uid, ruin.node});
    }
    std::sort(haunts.begin(), haunts.end(), [](const Haunt& a, const Haunt& b) {
        return a.d != b.d ? a.d < b.d : (a.uid != b.uid ? a.uid < b.uid : a.node < b.node);
    });
    check::is_true(haunts.size() > 3, "more ruins in reach than the circuit may take");

    check::is_true(dead->route_status == BeastRouteStatus::Routed, "the drifters to have routed");
    check::equal(dead->camps.size(), std::size_t{4}, "a base and AT MOST three haunts");
    check::equal(std::string(dead->camps[0].kind), std::string("base"), "the first camp");
    check::equal(dead->camps[0].node, start, "the base to be its own ground");
    for (std::size_t i = 0; i < 3; ++i) {
        check::equal(dead->camps[i + 1].node, haunts[i].node,
                     "haunt " + std::to_string(i) + " to be the next nearest ruin");
        check::equal(std::string(dead->camps[i + 1].kind), std::string("terminal"),
                     "a haunt to be a terminal");
    }
    check::equal(dead->legs.size(), std::size_t{4}, "three hops out and one home");
    check::equal(dead->legs.back().to, dead->camps[0].id, "the circuit to close on the base");
}

TEST(a_drifter_with_no_ruin_in_reach_keeps_one_base_camp_and_strands) {
    // A world that has had no age transition has no ruins, and every drifter on it
    // strands. That is a fact about the world, not a routing failure.
    const Fixture f = make_fixture();
    const int start = f.grid->node_index(8, 16);
    BeastMovementRequest r = make_request(f);
    const NestSite site = site_at("banshees", start, *f.grid);
    r.beast_nests = std::span<const NestSite>(&site, 1);
    const BeastMovements block = *add_beast_movements(r);
    const BeastGroup* dead = group_named(block, uid_of("banshees", start));
    check::is_true(dead != nullptr, "the drifters to exist even with nowhere to go");
    check::is_true(dead->route_status == BeastRouteStatus::Stranded, "them to strand");
    check::equal(dead->camps.size(), std::size_t{1}, "and to keep one base camp");
    check::equal(block.stranded, 1, "the block to count it stranded");
    check::is_true(beast_counts_balance(block), "the three counts to balance");
}

// --- the follower builder ---------------------------------------------------

TEST(a_scavenger_crosses_twice_the_distance_for_a_band_and_a_pursuit_predator_does_not) {
    // The bias is the whole reason this table exists: herds outnumber bands roughly forty
    // to one, so a plain nearest-host rule left nothing following a caravan.
    //
    // Both hosts are supplied as bands, because the bias key is the UID PREFIX and not the
    // source list -- which is the reference's own rule, not a shortcut taken here.
    const Fixture f = make_fixture();
    const int start = f.grid->node_index(8, 16);
    const int near = f.grid->node_index(9, 16);
    const int far = f.grid->node_index(10, 16);
    const std::vector<EncounterBand> hosts{
        band_at("beastmove-near-host", near, *f.grid, {"base", "summer"}),
        band_at("nomad-far-host", far, *f.grid, {"base", "station"})};

    const Sampled& g = f.ground;
    const double d_near = great_circle(g.dir[static_cast<std::size_t>(start)],
                                       g.dir[static_cast<std::size_t>(near)], f.radius);
    const double d_far = great_circle(g.dir[static_cast<std::size_t>(start)],
                                      g.dir[static_cast<std::size_t>(far)], f.radius);
    // The fixture only proves anything if it straddles the two biases. It also has to keep
    // the far host inside the raw reach, because the cut is on the UNBIASED span.
    check::is_true(d_far <= 3.0 * f.spacing, "the far host to be inside the reach");
    check::is_true(0.45 * d_far < d_near, "a scavenger's bias to reverse the order");
    check::is_true(0.85 * d_far > d_near, "and the default bias not to");

    BeastMovementRequest r = make_request(f);
    const std::vector<NestSite> sites{site_at("carrion-followers", start, *f.grid),
                                      site_at("cheetah", start, *f.grid)};
    r.wildlife = sites;
    r.bands = hosts;
    const BeastMovements block = *add_beast_movements(r);

    const BeastGroup* scavenger = group_named(block, uid_of("carrion-followers", start));
    const BeastGroup* hunter = group_named(block, uid_of("cheetah", start));
    check::is_true(scavenger != nullptr && hunter != nullptr, "both followers to exist");
    check::equal(scavenger->host_uid, std::string("nomad-far-host"),
                 "the scavenger to attach to people");
    check::equal(hunter->host_uid, std::string("beastmove-near-host"),
                 "the pursuit predator to take the nearer host");

    // The follower's camps are the HOST's, remapped: station and terminal stay terminal,
    // everything else folds to base. Its own start node is never a camp.
    check::is_true(scavenger->route_status == BeastRouteStatus::Routed, "the scavenger to route");
    check::equal(scavenger->camps.size(), std::size_t{2}, "the host's two camps");
    check::equal(scavenger->camps[0].node, hosts[1].camps[0].node, "the host's first camp");
    check::equal(std::string(scavenger->camps[0].kind), std::string("base"), "a base fold");
    check::equal(std::string(scavenger->camps[1].kind), std::string("terminal"),
                 "a station to remap to terminal");
    check::equal(std::string(hunter->camps[1].kind), std::string("base"),
                 "and a summer camp to fold to base");
    for (const BeastCamp& c : scavenger->camps) {
        check::is_true(c.node != start, "no follower camp to sit on the follower's own node");
    }
}

TEST(a_host_past_the_reach_is_refused_however_far_its_bias_would_have_admitted_it) {
    // The cut is on the RAW span. A bias below one shortens the effective distance, so a
    // preferred host wins from further away -- but it must never let an unreachable one
    // through, and testing that needs a host the bias alone WOULD have admitted.
    //
    // The refused host sits on the follower's own latitude, so the walk's latitude bound
    // does not refuse it first and the per-candidate cut is the only thing left to do it.
    const Fixture f = make_fixture();
    const int start = f.grid->node_index(8, 16);
    const std::vector<EncounterBand> hosts{
        band_at("nomad-beyond", f.grid->node_index(13, 16), *f.grid, {"base", "station"}),
        band_at("beastmove-within", f.grid->node_index(8, 23), *f.grid, {"base", "summer"})};

    const Sampled& g = f.ground;
    const double reach = 3.0 * f.spacing;
    const double beyond = great_circle(g.dir[static_cast<std::size_t>(start)],
                                       g.dir[static_cast<std::size_t>(hosts[0].node)], f.radius);
    const double within = great_circle(g.dir[static_cast<std::size_t>(start)],
                                       g.dir[static_cast<std::size_t>(hosts[1].node)], f.radius);
    check::is_true(beyond > reach, "the band to be outside the raw reach");
    check::is_true(0.45 * beyond < reach, "but inside it once a scavenger's bias is applied");
    check::is_true(within < reach, "the herd to be inside the reach");
    check::is_true(0.45 * beyond < within, "and to lose outright if the band were admitted");

    BeastMovementRequest r = make_request(f);
    const NestSite site = site_at("carrion-followers", start, *f.grid);
    r.wildlife = std::span<const NestSite>(&site, 1);
    r.bands = hosts;
    const BeastMovements block = *add_beast_movements(r);
    const BeastGroup* scavenger = group_named(block, uid_of("carrion-followers", start));
    check::is_true(scavenger != nullptr, "the scavenger to exist");
    check::equal(scavenger->host_uid, std::string("beastmove-within"),
                 "the scavenger to take the host it can actually reach");
}

TEST(a_follower_whose_host_cannot_be_derived_from_still_names_the_host) {
    // The uid is claimed even when the circuit is too short to derive from, because the
    // follower DID find a host -- it is the host's route that failed it.
    const Fixture f = make_fixture();
    const int start = f.grid->node_index(8, 16);
    const std::vector<EncounterBand> hosts{
        band_at("nomad-one-camp", f.grid->node_index(9, 16), *f.grid, {"base"})};
    BeastMovementRequest r = make_request(f);
    const NestSite site = site_at("carrion-followers", start, *f.grid);
    r.wildlife = std::span<const NestSite>(&site, 1);
    r.bands = hosts;
    const BeastMovements block = *add_beast_movements(r);
    const BeastGroup* follower = group_named(block, uid_of("carrion-followers", start));
    check::is_true(follower != nullptr, "the follower to exist");
    check::equal(follower->host_uid, std::string("nomad-one-camp"), "the host to be named");
    check::is_true(follower->route_status == BeastRouteStatus::Stranded, "and the follower to strand");
    check::equal(follower->camps.size(), std::size_t{1}, "keeping its own base camp");
}

TEST(the_latitude_walk_finds_the_host_a_flat_scan_over_every_host_finds) {
    // The walk exists because comparing every follower against every host is the quadratic
    // that made this the largest pass in the generation. It has to give the same answer,
    // which is the unique minimum of (span * bias, uid).
    const Fixture f = make_fixture();
    std::vector<EncounterBand> hosts;
    for (int x = 0; x < 32; x += 3) {
        for (const int z : {16, 24}) {
            const std::string uid = (x % 2 == 0 ? "nomad-b" : "beastmove-b") + std::to_string(x) +
                                    "-" + std::to_string(z);
            hosts.push_back(band_at(uid, f.grid->node_index(x, z), *f.grid, {"base", "station"}));
        }
    }
    std::vector<NestSite> sites;
    for (int x = 1; x < 32; x += 2) {
        sites.push_back(site_at("carrion-followers", f.grid->node_index(x, 16), *f.grid));
        sites.push_back(site_at("cheetah", f.grid->node_index(x, 12), *f.grid));
        // Fourteen rows above the nearest host row -- and a row is HALF a column, because
        // the meridian spans 180 degrees where the equator spans 360 -- so these sit well
        // past the reach. They are the ones the cut has to refuse, and without them the
        // test would pass against a walk that never stops.
        sites.push_back(site_at("african-wild-dog", f.grid->node_index(x, 2), *f.grid));
    }
    BeastMovementRequest r = make_request(f);
    r.wildlife = sites;
    r.bands = hosts;
    const BeastMovements block = *add_beast_movements(r);

    const double reach = 3.0 * f.spacing;
    int attached = 0;
    int unattached = 0;
    for (const BeastGroup& group : block.groups) {
        const Vec3 here = f.ground.dir[static_cast<std::size_t>(group.node)];
        std::string best_uid;
        double best_mark = 0.0;
        for (const EncounterBand& host : hosts) {
            const double span =
                great_circle(here, f.ground.dir[static_cast<std::size_t>(host.node)], f.radius);
            if (span > reach) continue;  // the cut is on the RAW span, before any bias
            const double mark =
                span * beast_host_bias(group.role,
                                       host.uid.rfind("nomad-", 0) == 0 ? "nomad" : "beastmove");
            if (best_uid.empty() || mark < best_mark || (mark == best_mark && host.uid < best_uid)) {
                best_mark = mark;
                best_uid = host.uid;
            }
        }
        check::equal(group.host_uid, best_uid, "the walk and the scan to agree for " + group.uid);
        if (best_uid.empty()) {
            ++unattached;
        } else {
            ++attached;
        }
    }
    check::is_true(attached > 20, "most followers to have found a host");
    check::is_true(unattached > 0, "and the reach cut to have refused at least one");
}

// --- legs and the calendar --------------------------------------------------

TEST(every_leg_is_a_contiguous_walk_whose_length_is_its_own_great_circle_sum) {
    const Fixture f = make_fixture();
    std::vector<NestSite> sites;
    for (int x = 0; x < 32; x += 4) {
        sites.push_back(site_at("american-bison", f.grid->node_index(x, 14), *f.grid));
    }
    BeastMovementRequest r = make_request(f);
    r.wildlife = sites;
    const BeastMovements block = *add_beast_movements(r);

    int checked = 0;
    for (const BeastGroup& group : block.groups) {
        if (group.route_status != BeastRouteStatus::Routed) continue;
        double round_length = 0.0;
        for (const BeastLeg& leg : group.legs) {
            check::is_true(leg.nodes.size() >= 2, "a leg to hold both its endpoints");
            const BeastCamp* from = nullptr;
            const BeastCamp* to = nullptr;
            for (const BeastCamp& c : group.camps) {
                if (c.id == leg.from) from = &c;
                if (c.id == leg.to) to = &c;
            }
            check::is_true(from != nullptr && to != nullptr, "a leg to name two real camps");
            check::equal(leg.nodes.front(), from->node, "the path to start at its camp");
            check::equal(leg.nodes.back(), to->node, "and to end at the other");
            double length = 0.0;
            for (std::size_t i = 0; i + 1 < leg.nodes.size(); ++i) {
                const int a = leg.nodes[i];
                const int b = leg.nodes[i + 1];
                bool adjacent = false;
                for (const SphereNeighbour& e : f.grid->neighbours[static_cast<std::size_t>(a)]) {
                    adjacent = adjacent || e.index == b;
                }
                check::is_true(adjacent, "consecutive path nodes to be graph neighbours");
                length += great_circle(f.ground.dir[static_cast<std::size_t>(a)],
                                       f.ground.dir[static_cast<std::size_t>(b)], f.radius);
            }
            // The length is plain geodesic metres, NOT the walking cost that shaped it.
            check::close(leg.length_m, length, 1e-6, "the published leg length");
            check::equal(std::string(leg.branch), std::string("trunk"), "this pass's only branch");
            round_length += leg.length_m;
            ++checked;
        }
        check::close(*group.round_length_m, std::round(round_length * 1e6) / 1e6, 1e-6,
                     "the round length to be the sum of its legs");
    }
    check::is_true(checked > 6, "enough legs to have proved anything");
}

TEST(a_herding_round_holds_its_winter_camp_at_midwinter_and_gives_every_camp_a_window) {
    const Fixture f = make_fixture();
    std::vector<NestSite> sites;
    // North of the equator and south of it, because the seasonal phase inverts and getting
    // that wrong is invisible on a world whose land sits in one hemisphere.
    for (const int z : {8, 24}) {
        for (int x = 0; x < 32; x += 6) {
            sites.push_back(site_at("american-bison", f.grid->node_index(x, z), *f.grid));
        }
    }
    BeastMovementRequest r = make_request(f);
    r.wildlife = sites;
    const BeastMovements block = *add_beast_movements(r);

    int north = 0;
    int south = 0;
    for (const BeastGroup& group : block.groups) {
        if (group.route_status != BeastRouteStatus::Routed) continue;
        const BeastCamp* winter = camp_of_kind(group, "winter");
        check::is_true(winter != nullptr, "a routed herd to hold a winter camp");
        const int z = f.grid->point_z[static_cast<std::size_t>(winter->node)];
        check::is_true(winter->arrive_day.has_value(), "the winter camp to be occupied");
        check::equal(*winter->arrive_day, midwinter_day(z, kSize),
                     "the year to be anchored on midwinter at the winter camp's own row");
        (z < 16 ? north : south)++;
        for (const BeastCamp& camp : group.camps) {
            check::is_true(camp.arrive_day.has_value() && camp.depart_day.has_value(),
                           "every camp of a routed group to carry a window");
            check::between(*camp.arrive_day, 0, 359, "an arrival day");
            check::between(*camp.depart_day, 0, 359, "a departure day");
        }
        for (const BeastLeg& leg : group.legs) {
            check::is_true(leg.depart_day.has_value() && leg.arrive_day.has_value(),
                           "every leg to carry a window");
        }
    }
    check::is_true(north > 0 && south > 0, "herds on both sides of the equator");
    check::equal(midsummer_day(0, kSize), 180, "midsummer at the north pole");
    check::equal(midsummer_day(kSize - 1, kSize), 0, "midsummer at the south pole");
    check::equal(midwinter_day(0, kSize), 0, "midwinter at the north pole");
    check::equal(midwinter_day(kSize - 1, kSize), 180, "midwinter at the south pole");
}

// --- what scales and what does not ------------------------------------------

TEST(gait_and_column_are_absolute_while_the_four_reaches_follow_the_spacing) {
    const Fixture f = make_fixture();
    std::vector<NestSite> sites;
    for (int x = 0; x < 32; x += 4) {
        sites.push_back(site_at("american-bison", f.grid->node_index(x, 14), *f.grid));
    }
    BeastMovementRequest narrow = make_request(f);
    narrow.wildlife = sites;
    BeastMovementRequest wide = narrow;
    wide.settlement_spacing_m = f.spacing * 3.0;

    const BeastMovements a = *add_beast_movements(narrow);
    const BeastMovements b = *add_beast_movements(wide);
    check::equal(a.groups.size(), b.groups.size(), "the same sites to produce the same groups");

    int moved = 0;
    for (std::size_t i = 0; i < a.groups.size(); ++i) {
        check::equal(a.groups[i].uid, b.groups[i].uid, "the published order to be unchanged");
        // A gait belongs to the animal, not to the world; so does a body's width.
        check::close(a.groups[i].speed_m_per_day, b.groups[i].speed_m_per_day, 0.0,
                     "speed not to move with the spacing");
        check::close(a.groups[i].column_length_m, b.groups[i].column_length_m, 0.0,
                     "column length not to move with the spacing");
        const BeastCamp* summer_a = camp_of_kind(a.groups[i], "summer");
        const BeastCamp* summer_b = camp_of_kind(b.groups[i], "summer");
        if (summer_a != nullptr && summer_b != nullptr && summer_a->node != summer_b->node) ++moved;
    }
    check::is_true(moved > 0, "a wider reach to find at least one better summer camp");

    // And the two absolute tables are what they are authored as.
    check::close(beast_speed_m_per_day("large"), 14000.0, 0.0, "a large animal's gait");
    check::close(beast_speed_m_per_day("tiny"), 26000.0, 0.0, "a tiny one's");
    check::equal(beast_herd_size(1), 900, "a tier one herd");
    check::equal(beast_herd_size(5), 12, "a tier five one");
    check::close(beast_spread_per_head_m("colossal"), 20.0, 0.0, "a colossal head's spread");
}

// --- the published order, the counts and the thinning -----------------------

TEST(the_published_order_is_a_string_sort_so_node_ten_precedes_node_nine) {
    // Node numbers sort as TEXT, and that order propagates into the encounter entries and
    // thence into every occupancy index. A numeric sort is a different published block.
    const Fixture f = make_fixture();
    std::vector<NestSite> sites;
    for (const int node : {9, 10, 1, 100}) sites.push_back(site_at("american-bison", node, *f.grid));
    BeastMovementRequest r = make_request(f);
    r.wildlife = sites;
    const BeastMovements block = *add_beast_movements(r);
    check::equal(block.groups.size(), std::size_t{4}, "four herds");

    std::vector<std::string> uids;
    for (const BeastGroup& g : block.groups) uids.push_back(g.uid);
    std::vector<std::string> sorted = uids;
    std::sort(sorted.begin(), sorted.end());
    check::is_true(uids == sorted, "the groups to come out in string order");
    check::equal(uids[0], uid_of("american-bison", 1), "-1 first");
    check::equal(uids[1], uid_of("american-bison", 10), "-10 before -9");
    check::equal(uids[2], uid_of("american-bison", 100), "-100 before -9");
    check::equal(uids[3], uid_of("american-bison", 9), "-9 last");
}

TEST(the_counts_name_each_class_in_first_appearance_order_and_the_three_totals_balance) {
    const Fixture f = make_fixture();
    std::vector<NestSite> sites;
    for (int x = 0; x < 30; x += 5) {
        sites.push_back(site_at("american-bison", f.grid->node_index(x, 14), *f.grid));
        sites.push_back(site_at("desert-locust", f.grid->node_index(x, 15), *f.grid));
        sites.push_back(site_at("carrion-followers", f.grid->node_index(x, 16), *f.grid));
        sites.push_back(site_at("banshees", f.grid->node_index(x, 17), *f.grid));
    }
    BeastMovementRequest r = make_request(f);
    r.wildlife = sites;  // the source never decides the order; the sorted species id does
    const BeastMovements block = *add_beast_movements(r);

    check::equal(block.considered, 24, "every eligible site considered");
    check::equal(block.skipped, 0, "and none thinned at share 1.0");
    check::equal(block.groups.size(), std::size_t{24}, "one group per site");
    check::is_true(beast_counts_balance(block), "routed + stranded + solitary to be every group");

    // First appearance in the UID-SORTED list: american-bison, banshees, carrion-followers,
    // desert-locust -- which is species-id order here, and not the order they were placed.
    check::equal(block.counts.size(), std::size_t{4}, "four classes present");
    check::equal(std::string(block.counts[0].first), std::string("migratory"), "bison first");
    check::equal(std::string(block.counts[1].first), std::string("drifter"), "banshees second");
    check::equal(std::string(block.counts[2].first), std::string("follower"), "carrion third");
    check::equal(std::string(block.counts[3].first), std::string("irruptive"), "locusts last");
    int total = 0;
    for (const auto& [name, count] : block.counts) {
        check::equal(count, 6, std::string(name) + " to have six groups");
        total += count;
    }
    check::equal(total, 24, "the counts to cover every group");
}

TEST(the_share_thins_the_ordered_list_deterministically_and_in_proportion) {
    const Fixture f = make_fixture();
    std::vector<NestSite> sites;
    for (int x = 0; x < 32; ++x) {
        for (const int z : {10, 14, 18, 22, 26, 30}) {
            sites.push_back(site_at("american-bison", f.grid->node_index(x, z), *f.grid));
        }
    }
    check::equal(sites.size(), std::size_t{192}, "enough sites for a share to mean something");
    BeastMovementRequest r = make_request(f);
    r.wildlife = sites;

    const BeastMovements whole = *add_beast_movements(r);
    check::equal(whole.considered, 192, "every site considered");
    check::equal(whole.skipped, 0, "and at share 1.0 not one draw is made");

    r.share = 0.5;
    const BeastMovements half = *add_beast_movements(r);
    check::equal(half.considered, 192, "the share not to change what is considered");
    check::is_true(half.skipped > 60 && half.skipped < 130,
                   "roughly half thinned, not a cap and not everything");
    check::equal(half.groups.size() + static_cast<std::size_t>(half.skipped), std::size_t{192},
                 "kept plus skipped to be the whole list");

    const BeastMovements again = *add_beast_movements(r);
    check::equal(again.skipped, half.skipped, "the same seed to thin the same list");

    // ...and the variation really is an independent reroll of this layer.
    r.nomad_variation = 9;
    const BeastMovements rerolled = *add_beast_movements(r);
    std::set<std::string> first, second;
    for (const BeastGroup& g : half.groups) first.insert(g.uid);
    for (const BeastGroup& g : rerolled.groups) second.insert(g.uid);
    check::is_true(first != second, "a different variation to keep a different set");
}

// --- the encounter index ----------------------------------------------------

TEST(the_index_orders_nodes_numerically_and_every_index_it_holds_resolves) {
    const Fixture f = make_fixture();
    std::vector<NestSite> sites;
    for (int x = 0; x < 32; x += 3) {
        sites.push_back(site_at("american-bison", f.grid->node_index(x, 14), *f.grid));
        sites.push_back(site_at("banshees", f.grid->node_index(x, 18), *f.grid));
    }
    // A single-digit node, so the index straddles the point at which a lexicographic order
    // over the published string keys would put "10" in front of "9".
    sites.push_back(site_at("american-bison", 5, *f.grid));
    const std::vector<BeastRuin> ruins{{f.grid->node_index(4, 18), "city-a"},
                                       {f.grid->node_index(20, 18), "city-b"}};
    BeastMovementRequest r = make_request(f);
    r.wildlife = sites;
    r.ruins = ruins;
    const BeastMovements block = *add_beast_movements(r);

    EncounterRequest e;
    e.groups = block.groups;
    const EncounterIndex index = *add_encounters(e);

    check::equal(index.version, 2, "the block version");
    check::is_true(index.entries.size() > 10, "an entry per group that holds a camp");
    check::equal(index.entries.size(), block.groups.size(),
                 "every group here holds at least one camp");
    check::is_true(index.occupancy.size() > index.entries.size(),
                   "more camps than groups, or nothing is routed");

    for (std::size_t i = 0; i < index.by_node.size(); ++i) {
        if (i > 0) {
            check::is_true(index.by_node[i - 1].first < index.by_node[i].first,
                           "by_node to ascend by NODE, not by the string that spells it");
        }
        for (const int at : index.by_node[i].second) {
            check::between(at, 0, static_cast<int>(index.occupancy.size()) - 1,
                           "a by_node value to index occupancy");
            check::equal(index.occupancy[static_cast<std::size_t>(at)].node, index.by_node[i].first,
                         "and to land on its own node");
        }
    }
    // A node above 9 and a node below it both occur, so a lexicographic order would show.
    const bool straddles = index.by_node.front().first < 10 && index.by_node.back().first > 10;
    check::is_true(straddles, "the fixture to straddle the point a string sort would invert");

    int in_a_month = 0;
    for (std::size_t m = 0; m < 12; ++m) {
        for (const int at : index.by_month[m]) {
            check::between(at, 0, static_cast<int>(index.occupancy.size()) - 1,
                           "a by_month value to index occupancy");
            const EncounterOccupancy& o = index.occupancy[static_cast<std::size_t>(at)];
            check::is_true(std::find(o.months.begin(), o.months.end(), static_cast<int>(m)) !=
                               o.months.end(),
                           "and to be a month that record actually claims");
            ++in_a_month;
        }
    }
    check::is_true(in_a_month > 20, "the month index to hold something");
    for (const EncounterOccupancy& o : index.occupancy) {
        check::between(o.entry, 0, static_cast<int>(index.entries.size()) - 1,
                       "an occupancy record to name a real entry");
    }
}

TEST(an_entry_takes_its_domain_from_the_authored_medium_and_flying_is_not_one) {
    // Per entry against the catalogue, one group per medium and per class, because a world
    // is three quarters one domain and a share-based assertion there passes for almost any
    // mapping -- including a constant one.
    const Fixture f = make_fixture();
    const std::vector<std::pair<std::string_view, std::string_view>> expected{
        {"american-bison", "land"},          // land animal
        {"walrus", "land"},                  // SHORE is a land medium
        {"atlantic-bluefin-tuna", "ocean"},  // marine
        {"atlantic-salmon", "lake"},         // freshwater
        {"ghost-galleons", "ocean"},         // marine monster
        {"skywhales", "land"},               // airborne, and still land
    };
    std::vector<NestSite> sites;
    int node = 60;
    for (const auto& [species, domain] : expected) {
        (void)domain;
        sites.push_back(site_at(species, node, *f.grid));
        node += 7;
    }
    BeastMovementRequest r = make_request(f);
    r.wildlife = sites;
    const BeastMovements block = *add_beast_movements(r);
    EncounterRequest e;
    e.groups = block.groups;
    const EncounterIndex index = *add_encounters(e);
    check::equal(index.entries.size(), expected.size(), "an entry for every group");

    std::map<std::string, const EncounterEntry*> by_species;
    for (const EncounterEntry& entry : index.entries) by_species[entry.kind] = &entry;
    for (const auto& [species, domain] : expected) {
        const auto it = by_species.find(std::string(species));
        check::is_true(it != by_species.end(), std::string(species) + " to be indexed");
        check::equal(it->second->domain, std::string(domain),
                     std::string(species) + " to be resolved from its authored medium");
        check::equal(it->second->source, std::string("beast_movements"), "its source");
    }
    check::is_true(by_species["skywhales"]->airborne, "a flyer to be marked as one");
    check::is_true(!by_species["american-bison"]->airborne, "and a herd not to be");
    check::equal(by_species["skywhales"]->entity_class, std::string("monster"), "its class");
    check::equal(by_species["american-bison"]->entity_class, std::string("animal"), "and a bison's");
    // A herd is worth something to whoever can take it; a swarm is worth avoiding.
    check::equal(by_species["american-bison"]->carries.size(), std::size_t{2}, "a herd's spoils");
    check::is_true(by_species["ghost-galleons"]->carries.empty(), "a monster to carry nothing");
    check::equal(by_species["american-bison"]->seeks.at(0), std::string("pasture"),
                 "a migratory group's want");
    check::equal(by_species["ghost-galleons"]->seeks.at(0), std::string("rest"), "a drifter's");
    check::equal(by_species["american-bison"]->threat_tier, 3, "the creature danger tier");
}

TEST(a_band_is_indexed_before_a_beast_and_takes_its_tier_from_its_classification) {
    const Fixture f = make_fixture();
    const int start = f.grid->node_index(8, 16);
    std::vector<EncounterBand> bands{band_at("nomad-a", start, *f.grid, {"base", "station"}),
                                     band_at("nomad-b", f.grid->node_index(20, 16), *f.grid,
                                             {"base", "station", "terminal"})};
    bands[0].classification = "bandits";
    bands[0].disposition = "hostile";
    bands[0].carries = {"loot"};
    bands[0].seeks = {"a caravan"};
    bands[0].god_id = "god_red_field";
    bands[1].classification = "a-classification-nobody-authored";

    BeastMovementRequest r = make_request(f);
    const NestSite site = site_at("american-bison", f.grid->node_index(12, 14), *f.grid);
    r.wildlife = std::span<const NestSite>(&site, 1);
    const BeastMovements block = *add_beast_movements(r);

    EncounterRequest e;
    e.bands = bands;
    e.groups = block.groups;
    const EncounterIndex index = *add_encounters(e);

    check::equal(index.entries.size(), std::size_t{3}, "two bands and a herd");
    check::equal(index.entries[0].group_uid, std::string("nomad-a"), "bands first");
    check::equal(index.entries[1].group_uid, std::string("nomad-b"), "then the rest of them");
    check::equal(index.entries[2].source, std::string("beast_movements"), "then the beasts");
    check::equal(index.entries[0].threat_tier, 4, "bandits on the creature-tier rubric");
    check::equal(index.entries[1].threat_tier, 2, "an unknown classification to fall back to two");
    check::equal(index.entries[0].entity_class, std::string("people"), "a band is people");
    check::equal(index.entries[0].domain, std::string("land"), "and walks on land");
    check::is_true(!index.entries[0].airborne, "and does not fly");
    check::equal(index.entries[0].god_id, std::string("god_red_field"), "its patron");
    check::equal(index.entries[0].carries.at(0), std::string("loot"), "what it carries");
    check::is_true(index.entries[0].host_uid.empty(), "a band follows nobody");
    // Entry indices are assigned in that order, so every occupancy index depends on it.
    check::equal(index.occupancy[0].entry, 0, "the first camp to belong to the first band");
    check::equal(index.occupancy[0].camp_id, bands[0].camps[0].id, "and to be its own camp");
    check::equal(index.occupancy.back().entry, 2, "and the last to belong to the herd");
}

TEST(a_stranded_group_is_findable_by_node_and_sits_in_no_month_at_all) {
    // RECORDED, NOT FIXED. The published limits say a null day window means "here, always";
    // the index encodes the opposite, because an empty month list puts the group in no
    // bucket. On a reference world that is 43% of the groups.
    const Fixture f = make_fixture();
    const int start = f.grid->node_index(8, 16);
    BeastMovementRequest r = make_request(f);
    const NestSite site = site_at("banshees", start, *f.grid);  // no ruins: it strands
    r.beast_nests = std::span<const NestSite>(&site, 1);
    const BeastMovements block = *add_beast_movements(r);
    check::equal(block.stranded, 1, "the group to have stranded");

    EncounterRequest e;
    e.groups = block.groups;
    const EncounterIndex index = *add_encounters(e);
    check::equal(index.entries.size(), std::size_t{1}, "it still earns an entry");
    check::equal(index.occupancy.size(), std::size_t{1}, "with its start camp");
    check::is_true(!index.occupancy[0].from_day.has_value(), "and no schedule");
    check::is_true(index.occupancy[0].months.empty(), "so no month claims it");
    check::equal(index.by_node.size(), std::size_t{1}, "it is findable by node");
    check::equal(index.by_node[0].first, start, "at its own node");
    for (const auto& month : index.by_month) {
        check::is_true(month.empty(), "and findable in no month whatsoever");
    }
}

TEST(a_camp_window_covers_both_its_endpoints_and_wraps_the_turn_of_the_year) {
    // Both endpoints inclusive, so a camp held for one day still occupies a whole month.
    check::equal(window_months(0, 0, 360).size(), std::size_t{1}, "a one-day camp");
    check::equal(window_months(0, 0, 360).at(0), 0, "in the first month");
    check::equal(window_months(29, 31, 360).size(), std::size_t{2}, "a camp spanning a boundary");
    check::equal(window_months(29, 31, 360).at(1), 1, "reaches the second month");
    check::equal(window_months(0, 359, 360).size(), std::size_t{12}, "a year-long camp");
    const std::vector<int> wrapped = window_months(330, 45, 360);
    check::equal(wrapped.size(), std::size_t{3}, "a camp held across new year");
    check::equal(wrapped.front(), 11, "starting in the last month");
    check::equal(wrapped.at(1), 0, "carrying on into the first");
    check::equal(wrapped.back(), 1, "and ending in the second");
    // A null endpoint is a real answer and not a zero.
    check::is_true(window_months(std::nullopt, 40, 360).empty(), "a null arrival");
    check::is_true(window_months(40, std::nullopt, 360).empty(), "a null departure");
    // Day 360 cannot index a thirteenth month.
    check::equal(window_months(359, 359, 360).at(0), 11, "the last day of the year");
}

// --- the gates --------------------------------------------------------------

TEST(a_design_space_grid_is_refused_and_both_passes_no_op_below_stage_sixteen) {
    const Fixture f = make_fixture();
    BeastMovementRequest r = make_request(f);
    const NestSite site = site_at("american-bison", f.grid->node_index(8, 16), *f.grid);
    r.wildlife = std::span<const NestSite>(&site, 1);
    check::is_true(add_beast_movements(r).has_value(), "the fixture itself to pass");

    // The reaches are absolute metres against an already-scaled spacing. A design-space
    // grid compares them with design units and every gate answers a different question.
    BeastMovementRequest wrong = r;
    wrong.globe_radius_m = f.radius * 5.6;
    check::throws([&] { add_beast_movements(wrong); }, "a grid that is not the world's");
    BeastMovementRequest headless = r;
    headless.grid = nullptr;
    check::throws([&] { add_beast_movements(headless); }, "a request with no grid");

    BeastMovementRequest early = r;
    early.phase = 15;
    check::is_true(!add_beast_movements(early).has_value(), "no block below phase 16");
    BeastMovementRequest recipeless = r;
    recipeless.world_recipe = 0;
    check::is_true(!add_beast_movements(recipeless).has_value(), "and none without a recipe");

    EncounterRequest e;
    e.phase = 15;
    check::is_true(!add_encounters(e).has_value(), "the index to be gated the same way");
    e.phase = 16;
    check::is_true(add_encounters(e).has_value(), "and to run on an empty world");
    check::equal(add_encounters(e)->by_month.size(), std::size_t{12},
                 "all twelve months present even when empty");
}

TEST(a_site_whose_species_the_catalogue_does_not_name_is_skipped_without_being_counted) {
    const Fixture f = make_fixture();
    const std::vector<NestSite> sites{
        site_at("a-species-that-was-never-authored", f.grid->node_index(8, 16), *f.grid),
        site_at("american-bison", f.grid->node_index(9, 16), *f.grid),
        site_at("aardvark", f.grid->node_index(10, 16), *f.grid)};  // a real NESTER
    BeastMovementRequest r = make_request(f);
    r.wildlife = sites;
    const BeastMovements block = *add_beast_movements(r);
    check::equal(block.considered, 1, "only the mover to be considered");
    check::equal(block.groups.size(), std::size_t{1}, "and only it to become a group");
    check::equal(block.groups[0].uid, uid_of("american-bison", f.grid->node_index(9, 16)),
                 "the herd, and not the nester or the phantom");
}

// --- end to end on a world this library can actually build ------------------

TEST(the_pass_runs_on_a_generated_world_and_routes_what_the_ground_allows) {
    Request request;
    request.seed = 42;
    request.size = 33;
    request.phase = 10;
    const World world = generate(resolve(request));
    // effective, not config: config is design space and every reach here is metres.
    const std::shared_ptr<const SphereGrid> grid =
        sphere_grid(world.config.size, world.effective.globe_radius);
    const NestPasses nests = *add_nests(world, *grid, {});

    BeastMovementRequest r;
    r.layers = &world.layers;
    r.grid = grid.get();
    r.globe_radius_m = world.effective.globe_radius;
    r.settlement_spacing_m = world.effective.settlement_spacing;
    r.road_max_grade = world.effective.road_max_grade;
    r.seed = world.effective.seed;
    r.genesis_seed = world.config.seed;
    r.wildlife = nests.wildlife.sites;
    r.beast_nests = nests.beast_nests.sites;
    const BeastMovements block = *add_beast_movements(r);

    check::is_true(block.considered > 20, "a world this size to place movers at all");
    check::equal(block.groups.size(), static_cast<std::size_t>(block.considered),
                 "and every one of them to become a group at share 1.0");
    check::is_true(beast_counts_balance(block), "the three totals to balance");
    check::is_true(block.counts.size() >= 3, "at least three classes on the ground");
    check::between(block.year_forage_factor, 0.55, 1.25, "the year's weather");
    check::equal(block.version, 2, "the block version");

    // The spacing really is the scaled one, and it is not the design-space 450.
    check::is_true(world.effective.settlement_spacing > 5000.0,
                   "the spacing to have been scaled to this world");

    for (const BeastGroup& group : block.groups) {
        check::is_true(!group.camps.empty(), "every group to hold at least one camp");
        check::is_true(group.size >= 2, "a group to be more than one animal");
        check::is_true(group.speed_m_per_day > 0.0, "and to have a gait");
        if (group.route_status == BeastRouteStatus::Routed) {
            check::is_true(!group.legs.empty(), "a routed group to walk something");
        } else {
            check::equal(group.camps.size(), std::size_t{1}, "and anything else to hold one camp");
            check::is_true(group.legs.empty(), "and to walk nothing");
        }
    }

    EncounterRequest e;
    e.groups = block.groups;
    const EncounterIndex index = *add_encounters(e);
    check::equal(index.entries.size(), block.groups.size(), "an entry per group");
    std::size_t camps = 0;
    for (const BeastGroup& group : block.groups) camps += group.camps.size();
    check::equal(index.occupancy.size(), camps, "an occupancy record per camp");
    check::is_true(!index.by_node.empty(), "and a node index with something in it");
}
