# Port specification: 02-climate

Extracted from the Python `FantasyWorldGenerator` at commit `b67c99b` before that
repository was retired, then audited against the same source by a second reader.

**Source modules**

- `Sim/icarus_sim/terrain_climate.py`

**Estimated C++ size:** roughly 420 lines, excluding tests.

## Audit: IMPLEMENTABLE

The Python-facing half of this specification is unusually good and I could not find a point in the algorithm where an implementer would have to guess. Every line of terrain_climate.py is accounted for: node_grid's scatter/seam/pole fill, transport_moisture's static rise and loss arrays, the .06/150/.85/.45 constants, the Jacobi sweep with the residual taken on the final pass before the swap, climate_wetness's recipe-3 demand term, the /0.04 runoff normaliser, the Kahn worklist that iterates a list it appends to, the three river conditions, the six node_grid calls in source order, the arrow lattice, every field of result['climate'], and the timing/warnings side effects. I verified the upwind stencil derivation term by term (east and north are the normalised d/dlon and d/dlat of the position vector; q = p*cos(angle) - wind*sin(angle) is a backward rotation, so wind_bearing names the direction the air blows toward), the node ordering and N = (n-2)*(n-1)+2, the Config bounds (wind_bearing 0..360, rain_passes int 1..128, rain_strength 0..3, temperature_offset -40..40, moisture_bias -1..1, river_threshold_km2 .001..10000, size 3..1025), the seed_config draw order, the 48.27/193.08/434.44 km2 resolutions, the raise-don't-clamp comment, and every quoted string and WHY-comment. All of them check out verbatim. The spec is also right that terrain_biomes' temperature line is the same formula (it pre-subtracts sea into h then takes max(0,h) -- algebraically identical, so the 'duplicated' claim stands). What is wrong is the C++ NOTES section, and it is wrong confidently: it says the water stage does not exist and instructs the reader to invent `struct WaterRouting { std::vector<int> receivers; }`, when fwg::WaterRouting, route_water(), add_water(), World::has_water and World::water.receivers all already exist; and it repeats a stale header comment to claim climate is the first reader of river_threshold_km2 when src/world.cpp already reads it and src/config.cpp already performs the runoff_scale-and-throw the spec asks someone to 'confirm'. Those would waste time and produce a colliding type, but they are compile-time collisions in the integration layer, not a different rainfall model; the module itself would come out correct -- hence implementable=true, provided the C++ notes are corrected before anyone acts on them.

## Purpose

`terrain_climate.add_climate` is the world's only rainfall model. It blows a fixed-compass-bearing wind across the sphere, carries humidity from ocean and lake surfaces inland along that wind, makes air rain out where it is forced to rise over terrain, and then routes the resulting rainfall downhill through the receiver tree the water stage already built to get a river network. It exists because the stage before it (`terrain_biomes.add_terrain_labels`) used pure latitude-plus-Perlin-noise moisture, which has no rain shadows, no continental interiors and no orographic wet coasts; and because the water stage's `river` layer assumed uniform unit runoff per cell, so every catchment of a given area was the same river regardless of whether it lay in a desert or a monsoon belt. After this stage runs, biomes read `climate_moisture` instead of the noise field and everything downstream (settlement siting, roads, ecology, magic colleges, city and castle plans) reads `rain_river` instead of `river`. The docstring is emphatic about what it is not: "Deterministic steady-wind moisture experiment; not a weather forecast." There are no seasons, no Coriolis force, no convection, no evaporation feedback, and the lake extents that feed it are not solved from its own rain budget.

## Where it sits in the pipeline

**Must already exist.** `add_climate` is a no-op returning `result` unchanged if either `result['water']` is absent/falsy or `cfg.phase < 6`. Concretely it needs:

- `result['layers']['height']` — bare terrain elevation in metres (post-erosion, post-scaling, post any tectonic-transition rewrite).
- `result['layers']['water_surface']` — the equilibrium spill surface from `terrain_water.add_water`: `sea_level` over ocean, lake spill level over lakes, ground elevation on dry land. Metres.
- `result['layers']['water_type']` — 0 dry, 1 ocean, 2 lake.
- `result['water']['receivers']` — one int per sphere node, the downhill receiver index, or `-1` for an ocean outlet / the global minimum in an oceanless world. Guaranteed acyclic (each receiver precedes its child in the flood order that produced it).
- `result['effective_config']['globe_radius']` and `['sea_level']` — physical metres, after `terrain_area.apply_world_scale`.

**Two call sites, and it runs exactly once per world.**

1. Linear pipeline (`terrain_area.apply_world_scale`, recipe 0 and the ordinary generate path): `initialize_derivation` → `derive_water` (which resolves `river_threshold_km2`) → `add_water` → **`add_climate`** → `add_terrain_labels` → `generate_networks` / `add_environment` / `add_biome_variants` → `derive_population` → `add_settlements` → … The `cfg.phase>=6 and result.get('climate')` guard immediately after is how the ecology/leyline block learns climate succeeded.
2. History ladder (`terrain_history.generate_history`, the 16-stage recipe-3 build): stage 8 is "climate, labels, ecology, ground detail" and calls `add_climate(result, replace(cfg, phase=6))`, then `add_terrain_labels`, then `add_environment`, then `attach_detail`.

**Not re-run.** Age transitions (stages 14 and 15) do not call `add_climate`. `refresh_environment` at stage 9 re-derives biomes from the *existing* `climate_moisture`. So the climate layers are frozen from stage 8 onward and the node indices in `result['climate']['river_segments']` stay valid for the life of the world. `docs/conformance/time-advance.md` states the physical layers — height, water, climate, biome — are read-only to a tick.

**Depends on it afterwards** (all skip entirely if `result['climate']` is missing):
- `terrain_biomes.add_terrain_labels`: `if 'climate_moisture' in layers: wet = layers['climate_moisture'][z][x]` replaces the Perlin moisture; `wetland_access` prefers `rain_river` over `river` as a marsh water source; the `terrain.method` string is swapped for the wind-transport wording.
- `terrain_ecology.add_environment`: gated on `result.get('climate')`; reads `rain_river` for estuary detection.
- `terrain_humans` (road cost), `terrain_magic.add_colleges` (gated on `result.get('climate')`; road cost), `terrain_society.add_world_society`, `terrain_settlements.add_settlements` (gated on `result.get('climate')` and `cfg.phase>=7`; freshwater sources) all read `rain_river`.
- `city_planner._sampler` and `castle_planner` read `world['climate']['river_segments']` falling back to `world['water']['river_segments']`, and index `world['water']['nodes']` with the pairs.
- `terrain_history` world validation requires `rain_river` and `climate_moisture` among the mandatory layers.

## Inputs

**Layers** (all n x n grids of doubles, seam column n-1 == column 0, rows 0 and n-1 constant):
- `layers['water_surface'][z][x]` — metres. Sampled at each sphere node as `height[i]`, the surface the wind flows over. Range roughly `sea_level - 0` up to peak elevation; over water it is flat.
- `layers['water_type'][z][x]` — 0/1/2. Used only as `water[i] = (value > 0)`, i.e. "is this node a moisture source".
- `layers['height'][z][x]` — metres, bare terrain. Used **only** for the temperature model, not for uplift. Do not conflate with `water_surface`.

