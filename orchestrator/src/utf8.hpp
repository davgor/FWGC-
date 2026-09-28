// The one check for text: what the director keeps of the game's words, and every string the C
// interface takes in, must be well-formed UTF-8, or a save could not carry it.
#pragma once

#include <string_view>

namespace orchestrator {

// Well-formed UTF-8: no overlong forms, no surrogates, nothing past U+10FFFF.
bool is_utf8(std::string_view text);

}  // namespace orchestrator
