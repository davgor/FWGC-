#include "fwg/json_value.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

namespace fwg {
namespace {

[[noreturn]] void wrong_type(const JsonValue& value, std::string_view wanted) {
    throw std::logic_error("JSON value is " + std::string(value.type_name()) + ", not " + std::string(wanted));
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

// Canonical decimal digits of an integer: no leading zeros, no "-0". Throws for anything
// that is not an optionally signed run of decimal digits.
std::string canonical_digits(std::string_view text) {
    bool negative = false;
    if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    if (text.empty() || !std::all_of(text.begin(), text.end(), is_digit)) {
        throw std::invalid_argument("an integer is an optionally signed run of decimal digits");
    }
    const std::size_t first = text.find_first_not_of('0');
    if (first == std::string_view::npos) return "0";
    return (negative ? "-" : "") + std::string(text.substr(first));
}

// ---------------------------------------------------------------------------------------
// UTF-8. Generalized ("WTF-8") on the way in from an escape, so a lone surrogate survives a
// read as CPython keeps it in a str; strict everywhere else.

void append_code_point(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Decodes one code point starting at `i`, accepting the generalized encoding of a
// surrogate (it can only be there because an escape put it there). Returns the byte count,
// or 0 for bytes no encoder here produces.
std::size_t decode_stored(std::string_view s, std::size_t i, std::uint32_t& cp) {
    const auto b = [&](std::size_t k) { return static_cast<unsigned char>(s[i + k]); };
    const unsigned char lead = b(0);
    if (lead < 0x80) {
        cp = lead;
        return 1;
    }
    std::size_t length = 0;
    if (lead >= 0xC2 && lead <= 0xDF) {
        length = 2;
        cp = lead & 0x1Fu;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        length = 3;
        cp = lead & 0x0Fu;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4;
        cp = lead & 0x07u;
    } else {
        return 0;
    }
    if (i + length > s.size()) return 0;
    for (std::size_t k = 1; k < length; ++k) {
        if ((b(k) & 0xC0u) != 0x80u) return 0;
        cp = (cp << 6) | (b(k) & 0x3Fu);
    }
    if ((length == 3 && cp < 0x800) || (length == 4 && (cp < 0x10000 || cp > 0x10FFFF))) return 0;
    return length;
}

// ---------------------------------------------------------------------------------------
// The reader.

class Parser {
public:
    Parser(std::string_view text, const JsonParseOptions& options) : s_(text), options_(options) {}

    JsonValue document() {
        if (s_.size() >= 3 && static_cast<unsigned char>(s_[0]) == 0xEF && static_cast<unsigned char>(s_[1]) == 0xBB &&
            static_cast<unsigned char>(s_[2]) == 0xBF) {
            fail("Unexpected UTF-8 BOM");
        }
        whitespace();
        JsonValue result = value();
        whitespace();
        if (i_ != s_.size()) fail("Extra data");
        return result;
    }

private:
    [[noreturn]] void fail(const std::string& what) const {
        throw JsonError(what + " at byte " + std::to_string(i_), std::to_string(i_));
    }

    bool at_end() const { return i_ >= s_.size(); }
    char peek() const { return s_[i_]; }

    void whitespace() {
        while (!at_end()) {
            const char c = peek();
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            ++i_;
        }
    }

    bool consume_literal(std::string_view literal) {
        if (s_.substr(i_, literal.size()) != literal) return false;
        i_ += literal.size();
        return true;
    }

    JsonValue value() {
        if (at_end()) fail("Expecting value");
        switch (peek()) {
            case '{': return object();
            case '[': return array();
            case '"': return JsonValue(string());
            case 'n':
                if (consume_literal("null")) return JsonValue();
                break;
            case 't':
                if (consume_literal("true")) return JsonValue(true);
                break;
            case 'f':
                if (consume_literal("false")) return JsonValue(false);
                break;
            case 'N':
                if (s_.substr(i_, 3) == "NaN") return constant(3, std::numeric_limits<double>::quiet_NaN());
                break;
            case 'I':
                if (s_.substr(i_, 8) == "Infinity") return constant(8, std::numeric_limits<double>::infinity());
                break;
            case '-':
                if (s_.substr(i_, 9) == "-Infinity") return constant(9, -std::numeric_limits<double>::infinity());
                return number();
            default:
                if (is_digit(peek())) return number();
                break;
        }
        fail("Expecting value");
    }

    JsonValue constant(std::size_t length, double v) {
        if (!options_.allow_nan_literals) fail("NaN and Infinity are not accepted here");
        i_ += length;
        return JsonValue(v);
    }

    struct DepthGuard {
        explicit DepthGuard(Parser& p) : parser(p) {
            if (++parser.depth_ > parser.options_.max_depth) parser.fail("nesting exceeds the depth limit");
        }
        ~DepthGuard() { --parser.depth_; }
        Parser& parser;
    };

    JsonValue object() {
        DepthGuard guard(*this);
        ++i_;  // '{'
        JsonValue::Object members;
        // Built only once an object is large enough for a linear search to matter.
        std::unordered_map<std::string, std::size_t> index;
        whitespace();
        if (!at_end() && peek() == '}') {
            ++i_;
            return JsonValue(std::move(members));
        }
        while (true) {
            whitespace();
            if (at_end() || peek() != '"') fail("Expecting property name enclosed in double quotes");
            std::string key = string();
            whitespace();
            if (at_end() || peek() != ':') fail("Expecting ':' delimiter");
            ++i_;
            whitespace();
            JsonValue item = value();

            std::size_t existing = members.size();
            if (!index.empty()) {
                const auto found = index.find(key);
                if (found != index.end()) existing = found->second;
            } else {
                for (std::size_t m = 0; m < members.size(); ++m) {
                    if (members[m].first == key) {
                        existing = m;
                        break;
                    }
                }
            }
            if (existing != members.size()) {
                if (options_.reject_duplicate_keys) fail("duplicate JSON key");
                members[existing].second = std::move(item);  // dict semantics: first position, last value
            } else {
                if (!index.empty()) index.emplace(key, members.size());
                members.emplace_back(std::move(key), std::move(item));
                if (index.empty() && members.size() > 16) {
                    for (std::size_t m = 0; m < members.size(); ++m) index.emplace(members[m].first, m);
                }
            }

            whitespace();
            if (at_end()) fail("Expecting ',' delimiter");
            if (peek() == '}') {
                ++i_;
                return JsonValue(std::move(members));
            }
            if (peek() != ',') fail("Expecting ',' delimiter");
            ++i_;
        }
    }

    JsonValue array() {
        DepthGuard guard(*this);
        ++i_;  // '['
        JsonValue::Array items;
        whitespace();
        if (!at_end() && peek() == ']') {
            ++i_;
            return JsonValue(std::move(items));
        }
        while (true) {
            whitespace();
            items.push_back(value());
            whitespace();
            if (at_end()) fail("Expecting ',' delimiter");
            if (peek() == ']') {
                ++i_;
                return JsonValue(std::move(items));
            }
            if (peek() != ',') fail("Expecting ',' delimiter");
            ++i_;
        }
    }

    int hex_digit(char c) const {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    // Four hex digits at i_, or -1 if there are not four.
    long hex4(std::size_t at) const {
        if (at + 4 > s_.size()) return -1;
        long v = 0;
        for (std::size_t k = 0; k < 4; ++k) {
            const int d = hex_digit(s_[at + k]);
            if (d < 0) return -1;
            v = v * 16 + d;
        }
        return v;
    }

    std::string string() {
        ++i_;  // opening quote
        std::string out;
        while (true) {
            if (at_end()) fail("Unterminated string");
            const auto c = static_cast<unsigned char>(peek());
            if (c == '"') {
                ++i_;
                return out;
            }
            if (c < 0x20) fail("Invalid control character");
            if (c == '\\') {
                if (i_ + 1 >= s_.size()) fail("Unterminated string");
                const char e = s_[i_ + 1];
                switch (e) {
                    case '"': out.push_back('"'); i_ += 2; continue;
                    case '\\': out.push_back('\\'); i_ += 2; continue;
                    case '/': out.push_back('/'); i_ += 2; continue;
                    case 'b': out.push_back('\b'); i_ += 2; continue;
                    case 'f': out.push_back('\f'); i_ += 2; continue;
                    case 'n': out.push_back('\n'); i_ += 2; continue;
                    case 'r': out.push_back('\r'); i_ += 2; continue;
                    case 't': out.push_back('\t'); i_ += 2; continue;
                    case 'u': {
                        const long first = hex4(i_ + 2);
                        if (first < 0) fail("Invalid \\uXXXX escape");
                        i_ += 6;
                        std::uint32_t cp = static_cast<std::uint32_t>(first);
                        // A high surrogate followed by an escaped low one is one code point;
                        // anything else leaves the surrogate alone, as CPython does.
                        if (cp >= 0xD800 && cp <= 0xDBFF && i_ + 1 < s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                            const long second = hex4(i_ + 2);
                            if (second >= 0xDC00 && second <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<std::uint32_t>(second) - 0xDC00);
                                i_ += 6;
                            }
                        }
                        append_code_point(out, cp);
                        continue;
                    }
                    default:
                        fail("Invalid \\escape");
                }
            }
            if (c < 0x80) {
                out.push_back(static_cast<char>(c));
                ++i_;
                continue;
            }
            // Raw non-ASCII: strict UTF-8, surrogates refused (CPython decodes the whole
            // input with the strict codec before it parses a byte of it).
            std::uint32_t cp = 0;
            const std::size_t length = decode_stored(s_, i_, cp);
            if (length == 0 || (cp >= 0xD800 && cp <= 0xDFFF)) fail("invalid UTF-8");
            out.append(s_.substr(i_, length));
            i_ += length;
        }
    }

    JsonValue number() {
        const std::size_t start = i_;
        if (peek() == '-') ++i_;
        if (at_end() || !is_digit(peek())) fail("Expecting value");
        if (peek() == '0') {
            ++i_;
        } else {
            while (!at_end() && is_digit(peek())) ++i_;
        }
        const std::size_t integer_end = i_;
        bool fractional = false;
        if (!at_end() && peek() == '.' && i_ + 1 < s_.size() && is_digit(s_[i_ + 1])) {
            i_ += 1;
            while (!at_end() && is_digit(peek())) ++i_;
            fractional = true;
        }
        if (!at_end() && (peek() == 'e' || peek() == 'E')) {
            std::size_t j = i_ + 1;
            if (j < s_.size() && (s_[j] == '+' || s_[j] == '-')) ++j;
            if (j < s_.size() && is_digit(s_[j])) {
                while (j < s_.size() && is_digit(s_[j])) ++j;
                i_ = j;
                fractional = true;
            }
        }
        const std::string_view token = s_.substr(start, i_ - start);
        if (!fractional) {
            const std::size_t digits = integer_end - start - (s_[start] == '-' ? 1 : 0);
            if (options_.max_integer_digits != 0 && digits > options_.max_integer_digits) {
                i_ = start;
                fail("integer token exceeds the digit limit");
            }
            std::int64_t v = 0;
            const auto result = std::from_chars(token.data(), token.data() + token.size(), v);
            if (result.ec == std::errc() && result.ptr == token.data() + token.size()) return JsonValue(v);
            return JsonValue(JsonBigInteger{std::string(token)});
        }
        if (!options_.allow_floats) {
            i_ = start;
            fail("floating-point JSON is not accepted here");
        }
        double v = 0.0;
        const auto result = std::from_chars(token.data(), token.data() + token.size(), v);
        if (result.ec == std::errc() && result.ptr == token.data() + token.size()) return JsonValue(v);
        // Out of the double range: float() reads an infinity above it and a signed zero
        // below it, and so does strtod.
        const std::string owned(token);
        errno = 0;
        v = std::strtod(owned.c_str(), nullptr);
        return JsonValue(v);
    }

    std::string_view s_;
    const JsonParseOptions& options_;
    std::size_t i_ = 0;
    std::size_t depth_ = 0;
};

// ---------------------------------------------------------------------------------------
// The writer.

class Writer {
public:
    Writer(const JsonDumpOptions& options, std::string& out) : options_(options), out_(out) {
        const bool indenting = options.indent.has_value();
        item_separator_ = !options.item_separator.empty() ? options.item_separator
                          : indenting                     ? std::string_view(",")
                                                          : std::string_view(", ");
        key_separator_ = !options.key_separator.empty() ? options.key_separator : std::string_view(": ");
        if (indenting) indent_unit_.assign(static_cast<std::size_t>(std::max(0, *options.indent)), ' ');
    }

    void write(const JsonValue& value, int level) {
        switch (value.type()) {
            case JsonValue::Type::Null: out_ += "null"; return;
            case JsonValue::Type::Bool: out_ += value.as_bool() ? "true" : "false"; return;
            case JsonValue::Type::Integer:
                if (value.is_int64()) {
                    out_ += std::to_string(value.as_int());
                } else {
                    out_ += value.as_big_integer().digits;
                }
                return;
            case JsonValue::Type::Float: write_float(value.as_float()); return;
            case JsonValue::Type::String: write_string(value.as_string()); return;
            case JsonValue::Type::Array: write_array(value.as_array(), level); return;
            case JsonValue::Type::Object: write_object(value.as_object(), level); return;
        }
    }

private:
    [[noreturn]] void fail(const std::string& what) const {
        std::string where;
        for (const PathPart& part : path_) {
            if (part.key != nullptr) {
                if (!where.empty()) where.push_back('.');
                where += *part.key;
            } else {
                where += "[" + std::to_string(part.index) + "]";
            }
        }
        throw JsonError(what + (where.empty() ? std::string() : " at " + where), where);
    }

    void write_float(double v) {
        if (std::isnan(v) || std::isinf(v)) {
            if (!options_.allow_nan) fail("Out of range float values are not JSON compliant: " + python_float_repr(v));
            out_ += std::isnan(v) ? "NaN" : (v > 0 ? "Infinity" : "-Infinity");
            return;
        }
        out_ += python_float_repr(v);
    }

    void escape_unit(std::uint32_t unit) {
        static constexpr char kHex[] = "0123456789abcdef";
        out_ += "\\u";
        out_.push_back(kHex[(unit >> 12) & 0xF]);
        out_.push_back(kHex[(unit >> 8) & 0xF]);
        out_.push_back(kHex[(unit >> 4) & 0xF]);
        out_.push_back(kHex[unit & 0xF]);
    }

    void write_string(std::string_view s) {
        out_.push_back('"');
        std::size_t i = 0;
        while (i < s.size()) {
            const auto c = static_cast<unsigned char>(s[i]);
            if (c < 0x80) {
                switch (c) {
                    case '"': out_ += "\\\""; break;
                    case '\\': out_ += "\\\\"; break;
                    case '\b': out_ += "\\b"; break;
                    case '\f': out_ += "\\f"; break;
                    case '\n': out_ += "\\n"; break;
                    case '\r': out_ += "\\r"; break;
                    case '\t': out_ += "\\t"; break;
                    default:
                        if (c < 0x20 || (c == 0x7F && options_.ensure_ascii)) {
                            escape_unit(c);
                        } else {
                            out_.push_back(static_cast<char>(c));
                        }
                }
                ++i;
                continue;
            }
            std::uint32_t cp = 0;
            const std::size_t length = decode_stored(s, i, cp);
            if (length == 0) fail("string is not UTF-8");
            if (options_.ensure_ascii) {
                if (cp >= 0x10000) {
                    const std::uint32_t n = cp - 0x10000;
                    escape_unit(0xD800 | ((n >> 10) & 0x3FF));
                    escape_unit(0xDC00 | (n & 0x3FF));
                } else {
                    escape_unit(cp);
                }
            } else {
                if (cp >= 0xD800 && cp <= 0xDFFF) fail("a lone surrogate has no UTF-8 encoding");
                out_.append(s.substr(i, length));
            }
            i += length;
        }
        out_.push_back('"');
    }

    void newline(int level) {
        out_.push_back('\n');
        for (int k = 0; k < level; ++k) out_ += indent_unit_;
    }

    void write_array(const JsonValue::Array& items, int level) {
        if (items.empty()) {
            out_ += "[]";
            return;
        }
        out_.push_back('[');
        const bool indenting = options_.indent.has_value();
        if (indenting) newline(level + 1);
        for (std::size_t k = 0; k < items.size(); ++k) {
            if (k != 0) {
                out_ += item_separator_;
                if (indenting) newline(level + 1);
            }
            path_.push_back({nullptr, k});
            write(items[k], level + 1);
            path_.pop_back();
        }
        if (indenting) newline(level);
        out_.push_back(']');
    }

    void write_object(const JsonValue::Object& members, int level) {
        if (members.empty()) {
            out_ += "{}";
            return;
        }
        std::vector<const JsonValue::Member*> order;
        order.reserve(members.size());
        for (const JsonValue::Member& m : members) order.push_back(&m);
        if (options_.sort_keys) {
            std::sort(order.begin(), order.end(),
                      [](const JsonValue::Member* a, const JsonValue::Member* b) { return python_less(a->first, b->first); });
        }
        out_.push_back('{');
        const bool indenting = options_.indent.has_value();
        if (indenting) newline(level + 1);
        for (std::size_t k = 0; k < order.size(); ++k) {
            if (k != 0) {
                out_ += item_separator_;
                if (indenting) newline(level + 1);
            }
            write_string(order[k]->first);
            out_ += key_separator_;
            path_.push_back({&order[k]->first, 0});
            write(order[k]->second, level + 1);
            path_.pop_back();
        }
        if (indenting) newline(level);
        out_.push_back('}');
    }

    // Where the writer is, kept cheaply and spelled out only when something fails.
    struct PathPart {
        const std::string* key;  // null for an array index
        std::size_t index;
    };

    const JsonDumpOptions& options_;
    std::string& out_;
    std::string_view item_separator_;
    std::string_view key_separator_;
    std::string indent_unit_;
    std::vector<PathPart> path_;
};

// ---------------------------------------------------------------------------------------
// Exact comparison of a double with an integer, as CPython compares float and int.

// The exact decimal digits of an integral finite double.
std::string integral_digits(double v) {
    std::array<char, 400> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), v, std::chars_format::fixed, 0);
    return canonical_digits(std::string_view(buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data())));
}

bool float_equals_integer(double f, const JsonValue& integer) {
    if (!std::isfinite(f) || f != std::floor(f)) return false;
    if (integer.is_int64()) {
        // Every int64 whose magnitude is below 2^63 is compared exactly by its digits;
        // converting the int to double would round and call 2^53 + 1 equal to 2^53.
        return integral_digits(f) == std::to_string(integer.as_int());
    }
    return integral_digits(f) == integer.as_big_integer().digits;
}

}  // namespace

