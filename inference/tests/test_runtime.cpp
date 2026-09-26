// The runtime against a real model: Qwen2.5-0.5B-Instruct, Q4_0 (README.md, "The test model").
// A missing model fails every test that needs it, with the way to get it; nothing is skipped.
#include "inference/runtime.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "check.hpp"

namespace {

std::string environment(const char* name) {
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

std::string model_path() {
    const std::string from_env = environment("FWGC_TEST_MODEL");
    return from_env.empty() ? std::string(INFERENCE_TEST_MODEL) : from_env;
}

inference::Model& model() {
    static std::unique_ptr<inference::Model> loaded = [] {
        try {
            return inference::Model::load(model_path());
        } catch (const inference::Error& e) {
            check::fail(std::string(e.what()) +
                        ". Download qwen2.5-0.5b-instruct-q4_0.gguf as README.md says, or point FWGC_TEST_MODEL at it");
        }
    }();
    return *loaded;
}

std::string embedding_model_path() {
    const std::string from_env = environment("FWGC_EMBEDDING_MODEL");
    return from_env.empty() ? std::string(INFERENCE_EMBEDDING_MODEL) : from_env;
}

inference::Model& embedder() {
    static std::unique_ptr<inference::Model> loaded = [] {
        inference::ModelOptions options;
        options.context_tokens = 512;
        options.embeddings = true;
        try {
            return inference::Model::load(embedding_model_path(), options);
        } catch (const inference::Error& e) {
            check::fail(std::string(e.what()) +
                        ". Download Qwen3-Embedding-0.6B-Q8_0.gguf as README.md says, or point FWGC_EMBEDDING_MODEL at it");
        }
    }();
    return *loaded;
}

float dot(const std::vector<float>& a, const std::vector<float>& b) {
    float out = 0.0f;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) out += a[i] * b[i];
    return out;
}

std::string ask(const std::string& question) {
    return model().chat_prompt({{"system", "You answer briefly."}, {"user", question}});
}

// Whether `text` is whole, valid UTF-8.
bool valid_utf8(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::size_t width = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 0;
        if (width == 0 || i + width > text.size()) return false;
        for (std::size_t k = 1; k < width; ++k)
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) return false;
        i += width;
    }
    return true;
}

}  // namespace

TEST(the_program_links_no_gpu_api_of_its_own) {
    // Keep this test first: it must run before any model loads. The Vulkan loader may come in
    // with the Vulkan backend, loaded at run time. Linked into the program itself, it made a
    // machine without Vulkan unable to start the program at all (a build that linked the
    // backend in, 2026-09-24).
#ifdef _WIN32
    check::is_true(GetModuleHandleW(L"vulkan-1.dll") == nullptr, "no vulkan-1.dll in the process before any backend loads");
#endif
}

TEST(a_backend_the_runtime_does_not_ship_is_unloaded) {
    // Keep this test before any model loads too: the backends load once, with the first. ggml
    // loads a backend GGML_BACKEND_PATH names, and any it finds beside the runtime, and its RPC
    // backend talks over sockets. Only the CPU and Vulkan backends stay.
#ifdef INFERENCE_TEST_FOREIGN_BACKEND
    _putenv_s("GGML_BACKEND_PATH", INFERENCE_TEST_FOREIGN_BACKEND);
    const std::vector<std::string> loaded = inference::backends();
    _putenv_s("GGML_BACKEND_PATH", "");
    char mark[8] = {};
    check::is_true(GetEnvironmentVariableA("INFERENCE_FOREIGN_BACKEND_LOADED", mark, sizeof mark) > 0,
                   "the premise: ggml loaded the foreign backend it was offered");
    check::is_true(std::find(loaded.begin(), loaded.end(), std::string("CPU")) != loaded.end(), "the CPU backend stays");
    check::is_true(std::find(loaded.begin(), loaded.end(), std::string("Foreign")) == loaded.end(), "the foreign backend is gone");
#else
    // A build for the CPU alone loads no backend at run time: the CPU is all there is.
    check::is_true(inference::backends() == std::vector<std::string>{"CPU"}, "only the CPU");
#endif
}

TEST(a_missing_model_file_is_an_error_that_names_it) {
    try {
        (void)inference::Model::load("C:/no/such/model.gguf");
    } catch (const inference::Error& e) {
        check::is_true(std::string(e.what()).find("C:/no/such/model.gguf") != std::string::npos, "the path in the message");
        return;
    }
    check::fail("expected an error");
}

