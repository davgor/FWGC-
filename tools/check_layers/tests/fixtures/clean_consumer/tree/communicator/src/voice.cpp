// The communicator reaches the orchestrator and inference, and never names fwg.
#include "inference/runtime.hpp"
#include "orchestrator/packet.hpp"

namespace communicator {

int say(int packet) { return packet; }

}  // namespace communicator
