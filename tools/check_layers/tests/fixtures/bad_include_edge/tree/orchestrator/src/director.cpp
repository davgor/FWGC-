#include "fwg/api/world.hpp"
#include "fwg/tectonics.hpp"
#include "communicator/voice.hpp"
#include <vector>

namespace orchestrator {
fwg::World step(const fwg::World&) { return fwg::generate(fwg::Config{}); }
}  // namespace orchestrator
