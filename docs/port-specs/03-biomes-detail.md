# Port specification: 03-biomes-detail

Extracted from the Python `FantasyWorldGenerator` at commit `b67c99b` before that
repository was retired, then audited against the same source by a second reader.

**Source modules**

- `Sim/icarus_sim/terrain_biomes.py`
- `Sim/icarus_sim/terrain_detail.py`

**Estimated C++ size:** roughly 950 lines, excluding tests.

## Audit: IMPLEMENTABLE

The specification is unusually faithful. I walked its Part A and Part B pseudocode against terrain_biomes.py and terrain_detail.py line by line and every formula, constant, threshold, comparison strictness, loop bound, tie-break, row-closure rule, dict key order and emitted string matches the source exactly: the temperature and moisture models, the eight-branch classify ladder in order, marsh_suitable, the truncated 180 m Dijkstra with its lazy-deletion heap and the cand<=180 prune inside the relaxation, source propagation, the landform ternary, the feature kind chain and score chain, the five-kind balanced selection sharing one selected list with the cos(0.24) dot test, the terrain block, the strength map with BIOME_GAIN and its 0.15/0.85 plateau, _cell/_at/height with its three exact-zero early returns, the band scales and amplitudes, and export_height_tile's validation, point canonicalisation and source_sha256 key set. I independently re-derived the reference-world numbers (696.8 m equatorial and 348.4 m meridional pitch at size 17, 135.9 m at z=1, 425.9 m marker separation, the 0.769/0.85 flood cutoffs, the 0.7647/1.0/0.5882 plateau-adjusted gains, 12675 noise calls, 0.33 mm at level 24) and all of them are right. I also verified the thirteen biomes.json rows, ids, colours and the two tombstones against the file: exact. A competent C++ programmer following the algorithm section would produce the same behaviour, so I left implementable true. What it omits is context supplied by neighbouring stages rather than by these two files (the direction() formula, and the fact that water_surface equals ground height on dry land, which is what makes every river seed pass the 0..4 m relief half of the marsh test), plus the 156 variant_names strings its own DATA FILES section implies it is recording. There are six descriptive claims that are wrong, most notably a self-contradiction about when perlin3 runs, an incorrect lake fade distance, and a wrong neighbour count on exactly the polar rows where the 180 m budget is said to bite; none is prescriptive enough to make an implementer build the wrong thing, but they are exactly the kind of confident wrong statement nobody will be able to check once the Python is gone.

## Purpose

Two subsystems that sit between the physical world (height, water, climate) and everything that later stands on it.

`terrain_biomes.add_terrain_labels` is the world's **classification pass**. It turns height, slope, TPI, water type and climate moisture into four published per-cell layers — `temperature` (degrees C), `moisture` (0..1), `biome` (a sparse natural-catalogue ID) and `landform` (0..3) — plus a snapshot layer `natural_biome`, plus a small `terrain` block holding the biome catalogues, the landform names and up to twenty "representative region" map markers. Every downstream system in the generator reads these: settlement scoring, farming, beast and monster habitat, nomads, magic colleges, encounters, and the art/asset catalogues. Its own docstring is the honest statement of what it is: *"Inspectable artistic climate proxies and terrain labels, not weather simulation."* Temperature is a latitude-and-elevation formula, not an energy balance; there are no prevailing winds, no rain shadows and no rainfall inside this module (recipe 3 gets those from the climate pass and hands them in as a layer).

`terrain_detail` is the **continuous surface**. The n x n raster is far too coarse to stand on — cells are hundreds of metres across — so this module publishes a compact, position-addressed definition (`terrain_detail`: a seed and three Perlin bands) and a sampler `HeightField` that evaluates coarse bilinear elevation plus those bands at any unit direction on the sphere. City streets, castle foundations, human-scale patches, the unified world scene and the height-tile export all read heights through this one object, so a building and the ground under it cannot disagree. The detail is masked off over water, rivers, flood-prone ground and near sea level, deliberately conservatively, because the masks it is protecting are themselves coarse.

They are documented together because the detail sampler's amplitude map is keyed on the biome layer the classifier writes.

## Where it sits in the pipeline

**Stage 8 of the 16-stage ladder ("climate, labels, ecology, ground detail"), in this exact order:**

    add_climate(result, cfg)          # terrain_climate  -> climate_moisture, rain_river, ...
    add_terrain_labels(result, cfg)   # THIS MODULE
    add_environment(result, cfg)      # terrain_ecology  -> cold relabel, salinity, coasts
    result['layers']['natural_biome'] = deepcopy(result['layers']['biome'])
    attach_detail(result)             # THIS MODULE (definition only; no sampling)

**Must already exist before `add_terrain_labels` runs:**
- `layers.height` (metres, post-scale), `layers.slope` (degrees), `layers.tpi` (metres) — stages 1-5, re-measured by `measure_surface` at stage 7.
- `effective_config.globe_radius`, `effective_config.sea_level` (metres).
- `result['warnings']` (a list) and `result['timing_ms']` with a `total` key.
- Optional but normally present for recipe 3: `layers.water_type`, `layers.water_surface`, `layers.river` (stage 5 `add_water`); `layers.climate_moisture`, `layers.rain_river` (stage 8 `add_climate`); `result['climate']` (block).

**Runs again later.** `terrain_history.refresh_environment` calls `add_terrain_labels(result, replace(cfg, phase=6))` followed by `add_environment` and a fresh `natural_biome` deep copy. That happens at stage 9 (after leyline networks) and inside `rebuild_tail`, which the two age transitions (stages 14 and 15) and a divine visitation both run. A measured fact recorded in `Sim/tests/test_biome_reachability.py`: at seeds 42 and 0, size 17, a phase-10 world and a phase-16 world agree **cell for cell, 0 differing cells**, across `natural_biome`, `biome`, `temperature`, `moisture`, `slope`, `height` and `water_type`. The re-runs recompute the same answer only because nothing between stages 10 and 15 moves height, water_type or rain_river. If a port makes any later stage write those, the re-label stops being idempotent and every biome-keyed downstream decision moves at an age boundary.

**`attach_detail`** needs only `world['config']['seed']`. It writes a definition; it samples nothing. It is idempotent and is used as an identity check: before an age advance, `terrain_history` rebuilds the expected definition from the world's stored config and refuses the world if `world['terrain_detail']` differs ("Terrain detail identity mismatch; regenerate").

**`HeightField` is constructed by consumers, not by the pipeline:** `city_planner._sampler` (every city), `castle_planner`, `hamlet_planner` (indirectly), `terrain_patch.generate_patch`, `world_scene.build_scene`, and `export_height_tile`. All of those are stage 16 or later, i.e. *after* `terrain_settlements` has written `flood_risk` and after the ecology cold pass has rewritten `natural_biome`. See **ordering_hazards**: constructing one earlier silently yields a different surface.

**What depends on the outputs afterwards:** `terrain_settlements` (site scoring reads `temperature`, `moisture`, `biome`), `terrain_humans` (`farming_potential` reads moisture/temperature), `terrain_nests` / `terrain_nomads` / `terrain_beast_movement` (habitat tables keyed on `natural_biome` ids), `terrain_magic`, `terrain_history.add_biome_variants` (`biome_variant = index(NATURAL_BIOMES[natural_biome[z][x]] + '.' + school)` — a bare dict index that raises `KeyError` on an unregistered id), `terrain_seasons`, `terrain_society`, and the asset/art catalogues.

## Inputs

## terrain_biomes.add_terrain_labels

**Config fields** (`cfg`, the design-space `Config`):
| field | type/range | use |
|---|---|---|
| `shape` | `'plane'` or `'globe'` | gate: returns unchanged unless `'globe'` |
| `tectonics` | 0 or 1 | gate with `phase` |
| `phase` | 1..16 | gate: if `tectonics` and `phase < 2`, return unchanged |
| `size` | int, the grid n | loop bounds |
| `seed` | 0..2^32-1 | `child_seed(seed, 'climate')` for the fallback moisture noise |
| `temperature_offset` | float, -40..40, default 0.0 | additive degrees C on every cell |
| `moisture_bias` | float, -1..1, default 0.0 | additive on the fallback moisture, before clamping |

**`result['effective_config']`** (metres, post-`world_scale`): `sea_level` (float, default 0.0, may be nonzero), `globe_radius` (metres; 1774.4123532462843 m on the reference world, i.e. an 11.15 km circumference).

**Layers read** (all n x n, row-major `[z][x]`, z=0 north pole, z=n-1 south pole, column n-1 duplicating column 0):
| layer | units / range | required | used for |
|---|---|---|---|
| `height` | metres above the reference sphere, post-scale. NOT above sea level. | yes | `h = height - sea_level` |
| `slope` | degrees from the reference-sphere tangent plane (from `measure_globe`, a two-point great-circle gradient at one cell pitch) | yes | `classify`, marsh test, feature kinds |
| `tpi` | metres: this cell's height minus the mean of 8 interpolated samples on a geodesic ring of `effective_tpi_radius_m`. Positive = local high. | yes | landform, mountain score |
| `water_type` | int 0 dry / 1 ocean / 2 lake | optional (defaults to all-zero) | biome override, gates marsh and features |
| `water_surface` | metres, the routed spill level (>= ground) | required *if* `water_type` present | wetland relief |
| `climate_moisture` | 0..1 | optional | **replaces** the analytic moisture entirely when present |
| `rain_river` | int 0/1, a river on the rainfall budget | optional | wetland seed set (preferred) |
| `river` | int 0/1, a river on the uniform-unit-runoff budget | optional | wetland seed set (fallback only) |

**Block read:** `result['climate']` — truthiness only, to choose the `method` string. `result['warnings']` (appended to), `result['timing_ms']` (`classification` written, `total` incremented).

## terrain_detail

`attach_detail` reads exactly one field: `world['config']['seed']` (the **design** config, not `effective_config`).

`HeightField.__init__` reads:
| source | units | required | default if absent |
|---|---|---|---|
| `effective_config.globe_radius` (falls back to `config.globe_radius`) | metres | yes | — |
| `effective_config.sea_level` (same fallback) | metres | no | `0.0` |
| `layers.height` | metres | yes | — (also fixes n = `len(height)`) |
| `world['terrain_detail']` | the definition dict | no | absent -> `height()` degrades to plain bilinear `sample` and tile export refuses |
| `layers.water_type` | 0/1/2 | no | all 0 |
| `layers.river` | 0/1 | no | all 0 |
| `layers.flood_risk` | 0..1 | no | all 0 |
| `layers.natural_biome` | natural catalogue id | no | all 3 (grassland) |

