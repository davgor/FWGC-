# Port specification: 08-settlements

Extracted from the Python `FantasyWorldGenerator` at commit `b67c99b` before that
repository was retired, then audited against the same source by a second reader.

**Source modules**

- `Sim/icarus_sim/terrain_settlements.py`

**Estimated C++ size:** roughly 2600 lines, excluding tests.

## Audit: NOT YET IMPLEMENTABLE

The spec is strong where it is hardest — the five Dijkstra variants, the lazy-deletion tie-break, the allocate_access (value,label,node) exact-equality ownership rule, the hexagonal packing ceiling and its measured justification, the founding turn structure, the RNG stream inventory including the two skipped randrange calls, the anchor-band metre collapse, and the twenty-nine ordering hazards all check out against the source line by line, as do its restatements of farming_potential, water_cost, allocate_access, biome_preference/biome_food_multiplier, node_grid, matches and eligible_civilizations in the neighbouring files. It is not implementable as written for five reasons. First, founding.py's `possible` flag is used in the spec's loop but never initialised, and its real semantics (reset to False at the top of every turn) are exactly the kind of thing an implementer would guess wrong — hoisting it out of the loop kills the `no_eligible_expansion` termination and shifts `end_year`, which is the next age's `start_year`. Second, `suitability_factors` is described as "the nine additive score terms" with only one of the ten keys named, so that published dict cannot be produced at all, and the spec also misses that `flood` and `magic` are published negated and that `magic` reads the raw layer rather than the species_hazard vector. Third, two of the three published `method` strings and the founding `definition` string are elided to `<prose>` despite the brief's instruction to quote content; they are world-output bytes. Fourth, `site['reason']` is actually `settlement.reason or default`, and since every shipped civilization authors a reason, a spec-following port writes the wrong string on 100 percent of sites. Fifth, `civilization_report` is described by field names only and omits region_index, the profile `civilization` splat, presentation, heritage and the `_color_rgb` filter, so `result['civilizations']` cannot be built. Separately, the C++ modelling note on `option['profiles']` inverts the source: building options collapse an empty profiles list to None (unrestricted), while layout feature counts keep it as an empty set (nobody) — a port that follows the note enables nothing where the Python enables everything.

**5 blocking gap(s)** were found and are recorded in the addenda below.

## Purpose

This subsystem decides where civilizations put their cities on a finished planet, how many people each city stands for, which people live there, what district/building programme each city draws, and how the cities are joined by roads. It is the first pass in which the world stops being physics and becomes inhabited: everything downstream (hamlets, fortresses, colleges, seasonal food, society, trade, wars, heroes, quests, NPCs) keys off `settlements.sites[]` and `roads.routes[]`. Concretely it (a) builds two derived scalar fields — freshwater distance and a flood-risk proxy — plus a per-civilization suitability field and a per-civilization food-potential field over the sphere node graph; (b) integrates those fields into a planetary carrying capacity and a per-people city quota; (c) runs a turn-based, seeded founding simulation that places cities of several peoples over simulated millennia with spacing, migration and diaspora rules; (d) names each city in its people's own constructed language; (e) attaches a node-scale "city layout" record naming the layout profile, building pack, district anchors and the asset requirement that follows; and (f) builds a minimum spanning forest of least-terrain-cost roads between the cities. It deliberately does NOT place buildings in metres — that is the separate stage-16 city/hamlet/castle planners' job.

## Where it sits in the pipeline

Runs as `add_settlements(result, cfg)` at stage 9 of the sixteen-stage ladder (the Python profile groups it under "Stages 10-12: civilization"; the conformance record calls it stage 9 — both refer to the same single call site), and again inside every age transition via `terrain_history.civilization()` → `rebuild_tail()`.

MUST already exist before it runs:
- Tectonics/hypsometry/noise/erosion → `layers['height']`, `layers['slope']`, `layers['tpi']`.
- Water routing (`add_water`) → `layers['water_type']` (0 dry, 1 ocean, 2 lake), `layers['water_depth']`, `layers['water_surface']`, `layers['river']`.
- Climate (`add_climate`) → `layers['temperature']`, `layers['moisture']`, `layers['rain_river']` (0/1 per node), and `result['climate']` must be truthy or the function returns immediately.
- Biome labelling → `layers['biome']` (natural biome id).
- Ecology (`add_environment`, recipe-3 only) → `layers['metal_richness']`, `layers['salinity']`, `layers['coastal_support']`, `layers['fishing_productivity']`, `layers['maritime']`.
- Leylines (`generate_networks`, which calls `evaluate_networks` at its end) → `layers['magic_hazard']` and one `layers['magic_risk_<civ_id>']` per entity civilization. If magic is off these are absent and the module substitutes zeros.
- Biome variants → `layers['biome_variant']` (flat index into the magical-biome catalogue) and `result['terrain']['magical_biomes']`.
- `result['effective_config']['globe_radius']` (metres), `result['timing_ms']`, `result['config']`, `result['derivation']['settings']`.
- On an age rebuild only: `result['_survivors']` (deep copies of the surviving `settlements.sites[]` records), `result['ruins']` (each with a `node`), and the previous `result['settlements']['founding']` block.

Consumes afterwards, in order, in the same generation: `add_humans` (hamlets, fortresses, cores — reads `settlements.sites`, `roads.routes`, `<civ>_support_reach`), `add_colleges`, `add_seasonal_food`, `add_world_society`, `add_nests`, `add_threat_assessments`, `add_religion`, then at stage 16 `fill_cities` / `fill_hamlets` / `fill_castles`. `city_planner` imports `FOUNDED_BY` and `PLAYER` from this module. `terrain_humans`, `terrain_magic`, `terrain_society` and `terrain_nomad_routes` import `road_cost_function` and `shortest_paths` from it, so those two are public API of this translation unit, not private helpers.

Guard: `if not result.get('climate') or cfg.phase < 7: return result` (no-op). Roads are only built when `cfg.phase >= 8`.

## Inputs