// ---------------------------------------------------------------------------------------
// JsonValue

JsonValue::JsonValue(JsonBigInteger big) {
    std::string digits = canonical_digits(big.digits);
    std::int64_t v = 0;
    const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), v);
    if (result.ec == std::errc() && result.ptr == digits.data() + digits.size()) {
        data_ = v;
    } else {
        data_ = JsonBigInteger{std::move(digits)};
    }
}

JsonValue::Type JsonValue::type() const noexcept {
    switch (data_.index()) {
        case 0: return Type::Null;
        case 1: return Type::Bool;
        case 2:
        case 3: return Type::Integer;
        case 4: return Type::Float;
        case 5: return Type::String;
        case 6: return Type::Array;
        default: return Type::Object;
    }
}

std::string_view JsonValue::type_name() const noexcept {
    switch (type()) {
        case Type::Null: return "NoneType";
        case Type::Bool: return "bool";
        case Type::Integer: return "int";
        case Type::Float: return "float";
        case Type::String: return "str";
        case Type::Array: return "list";
        case Type::Object: return "dict";
    }
    return "unknown";
}

bool JsonValue::as_bool() const {
    if (!is_bool()) wrong_type(*this, "bool");
    return std::get<bool>(data_);
}

std::int64_t JsonValue::as_int() const {
    if (!is_int64()) wrong_type(*this, "a 64-bit int");
    return std::get<std::int64_t>(data_);
}