`flood_risk` is written by `terrain_settlements`, not by either module here. Its formula, for the record, is `exp(-d/80) * max(0, 1 - max(0, height - level)/10)` where `d` is the great-circle metre distance to the nearest freshwater node (lake or `rain_river` cell with `salinity < 0.2`) along a Dijkstra that refuses to cross ocean, and `level` is that source node's `water_surface`. Range [0, 1]. 80 m and 10 m are absolute post-scale metres.

`export_height_tile` reads the whole `HeightField` plus `world['generator_version']`.

## Outputs

## Layers written by `add_terrain_labels` (all n x n doubles/ints, seam and poles closed)

| layer | type | range | notes |
|---|---|---|---|
| `temperature` | float | roughly -57..68 C at the config extremes; typically -17..28 | degrees Celsius |
| `moisture` | float | **0..1 inclusive** (clamped) | unitless wetness proxy |
| `biome` | int | one of {0,1,2,3,4,5,6,7,8,13,15,16,17} | a natural-catalogue **ID**, sparse, **never an array offset** |
| `landform` | int | 0..3, index into `LANDFORMS` | 0 Submerged, 1 Plain/hillside, 2 Mountain/ridge, 3 Valley |
| `natural_biome` | int | same domain as `biome` | a per-row copy of `biome`, taken at the end of this pass |

`add_terrain_labels` writes `natural_biome` as a shallow-per-row copy of `biome`. The ecology pass then mutates `biome` in place (cold relabel to 15/16/17) and re-copies, so the value that survives to stage 16 is the **post-cold** one. Nothing in these two modules produces 15, 16 or 17.

## Block written: `result['terrain']` (assigned wholesale, replacing whatever was there)

```
{'version': 6,
 'biomes':          natural_catalogue(),   # 13 rows, list, order is a contract
 'natural_biomes':  natural_catalogue(),   # an equal but distinct list
 'landforms':       ['Submerged','Plain / hillside','Mountain / ridge','Valley'],
 'features':        [...],                 # <= 20 markers, see below
 'climate_seed':    child_seed(cfg.seed,'climate'),   # uint32
 'method':          <one of three strings, see constants>,
 'icons':           'Sparse representative regions and prominent ridges; not unique geological objects.'}
```

A `features` entry — **its key field is `kind`, there is no id**:
```
{'kind':      'marsh'|'mountain'|'forest'|'desert'|'snow',
 'x': int, 'z': int,                       # grid cell
 'direction': (dx,dy,dz),                  # unit vector, the cell's sphere direction
 'height_m': float,                        # layers.height[z][x], ABSOLUTE, not above sea
 'label':     'Wetland margin'|'Prominent ridge'|'Forest region'|'Dry region'|'Snow region'}
```

`natural_catalogue()` row: `{'id': int, 'core': str, 'name': str, 'color': [r,g,b], 'asset_id': 'terrain.biome.%03d'}`. `name` is `core.replace('_',' ').capitalize()` except id 17, which is hard-coded `'Persistent land ice'`. `terrain_history` compares `terrain['biomes']` and `terrain['natural_biomes']` to `natural_catalogue()` for **equality**, so these dicts are world-output bytes: adding a key here is a schema change.

Also: `result['warnings'].append(terrain['method'])`; `result['timing_ms']['classification'] = ms`; `result['timing_ms']['total'] += ms`.

## Block written by `attach_detail`: `world['terrain_detail']`

```
{'version': 1,
 'seed':   child_seed(world['config']['seed'], 'continuous-terrain-v1'),   # uint32
 'bands': [{'scale_m':120.0,'amplitude_m':12.0},
           {'scale_m': 28.0,'amplitude_m': 2.5},
           {'scale_m':  6.0,'amplitude_m': 0.3}],
 'height_reference': 'radial metres above reference sphere; same datum as layers.height',
 'coordinates': 'unit direction (cos(lat)*cos(lon), sin(lat), cos(lat)*sin(lon))',
 'method': 'Coarse elevation plus continuous spherical Perlin bands; interpolated biome '
           'amplitude and water/river/flood protection. Resolved samples are '
           'authoritative; LOD never changes the height function.'}
```
This whole dict is compared for equality at age-advance time, so its key set, its key order in the emitted JSON and its float values are all contract.

## Document returned by `export_height_tile` (not a world block)

```
{'schema':'fantasy-world-generator.height-tile', 'version':1,
 'source_sha256': <hex>, 'generator_version': world['generator_version'],
 'terrain_detail': <the definition dict>,
 'level': L, 'origin_index':[x,z], 'size': cells+1, 'global_cells':[cols,rows],
 'radius_m': radius, 'angular_step_degrees': 180/rows,
 'equatorial_spacing_m': pi*radius/rows,
 'coordinates': 'row-major; latitude=90-180*(origin_z+row)/2^level; '
                'longitude=-180+180*(origin_x+column)/2^level; '
                'globe Cartesian=(radius_m+height_m)*unit_direction',
 'height_reference': <from the definition>,
 'heights_m': [[float]*(cells+1)]*(cells+1)}   # rows north->south, columns west->east
```
`heights_m` are **radial metres above the reference sphere**, the same datum as `layers.height` — not above sea level, not tile-local.

## Algorithm

════════════════════════════════════════════════════════════════════
PART A — `add_terrain_labels(result, cfg)`
════════════════════════════════════════════════════════════════════

**A0. Gate.** `if cfg.shape != 'globe' or (cfg.tectonics and cfg.phase < 2): return result` unchanged. (A non-tectonic globe runs at any phase; that is the recipe-0 geometry-experiment path.)

**A1. Setup.**
```
n    = cfg.size
sea  = effective_config.sea_level
seed = child_seed(cfg.seed, 'climate')                 # uint32
wetland = wetland_access(result, cfg) if 'water_type' in layers else None
water_layer = layers.get('water_type', <n x n of zeros>)
```

**A2. `wetland_access(result, cfg)` — truncated multi-source Dijkstra on the sphere node graph.**

Build `points, _, graph = sphere_grid(n, effective_config.globe_radius)`. The node list is
`[(0,0)] + [(x,z) for z in 1..n-2 for x in 0..n-2] + [(0,n-1)]`; node 0 is the north pole, the last node the south pole; neighbours are the eight surrounding distinct cells (the whole adjacent ring for a pole), each carrying the great-circle metre distance `radius*acos(clamp(dot(dir_i, dir_j)))`, and the neighbour list is built from a **sorted** set of indices.

```
river = layers.get('rain_river', layers.get('river'))   # prefer the rainfall river
distance[i] = +inf for all i;  source[i] = -1;  queue = empty min-heap
for i, (x,z) in enumerate(points):
    if water_type[z][x] != 0 or (river is not None and river[z][x]):
        distance[i] = 0;  source[i] = i;  push (0, i)
while queue:
    d, i = pop_min()
    if d != distance[i]: continue                       # lazy deletion
    for (j, edge_m) in graph[i]:
        cand = d + edge_m
        if cand <= 180 and cand < distance[j]:
            distance[j] = cand;  source[j] = source[i];  push (cand, j)
```
- It is **Dijkstra**, non-negative edges, lazy-deletion heap, **cost = great-circle metres**, **no target** — it terminates when the heap drains. The `cand <= 180` test prunes *inside* the relaxation, so the frontier never leaves the 180 m margin. Tie-break: the Python heap compares `(distance, node_index)`, so equal distances settle the **lower node index first**, and that choice reaches `source[]`, hence `relief`, hence the marsh label.
- **`source` propagates, it is not recomputed**: a node inherits the nearest seed of whichever predecessor relaxed it.

Then, per node:
```
(sx,sz) = points[source[i]] if source[i] >= 0 else (x,z)
relief[i] = height[z][x] - water_surface[sz][sx]
```
Return two grids via `node_grid`: `(distance with +inf mapped to -1, relief)`. `node_grid(values, points, n)` scatters one value per node into an n x n grid, then sets row 0 to `values[0]` repeated, row n-1 to `values[-1]` repeated, and every row's last column to its first (seam closure).

**A3. The per-cell loop.** For `z` in `0..n-1`, for `x` in `0 .. (0 if z is a pole row else n-2)`:

```
p    = direction(x, z, n)                     # unit vector; p[1] = sin(latitude)
h    = height[z][x] - sea                     # metres above sea level
temp = 28 - 45*p[1]**2 - 0.0065*max(0, h) + cfg.temperature_offset
wet  = clamp01( 0.5
              + 0.22*cos(3*asin(p[1]))
              + 0.9*perlin3(p[0]*2.3, p[1]*2.3, p[2]*2.3, seed)
              + cfg.moisture_bias )
if 'climate_moisture' in layers: wet = climate_moisture[z][x]     # total replacement
slope = slope[z][x];  tpi = tpi[z][x]
biome = classify(h, slope, temp, wet)
water = water_layer[z][x]
if   water == 1: biome = 0                    # ocean overrides the classifier
elif water == 2: biome = 8                    # lake  ditto
if wetland and water == 0 and h > 0 and wetland_distance[z][x] >= 0 \
   and marsh_suitable(wet, temp, slope, wetland_distance[z][x], wetland_relief[z][x]):
       biome = 13
form = 0 if h <= 0 else (2 if (h > 30 and tpi > 12) else (3 if tpi < -10 else 1))
```
Note the noise coordinates are the **unit direction scaled by 2.3**, *not* multiplied by the globe radius. The moisture noise therefore has a fixed angular wavelength: the whole planet spans 4.6 lattice units, and changing the world's size or scale does not change the pattern.

`classify(above_sea, slope, temperature, moisture)` — first match wins, order is load-bearing:
```
above_sea <= 0                          -> 0   ocean
temperature <= 0                        -> 6   snow
slope >= 38                             -> 5   exposed_rock
temperature < 5                         -> 1   tundra
moisture < 0.3                          -> 2   desert
temperature >= 20 and moisture >= 0.78  -> 7   rainforest
moisture >= 0.55                        -> 4   forest
otherwise                               -> 3   grassland
```
Snow before rock means a frozen cliff reads snow; rock before tundra means a cold cliff reads rock.

