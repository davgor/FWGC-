# Port specification: 09-humans-seasons

Extracted from the Python `FantasyWorldGenerator` at commit `b67c99b` before that
repository was retired, then audited against the same source by a second reader.

**Source modules**

- `Sim/icarus_sim/terrain_humans.py`
- `Sim/icarus_sim/terrain_seasons.py`

**Estimated C++ size:** roughly 1650 lines, excluding tests.

## Audit: IMPLEMENTABLE

The specification is unusually faithful. I re-derived every formula in both files against the source and the algorithm, constants, block/field lists, guards, orderings and prose strings are correct essentially everywhere: farming_potential (the 0.7 flood term, the 0.75 maintenance factor, the freshwater -1 branch), the 100/120/0.025/40/250 hamlet constants, the fortress demand and scoring arithmetic, exchange_food's 4000/5000/0.2/1e-9, seasonal_harvest's 20/0.4/0.05/8/1.5/12 and the (month-1)%12 lag, simulate_food's full accounting order and 1e-12, and add_seasonal_food's 6/3/0.01, 0.005/0.035/0.08/(t+5)/35, 15/(t+15)/20/0.1 and 1e-8. The 13-profile trait table, the entity ordering, the school orders, heartland's 17 magic multipliers and every cited test value all check out verbatim against the live registry. A competent C++ programmer could build a faithful port from it. What they could NOT get right without the source: (a) elf's food_magic_biome_multipliers are the one deviation from the table the spec calls "representative", so elf forests would silently be halved; (b) access_cost carries two different units depending on whether a row was generated or player-founded, and the delivery term divides both by support_reach — the spec never says so; (c) four fortress_demand fields are listed in WRITES with no formula anywhere, so they must be guessed; (d) the natural-biome id->name map is absent, leaving food_biome_multipliers' "13"/"3" keys opaque and the catalogue's biome-major ordering only half-specified. There are also four factually wrong statements, one of which (the recipe-3 tie-break commentary) asserts an outcome the code does not produce. None of these block the pipeline, but (a), (b) and the tie-break claim are exactly the kind of thing that silently builds something different.

## Purpose

These two modules build the human hinterland — everything that stands between a city and the raw terrain — and then stress-test whether it can feed the cities through a year.

`terrain_humans.add_humans` (stage 9, block `humans`) answers: which ground does each city control, how much exportable food and material does that ground yield, where do the rural hamlets and route-defence fortresses stand, which cities share a culture, and can annual trade over the road network close each city's food gap. It writes the rural settlement layer that `hamlet_planner`, `castle_planner`, `npc_roster`, `hero_generator`, `terrain_wars`, `terrain_magic.add_colleges` and `terrain_society` all build on.

`terrain_seasons.add_seasonal_food` (block `seasonal_food`) takes the annual, mean-climate food numbers `add_humans` produced and re-expresses them as a 12-month harvest curve with a one-month lag and a hemisphere-correct thermal season, then runs four repeated climatic years of monthly stock accounting with storage, spoilage, winter-limited road capacity, transport loss and material budgets. Year 4 is reported as a stress-test observation, explicitly *not* a claim of equilibrium.

Both modules are emphatic in their own prose that the numbers are **relative exportable potential units, not people, not tonnes per year, and not population capacity**. `add_humans` measures the *surplus a rural cell can export after its own subsistence*, which is why a value of zero is normal and not a failure. `terrain_seasons` exports `simulate_food` and `seasonal_harvest` as a shared library: `terrain_society.add_world_society` re-runs the identical simulator over a larger route set (ground + sea + air) to produce the authoritative `world_economy`, so these two functions must be written as reusable free functions in C++, not as private helpers of the seasons stage.

## Where it sits in the pipeline

**Before `add_humans` must exist:**
- `effective_config.globe_radius` (physical metres) and `config.size` (n).
- Layers, all n x n: `height`, `water_type`, `slope`, `flood_risk`, `moisture`, `temperature`, `resource_potential`, `tpi`, `rain_river`, `freshwater_distance`, `biome`. Optional: `magic_hazard`, `magic_risk_<profile_id>` (one per entity civilization), `biome_variant`.
- `result['settlements']['sites']` — the city list (stage 8).
- `result['roads']['routes']` — the minimum spanning forest of shortest terrain-cost city-to-city routes (stage 8). **`add_humans` returns the world unchanged if `cfg.phase < 9` or if `result['roads']` is missing/falsy.** A `roads` dict with an empty `routes` list is truthy, so a world with zero cities still runs the pass and emits empty lists.
- `result['terrain']['magical_biomes']` (the biome-variant catalogue) if `biome_variant` exists.
- `result['warnings']` and `result['timing_ms']` must already exist as a list and a dict with a `'total'` key.

**Call order in the generation pipeline** (`terrain_area.apply_world_scale`, and identically in `terrain_history.civilization`):

```
add_settlements  ->  derive_institutions  ->  add_humans  ->  add_colleges  ->  add_seasonal_food  ->  add_world_society  ->  add_nests
```

`add_seasonal_food` returns the world unchanged if `result['humans']` is missing or falsy. It has no phase guard of its own — it relies on `add_humans` having refused first.

**After, and depending on it:**
- `terrain_magic.add_colleges` reads `humans.hamlets` + `humans.fortresses` (for the occupied-node spacing set) and `humans.cores[i].culture_id`.
- `terrain_society.add_world_society` reads `humans.cores[].culture_id`, the occupied-node set (cities + hamlets + fortresses), deep-copies `seasonal_food.models`, adds fishery harvest into those copies, **appends coastal port rows to `humans.hamlets`** and port ids to `cores[].hamlet_ids`, pairs `roads.routes` positionally with `seasonal_food.routes`, and re-runs `simulate_food` to produce `world_economy`.
- `terrain_wars` computes a city's strength as `population_estimate + FORT_STRENGTH * len(core.fortress_ids)`, and `veteran_wars(site) = len(site.war_history or ())` feeds back into fortress demand the next time `add_humans` runs. This is a genuine loop across an age boundary: wars raise fort demand, forts raise war strength.
- `hamlet_planner` joins on `humans.hamlets[].id`; `castle_planner` joins on `humans.fortresses[].id`; `npc_roster` and `hero_generator` key on `uid`.
- `terrain_history` refuses to advance an age unless `world['humans']['version'] == 9`.
- `terrain_time` can re-run `add_seasonal_food` alone on a seasonal cadence (`CADENCE_PASSES['seasonal_food']`), which is why its trailing `warnings.append` is deduplicated by `terrain_time._dedupe_warnings`.

**Ordering hazard across the pipeline:** on that cadence re-run, `humans.hamlets` already contains `terrain_society`'s coastal ports, so the re-computed `seasonal_food` harvest is *not* the same as the one generation produced (ports carry `delivered_food` from fisheries and would be folded into the farm harvest curve a second time, through `seasonal_harvest` rather than as raw monthly fish). Port rows do carry `x`, `z`, `core_id` and `delivered_food`, so nothing crashes — it silently produces a different number. Decide deliberately in C++ whether the seasonal re-run filters hamlets to non-port rows.

## Inputs

### Config fields (`fwg::Config`)

| Field | Type | Range | Meaning in these modules |
|---|---|---|---|
| `phase` | int | 1..16 | `< 9` -> `add_humans` is a no-op |
| `size` | int (n) | 3..1025 | grid edge |
| `seed` | uint32 | 0..2^32-1 | only for culture id derivation |
| `world_recipe` | int | 0 or 3 | 3 selects per-civilization traversal rules everywhere below |
| `population_profile` | string | a registry key | default civilization; `'mixed'` means per-site profiles |
| `auto_parameters` | int | 0/1 | 1 makes the hamlet limit follow each city's rural residents |
| `support_reach` | double m | 100..100000, default 1000 | **post-scaling**: the caller multiplies the authored 1000 m by `circumference_m / 11148.96 m` |
| `culture_link_cost` | double m | 1..100000, default 1800 | **post-scaling**, same factor |
| `hamlets_per_core` | int | 0..8, default 3 | ceiling on generated hamlets per city |
| `fortress_count` | int | 0..1024, default 1024 | a **ceiling only**, never a target |
| `urban_food_demand` | double | 0..10000, default 10 | fallback city demand when a site has no `urban_population_estimate` |
| `human_adaptation` | double | 0..1, default 0.7 | irrigation effort when `population_profile != 'mixed'` |
| `human_magic_limit` | double | 0..1, default 0.45 | hazard above which a node/edge is impassable |
| `road_max_grade` | double | 0.01..1, default 0.35 | grade above which an edge is impassable |
| `bridge_cost` | double m | 0..10000, default 200 | flat penalty on an edge touching a river cell |
| `world_options` | JSON | | only `seasonality` (default 1.0, range 0..2) is read, and only when `world_recipe` is truthy |

`effective_config.globe_radius` is the **physical** radius in metres (design radius x `world_scale`). Everything in these modules measures in physical metres.

### Layers (all n x n `Grid`, sampled at the `sphere_grid` node points)

| Layer | Units / range | Used for |
|---|---|---|
| `height` | metres | `height_m` on every rural row; road grade |
| `water_type` | 0 dry, 1 ocean, 2 lake | impassability; yield is forced to zero on water |
| `slope` | degrees, >= 0 | farming potential, work-slope gates, fortress value |
| `flood_risk` | 0..1 | farming penalty, road cost, fortress value |
| `moisture` | 0..1 | farming potential, seasonal amplitude |
| `temperature` | degrees C, roughly -60..45 | farming potential, seasonal curve |
| `resource_potential` | 0..1 | material yield, resource-hamlet score |
| `tpi` | metres above/below the neighbourhood mean; signed | fortress elevation bonus |
| `rain_river` | 0/1 | bridge penalty on a road edge |
| `freshwater_distance` | metres, **-1 means no freshwater found** | irrigation access |
| `biome` | small integer natural-biome id | per-profile food multiplier |
| `biome_variant` (optional) | flat index into `terrain.magical_biomes`, -1 if none | exact magical-state food multiplier |
| `magic_hazard` (optional) | 0..1 | crop loss and impassability; **absent -> treated as all zeros** |
| `magic_risk_<profile_id>` (optional) | 0..1 | per-civilization view of hazard; today every one of these is a verbatim copy of `magic_hazard`, but do not rely on that |

### `settlements.sites[]` fields read

`id` (int, equals the list index), `node` (int, index into the sphere-grid node list), `x`, `z` (grid coords), `population_profile` (registry key), `moisture` (0..1), `temperature_c` (C), `urban_population_estimate` (int, **optional key** — presence is tested with `in`), `rural_population_estimate` (int, read with a default of 0), `war_history` (list, optional).

### `roads.routes[]` fields read

`from`, `to` (city indices), `cost` (double, terrain-cost units, roughly metres), `nodes` (list of node indices, the full path), `length_m` (double, sum of great-circle edge lengths), `river_crossings` (list of `[i, j]` node pairs).

### Population-profile traits read (from `civilizations.json`)

`food_slope_comfort` (degrees, 10..24), `food_temperature_ideal` (C, 4..28), `food_temperature_tolerance` (C, 22..30), `food_moisture_ideal` (0..1 exclusive of 0, 0.4..0.85), `water_reach` (metres, 400..650), `irrigation` (0..1, 0.4..0.9), `food_demand` (0..10000, 8 or 10 today), `work_slope_limit` (degrees, 10..30), `road_grade_limit` (0.01..1, 0.35..0.55), `mutation_limit` (0..1, 0.45..0.6), `food_biome_multipliers` (map of decimal-string natural-biome id -> 0..2), `food_magic_biome_multipliers` (map of variant id string -> 0..2), `id` (the registry key, injected by the validator).

### Previous-pass state read

`result['humans']` from a prior run, if any — only rows with `founded_by == 'player'` in `hamlets` and `fortresses` are read. Everything else in the old block is discarded.

## Outputs

### `result['layers']` — six new n x n grids, all built by `node_grid`

`node_grid(values, points, n)` scatters one value per node into an n x n grid, then sets row 0 to `values[0]` repeated n times, row n-1 to `values[-1]` repeated, and every row's last column to that row's first. This is exactly `fwg::SphereGrid::to_grid`.

| Layer | Value | Range |
|---|---|---|
| `food_potential` | irrigated exportable food potential per node | >= 0, typically 0..1.2 |
| `natural_food_potential` | unirrigated exportable food potential | >= 0, <= `food_potential` |
| `irrigation_benefit` | `food_potential - natural_food_potential` | >= 0 |
| `culture_region` | culture group root **city index**, or -1 outside every catchment | integer-valued double |
| `civilization_region` | index into `civilization_ids()`, or -1 | integer-valued double |
| `hamlet_catchment` | index into `humans.hamlets` (the generated+adopted list at allocation time), or -1 | integer-valued double |

### `result['humans']` — the block

```
version: 9  (constant; bumped again to 9 inside the `if 'magic' in result` branch, a no-op)
population_profile: cfg.population_profile
cores: [ ... ]         one per city, in city order
hamlets: [ ... ]       generated first (city-major), then player-adopted
fortresses: [ ... ]    generated in descending defence score, then player-adopted
cultures: [ ... ]      one per distinct group root, ascending root index
shipments: [ ... ]     annual food trades
fortress_demand: { ... }
method, culture_method, defence_method: prose strings (see reasoning_to_preserve)
```

