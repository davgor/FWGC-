# Porting status

The generator is a ladder of sixteen stages. Each stage reads the world the previous ones
built and writes new blocks into it. A world reports the stage it actually reached in
`completed_stage`, and a request above that adds a warning saying so.

**Reached: stage 11 of 16, contiguously and WIRED.** `generate()` at phase 11 produces a
world with terrain, water, climate, magic, settlements and roads in it.

Target is **1:1 functional parity** -- every system the Python generator has. Numeric
output is deliberately not matched; see [determinism.md](determinism.md).

A note on what "done" means in the table below, because the distinction cost a day.
**Wired** means `generate()` calls it and the world document carries the result. **Ported**
means the code exists, is tested, and nothing calls it yet. Stage 10 sat in the second
state while reporting itself in the first: `completed_stage` said 10 and the world had no
cities. Anything marked ported is not in a world until it is wired.

| # | Stage | Reference modules | State |
|---|---|---|---|
| 1 | Plate layout | `terrain_tectonics` | **wired** |
| 2 | Hypsometric surface | `terrain_tectonics` | **wired** |
| 2 | Ocean archipelagos | `terrain_ecology.shape_islands` | **wired** |
| 3 | Surface detail | `terrain_tectonics`, `terrain_globe` | **wired** |
| 4 | Erosion | `terrain_erosion` | **wired** |
| 5 | World scale, land mask, area | `terrain_area` | **wired** |
| 5 | Water: connected ocean, lake spill, receiver graph, rivers | `terrain_water` | **wired** |
| 6 | Tectonic transition: a second epoch | `terrain_history.tectonic_transition` | **wired** |
| 7 | Relic carving: abandoned waterways | `terrain_history.carve_relics` | **wired** |
| 8 | Climate: wind, rainfall, runoff, wetness | `terrain_climate` | **wired** |
| 8 | Terrain labels: temperature, moisture, biome, landform, features | `terrain_biomes` | **wired** |
| 9 | Leyline networks: eight schools, fields, dominance | `terrain_leyline_history`, `terrain_magic` | **wired** |
| 9 | Astrology: the moon, its tide and the almanac | `terrain_astrology` | **wired** |
| 9 | Ecology: habitats, cold relabel, islands, seasons, regions | `terrain_ecology` | **wired** |
| 9 | Biome variants: the magical state of the ground | `terrain_history.add_biome_variants` | **wired** |
| 9 | Continuous height field and tile export | `terrain_detail` | **wired** |
| — | Civilization registry: profiles, habitats, eligibility | `civilization_registry`, `terrain_profiles`, `terrain_civilizations` | **wired** |
| — | Routing and capacity substrate | `terrain_settlements` (searches), `terrain_humans.allocate_access` | **wired** |
| 10 | Settlement scoring, habitation, founding, ceiling, population, classification | `terrain_settlements`, `terrain_humans`, `founding` | **wired** |
| 11 | Roads: the spanning forest between cities | `terrain_settlements` (road pass) | **wired** |
| — | Heritage: naming, genomes, phonology | `Sim/heritage` | ported, not wired |
| — | City and hamlet plans, shapes, fortifications | `city_planner`, `hamlet_planner`, `city_shapes`, `city_fortifications` | ported, not wired |
| 13 | Beast nests | `terrain_nests` | ported, not wired |
| 13 | Religion: gods, cosmologies, faiths | `terrain_religion` | ported, not wired |
| 12 | Populated regions: hamlets, fortresses, colleges, harbours | `terrain_humans`, `terrain_society` | in flight |
| 13 | Beast movement and encounters | `terrain_beast_movement`, `terrain_encounters` | in flight |
| 16 | Nomads | `terrain_nomads`, `terrain_nomad_routes`, `terrain_nomad_effects` | in flight |
| 16 | Villains and corruption | `terrain_villains`, `terrain_corruption` | in flight |
| 16 | Heroes and the story web | `hero_generator`, `story_web` | in flight |
| 16 | NPCs and key locations | `npc_roster`, `key_locations` | in flight |
| 14 | Age transition | `terrain_history.age_transition` | todo |
| 15 | Age transition (final) | `terrain_history.age_transition` | todo |
| 16 | Time, quests, the world clock | `terrain_time`, `terrain_time_schedule`, `terrain_quest_actions` | todo |
| 16 | Wars and visitation | `terrain_wars`, `terrain_visitation` | todo |
| — | Castles | `castle_planner`, `castle_geometry` | todo |

## What the data capture covers

