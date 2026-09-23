// A JSON document tree, a strict reader for it, and the writer the product contracts are
// defined in.
//
// WHY A SECOND JSON MODULE. fwg/json.hpp is a streaming WRITER for the world document and
// deliberately nothing else. The product package -- world handles, the counter kernel, the
// asset list, the capability and control descriptors, `--request` -- needs the other half:
// reading a document, holding it, comparing it and re-emitting it in a canonical form.
//
// WHAT "PYTHON-COMPATIBLE" MEANS HERE, AND WHY IT IS A CONTRACT RATHER THAN A STYLE. Three
// of the published contracts are hashes or byte forms that the reference defines as the
// output of CPython's `json.dumps`: a world handle is sha256 over
// `json.dumps(doc, ensure_ascii=False, sort_keys=True, separators=(",", ":"))`, the
// kernel's canonical bytes are the ensure_ascii=True form of the same, and the asset
// list's `content_sha256` is taken over the ensure_ascii=False form. A second host names
// the same world only if it reproduces those bytes exactly, so `dump_json` reproduces
// `json.dumps` exactly:
//
//   * floats are written as CPython's `repr`: the shortest decimal that reads back as the
//     same double, positional when the decimal point position is in (-4, 16] and
//     scientific otherwise, a positional result always carrying a fractional part
//     ("1.0"), negative zero keeping its sign ("-0.0"), an exponent always signed and at
//     least two digits ("1e+16", "1e-05");
//   * integers and floats stay distinct: "1" and "1.0" are different documents, exactly
//     as `json.loads` then `json.dumps` keeps them apart;
//   * strings escape the same set CPython escapes -- `\b \f \n \r \t`, `\"`, `\\` by name,
//     every other control character as a lowercase `\u00XX`; with ensure_ascii every
//     code point outside printable ASCII becomes `\uXXXX` (a surrogate pair above the
//     BMP, and DEL is escaped), without it non-ASCII is written as raw UTF-8;
//   * sort_keys orders object keys by code point, which is byte order of their UTF-8;
//   * `indent` reproduces CPython's layout, including `[]`/`{}` for empty containers.
//
// The reader follows `json.loads` for everything a document can carry: a repeated key
// keeps its FIRST position and its LAST value (dict semantics); `NaN`, `Infinity` and
// `-Infinity` are accepted unless the caller turns them off; an exponent past the double
// range reads as an infinity and one below it as a signed zero, as `float()` does; an
// integer of any length is kept exactly. It is STRICT where CPython is strict: no control
// characters inside strings, no leading zeros, no trailing commas, no comments, no BOM, and
// the input must be valid UTF-8. An escaped lone surrogate ("\ud800") is kept, as CPython
// keeps it, in generalized UTF-8; it can be read and compared, and writing it as UTF-8
// (ensure_ascii off) is refused exactly where CPython's `.encode("utf-8")` would be.
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace fwg {

// An integer outside the signed 64-bit range, kept as canonical decimal digits: no leading
// zeros and never "-0". Every integer that fits int64 is stored as one instead.
struct JsonBigInteger {
    std::string digits;
    bool operator==(const JsonBigInteger&) const = default;
};

class JsonValue {
public:
    enum class Type { Null, Bool, Integer, Float, String, Array, Object };
    using Array = std::vector<JsonValue>;
    using Member = std::pair<std::string, JsonValue>;
    // Insertion order is kept and keys are unique: `set` replaces in place.
    using Object = std::vector<Member>;

    JsonValue() noexcept = default;
    JsonValue(std::nullptr_t) noexcept {}
    JsonValue(bool value) : data_(value) {}
    template <typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    JsonValue(T value) {
        if constexpr (std::is_unsigned_v<T> && sizeof(T) >= sizeof(std::int64_t)) {
            if (value > static_cast<T>(std::numeric_limits<std::int64_t>::max())) {
                data_ = JsonBigInteger{std::to_string(value)};
                return;
            }
        }
        data_ = static_cast<std::int64_t>(value);
    }
    JsonValue(double value) : data_(value) {}
    JsonValue(const char* text) : data_(std::string(text)) {}
    JsonValue(std::string text) : data_(std::move(text)) {}
    JsonValue(std::string_view text) : data_(std::string(text)) {}
    JsonValue(Array items) : data_(std::move(items)) {}
    JsonValue(Object members) : data_(std::move(members)) {}
    // Canonicalises: a value that fits int64 is stored as one. Throws std::invalid_argument
    // for text that is not an optionally signed run of decimal digits.
    explicit JsonValue(JsonBigInteger big);

    static JsonValue array() { return JsonValue(Array{}); }
    static JsonValue object() { return JsonValue(Object{}); }

    Type type() const noexcept;
    // CPython's name for the type -- "NoneType", "bool", "int", "float", "str", "list",
    // "dict" -- because refusal messages quote it the way the reference does.
    std::string_view type_name() const noexcept;

    bool is_null() const noexcept { return std::holds_alternative<std::monostate>(data_); }
    bool is_bool() const noexcept { return std::holds_alternative<bool>(data_); }
    // True for either width of integer. A bool is NOT an integer here, unlike Python's
    // isinstance; every contract in this package spells that out (`type(v) is not int`).
    bool is_integer() const noexcept {
        return std::holds_alternative<std::int64_t>(data_) || std::holds_alternative<JsonBigInteger>(data_);
    }
    bool is_int64() const noexcept { return std::holds_alternative<std::int64_t>(data_); }
    bool is_big_integer() const noexcept { return std::holds_alternative<JsonBigInteger>(data_); }
    bool is_float() const noexcept { return std::holds_alternative<double>(data_); }
    // An integer or a float. Never a bool.
    bool is_number() const noexcept { return is_integer() || is_float(); }
    bool is_string() const noexcept { return std::holds_alternative<std::string>(data_); }
    bool is_array() const noexcept { return std::holds_alternative<Array>(data_); }
    bool is_object() const noexcept { return std::holds_alternative<Object>(data_); }

    // Each throws std::logic_error naming the type actually held.
    bool as_bool() const;
    std::int64_t as_int() const;  // int64 only; a big integer throws
    const JsonBigInteger& as_big_integer() const;
    double as_float() const;
    const std::string& as_string() const;
    const Array& as_array() const;
    Array& as_array();
    const Object& as_object() const;
    Object& as_object();

    // Python's float(x) for a number: exact for a float, correctly rounded for an integer,
    // and nullopt where float() raises OverflowError (an integer past the double range).
    // nullopt for anything that is not a number, a bool included.
    std::optional<double> to_double() const;

    // --- objects ------------------------------------------------------------------
    const JsonValue* find(std::string_view key) const noexcept;
    JsonValue* find(std::string_view key) noexcept;
    bool contains(std::string_view key) const noexcept { return find(key) != nullptr; }
    // Throws std::out_of_range naming the key, or std::logic_error if not an object.
    const JsonValue& at(std::string_view key) const;
    JsonValue& at(std::string_view key);
    // Insert, or replace in place keeping the key's position (Python `d[key] = value`).
    // Returns the stored value. Throws std::logic_error if not an object.
    JsonValue& set(std::string_view key, JsonValue value);
    bool erase(std::string_view key);

    // --- arrays -------------------------------------------------------------------
    void push_back(JsonValue value);
    const JsonValue& at(std::size_t index) const;
    JsonValue& at(std::size_t index);

    // Members of an object or items of an array; 0 for a scalar.
    std::size_t size() const noexcept;

    // STRUCTURAL equality: same type, same value, same member order -- so 1 != 1.0 and a
    // reordered object differs. Use `python_equal` for the reference's `==`, which is what
    // every contract comparison in this package means.
    bool operator==(const JsonValue& other) const = default;

private:
    std::variant<std::monostate, bool, std::int64_t, JsonBigInteger, double, std::string, Array, Object> data_;
};

// A document that cannot be read or cannot be written.
//
// Distinct from a request refusal (PatchError): this is the codec saying the bytes are not
// JSON, or that a value has no JSON spelling -- the reference's JSONDecodeError and the
// ValueError json.dumps raises for a NaN. `where` is a byte offset for a read and a dotted
// path for a write.
class JsonError : public std::invalid_argument {
public:
    JsonError(const std::string& message, std::string where);
    const std::string& where() const noexcept { return where_; }

private:
    std::string where_;
};

struct JsonParseOptions {
    // False keeps the LAST value of a repeated key at the FIRST key's position, which is
    // what `json.loads` does. True refuses the document instead (the kernel's rule).
    bool reject_duplicate_keys = false;
    // `NaN`, `Infinity` and `-Infinity`, which `json.loads` accepts by default.
    bool allow_nan_literals = true;
    // False refuses any number with a fraction or an exponent (the kernel's rule).
    bool allow_floats = true;
    // Longest integer token, counted without its sign; 0 is unbounded. The kernel's is 16.
    std::size_t max_integer_digits = 0;
    // Nesting beyond this is refused rather than risking the stack. CPython's own limit is
    // its recursion limit, roughly a thousand.
    std::size_t max_depth = 512;
};

// Throws JsonError. The whole input must be one JSON value, optionally surrounded by JSON
// whitespace.
JsonValue parse_json(std::string_view text, const JsonParseOptions& options = {});

struct JsonDumpOptions {
    bool ensure_ascii = true;  // json.dumps default
    bool sort_keys = false;
    // nullopt is json.dumps(indent=None); a value is its indent width.
    std::optional<int> indent;
    // Empty means CPython's default for the indent mode: ", " and ": " on one line, ","
    // and ": " when indenting.
    std::string_view item_separator;
    std::string_view key_separator;
    // False makes a NaN or an infinity a JsonError, as allow_nan=False makes it a ValueError.
    bool allow_nan = true;
};

// Throws JsonError for a non-finite float when allow_nan is off, and for a lone surrogate
// when ensure_ascii is off (the reference's `.encode("utf-8")` fails on the same input).
std::string dump_json(const JsonValue& value, const JsonDumpOptions& options = {});

// The canonical compact form: sorted keys, "," and ":", no NaN. The world handle uses the
// ensure_ascii=False spelling and the kernel the ensure_ascii=True one.
std::string dump_canonical(const JsonValue& value, bool ensure_ascii);

// CPython's repr(float), e.g. "1.0", "-0.0", "1e+16", "0.0001", "nan", "inf".
std::string python_float_repr(double value);

// CPython's `==` between two JSON values: numbers compare by value across bool, int and
// float (True == 1 == 1.0, and an integer equals a float only if exactly equal), NaN is
// never equal to anything, objects compare regardless of member order.
bool python_equal(const JsonValue& a, const JsonValue& b);

// Code-point order of two strings, as CPython's `<` orders str. Byte order of UTF-8, which
// is what this is.
inline bool python_less(std::string_view a, std::string_view b) { return a < b; }

}  // namespace fwg
