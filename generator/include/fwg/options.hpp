// The world-options control surface.
//
// These are the knobs a request may set that are not `Config` fields — 164 of them,
// covering ocean islands, coasts, cold, beast nests, nomads, the moon, the magical
// networks and the thirteen regions. The table is generated from data/world-options.json
// by tools/embed_content.py; the ids live in the generated `option_ids.hpp`.
//
// Values are doubles throughout, including the integer-typed ones, because that is
// what every consumer does with them. `is_integer` says which must be whole.
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "fwg/option_ids.hpp"

namespace fwg {

struct OptionSpec {
    std::string_view name;
    double default_value;
    double min_value;
    double max_value;
    bool is_integer;
    std::string_view units;
    std::string_view group;
};

// Sorted by name; index equals the Option id.
std::span<const OptionSpec> option_specs();
const OptionSpec& option_spec(Option option);

// Nullopt for an unknown name — a request naming an option that does not exist is a
// caller error, not something to silently ignore.
std::optional<Option> option_by_name(std::string_view name);

// A resolved set of option values. Starts at the published defaults; `set` validates
// against the option's own bounds and throws std::invalid_argument naming the field.
//
// Refusing rather than clamping is deliberate and matches the reference: a clamped
// control stops responding past its bound without saying so, and the caller reads a
// world built from values it did not ask for.
class WorldOptions {
public:
    WorldOptions();

    double operator[](Option option) const { return values_[static_cast<std::size_t>(option)]; }
    double get(Option option) const { return (*this)[option]; }
    int as_int(Option option) const;

    void set(Option option, double value);

    // Rules that no single option can enforce, because they are about a COMBINATION.
    // `set` can only ever check one knob against its own bounds; a rule like "at least
    // one island character must be possible" is legal for every individual value and
    // illegal only together. Throws std::invalid_argument naming every field involved.
    void validate() const;

    // Whether this option was explicitly set rather than left at its default.
    bool overridden(Option option) const { return overridden_[static_cast<std::size_t>(option)]; }

private:
    double values_[kOptionCount];
    bool overridden_[kOptionCount];
};

}  // namespace fwg
