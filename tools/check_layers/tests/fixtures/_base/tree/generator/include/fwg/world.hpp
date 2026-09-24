#pragma once

namespace fwg {

struct Config {};
struct World {};

World generate(const Config& cfg);

// Internal: declared here, never listed in api.txt.
World plan_city(const World& world);
void census(const World& world);

}  // namespace fwg