TEST(options_out_of_range_are_refused_before_anything_loads) {
    check::throws([] { (void)inference::Model::load(model_path(), {16, 4, 0}); }, "a context too small");
    check::throws([] { (void)inference::Model::load(model_path(), {4096, 0, 0}); }, "no threads");
    check::throws([] { (void)inference::Model::load(model_path(), {4096, 4, -1}); }, "negative gpu layers");
}

TEST(a_model_kept_on_the_cpu_says_so) {
    check::equal(model().device(), std::string("CPU"), "the shared test model, loaded with no layers offloaded");
}

TEST(offloaded_layers_go_to_a_gpu_when_the_build_has_one) {
    inference::ModelOptions options;
    options.gpu_layers = 1000;  // past the test model's 24 layers: all of them
    const std::unique_ptr<inference::Model> offloaded = inference::Model::load(model_path(), options);
#if INFERENCE_TESTS_GPU
    // A build with a GPU backend, run on a machine with a GPU: the layers go there, and the
    // model still answers inside its grammar, the same way every time.
    check::is_true(offloaded->device() != "CPU", "the premise: a GPU on this machine, not " + offloaded->device());
    inference::Sampling held;
    held.max_tokens = 8;
    held.grammar = R"(root ::= "yes" | "no")";
    const std::string prompt = offloaded->chat_prompt({{"user", "Is water wet? Answer yes or no."}});
    const std::string reply = offloaded->complete(prompt, held).text;
    check::is_true(reply == "yes" || reply == "no", "held by its grammar on the GPU: " + reply);
    inference::Sampling greedy;
    greedy.max_tokens = 32;
    const std::string first = offloaded->complete(prompt, greedy).text;
    check::equal(offloaded->complete(prompt, greedy).text, first, "greedy output repeats on the GPU");
#else
    // A build for the CPU alone runs the model there, however many layers are asked for.
    check::equal(offloaded->device(), std::string("CPU"), "no GPU backend in this build");
#endif
}

TEST(the_model_loads_and_counts_tokens) {
    const int short_text = model().count_tokens("hello");
    const int long_text = model().count_tokens("hello there, friend; the river runs long and the bridges are few");
    check::is_true(short_text > 0, "a word is some tokens");
    check::is_true(long_text > short_text, "a sentence is more");
}

TEST(a_chat_prompt_uses_the_models_own_template) {
    const std::string prompt = ask("Where is the station?");
    check::is_true(prompt.find("Where is the station?") != std::string::npos, "the question is in it");
    check::is_true(prompt.find("<|im_start|>user") != std::string::npos, "in the model's ChatML turns");
    check::is_true(prompt.rfind("<|im_start|>assistant") != std::string::npos, "ending where the reply begins");
}

TEST(greedy_completion_says_the_same_thing_every_time) {
    inference::Sampling sampling;
    sampling.max_tokens = 24;
    const std::string prompt = ask("Name one colour.");
    const inference::Completion first = model().complete(prompt, sampling);
    const inference::Completion second = model().complete(prompt, sampling);
    check::is_true(!first.text.empty(), "some text");
    check::equal(second.text, first.text, "the same text: nothing carried between calls");
    check::is_true(first.tokens <= 24, "within the limit");
}

TEST(nothing_said_in_one_call_reaches_the_next) {
    // Tell the model a word in one call, ask for it in the next. What it answers must be exactly
    // what a model that never heard the word answers: a second, freshly loaded model is the
    // oracle. (Asking one thing twice cannot show this: a model that still saw the first call
    // tends to repeat itself anyway.)
    inference::Sampling sampling;
    sampling.max_tokens = 16;
    const std::string tell = model().chat_prompt({{"user", "Remember this secret word: marmalade. Reply only OK."}});
    const std::string ask_back = model().chat_prompt({{"user", "What secret word did I give you? If none, reply none."}});
    (void)model().complete(tell, sampling);
    const std::string after = model().complete(ask_back, sampling).text;
    std::unique_ptr<inference::Model> fresh = inference::Model::load(model_path());
    const std::string never_told = fresh->complete(ask_back, sampling).text;
    check::equal(after, never_told, "the answer of a model that never heard the word");
    check::is_true(never_told.find("marmalade") == std::string::npos, "the premise: without the word, it cannot say it");
}

