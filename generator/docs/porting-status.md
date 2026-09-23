# Porting status

The generator is a ladder of sixteen stages. Each stage reads the world the previous ones
built and writes new blocks into it. A world reports the stage it actually reached in
`completed_stage`, and `systems` lists what actually ran.

**Reached: stage 16 of 16, contiguously and wired.** `generate()` at phase 16 produces a
world with terrain, water, climate, magic, cities and roads, their hinterland, nests,
threats and faiths, two turned ages with their wars, ruins and villains, and every stage-16
system: plans, the world scene, bands and herds, the cast, the story web, the rosters and
the key locations.

Target is **1:1 functional parity** -- every system the Python generator has. Numeric
output is deliberately not matched (a clean rewrite, new worlds); see
[determinism.md](determinism.md).

**Wired** means `generate()` or a World-level API calls it and the world document carries
the result. **Ported** means the code exists and is tested but nothing calls it yet.

## The ladder

| # | Stage | Reference modules | State |
|---|---|---|---|
| 1-5 | Plates, relief, surface detail, erosion, land mask and connected water | `terrain_tectonics`, `terrain_globe`, `terrain_erosion`, `terrain_area`, `terrain_water` | **wired** |
| 2 | Ocean archipelagos | `terrain_ecology.shape_islands` | **wired** |
| 6-7 | Tectonic transition, relic carving | `terrain_history` | **wired** |
| 8 | Climate, terrain labels | `terrain_climate`, `terrain_biomes` | **wired** |
| 9 | Leylines (all twelve schools, hidden-school terms included), astrology, ecology, biome variants, height field | `terrain_leyline_history`, `terrain_magic`, `terrain_astrology`, `terrain_ecology`, `terrain_detail` | **wired** |
| 10 | Founding, population, classification, heritage names, building packs and node-scale city layouts | `terrain_settlements`, `founding`, `heritage` | **wired** |
| 11 | Roads | `terrain_settlements` | **wired** |
| 12 | Hinterland, colleges, seasonal food, society (harbours, transport, economy, witch huts) | `terrain_humans`, `terrain_magic`, `terrain_seasons`, `terrain_society` | **wired** |
| 13 | Nests, threat assessment, religion | `terrain_nests`, `terrain_history`, `terrain_religion` | **wired** |
| 14-15 | Age transitions: villains, wars, fates, ruins, ley drift, departures, refugees, rebuild, promotion, works | `terrain_history`, `terrain_villains`, `terrain_wars`, `terrain_ruins`, `terrain_visitation` | **wired** |
| 16 | City, hamlet and castle plans; world scene; nomads, routes, herds, encounters, nomad effects; heroes, story web, NPCs, key locations and their plans | `city_planner`, `hamlet_planner`, `castle_planner`, `world_scene`, `terrain_nomad*`, `terrain_beast_movement`, `terrain_encounters`, `hero_generator`, `story_web`, `npc_roster`, `key_locations` | **wired** |

## The APIs a game calls after generation

Each takes the world by const reference and returns a new one; each appends to
`history.operations`, one log of every operation since genesis.

| API | Reference | State |
|---|---|---|
| `advance_ages` -- more ages, after player ley edits | `terrain_history.advance_age_request` | **wired** |
| `corrupt`, `cleanse`, `summon`, `depart` | `terrain_corruption`, `terrain_visitation` request functions | **wired** |
| `generate_patch` -- a local metre patch with micro relief | `terrain_patch` | **wired** |
| Time and quests on a World; the liveness predicate | `terrain_time`, `terrain_quest_api`, `terrain_liveness` | **wired** |
| Found a player settlement; raise a band | `terrain_settlement_api`, `terrain_nomad_api` | **wired** |
| Read/select queries (blocks, near, place, person, quests, plus a local patch read the reference lacks); person state | `terrain_read_select`, `terrain_read`, `terrain_person_state`, `terrain_person_api` | **wired** |
| Product package: asset list, capabilities, controls, coordinates, taxonomy, world handles, counter kernel, hero guild, height-tile documents, CLI commands | `fantasy_world_generator`, `terrain_detail.export_height_tile` | **wired** |

## The world document

`write_world_json` writes the reference's published document -- `world_document` in
`fantasy_world_generator/cli.py`, so `build_stages` and `timing_ms` only on request, as there
(`--include-build-stages`, `--include-timings`). This section used to say the writer had been
checked path by path against a reference world. It had not: a structural diff at seed 42,
size 65 on 2026-09-23 found 131 reference key paths the document did not write and 40 it
wrote that the reference does not.

**What is checked now, and how.** `tests/test_document_paths.cpp` generates one request
(seed 10, size 33, phase 16) and requires every normalized key path and layer name of the
reference's published document for that request -- checked in as
`tests/fixtures/reference-world-paths-v1.json`, from FantasyWorldGenerator b67c99b -- in this
port's document, less an allowlist whose every entry gives its reason and must still excuse
something. It pins the key tree, not values: the worlds differ by design. It sees only
structures one of the two worlds populates for that request, so a key that neither world
writes at that seed is unchecked.

**A behaviour difference the document showed, and its cause.** No world of this port placed
a logging camp, so no key location carried `suitability_factors.river`, and geyser basins and
lava tubes (`volcanic`) were suppressed the same way. The passes were right and three of the
adapters feeding them were not, as measured on 2026-09-23:

- *The nest passes kept clear of the cities alone.* The reference's `settled_directions` is
  every city, harbour and hamlet. At seed 42, size 65, this port laired 472 of 3679 monsters
  inside a hamlet's or a harbour's clearance, where the reference's world lairs none, and 332
  of the 365 cells a logging camp qualified for were already held; kept clear of all three,
  225 are.
