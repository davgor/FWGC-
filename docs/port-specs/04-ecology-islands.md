# Port specification: 04-ecology-islands

Extracted from the Python `FantasyWorldGenerator` at commit `b67c99b` before that
repository was retired, then audited against the same source by a second reader.

**Source modules**

- `Sim/icarus_sim/terrain_ecology.py`

**Estimated C++ size:** roughly 780 lines, excluding tests.

## Audit: IMPLEMENTABLE

This is an unusually faithful spec: I walked all 189 source lines against it and every formula, constant, gate, loop bound, tie-break, RNG stream, draw count, seam/pole rule and published string in `terrain_ecology.py` is present and correct in the algorithm sections. I independently confirmed the supporting claims it makes about other files — ZONES order and the three-option-per-zone table, the ten Ocean-islands option specs and the cross-field weight rule, `node_grid`'s pole/seam fill, `sphere_grid`'s `sorted(ids)` neighbour lists and `(n-1)`-wedge pole areas, `allocate_access`'s heap key and relaxation, `child_seed`'s SHA-256 derivation, `terrain_area.apply_world_scale`'s ordering (lines 55-75 really are inside that function), `terrain_history` stages 8/9, `terrain_society`'s `'habitats' not in result` gate and its `witch-hut-<i>` re-minting, the 13-row `biomes.json` catalogue, the `test_layer_scale_regression` counts 1/47, 42/268, 14/48, 36/271, and the `key_locations/core/fields.py` whaling-station quote. All checked out. A competent C++ programmer could build this subsystem from the spec alone and would get the right thing. What it gets wrong is arithmetic ON TOP of correct formulas, plus one false statement about the node graph's neighbour structure; the formulas themselves are right, so the port would be correct even where the commentary is not — which is why I am answering true. But three of the errors are the kind that will mislead whoever later validates the C++ against this document, and they are unrecoverable once the Python is gone: the volcanic island's above-water radius is understated by 29%, the atoll's lagoon is stated 13% too wide and its outer shore 6% too narrow, and the "8 neighbours for an interior node" claim is false on the two rows adjacent to each pole, where the three cells in the pole row collapse to a single node and the count is 6 — which directly changes the `exposure`/`adj` denominators there. The most consequential omission is that `add_environment` does not run only at stages 8 and 9: `terrain_history.refresh_environment` is invoked on every age advance, so the whole subsystem is re-executed per age; a porter following this spec would wire it twice and silently lose a system that the `STATE_KEYS` carry-forward would make look correct.

## Purpose

Two subsystems that share one Python file, `Sim/icarus_sim/terrain_ecology.py` (189 lines), and that run at opposite ends of the pipeline.

`island_layout` / `shape_islands` scatter ocean archipelagos: small clusters of volcanic cones, atolls, continental fragments and cold skerries pushed up out of deep ocean floor *before* surface noise, erosion and drainage run, so that the islands are eroded and drained like any other land rather than pasted on top of a finished world. The docstring is explicit about the ordering intent: "Compact ocean uplifts before erosion and drainage; retain continental foundations." The "retain continental foundations" half is the `max()` accumulation — an island never lowers ground that is already there.

`add_environment` is the ecology pass. It turns the finished physical world (water type, depth, temperature, moisture, slope, rivers) into eighteen published scalar habitat layers (coastal, marine, mineral and cold-climate proxies), a twelve-month climatological calendar (temperature, snow cover, sea/lake ice), a land-component island flag, and thirteen overlapping "regional character" influence fields (demonic, draconic, pirate, steampunk, witch huts, red sands, dead sea, starlight lakes, haunted sands, enchanted, fungal, crystal, haunted marsh) with their manifested centres and two kinds of landmark. It also overwrites the biome layer on cold ground with boreal/cold-tundra/land-ice labels. The module header states the honesty contract that the published `habitats` and `regions` blocks repeat: "Artistic layered habitats on the spherical grid; quantities are explicit proxies" — no tides, no currents, no biological reef simulation, no ocean circulation.

## Where it sits in the pipeline

**shape_islands** — inside stage 1-5 terrain generation, called from `generate_tectonics` (terrain_tectonics.py:174-175) at the point:

```
layers['continental'], layers['structure'], layers['height'], layers['base'] written   (phase>=2)
--> if cfg.world_recipe and cfg.phase>=2:  archipelagos = shape_islands(layers, cfg)
    then surface noise added   (phase>=3, height := structure + noise)
    then erode()               (phase>=4)
    then measure_globe()       (slope, tpi)
```
Prerequisites: `structure`, `height`, `base`, `continental` layers and `cfg.sea_level`, `cfg.tectonic_relief`, `cfg.size` — all in **design space**, pre-`world_scale`. Returns the cluster list, which `generate_tectonics` publishes as `result['ocean_archipelagos']`.

Consequence of the position: at phase>=3 the `height` row is *recomputed* as `structure + noise`, and at phase>=4 `base` is replaced by a copy of `surface`. So the direct `height`/`base` increments only survive at phase 2 and 3; the increment to `structure` is the one that always reaches the finished world. The C++ port must still write all three, because a phase-2 or phase-3 stop is a legal request.

In C++ this is a new function called from `generate_tectonics` after the structure/height/base fill and before the noise loop. It belongs in an `ecology` translation unit, not in tectonics.

**add_environment** — stage 8 and again at stage 9, and it is a *gate* for later stages.

Ordinary path, `terrain_area.apply_world_scale` (terrain_area.py:62-70), after `world_scale` has already been applied to every layer:
```
add_water -> add_climate -> add_terrain_labels
  -> if cfg.phase>=6 and result['climate']:
       generate_networks(result,cfg)     # writes ley_* layers
       add_environment(result,cfg)
       add_biome_variants(result,cfg)
  -> add_settlements, add_humans, add_seasonal_food, add_world_society, add_nests
```
History path, `terrain_history.generate_history`:
- stage 8: `add_climate; add_terrain_labels; add_environment(result, replace(cfg, phase=6))` — **the ley layers do not exist yet**, so every magic-driven zone score is identically zero at this stage.
- stage 9: `generate_networks(...)` then `refresh_environment(...)` (terrain_history.py:509-517), which pops `magic_density`, re-runs `add_terrain_labels` at phase 6, restores `magic_density`, re-runs `add_environment` at phase 6, and re-copies `biome` into `natural_biome`. This second run is the one whose zone fields are real.

Guard: `if cfg.phase < 6 or not result.get('climate'): return` — returns with no side effect at all, writing no layer and no block.

Prerequisites for add_environment: layers `water_type`, `water_depth`, `temperature`, `moisture`, `slope`, `rain_river`, `height`, `volcanic`, `biome`; blocks `climate`, `effective_config` (for `globe_radius`), and `result['spacing_m']` — all **post-`world_scale`, in metres**. Optionally layers `ley_weave`, `ley_umbral`, `ley_infernal`, `ley_holy`, `ley_primordial`.

Downstream dependents (functional parity matters here):
- `terrain_society.add_world_society` returns immediately if `'habitats' not in result`. Writing the `habitats` block is therefore the switch for the entire society stage.
- `terrain_settlements` reads `metal_richness` (resource potential), `coastal_support` (settlement scoring), `fishing_productivity` (marine food), `salinity` (a lake is freshwater only below 0.2).
- `terrain_society` reads `harbor_suitability`, `fishing_productivity`, `coastal_support`, `island_habitat` (the `islander` community trait), `zone_witch_huts` (it deletes and re-places every `witch_hut` landmark), `zone_pirate`, and `seasonal_environment.months[m].water_ice` (ice-blocked fishing and sea routes).
- `terrain_nests` reads `maritime` as `coast` and `salinity` to pick `marine`/`freshwater`/`salt_lake`/`land` medium.
- `terrain_civilizations` reads `maritime`.
- `terrain_nomads` reads `seasonal_environment.months`.
- `key_locations` archetypes gate on `coastal_exposure`, `harbor_suitability`, `ice_cap`, `island_habitat`, `metal_richness`, `reef`, `rocky_coast`, and derive `coastal_fishing_productivity` from `fishing_productivity`; `key_locations/core/world.py` reads `regions.landmarks`.
- `terrain_history.STATE_KEYS` carries `habitats`, `regions`, `seasonal_environment`, `ocean_archipelagos` across age transitions.

## Inputs

**shape_islands(layers, cfg)** — all design space, pre-`world_scale`:
- `layers['continental'][z][x]` — double, metres. `cfg.tectonic_relief * continental_profile(crust)`; range about `-1.8*relief` (deep ocean floor) to `+0.3*relief` (continental platform). Read at exactly one cell per cluster.
- `layers['structure'][z][x]` — double, metres. The tectonic surface before noise. Read at every cell; this is `original`.
- `layers['height'][z][x]`, `layers['base'][z][x]` — double, metres. Written only.
- `cfg.size` (n, 3..1025), `cfg.sea_level` (metres, design space, default 0), `cfg.tectonic_relief` (metres, design space; 1667 physical / `world_scale` 0.17744 ≈ 9394 design at the 200 km preset — note the effective value printed by a built world is 1667.0), `cfg.seed` (uint32).
- `options(cfg)` — the Ocean-islands group, see **constants**.

**add_environment(result, cfg)** — all post-`world_scale`, metres:
- `cfg.phase`, `cfg.size`, `cfg.seed`; `result['effective_config']['globe_radius']` (metres, e.g. 31830.99 at the 200 km preset); `result['spacing_m']` (metres, `2*pi*r/(n-1)`, e.g. 12500.0 at 200 km size 17).
- `layers['water_type']` — int 0 dry / 1 ocean / 2 lake. **The only connected-ocean body is 1**; every other below-sea component is 2.
- `layers['water_depth']` — double, metres, 0 on dry land, `spill level - ground` on water.
- `layers['temperature']` — double, degrees Celsius, the annual mean from `add_terrain_labels`: `28 - 45*sin(lat)^2 - 0.0065*max(0, h-sea) + temperature_offset`. Typical range about -40..+30.
- `layers['moisture']` — double, 0..1 (`climate_moisture`, already clamped).
- `layers['slope']` — double, **degrees** (`measure_globe` returns `degrees(atan(hypot(gx,gy)))`), 0..90.
- `layers['rain_river']` — int 0/1, a land cell carrying a river.
- `layers['height']` — double, metres, absolute (NOT above sea level).
- `layers['volcanic']` — double, roughly 0..1, the subduction volcanic term from tectonics.
- `layers['biome']` — int, natural-catalogue id. Read/modified in place.
- Optional `layers['ley_weave']`, `ley_umbral`, `ley_infernal`, `ley_holy`, `ley_primordial` — double, 0..`<school>_strength` (<=2). `ley_holy` is an alias of `ley_radiant`; `ley_primordial` is the per-node maximum of `ley_fire`, `ley_water`, `ley_earth`, `ley_air`. **Both are compatibility projections written by `generate_networks`, not networks of their own — `holy` and `primordial` are not in `NETWORKS`.** Absent -> 0.
- `options(cfg)` — `metal_abundance`, `salinity`, `seasonality`, `ice_accumulation`, `sea_draft`, and per zone `<name>_occurrence`, `<name>_extent`, `<name>_intensity`.