**effective_config** (physical metres, post-scaling):
- `globe_radius` — metres. Reference world 1774.41 m radius / 11.148961626795008 km circumference; the three presets are 200/400/600 km circumference.
- `sea_level` — metres, default 0.

**world block**:
- `water['receivers']` — `list[int]`, one per sphere node, index of the downhill receiver or `-1`.

**Config fields** (`cfg`), with validated bounds:
- `cfg.size` (`n`) — 3..1025, grid edge.
- `cfg.phase` — gate, needs >= 6.
- `cfg.wind_bearing` — degrees, 0..360, default **90.0** (due east). Compass bearing clockwise from north.
- `cfg.rain_passes` — int, 1..128, default **48**.
- `cfg.rain_strength` — 0..3, default **1.0**.
- `cfg.temperature_offset` — °C, -40..40, default **0.0**.
- `cfg.moisture_bias` — relative, -1..1, default **0.0**.
- `cfg.world_recipe` — 0 or 3; selects the evaporative-demand formula. **FWGC++ is recipe 3; use the recipe-3 branch.**
- `cfg.river_threshold_km2` — km², 0.001..10000. Default 0.15 is authored for the 11.15 km reference world and is **scaled by the square of the width factor before it reaches this module** (see `constants`). At the 200/400/600 km presets it resolves to 48.27 / 193.08 / 434.44 km².

Two upstream derivation paths set the climate knobs differently, and both must be honoured:
- Recipe-3 request path (`terrain_world.default_config`, `auto_parameters=0`): wind_bearing 90, rain_passes 48, rain_strength 1, offsets 0. Fixed, not seeded.
- `auto_parameters=1` path (`terrain_recipes.seed_config`), from a `random.Random(child_seed(seed,'recipe-v1-climate'))` stream in this draw order: `wind_bearing=uniform(0,360)`, `temperature_offset=uniform(-3,3)`, `moisture_bias=uniform(-.06,.06)`; and non-random `rain_passes = min(128, max(24, ceil(size*0.375)))`. Also `derive_water` sets `river_threshold_km2 = max(.01, min(1, land_km2/110))`.

## Outputs

Six new layers, each built by `node_grid(values, points, n)` — scatter one value per sphere node into `grid[z][x]`, then `row[n-1] = row[0]` for every row, `row0 = [values[0]]*n`, `row_{n-1} = [values[-1]]*n` (node 0 is the north pole and node N-1 the south pole, which is why the first and last entries are the pole fill). Identical in effect to `fwg::SphereGrid::to_grid`.

- **`air_moisture`** — final-pass humidity after rain-out, dimensionless, `[0, 1)` in practice. Asymptotically bounded below 1 because over water `available = incoming + .45*(1-incoming)` contracts toward 0.45 and on land it only decreases. Purely diagnostic/visual; nothing downstream reads it except the seam test.
- **`rainfall`** — final-pass rain, dimensionless fraction of available moisture, `[0, 0.85*available]`. **This is a rate on the last pass only, not an accumulation over passes.** Feeds `climate_moisture` and `rain_runoff`.
- **`wind_uplift`** — `rise[i]` in metres, `>= 0`. Static, computed once before the transport loop from `water_surface`. Diagnostic.
- **`climate_moisture`** — `[0, 1]`, clamped. The field biomes actually classify on.
- **`rain_runoff`** — m²-equivalent. Per-node `area[i]*rain[i]/0.04`, then accumulated downstream through the receiver tree so each node holds its own plus all upstream contributions. Comparable directly against `river_threshold_km2 * 1e6`.
- **`rain_river`** — 0/1 int. 1 where the node is dry land, has a receiver, and `rain_runoff >= river_threshold_km2*1e6`.

Plus `result['climate']`, a dict with **no id field of its own** (it is a singleton block, keyed by nothing):
- `version`: `1`.
- `wind_arrows`: list of `{'x':int, 'z':int, 'direction':(3-tuple unit vector), 'wind':(3-tuple unit tangent)}`, a sparse sample for display.
- `river_segments`: `[[i, receivers[i]] for i in rivers]` — **node index pairs**, ascending by `i`. Indexes `result['water']['nodes']`, which is the `sphere_grid` point list. Stable for a fixed `n`; meaningless across a size change.
- `residual`: float, max absolute humidity change on the final pass. Convergence diagnostic only.
- `passes`: `cfg.rain_passes`.
- `transport_step_m`: `radius * 2*pi/(n-1)` — one equatorial cell arc in metres, i.e. how far the wind moves per pass.
- `runoff_input_m2_equivalent`: `sum(area[i]*rain[i]/0.04)` over all nodes, recomputed independently of the `runoff` list.
- `runoff_outlets_m2_equivalent`: `sum(runoff[i] for i where receivers[i] < 0)` — after accumulation.
- `method` and `convergence`: the two honesty strings, quoted verbatim in `reasoning_to_preserve`.

Side effects: `result['warnings'].append(result['climate']['method'])`; `result['timing_ms']['climate'] = elapsed_ms` and `result['timing_ms']['total'] += elapsed_ms`.

`runoff_input_m2_equivalent` and `runoff_outlets_m2_equivalent` are a **conservation check**: a test asserts them equal to 5 decimal places. They are equal iff the receiver graph is acyclic and every node drains to a `-1` root. A divergence means the receiver tree is broken, not that the climate is.

## Algorithm

Let `n = cfg.size`, `R = effective_config['globe_radius']`, `angle = 2*pi/(n-1)`, `bearing = radians(cfg.wind_bearing)`.

---

### Step 0 — Guard

```
if result has no 'water' block, or cfg.phase < 6: return result unchanged
```
No climate block is written, and every downstream consumer's `result.get('climate')` gate fails.

---

### Step 1 — Node graph

`points, areas, _ = sphere_grid(n, R)`. Neighbours are **not** used by this module; only the point list and the per-node spherical areas. Node order is exactly `fwg::sphere_grid`'s: north pole, then `z = 1..n-2` outer, `x = 0..n-2` inner, then south pole. Node count `N = (n-2)*(n-1) + 2`.

Build the wrapping lookup:
```
index(x, z) = node id of (0, z) if z in {0, n-1} else (x mod (n-1), z)
```

---

### Step 2 — Upwind bilinear stencils (the core geometric trick)

For each node `i` at grid cell `(x, z)`:

1. `p = direction(x, z, n)` — the unit position vector. At the poles this is exactly `(0, ±1, 0)`.
2. `lon = 2*pi*x/(n-1) - pi`, `lat = pi/2 - pi*z/(n-1)`. (Note: `x` is already in `0..n-2`, so no modulo is needed here; at a pole node `x = 0`, hence `lon = -pi`.)
3. Local tangent basis:
   - `east  = (-sin(lon), 0, cos(lon))`
   - `north = (-sin(lat)*cos(lon), cos(lat), -sin(lat)*sin(lon))`
   Both are unit and orthogonal to `p` (except degenerately at the poles, where `cos(lat)` is ~1e-16 and `north` collapses to a fixed horizontal direction — deterministic, harmless, and the pole nodes are excluded from the wind-arrow sample anyway).
