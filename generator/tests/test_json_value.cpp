// The JSON layer the product contracts are defined in: CPython's json.dumps and json.loads,
// byte for byte where a hash or a canonical form depends on it.
//
// The pinned strings below are CPython's own output for the same values. A scratch sweep
// of 1.83 million doubles and 24,000 random documents found no disagreement; these are the
// cases a port gets wrong, named so a regression says which convention it broke.
#include "check.hpp"

#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "fwg/json_value.hpp"

using namespace fwg;

namespace {

std::string repr(double v) { return python_float_repr(v); }

std::string canonical(std::string_view text, bool ascii) { return dump_canonical(parse_json(text), ascii); }

}  // namespace

TEST(json_float_repr_follows_cpython_thresholds_and_spelling) {
    // The four conventions on top of shortest digits: a positional float always has a
    // fractional part, negative zero keeps its sign, scientific from decpt > 16 and
    // decpt <= -4, and the exponent is signed with at least two digits.
    check::equal(repr(1.0), std::string("1.0"), "whole");
    check::equal(repr(-0.0), std::string("-0.0"), "negative zero");
    check::equal(repr(0.0), std::string("0.0"), "zero");
    check::equal(repr(0.30000000000000004), std::string("0.30000000000000004"), "shortest digits");
    check::equal(repr(1e15), std::string("1000000000000000.0"), "below the high threshold");
    check::equal(repr(1e16), std::string("1e+16"), "at the high threshold");
    check::equal(repr(9999999999999998.0), std::string("9999999999999998.0"), "sixteen digits positional");
    check::equal(repr(1e-4), std::string("0.0001"), "above the low threshold");
    check::equal(repr(1e-5), std::string("1e-05"), "at the low threshold");
    check::equal(repr(1.5e-7), std::string("1.5e-07"), "two-digit exponent");
    check::equal(repr(1e100), std::string("1e+100"), "three-digit exponent");
    check::equal(repr(5e-324), std::string("5e-324"), "smallest subnormal");
    check::equal(repr(1.7976931348623157e308), std::string("1.7976931348623157e+308"), "largest double");
    check::equal(repr(123456789012345680.0), std::string("1.2345678901234568e+17"), "no positional padding");
    check::equal(repr(-2.5), std::string("-2.5"), "negative");
    check::equal(repr(std::numeric_limits<double>::infinity()), std::string("inf"), "inf");
}

TEST(json_dumps_matches_cpython_for_the_fixture_float_case) {
    // Fixtures/world-handle-v1.json's "floats" case, the bytes a second host must produce.
    const std::string text =
        R"({"whole": 1.0, "negative zero": -0.0, "repr": 0.30000000000000004, "below the high threshold": 1000000000000000.0, "at the high threshold": 1e+16, "above the low threshold": 0.0001, "at the low threshold": 1e-05})";
    check::equal(canonical(text, false),
                 std::string(R"({"above the low threshold":0.0001,"at the high threshold":1e+16,"at the low threshold":1e-05,"below the high threshold":1000000000000000.0,"negative zero":-0.0,"repr":0.30000000000000004,"whole":1.0})"),
                 "canonical floats");
}

TEST(json_integers_and_floats_stay_distinct) {
    check::equal(canonical("[1, 1.0, -0, 1E2, 2e+05]", true), std::string("[1,1.0,0,100.0,200000.0]"), "int/float spelling");
    // Integers of any length are kept exactly, as Python ints are.
    check::equal(canonical("123456789012345678901234567890", true), std::string("123456789012345678901234567890"), "big int");
    check::equal(canonical("-9223372036854775809", true), std::string("-9223372036854775809"), "below int64");
    check::is_true(parse_json("9223372036854775807").is_int64(), "int64 max stays int64");
    check::is_true(parse_json("9223372036854775808").is_big_integer(), "one past int64 max is big");
}

TEST(json_escapes_are_cpythons) {
    // ensure_ascii: named escapes for \b \f \n \r \t, \u00XX for other controls, DEL escaped,
    // non-ASCII as \uXXXX and surrogate pairs above the BMP.
    const std::string text = "\"\\b\\f\\n\\r\\t\\u0000\\u001f\\u007f/\\u00e9\\u2028\\ud83d\\ude00\\\\\\\"\"";
    check::equal(canonical(text, true), std::string("\"\\b\\f\\n\\r\\t\\u0000\\u001f\\u007f/\\u00e9\\u2028\\ud83d\\ude00\\\\\\\"\""),
                 "ensure_ascii");
    // Without ensure_ascii: raw UTF-8 and a raw DEL; controls still escaped.
    check::equal(canonical(text, false),
                 std::string("\"\\b\\f\\n\\r\\t\\u0000\\u001f\x7f/\xc3\xa9\xe2\x80\xa8\xf0\x9f\x98\x80\\\\\\\"\""),
                 "utf-8");
}

TEST(json_sort_keys_is_code_point_order_not_utf16_order) {
    // U+E000 sorts before U+10000 by code point; UTF-16 order would put the pair first. The
    // kernel's numeric fixture pins exactly this.
    check::equal(canonical(R"({"\ud800\udc00": 2, "\ue000": 1, "b": 0, "B": 0})", true),
                 std::string(R"({"B":0,"b":0,"\ue000":1,"\ud800\udc00":2})"), "order");
}

