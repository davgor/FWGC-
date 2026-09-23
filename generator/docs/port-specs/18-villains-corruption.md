# Port specification: 18-villains-corruption

Extracted from the Python `FantasyWorldGenerator` at commit `b67c99b` before that
repository was retired, then audited against the same source by a second reader.

**Source modules**

- `Sim/icarus_sim/terrain_villains.py`
- `Sim/icarus_sim/terrain_corruption.py`
- `Sim/icarus_sim/terrain_visitation.py`

**Estimated C++ size:** roughly 2600 lines, excluding tests.

## Audit: NOT YET IMPLEMENTABLE

Part A (villains) and Part C (corruption) are genuinely implementable — I walked `advance`, `promote`, `regions`, `fallen_mark`, `causes`, `build`/`sink_well`/`claim_settlements`, the whole corruption request/cleanse path and `_drain` against the source and found no point where I would have to guess a constant, an initialisation, a tie-break or a loop bound; the ordering hazards, the shallow-copy semantics, the FRAGMENT_SHARE-overwrites-the-ledger detail, the stranded-anchor visit order, the two distinct seed derivations and the `born or age` trap are all correctly and precisely carried, and the hidden_pantheon.json transcription is exact against the file. Part D (visitation) is not. `divine_lottery` is the heart of the subsystem and the spec stops at the boundary of `terrain_ruins`: it gives the divine cause's weight but not the ley cause's eight `LEY_DESTRUCTION` reason strings, not the nest causes' `'dragon'`/`'monster'` kind or their reason string, and none of `source_school`'s resolution ladder or `NEST_FAMILY_SCHOOL` — so I cannot produce a ruin's `cause`, `reason` or `legacy.school`, and `new_node_school` decides where the per-ruin key point is planted, which changes the ley field and therefore every stage the rebuild runs. The visitation record's `moon` sub-document is given as `{...}` with only one of its nine fields named. And `depart_god`'s restore pseudocode omits the per-item network lookup, which a competent programmer would implement exactly as written and thereby leave every suppressed rival school permanently halved — the silent-wrong-build case, not the blocked case. Three smaller confident errors (the `fantasy_nest_threats` `or`-vs-`.get` and the missing `tier` default of 5, the `end_year` default, REFERENCE_RADIUS_M described as load-bearing) and one real behaviour nobody recorded (god_outer_unnamed's weave/umbral affinity makes `suppress_opposed` halve the god's own avatar cluster) round it out. The reasoning-preservation work is the strongest part of the document and I found no summarised-away comment of consequence; the gaps are all at the module boundary the author chose not to cross.

**4 blocking gap(s)** were found and are recorded in the addenda below.

## Purpose

Three coupled subsystems that give a finished world antagonists and the machinery for a game to escalate them.

**Villains** (`terrain_villains.py`, 722 lines) turn recorded regional turmoil into named super-villains. Unlike every other figure the generator produces — all of which are points named after events that already happened — a villain is a *field*: a reach in metres with linear falloff, like a ley network. It causes wars (it presses neighbouring cities together) and city fates (it enters the destruction lottery in its own name), sinks a leyline well that re-routes ley tracks, and stakes a mixed-population claim on ground near its seat. Tier is reach and is continuous, so "super villain" is a band on a curve, not a flag: one that has just crossed the line is a threat that is *growing*, which is the entire pacing model — the peace after a player kills one comes from the successor being weak, not from a cooldown.

**Corruption** (`terrain_corruption.py`, 532 lines) is a stateless JSON request API by which one of four *hidden* gods seats a standing super-villain as its instrument. It is the only path in the entire codebase by which a hidden magic school ever gets a leyline node: generation locks hidden-school occurrence at zero and player leyline edits are restricted to the known eight. The gate is the game's own ledger of how many villains the player has met across playthroughs; below a god's noticing point the chance is exactly zero. *Which* god takes an interest is never rolled — it is read from the world, so the corruption fits the place it happens. The corrupted **node**, not the villain, is what persists; killing the holder leaves the nodes, and an unheld node is a spawner. The companion `cleanse_request` drains those nodes back down.

**Visitation** (`terrain_visitation.py`, 459 lines) is a second stateless JSON request API: the orchestrator summons a *known* god onto the mortal plane. An avatar ley cluster saturates the god's schools around a target, opposed schools are suppressed, every city within reach rolls the destruction lottery with the god itself as one more cause, rival monster nests are purged, faithful cities gain divine favour, surviving peoples convert, and when the god departs a footprint key point, the ruins, the mutated biomes and the converted faiths remain.

The three share one shape: a point on the sphere, a reach in metres, and linear falloff `1 - distance/reach` applied to everything inside it.

## Where it sits in the pipeline

**Must already exist before villains run** (the whole tail of the sixteen-stage ladder, up to and including stage 13):

- `layers` with the twelve `ley_<school>` grids, `biome`, `biome_variant`, `lunar_sensitivity`
- `effective_config.globe_radius` (physical metres)
- `settlements.sites` (each with `uid`, integer `id`, `node`, `x`, `z`, `direction`, `population_profile`, `population_estimate`, `city_class`)
- `humans.cultures` (road-linked city groups, version 9)
- `magic.networks` (twelve schools, version 4)
- `threat_assessments.cities` (version 3 — the forward-looking war outlook fields `war_risk`/`war_hunger` are what `turmoil()` reads)
- `beast_nests.sites`, `ruins`, `roads.routes`, `history.ages`
- for the two request APIs additionally: `astrology` v1, `lunar_almanac` v1, `religion` v1 with a matching `catalogue` identity, `water.nodes`

**Villain call sites, in order, inside `terrain_history.age_transition(result, cfg, age, final=)`:**

1. `add_nests` (marks `beast_nests.evaluated_age = age-1`)
2. `villains = advance(result, cfg, age, rise, hold, density)` — **first**, before wars and before any city fate. The comment is explicit about why: *"The cast advances on the assessment this age inherited, so a villain is always one rebuild behind the region it is eating, and can never be raised by its own damage."*
3. `resolve_wars(..., villains=villains)` — the standing cast feeds `terrain_wars._villain_pressure`
4. per surviving city, `city_fate(..., villains=villains)` → which calls `terrain_villains.causes(villains, city, radius)` and **extends** (never inserts into) the cause list
5. ley intensity drift, ruin key points, `depart_god` for any walking god
6. `rebuild_tail(result, cfg, survivors, 'age', age)`
7. if `final`: `promote(result, cfg, age, density)` — *placed before the works pass so a promoted villain sinks its well and stakes its claims in the same age it rises, exactly as an accumulated one does*
8. if `'villains' in result`: `works = build(result, cfg, age, standing(people))`; if `works` is non-empty, re-run `evaluate_networks` and `add_biome_variants` (because a well changed the ley geometry), write `villains.works`
9. `villains.outlook = outlook(result, cfg, rise, density)`

`terrain_time._villain_outlook` refreshes **only** `villains.outlook` on a sub-age tick. The conformance record `docs/conformance/time-advance.md` states the rule: *"The villain **cast** is read-only: a villain rises, holds or falls at an age boundary and nowhere else … Only `villains.outlook`, a reading of present ground, is refreshed."*

