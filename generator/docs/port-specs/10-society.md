# Port specification: 10-society

Extracted from the Python `FantasyWorldGenerator` at commit `b67c99b` before that
repository was retired, then audited against the same source by a second reader.

**Source modules**

- `Sim/icarus_sim/terrain_society.py`

**Estimated C++ size:** roughly 1500 lines, excluding tests.

## Audit: NOT YET IMPLEMENTABLE

The body of terrain_society.py is specified unusually well: I walked the algorithm section against lines 127-291 step by step and could not find a constant, a tie-break, a loop bound, an initialisation or an ordering inside this file that the spec omits. Every literal (0.1, 0.55, 0.02, 200, 0.5, 0.4, the 0.1 fishing draft, 1e6, 12000, 10000, 8000, 5000, 0.12, 80, 1e-8, 2, the -12 slice) is present with its meaning; the ranking keys, the lexicographic claim tie-break, the insertion-order dependence of `claims` and `sea_pairs`, the `min()`-first-minimum landing choice, the `<=` first-writer sea-pair rule, the double `air_reach` test against two different lengths, and all six WHY-comments are carried across accurately. I also verified its outward claims and they hold: `site['id']` really is the list ordinal (`sites.append({'id':k,...})`), `magic_risk_<people>` really is a byte copy of `magic_hazard` for all twelve, the ecology pass really mints `witch_huts-{i}`, `water_ice`/`harbor_suitability`/`fishing_productivity`/`coastal_support` formulas are exact, `simulate_food`'s default really is `years=4`, and 'Populated regions' really is stage 12. The blockers are at the pass's two boundaries plus one flatly wrong pipeline claim. (1) `world_economy.months` and `world_economy.sites` are headline published outputs of this pass, and the spec gives only a four-term summary of `simulate_food` that is itself incomplete -- it omits that a route's monthly capacity is ONE `remaining` shared and decremented across both directions, the `sent <= 1e-12` floor, the whole monthly conservation order (spoilage on opening stock before harvest, `exportable = max(0, stock - demand - min(demand, storage))`, consume-then-overflow), and every field of the per-city and per-month records. A porter silently builds a different block with different numbers. (2) The spec states the pass runs from `apply_world_scale` after `add_seasonal_food`; it omits that the call sits inside `if cfg.world_recipe:`, so the main pipeline never calls it at all on recipe 0 -- which makes the spec's recurring "dead on recipe 3, would run on recipe 0" framing wrong and would lead a porter to wire the stage into a path the reference does not have. (3) `wanted` (the witch-hut budget) is described only as "the number already present", with no statement that it originates as `zone_witch_huts` influence centres and is bounded at 3. (4) The `sphere_grid` contract the pass leans on in three places -- above all that a pole node is adjacent to EVERY cell of the adjacent ring -- is deferred wholly to fwg without ever being stated, which is what makes its own pole-area warning meaningful. None of these are misreadings of terrain_society.py itself; they are gaps in what a reader who never sees the repo would be handed.

**1 blocking gap(s)** were found and are recorded in the addenda below.

## Purpose

terrain_society.py is stage-12's "society" pass (`add_world_society`). It is the only place in the generator where the world becomes multimodal: it sites coastal harbour hamlets ("ports") against each inland city, allocates every reachable ocean cell to exactly one port as an exclusive fishery, prices monthly fish delivery under sea ice, builds the sea-route and air-route networks on top of the existing road network, re-sites the world's witch huts onto isolated low-suitability ground, and then runs the authoritative four-year monthly food stress test (`world_economy`) over all three transport modes at once. It also contains a complete but permanently disabled sky-archipelago generator (`make_sky`). It exists because the earlier `seasonal_food` pass is ground-only and knows nothing about coasts: without this pass a coastal world has no harbours, no fisheries, no sea trade and no authoritative supply diagnostic. NOTE: `add_world_society` consumes ZERO random numbers on the live path -- every `random.Random` use is inside `make_sky`, which is unreachable (see `rng`). It is a pure function of the world document plus config.

## Where it sits in the pipeline

Runs at stage 12 ("Populated regions"), called from `terrain_area.apply_world_scale` immediately after `add_seasonal_food` and immediately before `add_nests`, and again from `terrain_history.civilization()` (which `rebuild_tail` calls) on every age advance, in the same relative position. MUST ALREADY EXIST: `result['layers']` (full stage-6..11 layer set), `result['effective_config']['globe_radius']` (physical metres), `result['spacing_m']`, `result['habitats']`, `result['regions']['landmarks']` and `result['regions']['influences']` (from `add_environment`), `result['settlements']['sites']` (from `add_settlements`), `result['roads']['routes']` (from `add_settlements`), `result['humans']` with `cores`/`hamlets`/`fortresses` (from `add_humans`), `result['seasonal_food']` with `models` and `routes` (from `add_seasonal_food`), `result['seasonal_environment']['months'][m]['water_ice']` (from `add_environment`). Two independent early returns: (a) `if cfg.phase < 6 or 'habitats' not in result: return` -- nothing at all is written, not even `sky`; (b) after writing `result['sky']`, `if cfg.phase < 9 or 'seasonal_food' not in result: return`. DEPENDS ON IT AFTERWARDS: `add_nests` reads settled anchors for its clearance test; `terrain_history` age advance reads `fisheries.ports[].core_id` to reap ports of dead cities; `hero_generator` counts `fisheries.ports` for the harbourmaster role and keys quest hooks on `port['id']`; `npc_roster.sites` enumerates `('fisheries','ports')`; the atlas/publishing layer reads `transport`, `world_economy`, `sky` and the `fishing_ground_owner` layer. `add_world_society` MUTATES blocks other passes own: it appends every port to `result['humans']['hamlets']`, appends port ids into `result['humans']['cores'][k]['hamlet_ids']`, writes `community_traits` onto each `settlements.sites` entry, and rewrites `result['regions']['landmarks']`. It does NOT mutate `result['seasonal_food']` (it deep-copies the models first). It is NOT idempotent: calling it twice without an intervening `add_humans` appends ports to `humans.hamlets` twice.

## Inputs

ALL LAYERS ARE n x n grids indexed `[z][x]`; the pass converts each to a per-node vector with `vals(k) = [l[k][z][x] for (x,z) in points]` where `points` is the `sphere_grid(n, r)` node list.

GEOMETRY / CONFIG
- `cfg.size` -> n (3..1025). `cfg.phase` (gates, see pipeline_position). `cfg.seed`. `cfg.world_recipe` (0 or 3). `cfg.magic_enabled` (0/1).
- `result['effective_config']['globe_radius']` -> `r`, PHYSICAL metres (design radius x world_scale). This is the radius used for every great-circle distance in this module.
- `result['spacing_m']` -- equatorial cell step in physical metres, `2*pi*r/(n-1)`. Used only as the air-route sampling step.
- `cfg.support_reach` -- metres, POST-SCALING (the request builder multiplies the authored 1000 m by `circumference_m / 11148.96...`; at the 200 km preset it resolves to ~17 943 m). Used as the coastal-hamlet search bound AND as the exponential decay length for delivery.
- `cfg.settlement_spacing` -- metres, POST-SCALING (authored 450 m). Witch-hut clearance from cities.
- `cfg.human_magic_limit`, `cfg.road_max_grade`, `cfg.bridge_cost` -- the last is read indirectly by `road_cost_function`; the first two are OVERRIDDEN per civilization before that call.

WORLD OPTIONS (`options(cfg)`, i.e. `validate_options(cfg.world_options, cfg.world_recipe)`) -- these are ABSOLUTE metres authored for the 11.15 km reference world and are NOT scaled by the request builder. Only `settlement_spacing`, `support_reach` and `culture_link_cost` get the reach factor; every option below stays at its literal value on a 200/400/600 km world.
- `coastal_hamlets` (int, default 2, 0..4) -- max ports per city.
- `sea_draft` (m, default 1.0, 0.1..10) -- minimum navigable depth for a landing and for a sea route.
- `fishing_reach` (m, default 700, 50..4000) -- base fishing allocation reach.
- `fish_productivity` (food units/km2/year, default 60, 0..200).
- `sea_reach` (m, default 6000, 100..50000), `sea_capacity` (food units/month, default 8), `air_reach` (m, default 4500), `air_capacity` (default 2).
- `seasonality` (default 1), `ice_accumulation` (default 0.5) -- read only by `make_sky`.
- `sky_clusters` (int, default 0, min 0, MAX 0), `sky_occurrence` (default 0, max 0), `sky_altitude` (450 m), `sky_radius` (180 m) -- see `open_questions`.

LAYERS (all required unless noted)
- `water_type` -- 0 dry, 1 ocean, 2 lake. `if water[i]` is truthy for lakes too; `water[i]==1` is ocean-only.
- `water_depth` (m, >=0), `height` (m, signed), `water_surface` (m; `make_sky` only).
- `flood_risk` (0..1), `rain_river` (0/1-ish truthiness).
- `magic_hazard` (0..1). OPTIONAL: if absent, a zero vector is used.
- `magic_risk_<civ_id>` for each civilization id. OPTIONAL per key; falls back to `magic_hazard`. TODAY EVERY ONE OF THESE IS A BYTE COPY OF `magic_hazard` (`terrain_leyline_history` writes `l['magic_risk_'+people] = node_grid(hazard, points, n)` for every people from the same list), so all the per-civilization risk branching in this module is currently a no-op. Keep the hook; it is where per-people mutation tolerance would land.
- `harbor_suitability` (0..1). Nonzero only on a LAND cell that is within `max(spacing_m,120)` m of ocean and has at least one neighbour with `water==1 and depth>=sea_draft`; value is `clamp((1-exposure)*0.7+0.3)*exp(-slope/22)`.
- `fishing_productivity` (0..1) -- nonzero on any water cell, `clamp(0.12+0.55*shallow+0.3*estuary+0.2*kelp+0.15*reef)`.
- `coastal_support` (0..1) -- land cells only: `harbor_suitability*0.5 + 0.5*max(fishing_productivity over neighbours)`.
- `island_habitat` (0.0/1.0) -- 1 where the cell's connected land component holds under 15% of world land area.
- `suitability` (0..1) -- the primary species' settlement score grid.
- `zone_witch_huts` (0..1), `zone_pirate` (0..1) -- regional influence fields.
- `ley_weave`, `ley_primordial` (0..~2) -- `ley_weave` is OPTIONAL at the air-terminal test (`l.get('ley_weave', zeros)`), required in `make_sky`.
- `temperature` (degC), `moisture` (0..1) -- `make_sky` only.

BLOCKS
- `result['settlements']['sites']` -- list; `site['id']` IS THE ORDINAL INDEX INTO THIS LIST (an int, not a string). Fields read: `id`, `node` (sphere-grid node index), `x`, `z`, `direction` (unit 3-vector), `population_profile` (civilization id string), `resource_potential` (0..1), `height_m`. Field written: `layer`, `mobility`, `community_traits`.
- `result['humans']['cores']` -- LIST parallel to `sites`, indexed by site ordinal. Reads `['culture_id']`, appends to `['hamlet_ids']`.
- `result['humans']['hamlets']`, `result['humans']['fortresses']` -- lists of rural rows; only `['node']` is read (to build the occupied set). Ports are appended to `hamlets`.
- `result['regions']['landmarks']` -- list; entries with `kind == 'witch_hut'` are counted then removed then replaced.
- `result['roads']['routes']` -- list of `{'from':int,'to':int,'nodes':[node...],'length_m':float,'cost':float,'river_crossings':[...]}`.
- `result['seasonal_food']['models']` -- list parallel to `sites`: `{'site_id','harvest':[12],'demand','storage','spoilage','transport_budget'}`. DEEP-COPIED, never mutated in place.
- `result['seasonal_food']['routes']` -- list parallel to `roads.routes`: `{'from','to','capacity':[12],'efficiency','price'}`.
- `result['seasonal_environment']['months'][m]['water_ice']` -- 12 grids, 0..1, `clamp((-1.8*salinity - temperature_month)/6)` on water, 0 on land.

