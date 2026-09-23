// Which ground each people will build on, how good it thinks that ground is, and how
// much food the ground around it could grow.
//
// This is stage 10's assessment pass: for every people in the world, a suitability field
// over every node, the subset of nodes it would actually settle, and a food potential
// field for the hinterland those settlements would farm.
//
// Three gates decide whether a people may settle a cell, and they are NOT the same test.
// The POPULATION habitat, from the civilization registry, says whether this people can
// exist here at all. The SETTLEMENT habitat, from the entity rules, says whether it would
// put a city here. And safety, slope and fresh water gate it again on top of both. A cell
// can pass any two and fail the third.
#pragma once

#include <string>
#include <vector>

#include "fwg/config.hpp"
#include "fwg/grid.hpp"

namespace fwg {

struct World;

// What one people makes of the whole world.
struct SpeciesAssessment {
    std::string id;
    // 0..1 per node; zero on water, which nobody builds on.
    std::vector<double> suitability;
    // The nodes this people would actually settle, ascending.
    std::vector<int> candidates;
    // Food this people could win from each node. NOT restricted to the nodes it would
    // settle: a city farms its hinterland, and a people whose rules allow it farms
    // ground outside its own habitat entirely.
    std::vector<double> food_potential;
};

// Assess every named people against this world.
//
// `species` are civilization ids. The caller decides whether that is every entity (a
// mixed world, where they compete for the same ground) or a single named people.
//
// `grid` MUST be built at `world.effective.globe_radius`, not `world.config`'s.
// `config` holds the DESIGN inputs a world can be regenerated from; `effective` holds
// the same geometry in metres, and the two differ by the world scale -- a factor of 5.6
// on the default world. Every reach a people owns (water_reach, support_reach) is in
// absolute metres, so a design-space grid compares metres against design units and every
// gate silently answers a different question. Refused rather than documented.
std::vector<SpeciesAssessment> assess_habitation(const World& world, const SphereGrid& grid,
                                                 const std::vector<std::string>& species);

}  // namespace fwg
