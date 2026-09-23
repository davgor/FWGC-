# Port specification: 01-recipes-water

Extracted from the Python `FantasyWorldGenerator` at commit `b67c99b` before that
repository was retired, then audited against the same source by a second reader.

**Source modules**

- `Sim/icarus_sim/terrain_recipes.py`
- `Sim/icarus_sim/terrain_water.py`

**Estimated C++ size:** roughly 520 lines, excluding tests.

## Audit: IMPLEMENTABLE

The hydrology half is unusually good: I re-derived `route_water` and `add_water` from the spec's Algorithm section alone and never had to guess a constant, an initialisation, a tie-break, a loop bound or an ordering. Every line of terrain_water.py is accounted for — the `h[i] <= sea` inclusive membership, `min(remaining)` component ordering, largest-by-AREA ocean pick with first-max tie, `roots = ocean_ids or [argmin(h[i], i)]`, ocean roots pinned to `sea` while a lone non-ocean root keeps its own `h`, visited-at-push, `level[j] = max(h[j], elevation)`, the `(level, index)` heap tie-break, the three filters and the `max((slope, j))` LARGEST-index tie in the re-point, the reverse-`order` accumulation, the 1e-7 lake epsilon, ocean-before-depth kind ordering, the `>=` river test, the scatter helper's pole rows and seam, and all four report sums. I ran the generator at seed 42 / size 33 / phase 5 / recipe 0 and the spec's measured table reproduces exactly (threshold 0.12868266184267615, land 14.155092802694377, ocean 28.141044974769844, lake 0.0, dry 14.155092802694378, capacity int 0, 994 nodes, 44 segments, 690 outlets). I also ran `seed_config` and its fourteen-value worked example is exact, the Config field declaration order is exactly as claimed, `world_options` really is the string '{}', and all thirteen profile trait rows match the registry to the digit. The recipe cascade (`direct > prof > seeded > formulas > fixed > default`, with `globe_radius` in both `fixed` and `formulas` and `formulas` winning) is faithful. What the spec gets wrong is peripheral but real: two Config validation bounds are misstated as the formula's own clamps (support_reach is 100..100000 and settlement_spacing is 10..100000, not 100..10000 and 150..10000), which matters because the spec tells the porter to reproduce "raise, do not clamp" via `cfg.validate()` — with the spec's numbers, the recipe-3 reach scaling the spec itself quotes at 96 870 m would throw; the "draw order is the Config keyword order" rule is not what the code does for two of the nine draws (the order happens to come out the same); interior nodes adjacent to a pole have 6 neighbours, not 8; and the consumer list understates who reads the water block — `river_segments`, `nodes` and the three area sums are read by city_planner, castle_planner, hamlet_planner and terrain_religion, not only by the modules named. None of that blocks building route_water/add_water or the four derive_* functions, so implementable stands, but the bound errors should be corrected before anyone ports `Config::validate` from this document.

## Purpose

Two modules that together decide how much water a world has and where it goes.

`terrain_water` is the hydrology kernel. It takes the finished, metre-scaled height field and turns it into a hydrology: which nodes are connected sea, which are lakes standing in filled depressions, what the water surface elevation is everywhere, and — most importantly for every later stage — the *receiver graph*, a forest of "this node drains into that node" links rooted at the ocean. Rivers are then the subset of dry nodes whose accumulated upstream area exceeds a threshold. Climate's rainfall routing, biome relief, settlement siting, freshwater distance, flood risk, ports and sea routes are all built on the blocks this module writes. It is explicitly an *equilibrium* model: depressions are filled to their spill level assuming enough water exists to do it, there is no rainfall budget, no time-dependent filling, no evaporation and no river width.

`terrain_recipes` is the recipe-0 ("auto_parameters") configuration derivation. It turns a bare request (seed, size, phase, world_size, population profile) into a full `Config` by drawing from three named RNG streams, and then — at three points partway through generation — re-derives parameters from what the world actually turned out to be: the river threshold from generated land area, the settlement count/spacing/reach from dry land area, and the hamlet/fortress/college ceilings from the roads, settlements and magic fields. It also builds the `derivation` provenance document that tells a UI where each config value came from. NOTE: this whole module is reachable only on world_recipe 0; recipe 3 (the shipping world) forbids `auto_parameters` and derives the same quantities in `terrain_world`/`terrain_scale` instead, which FWGC++ has already ported.

## Where it sits in the pipeline

**What must exist before `add_water` runs**

`add_water(result, cfg)` is called from `terrain_area.apply_world_scale`, immediately after the world has been scaled to metres and before climate. By then the following must be true:

- `result['layers']['height']` is an n x n grid **in metres** (post `world_scale` multiplication).
- `result['effective_config']` exists and holds `globe_radius` and `sea_level` **in metres**. `add_water` reads the *effective* (physical) config for geometry, never `cfg.globe_radius`.
- `result['layers']`, `result['warnings']` (a list) and `result['timing_ms']` (a dict with a `'total'` key) exist.
- `cfg.river_threshold_km2` is final. On recipe 0 that means `derive_water` has already run (it is called one line earlier); on recipe 3 it was resolved at request time by `terrain_world` scaling the 0.15 km² reference value by `runoff_scale(circumference)`.

**Guard.** `add_water` returns `result` untouched unless `cfg.shape == 'globe'` **and** `cfg.tectonics` is truthy **and** `cfg.phase >= 5`. So stages 1-4 have no water layers at all, and a non-tectonic or plane world never gets hydrology.

**Who runs it, and how often.** Three call sites, and it is *not* run once per world:

1. `terrain_area.apply_world_scale` — the base call, every world.
2. `terrain_history.tectonic_transition` (stage 6) — after the second tectonic epoch rewrites `height`, it calls `measure_surface(result,cfg); add_water(result, replace(cfg, phase=5))`. The `phase=5` substitution exists purely to satisfy the `phase>=5` guard.
3. `terrain_history.carve_relics` (stage 7) — after relic gorges/valleys are incised into `height`, same pair of calls.

Each call completely overwrites the five water layers and the `water` block, **appends the method warning again** (so a full history world carries three identical copies of that string in `warnings`), overwrites `timing_ms['water']` and *adds* to `timing_ms['total']`.

**What consumes its output**

- `terrain_climate.add_climate` reads `layers['water_surface']` (as the moisture source elevation), `layers['water_type'] > 0` (as "this is open water, evaporate here"), and `water['receivers']` — it re-runs its own leaf-to-root accumulation over the *same* receiver graph with rainfall-weighted runoff instead of uniform area, producing `rain_runoff` and `rain_river`. The receiver graph is the single shared routing skeleton.
- `terrain_biomes` reads `layers['water_surface']` for relief above local water.
- `terrain_settlements` reads `layers['water_surface']` at a freshwater source node.
- `terrain_society` reads `max(height, water_surface)` for sky altitudes.
- `terrain_visitation` reads `water['nodes']` to map a node index back to an `(x, z)` cell.
- `terrain_history` snapshots `water_type`, `water_surface`, `water_depth`, `river` (plus `rain_river`) across stages, and `'water'` is in `STATE_KEYS` so the block is delta-captured per build stage.
- `terrain_recipes.derive_population` reads `water['dry_km2']`.

**Where the recipe functions sit** (all in `terrain_area.apply_world_scale`, in this exact order):

```
apply_world_scale: scale layers to metres, compute result['area'], result['layers']['land']
  initialize_derivation(result, cfg)      # builds result['derivation']
  cfg = derive_water(result, cfg)         # sets river_threshold_km2
  result = add_water(result, cfg)         # <-- hydrology
  add_climate; add_terrain_labels
  if phase>=6 and climate: generate_networks; add_environment; add_biome_variants
  cfg = derive_population(result, cfg)    # settlement_count/spacing/support_reach/culture_link_cost
  result = add_settlements(result, cfg)
  cfg = derive_institutions(result, cfg)  # hamlets_per_core/fortress_count/college_count
  add_humans; add_colleges; add_seasonal_food ...
```

`seed_config` is earlier still: `terrain_lab.generate_base` calls it as the very first thing when `cfg.auto_parameters and not cfg.world_recipe`, replacing the request Config with a fully derived one before any terrain is generated.

## Inputs

**`route_water(h, areas, neighbors, sea)`** — pure function, no config:
- `h`: list of per-node ground elevation, metres, one entry per `sphere_grid` node. Typical range after scaling on the reference world: roughly -900 .. +900 m.
- `areas`: per-node spherical cell area, m². Pole nodes carry the area of the whole polar ring.
- `neighbors`: per-node list of `(neighbour_index, great_circle_distance_m)`, sorted ascending by neighbour index. 8 neighbours for interior nodes; a pole node has `n-1` neighbours (the whole adjacent ring) and each ring node has the pole among its neighbours.
- `sea`: sea level in metres (`effective_config['sea_level']`; 0.0 on every generated world today).

**`add_water(result, cfg)`**:
- `cfg.shape` (str), `cfg.tectonics` (int 0/1), `cfg.phase` (int) — guard only.
- `cfg.size` — n, 3..1025, the grid edge.
- `cfg.river_threshold_km2` — float, validated 0.001 .. 10000 km². Reference-world default 0.15; recipe-0 derived value lands 0.01 .. 1.
- `result['effective_config']['globe_radius']` — **physical** radius in metres (design radius × `world_scale`). On the recipe-0 small world this is 10000 × 0.1834617044641644 = 1834.617044641644 m.
- `result['effective_config']['sea_level']` — metres.
- `result['layers']['height']` — n x n metres, indexed `[z][x]`.
- `result['warnings']`, `result['timing_ms']` — mutated.

**`seed_config(request)`** reads `request.seed` (uint32), `request.size`, `request.phase`, `request.population_profile` (str key into the civilization registry), `request.world_size` (`'small'|'medium'|'large'`). Everything else it invents.

**`initialize_derivation(result, cfg)`** reads `cfg.population_profile`, `cfg.auto_parameters`, and every field of `cfg` via `dataclasses.asdict`.

**`derive_water(result, cfg)`** reads `cfg.auto_parameters` and `result.get('area', {}).get('land_km2', 0)` — km², the latitude-weighted above-sea footprint from `terrain_area.land_area`. NOTE the `.get` chain: at `tectonics and phase==1` `apply_world_scale` never writes `result['area']`, so the value is 0 and the threshold floors at 0.01.

**`derive_population(result, cfg)`** reads `cfg.auto_parameters`, requires the key `'water'` in `result`, then reads `result['water']['dry_km2']` (km²) and profile traits `land_per_city_km2` (0.25 .. 1.8 in the shipped registry) and `support_multiplier` (1 for every shipped profile).

