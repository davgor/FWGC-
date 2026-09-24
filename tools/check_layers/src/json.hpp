// A small JSON reader and writer for the commit gate: the reviewer's verdict, its schema and
// the gate's own cache. Strict RFC 8259; numbers keep their source text.
#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace layers::json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    std::string text;  // a string's value, or a number's source text
    std::vector<Value> items;
    std::vector<std::pair<std::string, Value>> members;

    const Value* get(std::string_view key) const;  // nullptr when absent or not an object
    bool is(Type t) const { return type == t; }
    double number() const;                          // 0 unless a number
};

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Value parse(std::string_view text);  // throws Error naming the offset
std::string dump(const Value& value);  // compact
std::string quote(std::string_view s);  // a JSON string literal

Value make_string(std::string s);
Value make_number(double n);
Value make_bool(bool b);
Value make_object();
Value make_array();
void set(Value& object, std::string key, Value v);

}  // namespace layers::json
