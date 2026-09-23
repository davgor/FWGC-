// Stage 12, "Populated regions".
//
// The hinterland, the seasons and the society passes each return a value rather than
// editing the world. This is the one place that turns those values into a world, which
// makes it the one place that has to get the order and the mutations right -- including
// the ones the society pass deliberately hands back rather than performing, because
// performing them twice is the reference's own bug.
#pragma once

#include "fwg/humans.hpp"

namespace fwg {

struct World;

// Runs stage 12. Needs stage 11: without roads there is no culture link, nothing to
// defend and nothing to trade over. Does nothing on a world with no cities.
void add_populated_regions(World& world);

// The hinterland pass's view of the world. It carries the rural rows the world already
// holds, because the pass re-seats every row the PLAYER founded instead of re-deriving it:
// without them a rebuild -- an age advance, or a founding that asks for one -- silently
// drops the player's hamlets and keeps. One adapter, whoever calls the pass.
HumansRequest humans_request(const World& world);

// The hinterland pass ALONE over the world as it stands, installed with its six layers:
// the settlement API's `rebuild: "humans"`, which is the reference's bare `add_humans`.
//
// Nothing downstream of the hinterland re-runs. Colleges, the seasonal food and society
// keep describing the hinterland as it stood before, and the harbours society folds into
// the rural layer drop out of it until the next full stage-12 pass puts them back. Both are
// the reference's behaviour, reproduced rather than tidied: that call re-runs add_humans
// and nothing else. Does nothing on a world without roads, as the pass does.
void rebuild_hinterland(World& world);

}  // namespace fwg
