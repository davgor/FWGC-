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

### Candidate models

Downloaded with the owner's approval to measure against the test model, never committed. The
tests do not use them.

| | |
|---|---|
| File | `Qwen3-4B-Instruct-2507-Q4_K_M.gguf` |
| From | `https://huggingface.co/unsloth/Qwen3-4B-Instruct-2507-GGUF` (a quantization of `Qwen/Qwen3-4B-Instruct-2507`) |
| Size | 2,497,281,120 bytes |
| SHA-256 | `3605803b982cb64aead44f6c1b2ae36e3acdb41d8e46c8a94c6533bc4c67e597` |
| Licence | Apache-2.0 |
| Approved | 2026-09-24 |

It runs in this runtime unchanged: its chat template is ChatML, and it has no thinking mode.

## The API

`include/inference/runtime.hpp` shows none of llama.cpp. A caller depends on this contract, not
on llama.cpp's API, which changes between releases.

- **`Model::load(path, {context_tokens, threads, gpu_layers})`** loads a GGUF file,
  memory-mapped. `gpu_layers` above 0 offloads that many layers (a count past the model's layers
  offloads all) to the GPU the runtime chooses. A discrete GPU comes before an integrated one,
  then the one with more memory, and only one: left alone, llama.cpp would split a model across
  a laptop's two GPUs. With no GPU backend in the build, or no GPU in the machine, the model runs
  on the CPU.
- **`device()`** says where it runs: `CPU`, or the GPU's name.
- **`inference::backends()`** lists what models can run on in this process: `CPU`, and `Vulkan`
  where a GPU build loaded it.
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
- **A grammar costs little when the model's own choice fits it.** Each step checks the token the
  model would choose anyway. Only when the grammar refuses it does the whole vocabulary go
  through the grammar, and the choice is made again among what it allows. Greedy output is the
  same as checking everything first: the best token the grammar allows is the best allowed one.
  Checking all 151,936 tokens at every step had made held prose about 65 ms a token slower than
  free text.
  - Measured on 2026-09-24 with the test model, over 8 held replies of prose and 17
    classifications: the text was identical to checking everything first.
  - Held replies took 0.3–1.4 s where they had taken 0.7–2.3 s, within 0.2 s of the same prompts
    unheld.
  - The grammar tests cover both paths. A mutation pass broke the fallback three ways (keep a
    refused token, choose again without the grammar, never check), and tests failed each time.
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
- **A model kept on the CPU says so,** and in a build with a GPU backend, offloaded layers reach
  a GPU: the model answers inside its grammar there, and greedy output repeats.
- **No GPU API is linked into the program.** Before any model loads, `vulkan-1.dll` is not in
  the process. A first Vulkan build linked the backend in, and `dumpbin` showed `vulkan-1.dll`
  among the program's own imports: on a machine without Vulkan it would not have started.
- **Only the backends the runtime ships stay loaded:** the CPU and Vulkan.
  - ggml loads any backend it finds beside the runtime, and any one `GGML_BACKEND_PATH` names.
    Its RPC backend talks over sockets.
  - The test offers a test-only backend through `GGML_BACKEND_PATH`. It checks that ggml loaded
    it and that the runtime unloaded it.
  - With the unloading switched off, the test fails.

## The GPU build

`-DINFERENCE_VULKAN=ON` builds llama.cpp's Vulkan backend. Building needs the Vulkan SDK
(LunarG 1.4.357.0 here); a player needs only a GPU driver, which carries Vulkan on Windows for
NVIDIA, AMD and Intel.

That build is shaped for shipping. llama.cpp and its backends are DLLs in one folder
(`bin/Release`), loaded at run time from the folder of the module the runtime is in:
- `llama.dll`, `ggml.dll` and `ggml-base.dll` beside the program;
- `ggml-vulkan.dll`, skipped when it will not load, which leaves the CPU;
- the CPU backend in one DLL per processor generation (x64 to icelake and alderlake), the best
  the machine runs chosen at start. The default build, for the CPU alone, is compiled for the
  machine that builds it (AVX2 here), which is fine for development and wrong to ship.

Measured on 2026-09-24: communicator_eval, Qwen3-4B-Instruct-2507, every layer on an RTX 5080
Laptop GPU (16 GB), against the CPU at 8 threads:

| | CPU | GPU |
|---|---|---|
| Time per line (the interpreter) | 5.3 s | 0.27 s |
| Time per reply (the voice) | 4.5 s | 0.40 s |
| Lines read fully right, of 243 | 186 | 186 |
| GPU memory | none | about 3.4 GB |

- **The same accuracy, not the same text.** 4 lines were read differently each way, and 17 of 24
  replies were worded differently. GPU arithmetic is not the CPU's, so greedy output differs
  between them. Tests that pin exact output run on the CPU.
- **The first call warms the GPU up,** about 16 s on this machine, while the driver compiles
  its shaders. A game makes that call behind a loading screen.

One noise: llama.cpp's grammar parser prints a malformed grammar's error to stderr itself, past
the log hook this runtime silences. The call still fails with an `Error`.

## Not in this slice

- **Sharing the GPU with the game.** The renderer (Direct3D 12 in Unreal) and Vulkan do not
  share a memory budget. The game should check free GPU memory before choosing `gpu_layers`, and
  a player setting should choose GPU, CPU or off. Only NVIDIA has been measured; AMD and Intel
  should be measured before anything ships.
- **CUDA.** It would be a further backend DLL, loaded the same way, if measured speed on NVIDIA
  ever calls for it.
- **An encoder classifier.** A small model that only classifies is cheaper than a generative
  one. Until it exists, a grammar-held generative model does the classifying.
- **Stop strings, several sequences at once, and reusing a prompt's cache across calls.**
- **Shipping.** When a model ships, the game ships llama.cpp's MIT licence and the model's own
  licence with it.