const JsonBigInteger& JsonValue::as_big_integer() const {
    if (!is_big_integer()) wrong_type(*this, "a big int");
    return std::get<JsonBigInteger>(data_);
}

double JsonValue::as_float() const {
    if (!is_float()) wrong_type(*this, "float");
    return std::get<double>(data_);
}

const std::string& JsonValue::as_string() const {
    if (!is_string()) wrong_type(*this, "str");
    return std::get<std::string>(data_);
}

const JsonValue::Array& JsonValue::as_array() const {
    if (!is_array()) wrong_type(*this, "list");
    return std::get<Array>(data_);
}

JsonValue::Array& JsonValue::as_array() {
    if (!is_array()) wrong_type(*this, "list");
    return std::get<Array>(data_);
}

const JsonValue::Object& JsonValue::as_object() const {
    if (!is_object()) wrong_type(*this, "dict");
    return std::get<Object>(data_);
}

JsonValue::Object& JsonValue::as_object() {
    if (!is_object()) wrong_type(*this, "dict");
    return std::get<Object>(data_);
}

std::optional<double> JsonValue::to_double() const {
    if (is_float()) return std::get<double>(data_);
    if (is_int64()) return static_cast<double>(std::get<std::int64_t>(data_));  // round-to-nearest-even, as float(int)
    if (is_big_integer()) {
        const std::string& digits = std::get<JsonBigInteger>(data_).digits;
        double v = 0.0;
        const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), v);
        if (result.ec != std::errc() || !std::isfinite(v)) return std::nullopt;  // float() raises OverflowError
        return v;
    }
    return std::nullopt;
}

