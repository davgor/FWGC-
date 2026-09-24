// The local model runtime: one model file in memory, text in, text out, run on this machine.
//
// It knows nothing of what the text is about. A caller hands it a prompt, a sampling policy and
// optionally a grammar, and gets text back, piece by piece if it asks. Nothing here opens a
// network connection: the model is a local file, loaded memory-mapped.
//
// llama.cpp does the work. This header does not show it, so a caller depends on this contract
// and not on llama.cpp's API, which changes between releases.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace inference {

// A caller's mistake, or a model that will not load or run. The message says which.
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct ModelOptions {
    int context_tokens = 4096;  // the context window: the prompt and the reply together
    int threads = 4;            // CPU threads; for a fixed count, greedy output is reproducible
    int gpu_layers = 0;         // layers offloaded to a GPU; this build has the CPU backend only
};

// One turn of a conversation, for the model's own chat template: "system", "user" or
// "assistant".
struct Message {
    std::string role;
    std::string content;
};

struct Sampling {
    int max_tokens = 256;
    float temperature = 0.0f;  // 0: greedy, the same text every time
    std::uint32_t seed = 0;    // used when temperature > 0; the same seed gives the same text
    // A GBNF grammar the output must belong to (llama.cpp's grammars/README.md); empty for free
    // text. With a grammar, the model can produce nothing the grammar does not allow.
    std::string grammar;
    std::string grammar_root = "root";
};

struct Completion {
    std::string text;
    int tokens = 0;
    bool stopped_at_end = false;     // the model ended it, rather than the token limit
    bool stopped_by_caller = false;  // on_piece returned false
};

// One model and one context. Not thread-safe: a caller serializes its calls or loads more
// than one Model.
class Model {
public:
    // Loads a GGUF file. Throws Error naming the path, or the option out of range.
    static std::unique_ptr<Model> load(const std::string& path, const ModelOptions& options = {});

    ~Model();
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    // The model's own chat template applied to `messages`, ending where its reply begins.
    // Throws Error for a model without a template.
    std::string chat_prompt(const std::vector<Message>& messages) const;

    // Continues `prompt`. Each piece of text is handed to `on_piece` as it is produced, never
    // splitting a UTF-8 character; returning false stops. The context is cleared first, so one
    // call is one reply and nothing carries over between calls. Throws Error for a prompt that
    // does not fit the context with max_tokens after it, a grammar that does not parse, or
    // sampling settings out of range.
    Completion complete(const std::string& prompt, const Sampling& sampling,
                        const std::function<bool(std::string_view)>& on_piece = {});

    // How many tokens `text` is, for budgeting a prompt against the context.
    int count_tokens(const std::string& text) const;

    int context_tokens() const;

private:
    Model();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace inference
