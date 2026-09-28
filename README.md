# FWGC++

A monorepo. Each component lives in its own top-level folder with its own build and its own
README.

| Folder | What it is |
|---|---|
| [generator/](generator/) | The fantasy world generator: a seed in, a finished world out -- tectonics to heroes, all sixteen stages -- as a dependency-free C++20 static library (`fwg`) and a CLI (`fwg.exe`). |
| [viewer/](viewer/) | A browser viewer for generated worlds: every published control, the world map with its layers and overlays, an inspector that follows the document's joins, city, hamlet, castle and key-location plans in 2D and 3D, and a debug panel. A dependency-free Node server over `fwg.exe` (`node server.mjs`) and a static page. |
| [orchestrator/](orchestrator/) | The director: deterministic C++ that holds one world, changes it only through the generator's logged verbs, and answers a player's dialogue acts as knowledge-scoped fact packets for a model to phrase. It keeps what people have heard in a brain database (SQLite, in memory) the game reads with SQL. It depends on the generator, and on SQLite's source, fetched at build time as [dependencies.json](dependencies.json) names it. |
| [communicator/](communicator/) | NPC dialogue. The interpreter turns a player's line into one dialogue act, and the voice puts a fact packet into the speaker's words. A grammar holds each model answer to what C++ can use. It sees the orchestrator's contract, never a world, and depends on the orchestrator and inference. |
| [inference/](inference/) | The local model runtime: a GGUF model in memory, text in, text out, on the player's machine, with chat templates, streaming and GBNF-grammar-held output. llama.cpp, pinned by hash in [dependencies.json](dependencies.json). It knows nothing of worlds and depends on no other component. |
| [concept art/](concept%20art/) | Three-tier concept art production map and master asset checklist, grounded in the generator's authored catalogues. |
| [ui-mockups/](ui-mockups/) | Mockups of the games' UI screens, each designed on a Claude Design canvas, with the approved exports the game's UI is built from. |
| [tools/](tools/) | The layer checker and the commit gate that keep the components apart: `check_layers` on every commit, a context-cleared red-team review of every commit, and the git hooks that run them. |

**Before changing anything, read [AGENTS.md](AGENTS.md)**: who works where, how the components
stay apart, and how to commit (`git config core.hooksPath tools/hooks`, once per clone).

Build a component from inside its folder; see its README. For the generator:

```bash
cd generator
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Dependencies

Everything the repository fetches from outside is listed in [dependencies.json](dependencies.json):
what it is, its source, its exact bytes (size and hash), its licence and what that obliges, which
components use it, whether it ships, and when the owner approved it. Nothing fetched is committed.

- **Builds** fetch through [dependencies.cmake](dependencies.cmake) (`fwgc_dependency(<name>)`):
  each file is downloaded once into a shared cache (`%LOCALAPPDATA%/fwgc/deps`, or
  `$FWGC_DEPS_CACHE`), checked by its hash on the way in and again when a build unpacks it. Every
  build folder on the machine shares the one download. Today: llama.cpp (inference) and SQLite (the
  orchestrator).
- **A new machine** runs `cmake -P dependencies.cmake` for the build sources, adds `-DWITH=models`
  for the models (into `inference/models/`), and `-DVERIFY=ON` to check what is there by hash
  without fetching.
- **Python** (training, dev-time only) installs from a hash-locked `training/requirements.txt`
  (`pip install --require-hashes`).
- **Installers** (the Vulkan SDK) are installed by hand; the manifest says which, and where from.

The shipped game downloads nothing: fetching is a build-time and dev-time matter only. A new
dependency needs the owner's approval of the file, its source and its size, and lands in the
manifest in the commit that first uses it.