CIVILIZATION REGISTRY (civilizations.json -> `entity_rules(id)` and `get_profile(id)`)
- `get_profile(id)['mutation_limit']` (0.45 human/dwarf, 0.55 gnome, 0.6 elf/tidekin), `['road_grade_limit']` (0.35..0.55), `['food_demand']`, `['food_temperature_tolerance']`, `['food_temperature_ideal']`, `['temperature_ideal']`, `['name']`.
- `entity_rules(id)['economy']['fishing_reach_multiplier']` (1.0; 1.25 human_cold and frosthold_dwarf; 1.5 tidekin).
- `entity_rules(id)['economy']['winter_fishing_fraction']` (0; 0.25 human_cold; 0.15 frosthold_dwarf).
- `entity_rules(id)['economy']['industrial_resource_min']` (null everywhere except gnome 0.5).
- `entity_rules(id)['economy']['air_terminal_resource_min']` (null everywhere except gnome 0.5).
- `entity_rules(id)['sky']['score_bias']`, `['moisture_score_weight']` -- `make_sky` only.

## Outputs

NEW BLOCKS

`result['sky']` (always written once phase>=6 and habitats exist):
- `version` = 1 (int)
- `islands` = [] -- see `make_sky`; empty on every world the generator can currently build
- `settlements` = sky_sites if `cfg.phase>=7` else []
- `area_km2` = sum of island `area_km2` (0.0)
- `enabled` = `cfg.world_recipe != 3` (bool)
- `disabled_reason` = 'Sky islands suspended in recipe 3' when recipe 3, else None/null
- `method` = 'Separate disk surfaces with rain capture, altitude climate and bounded food; static magical support.'

`result['fisheries']`:
- `ports` -- list of port records (below), in creation order
- `potential_annual_food` (float, food units/yr) -- summed over ALLOCATED cells only, never over all water
- `delivered_annual_food` (float) -- sum of every port's `delivered_food`
- `method` = 'One owner per reachable ocean cell; distance losses and monthly ice reduce delivery; civilization winter fishing retains a bounded fraction under ice. Potential capacity is never credited before allocation.'

PORT RECORD (this is also appended verbatim into `result['humans']['hamlets']`, where it sits beside `hamlet-N` rows that have a different field set). KEY FIELD IS `id`, A STRING: `f'coastal-{site_id}-{global_port_count}'` (e.g. `coastal-3-7`). It has NO `uid` and NO `core_id`->uid link, unlike every generated hamlet and fortress, so it is the one rural row that cannot be followed across an age boundary by uid. Its `node` is the stable key.
- `id` str, `kind` = 'hamlet' (constant), `role` str (see below), `layer` = 'surface'
- `node` int (land node), `sea_node` int (the water node it launches from), `landing_distance_m` float
- `x`,`z` ints (grid coords of `node`)
- `core_id` int -- the owning city's site ordinal
- `population_profile` str, `culture_id` str
- `height_m` float, `harbor_quality` float (= `harbor_suitability[node]`, always > 0.1)
- `trade_terminal` bool (`harbor_quality >= 0.4`)
- `access_nodes` list[int] -- ground path city-node -> port-node inclusive, city first
- `access_cost` float -- road cost from the city, metres-flavoured cost units
- `reason` = 'Safe ground connection to city; navigable landing; fishing allocated from exclusive grounds.'
- `worked_area_km2` float, `delivered_food` float, `delivered_materials` = 0.0 always, `irrigation_benefit` = 0.0 always
- `monthly_fish` list[12] float, `fishing_nodes` list[int] (allocated water nodes, claim order)
- `winter_fishing_fraction` float

`result['transport']`:
- `routes` -- the full multimodal route list, ORDER LOAD-BEARING (ground, then sea, then air)
- `terminals` -- list[int], indices into `all_sites` that have an air terminal
- `sites` -- `[{'id': site['id'], 'layer': site['layer']} for s in all_sites]`. `id` IS MIXED TYPE: int for surface sites, str for sky sites.

ROUTE RECORDS (all carry `capacity` list[12] >= 0, `efficiency` in (0,1], `price`, `from`, `to`; `from`/`to` are indices into `all_sites`):
- ground: `{**seasonal_food_route, 'mode':'ground', 'nodes':road['nodes'], 'length_m':road['length_m']}` -- so also `capacity`,`efficiency`,`price`,`from`,`to`
- sea: `{'from','to','mode':'sea','nodes':[water node...],'length_m','cost','ports':[id_a,id_b],'capacity':[12],'efficiency','price'}`
- air: `{'from','to','mode':'air','path':[[x,z],...],'length_m','cruise_altitude_m','capacity':[12],'efficiency','price'}` -- NOTE air has `path` (grid cells) and NO `nodes`; ground and sea have `nodes` (node indices) and no `path`.

`result['world_economy']`:
- `version` = 1
- `sites` -- `summarize_run` output, one row per `all_sites` entry in order: `{'site_id' (int or str), 'layer', 'name', 'annual_demand', 'annual_harvest', 'annual_shortage', 'food_coverage', 'lean_months':[1..12], 'ending_reserve'}`
- `models` -- the deep-copied seasonal models with fish folded into `harvest`, plus sky models appended
- `months` -- the raw 48-month `simulate_food` history
- `method` = 'Authoritative new-recipe four-year monthly stress test: existing farm surplus plus allocated fisheries and independent sky production; shared finite ground/sea/air throughput, material budgets, spoilage and loss. No same-month re-export.'
- `limits` = 'Artistic food units; static communities and routes. Surface farm production is rural export surplus; this is not demographic equilibrium or a vessel simulator.'

NEW LAYER
- `layers['fishing_ground_owner']` -- n x n, value is the owning PORT INDEX (position in `fisheries.ports`) or -1. Built with `node_grid`, so row 0 and row n-1 are the pole value repeated and column n-1 duplicates column 0.

MUTATIONS TO EXISTING BLOCKS
- `settlements.sites[k]['layer'] = 'surface'`, `['mobility'] = 'settled'`, `['community_traits'] = [...]` where the list is built as: (`['islander']` if the city has >=1 port AND `island_habitat[z][x]` is truthy, else `['maritime']` if it has >=1 port, else `[]`) + (`['industrial']` if `industrial_resource_min is not None and resource_potential > industrial_resource_min` else `[]`), and then `'pirate'` is appended later if `zone_pirate[z][x] > 0.02` AND at least one sea route names this site.
- `humans.cores[k]['hamlet_ids']` gains each port's string id, appended in port order.
- `humans.hamlets` gains every port record (extend, at the end).
- `regions.landmarks`: every entry with `kind == 'witch_hut'` is deleted and up to the same number of new ones are appended: `{'id': f'witch-hut-{node}', 'kind':'witch_hut', 'x','z', 'layer':'surface', 'intensity': zone_witch_huts[z][x], 'conventional_suitability': suitability[z][x], 'reason': 'Umbral influence, isolation and low conventional settlement suitability.'}`. NOTE the id spelling changes: the ecology pass mints `witch_huts-{node}` (underscore, plural); this pass mints `witch-hut-{node}` (hyphens, singular). There is no `node` field -- the node is embedded in the id.

## Algorithm

=====================================================================
HELPER 0 -- `trace(parent, start, end)`
=====================================================================
```
path = [end]
while path.back() != start:
    j = parent[path.back()]
    if j < 0: return []            # broken chain -> empty path
    path.push_back(j)
reverse(path); return path
```
Returns the node list from `start` to `end` inclusive. `trace(p, s, s)` returns `[s]`.

=====================================================================
HELPER 1 -- `water_cost(points, water, depth, hazard, limit, draft)`
=====================================================================
Returns an edge-cost callable `cost(i, j, d) -> optional<double>` for the sphere node
graph. `d` is the great-circle neighbour distance in metres.
```
lookup, n = sphere_index(points)     # {(x,z) -> node}, n = max(z)+1 = grid size
cost(i, j, d):
    if not (water[i]==1 and depth[i]>=draft and hazard[i]<=limit): return none
    if not (water[j]==1 and depth[j]>=draft and hazard[j]<=limit): return none
    (x,z)  = points[i];  (xx,zz) = points[j]
    if x != xx and z != zz and z not in (0, n-1) and zz not in (0, n-1):
        k = lookup[(x, zz)];  if not passable(k): return none
        k = lookup[(xx, z)];  if not passable(k): return none
    return d
```
The corner test stops a diagonal step slipping between two impassable corner cells.
The pole guard is necessary because a pole row has only one distinct cell, so
`(x, 0)` for x != 0 is not a key.

PERFORMANCE NOTE CARRIED FROM THE SOURCE, VERBATIM IN INTENT: the three-comparison
`passable()` predicate is deliberately INLINED into `cost` rather than called,
because "the routing search calls this several million times per world and the call
overhead dominated the three comparisons." `sphere_index` is likewise memoised on
the identity of the points list: "One `add_world_society` at size 128 built it
3 886 times for twelve seconds."

=====================================================================
HELPER 2 -- `fishing_access_factor(ice, winter_fraction)`
=====================================================================
```
ice = clamp(ice, 0, 1);  wf = clamp(winter_fraction, 0, 1)
return 1 - ice + ice*wf
```
"Fraction of the same finite fish harvest accessible under route ice." At ice=0 it is
1; at ice=1 it is exactly `winter_fraction`. It NEVER exceeds 1, so it can only
reduce a harvest -- it is not a cold-weather bonus.

=====================================================================
HELPER 3 -- two search routines this module borrows
=====================================================================
`shortest_paths(graph, start, cost, targets, limit) -> (distances, parent)`
Plain binary-heap Dijkstra. `distances` initialised to +inf except `distances[start]=0`;
`parent` to -1. Pop `(value, i)`; skip if `value != distances[i]` (lazy deletion). If
`targets` is supplied, erase `i` from the remaining set and BREAK when it empties.
Relax `j` only when `candidate <= limit AND candidate < distances[j]`.
Tie-break: none beyond the heap's own `(value, node)` ordering -- ties on `value`
resolve by smaller node index because the heap compares the pair.
SOURCE ARGUMENT FOR THE BOUND, PRESERVE IT: "Stopping at the reach is not an
approximation -- edge costs are non-negative, so every prefix of a path costing
`limit` or less also costs `limit` or less, and a node inside the bound keeps the
same distance and the same parent it had when the search was unbounded. Nodes
outside it stay at infinity rather than taking their true value, so read a bounded
result only inside the bound."

`allocate_access(graph, seeds, cost, limit) -> (distance, owner, parent)`
Multi-source Dijkstra with an owner label. Heap entries are `(value, label, node)`.
Seeds start at distance 0 with their label. Skip a pop if
`value != distance[i] or label != owner[i]`. Relax when
`candidate <= limit AND (candidate < distance[j] OR (candidate == distance[j] AND label < owner[j]))`.
The label tie-break makes an equidistant cell go to the LOWER-NUMBERED seed.
This module always calls it with exactly ONE seed labelled 0, so only the bound and
the parent tree matter here.

=====================================================================
MAIN -- `add_world_society(result, cfg)`
=====================================================================

STEP 0 -- gate and sky
```
if cfg.phase < 6 or 'habitats' not in result: return        # writes nothing at all
islands, sky_sites, sky_models = make_sky(result, cfg)
result['sky'] = { ...as in `writes`... }
if cfg.phase < 9 or 'seasonal_food' not in result: return
```

STEP 1 -- load
```
o = options(cfg); l = result['layers']; n = cfg.size
r = result['effective_config']['globe_radius']
points, areas, graph = sphere_grid(n, r)
water = vals('water_type'); depth = vals('water_depth'); height = vals('height')
flood = vals('flood_risk'); river = vals('rain_river')
hazard = vals('magic_hazard') if present else zeros(len(points))
harbor = vals('harbor_suitability'); fish = vals('fishing_productivity')
sites  = result['settlements']['sites']
models = deep_copy(result['seasonal_food']['models'])
```

STEP 2 -- WITCH HUTS (re-siting)
Intent comment, preserve it: "Huts follow their own preference for isolation and poor
conventional suitability."
```
wanted = count of landmarks with kind == 'witch_hut'        # counted BEFORE removal
landmarks = [s for s in landmarks if s.kind != 'witch_hut']  # in place
hut_nodes = []
for i in nodes sorted by DESCENDING zone_witch_huts[z][x]    # stable: ties -> ascending node index
    if len(hut_nodes) >= wanted: break                       # fires immediately when wanted == 0
    (x, z) = points[i]
    if water[i] != 0:                        continue        # rejects both ocean and lake
    if suitability[z][x] >= 0.55:            continue        # must be POOR conventional ground
    if zone_witch_huts[z][x] < 0.02:         continue
    p = direction(x, z, n)
    if any(r*angle(p, site.direction) < cfg.settlement_spacing for site in sites): continue
    if any(r*angle(p, direction(points[j], n)) < 200 for j in hut_nodes):          continue
    hut_nodes.append(i)
    landmarks.append({... witch hut record ...})
```
`angle(a,b) = acos(clamp(dot(a,b), -1, 1))`. `cfg.settlement_spacing` is POST-scaling;
the 200 m hut-to-hut separation is a bare literal that is NOT scaled with world width.
Cost: one O(N log N) sort of the whole node set even when `wanted` is 0.

