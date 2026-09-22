# Port specification: 11-nests

Extracted from the Python `FantasyWorldGenerator` at commit `b67c99b` before that
repository was retired, then audited against the same source by a second reader.

**Source modules**

- `Sim/icarus_sim/terrain_nests.py`

**Estimated C++ size:** roughly 1050 lines, excluding tests.

## Audit: NOT YET IMPLEMENTABLE

The algorithm section is, unusually, complete: I walked §0 through §7 line by line against the source and did not find a single point where I would have to guess a constant, a tie-break, a loop bound, an initialisation or an ordering. Every comparison operator that matters is reproduced correctly (`clear_of < clear_needed`, `score < floor`, `room <= 0`, `f.get(k,0) < v`, `draw <= cursor`, `distance < max(reach, a.range_m)`), the `(tier, wet)` budget keys are right, and `danger_ramp`, `tier_density`, `nearest_settled`'s latitude walk with the ADDED slack, `_blocked`'s two-phase same-cell-then-row-band scan with `int(max(reach,widest)/pitch)+1` taken over the bucket's widest, the `floor + one Bernoulli` discretisation, the refusal-only `settled_here` memo, the post-hoc `(-tier, id)` cap and its failure to rewrite `species_cells`, `cleared_out`'s restricted scan, and the five `REACH_REASONS` with their exact strings are all faithful. I independently verified every data-file statistic (680 profiles, 311/369 split, tier counts 253/151/141/85/50, medium/kind/size/family/movement histograms, the weights and requires key frequencies and exact threshold sets, spacing 100..3500, occurrence 0.12..0.70, temperature bounds -35..14 and 12..45, the single monster biome key set {0,2,3,4,5,7,8,13,15,16,17}, 25 never-satisfiable and all of them monsters, 21 roles, the 13-entry natural biome roster with 1 and 6 tombstoned), every option default and range, `child_seed`'s exact hash string, `reach_scale`'s reference circumference, slope being in degrees, node count 2+(n-2)(n-1), and all three pipeline call sites including both `evaluated_age` writes. Reasoning preservation is excellent — nearly every measurement comment survives verbatim. I set implementable=false on one thing only: the spec describes `terrain_nest_profiles.json` but does not contain it and never instructs anyone to rescue it. With the Python gone, 678 of 680 profile records and 20 of 21 role biome tables go with it, leaving a perfectly specified point process and no creatures to place. That is a data-preservation gap rather than an algorithm gap, but under the stated premise it is decisive. Three spec claims are also affirmatively wrong (the `reachability` triple, the scope of the retracted same-cell claim, and what `export_catalogues.py` exports), and the third compounds the first problem by misidentifying the second surviving copy of the catalogue.

**1 blocking gap(s)** were found and are recorded in the addenda below.

## Purpose

Two independent, deterministic habitat passes that populate a finished world with creatures: `add_animals` writes `result['wildlife']` (animal hunting grounds) and `add_monsters` writes `result['beast_nests']` (monster territory). Both are *thinned point processes* over the ground rather than a fixed anchor budget, so expected creature count is an integral over habitable land and doubling the land doubles the creatures, with no cap anywhere. Danger `tier` (1..5, "how dangerous to a player") does all the ecological work: density falls geometrically with it (prey many, apex few), territory radius and settlement clearance grow with it, and a continuous `danger_ramp` slides each tier's rate inward toward or outward from settled ground in proportion to danger. The two passes never read each other, so a wolf range overlaps a deer range and a dragon's territory may contain a goblin camp. Everything produced is a *static habitat proposal* — a centre, a radius, a species — not a simulated population: no migration, breeding, carrying capacity, patrol or runtime spawning.

## Where it sits in the pipeline

**Entry points.** `add_nests(result, cfg)` runs `add_animals(result, cfg)` then `add_monsters(result, cfg)`, in that order, and returns `result`. Each is independently gated: `if not cfg.world_recipe or cfg.phase < 7: return result` (unchanged, no block written — `test_phase_gate` asserts `'wildlife'` and `'beast_nests'` are absent at phase 6).

**Must already exist before it runs:**
- `result['effective_config']['globe_radius']` — the PHYSICAL radius in metres (post `apply_world_scale`), not the design radius.
- `result['layers']` — every layer, complete. Specifically: height/water stage (`water_type`, `water_depth`, `river`), climate (`temperature`, `moisture`), biome labelling (`biome`, `natural_biome`), ecology (`salinity`, `maritime`, `fishing_productivity`, `reef`, `kelp`, `lagoon`, `sheltered_bay`, `rocky_coast`, `metal_richness`, `zone_haunted_sands`), tectonics (`slope`, `volcanic`), and the ley layers (`ley_weave`, `ley_umbral`, `ley_infernal`, `ley_holy`, `ley_primordial`, `ley_air`, `ley_earth`, and the dormant `ley_void`, `ley_blood`, `ley_eldritch`, `ley_rot`). Note `ley_primordial` is itself derived (`max` of fire/water/earth/air) by `terrain_leyline_history`.
- Settled things, for clearance: `result['settlements']['sites']`, `result['fisheries']['ports']`, `result['humans']['hamlets']` — all optional; each missing block contributes nothing.

**Where it runs in the 16-stage ladder.** Stage 13 ("beasties") of `generate_history`: `add_nests(result, replace(cfg, phase=9))`, immediately followed by `add_threat_assessments(result, 'beast_nests', spacing=cfg.settlement_spacing)` and `add_religion`. On the non-history path (`terrain_area`) it runs after settlements, humans, colleges, seasonal food and `add_world_society`, inside the `world_society` timing bucket.

**Re-run at every age boundary.** `terrain_history.age_transition` calls `add_nests(..., phase=9)` at the top and sets `result['beast_nests']['evaluated_age'] = age - 1`; `terrain_history.rebuild_tail` (shared by an age transition and a divine visitation) calls it again after the civilization rebuild and sets `evaluated_age = age`. **`evaluated_age` is written by the caller, not by this module** — nothing in `terrain_nests` ever sets it. Both blocks are fully rebuilt from scratch each time; nothing is carried across.

**What depends on it afterwards:** `terrain_beast_movement` (turns a share of sites into travelling groups, controlled by `beast_movement_share`), `terrain_encounters`, `terrain_visitation` (**purges lairs from `beast_nests['sites']` in place and does not revisit `saturation`, so the two stop agreeing once a visitation has run**), `terrain_religion`, `terrain_corruption`, `terrain_leyline_history`, `terrain_history` (threat assessments), `terrain_read_select`, `terrain_time`, and per the conformance record `hero_generator`, `key_locations` and `npc_roster`. Both blocks are in `terrain_history.STATE_KEYS` so they are snapshotted per stage, and both are exported to the native catalogue table by `tools/export_catalogues.py` (a generated mirror channel — a catalogue edit is not landed until it is regenerated). `Sim/tests/test_terrain_nests.py::test_catalogue_coverage` asserts the profile name set equals `docs/catalogue/creatures.json`.

## Inputs

**Config / world fields**
- `cfg.seed` (uint32-ish master seed), `cfg.size` (n, grid edge), `cfg.phase`, `cfg.world_recipe`.
- `result['effective_config']['globe_radius']` — metres.

**World options** (all from `options(cfg)`; defaults, min, max as authored in `terrain_world.OPTIONS`):

| option | default | range | units | used for |
|---|---|---|---|---|
| `nest_density` | 1.0 | 0..3 | × | overall multiplier on both passes; 0 empties the world |
| `animal_density` | 4.5 | 0..40 | groups/km² | tier-1 animal density per km² of its own medium |
| `monster_density` | 0.53 | 0..40 | lairs/km² | tier-1 monster density per km² of its own medium |
| `nest_fantasy` | 1.0 | 0..3 | × | monster-only multiplier; 0 leaves only animals |
| `animal_tier_falloff` | 4.0 | 1.5..8 | × | how much rarer each animal tier is than the one below |
| `monster_tier_falloff` | 2.0 | 1.5..8 | × | same for monsters; gentler on purpose |
| `nest_spacing` | 1.0 | 0.25..4 | × | territory multiplier |
| `nest_min_suitability` | 0.3 | 0.05..0.95 | — | hard floor on weighted suitability |
| `nest_settlement_clearance` | 250.0 | 0..3000 | m | base hard clearance from settled things |
| `nest_per_species` | 0 | 0..64 | anchors | per-species cap; 0 = uncapped |
| `nest_limit` | 0 | 0..20000 | anchors | post-hoc per-pass cap; 0 = uncapped |
| `nest_variation` | 0 | 0..4294967295 | seed | independent nest seed variation |

**Layers**, read per cell. `habitat_cells` builds, for each grid node, a field dict `f` containing **every layer's value at that node** plus eight derived fields. So any layer name a profile references is readable, and an absent one reads as 0.

Layers consulted by this module's own code: `water_type` (int: 0 land, 1 ocean, 2 lake), `salinity` [0,1], `water_depth` (metres, ≥0), `moisture` [0,1], `maritime` [0,1], `slope` (DEGREES, 0..90), `river` (int 0/1 — the hydrological river flag from `terrain_water`, *not* `rain_river` from `terrain_climate`), `temperature` (degrees Celsius, roughly −40..45), `natural_biome` (int biome id; falls back to `biome`, then 0).

Layers consulted only through profile `weights` / `requires` keys (all approximately [0,1] unless noted): `fishing_productivity`, `reef`, `kelp`, `lagoon`, `sheltered_bay`, `rocky_coast`, `metal_richness`, `volcanic`, `zone_haunted_sands` (written by `terrain_ecology` as `'zone_'+name`), and `ley_weave`, `ley_umbral`, `ley_infernal`, `ley_holy`, `ley_primordial`, `ley_air`, `ley_earth`, `ley_void`, `ley_blood`, `ley_eldritch`.

**Settled things** (`settled_directions`): the concatenation, in this exact order, of `result['settlements']['sites']`, `result['fisheries']['ports']`, `result['humans']['hamlets']`. Each entry contributes `s.get('direction') or direction(s['x'], s['z'], n)` — a unit Vec3. Order matters only as a stable tie-break inside `nearest_settled`'s latitude sort.

**Data file**: `terrain_nest_profiles.json`, sibling of the module. See `data_files`.

## Outputs

Two top-level blocks. **Neither has a JSON schema of its own**; both are declared merely as objects in `Contracts/schemas/world-output.schema.json`.

## `result['wildlife']` (animals)
```
version            int   = WILDLIFE_VERSION = 3
sites              [Site]      sorted by `id` (LEXICOGRAPHIC string sort)
diagnostics        [Diag]      one row per animal profile, in catalogue order
saturation         [Sat]       one row per live tier, sorted ascending by tier
reachability       {authored, eligible, placed, never_satisfiable, scope}
profiles           [Profile]   the animal catalogue verbatim (311 entries)
catalogue_count    int   = len(catalogue) = 311
roles              {role_name: {biome_weights: {...}}}   ← wildlife ONLY
method             str   (long prose, quoted verbatim below)
limits             str   (long prose)
```

## `result['beast_nests']` (monsters)
Same shape, except `version = BEAST_NESTS_VERSION = 4`, no `roles` key, `profiles` is the 369 monster entries and `catalogue_count = 369`. `evaluated_age` is added by `terrain_history`, never here.

## Site record — **the key field is `id`** (a string), and there is also `species_id`
```
id          str   f"{prefix}-{species_id}-{node}"   prefix is "animal" for wildlife,
                  "nest" for beast_nests (NOT "monster" — the block name and the id
                  prefix disagree on purpose-by-accident; preserve it)
species_id  str   profile id, e.g. "gray-wolf", "absence-titans"
name        str   profile display name
node        int   sphere_grid node index (THE STABLE KEY — see ordering_hazards)
x, z        int   grid column / row of that node (pole nodes always have x = 0)
layer       str   always the literal "surface"
direction   [f,f,f]  unit Vec3, the CELL CENTRE direction, not a sub-cell position
biome       int   natural biome id of the cell
tier        int   1..5, DANGER tier (see the `tier` disambiguation in edge_cases)
family      str   "animal" for every animal; one of void/aberrant/fantastic/fey/holy/
                  unholy/draconic/infernal/primordial/undead/blood for monsters
kind        str   one of "den","lair","aquatic territory","colony","roost"
size        str   one of "tiny","small","medium","large","huge","colossal"
role        str|null  feeding role for animals; null for monsters
suitability f     the weighted suitability score that admitted it, >= nest_min_suitability
range_m     f     territory RADIUS in metres = profile.spacing_m * nest_spacing / 2
                  (profile.spacing_m is a DIAMETER; range_m is half of it)
class       str   "animal" or "monster"  ← literally the key "class"
den         bool  kind in ("den","lair") — a place you can find and raid
real        bool  class == "animal" — recomputed here, NOT copied from profile.real
```
Fields on the profile that are **not** copied onto a site: `movement`, `sky`, `likeness`, `likeness_traits`, `likeness_secondary`, `occurrence`, `medium`, `temperature`, `weights`, `requires`, `biome_weights`, `spacing_m`. Consumers get those from the block's `profiles` array.

## Diagnostics row — key field is `species_id`
```
species_id  str
name        str
tier        int
role        str|null   ← present on wildlife rows only; beast_nests rows omit `role`
placed      int        counts[species_id] AFTER the nest_limit cap has rewritten counts
cells       int        species_cells[species_id]: how many cells it could live in at all
                       (NOT recomputed after the cap)
reason      str        exactly one of REACH_REASONS (see constants)
```

