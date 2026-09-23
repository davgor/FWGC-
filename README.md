# FWGC++

A monorepo. Each component lives in its own top-level folder with its own build and its own
README.

| Folder | What it is |
|---|---|
| [generator/](generator/) | The fantasy world generator: a seed in, a finished world out -- tectonics to heroes, all sixteen stages -- as a dependency-free C++20 static library (`fwg`) and a CLI (`fwg.exe`). |

Build a component from inside its folder; see its README. For the generator:

```bash
cd generator
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
