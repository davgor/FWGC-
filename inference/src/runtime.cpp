#include "inference/runtime.hpp"

#include <cmath>
#include <filesystem>
#include <mutex>

#include "llama.h"

namespace inference {

namespace {

// llama.cpp logs to stderr by default; a runtime inside a game says nothing unless asked.
void quiet(ggml_log_level, const char*, void*) {}

void start_backend_once() {
    static std::once_flag once;
    std::call_once(once, [] {
        llama_log_set(quiet, nullptr);
        llama_backend_init();
    });
}

std::vector<llama_token> tokens_of(const llama_vocab* vocab, const std::string& text, bool add_special) {
    if (text.empty()) return {};
    const auto length = static_cast<std::int32_t>(text.size());
    // A first pass sized at nothing returns the count needed, negated.
    const std::int32_t needed = llama_tokenize(vocab, text.data(), length, nullptr, 0, add_special, /*parse_special=*/true);
    if (needed == INT32_MIN) throw Error("the text is too long to tokenize");
    std::vector<llama_token> tokens(static_cast<std::size_t>(needed < 0 ? -needed : needed));
    const std::int32_t written =
        llama_tokenize(vocab, text.data(), length, tokens.data(), static_cast<std::int32_t>(tokens.size()), add_special, true);
    if (written < 0) throw Error("the text did not tokenize");
    tokens.resize(static_cast<std::size_t>(written));
    return tokens;
}

std::string piece_of(const llama_vocab* vocab, llama_token token) {
    char small[128];
    const std::int32_t n = llama_token_to_piece(vocab, token, small, sizeof small, 0, /*special=*/false);
    if (n >= 0) return std::string(small, static_cast<std::size_t>(n));
    std::string large(static_cast<std::size_t>(-n), '\0');
    const std::int32_t m = llama_token_to_piece(vocab, token, large.data(), -n, 0, false);
    if (m < 0) throw Error("a token did not turn back into text");
    large.resize(static_cast<std::size_t>(m));
    return large;
}

// How many leading bytes of `text` end on a UTF-8 character boundary. A token can end partway
// through a character; what is left is held until the next piece completes it.
std::size_t complete_utf8(const std::string& text) {
    const std::size_t n = text.size();
    // Look back at most three bytes for the lead byte of the last character.
    for (std::size_t back = 1; back <= 4 && back <= n; ++back) {
        const auto byte = static_cast<unsigned char>(text[n - back]);
        if ((byte & 0xC0) == 0x80) continue;  // a continuation byte
        std::size_t width = 1;
        if ((byte & 0xE0) == 0xC0) width = 2;
        else if ((byte & 0xF0) == 0xE0) width = 3;
        else if ((byte & 0xF8) == 0xF0) width = 4;
        return back >= width ? n : n - back;
    }
    return n;  // only continuation bytes in reach: not UTF-8 we can repair, so pass it on
}

}  // namespace

struct Model::Impl {
    llama_model* model = nullptr;
    llama_context* context = nullptr;
    const llama_vocab* vocab = nullptr;
    ModelOptions options;