4. `wind = sin(bearing)*east + cos(bearing)*north`. So bearing 0 = due north, 90 = due east: a **compass bearing clockwise from north**, and it names the direction the wind blows *toward*.
5. Rotate one cell-step **backwards** along the wind to find where the air came from:
   ```
   q = p*cos(angle) - wind*sin(angle)
   ```
   This is a rotation of `p` by `angle` radians in the plane spanned by `p` and `wind`, in the `-wind` sense. `q` is a unit vector (to rounding) because `p ⟂ wind` and both are unit.
6. Convert `q` back to fractional grid coordinates:
   ```
   xx = (atan2(q.z, q.x) + pi) / (2*pi) * (n-1)         # in [0, n-1]
   zz = clamp(0, n-1, (pi/2 - asin(clamp(q.y, -1, 1))) / pi * (n-1))
   ```
   The `clamp(q.y, -1, 1)` before `asin` is the standard guard against a rounded-past-unit dot product.
7. Bilinear corners. `ix = trunc(xx)`, `iz = trunc(zz)`, `fx = xx - ix`, `fz = zz - iz`, `jz = min(n-1, iz+1)`. Both `xx` and `zz` are non-negative so truncation equals floor.
   ```
   stencil[i] = [ (index(ix,   iz), (1-fx)*(1-fz)),
                  (index(ix+1, iz),    fx *(1-fz)),
                  (index(ix,   jz), (1-fx)*   fz ),
                  (index(ix+1, jz),    fx *   fz ) ]
   ```
   Weights always sum to 1. Entries may collapse onto the same node (near a pole, or when `iz == jz` at the south edge); duplicates are kept as separate `(j, w)` pairs and simply add when summed. `ix+1` may equal `n-1`, which `index` wraps to 0 across the seam.

Also store `winds[i] = wind` for the arrow sample.

**Data structure**: `stencils` is `N` fixed-size arrays of 4 `(int, double)` pairs. This is the whole "wind field" — it is precomputed once and reused for every pass, which is what makes `rain_passes` cheap.

---

### Step 3 — Sample the surface

```
height[i] = layers['water_surface'][z][x]      # metres, spill surface
water[i]  = layers['water_type'][z][x] > 0     # ocean OR lake is a source
```

---

### Step 4 — `transport_moisture(height, water, upstream, passes, strength)`

**4a. Static orographic uplift, computed once:**
```
rise[i] = max(0, height[i] - sum(height[j]*w for (j,w) in stencil[i]))
```
Metres of climb between the upwind interpolated surface and this cell. Negative (lee descent) is clamped to 0 — descending air does not gain moisture here, it just stops losing extra.

**4b. Static rain-out fraction, computed once:**
```
loss[i] = min(0.85, 0.06 + strength * rise[i] / 150.0)
```

**4c. Transport loop.** `humidity` starts as `N` zeros.
```
for index in 0 .. passes-1:
    following = new array[N]
    rain      = new array[N]
    for i in 0 .. N-1:
        incoming  = sum(humidity[j] * w for (j, w) in stencil[i])
        available = incoming + 0.45*(1 - incoming)   if water[i]
                    incoming                          otherwise
        rain[i]      = available * loss[i]
        following[i] = available - rain[i]
    if index == passes-1:
        residual = max(|following[k] - humidity[k]| for all k)   # BEFORE the swap
    humidity = following
```
This is a Jacobi sweep: every node reads the *previous* pass's humidity, so pass order within a sweep does not matter and the whole inner loop is trivially parallel.

Returns `{humidity, rain, uplift: rise, residual}`. **`rain` is the final pass's rain only** — the intermediate passes' `rain` arrays are built and thrown away. That is deliberate: the model treats the last sweep as the steady state.

---

### Step 5 — Moisture field

For each node `i` at `(x, z)`:
```
T = 28 - 45*direction(x,z,n).y**2
      - max(0, layers['height'][z][x] - sea_level) * 0.0065
      + cfg.temperature_offset
demand = 0.025 * (1 + max(0, T - 10)/15)     if world_recipe == 3
         0.025                                otherwise
wetness = rain[i] / (rain[i] + demand)
moisture[i] = clamp(0, 1, wetness + cfg.moisture_bias)
```
`direction(...).y` is `sin(latitude)`, so the latitude term is `-45*sin²(lat)`: 28 °C at the equator, -17 °C at the poles. The elevation term is a 6.5 °C/km lapse rate applied only above sea level.

The demand term is an evaporative-demand proxy: warmer air needs more rain to read as "wet". A pinned test asserts `climate_wetness(.02, 30, 3) < climate_wetness(.02, 0, 3)` and `climate_wetness(.02, 30, 0) == .02/.045`.

**Note the layer split**: temperature uses the bare `height` layer; uplift used `water_surface`. This is intentional — a lake's air rises over the lake surface, but the land temperature under the lake is not a thing.

---

### Step 6 — Runoff and downstream accumulation

```
runoff[i] = areas[i] * rain[i] / 0.04
```
Then accumulate leaf-to-root over `parents = water['receivers']` using Kahn's algorithm on the reversed tree:
```
children[p] = number of i with parents[i] == p          # in-degree
queue = [i for i where children[i] == 0]                # ascending index order
for i in queue:                                         # queue GROWS during iteration
    p = parents[i]
    if p >= 0:
        runoff[p] += runoff[i]
        children[p] -= 1
        if children[p] == 0: queue.append(p)
```
The code comments this as "Leaf-to-root accumulation works for any acyclic receiver ordering" — it deliberately does not assume the water stage's topological `order` list is available. Termination: every node is enqueued exactly once iff the receiver graph is a forest; a cycle would leave its members permanently at `children > 0` and silently drop their runoff (detectable as `runoff_outlets != runoff_input`).

---

### Step 7 — River threshold

```
rivers = [i for i in 0..N-1 if (not water[i]) and parents[i] >= 0
                           and runoff[i] >= cfg.river_threshold_km2 * 1e6]
river_set = set(rivers)
```
`rivers` is in **ascending node index order** by construction, and `river_segments` inherits that order.

The three conditions mean: a river is dry land (never drawn across ocean or lake), has somewhere to flow, and drains enough.

---

### Step 8 — Scatter to layers, build the block

Six `node_grid` calls in this order: `air_moisture`, `rainfall`, `wind_uplift`, `climate_moisture`, `rain_runoff`, `rain_river` (the last as `int(i in river_set)`).

Wind arrows: every node with
```
x % max(1, (n-1)//8) == 0  and  z % max(1, (n-1)//6) == 0  and  z not in {0, n-1}
```
i.e. roughly an 8-by-6 lattice of arrows, poles excluded.

Then assemble `result['climate']` as described in `writes`, append the method string to `result['warnings']`, record `timing_ms`.

## Constants

**`0.06` — baseline rain-out fraction per pass (dimensionless).** Even over perfectly flat ground the air drops 6% of what it carries each step. This is what gives a continental interior a moisture *gradient* rather than uniform saturation. Raise it and everything dries out a few cells inland; lower it toward 0 and rainfall becomes a pure function of terrain slope with no distance-from-sea signal at all.

**`150.0` — metres of orographic rise that buys one full unit of extra loss, at `strength = 1`.** Combined with the cap, `loss` saturates at rise = `150*(0.85-0.06)/strength = 118.5/strength` metres per transport step. Because `transport_step_m` at the 200 km / n=129 preset is ~1562 m, that is a gradient of about 7.6%. Smaller values make rain shadows harsher and more binary; larger values soften them.

