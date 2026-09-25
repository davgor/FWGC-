# inference/: local rules

The local model runtime. **Lane: Claude.** The repository-wide rules are in
[`../AGENTS.md`](../AGENTS.md), and [`README.md`](README.md) says what it does, how to get the
test model and what each test guarantees.

## Build and test

```bash
cmake -S inference -B inference/build -G "Visual Studio 17 2022" -A x64
cmake --build inference/build --config Release
ctest --test-dir inference/build -C Release --no-tests=error
```

## Rules

- **It knows nothing of worlds.** No world, quest, hero, NPC, city, settlement or player; no
  generator or orchestrator. It depends on no other component. The layer checker refuses those
  words in identifiers here, and those includes.
- **Local only.** The runtime opens no network connection. llama.cpp is built without its tools,
  server or HTTPS support. Anything that would fetch a model at run time is out: a model is a
  local file.
- **llama.cpp is pinned by hash.** Moving to a new release changes the URL and the SHA-256 in
  `CMakeLists.txt` together. It lands in a commit of its own, with the tests run against the new
  release, since its API changes between releases.
- **The public header shows none of llama.cpp,** and `inference` links it privately. Keep
  llama.cpp's types out of `include/`.
- **A GPU build loads its backends at run time; the program links no GPU API.** Linked in,
  Vulkan made `vulkan-1.dll` a dependency of the program itself, and a machine without it could
  not start. `the_program_links_no_gpu_api_of_its_own` guards this; keep it the first test.
- **Ship only the backend DLLs the build makes:** `llama`, `ggml`, `ggml-base`, the `ggml-cpu-*`
  variants and `ggml-vulkan`. Never a `ggml-rpc.dll`: RPC talks over sockets. ggml would load any
  backend beside the runtime or named by `GGML_BACKEND_PATH`. The runtime unloads every one but
  the CPU and Vulkan, and `a_backend_the_runtime_does_not_ship_is_unloaded` guards that. A new
  backend (CUDA, say) joins that list on purpose, in the same commit as its test.
- **Models are downloaded, never committed.** `models/` is ignored. A new model is named in the
  README with its source, size, SHA-256 and licence, and the owner approves every download.
- **Tests fail loudly without their model**, never skip. They assert properties (grammar-held
  output, reproducibility, whole-character streaming, refusals), not a model's exact prose.