## Outputs

**shape_islands**

Layer `archipelago_relief` — new n x n double grid, metres, design space (`terrain_area.apply_world_scale` multiplies it by `world_scale` with the other height-like layers). **Always >= 0.** Seam and poles sealed.

In-place increments of the same non-negative delta into `structure`, `height`, `base` (metres, design space).

Return value, published as `result['ocean_archipelagos']` — a list of cluster dicts. **Key field is `id`, a string `"ocean-<k>"` where k is the CANDIDATE index 0..archipelago_count-1, so ids have gaps** (a cluster that failed the occurrence roll is never in the list and its number is skipped). Fields:
- `id`: string.
- `kind`: one of `"volcanic"`, `"atoll"`, `"continental"`, `"cold"`. (Disambiguation: this `kind` is the island character; it is unrelated to the `kind` field on regions landmarks, settlements or nests.)
- `direction`: 3-tuple unit vector, the cluster centre.
- `islands`: list of `{'direction': unit 3-vector, 'angular_radius': double radians}`, length `islands_per_cluster`, island 0 always sitting exactly on the cluster centre.
- `eligible`: bool, added by `shape_islands` after `island_layout` returns.

**add_environment**

Eighteen new double layers, each `node_grid`-sealed (seam column n-1 = column 0; row 0 and row n-1 are one repeated pole value). Order of creation is fixed and load-bearing (see **ordering_hazards**):
| layer | domain | range | meaning |
|---|---|---|---|
| `metal_richness` | all cells | 0..1 | ore proxy; measured 0.147..0.741 at seed 42 size 17 |
| `salinity` | water | 0..1 | 1.0 on ocean, arid-lake salinization on lakes, 0 on land |
| `coastal_exposure` | all | 0..1 | fraction of neighbours that are water |
| `harbor_suitability` | land only | 0..1 | 0 on every water cell |
| `fishing_productivity` | water only | 0 or 0.12..1 | **identically 0.0 on every land cell at every raster measured** (seed 42 at 17/33/65/129/257) |
| `reef` | ocean only | 0..1 | |
| `lagoon` | ocean only | 0..1 | |
| `estuary` | ocean only | 0 or 1 | |
| `sheltered_bay` | land only | 0..1 | **bit-identical to `harbor_suitability`** — the same expression is stored twice |
| `rocky_coast` | coastal land | 0..1 | |
| `kelp` | ocean only | 0..1 | |
| `fjord` | ocean only | 0..1 | |
| `open_ocean` | ocean only | 0..1 | `1 - shallow` |
| `maritime` | land only | 0..1 | `exp(-ocean_distance_m/300)`; see **edge_cases**, it collapses at coarse rasters |
| `boreal` | land | 0 or 1 | |
| `tundra` | land | 0 or 1 | |
| `ice_cap` | land | 0 or 1 | |
| `coastal_support` | land only | 0..1 | 0 on water |
| `island_habitat` | land only | 0 or 1 | 1 if the cell's connected land component is under 15% of world land area |
| `zone_<name>` x13 | varies | 0..intensity | one per `ZONES` entry |

In-place mutation of `layers['biome']`: on any **land** node whose monthly temperature profile is cold, the natural-catalogue id is replaced by 15 (`boreal_forest`), 16 (`cold_tundra`) or 17 (`land_ice`). The seam and pole rows are then resealed, and `layers['natural_biome']` is rewritten as a row-wise copy of `biome`.

Block `result['seasonal_environment']`:
```
{'months': [ {'temperature': Grid, 'snow': Grid, 'water_ice': Grid} ] * 12,
 'method': 'Monthly latitude/elevation temperature proxy; snow and water ice do not modify bedrock.'}
```
`temperature` in Celsius (unbounded), `snow` 0..1 land only, `water_ice` 0..1 water only. Month index 0..11; index 0 is the cold month in the northern hemisphere.

Block `result['regions']`:
```
{'influences': [ per zone, in ZONES order ], 'landmarks': [...],
 'method': 'Independent overlapping fields; low suitability can be valuable to specialist sites.'}
```
Each influence: **key field `id` = the zone name string** (`"demonic"`, `"witch_huts"`, ...), plus `centers` (list of 0..3 **node indices**, not grid coords), `maximum` (double), `area_km2` (double, cells whose value exceeds 0.05, summed by spherical area / 1e6), `reason` (one of `"manifested"`, `"insufficient suitable habitat"`, `"not selected by occurrence seed"`).

Each landmark: **key field `id` = `"<zone>-<node index>"`** (e.g. `"witch_huts-38"`), plus `kind` (`"witch_hut"` for the `witch_huts` zone, `"necropolis"` for `haunted_sands`), `x`, `z` (grid coordinates), `layer` (`"surface"`), `intensity` (double). Only those two zones produce landmarks.

Block `result['habitats']`:
```
{'version': 1,
 'aquatic': ['reef','lagoon','estuary','sheltered_bay','rocky_coast','kelp','fjord','open_ocean'],
 'method': 'Depth, coast exposure, salinity and climate proxies; no tides, currents or biological reef simulation.'}
```
Note `sheltered_bay` and `rocky_coast` are listed as aquatic but are computed only on land.

## Algorithm

## PART A -- island_layout(cfg) -> clusters

Helpers used throughout:
```
clamp(v)        = max(0, min(1, v))
dot(a,b)        = sum(a_i*b_i)
angle(a,b)      = acos(clamp_to_[-1,1](dot(a,b)))          # radians
random_point(r): y   = r.uniform(-1, 1)                    # 1 draw
                 lon = r.uniform(-pi, pi)                  # 1 draw
                 c   = sqrt(1 - y*y)
                 return (c*cos(lon), y, c*sin(lon))        # uniform on the sphere
```

```
o    = options(cfg)
rng  = Random(child_seed(cfg.seed, 'ocean-archipelagos-v1'))
clusters = []
for k in 0 .. o.archipelago_count-1:                       # default 5
    center = random_point(rng)                             # 2 draws
    kind   = weighted_choice(rng,
               ('volcanic','atoll','continental','cold'),
               (o.volcanic_island_weight, o.atoll_weight,
                o.continental_island_weight, o.cold_island_weight))   # exactly 1 draw
    if kind == 'cold':                                     # NO draw
        center = (center.x*0.4, copysign(0.9, center.y), center.z*0.4)
        center = center / |center|
    if rng.random() > o.archipelago_occurrence:            # 1 draw, ALWAYS taken
        continue                                           # candidate k is dropped, id skipped
    islands = []
    for j in 0 .. o.islands_per_cluster-1:                 # default 5
        q     = random_point(rng)                          # 2 draws
        scale = o.island_radius * o.island_spacing * o.archipelago_extent * sqrt(j)
        p     = center + scale*q
        p     = p / |p|
        islands.append({direction: p,
                        angular_radius: o.island_radius * rng.uniform(0.7, 1.3)})  # 1 draw
    clusters.append({id: 'ocean-%d' % k, kind: kind, direction: center, islands: islands})
return clusters
```

`weighted_choice` is CPython's `random.choices(pop, weights=w)[0]`: build the cumulative sums of `w`, take `total = cum[-1]`, and return `pop[bisect_right(cum, rng.random()*total, 0, len(pop)-1)]` — **one** stream draw, and the search is clamped to index `len(pop)-1` so a float overshoot cannot run off the end.

Geometry notes that matter:
- `island_radius` is an **angle in radians** (its description is "island radius / planet radius"), default 0.035 rad. It is scale-free: multiply by the effective globe radius to get metres. At the 200 km preset that is 0.035 * 31830.99 = **1114 m**.
- `scale` grows as `sqrt(j)`, so island j sits `island_radius*island_spacing*archipelago_extent*sqrt(j)` radians from the centre along a **freshly drawn random direction** (`q` is a random point on the whole sphere, not a tangent vector). Adding a full 3-vector and renormalising projects it onto the sphere. Island 0 has `scale = 0` and lands exactly on the cluster centre.
- The `cold` branch pins the cluster to high latitude: after scaling x and z by 0.4 and forcing |y| = 0.9, the normalised latitude is `asin(0.9/sqrt(0.16*(1-cy^2)+0.81))`, i.e. between **66.1 degrees and the pole**, sign taken from the original y.

## PART B -- shape_islands(layers, cfg) -> clusters

```
clusters = island_layout(cfg)
n = cfg.size
layers['archipelago_relief'] = n x n zeros

# B1: eligibility -- one grid sample per cluster
for cluster in clusters:
    p = cluster.direction
    x = round((atan2(p.z, p.x) + pi) / (2*pi) * (n-1))
    z = round(acos(p.y) / pi * (n-1))
    cluster.eligible = layers['continental'][z][x] < cfg.sea_level
```
This is the "retain continental foundations" rule: a cluster whose centre falls on a cell whose *crust-profile* elevation is already above sea level is discarded whole. It samples `continental` (the pure hypsometric crust term), **not** `structure` and **not** `height`, so an oceanic cell that a collision belt has raised above water is still eligible, and a continental cell that a trench has pushed below water is still ineligible.

`round` here is Python's banker's rounding (half to even). `x` may come out as `n-1`, which indexes the duplicate seam column and is fine; `z` may come out as 0 or `n-1`, the pole rows.

```
# B2: shaping
for z in 0 .. n-1:
    for x in 0 .. (0 if z is a pole row else n-2):
        p        = direction(x, z, n)
        original = layers['structure'][z][x]
        value    = original
        for cluster in clusters:
            if not cluster.eligible: continue
            for island in cluster.islands:
                d = angle(p, island.direction) / island.angular_radius     # in island radii
                if d >= 2: continue
                support = clamp((2 - d) / 0.7)        # 1 for d<=1.3, linear 1->0 over 1.3..2
                if cluster.kind == 'atoll':
                    target = cfg.sea_level + cfg.tectonic_relief *
                             (0.22 * exp(-((d - 0.7)/0.24)^2) - 0.07)
                else:
                    peak   = 0.9 if cluster.kind == 'volcanic' else 0.45   # continental and cold
                    target = cfg.sea_level + cfg.tectonic_relief *
                             (peak * exp(-2*d*d) - 0.1)
                value = max(value, original + (target - original) * support)
        delta = value - original                      # >= 0 by construction
        layers['archipelago_relief'][z][x] = delta
        layers['structure'][z][x] += delta
        layers['height'][z][x]    += delta
        layers['base'][z][x]      += delta
    # seal the row immediately, inside the z loop
    for key in ('structure','height','base','archipelago_relief'):
        if z is a pole row: layers[key][z] = [layers[key][z][0]] * n
        else:              layers[key][z][n-1] = layers[key][z][0]
return clusters
```

The accumulation is `max` over islands, evaluated against the *unmodified* `original` each time, so overlapping islands take the deepest single uplift rather than summing. `value` never falls below `original`, so `delta >= 0` always — **ocean archipelagos can only raise ground, never carve it.**