TEST(a_seed_makes_sampling_reproducible) {
    inference::Sampling sampling;
    sampling.max_tokens = 24;
    sampling.temperature = 0.9f;
    sampling.seed = 7;
    const std::string prompt = ask("Invent a name for a boat.");
    check::equal(model().complete(prompt, sampling).text, model().complete(prompt, sampling).text, "the same seed, the same text");
}

TEST(max_tokens_bounds_the_reply) {
    inference::Sampling sampling;
    sampling.max_tokens = 3;
    const inference::Completion c = model().complete(ask("Tell me a long story about a river."), sampling);
    check::is_true(c.tokens <= 3, "at most three tokens");
    check::is_true(!c.stopped_at_end, "cut by the limit, not ended by the model");
}

TEST(a_grammar_holds_the_reply_to_its_language) {
    // The control first: asked freely, the model says more than one bare word, so the
    // constrained answer below is the grammar's doing.
    const std::string prompt = ask("Is fire hot? Answer, then explain why.");
    inference::Sampling free_text;
    free_text.max_tokens = 24;
    const std::string free_answer = model().complete(prompt, free_text).text;
    check::is_true(free_answer != "yes" && free_answer != "no", "the premise: unconstrained, it says more: " + free_answer);
    inference::Sampling bounded = free_text;
    bounded.grammar = "root ::= \"yes\" | \"no\"";
    const inference::Completion c = model().complete(prompt, bounded);
    check::is_true(c.text == "yes" || c.text == "no", "only a word of the grammar: " + c.text);
    check::is_true(c.stopped_at_end, "and then it ends");
}

TEST(a_json_grammar_yields_one_of_a_closed_set_of_labels) {
    // Classification held by a grammar: text in, one label from a closed set out, as JSON. The
    // labels are neutral on purpose; this component knows nothing of what a caller classifies.
    const std::set<std::string> labels = {"greeting", "question", "farewell", "unclear"};
    std::string alternatives;
    for (const std::string& label : labels)
        alternatives += std::string(alternatives.empty() ? "" : " | ") + "\"\\\"" + label + "\\\"\"";
    inference::Sampling sampling;
    sampling.max_tokens = 32;
    sampling.grammar = "root ::= \"{\\\"label\\\": \" label \"}\"\nlabel ::= " + alternatives;
    const std::string prompt = model().chat_prompt(
        {{"system", "Classify the message as one label, as JSON."}, {"user", "Hello! What time does the museum open?"}});
    const inference::Completion c = model().complete(prompt, sampling);
    const std::string head = "{\"label\": \"";
    check::is_true(c.text.rfind(head, 0) == 0 && c.text.size() > head.size() + 2 && c.text.substr(c.text.size() - 2) == "\"}",
                   "the JSON shape: " + c.text);
    const std::string label = c.text.substr(head.size(), c.text.size() - head.size() - 2);
    check::is_true(labels.count(label) == 1, "a label from the set: " + label);
}

TEST(streamed_pieces_add_up_to_the_reply_and_never_split_a_character) {
    // The grammar forces rare four-byte characters (Gothic letters), which a byte-level
    // vocabulary spells in more than one token, so some token must end partway through a
    // character. The premise below checks that one did: fewer pieces reached the caller than
    // tokens were produced, because the runtime held a character's first bytes back.
    const std::string forced = "Hail, \xF0\x90\x8D\x88\xF0\x90\x8C\xB0\xF0\x90\x8D\x82 friend";
    inference::Sampling sampling;
    sampling.max_tokens = 48;
    sampling.grammar = "root ::= \"" + forced + "\"";
    std::string streamed;
    int pieces = 0;
    const inference::Completion c = model().complete(ask("Greet me."), sampling, [&](std::string_view piece) {
        check::is_true(valid_utf8(piece), "a whole-character piece");
        streamed += piece;
        ++pieces;
        return true;
    });
    check::equal(c.text, forced, "the grammar's text");
    check::equal(streamed, c.text, "the pieces are the reply");
    check::is_true(pieces < c.tokens, "the premise: some character spanned tokens (" + std::to_string(pieces) +
                                          " pieces, " + std::to_string(c.tokens) + " tokens)");
}