STEP 3 -- COASTAL PORTS (one search per city)
Intent comment, preserve it verbatim in spirit: "Fortresses belong in this set as much
as cities and hamlets do. Leaving them out let a harbour be founded on a node a fort
already held -- 28 of 56 forts in a seed-42 size-33 world shared their exact node with
a coastal hamlet, which draws as one icon on top of another and means two settlements
occupy one place."
```
occupied = set(node for every site, every existing hamlet, every fortress)
ports = []
ground_candidates = [i for i in 0..N-1 if harbor[i] > 0.1]   # ascending node index

for site in sites:                                            # founding order
    site['layer'] = 'surface'; site['mobility'] = 'settled'
    profile = get_profile(site.population_profile)
    risk    = vals('magic_risk_' + profile_id) if present else hazard
    cost    = road_cost_function(points, water, height, flood, river,
                                 cfg with human_magic_limit := profile.mutation_limit
                                     and road_max_grade   := profile.road_grade_limit,
                                 risk)
    distances, parents = shortest_paths(graph, site.node, cost,
                                        targets = ground_candidates,
                                        limit   = cfg.support_reach)
    eligible = [i in ground_candidates : distances[i] <= support_reach and i not in occupied]
    ranked   = stable_sort(eligible, key = -(harbor[i] + 0.5*coastal_support[z][x])
                                            * exp(-distances[i]/cfg.support_reach))
    for node in ranked[:o['coastal_hamlets']]:
        access = [(j,d) for (j,d) in graph[node]
                  if water[j]==1 and depth[j] >= o['sea_draft'] and risk[j] <= profile.mutation_limit]
        if access is empty: continue          # slot is consumed, no port results
        (sea_node, landing_distance) = min(access, key = d)    # tie -> lowest neighbour index
        occupied.add(node)
        ports.append({ ... port record, access_nodes = trace(parents, site.node, node) ... })

    city_ports = [p in ports : p.core_id == site.id]
    industrial_min = entity_rules(profile_id)['economy']['industrial_resource_min']
    site['community_traits'] = (['islander'] if city_ports and island_habitat[site.z][site.x]
                                else ['maritime'] if city_ports else []) \
                             + (['industrial'] if industrial_min is not None
                                and site.resource_potential > industrial_min else [])
```
OPTIMISATION 1 OF 3, EQUIVALENCE ARGUMENT AS WRITTEN: "Only landings inside
support_reach are ever eligible, so the search stops there rather than crossing the
whole landmass to price candidates it will discard." Unverified by test. It is sound
for the same prefix reason as the quoted `shortest_paths` docstring, given that the
only consumer of `distances` is the `<= support_reach` filter and the ranking key,
which is only evaluated on nodes that passed that filter.

`road_cost_function(points, water, height, flood, river, cfg, hazard)`:
```
cost(i, j, d):
    if water[i] or water[j] or d <= 0:                          return none
    if hazard and max(hazard[i],hazard[j]) > cfg.human_magic_limit: return none
    if xi != xj and zi != zj and zi not in (0,n-1) and zj not in (0,n-1):
        if water[(xi,zj)] or water[(xj,zi)]:                    return none
        if hazard and max(hazard[(xi,zj)],hazard[(xj,zi)]) > limit: return none
    grade = |height[j] - height[i]| / d
    if grade > cfg.road_max_grade:                              return none
    return d*(1 + 12*grade^2 + 0.5*(flood[i]+flood[j])) + (cfg.bridge_cost if river[i] or river[j] else 0)
```
Note `access_cost` is therefore a COST, not a length: it includes the grade and flood
surcharges and the bridge penalty, and it is nonetheless divided by `support_reach`
(a length) in the delivery exponential.

STEP 4 -- EXCLUSIVE FISHERIES
Intent comment: "Each port searches only accessible water. Globally award each cell once."
```
claims = ordered_map<node, (distance, port_index)>()    # INSERTION ORDER MATTERS
fishing_ice = []                                        # one map per port
for k, port in enumerate(ports):
    profile = get_profile(port.population_profile)
    reach   = o['fishing_reach'] * entity_rules(pid)['economy']['fishing_reach_multiplier']
    risk    = vals('magic_risk_' + pid) if present else hazard
    cost    = water_cost(points, water, depth, risk, profile.mutation_limit, 0.1)
    distances, _, parents = allocate_access(graph, [(port.sea_node, 0)], cost, reach)

    path_ice = map<node, array<double,12>>()
    for i in stable_sort([i : distances[i] <= reach], key = distances[i]):
        (x,z) = points[i]
        upstream = path_ice.get(parents[i], zeros12)     # parents[seed] == -1 -> zeros
        path_ice[i][m] = max(upstream[m], water_ice[m][z][x])   for m in 0..11
    fishing_ice.append(path_ice)

    for i, d in enumerate(distances):
        if d > reach: continue
        if (d, k) < claims.get(i, (+inf, 0)): claims[i] = (d, k)
```
`(d,k) < (d2,k2)` is lexicographic: nearer wins; on an exact tie the LOWER PORT INDEX
wins, i.e. the port belonging to the earlier-founded city.
`path_ice` is a running MAXIMUM of monthly sea ice along the allocation tree path from
the port's sea node to the cell -- a route that passes through a frozen strait is
frozen for that month even if the fishing ground itself is open. Parent-before-child
is guaranteed because every edge cost is `d > 0`, so tree distances strictly increase.
NOTE THE DRAFT: fishing uses a hardcoded `draft = 0.1` m, NOT `o['sea_draft']`. A
fishing boat can work water a trading vessel cannot enter.
OPTIMISATION 2 OF 3: `allocate_access` is bounded at `reach`. Same prefix argument.
Unverified by test.

STEP 5 -- AWARD AND PRICE
```
owner = [-1] * N; production = 0.0
for (i, (distance, k)) in claims:                       # INSERTION ORDER
    port = ports[k]; owner[i] = k; port.fishing_nodes.append(i)
    area   = areas[i] / 1e6                             # m2 -> km2
    annual = area * fish[i] * o['fish_productivity']    # food units/year
    production += annual
    delivery = exp(-(distance + port.access_cost) / max(1, cfg.support_reach))
    port.worked_area_km2 += area
    for m in 0..11:
        ice = fishing_ice[k][i][m]
        port.monthly_fish[m] += annual/12 * delivery * fishing_access_factor(ice, port.winter_fishing_fraction)

for port in ports:
    port.delivered_food = sum(port.monthly_fish)
    port.role = 'harbor + fishing' if trade_terminal and delivered_food > 0
           else 'harbor'           if trade_terminal
           else 'fishing'          if delivered_food > 0
           else 'landing'
    models[port.core_id].harvest[m] += port.monthly_fish[m]   for m in 0..11
    humans.cores[port.core_id].hamlet_ids.append(port.id)

humans.hamlets.extend(ports)
layers['fishing_ground_owner'] = node_grid(owner, points, n)
result['fisheries'] = { ports, potential_annual_food = production,
                        delivered_annual_food = sum(p.delivered_food), method }
```
`potential_annual_food` is the sum over ALLOCATED cells only. The method string says
so: "Potential capacity is never credited before allocation." `delivered_annual_food`
is provably <= it, because `delivery <= 1` and `fishing_access_factor <= 1`.

STEP 6 -- GROUND ROUTES
```
routes = []
for (road, model) in zip(roads.routes, seasonal_food.routes):      # same length, same order
    routes.append({**model, mode:'ground', nodes: road.nodes, length_m: road.length_m})
```

STEP 7 -- SEA ROUTES
Intent comment, preserve it verbatim in spirit: "A route is priced pair by pair, but
the search behind it only ever depends on where it starts and which mutation limit it
respects, and nothing past sea_reach can survive the length test below -- a landing
distance is never negative. So the search runs once per (origin, limit), bounded, and
the pair loop reads it. The loop keeps its original order and its original tests:
sea_pairs resolves a duplicate by first writer, and its insertion order is the order
the sea routes are emitted in."
```
sea_pairs  = ordered_map<(int,int), route>()
sea_costs  = map<double, cost_fn>()                    # shared across outer ports
sea_limits = {pid: get_profile(pid).mutation_limit for pid in SORTED set of port profiles}

for a, port in enumerate(ports):
    searches = map<double, (distances, parent)>()      # RESET per outer port
    for other in ports[a+1:]:
        if not port.trade_terminal or not other.trade_terminal: continue
        if port.core_id == other.core_id:                       continue
        limit = min(sea_limits[port.pid], sea_limits[other.pid])
        if limit not in searches:
            if limit not in sea_costs:
                sea_costs[limit] = water_cost(points, water, depth, hazard, limit, o['sea_draft'])
            searches[limit] = shortest_paths(graph, port.sea_node, sea_costs[limit],
                                             targets = none, limit = o['sea_reach'])
        distances, parent = searches[limit]
        length = distances[other.sea_node] + port.landing_distance_m + other.landing_distance_m
        if length > o['sea_reach']: continue            # also catches distances == +inf
        path = trace(parent, port.sea_node, other.sea_node)
        (a_id, b_id) = sorted((port.core_id, other.core_id))
        value = length + port.access_cost + other.access_cost
        if (a_id,b_id) in sea_pairs and sea_pairs[(a_id,b_id)].cost <= value: continue
        capacity[m] = o['sea_capacity'] * min(port.harbor_quality, other.harbor_quality)
                                        * (1 - max(water_ice[m][z][x] for node in path, default 0))
        sea_pairs[(a_id,b_id)] = {from:a_id, to:b_id, mode:'sea', nodes:path, length_m:length,
                                  cost:value, ports:[port.id, other.id], capacity,
                                  efficiency: exp(-value/12000), price: 0.2 + value/10000}
routes.extend(sea_pairs.values())
```
OPTIMISATION 3 OF 3, THE ONE THAT REMOVED THE QUADRATIC. Before the rewrite every
pair rebuilt a full cost vector and ran one unbounded Dijkstra across the whole
sphere graph; at size 128 that was 3 828 unbounded ocean searches to keep four
routes. `add_world_society` went 213.3 s -> 1.85 s (fitted exponent 2.05 -> 0.80) and
the whole size-257 projection went from ~4 750 s to ~790 s. The equivalence claim has
THREE parts and none of them is covered by a test: (i) the cost function depends only
on the mutation limit, so memoising it on `limit` is exact; (ii) the search depends
only on `(origin, limit)`, so memoising it per outer port is exact; (iii) bounding at
`sea_reach` cannot lose a pair, because both landing distances are non-negative and
the length test rejects anything above `sea_reach` anyway.
SUBTLETY: `searches` is reset per outer port while `sea_costs` is not -- the cost
closure is reusable across origins, the distance array is not.
SUBTLETY: the sea pass uses the GENERIC `hazard` layer, not the per-civilization
`magic_risk_<pid>`, unlike steps 3 and 4. Today they are identical data.
SUBTLETY: one route per (city, city) pair, keyed on CORE ids, not on port ids. Two
ports of the same city never route to each other. The cheapest pair of ports wins;
a strict `<=` means the FIRST writer keeps the slot on an exact tie, and a later
cheaper replacement overwrites the value while KEEPING the original insertion
position in the map.