## Saturation row — key field is `tier`
```
tier               int
cells              int   raster cells this tier can reach at all (ceiling on `placed`)
drawn              int   groups the rate asked for, before any territory refusal
                         (NOT adjusted by the nest_limit cap)
placed             int   sites that survived, AFTER the cap
refused            int   drawn - placed
rate_distance_m    f|null  rate-weighted mean distance from settled ground of the
                           groups the intensity asks for. null when nothing is settled
                           or the tier's total rate is 0.
placed_distance_m  f|null  plain mean distance from settled ground of the placed sites.
                           null when nothing is settled or placed == 0.
```

## Reachability
```
authored          int  len(catalogue)
eligible          int  profiles with cells > 0 in THIS world
placed            int  profiles with placed > 0 in THIS world
never_satisfiable int  profiles provably unreachable in ANY world
scope             str  "`eligible` and `placed` count this world only and move with the
                        seed; they are a sample, not a capability. `never_satisfiable`
                        is decided from the profile alone and holds for every world."
```

## `method` and `limits` strings — reproduce verbatim
`wildlife.method`: "Thinned point process over habitable ground: density falls geometrically with danger tier, so prey are common and apex animals rare. Hunting grounds are a centre and a radius; only a species competes with itself. `saturation` reports, per tier, the raster cells the tier can reach, the groups the rate drew, what was placed, what territory refused, and the mean distance from settled ground of the rate and of the placed set. `diagnostics` reports, per species, the cells it could live in and why it is not here; `reachability` counts the catalogue against what this world holds."

`beast_nests.method`: "Thinned point process keyed to magic and terrain, independent of the animals. Density falls geometrically with danger tier and territory grows with it; a lair is refused only by an equal lair, so a greater monster tolerates a lesser inside its territory and hunting grounds overlap. `saturation` reports, per tier, the raster cells the tier can reach, the lairs the rate drew, what was placed, what territory refused, and the mean distance from settled ground of the rate and of the placed set. `diagnostics` reports, per species, the cells it could live in and why it is not here; `reachability` counts the catalogue against what this world holds."

`wildlife.limits`: "Static habitat proposals. No prey carrying capacity, herd sizes, migrations or runtime spawning; a range is where an animal may be met, not a simulated population. Placement resolves at the raster cell: every site in a cell carries the cell centre, so the spacing rule can keep only one site per species per cell and a species whose rate exceeds the cells it reaches is pinned there rather than growing with the ground. The pyramid is exact in `saturation.drawn` and clipped in `sites`, and the danger gradient is exact in `saturation.rate_distance_m` and clipped the same way. `reachability.eligible` and `.placed` are this world only and move with the seed; only `never_satisfiable` holds for every world."

`beast_nests.limits`: "Static habitat proposals. No hostility, population, patrol or runtime spawning; territory is a claim on ground, not a simulated creature. Placement resolves at the raster cell: every lair in a cell carries the cell centre and no species' territory is as wide as the raster pitch, so a tier holds at most one lair per cell and a tier whose rate exceeds the cells it reaches is pinned at that ceiling however much ground the world gains. The pyramid is exact in `saturation.drawn` and clipped in `sites`, and the danger gradient is exact in `saturation.rate_distance_m` and clipped the same way: danger ramps outward from people in the rate and the placed medians barely move. `reachability.eligible` and `.placed` are this world only and move with the seed; only `never_satisfiable` holds for every world. `saturation` reports what this pass produced: a divine visitation purges lairs from `sites` afterwards, so the two stop agreeing once one has run."

(Note: the `beast_nests.limits` clause "no species' territory is as wide as the raster pitch" is **false** and is the exact claim `NEST-SAME-CELL-CLAIM-IS-FALSE.md` exists to retract. It survives in this string only; the module header and the conformance record were corrected. Port the string as-is or fix it, but do not implement it.)

## Algorithm

Everything below is per-pass. `add_animals` and `add_monsters` differ only in five inputs (catalogue filter, density, falloff, `clearance`, `bucket`, rng domain) and in which block they write.

---
## 0. Profile loading and validation — `profiles()`, `roles()` (cached, once per process)

Parse `terrain_nest_profiles.json`. `profiles()` returns `document['profiles']`; `roles()` returns `document['roles']`. Validate, raising on the first failure:
1. `len({p['id'] for p in rows}) != len(rows)` → `ValueError('Duplicate creature profile IDs')`
2. for each `p`: `p['medium'] not in ('land','shore','marine','freshwater')` **or** `not p['weights']` **or** `any(v <= 0 for v in p['weights'].values())` → `ValueError('Invalid nest profile: ' + p['name'])`
3. `p['class'] not in ('animal','monster')` **or** `not (1 <= p['tier'] <= 5)` → `ValueError('Creature needs class animal or monster and tier 1..5: ' + p['name'])`
4. `p['class'] == 'animal'` and `p.get('role') not in roles()` → `ValueError('Animal needs a known role: ' + p['name'])`
5. `p['class'] == 'monster'` and `not p.get('biome_weights')` → `ValueError('Monster needs its own biome_weights: ' + p['name'])`

Rule 5 exists because a monster has no feeding role, so a monster without its own table falls through `biome_weight` to `1.0` and is equally at home on a glacier and in a rainforest. That fallback was the whole of the monster half's opinion about biome until the 2026-09-20 ruling. **Refuse it rather than let it come back.**

---
## 1. Entry — `add_animals(result, cfg)` / `add_monsters(result, cfg)`

```
if not cfg.world_recipe or cfg.phase < 7: return result
o       = options(cfg)
radius  = result['effective_config']['globe_radius']
points, areas, _ = sphere_grid(cfg.size, radius)
cells   = habitat_cells(result, cfg, points, areas)
settled = settled_directions(result, cfg)

ANIMALS:  catalogue = [p for p in profiles() if p['class'] == 'animal']
          density   = o['animal_density'] * o['nest_density']
          falloff   = o['animal_tier_falloff']
          clearance(p) = o['nest_settlement_clearance'] * p['tier'] / 3
          bucket(p)    = p['id']          # territorial within a species only
          domain    = 'animals-v1'
          prefix    = 'animal'

MONSTERS: catalogue = [p for p in profiles() if p['class'] == 'monster']
          density   = o['monster_density'] * o['nest_density'] * o['nest_fantasy']
          falloff   = o['monster_tier_falloff']
          clearance(p) = o['nest_settlement_clearance'] * p['tier']
          bucket(p)    = p['tier']        # an EQUAL lair only, never a greater one
          domain    = 'monsters-v1'
          prefix    = 'nest'
```
Catalogue order is the JSON array order with the class filter applied — **preserve it exactly**, it reaches output through the cumulative pick (see `ordering_hazards`).

---
## 2. `habitat_cells(result, cfg, points, areas)` — one record per node, in node order

`points` is `sphere_grid`'s point list: `[(0,0)] + [(x,z) for z in 1..n-2 for x in 0..n-2] + [(0,n-1)]`, so node index `i` == index into `cells`. Node count = `2 + (n-2)*(n-1)` (994 at n=33).

For each `i, (x, z)`:
```
f = { layer_name: layer[z][x]  for every layer in result['layers'] }
water = f.get('water_type', 0)
wet   = f.get('moisture', 0)
f['medium']   = 'marine'     if water == 1
              else 'freshwater' if water and f.get('salinity',0) < 0.2
              else 'salt_lake'  if water
              else 'land'
f['depth']    = f.get('water_depth', 0)                     # METRES, unclamped
f['dryness']  = 1 - clamp(wet)
f['coast']    = f.get('maritime', 0)
f['mountain'] = clamp(f.get('slope',0) / 25)                 # slope in DEGREES
f['plains']   = clamp(1 - f.get('slope',0)/15) * (1 - abs(wet - 0.45))
f['wetland']  = max( float(bool(water)),
                     f.get('river', 0),
                     wet * exp(-f.get('slope',0) / 8) )
f['cold']     = clamp((10 - f.get('temperature', 10)) / 25)

cells[i] = { node: i, x: x, z: z,
             area_km2:  areas[i] / 1e6,
             biome:     int(f.get('natural_biome', f.get('biome', 0))),
             direction: direction(x, z, n),
             fields:    f }
```
`clamp(v) = max(0.0, min(1.0, v))`. Pole nodes carry the area of the whole polar ring (`sphere_grid` multiplies the band wedge by `n-1` there), so a pole cell is enormously larger than any other and generates a correspondingly large rate.

---
## 3. `settled_directions(result, cfg)`
```
sites = result.get('settlements',{}).get('sites',[])
      + result.get('fisheries',{}).get('ports',[])
      + result.get('humans',{}).get('hamlets',[])
return [ s.get('direction') or direction(s['x'], s['z'], n)  for s in sites ]
```

---
## 4. `nearest_settled(cells, settled, radius)` — great-circle distance to the nearest settled thing, in node order

**Semantics:** float-for-float identical to `min(distance(cell.direction, s, radius) for s in settled)`. Written as an argmax so the scan can stop early. `distance(a,b,r) = r * acos(clamp(dot(a,b), -1, 1))`; `acos` is decreasing and the clamp is monotone, so the smallest distance is at the LARGEST dot product.

```
if not settled: return [+inf] * len(cells)

order = sorted(range(len(settled)), key = lambda i: (asin(clamp(settled[i][1],-1,1)), i))
lats  = [asin(clamp(settled[i][1],-1,1)) for i in order]     # ascending latitudes
dirs  = [settled[i] for i in order]
total = len(lats)

for cell in cells:
    c    = cell.direction
    phi  = asin(clamp(c[1], -1, 1))
    left = bisect_left(lats, phi);  right = left
    best = -2.0;  angle = pi
    loop:
        gap_l = phi - lats[left-1]   if left  > 0     else None
        gap_r = lats[right] - phi    if right < total else None
        if gap_l is None and gap_r is None: break
        if gap_r is None or (gap_l is not None and gap_l <= gap_r):
            gap = gap_l;  left -= 1;  at = left
        else:
            gap = gap_r;  at = right;  right += 1
        if gap >= angle + LATITUDE_SLACK: break
        dot = sum(x*y for x,y in zip(c, dirs[at]))
        if dot > best:
            best  = dot
            angle = acos(clamp(best, -1, 1))
    out.append(radius * acos(clamp(best, -1, 1)))
```
**The stopping rule is the one piece of new mathematics.** The angle between two unit vectors is at least the difference of their latitudes, whatever their longitudes do. With the sites sorted by latitude and walked outwards from the cell's own latitude — nearest latitude first, alternating sides, always taking whichever side's remaining gap is smaller — the walk can stop the moment that gap reaches the best angle already found: everything unvisited is at least that far away on both sides.

**The slack is ADDED, never subtracted.** The walk goes one candidate too far rather than one too few. A cell whose nearest settled thing is due north or south has a latitude gap exactly equal to its angle; subtracting the slack stops the walk on the tie. Measured at size 33 that lost the argmax at 2 of 994 cells and moved `rate_distance_m` in the last three digits.

Cost: the naive form was one `acos` per cell per settled site — 108 million of them on a size 513 world holding 410 settled things.

---
## 5. `_place(...)` — the shared thinned point process

### 5a. Setup
```
rng      = Random(child_seed(cfg.seed, domain, o['nest_variation']))
placed   = []
counts   = { p.id: 0 for p in catalogue }
nearest  = nearest_settled(cells, settled, radius)
span     = NEST_DANGER_SPAN_M * reach_scale(2*pi*radius)      # 300 m × circ/11148.9616 m
```
Clearance and the ramp live **in the rate**, not as a post-hoc rejection. Rejecting a draw that already happened quietly leaves the world holding far fewer creatures than the density asked for.

### 5b. Ground-candidate memo — `ground_candidates(medium, biome)`
Two of the four gates read nothing about a cell but its **medium** and its **biome**, and a world has about a dozen distinct pairs (twelve at size 65, twelve at size 129) against tens of thousands of cells. Cache per `(medium, biome)`:
```
row = []
for p in catalogue:                      # CATALOGUE ORDER, unchanged
    if medium != ('land' if p.medium == 'shore' else p.medium): continue
    w = biome_weight(p, biome)
    if w > 0: row.append((p, w, clearance(p)))
```
Measured at size 65: of the cell-by-species pairs the flat loop used to walk, the biome gate refuses 44% of the animal ones and the medium gate a further 26%; the medium gate alone refuses 67% of the monster ones (every monster biome table names every live biome, so the monster half has no biome opinion at all and all its pruning here is medium).

Nothing is hoisted out of `suitability`, which still re-tests the medium it was selected on. Keeping that call whole is worth more than the duplicated comparison: it stays the single published answer to "can this species live here".

### 5c. `biome_weight(p, biome_id)`
```
table = p.get('biome_weights')
if table is None and p.get('role'): table = roles()[p['role']].get('biome_weights')
if not table: return 1.0                    # unreachable for the shipped catalogue
return table.get(str(biome_id), 0.0)        # KEYS ARE DECIMAL STRINGS
```
**An absent biome key is exclusion, not neutrality.** A hand-authored five-key table silently forbids a quarter of the catalogue the ground it was measured on.

