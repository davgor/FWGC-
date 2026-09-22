# Port specification: 14-city-planner

Extracted from the Python `FantasyWorldGenerator` at commit `b67c99b` before that
repository was retired, then audited against the same source by a second reader.

**Source modules**

- `Sim/icarus_sim/city_planner.py`
- `Sim/icarus_sim/city_shapes.py`
- `Sim/icarus_sim/city_geometry.py`
- `Sim/icarus_sim/city_fortifications.py`

**Estimated C++ size:** roughly 4200 lines, excluding tests.

## Audit: NOT YET IMPLEMENTABLE

This is an unusually strong spec — the driver, the sampler, `grow_roads`, `_clearance`, the candidate stream, `footprint_cells`, the fortification pipeline, the housing reservation/upgrade/release protocol, the ordering hazards and the preserved reasoning are all faithful and would survive the source's deletion. The `angle` rebind in `_stream`, the unclamped `asin`, the `climate`/`water` block-level fallback, the `None` in `_gate_sites`' sort tuple, the prefilter soundness argument and the `heights[c] for c in sorted(...) if c in heights or [...]` precedence trap are all caught correctly. I set implementable=false for two reasons, both of which cause a competent programmer to silently build something different rather than get stuck. (1) The spec states that the `height` layer is what "`HeightField` indexes directly". It is not: `H(direction)` is a *bilinear* interpolation of the coarse grid (fractional column with seam wrap, `ix+1` deliberately reading the duplicate column) plus three Perlin bands gated by a coast/strength/wetness mask. An implementer who follows the spec and nearest-samples gets a height field that is constant inside every coarse cell, which makes the finite-difference slope exactly zero everywhere, makes the corner-vs-cell mixing in plot test (3) pointless, and makes `height_at` redundant with `heights[]` — the planner still runs and still emits plausible plans, so nothing catches it. (2) `city_shapes.json` dies with the repo, and the spec documents its schema, its eligibility floors and its family list but not its per-shape `variation` intervals nor which field each `preferences` rule tests. `aspect_ratio` sets `rz` (the elliptical clip), `block_spacing_m` sets street spacing, `ring_count` sets ring count, and the only preference bonuses that can ever fire (`aridity`, `wooded_fraction`, `regional_threat`, `defense_priority`) are exactly the ones never enumerated — so the implementer must invent the numbers that decide every city's shape and footprint. Beyond those, three confident statements are simply false (the cost-field range, the wall-segment length range, the feature count), and several emitted fields and string literals are missing from the writes section. Fix those and the document is implementable.

**3 blocking gap(s)** were found and are recorded in the addenda below.

## Purpose

Stage 16 of the sixteen-stage ladder. Takes each city site chosen at stage 9 and packs it as a schematic town plan in **local metres**: a square raster of buildable ground centred on the site, a seeded street network grown over that raster, an optional defensive enceinte, and a set of non-overlapping rotated building plots drawn from the civilization's city preset, each with street frontage, an access corridor, a ground elevation and a rotation. It exists because `settlements.sites[]` says only *where* a city is and what civilization it belongs to; `city_plans` is the **only** block in the whole world document that says which buildings a settlement actually has, where each one stands, how it is rotated, who staffs it and how many beds were built for those staff. Downstream, `world_scene` turns every plot into a globe-space building with axes and a footprint polygon, and `npc_roster` opens exactly one NPC post per staffed worker. The planner is deliberately *schematic*: it resolves no groundwater, no structural feasibility and no flood extent, and it says so in four `warnings` strings it emits on every plan.

## Where it sits in the pipeline

**Must already exist before `fill_cities(world)` runs:**

- `world['config']` with `seed` (int) and `size` (n, the raster edge; column n-1 duplicates column 0).
- `world['effective_config']['globe_radius']` in metres (falls back to `world['config']['globe_radius']` if `effective_config` is absent). This is the physical radius, not the design radius.
- `world['layers']`: `height` (required — `HeightField` indexes it directly), and optionally `slope` (degrees), `water_type` (0 dry / 1 ocean / 2 lake), `river` (0..1), `flood_risk` (0 or 1), `moisture` (0..1), `natural_biome` (int id), `biome_variant` (int id, -1 = none). Every optional layer has a documented default (see **reads**).
- `world['settlements']['sites']` — the stage-9 city records.
- `world['water']['nodes']` — list of `[x, z]` raster coordinates, and river segment index pairs in either `world['climate']['river_segments']` or `world['water']['river_segments']` (see the ordering hazard about which is preferred).
- `world['roads']['routes']` — each route has `from`, `to` (indices into `settlements.sites`) and `nodes` (indices into `water.nodes`). Consumed by `world_scene.road_entries`.
- `world['terrain_detail']` — optional; when present `HeightField` adds Perlin bands and the planner computes real local slope by finite difference instead of reading the coarse `slope` layer.
- `world['threat_assessments']['cities']` — optional; rows of `{city_uid, regional_threat}` written by the beast/age assessment.
- `world.get('terrain', {})['biomes']` / `['magical_biomes']` and `world.get('magic', {})['networks']` — copied verbatim into each plan as catalogues, not interpreted.
- The civilization registry (`civilizations.json` via `civilization_registry`) and the shape catalogue (`city_shapes.json`).

**What depends on it afterwards:**

- `fill_cities` itself calls `world_scene.build_scene(world)` as its last act, which rebuilds the unified globe-space scene from `city_plans`, `hamlet_plans`, `castle_plans`.
- `npc_roster` opens one post per `plots[].workers`.
- `fantasy_world_generator.asset_list` must contain every `plots[].building_id`.
- `terrain_history` re-runs `fill_cities` at the end of every age advance, and refuses to load a saved world whose `city_plans.identity` does not equal the current `planner_identity()`.

**Order inside stage 16:** `fill_cities` runs *first*, before `fill_hamlets`, `fill_castles`, nomads, beast movements and encounters. It writes nothing into `settlements` — the plan reads the site and never the other way round, which is what keeps stage 9 reproducible without stage 16.

**Position among the three planners:** `city_planner`, `hamlet_planner` and `castle_planner` are independent; a castle plan is explicitly not derived from `city_plans`. They share `city_geometry` (`grow_roads`, `corners`, `footprint_cells`) and the `ICARUS_CITY_WORKERS` env var.

## Inputs