**`derive_institutions(result, cfg)`** reads `cfg.auto_parameters`, requires `'roads'` in `result`, then:
- `result['settlements']['sites']` — list of site dicts; uses `rural_population_estimate` and `urban_population_estimate` (both ints; absent on some sites, defaulted to 0).
- `result['roads']['routes']` — list of road dicts; uses `length_m` (float metres) and `river_crossings` (list of 2-element node-pair lists).
- `cfg.support_reach` (metres — the value `derive_population` just wrote), `cfg.size`, `cfg.human_magic_limit`.
- Optionally, when `'magic' in result`: `result['layers']['magic_density']`, `result['layers']['magic_hazard']` (both 0..1 fields written by `terrain_leyline_history.generate_networks`) and `result['layers']['water_type']` (from `add_water`), plus `result['effective_config']['globe_radius']`.

## Outputs

**Layers written by `add_water`** (all n x n, all produced by the same `grid()` scatter helper, so all carry the pole rows and the seam column):

| layer | units | range | meaning |
|---|---|---|---|
| `water_type` | enum | 0, 1 or 2 | 0 dry, 1 ocean, 2 lake |
| `water_depth` | metres | >= 0 | `water_surface - height`, clamped at 0 |
| `water_surface` | metres | >= `height` | the equilibrium spill level; equals `sea_level` on every ocean node |
| `routed_catchment` | m² | >= own cell area | upstream drainage area under uniform unit runoff |
| `river` | 0 or 1 | int | dry node whose catchment crosses the threshold and which has a receiver |

`routed_catchment` has **no consumer anywhere in the Python** — it is published for the viewer only. `river` is the one every later stage reads (and climate publishes a rainfall-weighted sibling, `rain_river`).

**Block written by `add_water`: `result['water']`** (key name is `'water'`; the block has no id field, it is a singleton):

- `version`: int, 1.
- `nodes`: the `sphere_grid` point list, `[(x, z), ...]`, one per node, **in node-index order**. Node 0 is the north pole, the last node is the south pole. This is the identity map from node index to grid cell and other modules index it directly. *It is the same list object the sphere-grid cache holds — never mutate it.*
- `receivers`: list of int, one per node. `-1` means "no receiver": either an ocean node (every ocean node is a root and therefore -1) or, in an oceanless world, the single global minimum.
- `river_segments`: list of `[i, parent[i]]` pairs, one per river node, in ascending node order. `parent[i]` is guaranteed >= 0 by the river predicate.
- `ocean_km2`, `lake_km2`, `dry_km2`: float km². These three sum to the full sphere area (verified: 42.29613777746421 vs `area['total_km2']` 42.2961377774642 at size 65).
- `lake_capacity_m3`: float, `sum(area_i * depth_i)` over lake nodes only. 0 when there are no lakes.
- `has_ocean`: bool.
- `method`, `routing`: fixed English strings, reproduced verbatim in the Constants section. `method` is also appended to `result['warnings']`.

**Also mutated:** `result['timing_ms']['water']` (set) and `result['timing_ms']['total']` (incremented), both in milliseconds.

**Writes by the recipe functions** — all go through `record(result, cfg, changes, sources)`, which does three things and returns a new frozen `Config`:
1. `resolved = dataclasses.replace(cfg, **changes)` — this **re-runs `Config.__post_init__`**, so every derived value is re-validated against the config bounds and an out-of-range derivation raises rather than silently clamping.
2. `result['config'].update(changes)` and `result['effective_config'].update(changes)` — the raw derived value goes into **both**, unscaled. None of the derived keys is in the `world_scale` multiplication list, so design and effective genuinely agree.
3. `result['derivation']['settings'][k] = {'value': v, 'source': sources[k]}` for each change.