    ~Impl() {
        if (context) llama_free(context);
        if (model) llama_model_free(model);
    }
};

Model::Model() : impl_(std::make_unique<Impl>()) {}
Model::~Model() = default;

std::unique_ptr<Model> Model::load(const std::string& path, const ModelOptions& options) {
    if (options.context_tokens < 64) throw Error("context_tokens must be at least 64");
    if (options.threads < 1) throw Error("threads must be at least 1");
    if (options.gpu_layers < 0) throw Error("gpu_layers must be zero or more");
    std::error_code ec;
    const std::filesystem::path file(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
    if (!std::filesystem::is_regular_file(file, ec)) throw Error("no model file at " + path);
    start_backend_once();

    std::unique_ptr<Model> m(new Model());
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = options.gpu_layers;
    model_params.load_mode = LLAMA_LOAD_MODE_MMAP;  // memory-mapped: loads fast, pages in as used
    m->impl_->model = llama_model_load_from_file(path.c_str(), model_params);
    if (!m->impl_->model) throw Error("the model file at " + path + " did not load");

    llama_context_params context_params = llama_context_default_params();
    context_params.n_ctx = static_cast<std::uint32_t>(options.context_tokens);
    context_params.n_batch = static_cast<std::uint32_t>(options.context_tokens);  // a whole prompt in one decode
    context_params.n_threads = options.threads;
    context_params.n_threads_batch = options.threads;
    m->impl_->context = llama_init_from_model(m->impl_->model, context_params);
    if (!m->impl_->context) throw Error("no context could be made for the model at " + path);
    m->impl_->vocab = llama_model_get_vocab(m->impl_->model);
    m->impl_->options = options;
    return m;
}

std::string Model::chat_prompt(const std::vector<Message>& messages) const {
    const char* tmpl = llama_model_chat_template(impl_->model, nullptr);
    if (!tmpl) throw Error("the model carries no chat template");
    std::vector<llama_chat_message> chat;
    std::size_t characters = 0;
    for (const Message& message : messages) {
        chat.push_back({message.role.c_str(), message.content.c_str()});
        characters += message.role.size() + message.content.size();
    }
    std::vector<char> text(characters * 2 + 256);
    std::int32_t n = llama_chat_apply_template(tmpl, chat.data(), chat.size(), /*add_ass=*/true, text.data(),
                                               static_cast<std::int32_t>(text.size()));
    if (n < 0) throw Error("the model's chat template could not be applied");
    if (static_cast<std::size_t>(n) > text.size()) {
        text.resize(static_cast<std::size_t>(n));
        n = llama_chat_apply_template(tmpl, chat.data(), chat.size(), true, text.data(), n);
        if (n < 0) throw Error("the model's chat template could not be applied");
    }
    return std::string(text.data(), static_cast<std::size_t>(n));
}

int Model::count_tokens(const std::string& text) const {
    return static_cast<int>(tokens_of(impl_->vocab, text, /*add_special=*/true).size());
}

int Model::context_tokens() const { return impl_->options.context_tokens; }

Completion Model::complete(const std::string& prompt, const Sampling& sampling,
                           const std::function<bool(std::string_view)>& on_piece) {
    if (sampling.max_tokens < 1) throw Error("max_tokens must be at least 1");
    if (!std::isfinite(sampling.temperature) || sampling.temperature < 0.0f)
        throw Error("temperature must be a finite number, zero or more");
    const std::vector<llama_token> tokens = tokens_of(impl_->vocab, prompt, /*add_special=*/true);
    if (tokens.empty()) throw Error("the prompt is empty");
    const auto room = static_cast<std::size_t>(impl_->options.context_tokens);
    if (tokens.size() + static_cast<std::size_t>(sampling.max_tokens) > room)
        throw Error("the prompt (" + std::to_string(tokens.size()) + " tokens) and up to " +
                    std::to_string(sampling.max_tokens) + " more do not fit the context of " + std::to_string(room) + " tokens");

    struct Owned {
        llama_sampler* sampler = nullptr;
        ~Owned() {
            if (sampler) llama_sampler_free(sampler);
        }
    };
    // The grammar stands apart from the chain that chooses, so that it can be asked about the
    // one token chosen before it is asked about the whole vocabulary.
    Owned grammar;
    if (!sampling.grammar.empty()) {
        grammar.sampler = llama_sampler_init_grammar(impl_->vocab, sampling.grammar.c_str(), sampling.grammar_root.c_str());
        if (!grammar.sampler) throw Error("the grammar did not parse");
    }
    Owned chain{llama_sampler_chain_init(llama_sampler_chain_default_params())};
    if (sampling.temperature <= 0.0f) {
        llama_sampler_chain_add(chain.sampler, llama_sampler_init_greedy());
    } else {
        llama_sampler_chain_add(chain.sampler, llama_sampler_init_temp(sampling.temperature));
        llama_sampler_chain_add(chain.sampler, llama_sampler_init_dist(sampling.seed));
    }

    // Checking every token of the vocabulary against a grammar at every step is what makes
    // held output slow. The token the model would choose anyway is checked first; only when the
    // grammar refuses it does the whole vocabulary go through the grammar, and the choice is made
    // again among what it allows. Greedy output is the same as filtering first: a best token the
    // grammar allows is the best of the allowed ones. Sampled output keeps the filtered
    // distribution, drawing again only after a refusal.
    const std::int32_t vocabulary = llama_vocab_n_tokens(impl_->vocab);
    std::vector<llama_token_data> candidates(static_cast<std::size_t>(vocabulary));
    const auto choose = [&](bool through_grammar) {
        const float* logits = llama_get_logits_ith(impl_->context, -1);
        if (!logits) throw Error("the model gave no scores to choose from");
        for (llama_token id = 0; id < vocabulary; ++id) candidates[static_cast<std::size_t>(id)] = {id, logits[id], 0.0f};
        llama_token_data_array all{candidates.data(), candidates.size(), -1, false};
        if (through_grammar) llama_sampler_apply(grammar.sampler, &all);
        llama_sampler_apply(chain.sampler, &all);
        if (all.selected < 0 || static_cast<std::size_t>(all.selected) >= all.size || !std::isfinite(all.data[all.selected].logit))
            throw Error("the grammar allows nothing to follow what was written");
        return all.data[all.selected].id;
    };
    const auto next_token = [&] {
        llama_token token = choose(false);
        if (grammar.sampler) {
            llama_token_data one{token, 0.0f, 0.0f};
            llama_token_data_array just_one{&one, 1, -1, false};
            llama_sampler_apply(grammar.sampler, &just_one);
            if (!std::isfinite(one.logit)) token = choose(true);
            llama_sampler_accept(grammar.sampler, token);
        }
        llama_sampler_accept(chain.sampler, token);
        return token;
    };

    // One call is one reply: nothing from an earlier call stays in the context.
    llama_memory_clear(llama_get_memory(impl_->context), true);
    std::vector<llama_token> prompt_tokens = tokens;
    if (llama_decode(impl_->context, llama_batch_get_one(prompt_tokens.data(), static_cast<std::int32_t>(prompt_tokens.size()))) != 0)
        throw Error("the prompt did not decode");

    Completion out;
    std::string held;  // bytes of a character still arriving
    for (int i = 0; i < sampling.max_tokens; ++i) {
        llama_token token = next_token();
        if (llama_vocab_is_eog(impl_->vocab, token)) {
            out.stopped_at_end = true;
            break;
        }
        const std::string piece = piece_of(impl_->vocab, token);
        out.text += piece;
        ++out.tokens;
        held += piece;
        const std::size_t ready = complete_utf8(held);
        if (ready > 0) {
            const bool go_on = !on_piece || on_piece(std::string_view(held).substr(0, ready));
            held.erase(0, ready);
            if (!go_on) {
                out.stopped_by_caller = true;
                return out;
            }
        }
        if (llama_decode(impl_->context, llama_batch_get_one(&token, 1)) != 0)
            throw Error("decoding stopped after " + std::to_string(out.tokens) + " tokens");
    }
    if (!held.empty() && on_piece) (void)on_piece(held);
    return out;
}

}  // namespace inference
