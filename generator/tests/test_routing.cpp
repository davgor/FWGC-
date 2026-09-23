// Routing and capacity invariants.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

#include "fwg/routing.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

// Plain geometric cost: every edge passable at its own length.
auto plain() {
    return [](int, int, double d) -> std::optional<double> { return d; };
}

World build(int size = 33) {
    Request r;
    r.seed = 42;
    r.size = size;
    r.phase = 9;
    return generate(resolve(r));
}

std::vector<double> node_values(const World& w, const SphereGrid& g, const char* layer) {
    const Grid& grid = w.layers.at(layer);
    std::vector<double> out(g.node_count());
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = grid.at(g.point_x[i], g.point_z[i]);
    return out;
}

}  // namespace

TEST(shortest_paths_reaches_everything_on_a_connected_sphere) {
    const auto held = sphere_grid(17, 1000.0);
    const PathResult r = shortest_paths(*held, 0, plain());
    check::equal(r.distance[0], 0.0, "zero at the source");
    for (std::size_t i = 1; i < held->node_count(); ++i) {
        check::is_true(std::isfinite(r.distance[i]), "every node reachable on an open sphere");
        check::is_true(r.parent[i] >= 0, "and via a parent");
    }
    // The far side of a sphere of radius R is pi*R away, and a grid path is at least
    // that and at most a modest factor more.
    const double antipode = *std::max_element(r.distance.begin(), r.distance.end());
    const double straight = std::acos(-1.0) * 1000.0;
    check::is_true(antipode >= straight * 0.99, "no path shorter than the great circle");
    check::is_true(antipode < straight * 2.0, "and none absurdly longer");
}

TEST(a_bounded_search_agrees_with_an_unbounded_one_inside_the_bound) {
    // The property that makes bounding exact rather than approximate: a node inside the
    // limit keeps the distance and the parent it would have had unbounded. Nodes
    // outside stay at infinity, which is why a bounded result is only readable inside.
    const auto held = sphere_grid(17, 1000.0);
    const PathResult full = shortest_paths(*held, 5, plain());
    const double limit = 2000.0;
    const PathResult bounded = shortest_paths(*held, 5, plain(), nullptr, limit);

    int inside = 0;
    for (std::size_t i = 0; i < held->node_count(); ++i) {
        if (full.distance[i] <= limit) {
            ++inside;
            check::close(bounded.distance[i], full.distance[i], 1e-9, "same distance inside the bound");
            check::equal(bounded.parent[i], full.parent[i], "same parent inside the bound");
        } else {
            check::is_true(!std::isfinite(bounded.distance[i]), "outside the bound stays unreachable");
        }
    }
    check::is_true(inside > 1, "the bound to admit more than the source");
    check::is_true(inside < static_cast<int>(held->node_count()), "and to exclude something");
}

TEST(a_targeted_search_finds_its_targets) {
    const auto held = sphere_grid(17, 1000.0);
    const std::vector<int> targets{7, 40, 100};
    const PathResult full = shortest_paths(*held, 0, plain());
    const PathResult early = shortest_paths(*held, 0, plain(), &targets);
    for (const int t : targets) {
        check::close(early.distance[static_cast<std::size_t>(t)], full.distance[static_cast<std::size_t>(t)],
                     1e-9, "a target to get its true distance");
    }
}

TEST(an_impassable_world_reaches_nothing) {
    const auto held = sphere_grid(17, 1000.0);
    const auto refuse = [](int, int, double) -> std::optional<double> { return std::nullopt; };
    const PathResult r = shortest_paths(*held, 0, refuse);
    check::equal(r.distance[0], 0.0, "the source is still where it is");
    for (std::size_t i = 1; i < held->node_count(); ++i) {
        check::is_true(!std::isfinite(r.distance[i]), "and nothing else is reachable");
    }
}

