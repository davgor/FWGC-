#include "fwg/world.hpp"

#include "fwg/tectonics.hpp"

namespace fwg {

World generate(const Config&) {
    generate_tectonics(1);
    return {};
}

}  // namespace fwg
