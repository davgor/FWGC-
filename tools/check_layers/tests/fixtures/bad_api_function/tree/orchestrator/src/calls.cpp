#include "fwg/api/world.hpp"
using namespace fwg;
namespace fwg { void mine(); }
namespace g = fwg;
namespace orchestrator {
void run(const fwg::World& w) {
    fwg::generate(fwg::Config{});
    fwg::plan_city(w);
    fwg::detail::helper();
    const int k = fwg::kSomething;
    fwg::MakeInternal();
}
}  // namespace orchestrator
