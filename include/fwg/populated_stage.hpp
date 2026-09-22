// Stage 12, "Populated regions".
//
// The hinterland, the seasons and the society passes each return a value rather than
// editing the world. This is the one place that turns those values into a world, which
// makes it the one place that has to get the order and the mutations right -- including
// the ones the society pass deliberately hands back rather than performing, because
// performing them twice is the reference's own bug.
#pragma once

namespace fwg {

struct World;

// Runs stage 12. Needs stage 11: without roads there is no culture link, nothing to
// defend and nothing to trade over. Does nothing on a world with no cities.
void add_populated_regions(World& world);

}  // namespace fwg