const JsonValue* JsonValue::find(std::string_view key) const noexcept {
    const Object* members = std::get_if<Object>(&data_);
    if (members == nullptr) return nullptr;
    for (const Member& m : *members) {
        if (m.first == key) return &m.second;
    }
    return nullptr;
}

JsonValue* JsonValue::find(std::string_view key) noexcept {
    Object* members = std::get_if<Object>(&data_);
    if (members == nullptr) return nullptr;
    for (Member& m : *members) {
        if (m.first == key) return &m.second;
    }
    return nullptr;
}

const JsonValue& JsonValue::at(std::string_view key) const {
    if (!is_object()) wrong_type(*this, "dict");
    const JsonValue* found = find(key);
    if (found == nullptr) throw std::out_of_range("JSON object has no key '" + std::string(key) + "'");
    return *found;
}

JsonValue& JsonValue::at(std::string_view key) {
    if (!is_object()) wrong_type(*this, "dict");
    JsonValue* found = find(key);
    if (found == nullptr) throw std::out_of_range("JSON object has no key '" + std::string(key) + "'");
    return *found;
}

JsonValue& JsonValue::set(std::string_view key, JsonValue value) {
    Object& members = as_object();
    for (Member& m : members) {
        if (m.first == key) {
            m.second = std::move(value);
            return m.second;
        }
    }
    members.emplace_back(std::string(key), std::move(value));
    return members.back().second;
}

