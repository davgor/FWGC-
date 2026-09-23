// Wizard colleges.
#include "check.hpp"

#include <cmath>
#include <limits>
#include <set>
#include <string>

#include "fwg/colleges.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

// Size 129, seed 7, seats both colleges it asks for. Coarser rasters place none at all
// -- the magic density a college needs is too rare a cell to sample -- and a test on a
// world with no colleges proves nothing about where colleges go.
World collegiate() {
    Request r;
    r.seed = 7;
    r.size = 129;
    r.phase = 12;
    return generate(resolve(r));
}

const CivilizationTraits& any_traits() { return civilization_profile(civilization_ids().front())->traits; }

}  // namespace

TEST(the_first_limit_a_cell_fails_is_the_one_reported) {
    // The order is the meaning. A cell that fails both density and flood reports density,
    // because density is tested first; reordering the tests would change every diagnostic
    // without changing a single placement, which is exactly why the order is pinned.
    const CivilizationTraits& t = any_traits();
    const auto both = college_refusal(0.0, 0.0, 1.0, 0.0, (t.college_temperature_min + t.college_temperature_max) / 2,
                                      0.0, 1e9, 1.0, 0.0, t);
    check::is_true(both.has_value(), "a cell failing twice to be refused");
    check::is_true(*both == CollegeLimit::Density, "the earlier limit to be the one reported");

    const auto flood_only = college_refusal(1.0, 0.0, 1.0, 0.0,
                                            (t.college_temperature_min + t.college_temperature_max) / 2, 0.0,
                                            1e9, 1.0, 0.0, t);
    check::is_true(flood_only.has_value() && *flood_only == CollegeLimit::Flood,
                   "the same cell with enough magic to be refused for its flooding");
}

TEST(a_cell_that_clears_every_limit_is_admitted) {
    // The floor for the refusal tests: without a cell that passes, a gate that refused
    // everything would satisfy them all.
    const CivilizationTraits& t = any_traits();
    const auto admitted = college_refusal(1.0, 0.0, 1.0, 0.0,
                                          (t.college_temperature_min + t.college_temperature_max) / 2,
                                          0.0, 0.0, 1.0, 0.0, t);
    check::is_true(!admitted.has_value(), "an ideal cell to be admitted");
}

TEST(a_missing_value_refuses_rather_than_admits) {
    // Every limit is written as a negated admission, so NaN -- a field nobody filled in
    // -- refuses. Written the other way round, a NaN compares false against every floor
    // and ceiling and sails through all eight.
    const CivilizationTraits& t = any_traits();
    const double mid = (t.college_temperature_min + t.college_temperature_max) / 2;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    check::is_true(college_refusal(nan, 0.0, 1.0, 0.0, mid, 0.0, 0.0, 1.0, 0.0, t).has_value(), "NaN density");
    check::is_true(college_refusal(1.0, nan, 1.0, 0.0, mid, 0.0, 0.0, 1.0, 0.0, t).has_value(), "NaN hazard");
    check::is_true(college_refusal(1.0, 0.0, 1.0, nan, mid, 0.0, 0.0, 1.0, 0.0, t).has_value(), "NaN slope");
    check::is_true(college_refusal(1.0, 0.0, 1.0, 0.0, nan, 0.0, 0.0, 1.0, 0.0, t).has_value(), "NaN climate");
    check::is_true(college_refusal(1.0, 0.0, 1.0, 0.0, mid, nan, 0.0, 1.0, 0.0, t).has_value(), "NaN water");
}

TEST(no_reachable_fresh_water_refuses_a_college) {
    // The settlement stage publishes -1 for "no fresh water reachable". A college needs
    // water within its own reach, and -1 is not a distance.
    const CivilizationTraits& t = any_traits();
    const auto r = college_refusal(1.0, 0.0, 1.0, 0.0, (t.college_temperature_min + t.college_temperature_max) / 2,
                                   -1.0, 0.0, 1.0, 0.0, t);
    check::is_true(r.has_value() && *r == CollegeLimit::Freshwater, "an unwatered cell to be refused for it");
}

TEST(the_spacing_between_colleges_scales_with_the_planet) {
    // Unlike most authored distances here, this one does: a fraction of the radius, with a
    // floor for a small world.
    check::close(college_spacing_m(1000.0), 1500.0, 1e-9, "a small world to take the floor");
    check::close(college_spacing_m(31831.0), 31831.0 * 0.15, 1e-9, "a 200 km world to take a fraction");
    check::is_true(college_spacing_m(95493.0) > college_spacing_m(31831.0), "a wider world to space them wider");
}

