// The reference's conformance fixtures, carried verbatim under tests/fixtures/.
//
// They were authored independently of the Python oracle (the files say how), which is what
// makes them worth more than values copied out of either implementation: a port that agrees
// with them agrees with the contract, not with a sibling's bugs.
#pragma once

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "check.hpp"
#include "fwg/json_value.hpp"
#include "fwg/patch.hpp"

namespace product_test {

inline std::string fixture_text(std::string_view name) {
    const std::string path = std::string(FWG_TEST_FIXTURES) + "/" + std::string(name);
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot read fixture " + path);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

inline fwg::JsonValue fixture(std::string_view name) { return fwg::parse_json(fixture_text(name)); }

// The refusal a callable raises, or a test failure if it raises none.
template <typename Fn>
fwg::PatchRefusal refusal_of(Fn&& fn) {
    try {
        fn();
    } catch (const fwg::PatchError& error) {
        return error.refusal();
    }
    check::fail("expected a PatchError, none was thrown");
}

}  // namespace product_test
