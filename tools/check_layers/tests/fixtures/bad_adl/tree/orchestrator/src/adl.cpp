#include "fwg/api/world.hpp"
namespace orchestrator {
void census(int);
void run(const fwg::World& w) {
    plan_city(w);
    census(3);
    generate(fwg::Config{});
}
}  // namespace orchestrator
