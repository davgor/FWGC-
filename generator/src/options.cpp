#include "fwg/options.hpp"

#include <cmath>
#include <sstream>
#include <string>
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

void WorldOptions::validate() const {
    // Every cluster draws its character from these four weights. With all four at zero
    // there is no distribution to draw from -- the reference refuses the combination
    // here rather than letting the draw decide, and so does this. Left to the draw, the
    // honest outcomes are both bad: return a fixed kind and every archipelago in the
    // world silently becomes volcanic, or skip the draw and every later cluster reads a
    // different random stream than it would have.
    static constexpr Option kIslandWeights[] = {Option::volcanic_island_weight, Option::atoll_weight,
                                                Option::continental_island_weight,
                                                Option::cold_island_weight};
    double island_total = 0.0;
    for (const Option option : kIslandWeights) island_total += (*this)[option];
    if (island_total <= 0.0) {
        std::string message =
            "at least one ocean island character weight must be positive; all four are zero, "
            "so no cluster could take a character (";
        for (std::size_t i = 0; i < std::size(kIslandWeights); ++i) {
            if (i != 0) message += ", ";
            message += option_spec(kIslandWeights[i]).name;
        }
        throw std::invalid_argument(message + ')');
    }
}

}  // namespace fwg
