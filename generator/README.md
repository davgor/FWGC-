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

Measured 2026-09-23 on this machine: phase 16 (the default), seed 42, Release, one run each;
size 513 was not measured. Stage 16's planners are most of the cost, and every plan's copy
of the catalogues is most of the document. **A world below size 513 has an incomplete
octave ladder** — size 33 resolves one octave of five, so a small preview world is not a
small version of the real one, it is a world with almost no surface detail.

As a library:

```cpp
#include "fwg/world.hpp"

fwg::Request request;
request.seed = 42;
request.size = 129;
const fwg::World world = fwg::generate(fwg::resolve(request));
const fwg::Grid& height = world.layers.at("height");   // metres
```

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
