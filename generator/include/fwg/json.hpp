// A minimal JSON writer.
//
// Only writing is supported, and only what a world document needs. Doubles are
// emitted with std::to_chars' shortest round-trip form, so a value written here and
// read back is the same double -- and two builds of this library produce the same
// text for the same world.
#pragma once

#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/grid.hpp"

namespace fwg {

class JsonWriter {
public:
    explicit JsonWriter(std::ostream& out, bool pretty = true) : out_(out), pretty_(pretty) {}

    void begin_object();
    void end_object();
    void begin_array();
    void end_array();

    void key(std::string_view name);

    void value(double v);
    void value(long long v);
    void value(int v) { value(static_cast<long long>(v)); }
    void value(unsigned v) { value(static_cast<long long>(v)); }
    void value(unsigned long v) { value(static_cast<long long>(v)); }
    void value(bool v);
    void value(std::string_view v);
    void value_null();

    // A grid as an array of rows. Written compactly regardless of pretty mode --
    // a size-129 layer is sixteen thousand numbers and one per line helps nobody.
    void value(const Grid& grid);

    void value(const std::vector<std::string>& items);

    // A value that is ALREADY JSON text, written verbatim. Nothing here parses it: the caller
    // vouches that it is one complete value. It exists so captured stage snapshots can be
    // spliced into a document exactly as they were written, rather than re-serialised.
    void raw(std::string_view json_text);

    template <typename T>
    void field(std::string_view name, const T& v) {
        key(name);
        value(v);
    }

private:
    void separator();
    void newline_indent();
    void write_string(std::string_view v);

    std::ostream& out_;
    bool pretty_ = true;
    int depth_ = 0;
    bool need_comma_ = false;
    bool after_key_ = false;
};

// Shortest round-trip text for a double, with non-finite values spelled out rather
// than emitted as bare NaN (which is not valid JSON).
std::string format_double(double v);

}  // namespace fwg