All 39 files under `data/` are extracted from the reference and verified against it,
whether or not the stage that reads them is ported. The Python tree is being abandoned, so
unextracted authored data would be unrecoverable. That includes five sibling packages
outside `icarus_sim` -- heritage, heroes, key locations, NPCs, the story web -- which the
generator imports at generation time and which an audit scoped to `icarus_sim` misses
entirely.

## Known scale defects inherited from the reference

Three systems have now been caught by the same disease, so it is systemic rather than
incidental: several authored distances are absolute metres tuned for the 11.15 km
reference world and do not scale with the planet.

- `maritime` is `exp(-distance_to_ocean / 300 m)`. On a 200 km world the first land cell
  inland reads effectively zero, so `human_maritime`, whose habitat needs 0.3, settles no
  world at all, and 21 authored shore nest species place nothing. Pinned by tests in
  `test_habitation.cpp` and `test_nests.cpp`.
- Of three authored nest distances only the ramp span scales; the clearance and spacing do
  not, so the widest monster clearance can refuse only a settlement's own cell.
- The reference already diagnosed and fixed exactly this for `island_habitat`, whose
  absolute-metre floor had become unreachable, and did not sweep the rest.

Fixing any of these changes every world, so they are pinned rather than corrected.

## Deliberate departures from the reference

Recorded here rather than discovered later.

**The reference's stage numbering bundles more than this one's.** In the reference,
`generate_base` is called for stages 1–5 and internally runs the *entire* downstream chain
— water, climate, settlements, humans, society, nests — through `apply_world_scale`, then
the stage loop discards some of it. Here, stages 1–5 are the tectonic kernel and nothing
else. The two numbering schemes agree on what stages 1–5 *mean* and disagree on what a
stage-5 world *contains*. Once stages 6–16 land, the ladders converge.

**Ocean archipelagos live in `ecology.cpp` but run from the tectonic stage.** The
reference calls `shape_islands` from inside `generate_tectonics` at phase >= 2, and that
ordering is load-bearing: islands must be raised BEFORE surface noise, erosion and
drainage so they are eroded and drained like any other land rather than pasted onto a
finished world. The code sits with the rest of ecology; only the call site is early.

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

## The rest of the pipeline is specified

[docs/port-specs/](port-specs/) holds an implementable specification for every remaining
group — twenty-two of them, extracted from the Python before it was retired and audited
against its source by a second reader. That was the urgent work: twelve of the nineteen
modules behind stages 6–16 had no written specification at all, so the source *was* the
spec and it stops being readable the day that tree goes.

The specifications put the remaining implementation at roughly **50,000 lines of C++**,
excluding tests. The audits found 46 blocking gaps in the first drafts, all recorded with
their missing content in each file's addenda.

## What each remaining stage will need before it can be written

- **Content data now has a pipeline.** `data/*.json` is the source of truth;
  `tools/embed_content.py` validates it and generates typed C++ tables into
  `src/generated/`, which are checked in so the build needs no Python. Run it with
  `--check` before committing a data change. `data/biomes.json` is the first;
  `civilizations.json`, `buildings.json` and the nest profiles follow the same route.
- **The civilization registry has landed.** Thirteen profiles — twelve peoples plus one
  aggregate — with thirty-two traits each, four biome opinion maps, habitat predicates and
  allocation-group eligibility. Verified against the Python registry exactly: zero numeric
  disagreement across every trait, and 1,512 habitat eligibility probes agreeing.
  `fwg registry` dumps it, which is how that comparison was made.
- **Stage 16's planners still need the building catalogue** — structure blocks, staffing
  and the city/hamlet plan libraries. That half of the registry is deliberately not ported
  yet; it is consumed only by the planners and belongs with them.
- **Stage 16** is by far the largest — nine subsystems, and in the reference roughly ten
  thousand lines. It should be sliced, not attempted whole.
- **Hidden schools and `magic_risk_*` are deliberately absent.** The four hidden magic
schools are in the taxonomy and their fields are published, but generation can never raise
one — their occurrence is locked to zero, and only the corruption API places a node in one.
The mechanics that run *once a hidden network has a node* (rot spreading over the node
graph, void biting into every other school, blood and eldritch feeding on cities and nests)
are therefore unreachable at generation and are not ported; they belong with the corruption
API. Likewise `magic_risk_<people>`, which needs the civilization registry.

**Stages 14–15** need the age model. An age is 5000 years; an age turn renumbers most ids
  in the world, so anything that keys on a settlement or plot id across an age boundary is
  wrong. Key on the terrain node.