**`cores[]` — keyed on `site_id`.**
- `site_id` int, equals the city index.
- `population_profile` string.
- `culture_id` string.
- `hamlet_ids` list of the ordinal `id` strings of hamlets whose `core_id` is this city.
- `fortress_ids` likewise. (`terrain_society` later appends port ids to `hamlet_ids`.)
- `food_demand` double: `profile.food_demand * urban_population_estimate / 100` when the site has that key, else `cfg.urban_food_demand`.
- `food_supply` double: sum of members' `delivered_food`.
- `food_deficit` double: **written twice.** Initially `max(0, cfg.urban_food_demand - supply)`, then unconditionally overwritten by `exchange_food` with `max(0, food_demand - food_supply - food_imports + food_exports)`. The first write is dead. Do not port it.
- `material_supply` double: sum of members' `delivered_materials`.
- Added by `exchange_food`: `food_imports`, `food_exports`, `trade_material_cost`, all >= 0.

**`hamlets[]` and `fortresses[]` — two ids, and they mean different things.**
- `id`: **ordinal**, `"hamlet-<k>"` / `"fortress-<k>"` where k is the position in the list this pass is building. `hamlet_plans` and `castle_plans` join on exactly this. It renumbers whenever the rural layer is rebuilt, which includes every age advance.
- `uid`: `"hamlet-node-<node>"` / `"fortress-node-<node>"`. This is the handle that survives an age boundary, because the terrain node does not move. `npc_roster`, `hero_generator` and the player-founding route (`terrain_settlement_api._rural_row`) all mint and key on this exact spelling.
- `kind`: `'hamlet'` or `'fortress'`.
- `node` int, `x`/`z` ints, `core_id` int (the owning **city index**, i.e. `owner[node]`), `population_profile` string, `culture_id` string, `height_m` double.
- `access_cost` double: terrain-cost distance from the owning city, in road-cost units.
- `access_nodes` list of node indices: `[node, ..., city_node]`, walked back up the Dijkstra parent chain. The first element is always the site's own node and the last is always the owning city's node. Every consecutive pair is a traversable road edge.
- `reason` string.
- Hamlet-only: `role` (`'farming'` or `'resource'`), `irrigation_benefit`, `worked_area_km2`, `delivered_food`, `delivered_materials`.
- Fortress-only: `defence_score` double, `protected_route_node` int.
- Player rows additionally carry whatever of `founded_by`, `founded_age`, `name` they had.

**`cultures[]` — keyed on `index`.**
`index` (the root city index), `id` (`"<profile>-<8 lowercase hex>"`), `city_ids` (ascending list of member city indices), `population_profile`, `civilization_id`, `architecture_style_id` — all three of the last are the same string, the root city's profile key.

**`shipments[]`** (annual, from `exchange_food`): `from`, `to` (city indices), `sent`, `delivered`, `lost`, `material_cost` (doubles), `city_path` (list of city indices from seller to buyer).

**`fortress_demand`**: `proposed`, `city_ceiling`, `requested_ceiling`, `limit` (ints), `player_founded`, `veteran_demand`, `veteran_cities` (ints), `road_length_m`, `support_reach` (doubles), `crossings` (int).

### `result['seasonal_food']`

```
version: 1, years: 4, reported_year: 4
cities:  [ ... ]   one per city
models:  [ ... ]   one per city
routes:  [ ... ]   one per road route, positionally aligned with roads.routes
months:  [ ... ]   48 entries
method, limits: prose strings
```

**`models[]` — keyed on `site_id`.** `site_id` int; `harvest` 12 doubles (food units delivered that month); `demand` double (monthly, `core.food_demand / 12`); `storage` double (capacity in food units); `spoilage` double, 0.005..0.08 (fraction of opening stock lost per month); `transport_budget` double (renewable material units per month).

**`routes[]`** — `from`, `to` (city indices), `capacity` (12 doubles), `efficiency` (0..1), `price` (material units per delivered food unit).

**`months[]`** (48 records): `year` 1..4, `month` 1..12, `cities` (per-city records), `shipments`, `opening`, `harvest`, `closing`, `transit_loss`, `consumed`, `spoiled`, `overflow` (world totals). Per-city record: `opening`, `harvest`, `spoiled`, `imports`, `exports`, `material_spent`, `demand`, `consumed`, `shortage`, `overflow`, `closing`.

**`cities[]` — keyed on `site_id`** (which is the list index, not a separate id space): `annual_demand`, `annual_harvest`, `annual_shortage`, `food_coverage` (0..1), `lean_months` (list of 1..12), `reserve_change`, `ending_reserve` (doubles), `passes_final_year` (bool), `urban_food_equivalent` (int).

### Other writes
- `result['warnings']`: `add_humans` appends its three prose strings in the order method, culture_method, defence_method. `add_seasonal_food` appends only `limits` (not `method`).
- `result['timing_ms']['human_hinterlands']` and `['seasonal_food']`, each also added into `timing_ms['total']`.

## Algorithm

Everything below runs on the sphere node graph. `points, areas, graph = sphere_grid(n, radius)`: node 0 is the north pole, node count-1 is the south pole, interior nodes are `(x, z)` for `z in 1..n-2`, `x in 0..n-2`, enumerated z-major then x-major. `areas[i]` is the node's spherical cell area in m^2 (a pole node carries the whole ring). `graph[i]` is a list of `(neighbour_index, great_circle_distance_m)` **sorted ascending by neighbour index**. `values(key)` means `[layers[key][z][x] for (x, z) in points]`.

---

# PART A — `add_humans`

## A0. Guard
If `cfg.phase < 9` or `result['roads']` is absent/falsy, return unchanged.

## A1. Shared helpers

### `road_cost_function(points, water, height, flood, river, cfg, hazard) -> cost(i, j, d)`
(Defined in `terrain_settlements`; reproduced here because it is the whole traversal model.)

```
cost(i, j, d):
    if water[i] or water[j] or d <= 0:            return IMPASSABLE
    if hazard and max(hazard[i], hazard[j]) > cfg.human_magic_limit: return IMPASSABLE
    (xi, zi) = points[i]; (xj, zj) = points[j]
    # a diagonal cannot slip between two water-filled corner cells
    if xi != xj and zi != zj and zi not in (0, n-1) and zj not in (0, n-1):
        c1 = node_at(xi, zj); c2 = node_at(xj, zi)
        if water[c1] or water[c2]:                return IMPASSABLE
        if hazard and max(hazard[c1], hazard[c2]) > cfg.human_magic_limit: return IMPASSABLE
    grade = |height[j] - height[i]| / d
    if grade > cfg.road_max_grade:                return IMPASSABLE
    return d * (1 + 12*grade^2 + 0.5*(flood[i] + flood[j]))
           + (cfg.bridge_cost if river[i] or river[j] else 0)
```
`node_at(x, z)` maps `(0, z)` for polar z and `(x mod (n-1), z)` otherwise to a node index.

### `allocate_access(graph, seeds, cost, limit = +inf) -> (distance, owner, parent)`
Multi-source Dijkstra. One terrain-cost owner per node; deterministic ties; edges the cost function rejects are not traversed (so no water shortcuts).

```
distance[*] = +inf; owner[*] = -1; parent[*] = -1
for (node, label) in seeds:                  # seeds in the caller's given order
    distance[node] = 0.0; owner[node] = label; push (0.0, label, node)
while heap not empty:
    (value, label, i) = pop min            # lexicographic on (value, label, node)
    if value != distance[i] or label != owner[i]: continue      # stale entry
    for (j, d) in graph[i]:                # ascending neighbour index
        edge = cost(i, j, d)
        if edge is IMPASSABLE: continue
        candidate = value + edge
        if candidate <= limit and (candidate < distance[j]
                                   or (candidate == distance[j] and label < owner[j])):
            distance[j] = candidate; owner[j] = label; parent[j] = i
            push (candidate, label, j)
```
The `label < owner[j]` clause is the tie-break: on an exactly equal cost the lower label wins, and it re-relaxes so the parent chain is rewritten too. `limit` is an exact cutoff, not an approximation — edge costs are non-negative, so every prefix of an admissible path is admissible; nodes outside it stay at infinity, so a bounded result is only meaningful inside the bound.

### `shortest_paths(graph, start, cost, targets = null, limit = +inf) -> (distances, parent)`
Ordinary single-source Dijkstra, `distances[start] = 0`, relax only when `candidate <= limit and candidate < distances[j]`. If `targets` is given, stop as soon as every target has been popped.

### `farming_potential(slope, temp, wet, flood, freshwater_distance, adaptation, profile) -> (natural, irrigated)`
```
terrain    = exp(-(slope / profile.food_slope_comfort)^2)
           * max(0, 1 - |temp - profile.food_temperature_ideal|
                        / profile.food_temperature_tolerance)
           * (1 - 0.7 * flood)
natural    = terrain * max(0, 1 - |wet - profile.food_moisture_ideal|
                                  / profile.food_moisture_ideal)
access     = exp(-freshwater_distance / profile.water_reach)  if freshwater_distance >= 0
             else 0                       # -1 means no mapped freshwater -> no irrigation
irrigation = adaptation * access
           * max(0, (profile.food_moisture_ideal - wet) / profile.food_moisture_ideal)
irrigated  = natural + 0.75 * terrain * irrigation
return (natural, irrigated)
```
The 0.75 is maintenance: irrigation costs, so only three quarters of the raw gain becomes exportable surplus. Note `irrigation` is zero when the cell is already wetter than ideal, so irrigation never reduces a wet cell and never applies without a freshwater source.

### `culture_groups(count, links, threshold) -> [root per index]`
Union-find, union-by-lower-index, no path compression, **no ordering dependence on the link list** because the merge is always "point the larger root at the smaller":
```
groups[i] = i for all i
root(i): while groups[i] != i: i = groups[i]; return i
for link in links:                      # order irrelevant to the result
    if link.cost <= threshold:
        a = root(link.from); b = root(link.to)
        groups[max(a,b)] = min(a,b)
return [root(i) for i in 0..count-1]
```
The canonical group id is therefore always the **lowest member city index**.

## A2. Setup
```
n = cfg.size; radius = effective_config.globe_radius
points, areas, graph = sphere_grid(n, radius)
sites = settlements.sites
height, water, slope, flood, wet, temp, resource, tpi, river, fresh = values(...)
hazard = values('magic_hazard') if present else zeros
cost = road_cost_function(points, water, height, flood, river, cfg, hazard)
```

## A3. Access allocation — two variants

**Variant 1 (always executed first):**
`distance, owner, parent = allocate_access(graph, [(site.node, site.id) for site in sites], cost)` — **unbounded** (limit = +inf).

**Variant 2 (`cfg.world_recipe` truthy — recipe 3):** the whole result above is thrown away and rebuilt. Build one cost function per city that uses *that city's own* traversal rules:
```
for site in sites:
    p    = get_profile(site.population_profile)
    risk = values('magic_risk_' + site.population_profile) if that layer exists else hazard
    individual_costs[site.id] = road_cost_function(points, water, height, flood, river,
                                     cfg with human_magic_limit := p.mutation_limit,
                                              road_max_grade   := p.road_grade_limit,
                                     risk)
```
then run a multi-source Dijkstra where **the edge cost depends on the owner label being carried**, so a frontier keeps its owner's traversal rules and a parent path can never jump ownership:
```
distance[*] = +inf; owner[*] = -1; parent[*] = -1
for site in sites: i = site.node; owner[i] = site.id; distance[i] = 0; push (0, site.id, i)
while heap:
    (d, o, i) = pop min
    if d != distance[i] or o != owner[i]: continue
    for (j, length) in graph[i]:
        edge = individual_costs[o](i, j, length)
        if edge is IMPASSABLE: continue
        candidate = d + edge
        if candidate < distance[j] and candidate <= cfg.support_reach:
            distance[j] = candidate; owner[j] = o; parent[j] = i; push (candidate, o, j)
```
Note the two differences from `allocate_access`: the test is **strict** `<` with no owner tie-break, and the search is **bounded at `cfg.support_reach`** so everything outside a catchment stays at infinity. Both are load-bearing.

The wasted Variant-1 search on recipe 3 is real: it runs an unbounded multi-source Dijkstra over the whole land graph and discards every array. Skip it in C++.

## A4. Node profiles and the per-node hazard view
```
site_profiles[k] = get_profile(sites[k].population_profile)         # indexed by city id
node_profiles[i] = site_profiles[owner[i]] if owner[i] >= 0 else default_profile
   # default_profile = get_profile(cfg.population_profile)
if cfg.world_recipe:
    hazard[i] = layer('magic_risk_' + node_profiles[i].id)[z][x]
                 falling back to 'magic_hazard', falling back to 0
```
This reassignment happens **after** the cost functions were built, so it does not change traversal — the closures captured the earlier list. It only changes the crop-loss term in A7.

## A5. Cultures
```
links   = [r for r in roads.routes
           if sites[r.from].population_profile == sites[r.to].population_profile]
groups  = culture_groups(len(sites), links, cfg.culture_link_cost)
roots   = sorted(set(groups))                # ascending
culture_ids[g] = sites[g].population_profile + '-' + hex8(child_seed(cfg.seed, 'culture-' + str(g)))
cultures = [ {index: g, id: culture_ids[g],
              city_ids: [i for i,v in enumerate(groups) if v == g],   # ascending
              population_profile / civilization_id / architecture_style_id
                  : sites[g].population_profile}
             for g in roots ]
```
Only same-civilization roads can link cultures, so two peoples sharing a road never share a culture. `child_seed(master, domain, variation=0)` is `first 4 bytes big-endian of sha256("tectonics-v1:{master}:{domain}:{variation}")`; formatted `:08x`.