bool JsonValue::erase(std::string_view key) {
    Object& members = as_object();
    for (auto it = members.begin(); it != members.end(); ++it) {
        if (it->first == key) {
            members.erase(it);
            return true;
        }
    }
    return false;
}

void JsonValue::push_back(JsonValue value) { as_array().push_back(std::move(value)); }

const JsonValue& JsonValue::at(std::size_t index) const {
    const Array& items = as_array();
    if (index >= items.size()) throw std::out_of_range("JSON array index " + std::to_string(index) + " out of range");
    return items[index];
}

JsonValue& JsonValue::at(std::size_t index) {
    Array& items = as_array();
    if (index >= items.size()) throw std::out_of_range("JSON array index " + std::to_string(index) + " out of range");
    return items[index];
}

std::size_t JsonValue::size() const noexcept {
    if (const Array* items = std::get_if<Array>(&data_)) return items->size();
    if (const Object* members = std::get_if<Object>(&data_)) return members->size();
    return 0;
}

// ---------------------------------------------------------------------------------------

JsonError::JsonError(const std::string& message, std::string where)
    : std::invalid_argument(message), where_(std::move(where)) {}

JsonValue parse_json(std::string_view text, const JsonParseOptions& options) {
    return Parser(text, options).document();
}

std::string dump_json(const JsonValue& value, const JsonDumpOptions& options) {
    std::string out;
    Writer(options, out).write(value, 0);
    return out;
}

