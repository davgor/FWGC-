#pragma once

namespace fwg {

struct Config {};
struct World {};

World generate(const Config& cfg);

// Internal: declared here, never listed in api.txt. MakeInternal is a function spelled like a
// type, which the api-function rule must still see.
World plan_city(const World& world);
void census(const World& world);
World MakeInternal();

}  // namespace fwg
