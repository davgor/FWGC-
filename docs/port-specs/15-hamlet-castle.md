# Port specification: 15-hamlet-castle

Extracted from the Python `FantasyWorldGenerator` at commit `b67c99b` before that
repository was retired, then audited against the same source by a second reader.

**Source modules**

- `Sim/icarus_sim/hamlet_planner.py`
- `Sim/icarus_sim/castle_planner.py`
- `Sim/icarus_sim/castle_geometry.py`

**Estimated C++ size:** roughly 2600 lines, excluding tests.

## Audit: NOT YET IMPLEMENTABLE

The spec is unusually strong — most of it survives adversarial reading, and I independently reproduced its headline measurements (the seed-42 hamlet-0 ellipse absorption down to the 1616/2500 cell count, the 32-of-32 corner indices, the ragged segment lengths, the kit weights, both building tables and the identical 12-civilization hamlet programme). But it fails the "implement it in your head" test in three places that stop or silently divert a porter. First, clip_ring_to_valid funnels every castle ring with >=8 snapped points through smooth_closed_ring, whose needle pass is gated on path_turn_degrees(prev, p, next) > 95; the spec names that function and never defines it, and it is NOT the turn_degrees the spec does define (turn_degrees is the interior angle, path_turn_degrees the heading change — the spec itself makes that distinction load-bearing in G1, then relies on the undefined one). Every castle perimeter in the world depends on it. Second, the spec states passes['perimeter'].placed is the count of closed rings; in the source that slot is written at C5 and then incremented again by every gate, tower and stair plot installed in C9, so the shipped value is closed_rings + perimeter_plots. A porter following the spec ships a different number on every castle and nothing fails. Third, the hamlet plot's building_id is row.get('structure_id', row.get('id')); the housing-profile row for hamlet_house has no structure_id, so a literal row['structure_id'] throws on the first cottage, and the spec never states the fallback. Alongside those it asserts "eight ground probes" for the hamlet flatness test and elevation fields when the actual list is four rotated corner heights plus one entry per footprint cell (typically 14-20), and it mis-scopes the road_connections[].cell key, the castle's absent node field, the drawbridge module's join data, and several measured claims (the 32-gon turn angle, the dist=half clip bound, the tower-budget "reliably three"). Everything in the Reasoning-preserved section checks out verbatim against the code and the two docs files, and the ordering-hazard list is accurate.

**2 blocking gap(s)** were found and are recorded in the addenda below.

## Purpose

Two sibling "final-world" schematic layout generators that turn a single rural pin on the world raster into a metre-scale buildable plan, plus the wall-ring geometry library the castle one uses.

`hamlet_planner` packs one support hamlet: it crops a ~200 m window around the hamlet's grid anchor, samples canonical terrain into a 4 m cell raster, clips that to a seeded compact ellipse, grows a least-cost street tree, then greedily installs the civilization's `hamlet` building preset along street frontages, interleaving worker-cottage passes so that staffed posts have beds. It deliberately shares nothing with the city shape catalogue and never places city worker housing — hamlets get `building.hamlet_house` (a 2-bed cottage) and never apartment densification.

`castle_planner` packs one route-defence fortress: it picks a typology kit (simple enclosure / motte-and-bailey / concentric wards) from a weighted draw on the world seed and the fortress id, traces one closed wall ring per kit ring by snapping ellipse rays inward to buildable land, segments each ring into curtain runs with gates, posterns, corner towers and wall stairs, then drops a bailey court and keep tower at each closed ring's centroid and fans the bailey services around the primary ring. It has no streets, no housing and no plot-collision test at all.

`castle_geometry` is the pure-geometry half of the castle planner: turn measurement, join-rule validation, ellipse sampling, inward clipping, polyline segmentation, corner detection and the `build_wall_network` assembler. It is data-driven from `castles.json` and is the only place that knows what a "closed ring" means.

They exist because the city planner's output shape (terrain raster + plots + passes + status) is what every downstream consumer — `world_scene`, `npc_roster`, the lab viewers, the asset list — already reads, and rural pins and fortresses needed the same record without being coupled to city shape selection, city fortifications or the city program sizing.

## Where it sits in the pipeline

Both run at the very end of generation, at **stage 16**, and again after every age transition. `terrain_history._finish` / the stage-16 branch calls, in this exact order:

    fill_cities(world)      # city_planner
    fill_hamlets(world)     # hamlet_planner   <-- this spec
    fill_castles(world)     # castle_planner   <-- this spec

`terrain_visitation` (god summoning) and `terrain_history` age advance repeat the same three-call sequence after rebuilding the world. `materialize_stage(world, 15)` must NOT contain `hamlet_plans` or `castle_plans`; a test pins that.

**Must already exist before either runs:**

- `world['config']['size']` (n) and `world['config']['seed']`; `world['effective_config']['globe_radius']` (metres) — both read `world.get('effective_config', world['config'])['globe_radius']`.
- `world['layers']`: `height` (required — `HeightField` indexes it directly), and optionally `slope`, `water_type`, `river`, `flood_risk`, `moisture`, `natural_biome`, `biome_variant`. Each missing layer has a hard-coded default (see Reads).
- `world['terrain_detail']` — the continuous-Perlin relief definition. If absent, `HeightField.height` degenerates to a bilinear sample of `layers['height']` and the per-sample slope derivative is skipped entirely (the coarse `slope` layer is used instead).
- `world['water']['nodes']` — list of `[x, z]` grid coordinates, one per hydrology node. Indexed by `access_nodes` and by `river_segments`.
- `world['climate']['river_segments']` (falls back to `world['water']['river_segments']`) — list of `[i, j]` node-index pairs forming routed river centrelines.
- `world['settlements']['sites']` — the city list. `core_id` on a hamlet/fortress is a **position in this list**, not a uid.
- `world['humans']['hamlets']` and `world['humans']['fortresses']` — the pins (see Reads for field-by-field contents).
- `world['terrain']['biomes']`, `world['terrain']['magical_biomes']`, `world['magic']['networks']` — copied verbatim into each plan's `terrain` block for viewers. All optional (`.get` with `[]` / `{}`).
- The civilization + building registries (`civilizations.json` + `buildings.json`) for hamlets; `castles.json` + `buildings.json`'s `structure_blocks.castle` for castles.

**What depends on them afterwards:**