TEST(allocate_access_gives_every_node_one_owner_and_breaks_ties_low) {
    const auto held = sphere_grid(17, 1000.0);
    const std::vector<std::pair<int, int>> seeds{{0, 0}, {50, 1}, {150, 2}};
    const AccessResult a = allocate_access(*held, seeds, plain());

    for (std::size_t i = 0; i < held->node_count(); ++i) {
        check::is_true(a.owner[i] >= 0 && a.owner[i] < 3, "every node claimed by a real seed");
        check::is_true(std::isfinite(a.distance[i]), "and at a finite distance");
    }
    for (const auto& [node, label] : seeds) {
        check::equal(a.distance[static_cast<std::size_t>(node)], 0.0, "a seed is at its own door");
        check::equal(a.owner[static_cast<std::size_t>(node)], label, "and owns itself");
    }

    // Every owner's distance is the minimum over the seeds, so allocation agrees with
    // running the single-source search once per seed.
    std::vector<PathResult> singly;
    for (const auto& [node, label] : seeds) {
        (void)label;
        singly.push_back(shortest_paths(*held, node, plain()));
    }
    for (std::size_t i = 0; i < held->node_count(); ++i) {
        double best = kUnreachable;
        int best_label = -1;
        for (std::size_t k = 0; k < singly.size(); ++k) {
            if (singly[k].distance[i] < best) {
                best = singly[k].distance[i];
                best_label = static_cast<int>(k);
            }
        }
        check::close(a.distance[i], best, 1e-9, "allocation to match the per-seed minimum");
        if (std::abs(a.distance[i] - best) < 1e-12) {
            // Ties go to the lower label; where there is no tie the owner is the winner.
            check::is_true(a.owner[i] <= best_label, "ties resolved toward the lower label");
        }
    }
}

TEST(allocate_access_respects_its_reach) {
    const auto held = sphere_grid(17, 1000.0);
    const double reach = 1500.0;
    const AccessResult a = allocate_access(*held, {{0, 0}}, plain(), reach);
    int owned = 0;
    for (std::size_t i = 0; i < held->node_count(); ++i) {
        if (a.owner[i] < 0) continue;
        ++owned;
        check::is_true(a.distance[i] <= reach, "nothing claimed beyond the reach");
    }
    check::is_true(owned > 1, "something claimed inside it");
    check::is_true(owned < static_cast<int>(held->node_count()), "and not the whole world");
}

// --- roads ----------------------------------------------------------------

TEST(a_road_refuses_water_cliffs_and_corner_cutting) {
    const World world = build(33);
    const auto held = sphere_grid(world.config.size, world.effective.globe_radius);
    const SphereGrid& grid = *held;

    const std::vector<double> water = node_values(world, grid, "water_type");
    const std::vector<double> height = node_values(world, grid, "height");
    const std::vector<double> river = node_values(world, grid, "river");
    const std::vector<double> flood(grid.node_count(), 0.0);
    const RoadCost cost(grid, world.effective, water, height, flood, river);

    int crossings = 0;
    int refusals = 0;
    for (std::size_t i = 0; i < grid.node_count(); ++i) {
        for (const SphereNeighbour& nb : grid.neighbours[i]) {
            const auto j = static_cast<std::size_t>(nb.index);
            const std::optional<double> c = cost(static_cast<int>(i), nb.index, nb.distance);
            if (!c) {
                ++refusals;
                continue;
            }
            ++crossings;
            check::is_true(water[i] == 0.0 && water[j] == 0.0, "a road never enters water");
            const double grade = std::abs(height[j] - height[i]) / nb.distance;
            check::is_true(grade <= world.effective.road_max_grade + 1e-12, "and never exceeds its grade");
            check::is_true(*c >= nb.distance, "a road is never cheaper than the ground it covers");
            if (river[i] != 0.0 || river[j] != 0.0) {
                check::is_true(*c >= nb.distance + world.effective.bridge_cost - 1e-9,
                               "a river crossing pays for its bridge");
            }
        }
    }
    check::is_true(crossings > 0, "some ground to be passable");
    check::is_true(refusals > 0, "and some not");
}