### 5d. `suitability(p, f) -> (score, factors)` — hard eligibility before preferences, absent fields fail closed
```
medium = f.get('medium')
if medium != ('land' if p.medium == 'shore' else p.medium):
    return 0.0, {'excluded': 'water/land habitat mismatch'}
if p.medium == 'shore' and f.get('coast', 0) < 0.1:
    return 0.0, {'excluded': 'no coastal refuge'}
if not (p.temperature[0] <= f.get('temperature', 100) <= p.temperature[1]):
    return 0.0, {'excluded': 'temperature outside profile range'}
if any(f.get(k, 0) < v for k, v in p.requires.items()):
    return 0.0, {'excluded': 'missing required habitat or magical influence'}

total   = sum(p.weights.values())
factors = { k: clamp(f.get(k, 0)) * w / total  for k, w in p.weights.items() }
return sum(factors.values()), factors
```
So the score is a **weight-normalised convex combination of CLAMPED field values**, in [0,1]. Note the asymmetry: `requires` thresholds test the **raw, unclamped** field (so `depth: 30` means 30 metres) while `weights` test the **clamped** field (so a `depth` weight is 1.0 at any depth over 1 m — see `open_questions`). A `shore` species is tested against `medium == 'land'` and then additionally requires `coast >= 0.1`.

### 5e. Per-cell room accumulation — first pass over `cells`
```
rooms = []                  # per cell: list of (p, wet, room, score)
tier_room = {}              # (tier, wet) -> float
ground    = {}              # wet -> km2
tier_cells= {}              # tier -> set of node ids
species_cells = { p.id: 0 for p in catalogue }

for cell, clear_of in zip(cells, nearest):
    medium = cell.fields.get('medium');  wet = (medium != 'land')
    ground[wet] = ground.get(wet, 0.0) + cell.area_km2
    here = []
    for (p, weight, clear_needed) in ground_candidates(medium, cell.biome):
        if clear_of < clear_needed: continue                      # HARD clearance
        score, _ = suitability(p, cell.fields)
        if score < o['nest_min_suitability']: continue            # HARD floor
        room = p.occurrence * score * weight
             * danger_ramp(p.tier, clear_of, span) * cell.area_km2
        if room <= 0: continue
        here.append((p, wet, room, score))
        species_cells[p.id] += 1
        tier_room[(p.tier, wet)] = tier_room.get((p.tier, wet), 0.0) + room
        tier_cells.setdefault(p.tier, set()).add(cell.node)
    rooms.append(here)
```
`room` is what the species would claim if nothing else existed: how common it is (`occurrence`), how well the ground suits it (`score`), how much it likes the biome (`weight`), how the danger gradient treats it there (`danger_ramp`), and how much ground there is (`area_km2`).

**Use plain left-to-right accumulation, not a compensated sum.** The Python comment says so explicitly: CPython compensates a builtin `sum` of floats and the port would have to replicate that to stay bit-exact, which is not worth buying here. (Irrelevant to a clean rewrite; recorded because it tells you which accumulations were *deliberately* left uncompensated.)

Note `ground[wet]` accumulates for **every** cell of that wetness, habitable or not — including `salt_lake` cells, which no profile can ever match. `ground[False]` is total land area, `ground[True]` is total water area.

### 5f. `danger_ramp(tier, clear_of, span)`
```
reach  = span * span
out    = clear_of * clear_of
near   = reach / (reach + out)          # 1 on the doorstep, 1/2 one span out, 0 at infinity
danger = (tier - 1) / 4.0               # 0 at tier 1, 1 at tier 5
return NEST_DANGER_FLOOR
     + (1 - NEST_DANGER_FLOOR) * (danger*(1 - near) + (1 - danger)*near)
```
Intent, quoted: the clearance is a hard floor and stays one — nothing lairs in the town square. But **a cutoff is the wrong shape for a difficulty gradient and cannot make one at any resolution**: scaling `clearance * tier` up digs a wider hole around every settlement rather than ramping anything, and measured on a 200 km world it left a tier five apex monster and a tier one prey animal at the same median 6.45 km from the nearest town, against a source comment declaring a 3:1 intent. So the ramp is continuous and lives in the rate.

**A ratio of squares rather than an exponential: no `pow`, no `exp`, nothing whose last bit depends on the libm the native build links against.** Squared because the plain ratio `span/(span+d)` is too gentle to be worth having — the usable band on a settled world is about one span wide, and across it the plain ratio swings by only a factor of two however low the floor goes.

Properties: tier 1 is monotonically decreasing in distance (drawn inward), tier 5 monotonically increasing (pushed outward), and the two cross exactly once. **Tier 3 is the pivot: `danger_ramp(3, d, span) == (1 + NEST_DANGER_FLOOR)/2 == 0.6` at every distance.** A five-tier danger ladder is therefore not something this mechanism can produce at any tuning on any world. Every tier keeps at least `NEST_DANGER_FLOOR` of its rate everywhere including at `clear_of == inf`.

**The ramp is purely redistributive.** `budget` renormalises by `tier_room`, which sums the same factor, so it moves a tier over the ground and never changes how many of that tier a world holds.

### 5g. Tier budget and per-cell rates
```
budget = { key: tier_density(density, falloff, key[0]) * ground[key[1]] / room
           for key, room in tier_room.items() if room > 0 }
rates  = [ [ (p, budget[(p.tier, wet)] * room, score)
             for (p, wet, room, score) in here ]
           for here in rooms ]
```
`tier_density(base, falloff, tier) = base * falloff ** (1 - tier)`.

**The pyramid is a claim about the world, so it is a budget for the world**, not per cell: tier `t` gets `density * falloff^(1-t)` groups per square kilometre of its own medium, spread over whatever ground the tier can actually use. Sharing per cell instead loses a tier its share wherever no member qualifies, and the low monster tiers are the specialised ones — grave mites want a barrow, shipbreaker crabs want any sea at all. **Measured, that inverted the pyramid outright: 114 tier four lairs against 9 tier one. A budget cannot be forfeited by being fussy about where it is spent.**

**Land and open water are budgeted apart** (the `wet` key). A species belongs to exactly one of them; budgeting together let the sea decide how empty the land was — measured on one world, land was a quarter of the surface and held five per cent of the animals, half a group per km² against a teeming ocean.

**`occurrence` divides a tier's share among the tier's members and nothing else.** It is authored as a pure function of `size`, so spending it as an absolute multiplier taxed the big creatures a second time for being big: tier five monsters average 0.26 against tier one's 0.68, which on top of the pyramid left them at a hundredth rather than a fortieth and emptied small worlds of anything worth fearing. Because `budget` divides by `tier_room` — which already contains `occurrence` — the tier total is unaffected by it.

Consequence worth stating: the expected total number of groups of tier `t` on medium `w` is exactly `tier_density(density, falloff, t) * ground[w]`, independent of suitability, biome weights, occurrence and the ramp. All of those decide only *where* within the tier's reachable ground the groups land and *which species* claims each one.

### 5h. Rate-weighted distance accumulation
```
rate_reach = {};  rate_weight = {}
if settled:                                   # guard: inf * 0 rate = NaN in the document
    for clear_of, shares in zip(nearest, rates):
        for (p, rate, _) in shares:
            rate_reach[p.tier]  = rate_reach.get(p.tier, 0.0)  + rate * clear_of
            rate_weight[p.tier] = rate_weight.get(p.tier, 0.0) + rate
clear_by_node = { cell.node: clear_of for cell, clear_of in zip(cells, nearest) }
```
This is `danger_ramp` integrated over the world and the only place the danger gradient is exact.

### 5i. The placement loop — second pass over `cells`
```
drawn   = {}            # tier -> int, every group the rate asked for
at_node = {}            # bucket -> { node -> [site,...] }   placement order
on_row  = {}            # bucket -> { z    -> [site,...] }   placement order
widest  = {}            # bucket -> largest range_m placed so far
pitch   = radius * pi / (cfg.size - 1)     # MERIDIONAL pitch; uniform with latitude

for cell, shares in zip(cells, rates):
    if not shares: continue
    total  = sum(rate for _, rate, _ in shares)       # float sum in catalogue order
    groups = int(total)                               # truncation toward zero
    if rng.random() < total - groups: groups += 1     # ONE Bernoulli draw, always taken
    settled_here = set()                              # species this cell has REFUSED
    for _ in range(groups):
        p, score = _pick(rng, shares, total)
        drawn[p.tier] = drawn.get(p.tier, 0) + 1
        if per_species and counts[p.id] >= per_species: continue
        if p.id in settled_here: continue
        reach = p.spacing_m * o['nest_spacing'] / 2
        b     = bucket(p)
        if _blocked(at_node.get(b), on_row.get(b), widest.get(b, 0.0),
                    cell, reach, radius, pitch):
            settled_here.add(p.id); continue
        counts[p.id] += 1
        site = { ...as in `writes`... }
        at_node.setdefault(b, {}).setdefault(cell.node, []).append(site)
        on_row .setdefault(b, {}).setdefault(cell.z,    []).append(site)
        if reach > widest.get(b, 0.0): widest[b] = reach
        placed.append(site)
```
`floor + one Bernoulli draw` is an **exact discretisation** while the cell rate is below one, which it is almost everywhere, and no new distribution is needed.

The `settled_here` memo: a species this cell has already REFUSED needs no second answer — the company only ever grows and the test is a disjunction over it, so the answer cannot come back the other way. A cell draws about ninety groups from a few dozen species, so it is asked the same question many times; **62% of animal draws and 47% of monster draws at size 65 repeat a `(cell, species)` pair.** A species this cell has already PLACED is deliberately NOT memoised, though it will be refused too: memoising it would mean asserting that a second site in the same cell is always refused, which needs `reach` to be positive — true of the shipped catalogue and of the option bounds, but an assumption, and `_blocked` answers it from `at_node` in one dict hit regardless. Both forms were timed at size 65 and neither is faster.

Note the ordering: `drawn` is incremented **before** the per-species cap and the memo, so `drawn` counts every group the rate asked for, including ones no rule could ever have placed.

### 5j. `_pick(rng, shares, total)` — cumulative weighted pick
```
draw = rng.random() * total;  cursor = 0.0
for (p, rate, score) in shares:
    cursor += rate
    if draw <= cursor: return p, score
return shares[-1][0], shares[-1][2]        # float fallback
```
Note the comparison is `<=`, and `shares` is walked in catalogue order.

### 5k. `_blocked(at_node_for_bucket, on_row_for_bucket, widest, cell, reach, radius, pitch)`

**Exactly** `any(distance(a.direction, cell.direction, radius) < max(reach, a.range_m))` over everything the bucket has placed. What the structure changes is which pairs are worth forming — every comparison performed IS that predicate on a real pair.
```
here   = at_node[cell.node] if at_node else None
facing = cell.direction
if here:
    for a in here:
        if distance(a.direction, facing, radius) < max(reach, a.range_m): return True
if not on_row: return False
band = int(max(reach, widest) / pitch) + 1
for row in range(cell.z - band, cell.z + band + 1):
    for a in on_row.get(row, ()):
        if distance(a.direction, facing, radius) < max(reach, a.range_m): return True
return False
```
**Same cell first.** A site sits at its cell's centre, so a second site in the same cell is at distance zero and refused by any positive reach — and same-cell is where the refusals are. Measured at size 65: **71% of every comparison the flat scan made was a walk towards a blocker that turned out to be in the candidate's own cell**, usually the one appended last, because a cell that has just placed something keeps drawing. The test is still the real predicate rather than an assertion that reach is positive.

**Then the rows `reach` can cross.** `direction` puts grid row `z` at latitude `pi/2 - pi*z/(n-1)`, and the angle between two unit vectors is at least the difference of their latitudes, so two sites `r` rows apart are at least `r*pitch` apart on the sphere whatever their longitudes are. A blocker must be nearer than `max(reach, range_m)`, so it must be inside that many rows. **One row is added to the bound**, because it is exact in real arithmetic and a raster row is cheaper than an argument about the last bit of a divide. The band is taken over `widest` (the widest territory *in the bucket*), not over the candidate's own reach, or a narrow-ranged species walks straight through a wide incumbent's territory — for a monster the bucket is a tier, so the blocker's reach routinely exceeds the candidate's.

The band scan re-visits the candidate's own row and therefore its own cell; the duplicate comparison is harmless and the function short-circuits.

**This is NOT the claim that only the same cell can ever block.** The grid is lat/lon, so the rings beside the poles are packed far tighter than the equator — 2.4 m against a 391 m equatorial pitch at size 513 on a 200 km world, 611.6 m at size 33 — and a cross-cell refusal is ordinary ground there rather than an edge case. Measured at seed 42 size 65: **426 of 9777 monster refusals and 50 of 47311 animal refusals have their blocker in another cell**; a same-cell-only test would place 476 sites the real scan refuses.

`distance(a, b, radius) = radius * acos(clamp(dot(a,b), -1, 1))`.

### 5l. The `nest_limit` safety valve
```
limit = o['nest_limit']
if limit and len(placed) > limit:
    placed = sorted(placed, key=lambda a: (-a.tier, a.id))[:limit]
    counts = { key: 0 for key in counts }
    for a in placed: counts[a.species_id] += 1
```
A safety valve, not the thing that decides how full a world is. When it bites it keeps the notable creatures: highest tier first, then by `id` **lexicographically**. It rewrites `counts` but **not** `species_cells`, so a species the cap empties reports `reason = 'lost the draw to incumbents'`.

### 5m. `cleared_out` — which absent species the settlement clearance alone refused
```
ceiling = max((clearance(p) for p in catalogue), default=0.0)
close   = [(cell, clear_of) for cell, clear_of in zip(cells, nearest) if clear_of < ceiling]
floor   = o['nest_min_suitability']
cleared_out = { p.id: (species_cells[p.id] == 0) and any(
                    clear_of < clearance(p)
                    and biome_weight(p, cell.biome) > 0
                    and suitability(p, cell.fields)[0] >= floor
                    for cell, clear_of in close)
                for p in catalogue }
```
Scanned only over the cells close enough to be refused by anything in the catalogue, and only for species with no cells at all — a few hundred species against a few hundred cells rather than a second pass over the raster.

