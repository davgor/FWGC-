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
| 10 | Founding, population, classification, heritage names | `terrain_settlements`, `founding`, `heritage` | **wired**, except each city's building pack and `city_layout` plan (in flight) |
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
| Product package: asset list, capabilities, controls, coordinates, taxonomy, world handles, counter kernel, hero guild, CLI commands | `fantasy_world_generator` | **wired** |

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

**Not written yet.**
- `settlements.sites[]` `building_pack_id`, `building_pack_seed`, `building_pack_version`
  and `city_layout`: the stage-10 building packs and the generated city layout (in flight).

**A behaviour difference the document shows.** No logging camp is ever placed, so no key
location carries `suitability_factors.river`; geyser basins and lava tubes (`volcanic`) are
suppressed the same way. The key-location scatter pass finds their ground already held by
lairs: at seed 42, size 65, lairs hold 331 of the 364 cells a logging camp qualifies for,
against 241 of 416 in the reference's world, and across seeds 1-32 and 42 at size 33 no world
of this port places one where every reference world does. The cause is upstream of the
writer, in how much moist and volcanic ground the monster pass claims; it is not diagnosed.

**A value difference it shows.** `lunar_almanac` always reports year 0 from the `epoch`:
`refresh_astrology` fixes the year at zero although its comment says it follows the world's
history. The reference reports `founding.end_year` from stage 10 on, because its
civilization pass refreshes astrology after founding; so here the almanac, the surged
strengths and `magic.lunar_surge` read the founding epoch's sky rather than the reported
year's.

**Deliberate departures.**
- `schema` is `fwg.world` version 1, not `fantasy-world-generator.world` version 2 (the
  capability descriptor says so); `completed_stage`, `systems` and `seeds` are this port's
  additions, the first read by `tools/preview.py`.
- Stable keys beside the reference's ordinals: `hamlet_uid` and `fortress_uid` (and a
  castle's `node`) on plans and scene entries, which renumber at no age turn.
- `config` and `effective_config` also carry `circumference_m` and `world_size_was_used`
  ([inherited-findings.md](inherited-findings.md) #17); `population_budget.calibration` also
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

See [inherited-findings.md](inherited-findings.md) for the reference defects reproduced and
pinned, and [verification.md](verification.md) for how parity was checked.

## Data

`data/*.json` is the source of truth for authored content; `tools/embed_content.py` validates
it and generates typed tables into `src/generated/`, checked in so the build needs no Python.
Run it with `--check` before committing a data change. The Python tree is being abandoned, so
anything not extracted is lost: extraction happens with the port of each module, including
the five sibling packages outside `icarus_sim`.