TEST(returning_false_stops_the_stream) {
    inference::Sampling sampling;
    sampling.max_tokens = 64;
    int pieces = 0;
    const inference::Completion c = model().complete(ask("Count from one to twenty."), sampling, [&](std::string_view) {
        ++pieces;
        return pieces < 2;
    });
    check::is_true(c.stopped_by_caller, "stopped by the caller");
    check::equal(pieces, 2, "no piece after the refusal");
}

TEST(a_malformed_grammar_is_an_error) {
    inference::Sampling sampling;
    sampling.grammar = "root ::= (";
    check::throws([&] { (void)model().complete(ask("Hello."), sampling); }, "a grammar that does not parse");
}

TEST(a_reply_that_cannot_fit_the_context_is_refused) {
    std::unique_ptr<inference::Model> small = inference::Model::load(model_path(), {128, 4, 0});
    inference::Sampling sampling;
    sampling.max_tokens = 120;
    const std::string prompt = small->chat_prompt({{"user", "Describe, at length, every road north."}});
    check::is_true(small->count_tokens(prompt) + sampling.max_tokens > 128, "the premise: it does not fit");
    bool refused = false;
    try {
        (void)small->complete(prompt, sampling);
    } catch (const inference::Error& e) {
        // The runtime's own refusal, before any decoding -- not a failure somewhere inside it.
        refused = std::string(e.what()).find("do not fit the context") != std::string::npos;
    }
    check::is_true(refused, "refused up front, as not fitting the context");
    sampling.max_tokens = 128 - small->count_tokens(prompt);
    check::is_true(sampling.max_tokens > 0, "the premise: the prompt alone fits");
    (void)small->complete(prompt, sampling);  // exactly full is allowed: the refusal is the bound, not below it
}

TEST(texts_close_in_meaning_embed_closer) {
    inference::Model& m = embedder();
    const auto a = m.embed("Is it safe to travel these roads?");
    check::equal(a.size(), static_cast<std::size_t>(m.embedding_size()), "the model's own width");
    check::is_true(std::abs(dot(a, a) - 1.0f) < 1e-4f, "unit length");
    // Each asks the same of its first two as of its first and third: which is nearer in meaning.
    const std::vector<std::array<std::string, 3>> triples = {
        {"Is it safe to travel these roads?", "Are the roads dangerous?", "I would like a loaf of bread."},
        {"Hello there!", "Good morning to you.", "Where is the smithy?"},
        {"Thank you for your help.", "I'm grateful to you.", "The river floods in spring."},
        {"You're a useless old fool.", "What a worthless idiot you are.", "Fine weather for the harvest."},
    };
    // (Not `near` and `far`: windows.h defines both away.)
    for (const auto& [text, alike, unlike] : triples) {
        const auto t = m.embed(text), a_ = m.embed(alike), u = m.embed(unlike);
        check::is_true(dot(t, a_) > dot(t, u) + 0.05f,
                       "\"" + text + "\" nearer \"" + alike + "\" (" + std::to_string(dot(t, a_)) + ") than \"" + unlike + "\" (" +
                           std::to_string(dot(t, u)) + ")");
    }
}

TEST(an_embedding_is_the_same_every_time) {
    inference::Model& m = embedder();
    const auto once = m.embed("What should I watch out for around here?");
    (void)m.embed("Something else entirely, in between.");
    check::is_true(m.embed("What should I watch out for around here?") == once, "the same vector, whatever came between");
}

TEST(a_model_is_loaded_for_text_or_for_embeddings) {
    // Refused up front, by the runtime's own words, before anything is decoded.
    const auto refusal = [](const std::function<void()>& call) {
        try {
            call();
        } catch (const inference::Error& e) {
            return std::string(e.what());
        }
        return std::string("(nothing thrown)");
    };
    const std::string text_model = refusal([] { (void)model().embed("Hello."); });
    check::is_true(text_model.find("not loaded for embeddings") != std::string::npos, "a text model embeds nothing: " + text_model);
    const std::string embedding_model = refusal([] { (void)embedder().complete("Hello.", {}); });
    check::is_true(embedding_model.find("loaded for embeddings, not for text") != std::string::npos,
                   "an embedding model writes nothing: " + embedding_model);
    check::throws([] { (void)embedder().embed(""); }, "an empty text");
}