TEST(json_indent_layout_is_cpythons) {
    JsonDumpOptions options;
    options.ensure_ascii = false;
    options.indent = 2;
    check::equal(dump_json(parse_json(R"({"a": [], "b": {}, "c": [1, {"d": null}]})"), options),
                 std::string("{\n  \"a\": [],\n  \"b\": {},\n  \"c\": [\n    1,\n    {\n      \"d\": null\n    }\n  ]\n}"),
                 "indent 2");
    options.indent = 0;
    check::equal(dump_json(parse_json("[1,2]"), options), std::string("[\n1,\n2\n]"), "indent 0 still breaks lines");
    JsonDumpOptions one_line;
    check::equal(dump_json(parse_json(R"({"a":[1,2]})"), one_line), std::string(R"({"a": [1, 2]})"), "default separators");
}

TEST(json_nan_is_written_or_refused_as_cpython_does) {
    const JsonValue doc = parse_json(R"({"a": [NaN, Infinity, -Infinity]})");
    check::equal(dump_json(doc), std::string(R"({"a": [NaN, Infinity, -Infinity]})"), "allow_nan default");
    try {
        dump_canonical(doc, false);
        check::fail("NaN was written in canonical form");
    } catch (const JsonError& error) {
        check::equal(error.where(), std::string("a[0]"), "the refusal names the path");
    }
}

TEST(json_reader_keeps_the_last_value_of_a_repeated_key_at_its_first_position) {
    const JsonValue doc = parse_json(R"({"b": 1, "a": 2, "b": 3})");
    check::equal(dump_json(doc), std::string(R"({"b": 3, "a": 2})"), "dict semantics");
    JsonParseOptions strict;
    strict.reject_duplicate_keys = true;
    check::throws([&] { parse_json(R"({"b": 1, "b": 1})", strict); }, "duplicate refused when asked");
}

TEST(json_reader_is_strict_where_cpython_is) {
    for (const char* bad : {"", "01", "1.", ".5", "+1", "[1,]", "{\"a\":1,}", "{'a':1}", "[1 2]", "nul", "\"\\x41\"",
                            "\"\x01\"", "1 2", "\xef\xbb\xbf{}", "\"\\u12\"", "-", "1e", "\"abc", "[1]//", "\"\xff\"",
                            "\"\xed\xa0\x80\""}) {
        check::throws([&] { parse_json(bad); }, std::string("refuses ") + bad);
    }
    // Out-of-range exponents read as float() reads them.
    check::is_true(std::isinf(parse_json("1e999").as_float()), "overflow is an infinity");
    check::is_true(parse_json("-1e-400").as_float() == 0.0 && std::signbit(parse_json("-1e-400").as_float()),
                   "underflow is a signed zero");
}

TEST(json_lone_surrogates_survive_a_read_and_refuse_utf8) {
    const JsonValue lone = parse_json("\"\\ud800\"");
    check::equal(dump_json(lone), std::string("\"\\ud800\""), "escaped back out with ensure_ascii");
    check::throws([&] { dump_canonical(lone, false); }, "no UTF-8 spelling, as .encode('utf-8') fails");
    check::equal(dump_json(parse_json("\"\\ud83d\\ude00\"")), std::string("\"\\ud83d\\ude00\""), "a pair joins");
}

TEST(json_python_equal_compares_as_cpython_does) {
    // Both argument orders: == is symmetric, and a port can get one order right only.
    for (const auto& [a, b] : std::vector<std::pair<const char*, const char*>>{
             {"1", "1.0"}, {"true", "1"}, {"true", "1.0"}, {"0", "-0.0"}, {"false", "0.0"}, {"-3", "-3.0"},
             {"123456789012345678901234567890", "1.2345678901234568e+29"}}) {
        const bool exact = std::string(a) != "123456789012345678901234567890";
        check::equal(python_equal(parse_json(a), parse_json(b)), exact, std::string(a) + " == " + b);
        check::equal(python_equal(parse_json(b), parse_json(a)), exact, std::string(b) + " == " + a);
    }
    check::is_true(!python_equal(parse_json("NaN"), parse_json("NaN")), "NaN != NaN");
    check::is_true(!python_equal(parse_json("9007199254740993"), parse_json("9007199254740992.0")),
                   "an int equals a float only exactly");
    check::is_true(!python_equal(parse_json("9007199254740992.0"), parse_json("9007199254740993")),
                   "in either order");
    check::is_true(python_equal(parse_json("1e20"), parse_json("100000000000000000000")), "a float equals a big int exactly");
    check::is_true(python_equal(parse_json(R"({"a":1,"b":[1,2]})"), parse_json(R"({"b":[1,2.0],"a":true})")),
                   "objects ignore member order");
    check::is_true(!python_equal(parse_json("[1,2]"), parse_json("[2,1]")), "arrays do not");
    check::is_true(!python_equal(parse_json("\"1\""), parse_json("1")), "a string is not a number");
    check::is_true(!(parse_json("1") == parse_json("1.0")), "structural == keeps them apart");
}
