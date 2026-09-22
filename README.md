# FWGC++

A fantasy world generator in C++: a seed in, a spherical planet out — plate tectonics,
a hypsometric surface, surface detail, erosion and a land mask — as a dependency-free
static library plus a CLI.

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

| `--size` | kernel time | octaves resolved | JSON |
|---:|---:|:---:|---:|
| 33 | 2 ms | 1 / 5 | 0.3 MB |
| 65 | 8 ms | 2 / 5 | 1.1 MB |
| 129 | 32 ms | 3 / 5 | 4.4 MB |
| 257 | 130 ms | 4 / 5 | 17.6 MB |
| 513 | 568 ms | 5 / 5 | 69.8 MB |

Measured on this machine at phase 5, seed 42, Release. Cost is linear in cells. **A world
below size 513 has an incomplete octave ladder** — size 33 resolves one octave of five, so
a small preview world is not a small version of the real one, it is a world with almost no
surface detail.

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

The generator is a sixteen-stage ladder. **Stages 1–5 are implemented**; stages 6–16 are
not. A world reports the stage it actually reached in `completed_stage` and says so in
`warnings` rather than implying a whole world. [docs/porting-status.md](docs/porting-status.md)
tracks the ladder.

## Documents

- [docs/determinism.md](docs/determinism.md) — the float and RNG contract, and what a
  consumer must not do to it.
- [docs/porting-status.md](docs/porting-status.md) — the stage ladder and what each stage
  owes.
- [docs/porting-notes.md](docs/porting-notes.md) — the traps that were expensive to find
  in the reference implementation and are invisible until they are wrong.

## Testing

There is no reference implementation to diff against, so the test suite **is** the
acceptance mechanism. It asserts properties rather than pinned numbers, and — where a
control exists — that the metric moves when the control does. A metric that ignores its
own knob is the measurement failing, not the model.
