#include "json.hpp"

#include <cstdio>
#include <cstdlib>

namespace layers::json {

const Value* Value::get(std::string_view key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& [k, v] : members)
        if (k == key) return &v;
    return nullptr;
}

double Value::number() const { return type == Type::Number ? std::strtod(text.c_str(), nullptr) : 0.0; }

namespace {

class Parser {
public:
    explicit Parser(std::string_view text) : s_(text) {}

    Value document() {
        Value v = value(0);
        space();
        if (i_ != s_.size()) fail("trailing characters");
        return v;
    }

private:
    std::string_view s_;
    std::size_t i_ = 0;

    [[noreturn]] void fail(const std::string& what) const {
        throw Error("json: " + what + " at offset " + std::to_string(i_));
    }

    void space() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
    }

    bool literal(std::string_view word) {
        if (s_.substr(i_, word.size()) != word) return false;
        i_ += word.size();
        return true;
    }

    Value value(int depth) {
        if (depth > 256) fail("nesting too deep");
        space();
        if (i_ >= s_.size()) fail("unexpected end");
        const char c = s_[i_];
        Value v;
        if (c == '{') {
            v.type = Value::Type::Object;
            ++i_;
            space();
            if (i_ < s_.size() && s_[i_] == '}') {
                ++i_;
                return v;
            }
            for (;;) {
                space();
                if (i_ >= s_.size() || s_[i_] != '"') fail("expected a member name");
                std::string key = string();
                space();
                if (i_ >= s_.size() || s_[i_] != ':') fail("expected ':'");
                ++i_;
                v.members.emplace_back(std::move(key), value(depth + 1));
                space();
                if (i_ < s_.size() && s_[i_] == ',') {
                    ++i_;
                    continue;
                }
                if (i_ < s_.size() && s_[i_] == '}') {
                    ++i_;
                    return v;
                }
                fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            v.type = Value::Type::Array;
            ++i_;
            space();
            if (i_ < s_.size() && s_[i_] == ']') {
                ++i_;
                return v;
            }
            for (;;) {
                v.items.push_back(value(depth + 1));
                space();
                if (i_ < s_.size() && s_[i_] == ',') {
                    ++i_;
                    continue;
                }
                if (i_ < s_.size() && s_[i_] == ']') {
                    ++i_;
                    return v;
                }
                fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            v.type = Value::Type::String;
            v.text = string();
            return v;
        }
        if (literal("true")) {
            v.type = Value::Type::Bool;
            v.boolean = true;
            return v;
        }
        if (literal("false")) {
            v.type = Value::Type::Bool;
            return v;
        }
        if (literal("null")) return v;
        if (c == '-' || (c >= '0' && c <= '9')) {
            const std::size_t start = i_;
            if (s_[i_] == '-') ++i_;
            auto digits = [&] {
                const std::size_t from = i_;
                while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
                if (i_ == from) fail("expected a digit");
            };
            digits();
            if (i_ < s_.size() && s_[i_] == '.') {
                ++i_;
                digits();
            }
            if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
                ++i_;
                if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) ++i_;
                digits();
            }
            v.type = Value::Type::Number;
            v.text = std::string(s_.substr(start, i_ - start));
            return v;
        }
        fail(std::string("unexpected character '") + c + "'");
    }

    unsigned hex4() {
        if (i_ + 4 > s_.size()) fail("short \\u escape");
        unsigned value = 0;
        for (int k = 0; k < 4; ++k) {
            const char h = s_[i_++];
            value <<= 4;
            if (h >= '0' && h <= '9') value |= static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f') value |= static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') value |= static_cast<unsigned>(h - 'A' + 10);
            else fail("bad \\u escape");
        }
        return value;
    }

    static void utf8(std::string& out, unsigned cp) {
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

    std::string string() {
        ++i_;  // opening quote
        std::string out;
        for (;;) {
            if (i_ >= s_.size()) fail("unterminated string");
            const char c = s_[i_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (i_ >= s_.size()) fail("unterminated escape");
            const char e = s_[i_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned cp = hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (s_.substr(i_, 2) != "\\u") fail("lone high surrogate");
                        i_ += 2;
                        const unsigned low = hex4();
                        if (low < 0xDC00 || low > 0xDFFF) fail("bad low surrogate");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        fail("lone low surrogate");
                    }
                    utf8(out, cp);
                    break;
                }
                default: fail(std::string("bad escape '\\") + e + "'");
            }
        }
    }
};

void dump_to(std::string& out, const Value& v) {
    switch (v.type) {
        case Value::Type::Null: out += "null"; break;
        case Value::Type::Bool: out += v.boolean ? "true" : "false"; break;
        case Value::Type::Number: out += v.text; break;
        case Value::Type::String: out += quote(v.text); break;
        case Value::Type::Array:
            out.push_back('[');
            for (std::size_t k = 0; k < v.items.size(); ++k) {
                if (k) out.push_back(',');
                dump_to(out, v.items[k]);
            }
            out.push_back(']');
            break;
        case Value::Type::Object:
            out.push_back('{');
            for (std::size_t k = 0; k < v.members.size(); ++k) {
                if (k) out.push_back(',');
                out += quote(v.members[k].first);
                out.push_back(':');
                dump_to(out, v.members[k].second);
            }
            out.push_back('}');
            break;
    }
}

}  // namespace

Value parse(std::string_view text) { return Parser(text).document(); }

std::string dump(const Value& value) {
    std::string out;
    dump_to(out, value);
    return out;
}

std::string quote(std::string_view s) {
    std::string out = "\"";
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof buffer, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buffer;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
    return out;
}

Value make_string(std::string s) {
    Value v;
    v.type = Value::Type::String;
    v.text = std::move(s);
    return v;
}

Value make_number(double n) {
    Value v;
    v.type = Value::Type::Number;
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.6g", n);
    v.text = buffer;
    return v;
}

Value make_bool(bool b) {
    Value v;
    v.type = Value::Type::Bool;
    v.boolean = b;
    return v;
}

Value make_object() {
    Value v;
    v.type = Value::Type::Object;
    return v;
}

Value make_array() {
    Value v;
    v.type = Value::Type::Array;
    return v;
}

void set(Value& object, std::string key, Value v) {
    for (auto& [k, existing] : object.members)
        if (k == key) {
            existing = std::move(v);
            return;
        }
    object.members.emplace_back(std::move(key), std::move(v));
}

}  // namespace layers::json
