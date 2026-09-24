// The test model inference/README.md names: Qwen2.5-0.5B-Instruct, Q4_0, loaded once for every
// test. A missing model fails every test that needs it, with the way to get it; nothing is
// skipped.
#pragma once

#include <cstdlib>
#include <memory>
#include <string>

#include "check.hpp"
#include "inference/runtime.hpp"

namespace test_model {

inline std::string environment(const char* name) {
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) return {};
    std::string out(value);
    std::free(value);
    return out;
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
}

inline std::string path() {
    const std::string from_env = environment("FWGC_TEST_MODEL");
    return from_env.empty() ? std::string(COMMUNICATOR_TEST_MODEL) : from_env;
}

inline inference::Model& model() {
    static std::unique_ptr<inference::Model> loaded = [] {
        try {
            return inference::Model::load(path());
        } catch (const inference::Error& e) {
            check::fail(std::string(e.what()) +
                        ". Download qwen2.5-0.5b-instruct-q4_0.gguf as inference/README.md says, or point FWGC_TEST_MODEL at it");
        }
    }();
    return *loaded;
}

}  // namespace test_model