`marsh_suitable(wet, temp, slope, water_distance, height_above_water)`:
```
wet >= 0.55 and temp > 0 and slope < 6
and 0 <= water_distance <= 180 and 0 <= height_above_water <= 4
```

**A4. Feature candidates**, gathered inside the same loop. Only for cells with `h > 0`, `water == 0`, and `z` not a pole row:
```
kind = 'marsh'    if biome == 13
     else 'mountain' if form == 2
     else 'forest'   if biome in (4, 7)
     else 'desert'   if biome == 2
     else 'snow'     if biome == 6
     else None
if kind:
    score = tpi + h*0.1   if kind == 'mountain'
          else wet        if kind == 'forest'
          else 1 - wet    if kind == 'desert'
          else -temp      # marsh AND snow both use -temp
    candidates.append((score, kind, x, z, p))
```
The `kind` chain is ordered: a marsh that is also a ridge is a marsh; a ridge that is also a forest is a ridge.

**A5. Row closure.** After each row, for each of the four fields:
```
stored_row = row * n            if z is a pole row     (one value repeated n times)
           = row + [row[0]]     otherwise              (seam duplicate)
```
Then `layers.update(fields)` publishes `temperature, moisture, biome, landform`.

**A6. Balanced feature selection** — *"Balanced per-kind selection, angular separation also wraps over the seam."*
```
selected = []
for kind in ('marsh','mountain','forest','desert','snow'):      # fixed order
    count = 0
    for (score, k, x, z, p) in sorted(candidates, reverse=True): # full list, re-sorted each kind
        if k != kind: continue
        if any(dot(p, f['direction']) > cos(0.24) for f in selected): continue
        selected.append({'kind':kind, 'x':x, 'z':z, 'direction':p,
                         'height_m': height[z][x], 'label': LABELS[kind]})
        count += 1
        if count == 4: break
```
- The separation test is against **every already-selected feature of every kind**, so a marsh can veto a mountain but never the reverse. That is why the kind order is part of the specification.
- `cos(0.24)` is a dot-product threshold: reject a candidate within **0.24 radians** great-circle of an existing marker. This is an *angle*, so on the ground it is `0.24 * globe_radius` metres — 425.9 m on the 1774.41 m reference world, 7639 m on a 31.8 km one. It "wraps over the seam" for free because it is computed on 3-vectors, never on grid indices.
- At most 4 per kind, 5 kinds, so at most **20** features. (The test asserts `<= 24`.)

**A7. Assemble `result['terrain']`** (see **writes**), overwriting any previous value; then
```
if result.get('climate'):  terrain['version'] = 6;  terrain['method'] = <climate variant>
layers['natural_biome'] = [row[:] for row in layers['biome']]
if wetland:                terrain['method'] += <marsh sentence>
result['warnings'].append(terrain['method'])
result['timing_ms']['classification'] = ms;  result['timing_ms']['total'] += ms
return result
```
The `natural_biome` copy is taken *before* the marsh sentence is appended and *before* the ecology pass runs, so it is the pre-cold labelling. (Stage 8 and `refresh_environment` both re-copy it after ecology; that later copy is the one that survives.)

════════════════════════════════════════════════════════════════════
PART B — `terrain_detail`
════════════════════════════════════════════════════════════════════

**B1. `attach_detail(world)`** — assign the literal definition dict (see **writes**). No sampling, no RNG stream, no layer access. Idempotent.

**B2. `HeightField(world)` construction.**
```
radius = effective_config.globe_radius (else config.globe_radius)
base   = layers.height;  n = len(base)
definition = world.get('terrain_detail')
if definition and definition['version'] != 1: raise ValueError('Unsupported terrain detail version')
sea    = effective_config.sea_level (else config.sea_level, else 0.0)
bands  = [(b.scale_m, b.amplitude_m, child_seed(definition.seed, str(i)))
          for i, b in enumerate(definition.bands)]         if definition else []
water = layer('water_type', 0);  river = layer('river', 0);  flood = layer('flood_risk', 0)
biome = layer('natural_biome', 3)
```
Then the **strength map**, an eagerly-built n x n grid — *"Smooth masks use the same bilinear geography as the coarse elevation."*
```
BIOME_GAIN = {5: 1.4, 3: 0.8, 4: 1.0, 7: 1.0, 15: 0.8}      # default 0.65
strength[z][x] = 0.0  if water[z][x] != 0 else
                 max(0, 1 - min(1, river[z][x]*2))
               * max(0, 1 - min(1, flood[z][x]))
               * BIOME_GAIN.get(biome[z][x], 0.65)
```
`river*2` means any cell whose river value reaches 0.5 is fully protected; the stored layer is 0/1, so **every river cell has strength exactly 0**.

**B3. `_cell(p)` — the index arithmetic, hoisted.** The comment records why it exists: *"height() interpolates three grids of identical shape at the same direction, so the two inverse trigonometric calls and the clamping were being repeated for each of them."*
```
x  = ((atan2(p[2], p[0]) + pi) / (2*pi) * (n-1)) mod (n-1)      # in [0, n-1)
z  = (pi/2 - asin(clamp(p[1], -1, 1))) / pi * (n-1)
z  = clamp(z, 0, n-1)
ix = trunc(x);  iz = trunc(z);  jz = min(n-1, iz+1)
return ix, iz, jz, x-ix, z-iz
```
`ix <= n-2` always (because `x < n-1`), so `ix+1` is a legal column; that is what makes the seam duplicate load-bearing.

**B4. `_at(h, ix, iz, jz, fx, fz)` — bilinear, in exactly this association order:**
```
((1-fx)*h[iz][ix] + fx*h[iz][ix+1]) * (1-fz) + ((1-fx)*h[jz][ix] + fx*h[jz][ix+1]) * fz
```

**B5. `height(p)` — the height function.** Its comment states the design: *"Each factor below can independently zero the detail, and the result is `base` whenever it does. Testing them as they are computed skips the remaining grid samples for water, shoreline and protected cells instead of multiplying by zero after paying for all of them."*
```
if no definition: return sample(base, p)                # plain bilinear, legacy worlds

ix, iz, jz, fx, fz = _cell(p)
base = _at(self.base, ...)

coast = clamp01((base - sea) / 20.0)                    # 20-metre shoreline ramp
if coast == 0: return base                              # at or below sea level

strength = _at(self.strength, ...)
strength = max(0.0, (strength - 0.15) / 0.85)           # zero plateau, then renormalise
if strength == 0: return base
   # "A zero plateau protects entire water cells and makes shores conservative."

wet = max(0.0, 1 - 2*_at(self.water, ...))              # interpolated water_type
strength *= coast*coast*(3 - 2*coast) * wet*wet         # smoothstep(coast) * wet^2
if strength == 0: return base

detail = sum( amplitude * perlin3(p[0]*radius/scale,
                                  p[1]*radius/scale,
                                  p[2]*radius/scale, band_seed)
              for (scale, amplitude, band_seed) in bands )
return base + strength * detail
```
- `coast*coast*(3-2*coast)` is the classic smoothstep; `wet*wet` is a plain square, not a smoothstep.
- The noise argument is `unit_direction * radius / scale_m`, i.e. **arc metres divided by the band's metre scale** — one lattice unit per `scale_m` of ground. The noise is continuous over the whole sphere and does not restart at any tile, city or LOD boundary. Sampling density never changes the function.
- Peak gain: biome 5 (exposed rock) with no river and no flood gives raw 1.4, plateau-adjusted to `(1.4-0.15)/0.85 = 1.4706`. Grassland/boreal 0.8 -> 0.7647; forest/rainforest 1.0 -> 1.0; everything else 0.65 -> 0.5882.

**B6. `export_height_tile(world, level, x, z, cells=64)`.**
```
if any of (level, x, z, cells) is not exactly an int, or not 1 <= level <= 24,
   or not 1 <= cells <= 256:
       raise ValueError('Tile level 1..24, cells 1..256 and integer indices required')
rows = 2**level;  cols = 2*rows
if not (0 <= x < cols) or not (0 <= z < rows) or x+cells > cols or z+cells > rows:
       raise ValueError('Tile lies outside global grid')
field = HeightField(world)
if not field.definition: raise ValueError('World has no canonical terrain detail; regenerate')
```
Sample point for global indices `(i, j)` — *"Canonicalize seam and pole coordinates before evaluating noise."*
```
if j == 0:    return (0.0,  1.0, 0.0)        # north pole, exactly
if j == rows: return (0.0, -1.0, 0.0)        # south pole, exactly
lat = pi*(0.5 - j/rows)
lon = pi*(2*(i mod cols)/cols - 1)
return (cos(lat)*cos(lon), sin(lat), cos(lat)*sin(lon))
```
Output grid: `heights_m[j][i] = field.height(point(x+i, z+j))` for `j` in `0..cells` and `i` in `0..cells` — `cells+1` samples per axis, **including both shared edges**, rows north to south, columns west to east. Because the index arithmetic is exact in binary (`j/rows` with `rows` a power of two) a level-L sample and the matching level-(L+1) sample produce bit-identical directions, so **coarser tiles share identical heights at matching indices of finer tiles**, and the wrap at `i == cols` reproduces column 0 exactly.

`source_sha256` identifies the regional source data so tiles from different generations cannot be mixed:
```
sha256(json.dumps({'base': field.base, 'strength': field.strength, 'water': field.water,
                   'sea': field.sea, 'radius': field.radius, 'detail': field.definition},
                  sort_keys=True, separators=(',',':'), allow_nan=False).encode()).hexdigest()
```
Keys serialise sorted: `base, detail, radius, sea, strength, water`; the nested `detail` dict is sorted too; no spaces; a non-finite float raises rather than hashing.

## Constants

