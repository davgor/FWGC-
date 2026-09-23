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
| `generate_patch` -- a local metre patch with micro relief; `fwg patch` on the CLI, writing this port's `fwg.patch` document (`write_patch_json`) | `terrain_patch` | **wired** |
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
worlds, as the reference places one in all 33 of its own, 167 in all. Recounted after the
fort rule (ruling 4) moved the hamlets: 171 camps in all (169 before it); geyser basins in 9
of the 33 worlds against the reference's 9 (12 before), and lava tubes in 9 against its 10
(11 before). `tests/test_stages.cpp` pins each rule on the seed-42 size-65 world. The fixture
world placed no natural arch until the fort rule moved its hamlets; it places one now, and
the allowlist entry that excused its absence is gone.

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

**The split export** (this port's own, 2026-09-23): `write_world_split` (`fwg generate
--split DIR`) writes the same world as a directory a game reads in parts -- `world.json`
without the plan rows and the world scene, each plan in its own file under `plans/`, the
scene as `scene.json` -- with each plan block listing its rows' files where the document
carries the rows. A plan file is the document's row less the copies of the world's
catalogues it carries, which `world.json` holds once. `tests/test_split.cpp` writes one world
both ways and holds them equal as parsed JSON, member order included.

## Rulings

Each is pinned by tests, so a change of mind lands as a deliberate change.

1. **A player's founding leaves stage 16 as it stood** (2026-09-23), as the reference does:
   no plan, post or threat report for the new settlement until something re-runs those
   passes; `rebuild: "humans"` drops the hamlet and castle plans and names them in
   `dropped_blocks`.
2. **Corrupted cities** (2026-09-23). A city the age lottery gives to a hidden school --
   which has no fall reason; the reference raised a KeyError -- stands, marked held by that
   school, instead of falling. So does a city a corruption farms (blood) or takes (eldritch),
   where the reference's rebuild erased both effects. The mark is `settlements.sites[].corruption`
   `{school, cause: fallen | farmed | taken, age, corruption}`, and every rebuild carries it.
   **Only a cleanse ends a hold, and cleansing destroys the city**: `cleanse` takes a city
   target, and a corruption cleansed to its last node destroys every city it holds (a partial
   drain destroys none). The city becomes a ruin (cause `cleansed`, no key point); a share of
   its people drawn per city from the world's seed, 10 to 40 percent, gets out as one survivors
   band at the ruin -- raised even with no refuge in reach, the event being its evidence; and
   the band gives a quest line on the cast's hooks under two new verbs, `rebuild` (raise the
   city from its ruins; difficulty 2, 3 or 4 by the city's class) and `return` (lead its people
   to their kin; 2). Bands are quest givers now, present while they stand. The cleanse record
   lists each destroyed city with its people, share, band and hooks.
3. **The world document is `fwg.world` version 2** (2026-09-23), and nothing from the retired
   Python repository is kept here.
4. **Forts are strategic, watch frontiers too, and stand** (2026-09-23). The reference raised
   forts up to its cities plus up to three per city for each city's wars, ranked over every
   road cell in the world with a 200 m spacing that no longer separated anything: about 1.6
   forts a city, piled on a few hubs (one city held 11, 17 of 42 held none). Now a capital, and a
   city that has fought, each hold **one road fort** on the most defensible junction or crossing
   of their own roads; every two civilizations a road joins hold **one frontier fort** on the
   high ground beside the narrowest point of the natural overland route between their lands --
   a pass, a land bridge, a ford -- found where the corridor of routes within 10% of the
   cheapest covers the least ground (`narrowest_crossings`, humans.hpp). Forts are placed before
   the hamlets, so a fort claims the strategic ground and the farms settle around it; the
   spacing is the reference's 200 m scaled with the world, as the support reach is. **A fort
   stands** while the city that garrisons it (`core_uid`) stands and its ground is dry and
   unbuilt, through age turns and every other rebuild, and a new age raises only what its
   cities and frontiers newly ask. Each fort publishes `watch` (road, frontier or player),
   `core_uid` and `raised_age`, a frontier fort also its peoples and corridor width;
   `fortress_demand` keeps the reference's keys with the new rule's meanings and adds
   `capitals`, `frontiers`, `carried`, `road_forts`, `frontier_forts`, `spacing_m` and
   `unanswered`. The humans block is version 10. At seed 99, size 33: 38 forts where there
   were 67, at most 3 on any city.
5. **One storekeeper post, a horse keeper in every city, and an age for every person**
   (2026-09-23). The reference's `store_keeper` (the hamlet food store) and `storekeeper`
   (granary, warehouse, root cellar) are one post, `storekeeper`, and each names the store it
   keeps (`npcs.people[].store`: granary, warehouse, root_cellar, hamlet_food_store) and stands
   in that very building (`site_uid`, `plot_id`); every post names the building it works in as
   its plan placed it (`workplace`, e.g. "Winter supply warehouse"). **Every city keeps
   horses**: every city program of every people queues one "Stable and farrier yard", a core
   building with no condition -- the reference's row waited on `working_animals`, which the
   city planner never sets, so no city built one. Its senior post is a `horse_keeper` (then an
   animal keeper and a farrier), who sells mounts and runs carriages (`npcs.post_services`).
   A city with no room left for the stable's 20 x 29 m plot lists it unplaced, as it does any
   building: at seeds 42, 7, 99 and 1234 (size 129) and 42 (size 257), 228 of 239 planned
   cities placed one, and 9 of the 11 without also lost their granary or market square.
   Hamlets keep barns and farmyards, not stables, and have no horse keeper. **Gender and
   age**: every post and every cast member draws a gender (female or male, even odds) and an
   age band (young, middle, old at 2 : 2 : 1), then a whole year inside the band, from a
   stream keyed on their own uid (`data/npc-appearance.json`). The bands are their people's:
   young from the adult onset to halfway to the elder onset, middle from there to the elder
   onset, old from the elder onset to the longest life, each onset rounded to a whole year
   first. The lifespans are the owner's -- a human lives to 80, an elf to 1000, a dwarf to 500,
   a gnome to 750 -- with the stage onsets scaled in proportion and every other people keeping
   its authored ratio to its race (human_cold 72, human_rainforest 66, hill_dwarf 400, tidekin
   778); the gnome's lifespan tempo is `ancient` now, 750 years lying outside `long`'s 180 to
   520. The block publishes each parent race's and each people's
   life and bands (`npcs.life_spans`) and each person's `appearance` {gender, age_band,
   age_years, civilization_id, parent_race_id}; a person whose people states no life is
   counted in `summary.appearance_unresolved`, never guessed. Hair and every finer choice are
   the game's. The npcs block is version 2. The gnome's appearance text still says gnomes grey
   early, and is to be rewritten.

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
- **Dreads are not in the NPC roster.** The reference folds `heroes.people` and then
  `heroes.dreads` into the roster; this port's adapter (`npc_request`, complete_stage.cpp)
  passes only the people, so a dread has no NPC record, gender or age.
- **Documents are large.** About 157 MB at size 129. At size 33 (60 MB) the plan rows are half
  of it -- their terrain rasters 31%, the copies of the world's catalogues each row carries,
  as the reference's do, 11% -- and the world scene a fifth. The split export (`--split`,
  above) writes each catalogue once and puts the plans and the scene in files of their own; it
  does not make the rasters any smaller.

## Data

`data/*.json` is the source of truth for authored content; `tools/embed_content.py` validates
it and generates typed tables into `src/generated/`, checked in so the build needs no Python.
Run it with `--check` before committing a data change. Every catalogue the generator reads
is extracted here; the Python tree it came from is retired and is not a source for anything.
