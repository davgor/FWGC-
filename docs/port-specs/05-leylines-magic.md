# Port specification: 05-leylines-magic

Extracted from the Python `FantasyWorldGenerator` at commit `b67c99b` before that
repository was retired, then audited against the same source by a second reader.

**Source modules**

- `Sim/icarus_sim/terrain_leyline_history.py`
- `Sim/icarus_sim/terrain_magic.py`

**Estimated C++ size:** roughly 1150 lines, excluding tests.

## Audit: IMPLEMENTABLE

This is an unusually strong spec. I tried to implement network_geometry, generate_networks, evaluate_networks, hidden_feeds, _saturating_field, rot_spread, void_bite, dominant_school, edit_network, college_refusal, college_diagnostics and add_colleges from it cold, and I never had to guess a constant, a tie-break, a loop bound, an initialisation or an ordering. Every magic number in both files appears with its meaning (the twelve schools' exact RGB triples and descriptor strings, 0.9999, 1000 attempts, 33 path samples, uniform(0.35,1.65), uniform(0.3,0.7), 0.12/0.8, 0.05/0.4, 0.08/0.45, 2.5, 6.0, 2.0, 64, 0.8, 0.35, 0.08, the 0.3/0.2 hazard weights, the growth weights, 1e-12, 1e-6, 150, 0.25, max(1500., r*0.15), REFERENCE_RADIUS_M), the five college_diagnostics reason strings are reproduced verbatim and correctly, the eight COLLEGE_LIMITS are in their true short-circuit order, and the load-bearing comments (negative zero, void's sign discipline, feed-before-saturation, rot's metre-denominated creep, the hoisting measurements, the budget-below-the-limits move, the add_magic warning) are carried rather than summarised. I also checked its claims about modules it does not own: node_grid's scatter-then-close order, sphere_grid's point order and sorted neighbour lists, child_seed's sha256 formula, allocate_access's candidate<=limit and (candidate<distance[j] or (candidate==distance[j] and label<owner[j])) tie-break, shortest_paths' return shape and its unbounded call, road_cost_function's full cost expression, and the twelve schools' option defaults (1.0/0.0 occurrence, 8 nodes, 110 m, 0.7/0.45/1.15 strength, 0.2/0.65/0.8 instability) -- all correct, and the add_magic dead-writer claim is confirmed by grep across Sim/. What it does not deliver is output fidelity, not functionality: two published prose fields (magic.method and magic.college_method) are promised and never quoted, so a porter would invent them; timing_ms['colleges'] is documented as += when the source assigns; and one Edge-cases claim -- that node placement's abs(dot) < 0.9999 test excludes degenerate and antipodal arcs at generation -- is false for node 2 after the sacred alignment, which is exactly the kind of confident wrong statement that gets a real guard dropped. I supply all of that below, so with this audit attached the subsystem is fully reconstructible.

## Purpose

This subsystem is the world's magic geography. It does two separable jobs that share one output block.

(1) **Leyline networks** (`terrain_leyline_history.py`). Twelve magical "schools" each own an independent, editable network of key points (nodes) and leylines (great-circle edges) scattered over the sphere. `generate_networks` rolls that geometry once, at stage 9, from the world seed. `evaluate_networks` is the field evaluator: it turns the current node/edge intensities into ~30 raster layers -- a saturated potency field per school, an instability field per school, and the composite `magic_density`, `magic_hazard`, `magic_growth`, `magic_opposition`, `dominant_magic`, plus per-people `magic_risk_*` copies. It is re-run every time anything touches an intensity: an age transition, a yearly ley-drift tick, a player leyline edit, a resolved quest hook, a nomad cult pilgrimage, a divine corruption. It is the single most re-entered pass in the generator and the one every downstream fantasy system (biome variants, settlement siting, road cost, beast nests, ruins, wars, heroes, key locations) reads.

(2) **Wizard colleges** (`terrain_magic.py`). After cities and their hinterlands exist, one pass walks every cell inside a city's road-support reach, tests it against eight independent limits, and seats up to `college_count` colleges on the best-scoring qualifying cells, subject to a settlement-clearance and a college-spacing rule. It publishes a per-institution diagnostic row saying why the colleges that did not place did not place.

Eight of the twelve schools are the world's public taxonomy ("known"); four are hidden and **generation can never raise one** -- their manifestation probability is locked to zero in the option registry, and only the corruption API, a ruin legacy or a player edit seats a node in one. Each hidden school has a bespoke field mechanic the known eight do not have: blood feeds on dense living population, eldritch on aberrant/undead nests and ruins, rot creeps cell-to-cell as a contagion rather than falling off as a Gaussian, and void *subtracts* potency from every other school instead of adding its own.

`terrain_magic.py` also contains `add_magic`, a **dead second writer** of the same `magic` block that emits schema version 1 and must not be ported or wired up. It is documented below only so a porter does not resurrect it.

## Where it sits in the pipeline

## The sixteen-stage ladder

Stage names (`terrain_history.STAGES`): 1 Plate layout, 2 Tectonic relief, 3 Surface detail, 4 Erosion and sediment, 5 Connected water, 6 Second tectonic relief, 7 Valleys and gorges, 8 Wind and rain, **9 Leylines**, 10 Founding, 11 Roads, **12 Populated regions**, 13 Beasties and animals, 14 Age transition 1, 15 Age transition 2, 16 Simulation complete.

### Must exist before `generate_networks` runs (stage 9)

- `result['layers']` populated through climate: `height`, `water_type`, `slope`, `temperature`, `moisture`, `freshwater_distance`, `flood_risk`, `rain_river`, `natural_biome`, `landform`. In practice only `size`/`globe_radius` are read by `generate_networks` itself; the climate gate is `if cfg.phase >= 6 and result.get('climate')`.
- `result['effective_config']['globe_radius']` -- **metres**, post world-scale. This is the scaling divisor for every authored school width.
- `cfg.world_options` resolved through `terrain_world.options(cfg)` -- six options per school, twelve schools, 72 values.
- `result['magic']` must NOT exist; `generate_networks` writes it whole.

`evaluate_networks` additionally needs, and reads **defensively** (all optional, absent == empty):
- `result['settlements']['sites']` (for blood)
- `result['beast_nests']['sites']` (for eldritch)
- `result['ruins']` (for eldritch)

At stage 9 none of those exist, so the hidden feeds are all absent and every hidden field is negative zero. That is the normal case.

### Must exist before `add_colleges` runs (stage 12)

- `result['magic']` (any of it -- the pass bails with `return result` if falsy)
- `result['humans']` with `hamlets`, `fortresses`, `cores` -- bails if absent
- `result['settlements']['sites']`, each site carrying integer `id` (its own index), `node` (sphere-grid node index), `x`, `z`, `population_profile`
- `result['humans']['cores'][i]['culture_id']`
- layers: `magic_density`, `magic_hazard`, `slope`, `temperature`, `freshwater_distance`, `flood_risk`, `suitability`, `water_type`, `height`, `rain_river`, and (recipe 3) `magic_risk_<profile_id>` and `suitability_<profile_id>` per people
- `cfg.support_reach` (metres, already scaled), `cfg.college_count`, `cfg.human_magic_limit`, `cfg.road_max_grade`, `cfg.bridge_cost`

### Who depends on the output afterwards

- `terrain_history.add_biome_variants` -- `dominant_school` over the twelve `ley_*` layers picks `biome_variant` (an index into the magical biome catalogue, or -1).
- `terrain_ecology.add_environment`, `terrain_nests.add_nests` -- habitat gates read `ley_holy`, `ley_primordial`, `magic_density`, `magic_hazard`.
- `terrain_settlements` / `terrain_humans` / `terrain_society` -- `road_cost_function` refuses any edge whose endpoint `magic_risk_<people>` exceeds that people's `mutation_limit`; city, hamlet, fortress and port siting all read those risk grids.
- `terrain_ruins.ruin_legacy` -- `dominant_school` picks the school a fallen city leaves behind, and seeds a new node `"<ruin id>-key"` in that school.
- `terrain_wars._key_point_school` -- same predicate, for war-fallen cities.
- `terrain_history.ley_threat_pressure` / `city_fate` -- dominant local potency drives city mortality at an age turn; `'self_magic'` fate probability is `0.035 + 0.06 * ley_weave`.
- `terrain_corruption` -- seats a cluster of nodes in a hidden school.
- `terrain_nomad_effects.apply_cultist_leylines` -- deepens an existing node by x1.18.
- `terrain_time._ley_drift` -- one year of compounding intensity drift (see the AGE_YEARS section).
- `terrain_astrology` / `terrain_time._apply_surge` -- writes `net['surged_strength'] = net['strength'] * tide_factor[school]` on **known schools only**; a hidden school carries no such key at all, and widening that to twelve would change every world's bytes.
- `hero_generator/wells/magic.py` -- a living college gets a Magister; a ruin's key point gets a Domain-holder.

### Re-entry graph for `evaluate_networks`

It is called from: `generate_networks` (tail call, always), `terrain_history.rebuild_tail` (age transition + visitation, guarded by `cfg.magic_enabled`), `terrain_history.advance_age_request` (after player edits), `terrain_time.advance` (once per request, only if `ley_touched`), and `terrain_history` line 405 (a visitation rebuild). **It is idempotent given identical intensities** and does no RNG.

## Inputs

## `generate_networks(result, cfg)`

| Source | Field | Units / range | Notes |
|---|---|---|---|
| `cfg` | `seed` | uint32 | world seed |
| `cfg` | `magic_enabled` | 0 or 1 | if 0, **no school manifests**; block is still written |
| `cfg` | `college_count` | int 0..12 | only to build the pending diagnostic row |
| `cfg` | `world_options` | JSON string | resolved via `terrain_world.options(cfg)` |
| `result` | `effective_config.globe_radius` | metres | e.g. 31830.988618379066 for the 200 km default |
| options | `<school>_occurrence` | 0..1 | probability the school manifests. Known default 1.0; hidden locked min=max=0.0 |
| options | `<school>_nodes` | int 3..24 | default 8 |
| options | `<school>_width` | metres 10..2000 | default 110, **pre-scaling** |
| options | `<school>_strength` | 0..2 | default 0.7; 0.45 for `umbral` and `infernal`; **1.15** for all four hidden |
| options | `<school>_instability` | 0..1 | default 0.2; 0.65 for `infernal`; 0.8 for all four hidden |
| options | `<school>_variation` | uint32 | independent per-school seed variation, default 0 |

## `evaluate_networks(result, cfg)`

| Source | Field | Units / range |
|---|---|---|
| `cfg` | `size` (n) | grid edge, 3..1025 |
| `result` | `effective_config.globe_radius` | metres |
| `result` | `magic.networks` | the twelve network records (see Writes) |
| `result` | `settlements.sites[].direction`, `.population_estimate` | unit 3-vector, people. **Optional** -- absent block means no blood feed |
| `result` | `beast_nests.sites[].direction`, `.family` | `family` in `('aberrant','undead')` only. **Optional** |
| `result` | `ruins[].direction` | **Optional**; `ruins` is a flat list, not a block with `sites` |
| `terrain_profiles.civilization_ids()` | 12 entity profile ids | `human_maritime, human_desert, human_cold, human_large_island, human_rainforest, human_heartland, dwarf, elf, gnome, tidekin, hill_dwarf, frosthold_dwarf` (note: `mixed` is a *configuration*, not an entity, and gets no risk layer) |

Per network record it reads `nodes` (each with `id`, `direction`, `intensity`), `edges` (each with `id`, `from`, `to`, `intensity`, `path`), `width_m`, `strength`, `instability`.

## `add_colleges(result, cfg)`

| Source | Field | Units / range |
|---|---|---|
| `cfg` | `size`, `support_reach` (m), `college_count` (0..12), `human_magic_limit` (0..1), `road_max_grade`, `bridge_cost`, `world_recipe`, `population_profile` |
| layers | `magic_density` 0..1, `magic_hazard` 0..1, `slope` degrees, `temperature` °C, `freshwater_distance` metres (negative means none), `flood_risk` 0..1, `suitability` 0..1, `water_type` int (0 = dry land), `height` metres, `rain_river` 0/1 |
| layers, recipe 3 | `magic_risk_<profile_id>` 0..1, `suitability_<profile_id>` 0..1 |
| `result` | `settlements.sites[]`: `id` (int, == its index), `node` (int), `x`, `z`, `population_profile` (str) |
| `result` | `humans.hamlets[]`, `humans.fortresses[]`: `x`, `z` |
| `result` | `humans.cores[i].culture_id` (str) |
| profiles | `mutation_limit` 0..1, `road_grade_limit` 0.01..1, `college_slope_limit` degrees (15, or 22 for dwarf/frosthold_dwarf), `college_temperature_min`/`_max` °C, `college_water_reach` m, `college_flood_limit` 0..1, `college_suitability_min` 0..1 |

Measured profile college limits (all thirteen profiles, from `civilizations.json`): `college_flood_limit` 0.5 and `college_suitability_min` 0.55 everywhere; `college_slope_limit` 15 except dwarf/frosthold_dwarf at 22; `college_water_reach` 600 m except human_maritime/human_cold/human_large_island 700, human_desert 850, human_rainforest 650; `college_temperature_min/max` from (-19, 24) for human_cold to (5, 48) for human_desert, most at (-5, 38).

## Outputs

## `result['magic']` -- schema **version 4**, written whole by `generate_networks`

```
magic = {
  'version': 4,
  'school_order': ['weave','umbral','infernal','radiant','fire','water','earth','air',
                   'blood','void','rot','eldritch'],          # list(SCHOOLS) -- ORDER IS A CONTRACT
  'groups': ['Raw magic','Holy / unholy','Primordial','Outside'],  # dict.fromkeys over school groups
  'networks': { <school name> : NetworkRecord, ... },          # 12 entries, insertion-ordered
  'colleges': [],                                              # filled later by add_colleges
  'diagnostics': pending_college_diagnostics(cfg.college_count),
  'enabled': bool(cfg.magic_enabled),
  'mutation_threshold': 0.35,     # WRITE-ONLY. Nothing in the repo reads it.
  'dominance_margin': 0.08,       # WRITE-ONLY. Nothing in the repo reads it.
  'method': <long prose string, see Constants>
}
```

`NetworkRecord`:
```
{ 'name': str,                 # == the dict key
  'group': str,                # 'Raw magic' | 'Holy / unholy' | 'Primordial' | 'Outside'
  'descriptors': str,          # comma-joined flavour words
  'color': [int,int,int],      # 0..255 RGB
  'seed': uint32,              # child_seed(cfg.seed, 'history-ley-'+name, variation)
  'strength': float 0..2,      # amplitude, from options
  'width_m': float,            # AUTHORED WIDTH x width_scale -- POST-SCALING METRES
  'instability': float 0..1,
  'nodes': [ {'id': str, 'direction': [x,y,z] unit, 'intensity': float 0..4}, ... ],
  'edges': [ {'id': str, 'from': int, 'to': int, 'intensity': float 0..4,
              'path': [[x,y,z] x 33]}, ... ],
  'distribution': {...} or None }
```
`distribution` (None when the school did not manifest):
```
{'version': 2, 'model': 'landscape_alignments', 'aligned_nodes': [0,2,1] or [],
 'clusters': int 1..4, 'spread': float 0.12..0.8,
 'outlier_fraction': float 0.05..0.4, 'extra_link_probability': float 0.08..0.45}
```

**Id fields, exactly:**
- `magic.networks` is keyed by **school name** (the dict key); each record repeats it in `name`.
- `nodes[].id` -- a string. Generated ones are `"<school>-node-<i>"` (i = 0-based index). Other writers use `"<ruin id>-key"` (ruin legacy), `"corrupt-<god id>-<index>-<k>"` (corruption cluster), `"<school>-probe"` (tests). Must be non-empty and unique **within its own network**; nothing enforces global uniqueness across networks.
- `edges[].id` -- `"<school>-line-<i>"`. `from`/`to` are **indices into that network's own `nodes` list**, not ids.
- `magic.nodes` / `magic.edges` (the flat projections written by `evaluate_networks`) -- `magic.nodes` is a bare list of 3-vectors **with no id at all**; `magic.edges[]` carries `network` (school name), the original `id`, `intensity`, `path`, plus `strength` (a duplicate of `intensity`) and `from`/`to` **re-offset into the flat `magic.nodes` list**.
- `magic.colleges[].id` -- `"college-<1-based>"`; also `kind: 'wizard_college'`.
- `magic.diagnostics[]` is keyed by `institution` (`'wizard_college'`).

## Layers written by `evaluate_networks` (all `n x n`, seam and poles closed by `node_grid`)

| Layer | Range | Meaning |
|---|---|---|
| `ley_<school>` x12 | 0..strength (may be **-0.0**) | saturated potency of that school |
| `instability_<school>` x12 | 0..1 (may be -0.0) | `min(1, ley * instability)` |
| `magic_density` | 0..1 | `1 - exp(-sum of all twelve ley values)` |
| `magic_hazard` | 0..1 | `min(1, 0.3*sum(instability) + 0.2*conflict)` |
| `magic_growth` | 0..~1 | fertile share of local potency |
| `magic_opposition` | 0..1 | `min(1, conflict)` |
| `dominant_magic` | int -1..11 | index into `school_order`, or -1 |
| `ley_holy` | alias | **the same object as `ley_radiant`** (see Edge cases) |
| `ley_primordial` | 0..1 | per-cell max of fire/water/earth/air |
| `magic_risk_<people>` x12 | 0..1 | **an exact copy of `magic_hazard` for every people** |

## Written by `add_colleges`

```
magic['colleges'] = [ {
    'id': 'college-<k>', 'kind': 'wizard_college',
    'node': int,                 # sphere-grid node index
    'x': int, 'z': int,          # grid cell
    'core_id': int,              # the OWNING CITY'S integer id (== its index in settlements.sites)
    'population_profile': str,   # copied from that city
    'culture_id': str,           # humans.cores[core_id].culture_id
    'density': float, 'hazard': float, 'suitability': float,   # the values at this cell
    'access_nodes': [int, ...],  # [college node, ..., city node] walked back through parents
    'access_cost': float,        # metres of road cost, == distance[i]
    'reason': 'Strong magic within population mutation, slope, climate, freshwater, flood '
              'and suitability limits; reachable from a city.' } ]
magic['diagnostics'] = college_diagnostics(...)     # exactly one row
magic['college_spacing_m'] = float
magic['college_method'] = <prose string>
result['timing_ms']['colleges'] += elapsed ; result['timing_ms']['total'] += elapsed
```

Diagnostic row shape (`_row`), **all seven keys always present**:
`{'institution':'wizard_college', 'placed':int, 'wanted':int, 'candidates':int, 'sources':int, 'source_kind':"cells within a city's support reach", 'reason':str}`

## Algorithm

════════════════════════════════════════════════════════════════════
PART A -- `network_geometry(seed, count) -> (nodes, edges, distribution)`
════════════════════════════════════════════════════════════════════

One RNG: `rng = Random(child_seed(seed, 'clustered-ley-v1'))`. `seed` is already the
per-school child seed, so this is a second derivation level.

Helper `unit()` -- uniform point on the sphere, **two draws, in this order**:
```
y = rng.uniform(-1, 1)
a = rng.uniform(-pi, pi)
r = sqrt(1 - y*y)
return (r*cos(a), y, r*sin(a))
```

A1. `clusters = rng.randint(1, min(4, count))`            # inclusive both ends, 1..4
A2. `centres = [unit() for _ in range(clusters)]`
A3. `spread  = rng.uniform(0.12, 0.8)`                    # gaussian sigma in R^3 before renormalising
A4. `outlier = rng.uniform(0.05, 0.4)`                    # probability a node ignores its cluster

A5. Place `count` nodes. For each node index (0..count-1):
```
group = rng.randrange(clusters)                  # 0..clusters-1
for attempt in 0..999:
    if rng.random() < outlier:
        p = unit()
    else:
        q = [v + rng.gauss(0, spread) for v in centres[group]]   # three gauss draws
        length = sqrt(q.q)
        p = q / length                                           # renormalise onto the sphere
    if all( abs(dot(p, existing)) < 0.9999 for existing in nodes ):
        break
else:
    raise ValueError('Could not separate ley points')
nodes.append(p); groups.append(group)
```
The separation test uses **abs**, so it rejects a point that is nearly coincident with
OR nearly antipodal to an existing node. That is deliberate: `arc_frame` divides by
`sin(angle)`, which vanishes at both 0 and pi, so an antipodal pair would produce a
garbage tangent and a meaningless leyline.

A6. **The sacred alignment.** If `count >= 3`:
```
arc = arc_frame(nodes[0], nodes[1])               # (a, b, tangent, angle)
t   = rng.uniform(0.3, 0.7) * arc.angle
nodes[2] = arc.a * cos(t) + arc.tangent * sin(t)  # a point ON the 0->1 great circle
groups[1] = groups[2] = groups[0]
```
Node 2 is *moved* onto the great circle between nodes 0 and 1, 30-70% of the way along.
Comment, preserve verbatim in spirit: *"Three sacred loci share a straight spherical
alignment; other loci remain scattered. This is a fantasy interpretation of Watkins'
alignments, not a physical law."* Note node 2's cluster membership is rewritten to node
0's group, and so is node 1's, so those three always end up in the same cluster.

A7. `pairs = {(0,2), (1,2)}` if `count >= 3` else `{}` (a **set of sorted index pairs**).
The alignment is published as an edge pair through the middle node, which is why
`distribution.aligned_nodes` is `[0, 2, 1]` -- the order along the line, not sorted.

A8. **Per-cluster minimum spanning tree (Prim's).** For `group` in `0..clusters-1`:
```
members = [i for i, g in enumerate(groups) if g == group]     # ascending index order
if not members: continue                                       # a group can be emptied by A6
joined = {members[0]}
while len(joined) < len(members):
    (_, a, b) = min( (1 - dot(nodes[a], nodes[b]), a, b)
                     for a in sorted(joined)
                     for b in members if b not in joined )
    pairs.add(sorted_pair(a, b)); joined.add(b)
```
Cost function is the **versine** `1 - cos(angle)`, monotone in great-circle distance.
Tie-break is the tuple `(cost, a, b)` -- lowest `a`, then lowest `b`. `sorted(joined)`
is what makes the tie-break deterministic; a raw set iteration would not be.
**Each cluster gets its own tree, so a school with several clusters has a disconnected
network.** That is intended: *"Sparse trees within clusters; some schools have
disconnected concentrations."* This is O(|joined| x |members|) per step, i.e. O(m^3)
for m members -- fine at m <= 24.

A9. **Extra links.** `link_chance = rng.uniform(0.08, 0.45)`, then for `a` in `0..count-1`:
```
if rng.random() < link_chance:
    choices = [b for b in range(count) if b != a]
    b = rng.choice(choices)
    pairs.add(sorted_pair(a, b))
```
Note the draw for `rng.choice` is only consumed when the probability test passes, so the
stream position is data-dependent.

A10. **Edges.** Iterate `sorted(pairs)` (ascending `(a, b)`). For each:
```
frame = arc_frame(nodes[a], nodes[b])
path  = [ frame.a * cos(frame.angle * k/32) + frame.tangent * sin(frame.angle * k/32)
          for k in 0..32 ]              # 33 points; path[0] == a, path[32] == b
edges.append({'from': a, 'to': b, 'path': path})
```

A11. Return `(nodes, edges, distribution)` with
`{'version':2, 'model':'landscape_alignments', 'aligned_nodes': [0,2,1] if count>=3 else [],
  'clusters':clusters, 'spread':spread, 'outlier_fraction':outlier,
  'extra_link_probability':link_chance}`.

════════════════════════════════════════════════════════════════════
PART B -- `generate_networks(result, cfg)`
════════════════════════════════════════════════════════════════════

B1. `o = options(cfg)` -- the resolved 72 school options.

B2. **The width scale.**
```
width_scale = result['effective_config']['globe_radius'] / REFERENCE_RADIUS_M
            = globe_radius / 1774.4123532462844
```
Comment to carry: *"Authored school widths are a reach on the reference world (design
radius 10000 at the recipe scale, 1774.4 m physical). Magic has to cover the same share
of the world at every circumference: left as absolute metres, a 110 m Gaussian on a
200 km world falls entirely between raster cells, and every ley field, magic density,
biome variant, college and magic-gated habitat collapses to zero."*
Measured: the 200 km default gives `globe_radius = 31830.988618379066`, `width_scale =
17.9394`, so the authored 110 m becomes `width_m = 1973.278` m.

B3. For each `(name, (group, descriptors, color))` in **`SCHOOLS` insertion order** (the
eight known, then the four hidden):
```
seed = child_seed(cfg.seed, 'history-ley-' + name, o[name + '_variation'])
enabled = cfg.magic_enabled and (Random(seed).random() < o[name + '_occurrence'])
nodes, edges = [], []
distribution = None
if enabled:
    positions, links, distribution = network_geometry(seed, o[name + '_nodes'])
    irng = Random(child_seed(seed, 'intensities'))
    nodes = [ {'id': f'{name}-node-{i}', 'direction': list(p),
               'intensity': irng.uniform(0.35, 1.65)} for i, p in enumerate(positions) ]
    edges = [ {**e, 'id': f'{name}-line-{i}',
               'intensity': irng.uniform(0.35, 1.65)} for i, e in enumerate(links) ]
networks[name] = { ... as in Writes ... , 'width_m': o[name+'_width'] * width_scale }
```
`irng` draws **all node intensities first, in index order, then all edge intensities in
index order**, from one stream.

The occurrence draw uses a **fresh `Random(seed)`** and consumes exactly one `random()`
call, so it cannot disturb the geometry stream. For a hidden school `occurrence` is
`0.0` and `random()` returns `[0,1)`, so `< 0.0` is never true -- hidden schools are
always empty at generation. The `cfg.magic_enabled and ...` short-circuit means the draw
is skipped entirely when magic is off, which is harmless because each school owns its
own `Random`.

**Dead local:** the body builds `temporary = {'climate': True, 'layers': {}, ...}` and
never uses it. Do not port it.

B4. Write `result['magic']` as in Writes, then **tail-call `evaluate_networks(result, cfg)`**.

════════════════════════════════════════════════════════════════════
PART C -- geometry primitives (`terrain_magic.py`)
════════════════════════════════════════════════════════════════════

```
arc_frame(a, b):
    cosine  = clamp(dot(a,b), -1, 1)
    angle   = acos(cosine)
    tangent = (b - cosine*a) / max(1e-12, sin(angle))     # component-wise
    return (a, b, tangent, angle)
```
`tangent` is `b` orthogonalised against `a` and normalised. The `max(1e-12, ...)` guard
means a degenerate frame (a == b, angle 0) yields `tangent = (0,0,0)` rather than NaN,
and an antipodal frame yields a garbage but finite tangent -- which is why A5 excludes
both cases at placement time.

```
distance_to_frame(p, frame):           # angular distance from p to the ARC SEGMENT a->b
    a, b, tangent, angle = frame
    pa = dot(p, a); pt = dot(p, tangent)
    along = atan2(pt, pa)              # signed angular position along the great circle
    best  = max(pa, dot(p, b))         # cosine of distance to the nearer ENDPOINT
    if 0 <= along <= angle:            # the foot of the perpendicular lies inside the arc
        best = max(best, hypot(pa, pt))
    return acos(clamp(best, -1, 1))
```
Verified numerically: `distance_to_frame(pole, frame(x̂, ŷ)) == pi/2`;
`distance_to_frame(x̂, frame(x̂,ŷ)) == 0`; midpoint == 0; antipode == pi/2; and a
degenerate frame `arc_frame(a, a)` correctly returns the angle from `a`.

`arc_distance(p, a, b)` is `distance_to_frame(p, arc_frame(a, b))` -- the uncached form.

════════════════════════════════════════════════════════════════════
PART D -- `evaluate_networks(result, cfg)` : THE FIELD EVALUATOR
════════════════════════════════════════════════════════════════════

D0. Setup.
```
n      = cfg.size
radius = result['effective_config']['globe_radius']
points, _, graph = sphere_grid(n, radius)     # per-node (x,z), area, [(neighbour, metres)]
vectors = [direction(x, z, n) for (x, z) in points]
feeds   = hidden_feeds(result, magic, vectors, radius)
```

D1. **`hidden_feeds`** -- the multipliers blood and eldritch apply to their own raw sum.
Preserve this docstring's content: *"Blood reads the living: many people, close together.
Eldritch reads what the world already fears, its aberrant and undead nests and its ruins.
Both read the world as it stood before this rebuild, because `evaluate_networks` runs
ahead of `civilization()` and `add_nests`, so a corruption is always one rebuild behind
the cities it is eating. Both read defensively: at stage nine there are no settlements,
nests or ruins at all."*

```
blood = networks.get('blood')
if blood and blood['nodes']:
    cities  = [c for c in result.get('settlements',{}).get('sites',[]) if c.get('direction')]
    largest = max((c.get('population_estimate') or 0.) for c in cities) if cities else 0.
    sources = [(c['direction'], (c.get('population_estimate') or 0.)/largest)
               for c in cities] if largest else []
    feeds['blood'] = [1. + 2.5*v for v in _saturating_field(sources, vectors, radius,
                                                            blood['width_m'])]
eldritch = networks.get('eldritch')
if eldritch and eldritch['nodes']:
    sources  = [(nest['direction'], 1.) for nest in result.get('beast_nests',{}).get('sites',[])
                if nest.get('direction') and nest.get('family') in ('aberrant','undead')]
    sources += [(ruin['direction'], 1.) for ruin in result.get('ruins',[])
                if ruin.get('direction')]
    feeds['eldritch'] = [1. + 2.5*v for v in _saturating_field(sources, vectors, radius,
                                                               eldritch['width_m'])]
```
Note the guard is `net['nodes']` truthiness -- a school with only edges (impossible in
practice, since edges index nodes) gets no feed.

```
_saturating_field(sources, vectors, radius, width_m):
    for each p in vectors:
        total = sum over (point, weight) of
                weight * exp( -( radius * acos(clamp(dot(p, point), -1, 1)) / width_m )^2 )
        field.append( 1 - exp(-total) )      # spelled -expm1(-total)
```
Blood's weights are population **normalised by the largest city**, so a world's biggest
city contributes 1.0 and everything else a fraction. Eldritch's weights are all 1.0 and
nests and ruins are pooled into one list (nests first, ruins after -- summation order,
and therefore the last ulp, depends on it).

D2. **Per-school raw potency.** Iterate `magic['networks']` in insertion order.
```
nodes, edges = net['nodes'], net['edges']
width, strength = net['width_m'], net['strength']
feed = feeds.get(name)

if not nodes and not edges and feed is None:
    powers[name] = [ strength * -expm1(-0) ] * len(vectors)      # NEGATIVE ZERO, see below
    continue
```
Comment to carry verbatim in substance: *"A school with no nodes and no lines sums to
integer zero for every point, so every point gets the identical value -- and that value
is NEGATIVE zero, since `-math.expm1(-0)` is `-0.0` and the sign survives the multiply.
It is written as the same expression rather than a literal so it cannot drift from the
loop. The four hidden schools take this path in every generated world: their occurrence
is locked at zero, so generation never raises one."*

Precompute, hoisted out of the per-cell loop (*"at size 513 that is 263k iterations x
twelve schools"*):
```
node_terms = [(node.intensity, node.direction) for node in nodes]
edge_terms = [( e.intensity * (nodes[e.from].intensity + nodes[e.to].intensity) / 2,
                arc_frame(nodes[e.from].direction, nodes[e.to].direction) )
              for e in edges]
```
An edge's effective weight `k` is **its own intensity times the mean of its two
endpoints' intensities** -- a leyline is only as strong as the key points it joins.

Then per cell `i` with direction `p`:
```
total  = sum over node_terms of intensity * exp( -( radius*acos(clamp(dot(p,point))) / width )^2 )
total += sum over edge_terms of k         * exp( -( radius*distance_to_frame(p,frame) / width )^2 )
if feed is not None: total *= feed[i]      # APPLIED BEFORE SATURATION
power[i] = strength * (1 - exp(-total))    # spelled strength * -expm1(-total)
```
Comment: *"Applied before saturation, so a well-fed field still tops out at strength."*
The node sum runs first, then the edge sum, and both accumulate in list order.

D3. **Rot: contagion instead of falloff.**
```
rot = magic['networks'].get('rot')
if rot is not None and rot['nodes']:
    powers['rot'] = rot_spread(powers['rot'], rot, graph, radius)
```
```
rot_spread(power, net, graph, radius):
    decay_m  = 2.0 * net['width_m']                     # ROT_DECAY_WIDTHS
    arcs     = [d for edges in graph for (_, d) in edges]      # every neighbour distance
    mean_arc = sum(arcs)/len(arcs) if arcs else 0.
    if not mean_arc or not decay_m: return power
    steps = min(64, max(1, round(6.0 * net['width_m'] / mean_arc)))    # ROT_REACH_WIDTHS, ROT_MAX_STEPS
    for _ in range(steps):
        carried = [ max( [value] + [ power[j] * exp(-distance/decay_m)
                                     for (j, distance) in graph[i] ] )
                    for i, value in enumerate(power) ]
        if carried == power: break              # elementwise equality on the whole vector
        power = carried
    return power
```
This is a **max-plus (Bellman-Ford style) relaxation, not a sum**: each cell takes the
best of its own value and every neighbour's value attenuated over that specific edge's
own arc length in metres. The whole vector is recomputed from the *previous* iteration's
values (Jacobi, not Gauss-Seidel) -- `carried` is built entirely from `power` before
`power` is replaced, so the sweep is order-independent within a step.
Docstring to carry: *"Rot creeps cell to cell instead of falling off from a node. The
creep is measured in metres, never in cells: each step carries a neighbour's value across
that edge's own arc length, decaying over a length derived from the network's width. A
33-grid world and a 257-grid world therefore rot at the same physical rate, which a fixed
iteration count over cells would not give."*
The test `test_rot_creeps_at_the_same_physical_rate_on_a_finer_grid` asserts the 17-grid
and 33-grid reaches agree within 35%, and `test_rot_creeps_past_its_own_falloff` asserts
rot still carries a tenth of its peak beyond `2 x width_m`, where the bare Gaussian is
`exp(-4)` -- under 2%.

D4. **Void: subtraction, not addition.**
```
if magic['networks'].get('void', {}).get('nodes'):
    void_bite(powers, list(magic['networks']))
```
```
void_bite(powers, names):
    void = powers.get('void')
    if void is None: return
    for i, bite in enumerate(v * 0.8 for v in void):        # VOID_BITE
        if not bite: continue                               # skips 0.0 AND -0.0
        for name in names:
            if name != 'void':
                powers[name][i] = max(0., powers[name][i] - bite)
```
Docstring, load-bearing: *"Void does not add potency, it takes it: every other school
loses ground to it. The subtraction is skipped where void is absent rather than applied
as a zero, because `max(0., x - 0.)` would turn the negative zero an unmanifested network
writes into a positive one and move every magic-disabled world's bytes."* The `if not
bite: continue` is the whole point -- a naive "always subtract" implementation is
functionally identical in value and wrong in sign, and the test
`test_void_absent_does_not_flip_a_negative_zero` guards it.

D5. **Write the per-school layers**, iterating `magic['networks']`:
```
l['ley_' + name]          = node_grid(power, points, n)
l['instability_' + name]  = node_grid([min(1., v * net['instability']) for v in power], points, n)
```

D6. **Composites.** Build once before the sweep (*"Rebuilt per point they were twelve
string concatenations, twelve dict lookups and a fresh `list(SCHOOLS)` whose `.index()`
then scanned it -- per cell, 263k times at 513"*):
```
school_names      = list(SCHOOLS)              # the module constant, NOT magic['networks']
school_index      = {name: i for i, name in enumerate(school_names)}
ley_grids         = [l['ley_' + name]         for name in school_names]
instability_grids = [l['instability_' + name] for name in school_names]
```
Then for each `(x, z)` in `points`, in `points` order:
```
p        = {name: grid[z][x] for name, grid in zip(school_names, ley_grids)}
total    = sum(p.values())                                         # SCHOOLS order
conflict = min(p.radiant, p.infernal) + min(p.weave, p.umbral) + min(p.fire, p.water)
density   = 1 - exp(-total)
opposition= min(1., conflict)
hazard    = min(1., sum(instability_grids[k][z][x] for k) * 0.3 + conflict * 0.2)
growth    = (p.earth + 0.6*p.radiant + 0.5*p.weave + 0.4*p.water) / max(total, 1e-12)
winner    = dominant_school(p)                  # DEFAULT threshold/margin -- see below
winners.append(school_index[winner] if winner else -1)
```
Note `p` is read back out of the **grids**, not out of `powers`. Numerically identical,
but it means D5 must complete before D6.

D7. Write the composites:
```
for key, vals in [('magic_density', density), ('magic_hazard', hazard),
                  ('magic_growth', growth), ('magic_opposition', opposition),
                  ('dominant_magic', winners)]:
    l[key] = node_grid(vals, points, n)
```

D8. Compatibility projections (*"for existing ecological/nest requirements, never extra
networks"*):
```
l['ley_holy']       = l['ley_radiant']          # ALIAS, not a copy
l['ley_primordial'] = node_grid([max(l['ley_'+s][z][x] for s in ('fire','water','earth','air'))
                                 for (x,z) in points], points, n)
```

D9. Per-people risk:
```
for people in civilization_ids():               # 12 entity profiles, registry order
    l['magic_risk_' + people] = node_grid(hazard, points, n)
```
Every one of these is the same field. The per-people distinction exists so a consumer can
read a people-specific hazard without a fallback, and so the fields can diverge later
without a schema change; today they do not differ.

D10. Flatten the geometry for consumers:
```
magic['nodes'], magic['edges'] = [], []
for name, net in magic['networks'].items():          # INSERTION ORDER
    offset = len(magic['nodes'])
    magic['nodes'].extend(node['direction'] for node in net['nodes'])
    magic['edges'].extend({**e, 'from': e['from']+offset, 'to': e['to']+offset,
                           'network': name, 'strength': e['intensity']}
                          for e in net['edges'])
magic['compatibility_fields'] = {'ley_holy': 'radiant',
                                 'ley_primordial': 'maximum of fire, water, earth, air'}
```

════════════════════════════════════════════════════════════════════
PART E -- `dominant_school(potencies, threshold=0.35, margin=0.08)`
════════════════════════════════════════════════════════════════════
```
ranked = heapq.nsmallest(2, ((-v, k) for k, v in potencies.items()))
if not ranked: return None
winner = ranked[0][1]
runner = -ranked[1][0] if len(ranked) > 1 else 0
return winner if potencies[winner] >= threshold
              and potencies[winner] - runner >= margin
              else None
```
Comment: *"Only the top two of twelve are ever read, so a partial selection replaces a
full sort: this runs once per cell. `(-potency, name)` tuples compare exactly as the old
sort key did, and no float is added or reordered -- these are comparisons only."*

Semantics: the winner is the school with the highest potency; **ties are broken by
lexicographically smallest school name**, NOT by `SCHOOLS` order. It returns `None`
unless the winner clears an absolute floor of 0.35 AND leads the runner-up by at least
0.08 in absolute potency. `-0.0` fails `>= 0.35`, so an empty world yields -1 everywhere.

The block publishes `mutation_threshold: 0.35` and `dominance_margin: 0.08` beside these
defaults and **nothing in the repository ever reads them back**. Keep the two in step or
delete the published pair; a porter who wires the published values in is not wrong, but
must then make every other caller pass them too (`terrain_history`, `terrain_ruins`,
`terrain_wars` all call `dominant_school(potencies)` bare).

════════════════════════════════════════════════════════════════════
PART F -- `edit_network(network, *, node_id=, line_id=, intensity=, new_node=)`
════════════════════════════════════════════════════════════════════
The **only** sanctioned mutation boundary for a network. Copy-on-write; returns a new
record and never mutates the argument.
```
net = deepcopy(network)
if count_of_non_none(node_id, line_id, new_node) != 1:
    raise 'Select one node, line, or new key point'
if new_node is not None:
    if not dict or set(new_node.keys()) != {'id','direction','intensity'}:
        raise 'New node requires id, direction, intensity'
    p = new_node['direction']
    if id not a non-empty str, or id already present in net['nodes']:
        raise 'Node id must be unique and nonempty'
    if p not a 3-element list/tuple of finite int|float:
        raise 'Direction must be a finite unit vector'
    if abs(|p|^2 - 1) > 1e-6:
        raise 'Direction must be a unit vector'
    intensity = new_node['intensity']
    target = deepcopy(new_node); net['nodes'].append(target)
else:
    collection = net['nodes'] if node_id is not None else net['edges']
    target = first element whose 'id' == (node_id or line_id), else None
    if target is None: raise 'Unknown key point or line'
if intensity not finite int|float in [0, 4]:
    raise 'Intensity must be finite and between 0 and 4'
target['intensity'] = intensity
return net
```
Three traps, all recorded elsewhere in the repo and all load-bearing:
- It **SETS** intensity; it never adds. `terrain_nomad_effects` records that a queued
  "add `DEVOTION_GAIN * shrines`" reading would have cut a node at 3.0 with three shrines
  down to 0.54 in the act of deepening it.
- A new node is **a local source with no lines**. Docstring: *"A new key point is a local
  source until a future caller supplies connecting lines."* Nothing auto-connects it.
- `intensity` is validated even on the `node_id`/`line_id` path, so calling with only
  `node_id` and no `intensity` raises (`type(None)` is not int/float).

`terrain_history.validate_age_world` reuses this as a validator by calling
`edit_network({'nodes':[],'edges':[]}, new_node={k: node[k] for k in ('id','direction',
'intensity')})` on every node of a supplied world, and separately enforces:
`0 <= strength <= 2`, `10 <= width_m <= 2000`, `0 <= instability <= 1`,
`len(nodes) <= 1024`, `len(edges) <= 4096`, unique node ids and line ids within a
network, `0 <= from,to < len(nodes)` with `from != to`, `0 <= edge.intensity <= 4`.
The public `/advance` API additionally caps `leyline_edits` at 128 per request and
**refuses any edit naming a hidden school** (`edit['school'] not in KNOWN_SCHOOLS`).

════════════════════════════════════════════════════════════════════
PART G -- `add_colleges(result, cfg)` : the college pass
════════════════════════════════════════════════════════════════════

G0. `if not result.get('magic') or not result.get('humans'): return result` -- the block
keeps its pending diagnostic row.

G1. Setup.
```
profile = get_profile(cfg.population_profile)     # effectively a VALIDATION call; shadowed below
n = cfg.size; r = result['effective_config']['globe_radius']
points, _, graph = sphere_grid(n, r)
vals(k) = [layers[k][z][x] for (x,z) in points]
density, hazard, slope, temp, fresh, flood, suitability, water =
    vals('magic_density'), vals('magic_hazard'), vals('slope'), vals('temperature'),
    vals('freshwater_distance'), vals('flood_risk'), vals('suitability'), vals('water_type')
cost = road_cost_function(points, water, vals('height'), flood, vals('rain_river'), cfg, hazard)
cities = result['settlements']['sites']
distance, owner, parent = allocate_access(graph, [(s['node'], s['id']) for s in cities],
                                          cost, cfg.support_reach)
city_profiles = [get_profile(s['population_profile']) for s in cities]
parent_by_city = {}
```

`allocate_access` is a **multi-source Dijkstra with ownership**: every city node starts at
distance 0 labelled with its own integer id; relaxation accepts `candidate <= limit` and
`(candidate < distance[j]) or (candidate == distance[j] and label < owner[j])`. The
tie-break is on the **integer city id**, lowest wins -- deterministic, no RNG. `parent[j]`
is the predecessor on the winning path. The priority queue is keyed `(value, label, node)`.

`road_cost_function(i, j, d)` returns `None` (impassable) when: either endpoint is water;
`d <= 0`; `max(hazard[i], hazard[j]) > cfg.human_magic_limit`; the step is diagonal and
either of the two corner cells it would slip between is water or over the hazard limit
(poles excluded); or `|height[j] - height[i]| / d > cfg.road_max_grade`. Otherwise
`d * (1 + 12*grade^2 + 0.5*(flood[i] + flood[j])) + (cfg.bridge_cost if river[i] or river[j])`.

G2. **Recipe-3 rework** -- `if cfg.world_recipe:` (truthy, so any non-zero recipe):
```
distance = [inf] * len(points); owner = [-1] * len(points)
for city, p in zip(cities, city_profiles):
    risk = vals('magic_risk_' + p['id']) if that layer exists else hazard
    own_cost = road_cost_function(points, water, vals('height'), flood, vals('rain_river'),
                                  replace(cfg, human_magic_limit=p['mutation_limit'],
                                               road_max_grade=p['road_grade_limit']),
                                  risk)
    own_distance, own_parent = shortest_paths(graph, city['node'], own_cost)   # UNBOUNDED
    parent_by_city[city['id']] = own_parent
    for i, d in enumerate(own_distance):
        if d < distance[i] and d <= cfg.support_reach:
            distance[i] = d; owner[i] = city['id']
hazard = [ layers.get('magic_risk_' + city_profiles[o]['id'], layers['magic_hazard'])[z][x]
           if o >= 0 else 1.
           for (x, z), o in zip(points, owner) ]
```
This replaces the single shared allocation with **one single-source Dijkstra per city,
each using that people's own mutation limit and road grade limit**, then takes the
per-cell minimum. Note the tie-break here is **strictly `<`**, so on an exact tie the
earlier city in `cities` order keeps the cell -- a different rule from `allocate_access`'s
explicit `label < owner[j]`, but equivalent because city ids ascend with list order.
Unowned cells get `hazard = 1.0`, which would fail every limit; they are skipped anyway.
`shortest_paths` is called **without a limit**, so it explores the whole passable
component and the reach is applied afterwards -- an O(component) cost per city that a
bounded search would avoid.

G3. **Per-owner suitability** -- runs unconditionally, recipe or not:
```
suitability = [ layers.get('suitability_' + cities[o]['population_profile'],
                           layers['suitability'])[z_i][x_i] if o >= 0 else 0
                for i, o in enumerate(owner) ]
```

G4. Clearance and spacing setup.
```
occupied = [direction(s['x'], s['z'], n)
            for s in cities + result['humans']['hamlets'] + result['humans']['fortresses']]
colleges = []; college_points = []
spacing  = college_spacing_m(r) if cfg.world_recipe == 3 else 150
         # college_spacing_m(r) = max(1500., r * 0.15)
candidates = 0; refusals = {}; refused_clearance = 0; refused_spacing = 0
sources = count of i where owner[i] >= 0
```
Note the **inconsistency**: G2 tests `if cfg.world_recipe:` (truthy) while G4 tests
`cfg.world_recipe == 3` (exact). Recipe 0 gets the shared allocation and 150 m spacing;
recipe 3 gets per-city allocation and geometric spacing. No other recipe value exists
today, but a recipe 1 or 2 would take the per-city path with the 150 m spacing.

G5. **The placement sweep.** Iterate cells in a fixed deterministic order:
```
for i in sorted(range(len(points)),
                key = lambda i: ( -(density[i]*(1 - hazard[i]) + 0.25*suitability[i]), i )):
```
The score is **magic you can survive, plus a quarter weight on how good the ground is**,
descending; ties broken by ascending node index. No RNG anywhere in this pass.

```
    if owner[i] < 0: continue                    # outside every city's reach
    profile = city_profiles[owner[i]]            # SHADOWS the outer `profile`
    limit   = profile['mutation_limit'] if cfg.world_recipe else cfg.human_magic_limit
    refusal = college_refusal(density[i], hazard[i], limit, slope[i], temp[i],
                              fresh[i], flood[i], suitability[i], water[i], profile)
    if refusal is not None:
        refusals[refusal] = refusals.get(refusal, 0) + 1
        continue
    candidates += 1
    if len(colleges) >= cfg.college_count: continue      # budget: COUNTED but not placed
    x, z = points[i]; p = direction(x, z, n)
    if any( r * acos(clamp(dot(p, q))) < 150 for q in occupied ):
        refused_clearance += 1; continue
    if any( r * acos(clamp(dot(p, q))) < spacing for q in college_points ):
        refused_spacing += 1; continue
    path = [i]
    own_parent = parent_by_city[owner[i]] if cfg.world_recipe else parent
    while own_parent[path[-1]] >= 0: path.append(own_parent[path[-1]])
    colleges.append({...})                        # see Writes
    occupied.append(p); college_points.append(p)
```
The 150 m settlement clearance is a **hard-coded absolute metre value that is NOT scaled
by world size**, unlike `spacing`. On the 200 km default world 150 m is well under one
cell, so it effectively only excludes a cell that coincides with a settlement; on the
11.15 km reference world it was a real exclusion radius. The `radius=40.` unit test
exploits this: on a 40 m globe the whole surface lies inside 150 m, so every candidate is
refused for clearance and the diagnostic says "no room" rather than "no ground".

Comment to carry on why the budget check sits below the limits: *"Counted for the
diagnostic, never read by placement. The budget check moved below the limits so a cell the
budget never reached is still counted as a candidate: placement is unchanged by that --
the checks above it have no side effects and a `continue` past the placement body leaves
colleges, occupied and college_points exactly as the old `break` did."*

G6. Publish `colleges`, `diagnostics`, `college_spacing_m`, `college_method`, and add the
elapsed milliseconds to `timing_ms['colleges']` and `timing_ms['total']`.

════════════════════════════════════════════════════════════════════
PART H -- the eight college limits
════════════════════════════════════════════════════════════════════
```
COLLEGE_LIMITS = ('density','hazard','water','slope','temperature',
                  'freshwater','flood','suitability')          # TEST ORDER, load-bearing

college_refusal(density, hazard, limit, slope, temp, fresh, flood, suitability, water,
                profile=None) -> str | None:
    if profile is None: profile = get_profile(default_profile_id())   # 'human_heartland'
    if not density >= 0.35:                                       return 'density'
    if not hazard <= limit:                                       return 'hazard'
    if not water == 0:                                            return 'water'
    if not slope < profile['college_slope_limit']:                return 'slope'
    if not profile['college_temperature_min'] < temp
           < profile['college_temperature_max']:                  return 'temperature'
    if not 0 <= fresh <= profile['college_water_reach']:           return 'freshwater'
    if not flood < profile['college_flood_limit']:                 return 'flood'
    if not suitability >= profile['college_suitability_min']:      return 'suitability'
    return None

college_eligible(...) = college_refusal(...) is None
```
Each clause is written `if not <predicate>` rather than the negation, so NaN in any input
refuses rather than passes. The clause order **is the original `and` chain's
short-circuit order**, so "the limit that refused" keeps its historical meaning.
Docstring: *"This is the definition of college eligibility; `college_eligible` is this
predicate read as a boolean. The diagnostic needs to say which limit refused, and a second
copy of the gate written for the report is how a report starts disagreeing with the thing
it reports on."* There is a test whose only job is to assert the two cannot drift.

`density >= 0.35` is the only threshold not drawn from a profile -- it is a bare literal
and is **numerically the same 0.35 as `dominant_school`'s threshold but a different
quantity**: this one is composite `magic_density`, that one is a single school's potency.

`freshwater_distance` is negative when the cell has no freshwater source at all, which is
why the test is a two-sided `0 <= fresh <= reach` rather than `fresh <= reach`.

════════════════════════════════════════════════════════════════════
PART I -- diagnostics
════════════════════════════════════════════════════════════════════
```
SOURCE_KIND = "cells within a city's support reach"

pending_college_diagnostics(wanted) ->
  [ _row(0, int(wanted), 0, 0,
         'the college pass has not run at this phase, so nothing was evaluated') ]
```
Docstring: *"`generate_networks` writes `colleges: []` and the pass fills it later, so
between the two the empty list means 'not yet', not 'none placed'. Those read alike in a
finished document if only one of them says anything."*

```
college_diagnostics(placed, wanted, candidates, sources, refusals, clearance, spacing):
  if wanted <= 0:
      reason = 'no colleges were requested'
  elif placed >= wanted:
      reason = f'{placed} of {candidates} candidates were seated, which is every college asked for'
  elif placed:
      reason = (f'{placed} of {wanted} colleges were seated from {candidates} candidates; '
                f'settlement clearance refused {clearance} and regional spacing refused {spacing}')
  elif candidates:
      reason = (f'no room: settlement clearance refused {clearance} of {candidates} candidates '
                f'and regional spacing refused {spacing}')
  elif sources:
      worst = max( (refusals.get(k, 0), -i, k) for i, k in enumerate(COLLEGE_LIMITS) )
      reason = (f'{sources} {SOURCE_KIND} were read and none produced a candidate; '
                f'{worst[2]} refused the most, at {worst[0]}')
  else:
      reason = f'no {SOURCE_KIND} in this world, so nothing rolled'
  return [ _row(placed, wanted, candidates, sources, reason) ]
```
The `worst` tie-break is *"the most refusals, then the earliest limit tested"* -- `-i`
inside a `max` makes the smaller index win a tie, so `density` beats `hazard` at equal
counts. The third tuple element `k` is never reached, because `-i` is already unique.

Docstring on the counting contract: *"`candidates` counts every cell that cleared all
eight limits, including cells the budget never reached, so `placed == candidates` means
the world ran out of ground and `placed < candidates` means the budget or the spacing
stopped it -- which are different facts and used to be the same zero."*

**Measured real output**, seed 42, recipe 3, size 17, phase 12, 13 cities, support_reach
17938.89 m, college_count 2, spacing 4774.6482927568595 m:
```
placed 0, wanted 2, candidates 0, sources 38,
reason "38 cells within a city's support reach were read and none produced a candidate;
        density refused the most, at 32"
```
That is the default world's honest answer: on a 17-grid 200 km world the magic field is
too diffuse for any reachable cell to clear `magic_density >= 0.35`.

════════════════════════════════════════════════════════════════════
PART J -- `add_magic` : THE DEAD WRITER. DO NOT PORT.
════════════════════════════════════════════════════════════════════
`terrain_magic.add_magic(result, cfg)` writes `result['magic']` with **`version: 1`** and
a completely different body: a single undifferentiated leyline network of `cfg.ley_nodes`
nodes with `cfg.ley_width` Gaussian width, spanning tree plus each node's second-nearest
link, per-edge `strength` / `instability` / `growth_affinity` (a coin-flip between 0.1 and
0.9), and only three layers (`magic_density`, `magic_hazard`, `magic_growth`). It has no
`school_order`, no `networks`, no twelve-school contract.

The warning comment, which is the most valuable thing in the file:
*"Dead writer. Imported by terrain_leyline_history and called from nowhere: no call site,
no string dispatch, no non-Python caller. `generate_networks` is the sole writer of
`result['magic']` and emits version 4. Do not connect this. It does not merge, it
REPLACES the whole block with a version-1 body that has no school_order and no
twelve-school contract, so whichever of the two ran second would silently discard the
other's block entirely. Delete it or leave it; wiring it back in is the one move that
breaks worlds, and it is the move a tidiness pass makes because an imported-unused symbol
reads like an oversight."*

`generate_networks` deliberately imports `pending_college_diagnostics` on a **statement of
its own**, not appended to the module-level `from .terrain_magic import add_magic,
arc_frame, distance_to_frame`, precisely so nothing gives a later reader a reason to
reopen that line.

`docs/conformance/version-bindings.json` records how the unreachability was established
three independent ways: an AST walk matching `func.id` and `func.attr` across every module
under `Sim/` finds no call site; the string `'add_magic'` appears as a constant nowhere,
so no `getattr` dispatch reaches it; and no `.html`, `.js`, `.json` or `.md` file names it
apart from that note, so the browser lab does not drive it either. That is *"no plausible
route rather than no possible route"*.

For the port: implement **only** `generate_networks`/`evaluate_networks`. Do not port
`add_magic`. The config fields it alone consumes -- `ley_nodes` (3..24, default 10),
`ley_width` (10..2000 m, default 180, derived on recipe 3 as `design_radius *
RECIPE_WORLD_SCALE * 0.1`) and `magic_instability` (0..1, recipe 3 draws
`uniform(0.5, 0.85)`) -- are **dead on the live path**; `terrain_world` already lists all
three under `inactive`.

## Constants

## `terrain_leyline_history.py`

| Name | Value | Meaning / effect of changing it |
|---|---|---|
| `REFERENCE_RADIUS_M` | `1774.4123532462844` | Physical radius of the reference world (design radius 10000 at the recipe world_scale). Circumference `2*pi*R = 11149.something m`, the "11.15 km world" every authored reach is tuned against. **Duplicated verbatim in `terrain_scale.py`** -- they must stay equal. Raise it and every school's Gaussian narrows proportionally; the fields thin out and `magic_density >= 0.35` stops being clearable. |
| `KNOWN_SCHOOLS` | 8 entries | `weave`(Raw magic, "chaos, creation, creativity", RGB [166,104,214]); `umbral`(Raw magic, "necrotic, entropy, death, order", [72,49,35]); `infernal`(Holy / unholy, "demons, hellscapes, corruption, evil", [154,43,49]); `radiant`(Holy / unholy, "holy, healing, order, peace, righteousness", [225,209,133]); `fire`(Primordial, "fire, heat, renewal, chaos", [207,86,37]); `water`(Primordial, "water, ice, currents, order", [65,156,202]); `earth`(Primordial, "nature, ground, stone, roots, order", [67,120,51]); `air`(Primordial, "wind, storms, chaos, force", [176,210,213]). |
| `HIDDEN_SCHOOLS` | 4 entries | `blood`(Outside, "sacrifice, lineage, vitality, hunger", [140,26,38]); `void`(Outside, "absence, entropy, unmaking, silence", [26,22,40]); `rot`(Outside, "disease, undeath, decay, rebirth", [124,130,62]); `eldritch`(Outside, "madness, wrong geometry, dominion of minds", [62,96,92]). |
| `SCHOOLS` | `{**KNOWN, **HIDDEN}` | **Order is a permanent contract.** `dominant_magic` stores an index into it and `biome_variant` an index into a catalogue built in the same order, so the known eight keep positions 0-7 forever and hidden schools take 8-11. *"Appended, never inserted... every world ever generated keeps its meaning."* A new school goes at the end or every saved world's `biome_variant` and `dominant_magic` change meaning. |
| `BLOOD_FEED_GAIN` | `2.5` | Multiplier on the saturating population field. Feed = `1 + 2.5*field`, so blood in the densest city is 3.5x itself. |
| `ELDRITCH_FEED_GAIN` | `2.5` | Same, over aberrant/undead nests and ruins. |
| `ROT_REACH_WIDTHS` | `6.0` | How far rot creeps, in units of its own `width_m`. Sets the step count. |
| `ROT_DECAY_WIDTHS` | `2.0` | e-folding length of the creep, likewise in `width_m`. Larger = rot carries further per step. |
| `ROT_MAX_STEPS` | `64` | Iteration ceiling. Binds on fine grids: at size 513 with `width_m` ~2 km the derived step count exceeds 64 and rot's physical reach silently stops scaling. |
| `VOID_BITE` | `0.8` | Share of void's potency subtracted from every other school at that cell. At 1.0 void would erase equal-strength neighbours entirely. |
| separation cutoff | `0.9999` | `abs(dot) < 0.9999`, i.e. ~0.81 degrees minimum angular separation from any existing node **or its antipode**. Loosen it and `arc_frame` starts producing near-singular tangents. |
| placement attempts | `1000` | Per node. Exhausting it raises `ValueError('Could not separate ley points')`. |
| path samples | `33` (`k` in `0..32`) | Points per leyline polyline; `path[0] == from`, `path[32] == to`. Presentation only -- nothing in the field evaluator reads `path`. |
| intensity roll | `uniform(0.35, 1.65)` | Both node and edge intensities at generation. Mean 1.0. |
| alignment parameter | `uniform(0.3, 0.7)` | Fraction along the 0->1 arc where node 2 is placed. |
| cluster count | `randint(1, min(4, count))` | Inclusive; at the default 8 nodes that is 1..4. |
| `spread` | `uniform(0.12, 0.8)` | Gaussian sigma in R^3 before renormalising. At 0.8 a "cluster" covers most of a hemisphere. |
| `outlier` | `uniform(0.05, 0.4)` | Probability a node ignores its cluster and lands uniformly. |
| `link_chance` | `uniform(0.08, 0.45)` | Per-node probability of one extra random link. |
| dominance threshold | `0.35` | `dominant_school` default; also published (unread) as `magic.mutation_threshold`. |
| dominance margin | `0.08` | `dominant_school` default; also published (unread) as `magic.dominance_margin`. *"At equal strength a corrupted cell cannot clear 0.08 margin over a strong radiant or earth field"* -- which is why hidden schools default to strength 1.15 against the known eight's 0.7. |
| hazard weights | `0.3`, `0.2` | `min(1, 0.3*sum(instability) + 0.2*conflict)`. |
| growth weights | `1.0` earth, `0.6` radiant, `0.5` weave, `0.4` water | Divided by `max(total, 1e-12)`. Only four schools count as fertile. |
| growth denominator floor | `1e-12` | Prevents 0/0 on an empty cell; yields growth 0 there. |
| `magic.version` | `4` | Bound in `docs/conformance/version-bindings.json`, id `"magic"`. |
| `distribution.version` | `2`, model `'landscape_alignments'` | |
| edge intensity clamp | `0..4` | Enforced by `edit_network` and by `validate_age_world`. |
| unit-vector tolerance | `1e-6` | `abs(|p|^2 - 1) > 1e-6` rejects. |

## `terrain_magic.py`

| Name | Value | Meaning |
|---|---|---|
| `COLLEGE_LIMITS` | `('density','hazard','water','slope','temperature','freshwater','flood','suitability')` | The eight limits **in the order they are tested**. Declared once *"so the diagnostic names them out of the gate's own list rather than out of a second copy of it."* Reordering changes both the short-circuit answer and the `worst` tie-break. |
| `SOURCE_KIND` | `"cells within a city's support reach"` | *"Cells a city cannot reach are never looked at, so a report that counted the whole grid would describe work that never happened."* Published verbatim in every diagnostic row. |
| density floor | `0.35` | Bare literal in `college_refusal`. The binding limit on the default world (refused 32 of 38 reachable cells at seed 42 size 17). |
| settlement clearance | `150` m | Hard-coded, **not** scaled by world size. |
| non-recipe spacing | `150` m | `spacing = 150` when `cfg.world_recipe != 3`. |
| `college_spacing_m(radius)` | `max(1500., radius * 0.15)` | Recipe-3 college separation. 4774.648 m on the 200 km default. The 1500 m floor binds below radius 10000 m. |
| score weight | `0.25` | `-(density*(1-hazard) + 0.25*suitability)` -- the sweep's sort key. |
| arc-frame denominator floor | `1e-12` | `max(1e-12, sin(angle))` in `arc_frame`. |
| `add_magic` (dead) | node separation `-0.995 < dot < 0.97`, 10000 attempts, strength `uniform(0.65,1.25)`, instability `uniform(0.15,1)`, `growth_affinity = choice((0.1, 0.9))`, `total > 1e-15` guard | Recorded for completeness only. Do not port. |

## Option registry (`terrain_world.OPTIONS`), per school, all twelve

```
<school>_occurrence : default 1.0   range 0..1     (hidden: default 0.0, min 0, MAX 0 -- locked)
<school>_nodes      : default 8     range 3..24    integer, units 'nodes'
<school>_width      : default 110.  range 10..2000 metres, PRE-SCALING ('Gaussian influence reach')
<school>_strength   : default 0.7   range 0..2     (0.45 for umbral & infernal; 1.15 for all hidden)
<school>_instability: default 0.2   range 0..1     (0.65 for infernal; 0.8 for all hidden)
<school>_variation  : default 0     range 0..4294967295, units 'seed'
```
Comment on the hidden lock: *"The hidden schools carry the same six options, because
`generate_networks` reads them for every school in the taxonomy, but their occurrence is
locked at zero: `min == max == 0` makes `validate_options` reject any nonzero override, so
no request can raise one at generation."* And on their strength: *"A hidden school
saturates above the known eight on purpose. At equal strength a corrupted cell cannot
clear `dominant_school`'s 0.08 margin over a strong radiant or earth field, so the ground
would never mutate and corruption would be invisible."*

## AGE_YEARS and the ley composition exponent (the trap this assignment names)

Not in either module -- in `terrain_time_schedule.py` and `terrain_time.py` -- but it
owns the ley layer, so a porter of this subsystem must carry it.

```
AGE_YEARS     = 5000.                      # terrain_time_schedule.py
DAYS_PER_YEAR = 360.                       # 30-day months, 12-month years
AGE_DAYS      = AGE_YEARS * DAYS_PER_YEAR  # 1_800_000

LEY_AGE_LOW, LEY_AGE_HIGH = 0.65, 1.35     # terrain_time.py
LEY_YEAR_LOW  = LEY_AGE_LOW  ** (1. / AGE_YEARS)   # ~= 0.99991385
LEY_YEAR_HIGH = LEY_AGE_HIGH ** (1. / AGE_YEARS)   # ~= 1.00006002
```

The **age transition** drift, in `terrain_history.age_transition`:
```
for name, net in result['magic']['networks'].items():
    rng = Random(child_seed(cfg.seed, 'age-ley-' + name, age))
    for item in net['nodes'] + net['edges']:
        item['intensity'] = min(4., item['intensity'] * rng.uniform(0.65, 1.35))
```
The **yearly** drift, `terrain_time._ley_drift(result, cfg, index)`, where `index` is the
**absolute year index from the epoch**, never a tick ordinal:
```
if not cfg.magic_enabled: return False
for name, net in networks.items():
    rng = Random(child_seed(cfg.seed, 'time-ley-' + name, index))
    for item in net['nodes'] + net['edges']:
        item['intensity'] = min(4., item['intensity'] * rng.uniform(LEY_YEAR_LOW, LEY_YEAR_HIGH))
        moved = True
return moved
```
Both walk `nodes + edges` concatenated, in that order, drawing one uniform per item from
one stream. Both clamp at 4.0 on the way up and have **no floor** on the way down.

The comment, which must survive the port verbatim in substance:
*"**The exponent is derived from `AGE_YEARS` and must never be written as a literal.** The
two numbers are one contract: the pair that composes correctly over a century composes to
`0.65 ** 50` -- about 2e-10 -- over the five thousand years an age now lasts, which is the
magic layer draining to zero through ordinary ticking, silently, with every individual
step inside its stated bounds."*

Concretely: if someone hard-codes the per-year band as the century-tuned pair (0.65 and
1.35 raised to 1/100), then five thousand years of legal, in-bounds year steps multiply
every intensity by roughly 2e-10. Every `ley_*` layer collapses to zero, `magic_density`
goes to zero, `dominant_magic` goes to -1 everywhere, every magical biome variant
disappears, no cell can clear the college density floor, and **nothing anywhere reports an
error** -- each individual step was inside its stated bounds. `LeyDriftCompositionTests`
in `Sim/tests/test_time_advance.py` is the guard: it asserts the derivation directly, then
runs `_ley_drift` for a whole age of year steps and checks both that every individual step
is inside the per-year band and that the product of all of them lands inside the age band.

`AGE_YEARS` is load-bearing in a second place: `band(days)` returns `AGE` at `AGE_DAYS`,
so what counts as a tick rather than an age advance moves with it. A century is a `LONG`
tick -- one fiftieth of an age -- and moves the living layer only.

## Data files

## `Sim/icarus_sim/civilizations.json`

The only data file this subsystem reads, and only indirectly, through
`civilization_registry.population_profiles()` -> `terrain_profiles.get_profile()`.

Top-level keys: `schema`, `schema_version`, `revision`, `description`, `defaults`,
`city_classification`, `configurations`, `building_catalogue`, `civilization_order`,
`parent_races`, `founding_rules`, plus `entities` and the building libraries.

`population_profiles()` returns
`{**{key: entity['population'] for key, entity in doc['entities'].items()},
  **doc['configurations']}` -- so entity-derived profiles first, then configurations, and
a configuration with an entity's key would shadow it. Thirteen profiles today:
`human_maritime, human_desert, human_cold, human_large_island, human_rainforest,
human_heartland, dwarf, elf, gnome, tidekin, hill_dwarf, frosthold_dwarf` (entities) and
`mixed` (a configuration).

`default_profile_id()` is `doc['defaults']['profile_id']` == `'human_heartland'`, used as
the fallback profile inside `college_refusal` when no profile is passed.

`civilization_ids()` filters to `value['civilization']['kind'] == 'entity'` -- **twelve**
ids, excluding `mixed`. That is the list `evaluate_networks` writes `magic_risk_<id>`
layers for, so there are twelve risk layers and thirteen profiles.

### Fields this subsystem selects out of a profile

```
mutation_limit           0..1   0.45 for most; 0.55 gnome; 0.60 elf and tidekin
road_grade_limit      0.01..1   0.35 most; 0.40 desert/rainforest; 0.45 cold/large_island;
                                0.55 dwarf and frosthold_dwarf
college_slope_limit   0..90 deg 15 everywhere except dwarf and frosthold_dwarf at 22
college_temperature_min/max  C  most (-5, 38); human_cold (-19, 24); human_desert (5, 48);
                                human_maritime (-3, 40); human_large_island (-5, 38);
                                human_rainforest (2, 45); dwarf/frosthold_dwarf (-15, 38)
college_water_reach       m>0   600 most; 650 rainforest; 700 maritime/cold/large_island;
                                850 desert
college_flood_limit      0..1   0.5 for all thirteen
college_suitability_min  0..1   0.55 for all thirteen
```
Representative entry (`human_heartland`, the default): `college_flood_limit 0.5,
college_slope_limit 15, college_suitability_min 0.55, college_temperature_max 38,
college_temperature_min -5, college_water_reach 600, mutation_limit 0.45,
road_grade_limit 0.35`.

`validate_profile` enforces `college_water_reach > 0`, `college_flood_limit` and
`college_suitability_min` in `0..1`, `0 < college_slope_limit < 90`, and
`college_temperature_min < college_temperature_max` (raising `'Invalid college climate
range'` otherwise). A profile must carry **exactly** the `FIELDS` set plus
`civilization`, so adding a field is a schema change across all thirteen entries.

**Registry identity gate:** `registry_identity()` is the canonical JSON SHA-256 of the
whole document. Bytes changed in `civilizations.json` invalidate every saved world. A new
college limit must be added to all thirteen profiles and the revision bumped.

### Not read by this subsystem

`terrain_biome_catalogue.biome_catalogue()` is built from `KNOWN_SCHOOLS` and
`HIDDEN_SCHOOLS` -- every natural core crossed with every school, **known block first,
then hidden** -- and the flat index into that list is what `biome_variant` stores. That
catalogue is a consumer of this module's taxonomy, not an input to it, but it is the
reason `SCHOOLS` order can never change: *"A position in this list is a contract... another
column would renumber every variant in every saved world."*

## Random streams

## Streams used

Seed derivation is `child_seed(master, domain, variation=0)` =
`int.from_bytes(sha256(f'tectonics-v1:{master}:{domain}:{variation}').digest()[:4], 'big')`
-- a uint32. Python's `random.Random(seed)` is Mersenne Twister.

**Chain, per school, in `generate_networks`:**
```
cfg.seed (uint32 world seed)
 -> school_seed  = child_seed(cfg.seed, 'history-ley-' + name, options[name + '_variation'])
     -> occurrence: Random(school_seed)                          -- exactly 1 random() call
     -> geometry:   Random(child_seed(school_seed, 'clustered-ley-v1'))
     -> intensity:  Random(child_seed(school_seed, 'intensities'))
```
That is a **second level of `child_seed` applied to an already-derived 32-bit seed** --
worth flagging because it means the geometry stream depends on the school seed only
through a 32-bit hash, and two schools whose seeds collide would produce identical
geometry. Measured seeds at world seed 42: weave 536283527, umbral 2267726960, infernal
3637348704, radiant 3053655540, fire 1146141800, water 2981861140, earth 3821365744, air
1475461844, blood 883614497, void 2101617240, rot 3373346140, eldritch 2824617983.

**Draw order and count in the geometry stream** (`network_geometry`), exact:
1. `randint(1, min(4, count))` -- 1 call (`_randbelow`, variable words)
2. `clusters` x `unit()` -- 2 `uniform` each
3. `uniform(.12,.8)` -- spread
4. `uniform(.05,.4)` -- outlier
5. per node: 1 `randrange(clusters)`, then per attempt 1 `random()` plus **either** 2
   `uniform` (outlier path) **or** 3 `gauss` (cluster path). **Data-dependent**: a
   rejected attempt costs another full round.
6. if `count >= 3`: 1 `uniform(.3,.7)`
7. MST: no draws
8. 1 `uniform(.08,.45)` for `link_chance`, then per node 1 `random()` plus, only if it
   passes, 1 `choice()` on a list of `count-1` entries. **Data-dependent.**

**Intensity stream**: exactly `len(nodes) + len(edges)` `uniform(0.35, 1.65)` calls, nodes
first in index order, then edges in index order.

**CPython `gauss` caches a spare deviate** (`gauss_next`), so three `gauss` calls consume
either 2 or 4 underlying `random()` calls depending on the parity of the call history.
`fwg::Rng::gauss` deliberately discards the spare, so the draw counts will differ -- which
is fine for a clean rewrite, but means the reference's exact placements are not
reproducible and must not be used as a test oracle.

**`evaluate_networks` draws nothing at all.** It is a pure function of the network
intensities, the layers it reads, and the grid. This is what makes it safe to call five
times per age transition.

**`add_colleges` draws nothing at all.** Every decision is a sort or a threshold.

**Outside these modules but on the same block:**
- age drift: `Random(child_seed(cfg.seed, 'age-ley-' + name, age))`, one `uniform(.65,1.35)`
  per node and per edge, nodes concatenated before edges.
- yearly drift: `Random(child_seed(cfg.seed, 'time-ley-' + name, absolute_year_index))`,
  same shape with the derived per-year band. **Keyed on simulated time, never on call
  count** -- that is the whole determinism argument for time advance.
- corruption cluster placement uses its own stream and its own `RING_SPACINGS`/
  `RING_POINTS`/`SEAT_INTENSITY`/`RING_INTENSITY` constants.

## Ordering hazards

Every place iteration order, sort stability or container ordering reaches the output.

1. **`SCHOOLS` insertion order (known 8, then hidden 4) is a permanent output contract.**
   `magic.school_order` publishes it; `dominant_magic` stores an index into it;
   `biome_variant` stores an index into a catalogue built in the same order. A C++ port
   must use an insertion-ordered container (a `std::vector<std::pair<...>>` plus a name
   index), never `std::map`, whose alphabetical order would be
   `air, blood, earth, eldritch, fire, infernal, radiant, rot, umbral, void, water, weave`
   and would silently renumber every saved world.

2. **`magic['networks']` iteration order** decides the offsets in the flattened
   `magic.nodes` / `magic.edges` projections (D10) and the `names` list handed to
   `void_bite`. It is the same insertion order at generation, and JSON object order after
   a round trip.

3. **`dominant_school` ties break on the lexicographically smallest school NAME**, not on
   `SCHOOLS` order. `heapq.nsmallest(2, ((-v, k) for ...))` compares the whole tuple. Two
   schools at identical potency give `air` over `earth`, `blood` over `void`. This is a
   different order from every other ordering in the file, and it is easy to get wrong.

4. **`_saturating_field` source order.** Eldritch pools nests first, then ruins, into one
   list and sums in that order. Float addition is not associative; reordering moves the
   last ulp. Nest order is `beast_nests.sites` order; ruin order is `result['ruins']` order.

5. **`sorted(pairs)` in `network_geometry` A10** fixes edge order and therefore edge ids
   `"<school>-line-<i>"` and the intensity draw order. `pairs` is a Python `set` of sorted
   index tuples -- **unsorted set iteration would produce different ids for the same
   geometry.**

6. **`sorted(joined)` in the MST (A8)** is what makes Prim's tie-break deterministic. A
   raw set iteration over `joined` changes which of two equal-cost edges is chosen.

7. **MST tie-break tuple `(1 - dot, a, b)`**: lowest cost, then lowest `a`, then lowest
   `b`. `members` is built in ascending node index, so the `b` loop is ordered too.

8. **`dict.fromkeys(v[0] for v in SCHOOLS.values())`** produces `magic.groups` --
   insertion-ordered deduplication, giving `['Raw magic','Holy / unholy','Primordial',
   'Outside']`. A `std::set` would give alphabetical and change the block.

9. **`points` order from `sphere_grid`**: `[(0,0)] + [(x,z) for z in 1..n-2 for x in
   0..n-2] + [(0,n-1)]` -- north pole, then row-major over the interior, then south pole.
   This is the index space for every per-node vector in both modules, and it is the
   iteration order of the composite sweep (D6) and of the `vals()` helper in
   `add_colleges`. **Node index correlates with latitude**: the first nodes are the north
   pole and the rows nearest it, so any test that samples `range(N)[:k]` samples the pole.

10. **`sphere_grid` neighbour lists are `sorted(ids)`**, so `rot_spread`'s inner `max` runs
    over neighbours in ascending node index. `max` returns the first maximal element, but
    since it compares floats and takes a value rather than an identity, only ties matter,
    and ties are exact-equal values -- harmless here.

11. **The college sweep's sort key** `(-(density*(1-hazard) + 0.25*suitability), i)`
    explicitly includes `i`, so the sort is fully ordered and does not depend on
    `sorted()`'s stability. Do not drop the `i`.

12. **`allocate_access` tie-break** `candidate == distance[j] and label < owner[j]` --
    lowest integer city id wins an exact tie. The recipe-3 replacement in G2 uses a strict
    `d < distance[i]` inside a loop over `cities` in list order, which is equivalent only
    because city ids ascend with list order. If cities are ever reordered or their ids
    stop being their indices, the two rules diverge.

13. **`occupied` and `college_points` grow inside the sweep**, so the clearance and
    spacing tests see every college seated earlier in the same sweep. The order of
    `occupied` at entry is `cities + hamlets + fortresses`, which affects nothing but the
    short-circuit of `any()`.

14. **`refusals` dict insertion order is irrelevant** -- `college_diagnostics` iterates
    `COLLEGE_LIMITS` with `enumerate`, so the `worst` tie-break is on the declared limit
    order, not on which limit was hit first.

15. **`node_grid(values, points, n)` scatter order**: writes `out[z][x] = v` in `points`
    order, then overwrites row 0 entirely with `values[0]`, row `n-1` with `values[-1]`,
    then sets every row's last column equal to its first. The pole and seam closure
    happens **after** the scatter, so a pole write during the scatter is discarded.

16. **`nodes + edges` concatenation in both drift passes** fixes the draw order: all nodes
    first, then all edges, one uniform each.

17. **`civilization_ids()` order** decides nothing numerically (all twelve risk layers are
    identical), but it decides key insertion order in `result['layers']`, which is world
    output bytes if the layers dict is serialised in insertion order.

18. **`rot_spread`'s `carried == power` early exit** is an exact elementwise float
    comparison over the whole vector. In C++ use `std::equal` on the doubles, not a
    tolerance; a tolerance would terminate earlier and change the field.

## Edge cases

**Negative zero is load-bearing.** `-math.expm1(-0)` evaluates to `-0.0` (because `-0` is
the integer 0, `expm1(0.0)` is `+0.0`, and unary minus makes it `-0.0`). An unmanifested
network therefore fills its whole `ley_*` layer with `-0.0`, and `min(1., -0.0 *
instability)` keeps the sign in `instability_*`. Two guards exist specifically to preserve
it: `void_bite`'s `if not bite: continue` (because `max(0., -0.0 - 0.0)` returns `+0.0`),
and `terrain_nomad_effects._clamp_intensity`, which uses `value - 0.0` rather than
`max(0., value)` for the same reason. `test_void_absent_does_not_flip_a_negative_zero`
asserts that a magic-disabled world's `ley_weave` contains at least one value with a
negative sign bit. **In C++ this survives naturally if you write `-std::expm1(-total)` and
never `1.0 - std::exp(-total)`** -- the latter gives `+0.0`. If you decide the sign is not
worth preserving in a clean rewrite, say so explicitly, because a reader comparing fields
will otherwise think a school manifested.

**`ley_holy` is an alias, not a copy.** `l['ley_holy'] = l['ley_radiant']` binds the same
list object. Any consumer that mutates one mutates the other. In C++ a `Grid` copy is a
deep copy, so the alias silently becomes a copy -- which is **safer** but no longer
identical if anything ever writes through it. Nothing does today.

**`magic_risk_<people>` are twelve independent copies of one field.** `node_grid(hazard,
...)` is called twelve times, so they are distinct objects with identical contents. Do not
collapse them into one shared grid: consumers index them by name and the intent is that
they diverge later.

**No nodes, no edges, no feed** -> the constant `-0.0` fast path. The `feed is None` term
in that guard is unreachable in practice: `hidden_feeds` only populates a feed when the
school already has nodes, so a nodeless school never has one. Harmless, but do not treat
it as a live branch.

**No sources for a hidden feed.** `largest == 0` (every city has zero or null
`population_estimate`) gives `sources = []`, and `_saturating_field` over an empty source
list returns `1 - exp(-0) = -0.0` at every point, so `feed = 1 + 2.5*(-0.0) = 1.0`. The
multiplier is a no-op, not a zero. Same for eldritch with no nests and no ruins. This is
the stage-9 case and it is correct.

**`population_estimate` may be `None`.** Every read is `(c.get('population_estimate') or
0.)`, which also maps `0` to `0.`. A city with a null estimate contributes weight 0.

**Degenerate and antipodal arcs.** `arc_frame(a, a)` yields `angle = 0` and `tangent =
(0,0,0)` because of the `max(1e-12, sin(angle))` floor; `distance_to_frame` then correctly
returns the angle from `a` (verified: `distance_to_frame(ŷ, arc_frame(x̂, x̂)) == pi/2`).
An antipodal pair yields `angle = pi`, `sin = 0`, and a tangent of magnitude ~1e12 --
finite but meaningless. Node placement's `abs(dot) < 0.9999` test excludes both cases at
generation, and `validate_age_world` rejects `from == to`, but **a player edit or a
corruption cluster can seat two nodes arbitrarily close to each other or to an antipode**,
and no edge is created between them, so no degenerate frame results. Guard anyway.

**`acos` domain.** Every `acos` in both modules is wrapped in `max(-1, min(1, ...))`
because a dot product of two unit vectors can exceed 1 by an ulp. Keep every clamp.

**`hypot(pa, pt)` in `distance_to_frame`** can exceed 1 by an ulp for the same reason,
hence the clamp on `best`.

**Empty `potencies`** -> `dominant_school` returns `None`. **One entry** -> `runner = 0`,
so a single school with potency >= 0.35 always wins. Neither happens in
`evaluate_networks` (always twelve) but both happen in hand-built test worlds and in
`terrain_wars._key_point_school`, which builds the dict defensively with `0.` for any
missing `ley_*` layer.

**`rot_spread` with no graph edges** (`arcs` empty) or `decay_m == 0` (`width_m == 0`, not
reachable through the option registry whose minimum is 10 m, but reachable through a
hand-built world) returns `power` unchanged.

**`rot_spread` step count** is `min(64, max(1, round(6*width_m / mean_arc)))`. Python's
`round` is **banker's rounding** (half to even); C++ `std::round` is half-away-from-zero.
On the exact half this differs by one iteration. Functionally irrelevant, structurally
worth knowing.

**`ROT_MAX_STEPS = 64` binds on fine grids.** At size 513 on a 200 km world, `mean_arc`
shrinks with `n` while `width_m` does not, so `6*width/mean_arc` exceeds 64 and rot's
physical reach stops scaling. The test only checks 17 against 33, where the ceiling does
not bind. This is a known, unrecorded limit of the scale-invariance claim.

**`add_colleges` early return** when `magic` or `humans` is absent leaves the *pending*
diagnostic row in place, which reads "the college pass has not run at this phase". That is
truthful only if the pass genuinely has not been reached; when it bails because `humans`
is missing, the row is misleading. Worth fixing or documenting in the port.

**Zero cities** -> `allocate_access` gets an empty seed list, `owner` is all `-1`,
`sources = 0`, and the diagnostic says `'no <SOURCE_KIND> in this world, so nothing
rolled'`. Tested.

**`college_count == 0`** -> the budget check refuses every placement, but candidates are
still counted, and the reason is `'no colleges were requested'`. Tested.

**Unowned cells in the recipe path get `hazard = 1.0`**, which fails `hazard <= limit` for
every profile (max `mutation_limit` is 0.60). They are skipped by `if owner[i] < 0` before
that ever matters.

**`freshwater_distance` is negative where there is no source**, hence the two-sided
`0 <= fresh <= reach` test. A one-sided `fresh <= reach` would seat colleges in deserts.

**Poles.** `direction(x, 0, n) == (0, 1, 0)` and `direction(x, n-1, n) == (0, -1, 0)` for
every `x`; `sphere_grid` collapses each pole to one node whose area is the whole polar
ring and whose neighbours are the entire adjacent ring. A leyline node can land exactly on
a pole (the `unit()` sampler can produce `y == ±1` only at measure zero, but a player edit
can place one), and everything downstream handles it: `acos` of the dot product is well
defined, and `node_grid` writes the pole value across the whole row.

**The longitude seam.** `node_grid` sets `row[-1] = row[0]` for every row, so column `n-1`
duplicates column 0. Consumers that sum a whole grid must exclude the duplicate column
(`land_area` does, with `row[:-1]`); the test helper `total(layer)` in
`test_hidden_schools.py` does **not**, which is fine because it only tests for zero.

**Grid size floor.** `n >= 3`. At `n = 3` the interior is a single row of 2 distinct cells
plus two poles, so `sphere_grid` yields 4 nodes. Nothing here special-cases it.

## Reasoning worth preserving

Comments that record intent, a measurement, or a failure that happened. Quoted.

**On why the taxonomy is closed and ordered** (`terrain_leyline_history.py:16-18, 29-37`):
> "The world's own taxonomy. Everything this world can name, measure, chart against its moon or raise a god for is one of these eight, and the places that must stay eight forever say KNOWN_SCHOOLS rather than SCHOOLS."
> "Schools outside that taxonomy. Appended, never inserted: dominant_magic exports an index into SCHOOLS and biome_variant an index into a catalogue built in this order, so the known eight keep positions 0-7 and every world ever generated keeps its meaning."
> "Generation can never raise one. Their occurrence is locked to zero in the option registry, so generate_networks always leaves them empty, and only the corruption API puts a node in one. They are declared here, and nowhere else, because a school that does not exist in the taxonomy cannot be edited into a world by a caller who guesses its name."

**On the alignment** (`network_geometry`):
> "Three sacred loci share a straight spherical alignment; other loci remain scattered. This is a fantasy interpretation of Watkins' alignments, not a physical law."
> "Sparse trees within clusters; some schools have disconnected concentrations."

**On the width scale -- the most expensive comment in the file** (`generate_networks`):
> "Authored school widths are a reach on the reference world (design radius 10000 at the recipe scale, 1774.4 m physical). Magic has to cover the same share of the world at every circumference: left as absolute metres, a 110 m Gaussian on a 200 km world falls entirely between raster cells, and every ley field, magic density, biome variant, college and magic-gated habitat collapses to zero."

**On the deliberately separate import** (`generate_networks`):
> "Deliberately a statement of its own rather than a name added to the module-level terrain_magic import above: that line also carries add_magic, a dead second writer of this block that must stay uncalled, and nothing should give a later reader a reason to reopen it."

**On `add_magic`** (`terrain_magic.py:112-122`):
> "Dead writer. Imported by terrain_leyline_history and called from nowhere: no call site, no string dispatch, no non-Python caller. generate_networks is the sole writer of result['magic'] and emits version 4. Do not connect this. It does not merge, it REPLACES the whole block with a version-1 body that has no school_order and no twelve-school contract, so whichever of the two ran second would silently discard the other's block entirely. Delete it or leave it; wiring it back in is the one move that breaks worlds, and it is the move a tidiness pass makes because an imported-unused symbol reads like an oversight."

**On `edit_network`'s contract**:
> "Validated copy-on-write edit; callers reevaluate fields without rerolling geometry. A new key point is a local source until a future caller supplies connecting lines."

And, from `terrain_nomad_effects.py`, the number that argues against ever rebuilding an effects queue:
> "the direct write is `current['intensity'] * 1.18` - absolute, shrine count ignored - while the queued value was `DEVOTION_GAIN * shrines`. On a node at 3.0 with three shrines those are 3.54 and 0.54, and `edit_network` SETS intensity rather than adding to it, so an applier reading the queue the obvious way would have cut the node by 82% in the act of deepening it."

**On the partial selection in `dominant_school`**:
> "Only the top two of twelve are ever read, so a partial selection replaces a full sort: this runs once per cell. (-potency, name) tuples compare exactly as the old sort key did, and no float is added or reordered -- these are comparisons only."

**On the hidden feeds being one rebuild behind**:
> "Blood reads the living: many people, close together. Eldritch reads what the world already fears, its aberrant and undead nests and its ruins. Both read the world as it stood before this rebuild, because evaluate_networks runs ahead of civilization() and add_nests, so a corruption is always one rebuild behind the cities it is eating. Both read defensively: at stage nine there are no settlements, nests or ruins at all."

**On rot's unit of measure**:
> "Rot creeps cell to cell instead of falling off from a node. The creep is measured in metres, never in cells: each step carries a neighbour's value across that edge's own arc length, decaying over a length derived from the network's width. A 33-grid world and a 257-grid world therefore rot at the same physical rate, which a fixed iteration count over cells would not give."

Plus the two tests that measure it: a single key point's Gaussian is at `exp(-4)` (under 2%) at twice the width, and rot still carries a tenth of its peak out there; and the 17-grid and 33-grid reaches agree within 35% where a cell-counted spread would differ by 2x.

**On void's sign discipline**:
> "Void does not add potency, it takes it: every other school loses ground to it. The subtraction is skipped where void is absent rather than applied as a zero, because `max(0., x - 0.)` would turn the negative zero an unmanifested network writes into a positive one and move every magic-disabled world's bytes."

**On the negative-zero fast path**:
> "A school with no nodes and no lines sums to integer zero for every point, so every point gets the identical value -- and that value is NEGATIVE zero, since -math.expm1(-0) is -0.0 and the sign survives the multiply. It is written as the same expression rather than a literal so it cannot drift from the loop. The four hidden schools take this path in every generated world: their occurrence is locked at zero, so generation never raises one."

**On the hoisting, with the measurement**:
> "Per-point invariants hoisted out of a loop that runs once per cell per school: at size 513 that is 263k iterations x twelve schools. Values and their order of accumulation are untouched -- k is the same ((intensity*(a+b))/2) the inline expression built, and both sums still run over the same sequence."
> "The grids, their names and the index of each school are fixed for the whole sweep. Rebuilt per point they were twelve string concatenations, twelve dict lookups and a fresh list(SCHOOLS) whose .index() then scanned it -- per cell, 263k times at 513."

**On feed-before-saturation**:
> "Applied before saturation, so a well-fed field still tops out at strength."

**On the compatibility projections**:
> "Compatibility projections for existing ecological/nest requirements, never extra networks."

**On the college gate having one definition** (`college_refusal`):
> "This is the definition of college eligibility; `college_eligible` is this predicate read as a boolean. The diagnostic needs to say which limit refused, and a second copy of the gate written for the report is how a report starts disagreeing with the thing it reports on. The clause order is the original `and` chain's short-circuit order, so 'the limit that refused' means the same thing it always did."

And from the test that guards it:
> "A second copy of the predicate written for the report is the failure mode the hero half named: the report and the gate disagree and the report is believed."

**On what `SOURCE_KIND` exists for**:
> "What the college pass walks, in words. Cells a city cannot reach are never looked at, so a report that counted the whole grid would describe work that never happened."

**On the pending row**:
> "generate_networks writes `colleges: []` and the pass fills it later, so between the two the empty list means 'not yet', not 'none placed'. Those read alike in a finished document if only one of them says anything."

**On the two zeros the diagnostic exists to separate**:
> "`candidates` counts every cell that cleared all eight limits, including cells the budget never reached, so `placed == candidates` means the world ran out of ground and `placed < candidates` means the budget or the spacing stopped it -- which are different facts and used to be the same zero."

And the original problem statement from the test module:
> "The `magic` block published `colleges: []` against `effective_config.college_count: 2` and said nothing else, so a consumer could not tell 'this world has no ground a college can stand on' from 'colleges are not implemented' from 'colleges placed and something moved them'."

**On moving the budget check below the limits**:
> "Counted for the diagnostic, never read by placement. The budget check moved below the limits so a cell the budget never reached is still counted as a candidate: placement is unchanged by that -- the checks above it have no side effects and a `continue` past the placement body leaves colleges, occupied and college_points exactly as the old `break` did."

**On the diagnostic tie-break**:
> "Deterministic tie-break: the most refusals, then the earliest limit tested."

**On hidden-school strength** (`terrain_world.py`):
> "A hidden school saturates above the known eight on purpose. At equal strength a corrupted cell cannot clear dominant_school's 0.08 margin over a strong radiant or earth field, so the ground would never mutate and corruption would be invisible."

**On the option lock and the name coupling** (`terrain_world.py`):
> "The hidden schools carry the same six options, because generate_networks reads them for every school in the taxonomy, but their occurrence is locked at zero: min == max == 0 makes validate_options reject any nonzero override, so no request can raise one at generation. Only the corruption API puts a node in these. These names must stay in step with HIDDEN_SCHOOLS in terrain_leyline_history; a mismatch is a KeyError at generation, and test_terrain_recipes pins the two together."

**On AGE_YEARS** (`terrain_time_schedule.py` and `terrain_time.py`):
> "An age had no duration before this module. Ages were ordinal... A tick cannot route a long span into an age advance without an answer, so this is that answer, and it is new contract rather than a discovered constant: an age is five thousand years."
> "It is load-bearing in two places outside this line, and both move with it or break silently. terrain_time takes the age transition's own ley band to the power of 1 / AGE_YEARS, so a year-step composes over one whole age to the band the age boundary itself applies -- hard-code that exponent and a longer age drains the magic layer to zero through ordinary ticking with every individual step inside its stated bounds. And band returns AGE at AGE_DAYS, so what counts as a tick rather than an age advance moves with it: a century is a LONG tick, one fiftieth of an age, and moves the living layer only."
> "The exponent is derived from AGE_YEARS and must never be written as a literal. The two numbers are one contract: the pair that composes correctly over a century composes to 0.65 ** 50 -- about 2e-10 -- over the five thousand years an age now lasts, which is the magic layer draining to zero through ordinary ticking, silently, with every individual step inside its stated bounds."

And from `docs/conformance/time-advance.md`:
> "They are not a tuned pair, they must not be rounded to friendlier numbers... LeyDriftCompositionTests guards the half of the age length that is silent when it breaks."

**On what a tick may not touch** (`time-advance.md`), which defines this block's mutability:
> "NOT the ley field grids: when the base intensities move, or a player edit or a resolved hook changes a node, evaluate_networks rewrites the ley_* layers, which is the point of running it... the ley geometry -- nodes and edges are never added or removed by a tick, only their intensity drifts, and only a player edit or a resolved hook adds a node."
> "A hidden school takes no lunar surge and carries no `surged_strength` key at all. `_apply_surge` writes only schools present in the tide factors. Widening that to all twelve would add a key to four networks in every world and change the bytes."

## Notes for the C++ implementation

Read `include/fwg/grid.hpp`, `include/fwg/random.hpp`, `include/fwg/world.hpp` and
`include/fwg/vec3.hpp` before starting. Today's `World` stops at
`kHighestImplementedStage = 5`; this is stage 9 and stage 12 work, and `World` will need
a `Magic` block, so raise the ceiling and extend `World` and `serialize.cpp`.

### Direct mappings

| Python | C++ |
|---|---|
| `result['layers']['ley_weave']` | `world.layers["ley_weave"]` -- a `fwg::Grid` |
| `sphere_grid(n, radius)` | `fwg::sphere_grid(n, radius)` -> `shared_ptr<const SphereGrid>`; hold the shared_ptr, the cache evicts |
| `points` | `sg->point_x[i]`, `sg->point_z[i]` |
| `graph[i]` | `sg->neighbours[i]` -- `{index, distance}` in metres |
| `node_grid(values, points, n)` | `sg->to_grid(values)` -- already closes seam and poles |
| `direction(x, z, n)` | `fwg::direction(x, z, n)` -> `Vec3` |
| `child_seed(master, domain, variation)` | `fwg::child_seed(...)` (uint32) for a noise seed; `fwg::Rng::child(master, domain, variation)` / `child_seed64` for a stream |
| `random.Random(s).uniform(a,b)` | `Rng(s).uniform(a, b)` |
| `random.Random(s).gauss(0, sigma)` | `Rng::gauss(0, sigma)` -- **does not cache the spare deviate**; the reference does, so placements will differ. Expected and fine. |
| `rng.randint(1, k)` | `Rng::randint(1, k)` (inclusive, matching semantics) |
| `rng.randrange(k)` | `Rng::below(k)` |
| `rng.choice(list)` | `Rng::choice(vec)` |

### Seed derivation -- the one place to be careful

`random.hpp` states: *"There are exactly THREE derivations in this library... a porter who
implements three of four gets a world that is correct except where it is not. If you need
a fourth, name it here."* This subsystem needs **no fourth**. Map it as:

```cpp
// school stream seed
const std::uint64_t school_seed =
    Rng::child_seed64(cfg.seed, "history-ley-" + name, opts.variation);
// three sub-streams, all through the SAME derivation
Rng occurrence(school_seed);
Rng geometry = Rng::child(school_seed, "clustered-ley-v1");
Rng intensity = Rng::child(school_seed, "intensities");
```
The reference nests `child_seed` twice (a 32-bit hash of a 32-bit hash). `Rng::child` on a
64-bit seed is the clean-rewrite equivalent; do not reproduce the double 32-bit narrowing.

### Python idioms with no direct equivalent

- **`SCHOOLS` as an insertion-ordered dict.** There is no `std::ordered_map`. Use
  `static const std::vector<School> kSchools` (name, group, descriptors, colour) plus a
  `static const std::unordered_map<std::string_view, int> kSchoolIndex`. **Never
  `std::map`** -- alphabetical order renumbers `dominant_magic` and `biome_variant`.
  `magic.networks` must likewise be a `std::vector<Network>` indexed parallel to
  `kSchools`, not a map, so iteration order is free.
- **`-math.expm1(-x)`** -> `-std::expm1(-x)`. Writing `1.0 - std::exp(-x)` loses the
  negative zero that an unmanifested school's whole layer is made of.
- **`heapq.nsmallest(2, ...)`** -> a two-element running selection, comparing
  `std::pair<double, std::string_view>` with `operator<`. `std::partial_sort` over twelve
  elements also works. The tie-break on the school **name string** must survive.
- **`min((cost, a, b) for ...)`** in the MST -> `std::min_element` over a
  `std::tuple<double,int,int>`, or an explicit running minimum. Iterate `joined` as a
  **sorted vector**, not a `std::set` you happen to iterate (a `std::set<int>` is sorted,
  so it is fine, but say so).
- **`pairs` as a `set` of sorted tuples** -> `std::set<std::pair<int,int>>` gives you both
  the dedup and the `sorted(pairs)` iteration order for free. This is one of the few
  places `std::set` is the right answer.
- **`carried == power`** list equality -> `std::equal(a.begin(), a.end(), b.begin())` on
  doubles. Exact, no tolerance.
- **`{**e, 'id': ...}`** dict-merge -> explicit struct field copies.
- **`copy.deepcopy`** in `edit_network` -> value semantics; a `Network` struct with
  `std::vector` members copies deeply by default. `edit_network` should take the network
  by const reference and return a new one.
- **`round()`** in `rot_spread` -> Python rounds half to even, `std::round` rounds half
  away from zero. One iteration difference at the exact half. Use `std::llround` and
  document the divergence, or `std::nearbyint` with the default rounding mode for exact
  parity.
- **`math.isfinite` / `type(v) not in (int, float)`** validation in `edit_network` -> a
  typed C++ API makes the type checks unnecessary; keep the range and finiteness checks
  (`std::isfinite`, `0 <= intensity <= 4`, `|p|^2` within 1e-6 of 1) and the uniqueness
  check on node ids.
- **Exceptions as the error channel.** The Python raises `ValueError` with specific
  strings. FWGC++ has no dependencies and its own test harness -- pick one convention
  (`std::invalid_argument`, or a `Result<T>`) and keep the message text, because
  `validate_age_world` and the request layer surface those strings to API callers.

### Data structures to add

```cpp
struct LeyNode   { std::string id; Vec3 direction; double intensity; };
struct LeyEdge   { std::string id; int from; int to; double intensity;
                   std::vector<Vec3> path; };          // 33 points
struct Distribution { int version = 2; std::string model = "landscape_alignments";
                      std::vector<int> aligned_nodes; int clusters;
                      double spread, outlier_fraction, extra_link_probability; };
struct Network   { std::string name, group, descriptors; std::array<int,3> color;
                   std::uint32_t seed; double strength, width_m, instability;
                   std::vector<LeyNode> nodes; std::vector<LeyEdge> edges;
                   std::optional<Distribution> distribution;
                   std::optional<double> surged_strength; };   // known schools only
struct College   { std::string id, kind, population_profile, culture_id, reason;
                   int node, x, z, core_id;
                   double density, hazard, suitability, access_cost;
                   std::vector<int> access_nodes; };
struct CollegeDiagnostic { std::string institution, source_kind, reason;
                           int placed, wanted, candidates, sources; };
struct Magic     { int version = 4; std::vector<Network> networks;
                   std::vector<College> colleges;
                   std::vector<CollegeDiagnostic> diagnostics;
                   bool enabled; double mutation_threshold, dominance_margin;
                   std::vector<Vec3> flat_nodes;  // magic.nodes
                   std::vector<FlatEdge> flat_edges;
                   std::optional<double> college_spacing_m;
                   std::string method, college_method; };
```

### An `ArcFrame` type is worth having

```cpp
struct ArcFrame { Vec3 a, b, tangent; double angle; };
ArcFrame arc_frame(const Vec3& a, const Vec3& b);
double distance_to_frame(const Vec3& p, const ArcFrame& f);
```
Both are hot: `evaluate_networks` calls `distance_to_frame` once per cell per edge. At
size 513 with twelve schools averaging 8 edges, that is ~25 million calls. Make them
`inline`, keep the `std::clamp(x, -1.0, 1.0)` guards, and note `std::hypot` is slower than
`std::sqrt(pa*pa + pt*pt)` while being the thing the reference used -- `hypot`'s only
benefit here is overflow safety on inputs bounded by 1, so `sqrt` is a legitimate
substitution in a clean rewrite. **Say which you chose.**

### Performance

`evaluate_networks` is the hot pass of the whole generator after erosion:
`O(n^2 x schools x (nodes + edges))`. At 513 that is 263k cells x 12 schools x ~16 terms =
50M exp() calls per rebuild, and an age transition rebuilds it several times. The Python
already hoisted everything hoistable; the obvious C++ wins are (a) restructuring the loop
as school-major over a contiguous `std::vector<double>` per school, (b) precomputing the
`radius / width` reciprocal per school so the inner term is `exp(-(k*angle)^2)`, (c)
parallelising over cells -- the per-cell computation is embarrassingly parallel and has
no cross-cell dependency, unlike `rot_spread`. If you parallelise, keep the summation
order within a cell fixed.

`add_colleges` is dominated by the recipe-3 loop: one **unbounded** single-source Dijkstra
per city over the whole passable component. With 13 cities at size 17 that is trivial; at
size 513 with 60 cities it is not. The bound is legitimate to add -- `shortest_paths`
documents that *"edge costs are non-negative, so every prefix of a path costing `limit` or
less also costs `limit` or less, and a node inside the bound keeps the same distance and
the same parent it had when the search was unbounded"* -- so passing `cfg.support_reach`
as the limit is exact, not an approximation, and is what the repo's own
"bound the search, not the result" note recommends. **This is a safe optimisation that the
Python did not take here (it does take it elsewhere).**

### Things that do not map

- The `magic_risk_<people>` layers require `civilization_ids()`, which requires the
  profile registry. FWGC++ has no `civilizations.json` reader yet. Either port the
  registry first or emit a single `magic_hazard` and defer the twelve aliases, **and say
  which**, because `road_cost_function` and every settlement planner read them by name.
- `suitability_<profile>` layers come from `add_settlements`, which is not ported. The
  college pass falls back to plain `suitability` when they are absent, so a partial port
  degrades gracefully.
- `surged_strength` needs `terrain_astrology.tide`. Leave the `std::optional` unset until
  astrology lands; a hidden school never gets one anyway.
- The `warnings` / `timing_ms` bookkeeping maps onto `World::warnings` and
  `World::timing_ms` directly.

### Do not port

`terrain_magic.add_magic` and everything it alone uses (`Config::ley_nodes`,
`ley_width`, `magic_instability`). `fwg::Config` today has none of them; keep it that way.
Adding them would create exactly the second writer the conformance record spent a
paragraph proving unreachable.

## Open questions

1. **`magic.mutation_threshold` and `magic.dominance_margin` are written and never read.**
   They duplicate `dominant_school`'s defaults (0.35 / 0.08). Every call site in the repo
   calls `dominant_school(potencies)` bare, so the published values are decorative. Either
   thread them through every caller or drop them from the block -- but dropping them is a
   schema change to a version-4 block that consumers may read. **Decide and record.**

2. **All twelve `magic_risk_<people>` layers are byte-identical to `magic_hazard`.** The
   per-people distinction exists throughout the consumers (`road_cost_function` refuses an
   edge on it, settlement/hamlet/fortress/port siting all branch on it) but the producer
   never differentiates. Is per-people risk meant to differ -- by `magic_penalty`, by
   school affinity, by `mutation_limit` -- and was never implemented, or is the split
   purely a forward-compatibility hook? Twelve identical copies of a 513x513 grid is 25 MB
   of duplicate output at the top size.

3. **`ROT_MAX_STEPS = 64` silently defeats rot's scale-invariance at fine grids.** The
   step count is `6*width_m / mean_arc`; `mean_arc` shrinks like `1/n` while `width_m` is
   fixed, so above roughly size 65 on a 200 km world the ceiling binds and rot's physical
   reach stops growing. The tests only compare 17 against 33, where it does not bind. Is
   the ceiling a cost guard that should scale with `n`, or a deliberate cap?

4. **The `cfg.world_recipe` vs `cfg.world_recipe == 3` inconsistency in `add_colleges`.**
   Line 198 tests `== 3` for the spacing; lines 186, 209 and 217 test truthiness for the
   allocation, the mutation limit and the parent array. A recipe 1 or 2 would get per-city
   allocation with a 150 m spacing -- almost certainly not intended. No such recipe exists
   today, so nothing fails; a porter must pick one rule.

5. **The 150 m settlement clearance is not scaled by world size** while the college
   spacing is. On the 200 km default it is far under one cell and effectively a no-op; on
   the 11.15 km reference world it was a real exclusion radius. Should it take
   `reach_scale(circumference)` like `support_reach` and `settlement_spacing` do? The
   `radius=40.` unit test depends on the current absolute value.

6. **`add_colleges` bails with `return result` when `humans` is absent, leaving the
   pending diagnostic row** that says "the college pass has not run at this phase". That
   is false in that case -- the pass ran and refused. It should probably write a row of
   its own.

7. **Node ids are only unique within a network, not across the twelve.** Nothing enforces
   global uniqueness, and `hero_generator/wells/magic.py` builds
   `{n['id']: (school, n) for school, net in networks.items() for n in net['nodes']}` --
   a **flat dict across all twelve**, so a colliding id in two networks silently loses one.
   Generated ids (`<school>-node-<i>`) and ruin ids (`<ruin id>-key`) cannot collide, but
   a player edit or a corruption cluster could. Should `edit_network` check globally?

8. **The `feed is None` term in the negative-zero fast path is unreachable.**
   `hidden_feeds` only populates a feed when the school already has nodes. Harmless dead
   condition, or a guard against a future feed that does not require nodes?

9. **No floor on ley drift.** Both `_ley_drift` and the age transition clamp at 4.0 on the
   way up and have no lower bound, so an unlucky random walk can take an intensity
   arbitrarily close to zero and it never recovers proportionally. Over ten ages that is a
   ~0.65^10 = 1.3% tail. Intended asymmetry, or an oversight?

10. **`from dataclasses import replace` in `terrain_leyline_history.py` is unused**, and so
    is the `temporary` dict built inside `generate_networks`'s per-school loop. Both are
    dead. Confirmed by inspection (the only other occurrence of "replace" in the file is
    inside a comment). Do not port either; flagged here only so a porter does not go
    looking for what they were for.

11. **No conformance record covers these two modules.** `docs/conformance/` has no
    `leylines.md` or `magic.md`. The only binding is `version-bindings.json`'s `"magic"`
    entry, which pins the version-4 integer and documents the `add_magic` shadow, and
    `time-advance.md`, which covers the ley *drift* composition but not the field
    evaluator. Nothing in those disagrees with the code as read. **This spec is the only
    description of the field evaluator and the college pass that will survive the repo.**

12. **The env var `ICARUS_CITY_WORKERS` does not reach this subsystem.** Neither module
    forks a process pool; `add_colleges` is fully serial. It is named in the assignment's
    trap list, and the answer here is "not applicable" -- but `add_colleges` runs
    *downstream of* both pooled settlement planners, so if pooled output ever differs from
    serial output, the city set it reads differs, and therefore so does every college.

## Addenda from the audit

The reader above wrote the specification; a second reader went back to the source
to find what it had left out or got wrong. Both are kept: the omissions are
content the implementer needs, and the corrections say where the specification
above is not to be trusted.

### Corrections

- **Claimed:** Writes / G6: "result['timing_ms']['colleges'] += elapsed ; result['timing_ms']['total'] += elapsed" and "add the elapsed milliseconds to timing_ms['colleges'] and timing_ms['total']"
  **Actually:** Only total accumulates. The source is `result['timing_ms']['colleges']=elapsed; result['timing_ms']['total']+=elapsed` -- colleges is a plain ASSIGNMENT. This matters on any path that runs the pass twice (an age rebuild re-runs the civilization stages): colleges reports the last run's duration while total keeps every run's, so a += port would disagree with the reference and would double-count the college pass in the per-stage breakdown.

- **Claimed:** Edge cases: "Node placement's abs(dot) < 0.9999 test excludes both cases at generation ... a player edit or a corruption cluster can seat two nodes arbitrarily close to each other or to an antipode, and no edge is created between them, so no degenerate frame results." Part A5 repeats it ("That is deliberate: arc_frame divides by sin(angle) ...").
  **Actually:** False for node 2. The alignment in A6 replaces nodes[2] AFTER the separation loop has run and never re-tests it, so ordinary generation can produce a node pair inside the 0.9999 cutoff (as close as ~0.24 deg when nodes 0 and 1 sit at the 0.81 deg minimum), and can place node 2 arbitrarily close to any other node the arc happens to pass. Edges (0,2) and (1,2) are then built on that geometry, so a near-degenerate arc_frame is reachable without any player edit or corruption cluster. The invariant holds for the nodes as PLACED, not for the nodes as PUBLISHED.

- **Claimed:** Pipeline position: "In practice only size/globe_radius are read by generate_networks itself"
  **Actually:** generate_networks never reads cfg.size. Its reads are cfg.seed, cfg.magic_enabled, cfg.college_count (only for the pending diagnostic row), cfg.world_options via terrain_world.options(cfg), and result['effective_config']['globe_radius']. cfg.size is read by evaluate_networks, which it tail-calls. The distinction matters because generate_networks can run against a result with no layers at all, so long as result['layers'] exists as a dict for the tail call to write into.

- **Claimed:** Pipeline position: "result['magic'] must NOT exist; generate_networks writes it whole."
  **Actually:** There is no such precondition and nothing checks for one. generate_networks assigns result['magic'] = {...} unconditionally, which is a destructive replace: any existing block -- player leyline edits, corruption clusters, ruin-legacy key points, drifted intensities -- is discarded with no warning and no error. That behaviour is what to preserve (re-running stage 9 is a reroll, not a refresh), but a porter who encodes absence as an asserted precondition would break the staged-rebuild path at terrain_history.py:545, which calls it on a result whose magic block may already be present from an earlier run of the same ladder.

### Missing — important

**The exact magic['method'] prose string (promised as "see Constants", absent from both Constants tables)**

'Eight known schools as independently seeded sacred alignments with uneven clusters, scattered outliers and variable connectivity, plus four hidden schools that generation never raises: their occurrence is locked at zero and only the corruption API places a node in one. Node and line intensities are editable; overlapping raw potency is preserved. Mutation requires potency >= 0.35 and an absolute lead >= 0.08 over the next strongest school.' -- written into every version-4 block by generate_networks.

**The exact magic['college_method'] prose string (spec writes only <prose string>)**

"Colleges share the selected population safety limit; no protective ward discount. Sites may be fewer than requested, and magic.diagnostics says per institution how many were asked for, how many cells were within a city's support reach, how many of those cleared every limit, and what refused the rest. Institutions have no separate population or supply demand yet."

**The sacred alignment (A6) silently breaks the 0.9999 separation invariant, and nothing re-checks it**

After the attempt loop guarantees abs(dot(p,q)) < 0.9999 for every placed pair, A6 OVERWRITES nodes[2] with a point on the nodes[0]->nodes[1] arc at t = uniform(0.3,0.7)*angle and performs no separation test on the result. Two consequences the spec denies: (1) the minimum legal angle between nodes 0 and 1 is acos(0.9999) ~= 0.81 deg, so node 2 can end up as little as 0.3*0.81 = 0.24 deg from node 0 -- inside the cutoff placement enforced; (2) the arc can pass arbitrarily close to any OTHER node k, which the alignment never consults. Edges (0,2) and (1,2) are then created from exactly that geometry, so a near-degenerate arc_frame (angle -> 0, tangent divided by the max(1e-12, sin(angle)) floor) is reachable in ordinary generation, not only via a player edit or a corruption cluster. Keep the degenerate-frame guard; do not assume placement excluded it.

### Missing — minor

**Neither generate_networks nor evaluate_networks writes any timing_ms entry; the caller does, and the caller owns the phase/climate gate**

In terrain_area.py the stage-9 block is `if cfg.phase>=6 and result.get('climate'):` wrapping `generate_networks(result,cfg); add_environment(result,cfg); add_biome_variants(result,cfg)` and then `result['timing_ms']['world_ecology']=elapsed; result['timing_ms']['total']+=elapsed`. So one combined 'world_ecology' entry covers all three passes, there is no timing_ms['leylines'] on the live path (that key belongs only to the dead add_magic), and the gate lives in the caller -- generate_networks itself has no phase or climate test. The second live call site is terrain_history.py:545-546, `elif stage==9: generate_networks(result,cfg); refresh_environment(result,cfg); add_astrology(result,cfg)`, which records no timing at all.

**magic_density and magic_growth also carry the negative zero in an unmanifested world**

The spec pins the sign only for ley_* and instability_*. In the composite sweep total = sum(p.values()) over twelve -0.0 values is -0.0; density.append(-math.expm1(-total)) then evaluates -expm1(0.0) = -0.0, and growth = (-0.0 + 0.6*-0.0 + 0.5*-0.0 + 0.4*-0.0) / max(-0.0, 1e-12) = -0.0/1e-12 = -0.0. So on a magic-disabled world magic_density, magic_growth, every ley_*, every instability_*, ley_holy, ley_primordial and all twelve magic_risk_* are negative zero; only magic_hazard and magic_opposition (both min(1., ...) of a positive-zero sum) and dominant_magic (-1) are not.

**The flat projections in D10 alias the network geometry rather than copying it**

magic['nodes'].extend(node['direction'] for node in net['nodes']) appends the SAME list objects held by networks[school]['nodes'][i]['direction'], and magic['edges'].extend({**e, ...}) is a shallow merge, so each flat edge shares its 33-point 'path' list with the network edge. Mutating a direction or a path through one view mutates the other, and a JSON round trip breaks the aliasing. In C++ (value semantics) these become copies -- safer, but a divergence worth stating, and it means the flat projections must be rebuilt, never patched, after any edit.

**magic['college_spacing_m'] is an int on non-recipe worlds, and the unowned-cell suitability fallback is an int**

spacing = college_spacing_m(r) if cfg.world_recipe==3 else 150 -- the literal 150 is a Python int, so magic.college_spacing_m serialises as 150, not 150.0, on every world whose recipe is not 3. Likewise G3's fallback is `... if o>=0 else 0` (int zero, not 0.0). A typed C++ double field changes both spellings in the emitted JSON.

**How road_cost_function identifies the two corner cells of a diagonal step**

The diagonal predicate is `if xi!=xj and zi!=zj and zi not in (0,n-1) and zj not in (0,n-1):`, and the two corner cells are lookup[(xi,zj)] and lookup[(xj,zi)], where lookup is terrain_settlements.sphere_index(points)[0] -- a {(x,z): i} map built once per point-list identity, with x taken modulo n-1. Both a water test and, when hazard is supplied, a `max(hazard[c1],hazard[c2]) > cfg.human_magic_limit` test are applied to that pair. The spec says only "the two corner cells it would slip between", which pins neither the wrapping nor which pair.

**G2's per-city loop rebuilds two whole per-node vectors per city on top of the unbounded Dijkstra**

own_cost=road_cost_function(points,water,vals('height'),flood,vals('rain_river'),replace(cfg,...),risk) calls vals('height') and vals('rain_river') -- each an O(n^2) list rebuild -- once per city, plus vals('magic_risk_'+p['id']) a third time when that layer exists. At 60 cities on a 513 grid that is tens of millions of redundant appends before any routing happens. Hoist all three out of the loop; the values are identical for every city. The spec's performance section names only the unbounded Dijkstra.

**Return-value convention differs across the three entry points**

generate_networks returns None (it mutates result in place and tail-calls evaluate_networks, which also returns None). add_colleges and the dead add_magic both `return result`, and add_colleges is called in expression position: `result=add_seasonal_food(add_colleges(add_humans(result,cfg),cfg),cfg)` in terrain_area.py. Both early-outs in add_colleges are `return result`, not a bare return.