### 5n. `saturation`
```
kept = {};  kept_reach = {}
for a in placed:                                  # POST-cap
    kept[a.tier]       = kept.get(a.tier, 0) + 1
    kept_reach[a.tier] = kept_reach.get(a.tier, 0.0) + clear_by_node[a.node]

saturation = [ { tier: t,
                 cells:    len(tier_cells.get(t, ())),
                 drawn:    drawn.get(t, 0),
                 placed:   kept.get(t, 0),
                 refused:  drawn.get(t, 0) - kept.get(t, 0),
                 rate_distance_m:   rate_reach[t]/rate_weight[t]
                                    if settled and rate_weight.get(t) else None,
                 placed_distance_m: kept_reach[t]/kept[t]
                                    if settled and kept.get(t) else None }
               for t in sorted(set(tier_cells) | set(drawn) | set(kept)) ]
```
**Why this exists.** The rate is an exact geometric pyramid; what is placed is nearly flat, and the difference is not ecology. On the reference world (seed 42, size 33, phase 13, re-measured 2026-09-21) monster draws by tier are **6762 / 3305 / 1716 / 862 / 429**, each about half the one below, while placed is **350 / 359 / 304 / 277 / 204**. On the threefold-area world: **20295 / 10072 / 4991 / 2541 / 1324** drawn and **368 / 376 / 347 / 314 / 296** placed. *Quote the block, not these numbers, if the two ever disagree.*

The same ceiling swallows the danger gradient. On the reference world the monster rate runs **4910 / 5285 / 5051 / 5565 / 6184 m** and the placed set **5730 / 5706 / 5281 / 5426 / 5787 m** — the rate carries 1.27 km end to end and the sites carry 0.51 km, because at this raster every tier is 60% to 98% of the way to its cell ceiling. Ablating the ramp entirely moves the spread of placed tier medians by 0.02 km; with the ramp ablated the rate span collapses to 236 m (monsters) and −76 m (animals). At size 129 the monster placed span is 2.38 km against 1.23 km ablated, because tier five is then only 7.5% of its cell ceiling instead of 60% — **the clipping claim is about the widths this product ships, not a property of the mechanism.**

### 5o. Return
`_place` returns `(sorted(placed, key=lambda a: a['id']), counts, species_cells, cleared_out, saturation)`.

---
## 6. Diagnostics and reachability — `reach_reason`, `never_satisfiable`, `reachability`
```
never_satisfiable(p) = any(k in DORMANT_FIELDS and v > 0 for k, v in p.requires.items())

reach_reason(p, cells_reached, placed_count, cleared_out=False):
    if placed_count:            return PLACED
    if never_satisfiable(p):    return NEVER_SATISFIABLE
    if cells_reached:           return LOST_THE_DRAW
    return CLEARED_OUT if cleared_out else ABSENT_FROM_THIS_WORLD

reachability(catalogue, species_cells, counts) = {
    authored:          len(catalogue),
    eligible:          count of p with species_cells[p.id] > 0,
    placed:            count of p with counts[p.id] > 0,
    never_satisfiable: count of p with never_satisfiable(p),
    scope:             <the fixed string in `writes`> }
```
`never_satisfiable` is the whole of what is decidable without generating worlds, and it is much less than the set of profiles a given world happens not to hold. **Measured across five seeds at size 33: 579 of 680 profiles are eligible on all five, 53 on some but not all, and 48 on none — and only 25 of that 48 are provable here.** Calling the other 23 unreachable would be asserting a search nobody ran; `polar-bear` and `sea-otter` want ground those five worlds did not roll, not ground no world can carry. A `requires` on a dormant school is the provable case because the threshold is a hard gate against a layer that is identically zero in every world the generator can build. A profile that merely *scores* on a dormant school is not proved unreachable, only handicapped.

Reference-world reason breakdown — monsters: 167 placed / 107 lost the draw / 57 cleared out / 25 never satisfiable / 13 conditions absent. Animals: 286 / 1 / 0 / 0 / 24. `reachability` on the reference world is 680 / 561 / 453 authored/eligible/placed with 25 provable.

---
## 7. Block assembly
Build the block dicts exactly as listed in `writes`, with `diagnostics` generated in catalogue order:
```
diagnostics = [ { species_id: p.id, name: p.name, tier: p.tier,
                  role: p.get('role'),                        # WILDLIFE ONLY
                  placed: counts[p.id], cells: species_cells[p.id],
                  reason: reach_reason(p, species_cells[p.id], counts[p.id],
                                       cleared_out[p.id]) }
                for p in catalogue ]
```

## Constants

## Module constants

| name | value | meaning / what changing it does |
|---|---|---|
| `MEDIA` | `('land','shore','marine','freshwater')` | the only legal `profile.medium` values. `habitat_cells` also produces `'salt_lake'`, which is **not** in this tuple, so a salt lake cell can never match any profile. Adding `'salt_lake'` here alone would do nothing — a profile would have to declare it. |
| `WILDLIFE_VERSION` | `3` | version stamped on the `wildlife` block. Bump only for an observable change; the conformance record asserts it via an HTML marker. |
| `BEAST_NESTS_VERSION` | `4` | version stamped on `beast_nests`. |
| `NEST_DANGER_SPAN_M` | `300.0` | how far out the danger ramp turns over, **in reference-world metres**. Multiplied by `reach_scale(2*pi*radius)` at use, giving 5381.7 m on a 200 km world, 10763.3 m at 400 km, 16145.0 m at 600 km. Authored against the 11.15 km reference like every other reach in this repo, **because an absolute metre constant is sub-cell on a 200 km world and ramps nothing.** Raising it flattens the gradient over a wider band; lowering it confines the whole gradient to the immediate ring around settlements, where few cells are. It cannot change any tier's total count — the budget renormalises it away. |
| `NEST_DANGER_FLOOR` | `0.2` | the share of a tier's rate that survives at the wrong end of the ramp. **Nothing may become impossible**: a floor of zero would make a tier five species unplaceable on the doorstep of a town and a tier one species unplaceable in the wilderness, and neither is true. Raising it toward 1.0 flattens the gradient to nothing; lowering it toward 0 sharpens it but starts excluding ground. Note the derived identity `danger_ramp(3, d, span) == (1 + NEST_DANGER_FLOOR)/2 == 0.6`, which several tests key on. |
| `LATITUDE_SLACK` | `1e-9` (radians) | slack on the latitude stopping bound in `nearest_settled`. The bound is exact in real arithmetic; this absorbs rounding in the two `asin` calls. 1e-9 rad is 3e-5 m on the widest world this product ships, **ten orders of magnitude above the error it covers**, and being generous costs at most one extra candidate per cell. It is ADDED to `angle`, never subtracted (see the algorithm). |
| `PLACED` | `'placed'` | `diagnostics.reason` value |
| `LOST_THE_DRAW` | `'lost the draw to incumbents'` | reason value |
| `ABSENT_FROM_THIS_WORLD` | `'conditions absent from this world'` | reason value |
| `NEVER_SATISFIABLE` | `'never satisfiable'` | reason value — the only world-independent verdict |
| `CLEARED_OUT` | `'its only ground is inside a settlement clearance'` | reason value. **Not a world reason and must not be reported as one.** |
| `REACH_REASONS` | tuple of the five above, in that order | the closed vocabulary; a test asserts every row takes one of them |
| `DORMANT_FIELDS` | `frozenset('ley_' + n for n in HIDDEN_NETWORKS)` = `{'ley_blood','ley_void','ley_rot','ley_eldritch'}` | ley schools generation can never raise: `<school>_occurrence` is locked at `spec(0., 0., 0.)` for a hidden network. A weight on one contributes nothing anywhere; a `requires` on one can be met nowhere. |

**Five reasons, not four,** because "absent" bundles one catalogue defect (`never satisfiable`) and one placement rule (`cleared out`) with two ordinary facts about a particular world, and a count that does not separate them is the thing `key_locations` grew `diagnostics` to stop doing.

## Derived constants and magic numbers inside functions

| expression | value | meaning |
|---|---|---|
| `REFERENCE_CIRCUMFERENCE_M` (from `terrain_scale`) | `2*pi*1774.4123532462844` = `11148.961626795008` m | the reference world every metre constant in this repo is authored against |
| `reach_scale(c)` | `c / 11148.961626795008` | 17.9389 at 200 km, 35.8778 at 400 km, 53.8167 at 600 km |
| `clamp(v)` | `max(0., min(1., v))` | used in `suitability` factors and throughout `habitat_cells` |
| `salinity < .2` | freshwater/salt-lake split threshold for a `water_type == 2` cell | |
| `slope / 25` | `mountain` from slope in **degrees**; saturates at 25° | |
| `slope / 15` | `plains` slope term; zero above 15° | |
| `abs(moisture - .45)` | `plains` moisture term; peaks at 45% moisture, worst at the extremes | |
| `exp(-slope / 8)` | `wetland` slope decay; halves roughly every 5.5° | |
| `(10 - temperature) / 25` | `cold`; 0 at 10 °C and above, 1 at −15 °C and below | |
| `0.1` | minimum `coast` (`maritime`) for a `shore` species | |
| `100` | the `temperature` default in `suitability` when the layer is missing — a value chosen so **every** profile's temperature band fails closed (the catalogue's highest upper bound is 45 °C) | |
| `p.spacing_m * nest_spacing / 2` | `range_m`; `spacing_m` is a diameter | |
| `clearance` animals | `nest_settlement_clearance * tier / 3` → 83.3 / 166.7 / 250 / 333.3 / 416.7 m at the default | game keeps its distance from people in proportion to how dangerous it is |
| `clearance` monsters | `nest_settlement_clearance * tier` → 250 / 500 / 750 / 1000 / 1250 m at the default | |
| `pitch` (in `_blocked`) | `radius * pi / (n - 1)` | the **meridional** pitch, uniform with latitude; at 200 km it is 3125 m at n=33, 1562.5 m at n=65, 195.3 m at n=513. **This is half the equatorial zonal pitch** (`2*pi*r/(n-1)`), which is the number the board card quotes. |
| `band` | `int(max(reach, widest)/pitch) + 1` | the `+1` is the deliberate over-reach described in the algorithm |
| `tier_density` | `base * falloff ** (1 - tier)` | animals at defaults: 4.5 / 1.125 / 0.28125 / 0.0703125 / 0.017578125 groups per km²; monsters: 0.53 / 0.265 / 0.1325 / 0.06625 / 0.033125 lairs per km² |
| `areas[i] / 1e6` | m² → km² | |
| `child_seed` domain strings | `'animals-v1'`, `'monsters-v1'` | |
| site `id` prefixes | `'animal'`, `'nest'` | |

## Data files

## `Sim/icarus_sim/terrain_nest_profiles.json` — 416,212 bytes (406 KB)

Loaded once by `_document()` (`lru_cache(maxsize=1)`) from `Path(__file__).with_name(...)`, UTF-8. Top-level object:
```
{ "version": 2,
  "roles":    { <role_name>: { "biome_weights": { "<biome_id>": <0..1>, ... } } },   // 21 roles
  "profiles": [ <Profile>, ... ] }                                                   // 680 entries
```
`version` is read by nothing in this module.

### Roles (21)
`ambush_predator, amphibian, apex_predator, bird_of_prey, bird_other, cephalopod, fish, herbivore, invertebrate, large_herbivore, marine_mammal, mustelid_small, omnivore, pack_predator, pursuit_predator, reptile, scavenger, shark_other, small_prey, venomous, waterfowl`.

Each role has exactly one field, `biome_weights`, a map from **decimal-string natural biome id** to a weight in `(0, 1]`. Tables are sparse — an absent biome is ground the role does not use. Example (`ambush_predator`): `{"0":1.0,"1":0.3,"2":0.3,"3":0.4,"4":1.0,"5":0.6,"6":0.2,"7":0.9,"8":1.0,"13":0.5,"15":0.8,"17":0.05}`.

### Profile record — **the key field is `id`**
```
id                 str    unique slug, e.g. "aardvark", "absence-titans"     [680/680]
name               str    display name                                       [680/680]
class              str    "animal" (311) | "monster" (369)                   [680/680]
tier               int    1..5 DANGER tier. Counts: 1→253, 2→151, 3→141,
                          4→85, 5→50 (the catalogue is itself a pyramid)     [680/680]
real               bool   == (class == "animal") — a test asserts this       [680/680]
family             str    "animal" for all 311 animals; for monsters one of
                          void(10) aberrant(40) fantastic(58) fey(37)
                          holy(34) unholy(32) draconic(35) infernal(33)
                          primordial(37) undead(43) blood(10)                [680/680]
size               str    tiny(55) small(299) medium(88) large(167)
                          huge(51) colossal(20)                              [680/680]
medium             str    land(538) marine(79) freshwater(42) shore(21)      [680/680]
kind               str    den(151) lair(257) "aquatic territory"(120)
                          colony(78) roost(74). Every "lair" is a monster;
                          every "den" is an animal.                          [680/680]
temperature        [f,f]  inclusive °C band. Lower bounds run −35..14,
                          upper bounds 12..45.                               [680/680]
weights            {k:f}  habitat preference, all values > 0, unnormalised   [680/680]
requires           {k:f}  hard minimum on a raw field value; may be empty    [680/680]
spacing_m          num    territory DIAMETER in metres, 100..3500            [680/680]
occurrence         f      share of its tier, 0.12..0.70                      [680/680]
sky                bool   true for 25 profiles; NOT read by this module      [680/680]
movement           str    nester(594) migratory(34) follower(17) drifter(20)
                          irruptive(15); NOT read by this module             [680/680]
biome_weights      {k:f}  monsters only, mandatory for them                  [369/680]
role               str    animals only, mandatory for them                   [311/680]
likeness           str    monsters only, art reference; not read here        [369/680]
likeness_traits    [str]  monsters only; not read here                       [369/680]
likeness_secondary str    12 monsters only; not read here                    [ 12/680]
```

