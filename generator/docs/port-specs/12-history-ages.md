# Port specification: 12-history-ages

Extracted from the Python `FantasyWorldGenerator` at commit `b67c99b` before that
repository was retired, then audited against the same source by a second reader.

**Source modules**

- `Sim/icarus_sim/terrain_history.py`

**Estimated C++ size:** roughly 3000 lines, excluding tests.

## Audit: NOT YET IMPLEMENTABLE

The spec is unusually strong on the parts it covers — the stage ladder, `capture`/`materialize_stage`, `tectonic_transition`, `carve_relics`, the fate lottery, `age_transition`'s ordering, the RNG stream naming and the ordering hazards are all accurate against the source, and I confirmed the load-bearing external claims it makes (the heapq `(-v,k)` tie-break in `dominant_school`, the `child_seed` digest, `layout_point`'s 13-tuple with owner/conv/div/shear at indices 0/3/4/5, `make_plates` assigning `id = len(plates)` so positional plate indexing is valid, the `'war-'`/`'war-survival-'` domain strings, `CARRIED_SURVIVOR_KEYS` omitting `vassal_of` with no consumer anywhere, and that FWGC++ already has `direction`, `sample`, `measure_globe`, `land_area` and a `sphere_grid` whose neighbour topology matches the Python). But it is not implementable as written. The `terrain` block is described as a five-key block that `add_biome_variants` creates, when in fact that function mutates a block `add_terrain_labels` already wrote, and the exported block also carries `landforms`, `features`, `climate_seed`, `method` and `icons` — a porter following the spec silently drops five exported fields, and cannot build the block at all because the spec never gives the record shape of `natural_catalogue()` or `biome_catalogue()`, both of which `validate_age_world` compares for exact equality. The catalogue's flat index — the entire meaning of the `biome_variant` layer — is described only as "13 biomes crossed with the 8 known schools first", which does not pin the nesting; the source is biome-major (`index = biome_pos*schools_in_block + school_pos`), and a school-major build also lands in 0..103 while renumbering every variant ever recorded. The two exported `threat_assessments` prose strings are elided as `<string>` although the spec quotes the analogous `geological_history.method` verbatim; nearly all of `validate_age_world`'s refusal texts and the entire `terrain_errors` refusal envelope (schema, three-code enum, received/expected/suggestion/retry, the switchable detail tier) are absent, so the age-advance API's failure contract is unrecoverable once the Python is gone. There is also one confident arithmetic error — contributor `distance_m`/`reach_m` are rounded to 4 decimal places, not 6 — and a blanket pole/seam padding claim that is false for most of the layers this module writes.

**3 blocking gap(s)** were found and are recorded in the addenda below.

## Purpose

`terrain_history.py` is the **spine of the recipe-3 world generator**: the only module that knows the sixteen-stage ladder exists, in what order the stages run, and what each one is allowed to write. Everything else in the Python package is a pass that this file calls. It owns four things nothing else owns: (1) the stage driver `generate_history`, which builds the whole world and records a replayable content-delta snapshot per stage; (2) two deep-time geomorphology passes of its own — `tectonic_transition` (a 50-million-year artistic plate epoch) and `carve_relics` (incision of abandoned river beds and drained lake/sea floors); (3) `age_transition`, the civilization mortality engine that runs wars, city fates, leyline drift, villain advancement and a full world rebuild, twice during generation and again on demand; and (4) `add_biome_variants`, which crosses the natural biome raster with the leyline fields to produce the magical biome index. It also carries the externally-facing age-advance request API (`validate_age_world`, `adopt_world`, `advance_age_request`) and `materialize_stage`, the Python counterpart of the lab's browser-side stage reconstruction. The module's own warning, appended to every world, is the honest statement of what it is: *"Recipe 3 history is an artistic simulation: plate motion, city mortality and leyline changes are not calibrated physical or demographic predictions."*

## Where it sits in the pipeline

**Before it can run**, the C++ side must already have everything FWGC++ has through stage 5 plus a great deal it does not: plate layout, hypsometric relief, surface noise, erosion, connected water routing (`add_water`), climate (`add_climate`), terrain labels/biomes (`add_terrain_labels`), ecology/environment (`add_environment`), the twelve-school leyline networks (`generate_networks` / `evaluate_networks`), astrology (`add_astrology`), settlement founding (`add_settlements`), roads, rural hinterland (`add_humans`), magic colleges, seasonal food, world society, beast nests (`add_nests`), religion (`add_religion`), villains, nomads, encounters, and the four external packages (`hero_generator`, `story_web`, `npc_roster`, `key_locations`).

`terrain_history` is the *caller* of all of those. It is therefore the LAST thing to port and the FIRST thing to design around: the order in this file is the contract every other subsystem's spec is written against.

Entry points into it:
- `terrain_lab.generate(cfg)` dispatches to `generate_history(cfg)` when `cfg.world_recipe == 3`. This is the only production path for a recipe-3 world.
- `terrain_area.apply_world_scale` imports `add_biome_variants` for the recipe-0 geometry-experiment path (phase >= 6 with climate). That is the only other consumer of a `terrain_history` symbol during generation.
- `advance_age_request(body)` is the stateless JSON API that advances a persisted world by 1..10 ages.
- `terrain_profile` wraps named passes and reads `STAGES`; `world_debug.build_debug` reads `build_stages`.

**What depends on it afterwards**: `materialize_stage` / `build_stages` is the lab's stage scrubber contract. `STATE_KEYS` is a published list — `docs/conformance/read-surface.md` and `time-advance.md` both assert against it, and `terrain_time` reuses it for its own snapshot logic. `threat_assessments` is read by `city_planner` as `defense_priority`. `ruins` is read by settlement founding (forbidden nodes), `key_locations`, `terrain_nomads` and the eldritch hidden-school feed.

**Conformance record: none.** `docs/conformance/coverage.json` lists `Sim/icarus_sim/terrain_history.py` under `uncovered`, ticketed to `board/in-progress/CONFORMANCE-DOCS.md`. The nearest prose is `docs/terrain-world-layers.md` (sections "Recipe 3: staged construction and two ages", "Deep-time geography", "Civilization ages", "Regional threat assessments"), which is accurate about mechanism but contains one claim the code does not honour — see `open_questions`.

## Inputs

**Config (`terrain_lab.Config`, a frozen dataclass; the fields this module reads directly):**
- `cfg.phase` — int 1..16. How far up the ladder to run. The driver loops `range(1, cfg.phase+1)`.
- `cfg.size` — int 3..1025, grid edge `n`. Recipe 3 age-advance additionally refuses `size > 1025`.
- `cfg.seed` — uint32, master seed for every derived stream.
- `cfg.world_scale` — 0.17744123532462844 by default. Design→physical factor.
- `cfg.tectonic_relief` — **design-space** metres (e.g. 800 on the reference world; 1667/0.17744 = 9394.0 on a 200 km recipe-3 world). Multiplied by `world_scale` inside this module to reach physical metres.
- `cfg.globe_radius` — **design-space** radius (10000 reference). Only reached indirectly, via `layout_point`, which reads it off `cfg`.
- `cfg.magic_enabled` — 0/1. Gates self-destruction fate, ley drift, ley rebuild, and god departure.
- `cfg.settlement_spacing` — **post-scaling physical metres** (450 authored × `reach_scale(circumference)`; 8073 m at the 200 km preset). Passed to `resolve_wars`, `war_outlook` and `add_threat_assessments`.
- `cfg.world_recipe` (3), `cfg.world_options` (JSON string resolved by `terrain_world.options`).

**World options (`terrain_world.options(cfg)`, resolved defaults):**
- `lunar_influence` default 0.5, range 0..1 — how strongly the moon sways the age lottery.
- `war_survival` default 0.0, range 0..1 — chance a defeated city becomes a vassal instead of a ruin.
- `villain_rise` default 0.5, `villain_hold` default 0.7, `villain_density` default 3.0 (regions per villain, 1..12).

**World layers read** (all `n × n` grids of doubles, seam column `n-1` == column 0, rows 0 and `n-1` constant):
- `height` (physical metres, radial above reference sphere), `river` (0/1), `water_type` (0 dry, 1 ocean, 2 lake), `water_depth` (metres ≥ 0) — by `tectonic_transition` and `carve_relics`.
- `ley_<school>` for all twelve schools — by `city_potencies`, `ley_threat_pressure`, `add_biome_variants`. Values are potency ≥ 0, bounded above by each network's `strength` (0..2).
- `lunar_sensitivity` (0..1) — by `city_potencies` when a lunar surge is in force. Absent → treated as 0.
- `natural_biome` (int ids drawn from {0,1,2,3,4,5,6,7,8,13,15,16,17}) — by `add_biome_variants`.
- `instability_<school>`, `coastal_exposure`, `water_type` — indirectly, through `refresh_astrology` → `lunar_sensitivity`.

**World blocks read:**
- `result['effective_config']` — dict of the scaled physical config. Keys used: `globe_radius`, `radius` (TPI neighbourhood), `sea_level`.
- `result['phases']['plates']` — list of `{'id': int, 'center': (x,y,z), 'omega': (x,y,z)}` from stage 1. `omega` is a dimensionless angular-velocity vector: its magnitude is the speed (0.25..1.0), its direction the rotation axis.
- `result['phases']['seeds']['crust']` — uint32 crust noise seed.
- `result['settlements']['sites']` — list of city dicts. Fields this module reads: `uid` (str, stable identity), `id` (**int index into this list — renumbers on every rebuild**), `name`, `node` (int index into `sphere_grid` points), `x`, `z`, `direction` ([x,y,z] unit vector), `height_m`, `population_profile` (civilization key), `civilization_id`, `parent_race_id`, `city_class` ('small'|'medium'|'capital'), `population_estimate`, `source_culture`, `founded_age`, `war_history` (list), `absorbed_refugees`, `absorbed_bands`.
- `result['settlements']['founding']['end_year']` — int, the year founding rounds ended. Drives the default moon day.
- `result['humans']['cores']` — hinterland records keyed by `site_id` (== city `id`, the int index). Fields read: `culture_id`, `food_demand`, `food_supply`, `fortress_ids`.
- `result['roads']['routes']` — list of `{'from': site_id, 'to': site_id, ...}`.
- `result['beast_nests']['sites']` — nest dicts. Fields read: `id` (**str**, `"<kind>-<species_id>-<node>"`), `name`, `direction`, `family`, `tier` (int 1..5, danger band), `layer` ('surface'), `real` (bool — True for `class == 'animal'`), `range_m`, `spacing_m`.
- `result['magic']['networks']` — dict keyed by school name, values `{'nodes': [...], 'edges': [...], 'strength', 'width_m', 'instability', ...}`. Node: `{'id', 'direction', 'intensity'}`. Edge: `{'id', 'from', 'to', 'path', 'intensity'}` where `from`/`to` are positional indices into `nodes`.
- `result['astrology']['moon']` — `{'periods': {'synodic','spin','nod'}, 'offsets': {...}, 'tilt_max_degrees', 'great_year_days'}`.
- `result['villains']['people']` — villain records with `uid`, `region`, `tier`, `reach_m`, `direction`, `seat_uid`, `growth`, `school`, `status`.
- `result['ruins']` — accumulated ruin records (all ages).
- `result['settlement_candidates']`, `result['nomads']['groups']` — read by `absorb_survivor_camps`.
- `result['religion']['gods']` — `status == 'walking'` gods depart at an age turn.

## Outputs

**Layers written by `tectonic_transition` (stage 6), all `n × n`:**
| layer | units / range | meaning |
|---|---|---|
| `height` | physical metres | **replaced** — the post-epoch surface |
| `second_uplift` | physical metres, signed | `new_height − old_height`. **The name lies**: this is the total height delta, not the uplift term alone. |
| `moved_plates` | int plate id, 0..plate_count−1, stored as double | owner plate of each cell after the epoch |
| `second_convergence` | 0..1 | blended convergence at the moved boundaries |
| `second_divergence` | 0..1 | blended divergence at the moved boundaries |
| `pre_tectonic_height` | physical metres | the pre-epoch height, **aliased** to the same list object the epoch read |
| `old_river` | 0/1 | pre-epoch river occupancy |
| `old_water_type` | 0/1/2 | pre-epoch water classification |

Plus, via `measure_surface`: `slope` (degrees), `tpi` (metres), `land` (0/1), and `result['area']` (an `AreaReport`). Plus, via `add_water(phase=5)`: `water_type`, `water_depth`, `water_surface`, `routed_catchment`, `river`, and the `water` block.

**Layers written by `carve_relics` (stage 7):**
| layer | units / range | meaning |
|---|---|---|
| `relic_incision` | physical metres ≥ 0 | depth cut out of `height` at this cell |
| `gorge` | 0/1 | **still-dry** abandoned river bed after the re-route |
| `dry_valley` | 0/1 | **still-dry** former lake/sea floor after the re-route |
| `relic_gorge` | 0/1 | abandoned river bed as first classified — provenance, survives refilling |
| `relic_valley` | 0/1 | drained water footprint as first classified — provenance |

`height` is decremented by `relic_incision`; `slope`/`tpi`/`land`/`area` and the whole water block are recomputed.

**Layers written by `add_biome_variants`:**
| layer | range | meaning |
|---|---|---|
| `biome_variant` | int, −1 or 0..155 | flat index into `terrain.magical_biomes`; −1 = no winning school |
| `biome` | int natural biome id | a deep copy of `natural_biome`. Legacy phenotype ids are retired; `biome` must equal `natural_biome` and `validate_age_world` enforces it. |

**Blocks written:**
- `result['terrain']` — `{'version': 6, 'natural_biomes': natural_catalogue(), 'biomes': natural_catalogue(), 'magical_biomes': biome_catalogue(), 'biome_contract': <string>}`. `natural_biomes` and `biomes` are the *same 13 records*; `magical_biomes` is 156.
- `result['geological_history']` — `{'version': 1, 'elapsed_million_years': 50, 'angular_duration': 0.65, 'plates_before': [...], 'plates_after': [...], 'method': <string>, 'relic_method': <string>}`. `relic_method` is added by `carve_relics`.
- `result['history']` — `{'version': 3, 'ages': [...]}`, created at stage ≤5 and appended to by each age transition. An age entry: `{'age': int, 'events': [ruin copies], 'wars': [war records], 'moon': {...}|None, 'surviving_city_ids': sorted list of uid strings, 'new_city_ids': list of uid strings in site order, 'order': [fixed 9-element list of stage names]}`. `advance_age_request` also writes `history['operations']` (append-only) and `history['replay']` (a string).
- `result['ruins']` — append-only list across all ages. A ruin record is: the subset of `RUIN_KEYS = ('uid','name','node','x','z','direction','height_m','population_profile','civilization_id','city_class','source_culture','founded_age','war_history')` present on the city, plus `id = 'ruin-' + uid` (**the ruin's key field is `id`, a string; the city's `id` was an int**), `kind = 'ruins'`, `destroyed_age`, `asset_id = 'marker.city_ruins'`, and the fate fields `cause`, `reason`, `evidence`, `probability`, `roll`, `new_node_school`, `legacy`.
- `result['threat_assessments']` — `{'version': 3, 'evaluated_after': str, 'age': int|None, 'cities': [reports], 'method': str, 'limits': str}`. A report's key field is `city_uid`; it also carries `site_id` (the renumbering int) and `city_name`. Numeric fields are `round(x, 6)`.
- `result['beast_nests']['evaluated_age']` — int; set to `age-1` for the pre-fate nest pass and to `age` for the post-rebuild one.
- `result['build_stages']` — the snapshot list (see `algorithm` §B).
- `result['recipe']` — `{'version': 3, 'seed', 'overrides', 'parameters', 'resolved', 'provenance'}`.
- `result['generator_version'] = 16`; `result['config'] = asdict(cfg)`; `result['effective_config']['world_recipe'] = 3`, `['phase'] = cfg.phase`; `result['phases'].update(version=2, completed=cfg.phase, titles=STAGES)`; `result['timing_ms']['history_total']` and `['total']`; `result['debug_stats']`; `result['age_api_version'] = 1` and `timing_ms['age_advance_total']` on the advance path.
- Transient internal keys `result['_survivors']` and `result['_age']` are set before `civilization()` and popped immediately after. They are **not** in `STATE_KEYS` and must never appear in an exported world.