STEP 8 -- AIR ROUTES
Intent comment: "Air terminals require industry or stable Weave access; sky
settlements have a landing surface."
```
all_sites = sites + sky_sites;  models.extend(sky_models)
terminals = []
for i, site in enumerate(all_sites):
    (x,z) = (site.x, site.z);  is_sky = (i >= len(sites))
    air_min = entity_rules(site.population_profile)['economy']['air_terminal_resource_min']
    if is_sky or (air_min is not None and site.resource_potential > air_min) \
       or ley_weave_or_zero[z][x] > 0.12:
        terminals.append(i)

for index, a in enumerate(terminals):
  for b in terminals[index+1:]:
    aa, bb = all_sites[a], all_sites[b];  va, vb = aa.direction, bb.direction
    alt_a = aa.get('altitude_m', aa.get('height_m', 0));  alt_b = same for bb
    length = hypot(r*angle(va,vb), alt_b - alt_a)
    if length > o['air_reach']: continue
    limit = min(get_profile(aa.pid).mutation_limit, get_profile(bb.pid).mutation_limit)
    safe = true; path = []
    steps = max(2, ceil(length / max(result['spacing_m'], 1)))
    for j in 0..steps:
        t = j/steps
        p = va*(1-t) + vb*t;  norm = |p|
        if norm < 1e-8: safe = false; break            # antipodal endpoints
        p /= norm
        x = round((atan2(p.z, p.x) + pi)/(2*pi)*(n-1))
        z = round(acos(clamp(p.y,-1,1))/pi*(n-1))
        if max over s in (aa,bb) of risk_layer(s.pid)[z][x] > limit: safe = false; break
        path.append([x, z])
    cruise = max([alt_a, alt_b] + [height_grid[z][x] + 80 for (x,z) in path])
    length = r*angle(va,vb) + max(0, cruise-alt_a) + max(0, cruise-alt_b)
    if safe and length <= o['air_reach']:
        routes.append({from:a, to:b, mode:'air', path, length_m:length,
                       cruise_altitude_m:cruise, capacity:[o['air_capacity']]*12,
                       efficiency: exp(-length/8000), price: 0.5 + length/5000})
```
The route is sampled along the great circle by normalised linear interpolation, which
is NOT constant-speed (slerp) -- samples bunch toward the midpoint on a long arc. That
is what the code does; keep it. The magic test uses `l.get('magic_risk_'+pid,
l.get('magic_hazard', zeros))[z][x]` per endpoint civilization and takes the max of the
two; it is evaluated at the SAMPLE cell, not at the endpoints. `length` is recomputed
AFTER the cruise altitude is known: horizontal great circle plus the climb at each end,
so the `air_reach` test runs twice against two different lengths.

STEP 9 -- PIRATE TRAIT
```
for site in sites:
    if zone_pirate[site.z][site.x] > 0.02 and
       any(route.mode == 'sea' and site.id in (route.from, route.to) for route in routes):
        site.community_traits.append('pirate')
```
Piracy therefore requires BOTH a pirate region AND a live sea link -- a pirate coast
with no trade to prey on gets no trait.

STEP 10 -- SIMULATE AND SUMMARISE
```
run = simulate_food(models, routes, years=4)
result['transport']    = {routes, terminals, sites: [{id, layer} for all_sites]}
result['world_economy']= {version:1, sites: summarize_run(run, models, all_sites),
                          models, months: run.months, method, limits}
```
`summarize_run(run, models, sites)`:
```
for i, site in enumerate(sites):
    records = [month.cities[i] for month in run.months[-12:]]   # the FOURTH year only
    demand   = sum(v.demand   for v in records)
    shortage = sum(v.shortage for v in records)
    append {site_id: site.id, layer: site.get('layer','surface'), name: site.name,
            annual_demand: demand, annual_harvest: sum(v.harvest for v in records),
            annual_shortage: shortage,
            food_coverage: 1 - shortage/demand if demand else 1.0,
            lean_months: [m+1 for m,v in enumerate(records) if v.shortage > 1e-8],
            ending_reserve: records[-1].closing}
```

`simulate_food(cities, roads, years=4)` is owned by the seasons module but this pass
is its only authoritative caller, so the contract it requires of the routes built here
is: each route needs `from`, `to` (indices into `cities`), `capacity[12]`, `efficiency`
(skipped entirely when <= 0) and `price` (default 1.0). Per month it iterates the route
list IN ORDER and, for each route, both directions `(a->b)` then `(b->a)`:
`sent = min(exportable[seller], remaining_capacity, need/efficiency, budget[buyer]/price/efficiency)`.
Earlier routes therefore get first claim on a seller's surplus and on a buyer's
material budget -- which is why the ground/sea/air concatenation order is load-bearing.

=====================================================================
`make_sky(result, cfg)` -- STRUCTURALLY DEAD, SPECIFIED FOR COMPLETENESS
=====================================================================
```
if cfg.world_recipe == 3: return [], [], []             # recipe 3 is the ONLY shipped recipe
o = options(cfg); points,_,_ = sphere_grid(n, r)
rng = Random(child_seed(cfg.seed, 'sky-archipelagos-v1'))
if not cfg.magic_enabled: return [], [], []
candidates = [(x,z) for (x,z) in points if max(ley_weave[z][x], ley_primordial[z][x]) > 0.06]
rng.shuffle(candidates); centers = []
for k in 0 .. o['sky_clusters']-1:                      # sky_clusters has min == max == 0
    if not candidates: break
    center = first (x,z) in candidates with angle(direction(x,z,n), c) > 0.25 for every c in centers
    if center is none: break
    centers.append(direction(center, n))                # appended BEFORE the occurrence roll
    if rng.random() > o['sky_occurrence']: continue     # sky_occurrence locked at 0.0
    (cx, cz) = center
    for j in 0..2:
        x = (cx + (j-1)*2) mod (n-1)
        z = clamp(cz + rng.randint(-2, 2), 1, n-2)
        radius   = o['sky_radius'] * rng.uniform(0.7, 1.3)
        altitude = max(height[z][x], water_surface[z][x]) + o['sky_altitude']*rng.uniform(0.8, 1.2)
        temp = temperature[z][x] - 0.0065*(altitude - height[z][x])     # dry lapse rate, C/m
        wet  = moisture[z][x]
        temps = monthly_temperatures(temp, 90 - 180*z/(n-1), wet, o['seasonality'])
        cold  = cold_habitat(temps, wet, o['ice_accumulation'])
        area  = pi*radius^2 / 1e6                                       # km2
        freshwater = area * wet * 160        # "provisional rain capture residents-equivalent budget"
        food = area * 80 * clamp(1 - |temp-16|/35) * wet * (0 if cold=='ice_cap' else 1)
        potential = floor(min(freshwater, food*1.5))
        sky_environment = {biome: 7 if wet>=0.7 and temp>20 else 3, temperature: temp,
                           moisture: wet, maritime: 0.0, landmass_area_m2: area*1e6,
                           landmass_fraction: 1.0, largest_landmass: true}
        eligible = eligible_civilizations(sky_environment)
        people   = eligible if cfg.population_profile=='mixed' else [p in eligible : p == cfg.population_profile]
        people_rng = Random(child_seed(cfg.seed, f'sky-population-{k}-{j}-v1'))
        profile_id = argmax over p in people of
              ( -|get_profile(p).temperature_ideal - temp|
                + people_rng.uniform(0, 8)
                + sky_rules[p].score_bias + sky_rules[p].moisture_score_weight * wet )
              (none if people is empty; argmax keeps the FIRST maximum)
        hazard_here = (magic_risk_<pid> or magic_hazard)[z][x] if pid else 0
        population  = potential if profile and hazard_here <= profile.mutation_limit else 0
        ... build the disk mesh, append the island ...
        if population < 4: continue
        ... append the settlement site and its food model ...
```
DISK MESH (local metres, "east, radial altitude, north"), an independent surface,
NEVER a displacement of the ground below:
- vertex 0 = `[0, altitude, 0]` (apex)
- 4 rings x 24 sectors = 96 more vertices; for ring in 1..4, a in 0..23:
  `theta = 2*pi*a/24`, `rr = radius*ring/4`,
  `relief = radius*0.08*sin(3*theta + j)*sin(pi*ring/4)`,
  vertex = `[rr*cos(theta), altitude + relief, rr*sin(theta)]`
- 24 fan triangles `[0, 1+a, 1+(a+1)%24]`
- 3 quad bands x 24 x 2 triangles: with `a0=1+ring*24+a`, `a1=1+ring*24+(a+1)%24`,
  `b0=a0+24`, `b1=a1+24`, emit `[a0,b0,b1]` then `[a0,b1,a1]`
- totals: 97 vertices, 168 triangles
ISLAND RECORD: `id = f'sky-{k}-{j}'` (this is also the `layer` name), `cluster_id =
f'sky-{k}'`, plus `x, z, direction, radius_m, altitude_m, area_km2, temperature_c,
moisture, cold_habitat, food_potential, freshwater_capacity, magic_support (=
max(ley_weave, ley_primordial)), mesh, months[12] = {temperature, snow =
clamp(-t/8)*clamp(2*wet)}`.
SETTLEMENT RECORD: `id = island_id + '-settlement'` (STRING -- this is why
`world_economy.sites[].site_id` is a mixed int/str field), `layer = island_id`, `kind
= 'sky_settlement'`, `population_profile`, `civilization_id`, `x, z, direction,
altitude_m, population_estimate, name = f'{profile.name} Skyhaven {k+1}.{j+1}',
freshwater_capacity, reason`.
FOOD MODEL: `demand = population*food_demand/100/12`, `storage = demand*4`,
`spoilage = 0.02`, `transport_budget = area*40/12`, `harvest =
seasonal_harvest(food, wet, 90-180*z/(n-1), temp, food_temperature_tolerance,
food_temperature_ideal, o['seasonality'])`.
`seasonal_harvest(annual, wet, lat, mean_temp, tol, ideal, seasonality)`:
```
amplitude = 20*sin(radians(lat))*(1 - 0.4*wet)*seasonality
baseline  = max(0.05, exp(-((mean_temp-ideal)/tol)^2))
for month 0..11:
    t = mean_temp + amplitude*cos(2*pi*(month-6)/12)
    growth[month] = min(1.5, exp(-((t-ideal)/tol)^2) * clamp(t/8, 0, 1) / baseline)
return [annual * growth[(month-1) mod 12] / 12 for month in 0..11]   # ONE-MONTH HARVEST LAG
```

## Constants

HARD LITERALS IN `add_world_society` (none of these is a config knob; every one is a
tuning decision baked into the source)

- `0.1` -- `harbor_suitability` floor for a node to be a port candidate. Raising it
  yields fewer, better harbours; at 0 every coastal land cell competes and the ranked
  sort does all the work. Dimensionless.
- `0.55` -- `suitability` ceiling above which a node is too good for a witch hut.
  Raising it lets huts sit on prime settlement ground, which defeats the "isolation
  and poor conventional suitability" intent.
- `0.02` -- `zone_witch_huts` floor for a hut, and separately the `zone_pirate` floor
  for the pirate trait. Both dimensionless intensity thresholds.
- `200` metres -- minimum hut-to-hut great-circle separation. ABSOLUTE, NOT SCALED by
  world width; on the 200 km preset this is roughly a hundredth of a cell, so it never
  binds in practice and huts are separated only by the `settlement_spacing` test
  against cities and by the `wanted` count.
- `0.5` -- weight on `coastal_support` in the port ranking key
  `(harbor + 0.5*coastal_support)*exp(-d/support_reach)`. Raising it favours cells
  near productive fishing water over cells that are simply good anchorages.
- `0.4` -- `harbor_quality` threshold for `trade_terminal`. This is the single most
  consequential number in the module: it decides which ports can carry sea trade at
  all, so raising it can empty the entire sea-route network.
- `0.1` metres -- fishing draft in `water_cost`. Deliberately below `o['sea_draft']`
  (1.0 m): fishing boats work water a trader cannot enter. Raising it to `sea_draft`
  would shrink every fishery to navigable water.
- `max(1, support_reach)` in the delivery exponential -- guards a division by zero if
  support_reach were ever under 1 m. Config bounds it at >= 100, so it is dead armour.
- `1e6` -- m2 -> km2 for cell areas.
- `12` -- months per year, everywhere (`annual/12`).
- `12000` metres -- sea-route efficiency decay length, `efficiency = exp(-cost/12000)`
  where `cost` is `length + both access costs`. Shorter -> sea trade loses more in
  transit. ABSOLUTE, unscaled: on a 200 km world a 6 km sea route (the reach ceiling)
  plus two access costs sits around e^-1, which is roughly the intended shape only on
  the 11.15 km reference world.
- `10000` metres -- sea-route price scale, `price = 0.2 + cost/10000` in material
  units per delivered food unit. `0.2` is the floor charge.
- `8000` metres -- air-route efficiency decay length, `efficiency = exp(-length/8000)`.
- `5000` metres -- air-route price scale, `price = 0.5 + length/5000`. `0.5` is the
  floor charge; air is deliberately priced above sea's 0.2.
