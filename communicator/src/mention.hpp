// A name the player says outright needs no guessing.
#pragma once

#include <optional>
#include <string_view>

#include "communicator/known.hpp"

namespace communicator {

// The one thing of `kind` that `line` names outright: its label's words appear in the line, in
// order and whole, ignoring case, typographic punctuation and any part of the label in brackets
// ("Hakndum" names "Hakndum (Age 2)"). When several are named, the one with the longest name
// ("the hamlet of Wenweg" over "Wenweg"); nullopt when none is, or when two tie.
std::optional<Referent> named_outright(Kind kind, const Known& known, std::string_view line);

}  // namespace communicator