## Algorithm

## A. `STAGES` and the driver `generate_history(cfg)`

```
STAGES = ['Plate layout', 'Tectonic relief', 'Surface detail', 'Erosion and sediment',
          'Connected water', 'Second tectonic relief', 'Valleys and gorges', 'Wind and rain',
          'Leylines', 'Founding', 'Roads', 'Populated regions', 'Beasties and animals',
          'Age transition 1', 'Age transition 2', 'Simulation complete']
```
Sixteen entries; `STAGES[stage-1]` is the title of stage `stage`.

```
started = perf_counter()
snapshots = []; previous_layers = {}; previous_state = {}
result = None; old_water = None
for stage in 1 .. cfg.phase:
    terrain_profile.begin(stage, STAGES[stage-1])
    <stage body, below>
    terrain_profile.mark()
    capture(stage)
    terrain_profile.end(result, snapshots)
```
The three profile hooks are no-ops when no profile is open. They must add no random draw and must not reorder anything — a profiled run and an unprofiled run produce byte-identical worlds.

**Stage bodies:**

- **Stages 1–5** (`stage <= 5`): `result = generate_base(replace(cfg, phase=stage))` — i.e. the whole base world is **regenerated from scratch at each of the five stages**, not incrementally extended. Then three fix-ups, in order:
  1. `result.pop('terrain', None)` — the biome catalogue block is not published yet.
  2. `for key in ('temperature','moisture','biome','natural_biome','landform'): result['layers'].pop(key, None)` — `add_terrain_labels` runs unconditionally inside `apply_world_scale` for a tectonic globe at phase ≥ 2, so these exist and must be stripped. **Comment: "Climate/biome inspection belongs to stage 8 in this recipe."**
  3. `result['history'] = {'version': 3, 'ages': []}`; `result['ruins'] = []`.

  Note that `generate_base` → `apply_world_scale` also calls `add_water`, `add_climate`, `add_settlements`, `add_humans`, `add_colleges`, `add_seasonal_food`, `add_world_society` and `add_nests`, but every one of those guards on `cfg.phase` (≥5, ≥6, ≥7, ≥9, ≥6, ≥9, ≥6, ≥7 respectively) and all are inert at phase ≤ 5. A C++ port that runs the base pipeline once and stops at 5 gets the same thing far more cheaply; the Python re-runs only because the stage snapshot needs each intermediate state.
- **Stage 6**: `old_water = tectonic_transition(result, cfg)` — §C. The return value is carried in a local across the loop iteration boundary to stage 7.
- **Stage 7**: `carve_relics(result, cfg, old_water)` — §D.
- **Stage 8**: with `c = replace(cfg, phase=6)`:
  `add_climate(result,c)`; `add_terrain_labels(result,c)`; `add_environment(result,c)`; `result['layers']['natural_biome'] = deepcopy(result['layers']['biome'])`; `attach_detail(result)`.
- **Stage 9**: `generate_networks(result,cfg)`; `refresh_environment(result,cfg)`; `add_astrology(result,cfg)`.
- **Stage 10**: `add_biome_variants(result,cfg)`; `civilization(result,cfg,7)`.
- **Stage 11**: `civilization(result,cfg,8)`.
- **Stage 12**: `civilization(result,cfg,9)`.
- **Stage 13**: `add_nests(result, replace(cfg,phase=9))`; `add_threat_assessments(result,'beast_nests', spacing=cfg.settlement_spacing)` (age is `None` here); `add_religion(result,cfg)`.
- **Stages 14 and 15**: `age_transition(result, cfg, stage-13, final=(stage==15))`. So stage 14 is age 1 and stage 15 is age 2, and `final` is true only at stage 15.
  **Comment, load-bearing:** *"`final` is stage 15 and not `min(15, cfg.phase)`: a phase-14 build must agree with stage 14 of a phase-16 build, which `test_terrain_biome_contract` pins, so a shorter run may not promote at a stage the longer run does not."* A phase-14 build therefore never promotes villains.
- **Stage 16**, in exactly this order: `fill_cities`, `fill_hamlets`, `fill_castles`, `add_nomads`, `add_nomad_routes`, `add_beast_movements`, `add_encounters`, `apply_nomad_effects`, `_attach_heroes`, `_attach_story_web`, `_attach_npcs`, `_attach_key_locations`, `_attach_key_location_plans`.
  **Comment:** *"Bands are part of the world, so they are placed before the packages that only read it: heroes, the story web and the rosters see a world with them in."* `_attach_npcs` runs after `_attach_heroes` because it reads `heroes`; `_attach_key_location_plans` runs after `_attach_key_locations` because it reads that block.

**Finalisation, after the loop:**
```
result['generator_version'] = 16
result['config'] = asdict(cfg)
result['effective_config']['world_recipe'] = 3
result['effective_config']['phase'] = cfg.phase
result['phases'].update(version=2, completed=cfg.phase, titles=STAGES)
result['build_stages'] = snapshots
definitions = registry(3)
resolved = {**asdict(cfg), **options(cfg)}
overrides = {k: resolved[k] for k,v in definitions.items()
             if k != 'seed' and k in resolved and resolved[k] != v['default']}
result['recipe'] = {'version':3,'seed':cfg.seed,'overrides':overrides,'parameters':definitions,
                    'resolved':resolved,
                    'provenance':{k:'override' if k in overrides else 'default' for k in definitions}}
result['warnings'].append('Recipe 3 history is an artistic simulation: plate motion, city mortality '
                          'and leyline changes are not calibrated physical or demographic predictions.')
result['timing_ms']['history_total'] = (perf_counter()-started)*1000
result['timing_ms']['total'] = result['timing_ms']['history_total']   # overwrites the accumulated total
result['debug_stats'] = build_debug(result)
```
**Comment on `overrides`:** *"Authored inputs the request builder consumes (`circumference_km`, `relief_m`) are not config fields, so they never reach the resolved document; their effect appears in the terms they derive."*

## B. `STATE_KEYS`, `capture()` and `materialize_stage()`

`STATE_KEYS` is a 49-entry tuple of top-level world keys that the snapshot machinery tracks, in this exact order:

```
world_scene, terrain_detail, wildlife, city_plans, hamlet_plans, castle_plans, civilizations,
history, ocean_archipelagos, sediment_budget, terrain, water, climate, magic, settlements,
roads, humans, sky, beast_nests, threat_assessments, ruins, villains, heroes, story_web,
npcs, key_locations, key_location_plans, nomads, beast_movements, encounters,
settlement_candidates, world_clock, quests, habitats, regions, seasonal_environment,
population_budget, peoples, population, population_profiles, seasonal_food, fisheries,
transport, world_economy, geological_history, area, astrology, lunar_almanac, religion
```
A block that is not in this list is invisible to the stage scrubber and to the age-advance snapshot. Two keys in the list (`world_clock`, `quests`) are written by `terrain_time`, not by generation.

`capture(stage)` — a closure over `previous_layers` / `previous_state`:
```
layers = {k: deepcopy(v) for k,v in result['layers'].items() if previous_layers.get(k) != v}
state  = {k: deepcopy(result.get(k)) for k in STATE_KEYS if previous_state.get(k) != result.get(k)}
snapshots.append({'stage': stage, 'title': STAGES[stage-1], 'layers': layers,
                  'removed_layers': sorted(set(previous_layers) - set(result['layers'])),
                  'state': state})
previous_layers = {k: (layers[k] if k in layers else previous_layers.get(k)) for k in result['layers']}
previous_state  = {k: (state[k]  if k in state  else previous_state.get(k))  for k in STATE_KEYS}
```
A key that existed and is now `None`/absent enters `state` with value `None`, which is the deletion marker.

**Comment, recording a measured regression:** *"The snapshot above already holds an independent copy of everything that changed, and an unchanged entry still equals the copy kept from the stage that last wrote it. Re-deep-copying every layer and state key here copied the whole world a second time on each of the sixteen stages."* The rebuild of `previous_*` reuses the copy already in `layers`/`state` rather than making a second one. Do not "simplify" this back.

`materialize_stage(world, stage)`:
```
if not 1 <= stage <= len(world['build_stages']): raise ValueError('Stage was not generated')
out = {k:v for k,v in world.items() if k not in STATE_KEYS and k not in ('layers','build_stages')}
out['layers'] = {}
for entry in world['build_stages'][:stage]:
    out['layers'].update(entry['layers'])
    for key in entry['removed_layers']: out['layers'].pop(key, None)
    for key, value in entry['state'].items():
        if value is None: out.pop(key, None)
        else:             out[key] = value
return out
```
Non-state top-level values are **shared, not copied**.

## C. Stage 6 — `tectonic_transition(result, cfg) -> old`

A 50-million-year artistic plate epoch. Returns the pre-epoch `old` dict, which stage 7 consumes.

```
DURATION = 0.65          # dimensionless angular duration of the epoch
```

1. `plates = deepcopy(result['phases']['plates'])`; `original = deepcopy(plates)` (kept for the report).
2. Define Rodrigues rotation about an axis:
   ```
   rotate(p, omega, t):
       speed = sqrt(dot(omega,omega));  axis = omega/speed;  angle = speed*t
       c = cos(angle); s = sin(angle)
       crossed = cross(axis, p); along = dot(axis, p)
       return ( p[i]*c + crossed[i]*s + axis[i]*along*(1-c) )  for i in 0..2
   ```
3. For every plate, in list order: `plate['center'] = rotate(plate['center'], plate['omega'], +0.65)`. The plate's `omega` is unchanged, so its motion is the one it has always had.
4. `old = deepcopy({k: layers[k] for k in ('height','river','water_type','water_depth')})`.
5. Allocate five output field lists in this insertion order: `height, second_uplift, moved_plates, second_convergence, second_divergence`. **The insertion order is load-bearing** — the write below zips the dict's key order against a positional tuple.
6. `crust_seed = result['phases']['seeds']['crust']`; `scale = cfg.world_scale`.
7. For `z` in `0..n-1`, for `x` in `0 .. distinct_count(z,n)-1`:
   ```
   p = direction(x, z, n)                      # unit vector
   record = layout_point(p, plates, crust_seed, cfg)     # the MOVED plates
   owner  = record[0]                          # plate id
   conv   = record[3];  div = record[4];  shear = record[5]
   back   = rotate(p, plates[owner]['omega'], -0.65)     # backtrace along the OWNER's motion
   transported = sample(old['height'], back)   # bilinear sample of the pre-epoch surface
   uplift = scale * cfg.tectonic_relief * (0.9*conv - 0.7*div + 0.18*shear)
   height = 0.25*old['height'][z][x] + 0.75*transported + uplift
   append to rows: (height, height - old['height'][z][x], owner, conv, div)
   ```
   **`plates[owner]` is indexed positionally by the plate id.** `layout_point` internally sorts plates by `id` and returns `ordered[owner_index]['id']`, so the id is also the position in the original list; the two agree only because `make_plates` assigns `id = len(plates)` in creation order. Preserve that invariant or index by id explicitly.
8. Row padding, identical to every other raster in this codebase:
   `rows[key] * n` if `z` is a pole row (the row holds one value), else `rows[key] + [rows[key][0]]` (close the seam).
9. `layers.update(fields)`; `layers['pre_tectonic_height'] = old['height']` (**an alias, not a copy**); `layers['old_river'] = old['river']`; `layers['old_water_type'] = old['water_type']`.
10. Write `result['geological_history']`:
    ```
    {'version': 1, 'elapsed_million_years': 50, 'angular_duration': 0.65,
     'plates_before': original, 'plates_after': plates,
     'method': '50-million-year artistic epoch: rotate original plate centers, backtrace crust, '
               'blend transported relief and new boundary uplift. Angular speeds are dimensionless, '
               'not calibrated geodynamics.'}
    ```
11. `measure_surface(result,cfg)`; `add_water(result, replace(cfg,phase=5))`; return `old`.

**Scale note (critical).** `layout_point` and `relief_at` work in **design space**: they use `cfg.globe_radius` (10000) and `cfg.belt_width` to compute boundary distances, and they return dimensionless convergence/divergence/shear in 0..1. The `uplift` term converts to physical metres by multiplying `cfg.tectonic_relief` (design) by `cfg.world_scale`. `old['height']` and `transported` are already physical metres (stage 1–5 output was scaled by `apply_world_scale`). The blend is therefore dimensionally consistent, but only because of that one `scale *` factor. In FWGC++, where the kernel runs in design space and scales once at the end, this pass runs **after** scaling and must apply the factor itself.

**Constant meaning.** `0.25 * old + 0.75 * transported` is an artistic memory term: three quarters of the relief rides with the plate, one quarter stays where it was, so old coastlines ghost through the new ones instead of vanishing. `0.9 conv − 0.7 div + 0.18 shear` builds mountains at collisions, opens basins at rifts and adds a weak roughening at transforms.

## D. Stage 7 — `carve_relics(result, cfg, old)`

`abandoned_waterways(old, new)` — the classifier, used twice:
```
gorges[z][x]  = int( bool(old['river'][z][x]) and not new['river'][z][x] and new['water_type'][z][x] == 0 )
valleys[z][x] = int( bool(old['water_type'][z][x]) and new['water_type'][z][x] == 0 )
```
A **gorge** is a cell that carried a river and is now both riverless and dry. A **valley** is a cell that carried *any* water — ocean (1) or lake (2) — and is now dry. Note the asymmetry: `valleys` includes drained ocean floor, which is why its cut ceiling is 100 m rather than 45.

Then:
1. `gorges, valleys = abandoned_waterways(old, layers)` using the post-epoch, post-reroute water.
2. `points, _, graph = sphere_grid(cfg.size, effective_config['globe_radius'])`.
3. Per-node cut depth, **iterating `points` in node order** (`(0,0)`, then every `(x,z)` for `z` in 1..n−2 and `x` in 0..n−2, then `(0,n-1)`):
   ```
   cuts[i] = min(45.0,  max(4.0,  cfg.tectonic_relief * cfg.world_scale * 0.12))   if gorges[z][x]
           = min(100.0, max(12.0, old['water_depth'][z][x] * 0.6))                 elif valleys[z][x]
           = 0.0                                                                    otherwise
   ```