**`0.85` — maximum rain-out fraction per pass.** Without it a steep enough ridge would take `loss > 1` and drive humidity negative. It also guarantees at least 15% of the air survives every windward wall, so a lee slope is dry but never bone-empty. This cap binds over essentially all real mountain terrain.

**`0.45` — the ocean/lake source term coefficient.** Over water, `available = incoming + 0.45*(1-incoming)`: a relaxation toward a 0.45 equilibrium that can only *raise* humidity below 0.45 and can only *lower* it above. It is what bounds humidity below 1. The steady-state open-ocean humidity is the fixed point of `h = (0.45 + 0.55h)*(1 - 0.06)`, i.e. ~0.876. Raising it wets the whole world uniformly; lowering it starves inland cells first.

**`0.025` — the evaporative-demand floor in `climate_wetness`.** `wetness = rain/(rain+demand)` is a Michaelis–Menten style saturating curve: rain equal to demand gives wetness 0.5. With typical rain values in the 0.02–0.06 band, 0.025 puts the half-saturation point squarely inside the working range, which is why the moisture field uses its full 0..1 span. Raising it desertifies globally.

**`10` and `15` in the recipe-3 demand term** — `demand = 0.025*(1 + max(0, T-10)/15)`. No extra demand below 10 °C; demand doubles at 25 °C and triples at 40 °C. This is the whole mechanism by which hot latitudes read drier than cold ones at equal rainfall. Under `world_recipe == 0` this term is absent and demand is flat 0.025.

**`28`, `45`, `0.0065` in the temperature proxy** — `T = 28 - 45*sin²(lat) - 0.0065*max(0, h - sea_level) + offset`. 28 °C equator, -17 °C pole, 6.5 °C per km lapse rate. **This formula is duplicated verbatim in `terrain_biomes.add_terrain_labels`** and the two must agree or moisture and temperature would describe different planets. Port it once and call it from both.

**`0.04` — the runoff normalising divisor.** `runoff = area * rain / 0.04`. It converts the dimensionless rain field into an "m² equivalent" so that a cell receiving rain 0.04 contributes exactly its own area, letting `river_threshold_km2` stay an honest area in km² and letting the rain-weighted catchment be compared against the water stage's uniform-area catchment on the same scale. Change it and every river threshold in the world silently rescales.

**`1e6` — km² to m²** in the river test.

**`0.15` km² — default `river_threshold_km2`, and the biggest scaling trap in the module.** It is authored for the **11.148961626795008 km reference world** and is multiplied by `runoff_scale(circumference) = (circumference / 11148.961626795008 m)²` *before it reaches this module*, by `terrain_world`'s request builder. The value `add_climate` sees is already post-scaling. At the presets: 200 km → 48.27 km², 400 km → 193.08 km², 600 km → 434.44 km². The ceiling is 10000 km², which binds at 2878.6 km circumference. **`terrain_world` deliberately raises rather than clamps** when the scaled value exceeds it. The `auto_parameters` path instead uses `max(.01, min(1, land_km2/110))` with no width scaling, because in that path the world is 11 km wide.

**`48` — default `rain_passes`, and `0.375`.** Total wind fetch is `passes * transport_step_m = passes * circumference/(n-1)`, so `passes/(n-1)` is the **fraction of the planet's circumference the air travels**. At the default 48 with n=129 that is `48/128 = 0.375`. The `auto_parameters` derivation `rain_passes = min(128, max(24, ceil(size*0.375)))` is the same 37.5% expressed against the grid, which is what keeps the fetch resolution-independent. If you increase `size` without increasing `rain_passes`, moisture stops reaching continental interiors and the whole landmass dries.

**`8` and `6` in the arrow sample** — target arrow counts along x and z; `max(1, (n-1)//8)` and `max(1, (n-1)//6)` are the strides, with `max(1, …)` guarding tiny grids.

**`1.0` — default `rain_strength`**, a plain multiplier on the orographic term only. At 0 the world has uniform 6%-per-pass rain-out and no rain shadows whatsoever; at the bound 3 the loss cap binds after ~40 m of rise.

**`90.0` — default `wind_bearing` (due east)** under the recipe-3 request path. The `auto_parameters` path draws it uniformly from 0..360.

## Data files

**None.** `terrain_climate.py` loads no JSON, no catalogue, no external content of any kind. Its only imports are `math`, `time.perf_counter`, `terrain_erosion.sphere_grid` and `terrain_globe.direction`. Everything it needs comes from `result` and `cfg`.

(The sibling modules it feeds do load data — `biomes.json` via `terrain_biome_catalogue`, `civilizations.json`, etc. — but none of that is in scope here.)

## Random streams

**None. This module draws no random numbers at all and takes no seed.** It is fully deterministic given `(layers, water.receivers, cfg)`.

Two indirect connections worth knowing, both *upstream*:
- Under `auto_parameters=1`, `terrain_recipes.seed_config` draws `wind_bearing`, `temperature_offset` and `moisture_bias` from a `random.Random(child_seed(seed, 'recipe-v1-climate'))` stream, in exactly that order, as `uniform(0,360)`, `uniform(-3,3)`, `uniform(-.06,.06)`. If FWGC++ ever reproduces that path, it needs a *named* stream for it. `fwg::Rng::child(master, domain, variation)` is the right shape; the domain string is the only thing that matters functionally, since the numbers need not match.
- Under the recipe-3 request path (`auto_parameters=0`, which is what FWGC++ targets) those three are **fixed constants**, not drawn. So in the C++ port as it stands, climate needs no RNG and no fourth seed derivation.

`terrain_biomes` does use a seeded Perlin moisture field (`child_seed(cfg.seed,'climate')`), but climate overwrites its result — so that stream's draws still happen and the `terrain.climate_seed` value is still published even in a world where the noise moisture is discarded.

## Ordering hazards

This is a clean rewrite, so bit-parity is not required — but these are the places where container order reaches *structure*, not just the last ULP.

**Reaches output structurally (must be preserved):**

1. **Sphere node order.** `points` from `sphere_grid` (north pole, then row-major `z=1..n-2` × `x=0..n-2`, then south pole) defines the integers in `climate.river_segments` and in `water.receivers`. `city_planner` and `castle_planner` index `world['water']['nodes']` with those integers. `fwg::SphereGrid` already matches exactly; do not reorder it.

2. **`rivers` is built by a list comprehension over `range(N)`**, so `river_segments` is sorted ascending by source node. Consumers do not require it, but the serialised world does, and a diff-based world test would see a reorder.

3. **`node_grid`'s pole fill uses `values[0]` and `values[-1]`.** This is only correct because the pole nodes are first and last in the point list. Any reordering of nodes silently corrupts the pole rows.

**Reaches output numerically (float summation order):**

4. **Stencil entry order.** The four `(j, w)` pairs are summed in the fixed corner order `(ix,iz), (ix+1,iz), (ix,jz), (ix+1,jz)`. Near the poles several collapse to the same `j`, so the sum contains repeated indices with different weights, added in that order.