- *Key locations kept clear of every hamlet, fortress, harbour and college.* The reference
  keeps clear of the settled records that carry a `direction`, and in its world only the
  cities do. On the same world that refused all 140 free cells a logging camp qualified for,
  where the cities alone refuse 33; applied to the reference's own world it would refuse all
  158 of its free cells.
- *Every landmark claimed its cell.* The reference's landmark records carry `x` and `z` and
  no `node`, so they claim nothing. Claiming changed the key-location block of 13 of the 33
  size-33 worlds below and cost the seed-10 fixture world its whaling station.

Across seeds 1-32 and 42 at size 33, the nest fix alone places no logging camp in any world
and neither do the two key-location fixes alone. All three together place one in all 33
worlds, 169 in all, as the reference places one in all 33 of its own, 167 in all; geyser
basins now stand in 12 of the 33 worlds against the reference's 9, and lava tubes in 11
against its 10. `tests/test_stages.cpp` pins each rule on the seed-42 size-65 world. The
fixture world still places no natural arch: that is its ground, and the reference's pass run
over this port's document for that request places none either.

**Blocks the game-facing APIs add.** `world_clock` and `quests` (the time API) and
`settlement_founding` (the founding API's last report) are written once an API has put them
on the world, as the reference's dict gains those keys at that API's first call; their key
sets are pinned by tests beside each API.

**Deliberate departures.**
- `schema` is `fwg.world` version 2: the reference's `fantasy-world-generator.world`
  version-2 key tree under this port's own name, by the owner's choice (2026-09-23); `completed_stage`, `systems` and `seeds` are this port's
  additions, the first read by `tools/preview.py`.
- Stable keys beside the reference's ordinals: `hamlet_uid` and `fortress_uid` (and a
  castle's `node`) on plans and scene entries, which renumber at no age turn.
- `config` and `effective_config` also carry `circumference_m` and `world_size_was_used`, so
  a reader sees which preset set the radius; `population_budget.calibration` also
  carries `per_city_km2`, `resolvable_spacing_m` and `city_limit`; a castle's closed ring says
  whether it was `clipped_to_terrain`.
- A sleeping god's evidence is `last_seen` one level deep, where the reference nests one
  level more at every resolution (religion.hpp).
- Two diagnostic layers, `water_distance` and `height_above_water`, are published that the
  reference computes and discards; the suite reads them.

Per-response keys the reference's JSON APIs add (`corruption`, `cleanse`, `*_api_version`)
are returned as typed results instead.

## Open rulings

Both are pinned by tests, so a ruling lands as a deliberate change.

1. **A hidden school owning a city's ground.** After a corruption, a hidden school can be
   the dominant one under a city. The age lottery gives a city its dominant school's fall
   reason, and only the eight known schools have one: the reference raises a KeyError at
   the next age turn; the port refuses by name
   (`an_age_cannot_yet_turn_over_ground_a_hidden_school_owns`). Author four fall reasons, or
   keep hidden schools out of the lottery.
2. **What a corruption does to survivors.** Blood's farmed population and eldritch's
   `taken_by` mark are wiped by the rebuild the corruption owes, as in the reference, and
   survive only in the corruption record's `touched` list. Keep, or carry them through.

Observed and not changed: a school god's aspect is Wild when its network's mean node
intensity times (1 + instability) exceeds 1.2, and generated intensities average about 2,
so school gods are nearly always Wild -- in the reference as well.

## Known scale defects inherited from the reference

The reference was authored for a world 11.15 km around; the shipped presets are 200 to
600 km. Distances that scale with the planet are fine; many authored metres do not, and
several whole systems consequently barely exist at shipped widths (air routes, irrigation,
hamlet catchment, a lair's reach -- which is why nests almost never destroy a city). The
full enumeration, with measurements, is in [scale-defects.md](scale-defects.md). Nothing is
fixed: each changes every world it touches, so the choice is the owner's. The port pins them
with tests so a fix cannot land silently.

## Deliberate departures from the reference

- **Stable keys where the reference drew on ordinals.** A castle's kit and a hamlet's streets
  are drawn from the settlement's stable uid ("<kind>-node-<n>"), not its list ordinal, which
  renumbers at every age turn.
- **One local sampler.** The city, hamlet and castle planners and the world scene share one
  tangent frame and river source; the reference's copies had drifted (the city read the water
  block's rivers where the others read climate's).
- **An age turn sends home only a god walking by visitation.** The reference sent every
  walking god home, and a god revealed by corruption has no record to send home with, so the
  first age turn after any corruption raised.
- **One ley network-edit check** (`plant_key_point`) for ruins' key points, avatars,
  corruptions and player edits.
- **Numbers, not timings, in the document.** Timings are off unless asked for; two runs of one
  seed are byte-identical.
- **Stage numbering.** Stages 1-5 are the tectonic kernel alone; the reference's `generate_base`
  bundles later passes into them. The ladders agree on what each stage means.

Reference defects the port reproduces are named in the header of the module that reproduces
them, each pinned by a test so a fix is a deliberate change.

## Known open items

- **Determinism is verified on one toolchain.** Only MSVC with the UCRT has built this
  library; the GCC/Clang half of [determinism.md](determinism.md)'s contract has never been
  compiled, let alone compared.
- **No continuous integration.** The suite runs where someone runs it.
- **Documents are large.** Every city, hamlet and castle plan carries its own copy of the
  biome and magic catalogues, as the reference's does: about 155 MB at size 129.

## Data

`data/*.json` is the source of truth for authored content; `tools/embed_content.py` validates
it and generates typed tables into `src/generated/`, checked in so the build needs no Python.
Run it with `--check` before committing a data change. Every catalogue the generator reads
is extracted here; the Python tree it came from is retired and is not a source for anything.
