// The inference test harness, kept here rather than shared, because nothing sits between two
// components to share it from.
#pragma once

#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace check {

struct Case {
    std::string name;
    std::function<void()> body;
};

std::vector<Case>& registry();

struct Registrar {
    Registrar(const char* name, std::function<void()> body) { registry().push_back({name, std::move(body)}); }
};

struct Failure {
    std::string message;
};

[[noreturn]] inline void fail(const std::string& message) { throw Failure{message}; }

inline void is_true(bool condition, const std::string& what) {
    if (!condition) fail("expected " + what);
}

template <typename A, typename B>
void equal(const A& actual, const B& expected, const std::string& what) {
    if (!(actual == expected)) {
        std::ostringstream message;
        message << what << ": expected " << expected << ", got " << actual;
        fail(message.str());
    }
}

template <typename Fn>
void throws(Fn&& fn, const std::string& what) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    fail(what + ": expected an exception, none thrown");
}

}  // namespace check

#define TEST(name)                                          \
    static void name();                                      \
    static ::check::Registrar registrar_##name(#name, name); \
    static void name()
