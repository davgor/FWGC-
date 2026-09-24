@AGENTS.md

# For Claude, in inference/

- **Build at a short path,** such as `%TEMP%\fwgi`. llama.cpp's source tree is deep, and MSBuild
  paths under a long base pass Windows' 260-character limit.
- **Check llama.cpp's own headers before calling its API.** It renames and deprecates between
  releases (`llama_model_load_from_file`, `llama_init_from_model`, `llama_vocab_is_eog`,
  `LLAMA_CURL` gone and `LLAMA_OPENSSL` in its place in v0.5.0). Read
  `_deps/llama_cpp-src/include/llama.h` in the build folder, not memory.
- **Downloads need the owner's approval by file name, source and size,** each time.