5. **The Kahn worklist `queue`.** Leaves enter in ascending index; interior nodes are appended in discovery order, and the loop `for i in queue` iterates a list it is appending to. The accumulation `runoff[p] += runoff[i]` therefore happens in a discovery-dependent order. The test asserting `runoff_input_m2_equivalent == runoff_outlets_m2_equivalent` to 5 places is exactly the test this order can perturb. In C++ use a `std::vector<int>` as a growing worklist with an integer read cursor — an `std::queue` gives the same structure; an unordered container does not.

6. **`runoff_input_m2_equivalent` is recomputed** as `sum(a*r/.04 for a,r in zip(areas, air['rain']))` rather than reusing the pre-accumulation `runoff` list. Same values, same order, so it agrees exactly in Python. If you compute it by summing the `runoff` vector before accumulation you get the same answer; if you accumulate in place and try to recover it afterwards you will not.

7. **`residual = max(abs(a-b) for ...)`** over a `zip` in node order. Ties do not matter (it is a max of magnitudes), so this one is safe.

**Not a hazard:** `lookup` and `river_set` are membership-only; their iteration order is never used. `sphere_grid`'s `sorted(ids)` for neighbours matters to other modules but climate does not read neighbours at all.

## Edge cases

**No water block / phase < 6** — returns `result` untouched. No `climate` key, no layers, no timing entry, no warning. Every downstream stage's `result.get('climate')` guard then fails and they skip too. This is the normal path for a phase-5 build and for non-globe or non-tectonic worlds (where `add_water` itself returned early).

**A world with no water at all** (`water_type` all zero — possible in an oceanless world where `route_water` fell back to the global minimum but nothing was below sea level): `available = incoming` everywhere, humidity stays identically 0, `rain` all zero, `residual` 0. Then `climate_moisture = clamp(0, 1, 0/(0+demand) + moisture_bias) = clamp(0,1,moisture_bias)`, `rain_runoff` all zero, `rivers` empty, `river_segments` empty. No division by zero: `demand` has a 0.025 floor, so `rain/(rain+demand)` is always well defined. A pinned test covers this: `transport_moisture([0]*7, [False]*7, …)['rain'] == [0]*7`.

**`rain_passes = 1`** — legal (bounds are 1..128). Humidity is all zero on entry, so `incoming = 0` everywhere; `available` is 0.45 over water and 0 on land; land gets no rain at all and `residual = max(following)`. Not degenerate, just useless. `rain_passes = 0` is refused by `Config.__post_init__`.

**Poles.** `direction` returns exactly `(0, ±1, 0)`, and `lat = ±pi/2` makes `cos(lat)` ~6.1e-17, so `north` degenerates to a fixed horizontal vector (~`(1, 6e-17, 1.2e-16)` at the north pole). The resulting wind is a legal unit tangent and the stencil is well formed — it is just arbitrary which way "north" points at a point where every direction is south. Deterministic and stable. Pole nodes are excluded from `wind_arrows` by `z not in (0, n-1)`. Pole temperature is `28 - 45*1 = -17 °C` plus offset, giving demand 0.025 flat (the `max(0, T-10)` term is zero).

**Seam.** `index(x, z)` wraps `x mod (n-1)`, and `xx` can land in `[n-2, n-1]` making `ix+1 == n`, which wraps to node 1 of that row. `atan2` returning exactly `+pi` gives `xx = n-1`, `ix = n-1`, `fx = 0`, and `index(n-1, iz)` wraps to column 0 — correct, and the `fx = 0` weight makes the out-of-range corner irrelevant anyway.

**South edge of the `zz` clamp.** If `zz` clamps to exactly `n-1`, then `iz = n-1`, `fz = 0`, `jz = min(n-1, n) = n-1`, so all four corners are the south pole node with weights summing to 1. Fine. Similarly `zz == 0` puts weight on the north pole.

**`asin` guard.** `q.y` is clamped to `[-1, 1]` before `asin` because `q` is only unit to rounding.

**Negative uplift** is clamped to 0 by `max(0, …)`, so `loss >= 0.06` always and `following >= 0.15*available >= 0`. Humidity can never go negative.

**A cycle in `receivers`** would leave part of the graph unvisited by the Kahn loop, its runoff silently dropped, and `runoff_outlets_m2_equivalent < runoff_input_m2_equivalent`. The code does not detect this; the comment asserts acyclicity as a precondition supplied by `route_water` (whose flood ordering guarantees each receiver precedes its child). In C++ this is worth an assertion.

**Grid shape invariant.** Every written layer must satisfy `row[0] == row[n-1]` for all rows and `len(set(row)) == 1` for rows 0 and n-1 — `terrain_history` world validation raises `'Invalid sphere seam or pole'` otherwise, and a climate test checks it explicitly for `rainfall` and `air_moisture`.

**Saturation is a real failure mode, not theoretical.** With an unscaled `river_threshold_km2` on a wide world, 100% of land cells became rivers at size 17 — `freshwater_distance` collapsed to 0 everywhere and `flood_risk` saturated at 1. `test_layer_scale_regression` asserts `rain_river` is a proper subset of land: neither every cell nor none.

## Reasoning worth preserving

**The module docstring, verbatim:**
> "Deterministic steady-wind moisture experiment; not a weather forecast."

**The `method` string published into `result['climate']` and appended to `result['warnings']`, verbatim:**
> "Fixed compass-bearing wind transports humidity from equilibrium ocean/lake surfaces. Rising air loses extra moisture as rainfall, drying the lee. Relative rainfall and runoff proxies, not mm/year or discharge. Lake volumes are not solved from this rain budget."

**The `convergence` string, verbatim:**
> "Fixed iteration count; residual is max humidity change on final pass. No convergence guarantee."

Both strings are load-bearing product statements, not decoration: they are what stops a consumer treating `rainfall` as millimetres or `rain_runoff` as cubic metres per second. Carry them into the C++ as the published block's own fields.

**The residual optimisation comment, verbatim (lines 28-30):**
> "Only the final pass's residual is reported and nothing feeds it back into the transport, so computing it on every pass was a whole-grid subtract-and-max built and discarded: 47 of the 48 passes at the default rain_passes."

A measured optimisation. Do not reintroduce a per-pass residual "for diagnostics" — it is 47/48 pure waste at the default.

**The accumulation comment, verbatim (line 65):**
> "Leaf-to-root accumulation works for any acyclic receiver ordering."

This explains why the module runs its own Kahn pass instead of reusing `route_water`'s topological `order` list: it deliberately does not couple to the water stage's internal ordering, only to the acyclicity of the receiver array.

**From `terrain_water` (the upstream contract this module depends on), verbatim:**
> "Uniform unit runoff proxy, not rainfall volume. River width is not modelled."

and
> "Receivers index unique spherical nodes; -1 is an ocean outlet or the global minimum in an oceanless world. Catchment assumes uniform unit runoff."

The second sentence is precisely what `add_climate` exists to replace — the water stage's `river` layer weights every cell equally, this stage weights it by rainfall.

**From `terrain_biomes.add_terrain_labels`, the method string swapped in when climate is present, verbatim:**
> "Temperature remains a latitude/elevation proxy. Biome moisture now follows wind-transport rainfall; lake extents still assume equilibrium filling."

The honest statement of the seam between the two models: rain drives biomes but does not drive lake extents. A future port that makes lakes respond to rainfall has to retire this sentence deliberately, not accidentally.

