// The embedding model inference/README.md names: Qwen3-Embedding-0.6B, Q8_0, loaded once for
// every test that compares meanings. A missing model fails every test that needs it, with the way
// to get it; nothing is skipped.
#pragma once

#include <memory>
#include <string>

#include "check.hpp"
#include "inference/runtime.hpp"
#include "model.hpp"

namespace test_model {

inline std::string embedding_path() {
    const std::string from_env = environment("FWGC_EMBEDDING_MODEL");
    return from_env.empty() ? std::string(COMMUNICATOR_EMBEDDING_MODEL) : from_env;
}

inline inference::Model& embedder() {
    static std::unique_ptr<inference::Model> loaded = [] {
        inference::ModelOptions options;
        options.context_tokens = 512;
        options.embeddings = true;
        try {
            return inference::Model::load(embedding_path(), options);
        } catch (const inference::Error& e) {
            check::fail(std::string(e.what()) +
                        ". Download Qwen3-Embedding-0.6B-Q8_0.gguf as inference/README.md says, or point FWGC_EMBEDDING_MODEL at it");
        }
    }();
    return *loaded;
}

}  // namespace test_model