- `0.12` -- `ley_weave` threshold that grants a city an air terminal without industry.
  This is the ONLY route by which a non-gnome civilization gets air transport, because
  `air_terminal_resource_min` is null for all eleven others.
- `80` metres -- terrain clearance added above every sampled cell when choosing the
  cruise altitude. Raising it lengthens every air route (the climb is charged twice).
- `1e-8` -- degenerate-direction guard for antipodal air endpoints.
- `2` -- minimum sampling steps for an air route, `steps = max(2, ceil(length/spacing))`.
- `1e-8` -- shortage epsilon in `summarize_run`'s `lean_months` test.
- `-12` slice -- `run['months'][-12:]`: only the FOURTH simulated year is summarised.

CONSTANTS INSIDE `make_sky`
- `0.06` ley threshold for a candidate cell; `0.25` radians minimum angular separation
  between cluster centres; `3` islands per cluster at columns `cx-2, cx, cx+2`;
  `randint(-2,2)` row jitter clamped to `1 .. n-2`; `uniform(0.7,1.3)` radius jitter;
  `uniform(0.8,1.2)` altitude jitter.
- `0.0065` degC per metre -- dry adiabatic lapse rate for island temperature.
- `160` -- rain-capture budget, `freshwater = area_km2 * moisture * 160` in
  residents-equivalent units. The source calls this "provisional".
- `80`, `16`, `35` -- `food = area*80*clamp(1-|temp-16|/35)*wet*(0 if ice_cap else 1)`:
  80 food units per km2 at the optimum, 16 degC optimum, 35 degC half-width.
- `1.5` -- `potential = floor(min(freshwater, food*1.5))`: food is allowed to support
  half again as many residents as water before water binds.
- `4` residents -- minimum population for a sky settlement to exist.
- `0.08`, `3`, `4`, `24` -- mesh relief amplitude fraction, angular relief frequency,
  ring count, sector count.
- `0.02` sky spoilage, `4` months of sky storage, `40` transport budget per km2 per
  year, `100` divisor on `food_demand`.
- `7`/`3` biome ids for the synthetic sky environment; `0.7` moisture and `20` degC
  split between them.

BORROWED CONSTANTS THAT REACH THIS MODULE'S OUTPUT
- `road_cost_function`: `12*grade^2` grade surcharge, `0.5*(flood_i+flood_j)` flood
  surcharge, `cfg.bridge_cost` (default 200) river penalty. These are inside
  `access_cost` and therefore inside every port's `delivery` and every sea route's
  `cost`, `efficiency` and `price`.
- `fishing_access_factor` has no free constants; it is `1-ice+ice*wf` exactly.

## Data files

ONE data file, read indirectly.

`Sim/icarus_sim/civilizations.json` (schema_version 6, ~9 000 lines), loaded and
validated by `civilization_registry._document()`, which also requires a sibling
`buildings.json` linked as `{"path":"buildings.json","schema_version":2}`. Top-level
keys: `schema, schema_version, revision, description, defaults, city_classification,
configurations, building_catalogue, civilization_order, parent_races, founding_rules`.

The twelve civilizations are NESTED under `parent_races[<race>]['civilizations'][<id>]`
and flattened by the registry into `doc['entities']`, ordered by the
`civilization_order` array (which must name every entity exactly once):
`human_maritime, human_desert, human_cold, human_large_island, human_rainforest,
human_heartland, dwarf, elf, gnome, tidekin, hill_dwarf, frosthold_dwarf`.
`configurations` holds exactly one aggregate profile, `mixed`, which has NO `economy`
and NO `sky` block -- a port whose `population_profile` were `mixed` would KeyError.
That cannot happen because site profiles are always concrete entity ids.

This module reads two sub-objects per entity.

`entities[<id>]['economy']` (read via `entity_rules(id)['economy']`):
```
{ "fishing_reach_multiplier": 1.0,      // 1.25 human_cold, 1.25 frosthold_dwarf, 1.5 tidekin
  "industrial_resource_min": null,      // 0.5 gnome only
  "air_terminal_resource_min": null,    // 0.5 gnome only
  "winter_fishing_fraction": 0 }        // 0.25 human_cold, 0.15 frosthold_dwarf
```
The two `*_resource_min` fields are NULLABLE and the null is meaningful: `null` means
the civilization never qualifies by resources, and the code tests
`air_min is not None and resource_potential > air_min`. A port must carry three-valued
logic here, not a sentinel like -1, or every civilization becomes industrial.

`entities[<id>]['sky']` (read only by the dead `make_sky`):
```
{ "score_bias": 0,            // +4 gnome, -10 tidekin
  "moisture_score_weight": 0 }  // 3 elf
```

`entities[<id>]['population']` (read via `get_profile(id)`) supplies
`mutation_limit`, `road_grade_limit`, `food_demand`, `food_temperature_tolerance`,
`food_temperature_ideal`, `temperature_ideal` and `name`. Representative values:
`human_heartland` -> `{name:'Heartland humans', mutation_limit:0.45,
road_grade_limit:0.35, food_demand:10, food_temperature_tolerance:28,
food_temperature_ideal:18, temperature_ideal:18}`; `dwarf` -> `road_grade_limit:0.55,
food_temperature_ideal:9`; `elf`/`tidekin` -> `mutation_limit:0.6, food_demand:8`;
`gnome` -> `mutation_limit:0.55`; `frosthold_dwarf` -> `temperature_ideal:-8`.

SELECTION: entries are selected by the `population_profile` string already stored on
each site by `add_settlements`; this module never chooses a civilization on the live
path. `make_sky` would choose one via `eligible_civilizations(environment)`, which
iterates `sorted(registry, key=lambda k: (civilization.priority, k))`, skips
non-`entity` kinds and any entity whose `allocation_group` is already claimed, and
keeps those whose `habitat` rules match the environment.

REGISTRY IDENTITY WARNING CARRIED FROM THE PROJECT RULES: bytes in
`civilizations.json` invalidate every saved world, and an age advance refuses a world
whose registry identity differs. In C++ these twelve economy/sky records should be
data, not compiled-in constants, for exactly that reason -- but the file's revision
hash is part of world identity, so the C++ must expose the same identity check.

## Random streams

ON THE LIVE PATH: NONE. `add_world_society` draws zero random numbers. Every
`random.Random` in the module is inside `make_sky`, which returns before its first
draw on recipe 3 (the only shipped recipe) and whose one loop cannot execute anyway
because `sky_clusters` is bounded `min == max == 0`. The pass is a deterministic
function of `(result, cfg)` and nothing else. A C++ port that threads an `Rng` into
this stage at all has added a dependency the reference does not have; do NOT invent a
fourth seed derivation for it.

IF `make_sky` IS EVER REVIVED (recipe 4 territory), the two streams are:
- `random.Random(child_seed(cfg.seed, 'sky-archipelagos-v1'))` -- the cluster stream.
  Draw order per generation: one `shuffle(candidates)` over the full candidate list,
  then per cluster k: one `random()` for the occurrence roll (drawn AFTER the centre
  is chosen and appended, so a rejected cluster still consumes a centre slot and still
  blocks its neighbourhood), then per island j in 0..2: one `randint(-2,2)`, one
  `uniform(0.7,1.3)`, one `uniform(0.8,1.2)`, in that order.
- `random.Random(child_seed(cfg.seed, f'sky-population-{k}-{j}-v1'))` -- a fresh
  stream per island, used for exactly `len(people)` `uniform(0,8)` draws, one per
  eligible civilization, consumed inside the `max(...)` key lambda in the iteration
  order of `people`. DRAW COUNT IS DATA-DEPENDENT: it equals the number of
  civilizations whose habitat rules admit the synthetic sky environment, so adding a
  civilization to the registry changes which draw every later candidate receives.
  That is the classic "an early continue moves the stream" hazard; in C++ this must be
  restructured to draw exactly `people.size()` values up front, in the same order, or
  the selection changes.
- `child_seed(master, domain, variation=0) = int.from_bytes(sha256(f'tectonics-v1:{master}:{domain}:{variation}').digest()[:4], 'big')` -- a uint32.
  In the C++ this maps to `Rng::child(master, domain, variation)`; the domain strings
  are `"sky-archipelagos-v1"` and `"sky-population-{k}-{j}-v1"`.

## Ordering hazards

Fifteen places where container order reaches output. Every one of them must be
reproduced deliberately; several are the difference between a port existing and not.

1. WITCH-HUT RANKING. `sorted(range(N), key=lambda i: -zone_witch_huts[z][x])` --
   Python's sort is STABLE, so equal intensities resolve to ASCENDING NODE INDEX. A
   C++ `std::sort` is not stable; use `std::stable_sort` or make the comparator
   `(-intensity, index)`. Node index is latitude-ordered (node 0 is the north pole,
   then row 1 west-to-east, ...), so an unstable sort systematically favours different
   latitudes.

2. SITE ITERATION ORDER. `for site in sites` is founding order. It decides port
   creation order, hence every port's `id` suffix (`len(ports)` at creation), hence the
   fishery tie-break in (6), hence the outer loop of the sea-route pass.

3. `ground_candidates` is built by `enumerate(harbor)` and is therefore in ascending
   node index. `eligible` preserves that. The stable ranking sort in (4) then inherits
   it.

4. PORT RANKING. `sorted(eligible, key=-(harbor+0.5*coastal_support)*exp(-d/reach))` --
   stable, ties -> ascending node index. Then `[:coastal_hamlets]`.

5. LANDING CHOICE. `min(access, key=lambda pair: pair[1])` over `graph[node]`, whose
   neighbour list `sphere_grid` emits in ASCENDING NEIGHBOUR INDEX (it iterates
   `sorted(ids)`). `min` returns the FIRST minimum, so an exact distance tie picks the
   lowest-numbered neighbour. Equal distances are common at a pole and along a row.

6. FISHERY CLAIMS. `claims` is a Python dict; the comparison `(d, k) < claims.get(i,
   (inf, 0))` breaks a distance tie in favour of the LOWER PORT INDEX. Distance ties
   between two ports are not exotic on a symmetric coastline.

7. CLAIM ITERATION. `for i, (distance, k) in claims.items()` iterates in INSERTION
   ORDER -- port 0's reach in ascending node index, then port 1's newly-claimed cells
   in ascending node index, and so on, with re-claims keeping their ORIGINAL position.
   This order is what `port['fishing_nodes']` records, and it is the float summation
   order for `production`, `worked_area_km2` and all twelve `monthly_fish` entries. A
   C++ `std::unordered_map` here changes the published `fishing_nodes` list and moves
   the summed floats. Use an insertion-ordered structure.

8. ICE PATH BUILD. `sorted((i for i,d in enumerate(distances) if d<=reach), key=d)` --
   stable, ties -> ascending index. The order only has to guarantee parent-before-child
   (which strictly-positive edge costs already guarantee), so this one is safe under
   any distance-ordered traversal, but keep it deterministic anyway.

9. `sea_limits` is built from `sorted({p['population_profile'] for p in ports})` --
   the sort exists purely to make the dict deterministic, since a Python set iterates
   in hash order. Nothing reads the dict in order, so the sort is defensive. Keep it.

10. SEA PAIR RESOLUTION. `if (a,b) in sea_pairs and sea_pairs[(a,b)]['cost'] <= value:
    continue` -- `<=`, so the FIRST writer wins an exact tie. And when a cheaper route
    does replace one, the Python dict KEEPS THE ORIGINAL INSERTION POSITION. The
    source comment says so explicitly: "sea_pairs resolves a duplicate by first writer,
    and its insertion order is the order the sea routes are emitted in." A C++ map that
    reorders on overwrite, or a `std::map` that sorts by key, changes `transport.routes`
    order and therefore changes the trade simulation. Use insert-ordered with
    overwrite-in-place.

11. SEA SEARCH MEMO. `searches` is reset per outer port; `sea_costs` is not. Neither is
    order-sensitive (both are keyed lookups) but the reset is semantically necessary --
    the distance array is per-origin.

12. TERMINALS. `terminals` is ascending `all_sites` index, and the pair loop is
    `for index, a in enumerate(terminals): for b in terminals[index+1:]`, so air routes
    are emitted in lexicographic `(a, b)` order.