4. Shoulder pass — **one simultaneous sweep, not in-place**:
   ```
   cuts = [ max(cuts[i], max((cuts[j]*0.25 for j,_ in graph[i]), default=0.0)) for i in range(len(points)) ]
   ```
   Every `cuts[j]` on the right reads the *pre-shoulder* array. In C++ this is `new_cuts[i] = max(old_cuts[i], 0.25 * max over neighbours of old_cuts[j])`, computed into a second buffer.
   **Comment:** *"Small shoulders make former river beds visible as terrain, not just marker pixels."*
5. `layers['relic_incision'] = node_grid(cuts, points, n)`.
6. `layers['height'][z][x] -= layers['relic_incision'][z][x]` for every cell.
7. `measure_surface(result,cfg)`; `add_water(result, replace(cfg,phase=5))` — the world is re-routed with the beds cut in, so some of them refill.
8. `layers['gorge'], layers['dry_valley'] = abandoned_waterways(old, layers)` — recomputed against the **new** water. **Comment:** *"Refilled cuts are historical features; only still-dry abandoned beds are current gorges/valleys."*
9. `layers['relic_gorge'] = gorges`; `layers['relic_valley'] = valleys` — the first classification, kept as provenance.
10. `result['geological_history']['relic_method'] = 'Compare old and rerouted river occupancy and dry former water footprints. Incise beds/shoulders, then reroute again; refilled features retain relic provenance.'`

## E. `measure_surface(result, cfg)`
```
physical = result['effective_config']
layers['slope'], layers['tpi'] = measure_globe(layers['height'], physical['globe_radius'], physical['radius'])
layers['land'] = [[int(h > physical['sea_level']) for h in row] for row in layers['height']]
result['area'] = land_area(layers['height'], physical['globe_radius'], physical['sea_level'])
```
All three are pure functions of `height` plus scalars. FWGC++ already has `measure_globe` (measure.hpp) and `land_area` (world.hpp).

## F. `add_biome_variants(result, cfg)`
```
catalogue = biome_catalogue()                    # 156 entries
index = {e['id']: i for i,e in enumerate(catalogue)}
for z in 0..n-1, for x in 0..n-1:
    potencies = {s: layers['ley_'+s][z][x] for s in SCHOOLS}      # all TWELVE schools
    school = dominant_school(potencies)                           # threshold .35, margin .08
    biome_variant[z][x] = index[ NATURAL_BIOMES[natural_biome[z][x]] + '.' + school ] if school else -1
result['terrain']['version'] = 6
result['terrain']['natural_biomes'] = natural_catalogue()
result['terrain']['biomes']         = natural_catalogue()        # the SAME 13 records
result['terrain']['magical_biomes'] = catalogue
result['terrain']['biome_contract'] = ('biome and natural_biome are natural catalogue IDs, never array '
    'offsets; biome_variant is a magical catalogue index or -1. Food and habitat rules use the natural '
    'core and explicit variant ID with independent school risk.')
layers['biome'] = deepcopy(layers['natural_biome'])
```
`dominant_school(p, threshold=.35, margin=.08)` returns the top school iff `p[top] >= 0.35` and `p[top] − p[runner_up] >= 0.08`, else `None`. Its implementation uses a partial selection (`heapq.nsmallest(2, ((-v,k) ...))`) — ties on potency are broken by **ascending school name**, because `(-potency, name)` tuples compare that way. This runs once per cell and only the top two of twelve are ever read.

**Catalogue layout.** The 13 natural biomes, in file order with their ids: `0 ocean, 1 tundra, 2 desert, 3 grassland, 4 forest, 5 exposed_rock, 6 snow, 7 rainforest, 8 lake, 13 marsh, 15 boreal_forest, 16 cold_tundra, 17 land_ice`. (Ids 1 and 6 are tombstoned as unreachable but stay in the catalogue.) The 12 schools in order: the eight known — `weave, umbral, infernal, radiant, fire, water, earth, air` — then the four hidden — `blood, void, rot, eldritch`. `biome_catalogue()` emits **all 13 biomes crossed with the 8 known schools first (indices 0..103), then all 13 crossed with the 4 hidden (104..155)**, so a `biome_variant` recorded before the hidden schools existed still means what it meant. Entry id is `"<core>.<school>"`, e.g. `"desert.umbral"`.

## G. `refresh_environment(result, cfg)`
```
density = layers.pop('magic_density', None)
add_terrain_labels(result, replace(cfg, phase=6))
if density is not None: layers['magic_density'] = density
add_environment(result, replace(cfg, phase=6))
layers['natural_biome'] = deepcopy(layers['biome'])
```
The `magic_density` dance exists because `add_terrain_labels` would otherwise consume or clobber it; it is pulled out, the labels are rebuilt, and it is put back before ecology runs.

## H. `civilization(result, cfg, phase=9)` and `remember_cities(result, age)`
```
c = replace(cfg, phase=phase)
add_settlements(result, c)
if phase >= 9:
    add_humans(result,c); add_colleges(result,c); add_seasonal_food(result,c); add_world_society(result,c)
remember_cities(result, result.get('_age', 0))
if 'astrology' in result: refresh_astrology(result, cfg)
```
So stage 10 (`phase=7`) founds cities only, stage 11 (`phase=8`) adds roads (inside `add_settlements`), stage 12 (`phase=9`) adds the rural/institution layer.

```
remember_cities(result, age):
    cores = {c['site_id']: c for c in result.get('humans',{}).get('cores', [])}
    for city in result['settlements']['sites']:          # sites order
        if 'uid' not in city and age > 0: city['name'] += f' (Age {age})'
        city.setdefault('uid', f"surface-city-{age}-{city['node']}-{city['population_profile']}")
        city.setdefault('founded_age', age)
        if city['founded_age'] == age and city['id'] in cores:
            city['source_culture'] = cores[city['id']]['culture_id']
        city.setdefault('source_culture', city['population_profile'] + '-founders-' + str(city['node']))
```
A survivor already carries `uid`, `founded_age`, `name` and `source_culture` (they are in `terrain_settlements.CARRIED_SURVIVOR_KEYS` and are copied onto the rebuilt site that lands on the same `node` with the same `population_profile`). So the rename and the uid mint apply only to genuinely new cities. During generation stages 10–12 `_age` is absent, so `age = 0` and uids are `surface-city-0-<node>-<profile>`.

## I. Threat assessment chain

`fantasy_nest_threats(city, nests, radius)` — standing monster pressure, shared by the age fate and the threat report:
```
for nest in sorted(nests, key=lambda n: n['id']):          # STRING sort of nest ids
    if nest.get('layer','surface') != 'surface': continue
    if nest.get('real', True): continue                    # real animals never threaten
    dragon = 'dragon' in nest.get('name','').lower()
    if not (dragon or nest.get('family') in ('infernal','undead','aberrant')): continue
    distance = radius * acos(clamp(dot(city['direction'], nest['direction']), -1, 1))
    reach    = min(radius*0.5, max(350.0, nest.get('range_m', nest.get('spacing_m', 350.0)) * 2))
    if distance > reach: continue
    weight = 0.11 * nest.get('tier', 5) * (1 - distance/reach)
    emit {'kind': 'dragon' if dragon else 'monster', 'weight': weight,
          'reason': nest['name'] + ' drove the inhabitants away.',
          'nest_id': nest['id'], 'name', 'family', 'distance_m': distance, 'reach_m': reach}
```
**Docstring, carried verbatim in intent:** *"Weight scales with the danger tier, so a greater lair at the gates is the reason a city falls and a tier one nuisance is not; tier five keeps the flat .55 the single fixed weight used to give every monster alike."* `0.11 × 5 = 0.55` is the historical single weight; the tier grading was added under it so tier 5 is unchanged.

`city_potencies(city, layers, lunar=None)`:
```
potencies = {s: layers['ley_'+s][city['z']][city['x']] if 'ley_'+s in layers else 0.0 for s in SCHOOLS}
if lunar:
    factors, influence = lunar
    sway = layers['lunar_sensitivity'][city['z']][city['x']] if present else 0.0
    for s in KNOWN_SCHOOLS:                      # the eight, NOT the twelve
        potencies[s] *= (1 + influence * sway * (factors[s] - 1))
```
**Comment:** *"Only the known schools sway: the tide is charted against this world's moon, and a school the world has never named was never in that chart."* And: *"The sway is momentary: it scales what the fate sees, never the exported field. Same association order as `Core/ages.cpp`."*

`lunar_context(result, cfg, day=None)`:
```
if 'astrology' not in result: return (None, None)
if day is None: day = int(result['settlements']['founding'].get('end_year', 0)) * DAYS_PER_YEAR   # 360
moon = result['astrology']['moon']; state = day_state(moon, day)
record = {'day', 'phase_fraction', 'elongation_degrees', 'illumination', 'leaning', 'tide'}   from state
return ( (tide(moon, day), options(cfg)['lunar_influence']), record )
```

`ley_threat_pressure(city, layers, lunar=None)`:
```
potencies = city_potencies(city, layers, lunar)
winner = dominant_school(potencies)
if not winner: return 0.0, None
return min(0.6, potencies[winner] * 0.45), {'school': winner, 'potency': potencies[winner]}
```
The 0.6 cap is a hard ceiling on what magic alone can contribute.

`assess_city_threat(city, layers, nests, radius, outlook=None)`:
```
nests = fantasy_nest_threats(city, nests, radius)
nest_pressure = sum(t['weight'] for t in nests)
ley_pressure, ley = ley_threat_pressure(city, layers)        # NO lunar argument here
fought = veteran_wars(city) = len(city.get('war_history') or ())
war_pressure = min(0.4, 0.15 * fought)
contributors = [one dict per nest threat, rounded to 6dp]
if ley: contributors.append({'kind':'ley','weight':round(ley_pressure,6), **ley})
if war_pressure: contributors.append({'kind':'war','weight':round(war_pressure,6),'wars_fought':fought})
outlook = outlook or {'wars_recent':0,'enemy_living':False,'war_risk':0.,'war_risk_kind':None,
                      'war_risk_opponent_uid':None,'war_hunger':0.,'war_hunger_target_uid':None}
return {'city_uid', 'city_name', 'site_id',
        'regional_threat': round(min(1.0, nest_pressure + ley_pressure + war_pressure), 6),
        'nest_pressure','ley_pressure','war_pressure','wars_fought',
        'wars_recent','enemy_living','war_risk','war_risk_kind','war_risk_opponent_uid',
        'war_hunger','war_hunger_target_uid','contributors','ley'}
```
**Comments:** *"A city that has already been to war expects the next one. This is why a veteran builds heavier walls: the city planner reads `regional_threat` as `defense_priority`."* And: *"Version 3 adds the forward-looking war outlook beside the backward-looking pressure. The regional threat the city planner reads is unchanged: a forecast does not build walls yet."*

`add_threat_assessments(result, evaluated_after, age=None, spacing=None)`:
```
radius  = result['effective_config']['globe_radius']
nests   = result.get('beast_nests',{}).get('sites',[])
cities  = sorted(result['settlements']['sites'], key=lambda s: str(s.get('uid', s['id'])))
spacing = spacing if spacing is not None else result.get('config',{}).get('settlement_spacing')
villains = [v for v in standing(result.get('villains',{}).get('people',[])) if v.get('reach_m')]
outlook  = war_outlook(cities, humans.cores, roads.routes, radius, spacing, age, villains) if spacing else {}
reports  = [assess_city_threat(city, layers, nests, radius, outlook.get(city['uid'])) for city in cities]
result['threat_assessments'] = {'version': THREAT_ASSESSMENT_VERSION (=3), 'evaluated_after': evaluated_after,
                                'age': age, 'cities': reports, 'method': <string>, 'limits': <string>}
```
**Comment on the villain filter:** *"`reach_m` is refreshed before the fall is decided, so a fallen villain still carries a live-looking reach and would feed the war outlook without the standing filter."*

## J. `city_fate(city, layers, nests, radius, seed, age, roll=None, magic_enabled=True, lunar=None, villains=())`

Builds an **ordered** list of causes, each `(kind, weight, reason, evidence_dict)`:
1. **Ley**, if `dominant_school` names one: `(school_name, min(0.6, potency*0.45), LEY_DESTRUCTION[school], {'school','potency'})`. Computed *with* the lunar sway.
2. **Nests**, one per eligible threat, in nest-id string order: `(kind, weight, reason, {'nest_id','family','distance_m','reach_m'})`.
3. **Self-magic**, only when `magic_enabled`: `('self_magic', 0.035 + 0.06*potencies['weave'], 'The city destroyed itself in a magical experiment, leaving a new Weave key point.', {})`.
4. **Villains**, appended last via `terrain_villains.causes(villains, city, radius)`.
   **Comment:** *"Appended last, like the divine cause: the selector walk below is order-sensitive, so a villain may extend the list but must never insert into it."*

Then the lottery:
```
chance   = min(0.9, sum(weights))
rng      = random.Random(child_seed(seed, 'city-fate-' + city['uid'], age))
draw     = rng.random() if roll is None else roll          # exactly one draw, always
if draw >= chance or not causes: return None               # the city survives
selector = (draw / chance) * sum(weights)                  # uncapped sum, inverse CDF
chosen   = causes[-1]
for cause in causes:
    selector -= cause[1]
    if selector <= 0: chosen = cause; break
legacy   = ruin_legacy(city, chosen[0], potencies, nest_family=chosen[3].get('family'),
                       villain_school=chosen[3].get('school'))
evidence = dict(chosen[3])
if lunar and chosen[0] in KNOWN_SCHOOLS: evidence['moon_tide'] = lunar[0][chosen[0]]
return {'cause': chosen[0], 'reason': chosen[2], 'evidence': evidence, 'probability': chance,
        'roll': draw, 'new_node_school': legacy['school'], 'legacy': legacy}
```
The `chosen = causes[-1]` default guards floating-point shortfall in the walk. Note the draw is consumed even when the city survives, so the stream advances identically for every city.

`LEY_DESTRUCTION` — the eight reason strings, one per known school:
- weave: "An arcane surge tore the city apart."
- umbral: "Necrotic entropy extinguished the city."
- infernal: "Infernal corruption and demonic incursions drove its people away."
- radiant: "An overwhelming radiant surge forced the city to be abandoned."
- fire: "A fire leyline engulfed the city in flames."
- water: "A water leyline drowned or froze the city."
- earth: "An earth leyline let roots and stone reclaim the city."
- air: "An air leyline shattered the city with storms and force."

(The four hidden schools have no entry, so a hidden-school ley cause would raise `KeyError`. Generation never raises one; only the corruption API can.)

`ruin_legacy` (in `terrain_ruins`, needed here to know what `new_node_school` means): the school is the destroyer's own if it has one (`cause in SCHOOLS` → itself; `self_magic` → `weave`; `divine*` → the god's; `villain*` → the villain's, possibly `None`; `war_*` → the victor culture's `magic_school` trait; a nest family → `NEST_FAMILY_SCHOOL = {infernal→infernal, undead→umbral, aberrant→weave, fey→weave, holy→radiant, unholy→infernal, draconic→fire, primordial→<strongest of fire/water/earth/air at the ruin, ties by school order>, fantastic→None}`), else `dominant_school(potencies)` (basis `'region'`), else the ruined culture's own school defaulting to `weave` (basis `'culture'`). Intensity is `{'small':1.5,'medium':2.5,'capital':3.5}[city_class]`, floored at 2.5 for `self_magic`, forced to 4.0 for a divine cause.

## K. `age_transition(result, cfg, age, moon_day=None, final=False)` — the ordered algorithm