std::string dump_canonical(const JsonValue& value, bool ensure_ascii) {
    JsonDumpOptions options;
    options.ensure_ascii = ensure_ascii;
    options.sort_keys = true;
    options.item_separator = ",";
    options.key_separator = ":";
    options.allow_nan = false;
    return dump_json(value, options);
}

std::string python_float_repr(double value) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return value > 0 ? "inf" : "-inf";
    if (value == 0.0) return std::signbit(value) ? "-0.0" : "0.0";

    // The shortest round-trip digits, read out of the scientific form so no positional
    // padding can be mistaken for significant digits.
    std::array<char, 64> buffer{};
    const auto result =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::scientific);
    const std::string_view text(buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data()));
    const bool negative = text.front() == '-';
    const std::size_t e = text.find('e');
    std::string digits;
    for (const char c : text.substr(negative ? 1 : 0, e - (negative ? 1 : 0))) {
        if (c != '.') digits.push_back(c);
    }
    int exponent = 0;
    std::from_chars(text.data() + e + 1 + (text[e + 1] == '+' ? 1 : 0), text.data() + text.size(), exponent);

    // CPython's float_repr_style 'short': with the value 0.d1d2... x 10^decpt, positional
    // when -4 < decpt <= 16 and scientific otherwise.
    const int decpt = exponent + 1;
    std::string out = negative ? "-" : "";
    if (decpt <= -4 || decpt > 16) {
        out.push_back(digits[0]);
        if (digits.size() > 1) {
            out.push_back('.');
            out.append(digits, 1, std::string::npos);
        }
        out.push_back('e');
        out.push_back(exponent < 0 ? '-' : '+');
        const std::string magnitude = std::to_string(exponent < 0 ? -exponent : exponent);
        if (magnitude.size() < 2) out.push_back('0');
        out += magnitude;
    } else if (decpt <= 0) {
        out += "0.";
        out.append(static_cast<std::size_t>(-decpt), '0');
        out += digits;
    } else if (static_cast<std::size_t>(decpt) >= digits.size()) {
        out += digits;
        out.append(static_cast<std::size_t>(decpt) - digits.size(), '0');
        out += ".0";
    } else {
        out.append(digits, 0, static_cast<std::size_t>(decpt));
        out.push_back('.');
        out.append(digits, static_cast<std::size_t>(decpt), std::string::npos);
    }
    return out;
}