13. ROUTE LIST ORDER: ground (in `roads.routes` order), then sea (in `sea_pairs`
    insertion order), then air. `simulate_food` walks this list in order every month and
    gives earlier routes first claim on a seller's exportable surplus and on a buyer's
    material budget, so the concatenation order is a real economic ordering, not
    cosmetic. `transport.routes[index]` is also the `route_index` published in every
    shipment record.

14. `summarize_run` iterates `sites` in order, so `world_economy.sites` is
    surface-sites-then-sky-sites.

15. `make_sky`: `people` comes from `eligible_civilizations`, which iterates
    `sorted(registry, key=(priority, key))`; that order decides which `uniform(0,8)`
    draw each candidate gets, and `max()` returns the FIRST maximum on a tie.

NOT A HAZARD: `occupied` is a `set` but is only ever membership-tested, never iterated.

## Edge cases

GATES AND EMPTY INPUTS
- `cfg.phase < 6` or no `habitats` block: the function returns having written NOTHING,
  not even `result['sky']`. A consumer must treat `sky` as absent, not empty.
- `6 <= cfg.phase < 9` or no `seasonal_food`: `result['sky']` is written and the
  function returns. `fisheries`, `transport`, `world_economy` and
  `layers['fishing_ground_owner']` are all absent.
- `cfg.phase < 7`: `sky.settlements` is forced to `[]` even if `make_sky` produced
  sites. `sky.islands` is NOT gated the same way.
- Zero sites (`settlement_count == 0`): `ports` empty, `claims` empty, `owner` all -1,
  `production == 0.0`, `fisheries.ports == []`, `sea_pairs` empty, `terminals` empty,
  `simulate_food([], ground_routes)` runs 48 months over zero cities,
  `world_economy.sites == []`. All well-defined. NOTE this is exactly the state on
  which the module's one test passes vacuously.
- Zero ocean (`water_type` never 1): `harbor_suitability` is identically 0, so
  `ground_candidates` is empty, no ports, no sea routes. Air routes and ground routes
  still build.
- `wanted == 0` witch huts: `len(hut_nodes) >= wanted` is `0 >= 0`, true on the first
  iteration, so the loop breaks immediately -- but the full O(N log N) sort has already
  run. Guard it in C++ if you care.

DEGENERATE NUMERICS
- `food_coverage = 1 - shortage/demand if demand else 1.0` -- a city with zero annual
  demand reports perfect coverage, never a NaN.
- `delivery = exp(-(d + access_cost)/max(1, support_reach))` -- the `max(1, ...)` is
  dead armour (config bounds support_reach at >= 100) but harmless.
- `steps = max(2, ceil(length/max(spacing_m, 1)))` -- two guards: a zero spacing and a
  zero-length route both still sample at least 3 points (j = 0, 1, 2).
- Antipodal air endpoints: the midpoint interpolation gives `norm < 1e-8`, the route is
  marked unsafe and dropped. Without this guard the normalisation divides by zero.
- Empty sea path in the capacity loop: `max(..., default=0.)` -- but `path` is never
  empty for a route that survives the length test, because `trace` on a reachable node
  always returns at least `[end]`.
- `trace` returns `[]` when the parent chain hits -1 before reaching `start`. On the
  ground side this cannot happen for a node inside the reach (the test asserts
  `port['access_nodes']` is truthy). On the sea side the same holds. Still return the
  empty vector rather than asserting -- the reference does.
- A port candidate with no navigable neighbour (`access` empty) is SKIPPED, and the
  skip CONSUMES one of the city's `coastal_hamlets` slots, because the slice
  `[:coastal_hamlets]` is taken before the loop body runs. A city can therefore end up
  with fewer ports than `coastal_hamlets` even with plenty of eligible ground. That is
  the behaviour; reproduce it.

WORLD EDGES AND POLES
- Both `water_cost` and `road_cost_function` refuse to run their diagonal-corner test
  when either endpoint is on a pole row (`z in (0, n-1)`), because a pole row holds one
  distinct cell and `lookup[(x, 0)]` for `x != 0` is not a key. A C++ `sphere_index`
  that maps every `(x, 0)` to the pole node would silently change behaviour: the guard
  must stay a guard, not become a lookup that happens to work.
- `sphere_grid` gives a pole node the area of the WHOLE polar ring
  (`area * (n-1)`), so a pole cell claimed as a fishery contributes `n-1` times a
  normal cell's area to `worked_area_km2` and to `potential_annual_food`. At small n
  a single polar claim can dominate the world's fishery total.
- Air-route sampling: `x = round((atan2(p.z,p.x)+pi)/(2*pi)*(n-1))` can produce exactly
  `n-1`, which is the duplicate seam column; every layer duplicates column 0 there, so
  the read is correct. `z = round(acos(clamp(p.y))/pi*(n-1))` can produce 0 or `n-1`,
  the pole rows, which are constant across the row, so any `x` is correct.
- `node_grid(owner, points, n)` writes the pole value across rows 0 and n-1 and copies
  column 0 into column n-1. `fishing_ground_owner` therefore shows a whole polar row
  owned by whichever port claimed the pole node.

TYPE HAZARDS
- `world_economy.sites[].site_id` is `int` for surface sites and `str` for sky sites.
  `transport.sites[].id` likewise. Both are currently always int because sky is dead,
  but the schema admits both.
- `fisheries.ports[].id` is a STRING while `fisheries.ports[].core_id` is an INT.
  `humans.cores[k].hamlet_ids` therefore mixes `'hamlet-3'`-style strings with
  `'coastal-2-5'`-style strings; both are strings, so this is consistent, but the
  `core_id`/`site['id']` int-vs-string distinction bites anyone who assumes ids are
  uniformly strings.

## Reasoning worth preserving

These comments record measurements and failures that exist nowhere else in the repo.
Quoted verbatim.

ON FORTRESSES IN THE OCCUPANCY SET (`terrain_society.py:156-159`):
"Fortresses belong in this set as much as cities and hamlets do. Leaving them out
let a harbour be founded on a node a fort already held -- 28 of 56 forts in a
seed-42 size-33 world shared their exact node with a coastal hamlet, which draws as
one icon on top of another and means two settlements occupy one place."
-> The occupied set MUST include fortresses. This is a fixed bug, not a preference.

ON THE BOUNDED GROUND SEARCH (`:166-167`):
"Only landings inside support_reach are ever eligible, so the search stops there
rather than crossing the whole landmass to price candidates it will discard."

ON THE MEMOISED SEA SEARCH (`:225-230`):
"A route is priced pair by pair, but the search behind it only ever depends on where
it starts and which mutation limit it respects, and nothing past sea_reach can
survive the length test below -- a landing distance is never negative. So the search
runs once per (origin, limit), bounded, and the pair loop reads it. The loop keeps
its original order and its original tests: sea_pairs resolves a duplicate by first
writer, and its insertion order is the order the sea routes are emitted in."

ON THE INLINED PASSABILITY TEST (`:32-33`):
"passable() is inlined here: the routing search calls this several million times per
world and the call overhead dominated the three comparisons."

ON ONE-OWNER-PER-CELL (`:187`):
"Each port searches only accessible water. Globally award each cell once."

ON WITCH HUTS (`:141`):
"Huts follow their own preference for isolation and poor conventional suitability."

ON AIR TERMINALS (`:258`):
"Air terminals require industry or stable Weave access; sky settlements have a
landing surface."

ON THE SKY MESH (`:83`):
"A small independent disk mesh, never a displacement of the ground below."
-> The island surface is its own geometry. The terrain under it is untouched.

ON THE RAIN BUDGET (`:68`):
"provisional rain capture residents-equivalent budget"

FROM `shortest_paths` (terrain_settlements.py:666-677), the bound's correctness proof:
"A caller that discards every result past a reach was still paying for the whole
component: the sea routes in `add_world_society` ran 3 828 unbounded searches over
the ocean at size 128 and kept four of them, because `sea_reach` is 6 000 m against a
5 834 m median edge. Stopping at the reach is not an approximation -- edge costs are
non-negative, so every prefix of a path costing `limit` or less also costs `limit` or
less, and a node inside the bound keeps the same distance and the same parent it had
when the search was unbounded. Nodes outside it stay at infinity rather than taking
their true value, so read a bounded result only inside the bound."
-> Note the measured fact buried in it: on a size-128 200 km world, `sea_reach`
(6 000 m, UNSCALED) is BELOW the median graph edge length (5 834 m). The sea network
is barely connectable at large rasters, which is why only four routes survived.

FROM `sphere_index` (terrain_erosion.py:38-48):
"Every routing cost function needs to name the two corner cells a diagonal step would
otherwise slip between, and each was rebuilding this sixteen-thousand-entry map --
and rescanning the same points for the width -- on every call. One
`add_world_society` at size 128 built it 3 886 times for twelve seconds."

FROM `road_cost_function`: "A diagonal cannot slip between two water-filled corner cells."

FROM `fishing_access_factor`'s docstring:
"Fraction of the same finite fish harvest accessible under route ice."
-> The name matters: it is an ACCESS fraction on a fixed harvest, never a bonus.

FROM `docs/performance.md`, the measured result of the rewrite this module carries:
`add_world_society` at size 128 went 213.3 s -> 1.85 s (-99.1%), fitted exponent
2.05 -> 0.80; stage 12 went 70.4 s -> 4.5 s; the projected size-257 total went
~4 750 s -> ~790 s. The pre-rewrite shape was "O(ports^2 x cells x log cells) with a
full O(cells) allocation per pair", and the document names the coastal-hamlet loop as
"the same shape one order down -- one full-graph search per city".

FROM `docs/terrain-world-layers.md`, the published contract:
"Cities consider coastal support opportunities before placement. Additional coastal
hamlets require a safe ground connection and viable landing. After exclusive fishing
grounds are allocated, hamlet roles resolve to harbor, fishing, combined
harbor/fishing or landing. Each ocean cell supplies at most one hamlet. Vessel reach,
distance losses and monthly ice reduce delivered food; prospective fishing habitat is
an upper bound, never harvested supply by itself."
"Floating clusters have separate disk meshes, local relief, altitude climate, magical
support, rainfall capture and food estimates. Surface elevation beneath them is
unchanged."

FROM the pre-port red team (`docs/reviews/2026-09-21-pre-port-red-team.md:43`), on what
is NOT proved: "rewritten today 213.3 s -> 1.85 s (exponent 2.05 -> 0.80) with three
independent equivalence arguments in comments... The only test calling it is
`test_arctic_civilizations.py:87-93` -- one total positive, one total zero, one <= 25%
bound. Everything else it decides (which nodes become ports, which cells each port
owns, witch-hut siting, the sea-route set) is covered only by invariants that hold
trivially on zero ports."
FROM the same review (`:260`), on the sky generator: "`terrain_society.py:47`
early-returns on recipe 3, and ~60 lines below it build island meshes, pick an
eligible civilization, site a `sky_settlement`, and register air terminals and food
models. This is a deliberate documented suspension... not a leftover -- but it is the
largest already-written feature not shipping, and switching it on is a recipe-4
question rather than a rewrite."

## Notes for the C++ implementation

READ `include/fwg/grid.hpp` AND `include/fwg/water.hpp` FIRST. `sphere_grid(n, radius)`
already gives you exactly what this module needs: `point_x`, `point_z`, `areas`
(pole nodes already carry the whole-ring area), `neighbours` as `{index, distance}`
lists already in ascending index order, and `to_grid(values)` which is the exact
equivalent of Python's `node_grid` (closes the seam and the poles). Do not rebuild
any of it. `WaterType { kDry=0, kOcean=1, kLake=2 }` in `water.hpp` matches the
`water_type` layer's encoding, so `water[i] == kOcean` is the right spelling of the
Python `water[i]==1` and `water[i] != kDry` is the right spelling of `if water[i]`.

WHAT HAS NO DIRECT C++ EQUIVALENT

1. INSERTION-ORDERED MAPS. Python dicts are insertion-ordered and this module relies
   on it in two places that reach published output: `claims` (fishery award order ->
   `fishing_nodes` and float summation order) and `sea_pairs` (route emission order,
   with an overwrite KEEPING the original position). Neither `std::map` nor
   `std::unordered_map` does this. Write a tiny `InsertionMap`: a `std::vector` of
   entries plus an index `unordered_map<Key,size_t>`; insert appends, overwrite
   assigns in place. Two of these, one keyed on `int` node, one on
   `std::pair<int,int>`.