The published order, recorded verbatim in each age record's `order` field:
`['nests before fates','wars','city fates','ruins and hamlet removal','leyline update','biomes','civilization','nests','threat assessment']`

1. `add_nests(result, replace(cfg, phase=9))`; `result['beast_nests']['evaluated_age'] = age - 1`.
   Nests are re-evaluated **before** any fate so every age judges cities against current habitat and settlement clearance.
2. `cities = deepcopy(result['settlements']['sites'])` — the working copy everything below mutates. `ruins = result.setdefault('ruins', [])`; `events = []`; `survivors = []`; `radius = effective_config['globe_radius']`.
3. `lunar, moon = lunar_context(result, cfg, moon_day)`.
   **Comment:** *"The moon on the day the age turns. A momentary surge scales every potency the lottery reads; the fields themselves are not rewritten."*
4. `o = options(cfg)`; `villains = advance_villains(result, cfg, age, o['villain_rise'], o['villain_hold'], o['villain_density'])`.
   **Comment:** *"The cast advances on the assessment this age inherited, so a villain is always one rebuild behind the region it is eating, and can never be raised by its own damage."*
5. **Wars first.**
   ```
   wars, war_fates = resolve_wars(cities, humans.cores, roads.routes, layers, radius,
                                  cfg.settlement_spacing, cfg.seed, age,
                                  magic_enabled=bool(cfg.magic_enabled),
                                  survival=float(options(cfg).get('war_survival', 0.)),
                                  villains=villains)
   ```
   **Comment:** *"Wars are settled before any other fate. They are decided between cities out of the ground and supply the finished age left them, and a city a neighbour has already taken cannot also be eaten by a dragon; its ruin records the war that ended it."*
6. `by_uid = {city['uid']: city for city in cities}`. For each war, for each uid in `war['participants']` (a two-element list `[a_uid, b_uid]`): `by_uid[uid].setdefault('war_history', []).append(participation(war, uid))`. If `war.get('outcome') == 'vassalage'`: `by_uid[war['defeated_uid']]['vassal_of'] = war['victor_uid']`.
7. **Per-city fate**, iterating `cities` in list order:
   ```
   fate = war_fates.get(city['uid'])
   if fate:
       legacy = ruin_legacy(city, fate['cause'], city_potencies(city, layers, lunar),
                            victor_culture=fate['evidence'].get('opponent_civilization_id'))
       fate = {**fate, 'new_node_school': legacy['school'], 'legacy': legacy}
   else:
       fate = city_fate(city, layers, beast_nests.sites, radius, cfg.seed, age,
                        magic_enabled=bool(cfg.magic_enabled), lunar=lunar, villains=villains)
   if not fate: survivors.append(city); continue
   ruin = {k: deepcopy(city[k]) for k in RUIN_KEYS if k in city}
   ruin.update(id='ruin-'+city['uid'], kind='ruins', destroyed_age=age,
               asset_id='marker.city_ruins', **fate)
   ruins.append(ruin); events.append(deepcopy(ruin))
   ```
   **Comment:** *"The victor's own magic scars the ground it took."* Note that the war branch **overwrites** the `new_node_school` that `resolve_wars` already computed (via `_key_point_school`, which can be `None`) with `ruin_legacy`'s, which always resolves to some school. A war ruin therefore always seeds a key point; a `_key_point_school` of `None` is discarded.
8. **Leyline evolution — only after every death is decided.** **Comment:** *"Death decisions all use the pre-transition fields. Only then evolve the ley inputs."*
   ```
   if cfg.magic_enabled:
       for name, net in result['magic']['networks'].items():          # SCHOOLS insertion order
           rng = random.Random(child_seed(cfg.seed, 'age-ley-'+name, age))
           for item in net['nodes'] + net['edges']:                   # nodes first, then edges, in list order
               item['intensity'] = min(4.0, item['intensity'] * rng.uniform(0.65, 1.35))
       for ruin in events:                                            # ruin order == cities order
           if ruin['new_node_school']:
               name = ruin['new_node_school']
               result['magic']['networks'][name] = edit_network(
                   result['magic']['networks'][name],
                   new_node={'id': ruin['id'] + '-key', 'direction': ruin['direction'],
                             'intensity': ruin.get('legacy', {}).get('intensity', 2.5)})
       if 'religion' in result:
           for god_id in walking_gods(result): depart_god(result, cfg, god_id, age, rebuild=False)
   ```
   New key points are added **after** the drift, so a ruin's node is not immediately perturbed. `edit_network` is copy-on-write: it deep-copies the network, validates the new node (unique non-empty string id, finite 3-vector with `|len²−1| ≤ 1e-6`, intensity finite in 0..4) and appends it. **Comment on god departure:** *"A god still walking the world goes home when the age turns; its footprint stays."*
9. `absorb_survivor_camps(result, survivors, age)`.
   **Comment:** *"A refugee band that reached a standing town is absorbed by it rather than founding a settlement on top of it: the candidate names the refuge's own node, not free ground. Before the rebuild, on the cities that survived the boundary, so a refuge that fell this age takes nobody in. Writes nothing on a generated world — `settlement_candidates` is written at stage 16, after both generation-internal age transitions."*
10. `rebuild_tail(result, cfg, survivors, 'age', age, evaluate=bool(cfg.magic_enabled))` — §L.
11. `if final: promote_villains(result, cfg, age, o['villain_density'])`.
    **Comment:** *"The simulation ends with promotions. Accumulation alone cannot seat anybody inside a generation — two age transitions against a rise the legal range tops out below — so the last age is where the world names its antagonists. Placed before the works pass so a promoted villain sinks its well and stakes its claims in the same age it rises, exactly as an accumulated one does."*
12. `if 'villains' in result:` build the works on settled ground:
    ```
    works = build_villain_works(result, cfg, age, standing(result['villains']['people']))
    if works:
        evaluate_networks(result, cfg); add_biome_variants(result, cfg)   # a well added ley nodes
        result['villains']['works'] = works
    result['villains']['outlook'] = villain_outlook(result, cfg, o['villain_rise'], o['villain_density'])
    ```
    **Comment:** *"Built after the rebuild, on settled ground: a well joins the holdings a villain actually has this age, and a claim names the peoples actually near it."*
13. Record the age:
    ```
    old_ids = {s['uid'] for s in survivors}
    new = [s['uid'] for s in result['settlements']['sites'] if s['uid'] not in old_ids]
    result['history']['ages'].append({'age': age, 'events': events, 'wars': deepcopy(wars), 'moon': moon,
                                      'surviving_city_ids': sorted(old_ids), 'new_city_ids': new,
                                      'order': [...the nine-element list above...]})
    ```

## L. `rebuild_tail(result, cfg, survivors, label, age, *, evaluate=True, religion=True)`

**Docstring, carried:** *"The tail an age transition and a visitation both run: fields, biomes, civilization, nests, threats, sky. One body, two callers, so the two cannot drift. `evaluate` skips the ley rebuild when magic is off; `religion` re-resolves the pantheon, which an age turning does and a visitation does not, because a visitation resolves religion itself once its own records are written. The threat pass takes the spacing explicitly from the config it was built with, rather than one caller passing it and the other letting the pass look it up."*

```
if evaluate: evaluate_networks(result, cfg)
refresh_environment(result, cfg)
add_biome_variants(result, cfg)
result['_survivors'] = survivors; result['_age'] = age
civilization(result, cfg)                      # phase 9: settlements, humans, colleges, food, society
result.pop('_survivors', None); result.pop('_age', None)
add_nests(result, replace(cfg, phase=9))
result['beast_nests']['evaluated_age'] = age
add_threat_assessments(result, label, age, spacing=cfg.settlement_spacing)
if religion:
    if 'astrology' in result: add_religion(result, cfg)
else:
    refresh_astrology(result, cfg)
```
**Comment:** *"All dependent institutions/roads/food are rebuilt from active cities, so no dead-city hamlets survive."*

The `_survivors` / `_age` handshake is how `add_settlements` learns it is re-founding rather than founding: it reads `result['_survivors']` to seed the founding pass with standing cities, forbids every `ruins[i]['node']`, resumes `used_civilizations` and `diaspora_bonus_used` from the previous founding report, and sets `start_year = previous end_year + years_per_round`. It then matches each new site to `previous[site['node']]` (previous = `_survivors` if present, else the current sites list) and, when the profile matches, copies `CARRIED_SURVIVOR_KEYS = ('uid','source_culture','founded_age','name','war_history','absorbed_refugees','absorbed_bands','founded_by')` plus a player-authored `city_layout`.

## M. The five external attachments (stage 16 and the last age of an advance)

Each has the same shape: pop the block, call `attach(result)` from a separate top-level package, and store the result only if it is not `None`. A raising revision yields a `{'status': 'failed'}` block rather than an exception; an environment switch (`FANTASY_WORLD_HEROES=0`, `FANTASY_WORLD_STORY_WEB=0`, `FANTASY_WORLD_NPCS=0`, `FANTASY_WORLD_KEY_LOCATIONS=0`) leaves the key absent entirely.
- `_attach_heroes` → `hero_generator.attach` → `heroes`.
- `_attach_story_web` → `story_web.attach` → `story_web` (reads `heroes`).
- `_attach_npcs` → `npc_roster.attach` → `npcs` (reads the planned sites and `heroes`; "It runs last because it reads `heroes`").
- `_attach_key_locations` → `key_locations.attach` → `key_locations`. **Docstring:** *"reads the finished terrain, hydrology, ley networks, roads, cultures, ruins, nests and religion and writes only this block: caves, mines, barrows, shrines, watchtowers, lairs, drowned ruins and wonders… It dedupes by node against every point another module already placed, so nothing here re-places a ruin, a nest, a shrine or a college."*
- `_attach_key_location_plans` → `key_locations.attach_plans` → `key_location_plans`. **Docstring:** *"A separate top-level block because `city_plans`, `hamlet_plans` and `castle_plans` are top-level: a consumer scanning for plan blocks would not find these nested inside a sited-feature one."*

## N. `adopt_world(world)`
```
result = deepcopy({k:v for k,v in world.items() if k != 'build_stages'})
if 'build_stages' in world: result['build_stages'] = list(world['build_stages'])   # shallow
timing = result.setdefault('timing_ms', {}); timing.setdefault('total', 0.0)
```
**Docstring, carried:** *"Three things every stateless request API needs and must not get subtly different: a deep copy, because the caller's object is never touched; `build_stages` shared rather than copied, since snapshots are immutable and copying them doubles the cost of the most expensive block in the document; `timing_ms` present, because `cli.py` strips it to keep an exported world byte-reproducible and roughly thirty producers write into it unguarded. The guard belongs here, at the boundary a persisted world re-enters through, rather than at whichever producer happens to run first."*

## O. `validate_age_world(world)` — the versioned state boundary

