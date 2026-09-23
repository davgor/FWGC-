// SHA-256, used only to derive one subsystem seed from another.
//
// Hand-rolled so the library has no dependencies at all and drops into an engine
// build unchanged. It is not on any hot path.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace fwg {

std::array<std::uint8_t, 32> sha256(std::string_view data);

// Hex digest, lowercase.
std::string sha256_hex(std::string_view data);

// First four digest bytes, big-endian, as the reference derives a child seed.
std::uint32_t sha256_seed(std::string_view data);

}  // namespace fwg