**From `terrain_scale.runoff_scale`, the measurement that justifies the square-law scaling (paraphrased with its numbers intact, and its explicit non-claim quoted):**

Measured at a fixed raster and seed: `rain_runoff` is cell area times rainfall accumulated downstream. Widening the world does not move the rainfall field — climate runs on the same normalised sphere grid — so runoff follows cell area exactly. Generating seed 42, size 17, at 200/400/600 km and taking the per-cell ratio gave **4.000000** at double the width and **9.000000** at triple, on all 47 land cells, against k² of 4 and 9. Land-river fraction at seed 42, sizes 17/33/65:

```
200 km  unscaled 0.15 km2   100.00% / 99.63% / 85.18%   <- saturated
200 km  derived 48.27 km2    68.09% / 47.76% / 16.01%
400 km  unscaled 0.15 km2   100.00% / 100.00% / 93.78%
400 km  derived 193.08 km2   68.09% / 47.76% / 16.01%
600 km  unscaled 0.15 km2   100.00% / 100.00% / 96.59%
600 km  derived 434.44 km2   68.09% / 47.76% / 16.01%
```
The derived fractions are identical at all three widths, "which is the property being bought: the river network stops depending on how wide the world is."

Its explicit non-claim, verbatim:
> "NOT claimed: that the live 200 km world's runoff is 321.8x the archived 11.15 km reference world's. It is not -- measured per-cell growth between those two spans 13.2x to 764x -- because commit 21df9aa re-derived wavelength, tectonic_relief, amplitude and orogeny along with the radius. Those are two different planets, not one planet at two widths, and the comparison does not test this factor."

**From `terrain_world`, the sweep behind the factor:** re-derive rather than transcribe. Sweeping the factor at 200 km gives 17/33/65 land-river fractions of 91.5/63.8/33.1 at 100x, 78.7/53.7/21.8 at 220x, 68.1/47.8/16.0 at the 321.8x this resolves, 59.6/29.9/0.9 at 1000x and 2.1/0.0/0.0 at 10000x. "The band that satisfies the target at every raster is roughly 100x to 1000x." And on the ceiling: "Do NOT clamp. A silent ceiling here is the same defect class being fixed: it would quietly stop scaling past a width and re-saturate the rivers."

**What the failure looked like before the scaling, verbatim from `terrain_world`:**
> "at the 200 km default every land cell drains more than 0.15 km2, so every land cell is a river, every land cell is its own freshwater source, freshwater_distance is 0 across all land and flood_risk saturates at 1. Three published layers had become constants."

**Pinned behavioural facts from `test_terrain_climate_sites.py`** (these are the acceptance criteria for the port, and are the only conformance evidence that exists — see `open_questions`):
- On a 7-node chain with the upwind stencil `[(i-1, 1.0)]` and only node 0 as a water source, putting a 100 m ridge at node 3 makes `rain[3]` **greater** than the flat case and `rain[5]` **less** — windward wetting and lee drying, in one test.
- All-dry input gives `rain == [0]*7`.
- Changing `wind_bearing` from 90 to 270 changes `rainfall` but leaves `water_surface` identical — climate reads the surface and never writes it.
- Changing `settlement_count` leaves `rainfall` identical — no back-coupling from civilisation to weather.
- `runoff_input_m2_equivalent == runoff_outlets_m2_equivalent` to 5 decimal places.
- `rainfall` and `air_moisture` satisfy the seam and pole invariants.
- A phase-5 and a phase-8 build agree exactly on `height`, `water_surface` and `water_type`.

## Notes for the C++ implementation

**What already exists and maps directly.**
- `points`/`areas` → `fwg::sphere_grid(n, radius)`, which returns `std::shared_ptr<const SphereGrid>` with `point_x`, `point_z`, `areas` in **exactly the Python node order**. Hold the `shared_ptr`; the cache evicts at 2 entries and a reference would dangle. Climate does **not** need `neighbours`.
- `node_grid(values, points, n)` → `SphereGrid::to_grid(values)`. Already closes the seam and the poles identically. Do not hand-roll it.
- `direction(x, z, n)` → `fwg::direction(int, int, int)` returning `Vec3`. Already exact at the poles.
- Layers → `World::layers`, a `std::map<std::string, Grid>`. Read with `at(x, z)`, write six new entries.
- `index(x, z)` → the same `node_at` lambda `src/grid.cpp` already has inside `build_sphere_grid`: `z<=0 ? 0 : z>=n-1 ? 1+(n-2)*(n-1) : 1+(z-1)*(n-1)+floor_mod(x, n-1)`. **Lift it out of `build_sphere_grid` into a reusable free function or a `SphereGrid::node_at(int x, int z)` member** rather than writing a third copy — `grid.hpp`'s own `band_wedge_area` comment says exactly why ("two copies of a formula that must agree is how the two silently stop agreeing"). Note `floor_mod`, not `%`: `ix+1` can reach `n` and, if a future change lets `ix` go negative, C++ `%` would give the wrong node.

**What does not exist yet and must be added.**
- `Config` has no `wind_bearing`, `rain_passes`, `rain_strength`, `temperature_offset`, `moisture_bias`. Add them with the recipe-3 defaults (90.0, 48, 1.0, 0.0, 0.0) and the validated bounds (0..360, 1..128, 0..3, -40..40, -1..1) in `Config::validate()`. `Request` needs matching `std::optional` overrides.
- `river_threshold_km2` **is** already on `Config` (default 0.15) and `scale::runoff_scale` already exists, but the header comments it as "carried for stages not yet ported… Nothing in the kernel reads them." Climate is the first reader. Confirm `resolve()` actually applies `runoff_scale` to it and raises rather than clamps above `kRiverThresholdMaxKm2` — that is the `RequestError('STATE_CAPACITY')` behaviour in Python, and the C++ has `kRiverThresholdMaxKm2 = 10000.0` in `config.hpp` waiting for it.
- There is no `water` stage (`route_water`, `water_type`, `water_surface`, `receivers`) — that is the 01-hydrology assignment. Climate cannot run before it lands. Define the interface climate needs as a struct, e.g.
  ```cpp
  struct WaterRouting { std::vector<int> receivers; };  // -1 = outlet
  ```
  stored on `World`, plus the three layers.
- `World` has no typed climate block. Add:
  ```cpp
  struct WindArrow { int x, z; Vec3 direction, wind; };
  struct ClimateReport {
      int version = 1;
      std::vector<WindArrow> wind_arrows;
      std::vector<std::pair<int,int>> river_segments;   // (node, receiver)
      double residual = 0.0;
      int passes = 0;
      double transport_step_m = 0.0;
      double runoff_input_m2_equivalent = 0.0;
      double runoff_outlets_m2_equivalent = 0.0;
  };
  ```
  with `bool has_climate` on `World` standing in for Python's `result.get('climate')` truthiness gate, and `kHighestImplementedStage` bumped when it lands. The `method`/`convergence` strings belong in the JSON serialiser and in `World::warnings`.

**Python idioms with no direct equivalent.**
- `int(xx)` truncates toward zero. Both `xx` and `zz` are non-negative, so `static_cast<int>` is correct and matches; `std::floor` would too. Do not use `std::lround`.
- The `for i in queue:` loop over a list being appended to. C++ range-for over a `std::vector` invalidates on `push_back`. Use an index cursor:
  ```cpp
  for (std::size_t cursor = 0; cursor < queue.size(); ++cursor) { int i = queue[cursor]; ... queue.push_back(p); }
  ```
  This is the *same* structure, not an approximation.