Wrapped in `try/except (KeyError, TypeError, IndexError, OverflowError)` → `ValueError('Malformed age world: ' + str(exc))`. Checks, in order:
1. `isinstance(world, dict)` else typed error.
2. `json.dumps(world, allow_nan=False)` — the world must be finite, serialisable JSON.
3. `cfg = Config(**world['config'])`; require `world_recipe == 3`, `phase >= 13`, `size <= 1025`.
4. `terrain.version == 6`, `generator_version == 16`, `recipe.version == 3`.
5. `magic.version == 4` and `history.version == 3`. **Comment:** *"History 3 is the twelve-school contract. A world built against the eight-school taxonomy has no hidden networks to validate, so it cannot advance an age here."*
6. `astrology.version == 1`, `lunar_almanac.version == 1`, `religion.version == 1`, `religion.catalogue == catalogue_identity()`.
7. Moon sanity: each of `synodic`, `spin`, `nod` must be an `int` inside `PERIOD_RANGES` (`synodic 26..34`, `spin 15..63`, `nod 17..37`) with an integer offset in `0 <= offset < period`; `tilt_max_degrees` numeric in `TILT_RANGE` (10..35); `great_year_days == lcm(*periods.values())`.
8. `settlements.version == 16`, `civilizations.version == 3`, `humans.version == 9` (with a long refusal message about fortress demand).
9. `civilizations.registry == registry_identity()`.
10. `terrain_detail` must equal a freshly computed `attach_detail({'config': world['config']})['terrain_detail']`.
11. `city_plans`/`hamlet_plans`/`castle_plans`, if present, must carry the current `planner_identity()`.
12. `set(magic.networks) == set(SCHOOLS)` (all twelve).
13. `history.ages` must be a list whose `age` values are exactly `1..len(ages)` — contiguous.
14. Every one of `height, slope, tpi, water_type, water_surface, water_depth, river, rain_river, temperature, moisture, climate_moisture, volcanic, natural_biome` must be present in `layers`.
15. Every layer: `n × n`, all values `int|float|bool` and finite, `row[0] == row[-1]` for every row, and rows 0 and `n-1` each hold a single distinct value.
16. Every `natural_biome` value is an `int` in `NATURAL_BIOMES`.
17. `terrain.natural_biomes`, `terrain.biomes` and `terrain.magical_biomes` must equal freshly built catalogues. `layers['biome'] == layers['natural_biome']`.
18. Every `biome_variant` is an `int` in `-1 .. len(catalogue)-1`, and when `>= 0`, `catalogue[value]['core_biome_id'] == natural_biome[z][x]`.
19. For `globe_radius`, `sea_level`, `radius`: `isclose(effective_config[key], getattr(cfg,key) * cfg.world_scale, rel_tol=1e-10, abs_tol=1e-10)`.
20. Per city: the profile's `civilization.kind` must be `'entity'` and `civilization_id == population_profile`; `city_class` in `('small','medium','capital')`; `node` an int in range and **not repeated**; `points[node] == (city['x'], city['z'])`; `direction(x,z,n)` within `1e-8` of `city['direction']`; `uid` unique; `source_culture` a string.
21. Re-run `classify_cities` on a deep copy and require every `city_class` to match.
22. No ruin's `node` may be occupied by an active city.
23. Per network: `strength` in 0..2, `width_m` in 10..2000, `instability` in 0..1; `len(nodes) <= 1024` and `len(edges) <= 4096` (the leyline edit budget); node ids unique and each node re-validated through `edit_network` (the public boundary's finite-vector/intensity/id checks); line ids unique; every edge's `from`/`to` an int in `0..len(nodes)-1` with `from != to`; edge `intensity` numeric in 0..4.

Returns `cfg`.

## P. `advance_age_request(body)`

Request schema: `{'api_version': 1, 'world': {...}, 'steps': 1..10, 'leyline_edits': [...<=128], 'moon_day': int >= 0 | null}`. Unknown fields, wrong types and out-of-range values raise the typed errors from `terrain_errors`. Each edit is `{'school', 'node_id'|'line_id'|'new_node', 'intensity'}` with `school` restricted to `KNOWN_SCHOOLS` (the eight), and `new_node` and a bare `intensity` are mutually exclusive. Edits require `cfg.magic_enabled`.

```
world = body['world']; cfg = validate_age_world(world)
result = adopt_world(world)
for edit in edits: result['magic']['networks'][school] = edit_network(net, **edit minus 'school')
if edits: validate_age_world(result)          # only when edits changed something
started = perf_counter()
evaluate_networks(result,cfg); refresh_environment(result,cfg)
add_biome_variants(result,cfg); refresh_astrology(result,cfg)
start_age = len(result['history']['ages'])
result['history'].setdefault('operations',[]).append(
    {'api_version':1,'after_age':start_age,'steps':steps,'leyline_edits':deepcopy(edits),'moon_day':moon_day})
result['history']['replay'] = 'Persist this returned world for subsequent calls. Config reproduces genesis; operation history records subsequent advances and player leyline edits.'
for age in start_age+1 .. start_age+steps:
    baseline = world if age == start_age+1 else result
    snapshotting = 'build_stages' in result
    previous_layers = deepcopy(baseline['layers']) if snapshotting else {}
    previous_state  = {k: deepcopy(baseline.get(k)) for k in STATE_KEYS} if snapshotting else {}
    result.pop('city_plans'); result.pop('hamlet_plans'); result.pop('castle_plans'); result.pop('world_scene')
    age_transition(result, cfg, age, moon_day, final=(age == start_age+steps))
    if age == start_age+steps:
        fill_cities; fill_hamlets; fill_castles; add_nomads; add_nomad_routes;
        add_beast_movements; add_encounters; apply_nomad_effects;
        _attach_heroes; _attach_story_web; _attach_npcs; _attach_key_locations; _attach_key_location_plans
    if snapshotting:
        stage = len(result['build_stages']) + 1
        append {'stage': stage, 'title': f'Age transition {age}', 'kind': 'age',
                'layers': changed-only deep copies, 'removed_layers': sorted set difference,
                'state': changed-only deep copies over STATE_KEYS}
        result['phases']['titles'] = [s['title'] for s in result['build_stages']]
        result['phases']['completed'] = stage
result['age_api_version'] = 1
result['timing_ms']['age_advance_total'] = (perf_counter()-started)*1000
result['debug_stats'] = build_debug(result)
```
**Comment on skipping the re-validation:** *"Without edits, `result` is a deepcopy of a world `validate_age_world` has just accepted and the check is a pure function of content, so re-running it serialises the whole world a second time to reach the answer already in `cfg`."*
**Comment on the snapshot guard:** *"Only the `build_stages` snapshot below reads these, and a live world carries no snapshots: the CLI drops them before any consumer sees the document. Copied unconditionally, they were a full copy of every layer grid plus one per `STATE_KEY` — allocated, never read and freed, on every age of every advance a game makes."*
**Comment on regenerating nomads:** *"Regenerated, not carried: an age advance ruins cities, so bands classified against the previous age would name ruins that have moved and refuges that no longer stand. `add_nomads` replaces the block wholesale."*
**Docstring:** *"Stateless JSON API v1… Same input => same output. Persist the returned world to advance again; Config alone reproduces genesis, not player-modified runtime state. The caller's object is unchanged."*

## Constants

**Deep-time epoch (`tectonic_transition`)**
| constant | value | meaning | effect of change |
|---|---|---|---|
| `duration` | `0.65` | dimensionless angular duration of the epoch, multiplying each plate's `omega`. Reported as `geological_history.angular_duration`, narratively 50 My. | Larger moves plates further, so more crust is transported from far away and more boundary uplift accrues. Above ~π/speed a plate wraps the globe. |
| `elapsed_million_years` | `50` | narrative label only, written into the report. Nothing reads it. | None on geometry. |
| height blend `0.25` / `0.75` | old-in-place / transported | memory term: 3/4 of relief rides with the plate, 1/4 stays. | Raising the 0.25 share ghosts the old coastline more strongly; at 1.0/0.0 the epoch becomes pure uplift with no plate motion visible. |
| uplift weights `0.9` conv, `−0.7` div, `0.18` shear | | mountains at collisions, basins at rifts, a weak roughening at transforms. | Sign of the divergence term is what opens ocean basins; flipping it fills them. |
| uplift scale | `cfg.world_scale * cfg.tectonic_relief` | converts a dimensionless 0..1 boundary field into physical metres. | Must not be dropped: without it the uplift is in design units and dwarfs the physical surface by 1/world_scale ≈ 5.6×. |

**Relic incision (`carve_relics`)**
| constant | value | meaning |
|---|---|---|
| gorge cut | `min(45.0, max(4.0, tectonic_relief * world_scale * 0.12))` metres | 12% of the physical relief unit, floored at 4 m, ceilinged at 45 m. On the 11.15 km reference world (`tectonic_relief=800`, `world_scale=0.17744`) this is `141.95 × 0.12 = 17.03 m`. On any recipe-3 preset (`relief_m = 1667`) it is `1667 × 0.12 = 200.04`, so **it saturates at the 45 m ceiling on every production world**. |
| valley cut | `min(100.0, max(12.0, old_water_depth * 0.6))` metres | 60% of the water that used to stand there, floored at 12 m, ceilinged at 100 m. Drained ocean floor always saturates at 100 m. |
| shoulder factor | `0.25` | a neighbour's cut bleeds a quarter of its depth into this cell, one simultaneous pass. Zero would leave beds as single-cell marker pixels. |

**Threat and fate**
| constant | value | meaning |
|---|---|---|
| nest weight | `0.11 * tier * (1 − distance/reach)` | tier 1..5, so tier 5 at the gates gives exactly `0.55`, the historical flat weight for every monster. |
| nest reach | `min(radius*0.5, max(350.0, range_m * 2))` metres | `350` is an authored **pre-scaling** reference-world metre floor; `radius*0.5` is a post-scaling physical half-circumference cap. `range_m` on a nest is `spacing_m * nest_spacing / 2` from `terrain_nest_profiles.json`, also **pre-scaling** (100..3500 m across eligible species). |
| ley pressure | `min(0.6, potency * 0.45)` | magic alone cannot exceed 0.6 of a fate. |
| war pressure | `min(0.4, 0.15 * wars_fought)` | saturates at 3 wars. Backward-looking only; feeds `defense_priority`. |
| self-magic weight | `0.035 + 0.06 * potencies['weave']` | a floor of 3.5% per age even on dead ground, rising with Weave. |
| fate cap | `min(0.9, sum(weights))` | no city can be more than 90% likely to fall in one age. |
| `THREAT_ASSESSMENT_VERSION` | `3` | mirrored in `docs/conformance/version-bindings.json`. |
| ley drift | `intensity * uniform(0.65, 1.35)`, clamped to `min(4.0, ·)` | ±35% per age per node and per edge, hard-capped at the edit ceiling of 4. |
| ruin key intensity | `legacy['intensity']`, defaulting to `2.5` | from `CLASS_INTENSITY = {'small':1.5,'medium':2.5,'capital':3.5}`, floored at `SELF_MAGIC_FLOOR = 2.5` for self-destruction, forced to `DIVINE_INTENSITY = 4.0` for a divine cause. |

**Mutation thresholds (`dominant_school`)**: `threshold = 0.35` (minimum potency to mutate a biome) and `margin = 0.08` (minimum lead over the runner-up). Note these are **function defaults**; `magic['mutation_threshold']` and `magic['dominance_margin']` publish the same numbers but are not read by `add_biome_variants`.

**Calendar**: `DAYS_PER_YEAR = 360` (`30 * 12`), `HOURS_PER_DAY = 24`.

**World-option defaults reached from here**: `lunar_influence = 0.5`, `war_survival = 0.0`, `villain_rise = 0.5`, `villain_hold = 0.7`, `villain_density = 3.0`.

**Wars constants** (owned by `terrain_wars` but load-bearing for this module's output): `PROXIMITY_FACTOR = 1.5`, `VILLAIN_CONTENTION = 0.35`, `WAR_BASE_CHANCE = 0.5`, `WAR_PRESSURE_GAIN = 0.4`, `WAR_MAX_CHANCE = 0.92`, `FORT_STRENGTH = 50`.

**Villain constants** reached from here: `SUPER_TIER = 1.0`, `VILLAIN_WEIGHT = 0.5`, `FALLOFF = 1.5`, `TIER_KEPT_PER_AGE = 0.9`.

**Fixed version numbers written**: `generator_version = 16`, `terrain.version = 6`, `history.version = 3`, `geological_history.version = 1`, `phases.version = 2`, `recipe.version = 3`, `effective_config.world_recipe = 3`, `age_api_version = 1`, `threat_assessments.version = 3`.

## Data files

`terrain_history.py` loads no data file directly. It reaches three through imports:

**`Sim/icarus_sim/biomes.json`** (via `terrain_biome_catalogue`) — the natural biome registry and the 156 magical variant names.
```
{ "schema_version": 1,
  "natural_biomes": [
    {"id": 0, "core": "ocean", "color": [r,g,b]},
    {"id": 1, "core": "tundra", "color": [...], "reachable": false, "unreachable_note": "..."},
    ... 13 rows, ids 0,1,2,3,4,5,6,7,8,13,15,16,17 ...
  ],
  "variant_names": { "weave": [13 names in catalogue order], "umbral": [...], ... 12 schools ... } }
```
Representative names: `variant_names['weave'][0] = "Prismatic seas"`, `[1] = "Dream tundra"`, `[2] = "Glass mirages"`. **Position in `natural_biomes` is a contract**: the loader preserves file order without sorting, because `biome_variant` stores a flat index built from it. Ids 1 (`tundra`) and 6 (`snow`) are tombstoned (`reachable: false`) but remain in the catalogue and in `terrain.biomes`, because `validate_age_world` compares those blocks for exact equality and removing a row would be a schema change in every saved world. The loader fails closed on a duplicate id, an unknown key, a non-boolean `reachable`, a tombstone with no note, a total tombstone, a school set that does not exactly equal `SCHOOLS`, or a variant-name list of the wrong length.

**`Sim/icarus_sim/terrain_nest_profiles.json`** (via `terrain_nests`) — 680 species profiles, `{"version": 2, "roles": ..., "profiles": [...]}`. A representative entry:
```
{"id":"aardvark","name":"Aardvark","class":"animal","tier":1,"role":"herbivore","real":true,
 "family":"animal","size":"large","medium":"land","kind":"den","temperature":[14,42],
 "weights":{"plains":2},"requires":{},"spacing_m":750,"occurrence":0.45,"sky":false,"movement":"nester"}
```
This module reads only the derived nest *sites*, but the selection that matters here is: a nest contributes to a city's fate iff `layer == 'surface'`, `real` is false (`class != 'animal'`), and either its `name` contains "dragon" (case-insensitive) or its `family` is one of `infernal`, `undead`, `aberrant`. 127 of the 680 profiles qualify; their `spacing_m` spans 100..3500 m, median 400.

**`Sim/icarus_sim/civilizations.json`** (via `terrain_profiles` / `civilization_registry`) — read indirectly for `get_profile(population_profile)['civilization']['kind']` in validation and for `culture_schools()` in `ruin_legacy` (each civilization's `magic_school` key trait, resolved through the heritage tables).

**`Sim/icarus_sim/pantheon.json` / `hidden_pantheon.json`** — reached through `add_religion` and `catalogue_identity()`; this module only compares the identity string.

## Random streams

Every stream in this module is derived with `terrain_tectonics.child_seed`:
```
child_seed(master, domain, variation=0) =
    int.from_bytes(sha256(f'tectonics-v1:{master}:{domain}:{variation}').digest()[:4], 'big')   # uint32
```
and consumed through `random.Random(seed)` (CPython Mersenne Twister). This is a clean rewrite, so FWGC++ substitutes `Rng::child(master, domain, variation)` (xoshiro256++ via `child_seed64`) — the *structure* of the naming is what must be preserved, not the numbers.

**The four streams this module opens:**

| domain string | variation | opened where | draws |
|---|---|---|---|
| `'city-fate-' + city['uid']` | `age` | `city_fate`, once per city per age | exactly **one** `random()`, taken even when the city survives |
| `'age-ley-' + school_name` | `age` | the ley drift loop, once per school per age | `len(nodes) + len(edges)` calls to `uniform(0.65, 1.35)` — **data-dependent**, and it grows every age because each ruin adds a node |
| `'war-' + a_uid + '-' + b_uid` | `age` | `resolve_wars`, per contested pair actually examined | one `random()` per pair that is not skipped |
| `'war-survival-' + a_uid + '-' + b_uid` | `age` | `resolve_wars`, only when `war_survival > 0` | one `random()` per war fought |

Plus, reached but not opened here: `terrain_villains` opens its own streams inside `advance`/`promote`/`build`; `add_nests`, `add_settlements`, `add_nomads` and the external packages open theirs.

**Stream discipline notes that matter for a port:**
- `city_fate` draws its single word **before** deciding survival, so the per-city stream is independent and the number of draws is fixed at one per city per age. Do not short-circuit it.
- The war draw happens for every pair that reaches it, but a pair is skipped entirely (no draw) when either participant is already fated or spared. So the war stream's draw count depends on the outcome of earlier pairs in the same sorted list.
- The ley-drift draw count is `nodes + edges` per school per age and grows monotonically with ruins. Two worlds that differ in how many cities fell in age 1 will have a different number of draws in age 2's ley stream for the schools that received key points — but because each school has its own named stream keyed on `(seed, school, age)`, that divergence cannot leak into another school.
- `variation` is the **age number**, so the same city in age 1 and age 2 draws from different streams and cannot repeat its roll.
- `city_fate` and `resolve_wars` both accept explicit `roll` / `rolls` / `survival_rolls` overrides for tests, keyed by war id with `'*'` as a default. Keep that seam.

## Ordering hazards

This module is dense with ordering that reaches output. Every one of these must be reproduced exactly.

1. **`fields = {k: [] for k in ('height','second_uplift','moved_plates','second_convergence','second_divergence')}`** then `for key, value in zip(fields, (height, height-old, owner, record[3], record[4]))`. The write is positional against **dict insertion order**. Reordering the tuple literal silently relabels five layers.

2. **`sorted(nests, key=lambda n: n['id'])` in `fantasy_nest_threats`** — nest ids are strings `"<kind>-<species_id>-<node>"`, so this is a **lexicographic string sort**; node 10 sorts before node 2. This order is the order causes enter `city_fate`'s list, and the selector walk is order-sensitive, so it decides *which* monster is named as the destroyer when several are in reach.

3. **`city_fate`'s cause list order is the lottery's inverse-CDF order**: ley, then nests (string-sorted), then self-magic, then villains. The code comment says it outright: *"Appended last, like the divine cause: the selector walk below is order-sensitive, so a villain may extend the list but must never insert into it."* Any insertion moves every world.

4. **`dominant_school`'s tie-break.** `heapq.nsmallest(2, ((-v, k) for k, v in potencies.items()))` compares `(-potency, name)` tuples, so an exact potency tie resolves to the **alphabetically first school name**, not to `SCHOOLS` order. (`terrain_ruins.source_school` uses the opposite convention for its `primordial` element tie — `list(SCHOOLS).index(s)` — so the two tie-breaks in the same pipeline disagree deliberately.)

5. **`sorted(result['settlements']['sites'], key=lambda s: str(s.get('uid', s['id'])))` in `add_threat_assessments`** — string sort of uids. This is also the order `fill_cities` and `fill_hamlets` use, so the report order and the plan order agree.

6. **`sorted(old_ids)`** for `surviving_city_ids` — a set of uid strings sorted lexicographically. `new_city_ids` is **not** sorted; it is in `settlements['sites']` list order.

7. **The city-fate loop runs over `cities` in `settlements['sites']` order**, so `events` and `ruins` are appended in that order, and the ruin key-point loop therefore adds nodes to each network in that order. Node position inside a network is the index `edges['from']/['to']` refer to, so this ordering is permanently baked into the network.

8. **The ley-drift loop `for name, net in result['magic']['networks'].items()`** iterates the dict in `SCHOOLS` insertion order (`weave, umbral, infernal, radiant, fire, water, earth, air, blood, void, rot, eldritch`). Each school has its own stream so cross-school order is not observable — but `for item in net['nodes'] + net['edges']` consumes one stream in nodes-then-edges list order and *is* observable.

9. **The shoulder pass in `carve_relics` is a simultaneous sweep, not in-place.** The list comprehension reads the pre-shoulder `cuts` for every `cuts[j]`. An in-place loop would let a shoulder cascade across the grid in `points` order.

10. **`sphere_grid` point order** is `[(0,0)] + [(x,z) for z in 1..n-2 for x in 0..n-2] + [(0,n-1)]`, i.e. **row-major, pole-first**. `cuts`, `node_grid` and `graph` all index against it. Note: `sorted(nodes)[:N]` on this list samples the pole, not a representative cell.

11. **`set(previous_layers) - set(result['layers'])` is wrapped in `sorted()`** for `removed_layers` — deliberately, so the snapshot bytes do not depend on set iteration order.

12. **Dict-equality comparisons drive the snapshot delta** (`previous_layers.get(k) != v`). Two dicts with the same contents in a different key order compare equal in Python, so key order is not itself a delta trigger — but the *stored* copy keeps the producer's insertion order, which reaches the exported JSON bytes. `terrain_biome_catalogue` says this explicitly: *"Both blocks go through one entry builder so key insertion order, which the output bytes depend on, cannot drift between them."*

13. **`for key in entry['removed_layers']` runs after `out['layers'].update(entry['layers'])` in `materialize_stage`** — a layer both written and removed in the same stage ends up removed.

14. **`contested_pairs` sorts by `(-pressure, a_uid, b_uid)`** and `resolve_wars` walks that order, skipping pairs whose participants are already fated or spared. Strongest contention is resolved first, and that decides who is still available to fight later pairs.

15. **`min(remaining)` and `max(components, key=...)` in `route_water`**, reached through every `add_water` call this module makes. The ocean is "the largest connected below-sea component", ties broken by which component `max` sees first, which is flood-fill discovery order from `min(remaining)`.

16. **`ICARUS_CITY_WORKERS`.** `city_planner.fill_cities` and `hamlet_planner.fill_hamlets` both read this undocumented environment variable and fork a process pool when it is unset-or-not-1 and the site count reaches `_MIN_SITES_FOR_POOL`. On failure they fall back to serial **with a `RuntimeWarning`, deliberately** — *"a silent fallback is indistinguishable from a planner that is simply slow, which is exactly the thing nobody would investigate."* Nothing in the repository proves pooled output equals serial output. Both are called from stage 16 and from the last age of an advance, so if they differ, the age-advance result depends on an environment variable. Port them serial.

## Edge cases

**Poles and the seam.** Every raster this module writes is padded the same way: a pole row (`z == 0` or `z == n-1`) computes **one** value and repeats it `n` times; every other row computes `n-1` values and appends `row[0]` to close the seam. `node_grid` does the same for node-indexed data: `out[0] = [values[0]] * n`, `out[-1] = [values[-1]] * n`, `row[-1] = row[0]`. `validate_age_world` enforces both invariants on every layer of an incoming world, so a violation is a hard refusal, not a warning.

**`sample()` at the poles.** `terrain_globe.sample` clamps `z` to `0 .. n-1` and clamps `p[1]` to `[-1, 1]` before `asin`, and uses `jz = min(n-1, iz+1)`, so the backtrace in `tectonic_transition` never indexes out of range even when a rotated point lands exactly on a pole. The longitude index wraps with `% (n-1)` and reads `h[iz][ix+1]`, which is in range because the grid stores the duplicate seam column.

**`layout_point` with a degenerate boundary.** It skips a plate pair whose projected boundary direction has `dot(raw, raw) < 1e-20`, and divides the blended contributions by `max(1.0, total)` — *"A baseline weight fades isolated pair tails to zero without a hard cutoff."* With no contributing pairs at all, `blended` is all zeros and the cell gets pure continental profile with no interaction.

**A world with no ocean.** `route_water` falls back to `roots = [min(range(len(h)), key=lambda i: (h[i], i))]` — the global minimum, ties by node index. `water['has_ocean']` is then false. Nothing in `carve_relics` special-cases this; `valleys` would simply be empty.

**No causes at all.** `city_fate` returns `None` (the city survives) when `causes` is empty *or* when `draw >= chance`. With `chance == 0` the first test always fires, so `selector = (draw/chance)` is never evaluated and there is no division by zero.

**`magic_enabled == 0`.** No self-magic cause, no ley drift, no ruin key points, no god departure, and `rebuild_tail` is called with `evaluate=False` so `evaluate_networks` is skipped. But `add_biome_variants` still runs and still reads all twelve `ley_*` layers — `generate_networks` writes them regardless (all zero, in fact **negative** zero: `strength * -math.expm1(-0)` is `-0.0`, and `terrain_leyline_history` deliberately preserves that sign). `dominant_school` on twelve zeros returns `None` because `0 < 0.35`, so every cell gets `biome_variant = -1`.

**A hidden-school ley cause would crash.** `LEY_DESTRUCTION` has only the eight known schools. `ley_threat_pressure` uses `dominant_school` over all twelve, so if a corruption-API-inserted hidden network ever won a city's cell, `LEY_DESTRUCTION[winner]` raises `KeyError`. Generation can never reach it (hidden occurrence is locked at zero), but the age-advance API operates on worlds the corruption API may have touched.

**Empty settlements.** `cities = deepcopy(result['settlements']['sites'])` can be empty (`settlement_count = 0` is legal). Then `wars = []`, `survivors = []`, `events = []`, and `history['ages']` still gets an entry with empty lists. `add_threat_assessments` writes an empty `cities` list.

**`astrology` absent.** `lunar_context` returns `(None, None)`, `city_potencies` takes the unswayed field, and the age record's `moon` is `None`. `civilization()` and `rebuild_tail` both guard `if 'astrology' in result`.

**`settlements.founding.end_year` absent.** `lunar_context` defaults the whole chain: `(result.get('settlements',{}).get('founding') or {}).get('end_year', 0)` → day 0.

**Missing `lunar_sensitivity` layer** → sway of 0.0, i.e. the moon has no effect. Missing `ley_<school>` layer → potency 0.0 for that school (an explicit `if 'ley_'+name in layers` guard, added because a hidden school's field can be absent from a hand-built layer set and *"missing is zero potency, not 'this city has no school', which would drop the key point a magically charged city is owed when it falls"*).

**`absorb_survivor_camps` on a generated world** writes nothing: `settlement_candidates` is produced by `apply_nomad_effects` at stage 16, after both generation-internal age transitions. It matters only on the advance path. It is idempotent by band uid (`nomad-<age>-<node>`, never reused by a later age) because `seed_survivor_camps` only assigns the block when it has candidates, so a world can carry a previous advance's list unchanged.

**A refuge that fell this age** absorbs nobody — the filter is the `survivors` argument, not a lookup.

**`materialize_stage` out of range** raises `ValueError('Stage was not generated')` for `stage < 1` or `stage > len(build_stages)`.

**`edit_network` on a duplicate id** raises. Ruin key ids are `'ruin-<uid>-key'` and uids are unique per world, so a collision would mean a repeated uid — which `validate_age_world` independently refuses.

**Leyline edit budget.** `validate_age_world` refuses a network with more than 1024 nodes or 4096 edges. Each age adds at most one node per fallen city plus villain wells, so the budget bounds how many ages a world can be advanced.

**`steps` bound**: 1..10 per request; `leyline_edits` bound: ≤128 per request.

## Reasoning worth preserving

These comments record decisions, measurements or failures that exist nowhere else. Quoted.

**On the stage-14 promotion gate** (`generate_history`):
> "`final` is stage 15 and not `min(15,cfg.phase)`: a phase-14 build must agree with stage 14 of a phase-16 build, which `test_terrain_biome_contract` pins, so a shorter run may not promote at a stage the longer run does not."

**On the last-age promotion** (`age_transition`):
> "The simulation ends with promotions. Accumulation alone cannot seat anybody inside a generation -- two age transitions against a rise the legal range tops out below -- so the last age is where the world names its antagonists. Placed before the works pass so a promoted villain sinks its well and stakes its claims in the same age it rises, exactly as an accumulated one does."

**On snapshot cost** (`capture`):
> "The snapshot above already holds an independent copy of everything that changed, and an unchanged entry still equals the copy kept from the stage that last wrote it. Re-deep-copying every layer and state key here copied the whole world a second time on each of the sixteen stages."

**On the age-advance snapshot guard**:
> "Only the `build_stages` snapshot below reads these, and a live world carries no snapshots: the CLI drops them before any consumer sees the document. Copied unconditionally, they were a full copy of every layer grid plus one per `STATE_KEY` -- allocated, never read and freed, on every age of every advance a game makes. Presence of `build_stages` cannot change inside this loop, so the guard is the same condition the snapshot already tests."

**On skipping the second validation**:
> "Without edits, `result` is a deepcopy of a world `validate_age_world` has just accepted and the check is a pure function of content, so re-running it serialises the whole world a second time to reach the answer already in `cfg`. Edits mutate `magic.networks`, so an edited world still pays for the full re-check."

**On ordering wars first**:
> "Wars are settled before any other fate. They are decided between cities out of the ground and supply the finished age left them, and a city a neighbour has already taken cannot also be eaten by a dragon; its ruin records the war that ended it."

**On deferring the ley drift**:
> "Death decisions all use the pre-transition fields. Only then evolve the ley inputs."

**On villain ordering**:
> "The cast advances on the assessment this age inherited, so a villain is always one rebuild behind the region it is eating, and can never be raised by its own damage."
> "Appended last, like the divine cause: the selector walk below is order-sensitive, so a villain may extend the list but must never insert into it."
> "Built after the rebuild, on settled ground: a well joins the holdings a villain actually has this age, and a claim names the peoples actually near it."

**On the standing filter in `add_threat_assessments`**:
> "`reach_m` is refreshed before the fall is decided, so a fallen villain still carries a live-looking reach and would feed the war outlook without the standing filter."

**On nest tier weighting** (`fantasy_nest_threats` docstring):
> "Weight scales with the danger tier, so a greater lair at the gates is the reason a city falls and a tier one nuisance is not; tier five keeps the flat .55 the single fixed weight used to give every monster alike."

**On the lunar sway** (`city_potencies` docstring and body):
> "The potencies a lottery reads: the base field, swayed by the moon when a surge is in force… The sway is momentary: it scales what the fate sees, never the exported field. Same association order as `Core/ages.cpp`."
> "Only the known schools sway: the tide is charted against this world's moon, and a school the world has never named was never in that chart."

**On the veteran term**:
> "A city that has already been to war expects the next one. This is why a veteran builds heavier walls: the city planner reads `regional_threat` as `defense_priority`."
> "Version 3 adds the forward-looking war outlook beside the backward-looking pressure. The regional threat the city planner reads is unchanged: a forecast does not build walls yet."

**On the war victor's scar**:
> "The victor's own magic scars the ground it took."
> "Both sides carry the war, so a survivor's history says what it has already fought."
> "World option `war_survival`: the loser stands as a vassal, a living enemy in both histories."

**On rebuilding everything from active cities** (`rebuild_tail`):
> "All dependent institutions/roads/food are rebuilt from active cities, so no dead-city hamlets survive."
> "One body, two callers, so the two cannot drift. `evaluate` skips the ley rebuild when magic is off; `religion` re-resolves the pantheon, which an age turning does and a visitation does not, because a visitation resolves religion itself once its own records are written. The threat pass takes the spacing explicitly from the config it was built with, rather than one caller passing it and the other letting the pass look it up."

**On the relic shoulders**:
> "Small shoulders make former river beds visible as terrain, not just marker pixels."
> "Refilled cuts are historical features; only still-dry abandoned beds are current gorges/valleys."

**On the epoch's honesty** (`geological_history.method`):
> "50-million-year artistic epoch: rotate original plate centers, backtrace crust, blend transported relief and new boundary uplift. Angular speeds are dimensionless, not calibrated geodynamics."

**On stage 1–5 layer stripping**:
> "Climate/biome inspection belongs to stage 8 in this recipe."

**On stage-16 ordering**:
> "Bands are part of the world, so they are placed before the packages that only read it: heroes, the story web and the rosters see a world with them in."
> (`_attach_npcs`) "It runs last because it reads `heroes`."
> (`_attach_key_location_plans`) "A separate top-level block because `city_plans`, `hamlet_plans` and `castle_plans` are top-level: a consumer scanning for plan blocks would not find these nested inside a sited-feature one."

**On regenerating nomads on an advance**:
> "Regenerated, not carried: an age advance ruins cities, so bands classified against the previous age would name ruins that have moved and refuges that no longer stand. `add_nomads` replaces the block wholesale."

**On `adopt_world`'s three guarantees** — quoted in full in the algorithm section; the `timing_ms` clause is the one a porter will otherwise get wrong:
> "`timing_ms` present, because `cli.py` strips it to keep an exported world byte-reproducible and roughly thirty producers write into it unguarded. The guard belongs here, at the boundary a persisted world re-enters through, rather than at whichever producer happens to run first."

**On the refusal messages in `validate_age_world`** — each carries a reason for the version floor:
> (history 3) "History 3 is the twelve-school contract. A world built against the eight-school taxonomy has no hidden networks to validate, so it cannot advance an age here."
> (humans 9) "A rural report older than 7 pinned its fortresses to a static count. Advancing it would rebuild the hinterland under the derived demand and hand back a world whose two ages disagree about how much route defence it ever wanted."
> (recipe) "This world was built against a retired contract. Regenerate it with recipe_version 3 rather than migrating; generated worlds are disposable by policy."
> (node re-validation) "Reuse the public edit boundary's finite vector/intensity/id checks."

**On the overrides document**:
> "Authored inputs the request builder consumes (`circumference_km`, `relief_m`) are not config fields, so they never reach the resolved document; their effect appears in the terms they derive."

**From `terrain_nomad_effects.absorb_survivor_camps`, an owner ruling that this module's call site depends on**:
> "**Owner ruling, 2026-09-21.** A candidate does not name free ground. It names the refuge city's own node, 4 of 4 across two worlds and true by construction… 'Adopt the candidate as a settlement' would therefore found a second settlement on a standing city. The band did not reach empty ground and start a hamlet; it reached a town and stopped, so the town gets bigger. The rejected alternative was recording the camp one cell short of the refuge, which buys free ground at the price of a resolution-dependent placement constant -- 8 km at size 17, under 1 km at size 129 -- which is the exact failure this subsystem exists to avoid."
> "**The absorbed count has to ride `CARRIED_SURVIVOR_KEYS`, not `population_estimate`.** … `add_settlements` re-derives `population_estimate` from the population budget on every rebuild, so a number added to a survivor row before `rebuild_tail` is overwritten by the very rebuild it was meant to ride."

**From `terrain_settlements`, the survivor carry contract**:
> "What a rebuilt site inherits from the survivor standing on the same node. `war_history` travels with the city: a survivor that rebuilds its hinterland is still the city that fought, and its forts answer to that."

**From `city_planner.fill_cities`, on the pool**:
> "A world must still generate if the pool cannot start, so this falls back rather than failing -- but a silent fallback is indistinguishable from a planner that is simply slow, which is exactly the thing nobody would investigate. Say it."

## Notes for the C++ implementation

**The shape of the port.** This module is a *driver* plus two geomorphology passes plus one simulation loop. Split it into at least four translation units: `stages.cpp` (the ladder, `capture`, `materialize_stage`), `epoch.cpp` (`tectonic_transition`, `carve_relics`, `measure_surface`, `abandoned_waterways`), `ages.cpp` (`age_transition`, `city_fate`, `rebuild_tail`, `remember_cities`, the threat chain), `age_api.cpp` (`adopt_world`, `validate_age_world`, `advance_age_request`). There is an existing `Core/ages.cpp` referenced by two comments in the Python (`city_potencies`: *"Same association order as Core/ages.cpp"*; `terrain_ruins`: *"The nest-family table is a code constant mirrored in Core/ages.cpp"*) — that file is not in FWGC++ and its content is not recoverable from here, but the names are a hint that a previous native attempt organised it the same way.

**`fwg::Grid` maps cleanly.** Every layer this module writes is an `n × n` double grid with the seam and pole redundancy that `Grid` already enforces via `fill_row`. The stage-6 inner loop is exactly `for z: { std::vector<double> row; for x in 0..distinct_count(z,n)-1 {...} grid.fill_row(z, row); }` — five grids filled in parallel, so gather into five `std::vector<double>` row buffers and call `fill_row` five times per row. `moved_plates` is an integer plate id stored in a double grid; that is fine (ids are ≤ 48) but note that the exported JSON will then print `3.0` where Python printed `3`. If byte-compatible export matters, carry it as a separate integer grid type.

**`sphere_grid` maps cleanly.** `carve_relics` needs `points`, `neighbours` (with great-circle distances) and `to_grid`, all of which `fwg::sphere_grid` already provides. The shoulder pass becomes:
```cpp
std::vector<double> shouldered(cuts.size());
for (std::size_t i = 0; i < cuts.size(); ++i) {
    double best = 0.0;
    for (const auto& nb : sg->neighbours[i]) best = std::max(best, cuts[nb.index] * 0.25);
    shouldered[i] = std::max(cuts[i], best);
}
```
Two buffers, never in place. Hold the `shared_ptr` the header insists on.

**`Rng` and the three seed derivations.** `city_fate`, the ley drift and both war draws all use `child_seed(master, domain, variation)` where `domain` is a runtime-built string. In C++ that is `Rng::child(cfg.seed, domain, age)` with `domain` assembled into a `std::string` — e.g. `"city-fate-" + city.uid`, `"age-ley-" + school`, `"war-" + a.uid + "-" + b.uid`, `"war-survival-" + a.uid + "-" + b.uid`. **No fourth derivation is needed and none must be invented.** `random.Random(int).random()` becomes `Rng::random()`; `rng.uniform(.65, 1.35)` becomes `Rng::uniform(0.65, 1.35)`.

**The big structural problem: the world is a Python dict.** Almost everything here is `result[...]` on a free-form document — `result['settlements']['sites'][i]['war_history']`, `result['magic']['networks'][school]['nodes']`, `result.setdefault('ruins', [])`. `fwg::World` holds typed blocks, which is better, but the port must decide up front what the typed blocks are for at least: `Settlements{version, founding, sites[]}`, `City{uid, id, node, x, z, direction, height_m, population_profile, civilization_id, parent_race_id, city_class, population_estimate, name, source_culture, founded_age, war_history[], absorbed_refugees, absorbed_bands[], vassal_of}`, `Ruin`, `War`, `AgeRecord`, `ThreatReport`, `MagicNetwork{nodes[], edges[]}`, `Nest`, `Villain`, `GeologicalHistory`. **No block uses the same key name for its identity** — see `ordering_hazards` and the id table below — so a generic "find by id" helper will be wrong; give every struct its own accessor.

| block | identity field | type |
|---|---|---|
| `settlements.sites[]` | `uid` | string, stable across ages |
| `settlements.sites[]` | `id` | **int index, renumbers on every rebuild** |
| `ruins[]` | `id` | string, `"ruin-" + uid` |
| `beast_nests.sites[]` | `id` | string, `"<kind>-<species>-<node>"` |
| `humans.cores[]` | `site_id` | int, matches a city's `id` **within one rebuild only** |
| `magic.networks[].nodes[]` | `id` | string; edges reference by **positional index** |
| `threat_assessments.cities[]` | `city_uid` | string |
| `villains.people[]` | `uid` | string; `region` is the persistent key |
| `history.ages[]` | `age` | int, contiguous from 1 |

**`copy.deepcopy` is everywhere and means something different in C++.** `plates = deepcopy(result['phases']['plates'])`, `cities = deepcopy(result['settlements']['sites'])`, `old = deepcopy({...four layers...})`, `ruin = {k: deepcopy(city[k]) ...}`, `events.append(deepcopy(ruin))`, `'wars': deepcopy(wars)`. In C++ value semantics give you these for free, but two places matter: `layers['pre_tectonic_height'] = old['height']` is an **alias in Python** (both names point at one list) and becomes a copy in C++ — harmless, but it doubles memory for one grid, and `carve_relics` reads `old['water_depth']` afterwards, so the `old` bundle must outlive stage 6. Pass it by value into stage 7 or keep it in a `struct PreEpoch { Grid height, river, water_type, water_depth; }` local to the driver.

**The snapshot delta is a Python-equality problem.** `previous_layers.get(k) != v` compares whole nested structures. In C++, grids compare cheaply (`std::vector<double>` equality), but the `state` half compares arbitrary blocks. Either give every typed block an `operator==`, or — better — have each producing pass set a dirty flag. A dirty-flag port changes nothing about the world but does change the snapshot contents if a pass is marked dirty without changing anything; the Python is content-addressed and will not record a no-op write. Pick content comparison if fidelity matters.

**Ordered maps.** Python dicts preserve insertion order and this module depends on it in at least three places (the five-field `fields` dict, `magic['networks']` iteration in `SCHOOLS` order, and `STATE_KEYS` as a tuple). Use `std::vector<std::pair<K,V>>` or an explicit ordered key list for `networks` and for the schools; `std::map<std::string, ...>` would silently alphabetise and change the ley-drift school order (harmless, since each has its own stream) and the exported JSON key order (not harmless).

**String-keyed sorts.** `sorted(nests, key=id)` and `sorted(sites, key=str(uid))` are byte-wise `std::string` comparisons — `std::sort` with `<` on `std::string` matches Python's `str` comparison for ASCII, which these ids are. `sorted(old_ids)` over a set becomes sort a `std::vector<std::string>`. Use `std::stable_sort` where the Python used `sorted` on a list that could contain equal keys; uids are unique so it does not bite here, but `contested_pairs`'s `(-pressure, a_uid, b_uid)` key can tie on pressure and the uid components resolve it deterministically.

**`heapq.nsmallest(2, ...)` in `dominant_school`** is just "find the two largest potencies, tie-break by ascending name". Write it as an explicit two-pass scan over the twelve schools with the comparator `(-potency, name)` — do not use `std::nth_element`, which is unstable.

**The five external packages have no C++ equivalent.** `hero_generator`, `story_web`, `npc_roster`, `key_locations` are separate Python packages the driver calls through a `try`-free `attach()` boundary that returns `None` when switched off. Stub them as optional callbacks on the `World` and leave `heroes`, `story_web`, `npcs`, `key_locations`, `key_location_plans` absent by default; the rest of the world is valid without them.

**Process pool: do not port it.** `ICARUS_CITY_WORKERS` forks a pool in `fill_cities` and `fill_hamlets`. Nothing proves pooled output equals serial output. Implement serial only, and if parallelism is wanted later, prove equality first.

**`json.dumps(world, allow_nan=False)` in `validate_age_world`** is doing double duty: it is both a finiteness check and a serialisability check. FWGC++ has its own JSON writer — make it reject non-finite doubles so the same guarantee holds, and run the layer finiteness check separately (the Python does both).

**Floats.** This is a clean rewrite, so no ULP matching is required. But two places are genuinely sensitive to arithmetic: `math.isclose(..., rel_tol=1e-10, abs_tol=1e-10)` in `validate_age_world`'s physical-scale check, and `abs(sum(v*v for v in p) - 1) > 1e-6` in `edit_network`'s unit-vector check. Both are tolerances on values the port itself produces, so keep the tolerances and make sure the producer stays inside them — the ruin `direction` handed to `edit_network` comes from `direction(x,z,n)`, which is unit to well within 1e-6.

**`replace(cfg, phase=N)`** appears seven times. `fwg::Config` is a plain struct, so this is `Config c = cfg; c.phase = N;`. It is the mechanism by which the driver tells a downstream pass which of *its* internal phases to run, and it is not the same number as the stage. Stage 6 and 7 pass `phase=5` to `add_water`; stage 8 passes `phase=6` to climate/labels/ecology; stages 10/11/12 pass `phase=7/8/9` to `civilization`; every `add_nests` call passes `phase=9`.

**Estimated shape**: roughly 3000 lines of C++ excluding tests, of which the age transition and the threat/fate chain are about half.

## Open questions

1. **`vassal_of` does not survive the rebuild.** `age_transition` sets `by_uid[war['defeated_uid']]['vassal_of'] = war['victor_uid']` on the in-flight `cities` copy. Those copies become `survivors` → `result['_survivors']`, and `add_settlements` rebuilds the site list from scratch, copying only `terrain_settlements.CARRIED_SURVIVOR_KEYS = ('uid','source_culture','founded_age','name','war_history','absorbed_refugees','absorbed_bands','founded_by')` onto the new site. `vassal_of` is not in that tuple, so the flag is discarded by the very rebuild that follows it. `docs/terrain-world-layers.md` states "the loser carries `vassal_of`", which the code does not deliver. Grepping the whole repository finds exactly two mentions of `vassal_of`: the write in this file and that doc sentence — no consumer. Because `war_survival` defaults to 0, the path never fires on a default world, which is why this has not been caught. **A porter must decide**: add `vassal_of` to the carried keys (making the doc true and changing any `war_survival > 0` world), or drop the field (making the doc false). Do not silently reproduce the current behaviour without recording it.

2. **Nest reach is authored in reference-world metres and is never scaled.** `fantasy_nest_threats` computes `reach = min(radius*0.5, max(350.0, range_m*2))`. `radius` is the **post-scaling** physical globe radius (31 831 m at the 200 km preset). `range_m` is `spacing_m * nest_spacing / 2` straight out of `terrain_nest_profiles.json`, whose `spacing_m` values are authored for the 11.15 km reference world and are **not** passed through `terrain_scale.reach_scale` — unlike `settlement_spacing`, `support_reach` and `culture_link_cost`, which `terrain_world` explicitly scales by `circumference/REFERENCE_CIRCUMFERENCE_M` (a factor of 17.94 at 200 km). The 350 m floor is likewise pre-scaling. Consequences: on the reference world the `radius*0.5 = 887 m` cap was the binding arm; on a 200 km production world the largest eligible nest reach is 3500 m (the max `spacing_m` among the 127 eligible species), against a settlement spacing of 8073 m and a cell pitch of 1562 m at size 129. So nest pressure fires only when a nest sits within about two cells of a city. **Is this intended?** `terrain_nests` itself imports `reach_scale` and applies it to `NEST_DANGER_SPAN_M`, so the module is aware of the scaling question and answered it differently for `range_m`. Either nest reaches are deliberately local (a lair threatens its own valley, not its region) or this is the same "reach stops spanning anything" defect that `terrain_world` fixed for the settlement reaches. I could not find a record that settles it.

3. **`second_uplift` is misnamed.** The layer holds `new_height − old_height`, the total delta including the transported-relief term, not the `uplift` variable computed two lines above it. A consumer reading it as "how much the boundaries lifted this epoch" gets the wrong quantity. No consumer exists in the repository, so this is free to rename — but renaming moves the layer set of every saved world.

4. **`magic['mutation_threshold']` and `magic['dominance_margin']` are published but not read.** `add_biome_variants` calls `dominant_school(potencies)` with no arguments, taking the function defaults `0.35` / `0.08`. The published block values happen to be the same numbers, so changing the block would change what the world *claims* without changing what it *does*. Decide whether the C++ threads the published values through or keeps them as constants and drops the block fields.

5. **The war branch discards `resolve_wars`'s `new_node_school`.** `resolve_wars` computes it with `_key_point_school`, which returns `None` when no school clearly held the ground. `age_transition` then overwrites it with `ruin_legacy(...)['school']`, which falls through source → region → culture and therefore *always* returns a school. So a war ruin always seeds a key point while `terrain_wars`' own docstring says "a war only seeds a key point where a clear school already held the ground". The wars module's computation is dead. Intended override, or a leftover? The doc prose (`docs/terrain-world-layers.md`) sides with the wars module: "leaves a leyline key point where `dominant_school` says a school already held its ground, the same test the leyline threat uses."

6. **`assess_city_threat` reads the unswayed ley field while `city_fate` reads the swayed one.** `ley_threat_pressure(city, layers)` is called without `lunar` in the assessment and with `lunar` in the fate. The `city_potencies` docstring justifies it ("the sway is momentary: it scales what the fate sees, never the exported field"), so this is probably deliberate — but it means `threat_assessments[].ley_pressure` and the ley weight that actually killed a city are different numbers for the same city in the same age, and nothing says so in the exported `method` string.

7. **`result['timing_ms']['total']` is overwritten, not accumulated.** The last two lines of `generate_history` set `total = history_total`, discarding whatever the stage passes accumulated into it. Harmless (the CLI strips timings for byte-reproducibility) but it means `total` is wall time of the history driver, not the sum of its parts — worth not "fixing".

8. **Pooled vs serial city planning is unproven.** Stated in the task brief and confirmed: `ICARUS_CITY_WORKERS` is read in `city_planner.py:537` and `hamlet_planner.py:302`, the pool is used whenever the variable is unset-or-not-1 and the site count is large enough, and the fallback is a `RuntimeWarning` rather than a failure. No test in the repository compares pooled output to serial output. Port serial.

## Addenda from the audit

The reader above wrote the specification; a second reader went back to the source
to find what it had left out or got wrong. Both are kept: the omissions are
content the implementer needs, and the corrections say where the specification
above is not to be trusted.

### Corrections

- **Claimed:** WRITES: `result['terrain']` — `{'version': 6, 'natural_biomes': natural_catalogue(), 'biomes': natural_catalogue(), 'magical_biomes': biome_catalogue(), 'biome_contract': <string>}`
  **Actually:** add_biome_variants does not create this block; it assigns five keys INTO a block `add_terrain_labels` already wrote (terrain_biomes.py:90), and raises KeyError if it is absent. The exported block is ten keys: version, biomes, natural_biomes, landforms, features, climate_seed, method, icons from add_terrain_labels, with version/biomes/natural_biomes overwritten and magical_biomes + biome_contract added. `method` is itself live exported prose ('Temperature remains a latitude/elevation proxy. Biome moisture now follows wind-transport rainfall; lake extents still assume equilibrium filling.', plus a marsh clause when wetland is on) and is also appended to result['warnings']. A port that writes the block as the spec describes silently drops landforms, features, climate_seed, method and icons from every world.

- **Claimed:** threat_assessments: 'Numeric fields are round(x, 6)', and contributors are '[one dict per nest threat, rounded to 6dp]'
  **Actually:** In a nest contributor, `weight` is round(...,6) but `distance_m` and `reach_m` are round(...,4). The full contributor is {'kind','weight':round(w,6),'nest_id','name','family','distance_m':round(d,4),'reach_m':round(r,4)}. In the report body only regional_threat, nest_pressure, ley_pressure and war_pressure are rounded to 6; `wars_fought` is a raw int, `enemy_living` is coerced with bool(), and wars_recent, war_risk, war_risk_kind, war_risk_opponent_uid, war_hunger, war_hunger_target_uid pass through from war_outlook unrounded.

- **Claimed:** EDGE CASES: 'Every raster this module writes is padded the same way: a pole row computes one value and repeats it n times; every other row computes n-1 values and appends row[0].'
  **Actually:** Only the five fields built in tectonic_transition's inner loop use that padding. `relic_incision` is built by node_grid (out[0] = values[0] repeated, out[-1] = values[-1] repeated, row[-1] = row[0], uncovered cells default 0.0). `biome_variant`, `biome`, `land`, the decremented `height`, `gorge`, `dry_valley`, `relic_gorge` and `relic_valley` are full n x n element-wise maps computed at every cell with no padding step — add_biome_variants explicitly loops `for x in range(cfg.size)` inside `for z in range(cfg.size)`. They come out seam- and pole-consistent only because their inputs already are.

- **Claimed:** EDGE CASES: 'Missing `ley_<school>` layer → potency 0.0 for that school (an explicit `if 'ley_'+name in layers` guard…)'
  **Actually:** That guard exists only in `city_potencies`. `add_biome_variants` builds `{s: l['ley_'+s][z][x] for s in SCHOOLS}` with no guard and raises KeyError on a missing ley layer — as the spec's own section F correctly shows. The rule is per-function, not module-wide, and the quoted justification ('missing is zero potency, not this city has no school') applies only to the fate path.

- **Claimed:** §I: `cities = sorted(result['settlements']['sites'], …)` and `nests = result['beast_nests']['sites']`
  **Actually:** Both reads are defensive in the source: `result.get('settlements',{}).get('sites',[])` and `result.get('beast_nests',{}).get('sites',[])`, and `spacing` falls back to `(result.get('config') or {}).get('settlement_spacing')` when the argument is None. Likewise age_transition calls city_fate with `result.get('beast_nests',{}).get('sites',[])` and resolve_wars with `result.get('humans',{}).get('cores',[])` / `result.get('roads',{}).get('routes',[])`. A port that dereferences these unconditionally throws where the Python returns an empty assessment.

- **Claimed:** §K step 4: `villains = advance_villains(result, cfg, age, o['villain_rise'], o['villain_hold'], o['villain_density'])`
  **Actually:** The source reads `float(o.get('villain_rise', 0.))`, `float(o.get('villain_hold', .7))`, `float(o.get('villain_density', 3.))` — the in-code fallbacks are 0.0 / 0.7 / 3.0, and the villain_rise fallback (0.0) differs from the option-registry default the spec quotes (0.5). Unreachable while options(cfg) always supplies the key, but it is the behaviour to port, and the same `.get` fallbacks are reused for promote_villains and villain_outlook.

- **Claimed:** C++ NOTES: 'FWGC++ already has measure_globe (measure.hpp) and land_area (world.hpp)', with the surrounding text implying `sample`, `direction` and the node graph still have to be built.
  **Actually:** FWGC++ also already provides `fwg::direction(int x, int z, int n)` and `fwg::sample(const Grid&, const Vec3&)` in include/fwg/grid.hpp, and its `sphere_grid` neighbour construction (pole-to-whole-adjacent-ring, 8-connected interior via node_at with longitude wrap, ascending-index neighbour lists, pole area multiplied by n-1) already matches the Python exactly. The stage-6 backtrace and the stage-7 shoulder pass have their primitives; the spec understates what is in place and would send a porter to rebuild them.

### Missing — blocking

**biome_catalogue() flat-index nesting is biome-major, not school-major**

`_block(schools) = [_entry(i,bid,core,school) for i,(bid,core) in enumerate(NATURAL_BIOMES.items()) for school in schools]` — the BIOME loop is outer. Within the known block, index = biome_position*8 + school_position: 0=ocean.weave, 1=ocean.umbral, ... 7=ocean.air, 8=tundra.weave, ... 103=land_ice.air. The hidden block is appended: index = 104 + biome_position*4 + hidden_school_position, so 104=ocean.blood, 105=ocean.void, 106=ocean.rot, 107=ocean.eldritch, 108=tundra.blood, ... 155=land_ice.eldritch. `biome_catalogue() = _block(KNOWN_SCHOOLS) + _block(HIDDEN_SCHOOLS)`. Biome positions are biomes.json file order (ids 0,1,2,3,4,5,6,7,8,13,15,16,17); school positions are SCHOOLS insertion order. A school-major build yields indices 0..155 too and is silently wrong in every saved world.

**biome_catalogue() entry record shape and key order (exported as terrain.magical_biomes, equality-checked by validate_age_world)**

`_entry(i,bid,core,school) = dict(id=f'{core}.{school}', core=core, core_biome_id=bid, magic_school=school, group=SCHOOLS[school][0], descriptors=SCHOOLS[school][1], name=VARIANT_NAMES[school][i], color=SCHOOLS[school][2], asset_id=f'terrain.mutation.{core}.{school}')` — nine keys in that insertion order (the source comment says both blocks go through one entry builder precisely so key order cannot drift, because the output bytes depend on it). `SCHOOLS[school]` is a 3-tuple (group, descriptors, colour): weave ('Raw magic','chaos, creation, creativity',[166,104,214]); umbral ('Raw magic','necrotic, entropy, death, order',[72,49,35]); infernal ('Holy / unholy','demons, hellscapes, corruption, evil',[154,43,49]); radiant ('Holy / unholy','holy, healing, order, peace, righteousness',[225,209,133]); fire ('Primordial','fire, heat, renewal, chaos',[207,86,37]); water ('Primordial','water, ice, currents, order',[65,156,202]); earth ('Primordial','nature, ground, stone, roots, order',[67,120,51]); air ('Primordial','wind, storms, chaos, force',[176,210,213]); blood ('Outside','sacrifice, lineage, vitality, hunger',[140,26,38]); void ('Outside','absence, entropy, unmaking, silence',[26,22,40]); rot ('Outside','disease, undeath, decay, rebirth',[124,130,62]); eldritch ('Outside','madness, wrong geometry, dominion of minds',[62,96,92]).

**natural_catalogue() entry record shape (exported TWICE, as terrain.biomes and terrain.natural_biomes, both equality-checked)**

`[dict(id=bid, core=core, name='Persistent land ice' if bid == 17 else core.replace('_',' ').capitalize(), color=list(NATURAL_COLORS[bid]), asset_id=f'terrain.biome.{bid:03d}') for bid, core in NATURAL_BIOMES.items()]` — five keys in that order, 13 records in biomes.json file order. Note the special-case name for id 17 and the zero-padded three-digit asset id (e.g. `terrain.biome.015` for boreal_forest, `terrain.biome.000` for ocean). NATURAL_BIOMES = {row['id']: row['core']}, NATURAL_COLORS = {row['id']: list(row['color'])}.

### Missing — important

**The verbatim `method` and `limits` strings written into result['threat_assessments']**

method = 'Per-city standing pressure from eligible fantasy nests (dragons and infernal/undead/aberrant families within reach) plus dominant local leyline potency, and the war outlook: wars fought this age, whether a former opponent still stands, and the strongest contention pressure a living neighbour exerts (the pair pressure the next age lottery draws against). Same nest eligibility and reach as age-fate weights; excludes magical self-destruction.'   limits = 'Artistic regional threat for fortification and shape preference, not creature counts, hostility AI or guaranteed attacks. Real-animal nests do not contribute. The war outlook is a forecast, not a promise, and does not move regional_threat.'   The spec renders both as `<string>` although it quotes the analogous geological_history.method in full.

**The refusal-message texts in validate_age_world that the spec did not quote**

missing_block('config', 'Age advancement requires a recipe 3 world generated through creatures (phase 13) on a grid of at most 1025. This world is recipe %s at phase %s, size %s.' % (world_recipe, phase, size)); missing_block('astrology','This world predates the moon. It is seeded during generation, so regenerate rather than migrate.'); missing_block('religion','This world predates the pantheon. Regenerate rather than migrate.'); missing_block('religion.catalogue','This world was built against a different pantheon catalogue, so the gods it names may not exist here.'); missing_block('magic','This world predates the twelve-school contract, so it carries no hidden networks to validate. Regenerate it.'); missing_block('settlements','This world carries a retired settlement or civilization version. Regenerate it.'); missing_block('civilizations.registry','The civilization registry changed since this world was generated, so the entities it names may no longer resolve. Regenerate or explicitly migrate it.'). Plus the bare ValueError texts, which are the only signal for most failures: 'Invalid moon '+key+' cycle', 'Invalid moon tilt', 'Invalid great year', 'Terrain detail identity mismatch; regenerate', 'City planner data changed; regenerate this world' (and Hamlet/Castle variants), f'Expected exactly 12 networks: the 8 known schools and the 4 hidden', 'Age history must be contiguous', 'Missing world layer: '+key, 'Invalid grid dimensions: '+key, 'Nonfinite or nonnumeric grid: '+key, 'Invalid sphere seam or pole: '+key, 'Unknown natural biome', 'Unknown biome catalogue', 'biome must carry natural IDs; legacy phenotypes are retired', 'Invalid biome variant', 'Magical biome core disagrees with natural biome', 'Physical scale disagrees with config', 'Invalid city civilization', 'Invalid city classification', 'Invalid or repeated active city node', 'City grid location disagrees with node', 'City direction disagrees with node', 'Invalid city identity', 'City classifications disagree with suitability', 'An active city occupies a ruin', 'Invalid network '+key, 'Leyline edit budget exceeded', 'Duplicate ley node id', 'Duplicate leyline id', 'Invalid leyline endpoints', 'Invalid line intensity'. All of these are wrapped: KeyError/TypeError/IndexError/OverflowError become ValueError('Malformed age world: '+str(exc)).

**The typed-refusal envelope the age API emits (terrain_errors.RequestError) — the spec says only 'the typed errors from terrain_errors'**

`RequestError(ValueError)` carries code/field/received/expected/suggestion/retry and serialises via `document(detail=True)` as {'schema':'fantasy-world-generator.failure','schema_version':2,'code':<code>,'message':str(exc),'field':<field>,'received':<value sent>,'expected':<bound or choice list>,'suggestion':<dict or {'kind':'none'}>,'retry':<bool>}; `document(detail=False)` drops received/expected/suggestion/retry and is the v1 shape. Exactly three codes: INVALID_INPUT, UNSUPPORTED_VERSION, STATE_CAPACITY. Suggestion `kind` values a caller may branch on without reading prose: 'clamp', 'choose', 'did_you_mean', 'none'. WHY it subclasses ValueError: every existing handler at the CLI, the lab server and the leaf packages already catches ValueError, so adopting it changed nothing for them. WHY the detail tier is switchable: whether `suggestion` actually reduces a small model's retry count is an empirical question and 'a field that cannot be switched off cannot be measured'. The constructors this module calls are cross_field, missing_block, out_of_range, unknown_field, unsupported_api, wrong_type, plus invalid_choice for the edit school (with `nearest()` folding prefix/substring matches ahead of difflib).

**advance_age_request request defaults and the bool-rejecting type checks**

`steps = body.get('steps', 1)` — absent means 1, not an error. `moon_day = body.get('moon_day')` — absent/None means the founding end_year decides the day. `edits = body.get('leyline_edits', [])`. All three checks use `type(x) is not int`, so a Python bool (and in C++ any non-integral JSON number) is refused even though bool is an int subclass. `api_version` must be present and exactly 1. An unknown request field raises naming `sorted(set(body)-allowed)[0]` — deterministic, the alphabetically first offender. Allowed request keys are exactly {'api_version','world','steps','leyline_edits','moon_day'}; allowed per-edit keys exactly {'school','node_id','line_id','intensity','new_node'}. `world = body.get('world')` is not type-checked here; a missing world reaches validate_age_world and fails its isinstance check. Validation order: dict → unknown fields → api_version → steps type → steps range → moon_day type → moon_day>=0 → edits list → edits<=128 → validate_age_world(world) → 'edits require magic_enabled' cross_field → adopt_world → per-edit checks inside the apply loop.

**ruin_legacy's returned record — the `legacy` dict stored verbatim on every ruin and read back for the ley key point**

`{'school': <str>, 'intensity': <float>, 'basis': <'source'|'region'|'culture'>}`. `basis` records WHICH fallback answered: 'source' if the destroyer had a school of its own, 'region' if `dominant_school(potencies)` answered, 'culture' if it fell through to `culture_schools().get(city['population_profile'], 'weave')`. Intensity = `CLASS_INTENSITY.get(city.get('city_class'), CLASS_INTENSITY['small'])` — the default for an unrecognised class is 1.5, not an error — then `max(intensity, SELF_MAGIC_FLOOR=2.5)` when cause == 'self_magic' and forced to DIVINE_INTENSITY=4.0 when the cause starts with 'divine'. The spec gives the numbers but never states that `basis` exists and is exported.

### Missing — minor

**sphere_grid's neighbour topology, which defines the carve_relics shoulder pass**

Interior node (x,z): neighbours = {index(x+dx, z+dz) for dx in (-1,0,1) for dz in (-1,0,1)} minus itself — an 8-connected Moore neighbourhood including diagonals, with longitude wrapped `x % (n-1)` and any z in (0, n-1) collapsing to the single pole node. A pole node's neighbours are EVERY node of the adjacent ring: {index(xx,1) for xx in 0..n-2} north, {index(xx,n-2) ...} south. Neighbour lists are emitted in ascending node index (`for j in sorted(ids)`) with distance `radius*acos(clamp(dot(v_i,v_j),-1,1))`, and a pole's area is the band wedge times (n-1). So a pole's shoulder is the max over the whole adjacent ring, not over three cells. (FWGC++'s existing fwg::sphere_grid already matches this, so the port is safe by luck rather than by the spec.)

**city_fate passes the ley cause's school into ruin_legacy's villain_school parameter**

`ruin_legacy(city, chosen[0], potencies, nest_family=chosen[3].get('family'), villain_school=chosen[3].get('school'))` — for a LEY cause `chosen[3]` is the `{'school','potency'}` dict, so the ley school arrives in the `villain_school` slot. Harmless only because `source_school` tests `if cause in SCHOOLS: return cause` before it reaches `if cause.startswith('villain')`. A port that reorders those branches, or that gives evidence a per-cause static type, changes behaviour.

**The RNG object is constructed even when a roll override is supplied**

`rng = random.Random(child_seed(seed,'city-fate-'+city['uid'],age)); draw = rng.random() if roll is None else roll` — the stream is seeded unconditionally but drawn from only when `roll is None`. Irrelevant for a per-city named stream, but a port sharing one RNG across cities would diverge; keep the stream keyed on (seed, 'city-fate-'+uid, age).
