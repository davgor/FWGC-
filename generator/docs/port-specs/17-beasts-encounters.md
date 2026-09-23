# Port specification: 17-beasts-encounters

Extracted from the Python `FantasyWorldGenerator` at commit `b67c99b` before that
repository was retired, then audited against the same source by a second reader.

**Source modules**

- `Sim/icarus_sim/terrain_beast_movement.py`
- `Sim/icarus_sim/terrain_encounters.py`

**Estimated C++ size:** roughly 1500 lines, excluding tests.

## Audit: NOT YET IMPLEMENTABLE

This is an unusually faithful specification — the closest to complete I have audited. Every constant in both modules (SPEED_BY_SIZE, HERD_BY_TIER, SPREAD_PER_HEAD_M, HOST_BIAS/DEFAULT_BIAS, the four reaches, WALK_SLACK, IRRUPTION_YEAR_LOW/HIGH, IRRUPTION_MARGIN, the inline 400./30./.4/.3/20./45./.5 scorer numbers, NOMAD_THREAT, MEDIUM_DOMAIN, MONTHS, both VERSIONs) is present with its correct value and meaning; every tie-break ((score,-node) lexicographic, heapq (distance,node), sorted(dirty), haunts.sort's (distance,uid,node), _host_index's (lat,i), _follow's (span*bias,uid), everything.sort as a STRING sort) is correctly identified; the four builders, _searcher, _priced, the shape/route caches, the two-phase build, _assign_days, _window_months and both index builds are transcribed accurately; the imported travel_cost/_camp/_assign_days/trace/distance are specified rather than pointed at; and every external fact I spot-checked is true (680 profiles with movement counts 594/34/20/17/15 exactly; follower roles pack_predator 8, pursuit_predator 7, scavenger 1, None 1, with bird_of_prey genuinely dead; MEDIA sizes/media/sky counts; ruin carries `uid`=city uid and `id`='ruin-'+uid; settlement_spacing = 450 base x reach_scale unclamped via terrain_world; slope really is degrees(atan(hypot)); CADENCES monthly + COALESCED; PIPELINE order; STATE_KEYS; terrain_read's published `encounters.by_node["<node>"]` surface; the test asserting routed+stranded+solitary==len(groups); the 2,778/1,586/1,184/8 domain split in the conformance record). I set implementable=false on two narrow grounds rather than on any weakness of the algorithm section. First, the spec specifies travel_cost in full but never gives the rule that produces `fields['medium']`, so the water mask on which the entire priced graph depends must be guessed — that is the one point where an implementer stops. Second, `method` and `limits` are literal output fields of both blocks and the spec's own WRITES table claims they are "quoted in reasoning_to_preserve" when the two `method` strings and two sentences of beast_movements.limits appear nowhere in the document; with the Python deleted they are unrecoverable, so a port would emit different prose or none. Both gaps are cheap to close and are supplied below in full; with them patched I would call this implementable without reservation.

**1 blocking gap(s)** were found and are recorded in the addenda below.

## Purpose

Two passes at the end of stage 16 that give the world its moving inhabitants and make them findable.

`terrain_beast_movement.add_beast_movements` takes every already-placed creature site whose species declares a `movement` class other than `nester` and turns it into a *travelling group* with a walked circuit. Four classes exist because four different mechanisms happen to share the outcome "wanders", and collapsing them into one flag would make every moving thing move the same way: `migratory` follows the green wave (high open pasture in the warm months, sheltered low ground in the cold, through a calving camp); `irruptive` is normally sparse and marches in a body only in a year that is actually bad for it; `follower` does not search for a route at all but derives one from a host's, which is how predators and scavengers come to shadow the herds and caravans that already walk; `drifter` is the unburied dead, circuiting between the ruins that made them. The groups are deliberately shaped like `nomads.groups` so one consumer reads both with one code path.

`terrain_encounters.add_encounters` is an index, not a generator. Travelling groups are the one kind of world content a consumer cannot find by looking at a place — a city is at a node, a caravan is at a different node every week — so this indexes every group by the node and the month it can be met at, plus enough description of *what* it is (`domain`, `class`, `airborne`) that a land query can drop the water without joining back through two other blocks. There are no quests, objectives, rewards or difficulty here, on purpose: the consumer that would define them does not exist yet.

## Where it sits in the pipeline

**Must already exist before `add_beast_movements` runs:**

- `result['effective_config']['globe_radius']` — metres.
- `result['layers']` complete enough for `habitat_cells`: `height`, `slope`, `tpi`, `temperature`, `water_type`, `salinity`, `moisture`, `maritime`, `water_depth`, `river`, `natural_biome`/`biome`, and crucially `food_potential` + `natural_food_potential` (written by the humans/farming pass — without them every forage term is 0 and every scorer degenerates).
- `result['wildlife']['sites']` and `result['beast_nests']['sites']` — the creature placement passes (`terrain_nests.add_nests`, stage 13, re-run at each age transition).
- `result['nomads']['groups']` — placement (`terrain_nomads.add_nomads`) **and** routing (`terrain_nomad_routes.add_nomad_routes`), because only bands that already carry `legs` are eligible hosts.
- `result['ruins']` — the flat list of destroyed cities written by `terrain_history.age_transition`. Empty list on a world that has had no age transition, in which case every `drifter` strands.
- `result['config']['seed']` — the *genesis* seed, retained separately from the possibly-stepped `cfg.seed`.
- `result['world_clock']` — optional; minted by the first time advance. Absent on a freshly generated world.
- `result['timing_ms']` with a `total` key — both passes do `result['timing_ms']['total'] += elapsed` with no guard.

**Ordering inside stage 16** (from `terrain_profile.PIPELINE`): `fill_cities`, `fill_hamlets`, `fill_castles`, `add_nomads`, `add_nomad_routes`, **`add_beast_movements`**, **`add_encounters`**, `apply_nomad_effects`, then heroes/story web/NPCs/key locations.

**`add_encounters` depends on `add_beast_movements` and on the nomad chain.** This is stated as load-bearing in `terrain_time_schedule.CADENCES`: the cadence tuple is dependency order, producers before consumers, and "ordering them any other way would build an index of bands that no longer exist."

**Time cadence.** `terrain_time_schedule.CADENCES` runs `beast_movements` and `encounters` on a **monthly** cadence (`DAYS_PER_MONTH` = 30 days), both in `COALESCED` — each replaces its block wholesale, so only the last crossing inside an advance span is executed. Both are also run by `terrain_history` on the age-transition path, and `add_encounters` alone is re-run by `terrain_nomad_api` after a requested band is added (routes, index and write-backs are rebuilt rather than patched).

**Downstream consumers:** `terrain_read_select._near_encounters` (proximity reads — walks `encounters.by_node`, then `occupancy`, then `entries`), `terrain_read` advertises `encounters.by_node["<node>"]` as a published read surface, and `terrain_history.STATE_KEYS` carries both `beast_movements` and `encounters` through save/restore. Nothing else reads `beast_movements` directly.

**Both passes are gated:** `if not cfg.world_recipe or cfg.phase < 16: return result` — they no-op entirely below stage 16.

## Inputs

### `add_beast_movements`

**Config (`cfg`):**
- `cfg.world_recipe` (truthy), `cfg.phase` (>= 16) — gate.
- `cfg.size` — n, grid edge.
- `cfg.seed` — uint32 master; may be a *stepped* seed on a tick.
- `cfg.settlement_spacing` — metres, **already scaled**. Authored base 450 m against the 11.148961626795008 km reference world; the caller multiplies by `reach_scale(circumference) = circumference_m / 11148.961626795008`. On the 200 km small preset that resolves to ≈8072 m; on 600 km, ≈24217 m. Every reach in this module is a multiple of this post-scaled value.
- `cfg.road_max_grade` — default 0.35, dimensionless rise/run, consumed by `travel_cost`.

**World options** (`terrain_world.options(cfg)`, a validated dict):
- `beast_movement_share` — float in [0,1], default 1.0. Share of eligible sites that become groups.
- `nomad_variation` — uint32, default 0. Feeds the RNG stream so this layer can be rerolled independently.

**World blocks:**
- `result['effective_config']['globe_radius']` — metres.
- `result['layers']` — every layer, read as a full `{name: grid[z][x]}` dict per cell by `habitat_cells`.
- `result['wildlife']['sites']` and `result['beast_nests']['sites']` — each site is a dict; the fields this pass touches are `species_id` (str, the key), `node` (int), `x`, `z` (int), `direction` (3-tuple of unit-sphere components), `name` (str). Sites carry other fields (`id`, `tier`, `family`, `kind`, `size`, `role`, `class`, `range_m`, `den`, `real`) which this pass **ignores in favour of the catalogue profile**, keyed by `species_id`.
- `result['nomads']['groups']` — only entries with a truthy `legs`. Fields read: `uid` (str, prefix `nomad-`), `node` (int), `camps` (list of `{id, node, direction, kind, arrive_day, depart_day}`).
- `result['ruins']` — flat list. Fields read: `node` (int, optional) and `uid` (str, optional, defaults `''`). The ruin's `uid` is the *city* uid it was made from, not `'ruin-'+uid` (that is its `id`).
- `result['world_clock']` — optional `{'day': float, ...}`.
- `result['config']['seed']` — the genesis seed (int).
- `result['settlements']['founding']['end_year']` — fallback year source, via `terrain_astrology.reported_year`.

**Derived per-cell fields**, from `terrain_nests.habitat_cells(result, cfg, points, areas)`, one entry per `sphere_grid` node in node order. The scorers lift six parallel arrays out of them once:
- `dirs[i]` = `cell['direction']` — unit vector `(cos(lat)cos(lon), sin(lat), cos(lat)sin(lon))`; pole rows are `(0,±1,0)`. **The y component is `sin(latitude)`** — this is what the host walk's `asin` inverts.
- `height[i]` = `fields.get('height', 0.)` — metres above the datum, signed.
- `forage[i]` = `max(fields.get('food_potential',0.), fields.get('natural_food_potential',0.))` — dimensionless, nominally [0,1]. **Not clamped here**, unlike the copy inside `travel_cost` which is.
- `slope[i]` = `fields.get('slope', 0.)` — **degrees** (`degrees(atan(hypot(gx,gy)))`), typically 0–60.
- `temperature[i]` = `fields.get('temperature', 0.)` — degrees Celsius.
- `tpi[i]` = `fields.get('tpi', 0.)` — **metres**, signed topographic position index (cell height minus an 8-point geodesic-ring mean).
- `fields.get('medium')` — `'land'`, `'marine'`, `'freshwater'` or `'salt_lake'`; used only by `travel_cost` (anything not `'land'` is impassable).
- `cell['z']` — grid row, used for the calendar anchor.

**Creature catalogue** — `terrain_nests.profiles()`, see `data_files`.

### `add_encounters`

- `cfg.world_recipe`, `cfg.phase >= 16` — gate. Reads no layer, no geometry, no config value beyond those two: a camp already carries its node.
- `result['nomads']['groups']` — `uid`, `classification`, `disposition`, `size`, `carries` (list, optional), `seeks` (list, optional), `god_id` (optional), `speed_m_per_day`, `column_length_m`, `camps`.
- `result['beast_movements']['groups']` — `uid`, `species_id`, `kind` (here meaning the *class*), `disposition`, `tier`, `size`, `movement`, `host_uid`, `speed_m_per_day`, `column_length_m`, `camps`.
- Each camp: `node`, `id`, `kind`, `arrive_day`, `depart_day` (the last two may be `None`).
- `terrain_astrology.DAYS_PER_YEAR` = 360.
- `terrain_nests.profiles()` for `medium`, `class` and `sky` per species.

## Outputs

### `result['beast_movements']` (block version **2**)

Top level, in this insertion order (it reaches the JSON):

| Field | Type | Meaning |
|---|---|---|
| `version` | int, const 2 | |
| `groups` | list | Every group, **sorted by `uid` as a string** |
| `counts` | dict `movement -> int` | Insertion order = order of first appearance in the uid-sorted list |
| `considered` | int | Eligible sites seen, before thinning |
| `skipped` | int | Sites the share dropped |
| `share` | float [0,1] | The `beast_movement_share` actually used |
| `routed` | int | `sum(route_status == 'routed')` |
| `stranded` | int | Groups whose circuit could not be built |
| `solitary` | int | Irruptive groups in the solitary phase this year |
| `year` | int | Absolute year the block was built for |
| `year_forage_factor` | float | `round(factor, 6)`, in (0.55, 1.25) |
| `method` | str | Fixed prose, quoted in `reasoning_to_preserve` |
| `limits` | str | Fixed prose, quoted in `reasoning_to_preserve` |

`routed + stranded + solitary == len(groups)` is an invariant the tests assert.

**Group record.** The key field is **`uid`**, formatted `'beastmove-%s-%d' % (species_id, node)`, schema-pinned to `^beastmove-[a-z0-9-]+-[0-9]+$`. Fields:

| Field | Type | Notes |
|---|---|---|
| `uid` | str | the identity |
| `species_id` | str | catalogue key |
| `name` | str or null | `site.get('name')` |
| `movement` | enum | `migratory` / `irruptive` / `follower` / `drifter` |
| `family` | str | from the profile |
| `tier` | int 1–5 | **creature danger tier**, not a settlement scale |
| `kind` | enum | **`animal` or `monster` — this is the profile's `class`**, not the nest kind |
| `source` | enum | `wildlife` or `beast_nests` |
| `host_uid` | str or null | followers only; a nomad band uid or another beast group uid |
| `node`, `x`, `z` | int | the start cell |
| `direction` | list[3] float | `list(site['direction'])` |
| `size` | int >= 2 | head count |
| `speed_m_per_day` | float | absolute metres/day, never scaled |
| `column_length_m` | float | `round(size * spread_per_head, 6)` |
| `disposition` | enum | `hostile` if class is monster, else `wary` |
| `role` | str or null | feeding role; null for monsters |
| `camps` | list, >= 1 | see below |
| `legs` | list | empty on stranded and solitary |
| `route_status` | enum | `routed` / `stranded` / `solitary` |
| `round_length_m` | float | **present only on routed groups**: `round(sum(leg.length_m), 6)` |