Profile shapes, in units of `tectonic_relief` above `sea_level`:
- volcanic: `0.9*exp(-2d^2) - 0.1`. +0.80 at the centre, crosses sea level at d = 0.7414, -0.1 in the far field (a shallow apron).
- continental / cold: `0.45*exp(-2d^2) - 0.1`. +0.35 at the centre, crosses sea level at d = 0.8670.
- atoll: `0.22*exp(-((d-0.7)/0.24)^2) - 0.07`. A **ring**: +0.15 at d = 0.7, and at the centre `0.22*exp(-8.507) - 0.07 = -0.0700` — a lagoon floor just below sea level. It crosses sea level at d = 0.4998 and d = 0.9002.

So every character has a sub-sea-level apron whose only effect is to *raise* very deep ocean floor toward a shelf, since `max()` discards it wherever the floor is already higher.

## PART C -- monthly_temperatures and cold_habitat

```
monthly_temperatures(mean, latitude_deg, wet, seasonality):
    amplitude = 20 * sin(radians(latitude_deg)) * (1 - 0.4*wet) * seasonality
    return [ mean + amplitude * cos(2*pi*(m - 6)/12) for m in 0..11 ]
```
`cos(2*pi*(m-6)/12)` is -1 at m = 0 and +1 at m = 6, so with a positive amplitude (northern hemisphere) month 0 is the coldest and month 6 the warmest. In the southern hemisphere `sin(lat) < 0` flips it. A wet cell is maritime and damped by up to 40%.

```
cold_habitat(temps, wet, accumulation):
    if max(temps) < 1  and wet >= accumulation*0.4:  return 'ice_cap'
    if max(temps) < 10:                              return 'tundra'
    if count(t > 5 for t in temps) <= 6:             return 'boreal' if wet >= 0.4 else 'tundra'
    return None
```
Order matters: a cell colder than 1 C all year but drier than `0.4*ice_accumulation` falls through to `tundra`, not `ice_cap`. "Six or fewer months above 5 C" is the boreal/temperate line.

## PART D -- add_environment(result, cfg)

