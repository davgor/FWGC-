# Porting status

The generator is a ladder of sixteen stages. Each stage reads the world the previous ones
built and writes new blocks into it. A world reports the stage it actually reached in
`completed_stage`, and a request above that adds a warning saying so.

**Reached: stage 5 of 16.**

| # | Stage | Reference modules | State |
|---|---|---|---|
| 1 | Plate layout | `terrain_tectonics` | **done** |
| 2 | Hypsometric surface | `terrain_tectonics` | **done** |
| 3 | Surface detail | `terrain_tectonics`, `terrain_globe` | **done** |
| 4 | Erosion | `terrain_erosion` | **done** |
| 5 | World scale, land mask, area | `terrain_area` | **done** |
| — | Ocean archipelagos | `terrain_ecology.shape_islands` | **not ported** (see below) |
| 6 | Tectonic transition (second epoch) | `terrain_history.tectonic_transition` | todo |
| 7 | Relic carving (abandoned waterways) | `terrain_history.carve_relics` | todo |
| 8 | Climate, biomes, ecology, detail | `terrain_climate`, `terrain_biomes`, `terrain_ecology`, `terrain_detail` | todo |
| 9 | Leyline networks, astrology | `terrain_leyline_history`, `terrain_astrology` | todo |
| 10 | Biome variants, civilizations | `terrain_history`, `terrain_settlements` | todo |
| 11 | Civilizations | `terrain_settlements` | todo |
| 12 | Civilizations | `terrain_settlements` | todo |
| 13 | Beast nests, threat, religion | `terrain_nests`, `terrain_religion` | todo |
| 14 | Age transition | `terrain_history.age_transition` | todo |
| 15 | Age transition (final) | `terrain_history.age_transition` | todo |
| 16 | Settlements, nomads, beasts, encounters, heroes, story web, NPCs, key locations | `city_planner`, `hamlet_planner`, `castle_planner`, `terrain_nomads`, `terrain_beast_movement`, `terrain_encounters`, `hero_generator`, `npc_roster`, `key_locations` | todo |

Also outside the ladder and not yet ported: water routing (`terrain_water`), which the
reference runs as part of world scaling rather than as a numbered stage, and
`terrain_society` (ports, sea routes, access allocation).

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

## What each remaining stage will need before it can be written

- **Stage 8** needs the biome taxonomy as data. In the reference this is spread across
  `Contracts/catalogues/` and `civilization_registry.json`; it has to arrive here as a data
  file the library loads or embeds, not as a table hand-copied into a `.cpp`.
- **Stages 10–12 and 16** need the civilization registry and the settlement catalogues. The
  same applies: they are content, and content belongs in a file with a version on it.
- **Stage 16** is by far the largest — nine subsystems, and in the reference roughly ten
  thousand lines. It should be sliced, not attempted whole.
- **Stages 14–15** need the age model. An age is 5000 years; an age turn renumbers most ids
  in the world, so anything that keys on a settlement or plot id across an age boundary is
  wrong. Key on the terrain node.
