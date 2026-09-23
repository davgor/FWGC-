// A test harness small enough to read in one sitting.
//
// No dependency, because the library has none and the test build should not be the
// thing that introduces one.
#pragma once

#include <cmath>
#include <functional>
#include <iostream>
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

inline void close(double actual, double expected, double tolerance, const std::string& what) {
    if (!(std::abs(actual - expected) <= tolerance)) {
        std::ostringstream message;
        message << what << ": expected " << expected << " +/- " << tolerance << ", got " << actual
                << " (off by " << std::abs(actual - expected) << ')';
        fail(message.str());
    }
}

inline void between(double actual, double low, double high, const std::string& what) {
    if (!(actual >= low && actual <= high)) {
        std::ostringstream message;
        message << what << ": expected " << low << ".." << high << ", got " << actual;
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

#define TEST(name)                                                              \
    static void name();                                                          \
    static ::check::Registrar registrar_##name(#name, name);                     \
    static void name()