**Temperature model** (`temp = 28 - 45*sin(lat)^2 - 0.0065*max(0,h) + temperature_offset`)
- `28` — equatorial sea-level temperature, C. Raising it warms the whole planet uniformly; identical in effect to `temperature_offset`.
- `45` — pole-to-equator span, C. Gives -17 C at the poles. Lowering it flattens the climate bands and collapses the tundra/snow belt toward the poles; the ecology cold pass then stops firing and biomes 15/16/17 vanish.
- `0.0065` — lapse rate, C per metre (6.5 C/km, Earth's environmental lapse). Applied only to `max(0, h)`, so ocean depth does not warm a cell. Post-scale metres: on a world with `world_scale = 0.177`, a design-space 1400 m peak is 248 m of real relief and costs only 1.6 C, so elevation barely moves a biome on the reference world. Raise `world_scale` and mountains start growing snow caps.
- `temperature_offset` — config, -40..40 C, default 0. Validated in `Config.__post_init__`.

**Moisture model** (analytic fallback only)
- `0.5` base, `0.22 * cos(3*asin(sin_lat))` latitude term, `0.9 * perlin3(p*2.3, seed)` noise term, `+ moisture_bias`, clamped to [0,1].
- `3` in `cos(3*lat)` produces a three-lobe latitude profile: +1 at the equator, 0 at +/-30 degrees, -1 at +/-60, 0 at the poles — a crude Hadley-cell caricature (wet tropics, dry horse latitudes, wet mid-latitudes).
- `2.3` — noise frequency in **unit-sphere coordinates**, i.e. 2.3 lattice units per planet radius. Not scaled by radius, so the blotch pattern is the same shape on every world at every size. Raising it makes moisture patchier.
- `0.9` — noise amplitude. With the 0.22 latitude term, noise dominates latitude nearly 4:1; the clamp bites often.
- `moisture_bias` — config, -1..1, default 0.

**Classifier thresholds** (`classify`)
- `0` C — snow. `38` degrees — exposed rock. `5` C — tundra. `0.3` — desert. `20` C and `0.78` — rainforest. `0.55` — forest. Everything else grassland.
- Moving `38` moves the rock/vegetation line on every mountain in the world; it is compared against `measure_globe` slope, which is a one-cell-pitch gradient and therefore *resolution dependent* — a finer raster reports steeper slopes on the same mountain and grows more exposed rock.
- `0` C and `5` C are the two branches the biome registry tombstones: ids 1 and 6 are emitted here and then almost always overwritten by the ecology cold pass.

**Marsh predicate** (`marsh_suitable`)
- `0.55` moisture floor, `0` C (strictly warmer than freezing), `6` degrees slope ceiling, `180` metres water distance ceiling, `4` metres above the water surface.
- `180` and `4` are **absolute post-scale metres**, tuned for the ~11.15 km reference world. `180` is compared against sphere-graph edge lengths: at size 17 on the reference world the equatorial cell pitch is 696.8 m and the meridional pitch 348.4 m, so **no equatorial relaxation happens at all** — the only cells the search reaches beyond the seeds are in the polar rows, where longitudinal neighbours close to `696.8*cos(78.75 deg) = 135.9 m`. At the default size 129 the pitch is 87.1 m and the margin spans about two rings. Consequence: at coarse rasters, marsh is effectively "a river cell that is wet, unfrozen and gently sloping", because river cells are themselves seeds at distance 0 and are dry land.

**Landform thresholds**
- `30` metres above sea level **and** TPI `> 12` metres -> mountain/ridge; TPI `< -10` metres -> valley. Post-scale metres, compared against post-scale layers. TPI is measured on a ring of `effective_tpi_radius_m = clamp(cfg.radius*world_scale, pi*R/(n-1), pi*R/2)`, so these numbers are only meaningful together with the TPI radius: shrink the ring and TPI magnitudes shrink, and mountains and valleys both disappear.

**Feature selection**
- `0.24` radians minimum angular separation. `cos(0.24) = 0.971338...`. Ground separation is `0.24 * globe_radius`: 425.9 m on the reference world.
- `4` markers per kind, 5 kinds -> 20 maximum.
- `0.1` in the mountain score `tpi + h*0.1` — weights absolute elevation against local prominence at one tenth. Raise it and the tallest peaks win; lower it and the sharpest ridges do.

**Detail bands** (absolute metres, never scaled by `world_scale`)
- `(120 m, 12 m)`, `(28 m, 2.5 m)`, `(6 m, 0.3 m)` — scale is the noise wavelength in ground metres, amplitude is a **noise coefficient, not a guaranteed peak-to-peak height** (the docs say so explicitly). Total coefficient 14.8; `perlin3` rarely exceeds about +/-0.7, so realistic relief is roughly +/-10 m before the strength gain. The test asserts a 300-sample equatorial traverse spans more than 3 m.
- Changing any of these six numbers changes `terrain_detail`, which changes the age-replay identity check and invalidates every saved world.

**Detail masking**
- `20.0` metres — the shoreline ramp: detail fades in linearly from sea level to 20 m above it, then through a smoothstep. Below sea level there is none at all.
- `0.15` / `0.85` — the strength plateau: raw strength at or below 0.15 gives exactly zero detail, and what remains is renormalised so that a raw 1.0 stays 1.0. *"A zero plateau protects entire water cells and makes shores conservative."* Practically it means a cell with default biome gain 0.65 is fully protected once `flood_risk > 0.769`, and a forest cell (gain 1.0) once `flood_risk > 0.85`.
- `2` in `river*2` — any river fraction at or above 0.5 zeroes the cell.
- `1 - 2*water` then squared — an interpolated `water_type` of 0.5 or more zeroes the detail, so the fade reaches half a cell inland from any ocean or lake.
- `BIOME_GAIN = {5: 1.4, 3: 0.8, 4: 1.0, 7: 1.0, 15: 0.8}`, default `0.65` — exposed rock is roughest, forest and rainforest nominal, grassland and boreal forest gentler, everything else (desert, marsh, cold tundra, land ice, and the water biomes that are masked anyway) gentlest. *"Plains receive gentler relief than exposed rock."*

**Tile export**
- `level` 1..24, `cells` 1..256; `rows = 2^level`, `cols = 2*rows`. Level 24 means 33.5 million longitude cells; on the reference world that is 0.33 mm of equatorial spacing, which is why the ceiling is where it is.
- `180/rows` degrees is simultaneously the latitude and the longitude step, because `cols = 2*rows`.

**Seed domains** (strings, part of the contract)
- `'climate'` — the fallback moisture noise field.
- `'continuous-terrain-v1'` — the detail definition seed.
- `'0'`, `'1'`, `'2'` — the three band seeds, derived from the definition seed.

## Data files

**`Sim/icarus_sim/biomes.json`** — loaded once at import by `terrain_biome_catalogue._registry()`. This is the only data file either module depends on.

Shape:
```
{"schema_version": 1,
 "note": "<the ordering contract, quoted below>",
 "natural_biomes": [ {"id": int, "core": str, "color": [r,g,b],
                      "reachable": bool?, "unreachable_note": str?}, ... ],
 "variant_names": { "<school>": [ 13 display names, one per natural biome, IN ORDER ], ... }}
```

The thirteen rows, in file order (position is a contract; the ids are sparse and are **not** offsets):

| pos | id | core | color | reachable |
|---|---|---|---|---|
| 0 | 0 | ocean | 42,102,147 | yes |
| 1 | 1 | tundra | 156,164,126 | **tombstoned** |
| 2 | 2 | desert | 218,184,120 | yes |
| 3 | 3 | grassland | 139,176,99 | yes |
| 4 | 4 | forest | 65,135,80 | yes |
| 5 | 5 | exposed_rock | 145,143,139 | yes |
| 6 | 6 | snow | 230,240,241 | **tombstoned** |
| 7 | 7 | rainforest | 22,83,58 | yes |
| 8 | 8 | lake | 68,160,185 | yes |
| 9 | 13 | marsh | 105,135,101 | yes |
| 10 | 15 | boreal_forest | 56,104,95 | yes |
| 11 | 16 | cold_tundra | 152,164,136 | yes |
| 12 | 17 | land_ice | 223,240,245 | yes |

`variant_names` has twelve schools, each with exactly thirteen names matched **by position** to the rows above (e.g. `weave[2] = "Glass mirages"` is `desert.weave`). Neither module in this spec consumes `variant_names`; `biome_catalogue()` does, and it is what `biome_variant` indexes.

Loader validation (fail closed — *"a registry that has lost a school or a name is not a smaller catalogue"*):
- `schema_version` must be exactly 1.
- Duplicate `id` -> error.
- Unknown keys (anything outside `{id, core, color, reachable, unreachable_note}`) -> error, naming the offenders.
- `reachable` must be a bool if present; `reachable: false` without an `unreachable_note` -> error.
- Tombstoning *every* row -> error.
- `set(variant_names) != set(SCHOOLS)` -> error; any school whose name list is not exactly `len(rows)` long -> error.

Derived tables: `NATURAL_BIOMES = {id: core}` (insertion-ordered, file order), `NATURAL_COLORS = {id: [r,g,b]}`, `UNREACHABLE_BIOMES = frozenset({1, 6})`.

**The tombstone rule, verbatim from the file's own note:** a `"reachable": false` row *"is a TOMBSTONE, not a deletion - the id, the row and its position survive, the classifier keeps emitting the label, and only asset commissioning stops."* `natural_catalogue()` is deliberately unchanged by tombstones and still returns all thirteen ordered rows, because `terrain_history` compares it for equality against world output bytes; `reachable_natural_catalogue()` is the filtered view for anything that commissions art.

**Why 1 and 6 are tombstoned** (recorded in the file, user ruling 0.2, 2026-09-20 — this reasoning exists nowhere else): `classify` assigns 1 below 5 C and 6 at or below 0 C, and the ecology cold pass relabels essentially every such cell to 15, 16 or 17. For **6 snow** the relabel is total: `cold_habitat` returns `None` only when a seventh month exceeds 5 C, and the seventh-largest month is `mean + amplitude*6.12e-17`, which cannot exceed 5 when the mean is at or below 0. For **1 tundra** there is a floating-point measure-zero survival band: `cos(2*pi*(3-6)/12)` and `cos(2*pi*(9-6)/12)` both evaluate to `+6.123233995736766e-17` rather than 0, so a mean temperature within about 2.2e-15 C below 5.0 C at large seasonal amplitude gets a seventh warm month and the label survives. No generated world has shown it. `classify` keeps its snow branch because `add_terrain_labels` derives the *Snow region* feature marker from biome 6, and removing the branch would silently empty world output of those markers.

`terrain_detail` loads no data file; its bands are literals.

## Random streams

**Neither module draws from a sequential random stream.** There is no `random.Random` anywhere in either file. All randomness is position-addressed gradient noise, which is why both are exactly reproducible at any sampling density and in any order.

**`add_terrain_labels`**
- One noise field: `seed = child_seed(cfg.seed, 'climate')`, i.e. the first four bytes of `sha256("tectonics-v1:<seed>:climate:0")` read big-endian, as a uint32. Published as `terrain.climate_seed`.
- `perlin3(p[0]*2.3, p[1]*2.3, p[2]*2.3, seed)` is called **once per distinct cell**, i.e. `(n-2)*(n-1) + 2` times, and **only when `climate_moisture` is absent from the layers**. On recipe 3 that layer is always present, so on a real world this noise field is evaluated zero times and `climate_seed` is published but unused. It is live for recipe-0 geometry-experiment worlds and for globe worlds run below phase 6.
- Draw count is therefore data-dependent in a coarse way (present/absent), never per-cell-branch-dependent.

**`terrain_detail`**
- `attach_detail` computes one value: `child_seed(world['config']['seed'], 'continuous-terrain-v1')`, a uint32, stored in the definition.
- `HeightField` derives three band seeds by re-applying the same derivation with the definition seed as master and the band index as a **decimal string domain**: `child_seed(definition['seed'], '0')`, `child_seed(definition['seed'], '1')`, `child_seed(definition['seed'], '2')` — i.e. `sha256("tectonics-v1:<detail_seed>:0:0")[0:4]` and so on. Note the master here is itself a hash output, not the world seed.
- `height()` calls `perlin3` **three times per evaluation, or zero times** — the three early returns (below sea level, protected strength, interpolated water) skip the noise entirely. So the noise call count *is* data-dependent, but no sequential stream is consumed, so skipping a call cannot perturb any other sample. That is the whole point of a position-addressed field and it is what makes `export_height_tile` able to guarantee that coarse and fine tiles agree at shared indices.
- `export_height_tile` evaluates at most `3 * (cells+1)^2` noise calls (default `cells = 64` -> 12 675).

**For the C++ port:** both uses map onto `fwg::child_seed(master, domain, variation, "tectonics-v1")` and `fwg::perlin3(x, y, z, seed)` with no new derivation. Do **not** invent a fourth seed derivation for the bands — the string domains `"0"`, `"1"`, `"2"` are what the existing `child_seed` signature already takes.

## Ordering hazards

**1. `fields = {k: [] for k in ('temperature','moisture','biome','landform')}` then `zip(fields, (temp, wet, biome, form))`.** The pairing of values to layer names is carried entirely by Python dict insertion order. A C++ port must use an explicit ordered array of four names; a `std::map` would reorder them to `biome, landform, moisture, temperature` and silently write temperature into the biome layer.

**2. `sorted(candidates, reverse=True)` on tuples `(score, kind, x, z, direction)`.** Ties in `score` fall through to a **descending string compare on `kind`**, then descending `x`, then descending `z`, then a lexicographic compare of the direction 3-tuple. Scores tie routinely — `-temp` for marsh and snow, `1-wet` for desert, and `wet` for forest all come from quantised layers. Reproduce the full key, descending, or the marker set moves.

**3. The five-kind loop shares one `selected` list.** Kinds are processed in the fixed order `marsh, mountain, forest, desert, snow`, and the 0.24-radian separation test runs against everything already selected *of any kind*. Earlier kinds veto later ones. Reordering that tuple changes which markers a world gets.

**4. `sorted(candidates, reverse=True)` is re-evaluated inside the kind loop** — the same list is sorted five times. Functionally identical to hoisting it (Python's sort is stable and the list is not mutated), but a port that hoists must keep the identical comparison key.

**5. `heapq` tie-break in `wetland_access`.** Entries are `(distance, node_index)` and equal distances pop the **lower index first**. The winning pop sets `source[j]`, which selects the `water_surface` cell used for `relief`, which decides the `0 <= height_above_water <= 4` half of the marsh test. `std::priority_queue<std::pair<double,int>, ..., std::greater<>>` gives the same ordering; a queue keyed on distance alone does not.

**6. `sphere_grid` neighbour lists are built from `sorted(ids)`** (`terrain_erosion.py:30`). That fixes the relaxation order within one pop and therefore which of two equidistant seeds wins a tie. The C++ `fwg::sphere_grid` already sorts; keep it.

**7. `NATURAL_BIOMES` is a dict built by iterating `biomes.json` rows in file order**, and `natural_catalogue()` iterates it. The emitted list order is a published contract, compared for equality by `terrain_history`. Use a vector, never a `std::map<int, ...>` — the ids 0..8, 13, 15..17 happen to be ascending, so a map would coincidentally agree today and diverge the moment a row is appended out of order.

**8. `biome_catalogue()` positional contract** (consumed by `add_biome_variants`, not by these modules, but it constrains `biomes.json`): `biome_variant` stores a flat index into `cores x schools`, known schools first then hidden. Reordering, inserting or deleting a `natural_biomes` row renumbers every variant in every saved world. Append only.

**9. `result['terrain']` is assigned wholesale.** A re-label drops `magical_biomes` and `biome_contract`, which `add_biome_variants` put there. In the real pipeline `rebuild_tail` calls `refresh_environment` and then immediately `add_biome_variants`, so the gap closes — but anything reading `result['terrain']` between those two calls sees a block missing two keys.

**10. `HeightField` construction time is observable.** `flood_risk` does not exist until `terrain_settlements` runs (stage 10+) and `natural_biome` carries the cold relabel (15/16/17) only after the ecology pass. Build a `HeightField` on a stage-8 world and `flood_risk` defaults to 0 (more detail) and `natural_biome` still holds 1/6 (gain 0.65 instead of 0.8 for what became boreal forest). The same world at two phases yields a different surface. The pipeline never does this; a tool or a test can.

**11. `layers.get('water_type', [[0]*n]*n)`** builds a list of `n` references to **one** row object. It is only ever read, so it is harmless — but a port that translates it literally into a shared-row structure and then writes through it would corrupt the whole grid.

**12. Pole rows are computed once and replicated.** `row * n` for `z in (0, n-1)`; every other row gets `row + [row[0]]`. Break that and the seam column stops matching column 0, which `_cell`'s `ix+1` access depends on.

**13. `node_grid` takes the pole values from `values[0]` and `values[-1]`** — the first and last entries of the node list, which are by construction the north and south pole nodes. It does not search for them.

## Edge cases

**Gates and degenerate configs**
- `add_terrain_labels` returns the world untouched for `shape != 'globe'`, and for a tectonic globe below phase 2. No layers written, no `terrain` block, no warning, no timing entry.
- With `water_type` absent (recipe-0 worlds, or globe worlds below phase 5): `wetland = None`, `water_layer` is all zeros, so no ocean/lake/marsh override ever fires, and no marsh sentence is appended to `method`. Biome 8 (lake) is then **unreachable** — it is produced only by the `water_type == 2` override, never by `classify`.
- With `climate_moisture` absent, moisture comes from the analytic noise; with it present, the analytic expression is computed and **immediately discarded** (a wasted `perlin3` call per cell, harmless).

**Poles and the seam**
- `direction(x, 0, n) = (0,1,0)` and `direction(x, n-1, n) = (0,-1,0)` exactly, so `p[1]**2 = 1` and pole temperature is exactly `28 - 45 - lapse + offset`.
- Pole cells are excluded from feature candidates (`z not in (0, n-1)`), so no marker ever sits on a pole. They are *not* excluded from the biome/landform/temperature/moisture layers.
- `asin(clamp(p[1], -1, 1))` guards the pole case in both `_cell` and `sample`; without the clamp, a direction whose `y` rounds to 1.0000000000000002 raises.

**`wetland_access`**
- Unreached nodes keep `distance = +inf`, published as **-1** (a sentinel, not a distance). The marsh test guards on `wetland_distance[z][x] >= 0` before calling `marsh_suitable`, so the -1 never reaches the predicate — but `marsh_suitable` also rejects negative distances itself, belt and braces.
- Unreached nodes have `source = -1`, and `relief` then falls back to `height[z][x] - water_surface[z][x]` at the node's own cell. That value is meaningless but is never consulted, because the caller has already failed the `>= 0` distance test.
- If no cell is water and no river exists, the queue starts empty, every distance is -1, and marsh is impossible. The Dijkstra still costs one full pass over the node list to discover that.
- At coarse rasters the 180 m budget is smaller than the cell pitch everywhere except the polar rows, so `wetland_access` reduces to "distance 0 on seeds, -1 elsewhere" over most of the globe. See **constants**.

**`classify`**
- `above_sea <= 0` means a cell exactly at sea level is ocean, before any other test. Combined with `h > 0` on the marsh and feature paths, sea-level cells are land for no purpose.
- The `temperature <= 0` and `temperature < 5` branches produce the two tombstoned ids. They must stay: removing the `<= 0` branch would silently empty world output of *Snow region* markers.

**Feature selection**
- A kind with no candidates contributes nothing; `features` can legitimately be empty (an all-ocean world, or a world below the marsh/mountain/forest/desert/snow thresholds everywhere).
- The separation test rejects on `dot > cos(0.24)`, a strict inequality, so two markers exactly 0.24 rad apart are both allowed.
- A world of exactly one land cell yields at most one marker.

**`HeightField`**
- No `terrain_detail` -> `height()` is plain bilinear `sample(base, p)` and `export_height_tile` raises `'World has no canonical terrain detail; regenerate'`. This is the legacy-world path.
- `terrain_detail['version'] != 1` -> `ValueError('Unsupported terrain detail version')` at construction.
- Missing mask layers default generously: water 0, river 0, flood 0, biome 3. The defaults *increase* detail, so a missing layer never protects.
- `_cell` clamps `z` to `[0, n-1]`; when `z == n-1` exactly (the south pole direction), `jz == iz == n-1` and `fz == 0`, so the bilinear reduces to the pole row, which is one repeated value — correct by construction.
- `x` is taken modulo `n-1`, so `ix <= n-2` and `ix+1 <= n-1` is always in range. This is the seam-duplicate column doing real work.
- The three `if not <factor>` early returns test a float against falsiness; they fire on exactly `0.0` (and `-0.0`), never on a small positive value. A port must use `== 0.0`, not an epsilon, or shore cells will snap flat.
- `strength` is built for all `n*n` cells in the constructor — O(n^2) work and memory on every `HeightField(world)`. `city_planner._sampler` constructs one per city; `world_scene.build_scene` and `terrain_patch` one each.

**`export_height_tile`**
- Type check is `type(v) is not int`, which **rejects `bool`** (`type(True) is bool`). `export_height_tile(w, 8, True, 0)` raises rather than silently meaning index 1.
- `x + cells <= cols` and `z + cells <= rows` are inclusive bounds, so the last column may be `i == cols`, which wraps via `i % cols` to column 0 — the documented seam behaviour, verified by a test comparing tile `(8, 0, 100, 1)` west edge with tile `(8, 511, 100, 1)` east edge. Likewise `j == rows` lands exactly on the south pole.
- `allow_nan=False` in the source hash: a non-finite value anywhere in `height`, `strength`, `water`, `sea` or `radius` raises instead of producing a hash.
- `world['generator_version']` is read with `.get`, so it may be `None` in the emitted document.

## Reasoning worth preserving

Quoted directly; each of these records a decision or a measurement that exists nowhere else.

**The module's own statement of scope** — `terrain_biomes.py` line 1: *"Inspectable artistic climate proxies and terrain labels, not weather simulation."*

**The published method string, which is also the world's warning** (no-climate form): *"Temperature: latitude and elevation proxy. Moisture: latitude and seeded spatial noise. No prevailing winds, rainfall or rain shadows simulated."*

**With climate present** the string is replaced, and the replacement is careful about what did *not* change: *"Temperature remains a latitude/elevation proxy. Biome moisture now follows wind-transport rainfall; lake extents still assume equilibrium filling."*

**The marsh sentence, appended only when a wetland pass ran:** *"Marsh is wet, unfrozen, gently sloping land within 180 m and 4 m above mapped water; no soil saturation simulation."*

**On the markers, so nobody mistakes them for geology:** *"Sparse representative regions and prominent ridges; not unique geological objects."*

**On the selection loop** — line 79: *"Balanced per-kind selection, angular separation also wraps over the seam."* The second clause explains why the test is a dot product of 3-vectors and not a grid-index distance.

**`wetland_access` docstring:** *"Nearest mapped water on the sphere, limited to a 180 metre wetland margin."*

**`terrain_detail.py` line 1:** *"Canonical, position-addressed local relief and resolved engine-neutral tiles."*

**The definition's own `method` field, which every consumer sees:** *"Coarse elevation plus continuous spherical Perlin bands; interpolated biome amplitude and water/river/flood protection. Resolved samples are authoritative; LOD never changes the height function."*

**On the strength map** — line 39: *"Smooth masks use the same bilinear geography as the coarse elevation."* The masks are interpolated with the same `_at` used for height precisely so a mask edge and a terrain edge cannot fall in different places.

**`_cell` docstring, a recorded optimisation:** *"The index arithmetic terrain_globe.sample performs, computed once. height() interpolates three grids of identical shape at the same direction, so the two inverse trigonometric calls and the clamping were being repeated for each of them."*

**On the early returns in `height()`:** *"Each factor below can independently zero the detail, and the result is `base` whenever it does. Testing them as they are computed skips the remaining grid samples for water, shoreline and protected cells instead of multiplying by zero after paying for all of them."*

**On the 0.15 plateau:** *"A zero plateau protects entire water cells and makes shores conservative."*

**On tile sampling:** *"Indexed latitude/longitude samples; shared indices yield identical heights."* And inside `point`: *"Canonicalize seam and pole coordinates before evaluating noise."*

**From `docs/continuous-terrain.md`, on what the amplitudes are:** *"Amplitudes are noise coefficients, not guaranteed peak-to-peak heights. Plains receive gentler relief than exposed rock. Biome strengths interpolate continuously. Noise does not restart at city or tile boundaries, and sample density never changes the height function."*

**Same doc, on the masking policy:** *"Water, river and flood masks suppress detail; near sea level a smooth 20-metre elevation ramp protects coastlines. This is intentionally conservative around coarse water samples. Regional hydrology is not rerun at metre scale; local depressions are not simulated ponds, and fine streams are not invented."*

**Same doc, on the boundary between regional and local:** *"Regional roads and founding still use regional geography; city streets use detailed elevations and local grades."* And: *"The engine receives resolved numbers and need not implement noise."*

**From `biomes.json`, the ordering contract** (the most expensive sentence in the data file): *"ORDER IS A CONTRACT: VARIANT_NAMES entries are matched to natural_biomes BY POSITION, and biome_variant in a saved world stores a flat index into the natural_biomes x schools cross product. Reordering, inserting or deleting a natural_biomes row renumbers every variant in every saved world. Append only."*

**From `biomes.json`, on tombstones:** *"A row may carry \"reachable\": false; that is a TOMBSTONE, not a deletion - the id, the row and its position survive, the classifier keeps emitting the label, and only asset commissioning stops."*

**From `biomes.json`, the measured reason biome 6 keeps its branch:** *"classify() keeps its branch because terrain_biomes.py derives the Snow region feature marker from biome 6, and removing it would silently empty world output of those markers."*

**From `biomes.json`, the floating-point survival band for biome 1:** *"cos(2*pi*(3-6)/12) and cos(2*pi*(9-6)/12) both evaluate to +6.123233995736766e-17 rather than 0, so a mean temperature within about 2.2e-15 C below 5.0 C at large seasonal amplitude gets a seventh warm month, cold_habitat returns None and the label survives. No generated world has shown it: a seed 42 size 17 world at phase 8 has zero cells of 1 or 6 in either layer."*

**From `terrain_biome_catalogue._registry`:** *"Fail closed: a registry that has lost a school or a name is not a smaller catalogue."*

**From `reachable_natural_catalogue`, on why the tombstone filter is a separate function:** *"natural_catalogue() itself is deliberately unchanged, and so are the dicts it emits: terrain_history.py compares world['terrain']['biomes'] and ['natural_biomes'] against it for equality, so those dicts are world-output bytes and a key added here would be a key in every saved world and a schema change... A tombstone is a statement about commissioning, not about the contract."*

**From `Sim/tests/test_biome_reachability.py`, the measurement that lets the witnesses run cheaply — and its falsification condition:** *"MEASURED on this tree, in one process, at seeds 42 and 0 at size 17: a phase-10 world and a phase-16 world agree cell for cell across natural_biome, biome, temperature, moisture, slope, height and water_type -- 0 differing cells in all seven layers, at 0.36 s against 77 s. FALSIFICATION: if a future change writes height, water_type or rain_river after stage 9, that equality dies silently."*

**Same test, on how to treat a failure:** *"A biome that these tests report as unreachable is a finding to re-measure and to record on the board. It is never a reason to weaken an assertion here."*

**Same test, on why lake cannot be swept out of the classifier:** *"`8 lake` is NOT produced by `classify`. `add_terrain_labels` overrides the classified label with 8 wherever `water_type == 2`. It therefore cannot appear in a sweep of the classifier, and asserting it there would be asserting a literal."*

**From `city_planner`, on the two things that were once one key** — worth carrying because it disambiguates a word this codebase overloads: *"Two different things used to share one `flood` key, and a caller could not tell which it had read. `channel` is local: this point is inside the reserved river channel and setback, measured in metres from a routed centerline. `flood_risk` is the regional proxy, one value for a raster cell thousands of metres across -- far larger than a city, so it is the same number everywhere in this footprint and can only ever block all of it or none of it."*

**From `hamlet_planner`, the same caution stated as a limit:** *"Coarse flood_risk is a regional layer, not a local inundation mask; coasts would otherwise be empty."*

## Notes for the C++ implementation

**Read first:** `include/fwg/grid.hpp` (Grid, `direction`, `distinct_count`, `fill_row`, `sample`, `SphereGrid`, `sphere_grid`), `include/fwg/noise.hpp` (`perlin3`), `include/fwg/random.hpp` (`child_seed`, and the block comment forbidding a fourth seed derivation), `include/fwg/world.hpp` (World, layers map), `include/fwg/water.hpp` (`WaterType`).

**Layer storage.** `temperature`, `moisture` and `landform` slot straight into `World::layers` as `Grid` (doubles). `biome` and `natural_biome` are *integers* stored in a double grid — fine, the ids are small, but every read must round or cast explicitly. If the port adds an integer layer type, biome and landform are the two that want it. Use `Grid::fill_row(z, values)` for the per-row closure; it already does the `row*n` / `row+[row[0]]` split via `distinct_count`.

**The `terrain` block and `terrain_detail` are not layers.** They want typed structs on `World`:
```cpp
struct BiomeRow { int id; std::string core, name; std::array<int,3> color; std::string asset_id; };
struct TerrainFeature { std::string kind; int x, z; Vec3 direction; double height_m; std::string label; };
struct TerrainBlock { int version = 6; std::vector<BiomeRow> biomes, natural_biomes;
                      std::vector<std::string> landforms; std::vector<TerrainFeature> features;
                      std::uint32_t climate_seed; std::string method, icons; };
struct DetailBand  { double scale_m, amplitude_m; };
struct TerrainDetail { int version = 1; std::uint32_t seed; std::array<DetailBand,3> bands;
                       std::string height_reference, coordinates, method; };
```
`biomes` and `natural_biomes` are equal lists in Python but distinct objects; a port can share one vector safely since nothing mutates them, but the JSON writer must emit both keys.

**`biomes.json`.** The library has no dependencies, so either embed the thirteen rows and twelve x thirteen variant names as a `constexpr`/static table in a translation unit, or write a tiny reader for this one file using the existing JSON support. Embedding is the honest choice for the classifier (this spec carries the whole table), but the variant names are 156 strings that `biome_catalogue()` needs at stage 10; keep them in one place. **Preserve file order in a `std::vector`, never a `std::map<int, ...>`** — see ordering_hazards #7.

**No direct C++ equivalents, and what to use instead:**

| Python | C++ |
|---|---|
| `heapq` with lazy deletion | `std::priority_queue<std::pair<double,int>, std::vector<...>, std::greater<>>`; keep the `if (d != dist[i]) continue;` guard. Tie-break on the int is automatic with `pair` and matches Python. |
| `sorted(candidates, reverse=True)` on a heterogeneous tuple | a `struct Candidate { double score; std::string kind; int x, z; Vec3 dir; }` with an explicit `operator<` comparing score, then kind (`std::string` compare = Python's), then x, then z, then dir componentwise; sort with `std::sort` and a `>` comparator, or `std::sort` ascending and iterate in reverse. Python's sort is stable but the full key makes every comparison total, so stability is not load-bearing here. |
| `{k: [] for k in (...)}` + `zip(fields, values)` | a `static constexpr std::array<const char*,4> kFieldNames` and four parallel `std::vector<double>`. Do not use a `std::map`. |
| `math.inf` sentinel then `-1` on output | `std::numeric_limits<double>::infinity()`, mapped to `-1.0` when scattering to the grid. |
| `layers.get(key, default_grid)` | a small helper returning `const Grid*` or a lazily-built constant `Grid`; do **not** materialise an n x n default grid per lookup in a hot path. |
| `[row[:] for row in grid]` | `Grid` copy assignment. |
| `perf_counter()` | `std::chrono::steady_clock`; write into `World::timing_ms`. |

**`sphere_grid`.** `fwg::sphere_grid(n, radius)` returns a `shared_ptr<const SphereGrid>` with `point_x`/`point_z` as parallel vectors rather than a vector of pairs. `wetland_access` needs the node -> (x,z) mapping (for `source` -> `water_surface` lookup) and `SphereGrid::to_grid` for the scatter — `to_grid` already does exactly what `node_grid` does, including the pole rows from `values.front()`/`values.back()` and the seam column. **Hold the `shared_ptr`**, per the header's warning about the evicting cache.

**`_cell` / `_at`.** `fwg::sample` already implements the same arithmetic for one grid. Add a `struct CellRef { int ix, iz, jz; double fx, fz; }` plus `CellRef locate(int n, const Vec3& p)` and `double bilinear(const Grid&, const CellRef&)`, and have `sample` call them so the two cannot drift — the existing header already makes that argument about `band_wedge_area`.

**Float semantics.** This is a clean rewrite and numbers need not match, so `pow(f,3)` vs `f*f*f` and the like do not matter. Two things still do:
- `int(x)` in `_cell` is truncation, but `x` and `z` are non-negative there, so `static_cast<int>` is correct; do not "fix" it to `std::floor` on a signed value and then change the guard.
- The `if not coast` / `if not strength` tests are exact zero comparisons. Write `if (coast == 0.0) return base;`. An epsilon here visibly flattens shorelines.

**`export_height_tile`'s `source_sha256`.** `fwg::sha256` exists and the library has its own JSON writer. Reproducing Python's exact bytes (shortest round-trip float repr, sorted keys, `(',',':')` separators) is not required for a clean rewrite, but the *structure* is: hash over exactly `{base, detail, radius, sea, strength, water}`, keys sorted, compact, and refuse non-finite values. Document in the C++ that the digest is this library's own and is not comparable with a Python-generated tile.

**Cost.** `HeightField` builds an n x n strength grid per construction and the planners build one per city. In C++ make it a `shared_ptr<const HeightField>` cached on the World, or at minimum construct it once per planner pass. `height()` is called millions of times by the city planners (four calls per finite-difference slope), so `locate`/`bilinear` should be header-inline.

**Stage placement.** These are stage 8. `kHighestImplementedStage` is 5 today; `add_terrain_labels` needs stages 6-7 (`tectonic_transition`, `carve_relics`, `measure_surface`) and the climate pass in front of it for `climate_moisture` and `rain_river`. The analytic-moisture fallback path is implementable *now* on a stage-5 world and is a reasonable first landing: it produces `temperature`, `moisture`, `biome`, `landform` and the marker set from height, slope, tpi and water alone. `attach_detail` and `HeightField` have no such dependency at all and can land immediately — `HeightField` degrades cleanly when `flood_risk` and `natural_biome` are absent.

**Word-collision warnings for this subsystem** (the codebase overloads several):
- **`tier`** does not appear here, but **`kind`** does, and it is the feature marker's *key field* — unrelated to `WaterType`, to `cold_habitat`'s `'boreal'|'tundra'|'ice_cap'`, and to the nest/creature `kind`.
- **`marsh`** is a biome id (13), a feature `kind`, and a label string ('Wetland margin'). They are three different things with three different value spaces.
- **`river`**: `layers.river` (uniform-unit-runoff, from `add_water`) and `layers.rain_river` (rainfall budget, from `add_climate`) are different layers. `wetland_access` prefers `rain_river`; `HeightField` reads **only `river`**. That asymmetry is real, not a typo, and a port that unifies them changes both the marsh belt and the detail mask.
- **`flood_risk`** is regional (one value per raster cell, kilometres across) and is **not** a local inundation mask; `city_planner` keeps a separate `channel` field for the local sense.
- **`height`** in `layers.height`, in `feature['height_m']` and in `heights_m` all mean *radial metres above the reference sphere*, never above sea level. `h` inside `add_terrain_labels` is the other thing — `height - sea_level`.

## Open questions

**1. `docs/continuous-terrain.md` documents a continuous surface-label sampler that does not exist in the code.** Its "Surface labels" section describes a function that evaluates `classify` from continuous height, analytic temperature and interpolated climate moisture, then applies the marsh and cold-habitat overrides, so that *"A surface sample reports the natural biome at the point asked for, not the label of the raster cell it falls in."* I grepped the whole `Sim/` tree: the only callers of `classify` are `add_terrain_labels` itself, and the only callers of `cold_habitat` are `terrain_ecology.add_environment` and `terrain_society`. There is no such sampler. `city_planner._sampler` and `world_scene` both read the biome by **rounding to the nearest raster cell** (`round(...)`, not `int(...)`), which is exactly the cell-addressed behaviour that section says was replaced. **Decision needed:** port the code as it is (cell-addressed labels, continuous heights) and correct the document, or implement the sampler the document promises. I recommend the former — the document is the outlier — but a consumer may have been written against the promise.

**2. The 180 m wetland margin is smaller than a cell over most of the globe at production rasters.** At size 17 on the reference world (equatorial pitch 696.8 m, meridional 348.4 m) the Dijkstra relaxes **only in the polar rows**, where longitudinal neighbours close below 180 m. Everywhere else the margin is exactly the seed set, so marsh reduces to "a `rain_river` cell that is wet, unfrozen and under 6 degrees of slope". At size 129 (pitch 87.1 m) it spans about two rings and behaves as intended. So the marsh belt's *width* is resolution-dependent in a way nothing documents, and the whole `wetland_access` Dijkstra is dead weight at size 17 outside the poles. **Decision needed:** is 180 m meant to be an absolute metre budget (in which case it should probably scale with `world_scale` like the other reaches, or be stated as "the seed cells only, at coarse rasters"), or was it authored against a finer raster than the lab uses? I did not find any record either way.

**3. `terrain.climate_seed` is published but unused on every recipe-3 world.** `climate_moisture` is always present after `add_climate`, so the analytic moisture — and its `perlin3` call — is computed and discarded on every cell of every real world. The seed still goes into world output. Harmless, but a porter should know the field is vestigial for recipe 3 and that the noise path exists for recipe-0 worlds only.

**4. Marsh and snow share the feature score `-temp`.** Reading the chain literally, `score = ... else -temp` catches both `'marsh'` and `'snow'`, which is plausible for snow (coldest wins) and odd for marsh (coldest wetland wins, rather than wettest or lowest). It may be intentional shorthand or may be an unnoticed fall-through. Nothing documents it. It only affects which four markers of each kind are chosen, never a layer.

**5. `add_terrain_labels` sorts the full candidate list five times.** Functionally a no-op (the list is not mutated between sorts) but it is O(5 n^2 log n) on a pass that is otherwise linear. I am recording it as a deliberate-looking inefficiency rather than a defect, but if the C++ hoists the sort — which it should — someone should confirm nobody depended on re-sorting.

**6. `result['terrain']` is replaced wholesale on a re-label, dropping `magical_biomes` and `biome_contract`.** `rebuild_tail` repairs this one line later by calling `add_biome_variants`. Stage 9's `refresh_environment`, though, runs **before** stage 10's `add_biome_variants`, so between them the block is legitimately missing those keys. If a port ever calls `refresh_environment` outside `rebuild_tail` and stage 9, the block is left incomplete. Worth an assertion rather than a comment.

**7. No conformance record covers either module.** `docs/conformance/` has sixteen records and none names `terrain_biomes.py` or `terrain_detail.py`; the nearest, `read-surface.md`, covers the read API and disagrees with nothing here. `docs/terrain-world-layers.md` and `docs/continuous-terrain.md` are prose and the second is wrong as noted above. For these two modules **this specification and the source are the only descriptions**, and the source is going away.

## Addenda from the audit

The reader above wrote the specification; a second reader went back to the source
to find what it had left out or got wrong. Both are kept: the omissions are
content the implementer needs, and the corrections say where the specification
above is not to be trusted.

### Corrections

- **Claimed:** RNG section: perlin3 'is called once per distinct cell ... and only when climate_moisture is absent from the layers. On recipe 3 that layer is always present, so on a real world this noise field is evaluated zero times and climate_seed is published but unused.'
  **Actually:** False, and the spec contradicts itself -- its EDGE CASES section states the truth. Line 62 unconditionally evaluates wet = max(0, min(1, .5 + .22*cos(3*asin(p[1])) + .9*perlin3(*(v*2.3 for v in p), seed) + cfg.moisture_bias)); line 63 then overwrites wet when climate_moisture is present. perlin3 therefore runs exactly (n-2)*(n-1) + 2 times on EVERY globe world at phase >= 2, including every recipe-3 world -- the RESULT is discarded, not the call. Functionally harmless (no sequential stream is consumed, so skipping it in C++ changes no value), but the spec states as fact that the field is evaluated zero times on a real world, which would mislead anyone profiling the pass or deciding whether the climate seed stream is live.

- **Claimed:** CONSTANTS, detail masking: '1 - 2*water then squared -- an interpolated water_type of 0.5 or more zeroes the detail, so the fade reaches half a cell inland from any ocean or lake.'
  **Actually:** Half a cell is correct only for ocean. water_type is 0 dry / 1 ocean / 2 LAKE, and the mask bilinearly interpolates the raw layer values. Between a lake cell (2) and a dry cell (0) the value at fraction f from the lake is 2*(1-f); wet = max(0, 1 - 2*value) is zero while value >= 0.5, i.e. while f <= 0.75. The detail fade therefore extends THREE QUARTERS of a cell inland from a lake and only one half of a cell from an ocean. The stated figure understates lake protection by 50%.

- **Claimed:** ALGORITHM A2: 'neighbours are the eight surrounding distinct cells (the whole adjacent ring for a pole)'
  **Actually:** Not eight in general. sphere_grid builds the set as {index(x+dx, z+dz) for dx,dz in {-1,0,1}^2} - {i}, and index() folds every z == 0 or z == n-1 onto the single pole node. A node in row 1 or row n-2 therefore has SIX neighbours -- the pole node absorbs all three dx values -- not eight. Nodes at x == 0 and x == n-2 take their longitudinal neighbours across the seam via x mod (n-1). This matters precisely where the spec says the 180 m budget bites: the polar rows are exactly the rows whose neighbour sets are degenerate, so the count the reader would use to reason about the wetland frontier is wrong there.

- **Claimed:** EDGE CASES, HeightField: 'Missing mask layers default generously: water 0, river 0, flood 0, biome 3. The defaults increase detail, so a missing layer never protects.'
  **Actually:** True for water, river and flood; false for biome. An absent natural_biome layer defaults every cell to id 3 (grassland), whose BIOME_GAIN is 0.8 -- LESS than the 1.0 a forest or rainforest cell would get and far less than the 1.4 of exposed rock. A missing biome layer measurably reduces detail over rock and forest. The spec's own ordering-hazard #10 states the decrease correctly ('gain 0.65 instead of 0.8'), so this sentence contradicts it.

- **Claimed:** WRITES: 'moisture | float | 0..1 inclusive (clamped)'
  **Actually:** The clamp is applied only to the analytic fallback. When climate_moisture is present, line 63 copies layers['climate_moisture'][z][x] straight through with no clamp in this module at all. The published range holds only because terrain_climate itself clamps with max(0, min(1, ...)) when it writes that layer (terrain_climate.py:62). A port whose climate pass emits unclamped moisture will silently publish out-of-range values in the moisture layer, and marsh_suitable's wet >= 0.55 and classify's 0.3/0.55/0.78 thresholds will then be applied to them.

- **Claimed:** ALGORITHM B2: 'if definition and definition[version] != 1: raise ValueError(Unsupported terrain detail version)'
  **Actually:** The source is `if self.definition and self.definition.get('version') != VERSION`. Using .get() rather than subscripting means a definition dict that is present but carries NO 'version' key also raises ValueError('Unsupported terrain detail version') rather than KeyError. Only an error-path difference, but the spec presents the subscript form as the code.

### Missing — important

**The direction(x, z, n) formula, which the whole classifier's latitude term and every feature marker vector depend on**

terrain_globe.direction: if z == 0 return (0.0, 1.0, 0.0); if z == n-1 return (0.0, -1.0, 0.0); otherwise lon = 2*pi*(x mod (n-1))/(n-1) - pi, lat = pi/2 - pi*z/(n-1), return (cos(lat)*cos(lon), sin(lat), cos(lat)*sin(lon)). The x wrap is modulo n-1 (the seam duplicate folds back to column 0) and the pole rows are exact literals, not evaluated trig -- which is why p[1]**2 is exactly 1 at a pole and pole temperature is exactly 28 - 45 - 0.0065*max(0,h) + offset. fwg::direction in include/fwg/grid.hpp already implements this so the port is not blocked, but this spec is meant to survive the source's deletion and it is the only place the latitude/longitude convention would have been pinned.

**water_surface equals ground height on dry land -- the fact that makes the marsh relief test pass on river seeds**

terrain_water.route_water sets level[i] = sea if the node is ocean else h[i] for roots, and level[j] = max(h[j], elevation) for every node it settles, so on dry ground water_surface == height exactly and water_depth = max(0, level - ground) = 0. Consequence inside terrain_biomes: a rain_river cell is a Dijkstra seed at distance 0 whose source is itself, so relief = height[z][x] - water_surface[z][x] = 0.0, which always satisfies marsh_suitable's `0 <= height_above_water <= 4`. That is the mechanism behind the spec's own claim that at coarse rasters marsh reduces to 'a wet, unfrozen, gently sloping river cell'. If a port's stage-5 water pass writes anything else into water_surface on dry cells (leaves it at 0, or at sea level), relief becomes the cell's full elevation above sea and biome 13 disappears from the world entirely.

**The exact sphere_grid neighbour rule, including the index() seam wrap and the pole-adjacent collapse**

terrain_erosion.sphere_grid: lookup = {(x,z): i}; index(x,z) = lookup[(0,z) if z in (0,n-1) else (x mod (n-1), z)]. For a pole node, ids = {index(xx, 1 if z==0 else n-2) for xx in range(n-1)} -- the whole adjacent ring, n-1 neighbours. For any other node i at (x,z): ids = {index(x+dx, z+dz) for dx in (-1,0,1) for dz in (-1,0,1)} - {i}. Because index() folds z==0 and z==n-1 onto the single pole node and wraps x modulo n-1, a node in row 1 or row n-2 has SIX neighbours (one pole node + two in its own row + three in the next interior row), and nodes at x == 0 or x == n-2 take their longitudinal neighbours across the seam. Each entry is (j, radius*acos(clamp(dot(vectors[i], vectors[j]), -1, 1))), built over sorted(ids).

**The twelve magic-school keys and the 156 variant_names strings in biomes.json**

The spec's DATA FILES section describes variant_names but reproduces exactly one of its 156 strings. The twelve school keys, which must match SCHOOLS exactly or _registry() raises, are: air, blood, earth, eldritch, fire, infernal, radiant, rot, umbral, void, water, weave. Each maps to a list of exactly thirteen display names matched BY POSITION to the natural_biomes rows (position 0 = ocean, 1 = tundra, 2 = desert, 3 = grassland, 4 = forest, 5 = exposed_rock, 6 = snow, 7 = rainforest, 8 = lake, 9 = marsh, 10 = boreal_forest, 11 = cold_tundra, 12 = land_ice). Nothing in terrain_biomes or terrain_detail consumes them, but biome_catalogue() does at stage 10 and biome_variant stores a flat index into cores x schools, so these 156 strings are irrecoverable world-output content once the repo is deleted and no other document carries them.

**add_terrain_labels appends its method sentence to result['warnings'] on EVERY run, and the pipeline relies on a separate dedupe pass to stop that becoming a tally**

Line 99 is an unconditional append, and add_terrain_labels runs at least four times on a full recipe-3 world (stage 8; stage 9 via refresh_environment; and once per rebuild_tail at stages 14 and 15, plus any divine visitation). Without repair, warnings accumulates four identical copies of the method sentence. The repair is terrain_time._dedupe_warnings, whose recorded intent is worth carrying verbatim: 'Keep `warnings` a set of statements rather than a tally of how often a pass ran... Run once at generation that is invisible; run on a cadence it turns a list of limitations into a counter, and it would differ between two chunkings of one span purely by how many calls were made. Order is preserved, and only exact repeats are dropped -- a warning this tick did not produce is never removed.' It keys on json.dumps(warning, sort_keys=True, default=str) and rebuilds the list in first-seen order.

### Missing — minor

**Stage 8 and refresh_environment do not pass the caller's cfg unchanged**

terrain_history stage 8 is `c = replace(cfg, phase=6); add_climate(result, c); add_terrain_labels(result, c); add_environment(result, c)` -- the spec's pipeline listing shows plain `cfg`. Identically, refresh_environment calls add_terrain_labels(result, replace(cfg, phase=6)) and then add_environment(result, replace(cfg, phase=6)). The substitution matters only for the `cfg.tectonics and cfg.phase < 2` gate (phase 6 always clears it), but a port that forwards the real phase into a re-label on a low-phase world would silently skip the pass instead of running it.

**refresh_environment removes magic_density from layers across the add_terrain_labels call**

`density = result['layers'].pop('magic_density', None); add_terrain_labels(result, replace(cfg, phase=6)); if density is not None: result['layers']['magic_density'] = density` -- the layer is popped before the re-label and restored before add_environment. Nothing in terrain_biomes reads or writes magic_density, so this is defensive, but it is part of the documented call contract for the re-label and a port that drops it changes which layers exist during the pass.

**result['terrain']['landforms'] is the module-level LANDFORMS list object itself, not a copy**

Line 91 assigns `'landforms': LANDFORMS`, the same list object shared by every world block (unlike 'biomes'/'natural_biomes', which are two fresh natural_catalogue() calls producing equal but distinct lists). Mutating world['terrain']['landforms'] mutates the module constant for the rest of the process. The spec's ordering-hazard list flags the analogous `[[0]*n]*n` shared-row trap but not this one; in C++ a value-semantics std::vector<std::string> copy removes the hazard and is the right choice.

**natural_catalogue()'s name field uses Python str.capitalize(), which lowercases the tail as well as uppercasing the head**

name = 'Persistent land ice' if bid == 17 else core.replace('_', ' ').capitalize(). Python's capitalize() uppercases the first character AND lowercases every other one. All thirteen cores are already lowercase so today's output is unaffected, but a C++ port that only does toupper(s[0]) diverges the moment a row with an uppercase letter is appended. The emitted names, in file order, are: Ocean, Tundra, Desert, Grassland, Forest, Exposed rock, Snow, Rainforest, Lake, Marsh, Boreal forest, Cold tundra, Persistent land ice.

**node_grid's initialisation, and why every cell ends up written**

terrain_climate.node_grid(values, points, n): out = [[0.0]*n for _ in range(n)]; for (x,z), v in zip(points, values): out[z][x] = v; out[0] = [values[0]]*n; out[-1] = [values[-1]]*n; for row in out: row[-1] = row[0]. The 0.0 fill is never observable -- the scatter covers x in 0..n-2 for rows 1..n-2, the two pole rows are overwritten wholesale from values[0] and values[-1], and column n-1 is then copied from column 0 -- but a port that skips the fill and leaves the column-(n-1) entries uninitialised before the seam copy has undefined behaviour rather than a benign zero.
