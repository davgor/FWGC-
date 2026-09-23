// Just enough of a JSON reader to take a world document apart at its seams.
//
// The library writes JSON and, deliberately, has no general reader: nothing it does needs a
// document tree, and a DOM of a size-513 world is gigabytes. What the stage snapshots need
// is narrower -- split an object into its members and an array into its elements, each as
// the RAW TEXT the writer produced -- so a snapshot can compare a block by its bytes, keep it
// without re-serialising it, and splice it back into a document verbatim.
//
// Values are string_views into the caller's text; nothing is copied or converted except a
// member's key, which is decoded. Structure is checked (brackets balance, strings close,
// members have a colon); scalars are delimited, not validated. Malformed input throws
// std::invalid_argument naming the byte where it went wrong.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace fwg::json_scan {

struct Member {
    std::string key;         // decoded
    std::string_view value;  // raw, trimmed
};

// The members of the object in `text`, in document order. `text` may carry surrounding
// whitespace.
std::vector<Member> object_members(std::string_view text);

// The elements of the array in `text`, raw and trimmed, in order.
std::vector<std::string_view> array_items(std::string_view text);

// A JSON string literal, decoded (escapes, including \uXXXX and surrogate pairs, to UTF-8).
std::string string_value(std::string_view text);

// A JSON integer.
long long integer_value(std::string_view text);

bool is_null(std::string_view text);

// `text` as a JSON string literal, escaped the way JsonWriter escapes one.
std::string quoted(std::string_view text);

}  // namespace fwg::json_scan
