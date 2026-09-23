#include "fwg/json_scan.hpp"

#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <system_error>

namespace fwg::json_scan {
namespace {

[[noreturn]] void malformed(const std::string& what, std::size_t at) {
    throw std::invalid_argument("malformed JSON: " + what + " at byte " + std::to_string(at));
}

bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

std::size_t skip(std::string_view text, std::size_t pos) {
    while (pos < text.size() && space(text[pos])) ++pos;
    return pos;
}

// One past the closing quote of the string starting at `pos`.
std::size_t string_end(std::string_view text, std::size_t pos) {
    if (pos >= text.size() || text[pos] != '"') malformed("a string", pos);
    ++pos;
    while (pos < text.size()) {
        const char c = text[pos];
        if (c == '\\') {
            pos += 2;
            continue;
        }
        ++pos;
        if (c == '"') return pos;
    }
    malformed("an unterminated string", pos);
}

// One past the end of the value starting at `pos`.
std::size_t value_end(std::string_view text, std::size_t pos) {
    if (pos >= text.size()) malformed("a value", pos);
    const char c = text[pos];
    if (c == '"') return string_end(text, pos);
    if (c == '{' || c == '[') {
        // Only brackets outside strings count, so the scan steps over every string whole.
        int depth = 0;
        while (pos < text.size()) {
            const char d = text[pos];
            if (d == '"') {
                pos = string_end(text, pos);
                continue;
            }
            if (d == '{' || d == '[') ++depth;
            if (d == '}' || d == ']') {
                if (--depth == 0) return pos + 1;
                if (depth < 0) malformed("an unbalanced bracket", pos);
            }
            ++pos;
        }
        malformed("an unclosed " + std::string(1, c), pos);
    }
    // A scalar runs to the next delimiter.
    const std::size_t start = pos;
    while (pos < text.size() && text[pos] != ',' && text[pos] != '}' && text[pos] != ']' && !space(text[pos])) ++pos;
    if (pos == start) malformed("a value", pos);
    return pos;
}

void append_utf8(std::string& out, std::uint32_t cp) {
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

std::uint32_t hex4(std::string_view text, std::size_t pos) {
    if (pos + 4 > text.size()) malformed("a short \\u escape", pos);
    std::uint32_t value = 0;
    const auto result = std::from_chars(text.data() + pos, text.data() + pos + 4, value, 16);
    if (result.ec != std::errc() || result.ptr != text.data() + pos + 4) malformed("a \\u escape", pos);
    return value;
}

std::string_view trim(std::string_view text) {
    std::size_t first = 0;
    while (first < text.size() && space(text[first])) ++first;
    std::size_t last = text.size();
    while (last > first && space(text[last - 1])) --last;
    return text.substr(first, last - first);
}

}  // namespace

std::vector<Member> object_members(std::string_view text) {
    text = trim(text);
    if (text.empty() || text.front() != '{' || text.back() != '}') malformed("an object", 0);
    std::vector<Member> out;
    std::size_t pos = skip(text, 1);
    if (pos < text.size() && text[pos] == '}') return out;
    while (true) {
        pos = skip(text, pos);
        const std::size_t key_end = string_end(text, pos);
        std::string key = string_value(text.substr(pos, key_end - pos));
        pos = skip(text, key_end);
        if (pos >= text.size() || text[pos] != ':') malformed("a ':'", pos);
        pos = skip(text, pos + 1);
        const std::size_t end = value_end(text, pos);
        out.push_back({std::move(key), text.substr(pos, end - pos)});
        pos = skip(text, end);
        if (pos < text.size() && text[pos] == ',') {
            ++pos;
            continue;
        }
        if (pos == text.size() - 1 && text[pos] == '}') return out;
        malformed("',' or '}'", pos);
    }
}

std::vector<std::string_view> array_items(std::string_view text) {
    text = trim(text);
    if (text.empty() || text.front() != '[' || text.back() != ']') malformed("an array", 0);
    std::vector<std::string_view> out;
    std::size_t pos = skip(text, 1);
    if (pos < text.size() && text[pos] == ']') return out;
    while (true) {
        pos = skip(text, pos);
        const std::size_t end = value_end(text, pos);
        out.push_back(text.substr(pos, end - pos));
        pos = skip(text, end);
        if (pos < text.size() && text[pos] == ',') {
            ++pos;
            continue;
        }
        if (pos == text.size() - 1 && text[pos] == ']') return out;
        malformed("',' or ']'", pos);
    }
}

std::string string_value(std::string_view text) {
    text = trim(text);
    if (text.size() < 2 || text.front() != '"' || text.back() != '"') malformed("a string", 0);
    std::string out;
    out.reserve(text.size() - 2);
    for (std::size_t pos = 1; pos + 1 < text.size(); ++pos) {
        const char c = text[pos];
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        // The escaped character must sit inside the closing quote.
        if (++pos >= text.size() - 1) malformed("an unterminated escape", pos);
        switch (text[pos]) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                std::uint32_t cp = hex4(text, pos + 1);
                pos += 4;
                // A high surrogate is half of a pair; the low half follows as its own escape.
                if (cp >= 0xD800 && cp <= 0xDBFF && pos + 7 < text.size() && text[pos + 1] == '\\' &&
                    text[pos + 2] == 'u') {
                    const std::uint32_t low = hex4(text, pos + 3);
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        pos += 6;
                    }
                }
                append_utf8(out, cp);
                break;
            }
            default: malformed("an unknown escape", pos);
        }
    }
    return out;
}

long long integer_value(std::string_view text) {
    text = trim(text);
    long long value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size()) malformed("an integer", 0);
    return value;
}

bool is_null(std::string_view text) { return trim(text) == "null"; }

std::string quoted(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (const char ch : text) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    static constexpr char kDigits[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(kDigits[(static_cast<unsigned char>(ch) >> 4) & 0x0f]);
                    out.push_back(kDigits[static_cast<unsigned char>(ch) & 0x0f]);
                } else {
                    out.push_back(ch);
                }
        }
    }
    out.push_back('"');
    return out;
}

}  // namespace fwg::json_scan