### `weights` key frequency (the layer/derived field a species scores on)
`moisture` 335, `wetland` 89, `fishing_productivity` 78, `ley_umbral` 76, `ley_weave` 71, `ley_primordial` 70, `plains` 62, `mountain` 53, `dryness` 46, `ley_holy` 34, `ley_infernal` 33, `cold` 32, `volcanic` 13, `ley_void` 10, `ley_blood` 10, `reef` 9, `coast` 6, `metal_richness` 5, `ley_eldritch` 5, `river` 4, `kelp` 2, and one each of `sheltered_bay`, `rocky_coast`, `lagoon`, `ley_air`, `depth`, `ley_earth`, `zone_haunted_sands`.

### `requires` key frequency and the exact thresholds used
`ley_umbral` 75 (0.02, 0.025), `wetland` 73 (0.1, 0.15), `ley_weave` 71 (0.025, 0.05), `ley_primordial` 70 (0.025), `ley_holy` 34 (0.025), `ley_infernal` 33 (0.025), `mountain` 11 (0.2), `ley_void` 10 (0.05, 0.1, 0.18, 0.24, 0.3), `ley_blood` 10 (same five), `depth` 8 (**30 — raw metres**), `volcanic` 6 (0.04), `ley_eldritch` 5 (0.18, 0.24, 0.3), `cold` 3 (0.4), `coast` 1 (0.15), `ley_air` 1 (0.04), `ley_earth` 1 (0.04). `ley_rot` is never required.

**The 25 provably unreachable profiles are exactly those with a positive `requires` on `ley_void` (10), `ley_blood` (10) or `ley_eldritch` (5).**

### Representative entries
Animal:
```json
{"id":"aardvark","name":"Aardvark","class":"animal","tier":1,"role":"herbivore",
 "real":true,"family":"animal","size":"large","medium":"land","kind":"den",
 "temperature":[14,42],"weights":{"plains":2},"requires":{},
 "spacing_m":750,"occurrence":0.45,"sky":false,"movement":"nester"}
```
Monster (note the full 11-key biome table):
```json
{"id":"absence-titans","name":"Absence titans","class":"monster","tier":5,
 "real":false,"family":"void","size":"huge","medium":"land","kind":"lair",
 "temperature":[-25,45],"weights":{"ley_void":3,"mountain":1},
 "requires":{"ley_void":0.3},
 "biome_weights":{"0":0.7,"2":0.8,"3":0.3,"4":0.25,"5":0.9,"7":0.2,"8":0.4,
                  "13":0.35,"15":0.3,"16":0.75,"17":1.0},
 "spacing_m":450,"occurrence":0.5,"sky":false,"movement":"nester",
 "likeness":"western-gorilla","likeness_traits":["bipedal","incorporeal"]}
```

### Biome ids referenced
The natural biome roster (`Sim/icarus_sim/biomes.json`) is `0 ocean, 1 tundra, 2 desert, 3 grassland, 4 forest, 5 exposed_rock, 6 snow, 7 rainforest, 8 lake, 13 marsh, 15 boreal_forest, 16 cold_tundra, 17 land_ice`. **Ids 1 and 6 are tombstoned** — `terrain_ecology.cold_habitat` relabels every such cell to 15/16/17 before any nest pass runs, so no creature may claim them. Consequently **all 369 monster tables carry exactly the eleven live ids** `{0,2,3,4,5,7,8,13,15,16,17}` and nothing else — a test asserts this exact key set. Role tables may still name 1 and 6 (several do) and those keys are simply never hit. The practical consequence: **the monster half has no biome exclusions at all, only biome preferences**; every monster pruning in `ground_candidates` is the medium gate.

### Mirror channels
`docs/catalogue/creatures.json` holds a derived list whose `name` set must equal `profiles()`'s (`test_catalogue_coverage`). `tools/export_catalogues.py` exports both output blocks to the native catalogue table. A catalogue edit is not landed until both are regenerated.

## Random streams

**One stream per pass**, created fresh at the top of `_place` and used nowhere else:
```
rng = random.Random(child_seed(cfg.seed, domain, o['nest_variation']))
```
where `child_seed(master, domain, variation) = int.from_bytes(sha256(f'tectonics-v1:{master}:{domain}:{variation}').digest()[:4], 'big')` — a uint32 seeding CPython's Mersenne Twister.

`domain` is `'animals-v1'` for `add_animals` and `'monsters-v1'` for `add_monsters`. `variation` is the world option `nest_variation` (default 0, range 0..4294967295). The two passes therefore never interleave, and `test_replay_and_isolation` asserts that changing `nest_variation` moves both `sites` lists while leaving `layers`, `sky`, `settlements` and `world_economy` byte-identical.

**In the C++: use `Rng::child(cfg.seed, "animals-v1", nest_variation)` and `Rng::child(cfg.seed, "monsters-v1", nest_variation)`. Do not invent a fourth seed derivation.**

**Only `rng.random()` is called — no other distribution.** Two call sites:
1. **Once per cell that has a non-empty `shares` list**, unconditionally, for the Bernoulli fractional part: `if rng.random() < total - groups: groups += 1`. This draw happens even when `total == 0.0` (e.g. `nest_density = 0`), because `shares` is non-empty whenever any species has positive `room`, and `room` does not contain `density`.
2. **Once per group drawn**, inside `_pick`: `draw = rng.random() * total`.

Total draws per pass = `(number of cells with a non-empty shares list) + sum(groups over those cells)`. On the reference world the monster pass draws about 13,000 groups and the animal pass about 90,000, so the count is very data-dependent: it moves with world size, raster, density options, settlement count (through the clearance gate) and the ramp.

**Draw count is data-dependent in a way that matters for porting.** Because the Bernoulli draw is taken before the group loop and every group consumes exactly one draw regardless of whether it is placed, the stream position after cell `k` depends on the *rates* at cells `0..k` but not on any placement outcome. That makes the stream independent of `_blocked`, `settled_here`, `nest_per_species` and `nest_limit` — a useful property if you ever want to compare two placement rules on one stream.

No other module's RNG is touched, no global RNG is used, and no process pool is forked. **`ICARUS_CITY_WORKERS` does not apply to this module** — both nest passes are strictly serial, unlike the two settlement planners.

## Ordering hazards

Every place iteration order reaches output. Reproduce each one.

1. **Catalogue order is the JSON array order**, filtered by `class` with order preserved. It reaches output twice: `total = sum(rate for ... in shares)` is a float sum accumulated in that order, and `_pick` walks `shares` cumulatively in that order. **A reordering is a different world even when it is the same set.** `ground_candidates` builds its memoised rows by iterating `catalogue` in order, so the memo preserves it too. `diagnostics` rows are also emitted in catalogue order.

2. **`p['weights']` insertion order** (JSON object key order) reaches output through `total = sum(p['weights'].values())` and `sum(factors.values())` in `suitability`. Both are float sums whose result depends on the order of the keys in the file.

3. **Cell iteration order = `sphere_grid` point order** = `[(0,0)]` then `(x,z)` for `z` in `1..n-2`, `x` in `0..n-2`, then `(0,n-1)`. This is *placement order*: whoever reaches a cell first holds it against everyone in its bucket afterwards. **Node index is therefore latitude-ordered — node 0 is the north pole.** Any test that samples `nodes[:N]` samples the pole.

4. **`at_node` and `on_row` lists are in placement order**, and the same site is appended to both. `_blocked` short-circuits, so the boolean answer is order-independent, but *which* site ends up being the blocker is not — irrelevant to output today because only the boolean is used.

5. **`sites` is `sorted(placed, key=lambda a: a['id'])` — a LEXICOGRAPHIC STRING SORT.** `id` is `f"{prefix}-{species_id}-{node}"`, so node 10 sorts before node 2 and `"animal-wolf-1000"` sorts before `"animal-wolf-99"`. This is the published order of the block. Do not "fix" it to a numeric sort.

6. **The `nest_limit` cap sorts by `(-tier, id)`** — again the lexicographic id as tie-break — then truncates. `test_disabled_and_capped` pins this exact key.

7. **`saturation` rows are `sorted(set(tier_cells) | set(drawn) | set(kept))`** — an ordinary ascending integer sort over a set union. Only the sorted result reaches output, so the set iteration order is safe.

8. **`nearest_settled`'s site sort is `key=(asin(y), i)`** — latitude ascending with the **original index as an explicit stable tie-break**. The original index is the concatenation order `settlements.sites + fisheries.ports + humans.hamlets`, each in its own block's order. Two settled things at exactly the same latitude must be visited in that order or the walk can stop one candidate earlier or later.

9. **`bisect_left(lats, phi)`** — left-bisection specifically. With duplicate latitudes, `bisect_right` would start the walk on the other side.

10. **`budget` is a dict comprehension over `tier_room.items()`** — dict insertion order — producing another dict that is only ever looked up by key. Safe. Likewise `ground`, `drawn`, `kept`, `kept_reach`, `rate_reach`, `rate_weight`, `counts`, `species_cells`, `cleared_out`, `widest`, `by_ground`: all keyed lookups, never iterated in a way that reaches output.

11. **`tier_cells` is a dict of `set`s, but only `len()` of each is published.** Set iteration order never reaches output. Safe.

12. **`settled_here` is a `set` used only for membership.** Safe.

13. **`max((clearance(p) for p in catalogue), default=0.)`** — a max over floats; ties are irrelevant since only the value is used.

14. **`close` is built in cell order and consumed by `any()`**, which short-circuits. The boolean is order-independent; the *cost* is not.

15. **`_blocked`'s row band is scanned `range(z-band, z+band+1)`** — ascending, and the same-cell list is scanned first. Only the short-circuited boolean escapes, so order is safe, but reproduce the two-phase structure anyway: the same-cell phase exists because 71% of blockers are found there.

16. **`layers` dict iteration in `habitat_cells`** (`{k: v[z][x] for k, v in l.items()}`) — builds a field map keyed by name; order never reaches output, but a derived key must overwrite a layer of the same name if one ever appears (today none of `medium/depth/dryness/coast/mountain/plains/wetland/cold` collides with a layer name).

17. **`sum()` vs explicit dot products.** The Python deliberately uses builtin `sum()` in `distance` and in `nearest_settled`'s dot product, with comments stating that CPython 3.12 sums floats with Neumaier compensation and that an explicit three-term dot differs by about one ULP and silently changes generated worlds on some seeds. Equally deliberately it uses **plain accumulation** for `rooms`/`tier_room`/`ground` and for `rate_reach`/`rate_weight`. A clean rewrite need match neither, but the split records which accumulations were considered numerically load-bearing.

## Edge cases

**No settled things at all** (`settled == []`). `nearest_settled` returns `[+inf] * len(cells)` immediately. Then: the hard clearance never bites (`inf < clear_needed` is false); `danger_ramp` gets `out = inf*inf = inf` so `near = reach/(reach+inf) = 0.0` — finite, not NaN — and the ramp degenerates to `FLOOR + (1-FLOOR)*danger`, i.e. 0.2 at tier 1 rising to 1.0 at tier 5, which is still purely redistributive because `budget` renormalises it. `rate_reach`/`rate_weight` are **guarded by `if settled:`** precisely because `inf * 0.0` is NaN and a NaN would land in the world document (`test_constraints_and_finite` runs `json.dumps(block, allow_nan=False)`). Both `rate_distance_m` and `placed_distance_m` come out `None` for every tier.

**`nest_density == 0` (or `nest_fantasy == 0` for monsters).** `density` is 0, so every `budget` value is 0 and every rate is 0. `total = 0.0`, `groups = int(0.0) = 0`, and `rng.random() < 0.0 - 0` is false. `sites == []`. But `shares` is still non-empty for every habitable cell (because `room` does not contain `density`), so **one RNG draw per habitable cell is still consumed**, and `species_cells`, `tier_cells` and `diagnostics` are still fully populated — a species reports `LOST_THE_DRAW` rather than an absence. `test_disabled_and_capped` asserts `sites == []` for both passes at `nest_density=0`, and only `beast_nests` empty at `nest_fantasy=0`.

**`salt_lake` cells are dead ground.** `habitat_cells` emits `medium='salt_lake'` for a `water_type == 2` cell with `salinity >= 0.2`, but `'salt_lake'` is not in `MEDIA` and no profile declares it, so `ground_candidates` returns an empty row for every biome there and `shares` is empty — the cell is `continue`d without an RNG draw. **Its area is nevertheless counted into `ground[True]`**, so salt lakes dilute the water budget and thin every water species slightly.

**Pole cells.** Nodes 0 and `len(cells)-1` are single nodes at `x = 0`, `z = 0` / `z = n-1`, with `direction` exactly `(0, ±1, 0)`. `sphere_grid` gives them the area of the **entire polar ring** (`band_wedge_area * (n-1)`), so their `room` and their rate are enormous — a pole cell can be drawing thousands of groups while holding at most one site per bucket. This is a large, silent contributor to the `drawn` vs `placed` gap that `saturation` reports.

