# generator/: local rules

The world generator: a seed in, a finished world out. It is a dependency-free C++20 static library
(`fwg`) and a CLI (`fwg.exe`). **Lanes:** Claude owns the code. GPT owns the visual fields of the
content data (see "Content data" below). The repository-wide rules are in
[`../AGENTS.md`](../AGENTS.md).

## Build and test

Every command runs from this folder. On this machine `cmake` ships with the VS 2022 Build Tools
and is not on `PATH`; [`README.md`](README.md) gives the path.

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --no-tests=error
```

- The test suite **is** the acceptance mechanism: there is no reference implementation to diff
  against.
- Tests assert properties, and assert that a metric moves when its control does. Pinned numbers
  are deliberate and few (README, "Testing").
- A test written to fail that passes is a broken test.

## The generator knows nothing of its consumers

- No include, link, option, data field, callback or comment in here may exist for the
  orchestrator, communicator, creator, viewer or a game.
- The generator's vocabulary is the world's. The layer checker refuses pacing, dialogue and
  model words in identifiers (`tools/check_layers/layers.txt`), and the red team looks for
  synonyms. Player, intensity, persona, prompt and encounter already mean world things here, and
  stay.
- The generator must build and test with every other folder deleted.

## The public API: `include/fwg/api/` and `api.txt`

- A sibling component includes only `fwg/api/*.hpp` (thin forwarders), writes `fwg::` in full,
  and calls only what [`api.txt`](api.txt) lists.
- Everything else is the generator's own to change: the stage passes, the in-place `World&`
  mutators and the board steps.
- To make a function public, in a generator commit of its own:
  1. add its line to `api.txt` and say why;
  2. add a forwarder in `include/fwg/api/` if its header has none;
  3. add a test.

  The layer checker holds each `api.txt` line to a declaration in the header its forwarder
  includes. It reads the list from the last commit when it checks a consumer, so the first
  caller comes in the next commit.
- `tests/test_api.cpp` includes every forwarder before anything else, and calls only listed
  functions.

## Changing a world

- A verb that changes a world takes `const World&`, returns a new world (alone or inside its
  result), and appends to `history.operations`. `world_api.hpp`, `world_time.hpp` and
  `settlement_api.hpp` are the pattern.
- A read (`read.hpp`) never records an operation, never mutates, never regenerates.
- Contracts carry an `api_version`. A shape a consumer relies on does not change without a bump.

## Determinism

[`docs/determinism.md`](docs/determinism.md) is the contract: the same request, library version,
compiler and C runtime give a byte-identical document.

- Never `/fp:fast`.
- Nothing that is not a function of the seed goes in a document. Timings are excluded unless
  `--timings` is passed.
- A change that should not change worlds proves it: hash the document before and after, at the
  sizes it touches.

## Content data

`data/*.json` is the source of truth. `tools/embed_content.py` turns it into the typed tables in
`src/generated/`, which are checked in, so building needs no Python.

- Never hand-edit `src/generated/`.
- After any data edit, run `python tools/embed_content.py --check` and the tests.
- Data edits can change every world, including edits that look like pure presentation. Say so in
  the commit.
- **GPT's lane** is the visual fields: appearance prose, colours and asset references, in files
  such as `heritage-appearance.json`, `npc-appearance.json`, `key-location-exteriors.json` and
  `world-asset-requirements.json`.
- **Claude's lane** is anything a generation pass reads for numbers, choices or placement.

## Language

C++ only. The one exception is `tools/`, the Python content tooling (declared 2026-09-24). No
other non-C++ source belongs anywhere in this folder.

## The viewer reads `fwg.exe`

`../viewer/` runs this CLI and reads its documents. Changing the CLI's commands or a document's
shape changes what the viewer can show. That is GPT's lane: say so in the commit.