**Depends on villains afterwards:** `terrain_wars.contested_pairs` (villain contention pressure → war chance → `war_outlook` → `regional_threat` → the city planner's `defense_priority`, i.e. wall thickness); `terrain_history.add_threat_assessments`; `key_locations.core.world.villain_holdings`; `hero_generator` (reads `growth` and picks the matching escalation of an archetype card — *"the sim owns this field … so the two agree without either package importing the other"*); `npc_roster`; `story_web`; `terrain_liveness` (delegates to `is_standing` rather than restating the rule); `terrain_person_api`.

**Corruption** requires a standing super-villain, so it runs only on a finished world, through `corruption_request`/`cleanse_request`. **Visitation** runs on a finished world through `visitation_request`. Both are stateless: same body in, same world out, caller's world never mutated.

**Cross-dependency:** `terrain_corruption` imports `state_keys` and `_snapshot` from `terrain_visitation` — *"One snapshot body, several callers: a third copy of this is exactly the duplication that had age_transition and _rebuild silently disagreeing before they were unified."* `cleanse_request` with a `god_id` target delegates to `terrain_visitation.depart_god`.

## Inputs

## terrain_villains

**World blocks**
- `result['humans']['cultures']` — list of `{'index': int, 'id': str, 'city_ids': [int], 'population_profile', 'civilization_id', 'architecture_style_id'}`. **`city_ids` are integer indices into `settlements.sites`, matching each site's integer `id` field, not uids.** A culture id looks like `human_heartland-672bcb6c` (profile + hex of `child_seed(seed, 'culture-<index>')`).
- `result['settlements']['sites']` — reads `uid` (str), `id` (int), `node` (int), `direction` (3-vector unit), `population_profile` (str).
- `result['threat_assessments']['cities']` — keyed by `city_uid`. Fields read: `regional_threat` (0..1), `war_risk` (0..1), `war_hunger` (0..1), `war_pressure`, `nest_pressure`, `ley_pressure`.
- `result['magic']['networks']` — `{school: {'nodes': [{'id','direction','intensity'}], 'edges': [{'from': int, 'to': int, 'path', 'id', 'intensity'}], ...}}`.
- `result['ruins']` — reads `id` and `evidence.villain_uid`.
- `result['effective_config']['globe_radius']` — **physical metres**, e.g. 31830.988618379066 for a recipe-3 seed-42 size-17 world (circumference exactly 200 km).
- `result['villains']` itself on every age after the first: `people`, `tiers`, `fallen`.

**Config**
- `cfg.settlement_spacing` — **physical metres, already scaled**. `terrain_area.apply_world_scale` multiplies `globe_radius, extent, amplitude, wavelength, depth, width, meander, radius, tectonic_relief, sea_level` by `world_scale`; `settlement_spacing` is **not** in that list, and recipe 3 derives it directly in metres. On the reference world (seed 42, size 17) it is **8072.500651872125 m** and `config['settlement_spacing'] == effective_config['settlement_spacing']`. Every reach in these three modules is `k * settlement_spacing` and is therefore already post-scaling; no further multiplication.

**World options** (from `terrain_world.options(cfg)`, definitions in `terrain_world.py:82-84`)
- `villain_rise` — default `0.5`, range `0.0 .. 1.0`. Share of a region's concentrated turmoil that becomes tier each age. Zero raises none and writes no block at all.
- `villain_hold` — default `0.7`, range `0.0 .. 1.0`. Tier a seated villain falls below to lose the world.
- `villain_density` — default `3.0`, range `1.0 .. 12.0`, units "regions". Cultural regions per permitted villain.

## terrain_corruption

- Everything `validate_age_world` checks (recipe 3, phase ≥ 13, size ≤ 1025, terrain v6, generator v16, recipe v3, magic v4, history v3, astrology v1, lunar_almanac v1, religion v1, settlements v16, civilizations v3, humans v9, catalogue and registry identities, terrain-detail identity, planner identities).
- `world['religion']` — `version` must equal 1, `catalogue` must equal `catalogue_identity()`, `facts` must be present and truthy (else `watching()` returns empty), `gods` (known + hidden records), `corruptions`.
- `world['villains']['people']` — needs at least one **standing** villain.
- `world['settlements']['sites']`, `world['ruins']`, `world['beast_nests']['sites']`, `world['magic']['networks']` — all read by `interest()`.
- Request body: `api_version` (must be `1`), `world`, `encounters` (int, `0 <= e < 2**32`, default 0), `variation` (int, same range, default 0), `villain_uid` (str or absent).
- Data file `hidden_pantheon.json` (see data_files).
- Cleanse body: `api_version`, `world`, `target` (exactly one of `{'corruption': int}` or `{'god_id': str}`), `power` (number, `0 < p <= 1`, default `0.25`).

## terrain_visitation

- Same `validate_age_world` gate, plus `astrology`, `lunar_almanac`, `religion` each at `version == 1`.
- `world['water']['nodes']` — list of `(x, z)` raster coordinates, one entry per distinct sphere cell; the target `node` is an index into it.
- `world['layers']['water_type'][z][x]` — must be `0` (dry land) at the target.
- `world['layers']['ley_<school>'][z][x]` for all twelve schools.
- `world['astrology']['moon']` — `periods` (`synodic`, `spin`, `nod`), `offsets`, `tilt_max_degrees`, `great_year_days`.
- `world['religion']['faiths'][<population_profile>]` — `gods` (list of ids), `patron` (id or None), `conversion`.
- `world['religion']['gods']` — `id`, `status`, `aspect`, `aspect_name`.
- `world['settlements']['founding']['end_year']` — for the default summons day.
- Request body: `api_version`, `world`, `god_id`, `target` (`{'city_uid': str}` or `{'node': int}`), `wrath` (number `0..1`, default `0.5`), `variation` (uint32, default 0), `depart` (bool, default False), `day` (int ≥ 0 or absent).
- Data file `pantheon.json` via `gods_by_id()`.

## Outputs

## `result['villains']` — written by `advance`, `promote`, `build`, `outlook`

```
villains = {
  'version': 2,                    # int, always 2 after advance/promote
  'people':  [ villain, ... ],     # living first (by region anchor), then fallen
  'tiers':   { anchor_id: float }, # rounded to 9 dp, key-sorted
  'outlook': { ... },              # see below
  'fallen':  [ mark, ... ],        # append-only, never pruned
  'works':   [ { 'villain_uid': str, 'well': bool, 'claim': bool } ],  # only if non-empty
}
```

**`villain` record — the key field is `uid`**, a string `f'villain-{age}-{seat_node}'` (e.g. `villain-2-115`). Fields:

| field | type | meaning |
|---|---|---|
| `uid` | str | identity; `villain-<born_age>-<seat node index>` |
| `region` | str | the **anchor**: a ley node id, or `'seat-' + seat_uid` where the world has no magic. This is the stable cross-age key. |
| `born_age` | int | age it rose or was promoted |
| `seat_uid` | str | city uid of its seat |
| `node` | int | sphere-cell index of the seat |
| `direction` | list[3] | unit vector of the seat, copied from the city |
| `school` | str or None | school of the ley network holding its anchor node |
| `held_nodes` | list[str] | ley node ids; starts as `[anchor]`, gains the well id, gains corrupt node ids |
| `tier` | float | the field strength; ≥ `SUPER_TIER` while super |
| `reach_m` | float | `tier * 4.0 * settlement_spacing`, **physical metres** |
| `growth` | str | one of `devouring`, `spreading`, `hoarding`, `usurping` |
| `status` | str | `'living'` or `'fallen'`; **absent reads as living** |
| `fell_age` | int | present only when fallen |
| `log` | list | `[{'age', 'event', ...}]`, events `rose`, `promoted`, `fell`, `sank a well`, `corrupted`, `cleansed` |
| `god` | str | god id, written by `build` (school god) or `corrupt` (hidden god) |
| `corrupted` | bool | written by corruption; cleared to `False` on cleanse |
| `well` | dict | `{'node', 'school', 'god', 'lines': [edge ids], 'age'}` |
| `claims` | list | see below |

**`claim` record — the key field is `id`**, `f'claim-{villain_uid}'` (exactly one constant id per villain, ever).
`{'id', 'villain_uid', 'age', 'direction': [3], 'node': int, 'kind': 'seat'|'village', 'refreshed_age': int, 'factions': sorted unique population_profiles, 'drawn_from': [3 city uids], 'well': node id or None, 'holder_status': 'living'|'fallen', 'influence': float}`. `advance` also stamps `holder_fell_age` at the fall.

**`fallen` mark — the key field is `id`**, `f'fallen-{villain_uid}'`, with `kind: 'fallen_villains'`. Carries deep copies of `MARK_KEYS = ('uid','region','born_age','seat_uid','node','direction','school','growth','held_nodes','claims','well','god','log')` plus `fell_age`, `born_age`, `reigned_ages = max(0, age - born_age)`, `tier_at_fall` (9 dp), and `left = {'ruins': sorted ids, 'claims': sorted ids, 'well': node id or None, 'held_nodes': sorted}`. **Deliberately no `tier` or `reach_m`** — *"both describe a grip it no longer has"* — and **deliberately no `asset_id`**, unlike a ruin: *"a fallen villain is a record about ground that other records already hold … Giving it an asset would add an identity the catalogue must carry for nothing to stand at."*

**`outlook`**: `{'rise': float, 'ceiling': int, 'standing': int, 'regions': [row]}` where each row is `{'region': anchor, 'culture': culture id or None, 'concentration': 6dp, 'tier': 6dp, 'to_threshold': 6dp, 'seated': bool, 'stalled': bool, 'cities': int}`.

**Into other blocks:** `magic.networks[school]` gains a well node (`well-<uid>`, intensity 3.75) and well edges (`well-<uid>-line-<k>`, intensity 2.0). Ruins caused by a villain carry `evidence.villain_uid`, `evidence.growth`, `evidence.tier`, `evidence.distance_m`, `evidence.reach_m`, `evidence.school`.

## Corruption writes

- `result['religion']['corruptions']` — append-only list. **Key field is `index`** (its own position, `0`-based). `{'index', 'god_id', 'school', 'villain_uid', 'age', 'encounters', 'variation', 'nodes': [4 node ids], 'reach_m', 'touched': [...], 'ruins': [ruin ids], 'cleansed_age': int or None, 'order': [11 strings]}`
- `result['magic']['networks'][hidden_school]` — four nodes `corrupt-<god_id>-<index>-<k>` for k in 0..3, intensities `[4.0, 2.5, 2.5, 2.5]`.
- `result['ruins']` — for rot/void only. `id = 'ruin-' + city_uid`, `kind: 'ruins'`, `destroyed_age`, `asset_id: 'marker.city_ruins'`, `cause` ∈ {`rot_plague`, `void_unmade`}, `reason`, `evidence: {'god_id','school','share' (6dp),'corruption': index}`, `probability: 1.0`, `roll: 0.0`, `new_node_school`, `legacy`, `corruption: index`.
- On the host villain: `god`, `school`, `corrupted: True`, extended `held_nodes`, log entry.
- `result['religion']['gods']` — via `_reveal`.
- `result['history']['operations']` — `{'api_version': 1, 'kind': 'corruption', 'corruption', 'god_id', 'villain_uid', 'encounters', 'variation'}`.
- `result['corruption']` — either `{'acted': False, 'encounters', 'watching': [ids], 'reason'}` or `{'acted': True, 'encounters', 'god_id', 'villain_uid', 'corruption'}`.
- `result['corruption_api_version'] = 1`, `result['timing_ms']['corruption_total']` (3 dp ms).
- On a city inside the reach: blood sets `population_estimate`; eldritch sets `taken_by` = god id. **Both are erased by the rebuild — see open_questions.**
- Cleanse writes `result['cleanse']` = the report, and `record['cleansed_age']`.

## Visitation writes

- `result['religion']['visitations']` — append-only. **Key field is `index`.** `{'index','god_id','aspect','node','direction': [3],'age','day','wrath','variation','schools': [str],'reach_m','cluster': [node ids],'restore': [...],'ruins': [ids],'purged_nests': [ids],'favored': [{'city_uid','strength'}],'converted': [profiles],'departed_age': int or None,'moon': {...},'order': [11 strings]}`
- `result['magic']['networks'][s]` for each `s` in `schools` — five nodes `avatar-<god_id>-<index>-<school>-<k>`, intensities `[4.0, 3.0, 3.0, 3.0, 3.0]`. Opposed schools' node and edge intensities scaled down in place. Each ruin adds `<ruin id>-key` at the legacy school with the legacy intensity. Departure adds `footprint-<god_id>-<index>` at intensity 3.5.
- `result['religion']['gods'][…]` — `status: 'walking'`, `avatar: {'node','since_age','day','visitation','cluster'}`; on departure `status: 'manifest'` and `avatar` removed.
- `result['religion']['sites']` — `{'id': f'theophany-{index}-{god_id}', 'kind': 'theophany', 'god_id', 'node', 'visitation'}`; on departure `kind` becomes `'pilgrimage'`.
- `result['religion']['faiths'][civ]['conversion'] = {'god_id', 'visitation'}`.
- `result['threat_assessments']['cities'][…]` — `divine_favor` added, `regional_threat` reduced by `0.2 * strength` and re-rounded to 6 dp.
- `result['beast_nests']['sites']` — purged nests removed.
- `result['build_stages']`, `result['phases']['titles']`, `result['phases']['completed']` via `_snapshot`.
- `result['visitation_api_version'] = 1`, `result['timing_ms']['visitation_total']` (unrounded ms).

## Algorithm

════════════════════════════════════════════════════════════════
PART A — VILLAINS
════════════════════════════════════════════════════════════════

── A0. Pure helpers ──────────────────────────────────────────

`great_circle(radius, a, b)` — used verbatim in six places, inline every time:
```
dot   = a.x*b.x + a.y*b.y + a.z*b.z
angle = acos(clamp(dot, -1.0, 1.0))
return radius * angle
```

`is_standing(v)`:  `(v.status or 'living') == 'living'`. **An absent `status` reads as living**, because *"a record written before the fall was recorded at all described someone who stood."*
`standing(people)`: filter preserving order.

`turmoil(report) -> [0,1]`:
```
standing_threat = report.regional_threat or 0
risk            = report.war_risk        or 0
hunger          = report.war_hunger      or 0
return clamp(standing_threat + 0.3*risk + 0.3*hunger, 0.0, 1.0)
```
Forward-looking terms are included deliberately: *"a region about to fight is concentrating as surely as one that already has."*

`concentration(region, reports_by_uid) -> [0,1]`: arithmetic **mean** of `turmoil(report)` over the region's cities that have a report; `0.0` when none. The mean, not the sum, is *"what separates 'one bad city' from 'a region coming apart'."*

`dominant_pressure(reports) -> 'war'|'nest'|'ley'|None`:
```
totals = {}
for report in reports:                       # iteration order = argument order
    totals['war'] += report.war_pressure  or 0
    totals['nest'] += report.nest_pressure or 0
    totals['ley'] += report.ley_pressure  or 0
if all three are 0 (or the report list is empty): return None
return max(sorted(totals.keys()), key=lambda k: totals[k])
```
`max` over a **sorted** key list: ties resolve to the first key in *alphabetical* order among the tied maxima — i.e. `ley` < `nest` < `war` and Python's `max` keeps the first maximum encountered, so `ley` beats `nest` beats `war` on an exact tie.

`ceiling(count, density) -> int`:
```
if count == 0: return 0
return max(1, ceil(count / max(density, 1.0)))
```
Checked cases: `ceiling(0,3)=0`, `ceiling(1,3)=1`, `ceiling(9,3)=3`, `ceiling(10,3)=4`, `ceiling(2,12)=1`.

`claim_influence(n) -> float`:
```
if n < 0: return 1.0                       # guard, not a decay value
value = 0.6 * pow(0.6, n)
return 0.0 if value < 0.05 else round(value, 9)
```
Table from the source comment: `n=0 → .60, 1 → .36, 2 → .216, 3 → .130, 4 → .078, 5 → 0`. Zero rather than a vanishing fraction *"so a consumer scaling by this stops being nudged by claims too old to matter instead of being nudged imperceptibly forever."*

── A1. `regions(result, radius=None, reports_by_uid=None)` ───

Produces the cultural regions each with a stable anchor. **This is the single most delicate function in the module.**

```
cultures = result.humans.cultures                     (may be absent → [])
by_uid   = { site.uid : site for site in settlements.sites if 'uid' in site }
by_site  = { site.id  : site for site in settlements.sites if 'id'  in site }   # id is an INT

groups = []
for culture in sorted(cultures, key = lambda c: str(c.id)):      # ORDERED: string sort of culture id
    cities = [ by_site[i] for i in culture.city_ids if i in by_site ]
    if cities: groups.append({'id': str(culture.id), 'cities': cities})

if groups is empty and by_uid is non-empty:
    groups = [ {'id': 'world', 'cities': sorted(by_uid.values(), key=lambda c: c.uid)} ]

if radius is None: return groups                       # shape-only mode, no anchors
```

Then for each group, in that order:
```
seat = argmin over group.cities of ( -turmoil(reports_by_uid.get(city.uid, {})), city.uid )
       # i.e. MOST TROUBLED city, ties broken by ascending uid string
if seat is None: skip group
(school, node_id) = held_node(result, seat, radius)
group.seat = seat; group.school = school; group.node_id = node_id
group.anchor = node_id if node_id else ('seat-' + seat.uid)
out.append(group)
```

Then de-duplicate anchors. *"Two regions can share a nearest node; the troubled one keeps it, so an anchor names one region and a tier cannot be double-counted into it."*
```
best = {}                                              # insertion-ordered map
for group in out:
    cur = best.get(group.anchor)
    if cur is None or concentration(group,…) > concentration(cur,…):
        best[group.anchor] = group
return [ best[k] for k in sorted(best.keys()) ]         # FINAL ORDER: anchor id ascending
```
Note the strict `>`: on an exact concentration tie the **earlier** group (by sorted culture id) keeps the anchor.

`held_node(result, seat, radius) -> (school, node_id) or (None, None)`:
```
best = None
for (school, net) in sorted(result.magic.networks.items()):     # school name ascending
    for node in net.nodes:                                      # list order
        if node.id starts with 'well-': continue                # WELL_NODE_PREFIX
        d = great_circle(radius, seat.direction, node.direction)
        if best is None or d < best.distance: best = (d, school, node.id)
return best or (None, None)
```
Strict `<` — on an exact distance tie the first school/node in that iteration wins.

**Why the anchor is a ley node and not the culture id** (from the docstring): a culture's id *"is rebuilt every age from whatever the roads join that age, so it cannot carry anything across one: accumulating tier against it silently resets the region every time."* Ley node ids are stable and only ever appended.

**Why wells are excluded** (the `WELL_NODE_PREFIX` comment, quoted in full in reasoning_to_preserve): a villain's own well is at its own seat, so it is always the nearest node; leaving it in re-anchors the region onto the holder's own work, `advance` then looks the villain up by the old anchor and misses, and *"the reign is stranded on the very age it began — deterministically, for every villain bound to a school god."*

── A2. `advance(result, cfg, age, rise, hold, density) -> standing cast`

```
if rise <= 0.0 and 'villains' not in result:
    return []                                # a real off switch: no block written at all
block = result.setdefault('villains', {'version':2,'people':[],'tiers':{},'outlook':[],'fallen':[]})
block.setdefault('fallen', [])               # migration for pre-fallen-list worlds
tiers  = copy of (block.tiers or {})
living = { v.region : shallow_copy(v) for v in standing(block.people) }
fallen = [ shallow_copy(v) for v in block.people if not is_standing(v) ]
```

**Decay the dead's claims** (restamped here so leaf packages need not import this module):
```
for old in fallen:
    if old.fell_age is None: continue
    for claim in old.claims:
        claim.influence = claim_influence(age - int(old.fell_age))
```

```
marks           = []
reports_by_uid  = { r.city_uid : r for r in result.threat_assessments.cities }
radius          = result.effective_config.globe_radius
spacing         = cfg.settlement_spacing
region_list     = regions(result, radius, reports_by_uid)          # anchor-sorted
by_anchor       = { r.anchor : r for r in region_list }
limit           = ceiling(len(region_list), density)
seated_count    = count of living villains whose tier >= 1.0
stranded        = [ rid for rid in sorted(living.keys()) if rid not in by_anchor ]
```

**The main loop**, over `[r.anchor for r in region_list] + stranded` — produced regions first (anchor-ascending), stranded anchors afterwards (anchor-ascending). The stranded are visited *after* deliberately: *"a stranded reign ending must not free a seat in the age it ends."*

```
for rid in that sequence:
    region  = by_anchor.get(rid)                 # None ⇒ stranded
    pressed = concentration(region, reports_by_uid) if region else 0.0
    tier    = tiers.get(rid, 0.0) * 0.9 + rise * pressed        # TIER_KEPT_PER_AGE
    villain = living.get(rid)

    if tier >= 1.0 and villain is None and seated_count >= limit:
        tier = min(tier, 0.999)                  # STALLED_TIER: the ceiling stops a rise,
                                                 # not an existence. The LEDGER is clamped too,
                                                 # so a pretender cannot bank past the line.
    tiers[rid] = tier

    if villain is None:
        if tier >= 1.0 and region is not None:   # region None ⇒ stranded ⇒ already holds one
            seat, school, node_id = region.seat, region.school, region.node_id
            pressure = dominant_pressure([ reports_by_uid[c.uid] for c in region.cities
                                            if c.uid in reports_by_uid ])
            living[rid] = {
              uid:        'villain-{age}-{seat.node}',
              region:     rid,  born_age: age,
              seat_uid:   seat.uid,  node: seat.node,  direction: list(seat.direction),
              school:     school,
              held_nodes: [node_id] if node_id else [],
              tier:       tier,
              reach_m:    tier * 4.0 * spacing,
              growth:     GROWTH_BY_PRESSURE.get(pressure, 'usurping'),
              status:     'living',
              log:        [ {age: age, event: 'rose', concentration: round(pressed,6)} ],
            }
            seated_count += 1
    else:
        villain.tier    = tier
        villain.reach_m = tier * 4.0 * spacing   # REFRESHED BEFORE the fall test, so a fallen
                                                 # villain carries a live-looking reach
        if tier < hold:
            villain.status  = 'fallen'
            villain.fell_age = age
            villain.log.append({age, event:'fell', concentration: round(pressed,6)})
            tiers[rid] = tier * 0.35             # FRAGMENT_SHARE overrides the line above
            for claim in villain.claims:
                claim.holder_status   = 'fallen'
                claim.holder_fell_age = age
                claim.influence       = claim_influence(0)      # = 0.6
            mark = fallen_mark(result, villain, age)             # BEFORE it leaves `living`
            if mark is not None: marks.append(mark)
            fallen.append(living.pop(rid))
            seated_count -= 1
        else:
            living[rid] = villain
```

**Write-back:**
```
block.version = 2
block.tiers   = { k: round(v,9) for k in sorted(tiers.keys()) }     # key-sorted map
block.fallen  = (block.fallen or []) + sorted(marks, key = m.id)    # append-only
block.people  = [ living[k] for k in sorted(living.keys()) ]        # anchor-ascending
              + sorted(fallen, key = (v.fell_age if present else -1, v.uid))
return standing(block.people)
```
The return is the **standing cast, not the block**: *"this return flows into resolve_wars, whose `_villain_pressure` reads `reach_m` with no status test of its own, and a fallen villain still carries the reach it had when it fell."*

**Hysteresis.** `hold` (default .7) is strictly below `SUPER_TIER` (1.0), so the band to stay is below the band to rise and nothing flickers across the line.

**Subtlety worth implementing exactly:** `seated_count` counts only villains with `tier >= 1.0`. A living villain whose tier has decayed into `[hold, 1.0)` still holds its region but is **no longer counted against the ceiling**, so a decaying reign frees a slot for a new seating while still standing.

── A3. `fallen_mark(result, villain, age)` ───────────────────

```
left = marks_left(result, villain)
if not (left.ruins or left.claims or left.well): return None     # held_nodes alone is NOT enough
mark = { k: deepcopy(villain[k]) for k in MARK_KEYS if k in villain }
born = villain.born_age;  born = age if born is None else int(born)   # NOT `or age` — age 0 is falsy
mark.id            = 'fallen-' + villain.uid
mark.kind          = 'fallen_villains'
mark.fell_age      = age
mark.born_age      = born
mark.reigned_ages  = max(0, age - born)
mark.tier_at_fall  = round(float(villain.tier or 0.0), 9)
mark.left          = left
```

`marks_left(result, villain)`:
```
ruined = [ r.id for r in (result.ruins or []) if (r.evidence or {}).villain_uid == villain.uid ]
return { 'ruins':  sorted(ruined),
         'claims': sorted(c.id for c in (villain.claims or [])),
         'well':   (villain.well or {}).node,
         'held_nodes': sorted(villain.held_nodes or []) }
```
Ruins are read back out of the world *"rather than accumulated on the villain, for the same reason the region is anchored to a ley node: anything hung on a live record is lost the moment that record is rebuilt."*

── A4. `causes(villains, city, radius) -> list of (kind, weight, reason, evidence)`

Called from `terrain_history.city_fate`; the results are **appended**, never inserted, because the selector walk downstream is order-sensitive.

```
out = []
for v in standing(villains):                # order = the cast's order
    if v.tier < 1.0 or not v.reach_m: continue      # restated, not relied on: a fallen villain
                                                    # reaching this lottery would move every world
    distance = great_circle(radius, city.direction, v.direction)
    spent    = 1.5 * v.reach_m                      # FALLOFF
    if distance >= spent or city.uid == v.seat_uid: continue    # THE SEAT ITSELF IS IMMUNE
    weight = 0.5 * v.tier * (1.0 - distance/spent)  # VILLAIN_WEIGHT
    if weight <= 0.0: continue
    out.append((
      'villain_' + v.growth,
      weight,
      f"A power seated at {v.seat_uid} reached this far and the city did not stand.",
      { villain_uid: v.uid, growth: v.growth, tier: round(v.tier,6),
        distance_m: round(distance,3), reach_m: round(v.reach_m,3), school: v.school }
    ))
```
Cause kinds emitted: `villain_devouring`, `villain_spreading`, `villain_hoarding`, `villain_usurping`. Downstream `terrain_ruins.source_school` matches `cause.startswith('villain')` and takes `evidence['school']` as the scar school; `None` falls through to the region's dominant school and then the culture's.

── A5. `outlook(result, cfg, rise, density)` ─────────────────

```
reports_by_uid = { r.city_uid : r for r in threat_assessments.cities }
block   = result.villains or {}
tiers   = block.tiers or {}
seated  = { v.region for v in standing(block.people) }        # a SET
regions = regions(result, globe_radius, reports_by_uid)
limit   = ceiling(len(regions), density)

rows = [ for each region in regions (anchor-ascending):
   { region: r.anchor, culture: r.id,
     concentration: round(concentration(r, reports),6),
     tier: round(tiers.get(r.anchor, 0.0), 6),
     to_threshold: round(max(0.0, 1.0 - tier), 6),
     seated: r.anchor in seated,
     stalled: (r.anchor not in seated) and tier >= 0.999,
     cities: len(r.cities) } ]

for rid in sorted( seated - { r.anchor for r in regions } ):   # STRANDED ANCHORS, sorted
   rows.append({ region: rid, culture: None, concentration: 0.0,
                 tier: round(tiers.get(rid,0.0),6),
                 to_threshold: round(max(0.0, 1.0 - tier),6),
                 seated: True, stalled: False, cities: 0 })

return { rise: rise, ceiling: limit, standing: len(seated), regions: rows }
```
The stranded rows are version 2's fix for one document giving two answers: *"a villain standing on an anchor no region was produced for made one document give two answers — `standing: 1` beside zero regions seated — with no way for a reader to tell which was right."* The invariant that must hold: `standing == count of rows with seated == True`.

**This is not a rare path.** In the reference world (seed 42, size 17, `villain_rise: 1.0`) the final outlook has 9 produced regions and 1 stranded row — anchor `earth-node-4`, `culture: null`, `cities: 0`, `seated: true` — because `advance` ran at age 2 on the pre-transition world and the rebuild afterwards appended ruin key points that moved that culture's nearest node to `earth-node-1`.

── A6. `promote(result, cfg, age, density)` ──────────────────

The arrival. `advance` cannot seat anyone in a generated world: *"a world only ever runs two age transitions inside a generation, so organic seating needs a `villain_rise` above 0.834536 at seed 42 size 17 — above most of the legal range, and unreachable at the default."*

```
block = result.villains
if block is None: return []            # villain_rise:0 stays a real off switch
reports_by_uid, radius, spacing as before
tiers  = copy of block.tiers
living = { v.region : v for v in standing(block.people) }     # REFERENCES, not copies
regions = regions(result, radius, reports_by_uid)
limit  = ceiling(len(regions), density)
room   = limit - count(v in living.values() where v.tier >= 1.0)
if room <= 0: return []

ranked = sorted( [r for r in regions if r.anchor not in living],
                 key = ( -concentration(r, reports), r.anchor ) )
promoted = []
for region in ranked[:room]:
    ... build the villain record exactly as `advance` does, EXCEPT:
        tier    = 1.0          (SUPER_TIER exactly — the bottom of the band)
        reach_m = 1.0 * 4.0 * spacing
        log     = [ {age, event: 'promoted', concentration: round(pressed,6)} ]
    living[region.anchor] = villain
    tiers[region.anchor]  = 1.0
    promoted.append(villain)

if not promoted: return []             # nothing written at all
block.version = 2
block.tiers   = { k: round(v,9) for k in sorted(tiers) }
fallen        = [ shallow_copy(v) for v in block.people if not is_standing(v) ]
block.people  = [living[k] for k in sorted(living)] + sorted(fallen, key=(fell_age or -1, uid))
return promoted
```

*"Deliberately NOT an accumulation. A promoted villain enters at exactly `SUPER_TIER`, the bottom of the band, because a villain that has just crossed the line is a threat that is growing rather than one that has arrived… The tier it is given is the tier it would have to hold."*

**Idempotent by construction** — a region already holding a standing villain is skipped and the ceiling counts who is already seated — *"because the hook fires on the final age of a generation AND on the final age of an `advance_age_request`, and a caller may advance a world that was already promoted."*

`promote` does **not** decay fallen claims and does **not** append to `block.fallen`.

── A7. `build(result, cfg, age, villains)` — what a seat puts on the ground

```
built = []
for v in standing(villains):
    if v.tier < 1.0: continue
    if not v.god and v.school:
        v.god = school_god(v.school)
    well  = sink_well(result, cfg, v, age)
    claim = claim_settlements(result, cfg, v, age)
    if well or claim:
        built.append({ villain_uid: v.uid, well: bool(well), claim: bool(claim) })
return built
```

`school_god(school)` — first god in `pantheon.json`'s `gods` list with `family == 'school'` and `school` in its `school_affinity`; `None` if none. List order is the tie-break.

`sink_well(result, cfg, villain, age) -> the well dict, or None`

*"A key point is a source; a well is a source that is **joined**. The edges are the point: they change what the network carries between the villain's holdings, which a lone node cannot do."*
```
school = villain.school
if not school or school not in networks or not villain.god: return None
net     = deepcopy(networks[school])
well_id = 'well-' + villain.uid
if any node in net.nodes has that id: return None          # ONE WELL PER VILLAIN, EVER
direction = list(villain.direction)                        # the seat's direction
net.nodes.append({ id: well_id, direction: direction, intensity: 3.75 })
index = len(net.nodes) - 1
held  = set(villain.held_nodes)
joined = []
for (position, node) in enumerate(net.nodes[:-1]):         # everything but the new well
    if node.id not in held: continue
    (a, b, tangent, angle) = arc_frame(direction, node.direction)
    path = [ [ a[i]*cos(angle*k/32) + tangent[i]*sin(angle*k/32) for i in 0..2 ]
             for k in 0..32 ]                              # 33 points, endpoints exact
    edge_id = f'{well_id}-line-{len(joined)}'
    net.edges.append({ from: index, to: position, path: path,
                       id: edge_id, intensity: 2.0 })
    joined.append(edge_id)
networks[school] = net
villain.held_nodes = sorted( set(villain.held_nodes) | {well_id} )
villain.well = { node: well_id, school: school, god: villain.god, lines: joined, age: age }
villain.log.append({ age, event: 'sank a well', school: school, lines: len(joined) })
```
where `arc_frame(a, b)`:
```
cosine  = clamp(dot(a,b), -1, 1)
angle   = acos(cosine)
tangent = ( (b[i] - cosine*a[i]) / max(1e-12, sin(angle)) for i in 0..2 )
return (a, b, tangent, angle)
```
*"Edges index their endpoints by position, so they are appended with the node and never inserted."* `sink_well` bypasses `edit_network` and appends directly, so it performs **no** unit-vector or intensity validation.

`claim_settlements(result, cfg, villain, age) -> the claim, or None`
```
near = sorted( (great_circle(radius, villain.direction, c.direction), c.uid, c)
               for c in settlements.sites ),
       key = (distance, uid)
mix = [ c.population_profile for (_,_,c) in near[:3] ]        # MIXED_FACTIONS
if not mix: return None
claim = { id: 'claim-' + villain.uid, villain_uid: villain.uid, age: age,
          direction: list(villain.direction), node: villain.node,
          kind: claim_kind(villain),  refreshed_age: age,
          factions: sorted(set(mix)), drawn_from: [uid for (_,uid,_) in near[:3]],
          well: (villain.well or {}).node,
          holder_status: 'living', influence: 1.0 }
villain.claims = villain.claims or []
held = first existing claim in villain.claims with the same id
if held is None:
    villain.claims.append(claim);  return claim
held.kind          = claim.kind        # ONLY these two are refreshed
held.refreshed_age = age
return held
```
`claim_kind(v)` = `'seat'` if `v.tier >= 1.5` (`SEAT_TIER = SUPER_TIER * 1.5`) else `'village'`.

*"Mixed by construction: a villain takes whoever is close, not whoever shares its blood, which is what makes its holdings look wrong to everyone."*

The split between frozen and standing halves is argued at length; the rejected alternative (redraw `factions` and `drawn_from` every age) is recorded in reasoning_to_preserve.

════════════════════════════════════════════════════════════════
PART B — SHARED SPHERICAL HELPERS (duplicated verbatim)
════════════════════════════════════════════════════════════════

`_angle`, `_unit`, `_offset` exist **character-for-character identically** in `terrain_corruption` and `terrain_visitation` (only the declaration order differs — corruption declares `_angle` then `_unit`; visitation `_unit` then `_angle` — and only visitation's `_offset` carries a docstring). Implement once in C++.

```
_angle(a, b)  = acos(clamp(sum(a[i]*b[i]), -1.0, 1.0))
_unit(v)      = v / sqrt(sum(v[i]*v[i]))                # NO zero guard

_offset(up, angle, bearing) -> unit vector `angle` radians from `up` along `bearing`:
    reference = (1,0,0) if abs(up.y) > 0.999 else (0,1,0)     # pole guard
    dot       = reference · up
    north     = _unit( reference - dot*up )                   # Gram-Schmidt
    east      = _unit( north × up )                           # cross in THIS order
    tangent   = cos(bearing)*east + sin(bearing)*north
    return _unit( cos(angle)*up + sin(angle)*tangent )
```
The cross product as written: `(n.y*u.z - n.z*u.y, n.z*u.x - n.x*u.z, n.x*u.y - n.y*u.x)`.

════════════════════════════════════════════════════════════════
PART C — CORRUPTION
════════════════════════════════════════════════════════════════

── C1. `interest(kind, world) -> [0,1]` — read, never rolled ──

```
cities = world.settlements.sites
'living':     people = Σ (city.population_estimate or 0)
              return min(1.0, people/200000.0) if people else 0.0
'decay':      ruins = len(world.ruins)
              return min(1.0, ruins / max(1, len(cities) + ruins))
'aberrant':   wrong = count of beast_nests.sites with family in ('aberrant','undead')
              return min(1.0, wrong/24.0) if wrong else 0.0
'dead_magic': live = count of magic.networks entries whose name is NOT a hidden school
                     and whose `nodes` list is non-empty
              return max(0.0, 1.0 - live/8.0)
otherwise:    raise ValueError('Unknown interest kind ' + kind)
```

── C2. `chance(god, encounters) -> [0,1]` ────────────────────

```
noticing = god.noticing ; climb = god.climb
if encounters < noticing:  return 0.0          # EXACTLY zero, not merely small
if climb <= noticing:      return 1.0          # degenerate authored data
return clamp( (encounters - noticing) / float(climb - noticing), 0.0, 1.0 )
```
*"Zero and not merely small: a guaranteed-clean early playthrough is what makes the first corruption read as something the player caused."*

── C3. `watching(world, encounters) -> ranked list of hidden god records`

```
facts = world.religion.facts
if not facts: return []
manifest = frozenset( g.id for g in world.religion.gods
                       if g.status in ('manifest','walking') )
rows = []
for god in hidden_catalogue().gods:                     # FILE ORDER
    if chance(god, encounters) <= 0.0: continue
    if not rule_holds(god.exists, facts, manifest): continue
    rows.append( (interest(god.interest, world), god.id, god) )
return [ god for (_,_,god) in sorted(rows, key = (-interest, id)) ]
```
*"Most interested first, ties by id: a draw between two gods is decided by the world and then by name, never by a roll."*

`rule_holds` is a small recursive predicate over the authored `exists` tree; the only kinds the hidden four use are `always`, `magic_enabled`, `any`, `nest_family` (`any(f in facts.families for f in rule.families)`) and `ruins` (`facts.ruins >= rule.min`).

── C4. `validate_corruption(body)` ───────────────────────────

In this exact order:
1. body must be a dict → `wrong_type('request', …)`
2. unknown fields outside `{api_version, world, encounters, variation, villain_uid}` → `unknown_field(sorted(extra)[0], sorted(allowed), noun='request field')`
3. `type(api_version) is int and == 1` → else `unsupported_api`
4. `cfg = validate_age_world(world)`
5. `religion` must be a dict with `version == 1` → else `missing_block('religion', …)`
6. `religion.catalogue == catalogue_identity()` → else `missing_block('religion.catalogue', …)`
7. `encounters`: `type(...) is int` (bool is rejected by `type is`, not `isinstance`), `0 <= e < 2**32`
8. `variation`: identical checks
9. `villain_uid`: `None` or `str`
10. `people = standing(world.villains.people)`; empty → `refused_by_world('world.villains', None, 'Corruption needs a standing super villain …')`
11. if `villain_uid` given and not among those uids → `unknown_field(villain_uid, [uids], noun='standing villain')`
12. `held = Σ len(c.nodes) for c in religion.corruptions if not c.cleansed_age`; `held >= 96` → `over_capacity('religion.corruptions', held, 96, 'per-world corrupted node')`

── C5. `_host(world, villain_uid)` ───────────────────────────

```
candidates = standing(world.villains.people)
people     = [ p for p in candidates if not p.corrupted ]
if villain_uid is not None:
    return the first p in candidates with p.uid == villain_uid, else None
return max(people, key = (p.tier, p.uid)) if people else None
```
Tie-break on equal tier is the **lexicographically greatest uid** (`max` over a tuple). *"Already taken by a hidden god is the only disqualification. Being sworn to an old one is not: a villain bound to a school god is precisely who a hidden god reaches for, and excluding them made every seated villain ineligible the moment they started sinking wells."* Note that an explicit `villain_uid` bypasses the `corrupted` filter.

── C6. `_cluster(result, cfg, god, host, rng, index) -> [4 node ids]`

```
centre = host.direction
angle  = 0.4 * cfg.settlement_spacing / globe_radius      # RING_SPACINGS, radians
base   = rng.uniform(0, 2π)                               # ONE draw, the ring's bearing
placements = [ (centre, 4.0) ]                            # SEAT_INTENSITY
           + [ (_offset(centre, angle, base + k*2π/3), 2.5) for k in 0,1,2 ]
school = god.school
net    = result.magic.networks[school]
for k, (point, strength) in enumerate(placements):
    node_id = f'corrupt-{god.id}-{index}-{k}'
    net = edit_network(net, new_node={id: node_id, direction: list(point), intensity: strength})
result.magic.networks[school] = net
return the four ids in order
```
`edit_network` is copy-on-write with validation: unique non-empty string id, finite 3-vector, `|len²-1| <= 1e-6`, intensity finite and in `[0, 4]`.

── C7. `_failure_mode(result, god, host, reach, age)` — "what the four do to a city"

```
survivors, touched, ruined = [], [], []
for city in result.settlements.sites:            # LIVE dicts, mutated in place
    distance = great_circle(radius, city.direction, host.direction)
    if distance > reach: survivors.append(city); continue
    share = 1.0 - distance/reach                 # linear falloff
    if school == 'blood':                        # Farmed: the city stands, its people do not
        before = city.population_estimate or 0.0
        city.population_estimate = round(before * (1.0 - 0.45*share), 6)   # FARM_SHARE
        touched.append({uid, effect:'farmed',
                        people_lost: round(before - city.population_estimate, 6)})
        survivors.append(city)
    elif school == 'eldritch':                   # Taken, not destroyed. Nothing visibly
        city['taken_by'] = god.id                # changes, which is the horror.
        touched.append({uid, effect:'taken'})
        survivors.append(city)
    else:                                        # rot and void both END the city
        cause = 'rot_plague' if school=='rot' else 'void_unmade'
        ruined.append((city, cause, share))
return survivors, touched, ruined
```
`age` is accepted and never used.

── C8. `corruption_legacy(city, cause, potencies, school)` ───

```
legacy = ruin_legacy(city, cause, potencies)     # cause is rot_plague / void_unmade
legacy.school = school                           # the god IS the source
legacy.basis  = 'source'
```
`ruin_legacy` for these causes finds no source school (the cause matches none of its prefixes), so it falls to the region's `dominant_school` and then the culture's — both overwritten here. The **intensity** survives: `CLASS_INTENSITY = {'small':1.5,'medium':2.5,'capital':3.5}` keyed on `city.city_class`, defaulting to `small`; the `DIVINE_INTENSITY = 4.0` branch does **not** fire because these causes do not start with `divine`.

── C9. `corrupt(result, cfg, god, host, encounters, variation, index, age)`

```
rng   = Random( child_seed(cfg.seed, f'corruption-{index}-{god.id}', variation) )
nodes = _cluster(result, cfg, god, host, rng, index)
reach = max( float(host.reach_m or 0.0), 2.5 * cfg.settlement_spacing )   # REACH_SPACINGS floor
survivors, touched, ruined = _failure_mode(result, god, host, reach, age)

for (city, cause, share) in ruined:
    potencies = city_potencies(city, result.layers)       # no lunar sway
    legacy    = corruption_legacy(city, cause, potencies, god.school)
    record    = { k: deepcopy(city[k]) for k in RUIN_KEYS if present }
    record.update( id='ruin-'+city.uid, kind='ruins', destroyed_age=age,
                   asset_id='marker.city_ruins', cause=cause,
                   reason=f"{god.name}: {god.smite}.",
                   evidence={god_id, school, share: round(share,6), corruption: index},
                   probability=1.0, roll=0.0, new_node_school=god.school,
                   legacy=legacy, corruption=index )
    result.ruins.append(record)

drop result['city_plans','hamlet_plans','castle_plans','world_scene']
rebuild_tail(result, cfg, survivors, 'corruption', age, religion=False)

host.god        = god.id
host.school     = god.school                    # the host's school BECOMES a hidden school
host.corrupted  = True
host.held_nodes = sorted( set(host.held_nodes) | set(nodes) )
host.log.append({ age, event:'corrupted', god: god.id, encounters })
for person in result.villains.people:
    if person.uid == host.uid: person.update(host)

record = { index, god_id, school, villain_uid, age, encounters, variation,
           nodes, reach_m: reach, touched, ruins: [ids], cleansed_age: None, order: ORDER }
result.religion.corruptions.append(record)
add_religion(result, cfg)                        # AFTER the record, so it is carried forward
_reveal(result, god, host, age)                  # AFTER add_religion, so it is not overwritten
result.history.operations.append({api_version:1, kind:'corruption', corruption:index,
                                  god_id, villain_uid, encounters, variation})
```

`RUIN_KEYS = ('uid','name','node','x','z','direction','height_m','population_profile','civilization_id','city_class','source_culture','founded_age','war_history')`.

── C10. `_reveal(result, god, host, age)` ────────────────────

*"Before a return there is no trace of the hidden four anywhere; afterwards the known pantheon gains it as a rival and its aspects are pushed Wild, which is what makes the cure able to become worse than the disease: a Wild god destroys four times as much."* (Wild doubles `ASPECT_FACTOR`, Sovereign halves it, so Wild is 4× Sovereign in the divine lottery.)
```
rivals = set( hidden_gods_by_id()[god.id].rivals )
for record in result.religion.gods:
    if record.id == god.id:
        record.status      = 'walking'
        record.aspect      = 'wild'
        record.aspect_name = hidden_gods_by_id()[god.id].aspects['wild']
        record.evidence    = { villain_uid: host.uid, since_age: age }
    elif record.id in rivals and record.status in ('manifest','sleeping','walking'):
        known = gods_by_id().get(record.id)                # the KNOWN catalogue
        if known and known.aspects.get('wild'):
            record.aspect      = 'wild'
            record.aspect_name = known.aspects['wild']
        record.roused_by = (record.roused_by or []) + [god.id]
```

── C11. `corruption_request(body)` ───────────────────────────

```
start = now()
cfg, encounters, variation, villain_uid = validate_corruption(body)
result = adopt_world(body.world)               # deep copy; build_stages shared, not copied;
                                               # timing_ms guaranteed present
previous_layers = deepcopy(result.layers)
previous_state  = { k: deepcopy(result.get(k)) for k in state_keys() }
age        = len(result.history.ages)
index      = len(result.religion.corruptions)
candidates = watching(result, encounters)
host       = _host(result, villain_uid)
acted      = None
if candidates and host is not None:
    god  = candidates[0]                       # the MOST INTERESTED, never a roll
    roll = Random( child_seed(cfg.seed, f'corruption-{index}-{god.id}-escalation',
                              encounters) ).random()
    if roll < chance(god, encounters):
        acted = corrupt(result, cfg, god, host, encounters, variation, index, age)
        _snapshot(result, previous_layers, previous_state,
                  f'Corruption {index+1}: {god.name}', 'corruption')
result.corruption_api_version = 1
result.timing_ms['corruption_total'] = round((now()-start)*1000, 3)
result.corruption = acted ? {acted:True, encounters, god_id, villain_uid, corruption:index}
                          : {acted:False, encounters, watching:[ids],
                             reason: candidates ? 'a watching god did not act this time'
                                                : 'no hidden god is watching yet'}
```
Note the **two distinct seed derivations**: the escalation roll uses `variation = encounters`; the cluster/ruin stream uses `variation = body.variation`.

── C12. `validate_cleanse(body)` ─────────────────────────────

Same preamble, then: `target` a dict with exactly one key from `{corruption, god_id}` else `cross_field`; `power` a number (int or float), finite, `0 < power <= 1` else `out_of_range`.

`{'corruption': i}` — `i` an int, `0 <= i < len(records)` else `refused_by_world`; `records[i].cleansed_age is not None` → already cleansed, refuse.

`{'god_id': g}` — the god must exist in `religion.gods` else `unknown_field`; `status` must be `'walking'` else refuse; and there must be a visitation record with `god_id == g` and `departed_age is None`, else refuse with *"A god revealed by corruption is opposed through its corruption rather than sent home; target `{"corruption": <index>}` instead."* The reason is recorded in the source: *"Two different things set `status` to 'walking', and only one of them writes a visitation record… `depart_god` requires the record, so a god revealed by corruption would reach an unguarded `next()` and raise a bare StopIteration out of a validated API."* Note the use of `.get` throughout: *"a validator whose job is to turn malformed input into a ValueError must not itself raise KeyError on a partial record."*

── C13. `_drain(result, cfg, index, power, age)` ─────────────

```
record  = result.religion.corruptions[index]
school  = record.school
net     = result.magic.networks[school]
drain   = power * 4.0                            # CLEANSE_MAX_DRAIN: power 1.0 ends a
                                                 # full-intensity node in one tick
targets = set(record.nodes)
# guard (see open_questions — it is dead in practice)
if any edge e in net.edges where isinstance(e.from, str) and (e.from in targets or e.to in targets):
    raise ValueError('A corrupted node carries an edge; removal would misindex this network')

remaining, removed = [], []
for node in net.nodes:
    if node.id not in targets: remaining.append(node); continue
    left = round(max(0.0, node.intensity - drain), 9)
    if left <= 0.0: removed.append(node.id)
    else:           remaining.append({**node, intensity: left})
net = deepcopy(net); net.nodes = remaining
result.magic.networks[school] = net
record.nodes = [ n for n in record.nodes if n not in removed ]

survivors = list(result.settlements.sites)       # nobody dies from a cleanse
drop the four plan keys
rebuild_tail(result, cfg, survivors, 'cleanse', age, religion=False)

cleansed = (record.nodes is now empty)
if cleansed:
    record.cleansed_age = age
    for person in result.villains.people where person.uid == record.villain_uid:
        remove person.god ; person.corrupted = False
        person.held_nodes = [n for n in person.held_nodes if n not in removed]
        person.log.append({age, event:'cleansed', god: record.god_id})
    for god in result.religion.gods where god.id == record.god_id and god.status == 'walking':
        god.status   = 'sleeping'
        god.evidence = { last_seen: { cleansed_age: age } }
return report, title
```
*"The holder is unseated with its last node. Killing the villain would have left the nodes standing and a successor free to take them; this is the other order."*

The drain-rather-than-outcompete design is argued: *"out-competing cannot work: potency saturates at a network's strength, so a corrupted node at full intensity sits above what any counter-node of equal strength can reach, and the boundary would creep inward in a ring and never take the node's own cell."*

At the default `power = 0.25`, `drain = 1.0` per tick: the seat node (4.0) needs 4 ticks, the ring nodes (2.5) need 3.

── C14. `cleanse_request(body)` ──────────────────────────────

`adopt_world`, snapshot prep, `age = len(history.ages)`. If `god_id` target → `depart_god(result, cfg, god_id, age, rebuild=True)` and `report = {kind:'god', god_id, departed:True}`, title `'Opposed: ' + god_id`. Else `_drain`. Then append `{api_version:1, kind:'cleanse', target: dict(target), power, **report}` to `history.operations`, `_snapshot(title, 'cleanse')`, set `corruption_api_version`, `timing_ms['cleanse_total']` (3 dp), and `result.cleanse = report`.

════════════════════════════════════════════════════════════════
PART D — VISITATION
════════════════════════════════════════════════════════════════

── D1. `reach_m(world, cfg) = 3.0 * cfg.settlement_spacing` ──

*"Every distance is a ratio of settlement spacing, never a metre constant or a raster cell, so world scale and raster size cannot turn a local visitation into a regional one."*

── D2. `god_schools(god_id, world, day) -> ordered school list` ──

```
affinity = gods_by_id()[god_id].school_affinity
if affinity == ['moon']:
    leaning = 'still' if moon_state(world.astrology.moon, day).nod_degrees >= 0 else 'restless'
    return [ s for s in KNOWN_SCHOOLS_ORDER if s in HEMISPHERES[leaning] ]
return [ s for s in KNOWN_SCHOOLS_ORDER if s in affinity ]
```
`KNOWN_SCHOOLS_ORDER = weave, umbral, infernal, radiant, fire, water, earth, air` — **the output preserves this order, not the affinity list's order.**
`HEMISPHERES = {'still': [radiant, water, earth, umbral], 'restless': [weave, fire, air, infernal]}`, so a moon god at a positive nod gets `[umbral, radiant, water, earth]` and at a negative nod `[weave, infernal, fire, air]`.
`moon_state(moon,t).nod_degrees = tilt_max_degrees * sin(2π * cycle_fraction(moon,'nod',t))`.

── D3. `validate_visitation(body)` ───────────────────────────

Field set is `('api_version','world','god_id','target','wrath','variation','depart','day')`. After `validate_age_world`, all three of `astrology`, `lunar_almanac`, `religion` must be dicts at `version == 1`, and `religion.catalogue` must match. The god must be named in `world.religion.gods`.

**Departure branch** (`depart == True`): status must be `'walking'`; there must be an undeparted visitation record for it (the same `_reveal` guard as in cleanse); `target` and `wrath` must be absent (`cross_field` otherwise). Returns `(cfg, god, None, 0.0, 0)`.

**Summons branch**: status must not already be `'walking'`; must be `'manifest'` or `'sleeping'` (else refuse with an `alternatives` list of summonable god ids); `len(religion.visitations) < 8` else `over_capacity`; `target` must be a dict naming exactly one of `city_uid`/`node`; for `city_uid`, the city must exist and its `node` is used; for `node`, an int in `[0, len(water.nodes))`; `layers.water_type[z][x]` must be `0` — *"A god walks on land; the target is water"*; `wrath` a finite number in `[0,1]`; `variation` a uint32; `day` `None` or a non-negative int.

── D4. `avatar_cluster(result, cfg, god_id, schools, node, rng, index)`

```
(x, z) = result.water.nodes[node]
centre = direction(x, z, cfg.size)               # the sphere-cell unit vector
angle  = 0.5 * cfg.settlement_spacing / globe_radius       # RING_SPACINGS
base   = rng.uniform(0, 2π)                      # ONE draw, SHARED BY EVERY SCHOOL
placements = [ (centre, 4.0) ] + [ (_offset(centre, angle, base + k*2π/4), 3.0) for k in 0..3 ]
ids = []
for school in schools:                           # KNOWN_SCHOOLS order
    net = result.magic.networks[school]
    for k, (point, intensity) in enumerate(placements):
        node_id = f'avatar-{god_id}-{index}-{school}-{k}'
        net = edit_network(net, new_node={id, direction: list(point), intensity})
        ids.append(node_id)
    result.magic.networks[school] = net
return (ids, centre)
```
Five nodes per school; every school's ring is at the same four bearings.

`direction(x, z, n)`: `z == 0 → (0,1,0)`; `z == n-1 → (0,-1,0)`; otherwise `lon = 2π*(x mod (n-1))/(n-1) - π`, `lat = π/2 - π*z/(n-1)`, `(cos lat cos lon, sin lat, cos lat sin lon)`.

── D5. `suppress_opposed(result, schools, centre, reach, wrath)` ──

*"Weaken rival schools within reach; the record lets departure restore them exactly."*
```
scale   = 1.0 - 0.5*wrath                        # SUPPRESSION
restore = []
for school in schools:
    opposed = OPPOSED.get(school)                # radiant↔infernal, weave↔umbral, fire↔water
    if not opposed: continue                     # earth and air have NO opposite — skipped
    net  = result.magic.networks[opposed]        # MUTATED IN PLACE, not copy-on-write
    near = { i for i,n in enumerate(net.nodes)
             if great_circle(radius, centre, n.direction) <= reach }
    for i in sorted(near):
        restore.append({school: opposed, node_id: net.nodes[i].id,
                        intensity: net.nodes[i].intensity})     # value BEFORE scaling
        net.nodes[i].intensity *= scale
    for edge in net.edges:                       # list order
        if edge.from in near or edge.to in near:
            restore.append({school: opposed, line_id: edge.id, intensity: edge.intensity})
            edge.intensity *= scale
return restore
```
The `restore` entries are discriminated by which key they carry: `node_id` vs `line_id`.

── D6. `divine_lottery(...)` — the fate roll ─────────────────

This is a **second, parallel implementation** of `terrain_history.city_fate`, not a call into it. Differences: no `self_magic` cause, no villain causes, no lunar sway, and the divine cause is appended last.

```
aspect_factor = ASPECT_FACTOR.get(god.aspect, 1.0)      # wild:2.0, sovereign:0.5, else 1.0
cities = deepcopy(result.settlements.sites)
age    = len(result.history.ages)
for city in cities:
    distance = great_circle(radius, centre, city.direction)
    if distance > reach: survivors.append(city); continue

    potencies = { s : layers['ley_'+s][city.z][city.x] if present else 0.0
                  for s in ALL TWELVE SCHOOLS }        # "a god smiting a city sees the
                                                       #  ground as it is"
    causes = []
    (ley_pressure, ley) = ley_threat_pressure(city, layers)
    if ley: causes.append( (ley.school, ley_pressure, LEY_DESTRUCTION[ley.school], ley) )
    for threat in fantasy_nest_threats(city, nests, radius):
        causes.append( (threat.kind, threat.weight, threat.reason,
                        {nest_id, family, distance_m, reach_m}) )
    faith_factor = _faith_factor(result, city, god.id)
    weight = 0.6 * wrath * (1.0 - distance/reach) * aspect_factor * faith_factor
    causes.append( ('divine_'+god.id, weight,
                    f"{god.aspect_name or god.name}: {catalogue_record.smite}.",
                    {god_id, distance_m: distance, reach_m: reach, aspect: god.aspect,
                     faith_factor}) )

    chance = min(0.9, Σ weights)
    rng    = Random( child_seed(seed, f"visitation-{index}-{god.id}-{city.uid}", variation) )
    draw   = rng.random()                                # ONE draw per city, own stream

    if draw >= chance:                                   # the city stands
        survivors.append(city)
        if faith_factor < 1:                             # i.e. == 0.25, the god's own faithful
            strength = round(wrath * (1.0 - distance/reach), 6)
            city.favor = (city.favor or []) + [{god_id, strength, visitation: index}]
            favored.append({city_uid: city.uid, strength})
        continue

    selector = (draw/chance) * Σ weights                 # ORDER-SENSITIVE WALK
    chosen   = causes[-1]                                # default: the last, i.e. the god
    for cause in causes:
        selector -= cause.weight
        if selector <= 0: chosen = cause; break

    legacy = ruin_legacy(city, chosen.kind, potencies,
                         nest_family = chosen.evidence.get('family'),
                         god_school  = schools[0])
    ruin = { RUIN_KEYS from city } + { id:'ruin-'+city.uid, kind:'ruins', destroyed_age: age,
             asset_id:'marker.city_ruins', cause: chosen.kind, reason: chosen.reason,
             evidence: chosen.evidence, probability: chance, roll: draw,
             new_node_school: legacy.school, legacy, visitation: index }
    ruins.append(ruin)
```
`_faith_factor(world, city, god_id)`:
```
faith = religion.faiths.get(city.population_profile)
if not faith: return 1.0
if god_id in faith.gods: return 0.25                    # its own faithful are spared
patron = faith.patron
if patron and (god_id in gods_by_id()[patron].rivals or patron in gods_by_id()[god_id].rivals):
    return 2.0                                          # a rival's flock is punished
return 1.0
```
`ley_threat_pressure(city, layers)` = `min(0.6, potency * 0.45)` for the school `dominant_school` selects (potency ≥ 0.35 and a margin ≥ 0.08 over the runner-up), else `(0.0, None)`.
`fantasy_nest_threats(city, nests, radius)`: iterate `sorted(nests, key=id)`; skip non-surface or real nests; eligible only if the name contains "dragon" or family ∈ {infernal, undead, aberrant}; `reach = min(radius*0.5, max(350.0, (range_m or spacing_m or 350.0)*2))`; skip if beyond; `weight = 0.11 * tier * (1 - distance/reach)` (`tier` here is the **nest danger tier**, 1..5).

── D7. `summon(result, cfg, god, node, wrath, variation, day=None)` ──

```
snapshot prep (previous_layers, previous_state)
index  = len(religion.visitations)
seed   = cfg.seed
age    = len(result.history.ages)
if day is None: day = int(settlements.founding.end_year or 0) * DAYS_PER_YEAR
schools = god_schools(god.id, result, day)
rng     = Random( child_seed(seed, f"visitation-{index}-{god.id}", variation) )
reach   = 3.0 * cfg.settlement_spacing
(cluster, centre)             = avatar_cluster(...)          # consumes ONE rng draw
restore                       = suppress_opposed(...)
(survivors, ruins, favored)   = divine_lottery(...)          # own per-city streams
result.ruins.extend(ruins)
for ruin in ruins:                                           # key point per ruin
    net = networks[ruin.legacy.school]
    networks[ruin.legacy.school] = edit_network(net, new_node={
        id: ruin.id + '-key', direction: ruin.direction, intensity: ruin.legacy.intensity })
drop the four plan keys
rebuild_tail(result, cfg, survivors, 'visitation', age, religion=False)

# --- nest purge, AFTER the rebuild regenerated the nests ---
purge = set( gods_by_id()[god.id].purge_families )
kept, purged = [], []
for nest in result.beast_nests.sites:
    if nest.family in purge and not nest.get('real', True)
       and great_circle(radius, centre, nest.direction) <= reach:
        purged.append(nest.id)
    else: kept.append(nest)
result.beast_nests.sites = kept

# --- favour relief on the rebuilt threat assessment ---
for report in result.threat_assessments.cities:
    favor = first f in favored with f.city_uid == report.city_uid
    if favor:
        report.divine_favor    = favor.strength
        report.regional_threat = round(max(0.0, report.regional_threat
                                              - 0.2*favor.strength), 6)     # FAVOR_RELIEF

# --- conversion ---
converted = []
for city in result.settlements.sites:                        # the REBUILT sites
    if great_circle(radius, centre, city.direction) <= reach
       and city.uid not in {r.uid for r in ruins}:
        faith = religion.faiths.get(city.population_profile)
        if faith and faith.patron != god.id and city.population_profile not in converted:
            converted.append(city.population_profile)        # FIRST-SEEN order preserved

moon = { day: day, **day_state(result.astrology.moon, day) }
record = { …all the fields listed in `writes`… }
for g in religion.gods where g.id == god.id:
    g.status = 'walking'
    g.avatar = { node, since_age: age, day, visitation: index, cluster }
for civ in converted: religion.faiths[civ].conversion = { god_id, visitation: index }
religion.sites.append({ id: f"theophany-{index}-{god.id}", kind:'theophany',
                        god_id, node, visitation: index })
religion.visitations.append(record)
add_religion(result, cfg)                    # AFTER the records, so they are carried forward
result.history.operations.append({api_version:1, kind:'visitation', visitation:index,
                                  god_id, node, wrath, variation})
_finish(result)                              # fill_cities, fill_hamlets, fill_castles, debug
_snapshot(result, …, f"Visitation {index+1}: {god.name}", 'visitation')
```
`add_religion` survives the walking status (`resolve_gods` keeps `status == 'walking'` from the prior record and carries `avatar` forward), the theophany site (`resolve_sites` copies forward any prior site of kind `theophany` or `pilgrimage`), and the conversion (`choose_faiths` is given the prior conversions).

── D8. `depart_god(result, cfg, god_id, age=None, rebuild=True)` ──

```
record = the LAST (search `reversed(visitations)`) record with god_id matching
         and departed_age is None
if record is None: raise ValueError('No undeparted visitation for <god_id>; that god does
                                     not walk by visitation')
cluster = set(record.cluster)
for (school, net) in result.magic.networks.items():
    if no node of net is in cluster: continue
    keep  = [ i for i,n in enumerate(net.nodes) if n.id not in cluster ]
    moved = { old_index : new_index for new_index, old_index in enumerate(keep) }
    net.nodes = [ net.nodes[i] for i in keep ]
    net.edges = [ {**e, from: moved[e.from], to: moved[e.to]}
                  for e in net.edges if e.from in moved and e.to in moved ]
school = record.schools[0]
networks[school] = edit_network(networks[school], new_node={
    id: f"footprint-{god_id}-{record.index}", direction: list(record.direction),
    intensity: 3.5 })                                       # FOOTPRINT_INTENSITY
for item in record.restore:
    pool   = net.nodes if item has 'node_id' else net.edges
    target = first element with id == (item.node_id or item.line_id)
    if target: target.intensity = item.intensity            # EXACT restore, not a re-scale
record.departed_age = len(result.history.ages) if age is None else age
for g in religion.gods where g.id == god_id: g.status='manifest'; remove g.avatar
for site in religion.sites where kind=='theophany' and visitation==record.index:
    site.kind = 'pilgrimage'
if rebuild:
    _rebuild(result, cfg, deepcopy(settlements.sites), 'departure', record.departed_age)
    add_religion(result, cfg)
```
The endpoint remapping is explained: *"Edges index their endpoints by position, so dropping a node renumbers every edge after it. Avatar nodes carry no edges and are appended last, which is why a plain filter was safe; a corrupted network can hold nodes that do have edges, so the endpoints are remapped rather than left pointing at whatever slid down."* The defaulted `next()` is also explained: *"a bare `next()` here raised StopIteration with no message out of two validated request APIs, and inside a caller's generator expression that reads as a normal stop rather than as a failure."*

── D9. `_snapshot` and `state_keys` ──────────────────────────

```
state_keys() = tuple(terrain_history.STATE_KEYS)
             + tuple(k for k in ('astrology','lunar_almanac','religion') if k not in STATE_KEYS)

_snapshot(result, previous_layers, previous_state, title, kind):
    if 'build_stages' not in result: return          # silently skips on a stripped world
    stage = len(result.build_stages) + 1
    result.build_stages.append({
        stage, title, kind,
        layers:         { k: deepcopy(v) for k,v in result.layers.items()
                          if previous_layers.get(k) != v },       # CHANGED LAYERS ONLY
        removed_layers: sorted(set(previous_layers) - set(result.layers)),
        state:          { k: deepcopy(result.get(k)) for k in state_keys()
                          if previous_state.get(k) != result.get(k) } })
    result.phases.titles    = [ s.title for s in result.build_stages ]
    result.phases.completed = stage
```
The comparison is a **deep structural equality** of arbitrary JSON values.

── D10. `visitation_request(body)` ───────────────────────────

```
cfg, god, node, wrath, variation = validate_visitation(body)
started = now()
result  = adopt_world(body.world)
if body.depart:
    snapshot prep; drop the four plan keys
    record = depart_god(result, cfg, god.id)          # age defaults to len(history.ages)
    result.history.operations.append({api_version:1, kind:'departure',
                                      visitation: record.index, god_id: god.id})
    _finish(result)
    _snapshot(…, f"Departure {record.index+1}: {god.name}", 'departure')
else:
    summon(result, cfg, god, node, wrath, variation, body.get('day'))
result.visitation_api_version = 1
result.timing_ms['visitation_total'] = (now()-started)*1000     # NOT rounded
```

## Constants

## terrain_villains

| name | value | units | meaning and effect of change |
|---|---|---|---|
| `VERSION` | `2` | — | Block contract. v1 `tiers` was turmoil-*ever*; v2 is turmoil-*sustained*. *"A consumer that reads a version 1 `tiers` value as 'the worst this region ever was' reads a version 2 one wrong, which is what the integer is for."* |
| `SUPER_TIER` | `1.0` | tier | The band above which a villain is "super". Seating threshold, the tier floor for `causes()` and `build()`, and the exact tier `promote` grants. Raising it makes seating rarer and, because `SEAT_TIER = 1.5 × SUPER_TIER`, also moves the seat/village claim boundary. |
| `REACH_SPACINGS_PER_TIER` | `4.0` | settlement spacings per tier | `reach_m = tier × 4 × settlement_spacing`. At tier 1 on the reference world that is 32 290 m against a 200 km circumference — 16% of the way round. Raising it makes villains regional rather than local. |
| `RISK_WEIGHT` | `0.3` | — | Weight of `war_risk` in `turmoil`. |
| `HUNGER_WEIGHT` | `0.3` | — | Weight of `war_hunger` in `turmoil`. Both are the forward-looking half; with `regional_threat` already 0..1, a maximally tense city saturates the clamp easily. |
| `FALLOFF` | `1.5` | reach multiples | The distance at which a villain's pressure is spent: `spent = 1.5 × reach_m`. Only used in `causes()`. Lowering it sharpens the shadow; raising it widens the lottery's footprint without changing `reach_m` itself (so it does **not** change war contention, which uses `reach_m` directly). |
| `VILLAIN_WEIGHT` | `0.5` | lottery weight | Its share of a city's fate lottery *at the seat, at tier one*. The fate lottery caps total chance at 0.9, so 0.5 is already more than half the ceiling. |
| `STALLED_TIER` | `0.999` | tier | Where a pretender sits when the ceiling is full. Must be strictly below `SUPER_TIER`; the test suite asserts it. The clamp is applied to the **ledger**, not just to the local variable, so a blocked region cannot bank tier and burst through when a slot opens. |
| `FRAGMENT_SHARE` | `0.35` | — | The tier a region keeps when its villain falls: `tiers[rid] = tier × 0.35`. This is the successor squabble — what a villain that left no mark leaves behind. |
| `TIER_KEPT_PER_AGE` | `0.9` | — | **The most consequential constant in the module.** See reasoning_to_preserve for the full argument. A steady concentration `p` settles at `rise × p / (1 − 0.9) = 10 × rise × p` rather than growing without bound with the world's age. Decay from `SUPER_TIER`: `1.0, .9, .81, .729, .656` — four quiet ages to cross the default hold of `.7` from the *bottom* of the band. Seed-visible: changing it moves every world's `tiers` and `outlook`. |
| `FALLEN_CLAIM_INFLUENCE` | `0.6` | — | What a claim keeps at the moment its holder falls. |
| `CLAIM_DECAY_PER_AGE` | `0.6` | — | What it keeps of that for each further age. |
| `CLAIM_INFLUENCE_FLOOR` | `0.05` | — | Below this the value is reported as exactly `0.0`, so a consumer scaling by it stops being nudged. Series: `.60, .36, .216, .1296, .07776, 0`. Seed-visible; recorded in `docs/decisions/024-fallen-claim-decay.md`. |
| `GROWTH_BY_PRESSURE` | `{war:'devouring', nest:'spreading', ley:'hoarding'}` | — | The growth function name, read by `hero_generator`. |
| `DEFAULT_GROWTH` | `'usurping'` | — | Used when no pressure dominates (all three totals zero, or no reports). |
| `WELL_NODE_PREFIX` | `'well-'` | — | The prefix `_held_node` skips. Changing it strands every god-bound villain on its birth age. |
| `WELL_INTENSITY` | `3.75` | ley intensity | *"a well is a source in its own right, not a marker."* Must stay ≤ 4.0 (the hard ceiling `edit_network` enforces and the age-drift clamps to). |
| `WELL_EDGE_INTENSITY` | `2.0` | ley intensity | The joining line's carrying strength. |
| `FOUND_SPACINGS` | `0.8` | spacings | **Dead.** Declared with the comment "how far from its seat a villain plants its own ground" and never read anywhere in the repository. It is the residue of the rejected "found a real city" design that `claim_settlements`' docstring argues against. |
| `MIXED_FACTIONS` | `3` | cities | How many nearest cities a claim draws its peoples from. |
| `SEAT_TIER` | `1.5` (`SUPER_TIER × 1.5`) | tier | Above this a claim is `'seat'`, below it `'village'`. |
| `MARK_KEYS` | 13 names | — | What a fallen villain carries out of its reign. `tier` and `reach_m` are deliberately absent. |
| `RUIN_KEYS` (from `terrain_history`) | 13 names | — | `uid, name, node, x, z, direction, height_m, population_profile, civilization_id, city_class, source_culture, founded_age, war_history` |

**Option defaults and ranges** — `villain_rise` `.5` in `[0,1]`; `villain_hold` `.7` in `[0,1]`; `villain_density` `3.0` in `[1,12]`.

## terrain_corruption

| name | value | meaning |
|---|---|---|
| `API_VERSION` | `1` | |
| `RING_POINTS` | `3` | Ring nodes around the seat; cluster size is `1 + 3 = 4`. |
| `RING_SPACINGS` | `0.4` | Ring radius in settlement spacings → 3 229 m on the reference world, `angle = 0.1014 rad`. |
| `REACH_SPACINGS` | `2.5` | The **floor** under a corrupted villain's reach, 20 181 m on the reference world. *"a villain whose own seat city has since fallen still sits where its node is, and the nearest survivor can be further away than a fixed constant would ever carry."* |
| `SEAT_INTENSITY` | `4.0` | Exactly the `edit_network` ceiling — a corrupted seat is a maximal source. |
| `RING_INTENSITY` | `2.5` | |
| `NODE_BUDGET` | `96` | Corrupted nodes a world may carry at once; cleansing frees them. At 4 per corruption that is 24 concurrent corruptions. |
| `FARM_SHARE` | `0.45` | Of a city's people, **at the seat** (`share = 1`), when blood takes it. |
| `TAKEN_KEY` | `'taken_by'` | The city field eldritch writes. |
| `ORDER` | 11 strings | `['gate','god','host','corrupted cluster','leyline update','biomes','civilization','nests','threat assessment','failure mode','pantheon reaction']` — stamped on the record so a reader knows what ran in what order. |
| `CLEANSE_MAX_DRAIN` | `4.0` | *"a node's full intensity, so power 1.0 ends one in a single tick."* At the default `power = 0.25` the drain is 1.0/tick: seat 4 ticks, ring 3. |
| interest divisors | `200000.` people, `24.` nests, `8.` live schools | Saturation points for `living`, `aberrant`, `dead_magic`. |

**Authored per-god gate values** (`hidden_pantheon.json`): `noticing` = 3 / 6 / 10 / 15; `climb` = 12 / 16 / 22 / 30. The test suite pins that all four `noticing` values are distinct and the last is more than twice the first — *"each god needs its own noticing point"* and *"the last should be far behind the first."*

## terrain_visitation

| name | value | meaning |
|---|---|---|
| `API_VERSION` | `1` | |
| `MAX_VISITATIONS` | `8` | Per-world cap, counted over the whole `visitations` list including departed ones. |
| `RING_POINTS` | `4` | Cluster is `1 + 4 = 5` nodes **per school**. |
| `RING_SPACINGS` | `0.5` | Ring radius, 4 036 m on the reference world. |
| `REACH_SPACINGS` | `3.0` | Visitation reach, 24 217.5 m on the reference world. |
| `AVATAR_INTENSITY` | `4.0` | Centre; the `edit_network` maximum. |
| `RING_INTENSITY` | `3.0` | |
| `FOOTPRINT_INTENSITY` | `3.5` | What the collapsing cluster leaves behind on departure. |
| `DIVINE_WEIGHT` | `0.6` | The god's base share of the lottery at the centre, before wrath, aspect and faith. |
| `SUPPRESSION` | `0.5` | Opposed intensities are multiplied by `1 − 0.5×wrath`, so at full wrath a rival school is halved inside the reach. |
| `FAVOR_RELIEF` | `0.2` | Subtracted from `regional_threat` as `0.2 × strength`. |
| `OPPOSED` | `{radiant↔infernal, weave↔umbral, fire↔water}` | **`earth` and `air` have no opposite and are silently skipped.** |
| `ASPECT_FACTOR` | `{wild: 2.0, sovereign: 0.5}` | Anything else (including `None`) is `1.0`. A Wild god destroys **four times** as much as a Sovereign one — the phrase *"the cure can become worse than the disease"* refers to this, because `_reveal` pushes rivals Wild. |
| faith factors | `0.25` / `1.0` / `2.0` | Own faithful / neutral / a rival's flock. |
| lottery cap | `0.9` | `chance = min(0.9, Σ weights)` — identical to `city_fate`'s cap. |
| `ORDER` | 11 strings | `['avatar cluster','opposed suppression','divine fate lottery','ruins and legacies','leyline update','biomes','civilization','nests and purge','threat assessment and favour','astrology','conversion']` |
| `OWN_STATE` | `('astrology','lunar_almanac','religion')` | Historical: all three are already in `terrain_history.STATE_KEYS`, so `state_keys()` currently returns exactly `STATE_KEYS`. |

**From imported modules, load-bearing here:** `CLASS_INTENSITY = {small:1.5, medium:2.5, capital:3.5}`, `SELF_MAGIC_FLOOR = 2.5`, `DIVINE_INTENSITY = 4.0` (fires only for causes prefixed `divine`, so visitation ruins from the god itself get 4.0 while corruption ruins do not), `dominant_school` threshold `0.35` and margin `0.08`, `VILLAIN_CONTENTION = 0.35` in `terrain_wars`, nest threat gain `0.11 × tier`, nest reach `min(radius*0.5, max(350, range*2))`, `DAYS_PER_YEAR` (360 on the reference world), `REFERENCE_RADIUS_M = 1774.4123532462844` (the ley-width reference world, 11.15 km around — **not** the generated world, which is 200 km around).

## Data files

## `Sim/icarus_sim/hidden_pantheon.json`

The only data file these three modules load that does not already exist elsewhere in the port. Loaded lazily and cached in a module global `_HIDDEN`, by `hidden_catalogue()` in `terrain_religion`.

*"Their own file, with its own identity, so `catalogue_identity()` never moves: hashing them into pantheon.json would retire every saved world each time one was touched."* — the known catalogue's SHA-256 is pinned into every world; the hidden four are deliberately outside that hash.

**Shape:**
```json
{ "schema": "fantasy-world-generator.hidden-pantheon",
  "schema_version": 1, "revision": 1,
  "description": "...", "notes": { "noticing": "...", "interest": "...", "climb": "..." },
  "gods": [ { ... } x4 ] }
```

**One entry in full** (this is the whole schema; the other three differ only in values):
```json
{
  "id": "god_shape_beneath",
  "family": "outside",
  "name": "The Shape Beneath",
  "school": "eldritch",
  "aspects": { "sovereign": "The Quiet Geometry", "wild": "The Waking Angle" },
  "domains": ["madness", "wrong geometry", "dominion of minds", "knowledge that costs"],
  "exists": { "kind": "any", "rules": [ { "kind": "nest_family", "families": ["aberrant"] },
                                        { "kind": "ruins", "min": 1 } ] },
  "interest": "aberrant",
  "noticing": 10,
  "climb": 22,
  "rivals": ["god_radiant", "god_earth", "god_crown_law"],
  "purge_families": ["holy", "fey", "primordial"],
  "smite": "nothing was destroyed; the gates opened on time and the wrong people gave the orders"
}
```

**All four, in file order — the order `watching()` iterates:**

| id | name | school | interest | noticing | climb | exists | rivals | purge_families |
|---|---|---|---|---|---|---|---|---|
| `god_long_thirst` | The Long Thirst | `blood` | `living` | 3 | 12 | `magic_enabled` | god_radiant, god_hearth_harvest, god_first_hearth | holy, fey |
| `god_glad_mother` | The Glad Mother | `rot` | `decay` | 6 | 16 | `magic_enabled` | god_radiant, god_grave_memory | holy |
| `god_shape_beneath` | The Shape Beneath | `eldritch` | `aberrant` | 10 | 22 | `any(nest_family aberrant, ruins ≥ 1)` | god_radiant, god_earth, god_crown_law | holy, fey, primordial |
| `god_unwriting` | The Unwriting | `void` | `dead_magic` | 15 | 30 | `always` | god_weave, god_fire, god_great_wyrm | holy, fey, primordial, draconic |

Aspect names: Long Thirst `{sovereign: "The Patient Vein", wild: "The Red Harvest"}`; Glad Mother `{"The Kind Decay", "The Teeming Bloom"}`; Unwriting `{"The Long Silence", "The Open Absence"}`.

Smites (used verbatim as the ruin `reason`, formatted `f"{god.name}: {god.smite}."`):
- Long Thirst — "the city stood and its people did not; what walked the walls afterward wore their faces"
- Glad Mother — "the dead did not stay where they were put, and there were more of them each morning"
- Unwriting — "the ground where it stood is on no map now, and the ley runs around the place it was"

The `notes` block records the design intent verbatim and should be carried into the port's own data file:
- *noticing*: "Encounters the game has reported before this god will act at all. Below it the chance is exactly zero, not merely small: early playthroughs are guaranteed clean, which is what makes a first corruption read as an escalation the player caused rather than a bad roll."
- *interest*: "Which god takes an interest is read from the world, never rolled. The counter says whether; the world says which."
- *climb*: "Encounters above the noticing point, at which this god is certain to act if it is the most interested."

**Selection:** `watching()` keeps a god when `chance(god, encounters) > 0` **and** `rule_holds(god.exists, facts, manifest)`, then ranks by `(-interest(god.interest, world), god.id)`. `corruption_request` acts on `candidates[0]` only.

## `Sim/icarus_sim/pantheon.json`

Not loaded directly by these modules, but read through `gods_by_id()` for `school_affinity` (villains' `school_god`, visitation's `god_schools`), `rivals` (`_faith_factor`, `_reveal`), `aspects` (`_reveal`), `purge_families` (visitation's nest purge) and `smite` (the divine ruin reason). Entries relevant here carry `id`, `name`, `family` (`school` / others), `school_affinity` (a list, possibly `['moon']`), `aspects` (`sovereign`, optional `wild`), `domains`, `exists`, `rivals`, `purge_families`, `smite`, `folds_into`. `school_god(school)` scans it in file order for the first `family == 'school'` entry whose `school_affinity` contains the school.

## Leyline school taxonomy (a code constant, `terrain_leyline_history`)

`KNOWN_SCHOOLS`, in this exact insertion order, which is the order `god_schools` preserves and the order `dominant_magic` indexes:
`weave, umbral, infernal, radiant, fire, water, earth, air`
`HIDDEN_SCHOOLS`, appended after them so the known eight keep indices 0-7 in every world ever generated:
`blood, void, rot, eldritch`
`SCHOOLS = KNOWN + HIDDEN` (twelve).

*"Generation can never raise one. Their occurrence is locked to zero in the option registry, so `generate_networks` always leaves them empty, and only the corruption API puts a node in one."*

## Random streams

**Villains use no randomness at all.** `terrain_villains` imports `child_seed` from `terrain_tectonics` and never calls it. Every decision — which region seats, which city is the seat, which growth function, which ley node is the anchor, who is promoted first — is a deterministic reading of world state with an explicit tie-break. This is a design property the port must preserve: *"ties by anchor so the order is a property of the world and not of dictionary iteration."*

**Seed derivation** (`terrain_tectonics.child_seed`):
```
child_seed(master, domain, variation=0)
    = first 4 bytes, big-endian, of sha256(f'tectonics-v1:{master}:{domain}:{variation}')
```
A 32-bit value used as a `random.Random` seed. In the C++ port this maps to `fwg::child_seed` (32-bit, `"tectonics-v1"` namespace) or `child_seed64` / `Rng::child` — the port already has three named derivations and must not invent a fourth.

## Corruption — three streams

1. **The escalation roll**, in `corruption_request`, one `random()` draw:
   `Random( child_seed(cfg.seed, f'corruption-{index}-{god_id}-escalation', encounters) ).random()`
   **Variation is `encounters`, not `body.variation`.** The result is compared `roll < chance(god, encounters)`. Because the stream depends on `encounters`, retrying the same request with a higher `encounters` gives a different roll *and* a higher threshold.
2. **The cluster bearing**, in `corrupt` → `_cluster`, one `uniform(0, 2π)` draw:
   `Random( child_seed(cfg.seed, f'corruption-{index}-{god_id}', variation) ).random…`
   **Variation is `body.variation` here.** Exactly one draw is taken from this stream in the current code; the `rng` object is created in `corrupt` and passed down to `_cluster`, which uses it once.
3. **Downstream streams** consumed indirectly by `rebuild_tail` — settlement founding, nests, biomes, roads — each with their own domains. These are not this module's to specify but their draw counts change when the survivor set changes.

Draw count is **not** data-dependent within corruption itself: 1 escalation draw always, 1 bearing draw if it acts.

## Visitation — two streams

1. **The cluster bearing**, in `summon`, one `uniform(0, 2π)` draw:
   `Random( child_seed(cfg.seed, f"visitation-{index}-{god_id}", variation) ).random…`
   Drawn once, **before** the school loop, so every school's ring sits at the same four bearings. If a porter moved the draw inside the loop, every multi-school summons would move.
2. **The fate lottery**, in `divine_lottery`, **one fresh `Random` per city, one `random()` draw each**:
   `Random( child_seed(cfg.seed, f"visitation-{index}-{god_id}-{city_uid}", variation) ).random()`
   This is the same per-city-stream discipline `terrain_history.city_fate` uses (`child_seed(seed, 'city-fate-'+uid, age)`). It means the draw count is **independent of how many cities are in reach** and of the order they are visited — a city's roll depends only on its own uid. That is deliberate and must be preserved: it is what makes a visitation's outcome stable when an unrelated city is added elsewhere in the world.

`depart_god` draws nothing. `_drain` draws nothing. `_snapshot` draws nothing.

## Where non-determinism could leak in

- `_host` uses `max(people, key=(tier, uid))` — total order, no roll.
- `watching` sorts by `(-interest, id)` — total order, no roll.
- `promote` sorts by `(-concentration, anchor)` — total order, no roll.
- `regions` de-duplication uses strict `>` on concentration with the earlier (sorted-culture-id) group winning ties.
- `dominant_pressure` uses `max(sorted(keys), key=totals.get)` — alphabetical tie-break, no roll.

Every one of these is a place where a C++ `std::sort` (unstable) or an unordered container would silently change output. Use `std::stable_sort` with an explicit total-ordering comparator, and ordered maps.

## Ordering hazards

Every one of these reaches output. This codebase is unusually explicit about it (*"ties by anchor so the order is a property of the world and not of dictionary iteration"*), and most of these are load-bearing.

**terrain_villains**

1. `regions()`: `sorted(cultures, key=lambda c: str(c.id))` — **string** sort of the culture id, not the integer `index`. Culture ids look like `human_heartland-672bcb6c`, so this is a lexicographic sort over profile-then-hex.
2. `regions()` fallback: `sorted(by_uid.values(), key=c.uid)` — uid string ascending.
3. `regions()` final return: `[best[k] for k in sorted(best)]` — **anchor id ascending**. This is the order of the main loop in `advance`, of the rows in `outlook`, and the tie order in `promote`.
4. `regions()` anchor de-duplication: strict `>` on concentration — the **first** group in culture-id order keeps the anchor on an exact tie.
5. `_seat()`: `sorted(cities, key=(-turmoil, uid))` — most troubled first, **ties by ascending uid**.
6. `_held_node()`: `sorted(result.magic.networks.items())` — **school name ascending** (`air, blood, earth, eldritch, fire, infernal, radiant, rot, umbral, void, water, weave`), then node list order within a network, with strict `<` on distance. Two equidistant nodes resolve to the alphabetically earlier school.
7. `dominant_pressure()`: `max(sorted(totals), key=totals.get)` — ties resolve to `ley` before `nest` before `war`.
8. `advance()` main loop order: produced anchors (ascending) **then** stranded anchors (ascending). *"Visited **after** the produced regions, so which regions seat a villain this age is unchanged: a stranded reign ending must not free a seat in the age it ends."*
9. `advance()` `stranded = [rid for rid in sorted(living) ...]`.
10. `advance()` `block['tiers'] = {k: round(v,9) for k, v in sorted(tiers.items())}` — the serialised map is key-sorted, so JSON byte order depends on it.
11. `advance()` `block['fallen'] += sorted(marks, key=m['id'])`.
12. `advance()` `block['people'] = [living[k] for k in sorted(living)] + sorted(fallen, key=(v.get('fell_age',-1), v['uid']))`. *"The living keep their existing order, by region anchor, so a world in which nobody has fallen serialises exactly as it did before the fallen were kept at all."* `fell_age` defaults to `-1` so a malformed fallen record sorts first.
13. `marks_left()`: `sorted(ruined)`, `sorted(claims)`, `sorted(held_nodes)`.
14. `causes()` iterates the cast in `standing()` order (i.e. `people` order) and **appends**; the comment is explicit: *"Appended, never inserted: the selector walk in city_fate is order-sensitive."* Reordering the villain causes changes which cause is chosen for a given draw, which changes the ruin's school, which changes the ley field.
15. `outlook()` stranded rows: `sorted(seated - produced_anchors)`.
16. `promote()`: `sorted(..., key=(-concentration, anchor))`, then `ranked[:room]`.
17. `promote()` write-back uses the same `sorted(living)` / `sorted(fallen, key=(fell_age,uid))` shape.
18. `sink_well()`: `enumerate(net['nodes'][:-1])` — edges are created in **node list order**, and `edge_id` is `f'{well_id}-line-{len(joined)}'`, so the *name* of each edge depends on that order. `villain['held_nodes'] = sorted(set(...))` afterwards.
19. `claim_settlements()`: `sorted(..., key=(distance, uid))` then `[:3]`; `factions: sorted(set(mix))` (deduplicated and sorted) but `drawn_from` preserves the distance order.
20. `school_god()`: first match in `pantheon.json`'s `gods` array order.

**terrain_corruption**

21. `watching()`: `sorted(rows, key=(-interest, god_id))`. The candidate list order is also reported verbatim in `result['corruption']['watching']`.
22. `hidden_catalogue()['gods']` iteration order = **file order**, which is the pre-sort order and therefore the tie order for equal `(interest, id)` — though ids are unique so that cannot bite.
23. `_host()`: `max(people, key=(tier, uid))` — ties on tier resolve to the **greatest** uid string.
24. `_cluster()`: placements list order fixes `corrupt-<god>-<index>-0` as the seat and `-1..-3` as the ring at bearings `base`, `base+2π/3`, `base+4π/3`.
25. `_failure_mode()` iterates `result['settlements']['sites']` in list order; `survivors` and `touched` preserve it. `survivors` order feeds `rebuild_tail` → `add_settlements`, where `_survivors` order affects founding.
26. `validate_*`: `sorted(set(body) - allowed)[0]` — the *first alphabetically* unknown field is the one named in the error.
27. `corrupt()`: `host['held_nodes'] = sorted(set(old) | set(nodes))`.
28. `_reveal()` iterates `religion['gods']` in list order; `roused_by` is appended in god-record order.
29. `_drain()`: `remaining` preserves node list order; `record['nodes']` filter preserves its own order.

**terrain_visitation**

30. `god_schools()`: `[s for s in KNOWN_SCHOOLS if s in affinity]` — **the output order is `KNOWN_SCHOOLS` insertion order (`weave, umbral, infernal, radiant, fire, water, earth, air`), not the affinity list's order.** This matters because `schools[0]` is passed to `ruin_legacy` as `god_school` and `record['schools'][0]` is where the departure footprint is planted.
31. `avatar_cluster()`: node ids embed both the school and `k`, and `ids` accumulates school-major then k-minor.
32. `suppress_opposed()`: `for i in sorted(near)` for nodes (so `restore` is node-index ascending), then `for edge in net['edges']` in list order. The `restore` list's order is what `depart_god` replays, but since it restores by id and sets an absolute value, order does not change the result — only the serialised bytes.
33. `divine_lottery()` cause list order: ley (if any), then nest threats **in `sorted(nests, key=id)` order**, then the divine cause **last**. The selector walk subtracts in this order and defaults to `causes[-1]`, i.e. the god, so ordering directly decides attribution.
34. `divine_lottery()` iterates `cities` in `settlements.sites` order; `survivors` and `ruins` preserve it.
35. `summon()` `converted` preserves **first-seen** order over the rebuilt `settlements.sites`, with an explicit `not in converted` dedup — not a sort.
36. `summon()` nest purge iterates `beast_nests.sites` in list order; `kept` preserves it.
37. `summon()` favour: `next((f for f in favored if f.city_uid == report.city_uid), None)` — a linear scan, first match.
38. `depart_god()`: `reversed(visitations)` — the **most recent** undeparted record for that god.
39. `depart_god()` node compaction: `keep` preserves node order, `moved` is built `{old: new for new, old in enumerate(keep)}`, and edges are filtered to those with both endpoints surviving.
40. `depart_god()` restore: `next((v for v in pool if v.id == …), None)` — first match, silently skips a missing id.
41. `_snapshot()`: `removed_layers: sorted(...)`, and `result.layers.items()` / `state_keys()` iteration order decides the serialised order of the `layers` and `state` maps.

**General:** every `dict` in this codebase is insertion-ordered and that order is serialised to JSON. The port must use an ordered map (or preserve an explicit key vector) anywhere a block is emitted, and `std::map<std::string, …>` only where the Python code actually sorts.

## Edge cases

**Villains**

- `rise <= 0 and 'villains' not in result` → `advance` returns `[]` and **writes no block at all**. *"the option is invisible in the output, not merely inert in it. A world that was built with villains keeps its record even if the rate is later turned down."* Note `rise <= 0.` with `rise` a float — exactly zero and any negative.
- `promote` with `result.get('villains') is None` → returns `[]`. *"the end of the simulation must not invent one."*
- `promote` with `room <= 0` or an empty `ranked` slice → returns `[]` **without touching the block at all** (no version bump, no tiers rewrite). Idempotency depends on this.
- `regions()` with no cultures and no sites → `groups == []` and `by_uid == {}` → returns `[]`; `ceiling(0, d) == 0`, so nothing can seat.
- `regions()` with cultures whose `city_ids` resolve to nothing → those groups are dropped before the anchor pass.
- `regions()` with no magic at all → `_held_node` returns `(None, None)` and the anchor falls back to `'seat-' + seat_uid`. *"Where there is no magic at all it falls back to the seat city's uid, which survivors also carry forward."*
- `_seat()` with an empty city list → `None`, and the whole group is skipped (cannot happen after the `if cities:` filter, but the guard is there).
- `concentration()` over a region none of whose cities has a threat report → `0.0`. *"a region the assessment has nothing to say about concentrates nothing."*
- `dominant_pressure([])` or all-zero totals → `None` → `growth = 'usurping'`.
- **Stranded anchors**: a villain whose anchor is not among this age's produced regions. Two routes, both documented: a seat city taken by a fate (so its culture is not rebuilt from the surviving roads), and a ley node appended nearer the seat (which moves the anchor while the region carries on). Handled by visiting the anchor with `pressed = 0.0`, **deliberately not by an immediate fall**: *"unseating that one the moment a node is appended would be a rule about ley geometry masquerading as a rule about power."* This occurs in the default reference world.
- `claim_influence(n)` with `n < 0` → `1.0`, which is the guard for a `fell_age` ahead of the current age (should not happen; it is a defensive value, not a decay step).
- Fallen villains whose `fell_age` is `None` are skipped by the decay loop entirely.
- `fallen_mark` returns `None` when the villain left no ruins, no claims and no well. `held_nodes` alone does **not** count. *"A villain that reached the band, held nothing, took no city and sank no well did not mark the world, and inventing a monument for it would make the record of the dead less useful rather than more."*
- `born_age` of `0`: handled explicitly with `born = age if born is None else int(born)`, **not** `born or age` — *"age 0 is falsy, and the first age is exactly when a villain is most likely to have risen."*
- `causes()`: the villain's own **seat city is immune** (`city.uid == villain.seat_uid` → skip) and a city exactly at `distance == spent` is excluded (`>=`).
- `sink_well` is a no-op returning `None` when the villain has no school, the school is absent from `networks`, the villain has no god, or the well node already exists — so one well per villain, ever.
- `sink_well` with an empty `held_nodes ∩ net.nodes` → the well node is still added, `joined == []`, `well.lines == []`, and the villain still gets a `well` record. `build` still counts that as work done.
- `claim_settlements` with no cities at all → `None`.
- A world with fewer than 3 cities: `near[:3]` silently yields fewer, and `drawn_from` is shorter than `MIXED_FACTIONS`.
- **Poles**: `direction(x, 0, n) == (0,1,0)` and `direction(x, n-1, n) == (0,-1,0)`. Every `acos(clamp(dot))` is safe there. `_offset` has an explicit pole guard (`abs(up.y) > 0.999` switches the Gram-Schmidt reference from `(0,1,0)` to `(1,0,0)`), without which `north` would be a zero vector and `_unit` would divide by zero.
- **Seam**: `x mod (n-1)` in `direction` folds column `n-1` onto column `0`. All distances here are great-circle on unit vectors, so the seam is not a special case.

**Corruption**

- `watching()` with `religion.facts` absent or falsy → `[]`, and the response says *"no hidden god is watching yet"*.
- `candidates` non-empty but `host is None` (every standing villain already corrupted, with no explicit `villain_uid`) → nothing acts, and the response says *"a watching god did not act this time"* — which is slightly misleading, because nothing was rolled.
- An explicit `villain_uid` naming an **already corrupted** standing villain is accepted by `_host` (it searches `candidates`, not the filtered `people`), so a second god can take a host the first already has. `host['god']` and `host['school']` are overwritten and both corruption records point at the same villain.
- `interest('living', …)` with zero total population → `0.0` via the explicit `if people else 0.0` (avoids `0/200000` producing `0.0` anyway, but also avoids a divide that never happens).
- `interest('decay', …)` with zero cities and zero ruins → `0/max(1,0) = 0.0`.
- `interest('dead_magic', …)` with all eight known schools live → `max(0, 1-1) = 0.0`; with none live → `1.0`.
- `chance` with `climb <= noticing` (malformed data) → `1.0` rather than a division by zero.
- `NODE_BUDGET`: the counter is `sum(len(c['nodes']) for c in corruptions if not c.get('cleansed_age'))` — **`cleansed_age == 0` is falsy and therefore counts as not cleansed.** In practice `validate_age_world` forces at least two ages so `age >= 2`, but the port should test `is not None`.
- `_failure_mode` with `reach == 0` would divide by zero in `share`; prevented by the `max(..., 2.5*spacing)` floor in `corrupt`.
- A city exactly at `distance == reach` is **inside** (`if distance > reach: survivor`), giving `share == 0.0`, a `farmed` entry with `people_lost == 0.0`, or a ruin with `share == 0.0`.
- `_drain`: a node whose intensity is exactly equal to `drain` → `left == 0.0` → removed (`left <= 0`).
- `_drain` on a corruption whose nodes were all already removed → `cleansed` is True and the record is closed; but `validate_cleanse` refuses a record that already has a `cleansed_age`.
- `_drain`'s misindex guard tests `isinstance(e.get('from'), str)` — edges index by **integer position**, so the guard never fires. See open_questions.
- `cleanse` with `power` exactly `1.0` → `drain = 4.0` → every node in the cluster removed in one tick, since the maximum intensity is 4.0.
- `power == 0` is rejected (`0 < power`), `power > 1` is rejected.

**Visitation**

- A target on water → `ValueError('A god walks on land; the target is water')`.
- A `node` outside `[0, len(water.nodes))` → `ValueError('target node outside the grid')`.
- `wrath == 0` → the divine cause has weight `0`, the god still plants its cluster and still suppresses nothing (`scale = 1 - 0 = 1`), and no city is favoured (`strength == 0` is still recorded for the faithful, as `round(0*…, 6) == 0.0`).
- `god_schools` returning `[]` — possible if a god's `school_affinity` names nothing in `KNOWN_SCHOOLS`. Then `avatar_cluster` adds no nodes and returns `([], centre)`, `suppress_opposed` does nothing, and `schools[0]` in `divine_lottery` and `depart_god` raises `IndexError`. No god in the shipped catalogue has this shape.
- `OPPOSED` has no entry for `earth` or `air`, so an earth or air god suppresses nothing.
- `ASPECT_FACTOR.get(god['aspect'], 1.0)` with `aspect is None` (a sleeping god that was never resolved) → `1.0`.
- `_faith_factor` with no faith for the city's profile → `1.0`; with `patron is None` → `1.0`.
- `chance` can exceed `0.9` before the cap; the cap means a city can never be certain to fall.
- `draw >= chance` with `chance == 0.0` → always a survivor (`random()` is in `[0,1)` so `draw >= 0` always holds).
- The selector walk defaults to `causes[-1]` (the god) if floating point leaves `selector > 0` after subtracting everything.
- `MAX_VISITATIONS` counts **departed** records too, so a world can be permanently out of summons.
- `depart_god` with no undeparted record → `ValueError` with a message (deliberately not a bare `StopIteration`).
- `depart_god`'s restore loop uses `next(..., None)` and silently skips a node or edge id that no longer exists — which happens if a later corruption cleanse removed it.
- `_snapshot` returns immediately if `'build_stages' not in result`, so a world exported without stages takes no snapshot and `phases` is not touched.
- `visitation_request` writes `result['timing_ms']['visitation_total']` by subscript; this is safe only because `adopt_world` guarantees `timing_ms` exists. *"`cli.py` strips `timing_ms` recursively to keep an exported world byte-reproducible, and 32 producers across 18 modules write into it by subscript, so the key has to exist before any of them runs."*
- `adopt_world` shares `build_stages` by reference rather than deep-copying it: *"snapshots are immutable and copying them doubles the cost of the most expensive block in the document."* The list itself is re-bound (`list(...)`), so appending to the copy does not touch the caller's, but the stage dicts are shared.

## Reasoning worth preserving

These comments record measurements, rulings and failures that exist nowhere else. Quoted.

## Why villains exist at all (`terrain_villains` module docstring)

> "Every figure the generator otherwise produces is a point in the world, named after events that already happened. A super villain is a field: a reach in metres with falloff, like a ley network or a visitation, that causes wars and city fates in its own name.
>
> Tier is reach and it is continuous, so 'super villain' is a band on a curve rather than a flag that flips. That is the whole pacing model: one that has just crossed the line is a threat that is growing, not one that has arrived, so the peace after a player kills one comes from the successor being weak rather than from a cooldown holding anything back.
>
> `villain_rise` defaults to .5 and a generated world ends with super villains standing in it: accumulation alone cannot seat anybody inside two age transitions, so the rate is the campaign ramp and `promote()` is the arrival. Setting it to zero is still a real off switch -- no `villains` block is written at all."

## `TIER_KEPT_PER_AGE` — the whole argument (this is the most expensive comment in the file)

> "What a region's tier ledger keeps from one age to the next.
>
> Without it the ledger is turmoil-*ever*: `tier = tier + rise * concentration`, both factors non-negative by construction, so a seated villain's tier is monotonically non-decreasing. Seating requires `tier >= SUPER_TIER` (1.) and `villain_hold` is declared `0. .. 1.`, so `tier < hold` needed `hold > 1.0000000000000009` and the reachable set was empty by one epsilon. Everything downstream of the fall -- `status: 'fallen'`, `fell_age`, `villains.fallen`, the claim influence fields, `fallen_mark`, and the consumer filters added when the fall was made recordable -- was dead code in every buildable world. Hysteresis needs a measure that can come back down, and turmoil-ever cannot: a region at zero turmoil for twenty-five ages held its villain at exactly its seating tier.
>
> With it, tier is turmoil *sustained*. A region holds its villain while it keeps concentrating and loses it when it goes quiet, and a steady concentration `p` settles at `rise * p / (1 - TIER_KEPT_PER_AGE)` rather than growing without bound with the world's age -- so reach measures how bad a region is, not how old the world is.
>
>   quiet ages since the peak   0     1    2    3     4
>   tier from SUPER_TIER        1.0   .9   .81  .729  .656
>
> .9 is chosen for the band, not for the arithmetic: four quiet ages to cross the default `villain_hold` of .7 from the *bottom* of the band is what 'a reign is long once established' has to mean once it can end at all. Seed-visible -- every world's `tiers` and `villains.outlook` move, which is why VERSION is 2."

## The version-2 note

> "2: `tiers` stopped being turmoil-ever and became turmoil-sustained (TIER_KEPT_PER_AGE), which is what made the fall band reachable at all; a claim's `kind` tracks its holder's tier and carries `refreshed_age`; and `outlook.regions` now also reports an anchor that still holds a villain but that no region was produced for, whose `culture` is null. A consumer that reads a version 1 `tiers` value as 'the worst this region ever was' reads a version 2 one wrong, which is what the integer is for."

## Fallen claim decay (`FALLEN_CLAIM_INFLUENCE` / `CLAIM_DECAY_PER_AGE`)

> "How hard a fallen villain's claims still press on the world. Ground it took stays taken -- that is a decision, not a default -- but how *strongly* taken is a separate question from whether the record persists, and it is deliberately a number rather than a boolean so it can decay without reopening the consumers.
>
> The decay is now argued and set, by the same case FRAGMENT_SHARE makes for tier: a region's grip fades when its holder goes. A world that never decays this places by who *ever* held power rather than who holds it, and that failure only appears over spans no single age advance could reach -- which is why the value waited for the clock.
>
> The claim drops at the fall and fades by the same factor each age after it, to a floor below which it stops steering entirely and is reported as zero. The record is never pruned: what fades is how hard the claim presses, not whether it happened.
>
>   ages since fall  0     1     2     3     4     5
>   influence        .60   .36   .216  .130  .078  0 (below the floor)
>
> Seed-visible: a world containing a fallen villain places differently than it did when this was 1.0. See docs/decisions/024-fallen-claim-decay.md."

## Why the anchor is a ley node, and why wells are excluded

> "`humans.cultures` groups road-linked cities, so a region is something the world decided rather than something this module invented. But a culture's id is rebuilt every age from whatever the roads join that age, so it cannot carry anything across one: accumulating tier against it silently resets the region every time. The anchor is therefore the nearest ley node, which persists — ids are stable, new ones are only ever appended."

> "A villain's own well is a ley node it created at its own seat, so it is always the nearest node to that seat. Left in the running it re-anchors the region onto the holder's own work: `regions()` then reports a different anchor, `advance` looks the villain up by the old one and misses, and the reign is stranded on the very age it began -- deterministically, for every villain bound to a school god. That is why the anchor must be ground the world laid down, not ground the villain did."

> "Keyed to the node and not to the city on purpose: anything hung on a city dict that is not in the survivor key tuple is dropped silently at the next age transition."

## The stranded-anchor ruling, in full

> "A villain whose anchor is not produced this age was never the subject of this loop: its tier was never touched, the fall check never ran, and it went straight back into `people` as standing, holding a frozen reach over a region the world no longer has and pressing cities together through `resolve_wars._villain_pressure` forever. Two ordinary routes get there -- a seat city taken by a fate, so its culture is not rebuilt from the surviving roads, and a ley node appended nearer the seat, which moves the anchor while the region itself carries on. It is visited here with the concentration a region nobody produced presses, which is none.
>
> Deliberately NOT an immediate fall. The second route leaves a region that still exists under a villain that still holds it, and unseating that one the moment a node is appended would be a rule about ley geometry masquerading as a rule about power. Decay reaches the same end for the first route within the same band as any other quiet region.
>
> Visited *after* the produced regions, so which regions seat a villain this age is unchanged: a stranded reign ending must not free a seat in the age it ends."

## Keeping the fallen

> "`people` holds the fallen as well as the living, the way `heroes.people` holds legends and `npcs.people` holds the dead: one list, a status field, and the reader filters. Every consumer that means 'who stands right now' must say so, because the list stopped answering that question by construction when the fallen began staying in it. Absent status reads as living: a record written before the fall was recorded at all described someone who stood."

> "Appended and never pruned, exactly as `ruins` is. The world has a finite population, so its dead are a knowable set rather than an unbounded stream -- which is the argument for keeping them properly, not the argument against."

> "Out of the standing set, into the record. This line used to be `living.pop(rid)` alone, which threw away the status and the log entry written immediately above it."

> "The claims are stamped with the holder's state as it changes, so a consumer reads how hard a claim presses off the claim itself and needs neither this module nor a join against the holder. That matters because `key_locations` is a leaf package and cannot import any of this."

> "The mark is taken before the roster entry leaves the standing set, so it reads the villain as it was at the end of its reign rather than after."

## Why `advance` returns the standing cast rather than the block

> "The standing cast, not the block. This return flows into resolve_wars, whose `_villain_pressure` reads `reach_m` with no status test of its own, and a fallen villain still carries the reach it had when it fell. Returning the block here would press cities together on behalf of someone who no longer stands."

And in `terrain_history.add_threat_assessments`: *"`reach_m` is refreshed before the fall is decided, so a fallen villain still carries a live-looking reach and would feed the war outlook without the standing filter."*

## Why `causes()` may not insert

> "Appended, never inserted: the selector walk in city_fate is order-sensitive."
> "The tier test below also excludes the fallen, because a villain falls under `hold` and `hold` is under SUPER_TIER -- but that is a coupling between two constants, not a rule. A fallen villain reaching this lottery would move every world, so the rule is stated rather than relied upon."

## `promote` — the measurement

> "`advance` accumulates tier from turmoil, and a world only ever runs two age transitions inside a generation, so organic seating needs a `villain_rise` above 0.834536 at seed 42 size 17 -- above most of the legal range, and unreachable at the default. Turning the rate up far enough to seat somebody would also make every intervening age lurch. So the rate stays the campaign ramp and this is the arrival.
>
> Deliberately NOT an accumulation. A promoted villain enters at exactly `SUPER_TIER`, the bottom of the band, because a villain that has just crossed the line is a threat that is growing rather than one that has arrived -- which is the whole pacing model. The tier it is given is the tier it would have to hold.
>
> Idempotent: a region that already holds a standing villain is skipped, and the ceiling counts who is already seated, so running this twice changes nothing. That matters because the hook fires on the final age of a generation AND on the final age of an `advance_age_request`, and a caller may advance a world that was already promoted."

The world-options comment adds the same measurement: *"at seed 42 size 17 the most concentrated region reaches tier 1.198 only at rise 1.0, so nothing below 0.834536 seats anybody by accumulation alone."*

## Marks, and why a fallen villain has no asset

> "What a fallen villain carries out of its reign, chosen the way RUIN_KEYS chooses what a ruined city carries: identity, where it stood, and what it was -- not its live state. `tier` and `reach_m` are deliberately absent; both describe a grip it no longer has."

> "No asset_id, unlike a ruin. A ruin is a thing standing on the ground and needs a marker in the exhaustive asset list; a fallen villain is a record about ground that other records already hold -- its wells are ley nodes and its claims are claims, both already placed. Giving it an asset would add an identity the catalogue must carry for nothing to stand at."

> "Read back out of `ruins` rather than accumulated on the villain, for the same reason the region is anchored to a ley node: anything hung on a live record is lost the moment that record is rebuilt, and ruins are already durable and already carry the attribution."

> "'If they indeed left a mark' is a real condition, not a formality. A villain that reached the band, held nothing, took no city and sank no well did not mark the world, and inventing a monument for it would make the record of the dead less useful rather than more. What that villain leaves is what it always left: the decayed tier the region keeps, which is the successor squabble."

## Growth is a contract with a package that never imports this one

> "Which pressure raised them decides how their reach grows, and therefore how they are fought. The sim owns this field; the hero generator reads it and picks the matching escalation of the archetype card a person already carries, so the two agree without either package importing the other."

## Wells and claims

> "A god-bound villain sinks a leyline well, which re-routes the tracks themselves. A key point is a source; a well is a source that is *joined*. The edges are the point: they change what the network carries between the villain's holdings, which a lone node cannot do. Edges index their endpoints by position, so they are appended with the node and never inserted."

> "Ground a villain has taken for its own, mixed rather than of one people. Recorded as the villain's claim, not injected into `settlements.sites`: add_settlements places by suitability and only inherits identity from a survivor already at that node, so a site pushed into the survivor list would simply not be placed. Founding real cities means changing placement, which is a settlement-model change and not a tail on this one. The claim is what an orchestrator reads until then.
>
> **A claim is a snapshot with a standing half, and the split is deliberate.** `age`, `direction`, `node`, `factions` and `drawn_from` record what this villain took and when, so a city founded two ages later does not retroactively join a claim staked before it existed -- and a `drawn_from` uid that is a ruin by now is the record being honest about a claim taken from the living. What tracks the present is the holder's own standing: `kind`, and the `holder_status`, `holder_fell_age` and `influence` that `advance` restamps.
>
> The rejected alternative was to treat the whole claim as a standing fact and redraw `factions` and `drawn_from` every age. It is defensible -- the docstring above argues for it -- but it makes `age` mean nothing, it silently rewrites history every age, and no consumer reads either field today. Taking `kind` alone fixes the half that is wrong under both readings and leaves the half that needs a ruling to the ruling."

> "The id is `claim-<uid>`, one constant per villain, so from the second age onward this function used to recompute the whole claim and drop it on the floor. Only the standing half is refreshed: `kind` is a band on the holder's tier now, and a tier band is not a thing any reading of this record makes correct to freeze."

> "Mixed by construction: a villain takes whoever is close, not whoever shares its blood, which is what makes its holdings look wrong to everyone."

## Corruption — the design

> "Corruption: a hidden god seats a super villain as its instrument. Stateless JSON API v1. Same input, same output; the caller's world is unchanged; persist the returned world to call again. This is the only path by which a hidden school ever gets a node: generation locks their occurrence at zero and player leyline edits are restricted to the known eight, so nothing else in the codebase can reach them.
>
> The gate is the game's ledger. It reports how many villains the player has met across playthroughs as `encounters`, and below a god's noticing point the chance is exactly zero — the gods are not watching yet. Above it, it climbs. Which god takes an interest is never rolled: it is read from the world, so the corruption fits the place it happens.
>
> The corrupted node, not the villain, is the thing that persists. Killing a holder unseats them and leaves the nodes; an unheld node is a spawner."

> "Zero and not merely small: a guaranteed-clean early playthrough is what makes the first corruption read as something the player caused."

> "Most interested first, ties by id: a draw between two gods is decided by the world and then by name, never by a roll."

> "Already taken by a hidden god is the only disqualification. Being sworn to an old one is not: a villain bound to a school god is precisely who a hidden god reaches for, and excluding them made every seated villain ineligible the moment they started sinking wells, because sinking a well binds them to their school's god."

> "A corrupted villain reaches as far as it already did. The floor matters because a villain whose own seat city has since fallen still sits where its node is, and the nearest survivor can be further away than a fixed constant would ever carry."

The four failure modes, verbatim:
> blood — "Farmed: the city stands and its people do not."
> eldritch — "Taken, not destroyed. Nothing visibly changes, which is the horror."
> rot/void — "Rot and Void both end the city; what they leave behind differs."

> "`cause` is `rot_plague` or `void_unmade`, and no `source_school` branch names either, so `ruin_legacy` is asked only for the class intensity: the god is the source and the school and basis are stated here rather than resolved. This used to pass the god's school to `ruin_legacy` as `villain_school` and then overwrite `school` on the way out. Only a `villain`-prefixed cause ever reads that parameter, so the argument did nothing and the returned `basis` stayed `region` - the ruin claimed the region's dominant magic had chosen a hidden school, which the region can never hold."

> "The known gods stop not speaking of it. Before a return there is no trace of the hidden four anywhere; afterwards the known pantheon gains it as a rival and its aspects are pushed Wild, which is what makes the cure able to become worse than the disease: a Wild god destroys four times as much."

> "One snapshot body, several callers: a third copy of this is exactly the duplication that had age_transition and _rebuild silently disagreeing before they were unified."

> "Standing only: a god reaches for someone who holds ground, and `people` now keeps the fallen too. A world whose only villain has fallen has none to corrupt."

## Cleansing

> "Drive a corrupted node down, or drive a walking god off the plane. One mechanic with two names. An avatar is a ley cluster and not a creature, so opposing a god is cleansing its nodes; the difference is only that a god has somewhere to go back to.
>
> The structure drains as it ramps rather than merely out-competing, because out-competing cannot work: potency saturates at a network's strength, so a corrupted node at full intensity sits above what any counter-node of equal strength can reach, and the boundary would creep inward in a ring and never take the node's own cell."

> "The holder is unseated with its last node. Killing the villain would have left the nodes standing and a successor free to take them; this is the other order."

> "Corrupted clusters are nodes only. If that ever stops being true, removing one by filter would leave every edge endpoint index in this network pointing at the wrong node, which is the same latent trap depart_god carries."

> "Two different things set `status` to 'walking', and only one of them writes a visitation record. A visitation does; `_reveal` does not, because a corrupted god's footprint is its ley cluster. `depart_god` requires the record, so a god revealed by corruption would reach an unguarded `next()` and raise a bare StopIteration out of a validated API. Refuse it here, by name, instead. `.get` rather than subscripts: a validator whose job is to turn malformed input into a ValueError must not itself raise KeyError on a partial record."

## Visitation

> "Divine visitation: the orchestrator summons a god onto the mortal plane. Stateless JSON API v1. … A summoned god is highly disruptive: an avatar cluster saturates its school around the target, the cities within reach roll their fate under divine pressure, rival nests are driven out, survivors convert, and when the god departs a footprint key point, the ruins, the mutated biomes and the converted faiths remain. Every distance is a ratio of settlement spacing, never a metre constant or a raster cell, so world scale and raster size cannot turn a local visitation into a regional one."

> "Every school, known or not: a god smiting a city sees the ground as it is."

> "Weaken rival schools within reach; the record lets departure restore them exactly."

> "Every city within reach rolls the age lottery with one more cause: the god itself."

> "The pantheon is not re-resolved here; a visitation writes its own religion records and calls add_religion once they are in place."

> "Defaulted deliberately: a bare `next()` here raised StopIteration with no message out of two validated request APIs, and inside a caller's generator expression that reads as a normal stop rather than as a failure."

> "Edges index their endpoints by position, so dropping a node renumbers every edge after it. Avatar nodes carry no edges and are appended last, which is why a plain filter was safe; a corrupted network can hold nodes that do have edges, so the endpoints are remapped rather than left pointing at whatever slid down."

> "`status` alone is not the precondition `depart_god` actually needs: it needs an undeparted visitation record. A god revealed by `terrain_corruption._reveal` walks without one, and would otherwise reach depart_god's `next()` and raise a bare StopIteration. `.get`, not subscripts - records in the wild are not all complete, and a validator must not raise KeyError on the input it is judging."

> "The sky on the arrival day itself, which the orchestrator may have named."

> "The pipeline's snapshot keys plus this feature's own, until the pipeline lists them."

## The conformance record (`docs/conformance/time-advance.md`)

The only written record that touches these modules. It does **not** disagree with the code; it restates and confirms:

> "The villain **cast** is read-only: a villain rises, holds or falls at an age boundary and nowhere else, because `advance` reads the threat assessment an age inherited and raising one mid-tick would let a villain be raised by its own damage. Only `villains.outlook`, a reading of present ground, is refreshed."

> "`villains` is a block whose bytes move for reasons no `world_options` records. Two module constants, not controls, so two worlds from one seed and one option set can differ across either change: the fallen-claim decay of decision 024, and `TIER_KEPT_PER_AGE`, the share of its tier a region carries from one age to the next. The second is what makes `villain_hold` reachable at all — while the ledger only ever rose, no legal value of that option could end a reign — and it moves every world that carries a `villains` block, which is why the block is at version 2. Both are seed-visible and deliberate."

`docs/conformance/request-failures.md` mentions the corruption, cleanse and visitation boundaries only in a list: *"age, visitation, corruption, cleanse, nomad, moon, patch and time boundaries validate their…"*. There is **no** conformance record specific to villains, corruption or visitation.

## The vocabulary rulings, from consumers

`hero_generator/features.py` and `story_web/facts.py` both carry the same warning: *"`villain:prior_age` is NOT a reference to a super villain. `icarus_sim.terrain_villains` …"* — the word "villain" is overloaded across packages and `villain:prior_age` means a dispossessed claimant, not a super villain. *"`villains.fallen` answers that and this does not."*

`key_locations/core/world.py`: *"A fallen villain no longer stands anywhere, so its own position is dropped. Its **claims are kept**: ground a villain took stays taken, and the places that grew around it do not become unremarkable because it died. That asymmetry is a decision, not an oversight."*

`terrain_liveness.py`: *"The villain rule is authored in the module that writes it, including its own absent-reads-as-standing clause. Two implementations of one rule is the defect this module exists to prevent, so it delegates rather than restating."*

## Notes for the C++ implementation

Read `include/fwg/` before starting; nothing below invents a facility the library does not already name.

## Geometry maps directly

Every distance here is `radius * acos(clamp(dot(a,b), -1, 1))` on unit 3-vectors. `fwg::vec3` already exists; add a free `great_circle_m(double radius, const Vec3& a, const Vec3& b)` and use it in all six places rather than re-inlining, but **keep the identical clamp-then-acos form** — a `std::atan2`-based great-circle formula is more accurate and would therefore not reproduce the reference's behaviour at the tie boundaries where `distance < best` decides an anchor.

`direction(x, z, n)` is already implied by `fwg::sphere_grid`, which gives one node per distinct cell with per-node area and neighbour distances. Note that these modules index `water['nodes']` — a flat `(x, z)` list, one entry per distinct cell — which is exactly `sphere_grid`'s node list. A `Grid` here is only ever read as `layers['ley_<school>'][z][x]`; no Grid-level operations are needed.

`_offset(up, angle, bearing)` is a small Gram-Schmidt + cross-product routine with a pole guard. Write it once in a shared header; **do not** write it twice, which is what the reference did.

## What has no C++ equivalent

**`random.Random` with an integer seed and `.random()` / `.uniform()`.** The port owns `fwg::Rng` (xoshiro256++, `child` for named streams). This is a clean rewrite, so numbers need not match — but the *stream discipline* does. Specifically:

- One `Rng::child(seed, "corruption-<index>-<god_id>-escalation", encounters)` for the escalation roll, one draw.
- One `Rng::child(seed, "corruption-<index>-<god_id>", variation)` for the cluster bearing, one draw.
- One `Rng::child(seed, "visitation-<index>-<god_id>", variation)` for the avatar bearing, one draw, taken **before** the school loop.
- **One child stream per city** in `divine_lottery`: `Rng::child(seed, "visitation-<index>-<god_id>-<city_uid>", variation)`, one `random()` each. Do **not** reuse a single stream across cities — the per-city derivation is what makes a city's outcome independent of how many other cities are in reach and in what order they are visited. `Rng::draws()` gives you a direct assertion for that.
- Villains take **no** draws at all. A port that needs one has a bug.

Use `child_seed64` / `Rng::child` for all four. Do not invent a fifth derivation: the header is explicit that three exist and *"a porter who implements three of four gets a world that is correct except where it is not."*

**`round(x, 9)` / `round(x, 6)` / `round(x, 3)`.** CPython's `round` is banker's rounding on the *decimal* representation via a correctly-rounded double conversion. `std::round(x * 1e9) / 1e9` is not the same function. Since this is a clean rewrite, pick one helper (`round_dp(double, int)`), use it everywhere the reference rounds, and document that the tie behaviour differs. The rounding is **not** cosmetic in one place: `block['tiers']` stores 9-dp values that are read back next age and fed into `tier * 0.9 + rise * p`, so the rounding is part of the recurrence.

**`dict` insertion order as an output contract.** `std::map` is not it. Where the Python code emits a map that it *sorted* (`block['tiers']`, `{k: … for k in sorted(...)}`), `std::map<std::string, double>` is correct. Where it emits a map in insertion order (`magic.networks`, a villain record, a claim record), use an ordered-field struct and serialise the fields in a fixed order — the JSON writer the library already has should take a field list, not a `std::map`.

**`sorted()` is stable; `std::sort` is not.** Use `std::stable_sort` everywhere, or make every comparator a strict total order. The hazardous ones: `regions()`'s `sorted(cultures, key=str(id))`, `_seat`'s `(-turmoil, uid)`, `promote`'s `(-concentration, anchor)`, `divine_lottery`'s implicit reliance on `sorted(nests, key=id)` inside `fantasy_nest_threats`. Note that `-concentration` as a sort key on doubles has a `-0.0`/`+0.0` subtlety that Python's `<` and C++'s `<` both handle identically (they compare equal), so the secondary key decides — which is why there always is one.

**`max(people, key=lambda p: (p.tier, p.uid))`** keeps the *first* maximum in Python. `std::max_element` also returns the first. But `_host`'s key is `(tier, uid)`, so the uid is part of the ordering, not a tie-break on equal keys — the result is the lexicographically greatest uid among the highest tier. Implement it as `std::max_element` with a comparator on the pair.

**`max(sorted(totals), key=totals.get)`** in `dominant_pressure` — this is `max_element` over a pre-sorted key vector `{"ley","nest","war"}`, keeping the first maximum. Write it as an explicit loop over that fixed vector with strict `>`.

**Dynamic JSON blocks.** `result['villains']`, `religion.corruptions`, `religion.visitations` are heterogeneous documents. `fwg::World` holds "layers plus typed blocks", so define `struct VillainBlock { int version; std::vector<Villain> people; std::map<std::string,double> tiers; Outlook outlook; std::vector<FallenMark> fallen; std::optional<std::vector<Work>> works; }` and give each a `to_json`. The optional-field problem is real: `status`, `fell_age`, `god`, `corrupted`, `well`, `claims`, `works` are all sometimes absent, and *absence has meaning* (`status` absent reads as living). Use `std::optional` and preserve the absent/present distinction in the writer — do not normalise to a default on write, or every world's bytes move.

**`copy.deepcopy`.** Value semantics make most of this free. But note two places the reference relies on *shallow* copying: `advance`'s `living = {v.region: dict(v) …}` shares the `log` list and the `claims` list with the originals, so `villain['log'].append(...)` mutates through. In C++ with value types this simply works out the same because the block is rebuilt from `living` and `fallen` at the end. `_drain`'s `net = copy.deepcopy(net); net['nodes'] = remaining` leaves the stored network's nodes aliasing the pre-copy objects while its edges are copies — functionally irrelevant with value semantics.

**`math.acos` / `math.cos` / `math.sin` / `math.sqrt`.** Bit-for-bit parity is explicitly not required. But `math.hypot` is not used here and `sum()` over a generator is — the compensated-summation trap from elsewhere in this port does **not** apply, because every sum in these three modules is over at most a handful of terms (`concentration` over a region's cities, `Σ weights` over ≤ ~8 causes, `interest('living')` over all cities — that last one is the only long sum, and it is a population total whose tiny differences cannot cross the `/200000` saturation boundary in practice).

**`edit_network`** is copy-on-write with validation: unique non-empty id, 3 finite components, `|len²−1| ≤ 1e-6`, intensity finite in `[0,4]`. Porting it as a copy per call is O(n²) in the cluster loop (4 and 5 calls respectively, so it does not matter); a `push_back`-with-validation is behaviourally identical and should be preferred.

**The four hidden gods are data, not code.** Ship `hidden_pantheon.json` as a data file (the library has a JSON *writer*, not a reader — so either embed the four as a `constexpr` table in C++, or add a minimal reader. Embedding is defensible here: four entries, fixed schema, and the comment about the separate file exists only to keep `catalogue_identity()` stable, which the C++ port does not yet have.)

**`rule_holds`** is a small recursive predicate over a tagged union. Only five kinds are reachable from the hidden four (`always`, `magic_enabled`, `any`, `nest_family`, `ruins`), but the full set is 18; implement the whole thing or the known pantheon will not resolve either.

**`perf_counter()`** → `std::chrono::steady_clock`. The reference writes unrounded ms for visitation and 3-dp-rounded ms for corruption and cleanse; that asymmetry is not meaningful, pick one.

**Exceptions.** The two request APIs raise `terrain_errors.RequestError` (a `ValueError` subclass) carrying `field`, `received`, `expected`, `suggestion`, `retry`. Port that as a single exception type with those fields; the refusal *messages* are user-visible and worth carrying verbatim.

## Sequencing that will bite

`rebuild_tail` is called from inside `corrupt`, `_drain` and `summon`, and it re-runs settlement founding, biomes, nests, threats and roads. In the C++ port those stages do not exist yet (stage 5 of 16). **These three modules cannot be meaningfully ported before stages 6-15**, and specifically before `rebuild_tail`. Port them last among the pipeline modules; the spec above is complete enough to implement against whatever `rebuild_tail` becomes.

`_snapshot` needs `build_stages`, a debug/inspection facility the C++ port does not have. It is optional behaviour (it returns immediately when the key is absent) and can be deferred.

`_finish` calls the three planners, which fork a process pool controlled by the undocumented `ICARUS_CITY_WORKERS` environment variable (`city_planner.py:537`, `hamlet_planner.py:302`). Nothing ever proved pooled output equals serial. The C++ port should run these serially and treat that as the definition, not as a fallback.

## Open questions

Nine items. The first four are **confirmed defects** — I reproduced them by running the code, not by reading it.

## 1. CONFIRMED — an age advance over a corrupted world raises

A hidden god revealed by `_reveal` gets `status: 'walking'` but **no visitation record**. `terrain_history.age_transition` does, inside `if cfg.magic_enabled:`:
```python
for god_id in walking_gods(result): depart_god(result,cfg,god_id,age,rebuild=False)
```
`walking_gods` returns every god with `status == 'walking'`, including the hidden one; `depart_god` requires an undeparted visitation record and raises when there is none. Reproduced:

```
seed 42, size 17, villain_rise 1.0 → corruption_request(encounters=40) → acted, god_shape_beneath walks
→ advance_age_request({'steps': 1})
ValueError: No undeparted visitation for god_shape_beneath; that god does not walk by visitation
  terrain_history.py:381  for god_id in walking_gods(result):depart_god(...)
  terrain_visitation.py:385  raise ValueError(...)
```
Both request validators guard this path carefully and by name (`validate_cleanse`, `validate_visitation`), but `age_transition` calls `depart_god` directly with no guard at all. **A corrupted world cannot advance an age.** The port must decide: filter `walking_gods` to gods with a visitation record, or make `depart_god` a no-op when there is none, or give a corrupted god a distinct status. The comments show the author was aware of exactly this asymmetry in two other places and did not close it here.

## 2. CONFIRMED — corruption's failure modes are erased by its own rebuild

`_failure_mode` mutates live city dicts: blood writes `population_estimate`, eldritch writes `taken_by`. `corrupt` then calls `rebuild_tail`, which runs `add_settlements`, which rebuilds every site from scratch and carries forward only
```python
CARRIED_SURVIVOR_KEYS = ('uid','source_culture','founded_age','name','war_history',
                         'absorbed_refugees','absorbed_bands', FOUNDED_BY)
```
Neither `population_estimate` nor `taken_by` is in it, and `add_settlements` unconditionally re-derives `population_estimate` from the habitat budget. Reproduced on the reference world with an eldritch corruption: the record's `touched` list names three cities as `taken`, and afterwards `[c['uid'] for c in out['settlements']['sites'] if c.get('taken_by')]` is `[]`. The same mechanism erases blood's farming.

So two of the four gods' signature effects survive only as narration inside `religion.corruptions[i].touched`. The tests never catch it because `FailureModeTests` calls `_failure_mode` directly and never goes through `corruption_request`. **Decide for the port:** either add both keys to the carried set, or apply the failure mode *after* the rebuild, or accept the record-only semantics and say so.

## 3. CONFIRMED (by inspection) — the misindex guard in `_drain` is dead

```python
if any(e['from'] in targets or e['to'] in targets for e in net['edges'] if isinstance(e.get('from'), str)):
    raise ValueError('A corrupted node carries an edge; removal would misindex this network')
```
Edge endpoints are **integer positions** everywhere in this codebase (`network_geometry`, `sink_well`, `depart_god`'s remap all treat them as ints), so `isinstance(e.get('from'), str)` is never true and the guard never fires. The condition it wants to test is "does any surviving edge reference a node index that `remaining` is about to renumber", which is a different predicate entirely.

This is not academic. `corrupt` sets `host['school'] = god['school']` — a **hidden** school — and `host['god'] = god['id']`. At the next age boundary, `build` → `sink_well` runs with `school = 'blood'`/`'void'`/`'rot'`/`'eldritch'` and appends a well node **plus edges from the well to every node in `held_nodes`**, which now includes the four `corrupt-*` ids. A subsequent `_drain` then removes those nodes by filter without remapping, and every edge endpoint index in that hidden network points at the wrong node. `depart_god` carries the correct remap and its comment even names this as *"the same latent trap depart_god carries."*

The port should use `depart_god`'s remap in `_drain`, or make the guard test the real predicate. (Reaching this requires an age advance on a corrupted world, which item 1 currently blocks — so the two defects mask each other.)

## 4. CONFIRMED (by inspection) — corruption's ruins seed no key point

`corrupt` writes `new_node_school: god['school']` and a full `legacy` on each rot/void ruin, exactly as an age fate does. But `age_transition` is what turns `new_node_school` into an actual ley node (`edit_network(net, new_node={'id': ruin['id']+'-key', ...})`), and `visitation.summon` does it explicitly for its own ruins. `corrupt` does neither. The field is written and never acted on. Verified: on a corrupted reference world, `ruin-<uid>-key` appears in no network. Either add the node (matching visitation) or stop writing the field.

## 5. `held_nodes` is only cleaned on the final cleanse tick

`_drain` filters `person['held_nodes']` by `removed` only inside `if cleansed:`, and `removed` is this tick's removals alone. Nodes removed on earlier partial ticks stay in the villain's `held_nodes` forever, and `sink_well` would then try to join an edge to a node that no longer exists (it iterates `net['nodes']` and checks membership in `held`, so it silently skips — no crash, but the record is wrong). Accumulate removals on the record, or filter unconditionally each tick.

## 6. `cleansed_age == 0` is falsy in the budget counter

`validate_corruption` computes `held = sum(len(c['nodes']) for c in religion.get('corruptions', []) if not c.get('cleansed_age'))`. A record cleansed in age 0 still counts against `NODE_BUDGET`. `validate_age_world` forces phase ≥ 13 (two age transitions), so `age >= 2` in practice and this cannot fire today — but `validate_cleanse` correctly uses `is not None` for the same field two hundred lines later, so the two disagree. Use `is not None`.

## 7. `_reveal`'s effect on known rivals does not survive the next `add_religion`

`_reveal` pushes rival *known* gods to `aspect: 'wild'` and appends `roused_by`. Both are written after `add_religion` in `corrupt`, so they hold until the next resolve. But `resolve_gods` rebuilds every known god record from scratch, recomputing `aspect` from `aspect_of` and dropping `roused_by` entirely. `hidden_records` carries the *hidden* god's status/aspect/evidence forward from the prior block, so the corrupted god keeps walking — but its roused rivals quietly revert at the next age boundary or visitation. Given that the "cure worse than the disease" argument rests on Wild rivals, this looks unintended. Is `roused_by` meant to persist?

## 8. `favor` on a city does not survive a visitation's own rebuild

Same mechanism as item 2: `divine_lottery` writes `city.setdefault('favor', []).append({...})` on a deepcopy that becomes a survivor, and `rebuild_tail` drops the key. The *report* half does survive (`report['divine_favor']` and the reduced `regional_threat` are written to `threat_assessments` **after** the rebuild, which is why that half works). So `favored` lives on the visitation record and on the threat report but not on the city. Probably deliberate given the ordering, but the two halves being written in two different places with two different lifetimes is worth stating in the port.

Also in `summon`: `favored_uids = {f['city_uid'] for f in favored}` is computed and never used.

## 9. Smaller dead or vestigial things a porter should not faithfully reproduce

- `terrain_villains.FOUND_SPACINGS = .8` — declared, commented, never read anywhere in the repository.
- `terrain_corruption` imports `direction` from `terrain_globe` and never uses it.
- `terrain_visitation` imports `tide` from `terrain_astrology` and never uses it (`day_state` already returns the tide).
- `terrain_visitation._failure_mode`-equivalent: `_failure_mode(result, god, host, reach, age)` in corruption takes `age` and never uses it.
- `terrain_visitation.state_keys()` adds `OWN_STATE = ('astrology','lunar_almanac','religion')` to `terrain_history.STATE_KEYS`, but all three are already the last three entries of `STATE_KEYS`, so the function returns exactly `STATE_KEYS`. The docstring says "until the pipeline lists them"; the pipeline now does.
- `corruption_request` reports `'a watching god did not act this time'` when `candidates` is non-empty but `host is None` — nothing was rolled, and the message says otherwise.

## One thing I could not settle

The relationship between `villain_hold`, `SUPER_TIER` and `STALLED_TIER` is asserted by the test suite (`hold < SUPER_TIER`, `STALLED_TIER < SUPER_TIER`) but `villain_hold`'s declared range is `[0, 1]`, which *includes* `1.0`. At `villain_hold = 1.0` exactly, `tier < hold` is satisfied by any tier below 1.0, so a villain that decays by a single epsilon falls immediately and the hysteresis band is empty. Nothing refuses that configuration. Whether `1.0` should be excluded from the range is a ruling I do not have.

## Addenda from the audit

The reader above wrote the specification; a second reader went back to the source
to find what it had left out or got wrong. Both are kept: the omissions are
content the implementer needs, and the corrections say where the specification
above is not to be trusted.

### Corrections

- **Claimed:** Spec D8, `depart_god`'s restore loop, given as: `for item in record.restore: pool = net.nodes if item has 'node_id' else net.edges; target = first element with id == ...`
  **Actually:** `net` is never re-bound in the spec's pseudocode, so it is stale — it holds whichever network the compaction loop left, or the footprint network `record.schools[0]`. The source re-reads the network per item: `for item in record['restore']: net = result['magic']['networks'][item['school']]; pool = net['nodes'] if 'node_id' in item else net['edges']; target = next((v for v in pool if v['id'] == item.get('node_id', item.get('line_id'))), None); if target is not None: target['intensity'] = item['intensity']`. A `restore` list spans several schools (one per suppressed opposed school), so an implementer following the spec restores into the wrong network and every suppressed rival school stays permanently halved after the god departs — silently, because the `next(..., None)` lookup just misses and skips.

- **Claimed:** Constants section, under "From imported modules, load-bearing here": `REFERENCE_RADIUS_M = 1774.4123532462844` (the ley-width reference world, 11.15 km around)
  **Actually:** It is not load-bearing in any of these three modules, nor in anything they call on these paths — none of `terrain_villains`, `terrain_corruption`, `terrain_visitation` reads it directly or indirectly. What *is* load-bearing and is missing is `terrain_scale.REFERENCE_CIRCUMFERENCE_M = 11148.961626795008`, which is what `reach_scale()` divides into the world's circumference to resolve `settlement_spacing` (see the `missing` entry).

- **Claimed:** D6: `fantasy_nest_threats(...)`: `reach = min(radius*0.5, max(350.0, (range_m or spacing_m or 350.0)*2))`; `weight = 0.11 * tier * (1 - distance/reach)` (`tier` here is the nest danger tier, 1..5).
  **Actually:** The source uses key-absence fallbacks, not falsiness, and `tier` has a default of 5, not none: `reach = min(radius*.5, max(350., nest.get('range_m', nest.get('spacing_m', 350.))*2))` and `weight = .11 * nest.get('tier', 5) * (1 - distance/reach)`. A nest carrying `range_m: 0` gets reach 350 m under the source but would fall through to `spacing_m` under the spec's reading, and a nest with no `tier` key weighs as the *maximum* danger tier (0.55 at the centre), not as absent.

- **Claimed:** D7: `if day is None: day = int(settlements.founding.end_year or 0) * DAYS_PER_YEAR`
  **Actually:** The source is `int((result['settlements'].get('founding') or {}).get('end_year', 0)) * DAYS_PER_YEAR`. The `0` is a *key-absent* default; an explicitly present `end_year: null` raises `TypeError` rather than defaulting to day 0. The `or {}` guard is on `founding`, not on `end_year`.

- **Claimed:** C++ notes: `rule_holds` ... "the full set is 18"
  **Actually:** There are 17 rule kinds before the terminating `raise ValueError('Unknown existence rule ' + kind)`: always, magic_enabled, magic_disabled, school, race, races, civilization_city, ocean_share, sky, dragon, nest_family, ruins, diaspora, primordial_count, gods, any, all. (The `gods` kind reads `rule.get('all', True)` to choose between `all(...)` and `any(...)` over the `manifest` set.)

- **Claimed:** C12: "`target` a dict with exactly one key from `{corruption, god_id}` else `cross_field`"
  **Actually:** Two checks, two different errors, in order: `if not isinstance(target, dict): raise wrong_type('target', target, {'type':'object'})` first, then `if len(target) != 1 or not set(target) <= {'corruption','god_id'}: raise cross_field(...)`. Same two-step shape in `validate_visitation` for its `{city_uid, node}` target. Also unstated: `validate_cleanse` returns `(cfg, target, float(power))`.

### Missing — blocking

**LEY_DESTRUCTION — the eight ruin `reason` strings a ley-caused divine-lottery ruin carries verbatim**

`divine_lottery` appends `(ley['school'], ley_pressure, LEY_DESTRUCTION[ley['school']], ley)` and, if that cause wins the selector walk, writes `reason = chosen[2]` straight onto the ruin. The spec names the constant and gives none of its values. They are: weave → 'An arcane surge tore the city apart.'; umbral → 'Necrotic entropy extinguished the city.'; infernal → 'Infernal corruption and demonic incursions drove its people away.'; radiant → 'An overwhelming radiant surge forced the city to be abandoned.'; fire → 'A fire leyline engulfed the city in flames.'; water → 'A water leyline drowned or froze the city.'; earth → 'An earth leyline let roots and stone reclaim the city.'; air → 'An air leyline shattered the city with storms and force.' Note the table has only the known eight — a hidden school winning `dominant_school` would KeyError. The ley cause's evidence is the whole `{'school': winner, 'potency': potencies[winner]}` dict, and its `kind` is the bare school name (which is why `source_school`'s first rule `if cause in SCHOOLS: return cause` fires for it).

**The nest cause's `kind` values and `reason` string in divine_lottery**

`divine_lottery` does `causes.append((threat['kind'], threat['weight'], threat['reason'], {...}))` and the spec never says what `kind` or `reason` are. From `fantasy_nest_threats`: `dragon = 'dragon' in nest.get('name','').lower()`; `kind = 'dragon' if dragon else 'monster'`; `reason = nest['name'] + ' drove the inhabitants away.'`. Eligibility is `dragon or nest.get('family') in ('infernal','undead','aberrant')`, after skipping `nest.get('layer','surface') != 'surface'` or `nest.get('real', True)` (i.e. real nests are skipped by default). The threat dict also carries `name`, which the visitation evidence dict deliberately drops — evidence is only `{nest_id, family, distance_m, reach_m}`. `chosen[0]` becomes the ruin's `cause`, so getting these wrong changes the ruin record and the school resolution below it.

**terrain_ruins.source_school — the full school-resolution ladder that decides `legacy.school`, and hence `new_node_school` and where the visitation plants each ruin's key point**

The spec gives CLASS_INTENSITY/SELF_MAGIC_FLOOR/DIVINE_INTENSITY and the dominant_school thresholds but not the resolution itself. In order: `if cause in SCHOOLS: return cause` (the ley cause IS a school name); `if cause == 'self_magic': return 'weave'`; `if cause.startswith('divine'): return god_school` (= `schools[0]`); `if cause.startswith('villain'): return villain_school` (= `evidence['school']`); `if cause.startswith('war_'): return culture_schools().get(victor_culture)`; else `school = NEST_FAMILY_SCHOOL.get(nest_family)` with `NEST_FAMILY_SCHOOL = {'infernal':'infernal','undead':'umbral','aberrant':'weave','fey':'weave','holy':'radiant','unholy':'infernal','draconic':'fire','primordial':'primordial','fantastic':None}`; and `if school == 'primordial'`, return `min(('fire','water','earth','air'), key=lambda s: (-potencies.get(s,0.), list(SCHOOLS).index(s)))` — strongest element at the ruin, dead ties broken by taxonomy order. Then `ruin_legacy`: `basis='source'`; if school is None → `dominant_school(potencies)` with `basis='region'`; if still None → `culture_schools().get(city['population_profile'], 'weave')` with `basis='culture'`. Intensity = `CLASS_INTENSITY.get(city['city_class'], 1.5)`, raised to `max(.., 2.5)` for self_magic and set flat to 4.0 for any `divine`-prefixed cause. Without this, `divine_lottery` cannot emit `legacy`, `new_node_school`, or the `<ruin id>-key` node — and that node changes the ley field, so every downstream stage diverges.

**The contents of `religion.visitations[i].moon`**

`moon = {'day': day, **day_state(result['astrology']['moon'], day)}` — nine keys, of which the spec gives only `nod_degrees`. `cycle_fraction(moon, key, t) = ((t + offsets[key]) % periods[key]) / periods[key]` (integer arithmetic on integer days). `day_state` returns: `phase_fraction = cycle_fraction(moon,'synodic',t)`; `elongation_degrees = 360 * phase_fraction`; `illumination = (1 + cos(phase_angle)) / 2` where `phase_angle = 2π*phase_fraction`; `moonrise_hour = HOURS_PER_DAY * phase_fraction`; `spin_fraction = cycle_fraction(moon,'spin',t)`; `nod_degrees = tilt_max_degrees * sin(2π * cycle_fraction(moon,'nod',t))`; `leaning = 'still' if nod_degrees >= 0 else 'restless'`; `tide = tide(moon, t)`, a per-school multiplier dict over all twelve schools computed as `min(TIDE_MAX, max(TIDE_MIN, TIDE_FLOOR + TIDE_GAIN * visible * lit))` from the moon-frame region vectors rotated by spin about y then leaned by nod about z. An implementer cannot write the visitation record without these.

### Missing — important

**A god whose affinity contains an opposed pair suppresses its own avatar cluster — and one such god ships**

`god_outer_unnamed` has `school_affinity: ['weave','umbral']`, and `OPPOSED` maps weave↔umbral. `suppress_opposed` runs *after* `avatar_cluster`, reads `result['magic']['networks'][opposed]` live and mutates in place, so for that god iteration 1 (school=weave, opposed=umbral) scales every umbral node within reach — including the five `avatar-god_outer_unnamed-<i>-umbral-<k>` nodes placed moments earlier at intensity 4.0/3.0 — by `1 - 0.5*wrath`, and iteration 2 does the same to weave. Their pre-scaling intensities go into `restore`, where departure's lookup silently misses them because the cluster nodes have already been deleted. `god_red_field` (['fire','infernal']) does not self-suppress, since its opposites are water and radiant. The spec asserts only that "rival schools" are weakened; a porter who reads that will exclude the god's own cluster and produce a different world for that god.

**How `settlement_spacing` is actually derived — the answer to the pre-/post-scaling question the spec only asserts**

Recipe 3 resolves it in `terrain_world` as `settlement_spacing = 450.0 m * reach_scale(circumference)`, where `reach_scale` normalises against `terrain_scale.REFERENCE_CIRCUMFERENCE_M = 11148.961626795008 m`. It is deliberately NOT clamped (the old `min(REACH_MAX_M, raw*factor)` was a defect: past the binding width the reach stopped growing with the planet and cultural regions quietly stopped scaling). Instead, resolving above `REACH_MAX_M = 100000 m` raises `RequestError('STATE_CAPACITY', ...)`; for settlement_spacing that binds at a 2477.55 km circumference (support_reach at 1114.90 km, culture_link_cost at 619.39 km). An explicit override in `overrides` skips the derivation entirely. `apply_world_scale` does not touch it, exactly as the spec says. On the 200 km reference world: 450 * (200000/11148.961626795008) = 8072.500651872125 m, which is the spec's figure. Without the formula a porter can reproduce only the one world the spec quotes.

**`edit_network`'s exact contract**

Given as a one-line summary. Full: `edit_network(network, *, node_id=None, line_id=None, intensity=None, new_node=None)`; it deep-copies the whole network first; exactly one of `node_id`/`line_id`/`new_node` must be non-None else `ValueError('Select one node, line, or new key point')`; for `new_node` the key set must be EXACTLY `{'id','direction','intensity'}` (extra or missing keys raise `ValueError('New node requires id, direction, intensity')`); id must be a non-empty str and unique within THIS network; direction must be a list/tuple of 3 finite int/float with `abs(sum(v*v)-1) <= 1e-6`; the node is deep-copied before append; and finally `intensity` must be finite and `0 <= intensity <= 4` else `ValueError('Intensity must be finite and between 0 and 4')`. The intensity check runs on all three paths, so it also gates plain intensity edits.

**`_finish` writes `result['debug_stats']`, and which request paths call it**

`_finish(result)` calls `fill_cities`, `fill_hamlets`, `fill_castles` and then `result['debug_stats'] = build_debug(result)`. The WRITES section never lists `debug_stats`. It is called on the visitation summons path and the departure path only — `corruption_request` and `cleanse_request` do NOT call it, so a corrupted or cleansed world comes back with the four plan keys dropped and never refilled, and with a stale `debug_stats` from before the request. That asymmetry is a behavioural difference between the two APIs the spec does not record.

### Missing — minor

**`_snapshot` subscripts `result['phases']`**

After appending the stage it does `result['phases']['titles'] = [...]` and `result['phases']['completed'] = stage` by subscript. The guard at the top is only `if 'build_stages' not in result: return` — a world carrying `build_stages` but no `phases` raises KeyError out of a validated API. The spec states the `build_stages` guard and stops there.

**`walking_gods` lives in terrain_visitation and is part of its public surface**

`def walking_gods(result): return [g['id'] for g in result.get('religion', {}).get('gods', []) if g['status'] == 'walking']` — note `g['status']` is a subscript, so a god record without a status key raises, and note the `.get` chain means a world with no religion block yields `[]`. It is the function `terrain_history.age_transition` iterates to drive `depart_god`, i.e. the entry point of the spec's confirmed defect #1. The spec references it in open questions but never specifies it as an output of this module.

**`validate_visitation` passes an UNSORTED allowed-field list to its unknown-field error**

`unknown_field(sorted(set(body) - set(VISITATION_FIELDS))[0], VISITATION_FIELDS, noun='request field')` — the offending field is the first alphabetically (matching corruption), but the *allowed* list reported back is `VISITATION_FIELDS` in declaration order `('api_version','world','god_id','target','wrath','variation','depart','day')`, not sorted. `validate_corruption` and `validate_cleanse` both pass `sorted(allowed)`. Ordering hazard #26 states the sorted behaviour as general.

**A freshly-created villains block carries `outlook: []` — a list, not a dict**

`result.setdefault('villains', {'version': 2, 'people': [], 'tiers': {}, 'outlook': [], 'fallen': []})`. `outlook()` returns a dict and `age_transition` overwrites the key with it, so the empty list is only observable on a world where `advance` wrote the block and the caller never ran the outlook pass. The WRITES schema shows `'outlook': { ... }` unconditionally; a C++ `struct Outlook` would normalise this away and move the bytes of such a world.

**Unused parameters that constrain the C++ signatures**

In addition to corruption's `_failure_mode(..., age)` that the spec flags: `terrain_villains.sink_well(result, cfg, villain, age)`, `claim_settlements(result, cfg, villain, age)` and `outlook(result, cfg, rise, density)` all take `cfg` and never read it (spacing comes from the caller in `advance`/`promote`; radius from `result['effective_config']`). `terrain_visitation.reach_m(world, cfg)` ignores `world`. `divine_lottery(result, cfg, ...)` ignores `cfg` and takes `seed` separately. Harmless, but a porter matching signatures will wire dead arguments through.