**`_blocked` does not wrap across a pole.** The row band is a plain `range(z-band, z+band+1)`; rows outside `[0, n-1]` simply miss the dict. This is sound because the latitude-difference bound is a true lower bound on the great-circle angle in every case, and two cells in the same polar ring share a row and are therefore both scanned. Nothing tests the pole case on a generated world.

**Longitude seam.** `sphere_grid` points never include column `n-1`, so there is no duplicate cell and no seam double-count. Sites carry `x` in `0..n-2`.

**A species at the `nest_per_species` cap** is still drawn (`drawn[tier]` increments) and then `continue`d **before** the `settled_here` memo, so it is re-drawn and re-rejected for the rest of the pass. That is deliberate: the cap is not a territory refusal.

**`_pick` float fallback.** If accumulated float error leaves `draw > cursor` after the whole list, the last share is returned. Guard against an empty `shares` list — the caller already does with `if not shares: continue`.

**`nest_limit` interactions.** The cap runs after placement, rewrites `counts` but not `species_cells`, and runs *before* `saturation` is computed — so `saturation.placed` reflects the cap while `saturation.drawn` does not, and `refused = drawn - placed` silently includes cap removals. A species the cap empties reports `LOST_THE_DRAW`, indistinguishable from one that genuinely lost.

**`reach_reason` precedence.** `PLACED` beats everything; `NEVER_SATISFIABLE` beats `LOST_THE_DRAW`. In practice a never-satisfiable species always has `cells == 0` (its `requires` gate fails on an identically-zero layer), so the precedence is not exercised — but `test_a_dormant_school_requirement_is_the_world_independent_verdict` asserts that **every** dormant-requiring profile reports `NEVER_SATISFIABLE` and no other profile does.

**`cleared_out` does not test the medium directly** — it relies on `suitability` to do it. It is only computed for species with `species_cells == 0`, and only over cells with `clear_of < max(clearance)`.

**A `shore` profile is matched against `medium == 'land'`**, both in `ground_candidates` and again inside `suitability`, and then additionally requires `coast >= 0.1`. A shore species never matches a water cell.

**Missing fields fail closed.** `suitability` uses `f.get('temperature', 100)` — 100 °C is above every authored upper bound, so a world missing the temperature layer places nothing. `f.get(k, 0)` for a `requires` key means a missing layer fails every positive threshold. `biome_weight` returns `0.0` for an unlisted biome. `clamp(f.get(k, 0))` for a weight key means a missing layer contributes zero to the score, which usually drops it below the 0.3 floor but not always.

