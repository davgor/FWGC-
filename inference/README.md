# FWGC++ inference

The local model runtime: a GGUF model file in memory, text in, text out, on the player's
machine. It knows nothing of worlds, quests or people. The layer checker keeps those words out,
and it depends on no other component. The communicator (NPC dialogue) and the creator (new-world
setup) will build on it.

This is the `inference/` component of the FWGC++ monorepo.

## Build and test

```bash
cmake -S inference -B inference/build -G "Visual Studio 17 2022" -A x64
cmake --build inference/build --config Release
ctest --test-dir inference/build -C Release --no-tests=error
```

The first configure downloads **llama.cpp v0.5.0**, the source archive from
`codeload.github.com/ggml-org/llama.cpp` (37,561,391 bytes). `CMakeLists.txt` pins it by
SHA-256, so a different archive fails the build.

Only the library is built:
- no tools, server, examples or tests of llama.cpp's own;
- no HTTPS support (`LLAMA_OPENSSL` off);
- the CPU backend only.

The runtime downloads nothing. Dev tooling may use the network; what ships may not.

### The test model

The tests run a real model, which is downloaded and never committed. `inference/.gitignore`
keeps `models/` out of git.

| | |
|---|---|
| File | `qwen2.5-0.5b-instruct-q4_0.gguf` |
| From | `https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF` |
| Size | 428,730,208 bytes |
| SHA-256 | `7671c0c304e6ce5a7fc577bcb12aba01e2c155cc2efd29b2213c95b18edaf6ed` |
| Licence | Apache-2.0 |

```bash
curl -L -o inference/models/qwen2.5-0.5b-instruct-q4_0.gguf \
  https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_0.gguf
sha256sum inference/models/qwen2.5-0.5b-instruct-q4_0.gguf
```

`FWGC_TEST_MODEL` points the tests at another copy. Without a model, every test that needs one
**fails** and says how to get it; none is skipped.

This is a test model, not a choice for the game. The model that ships, and its fine-tuning, is
a later decision (the plan's "Building our own model").

## The API

`include/inference/runtime.hpp` shows none of llama.cpp. A caller depends on this contract, not
on llama.cpp's API, which changes between releases.

- **`Model::load(path, {context_tokens, threads, gpu_layers})`** loads a GGUF file,
  memory-mapped.
- **`chat_prompt(messages)`** applies the model's own chat template.
- **`complete(prompt, sampling, on_piece)`** continues a prompt.
  - It is greedy when `temperature` is 0; otherwise it samples from `seed`.
  - It is held to a GBNF `grammar` when one is given.
  - It streams each piece to `on_piece`, never splitting a UTF-8 character. Returning `false`
    stops the stream.
- **`count_tokens(text)`** budgets a prompt against the context.

## What it guarantees, each with a test

- **A grammar holds the output to its language.** Asked freely, the model explains itself; with
  `root ::= "yes" | "no"` it says one word and stops. With a JSON grammar over a closed set of
  labels, it can only answer one of those labels. Classification held by a grammar is what a
  caller builds on.
- **Nothing carries between calls.** The context is cleared first. A word told to the model in
  one call is unknown in the next: the answer matches a freshly loaded model's exactly. The same
  greedy prompt gives the same reply twice.
- **A seed makes sampling reproducible,** for one build and one thread count.
- **Streaming never splits a character**, and the pieces add up to the reply.
- **Refusals come before any work:** a prompt that cannot fit the context with `max_tokens`
  after it, a grammar that does not parse, a missing file, or options out of range.
- **No network library in the binary.** Checked on 2026-09-24 with `dumpbin /dependents` on the
  test binary: `KERNEL32`, `ADVAPI32`, the C and C++ runtimes, and `VCOMP140` (the OpenMP runtime
  ggml threads with, part of the Visual C++ redistributable a game ships). No `ws2_32`, `winhttp`
  or `wininet`.

One noise: llama.cpp's grammar parser prints a malformed grammar's error to stderr itself, past
the log hook this runtime silences. The call still fails with an `Error`.

## Not in this slice

- **GPU offload.** The game shares its GPU with the renderer, and `gpu_layers` needs a Vulkan or
  CUDA backend built in, and a VRAM budget measured on the game.
- **The interpreter's classifier.** A small encoder is the plan; a grammar-held model can do the
  job meanwhile.
- **Stop strings, several sequences at once, and reusing a prompt's cache across calls.**
- **Shipping.** When a model ships, the game ships llama.cpp's MIT licence and the model's own
  licence with it.
