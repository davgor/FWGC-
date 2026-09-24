# FWGC++

A monorepo. Each component lives in its own top-level folder with its own build and its own
README.

| Folder | What it is |
|---|---|
| [generator/](generator/) | The fantasy world generator: a seed in, a finished world out -- tectonics to heroes, all sixteen stages -- as a dependency-free C++20 static library (`fwg`) and a CLI (`fwg.exe`). |
| [viewer/](viewer/) | A browser viewer for generated worlds: every published control, the world map with its layers and overlays, an inspector that follows the document's joins, city, hamlet, castle and key-location plans in 2D and 3D, and a debug panel. A dependency-free Node server over `fwg.exe` (`node server.mjs`) and a static page. |
| [orchestrator/](orchestrator/) | The director: deterministic C++ that holds one world, changes it only through the generator's logged verbs, and answers a player's dialogue acts as knowledge-scoped fact packets for a model to phrase. It depends on the generator alone. |
| [concept art/](concept%20art/) | Three-tier concept art production map and master asset checklist, grounded in the generator's authored catalogues. |
| [tools/](tools/) | The layer checker and the commit gate that keep the components apart: `check_layers` on every commit, a context-cleared red-team review of every commit, and the git hooks that run them. |

**Before changing anything, read [AGENTS.md](AGENTS.md)**: who works where, how the components
stay apart, and how to commit (`git config core.hooksPath tools/hooks`, once per clone).

Build a component from inside its folder; see its README. For the generator:

```bash
cd generator
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