**`profiles()` raises rather than degrades** on any catalogue defect (see the algorithm's five validation rules). These are hard failures of world generation, not warnings.

**Empty catalogue after filtering** — impossible with the shipped file, but `max(..., default=0.)` and `{}` initialisations mean `_place` would return empty structures rather than throwing.

---
## Disambiguation: the words that mean several things

- **`tier`** in this module ALWAYS means **danger to a player, 1..5**. It appears on `Profile.tier`, `Site.tier`, `Diag.tier` and `Sat.tier`, and it is also the *monster bucket key*. It is **not** `key_locations.tier` (which is scale, not danger), and it is not a settlement tier. Where the docs say "a tier holds at most one lair per cell", "tier" is a danger band, which for monsters happens to coincide with the bucket.
- **`kind`** on a profile/site is the site archetype (`den`, `lair`, `aquatic territory`, `colony`, `roost`), not a block discriminator and not a key.
- **`camp`** appears only in prose ("a dragon's range may contain a goblin camp"). Nothing in this module is named `camp`, and the word collides with a nomad/encounter concept elsewhere — do not introduce it here.
- **`class`** is a literal JSON key on both profiles and sites, holding `"animal"` or `"monster"`. It is a C++ keyword; see `cpp_notes`.
- **`range_m`** is a territory **radius**; **`spacing_m`** is the authored **diameter**. `range_m = spacing_m * nest_spacing / 2`.
- **`river`** here is the binary hydrological river flag from `terrain_water`, not `rain_river` from `terrain_climate`. Other modules use the latter.
- **`real`** on a site means "is a real-world animal", i.e. `class == 'animal'`. It is not a validity flag.
- **`reach`** is used for three different things in one file: the candidate's territory radius in `_place`/`_blocked`, `span*span` inside `danger_ramp`, and "reachability" in the diagnostics sense. Rename them in the port.

---
## Pre- vs post-scaling for every distance in this module

| quantity | authored in | scaled by `reach_scale`? |
|---|---|---|
| `NEST_DANGER_SPAN_M` (300 m) | reference-world metres | **YES** — multiplied by `reach_scale(2*pi*radius)` at the one use site |
| `nest_settlement_clearance` (250 m default) | absolute metres | **NO** — used raw |
| `profile.spacing_m` (100..3500 m) | absolute metres | **NO** — used raw, halved by `nest_spacing/2` |
| `nearest`, `clear_by_node`, `rate_distance_m`, `placed_distance_m` | true physical metres on the sphere | n/a — measured, not authored |
| `pitch` | true physical metres | n/a |

So of the three authored distances, **only the ramp span scales with the world.** On a 200 km world the monster clearance tops out at 1250 m against a 3125 m meridional pitch at size 33, so the hard clearance refuses at most the cells immediately around a settlement, while the ramp span is 5382 m and spans two cells. That asymmetry is why the gradient had to move into the rate.

## Reasoning worth preserving

These are the comments that record measurements or failed approaches. They exist nowhere else and are the most expensive thing in the file.

**On the construction as a whole (module docstring):**
> "Both are thinned point processes over the ground rather than a fixed anchor budget. For a species `s` in cell `c` the intensity is `lambda = density(tier) * occurrence * area_km2 * suitability * biome_weight * danger_ramp` so the expected count is an integral over habitable land and doubling the land doubles the creatures, with no cap anywhere. A cell places `floor(lambda) + [u < frac(lambda)]` groups and picks the species in proportion to its share, which is **the marked-process construction built from the only primitives both implementations have: one uniform draw and a cumulative weighted pick.**"

> "Tier is danger to a player, one to five, and **it does the ecological work**: density falls geometrically with it, so prey are many and apex creatures are few, and territory and settlement clearance grow with it, and `danger_ramp` slides it outward from people."

> "The two passes never read each other. Animals hold hunting grounds; monsters hold territory over the same ground, and **a monster is excluded only by an equal, so a dragon's range may contain a goblin camp whichever of the two was drawn first.**"

> "Placement resolves at the raster cell, and **that is the ceiling on all of it**: every site in a cell carries the cell's own direction, so a tier holds at most one lair per cell and a species at most one den per cell whatever else the spacing rule does. `saturation` publishes the gap that leaves — cells reached, groups drawn, placed, refused — because **the pyramid is exact in the rate and clipped in what is placed, and a flat placed histogram is unreadable without both.**"

> "The rule does more than that, **though this module used to say it did not.** It claimed no territory was as wide as the raster pitch, so the test only ever compared a cell against itself; the grid is lat/lon, the rings beside the poles are packed a hundred times tighter than the equator, and a cross-cell refusal is ordinary ground there."

> "`rate_distance_m` is where the rate puts a tier and `placed_distance_m` is where the sites ended up; the first rises with tier because `danger_ramp` makes it, **the second barely moves because a saturated tier is pinned to the cells it reaches and every tier reaches nearly the same ground.**"

> "Both are statements about THIS world: eligibility moves with the seed, so a per-world count is a sample. **The one world-independent verdict is `never satisfiable`, and it is decided from the profile alone.**"

**Version-history comments, verbatim:**
> "2: `wildlife` gained `saturation`. The placed set alone cannot say whether a flat tier histogram is the rate or the raster clipping it, **and it was the raster.**"
> "3: `saturation` gained the two distance columns, `diagnostics` gained `cells` and `reason`, and the block gained `reachability`. All additive; no site moved."
> "3 [beast_nests]: `beast_nests` gained `saturation`, and **a lair is now refused by an equal lair only rather than by an equal or greater one. Every tier's placed count moves, and a consumer that inferred danger from the shape of the histogram was reading a different shape.**"

**On `NEST_DANGER_SPAN_M`:**
> "Authored against the 11.15 km reference like every other reach in this repo and scaled by circumference, **because an absolute metre constant is sub-cell on a 200 km world and ramps nothing.**"

**On `NEST_DANGER_FLOOR`:**
> "Nothing may become impossible: a floor of zero would make a tier five species unplaceable on the doorstep of a town and a tier one species unplaceable in the wilderness, and neither is true."

**On the five reasons:**
> "Five values, because 'absent' bundles one catalogue defect and one placement rule with two ordinary facts about a particular world, and **a count that does not separate them is the thing `key_locations` grew `diagnostics` to stop doing.**"

**On `CLEARED_OUT` (the fifth reason):**
> "A species whose habitat is the ground people stand on — `graves` exists where people bury people — can have all its eligible cells inside the settlement clearance, and the clearance refuses exactly them. **Measured at size 33: 57 profiles on seed 42, 32 on seed 7, 1 on seed 73, all of them graveyard and barrow creatures.** It swings that hard because it depends on how much grave ground a world puts outside a settlement's own cell. **Calling it 'conditions absent from this world' sends a reader to look at the terrain, where there is nothing to find.**"

**On `never_satisfiable`:**
> "This is the whole of what is decidable without generating worlds, and it is much less than the set of profiles a given world happens not to hold. **Measured across five seeds at size 33: 579 of 680 profiles are eligible on all five, 53 on some but not all, and 48 on none — and only 25 of that 48 are provable here. Calling the other 23 unreachable would be asserting a search nobody ran; `polar-bear` and `sea-otter` want ground those five worlds did not roll, not ground no world can carry.**"
> "A `requires` on a dormant school is the provable case because the threshold is a hard gate against a layer that is identically zero in every world the generator can build. A profile that merely *scores* on one is not proved unreachable, only handicapped."

**On `reachability`:**
> "'Ever reachable' is a search over seed, raster and every world option, not a check, and it was measured to be genuinely world-dependent rather than merely expensive: **53 of 680 profiles change eligibility across five seeds at one raster.** So the middle integer is this world's, `never_satisfiable` is the part that is proved, and **the gap between them is the part nobody has searched.**"

**On the monster `biome_weights` requirement:**
> "Monsters have no feeding role, so a monster without its own table falls through `biome_weight` to 1. and **is equally at home on a glacier and in a rainforest. That fallback was the whole of the monster half's opinion about biome until the 2026-09-20 ruling; refuse it rather than let it come back.**"

**On `biome_weight`:**
> "Every monster table names every live biome for the same reason — **an absent key is exclusion, not neutrality, so a five-key table silently forbids a quarter of the catalogue the ground it was measured on.**"

**On `distance` and on the dot product in `nearest_settled`:**
> "Keep sum(): CPython 3.12 sums floats with Neumaier compensation, so an explicit `a[0]*b[0]+a[1]*b[1]+a[2]*b[2]` is a different (less accurate) value by about one ULP and **silently changes generated worlds on some seeds.**"

**On `tier_density`:**
> "**The pyramid is a property of the tier, not of the species in it**: a tier is a niche with a share of the living space, and the species at that tier divide it. Authoring twenty more tier one animals makes each of them rarer rather than making the world thirty percent more crowded, **so the shape of the ecosystem survives the catalogue growing.**"
> "`occurrence` divides this share among the tier's members and nothing else. It is authored as a pure function of `size`, so spending it as an absolute multiplier taxed the big creatures a second time for being big: **tier five monsters average .26 against tier one's .68, which on top of the pyramid left them at a hundredth rather than a fortieth and emptied small worlds of anything worth fearing.**"

**On `danger_ramp`:**
> "The clearance below is a hard floor and stays one: nothing lairs in the town square. **But a cutoff is the wrong shape for a difficulty gradient and cannot make one at any resolution** — scaling `clearance * tier` up digs a wider hole around every settlement rather than ramping anything, and **measured on a 200 km world it left a tier five apex monster and a tier one prey animal at the same median 6.45 km from the nearest town against a source comment declaring a 3:1 intent.**"
> "**A ratio of squares rather than an exponential: no `pow`, no `exp`, nothing whose last bit depends on the libm the native build links against.** Squared because the plain ratio is too gentle to be worth having — the usable band on a settled world is about one span wide, and across it `span/(span+d)` swings by a factor of two however low the floor goes, **which is not a gradient a player would notice.**"
> "The tier's total is untouched. `budget` renormalises by `tier_room`, which sums the same factor, so this redistributes a tier over the ground and never changes how many of it a world holds."

**On `LATITUDE_SLACK`:**
> "1e-9 rad is 3e-5 m on the widest world this product ships, **ten orders of magnitude above the error it covers**, and being generous costs at most one extra candidate per cell."

**On `nearest_settled`:**
> "Float for float the same list as a `min` of `distance` over every settled site, **and for a reason rather than by measurement**: `distance` is `radius*acos(clamp(dot))`, `acos` is decreasing and `clamp` is not, so the smallest distance is the one at the LARGEST dot product."
> "The stopping rule is the one new piece of mathematics. The angle between two unit vectors is at least the difference of their latitudes, whatever their longitudes do."
> "**The slack is ADDED, so the walk goes one candidate too far rather than one too few.** A cell whose nearest settled thing is due north or south of it has a latitude gap exactly equal to its angle, and subtracting the slack stops the walk on the tie: **measured at size 33, that lost the argmax at 2 of 994 cells and moved `rate_distance_m` in the last three digits.**"
> "Cost is what changes. The old form was one `acos` per cell per settled site — **108 million of them on a size 513 world holding 410 settled things.**"

**On the bucket in `_place`:**
> "Scoping the scan to the bucket is also what keeps it affordable. Comparing a candidate against every site already placed is quadratic in the pass, and **this pass is the dominant cost of a generation**; comparing it only against its own bucket is the same predicate over a fifth (monsters, five tiers) or a three-hundredth (animals, one species) of the list."

**On putting clearance in the rate:**
> "Keeping clear of people is a property of the ground rather than of luck, so it belongs in the rate: **reject a draw that already happened and the world quietly holds far fewer creatures than the density asked for.**"

**On `ground_candidates`:**
> "Measured on the size 65 world: of the cell-by-species pairs this loop used to walk, **the biome gate refuses 44% of the animal ones and the medium gate a further 26%, and the medium gate alone refuses 67% of the monster ones** — every monster biome table names every live biome, so the monster half has no opinion about biome at all and all of its pruning here is medium."
> "**Order inside a list is catalogue order, unchanged. It has to be**: `total` below is a float sum over the list and `_pick` walks it cumulatively, so a reordering is a different world even when it is the same set."
> "Nothing is hoisted out of `suitability`, which still re-tests the medium it was selected on. **Keeping that call whole is worth more than the duplicated comparison: it stays the single published answer to 'can this species live here'.**"

**On budgeting land and water apart:**
> "Land and open water are two ecosystems sharing a planet, and a species belongs to exactly one of them... **Budget them apart or the sea decides how empty the land is — measured on one world, land was a quarter of the surface and held five per cent of the animals, half a group per square kilometre against a teeming ocean.**"

**On the world budget vs per-cell sharing:**
> "Sharing per cell instead loses a tier its share wherever no member qualifies, and the low monster tiers are the specialised ones — grave mites want a barrow, shipbreaker crabs want any sea at all. **Measured, that inverted the pyramid outright: 114 tier four lairs against 9 tier one. A budget cannot be forfeited by being fussy about where it is spent.**"

**On `rate_distance_m`:**
> "This is `danger_ramp` integrated over the world and **it is the only place the danger gradient is exact.** What is placed cannot show it... **Measured on the reference world: ablating the ramp entirely moves the spread of placed tier medians by 0.02 km.**"
> "Plain accumulation and a guard on `settled`: with nothing settled anywhere `clear_of` is infinite, and **an infinite times a zero rate is a NaN in the world document.**"

**On the `settled_here` memo:**
> "A species this cell has already REFUSED needs no second answer: the company only ever grows and the test is a disjunction over it, so the answer cannot come back the other way. A cell draws about ninety groups from a few dozen species, so it is asked the same question many times — **62% of animal draws and 47% of monster draws at size 65 repeat a (cell, species) pair.**"
> "A species this cell has already PLACED is deliberately NOT memoised, though it will be refused too. **Memoising it would mean asserting that a second site in the same cell is always refused, which needs `reach` to be positive: true of the shipped catalogue and of the option bounds, but an assumption**, and `_blocked` answers it from `at_node` in one dict hit regardless. **Both forms were timed at size 65 and neither is faster.**"

**On the Bernoulli:**
> "floor plus one Bernoulli draw: **an exact discretisation while the cell rate is below one, which it is almost everywhere**, and no new distribution is needed."

**On `den`:**
> "A den or lair is a place you can find and raid; a herd, roost or shoal is only a range where you meet them."

**On `nest_limit`:**
> "A safety valve, not the thing that decides how full a world is. When it has to bite it keeps the notable creatures: highest tier first, then by id."

**On the monster bucket (the retracted rule):**
> "Rank, not spacing, and an equal only. A greater monster excludes an equal and tolerates a lesser... **The rule used to read `a['tier']>=p['tier']`, which says the opposite in one direction: a goblin drawn after the dragon was refused by it, so a lesser lair nested inside a greater territory only when the draw happened to reach it first. Refusing by rank also refused the pyramid — a tier one candidate was held off by every lair on the world while a tier five was held off only by other tier fives.**" (The conformance record adds: on a world three times the reference area that put tier five **above** tier one.)

**On the animal bucket:**
> "Territorial within a species only: **a wolf range over a deer range is the point of the map, not a collision.**"

**On `_blocked`:**
> "Exactly `any(distance(a,cell) < max(reach, a.range_m))` over everything the bucket has placed, and **every comparison below IS that predicate on a real pair. What changes is which pairs are worth forming.**"
> "**Same cell first.** ...Measured at size 65: **71% of every comparison the flat scan made was a walk towards a blocker that turned out to be in the candidate's own cell, usually the one appended last, because a cell that has just placed something keeps drawing.** The test is still the real predicate rather than an assertion that reach is positive."
> "**One row is added to the bound, because it is exact in real arithmetic and a raster row is cheaper than an argument about the last bit of a divide.**"
> "This is NOT the claim that only the same cell can ever block... **2.4 m against a 391 m equatorial pitch at size 513 on a 200 km world, and 611 m at size 33** — and a cross-cell refusal is ordinary ground there rather than an edge case. **Measured at size 65: 426 of 9777 monster refusals and 50 of 47311 animal refusals have their blocker in another cell. The row band finds all of them; a same-cell test would place 476 sites the flat scan refuses.**"

**From `NEST-SAME-CELL-CLAIM-IS-FALSE.md` (backlog card, still open):**
> "The sentence is exactly the justification a reader needs to replace the territory scan with a `node in set` test, which is O(1) against the neighbourhood scan that is correct. **The performance pass reached for that fast path first and only found the gap by measuring the grid.**"
> Smallest neighbour gap vs equatorial pitch: 200 km/size 33 → **611.6 m vs 6250 m**; 200 km/size 129 → **38.3 m vs 1562.5 m**; 200 km/size 513 → **2.4 m vs 390.6 m**; 600 km/size 513 → **7.2 m vs 1171.9 m**.
> Cross-cell refusals: size 33 / 200 km → 0 of 63,183 animal, 0 of 11,580 monster; size 33 / 346 km → 0 of 212,657, 0 of 37,522; size 65 / 200 km → **50 of 47,311, 426 of 9,777**.
> "**Those are exactly the two worlds `Sim/tests/test_terrain_nests.py` builds.** ...Verified rather than assumed: `_blocked` was sabotaged to a same-cell-only rule and `test_animals_are_territorial_only_within_a_species` and `test_greater_monsters_hold_ground_against_equals_only` both still passed."
> "It does not establish how the cross-cell refusal count behaves above size 129. It was counted at 33 and 65 only. **It should rise with the raster, because the polar rings tighten while territory does not move, but that is an expectation and not a measurement.**"

**From the conformance record, on the pivot tier:**
> "`danger_ramp(3, d, span)` is `(1 + NEST_DANGER_FLOOR) / 2` at every distance, so tier three is pushed neither in nor out and sits wherever its habitat puts it. Its `rate_distance_m` is identical in every ablation arm, 5051 m for monsters and 5771 m for animals, while every other tier moves. **A five-tier ladder is therefore not something this mechanism can produce at any tuning on any world**, and that is a property of the shape rather than of the constants."

**From the conformance record, on habitat breadth:**
> "Habitat breadth per tier is the real ceiling on the placed histogram, which is why **one missing species can show up as a tier-wide hole. Tier three had four marine species, all of them gated on a reef or a ley school, so it reached 37 of 724 sea cells against 230 to 260 for every other tier — on a world three quarters water, that alone put tier three at 97 placed against tier four's 209.**"

## Notes for the C++ implementation

Read `include/fwg/grid.hpp`, `include/fwg/random.hpp`, `include/fwg/scale.hpp` and `include/fwg/world.hpp` before starting. This is stage 13 work and depends on stages 6–12 existing, so it cannot land until climate, biomes, ecology, ley networks and settlements do.

## Mapping onto what exists
- **`fwg::sphere_grid(n, radius)`** already gives exactly what `habitat_cells` needs: `point_x`, `point_z`, `areas` (with the polar ring multiplier), and `node_count()`. Node index ordering matches the Python `points` list exactly. Hold the `shared_ptr` — the cache evicts.
- **`fwg::direction(x, z, n)`** matches `terrain_globe.direction` including the `(0, ±1, 0)` pole special case.
- **`fwg::Grid`** carries the duplicate seam and pole rows; `habitat_cells` reads `layer.at(x, z)` for `x in 0..n-2`, so the seam column is never touched.
- **`fwg::reach_scale(circumference_m)`** exists and is the one function `NEST_DANGER_SPAN_M` needs.
- **`fwg::Rng::child(master, domain, variation)`** is the seed derivation to use. Do not add a fourth.
- **`fwg::World`** today holds `std::map<std::string, Grid> layers` plus typed structs. The two nest blocks want typed structs (`WildlifeBlock`, `BeastNestBlock`) hanging off `World`, plus the JSON writer emitting them under `"wildlife"` and `"beast_nests"`.

## Python idioms with no direct equivalent

**`f = {k: v[z][x] for k, v in layers.items()}` per cell is a dict allocation per node.** Do not port it literally — at n=513 that is 262k string-keyed maps. Instead resolve, once per pass, a fixed vector of layer pointers for every field name any profile mentions, interned to small integer ids, and build a `std::vector<double> fields` indexed by that id. `suitability` and `biome_weight` then index by int. Keep the eight derived fields at fixed indices. **Preserve the semantics that a missing layer reads as 0.0 and `temperature` reads as 100.0.**

**`p['weights']` is an ordered JSON object.** `std::map<std::string,double>` reorders. Use a `std::vector<std::pair<int,double>>` (interned key id, weight) built in **file order**, because `sum(factors.values())` accumulates in that order. Same for `requires`.

**`biome_weights` keys are decimal strings.** Intern to `int` at load time into a fixed-size array indexed by biome id (0..17) with a sentinel for "absent", since absent means weight 0. Do not use a `map<string,double>` in the inner loop — `ground_candidates` is called per cell.

**`lru_cache(maxsize=1)`** on `_document`/`profiles`/`roles` → a function-local `static` holding a parsed catalogue, or better, load once into a `CreatureCatalogue` struct and pass it in. `fwg` has its own JSON *writer* but the header list shows no parser — **you will need a small JSON reader for the 406 KB profile document, or a build-time conversion of it into a generated C++ table.** That is a real, unbudgeted piece of work; the generated-table route avoids adding a parser to a zero-dependency library and makes the catalogue a compile-time constant, at the cost of making a catalogue edit a rebuild.

**`class` is a JSON key and a C++ keyword.** Name the struct member `creature_class` (or an enum `CreatureClass { Animal, Monster }`) and emit the literal key `"class"` in the writer.

**`role: None` for monsters** must serialise as JSON `null`, not be omitted — `wildlife` rows carry `role`, `beast_nests` rows omit the key entirely. Two different row structs, or one with a flag.

**`rate_distance_m` / `placed_distance_m` are `float | None`.** Use `std::optional<double>` and have the writer emit `null`.

**`bisect.bisect_left`** → `std::lower_bound`.

**`sorted(..., key=lambda i: (asin(y), i))`** → `std::sort` with an explicit comparator on the pair; `std::sort` is *not* stable, so the `i` tie-break must be written out, exactly as the Python does.

**`sorted(placed, key=lambda a: a['id'])`** is a **lexicographic byte comparison of the id string**. Use `std::sort` with `a.id < b.id` on `std::string` — that is the same ordering. Do not substitute a numeric node comparison. Same for the cap's `(-tier, id)` comparator.

**`dict.setdefault(...).append(...)`** → `std::unordered_map<Bucket, std::unordered_map<int, std::vector<Site>>>`. For monsters `Bucket` is `int` (tier 1..5) — use a flat 6-element array. For animals it is the species id — intern species to a dense index at load time and use a `std::vector` indexed by it, sized `catalogue.size()`. That removes all hashing from the hot loop.

**Storing `Site` by value in `at_node`/`on_row`/`placed` means three copies.** Store `Site` once in `placed` (a `std::deque` or a `std::vector` reserved generously so pointers stay valid) and keep indices in `at_node`/`on_row`. Note the `nest_limit` cap reorders and truncates `placed` *after* the loop, so index stability only has to hold during the loop.

**`set` of node ids per tier (`tier_cells`)** — only `len()` is published. Use a `std::vector<uint8_t>` bitmap per tier over node indices plus a counter; that is O(1) and removes a hash set from the inner loop.

**`float('inf')`** → `std::numeric_limits<double>::infinity()`. Keep the `if (settled.empty())` early return and the `if (!settled.empty())` guard on the rate-distance accumulation — `inf * 0.0` is NaN and the JSON writer must never emit one.

**`int(total)`** truncates toward zero; `total >= 0` always here, so `static_cast<int>` is correct, but `total` can exceed `INT_MAX` on a pole cell of a huge world — use `long long` or clamp.

## Data structures
```cpp
struct CreatureProfile {
    std::string id, name;
    CreatureClass klass;     int tier;               // 1..5 danger
    Family family; Size size; Medium medium; Kind kind;
    double temp_lo, temp_hi;
    std::vector<std::pair<int,double>> weights;      // (field id, weight), FILE ORDER
    std::vector<std::pair<int,double>> requires_;    // (field id, threshold), FILE ORDER
    double weight_total;                             // sum in file order, precomputed
    double spacing_m, occurrence;
    std::array<double, kBiomeCount> biome_weights;   // 0 = absent/excluded
    int role_index;                                  // -1 for monsters
    bool never_satisfiable;                          // precomputed from requires_
};
struct HabitatCell {
    int node, x, z, biome; double area_km2; Vec3 dir;
    std::vector<double> fields;    // dense, interned field ids
    Medium medium;                 // cached from fields
};
```

## Ordering and float
Numeric output need not match, but **structural output must**: `sites` order (lexicographic id), `diagnostics` order (catalogue order), `saturation` order (tier ascending), and above all the *placement* order — cells in node order, shares in catalogue order — because those decide which sites exist at all. The `sum()`-vs-plain-accumulation distinction in the Python is a CPython-compensation artefact and can be ignored; use plain accumulation everywhere.

## Cost
This pass is the dominant cost of a generation in Python. The three optimisations that make it affordable are all structural and must be ported: the `(medium, biome)` candidate memo, the bucket-scoped refusal scan, and the latitude-walk in `nearest_settled`. A naive port will be fine at n=33 and unusable at n=513.

## What is not needed
No process pool. `ICARUS_CITY_WORKERS` does not apply — both passes are strictly serial and nothing about them was ever parallelised. If you parallelise the first (`room`) loop, note that it is genuinely independent per cell; the second (placement) loop is **not** — it is inherently sequential because each placement changes what the next candidate is refused by.

## Open questions

1. **`graves` does not exist.** Three comments (the `CLEARED_OUT` constant, `test_nest_diagnostics`'s docstring, the conformance record) explain the fifth reason by saying "`graves` is a field that exists where people bury people". There is no `graves` layer and no profile weights or requires on it. The graveyard/barrow creatures (`grave-mites`, `grave-worms`, `grave-candles`, `barrow-rats`, `barrow-wight`, `grave-leviathans`, …) are actually gated on `requires: {ley_umbral: 0.02–0.025}` and weight on `moisture` + `ley_umbral`. The mechanism and its measurements (57 profiles on seed 42, 32 on seed 7, 1 on seed 73) are real; the stated cause is stale prose. **Implement the mechanism; do not go looking for a `graves` field.** Whether `ley_umbral` genuinely concentrates on settled ground, and therefore whether the explanation is even approximately right, is not recorded anywhere and nobody appears to have checked.

2. **`salt_lake` is dead ground with a live area.** `habitat_cells` invents a fifth medium that `MEDIA` does not contain and no profile can declare, so a saline lake cell holds nothing — yet its `area_km2` is summed into `ground[True]`, inflating the water budget's numerator and thinning every marine and freshwater species proportionally. No comment, no test, no ticket. **Decide deliberately**: either exclude `salt_lake` area from `ground`, or add a `salt_lake` medium to the catalogue, or keep the current behaviour and say so. Porting it silently reproduces an effect nobody chose.

3. **`depth` is read two incompatible ways.** As a `requires` key it is raw metres (`depth: 30` = 30 m down, and 8 marine profiles use it that way, correctly). As a `weights` key it goes through `clamp()`, so `the-watching-deep`'s `depth: 1` weight evaluates to 1.0 at any depth over one metre — a constant, not a preference. One field, two readings, undocumented. Probably an authoring slip in that one profile rather than a design.

4. **Only one of three authored distances scales with the world.** `NEST_DANGER_SPAN_M` is multiplied by `reach_scale`; `nest_settlement_clearance` and `profile.spacing_m` are not. On a 200 km world at size 33 the widest monster clearance (1250 m) is 40% of the meridional pitch (3125 m), so the hard clearance can only ever refuse the settlement's own cell and its immediate neighbours, while territory (up to 1750 m at default spacing, 7000 m at the 4.0 the control allows) is also sub-pitch almost everywhere. No comment says whether that is intended; the `NEST_DANGER_SPAN_M` comment argues forcefully that an unscaled metre constant "is sub-cell on a 200 km world and ramps nothing", which is an equally strong argument against leaving the other two unscaled. **Flag before porting.**

5. **The `nest_limit` cap corrupts two diagnostics.** It runs after placement and rewrites `counts` but not `species_cells`, so a species the cap empties reports `'lost the draw to incumbents'` — indistinguishable from one that genuinely lost. And it runs before `saturation`, so `saturation.placed` reflects the cap while `saturation.drawn` does not, making `refused = drawn - placed` silently include cap removals that no territory rule refused. The conformance record acknowledges the first ("does not establish that a species removed by the cap is distinguished"). The second is not acknowledged anywhere. The cap is 0 by default, so neither bites in shipped worlds.

6. **`saturation` and `sites` disagree after a divine visitation.** `terrain_visitation` purges lairs from `beast_nests['sites']` in place and does not revisit `saturation`. The `limits` string says so. A consumer summing `saturation.placed` and comparing to `len(sites)` is wrong on any world where a visitation has run.

7. **`_blocked`'s row band does not wrap across a pole**, and the argument that it need not (latitude difference is a true lower bound on great-circle angle, and same-ring cells share a row) is sound but is stated nowhere and tested nowhere. Worth writing down in the port.

8. **Nothing exercises a cross-cell refusal on a generated world.** `NEST-SAME-CELL-CLAIM-IS-FALSE.md` item 2 is still open: the two worlds the suite builds are both size 33, where cross-cell refusals are 0 of 63,183 and 0 of 11,580. `TerritoryAcrossCellsTests` units the predicate directly, which is why the sabotage experiment caught it — but a world-level regression would not be caught. The card judges this a test-budget decision (about thirty seconds of nest placement for a size 65 world), not an oversight.

9. **The retracted `>=` rule survives in one string.** `beast_nests.limits` still contains "no species' territory is as wide as the raster pitch", the exact claim the board card exists to retract. The module header and the conformance record were both corrected; this string was not. Port it verbatim (it is published data) or fix it — but do not implement it.

10. **23 profiles are absent from every sampled world without being provably unreachable.** Whether they are authoring faults or ground five seeds did not roll is `BESTIARY-AUTHORING-CHECK-SEES-ONE-SEED`, open. The five-seed sample is all the evidence there is.

11. **Native divergence is knowingly untracked.** Per decision 027, the existing `Core/nests.cpp` carries the retired `other.tier >= profile.tier` monster rule under a comment describing the current one, and that is explicitly not treated as a defect. If any of that code is carried into FWGC++, **the `>=` is wrong** — the rule is `bucket(p) == bucket(a)`, an equal only.

## Addenda from the audit

The reader above wrote the specification; a second reader went back to the source
to find what it had left out or got wrong. Both are kept: the omissions are
content the implementer needs, and the corrections say where the specification
above is not to be trusted.

### Corrections

- **Claimed:** "`reachability` on the reference world is 680 / 561 / 453 authored/eligible/placed with 25 provable."
  **Actually:** No block ever reports authored = 680. reachability(catalogue, ...) is called with the class-filtered catalogue, so wildlife.reachability.authored == 311 and beast_nests.reachability.authored == 369; likewise wildlife.reachability.never_satisfiable == 0 (I confirmed all 25 never-satisfiable profiles are monsters — the dormant requires are 10 on ley_void, 10 on ley_blood, 5 on ley_eldritch, every one of them a monster) and beast_nests.reachability.never_satisfiable == 25. The 680/561/453 triple is the sum of the two blocks, consistent with the spec's own per-pass breakdown (animals 286 placed + 1 lost = 287 eligible; monsters 167 + 107 = 274 eligible; 287+274 = 561; 286+167 = 453). As written, a porter validating their port would expect one block to report the whole catalogue and would conclude the port was broken. The spec's own WRITES section states authored = len(catalogue) correctly, so this is an internal contradiction.

- **Claimed:** Of the retracted "no species' territory is as wide as the raster pitch" claim: "It survives in this string only; the module header and the conformance record were corrected."
  **Actually:** It survives in the Python source in at least one more place, uncorrected: the inline comment introducing saturation inside _place reads 'A tier can hold at most one site per bucket per cell, because every site in a cell carries the cell's own direction and no species' reach is as wide as the raster pitch at any width this product ships -- so a tier whose budget exceeds `cells` is pinned there and stays pinned however much ground the world gains.' Same false justification, same words, guarding the same data. The module header is also only half-corrected: lines 23-25 still assert 'a tier holds at most one lair per cell and a species at most one den per cell whatever else the spacing rule does' before lines 30-34 retract the reasoning behind it. The conclusion happens to be true — same-cell distance is 0 and the minimum possible reach is 100 * 0.25 / 2 = 12.5 m > 0 — but the stated reason is false, and it is the reason, not the conclusion, that tempts a porter into the O(1) 'node in set' fast path that _blocked deliberately does not use.

- **Claimed:** "both are exported to the native catalogue table by `tools/export_catalogues.py` (a generated mirror channel — a catalogue edit is not landed until it is regenerated)", said of result['wildlife'] and result['beast_nests'].
  **Actually:** export_catalogues.py exports neither output block. It imports 'profiles as nest_profiles, roles as nest_roles' from this module and writes the INPUT catalogue — "nest_profiles": [_plain(profile) for profile in nest_profiles()] (all 680 records) and "nest_roles": _plain(nest_roles()) (all 21 role tables) — into Contracts/catalogues/native-catalogues-v1.json, alongside civilization profiles, entities, heritage and city packs, under schema 'fantasy-world-generator.native-catalogues', version 1, with a content_sha256 over the sorted body. The generator's per-world output blocks are never touched by it. This matters twice: the 'regenerate after a catalogue edit' rule applies to the authored profile document rather than to anything this module produces, and that JSON is the second surviving copy of the creature data the port needs — which the spec, by mislabelling it, points nobody at.

### Missing — blocking

**The creature catalogue itself, and any instruction to rescue it before deletion**

The spec's data_files section gives the schema of Sim/icarus_sim/terrain_nest_profiles.json (416,212 bytes) plus aggregate statistics, two example profiles (aardvark, absence-titans) and one example role table (ambush_predator). It contains none of the actual weights, requires, biome_weights, spacing_m, occurrence or temperature values for the other 678 profiles, and none of the other 20 role biome tables; none of that is derivable from any statistic in the spec. The spec must state as a prerequisite: copy Sim/icarus_sim/terrain_nest_profiles.json verbatim into FWGC++ (it is the whole subject matter of this module); a second, already-flattened copy exists at Contracts/catalogues/native-catalogues-v1.json under the keys 'nest_profiles' (a _plain() pass over profiles()) and 'nest_roles' (a _plain() pass over roles()), written by tools/export_catalogues.py; and also copy Sim/icarus_sim/biomes.json, whose natural_biomes roster is exactly the 13 entries 0 ocean, 1 tundra (tombstoned), 2 desert, 3 grassland, 4 forest, 5 exposed_rock, 6 snow (tombstoned), 7 rainforest, 8 lake, 13 marsh, 15 boreal_forest, 16 cold_tundra, 17 land_ice — ids 9-12 and 14 do not exist, so no biome id outside that set can ever reach biome_weight.

### Missing — minor

**The BEAST_NESTS_VERSION 4 history line**

The source carries four version-history comments; the spec reproduces three and drops the fourth, so it never explains why beast_nests is at 4 while wildlife is at 3. Verbatim: '4: the same additive reporting as `wildlife` 3.' That is: version 3 of beast_nests was the behavioural change (gained saturation, and a lair became refused by an equal lair only rather than by an equal or greater one), and version 4 is purely the additive reporting that took wildlife to 3 — the two saturation distance columns, diagnostics gaining cells and reason, and the block gaining reachability. No site moved at 4.

**Why all 369 monster biome tables have identical key sets**

biome_weight's docstring gives the authoring provenance the spec summarises away: 'an animal resolves its table through its feeding role; a monster carries its own, authored from a family template with per-species deltas, and profiles() refuses a monster that has none.' That is why the eleven-key set is uniform across the whole monster half rather than a coincidence a porter might break by hand-editing one profile. The same docstring also records that the decimal-string keys were chosen 'matching how civilization biome preferences are authored' — the key encoding is a cross-module convention, not local taste.

**The module's public surface beyond the three entry points**

profiles, roles, suitability, biome_weight and distance are imported by code outside the module: tools/export_catalogues.py imports 'profiles as nest_profiles, roles as nest_roles', and Sim/tests/test_terrain_nests.py imports 'profiles, roles, suitability, biome_weight' and calls suitability with hand-built field maps as sparse as {'medium':'land','temperature':12}. So suitability must tolerate a field map containing almost nothing — which bears directly on the C++ note recommending dense interned field vectors. Relatedly, the factors / {'excluded': ...} second return value the spec documents is discarded by both in-module call sites (score,_=suitability(...) and suitability(...)[0]); it exists for external callers and diagnostics, not for the placement path.

**Entry-point docstrings stating each pass's one-line intent**

add_animals: 'Hunting grounds. Prey are many and small-ranged, predators few and wide.' add_monsters: 'Territory. A greater monster excludes an equal, and tolerates a lesser.' add_nests: 'Both passes, animals first. They never read each other.' _pick: 'The species that claims this group, in proportion to its share of the cell.' habitat_cells: 'Every cell's habitat description, in grid order: the ground both passes read.' settled_directions: 'Everything already standing on the surface, for clearance tests.'

**The count of what actually differs between the two passes**

The spec says the passes 'differ only in five inputs' and then lists six. The real count is seven, and they are the seven arguments of _place(kind_of, catalogue, result, cfg, cells, settled, radius, domain, density, falloff, clearance, bucket) that vary: kind_of (the id prefix, 'animal' vs 'nest'), catalogue (class filter), domain ('animals-v1' vs 'monsters-v1'), density, falloff, clearance and bucket — plus which block is written and whether a roles key and a role diagnostics column are emitted. Note also that result is passed to _place and never read by it; habitat_cells is the only thing that touches result['layers'].
