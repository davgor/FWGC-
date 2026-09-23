#include "fwg/json.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <ostream>

namespace fwg {

std::string format_double(double v) {
    if (std::isnan(v)) return "null";
    if (std::isinf(v)) return v > 0 ? "1e999" : "-1e999";
    std::array<char, 32> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), v);
    return std::string(buffer.data(), result.ptr);
}

void JsonWriter::separator() {
    if (need_comma_) out_ << ',';
    if (!after_key_) newline_indent();
    need_comma_ = false;
    after_key_ = false;
}

void JsonWriter::newline_indent() {
    if (!pretty_ || depth_ == 0) {
        if (pretty_ && depth_ == 0 && need_comma_) out_ << '\n';
        return;
    }
    out_ << '\n';
    for (int i = 0; i < depth_; ++i) out_ << "  ";
}

void JsonWriter::begin_object() {
    separator();
    out_ << '{';
    ++depth_;
}

void JsonWriter::end_object() {
    --depth_;
    if (need_comma_) newline_indent();
    out_ << '}';
    need_comma_ = true;
}

void JsonWriter::begin_array() {
    separator();
    out_ << '[';
    ++depth_;
}

void JsonWriter::end_array() {
    --depth_;
    if (need_comma_) newline_indent();
    out_ << ']';
    need_comma_ = true;
}

void JsonWriter::key(std::string_view name) {
    // Write the name directly rather than through value(): value() runs separator()
    // itself, so routing the key through it emitted a second newline and indent before
    // every single key in every document.
    separator();
    write_string(name);
    out_ << ':';
    if (pretty_) out_ << ' ';
    need_comma_ = false;
    after_key_ = true;
}

void JsonWriter::value(double v) {
    separator();
    out_ << format_double(v);
    need_comma_ = true;
}

void JsonWriter::value(long long v) {
    separator();
    out_ << v;
    need_comma_ = true;
}

void JsonWriter::value(bool v) {
    separator();
    out_ << (v ? "true" : "false");
    need_comma_ = true;
}

void JsonWriter::value_null() {
    separator();
    out_ << "null";
    need_comma_ = true;
}

void JsonWriter::value(std::string_view v) {
    separator();
    write_string(v);
    need_comma_ = true;
}

void JsonWriter::write_string(std::string_view v) {
    out_ << '"';
    for (const char ch : v) {
        switch (ch) {
            case '"': out_ << "\\\""; break;
            case '\\': out_ << "\\\\"; break;
            case '\n': out_ << "\\n"; break;
            case '\r': out_ << "\\r"; break;
            case '\t': out_ << "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    static constexpr char kDigits[] = "0123456789abcdef";
                    out_ << "\\u00" << kDigits[(static_cast<unsigned char>(ch) >> 4) & 0x0f]
                         << kDigits[static_cast<unsigned char>(ch) & 0x0f];
                } else {
                    out_ << ch;
                }
        }
    }
    out_ << '"';
}

void JsonWriter::value(const Grid& grid) {
    separator();
    const int n = grid.size();
    out_ << '[';
    for (int z = 0; z < n; ++z) {
        if (z) out_ << ',';
        out_ << '[';
        for (int x = 0; x < n; ++x) {
            if (x) out_ << ',';
            out_ << format_double(grid.at(x, z));
        }
        out_ << ']';
    }
    out_ << ']';
    need_comma_ = true;
}

void JsonWriter::value(const std::vector<std::string>& items) {
    begin_array();
    for (const std::string& item : items) value(std::string_view(item));
    end_array();
}

}  // namespace fwg
