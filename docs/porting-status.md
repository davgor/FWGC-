# Porting status

The generator is a ladder of sixteen stages. Each stage reads the world the previous ones
built and writes new blocks into it. A world reports the stage it actually reached in
`completed_stage`, and a request above that adds a warning saying so.

**Reached: stage 5 of 16**, including its hydrology, plus stage 8's climate.

`completed_stage` reports the highest CONTIGUOUS stage, so it stays at 5 while stages 6
and 7 are missing. The `systems` list in the document says what actually ran. Climate is
gated on `phase >= 6` exactly as the reference gates it, and runs on the stage-5 surface
-- the same thing the reference's own `generate_base` path does when called without the
history ladder. A world built that way carries a warning saying so.

Target is **1:1 functional parity** — every system the Python generator has. Numeric
output is deliberately not matched; see [determinism.md](determinism.md).

| # | Stage | Reference modules | State |
|---|---|---|---|
| 1 | Plate layout | `terrain_tectonics` | **done** |
| 2 | Hypsometric surface | `terrain_tectonics` | **done** |
| 3 | Surface detail | `terrain_tectonics`, `terrain_globe` | **done** |
| 4 | Erosion | `terrain_erosion` | **done** |
| 5 | World scale, land mask, area | `terrain_area` | **done** |
| 5 | Water: connected ocean, lake spill, receiver graph, rivers | `terrain_water` | **done** |
| — | Ocean archipelagos | `terrain_ecology.shape_islands` | **not ported** (see below) |
| 6 | Tectonic transition (second epoch) | `terrain_history.tectonic_transition` | todo |
| 7 | Relic carving (abandoned waterways) | `terrain_history.carve_relics` | todo |
| 8 | Climate: wind, rainfall, runoff, wetness | `terrain_climate` | **done** |
| 8 | Terrain labels: temperature, moisture, biome, landform, features | `terrain_biomes` | **done** |
| 8 | Ecology, detail sampler | `terrain_ecology`, `terrain_detail` | todo |
| 9 | Leyline networks, astrology | `terrain_leyline_history`, `terrain_astrology` | todo |
| 10 | Biome variants, civilizations | `terrain_history`, `terrain_settlements` | todo |
| 11 | Civilizations | `terrain_settlements` | todo |
| 12 | Civilizations | `terrain_settlements` | todo |
| 13 | Beast nests, threat, religion | `terrain_nests`, `terrain_religion` | todo |
| 14 | Age transition | `terrain_history.age_transition` | todo |
| 15 | Age transition (final) | `terrain_history.age_transition` | todo |
| 16 | Settlements, nomads, beasts, encounters, heroes, story web, NPCs, key locations | `city_planner`, `hamlet_planner`, `castle_planner`, `terrain_nomads`, `terrain_beast_movement`, `terrain_encounters`, `hero_generator`, `npc_roster`, `key_locations` | todo |

Also outside the numbered ladder: `terrain_society` (ports, sea routes, access
allocation), not ported. `terrain_water` was in this position and is now done — the
reference runs it from inside world scaling, gated on `phase >= 5`, so it belongs to
stage 5 and is placed there.

**Scope check.** Stages 1–5 cover 6 of 140 Python modules and roughly 820 of 28,769
non-test lines — about 2.9%. The owner has ruled for 1:1 functional parity, so the
remaining 134 modules are all in scope. Numerics stay free: worlds need not match, systems
must.

## Deliberate departures from the reference

Recorded here rather than discovered later.

**The reference's stage numbering bundles more than this one's.** In the reference,
`generate_base` is called for stages 1–5 and internally runs the *entire* downstream chain
— water, climate, settlements, humans, society, nests — through `apply_world_scale`, then
the stage loop discards some of it. Here, stages 1–5 are the tectonic kernel and nothing
else. The two numbering schemes agree on what stages 1–5 *mean* and disagree on what a
stage-5 world *contains*. Once stages 6–16 land, the ladders converge.

**Ocean archipelagos are not in the kernel.** The reference calls `shape_islands` from
inside `generate_tectonics` at phase ≥ 2, which puts an ecology module in the middle of the
tectonics module. Here it belongs with stage 8's ecology work, where its dependencies are.
The `ocean_archipelagos` block is absent until then, rather than present and empty.

**Timings are not in the document.** See [determinism.md](determinism.md) §4.

**`x ** 3` is `x*x*x`.** The reference compiles that expression to a libm `pow()` call whose
last bit is a property of the C runtime. See [determinism.md](determinism.md) §1.

## How much written spec survives for the unported stages

The Python repository carried a conformance record — a written acceptance spec — for 26 of
its ~140 modules. Measured across the 19 modules behind stages 6–16:

| Has a record | Source is the only spec |
|---|---|
| `terrain_settlements` (settlement-records) | `terrain_history` |
| `city_planner` (settlement-records) | `terrain_climate` |
| `hamlet_planner` (settlement-records) | `terrain_biomes` |
| `castle_planner` (settlement-records) | `terrain_ecology` |
| `terrain_nests` (creature-placement) | `terrain_detail` |
| `terrain_nomads` (nomads) | `terrain_leyline_history` |
| `terrain_encounters` (encounter-index) | `terrain_astrology` |
| `terrain_time` (time-advance) | `terrain_religion` |
| | `terrain_beast_movement` |
| | `terrain_water` |
| | `terrain_society` |
| | *(and the rest of the 106 uncovered Sim modules)* |

**Seven of nineteen are specified. Twelve are not** — including the whole of stage 8's
climate → biome → ecology chain and stage 9's leylines and astrology.

This decides something about the order of work. For the twelve, **the Python source is the
specification**, and it stops being readable the day that repository is deleted. Either
those stages get ported while it still exists, or the modules get extracted and archived
first. Nothing else in this repository will tell you what `terrain_climate` did.

The eight surviving records total ~3,350 lines under `docs/conformance/` in that tree and
are not copied here. Neither are `Contracts/` — the schemas, the block registry and the
catalogues — which are not documentation but **data stages 8–16 need at runtime**.

## What each remaining stage will need before it can be written

- **Content data now has a pipeline.** `data/*.json` is the source of truth;
  `tools/embed_content.py` validates it and generates typed C++ tables into
  `src/generated/`, which are checked in so the build needs no Python. Run it with
  `--check` before committing a data change. `data/biomes.json` is the first;
  `civilizations.json`, `buildings.json` and the nest profiles follow the same route.
- **Stages 10–12 and 16** need the civilization registry and the settlement catalogues. The
  same applies: they are content, and content belongs in a file with a version on it.
- **Stage 16** is by far the largest — nine subsystems, and in the reference roughly ten
  thousand lines. It should be sliced, not attempted whole.
- **Stages 14–15** need the age model. An age is 5000 years; an age turn renumbers most ids
  in the world, so anything that keys on a settlement or plot id across an age boundary is
  wrong. Key on the terrain node.