TEST(a_world_seats_colleges_and_says_how) {
    const World w = collegiate();
    const CollegeReport& c = w.colleges;
    check::is_true(!c.colleges.empty(), "this world to seat at least one college, or nothing below is tested");
    check::equal(c.diagnostic.placed, static_cast<int>(c.colleges.size()), "the diagnostic to count what was seated");
    check::is_true(c.diagnostic.candidates >= c.diagnostic.placed,
                   "candidates to count every eligible cell, including ones the budget never reached");
    // The distinction the diagnostic exists for. This world fills its budget, so it had
    // MORE eligible ground than it used -- and a count that stopped at the budget would
    // report exactly as many candidates as colleges, making "the budget stopped it" and
    // "the world ran out of ground" the same zero again. `>=` above cannot tell them apart.
    //
    // Every candidate is one of four things: seated, refused for crowding a settlement,
    // refused for crowding a college, or never reached because the budget was full. A
    // count that stopped at the budget would make the first three the WHOLE of it. A
    // bare `candidates > placed` does not catch that -- cells refused for crowding before
    // the budget filled still inflate it -- which is why the first version of this test
    // passed against exactly the defect it was written for.
    check::equal(c.diagnostic.placed, c.diagnostic.wanted, "this world to fill its budget");
    const int accounted = c.diagnostic.placed + c.refused_clearance + c.refused_spacing;
    check::is_true(c.diagnostic.candidates > accounted,
                   "a full budget to leave candidates it never reached (" +
                       std::to_string(c.diagnostic.candidates) + " candidates, " + std::to_string(accounted) +
                       " seated or refused for crowding)");
    check::is_true(c.diagnostic.sources >= c.diagnostic.candidates, "sources to bound candidates");
    check::is_true(!c.diagnostic.reason.empty(), "the diagnostic to say why");
    bool recorded = false;
    for (const std::string& s : w.systems) recorded = recorded || s == "colleges";
    check::is_true(recorded, "the systems list to record that colleges ran");
}

TEST(every_college_clears_every_limit_under_its_owners_eyes) {
    // Placement and the refusal predicate must agree: a college that the predicate would
    // refuse means the report and the thing it reports on have drifted apart.
    const World w = collegiate();
    const auto held = sphere_grid(w.effective.size, w.effective.globe_radius);
    const auto at = [&](const char* layer, int node) {
        const auto n = static_cast<std::size_t>(node);
        return w.layers.at(layer).at(held->point_x[n], held->point_z[n]);
    };
    for (const College& college : w.colleges.colleges) {
        const CivilizationProfile* p = civilization_profile(college.population_profile);
        check::is_true(p != nullptr, college.id + " to belong to a real people");
        if (p == nullptr) continue;
        const auto r = college_refusal(college.density, college.hazard, p->traits.mutation_limit,
                                       at("slope", college.node), at("temperature", college.node),
                                       at("freshwater_distance", college.node), at("flood_risk", college.node),
                                       college.suitability, at("water_type", college.node), p->traits);
        check::is_true(!r.has_value(), college.id + " to clear every limit");
    }
}

TEST(colleges_stand_apart_from_each_other_and_from_every_settlement) {
    const World w = collegiate();
    const double r = w.effective.globe_radius;
    const auto& colleges = w.colleges.colleges;
    for (std::size_t a = 0; a < colleges.size(); ++a) {
        const Vec3 pa = direction(colleges[a].x, colleges[a].z, w.effective.size);
        for (std::size_t b = a + 1; b < colleges.size(); ++b) {
            const Vec3 pb = direction(colleges[b].x, colleges[b].z, w.effective.size);
            check::is_true(great_circle_m(r, pa, pb) >= w.colleges.spacing_m,
                           colleges[a].id + " and " + colleges[b].id + " to keep the regional spacing");
        }
        std::set<int> taken;
        for (const SettlementSite& s : w.settlements.sites) taken.insert(s.node);
        for (const RuralSite& h : w.humans.hamlets) taken.insert(h.node);
        for (const RuralSite& f : w.humans.fortresses) taken.insert(f.node);
        check::is_true(taken.count(colleges[a].node) == 0, colleges[a].id + " not to stand on a settlement");
    }
}

TEST(a_colleges_road_home_is_a_real_path_to_its_own_city) {
    // The access path is walked back through the OWNING city's own search, so its last
    // node is that city and every step is one edge of the grid.
    const World w = collegiate();
    const auto held = sphere_grid(w.effective.size, w.effective.globe_radius);
    for (const College& college : w.colleges.colleges) {
        check::is_true(!college.access_nodes.empty(), college.id + " to have a path");
        check::equal(college.access_nodes.front(), college.node, college.id + " path to start at the college");
        check::is_true(college.core_id >= 0, college.id + " to have an owner");
        const int home = w.settlements.sites[static_cast<std::size_t>(college.core_id)].node;
        check::equal(college.access_nodes.back(), home, college.id + " path to end at its own city");
        for (std::size_t k = 0; k + 1 < college.access_nodes.size(); ++k) {
            bool adjacent = false;
            for (const SphereNeighbour& nb : held->neighbours[static_cast<std::size_t>(college.access_nodes[k])]) {
                adjacent = adjacent || nb.index == college.access_nodes[k + 1];
            }
            check::is_true(adjacent, college.id + " path step " + std::to_string(k) + " to be one edge");
        }
        check::is_true(college.access_cost <= w.effective.support_reach,
                       college.id + " to stand within its city's support reach");
    }
}

TEST(a_world_that_asks_for_no_colleges_seats_none_and_says_so) {
    Request r;
    r.seed = 7;
    r.size = 65;
    r.phase = 12;
    r.college_count = 0;
    const World w = generate(resolve(r));
    check::equal(w.colleges.colleges.size(), std::size_t{0}, "no colleges");
    check::equal(w.colleges.diagnostic.reason, std::string("no colleges were requested"),
                 "the reason to be the request, not a failure of the ground");
}