**Camp record** (built by `terrain_nomad_routes._camp`): `{'id': '<group uid>-camp-<index>', 'node': int, 'direction': list(cell.direction), 'kind': str, 'arrive_day': int|None, 'depart_day': int|None}`. Camp `kind` values this pass can produce: `calving`, `summer`, `winter` (migratory); `base`, `terminal` (irruptive, drifter, and the stranded/solitary fallback); `base`, `terminal` (follower, after remapping the host's kinds).

**Leg record**: `{'from': camp_id, 'to': camp_id, 'nodes': list[int] (inclusive of both endpoints), 'length_m': float, 'depart_day': int|None, 'arrive_day': int|None, 'branch': 'trunk'}`. This pass only ever emits branch `trunk`; the nomad pass also uses `base_limb`, `spur` and `fission`.

Also writes `result['timing_ms']['beast_movements']` and adds to `result['timing_ms']['total']`.

### `result['encounters']` (block version **2**)

| Field | Type | Meaning |
|---|---|---|
| `version` | int, const 2 | |
| `entries` | list | one per travelling group that has at least one camp |
| `occupancy` | list | one per camp |
| `by_month` | dict `"0".."11" -> list[int]` | all twelve keys always present, possibly empty; values index `occupancy` |
| `by_node` | dict `"<node>" -> list[int]` | only nodes that occur; **rebuilt sorted by `int(key)`** |
| `method`, `limits` | str | fixed prose |

**Entry record.** Its identity field is **`group_uid`**; `entries` is addressed positionally by `occupancy[i]['entry']`.

`group_uid`, `source` (`nomads` | `beast_movements`), `kind` (**a nomad classification or a creature species id — a third meaning of the word**), `domain` (`land`|`ocean`|`lake`), `class` (`animal`|`monster`|`people`), `airborne` (bool), `disposition`, `threat_tier` (int 1–5), `size` (int), `carries` (list[str]), `seeks` (list[str]), `host_uid` (str|null), `god_id` (str|null), `speed_m_per_day` (float), `column_length_m` (float).

**Occupancy record**: `entry` (index into `entries`), `node` (int), `camp_id` (str), `camp_kind` (str — **a fourth meaning of `kind`**), `from_day` (int|null), `to_day` (int|null), `months` (list[int] 0–11).

Also writes `result['timing_ms']['encounters']` and adds to `result['timing_ms']['total']`.

## Algorithm

## PART A — `add_beast_movements(result, cfg)`

### A0. Gate and setup

```
if not cfg.world_recipe or cfg.phase < 16: return result
o        = options(cfg)
share    = float(o.get('beast_movement_share', 1.))
radius   = result['effective_config']['globe_radius']
points, areas, graph = sphere_grid(cfg.size, radius)
cells    = habitat_cells(result, cfg, points, areas)
priced   = _priced(graph, travel_cost(points, cells, cfg))
spacing  = float(cfg.settlement_spacing)
search   = _searcher(priced)          # ONE searcher for the whole pass
```

### A1. `travel_cost` — the edge cost function (from `terrain_nomad_routes`)

Closure over four per-cell arrays. `ceiling = cfg.road_max_grade * GRADE_RELIEF` with `GRADE_RELIEF = 2.2` (default ceiling 0.77). `water[i] = 1 if fields['medium'] != 'land' else 0`. `forage_c[i] = clamp01(max(food_potential, natural_food_potential))`.

```
cost(i, j, d):                        # d = great-circle metres between the two nodes
    if water[i] or water[j] or d <= 0:            return None      # impassable
    (xi,zi), (xj,zj) = points[i], points[j]
    if xi != xj and zi != zj and zi not in (0,n-1) and zj not in (0,n-1):
        if water[lookup[(xi,zj)]] or water[lookup[(xj,zi)]]:  return None
        # a diagonal cannot slip between two water-filled corner cells
    grade = abs(height[j] - height[i]) / d
    if grade > ceiling:                            return None
    green = 1. - .25 * (forage[i] + forage[j]) / 2.
    return d * (1. + 8. * grade**2) * green
```

Not the road cost function: a band fords rather than bridges and takes ground a road would not, which is what `GRADE_RELIEF` buys. Water stays impassable — these are not boats. `green` makes a herd prefer the green way round; it lies in [0.75, 1.0], so it can never make an edge free and never reverses a large length difference. **Costs are symmetric** (`abs` on the grade, symmetric `green`), so a forward and a reverse leg between the same two camps have the same length even though they are traced by separate searches.

### A2. `_priced(graph, cost)` — price every edge once

```
priced = []
for i, edges in enumerate(graph):
    row = []
    for (j, span) in edges:
        v = cost(i, j, span)
        if v is not None: row.append((j, v))
    priced.append(row)
```

Adjacency in `sphere_grid`'s own neighbour order (ascending neighbour index). **Impassable edges are dropped, not priced as infinite**, so the search never sees them — the same set the unpriced form skipped on `edge is None`, in the same order, which is why distances came out float-for-float identical across the optimisation. Roughly 8 directed edges per cell; this replaces several million evaluations of a function whose answer cannot change because the graph and the ground are both fixed for the pass.

### A3. `_searcher(priced)` — a bounded, targetable Dijkstra with persistent scratch

State kept between calls, sized to the node count: `distances[] = +inf`, `parent[] = -1`, `dirty = []`.

```
search(start, limit = +inf, target = None):
    for node in dirty: distances[node] = inf; parent[node] = -1
    dirty.clear()
    distances[start] = 0
    dirty.append(start)
    queue = [(0, start)]                        # binary min-heap on (distance, node)
    while queue:
        (value, i) = heappop(queue)
        if value != distances[i]: continue      # stale entry, lazy deletion
        if i == target: break
        for (j, edge) in priced[i]:
            candidate = value + edge
            if candidate < distances[j] and candidate <= limit:
                if parent[j] < 0: dirty.append(j)
                distances[j] = candidate
                parent[j] = i
                heappush(queue, (candidate, j))
    return distances, parent, sorted(dirty)
```

Four properties the callers rely on, each with its argument:

- **It stops at the reach.** Edge costs are strictly positive, so every prefix of a path costing `limit` or less also costs `limit` or less; a node inside the bound therefore keeps the distance and parent the unbounded search would have given it.
- **It reports what it reached.** A node enters `dirty` the first time it is given a finite distance, which is the first time its parent stops being −1. `start` is pushed explicitly and keeps `parent == -1` forever (no positive-cost edge can relax a node at distance 0), so it is never double-counted. The list is **sorted before return so callers read ascending node order** — load-bearing, because `_irruption` sums forage over it and float addition is not associative.
- **It can stop on a target.** Every node on the shortest path to the target has a strictly smaller distance (strictly, because no edge is free), so all of them are settled with final parents by the time the target is popped.
- **`distances` and `parent` are scratch.** They hold exactly until the next call. Every caller respects this: two read only the reached list, `_drift` reads the distances before returning, and `route` traces the parents before returning. The returned `sorted(dirty)` list is fresh each call and is safe to keep. **Nothing here is thread-safe.**

The relaxation test orders **distance first, reach second** deliberately: the reach test passes on almost every edge a bounded search looks at and is pure overhead there, while most neighbours of a settled cell are already settled better. Both have to hold, so the order is free to choose.

### A4. Field arrays and the context

Lift `dirs`, `height`, `forage`, `slope`, `temperature`, `tpi` from the cells once (see `reads`). Build the host list and the year:

```
hosts = [b for b in (result.get('nomads',{}).get('groups',[]) or []) if b.get('legs')]
year  = world_year(result)
ctx   = {cells, spacing, search, radius, ruins: result.get('ruins',[]) or [],
         year, year_forage: year_forage_factor(result, year),
         dirs, height, forage, slope, temperature, tpi,
         host_index: _host_index(hosts, dirs)}
```

**`world_year(result)`**:
```
clock = result.get('world_clock')
if isinstance(clock, dict) and isinstance(clock.get('day'), (int,float)):
    return int(clock['day'] // (DAYS_PER_YEAR or 1))        # floor division
return int(reported_year(result)[0])   # settlements.founding.end_year, else 0
```
A freshly generated world has no clock — it is minted by the first time advance — so it is read at its founding year, the same day the age lottery already samples.

**`year_forage_factor(result, year)`**:
```
seed = int((result.get('config') or {}).get('seed', 0))      # GENESIS seed
return Random(child_seed(seed, 'beast-year', int(year))).uniform(0.55, 1.25)
```
**Keyed on the genesis seed, deliberately not on `cfg.seed`.** The time advance hands each cadence `replace(cfg, seed=child_seed(cfg.seed, 'time-beast_movements', index))` and this pass runs monthly, so a factor drawn from `cfg.seed` would be a different number in every month of the same year — twelve different weathers inside one year, and a swarm that formed and dissolved with the call rather than with the world.

**`_host_index(hosts, dirs)`** — the hosts sorted by latitude:
```
lats  = [asin(clamp(dirs[h['node']][1], -1, 1)) for h in hosts]
order = sorted(range(len(hosts)), key = lambda i: (lats[i], i))
return ([lats[i] for i in order], [hosts[i] for i in order],
        [dirs[hosts[i]['node']] for i in order])
```
Ties break on position in the caller's list so the order is a function of the world and not of the sort's stability.

### A5. `_site_groups(result)` — the eligible sites, ordered

```
catalogue = {p['id']: p for p in profiles()}
out = []
for key in ('wildlife', 'beast_nests'):          # this order
    for site in (result.get(key) or {}).get('sites', []):
        profile = catalogue.get(site.get('species_id'))
        if profile is None or profile.get('movement','nester') == 'nester': continue
        out.append((site, profile, key))
out.sort(key = lambda pair: (pair[0]['species_id'], pair[0]['node']))   # string, then int
return out
```
The two source lists are merged and then sorted together; because a species id belongs to exactly one catalogue half, `source` never affects the order.

### A6. Group construction and deterministic thinning

```
rng = Random(child_seed(cfg.seed, 'beast-movement-v1', int(o.get('nomad_variation', 0))))
groups, deferred = [], []
stranded = solitary = considered = skipped = 0
for (site, profile, source) in _site_groups(result):
    movement = profile.get('movement','nester')
    considered += 1
    if share < 1. and rng.random() >= share:      # NOTE: no draw at all when share == 1
        skipped += 1; continue
    size = max(2, int(HERD_BY_TIER.get(profile['tier'], 40)))
    group = { uid: 'beastmove-%s-%d' % (site['species_id'], site['node']),
              species_id, name = site.get('name'), movement,
              family = profile['family'], tier = profile['tier'],
              kind   = profile['class'],  source, host_uid = None,
              node, x, z, direction = list(site['direction']), size,
              speed_m_per_day  = SPEED_BY_SIZE.get(profile['size'], 15000.),
              column_length_m  = round(size * SPREAD_PER_HEAD_M.get(profile['size'], 1.), 6),
              disposition = 'hostile' if profile['class']=='monster' else 'wary',
              role = profile.get('role'),
              camps = [], legs = [], route_status = 'stranded' }
    (deferred if movement == 'follower' else groups).append((group, movement))
```
Wildlife is dense — thousands of sites — so routing every eligible one makes the block enormous for little added meaning. The share is a **deterministic thinning over an ordered list, not a cap**, so it stays proportional to the ground rather than to whatever happened to be placed first.

**Followers are deferred**, because they derive from whatever already walks and the herds do not exist yet.

### A7. The shape cache

```
shapes = {}
def shape(group, movement):
    key = (movement, group['node'], group['role']) if movement == 'follower'
          else (movement, group['node'])
    built = shapes.get(key, _MISSING)              # sentinel, because None is a real result
    if built is _MISSING: built = shapes[key] = BUILDERS[movement](group, ctx)
    return built
```
A circuit is a property of the ground a group stands on, not of the group: nothing any builder reads varies between two groups on one cell of one movement class — a follower additionally reads its `role`, because that is what biases its choice of host. So the shape is built once per key and worn by everyone who shares it. What cannot be shared is the naming: `_camp` stamps the group's own uid into every camp id, and `_assign_days` runs on the group's own speed. **The sentinel matters**: a builder returning `None` (stranded) is a result worth remembering, and must not be rebuilt.

### A8. The route cache

```
routes = {}
def route(source, destination):
    found = routes.get((source, destination))
    if found is None:
        _, parent, _ = search(source, target=destination)      # NO reach limit
        nodes = trace(parent, source, destination)
        length = 0.
        for i in range(len(nodes)-1):
            length += distance(dirs[nodes[i]], dirs[nodes[i+1]], radius)
        found = routes[(source,destination)] = (nodes, length)
    return found
```
`trace(parent, start, end)` walks `end` backwards through `parent` until it hits `start`, returning `[]` the moment a parent is negative, otherwise the reversed list (inclusive of both endpoints).

`length` is **great-circle metres along the node path, not the walking cost** — the cost function's grade and forage terms shape the route, but the reported length is plain geodesic distance, accumulated with a plain loop rather than `sum()`.

The cache is keyed on the ordered pair, so `(a,b)` and `(b,a)` are separate entries.

---

### A9. `_seasonal(group, ctx)` — the migratory builder

The green wave: high and open for the warm months, sheltered and low for the cold.

```
start = group['node']
base  = height[start]
span  = max(1., abs(base) + 400.)        # loop-invariant: the GROUP'S ground, not the candidate's
_, _, reached = search(start, SEASONAL_REACH * spacing)      # 3.0 spacings
s = w = None; best_s = best_w = None
for node in reached:
    if node == start: continue           # the start node is the calving camp and cannot also be a seasonal one
    food   = forage[node]
    h      = height[node]
    lift   = clamp01((h - base) / span)
    mark_s = ( food * (1. + lift) * (.4 + clamp01(1. - slope[node]/30.)) , -node )
    if best_s is None or mark_s > best_s: best_s, s = mark_s, node
    drop   = clamp01((base - h) / span)
    warmth = clamp01((temperature[node] + 20.) / 45.)
    mark_w = ( (.3 + food) * (1. + drop) * (.4 + clamp01(abs(tpi[node])/30.)) * (.5 + warmth) , -node )
    if best_w is None or mark_w > best_w: best_w, w = mark_w, node
if s is None or w is None or s == w: return None
return ([(start,'calving'), (s,'summer'), (w,'winter')],
        [(0,1,'trunk'), (1,2,'trunk'), (2,0,'trunk')])
```

- The maximum is over `(score, -node)` **lexicographically**, so ties break on the *lowest* node index. Both scores are taken in one pass over the reachable ground; `clamp01` is spelled out inline rather than called because these two scorers are the innermost loop of the pass and were four million Python calls at size 128 — it is the same arithmetic in the same order.
- Summer wants **height and openness**: ground that cannot be used in the cold months, so it is not competing with anyone. `.4 +` floor means a 30°+ slope still scores 40% rather than zero.
- Winter wants the opposite **and one thing more**: snow settles on the flat, so the camps that work are tucked against relief (`abs(tpi)`) where sun and wind strip the pasture clear. `.3 + food` means a winter camp can be chosen on ground with no forage at all; `.5 + warmth` makes temperature a factor of at most 3:1.
- Calving is the reason the round has a third stop: moving to it is what buys the young their distance from the predators that follow the herd.
- Returning `None` when `s == w` prevents a degenerate two-camp round.

### A10. `_irruption(group, ctx)` — the irruptive builder

One march out of ground that stopped supporting them, until something does. A swarm does not form where conditions are good; it forms where they are marginal and then leaves — so the destination is the best forage in reach and the route is **one way**.

```
start = group['node']
_, _, within = search(start, IRRUPTION_REACH * spacing)      # 4.0 spacings
if not within: return None                                   # stranded
total = 0.
for node in within: total += forage[node]      # plain accumulation, NOT sum()
local_mean = total / len(within)
if forage[start] * ctx['year_forage'] >= IRRUPTION_MARGIN * local_mean:
    return SOLITARY                                          # the normal state
target = None; best = None
for node in within:
    if node == start: continue
    mark = (forage[node], -node)
    if best is None or mark > best: best, target = mark, node
if target is None: return None                               # stranded
return ([(start,'base'), (target,'terminal')], [(0,1,'trunk')])
```

**Two conditions, and neither on its own is enough.** *The year*: `year_forage_factor` scales this year's forage against an ordinary one, one draw for the whole world, so a bad year is a bad year for everybody — which is what makes the swarms of one year a regional event rather than a flat probability wearing a calendar. *The ground*: the group's own cell against the mean of what a march can reach. Marginal ground is ground that falls short of its **neighbourhood**, because a swarm forms where conditions turn rather than where they were always poor — and because an absolute floor is a forage constant, which would mean something different in every biome.

**`within` includes `start`**, so `start` contributes to the mean it is compared against.

**`SOLITARY` is not `None`.** `None` keeps its old meaning — no reachable ground to march to — and is still reported as `stranded`. A swarm that is not swarming is not a failure to route.

The plain accumulation loop is deliberate: CPython compensates a builtin `sum()` of floats (Neumaier), and a native port would have to replicate that to stay bit-exact. `within` being in ascending node order is what makes the total reproducible.

### A11. `_drift(group, ctx)` — the drifter builder

A circuit between the places that made them: ruins, and ground that remembers. These are the dead nobody buried, so the route has no seasonal logic at all.

```
start = group['node']
reach = DRIFT_REACH * spacing                    # 2.5 spacings
distances, _, _ = search(start, reach)
haunts = []
for ruin in ctx['ruins']:
    node = ruin.get('node')
    if node is None or node == start or not (0 <= node < len(cells)): continue
    if isfinite(distances[node]) and distances[node] <= reach:
        haunts.append((distances[node], ruin.get('uid',''), node))
haunts.sort()                                    # (walk distance, uid string, node)
if not haunts: return None
camps = [(start,'base')] + [(node,'terminal') for (_,_,node) in haunts[:3]]
order = [(i, i+1, 'trunk') for i in range(len(camps)-1)] + [(len(camps)-1, 0, 'trunk')]
return camps, order
```

At most **three** haunts; the circuit closes back to the base. `haunts.sort()` is a plain tuple sort — distance, then the ruin's uid string, then node index.

### A12. `_follow(group, ctx)` — the follower builder

A follower does not search; it derives. This is the class that closes the loop the whole feature is for: predators and scavengers walking the routes that nomads and herds already walk.

Finding the host had to change: by the time followers are built nearly everything else in the block is a host, so comparing each follower against every host is quadratic in the size of the block — at size 128 that was half the pass. It is now the same outward latitude walk `terrain_nests.nearest_settled` uses, over the same mathematics: **the angle between two directions is at least the difference of their latitudes**, so with the hosts sorted by latitude and taken nearest-latitude first, from whichever side has the smaller remaining gap, everything still unvisited is at least that far away.

```
start = group['node']
reach = FOLLOW_REACH * spacing                    # 3.0 spacings
bias  = HOST_BIAS.get(group.get('role'), DEFAULT_BIAS)
floor = min(min(bias.values()), 1.)               # bias.get(kind,1.) falls back to one,
                                                  # so the floor must admit one as well
lats, ordered, host_dirs = ctx['host_index']
total = len(lats)
here  = dirs[start]
phi   = asin(clamp(here[1], -1, 1))
left  = bisect_left(lats, phi)                    # first index with lats[i] >= phi
right = left
slack = radius * WALK_SLACK                       # 1e-9 rad -> metres
best  = None
while True:
    gap_l = phi - lats[left-1] if left > 0    else None
    gap_r = lats[right] - phi  if right < total else None
    if gap_l is None and gap_r is None: break
    if gap_r is None or (gap_l is not None and gap_l <= gap_r):
        gap = gap_l; left -= 1; at = left         # ties go LEFT
    else:
        gap = gap_r; at = right; right += 1
    floor_span = radius * gap
    if floor_span > reach + slack: break                                   # stop 1: the reach
    if best is not None and floor_span * floor > best[0][0] + slack: break  # stop 2: the mark
    span = distance(here, host_dirs[at], radius)
    if span > reach: continue                     # NOTE: no slack on this one
    host = ordered[at]
    kind = 'nomad' if host['uid'].startswith('nomad-') else 'beastmove'
    mark = (span * bias.get(kind, 1.), host['uid'])
    if best is None or mark < best[0]: best = (mark, host)
if best is None: return None, None
host = best[1]
camps = [(camp['node'], 'terminal' if camp['kind'] in ('station','terminal') else 'base')
         for camp in host['camps']]
if len(camps) < 2: return host['uid'], None
return host['uid'], (camps, [(i, (i+1) % len(camps), 'trunk') for i in range(len(camps))])
```

**Two thresholds stop the walk, whichever is met first.** *The reach*: a host whose latitude alone puts it past the reach cannot pass the cut, and neither can anything behind it. *The mark*: the winner is the smallest `span * bias`, and `bias` is never above one, so a host at latitude gap `g` cannot mark below `radius * g * bias_min`; once that floor passes the best mark already found, nothing left can win.

The answer is exactly the one a flat scan gives: the winner is the **unique minimum of `(span*bias, uid)`**, which does not depend on the order candidates arrive in.

**A bias below one shortens the effective distance**, so a preferred host wins from further away without ever letting an unreachable one through — the `span > reach` cut is applied to the *raw* span, before the bias.

**`span` is great-circle distance, not walking distance.** A follower can therefore attach to a host it could not walk to.

**The uid is claimed even when the circuit is too short to derive from** (`len(camps) < 2`), because the follower did find a host — it is the host's route that failed it, and the block should say which host.

The follower's camps are the host's camps, **one step behind**: the follower arrives where the host has been rather than where it is going. The camps form a **closed cycle** (`i -> (i+1) % len`, for every `i`, including last → first). Note the kind remapping folds `summer`, `winter`, `lair`, `shrine`, `base` and `calving` all to `base`, and only `station`/`terminal` to `terminal`.

**The follower's own start node is never a camp.**

### A13. `build(group, movement)`

```
built = shape(group, movement)
if movement == 'follower': group['host_uid'], built = built
if built is SOLITARY:
    group['route_status'] = 'solitary'
    group['camps'] = [_camp(group, 0, group['node'], cells, 'base')]
    solitary += 1; return
if not built:                                   # None
    group['route_status'] = 'stranded'
    group['camps'] = [_camp(group, 0, group['node'], cells, 'base')]
    stranded += 1; return
spec, order = built
camps = [_camp(group, i, node, cells, kind) for i,(node,kind) in enumerate(spec)]
legs  = []
for (a, b, branch) in order:
    src, dst = camps[a], camps[b]
    if src['node'] == dst['node']: continue
    nodes, length = route(src['node'], dst['node'])
    if not nodes: continue
    legs.append({from: src['id'], to: dst['id'], nodes: list(nodes),
                 length_m: length, depart_day: None, arrive_day: None, branch: branch})
if not legs:
    group['route_status'] = 'stranded'
    group['camps'] = [_camp(group, 0, group['node'], cells, 'base')]
    stranded += 1; return
_assign_days(group, camps, legs, {'cfg': cfg, 'cells': cells})
group['camps'] = camps
group['legs']  = legs
group['route_status'] = 'routed'
group['round_length_m'] = round(sum(l['length_m'] for l in legs), 6)
```
`list(nodes)` copies the cached path per leg so two groups never share one list. A solitary or stranded group keeps a base camp so the encounter index still places it: a player can meet the solitary phase, just not a swarm on the march.

### A14. `_assign_days` — the calendar (from `terrain_nomad_routes`)

A herding round is anchored so the **winter camp is occupied at midwinter** — that is the camp survival depends on, so it is the one the year is built around. Everything else starts at its own midsummer and runs on travel time, which is length over speed.

```
n     = cfg.size
speed = max(1., group['speed_m_per_day'])
total_travel = 0
for leg in legs: total_travel += max(1, int(round(leg['length_m'] / speed)))
shares = [CAMP_REST.get(c['kind'], 1.) for c in camps]
weight = 0.
for s in shares: weight += s
spare  = max(len(camps), DAYS_PER_YEAR - total_travel)
rest_of = {}
for camp, share in zip(camps, shares):
    held = max(1, int(spare * share / weight))          # int() truncates toward zero
    rest_of[camp['id']] = min(held, CAMP_REST_CEILING.get(camp['kind'], held))
winter = first camp with kind == 'winter', else None
anchor = midwinter_day(cells[winter['node']]['z'], n) if winter
         else midsummer_day(cells[group['node']]['z'], n)
day = anchor
by_id = {c['id']: c for c in camps}
if winter is not None:
    k = index of first leg whose 'from' == winter['id'], else 0
    order = legs[k:] + legs[:k]          # rotate so the clock starts at the winter camp
else:
    order = legs
first = by_id.get(order[0]['from']) if order else None
if first is not None:
    first['arrive_day'] = day % DAYS_PER_YEAR
    first['depart_day'] = (day + rest_of[first['id']]) % DAYS_PER_YEAR
for leg in order:
    travel = max(1, int(round(leg['length_m'] / speed)))
    held   = rest_of.get(leg['from'], 1)
    leg['depart_day'] = (day + held) % DAYS_PER_YEAR
    day += held + travel
    leg['arrive_day'] = day % DAYS_PER_YEAR
    camp = by_id.get(leg['to'])
    if camp is not None and camp['arrive_day'] is None:
        camp['arrive_day'] = day % DAYS_PER_YEAR
        camp['depart_day'] = (day + rest_of.get(camp['id'],1)) % DAYS_PER_YEAR
for leg in order:                # a camp that is only ever a leg's SOURCE
    camp = by_id.get(leg['from'])
    if camp is not None and camp['arrive_day'] is None and leg['depart_day'] is not None:
        held = rest_of.get(camp['id'], 1)
        camp['depart_day'] = leg['depart_day']
        camp['arrive_day'] = (leg['depart_day'] - held) % DAYS_PER_YEAR
```

`midsummer_day(z, n)`: `latitude = 90. - 180.*z/(n-1)`; `phase = 0.5` if `latitude >= 0` else `1.0`; `day = int(round((phase % 1.) * 360)) % 360` — **180 in the north, 0 in the south**. The seasonal phase inverts across the equator: a southern clan winters on the opposite half of the year, and getting that wrong is invisible on a world whose land happens to sit in one hemisphere. `midwinter_day(z,n) = (midsummer_day(z,n) + 180) % 360`.

The second loop repairs the camp a transhumant group holds while the herd moves — never reached as a leg destination, so it would carry no window at all. It is occupied from when the moving limb leaves it until the limb returns. On this module's shapes only the migratory calving camp and the follower's cycle can exercise it.

**Note the follower quirk**: a follower has no `winter` camp (the remap only produces `base`/`terminal`), so its anchor is `midsummer_day` at the **follower's own start row**, even though every one of its camps is at the host's nodes.

### A15. Two-phase build and the block

```
for (group, movement) in groups: build(group, movement)     # non-followers first
ctx['host_index'] = _host_index(hosts + [g for g,_ in groups if g['legs']], dirs)
for (group, movement) in deferred: build(group, movement)   # then followers
everything = [g for g,_ in groups + deferred]
everything.sort(key = lambda g: g['uid'])                   # STRING sort
counts = {}
for g in everything: counts[g['movement']] = counts.get(g['movement'],0) + 1
result['beast_movements'] = { ...see `writes`... }
result['timing_ms']['beast_movements'] = elapsed
result['timing_ms']['total'] += elapsed
```
Herds now exist, so a follower may attach to one as readily as to a nomad band. The host list is **nomad bands first (in `nomads.groups` order, which is uid-sorted), then beast groups in `_site_groups` order** — that concatenation order is the tie-break inside `_host_index`. Followers are never hosts (they are all in `deferred`), and solitary/stranded groups are never hosts (no legs).

---

## PART B — `add_encounters(result, cfg)`

### B1. Gate, then build entries and occupancy in one pass

```
if not cfg.world_recipe or cfg.phase < 16: return result
entries, occupancy = [], []
sources = [ (result.get('nomads',{}).get('groups',[]) or [],           _nomad_entry),
            (result.get('beast_movements',{}).get('groups',[]) or [],  _beast_entry) ]
for (groups, make) in sources:
    for group in groups:
        if not group.get('camps'): continue          # no camps -> no entry index consumed
        index = len(entries)
        entries.append(make(group))
        for camp in group['camps']:
            arrive, depart = camp.get('arrive_day'), camp.get('depart_day')
            occupancy.append({ 'entry': index, 'node': camp['node'],
                               'camp_id': camp['id'], 'camp_kind': camp['kind'],
                               'from_day': arrive, 'to_day': depart,
                               'months': _window_months(arrive, depart, DAYS_PER_YEAR) })
```
Occupancy is recorded at **camps**, where a group dwells, not along legs. A group in transit is derivable from its leg node path and its speed; recording every node of every leg would multiply the block by the length of the walking. Where a group is camped is the thing you cannot derive.

### B2. `_nomad_entry(band)`

```
{ group_uid: band['uid'], source: 'nomads', kind: band['classification'],
  domain: 'land', class: 'people', airborne: False,
  disposition: band['disposition'],
  threat_tier: NOMAD_THREAT.get(band['classification'], 2),
  size: band['size'],
  carries: list(band.get('carries') or []), seeks: list(band.get('seeks') or []),
  host_uid: None, god_id: band.get('god_id'),
  speed_m_per_day: band['speed_m_per_day'], column_length_m: band['column_length_m'] }
```
Nomad bands have no tier of their own, so their classification supplies one: what a band will do to you is a property of what kind of band it is.

### B3. `_beast_entry(group)`

```
(domain, klass, airborne) = _species_facts().get(group['species_id'],
                              ('land', group.get('kind') or 'animal', False))
{ group_uid: group['uid'], source: 'beast_movements',
  kind: group['species_id'], domain, class: klass, airborne,
  disposition: group['disposition'], threat_tier: group['tier'], size: group['size'],
  carries: ['hides','meat'] if group['kind'] == 'animal' else [],
  seeks: {'migratory':['pasture'], 'irruptive':['anything'],
          'follower':['the group ahead'], 'drifter':['rest']}.get(group['movement'], []),
  host_uid: group.get('host_uid'), god_id: None,
  speed_m_per_day: group['speed_m_per_day'], column_length_m: group['column_length_m'] }
```
`_species_facts()` is built once and cached: `{p['id']: (MEDIUM_DOMAIN.get(p['medium'],'land'), p['class'], bool(p.get('sky'))) for p in profiles()}`.

The fallback uses the group's own `kind` (its class) rather than a constant, so a species the catalogue no longer names does not silently read as an animal. A herd is worth something to whoever can take it; a swarm is worth avoiding — hence `carries` only for animals.

The **domain is resolved from the species' authored medium, not from the terrain under the camp**. A group's camps move; what it can live in does not, and a marine group camped at a coastal node is still not something a party walks up to.

### B4. `_window_months(start, end, days_per_year)`

```
if start is None or end is None: return []
first = _month_of(start % days_per_year, days_per_year)
last  = _month_of(end   % days_per_year, days_per_year)
if first <= last: return list(range(first, last+1))
return list(range(first, 12)) + list(range(0, last+1))       # wraps the turn of the year
```
`_month_of(day, dpy) = min(11, int(day * 12 / dpy))` — with `dpy == 360` that is `day // 30`, floored, clamped to 11. Both endpoints are **inclusive**, so a camp held for one day still occupies a whole month.

### B5. The two indexes

```
by_month = {str(m): [] for m in range(12)}          # all twelve keys, in order
by_node  = {}
for i, record in enumerate(occupancy):
    for month in record['months']: by_month[str(month)].append(i)
    by_node.setdefault(str(record['node']), []).append(i)
result['encounters'] = { version: 2, entries, occupancy, by_month,
                         by_node: {k: by_node[k] for k in sorted(by_node, key=int)},
                         method: ..., limits: ... }
result['timing_ms']['encounters'] = elapsed
result['timing_ms']['total'] += elapsed
```
Both indexes hold **indices into `occupancy`**, not into `entries`; the entry is one further hop (`occupancy[i]['entry']`). Within a node or a month, the order is the order `occupancy` was appended — i.e. nomads before beasts, then group order, then camp order. `by_node` is rebuilt as a fresh dict in ascending integer-node order.

## Constants

### `terrain_beast_movement`

| Constant | Value | Units | Meaning and effect of changing it |
|---|---|---|---|
| `VERSION` | `2` | — | Block version. Moved to 2 when irruptions gained a condition in time: under 1 every irruptive group marched every year, so the block was a fixed roster of swarms a consumer could cache against the world's identity. |
| `MOVING` | `('migratory','irruptive','follower','drifter')` | — | The four non-nester classes. Declared but not read by the pass itself; it is the published vocabulary. |
| `SOLITARY` | `'solitary'` | — | Sentinel return value AND the `route_status` string. Distinct from `None`, which means "no route could be built" and is reported as `stranded`. |
| `_MISSING` | unique object | — | Shape-cache sentinel, so a builder that legitimately returned `None` is remembered rather than rebuilt. |
| `SPEED_BY_SIZE` | tiny 26000, small 21000, medium 17000, large 14000, huge 10000, colossal 7000 | **metres/day, absolute** | A herd moves at the pace of its slowest member and a swarm at the pace of the wind behind it. **Never scaled by world size** — a gait belongs to the animal rather than to the world. Directly sets travel days (`length/speed`), so halving these roughly doubles travel time and shrinks every rest window. Fallback 15000 for an unknown size (unreachable: all six are authored). |
| `HERD_BY_TIER` | `{1:900, 2:400, 3:160, 4:60, 5:12}` | head | The pyramid again — many small things, few great ones. Feeds `size` and, through `SPREAD_PER_HEAD_M`, `column_length_m`. Fallback 40 (unreachable; tiers are validated 1–5). |
| `SPREAD_PER_HEAD_M` | tiny .05, small .4, medium 1.2, large 3., huge 8., colossal 20. | **metres/head, absolute** | Column length = head count × this, rounded to 6 dp. A tier-1 tiny swarm is 45 m long; a tier-5 colossal group is 240 m. Not scaled. Fallback 1.0. |
| `HOST_BIAS` | `scavenger {nomad .45, beastmove 1.}`, `bird_of_prey {nomad .8, beastmove 1.}`, `pack_predator {nomad .7, beastmove 1.}` | multiplier on distance | Which host a follower prefers when several are in reach. Scavengers and camp followers attach to people — that is the whole of what they do — while hunters attach to herds. **A plain nearest-host rule got this wrong in one direction: herds outnumber bands roughly forty to one, so nothing ever followed a caravan.** Raising a value toward 1 removes the preference; lowering it makes that host win from proportionally further away. Never above 1, which is what makes the walk's second stopping floor sound. |
| `DEFAULT_BIAS` | `{nomad: .85, beastmove: 1.}` | multiplier | Any role not in `HOST_BIAS`, and `role is None`. |
| `SEASONAL_REACH` | `3.` | **settlement spacings** | Migratory search bound. ≈24.2 km on the 200 km preset. |
| `IRRUPTION_REACH` | `4.` | settlement spacings | Both the neighbourhood the local mean is taken over **and** the march destination bound. ≈32.3 km at 200 km. Widening it lowers `local_mean` in a hilly neighbourhood and therefore changes how often groups swarm, as well as how far they march. |
| `DRIFT_REACH` | `2.5` | settlement spacings | Ruin haunt bound. |
| `FOLLOW_REACH` | `3.` | settlement spacings | Host search bound, in **great-circle** metres, not walking cost. |
| `WALK_SLACK` | `1e-9` | radians | Slack on the latitude bound the host walk stops on. The bound — that the angle between two directions is at least the difference of their latitudes — is exact in real arithmetic; this absorbs the rounding in the `asin` that stands in for a latitude and the `acos` inside `distance`. Same number and same argument as `terrain_nests.LATITUDE_SLACK`, **kept here rather than imported because the two walks stop on different quantities and a shared constant would imply they move together.** 1e-9 rad is 3e-5 m on the widest world this product ships, and the slack is **ADDED** to the threshold, so the walk goes one candidate too far rather than one too few. |
| `IRRUPTION_YEAR_LOW`, `IRRUPTION_YEAR_HIGH` | `.55`, `1.25` | multiplier | How much a year's forage departs from an ordinary one. One draw for the whole world, not one per group: a bad year is bad for everybody at once, which is what makes the swarms of one year a regional event rather than a flat probability wearing a calendar. |
| `IRRUPTION_MARGIN` | `.18` | ratio | How far below its own neighbourhood this year's forage has to fall before the gregarious phase takes over. **Calibrated, and the shape of the distribution it sits in is the reason it is well below one.** An irruptive species is already placed on marginal ground by the nest pass, so `forage(start) / mean(forage within a march)` runs 0.08 at the tenth percentile to 1.14 at the maximum with a median of 0.30 — measured over the 90 irruptive groups of seed 42 at size 17. At 0.18, **59% march in the leanest year of a century, 28% in an ordinary one and 20% in the fattest**, so the solitary phase stays the normal state and a bad year roughly doubles the swarms. **At 0.85 nearly every group marched in every year, which is the behaviour this mechanism exists to remove.** |

Inline magic numbers inside the scorers:

| Number | Where | Meaning |
|---|---|---|
| `400.` | `span = max(1., abs(base) + 400.)` | Height scale over which "higher than home" saturates, in metres. Bigger on high ground so a mountain herd is not pinned at `lift == 1` everywhere. The `max(1., ...)` guards a divide by zero. |
| `30.` | `1. - slope/30.` | Openness fades linearly to 0 at 30° of slope. |
| `.4` | `(.4 + openness)` | Floor on the openness factor: a sheer summer camp still scores 40%. |
| `30.` | `abs(tpi)/30.` | Shelter saturates at 30 m of topographic departure. |
| `.4` | `(.4 + shelter)` | Floor on shelter. |
| `20.`, `45.` | `(temperature + 20.)/45.` | Maps −20 °C…+25 °C to 0…1. |
| `.5` | `(.5 + warmth)` | Floor on warmth: temperature is at most a 3:1 factor. |
| `.3` | `(.3 + food)` | Winter ground need not feed them; it need only be survivable. |
| `1. + lift`, `1. + drop` | both scorers | Height difference is at most a 2× multiplier. |
| `6` | `round(x, 6)` | Decimal places on `column_length_m`, `round_length_m`, `year_forage_factor`. |

### From `terrain_nomad_routes` (consumed here)

| Constant | Value | Meaning |
|---|---|---|
| `GRADE_RELIEF` | `2.2` | Herds and refugees cross ground roads refuse: steeper, and they ford rather than bridge. Multiplies `cfg.road_max_grade` (default 0.35) to a walking ceiling of 0.77. |
| cost shape | `d * (1. + 8.*grade**2) * green` | The `8.` sets how sharply climbing is penalised: at the 0.77 ceiling the factor is 5.74. |
| `green` | `1. - .25*(forage_i+forage_j)/2.` | Range [0.75, 1.0]. Makes a herd prefer the green way round without ever making an edge free. |
| `CAMP_REST` | `winter 5., summer 4., base 3., lair 5., shrine 2., station 1., terminal .35, calving 3.` | Relative share of the leftover year a camp holds. **Splitting the year evenly across camps put a bandit at the hamlet it had raided for four months, which is not a raid.** |
| `CAMP_REST_CEILING` | `terminal 3, station 6, shrine 21` | **Days**, absolute. Some stops are events, not seasons, however much of the year is left to divide: a raid and a market day are measured in days, and a share of the leftover year is the wrong unit for them. |
| `MIDSUMMER_NORTH` | `.5` | Midsummer as a fraction of the year in the north; the south is half a year away. Anchoring on a fraction rather than a day keeps this correct if the calendar length changes. |
| `DAY_MARCH_M` | `35000.` | Not used by this pass (it belongs to merchant leg subdivision), listed so a porter does not wire it in. |

### From `terrain_astrology`

`HOURS_PER_DAY 24`, `DAYS_PER_MONTH 30`, `MONTHS_PER_YEAR 12`, **`DAYS_PER_YEAR = 360`**.

### `terrain_encounters`

| Constant | Value | Meaning |
|---|---|---|
| `VERSION` | `2` | Version 1 entries said only which species they were, so "what is near here, on foot" could not be asked of the index at all — it meant joining back through `beast_movements` to `beast_nests` to drop the water, and on a world three quarters ocean that is most of the index. |
| `NOMAD_THREAT` | `merchants 1, survivors 1, wanderers 2, cultists 3, deserters 3, bandits 4` | On the creature-tier rubric so it reads the same as `beast_nests`. Fallback 2 for an unknown classification. |
| `MONTHS` | `12` | |
| `MEDIUM_DOMAIN` | `marine→ocean, freshwater→lake, salt_lake→lake, land→land, shore→land` | Fallback `land`. **`shore` is a land medium** — `terrain_nests.suitability` refuses a shore species every water cell and then asks for a coast (`coast >= .1`) — so a puffin colony is on the beach and a traveller walks to it. **Flying is not a domain**: a roost is still somewhere you can stand, and folding it in would make `domain == 'land'` stop meaning "reachable on foot". |
| `NOMAD_CLASS` | `'people'` | `animal` and `monster` are the words `wildlife` and `beast_nests` already use; the entry takes them unchanged rather than minting a third spelling. |
| `NOMAD_DOMAIN` | `'land'` | Also the domain fallback for an unknown species. |
| `carries` for a beast | `['hides','meat']` for class `animal`, `[]` otherwise | |
| `seeks` by movement | `migratory→['pasture']`, `irruptive→['anything']`, `follower→['the group ahead']`, `drifter→['rest']` | Fallback `[]`. |

### Seed derivation

`child_seed(master, domain, variation=0)` = first 4 bytes, big-endian, of `sha256("tectonics-v1:{master}:{domain}:{variation}")`. Two domains are used here: **`'beast-movement-v1'`** with variation `nomad_variation`, and **`'beast-year'`** with variation `year`.

## Data files

### `Sim/icarus_sim/terrain_nest_profiles.json`

Loaded once and cached (`terrain_nests._document`, `profiles()`, `roles()`). Top level: `{"profiles": [...], "roles": {...}}`. **680 profiles** in the shipped catalogue.

`profiles()` validates on load and raises on: duplicate ids; an unknown `medium`; empty or non-positive `weights`; a `class` other than `animal`/`monster`; a `tier` outside 1..5; an animal whose `role` is not a known role; a monster with no `biome_weights`.

A representative animal entry — `african-wild-dog`, one of the 17 followers:

```json
{ "id": "african-wild-dog", "name": "African wild dog", "class": "animal",
  "family": "animal", "kind": "den", "medium": "land", "movement": "follower",
  "role": "pack_predator", "size": "small", "tier": 3, "sky": false,
  "occurrence": 0.65, "real": true, "spacing_m": 200,
  "temperature": [14, 42], "requires": {}, "weights": {"plains": 2} }
```

A monster drifter — `banshees`:

```json
{ "id": "banshees", "name": "Banshees", "class": "monster", "family": "undead",
  "kind": "lair", "medium": "land", "movement": "drifter", "size": "medium",
  "tier": 3, "sky": false, "occurrence": 0.55, "real": false, "spacing_m": 400,
  "likeness": "chimpanzee", "likeness_traits": ["bipedal", "incorporeal"],
  "temperature": [-15, 36], "requires": {"ley_umbral": 0.025},
  "weights": {"ley_umbral": 2, "moisture": 2},
  "biome_weights": { "...": ... } }
```

**Fields these two modules actually consume:** `id`, `movement`, `class`, `family`, `tier`, `size`, `role`, `name`, `medium`, `sky`. Everything else (`weights`, `requires`, `biome_weights`, `occurrence`, `spacing_m`, `temperature`, `kind`, `real`, `likeness*`) is read by the placement pass only.

**Selection**: profiles are not chosen here; they are looked up by the `species_id` already stamped on a placed site. Selection is `movement != 'nester'`.

**Current catalogue distribution of `movement`** (the thing that decides how much work this pass does):

| movement | count | by class |
|---|---:|---|
| `nester` | 594 | 338 monster, 256 animal |
| `migratory` | 34 | 31 animal, 3 monster |
| `drifter` | 20 | all monster (the unburied dead) |
| `follower` | 17 | 16 animal, 1 monster |
| `irruptive` | 15 | 8 animal, 7 monster |

`size`: small 299, large 167, medium 88, tiny 55, huge 51, colossal 20. `medium`: land 538, marine 79, freshwater 42, shore 21. `sky`: 25 of 680.

**Follower roles present**: `pack_predator` 8, `pursuit_predator` 7, `scavenger` 1, `None` 1. **`bird_of_prey` appears in `HOST_BIAS` but no follower in the catalogue has that role**, so that row is currently dead; `pursuit_predator` (7 of 17 followers, the second-largest group) is **not** in `HOST_BIAS` and falls through to `DEFAULT_BIAS`. See `open_questions`.

**The undead rule**, which is data and not a tag: *an undead with a grave nests at it; an undead without one wanders.* Skeletons, tomb wardens, barrow wights, cairn wolves and ossuary spiders are raised from burials and hold them; zombies, wraiths, road shades and the wild hunt have no resting place, so they drift. A barrow wight was added to the catalogue in the same pass as five wandering dead so the contrast lives in the data.

**The `roles` table** (21 roles) supplies each animal's biome preferences. Neither of these modules reads it; only `role` as a bias key.

### `Sim/icarus_sim/nomads.json`

Not read by these modules, but it authors the nomad band fields the encounter index reads through: `classification`, `disposition`, `size` band, `speed_m_per_day`, `column_length_per_head_m`, `carries`, `seeks`. Version 2 is required by `terrain_nomads.policy()`.

### Schemas

`Contracts/schemas/beast-movements.schema.json` and `Contracts/schemas/encounters.schema.json`, both asserting `version` const 2. They are structural envelopes only; movement-class authoring is validated by `Sim/tests/test_creature_movement.py` and the irruption trigger by `Sim/tests/test_beast_movements.py`.

## Random streams

Two independent streams, both CPython `random.Random` (Mersenne Twister) in the original.

**1. The thinning stream.**
```
rng = random.Random(child_seed(cfg.seed, 'beast-movement-v1', int(options['nomad_variation'])))
```
Created once per pass. Consumed **only** by `if share < 1. and rng.random() >= share`, once per considered site, in `_site_groups` order.

- **Draw count is data-dependent and conditional.** When `share >= 1.` (the default) the short-circuit means **zero draws are made at all**. When `share < 1.`, exactly one `random()` per considered site — 2 760 draws on seed 42 at size 33.
- Because nothing else touches this stream, the position never matters for anything but the thinning itself. But the conditional means "the stream with share 1.0" and "the stream with share 1.0 minus epsilon" are not the same sequence of states, which is fine and worth knowing.
- The domain string `'beast-movement-v1'` is its own, so nothing here perturbs an existing stream. `nomad_variation` lets this layer and the nomad layer be rerolled together without disturbing the rest of the world.

**2. The year-forage stream.**
```
random.Random(child_seed(genesis_seed, 'beast-year', int(year))).uniform(0.55, 1.25)
```
A **fresh generator per call**, discarded immediately. Exactly one `random()` draw (CPython's `uniform(a,b)` is `a + (b-a) * random()`). Called once per pass, from `add_beast_movements`, and once per year by the test harness when it searches a century for the lean and fat years.

Its master is `result['config']['seed']` — the **genesis** seed — not `cfg.seed`. On a tick `cfg.seed` has been replaced by `child_seed(cfg.seed, 'time-beast_movements', index)` where `index` is the absolute monthly step, so keying on it would yield twelve different weathers inside one year. This is exactly the failure `terrain_time_schedule` exists to prevent, arriving from the other direction: the year's weather is a property of the world and the year, and of nothing about when somebody happened to tick. `Sim/tests/test_beast_movements.py::test_the_year_survives_the_tick_that_asks_about_it` pins it by re-running the pass under three stepped seeds and asserting the same `year_forage_factor`.

**`terrain_encounters` draws no randomness at all.** It is a pure function of the two group lists.

**Nothing else in either module is stochastic.** Every tie-break is deterministic (`-node`, uid string, list position), and the four builders are pure functions of the ground.

## Ordering hazards

Every place iteration order, sort stability or container ordering reaches output. This is the section to read twice.

1. **`_site_groups` sort: `(species_id, node)`** — a *string* comparison then an integer. Both source lists (`wildlife`, then `beast_nests`) are merged before sorting. This order decides the thinning-RNG consumption order, which decides *which* sites survive `share < 1`, and it decides the host concatenation order in step 12.

2. **`sorted(dirty)` in `_searcher`** — the reached list is returned in ascending node order, explicitly so. `_irruption` accumulates `total += forage[node]` over it, and float addition is not associative: reorder this and the local mean moves in the last bits, which can flip a group across `IRRUPTION_MARGIN`.

3. **`_priced` preserves `sphere_grid`'s adjacency order**, which is `sorted(ids)` per node — ascending neighbour index. Equal-cost paths are therefore broken by the heap's pop order, which is `(distance, node)` on the tuple, so ties in distance resolve to the lower node index. Change the adjacency order and equal-cost routes change.

4. **`heapq` pops `(distance, node)` tuples**, so equal distances break on the *lower node index*. A C++ `std::priority_queue<std::pair<double,int>>` with `std::greater` reproduces this; a queue keyed on distance alone does not.

5. **`(score, -node)` maxima in `_seasonal` and `_irruption`** — lexicographic tuple comparison, so equal scores pick the **lowest node index**. This is the same `(score, -node)` maximum `terrain_nomad_routes._best` takes, and the comment says so explicitly, because the two must not drift apart.

6. **`haunts.sort()` in `_drift`** — a plain three-tuple sort on `(walk_distance, ruin_uid_string, node)`. The uid is an explicit tie-break and must be compared as a string.

7. **`sorted(range(len(hosts)), key=lambda i: (lats[i], i))` in `_host_index`** — ties break on position in the caller's list **so the order is a function of the world and not of the sort's stability**. Do not rely on a stable sort to supply this; make `i` an explicit key component.

8. **`bisect_left(lats, phi)` and the left/right walk in `_follow`** — the walk takes the *left* side on `gap_l <= gap_r`. This decides visit order and therefore which of two candidates is compared first, which matters only via the `best is None or mark < best[0]` strict comparison. The winner itself is order-independent (unique minimum of `(span*bias, uid)`), but the *stopping point* is not: a different walk order can stop later and cost more, never less.

9. **`mark = (span * bias, host['uid'])`, minimised** — a **string** tie-break on host uid. `'beastmove-...'` sorts before `'nomad-...'` in ASCII, so a tie between a herd and a band goes to the herd.

10. **`everything.sort(key=lambda g: g['uid'])`** — a *lexicographic string* sort over `beastmove-<species>-<node>`. **Node numbers sort as text**: `beastmove-wolf-10` precedes `beastmove-wolf-9`. This is the published order of `beast_movements.groups` and it propagates into `encounters.entries` order and thence into every occupancy index.

11. **`counts` insertion order** — built by walking the uid-sorted list, so the key order of the `counts` dict is the order of first appearance. It reaches the JSON.

12. **Host concatenation order**: `hosts + [g for g,_ in groups if g['legs']]` — nomad bands (already uid-sorted by `terrain_nomads`) first, then non-follower beast groups in `_site_groups` order (**not** uid order). This is the list index `_host_index` uses as its tie-break, so a tie between two hosts at the same latitude resolves by this concatenation.

13. **`shapes` and `routes` are plain dicts used as memo tables only** — insertion order does not reach output, but a cache miss versus hit must not change the result, which it does not because the searcher's scratch is fully reset per call.

14. **`encounters` source order is fixed**: nomads first, then beast movements. Entry indices are assigned in that order, so every occupancy index in `by_month` and `by_node` depends on it.

15. **`by_month = {str(m): [] for m in range(12)}`** — all twelve keys are created up front, in ascending order, so the JSON always has twelve keys even when empty.

16. **`by_node` is rebuilt as `{k: by_node[k] for k in sorted(by_node, key=int)}`** — keyed by string but ordered by integer value. A naive C++ `std::map<std::string, ...>` would give `"10"` before `"9"`, which is wrong.

17. **Within one `by_node[k]` or `by_month[m]` list, order is append order** — `occupancy` index ascending, i.e. nomads before beasts, group order, then camp order. `terrain_read_select._near_encounters` documents that it preserves "the order it appended", so this is a contract a consumer sees.

18. **`_assign_days` leg rotation** — `order = legs[k:] + legs[:k]` where `k` is the index of the **first** leg whose `from` is the winter camp, else 0. With duplicate camps this could match more than one leg; first wins.

19. **`route` cache is keyed on the ordered pair**, so a cycle's forward and reverse legs are traced independently and can pick different equal-cost paths, even though their lengths are equal.

20. **`_species_facts()` dict comprehension over `profiles()`** — on a duplicate id the last wins, but `profiles()` refuses duplicates at load, so this cannot fire.

## Edge cases

**Gating and missing blocks**

- `cfg.world_recipe` falsy or `cfg.phase < 16` → both passes return `result` untouched, writing no block and no timing.
- `result['wildlife']` / `result['beast_nests']` absent → `(result.get(key) or {}).get('sites', [])` yields `[]`. No groups.
- `result['nomads']` absent → no hosts; every follower strands with `host_uid = None`.
- `result['ruins']` absent or `[]` → every drifter strands. A world that has had no age transition has no ruins.
- `result['timing_ms']` absent or missing `'total'` → **KeyError**. Both passes assume it exists; nothing guards it.
- `result['effective_config']['globe_radius']` absent → KeyError.
- A site whose `species_id` is not in the catalogue → `profile is None` → skipped silently, not counted in `considered`.

**Search degeneracies**

- A start node that is water, or every one of whose edges was dropped by `travel_cost` → `reached == [start]`. `_seasonal` then finds `s is None` and returns `None` (stranded). `_irruption` finds `within == [start]`, so `local_mean == forage[start]`; the test becomes `forage[start] * year_forage >= 0.18 * forage[start]`, which holds for any positive forage (0.55 ≥ 0.18) → `SOLITARY`. **If `forage[start] == 0` both sides are 0 and `0 >= 0` still holds → also `SOLITARY`.** `_drift` finds no haunts → `None`.
- Pole nodes: `sphere_grid` makes row 0 and row n−1 single nodes adjacent to the whole ring below/above, carrying the whole ring's area. `travel_cost`'s diagonal-corner test explicitly exempts `z in (0, n-1)` on either endpoint, because there is no corner cell to slip between at a pole. `direction` returns exactly `(0,±1,0)` there, so `asin(y)` is exactly ±π/2.
- `d <= 0` in `travel_cost` → `None`. Guards a zero-length neighbour span.
- `distance(a,b,radius)` clamps the dot product to [−1,1] before `acos`, so a float overshoot cannot produce NaN.

**Builder degeneracies**

- `_seasonal` returns `None` when `s is None or w is None or s == w`. The `s == w` case is real and is the reason the two scorers must differ; without it the round would be two camps and one degenerate leg.
- `_seasonal` skips `node == start` because *the start node is the calving camp and cannot also be a seasonal one* — in the nomad analogue the comment is sharper: if it were, the limb leg would collapse to zero length, be dropped, and leave the base camp with no leg referencing it and so no day window at all.
- `_irruption` returns `None` for an empty `within` (impossible in practice, since `start` is always reached) and for `target is None` (only when `within == [start]`, which the SOLITARY branch usually catches first).
- `_drift` guards each ruin with `node is None`, `node == start`, and `not 0 <= node < len(cells)` — a stale ruin from a different raster is dropped rather than crashing. `ruin.get('uid','')` tolerates a ruin with no uid.
- `_drift` takes at most three haunts; with exactly one haunt the circuit is base → haunt → base, two legs over the same pair in opposite directions.
- `_follow` with **zero hosts** → `total == 0`, both gaps `None`, immediate break, returns `(None, None)`.
- `_follow` with a host at the same node as the follower → `span == 0`, `mark == (0.0, uid)`, wins outright.
- `_follow` returns `(host_uid, None)` when the host has fewer than two camps — the follower strands but **keeps the host uid**.
- A follower whose host is across water attaches anyway: the reach test is great-circle, not walking.

**Leg construction**

- `if src['node'] == dst['node']: continue` — drops a zero-length leg. A follower whose host has two camps on the same node loses both legs and strands.
- `if not nodes: continue` — `trace` returned `[]`, meaning the destination was unreachable from the source through `priced`. Cannot normally happen for camps found by the same graph, but *can* for a follower, whose leg endpoints were found by the host's search, not its own — except that those camps were routed by the host through the same static graph, so they are connected.
- `if not legs:` after the loop → stranded, and the already-built `camps` are **discarded** and replaced by a single base camp at the group's own node.

**Calendar**

- `speed = max(1., speed_m_per_day)` guards a divide by zero.
- `travel = max(1, int(round(length/speed)))` — **every leg costs at least one day**, even a 10 m one.
- `spare = max(len(camps), DAYS_PER_YEAR - total_travel)` — a circuit whose travel exceeds a year still leaves at least one day per camp, so `rest_of` is never empty.
- `held = max(1, int(spare*share/weight))` — at least one day per camp.
- `weight` is a sum of `CAMP_REST` values, all positive, so never zero (camps is non-empty whenever `_assign_days` is called).
- `order[0]['from']` — `legs` is non-empty by construction at this point, so no index error.
- Days wrap with Python `%`, which is **non-negative for a positive modulus**. `(leg['depart_day'] - held) % 360` relies on this: in C++ use a floor-mod, not `%`.
- `int(round(x))` is Python's **banker's rounding** (half to even). See `cpp_notes`.
- `midsummer_day` for the south: `phase = 1.5`, `phase % 1. == 0.5`... **no** — for the southern hemisphere `phase = MIDSUMMER_NORTH + .5 = 1.0`, and `1.0 % 1. == 0.0`, giving day 0. Midwinter south is 180.

**Encounters**

- `if not group.get('camps'): continue` — a group with no camps produces **no entry and no index slot**. Every beast group has at least one camp by construction; a nomad band always has its origin camp; so this rarely fires. But a hand-built or patched block could.
- `_window_months(None, x, ...)` or `(x, None, ...)` → `[]`. A **stranded or solitary group has null days**, so it carries an occupancy record with `from_day: null`, `to_day: null`, `months: []`. It therefore appears in `by_node` but in **no** `by_month` bucket. The block's `limits` says a null day window means "here, always" rather than "here, never" — **but the index does not encode that**, and a consumer scanning `by_month` will miss it entirely. This is the live gap `CONTENT-BEAST-MOVEMENTS-STRANDED` step 4 names.
- `_month_of` clamps with `min(11, ...)` so `day == 360.0` (which `% 360` should have prevented) cannot index 12.
- A one-day camp occupies a whole month; a camp spanning day 29 to day 31 occupies months 0 and 1.
- `by_month` always has twelve keys; `by_node` has only the nodes that occur.
- An unknown `species_id` in `_beast_entry` falls back to `('land', group['kind'] or 'animal', False)`.
- An unknown nomad classification in `NOMAD_THREAT` falls back to threat tier 2.
- `_species_facts` is `lru_cache`d for the process. If the catalogue were reloaded mid-process the cache would be stale — it cannot be, since `profiles()` is cached too.

**Stranding rate.** On seed 42 size 33 after the version-2 change: `considered 2760, routed 1576, stranded 933 (33.8%), solitary 251 (9.1%)`; 1184 of 2760 (42.9%) carry no legs. That is expected on a world four fifths ocean with `resolved_octaves: 0` — a seasonal round between high summer and winter ground needs elevation to have somewhere to go. **A high strand rate is plausibly the correct output, not a routing failure**, and a stranded group does not currently carry the reason it stranded.

## Reasoning worth preserving

These are the comments that record *why*, and they exist nowhere else.

**On why the four classes are separate at all** (module docstring):
> "The nest passes place everything as if it lived somewhere. Most creatures do. The ones that do not are here, and each moves for a different reason, which is why they are separate classes rather than one 'wanders' flag."
> `migratory` — "follows the green wave... Routes are inherited, so a herd walking the same ground each year is the correct behaviour, not a limitation."
> `irruptive` — "Not a season -- a trigger, and since `world_clock` exists the trigger is a particular year being bad. A group whose year is ordinary stays in the solitary phase and carries `route_status: \"solitary\"`, which is a normal state and not a failed route."
> `follower` — "A wolf pack does not hold a range when the herd it eats does not either."
> `drifter` — "the dead with no grave to return to. An undead with one nests at it; these have nowhere to be, so they move between the places that made them."
> "Groups share the shape of `nomads.groups` deliberately, so an encounter index or a time mover reads both with one code path."

**On the four optimisations, and the only claim worth making about them** (module docstring, "Cost, and the four things that decide it"):
> "This pass is the largest in a size-128 generation and the only superlinear one, so the shape of the work is part of the design rather than an implementation detail. Four structures carry it, and each exists to remove a term that grew with the world:
> **priced edges** — every passable step is priced once... The search below is run thousands of times over one static graph, and the cost function is the expensive part of it.
> **bounded search** — ...The old form searched the whole land component and then scanned every cell in the world to find the two hundred it wanted.
> **one shape per ground** — a route depends on the ground under the group, not on the group. Two species standing on one cell walk the same circuit, so the circuit is built once and worn by both.
> **a latitude walk** — `_follow` picks the nearest host out of everything that already walks, and everything that already walks is most of this block. Comparing every follower against every host is the quadratic that made the pass the successor to `add_world_society`.
> **None of the four moves a float. The block this module writes is byte-identical across the change, floats by `repr`, at sizes 33, 65 and 128 -- which is the only claim worth making about a performance change to a deterministic generator.**"

`docs/performance.md` records the outcome: exponent 1.81 → **1.10 in situ** (1.47 on an isolated harness), 35.8 s → **3.60 s at size 128, 2.6% of the run**, projected ~17–25 s at size 257 against a previous projection of ~445 s. "an unbounded Dijkstra per group, cached at full map width so the cache alone reached **2.2 GB**"; "148 million list slots over a size-128 run, against 4.4 million cells actually settled."

**On the relaxation test order** (`_searcher`):
> "Distance first, reach second, and not the other way round: the reach test passes on almost every edge a bounded search looks at and is pure overhead there, while most neighbours of a settled cell are already settled better. Both have to hold, so the order is free to choose."

**On the scratch arrays** (`_searcher`):
> "`distances` and `parent` are therefore scratch. They hold for exactly as long as it takes to call this again, which every caller in this module respects... The reached list is a fresh one each time and is safe to keep. **Nothing here is thread-safe, and nothing in this generator is.**"

**On the sorted reached list** (`_searcher`):
> "the list is sorted before it is returned so a caller reads it in ascending node order. **That order is load-bearing: `_irruption` sums forage over it, and float addition is not associative.**"

**On spelling `clamp` out** (`_seasonal`):
> "`clamp` is spelled out rather than called. That is not style: the two scorers are the innermost loop of this pass and were four million Python calls at size 128. It is the same arithmetic in the same order -- `clamp` *is* `max(0., min(1., v))` -- and the pick is the same `(score, -node)` maximum `_best` takes, so the two camps come out the same cells."

**On the calving camp** (`_seasonal`):
> "Calving is the reason the round has a third stop: moving to it is what buys the young their distance from the predators that follow the herd."

And the nomad analogue's version of the same guard, which explains the failure mode:
> "The start node is the base and cannot also be a seasonal camp: if it were, the base_limb leg would collapse to zero length, be dropped, and leave the base camp with no leg referencing it and so no day window at all."

**On the plain accumulation loop** (`_irruption`):
> "Plain accumulation rather than sum(): **CPython compensates a builtin sum of floats and a native port would have to replicate that to stay bit-exact.** `within` is in ascending node order, which is what makes this total reproducible."

The same note appears in `travel_cost`: "Plain accumulation rather than a sum(): one term, and the native port would otherwise have to carry a compensated accumulator for nothing." And in `terrain_nests.distance`, the opposite ruling for a reason: "**Keep sum()**: CPython 3.12 sums floats with Neumaier compensation, so an explicit `a[0]*b[0]+a[1]*b[1]+a[2]*b[2]` is a different (less accurate) value by about one ULP and silently changes generated worlds on some seeds."

**On the irruption margin being calibrated** (`IRRUPTION_MARGIN`):
> "Measured against the neighbourhood rather than an absolute floor because a swarm forms where conditions turn, not where they were always poor -- and because an absolute floor is a forage constant, which would mean something different in every biome... `forage(start) / mean(forage within a march)` runs 0.08 at the tenth percentile to 1.14 at the maximum with a median of 0.30 -- measured over the 90 irruptive groups of seed 42 at size 17. At 0.18, 59% of them march in the leanest year of a century, 28% in an ordinary one and 20% in the fattest... **At 0.85 nearly every group marched in every year, which is the behaviour this card exists to remove.**"

**On one draw for the world** (`IRRUPTION_YEAR_LOW/HIGH`):
> "One draw for the whole world, not one per group: a bad year is bad for everybody at once, which is what makes the swarms of one year a regional event rather than a flat probability wearing a calendar."

And from `docs/beast-movement.md`: "a per-group draw would make 'a bad year' mean nothing and leave a flat probability wearing a calendar."

**On the genesis seed** (`year_forage_factor`):
> "**Keyed on the world's genesis seed, deliberately not on `cfg.seed`.** The time advance hands each cadence a stepped config... and this pass runs on a *monthly* cadence, so a factor drawn from `cfg.seed` would be a different number in every month of the same year. That is the failure `terrain_time_schedule` exists to prevent, arriving from the other direction: **the year's weather is a property of the world and the year, and of nothing about when somebody happened to tick.**"

**The stated, measured imprecision** (`world_year`):
> "**Known imprecision, stated because it is visible in the output.** On a tick this reads the clock as it stands when the pass runs, and `advance_time_request` writes the advanced `world_clock` only after its cadence loop finishes. So a span crossing several years builds this block for the year the span *started* in -- **measured at 5250 against a clock that ended at 5252 on a two-year advance.** Generation, the age path and any span that does not cross a year boundary are exact. Closing it means the executor handing each cadence step its own day, which is a change to `terrain_time` and belongs to whoever owns that module."

**On the follower bias** (`HOST_BIAS`):
> "Scavengers and camp followers attach to people -- that is the whole of what they do -- while hunters attach to herds. **A plain nearest-host rule got this wrong in one direction: herds outnumber bands roughly forty to one, so nothing ever followed a caravan.**"

And in `_follow`: "A bias below one shortens the effective distance, so a preferred host wins from further away without ever letting an unreachable one through."

**On the latitude slack** (`WALK_SLACK`):
> "It is the same number and the same argument as `terrain_nests.LATITUDE_SLACK`, **kept here rather than imported because the two walks stop on different quantities and a shared constant would imply they move together.** 1e-9 rad is 3e-5 m on the widest world this product ships, and **the slack is ADDED to the threshold, so the walk goes one candidate too far rather than one too few.**"

**On claiming the host uid even on failure** (`_follow`):
> "The uid is claimed even when the circuit is too short to derive from, because the follower did find a host -- **it is the host's route that failed it, and the block should say which host.**"

**On the shape cache** (`add_beast_movements`):
> "A circuit is a property of the ground a group stands on, not of the group... What cannot be shared is the naming: `_camp` stamps the group's own uid into every camp id, and `_assign_days` runs on the group's own speed."

And on the `_MISSING` sentinel: "a sentinel the shape cache can tell apart from a builder that legitimately returned `None`, which is how a stranded group is reported and is very much a result worth remembering."

**On the share** (`add_beast_movements`):
> "Wildlife is dense -- thousands of sites -- so every eligible one becoming a routed group makes the block enormous for little added meaning. **The share is a deterministic thinning over an ordered list, not a cap, so it stays proportional to the ground rather than to whatever happened to be placed first.**"

**On keeping a camp when solitary** (`build`):
> "It keeps a base camp, so the encounter index still places it: **a player can meet the solitary phase, just not a swarm on the march.**"

**On why `travel_cost` is not the road cost function:**
> "that refuses a river without a bridge and caps grade at the road limit, both of which describe an engineered route rather than a walked one. **A band fords, and takes ground a road would not.** Water is still impassable -- these are not boats -- and the forage term makes a herd prefer the green way round."

**On camp rest** (`CAMP_REST`, `CAMP_REST_CEILING`):
> "A winter camp is where the year is survived and a lair is where a raider lives; a market stall and a village just robbed are both places you leave. **Splitting the year evenly across camps put a bandit at the hamlet it had raided for four months, which is not a raid.**"
> "Some stops are events, not seasons, however much of the year is left to divide. A raid and a market day are measured in days; a share of the leftover year is the wrong unit for them and produced **a band sitting three weeks in a hamlet it had just robbed**."

**On the seasonal inversion** (`terrain_nomad_routes` docstring):
> "the seasonal phase **inverts across the equator**, so a southern clan winters on the opposite half of the year. **Getting that wrong is invisible on a world whose land happens to sit in one hemisphere**, which is why it is pinned by a test rather than by inspection."

**On why encounters exists at all** (`terrain_encounters` docstring):
> "This exists because travelling groups are the one kind of world content a consumer cannot find by looking at a place. **A city is at a node; a caravan is at a different node every week.** Answering 'what might a party run into around here' means either scanning every group's every leg, or having this."
> "It is an index, not a generator. There are no quests here, no objectives, no rewards and no difficulty -- those belong to whatever eventually consumes it, and **inventing them now would be guessing at a contract that does not exist yet**."
> "Occupancy is recorded at **camps**, where a group dwells, not along legs... **Where a group is camped is the thing you cannot derive.**"

**On domain, class and airborne** (`terrain_encounters`, `MEDIUM_DOMAIN` and the conformance record):
> "`shore` is a land medium -- `terrain_nests.suitability` refuses a shore species every water cell and then asks for a coast -- so a puffin colony is on the beach and a traveller walks to it. **Flying is not a domain: a roost is still somewhere you can stand, and folding it in would make `domain == 'land'` stop meaning 'reachable on foot'.**"
> "A band is people. `animal` and `monster` are the words `wildlife` and `beast_nests` already use for this, and the entry takes them unchanged rather than **minting a third spelling of the same distinction**."
> "**The domain is resolved from the species' authored medium rather than from the terrain under the camp.** A group's camps move; what it can live in does not, and a marine group camped at a coastal node is still not something a party walks up to."
> "On a reference world this splits **2,778 entries into 1,586 ocean, 1,184 land and 8 lake**, so a land query drops 57% of the index in one field test."

**On the class fallback** (`_beast_entry`):
> "Falling back to the group's own `kind` rather than to a default keeps a species the catalogue no longer names from **silently reading as an animal**."

**On the threat histogram** (`encounters.limits`, and the conformance record):
> "Static sites are not indexed at all: only movers are, so the lairs in `beast_nests` -- **the most dangerous things in the world, and the ones that do not move** -- are absent, and the `threat_tier` histogram therefore describes travelling groups and not the world's danger profile. **A world holding tier-five nests can and does produce an index with no tier-five entry.** Whether to index static sites is a design decision and not a field addition."

**On the block's instability, which is the point of it** (`beast_movements.limits` and `docs/beast-movement.md`):
> "This block is therefore **not stable from year to year, which is the point of it**. Circuits are otherwise static."
> "**Cache it against `world_clock.day`, never against the world's identity.**"

**On the irruption factor standing in for weather** (`beast_movements.limits`):
> "The irruption year is a draw rather than a simulated drought: `seasonal_environment` carries **twelve climatological months that are the same in every year**, so there is no modelled weather for a year to depart from, and the factor stands in for one."

**On what is not modelled** (`beast_movements.limits`):
> "A group is one site rather than a modelled population: nothing breeds, starves, is eaten or merges with another group, and **a herd does not shrink when a follower attaches to it**. Followers pick the nearest host and never change host."

**On the conformance test fixture being hand-built, and why** (`docs/conformance/encounter-index.md`):
> "against a hand-built block rather than a generated world. **That is the point of it.** The claim is about a *split*, and a generated world is a bad fixture for a split: it is 98.5% one disposition and roughly three quarters one domain, so **a share-based assertion passes for almost any mapping, including a constant one.** The fixture carries one group per medium and per class in equal numbers, and every assertion is per entry against the catalogue, so a constant domain, a swapped one, or a lake folded into the ocean fails on a named uid."

**On why the bounded-search test runs at size 33 and not 17** (`Sim/tests/test_beast_movements.py`):
> "At size 17 a search from a routed group's cell settles **2.0 cells on average** -- mostly the group's own -- so a bounded search agrees with an unbounded one for want of anywhere to disagree, and every perturbation of it still passes. **At 33 the same sample settles 13, and four separate ways of breaking the search all go red.**"

And on the sample of start nodes:
> "**Not simply the lowest two dozen node indices.** That was the first shape of this and it proved nothing: **node 0 is the north pole** and the cells around it on this world are all isolated, so every one of those searches settled exactly one cell -- itself -- and passed against any search that can return its own start."

**The undead rule** (`docs/beast-movement.md`):
> "**An undead with a grave nests at it. An undead without one wanders.** ... This is why a barrow wight was added to the catalogue in the same pass as five wandering dead: **the contrast belongs in the data, not only in this paragraph.**"

## Notes for the C++ implementation

Read `include/fwg/grid.hpp`, `include/fwg/random.hpp`, `include/fwg/world.hpp` and `include/fwg/config.hpp` before starting. This subsystem is stage 16; nothing below it exists yet, so most of what it reads has to be ported first.

### What already maps cleanly

- **`sphere_grid`.** `fwg::sphere_grid(n, radius)` already gives exactly what `terrain_erosion.sphere_grid` gives: one node per distinct cell, `point_x`/`point_z`, `areas`, and `neighbours[i]` as `{index, distance}` in ascending index order. Hold the `shared_ptr` for the whole pass — the cache evicts. `fwg::direction(x,z,n)` matches the Python bit for bit in structure, including `y == sin(lat)` and the exact `(0,±1,0)` poles.
- **`child_seed`.** `fwg::child_seed(master, domain, variation, "tectonics-v1")` is the same 4-byte big-endian SHA-256 prefix. Use it for the two domains `"beast-movement-v1"` and `"beast-year"`. **Do not invent a fourth derivation** — the header says so explicitly. Feed the 32-bit result into `Rng` (or `child_seed64` if you want a 64-bit stream); either is fine under the clean-rewrite ruling, but pick one and record it.
- **`Rng::uniform(low, high)`** is the direct analogue of `random.uniform`. Numbers will differ from Python. That is expected and allowed.
- **`Grid`** is not what this pass reads. It reads *node-indexed* arrays. Build `std::vector<double>` of length `sphere.node_count()` by sampling each layer `Grid` at `(point_x[i], point_z[i])`. Do **not** iterate the n×n grid — the seam column and the repeated pole rows would produce duplicate nodes.

### What has no direct equivalent

**1. `habitat_cells` does not exist yet.** It is a fat per-cell dict with derived fields (`medium`, `depth`, `dryness`, `coast`, `mountain`, `plains`, `wetland`, `cold`). This pass needs only a handful. Port it as a struct-of-arrays, not an array-of-structs:

```cpp
struct GroundArrays {
    std::vector<Vec3>   dir;          // unit sphere
    std::vector<double> height;       // m
    std::vector<double> forage;       // max(food_potential, natural_food_potential), UNCLAMPED
    std::vector<double> slope;        // DEGREES
    std::vector<double> temperature;  // degrees C
    std::vector<double> tpi;          // m, signed
    std::vector<std::uint8_t> water;  // medium != "land"
    std::vector<int> x, z;
};
```
The Python `fields.get(k, 0.)` defaults matter: a missing layer reads as 0.0, not as an error.

**2. Python dicts with insertion order reaching JSON.** `counts` and `by_month` are ordered containers in the output. `std::map<std::string,...>` gives lexicographic order, which is **wrong for `by_node`** (`"10"` before `"9"`) and wrong for `counts`. Use a `std::vector<std::pair<K,V>>` plus a lookup index, or emit through `fwg::json` in an explicit order you control.

**3. `heapq` tuple ordering.** `heapq` pops the minimum `(distance, node)` *tuple*, so equal distances break on the lower node index. Use:
```cpp
struct QEntry { double d; int node; };
// min-heap: greater-than comparator on (d, node) lexicographically
auto cmp = [](const QEntry& a, const QEntry& b) {
    return a.d != b.d ? a.d > b.d : a.node > b.node;
};
std::priority_queue<QEntry, std::vector<QEntry>, decltype(cmp)> q(cmp);
```
A queue keyed on distance alone will diverge on equal-cost paths.

**4. `(score, -node)` maxima.** Write them out as an explicit lexicographic compare; do not fold into a single double.
```cpp
inline bool better(double s, int node, double bs, int bnode) {
    return s != bs ? s > bs : node < bnode;   // -node maximised == node minimised
}
```

**5. `sorted(dirty)`** — `std::sort` on a `std::vector<int>`. Keep it: the irruption mean depends on ascending node order.

**6. `bisect.bisect_left(lats, phi)`** → `std::lower_bound(lats.begin(), lats.end(), phi) - lats.begin()`. Identical semantics.

**7. Python `%` on days.** `(x - held) % 360` is non-negative in Python for positive modulus. C++ `%` is truncating and can be negative. Write a helper:
```cpp
inline int day_mod(long long v, int m) { long long r = v % m; return int(r < 0 ? r + m : r); }
```
`grid.cpp` already has a `floor_mod` for the longitude wrap — reuse or mirror it.

**8. `int(round(x))` is banker's rounding** (half to even) in Python 3, not `std::round` (half away from zero). `int(round(0.5)) == 0` in Python, `std::round(0.5) == 1.0`. This appears twice in `_assign_days` (`travel` and `total_travel`). Under the clean-rewrite ruling you may use `std::llround`; **record which you chose**, because it shifts day windows by one on exact halves and a reviewer comparing against the Python will otherwise chase it.

**9. `int(spare * share / weight)`** truncates toward zero. `static_cast<int>` on a non-negative double does the same. Fine.

**10. Float summation.** The Python is explicit about this in three places. `_irruption` and `travel_cost` use plain accumulation *so a native port does not have to carry a compensated accumulator*; `terrain_nests.distance` deliberately keeps `sum()` because CPython's Neumaier compensation is a ULP better and moved worlds on some seeds. **Under the clean-rewrite ruling, use plain `double` accumulation everywhere** — but be aware `round_length_m` uses a builtin `sum()` (compensated) while `total` in `_irruption` does not. Numbers will differ from Python by ULPs; that is sanctioned.

**11. `math.acos(max(-1., min(1., dot)))`** — `distance(a, b, radius)`. `Vec3` presumably has a dot; clamp before `std::acos` or you get NaN.

**12. The shape and route caches.** Keys are tuples. Use:
```cpp
struct ShapeKey { int movement; int node; int role_id; };   // role_id = -1 for non-followers
struct RouteKey { int src, dst; };
```
with `std::unordered_map` and a hand-written hash, or `std::map` with `operator<`. **Cache lookup order does not reach output** (the searcher fully resets its scratch), so either is safe. But the shape cache must distinguish "not built" from "built and returned nothing" — model it as `std::optional<std::optional<Shape>>` or a small tagged struct, matching the `_MISSING` sentinel. Getting this wrong makes a stranded group rebuild its (failing) search once per group, which is the quadratic the Python removed.

**13. The three-valued builder return.** `_seasonal`/`_drift`/`_irruption` return one of: `None` (stranded), `SOLITARY` (irruptive only), or `(camps, order)`. Model as:
```cpp
enum class ShapeOutcome { Stranded, Solitary, Built };
struct Shape { ShapeOutcome outcome; std::vector<std::pair<int,CampKind>> spec;
               std::vector<std::tuple<int,int,Branch>> order; };
```
`_follow` returns a *pair*: `(host_uid, shape_or_null)`. Keep the host uid even when the shape is null.

**14. Strings as tie-breaks.** `haunts.sort()` compares ruin uid strings; `_follow`'s mark compares host uid strings; `everything.sort` compares group uid strings. Python compares by Unicode code point; all these ids are ASCII (`[a-z0-9-]`), so `std::string::operator<` matches. **But `everything.sort(key=uid)` sorts node numbers as text** — preserve that if you want the published order, or make a deliberate, recorded decision to sort numerically instead. `encounters` occupancy indices depend on it.

**15. `fwg::World` needs new typed blocks.** Add something like:

```cpp
struct Camp { std::string id; int node; Vec3 direction; std::string kind;
              std::optional<int> arrive_day, depart_day; };
struct Leg  { std::string from, to; std::vector<int> nodes; double length_m;
              std::optional<int> depart_day, arrive_day; std::string branch; };
struct BeastGroup {
    std::string uid;          // the key field — NOT `id`
    std::string species_id, name, movement, family, kind, source;
    std::optional<std::string> host_uid, role;
    int tier, node, x, z, size;
    Vec3 direction;
    double speed_m_per_day, column_length_m;
    std::string disposition, route_status;
    std::vector<Camp> camps; std::vector<Leg> legs;
    std::optional<double> round_length_m;   // routed only
};
struct BeastMovements { int version = 2; std::vector<BeastGroup> groups;
    std::vector<std::pair<std::string,int>> counts;   // ordered!
    int considered, skipped; double share;
    int routed, stranded, solitary; long long year; double year_forage_factor;
    std::string method, limits; };
```
and the encounter analogue. **`world.hpp` currently has no typed blocks beyond plates and the water report**, so this is where the block-carrying half of `World` gets built out. `NomadBand` must exist first with `uid`, `node`, `camps`, `legs`, `classification`, `disposition`, `size`, `carries`, `seeks`, `god_id`, `speed_m_per_day`, `column_length_m`.

**16. The genesis seed must survive the stepped config.** `year_forage_factor` reads `result['config']['seed']`, not `cfg.seed`. `fwg::World` already keeps `config` (design space, as supplied) separate from `effective`. Make sure the *time-advance* path, when it exists, steps a copy and leaves `world.config.seed` alone — otherwise the whole irruption trigger silently becomes per-month noise. This is the single most likely porting defect in this module.

**17. `lru_cache(maxsize=1)` on `profiles()`, `roles()` and `_species_facts()`** → a function-local `static const` built on first call. The library is single-threaded; if that ever changes, `static` initialisation in C++11 and later is already thread-safe.

**18. `profiles()` validation raises on load.** Port the five checks (duplicate id, unknown medium, empty/non-positive weights, bad class/tier, animal without a known role, monster without `biome_weights`) — they are the catalogue's only integrity gate and the library has no JSON schema validator.

**19. No dependencies.** `fwg::json` is the writer. There is no `heapq`, no `bisect` (use `<algorithm>`), no `lru_cache`.

**20. `perf_counter` → `std::chrono::steady_clock`**, into `world.timing_ms`.

### Performance shape to preserve

The Python is 3.60 s at size 128 and 2.6% of the run, with an in-situ exponent of 1.10, only because of the four structures named in the module docstring. A naive C++ port that rebuilds a full-width distance array per search, or compares every follower against every host, will be *faster than the naive Python* and still superlinear — which is the trap, because it will look fine at size 33 and project badly. Port all four: priced edges, the bounded/targeted search with persistent scratch, the per-(movement, node[, role]) shape cache, and the latitude walk.

### Things that are easy to get subtly wrong

- Not clamping `forage` in the *scorers* while clamping it in `travel_cost` — the Python does exactly this, and the asymmetry is real.
- Applying the follower bias before the `span > reach` cut. The cut is on the **raw** span.
- Adding `slack` to the `span > reach` test. The Python adds slack to the two *walk-stopping* floors only, never to the per-candidate cut.
- Forgetting that `within` in `_irruption` includes `start`.
- Letting a follower's anchor use a host camp's row instead of the follower's own row in `_assign_days`. The Python uses `cells[band['node']]['z']`, i.e. the follower's own start.
- Treating `_beast_entry`'s `group['kind']` as the nest kind. It is the class.

## Open questions

Genuinely ambiguous or apparently defective; a porter must decide each one.

1. **`HOST_BIAS` has a dead row and a missing one.** `bird_of_prey` is authored but **no follower in the catalogue has that role** (followers are `pack_predator` 8, `pursuit_predator` 7, `scavenger` 1, `None` 1). Meanwhile `pursuit_predator` — the second-largest follower group, 7 of 17 — is **not** in the table and falls through to `DEFAULT_BIAS` (`nomad .85`). The docstring says "hunters attach to herds", and `pack_predator` is biased 0.70 toward beasts to say so, but `pursuit_predator` gets the generic 0.85. Whether that is intentional (pursuit predators are less herd-specific) or an authoring oversight is not recorded anywhere. Port the table verbatim and flag it; do not "fix" it silently.

2. **`_irruption` treats a zero-forage isolated cell as SOLITARY, not stranded.** With `within == [start]` and `forage[start] == 0`, the test is `0 * factor >= 0.18 * 0`, i.e. `0 >= 0`, which is true. A group on barren, isolated ground therefore reports "sparse and not marching this year" rather than "nowhere to march to". Both readings are defensible; the code picks the first by arithmetic accident rather than by decision.

3. **A follower's calendar is anchored on its own latitude, not its route's.** `_assign_days` uses `cells[group['node']]['z']` for the midsummer anchor when there is no winter camp, and a follower never has a winter camp (the kind remap folds `winter` into `base`). So a follower that attaches to a host a thousand kilometres away, in the other hemisphere, anchors its year on where the follower was *placed*. No comment addresses this. It may be intended (the follower's own seasons) or it may be a consequence of the remap.

4. **The follower reach is great-circle, the builders' reaches are walking cost.** `_follow` cuts on `distance(here, host_dir, radius) > reach`, which ignores water and grade entirely, while `_seasonal`, `_irruption` and `_drift` all bound by walked cost. A follower can therefore attach to a host on another continent 3 spacings away as the crow flies. Nothing says whether that is deliberate (a follower derives, it does not walk to its host) or an oversight.

5. **A stranded or solitary group is indexed with a null day window and appears in no month.** `encounters.limits` says "a null day window means 'here, always' rather than 'here, never'", but `_window_months` returns `[]`, so such a group is in `by_node` and in **zero** `by_month` buckets — the index encodes exactly the opposite of what `limits` claims. On seed 42 size 33 that is 1 184 of 2 760 groups, 42.9%. `CONTENT-BEAST-MOVEMENTS-STRANDED` step 4 asks for this to be marked or excluded, and `board/done/CONTENT-ENCOUNTERS-ARE-FISH` shipped v2 without it. **Decide before porting**: either carry `route_status` onto the entry, or give a null window all twelve months, or keep the current behaviour and correct the `limits` prose. The record says it is "a one-field change to `_beast_entry` once the module settles."

6. **A third of groups strand, and no reason is recorded.** Re-confirmed 2026-09-21: 933 of 2 760 stranded (33.8%). The caveat on the card matters: the measured world had `resolved_octaves: 0` of 5 — no surface relief at all — and a seasonal round between high and low ground needs elevation to have somewhere to go, so a high strand rate may be the correct output rather than a routing failure. **Re-measure at a size where all five octaves resolve before treating it as a defect.** But a stranded group carrying its reason (the way `key_locations.diagnostics` does) is asked for either way, and `solitary` is the precedent for the distinction.

7. **`terrain_beast_movement.py` has no conformance record.** It is on the `uncovered` allowlist in `docs/conformance/coverage.json` under `board/in-progress/CONFORMANCE-DOCS.md`. `docs/beast-movement.md` is a design document, not a conformance record, and `tools/docs_check.py` can never fail on an uncovered Sim module. So **this specification and the module docstring are the only descriptions of the movement pass's algorithm that exist.** `terrain_encounters.py` does have one (`docs/conformance/encounter-index.md`) and it **agrees with the code** — I found no disagreement, and its "does not establish" section already names the stranded-entry gap in item 5.

8. **The year read on a tick is off by up to the span length.** Documented and measured (5250 against a clock ending 5252 on a two-year advance), and explicitly out of scope for this module: "Closing it means the executor handing each cadence step its own day, which is a change to `terrain_time` and belongs to whoever owns that module." A C++ port that builds the time executor fresh could close it — but doing so changes which year the block describes, so it is a decision, not a cleanup.

9. **`round_length_m` uses a compensated `sum()` while `_irruption`'s mean uses a plain loop**, and the two comments give opposite rationales in the same repository. Under the clean-rewrite ruling both become plain `double` accumulation and the distinction vanishes. Record that it vanished, because someone comparing outputs will find the last bits move.

10. **Neither module forks a process pool.** The `ICARUS_CITY_WORKERS` trap applies only to `city_planner.py` and `hamlet_planner.py`; I confirmed both of these modules are wholly serial. Nothing to preserve here.

## Addenda from the audit

The reader above wrote the specification; a second reader went back to the source
to find what it had left out or got wrong. Both are kept: the omissions are
content the implementer needs, and the corrections say where the specification
above is not to be trusted.

### Corrections

- **Claimed:** In WRITES: "`method` | str | Fixed prose, quoted in `reasoning_to_preserve`" and "`limits` | str | Fixed prose, quoted in `reasoning_to_preserve`" (for both blocks).
  **Actually:** False for three of the four strings. Neither `method` string appears anywhere in the specification, in any form. `beast_movements.limits` is quoted in fragments totalling roughly five of its seven sentences (the two about the tick-year and about solitary-vs-stranded are absent). `encounters.limits` is quoted in fragments, but several of those quotes are actually taken from the module DOCSTRING, which is different text from the `limits` field (e.g. the docstring's "It is an index, not a generator. There are no quests here, no objectives, no rewards and no difficulty" versus the field's "An index, not a quest generator: no objectives, stakes, rewards or difficulty"). Since these are literal output fields a consumer reads, a porter following the spec would emit different prose or omit the fields. All four verbatim strings are supplied in the `missing` list.

- **Claimed:** C++ note 8: "`int(round(x))` is banker's rounding (half to even) in Python 3 ... This appears twice in `_assign_days` (`travel` and `total_travel`)."
  **Actually:** It appears three times on the path this pass runs, not twice. The third is `terrain_nomad_routes.midsummer_day`: `int(round((phase % 1.) * DAYS_PER_YEAR)) % DAYS_PER_YEAR`. That one is benign in practice — `phase % 1.` is exactly 0.5 or exactly 0.0, so the product is exactly 180.0 or 0.0 and no half-case arises — but a porter auditing the two call sites the spec names will not look at `midsummer_day`, and the note's whole purpose is to make the rounding choice explicit and recorded at every site.

- **Claimed:** A12: "The follower's camps are the host's camps, **one step behind**: the follower arrives where the host has been rather than where it is going."
  **Actually:** No code implements a lag. `_follow` copies `host['camps']` in the host's own order and performs only a kind remap (`'terminal' if camp['kind'] in ('station','terminal') else 'base'`); there is no rotation, no offset, and no day shift anywhere in the follower path. "One step behind" is narrative in the Python's own comment, and the spec reprints it inside the ALGORITHM section as though it were mechanism. The spec's own pseudocode immediately above is correct and contradicts the prose, so a careful porter is safe — but a porter reading the prose could insert a rotation, and once the Python is gone there is nothing to check it against. The only sense in which a follower runs behind its host is incidental: `_assign_days` anchors the follower on `midsummer_day` at the FOLLOWER's own start row (a follower never has a `winter` camp, because the remap folds `winter` into `base`), so its calendar is unrelated to the host's — which is open question 3, and which the spec states correctly elsewhere.

### Missing — blocking

**The derivation of `fields['medium']`, and therefore travel_cost's water mask — the predicate the whole priced graph rests on**

`terrain_nests.habitat_cells` computes, per cell, from the layer dict `f = {name: layer[z][x]}`:

    water = f.get('water_type', 0)
    wet   = f.get('moisture', 0)
    medium = ('marine'     if water == 1
              else 'freshwater' if water and f.get('salinity', 0) < .2
              else 'salt_lake'  if water
              else 'land')

So `travel_cost`'s mask `water[i] = 1 if fields['medium'] != 'land' else 0` is exactly `water_type != 0`: salinity and the marine/freshwater/salt_lake distinction never affect passability, they only affect what the cell is called. Without this rule the spec's A1/A2 cannot be implemented at all — the spec names `medium` and its four values but gives no way to compute it.

The seven other derived fields habitat_cells writes into the same dict (none of them read by either module under audit, listed so a porter does not have to invent them or wonder what is missing):

    depth    = f.get('water_depth', 0)
    dryness  = 1 - clamp(wet)
    coast    = f.get('maritime', 0)
    mountain = clamp(f.get('slope', 0) / 25)
    plains   = clamp(1 - f.get('slope', 0) / 15) * (1 - abs(wet - .45))
    wetland  = max(float(bool(water)), f.get('river', 0), wet * exp(-f.get('slope', 0) / 8))
    cold     = clamp((10 - f.get('temperature', 10)) / 25)

where `clamp(v) = max(0., min(1., v))`. The cell record itself is `{node: i, x, z, area_km2: areas[i]/1e6, biome: int(f.get('natural_biome', f.get('biome', 0))), direction: direction(x,z,n), fields: f}` — note `direction`, `z` and `fields` are the three the movement pass touches, and `fields` is the layer dict *plus* the eight derived keys above, so a missing layer really does read as 0.0 through `.get`.

### Missing — important

**The four literal prose strings both blocks emit as output fields. The spec's WRITES table says they are "quoted in `reasoning_to_preserve`"; the two `method` strings are not in the document at all, and two sentences of `beast_movements.limits` are missing**

`result['beast_movements']['method']`, verbatim:
"Every placed site whose species declares a movement class other than nester becomes a travelling group. Herds walk a seasonal round between high summer pasture and sheltered winter ground through a calving camp; the unhoused dead circuit between ruins; and followers derive their circuit from a host's, taking nomad bands and herds alike. A swarm marches only in a year that is actually bad for it: this year's forage factor -- one draw for the whole world, keyed on the genesis seed and the absolute year so a monthly tick cannot re-roll it -- is applied to the group's own cell and compared against the mean of what a march can reach, so marginal ground in a lean year erupts and the same ground in an ordinary year stays solitary. Groups share the shape of nomads.groups so one consumer reads both."

`result['beast_movements']['limits']` — the two sentences the spec does not carry (they sit between the irruption-draw sentence and the "not stable from year to year" sentence, both of which the spec does quote):
"On a tick the year is read from world_clock as it stands when this pass runs, and a time advance writes the advanced clock after its cadence loop, so a span crossing several years builds this block for the year that span started in." and "A swarm that is not swarming carries route_status \"solitary\", which is its normal state; a group whose route could not be built is still reported as stranded."

`result['encounters']['method']`, verbatim:
"One entry per travelling group and one occupancy record per camp it holds, indexed by month and by terrain node so a consumer answers \"what is near here, and when\" in a lookup rather than a scan over every leg of every group. Both nomad bands and travelling creatures are indexed through the same shape. Threat is on the creature-tier rubric, one to five; a band takes its tier from its classification, because what a band will do to you is a property of what kind of band it is. Each entry also carries what it is rather than only which species it is: `domain` is land, ocean or lake on the same word key_locations uses, `class` is animal, monster or people on the word wildlife and beast_nests use, and `airborne` marks a flyer. A shore species is `land`, because a shore medium is refused every water cell and a traveller can walk to the colony; flying is not a domain, so `domain == \"land\"` means reachable on foot and keeps meaning it."

`result['encounters']['limits']`, verbatim (the spec quotes three of its clauses from the docstring instead, which is different text):
"An index, not a quest generator: no objectives, stakes, rewards or difficulty, because the consumer that would define them does not exist yet. Occupancy covers camps only -- a group in transit is derivable from its leg node path and its speed, and indexing every node of every leg would multiply the block by the length of the walking. Stranded groups appear with their start camp and no schedule, so a null day window means \"here, always\" rather than \"here, never\". Nothing here accounts for two groups meeting each other. Static sites are not indexed at all: only movers are, so the lairs in beast_nests -- the most dangerous things in the world, and the ones that do not move -- are absent, and the threat_tier histogram therefore describes travelling groups and not the world's danger profile. A world holding tier-five nests can and does produce an index with no tier-five entry. Whether to index static sites is a design decision and not a field addition."

### Missing — minor

**The formula for `distance(a, b, radius)`, which is every `length_m`, every `round_length_m` and the follower's `span`**

`terrain_nests.distance(a, b, radius) = radius * math.acos(max(-1., min(1., sum(x*y for x, y in zip(a, b)))))` — great-circle metres between two unit directions, dot product clamped to [-1, 1] before acos. The spec gives this only obliquely, as `math.acos(max(-1., min(1., dot)))` inside C++ note 11, and never states the `radius *` factor or that this is the same function `sphere_grid` used to build its neighbour spans. Its docstring is also the one place in the repo that rules the OPPOSITE way on float summation from `_irruption`: "Keep sum(): CPython 3.12 sums floats with Neumaier compensation, so an explicit a[0]*b[0]+a[1]*b[1]+a[2]*b[2] is a different (less accurate) value by about one ULP and silently changes generated worlds on some seeds." The spec does carry that quote, but under `terrain_nests.distance` without connecting it to the fact that every leg length in this block is accumulated from calls to it.

**`travel_cost`'s `lookup` table, used unexplained in the spec's A1 pseudocode**

`lookup = {p: i for i, p in enumerate(points)}` — a map from the `(x, z)` grid coordinate pair to the sphere_grid node index, built once per `travel_cost` call. The diagonal-corner test `water[lookup[(xi, zj)]] or water[lookup[(xj, zi)]]` indexes it with the two mixed corner coordinates. `n = cfg.size` in the same closure is what the `zi not in (0, n-1)` pole exemption tests against.

**`trace`'s behaviour when start == end**

`trace(parent, start, end)` is `path = [end]; while path[-1] != start: j = parent[path[-1]]; if j < 0: return []; path.append(j); return list(reversed(path))`. When `start == end` the loop never runs and it returns a ONE-element list `[end]`, not `[]` and not a two-element path. In this module `build` already guards that case with `if src['node'] == dst['node']: continue`, so it never fires here — but a porter who implements `trace` to return `[]` on a self-path, or who drops the `build` guard, changes behaviour. The spec says only "returning [] the moment a parent is negative, otherwise the reversed list".

**Group-record field insertion order also reaches the JSON, not just the top-level block order**

The spec flags insertion order as load-bearing for the block's top-level dict and for `counts`, but not for the group dict. The literal construction order is: uid, species_id, name, movement, family, tier, kind, source, host_uid, node, x, z, direction, size, speed_m_per_day, column_length_m, disposition, role, camps, legs, route_status — and `round_length_m` is assigned afterwards, so on a routed group it appears LAST, after route_status. The spec's WRITES table happens to list them in exactly this order, so a porter who follows the table is safe by accident; nothing tells them the order matters. Same for the camp record (id, node, direction, kind, arrive_day, depart_day), the leg record (from, to, nodes, length_m, depart_day, arrive_day, branch), the entry record and the occupancy record (entry, node, camp_id, camp_kind, from_day, to_day, months).

**The first `_host_index` build is dead work — the index in `ctx` at setup time is never read by any builder**

`ctx['host_index'] = _host_index(hosts, dirs)` is built during ctx construction, but `_follow` is the only reader and every follower is in `deferred`, which is built only after `ctx['host_index']` is replaced with `_host_index(hosts + [g for g, _ in groups if g['legs']], dirs)`. So the nomads-only index is constructed and discarded unused on every run. A C++ port can skip it entirely with no behavioural change; the spec presents it in A4 as if it were live, which will cost a porter a sort over the band list for nothing (and may mislead someone into thinking early-built followers see a nomads-only host set).

**`salt_lake` is not an authorable species medium, so that row of `MEDIUM_DOMAIN` is dead for `_species_facts` — the same class of finding the spec correctly flagged for `bird_of_prey`**

`terrain_nests.MEDIA = ('land', 'shore', 'marine', 'freshwater')` and `profiles()` raises on any profile whose `medium` is outside it. `salt_lake` exists only as a *derived cell* medium inside `habitat_cells` (a water cell with salinity >= .2 that is not `water_type == 1`). Therefore `MEDIUM_DOMAIN['salt_lake'] -> 'lake'` can never be reached through `_species_facts()`, which maps only profile media; the live rows are marine->ocean, freshwater->lake, land->land, shore->land, plus the `land` fallback. The catalogue's 680 profiles are land 538, marine 79, freshwater 42, shore 21 — no salt_lake, ever. The 8 `lake` entries in the measured 2,778-entry reference split all come from freshwater species. Port the table verbatim, but know that row is unreachable, exactly as `bird_of_prey` is.