- `derive_water` writes `river_threshold_km2` (float km²).
- `derive_population` writes `settlement_count` (int 0..24), `settlement_spacing` (float m, 150..10000), `support_reach` (float m, 100..10000), `culture_link_cost` (float m, = reach × 1.8, so 180..18000).
- `derive_institutions` writes `hamlets_per_core` (int 0..8), `fortress_count` (int 0..24 — note the *Config* bound is 0..1024; 24 is this formula's own ceiling), `college_count` (int 0..12).

**`initialize_derivation` writes** `result['population']` (the validated profile dict), `result['population_profiles']` (list of `{'id','name'}`), and — only when `auto_parameters` — `result['derivation']` = `{'version': 1, 'mode': 'derived', 'settings': {...}, 'note': ...}` where `settings` has one entry per `Config` field, **in dataclass field declaration order**, each `{'value': v, 'source': <string>}`.

## Algorithm

════════════════════════════════════════════════════════════
PART A — `route_water(h, areas, neighbors, sea)`
════════════════════════════════════════════════════════════
Returns `{ocean, level, parent, flow, order}`. Let `N = len(h)`.

────────────────────────────────────────
A1. Find the connected below-sea components
────────────────────────────────────────
This is a plain **flood fill / connected components** over the node graph, restricted to nodes at or below sea level.

```
remaining = { i : h[i] <= sea }          # a set; note <=, not <
components = []
while remaining:
    start = min(remaining)               # smallest node index still unassigned
    remaining.remove(start)
    group = [start]; stack = [start]
    while stack:
        i = stack.pop()                  # LIFO -> depth-first
        for (j, _) in neighbors[i]:
            if j in remaining:
                remaining.remove(j); group.append(j); stack.append(j)
    components.append(group)
```

Notes that matter:
- The membership test is `h[i] <= sea`, **inclusive**. A node exactly at sea level is a below-sea candidate. The land mask elsewhere uses `h > sea_level`, so the two are consistent (a node at exactly sea level is not land and is a water candidate).
- Nodes are claimed out of `remaining` at *push* time, so no node enters two groups.
- `min(remaining)` makes the order of `components` deterministic: ascending by each component's smallest node index.
- `group` order is DFS order and does not reach any output (only membership and the area sum are used).

────────────────────────────────────────
A2. Pick the ocean
────────────────────────────────────────
```
ocean_ids = max(components, key = lambda ids: sum(areas[i] for i in ids)) if components else []
ocean = [False] * N
for i in ocean_ids: ocean[i] = True
```

The ocean is the **single largest-by-area** connected below-sea component. Every other below-sea component is *not* ocean — it will be filled from its rim in step A4 and end up a (deep) lake. Python's `max` returns the **first** maximum, so on an exact area tie the component with the lowest smallest-node-index wins. Compare by summed *area*, never by node count: pole nodes carry a whole ring's area and a node-count comparison would give a different answer.

────────────────────────────────────────
A3. Choose the roots
────────────────────────────────────────
```
roots = ocean_ids or [ min(range(N), key = lambda i: (h[i], i)) ]
root_set = set(roots)
```

If there is no below-sea node anywhere (an oceanless world), the single root is the **global minimum of `h`**, ties broken by smallest node index. This node is the sink the whole planet drains to.

────────────────────────────────────────
A4. Priority flooding (the spill surface)
────────────────────────────────────────
This is **Priority-Flood without epsilon** (Barnes/Lehman/Mulla), producing a monotone "water surface" `level[]` in which no node is lower than the lowest rim it must cross to escape.

```
level  = h[:]            # copy
parent = [-1] * N
visited = set(roots)
queue = []               # min-heap of (level, node_index)
order = []

for i in roots:
    level[i] = sea if ocean[i] else h[i]
    heappush(queue, (level[i], i))

while queue:
    elevation, i = heappop(queue)
    order.append(i)
    for (j, _) in neighbors[i]:
        if j in visited: continue
        visited.add(j)
        parent[j]  = i
        level[j]   = max(h[j], elevation)      # `elevation` is level[i]
        heappush(queue, (level[j], j))
```

Details a reimplementation must get right:

- **Root levels.** An ocean root is pinned to exactly `sea`, *not* to its own `h`. That is what makes the whole ocean one flat surface at sea level and what makes `water_depth` on an ocean node equal `sea - h`. A non-ocean root (the oceanless case) keeps its own height.
- **The priority is `level`, not `h`.** The heap is keyed on the spill level.
- **Tie-break is the node index**, because the heap holds `(level, index)` tuples and tuple comparison falls through to the index. This is load-bearing: large flat regions (the whole ocean sits at exactly `sea`) are settled in ascending index order, and `order` feeds the rank test in A5.
- **Nodes are marked `visited` at push time, not at pop time.** A node's `parent` and `level` are therefore fixed by the *first* node that reaches it, which under a min-heap is the lowest-spill neighbour. Marking at pop time instead would let a later, higher neighbour overwrite the level and the algorithm would be wrong.
- `max(h[j], elevation)` is the whole of the depression filling: a node lower than the rim it was reached across takes the rim's level, i.e. it is under water; a node higher than that rim keeps its own ground.
- **Termination.** The heap empties after exactly N pops: every node is pushed exactly once (guarded by `visited`) and popped exactly once. The sphere node graph is connected, so `len(order) == N` always.
- Cost: O(N log N). At size 1025 that is 1 047 554 nodes.

After this loop, `parent[]` is a *flood tree*: every non-root node points at whichever node first reached it, and a parent always precedes its child in `order`.

────────────────────────────────────────
A5. Re-point receivers to steepest descent
────────────────────────────────────────
Verbatim comment from the source, and it explains exactly why the rank test exists:

> `# Select steepest descent on the spill surface where possible. Flood parents`
> `# route flats without cycles; each receiver precedes its child in order.`

```
rank = {i: k for k, i in enumerate(order)}      # position in pop order
for i in order:
    if i in root_set: continue
    lower = [ ((level[i] - level[j]) / d, j)
              for (j, d) in neighbors[i]
              if d > 0 and level[j] < level[i] and rank[j] < rank[i] ]
    if lower:
        parent[i] = max(lower)[1]
```

- The gradient is measured on the **spill surface `level`**, not on the ground `h`. On a filled lake the surface is flat, no neighbour is strictly lower, `lower` is empty, and the flood parent survives — which is precisely how flat water routes to its outlet without cycles.
- Three filters, all required: `d > 0` (guards a zero great-circle distance), `level[j] < level[i]` (strict), and `rank[j] < rank[i]` (the neighbour was settled earlier).
- The rank filter is what makes the result **acyclic**: every receiver strictly precedes its child in `order`, exactly as the flood parent did.
- `max(lower)` compares `(slope, j)` tuples: **maximum slope, and on an exact slope tie the LARGEST neighbour index**. Note this is the opposite tie-direction from the heap's smallest-index rule. See Ordering hazards.
- This loop is pure with respect to iteration order — it reads only `level` and `rank` and writes only `parent[i]` — so it can be parallelised, but the tie-break must be reproduced exactly.

────────────────────────────────────────
A6. Accumulate catchment
────────────────────────────────────────
```
flow = areas[:]                       # copy; every node starts with its own cell area
for i in reversed(order):
    if parent[i] >= 0:
        flow[parent[i]] += flow[i]
```

Valid because every parent precedes its child in `order` (A4 and A5 both guarantee it), so a single reverse sweep is a correct leaf-to-root accumulation — no queue, no topological sort needed. Units: m². The comment in `add_water` calls this what it is:

> `# Uniform unit runoff proxy, not rainfall volume. River width is not modelled.`

Since every ocean node is a root with `parent == -1`, flow never crosses the ocean; each ocean node holds only its own cell area plus whatever land drained into it.

Return `{'ocean': ocean, 'level': level, 'parent': parent, 'flow': flow, 'order': order}`.

════════════════════════════════════════════════════════════
PART B — `add_water(result, cfg)`
════════════════════════════════════════════════════════════

```
B0. if cfg.shape != 'globe' or not cfg.tectonics or cfg.phase < 5: return result unchanged
B1. started = now(); n = cfg.size; physical = result['effective_config']
B2. points, areas, neighbors = sphere_grid(n, physical['globe_radius'])
B3. h = [ layers['height'][z][x] for (x, z) in points ]      # note the index swap
B4. routed = route_water(h, areas, neighbors, physical['sea_level'])
B5. depth = [ max(0, level_i - h_i) for level_i, h_i in zip(routed['level'], h) ]
B6. kind[i] = 1 if routed['ocean'][i]
              else 2 if depth[i] > 1e-7
              else 0
B7. river[i] = (kind[i] == 0)
               and routed['parent'][i] >= 0
               and routed['flow'][i] >= cfg.river_threshold_km2 * 1e6
B8. scatter each of (kind, depth, level, flow, int(river)) into an n x n grid
B9. build river_segments, the water block, the warning, the timing
```

**B3 index order.** `points` entries are `(x, z)` but layers are indexed `[z][x]`. Getting this backwards transposes the world silently.

**B6 ordering of the tests.** Ocean is tested *first*: an ocean node is type 1 whatever its depth, including a node sitting exactly at sea level with depth 0. Only then does the lake epsilon apply.

**B7** uses `cfg.river_threshold_km2` — the *design* config, not `effective_config`. They are equal (the key is not in the scale list), but reading the wrong one is a trap if that ever changes. `* 1e6` converts km² to m² to compare against `flow`.

**B8 — the scatter helper**, verbatim in structure:

```
def grid(values):
    out = [[0.] * n for _ in range(n)]
    for (x, z), v in zip(points, values): out[z][x] = v
    out[0]  = [values[0]]  * n          # north pole row  = node 0
    out[-1] = [values[-1]] * n          # south pole row  = last node
    for row in out: row[-1] = row[0]    # close the longitude seam
    return out
```
`points[0]` is `(0, 0)` and `points[-1]` is `(0, n-1)`, so `values[0]` and `values[-1]` are exactly the two pole nodes. This is identical in meaning to `fwg::SphereGrid::to_grid`.

**B9 — segments and the block**

```
segments = []
for i, active in enumerate(river):
    if active: segments.append([i, routed['parent'][i]])
```
Ascending node order. `parent[i] >= 0` is guaranteed by B7.

```
ocean_km2       = sum(areas[i] for kind[i]==1) / 1e6
lake_km2        = sum(areas[i] for kind[i]==2) / 1e6
dry_km2         = sum(areas[i] for kind[i]==0) / 1e6
lake_capacity_m3= sum(areas[i]*depth[i] for kind[i]==2)
has_ocean       = any(routed['ocean'])
```
All four are straight left-to-right sums over node index order.

Finally `result['warnings'].append(method)` and `timing_ms['water'] = elapsed; timing_ms['total'] += elapsed`.

**Measured sanity values** (recipe 0, seed 42, world_size small, phase 5, so physical radius 1834.617044641644 m, sphere area 42.2961 km²):

| size | river_threshold_km2 | land_km2 | ocean_km2 | lake_km2 | dry_km2 | lake_capacity_m3 | nodes | river segs | outlets |
|---|---|---|---|---|---|---|---|---|---|
| 33 | 0.12868266184267615 | 14.155092802694377 | 28.141044974769844 | 0.0 | 14.155092802694378 | 0 | 994 | 44 | 690 |
| 65 | 0.1516711800613214 | 16.683829806745354 | 25.612307970718863 | 0.23386801574286017 | 16.449961791002487 | 888167.153252486 | 4034 | 40 | 2607 |

Note at size 65 `dry_km2` (16.4500) is *less* than `area['land_km2']` (16.6838) by exactly the lake area — lakes stand on land. At size 33 there happened to be no depression deep enough to clear the 1e-7 epsilon, so the two agree to a float ulp. "Outlets" is the count of `receivers == -1`, which equals the ocean node count (690 and 2607 respectively) because every ocean node is a root.

════════════════════════════════════════════════════════════
PART C — `seed_config(request)`
════════════════════════════════════════════════════════════

Builds a complete `Config` from a bare request. Three independent RNG streams, each `random.Random(child_seed(request.seed, <domain>))` where `child_seed(master, domain, variation=0)` is the first 4 bytes, big-endian, of `sha256("tectonics-v1:{master}:{domain}:{variation}")` — i.e. exactly `fwg::child_seed`.

| stream | domain string |
|---|---|
| geology (`rng`) | `'recipe-v1-geology'` |
| weather | `'recipe-v1-climate'` |
| magic | `'recipe-v1-magic'` |

**Draw order is the source order of the `Config(...)` keyword arguments** — Python evaluates them left to right. Getting this order wrong changes every world.

Geology stream, in order:
1. `count = rng.randint(9, 15)` → `plate_count`
2. `relief = rng.uniform(650, 850)` → `tectonic_relief`
3. `crust_bias = rng.uniform(-0.08, 0.08)`
4. `mountain_detail = rng.uniform(0.55, 0.80)`

Weather stream, in order:
1. `wind_bearing = weather.uniform(0, 360)`
2. `temperature_offset = weather.uniform(-3, 3)`
3. `moisture_bias = weather.uniform(-0.06, 0.06)`

Magic stream, in order:
1. `ley_nodes = magic.randint(16, 20)`
2. `magic_instability = magic.uniform(0.5, 0.85)`

Closed-form fields:
```
radius        = 10000 * {'small':1, 'medium':2, 'large':3}[request.world_size]   # DESIGN radius
world_scale   = RECIPE_WORLD_SCALE = 0.1834617044641644
globe_radius  = radius
belt_width    = 0.28 / sqrt(plate_count)
amplitude     = tectonic_relief * 1.5
wavelength    = radius * 1.6 / sqrt(plate_count)
rain_passes   = min(128, max(24, ceil(request.size * 0.375)))
ley_width     = radius * RECIPE_WORLD_SCALE * 0.1
```
Profile-sourced fields (`p = get_profile(request.population_profile)`):
```
human_magic_limit = p['mutation_limit']
road_max_grade    = p['road_grade_limit']
human_adaptation  = p['irrigation']
urban_food_demand = p['food_demand']
stubbornness      = p['difficult_fraction']
```
Passed straight through: `seed`, `size`, `phase`, `population_profile`, `world_size`. Pinned: `shape='globe'`, `tectonics=1`, `auto_parameters=1`.

**Everything else takes the `Config` dataclass default**, notably `sea_level=0.0`, `erosion_passes=6`, `erosion_strength=0.5`, `octaves=5`, `ridge=0.55`, `radius=500.0` (TPI neighbourhood), `orogeny=1.0`, `world_recipe=0`, `world_options='{}'`.

Worked example, seed 42, size 33, small:
`plate_count=11`, `tectonic_relief=732.86701805036`, `belt_width=0.08442317648177382`, `crust_bias=0.005572131245612191`, `amplitude=1099.30052707554`, `wavelength=4824.181513244218`, `mountain_detail=0.6891058762974224`, `wind_bearing=48.261668825907634`, `temperature_offset=-0.38209059272837465`, `moisture_bias=-0.02112516025099013`, `rain_passes=24`, `ley_nodes=19`, `ley_width=183.46170446416443`, `magic_instability=0.7959662510039553`.

════════════════════════════════════════════════════════════
PART D — `initialize_derivation(result, cfg)`
════════════════════════════════════════════════════════════

```
result['population']          = get_profile(cfg.population_profile)
result['population_profiles'] = profile_options()          # [{'id','name'}, ...]
if not cfg.auto_parameters: return                          # no 'derivation' key at all
```

Then, for every field of `asdict(cfg)` **in dataclass declaration order**, classify the source by this cascade — the order of the tests is the specification:

```
'user choice'              if k in direct
'population profile'       elif k in prof
'seed stream v1'           elif k in seeded
formulas[k]                elif k in formulas
'world recipe constant'    elif k in fixed
'solver/default constant'  otherwise
```

The sets, verbatim:
```
direct  = {world_size, seed, size, phase, population_profile, auto_parameters}
prof    = {human_magic_limit, road_max_grade, human_adaptation, urban_food_demand, stubbornness}
seeded  = {plate_count, tectonic_relief, crust_bias, mountain_detail,
           wind_bearing, temperature_offset, moisture_bias, ley_nodes, magic_instability}
fixed   = {shape, tectonics, globe_radius, world_scale, sea_level,
           erosion_passes, erosion_strength, magic_enabled}
formulas = {
  'globe_radius': 'world_size preset: 1x, 2x or 3x small radius',
  'belt_width':   'plate count',
  'amplitude':    'tectonic relief',
  'wavelength':   'plate count and radius',
  'ley_width':    'physical radius',
  'rain_passes':  'grid resolution' }
```

**`globe_radius` is in BOTH `fixed` and `formulas`, and `formulas` is tested first**, so it reports `'world_size preset: 1x, 2x or 3x small radius'`, never `'world recipe constant'`. Verified by running it. Do not "tidy" this by moving the tests.

Result shape:
```
result['derivation'] = {'version': 1, 'mode': 'derived',
   'settings': { field: {'value': v, 'source': s}, ... },
   'note': 'Same seed recipe for every population. Shared scale targets roughly 20 km² land on average; no per-seed area fitting. Derived inputs supersede expert values.'}
```

════════════════════════════════════════════════════════════
PART E — `record(result, cfg, changes, sources)`
════════════════════════════════════════════════════════════
```
resolved = dataclasses.replace(cfg, **changes)     # re-validates the whole Config
result['config'].update(changes)
result['effective_config'].update(changes)
for k, v in changes.items():
    result['derivation']['settings'][k] = {'value': v, 'source': sources[k]}
return resolved
```
Two consequences worth stating: (a) an out-of-bounds derivation **raises** from `Config.__post_init__` rather than being clamped; (b) `result['config']` is the regeneration record, so a world regenerated from it gets the derived values directly and the derivation is idempotent.

════════════════════════════════════════════════════════════
PART F — `derive_water(result, cfg)`
════════════════════════════════════════════════════════════
```
if not cfg.auto_parameters: return cfg
area = result.get('area', {}).get('land_km2', 0)          # km², 0 if area not computed
river_threshold_km2 = max(0.01, min(1, area / 110))
source: 'generated above-sea land area / 110, bounded 0.01..1 km²'
```
That is the entire function. `max(0.01, min(1, x))` — floor 0.01, ceiling 1.0. At seed 42 size 33 it resolves 0.12868266184267615; at size 65, 0.1516711800613214 — i.e. it lands near the 0.15 km² recipe-3 reference default, which is the calibration target.

════════════════════════════════════════════════════════════
PART G — `derive_population(result, cfg)`
════════════════════════════════════════════════════════════
```
if not cfg.auto_parameters or 'water' not in result: return cfg
p    = get_profile(cfg.population_profile)
area = result['water']['dry_km2']                          # km², NOT land_km2

count   = min(24, floor(area / p['land_per_city_km2'])) if area > 0 else 0
spacing = max(150, min(10000, sqrt(area * 1e6 / max(1, count)) * 0.27))
reach   = max(100, min(10000, spacing * 2.2 * p['support_multiplier']))
culture_link_cost = reach * 1.8
```
`area * 1e6` converts km² to m², so `spacing` and `reach` come out in metres. `max(1, count)` guards the zero-city case: with no cities the spacing becomes `sqrt(total dry area) * 0.27`.

Verbatim comment, which is the only record of why the 10000 ceiling is where it is:

> `# The ceiling is the settlement_spacing config bound, not a world-size assumption.`
> `# At 1500 m a world wider than about 25 km had every city clamped into one region:`
> `# the formula wants 3.2 km at a 200 km circumference and 8.6 km at 1000 km.`

Source strings:
```
settlement_count   : 'dry land area / population land-per-city trait; maximum 24'
settlement_spacing : 'square root of dry land per requested city × 0.27'
support_reach      : 'city spacing × 2.2 × population support capability'
culture_link_cost  : 'derived support reach × 1.8'
```

════════════════════════════════════════════════════════════
PART H — `derive_institutions(result, cfg)`
════════════════════════════════════════════════════════════
```
if not cfg.auto_parameters or 'roads' not in result: return cfg
sites  = result['settlements']['sites']
routes = result['roads']['routes']

rural   = max((s.get('rural_population_estimate', 0) for s in sites), default=0)
hamlets = min(8, ceil(rural / 40))

crossings = { tuple(sorted(edge)) for road in routes for edge in road['river_crossings'] }
forts     = min(24, floor(sum(road['length_m'] for road in routes) / cfg.support_reach)
                    + len(crossings))

count = 0
if sites and 'magic' in result:
    points, areas, _ = sphere_grid(cfg.size, result['effective_config']['globe_radius'])
    L = result['layers']
    opportunity = sum(areas[i] * L['magic_density'][z][x]
                      for i, (x, z) in enumerate(points)
                      if L['water_type'][z][x] == 0
                     and L['magic_density'][z][x] >= 0.35
                     and L['magic_hazard'][z][x] <= cfg.human_magic_limit)
    residents = sum(s.get('urban_population_estimate', 0) for s in sites)
    count = min(12, floor(opportunity / (pi * 150**2)), residents // 100)
```

- `hamlets` uses the **single largest** site's rural population, not the total — that is what `max(...)` means here, and the source string says so explicitly ("each city uses its own residents"). `terrain_humans` then re-applies the same `ceil(rural/40)` per site against this ceiling.
- `crossings` is a **set of sorted 2-tuples**, so the same river crossing shared by two roads counts once.
- `cfg.support_reach` is the value `derive_population` wrote earlier in the same pipeline, in metres.
- The college term is an **area-weighted magic opportunity**: sum of cell area times magic density over cells that are dry, dense enough (>= 0.35) and safe enough (hazard <= the profile's `mutation_limit`), divided by the area of a 150 m-radius disc — i.e. "how many 150 m magical footprints fit". It is then capped by 12 and by one college per 100 urban residents.
- `residents` is an integer sum (`urban_population_estimate` is `round(residents*0.55)`), so `residents // 100` is an int and `count` is an int, as `Config` requires.

Source strings, verbatim:
```
hamlets_per_core : 'maximum city rural residents / 40, rounded up; each city uses its own residents; ceiling 8'
fortress_count   : 'road length / support reach plus unique river crossings; ceiling 24; strategic placement still required'
college_count    : 'safe dense magic footprint and urban residents / 100; ceiling 12; host eligibility and access still required'
```

## Constants

**Hydrology**

- `1e-7` — the lake depth epsilon, metres. A non-ocean node is a lake iff `water_surface - height > 1e-7`. Raise it and shallow genuine lakes disappear; remove it and essentially every land node becomes a one-ulp puddle, because the priority-flood surface sits a rounding error above the ground almost everywhere. (Already named `fwg::kLakeDepthEpsilon` in `water.hpp`.)
- `1e6` — km² to m², used to compare `cfg.river_threshold_km2` against the accumulated `flow`, and to convert the three area sums back to km² for the report.
- `sea_level` — metres, from `effective_config`. 0.0 on every world generated today. It is both the ocean-membership test (`h <= sea`) and the pinned ocean surface level.
- `0.15 km²` — the `river_threshold_km2` dataclass default, the reference world's value. Verbatim from `terrain_lab.Config.__post_init__`, and the bound is 0.001..10000 km²:
  > `# 10000, not 100: the default 0.15 km2 is the 11.15 km reference world's value and`
  > `# recipe 3 scales it as the square of the width, resolving 48.27, 193.08 and 434.44`
  > `# km2 at the 200/400/600 km presets. At 100 the medium and large presets raised`
  > `# here instead of generating. terrain_world.RIVER_THRESHOLD_MAX_KM2 mirrors this.`

**Fixed report strings** (reproduce verbatim — the first is also appended to `warnings`):

- `method`: `"Largest connected below-sea component is the ocean. Inland depressions are filled to their spill levels assuming sufficient water. Water extents do not use a rainfall budget or time-dependent filling; lake evaporation and river width are not modelled."`
- `routing`: `"Receivers index unique spherical nodes; -1 is an ocean outlet or the global minimum in an oceanless world. Catchment assumes uniform unit runoff."`
- Module docstring: `"Connected ocean and equilibrium lake spill routing on the physical globe."`

**Recipe constants**

- `RECIPE_WORLD_SCALE = 0.1834617044641644`, with the comment: `# One shared calibration across seeds 0..31 at grid 65; never fitted per world.` **This is NOT the same as `Config.world_scale`'s default of 0.17744123532462844.** The recipe-0 small world is 10000 × 0.1834617 = 1834.617 m physical radius = **11.53 km circumference**; the recipe-3 reference world (`terrain_scale.REFERENCE_RADIUS_M = 1774.4123532462844`) is **11.15 km**. Two different reference planets; do not conflate them.
- `10000` — the small-world **design** radius; `world_size` multiplies it by 1, 2 or 3. Note this is design space; the physical radius is that times `world_scale`.
- Plate count draw `randint(9, 15)`; tectonic relief draw `uniform(650, 850)` metres (design space).
- `0.28` in `belt_width = 0.28/sqrt(plate_count)` — keeps boundary belts a constant share of a plate's angular span. `Config` bounds `belt_width` to 0.01..0.3, so a plate count below ~0.87 would break it; 9..15 gives 0.0723..0.0933.
- `1.5` in `amplitude = tectonic_relief * 1.5` — the recipe-0 relief-to-detail ratio. Recipe 3 uses 1100/800 = 1.375 (`scale::kAmplitudeRatio`) instead. Another pair of near-but-different constants.
- `1.6` in `wavelength = radius * 1.6 / sqrt(plate_count)`. Tying wavelength to radius is what keeps the resolved octave count scale-invariant; from `terrain_scale.detail_wavelength`:
  > `"Tying wavelength to the radius is what makes the resolved octave count scale-invariant: leave it at a fixed 4300 m and a world an order of magnitude wider resolves no surface noise at all."`
- `crust_bias` draw `uniform(-0.08, 0.08)`; `mountain_detail` draw `uniform(0.55, 0.80)`.
- `rain_passes = min(128, max(24, ceil(size * 0.375)))` — floor 24, ceiling 128 (the `Config` bound is 1..128). 0.375 ≈ 3/8 of the grid edge: transport must cross the world. At size 33 this floors at 24; it starts binding above size 64.
- `ley_nodes` draw `randint(16, 20)` (`Config` bound 3..24); `ley_width = design_radius * RECIPE_WORLD_SCALE * 0.1` — i.e. 10% of the *physical* radius (`Config` bound 10..2000 m, so a design radius above 109 000 would break it); `magic_instability` draw `uniform(0.5, 0.85)`.
- Weather draws: `wind_bearing uniform(0,360)` degrees; `temperature_offset uniform(-3,3)` °C (bound ±40); `moisture_bias uniform(-0.06,0.06)` (bound ±1).

**Derivation constants**

- `110` in `river_threshold_km2 = max(0.01, min(1, land_km2/110))`. The divisor is what makes the derived value land near 0.15 km² on the ~42 km² recipe-0 sphere: at 14-17 km² of land it gives 0.129-0.152. Lower it and rivers become rarer (a higher threshold); raise it and the land saturates with rivers.
- Bounds `0.01 .. 1` km² on that threshold.
- `p['land_per_city_km2']` — profile trait, 0.25 (tidekin) to 1.8 (human_desert) in the shipped registry; validated 0.01..1000. Cities per world = dry_km2 / this, capped 24 (which is also the `Config` bound on `settlement_count`).
- `0.27` in `spacing = sqrt(dry_m2 / cities) * 0.27`. The square root is the side of the square each city would own; 0.27 makes the *spacing* about a quarter of that side. Raise it and cities repel each other out of the good ground.
- Bounds `150 .. 10000` metres on spacing; see the verbatim comment in Algorithm Part G for what the ceiling actually is.
- `2.2` in `support_reach = spacing * 2.2 * support_multiplier` — a city farms out to a bit more than the distance to its neighbour, so supports overlap. Bounds `100 .. 10000` metres. `support_multiplier` is 1 for every shipped profile, validated 0 < x <= 10.
- `1.8` in `culture_link_cost = support_reach * 1.8` — a culture reaches nearly twice as far as its food does.
- `40` in `hamlets = min(8, ceil(largest_rural/40))` — 40 rural residents per hamlet. Ceiling 8 (also the `Config` bound on `hamlets_per_core`).
- `24` — the `fortress_count` ceiling *in this formula*. The `Config` bound is 0..1024, and the dataclass default 1024 with its own note: `# A ceiling, not a count. The default sits at the bound so it never binds and the generated road network decides how much route defence a world asks for.`
- `0.35` — minimum `magic_density` for a college-eligible cell. `cfg.human_magic_limit` (= the profile's `mutation_limit`, 0.45 for humans/dwarves, 0.55 gnome, 0.6 elf/tidekin) is the maximum tolerable `magic_hazard`.
- `pi * 150**2 = 70685.834705770345` m² — the footprint of one college, a 150 m-radius disc. Divide the safe dense magic area by this to get "how many colleges the world can host".
- `100` — urban residents per college; `12` — the college ceiling (also the `Config` bound).

**Reach/area scaling (recipe 3, for contrast — already ported)**

- `REFERENCE_RADIUS_M = 1774.4123532462844`, `REFERENCE_CIRCUMFERENCE_M = 2π × that ≈ 11150.9 m`.
- `reach_scale(C) = C / REFERENCE_CIRCUMFERENCE_M`; `runoff_scale(C) = reach_scale(C)**2`.
- `REACH_MAX_M = 100000`, `RIVER_THRESHOLD_MAX_KM2 = 10000` — these **raise** rather than clamp, deliberately.
- Preset circumferences: small 200 km, medium 400 km, large 600 km. Derived river thresholds 48.27 / 193.08 / 434.44 km².

## Data files

**`Sim/icarus_sim/civilizations.json`** — the master civilization registry, loaded through `civilization_registry._document()` and exposed as `population_profiles()`. `terrain_recipes` never reads the file directly; it goes through `terrain_profiles.get_profile(profile_id)`, which validates the record and adds `id`, `schema_version: 4` and a `definition_hash` (sha256 of the json-sorted record).

Top-level keys: `schema`, `schema_version`, `revision`, `description`, `defaults`, `city_classification`, `configurations`, `building_catalogue`, `civilization_order`, `parent_races`, `founding_rules`, plus `entities` and `building_packs` merged in by the registry loader.

`population_profiles()` returns `{**{key: entity['population'] for key, entity in doc['entities'].items()}, **doc['configurations']}` — i.e. one profile per civilization entity plus the authored aggregate configurations. `doc['configurations']` currently holds exactly one entry, `'mixed'`. Thirteen profiles total. `doc['defaults']['profile_id']` is `'human_heartland'` and is the `Config.population_profile` default.

**The five traits `seed_config` pulls, and the two `derive_population`/`derive_institutions` pull**, for every shipped profile:

| profile | land_per_city_km2 | support_multiplier | mutation_limit | road_grade_limit | irrigation | food_demand | difficult_fraction |
|---|---|---|---|---|---|---|---|
| human_heartland | 1.6 | 1 | 0.45 | 0.35 | 0.7 | 10 | 0.2 |
| human_maritime | 1.6 | 1 | 0.45 | 0.35 | 0.7 | 10 | 0.2 |
| human_desert | 1.8 | 1 | 0.45 | 0.40 | 0.9 | 10 | 0.2 |
| human_cold | 0.9 | 1 | 0.45 | 0.45 | 0.55 | 10 | 0.2 |
| human_large_island | 1.6 | 1 | 0.45 | 0.45 | 0.75 | 10 | 0.2 |
| human_rainforest | 1.6 | 1 | 0.45 | 0.40 | 0.5 | 10 | 0.2 |
| dwarf | 1.0 | 1 | 0.45 | 0.55 | 0.7 | 10 | 0.2 |
| hill_dwarf | 0.7 | 1 | 0.45 | 0.35 | 0.7 | 8 | 0.2 |
| frosthold_dwarf | 0.55 | 1 | 0.45 | 0.55 | 0.7 | 10 | 0.2 |
| elf | 1.2 | 1 | 0.60 | 0.35 | 0.4 | 8 | 0.2 |
| gnome | 0.35 | 1 | 0.55 | 0.35 | 0.7 | 8 | 0.2 |
| tidekin | 0.25 | 1 | 0.60 | 0.35 | 0.7 | 8 | 0.2 |
| mixed | 1.6 | 1 | 0.45 | 0.35 | 0.7 | 10 | 0.2 |

Mapping into config: `mutation_limit → human_magic_limit`, `road_grade_limit → road_max_grade`, `irrigation → human_adaptation`, `food_demand → urban_food_demand`, `difficult_fraction → stubbornness`. `land_per_city_km2` and `support_multiplier` are read at `derive_population` time; `mutation_limit` is read again (as `cfg.human_magic_limit`) in the college term.

Validation bounds that a C++ profile loader must keep (`terrain_profiles.validate_profile`): `land_per_city_km2` in 0.01..1000, `support_multiplier` in (0, 10], `mutation_limit`/`irrigation`/`difficult_fraction` in 0..1, `road_grade_limit` in 0.01..1, `food_demand` in 0..10000. A profile record must carry **exactly** the `FIELDS` set plus `civilization` — missing or extra keys raise `'Civilization requires a complete independent trait record'`.

`terrain_water` reads **no data files at all**.

Registry identity note carried from elsewhere in the codebase: `civilization_registry.registry_identity()` hashes the canonical JSON, and changing bytes in `civilizations.json` invalidates every saved world. A C++ port that embeds these traits rather than loading the file must decide how it reports that identity.

## Random streams

**`terrain_water` draws nothing.** `route_water` and `add_water` are fully deterministic functions of the height field, the grid geometry and the sea level. No stream, no seed, no shuffle.

**`terrain_recipes.seed_config`** draws from three independent CPython `random.Random` instances, each seeded with a 32-bit `child_seed`:

| stream | seed | draws | count |
|---|---|---|---|
| geology | `child_seed(seed, 'recipe-v1-geology')` | `randint(9,15)`, `uniform(650,850)`, `uniform(-.08,.08)`, `uniform(.55,.8)` | exactly 4 |
| weather | `child_seed(seed, 'recipe-v1-climate')` | `uniform(0,360)`, `uniform(-3,3)`, `uniform(-.06,.06)` | exactly 3 |
| magic | `child_seed(seed, 'recipe-v1-magic')` | `randint(16,20)`, `uniform(.5,.85)` | exactly 2 |

`child_seed(master, domain, variation=0)` = first four bytes, big-endian, of `sha256(f'tectonics-v1:{master}:{domain}:{variation}')`. This is `fwg::child_seed` unchanged — but note it produces a **32-bit** value, which is the noise-field derivation. For an RNG *stream* the C++ convention is `Rng::child(master, domain, variation)` on top of `child_seed64`. Using the 32-bit one to seed an engine would be a fourth derivation; use `Rng::child` with the same three domain strings.

**Draw counts are fixed and data-independent** in all three streams — nine draws total, always, regardless of seed, size or profile. There is no rejection loop, no conditional draw, no `sample`/`shuffle`. That makes this one of the easy ports: stream discipline cannot drift.

`derive_water`, `derive_population`, `derive_institutions` and `initialize_derivation` draw nothing.

Since this is a clean rewrite, the actual values will differ from Python's Mersenne Twister. What must be preserved is the *count and order* of draws per stream, and the fact that the three streams are independent so that adding a draw to one cannot move the others.

## Ordering hazards

Nine places where container or iteration order reaches output. All of them are in `route_water` unless noted.

1. **`min(remaining)` when starting a flood-fill component.** `remaining` is a `set`; `min` over it is deterministic, and it makes `components` ordered by ascending smallest-member index. In C++ use the lowest index not yet claimed — scan a `std::vector<char> pending` from 0, or keep a cursor.

2. **`max(components, key=area_sum)` picks the FIRST maximum on a tie.** Two below-sea components with bit-identical total area: the one whose smallest node index is lower becomes the ocean and the other becomes a lake. `std::max_element` has the same first-max semantics, so it maps directly — but only if `components` is built in the ascending-smallest-index order from (1). Realistically a tie needs exact float equality of two area sums, but the sphere grid is latitudinally symmetric and pole rings carry identical areas, so a north/south mirrored pair of basins is not absurd.

3. **`min(range(N), key=lambda i: (h[i], i))` for the oceanless root** — argmin of height, tie-broken by *smallest* index. Remember node index is essentially latitude (node 0 is the north pole, index increases southward), so an index tie-break is a latitude preference, not a neutral one.

4. **The heap is keyed on `(level, index)`, so equal levels pop in ascending index order.** This is not incidental: the entire ocean sits at exactly `sea`, so *every* ocean root ties, and the whole flat sea is settled in ascending node order. That order becomes `order`, which becomes `rank`, which gates the steepest-descent re-point in step A5. A C++ `std::priority_queue<std::pair<double,int>, vector, greater<>>` reproduces this exactly (pair compares lexicographically). A priority queue that breaks ties by insertion order instead will produce a different receiver graph.

5. **`for j,_ in neighbors[i]` in the flood loop.** `neighbors[i]` is `sorted(ids)`, ascending by node index. In this particular loop the order does *not* reach output — every unvisited neighbour of the popped node is claimed by that same node, so which one is touched first is irrelevant. Keep the sorted order anyway; it is load-bearing elsewhere in the codebase and free here.

6. **`max(lower)[1]` in the steepest-descent re-point compares `(slope, j)` tuples, so an exact slope tie goes to the LARGEST neighbour index.** This is the opposite direction from the heap's tie rule in (4). `std::max_element` returns the *first* maximum, which would give the smallest index — wrong. Write the loop by hand: keep the candidate if `slope > best_slope || (slope == best_slope && j > best_j)`. Exact slope ties are common on the sphere grid: the eight neighbours of an interior node come in symmetric distance classes, and a plateau of equal `level` values with two equidistant lower neighbours ties exactly.

7. **`for i in reversed(order)` in the flow accumulation.** Reverse pop order is a valid reverse-topological order only because every parent precedes its child in `order`. Preserve `order` as an explicit vector; do not substitute "reverse node index" or a freshly computed topological sort, both of which would change the float summation order and, for a sort, could change it non-deterministically.

8. **The four report sums (`ocean_km2`, `lake_km2`, `dry_km2`, `lake_capacity_m3`) are left-to-right in node-index order.** Float addition is not associative; a different traversal or a parallel reduction gives a different last bit. Since numbers need not match, this only matters for *internal* reproducibility — but it must be reproducible run to run.

9. **`asdict(cfg)` in `initialize_derivation` iterates in dataclass field declaration order**, and `result['derivation']['settings']` is a dict, so the JSON emits in that order. If a UI or a diff depends on it, the C++ port must pin an explicit field order rather than use `std::map<std::string,...>`, which would emit alphabetically. (Field order starts: `world_recipe, world_options, world_size, auto_parameters, population_profile, shape, globe_radius, tectonics, ...`.)

10. **`crossings = {tuple(sorted(edge)) for ...}` in `derive_institutions`** is a set of *sorted* pairs — the normalisation is what dedupes a crossing that two roads share. Only `len(crossings)` is used, so the set's iteration order never escapes; a `std::set<std::pair<int,int>>` with sorted pairs is exact.

Not applicable here, but recorded because the task asks: `ICARUS_CITY_WORKERS` and the settlement-planner process pools live in `city_planner`/`hamlet_planner`, not in these two modules. `derive_institutions` *reads* the output of those pools (`settlements.sites`, `roads.routes`), so any nondeterminism they introduce flows straight into `hamlets_per_core`, `fortress_count` and `college_count`.

## Edge cases

**`route_water`**

- **No below-sea node at all** (`components` empty): `ocean_ids = []`, `ocean` is all false, and the single root is the global minimum of `h`. `has_ocean` is false, `ocean_km2 == 0`. The root's `level` is its own `h`, not `sea`. Every other node floods from it. Rivers still form; the single outlet is that minimum. The `routing` string names this case explicitly.
- **Every node below sea** (a drowned world): one component containing everything, all nodes are ocean roots, all receivers `-1`, `dry_km2 == 0`, no rivers, no lakes, `flow[i] == areas[i]` everywhere.
- **A below-sea component that is not the largest** becomes a lake, filled from its rim upward by the priority flood. Its `level` can exceed `sea` even though its ground is below sea — a sub-sea-level lake with a high rim (a Dead Sea). This is correct and intended.
- **Ocean node at exactly sea level**: `depth == 0` but `kind == 1`, because the ocean test precedes the depth test.
- **`d > 0` guard** in the steepest-descent filter. Two distinct sphere-grid nodes always have distinct directions so the great-circle distance is positive, but the guard exists and costs nothing; keep it. (`acos` is clamped to [-1,1] inside `sphere_grid` for the same class of reason.)
- **Poles.** A pole is a single node adjacent to the entire ring below (or above) it, carrying the whole ring's area (`areas[i] *= (n-1)` for `z in (0, n-1)`). It participates in flooding and routing like any other node, and can be a river mouth or an ocean root. A pole node's own `water_depth` is scattered across its whole grid row by the `grid()` helper.
- **The longitude seam.** Column `n-1` is not a node; it is filled from column 0 at the end. `index(x, z)` wraps `x` modulo `n-1`, so a node at `x == n-2` is a neighbour of the node at `x == 0`.
- **Minimum grid.** `size == 3` gives 4 nodes: north pole, two equatorial nodes, south pole. Everything still runs.
- **Graph connectivity.** The sphere graph is always connected, so `len(order) == N` and no node is left unvisited. Do not write code that silently tolerates an unvisited node; assert it.
- **Cycles.** Impossible by construction: `rank[parent[i]] < rank[i]` for every `i` with a parent. Worth an assertion in a debug build, because the whole flow accumulation depends on it.

**`add_water`**

- **Guard returns the result untouched** when shape is not globe, tectonics is off, or `phase < 5`. Phases 1-4 have no water layers, so anything reading `water_type` must handle its absence.
- **Called three times on a history world** (base, post-tectonic-transition, post-relic-carving). Every layer and the whole `water` block are overwritten; the method warning is appended each time (three duplicates); `timing_ms['water']` is overwritten while `timing_ms['total']` accumulates.
- **`points` is the cached, shared sphere-grid list** and is stored *by reference* into `result['water']['nodes']`. In Python mutating it would corrupt the `lru_cache`. In C++ this is `std::shared_ptr<const SphereGrid>`, which makes it impossible.
- **No lakes is normal.** At seed 42 size 33 there were zero lake nodes; `lake_capacity_m3` is `0` (the integer, from an empty `sum`). Do not treat an empty lake set as a failure.
- **Zero rivers is possible** on a threshold that is too high for the raster; the recipe-3 comment records that the unscaled 0.15 km² default made *every* land cell a river at 200 km (100% land-river fraction at sizes 17 and 33), which is the same defect from the other side.

**Recipe derivation**

- **`derive_water` with no `area` block.** At `tectonics and phase == 1`, `apply_world_scale` never computes `result['area']`; `result.get('area', {}).get('land_km2', 0)` returns 0 and the threshold floors at 0.01 km². Both `.get`s are needed.
- **`derive_population` with `dry_km2 == 0`** (fully drowned world): `count = 0` by the explicit `if area > 0 else 0` branch, then `max(1, count)` keeps the spacing division alive and `sqrt(0) * 0.27 = 0` floors to 150 m, reach floors to 330 m (= 150 × 2.2 × 1), `culture_link_cost` 594 m.
- **`derive_population` when `'water'` is absent** (phase < 5): returns `cfg` unchanged, so `settlement_count` etc. keep their dataclass defaults (6 / 450 / 1000 / 1800).
- **`derive_institutions` when `'roads'` is absent**: returns unchanged. When `sites` is empty, `max(..., default=0)` gives `rural = 0`, `hamlets = min(8, ceil(0/40)) = 0`, and the college branch is skipped (`if sites and ...`), giving `count = 0`.
- **`derive_institutions` with no `magic` block** (phase < 6, or climate missing): `count = 0`.
- **`routes` empty**: `sum(...) = 0`, `crossings` empty, `forts = 0`.
- **`record` re-validates.** Every derived value flows through `dataclasses.replace`, which re-runs `Config.__post_init__`. A derivation that escapes its bounds **raises**; it does not clamp. That is deliberate — every formula above already clamps itself, and the validation is the backstop that says a formula was wrong.
- **`initialize_derivation` with `auto_parameters == 0`** writes `population` and `population_profiles` but no `derivation` key. Every `derive_*` also returns early in that case, so nothing ever calls `record` without a `derivation` to write into. Preserve that pairing; breaking one half raises a `KeyError` in the other.

## Reasoning worth preserving

The comments in these two files are short and each one is load-bearing. Quoted verbatim.

**`terrain_water.py`, on the steepest-descent re-point (the single most important comment in the file):**
> `# Select steepest descent on the spill surface where possible. Flood parents`
> `# route flats without cycles; each receiver precedes its child in order.`

Three claims packed into two lines: the gradient is measured on the *spill* surface not the ground; the flood tree is kept as the fallback specifically because it is the thing that routes flat water without cycles; and acyclicity is guaranteed by the pop-order precedence, which is what makes the single reverse sweep in the flow accumulation correct.

**`terrain_water.py`, on the river predicate:**
> `# Uniform unit runoff proxy, not rainfall volume. River width is not modelled.`

`routed_catchment` is drainage *area*, in m². It is not discharge. Climate later computes a rainfall-weighted sibling over the same receiver graph (`rain_runoff` / `rain_river`), and *that* is the one biomes and settlements should prefer.

**`terrain_water.py`, the two report strings — these are the honest-limitations statement the world document publishes:**
> `'Largest connected below-sea component is the ocean. Inland depressions are filled to their spill levels assuming sufficient water. Water extents do not use a rainfall budget or time-dependent filling; lake evaporation and river width are not modelled.'`
> `'Receivers index unique spherical nodes; -1 is an ocean outlet or the global minimum in an oceanless world. Catchment assumes uniform unit runoff.'`

**`terrain_recipes.py`, on the shared scale constant:**
> `# One shared calibration across seeds 0..31 at grid 65; never fitted per world.`

and its companion in the derivation note:
> `'Same seed recipe for every population. Shared scale targets roughly 20 km² land on average; no per-seed area fitting. Derived inputs supersede expert values.'`

The refusal to fit per seed is the design position: one calibration, thirty-two seeds, ~20 km² land on average, and individual worlds are allowed to be wetter or drier than that.

**`terrain_recipes.py`, on the settlement spacing ceiling:**
> `# The ceiling is the settlement_spacing config bound, not a world-size assumption.`
> `# At 1500 m a world wider than about 25 km had every city clamped into one region:`
> `# the formula wants 3.2 km at a 200 km circumference and 8.6 km at 1000 km.`

A measurement of a previous failure. The 10000 m ceiling is *not* a statement about how far apart cities should be; it is the config bound, and the earlier 1500 m value collapsed every city into one region on any world wider than 25 km.

**Related, from `terrain_lab.Config`, on why the `river_threshold_km2` bound is 10000 and not 100:**
> `# 10000, not 100: the default 0.15 km2 is the 11.15 km reference world's value and recipe 3 scales it as the square of the width, resolving 48.27, 193.08 and 434.44 km2 at the 200/400/600 km presets. At 100 the medium and large presets raised here instead of generating.`

**From `terrain_world.py`, on why `river_threshold_km2` had to be scaled — the failure this fixed:**
> `# river_threshold_km2 is an AREA, not a reach, so it takes the SQUARE of the same factor. Left absolute it stopped spanning anything: at the 200 km default every land cell drains more than 0.15 km2, so every land cell is a river, every land cell is its own freshwater source, freshwater_distance is 0 across all land and flood_risk saturates at 1. Three published layers had become constants.`

And the calibration sweep, which says where the usable band is:
> `# Re-derive rather than transcribe: sweeping the factor at 200 km gives 17/33/65 fractions of 91.5/63.8/33.1 at 100x, 78.7/53.7/21.8 at 220x, 68.1/47.8/16.0 at the 321.8x this resolves, 59.6/29.9/0.9 at 1000x and 2.1/0.0/0.0 at 10000x. The band that satisfies the target at every raster is roughly 100x to 1000x.`

The stated target: *a land-river fraction that is neither saturated nor empty at sizes 17, 33 and 65, and that does not drift with world width.* Any re-tuning of the recipe-0 `/110` divisor should be judged against the same target.

**From `terrain_world.py`, on why `derive_population` is not the answer on the shipping path:**
> `# Distances that decide how far a city reaches are authored against the reference world and have to grow with it. derive_population would have scaled them, but it is unreachable on recipe 3 (auto_parameters is forbidden there), so they are derived here instead. Left absolute, the rural layer disappears completely.`

**From `terrain_world.py`, on why the reach scaling does NOT clamp — the arm-swap bug:**
> `# Do NOT clamp, for the same reason river_threshold_km2 below does not. The old min(REACH_MAX_M, raw*factor) was the min(<absolute>,<relative>) arm swap this block exists to undo: past the width where the ceiling binds, the reach stops growing with the planet, silently, and cultural regions quietly stop scaling while everything around them keeps going. It was one preset from firing -- culture_link_cost resolves 96870.01 m at the 600 km large preset, 3.13% below the ceiling, so a world authored at 620 km would have hit it.`

This matters for the port because `derive_population`'s `min(10000, ...)` clamps are *exactly* that shape. They are benign only because the recipe-0 world is 11.53 km around; a wider auto-parameters world would hit them and silently stop scaling.

**From `terrain_scale.runoff_scale`, the measurement behind the square:**
> `Generating seed 42 size 17 at 200/400/600 km and taking the per-cell ratio gives 4.000000 at double the width and 9.000000 at triple, on all 47 land cells, against k**2 of 4 and 9.`

and the explicit non-claim:
> `NOT claimed: that the live 200 km world's runoff is 321.8x the archived 11.15 km reference world's. It is not -- measured per-cell growth between those two spans 13.2x to 764x -- because commit 21df9aa re-derived wavelength, tectonic_relief, amplitude and orogeny along with the radius. Those are two different planets, not one planet at two widths, and the comparison does not test this factor.`

**From `terrain_erosion.sphere_grid`, on why the grid is cached — a real measurement:**
> `# Pure in (n, radius) and rebuilt from scratch by sixteen call sites, roughly forty times per world: at grid 129 that is forty passes over sixteen thousand nodes computing an arc length per neighbour. No caller mutates the returned lists, so the grid is shared.`

`add_water` and `derive_institutions` are two of those sixteen call sites.

**From `terrain_history.capture`, on why `add_water` is re-run rather than patched:** the second tectonic epoch and the relic cut both rewrite `height`, and both immediately call `measure_surface` then `add_water(result, replace(cfg, phase=5))`. The hydrology is never incrementally updated — it is recomputed from the new surface, whole. The relic comment says why it matters:
> `# Refilled cuts are historical features; only still-dry abandoned beds are current gorges/valleys.`

i.e. the second routing pass is what decides whether an abandoned river bed is a dry gorge or has been reflooded.

## Notes for the C++ implementation

**`include/fwg/water.hpp` already exists and already declares the right shapes** — `WaterRouting {ocean, level, parent, flow, order}`, `WaterReport`, `enum WaterType {kDry=0, kOcean=1, kLake=2}`, `kLakeDepthEpsilon = 1e-7`, and `route_water(const std::vector<double>& height, const SphereGrid& grid, double sea_level)`. There is no `src/water.cpp`. The header's doc comment already paraphrases the method and routing strings correctly. Implement to that interface; do not redesign it.

**Direct mappings**

- `points, areas, neighbors = sphere_grid(n, radius)` → `fwg::sphere_grid(n, radius)` returning `shared_ptr<const SphereGrid>`; `points[i]` is `(grid.point_x[i], grid.point_z[i])`, `areas[i]` is `grid.areas[i]`, `neighbors[i]` is `grid.neighbours[i]` with `.index` and `.distance`. The header already documents that `neighbours` is sorted ascending by index and that a pole node is adjacent to its whole ring and carries the ring's area.
- The `grid(values)` scatter helper → `SphereGrid::to_grid(values)`, which the header says already "closes the seam and the poles". **Verify** it does exactly `out[0] = values[0]` across the whole row, `out[n-1] = values.back()` across the whole row, and `row[n-1] = row[0]` — that is the Python behaviour and anything else transposes or corrupts the poles.
- `h = [layers['height'][z][x] for x, z in points]` → gather `height.at(grid.point_x[i], grid.point_z[i])` into a `std::vector<double>`. Note `Grid::at(x, z)`, so the Python index swap disappears; do not re-introduce it.
- `child_seed(seed, 'recipe-v1-geology')` → for an RNG stream use `Rng::child(seed, "recipe-v1-geology")`, not `child_seed`. `child_seed` is the 32-bit *noise field* derivation; `Rng::child` goes through `child_seed64`. Three domains: `"recipe-v1-geology"`, `"recipe-v1-climate"`, `"recipe-v1-magic"`. No fourth derivation is needed.

**Python idioms with no direct equivalent**

1. **`heapq` of tuples → `std::priority_queue<std::pair<double,int>, std::vector<...>, std::greater<>>`.** `std::pair` compares lexicographically, so this reproduces the `(level, index)` tie-break exactly. Do *not* use a comparator that only looks at the level: the index tie-break is load-bearing (the entire ocean ties at exactly `sea`).

2. **`max(lower)` over `(slope, j)` tuples picks the largest index on a slope tie.** `std::max_element` returns the *first* maximum, which is the wrong index. Write it as an explicit scan:
   ```cpp
   double best_slope = -1.0; int best_j = -1;
   for (const auto& nb : grid.neighbours[i]) {
       if (nb.distance <= 0.0) continue;
       if (level[nb.index] >= level[i]) continue;
       if (rank[nb.index] >= rank[i]) continue;
       const double s = (level[i] - level[nb.index]) / nb.distance;
       if (s > best_slope || (s == best_slope && nb.index > best_j)) { best_slope = s; best_j = nb.index; }
   }
   if (best_j >= 0) parent[i] = best_j;
   ```

3. **`max(components, key=area_sum)` picks the first maximum** — `std::max_element` matches, provided components are generated in ascending-smallest-index order.

4. **`visited`, `remaining`, `root_set` as Python sets.** Use `std::vector<char>` flags indexed by node, not `std::unordered_set` — same semantics, no hashing, and no iteration-order exposure. For `remaining`, keep a `std::vector<char> pending` plus a monotone cursor so "`min(remaining)`" is O(1) amortised instead of O(N) per component.

5. **`rank = {i: k for k, i in enumerate(order)}`** → `std::vector<int> rank(N); for (k) rank[order[k]] = k;`.

6. **`flow = areas[:]`** is a copy, and `level = h[:]` is a copy. Both must be copies; `areas` belongs to the shared immutable `SphereGrid`.

7. **`result['warnings'].append(method)`** → `world.warnings.push_back(...)`. Remember the Python appends the same string once per `add_water` call, so a history world carries three copies. Decide whether to reproduce that or deduplicate, and say which in the code.

8. **`dataclasses.replace(cfg, **changes)` re-validates.** In C++ that is: mutate the `Config` fields, then call `cfg.validate()` (which already throws `std::invalid_argument` naming the field and its bounds). Preserve the "raise, do not clamp" behaviour.

9. **`asdict(cfg)` field ordering** in the derivation document. `std::map<std::string, ...>` sorts alphabetically and would change the emitted order. If the derivation document is ported, use an ordered vector of `(name, value, source)` in the declared field order.

10. **Integer division `residents // 100`** is floor division on non-negative ints, so plain `/` on `int` matches (both operands are non-negative here). `math.floor` on a positive double → `static_cast<int>(std::floor(x))`. `math.ceil(rural/40)` — note `rural/40` is *true* division in Python 3, so it is `std::ceil(static_cast<double>(rural)/40.0)`.

**Where it plugs into the existing library**

- Water is not one of the sixteen numbered stages. `docs/porting-status.md` already records it as "Also outside the ladder and not yet ported: water routing (`terrain_water`), which the reference runs as part of world scaling rather than as a numbered stage." It belongs at the end of stage 5, after `land_area` and the metre scaling, reading `world.effective.globe_radius` and `world.effective.sea_level`.
- New layers go into `World::layers` as `Grid`: `"water_type"`, `"water_depth"`, `"water_surface"`, `"routed_catchment"`, `"river"`. `water_type` and `river` are enum/boolean values stored as doubles; that is consistent with the rest of the library (`land` is already stored that way).
- `WaterReport` needs a home on `World` — a `bool has_water; WaterReport water;` pair alongside `has_sediment_budget`/`sediment`, and a serializer entry. The `nodes` list from the Python block does not need storing: `SphereGrid::point_x/point_z` already is it, and it is reconstructible from `(n, radius)`.
- Timing: `world.timing_ms["water"]`, subject to the existing decision in `determinism.md` §4 that timings are not in the document.

**The recipe module is a different matter.** FWGC++ has no `auto_parameters` path at all: `Config`/`Request` implement the *recipe-3* derivation (circumference → `shape_overrides` → reach/runoff scaling), which is the shipping path and which Python computes in `terrain_world`/`terrain_scale`, not here. Of this assignment's four derivation functions:

- `derive_water` has a recipe-3 counterpart already ported (`river_threshold_km2 * runoff_scale(circumference)` in `src/config.cpp:180-191`). The recipe-0 `land_km2/110` form is a *different* formula for the same field, and it needs the world's land area, which is not known at `resolve()` time — it would have to run as a post-stage-5 step.
- `derive_population` is explicitly dead on recipe 3, and its outputs (`settlement_spacing`, `support_reach`, `culture_link_cost`) are already in `fwg::Config` under the comment "carried for stages not yet ported", filled by the reach scaling instead.
- `derive_institutions` has no C++ counterpart; `hamlets_per_core`, `fortress_count` and `college_count` are not in `fwg::Config` at all yet. It also cannot run until stages 10-13 exist (it needs settlements, roads and the magic field).
- `seed_config` is the recipe-0 request builder. `fwg::Request`/`resolve` is the recipe-3 one. They overlap in `belt_width_for` and `detail_wavelength` (both already in `scale.hpp`) and disagree in the amplitude ratio (1.5 vs 1.375) and the world scale (0.1834617 vs 0.17744124).

**Cost.** `route_water` is O(N log N) with N up to 1 047 554 at size 1025. The flood fill and the two linear sweeps are O(N·8). No allocation inside the hot loop if `parent`, `level`, `rank`, `order` and `flow` are all pre-sized. Expect it to be comfortably the cheapest of the big stages.

**Testing.** The invariants worth asserting directly, all cheap: `order.size() == N`; `rank[parent[i]] < rank[i]` for every `i` with `parent[i] >= 0`; `level[i] >= height[i]` for every `i`; `ocean_km2 + lake_km2 + dry_km2 == area.total_km2` to a float tolerance; every ocean node has `parent == -1`; `flow[i] >= areas[i]`; the sum of `flow` over outlet nodes equals the total sphere area (every cell's area reaches exactly one root).

## Open questions

1. **Should the recipe-0 / `auto_parameters` path be ported at all?** `Config.__post_init__` raises on `world_recipe == 3 and auto_parameters`, so everything in `terrain_recipes` is reachable only on `world_recipe 0` — the "geometry experiment" path. FWGC++ has implemented the recipe-3 derivation only. Porting `seed_config` means carrying a *second*, subtly different parameter recipe (amplitude ratio 1.5 vs 1.375, world scale 0.1834617044641644 vs 0.17744123532462844, a `world_size` multiplier on the design radius rather than a circumference in metres). My read is that `route_water`/`add_water` must be ported and the four `derive_*` functions are optional, but that is an owner call, not mine.

2. **Two reference worlds.** `RECIPE_WORLD_SCALE = 0.1834617044641644` (recipe 0, 11.53 km circumference) and `Config.world_scale = 0.17744123532462844` / `terrain_scale.REFERENCE_RADIUS_M = 1774.4123532462844` (recipe 3, 11.15 km) are different constants for the same concept, and comments across the codebase say "the 11.15 km reference world" without qualifying which. Everything tuned against one is mistuned against the other by ~3.4% in length, ~7% in area. Is this intentional (a genuinely different experimental planet) or drift?

3. **`derive_population`'s `min(10000, ...)` clamps are the exact defect shape that `terrain_world` was rewritten to remove.** That comment says a `min(<absolute>, <relative>)` arm swap makes a reach silently stop growing with the planet, and that recipe 3 now *raises* instead of clamping. `derive_population` still clamps. On an 11.53 km world it never binds, so nothing moves today; on any wider auto-parameters world it would. Port the clamps as-is, or port them as raises to match recipe 3?

4. **`routed_catchment` has no consumer.** Nothing in the 140-module Python reads it; it exists for the viewer. It is also the layer most likely to be misread as discharge when the real discharge proxy is climate's `rain_runoff`. Keep it as a published layer, drop it, or keep it and rename it?

5. **The method warning is appended three times on a history world.** `add_water` runs at stage 5, again inside `tectonic_transition` (stage 6) and again inside `carve_relics` (stage 7), and each call does `warnings.append(method)`. Almost certainly unintended; harmless. Reproduce or deduplicate?

6. **`fortress_count`'s formula ceiling is 24 while the `Config` bound is 0..1024**, and the dataclass default sits at 1024 with a comment saying the default is a ceiling that must never bind. So on an auto-parameters world the fortress count is hard-capped at 24 regardless of how much road there is, while on recipe 3 it is effectively uncapped. Deliberate asymmetry or a stale number?

7. **The ocean tie-break is untested.** `max(components, key=area_sum)` resolving an exact float tie by lowest smallest-node-index is a real behaviour with no test anywhere. The sphere grid is latitudinally symmetric, so a north/south mirrored pair of basins could produce one. Nothing verifies which side wins, and nothing verifies that it is stable across a recompile. Worth a targeted test in C++ since the outcome decides which half of the world is sea.

8. **Nothing verifies that the two `river` layers agree.** `add_water` publishes `river` from uniform-area catchment and `add_climate` publishes `rain_river` from rainfall-weighted runoff, both against the *same* `river_threshold_km2` although the two quantities have different magnitudes. Downstream modules read both. There is no record of whether the shared threshold is intentional or whether `rain_river` should have its own.

## Addenda from the audit

The reader above wrote the specification; a second reader went back to the source
to find what it had left out or got wrong. Both are kept: the omissions are
content the implementer needs, and the corrections say where the specification
above is not to be trusted.

### Corrections

- **Claimed:** `settlement_spacing` (float m, 150..10000), `support_reach` (float m, 100..10000) ... 'Bounds 150 .. 10000 metres on spacing' ... 'Bounds 100 .. 10000 metres' [support_reach]
  **Actually:** Those are the formula's own clamps inside derive_population, not the Config bounds. terrain_lab.Config.__post_init__ validates `('support_reach',100,100000)`, `('culture_link_cost',1,100000)` and `('settlement_spacing',10,100000)`. This is not cosmetic: the spec instructs the porter to preserve 'raise, do not clamp' by calling cfg.validate() after dataclasses.replace, and the spec itself quotes terrain_world saying culture_link_cost resolves 96870.01 m at the 600 km large preset — with the spec's stated bounds that value, and the recipe-3 support_reach that produces it, would throw. Correct bounds: settlement_spacing 10..100000 m, support_reach 100..100000 m, culture_link_cost 1..100000 m. Corollary: the source comment 'The ceiling is the settlement_spacing config bound, not a world-size assumption' is itself stale — the config bound is 100000, so the 10000 in the formula is no longer the bound it claims to be. Carry the comment, but record that it no longer matches terrain_lab.

- **Claimed:** Draw order is the source order of the `Config(...)` keyword arguments — Python evaluates them left to right. Getting this order wrong changes every world.
  **Actually:** Two of the nine draws happen before the Config call: line 15 is `count=rng.randint(9,15);relief=rng.uniform(650,850)`, and the weather and magic Random objects are constructed on lines 16-17, after them. Only crust_bias, mountain_detail and the weather/magic draws are keyword arguments. The resulting per-stream order is identical to what the spec lists (geology: randint(9,15), uniform(650,850), uniform(-.08,.08), uniform(.55,.8)), so no value changes — but the stated rule is not what the code does, and a porter who implements 'draw in keyword order' from a differently-ordered Config constructor would get it wrong.

- **Claimed:** `neighbors`: per-node list of (neighbour_index, great_circle_distance_m) ... 8 neighbours for interior nodes; a pole node has n-1 neighbours
  **Actually:** Interior nodes on the two pole-adjacent rings (z==1 and z==n-2) have 6 neighbours, not 8, because the 3x3 stencil's three entries at z==0 (or z==n-1) all map to the single pole node and the neighbour set dedupes them. Verified at n=9: 42 nodes with 8 neighbours, 16 with 6, and each pole with n-1==8. At n=3 the two equatorial nodes have 3 neighbours each.

- **Claimed:** `Config.__post_init__` raises on `world_recipe == 3 and auto_parameters`, so everything in `terrain_recipes` is reachable only on `world_recipe 0`
  **Actually:** The guard is `if self.world_recipe and self.auto_parameters: raise ValueError('World recipe 3 uses explicit defaults and overrides; auto_parameters requires world_recipe 0')` — it fires for ANY truthy world_recipe, not specifically 3. The conclusion (terrain_recipes is reachable only at world_recipe 0) is right; the stated condition is narrower than the code.

### Missing — important

**Consumers of `water['river_segments']`, `water['nodes']` and the three area sums (the spec's consumer list names none of these)**

`city_planner.py:71-72` and `castle_planner.py:68-70` both do `nodes = world.get('water',{}).get('nodes',[])` then `for i,j in world.get('climate', world.get('water',{})).get('river_segments',[])` — the local plan builders draw river polylines from climate's segments when climate exists and FALL BACK to `water['river_segments']` when it does not, projecting each node through `water['nodes'][i]` into the local plan frame. `hamlet_planner.py:51` also reads `world['water']['nodes']`. `terrain_religion.py:140-141,155` computes `ocean = water.get('ocean_km2',0.); total = ocean + water.get('lake_km2',0.) + water.get('dry_km2',0.)` and publishes `'ocean_share': ocean/total if total else 0.` So dry_km2 is NOT only read by derive_population, and ocean_km2/lake_km2/river_segments are not viewer-only: a port that drops any of them breaks four modules.

**The two unit-test fixtures that pin route_water's semantics (the spec's Testing section gives invariants only)**

From tests/test_terrain_water.py, both are tiny hand-built graphs that a C++ port can use verbatim. (1) Inland basin fills to its sill, not to sea level: h=[-2,-1,5,-3,7], areas=[2,2,1,1,1], a line graph with unit distances neighbors=[[(1,1)],[(0,1),(2,1)],[(1,1),(3,1)],[(2,1),(4,1)],[(3,1)]], sea=0 — expect ocean==[True,True,False,False,False], level[3]==5 (node 3 at h=-3 is reached across the h=5 sill at node 2 and takes 5, NOT the sea level 0), parent[3]==2, sum of flow over nodes with parent<0 == sum(areas), and following parent from every node terminates without revisiting a node (explicit acyclicity check). (2) Dry world invents no ocean: h=[3,1,2], areas=[1,1,1], same line topology, sea=0 — expect any(ocean)==False, parent[1]==-1 (the global minimum is the sole root), flow[1]==3 (the whole world accumulates into it). The generated-world test additionally asserts, for each of water_type/water_depth/water_surface/routed_catchment, that `row[0]==row[-1]` on every row (seam closed) and `len(set(row_0))==1` (pole row constant), and that ocean_km2+lake_km2+dry_km2 == area['total_km2'].

### Missing — minor

**`water_depth` has a functional consumer, not just a snapshot: it sizes relic valley cuts in stage 7**

`terrain_history.carve_relics` computes, per sphere-grid node, `cut = min(45., max(4., cfg.tectonic_relief*cfg.world_scale*0.12))` where the abandoned-waterway mask says 'gorge', else `min(100., max(12., old['water_depth'][z][x]*0.6))` where it says 'valley', else 0 — i.e. a former lake/river bed is incised to 60% of its OLD water depth, clamped to 12..100 m. Then a shoulder pass: `cuts[i] = max(cuts[i], 0.25 * max(cuts[j] for neighbours j))`, with the comment 'Small shoulders make former river beds visible as terrain, not just marker pixels.' The cut is subtracted from `height` and `add_water` is re-run over the new surface. So the depth layer from the PREVIOUS add_water call is load-bearing input to the third one.

**Exact neighbour counts on the sphere graph, including the pole-adjacent rows**

sphere_grid builds neighbours as a SET before sorting: for an interior node `ids = {index(x+dx, z+dz) for dx,dz in (-1,0,1)^2} - {i}`, and `index(x,z)` maps every x at z==0 or z==n-1 onto the single pole node. So the three stencil entries on a pole row collapse to one and rows z==1 and z==n-2 have exactly 6 neighbours, every other interior node has 8, and each pole has n-1. Measured at n=9: 42 nodes with 8, 16 nodes (the two pole-adjacent rings) with 6, poles with 8 (=n-1). At n=3 the two equatorial nodes have 3 neighbours each (both poles plus each other). Neighbour lists are `sorted(ids)`, ascending by node index, and the distance is `radius*acos(clamp(dot(unit_i,unit_j),-1,1))`.

**Which dict accesses in derive_institutions raise rather than default**

`rural = max((s.get('rural_population_estimate',0) for s in sites), default=0)` and `residents = sum(s.get('urban_population_estimate',0) for s in sites)` both default missing site fields to 0 — but the road fields are plain indexing: `sum(road['length_m'] for road in routes)` and `{tuple(sorted(edge)) for road in routes for edge in road['river_crossings']}`. A route dict missing `length_m` or `river_crossings` raises KeyError; a site missing its population estimates does not. The spec says '(both ints; absent on some sites, defaulted to 0)' for the sites but is silent on the asymmetry.

**seed_config's 'request' is the user's own Config, and the call guard**

`terrain_lab.generate_base` opens with `if cfg.auto_parameters and not cfg.world_recipe: from .terrain_recipes import seed_config; cfg = seed_config(cfg)`. The 'request' parameter IS a Config: only seed, size, phase, population_profile and world_size survive; every expert field the caller set (plate_count, ley_nodes, settlement_count, ...) is discarded, which is what test_terrain_recipes.test_recipe_is_reproducible_and_ignores_expert_values pins. Also `radius = 10000*{'small':1,'medium':2,'large':3}[request.world_size]` is a bare dict index executed before any validation, so an unknown world_size raises KeyError, not ValueError.