2. STABLE SORT. Three ranking sorts (witch huts, port candidates, ice-path order)
   depend on Python's stable sort resolving ties by the pre-sort order, which is
   ascending node index everywhere. Use `std::stable_sort`, or fold the index into the
   comparator as a secondary key. `std::sort` will silently produce a different world.

3. `min()` RETURNS THE FIRST MINIMUM. `min(access, key=...)` and `max(people, key=...)`
   both keep the first on a tie. `std::min_element`/`std::max_element` keep the first
   too (strict `<` / `comp(largest, *it)`), so they are correct here -- but only if
   your comparator is strict. Do not write `<=`.

4. TUPLE COMPARISON. `(d, k) < claims.get(i, (inf, 0))` is lexicographic on
   `(double, int)`. `std::pair<double,int>` gives exactly this. The default when the
   cell is unclaimed is `{+infinity, 0}`; `std::numeric_limits<double>::infinity()`.

5. NEGATIVE MODULO. `make_sky`'s `(cx + (j-1)*2) % (n-1)` can have a negative left
   operand and Python returns a non-negative result. C++ `%` does not. Write
   `((v % m) + m) % m`. (Dead code today, but it is the classic trap.)

6. BANKER'S ROUNDING. The air-route sampler uses Python `round()`, which rounds half
   to EVEN. `std::round` rounds half away from zero. On an exact `.5` the two pick
   different cells, which can flip a route's `safe` flag. Either implement
   round-half-to-even (`std::nearbyint` with `FE_TONEAREST`, which is the default
   rounding mode) or accept the divergence and say so -- this is a clean rewrite, so
   accepting it is legitimate, but it must be a decision, not an accident.

7. `math.hypot` and builtin `sum()`. The reference relies on CPython's compensated
   `sum()` in places (documented as worth ~1 ULP and enough to move generated worlds).
   Since this is a clean rewrite with its own float semantics, use the existing
   `fwg::Sum` accumulator declared in `Core/globe.hpp`-style for consistency with the
   rest of the port, and do not chase bit parity.

8. NULLABLE REGISTRY FIELDS. `industrial_resource_min` and `air_terminal_resource_min`
   are `null` for eleven of twelve civilizations and the null is load-bearing. Use
   `std::optional<double>`, not a sentinel. A `-1` sentinel makes every civilization
   industrial and every city an air terminal.

9. MIXED-TYPE IDS. `world_economy.sites[].site_id` and `transport.sites[].id` are int
   for surface sites and string for sky sites. `fisheries.ports[].id` is a string while
   `core_id` is an int. Since sky is dead, the cleanest C++ is: `site_id` as an int
   everywhere, plus a separate `layer` string, and a documented note that the
   reference's JSON admits a string there for sky sites only. If the JSON writer must
   match the reference shape, a small variant type is needed for exactly these two
   fields.

DATA STRUCTURES TO ADD TO `fwg::World`

```cpp
struct Port {                    // key field: id (string)
    std::string id;              // "coastal-<core_id>-<ordinal>"
    std::string kind = "hamlet";
    std::string role;            // harbor + fishing | harbor | fishing | landing
    std::string layer = "surface";
    int node = -1, sea_node = -1, x = 0, z = 0;
    double landing_distance_m = 0;
    int core_id = -1;            // site ORDINAL, not a string
    std::string population_profile, culture_id;
    double height_m = 0, harbor_quality = 0;
    bool trade_terminal = false;
    std::vector<int> access_nodes;
    double access_cost = 0;
    std::string reason;
    double worked_area_km2 = 0, delivered_food = 0;
    double delivered_materials = 0, irrigation_benefit = 0;   // always 0, kept for shape
    std::array<double,12> monthly_fish{};
    std::vector<int> fishing_nodes;
    double winter_fishing_fraction = 0;
};
enum class RouteMode { Ground, Sea, Air };
struct Route {                   // no id field at all; identity is its index
    int from = -1, to = -1;      // indices into all_sites
    RouteMode mode;
    std::vector<int> nodes;               // ground + sea
    std::vector<std::array<int,2>> path;  // air only
    double length_m = 0, cost = 0, cruise_altitude_m = 0;
    std::array<double,12> capacity{};
    double efficiency = 0, price = 0;
    std::array<std::string,2> ports;      // sea only
};
struct Fisheries { std::vector<Port> ports; double potential_annual_food, delivered_annual_food; };
struct Transport { std::vector<Route> routes; std::vector<int> terminals;
                   std::vector<std::pair<int,std::string>> sites; };
```

TWO SEARCH PRIMITIVES TO FACTOR OUT, because four call sites share them and they are
the whole module: a bounded single-source Dijkstra returning `(distances, parents)`
with an optional target set, and a bounded multi-source labelled Dijkstra returning
`(distances, owners, parents)`. Both take `std::function<std::optional<double>(int,int,double)>`
or, better for speed, a template parameter -- this pass makes millions of edge calls
and the reference explicitly inlined its predicate for that reason. Use
`std::priority_queue<std::pair<double,int>, vector, greater<>>` with lazy deletion
(`if (value != distances[i]) continue;`), matching the reference exactly.