bool python_equal(const JsonValue& a, const JsonValue& b) {
    const auto numeric = [](const JsonValue& v) { return v.is_bool() || v.is_number(); };
    if (numeric(a) && numeric(b)) {
        // bool is an int subclass: True == 1 == 1.0.
        const JsonValue ia = a.is_bool() ? JsonValue(a.as_bool() ? 1 : 0) : a;
        const JsonValue ib = b.is_bool() ? JsonValue(b.as_bool() ? 1 : 0) : b;
        if (ia.is_float() && ib.is_float()) return ia.as_float() == ib.as_float();
        if (ia.is_float()) return float_equals_integer(ia.as_float(), ib);
        if (ib.is_float()) return float_equals_integer(ib.as_float(), ia);
        if (ia.is_int64() && ib.is_int64()) return ia.as_int() == ib.as_int();
        if (ia.is_big_integer() && ib.is_big_integer()) return ia.as_big_integer() == ib.as_big_integer();
        return false;  // an int64 and a canonical big integer are never equal
    }
    if (a.type() != b.type()) return false;
    switch (a.type()) {
        case JsonValue::Type::Null: return true;
        case JsonValue::Type::String: return a.as_string() == b.as_string();
        case JsonValue::Type::Array: {
            const auto& x = a.as_array();
            const auto& y = b.as_array();
            if (x.size() != y.size()) return false;
            for (std::size_t i = 0; i < x.size(); ++i) {
                if (!python_equal(x[i], y[i])) return false;
            }
            return true;
        }
        case JsonValue::Type::Object: {
            const auto& x = a.as_object();
            if (x.size() != b.as_object().size()) return false;
            for (const JsonValue::Member& m : x) {
                const JsonValue* other = b.find(m.first);
                if (other == nullptr || !python_equal(m.second, *other)) return false;
            }
            return true;
        }
        default: return false;
    }
}

}  // namespace fwg
