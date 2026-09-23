// Stage 16, "Simulation complete": the planners, the bands, and the packages that read a
// finished world -- heroes, the story web, the rosters, the key locations.
#pragma once

namespace fwg {

struct World;

// Runs stage 16 over whatever world exists. Every pass here reads the world rather than
// changing its ground, so it runs the same whether or not the age transitions ran first.
void add_simulation_complete(World& world);

}  // namespace fwg