**Per-site fields read from `settlements.sites[]`** (the block's key is `uid`, a string; `id` is an int and is the fallback):

| field | type | use |
|---|---|---|
| `uid` | str | plan `city_uid`; sort key for `fill_cities`; shape-selection salt. Falls back to `str(site['id'])`. |
| `id` | int | plan `site_id`, always emitted raw. |
| `name` | str | plan `name`. |
| `x`, `z` | numbers in **raster node units**, 0..n-1 | the site's node. Converted to lat/lon by `lat = pi/2 - pi*z/(n-1)`, `lon = -pi + 2*pi*x/(n-1)`. |
| `city_class` | `'small'` / `'medium'` / `'capital'` | selects the preset block (`<class>_city`), the base half-extent, and the ring policy. |
| `population_profile` | str, a civilization entity id | selects the preset. Emitted as `civilization_id`. |
| `population_estimate`, `urban_population_estimate` | numbers | copied verbatim into `stats`, never used in any computation. May be absent → `None`. |
| `freshwater_distance_m` | number or absent | `conditions['water_collection_or_delivery'] = (value is not None)`. |
| `resource_potential` | number, default 0 | `conditions['ore_and_fuel_supply'] = (value >= 0.5)`. |
| `founded_by` | str or absent | `== 'player'` means the city is authored; the planner packs nothing. |

**Layers**, all indexed `[gz][gx]` (row = latitude, column = longitude), with defaults when the key is missing:

| layer | default | units/range | use |
|---|---|---|---|
| `height` | required | metres above reference sphere | `HeightField` base |
| `slope` | 0 | degrees 0..90 | cell block test `> 25`; overridden by finite-difference slope when `terrain_detail` exists |
| `water_type` | 0 | 0/1/2 | `!= 0` ⇒ cell is water |
| `river` | 0 | 0..1 | `> 0.5` gates the routed-river channel test |
| `flood_risk` | 0 | 0 or 1 | **read and emitted, never used to block a cell** — see reasoning_to_preserve |
| `moisture` | 0.5 | 0..1 | `aridity = 1 - clamp(moisture, 0, 1)` at the city centre only |
| `natural_biome` | 3 | int | per-cell biome, and the wooded test `biome in {4, 7, 15}` |
| `biome_variant` | -1 | int | per-cell magical variant, emitted only |

**World blocks:** `water.nodes`, `climate.river_segments` (preferred) or `water.river_segments`, `roads.routes`, `threat_assessments.cities`, `terrain_detail`, `terrain.biomes`, `terrain.magical_biomes`, `magic.networks[*].color`.

**Config:** `config.seed` (int), `config.size` (n), `effective_config.globe_radius` (metres).

**Environment:** `ICARUS_CITY_WORKERS` — an integer process-pool width, read only by `fill_cities` and only when its `workers` argument is `None`.

## Outputs

`fill_cities` writes exactly one top-level block:

```
world['city_plans'] = {
  'version': 8,
  'identity': planner_identity(),        # see constants
  'cities': [ <plan>, ... ],             # one per site, in sorted-uid order
  'phase_order': ['map','shape','fortification','high','high_housing','low','low_housing'],
  'scope': 'Schematic local metres from final world raster; does not change simulated population.'
}
```

and then calls `world_scene.build_scene(world)`, which replaces `world['world_scene']`.

**One `<plan>` (the return value of `plan_city`), in emission order.** Every distance is metres in the local gnomonic frame unless stated.

| field | type | notes |
|---|---|---|
| `version` | 8 | |
| `city_uid` | str | `uid` or `str(id)` — **this block's key** |
| `site_id` | int | the raw `id` |
| `name`, `civilization_id`, `city_class` | str | |
| `unit` | `'metres'` | |
| `bounds_m` | `[-half, -half, half, half]` | half is an int multiple of 4 |
| `terrain.cell_m` | 4 | |
| `terrain.size` | int | cells per edge, `= 2*half/4` |
| `terrain.codes` | `size × size` ints, `[j][i]` | 0 buildable, 1 water, 2 steep-or-channel |
| `terrain.surface` | `{size: size+1, step_m: 4.0, heights_m: (size+1)² doubles rounded to 4 dp, source, coordinates}` | corner-sampled elevations |
| `terrain.natural_biome`, `terrain.biome_variant` | `size × size` ints | |
| `terrain.biome_catalogue`, `terrain.magical_catalogue`, `terrain.magic_colors` | copied through | |
| `reference_frame` | `{radius_m, latitude_degrees, longitude_degrees, projection}` | `projection` is the literal string `direction=normalize(up+(east*x_m+north*z_m)/radius_m); position=(radius_m+height_m)*direction` |
| `location` | the seven `facts` (see algorithm step 4) | |
| `shape` | the `select_shape` record | `shape_id` (may be `None`), `parameters`, `candidates[]`, `layout`, `source_ids`, `reason`, `catalogue_revision`, `catalogue_sha256`, `version: 1`, `runtime_geometry_generated: false` |
| `plots[]` | see below | |
| `roads` | `[[i, j], ...]` sorted ascending | street cells (cell indices, not metres) |
| `unplaced[]` | `{building_id, count, reason}` | |
| `fortifications` | `null` for an unbuildable/authored city, else the fortification report | |
| `passes[]` | `[{id, placed}]` in `phase_order` | `map.placed` = terrain-valid cell count, `shape.placed` = 1, `fortification.placed` = kept wall-segment count, the rest = plots placed in that phase |
| `warnings[]` | four fixed strings, plus a fifth when `source_resolution_m > half` | |
| `source_resolution_m` | `round(pi*radius/(n-1), 2)` | the regional raster's metre spacing |
| `status` | `'unbuildable'` / `'partial'` / `'complete'` | |
| `stats` | see below | |
| `debug` | see below | |
| `road_connections[]` | from `world_scene.road_entries`, each mutated with `cell` and possibly `status`/`reason`/`local_path_m` | |
| `street_paths_m[]` | list of polylines of `[x, z]` rounded to 2 dp | only present when a shape was selected |
| `authored_by` | `'player'` | **only** when the site was player-authored |

**A `plot`** — this block's key is `id`, a string `"plot-<n>"` with `n` the index at insertion time:

```
{'id': 'plot-7', 'building_id': <structure_id or housing id>, 'name': str,
 'kind': 'service' | 'housing', 'phase': 'fortification'|'high'|'high_housing'|'low'|'low_housing',
 'x_m': float (2 dp), 'z_m': float (2 dp), 'rotation_degrees': float (4 dp),
 'ground_elevation_m': float (4 dp, = max of sampled ground),
 'foundation_bottom_m': float (4 dp, = min of sampled ground),
 'plot_m': {'width','depth'}, 'dimensions_m': {'width','depth','height'},
 'road_access': [ax, az],            # the street anchor this plot fronts
 'workers': int, 'beds': int,
 'housing_upgrade': {...}}           # OPTIONAL, only on a house converted to apartments
```

`housing_upgrade` is `{'phase': <phase>, 'previous': {'building_id','name','dimensions_m','beds'}}` and is written with `setdefault`, so the first upgrade wins.

**`stats`** (absent keys on an unbuildable plan are marked †): `workers`, `worker_beds`, `housing_shortfall`, `service_buildings`, `houses`, `apartments`†, `unplaced_core`†, `unplaced_total`†, `population_estimate`, `urban_population_estimate`, `reserved_plot_area_m2`†, `wall_segments`†, `defended_perimeter`†.

**`debug`**: `terrain_safe_cells`, `total_cells`, `housing_passes[]` (one audit per housing phase: `phase, workers, starting_beds, houses_placed, upgrades, plots_exhausted, reserved_slots, final_beds, shortfall`), `apartment_policy` (fixed string `houses_first_then_upgrade_on_plot_exhaustion`), `housing_reservations[]` (`{phase, workers_budgeted, slots}`), `shape_safe_cells`, `road_cells`, `occupied_cells`, `access_cells`, `vacant_shape_cells`, `housing_frontage_candidates`, `fortification_rings`, `program_half_m`.

**The fortification report** (`city_fortifications.build_fortifications`), `version: 1`:

```
{'version':1, 'defended_perimeter': bool,
 'rings':[{'id':'ring-0','role':'inner'|'middle'|'outer','scale':float(4dp),
           'polyline_m':[[x,z],...], 'status':'closed',
           'segment_count':int,'gate_count':int}],
 'segments':[{'id':'ring-0-seg-3','ring_id','structure_id','from_m','to_m',
              'center_m':[2dp],'length_m':3dp,'rotation_degrees':4dp}],
 'nodes':[{'id':'ring-0-node-7','kind':'corner','position_m','ring_id'}],
 'gates':[{'id':'ring-0-gate-0','ring_id','structure_id','position_m','approach_m',
           'route_index': int|None, 'plot_id': str}],   # plot_id added by the planner if a gatehouse was placed
 'towers':[{'id':'ring-0-tower-0','ring_id','structure_id':'building.tower',
            'position_m', 'plot_id': str}],
 'unplaced':[{'building_id', 'ring_id'?, 'reason'}],
 'method': 'Angular boundary samples of the buildable shape; city-only wall network.',
 'limits': 'Schematic enceinte, not structural engineering. Castle baileys use a separate generator.'}
```

Note the **four different key names** in this one block: rings/segments/nodes/gates/towers all use `id`, plots use `id`, the plan uses `city_uid`, preset rows use `structure_id`, housing profiles use `id`, and the site uses `uid`. There is no uniform id field anywhere.

## Algorithm

## 0. `fill_cities(world, workers=None)` — the driver

1. `sites = sorted(world['settlements']['sites'], key = lambda s: str(s.get('uid', s['id'])))`. **The sort key is the string form of the uid** — lexicographic, not numeric. `"city-10" < "city-2"`.
2. If `workers is None` and the environment variable `ICARUS_CITY_WORKERS` is set, `workers = int(env)`.
3. If `workers != 1` **and** `len(sites) >= 3`, attempt the pooled path (§0a); on **any** exception, emit a `RuntimeWarning` naming the exception type and message, and fall through to serial. The warning text is quoted in reasoning_to_preserve — it must be kept, because a silent fallback is indistinguishable from a planner that is merely slow.
4. Otherwise (or after a failed pool) run the serial loop (§0b).
5. Write `world['city_plans']` and call `build_scene(world)`; return the block.

### 0a. Pooled path (`_plan_pooled`)

The plan for one city is a pure function of `(world, site, nearby_counts)`. The *only* thing serialising the cities is the shape-repetition counter, which each city reads and the next one sees updated. Splitting that one dependency out lets the geometry — which is all of the cost — run in parallel:

1. Pickle the whole world once; `workers = min(workers or cpu_count() or 1, len(sites))`.
2. Start a process pool whose initializer unpickles the world into a process global.
3. **Pass 1:** `pool.map(shape_only_plan, range(len(sites)))` — each worker runs `plan_city(..., shape_only=True)`, which returns the seven `facts` and stops before the expensive surface grid.
4. **Serially, in the parent**, replay the counter (`_shape_counts`): walk the sites in sorted order; before each site, snapshot the current counts dict; if the site is player-authored, skip (an authored city is given no shape, so it does not count); otherwise run `select_shape(fact, seed, uid, class, counts)` and, if it returned a `shape_id`, increment `counts[shape_id]`.
5. **Pass 2:** `pool.map(full_plan, enumerate(snapshots))` — each worker runs the full `plan_city` with its own snapshot.

In each worker, the sites are re-derived by re-sorting the unpickled world with the identical key, then indexed.

### 0b. Serial path (`_plan_serial`)

`counts = {}`; for each site in sorted order, `plan = plan_city(world, site, counts)`, append, and if `plan['shape']['shape_id']` is truthy increment `counts[shape_id]`.

---

## 1. `plan_city(world, site, nearby_counts=None, shape_only=False)`

### 1.1 Resolve the preset

`preset = city_plan(site['population_profile'], site['city_class'] + '_city')` — the civilization registry expands the class block into rows that each merge the structure definition with the per-city row. A row carries `structure_id`, `name`, `count`, `priority` (`'core'` / `'conditional'` / `'specialist'`), `placement_conditions[]`, `plot_m {width,depth}`, `dimensions_m {width,depth,height}`, `clearance_m`, and `staffing.roles[].target`. `plot_m = dimensions_m + clearances` is a registry invariant.

`housing = section('housing_profiles')`; `house = housing['worker_house']`, `apartment = housing['worker_apartment']`. Both carry `id`, `name`, `dimensions_m`, `plot_m`, `worker_beds`. The registry guarantees the apartment has the **same plot** and a strictly higher bed count — that is what makes in-place densification legal.

### 1.2 Neighbour bound and the programme half-extent

```
neighbour_half = {'small':240, 'medium':320, 'capital':400}[city_class]
for other in world['settlements']['sites']:
    if other is site or other.get('uid') == site.get('uid'): continue
    # great-circle distance in metres on the effective radius
    lat1 = pi/2 - pi*site['z']/(n-1);  lat2 = pi/2 - pi*other['z']/(n-1)
    dl   = 2*pi*(other['x'] - site['x'])/(n-1)
    d    = radius * acos(clamp(sin(lat1)*sin(lat2) + cos(lat1)*cos(lat2)*cos(dl), -1, 1))
    neighbour_half = min(neighbour_half, d*0.28)
```

`0.28` gives two adjacent cities a 44 % gap between their crops. **This loop bounds the footprint without moving any anchor** — no site position is changed.

Then `half = program_half_m(preset, city_class, neighbour_half, house_plot=(house.plot_m.width, house.plot_m.depth), beds_per_house=house.worker_beds)`:

```
base       = {'small':240,'medium':320,'capital':400}[city_class]
plot_area  = sum(row.plot_m.width * row.plot_m.depth * row.count for row in preset.buildings)
workers    = sum(sum(r.target for r in row.staffing.roles) * row.count for row in preset.buildings)
houses     = ceil(workers / max(1, beds_per_house))
plot_area += houses * house_plot[0] * house_plot[1]
needed     = sqrt(max(plot_area, 1) * 2.4) / 2 * 1.15   # 2.4 = streets/wall/corridor overhead; 1.15 = slack
half       = min(float(neighbour_half), max(base, needed))
return max(4, int(half/4)*4)                             # truncate toward zero to a multiple of 4
```

`size = 2*half // 4` — since `half` is a multiple of 4 this is exact, e.g. `half=240 → size=120`.

### 1.3 Build the local sampler (`_sampler(world, site, half)`)

A **gnomonic** tangent-plane frame at the site node:

```
lat = pi/2 - pi*z/(n-1);  lon = -pi + 2*pi*x/(n-1)
up    = ( cos(lat)*cos(lon),  sin(lat),  cos(lat)*sin(lon) )
east  = (-sin(lon),           0,         cos(lon)          )
north = (-sin(lat)*cos(lon),  cos(lat), -sin(lat)*sin(lon) )
direction_at(x, z) = normalize( up + (east*x + north*z)/radius )
```

**River lines.** Project every routed river segment endpoint into the plane:

```
local(node):
    la = pi/2 - pi*node.z/(n-1);  lo = -pi + 2*pi*node.x/(n-1)
    p  = (cos(la)*cos(lo), sin(la), cos(la)*sin(lo))
    den = dot(p, up)
    if den <= 0.1: return None        # more than ~84 deg away; the projection blows up
    return ( radius*dot(p,east)/den, radius*dot(p,north)/den )
```

Segment source: `world.get('climate', world.get('water', {})).get('river_segments', [])` — **the climate block is preferred as a whole**, so a world that has a `climate` block without `river_segments` yields no river lines even if `water.river_segments` is populated (ordering hazard). Endpoints index `world['water']['nodes']`.

A segment is kept only if both endpoints projected and its axis-aligned bounding box intersects `[-half-40, half+40]` on both axes. The 40 m pad covers the 28 m setback plus slack.

`river_distance(x, z)` = minimum point-to-segment Euclidean distance over the kept lines (`1e30` if there are none), with the usual clamped projection `t = clamp(((P-A)·(B-A)) / |B-A|², 0, 1)`; the `|B-A|²` denominator is replaced by 1 when zero.

**`sample(x, z)`** returns eight values:

```
p  = normalize(up + (east*x + north*z)/radius)
gx = round((atan2(p.z, p.x) + pi)/(2*pi)*(n-1)) % (n-1)
gz = clamp(round((pi/2 - asin(p.y))/pi*(n-1)), 0, n-1)
distance = river_distance(x, z)
slope = slope_layer[gz][gx]                       # 0 if the layer is absent
if terrain_detail exists:
    dx = (H(direction_at(x+1,z)) - H(direction_at(x-1,z)))/2     # central difference over 2 m
    dz = (H(direction_at(x,z+1)) - H(direction_at(x,z-1)))/2
    slope = degrees(atan(hypot(dx, dz)))
channel = 1 if (lines nonempty and river_layer[gz][gx] > 0.5 and distance < 28) else 0
return { water:  water_type[gz][gx] != 0  or  distance < 12,
         slope:  slope,
         channel: channel,
         flood_risk: flood_layer[gz][gx],
         height: H(p),
         moisture: moisture[gz][gx]   (default 0.5),
         biome:    natural_biome[gz][gx] (default 3),
         variant:  biome_variant[gz][gx] (default -1) }
```

`sample.height_at(x, z) = H(direction_at(x, z))` — an elevation-only fast path that returns the *identical* float as `sample(...).height` because `H` is a pure function of the same normalised vector. It skips the river-distance loop, two inverse-trig grid-index calls, six layer lookups and the eight-key result. The surface grid alone calls it `(size+1)²` times per city.

The sampler also returns `resolution = pi*radius/(n-1)`, the regional raster's metre spacing.

### 1.4 Rasterise the city ground

For `j` in `0..size-1`, `i` in `0..size-1` (row-major, j outer):

```
v = sample(-half + (i+0.5)*4, -half + (j+0.5)*4)
heights[(i,j)] = v.height
code = 1 if v.water else (2 if (v.slope > 25 or v.channel) else 0)
codes[j][i] = code;  biomes[j][i] = v.biome;  mutations[j][i] = v.variant
if code == 0:
    valid.add((i,j));  slopes.append(v.slope);  woods += (v.biome in {4, 7, 15})
```

**`flood_risk` is sampled and emitted but never blocks a cell.** See reasoning_to_preserve — this is the single most expensive piece of intent in the file.

Then `center = sample(0, 0)`, and the regional threat is the first `threat_assessments.cities` row whose `city_uid` equals this city's uid, else `0.0`.

### 1.5 The seven `facts`

```
buildable_area_m2   = len(valid) * 16                    # 4 m cells
usable_land_fraction= len(valid) / (size*size)
local_slope_degrees = mean(slopes) if slopes else 90     # 90 = "unbuildable" sentinel
aridity             = 1 - clamp(center.moisture, 0, 1)
wooded_fraction     = woods / max(1, len(valid))
regional_threat     = threat
defense_priority    = threat                             # deliberately the same number
```

**If `shape_only` is true, return here.** The pooled path takes this exit: shape selection reads `facts` and nothing below, and the counter must run in uid order before any city can be fully planned.

### 1.6 Surface grid

`surface_size = size+1`; `step = 2*half/(surface_size-1)` (which equals exactly 4.0). Heights at the `(size+1)²` cell *corners*, `round(height_at(-half + i*step, -half + j*step), 4)`.

### 1.7 Shape selection (`city_shapes.select_shape`)

`select_shape(facts, config.seed, str(uid or id), city_class, nearby_counts)`.

**Ranking (`_rank`).** Validate, then for each shape in the catalogue **sorted by `id`**:

- skip if `not enabled_by_default` and `include_later` is false (the planner always passes false);
- skip if `city_class not in shape.city_classes`;
- skip unless **every** rule in `eligibility.all` matches;
- collect the `preferences` whose `when` rule matches;
- `weight = (base_weight + sum(matched bonuses)) / (1 + counts.get(id, 0) * repetition_penalty)`;
- emit `{id, family, weight, matched_preferences: [rule.field, ...]}`.

A rule is `{field, min?, max?, equals?}`. **`_matches` returns false when the field is absent from the site dict** — a missing feature fails a hard requirement and earns no bonus. That is the documented `missing` semantics for every boolean feature in the catalogue.

**Roulette.** With `identity = sha256(canonical_json(catalogue))` (see constants) and

```
unit(label) = int.from_bytes(sha256(json([seed, city_id, identity, label]))[:8], 'big') / 2**64
```

take `target = unit('shape') * sum(weights)`, default `chosen = candidates[-1]`, then walk the ID-sorted candidate list subtracting each weight and taking the first row that drives `target` below zero. (The `candidates[-1]` default catches the floating-point case where the accumulated subtraction never goes negative.)

**Parameters.** For each `(key, interval)` in `sorted(shape.variation.items())`:

```
u = unit(shape.id + ':' + key)
integer interval: parameters[key] = min(high, low + int(u*(high - low + 1)))
else:             parameters[key] = round(low + u*(high - low), 6)
```

If no candidate is eligible, the result carries `shape_id: None` and `reason: 'No compatible shape; change site or provide missing location facts.'`

### 1.8 Authored cities

`authored = (site.get('founded_by') == 'player')`. An authored city gets the full record — terrain, reference frame, road connections — because a consumer rendering the player's town needs the ground it stands on. What it does **not** get is a planner's opinion about where the buildings go: `shape_id` is forced to `None`, `parameters` to `{}` and `reason` to *"Layout authored by the player; the generator chooses no shape for an authored city."*, and `authored_by: 'player'` is added. `unbuildable` is a status the planner already emits, so no consumer needs a new branch.

### 1.9 Early exit

`result['road_connections'] = road_entries(world, site, half)` is computed for **every** plan, authored or not. If `resolution > half`, append the fifth warning (*"Regional land categories repeat coarse samples; canonical local relief supplies finer elevation detail."*).

If `shape_id` is falsy (authored or no eligible shape): fill `unplaced` with one row per preset building (`{building_id: structure_id, count, reason}`, reason `'Layout authored by the player'` or `'No compatible buildable footprint'`), set the five-key `stats` (all zeros plus the two population figures), and **return with `status == 'unbuildable'` and `fortifications == None`**.

### 1.10 Shape clip

```
family  = catalogue family of the chosen shape
spacing = max(72, parameters['block_spacing_m'])       # metres
aspect  = parameters['aspect_ratio']
rx = half*0.94;   rz = half*0.94 / min(aspect, 2)
inside(x, z):
    grid | compound | hybrid : |x| < rx and |z| < rz
    cluster                  : any of three discs of radius half*0.52 centred at
                               (-half*0.32, 0), (half*0.32, 0), (0, half*0.25)
    everything else          : (x/rx)² + (z/rz)² < 1
```

`terrain_valid = copy of valid` (unclipped); `valid = {c in valid : inside(cell centre of c)}`. The clipped set is what plots and walls use. The comment records that *organic, contour, rings and paired lobes share the elliptical clip; ring scales are fortification-only*.

### 1.11 Streets

```
seed = int.from_bytes(sha256(f"{config.seed}:{city_uid}:streets-v2")[:8], 'big')
for each road connection c: c['cell'] = [clamp(floor((v + half)/4), 0, size-1) for v in c['gate_local_m']]
road, paths = grow_roads(terrain_valid if connections else valid,
                         heights.__getitem__, size, seed, spacing/4,
                         [tuple(c['cell']) for c in connections])
```

Note the domain switch: **when the city has regional road connections the street network is grown over the unclipped terrain-valid set**, so streets may run outside the shape clip and therefore outside the plot-eligible `valid`.

#### `grow_roads(valid, height, size, seed, spacing_cells, required=(), branch_floor=12)`

An 8-connected **Dijkstra** tree from a central root, then greedy **farthest-point** branch targets, then a one-cell orthogonal widening.

1. Empty `valid` → `(∅, [])`.
2. Seed a Mersenne-Twister-equivalent RNG with `seed`.
3. `root = argmin over valid of ((x - size/2)² + (z - size/2)², (x, z))` — nearest the raster centre, tie-broken by the cell tuple.
4. Three phase draws `uniform(-pi, pi)`, consumed in order. *A smooth travel-cost field produces persistent bends, rather than white-noise zigzags.*
5. `heights[c] = height(c)` for `c` in `sorted(valid)`.
6. `costs[c] = 1 + 0.45*(1 + sin(x/9 + p0)*cos(z/11 + p1)) + 0.3*(1 + sin((x+z)/17 + p2))` for `c` in `sorted(valid)`; range `[0.25, 2.05]`.
7. **Dijkstra**, lazy-deletion binary heap keyed `(cost, cell)` so ties break on the cell tuple. Pop, skip if `cost != distances[c]`, then relax the eight neighbours **in this exact order**: `(-1,0), (0,-1), (0,1), (1,0), (-1,-1), (-1,1), (1,-1), (1,1)`.
   - `q` must be in `valid`.
   - A diagonal step requires **both** orthogonal support cells `(x+dx, z)` and `(x, z+dz)` to be in `valid` — no corner cutting.
   - A diagonal step additionally requires that **all four** height differences `|h[r] - h[end]|` for `r` in the two support cells and `end` in `{c, q}` are `<= 1.4` m. This is the cliff guard: it forbids a diagonal that slips between two cliff cells.
   - `length = hypot(dx, dz) * 4` — the **4 is a hardcoded metre cell size in this function**, not a parameter.
   - `grade = |h[q] - h[c]| / length`; reject if `grade > 0.35`.
   - `new = cost + length * ((costs[c] + costs[q])/2 + 18*grade²)`. The quadratic grade term at the 0.35 ceiling adds `18*0.1225 = 2.205` — roughly the whole span of the smooth cost field, so a steep link is about twice as expensive as the worst flat one.
8. `reachable = sorted(distances)`; `targets = sorted(set(required) & set(distances))` — required gate cells that the tree actually reached, in ascending cell order. *Destinations represent gates and growing districts, never an imposed street grid.*
9. `count = max(branch_floor, min(36, round(len(valid) / max(12, spacing_cells)² * 1.5)))`. With `spacing >= 72 m` and 4 m cells, `spacing_cells >= 18`, so the `max(12, ·)` never binds in the city planner.
10. `candidates = rng.sample(reachable, min(len(reachable), max(256, count*20)))` — a uniform sample **without replacement, in draw order**.
11. Repeat `count` times, stopping early if `candidates` is empty: pick `target = argmax over candidates of (min over q in [root] + targets of squared cell distance, cell)` — greedy farthest-point, tie-broken by the cell tuple, with `targets` growing each round so the min is over an expanding set. Append to `targets`, remove from `candidates`.
12. `network = {root}`; for each target in `targets` order, walk the predecessor chain back until a cell already in `network`, and if the walk has length > 1 record the **reversed** path as a street path and merge it into `network`.
13. `roads = set(network)`; then for each `(x,z)` in `sorted(network)` and each of the four orthogonal neighbours `q`, add `q` to `roads` if `q` was reached by the Dijkstra **and** `|h[q] - h[(x,z)]| <= 1.4`. This widens the centreline to two cells where the step is gentle.
14. Return `(roads, paths)`.

#### Connecting to the regional roads

For each connection `c` with cell `cell` and metre gate point `gate`:

```
point  = cell centre in metres
v      = sample(*gate)
length = dist(point, gate)
if cell in road and not v.water and not v.channel and v.slope <= 25
        and |v.height - heights[cell]| <= 0.35*length + 1e-6:
    approach = the first street path whose LAST cell == cell, else [cell]
    c.status = 'connected'
    c.reason = 'Terrain-safe junction to regional road'
    c.local_path_m = [cell centres along approach] + [gate]
```

Connections that fail keep the `status: 'unreachable'` / `reason: 'No terrain-safe street connection to regional road'` that `road_entries` gave them.

`result['roads'] = [list(c) for c in sorted(road)]`; `result['street_paths_m'] = [[[round(-half+(x+.5)*4, 2), round(-half+(z+.5)*4, 2)] for (x,z) in path] for path in paths]`.

### 1.12 Clearance prefilter (`_clearance(valid - road, size)`)

A **Chebyshev distance transform**: for each cell, the number of cells to the nearest cell that is **not free ground**, where free = shape-valid and not a street, and everything outside the raster counts as not free.

Two sweeps, both in place over a `size × size` int array indexed `grid[j][i]`, initialised to `2*size` for free cells and `0` for blocked ones:

```
forward  (j ascending, i ascending):
    if grid[j][i] == 0: continue
    best = grid[j][i-1] + 1  if i > 0  else 1
    if j == 0:                       # no row above -> the raster edge is blocked
        if best > 1: best = 1
    else:
        best = min(best, grid[j-1][i] + 1)
        if i > 0:       best = min(best, grid[j-1][i-1] + 1)
        if i+1 < size:  best = min(best, grid[j-1][i+1] + 1)
    grid[j][i] = min(grid[j][i], best)

backward (j descending, i descending): the mirror image, using grid[j][i+1],
    grid[j+1][i], grid[j+1][i+1], grid[j+1][i-1], and `best = 1` at the far edges.
```

The `best = 1` at every border is the edge convention: a free cell on the raster boundary is at distance 1 from blocked ground.

**Why it exists:** two sweeps once per city let the candidate sweep decide most positions without rasterising anything. Roughly four candidate positions in five used to be rejected because the plot overlapped blocked ground or a street, and each of those cost a footprint rasterisation and two set tests.

### 1.13 Fortifications (`build_fortifications`)

`wall_def, gate_def = wall_and_gate_defs(preset)`:

```
structures = {row.structure_id: row for row in preset.infrastructure}
buildings  = {row.structure_id: row for row in preset.buildings}
wall = structures.get('building.wall')      or buildings.get('building.wall')
gate = buildings.get('building.gatehouse')  or structures.get('building.gatehouse')
if wall and 'dimensions_m' not in wall:
    wall = {**wall, dimensions_m: {width:3, depth:10, height:7}, id: wall.structure_id or 'building.wall'}
elif wall: wall = {**wall, id: wall.structure_id or wall.id or 'building.wall'}
if gate:   gate = {**gate, id: gate.structure_id or gate.id or 'building.gatehouse'}
```

The 3×10×7 fallback is the catalogue dimension, used only when an infrastructure row omits measured fields.

`gate_budget = count of the 'building.gatehouse' preset row (0 if absent)`; `tower_budget = count of the 'building.tower' row (0 if absent)`. The fortifier is called with `gate_budget = max(1, gate_budget)`, so a wall always gets at least one opening even when the preset lists no gatehouse.

**Ring policy** (`ring_count_for`):

```
shape_id == 'concentric_enceintes' -> clamp(int(parameters.get('ring_count', 3)), 2, 3)
city_class in {'medium','capital'} -> 1
otherwise ('small')                -> 0
```

**Scales:** one ring → `[1.0]`; otherwise `[0.45 + i/(rings-1)*0.55 for i in range(rings)]`, i.e. `[0.45, 1.0]` for two and `[0.45, 0.725, 1.0]` for three. Roles: index 0 is `inner`, the last is `outer`, anything between is `middle` (with one ring, index 0 is `outer`).

`segment_depth = wall.dimensions_m.depth` (10 m).

If `rings_wanted <= 0`, or `valid` is empty, or there is no wall structure: return the empty report, adding an `unplaced` row `{building_id, reason: 'No buildable perimeter for fortification'}` only when rings were wanted.

For each ring, in scale order:

**(a) `_boundary_points(valid, half, cell, rx, rz, scale, family)`** — 48 angular rays from the centroid of the valid **cell indices**:

```
cx = mean of cell i over valid;  cz = mean of cell j over valid       # index units, not metres
for k in 0..47:
    angle = 2*pi*k/48;  (dx, dz) = (cos angle, sin angle)
    best = None
    for dist in 1 .. max(1, int(max(rx, rz)/cell) + 2):               # step of one cell
        cell_i = (floor(cx + dx*dist), floor(cz + dz*dist))
        if cell_i not in valid: break
        (ex, ez) = metre centre of cell_i
        grid|compound|hybrid: break if |ex| > rx*scale or |ez| > rz*scale
        cluster:              break if (ex,ez) is outside ALL three discs of radius half*0.52*scale
        else:                 break if (ex/(rx*scale))² + (ez/(rz*scale))² >= 1
        best = (round(ex,2), round(ez,2))
    if best: append best
```

The result is the outermost valid cell on each ray, clipped to the scaled shape, in increasing-angle order. Up to 48 points; fewer when a ray dies at `dist == 1`.

**(b) `smooth_closed_ring(points, max_turn_degrees=95, radius_factor=1.12, passes=12)`** — *pull in needle vertices so an enceinte cannot form one giant outward point.*

If fewer than 8 points, return them unchanged. Otherwise take the centroid `(cx, cz)` **once**, then run 12 passes; each pass is two **Jacobi** (simultaneous, not in-place) sweeps:

```
radial cap:   radii[i] = hypot(p[i] - c) or 1.0
              cap = 1.12 * max(radii[i-1], radii[i+1])          # indices wrap
              r   = min(radii[i], cap)
              p[i] = c + (p[i] - c)/radii[i] * r
corner cap:   if |signed_turn(p[i-1], p[i], p[i+1])| > 95 degrees:
                  p[i] = (p[i-1] + 2*p[i] + p[i+1]) / 4
```

`signed_turn(a,b,c) = degrees(atan2(c.z-b.z, c.x-b.x) - atan2(b.z-a.z, b.x-a.x))`, normalised into `(-180, 180]` by adding 360 while `<= -180` and subtracting 360 while `> 180`.

Return the points rounded to 2 dp. Note the `or 1.0` guard: a vertex exactly at the centroid gets radius 1.0 for the division, so it collapses onto the centroid rather than dividing by zero.

If the smoothed ring has fewer than 8 points, record `unplaced` `{building_id, ring_id, reason: 'Perimeter could not close on buildable land'}` and skip the ring.

**(c) `_segmentize(polyline, segment_depth, structure_id, ring_id)`** — walks the *closed* polyline (vertices + first vertex again) laying down wall segments of nominal length `segment_depth`:

```
nodes = one per polyline vertex: {id: f'{ring}-node-{i}', kind:'corner', position_m, ring_id}
seg_i = 0; carry = 0.0; path = []
for (a, b) in consecutive pairs of the closed polyline:
    path.append(a)
    edge = dist(a, b)
    while carry + edge >= segment_depth - 0.01:
        need  = segment_depth - carry
        t     = need/edge if edge else 0
        end   = round(a + (b-a)*t, 2)   componentwise
        start = path[-1] if path else a
        emit segment { id: f'{ring}-seg-{seg_i}', ring_id, structure_id,
                       from_m: start, to_m: end,
                       center_m: round(midpoint, 2),
                       length_m: round(dist(start, end), 3),
                       rotation_degrees: round(degrees(atan2(end.z-start.z, end.x-start.x)) - 90, 4) }
        seg_i += 1;  path = [end];  a = end;  edge = dist(a, b);  carry = 0.0
    carry += edge
```

`rotation_degrees` is the heading **minus 90** because a wall module's depth axis runs along the wall. Because `path.append(a)` at the top of each outer iteration overwrites the leftover point with the polyline vertex, the first segment on each edge starts at the *vertex* and is shorter than `segment_depth` by the carried remainder — segment lengths therefore vary in `[0, 2*segment_depth]`. Keep this; the test that guards it only asserts `|length - 10| <= 10`.

`carry` can never reach `segment_depth - 0.01` at loop entry, so the while loop always terminates even on a zero-length edge.

If fewer than 6 segments came out, record `unplaced` `{... 'Insufficient continuous wall segments'}` and skip the ring.

**(d) `_gate_sites(polyline, road_connections, gate_budget)`** — only the **outer** ring sees the real road connections; inner and middle rings are passed an empty list and a budget of `max(1, gate_budget//2)`.

```
if not polyline or gate_budget <= 0: return []
for each connection with status == 'connected' and a gate_local_m:
    best = argmin_i dist(polyline[i], gate)
    candidates.append( (dist(polyline[best], gate), best, connection.route_index, gate) )
candidates.sort()                       # lexicographic on the whole tuple
chosen = []; used = set()
for (dist, idx, route, gate) in candidates:
    if idx in used or dist > 48: continue
    used.add(idx)
    chosen.append({ring_vertex: idx, route_index: route,
                   position_m: polyline[idx], approach_m: gate})
    if len(chosen) >= gate_budget: break
if not chosen and polyline:
    # Ensure at least one schematic gate on the primary approach axis.
    idx = argmin_i ( |polyline[i].z| + |polyline[i].x - max_x| )
    chosen.append({ring_vertex: idx, route_index: None,
                   position_m: polyline[idx], approach_m: polyline[idx]})
return chosen[:gate_budget]
```

`48` m is the maximum distance a regional approach may sit from a ring vertex and still open a gate there. The fallback puts the gate on the easternmost vertex nearest the `z = 0` axis, with the approach equal to the gate itself.

Each chosen site becomes a record `{id: f'{ring}-gate-{k}', ring_id, structure_id: gate_def.id or 'building.gatehouse', position_m, approach_m, route_index}`.

**(e) Drop colliding wall segments.** Keep a segment only if its `center_m` is at least `max(8, segment_depth*0.6)` from **every** gate centre on the ring. With a 10 m segment that threshold is 8 m.

**(f) Towers.** `T = tower_budget` on the outer ring, `max(1, tower_budget//2)` elsewhere; `step = max(1, len(nodes) // max(1, T))`; take `nodes[::step]` truncated to `max(1, tower_budget)` on the outer ring and `2` elsewhere. Emit `{id: f'{ring}-tower-{k}', ring_id, structure_id: 'building.tower', position_m: node.position_m}`.

**(g)** Append the ring record with `status: 'closed'`, extend `segments` / `nodes` / `gates` / `towers`.

`defended_perimeter = any ring closed`. `passes['fortification'].placed = len(report.segments)` (the kept ones).

### 1.14 Street anchors

```
anchors = []
for path in street_paths_m:
    for i in 0 .. len(path)-1:
        a = path[max(0, i-3)];  b = path[min(len(path)-1, i+3)]
        if a == b: continue
        (x, z) = path[i]
        angle = atan2(b.z - a.z, b.x - a.x)
        angle += radians(5 * sin(x*0.13 + z*0.17 + (seed % 1000)))
        anchors.append((x, z, angle))
anchors = sorted(set(anchors), key = lambda p: (p.x² + p.z², p))
```

The ±3-step window gives a locally smoothed street heading; the ±5° sinusoidal term is *stable frontage variation following the street, with modest organic deviations* — deterministic in position and the street seed, not random per call. Anchors are de-duplicated on the exact triple and ordered **centre-outward**, tie-broken by the tuple. That order is the placement priority for the whole city: the first buildings to be installed take the most central frontage.

### 1.15 The candidate stream

For each plot size `(w, d)` a lazily-generated, **memoised** stream of legal positions. The memo (`_Replay`) exists because the eager list built the whole anchor sweep for every plot size while `install` takes the first free option and stops: between 22 % and 61 % of each list was built and never looked at. Yielding on demand and keeping what was yielded leaves the order and the first match identical — **no plot moves** — and simply stops producing options past the deepest one anybody asked for. A second iteration replays the memo first, then resumes the generator.

```
for (ax, az, angle) in anchors:
    for side in (1, -1):
        dx = -sin(angle)*side;  dz = cos(angle)*side          # street normal
        x = round(ax + dx*(d/2 + 10), 2)                      # 10 m street half-width setback
        z = round(az + dz*(d/2 + 10), 2)
        degrees = round(degrees_of(angle), 4)
        angle   = radians(degrees)                            # <-- see ordering_hazards

        # (1) clearance prefilter
        ci = int(floor((x + half)/4));  cj = int(floor((z + half)/4))
        if 0 <= ci < size and 0 <= cj < size
           and (clearance[cj][ci] + 1)*4*sqrt(2) <= min(w, d)/2:
            continue

        # (2) the footprint must be entirely on shape-valid ground and off every street
        footprint = footprint_cells(x, z, w, d, angle, half, 4)
        if not (footprint <= valid) or (footprint & road): continue

        # (3) the actual interpolated ground must be flat enough
        ground = [height_at(px, pz) for (px, pz) in corners(x, z, w, d, angle)]
        ground += [heights[c] for c in footprint]
        if max(ground) - min(ground) > hypot(w, d) * tan(radians(25)): continue

        # (4) an access corridor from the street anchor to the plot's near edge
        ex = x - dx*d/2;  ez = z - dz*d/2
        corridor = cached_or_build(ax, az, ex, ez)
        if corridor <= valid: yield (x, z, degrees, ax, az, footprint, corridor, ground)
```

**Prefilter soundness (record this argument).** `clearance[cj][ci] = k` means the nearest non-free cell is `k` cells away in Chebyshev terms. The plot centre lies inside cell `(ci, cj)`, so that cell's centre is within `2` m of it on each axis, and the blocked cell's centre is within `(k + 0.5)*4` m on each axis — the code's `(k+1)*4` is deliberately looser. Euclidean distance is therefore at most `(k+1)*4*sqrt(2)`. When that is `<= min(w, d)/2`, the blocked centre lies inside the rectangle's **inscribed circle**, hence inside the rectangle, hence inside `footprint_cells` (whose test is a dilated superset of "centre inside the rectangle"). Tests (2) is then already decided as a rejection, so the rasterisation is skipped. *It can never fire on a candidate the tests would have accepted.* I verified this empirically as well: on a heterogeneous 17-node fixture across all three classes, the prefilter fired 33 359 times and **zero** of those candidates would have been accepted; disabling it produced byte-identical plans for all three classes.

**Corridor.** Eleven 4×4 axis-aligned footprints sampled at `t/10` for `t` in `0..10` along the segment from `(ax, az)` to `(ex, ez)`, unioned. Keyed and cached on `(ax, az, ex, ez)`: *an access corridor depends only on its two endpoints, so every building that shares a plot depth reuses the same one. Rebuilding it per (width, depth) made the eleven-step rasterization the hottest call in city planning.* The corridor must lie inside `valid` but **may** overlap `road` — that is the point of it.

The slope test (3) mixes two sources deliberately: the four rotated corner elevations sampled from the continuous height field, plus the discrete cell elevations already sampled for every footprint cell. Only `max()` and `min()` are read, so the list is never sorted.

#### `corners(x, z, w, d, angle)`

`c = cos(angle)`, `s = sin(angle)`; returns the four points `(x + u*c - v*s, z + u*s + v*c)` for `(u, v)` in `(-w/2,-d/2), (w/2,-d/2), (w/2,d/2), (-w/2,d/2)`.

#### `footprint_cells(x, z, w, d, angle, half, cell=4)`

Called roughly a million times per world.

```
points = corners(...);  c = cos(angle);  s = sin(angle)
lo[k]  = floor((min over points of coordinate k + half)/cell)     for k in (0,1)
hi[k]  = floor((max over points of coordinate k + half)/cell)
r      = cell/2 * (|c| + |s|)          # half-extent of an axis-aligned cell on a rotated axis
half_w = w/2 + r - 1e-8;  half_d = d/2 + r - 1e-8
for j in lo[1]..hi[1]:
    dz = -half + (j+0.5)*cell - z
    for i in lo[0]..hi[0]:
        dx = -half + (i+0.5)*cell - x
        if |dx*c + dz*s| < half_w and |-dx*s + dz*c| < half_d: add (i, j)
```

This is the Minkowski dilation of the rotated rectangle by an axis-aligned cell: a cell is in the footprint if the *square* overlaps the rectangle, not merely if its centre does. The `1e-8` excludes exact-boundary touches.

**Cells outside `[0, size)` are produced and not clamped** — that is deliberate. A plot hanging over the raster edge yields cells that are not in `valid`, so test (2) rejects it. Any C++ port that clamps here silently admits plots off the edge of the world.

### 1.16 Fortification plots (phase `fortification`)

Run **before** ordinary packing, so the enceinte openings are reserved first.

**Gatehouses** — only if `defended_perimeter` and `gate_def` exists and has a `plot_m`. For each gate on every ring, in report order:

```
(x, z) = gate.position_m;  approach = gate.approach_m or [x+1, z]
angle   = atan2(approach.z - z, approach.x - x);  degrees = round(degrees_of(angle), 4)
footprint = footprint_cells(x, z, gate.plot_m.width, gate.plot_m.depth, angle, half, 4)
ground    = [height_at(p) for p in corners(x, z, w, d, angle)]
ground   += ([heights[c] for c in sorted(footprint) if c in heights] or [height_at(x, z)])
install(gate_def, phase='fortification', kind='service',
        placement=(x, z, degrees, x, z, footprint, footprint, ground))
```

If it lands, `placed_gates += 1` and the gate record gains `plot_id`. **A placement passed explicitly to `install` skips tests (2) and (3) entirely** — only the occupancy tests apply — so a gatehouse can legally sit on blocked ground or on a street. That is intentional: the wall opening is where the wall says it is.

Note the `[...] or [...]` precedence: the `or` binds to the whole comprehension, so the single-sample fallback fires only when the comprehension is empty.

**Towers** — only if `defended_perimeter` and the preset has a `building.tower` row. For each tower in report order, stopping once `placed_towers >= tower_budget`:

```
(x, z) = tower.position_m;  degrees = 0.0
footprint = footprint_cells(x, z, tower.plot_m.width, tower.plot_m.depth, 0, half, 4)
if not (footprint <= valid) or (footprint & road) or (footprint & occupied): continue
ground = corner heights + [heights[c] for c in sorted(footprint)]
install(tower_row, 'fortification', 'service', (x, z, 0.0, x, z, footprint, footprint, ground))
```

Towers, unlike gates, **are** terrain-tested. Their corridor is the footprint itself, so they claim no access cells.

### 1.17 Placement conditions

```
conditions = {
  'usable_groundwater':           False,
  'water_collection_or_delivery': site.freshwater_distance_m is not None,
  'navigable_shore':              False,
  'flowing_water':                False,
  'reliable_wind':                False,
  'ore_and_fuel_supply':          site.resource_potential (default 0) >= 0.5,
  'food_surplus':                 False,
  'working_animals':              False,
  'clay_and_fuel_supply':         False,
  'medicinal_supply':             facts.wooded_fraction > 0.1,
  'clear_sky_view':               True,
  'isolation_access':             True,
  'defended_perimeter':           fortifications.defended_perimeter }
```

Five further conditions exist in the registry vocabulary (`route_crossing`, `grade_change`, `retention_needed`, `farming_support`, `resource_support`) and are **absent** from this dict, so a lookup with a `False` default rejects them. Every hard-coded `False` is an honest "the planner has not measured this", not a claim that the condition is unmet.

### 1.18 Priority split

Walk `preset['buildings']` **in preset order**:

```
remaining = row.count
if row.structure_id == 'building.gatehouse': remaining = max(0, remaining - placed_gates)
if row.structure_id == 'building.tower':     remaining = max(0, remaining - placed_towers)
if remaining <= 0: continue
missing = [c for c in row.placement_conditions if not conditions.get(c, False)]
if missing:
    unplaced.append({building_id, count: remaining,
                     reason: 'Unverified prerequisite: ' + ', '.join(missing)})   # source order
    continue
(high if row.priority == 'core' else low).append({**row, count: remaining})
```

`'core'` goes to `high`; `'conditional'` and `'specialist'` both go to `low`.

### 1.19 Reservation, installation, housing

```
occupied  : set of cells covered by a placed building's plot
access    : set of cells covered by a placed building's access corridor only
reserved  : set of cells held for future worker housing
housing_reserve : ordered list of held candidate options
```

**`reserve_housing(worker_budget)`** — *hold street-front plots so later service packing cannot starve worker houses.*

```
need = max(0, ceil(max(0, worker_budget - sum(p.beds for p in plots)) / house.worker_beds)
                - len(housing_reserve))
for option in candidates(house.plot_m.width, house.plot_m.depth):
    if need <= 0: break
    footprint, corridor = option[5], option[6]
    if footprint & occupied or footprint & access or corridor & occupied: continue
    if footprint & reserved  or corridor & reserved:  continue
    reserved |= footprint;  reserved |= (corridor - footprint)
    housing_reserve.append(option);  need -= 1
```

(The `need <= 0` break is tested *after* the generator yields, so one extra candidate is materialised. Harmless because the stream is memoised, but it changes the memo depth.)

**`install(row, phase, kind='service', placement=None)`**

```
w, d = row.plot_m.width, row.plot_m.depth
options = [placement] if placement else candidates(w, d)
for (x, z, degrees, ax, az, footprint, corridor, ground) in options:
    if footprint & occupied or footprint & access or corridor & occupied: continue
    if kind != 'housing' and (footprint & reserved or corridor & reserved): continue
    occupied |= footprint
    access   |= (corridor - footprint)
    reserved -= (footprint | corridor)
    if kind == 'housing' and this option is in housing_reserve: remove it from housing_reserve
    plots.append({ id: 'plot-' + str(len(plots)),
                   building_id: row.structure_id if present else row.id,
                   name, kind, phase, x_m: x, z_m: z, rotation_degrees: degrees,
                   ground_elevation_m: round(max(ground), 4),
                   foundation_bottom_m: round(min(ground), 4),
                   plot_m, dimensions_m, road_access: [ax, az],
                   workers: sum(r.target for r in row.staffing.roles),
                   beds: row.worker_beds or 0 })
    passes[index of phase].placed += 1
    return True
return False
```

A corridor may cross another building's corridor (`access` is only tested against the new *footprint*, never against the new corridor) but never another building's plot. Housing is the one kind allowed to take reserved ground — the reservations exist for it.

**`house_workers(phase)`**

```
workers = sum(p.workers);  beds = sum(p.beds)
audit = {phase, workers, starting_beds: beds, houses_placed: 0, upgrades: 0,
         plots_exhausted: False, reserved_slots: len(housing_reserve)}
while beds < workers:
    placement = housing_reserve[0] if housing_reserve else None
    if not install(house, phase, 'housing', placement):
        if placement:                       # reserved slot became unusable
            housing_reserve.pop(0)
            reserved -= (placement.footprint | placement.corridor)
            continue
        audit.plots_exhausted = True;  break
    audit.houses_placed += 1
    beds += house.worker_beds

# Replace existing dwellings IN PLACE: access and occupied land stay valid.
for plot in plots:                          # insertion order
    if beds >= workers: break
    if plot.building_id != house.id: continue
    audit.upgrades += 1
    plot.setdefault('housing_upgrade',
        {phase, previous: {building_id, name, dimensions_m, beds}})
    beds += apartment.worker_beds - plot.beds
    plot.building_id = apartment.id; plot.name = apartment.name
    plot.dimensions_m = apartment.dimensions_m; plot.beds = apartment.worker_beds

# Unused housing holds are released so later lower-priority services may use the land.
while housing_reserve:
    option = housing_reserve.pop()          # from the END
    reserved -= (option.footprint | option.corridor)

audit.final_beds = beds;  audit.shortfall = max(0, workers - beds)
debug.housing_passes.append(audit)
```

The apartment has the same plot as the house, which is exactly why densification can happen in place without re-testing terrain or corridors. Policy string: `houses_first_then_upgrade_on_plot_exhaustion`.

**The two priority phases**

```
for (priority, rows) in (('high', high), ('low', low)):
    if priority == 'low' and sum(p.workers - p.beds for p in plots) > 0:
        unplaced.extend({building_id, count, reason:
            'High-priority worker housing must be completed first'} for each row in rows)
        break
    demand  = sum(p.workers for p in plots)
    demand += sum(sum(r.target for r in row.staffing.roles) * row.count for row in rows)
    reserve_housing(demand)
    debug.housing_reservations.append({phase: priority, workers_budgeted: demand,
                                       slots: len(housing_reserve)})
    for row in rows:
        failed = 0
        for _ in range(row.count):
            if not install(row, priority): failed += 1
        if failed: unplaced.append({building_id, count: failed,
            reason: 'No terrain-safe plot with road access remaining'})
    house_workers(priority + '_housing')
```

### 1.20 Close out

```
workers = sum(p.workers);  beds = sum(p.beds)
debug.shape_safe_cells   = len(valid)
debug.road_cells         = len(road)
debug.occupied_cells     = len(occupied)
debug.access_cells       = len(access)
debug.vacant_shape_cells = len(valid - road - occupied - access)
debug.housing_frontage_candidates = free_frontage(house.plot_m.width, house.plot_m.depth)
debug.fortification_rings = len(fortifications.rings)
debug.program_half_m      = half

core_ids    = {row.structure_id for row in preset.buildings if row.priority == 'core'}
failed_core = sum(r.count for r in unplaced if r.building_id in core_ids)
status      = 'complete' if (failed_core == 0 and beds >= workers) else 'partial'
```

`free_frontage(w, d)` counts every memoised candidate that is not blocked by `occupied`, `access` or `reserved`. It **forces full enumeration of the stream** for the house plot size — it is a debug figure that costs the whole sweep, and a C++ port should either keep the cost or make the field optional deliberately.

`stats` is assembled as listed in **writes**.

## Constants

**`city_planner`**

| constant | value | meaning / effect of change |
|---|---|---|
| `VERSION` | `8` | Emitted on the block and every plan, and asserted by `docs/conformance/settlement-records.md` as `city-planner=8`. Bumping it invalidates every saved world through the identity check. |
| `CELL` | `4` m | City raster cell size. Halving it quadruples the raster and the candidate sweep and doubles plot-position resolution. `grow_roads` has a **separate hardcoded 4** for edge length — they must be changed together. |
| `_ROOT2` | `sqrt(2)` | Chebyshev→Euclidean conversion in the clearance prefilter. |
| `_MIN_SITES_FOR_POOL` | `3` | Fewer sites than this never fork a pool. |
| class base half-extent | `small 240`, `medium 320`, `capital 400` m | Used twice: as the `neighbour_half` start and as `base` in `program_half_m`. With the shipped registry, `base` always wins over the demand term (see below), so these three numbers alone set every city's crop size. |
| neighbour factor | `0.28` | `neighbour_half = min(neighbour_half, great_circle_distance * 0.28)`. Two adjacent cities leave a 44 % gap. **Absolute-metre reach, applied before the half is truncated to a multiple of 4.** |
| slope block threshold | `25` degrees | A cell is code 2 above this. Also the per-plot ground-relief limit via `tan(25 deg) = 0.4663` and the road-junction gate. |
| plot relief limit | `hypot(w,d) * tan(radians(25))` | Max elevation spread over a plot's corners and cells. |
| river channel | `28` m | Half-width of the reserved river channel measured from a routed centreline; blocks a cell. Emitted in warning text as "24 m channel and 28 m setback". |
| river water | `12` m | Within this of a routed centreline the point counts as water outright. |
| river AABB pad | `40` m | Segments whose bounding box misses `[-half-40, half+40]` are dropped. |
| gnomonic cut-off | `den <= 0.1` | A river node more than ~84° from the site projects to infinity and is dropped. |
| street setback | `10` m | Plot centre sits `d/2 + 10` from the street anchor: a 10 m street half-width. |
| corridor width | `4 × 4` m at 11 samples | The access spur from the anchor to the plot's near edge. |
| frontage jitter | `5 * sin(x*0.13 + z*0.17 + (seed % 1000))` degrees | Deterministic ±5° organic deviation on the street heading. |
| shape clip | `rx = half*0.94`, `rz = half*0.94/min(aspect,2)` | The 0.94 leaves a 6 % margin; the `min(aspect, 2)` caps how thin an elongated city can get. |
| cluster clip | three discs, radius `half*0.52`, centres `(-half*0.32, 0)`, `(half*0.32, 0)`, `(0, half*0.25)` | |
| street spacing | `max(72, block_spacing_m)` m | The catalogue's `block_spacing_m` ranges 35–90, so the 72 floor binds for most shapes. `spacing/4` becomes `spacing_cells`. |
| junction grade | `0.35 * length + 1e-6` | Max elevation step between a street cell centre and the regional gate point. |
| biome "wooded" set | `{4, 7, 15}` | `wooded_fraction` counter; also the `medicinal_supply` condition via `> 0.1`. |
| `medicinal_supply` | `wooded_fraction > 0.1` | |
| `ore_and_fuel_supply` | `resource_potential >= 0.5` | |
| slope sentinel | `90` degrees | `local_slope_degrees` when no cell is buildable. |
| moisture default | `0.5` | Yields `aridity = 0.5` on a world with no moisture layer. |
| biome default | `3` | |
| variant default | `-1` | |
| `planner_identity()` | `{version: 8, registry: registry_identity(), shapes_sha256: sha256(json.dumps(catalogue, sort_keys=True, separators=(',',':')))}` | The saved-world gate. **`registry_identity()` uses a different canonicalisation** (`ensure_ascii=False`, no `sort_keys`) — two different canonical-JSON conventions in one dict. |

**`city_fortifications`**

| constant | value | meaning |
|---|---|---|
| `VERSION` | `1` | on the report |
| `program_half_m` overhead | `2.4` | "Streets, wall reserve and access corridors roughly double service/housing land" — 2.4 is that doubling plus the wall reserve. |
| `program_half_m` slack | `1.15` | 15 % slack on the half-edge. |
| `program_half_m` quantum | `max(4, int(half/4)*4)` | Truncate toward zero to a multiple of `CELL`; floor of 4 m. |
| ring scales | `[1.0]` or `0.45 + i/(rings-1)*0.55` | 2 rings → `[0.45, 1.0]`; 3 → `[0.45, 0.725, 1.0]`. |
| ring count | 2..3 for `concentric_enceintes`, 1 for medium/capital, 0 for small | |
| boundary rays | `48` | Angular samples; also the maximum vertex count of a ring. |
| ray reach | `max(1, int(max(rx,rz)/cell) + 2)` cells | |
| minimum ring | `8` points, `6` segments | Below either, the ring is `unplaced`. |
| `smooth_closed_ring` | `max_turn_degrees=95`, `radius_factor=1.12`, `passes=12` | Raising `radius_factor` admits sharper spikes; lowering `passes` leaves needles; raising `max_turn_degrees` above ~120 disables corner smoothing entirely. |
| wall fallback dims | `{width: 3, depth: 10, height: 7}` m | Only when an infrastructure row omits `dimensions_m`. `depth` is the nominal wall segment length. |
| gate collision radius | `max(8, segment_depth*0.6)` m | Segments within this of a gate centre are dropped so the opening is real. |
| gate snap distance | `48` m | A connected regional approach farther than this from any ring vertex opens no gate. |
| inner-ring budgets | `max(1, gate_budget//2)` gates, `max(1, tower_budget//2)` towers capped at `2` | |
| segment rotation | `heading - 90` degrees | A wall module's depth axis runs along the wall. |

**`city_geometry.grow_roads`**

| constant | value | meaning |
|---|---|---|
| neighbour order | `(-1,0),(0,-1),(0,1),(1,0),(-1,-1),(-1,1),(1,-1),(1,1)` | Load-bearing through heap tie-breaks. |
| edge length | `hypot(dx,dz) * 4` | **A hardcoded 4 m**, not the caller's cell size. |
| cost field | `1 + 0.45*(1 + sin(x/9+p0)*cos(z/11+p1)) + 0.3*(1 + sin((x+z)/17+p2))` | Range `[0.25, 2.05]`. Wavelengths 9, 11, 17 cells (36, 44, 68 m) give persistent bends rather than white-noise zigzags. |
| cliff step | `1.4` m | Two thresholds use it: diagonal admissibility (all four support-cell height differences) and the one-cell orthogonal road widening. |
| max grade | `0.35` | A link steeper than this is rejected outright. |
| grade penalty | `18 * grade²` | At the 0.35 ceiling this adds 2.205, about the full span of the cost field. |
| branch count | `max(branch_floor, min(36, round(len(valid)/max(12, spacing_cells)² * 1.5)))` | |
| `branch_floor` | `12` | Never overridden by the city planner. |
| candidate pool | `max(256, count*20)` | |

**`city_shapes`**

| constant | value | meaning |
|---|---|---|
| schema | `fantasy-world-generator.city-shapes`, `schema_version` must be exactly `1` | |
| `revision` | `2` (shipped) | |
| `selection.algorithm` | must be exactly `sha256_weighted_v1` | |
| `repetition_penalty` | `0.75` | `weight /= (1 + seen_count * 0.75)`. A shape seen once is worth 4/7 of its base; twice, 2/5. |
| `required_site_fields` | `buildable_area_m2`, `usable_land_fraction`, `local_slope_degrees` | The site dict must carry all three or `_rank` raises. |
| base weights | `10` for every shape except `concentric_enceintes` at `6` | |
| preference bonus | `8` for every preference except `concentric_enceintes / regional_threat` at `18` | |
| eligibility floors | `buildable_area_m2 >= 12000`, `usable_land_fraction >= 0.15`, `local_slope_degrees <= 35` on every shape; `concentric_enceintes` needs `>= 40000`, `>= 0.2`, `<= 25` | |
| extra slope caps | `market_grid <= 8`, `walled_compounds <= 10`, `courtyard_lanes <= 15`, `lagoon_network <= 5`, `planned_extension <= 8` | |
| unit draw | `sha256(json([seed, city_id, identity, label]))[:8] as big-endian u64 / 2**64` | `label` is `'shape'` for the roulette and `'<shape_id>:<param>'` for each parameter. |
| `city_id` length | 1..256 characters | validated |
| `nearby_counts` values | ints `0..10000`, keys must be known shape ids | validated |

**Registry figures the planner depends on** (`civilizations.json`, `human_heartland`, indicative):

| | small_city | medium_city | capital_city |
|---|---|---|---|
| building rows / instances | 28 / 29 | 41 / 58 | 45 / 82 |
| core instances | 27 | 42 | 58 |
| staffed workers | 86 | 188 | 285 |
| raw plot area (m²) | 13 195 | 22 972 | 31 945 |
| gatehouse / tower counts | none | 2 / 2 | 4 / 4 |
| `program_half_m` demand term `needed` | ~135 m | ~168 m | ~191 m |
| class `base` | 240 m | 320 m | 400 m |

**The demand term never binds.** `needed` is well below `base` for all three classes, so with the shipped registry `program_half_m` always returns `min(neighbour_half, base)` truncated to a multiple of 4. The `sqrt(plot_area*2.4)/2*1.15` branch is live code that has no effect on any shipped world; it would only bind if a preset's programme grew roughly fourfold. Record this rather than dropping it — it is a designed growth path, not dead code, but a C++ port that gets it wrong will not be caught by any current world.

`worker_house`: 8×10×6 building on a 12×16 plot, 4 beds. `worker_apartment`: 10×12×12 on the **same** 12×16 plot, 16 beds. `hamlet_house` exists but is not used here.

## Data files

**`Sim/icarus_sim/city_shapes.json`** — the shape catalogue, loaded by `city_shapes.load_catalogue()`.

Loading is `json.loads(..., object_pairs_hook=<reject duplicate keys>)`, validated by `validate_catalogue`, cached on `(resolved path, mtime_ns, size)` with an LRU of 4, and **deep-copied on every call** so a caller cannot mutate the cache.

Top-level shape (shipped `revision: 2`, `schema_version: 1`):

```
{ "schema": "fantasy-world-generator.city-shapes",
  "schema_version": 1, "revision": 2,
  "status": ..., "units": ..., "research_date": ..., "numeric_parameters_status": ...,
  "selection": { "algorithm": "sha256_weighted_v1",
                 "required_site_fields": ["buildable_area_m2","usable_land_fraction","local_slope_degrees"],
                 "repetition_penalty": 0.75, "no_match": <string>, "notes": <string> },
  "site_features": { "<name>": {"type":"boolean","missing":<string>}
                   | {"type":"number","min":..,"max":..,"unit":..,"description"?:..} },
  "sources": { "<id>": {"url": "https://...", "supports": <nonempty>} },
  "shapes": [ ... ] }
```

`site_features` holds **22 features**: eleven booleans (`coast_access`, `navigable_water`, `river_access`, `river_bend`, `bridge_feasible`, `island`, `peninsula`, `lagoon`, `ridge`, `hill`, `oasis`), each documenting `missing: "fails a hard requirement; no preference bonus"`; and eleven numbers (`usable_land_fraction`, `corridor_constraint`, `aridity`, `wooded_fraction`, `defense_priority`, `regional_threat`, `planned_foundation`, `trade_intensity` all on 0..1; `local_slope_degrees` 0..90; `buildable_area_m2` 0..1e12).

A `shape` entry:

```
{ "id": "organic_market", "name": ..., "family": "organic",
  "enabled_by_default": true,
  "city_classes": ["small","medium","capital"],       # subset of those three, nonempty
  "base_weight": 10,                                  # 0 < w <= 1000
  "eligibility": { "all": [ {"field":"buildable_area_m2","min":12000}, ... ] },
  "preferences": [ {"when": {"field":"trade_intensity","min":0.5}, "bonus": 8} ],
  "layout": { "footprint", "street_network", "primary_anchor",
              "housing_fill", "building_fit", "guild_hall", "orientation" },  # prose, copied verbatim
  "variation": { "aspect_ratio": {"min":1,"max":1.6},
                 "street_curvature": {...}, "block_spacing_m": {...},
                 "plaza_offset_fraction": {...},
                 "district_count": {"min":1,"max":3,"integer":true} },
  "history": { "source_ids": [...] },                 # must all exist in `sources`
  "placement_checks": [ ... ], "description": ..., "notes": ... }
```

A rule is a dict with `field` plus one or more of `min`, `max`, `equals`, with at least two keys total. Boolean features accept **only** `{field, equals: <bool>}`. Numeric rules must have finite values and `min <= max`.

**The 17 shipped shapes**, by `family`:

`organic_market` (organic), `market_grid` (grid), `courtyard_lanes` (organic), `ridge_fishbone` (linear), `hill_rings` (contour), `oasis_cluster` (cluster), `river_meander` (water_edge), `bridge_twin` (paired), `island_spines` (linear), `waterfront_comb` (water_edge), `peninsula_core` (water_edge), `lagoon_network` (archipelago), `citadel_lower` (paired), `ceremonial_clusters` (cluster), `walled_compounds` (compound), `concentric_enceintes` (rings, capital-only, `base_weight 6`, and the only shape with a `ring_count` variation), `planned_extension` (hybrid, `enabled_by_default: false`).

The planner's `inside()` switch only distinguishes `grid`/`compound`/`hybrid`, `cluster`, and everything else, so `organic`, `linear`, `contour`, `water_edge`, `paired`, `archipelago` and `rings` all share the elliptical clip.

**Only six of the seventeen shapes are reachable, and only five outside a capital.** The planner supplies exactly seven site features (`buildable_area_m2`, `usable_land_fraction`, `local_slope_degrees`, `aridity`, `wooded_fraction`, `regional_threat`, `defense_priority`) and a rule on an absent field never matches. Every shape whose eligibility names `ridge`, `hill`, `oasis`, `river_access`, `river_bend`, `bridge_feasible`, `island`, `coast_access`, `navigable_water`, `peninsula` or `lagoon` is therefore permanently ineligible, and `planned_extension` is excluded by `enabled_by_default: false`. What is left is:

| class | eligible shapes |
|---|---|
| small, medium | `ceremonial_clusters`, `courtyard_lanes`, `market_grid`, `organic_market`, `walled_compounds` |
| capital | the above plus `concentric_enceintes` |

Likewise, the `trade_intensity`, `planned_foundation` and `corridor_constraint` preference bonuses can never fire. The catalogue's `notes` field says why: *"Location summaries must be supplied by a future footprint-analysis adapter."* The adapter was never written. **A C++ port should implement the whole rule engine anyway** — it is the extension point — but must not be surprised that eleven shapes and several bonuses are unreachable with the current facts, and should not "fix" it by inventing landform booleans.

**`Sim/icarus_sim/civilizations.json`** — the civilization registry, reached through `civilization_registry.city_plan(entity_id, '<class>_city')` and `section('housing_profiles')`. Twelve entities (`human_maritime`, `human_desert`, `human_cold`, `human_large_island`, `human_rainforest`, `human_heartland`, `dwarf`, `elf`, `gnome`, `tidekin`, `hill_dwarf`, `frosthold_dwarf`) plus one aggregate configuration (`mixed`). `city_plan` merges each row of the class block over the structure definition from the entity's structure-block libraries, so a row carries both the authored fields (`count`, `name`, `priority`, `placement_conditions`) and the measured ones (`dimensions_m`, `clearance_m`, `plot_m`, `staffing`, `geometry_type`, `module_family`). The registry enforces `plot = dimensions + clearances`, that no city block contains housing (housing is deferred to the `housing_profiles` section), and that the apartment shares the house's plot with a strictly larger bed count.

**Provenance note for the porter:** the memory rule *"bytes in civilizations.json invalidate every saved world"* applies — the registry's SHA-256 is part of `planner_identity()`, so changing a single preset row makes every previously generated world refuse to load.

## Random streams

There are **three** independent randomness sources, and none of them is the world's master RNG.

**1. Shape selection — a pure hash, no stream.** `city_shapes.select_shape` draws nothing. Every value is
`u = int.from_bytes(sha256(json.dumps([seed, city_id, identity, label], separators=(',',':')).encode()).digest()[:8], 'big') / 2**64`,
a uniform double in `[0, 1)`. Labels used: `'shape'` once for the roulette, then `'<shape_id>:<param>'` once per variation key, in **sorted key order**. `identity` is the hex SHA-256 of the canonical catalogue JSON, so changing the catalogue re-rolls every city's shape.

Draw count: `1 + len(shape.variation)` per city, i.e. 6 for most shapes and 7 for `concentric_enceintes`. Data-independent given the chosen shape.

**2. Street growth — one seeded stream per city.** `grow_roads` builds `random.Random(street_seed)` where
`street_seed = int.from_bytes(sha256(f"{config.seed}:{city_uid}:streets-v2".encode()).digest()[:8], 'big')`.
The `:streets-v2` suffix is a domain tag; it must not be dropped, or a street layout collides with whatever else salts on the same pair.

Draws, in order:
- three `uniform(-pi, pi)` for the cost-field phases — **always three, unconditionally**;
- one `sample(reachable, k)` with `k = min(len(reachable), max(256, count*20))`, drawn without replacement in draw order.

The `sample` call is the hazard. Python's `random.sample` switches between a selection-set algorithm and a pool algorithm depending on `k` relative to `n`, and the two consume different amounts of stream — the FWGC++ `Rng::sample_indices` deliberately uses **one** algorithm at every size for exactly this reason. The draw count is therefore data-dependent (`len(reachable)` depends on terrain), but the *number of subsequent draws is zero*, so the divergence is contained: nothing is drawn after the sample.

The farthest-point branch selection that follows is fully deterministic — `max()` with an explicit tie-break, no draws.

**3. Nothing else draws.** Plot placement, clearance, fortification geometry, ring smoothing, segmentisation and gate siting are all deterministic functions of the raster, the anchors and the shape parameters. The ±5° frontage jitter is `sin(x*0.13 + z*0.17 + (street_seed % 1000))` — a deterministic function of position and the street seed, not a stream draw.

**For the C++ port:** use `Rng::child(master, "city-streets", <city index>)` or a domain string carrying the city uid — do **not** invent a fourth seed derivation. The three uniform phase draws must be taken unconditionally and in order even when the raster is empty… except that `grow_roads` returns before constructing the RNG when `valid` is empty, so an empty city consumes nothing. Keep that early return.

## Ordering hazards

Every place where container order or iteration order reaches the output. All of these are load-bearing.

1. **`fill_cities` sorts sites by `str(uid)`** — lexicographic on the string form. `"city-10"` sorts before `"city-2"`. This order determines which city sees which shape-repetition counts, so it changes shape choices, not just the order of the output array.

2. **`_rank` sorts the catalogue by `shape['id']`** before ranking, and the roulette walks that sorted list. Change the sort and every city's shape changes.

3. **`select_shape` iterates `sorted(shape['variation'].items())`** so parameter labels are hashed in a fixed order. (The labels are unique so the *values* do not depend on order, but the emitted dict's insertion order does, and that reaches the world JSON.)

4. **`sorted(set(anchors), key=lambda p: (p[0]**2 + p[1]**2, p))`** — the master placement priority. Two anchors at the same squared radius fall back to lexicographic `(x, z, angle)` tuple comparison on floats. This single ordering decides which building gets the most central frontage in the entire city.

5. **`_stream` iterates anchors in that order, then `for side in (1, -1)`** — the street's left side is always tried before its right.

6. **`_Replay` memo order** — candidates are handed out in generation order and replayed in the same order for every consumer of the same `(w, d)`. Two different plot sizes have independent streams.

7. **`preset['buildings']` source order** survives into `high` and `low`, into the per-row installation loop and into the `unplaced` array. The registry's authored row order is output.

8. **`', '.join(missing)`** preserves the source order of `placement_conditions` inside the `'Unverified prerequisite: ...'` string.

9. **`result['plots']` insertion order** is the plot id (`plot-<n>`), the upgrade scan order in `house_workers`, and the pairwise overlap test's order in the test suite.

10. **`housing_reserve` is a list used as a queue at the front (`[0]`, `pop(0)`) and as a stack at the back (`pop()`)** — reserved slots are *consumed* oldest-first and *released* newest-first. Getting either end wrong changes which land stays reserved.

11. **`option in housing_reserve` / `housing_reserve.remove(option)`** use deep tuple equality over sets and lists, not identity. Two structurally identical options at different anchors cannot occur (the position differs), but a C++ port should key on index or pointer, not on a deep compare.

12. **`grow_roads` neighbour order** `(-1,0),(0,-1),(0,1),(1,0),(-1,-1),(-1,1),(1,-1),(1,1)` reaches the output through heap insertion order on equal costs.

13. **The Dijkstra heap key is `(cost, cell)`** — a Python tuple, so ties break on the cell tuple. A C++ `priority_queue<pair<double, pair<int,int>>>` with the default comparator reproduces this only if you also make it a *min*-heap and compare the cell on cost ties.

14. **`sorted(valid)` for `heights` and `costs`**, **`sorted(distances)` for `reachable`**, **`sorted(set(required) & set(distances))` for the initial targets**, and **`sorted(network)` for the road-widening sweep** — four explicit sorts, all over `(x, z)` tuples, all load-bearing for the RNG sample and for float summation order.

15. **`rng.sample(reachable, k)` returns items in draw order**, and `max(candidates, key=...)` scans that order with a tie-break on the cell. `candidates.remove(target)` removes the *first* equal element.

16. **`targets` grows inside the farthest-point loop**, so `[root] + targets` is re-read each iteration and the greedy choice depends on the order targets were appended.

17. **`result['roads'] = sorted(road)`** and `[heights[c] for c in sorted(footprint)]` in the gate/tower ground lists — explicit sorts over otherwise-unordered sets, so float `max`/`min` see a deterministic sequence.

18. **`_boundary_points` walks rays `k = 0..47` in increasing angle**, producing the polyline in angular order; `smooth_closed_ring` and `_segmentize` both depend on that being a simple closed curve in order.

19. **`_gate_sites`: `candidates.sort()` sorts the whole tuple `(distance, vertex_index, route_index, gate_point)`.** `route_index` can be `None`. In Python 3 a tie on `(distance, vertex_index)` would then compare `None` against an `int` and raise `TypeError`. It cannot currently happen — `vertex_index` is unique per connection only if two connections snap to different vertices, and two connections snapping to the *same* vertex at *exactly* the same float distance is required for the collision — but a C++ port must define a total order here explicitly rather than reproducing a latent crash. Sort by `(distance, vertex_index, route_index_or_sentinel)`.

20. **`fortifications['gates']` / `['towers']` are iterated in report order** (ring by ring, gate index within ring) when reserving gatehouse and tower plots, and that order decides which gate wins when two are close enough to contest cells.

21. **`magic_colors` is built from `world['magic']['networks'].items()`** — dict insertion order reaches the emitted JSON.

22. **`world.get('climate', world.get('water', {})).get('river_segments', [])`** — the fallback is on the *block*, not the field. A world with a `climate` block that has no `river_segments` silently gets no river lines even when `water.river_segments` is populated. This is a latent behaviour change waiting for whoever adds a `climate` block without that key.

23. **`_stream` rebinds its own loop variable.** Inside `for (ax, az, angle) in anchors: for side in (1, -1):`, the line `angle = radians(round(degrees(angle), 4))` overwrites `angle` for the *second* side iteration. So `side = 1` computes its street-normal `(dx, dz)` from the raw anchor angle and its footprint from the round-tripped one, while `side = -1` uses the round-tripped angle for both. The difference is ~1e-9 radians and does not change any placement in practice, but it is an asymmetry between the two sides of every street and it is not what the code reads as. Reproduce it or do not, but know it is there: a port that "cleans it up" changes candidate positions at the last bit.

24. **`neighbour_half` guard**: `if other is site or other.get('uid') == site.get('uid'): continue`. If **no** site carries a `uid`, `None == None` is true for every pair and the neighbour bound is silently disabled for the whole world. Shipped worlds always have uids; test fixtures and hand-built worlds may not.

## Edge cases

**Empty or degenerate inputs**

- `valid` empty (every cell water or too steep): `facts.local_slope_degrees = 90`, `usable_land_fraction = 0`, `buildable_area_m2 = 0` → every shape fails the `>= 12000` floor → `shape_id` is `None` → the early-exit path fires, `status = 'unbuildable'`, `plots = []`, `fortifications = None`, and `unplaced` carries one row per preset building with `'No compatible buildable footprint'`. This is the covered case (`test_water_site_reports_failure_without_forced_buildings`).
- `wooded_fraction = woods / max(1, len(valid))` — the `max(1, ·)` is the only guard against division by zero, and it is reached only when `valid` is empty, in which case `woods` is also 0.
- `grow_roads` with empty `valid` returns `(∅, [])` **before** constructing the RNG, so an empty city consumes no stream.
- `grow_roads` where nothing is reachable from the root (the cliff fixture): `distances == {root: 0}`, `reachable == [root]`, `targets` may contain only `root`, every path has length 1 and is discarded, `paths == []`, `roads == {root}`. No anchors, no plots, but `status` is still computed from `failed_core`.
- `river_distance` with no lines returns `1e30`, so `distance < 12` and `distance < 28` are both false — and `channel` is additionally gated on `lines` being non-empty.
- `_segmentize` with a polyline shorter than 2 points returns `([], [])`; a zero-length edge sets `t = 0` and cannot loop forever because `carry` is always below `segment_depth - 0.01` on entry.
- `smooth_closed_ring` with fewer than 8 points returns the points unchanged (as tuples) without smoothing — and the caller then rejects the ring for being under 8 points.
- `smooth_closed_ring` with a vertex exactly at the centroid: `radii[i] = hypot(...) or 1.0` substitutes 1.0, the unit vector becomes `(0, 0)`, and the vertex is pinned at the centroid rather than dividing by zero.
- `_gate_sites` with an empty polyline or a non-positive budget returns `[]`. With a non-empty polyline and no qualifying connection, the fallback **always** produces exactly one gate — so an inner ring (which is passed no connections at all) always gets one.
- `_boundary_points` with empty `valid` returns `[]`.
- A preset with no `building.gatehouse` row gives `gate_budget = 0`, but `max(1, gate_budget)` is passed to the fortifier, so the wall still gets an opening; `gate_def` will be `None` (or lack `plot_m`) so no gatehouse plot is reserved, and the gate record falls back to `structure_id: 'building.gatehouse'`.
- A preset with no `building.tower` row: `tower_budget = 0`, `tower_row` is `None`, the tower reservation loop is skipped entirely, but `build_fortifications` still emits tower *records* (`max(1, tower_budget//2)` → step over nodes, `[:max(1, 0)] → [:1]` on the outer ring) — **so the report can contain tower records that no plot will ever back**. That is by design (the wall geometry is independent of the building programme) but a consumer must not assume `towers[].plot_id` exists.

**World edges and poles**

- The local frame is gnomonic about the site node. At a pole (`z = 0` or `z = n-1`), `lat = ±pi/2`, `cos(lat) = 0`, and `up = (0, ±1, 0)` while `east` and `north` remain well-defined unit vectors — the frame does not degenerate, but longitude is meaningless there and every raster column maps to the same physical point. `gx = round(...) % (n-1)` wraps correctly; `gz` is clamped to `[0, n-1]`.
- `gx` uses `% (n-1)`, not `% n` — column `n-1` duplicates column 0 in this raster convention, so the modulus is `n-1` and the duplicate column is never indexed.
- `local()` (river projection) returns `None` when `dot(p, up) <= 0.1`, i.e. beyond ~84° of arc, which is the horizon of a gnomonic projection. A river segment with one endpoint beyond that is dropped entirely, not clipped.
- `footprint_cells` produces cell indices outside `[0, size)` for a plot hanging over the crop edge; they are deliberately not clamped so the `footprint <= valid` test rejects the plot. **Do not clamp them in C++.**
- `_clearance` treats everything outside the raster as blocked, so plots cannot be pushed off the edge by the prefilter's absence.
- `road_entries` clips the regional route at the crop boundary by parameter `t = min over axes that exceed half` — the entry point is where the route first leaves `[-half, half]²`.

**Numerical guards**

- `acos(clamp(·, -1, 1))` in the neighbour-distance loop.
- `asin(clamp(p.y, -1, 1))` inside `HeightField._cell`; `_sampler.sample` does **not** clamp before `asin(p[1])` — `p` is normalised so `|p[1]| <= 1` up to rounding, and a tiny overshoot would raise `ValueError`. A C++ port should clamp.
- `(dx*dx + dz*dz) or 1` in the point-to-segment projection.
- `edge if edge else 0` in `_segmentize`.
- `max(1, ·)` on `wooded_fraction`, `beds_per_house`, ring steps, tower budgets and the ray reach.
- `1e-6` slack on the road-junction grade test; `1e-8` on the footprint boundary test.

**Status and degenerate programmes**

- `status = 'complete'` requires **both** zero unplaced core instances and `beds >= workers`. A city that placed every core building but ran out of housing land reports `'partial'` with a positive `stats.housing_shortfall`.
- `'unbuildable'` is emitted both for a city with no eligible shape and for a player-authored city — deliberately, so no consumer needs a new branch for authored towns.
- The `low` phase is skipped wholesale (and every low row goes to `unplaced` with `'High-priority worker housing must be completed first'`) if any placed plot still has `workers > beds` after the high housing pass.
- `house_workers` can loop repeatedly popping unusable reserved slots; it terminates because each `continue` removes one element from a finite `housing_reserve` and the `placement is None` branch breaks.

**Process pool**

- `ICARUS_CITY_WORKERS=1` (or `workers=1`) skips the pool outright. Fewer than three sites also skips it.
- On a spawn platform the pool re-imports the caller's `__main__`, so a caller that generates a world at import time instead of under `if __name__ == "__main__"` cannot use the pool. That is what the fallback is for. The fallback warns rather than failing.

## Reasoning worth preserving

These are the comments that record a measurement or a failed approach. They exist nowhere else and they are the most expensive content in these four files. Quoted verbatim where the wording carries the evidence.

**Why the regional flood proxy does not block a cell** (`city_planner.plan_city`, the raster loop):

> "Standing water, real steepness and the reserved river channel block a cell. The regional flood proxy does not, for the reason hamlet_planner already records: it is a coarse layer, not a local inundation mask. It is emitted as 0 or 1 and a city is smaller than one of its cells, so honouring it here disqualified whole cities outright — 23 of 35 in a seed-42 size-33 world, every one of them on a cell reading exactly 1.0, on ground under 21 degrees."

The test that pins it adds: "The neighbouring test covers the case this does NOT relax: a routed river still reserves its channel and setback." `flood_risk` is still *sampled and emitted* so a consumer can see it; it simply does not gate.

**Why `channel` and `flood_risk` are two keys** (`_sampler.sample`):

> "Two different things used to share one `flood` key, and a caller could not tell which it had read. `channel` is local: this point is inside the reserved river channel and setback, measured in metres from a routed centerline. `flood_risk` is the regional proxy, one value for a raster cell thousands of metres across — far larger than a city, so it is the same number everywhere in this footprint and can only ever block all of it or none of it."

**Why the clearance transform exists** (`_clearance` docstring):

> "Two sweeps over the city raster, once per city, so that the candidate sweep can decide most positions without rasterizing anything. Roughly four candidate positions in five are rejected because the plot overlaps blocked ground or a street, and each of those used to cost a footprint rasterization and two set tests."

And the soundness argument at the call site:

> "The nearest cell that is not free ground has its centre within (k+1)*CELL of this plot centre in Chebyshev terms, so within (k+1)*CELL*sqrt(2) Euclidean. When that lands inside the inscribed circle it is inside the rectangle and therefore inside the footprint, so the two tests below are already decided and the rasterization is skipped. **It can never fire on a candidate they would have accepted.**"

**Why candidates are a replaying generator** (`_Replay` docstring):

> "`candidates` used to build the whole anchor sweep for every plot size, but `install` takes the first free option and stops: between 22% and 61% of each list was built and never looked at. Yielding on demand and keeping what was yielded leaves the order and the first match identical — no plot moves — and simply stops producing options past the deepest one anybody asked for."

and on `__len__`:

> "Asking how many options there are is asking for all of them, so this is a full pass by definition — the same work the eager list used to do up front. It stays correct while an iteration is open: that iteration reads the memo."

**Why the corridor is cached on endpoints only:**

> "An access corridor depends only on its two endpoints, so every building that shares a plot depth reuses the same one. Rebuilding it per (width, depth) made the eleven-step rasterization the hottest call in city planning."

**Why `height_at` exists beside `sample`:**

> "direction_at builds and normalises the very same vector sample() does and field.height is a pure function of it, so this returns the identical float. What it skips is what a height-only caller discards anyway: the river distance loop, two inverse trigonometric grid-index calls, six layer lookups and a seven-key dict. The surface grid alone asks for this (size+1) squared times per city."

(The dict is now eight keys; the comment predates one addition. Not a defect.)

**Why the ground list is not sorted:**

> "Only max() and min() are ever read from this list, so the sort was ordering a few hundred thousand short lists for nothing."

**Why `shape_only` exists:**

> "Shape selection reads `facts` and nothing below it, and fill_cities needs every city's shape before it can plan any city, because the repetition counter runs in uid order. A caller taking that pass asks for the facts alone; the surface grid below is (size+1) squared height samples it would only throw away."

**Why an authored city still gets a full record:**

> "A city the player authored is not packed here. The record is still produced in full — terrain, reference frame, road connections — because a consumer rendering the player's town needs the ground it stands on; what it does not get is a planner's opinion about where the buildings go. `unbuildable` is a status the planner already emits, so no consumer needs a new branch for it."

**Why the two population figures are named for their axes:**

> "The site's own two demographic figures, copied verbatim and named for their axes. This used to be one key, `simulation_population`, holding the urban estimate under a name that said neither which axis nor whose number it was."

The conformance record expands this: four figures measure people on four axes, the catchment estimate and the built capacity differ by **two orders of magnitude** (on seed 42 at size 17 the largest city estimates 29 428 residents against 244 beds and 241 staffed posts), and that is a ratio, not a rounding — a demographic estimate for a catchment and the beds in a schematic city centre are different quantities. *A consumer choosing which to hold should hold the built capacity, because it is the one a runtime can instantiate.* A plan that housed 29 428 people would be a different product.

**Why the fallback to serial is loud** (`fill_cities`):

> "A world must still generate if the pool cannot start, so this falls back rather than failing — but a silent fallback is indistinguishable from a planner that is simply slow, which is exactly the thing nobody would investigate. Say it."

and the warning text itself, which must survive:

> "City planning fell back to one process (%s: %s). A spawn platform re-imports the caller's `__main__`, so generation must happen under `if __name__ == \"__main__\"` for the pool to start."

**Why the pool is legal at all:**

> "One city's plan is a pure function of (world, site, nearby_counts) — it reads the world and never writes to it — so the only thing serialising the cities is the shape repetition counter, which each city reads and the next one sees updated. Splitting that one dependency out lets the geometry, which is all of the cost, run in a pool."

and `_shape_counts`:

> "This is the serial loop's counter and nothing else: fill_cities increments on the shape a city was given, and an authored city is given none, so it does not count."

**Street network intent** (`city_geometry.grow_roads`):

> "A smooth travel-cost field produces persistent bends, rather than white-noise zigzags."
> "Destinations represent gates and growing districts, never an imposed street grid."
> "Diagonal centerline steps have orthogonal safe support cells (no corner cutting)."

**Rasterisation intent** (`footprint_cells`):

> "Hot path: called roughly a million times per world. Every expression below is the original one, only hoisted out of the loops it did not depend on, so the floating-point results are bit-for-bit unchanged."

That last clause is a constraint on the *Python*; in a clean C++ rewrite the bit-exactness requirement is lifted, but the geometric predicate (dilated rotated-rectangle containment with the `1e-8` bias) is not.

**Fortification intent:**

> `smooth_closed_ring`: "Pull in needle vertices so an enceinte cannot form one giant outward point."
> `program_half_m`: "Grow the city crop from class minimum toward program demand, capped by neighbours." / "Streets, wall reserve and access corridors roughly double service/housing land."
> `build_fortifications`: "Return city fortification export; defended_perimeter is true only when a ring closes."
> `_gate_sites` fallback: "Ensure at least one schematic gate on the primary approach axis."
> `wall_and_gate_defs`: "Fall back to catalogue dimensions when infrastructure rows omit measured fields."
> `build_fortifications` limits string: "Schematic enceinte, not structural engineering. Castle baileys use a separate generator."

**Housing intent:**

> `reserve_housing`: "Hold street-front plots so later service packing cannot starve worker houses."
> upgrade loop: "Replace existing dwellings in place: access and occupied land stay valid."
> release loop: "Unused housing holds are released so later lower-priority services may use the land."
> anchors: "Stable frontage variation follows the street, with modest organic deviations."
> shape clip: "Fine topology/navigability is unknown in this world raster; do not invent it."
> shape clip families: "organic, contour, rings and paired lobes share an elliptical clip; ring scales are fortification-only."

**`city_shapes` module docstring:** "Research-backed shape selection for a future city-footprint planner. This module selects a pattern and parameters; it does not generate coordinates." The separation is intentional: the catalogue is research + weights, the planner is geometry.

**The four plan warnings, verbatim** — these are emitted on every plan and must be carried:

1. "Schematic packing over canonical detailed terrain; groundwater and structural feasibility are not resolved."
2. "Routed river centerlines reserve a provisional 24 m channel and 28 m setback from centerline, not a simulated flood extent."
3. "Four worker beds per house; 16 per apartment building when land is constrained. Dependents, commuters and households are not modeled."
4. "City walls are schematic enceintes separate from castle bailey generation."

Plus the conditional fifth: "Regional land categories repeat coarse samples; canonical local relief supplies finer elevation detail."

**From the conformance record (`docs/conformance/settlement-records.md`, tier EXERCISED, `city-planner=8`):** `city_plans` is the only block that says which buildings a settlement has; `settlements.sites[].city_layout` states no building placement of any kind and carries no key that could be read as one. Two blocks describing the same city's buildings disagreed, and *"because the node-scale record reported a shortfall in every city, a real shortfall in one would have been invisible."* Removing the claim rather than reconciling it is what makes the guarantee checkable. **The record does not disagree with the code anywhere I found.** It is silent on `city_shapes.py`, `city_geometry.py` and `city_fortifications.py`, all three of which `docs/conformance/coverage.json` lists as uncovered against `board/in-progress/CONFORMANCE-DOCS.md`.

## Notes for the C++ implementation

**Read `include/fwg/` first.** Nothing in stages 1–5 resembles this subsystem; it is the first stage that builds *typed records* rather than layers, so `fwg::World` will need a new `std::vector<CityPlan>` block and `fwg::json` will need to serialise deeply nested optional fields.

**Layer access.** A layer is `fwg::Grid` (n × n doubles, column n-1 duplicating column 0, rows 0 and n-1 a repeated pole value). The planner indexes `layer[gz][gx]` with `gx = round(...) % (n-1)`. Because the duplicate column is never indexed, the `Grid` seam convention is already correct here — but the *pole rows* are read as ordinary values, so a city at `z = 0` will read the pole value for every cell. That is what the Python does; keep it.

Optional layers: the Python uses `layers.get(key)` with a per-key default. In C++, `world.layers` is a `std::map<std::string, Grid>` — write a helper `double layer_at(const World&, const char* key, int gz, int gx, double fallback)` rather than materialising default grids, and keep the exact fallbacks (`slope 0`, `water_type 0`, `river 0`, `flood_risk 0`, `moisture 0.5`, `natural_biome 3`, `biome_variant -1`).

**`sphere_grid` is not used here.** This subsystem does not run on the node graph; it runs on a *local* 4 m raster in the gnomonic tangent plane. `water.nodes` indices are only used to project river segment endpoints. Do not try to route streets on the sphere grid — the street Dijkstra is an 8-connected local raster search, not a node-graph search.

**RNG.** `Rng::child(master, "city-streets", <variation>)` for the street stream. The Python derives the street seed from `sha256(f"{seed}:{uid}:streets-v2")`; the FWGC++ rule says three seed derivations exist and a fourth must not be invented, so express this as a `child` with a domain string that includes the city uid, and note in the C++ that the domain tag must be stable across releases or every street layout changes. The three `uniform(-pi, pi)` phase draws must be unconditional and in order.

`Rng::sample_indices(n, k)` replaces `random.sample`. The header already explains why one algorithm is used at every size; that is exactly the right choice here, and it means the street candidate pool will differ from Python's — which is fine under the clean-rewrite ruling.

**Shape selection needs SHA-256 and a canonical JSON writer.** Both already exist (`fwg/sha256.hpp`, `fwg/json.hpp`). The Python hashes `json.dumps([seed, city_id, identity, label], separators=(',',':'))` with `ensure_ascii=True` — you need a *stable* canonicalisation, not Python's. Define one and document it; the numbers will differ and that is allowed. The catalogue identity hash (`json.dumps(catalogue, sort_keys=True, separators=(',',':'))`) likewise. **Note the inconsistency to avoid copying it:** `registry_identity()` uses `ensure_ascii=False` and *no* `sort_keys`, while `shapes_sha256` uses ASCII escaping *and* sorted keys. Pick one convention in C++ and use it for both.

The `/2**64` conversion of the top 8 hash bytes is exact in a double only to 53 bits; that is fine (it is what Python does) but in C++ write it as `static_cast<double>(u64) / 18446744073709551616.0` rather than `ldexp`, and be aware the top 11 bits dominate.

**Python idioms with no direct equivalent**

| Python | C++ approach |
|---|---|
| `set` of `(int,int)` cells | pack to `std::int64_t` (`(int64)i << 32 \| (uint32)j`) or a `std::vector<char>` bitmap of `size*size` plus a count. The bitmap is far faster for `footprint <= valid` and `footprint & road`; use it for `valid`, `road`, `occupied`, `access`, `reserved` and keep footprints as small `std::vector<std::int32_t>` index lists. Every "sorted(set)" becomes a scan of the bitmap in index order, which is already sorted. |
| generator + `_Replay` memo | a `CandidateStream` class holding the anchor index, side index, a `std::vector<Candidate>` memo and a `bool spent`; `next(i)` returns memo[i] or advances. Consumers hold an integer cursor. This is a direct, faithful translation and it matters: the lazy semantics are what keeps the planner from building 22–61 % dead options. |
| `dict` insertion order (`magic_colors`, `counts`) | `std::vector<std::pair<K,V>>` or a `std::map` **only** where the Python sorts anyway. `counts` is read by `.get(id, 0)` so a `std::map<std::string,int>` is safe; `magic_colors` must preserve source order, so use a vector. |
| `sorted(x, key=lambda c: (a, c))` | `std::stable_sort` with an explicit comparator that reproduces the tie-break. Python's sort is stable; `std::sort` is not. Use `std::stable_sort` everywhere a key could tie. |
| `heapq` with `(cost, cell)` tuples | `std::priority_queue<std::pair<double, std::int64_t>, vector, std::greater<>>` with the packed cell as the tie-break. Lazy deletion (`if cost != distances[c] continue`) is already the Python idiom and maps directly. |
| `round(x, n)` | Python rounds **half to even**; `std::round` is half away from zero. All the rounded output fields (2 dp positions, 4 dp rotations and elevations, 3 dp lengths, 6 dp shape parameters) use it. Under the clean-rewrite ruling the exact tie behaviour does not matter, but be deliberate: write one `round_to(double, int digits)` helper and use it everywhere so the output is at least internally consistent. |
| `round()` on `gx`/`gz` grid indices and on the branch `count` | same banker's-rounding note; here it can move a sample by one cell at an exact .5, which is rare but real. |
| `int(x)` | truncation toward zero. `int(half/4)*4` and `int(math.floor(...))` are different operations — the first truncates a positive quantity, the second floors. Do not collapse them into one. |
| `//` on positive ints | `/` in C++ matches; on negatives it does **not** (`-7 // 4 == -2` in Python, `-7 / 4 == -1` in C++). `floor((x+half)/cell)` is written with `math.floor` in `footprint_cells` and with `//` in the prefilter — both operate on values that can be negative when a plot hangs off the crop, so use `std::floor` in both. |
| `math.dist`, `math.hypot` | `std::hypot` for two arguments. The memory note about CPython's `hypot` being a compensated algorithm applies to *parity* work, not to a clean rewrite — plain `std::hypot` is correct here. |
| `sum()` over floats | the compensated-summation note likewise does not bind, but keep the iteration order (§ordering_hazards 14, 17). |
| `set.difference_update(a | b)` | two passes over the bitmap, or clear the union's indices. |
| `option in housing_reserve` (deep equality) | store reserve entries as *indices into the candidate memo* and compare indices. |
| `copy.deepcopy` of the catalogue on every `load_catalogue()` | load once into an immutable `ShapeCatalogue` struct and hand out `const&`. The deep copy exists only to stop callers mutating an `lru_cache` entry. |
| `warnings.warn(..., RuntimeWarning)` | push onto `World::warnings`; the fallback message must still be emitted. |
| `ProcessPoolExecutor` | see below. |

**Data structures worth naming**

```cpp
struct Candidate {            // one legal plot position
    double x, z, rotation_degrees;
    double anchor_x, anchor_z;
    std::vector<std::int32_t> footprint;   // cell indices, may be out of range -> rejected
    std::vector<std::int32_t> corridor;
    double ground_max, ground_min;         // only max/min are ever read
};
struct Anchor { double x, z, angle; };
struct PlotRow { std::string structure_id, name; int count; Priority priority;
                 std::vector<std::string> conditions; Extent plot_m, dimensions_m; int workers, beds; };
```

Storing `ground_max` / `ground_min` instead of the whole list is safe and is what the Python comment says it wanted.

**`Grid` vs. the city raster.** The city raster is `size × size` with `size = half/2` (e.g. 120 for a small city, 200 for a capital) and is **not** a `fwg::Grid` — it has no seam column and no pole rows. Give it its own small type; reusing `Grid` will invite seam handling that must not happen here.

**Parallelism.** The Python forks a process pool (`ICARUS_CITY_WORKERS`, `_MIN_SITES_FOR_POOL = 3`) because CPython cannot thread. In C++ this is `std::thread` / a simple task queue, and the two-pass structure becomes unnecessary in one respect and necessary in another:

- Pass 1 (facts for every city) can run in parallel because `plan_city(..., shape_only=true)` reads only the world.
- The repetition counter **must** run serially in uid order between the passes. Keep the two-pass shape exactly; it is the only serial dependency and it is cheap.
- Pass 2 can run in parallel. Each thread needs its own scratch (the raster, the bitmaps, the candidate memo); share the world and the catalogue as `const&`.

Because threads share memory, the pickle round-trip and the spawn-platform `__main__` hazard both disappear — as does the fallback path and its warning. **Do not delete the determinism guarantee along with them:** the output vector must be indexed by city, not appended in completion order, and nothing in `plan_city` may write to the world.

**Verification I ran, worth reproducing as a C++ test:** on a four-city fixture the pooled and serial paths produced identical plans (`serial == pooled: True`), and disabling the clearance prefilter produced byte-identical plans across all three city classes while the prefilter fired 33 359 times with zero false rejections. Both are cheap invariant tests: (a) `plan_all_parallel == plan_all_serial`, (b) `plan_with_prefilter == plan_without_prefilter`. The second one is the assertion the Python never had.

**Output size.** A capital plan carries a `size²` code grid plus a `(size+1)²` height grid — for `half = 400`, that is 200² + 201² ≈ 80 000 doubles per city, before plots and wall geometry. With twelve cities that is most of a megabyte of JSON. Consider whether the C++ `World` should hold these as `std::vector<float>` and whether `terrain.surface.heights_m` needs 4 decimal places.

**No dependencies.** Everything here is arithmetic, sorting, a binary heap and SHA-256, all of which the library already has. The one thing missing is a JSON *reader* for `city_shapes.json` and `civilizations.json` — the library has a writer only. Either write a small reader or compile the catalogues in as generated headers; the memory note that bytes in `civilizations.json` invalidate every saved world argues for treating them as versioned data with a hash, not as source.

## Open questions

**1. Eleven of seventeen shapes are unreachable, and it is not obviously intentional.** The planner supplies seven site features; every shape requiring a landform boolean (`ridge`, `hill`, `oasis`, `river_access`, `river_bend`, `bridge_feasible`, `island`, `coast_access`, `navigable_water`, `peninsula`, `lagoon`) can never match, and `planned_extension` is disabled by default. The `trade_intensity`, `planned_foundation` and `corridor_constraint` preference bonuses can never fire either. The catalogue's own `selection.notes` says "Location summaries must be supplied by a future footprint-analysis adapter", which reads as an acknowledged gap rather than a defect — but the planner already knows enough to answer several of them (it has `water_type`, `river`, the routed river lines and the height field). **Decide:** port the rule engine faithfully and leave the gap, or extend the facts. Do not silently do the latter — every city's shape changes if you do, and `concentric_enceintes` (the only shape that produces multiple rings) would stop being the only capital-specific option.

**2. `program_half_m`'s demand term is inert.** With the shipped registry, `needed` (135–191 m) never exceeds `base` (240–400 m), so the class minimum always wins and the `sqrt(plot_area * 2.4)/2 * 1.15` branch has no effect on any world this repository generates. It is a designed growth path, but it is also untested in the only sense that matters. Port it, and add a test that exercises it with an inflated preset.

**3. `_gate_sites` can raise `TypeError`.** `candidates.sort()` sorts `(distance, vertex_index, route_index, gate_point)` tuples where `route_index` may be `None`. A tie on `(distance, vertex_index)` would compare `None` with an `int`. It cannot occur today (a tie needs two connections snapping to the same vertex at bit-identical distance), but it is a latent crash and a C++ port must choose a total order explicitly. Recommendation: `(distance, vertex_index, route_index.value_or(INT_MAX))`.

**4. `_stream` rebinds `angle` inside the `side` loop.** `side = 1` computes its street-normal from the raw anchor angle but its footprint from the 4-dp round-tripped one; `side = -1` uses the round-tripped angle for both. The two sides of every street are therefore treated asymmetrically at the 1e-9 level. Almost certainly an accident. Reproducing it in C++ is possible but pointless under the clean-rewrite ruling; **decide explicitly** to use one angle consistently and record that you did.

**5. An explicit `placement` bypasses the terrain tests.** Gatehouse plots are installed with a hand-built placement tuple, so `footprint <= valid` and `footprint & road` are never checked for them — a gatehouse can sit on water, on a 40° slope or in the middle of a street. That is arguably correct (the wall opening is where the wall is) and towers *are* tested, so the asymmetry is deliberate. But nothing states it. Confirm it is intended before porting, because a downstream renderer placing a 14×12×14 building inside a lake will look like a bug in the renderer.

**6. `build_fortifications` emits tower records with no budget behind them.** On the outer ring the slice is `[:max(1, tower_budget)]`, so `tower_budget = 0` still yields one tower record. The planner then never places a plot for it (`tower_row` is `None`), so the report carries a tower with no `plot_id`. Decide whether the report should be geometry-only (current behaviour, defensible) or budget-aware.

**7. The `climate` / `water` river-segment fallback is on the wrong granularity.** `world.get('climate', world.get('water', {})).get('river_segments', [])` falls back on the *block*, so a world with a `climate` block lacking `river_segments` gets no river reservation at all even when `water.river_segments` is populated. No current world hits it. Recommend falling back per-field in C++ and noting the change.

**8. `neighbour_half` disables itself on a world with no uids.** `other.get('uid') == site.get('uid')` is `None == None` when neither has one. Shipped worlds always carry uids; fixtures may not. A C++ port with a non-optional uid field makes this unrepresentable, which is the right fix — but it means a C++ fixture cannot reproduce a Python fixture's crop size.

**9. `free_frontage` is a debug field that costs the entire candidate sweep.** `debug.housing_frontage_candidates` forces full enumeration of the house-size stream, defeating the lazy `_Replay` it sits beside. Decide whether to keep it, make it opt-in, or drop it. It is the only consumer of the full sweep.

**10. `shape.parameters` are almost entirely unused.** Of the five or six parameters rolled per city, only `block_spacing_m` (via `spacing`) and `aspect_ratio` (via `rz`) and `ring_count` (via `ring_count_for`) reach any geometry. `street_curvature`, `plaza_offset_fraction` and `district_count` are emitted and never read by anything in this subsystem. Check whether a consumer outside these four files reads them before deciding they are dead.

**11. Nothing checks that the process pool preserves output** — that is the trap as stated. I measured it once (four cities, one fixture: identical) and the purity argument is sound, but neither is a proof, and the Python test suite contains no such test. If the C++ goes parallel, write the test.

## Addenda from the audit

The reader above wrote the specification; a second reader went back to the source
to find what it had left out or got wrong. Both are kept: the omissions are
content the implementer needs, and the corrections say where the specification
above is not to be trusted.

### Corrections

- **Claimed:** grow_roads cost field `1 + 0.45*(1 + sin(x/9+p0)*cos(z/11+p1)) + 0.3*(1 + sin((x+z)/17+p2))` has range `[0.25, 2.05]` (stated twice: algorithm step 6 and the constants table), and the 18*grade^2 term at the 0.35 ceiling adds 2.205, 'roughly the whole span of the smooth cost field'.
  **Actually:** The range is [1.0, 2.5]: each bracketed term is (1 + something in [-1,1]) so it is in [0,2], giving 1 + [0,0.9] + [0,0.6]. Verified numerically over a 200x200 sweep: observed 1.000 to 2.499. The span is therefore 1.5, and the 2.205 grade penalty exceeds the entire span rather than roughly matching it; the incidental conclusion that a maximally steep link costs about twice the worst flat one (4.705 vs 2.5) still holds.

- **Claimed:** _segmentize: 'segment lengths therefore vary in [0, 2*segment_depth]'.
  **Actually:** They vary in (0, segment_depth]. carry is always < segment_depth - 0.01 on entry to the outer edge, so need = segment_depth - carry is in (0, segment_depth], and the while guard carry+edge >= segment_depth-0.01 forces t = need/edge <= 1 + epsilon. Subsequent segments on the same edge start at the previous end with carry reset to 0, so they are exactly segment_depth. Measured on a 48-vertex noisy ring with segment_depth 10: 246 segments, min 0.05, max 10.006, none above 10.01. The test's |length-10| <= 10 is loose in the other direction. What the spec correctly identifies as a consequence — the leftover point before each vertex is abandoned — means the wall has a gap of length `carry` immediately before every polyline vertex; that is the real artefact, not long segments.

- **Claimed:** '`site_features` holds 22 features: eleven booleans ... and eleven numbers'.
  **Actually:** 21 features: 11 booleans (coast_access, navigable_water, river_access, river_bend, bridge_feasible, island, peninsula, lagoon, ridge, hill, oasis) and 10 numbers (usable_land_fraction, corridor_constraint, aridity, wooded_fraction, defense_priority, regional_threat, planned_foundation, trade_intensity on 0..1; local_slope_degrees 0..90; buildable_area_m2 0..1e12). The spec's own enumeration lists exactly those 10 numbers while claiming eleven.

- **Claimed:** `world['layers']`: `height` (required — `HeightField` indexes it directly)'; and the C++ note 'Because the duplicate column is never indexed, the Grid seam convention is already correct here'.
  **Actually:** HeightField bilinearly interpolates layers.height at a fractional (x,z) derived from the direction, and adds Perlin detail bands when terrain_detail exists. It explicitly indexes column ix+1, which reaches the duplicate seam column n-1 when ix == n-2, so the duplicate column IS read by the height path even though the planner's own integer `gx = round(...) % (n-1)` layer lookups never reach it. Direct/nearest indexing would make height_at constant within a coarse cell and collapse the finite-difference slope to zero.

- **Claimed:** `_boundary_points` ray reach: 'for dist in 1 .. max(1, int(max(rx, rz)/cell) + 2)' (and the constants table's 'ray reach | max(1, int(max(rx,rz)/cell) + 2) cells').
  **Actually:** The source is `for dist in range(1, max(1, int(max(rx,rz)/cell)+2))`, so dist runs 1 .. int(max(rx,rz)/cell)+1 — the printed bound is exclusive. If the expression collapses to 1 the loop body never executes at all and the ray contributes no point; an inclusive reading would sample dist=1.

- **Claimed:** 'Takes each city site chosen at stage 9' / '`world['settlements']['sites']` — the stage-9 city records'.
  **Actually:** Settlements are written by terrain_settlements.add_settlements, which sits in the profile under 'Stages 10-12: civilization, in three passes over the same body'. Stage 9 is leylines and the sky (terrain_leyline_history / terrain_astrology). threat_assessments is stage 13. The stage-16 position of fill_cities, and its running first among the planners, are correct.

- **Claimed:** Registry table: `program_half_m` demand term `needed` is '~135 m' for small_city and '~168 m' for medium_city.
  **Actually:** Computed from the shipped human_heartland registry: small 117.6 m, medium 159.3 m, capital 190.6 m (capital is right). Every other figure in that table checks out exactly — 28/29, 41/58, 45/82 rows/instances; 27/42/58 core; 86/188/285 workers; 13195/22972/31945 m^2 raw plot area; 0/2/4 gatehouses and towers. The conclusion that the demand term never binds is still correct, and with a wider margin than the spec claims.

- **Claimed:** The shape-entry schema sketch lists `"placement_checks": [ ... ], "description": ..., "notes": ...`.
  **Actually:** A shape entry has exactly these keys: id, name, family, enabled_by_default, city_classes, base_weight, history, eligibility, preferences, layout, variation, placement_checks. There is no `description` and no `notes` key on a shape; `notes` exists only under `selection`.

### Missing — blocking

**Per-shape `variation` intervals in city_shapes.json (drives rz, street spacing and ring count)**

Every shape has aspect_ratio, street_curvature, block_spacing_m, plaza_offset_fraction, district_count (integer); concentric_enceintes adds ring_count (integer). Ranges as {min,max}: organic_market aspect 1–1.6, curvature 0.25–0.7, spacing 35–90, plaza 0–0.25, district 1–3. market_grid aspect 1–1.8, curvature 0–0.12, spacing 35–90, plaza 0–0.25, district 1–3. courtyard_lanes aspect 1–1.5, curvature 0.35–0.8, spacing 35–90, plaza 0–0.25, district 1–3. ceremonial_clusters aspect 1.1–2.8, curvature 0.2–0.65, spacing 35–90, plaza 0–0.25, district 2–5. walled_compounds aspect 1.2–2.2, curvature 0–0.15, spacing 35–90, plaza 0–0.25, district 1–3. concentric_enceintes aspect 1–1.5, curvature 0.2–0.55, spacing 40–90 (note: 40, not 35), plaza 0–0.2, district 2–3, ring_count 2–3. Unreachable-but-portable shapes: ridge_fishbone 2–4.5; hill_rings 1–1.6; oasis_cluster 1.2–2.5 (district 2–5); river_meander 1–2; bridge_twin 1.3–2.8 (district 2–3); island_spines 1.8–3.5; waterfront_comb 1.8–3.8; peninsula_core 1.2–2.5; lagoon_network 1–2.5 (district 2–5); citadel_lower 1.2–2.5 (district 2–3); planned_extension 1.4–2.5. All non-listed curvature/spacing/plaza follow the 35–90 / 0–0.25 pattern.

**Which field each shape's `preferences` rule tests (the spec gives only the bonus magnitudes)**

All preference rules are {field, min} with bonus 8 unless noted. organic_market: trade_intensity>=0.5. market_grid: planned_foundation>=0.5, trade_intensity>=0.5. courtyard_lanes: aridity>=0.5. ridge_fishbone: corridor_constraint>=0.5, defense_priority>=0.5. hill_rings: defense_priority>=0.5. oasis_cluster: aridity>=0.5. river_meander: trade_intensity>=0.5, defense_priority>=0.5. bridge_twin: trade_intensity>=0.5. island_spines: trade_intensity>=0.5, corridor_constraint>=0.5. waterfront_comb: trade_intensity>=0.5. peninsula_core: defense_priority>=0.5. lagoon_network: trade_intensity>=0.5. citadel_lower: defense_priority>=0.5. ceremonial_clusters: wooded_fraction>=0.5. walled_compounds: aridity>=0.5, planned_foundation>=0.5. concentric_enceintes: regional_threat>=0.55 bonus 18, defense_priority>=0.5 bonus 8. planned_extension: planned_foundation>=0.5, trade_intensity>=0.5. The list order is load-bearing: `matched_preferences` is emitted as [p['when']['field'] for p in matched] in catalogue order. With the seven facts the planner supplies, the only bonuses that can ever fire are aridity (courtyard_lanes, walled_compounds), wooded_fraction (ceremonial_clusters), and regional_threat/defense_priority (concentric_enceintes).

**The contract of `H` / `HeightField.height(direction)` — the spec says the height layer is indexed directly**

H is continuous, not a cell lookup. Given a unit direction p: x = ((atan2(p.z,p.x)+pi)/(2*pi)*(n-1)) % (n-1); z = clamp((pi/2 - asin(clamp(p.y,-1,1)))/pi*(n-1), 0, n-1); ix=int(x), iz=int(z), jz=min(n-1,iz+1), fx=x-ix, fz=z-iz; base = ((1-fx)*h[iz][ix]+fx*h[iz][ix+1])*(1-fz) + ((1-fx)*h[jz][ix]+fx*h[jz][ix+1])*fz. Note ix+1 deliberately reads the duplicate seam column n-1, so unlike the planner's own integer `gx` lookup the seam column IS indexed here. With no terrain_detail that value is returned. With terrain_detail: coast = clamp((base - sea_level)/20, 0, 1) and return base if coast==0; strength = bilinear(strength_grid) then max(0,(strength-0.15)/0.85), return base if 0; wet = max(0, 1-2*bilinear(water_type)); strength *= coast*coast*(3-2*coast)*wet*wet, return base if 0; detail = sum over three bands of amplitude*perlin3(p*radius/scale, band_seed) with bands (scale_m 120, amp 12), (28, 2.5), (6, 0.3); return base + strength*detail. strength_grid[z][x] = 0 if water else max(0,1-min(1,river*2))*max(0,1-min(1,flood))*({5:1.4, 3:0.8, 4:1.0, 7:1.0, 15:0.8} by natural_biome, default 0.65). sea_level comes from effective_config['sea_level'] default 0. Without this, the finite-difference slope in §1.3 is identically zero and plot test (3) never rejects anything.

### Missing — important

**`terrain.surface` emits a fifth key the spec omits, and the two literal strings it does list have no values**

surface = {'size': size+1, 'step_m': 2*half/(surface_size-1), 'heights_m': [...], 'source': 'canonical terrain height in metres', 'terrain_detail': world.get('terrain_detail'), 'coordinates': 'local east/north gnomonic coordinates; radial elevation above reference sphere'}. The whole terrain_detail block (version, seed, the three bands, height_reference, coordinates, method) is copied into every city plan. Key order in the emitted dict is size, step_m, heights_m, source, terrain_detail, coordinates.

**The `reason` string emitted on a successful shape selection**

'Seeded weighted choice among compatible shapes.' — set by result.update(shape_id=..., parameters=..., layout=..., source_ids=..., reason=...). The spec gives only the no-match and authored strings.

**`_rank` and `select_shape` raise on malformed site facts — a whole failure mode is undocumented**

_rank raises ValueError('Missing required footprint location data') unless site is a dict containing all of buildable_area_m2, usable_land_fraction, local_slope_degrees; ValueError('Unknown site feature: <key>') for ANY key in the site dict that is not a declared site_feature; ValueError('Expected boolean site feature') if a boolean feature's value is not a Python bool; ValueError('Out-of-range site feature') unless a numeric value is finite and within the feature's declared [min,max]. select_shape additionally raises ValueError('Expected an integer seed and stable nonempty city ID') unless seed is an int and city_id is a str of length 1..256. This is live: local_slope_degrees is declared 0..90 and the planner's no-buildable-cell sentinel is exactly 90 (passes); regional_threat and defense_priority are declared 0..1, so a threat assessment writing >1 aborts city planning for the whole world.

**`shape.layout` and `shape.source_ids` are absent when no shape was chosen, and stale when the city is authored**

select_shape's early return on zero candidates yields only {version, catalogue_revision, catalogue_sha256, shape_id: None, parameters: {}, candidates: [], runtime_geometry_generated: False, reason}. layout and source_ids are never added. For a player-authored city select_shape runs normally first, so the record KEEPS the layout prose, source_ids and candidates of the shape it would have picked, while shape_id is None and parameters {}. A consumer must not read layout as describing the authored town.

### Missing — minor

**`sample()` has a third parameter**

def sample(x, z, with_slope=True): when with_slope is False the finite-difference override is skipped and the coarse `slope` layer value is returned even with terrain_detail present. No call site inside these four files passes it; it is a hook, and a port may omit it, but the signature is part of the sampler's shape.

**`heights_m` index order in the surface grid**

heights_m[j][i] where i indexes the x (east) axis and j the z (north) axis: [[... for i in range(surface_size)] for j in range(surface_size)], matching terrain.codes[j][i].

**Emitted reference_frame angle formulas**

latitude_degrees = 90 - 180*site['z']/(n-1); longitude_degrees = -180 + 360*site['x']/(n-1); radius_m = the effective globe radius in metres.

**The catalogue's top-level metadata strings and no_match text (the planner does NOT use no_match)**

status: 'planner_input_not_runtime_geometry'; units: 'metres unless a field states otherwise'; research_date: '2026-09-15'; numeric_parameters_status: 'Provisional game-design thresholds and ranges; not historical measurements.'; selection.no_match: 'Return no shape; request better site data or another site. Never fabricate shore, bridge or dry land.'; selection.notes: 'Filter hard requirements, then weight preferences and recent local repeats. Civilizations are not locked to historical cultures. Location summaries must be supplied by a future footprint-analysis adapter.' Note that selection.no_match is data only — select_shape hardcodes 'No compatible shape; change site or provide missing location facts.' instead. There are 15 entries in `sources`.

**Registry plot sizes for the fortification buildings the planner installs**

human_heartland medium/capital: building.wall is an INFRASTRUCTURE row with dimensions_m {width 3, depth 10, height 7} and plot_m {width 7, depth 10} — so the 3x10x7 fallback in wall_and_gate_defs never fires with the shipped registry. building.gatehouse is a BUILDINGS row, priority 'conditional', placement_conditions ['defended_perimeter'], dimensions_m {14, 12, 14}, plot_m {width 18, depth 21}, count 2 (medium) / 4 (capital). small_city has no building.wall in infrastructure at all and no gatehouse/tower rows, which is consistent with ring_count_for returning 0 for 'small'. Because the gatehouse is 'conditional' it lands in `low`, and because its only placement_condition is defended_perimeter it is dropped to unplaced with 'Unverified prerequisite: defended_perimeter' whenever no ring closed.

**`int(os.environ['ICARUS_CITY_WORKERS'])` is outside the try/except**

A non-numeric value raises ValueError out of fill_cities before any planning; it is not covered by the RuntimeWarning fallback, which only wraps _plan_pooled.

**`.get(key, default)` is not `or` — the spec writes several id fallbacks as `or`**

city_uid is site.get('uid', str(site['id'])) and building_id is row.get('structure_id', row.get('id')). A site carrying uid=None or uid='' yields city_uid None / '' rather than falling back to the id, and str(site.get('uid', site['id'])) then passes 'None' or '' to select_shape, which rejects '' with ValueError. Same for wall.get('structure_id', wall.get('id','building.wall')). In C++ decide explicitly: key-present-fallback, not truthiness-fallback.

**One hoisting comment not carried**

_sampler, above the layer binds: 'Resolved once instead of on every layer read inside sample(); plan_city only reads these grids, so hoisting them changes no value, just the lookup count.' Also city_shapes.rank_shapes (a public entry point the spec never mentions) carries: 'Return eligible weighted alternatives in stable ID order, not a forced winner.'