GEOMETRY
- `cfg.size` = n (grid edge, 3..1025). `result['effective_config']['globe_radius']` = r in metres.
- `sphere_grid(n, r)` → `points` (list of (x,z) per node, node 0 = north pole, node len-1 = south pole, rows 1..n-2 contribute n-1 nodes each), `areas` (m² per node; a pole node carries the whole polar ring's area), `graph` (adjacency list of (neighbour_index, great_circle_distance_m), 8-connected interior, poles adjacent to the whole adjacent ring, neighbour ids sorted ascending).
- `direction(x,z,n)` → unit Vec3 per node, stored on each site as `direction`.

LAYERS (all read as `layers[key][z][x]` per node, i.e. `values(key) = [layers[key][z][x] for x,z in points]`)
- `height` — metres above datum, unbounded, typically -4000..+4000.
- `water_type` — int 0 = dry land, 1 = ocean, 2 = lake.
- `slope` — degrees, 0..90.
- `magic_hazard` — 0..1; absent ⇒ zeros.
- `magic_risk_<civ_id>` — 0..1, per entity civilization; only used when `cfg.world_recipe` is set AND the layer exists, else falls back to `magic_hazard`.
- `temperature` — degrees C.
- `moisture` — 0..1.
- `rain_river` — 0/1 int, the climate-routed river set. NOTE: this is `rain_river`, NOT the hydrology `river` layer. Both exist and they differ.
- `salinity` — 0..1; read only when `cfg.world_recipe` is truthy.
- `metal_richness` — 0..1; read only when `cfg.world_recipe`, else resource is synthesised from Perlin noise.
- `water_surface` — metres, water table/lake surface elevation.
- `biome` — natural biome id ∈ {0 ocean, 1 tundra, 2 desert, 3 grassland, 4 forest, 5 exposed_rock, 6 snow, 7 rainforest, 8 lake, 13 marsh, 15 boreal_forest, 16 cold_tundra, 17 land_ice}.
- `biome_variant` — flat index into `result['terrain']['magical_biomes']`, or -1 for none. Resolved by `cell_variant(result,x,z)` to a string id like `"forest.earth"` or None.
- `tpi` — topographic position index, metres.
- `coastal_support` — 0..1, recipe-3 only.
- `water_depth` — metres, recipe-3 only (marine pass).
- `fishing_productivity` — recipe-3 only.
- `maritime` — 0..1, read indirectly by `environment_at`.

CONFIG (`cfg`)
- `phase` (int), `size`, `seed` (uint32), `world_recipe` (0 or 3), `auto_parameters` (0/1), `magic_enabled` (0/1).
- `population_profile` (string; `'mixed'` selects every entity civilization).
- `human_magic_limit` (0..1, default 0.45) — hazard ceiling for roads and for the aggregate-profile eligibility list.
- `support_reach` (metres, default 1000 pre-scale) — how far a city reaches for rural support land. SCALED by the caller: `reach_scale(circumference)` multiplies the authored 1000 m base, which is authored for the 11.148961626795008 km reference world.
- `settlement_spacing` (metres, default 450 pre-scale, same scaling) — minimum great-circle separation between founded cities.
- `settlement_count` (int 0..24, Config default 6, recipe-3 default 24) — a MAXIMUM, not a target.
- `road_max_grade` (0.01..1, default 0.35), `bridge_cost` (0..10000, default 200).

WORLD OPTIONS (`options(cfg)`, recipe-3 only)
- `fishing_reach` — metres, default 700, range 50..4000.
- `fish_productivity` — default 60, range 0..200, "food units/km²/year".

REGISTRY (civilizations.json + buildings.json, resolved by `civilization_registry`)
- `section('founding_rules')`: `years_per_round`=250, `origin_min_separation_degrees`=90, `diaspora_interval_years`=1000, `diaspora_bonus_per_parent`=1.
- `section('parent_races')`: {human: 95%, elf: 25%, dwarf: 60%} `founding_participation_percent`.
- `section('defaults')`: `resource_score_factor_label`="gnome_metal_affinity", `normal_site_reason`="Combined freshwater, slope, climate and flood-risk score", `outpost_site_reason`="Resource-driven difficult site".
- `section('city_classification')`: `medium_suitability_min`=0.65.
- `entity_rules(id)` per civilization: `parent_race_id`, `settlement{surface_habitat, world_habitat, freshwater_reach_multiplier, support_outside_habitat, coastal_score_weight, resource_score_weight, reason}`, `economy{fishing_reach_multiplier, winter_fishing_fraction, ...}`.
- `get_profile(id)` — the ~40-field numeric population profile (weights, ideals, tolerances, limits, biome preference tables). See `constants`.
- `building_pack_data()` and `layout_profile_data()` — see `data_files`.

SURVIVOR / AGE INPUTS
- `result['_survivors']` — list of whole prior site dicts. Presence of this key is what switches the module into "age rebuild" mode.
- `result['ruins']` — each `{'node': int, ...}`; those nodes are forbidden to new foundings.
- `result['settlements']['founding']['diaspora_bonus_used']`, `['used_civilizations']`, `['end_year']` — carried forward, but only when `'_survivors' in result`.

## Outputs

LAYERS (each written via `node_grid(values, points, n)`: scatter node values into an n×n grid, then row 0 := pole[0] repeated, row n-1 := pole[last] repeated, and every row's last column := its first column)
- `suitability` — 0..1. On a recipe world this is `score_sets[species_ids[0]]`, i.e. the FIRST entity civilization's field (`human_maritime` under the shipped civilization_order), not an aggregate. On a non-recipe world it is the aggregate `cfg.population_profile` field.
- `suitability_<civ_id>` — 0..1, one per species in `species_ids`.
- `flood_risk` — 0..1.
- `resource_potential` — 0..1.
- `freshwater_distance` — metres, with **-1 substituted for infinity** (unreachable).
- `life_capacity_<civ_id>` — food potential per node, 0..~1 (recipe/inferred worlds only).
- `<civ_id>_support_reach` — 0/1 int, only for civilizations whose `settlement.support_outside_habitat` is true (dwarf, frosthold_dwarf in shipped data).

BLOCKS
- `result['settlements']` = `{'version': 16, 'founding': <founding report>, 'population_profile': cfg.population_profile, 'sites': [...], 'seed': <settlement seed>, 'requested': int, 'method': <prose>}`. `requested` is `sum(population_budget.requested_cities.values())` when a budget exists, else `cfg.settlement_count`.
- `result['settlements']['sites'][k]` — **keyed by `id`, which MUST equal the index k**. Downstream code uses it as a direct list index into `humans.cores`, `seasonal_food.models` and per-site profile lists, so a mismatch joins the wrong rows silently. Fields:
  - `id` (int = index), `name` (str), `name_gloss` (str, hyphen-joined root slots), `population_profile` (civ id), `parent_race_id`,
  - `founding_year` (int), `migration_source_node` (int|None), `source_civilization_id` (str|None), `migration_distance_m` (float), `cultural_branch` (bool), `diaspora` (bool, absent→None), `diaspora_reason` (str|None), `diaspora_bonus` (bool|None) — these seven are copied with `.get()` so they are present-but-None on non-diaspora sites,
  - `founding_turn` (int, default 1), `founding_capital` (bool, default False),
  - `node` (int, node index — **the stable key across ages**), `x`, `z` (grid coords), `direction` (3-tuple unit vector), `height_m`, `outpost` (bool — ALWAYS False, see edge cases), `kind` — the constant string `'city'`,
  - `suitability` (0..1, from that species' field), `freshwater_distance_m` (float|**None** for infinite), `slope_degrees`, `temperature_c`, `moisture`, `flood_risk`, `resource_potential`,
  - `city_layout` (the node-scale layout record, version 2 — see algorithm step 12),
  - `building_pack_id` (str), `building_pack_seed` (uint32), `building_pack_version` (int = 3),
  - `reason` (str),
  - `suitability_factors` (recipe-3 only): dict of the nine additive score terms, with the last key named by `defaults['resource_score_factor_label']` = `"gnome_metal_affinity"`,
  - added by the population-budget pass: `population_estimate` (int), `urban_population_estimate` (int), `rural_population_estimate` (int),
  - added by `classify_cities`: `civilization_id`, `city_class` ∈ {capital, medium, small}, `classification_reason`,
  - carried from a survivor on recipe 3: `uid`, `source_culture`, `founded_age`, `name`, `war_history`, `absorbed_refugees`, `absorbed_bands`, `founded_by`.
- `result['roads']` = `{'version': 1 (2 if 'magic' in result), 'routes': [...], 'components': int, 'method': <prose>}`. Each route: `{'from': a, 'to': b, 'nodes': [node ids along the path, inclusive of both ends], 'length_m': float, 'cost': float, 'river_crossings': [[i,j], ...]}`. `from`/`to` are **site indices**, not node ids.
- `result['population_budget']` (only when `inferred`) = `{'version': 4, 'world_cap': int, 'allowances': {civ: int}, 'shares': {civ: float}, 'weighted_habitat_km2': {civ: float}, 'requested_cities': {civ: int}, 'calibration': {'residents_per_productive_km2': 100, 'minimum_city_region_residents': {civ: int}, 'spacing_city_ceiling': int, 'habitable_km2': float}, 'method': <prose>}`, plus later `placed_cities` {civ: int}, `allocated` (int), `unallocated` (int). On recipe 3 a second sentence is appended to `method`.
- `result['peoples']` = {civ_id: full population profile dict}, one per key in `budget['shares']`.
- `result['civilizations']` = `civilization_report(sites)` version 3: registry identity hash, heritage identity, `medium_suitability_min`, `capital_scope`, `parent_races`, presentation defaults, and one `entities[]` row per entity civilization with `city_count` and `capital_node`.
- `result['timing_ms']['settlements']`, `['roads']`, and `['total'] += whole-function elapsed`.
- `result['config']['settlement_count']`, `result['effective_config']['settlement_count']`, `result['derivation']['settings']['settlement_count']` — only when `cfg.auto_parameters` is set.

BLOCK KEY FIELDS (the "no uniform id" trap)
- `settlements.sites[]` → `id` (ordinal index) AND `uid` (`surface-city-{age}-{node}-{population_profile}`, minted later by `terrain_history.remember_cities`, carried here across rebuilds) AND `node` (stable). It also carries `kind`, but `kind` is the constant `'city'` and is NOT a key.
- `roads.routes[]` → no id at all; identified by the `(from, to)` site-index pair.
- `population_budget.allowances` / `.shares` / `.placed_cities` → keyed by civilization id string.
- `civilizations.entities[]` → `id`.
- `ruins[]` → `id` (`'ruin-' + uid`) and `node`.
- The word `tier` does not appear in this module at all, and neither does `camp`. Do not introduce either.

## Algorithm

Let N = number of nodes = `len(points)`.

── STEP 0. GUARD AND SETUP ────────────────────────────────────────────────
```
if not result['climate'] or cfg.phase < 7: return result
profile = get_profile(cfg.population_profile)          # the aggregate/"mixed" profile
building_packs = _city_building_packs()                # validate + normalise buildings.json
n = cfg.size;  r = effective_config['globe_radius']
points, areas, graph = sphere_grid(n, r)
```
Read all the node-scale layer vectors listed under `reads`.

── STEP 1. FRESHWATER DISTANCE (multi-source Dijkstra) ────────────────────
Sources: `freshwater = [i for i in range(N) if (water[i]==2 or river[i]) and (not cfg.world_recipe or salinity[i] < 0.2)]`
— i.e. lakes and rain-river cells, and on a recipe world only fresh ones (salinity < 0.2).

```
distances = [inf]*N;  source = [-1]*N;  queue = []
for i in freshwater: distances[i]=0; source[i]=i; push(queue,(0,i))
while queue:
    d,i = pop_min(queue)
    if d != distances[i]: continue                     # stale entry
    for (j, edge) in graph[i]:
        if water[j] == 1: continue                     # ocean is not walkable
        if d + edge < distances[j]:                    # STRICT <
            distances[j] = d+edge; source[j] = source[i]; push(queue,(d+edge,j))
```
`source[j]` propagates the originating freshwater node so the flood term can read that water body's surface level. Ties break by first arrival; because the heap orders by `(value, index)` the lower node index wins an exact tie. Ocean nodes keep `inf` and `source = -1`.

── STEP 2. RIVER DISTANCE (a second, unconstrained Dijkstra) ───────────────
`river_cells = [i for i in range(N) if river[i]]`, then `river_distances = _dijkstra_distances(graph, river_cells)`:
standard multi-source Dijkstra with `distances[source]=0` for every source (guarded by `if 0 <= source < N and distances[source] > 0`), relaxation `candidate < distances[neighbor]` strict, no terrain filter at all — **this one crosses water**. Used only for city-layout profile selection and for the anchor river buffer.

── STEP 3. SEED ───────────────────────────────────────────────────────────
`seed = child_seed(cfg.seed, 'settlements')`; `rng = random.Random(seed)` (this `rng` object is created and then never used — dead, but it consumes no stream from anything else).
`layout_profiles = _load_city_layout_profiles()`.

── STEP 4. RESOURCE, FLOOD, AGGREGATE SUITABILITY ─────────────────────────
For each node i with unit vector p = direction(x,z,n):
```
resource[i] = metal_richness[i]                                   if cfg.world_recipe
            = clamp01(0.5 + 0.8 * perlin3(4*p.x, 4*p.y, 4*p.z, seed))   otherwise
level       = water_surface[source[i]]   if source[i] >= 0 else 0
flood[i]    = exp(-distances[i]/80) * max(0, 1 - max(0, height[i]-level)/10)
score       = profile.water_weight    * exp(-distances[i]/profile.water_reach)
            + profile.slope_weight    * exp(-(slope[i]/profile.slope_comfort)^2)
            + profile.climate_weight  * exp(-((temp[i]-profile.temperature_ideal)/profile.temperature_tolerance)^2)
            + profile.moisture_weight * max(0, 1 - |wet[i]-profile.moisture_ideal|)
            + profile.resource_weight * resource[i]
            - profile.flood_penalty   * flood[i]
            + biome_preference(profile, biome[i], variant[i])
scores[i]   = clamp01(score - profile.magic_penalty * hazard[i])  if water[i]==0 else 0
```
`biome_preference(profile, core_id, variant_id)`: if `variant_id` names a HIDDEN-school variant, return `magic_biome_preferences.get(variant_id, -1.0)`; otherwise return `magic_biome_preferences.get(variant_id, biome_preferences.get(str(core_id), 0))`. The hidden-school default of −1 is a rule, not authored data: nobody has a cultural preference about a school they have never heard of, and without it corrupted ground would read as attractive as clean ground.

`eligible = [i for i in range(N) if water[i]==0 and slope[i] < profile.site_slope_limit and hazard[i] <= cfg.human_magic_limit]` — **computed and never used.** Dead. (`habitat_capacity(areas,scores,eligible,land_per_city)` is defined in the module and called by nothing but a unit test.)

On a recipe world `scores` is overwritten in step 9, so on recipe 3 this entire aggregate scoring pass exists only to produce `resource[]` and `flood[]`.

── STEP 5. SPECIES SET AND LANDMASS CONTEXT ───────────────────────────────
```
species_ids = civilization_ids() if cfg.population_profile=='mixed' else (cfg.population_profile,)
contexts    = landmass_context(points, areas, graph, water)
registry    = profiles()                         # every profile, entities + aggregates
rules       = {key: entity_rules(key) for key in species_ids}
defaults    = section('defaults')
entity_habitats[i] = set(eligible_civilizations(environment_at(result,x,z,contexts[i]), registry))
profiles_by_id     = {p: get_profile(p) for p in species_ids}
```
`landmass_context` — iterative flood fill over the land nodes (`water==0`) using the full sphere adjacency (so seam and pole links count). Components are discovered by repeatedly taking `min(remaining)` as the start, so component order is deterministic. Each node gets `{landmass_area_m2, landmass_fraction = area/total_land_area, largest_landmass: bool}`; a water node gets `{0.0, 0.0, False}`.

`environment_at` yields exactly eight fields: `landmass_area_m2`, `landmass_fraction`, `largest_landmass`, `abs_latitude = |90 - 180*z/(size-1)|`, `biome`, `temperature`, `moisture`, `maritime` (0.0 when the layer is absent).

`eligible_civilizations(env, registry)`:
```
eligible=[]; claimed=set()
for key in sorted(registry, key=lambda k: (registry[k].civilization.priority, k)):
    ent = registry[key].civilization
    if ent.kind != 'entity': continue
    if ent.allocation_group and ent.allocation_group in claimed: continue
    if matches(ent.habitat, env):
        eligible.append(key)
        if ent.allocation_group: claimed.add(ent.allocation_group)
```
The `allocation_group` makes habitats EXCLUSIVE: all six shipped human civilizations share group `human_geography`, so at most one human is eligible per cell — the lowest-priority-number match, ties broken by id. Shipped priorities: human_cold 0, human_desert 1, human_rainforest 2, human_large_island 3, human_maritime 4, human_heartland 5 (heartland has habitat `{}` so it always matches and is the catch-all). Non-human entities have `allocation_group: null` and never claim.

`matches(rule, env)`:
```
if rule is empty: True
if 'all' in rule: all(matches(r,env) for r in rule['all'])
if 'any' in rule: any(matches(r,env) for r in rule['any'])
value = env[rule['field']]            # KeyError if the field is not in this env dict
return ('in'     not in rule or value in rule['in'])
   and ('min'    not in rule or value >= rule['min'])
   and ('max'    not in rule or value <= rule['max'])
   and ('gt'     not in rule or value >  rule['gt'])
   and ('equals' not in rule or value == rule['equals'])
```

── STEP 6. PER-SPECIES FIELDS (the main scoring loop) ─────────────────────
For each `species` in `species_ids` **in registry order** (the `civilization_order` list), with profile p:
```
settlement = rules[species]['settlement'];  economy = rules[species]['economy']
species_hazard = magic_risk_<species>   if cfg.world_recipe and that layer exists
               = hazard (magic_hazard, or zeros)   otherwise
```
For each node i:
```
score = p.water_weight    * exp(-distances[i]/p.water_reach)
      + p.slope_weight    * exp(-(slope[i]/p.slope_comfort)^2)
      + p.climate_weight  * exp(-((temp[i]-p.temperature_ideal)/p.temperature_tolerance)^2)
      + p.moisture_weight * max(0, 1-|wet[i]-p.moisture_ideal|)
      + p.resource_weight * resource[i]
      - p.flood_penalty   * flood[i]
      + biome_preference(p, biome[i], variant[i])
      - p.magic_penalty   * species_hazard[i]
if cfg.world_recipe:
      score += coastal_support[i] * settlement.coastal_score_weight
      if settlement.resource_score_weight: score += settlement.resource_score_weight * resource[i]
field[i] = clamp01(score) if water[i]==0 else 0
```
Note the ordering difference from step 4: here the magic penalty is inside the sum before clamping; in step 4 it is subtracted after the sum and before the clamp. Numerically identical, but the recipe-only coastal/resource terms are added AFTER the magic penalty and BEFORE the clamp.

Habitat and eligibility for i:
```
habitat = species in entity_habitats[i]
environment = environment_at(result,x,z,contexts[i]) ∪ {
      resource: resource[i], slope: slope[i], height: height[i], tpi: tpi[i],
      variant: variant[i], coastal_support: coastal_support[i] if cfg.world_recipe else 0.0 }
habitat = habitat and matches(settlement['world_habitat' if cfg.world_recipe else 'surface_habitat'], environment)
if cfg.world_recipe and biome[i] == 17: habitat = False      # persistent land ice
mutation_ceiling = p.mutation_limit if cfg.world_recipe else min(cfg.human_magic_limit, p.mutation_limit)
safe     = (water[i]==0) and species_hazard[i] <= mutation_ceiling
suitable = habitat and safe and slope[i] < p.site_slope_limit
if settlement.freshwater_reach_multiplier is not None:
    suitable = suitable and distances[i] <= p.water_reach * settlement.freshwater_reach_multiplier
if suitable: candidates.append(i)
```
This second `environment` dict is a SUPERSET of the one used for `entity_habitats`; a settlement habitat rule may name `resource`/`slope`/`height`/`tpi`/`variant`/`coastal_support`, a civilization-identity habitat rule may not.

Food potential for i:
```
fresh = distances[i] if finite else -1
_, food = farming_potential(slope[i], temp[i], wet[i], flood[i], fresh, p.irrigation, p)
food *= biome_food_multiplier(p, biome[i], variant[i]) * (1 - species_hazard[i])
if cfg.world_recipe and biome[i] == 17: food = 0.0
potential[i] = food if (safe and slope[i] < p.work_slope_limit and (suitable or settlement.support_outside_habitat)) else 0.0
```
`farming_potential(slope, temp, wet, flood, freshwater_distance, adaptation, p)` returns `(natural, irrigated)` and this caller takes **the second**:
```
terrain    = exp(-(slope/p.food_slope_comfort)^2)
           * max(0, 1 - |temp-p.food_temperature_ideal| / p.food_temperature_tolerance)
           * (1 - 0.7*flood)
natural    = terrain * max(0, 1 - |wet-p.food_moisture_ideal| / p.food_moisture_ideal)
access     = exp(-freshwater_distance/p.water_reach) if freshwater_distance >= 0 else 0
irrigation = adaptation * access * max(0, (p.food_moisture_ideal - wet)/p.food_moisture_ideal)
return natural, natural + 0.75*terrain*irrigation
```
The comment on the 0.75: "Maintenance reduces the added exportable surplus; no source means no irrigation."

`biome_food_multiplier(p, core, variant)`: hidden-school variant → `food_magic_biome_multipliers.get(variant, 0.0)`; otherwise `food_magic_biome_multipliers.get(variant, food_biome_multipliers.get(str(core), 1))`.

SUPPORT PASS (only when `settlement.support_outside_habitat`, i.e. dwarf and frosthold_dwarf in shipped data):
```
support = reachable_support(graph, candidates, road_cost_function(points,water,height,flood,river,cfg,hazard), cfg.support_reach)
potential = [v if support[i] else 0.0 for i,v in enumerate(potential)]
layers[species+'_support_reach'] = node_grid([int(v) for v in support], points, n)
```
`reachable_support` calls `allocate_access(graph, [(node,k) for k,node in enumerate(starts)], cost, reach)` and returns `[owner[i] >= 0 and distance[i] <= reach for i]`. Note this uses the GLOBAL `cfg` (so `cfg.human_magic_limit` and `cfg.road_max_grade`) and the GLOBAL `hazard`, not the species' own limits — a known asymmetry with the road pass, which does use per-species limits on recipe worlds. The docstring records the intent: "Support must follow passable terrain, not a circle across water or cliffs."

`allocate_access(graph, seeds, cost, limit)` — multi-source Dijkstra with an owner label, priority queue ordered `(value, label, node)`:
```
for (node,label) in seeds: distance[node]=0.; owner[node]=label; push((0.,label,node))
while queue:
    (value,label,i) = pop_min()
    if value != distance[i] or label != owner[i]: continue
    for (j,d) in graph[i]:
        edge = cost(i,j,d);  if edge is None: continue
        cand = value + edge
        if cand <= limit and (cand < distance[j] or (cand == distance[j] and label < owner[j])):
            distance[j]=cand; owner[j]=label; parent[j]=i; push((cand,label,j))
```
The exact-tie rule "lower label wins" is the determinism guarantee; port it exactly.

MARINE PASS (recipe worlds only):
```
o = options(cfg);  depth = water_depth
starts = { j for i in candidates for (j,d) in graph[i]
           if water[j]==1 and depth[j] >= 0.1 and species_hazard[j] <= p.mutation_limit }
fishing_cost = water_cost(points, water, depth, species_hazard, p.mutation_limit, 0.1)
marine_distance,_,_ = allocate_access(graph, [(i,0) for i in starts], fishing_cost,
                                      o['fishing_reach'] * economy.fishing_reach_multiplier)
for i in range(N):
    if isfinite(marine_distance[i]): potential[i] = fishing_productivity[i] * o['fish_productivity'] / 100
```
`water_cost(points,water,depth,hazard,limit,draft)` returns `d` if both endpoints are ocean with `depth >= draft` and `hazard <= limit`, else None; a diagonal step additionally requires both corner cells `(x_i,z_j)` and `(x_j,z_i)` to pass the same test, unless either endpoint is a pole row. This is the "a boat cannot slip between two land corners" rule.

Because `water_cost` admits only water→water, only ocean nodes ever get a finite marine distance, so the overwrite only ever lands on cells whose land potential was already 0. This is how "bounded potentially reachable marine habitat" enters the capacity integral. It is NOT gated by the support pass.

Finally `score_sets[species]=field; habitats[species]=candidates; potentials[species]=potential`.

── STEP 7. CAPACITY AND QUOTAS ────────────────────────────────────────────
```
inferred = cfg.world_recipe or cfg.auto_parameters or cfg.population_profile=='mixed'
cap, allowances = life_capacity(areas, potentials)
```
`life_capacity(areas, potentials)` — "100 residents per fully productive km²: explicit provisional food calibration. Overlapping species claims divide each cell's best supported capacity; they never multiply that cell's capacity. Fractional residents remain unallocated."
```
totals = {p: 0.0 for p in potentials};  total = 0.0
for i, area in enumerate(areas):
    values = {p: field[i] for p,field in potentials.items()}
    weight = sum(values.values())
    if weight <= 0: continue
    capacity = area/1e6 * 100 * max(values.values())      # km² * 100 residents/km² * best claim
    total += capacity
    for p,value in values.items(): totals[p] += capacity * value / weight
return floor(total), {p: floor(v) for p,v in totals.items()}
```
Then per species:
```
footprints[species] = sum(areas[i]*score_sets[species][i] for i in habitats[species]) / 1e6   # km²
capacity            = city_capacity(footprints[species], allowances[species], p)
                    = min(floor(footprint / p.land_per_city_km2), allowance // p.minimum_founding_residents)
quotas[species]     = capacity if inferred else min(capacity, cfg.settlement_count)
if (not cfg.auto_parameters) and cfg.settlement_count == 0: quotas = {p: 0 for p in profiles}
```

── STEP 8. THE PLANETARY CEILING ──────────────────────────────────────────
Verbatim intent from the source: "The shared ceiling is the ground itself: cities must stand a settlement spacing apart, so habitable land divided by the densest packing of that separation is how many can exist at all. It is applied after every entity is assessed, so an early registry entry cannot exhaust the budget before a later one is considered."
```
habitable      = union of habitats[species] over all species          # a set
habitable_km2  = sum(areas[i] for i in habitable) / 1e6
cell_spacing_m = 2*pi*r/(n-1)
resolvable_spacing_m = max(cfg.settlement_spacing, cell_spacing_m)
per_city_km2   = sqrt(3)/2 * (resolvable_spacing_m/1000)^2            # hexagonal packing area
packing        = floor(habitable_km2 / per_city_km2) if per_city_km2 > 0 else 0
asked_for_fewer = cfg.settlement_count < default_config(cfg.world_recipe or 3).settlement_count   # i.e. < 24
if (not cfg.auto_parameters) and cfg.settlement_count == 0: limit = 0
elif inferred and not asked_for_fewer:                      limit = packing
else:                                                       limit = min(packing, cfg.settlement_count)
while sum(quotas.values()) > limit:
    key = max(quotas, key=lambda p: (quotas[p], p));  quotas[key] -= 1
```
The `max(quotas[p], p)` tie-break means the lexicographically LARGEST civilization id loses a city when two share the top quota. The loop runs `sum(quotas) - limit` times; it terminates because every quota is ≥ 0 and `limit ≥ 0`, and the max over a non-empty dict of non-negative ints with a positive sum always picks a positive one.

Why `max(spacing, cell_spacing)` — recorded verbatim because it was a real failure: "Cities cannot be packed closer than the raster can separate them. Spacing alone bounded this while the world was 11 km across and a cell was 87 m, but a coarse grid on a large world inverts that: at 200 km circumference a size-17 grid has 12.5 km cells against an 8 km spacing, so the area ceiling admitted about 63 cities onto 289 nodes — a city every four or five nodes, each then planning a full crop. The binding spacing is whichever is coarser."

And on the `inferred`/`asked_for_fewer` split: "Asking for no cities is not the same as asking for no ceiling, and it outranks the ground: a caller who wants an empty world gets one, inferred or not. A caller who lowers the ceiling is asking for fewer cities and gets them, inferred or not: `settlement_count` is published as 'Maximum surface cities', so a request for two that silently returns ten is a broken parameter rather than a ground rule. Leaving it at the recipe default still means 'no ceiling but the ground', which is what the inferred path is for."

── STEP 9. RECIPE SCORE SUBSTITUTION ──────────────────────────────────────
`if cfg.world_recipe: scores = score_sets[species_ids[0]]` — the published `suitability` layer becomes the first entity civilization's field.

── STEP 10. FOUNDING (found_cities) ───────────────────────────────────────
```
parents  = section('parent_races')
owners   = {civ: rules[civ]['parent_race_id'] for civ in profiles}
parents  = {k:v for k,v in parents.items() if k in owners.values()}      # prune unused races
survivors = [dict(s, parent_race_id=owners[s['population_profile']]) for s in result.get('_survivors',[])]
founding_distance(i,j) = r * acos(clamp(-1,1, dot(vectors[i], vectors[j])))     # great circle, metres
founded, founding = found_cities(
    seed, parents, owners, quotas, habitats, score_sets, founding_distance,
    cfg.settlement_spacing, math.pi*r, survivors,
    forbidden=[ruin['node'] for ruin in result.get('ruins',[])],
    food_allowances=allowances,
    minimum_residents={k: p.minimum_founding_residents for k,p in profiles.items()},
    city_limit=limit, magic_enabled=bool(cfg.magic_enabled),
    diaspora_bonus_used = prev founding.diaspora_bonus_used if '_survivors' in result else [],
    used_civilizations  = prev founding.used_civilizations  if '_survivors' in result else [],
    start_year = (prev founding.end_year + years_per_round) if '_survivors' in result else 0,
    **section('founding_rules'))
quotas = founding['effective_quotas']
selected = [s['node'] for s in founded]; peoples = [s['population_profile'] for s in founded]
outposts = []                                       # never appended to
```

THE FOUNDING SIMULATION IN FULL (founding.py, 99 lines, "Seeded parent-race founding rounds; geographic distances are physical metres"):
```
sites   = [dict(s) for s in survivors]                      # survivors occupy the list first
blocked = set(forbidden); events = []
quotas  = copy(quotas)
bonus_used = set(diaspora_bonus_used)
ever_used  = set(used_civilizations) | {s.population_profile for s in sites}
if food_allowances is not None:
    quotas = {k: min(v, food_allowances.get(k,0) // minimum_residents.get(k,40)) for k,v in quotas.items()}
origin_gap = world_reach * origin_min_separation_degrees / 180          # = pi*r*90/180 = quarter circumference
year_offset = max(start_year, max(s.founding_year for s in sites, default 0) + (years_per_round if sites else 0))
blocked_parents = set()
target = sum(quotas.values())
base   = max(spacing*2, world_reach/32, 1)
stop   = 'round_limit_reached'
```
`available(race)` returns every `(civ_key, node)` pair such that `owners[civ_key]==race`, that civ has fewer sites than its quota, the node is not blocked (a ruin), and the node is at least `spacing` metres from every existing site (`node != s.node and distance(node,s.node) >= spacing`). The pair list is generated in `owners` insertion order then `candidates[key]` order, i.e. ascending node index.

Main loop, `for turn in 1..128`:
1. `radius = min(world_reach, base * 1.6^min(turn-1, 32))` — the expansion front, exponentially growing and capped at turn 33.
2. `races = list(parents)` rotated left by `(turn-1) % len(races)`. Comment: "Rotate initiative so one parent does not always claim contested sites first."
3. For each race in that rotated order:
   - `if len(sites) >= target: break`
   - skip if the race is in `blocked_parents`
   - `members = [s for s in sites if s.parent_race_id == race]`; `pool = available(race)`; `pct = parents[race].founding_participation_percent`
   - `if not pool or ((turn > 1 or members) and pct == 0): continue`
   - FIRST-SETTLEMENT ORIGIN SEPARATION: if `not members`, filter pool to nodes at least `origin_gap - 1e-8` from every existing `founding_capital` site. If the filtered pool is empty, add the race to `blocked_parents`, emit event `{turn, parent_race_id, status:'no_separated_origin', radius_m: world_reach, required_separation_degrees}` and continue.
   - `possible = True`
   - PARTICIPATION ROLL: `draw = Random(child_seed(seed, f'founding-{turn}-{race}')).random() * 100`. If `(turn > 1 or members) and draw >= pct`, emit `{turn, parent_race_id, status:'did_not_participate', draw_percent:draw, radius_m:radius}` and continue. A parent's very first settlement on turn 1 is exempt from the roll.
   - UNUSED-CIV PREFERENCE: `unused = [pair for pair in pool if no site has that population_profile]`; if non-empty, `pool = unused`. A parent race spreads its civilizations before doubling up.
   - EXPANSION RADIUS: if `members`, keep only pairs within `radius` of the race's nearest existing member (`min(distance(node, s.node) for s in members) <= radius`). If empty, emit `{turn, parent_race_id, status:'search_radius_too_small', radius_m:radius}` and continue.
   - CHOOSE: `key, node = min(pool, key=lambda pair: (-scores[pair[0]][pair[1]], pair[0], pair[1]))` — highest suitability, then lowest civ id string, then lowest node index.
   - RECORD: `{node, population_profile:key, parent_race_id:race, founding_turn:turn, founding_capital: not members}` plus
     `founding_year = year_offset + (turn-1)*years_per_round`,
     `migration_source_node` / `source_civilization_id` from `source = min(members, key=lambda s: (distance(node, s.node), s.node))` (None when this is the race's first),
     `migration_distance_m = distance(node, source.node)` or 0,
     `cultural_branch = bool(source and source.population_profile != key)`.
   - Append, `ever_used.add(key)`, emit `{**record, status:'founded', turn, radius_m: world_reach if not members else radius, suitability: scores[key][node]}`.
4. DIASPORA (once per turn, after every race has acted):
   `year = year_offset + (turn-1)*years_per_round`; `options = diaspora_candidates()`.
   `diaspora_candidates()` returns `{}` if `food_allowances is None` or `city_limit is not None and len(sites) >= city_limit`. Otherwise, for each `(key, race)` in `owners`:
   - skip if `key in ever_used` or `food_allowances.get(key,0) < minimum_residents.get(key,40)`
   - `bonus = quotas[key] == 0`; skip if `bonus` and (`diaspora_bonus_per_parent` is 0, or `race in bonus_used`, or `city_limit is not None and target >= city_limit`)
   - skip if `len(sites) >= target and not bonus`
   - `pool = [node in candidates[key] not blocked and >= spacing from every site]`; if the race has no members yet, additionally require `distance(node, s.node) + 1e-8 >= origin_gap` for every `founding_capital` site.
   - if the pool is non-empty, `options[key] = pool`.
   If `options` and `year > 0` and `year % diaspora_interval_years == 0`:
   ```
   rng   = Random(child_seed(seed, f'diaspora-{year}'))
   key   = rng.choice(sorted(options))                  # SORTED keys — canonical
   race  = owners[key]
   node  = min(options[key], key=lambda n: (-scores[key][n], n))
   source = min(members of race, key=(distance, node index)) or None
   bonus  = quotas[key] == 0
   if bonus: quotas[key] = 1; target += 1; bonus_used.add(race)
   reasons = ['exile','religious_schism','expedition'] + (['magical_displacement'] if magic_enabled else [])
   record = {... founding_year: year, cultural_branch: bool(source), diaspora: True,
             diaspora_reason: rng.choice(reasons), diaspora_bonus: bonus}
   sites.append(record); ever_used.add(key)
   options = diaspora_candidates()                       # recompute
   ```
5. `if options: possible = True`
6. `if len(sites) >= target and not options: stop = 'capacity_target_reached'; break`
7. `if not possible: stop = 'no_eligible_expansion'; break`

The founding report (version 4) returned as `founding`: `minimum_residents`, `effective_quotas`, `diaspora_interval_years`, `diaspora_bonus_used` (sorted), `used_civilizations` (sorted), `start_year`, `end_year = year_offset + (turn-1)*years_per_round`, `years_per_round`, `elapsed_years = (turn-1)*years_per_round`, `origin_min_separation_degrees`, `turns`, `target_cities`, `placed_cities`, `stop_reason`, `events`, and a `definition` prose string.

`city_limit` semantics, recorded verbatim: "`city_limit` is None for no ceiling and a count otherwise, including zero. It used to be zero-for-none, which collided with a caller asking for no cities at all: quotas went to zero, and the diaspora bonus — whose whole purpose is to waive a zero quota — then had nothing left to stop it and settled the world anyway."

── STEP 11. POPULATION BUDGET BLOCK (only when `inferred`) ─────────────────
Write `result['population_budget']` version 4 as described under `writes`, with `shares[p] = allowances[p]/cap if cap else 0`. Write `layers['life_capacity_<species>']` for every species. If `cfg.auto_parameters`, publish `sum(quotas.values())` into `config`, `effective_config` and `derivation.settings.settlement_count` with the source string "quality-weighted eligible habitat and productive capacity per people; no minimum; ceiling is the densest packing of the settlement spacing over habitable land".

Then write `layers['suitability_<species>']` for every species.

── STEP 12. NAMING ────────────────────────────────────────────────────────
`named = _settlement_names(seed, selected, points, peoples)` → `{node: (name, gloss)}`.
```
lexicon = heritage.lexicon()
profile_by_node = {node: peoples[k] for k,node in enumerate(selected)}
resolved={}; named={}; taken=set()
for node in sorted(profile_by_node):                     # NODE ORDER, not founding order
    profile = profile_by_node[node]
    if profile not in resolved:
        resolved[profile] = heritage.resolve(profile, entity_rules(profile)['parent_race_id'])
    x,z = points[node];  domain = f'city-name-{node}-{x}-{z}-{profile}'
    for variation in range(8):
        draw = random.Random(child_seed(seed, domain, variation))
        name, gloss = heritage.settlement_name(resolved[profile], lexicon, draw)
        if name.lower() not in taken: break
    taken.add(name.lower());  named[node] = (name, gloss)
```
The docstring is load-bearing and should be carried: "This replaces a 24-word English list indexed by founding order. That index was the problem: `names[k%24]` renamed every later city when one was founded earlier, and it could not survive an age advance without the explicit copy-back downstream. Keying on the node and its coordinates instead gives a name that is a property of the place and the people, not of the order they happened to be founded in. Naming runs in node order rather than founding order so that collision resolution is canonical: whichever city is processed second re-draws, and which one that is must not depend on how the founding loop happened to be sequenced. The redraw is bounded and uses `child_seed`'s third parameter, so a duplicate yields another authentic name rather than a numeric suffix." The gloss is the English reading of the roots, so a consumer can say that Bargdorn is the wood-hold.

── STEP 13. SITE CONSTRUCTION AND THE NODE-SCALE LAYOUT ───────────────────
For each `k, i` in `enumerate(selected)`:
```
x,z = points[i];  outpost = i in outposts            # always False
city_seed = child_seed(seed, f'building-pack-{i}-{k}-{x}-{z}-{peoples[k]}')
building_pack_id = _pick_city_building_pack(city_seed, peoples[k], biome[i], height[i], slope[i],
                                            resource[i], distances[i] if finite else -1,
                                            building_packs, variant[i])
pack_entry = building_packs.pack_index.get(building_pack_id) or pack_index[fallback_pack_id]
city_river_distance = river_distances[i] if finite else inf
layout_profile = _pick_city_layout_profile(pack_entry, layout_profiles, city_river_distance)
anchor_candidates = _collect_land_anchor_candidates(points, graph, i, river_distances, water, slope,
                                                    layout_profile.placement.max_anchor_slope_degrees,
                                                    layout_profile.placement.river_buffer_m)
city_layout = _build_city_layout_plan(city_seed, i, points, layout_profile, peoples[k],
                                      None,                       # city_population is None here
                                      city_river_distance, graph[i], river, anchor_candidates, graph, pack_entry)
```
then append the site dict as specified under `writes`.

`_pick_city_building_pack(city_seed, profile_id, biome, height_m, slope_deg, resource, freshwater_m, catalog, variant_id)`:
```
candidates = []
for pack in catalog.packs:                                       # PACK ORDER from the JSON
    if pack.profiles is not None and profile_id not in pack.profiles: continue
    if (pack.biomes is not None or pack.biome_variants)
       and biome not in (pack.biomes or {}) and variant_id not in pack.biome_variants: continue
    if not (pack.height_min <= height_m <= pack.height_max): continue
    if resource < pack.resource_min or resource > pack.resource_max: continue
    if slope_deg > pack.slope_max: continue
    if not (pack.water_distance_min <= freshwater_m <= pack.water_distance_max): continue
    candidates.append((pack.id, pack.weight))
if not candidates: return catalog.fallback_pack_id
total = sum(weights);  draw = Random(city_seed).random() * total;  cursor = 0
for (pid, w) in candidates:
    cursor += w
    if draw <= cursor: return pid
return candidates[-1][0]
```
Note this creates its OWN `Random(city_seed)` and draws exactly once; it does not share a stream with the layout RNG, so both start from the same state.

`_pick_city_layout_profile(pack_entry, layout_profiles, river_distance_m)`:
```
riverfront = layout_profiles.profiles.get('riverfront_city_layout')
if pack_entry and pack_entry.layout_profile_id in layout_profiles.profiles: return that profile
if riverfront is None: return profiles[fallback_profile_id]
if isfinite(river_distance_m) and river_distance_m <= riverfront.placement.river_buffer_m * 1.8: return riverfront
return profiles[fallback_profile_id]
```
The forced pack layout wins first, so in shipped data the 1.8× band is only reached when the pack declares no layout — which none of the seven do. Effectively dead with shipped data, but keep it.

`_collect_land_anchor_candidates(points, graph, start, river_distances, water, slope, max_slope, min_river_distance)` — a **bounded Dijkstra with an in-loop predicate**:
```
max_walk = 800 if not isfinite(min_river_distance) else max(180, 4*min_river_distance + 180)
distances = [inf]*len(graph);  distances[start] = 0;  queue = [(0, start)]
candidates = []
while queue:
    d, node = pop_min()
    if d != distances[node]: continue
    if d > max_walk: continue                          # do not expand past the band
    if water[node] == 0 and slope[node] <= max_slope:
        if isinf(min_river_distance) or river_distances[node] >= min_river_distance:
            candidates.append((d, node))
    for (j, edge) in graph[node]:
        if d + edge < distances[j]: distances[j] = d+edge; push((d+edge, j))
candidates.sort(key=lambda item: (item[0], item[1]))   # distance, then node index
return candidates
```
With shipped layout profiles `min_river_distance` is 90 m (standard) or 70 m (riverfront), so `max_walk` is 540 m or 460 m. The `800` branch is unreachable from this module's callers. Note the relaxation does NOT apply the `max_walk` bound, so nodes beyond it get finite distances and are simply never expanded or collected.

`_build_city_layout_plan(city_seed, node, points, layout_profile, population_profile, city_population, river_distance_m, river_neighbors, river, anchor_candidates, graph, building_pack)`:
```
rng = random.Random(city_seed)                         # ONE stream for the whole plan
placement = layout_profile.placement
plan = {version: 2, layout_profile_id, layout_profile_name, population_profile,
        population_estimate: city_population}
fork_degree = count of (neighbour_id, _) in river_neighbors where river[neighbour_id]
plan['river'] = {adjacent_river_edges: fork_degree,
                 river_distance_m: river_distance_m if finite else None,
                 bridge_recommended: fork_degree >= placement.river_fork_bridge_threshold
                                     and placement.bridge_if_river_fork,
                 bridge_threshold: placement.river_fork_bridge_threshold}
ordered_nodes = de-duplicated node ids from anchor_candidates, preserving (distance, node) order
anchor_distance = {node_id: distance for (distance, node_id) in anchor_candidates}
                  # a LATER duplicate overwrites an earlier one; duplicates cannot occur here
cursor = rng.randrange(len(ordered_nodes)) if ordered_nodes else 0     # DRAW #1
if ordered_nodes: ordered_nodes = ordered_nodes[cursor:] + ordered_nodes[:cursor]   # rotate
cursor = 0;  used_nodes = set();  missing_anchors = False
for feature_name in ('leader_homes','barracks','noble_homes','worker_housing',
                     'apartments','market_district','religious_building'):        # FIXED ORDER
    spec  = layout_profile.features[feature_name]
    count = _resolve_feature_count(spec, city_population)
    if spec.profiles is not None and population_profile not in spec.profiles: count = 0
    anchors = []
    for candidate in ordered_nodes[cursor : cursor+count]:
        x,z = points[candidate]
        anchors.append({node: candidate, x, z, distance_to_city_m: anchor_distance.get(candidate, 0.)})
        used_nodes.add(candidate)
    cursor += len(anchors)
    if len(anchors) < count: missing_anchors = True
    features[feature_name] = {target_count: count, count: len(anchors), anchors}
if missing_anchors:
    plan['fallback'] = {'reason': 'Not enough valid non-water land anchors for requested feature counts within local search band.'}
building_plan = _build_city_building_plan(rng, city_seed, population_profile, city_population,
                                          plan.river.bridge_recommended, ordered_nodes, anchor_distance,
                                          river_neighbors, river, used_nodes, points, building_pack, graph)
plan['building_pack_id']       = building_pack.id
plan['required_assets']        = building_plan.required_assets
plan['required_asset_count']   = building_plan.required_asset_count
plan['required_node_slots']    = building_plan.required_node_slots
plan['asset_anchors_missing']  = building_plan.missing_anchors
if building_plan.missing_anchors:
    plan['fallback'] = {'reason': 'Not enough valid non-water land anchors for requested feature and building placement counts within local search band.'}   # overwrites the earlier reason
plan['features']    = features
plan['constraints'] = {max_anchor_slope_degrees, river_buffer_m, min_river_distance_m: river_buffer_m}
```
The deliberate omission is recorded in a comment that MUST be honoured: "`buildings` is deliberately not published. It carried a per-option target_count/placed_count pair that read as 'this city placed N buildings', beside a `city_plans` entry that had actually placed them — two answers to one question, and the wrong one reported a shortfall in every city, which hid the real ones. The asset requirement the draw produces survives as the three roll-ups below." Similarly `population_estimate` was renamed from `residents` "so the figure names the axis it measures... It was `residents`, which reads as people standing in the city; it is a demographic estimate two orders of magnitude above the beds the plan builds."

`_resolve_feature_count(feature, residents)`:
```
base = feature.base
if residents is not None: base += feature.per_100_residents * (max(0, residents) / 100)
count = floor(base)
if feature.min is not None: count = max(count, feature.min)
if feature.max is not None: count = min(count, feature.max)
return max(0, count)
```

`_build_city_building_plan(rng, city_seed, population_profile, city_population, bridge_recommended, ordered_nodes, anchor_distance, river_neighbors, river, used_nodes, points, building_pack, graph)`:
```
graph_neighbors = graph or []
ordered_node_set = set(ordered_nodes)
river_frontier_nodes = [nid for nid in ordered_nodes
                        if any(river[nb] for (nb,_) in graph_neighbors[nid] if 0 <= nb < len(river))]  if graph else []
boundary_nodes = [nid for nid in ordered_nodes
                  if any(nb not in ordered_node_set for (nb,_) in graph_neighbors[nid])]  if ordered_node_set and graph else []
option_plans = {};  required_assets = {};  required_node_slots = 0;  missing_anchors = False
anchor_cursor = rng.randrange(len(ordered_nodes)) if ordered_nodes else 0        # DRAW #2
wall_cursor   = 0
```
`_collect_anchors(candidates, target_count, cursor)`:
```
if target_count <= 0: return [], cursor, False
if not candidates:    return [], cursor, True
n = len(candidates);  anchors = [];  attempts = 0;  max_attempts = target_count * max(1, n)
cursor %= n
while len(anchors) < target_count and attempts < max_attempts:
    node_id = candidates[cursor];  cursor = (cursor+1) % n;  attempts += 1
    if node_id in used_nodes: continue
    used_nodes.add(node_id)
    x,z = points[node_id]
    anchors.append({node: node_id, x, z, distance_to_city_m: anchor_distance.get(node_id, 0.), sequence: len(anchors)})
return anchors, cursor, len(anchors) < target_count
```
`used_nodes` is shared with, and already populated by, the district-feature pass, so buildings never land on a district node. `anchor_cursor` is threaded through the anchored and river_gate placements (mutated across options); `wall_cursor` is separate and used only by wall placements.

Then for each `option` in `building_pack.building_options` **in pack order**:
```
if option.profiles is not None and population_profile not in option.profiles: continue
target_count = _resolve_feature_count(option.count, city_population)
if target_count == 0: emit an all-zero plan row and continue  (no RNG consumed)
planned_count = target_count
switch option.placement:
  'anchored':   anchors, anchor_cursor, has_missing = _collect_anchors(ordered_nodes, target_count, anchor_cursor)
                planned_count = len(anchors)
  'river_gate': if option.requires_bridge and not bridge_recommended:
                     anchors = []; has_missing = option.required; planned_count = 0
                else:
                     gate_candidates = list(reversed(river_frontier_nodes)) if river_frontier_nodes else []
                     anchors, anchor_cursor, has_missing = _collect_anchors(gate_candidates, target_count, anchor_cursor)
                     planned_count = len(anchors)
  'wall':       wall_candidates = sorted(boundary_nodes, key=lambda nid: anchor_distance.get(nid,0.), reverse=True)
                                  if boundary_nodes else list(reversed(ordered_nodes))
                anchors, wall_cursor, has_missing = _collect_anchors(wall_candidates, target_count, wall_cursor)
                planned_count = len(anchors)
  'structural': anchors = []; has_missing = False       # planned_count stays target_count
  default:      raise ValueError(f'Unknown placement {placement} for building option {id}')
if has_missing: missing_anchors = True
selected_assets = [_weighted_pick(rng, option.asset_choices) for _ in range(planned_count)] if option.asset_choices else []
for a in selected_assets: required_assets[a] += 1
asset_requirements = per-option tally of the same
required_node_slots += len(anchors)
skip_reason = 'requires_bridge' if (placement=='river_gate' and option.requires_bridge and not bridge_recommended) else None
option_plans[option.id] = {option_id, name, target_count, placed_count: len(anchors), required, placement,
                           tags: sorted(option.tags), anchors, assets: selected_assets, asset_requirements,
                           bridge_required: option.requires_bridge, skip_reason, required_node_slots: len(anchors)}
required_rows = [{asset_id, count} for asset_id in sorted(required_assets)]
return {version: 1, city_seed, options: option_plans, required_assets: required_rows,
        required_asset_count: sum(required_assets.values()), required_node_slots, missing_anchors}
```
`_weighted_pick(rng, choices)`:
```
total = sum(c.weight for c in choices)
if total <= 0: return choices[0].asset_id
draw = rng.random() * total;  cursor = 0.
for c in choices: cursor += c.weight; if draw <= cursor: return c.asset_id
return choices[-1].asset_id
```
Note `structural` still draws `target_count` assets even though it places no anchors, which is the only way the RNG stream depends on a placement that puts nothing on the ground. No shipped option uses `structural`, but a C++ port must reproduce it or a future data edit will diverge.

── STEP 14. SURVIVOR CARRY-BACK (recipe 3 only) ───────────────────────────
```
previous = {s['node']: s for s in result.get('_survivors', result.get('settlements',{}).get('sites',[]))}
for site in sites:
    old = previous.get(site['node'])
    if old and old['population_profile'] == site['population_profile']: carry_survivor(site, old)
```
`carry_survivor(site, old)` copies, in place, any of `('uid','source_culture','founded_age','name','war_history','absorbed_refugees','absorbed_bands','founded_by')` present on `old`, and additionally copies `old['city_layout']` when `authored_layout(old)` — i.e. when `old['founded_by'] == 'player'` and `old` has a `city_layout`.

The comment is the specification of why: "war_history travels with the city: a survivor that rebuilds its hinterland is still the city that fought, and its forts answer to that. absorbed_refugees and absorbed_bands travel for the same reason and are the only way absorption can persist at all: `population_estimate` is re-derived from the population budget on every rebuild, so refugees written into it at an age boundary would be erased by the next one." And on layouts: "A site the player founded with `layout: 'player'` owns where its buildings go; re-deriving the layout from the ground would replace that with a planner's answer for the same node."

── STEP 15. POPULATION SPLIT AND LAYOUT REBUILD ───────────────────────────
```
if 'population_budget' in result:
  budget = result['population_budget']
  if cfg.world_recipe: budget['method'] += ' New recipe includes bounded potentially reachable marine habitat, shared across peoples once per cell. Actual fishing delivery requires allocated hamlets and is assessed separately.'
  for species, allowance in budget['allowances'].items():
      members = [s for s in sites if s.population_profile == species]
      for index, site in enumerate(members):                      # site order, not node order
          residents = allowance // len(members) + (1 if index < allowance % len(members) else 0)
          residents += int(site.get('absorbed_refugees') or 0)
          urban = round(residents * 0.55)
          site.population_estimate       = residents
          site.urban_population_estimate = urban
          site.rural_population_estimate = residents - urban
          if authored_layout(site):
              site.city_layout.population_estimate = residents
              continue                                            # do NOT re-derive
          layout_profile = layout_profiles.profiles[site.city_layout.layout_profile_id]
          site_river_distance = river_distances[site.node]
          site_pack_entry = pack_index.get(site.building_pack_id, pack_index[fallback_pack_id])
          anchors = _collect_land_anchor_candidates(points, graph, site.node, river_distances, water, slope,
                        layout_profile.placement.max_anchor_slope_degrees,
                        layout_profile.placement.river_buffer_m)
          site.city_layout = _build_city_layout_plan(site.building_pack_seed, site.node, points, layout_profile,
                        site.population_profile, residents,
                        site_river_distance if finite else inf,
                        graph[site.node], river, anchors, graph, site_pack_entry)
  budget.placed_cities = {p: count of sites with that profile for p in budget.shares}
  result['peoples'] = {p: get_profile(p) for p in budget.shares}
  budget.allocated   = sum(s.population_estimate for s in sites)
  budget.unallocated = budget.world_cap - budget.allocated
```
So on any inferred world every city's layout is built **twice** — once with `city_population = None` in step 13, then discarded and rebuilt with the real resident count. Both use the same `city_seed`, so the second build starts the RNG stream from scratch; the two are not chained. `round()` here is Python's banker's rounding (half to even), which matters at exact .5 — use the same or state the divergence.

Remainder distribution: the first `allowance % len(members)` members of the species (in site order, which is founding order, which puts survivors first) each get one extra resident.

The refugee comment: "Refugees a city took in at an age boundary are people the capacity split cannot know about: the split is derived from the ground this city farms, and these walked in from a town that fell. Added after it, so the split stays a pure function of habitat and the arrivals stay auditable in `absorbed_bands`. Zero on every generated world: nothing writes `absorbed_refugees` before stage 16, and absorption runs at an age boundary."

── STEP 16. PUBLISH FIELDS, CLASSIFY, EMIT ────────────────────────────────
Write the four node grids (`suitability`, `flood_risk`, `resource_potential`, `freshwater_distance` with −1 for inf).
`classify_cities(sites)`:
```
threshold = section('city_classification')['medium_suitability_min']     # 0.65
group sites by population_profile (dict insertion order = site order)
for each group:
    capital = min(members, key=lambda s: (not s.founding_capital, -s.suitability, s.node))
    for site in members:
        site.civilization_id = key
        site.city_class = 'capital' if site is capital else 'medium' if site.suitability >= threshold else 'small'
        site.classification_reason = 'Founding capital retained; otherwise highest suitability, node breaks ties'
                                     if capital else f'Suitability >= {threshold:g}' / f'Suitability < {threshold:g}'
```
Note `site is capital` is IDENTITY, not equality. Exactly one capital per non-empty civilization.
`result['civilizations'] = civilization_report(sites)`.
Emit `result['settlements']` version 16 with its method prose.
`result['timing_ms']['settlements'] = elapsed`.

── STEP 17. ROADS (cfg.phase >= 8) ────────────────────────────────────────
```
cost = road_cost_function(points, water, height, flood, river, cfg, hazard)
candidates = [];  targets = {s.node for s in sites}
for a, site in enumerate(sites):
    if cfg.world_recipe:
        p = profiles[site.population_profile]
        risk = magic_risk_<that profile> if present else hazard
        cost = road_cost_function(points, water, height, flood, river,
                                  replace(cfg, human_magic_limit=p.mutation_limit,
                                               road_max_grade=p.road_grade_limit), risk)
    distance, parent = shortest_paths(graph, site.node, cost, targets)
    for b in range(a+1, len(sites)):
        goal = sites[b].node
        if not isfinite(distance[goal]): continue
        path = walk parent[] back from goal to site.node, append, reverse    # inclusive of both ends
        candidates.append((distance[goal], a, b, path))
        if cfg.world_recipe:
            other      = profiles[sites[b].population_profile]
            other_risk = magic_risk_<other> if present else hazard
            other_cost = road_cost_function(..., replace(cfg, human_magic_limit=other.mutation_limit,
                                                              road_max_grade=other.road_grade_limit), other_risk)
            if any(other_cost(i,j, the graph edge length i→j) is None for (i,j) in zip(path, path[1:])):
                candidates.pop()                                 # veto: not passable for the far end's people
```
`road_cost_function(points, water, height, flood, river, cfg, hazard)` returns a closure:
```
n = cfg.size;  lookup,_ = sphere_index(points)
cost(i, j, d):
    if water[i] or water[j] or d <= 0: return None                 # any water, lake or ocean
    if hazard is not None and max(hazard[i], hazard[j]) > cfg.human_magic_limit: return None
    (xi,zi) = points[i];  (xj,zj) = points[j]
    # "A diagonal cannot slip between two water-filled corner cells."
    if xi != xj and zi != zj and zi not in (0, n-1) and zj not in (0, n-1):
        if water[lookup[(xi,zj)]] or water[lookup[(xj,zi)]]: return None
        if hazard is not None and max(hazard[lookup[(xi,zj)]], hazard[lookup[(xj,zi)]]) > cfg.human_magic_limit: return None
    grade = |height[j] - height[i]| / d
    if grade > cfg.road_max_grade: return None
    return d * (1 + 12*grade^2 + 0.5*(flood[i]+flood[j])) + (cfg.bridge_cost if river[i] or river[j] else 0)
```
`shortest_paths(graph, start, cost, targets=None, limit=inf)` — Dijkstra with early exit and an optional cost bound:
```
distances=[inf]*N; parent=[-1]*N; distances[start]=0; queue=[(0,start)]
remaining = set(targets) if targets is not None else None
while queue:
    value, i = pop_min()
    if value != distances[i]: continue
    if remaining is not None:
        remaining.discard(i)
        if not remaining: break
    for (j, d) in graph[i]:
        edge = cost(i,j,d);  if edge is None: continue
        cand = value + edge
        if cand <= limit and cand < distances[j]:
            distances[j] = cand; parent[j] = i; push((cand, j))
```
The `limit` proof, quoted because it justifies the optimisation: "A caller that discards every result past a reach was still paying for the whole component: the sea routes in `add_world_society` ran 3 828 unbounded searches over the ocean at size 128 and kept four of them, because `sea_reach` is 6 000 m against a 5 834 m median edge. Stopping at the reach is not an approximation — edge costs are non-negative, so every prefix of a path costing `limit` or less also costs `limit` or less, and a node inside the bound keeps the same distance and the same parent it had when the search was unbounded. Nodes outside it stay at infinity rather than taking their true value, so read a bounded result only inside the bound." This module calls it with `limit=inf` but other modules rely on the bound.

MINIMUM SPANNING FOREST (Kruskal, union-by-assignment, no path compression):
```
groups = list(range(len(sites)));  roads = []
root(i): while groups[i] != i: i = groups[i]; return i
for (value, a, b, path) in sorted(candidates):            # by cost, then site a, then site b
    ra, rb = root(a), root(b)
    if ra == rb: continue
    groups[ra] = rb
    length = sum(the graph edge length for each consecutive pair in path)
    crossings = [[i,j] for (i,j) in zip(path, path[1:]) if river[i] or river[j]]
    roads.append({from: a, to: b, nodes: path, length_m: length, cost: value, river_crossings: crossings})
result['roads'] = {version: 1, routes: roads, components: len({root(i) for i in range(len(sites))}),
                   method: 'Minimum spanning forest of shortest terrain-cost routes. Lakes/ocean and edges above max grade are forbidden. River crossings incur a penalty and are bridge candidates, not engineered bridges. Disconnected islands remain disconnected.'}
result['timing_ms']['roads'] = elapsed
```

── STEP 18. MAGIC PROSE AND TOTAL TIMING ──────────────────────────────────
```
if 'magic' in result:
    settlements.method += ' All city sites must pass the mutation hazard limit, including difficult-site cities.'
    if 'roads' in result:
        roads.version = 2
        roads.method += ' Mutation above the human safety limit also forbids road nodes and diagonal corners.'
result['timing_ms']['total'] += whole-function elapsed
return result
```

## Constants

MODULE CONSTANTS
- `CITY_LAYOUT_VERSION = 2` — the version stamped on every `city_layout`. Version 2 dropped the `buildings` key and renamed `residents` to `population_estimate`. Bumping it is a world-output schema change.
- `FOUNDED_BY = 'founded_by'`, `PLAYER = 'player'` — the key/value marking a player-founded site. Exported and imported by `city_planner` and `terrain_settlement_api`; they live here "so the two readers can see them without a circular import".
- `CARRIED_SURVIVOR_KEYS = ('uid','source_culture','founded_age','name','war_history','absorbed_refugees','absorbed_bands','founded_by')` — removing any one of these silently loses that property at every age boundary.
- `settlements` block version `16`; `roads` version `1` (bumped to `2` when magic is present); `population_budget` version `4`; building-plan sub-record version `1`; `civilizations` report version `3`; founding report version `4`.
- Building pack schema must equal exactly `3`; layout profile `schema_version` must be an int (any).

FIELD FORMULAS
- `flood[i] = exp(-distances[i]/80) * max(0, 1 - max(0, height[i]-level)/10)`. **80 m** is the freshwater decay length of flood exposure; **10 m** is the elevation above the water surface at which flood risk reaches zero. Both are absolute metres tuned for the ~11.15 km reference world and are NOT scaled by the caller. On a 200 km world a single cell is ~12.5 km, so `exp(-12500/80)` is zero everywhere but the source cells themselves: flood risk effectively collapses to "on a water cell or not". Raising 80 spreads flood inland; raising 10 makes hills flood too.
- Resource fallback (non-recipe): `clamp01(0.5 + 0.8*perlin3(4p.x, 4p.y, 4p.z, seed))`. **4** is the noise frequency in unit-sphere units (roughly four features across a hemisphere); **0.8** is the amplitude and **0.5** the midpoint, so the field spans [0,1] with saturation at both ends.
- `resolvable_spacing_m = max(cfg.settlement_spacing, 2*pi*r/(n-1))`.
- `per_city_km2 = sqrt(3)/2 * (resolvable_spacing_m/1000)^2` — the area of a hexagon in a densest packing of circles of that diameter. sqrt(3)/2 ≈ 0.8660254037844386. Lowering it admits more cities.
- `residents_per_productive_km2 = 100` — "explicit provisional food calibration", appears as a literal `*100` in `life_capacity` and as a published calibration field. The method prose explicitly disclaims it: "Provisional 100 residents per fully productive km2, not validated agricultural yields."
- `urban share = 0.55`, published as `urban_population_estimate = round(residents*0.55)`; rural is the remainder so the two always sum to the whole.
- `farming_potential`: flood damping `1 - 0.7*flood`; irrigation surplus factor `0.75`.
- Road cost: `d * (1 + 12*grade^2 + 0.5*(flood[i]+flood[j])) + bridge_cost`. **12** is the grade penalty gain (at the 0.35 default max grade a segment costs 1+12·0.1225 = 2.47× its length); **0.5** is the flood-avoidance gain; `bridge_cost` default 200 (cost units, i.e. metre-equivalents) is charged once per river-touching edge.
- Anchor search band: `max_walk = max(180, 4*river_buffer_m + 180)` metres, or 800 m when the buffer is infinite (unreachable from this module). **180 m** is the floor; **4×** is how far past the buffer the search is willing to walk. Both are absolute metres for the reference world — see the known-defect note in edge cases.
- Layout selection band: `river_distance_m <= riverfront.river_buffer_m * 1.8` (= 126 m with the shipped 70 m buffer).
- Freshwater salinity cutoff: `< 0.2` (recipe worlds only) — above that a lake is brackish and does not count as freshwater.
- Land-ice biome id `17` zeroes both habitat and food on recipe worlds.
- Marine draft minimum `0.1` m and `depth >= 0.1` for a fishing start cell.
- Marine yield: `fishing_productivity * fish_productivity / 100` (fish_productivity default 60, so ×0.6).
- `origin_gap = world_reach * origin_min_separation_degrees/180` with `world_reach = pi*r` (half circumference) and 90 degrees ⇒ a quarter of the circumference between two parent races' first capitals.
- `base = max(spacing*2, world_reach/32, 1)` and `radius = min(world_reach, base * 1.6^min(turn-1, 32))` — the expansion front. **1.6** per 250-year round; the exponent caps at 32, i.e. the front stops growing after turn 33 (1.6^32 ≈ 2.1e6, long past `world_reach` for any world).
- Founding round cap `128`; name-redraw cap `8` variations; `heritage.settlement_name` has its own internal `REDRAW_LIMIT`.
- Epsilon `1e-8` on the two origin-separation comparisons (`distance + 1e-8 >= origin_gap`), so a site exactly at the gap passes despite float error.
- `medium_suitability_min = 0.65` — the suitability at or above which a non-capital city is `medium` rather than `small`.
- Parent participation percentages: human 95, dwarf 60, elf 25. A parent's first settlement is exempt from the roll; every later one must draw below its percentage.
- `years_per_round = 250`, `diaspora_interval_years = 1000` (must be an exact multiple of years_per_round — the registry validator enforces it), `diaspora_bonus_per_parent = 1`.
- Default `minimum_founding_residents` fallback inside `found_cities` is **40** when the caller omits a species; shipped values are 40 except human_cold and frosthold_dwarf at 30.
- Config defaults relevant here: `human_magic_limit 0.45`, `support_reach 1000 m` (pre-scale), `settlement_spacing 450 m` (pre-scale), `road_max_grade 0.35`, `bridge_cost 200`, `settlement_count 6` (Config) / 24 (recipe 3 default_config).
- Reach scaling: `settlement_spacing` and `support_reach` are multiplied by `reach_scale(circumference)` against the reference circumference `11148.961626795008 m`, and are capped at `REACH_MAX_M = 100000`, above which the caller raises `STATE_CAPACITY` rather than clamping. So every distance this module compares against `cfg.settlement_spacing` or `cfg.support_reach` is POST-scaling, while every literal inside this module (80, 10, 180, 800, 90, 70, 2500, 4500, 100) is PRE-scaling reference-world metres that nothing ever scales.

## Data files

Two logical catalogues, both physically in `Sim/icarus_sim/buildings.json` (schema `fantasy-world-generator.building-registry`, `schema_version: 2`), resolved and profile-annotated by `civilization_registry`. The sibling files `terrain_city_building_packs.json` and `terrain_city_layout_profiles.json` are seven-line REFERENCE STUBS pointing at `buildings.json` — they contain no data and can be ignored by a port.

── BUILDING PACKS: `buildings.json → building_packs` ──────────────────────
Shape: `{schema_version: 3, description, fallback_pack_id: "city_pack_generic_surface", packs: [...]}`.
`building_pack_data()` deep-copies it and then INJECTS two derived fields from `civilizations.json`:
- `pack.criteria.population_profiles` := every entity id whose `buildings.packs` binds this pack.
- `option.profiles` := every entity id that lists this option under that pack.
- Then `pack.building_options` is filtered to options with a non-empty `profiles`. The comment: "Empty profiles mean unrestricted in the legacy layout engine. Remove truly unreachable options instead of accidentally enabling them."

Per pack: `{id, name, layout_profile_id (nullable), criteria{...}, weight, building_options: [...]}`.
Criteria fields, all optional, with the defaults `_validate_building_packs` applies:
| field | default | meaning |
|---|---|---|
| `biomes` | absent ⇒ `biomes = None` unless `biome_variants` is present, in which case `set()` | natural biome ids; every one must be in NATURAL_BIOMES |
| `biome_variants` | `[]` | magical variant ids, e.g. `"desert.infernal"`; must exist in the biome catalogue |
| `population_profiles` | injected | civilization ids |
| `height_min_m` / `height_max_m` | −1e6 / +1e6 | metres |
| `resource_min` / `resource_max` | 0.0 / 1.0 | |
| `slope_max_degrees` | 1000.0 | |
| `freshwater_distance_m_min` / `_max` | 0.0 / 1e9 | metres |
| `weight` (on the pack, not criteria) | 1.0, must be > 0 | selection weight |

The seven shipped packs, in JSON order (which is the candidate-scan order):
1. `city_pack_generic_surface` — layout `standard_city_layout`, weight 1.0, criteria `{}` (matches everything). 15 options. This is the fallback.
2. `city_pack_human_rivergate` — layout `riverfront_city_layout`, weight 1.2, biomes [13 marsh, 15 boreal_forest], `freshwater_distance_m_max: 2500`. Has the only `requires_bridge: true` option, `river_gate`.
3. `city_pack_human_highland` — layout `standard_city_layout`, weight 0.8, biomes [5 exposed_rock, 6 snow], `height_min_m: 20`, `slope_max_degrees: 40`, variants `["desert.infernal","desert.water","desert.weave"]`. 11 options, `city_wall` required.
4. `city_pack_dwarven_metalwork` — weight 1.5, biomes [5,6], `resource_min: 0.4`, same three desert variants. Has `forge`.
5. `city_pack_elf_woodland` — weight 1.2, biomes [4 forest, 7 rainforest], `resource_max: 0.9`, six earth/weave forest variants.
6. `city_pack_tideport` — layout `riverfront_city_layout`, weight 1.3, biomes [13,15], freshwater 100..4500 m. Has `harbor_gate`.
7. `city_pack_gnome_foundry` — weight 1.1, criteria `{resource_min: 0.5}` only.

Per building option: `{id, name, count, required, placement, tags, asset_choices, requires_bridge}`.
- `count` is either a bare number (⇒ `{base: value, per_100_residents: 0, min: None, max: None, profiles: None}`) or an object `{base, per_100_residents, min, max, profiles}`; `min` and `max` must be ints, `min <= max`.
- `required: true` forces `count.min = max(count.min or 0, 1)`.
- `placement` ∈ `{anchored, structural, river_gate, wall}`; default `anchored`; anything else raises.
- `requires_bridge` default false. Only `city_pack_human_rivergate.river_gate` sets it.
- `asset_choices` must be a non-empty list of `{asset_id, weight > 0}`. Example: `noble_homes` in the generic pack draws `building_noble_house_generic` at 0.6 against `building_noble_estate_generic` at 0.4.
- `tags` is a list of strings, published as `sorted(tags)`.
A representative entry (generic pack):
```
{"id":"worker_housing","name":"Working-Class Housing",
 "count":{"base":2,"per_100_residents":0.25,"max":24},
 "placement":"anchored","tags":["residential","workforce"],
 "asset_choices":[{"asset_id":"building_worker_housing_generic","weight":1.0}]}
```
No shipped option uses `structural`.

── LAYOUT PROFILES: `buildings.json → layout_profiles` ────────────────────
Shape: `{schema_version: 1, fallback_profile_id: "standard_city_layout", profiles: [...]}`.
`layout_profile_data()` normalises each bare-number feature into `{base: value}` and INJECTS `feature.profiles` := every entity id that lists that feature name under that layout in `buildings.layout_features`.

Two shipped profiles:
- `standard_city_layout` — features leader_homes 1, barracks 1, noble_homes {base 1, per_100 0.04, max 3}, worker_housing {base 2, per_100 0.25, max 24}, apartments {base 0, per_100 0.15, max 8}, market_district {base 1, max 3}, religious_building {base 1, max 2}; placement `{river_buffer_m: 90, river_fork_bridge_threshold: 2, bridge_if_river_fork: true, max_anchor_slope_degrees: 16}`.
- `riverfront_city_layout` — noble_homes per_100 0.05, worker_housing {base 3, per_100 0.32, max 26}, apartments {base 1, per_100 0.24, max 9}, market_district {base 2, max 4}; placement `{river_buffer_m: 70, threshold: 2, bridge_if_river_fork: true, max_anchor_slope_degrees: 14}`.

`_normalize_layout_profile` normalises ONLY the seven names in its hardcoded `defaults` table and silently drops any other feature key. Its per-feature defaults (used when a profile omits a feature) are:
`leader_homes {base 1}`, `barracks {base 1}`, `noble_homes {base 1, per_100 0.04}`, `worker_housing {base 2, per_100 0.25}`, `apartments {base 0, per_100 0.15, max 5}`, `market_district {base 1, max 3}`, `religious_building {base 1, profiles ['human','elf','gnome','dwarf','tidekin'], max 2}`.
Those five profile ids in the religious_building default do NOT exist in the registry (the real ids are `human_heartland`, `elf`, `gnome`, `dwarf`, `tidekin`), so if either shipped profile ever drops `religious_building` the feature would be disabled for everyone but `elf`, `gnome`, `dwarf` and `tidekin`. Both shipped profiles define all seven, so the table is currently dead. Port it as written and note the hazard.
Placement defaults: `river_buffer_m 30`, `river_fork_bridge_threshold 2`, `bridge_if_river_fork true`, `max_anchor_slope_degrees 12`.

── CIVILIZATION REGISTRY: `civilizations.json` ────────────────────────────
Schema `fantasy-world-generator.civilization-registry`, `schema_version: 6`, with an integer `revision` that MUST be bumped when the bytes change (a saved world records the SHA-256 of the resolved document via `registry_identity()`).
Sections this module reads: `founding_rules`, `parent_races` (nested `civilizations`), `civilization_order` (the canonical iteration order and the exact key order of the resolved `entities` map), `defaults`, `city_classification`, `configurations` (the `mixed` aggregate).
Shipped `civilization_order`: `['human_maritime','human_desert','human_cold','human_large_island','human_rainforest','human_heartland','dwarf','elf','gnome','tidekin','hill_dwarf','frosthold_dwarf']`.
Parent races: human (95%, six civilizations), elf (25%, elf and tidekin), dwarf (60%, dwarf/gnome/hill_dwarf/frosthold_dwarf).
Per entity, `settlement`: `{surface_habitat, world_habitat, freshwater_reach_multiplier, support_outside_habitat, coastal_score_weight, resource_score_weight, reason}`. Shipped: dwarf and frosthold_dwarf have `support_outside_habitat: true`; elf, dwarf and frosthold_dwarf have `freshwater_reach_multiplier: 2`; tidekin has `coastal_score_weight: 0.35` and frosthold_dwarf 0.25, all others 0.15; only gnome has a non-zero `resource_score_weight` (0.15), which is why the published suitability-factor key is literally named `"gnome_metal_affinity"`.
Per entity, `economy.fishing_reach_multiplier`: 1.0 except human_cold 1.25, frosthold_dwarf 1.25, tidekin 1.5.
Per entity, `population` is the ~40-field profile validated by `terrain_profiles.validate_profile`, which enforces: weights and penalties in [0,1], `mutation_limit`/`irrigation`/`moisture_ideal` in [0,1], slope limits strictly in (0,90), `land_per_city_km2` in [0.01,1000], `minimum_founding_residents` an int in [1,10000], `road_grade_limit` in [0.01,1], `biome_preferences` values in [−1,1] keyed by DIGIT STRINGS, `food_biome_multipliers` values in [0,2] likewise, `magic_biome_preferences`/`food_magic_biome_multipliers` keyed by variant id strings. It stamps `schema_version: 4` and a `definition_hash` = SHA-256 of the sorted-key JSON of the profile.
Shipped `land_per_city_km2`: tidekin 0.25, gnome 0.35, frosthold_dwarf 0.55, hill_dwarf 0.7, human_cold 0.9, dwarf 1.0, elf 1.2, most humans 1.6, human_desert 1.8. `water_reach`: 400–650 m (pre-scale, never scaled — another reference-world constant).

── HERITAGE LEXICON ───────────────────────────────────────────────────────
`heritage.lexicon()` supplies `settlement_templates` (weighted patterns of slot roles) and `slot_roles`; `heritage.resolve(civ_id, parent_race_id)` supplies a per-civilization `genome` with a `lexicon` of root morphemes, phonotactics and `forbidden_forms`. `heritage.settlement_name(resolved, lexicon, draw)` picks a weighted template, then for each role in its pattern picks a root slot (falling back to `sorted(roots)` if no root fills that role), joins them under the genome's phonotactic join rules, and returns `(Capitalised word, '-'.join(chosen_slots))`. It is bounded by its own redraw limit and always terminates.

## Random streams

All randomness derives from `child_seed(master, domain, variation=0)`:
```
child_seed(m, d, v) = first 4 bytes, big-endian, of SHA256(f'tectonics-v1:{m}:{d}:{v}')   →  uint32
```
That is the ONE derivation this module uses; it never touches `cfg.seed` directly except through it.

STREAMS
1. `seed = child_seed(cfg.seed, 'settlements')` — the module's root. `rng = random.Random(seed)` is constructed and **never drawn from**; it is dead and a port may omit it (nothing else in the module shares that object).
2. Resource noise (non-recipe worlds only): `perlin3(x,y,z, seed)` — a hash-based value noise, not a stream; N evaluations, one per node, no state.
3. Founding participation: `random.Random(child_seed(seed, f'founding-{turn}-{race}')).random()` — one fresh generator, one draw, per (turn, race) pair that reaches the roll. Data-dependent count: at most 128 turns × number of parent races, fewer when a race is blocked, has no pool, or has 0%.
4. Diaspora: `random.Random(child_seed(seed, f'diaspora-{year}'))` — one fresh generator per qualifying year; two draws from it (`rng.choice(sorted(options))` then `rng.choice(reasons)`). Fires only when `year % 1000 == 0` and options exist.
5. Settlement naming: `random.Random(child_seed(seed, f'city-name-{node}-{x}-{z}-{profile}', variation))` for `variation` in 0..7, a fresh generator per attempt, abandoned as soon as the name is unique. The draw count inside `heritage.settlement_name` is itself data-dependent (template choice, per-slot choices, nucleus rolls, and its own redraw loop).
6. Per-city layout: `city_seed = child_seed(seed, f'building-pack-{i}-{k}-{x}-{z}-{peoples[k]}')`, then:
   - `_pick_city_building_pack` constructs its own `random.Random(city_seed)` and draws exactly once.
   - `_build_city_layout_plan` constructs a SEPARATE `random.Random(city_seed)` (same start state, independent object) and draws: one `randrange(len(ordered_nodes))` for the district cursor, one `randrange(len(ordered_nodes))` for the building anchor cursor, then one `random()` per planned building via `_weighted_pick`. **Both randrange calls are skipped when `ordered_nodes` is empty**, which changes the stream position for every subsequent weighted pick — this is the single most important data-dependent draw count in the module.
   - On any inferred world the whole layout is built twice from the same `city_seed`: once with `city_population=None` and once with the real resident count. The second build restarts the stream, so the number of buildings (and hence draws) differs between the two, and only the second survives.

Notable: the per-city seed embeds BOTH the node `i` and the founding index `k`. Since `k` renumbers whenever the founding order changes (survivors first, then new foundings), an age advance that reorders cities rerolls every affected city's pack and layout even when the city never moved. `building_pack_seed` is published on the site so the rebuild in step 15 can reuse the exact value.

Total draws for a size-17 seed-42 world: roughly 12 name attempts, ~40 founding rolls, 0–2 diaspora draws, 12 pack picks, and 12 × (2 cursors + up to ~40 weighted picks) — but on that world `ordered_nodes` is empty or near-empty, so the cursors are skipped and `required_asset_count` is 0 for all twelve cities.

FOR THE C++ PORT: `fwg::Rng::child(master, domain, variation)` is the direct analogue of `child_seed`. Use one `Rng::child` per named stream above; do not invent a fourth derivation. Because this is a clean rewrite, `random()`, `randrange()` and `choice()` need not match CPython bit-for-bit; what must match is (a) which stream a draw comes from, (b) how many draws each code path takes, and (c) the fact that a stream is restarted rather than continued.

## Ordering hazards

Every one of these reaches output. Listed in execution order.

1. **`points` order from `sphere_grid`** — node 0 is the north pole, then row-major over rows 1..n-2 with x ascending, then the south pole. `sorted(nodes)[:N]` samples the pole, not a random place. Node index is essentially latitude.
2. **Freshwater Dijkstra tie-break** — relaxation is strict `<`, so on an exact distance tie the node popped first wins, which the `(value, index)` heap ordering makes "lower node index". Both `distances[]` and the propagated `source[]` depend on it, and `source[]` feeds the flood term.
3. **`river_distances`** — same strict-`<` tie rule; ties do not affect the value, only which parent, and no parent is kept here.
4. **`landmass_context` component discovery** — `start = min(remaining)` each round, so component order is by lowest member node. Only `largest_landmass` (an `area == largest` equality on floats!) escapes the order, but two components with bit-identical areas would BOTH be flagged largest.
5. **`eligible_civilizations` iterates `sorted(registry, key=(priority, id))`** — the allocation-group exclusivity makes this order decisive: the first matching civilization in a group claims the cell and every later one in the same group is excluded. Changing a priority or renaming a civilization changes which people can live where.
6. **`species_ids` order = `civilization_ids()` = the `civilization_order` list from the registry**, which is validated to name every entity exactly once. This drives: per-species field computation order, `potentials` dict insertion order (which `life_capacity` iterates), `layers['life_capacity_*']` and `layers['suitability_*']` write order (output key order), and `score_sets[species_ids[0]]` — the identity of the published `suitability` layer on a recipe world.
7. **`quotas` reduction `max(quotas, key=lambda p: (quotas[p], p))`** — on a tie the lexicographically LARGEST civilization id is decremented. Rename a civilization and a different people loses a city.
8. **`starts` in the marine pass is a `set` comprehension over graph neighbours.** Its iteration order is Python's int-hash order. Here it is harmless because every seed carries label 0 and distance 0, so the `label < owner[j]` tie-break never distinguishes them — but a port that changes the labels would expose it.
9. **`allocate_access` tie rule `candidate == distance[j] and label < owner[j]`** — exact float equality deciding ownership. Deliberate and documented ("deterministic ties, no water shortcuts"). It reaches the `<civ>_support_reach` layer.
10. **`found_cities`: `available(race)` builds pairs in `owners` insertion order, then `candidates[key]` order (ascending node).** The final `min(pool, key=(-suitability, civ_id, node))` makes the tie-break explicit, but the pool's construction order still decides which equal-suitability pair is seen when the key itself ties on all three components (impossible, since node is unique).
11. **`found_cities`: race initiative rotation `races[offset:] + races[:offset]` with `offset = (turn-1) % len(races)`.** `list(parents)` is the registry's `parent_races` insertion order, filtered to races that own a species in `owners`. Reorder `parent_races` in the JSON and every world changes.
12. **`found_cities`: `min(members, key=(distance(node,s.node), s.node))` for the migration source** — distance first, node index as the tie-break.
13. **`diaspora_candidates()` iterates `owners.items()`** (registry order) but the pick is `rng.choice(sorted(options))`, which is canonical. The `options` dict's own order does not escape.
14. **`sites` list order in `found_cities` is survivors-first, then founding order.** That order becomes `site['id']`, which downstream code uses as a direct list index. Reordering it is a silent cross-block join corruption.
15. **`_settlement_names` iterates `sorted(profile_by_node)` — node order, explicitly chosen over founding order** so that collision redraws are canonical. Do not "optimise" it back to founding order.
16. **`anchor_candidates.sort(key=(distance, node))`** in `_collect_land_anchor_candidates`, then `ordered_nodes` de-duplicates preserving that order, then `anchor_distance` is built by dict comprehension over the same list (a later duplicate would overwrite an earlier one).
17. **`ordered_nodes` is rotated by a random cursor** before the district pass, and the district pass then consumes it as a contiguous slice per feature in the fixed order `('leader_homes','barracks','noble_homes','worker_housing','apartments','market_district','religious_building')`. Change the feature order and every district moves.
18. **`_collect_anchors` walks `candidates` cyclically from a cursor**, and `anchor_cursor` is THREADED ACROSS OPTIONS (anchored and river_gate share it; wall has its own `wall_cursor` starting at 0). So the options' JSON order determines every building's anchor.
19. **`sorted(boundary_nodes, key=anchor_distance, reverse=True)` for wall placement** — Python's sort is stable, so equal-distance boundary nodes keep `ordered_nodes` order. A port using an unstable sort will diverge.
20. **`list(reversed(river_frontier_nodes))` and `list(reversed(ordered_nodes))`** — reversal, not sorting; farthest-first.
21. **`required_rows = [... for asset_id in sorted(required_assets)]`** — the published asset requirement is sorted by asset id, which is the one place the dict order is normalised away.
22. **`option_plans` is a dict keyed by option id, populated in pack order** — output key order follows the JSON.
23. **Population split iterates `budget['allowances'].items()`** (species order) and within each species `members` in site order; the first `allowance % len(members)` sites get the extra resident. Reorder sites and a different city is one person bigger.
24. **`classify_cities` groups by `setdefault` (insertion order) and picks `min(members, key=(not founding_capital, -suitability, node))`**, then compares with `is` (identity). Two sites with identical suitability and node cannot exist, so the tie-break is total.
25. **Roads: the outer loop is `for a in range(len(sites))` and the inner `for b in range(a+1, ...)`.** The path is routed with site `a`'s cost function and only vetoed by site `b`'s, so the edge is asymmetric in construction even though it is stored undirected. Swapping the loop order changes which routes exist on a recipe world.
26. **`sorted(candidates)` for Kruskal** sorts `(cost, a, b, path)` tuples; cost first, then the site index pair, which is unique, so the path lists are never compared. A C++ port must NOT try to order by the path.
27. **Union-find without path compression**, `groups[ra] = rb` — the resulting forest shape is order-dependent but `components` and the edge set are not.
28. **`targets = {s['node'] for s in sites}` is a set**, used only for the `remaining.discard` early exit, so its order does not escape.
29. **Layer write order** (`suitability_*` then `suitability`, `flood_risk`, `resource_potential`, `freshwater_distance`) determines the key order of the emitted `layers` map, which is world-output bytes if the serialiser preserves insertion order.

## Edge cases

DEGENERATE INPUTS AND GUARDS
- `result['climate']` falsy or `cfg.phase < 7` ⇒ immediate no-op return. No layers, no blocks, no timing entry.
- No freshwater anywhere: every `distances[i]` stays `inf`, `source[i] = -1`, `flood[i] = exp(-inf/80)*... = 0`, the water term of every suitability score is `exp(-inf) = 0`, `farming_potential` is called with `fresh = -1` so `access = 0` and irrigation contributes nothing, `freshwater_distance_m` is published as `None` on the site and `-1` in the layer, and `_pick_city_building_pack` is handed `-1` which fails `water_distance_min <= -1` for every shipped pack (all have min 0) ⇒ **every city falls back to `city_pack_generic_surface`**.
- No rivers: `river_cells` empty ⇒ `_dijkstra_distances` returns all-`inf` ⇒ `city_river_distance = inf` ⇒ `_pick_city_layout_profile` skips the riverfront band (`isfinite` false), `plan['river']['river_distance_m']` is published as `None`, `fork_degree = 0`, `bridge_recommended = False`, and `_collect_land_anchor_candidates` accepts every node in the band because `river_distances[node] >= min_river_distance` is `inf >= 90`.
- `ordered_nodes` empty (no land anchor in the band): both `rng.randrange` calls are SKIPPED (cursor defaults to 0) — this shifts every later draw. Every feature reports `count: 0` against a non-zero `target_count`, `missing_anchors` is set, and `city_plan['fallback']` carries the district reason, then the building reason overwrites it if the building pass also missed.
- **This is the normal case, not an edge case.** `_collect_land_anchor_candidates` walks a band of at most `4*river_buffer_m + 180` metres over a raster whose cells are kilometres across, so the search never leaves the city's own node. On seed 42 at size 17 every city reports 0 or 1 district anchors against a target of 24 to 43, and `required_asset_count` is 0 for all twelve. This is a known defect of the SCALE-METRE-CONSTANTS-COLLAPSE class (a constant in absolute metres authored for an 11 km world), it is tracked on its own card, and the conformance record explicitly declines to claim the district planner works. **A C++ port must reproduce the mechanism, and should not "fix" the constant silently — doing so changes every city's asset requirement from 0 to tens.**
- `candidates` empty for a species: `footprints[species] = 0`, `city_capacity` ⇒ 0, quota 0. The marine pass's `starts` set is empty, so `allocate_access` returns all-`inf` and no potential is overwritten.
- `weight <= 0` total in `_weighted_pick`: returns `choices[0].asset_id`. Unreachable (the validator requires every weight > 0) but present.
- `cap == 0` in the budget: `shares[p] = 0` for everyone (explicit `if cap else 0` guard, no division by zero).
- A species with a positive `allowance` but zero placed cities: `members` is empty and the `for index, site in enumerate(members)` loop simply does not run, so `allowance // len(members)` is never evaluated. **No ZeroDivisionError.** That allowance lands entirely in `budget['unallocated']`.
- `per_city_km2 == 0` (spacing 0 — impossible via config bounds, which floor spacing at 10 m): `packing = 0`.
- `cfg.settlement_count == 0` and `not cfg.auto_parameters`: quotas are forced to all-zero AND `limit = 0`, and `found_cities` receives `city_limit = 0`, which its comment explains is distinct from `None`. Zero cities, zero roads, `components = 0`.
- `sites` empty: `classify_cities` does nothing, `civilization_report` reports `city_count: 0` and `capital_node: None` per entity, `roads.routes` is empty and `components = len(set()) = 0`.
- `found_cities` exhausting its 128 rounds: `stop_reason = 'round_limit_reached'`. Other terminations are `'capacity_target_reached'` and `'no_eligible_expansion'`.
- A parent race that can never separate its origin: added to `blocked_parents` permanently for the rest of the simulation with a `no_separated_origin` event; the world simply has no cities of that race.
- `heritage.settlement_name` colliding eight times: the eighth name is accepted anyway (the loop falls through), so two cities CAN share a name. The comment says the bound is deliberate so naming always terminates.

WORLD EDGES AND POLES
- There is no world edge — the grid is a sphere. Column n−1 duplicates column 0 and rows 0 and n−1 are single pole nodes, all handled by `sphere_grid` producing one node per DISTINCT cell.
- A pole node's `area` is the WHOLE polar ring's area (`area * (n-1)`), so a city at the pole carries an enormous habitat footprint into `footprints[]` and `habitable_km2`. Nothing special-cases this.
- A pole node is adjacent to every node in the ring below it, so `boundary_nodes`, `river_frontier_nodes` and the road diagonal-corner check all see a very high-degree node there.
- The diagonal-corner rules in `road_cost_function` and `water_cost` are BYPASSED when either endpoint is on a pole row (`z in (0, n-1)`), because "the corner cell" is not well defined there.
- `node_grid` reconstructs the seam and poles on write: row 0 := the first value repeated, row n−1 := the last value repeated, every row's last column := its first column. Any layer this module writes must go through it.
- `direction(x, z, n)` returns exactly `(0,1,0)` at z=0 and `(0,-1,0)` at z=n−1, so `founding_distance` between the two poles is `r*acos(-1) = pi*r = world_reach` exactly.

DEAD OR UNREACHABLE CODE (port it or deliberately drop it, but know which)
- `eligible` (step 4) is computed and never read.
- `habitat_capacity()` is defined and called only by a unit test.
- `outposts` is `[]` and never appended to, so `site['outpost']` is always `False` and `defaults['outpost_site_reason']` is never selected.
- `rng = random.Random(seed)` in `add_settlements` is never drawn from.
- The `800` metre branch in `_collect_land_anchor_candidates` is unreachable from this module (both callers pass a finite buffer).
- `'structural'` placement is implemented but no shipped option uses it. It draws assets without placing anchors.
- The 1.8× riverfront band in `_pick_city_layout_profile` is shadowed because all seven shipped packs declare a `layout_profile_id`.
- `_normalize_layout_profile`'s hardcoded feature defaults are shadowed because both shipped profiles define all seven features; the five profile ids in the `religious_building` default do not exist in the registry.
- The non-recipe branches throughout (`perlin3` resource, `surface_habitat` instead of `world_habitat`, no coastal/marine/salinity terms, no per-species road limits) are reachable only on `world_recipe == 0`, which `default_config` now refuses to build. They are live code for a lab world.

FLOATING-POINT AND ROUNDING
- `math.floor` in `_resolve_feature_count`, `habitat_capacity`, `city_capacity`, `life_capacity` and the packing ceiling — floor of a double, not truncation toward zero (identical here since every argument is non-negative).
- `round(residents * 0.55)` is Python's banker's rounding (half to even). At `residents` odd × 0.55 landing exactly on .5 the two differ from C's `std::round`. This is an intentional divergence a clean rewrite may accept, but it must be a decision, not an accident.
- `acos(max(-1, min(1, dot)))` clamps before the inverse cosine — mandatory, since the dot of two normalised vectors can exceed 1 by an ULP.
- `1e-8` epsilons on the origin-separation comparisons.
- `area == largest` float equality in `landmass_context`.
- `candidate == distance[j]` float equality in `allocate_access`'s tie rule.

## Reasoning worth preserving

Quoted verbatim from the source. These record failures and rulings that exist nowhere else.

**On the layout record's version 2 and the two-answers problem** (module header): "The node-scale record nested in every site. Version 2 dropped `buildings`, the per-option placement claim that contradicted `city_plans`, and renamed `residents` to `population_estimate` so the figure names the axis it measures." And in `_build_city_layout_plan`: "`buildings` is deliberately not published. It carried a per-option target_count/placed_count pair that read as 'this city placed N buildings', beside a `city_plans` entry that had actually placed them -- two answers to one question, and the wrong one reported a shortfall in every city, which hid the real ones. The asset requirement the draw produces survives as the three roll-ups below." And: "The site's own catchment estimate, copied verbatim. It was `residents`, which reads as people standing in the city; it is a demographic estimate two orders of magnitude above the beds the plan builds."

**On why the player-founding constants live in this file**: "A site the player founded rather than the suitability search. `terrain_settlement_api` writes it; the survivor carry-back below is the only reason an authored layout outlives a rebuild, so the two constants live here where both readers can see them without a circular import."

**On what a rebuilt city inherits**: "What a rebuilt site inherits from the survivor standing on the same node. war_history travels with the city: a survivor that rebuilds its hinterland is still the city that fought, and its forts answer to that. absorbed_refugees and absorbed_bands travel for the same reason and are the only way absorption can persist at all: population_estimate is re-derived from the population budget on every rebuild, so refugees written into it at an age boundary would be erased by the next one."

**On not overwriting an authored layout**: "Called at the one place the generator would otherwise overwrite it. A site the player founded with `layout: \"player\"` owns where its buildings go; re-deriving the layout from the ground would replace that with a planner's answer for the same node, which is the requirement PLAYER-FOUNDED-SETTLEMENTS exists to meet." And at the sizing pass: "The player owns an authored layout. The sizing above still applies -- the world owns the metadata -- but the districts are not re-derived."

**On removing truly-unreachable building options** (`civilization_registry.building_pack_data`): "Empty profiles mean unrestricted in the legacy layout engine. Remove truly unreachable options instead of accidentally enabling them."

**On the bounded shortest path** (`shortest_paths` docstring, the whole thing is a proof): "A caller that discards every result past a reach was still paying for the whole component: the sea routes in `add_world_society` ran 3 828 unbounded searches over the ocean at size 128 and kept four of them, because `sea_reach` is 6 000 m against a 5 834 m median edge. Stopping at the reach is not an approximation -- edge costs are non-negative, so every prefix of a path costing `limit` or less also costs `limit` or less, and a node inside the bound keeps the same distance and the same parent it had when the search was unbounded. Nodes outside it stay at infinity rather than taking their true value, so read a bounded result only inside the bound."

**On the road diagonal rule**: "A diagonal cannot slip between two water-filled corner cells."

**On support being routed rather than circular** (`reachable_support`): "Support must follow passable terrain, not a circle across water or cliffs."

**On the capacity calibration** (`life_capacity`): "100 residents per fully productive km2: explicit provisional food calibration. Overlapping species claims divide each cell's best supported capacity; they never multiply that cell's capacity. Fractional residents remain unallocated."

**On irrigation** (`farming_potential`): "Maintenance reduces the added exportable surplus; no source means no irrigation."

**On naming by place rather than by founding order** (`_settlement_names`, the most expensive comment in the file): "A name per settled node, in each people's own language. This replaces a 24-word English list indexed by founding order. That index was the problem: `names[k%24]` renamed every later city when one was founded earlier, and it could not survive an age advance without the explicit copy-back downstream. Keying on the node and its coordinates instead gives a name that is a property of the place and the people, not of the order they happened to be founded in. Naming runs in node order rather than founding order so that collision resolution is canonical: whichever city is processed second re-draws, and which one that is must not depend on how the founding loop happened to be sequenced. The redraw is bounded and uses child_seed's third parameter, so a duplicate yields another authentic name rather than a numeric suffix. Returns {node: (name, gloss)}. The gloss is the English reading of the roots the name was built from, so a consumer can say that Bargdorn is the wood-hold."

**On the shared planetary ceiling**: "The shared ceiling is the ground itself: cities must stand a settlement spacing apart, so habitable land divided by the densest packing of that separation is how many can exist at all. It is applied after every entity is assessed, so an early registry entry cannot exhaust the budget before a later one is considered."

**On why the cell spacing binds** (a measured failure): "Cities cannot be packed closer than the raster can separate them. Spacing alone bounded this while the world was 11 km across and a cell was 87 m, but a coarse grid on a large world inverts that: at 200 km circumference a size-17 grid has 12.5 km cells against an 8 km spacing, so the area ceiling admitted about 63 cities onto 289 nodes -- a city every four or five nodes, each then planning a full crop. The binding spacing is whichever is coarser."

**On the settlement_count semantics**: "A world that infers its own quotas is ceilinged by its ground, not by a count the caller never set: the quota line above already reads `inferred`, and the two have to agree or a recipe world silently keeps the settlement_count default. Asking for no cities is not the same as asking for no ceiling, and it outranks the ground: a caller who wants an empty world gets one, inferred or not. A caller who lowers the ceiling is asking for fewer cities and gets them, inferred or not: `settlement_count` is published as 'Maximum surface cities', so a request for two that silently returns ten is a broken parameter rather than a ground rule. Leaving it at the recipe default still means 'no ceiling but the ground', which is what the inferred path is for."

**On the refugee term being added after the split**: "Refugees a city took in at an age boundary are people the capacity split cannot know about: the split is derived from the ground this city farms, and these walked in from a town that fell. Added after it, so the split stays a pure function of habitat and the arrivals stay auditable in absorbed_bands. Zero on every generated world: nothing writes absorbed_refugees before stage 16, and absorption runs at an age boundary."

**On `city_limit` being None-for-none** (`founding.py`, a real collision): "`city_limit` is None for no ceiling and a count otherwise, including zero. It used to be zero-for-none, which collided with a caller asking for no cities at all: quotas went to zero, and the diaspora bonus -- whose whole purpose is to waive a zero quota -- then had nothing left to stop it and settled the world anyway."

**On initiative rotation** (`founding.py`): "Rotate initiative so one parent does not always claim contested sites first."

**On the hidden-school preference default** (`terrain_profiles.biome_preference`): "An exact magical state overrides its natural-core preference. Ground held by a hidden school is uninhabitable to everyone, and that is a rule rather than 52 authored opinions per people: nobody has a cultural preference about a school they have never heard of. Without it the fallback below would read a rotted grassland as plain grassland, and settlement placement would find corrupted ground exactly as attractive as clean ground."

**On the food multiplier** (`biome_food_multiplier`): "Exact state food factor, before independent school-specific hazard losses. Corrupted ground feeds nobody; which of the four took it is the corruption record's business, not the terrain table's."

**On exclusive habitats** (`eligible_civilizations`): "Exclusive habitat groups partition capacity, without profile inheritance."

**On `sphere_index` being cached** (a measured cost): "Every routing cost function needs to name the two corner cells a diagonal step would otherwise slip between, and each was rebuilding this sixteen-thousand-entry map -- and rescanning the same points for the width -- on every call. One `add_world_society` at size 128 built it 3 886 times for twelve seconds."

**From the conformance record `settlement-records.md`, which the code does not repeat**:
- "`city_plans` is the only block that says which buildings a settlement has." The node-scale `city_layout` "states no building placement of any kind, and it carries no key that could be read as one."
- The two are "not reconcilable by arithmetic and were never meant to be. They draw from two different catalogues... at two different scales (world-raster terrain nodes against local metres), at two different stages (9 against 16). A `placed_count` beside a plot count is two answers to one question, and only one of them was ever the product."
- "One authority per question. Two blocks describing the same city's buildings disagreed, and a reader had no way to tell which was load-bearing without reading both and guessing. The failure was worse than a mismatch: because the node-scale record reported a shortfall in every city, a real shortfall in one would have been invisible."
- "Absence over a zero that was never measured... a consumer that reads a removed field gets a `KeyError` at the line that made the wrong assumption, rather than a plausible number and a wrong world."
- The catchment-versus-beds ratio: "on seed 42 at size 17 the largest city estimates 29,428 residents against 244 beds and 241 staffed posts. That is a ratio, not a rounding, and it is not a defect of either number."
- The explicit non-claim: "That the node-scale district plan finds anchors. It does not, at any size this repository generates."

**From `settlement-founding.md`, on invariants this module must not break**:
- "`site['id']` equals the site's index in `sites[]`. It is used as a direct list index into `humans.cores`, `seasonal_food.models` and per-site profile lists, so an id that is not the ordinal position joins the wrong rows *silently* rather than raising."
- "`city_class` is derived, never supplied. It is a per-civilization suitability *rank*, not a size."
- "`village` is not a settlement kind anywhere in the tree; `settlements.sites[].kind` is the constant `'city'`, and small/medium/capital is the classification rank rather than a size vocabulary."
- On why one new town must not re-roll the nest pass: "`terrain_nests._place` opens one sequential RNG over a fixed cell order whose per-cell draw count depends on distance to the nearest settled thing, so one new town changes the draw stream in every cell on the planet -- measured at 152 of 248 monster nests replaced, the furthest at the antipode."
- On rural ordinals: "`add_humans` re-derives hamlets and fortresses from scratch in site order with length-at-append-time ids, so adding a city trades ordinals among settlements that were already there -- measured at 15 hamlets reassigned on a single founding."
- "That a founded city survives. It usually will not, and that is the world working. On an untouched seed-42 size-17 world an age advance ruins ten of twelve cities across six causes."

## Notes for the C++ implementation

MAPPING TO THE EXISTING LIBRARY
- A layer ⇒ `fwg::Grid` (n×n doubles, column n−1 duplicating column 0, rows 0 and n−1 single pole values). Every layer this module writes must go through the `node_grid` reconstruction, which is exactly `Grid::fill_row` semantics plus the pole/seam rules the header already documents. Integer-valued layers (`water_type`, `rain_river`, `<civ>_support_reach`, `dominant_magic`) are still doubles in a `Grid`; compare with `== 1.0` or convert once into a `std::vector<int>` node vector at read time, as the Python does (`values(key)` is exactly "flatten the grid to one value per distinct node").
- `sphere_grid(n, r)` ⇒ `fwg::sphere_grid(n, radius)` → `SphereGrid{point_x, point_z, areas, neighbours}`. Python's `points`/`areas`/`graph` triple maps one-to-one. Neighbour lists are already sorted ascending by index in both.
- `direction(x,z,n)` already exists as `fwg::direction`.
- `child_seed(master, domain, variation)` ⇒ `fwg::Rng::child(master, domain, variation)`. **Do not invent a fourth seed derivation.** Every stream in this module goes through it.
- `cfg` ⇒ `fwg::Config`. It will need new fields: `population_profile`, `human_magic_limit`, `support_reach`, `settlement_spacing`, `settlement_count`, `road_max_grade`, `bridge_cost`, `world_recipe`, `auto_parameters`, `magic_enabled`, plus the world-options `fishing_reach` and `fish_productivity`.
- `result` ⇒ `fwg::World`. It will need typed blocks: `std::vector<Site>`, `std::vector<Road>`, `PopulationBudget`, `FoundingReport`, `CivilizationReport`, and `std::map<std::string, PopulationProfile> peoples`. The existing `World` has `layers` and `timing_ms` already.
- JSON out goes through the library's own writer; the registry read needs a JSON PARSER, which the library does not have. This is the single largest new dependency-free component: `civilizations.json` and `buildings.json` are large, deeply nested documents with a strict validator. Budget for it separately, or bake the registry into generated C++ data (which loses the `registry_identity()` SHA-256 that saved worlds record — `fwg::sha256` exists, so hashing a canonical serialisation of the loaded document is feasible).

PYTHON IDIOMS WITH NO DIRECT EQUIVALENT
- `heapq` with `(value, index)` tuples ⇒ `std::priority_queue<std::pair<double,int>, vector<...>, std::greater<>>`. **The lazy-deletion pattern (`if value != distances[i]: continue`) must be preserved exactly**, because it is what makes the tie-break "lower index wins" rather than "whatever the heap felt like". `std::priority_queue` is not stable, so relying on the comparator alone is wrong — compare the pair, which makes it total.
- `allocate_access` pushes `(value, label, node)` triples and the tie rule is `candidate == distance[j] && label < owner[j]`. Use a 3-tuple comparator; do not collapse it.
- `random.Random(seed)` constructed fresh in several places ⇒ construct a fresh `fwg::Rng` each time. Do NOT hold one long-lived Rng and reuse it; the Python semantics are "new stream from the same seed", and `_pick_city_building_pack` and `_build_city_layout_plan` deliberately both start from `city_seed`.
- `rng.randrange(k)` ⇒ `Rng::below(k)`. `rng.choice(seq)` ⇒ `Rng::choice`. `rng.random()` ⇒ `Rng::random()`.
- Dict insertion order is load-bearing in at least six places (see ordering hazards). Use `std::vector<std::pair<K,V>>` or an insertion-ordered map for: `species_ids` order, `potentials`, `score_sets`, `habitats`, `quotas`, `option_plans`, `features`, `budget.allowances`, `owners`, `parents`. `std::map` gives you SORTED order, which is NOT the same and will silently change which people loses a city in the quota reduction (`max` over `(quota, id)`) and which civilization claims an allocation group.
- Set iteration order (`starts` in the marine pass, `habitable`, `targets`) does not escape here, but use `std::set<int>` or a sorted vector so it cannot start to.
- `sorted(x, key=..., reverse=True)` is STABLE. `std::sort` is not. Use `std::stable_sort` for the wall-candidate sort (`sorted(boundary_nodes, key=anchor_distance, reverse=True)`) — ties there keep `ordered_nodes` order and that decides where walls go. Everywhere else the sort key is total (distance then node index), so `std::sort` is fine.
- `math.inf` ⇒ `std::numeric_limits<double>::infinity()`. `math.isfinite` ⇒ `std::isfinite`. The module publishes `None` for infinity in two places (`freshwater_distance_m`, `river.river_distance_m`) and `-1` in a third (the `freshwater_distance` layer) — three different sentinels for the same condition, all of which must survive.
- `round()` is banker's rounding; `std::round` is half-away-from-zero. Decide explicitly (see edge cases).
- `math.floor` on a non-negative double ⇒ `std::floor`, then cast.
- `int.from_bytes(sha256(...)[:4], 'big')` ⇒ already `Rng::child`'s job.
- `dataclasses.replace(cfg, human_magic_limit=..., road_max_grade=...)` in the road loop ⇒ copy the `Config` and mutate two fields, or better, lift the two values out of `Config` into the cost functor's constructor arguments. The Python only uses `cfg` inside `road_cost_function` for `size`, `human_magic_limit`, `road_max_grade` and `bridge_cost`, so a `RoadCostParams{n, magic_limit, max_grade, bridge_cost}` struct is cleaner and removes the copy.
- `road_cost_function` returns a closure over eight vectors; in C++ make it a struct with `operator()(int i, int j, double d) -> std::optional<double>`. `None` ⇒ `std::nullopt`. The Dijkstras call it in the innermost loop millions of times, so it must be inlinable — a `std::function` here will cost real time (the Python comments already record twelve seconds lost to a rebuilt lookup map).
- `sphere_index(points)` is a memoised `{(x,z): index}` map keyed on the identity of the points list. In C++ build it once alongside the `SphereGrid` and hand it to every cost functor; the memoisation hack exists only because Python had no place to put it.
- `next(d for j,d in graph[i] if j==k)` in the road length sum is a linear scan per edge. In C++ either keep the scan (degree ≤ 8 except at the poles) or carry the edge length alongside the node in the path.
- `list(reversed(x))` ⇒ `std::vector` copied with reverse iterators. `x[cursor:] + x[:cursor]` ⇒ `std::rotate` on a copy.
- The `_collect_anchors` cyclic walk mutates a `used_nodes` set shared across every option AND across the district pass. Pass it by reference; do not copy.
- `option['profiles']` is `Optional<set<string>>` where `None` means unrestricted and an empty set means nobody. Model it as `std::optional<std::set<std::string>>` — collapsing `None` and `{}` into one empty set inverts the meaning and enables every option for everyone.
- `_coerce_feature_count`'s `min`/`max` are `Optional<int>`; model them as `std::optional<int>`, not sentinel −1.
- The two `environment` dicts (`environment_at`'s eight fields versus the settlement habitat's fourteen) are different types. Give them different structs, or one struct with an `has_terrain_fields` flag, so a habitat rule naming `resource` against the identity environment fails loudly rather than reading garbage. In Python it raises `KeyError`; in C++ a `std::map::operator[]` would silently return 0.
- `matches()` is a small recursive rule interpreter over a JSON tree. Parse it once into a typed `HabitatRule` tree (`All{children}`, `Any{children}`, `Compare{field, in?, min?, max?, gt?, equals?}`) at registry load, not per node — it is evaluated N × species times.
- `heritage` is a separate package (constructed-language name generation). It is its own spec; here you need only its three entry points: `lexicon()`, `resolve(civ_id, parent_race_id)`, and `settlement_name(resolved, lexicon, rng) -> (name, gloss)`.

PERFORMANCE NOTES THE PYTHON ALREADY PAID FOR
- The per-species loop is O(species × N) with a `farming_potential`, a `matches()` evaluation and several `exp()` per node. At size 513 with twelve species that is ~3.2M `matches()` calls.
- The road pass is O(sites) Dijkstras over the whole node graph, each with an early exit once every other site has been settled, plus O(sites²) path reconstructions, plus on recipe worlds an O(sites²) veto scan that constructs a fresh cost functor per pair — hoist that: `other_cost` depends only on `sites[b]`'s profile, so build one per distinct civilization, not one per pair. This is a safe optimisation because the functor is pure.
- `_collect_land_anchor_candidates` runs a bounded Dijkstra per city, twice per city on an inferred world.

ESTABLISHED C++ CONVENTIONS TO FOLLOW
- No dependencies: the library has its own JSON writer, SHA-256 and test harness. Any JSON READER you add must be in the same spirit.
- `fwg::Config` holds resolved parameters; `fwg::World` holds layers plus typed blocks. Bump `kHighestImplementedStage` only when the whole stage lands.
- The header comments in `include/fwg/` carry the reasoning inline, which is the same discipline this module's Python comments use. Carry the quoted reasoning above into the headers rather than into a separate document, since the Python will not exist.

## Open questions

1. **`round(residents*0.55)` is banker's rounding.** A clean rewrite is free to use half-away-from-zero, but `urban + rural == residents` must still hold (it does either way, since rural is the remainder). Decide and record.

2. **The anchor band is a known defect, not a design.** `_collect_land_anchor_candidates` walks `max(180, 4*river_buffer_m + 180)` metres over a raster whose cells are kilometres across, so on every world this repository generates it never leaves the city's own node and `required_asset_count` is 0 for all cities. The conformance record explicitly declines to claim it works and assigns the fix to the SCALE-METRE-CONSTANTS-COLLAPSE card. **A porter must decide whether to reproduce the collapse (faithful, produces empty layouts) or scale the band with the world (fixes the feature, changes every city's asset requirement from 0 to tens, and diverges from the Python by design).** Same question applies to the flood term's 80 m and 10 m, and to every profile's 400–650 m `water_reach`, all of which are reference-world metres that nothing scales while `settlement_spacing` and `support_reach` ARE scaled by the caller.

3. **The support pass uses the global `cfg.human_magic_limit` and `cfg.road_max_grade` and the global `hazard` layer, while the road pass uses per-species limits on recipe worlds.** A dwarf's rural support is therefore routed with human road tolerances even though the dwarf profile has `road_grade_limit: 0.55` and `mutation_limit: 0.45`. This looks like an oversight rather than intent — there is no comment defending it — but it is observable in the `<civ>_support_reach` layer and in the food allowance it gates. Port as-is and flag, or fix and accept the divergence.

4. **The road veto is asymmetric.** The path between sites `a` and `b` is routed with `a`'s cost function and only vetoed by `b`'s, so the resulting edge is not necessarily the cheapest path that both peoples can use, and swapping the loop order would produce a different network. Nothing defends this.

5. **`_pick_city_building_pack` is handed `-1` for an unreachable freshwater distance**, which fails `water_distance_min <= -1` for all seven shipped packs (every one has an implicit or explicit minimum of 0). So a city with no reachable freshwater always gets the fallback pack, even when a pack would otherwise match on biome and resources. Intended sentinel behaviour or a sentinel leaking into a range check? No comment either way.

6. **The dead code inventory** — `eligible`, `habitat_capacity`, `outposts`, the unused `rng`, the `800` metre branch, the `'structural'` placement, the 1.8× riverfront band, and `_normalize_layout_profile`'s default feature table with its five non-existent profile ids. A porter has to decide which to carry. My recommendation: carry `'structural'` (a data edit would activate it), drop `eligible`/`habitat_capacity`/`outposts`/the unused `rng`, and carry the rest with a comment marking them shadowed.

7. **Two cities can share a name.** After eight redraws `_settlement_names` accepts the eighth regardless. The comment says the bound is deliberate so naming terminates, but it does not say a collision is acceptable. Nothing downstream keys on the name (the `uid` is the key), so it is probably fine — confirm before relying on names for display joins.

8. **`ICARUS_CITY_WORKERS`** — the task brief warns that both settlement planners fork a process pool controlled by this undocumented env var and that nothing ever proved pooled output equals serial. **It does not appear in this module.** It belongs to `city_planner.py` / `hamlet_planner.py` at stage 16. Nothing in `terrain_settlements.py` is parallel; the whole module is strictly sequential.

9. **`population_budget` is only written when `inferred`.** On a non-inferred world no site ever gets `population_estimate`, `urban_population_estimate` or `rural_population_estimate`, and no `result['peoples']` is written. Consumers that read those unconditionally would `KeyError`. Since `default_config` now refuses any recipe but 3 (and recipe 3 always sets `inferred`), that path is only reachable from the lab. Decide whether the C++ supports it at all.

10. **The layout is built twice per city on every inferred world** (once with `city_population=None`, then discarded and rebuilt with the real count). The first build is pure waste except that it fixes `building_pack_id` and `building_pack_seed` on the site, which the second build reads back. A port could compute the pack and seed, skip the first plan entirely, and build once — the output is identical because the second build restarts the RNG from `city_seed`. I believe this is safe, but it is a behavioural claim I did not execute, so verify it before relying on it.

## Addenda from the audit

The reader above wrote the specification; a second reader went back to the source
to find what it had left out or got wrong. Both are kept: the omissions are
content the implementer needs, and the corrections say where the specification
above is not to be trusted.

### Corrections

- **Claimed:** site['suitability_factors'] (recipe-3 only): "dict of the nine additive score terms, with the last key named by defaults['resource_score_factor_label']"
  **Actually:** There are TEN keys, and only one of them is named in the spec. Source (lines 966-974) writes, in this order: 'freshwater', 'slope', 'temperature', 'moisture', 'resources', 'flood', 'biome', 'magic', 'coastal_support', and defaults['resource_score_factor_label'] ('gnome_metal_affinity'). 'flood' and 'magic' are published NEGATED (-flood_penalty*flood[i], -magic_penalty*risk), so the ten values sum to the pre-clamp score. Also: 'magic' reads risk from the LAYER, not from the species_hazard node vector used in scoring: risk = layers.get('magic_risk_'+profile, layers.get('magic_hazard', [[0.]*n]*n))[z][x] — a three-level fallback ending in an n x n zero grid, with no world_recipe gate (the block only runs under world_recipe anyway).

- **Claimed:** C++ note: "option['profiles'] is Optional<set<string>> where None means unrestricted and an empty set means nobody. Model it as std::optional<std::set<std::string>> -- collapsing None and {} into one empty set inverts the meaning and enables every option for everyone."
  **Actually:** Backwards for BUILDING OPTIONS. _coerce_building_options collapses empty to None twice: line 169 `profiles = set(raw_profiles) if raw_profiles else None` and line 181 `'profiles': set(profiles) if profiles else None`. An empty profiles list on a building option therefore means UNRESTRICTED, and an empty set can never reach the planner. The opposite is true one level down: _coerce_feature_count (line 102) uses `set(profiles) if profiles is not None else None`, so a LAYOUT FEATURE (or a count object) with `profiles: []` really does mean nobody. A port that models both the same way gets one of the two wrong.

- **Claimed:** Ordering hazard 29: "Layer write order (suitability_* then suitability, flood_risk, resource_potential, freshwater_distance) determines the key order of the emitted layers map"
  **Actually:** Incomplete, and the first two families are missing. Actual write order is: (1) '<species>_support_reach' — written inside the per-species loop (line 860), so the earliest and interleaved with species iteration; (2) 'life_capacity_<species>' for every species (line 933, inside `if inferred`); (3) 'suitability_<species>' for every species (line 938); (4) the four-key layers.update() of 'suitability', 'flood_risk', 'resource_potential', 'freshwater_distance' (line 1015), which lands AFTER the whole site-construction and population-split passes.

- **Claimed:** Founding report field list: "...diaspora_bonus_used (sorted), used_civilizations (sorted), start_year, end_year = year_offset + (turn-1)*years_per_round..."
  **Actually:** The published `start_year` is NOT the `start_year` argument the caller passed; it is `year_offset` (founding.py line 98: `start_year=year_offset`). year_offset = max(start_year, max(existing survivor founding_year, default 0) + (years_per_round if sites else 0)), so on an age rebuild where a survivor's founding_year is late the two differ, and this is the value the next age reads back. Likewise `minimum_residents` in the report is not the caller's dict but the resolved map `{key: (minimum_residents or {}).get(key, 40) for key in owners}` — one entry per civilization in `owners`, with the 40 default already applied.

### Missing — blocking

**`possible` is never initialised in the spec's founding loop**

founding.py line 40: each turn begins `radius=min(world_reach,base*1.6**min(turn-1,32)); possible=False`. `possible` is a PER-TURN flag, reset to False at the top of every one of the 128 rounds, set True at founding.py line 57 (after the origin-separation filter passes, before the participation roll) and again at line 95 (`if options: possible=True`). Without the reset, step 7's `if not possible: stop='no_eligible_expansion'; break` can never fire after the first successful turn, so a world that stalls short of target runs all 128 rounds instead of stopping. That changes `turns`, `stop_reason`, `elapsed_years` and `end_year` — and `end_year + years_per_round` is the next age's `start_year`, so every later age's founding_year is shifted.

**How site['reason'] is resolved**

Source line 963: `'reason': rules[peoples[k]]['settlement']['reason'] or (defaults['outpost_site_reason'] if outpost else defaults['normal_site_reason'])`. The per-entity `settlement.reason` from civilizations.json WINS; the two registry defaults are only the falsy-fallback. The spec lists `reason (str)` as a site field and quotes both defaults but never gives the precedence, so an implementer publishes defaults['normal_site_reason'] ("Combined freshwater, slope, climate and flood-risk score") on every site, which is wrong for every shipped civilization that authors its own reason.

**settlements.method prose — the spec writes only `<prose>`**

Verbatim (line 1021): 'Candidate sites, not built cities or population simulation. Resources are seeded potential, flood risk a proximity/height proxy. Outposts can accept poor conditions; water cells and slopes beyond the selected population limit remain excluded. City assets are selected from deterministic data-driven building packs.' The magic suffix (already in the spec) appends ' All city sites must pass the mutation hazard limit, including difficult-site cities.'

**population_budget.method prose — the spec writes only `<prose>`**

Verbatim (line 932): 'Capacity integrates area-weighted farming potential, climate, freshwater/irrigation, slope, biome and mutation penalties. Dwarven productive hinterland may extend beyond mineral uplands only over safe terrain routes within support reach. Overlapping peoples split each cell capacity. Provisional 100 residents per fully productive km2, not validated agricultural yields; access and trade can reduce realized support. Cities require habitat footprint and their civilization-specific minimum regional residents; no species count or majority guarantee. Diaspora can waive a city-count footprint quota for an unused civilization with its required supported residents, once per parent; habitat and spacing still apply. Unsettled capacity remains unused.' The recipe suffix the spec does quote is appended to this.

**Structure of result['civilizations'] — the spec gives a prose field list, not a shape**

terrain_civilizations.civilization_report(sites) returns {'version':3, 'registry': registry_identity(), 'heritage': heritage_identity(tuple((key, entity_rules(key)['parent_race_id']) for key in civilization_ids())), 'medium_suitability_min': classes['medium_suitability_min'], 'capital_scope': classes['capital_scope'], 'parent_races': section('parent_races'), 'presentation_defaults': {k:v for k,v in section('defaults').items() if k.endswith('_color_rgb')}, 'entities': [...]}. Each entities row is dict(id=key, region_index=index, name=get_profile(key)['name'], **get_profile(key)['civilization'], presentation=entity_rules(key)['presentation'], parent_race_id=entity_rules(key)['parent_race_id'], heritage=heritage_of(key), city_count=sum(s['population_profile']==key for s in sites), capital_node=next((s['node'] for s in sites if s['population_profile']==key and s['city_class']=='capital'), None)) for index, key in enumerate(civilization_ids()). Note: one row per civilization in civilization_order whether or not the world used it; region_index is the ordinal in that order; the whole `civilization` sub-dict of the population profile is SPLATTED into the row; and capital_node depends on classify_cities having already run.

### Missing — important

**The diaspora founding emits an event row**

founding.py line 93, immediately after the diaspora site is appended: events.append(dict(record, status='diaspora_founded', turn=turn, radius_m=world_reach, suitability=scores[key][node])). The spec's step 4 pseudocode goes straight from `sites.append(record); ever_used.add(key)` to `options = diaspora_candidates()`, so a port produces a founding report whose events list silently loses every diaspora row — the only audit trail for a bonus-waived civilization.

**A building option's `profiles` falls back to its count object's `profiles`**

_coerce_building_options lines 163-165: `raw_profiles = raw.get('profiles')`; if it is absent (None), `profiles = count.get('profiles')` — the option inherits the `profiles` set parsed out of its own `count` object by _coerce_feature_count. Only when the option carries a top-level `profiles` list is that used instead. So `{"id":"forge","count":{"base":1,"profiles":["dwarf"]}}` restricts the option to dwarves even with no option-level profiles key.

**population_budget.calibration.spacing_city_ceiling is `packing`, not `limit`**

Line 931: 'spacing_city_ceiling': packing — the raw hexagonal-packing count floor(habitable_km2/per_city_km2), NOT the applied `limit` (which is 0 / packing / min(packing, settlement_count) depending on the inferred and asked_for_fewer branches). Also in the same calibration dict: 'minimum_city_region_residents': {k: p['minimum_founding_residents'] for k,p in profiles.items()} and 'habitable_km2': habitable_km2 (the float km2 of the union of all species' candidate sets, before any flooring).

**derivation.settings.settlement_count is a {value, source} dict**

Line 937: result['derivation']['settings']['settlement_count'] = {'value': sum(quotas.values()), 'source': '<the quoted source string>'} — a two-key dict, not the bare int that result['config'] and result['effective_config'] receive. The spec says "publish sum(quotas.values()) into config, effective_config and derivation.settings.settlement_count with the source string", which reads as three identical writes.

**founding report `definition` string**

founding.py line 99: 'Sufficient population means capacity-derived city quotas, plus at most one food-supported diaspora bonus per parent, subject to safe habitat, spacing and a 24-city lab ceiling; 128-round safety limit.' The spec says only "and a `definition` prose string".

### Missing — minor

**`river_neighbors` is a dead parameter of _build_city_building_plan**

_build_city_building_plan takes `river_neighbors` (line 258) and never reads it. river_frontier_nodes is computed from `graph_neighbors[node_id]` over the whole ordered_nodes band (lines 263-264), i.e. every band node with a river neighbour, not from the city node's own neighbour list. The spec reproduces the parameter in the signature without marking it dead, and its DEAD-OR-UNREACHABLE inventory (which is presented as complete) omits it. Two more inventory gaps: _build_city_layout_plan's `building_pack=None` default substitutes {'id': None, 'building_options': []} (line 596-597) and then raises ValueError if a non-dict is passed, and `_pick_city_layout_profile`'s riverfront==None branch is also unreachable with shipped data.

**Validator behaviours: empty-vs-absent building_options, duplicate ids, name defaults**

_coerce_building_options returns [] when `building_options` is absent (None), but RAISES ValueError('Pack {id} building_options must be a non-empty list when provided') for an explicit empty list. Other validator rules the spec omits: duplicate building option id within a pack raises; duplicate pack id raises; duplicate layout profile id raises; a layout profile whose `features` is absent or an empty dict raises; and pack/option/layout `name` defaults to the corresponding `id` (str(raw.get('name', raw['id']))), which matters because `name` is published on every option plan and `layout_profile_name` on every city_layout.

**Survivor parent_race_id lookup can KeyError; landmass_fraction zero guard**

Line 920: survivors = [dict(s, parent_race_id=owners[s['population_profile']]) for s in result.get('_survivors', [])]. `owners` only has keys for civilizations in species_ids, so a rebuild on a non-'mixed' population_profile whose survivors include another people raises KeyError rather than dropping or defaulting them. Also landmass_context guards the division: landmass_fraction = area/total if total else 0. (a world with zero land nodes yields all-zero contexts rather than a ZeroDivisionError).