TEST(a_steeper_road_costs_more_than_a_flat_one_of_the_same_length) {
    // Grade is charged quadratically, so a gentle detour beats a short climb. Without
    // that, roads run straight over ridges.
    const auto held = sphere_grid(9, 100000.0);
    const SphereGrid& grid = *held;
    const std::size_t count = grid.node_count();

    Config cfg;
    cfg.size = 9;
    cfg.road_max_grade = 0.35;
    cfg.bridge_cost = 0.0;
    cfg.human_magic_limit = 1.0;

    const std::vector<double> water(count, 0.0);
    const std::vector<double> river(count, 0.0);
    const std::vector<double> flood(count, 0.0);

    std::vector<double> flat(count, 0.0);
    const RoadCost cheap(grid, cfg, water, flat, flood, river);

    std::vector<double> stepped(count, 0.0);
    const SphereNeighbour& nb = grid.neighbours[1].front();
    stepped[static_cast<std::size_t>(nb.index)] = nb.distance * 0.2;  // a 20% grade
    const RoadCost dear(grid, cfg, water, stepped, flood, river);

    const auto a = cheap(1, nb.index, nb.distance);
    const auto b = dear(1, nb.index, nb.distance);
    check::is_true(a.has_value() && b.has_value(), "both passable");
    check::is_true(*b > *a, "the climb to cost more");
    check::close(*a, nb.distance, 1e-9, "flat ground to cost its own length");
}

// --- capacity -------------------------------------------------------------

TEST(habitat_capacity_counts_whole_communities_only) {
    const std::vector<double> areas{2e6, 2e6, 2e6};  // 2 km2 each
    const std::vector<double> scores{1.0, 1.0, 1.0};
    const std::vector<int> all{0, 1, 2};
    check::equal(habitat_capacity(areas, scores, all, 2.0), 3LL, "six good km2 at two per city");
    check::equal(habitat_capacity(areas, scores, all, 7.0), 0LL,
                 "a world too small for one city gets none, not a bad one");
    // Quality weights the footprint: half-quality ground supports half as much.
    const std::vector<double> poor{0.5, 0.5, 0.5};
    check::equal(habitat_capacity(areas, poor, all, 2.0), 1LL, "poor ground supports less");
    check::equal(habitat_capacity(areas, scores, {}, 2.0), 0LL, "no eligible ground, no cities");
}

TEST(city_capacity_is_bounded_by_land_and_by_people) {
    // 100 km2 at 10 km2 each is ten cities by land; 45 people at 20 each is two by
    // population. The smaller binds, and which one binds is the interesting part.
    check::equal(city_capacity(100.0, 45, 10.0, 20.0), 2LL, "people bind");
    check::equal(city_capacity(15.0, 1000, 10.0, 20.0), 1LL, "land binds");
    check::equal(city_capacity(0.0, 1000, 10.0, 20.0), 0LL, "no land, no cities");
    check::equal(city_capacity(100.0, 0, 10.0, 20.0), 0LL, "no people, no cities");
}

TEST(overlapping_claims_share_a_cell_rather_than_multiplying_it) {
    // One square kilometre at full potential supports a hundred residents. Two peoples
    // who both want it get fifty each -- not a hundred each, which is the bug this
    // rule exists to prevent.
    const std::vector<double> areas{1e6};
    const LifeCapacity one = life_capacity(areas, {{1.0}});
    check::equal(one.total, 100LL, "a hundred residents per productive km2");
    check::equal(one.by_people[0], 100LL, "all of them to the only claimant");

    const LifeCapacity two = life_capacity(areas, {{1.0}, {1.0}});
    check::equal(two.total, 100LL, "two claimants do not double the land");
    check::equal(two.by_people[0], 50LL, "split evenly");
    check::equal(two.by_people[1], 50LL, "both ways");

    // The cell supports what its BEST claimant could, split by their relative claims.
    const LifeCapacity uneven = life_capacity(areas, {{1.0}, {0.25}});
    check::equal(uneven.total, 100LL, "the best claimant sets the capacity");
    check::equal(uneven.by_people[0], 80LL, "and takes four fifths of it");
    check::equal(uneven.by_people[1], 20LL, "the weaker claim takes the rest");

    // Fractions stay unallocated rather than rounding up into people who do not exist.
    const LifeCapacity tiny = life_capacity({1e6}, {{0.005}});
    check::equal(tiny.total, 0LL, "half a resident is not a resident");
    const LifeCapacity none = life_capacity(areas, {{0.0}, {0.0}});
    check::equal(none.total, 0LL, "ground nobody can use supports nobody");
}