## A6. Region layers
```
region[i]             = groups[owner[i]] if owner[i] >= 0 and distance[i] <= support_reach else -1
entity_index          = {profile_key: position} over civilization_ids()   # entity-kind keys only
civilization_region[i]= entity_index[sites[owner[i]].population_profile]
                        if owner[i] >= 0 and distance[i] <= support_reach else -1
layers['civilization_region'] = node_grid(civilization_region, points, n)
```
(`culture_region` is written later from `region`, in the batch at A14.)

## A7. Per-node food and material yields
```
adaptation(i) = node_profiles[i].irrigation  if cfg.population_profile == 'mixed'
                else cfg.human_adaptation
for each node i:
    if water[i]: (natural_i, food_i) = (0, 0)
    else:        (natural_i, food_i) = farming_potential(slope[i], temp[i], wet[i],
                                          flood[i], fresh[i], adaptation(i), node_profiles[i])
# then, for every node including water ones:
m = biome_food_multiplier(node_profiles[i], biome[i], cell_variant(i))
natural[i] *= m * (1 - hazard[i])
food[i]    *= m * (1 - hazard[i])
```
`cell_variant(i)` = `terrain.magical_biomes[biome_variant[z][x]].id` when `biome_variant` exists and the value is >= 0, otherwise null.

`biome_food_multiplier(profile, core_id, variant_id)`:
- if `variant_id` is in the **hidden-school** variant set (schools `blood`, `void`, `rot`, `eldritch`): return `profile.food_magic_biome_multipliers[variant_id]` if authored, else **0.0**. Corrupted ground feeds nobody, and that is a rule rather than 52 authored opinions per people.
- otherwise: return `profile.food_magic_biome_multipliers[variant_id]` if authored, else `profile.food_biome_multipliers[str(core_id)]` if authored, else **1**.

**Trap:** `food_biome_multipliers` is keyed by the *decimal string* of the natural biome id. If the biome layer is stored as a double, `str(3.0)` is `"3.0"` and every lookup silently misses, degrading every profile to multiplier 1. In C++ store the natural biome id as an integer and index by integer.

## A8. Player-founded rural rows
```
previous = result.get('humans') or {}
player_rural = {'hamlet': [], 'fortress': []}
for (key, kind) in (('hamlets','hamlet'), ('fortresses','fortress')):
    for row in previous.get(key) or []:
        if row.get('founded_by') == 'player': player_rural[kind].append(row)
sort each list ascending by row['node']
player_nodes = set of all their nodes
occupied = [site.node for site in sites] + sorted(player_nodes)
```
A rural site the player founded is an instruction, not a proposal: it is not re-derived and is not subject to the counts and ceilings below. Its node joins the occupied set **before** the generated passes run, so nothing is placed on top of it.

```
vectors[i] = unit direction of points[i]
separated(i, minimum) = for every j in occupied:
                            radius * acos(clamp(dot(vectors[i], vectors[j]), -1, 1)) >= minimum
```
(`occupied` grows as sites are accepted; `separated` reads the current contents.)

```
record(i, kind, number, reason):
    core = owner[i]; (x, z) = points[i]
    path = [i]; while parent[path.back()] >= 0: path.push_back(parent[path.back()])
    return { id: f'{kind}-{number}', uid: f'{kind}-node-{i}', kind, node: i, x, z,
             core_id: core, population_profile: sites[core].population_profile,
             culture_id: culture_ids[groups[core]], height_m: height[i],
             access_cost: distance[i], access_nodes: path, reason }
```

```
adopt(row, kind, number):
    i = row.node
    if owner[i] >= 0:
        reseated = record(i, kind, number, row.get('reason', 'Founded by the player.'))
    else:
        reseated = copy of row with id := f'{kind}-{number}'
    for key in (uid, founded_by, founded_age, name, population_profile, role,
                irrigation_benefit, worked_area_km2, delivered_food,
                delivered_materials, defence_score, protected_route_node):
        if key in row: reseated[key] = row[key]
    return reseated
```
The ordinal `id` is reissued because it is a position in a list this pass rebuilt; the access path, owning city and culture are recomputed because they describe the world as it is now. The `uid`, the node and the player's own choices are not re-derived — that would move a settlement under its owner. A node outside every catchment keeps the core it was founded against rather than being dropped. **The caller's block is never mutated** — `adopt` copies.

## A9. Hamlet placement (generated)
```
hamlets = []
for site in sites:                                  # city order
    profile  = site_profiles[site.id]
    eligible = [i for i in nodes if owner[i] == site.id
                                 and 100 <= distance[i] <= cfg.support_reach
                                 and not water[i]
                                 and slope[i] < profile.work_slope_limit]     # ascending i
    hamlet_limit = min(cfg.hamlets_per_core, ceil(site.rural_population_estimate / 40))
                   if cfg.auto_parameters else cfg.hamlets_per_core
    for k in 0 .. hamlet_limit-1:
        role  = 'resource' if k % 3 == 2 else 'farming'
        score(i) = (food[i] if role=='farming' else resource[i]*(1-flood[i]))
                   * exp(-distance[i] / cfg.support_reach)
        for i in eligible sorted by (-score(i), i):
            if score(i) < 0.025: break              # nothing left worth settling
            if separated(i, 120):
                h = record(i, 'hamlet', len(hamlets),
                           'Reachable ' + role + ' potential with low transport cost')
                h.role = role
                h.irrigation_benefit = food[i] - natural[i]
                h.worked_area_km2 = h.delivered_food = h.delivered_materials = 0
                hamlets.append(h); occupied.append(i); break
```
So the roles cycle farming, farming, resource, farming, farming, resource, ... per city. The `eligible` list is *not* re-filtered between k iterations; the 120 m spacing check against the growing `occupied` list is what stops a node being taken twice. The `100 <=` lower bound keeps a hamlet off the city's own doorstep.

## A10. Player hamlet adoption
```
for row in player_rural['hamlet']:                  # already sorted by node
    adopted = adopt(row, 'hamlet', len(hamlets))
    i = row.node
    adopted.irrigation_benefit = food[i] - natural[i]
    adopted.worked_area_km2 = adopted.delivered_food = adopted.delivered_materials = 0
    hamlets.append(adopted)
```
Adopted **before** the catchment allocation below, so a player hamlet works land exactly as a generated one does. The four accumulators start at zero for the same reason a generated hamlet's do — the allocation fills them — and the irrigation benefit is a read of this pass's ground rather than the previous pass's, because it describes the cell and not the settlement.

## A11. Catchment allocation and delivery
Each productive cell belongs to at most one hamlet and cannot cross city catchments.
```
farm_cost(i, j, d):
    if owner[i] != owner[j] or distance[j] > cfg.support_reach: return IMPASSABLE
    return individual_costs[owner[i]](i, j, d) if cfg.world_recipe and owner[i] >= 0
           else cost(i, j, d)

_, farm_owner, _ = allocate_access(graph,
                       [(h.node, k) for k, h in enumerate(hamlets)],   # label = hamlet index
                       farm_cost, limit = 250.0)

city_nodes = {site.node for site in sites}
for i, k in enumerate(farm_owner):
    if k < 0 or i in city_nodes or slope[i] >= node_profiles[i].work_slope_limit: continue
    h        = hamlets[k]
    area     = areas[i] / 1e6                                  # km^2
    delivery = exp(-h.access_cost / cfg.support_reach)
    h.worked_area_km2      += area
    h.delivered_food       += 100 * area * food[i]     * delivery * (1 if h.role=='farming'  else 0.25)
    h.delivered_materials  += 100 * area * resource[i] * delivery * (1 if h.role=='resource' else 0.25)
```
The 100 is the explicit provisional food calibration used throughout this generator ("100 residents per fully productive km2"). A hamlet still works land outside its own speciality at a quarter rate. The city's own node is excluded so a city does not farm the ground it stands on.

## A12. Fortress placement
Route junctions/crossings and elevated surroundings suggest defensive sites.
```
route_neighbors = {}                # node -> set of adjacent route nodes; INSERTION ORDERED
crossings       = set()             # nodes touched by any river crossing
crossing_edges  = set()             # sorted (i, j) pairs, for counting distinct crossings
for road in roads.routes:
    for (i, j) in consecutive pairs of road.nodes:
        route_neighbors[i].add(j); route_neighbors[j].add(i)
    for edge in road.river_crossings:
        crossings.update(edge); crossing_edges.add(tuple(sorted(edge)))
```
Demand. The road network asks for its own defence: one watch per support reach of road plus one per distinct crossing. A civilization cannot raise more forts than it has cities to raise them, and `cfg.fortress_count` is a caller's ceiling over both — it has always been a maximum, never a target or a guarantee. A city that has fought a war wants more watching its roads, so its wars raise both what the world asks for and how much it is allowed; a world that has never been to war keeps exactly the road-and-city answer.
```
veteran[site.id] = min(3, len(site.war_history or []))
veteran_demand   = sum(veteran.values())
road_length      = sum(road.length_m for road in roads.routes)
proposed         = floor(road_length / cfg.support_reach) + len(crossing_edges) + veteran_demand
fortress_limit   = min(cfg.fortress_count, proposed, len(sites) + veteran_demand)
```
Scoring. For each route node, score the node itself and each of its graph neighbours:
```
strategic = {}                                      # node -> (value, route_node)
for (node, links) in route_neighbors:               # INSERTION ORDER
    base = 1 + min(2, max(0, len(links) - 2)) + (1 if node in crossings else 0)
    for (i, d) in [(node, 0.0)] + graph[node]:
        if water[i] or slope[i] >= node_profiles[i].work_slope_limit
           or owner[i] < 0 or distance[i] > cfg.support_reach: continue
        if i != node and cost(node, i, d) is IMPASSABLE: continue    # always the GLOBAL cost fn
        value = base
              + clamp(tpi[i] / 30, -0.5, 1)         # elevated surroundings, bounded both ways
              - 0.5 * flood[i]
              - slope[i] / 40
              - d / 500                             # prefer the junction itself over a neighbour
              + 0.5 * veteran.get(owner[i], 0)      # ground belonging to a city that has fought
                                                    # outranks equally defensible ground that has not
        if value > strategic[i].value: strategic[i] = (value, node)
```
Selection:
```
forts = []
for (i, (score, route_node)) in strategic sorted by (-score, node_index):
    if len(forts) >= fortress_limit: break
    if separated(i, 200):
        f = record(i, 'fortress', len(forts),
                   'Nearby city road; junction/crossing importance and elevated surroundings')
        f.defence_score = score; f.protected_route_node = route_node
        forts.append(f); occupied.append(i)
```
A player keep is not a route-defence proposal, so it is appended **after** the ceiling rather than competing for a place under it:
```
for row in player_rural['fortress']: forts.append(adopt(row, 'fortress', len(forts)))
```

## A13. Cores
```
for site in sites:
    members = [h for h in hamlets if h.core_id == site.id]     # in hamlet-list order
    supply  = sum(h.delivered_food for h in members)
    cores.append({ site_id: site.id, population_profile: ..., culture_id: culture_ids[groups[site.id]],
                   hamlet_ids: [h.id for h in members],
                   fortress_ids: [f.id for f in forts if f.core_id == site.id],
                   food_demand: (site_profiles[site.id].food_demand * site.urban_population_estimate / 100)
                                if site has 'urban_population_estimate' else cfg.urban_food_demand,
                   food_supply: supply,
                   food_deficit: max(0, cfg.urban_food_demand - supply),   # DEAD, overwritten below
                   material_supply: sum(h.delivered_materials for h in members) })
```

## A14. `exchange_food(cores, roads)` — annual trade
Finite local surplus traded over existing roads, with loss and material cost.
```
graph2 = adjacency over cities: for each road, graph2[from] += (to, road.cost)
                                             graph2[to]   += (from, road.cost)
for c in cores: c.food_imports = c.food_exports = c.trade_material_cost = 0
shipments = []
for buyer in cores sorted ascending by (food_supply - food_demand, site_id):   # neediest first
    needed = max(0, buyer.food_demand - buyer.food_supply); b = buyer.site_id
    distances, parents = shortest_paths(graph2, b, cost = the edge weight itself)
    for a in city indices sorted ascending by (distances[a], a):               # nearest first
        if needed <= 1e-9: break
        if a == b or distances[a] is infinite: continue
        seller     = cores[a]
        surplus    = max(0, seller.food_supply - seller.food_demand - seller.food_exports)
        efficiency = exp(-distances[a] / 4000)
        price      = 0.2 + distances[a] / 5000
        budget     = max(0, buyer.material_supply - buyer.trade_material_cost)
        received   = min(needed, surplus * efficiency, budget / price)
        if received <= 1e-9: continue
        sent = received / efficiency
        seller.food_exports       += sent
        buyer.food_imports        += received
        buyer.trade_material_cost += received * price
        needed                    -= received
        path = [a]; while path.back() != b: path.push_back(parents[path.back()])
        shipments.append({from: a, to: b, sent, delivered: received, lost: sent - received,
                          material_cost: received * price, city_path: path})
for c in cores:
    c.food_deficit = max(0, c.food_demand - c.food_supply - c.food_imports + c.food_exports)
return shipments
```
`efficiency` is transport loss (drops to ~78% at 1000 cost units, ~37% at 4000); `price` is what a delivered food unit costs in material units. `surplus` already subtracts what a seller has committed to earlier buyers, so a seller cannot sell the same grain twice. Remaining shortages stay visible in `food_deficit` rather than being papered over.