A NOTE ON SCALE, BECAUSE IT WILL BITE. `cfg.support_reach` and `cfg.settlement_spacing`
arrive ALREADY SCALED by `circumference_m / 11148.96` (`fwg::Config` already carries
them that way, per `config.hpp`'s "carried for stages not yet ported" block). The world
options this module reads -- `sea_reach`, `air_reach`, `fishing_reach`, `sea_draft`,
`sky_radius`, `sky_altitude` -- are NOT scaled anywhere, and neither are the bare
literals `200` (hut spacing), `80` (air clearance), `12000`/`10000`/`8000`/`5000`
(efficiency and price scales). On the 200 km default preset that means a 6 km sea reach
against a ~5.8 km median graph edge at size 128: the sea network is at the edge of
connectability by construction. `fwg::Config` has no home for the world options yet;
when you add one, decide explicitly whether these six get the reach factor. The
reference does not apply it, so 1:1 functional parity means NOT applying it -- but
record the decision, because it is the difference between a world with sea trade and
one with four routes.

PROCESS POOLS: none here. `ICARUS_CITY_WORKERS` controls `city_planner.fill_cities` and
`hamlet_planner.fill_hamlets` only. `add_world_society` is single-threaded in the
reference and has no pooled path to match. If you parallelise the per-port fishery
search, note that the claim map's INSERTION ORDER is output, so the per-port searches
may run in parallel but the claim merge must be serial and in port order.

SIZE OF THE JOB: the two Dijkstras and the two cost functions are perhaps 200 lines;
the main pass is the other two thirds. `make_sky` is worth porting behind a flag
(it is a complete feature, and the sky mesh is the only place in the generator that
emits geometry) but it is 65 of the 297 Python lines and reaches nothing today.

## Open questions

1. SKY IS UNREACHABLE TWICE OVER, AND THE CONTROLS LIE. `make_sky` returns empty on
   `cfg.world_recipe == 3`, and recipe 3 is the only recipe `capabilities.json`
   declares supported. Even on recipe 0 the island loop cannot execute, because
   `sky_clusters` is declared `spec(0, 0, 0, ...)` -- default 0, min 0, MAX 0 -- and
   `sky_occurrence` is likewise locked at 0. So `islands`, `settlements` and
   `area_km2` are `[] / [] / 0.0` on every world the generator can build, and the
   `sky_altitude` and `sky_radius` controls are published as `"status": "open"` in
   `Contracts/catalogues/world-controls-v1.json` while having no reachable read site.
   The red team calls the suspension deliberate and documented (five doc sites, pinned
   by `test_world_debug.py:18`) and the two open controls an unambiguous bug. DECIDE:
   does the C++ port (a) carry `make_sky` behind a flag as specified here, (b) carry
   only the empty `sky` block so the document shape matches, or (c) drop it and write
   the block as a literal? The pipeline is not affected either way -- `all_sites`,
   `models`, `terminals` and `summarize_run` all degrade cleanly to the surface-only
   case.

2. THE THREE EQUIVALENCE ARGUMENTS ARE UNVERIFIED. The 213.3 s -> 1.85 s rewrite rests
   on three reasoning-only claims (bounded ground Dijkstra, bounded `allocate_access`,
   memoised per-(origin, limit) sea search). None has a test; the module is on the
   `uncovered` allowlist so no conformance record describes it either. The only test
   that calls it asserts one total > 0, one total == 0, and one <= 25% bound. All three
   arguments read as sound to me on the prefix property of non-negative-weight
   Dijkstra, but I did not execute them, and the pre-rewrite baseline still exists at
   parent commit `9808fee` if anyone wants the real before-state. A porter implementing
   from this document implements the OPTIMISED behaviour; if the arguments are wrong,
   the port inherits the same wrongness. Flagging it as inherited risk, not as a defect
   I found.

3. `magic_risk_<civ>` IS A BYTE COPY OF `magic_hazard` FOR EVERY CIVILIZATION.
   `terrain_leyline_history.py:346` writes the same `hazard` vector under all twelve
   names. So the per-civilization risk branching in the port pass and the fishery pass
   is currently indistinguishable from reading `magic_hazard`, and the sea-route pass's
   use of the generic `hazard` is currently indistinguishable from the other two. Is
   the per-people hook intended to diverge (in which case the sea pass's use of the
   generic layer is an inconsistency worth fixing) or is it vestigial (in which case
   all three should read one layer and the twelve copies should go)? I could not tell
   from the code which was intended.

4. THE FISHING DRAFT IS `0.1`, NOT `o['sea_draft']`. `water_cost(..., draft=.1)` in the
   fishery pass against `draft=o['sea_draft']` (1.0 m) in the landing test and in the
   sea-route pass. This reads intentional -- a fishing boat draws less than a trader --
   but it is an undocumented bare literal sitting next to a published control that
   means the same thing, and nothing states the relationship. Confirm before porting.

5. A SKIPPED CANDIDATE STILL CONSUMES A HAMLET SLOT. `ranked[:coastal_hamlets]` is
   sliced before the loop, and a candidate whose `access` list is empty hits `continue`
   without being replaced. A city can therefore get zero ports while its third- and
   fourth-ranked candidates were both perfectly viable. Intended thrift or an
   off-by-design? The comment says nothing.

6. `access_cost` IS A COST, USED AS A LENGTH. It comes out of `road_cost_function`,
   which adds a `12*grade^2` surcharge, a `0.5*(flood_i+flood_j)` surcharge and a flat
   `bridge_cost` of 200 on a river crossing -- and it is then divided by
   `support_reach` (metres) in the delivery exponential and added to a sea route's
   metre `length` to form `value`, which sets `efficiency` and `price`. Dimensionally
   this mixes cost units with metres. It is self-consistent as a monotone penalty, and
   changing it would move every world, but a porter should know it is not a distance.

7. RE-ENTRANCY. `add_world_society` appends to `result['humans']['hamlets']` and to
   `humans.cores[k]['hamlet_ids']` without clearing. Called twice on the same document
   without an intervening `add_humans` (which the one covering test does), ports appear
   twice. The pipeline never does this, but nothing guards it. Should the C++ clear
   prior `coastal-*` rows on entry?

8. `sea_reach` (6 000 m, unscaled) IS BELOW THE MEDIAN GRAPH EDGE (5 834 m) AT SIZE 128
   ON THE 200 km DEFAULT. This is stated as a measurement in the `shortest_paths`
   docstring. It means the sea network at large rasters consists of routes between
   ports one or two graph hops apart, and four routes survived out of 3 828 candidate
   pairs. Is that the intended world, or is the missing reach-scaling on the world
   options a latent defect of the same shape as the one the request builder fixed for
   `support_reach`? I did not change anything; a 1:1 port reproduces the unscaled
   behaviour.

## Addenda from the audit

The reader above wrote the specification; a second reader went back to the source
to find what it had left out or got wrong. Both are kept: the omissions are
content the implementer needs, and the corrections say where the specification
above is not to be trusted.

### Corrections

- **Claimed:** PIPELINE POSITION: 'called from terrain_area.apply_world_scale immediately after add_seasonal_food and immediately before add_nests' -- and, throughout, that make_sky would be live on recipe 0 ('cfg.world_recipe (0 or 3)', 'Even on recipe 0 the island loop cannot execute', 'recipe 3 is the ONLY shipped recipe').
  **Actually:** The call sits inside a recipe gate the spec never mentions. terrain_area.py reads `if cfg.world_recipe:` and only then `from .terrain_society import add_world_society; add_world_society(result,cfg)` followed by `add_nests(result,cfg)`. On recipe 0 (world_recipe == 0, falsy) the main pipeline NEVER CALLS add_world_society at all -- no `sky`, no `fisheries`, no `transport`, no `world_economy`, no `fishing_ground_owner` block is written on that path. So make_sky is unreachable three ways over, not two: recipe 3 early-returns inside it, recipe 0 never reaches the caller, and sky_clusters is min==max==0 on top. The only recipe-0 entry is terrain_history.civilization(), which has no recipe gate. Also `result['timing_ms']['world_society']` covers add_world_society AND add_nests together, not this pass alone.

- **Claimed:** cfg.support_reach 'at the 200 km preset it resolves to ~17 943 m'.
  **Actually:** 17 938.9 m. The small preset is exactly 200 000 m of circumference, reach_scale = 200000 / 11148.961 = 17.93893, and the authored base is 1000 m, giving 17 938.93 m. It is presented as a concrete resolved figure a porter may use to sanity-check their scaling.

- **Claimed:** The road_cost_function pseudocode: 'if hazard and max(hazard[i],hazard[j]) > cfg.human_magic_limit: return none' and 'if hazard and max(hazard[(xi,zj)],hazard[(xj,zi)]) > limit: return none'.
  **Actually:** The source tests `if hazard is not None and ...` (terrain_settlements.py:701 and :706). The distinction is load-bearing in a port: a truthiness test on a container would silently disable the magic gate for an empty hazard vector, whereas the reference disables it only when the argument is absent (the parameter defaults to None; this pass always passes a real vector, so the gate is always active here).

- **Claimed:** STEP 7 / the simulate_food contract: 'Per month it iterates the route list IN ORDER and, for each route, both directions (a->b) then (b->a): sent = min(exportable[seller], remaining_capacity, need/efficiency, budget[buyer]/price/efficiency).'
  **Actually:** Right as far as it goes but incomplete in two ways that move every number: (a) `remaining` is a SINGLE per-route, per-month budget initialised to edge['capacity'][month] and decremented by the a->b flow before b->a is evaluated -- the two directions share one capacity, they do not each receive the month's full capacity; (b) any shipment with `sent <= 1e-12` is discarded entirely (`if sent<=1e-12: continue`), emitting no shipment record and no budget charge. A port that gives each direction its own capacity over-delivers on every bidirectional route and changes every food_coverage in world_economy.sites.

### Missing — blocking

**simulate_food monthly algorithm (terrain_seasons.py:22-62) -- it produces world_economy.months, a published block of this pass**

`simulate_food(cities, roads, years=4)`. `stocks = [0.]*len(cities)` (stores start EMPTY). For `step in range(years*12)`: `month = step%12`; `opening = copy(stocks)`; `spoiled[i] = stocks[i]*cities[i]['spoilage']` (on the OPENING stock, before harvest); `harvest[i] = cities[i]['harvest'][month]`; `stocks[i] = stocks[i] - spoiled[i] + harvest[i]`; `exportable[i] = max(0, stocks[i] - demand - min(demand, storage))`; `budgets[i] = cities[i].get('transport_budget', inf)`; imports/exports/spent zeroed; `shipments=[]`, `loss=0.`. Then `for route_index, edge in enumerate(roads)`: `a,b = edge['from'], edge['to']`; `remaining = edge['capacity'][month]` -- ONE value shared and decremented across BOTH directions of that route in that month; `for (seller,buyer) in ((a,b),(b,a))`: `efficiency = edge['efficiency']`, `price = edge.get('price', 1.)`; `if efficiency <= 0: continue`; `need = max(0, cities[buyer]['demand'] - stocks[buyer])`; `sent = min(exportable[seller], remaining, need/efficiency, budgets[buyer]/price/efficiency)`; `if sent <= 1e-12: continue`; `received = sent*efficiency`; `charge = received*price`; then `stocks[seller] -= sent; stocks[buyer] += received; exportable[seller] -= sent; remaining -= sent; budgets[buyer] -= charge; spent[buyer] += charge; exports[seller] += sent; imports[buyer] += received; loss += sent - received`; append `{'from':seller,'to':buyer,'route_index':route_index,'sent':sent,'delivered':received,'lost':sent-received,'material_cost':charge}`. Then per city in order: `consumed = min(demand, stocks[i]); stocks[i] -= consumed; overflow = max(0, stocks[i] - storage); stocks[i] -= overflow`; record `{'opening','harvest','spoiled','imports','exports','material_spent','demand','consumed','shortage': max(0, demand-consumed),'overflow','closing': stocks[i]}`. Month record: `{'year': step//12+1, 'month': month+1, 'cities': records, 'shipments': shipments, 'opening': sum(opening), 'harvest': sum(harvest), 'closing': sum(stocks), 'transit_loss': loss, 'consumed': sum, 'spoiled': sum, 'overflow': sum}`. Returns `{'months': history}`.

### Missing — important

**Origin and magnitude of `wanted`, the witch-hut budget**

`wanted = sum(s['kind']=='witch_hut' for s in result['regions']['landmarks'])`, and those landmarks are minted by `add_environment` (terrain_ecology.py:184-186) as `{'id': f'witch_huts-{i}', 'kind':'witch_hut', 'x':x, 'z':z, 'layer':'surface', 'intensity': values[i]}` -- ONE PER INFLUENCE CENTRE of the `witch_huts` zone, and that zone selects AT MOST THREE centres (`if len(centers)>=3: break`, terrain_ecology.py:179), zero when the zone did not manifest. So `wanted` is 0..3, never more. The spec's 'up to the same number of new ones are appended' gives a porter no scale at all -- it matters for whether the 200 m hut-to-hut separation test can ever bind and whether the O(N log N) sort is worth guarding. Also: on an age advance `rebuild_tail` calls `refresh_environment` -> `add_environment` BEFORE `civilization()`, so `regions.landmarks` is fully re-minted each age and `wanted` resets to the ecology count -- the hut budget does NOT decay across ages.

**sphere_grid node layout, pole adjacency, area and neighbour-distance formulas (terrain_erosion.py:15-31)**

`points = [(0,0)] + [(x,z) for z in range(1,n-1) for x in range(n-1)] + [(0,n-1)]` -- node 0 is the NORTH pole (`direction(x,0,n) = (0,1,0)`), the last node is the south pole, interior nodes row-major latitude-ordered. `index(x,z) = lookup[(0,z) if z in (0,n-1) else (x % (n-1), z)]`. Neighbours: for a POLE node `ids = {index(xx, 1 if z==0 else n-2) for xx in range(n-1)}` -- the pole is adjacent to EVERY ONE of the n-1 cells of the adjacent ring; otherwise the 8-neighbourhood `{index(x+dx, z+dz) for dx,dz in (-1,0,1)^2} - {i}`. Each list is emitted over `sorted(ids)`, hence ascending node index. Neighbour distance = `radius*acos(clamp(dot(vec_i,vec_j), -1, 1))`. Area: with `step = pi/(n-1)`, `lat = pi/2 - z*step`, `area = radius^2 * 2*step * (sin(min(pi/2, lat+step/2)) - sin(max(-pi/2, lat-step/2)))`, multiplied by `(n-1)` when `z in (0, n-1)`. The pole-ring adjacency is what makes the spec's own pole warnings mean anything: a pole land node's landing search scans the whole adjacent ring, and a pole water node is one hop from every cell of its ring, so one polar claim both dominates `worked_area_km2` and short-circuits the fishery allocation across a latitude.

**island_habitat is identically zero at the default raster, so the `islander` community trait is currently unreachable**

terrain_ecology.py carries tuning history the spec drops. The `min(3e6, ...)` absolute-metres arm of the island test was removed because it was '3 km2 in absolute metres, authored for the 11.15 km reference world where every raster cell is under 0.25 km2. At the 200 km default the smallest sphere-grid cell is 15.217 km2 at size 17, so at THE DEFAULT RASTER no component of any seed can reach the floor and island_habitat is identically zero -- a published layer that had become a constant.' Plus the correction to an earlier wrong version of the same comment: 'Cells shrink to 1.914 km2 at size 33 and 0.240 km2 at size 65... Measured at phase 12 on the 200 km default: under the old arm seed 40 reads 1 island cell at size 33 and 6 at size 65 and seed 43 reads 1 at size 65, against 29 / 167 / 166 under the fraction arm.' The shipped rule is the fraction arm only: flood-fill land components over `graph` (stack DFS, `unvisited` seeded from `min(unvisited)` so component order is deterministic), then `islandness[i] = float(component_area < land_total*0.15)`. Consequence for THIS pass: at the 200 km / size-17 default `island_habitat` is all zero, so a city with ports always gets `['maritime']` and NEVER `['islander']`. A porter who does not know this reads an empty islander set as a bug in their own port.

### Missing — minor

**allocate_access claims its seed unconditionally**

terrain_humans.py:27-29 sets `distance[seed]=0.` and `owner[seed]=label` before the heap loop, WITHOUT consulting `cost`. In STEP 4 the port's own `sea_node` therefore always enters `claims` at distance 0 regardless of whether `water_cost` would pass it, and at distance 0 it wins every tie unless another port shares that sea node. Two ports of different cities CAN share a sea node (only the land `node` goes into `occupied`, never `sea_node`), and that exact 0-vs-0 tie resolves to the lower port index.

**node_grid exact semantics and operation order (terrain_climate.py:8-13)**

`out = [[0.]*n for _ in range(n)]`; `for (x,z),v in zip(points, values): out[z][x] = v`; THEN `out[0] = [values[0]]*n` and `out[-1] = [values[-1]]*n` -- row 0 takes node 0's value and row n-1 takes the LAST node's value specifically, not a generic 'pole value'; THEN `for row in out: row[-1] = row[0]`, which runs after the pole rows are written. The grid is float-initialised, so `fishing_ground_owner` is a float grid holding integral owner indices and -1.

**port['height_m'] source**

`'height_m': height[node]` -- the `height` LAYER sampled at the port's land node, not the owning city's `height_m` and not the sea node. The spec lists the field with no provenance.

**sphere_index memo policy**

`_INDEX` is a module-level dict keyed on `id(points)`; on a miss, `if len(_INDEX) > 4: _INDEX.clear()` -- a hard flush at five entries, not an LRU. The entry is `(points, {p:i for i,p in enumerate(points)}, max(z for x,z in points)+1)`; it stores the points list itself so a recycled `id()` cannot alias a stale map and the list stays alive. The spec quotes the performance comment but neither the eviction rule nor the identity-keying reason ('Keyed on the identity of the point list rather than on (n, radius), because water_cost is handed the points and never learns the radius').

**port['role'] initial value at creation**

The record is built with `'role': 'harbor + fishing'` at line 174 and only overwritten in the second loop at line 214. Harmless in the reference, but a C++ struct that default-initialises `role` to an empty string and then drops or reorders the second loop publishes empty roles rather than the reference's visible default.

**make_sky literals and fields the spec omits**

Sky settlement `reason` = 'Independent rain capture, growing climate and magical tolerance support this island community.'. The sky food model's first field is `'site_id': site['id']`, which is what keeps it parallel to the surface models (the spec's FOOD MODEL lists demand/storage/spoilage/transport_budget/harvest but not site_id). The mesh dict carries a third key, `'coordinates': 'local east, radial altitude, north; metres'`, beside `vertices` and `triangles`.

**reach_scale provenance and the raise-not-clamp rule**

`REFERENCE_RADIUS_M = 1774.4123532462844`, `REFERENCE_CIRCUMFERENCE_M = 2*pi*REFERENCE_RADIUS_M = 11148.961 m`, `reach_scale(circumference_m) = circumference_m / REFERENCE_CIRCUMFERENCE_M`. Only `settlement_spacing`, `support_reach` and `culture_link_cost` take it, and the request builder RAISES `STATE_CAPACITY` above `REACH_MAX_M` rather than clamping: 'Do NOT clamp... past the width where the ceiling binds, the reach stops growing with the planet, silently, and cultural regions quietly stop scaling while everything around them keeps going. It was one preset from firing -- culture_link_cost resolves 96870.01 m at the 600 km large preset, 3.13% below the ceiling.' Binding widths: 2477.55 km for settlement_spacing, 1114.90 km for support_reach, 619.39 km for culture_link_cost. The spec quotes 11148.96 but not the radius it derives from nor the raise-not-clamp behaviour.
