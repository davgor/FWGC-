// A consumer that does everything right: api headers only, fwg:: in full, listed calls only.
#include "fwg/api/world.hpp"

#include <vector>

namespace orchestrator {

std::vector<fwg::World> worlds;

fwg::World make() { return fwg::generate(fwg::Config{}); }

}  // namespace orchestrator