- `max(abs(a-b) for a,b in zip(...))` over a non-empty range — `N >= 2` always, so no empty-range guard is needed, but `std::max_element` on an empty range is UB, so prefer an explicit accumulating loop.
- `sum(humidity[j]*w for j,w in stencil)` — four terms, left to right. Write the loop; do not reach for `std::inner_product` with a different association.
- Python's `min`/`max` on floats propagate NaN differently from `std::min`/`std::max`. Inputs are validated finite upstream, so this is theoretical, but `loss = std::min(0.85, …)` and the `clamp(0,1,…)` on moisture are the two places it would show.

**Data structures.** `stencils` is the memory hot spot: `N` × 4 × `(int, double)` = 96 bytes per node, 1.5 MB at n=129, 25 MB at n=513, 100 MB at n=1025. Consider a flat `std::vector<int> stencil_index` of size `4N` and a parallel `std::vector<double> stencil_weight` rather than a vector-of-vectors — better locality, and the inner loop is the whole cost of the stage (`passes * N * 4` multiply-adds; at n=129, 48 passes that is ~3.2 M operations, trivial; at n=1025, 128 passes it is ~540 M).

**Parallelism.** The pass sweep is pure Jacobi: every node reads only the previous `humidity`. It parallelises cleanly with no ordering concern *except* that the residual reduction on the final pass must be a deterministic max (a max is order-independent, so even that is safe). Unlike the settlement planners (`ICARUS_CITY_WORKERS`), there is no undocumented process pool here and no unproven pooled-vs-serial equivalence — this stage was always serial.

**Numerics.** Clean rewrite, so no CPython emulation. Note only that the three summations that must agree — `runoff_input_m2_equivalent`, the accumulated `runoff` at outlets, and the test comparing them — are not exactly equal in any float order; the Python test allows 5 decimal places on values of order 1e10, which is generous. Do not assert exact equality.

**Reuse the temperature proxy.** `T = 28 - 45*p.y² - 0.0065*max(0, h - sea_level) + temperature_offset` appears identically in climate and in the biome labelling stage. Put it in one header (`climate.hpp` is the natural home) and have biomes call it.

## Open questions

**1. There is no conformance record for this module.** `docs/conformance/` contains `actor-scale-writes, creature-placement, encounter-index, nomads, read-surface, request-failures, settlement-founding, settlement-records, stage-profile, time-advance, world-geometry, world-handles, world-parameters` — none covers climate, rainfall or hydrology. The only mentions are incidental: `settlement-records.md` lists `rain_river` in the finished layer stack, and `time-advance.md` states climate layers are read-only to a tick. **The Python source and `test_terrain_climate_sites.py` are the entire specification.** Nothing to disagree with, and nothing to cross-check this document against.

**2. Convergence is asserted nowhere.** The block publishes `residual` and explicitly says "No convergence guarantee." Nothing anywhere checks that `residual` is small, and no test pins a value. At the default 48 passes the field is probably far from steady state in continental interiors — that may be *intentional* (a shorter fetch is a drier interior, which is the desired look) or it may be an untested assumption. A porter should publish `residual` as the Python does and resist the temptation to iterate to convergence, because doing so would change every world's moisture field with no evidence it is an improvement.

**3. `rainfall` is the last pass only, and it is not obvious that is right.** `transport_moisture` builds and discards a full `rain` array on every pass but the last. Under a steady-state reading that is correct. Under an "accumulated precipitation" reading it is a bug: a cell that rains hard early and then sits in dry air reports zero. The module never says which reading is intended. The published `method` string ("Relative rainfall and runoff proxies, not mm/year or discharge") suggests the steady-state reading is deliberate. **Port as written; do not switch to an accumulation without a decision.**

**4. The dead `enumerate` in the moisture comprehension.** `for i,((x,z),r) in enumerate(zip(points, air['rain']))` — `i` is never used. Harmless; drop it.