## A15. Layer writes and the block
```
layers['food_potential']         = node_grid(food, points, n)
layers['natural_food_potential'] = node_grid(natural, points, n)
layers['irrigation_benefit']     = node_grid(food[i] - natural[i], points, n)
layers['culture_region']         = node_grid(region, points, n)
layers['hamlet_catchment']       = node_grid(farm_owner, points, n)
```
Assemble `result['humans']` exactly as described in **writes**, then, if the world has a `magic` block, append to `method`:
`' Mutation and the selected population biome food multipliers reduce crop surplus. Rural access cannot cross unsafe magic.'`
Append all three prose strings to `warnings` in order. Record `timing_ms['human_hinterlands']` and add it into `timing_ms['total']`.

---

# PART B — `terrain_seasons`

## B1. `seasonal_harvest(annual_food, wet, latitude, mean_temp, tolerance, ideal = 18, seasonality = 1.0) -> 12 doubles`
One-month harvest lag; thermal seasonality reverses across the equator. Annual food is the existing mean-climate estimate, not a guaranteed yield — seasonal growing suitability can change the year's actual production.
```
amplitude = 20 * sin(radians(latitude)) * (1 - 0.4 * wet) * seasonality
baseline  = max(0.05, exp(-((mean_temp - ideal) / tolerance)^2))
for month in 0..11:
    temp    = mean_temp + amplitude * cos(2*pi*(month - 6) / 12)
    thermal = exp(-((temp - ideal)/tolerance)^2) * clamp(temp / 8, 0, 1)
    growth[month] = min(1.5, thermal / baseline)
return [ annual_food * growth[(month - 1) mod 12] / 12  for month in 0..11 ]
```
- Month index 0 is January. `cos(2*pi*(month-6)/12)` peaks at month index 6 (July). Northern latitude -> positive amplitude -> warm in July; southern -> negative amplitude -> warm in January. The test `test_hemispheres_reverse_seasons` pins `north[i] == south[(i+6) % 12]`.
- `clamp(temp/8, 0, 1)` is a hard cold cutoff: nothing grows at or below 0 C, and growth is linearly suppressed up to 8 C.
- `growth` is normalised against the annual-mean suitability (`baseline`), so a city whose mean climate is already ideal sees `growth` near 1 all year. The `max(0.05, ...)` floor on `baseline` stops a city in a hostile mean climate from getting an unbounded multiplier; `min(1.5, ...)` caps the per-month bonus.
- Dividing by 12 makes the 12 months sum to roughly `annual_food` when growth averages 1.
- The `(month - 1) mod 12` index is the **one-month harvest lag**: the food that arrives in month m grew in month m-1.

## B2. `simulate_food(cities, roads, years = 4) -> {'months': [...]}`
Monthly conservation with shared edge throughput and no same-month re-export. Stores start empty; consumption precedes storage overflow. Trade is directly between road neighbours and costs the buyer renewable monthly material units.

Each `cities[i]` needs `harvest` (12 doubles), `demand`, `storage`, `spoilage`, and optionally `transport_budget` (default +inf). Each `roads[e]` needs `from`, `to`, `capacity` (12 doubles), `efficiency`, and optionally `price` (default 1.0).
```
stocks[*] = 0; history = []
for step in 0 .. years*12 - 1:
    month   = step mod 12
    opening = copy of stocks
    spoiled[i] = stocks[i] * cities[i].spoilage
    harvest[i] = cities[i].harvest[month]
    stocks[i]  = stocks[i] - spoiled[i] + harvest[i]

    # what a donor is willing to part with: it keeps one month of demand,
    # plus a reserve of min(demand, storage) -- a donor does not reserve food it cannot store
    exportable[i] = max(0, stocks[i] - demand_i - min(demand_i, storage_i))
    budgets[i]    = cities[i].transport_budget (or +inf)     # RENEWED every month
    imports[*] = exports[*] = spent[*] = 0
    shipments = []; loss = 0

    for route_index, edge in enumerate(roads):               # given order
        a = edge.from; b = edge.to; remaining = edge.capacity[month]
        for (seller, buyer) in ((a, b), (b, a)):             # both directions, SHARED remaining
            efficiency = edge.efficiency; price = edge.price or 1
            if efficiency <= 0: continue
            need = max(0, cities[buyer].demand - stocks[buyer])
            sent = min(exportable[seller], remaining,
                       need / efficiency,
                       budgets[buyer] / price / efficiency)
            if sent <= 1e-12: continue
            received = sent * efficiency; charge = received * price
            stocks[seller] -= sent;  stocks[buyer] += received
            exportable[seller] -= sent; remaining -= sent
            budgets[buyer] -= charge; spent[buyer] += charge
            exports[seller] += sent;  imports[buyer] += received
            loss += sent - received
            shipments.append({from: seller, to: buyer, route_index, sent,
                              delivered: received, lost: sent - received, material_cost: charge})

    records = []
    for i, c in enumerate(cities):
        consumed = min(c.demand, stocks[i]); stocks[i] -= consumed
        overflow = max(0, stocks[i] - c.storage); stocks[i] -= overflow
        records.append({opening: opening[i], harvest: harvest[i], spoiled: spoiled[i],
                        imports: imports[i], exports: exports[i], material_spent: spent[i],
                        demand: c.demand, consumed,
                        shortage: max(0, c.demand - consumed),
                        overflow, closing: stocks[i]})
    history.append({year: step//12 + 1, month: month + 1, cities: records, shipments,
                    opening: sum(opening), harvest: sum(harvest), closing: sum(stocks),
                    transit_loss: loss,
                    consumed: sum, spoiled: sum, overflow: sum})     # of the per-city records
return {months: history}
```
Invariants the tests pin, which a C++ port must preserve:
- Global: `opening + harvest == closing + consumed + spoiled + overflow + transit_loss`.
- Per city: `opening + harvest + imports == closing + consumed + spoiled + overflow + exports`.
- Per edge per month, total `sent` across **both** directions never exceeds `capacity[month]` — `remaining` is shared, which is what "shared edge throughput" means.
- `efficiency <= 0` produces no shipment at all (not a divide by zero).
- `material_spent` never exceeds `transport_budget`.
- **No same-month re-export:** goods bought on edge e are added to `stocks[buyer]`, but `exportable[buyer]` was frozen before any trade, so the buyer cannot resell them until next month. That is the reason `exportable` is computed once, outside the edge loop.
- Stores start empty; there is no free initial reserve.

