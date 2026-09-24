// The communicator depends on the orchestrator, which publishes its contract alone.
#include "orchestrator/contract.hpp"
#include "orchestrator/director.hpp"

namespace communicator {
int act(int line) { return line; }
}  // namespace communicator
