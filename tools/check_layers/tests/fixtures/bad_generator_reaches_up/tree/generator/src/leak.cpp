// The orchestrator is mentioned in a comment, which is fine.
#include "orchestrator/director.hpp"
namespace fwg {
int leak() { return orchestrator::tick(); }
}  // namespace fwg