- `world_scene.build_scene(world)` is called at the END of both `fill_hamlets` and `fill_castles` (each rebuilds the whole scene, so the castle run's scene is the one that survives). It reads `street_paths_m`, `road_connections` (with `direction`, `junction_id`, `status`, and `local_path_m` when `status=='connected'`) and `plots[]` (`x_m`, `z_m`, `rotation_degrees`, `dimensions_m`, `ground_elevation_m`, `building_id`, `id`), tagging everything `settlement_kind='hamlet'` / `'castle'`.
- `npc_roster` opens one post per `plots[].workers` (castle) and reads hamlet plots and beds.
- `terrain_read_select` resolves `read-place` for a hamlet by matching `humans.hamlets[].id` → `hamlet_plans.hamlets[].hamlet_id`, and `humans.fortresses[].id` → `castle_plans.castles[].fortress_id`.
- `terrain_history` age advance **rejects the world** with `ValueError('Hamlet planner data changed; regenerate this world')` / `'Castle planner data changed; ...'` if the stored `identity` block does not equal a freshly computed `planner_identity()`.
- `terrain_settlement_api`, `terrain_corruption` and `terrain_visitation` treat `hamlet_plans` / `castle_plans` as derived state: they **drop** both blocks whenever `humans` is rebuilt, because the ordinal ids renumber.

## Inputs

**A. World-level, both planners**

| Path | Meaning | Units / range | Missing-value behaviour |
|---|---|---|---|
| `config.size` = n | grid edge | odd int, 17…1025 | required |
| `config.seed` | world seed | int | required |
| `effective_config.globe_radius` (else `config.globe_radius`) | planet radius | metres (~1775 m for the 11.15 km reference world; up to ~1e6 for big worlds) | required |
| `effective_config.sea_level` | datum for the coast fade in `HeightField` | metres | defaults 0.0 |
| `layers.height` | coarse elevation | metres above reference sphere | required |
| `layers.slope` | coarse slope | degrees | 0 |
| `layers.water_type` | 0 = dry, non-zero = standing water | int | 0 |
| `layers.river` | routed-river strength | 0..1 | 0 |
| `layers.flood_risk` | regional flood proxy | 0 or 1 | 0 |
| `layers.moisture` | | 0..1 | 0.5 |
| `layers.natural_biome` | biome code | int | 3 |
| `layers.biome_variant` | magical variant code | int, −1 = none | −1 |
| `terrain_detail` | Perlin relief bands | see `HeightField` | None → coarse-only heights, coarse slope |
| `water.nodes[i]` | `[x, z]` grid coords of hydrology node i | ints | `[]` |
| `climate.river_segments` (else `water.river_segments`) | `[[i, j], …]` node pairs | | `[]` |
| `terrain.biomes`, `terrain.magical_biomes` | catalogues, copied verbatim | | `[]` |
| `magic.networks` | `{school: {color: …}}`, only `color` is copied | | `{}` |
| `settlements.sites[]` | city anchors; only `x`, `z` (and `uid`/`id` for the hamlet's core lookup) are read | grid coords | required |

**B. The hamlet pin** (`world['humans']['hamlets'][k]`, key field is **`id`**, a string)

- `id` — `'hamlet-<ordinal>'` for inland pins, `'coastal-<city_id>-<k>'` for fishery ports. **Ordinal; renumbers on an age advance.** There is a stable `uid` (`'hamlet-node-<node>'`) beside it on inland pins, but coastal port rows carry **no `uid` at all** and the planner never reads it.
- `x`, `z` — grid coordinates (ints; may be non-integral only in synthetic fixtures).
- `node` — hydrology node index. Copied out verbatim; the stable key.
- `core_id` — **integer position** into `settlements.sites`, not a uid.
- `population_profile` — civilization entity id, e.g. `'human_heartland'`. Used as the registry key and copied out as `civilization_id`.
- `role` — `'farming'`, `'resource'`, or for ports `'harbor + fishing'` / `'harbor'` / `'fishing'` / `'landing'`. May be absent or `''` on a player-founded row.
- `access_nodes` — node-index path running **from the hamlet toward its parent city** (`path[0]` is the hamlet's own node). Used to synthesise the support-road gate.

**C. The fortress pin** (`world['humans']['fortresses'][k]`, key field is **`id`**)

- `id` — `'fortress-<ordinal>'`. **Ordinal; renumbers on an age advance.** `uid` (`'fortress-node-<node>'`) exists but is never read.
- `x`, `z`, `node`, `core_id`, `access_nodes` — as above. No `role`, no `population_profile` use.

**D. Data registries** — see Data files.

**E. Environment** — `ICARUS_CITY_WORKERS` (string int) is read by `fill_hamlets` only. `fill_castles` is always serial.

## Outputs

### `world['hamlet_plans']` (written by `fill_hamlets`)

    {
      'version': 3,
      'identity': {'version': 3, 'registry': {'schema_version': …, 'revision': …, 'sha256': …}},
      'hamlets': [ <plan>, … ],                       # one per humans.hamlets row, sorted by str(id)
      'phase_order': ['map','shape','high','high_housing','low','low_housing'],
      'scope': 'Schematic local metres for support hamlets; independent of city_plans; does not change simulated population.'
    }

One `<plan>`, keyed on **`hamlet_id`**:

| Field | Type / units | Notes |
|---|---|---|
| `version` | 3 | |
| `hamlet_id` | string | = pin `id`. The join key. |
| `kind` | `'hamlet'` | |
| `role` | string or None | copied from pin |
| `core_city_uid` | string | `sites[core_id].get('uid', str(sites[core_id]['id']))` |
| `core_id` | int | position in `settlements.sites` |
| `x`, `z`, `node` | grid coords, node index | copied |
| `civilization_id` | string | = pin `population_profile` |
| `unit` | `'metres'` | |
| `bounds_m` | `[-half, -half, half, half]` | half is an int multiple of 4, ≤ 100 |
| `terrain.cell_m` | 4 | |
| `terrain.size` | int | `2*half//4` |
| `terrain.codes[j][i]` | 0/1/2 | 0 buildable, 1 water, 2 steep. **Row-major `[z][x]`.** |
| `terrain.natural_biome[j][i]`, `terrain.biome_variant[j][i]` | ints | |
| `terrain.surface` | `{size: terrain.size+1, step_m: 2*half/(size), heights_m: [[…]], source, terrain_detail, coordinates}` | heights rounded to 4 dp, metres |
| `terrain.biome_catalogue`, `terrain.magical_catalogue`, `terrain.magic_colors` | copied | |
| `reference_frame` | `{radius_m, latitude_degrees: 90-180*z/(n-1), longitude_degrees: -180+360*x/(n-1), projection: <string>}` | |
| `shape` | fixed literal `{'shape_id':'hamlet_compact','family':'organic','parameters':{'block_spacing_m':48,'aspect_ratio':1.1}}` | never varies |
| `plots[]` | see below | |
| `roads[]` | `[[i,j], …]` sorted ascending | road cells in raster indices |
| `street_paths_m[]` | list of polylines, each `[[x,z],…]` in metres, 2 dp | **only present when ≥8 valid cells survive the ellipse**; absent on the two early returns |
| `unplaced[]` | `{building_id, count, reason}` | |
| `passes[]` | `[{id, placed}]` in phase order | `map.placed` = terrain-valid cell count; `shape.placed` = 1 |
| `road_connections[]` | ≤1 entry, see below | |
| `warnings[]` | 4 fixed strings, +1 more when `resolution > half` | |
| `source_resolution_m` | float, 2 dp | `round(pi*radius/(n-1), 2)` |
| `status` | `'unbuildable'` / `'partial'` / `'complete'` | |
| `stats` | `{workers, worker_beds, housing_shortfall, service_buildings, houses, apartments (always 0), unplaced_core, unplaced_total, reserved_plot_area_m2}` | short form on early return: only the first five keys |
| `debug` | `{terrain_safe_cells, total_cells, housing_passes[], apartment_policy:'hamlet_houses_only', shape_safe_cells, road_cells, occupied_cells, access_cells, vacant_shape_cells, housing_frontage_candidates}` | last six only on the full path |

Hamlet **plot** record (key field `id` = `'plot-<index>'`):

    {'id','building_id','name','kind' ∈ {'service','housing'},'phase','x_m','z_m',
     'rotation_degrees','ground_elevation_m','foundation_bottom_m','plot_m','dimensions_m',
     'road_access':[ax,az],'workers','beds'}

`x_m`/`z_m`/`road_access` are metres, 2 dp; `rotation_degrees` 4 dp (from `atan2`, so in (−180, 180]); `ground_elevation_m` = max of the eight ground probes, `foundation_bottom_m` = min, both 4 dp. `beds` is `row.get('worker_beds', 0)` — 0 for every service building, 2 for a cottage.

### `world['castle_plans']` (written by `fill_castles`)

    {
      'version': 3,
      'identity': {'version':3,'geometry_version':1,'castles_sha256':…,'castle_buildings_sha256':…},
      'castles': [ <plan>, … ],                        # one per humans.fortresses row, sorted by str(id)
      'phase_order': ['map','kit','perimeter','courts','landmarks','services'],
      'limits': 'Schematic fortification modules and bailey plots; not structural engineering or production art.',
      'beds':  <the long paragraph quoted under reasoning_to_preserve>
    }

One `<plan>`, keyed on **`fortress_id`**:

| Field | Notes |
|---|---|
| `version` 3, `kind` `'castle'` | |
| `fortress_id` | = pin `id`. Join key. |
| `core_id` | `fortress.get('core_id')` — may be None |
| `x`, `z` | grid coords |
| `kit_id`, `kit_name` | selected typology |
| `bounds_m` `[-half,-half,half,half]` | half ≤ kit `half_m` (72 / 88 / 96) |
| `passes[]` | 6 entries in PHASE_ORDER |
| `plots[]`, `roads[]` (**always `[]`**), `wall_networks[]`, `unplaced[]`, `road_connections[]` | |
| `warnings[]` | 2 fixed strings, never extended |
| `stats`, `debug`, `status` | |
| `source_resolution_m` | `round(pi*radius/(n-1), 4)` — **4 dp here, 2 dp in the hamlet** |
| `terrain` | same shape as the hamlet's **minus** `surface.source` / `surface.terrain_detail` / `surface.coordinates` |

Castle plan has **no** `reference_frame`, **no** `unit`, **no** `civilization_id`, **no** `street_paths_m`.

Castle **plot** record: same keys as the hamlet's **except that `beds` is deliberately absent** (see reasoning). `x_m`, `z_m` 2 dp, `rotation_degrees` 4 dp and **not normalised to any range** (observed values of 422.9 and 611.9 degrees). `road_access` is `[x_m, z_m]` — the plot's own centre, not a street.

`wall_networks[]` entry (`id` = `'ring-<role>'`, and each ring also carries `role`):

    {'id','role','polyline_m':[[x,z],…],'status' ∈ {'open','broken','closed'},
     'segments':[…], 'nodes':[…], 'gates':[…], 'towers':[…], 'stairs':[…],
     'walkway_continuous': bool, 'unplaced':[…],
     # only when status=='closed':
     'segment_count','gate_count','ditch': bool}

- **segment** (`id` = `'<ring_id>-seg-<k>'`, k NON-contiguous because gate-colliding segments are dropped after numbering): `ring_id, structure_id, module_id, from_m, to_m, center_m, length_m (3 dp), rotation_degrees (4 dp), thickness_m, height_m, walkway: True`.
- **node** (`id` = `'<ring_id>-node-<i>'`): `kind:'vertex'`, `position_m`, `ring_id`. One per polyline vertex, never filtered.
- **gate** (`id` = `'<ring_id>-gate-0'`; postern is `'<ring_id>-postern-0'` and additionally carries `kind:'postern'`): `ring_id, module_key, module_id, structure_id, position_m, approach_m, thickness_m, height_m, curtain_height_m, walkway`.
- **tower** (`id` = `'<ring_id>-tower-<t_i>'`, t_i NON-contiguous): `module_key:'corner_join'`, `module_id`, `structure_id`, `position_m`, `thickness_m`, `height_m`, `curtain_height_m`, `walkway`.
- **stair** (`id` = `'<ring_id>-stair-<s_i>'`, contiguous): same shape, `module_key:'wall_stair'`.

`stats` = `{wall_rings_closed, segments, plots, workers, kit_id, walkway_continuous, unplaced_total}`; on the "no ring closed" early return it is the short `{wall_rings_closed:0, segments:0, plots:0, workers:0}`.
`debug` = `{valid_cells, approach_m (list or None), geometry_version:1}`.

### `road_connections[]` (both planners, ≤ 1 entry)

    {'route_id': 'support:<pin id>', 'route_index': -1, 'end': 'to', 'outside_index': 0,
     'junction_id': 'junction:<pin id>:support',
     'gate_local_m': [x, z],                 # metres, the window-boundary crossing
     'direction': [dx,dy,dz],                # unit globe direction of the gate point
     'status': 'unreachable', 'reason': 'No terrain-safe street connection to parent-city approach'}

The hamlet planner may upgrade an entry in place to `status='connected'`, `reason='Terrain-safe junction to parent-city approach'`, and add `local_path_m` (list of `[x,z]` metres ending at the gate). It also adds a `cell` key (`[i,j]` raster indices) to every entry, which survives into the output. **The castle planner never upgrades — a castle's `road_connections[0].status` is always `'unreachable'` and there is never a `local_path_m` or a `cell` key.**

## Algorithm

================================================================
PART 1 — SHARED MACHINERY (both planners rest on this)
================================================================

## 1.1 The local frame and the terrain sampler

Given a pin at grid `(sx, sz)` on an n×n grid with sphere radius R:

    lat  = pi/2 - pi*sz/(n-1)
    lon  = -pi + 2*pi*sx/(n-1)
    up    = ( cos(lat)*cos(lon),  sin(lat),  cos(lat)*sin(lon) )
    east  = ( -sin(lon),          0,         cos(lon)          )
    north = ( -sin(lat)*cos(lon), cos(lat), -sin(lat)*sin(lon) )

This is a **gnomonic** (central) projection: local metres `(x, z)` map to the unit direction

    p = normalize( up + (east*x + north*z)/R )

and the inverse (used to project world nodes into local metres) is

    den = dot(p, up);  if den <= 0.1 the node is discarded (below the tangent horizon)
    q   = ( R*dot(p,east)/den , R*dot(p,north)/den )

The `0.1` threshold is used by the river-line projection; the road/gate projection in
`support_road_entries` / `support_approach` uses `den <= 0` instead. Do not unify them.

**River lines.** For every `[i,j]` in `river_segments`, project `nodes[i]` and `nodes[j]`
to local metres. Keep the segment only if BOTH projections survive the horizon test AND
the segment's axis-aligned bounding box overlaps the window inflated by 40 m:

    for k in (0,1):  min(a[k],b[k]) <= half+40  and  max(a[k],b[k]) >= -half-40

`river_distance(x,z)` is the minimum point-to-segment distance over the kept lines,
with the standard clamped projection parameter `t = clamp(((x-ax)*dx + (z-az)*dz) / (dx*dx+dz*dz or 1), 0, 1)`
and `1e30` when there are no lines. **The `or 1` guards a zero-length segment.**

**`sample(x, z, with_slope=True)`** returns a record:

    p  = normalize(up + (east*x + north*z)/R)
    gx = round((atan2(p.z, p.x) + pi) / (2*pi) * (n-1))  mod (n-1)
    gz = clamp(round((pi/2 - asin(p.y)) / pi * (n-1)), 0, n-1)
    distance = river_distance(x, z)
    slope = layers.slope[gz][gx]            (0 if the layer is absent)
    if terrain_detail exists and with_slope:
        dhdx = (H(dir(x+1,z)) - H(dir(x-1,z))) / 2      # 1-metre central differences
        dhdz = (H(dir(x,z+1)) - H(dir(x,z-1))) / 2
        slope = degrees(atan(hypot(dhdx, dhdz)))
    return {
      'water':      layers.water_type[gz][gx] != 0  OR  distance < 12,
      'slope':      slope,                                   # degrees
      'channel':    1 if (lines non-empty AND layers.river[gz][gx] > 0.5 AND distance < 28) else 0,
      'flood_risk': layers.flood_risk[gz][gx],
      'height':     H(p),                                    # metres
      'moisture':   layers.moisture[gz][gx]   (default 0.5),
      'biome':      layers.natural_biome[gz][gx] (default 3),
      'variant':    layers.biome_variant[gz][gx] (default -1),
    }

`H` is `HeightField.height`: bilinear interpolation of `layers.height` at `p`, plus, when
`terrain_detail` is present, three octaves of spherical Perlin noise scaled by a mask that
zeroes over water, near shorelines, in river/flood cells and by biome. Three independent
factors can each zero the detail and the function returns the bare bilinear base when any
does. The hamlet's sampler exposes a cheap `height_at(x,z)` that returns exactly `H(dir(x,z))`
and skips the river loop, the two inverse-trig grid lookups and the layer reads. The castle
planner has **no** `height_at`; it calls `sample(x, z, False)` instead, which computes the
identical height but pays for the river loop and the layer reads.

`resolution` (both) = `pi*R/(n-1)` — the metre span of one raster cell of the world grid.

## 1.2 `footprint_cells(x, z, w, d, angle, half, cell=4)` — rotated-rect rasterization

Conservative: a cell is IN the footprint if the cell CENTRE lies inside the rectangle
inflated by the cell's half-diagonal projected on the rectangle's axes.

    c = cos(angle);  s = sin(angle)
    corners = [(x + u*c - v*s, z + u*s + v*c) for (u,v) in
               ((-w/2,-d/2),(w/2,-d/2),(w/2,d/2),(-w/2,d/2))]
    lo[k] = floor((min over corners of coord k + half)/cell)
    hi[k] = floor((max over corners of coord k + half)/cell)
    r      = cell/2 * (|c| + |s|)
    half_w = w/2 + r - 1e-8
    half_d = d/2 + r - 1e-8
    for j in lo[1]..hi[1]:
      dz = -half + (j+0.5)*cell - z
      for i in lo[0]..hi[0]:
        dx = -half + (i+0.5)*cell - x
        if |dx*c + dz*s| < half_w  and  |-dx*s + dz*c| < half_d:  add (i, j)

Note: the tests are **strict `<`**, and the `1e-8` is subtracted (shrinking, not growing).
The returned cell indices are NOT clamped to `[0, size)`; callers rely on a
set-containment test against `valid` to reject anything off the raster.

`corners(x,z,w,d,angle)` returns the four corner points in the order above and is used
separately for the four interpolated ground probes.

## 1.3 `_clearance(free, size)` — Chebyshev distance transform

Two sweeps over a `size × size` integer grid indexed `[z][x]`.

    far = 2*size
    grid[j][i] = far if (i,j) in free else 0

Forward sweep j = 0..size-1, i = 0..size-1, skipping cells already 0:
    best = grid[j][i-1]+1 if i>0 else 1
    if j == 0:  best = min(best, 1)
    else:       best = min(best, grid[j-1][i]+1,
                                 grid[j-1][i-1]+1 (if i>0),
                                 grid[j-1][i+1]+1 (if i+1<size))
    grid[j][i] = min(grid[j][i], best)

Backward sweep j = size-1..0, i = size-1..0, same shape with `i+1` and `j+1`.

Result: Chebyshev cell distance from each free cell to the nearest cell that is NOT free
(edges of the raster count as not-free, hence the `best = 1` at the borders).
Blocked cells hold 0.

Used as a **rasterization skip**: if the nearest non-free cell is within `(k+1)*cell*sqrt(2)`
Euclidean of the plot centre and that is `<= min(w,d)/2` (the inscribed-circle radius), the
plot certainly overlaps blocked ground, so the footprint is never built. It can never reject
a candidate the real tests would have accepted.

## 1.4 `grow_roads(valid, height_fn, size, seed, spacing_cells, required, branch_floor)`

A seeded least-cost tree over the buildable cell set. Steps:

1. If `valid` is empty, return `(empty set, empty list)`.
2. `rng = Random(seed)` (seed is the 64-bit integer described under RNG).
3. `root = min(valid, key=lambda c: ((c0 - size/2)^2 + (c1 - size/2)^2, c))` — the cell
   nearest the raster centre, ties broken by `(i, j)` lexicographic order.
4. Draw `phases = [rng.uniform(-pi, pi) for _ in range(3)]` — **three draws, in order**.
5. Build `heights[c] = height_fn(c)` and a smooth travel-cost field, both iterating
   `sorted(valid)`:

       cost[c] = 1
               + 0.45*(1 + sin(c0/9  + phases[0]) * cos(c1/11 + phases[1]))
               + 0.30*(1 + sin((c0+c1)/17 + phases[2]))

   (range roughly 1.0 … 2.35). The comment records the intent: *"A smooth travel-cost field
   produces persistent bends, rather than white-noise zigzags."*
6. **Dijkstra from `root`** over the 8-neighbourhood, in the fixed neighbour order
   `(-1,0), (0,-1), (0,1), (1,0), (-1,-1), (-1,1), (1,-1), (1,1)`, with a binary min-heap
   keyed on `(cost, cell)` and the standard stale-entry check `if cost != distances[c]: continue`.

   Edge admissibility:
   - `q` must be in `valid`.
   - **No corner cutting:** for a diagonal step both orthogonal support cells
     `(x+dx, z)` and `(x, z+dz)` must be in `valid`.
   - **Diagonal step height guard:** for a diagonal step, reject if any of the four
     pairs (support cell r ∈ {(x+dx,z),(x,z+dz)}) × (end cell ∈ {c, q}) has
     `|heights[r] - heights[end]| > 1.4` metres.
   - `length = hypot(dx, dz) * 4` metres (4 = CELL).
   - `grade = |heights[q] - heights[c]| / length`; reject if `grade > 0.35`.
   - Edge weight `= length * ((cost[c] + cost[q])/2 + 18*grade*grade)`.
7. `reachable = sorted(distances)`.
8. `targets = sorted(set(required) & set(distances))` — the gate cells that are reachable.
9. `count = max(branch_floor, min(36, round(len(valid) / max(12, spacing_cells)**2 * 1.5)))`.
   For a hamlet, `spacing_cells = 48/4 = 12` so the divisor is 144, and `branch_floor = 2`.
   (City uses `branch_floor = 12`.)
10. `candidates = rng.sample(reachable, min(len(reachable), max(256, count*20)))` — one
    `sample` draw of that many distinct elements.
11. Farthest-point seeding, `count` times: pick
    `target = max(candidates, key=lambda c: (min over q in [root]+targets of (c0-q0)^2+(c1-q1)^2, c))`,
    append it to `targets`, remove it from `candidates`. Break early if `candidates` empties.
    **`targets` grows inside the loop, so each pick is farthest from everything chosen so far.**
12. Trace each target back through `previous` until a cell already in `network`; if the
    traced path has more than one cell, append `reversed(path)` to `paths` and add the path
    to `network`. Targets are processed in list order (gates first, then the farthest picks
    in pick order).
13. Widen: `roads = set(network)`; for each `(x,z)` in `sorted(network)`, for each
    orthogonal neighbour `q` in `((1,0),(-1,0),(0,1),(0,-1))`, add `q` to `roads` if
    `q in distances` and `|heights[q] - heights[(x,z)]| <= 1.4`.
    Comment: *"Diagonal centerline steps have orthogonal safe support cells (no corner cutting)."*
14. Return `(roads, paths)`.

## 1.5 `_Replay` — a memoised generator

`candidates(w,d)` must be iterable any number of times, always in the same order, but
`install` takes the first free option and stops. `_Replay` wraps the generator: each
iteration reads the memo first and pulls new items from the source only past the memo's
end, appending as it goes. `len()` forces the entire stream.

Comment, worth carrying: *"`candidates` used to build the whole anchor sweep for every plot
size, but `install` takes the first free option and stops: between 22% and 61% of each list
was built and never looked at. Yielding on demand and keeping what was yielded leaves the
order and the first match identical — no plot moves — and simply stops producing options
past the deepest one anybody asked for."*

================================================================
PART 2 — `plan_hamlet(world, hamlet)`
================================================================

### Step H1 — resolve the building programme

`preset = hamlet_plan(hamlet['population_profile'])`: the civilization's `hamlet` block,
with each `buildings[]` row merged **structure-first** (`{**structure, **row}`), so the row
supplies `structure_id`, `name`, `count`, `placement_conditions` and the structure supplies
`priority`, `plot_m`, `dimensions_m`, `staffing`, `module_family`, etc.
`house = section('housing_profiles')['hamlet_house']`.

### Step H2 — crop the window

    half = 100                                        # HALF_DEFAULT; 200 m across
    sites = list(settlements.sites)
      + [every other hamlet in humans.hamlets whose identity and id differ from this one]
    for other in sites:
        lat1 = pi/2 - pi*hamlet.z/(n-1)
        lat2 = pi/2 - pi*other.z/(n-1)
        dl   = 2*pi*(other.x - hamlet.x)/(n-1)
        distance = R * acos(clamp(sin(lat1)*sin(lat2) + cos(lat1)*cos(lat2)*cos(dl), -1, 1))
        half = min(half, max(4, distance*0.5 - 8))    # NEIGHBOUR_GAP = 8
    half = max(4, int(half/4)*4)                      # truncate toward zero to a 4 m multiple
    size = 2*half // 4

Intent recorded in the docs: *"Neighbouring cities and hamlets clip windows to half the metre
distance minus an 8 m gap, so packed footprints do not overlap. Version 1 used the city 0.28
distance crop, which left clustered rural sites too small for cottages."*
**These are absolute metres, pre-scaling** — 100 m and 8 m were tuned for the ~11.15 km
reference world, and they are NOT multiplied by any world-size factor here. Read the
`globe_radius` the caller resolved; the constants stay literal.

Note the great-circle formula uses only the LONGITUDE difference `dl`; `hamlet.x`/`other.x`
are grid columns and wrap is NOT applied, so a pair straddling the seam measures the long way
round. That is the existing behaviour.

### Step H3 — raster the window

`sample, resolution = sampler(world, hamlet, half)`.

    for j in 0..size-1:
      for i in 0..size-1:
        v = sample(-half + (i+0.5)*4, -half + (j+0.5)*4)
        heights[(i,j)] = v.height
        code = 1 if v.water else 2 if v.slope > 25 else 0
        valid.add((i,j)) when code == 0

`slopes` and `woods` are accumulated here and **never read** — dead code carried from the
city planner. Do not port them.

**The hamlet mask has no `channel` term**, unlike the city and castle masks. Only standing
water (or within 12 m of a routed river centreline, via `v.water`) and slope above 25° block
a cell. Comment: *"Coarse flood_risk is a regional layer, not a local inundation mask; coasts
would otherwise be empty."*

Surface grid: `surface_size = size+1`, `step_m = 2*half/(surface_size-1)` (= 4.0), and
`heights_m[j][i] = round(height_at(-half + i*step, -half + j*step), 4)`.

### Step H4 — assemble the skeleton record and the support gate

Build `result` with everything listed under Writes; set `status='unbuildable'`, `stats={}`.
Then `result['road_connections'] = support_road_entries(world, hamlet, half)`:

    path = hamlet.access_nodes                     # hamlet-first, city-last
    if len(path) < 2 or no water nodes:  return []
    prev = (0.0, 0.0)
    for index in path[1:]:
        if index out of range: break
        p   = direction(nodes[index].x, nodes[index].z, n)
        den = dot(p, up);  if den <= 0: break
        q   = ( R*dot(p,east)/den , R*dot(p,north)/den )
        if max(|q0|, |q1|) > half:
            # the segment prev->q leaves the window; find the first axis crossing
            ts   = [ ((half if q[k] > 0 else -half) - prev[k]) / (q[k] - prev[k])
                     for k in (0,1) if |q[k]| > half ]
            t    = min(ts)
            gate = [ prev[k] + t*(q[k] - prev[k]) for k in (0,1) ]
            emit the single entry described under Writes;  break
        prev = q

If the whole path stays inside the window, or the path is shorter than two nodes, there is
no entry at all.

`if resolution > half:` append the extra warning about coarse land categories.

### Step H5 — early return on empty terrain

If `len(valid) < 8`: fill `unplaced` with every preset building row
(`{building_id, count, reason:'No compatible buildable footprint'}`), set the short `stats`,
and return. `status` stays `'unbuildable'`.

### Step H6 — the seeded ellipse clip

    result.passes['map'].placed = len(valid);  result.passes['shape'].placed = 1
    spacing = 48;  rx = half*0.9;  rz = half*0.9/1.1
    seed   = big-endian uint64 of the first 8 bytes of
             sha256(f"{config.seed}:{hamlet_id}:hamlet-streets-v1")
    wobble = 0.08 + 0.04*((seed % 1000)/1000)                 # 0.08 … 0.12
    inside(x,z) := (x/(rx*(1 + wobble*sin(x*0.07 + seed))))^2
                 + (z/(rz*(1 + wobble*cos(z*0.09 + seed))))^2  < 1
    valid = { c in valid : inside(-half + (c0+0.5)*4, -half + (c1+0.5)*4) }

Comment: *"Seeded irregular ellipse keeps city shape catalogues independent."*

**MEASURED DEFECT, carry it.** `seed` is a full 64-bit integer (~2×10^18); as a double its
ULP is ~256. `x*0.07` and `z*0.09` are at most 9 over a 200 m window, so
`x*0.07 + seed == seed` exactly for every point in the window. The sine and cosine are
therefore **constant**, and `inside` is a plain axis-aligned ellipse with fixed semi-axes
`rx*(1 + wobble*sin(seed))` and `rz*(1 + wobble*cos(seed))`. Verified: seed 42, hamlet-0
gives `seed = 2071028793166100975`, `wobble = 0.119`, effective `rx = 100.71`, `rz = 81.88`,
area fraction 0.6476 — and the planner reports `shape_safe_cells = 1616 / 2500 = 0.6464`.
A C++ port that derives a small phase from the stream will produce a genuinely wobbly
ellipse and a visibly different hamlet. Decide deliberately (see Open questions).

If `len(valid) < 8` after the clip, take the same early return as H5.

### Step H7 — streets

    for each road_connection entry:
        entry['cell'] = [ clamp(floor((v + half)/4), 0, size-1) for v in entry.gate_local_m ]
    gates = [ tuple(entry.cell) for entry in entries if tuple(entry.cell) in valid ]
    road, paths = grow_roads(valid, heights.__getitem__, size, seed, 48/4, gates, branch_floor=2)

Comment: *"Rural streets stay inside the ellipse with a low branch floor; city 12-district
growth fills a 100 m window."* Note the hamlet passes the **ellipse-clipped** `valid`, where
the city passes the unclipped terrain set whenever it has gates.

Then try to promote each gate to `connected`:

    cell   = tuple(entry.cell);  gate = entry.gate_local_m
    point  = [-half + (v+0.5)*4 for v in cell]         # the cell centre in metres
    v      = sample(*gate)
    length = dist(point, gate)
    if cell in road and not v.water and not v.channel and v.slope <= 25
       and |v.height - heights[cell]| <= 0.35*length + 1e-6:
        approach = the first path in `paths` whose last cell == cell, else [cell]
        entry.status = 'connected'
        entry.reason = 'Terrain-safe junction to parent-city approach'
        entry.local_path_m = [ [-half+(a+0.5)*4, -half+(b+0.5)*4] for (a,b) in approach ] + [gate]

The `0.35` is the same max grade `grow_roads` enforces; `1e-6` absorbs float noise at
`length == 0`. The docs record why `v.channel` and not `v.flood_risk` is tested here
(version 3's fix): *"the gate deciding whether a plot reaches a street still required
`flood <= 0.65` from a sampler field that meant the regional layer whenever no river was
near, so on a flood_risk=1 cell the mask admitted the ground and the gate then refused every
plot on it."*

    result.roads        = [ list(c) for c in sorted(road) ]
    clearance           = _clearance(valid - road, size)
    result.street_paths_m = [ [[round(-half+(x+0.5)*4, 2), round(-half+(z+0.5)*4, 2)] for (x,z) in path]
                              for path in paths ]

### Step H8 — frontage anchors

    anchors = []
    for path in street_paths_m:
      for i in 0..len(path)-1:
        a = path[max(0, i-3)];  b = path[min(len(path)-1, i+3)]
        if a == b: continue
        (x, z) = path[i]
        angle = atan2(b1 - a1, b0 - a0)
        angle += radians( 5 * sin(x*0.13 + z*0.17 + (seed % 1000)) )
        anchors.append((x, z, angle))
    anchors = sorted(set(anchors), key=lambda p: (p0*p0 + p1*p1, p))

City comment on the jitter: *"Stable frontage variation follows the street, with modest
organic deviations."* Here the additive term uses `seed % 1000` (< 1000), so unlike the
ellipse wobble the sine argument is NOT absorbed and the jitter really varies along the street.
The deduplicating `set` then the sort by squared distance from the local origin (ties broken
by the `(x, z, angle)` tuple) is the entire placement priority: **nearest the anchor first**.

### Step H9 — the candidate stream (per plot size `(w, d)`)

Memoised by `(w, d)`. Yields, in anchor order, for each anchor and each side `(1, -1)`:

    dx = -sin(angle)*side;  dz = cos(angle)*side          # the street's left/right normal
    x = round(ax + dx*(d/2 + 8), 2)                       # 8 m setback; the CITY uses 10
    z = round(az + dz*(d/2 + 8), 2)
    degrees = round(degrees(angle), 4);  angle = radians(degrees)     # <-- see hazard note
    ci = int((x + half) // 4);  cj = int((z + half) // 4)
    if 0<=ci<size and 0<=cj<size and (clearance[cj][ci] + 1)*4*sqrt(2) <= min(w,d)/2:  skip
    footprint = footprint_cells(x, z, w, d, angle, half, 4)
    if not footprint ⊆ valid  or  footprint ∩ road ≠ ∅:  skip
    ground = [ height_at(px, pz) for (px,pz) in corners(x, z, w, d, angle) ]
           + [ heights[c] for c in sorted(footprint) ]
    if max(ground) - min(ground) > hypot(w, d)*tan(radians(25)):  skip
    ex = x - dx*d/2;  ez = z - dz*d/2                     # the plot's street-facing edge
    corridor = cached by (ax, az, ex, ez); otherwise
               union over t = 0..10 of footprint_cells(ax + (ex-ax)*t/10,
                                                       az + (ez-az)*t/10, 4, 4, 0, half, 4)
    if corridor ⊆ valid:  yield (x, z, degrees, ax, az, footprint, corridor, ground)

Comments to carry: *"Decided without rasterizing when the nearest cell that is not free
ground must fall inside the inscribed circle. See `_clearance`."* and *"An access corridor
depends only on its endpoints, so building it once per distinct pair is the same set. The key
is the endpoints as computed and not a tidied version of them, so this can only ever return
what the eleven rasterizations below would have produced."*

### Step H10 — `install(row, phase, kind='service')`

Walk `candidates(row.plot_m.width, row.plot_m.depth)` from the beginning. Take the first
option with `footprint ∩ occupied = ∅`, `footprint ∩ access = ∅`, `corridor ∩ occupied = ∅`
(note: `corridor ∩ access` is NOT tested — corridors may share). Then

    occupied |= footprint
    access   |= (corridor - footprint)
    append the plot record; increment the named pass's `placed`; return True

Return False if the stream is exhausted.

The hamlet has **no housing reservation and no apartment upgrade** — both exist only in the
city planner. `kind` only labels the plot.

### Step H11 — role conditions and the high/low split

    role = hamlet.get('role', '')
    coastal(role) := role in {'harbor','fishing','landing','harbor + fishing'}
                     or 'harbor' in role or 'fishing' in role or role == 'landing'
    conditions = {
      farming_support:             role == 'farming',
      resource_support:            role == 'resource',
      navigable_shore:             coastal(role),
      ore_and_fuel_supply:         role == 'resource',
      food_surplus:                role == 'farming',
      working_animals:             role == 'farming',
      water_collection_or_delivery: True,
      clear_sky_view:              True,
      isolation_access:            True,
      usable_groundwater: False, flowing_water: False, reliable_wind: False,
      clay_and_fuel_supply: False, medicinal_supply: False, route_crossing: False,
      grade_change: False, retention_needed: False, defended_perimeter: False,
    }

Then, **in preset order**:

    for row in preset.buildings:
      missing = [c for c in row.placement_conditions if not conditions.get(c, False)]
      if missing:
          unplaced.append({building_id: row.structure_id, count: row.count,
                           reason: 'Unverified prerequisite: ' + ', '.join(missing)})
          continue                                     # preserves preset order in `missing`
      (high if row.priority == 'core' else low).append(row)

`priority` comes from the merged STRUCTURE definition, not from the hamlet block row.

### Step H12 — the four packing passes

    for (priority, rows) in (('high', high), ('low', low)):
        if priority == 'low' and sum(p.workers - p.beds for p in plots) > 0:
            unplaced.extend({building_id, count, reason:
              'High-priority worker housing must be completed first'} for each remaining row)
            break
        for row in rows:
            failed = 0
            for _ in range(row.count):
                if not install(row, priority): failed += 1
            if failed: unplaced.append({building_id: row.structure_id, count: failed,
                       reason: 'No terrain-safe plot with road access remaining'})
        house_workers(priority + '_housing')

`house_workers(phase)`:

    workers = sum(p.workers for p in plots);  beds = sum(p.beds for p in plots)
    audit = {phase, workers, starting_beds: beds, houses_placed: 0, upgrades: 0,
             plots_exhausted: False}
    while beds < workers:
        if not install(house, phase, 'housing'):
            audit.plots_exhausted = True;  break
        audit.houses_placed += 1
        beds += house.worker_beds                  # 2 for building.hamlet_house
    audit.final_beds = beds;  audit.shortfall = max(0, workers - beds)
    debug.housing_passes.append(audit)

`upgrades` is always 0 — the key exists only so the hamlet audit has the same shape as the
city's. Do not implement densification.

### Step H13 — statistics and status

    workers = sum(p.workers);  beds = sum(p.beds)
    debug.shape_safe_cells   = len(valid)
    debug.road_cells         = len(road)
    debug.occupied_cells     = len(occupied)
    debug.access_cells       = len(access)
    debug.vacant_shape_cells = len(valid - road - occupied - access)
    debug.housing_frontage_candidates = len(candidates(house.plot_m.width, house.plot_m.depth))
        # NOTE: this FORCES the whole candidate stream and counts EVERY geometrically valid
        # option, occupied or not. The city computes a free-frontage count instead.
    core_ids    = { r.structure_id for r in preset.buildings if r.priority == 'core' }
    failed_core = sum(r.count for r in unplaced if r.building_id in core_ids)
    status      = 'complete' if (failed_core == 0 and beds >= workers) else 'partial'
    stats       = {workers, worker_beds: beds, housing_shortfall: max(0, workers-beds),
                   service_buildings: count of plots with kind=='service',
                   houses: count of plots with building_id == house.id,
                   apartments: 0, unplaced_core: failed_core,
                   unplaced_total: sum(r.count for r in unplaced),
                   reserved_plot_area_m2: sum(p.plot_m.width * p.plot_m.depth for p in plots)}

**MEASURED: `status` is never `'complete'` in practice.** Every role excludes at least one
`core`-priority building. Verified on the test fixture:
farming → `hamlet_staging_yard` unplaced (`unplaced_core = 1`);
resource → `hamlet_barn` + `hamlet_farmyard` (`unplaced_core = 2`);
`harbor + fishing` → barn + farmyard + staging yard (`unplaced_core = 3`).
All three return `'partial'`. A role-less (player-founded) hamlet fails all five.
Reproduce this, do not "fix" it — `status` is a documented consumer-facing vocabulary word
and changing it changes every consumer's reading of the world.

### Step H14 — `fill_hamlets(world, workers=None)`

    sites = sorted(humans.hamlets, key=lambda h: str(h['id']))     # LEXICOGRAPHIC
    env   = os.environ.get('ICARUS_CITY_WORKERS'); if workers is None and env: workers = int(env)
    if workers != 1 and len(sites) >= 3:
        try pooled planning (process pool, world pickled to each worker, one map pass);
        on ANY exception warn and fall back to serial.
    else / on fallback: [plan_hamlet(world, site) for site in sites]
    world['hamlet_plans'] = { … }
    world_scene.build_scene(world)

Comment recording why one pass suffices: *"plan_hamlet is a pure function of (world, hamlet)
and, unlike a city, carries no cross-hamlet state at all — no shape counter, nothing a later
hamlet reads from an earlier one. So the pool needs one pass, not the city planner's two."*
The worker re-sorts the pickled world's hamlet list identically and indexes it, so the pooled
result is order-identical to the serial one by construction — but **nothing in the repo ever
proved pooled output equals serial output**, and the fallback is deliberately noisy:
*"a silent fallback is indistinguishable from a planner that is simply slow, which is exactly
the thing nobody would investigate. Say it."*

================================================================
PART 3 — `castle_geometry`
================================================================

### G1 `turn_degrees(a, b, c)`

    v1 = a - b;  v2 = c - b
    delta = degrees(atan2(v1.z, v1.x) subtracted from atan2(v2.z, v2.x))
          # literally: degrees(ang2 - ang1) where ang1 = atan2(v1…), ang2 = atan2(v2…)
    normalise into (-180, 180] by adding 360 while delta <= -180 and subtracting while > 180

**The docstring says "Signed exterior turn at b going a->b->c". It is not.** It measures the
angle between the two edges AT b — the signed INTERIOR angle. Verified: on a regular convex
32-gon it returns ±166.5°, not ±11.25°. `city_fortifications.path_turn_degrees` next door
computes the real heading change. Keep this one as written; `corner_indices` depends on it.

### G2 `corner_indices(polyline, min_turn_degrees)`

Returns `[i for i in 0..n-1 if |turn_degrees(prev, p_i, next)| >= min_turn_degrees]`, with
wraparound indices. Returns `[]` when `len < 3`.

**Consequence of G1:** with `min_turn_degrees_for_corner = 25`, every vertex of any ring that
is not a near-180° spike qualifies. On the shipped 32-sample ellipse this returns all 32
indices. The tower budget, not the geometry, is what limits towers.

### G3 `can_join(a, b, rules, thickness_a, thickness_b, height_a, height_b)`

Returns `(ok, reason_or_None)`.

    if a is None or b is None:                       (False, 'Missing module')
    end_a = a.end_type;  end_b = b.end_type
    if either is None:                               (False, 'Module has no join end')
    if end_b not in a.compatible_ends and end_a not in b.compatible_ends:
                                                     (False, f'Incompatible ends {end_a}/{end_b}')
    ta/tb default to a/b .thickness_m.nominal;  ha/hb default to a/b .height_m.nominal
    if any of the four is None:                      (False, 'Missing thickness or height')
    if |ta-tb| > rules.max_thickness_delta_m:        (False, f'Thickness delta {…:.3f} exceeds {…}')
    if |ha-hb| > rules.max_height_delta_m:           (False, f'Height delta {…:.3f} exceeds {…}')
    if rules.walkway_required and a.walkway and b.walkway is False:  (False,'Walkway continuity broken')
    if rules.walkway_required and b.walkway and a.walkway is False:  (False,'Walkway continuity broken')
    otherwise                                        (True, None)

Note the walkway test uses **identity with `False`**, so a module with a missing/None walkway
flag does not trip it.

### G4 `ellipse_ring(rx, rz, samples=48)`

`[(round(rx*cos(2*pi*i/samples), 2), round(rz*sin(2*pi*i/samples), 2)) for i in 0..samples-1]`.
Open polyline — last point is NOT the first. The planner always passes `samples = 32`.

### G5 `clip_ring_to_valid(points, valid, half, cell, max_slope_deg, height_fn, slope_fn)`

Radial inward snapping **from the local origin (0,0)**, not from the centroid of `valid`.

    if points or valid is empty: return []
    snapped = []
    for (px, pz) in points:
        length = hypot(px, pz) or 1.0
        (ux, uz) = (px/length, pz/length)
        for dist in range(int(max(half,1)), 0, -cell):      # half, half-4, …, 4
            (x, z) = (ux*dist, uz*dist)
            i = floor((x+half)/cell);  j = floor((z+half)/cell)
            if (i,j) not in valid:                continue
            if slope_fn(i,j) > max_slope_deg:     continue
            snapped.append((round(x,2), round(z,2)));  break
        # a ray that finds nothing contributes NO point — the polyline shortens
    return smooth_closed_ring(snapped) if len(snapped) >= 8 else snapped

`height_fn` is accepted and **never called** — dead parameter.

**CRITICAL BEHAVIOURAL FACT.** The unit direction `(ux, uz)` of an ellipse point is
independent of a uniform scale on `(rx, rz)`. The planner builds each ring as
`ellipse_ring(rx*scale, rz*scale, 32)`, so **the `scale` in a kit's ring spec has essentially
no effect once clipping succeeds** — every ring collapses onto the same buildable boundary.
Verified on a flat fixture: the motte ring (`scale 0.38`) and the bailey ring (`scale 1.0`)
produce polylines differing only in the last 2-dp digit (`[86.74,14.85]` vs `[86.74,14.84]`),
purely from the 2-dp rounding of the raw points before normalisation. The visible consequence
is two bailey courts and a keep all installed at the same centre. `scale` matters only on the
fallback path where clipping collapses the ring and the raw ellipse is used unclipped.

Also note the loop starts at `dist = half`, whose grid index is `floor(2*half/cell) = size`,
always out of range — so the outermost radius ever achievable is `half - cell`.

`smooth_closed_ring(points, max_turn_degrees=95, radius_factor=1.12, passes=12)`
(borrowed from `city_fortifications`): if fewer than 8 points, return them unchanged.
Otherwise compute the centroid `(cx, cz)` once, then 12 passes of:
  1. radial cap — `radii[i] = hypot(p - c) or 1.0`; `cap = 1.12*max(radii[i-1], radii[i+1])`;
     move each point to radius `min(radii[i], cap)` along its own ray;
  2. needle smoothing — recompute neighbours from the just-capped set; where the
     `path_turn_degrees(prev, p, next)` heading change exceeds 95°, replace `p` with
     `(prev + 2*p + next)/4`.
Finally round every coordinate to 2 dp. Docstring: *"Pull in needle vertices so an enceinte
cannot form one giant outward point."*

### G6 `segmentize(polyline, segment_depth_m, structure_id, ring_id, thickness_m, height_m)`

Returns `(segments, nodes)`. `[]`, `[]` when `len(polyline) < 3`.

`nodes` = one `{'id': f'{ring_id}-node-{i}', 'kind': 'vertex', 'position_m': list(point), 'ring_id': ring_id}`
per vertex, in polyline order, never filtered.

Segmentation walks the CLOSED ring `polyline + [polyline[0]]`:

    seg_i = 0;  carry = 0.0;  path = []
    for (a, b) in consecutive pairs:
        path.append(a)
        edge = dist(a, b)
        while carry + edge >= segment_depth_m - 0.01:
            need  = segment_depth_m - carry
            t     = need/edge if edge else 0
            end   = (round(a0 + (b0-a0)*t, 2), round(a1 + (b1-a1)*t, 2))
            start = path[-1] if path else a
            mid   = ((start0+end0)/2, (start1+end1)/2)
            heading = degrees(atan2(end1-start1, end0-start0))
            emit segment { id: f'{ring_id}-seg-{seg_i}', ring_id, structure_id,
                           module_id: 'module.curtain_segment',
                           from_m: start, to_m: end, center_m: [round(mid,2) …],
                           length_m: round(dist(start,end), 3),
                           rotation_degrees: round(heading - 90, 4),
                           thickness_m, height_m, walkway: True }
            seg_i += 1;  path = [end];  a = end;  edge = dist(a, b);  carry = 0.0
        carry += edge

**Carry the consequence.** `start` is `path[-1]`, i.e. the most recent VERTEX or cut point on
this edge — not the point `carry` metres back. So the arc length consumed since the last cut
is `carry + need = segment_depth_m`, but the emitted chord only spans `need`. On a fine
polyline the segments are ragged: measured lengths on the shipped 32-gon with
`segment_depth_m = 10` were `4.90, 9.99, 9.69, 3.91, 10.00, 7.31, 10.00, 9.75, 1.17, 10.00,
1.74, 10.00, 1.85, 10.00 …`. Whether that is a defect or the intended "cut at vertices"
behaviour is not recorded anywhere; reproduce it.

`edge == 0` cannot loop forever: `t = 0`, `end == a`, `carry` resets to 0 and the guard fails.

### G7 `approach_gate_index(polyline, approach_m)`

    if not polyline:      return None
    if approach_m is None:
        return max(range(len(polyline)), key=lambda i: (polyline[i][0], -abs(polyline[i][1])))
        # "Prefer the +x extremity as a deterministic fallback approach."
        # max returns the FIRST maximal element on a tie.
    return min(range(len(polyline)), key=lambda i: dist(polyline[i], approach_m))
        # min returns the FIRST minimal element on a tie.

`opposite_index(polyline, index) = (index + len(polyline)//2) % len(polyline)`.

### G8 `validate_segment_chain(segments, nodes_by_join, modules, rules)`

    curtain = modules['curtain_segment']
    for seg in segments:
        can_join(curtain, curtain, rules, ta=seg.thickness_m, tb=seg.thickness_m,
                                          ha=seg.height_m,    hb=seg.height_m)
        on failure append {'segment_id': seg.id, 'reason': reason}
    for join in nodes_by_join:
        mod    = modules.get(join.get('module_key'))
        join_h = join.get('curtain_height_m', curtain.height_m.nominal)
           # "Node structures may rise above the walkway; join continuity uses curtain height."
        can_join(curtain, mod, rules, ta=join.thickness_m, tb=join.thickness_m,
                                      ha=join_h,            hb=join_h)
        on failure append {'node_id': join.get('id'), 'reason': reason or 'join failed'}
    return failures

**Note what this actually checks.** Both thickness arguments are the same value and both
height arguments are the same value, so the delta tests are structurally satisfied. What can
still fail is a missing module (`modules.get` returns None), a missing `end_type`, an
incompatible end pair, and the walkway rule. With the shipped `castles.json` none of those
can fire for any node the planner builds, so this validation is inert against the current
data. Keep it — `castles.json` is data and an edited registry must still be rejected.

### G9 `build_wall_network(*, ring_id, role, polyline, approach_m, ring_spec, modules, rules, ditch=False)`

    report = {id, role, polyline_m: [list(p) …], status:'open', segments:[], nodes:[],
              gates:[], towers:[], stairs:[], walkway_continuous: False, unplaced:[]}

 1. `if len(polyline) < 8:` append `{'building_id':'building.curtain_segment',
    'reason':'Perimeter could not close'}` and return (status stays `'open'`).
 2. `curtain = modules['curtain_segment']`; `thickness = curtain.thickness_m.nominal` (3.0);
    `height = curtain.height_m.nominal` (8.0).
 3. `segments, vertices = segmentize(polyline, rules.segment_depth_m, curtain.structure_id, ring_id, thickness, height)`.
 4. `if len(segments) < 6:` append `{'building_id': curtain.structure_id,
    'reason':'Insufficient continuous wall segments'}` and return.
 5. `gate_idx = approach_gate_index(polyline, approach_m)`;
    `gate_key = ring_spec.get('gate') or 'gatehouse'`; `gate_mod = modules[gate_key]`.
    Build the gate record with `position_m = polyline[gate_idx]` and
    `approach_m = list(approach_m) if approach_m else list(polyline[gate_idx])`.
 6. If `ring_spec.postern`: `post_idx = opposite_index(polyline, gate_idx)`, module `'postern'`,
    `approach_m = polyline[post_idx]`, extra key `kind: 'postern'`, id `'<ring>-postern-0'`.
 7. **Drop colliding segments** — `keep` = segments whose `center_m` is at least
    `max(8, rules.segment_depth_m * 0.6)` from EVERY gate position. (`= max(8, 6) = 8` with
    the shipped rules.) Comment: *"Drop curtain segments that collide with gate centres."*
    Segment ids are assigned before this filter, so kept ids are non-contiguous.
 8. **Towers** — iterate `enumerate(corner_indices(polyline, rules.min_turn_degrees_for_corner))`:
        if t_i >= ring_spec.get('tower_budget', 4): break
        if any gate within 10 m of polyline[idx]: continue        # BUDGET STILL CONSUMED
        append tower id f'{ring_id}-tower-{t_i}' with module 'corner_join'
    Because `t_i` counts candidates and not placements, a gate-adjacent vertex both skips a
    tower and eats a budget slot, and tower ids skip numbers. Verified: `tower_budget = 4`
    yielded ids `tower-1, tower-2, tower-3` — three towers, not four.
 9. **Stairs** — `stair_budget = ring_spec.get('stairs', 0)`; if budget and `keep` non-empty:
        step = max(1, len(keep) // stair_budget)
        for s_i, seg in enumerate(keep[::step][:stair_budget]):
            append stair at `seg.center_m`, module 'wall_stair'
10. `joins = gates + towers + stairs`; `failures = validate_segment_chain(keep, joins, modules, rules)`.
    On failure: extend `unplaced` with `{'building_id': f.segment_id or f.node_id, 'reason': f.reason}`,
    set `status = 'broken'`, attach `keep`/`vertices`/gates/towers/stairs, return.
11. Otherwise `status='closed'`, `segments=keep`, `nodes=vertices`, `walkway_continuous=True`,
    `segment_count=len(keep)`, `gate_count=len(gates)`, `ditch = bool(ditch or ring_spec.get('ditch'))`.

================================================================
PART 4 — `plan_castle(world, fortress)`
================================================================

### C1 — kit selection

    rng = Random( sha256(f'{config.seed}:castle-kit:{fortress.id}').hexdigest() )
    weights = [ max(1, int(k.get('weight', 1))) for k in doc.kits ]
    kit = rng.choices(doc.kits, weights=weights, k=1)[0]

**This is the "64-char hex STRING" seeding path.** CPython's `random.Random(str)` encodes the
string to UTF-8 bytes, appends `sha512(bytes)`, reinterprets the whole thing as one big
integer and runs MT19937's `init_by_array` on it. In a clean rewrite this must become a single
named stream and a single uniform draw. The weighted choice itself is exactly:

    cum   = running sums of weights = [4, 7, 9] for the shipped kits
    total = cum[-1] + 0.0
    u     = rng.random() * total
    index = bisect_right(cum, u, lo=0, hi=len(kits)-1)       # NOTE hi clamps to the last kit

so P(simple_enclosure) = 4/9, P(motte_bailey) = 3/9, P(concentric) = 2/9, and `u >= 7`
selects the last kit because `hi` clamps. One draw, always.

### C2 — crop the window

    half  = kit['half_m']                              # 72 / 88 / 96
    sites = list(settlements.sites) + [other fortresses with a different identity and id]
    for other in sites:  (same great-circle distance as H2)
        half = min(half, distance * 0.28)              # 0.28, NOT the hamlet's 0.5 - 8
    half = max(4, int(half/4)*4)
    size = 2*half // 4

There is **no `max(CELL, …)` inside the loop and no neighbour gap**; a fortress packed hard
against a city can be cropped to the 4 m floor. The 0.28 is the city planner's crop constant.
**Absolute metres, pre-scaling**, like the hamlet's.

### C3 — raster the window

`sample = _sampler(world, fortress, half)` — a duplicated, `height_at`-less copy of the city
sampler. *"Local east/north metre sampler; duplicated to avoid importing city_planner."*

    heights[(i,j)] = v.height;  slopes[(i,j)] = v.slope          # slopes IS used, unlike the hamlet
    code = 1 if v.water else 2 if (v.slope > 25 or v.channel) else 0

Comment: *"See city_planner: the regional flood proxy cannot block a cell it cannot resolve.
34 of 56 castles were unbuildable on this rule."* Note the castle mask DOES include
`v.channel`; the hamlet's does not.

Surface grid uses `sample(x, z, False)['height']`, not a `height_at`.

### C4 — skeleton and the first two passes

Build `result` (see Writes). `passes['map'].placed = 1` (a literal 1, not a cell count —
the hamlet's `map` pass carries the valid-cell count instead).

    if len(valid) < 24:
        unplaced.append({'building_id':'building.curtain_segment', 'count':1,
                         'reason':'Insufficient buildable land for a fortress precinct'})
        return result                                   # status stays 'unbuildable', stats == {}

    passes['kit'].placed = 1
    approach, entries = support_approach(world, fortress, half)   # identical to H4's routine,
                                                                 # but also returns the gate point
    result.road_connections = entries

### C5 — wall rings

    rx = half * 0.72;   rz = half * 0.62
    for ring_i, ring_spec in enumerate(kit.rings):
        scale    = ring_spec.scale
        ring_id  = f"ring-{ring_spec.role}"
        raw      = ellipse_ring(rx*scale, rz*scale, 32)
        polyline = clip_ring_to_valid(raw, valid, half, 4, 25,
                                      lambda i,j: heights[(i,j)],    # never called
                                      lambda i,j: slopes[(i,j)])
        if len(polyline) < 8:
            polyline = raw        # "Fall back to unclipped ellipse when terrain clipping
                                  #  collapses the ring."
        network = build_wall_network(ring_id=ring_id, role=ring_spec.role, polyline=polyline,
                                     approach_m = approach if ring_i == 0 else None,
                                     ring_spec=ring_spec, modules=doc.modules, rules=doc.join_rules,
                                     ditch=ring_spec.get('ditch'))
        networks.append(network)
        if network.status != 'closed':
            unplaced.extend({'building_id': u.get('building_id','building.curtain_segment'),
                             'count': u.get('count', 1),
                             'reason': u.get('reason','Wall network failure')}
                            for u in network.get('unplaced', []))
    result.wall_networks = networks
    passes['perimeter'].placed = count of closed networks
    if no network closed:
        status = 'partial';  stats = {wall_rings_closed:0, segments:0, plots:0, workers:0}
        return result

**Only ring 0 gets the real approach** — every inner ring's gate falls back to the +x extremity.

### C6 — `install(structure_id, x, z, degrees, phase, kind='service')`

    row    = structures[structure_id]                # from structure_blocks.castle
    ground = the four rotated corner samples of the PLOT rectangle:
             for (u,v) in ((-w/2,-d/2),(w/2,-d/2),(w/2,d/2),(-w/2,d/2)) with
             w = row.plot_m.width, d = row.plot_m.depth, rad = radians(degrees):
                 sample(x + u*cos(rad) - v*sin(rad), z + u*sin(rad) + v*cos(rad))['height']
    plot = { id: f'plot-{len(plots)}', building_id, name, kind, phase,
             x_m: round(x,2), z_m: round(z,2), rotation_degrees: round(degrees,4),
             ground_elevation_m: round(max(ground),4), foundation_bottom_m: round(min(ground),4),
             plot_m: row.plot_m, dimensions_m: row.dimensions_m,
             road_access: [round(x,2), round(z,2)],
             workers: sum(r.target for r in row.staffing.roles) }
    plots.append(plot);  passes[phase].placed += 1;  return plot

**There is no footprint test of any kind.** No `valid` containment, no occupancy set, no
overlap rejection, no slope test. A castle plot may overlap another plot, sit on water, or lie
outside `bounds_m` entirely (measured: a drawbridge at `x_m = -98.0` in a window with
`half = 88`). This is deliberate to the extent that the module says "schematic"; reproduce it.

### C7 — courts and landmarks

    ring_polylines = { n.role: n.polyline_m for n in networks if n.status == 'closed' }
        # dict keyed by ROLE; a kit with two rings of the same role would lose one
    for court in kit.get('courts', []):
        ring = ring_polylines.get(court.ring_role)
        if not ring: unplaced.append({building_id: court.structure_id, count: 1,
                     reason: f"No closed ring {court.ring_role} for court"});  continue
        cx = mean of ring x;  cz = mean of ring z
        install(court.structure_id, cx, cz, 0, 'courts', kind='court')
    for landmark in kit.get('landmarks', []):
        same, with reason f"No closed ring {landmark.ring_role} for keep" and
        install(..., 'landmarks', kind='landmark')

`offset_scale` appears in every landmark entry in `castles.json` and is **never read**.
Because clipping collapses all rings onto the same boundary (see G5), all courts and the keep
land on the same centroid.

### C8 — bailey services

    primary = networks[0]
    if primary.status == 'closed':
        poly = primary.polyline_m;  cx, cz = centroid of poly
        angle0 = 0.0
        if approach:  angle0 = atan2(approach[1] - cz, approach[0] - cx)
        service_i = 0
        for service in kit.get('services', []):
            sid = service.structure_id
            if sid not in structures:
                unplaced.append({sid, 1, 'Unknown castle structure'});  continue
            if sid == 'building.drawbridge':
                if not primary.get('ditch') or not primary.get('gates'):
                    unplaced.append({sid, 1, 'Drawbridge requires outer ditch and gate'});  continue
                gate = primary.gates[0];  (gx, gz) = gate.position_m
                install(sid, gx + cos(angle0)*10, gz + sin(angle0)*10,
                        degrees(angle0), 'services')
                continue                              # does NOT advance service_i
            if sid == 'building.barbican' and any(g.structure_id == 'building.barbican'
                                                  for g in primary.get('gates', [])):
                continue                              # "Already represented as the gate node."
            for _ in range(service.get('count', 1)):
                ang  = angle0 + pi*0.5 + service_i*(pi*0.35)
                dist = half * 0.22
                install(sid, cx + cos(ang)*dist, cz + sin(ang)*dist,
                        degrees(ang + pi/2), 'services')
                service_i += 1

Comment: *"Place services around the primary bailey centre, offset from the keep."*
The `pi*0.35` fan (63°) exceeds a full turn after six services and the rotation is never
wrapped, which is where the >360° `rotation_degrees` come from.

### C9 — perimeter plots

    for network in networks:
        if network.status != 'closed': continue
        for gate in network.gates:
            if any existing plot has p.building_id == gate.structure_id
               and dist((p.x_m, p.z_m), gate.position_m) < 1:  continue
            install(gate.structure_id, gate.position_m[0], gate.position_m[1], 0, 'perimeter', kind='gate')
        for tower in network.towers:
            install(tower.structure_id, …, 0, 'perimeter', kind='tower')      # NO dedup
        for stair in network.stairs:
            install(stair.structure_id, …, 0, 'perimeter', kind='stair')      # NO dedup

Comment: *"Emit gate/tower/stair plots for consumers that only read plots[]."*
The 1-metre dedup applies only to gates, and only against an EXACT `building_id` match.

### C10 — statistics and status

    workers  = sum(p.workers for p in plots)
    closed   = count of networks with status == 'closed'
    segments = sum(len(n.segments) for n in ALL networks, closed or not)
    stats    = {wall_rings_closed: closed, segments, plots: len(plots), workers,
                kit_id: kit.id,
                walkway_continuous: all(n.walkway_continuous for n in networks if closed),
                unplaced_total: len(unplaced)}          # LENGTH, not the sum of `count`
    status   = 'complete' if (closed == len(kit.rings) and not unplaced) else 'partial'
    debug    = {valid_cells: len(valid), approach_m: approach, geometry_version: 1}

Note `unplaced_total` is a row count here and a `count` sum in the hamlet. Note also that
`all(…)` over an empty sequence is True, but the branch is only reached when at least one ring
closed.

### C11 — `fill_castles(world)`

    for fortress in sorted(humans.fortresses, key=lambda f: str(f['id'])):   # LEXICOGRAPHIC
        castles.append(plan_castle(world, fortress))
    world['castle_plans'] = { … }
    world_scene.build_scene(world)

**Always serial.** No process pool, no `ICARUS_CITY_WORKERS`.

### C12 — `planner_identity()`

    doc        = load_castles()                         # validated, lru_cached
    structures = { s.id: s for block in structure_blocks['castle'].blocks for s in block.structures }
    castles_sha256          = sha256(json.dumps(doc,        sort_keys=True, separators=(',',':')))
    castle_buildings_sha256 = sha256(json.dumps(structures, sort_keys=True, separators=(',',':')))
    return {version: 3, geometry_version: 1, castles_sha256, castle_buildings_sha256}

`load_castles()` raises `ValueError('Unsupported castle registry schema')` unless
`schema == 'fantasy-world-generator.castle-registry'` and `schema_version == 1`, and
`ValueError('Invalid castle registry revision')` unless `revision` is an `int >= 1`.

The hamlet's `planner_identity()` is `{version: 3, registry: registry_identity()}`, where
`registry_identity()` is `{schema_version, revision, sha256}` of the FULLY RESOLVED, merged
civilization+building document serialised with `ensure_ascii=False, separators=(',', ':')`
and **no key sorting** — i.e. Python dict insertion order is part of the hash.

## Constants

**hamlet_planner**

| Name | Value | Meaning / effect of changing it |
|---|---|---|
| `VERSION` | 3 | Written into every plan and into `planner_identity`. Bumping it invalidates every saved world at the age-advance identity gate. |
| `HALF_DEFAULT` | 100 | Half-extent of the hamlet window in **absolute metres, pre-scaling**. 200 m across. Comment: *"smaller than small-city 480 m"*. Raising it grows the raster quadratically (`size = 2*half/4`) and lets more buildings fit; lowering it below ~40 leaves too few cells for cottages. |
| `NEIGHBOUR_GAP` | 8 | **Absolute metres** reserved between facing hamlet windows. `half = min(half, distance*0.5 - 8)`. Set to 0 and adjacent hamlets' windows touch; raise it and dense clusters crop to the 4 m floor. |
| `CELL` (from city_planner) | 4 | Raster cell size in metres. Everything scales off it: window raster, corridor width (a 4×4 rasterised square), the `grow_roads` edge length `hypot(dx,dz)*4`, the clearance-to-metres conversion. |
| `_ROOT2` | `sqrt(2)` | The Chebyshev→Euclidean bound in the clearance skip. |
| slope block threshold | 25° | `code = 2` above it. Also the gate test and the plot-flatness test's `tan(radians(25))`. |
| river water setback | 12 m | Inside `sample`: `water` is true within 12 m of a routed centreline. |
| river channel setback | 28 m | `channel = 1` within 28 m when `river > 0.5`. Not used by the hamlet cell mask, only by the gate test. |
| river line bbox slack | 40 m | Inflates the window before a river segment is kept. |
| horizon `den` cutoffs | 0.1 (river projection), 0.0 (road projection) | Two different values on purpose. |
| ellipse `rx` | `half * 0.9` | |
| ellipse `rz` | `half * 0.9 / 1.1` | The 1.1 is the fixed `aspect_ratio` in the literal `shape` block. |
| wobble | `0.08 + 0.04*((seed % 1000)/1000)` → 0.08…0.12 | Perturbs the two semi-axes. See the measured absorption defect. |
| wobble frequencies | 0.07 (x), 0.09 (z) | Inert in practice (absorbed by the 64-bit seed). |
| `spacing` | 48 m | Only feeds `grow_roads`' `spacing_cells = 48/4 = 12`. Also the literal `block_spacing_m` in `shape.parameters`. |
| `branch_floor` | 2 | Minimum street branches. City uses 12. Comment: *"Rural streets stay inside the ellipse with a low branch floor; city 12-district growth fills a 100 m window."* |
| plot setback from anchor | `d/2 + 8` | **The city uses `d/2 + 10`.** Metres. Narrows or widens the street. |
| anchor lookahead | ±3 path points | Direction is `atan2` over `path[i-3] → path[i+3]`. |
| anchor jitter | `5 * sin(x*0.13 + z*0.17 + seed%1000)` degrees | ±5°. This one genuinely varies. |
| corridor rasterisation steps | 11 (`t = 0..10`, `t/10`) | Eleven 4×4 squares along the plot-to-anchor line. |
| plot flatness limit | `hypot(w,d) * tan(radians(25))` metres of relief across the eight ground probes | |
| gate height tolerance | `0.35 * length + 1e-6` metres | 0.35 is the same max grade `grow_roads` uses. |
| `_MIN_SITES_FOR_POOL` | 3 | Below this, planning is serial regardless of `ICARUS_CITY_WORKERS`. |

**grow_roads** (shared)

| Value | Meaning |
|---|---|
| `0.45`, `/9`, `/11`, `0.3`, `/17` | The smooth cost field. Range ≈ 1.0…2.35. Longer periods = longer straight runs. |
| `1.4` m | Both the diagonal-step height guard and the road-widening height guard. |
| `0.35` | Max admissible grade (rise/run). Above it an edge does not exist at all. |
| `18` | Grade penalty coefficient in `length*((c_a+c_b)/2 + 18*grade²)`. Raising it makes roads contour harder. |
| `36` | Hard cap on branch count. |
| `1.5`, `max(12, spacing_cells)²` | Branch-count heuristic: `round(len(valid)/144 * 1.5)` for a hamlet. |
| `max(256, count*20)` | Size of the random candidate pool for farthest-point seeding. |

**castle_planner**

| Name | Value | Meaning |
|---|---|---|
| `VERSION` | 3 | |
| `GEOMETRY_VERSION` | 1 | From `castle_geometry.VERSION`; goes into `identity` and `debug`. |
| `CELL` | 4 | Own copy, same value. |
| `PHASE_ORDER` | `['map','kit','perimeter','courts','landmarks','services']` | |
| neighbour crop | `distance * 0.28` | **Absolute metres after the multiply; the 0.28 is dimensionless.** The city planner's constant, kept. |
| minimum buildable cells | 24 | Below it, `'Insufficient buildable land for a fortress precinct'`. The hamlet's threshold is 8. |
| `rx` | `half * 0.72` | |
| `rz` | `half * 0.62` | Fixed 0.72/0.62 aspect for every kit. |
| ring samples | 32 | Passed to `ellipse_ring`. |
| clip max slope | 25° | |
| drawbridge offset | 10 m along `angle0` from the gate | |
| service fan start | `angle0 + pi*0.5` | 90° off the approach. |
| service fan step | `pi*0.35` (63°) | Overruns a full turn after 6 services. |
| service radius | `half * 0.22` | |
| service rotation | `degrees(ang + pi/2)` | Unwrapped; exceeds 360°. |
| gate dedup radius | 1 m | |

**castles.json `join_rules`**

| Key | Value | Effect |
|---|---|---|
| `max_thickness_delta_m` | 0.5 | Inert against the shipped data (both sides of every check carry the same value). |
| `max_height_delta_m` | 1.0 | Likewise. |
| `min_turn_degrees_for_corner` | 25 | With `turn_degrees` measuring the interior angle, this admits essentially every vertex. Raising it toward 180 would start excluding straight runs — i.e. the sense is inverted from the intent. |
| `max_turn_degrees` | 135 | **Never read anywhere.** Dead. |
| `segment_depth_m` | 10 | Curtain run target length, and (via `max(8, depth*0.6)`) the gate-collision radius. |
| `walkway_required` | true | Enables the two walkway continuity clauses in `can_join`. |

**castles.json module nominals used by the planner**: curtain thickness 3.0, curtain height 8.0
(these two become every segment's, gate's, tower's and stair's `thickness_m` and
`curtain_height_m`); gatehouse height 14.0; postern height 8.0; corner_join height 14.0;
wall_stair height 8.0.

**castles.json kits** (`weight`, `half_m`, ring `scale`/`ditch`/`gate`/`postern`/`stairs`/`tower_budget`)

- `kit.simple_enclosure` — weight 4, half 72; one ring `outer` scale 1.0, no ditch, gate `gatehouse`, postern true, 2 stairs, tower budget 4. Courts: 1 bailey. Landmark: keep on `outer`. Services: barracks, armory, well, great_hall.
- `kit.motte_bailey` — weight 3, half 88; rings `bailey` (1.0, ditch, gatehouse, postern, 2 stairs, 4 towers) and `motte` (0.38, no ditch, gate `postern`, no postern, 1 stair, 2 towers). Courts on both. Landmark: keep on `motte`. Services: barracks, armory, well, chapel, great_hall, drawbridge.
- `kit.concentric` — weight 2, half 96; rings `outer` (1.0, ditch, gatehouse, postern, 2 stairs, 6 towers) and `inner` (0.55, no ditch, gatehouse, no postern, 2 stairs, 4 towers). Courts on both. Landmark: keep on `inner`. Services: barracks, armory, well, chapel, great_hall, barbican, drawbridge.

**`smooth_closed_ring` defaults** (from `city_fortifications`): `max_turn_degrees = 95`,
`radius_factor = 1.12`, `passes = 12`, minimum 8 points to do anything.

**Housing**: `building.hamlet_house` — dimensions 5×6×4 m, plot 9×12 m, `worker_beds = 2`.
The registry validates `1 <= worker_beds <= 32`.

## Data files

### `Sim/icarus_sim/castles.json` — the castle registry

Loaded once, `lru_cache(maxsize=1)`, at the module's own path (`Path(__file__).with_name('castles.json')`).
Validated on load: `schema == 'fantasy-world-generator.castle-registry'`, `schema_version == 1`,
`revision` an `int >= 1`. Anything else raises.

Top-level keys: `schema`, `schema_version`, `revision`, `description`, `unit` (`"metres"`),
`join_rules`, `modules`, `kits`.

`modules` is a **dict keyed by module key** (`curtain_segment`, `corner_join`, `gatehouse`,
`postern`, `wall_stair`, `interval_tower`, `drawbridge`, `bailey_court`, `keep_tower`). One
representative entry:

    "gatehouse": {
      "id": "module.gatehouse",
      "structure_id": "building.barbican",
      "role": "node",
      "end_type": "gate_join",
      "thickness_m": {"min": 2.5, "max": 4.0, "nominal": 3.0},
      "height_m":    {"min": 10.0, "max": 16.0, "nominal": 14.0},
      "walkway": true,
      "compatible_ends": ["curtain_end"]
    }

`curtain_segment` is the only module with a wide `compatible_ends`
(`["curtain_end","corner_join","gate_join","tower_join","stair_join"]`); every node module
lists only `["curtain_end"]`. `bailey_court` and `keep_tower` have `end_type: null` and
`thickness_m: null` and are never passed through `can_join`. `interval_tower` is defined and
**never selected by any code path** — dead data.

Selection: the planner reaches modules by the literal keys `'curtain_segment'`,
`'corner_join'`, `'postern'`, `'wall_stair'`, and by `ring_spec['gate'] or 'gatehouse'`.

`kits` is a **list** (order matters for the weighted draw). Each kit: `id`, `name`, `weight`
(int), `half_m`, `rings[]`, `courts[]`, `landmarks[]`, `services[]`. Full contents are listed
under Constants. `landmarks[].offset_scale` exists in the file and is never read.

### `Sim/icarus_sim/buildings.json` — the neutral building registry (schema_version 2)

Loaded through `civilization_registry._document()` alongside `civilizations.json`; the four
sections `building_packs`, `layout_profiles`, `structure_blocks`, `housing_profiles` are
spliced into the resolved civilization document. Libraries in `structure_blocks`: `common`,
`rural`, `castle`.

`castle_structures()` flattens `structure_blocks['castle']` into `{structure.id: structure}`
across both of its blocks (`castle_fortification`, `castle_services`). A representative entry:

    {"id": "building.barbican", "name": "Barbican gatehouse", "priority": "core",
     "module_family": "fortification", "geometry_type": "building",
     "dimensions_m": {"width": 14, "depth": 16, "height": 14},
     "clearance_m":  {"front": 8, "rear": 4, "left": 2, "right": 2},
     "plot_m":       {"width": 18, "depth": 28},
     "staffing": {"basis":"distinct_people_all_shifts", "housing_location":"city",
                  "roles":[{"role":"gate_captain","minimum":1,"target":1,"maximum":2},
                           {"role":"gate_guard","minimum":2,"target":4,"maximum":6}],
                  "notes":"Gate roster; sleeping deferred."}}

The registry validator enforces `plot.width == dimensions.width + clearance.left + clearance.right`
and the same for depth, so plots are footprints plus measured clearances, never invented.

Castle structures and their `sum(role.target)` worker counts — the whole table, because
`stats.workers` is the garrison `npc_roster` opens posts against:

| structure_id | plot w×d | dims w×d×h | workers |
|---|---|---|---|
| `building.curtain_segment` | 7×10 | 3×10×8 | 0 |
| `building.corner_tower` | 12×12 | 8×8×14 | 2 |
| `building.wall_stair` | 6×9 | 4×6×8 | 0 |
| `building.postern` | 10×14 | 6×8×8 | 2 |
| `building.barbican` | 18×28 | 14×16×14 | 5 |
| `building.drawbridge` | 10×18 | 6×14×2 | 0 |
| `building.bailey_court` | 36×44 | 36×44×0 | 0 |
| `building.keep_tower` | 24×24 | 16×16×24 | 8 |
| `building.great_hall` | 16×31 | 12×24×10 | 3 |
| `building.castle_chapel` | 12×17 | 8×12×9 | 1 |
| `building.castle_barracks` | 16×25 | 12×18×7 | 10 |
| `building.castle_armory` | 12×18 | 8×12×7 | 1 |
| `building.castle_well` | 7×7 | 3×3×3 | 0 |

`structure_blocks['rural']` — the hamlet library, whole table:

| structure_id | priority | plot w×d | dims w×d×h | workers |
|---|---|---|---|---|
| `building.hamlet_well` | core | 4×6 | 2×2×2 | 1 |
| `building.hamlet_latrine` | core | 5×7 | 3×3×2 | 1 |
| `building.hamlet_food_store` | core | 6×11 | 4×6×3 | 1 |
| `building.hamlet_barn` | core | 10×14 | 6×8×5 | 2 |
| `building.hamlet_farmyard` | core | 14×18 | 10×12×4 | 3 |
| `building.hamlet_staging_yard` | core | 10×11 | 6×6×3 | 2 |
| `building.hamlet_shrine` | conditional | 6×9 | 4×4×4 | 1 |
| `building.hamlet_meeting_shed` | conditional | 8×13 | 6×8×4 | 1 |
| `building.hamlet_cart_shed` | conditional | 8×13 | 6×8×3 | 1 |
| `building.hamlet_smith_shed` | conditional | 7×11 | 5×6×4 | 1 |
| `building.hamlet_landing` | conditional | 8×14 | 4×8×2 | 2 |
| `building.hamlet_net_shed` | conditional | 6×11 | 4×6×3 | 2 |
| `building.hamlet_track` | core | 5×8 | 3×8×0 | 0 (infrastructure only; never placed) |

`housing_profiles.hamlet_house`: id `building.hamlet_house`, name "Hamlet cottage",
dims 5×6×4, plot 9×12, `worker_beds` 2.

### `Sim/icarus_sim/civilizations.json` — the civilization registry (schema_version 6)

`hamlet_plan(entity_id)` returns `entity['hamlet']` with each `buildings[]` and
`infrastructure[]` row merged structure-first: `{**structure, **row}`. **Order is the file's
order and it is load-bearing** — it is the placement order for the high/low passes and the
order of `unplaced` entries.

Every one of the twelve shipped civilizations has an **identical** hamlet programme (verified
by signature comparison), so the entity id selects nothing that the planner can see:

    hamlet_well, hamlet_latrine, hamlet_food_store            — count 1, no conditions
    hamlet_barn        — count 1, conditions ["farming_support"]
    hamlet_farmyard    — count 1, conditions ["farming_support"]
    hamlet_staging_yard— count 1, conditions ["resource_support"]
    hamlet_shrine, hamlet_meeting_shed, hamlet_cart_shed,
    hamlet_smith_shed  — count 1, no conditions
    hamlet_landing     — count 1, conditions ["navigable_shore"]
    hamlet_net_shed    — count 1, conditions ["navigable_shore"]
    infrastructure: hamlet_track, quantity_mode "fit_route_or_perimeter"  — NEVER PLACED

`plan['buildings'][i]['priority']` comes from the structure, not the block row.
`row['count']` is always 1 in the shipped data but the code loops `for _ in range(count)`.
`plan['infrastructure']` is resolved and **never read by the planner**.

**Do not move any of this into C++ source.** A byte changed in `civilizations.json` changes
`registry_identity().sha256` and invalidates every saved world at the age gate; a string key
in a profile throws in C++. The C++ side must keep these as data files with the same identity
hashes, or accept that saved-world compatibility is not a goal of the rewrite and say so.

## Random streams

Four seeded values, all deterministic in the world seed plus a pin id.

### 1. The hamlet street/shape seed (one per hamlet)

    seed = big-endian uint64 of the FIRST EIGHT BYTES of
           sha256( f"{world.config.seed}:{plan.hamlet_id}:hamlet-streets-v1" ).digest()

Used three ways: as the argument to the (absorbed) ellipse sine/cosine; as `seed % 1000` in
the anchor-angle jitter; and as the seed of `grow_roads`' `random.Random`.

Note the hash input is the plan's `hamlet_id`, which is the pin's ORDINAL id
(`'hamlet-7'` / `'coastal-3-1'`). Ordinals renumber on an age advance, so a hamlet's street
layout changes across an age boundary even when its terrain node did not move.

### 2. `grow_roads`' `random.Random(seed)` — draw count is DATA-DEPENDENT

In order:
 - 3 × `uniform(-pi, pi)` for the cost-field phases. Fixed.
 - 1 × `sample(reachable, k)` where `k = min(len(reachable), max(256, count*20))` and
   `count = max(2, min(36, round(len(valid)/144 * 1.5)))`. `k` therefore depends on how much
   of the ellipse is buildable, and CPython's `random.sample` picks between two algorithms
   (a selection set vs a shuffled pool) by population size, consuming different amounts of
   stream. `fwg::Rng` already calls this out and `sample_indices` uses one algorithm at every
   size — use it and do not attempt to reproduce CPython's branch.
 - Nothing else. The farthest-point loop and the backtrace are deterministic.

Typical magnitude: for a 200 m hamlet window with ~1600 buildable cells, `count = 17`,
`k = min(|reachable|, 340)` → roughly 350 draws per hamlet.

### 3. The castle kit draw (one per fortress)

    rng = random.Random( sha256(f'{world.config.seed}:castle-kit:{fortress.id}').hexdigest() )
    kit = rng.choices(kits, weights=[max(1,int(k.weight)) for k in kits], k=1)[0]

**This is the odd seeding path.** The seed is a 64-character lowercase hex STRING, not an
integer. CPython's `random.seed(str)` does
`a = str.encode('utf-8'); a = int.from_bytes(a + sha512(a).digest(), 'big')` and then
`init_by_array` on the resulting ~1024-bit integer. That is reachable but pointless to
reproduce in a clean rewrite. Exactly **one** `random()` draw is consumed.

**What it must become in C++:** one named child stream and one uniform, e.g.

    Rng rng = Rng::child(config.seed, "castle-kit", <fortress key>);
    const double u = rng.random() * total_weight;      // total = 9 with the shipped kits
    // first index whose running weight sum EXCEEDS u, clamped to the last kit

`Rng::child` already derives its seed as the first eight bytes of
`sha256("<namespace>:<master>:<domain>:<variation>")`, which is structurally the same
construction the hamlet seed uses — reuse it rather than inventing a fourth derivation.
`variation` is an `int64`, so the string fortress id has to be reduced to an integer; the
natural choice is the terrain node (stable across ages), the faithful choice is the ordinal
(unstable, matches Python). See Open questions.

### 4. Nothing else

`plan_castle` draws no further randomness at all after kit selection: the rings, the gate, the
towers, the stairs, the courts, the landmark and the whole service fan are pure geometry.
`plan_hamlet` draws none outside `grow_roads`. Both planners are pure functions of
`(world, pin)`.

## Ordering hazards

Every place iteration order, sort stability or container ordering reaches output.

**1. `fill_hamlets` / `fill_castles` pin order — `sorted(pins, key=lambda p: str(p['id']))`.**
Lexicographic on the STRING, so `'coastal-0-0' < 'hamlet-0' < 'hamlet-10' < 'hamlet-2'`. This
is the order of the output lists and (for hamlets) the pool's index space. The pooled worker
re-derives the same sorted list from its own copy of the world, so the two must agree exactly.

**2. The neighbour-crop loop builds `sites` in a fixed order** (all `settlements.sites` first,
then the other hamlets/fortresses in `humans` list order). `half` is `min`-folded, so the
final value is order-independent in exact arithmetic but not in floating point when several
neighbours are near-equidistant. Keep the order.

**3. `valid` is a Python `set` of `(i, j)` tuples.** Every place it is iterated for output is
explicitly sorted or is order-independent:
 - `grow_roads`: `sorted(valid)` for the `heights` and `costs` dicts; `min(valid, key=…)` for
   the root with the `(i, j)` tie-break baked into the key.
 - `sorted(road)` for `result['roads']`.
 - `sorted(footprint)` in the hamlet's ground-probe list (city does not sort; both only take
   max/min, so it changes nothing numerically — but it changes the cost).
 - `len(valid - road - occupied - access)` etc. — cardinality only.
**But `_stream`'s `footprint <= valid` and `footprint & road` are set operations whose result
is order-independent. No unsorted set iteration reaches output here.**

**4. `anchors = sorted(set(anchors), key=lambda p: (p[0]**2 + p[1]**2, p))`.** The `set()`
deduplicates identical `(x, z, angle)` triples; the sort key is (squared distance from the
local origin, then the tuple itself). This IS the placement priority — nearest the pin gets
built first. Tuple comparison falls through to `x`, then `z`, then `angle` as floats.

**5. `grow_roads`' Dijkstra neighbour order** `(-1,0),(0,-1),(0,1),(1,0),(-1,-1),(-1,1),(1,-1),(1,1)`
is fixed and matters on exact-tie relaxations. The heap is keyed `(cost, cell)`, so ties in
cost are broken by the cell tuple — a Python tuple comparison the C++ must reproduce with a
`(double, int, int)` comparator, not with an unspecified heap ordering.

**6. `grow_roads`' farthest-point loop** uses `max(candidates, key=…)`; Python's `max` returns
the FIRST maximal element, and the key's second component is the cell tuple, so ties are
resolved deterministically. `candidates.remove(target)` removes the first equal element.
`targets` grows inside the loop and the min-distance is recomputed against `[root] + targets`
each pick.

**7. `targets = sorted(set(required) & set(distances))`** — the gate cells are sorted before
the farthest picks are appended, so gates are always traced first.

**8. `paths` order** determines `street_paths_m` order, which determines `anchors` order
before the sort, which determines the dedup survivor for identical triples.

**9. `approach = next((path for path in paths if path[-1] == cell), [cell])`** — the FIRST
path ending at that cell. Two branches can end at the same cell only if the tree is malformed,
but the `next()` is still first-wins.

**10. `_Replay` preserves generator order exactly** and `install` scans from the beginning
every time. Two buildings with the same `(w, d)` share one memo, so the second one re-tests
and skips the first's option. Changing the cache key or the scan start moves plots.

**11. `candidate_cache` and `corridor_cache` are dicts keyed by float tuples.** They are pure
memoisation — the comment insists on that: *"The key is the endpoints as computed and not a
tidied version of them, so this can only ever return what the eleven rasterizations below
would have produced."* A C++ `std::map<std::tuple<double,double,double,double>, …>` is fine;
a hash of rounded coordinates is not.

**12. `result['passes'][next(i for i,p in enumerate(passes) if p['id'] == phase)]`** — first
match. Phase ids are unique in both planners.

**13. `preset['buildings']` order** is `civilizations.json`'s file order, preserved by
`json.load(object_pairs_hook=_unique_object)` and by `copy.deepcopy`. It is the placement
order within a priority tier and the order of `unplaced` rows.

**14. `high` / `low` split preserves preset order within each tier**, and the tiers are
processed `('high', high), ('low', low)` in that order.

**15. `registry_identity()` hashes `json.dumps(doc, ensure_ascii=False, separators=(',',':'))`
with NO `sort_keys`.** Python dict insertion order — which is the file's key order, then the
order `_resolve` splices in the four building sections, then `data['entities']` rebuilt in
`civilization_order` order — is part of the hash. `castle_planner`'s two hashes DO use
`sort_keys=True`, so they are order-free.

**16. `castle_structures()` flattens `structure_blocks['castle']` in block order then
structure order** into a dict. Only used for lookup, so order is inert — but the
`buildings_hash` is `sort_keys=True`, so it is order-free too.

**17. `ring_polylines = {n['role']: n['polyline_m'] for n in networks if closed}`** — last
writer wins on a duplicate role. No shipped kit has duplicate roles.

**18. `corner_indices` returns ascending indices**, and the tower loop consumes them in that
order with the budget counted over CANDIDATES, not placements. Changing the vertex numbering
origin (currently angle 0, i.e. +x) moves every tower.

**19. `keep[::step][:stair_budget]`** — a strided slice of the gate-filtered segment list.
The stride is over `keep`, after the gate filter, not over the raw segments.

**20. `networks[0]` is the "primary" ring** — the first entry of `kit['rings']` in file order,
not the largest and not the outermost by construction.

**21. The gate dedup in C9** scans `result['plots']` in insertion order but only tests
existence, so it is order-independent.

**22. `sorted(strategic.items(), key=lambda item: (-item[1][0], item[0]))` upstream** in
`terrain_humans` is what assigns fortress ordinals; it is outside this spec but it is why
fortress ids renumber.

**23. `world_scene.build_scene` is called twice** (once by `fill_hamlets`, once by
`fill_castles`), each time rebuilding the whole scene from scratch. Only the second result
survives. If a port runs them in a different order or skips one, the scene loses the other's
entries.

## Edge cases

**Hamlet**

- `access_nodes` shorter than 2 entries, or `world['water']['nodes']` empty → no
  `road_connections` at all. `grow_roads` gets an empty `required` list and grows purely from
  the farthest-point seeding.
- An `access_nodes` index outside `range(len(nodes))` → `break`, no entry.
- `den <= 0` on any node along the path (the node is below the local horizon) → `break`,
  no entry.
- The whole access path staying inside the window → the loop exits normally with no entry.
- `q[j] - prev[j] == 0` while `|q[j]| > half` → division by zero. Reachable only if the path
  first leaves the window exactly along the other axis; not guarded. CPython raises
  `ZeroDivisionError` and the whole world generation fails. In C++ this is a silent inf.
  Guard it, and say in a comment that the reference did not.
- Fewer than 8 terrain-valid cells → early return, `status='unbuildable'`, `plots=[]`,
  `stats` has only five keys, `street_paths_m` and the six extra `debug` keys are absent.
- Fewer than 8 cells after the ellipse clip → the same early return, but `passes['map']` and
  `passes['shape']` have already been set to `len(valid)` and 1.
- `half` collapsing to 4 (two pins essentially on top of each other) → `size = 2`, at most 4
  cells, so the `< 8` early return fires.
- A hamlet with no `role` key or `role=''` → every role condition false; barn, farmyard,
  staging yard, landing and net shed all go to `unplaced`; three of those five are `core`, so
  `unplaced_core = 3` and status is `'partial'`.
- `install` on an exhausted candidate stream returns False; the caller records
  `'No terrain-safe plot with road access remaining'` with the failed count.
- `house_workers` with zero workers never loops and records an audit with
  `houses_placed = 0, shortfall = 0`.
- `house_workers` when the stream is exhausted sets `plots_exhausted = True` and breaks; the
  shortfall survives into `stats.housing_shortfall` and forces `status='partial'`.
- The low tier is skipped entirely (all its rows go to `unplaced` with
  `'High-priority worker housing must be completed first'`) when any plot placed so far has
  `workers > beds` — note this is a per-plot sum, so a single unhoused service building blocks
  the whole low tier. `house_workers('high_housing')` runs first, so this fires only when the
  high housing pass ran out of frontage.
- Pole rows: a pin at `z = 0` or `z = n-1` gives `lat = ±pi/2`, `east` and `north` degenerate
  on the longitude, and the gnomonic frame still works but the window covers a wedge. Nothing
  guards it. `direction(x, 0, n)` returns the exact pole `(0,1,0)`.
- Seam: the neighbour-crop `dl = 2*pi*(other.x - hamlet.x)/(n-1)` does not wrap, so a pair
  straddling column 0 / column n-1 measures the long way round and does not crop.
- `sample`'s `gx` uses `round(...) % (n-1)`, which wraps correctly; `gz` is clamped.
- Python's `round()` is banker's rounding (half-to-even). It appears in `gx`/`gz` and in
  `grow_roads`' branch count. `std::round` is half-away-from-zero — a real divergence on
  exact halves.
- `int(half/CELL)*CELL` truncates toward zero. `half` is never negative here because of the
  `max(CELL, ...)`.

**Castle**

- Fewer than 24 valid cells → early return with one `unplaced` row and `status='unbuildable'`;
  `stats` is `{}` and `wall_networks` is `[]`.
- A ray in `clip_ring_to_valid` finding no buildable cell contributes NO point, so the
  polyline gets shorter than 32. Below 8 points, `smooth_closed_ring` is skipped and
  `build_wall_network` immediately reports `'Perimeter could not close'`.
- `clip_ring_to_valid` returning fewer than 8 points → the planner substitutes the UNCLIPPED
  raw ellipse, which may lie entirely on water. Nothing re-checks it.
- `hypot(px, pz) or 1.0` guards a ring point at the exact origin (only possible if
  `rx*scale == rz*scale == 0`).
- No ring closing → `status='partial'`, short `stats`, `plots=[]`, and `debug` stays `{}`
  because it is assigned only at the end.
- A closed ring whose role is not named by any court/landmark → nothing is placed there and
  nothing is reported.
- A court or landmark whose `ring_role` did not close → an `unplaced` row naming the court's
  own `structure_id`.
- `primary = networks[0]` open → the entire services block is skipped silently, with no
  `unplaced` rows for any of them.
- Drawbridge without a ditch or without gates → `'Drawbridge requires outer ditch and gate'`.
  Note `primary.get('ditch')` only exists on a CLOSED network, so an open primary also fails
  this (but the whole block is skipped anyway).
- Barbican in a kit's services while the primary gate is already a barbican → silently
  skipped, not reported as unplaced, and `service_i` does not advance.
- An unknown `structure_id` in a kit's services → `'Unknown castle structure'`.
- `install` never validates anything, so plots overlap each other, sit outside `bounds_m` and
  may sit on water. Measured: two `building.bailey_court` plots at the identical centre, a
  keep on top of them, and a drawbridge at `x_m = -98` inside a `half = 88` window.
- `segmentize` with `edge == 0` emits one zero-length segment and terminates.
- `stair_budget` larger than `len(keep)` → `step = max(1, …) = 1` and the slice yields
  `len(keep)` stairs, fewer than the budget. No report.
- `tower_budget` of 0 → the loop breaks on the first candidate; no towers, no report.
- `approach is None` (no boundary crossing) → `approach_gate_index` uses the +x extremity,
  `angle0 = 0.0`, and the gate's `approach_m` is set to its own position.
- A kit with zero rings would make `networks` empty and `any(...)` false → the "no ring closed"
  early return. Not reachable with the shipped data.

**Both**

- `world['layers']` missing any optional layer → the documented default is used, silently.
  `layers['height']` missing raises a `KeyError` inside `HeightField`.
- `terrain_detail` absent → heights are bare bilinear samples of the coarse grid and the
  per-sample slope derivative is skipped entirely, so the cell mask falls back to the coarse
  `slope` layer. Plans built this way are much flatter and much more permissive.
- `terrain_detail['version'] != 1` → `ValueError('Unsupported terrain detail version')`.
- `resolution > half` (a coarse world where one raster cell is wider than the whole window) —
  the hamlet appends an extra warning; the castle does not, though the same condition holds.

## Reasoning worth preserving

Quoted verbatim, because these are measurements and failed approaches that exist nowhere else.

**On the regional flood proxy — the single most expensive lesson in these files.**

`hamlet_planner`, at the cell mask: *"Coarse flood_risk is a regional layer, not a local
inundation mask; coasts would otherwise be empty."*

`castle_planner`, at the cell mask: *"See city_planner: the regional flood proxy cannot block
a cell it cannot resolve. 34 of 56 castles were unbuildable on this rule."*

`city_planner`, the full version both refer to: *"Standing water, real steepness and the
reserved river channel block a cell. The regional flood proxy does not, for the reason
hamlet_planner already records: it is a coarse layer, not a local inundation mask. It is
emitted as 0 or 1 and a city is smaller than one of its cells, so honouring it here
disqualified whole cities outright — 23 of 35 in a seed-42 size-33 world, every one of them on
a cell reading exactly 1.0, on ground under 21 degrees."*

Both planners' samplers: *"`channel` is the local reserved river channel and setback;
`flood_risk` is the coarse regional proxy, constant across a footprint and therefore unable to
say anything about one cell within it."* And the city's longer form: *"Two different things
used to share one `flood` key, and a caller could not tell which it had read."*

`docs/hamlet-planner.md` on version 3's remaining half of that bug: *"The cell mask had
excluded the regional proxy since version 1, but the gate deciding whether a plot reaches a
street still required `flood <= 0.65` from a sampler field that meant the regional layer
whenever no river was near, so on a flood_risk=1 cell the mask admitted the ground and the gate
then refused every plot on it."*

**On the neighbour crop.** `docs/hamlet-planner.md`: *"Version 1 used the city 0.28 distance
crop, which left clustered rural sites too small for cottages."* That is why the hamlet uses
`distance*0.5 - 8` while the castle still uses `distance*0.28`.

**On castle beds — carry the whole paragraph, it is a design ruling.** In `install`:

> No `beds`. No structure in the castle registry carries a bed count, so a number here would
> be invented rather than measured. The field used to be a literal 0 on every plot beside
> 35-53 placed `workers`, which reads as a garrison sleeping nowhere and divides into a
> runtime error for anything computing occupancy. Absence is representable; a zero that was
> never measured is not.

And the consumer-facing restatement in `fill_castles`:

> A castle plan declares no sleeping capacity. No structure in the castle registry carries a
> bed count, so castle plots carry no beds key at all rather than a zero that was never
> measured. The garrison a plan staffs is stats.workers, which npc_roster opens one post per.

**On the candidate stream.** `_Replay`'s docstring: *"`candidates` used to build the whole
anchor sweep for every plot size, but `install` takes the first free option and stops: between
22% and 61% of each list was built and never looked at. Yielding on demand and keeping what was
yielded leaves the order and the first match identical — no plot moves — and simply stops
producing options past the deepest one anybody asked for."*

And at the call site: *"Yielded rather than listed, for the reason `_Replay` records: install
takes the first free option and stops, so the tail of every list was built and never looked at."*

**On the clearance skip.** `_clearance`'s docstring: *"Two sweeps over the city raster, once
per city, so that the candidate sweep can decide most positions without rasterizing anything.
Roughly four candidate positions in five are rejected because the plot overlaps blocked ground
or a street, and each of those used to cost a footprint rasterization and two set tests."*
And the correctness argument at the call site: *"The nearest cell that is not free ground has
its centre within (k+1)*CELL of this plot centre in Chebyshev terms, so within
(k+1)*CELL*sqrt(2) Euclidean. When that lands inside the inscribed circle it is inside the
rectangle and therefore inside the footprint, so the two tests below are already decided and
the rasterization is skipped. It can never fire on a candidate they would have accepted."*

**On the corridor cache.** *"An access corridor depends only on its endpoints, so building it
once per distinct pair is the same set. The key is the endpoints as computed and not a tidied
version of them, so this can only ever return what the eleven rasterizations below would have
produced."*

**On `footprint_cells`.** *"Hot path: called roughly a million times per world. Every
expression below is the original one, only hoisted out of the loops it did not depend on, so
the floating-point results are bit-for-bit unchanged."*

**On the road cost field.** *"A smooth travel-cost field produces persistent bends, rather
than white-noise zigzags."* And on the targets: *"Destinations represent gates and growing
districts, never an imposed street grid."* And on widening: *"Diagonal centerline steps have
orthogonal safe support cells (no corner cutting)."*

**On the hamlet pool.** *"plan_hamlet is a pure function of (world, hamlet) and, unlike a city,
carries no cross-hamlet state at all — no shape counter, nothing a later hamlet reads from an
earlier one. So the pool needs one pass, not the city planner's two."* And the city's rationale
for the noisy fallback, which the hamlet's warning text copies: *"A world must still generate
if the pool cannot start, so this falls back rather than failing — but a silent fallback is
indistinguishable from a planner that is simply slow, which is exactly the thing nobody would
investigate. Say it."* The warning itself: *"A spawn platform re-imports the caller's
`__main__`, so generation must happen under `if __name__ == "__main__"` for the pool to start."*

**On the hamlet's independence from city shapes.** *"Seeded irregular ellipse keeps city shape
catalogues independent."* And *"Rural streets stay inside the ellipse with a low branch floor;
city 12-district growth fills a 100 m window."*

**On `smooth_closed_ring`.** *"Pull in needle vertices so an enceinte cannot form one giant
outward point."*

**On the wall-network fallbacks.** *"Fall back to unclipped ellipse when terrain clipping
collapses the ring."* / *"Drop curtain segments that collide with gate centres."* /
*"Already represented as the gate node."* / *"Emit gate/tower/stair plots for consumers that
only read plots[]."* / *"Node structures may rise above the walkway; join continuity uses
curtain height."* / *"Prefer the +x extremity as a deterministic fallback approach."*

**On the duplicated sampler.** *"Local east/north metre sampler; duplicated to avoid importing
city_planner."* That is a deliberate coupling refusal, echoed by `docs/castle-planner.md`'s
independence list: *"No imports from city_planner.py."*

**On the id space — from `terrain_humans.record`, the writer of the pins these planners join
on.** *"`id` is the ORDINAL, kept because `hamlet_plans` and `castle_plans` join on it. `uid`
beside it is the handle that survives an age boundary: this list is rebuilt and re-sorted by
defence score every age, so `fortress-36` names different ground after an advance while the
terrain node does not move."* And from `terrain_settlement_api`: *"Rebuilding humans renumbers
hamlet and fortress ordinals, and hamlet_plans and castle_plans key on exactly those ordinals
with no uid beside them."*

**On `core_id` — from `docs/conformance/read-surface.md`.** *"`core_id` on a hamlet or fortress
is a **position** in `settlements.sites`, not a uid. The small-site builder stores `owner[i]`
and the planners dereference it by index."*

**On `hamlet_id` — same document.** *"`hamlet_plans.hamlets[].hamlet_id` is the hamlet's own id
and **not** `hamlet-<node>`: `humans.hamlets[].id` is `hamlet-0` for inland sites and
`coastal-1-0` for coastal ones, and the plan carries whichever the hamlet had. Matching by node
would agree on a size-17 world — both lists are one to one over nodes — and would be a
different question."*

**The four hamlet warnings, verbatim** (they are consumer-facing output, not comments):

    Schematic rural packing; groundwater and structural feasibility are not resolved.
    Hamlet cottages use dedicated rural housing IDs, not city worker houses.
    Compact elliptical boundary is provisional; historical village morphology is future work.
    Regional flood_risk does not empty hamlet plots; standing water and slopes above 25 degrees still block cells.

plus, conditionally: *"Regional land categories repeat coarse samples; canonical local relief
supplies finer elevation detail."*

**The two castle warnings, verbatim:**

    Schematic fortification modules; structural engineering and garrison beds are not resolved.
    Curtain segments are metre-scale wall runs; production ARCH meshes remain unassigned.

## Notes for the C++ implementation

**What already exists at `FWGC++` and maps straight across**

- `fwg::Grid` — `layers.slope`, `water_type`, `river`, `flood_risk`, `moisture`,
  `natural_biome`, `biome_variant`, `height`. The seam duplication (column n−1 == column 0)
  and the pole rows matter here: the samplers index `[gz][gx]` with `gx = … % (n-1)`, so they
  never read column n−1, and `gz` is clamped into `[0, n-1]`, so they do read pole rows. Both
  are consistent with the Grid contract; do not "fix" the modulo.
- `fwg::direction(x, z, n)` is exactly `terrain_globe.direction` including the two pole
  special cases. `support_road_entries` / `support_approach` call it on node grid coordinates.
- `fwg::sample(Grid, Vec3)` is the bilinear sampler `HeightField` degrades to when there is no
  `terrain_detail`.
- `fwg::Rng::child(master, domain, variation)` and `child_seed64` are the right home for both
  seeded values; `child_seed64` is literally "first eight bytes of
  sha256(namespace:master:domain:variation)", which is the same construction the hamlet seed
  uses (`sha256("{seed}:{id}:hamlet-streets-v1")`, first eight bytes, big-endian). Use it;
  the header is explicit that a fourth derivation must not be invented.
- `fwg::Rng::sample_indices` replaces `random.sample`. The header already explains why the
  reference's two-algorithm `sample` is a hazard. One algorithm, one stream cost.
- The library's own SHA-256 (`fwg/sha256.hpp`) and JSON writer (`fwg/json.hpp`) cover both
  `planner_identity` implementations.

**What does NOT exist and has to come first**

- `HeightField` (the continuous-relief height function, spherical Perlin bands, the coast /
  strength / wet masks). It is the single most-called thing in both planners — a hamlet
  evaluates it `(size+1)² + 4·candidates` times and a castle `(size+1)²` times plus four per
  plot. It belongs in the terrain-detail port, not here.
- `sphere_grid`'s node graph is NOT what these modules use. They index
  `world.water.nodes[i] -> (x, z)` directly and project with the gnomonic frame. The only
  graph relationship they need is `access_nodes`, which the settlement pass already resolved.
- `fwg::World` needs typed blocks for `settlements.sites`, `humans.hamlets`,
  `humans.fortresses`, and for `hamlet_plans` / `castle_plans` / `world_scene`.

**Python idioms with no direct equivalent**

| Python | C++ |
|---|---|
| `set` of `(i, j)` cell tuples, with `<=`, `&`, `-`, `|` | The rasters are dense and small (`size ≤ 50`). A `std::vector<uint8_t>` bitset indexed `j*size + i` beats any node-based set, and subset / intersection become word loops. **But `footprint_cells` can emit indices outside `[0, size)`** — the Python relies on `footprint <= valid` to reject those. Bound-check before indexing. |
| `dict` insertion order in `registry_identity`'s unsorted `json.dumps` | An ordered map or a vector of pairs, if saved-world hash compatibility is wanted at all. |
| `heapq` with `(cost, (i,j))` tuple keys | `std::priority_queue` with an explicit comparator that compares cost first, then i, then j. `std::greater` on a `std::tuple<double,int,int>` gives exactly Python's ordering. |
| generator + `_Replay` | A vector memo plus a lazily-advanced coroutine, or simply a `std::function` state machine. A plain "build the whole list" is CORRECT (same order, same first match) and only costs the work `_Replay` was written to avoid — take it if the profile allows. |
| `sorted(set(anchors), key=…)` | `std::sort` + `std::unique` on the triple, then a stable sort by `(x²+z², x, z, angle)`. Python's `sorted` is stable, but the key is total here, so stability is not load-bearing. |
| `max(iterable, key=…)` / `min(…, key=…)` | **First** maximal / minimal element. `std::max_element` returns the first maximum and `std::min_element` the first minimum — matching. `std::max_element` with a `<` comparator is correct; do not use `>=`. |
| `round(x, n)` | Python rounds half-to-even in binary; `std::round` is half-away-from-zero. Every `round(x, 2)` / `round(x, 4)` in the output feeds a float that is later compared (`dist(...) < 1`, `footprint <= valid`), so the divergence is observable. Since numbers need not match, pick one convention and state it, but pick it ONCE. |
| `round(x)` (no digits) in `gx`/`gz` and `grow_roads`' branch count | Same hazard, and here it is an INDEX. Half-to-even vs half-away changes which raster cell a sample reads. |
| `int(x)` truncation toward zero | `static_cast<int>` matches. `//` floor-division on positives matches, on negatives does NOT — `int((x+half)//CELL)` in the clearance skip can see a negative `x+half`. Use `std::floor` explicitly. |
| `math.floor` returning a float that is then `int()`d | `static_cast<int>(std::floor(...))`. |
| `math.dist`, `math.hypot` | CPython's `hypot` is a compensated multi-term algorithm; `std::hypot` is not the same. Clean rewrite, so it does not matter — but use one consistently, because `length_m` and the gate-collision radii both depend on it. |
| `sum(...)` over floats | CPython's builtin `sum` is a plain left fold (only `math.fsum` compensates), so a left fold matches. |
| `os.environ.get('ICARUS_CITY_WORKERS')` + `ProcessPoolExecutor` | Drop the process pool entirely. `plan_hamlet` is a pure function of `(world, pin)` and `std::for_each(std::execution::par, …)` or a simple thread pool over the sorted pin list gives the same results with no pickling and no spawn-platform hazard. **Preserve the sorted order of the output vector**, which the Python does by indexing. |
| `warnings.warn(..., RuntimeWarning)` | There is no pool to fall back from; drop the warning, but keep the `ICARUS_CITY_WORKERS` name available as a thread-count knob if you want the old operator muscle memory to work. |

**Numeric traps specific to this pair**

1. **The ellipse wobble absorption.** `sin(x*0.07 + seed)` with `seed ≈ 2×10^18` is
   `sin(seed)` for every x in the window, in double precision, in Python and in C++ alike. If
   you carry the formula literally you reproduce the Python (a plain ellipse). If you "clean
   it up" by reducing the seed first, you get a genuinely wobbly boundary and a different
   hamlet. Decide, and put the decision in a comment.
   Beware also that `std::sin` of a ~10^18 argument requires exact Payne–Hanek reduction;
   glibc and MSVC both do it, but do not assume the two agree bit-for-bit.

2. **The in-loop `angle` rebind.** In `_stream`:
   `for ax, az, angle in anchors: for side in (1,-1): … degrees = round(degrees(angle),4); angle = radians(degrees)`.
   The rebind happens AFTER `dx`/`dz` are computed for `side = 1` but BEFORE they are computed
   for `side = -1`, so the two sides use slightly different angles. A C++ `for (auto [ax,az,angle] : anchors)`
   with `angle` captured by value and left alone will place the `side = -1` plots at a
   different position. Write it as a mutable local that is deliberately updated.

3. **`half` is an integer after `max(4, int(half/4)*4)`** but a `double` before it. `size = 2*half//4`
   is integer division. `bounds_m` and the surface `step_m` both derive from it.

4. **`clearance[cj][ci]` is indexed `[z][x]`** while `valid` holds `(i, j) = (x, z)`. Both
   planners' rasters are `[z][x]` row-major in the output too (`terrain.codes[j][i]`).

5. **`castle_geometry::turn_degrees` measures the interior angle, not the exterior turn.**
   A porter who reads the docstring and implements the turn will get zero towers on every
   convex ring. Implement what the code does and put the discrepancy in the comment.

6. **`clip_ring_to_valid` makes `ring_spec.scale` inert.** Uniformly scaling an ellipse does
   not change any point's unit direction, and the function only uses the direction. A porter
   who "notices" that scale is unused and reintroduces it will build genuinely concentric
   castles — a visible, arguably better, but different world.

7. **`segmentize`'s `start = path[-1]`** produces ragged chord lengths (measured 1.17 m to
   10.0 m against a 10 m target). It looks like a bug; reproduce it or fix it deliberately,
   but do not "obviously" fix it while porting.

8. **The tower budget counts candidates, not placements**, so `tower_budget = 4` yields three
   towers whenever one vertex is within 10 m of a gate, and the emitted ids skip numbers.

9. **Castle plots have no collision test at all.** Do not add one. Two courts and a keep
   legitimately share a centre in the shipped data.

10. **`ZeroDivisionError` in the gate crossing** (`q[j] - prev[j] == 0` while `|q[j]| > half`)
    is unguarded in Python. In C++ it silently yields inf. Guard it and note the divergence.

**Suggested C++ shape**

    // fwg/planning.hpp — shared with the city planner
    struct LocalFrame { double radius; Vec3 up, east, north; };
    LocalFrame local_frame(const World&, int sx, int sz);
    struct TerrainProbe { bool water; double slope, height, moisture; int channel, flood_risk, biome, variant; };
    class LocalSampler { … TerrainProbe at(double x, double z, bool with_slope = true) const;
                         double height_at(double x, double z) const; };
    std::vector<uint8_t> chebyshev_clearance(const CellSet& free, int size);
    CellSet footprint_cells(double x, double z, double w, double d, double angle, int half, int cell = 4);
    std::array<Vec2,4> plot_corners(double x, double z, double w, double d, double angle);
    struct RoadTree { CellSet cells; std::vector<std::vector<Cell>> paths; };
    RoadTree grow_roads(const CellSet& valid, const HeightLookup&, int size,
                        uint64_t seed, double spacing_cells,
                        const std::vector<Cell>& required, int branch_floor);

    // fwg/hamlet_plan.hpp
    HamletPlan plan_hamlet(const World&, const HamletPin&);
    HamletPlans fill_hamlets(World&, int threads = 0);

    // fwg/castle_geometry.hpp   (pure, no World dependency — keep it that way)
    double turn_degrees(Vec2 a, Vec2 b, Vec2 c);
    JoinResult can_join(const Module& a, const Module& b, const JoinRules&, …);
    std::vector<Vec2> ellipse_ring(double rx, double rz, int samples = 48);
    std::vector<Vec2> clip_ring_to_valid(…);
    std::pair<std::vector<Segment>, std::vector<RingNode>> segmentize(…);
    WallNetwork build_wall_network(const BuildWallNetworkArgs&);   // named-arg struct; the
                                                                   // Python is keyword-only

    // fwg/castle_plan.hpp
    CastlePlan plan_castle(const World&, const FortressPin&);
    CastlePlans fill_castles(World&);

Keep `castle_geometry` free of `World` and `Config`, as the Python does — it is the only part
of this pair with real unit tests that do not need a world.

`castles.json` should stay a file (the identity hash is over its bytes). The hamlet programme
lives in `civilizations.json`, which the registry-identity rule already forbids inlining.

## Open questions

**1. What integer keys the two RNG streams.** `Rng::child`'s `variation` is an `int64`, and
both Python seeds hash a STRING id (`'hamlet-7'`, `'coastal-3-1'`, `'fortress-12'`). Those
ordinals renumber on every age advance, so the Python's street layout and kit choice change
for a settlement that did not move. Options: (a) hash the id string into the domain, which
reproduces the instability; (b) key on the terrain `node`, which is stable across ages and is
what every memory note about this codebase says to do. This is a behaviour decision, not a
porting detail, and it should be made by the owner. (b) is the better world; (a) is the
faithful port.

**2. The ellipse wobble.** `sin(x*0.07 + seed)` is provably constant over the window because
`seed` is a full 64-bit integer. The code's own comment calls it a "seeded irregular ellipse",
so the intent and the behaviour disagree. Reproduce the plain ellipse, or reduce the seed
(e.g. `seed % 1000`, as the anchor jitter already does) and get the irregular boundary that was
intended? The measured area fraction changes materially either way.

**3. `ring_spec.scale` is inert.** Because `clip_ring_to_valid` normalises the ray, a motte
ring at scale 0.38 and a bailey ring at scale 1.0 land on the same boundary, so
`kit.motte_bailey` and `kit.concentric` produce two coincident rings, two coincident courts
and a keep on top of them. Was concentric fortification intended and lost, or is this the
accepted schematic? Fixing it (clip each ray to `dist <= scale * half`, or snap along the
ellipse's own radius rather than the unit direction) changes every multi-ring castle.

**4. `segmentize`'s chord start.** Segments span `segment_depth - carry`, not
`segment_depth`, so on a fine polyline they are ragged (1.17 m … 10.0 m for a 10 m target).
Is that "cut at vertices" by design, or a defect? Nothing records an intent.

**5. `turn_degrees` measures the interior angle while its docstring says exterior turn.**
Consequently `min_turn_degrees_for_corner = 25` admits every vertex and its sense is inverted.
Was the threshold tuned against the actual behaviour (in which case leave it) or against the
documented one (in which case every castle's towers move)?

**6. The tower budget counts candidates, not placements.** `tower_budget = 4` reliably yields
three towers. Off-by-one or intentional "reserve a slot at the gate"?

**7. Hamlet `status` can never be `'complete'`.** Every role filter excludes at least one
`core`-priority building, so `unplaced_core >= 1` always and the status is always `'partial'`.
Either the role-filtered buildings should not be `core`, or `failed_core` should exclude
prerequisite failures, or `'partial'` is the honest answer forever. `docs/consumer-vocabulary.md`
gives `complete` a meaning that no hamlet can ever satisfy.

**8. Castle plots are never collision-tested, bounds-tested or terrain-tested.** Measured: a
drawbridge 10 m outside `bounds_m`, and two courts plus a keep at one point. Is the castle
deliberately a "schematic module diagram" (in which case say so in the block's `limits`, which
currently says only "not structural engineering or production art"), or should the port add
what the hamlet has?

**9. `hamlet.debug.housing_frontage_candidates` counts ALL candidates** (it forces the whole
`_Replay`), while the city's equivalent counts only FREE frontage. Two fields with the same
name meaning different things across two planners. Which one is the port's?

**10. Pooled vs serial hamlet output was never proved equal.** The construction argues it
(each worker re-sorts the same pinned list and indexes it), but no test compares
`ICARUS_CITY_WORKERS=1` output against pooled output. If the C++ parallelises, add that test —
it is cheap and it is the one thing nobody checked.

**11. `join_rules.max_turn_degrees` (135) and `modules.interval_tower` and
`landmarks[].offset_scale` are all in `castles.json` and never read.** Port them as data
(they are inside the identity hash) or drop them and accept that the hash changes?

**12. Does saved-world compatibility matter at all?** `registry_identity()` hashes the
resolved civilization document with `ensure_ascii=False` and no `sort_keys`, so Python dict
insertion order is inside the hash. If C++ worlds must load Python worlds, that ordering has
to be reproduced exactly; if not, the whole identity block can be redefined with sorted keys
and the age gate can be re-derived. Nobody has said which.

## Addenda from the audit

The reader above wrote the specification; a second reader went back to the source
to find what it had left out or got wrong. Both are kept: the omissions are
content the implementer needs, and the corrections say where the specification
above is not to be trusted.

### Corrections

- **Claimed:** Hamlet: "ground_elevation_m = max of the eight ground probes, foundation_bottom_m = min" and "plot flatness limit: hypot(w,d)*tan(radians(25)) metres of relief across the eight ground probes".
  **Actually:** There are not eight probes and the count is not fixed. ground = [sample.height_at(px,pz) for px,pz in corners(x,z,w,d,angle)] + [heights[c] for c in sorted(footprint)] — four rotated corner heights from the continuous field, plus ONE entry per raster cell in the footprint (the cell-centre heights already sampled for the mask). For the shipped plots the footprint is roughly 3x4 to 5x8 cells, so the list is typically 14-20 long and varies per candidate. Both the min/max written into the plot and the flatness rejection use that whole list. The spec's own ALGORITHM section H9 states the expression correctly; the Writes and Constants tables contradict it.

- **Claimed:** Castle step C5: "passes['perimeter'].placed = count of closed networks", with the Writes table implying that is the final value.
  **Actually:** result['passes'][2]['placed'] = sum(1 for n in networks if n['status']=='closed') is written at C5, but C9's install(..., 'perimeter', kind='gate'/'tower'/'stair') resolves the phase by id and does ['placed'] += 1 on that SAME index for every perimeter plot. The shipped value is closed_ring_count + number of gate/tower/stair plots. The closed-ring count only survives as the final value on the "no ring closed" early return, where it is 0 and C9 never runs.

- **Claimed:** clip_ring_to_valid: "the loop starts at dist = half, whose grid index is floor(2*half/cell) = size, always out of range — so the outermost radius ever achievable is half - cell."
  **Actually:** Only true for rays near the +/-x and +/-z axes. The index is floor((ux*dist + half)/cell), which equals size only when |ux| (or |uz|) is ~1. For a 45-degree ray with half=72, cell=4 (size=36): ux=uz=0.7071, dist=72 gives x=z=50.9 and i=j=floor(122.9/4)=30, comfortably in range. Off-axis rays routinely snap at dist = half, and the shipped rings are 32-sample ellipses, so most rays are off-axis.

- **Claimed:** turn_degrees: "Verified: on a regular convex 32-gon it returns +/-166.5 degrees, not +/-11.25."
  **Actually:** A regular convex 32-gon returns +/-168.75 degrees — the interior angle (n-2)*180/n. I measured -168.75 exactly. The ~166.9-169.5 spread is what ellipse_ring(96*0.72, 96*0.62, 32) produces (measured -166.93, -167.15, -167.54, -168.24, -168.83, -169.49 for the first six vertices), not a regular polygon. The substantive claims — that it measures the interior angle rather than the exterior turn, that the docstring is wrong, and that min_turn_degrees_for_corner=25 therefore admits all 32 vertices — are correct and I reproduced the 32-of-32 result.

- **Claimed:** castles.json modules: "curtain_segment is the only module with a wide compatible_ends ...; every node module lists only ['curtain_end']."
  **Actually:** False for drawbridge, which has role 'node', end_type 'gate_join', compatible_ends ['gate_join'] and walkway FALSE — the only join-capable module in the file with walkway false, and the only one that would trip can_join's walkway-continuity clause if it were ever passed through it. bailey_court and keep_tower have compatible_ends []. The modules that do list exactly ['curtain_end'] are corner_join, gatehouse, postern, wall_stair and interval_tower. The drawbridge MODULE is also dead data alongside interval_tower — the planner only reaches 'building.drawbridge' as a kit service structure_id, never modules['drawbridge'].

- **Claimed:** Reads section C (the fortress pin): "x, z, node, core_id, access_nodes — as above."
  **Actually:** castle_planner never reads fortress['node'], and the castle plan writes no node field, unlike the hamlet plan which copies 'node': hamlet['node'] verbatim. What the castle actually reads from the pin is id (required), x and z (required), core_id (via .get, may be None) and access_nodes (via .get).

- **Claimed:** "It also adds a `cell` key ([i,j] raster indices) to every entry, which survives into the output."
  **Actually:** The cell key is assigned at H7, after BOTH len(valid) < 8 early returns. On either early return (empty terrain, or empty after the ellipse clip) the plan still carries the road_connections entry — it is built before the first check — but with no cell key and no promotion. "Every entry" holds only on the full path.

- **Claimed:** Open question 6 / build_wall_network step 8: "tower_budget = 4 reliably yields three towers."
  **Actually:** It yields three only when a gate or postern vertex falls among the FIRST tower_budget corner candidates. For any ring with approach_m None (every non-primary ring), approach_gate_index falls back to the +x extremity, which is index 0 of ellipse_ring, so t_i=0 is consumed and skipped — three towers. For ring 0 with a real approach the gate index is wherever the approach is nearest; the postern sits at gate_idx + len//2, so unless gate_idx is in 16..19 neither is in 0..3 and the budget yields four towers with contiguous ids. The mechanism the spec identifies (t_i counts candidates, not placements) is correct; the "reliably" is not, and a porter told to expect three will treat a correct four as an off-by-one.

- **Claimed:** HeightField: "plus, when terrain_detail is present, three octaves of spherical Perlin noise".
  **Actually:** The band count is data-driven, not three: self.bands = [(b['scale_m'], b['amplitude_m'], child_seed(definition['seed'], str(i))) for i, b in enumerate(definition['bands'])], and detail = sum(amplitude * perlin3(p0*R/scale, p1*R/scale, p2*R/scale, seed) for scale, amplitude, seed in self.bands). For the port, the three zeroing factors are, in order: coast = clamp((base - sea_level)/20, 0, 1); strength = max(0, (bilinear(strength_grid) - 0.15)/0.85) where the grid is `0 if water else max(0,1-min(1,river*2)) * max(0,1-min(1,flood)) * biome_factor` with biome_factor from {5:1.4, 3:0.8, 4:1.0, 7:1.0, 15:0.8} defaulting to 0.65; and wet = max(0, 1 - 2*bilinear(water_type)), combined as strength *= coast*coast*(3-2*coast)*wet*wet. terrain_detail['version'] != 1 raises ValueError('Unsupported terrain detail version').

- **Claimed:** Hamlet plot record: "rotation_degrees 4 dp (from atan2, so in (-180, 180])".
  **Actually:** The stored angle is the street heading plus the frontage jitter: angle = atan2(b[1]-a[1], b[0]-a[0]) + radians(5*sin(x*0.13 + z*0.17 + seed%1000)), and degrees = round(degrees(angle), 4) is taken after the jitter. The range is therefore (-185, 185], not (-180, 180]; nothing normalises it, exactly as on the castle side.

- **Claimed:** Kit selection: "u >= 7 selects the last kit because hi clamps."
  **Actually:** The hi = len(kits)-1 argument is inert for the shipped weights. bisect_right([4,7,9], u, 0, 2) returns 2 for every u in [7,9) with or without it, because index 2 is a legal return of a search bounded at hi=2. The clamp only matters if u could equal total, which it cannot since random() is in [0,1). The resulting probabilities 4/9, 3/9, 2/9 and the single-draw claim are correct.

### Missing — blocking

**`path_turn_degrees` — the function `smooth_closed_ring` is gated on, cited by the spec but never defined**

From city_fortifications.py, docstring "Signed heading change of polyline a->b->c, degrees in (-180, 180]": delta = degrees(atan2(c[1]-b[1], c[0]-b[0]) - atan2(b[1]-a[1], b[0]-a[0])), then `while delta <= -180: delta += 360` and `while delta > 180: delta -= 360`; return delta. This is the EXTERIOR/heading change and is deliberately different from castle_geometry.turn_degrees, which measures the interior angle at b. smooth_closed_ring's needle pass replaces p with (prev + 2*p + next)/4 whenever abs(path_turn_degrees(prev, p, next)) > 95. Note also the sub-8 early return: `if len(points) < 8: return [tuple(p) for p in points]` — unchanged AND unrounded, unlike the >=8 path which rounds every coordinate to 2 dp at the end. Without this formula no castle ring polyline can be reproduced, because clip_ring_to_valid routes every successful clip through it.

**Hamlet plot `building_id` resolution rule**

`'building_id': row.get('structure_id', row.get('id'))`. Preset building rows carry `structure_id` (from the civilizations.json block row) AND `id` (from the merged structure), same value. The housing row passed to install(house, ...) is section('housing_profiles')['hamlet_house'] — it has id='building.hamlet_house' and NO structure_id. A literal row['structure_id'] raises KeyError on every cottage. stats.houses then counts p['building_id'] == house['id'].

### Missing — important

**`grow_roads` backtrace: the traced path INCLUDES the already-networked junction cell**

path=[target]; while path[-1] not in network: path.append(previous[path[-1]]); if len(path)>1: paths.append(list(reversed(path))); network.update(path). The loop terminates with path[-1] already IN network, so that cell is part of the path. After reversal each entry of `paths` therefore STARTS at an existing network cell (the root on the first trace) and ENDS at the target. network.update(path) adds the junction cell too plus every new cell. A target already in network gives len(path)==1 and contributes no path at all. The spec's "trace back until a cell already in network" is ambiguous about inclusivity; excluding the junction cell drops the first point of every street polyline, which shifts every `anchors` entry (the +/-3 lookahead) and moves every hamlet plot.

**Verbatim `reference_frame.projection` string (consumer-facing output field)**

'direction=normalize(up+(east*x_m+north*z_m)/radius_m); position=(radius_m+height_m)*direction'. The spec writes only `projection: <string>`.

**Castle plot `kind` vocabulary**

Castle plots use kind in {'service' (default, used by the bailey-services fan and the drawbridge), 'court' (C7 courts), 'landmark' (C7 keep), 'gate', 'tower', 'stair' (C9 perimeter emission)}. The spec's Writes section says castle plots have "the same keys as the hamlet's", whose kind is documented as in {'service','housing'} — a castle plot is never 'housing', and the four extra values are what a viewer or npc_roster filters on.

**`castle_plans` carries no stable key at all**

castle_planner never reads fortress['node'] (the only 'node' references in the module are water.nodes and access_nodes) and never writes a `node` field into the plan. The hamlet plan does write 'node': hamlet['node']. So a castle plan joins ONLY on fortress_id, the ordinal that renumbers on an age advance — there is no terrain-node fallback in the record. This directly constrains the spec's own Open Question 1: for castles the node is not even present in the output today and would have to be added.

**castles.json module records the spec does not reproduce (the file dies with the repo)**

Every module also carries `role` ('curtain'|'node'|'court'|'landmark') and full bands; thickness is {min 2.5, max 4.0, nominal 3.0} on every joinable module. Not given by the spec: drawbridge = {id 'module.drawbridge', structure_id 'building.drawbridge', role 'node', end_type 'gate_join', thickness {2.5,4.0,nom 3.0}, height {1.0,4.0,nom 2.0}, walkway FALSE, compatible_ends ['gate_join']} — dead as a module, reached only as a kit service structure_id; bailey_court = {role 'court', end_type null, thickness_m null, height {0.0,0.0,nom 0.0}, walkway false, compatible_ends []}; keep_tower = {role 'landmark', end_type null, thickness_m null, height {18.0,28.0,nom 24.0}, walkway false, compatible_ends []}; interval_tower = {structure_id 'building.corner_tower', end_type 'tower_join', height {10.0,18.0,nom 14.0}, compatible_ends ['curtain_end']} (dead). Height bands for the live ones: curtain {6,10,nom 8}, corner_join {10,18,nom 14}, gatehouse {10,16,nom 14}, postern {6,10,nom 8}, wall_stair {6,10,nom 8}. Top-level description: 'Independent fortification modules, join rules and typology kits for castle_plans. Does not participate in city or hamlet planner identity.' Each kit's courts[] entries also carry a `role` ('bailey','motte_yard','outer_bailey','inner_ward') that is never read, alongside the offset_scale the spec does flag.

**Registry `revision` numbers that land in the emitted identity blocks**

registry_identity() emits {'schema_version': doc['schema_version'], 'revision': doc['revision'], 'sha256': ...}; on the resolved civilization document those are schema_version 6, revision 12. buildings.json is schema 'fantasy-world-generator.building-registry', schema_version 2, revision 5. castles.json is schema_version 1, revision 1. The spec gives the schema_versions but omits every revision, and `revision` is a written output field on hamlet_plans.identity.registry.

### Missing — minor

**The twelve civilization entity ids that `hamlet_plan` accepts**

human_maritime, human_desert, human_cold, human_large_island, human_rainforest, human_heartland, dwarf, elf, gnome, tidekin, hill_dwarf, frosthold_dwarf. hamlet_plan(entity_id) raises ValueError('Unknown civilization') if population_profile is not one of these. Each entity's buildings.structure_block_ids is ['common','rural'], and the merge source for {**structure, **row} is every structure in THOSE libraries, not just 'rural'.

**Hamlet block fields around the building list**

The hamlet block is {status: 'preconfigured_design', housing_status: 'deferred', selection_notes: 'Compact rural support services; role filters farming, resource and coastal extras at plan time.', buildings: [...], infrastructure: [...]}. _expand_settlement_plan additionally injects per-row staffing_totals, a staffing_summary block, and civilization_id/unit/runtime_placement_enabled/settlement_block keys — none read by the planner. Each buildings[] row also carries its own `name` which overrides the structure's name in the merge (identical values in the shipped data).

**`_plan_pooled` worker-count resolution**

workers = min(workers or (os.cpu_count() or 1), len(sites)) before constructing the ProcessPoolExecutor. Matters if the C++ keeps ICARUS_CITY_WORKERS as a thread-count knob as the spec recommends.

**A stale WHY record that contradicts the code — worth carrying as the contradiction it is**

docs/castle-planner.md states "Missing joins and failed rings remain in `unplaced`. Curtain segments are never silently dropped to invent a gate." build_wall_network step 7 does exactly that: it drops every segment whose center_m is within max(8, segment_depth_m*0.6) of a gate position, with no unplaced row, and the surviving ids are non-contiguous. The same doc pins the flood-proxy measurement's world ("34 of 56 castles unbuildable in a seed-42 size-33 world") and rules "Worlds built by planner 1 must be regenerated" — neither the world parameters nor the regeneration ruling is in the spec.

**Node-projection longitude differs between the river sampler and the road/gate projection**

_sampler's internal local(node) computes lo = -pi + 2*pi*x/(n-1) with no wrap and no pole special case, while support_road_entries/support_approach project nodes through terrain_globe.direction(x, z, n) = `if z==0: (0,1,0); if z==n-1: (0,-1,0); lon = 2*pi*(x % (n-1))/(n-1) - pi; lat = pi/2 - pi*z/(n-1); return (cos(lat)*cos(lon), sin(lat), cos(lat)*sin(lon))`. The spec gives the frame construction but never the `direction` formula, and does not record that the two node projections differ (numerically inert for x in [0, n-1], but a real fork if a port unifies them).
