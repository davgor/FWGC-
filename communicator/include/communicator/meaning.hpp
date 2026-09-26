// What the stranger's words are about, by meaning. An embedding model (Qwen3-Embedding, through
// inference) turns their words and a few written descriptions of subjects into vectors, and the
// subject closest in meaning is the one they spoke of: "Still cold out?" is the weather, "How are
// you keeping?" the speaker's own day. The written lines then answer on that subject. A model
// only compares; it writes nothing a speaker says.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace inference {
class Model;
}

namespace communicator {

class Meaning {
public:
    // `model` is loaded with inference::ModelOptions::embeddings and outlives this.
    explicit Meaning(inference::Model& model);

    // How close in meaning each of `texts` is to `said` (the stranger's words), from -1 to 1, in
    // their order. A text's vector is kept for the next time, up to a few thousand texts. Throws
    // inference::Error for a model loaded for text, or empty text. Not for two threads at once.
    std::vector<float> closeness(std::string_view said, const std::vector<std::string>& texts);

    // Which of `subjects` (descriptions of what talk can be about) `said` is about: the index of
    // the closest. Throws as closeness() does, and std::invalid_argument for no subjects.
    std::size_t subject_of(std::string_view said, const std::vector<std::string>& subjects);

private:
    inference::Model& model_;
    std::map<std::string, std::vector<float>, std::less<>> texts_;
};

}  // namespace communicator
