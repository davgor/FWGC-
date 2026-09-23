# FWGC++ generator

A fantasy world generator in C++: a seed in, a finished spherical world out — plate
tectonics, water, climate, magic, cities and roads, their hinterland, nests and faiths, two
turned ages of wars, ruins and villains, and the stage-16 world of plans, bands, heroes,
rosters and key locations — as a dependency-free static library plus a CLI.

This is the `generator/` component of the FWGC++ monorepo. Every command below runs from
this folder.

This is a **clean-room rewrite** of the Python FantasyWorldGenerator, not a transliteration
of it. It takes that project's algorithms and its hard-won world-shape arithmetic, and
replaces its dependence on CPython's `random` module, its compensated float summation and
its interpreter-version-sensitive numerics with a stream of its own. **It therefore
produces different worlds from the same seed number, by design** — see
[docs/determinism.md](docs/determinism.md).

## Build

Needs CMake 3.20+ and a C++20 compiler. MSVC is the primary target.

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

On this machine CMake ships with the VS 2022 Build Tools and is not on `PATH`:

```bash
export PATH="/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin:$PATH"
```

## Use

```bash
./build/apps/Release/fwg.exe generate --seed 42 --size 129 -o world.json
```

`--size` is the sampling grid along one edge of the sphere and must be odd. It is a
**content** control as much as a detail control: the surface-noise octave ladder is
Nyquist-filtered against cell spacing, so a coarse world does not resolve fine terrain at
any wavelength.

| `--size` | generation | octaves resolved | JSON |
|---:|---:|:---:|---:|
| 33 | 1.9 s | 1 / 5 | 60 MB |
| 65 | 3.4 s | 2 / 5 | 108 MB |
| 129 | 5.4 s | 3 / 5 | 156 MB |
| 257 | 10.6 s | 4 / 5 | 276 MB |
| 513 | 32.8 s | 5 / 5 | 687 MB |

Measured 2026-09-23 on this machine: phase 16 (the default), seed 42, Release, one run each;
size 513 peaked at 960 MB of memory. Stage 16's planners are most of the cost up to size 129.
What the document is
made of shifts with size: at 33 the plans are half of it and the world scene a fifth, and at
257 the 166 layer grids alone are 109 MB. **A world below size 513 has an incomplete
octave ladder** — size 33 resolves one octave of five, so a small preview world is not a
small version of the real one, it is a world with almost no surface detail.

### In parts: the split export

```bash
./build/apps/Release/fwg.exe generate --seed 42 --size 129 --split world/
```

writes the same world as a directory a game can read piece by piece:

- `world.json`: the document without its plan rows and its world scene. Each plan block
  lists its rows' files (`files`: `[{city_uid, path}]` and so on) where the document carries
  the rows.
- `plans/{city,hamlet,castle,key_location}/`: one plan per file. Each is written as the
  document writes the row, less the copies of the world's catalogues every row carries;
  `world.json` holds those once.
- `scene.json`: the world scene.

The destination must be new or empty, and a failed export removes what it wrote.
`fwg::write_world_split` does the same from code; `include/fwg/serialize.hpp` documents the
layout.

| `--size` | whole document | `world.json` | `scene.json` | plans | largest plan |
|---:|---:|---:|---:|---:|---:|
| 33 | 60.2 MB | 17.2 MB | 11.4 MB | 24.9 MB in 398 files | 0.86 MB |
| 65 | 108.8 MB | 40.9 MB | 17.9 MB | 38.4 MB in 753 files | 0.85 MB |
| 129 | 156.9 MB | 80.6 MB | 22.0 MB | 41.9 MB in 926 files | 0.90 MB |
| 257 | 276.5 MB | 195.8 MB | 22.9 MB | 44.7 MB in 860 files | 0.86 MB |

Measured the same day, seed 42. The split is 5–11% smaller in total, since each catalogue is
written once. Its value is that a reader takes only what it needs. The plans and the scene
barely grow past size 129, and `world.json` does: at 257, its 166 layer grids are 109 MB of
its 196.

### As a library

```cpp
#include "fwg/world.hpp"

fwg::Request request;
request.seed = 42;
request.size = 129;
const fwg::World world = fwg::generate(fwg::resolve(request));
const fwg::Grid& height = world.layers.at("height");   // metres
```

Linking the library into the game skips the document altogether: the world is already in
memory. A sibling component of this monorepo adds the folder and links the target:

```cmake
add_subdirectory(../generator fwg)   # the binary-directory argument is required
target_link_libraries(my_game PRIVATE fwg::fwg)
```

Added this way, the generator builds the library alone; no test suite or CLI. Your target
gets the two things the headers need and nothing else:
- C++20;
- the determinism flags (`/fp:precise`; `-ffp-contract=off -fno-fast-math` elsewhere),
  which are part of the contract (see [docs/determinism.md](docs/determinism.md)).

The generator's warning policy (`/W4 /WX`) applies to its own code only.

The library has **no dependencies at all** — JSON, SHA-256, the RNG and the test harness
are all in-tree — so it drops into an engine build unchanged.

## Status

The generator is a sixteen-stage ladder and **all sixteen stages run**. A world reports the
stage it actually reached in `completed_stage`, and `systems` lists what ran.
[docs/porting-status.md](docs/porting-status.md) tracks the ladder, the APIs a game calls
after generation, and the open rulings.

## Documents

- [docs/determinism.md](docs/determinism.md) — the float and RNG contract, what a consumer
  must not do to it, and the traps that are invisible until they are wrong.
- [docs/porting-status.md](docs/porting-status.md) — the stage ladder, the APIs a game calls
  after generation, the world document, rulings and deliberate departures.
- [docs/scale-defects.md](docs/scale-defects.md) — the authored metres that do not scale
  with the planet, and what they cost at shipped widths.

## Testing

```bash
ctest --test-dir build -C Release --no-tests=error
```

There is no reference implementation to diff against, so the test suite **is** the
acceptance mechanism. It asserts properties rather than pinned numbers, and — where a
control exists — that the metric moves when the control does. A metric that ignores its own
knob is the measurement failing, not the model.

Pass `--no-tests=error`. Without it `ctest` exits 0 when it selects nothing, so a filter
typo or a `-DFWG_BUILD_TESTS=OFF` build reads as a pass.

Pinned values are deliberate and few: the FIPS SHA-256 vectors; the RNG golden values in
`tests/test_substrate.cpp`, which exist because every other test asserts a *property* of the
stream and a changed constant satisfies all of them while producing a different planet from
every seed; and `tests/fixtures/`, the product contracts' cases and the reference document's
key paths that `tests/test_document_paths.cpp` holds the document to.