**5. Temperature uses `height`; uplift uses `water_surface`.** Almost certainly deliberate (air rises over a lake's surface, but there is no land temperature under a lake), and it is the only defensible reading. Worth an explicit comment in the port so the next reader does not "fix" it.

**6. `world_recipe == 0` vs `== 3` in `climate_wetness`.** FWGC++ has no `world_recipe` field and targets recipe 3, so the temperature-dependent demand is the one to implement. If a geometry-experiment mode is ever added to the C++, the flat-0.025 branch has to come back with it.

**7. The wind field is a fixed great-circle bearing with no Coriolis, no Hadley cells, no seasonal reversal.** Every cell's wind is the *same compass bearing*, which on a sphere means the streamlines are rhumb-like and spiral into the poles for any bearing that is not due east or due west. At the default bearing 90 (due east) they are latitude circles and the air never changes latitude at all, so a continent's north–south moisture gradient comes purely from the source distribution and the temperature-dependent demand. That is a real and visible property of every default world, and nothing in the repo remarks on it. Worth flagging to the owner before the port hardens the behaviour.

**8. `climate.river_segments` vs `water.river_segments`.** Two blocks publish pairs of the same shape with different meanings (rainfall-weighted vs uniform-area catchment), and consumers pick with `world.get('climate', world.get('water', {}))`. The C++ should either name them distinguishably or keep the same fallback discipline; silently serving the uniform-area one would degrade every city plan without an error.

## Addenda from the audit

The reader above wrote the specification; a second reader went back to the source
to find what it had left out or got wrong. Both are kept: the omissions are
content the implementer needs, and the corrections say where the specification
above is not to be trusted.

### Corrections

- **Claimed:** "There is no `water` stage (`route_water`, `water_type`, `water_surface`, `receivers`) -- that is the 01-hydrology assignment. Climate cannot run before it lands. Define the interface climate needs as a struct, e.g. `struct WaterRouting { std::vector<int> receivers; };` stored on `World`, plus the three layers."
  **Actually:** BLOCKING-GRADE FALSEHOOD: the water stage already exists in FWGC++. `include/fwg/water.hpp` defines `struct WaterRouting { std::vector<char> ocean; std::vector<double> level; std::vector<int> parent; std::vector<double> flow; std::vector<int> order; }` -- the receiver array is named `parent`, with -1 meaning an outlet or the global minimum -- and declares `WaterRouting route_water(const std::vector<double>& height, const SphereGrid& grid, double sea_level)`. `src/world.cpp::add_water` is implemented and already writes the layers `water_type` (enum WaterType { kDry=0, kOcean=1, kLake=2 }), `water_surface`, `water_depth`, `routed_catchment` and `river`. `World` already has `bool has_water` and a `WaterReport water` which already carries `std::vector<int> receivers` and `std::vector<std::array<int,2>> river_segments`. Climate should read `world.water.receivers` and the existing layers; defining a second WaterRouting collides with the existing type. Note also that WaterRouting::order already exists -- which is exactly what the Python's 'Leaf-to-root accumulation works for any acyclic receiver ordering' comment says not to couple to, so the port should still run its own Kahn pass.

- **Claimed:** "`river_threshold_km2` **is** already on `Config` (default 0.15) and `scale::runoff_scale` already exists, but the header comments it as 'carried for stages not yet ported... Nothing in the kernel reads them.' Climate is the first reader. Confirm `resolve()` actually applies `runoff_scale` to it and raises rather than clamps above `kRiverThresholdMaxKm2`."
  **Actually:** Climate is not the first reader, and the confirmation is already satisfied. `src/world.cpp:81` reads it today -- `const double threshold_m2 = cfg.river_threshold_km2 * 1e6;` -- to build the existing `river` layer from `routed.flow`, with a comment stating the threshold is an area scaled at resolve() time. And `src/config.cpp:180-191` already does what the spec asks to be confirmed: absent an explicit override it computes `scaled = cfg.river_threshold_km2 * scale::runoff_scale(circumference)` and throws naming kRiverThresholdMaxKm2 when it exceeds the bound, rather than clamping. The header comment quoted from include/fwg/config.hpp:84-85 is stale, not current.

- **Claimed:** "`cfg.river_threshold_km2` ... is **scaled by the square of the width factor before it reaches this module**" and "The value `add_climate` sees is already post-scaling." (stated unconditionally)
  **Actually:** The scaling is conditional. terrain_world applies it only under `if 'river_threshold_km2' not in overrides:`; when the caller supplies an explicit river_threshold_km2 the value is passed through raw and unscaled, and only the derived value can raise RequestError('STATE_CAPACITY'). So add_climate sees a post-scaled value on the default path and a caller-supplied absolute km2 on the override path. FWGC++ already mirrors this exactly at src/config.cpp:180-191 (`if (request.river_threshold_km2) { cfg.river_threshold_km2 = *request.river_threshold_km2; } else { ...runoff_scale... }`).

- **Claimed:** "On a 7-node chain with the upwind stencil `[(i-1, 1.0)]` and only node 0 as a water source..."
  **Actually:** The stencil is `[[(max(0,i-1),1.)] for i in range(7)]` -- node 0's upwind cell is itself, not index -1. In Python -1 would wrap to the last element and turn the chain into a ring; a C++ port written from the spec's `(i-1)` reads out of bounds at i=0. The test also passes 32 passes and strength 1 explicitly, not the pipeline defaults.

- **Claimed:** "**The Python source and `test_terrain_climate_sites.py` are the entire specification.**" (OPEN QUESTIONS 1), together with "A pinned test asserts `climate_wetness(.02, 30, 3) < climate_wetness(.02, 0, 3)` and `climate_wetness(.02, 30, 0) == .02/.045`."
  **Actually:** Those two climate_wetness assertions live in Sim/tests/test_world_debug.py:35-37, not in test_terrain_climate_sites.py, and the second is assertAlmostEqual, not exact equality. A third file also pins climate output: Sim/tests/test_layer_scale_regression.py:79-90 asserts rain_river is a proper subset of land (neither every land cell nor none) across seeds at sizes 17/33/65, and line 129 asserts rain_river is false at specific cells. The conformance evidence is spread over three test files, not one.

### Missing — important

**Exact pinned transport-kernel test (the only unit-level conformance evidence for transport_moisture)**

From Sim/tests/test_terrain_climate_sites.py::test_windward_rain_and_downwind_drying, in full: `upstream=[[(max(0,i-1),1.)] for i in range(7)]` (node 0's upwind cell is ITSELF, not -1); `flat=transport_moisture([0]*7,[True]+[False]*6,upstream,32,1)`; `ridge=transport_moisture([0,0,0,100,0,0,0],[True]+[False]*6,upstream,32,1)`; asserts `ridge['rain'][3] > flat['rain'][3]` and `ridge['rain'][5] < flat['rain'][5]`; then `dry=transport_moisture([0]*7,[False]*7,upstream,32,1)` asserts `dry['rain']==[0]*7`. Note 32 passes and strength 1, not the pipeline defaults of 48 and 1.0. The spec writes the stencil as `[(i-1, 1.0)]`, which at i=0 is a negative index; a C++ port of the test would read out of bounds.

### Missing — minor

**sphere_grid's per-node area formula, including the pole cap aggregation**

step = pi/(n-1); lat = pi/2 - z*step; area = radius^2 * 2 * step * (sin(min(pi/2, lat+step/2)) - sin(max(-pi/2, lat-step/2))), in m^2; and for z in (0, n-1) this is multiplied by (n-1), so a pole node carries the whole polar cap rather than one wedge. This is load-bearing for climate: runoff[i] = areas[i]*rain[i]/0.04 and the conservation identity runoff_input_m2_equivalent == runoff_outlets_m2_equivalent both depend on it, and the m^2 units are what make the comparison against river_threshold_km2*1e6 an honest area test. FWGC++ already matches (band_wedge_area in src/grid.cpp, times (n-1) when pole at src/grid.cpp:125), so parity holds -- but the spec records only 'per-node spherical area' and the formula itself would be lost with the Python.

**The lat/lon grid is anisotropic, so the upwind rotation spans two latitude rows**

The grid spans 2*pi of longitude and pi of latitude over the same n-1 steps, so the longitude step is 2*pi/(n-1) and the latitude step is pi/(n-1). The transport rotation uses `angle = 2*math.pi/(n-1)` -- one longitude cell, i.e. one equatorial cell arc, i.e. TWO latitude rows. A meridional wind therefore advects two grid rows per pass and the bilinear upwind sample skips the immediately adjacent row entirely. The published transport_step_m = radius*angle is a true arc length for any bearing, so the 'fetch = passes * circumference/(n-1)' reasoning is unaffected; but a porter sanity-checking the stencil against a one-cell-per-pass mental model will conclude it is a factor-of-two bug and 'fix' it, changing every world's north-south moisture gradient.

**transport_moisture's pre-loop initialisations make a direct passes=0 call return zeros rather than raise or return empty**

Line 17 is `humidity=[0.]*len(height); rain=[0.]*len(height)` and line 20 is `residual=0.`, both before the pass loop. The loop rebinds `rain` on every pass, so with passes>=1 the line-17 value is dead; but transport_moisture is a public function the tests call directly, and a direct call with passes=0 returns {'humidity': N zeros, 'rain': N zeros, 'uplift': rise, 'residual': 0.0}. Config.__post_init__ forbids rain_passes=0 only on the pipeline path. A C++ port that builds `rain` only inside the loop and returns the last pass's vector must still return an N-length zero vector, and residual 0.0, for passes=0.

**terrain_globe.direction's full definition (climate calls it twice; the file is about to be deleted)**

direction(x, z, n): if z == 0 return (0., 1., 0.); if z == n-1 return (0., -1., 0.); otherwise lon = 2*pi*(x mod (n-1))/(n-1) - pi, lat = pi/2 - pi*z/(n-1), return (cos(lat)*cos(lon), sin(lat), cos(lat)*sin(lon)). Note the explicit pole short-circuit -- the general formula would give cos(lat) ~ 6.1e-17 rather than exact zero, which is why direction(x,0,n).y**2 is exactly 1 and pole temperature is exactly 28-45 = -17 C -- and the `x mod (n-1)` inside, which climate's own inline lon/lat on line 49 omits because point x values are already 0..n-2. The spec relies entirely on fwg::direction existing; it does and matches exactly (src/grid.cpp:33), but the formula is nowhere in the spec.
