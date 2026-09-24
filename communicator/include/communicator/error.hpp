#pragma once

#include <cstddef>
#include <stdexcept>

namespace communicator {

// A caller's mistake (a packet of another contract version, a line past kMaxLineBytes), or a
// model answer outside its grammar. The message says which. A model that will not load or run
// throws inference::Error instead.
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The longest line a player may say, in bytes. A game's text box holds its players to it; a
// longer line is refused, never cut.
inline constexpr std::size_t kMaxLineBytes = 1000;

}  // namespace communicator