## B3. `add_seasonal_food(result, cfg)`
Guard: if `result['humans']` is missing or falsy, return unchanged.
```
seasonality = options(cfg)['seasonality'] if cfg.world_recipe else 1.0
models = []
for (core, site) in zip(humans.cores, sites):            # positional; core order == site order
    p = get_profile(site.population_profile)
    harvest = [0]*12
    for hamlet in humans.hamlets:
        if hamlet.core_id != site.id: continue
        x = hamlet.x; z = hamlet.z
        latitude = 90 - 180 * z / (cfg.size - 1)
        annual = seasonal_harvest(hamlet.delivered_food,
                                  layers['moisture'][z][x],
                                  latitude,
                                  layers['temperature'][z][x],
                                  p.food_temperature_tolerance,
                                  p.food_temperature_ideal,
                                  seasonality)
        harvest = elementwise sum(harvest, annual)
    demand         = core.food_demand / 12
    # Materials imply storage construction/maintenance; humidity/heat drive spoilage.
    storage_months = min(6, core.material_supply / max(core.food_demand, 0.01) * 3)
    spoilage       = min(0.08, 0.005 + 0.035 * site.moisture
                                     * clamp((site.temperature_c + 5) / 35, 0, 1))
    models.append({site_id: site.id, harvest, demand,
                   storage: demand * storage_months, spoilage,
                   transport_budget: core.material_supply / 12})
```
Note the harvest curve is built **per hamlet** (using that hamlet's own cell climate and latitude) and summed, not from the city's climate.
```
routes = []
for road in roads.routes:                                # positional order preserved
    a = road.from; b = road.to
    mean     = (sites[a].temperature_c + sites[b].temperature_c) / 2
    latitude = 90 - 90 * (sites[a].z + sites[b].z) / (cfg.size - 1)   # midpoint latitude
    base     = (models[a].demand + models[b].demand) / (1 + road.cost / cfg.support_reach)
    for month in 0..11:
        temp   = mean + 15 * sin(radians(latitude)) * cos(2*pi*(month - 6)/12)
        winter = clamp((temp + 15) / 20, 0, 1)           # 0 at -15 C, full at +5 C
        capacity[month] = base * winter / (1 + 0.1 * len(road.river_crossings))
    routes.append({from: a, to: b, capacity,
                   efficiency: exp(-road.cost / 4000),
                   price: 0.2 + road.cost / 5000})
```
The capacity of a road is pegged to the two cities' combined monthly demand, attenuated by the road's cost relative to the support reach, shut down in deep winter, and reduced 10% per river crossing (unbridged fords).
```
run  = simulate_food(models, routes)          # years defaults to 4 -> 48 month records
final = run.months[-12:]                      # year 4, month indices 0..11 in order
for i, (core, site) in enumerate(zip(humans.cores, sites)):
    months   = [m.cities[i] for m in final]
    shortage = sum(m.shortage for m in months)
    demand   = sum(m.demand   for m in months)
    coverage = 1 - shortage/demand if demand != 0 else 1.0
    delta    = months[-1].closing - months[0].opening
    summaries.append({site_id: i,
        annual_demand: demand, annual_harvest: sum(m.harvest for m in months),
        annual_shortage: shortage, food_coverage: coverage,
        lean_months: [m+1 for m, v in enumerate(months) if v.shortage > 1e-8],
        reserve_change: delta, ending_reserve: months[-1].closing,
        passes_final_year: shortage < 1e-8 and delta >= -1e-8,
        urban_food_equivalent: floor(site.urban_population_estimate_or_0 * coverage)})
```
Write `result['seasonal_food']` with `version: 1, years: 4, reported_year: 4`, the summaries, models, routes and all 48 month records, plus the `method` and `limits` prose. Append **only** `limits` to `warnings`. Record `timing_ms['seasonal_food']` and add it into `timing_ms['total']`.

## Constants

Every literal in these two files, with its meaning and the effect of moving it. Distances are marked **pre-scale** (authored against the ~11.15 km reference world, never scaled) or **post-scale** (arrives already multiplied by `circumference_m / 11148.96`).

### `farming_potential`
- `0.7` in `(1 - 0.7*flood)` — a fully flood-prone cell keeps 30% of its farming terrain factor. Raising it toward 1 makes floodplains worthless; floodplain hamlets vanish.
- `0.75` in `natural + 0.75*terrain*irrigation` — **maintenance**: only three quarters of the irrigation gain becomes exportable surplus. Lowering it makes irrigation not worth the water reach; raising it above 1 would make an irrigated cell out-yield a naturally ideal one.

### Yield calibration
- `100` in `100 * area * food[i] * delivery` — the generator-wide provisional calibration, "100 residents per fully productive km2" (stated in `terrain_settlements.life_capacity`). It is the single number that converts a 0..1 potential and a km2 into food units, and it must match `life_capacity`'s or population estimates and food supply stop meaning the same thing.
- `0.25` — the off-speciality rate. A farming hamlet still delivers a quarter of the material a resource hamlet would, and vice versa. Zero would make every hamlet a monoculture; 1 would make `role` meaningless.

### Hamlet placement
- `100` metres (**pre-scale**, a *cost* not a distance, but in the same units) — minimum access cost from the city. Keeps hamlets off the city's doorstep.
- `120` metres (**pre-scale**, true great-circle distance) — minimum spacing between a hamlet and anything already occupied.
- `0.025` — the score floor. Below it the loop breaks and the city simply gets fewer hamlets than its limit. Raising it empties marginal worlds of rural settlement; lowering it seats hamlets on ground that delivers essentially nothing.
- `k % 3 == 2` — one resource hamlet in every three, in the order farming, farming, resource.
- `40` in `ceil(rural_population_estimate / 40)` — residents per hamlet when `auto_parameters` is on. Mirrors `terrain_recipes.derive_institutions`, which uses the same 40.
- `250.0` (**pre-scale**) — the cost limit on the hamlet catchment allocation. A hamlet farms out to 250 cost units, not to `support_reach`. Raising it makes catchments overlap-compete over more ground and inflates `worked_area_km2`; the test `sum(worked_area_km2) <= dry_km2` is the guard.

### Fortress placement
- `200` metres (**pre-scale**) — minimum fortress spacing. Larger than the hamlet's 120.
- `min(3, war_count)` — a city's veteran demand saturates at three wars.
- `0.5 * veteran[owner]` — how much a fought-over city's ground outranks equally defensible peaceful ground.
- `base = 1 + min(2, max(0, len(links) - 2)) + (1 if crossing else 0)` — a plain road node scores 1, a three-way junction 2, a four-way-or-more 3, plus 1 for a river crossing. The `min(2, ...)` caps junction importance.
- `tpi[i] / 30` clamped to `[-0.5, 1]` — 30 m of local relief is worth a full point of defence value; a pit costs at most half a point.
- `0.5 * flood[i]` — flood penalty.
- `slope[i] / 40` — a 40-degree slope costs a full point (and is usually already excluded by `work_slope_limit`).
- `d / 500` (**pre-scale**, metres) — a neighbour 500 m from the junction loses a full point, so the junction itself is strongly preferred over the ring around it.

### `exchange_food` (annual trade)
- `4000` (**pre-scale**, cost units) in `efficiency = exp(-cost/4000)` — transport loss e-folding length. At 1000 cost, 78% arrives; at 4000, 37%.
- `5000` (**pre-scale**) and `0.2` in `price = 0.2 + cost/5000` — material cost per delivered food unit: a 0.2 base handling charge plus distance. A city with 30 material units can buy about 150 food units locally, far fewer far away.
- `1e-9` — the two "nothing left to trade" epsilons.

### Culture
- `child_seed` domain string `'culture-' + str(g)` and the fixed prefix `'tectonics-v1:'` inside `child_seed`. The 8-hex formatting `:08x` of a 32-bit value.

### `seasonal_harvest`
- `20` in `amplitude = 20*sin(lat)*...` — degrees C of peak-to-mean thermal swing at the pole before the moisture and seasonality terms. Identical to `terrain_ecology.monthly_temperatures`, which must stay in step or the climate and the food disagree about the same month.
- `0.4` in `(1 - 0.4*wet)` — a saturated-moisture cell damps its seasonal swing by 40% (maritime moderation).
- `ideal = 18` default and `seasonality = 1.0` default. The callers always pass the profile's `food_temperature_ideal` and the world option, so the defaults only bite in tests.
- `0.05` floor on `baseline` — caps the normalised growth multiplier for a city whose mean climate is already hostile. Without it a mean temperature far from ideal gives a near-zero baseline and an enormous `thermal/baseline` ratio.
- `8` in `clamp(temp/8, 0, 1)` — cold cutoff: no growth at 0 C, full thermal expression at 8 C.
- `1.5` — cap on the per-month growth multiplier.
- `12` — the division that makes the twelve months sum to roughly the annual figure.
- `month - 6` — puts the thermal peak in July (index 6) for the northern hemisphere.
- `(month - 1) mod 12` — the one-month harvest lag.

### `simulate_food`
- `years = 4` default, and `1e-12` as the "no shipment" epsilon (note: different from `exchange_food`'s `1e-9`).
- `transport_budget` default `+inf`, `price` default `1.0`.

### `add_seasonal_food`
- `6` and `3` in `storage_months = min(6, material_supply / max(food_demand, 0.01) * 3)` — three months of storage per unit of material-to-demand ratio, capped at six months. `0.01` is the divide-by-zero guard.
- `0.005`, `0.035`, `0.08`, `+5`, `35` in the spoilage formula: a base 0.5%/month loss, up to 3.5% more driven by moisture x heat, capped at 8%/month. The `(temp + 5)/35` clamp means -5 C and below spoils nothing extra, +30 C and above spoils at the full rate.
- `15` in `mean + 15*sin(lat)*cos(...)` — the route-level thermal swing; deliberately smaller than the harvest curve's 20 because a road midpoint is being described rather than a farm cell.
- `(temp + 15)/20` clamped to `[0,1]` — roads are fully closed at -15 C and fully open at +5 C.
- `0.1` in `1 / (1 + 0.1 * len(river_crossings))` — each unbridged ford costs about 10% of throughput.
- `4000`, `5000`, `0.2` in `efficiency` and `price` — the same constants as `exchange_food`, deliberately, so the annual diagnostic and the monthly simulation price the same road the same way.
- `1e-8` — the shortage and reserve-drift epsilon in the year-4 summaries. Note it is a third distinct epsilon.
- `-12` in `run.months[-12:]` — report year 4 only.

### Prose constants (block metadata)
`humans.version = 9`, `seasonal_food.version = 1`, `seasonal_food.years = 4`, `seasonal_food.reported_year = 4`. Version 9 is a hard gate on age advancement.

## Data files

### `Sim/icarus_sim/civilizations.json` (+ its sibling `buildings.json`)

Schema version 6, revision 12 at time of reading. `population_profiles()` returns `{**{key: entity['population'] for key, entity in doc['entities'].items()}, **doc['configurations']}` — so the map is **entity profiles first, in document order, then aggregate configurations**. Today that yields, in order:

`human_maritime, human_desert, human_cold, human_large_island, human_rainforest, human_heartland, dwarf, elf, gnome, tidekin, hill_dwarf, frosthold_dwarf`, then the aggregate `mixed`.

`civilization_ids()` filters to `civilization.kind == 'entity'`, preserving that order, and drops `mixed`. **`civilization_region` stores a positional index into exactly that tuple.** Reordering the JSON keys renumbers that layer for every world ever generated. In C++ this must be a fixed, explicitly-ordered enum or table, not a map iteration.

`defaults.profile_id` is `'human_heartland'`; `defaults.aggregate_profile_id` is `'mixed'`.

### Profile traits these modules read

| profile | food_slope_comfort | food_temp_ideal | food_temp_tol | food_moisture_ideal | water_reach | irrigation | food_demand | work_slope_limit | road_grade_limit | mutation_limit |
|---|---|---|---|---|---|---|---|---|---|---|
| human_maritime | 18 | 20 | 26 | 0.65 | 500 | 0.7 | 10 | 20 | 0.35 | 0.45 |
| human_desert | 20 | 28 | 26 | 0.40 | 650 | 0.9 | 10 | 22 | 0.40 | 0.45 |
| human_cold | 22 | 4 | 26 | 0.55 | 500 | 0.55 | 10 | 25 | 0.45 | 0.45 |
| human_large_island | 22 | 18 | 28 | 0.70 | 500 | 0.75 | 10 | 25 | 0.45 | 0.45 |
| human_rainforest | 20 | 25 | 22 | 0.85 | 450 | 0.5 | 10 | 22 | 0.40 | 0.45 |
| human_heartland | 18 | 18 | 28 | 0.60 | 400 | 0.7 | 10 | 20 | 0.35 | 0.45 |
| dwarf | 24 | 9 | 30 | 0.55 | 600 | 0.7 | 10 | 30 | 0.55 | 0.45 |
| elf | 18 | 18 | 28 | 0.75 | 400 | 0.4 | 8 | 20 | 0.35 | 0.60 |
| gnome | 18 | 18 | 28 | 0.65 | 400 | 0.7 | 8 | 20 | 0.35 | 0.55 |
| tidekin | 18 | 18 | 28 | 0.65 | 600 | 0.7 | 8 | 20 | 0.35 | 0.60 |
| hill_dwarf | 10 | 18 | 28 | 0.60 | 400 | 0.7 | 8 | 16 | 0.35 | 0.45 |
| frosthold_dwarf | 24 | 9 | 30 | 0.55 | 600 | 0.7 | 10 | 30 | 0.55 | 0.45 |
| mixed | 18 | 18 | 28 | 0.65 | 400 | 0.7 | 10 | 20 | 0.35 | 0.45 |

`water_reach` is in **metres, pre-scale** (authored for the reference world) and is used raw inside `exp(-freshwater_distance / water_reach)`, where `freshwater_distance` is in post-scale physical metres. That mismatch is real and is noted in open questions.

**`food_biome_multipliers`** — keyed by the decimal string of a natural biome id, value 0..2. Almost empty: every profile has `{'13': 0.35}` except `elf` (`{'13': 0.6}`) and `hill_dwarf` (`{'13': 0.35, '3': 1.15}`). Biome 13 is heavily penalised for everyone; a hill dwarf gets a 15% bonus on biome 3.

**`food_magic_biome_multipliers`** — keyed by a variant id string `"<core_biome_name>.<school>"`, value 0..2. Human heartland's 17 entries, as a representative set:

```
tundra.infernal 0.05    desert.infernal 0.15    grassland.infernal 0.05
forest.infernal 0.05    exposed_rock.infernal 0.05   snow.infernal 0.05
rainforest.infernal 0.05  marsh.infernal 0.05   boreal_forest.infernal 0.05
cold_tundra.infernal 0.05 land_ice.infernal 0.05
forest.earth 0.5        rainforest.earth 0.5    boreal_forest.earth 0.5
desert.weave 0.15       desert.water 0.15       marsh.umbral 0.35
```
So infernal ground is near-sterile, earth-touched forest is halved (overgrown, not farmable), weave/water desert and umbral marsh are heavily penalised.

### The biome-variant catalogue (`terrain_biome_catalogue.biome_catalogue()`)

A flat list: **every natural biome crossed with every KNOWN school first, then every natural biome crossed with every HIDDEN school.** A position in this list is a contract — `biome_variant` stores a flat index into it, so the known block keeps its positions forever and hidden schools are appended, never inserted.

- KNOWN_SCHOOLS, in order: `weave, umbral, infernal, radiant, fire, water, earth, air`.
- HIDDEN_SCHOOLS, in order: `blood, void, rot, eldritch`.
- Entry id is `"<core>.<school>"`, e.g. `"forest.earth"`.

`biome_food_multiplier` returns **0.0** for any variant whose school is hidden and that the profile does not name explicitly — no profile names one today, and none needs to. Corrupted ground feeds nobody; which of the four took it is the corruption record's business, not the terrain table's. Port this as a rule, not as 52 table entries per people.

### `world_options` (JSON string in the config)

Only `seasonality` is read here: default `1.0`, range `0..2`, described as "Latitude-dependent temperature seasonality". It is applied only when `cfg.world_recipe` is truthy; on recipe 0 the code hardcodes `1.0`.

## Random streams

**Neither module draws a single random number.** There is no `random.Random`, no sampling, no shuffling. Both are fully deterministic functions of the layers, the settlement list and the config.

The only seed-derived value is the culture id string:

```
child_seed(master, domain, variation=0)
    = first 4 bytes, big-endian, of sha256(f'tectonics-v1:{master}:{domain}:{variation}')

culture_ids[g] = f"{sites[g].population_profile}-{child_seed(cfg.seed, 'culture-' + str(g)):08x}"
```

with `master = cfg.seed` (a uint32) and `domain = 'culture-' + decimal(g)` where `g` is the root city index. It is used purely as a label — nothing downstream derives behaviour from its bits, only string identity and equality.

For the C++ port, `fwg::Rng::child(master, domain, variation)` is the established third seed derivation and is the right place to hang this, *or* the SHA-256 already in `fwg/sha256.hpp` can reproduce the exact Python spelling if any archived world's culture ids must remain readable. Since this is a clean rewrite with its own numeric semantics, either is acceptable — but pick one and do not invent a fourth derivation.

**Draw count: zero, and therefore not data-dependent.** No stage in these two modules can perturb any other stage's stream.

Note that the culture id embeds `g`, a city index, so it is *not* stable across an age boundary — see ordering hazards.

## Ordering hazards

Ten places where container order or comparison order reaches output.

1. **`sphere_grid` neighbour lists are sorted ascending by node index** (`sorted(ids)` in the producer). Every Dijkstra in these modules relaxes neighbours in that order, so a tie broken by "first relaxed wins" resolves to the lowest neighbour index. `fwg::sphere_grid` must preserve this.

2. **`allocate_access` heap ordering is lexicographic on `(cost, label, node)`** and the tie-break is explicit: `candidate == distance[j] and label < owner[j]`. Two cities exactly equidistant from a cell: the lower city id owns it. Reproduce the three-element comparison, not just the cost.

3. **The recipe-3 replacement Dijkstra has NO owner tie-break** — it uses strict `candidate < distance[j]`. An exact tie therefore goes to whichever label was relaxed first, which the `(cost, label, node)` heap order makes the lower label anyway, but by a different mechanism. Keep both spellings distinct; do not "fix" the recipe branch to match `allocate_access`.

4. **`route_neighbors` is a dict built in road-iteration order, and the strategic-score loop iterates it in insertion order.** The scoring test is strict `value > strategic[i].value`, so when two route nodes give the same node the same defence value, the **first-inserted route node** wins and becomes `protected_route_node`. That depends on the order of `roads.routes` and, within a route, on the order of `road.nodes`. Use an insertion-ordered container in C++ (a vector of (node, neighbours) plus an index map), not `std::unordered_map` or `std::map`.

5. **Final fortress selection sorts `strategic.items()` by `(-value, node_index)`** — a total order, so this step itself is safe once (4) is fixed.

6. **`hamlets` order is city-major, then generated k order, then player-adopted rows sorted by node.** The `id` ordinal is a position in that list, and `hamlet_catchment` stores indices into it. Change the order and you change both.

7. **`exchange_food` iterates buyers sorted ascending by `(food_supply - food_demand, site_id)`** — neediest first, ties by city id — and, for each buyer, sellers sorted ascending by `(distance, city_index)`. Both are total orders. But the whole result is order-dependent in substance: the first buyer served takes the surplus, so changing the buyer order changes who goes hungry.

8. **`culture_ids` and `cultures` iterate `sorted(set(groups))`** — ascending root city index. `cultures[].city_ids` is built by ascending scan, so it is ascending. Safe, but must be a sorted container in C++.

9. **`civilization_ids()` order is the JSON document's entity key order**, and `civilization_region` stores a positional index into it. This is the single most fragile ordering in the pair: it is baked into a saved layer. Pin it as an explicit ordered table in C++.

10. **`zip(society['cores'], sites)` and `models[a]` / `models[b]` in the seasons module assume `cores[i].site_id == i == sites[i].id`.** That holds because `cores` is built by iterating `sites` in order, but it is an unstated invariant. Assert it in C++ rather than relying on it.

### Ids that renumber across an age boundary

`hamlet-<k>`, `fortress-<k>`, the group root index `g` inside a culture id, and therefore the culture id string itself, all renumber whenever the rural layer is rebuilt — which every age advance does, because `add_humans` re-runs from scratch and forts are re-sorted by defence score. `fortress-36` names different ground after an advance while the terrain node does not move. The stable handles are:
- `uid` = `"hamlet-node-<node>"` / `"fortress-node-<node>"`, and
- the terrain `node` index itself.

`core_id` is a city index, which also renumbers if the city list changes. Anything that must survive an advance keys on the node, spelled out.

### `tier`, and `camp`

Neither word appears in these two modules. `kind` here takes only `'hamlet'` and `'fortress'`; `role` takes only `'farming'` and `'resource'`. Nothing here collides with the four meanings of `tier` elsewhere in the generator — but `id` does double duty across the codebase and in this block it is unambiguously an **ordinal string**, not a stable key.

## Edge cases

### `add_humans`

- **`cfg.phase < 9` or no `roads`**: returns the world untouched. No `humans` block, no layers, no timing entry, no warnings. `add_seasonal_food` then also returns untouched.
- **Zero cities** (`settlement_count = 0`): `sites` is empty, `roads.routes` is empty (but the `roads` dict exists and is truthy, so the pass runs). `owner` is all -1, `groups` is empty, `cultures` and `cores` and `hamlets` and `forts` are all empty, `region` and `civilization_region` are all -1. `node_profiles` is the default profile everywhere. `proposed = 0 + 0 + 0 = 0`, `fortress_limit = min(fortress_count, 0, 0) = 0`. The layers are still written. Pinned by `test_empty_and_disabled_rural_generation`.
- **`hamlets_per_core = 0` / `fortress_count = 0`**: empty lists, `cores[].food_supply == 0`, `material_supply == 0`. `exchange_food` then finds no surplus anywhere and every core carries its full demand as deficit.
- **A city with no eligible hinterland** (all water, all too steep, all outside `support_reach`, or the score floor bites): fewer hamlets than the limit, possibly zero. This is normal and must not be treated as an error.
- **`score(i) < 0.025` on the very best candidate**: the inner `for` breaks immediately and that k iteration produces nothing. Note the break, not continue — nothing worse will follow in a descending-score list.
- **`separated` rejects every remaining candidate**: the inner loop runs to exhaustion and that k iteration produces nothing.
- **`farm_owner[i] == -1`** (no hamlet within 250 cost units, or the cell crosses a catchment boundary): the cell contributes to nobody. Wilderness is expected and is deliberately left unassigned.
- **A hamlet whose own node is a city node**: impossible by construction (`separated` against `occupied`, which starts with every city node), but the delivery loop also explicitly skips `i in city_nodes`.
- **`freshwater_distance == -1`** (no mapped freshwater reachable): `access = 0`, irrigation contributes nothing, and `natural == irrigated` exactly. Pinned by `test_adaptation_needs_water_and_trade_needs_routes`.
- **`wet >= food_moisture_ideal`**: the irrigation term's `max(0, (ideal - wet)/ideal)` is zero — irrigation never applies to an already-wet cell.
- **`|temp - ideal| >= tolerance`**: `max(0, ...)` clamps the temperature factor to 0, so `terrain` and both yields are exactly zero. A far-too-cold or far-too-hot cell produces nothing, it does not go negative.
- **`hazard[i] == 1`**: `(1 - hazard[i]) == 0`, yields are zero. Hazard is clamped to `<= 1` upstream in both producers, so `1 - hazard` never goes negative — but clamp defensively in C++.
- **Unowned node (`owner[i] < 0`) in the recipe-3 hazard rebuild**: `node_profiles[i]` falls back to the default profile, so `magic_risk_<default_profile>` is read. When the default profile is the aggregate `'mixed'`, no `magic_risk_mixed` layer exists, so it falls back to `magic_hazard`, and if that is absent, to a zero grid.
- **Missing `magic_hazard` layer entirely**: hazard is a zeros vector; `road_cost_function` is still handed a non-null hazard so the `max(hazard[i], hazard[j]) > limit` test runs and always passes.
- **World edges and poles**: everything is done on the sphere node graph, so there are no edges. The poles are single nodes carrying the whole ring's area (`areas[0]` and `areas[-1]` are the full polar cap), which means a hamlet that wins a pole node in its catchment picks up an enormous `worked_area_km2` and a correspondingly enormous `delivered_food`. `road_cost_function`'s diagonal-corner test explicitly exempts polar rows (`zi not in (0, n-1)`), because a pole has no diagonal.
- **`node_grid` at the poles**: row 0 takes `values[0]` and row n-1 takes `values[-1]`, both repeated n times; every row's last column is copied from its first. Integer-valued layers (`culture_region`, `civilization_region`, `hamlet_catchment`) go through the same path, so -1 is repeated across the pole rows.
- **Two player rows on the same node**: not prevented here. The founding API prevents it upstream. If it happened, both would be adopted and `test_no_two_settlements_share_a_node_in_a_finished_world` would fail.
- **A player row whose node is outside every catchment** (`owner[node] < 0`): `adopt` keeps the *old* row wholesale (old `core_id`, old `culture_id`, old `access_cost`, old `access_nodes`) and only reissues the ordinal `id`. It is never dropped.

### `exchange_food`

- **A city disconnected from every road**: `distances[a]` is infinite for every other city, so it neither buys nor sells; its full shortfall stays in `food_deficit`. Pinned by the test's third core.
- **`buyer.material_supply == 0`**: `budget = 0`, `received = 0`, nothing is bought however much surplus exists.
- **`efficiency` near zero on a very long road**: `sent = received / efficiency` blows up, but `received` is bounded by `surplus * efficiency`, so `sent <= surplus`. No division by zero: `exp(-x)` is never exactly 0 for finite x.
- **`needed <= 1e-9`**: the seller loop breaks. **`received <= 1e-9`**: that seller is skipped but the loop continues to the next.

### `seasonal_harvest`

- **`latitude == 0`**: amplitude is 0, every month is the mean temperature, growth is flat, harvest is `annual/12` in each month (modulo the `min(1.5, ...)` cap, which cannot bite when growth is exactly 1).
- **`mean_temp` far from ideal**: `baseline` floors at 0.05, so growth is capped at 30x before the `min(1.5, ...)`; the cap always binds.
- **`temp <= 0` in a month**: `clamp(temp/8, 0, 1)` is 0, that month grows nothing.
- **`annual_food == 0`** (a hamlet that delivers nothing): all twelve months are 0.
- **`tolerance` is guaranteed > 0** by the profile validator, so the Gaussian never divides by zero.

### `simulate_food`

- **Empty `cities`**: 48 month records with empty `cities` lists and zero totals. The outer loop still runs.
- **Empty `roads`**: no trade at all; every city lives off its own harvest and store. Pinned.
- **`edge.efficiency <= 0`**: `continue` before any arithmetic — no shipment, no division. Pinned.
- **`storage == 0`**: `exportable = max(0, stocks - demand - min(demand, 0)) = max(0, stocks - demand)`. A donor does not reserve food it cannot store — pinned by `test_donor_does_not_reserve_food_it_cannot_store`.
- **`transport_budget` absent**: treated as `+inf`; `budgets[buyer] / price / efficiency` is then `+inf` and never binds. Guard against `inf * 0` in C++.
- **An edge both of whose endpoints are the same city**: not produced by the road builder (the MST never self-loops) but would double-count if it were.
- **Stocks can never go negative**: `sent <= exportable[seller] <= stocks[seller] - demand - reserve`, and `consumed = min(demand, stocks)`. Pinned by `all(c['closing'] >= 0)`.

### `add_seasonal_food`

- **No `humans` block**: returns unchanged, no `seasonal_food`, no timing entry.
- **`annual_demand == 0`** (a city with zero urban population and a zero `food_demand`): `coverage` is set to `1.0` by the explicit `if demand else 1.` guard, and `urban_food_equivalent` is `floor(0 * 1) = 0`.
- **Site with no `urban_population_estimate`**: `.get(..., 0)` yields 0, so `urban_food_equivalent` is 0 even at full coverage.
- **`core.food_demand == 0`**: `max(food_demand, 0.01)` guards the storage-months division; `demand = 0` then makes `storage = 0` too, so the city stores nothing and consumes nothing.
- **A road whose `cost` is very large**: `base` shrinks toward zero, `efficiency` toward zero, `price` grows. `efficiency <= 0` is unreachable for finite cost, so the shipment path is taken with a vanishing `sent`.
- **Negative `capacity`**: impossible — `base >= 0`, `winter` is clamped to `[0, 1]`, the crossing divisor is `>= 1`.

## Reasoning worth preserving

These comments record decisions, measurements and failures that exist nowhere else. Quoted verbatim.

**On why player-founded rural sites bypass every ceiling** (`terrain_humans.py:143-146`):
> "A rural site the player founded is an instruction, not a proposal, so it is not re-derived and not subject to the counts and ceilings below. Its node joins the occupied set before the generated passes run, so nothing is placed on top of it -- the uniqueness `test_no_two_settlements_share_a_node_in_a_finished_world` pins."

**On the two ids, and why there are two** (`terrain_humans.py:158-163`, inside `record`):
> "`id` is the ORDINAL, kept because `hamlet_plans` and `castle_plans` join on it. `uid` beside it is the handle that survives an age boundary: this list is rebuilt and re-sorted by defence score every age, so `fortress-36` names different ground after an advance while the terrain node does not move. Same spelling the player founding route mints (terrain_settlement_api._rural_row) and the same one npc_roster and hero_generator key on -- one convention, not three."

**On what `adopt` re-derives and what it must not** (`terrain_humans.py:170-177`):
> "The ordinal `id` is reissued because it is a position in a list this pass rebuilt, and the access path, owning city and culture are recomputed because they describe the world as it is now. The `uid`, the node and the player's own choices are not: they are what the player founded, and re-deriving them would move a settlement under its owner. A node outside every catchment keeps the core it was founded against rather than being dropped."

**On adopting hamlets before the catchment allocation** (`terrain_humans.py:205-209`):
> "Adopted before the catchment allocation below, so a player hamlet works land exactly as a generated one does. The four accumulators start at zero for the same reason a generated hamlet's do -- the allocation fills them -- and the irrigation benefit is a read of this pass's ground rather than the previous pass's, because it describes the cell and not the settlement. The caller's block is never mutated."

**On why fortress count is a ceiling and not a target** (`terrain_humans.py:235-242`):
> "The road network asks for its own defence: one watch per support reach of road plus one per distinct crossing. A civilization cannot raise more forts than it has cities to raise them, and cfg.fortress_count is a caller's ceiling over both -- it has always been a maximum here, never a target or a guarantee."
> "A city that has fought a war wants more watching its roads, so its wars raise both what the world asks for and how much it is allowed. A world that has never been to war keeps exactly the road-and-city answer."

And the default in `terrain_lab.Config`:
> "A ceiling, not a count. The default sits at the bound so it never binds and the generated road network decides how much route defence a world asks for."

**On the veteran bonus** (`terrain_humans.py:255-256`):
> "Ground belonging to a city that has fought outranks equally defensible ground belonging to one that has not."

**On the player keep** (`terrain_humans.py:266-267`):
> "A player keep is not a route-defence proposal, so it is appended after the ceiling rather than competing for a place under it."

**On per-civilization frontiers in recipe 3** (`terrain_humans.py:107`):
> "A frontier retains its owner's traversal rules; parent paths cannot jump ownership."

**On maintenance in `farming_potential`** (`terrain_humans.py:52`):
> "Maintenance reduces the added exportable surplus; no source means no irrigation."

**On what the food numbers are** (`terrain_humans.py:135`):
> "Relative exportable potential after rural subsistence, not people or tonnes/year."

**On catchment exclusivity** (`terrain_humans.py:216`):
> "Each productive cell belongs to at most one hamlet and cannot cross city catchments."

**On `allocate_access`'s contract** (docstring):
> "One terrain-cost owner per node; deterministic ties, no water shortcuts."

**On `culture_groups`' canonical id** (docstring):
> "Single-link groups; canonical ID is the lowest member city index."

**On `exchange_food`** (docstring):
> "Finite local surplus traded over existing roads, with loss and material cost."

**The three `humans` method strings, which are output and must be reproduced verbatim:**

`method`:
> "All primary pins are cities. Rural sites share exclusive reachable catchments. Food/material values are relative exportable potential units, not historical yields or population capacity. Irrigation requires nearby mapped freshwater; water extraction capacity and groundwater are not simulated. Cities can buy finite surplus over roads using material potential, with transport loss; remaining shortages stay visible."
> (with magic: + " Mutation and the selected population biome food multipliers reduce crop surplus. Rural access cannot cross unsafe magic.")

`culture_method`:
> "Existing road links below a cost threshold form single-link interaction groups. Culture IDs are seed-local, not inferred ethnicities or political borders; architecture style keys identify standalone civilizations. Territory stops at the support reach; wilderness remains unassigned."

`defence_method`:
> "Fortresses are spaced route-defence proposals, not a siege or visibility simulation. How many a world asks for follows its own road length, support reach and distinct river crossings, ceilinged by its city count; fortress_count caps that and never raises it. Usable defensible ground and the 200 m spacing decide how many of the requested forts exist. Garrison demand is not yet budgeted."

**On what `terrain_seasons` is** (module docstring):
> "Deterministic seasonal stress test of proposed cities, not live population state."

**On `seasonal_harvest`** (docstring):
> "One-month harvest lag; thermal seasonality reverses across the equator. Annual food is the existing mean-climate estimate, not a guaranteed yield. Seasonal growing suitability can change the year's actual production."

**On `simulate_food`** (docstring):
> "Monthly conservation with shared edge throughput and no same-month re-export. Stores start empty; consumption precedes storage overflow. Trade is directly between road neighbours and costs the buyer renewable monthly material units."

**On storage and spoilage** (`terrain_seasons.py:80`):
> "Materials imply storage construction/maintenance; humidity/heat drive spoilage."

**The two `seasonal_food` prose strings, which are output:**

`method`:
> "Four repeated climatic years from empty stores; year 4 is a stress-test observation, not proof of equilibrium. Monthly mean thermal growth and a one-month harvest lag use existing rural exportable surplus after rural subsistence. Storage and monthly transport budgets derive from material potential; humidity/heat cause spoilage. Direct road neighbours trade finite surplus, protecting one month of donor reserve; shared edge throughput, winter penalties, transport loss and material cost apply. No same-month re-export. Legacy annual trade is a separate diagnostic, not extra food. Proposed population is not mutated."

`limits`:
> "Relative food units retain provisional calibration. Rural subsistence is assumed upstream, not seasonally validated here; coverage and food-equivalent residents concern urban demand only. No crop calendar diversity, stochastic droughts, soil depletion, groundwater extraction, shipping, migration or mortality. Route winter conditions use endpoint climate rather than every pass. Greedy direct-neighbour trade is not an optimal multi-hop logistics solver."

**From `terrain_settlements.shortest_paths`, on why bounding a Dijkstra is exact** (the justification for `allocate_access`'s `limit` and the recipe-3 `support_reach` bound):
> "Stopping at the reach is not an approximation -- edge costs are non-negative, so every prefix of a path costing `limit` or less also costs `limit` or less, and a node inside the bound keeps the same distance and the same parent it had when the search was unbounded. Nodes outside it stay at infinity rather than taking their true value, so read a bounded result only inside the bound."

**From `terrain_profiles.biome_food_multiplier`:**
> "Corrupted ground feeds nobody; which of the four took it is the corruption record's business, not the terrain table's."

**From `terrain_scale.reach_scale`, on why these metre constants must be scaled by the caller:**
> "Reaches, spacings and influence radii are authored as absolute metres that only make sense at the 11.15 km reference world. They have to grow with the world or they stop spanning anything: a 1000 m support reach is a sixth of one cell on a 200 km world at raster 33, so no node is ever near enough to farm and hamlets -- and the fortresses that follow their roads -- both vanish entirely."

**From `terrain_history`, on the version 9 gate:**
> "A rural report older than 7 pinned its fortresses to a static count. Advancing it would rebuild the hinterland under the derived demand and hand back a world whose two ages disagree about how much route defence it ever wanted."

**From `terrain_society`, on why fortresses must be in the occupied set:**
> "Fortresses belong in this set as much as cities and hamlets do. Leaving them out let a harbour be founded on a node a fort already held -- 28 of 56 forts in a seed-42 size-33 world shared their exact node with a coastal hamlet, which draws as one icon on top of another and means two settlements occupy one place."

**From `terrain_time._dedupe_warnings`, on why `add_seasonal_food`'s trailing append needs deduplication:**
> "`add_seasonal_food` ends with `result['warnings'].append(...)`, so it is not quite the wholesale replacement the rest of its body is: every run leaves another copy of the same sentence. Run once at generation that is invisible; run on a cadence it turns a list of limitations into a counter."

## Notes for the C++ implementation

### Mapping to what already exists

- `sphere_grid(n, radius)` -> `fwg::sphere_grid(n, radius)`, which already returns `point_x`, `point_z`, `areas` and `neighbours` as `{index, distance}`. Confirm the neighbour lists come back **ascending by index**; the Python producer sorts them and every tie-break here depends on it.
- `node_grid(values, points, n)` -> `SphereGrid::to_grid`. Both write `values[0]` across row 0, `values[back]` across row n-1, and close the seam. Integer-valued layers (`culture_region`, `civilization_region`, `hamlet_catchment`) go into the same `Grid` of doubles; store -1.0 and compare with a tolerance-free equality on an exactly-representable integer, or keep a parallel `std::vector<int>` and only convert at serialisation.
- `direction(x, z, n)` -> `fwg::direction`. `separated` needs `radius * acos(clamp(dot(a,b), -1, 1))`; `fwg::Vec3` should already give the dot product.
- `child_seed` -> `fwg::Rng::child` or `fwg/sha256.hpp`. Do not add a fourth derivation.
- `fwg::Config` already has `support_reach`, `culture_link_cost`, `settlement_spacing`. It needs `hamlets_per_core`, `fortress_count`, `urban_food_demand`, `human_adaptation`, `human_magic_limit`, `road_max_grade`, `bridge_cost`, `population_profile`, `auto_parameters`, `world_recipe` and a `seasonality` world option.
- `fwg::World` needs typed blocks: `HumansBlock` (cores, hamlets, fortresses, cultures, shipments, fortress_demand, three prose strings) and `SeasonalFoodBlock` (cities, models, routes, months, two prose strings).

### Python idioms with no direct equivalent

1. **`heapq` with tuple keys.** `heapq.heappush(queue, (cost, label, node))` compares lexicographically. In C++ use `std::priority_queue<std::tuple<double,int,int>, std::vector<...>, std::greater<>>`, which gives the same lexicographic order on a `std::tuple`. The explicit `(value != distance[i] || label != owner[i]) continue` stale check is required — there is no decrease-key.

2. **`sorted(container, key=lambda ...)` is a stable sort.** Where the key is a total order (`(-score, index)`, `(distance, index)`, `(supply-demand, site_id)`) stability does not matter. Where it is `sorted(eligible, key=lambda i: (-score(i), i))`, the second element makes it total. Use `std::sort` with an explicit comparator returning that tuple order; do not rely on `std::stable_sort` semantics you do not need.
   **Important:** `score(i)` is re-evaluated by the sort comparator many times per element in Python. Precompute it into a vector once in C++ and sort indices — but be aware that if `score` could produce a NaN the two would diverge. `food` and `resource` are finite and non-negative, and `exp(-d/reach)` is finite for finite `d`, so this is safe; still, assert finiteness.

3. **`dict` insertion order is part of the semantics** for `route_neighbors` (hazard 4 in ordering). Model it as `std::vector<std::pair<int, std::vector<int>>>` plus a `std::unordered_map<int, size_t>` index, or a small ordered-insert structure. `std::map` (sorted by node) would silently change which route node wins a tie.

4. **`set` of tuples for `crossing_edges`**: only `len()` is used, so `std::set<std::pair<int,int>>` or a hashed set both work. `crossings` is membership-only; a `std::vector<bool>` indexed by node is faster and order-free.

5. **`math.inf` sentinels.** `distance` arrays start at `std::numeric_limits<double>::infinity()`; `math.isfinite(distances[a])` -> `std::isfinite`. In `simulate_food`, `transport_budget` defaults to `+inf` and is used as `budgets[buyer] / price / efficiency`; that is `inf` for finite positive `price`/`efficiency`, and `min(x, inf) == x`. Fine in IEEE, but guard `price == 0` (never produced by the callers, always produced by the `.get('price', 1.)` default which is 1).

6. **`dict.get(key, default)` on layers** — `layers.get('magic_risk_' + id, layers.get('magic_hazard', zeros))`. In C++ resolve this to a `const Grid*` once per profile before the loop, never per node.

7. **Optional-key tests.** `'urban_population_estimate' in site` (presence) is distinct from `site.get('urban_population_estimate', 0)` (value with default) — both appear, and they mean different things. Use `std::optional<int>` on the site struct and distinguish `has_value()` from `value_or(0)`.

8. **String-keyed profile biome multipliers.** Convert to integer-keyed at profile-load time. See the `str(3.0)` trap in the algorithm section.

9. **`f'{value:08x}'`** on a 32-bit int -> `std::format("{:08x}", v)` or an 8-wide zero-filled hex stream. Lowercase.

10. **`math.ceil(x / 40)` on an int** -> `(x + 39) / 40` for non-negative x, but the Python is float division then ceil; for x up to a few thousand they agree exactly. Use integer arithmetic.

11. **`math.floor(road_length / support_reach)`** — a genuine float floor; use `std::floor` and then cast, not integer division.

### Data structures worth choosing deliberately

- `owner`, `parent`: `std::vector<int>` sized to node count, initialised to -1.
- `distance`: `std::vector<double>` initialised to infinity.
- Cost functions: the Python builds closures; in C++ make a small `RoadCost` struct holding `const std::vector<double>*` pointers to water/height/flood/river/hazard, the point index map, `n`, `magic_limit`, `max_grade`, `bridge_cost`, with an `operator()(int i, int j, double d) -> std::optional<double>`. On recipe 3 you need one per city — but they differ only in `magic_limit`, `max_grade` and which hazard vector they point at, so store them by value in a `std::vector<RoadCost>` indexed by city id. Do **not** allocate a `std::function` per city and call it in the inner Dijkstra loop; that is the hot path.
- `individual_costs` is keyed by city id and looked up per edge relaxation in the recipe-3 search. Index a vector directly.
- The corner-cell lookup `node_at(x, z)` is called up to twice per edge test. The Python caches it globally (`sphere_index`, with a comment recording that rebuilding it cost twelve seconds at size 128). Build it once per `(n)` and hand it to every cost function.

### Performance notes carried from the Python

- `sphere_index` exists because "one `add_world_society` at size 128 built it 3 886 times for twelve seconds." Build the point->index map once.
- The recipe-3 branch discards a full unbounded multi-source Dijkstra. Skip it.
- `cell_variant(result, x, z)` is called twice per node in the yield loop. Call it once.
- `eligible` is rebuilt per city by scanning every node. At size 513 that is ~263k nodes per city. Either accept it or bucket nodes by owner once.

### What is genuinely easy in C++

There is no RNG, no hashing in the hot path, no recursion, and the only transcendental functions are `exp`, `acos`, `cos`, `sin` and `sqrt` via `hypot`-free code. All the floating-point work is straightforward; since this is a clean rewrite, no CPython float reproduction is needed anywhere in these two modules.

### Testing

Port the seven `test_terrain_seasons` cases and the conservation identities directly — they are pure-function tests on `simulate_food`/`seasonal_harvest` with literal inputs and will transfer unchanged:
- global `opening + harvest == closing + consumed + spoiled + overflow + transit_loss`
- per-city `opening + harvest + imports == closing + consumed + spoiled + overflow + exports`
- `north[i] == south[(i+6) % 12]` and `north[0] != north[6]`
- shared edge throughput, zero-efficiency, material budget, empty-store start, donor-with-no-storage.

From `test_terrain_humans`, the transferable ones are `culture_groups(4, [(0,2,40),(2,1,200)], 100) == [0,1,0,3]` and `== [0,0,0,3]` at threshold 300; `allocate_access` on the four-node chain graph giving `owner == [4,4,7,-1]`, `parent[1] == 0`, `distance[1] == 1`; the `farming_potential` no-freshwater identity; and the `exchange_food` three-city case.

## Open questions

1. **`water_reach` is a pre-scale metre constant compared against a post-scale metre distance.** `farming_potential` computes `exp(-freshwater_distance / profile.water_reach)` where `freshwater_distance` comes from the `freshwater_distance` layer in **post-scale physical metres** (400 m to many kilometres on a 200 km world) while `water_reach` is an authored 400-650 m tuned for the 11.15 km reference world and is *not* passed through `reach_scale`. On a 200 km world the irrigation access term is therefore effectively zero almost everywhere, and `natural == irrigated` for nearly every cell. The same applies to the literals `100`, `120`, `200`, `250` and `500` inside `add_humans`, and to `4000`/`5000` in both trade pricers. Only `support_reach` and `culture_link_cost` are scaled. This may be deliberate (a fixed human walking radius that does not grow with the planet) or it may be the same defect `terrain_world` fixed for the three reaches it does scale. **A porter must decide.** The measured consequence, if it is a defect, is that irrigation and inter-city trade are near-dead on any world wider than the reference.

2. **`cores[].food_deficit` is written twice and the first write is dead.** `max(0, cfg.urban_food_demand - supply)` is computed against the *config* demand, then immediately overwritten by `exchange_food` against the *site* demand. Since `exchange_food` is always called, the first expression can never be observed. Drop it, or keep it if some consumer reads a `humans` block assembled without trade (none does today).

3. **The first, unbounded `allocate_access` in `add_humans` is dead on recipe 3.** Its three arrays are reassigned to fresh infinities before anything reads them. On recipe 0 it is live and, crucially, **unbounded** — so `distance[i]` on recipe 0 is a true global catchment distance, while on recipe 3 it is capped at `support_reach`. Every later `distance[i] <= cfg.support_reach` test therefore behaves identically, but `record()`'s `access_cost` field does not: on recipe 0 a player-adopted row outside the reach still gets a finite access cost, on recipe 3 it would be infinite (and `adopt` avoids that only because `owner[i] < 0` sends it down the copy branch). Confirm this is intended before unifying the two branches.

4. **The fortress `strategic` loop uses the global `cost` function even on recipe 3**, where every other traversal test uses `individual_costs[owner]`. A fort can therefore be sited on a node its owning civilization could not legally reach, if the global `road_max_grade`/`human_magic_limit` are more permissive than that civilization's. Likely an oversight; the comment block above it says nothing about it.

5. **`node_profiles` falls back to `get_profile(cfg.population_profile)` for unowned nodes**, and when `cfg.population_profile == 'mixed'` that is the aggregate configuration, not an entity. `biome_food_multiplier` and `work_slope_limit` are then read off an aggregate. Harmless today (mixed's traits match human_heartland's closely) but it means an unowned cell's yield is computed against a people that does not exist.

6. **`add_seasonal_food` on a time cadence sees `terrain_society`'s coastal port hamlets**, which carry fishery `delivered_food`, and folds them through `seasonal_harvest` as if they were farms. At generation time they are not present (seasons runs before society); on a re-run they are. Nothing in the code acknowledges this. A porter must decide whether the cadence re-run filters port rows out, or whether the fishery contribution is deliberately re-seasonalised.

7. **`ICARUS_CITY_WORKERS` is not read by either of these modules.** The process-pool fork lives in the two settlement planners, not here. Neither `add_humans` nor `add_seasonal_food` forks anything; both are single-threaded and fully deterministic. Recorded because the brief asked about it.

8. **Neither module has a conformance record.** `docs/conformance/coverage.json` lists `Sim/icarus_sim/terrain_humans.py` and `Sim/icarus_sim/terrain_seasons.py` in `uncovered`, both against ticket `board/in-progress/CONFORMANCE-DOCS.md`. The only external mentions are two rows in `read-surface.md` (the `humans.cores.site_id` and `seasonal_food.models.site_id` joins, both citing line numbers that are now stale by a dozen lines) and one line in `settlement-founding.md` confirming that `terrain_humans.record` mints `f'{kind}-node-{i}'` with the same expression the player-founding route uses. **Nothing contradicts the code; there is simply no record to contradict it.** This specification is the only description of these two modules.

9. **`magic_risk_<people>` is currently a verbatim copy of `magic_hazard` for every people** (`terrain_leyline_history.py:346` writes the same `hazard` vector under all twelve names). Every branch in `add_humans` that distinguishes them is therefore currently a no-op. Port the distinction anyway — the layer exists precisely so that a people can eventually have its own view of danger.

## Addenda from the audit

The reader above wrote the specification; a second reader went back to the source
to find what it had left out or got wrong. Both are kept: the omissions are
content the implementer needs, and the corrections say where the specification
above is not to be trusted.

### Corrections

- **Claimed:** ORDERING HAZARDS #3: "The recipe-3 replacement Dijkstra has NO owner tie-break -- it uses strict `candidate < distance[j]`. An exact tie therefore goes to whichever label was relaxed first, which the `(cost, label, node)` heap order makes the lower label anyway, but by a different mechanism."
  **Actually:** The premise is right and the conclusion is wrong. Under strict < the winner is the first RELAXATION, fixed by the pop order of the PREDECESSOR, not by the label on the tying entry. Equal candidate costs can arrive from predecessors of unequal value: node i1 (popped at value 4, owner label 7) relaxes j to 10; node i2 (popped later at value 6, owner label 2) computes candidate 6+4 = 10, which is not < 10, and is discarded -- the higher label 7 keeps the node. The recipe-3 branch therefore resolves ties by predecessor pop order and can award a cell to the higher city id, which allocate_access's explicit `label < owner[j]` clause never does. The spec's pseudocode is correct; only this commentary is wrong, and acting on it (adding a lower-label tie-break 'because it comes out the same anyway') changes catchment ownership.

- **Claimed:** C++ NOTES idiom 2: "`score(i)` is re-evaluated by the sort comparator many times per element in Python. Precompute it into a vector once in C++ and sort indices -- but be aware that if `score` could produce a NaN the two would diverge."
  **Actually:** sorted(seq, key=f) uses decorate-sort-undecorate: CPython calls f exactly once per element, builds the key array, and sorts that. There is no per-comparison re-evaluation and no NaN divergence risk from precomputing. score(i) IS evaluated a second time per examined candidate, but by the explicit `if score(i) < .025: break` test inside the placement loop, not by the sort. The advice to precompute is fine; the stated reason and the stated hazard are both false.

- **Claimed:** B2 pseudocode: "efficiency = edge.efficiency; price = edge.price or 1"
  **Actually:** The source is price = edge.get('price', 1.). Those differ on a literal 0.0: .get returns 0.0 (and budgets[buyer]/price/efficiency then raises ZeroDivisionError), whereas `x or 1` substitutes 1. The spec's own EDGE CASES and C++ NOTES sections state the .get('price', 1.) semantics correctly, so only this pseudocode line is wrong -- but it is the line an implementer would transcribe.

- **Claimed:** A1: "`node_at(x, z)` maps `(0, z)` for polar z and `(x mod (n-1), z)` otherwise to a node index."
  **Actually:** That describes sphere_grid's internal index() helper, not what road_cost_function uses. road_cost_function calls `lookup, _ = sphere_index(points)` and then indexes the plain dict lookup[(xi, zj)] / lookup[(xj, zi)] directly -- no modular wrap, no polar collapse; an out-of-range key would raise KeyError rather than wrapping. It is safe only because the preceding guard already excludes zi/zj in (0, n-1) and because xi, xj are already in 0..n-2. Equivalent in effect for the arguments actually passed, but the described wrapping never executes, and a C++ port that implements the wrap implements a branch the Python does not have.

### Missing — important

**fortress_demand sub-field formulas (four of ten are undefined anywhere in the spec)**

city_ceiling = len(sites) + veteran_demand; requested_ceiling = cfg.fortress_count; player_founded = len(player_rural['fortress']) (the number of adopted player keeps, appended AFTER the ceiling, so len(forts) can exceed limit by exactly this); veteran_cities = sum(1 for v in veteran.values() if v) -- the COUNT of cities with at least one war, not the sum of their saturated veteran values (that is veteran_demand). The other six (proposed, limit, veteran_demand, road_length_m, crossings, support_reach) are correctly derivable from A12.

**elf deviates from the quoted food_magic_biome_multipliers table; the spec calls heartland's 17 entries 'a representative set' and never says who differs**

All thirteen profiles (twelve entities plus 'mixed') carry byte-identical 17-entry maps EXCEPT elf, which overrides four: forest.earth 1.0, rainforest.earth 1.0, boreal_forest.earth 1.0 (all 0.5 for everyone else) and marsh.umbral 0.5 (0.35 for everyone else). Elves farm earth-touched woodland at full rate and umbral marsh better than anyone. Verified by diffing every profile's resolved map against human_heartland's.

**access_cost holds two different quantities in two different units, and the delivery term divides both by support_reach**

A generated row's access_cost is distance[i], a terrain ROAD COST in the units of d*(1+12*grade^2+0.5*(flood_i+flood_j)) + bridge_cost. A player-founded row's (minted by terrain_settlement_api._rural_row) is core_distance, the raw GREAT-CIRCLE METRES to the nearest site via radius*acos(dot(here, site.direction)), falling back to 0. when no site has a 'direction' -- no path search stands behind it. adopt() recomputes it only when owner[node] >= 0; otherwise the great-circle value survives into delivery = exp(-access_cost / support_reach) in the catchment loop and into hamlet_plans. Same field, same divisor, two units.

**NATURAL_BIOMES id->name map -- needed to decode food_biome_multipliers keys and to reconstruct the catalogue's biome-major ordering (the spec gives the school order but not the biome order)**

{0:'ocean', 1:'tundra', 2:'desert', 3:'grassland', 4:'forest', 5:'exposed_rock', 6:'snow', 7:'rainforest', 8:'lake', 13:'marsh', 15:'boreal_forest', 16:'cold_tundra', 17:'land_ice'} -- 13 entries, and the ids are NOT contiguous (9-12 and 14 do not exist), so str(core_id) is a biome id and never a position. Therefore 'biome 13 is heavily penalised for everyone' means MARSH (0.35, elf 0.6) and 'hill dwarf gets 15% on biome 3' means GRASSLAND (1.15). biome_catalogue() is _block(KNOWN)+_block(HIDDEN) where _block(schools) = [entry for (bid,core) in NATURAL_BIOMES.items() for school in schools], i.e. biome-major, school-minor: length 13*8=104 known entries at flat indices 0..103, then 13*4=52 hidden at 104..155, total 156. Entry 0 is ocean.weave, entry 155 is land_ice.eldritch.

### Missing — minor

**farming_potential's profile=None default branch (the function is exported and the tests call it with six arguments)**

def farming_potential(slope, temp, wet, flood, freshwater_distance, adaptation, profile=None) -- when profile is None it loads get_profile(default_profile_id()), and defaults.profile_id in civilizations.json is 'human_heartland' (food_slope_comfort 18, food_temperature_ideal 18 C, tolerance 28, food_moisture_ideal 0.60, water_reach 400 m). add_humans always passes a profile, but the ported free function needs this default or test_adaptation_needs_water_and_trade_needs_routes cannot be transferred as written.

**auto_parameters and world_recipe are mutually exclusive, so the auto hamlet-limit path is recipe-0 only**

terrain_lab.Config.__post_init__ raises ValueError('World recipe 3 uses explicit defaults and overrides; auto_parameters requires world_recipe 0') when both are set. The min(hamlets_per_core, ceil(rural_population_estimate/40)) branch in A9 therefore can never coexist with the per-civilization traversal rules of A3 Variant 2. The spec's config table lists the two fields as independent.

**A player hamlet seated outside every catchment farms exactly one cell -- its own -- and, generally, every hamlet always works its own node**

allocate_access sets owner[seed] = label, so farm_owner[h.node] == k for every hamlet including adopted ones. But farm_cost tests distance[j] > cfg.support_reach against the OUTER access distance, which is +inf for an unowned node, so a hamlet whose owner[node] < 0 can never expand past its seed. Its worked_area_km2 is exactly areas[node]/1e6 and its delivered_food is one cell's worth (unless that node is a city node or slope >= work_slope_limit, in which case it is zero). Conversely every generated hamlet's own cell is always counted.

**hamlet_catchment layer indices go stale within the same world**

terrain_society.add_world_society does result['humans']['hamlets'].extend(ports) AFTER add_humans wrote layers['hamlet_catchment'] = node_grid(farm_owner, ...). The stored indices still address the generated+adopted prefix, which is fine, but any consumer that reads the layer and indexes the CURRENT hamlet list on a finished recipe-3 world is indexing a list that grew. The layer can never name a port.

**support_reach / culture_link_cost scaling is conditional, and overshoot is a hard error rather than a clamp**

terrain_world multiplies settlement_spacing, support_reach and culture_link_cost by reach_scale(circumference) = circumference_m / (2*pi*1774.4123532462844) = circumference_m / 11148.96 m, but ONLY when the key is absent from overrides and only on the registry/world-config path. On the direct terrain_lab.Config path (every test, and Config(size=33, phase=9)) there is NO scaling: support_reach stays the authored 1000 m against a physical radius near 1774 m. If a scaled value exceeds REACH_MAX_M the code deliberately raises RequestError('STATE_CAPACITY', ...) instead of clamping -- the comment records that clamping was the defect being undone, and that culture_link_cost resolves 96870.01 m at the 600 km preset, 3.13% below the ceiling.

**sphere_grid cell-area formula (the spec asserts the property but not the arithmetic)**

step = pi/(n-1); for node (x,z): lat = pi/2 - z*step; area = radius^2 * 2*step * (sin(min(pi/2, lat+step/2)) - sin(max(-pi/2, lat-step/2))); then areas[i] = area * (n-1 if z in (0, n-1) else 1). Neighbour distance is radius * acos(clamp(dot(v_i, v_j), -1, 1)). A pole node's neighbour set is the ENTIRE adjacent ring {index(xx, 1 or n-2) for xx in 0..n-2}, not eight cells. Needed only to verify fwg::sphere_grid already matches.

**add_world_society and add_nests are gated on cfg.world_recipe in terrain_area; the spec's pipeline chain shows them unconditionally**

terrain_area runs result = add_seasonal_food(add_colleges(add_humans(result, cfg), cfg), cfg) unconditionally, then 'if cfg.world_recipe:' before add_world_society(result, cfg) and add_nests(result, cfg). So on recipe 0 the coastal ports never exist and the seasonal_food cadence re-run hazard the spec raises cannot fire at all.

**_rural_row guarantees the keys the unguarded reads depend on**

A player hamlet row always carries role, irrigation_benefit, worked_area_km2, delivered_food, delivered_materials; a player fortress row always carries defence_score: 0. and protected_route_node: None. This is why the catchment delivery loop's unguarded h['role'] cannot KeyError on an adopted row, and why protected_route_node can legitimately be null in the output (a C++ int field needs an optional or a sentinel). access_nodes on a player row is [node] -- a path of length one, which the two rural planners read as 'no support road'.
