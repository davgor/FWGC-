#include "communicator/meaning.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

#include "inference/runtime.hpp"

namespace communicator {

namespace {

// Qwen3-Embedding compares a query put with an instruction against texts put plain (its model
// card's form: "Instruct: <task>\nQuery:<query>"). Measured 2026-09-26 on 14 lines of small talk
// against two descriptions, this instruction read every one's subject right; asking instead for
// "the reply that answers it" drew every question to a reply that thanked the asker.
constexpr std::string_view kQuery = "Instruct: Given a remark in a conversation, retrieve a remark about the same subject\nQuery:";

// More texts than a long session compares; past it, the kept vectors start again.
constexpr std::size_t kKept = 4096;

float dot(const std::vector<float>& a, const std::vector<float>& b) {
    float out = 0.0f;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) out += a[i] * b[i];
    return out;
}

}  // namespace

Meaning::Meaning(inference::Model& model) : model_(model) {}

std::vector<float> Meaning::closeness(std::string_view said, const std::vector<std::string>& texts) {
    const std::vector<float> query = model_.embed(std::string(kQuery) + std::string(said));
    std::vector<float> out;
    out.reserve(texts.size());
    for (const std::string& text : texts) {
        auto found = texts_.find(text);
        if (found == texts_.end()) {
            if (texts_.size() >= kKept) texts_.clear();
            found = texts_.emplace(text, model_.embed(text)).first;
        }
        // Both are of unit length (inference::Model::embed), so their dot product is the cosine.
        out.push_back(dot(query, found->second));
    }
    return out;
}

std::size_t Meaning::subject_of(std::string_view said, const std::vector<std::string>& subjects) {
    if (subjects.empty()) throw std::invalid_argument("no subjects to choose from");
    const std::vector<float> close = closeness(said, subjects);
    return static_cast<std::size_t>(std::max_element(close.begin(), close.end()) - close.begin());
}

}  // namespace communicator
