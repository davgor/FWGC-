#include "fwg/options.hpp"

#include <cmath>
#include <sstream>
#include <stdexcept>

namespace fwg {

WorldOptions::WorldOptions() {
    const auto specs = option_specs();
    for (std::size_t i = 0; i < specs.size(); ++i) {
        values_[i] = specs[i].default_value;
        overridden_[i] = false;
    }
}

int WorldOptions::as_int(Option option) const {
    return static_cast<int>(std::llround((*this)[option]));
}

void WorldOptions::set(Option option, double value) {
    const OptionSpec& spec = option_spec(option);
    if (!std::isfinite(value) || value < spec.min_value || value > spec.max_value) {
        std::ostringstream message;
        message << spec.name << " must be " << spec.min_value << ".." << spec.max_value;
        if (!spec.units.empty()) message << ' ' << spec.units;
        message << "; received " << value;
        throw std::invalid_argument(message.str());
    }
    if (spec.is_integer && value != std::floor(value)) {
        std::ostringstream message;
        message << spec.name << " must be a whole number; received " << value;
        throw std::invalid_argument(message.str());
    }
    const auto i = static_cast<std::size_t>(option);
    values_[i] = value;
    overridden_[i] = true;
}

}  // namespace fwg
