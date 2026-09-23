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

The generator is a sixteen-stage ladder and **all sixteen stages run**. A world reports the
stage it actually reached in `completed_stage`, and `systems` lists what ran.
[docs/porting-status.md](docs/porting-status.md) tracks the ladder, the APIs a game calls
after generation, and the open rulings.

## Documents

- [docs/determinism.md](docs/determinism.md) — the float and RNG contract, and what a
  consumer must not do to it.
- [docs/porting-status.md](docs/porting-status.md) — the stage ladder and what each stage
  owes.
- [docs/porting-notes.md](docs/porting-notes.md) — the traps that were expensive to find
  in the reference implementation and are invisible until they are wrong.
- [docs/inherited-findings.md](docs/inherited-findings.md) — the Python project's pre-port
  red team, triaged against this codebase: what still binds, what binds when a stage lands,
  and what died with the rewrite.
- [docs/archive/](docs/archive/) — verbatim documents inherited from the retiring Python
  repository, and a list of what is still only there.

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

Two kinds of pinned number are deliberate and are the only two: the FIPS SHA-256 vectors,
and the RNG golden values in `tests/test_substrate.cpp`. The latter exist because every
other test asserts a *property* of the stream, and a changed constant satisfies all of them
while producing a different planet from every seed. They were cross-checked against an
independent reimplementation rather than copied out of this one.

The suite has been adversarially reviewed once, which found seventeen defects in it and in
the code it covers — including a test that could not fail. See
[docs/inherited-findings.md](docs/inherited-findings.md).