```
D0. if cfg.phase < 6 or result has no 'climate': return   (no side effects at all)
    o = options(cfg); l = result['layers']; n = cfg.size
    r = result['effective_config']['globe_radius']
    points, areas, graph = sphere_grid(n, r)
    vectors[i] = direction(points[i].x, points[i].z, n)
    water, temp, wet, slope, depth, river, height  := per-node samples of the layers
```
`sphere_grid(n, r)` node ordering is fixed and is an output contract: node 0 is the north pole `(0,0)`, then row-major `(x, z)` for `z = 1 .. n-2`, `x = 0 .. n-2`, then the south pole `(0, n-1)`. Each node carries its spherical cell area (a pole node carries the whole polar ring's area, `n-1` wedges) and a neighbour list of `(index, great-circle distance in metres)` sorted by ascending neighbour index — 8 neighbours for an interior node, the whole adjacent ring for a pole node.

```
D1. ocean distance
    seeds    = [(i, 0) for every node with water[i] == 1]
    distance = Dijkstra(graph, seeds, edge_cost = great-circle distance)
```
This is `terrain_humans.allocate_access` with the identity cost: a multi-source Dijkstra on a binary heap keyed `(distance, label, node)`. All seeds carry label 0 here, so the owner/tie-break machinery is inert and only the distance array is used. Relaxation is `candidate < distance[j]`, or `candidate == distance[j] and label < owner[j]` (never true with one label). Termination when the heap empties; a stale pop is skipped when `value != distance[i] or label != owner[i]`. Unreachable nodes (and every node if the world has no ocean) keep `+inf`.

```
D2. the per-node sweep -- one pass over every node i at grid cell (x, z)
    p    = vectors[i]
    near = [j for (j, d) in graph[i]]
    adj  = count(water[j] == 1 for j in near) / max(1, len(near))
    coast    = (water[i] == 0) and distance[i] <= max(result['spacing_m'], 120)
    exposure = clamp( adj if coast
                      else count(water[j] > 0 for j in near) / max(1, len(near)) )
    shallow  = clamp(1 - depth[i]/80) if water[i] != 0 else 0
    salt     = 1.0                                      if water[i] == 1
               else clamp((0.6 - wet[i]) * 2 * o.salinity) if water[i] == 2
               else 0.0
    reef     = shallow * clamp((temp[i] - 15)/12) * (1 - 0.5*exposure)   if water[i]==1 else 0
    lagoon   = shallow * (1 - exposure)                                  if water[i]==1 else 0
    estuary  = 1.0 if (water[i]==1 and any(river[j] and water[j]==0 for j in near)) else 0.0
    kelp     = shallow * clamp(1 - |temp[i] - 10|/15)                    if water[i]==1 else 0
    fjord    = (water[i]==1) * clamp(count(slope[j] > 12 and water[j]==0 for j in near)/3)
                             * clamp((15 - temp[i])/15)
    navigable = any(water[j]==1 and depth[j] >= o.sea_draft for j in near)
    harbor   = clamp((1 - exposure)*0.7 + 0.3) * exp(-slope[i]/22)  if (coast and navigable) else 0
    fish     = clamp(0.12 + 0.55*shallow + 0.3*estuary + 0.2*kelp + 0.15*reef) if water[i] != 0 else 0
    latitude = 90 - 180*z/(n-1)                                  # degrees
    temps    = monthly_temperatures(temp[i], latitude, wet[i], o.seasonality)
    cold     = cold_habitat(temps, wet[i], o.ice_accumulation) if water[i]==0 else None
    if cold:  l['biome'][z][x] = {boreal:15, tundra:16, ice_cap:17}[cold]
    metal    = clamp( (0.45 + 0.7*perlin3(5*p.x, 5*p.y, 5*p.z,
                                          child_seed(cfg.seed, 'metals-v1'))
                       + 0.2*l['volcanic'][z][x]) * o.metal_abundance )
```
then append, **in this exact order**, to the eighteen field arrays:
```
metal_richness       <- metal
salinity             <- salt
coastal_exposure     <- exposure
harbor_suitability   <- harbor
fishing_productivity <- fish
reef                 <- reef
lagoon               <- lagoon
estuary              <- estuary
sheltered_bay        <- harbor          # yes, the same value again
rocky_coast          <- (coast ? 1 : 0) * exposure * clamp(slope[i]/25)
kelp                 <- kelp
fjord                <- fjord
open_ocean           <- (water[i]==1 ? 1 : 0) * (1 - shallow)
maritime             <- exp(-distance[i]/300) if water[i]==0 else 0
boreal               <- (cold == 'boreal')  ? 1 : 0
tundra               <- (cold == 'tundra')  ? 1 : 0
ice_cap              <- (cold == 'ice_cap') ? 1 : 0
coastal_support      <- harbor * 0.5        # provisional, land is overwritten in D3
```
and to the twelve month arrays:
```
months[m].temperature <- temps[m]
months[m].snow        <- clamp(-temps[m]/8) * clamp(2*wet[i])  if water[i]==0 else 0
months[m].water_ice   <- clamp((-1.8*salt - temps[m]) / 6)     if water[i]!=0 else 0
```
`water_ice` is the only place `salt` reappears: ocean (salt = 1) starts freezing at -1.8 C and saturates at -7.8 C; a fresh lake (salt = 0) starts at 0 C.

```
D3. coastal support, second pass over all nodes
    for each node i with water[i] == 0:
        coastal_support[i] = harbor_suitability[i]*0.5
                           + 0.5 * max(fishing_productivity[j] for j in neighbours(i), default 0)
```
Comment, kept verbatim: *"Nearby coastal support is opportunity, never credited as harvested food."* The layer is a settlement-scoring term, not a food yield. Water cells keep `harbor*0.5 = 0`.

```
D4. l[key] = node_grid(values, points, n)   for each of the eighteen fields
```
`node_grid` scatters one value per node into an n x n grid, sets row 0 to `values[0]` repeated, row n-1 to `values[last]` repeated, and every row's column n-1 to its column 0.

```
D5. island components -- connected land, flood fill
    unvisited  = { i : water[i] == 0 }
    islandness = [0.0] * node_count
    land_total = sum(areas[i] for i with water[i] == 0)
    while unvisited not empty:
        start = min(unvisited)                 # smallest node index: determinism
        remove start; component = [start]; stack = [start]
        while stack:
            i = stack.pop()                    # LIFO -> depth-first
            for (j, d) in graph[i]:
                if j in unvisited: remove j; component.append(j); stack.append(j)
        component_area = sum(areas[i] for i in component)
        for i in component: islandness[i] = 1.0 if component_area < land_total*0.15 else 0.0
    l['island_habitat'] = node_grid(islandness, points, n)
```
8-connected on the sphere grid, with the pole node joined to its whole ring and the seam closed by `sphere_grid`'s neighbour construction, so a landmass wrapping the date line or covering a pole is one component.

```
D6. reseal biome and refresh natural_biome
    for z in 0..n-1:
        if z is a pole row: l['biome'][z] = [l['biome'][z][0]] * n
        else:               l['biome'][z][n-1] = l['biome'][z][0]
    l['natural_biome'] = row-wise copy of l['biome']
```
Needed because D2 wrote `biome` at node positions only, leaving the seam column stale.

```
D7. result['seasonal_environment'] = {
        'months': [ {k: node_grid(v, points, n) for k, v in months[m]} for m in 0..11 ],
        'method': 'Monthly latitude/elevation temperature proxy; snow and water ice do not modify bedrock.' }
```

```
D8. zone eligibility -- a second pass over all nodes
    magic(name, i) = l['ley_'+name][z][x] if that layer exists else 0.0
    dry      = 1 if water[i]==0 else 0
    arid     = clamp((0.5 - wet[i]) * 3)
    mountain = clamp(max(0, height[i]) / 100) * clamp(slope[i] / 20)

    demonic         = dry * magic('infernal')
    draconic        = dry * mountain * max(magic('primordial'), magic('weave'))
    pirate          = harbor_suitability[i]
    steampunk       = dry * metal_richness[i] * max(maritime[i], l['volcanic'][z][x], 0.2)
    witch_huts      = dry * magic('umbral') * (1 - coastal_support[i])
    red_sands       = dry * arid
    dead_sea        = (water[i]==2) * salinity[i]
    starlight_lakes = (water[i]==2) * max(magic('weave'), magic('holy'))
    haunted_sands   = dry * arid * magic('umbral')
    enchanted       = dry * wet[i] * magic('weave')
    fungal          = dry * wet[i] * magic('primordial')
    crystal         = dry * arid * magic('primordial')
    haunted_marsh   = dry * wet[i] * exp(-slope[i]/6) * magic('umbral')
```
Note `pirate` is the only score with no `dry` factor at all — it is `harbor_suitability`, which is already zero on water, so it is implicitly land-only. `height[i]` in `mountain` is **absolute height, compared against 0, not against sea level**.

```
D9. per zone, in ZONES order
    for name in ZONES:
        scores     = eligibility[name]
        rng        = Random(child_seed(cfg.seed, 'region-' + name + '-v1'))
        candidates = [i for i where scores[i] > 0.04]
        active     = (candidates is non-empty) AND (rng.random() < o[name+'_occurrence'])
        centers = []
        if active:
            ranked = stable_sort(candidates,
                                 key = -(scores[i] * (0.6 + 0.4*rng.random())))
            for i in ranked:
                if all( angle(vectors[i], vectors[j]) > o[name+'_extent']*1.5
                        for j in centers ):
                    centers.append(i)
                if len(centers) >= 3: break
        values = [ o[name+'_intensity'] * scores[i]
                   * max( exp(-(angle(vectors[i], vectors[j]) / o[name+'_extent'])^2)
                          for j in centers, default 0.0 )
                   for every node i ]
        l['zone_' + name] = node_grid(values, points, n)
        regions.append({ id: name, centers: centers,
                         maximum: max(values, default 0.0),
                         area_km2: sum(areas[i] for i where values[i] > 0.05) / 1e6,
                         reason: 'manifested'                  if centers
                                 else 'insufficient suitable habitat'   if not candidates
                                 else 'not selected by occurrence seed' })
        if name in ('witch_huts', 'haunted_sands'):
            for i in centers:
                landmarks.append({ id: name + '-' + str(i),
                                   kind: 'witch_hut' if name=='witch_huts' else 'necropolis',
                                   x: points[i].x, z: points[i].z,
                                   layer: 'surface', intensity: values[i] })
```
Critical subtleties:
- `active` short-circuits: **when `candidates` is empty, `rng.random()` is never called.** Draw counts are data-dependent.
- The jitter `0.6 + 0.4*rng.random()` is evaluated **once per candidate, in ascending candidate-node-index order**, before the sort compares anything (CPython decorates then sorts). It breaks ties between equal scores and lets a slightly weaker candidate outrank a stronger one by up to 1.667x.
- The separation test is against *already accepted* centres only, so it is a greedy farthest-first pass, not a global optimum. The first ranked candidate always passes (the `all` over an empty list is true), so `active` implies at least one centre.
- `values[i]` at a centre is `intensity * scores[i] * 1.0`.
- Zone influence is Gaussian in **angle**, with `extent` in radians, and it is *not* normalised by area, so a zone is the same angular size on a 200 km world and a 600 km one.

```
D10. result['regions']  = {'influences': regions, 'landmarks': landmarks,
                           'method': 'Independent overlapping fields; low suitability can be valuable to specialist sites.'}
     result['habitats'] = {'version': 1,
                           'aquatic': ['reef','lagoon','estuary','sheltered_bay',
                                       'rocky_coast','kelp','fjord','open_ocean'],
                           'method': 'Depth, coast exposure, salinity and climate proxies; no tides, currents or biological reef simulation.'}
```

## Constants

## Options (validated user controls; `spec(default, min, max, group, description, units)`)

Ocean islands group, read by `island_layout`:
| key | default | min | max | meaning / effect of change |
|---|---|---|---|---|
| `archipelago_count` | 5 (int) | 0 | 16 | candidate clusters *attempted*. 0 disables islands entirely. Raising it raises the draw count linearly and adds clusters roughly uniformly over the sphere. |
| `archipelago_occurrence` | 0.8 | 0 | 1 | probability each candidate survives. 0 kills all islands but still consumes the same RNG per candidate. |
| `islands_per_cluster` | 5 (int) | 1 | 12 | islands drawn per surviving cluster; also sets the cluster's radius through `sqrt(j)`. |
| `island_radius` | 0.035 | 0.008 | 0.1 | **angular** radius / planet radius, radians. 0.035 rad = 2.005 degrees = 1114 m on the 200 km preset. This is the single most raster-sensitive number in the file (see edge_cases). |
| `island_spacing` | 2.5 | 1 | 6 | spacing between islands in island radii. Below ~2 the islands merge into one mass. |
| `archipelago_extent` | 1.0 | 0.3 | 3 | multiplier on cluster footprint. At 3 the outermost island sits `0.035*2.5*3*2 = 0.525` rad from the centre — a quarter of the way round the planet. |
| `volcanic_island_weight` | 1.0 | 0 | 5 | relative chance of the tall 0.9-peak character. |
| `atoll_weight` | 1.0 | 0 | 5 | relative chance of the ring/lagoon character. |
| `continental_island_weight` | 1.0 | 0 | 5 | relative chance of the 0.45-peak character. |
| `cold_island_weight` | 1.0 | 0 | 5 | relative chance of the 0.45-peak character pinned above 66 degrees latitude. |

Cross-field rule enforced in `validate_options`: the four weights must sum to more than zero, or the request is rejected with "At least one ocean island character weight must be positive; all four are zero, so no cluster can take a character."

Options read by `add_environment`:
| key | default | min | max | meaning |
|---|---|---|---|---|
| `metal_abundance` | 1.0 | 0.1 | 2.0 | multiplier on `metal_richness` before clamping. At 2.0 most of the world saturates at 1. |
| `salinity` | 0.65 | 0 | 1 | arid-lake salinization strength. 0 makes every lake fresh, which also removes `dead_sea` entirely and unblocks lake-based freshwater in `terrain_settlements` (which tests `salinity < 0.2`). |
| `seasonality` | 1.0 | 0 | 2 | multiplier on the monthly temperature amplitude. 0 makes every month the annual mean, which makes `cold_habitat` a pure function of the mean and removes all snow/ice seasonality. |
| `ice_accumulation` | 0.5 | 0 | 1 | moisture required for persistent land ice, used as `wet >= accumulation*0.4`, so the effective moisture floor is 0.2 at the default. 0 makes every year-round-frozen cell an ice cap. |
| `sea_draft` | 1.0 | 0.1 | 10 | metres (post-scale) of water depth a neighbour needs before a coastal land cell can have a harbour. |
| `<zone>_occurrence` | 0.55 | 0 | 1 | per zone, probability it manifests at all. |
| `<zone>_extent` | 0.22 | 0.04 | 0.8 | per zone, **radians**; the Gaussian sigma of the influence field and, times 1.5, the minimum centre separation. |
| `<zone>_intensity` | 0.8 | 0 | 1 | per zone, linear gain on the published `zone_<name>` layer. |

`ZONES` (order is a contract; the `regions.influences` list and the dict iteration both follow it):
`demonic, draconic, pirate, steampunk, witch_huts, red_sands, dead_sea, starlight_lakes, haunted_sands, enchanted, fungal, crystal, haunted_marsh` — thirteen entries.

## Bare magic numbers in shape_islands

| value | where | meaning / sensitivity |
|---|---|---|
| `2` | `if d >= 2: continue` | influence cut-off in island radii. Also the outer edge of `support`. |
| `0.7` | `support = clamp((2-d)/0.7)` | width of the taper band. Support is 1 inside d = 1.3 and falls linearly to 0 at d = 2. Widening it softens the island shelf into the ocean. |
| `0.22`, `0.7`, `0.24`, `0.07` | atoll target | ring amplitude, ring radius (in island radii), ring width, and lagoon depth, all in units of `tectonic_relief`. Raise 0.07 and the lagoon deepens without touching the rim. |
| `0.9` | volcanic peak | 0.9 - 0.1 = **0.80 * tectonic_relief** above sea level at the cone summit. At the 200 km preset that is 1334 m. |
| `0.45` | continental and cold peak | 0.45 - 0.1 = **0.35 * tectonic_relief** = 583 m at the preset. |
| `2` | `exp(-2*d*d)` | Gaussian tightness of the cone; larger means a narrower, steeper island. |
| `0.1` | volcanic/continental offset | the sub-sea apron. It is what makes a shallow shelf around the island rather than a cliff. |
| `0.4`, `0.9` | cold centre reshaping | latitude pin; `copysign(0.9, y)` with x,z scaled by 0.4 yields latitudes 66.1 degrees to 90. |
| `0.7`, `1.3` | `uniform(.7, 1.3)` | per-island size jitter, +/-30%. |

## Bare magic numbers in add_environment

Metric constants. **All of these are post-`world_scale` metres and are compared against distances on the real planet, so their behaviour changes with world size and with raster.**

| value | where | meaning / sensitivity |
|---|---|---|
| **120 m** | `coast = distance[i] <= max(spacing_m, 120)` | floor under the coast band. `spacing_m` is 12500 m at the 200 km size-17 default and 697 m on the 11.15 km reference world, so this floor **never binds on any shipped world** and exists only for hypothetical very fine rasters. |
| **80 m** | `shallow = clamp(1 - depth/80)` | the shelf depth. Everything marine — reef, lagoon, kelp, open_ocean, fishing — keys off it. Deeper than 80 m is fully "open ocean". Absolute metres, not scaled by world size. |
| **300 m** | `maritime = exp(-distance/300)` | the maritime decay length. See open_questions: this collapses the layer at the default raster. |
| **22** | `harbor = ... * exp(-slope/22)` | degrees; harbour quality halves at 15.2 degrees of slope. |
| **25** | `rocky_coast = ... * clamp(slope/25)` | degrees; a rocky coast needs 25 degrees to read as 1. |
| **12** | `fjord` neighbour test `slope[j] > 12` | degrees of the surrounding land. |
| **3** | `fjord = clamp(count/3)` | three steep land neighbours saturate the term. |
| **15**, **12** | `reef = ... clamp((temp-15)/12)` | Celsius; reef needs warmer than 15 C and saturates at 27 C. |
| **10**, **15** | `kelp = ... clamp(1 - abs(temp-10)/15)` | Celsius; kelp peaks at 10 C and vanishes outside -5..25 C. |
| **15** | `fjord = ... clamp((15-temp)/15)` | Celsius; fjords need colder than 15 C, saturating at 0 C. |
| **0.6**, **2** | `salt = clamp((0.6-wet)*2*salinity)` | a lake begins salinizing below moisture 0.6 and saturates at 0.1 (before the `salinity` option scales it). |
| **0.12, 0.55, 0.3, 0.2, 0.15** | `fish` | base productivity plus shelf, estuary, kelp and reef weights. The 0.12 base is why every water cell reads at least 0.12. |
| **0.7, 0.3** | `harbor = clamp((1-exposure)*0.7 + 0.3)` | a fully exposed coast still scores 0.3; shelter adds up to 0.7. |
| **0.5** | `reef = ... (1 - 0.5*exposure)` | exposure halves reef at most. |
| **0.45, 0.7, 0.2, 5** | `metal` | base, Perlin gain, volcanic gain, and the noise frequency (5 cycles across the unit sphere, so features are roughly 36 degrees across). `perlin3` returns about +/-0.707, so the term without volcanic spans 0.45 +/- 0.495. |
| **20, 0.4, 6, 12** | `monthly_temperatures` | 20 C peak seasonal amplitude at the pole, 40% damping by a fully wet cell, month 6 as the warm peak, 12 months. |
| **1, 10, 5, 6, 0.4, 0.4** | `cold_habitat` | ice-cap ceiling 1 C, tundra ceiling 10 C, the "growing month" line at 5 C, six months of it, the boreal moisture line 0.4, and the `0.4*ice_accumulation` ice moisture factor. |
| **8, 2** | `snow = clamp(-t/8)*clamp(2*wet)` | full snow at -8 C and moisture 0.5. |
| **-1.8, 6** | `water_ice = clamp((-1.8*salt - t)/6)` | seawater freezing point in Celsius and the 6-degree ramp to full ice. |
| **0.5** | `coastal_support` weights | half from the cell's own harbour, half from the best neighbouring fishery. |
| **0.15** | `island_habitat` threshold | a land component under 15% of the world's total land area is an island. See reasoning_to_preserve. |
| **0.04** | zone candidate floor | a node under this score is not a candidate and does not receive a jitter draw. |
| **3** | zone centre cap | at most three centres per zone. |
| **1.5** | centre separation, `extent*1.5` | 0.33 rad at the default extent. |
| **0.05** | `area_km2` threshold | cells above this influence count toward the reported region area. |
| **0.6, 0.4** | ranking jitter | multiplier in [0.6, 1.0). |
| **100** | `mountain = clamp(max(0,height)/100)` | metres of absolute height. On any world with real relief this saturates to 1 on essentially all land, so `mountain` is effectively `clamp(slope/20)`. |
| **20** | `mountain = ... clamp(slope/20)` | degrees. |
| **3** | `arid = clamp((0.5-wet)*3)` | aridity saturates at moisture 0.167. |
| **6** | `haunted_marsh = ... exp(-slope/6)` | degrees; marshes demand nearly flat ground. |
| **0.2** | `steampunk = ... max(maritime, volcanic, 0.2)` | a floor that rescues steampunk when `maritime` has collapsed and there is no volcanism. |

## Seed domain strings (these are content, not decoration)

- `'ocean-archipelagos-v1'` — the island layout stream.
- `'metals-v1'` — the metal richness noise field.
- `'region-' + <zone name> + '-v1'` — thirteen independent zone streams, e.g. `'region-witch_huts-v1'`.

`child_seed(master, domain, variation=0)` in the Python is the first four bytes, big-endian, of `sha256("tectonics-v1:{master}:{domain}:{variation}")`.

## Data files

**No data file is loaded by `terrain_ecology.py`.** It imports no JSON and opens nothing.

Two indirect data dependencies the porter must honour:

1. **Biome ids 15, 16, 17.** The cold pass writes literal integers into `layers['biome']`, and those integers are positions in `Sim/icarus_sim/biomes.json`. That file's `natural_biomes` array holds one object per identity with fields `id` (int), `core` (string), `color` ([r,g,b]) and optionally `reachable: false` plus `unreachable_note`. The full id set is `0 ocean, 1 tundra, 2 desert, 3 grassland, 4 forest, 5 exposed_rock, 6 snow, 7 rainforest, 8 lake, 13 marsh, 15 boreal_forest, 16 cold_tundra, 17 land_ice`. The ids are NOT contiguous and are NOT array offsets — `terrain_biomes` states the contract: "biome and natural_biome are natural catalogue IDs, never array offsets". The file's own header warns "ORDER IS A CONTRACT ... biome_variant in a saved world stores a flat index into the natural_biomes x schools cross product."

   Directly relevant: row id 1 (`tundra`) carries `"reachable": false` and a note that names this module. Quoting the load-bearing part: this cold pass *"relabels it to 15, 16 or 17 on every cell except a floating-point measure-zero band: cos(2*pi*(3-6)/12) and cos(2*pi*(9-6)/12) both evaluate to +6.123233995736766e-17 rather than 0, so a mean temperature within about 2.2e-15 C below 5.0 C at large seasonal amplitude gets a seventh warm month, cold_habitat returns None and the label survives."* In a clean C++ rewrite with its own float semantics that band will sit somewhere else or not exist; it is a measure-zero artefact and no behaviour should be built on it. But the *id 1 row must still exist in the catalogue*, because `biome_variant` indexes a flat 13 x 12 cross product.

2. **`ZONES` and the option table** live in `terrain_world.py`, not in JSON. The thirteen zone names and their three options per name are generated by a loop; the tuple order is the contract.

## Random streams

Three independent uses, no shared stream anywhere.

**1. `island_layout` — one stream, `child_seed(seed, 'ocean-archipelagos-v1')`, seeding a `random.Random`.**

Draw accounting per candidate cluster k (a "draw" = one `random()` call):
- centre `random_point`: **2**
- `choices` for the character: **1** (CPython's weighted `choices` takes exactly one draw per pick and does a `bisect_right` on the accumulated weights)
- the cold reshaping: **0**
- the occurrence roll `rng.random() > occurrence`: **1**, taken unconditionally, even when the cluster is about to be dropped
- if the cluster survives, per island j: `random_point` **2** + `uniform(0.7, 1.3)` **1** = **3**

So a dropped candidate costs 4 draws and a surviving one costs `4 + 3*islands_per_cluster` — 19 at the defaults. **The draw count is data-dependent**, because `continue` skips the island loop. Total at defaults with all five surviving: 95 draws.

**2. `metal_richness` — no stream at all.** It is a deterministic `perlin3` lookup at `5*p` with the noise seed `child_seed(seed, 'metals-v1')`. Zero draws.

**3. Regional characters — thirteen independent streams**, one per zone, `child_seed(seed, 'region-' + name + '-v1')`. Per zone:
- **0 draws** if no node scores above 0.04 (the `candidates and rng.random() < ...` short-circuits before the roll).
- **1 draw** if there are candidates but the occurrence roll fails.
- **1 + len(candidates) draws** if it manifests — one per candidate for the sort jitter, evaluated in ascending candidate index order.

`len(candidates)` is a function of the world, so this is strongly data-dependent. Because each zone has its own stream, adding or removing a zone, or changing how many candidates one zone has, cannot move any other zone.

`add_environment` itself consumes nothing else: the flood fill, the Dijkstra and the eighteen field evaluations are all deterministic.

**C++ mapping.** `Rng::child(master, "ocean-archipelagos-v1")` and `Rng::child(master, "region-" + name + "-v1")` for the streams; `child_seed(master, "metals-v1")` (the 32-bit derivation) for the noise field. This introduces no fourth seed derivation. Weighted choice must be written out: accumulate the four weights, draw once, `upper_bound` on the cumulative array, clamp the result to the last index.

## Ordering hazards

Every place iteration order reaches output, in the order it appears:

1. **`island_layout`'s candidate loop.** The stream is consumed in candidate order, so cluster k's centre depends on how many draws clusters 0..k-1 took, which depends on whether each survived. Reordering or parallelising the loop changes every world.

2. **`'ocean-%d' % k` uses the candidate index.** In CPython the inner `weights=[o[k] for k in (...)]` comprehension has its own scope and does **not** clobber the outer `k`; a naive C++ port that reuses one loop variable for both would silently label every cluster `ocean-3`. Use two distinct names.

3. **`shape_islands`'s cluster and island loops** feed a `max()`, which is order-independent in exact arithmetic but not in floating point. It does not matter for a clean rewrite.

4. **The per-row seam seal happens inside the `z` loop**, immediately after the row's cells are computed. Doing it after the whole grid is filled is equivalent here (no cell reads another cell), but the structure must be preserved if anything ever becomes stencil-based.

5. **`sphere_grid` node order is an output contract**, not an implementation detail: node 0 = north pole, then `(x, z)` row-major for `z = 1..n-2`, `x = 0..n-2`, then node `last` = south pole. `node_grid` relies on `values[0]` being the north pole and `values[-1]` the south pole to fill the pole rows. Every field array, every months array, the `islandness` array, the zone score arrays and the `centers`/landmark node indices are all indexed by this ordering. Changing it renumbers every landmark id and every `regions.centers` entry.

6. **Neighbour lists are `sorted(ids)`** in `sphere_grid` — ascending node index. `estuary`, `fjord`, the harbour `navigable` test and the `coastal_support` max are all `any`/`max` reductions, so the order does not change the value, but the C++ `SphereGrid` must keep the same sort because other stages do depend on it.

7. **`fields` is an insertion-ordered dict zipped against a positional tuple**: `for key, value in zip(fields, values)`. The eighteen keys and the eighteen tuple entries are paired **by position only**. Inserting a key, or a value, without inserting the matching other one silently mis-assigns everything after it, and `zip` truncates without error. In C++ use a fixed-size array of named indices, or a struct, so that a mismatch is a compile error.

8. **`min(unvisited)` in the flood fill** is what makes component discovery deterministic despite `unvisited` being a `set`. The component list order itself (LIFO stack, so depth-first) does not reach output because only the summed area matters — but `min` does, and a C++ port using an unordered container without an explicit "lowest remaining index" scan would produce different components only if the connectivity were ambiguous (it is not) — still, keep the low-index start so the code reads the same way. Use a `std::vector<bool> visited` plus a monotone cursor; `min()` over a shrinking set is O(k^2) in the Python and is worth not reproducing.

9. **`eligibility` is built as `{name: [] for name in ZONES}`**, so `for name, scores in eligibility.items()` walks `ZONES` order. `regions['influences']` is therefore in `ZONES` order, and any consumer reading it positionally depends on that.

10. **`sorted(candidates, key=...)` is a stable sort and the key function is evaluated once per element, in ascending candidate order.** Both halves are load-bearing: the RNG jitter is drawn per candidate in that order, and ties in the jittered score are broken by ascending node index. In C++, precompute the keys in ascending order into a vector, then `std::stable_sort`. `std::sort` is not stable and would change which node becomes a centre on a tie.

11. **The greedy centre loop** accepts centres in ranked order and tests separation only against already-accepted ones, so the result depends on the ranking, not on any global criterion.

12. **`max(values, default=0.)`** for `regions.maximum` and **`sum(...)`** for `area_km2` walk node order; the sum is a plain left-to-right accumulation.

13. **The two-pass structure of `coastal_support`** is required. Pass 1 stores `harbor*0.5` for every node, pass 2 overwrites every land node with `harbor*0.5 + 0.5*max(neighbour fishing)`. Fusing them into one pass would read neighbours' `fishing_productivity` before they were written.

14. **`terrain_area` calls `generate_networks` before `add_environment`; `terrain_history` stage 8 does not.** The same function therefore produces different zone fields depending on which caller ran it, and stage 9's `refresh_environment` exists to fix that.

No process pool anywhere in this module. `ICARUS_CITY_WORKERS` is a `city_planner`/`hamlet_planner` concern and does not touch ecology.

## Edge cases

**shape_islands**

- `archipelago_count = 0` -> empty cluster list, `archipelago_relief` still created as an all-zero grid, `ocean_archipelagos` is `[]`. `test_terrain_world_layers.py:128` asserts exactly this.
- Every cluster ineligible -> `archipelago_relief` is identically zero. **This is the normal case, not an error.** Measured at size 17, 200 km, phase 2, seeds 40-55: 57 candidate clusters survived the occurrence roll, 30 were eligible, and only 11 of the 16 worlds had any nonzero relief at all. Seed 42 — the project's reference seed — has **three clusters, zero eligible, zero relief.**
- **Islands are sub-cell at the default raster.** The influence radius is `2 * angular_radius`, about 0.07 rad, while the grid's angular step is `2*pi/(n-1)` = 0.3927 rad at n = 17. An island therefore covers about 1.5% of the sphere per cell and hits a node only by luck. Islands become roughly one cell wide at n ≈ 91 and about 3.6 cells wide at n = 257. This is a function of `size` alone — the angular radius scales with the planet, so world width does not change it. That is why seeds 41, 45 and 49 have eligible clusters and still show zero relief: every island missed every node.
- Pole rows: the `x` loop runs once (`x = 0`), the whole row is then overwritten with that one value. A cluster centred on a pole raises the entire polar ring.
- `acos(p[1])` in the eligibility sample: after the cold-branch renormalisation `p[1]` can exceed 1 by an ULP. Clamp to [-1, 1] before `acos`, or it throws.
- `round()` in the eligibility sample is Python's banker's rounding (half to even). `std::lround` rounds half away from zero. The difference is one cell at an exact half and is acceptable in a clean rewrite; do not try to reproduce it.
- `x` can round to `n-1`, which is the duplicate seam column — a legal read. `z` can round to 0 or `n-1`, a pole row — also legal.
- `cfg.phase == 2`: `base` is a separate continental-profile grid and receives its own `+= delta`. At `phase >= 3` `base` was copied from `structure` before the islands ran and then also receives `+= delta`, so the two stay equal. At `phase >= 4` `base` is replaced wholesale by a copy of `surface`. All three behaviours must be preserved because all three phases are requestable.
- `delta` is never negative: `value` is seeded with `original` and only ever raised by `max`.

**add_environment**

- `cfg.phase < 6` or no `climate` block -> immediate return, **no layer and no block written**. Because `terrain_society.add_world_society` gates on `'habitats' in result`, this silently disables the society stage too.
- **No ocean in the world** (`route_water` found no below-sea component, or the largest one is a lake): the Dijkstra seed list is empty, every `distance[i]` stays `+inf`, so `coast` is false everywhere, `harbor`, `rocky_coast` and `coastal_support` are zero everywhere, and `maritime = exp(-inf/300) = 0.0` (not NaN). `pirate` is then identically zero and reports `"insufficient suitable habitat"`.
- **Land unreachable from any ocean** (an inland cell separated by nothing — impossible on an 8-connected sphere graph unless every node is water) — same `+inf` handling.
- **World with no land**: `unvisited` is empty, the flood-fill loop never runs, `land_total = 0`, `island_habitat` is all zero. No division by zero occurs (`land_total` is only ever multiplied).
- **World that is one landmass**: `component_area == land_total`, `land_total*0.15 < land_total`, so `islandness = 0` everywhere. Correct, and `test_layer_scale_regression` explicitly refuses to assert otherwise: *"a world whose land is a few comparable masses legitimately has no island (seed 45 at phase 6 yields two components of 56 and 50 cells and zero islands)"*.
- `max(1, len(near))` guards the neighbour fractions; `len(near)` is never 0 on a real sphere grid, but the guard is there.
- `max(..., default=0.)` guards the `coastal_support` neighbour max and the zone Gaussian max over an empty `centers` list.
- `max(values, default=0.)` guards `regions.maximum` when there are no nodes.
- Lakes: `shallow` is computed for `water[i] != 0`, so a lake gets a shelf value, and `fish` is computed for `water[i] != 0`, so **a lake has fishing productivity** (0.12 to about 0.67) even though `reef`, `lagoon`, `estuary`, `kelp`, `fjord` and `open_ocean` are all gated on `water[i] == 1` and are zero there.
- `estuary` requires an ocean cell with a land neighbour carrying a river. A river reaching the sea through a lake produces no estuary.
- A cell in the `coast` band that has no navigable neighbour (`depth >= sea_draft`) gets `harbor = 0` and therefore `sheltered_bay = 0` and `pirate = 0`.
- `mountain`'s `max(0, height)` compares against **absolute zero**, not `sea_level`. On a world with a nonzero `sea_level` this silently shifts the mountain term; at the default `sea_level = 0` it does not.
- The cold pass writes `biome` only at node positions, leaving the seam column stale until D6 reseals it. Skipping D6 leaves a visible one-column stripe in the rendered biome map.
- `l['natural_biome']` is rebuilt by shallow row copies (`[row[:] for row in ...]`), so the rows are independent but a nested mutable would be shared. Biome rows hold ints, so this is safe as written.

**World edges and poles, generally**: there is no edge — the grid wraps at the seam and the poles are single nodes joined to their whole ring. Every reduction here is over `sphere_grid` neighbours, which already encode that, so no special-casing is needed beyond preserving `node_grid`'s pole/seam fill.

## Reasoning worth preserving

**The island_habitat threshold.** This is the single most expensive comment in the file and it records a measured defect, its fix, and an explicit correction of an earlier wrong claim. Quoted in full from terrain_ecology.py:130-147:

> A component is an island when it is small RELATIVE TO THIS WORLD'S LAND. The dropped `min(3e6,...)` arm was 3 km2 in absolute metres, authored for the 11.15 km reference world where every raster cell is under 0.25 km2. At the 200 km default the smallest sphere-grid cell is 15.217 km2 at size 17, so at THE DEFAULT RASTER no component of any seed can reach the floor and island_habitat is identically zero -- a published layer that had become a constant.
>
> Scoped to size 17 deliberately: an earlier revision of this comment claimed the layer was zero at EVERY raster and that is false. Cells shrink to 1.914 km2 at size 33 and 0.240 km2 at size 65, so a one-cell polar component can clear the 3 km2 floor. Measured at phase 12 on the 200 km default: under the old arm seed 40 reads 1 island cell at size 33 and 6 at size 65 and seed 43 reads 1 at size 65, against 29 / 167 / 166 under the fraction arm. Zero at the default raster, all but zero above it -- not a mathematical constant there.
>
> The fraction arm is the one that bound on every world on disk (the 3e6 arm never bound at any raster there), so dropping the metre arm reproduces all twelve archived worlds cell-for-cell while restoring the layer at 200 km.

Three things the C++ port must carry from this: (a) the test is a **fraction of the world's own land**, never an absolute area; (b) the old arm is gone and must not be reintroduced "for safety"; (c) the reason dropping it was safe is that the fraction arm was always the binding one on the archived worlds.

**The regression test's framing.** `Sim/tests/test_layer_scale_regression.py` exists for this class of defect and states its method: *"These are SHAPE assertions, deliberately. The class of defect is 'a layer became a constant', and a shape assertion catches the next absolute-metre constant too, where a value assertion would only re-break on the next retune."* Its measured island counts for the four cases (seed 42 size 17, 42/33, 43/17, 43/33) are **1 of 47, 42 of 268, 14 of 48 and 36 of 271** land cells. Those are the numbers a C++ port should be in the same league as.

**Coastal support is opportunity, not food.** terrain_ecology.py:117: *"Nearby coastal support is opportunity, never credited as harvested food."* The layer feeds settlement site scoring; actual fish yield goes through `terrain_seasons` and the ports in `terrain_society`, which allocate each water cell to exactly one port. Do not let `coastal_support` become a food term.

**shape_islands' docstring.** *"Compact ocean uplifts before erosion and drainage; retain continental foundations."* Both halves are decisions: run before erosion so the islands weather and drain naturally, and never modify ground whose crust profile is already continental.

**Region intent.** `result['regions']['method']`: *"Independent overlapping fields; low suitability can be valuable to specialist sites."* The fields are deliberately not partitioned or normalised — two regions may overlap, and a low `zone_witch_huts` score is not noise, it is exactly what a hermit wants. `terrain_society` builds on this: *"Huts follow their own preference for isolation and poor conventional suitability."*

**Habitat honesty.** `result['habitats']['method']`: *"Depth, coast exposure, salinity and climate proxies; no tides, currents or biological reef simulation."* And `seasonal_environment`: *"Monthly latitude/elevation temperature proxy; snow and water ice do not modify bedrock."* These strings are published to consumers and are the module's contract about what it is not. Port them verbatim.

**The `fishing_productivity` domain, from `key_locations/core/fields.py`** (the reason `coastal_fishing_productivity` exists at all): *"A shore building is a land feature whose reason for existing is out at sea. A whaling station stands on land and lives off `fishing_productivity`, which is an ocean quantity: measured on seed 42 at sizes 17, 33, 65, 129 and 257, it is identically 0.0 on every land cell and runs 0.12 to 1.0 on water. So a `domain: land` archetype gating on it is unplaceable by construction, at every raster, forever."* If a C++ port ever makes `fishing_productivity` nonzero on land, that derived layer becomes a duplicate and the archetypes behind it change meaning.

**The tombstoned tundra row**, from `biomes.json`: id 1 is `"reachable": false` *because* this module's cold pass relabels essentially every cell that `classify()` would have given it. The row stays because `biome_variant` is a flat index into a 13 x 12 cross product. Deleting it renumbers every variant in every saved world.

**The hidden-school note in terrain_world.py** matters here only indirectly: `ley_holy` and `ley_primordial`, which four of the thirteen zone scores read, are *compatibility projections* (`magic['compatibility_fields'] = {'ley_holy': 'radiant', 'ley_primordial': 'maximum of fire, water, earth, air'}`), described as *"Compatibility projections for existing ecological/nest requirements, never extra networks."* They exist for this file. Do not add `holy` or `primordial` to the network taxonomy.

## Notes for the C++ implementation

**What already exists that this maps onto.**
- A layer is `fwg::Grid` (n x n doubles, column n-1 duplicating column 0, rows 0 and n-1 a single repeated pole value). `Grid::fill_row(z, values)` takes `distinct_count(z, n)` values and does the pole/seam expansion, which is exactly what `shape_islands`' inner row loop wants.
- `fwg::sphere_grid(n, radius)` returns a `shared_ptr<const SphereGrid>` with `point_x`, `point_z`, `areas`, and `neighbours` as `vector<vector<SphereNeighbour{index, distance}>>`. `SphereGrid::to_grid(values)` is `node_grid` — it already closes the seam and the poles. Verify it fills row 0 from `values[0]` and row n-1 from `values.back()`; the Python depends on that.
- `fwg::direction(x, z, n)`, `fwg::perlin3(x, y, z, seed)`, `fwg::Rng::child(master, domain, variation)`, `fwg::child_seed(master, domain, variation)` all exist. **No fourth seed derivation is needed**: `'ocean-archipelagos-v1'` and `'region-<zone>-v1'` are `Rng::child` domains; `'metals-v1'` is a `child_seed` for the noise field.
- `fwg::Config` is where the Python's `world_options` must land. The precedent is already set — `mountain_abundance` lives in `world_options` in Python and is a plain `Config` field in C++. Add the ten Ocean-islands options plus `metal_abundance`, `salinity`, `seasonality`, `ice_accumulation`, `sea_draft` and the 39 zone options the same way, with their bounds enforced in `Config::validate()` and the cross-field rule (island weights must sum > 0) alongside them.

**What does not exist and must be written.**

1. *A weighted choice.* `Rng` has `choice` (uniform) and `below`, not a weighted pick. Write it out: accumulate four doubles, one `random()`, `std::upper_bound` on the cumulative array, clamp the index to 3.

2. *A multi-source Dijkstra.* `terrain_humans.allocate_access` has no C++ equivalent yet. Ecology needs only the degenerate single-label form:
```cpp
std::vector<double> ocean_distance(const SphereGrid& g, const std::vector<int>& water) {
    std::vector<double> dist(g.node_count(), std::numeric_limits<double>::infinity());
    using Item = std::pair<double, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> q;
    for (std::size_t i = 0; i < g.node_count(); ++i)
        if (water[i] == 1) { dist[i] = 0.0; q.emplace(0.0, static_cast<int>(i)); }
    while (!q.empty()) {
        auto [v, i] = q.top(); q.pop();
        if (v != dist[i]) continue;                 // stale entry
        for (const auto& nb : g.neighbours[i]) {
            const double cand = v + nb.distance;
            if (cand < dist[nb.index]) { dist[nb.index] = cand; q.emplace(cand, nb.index); }
        }
    }
    return dist;
}
```
Non-negative edges, so it is exact and needs no reach bound. Later stages will want the full owner/tie-break version; write that one and let ecology pass a single label.

3. *The eighteen-field array.* The Python's `dict` + `zip(fields, values)` positional pairing is a trap. Use `enum class EcoField { MetalRichness, Salinity, ..., CoastalSupport, Count }` and a `std::array<std::vector<double>, static_cast<std::size_t>(EcoField::Count)>`, so a mismatch cannot compile.

4. *The flood fill.* Replace `while unvisited: start = min(unvisited)` (O(k^2)) with a `std::vector<char> visited` and a forward cursor over node indices, plus an explicit `std::vector<int>` stack. Same components, same determinism, linear.

5. *The stable sort with per-element RNG keys.* Do not put the draw inside the comparator. Build `std::vector<std::pair<double,int>> keyed` in ascending candidate order, drawing once per candidate, then `std::stable_sort` on the negated jittered score with the node index as the implicit secondary key.

6. *The `ocean_archipelagos` block.* `World` holds `std::map<std::string, Grid> layers` plus typed blocks. Add:
```cpp
struct IslandSite   { Vec3 direction; double angular_radius; };
struct Archipelago  { std::string id; std::string kind; Vec3 direction;
                      std::vector<IslandSite> islands; bool eligible; };
```
and serialise through the library's own JSON writer. `kind` is a fixed four-value enum in practice; keep the string for the JSON but use an enum internally.

7. *The `regions`, `habitats` and `seasonal_environment` blocks.*
```cpp
struct RegionInfluence { std::string id; std::vector<int> centers;
                         double maximum; double area_km2; std::string reason; };
struct RegionLandmark  { std::string id; std::string kind; int x, z;
                         std::string layer; double intensity; };
struct MonthField      { Grid temperature, snow, water_ice; };
```
`seasonal_environment` is **36 full n x n grids**. At n = 513 that is 36 * 513^2 * 8 bytes = 75 MB. The Python stores them as nested lists and pays far more; still, consider whether `Grid` is the right container here or whether a 12-slice node-indexed store with lazy `to_grid` is better. It is a published block, so the JSON shape must match whatever the reader expects.

8. *`Config` has no `world_recipe` field.* The Python gates `shape_islands` on `cfg.world_recipe and cfg.phase >= 2`. Recipe 3 is the only recipe, so the C++ condition is just `phase >= 2`.

**Python idioms with no direct equivalent.**
- `random.choices(pop, weights=w)` -> written out, above.
- `candidates and rng.random() < p` -> `!candidates.empty() && rng.random() < p`. The short-circuit is load-bearing (it is what makes an empty-candidate zone cost zero draws); `&&` gives it for free, but do not "clean it up" by hoisting the draw.
- `max(gen, default=0.)` -> an explicit loop with an initial 0.0, or `std::max_element` on a possibly empty range guarded by `.empty()`.
- `float(bool)` -> `cond ? 1.0 : 0.0`.
- `math.copysign(.9, y)` -> `std::copysign(0.9, y)`.
- `round()` (banker's) -> `std::nearbyint` with `FE_TONEAREST` matches Python's half-to-even; `std::lround` does not. Either is fine for a clean rewrite; pick one and say which in a comment.
- `math.acos` on a dot product -> always clamp the argument to [-1, 1] first. The Python's `angle()` does; the eligibility sample's `acos(p[1])` does not, and it is a latent domain error.
- `f'{name}-{i}'` -> `name + "-" + std::to_string(i)`.
- `l.get('ley_'+name, [[0.]])[z][x] if 'ley_'+name in l else 0.` -> a `layers.find(key)` returning 0.0 on miss. The Python expression is redundant but its meaning is just "0 if absent".
- Python `round` on `math.atan2(...)` giving `x == n-1` indexes the seam column; `Grid::at(n-1, z)` is in range and holds the same value as column 0. No special case needed.

**Performance.** `add_environment` is two passes over roughly `(n-1)^2` nodes plus one Dijkstra plus thirteen zone passes, each of which evaluates `angle()` against up to three centres for every node. At n = 513 that is 263k nodes x 13 zones x 3 `acos` calls = 10M transcendentals. Precompute `cos(extent * 1.5)` and compare dot products directly instead of calling `acos` in the separation test; for the Gaussian you do need the angle, but you can skip any node whose dot product is below `cos(3*extent)` (the Gaussian is `exp(-9)` there, under 1.3e-4).

**Testing.** Bit-exact parity is not the target, but two properties are worth pinning: (a) `archipelago_relief >= 0` everywhere, (b) `island_habitat` is nonzero on at least one and fewer than all land cells at seed 42 sizes 17 and 33 and seed 43 sizes 17 and 33 — the same four cases and the same shape assertion the Python test uses, for the same reason.

## Open questions

Four things a porter must decide, all verified against the running code rather than inferred.

**1. `maritime` has collapsed to zero at every shipped raster, and nothing guards it.** `maritime = exp(-ocean_distance_m / 300)` with 300 absolute metres. Measured, seed 42 size 17 at the 200 km preset, phase 9: **max 0.00031, and only 3 cells of 289 exceed 1e-6.** The reason is arithmetic, not seed-specific: the equatorial cell spacing is 12500 m, so the nearest land node to any ocean node sits at `exp(-12500/300) = 7.6e-19`. The only cells that read anything are in the polar rows, where the longitudinal edge shrinks to `r * (2*pi/(n-1)) * cos(78.75 deg) = 2438 m` and `exp(-2438/300) = 3.0e-4` — which is exactly the measured maximum. On the 11.15 km reference world the spacing was 697 m and `maritime` peaked near 0.098, so the constant was authored there and never rescaled.

This is the same defect class as the `island_habitat` `min(3e6, ...)` arm, and `test_layer_scale_regression.py` was written for it — but that file covers `island_habitat`, `freshwater_distance` and `flood_risk`, **not `maritime`**. Consumers: `terrain_civilizations` reads it as a settlement-context field, `terrain_nests` reads it as `coast`, and `steampunk` reads it under a `max(..., 0.2)` floor that hides the collapse. Should the C++ scale the 300 m against the reference world (`300 * circumference / 11148.96`), express it in cell spacings (`exp(-distance / (0.43 * spacing_m))` reproduces the reference-world behaviour), or keep the literal and accept that `maritime` is a dead layer? **I did not change it and I am not guessing.** Owner ruling needed.

**2. `sheltered_bay` is a byte-for-byte duplicate of `harbor_suitability`.** The `values` tuple stores `harbor` at both position 3 and position 8. Confirmed empirically: both layers read 0.0..0.854618 with identical extrema at seed 42 size 17. `sheltered_bay` is advertised in `habitats['aquatic']` as a distinct marine habitat but is a land-only harbour score. Is this an unfinished expression (the obvious intent would be something like `shallow * (1 - exposure)` on water — which is what `lagoon` already is), or a deliberate alias? Porting it as a duplicate preserves behaviour; porting it as a distinct field does not. It has no in-repo consumer, so either choice is invisible until an archetype gates on it.

**3. Ocean archipelagos are sub-cell at every raster anyone runs.** The influence radius is `2 * island_radius` = 0.07 rad; the grid step is `2*pi/(n-1)` = 0.3927 rad at n = 17, 0.049 at n = 129, 0.0245 at n = 257. So an island first spans one cell at n ≈ 91 and is about 3.6 cells wide at n = 257. Measured at size 17 over seeds 40-55: 30 of 57 surviving clusters were eligible, and 5 of those 16 worlds had **zero** archipelago relief despite having eligible clusters, because every island missed every node. Seed 42 has zero eligible clusters and therefore an identically zero `archipelago_relief` layer. Is `island_radius`'s 0.035 default (and its 0.1 ceiling) intended to be this small relative to the default raster, or should the default rise so islands resolve at the shipped `size`? The C++ should either raise it or state in a comment that archipelagos are a high-raster feature.

**4. `mountain` compares absolute height against zero, not against sea level.** `mountain = clamp(max(0, height[i]) / 100) * clamp(slope[i] / 20)`, and `height` is absolute metres. At the default `sea_level = 0` the two agree, but a request with a nonzero `sea_level` silently shifts which ground counts as mountain for the `draconic` zone. Separately, the `/100` saturates on essentially all land on any world with real relief (the 200 km preset carries a 1667 m relief budget), so `mountain` is in practice just `clamp(slope/20)`. Is the 100 m intended as a real threshold, or is it vestigial from a low-relief era? Porting it verbatim is safe; the porter should know it does almost nothing.

Minor, non-blocking: the `pirate` score is `harbor_suitability` with no `dry` factor, relying on harbour being zero on water — correct but fragile if `harbor` is ever extended to water cells. And `regions.landmarks[].id` embeds a **node index**, which renumbers with `size`; `terrain_society` later deletes and re-mints every `witch_hut` landmark with a different id scheme (`witch-hut-<i>` with a hyphen, versus this module's `witch_huts-<i>` with an underscore), so two id formats for the same kind coexist in one world depending on the phase reached.

## Addenda from the audit

The reader above wrote the specification; a second reader went back to the source
to find what it had left out or got wrong. Both are kept: the omissions are
content the implementer needs, and the corrections say where the specification
above is not to be trusted.

### Corrections

- **Claimed:** Profile shapes: volcanic `0.9*exp(-2d^2) - 0.1`. +0.80 at the centre, crosses sea level at d = 0.7414.
  **Actually:** The formula is right, the crossing is not. 0.9*exp(-2d^2) = 0.1 gives d = sqrt(ln(9)/2) = 1.048147. At the spec's stated d = 0.7414 the profile is still +0.1998 * tectonic_relief ABOVE sea level (about +333 m at the 200 km preset), not zero. The volcanic island's above-water radius is 1.0482 island radii, not 0.7414 - a 41% understatement of radius and roughly 2x of above-water area. (The continental/cold figure in the same paragraph, d = 0.8670, is correct: exact value 0.867202.)

- **Claimed:** atoll: `0.22*exp(-((d-0.7)/0.24)^2) - 0.07` ... It crosses sea level at d = 0.4998 and d = 0.9002.
  **Actually:** Again the formula is right and the crossings are not. Solving 0.22*exp(-u^2) = 0.07 with u = (d-0.7)/0.24 gives u = +/- sqrt(-ln(0.07/0.22)) = +/- 1.069956, so d = 0.7 -/+ 0.256789: the rim stands above sea level between d = 0.443174 and d = 0.956826. At the spec's stated 0.4998 and 0.9002 the profile is +0.0397 * tectonic_relief, still above water. Consequence for anyone validating a port: the dry rim is 0.5137 island radii wide, not 0.4004, and the lagoon is 0.8863 island radii across, not 0.9996.

- **Claimed:** `sphere_grid` ... a neighbour list of `(index, great-circle distance in metres)` sorted by ascending neighbour index -- 8 neighbours for an interior node, the whole adjacent ring for a pole node.
  **Actually:** False on the two rows adjacent to each pole. `sphere_grid` builds `ids` as a SET of `index(x+dx, z+dz)`, and `index` collapses z<=0 and z>=n-1 onto the single pole node, so at z = 1 the three dz = -1 entries dedupe to one. Measured: at n = 17 the node counts by neighbour-list length are z=0 -> 16, z=1 -> 6 (x16), z in 2..14 -> 8, z=15 -> 6 (x16), z=16 -> 16; at n = 33, z=1 and z=31 likewise have 6 while interior rows have 8. This is load-bearing because `adj` and the non-coast branch of `exposure` are `count / max(1, len(near))`, so on those rows the denominator is 6, not 8, and `harbor`, `reef`, `lagoon` and every `exposure`-derived term shift accordingly. The existing `fwg::sphere_grid` already uses a `std::set<int>` and so already matches the Python - the spec's sentence is the thing that is wrong, and a future rewrite following it literally would diverge.

- **Claimed:** WRITES table: `coastal_exposure` | all cells | 0..1 | fraction of neighbours that are water.
  **Actually:** Only on the non-coast branch. The source is `exposure = clamp(adj if coast else sum(water[j]>0 for j in near)/max(1,len(near)))` where `adj = sum(water[j]==1 ...)/max(1,len(near))`. For a cell in the coast band (`water[i]==0 and distance[i] <= max(spacing_m,120)`) it is the fraction of neighbours that are OCEAN specifically - a lake neighbour does not count. Everywhere else it is the fraction with `water_type > 0`, which does include lakes. The spec's own algorithm section (D2) states this correctly; the summary table contradicts it.

- **Claimed:** stage 9: `generate_networks(...)` then `refresh_environment(...)` (terrain_history.py:509-517)
  **Actually:** Provenance only, but wrong: `refresh_environment` is defined at terrain_history.py:35-42 and the stage-9 call site is line 546. Line 511 is merely the deferred `from .terrain_ecology import add_environment` inside `generate_history`. Also `island first spans one cell at n ~ 91 and is about 3.6 cells wide at n = 257` is inconsistent with the spec's own figures: 2*island_radius / (2*pi/(n-1)) = 0.07 / 0.024544 = 2.85 cells at n = 257 (3.7 only for a maximally jittered 1.3x island).

### Missing — important

**add_environment re-runs on every age advance, not just at history stage 9**

`terrain_history.refresh_environment` (defined at terrain_history.py:35-42) is called from THREE places, not one: stage 9 of genesis (line 546), the age-advance path `advance_age` (line 304: `if evaluate: evaluate_networks(result,cfg); refresh_environment(result,cfg); add_biome_variants(result,cfg)`), and the player-operation age path (line 839: `evaluate_networks(result,cfg); refresh_environment(result,cfg); add_biome_variants(result,cfg); refresh_astrology(result,cfg)`). The spec names only stage 9. Its body is: pop `layers['magic_density']`, run `add_terrain_labels(result, replace(cfg, phase=6))`, restore `magic_density` if it was present, run `add_environment(result, replace(cfg, phase=6))`, then `layers['natural_biome'] = copy.deepcopy(layers['biome'])`. Functional consequence a porter must carry: all 32 layers, both `regions` sub-lists, `seasonal_environment` and `habitats` are REBUILT from scratch at every age turn. Because `cfg.seed` is unchanged, each zone's `region-<name>-v1` stream replays the same values, but the candidate set and scores have changed, so zone centres move, `regions.landmarks[].id` (`<zone>-<node index>`) changes, and `zone_*` fields differ age to age. A port that wires add_environment only into genesis stages 8/9 loses this silently, because `STATE_KEYS` carries `habitats`, `regions` and `seasonal_environment` forward and the stale blocks still validate.

**The per-node spherical area formula that area_km2 and the island_habitat 15% test both depend on**

From `terrain_erosion.sphere_grid`, with `step = pi/(n-1)` and `lat = pi/2 - z*step`: `area = radius^2 * 2*step * (sin(min(pi/2, lat + step/2)) - sin(max(-pi/2, lat - step/2)))`, and a pole node (z == 0 or z == n-1) carries `area * (n-1)` because it stands in for the whole polar cap. Note `2*step` is the longitude wedge width (2*pi/(n-1)), not a factor of two on the latitude band. The spec says only 'per-node spherical area' and defers to `fwg::sphere_grid`. Both `regions.influences[].area_km2` (sum of areas where value > 0.05, divided by 1e6) and the `component_area < land_total*0.15` island test are pure functions of these numbers, and the file's own tuning comment quotes them (15.217 km2 smallest cell at size 17, 1.914 at size 33, 0.240 at size 65) - so the formula that produced those figures should be on record.

**The full allocate_access contract the spec tells the porter to write**

Signature `allocate_access(graph, seeds, cost, limit=math.inf)`, docstring 'One terrain-cost owner per node; deterministic ties, no water shortcuts.' It returns THREE arrays `(distance, owner, parent)`, all initialised to `inf`/-1/-1. Seeds are `(node, label)` pairs: `distance[node]=0.; owner[node]=label; heappush(queue,(0.,label,node))`. Loop: pop `(value,label,i)`; `if value!=distance[i] or label!=owner[i]: continue`; for each `(j,d)` in `graph[i]`, `edge = cost(i,j,d)`, and **`if edge is None: continue`** - the cost function may VETO an edge outright, which is how other callers implement 'no water shortcuts'. Relaxation is `candidate = value + edge` accepted when `candidate <= limit and (candidate < distance[j] or (candidate == distance[j] and label < owner[j]))`; on acceptance set distance/owner/parent and push. The spec's C++ sketch drops `limit`, the `None` veto and the `parent` array. Ecology itself passes `lambda i,j,d: d` with no limit and one label, so none of it matters here - but the spec explicitly instructs the porter to 'write that one [the full owner/tie-break version] and let ecology pass a single label', and these are the three pieces of it that are not described anywhere.

**biomes.json id 6 (snow) is tombstoned by this same cold pass, and its note differs from id 1's**

The spec records only the id 1 (`tundra`) `reachable: false` row. Row id 6 (`snow`, color [230,240,241]) carries its own `unreachable_note`, also naming this module: 'classify() in terrain_biomes.py assigns this label at or below 0 C and the ecology cold pass (cold_habitat in terrain_ecology.py) ALWAYS relabels it to 15, 16 or 17. Unlike 1 there is no floating-point survival band: cold_habitat returns None only when a seventh month exceeds 5 C, and the seventh-largest month is the mean plus amplitude*6.12e-17, which cannot exceed 5 when the mean is at or below 0. classify() keeps its branch because terrain_biomes.py derives the Snow region feature marker from biome 6, and removing it would silently empty world output of those markers.' Both rows were tombstoned by 'user ruling 0.2 on 2026-09-20: art commissioning stops, the identity does not.' Two things a porter needs that the spec omits: (a) biome 6 still feeds a Snow region feature marker in terrain_biomes even though the ecology pass always overwrites the layer value, so the branch must survive; (b) the measurement 'a seed 42 size 17 world at phase 8 has zero cells of 1 or 6 in either layer'.

### Missing — minor

**What a pole node's neighbour list does to the exposure and fjord terms**

A pole node's neighbour list is the ENTIRE adjacent ring: n-1 entries (16 at n=17, 32 at n=33, 512 at n=513). So at a pole, `adj` and `exposure` are `count / (n-1)` - an average over the whole ring rather than a local 8-cell window - and `fjord`'s `clamp(count(slope[j]>12 and land)/3)` saturates from any three steep cells anywhere on that ring. Equivalently, the pole node's `coastal_exposure` is the ring's water fraction, and at high n it is effectively a global-ish average while every other cell's is local. The spec states the neighbour structure but never draws out that the same expression means something different at the two pole nodes, which is exactly the kind of thing that reads as a bug in a port.

**child_seed('metals-v1') is recomputed inside the per-node loop**

Source line 108 evaluates `child_seed(cfg.seed,'metals-v1')` for every node: one full SHA-256 of the string 'tectonics-v1:{seed}:metals-v1:0' per node, 289 times at n=17 and 262,144 times at n=513, always returning the same 32-bit value. The C++ must hoist it out of the loop (it is a loop invariant, so this is free and changes nothing). The spec's Performance paragraph accounts for the Dijkstra, the two node passes and the 10M zone transcendentals but not this.

**direction(x, z, n) exact definition**

`if z == 0: return (0., 1., 0.); if z == n-1: return (0., -1., 0.); lon = 2*pi*(x mod (n-1))/(n-1) - pi; lat = pi/2 - pi*z/(n-1)` and then the usual unit vector `(cos(lat)*cos(lon), sin(lat), cos(lat)*sin(lon))`. The explicit pole special-cases matter: every cluster/island `angle()` against a pole row uses exactly (0,+/-1,0), and the `x mod (n-1)` wrap is what makes column n-1 read as column 0. The spec defers entirely to `fwg::direction` existing; the file will not.

**The deferred import that records a circular dependency**

`add_environment` does `from .terrain_humans import allocate_access` as its first statement after the phase gate (line 80), and `shape_islands` is likewise imported lazily by `terrain_tectonics` inside the phase>=2 branch. Both are circular-import breaks: terrain_humans imports from the terrain stack that ends up importing ecology, and tectonics is imported by ecology's own import chain. A C++ port has no such constraint, but the dependency edge itself (ecology -> humans' routing helper, tectonics -> ecology) is worth recording when deciding which translation unit each function lands in.

**The seam-reseal comment in add_environment**

Line 150, immediately before the biome reseal loop: '# Return categorical edits to the duplicate seam and unique poles.' The word 'categorical' is the point - biome is an integer catalogue id, so the seam cannot be fixed by the same `node_grid` scatter used for the eighteen double fields, and the reseal must copy column 0 into column n-1 and broadcast the pole value rather than interpolate. The spec explains the mechanism in D6 but does not carry the comment, and 'categorical' is the reason D6 exists as a separate step at all.
