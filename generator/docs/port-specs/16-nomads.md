# Port specification: 16-nomads

Extracted from the Python `FantasyWorldGenerator` at commit `b67c99b` before that
repository was retired, then audited against the same source by a second reader.

**Source modules**

- `Sim/icarus_sim/terrain_nomads.py`
- `Sim/icarus_sim/terrain_nomad_routes.py`
- `Sim/icarus_sim/terrain_nomad_effects.py`

**Estimated C++ size:** roughly 2600 lines, excluding tests.

## Audit: NOT YET IMPLEMENTABLE

This is an unusually strong specification — I walked the algorithm section line-by-line against all three modules, and the gates, the thinned point process, the six route builders, `_subdivide`, `_assign_days`, fission and all four write-backs are reproduced with their exact constants, tie-breaks, loop bounds and initialisations. The ordering-hazard and reasoning sections carry the source's measurements verbatim (the 34730.3/40980.3 m subdivide measurement, the 3.54-vs-0.54 ley-queue number, the 0.32–0.57 round forage, the 27%/9%/45% rich_forage calibration, the seed-42-size-17 absorption result). I verified the pipeline claims at their call sites and they hold exactly: the stage-16 ordering in terrain_history, the 360/30/360 tick cadences, `absorb_survivor_camps` immediately before `rebuild_tail`, `CARRIED_SURVIVOR_KEYS` containing `absorbed_refugees`/`absorbed_bands`, `residents += absorbed_refugees` after the capacity split, `nomads` in STATE_KEYS, `regional_threat` read as `defense_priority` by city_planner, and the two request-API seed domains. I mark it not-implementable for one narrow reason: `cfg.road_max_grade` is named as an input but its value, unit and range are never given, and `travel_cost`'s impassability ceiling is `road_max_grade * GRADE_RELIEF`. With no number an implementer must guess, and plausible guesses (0.1 vs 1.0, or reading "grade" as degrees) differ by an order of magnitude in which ground a band may cross — which changes how many bands strand, which changes the fission candidate set, which changes the band population. That is precisely this subsystem's own stated failure mode: it does not raise, it just builds a different world. The remaining gaps are output content rather than behaviour — five of six classifications' `seeks`/`carries` arrays and the three schema-REQUIRED `method`/`limits` strings are elided — plus one factual error in the `biome` derivation. Close those five entries and the document stands alone.

**1 blocking gap(s)** were found and are recorded in the addenda below.

## Purpose

Nomads are the travelling population of a finished world: six kinds of band (survivors, deserters, cultists, merchants, bandits, wanderers) that are placed on habitable land by a thinned point process, classified by a hard geographic reading of the ground around the start point, given a walked circuit whose shape follows from the classification, scheduled against the calendar, and then allowed to write four things back into the world.

The design rule that drives everything is "the land decides who walks it." A classification is not rolled and then placed; the ground is read first and a classification whose preconditions fail is ineligible outright, whatever its weight. A candidate point that satisfies no gate raises nobody, and that is a result rather than a failure (roughly one candidate in eight produces nothing). The obvious inverse implementation -- roll a kind, then find somewhere to put it -- produces cults where nothing is sacred, caravans with no road and no market, and bandits in open country with nothing to rob; those are not balance bugs that tuning fixes, they are the feature being wrong.

The subsystem is three passes that run in a fixed order: `add_nomads` (place and classify), `add_nomad_routes` (circuits, seasonal camps, day windows, then lineage fission), `apply_nomad_effects` (write-backs). A fourth entry point, `absorb_survivor_camps`, runs at an age boundary from the history pass rather than from the effects pass. A fifth, `nomad_request`, is a stateless JSON API that raises one band at a caller-chosen node so that villains can spawn cultists without villains and nomads importing each other.

## Where it sits in the pipeline

RUNS AT STAGE 16, the last stage of the sixteen-stage ladder, after everything else in the world exists. FWGC++ today reaches stage 5, so essentially the whole prerequisite chain is unported.

Must already exist before `add_nomads` runs:
  - `layers` (terrain fields): `height` (m), `slope` (DEGREES), `tpi` (m), `temperature` (C), `food_potential` (0..1), `natural_food_potential` (0..1), `water_type`, `salinity`, `moisture`, `water_depth`, `river`, `maritime`, `biome`, `natural_biome`.
  - `effective_config.globe_radius` (m).
  - `settlements.sites[]` (stage ~10) with `uid`, `id`, `node`, `x`, `z`, `direction`, `population_estimate`, `urban_population_estimate`, `rural_population_estimate`, optional `war_history`.
  - `humans.hamlets[]` (stage ~11) with `id` (ordinal, e.g. `hamlet-7`), `uid` (`hamlet-node-<i>`), `node`, `x`, `z`, `delivered_food`.
  - `roads.routes[]` with `nodes[]` (graph node indices) and `length_m`.
  - `magic.networks{school: {nodes:[{id, direction, intensity}], edges:[...]}}` -- twelve networks, eight known (`radiant water earth umbral weave fire air infernal`) plus four hidden (`blood void rot eldritch`).
  - `religion.gods[]` (with `id`, `schools[]`, optional `exists.school`, `family`) and `religion.sites[]` (with `id`, `kind`, `god_id`, `node`).
  - `villains.people[]` with `uid`, `school`, `tier`, `claims[]` where a claim has `id`, `direction`, `node`, `influence`.
  - `ruins[]` with `uid`, `id` (`ruin-<uid>`), `direction`, `node`, `destroyed_age`, optional `war_history`, optional `legacy.intensity`.
  - `history.ages[]` with `age` integers.
  - `seasonal_environment.months[]` -- twelve dicts each holding a `temperature` grid.
  - `threat_assessments.cities[]` with `city_uid`, `regional_threat` (for the raid write-back).
  - `timing_ms` dict with a `total` key.

ORDER AT STAGE 16 (terrain_history, both the generation path and the age-advance tail):
  fill_cities -> fill_hamlets -> fill_castles -> add_nomads -> add_nomad_routes ->
  add_beast_movements -> add_encounters -> apply_nomad_effects -> heroes, story web,
  npcs, key locations, key location plans.
Bands are placed before the packages that only read the world, so heroes/story-web/rosters see a world with bands in it.

Depends on it afterwards: `add_encounters` reads `nomads` (and `beast_movements`) to build the encounter index; the heroes/story-web/npc/key-location packages read `nomads.groups`. `city_planner` reads `threat_assessments[].regional_threat` as `defense_priority`, so raid pressure can move wall weight -- but only on a later rebuild, because raid pressure is applied AFTER the threat assessment was evaluated.

ALSO RUNS AT TWO MORE SITES, and this matters:
  1. The age-advance tail (`advance_age_request`, at `age == start_age + steps`). The whole block is REPLACED, never merged, because an aged world must reclassify against the world it actually has -- a carried band would name ruins that moved and refuges that no longer stand.
  2. A time advance (`terrain_time`), on cadences: `nomads` every 360 days, `nomad_routes` every 30 days, `nomad_effects` every 360 days. Each of those passes is invoked with a config whose seed has been replaced by `child_seed(cfg.seed, 'time-<cadence>', absolute_step_index)` -- a time-derived seed is what makes a band move at all between two ages. Consequence: `nomads` and `nomads.routes` are NOT stable between two reads of a live world. A consumer may cache them against `world_clock.day`; it may not cache them against the world's identity.

`absorb_survivor_camps` runs inside `age_transition`, immediately before `rebuild_tail`, after the fates are decided, against the list of cities that survived the boundary.

`nomads` is in `STATE_KEYS` (so `materialize_stage` snapshots it) and must also be in the lab's `historyStateKeys`; omitting the second does not raise, it silently shows the final bands at every earlier stage.

## Inputs

### add_nomads(result, cfg)

Gate: returns `result` unchanged unless `cfg.world_recipe` is truthy AND `cfg.phase >= 16`.

Config / options:
  - `cfg.size` -- n, the raster width.
  - `cfg.seed` -- 32-bit master seed.
  - `cfg.settlement_spacing` -- metres, POST-SCALING. The authored base is 450.0 m against an 11.149 km reference world (`REFERENCE_RADIUS_M = 1774.4123532462844`, circumference 2*pi*that = 11149.0 m); `terrain_world` multiplies it by `circumference_m / 11149.0` before the Config reaches here. At the three shipped presets (200/400/600 km circumference) that resolves to roughly 8073 / 16145 / 24218 m. EVERY nomad reach is expressed as a multiple of this value, so all nomad reaches are post-scaling absolute metres.
  - `options(cfg)['nomad_occurrence']` -- default 1.0, range 0..3. Overall multiplier on band count; 0 leaves the world settled.
  - `options(cfg)['nomad_density']` -- default 18.0, range 0..200, units bands per 1000 km2 of habitable land. Falls back to `policy()['candidate']['density_per_1000km2']` (also 18.0) when absent.
  - `options(cfg)['nomad_variation']` -- default 0, range 0..4294967295. Independent nomad seed variation.
  - `result['effective_config']['globe_radius']` -- metres.

World blocks, via `ground(result, cfg, radius, points)` which resolves everything once rather than per candidate:
  - `settlements.sites[]` -> three derived lists:
      `wealth`   = every site (key `uid`) plus every hamlet (key `h.get('id','')`, i.e. the ORDINAL), filtered to those with a resolvable direction.
      `markets`  = sites only, key `uid`.
      `wars`     = ruins with a truthy `war_history` (key `uid`) THEN sites with a truthy `war_history` (key `uid`), in that order, filtered to those with a direction. A war site is where a war was decided, which is the ground a deserter runs from; ruins carry the loser's history and surviving cities carry their own.
  - `humans.hamlets[]` -> contributes to `wealth` only.
  - `ruins[]` -> `fresh_ruins` = ruins whose `destroyed_age == latest_age`, key `uid`.
  - `history.ages[]` -> `latest_age = max(age['age'] for age in ages, default=0)`.
  - `magic.networks` -> `ley` = for each network name in dict order, for each node in list order, the triple `(tuple(node['direction']), node['id'], {'school': name, 'id': node['id'], 'intensity': node.get('intensity', 0.)})`. Intensity range 0..4.
  - `religion.sites[]` -> `shrines`, key `site.get('id','')`, direction resolved from `node`.
  - `religion.gods[]` -> read directly by `_god_for_school` and by the shrine branch of the cultist gate.
  - `villains.people[]` -> `claims` = for every villain (standing OR fallen) and every claim it holds, `(direction, claim['id'], {'claim': claim, 'villain': villain})`. DELIBERATELY not filtered to standing villains: held ground outlives its holder, so a cult raised on a claim does not disband because the villain that staked it fell. The villain record reached through `claim['villain']` may therefore be a fallen one.
  - `roads.routes[]` -> `roads` = each distinct node index appearing in any route, first occurrence wins, in route order then node-within-route order. Entry is `(direction(points[node]), str(node), node)`.
  - `settled_directions(result, cfg)` -> `settled` = directions of `settlements.sites + fisheries.ports + humans.hamlets`, in that concatenation order, entry `(d, str(i), d)` where i is the enumeration index.
  - `seasonal_environment.months[]` -> twelve `temperature` grids, read per candidate cell by `seasonal_swing`.

Per-cell fields, via `habitat_cells(result, cfg, points, areas)` (one entry per graph node, in node order):
  - `node` (int), `x`, `z` (grid coords), `area_km2` = `areas[i] / 1e6`, `biome` = `int(natural_biome or biome)`, `direction` (unit 3-vector tuple), and `fields` = every layer sampled at (x,z) plus derived keys.
  - Derived `medium`: `'marine'` if `water_type == 1`, else `'freshwater'` if `water_type` truthy and `salinity < 0.2`, else `'salt_lake'` if `water_type` truthy, else `'land'`.
  - Fields the nomad gates actually read: `medium`, `slope` (degrees), `tpi` (metres, signed), `height` (metres), `temperature` (Celsius), `food_potential` (0..1), `natural_food_potential` (0..1).

### add_nomad_routes(result, cfg)

Gate: returns immediately if `result['nomads']` is missing or `groups` is empty. THERE IS NO PHASE GATE HERE.
Reads: `result['nomads']` (its own output), `cfg.size`, `cfg.seed`, `cfg.settlement_spacing`, `cfg.road_max_grade`, `options(cfg)['nomad_variation']`, `effective_config.globe_radius`, `settlements.sites`, `humans.hamlets`, `magic.networks` (directions and school names only -- intensity is NOT re-read here), `ruins`, and the same `habitat_cells` fields plus `food_potential`/`natural_food_potential` for the travel cost.

### apply_nomad_effects(result, cfg)

Gate: returns unchanged unless `cfg.world_recipe` truthy AND `cfg.phase >= 16` AND `result['nomads']` exists.
Reads `cfg.magic_enabled`, `cfg.settlement_spacing`, `effective_config.globe_radius`, `nomads.groups`, `magic.networks`, `threat_assessments.cities`, `settlements.sites`, `roads.routes`.

### absorb_survivor_camps(result, survivors, age)

Reads `result['settlement_candidates']`, `result['nomads'].groups`, and the caller-supplied `survivors` list of surviving city site dicts. `age` is an integer.

## Outputs

### result['nomads'] -- written wholesale by add_nomads, schema Contracts/schemas/nomads.schema.json, version 1

```
{
  version: 1,
  groups: [band, ...],          # sorted by uid (LEXICOGRAPHIC, see ordering hazards)
  counts: {classification: int} # all six keys always present, so a quiet world is
                                # distinguishable from a broken pass
  rolls:  [roll, ...],          # diagnostic, in candidate-evaluation order
  method: "<long prose string>",
  limits: "<long prose string>"
}
```

A **band** (the key field is `uid`, NOT `id`):
```
uid               str, "nomad-<latest_age>-<node>" with "-<draw_index>" appended when a
                  cell raises more than one. Keyed to the TERRAIN NODE, never to an
                  ordinal or a culture id: those renumber at an age boundary and a band
                  has to survive one. Schema pattern ^nomad-[0-9]+-[0-9]+(-[0-9]+)?$
classification    one of survivors|deserters|cultists|merchants|bandits|wanderers
parent_uid        str|null -- null for a placed band, the parent's uid for a fission child
origin            {kind: "world"|"god"|"villain", id: str|null, age: int}
                  world generation always writes {kind:"world", id:null, age:latest_age}
god_id            str|null -- set for cultists, null otherwise
school            str|null -- the ley school, set for cultists
node, x, z        ints
direction         [x,y,z] unit vector
biome             int
size              int, head count, drawn uniformly from the classification's authored band
speed_m_per_day   float, ABSOLUTE metres per day (the one deliberate non-scaled distance)
column_length_m   float, round(size * column_length_per_head_m, 6). How far the band
                  strings out along its path, so a player meeting one on the move sees a
                  column rather than a point.
disposition       hostile|wary|tradeable|devout|fleeing
seeks             [str]
carries           [str]
camps             [camp], at least one
legs              [leg], empty until the route pass
basis             object -- the evidence that classified this band (see below)
route_status      "routed"|"stranded"  -- added by the route pass
round_length_m    float -- added by the route pass, round(sum of leg lengths, 6),
                  EXCLUDING any fission leg
```

A **camp** (key field `id`):
```
id          "<band uid>-camp-<index>"
node        int
direction   [x,y,z]
kind        winter|summer|base|station|lair|shrine|terminal|calving
            (calving is in the schema enum and in CAMP_REST but NO nomad builder ever
             produces it; it belongs to terrain_beast_movement. Dead for nomads.)
arrive_day  int|null, 0..359
depart_day  int|null, 0..359
```
A lair or a base is a place you can find and raid; a station or a seasonal camp is empty most of the year. A quest saying "clear the camp" is only valid for the former, which is why the band uid rather than the camp id is the durable handle.

A **leg** (no id; identified by `from`/`to` camp ids):
```
from        camp id
to          camp id
nodes       [int], a contiguous walk -- consecutive entries are graph neighbours
length_m    float, sum of great-circle distances between consecutive nodes (NOT the
            Dijkstra cost, which is a weighted value)
depart_day  int|null
arrive_day  int|null
branch      trunk|base_limb|fission|spur
```
`trunk` is the annual round. `base_limb` is the transhumance split where part of the group holds a base while the herd moves. `spur` is a short out-and-back. `fission` joins a daughter clan's round to its parent's; it is the ONLY leg whose `to` names a camp of a DIFFERENT band, it carries no day window, and it is excluded from `round_length_m`.

A **roll** (diagnostic; a fission child and an API-raised band have no roll):
```
node, x, z    ints
eligible      [classification names that passed their gate], in CLASSIFICATIONS order
evidence      {classification: basis object} for every eligible one
precipitated  bool
roll          float, present only when precipitated -- the uniform draw, in [0, total)
chose         classification, present only when precipitated
uid           band uid, present only when precipitated
```

**basis** contents by classification (this is the join back into the rest of the world, and the field a quest compiler should build on):
  survivors: `ruin_uid`, `ruin_distance_m`, `refuge_uid`, `refuge_distance_m`
  deserters: `war_site_uid`, `war_distance_m`, `ruggedness`, `engagements`
  cultists:  one of {`ley_node_id`, `ley_distance_m`, `ley_intensity`} /
             {`shrine_id`, `shrine_distance_m`} / {`claim_id`, `villain_uid`,
             `claim_distance_m`}, plus `school` and `god_id` on all three
  merchants: `markets` (up to 4 site uids), `market_count`, `nearest_market_m`,
             `road_distance_m`, `road_node`
  bandits:   `prize_uid` (site uid, or hamlet uid), `prize_distance_m`, `ruggedness`,
             `nearest_seat_m`
  wanderers: `forage`, `seasonal_swing`, `openness`
  A fission child's basis also carries `inherited_camp` (the parent camp id it took).
A basis uid can OUTLIVE the record it points at -- an age advance moves a ruined city from `settlements.sites` into `ruins` -- so a consumer must resolve against both.

### result['nomads']['routes'] -- written by add_nomad_routes, version 3
```
{version: 3, routed: int, stranded: int, fissioned: int, day_march_m: 35000.0,
 method: "...", limits: "..."}
```
`fissioned` is counted INSIDE `routed`. `routed + stranded == len(groups)` is an invariant a test pins.

### result['nomads']['effects'] -- written by apply_nomad_effects, version 1
```
{version: 1, leyline_edits: int, leyline_edits_hidden: int,
 towns_under_raid_pressure: int, roads_ridden: int, settlement_candidates: int,
 method: "...", limits: "..."}
```

### The four write-backs

1. `magic.networks[school]` REPLACED (copy-on-write) with the node's intensity raised. Only ever DEEPENED, never created.
2. `threat_assessments.cities[]` gains `nomad_pressure` (float, written on every city with a direction even when zero), and when at least one raider is in reach also `nomad_contributors` (sorted list of band uids) and a raised `regional_threat` (capped at 1.0, rounded to 6 dp).
3. `roads.routes[]` gains `caravan_riders` (sorted list of merchant band uids) and `caravan_throughput` (float, 6 dp) on every route ridden by at least one caravan sharing 2+ nodes.
4. `result['settlement_candidates']` -- a list, sorted by `id`. Each entry:
   `{id: "settlement-candidate-<band uid>", from_band, node, direction, migration_source_node, migration_distance_m, population_estimate, reason}`.
   ONLY ASSIGNED WHEN NON-EMPTY, so a world can carry a previous pass's list unchanged.

### absorb_survivor_camps write-backs
On a surviving refuge site: `absorbed_bands` (sorted list of band uids) and `absorbed_refugees` (int, cumulative). On the candidate: `absorbed_age` (int) and `absorbed_into` (refuge uid). Returns `(absorbed_count, people_count)`.
Those two city fields MUST be in `CARRIED_SURVIVOR_KEYS` -- the settlement rebuild re-derives `population_estimate` from the population budget on every rebuild (`residents = allowance//len(members) + (index < allowance%len(members))`, then `residents += absorbed_refugees` AFTER the capacity split), so refugees written into `population_estimate` at a boundary would be erased by the rebuild that was meant to carry them.

### timing
`result['timing_ms']['nomads']`, `['nomad_routes']`, `['nomad_effects']` each set to elapsed ms, and each ADDED to `result['timing_ms']['total']`.

## Algorithm

================================================================================
PASS 1 -- add_nomads(result, cfg)
================================================================================

STEP 0. Gate. If `not cfg.world_recipe or cfg.phase < 16`, return result unchanged.

STEP 1. Resolve inputs.
  o        = options(cfg)
  pol      = policy()                      # the validated nomads.json, cached
  occurrence = float(o.get('nomad_occurrence', 1.))
  density    = float(o.get('nomad_density', pol['candidate']['density_per_1000km2']))
  radius     = result['effective_config']['globe_radius']
  points, areas, _ = sphere_grid(cfg.size, radius)
  cells    = habitat_cells(result, cfg, points, areas)    # one per graph node, node order
  g        = ground(result, cfg, radius, points)
  rng      = Random(child_seed(cfg.seed, 'nomads-v1', int(o.get('nomad_variation', 0))))
  clearance = pol['candidate']['settlement_clearance_spacings'] * g['spacing']
              # 0.3 * settlement_spacing

STEP 2. The thinned point process. FOR EACH CELL IN NODE ORDER (this order is the RNG
        stream order and is therefore load-bearing):

  a) `if fields['medium'] != 'land': continue`  -- no draw, no roll record.

  b) room = clamp(1 - slope / 45)             # clamp(v) = max(0, min(1, v))
     `if room <= 0: continue`                 # slope >= 45 degrees is unusable

  c) If `g['settled']` is non-empty:
       near, _ = _nearest(cell.direction, g['settled'], radius)
       room *= clamp(near / clearance)
     CLEARANCE SCALES THE RATE; IT DOES NOT SKIP THE CELL. This is the single most
     important structural decision in the pass and the comment spells out why: a skip
     discards whole cells, and a cell is 52 km2 at raster 17 against 13 km2 at raster 33,
     so the same world would quietly hold a different number of bands at a different
     resolution -- the failure does not raise, it just changes the world. Scaling the
     rate removes area instead of cells and is resolution-independent. For the same
     reason clearance belongs in the rate rather than in a rejection AFTER the draw:
     rejecting an already-drawn candidate makes the world quietly hold fewer bands
     than asked.

  d) rate = occurrence * density * cell.area_km2 / 1000 * room
     `if rate <= 0: continue`

  e) drawn = int(rate)                       # truncation toward zero
     `if rng.random() < rate - drawn: drawn += 1`   # ONE uniform draw, always taken
     `if not drawn: continue`
     This is the standard "floor(lambda) + [u < frac(lambda)]" marked-process
     construction, the same one the beast-nest passes use, built from the only two
     primitives both implementations have: one uniform draw and a cumulative weighted
     pick. NOTE: the uniform draw is consumed EVEN WHEN drawn ends up 0.

  f) cell['swing'] = seasonal_swing(result, cell.x, cell.z)     # computed once per cell,
     only for cells that drew at least one candidate.

  g) FOR index IN range(drawn):
       shares = []        # list of (classification_name, share)
       evidence = {}      # classification_name -> basis object
       total = 0.0
       FOR name IN CLASSIFICATIONS (the fixed order
           ('survivors','deserters','cultists','merchants','bandits','wanderers')):
           verdict = GATES[name](cell, g, pol['classifications'][name]['gate'], result)
           if verdict is None: continue          # hard gate failed -> ineligible
           score, why = verdict                  # score is in [0,1] BY CONSTRUCTION
           share = score * pol['classifications'][name]['weight']
           if share <= 0: continue
           shares.append((name, share)); evidence[name] = why; total += share
           # total accumulates in CLASSIFICATIONS order -- plain accumulation

       record = {node, x, z, eligible: [n for n,_ in shares], evidence,
                 precipitated: bool(shares)}
       if not shares:
           rolls.append(record); continue        # empty ground is the honest answer

       name, draw = _pick(rng, shares, total)
       rule = pol['classifications'][name]
       low, high = rule['size']
       size = rng.randint(low, high)             # INCLUSIVE both ends
       uid = 'nomad-{latest_age}-{node}'
       if index: uid = uid + '-{index}'
       groups.append({ ... band record as specified in `writes` ... })
         camps = [ one camp: id uid+'-camp-0', node=cell.node,
                   direction=cell.direction, kind=ORIGIN_CAMP_KIND[name],
                   arrive_day=None, depart_day=None ]
         legs = []
         basis = evidence[name]
         god_id = evidence[name].get('god_id'); school = evidence[name].get('school')
         column_length_m = round(size * rule['column_length_per_head_m'], 6)
       counts[name] += 1
       record.update(roll=draw, chose=name, uid=uid); rolls.append(record)

STEP 3. `groups.sort(key=lambda b: b['uid'])`  -- LEXICOGRAPHIC string sort.
STEP 4. Write `result['nomads']` with version 1, groups, counts, rolls, method, limits.
STEP 5. Add elapsed ms to `timing_ms['nomads']` and `timing_ms['total']`.

--------------------------------------------------------------------------------
_pick(rng, shares, total) -- the weighted draw
--------------------------------------------------------------------------------
    draw = rng.random() * total
    cursor = 0.0
    for name, share in shares:          # shares is in CLASSIFICATIONS order
        cursor += share
        if draw <= cursor: return name, draw
    return shares[-1][0], draw          # float-rounding fallback: last eligible wins
Note the comparison is `<=`, not `<`. The cumulative walk uses plain accumulation.

--------------------------------------------------------------------------------
_nearest(point, candidates, radius) -> (metres, item)
--------------------------------------------------------------------------------
    best = None
    for where, key, item in candidates:
        if not where: continue          # a falsy direction is skipped
        mark = (distance(point, where, radius), key)
        if best is None or mark < best[0]: best = (mark, item)
    return (best[0][0], best[1]) if best else (inf, None)
Tuple comparison: distance first, then the STRING key. Strict `<`, so on an exact
(distance, key) tie the FIRST candidate in list order wins. Every candidate list's key
is a string in every producer.
`distance(a, b, radius) = radius * acos(clamp_to_[-1,1](dot(a,b)))` -- great-circle
metres. Returns `inf` and `None` when the candidate list is empty, which every gate
must tolerate (see edge cases).

--------------------------------------------------------------------------------
_where(item, points, n) -- resolve a record's unit direction
--------------------------------------------------------------------------------
    if item.get('direction'): return tuple(item['direction'])
    if 'x' in item and 'z' in item:   return direction(item['x'], item['z'], n)
    node = item.get('node')
    if isinstance(node,int) and 0 <= node < len(points):
        return direction(points[node].x, points[node].z, n)
    return None
Sites and claims carry `direction`; hamlets and religion shrines carry only a node.

--------------------------------------------------------------------------------
ruggedness(fields) -- the cover score deserters and bandits need
--------------------------------------------------------------------------------
    return clamp(slope / 22.0) * (1.0 + 0.35 * clamp(abs(tpi) / 40.0))
Range [0, 1.35]. Slope carries it and the topographic position index sharpens it: a
ridge or a ravine hides a band that open ground of the same steepness does not. NOTE
the gates compare the UNCLAMPED value against `min_ruggedness` but multiply the
CLAMPED value into the score.

--------------------------------------------------------------------------------
seasonal_swing(result, x, z) -- summer-to-winter spread, 0..1
--------------------------------------------------------------------------------
    months = result['seasonal_environment']['months'] or []
    if len(months) < 2: return 0.0
    warm = []
    for month in months:
        grid = month.get('temperature')
        if not grid: return 0.0
        warm.append(grid[z][x])
    return clamp((max(warm) - min(warm)) / 30.0)
Wanderers exist because of this number: a round is only worth walking where the good
ground moves with the year. A flat year feeds a settled farm, not a herd. The twelve
monthly grids already carry the latitude and elevation proxy INCLUDING the reversal
across the equator, so a southern cell swings on the opposite half of the year and
this function needs no hemisphere logic of its own.

--------------------------------------------------------------------------------
population_of(site) -- a settlement's weight as a target or market, in people
--------------------------------------------------------------------------------
    for key in ('population_estimate','urban_population_estimate',
                'rural_population_estimate'):
        v = site.get(key)
        if isinstance(v,(int,float)) and v > 0: return float(v)
    y = site.get('delivered_food')
    return float(y) if isinstance(y,(int,float)) and y > 0 else 0.0
Hamlets carry `population_estimate: None` -- they are sized by the food they deliver,
not by a head count -- so the chain ends at that yield. Without the fallback a hamlet
scores zero and bandits ignore the rural settlements that are their easiest prey.
Deliberately reads population rather than `city_class`, which is a label ('capital',
'medium') with no ordering to compare against. NOTE the units are inconsistent by
construction: for a hamlet the returned number is a food yield, not people.

--------------------------------------------------------------------------------
_god_for_school(result, school) -- a god bound to a school
--------------------------------------------------------------------------------
    if not school: return None
    for god in result['religion']['gods']:           # LIST ORDER, first match wins
        schools = god.get('schools')
        if not schools:
            single = (god.get('exists') or {}).get('school')
            schools = [single] if single else []
        if school in schools: return god['id']
    return None
Deliberately NOT a lookup against the authored pantheon file: that file does not carry
the four hidden gods, so a blood cult -- the exact case a villain of the blood god
creates -- would raise instead of finding its patron. Hidden gods are the entries whose
`family` is 'outside'.

================================================================================
THE SIX GATES. Each returns None (ineligible) or (score in [0,1], basis dict).
EVERY GATE RETURNS A SCORE IN 0..1 SO THE WEIGHT IN nomads.json IS THE ONLY BALANCE
LEVER. This was learned the hard way: with raw unnormalised scores, ley intensity ran
to 4.0 while a bandit's score sat near 0.5, so cultists won almost every contested draw
no matter what the weights said -- a table that looked tuned and was decorative.
================================================================================

_gate_survivors(cell, g, rule, result) -- refugees
  span, ruin = _nearest(cell.direction, g['fresh_ruins'], radius)
  if ruin is None or span > rule['ruin_reach_spacings'] * spacing: return None
  safe, site = _nearest(cell.direction, g['markets'], radius)
  if site is None or safe > rule['refuge_reach_spacings'] * spacing: return None
  scar = clamp(ruin.get('legacy',{}).get('intensity', 1.0) / 4.0)
  score = clamp(scar / (1.0 + safe / spacing))
  basis = {ruin_uid, ruin_distance_m: span, refuge_uid: site['uid'],
           refuge_distance_m: safe}
  A town died THIS AGE and somewhere near is still standing. Displaced people
  overwhelmingly stay close to home, so the band only exists where there is both a
  fresh ruin to flee and a surviving neighbour to flee toward. Note `legacy.intensity`
  defaults to 1.0 when the key or the sub-dict is absent, and `(x or {})` guards a
  `legacy: null`.

_gate_deserters -- broken soldiery
  span, war = _nearest(cell.direction, g['wars'], radius)
  if war is None or span > rule['war_reach_spacings'] * spacing: return None
  rough = ruggedness(cell.fields)
  if rough < rule['min_ruggedness']: return None
  history = war.get('war_history') or []
  score = clamp(rough) * clamp(0.6 + 0.2 * len(history))
  basis = {war_site_uid, war_distance_m: span, ruggedness: rough,
           engagements: len(history)}

_gate_cultists -- pilgrims. THE HARD PART OF THIS GATE IS THE POINT OF THE WHOLE
FEATURE: without sacred ground within reach AND a god actually bound to that school,
there is no cult here however well the weights would have scored.
  span,       node   = _nearest(cell.direction, g['ley'],     radius)
  shrine_span,shrine = _nearest(cell.direction, g['shrines'], radius)
  claim_span, claim  = _nearest(cell.direction, g['claims'],  radius)
  school, strength, evidence = None, 0.0, {}
  BRANCH A (ley, tried first): if node is not None
                               and node['intensity'] >= rule['min_ley_intensity']
                               and span <= rule['ley_reach_spacings'] * spacing:
      school = node['school']; strength = node['intensity']
      evidence = {ley_node_id, ley_distance_m: span, ley_intensity: strength}
  ELIF BRANCH B (shrine): shrine is not None
                          and shrine_span <= rule['shrine_reach_spacings'] * spacing:
      god = shrine.get('god_id')
      evidence = {shrine_id, shrine_distance_m, god_id: god}
      school = None
      for record in result['religion']['gods']:           # LIST ORDER
          if record.get('id') == god and record.get('schools'):
              school = record['schools'][0]; break
      strength = 1.0
  ELIF BRANCH C (villain claim): claim is not None
                          and claim_span <= rule['claim_reach_spacings'] * spacing:
      school = claim['villain'].get('school')
      strength = max(1.0, float(claim['villain'].get('tier', 1.0)))
                 * float(claim['claim'].get('influence', 1.0))
      evidence = {claim_id, villain_uid, claim_distance_m}
      The `influence` factor is 1.0 for a living holder and the decayed fallen-claim
      value once its holder goes (0.60, 0.36, 0.216, 0.130, 0.078, then 0 below a 0.05
      floor). Reading it off the claim keeps the decay question in one place instead
      of in every consumer.
  if not school: return None
  god = _god_for_school(result, school)
  if not god: return None
  evidence.update(school=school, god_id=god)
  score = clamp(strength / 4.0)
  NOTE the branches are EXCLUSIVE elif, so a ley node in reach but BELOW intensity
  blocks nothing -- the elif chain still falls through to the shrine branch, because
  the whole condition (not just `node is not None`) failed. But a ley node in reach and
  above intensity whose school has no bound god returns None outright; it does NOT fall
  back to the shrine or claim branch.
  NOTE ALSO that in branch B the final `evidence.update(god_id=god)` OVERWRITES the
  shrine's own `god_id` with whatever `_god_for_school` found first for that school,
  which need not be the shrine's god.

_gate_merchants -- caravans: a road underfoot and two markets worth walking between
  road_span, node = _nearest(cell.direction, g['roads'], radius)
  if node is None or road_span > rule['road_reach_spacings'] * spacing: return None
  reach = rule['market_reach_spacings'] * spacing
  found = []
  for where, key, site in g['markets']:            # LIST ORDER
      people = population_of(site)
      if people < rule['min_market_population']: continue
      span = distance(cell.direction, where, radius)
      if span <= reach: found.append((span, key, site, people))
  if len(found) < rule['min_markets']: return None
  found.sort()                                     # by (span, uid, ...) -- see hazards
  weight = 0.0
  for span,_,site,people in found[:4]:             # PLAIN ACCUMULATION, not sum()
      weight += clamp(log10(1 + people) / 5.0) / (1.0 + span / spacing)
  score = clamp(weight / min(4, len(found)))
  basis = {markets: [site uid for the first 4], market_count: len(found),
           nearest_market_m: found[0][0], road_distance_m: road_span, road_node: node}
  The source note on the accumulation: CPython compensates a builtin `sum` of floats
  and a native port would have to replicate that to stay bit-exact -- not worth buying
  here, so it is written as an explicit loop. In a clean rewrite this is simply a loop.

_gate_bandits -- rough ground, wealth in reach of it, nobody close enough to police it
  rough = ruggedness(cell.fields)
  if rough < rule['min_ruggedness']: return None
  span, prize = _nearest(cell.direction, g['wealth'], radius)
  if prize is None or span > rule['strike_reach_spacings'] * spacing: return None
  control, seat = _nearest(cell.direction, g['markets'], radius)
  if control < rule['control_clear_spacings'] * spacing: return None    # STRICT <
  worth = clamp(log10(1 + population_of(prize)) / 5.0)
  if worth <= 0: return None
  score = clamp(rough) * worth * clamp(control / spacing / 3.0)
  basis = {prize_uid: prize.get('uid') or prize.get('id'), prize_distance_m: span,
           ruggedness: rough, nearest_seat_m: control}
  The productive pattern is refuge terrain beside prosperity -- the hills supply safety
  and the lowlands supply targets -- so both must hold, and authority must NOT. The
  third condition is the ABSENCE of a seat, which is what makes this a bandit gate
  rather than a "rough ground near a town" gate.

_gate_wanderers -- herders: forage worth following, and a year that moves it
  forage = max(food_potential, natural_food_potential)
  if forage < rule['min_forage']: return None
  swing = cell.get('swing', 0.0)
  if swing < rule['min_seasonal_swing']: return None
  openness = clamp(1.0 - slope / 30.0)
  score = clamp(forage / 0.25) * clamp(0.5 + 0.5*swing) * clamp(0.4 + 0.6*openness)
  basis = {forage, seasonal_swing: swing, openness}
  Transhumance needs two grounds, not one -- high open pasture for the warm months and
  sheltered low ground for the cold. A cell with forage but no seasonal spread feeds a
  farm instead. Note this gate does NOT check whether summer and winter ground are
  mutually REACHABLE; that needs the route pass's cost function, so a herder with
  nowhere to winter is discovered late and reported as `route_status: "stranded"`.

================================================================================
PASS 2 -- add_nomad_routes(result, cfg)
================================================================================

STEP 0. `block = result.get('nomads')`; return if missing or `groups` empty.

STEP 1. Build the routing context.
  points, areas, graph = sphere_grid(cfg.size, radius)
  cells = habitat_cells(...)
  cost  = travel_cost(points, cells, cfg)
  spacing = float(cfg.settlement_spacing)
  paths(start) = memoised shortest_paths(graph, start, cost) -> (distances[], parent[])
      Cache keyed on the start node, unbounded (no `limit`), for the whole world.
  node_of = {cell.direction -> node index}, built by iterating cells in order so a
      duplicate direction would keep the LAST; directions are distinct in practice.
  nearest_node(where) = node_of[where] if present, else argmin over all cells of
      (distance(where, cell.direction, radius), index). An exact hit and the scan agree
      because an exact match is distance 0, so a port may simply always scan.
  ley = for each network name in dict order, each node in list order:
      (tuple(node.direction), node.id, {'school': name})       # NO intensity here
  war_direction(band): scan `(result['ruins'] or []) + result['settlements']['sites']`
      in that concatenation order for the record whose `uid` equals
      `band['basis']['war_site_uid']`; on the first match take
      `away = band.direction - record.direction`, normalise, return it; return None if
      the record has no direction or the norm is <= 1e-9, and None if no record matches.
      (Note: returns on the FIRST uid match even if that record has no direction.)
  ctx = {cells, cfg, pol, spacing, paths, points, sites, hamlets, ley, nearest_node,
         war_direction}

STEP 2. STRIP CHILDREN: `block['groups'] = [b for b in groups if not b.get('parent_uid')]`
  Daughter clans are DERIVED from the parents below, never carried in. This pass runs
  again on every `nomad_request` and on a monthly route cadence, so a fission step that
  added to the set instead of replacing it would breed a new generation per call.

STEP 3. FOR EACH BAND in `block['groups']` order (which is uid-sorted):
  built = BUILDERS[band.classification](band, ctx)
  if not built:
      band['route_status'] = 'stranded'; stranded += 1; continue
  camps, order, parent, distances = built
  legs = _legs_for(band, camps, order, parent, ctx, cells, radius, paths)
  if not legs:
      band['route_status'] = 'stranded'; stranded += 1; continue
  if band.classification == 'merchants':
      camps, legs = _subdivide(band, camps, legs, cells, radius, DAY_MARCH_M)
  _assign_days(band, camps, legs, ctx)
  band['camps'] = camps; band['legs'] = legs
  band['route_status'] = 'routed'
  band['round_length_m'] = round(sum(l['length_m'] for l in legs), 6)
  routed += 1

STEP 4. born = apply_fission(result, cfg, block, ctx, cells, radius, paths,
                             int(o.get('nomad_variation', 0)))
        if born: block['groups'] = sorted(groups + born, key=uid); routed += len(born)

STEP 5. RECOMPUTE counts from the band set (never incremented), because three callers
        add to it -- placement, the request API and fission -- and a tally that is only
        ever incremented cannot survive a pass that removes anybody.
          counts = {name: 0 for name in CLASSIFICATIONS}
          for band in groups: counts[band.classification] += 1

STEP 6. Write `block['routes']` with version 3, routed, stranded, fissioned,
        day_march_m, method, limits; add elapsed to timing.

--------------------------------------------------------------------------------
travel_cost(points, cells, cfg) -- the walking cost function
--------------------------------------------------------------------------------
Precompute per node: water[i] = (medium != 'land'), height[i], and
forage[i] = clamp(max(food_potential, natural_food_potential)).
ceiling = cfg.road_max_grade * GRADE_RELIEF          # GRADE_RELIEF = 2.2

cost(i, j, d):                     # d is the great-circle metres between the two nodes
    if water[i] or water[j] or d <= 0: return None                # impassable
    xi,zi = points[i]; xj,zj = points[j]
    if xi != xj and zi != zj and zi not in (0, n-1) and zj not in (0, n-1):
        # A diagonal cannot slip between two water-filled corner cells.
        if water[lookup[(xi,zj)]] or water[lookup[(xj,zi)]]: return None
    grade = abs(height[j] - height[i]) / d
    if grade > ceiling: return None
    green = 1.0 - 0.25 * (forage[i] + forage[j]) / 2.0            # in [0.75, 1.0]
    return d * (1.0 + 8.0 * grade**2) * green

This is DELIBERATELY NOT the road cost function. That one refuses a river without a
bridge and caps grade at the road limit, both of which describe an engineered route
rather than a walked one. A band fords, and takes ground a road would not. Water is
still impassable -- these are not boats -- and the forage term makes a herd prefer the
green way round. The road function uses `1 + 12*grade^2 + 0.5*(flood[i]+flood[j])` plus
a bridge cost; this one uses `1 + 8*grade^2` and the green multiplier.

--------------------------------------------------------------------------------
shortest_paths(graph, start, cost) -- DIJKSTRA, binary heap
--------------------------------------------------------------------------------
    distances = [inf]*N; parent = [-1]*N; distances[start] = 0
    queue = [(0, start)]
    while queue:
        value, i = heappop(queue)
        if value != distances[i]: continue         # stale entry, skip
        for j, d in graph[i]:                      # neighbours in ASCENDING index order
            edge = cost(i, j, d)
            if edge is None: continue
            candidate = value + edge
            if candidate <= limit and candidate < distances[j]:
                distances[j] = candidate; parent[j] = i; heappush(queue, (candidate, j))
    return distances, parent
`limit` is inf for every nomad call. The relaxation is STRICT `<`, so on an exact cost
tie the FIRST relaxation wins, which depends on pop order. Edge costs are non-negative,
so the result is exact.
`trace(parent, start, end)`: walk `end -> parent[...] -> start`, return the reversed
list; return [] the moment a parent is -1 (unreachable).

--------------------------------------------------------------------------------
_reach(band, pol, key, spacing, fallback) = gate.get(key, fallback) * spacing
_camp(band, index, node, cells, kind) = {id: '<uid>-camp-<index>', node,
    direction: list(cells[node].direction), kind, arrive_day: None, depart_day: None}
_best(candidates, score): argmax over `(score(node), -node)`, strict `>`, so a tie on
    score is broken by the LOWEST node index. Returns None on an empty candidate list.
_within(distances, limit) = [i for i,d in enumerate(distances) if d <= limit and
    isfinite(d)] -- ASCENDING node index order.
--------------------------------------------------------------------------------

BUILDER A -- _seasonal_camps (wanderers): summer pasture and winter shelter
  start = band.node
  reach_s = gate['summer_reach_spacings'] (default 3.5) * spacing
  reach_w = gate['winter_reach_spacings'] (default 3.5) * spacing
  distances, parent = paths(start)
  base = height[start]
  summer(node): openness = clamp(1 - slope/30)
                lift     = clamp((height[node] - base) / max(1, abs(base) + 400))
                forage   = max(food_potential, natural_food_potential)
                return forage * (1 + lift) * (0.4 + openness)
  winter(node): shelter  = clamp(abs(tpi) / 30)
                drop     = clamp((base - height[node]) / max(1, abs(base) + 400))
                warmth   = clamp((temperature + 20) / 45)
                forage   = max(food_potential, natural_food_potential)
                return (0.3 + forage) * (1 + drop) * (0.4 + shelter) * (0.5 + warmth)
  s = _best([i in _within(distances, reach_s) if i != start], summer)
  w = _best([i in _within(distances, reach_w) if i != start], winter)
  if s is None or w is None or s == w: return None
  camps = [_camp(0, start, 'base'), _camp(1, s, 'summer'), _camp(2, w, 'winter')]
  order = [(0,1,'base_limb'), (1,2,'trunk'), (2,1,'trunk')]
  THE START NODE IS THE BASE AND CANNOT ALSO BE A SEASONAL CAMP: if it were, the
  base_limb leg would collapse to zero length, be dropped by `_legs_for`, and leave the
  base camp with no leg referencing it and therefore no day window at all.
  Summer wants HEIGHT and OPENNESS -- ground that cannot be used in the cold months, so
  it is not competing with anyone. Winter wants the opposite and one thing more: snow
  settles on the flat, so the camps that work are tucked against relief where sun and
  wind strip the pasture clear. That is what the `shelter` term on |tpi| buys.

BUILDER B -- _market_stations (merchants): a chain between markets
  reach = gate['market_reach_spacings'] (default 3.0) * spacing
  floor = merchants gate['min_market_population'] (default 0)
  distances, parent = paths(start)
  found = []
  for site in ctx['sites']:                              # LIST ORDER
      node = site.get('node')
      if node is None or not 0 <= node < len(cells): continue
      if population_of(site) < floor or not isfinite(distances[node])
         or distances[node] > reach: continue
      found.append((distances[node], site['uid'], node))
  if len(found) < 2: return None
  found.sort()                                           # (dijkstra cost, uid, node)
  camps = [_camp(0, start, 'station')] + [_camp(i+1, node, 'station')
                                          for i,(_,_,node) in enumerate(found[:4])]
  order = [(i, i+1, 'trunk') for i in range(len(camps)-1)] + [(len(camps)-1, 0, 'trunk')]
  A CLOSED chain: last market back to the start station.
  NOTE the reach here is the DIJKSTRA COST, not the great-circle distance the gate used.

BUILDER C -- _sacred_circuit (cultists): a closed loop over one god's sacred ground
  reach = gate['claim_reach_spacings'] (default 1.2) * spacing * 2.5
          -- i.e. 3.0 spacings by default, and it uses the CLAIM reach key regardless of
          which branch of the gate actually classified the band.
  distances, parent = paths(start)
  school = band['school']
  nodes = []
  for where, node_id, record in ctx['ley']:              # LIST ORDER
      if record['school'] != school: continue
      node = nearest_node(where)
      if node is None or not isfinite(distances[node]) or distances[node] > reach:
          continue
      nodes.append((distances[node], node_id, node))
  nodes.sort()                                           # (cost, ley node id, node)
  seen = {start}; picked = []
  for _,_,node in nodes:
      if node in seen: continue
      seen.add(node); picked.append(node)
      if len(picked) == 3: break                         # AT MOST THREE
  if not picked: return None
  camps = [_camp(0, start, 'shrine')] + [_camp(i+1, node, 'shrine')
                                         for i,node in enumerate(picked)]
  order = chain 0->1->...->k then k->0, all 'trunk'.
  Cultists snap to the world's EXISTING ley networks rather than inventing geometry,
  because pilgrimage is a fixed repeated circuit widely read as tracing ley lines.

BUILDER D -- _raid_orbit (bandits): a lair with out-and-back spurs
  reach = gate['strike_reach_spacings'] (default 1.3) * spacing
  distances, parent = paths(start)
  prizes = []
  for site in ctx['sites'] + ctx['hamlets']:             # CONCATENATION ORDER
      node = site.get('node')
      if node is None or not 0 <= node < len(cells) or node == start: continue
      if not isfinite(distances[node]) or distances[node] > reach: continue
      prizes.append((-population_of(site), distances[node], node))
  if not prizes: return None
  prizes.sort()                                          # richest first, then nearest
  camps = [_camp(0, start, 'lair')]
  order = []
  for i,(_,_,node) in enumerate(prizes[:2]):             # AT MOST TWO targets
      camps.append(_camp(i+1, node, 'terminal'))
      order.append((0, i+1, 'spur')); order.append((i+1, 0, 'spur'))
  A raiding band does not migrate. The spur returning to the lair IS the whole shape:
  strike, vanish, and be somewhere defensible before anyone organises.

BUILDER E -- _flight(band, ctx, kind) (deserters and survivors): one terminating move
  distances, parent = paths(start)
  IF kind == 'survivors':
      reach = gate['refuge_reach_spacings'] (default 2.5) * spacing
      target = the node of the LAST site in ctx['sites'] whose uid ==
               band['basis']['refuge_uid']   (the loop does not break, so the last match
               wins; uids are unique in practice)
      if target is None or not isfinite(distances[target]) or distances[target] > reach:
          return None
      camps = [_camp(0, start, 'base'), _camp(1, target, 'terminal')]
      THIS IS WHY A SURVIVOR CANDIDATE NAMES THE REFUGE CITY'S OWN NODE: the band is
      routed to the node of the site whose uid is basis['refuge_uid'], and
      seed_survivor_camps records the candidate at the terminal camp's node.
  ELSE (deserters):
      reach = gate['war_reach_spacings'] (default 0.8) * spacing * 3.0
      away = war_direction(band)                         # unit vector, or None
      candidates = _within(distances, reach)
      cover(node): if node == start: return -1.0
                   lead = 0.0
                   if away: lead = clamp((1 + dot(away, cells[node].direction)) / 2)
                   return ruggedness(cells[node].fields) * (0.4 + lead)
      target = _best(candidates, cover)
      if target is None or target == start: return None
      camps = [_camp(0, start, 'base'), _camp(1, target, 'lair')]
  order = [(0, 1, 'trunk')]
  Both terminate: neither is a circuit.

--------------------------------------------------------------------------------
_legs_for(band, camps, order, parent, ctx, cells, radius, paths) -- one body, three
callers (the main loop, the fission child, and nothing else)
--------------------------------------------------------------------------------
    legs = []
    for a, b, branch in order:
        src, dst = camps[a], camps[b]
        if src.node == dst.node: continue            # DROP a zero-length leg silently
        nodes = trace(parent, band.node, dst.node) if src.node == band.node else []
        if not nodes:
            sub_d, sub_p = paths(src.node)           # fresh Dijkstra from src, memoised
            nodes = trace(sub_p, src.node, dst.node)
        if not nodes: continue                       # unreachable -> drop the leg
        length = sum over consecutive pairs of distance(cells[p].direction,
                                                        cells[q].direction, radius)
        legs.append({from: src.id, to: dst.id, nodes, length_m: length,
                     depart_day: None, arrive_day: None, branch})
`length_m` is the GREAT-CIRCLE walk length, not the Dijkstra cost. The two differ by
the grade and forage factors.

--------------------------------------------------------------------------------
_subdivide(band, camps, legs, cells, radius, day_march) -- MERCHANTS ONLY
--------------------------------------------------------------------------------
    out_camps = copy of camps; out_legs = []
    for leg in legs:
        if leg.length_m <= day_march or len(leg.nodes) < 3:
            out_legs.append(leg); continue
        cut_from = leg.from; run = 0.0; nodes = leg.nodes; segment = [nodes[0]]
        for (a, b) in consecutive pairs of nodes:
            step = distance(cells[a].direction, cells[b].direction, radius)
            if run > 0.0 and run + step > day_march:
                station = _camp(band, len(out_camps), a, cells, 'station')
                out_camps.append(station)
                out_legs.append({from: cut_from, to: station.id, nodes: segment,
                                 length_m: run, depart_day: None, arrive_day: None,
                                 branch: leg.branch})
                cut_from = station.id; segment = [a]; run = 0.0
            run += step
            segment.append(b)
        out_legs.append({from: cut_from, to: leg.to, nodes: segment, length_m: run,
                         depart_day: None, arrive_day: None, branch: leg.branch})
    return out_camps, out_legs

THE CUT CLOSES AT `a`, THE LAST NODE THE DAY'S WALK REACHES, NOT AT `b`, THE FIRST NODE
PAST IT. This is the whole logic of a caravan route rather than a decoration on it:
stations sat a day apart precisely so a laden train never had to spend a night in the
unmonitored gap between nodes. A leg that survives uncut is one a caravan can walk
between dawn and dusk, so it needs no station. Owner ruling 2026-09-21: NO SEGMENT
EXCEEDS ONE DAY'S MARCH. The rejected reading was that a herder day is elastic so a
short tail may be absorbed into the previous day; it lost because a station exists to
stop a train sleeping in the gap, which a 1.5-day segment does not do at any elasticity.
  - Cutting on the crossing (at `b`) would overshoot by up to one graph step -- 6250 m
    at size 33, 17.9% of the march -- and still break the assertion it is meant to
    satisfy.
  - The OLD tail condition `remaining > day_march * 0.5` was worse than either: it
    declined to cut whenever the remainder was half a march or less, so the remainder
    was absorbed backwards and a segment could reach 1.5 marches. Measured on seed 42
    size 33, leg `nomad-2-723-camp-3 -> camp-4`: 11 steps, cumulative 34730.3 m at the
    tenth node and 40980.3 m at the eleventh, so the only crossing left 3125 m to run
    and no cut fired at all. It was emitted whole at 1.171 day-marches.
  - `run > 0.0` is NOT defensive: at the head of a segment there is no earlier node to
    fall back to, so a single over-long hop is WALKED rather than refused.
  - The tail always holds at least one step, because the cut closes at `a` and `b` is
    appended after it, so the last node is never a cut point. An older shape cut at `b`
    and could end with a one-node tail; the repair branch that fixed that
    (`elif out_legs: out_legs[-1]['to'] = leg['to']`) is unreachable under this cut and
    was removed rather than left to be trusted.
  - `len(nodes) < 3` stays because a leg with no intermediate node has nowhere to stand
    a station, and neither has a single hop inside a longer leg. A graph step wider than
    a day's march is emitted over-long BY CONSTRUCTION. It does not arise at any raster
    the generator ships -- the step is 12500 m at size 17 and 6250 m at size 33 against
    a 35000 m march -- and it is a property of the route graph, not of this cut.

--------------------------------------------------------------------------------
_assign_days(band, camps, legs, ctx) -- walk the circuit against the calendar
--------------------------------------------------------------------------------
    n = cfg.size; speed = max(1.0, band.speed_m_per_day)
    total_travel = sum over legs of max(1, int(round(leg.length_m / speed)))
    shares = [CAMP_REST.get(camp.kind, 1.0) for camp in camps]
    weight = plain accumulation of shares
    spare  = max(len(camps), DAYS_PER_YEAR - total_travel)        # DAYS_PER_YEAR = 360
    rest_of = {}
    for camp, share in zip(camps, shares):
        held = max(1, int(spare * share / weight))                # int() TRUNCATES
        rest_of[camp.id] = min(held, CAMP_REST_CEILING.get(camp.kind, held))

    winter = the FIRST camp whose kind == 'winter', or None
    anchor = midwinter_day(cells[winter.node].z, n) if winter is not None
             else midsummer_day(cells[band.node].z, n)
    day = anchor
    by_id = {camp.id: camp for camp in camps}

    if winter is not None:
        # Start the clock at the winter camp so it HOLDS midwinter, then walk forward.
        start_index = the first k with legs[k].from == winter.id, else 0
        order = legs[start_index:] + legs[:start_index]     # ROTATED COPY of the list;
                                                            # the leg dicts are shared
    else:
        order = legs

    first = by_id.get(order[0].from) if order else None
    if first is not None:
        first.arrive_day = day % 360
        first.depart_day = (day + rest_of[first.id]) % 360

    for leg in order:
        travel = max(1, int(round(leg.length_m / speed)))
        held   = rest_of.get(leg.from, 1)
        leg.depart_day = (day + held) % 360
        day += held + travel                    # NOT reduced mod 360; it accumulates
        leg.arrive_day = day % 360
        camp = by_id.get(leg.to)
        if camp is not None and camp.arrive_day is None:
            camp.arrive_day = day % 360
            camp.depart_day = (day + rest_of.get(camp.id, 1)) % 360

    # A camp that is only ever a leg's SOURCE -- the base a transhumant group holds while
    # the herd moves -- is never reached by the loop above, so it would carry no window
    # at all. It is occupied from when the moving limb leaves it until the limb returns.
    for leg in order:
        camp = by_id.get(leg.from)
        if camp is not None and camp.arrive_day is None and leg.depart_day is not None:
            held = rest_of.get(camp.id, 1)
            camp.depart_day = leg.depart_day
            camp.arrive_day = (leg.depart_day - held) % 360

A herding round is anchored so the WINTER camp is occupied at midwinter -- that is the
camp survival depends on, so it is the one the year is built around. Everything else
starts at its own midsummer and runs on travel time, which is length over speed.

Worked example, wanderers (camps [base, summer, winter], order [(0,1,base_limb),
(1,2,trunk), (2,1,trunk)]): legs are base->summer, summer->winter, winter->summer. The
winter camp exists, so the rotation puts winter->summer first. `first` = the winter camp,
which is stamped at the anchor. Then winter->summer, base->summer (whose `to` already has
a window so it is not overwritten), summer->winter (ditto). Finally the post-pass gives
the base camp its window, closing backwards from the base_limb leg's departure.

Worked example, bandits (camps [lair, t1, t2], order [(0,1,spur),(1,0,spur),(0,2,spur),
(2,0,spur)]): no winter camp, so `anchor = midsummer` at the band's own row and `order`
is the leg list unrotated. NOTE the lair's rest is charged TWICE, once for leg 0 and
once for leg 2, because `held = rest_of[leg.from]` is looked up per leg.

midsummer_day(z, n):
    latitude = 90.0 - 180.0 * z / (n - 1)
    phase = MIDSUMMER_NORTH if latitude >= 0 else MIDSUMMER_NORTH + 0.5
    return int(round((phase % 1.0) * DAYS_PER_YEAR)) % DAYS_PER_YEAR
midwinter_day(z, n) = (midsummer_day(z, n) + DAYS_PER_YEAR // 2) % DAYS_PER_YEAR
With MIDSUMMER_NORTH = 0.5 and DAYS_PER_YEAR = 360 this is day 180 in the north and
day 0 in the south for midsummer, and day 0 / day 180 for midwinter. THE SEASONAL PHASE
INVERTS ACROSS THE EQUATOR. Getting that wrong is invisible on a world whose land
happens to sit in one hemisphere, which is why it is pinned by a test rather than by
inspection. Anchoring on a FRACTION rather than a day keeps this correct if the calendar
length ever changes.

--------------------------------------------------------------------------------
LINEAGE FISSION
--------------------------------------------------------------------------------
round_forage(band, cells):
    total = plain accumulation over band.camps of
            max(food_potential, natural_food_potential) at camp.node
    return total / len(camps) if camps else 0.0
The ROUND rather than the start cell, because the route pass picks the best ground in
reach: a clan's start point says where it was raised and its camps say what it eats.
Measured on seed 42 at size 33, a routed herding round runs 0.32 to 0.57 while the
wanderer gate's floor at the start cell is 0.07 -- the two are NOT the same quantity and
a capacity read off the floor would never bind.

round_capacity(band, pol, forage):
    low, high = pol['classifications'][band.classification]['size']
    floor = that classification's gate['min_forage'] (default 0.0)
    rich  = pol['fission']['rich_forage']
    return low + (high - low) * clamp((forage - floor) / (rich - floor))
DIMENSIONLESS ON PURPOSE. The obvious model -- head per square kilometre of forage --
reads the raster rather than the land, exactly as the placement rate would have if
clearance had been a rejection instead of a rate: a cell is 52 km2 at raster 17 against
13 km2 at raster 33, so the same clan on the same ground would outgrow it at one
resolution and not the other. The authored `size` band is already this module's
statement of how large a band of a given kind gets, so the round's forage picks a point
on it: a round at the gate's forage floor carries the bottom of the band, a round at
`rich_forage` carries the top.

_inheritable(band):
    rest = [camp for camp in band.camps if camp.node != band.node]
    return sorted(rest, key=lambda c: (c.kind != 'summer', c.id))
Its own start camp is NOT one of them: a split that leaves the child standing where the
parent already stands has divided nothing. SUMMER PASTURE FIRST, because high open
ground in the warm months is what a herd that has outgrown its range competes for. The
secondary key is the camp id STRING.

apply_fission(result, cfg, block, ctx, cells, radius, paths, rng_seed):
  rule = pol['fission']; kinds = set(rule['classifications'])   # {'wanderers'} today
  parents = [band for band in block.groups if not band.parent_uid]
  candidates = [band for band in parents
                if band.classification in kinds
                and band.route_status == 'routed'
                and band.size > round_capacity(band, pol,
                                               round_forage(band, cells))]
  if not candidates: return []
  g = ground(result, cfg, radius, ctx['points'])   # THE REAL GATE, rebuilt, not a
                                                   # restatement of it
  taken = {band.uid for band in block.groups}
  born = []
  FOR band IN candidates (in block.groups order, i.e. uid-sorted):
      rng = Random(child_seed(cfg.seed, 'nomad-fission-' + band.uid, rng_seed))
      low, high = the classification's size band
      share_low, share_high = rule['child_share']
      size = int(min(high, max(low, round(band.size * rng.uniform(share_low,
                                                                 share_high)))))
      # EXACTLY ONE RNG DRAW PER CANDIDATE, taken before the inheritance loop, so the
      # size does not depend on which camp the child ends up seated on.
      child = None
      FOR inherited IN _inheritable(band):
          node = inherited.node; cell = cells[node]
          cell['swing'] = seasonal_swing(result, cell.x, cell.z)
          verdict = GATES[band.classification](cell, g,
                        pol['classifications'][band.classification]['gate'], result)
          if verdict is None: continue      # this camp's ground raises nobody of this
                                            # kind on its own merits
          _, why = verdict
          uid = 'nomad-%d-%d' % (band.origin.age, node)
          suffix = 0
          while uid in taken: suffix += 1; uid = 'nomad-%d-%d-%d' % (age, node, suffix)
          child = { ...same band shape as placement..., parent_uid: band.uid,
                    origin: copy of band.origin,
                    god_id/school from `why`, node/x/z/direction/biome from the cell,
                    size, speed/column/disposition/seeks/carries from the classification,
                    camps: [one camp at `node` of kind ORIGIN_CAMP_KIND[classification]],
                    legs: [], basis: dict(why, inherited_camp=inherited.id),
                    route_status: 'stranded' }
          built = BUILDERS[child.classification](child, ctx)
          if not built: child = None; continue
              # A segment with nowhere of its own to winter has not split, it has
              # STARVED. Refused rather than recorded as a stranded child.
          camps, order, parent, _ = built
          legs = _legs_for(child, camps, order, parent, ctx, cells, radius, paths)
          if not legs: child = None; continue
          _assign_days(child, camps, legs, ctx)
          # The move that FOUNDED the child, appended after its round is scheduled and
          # deliberately carrying NO day window: it happened once, so it is not a leg of
          # anybody's year, and giving it one would put the child at its parent's camp
          # every spring.
          _, back = paths(child.node)
          joined = trace(back, child.node, band.node)
          if len(joined) < 2: child = None; continue
          span = sum of great-circle distances along `joined`
          child.camps = camps
          child.legs  = legs + [{from: camps[0].id, to: band.camps[0].id,
                                 nodes: joined, length_m: span,
                                 depart_day: None, arrive_day: None,
                                 branch: 'fission'}]
              # `to` names a camp of the band in parent_uid; that is what makes the
              # branch a join between two rounds rather than one inside a round.
          child.route_status = 'routed'
          child.round_length_m = round(sum(leg.length_m for leg in LEGS), 6)
              # THE ANNUAL ROUND ONLY. The fission leg is excluded, because it is not
              # walked every year and counting it would tell a time mover the clan
              # travels further than it does.
          break                              # first camp that works wins
      if child is None: continue
      taken.add(child.uid); born.append(child)
  return born

CADENCE -- ONCE AT PLACEMENT, NOT ONCE AN AGE. This was an open design question and the
answer is recorded so it can be reversed knowingly. Three reasons, the first two
load-bearing:
 1. NOTHING KILLS A BAND. Per-age fission grows the population every age with no term
    removing anyone, so band count rises without a ceiling. Once at placement is bounded
    by construction: at most one child per placed band, and a child never splits (the
    candidate filter excludes anything with a parent_uid), so a world holds fewer than
    twice the bands it placed.
 2. A BAND DOES NOT SURVIVE AN AGE ANYWAY. `add_nomads` replaces the whole block at
    every age turn, deliberately, so there is no surviving parent at a boundary to split
    from; "fission per age" would have meant inventing a lineage across a gap the
    generator does not model.
 3. AN AGE IS FIVE THOUSAND YEARS (decision 028, ruled the same day this was built). A
    rule firing once per age fires once per two hundred generations of herders. The
    cadence argument that made per-age attractive was written when an age was a century.
What is given up: a clan's descent is not legible across ages, and no band has a
grandchild. Reversing the ruling means keeping bands across an age turn FIRST, and then
this needs the ceiling and the death term point 1 names.

IDEMPOTENT BY CONSTRUCTION, because it has to be: this pass runs again on every
`nomad_request` and on the monthly route cadence of a time advance. Every child is
DISCARDED (step 2 of the route pass) and re-derived from the surviving parents rather
than added to the set, so calling it twice is calling it once.

A child is NOT written into `rolls`. `rolls` records what the point process considered
at a candidate point, and a child was never considered -- it descends. The invariant a
consumer can hold is that EVERY BAND EITHER NAMES A ROLL OR NAMES A PARENT THAT DOES.

================================================================================
PASS 3 -- apply_nomad_effects(result, cfg)
================================================================================
Gate: `if not cfg.world_recipe or cfg.phase < 16 or not result.get('nomads'): return`.
Then, in this fixed order (the order they are safe to apply in):
  result, direct, hidden = apply_cultist_leylines(result, cfg)
  result, towns         = apply_raid_pressure(result, cfg)
  result, roads         = apply_caravan_trade(result, cfg)
  result, seeds         = seed_survivor_camps(result, cfg)
then write `result['nomads']['effects']` and the timings.

--------------------------------------------------------------------------------
apply_cultist_leylines -- a pilgrimage circuit is worship, and worship on charged
ground leaves a mark
--------------------------------------------------------------------------------
    bands = [b for b in nomads.groups if b.classification == 'cultists' and b.school]
    if not bands or not cfg.magic_enabled: return result, 0, 0
    networks = magic.networks
    direct = hidden = 0
    for band in SORTED(bands, key=uid):          # sorted so two bands touching the same
                                                 # node cannot produce two different
                                                 # worlds depending on placement order
        school = band.school
        basis_node = band.basis.get('ley_node_id')
        net = networks.get(school)
        if not net or not basis_node: continue
            # A claim-derived or shrine-derived band carries NO ley_node_id, so there is
            # nothing to deepen. This guard used to cover only known schools, which is
            # why the hidden branch could queue an entry naming no node at all.
        current = first node in net['nodes'] whose id == basis_node, or None
        if current is None: continue
            # DEEPEN, NEVER CREATE. Only the corruption API seats a node in a hidden
            # network.
        lifted = min(INTENSITY_CEILING, current.intensity * (1 + DEVOTION_GAIN) - 0.0)
        if lifted == current.intensity: continue     # already at the ceiling
        networks[school] = edit_network(net, node_id=basis_node, intensity=lifted)
        if school in KNOWN_SCHOOLS: direct += 1 else: hidden += 1
`edit_network` deep-copies the network, finds the node by id, validates that intensity
is finite and in [0, 4], SETS it (does not add), and returns the copy.
`_clamp_intensity` is written as `min(4., value - 0.0)` rather than `max(0., min(4., v))`
because `max(0., -0.0)` returns `0.0` and flips a sign bit that a byte comparison
catches, for no gain.

THE RETIRED QUEUE, AND WHY IT MUST NOT BE REBUILT. There used to be a hidden/known
split here, resting on the claim that `advance_age_request` rejects any leyline edit
whose school is outside KNOWN_SCHOOLS, so a blood cult writing its own node would be
refused at the next age boundary; hidden-school intent therefore went into a
`pending_ley_edits` block that a corruption-side applier was supposed to drain. No
applier was ever written. The claim was TESTED AND IS FALSE: the age-advance gate
validates only the CALLER-SUPPLIED `body['leyline_edits']` of a request and never looks
at the world's own networks; `validate_age_world` was run against a world carrying a
hidden-school node, both newly created and intensified, and accepted both -- it asserts
the twelve networks exist and never asks which network a node sits in. This pass writes
through `edit_network` directly and never passes through that validator at all, exactly
as the known-school half already did. ONE NUMBER IS THE STRONGEST ARGUMENT AGAINST EVER
REBUILDING THE QUEUE: the direct write is `current.intensity * 1.18` -- absolute, shrine
count ignored -- while the queued value was `DEVOTION_GAIN * shrines`. On a node at 3.0
with three shrines those are 3.54 and 0.54, and `edit_network` SETS intensity rather
than adding to it, so an applier reading the queue the obvious way would have CUT the
node by 82% in the act of deepening it.
The invariant that does hold, and that the code enforces rather than merely states:
a cult may only DEEPEN a hidden node that already exists, never create one.

--------------------------------------------------------------------------------
apply_raid_pressure -- record raiders beside the nest, ley and war pressures a town
already tracks
--------------------------------------------------------------------------------
    assessments = result.get('threat_assessments'); sites = settlements.sites
    if not assessments or not sites: return result, 0
    reach = STRIKE_SPACINGS * cfg.settlement_spacing            # 2.0 spacings
    by_uid = {s.uid: s for s in sites}
    raiders = [b for b in nomads.groups if b.classification in RAID_WEIGHT]
    touched = 0
    for city in assessments['cities']:                # LIST ORDER (uid-sorted at build)
        site = by_uid.get(city.get('city_uid'))
        if not site or not site.get('direction'): continue
        pressure = 0.0; named = []
        for band in raiders:                          # groups order, i.e. uid-sorted
            span = distance(tuple(site.direction), tuple(band.direction), radius)
            if span > reach: continue
            pressure += RAID_WEIGHT[band.classification] * (1.0 - span / reach)
            named.append(band.uid)
        city['nomad_pressure'] = round(pressure - 0.0, 6)       # written even when zero
        if named:
            city['nomad_contributors'] = sorted(named)
            city['regional_threat'] = round(min(1.0,
                                     city.get('regional_threat', 0.0) + pressure), 6)
            touched += 1
Falls off with distance: a lair on the doorstep is not the same as one at the edge of
the reach, and a FLAT term made every town in a region read alike. Note this uses the
BAND'S START DIRECTION, not its lair camp and not any leg.
Raid pressure is added AFTER the threat assessment was evaluated, so it widens
`regional_threat` without having influenced anything that already read it.

--------------------------------------------------------------------------------
apply_caravan_trade -- a road a caravan rides is busier than one nobody walks
--------------------------------------------------------------------------------
    routes = roads.routes; if not routes: return result, 0
    ridden = {}                                      # route index -> [(band uid, count)]
    for band in nomads.groups:                       # groups order
        if band.classification != 'merchants': continue
        walked = set of every node in every leg of the band
        for index, route in enumerate(routes):
            shared = walked & set(route.get('nodes', []))
            if len(shared) < 2: continue             # a single shared node is a crossing
            ridden.setdefault(index, []).append((band.uid, len(shared)))
    for index, riders in ridden.items():             # INSERTION ORDER
        route = routes[index]
        carried = plain accumulation of `shared` over riders        # integers
        route['caravan_riders'] = sorted(uid for uid,_ in riders)
        route['caravan_throughput'] = round(carried /
                                     max(1, len(route.get('nodes', []) or [1])), 6)
    return result, len(ridden)
Caravan throughput is a SHARE OF NODES RIDDEN, not a modelled cargo volume, and nothing
consumes it.

--------------------------------------------------------------------------------
seed_survivor_camps -- where a refugee band settled, without pretending it is a town
--------------------------------------------------------------------------------
    candidates = []
    for band in nomads.groups:
        if band.classification != 'survivors' or band.route_status != 'routed': continue
        terminal = the FIRST camp whose kind == 'terminal', or None
        if terminal is None: continue
        candidates.append({
          id: 'settlement-candidate-' + band.uid,
          from_band: band.uid,
          node: terminal.node,
          direction: list(terminal.direction),
          migration_source_node: band.node,
          migration_distance_m: round(band.get('round_length_m', 0.0), 6),
          population_estimate: band.size,
          reason: 'Refugees from <basis.ruin_uid> who reached <basis.refuge_uid> and stayed.'
        })
    if candidates: result['settlement_candidates'] = sorted(candidates, key=id)
    return result, len(candidates)
ONLY ASSIGNED WHEN NON-EMPTY. A survivor band that reached its refuge and stopped is
exactly how a hamlet starts. It is recorded as a CANDIDATE rather than founded outright,
because founding one here would mean re-running settlement generation after every
downstream block has already read the settlements it produced -- which invalidates the
world to add one hamlet. The existing founding field names are used so a later pass can
adopt these directly.

--------------------------------------------------------------------------------
absorb_survivor_camps(result, survivors, age) -- called at an age boundary from
terrain_history.age_transition, NOT from apply_nomad_effects
--------------------------------------------------------------------------------
    candidates = result.get('settlement_candidates') or []
    if not candidates: return 0, 0
    bands = {band.uid: band for band in nomads.groups}
    standing = {site.uid: site for site in survivors if 'uid' in site}
    absorbed = people = 0
    for candidate in SORTED(candidates, key=id):
        band = bands.get(candidate.get('from_band'));  if band is None: continue
        refuge_uid = band.basis.get('refuge_uid')
        site = standing.get(refuge_uid);               if site is None: continue
        taken = list(site.get('absorbed_bands') or [])
        if band.uid in taken: continue                 # IDEMPOTENT BY BAND UID
        count = int(candidate.get('population_estimate') or 0)
        if count <= 0: continue
        site['absorbed_bands'] = sorted(taken + [band.uid])
        site['absorbed_refugees'] = int(site.get('absorbed_refugees') or 0) + count
        candidate['absorbed_age'] = age
        candidate['absorbed_into'] = refuge_uid
        absorbed += 1; people += count
    return absorbed, people

OWNER RULING, 2026-09-21. A candidate does NOT name free ground. It names the refuge
city's own node, 4 of 4 across two worlds and TRUE BY CONSTRUCTION: `_flight` routes the
band to the node of the site whose uid is `basis['refuge_uid']`, and
`seed_survivor_camps` records the candidate at `terminal['node']`. "Adopt the candidate
as a settlement" would therefore found a SECOND settlement on a standing city. The band
did not reach empty ground and start a hamlet; it reached a town and stopped, so the
town gets bigger. The rejected alternative was recording the camp one cell short of the
refuge, which buys free ground at the price of a resolution-dependent placement constant
-- 8 km at size 17, under 1 km at size 129 -- which is the exact failure this subsystem
exists to avoid.
Called with the cities that SURVIVED the boundary, so a refuge that fell this age
absorbs nobody and the band's arrival is simply not recorded. Measured on seed 42 size
17: the age-3 boundary leaves 3 of 12 cities standing and takes all three refuges with
it, so that world absorbs NOBODY -- the filter working, not the pass failing.
Idempotence has to be real, because `seed_survivor_camps` only assigns the block when it
has candidates, so a world can carry a previous advance's list unchanged; absorbing it
again would breed people every age out of one migration. The band uid `nomad-<age>-<node>`
is never reused by a later age, which is what makes the uid check sound.

================================================================================
THE REQUEST API -- nomad_request(body) (terrain_nomad_api, API_VERSION 1)
================================================================================
Stateless: same input, same output; the caller's world is unchanged; persist the
returned world to call again. This is the seam that lets a villain put cultists on the
map without either feature importing the other.
FIELDS (the exact accepted key set) = {api_version, world, node, origin, classification,
variation}. Validation: unknown key -> error naming the first sorted offender;
api_version must be exactly 1; world must be an object and must already carry a `nomads`
block; node must be a non-negative int and inside the grid; origin must be an object
with only {kind, id, age}, kind in {world, god, villain}, age a non-negative int;
classification, if given, must be one of the six; variation an int in [0, 4294967295].
The node must be land, or the request is refused.
Then: compute swing, build `ground`, evaluate ALL SIX gates. If nothing holds, raise.
If a classification was requested, it must be in the evidence or the request is refused
-- A REQUEST FOR CULTISTS ON GROUND WITH NOTHING SACRED IS REFUSED RATHER THAN QUIETLY
HONOURED. Otherwise the same cumulative weighted pick runs against
`child_seed(cfg.seed, 'nomad-request-%d' % node, variation)`, and the size is drawn from
a SEPARATE stream `child_seed(cfg.seed, 'nomad-request-size-%d' % node, variation)`.
The uid is `nomad-<origin.age>-<node>` with a `-<suffix>` disambiguator against existing
uids. The band is appended, `groups` re-sorted by uid, `counts[chosen]` incremented,
and then `add_nomad_routes`, `add_encounters` and `apply_nomad_effects` are all RE-RUN
WHOLESALE rather than patched -- cheap (the route pass is single-digit milliseconds) and
the only way the encounter index cannot drift from the bands. An operation record is
appended to `history.operations`.
DELIBERATELY NOT copying the visitation cap: a summoning cap is right for a god walking
the mortal plane and wrong for a spawner, because bands are raised across ages as a
matter of course and a cap would silently stop working partway through a long history.

## Constants

=== terrain_nomads.py ===

VERSION = 1
    The `nomads` block's schema version. Bump only when a field changes.

CLASSIFICATIONS = ('survivors','deserters','cultists','merchants','bandits','wanderers')
    EVALUATION ORDER, MOST SPECIFIC FIRST. It is stable and exported, so a new
    classification is APPENDED, NEVER INSERTED: anything that has stored an index into
    this order would otherwise be renumbered in every world ever generated. Changing the
    order changes the order shares accumulate in and therefore changes every weighted
    draw, which changes every world.

ORIGIN_CAMP_KIND = {survivors:'base', deserters:'lair', cultists:'shrine',
                    merchants:'station', bandits:'lair', wanderers:'base'}
    The camp a band starts from, before the route pass gives it a seasonal set. Changing
    a value changes that camp's CAMP_REST share and so moves the whole schedule.

ruggedness: slope divisor 22.0 (degrees), tpi sharpener 0.35, tpi divisor 40.0 (metres).
    Raising the slope divisor makes rough ground rarer and shrinks the deserter and
    bandit populations. Raising 0.35 makes ridges and ravines count for more relative to
    open steepness. The product ranges [0, 1.35].

seasonal_swing divisor 30.0 (degrees C of summer-to-winter spread mapped to 1.0).
    Lowering it makes more of the world qualify as seasonal and multiplies wanderers.

survivors gate: `scar = clamp(legacy.intensity / 4.0)`, default intensity 1.0;
    `score = clamp(scar / (1 + refuge_distance/spacing))`. The 4.0 is the ley-intensity
    ceiling, reused because a ruin's legacy intensity lives on the same scale.

deserters gate: `clamp(0.6 + 0.2 * engagements)` -- a war site with 2 or more recorded
    engagements saturates the term at 1.0. Lowering 0.6 makes a single-engagement site
    much weaker.

cultists gate: `score = clamp(strength / 4.0)`. 4.0 is INTENSITY_CEILING, so a node at
    the ceiling scores 1.0. Villain strength is `max(1, tier) * claim.influence`, so a
    tier-4 living villain also saturates.

merchants gate: `clamp(log10(1 + people) / 5.0)` -- population 100000 scores 1.0;
    a 2000-person market scores 0.661. Distance decay `1 / (1 + span/spacing)`.
    Only the nearest FOUR markets contribute; the mean is over `min(4, len(found))`.

bandits gate: `clamp(control / spacing / 3.0)` -- authority weakness saturates at three
    spacings from the nearest seat. Same `log10(1+people)/5` worth term.

wanderers gate: `clamp(forage / 0.25)` -- forage saturates at 0.25, which is well above
    the 0.07 gate floor. `clamp(0.5 + 0.5*swing)`, `clamp(0.4 + 0.6*openness)`,
    `openness = clamp(1 - slope/30)`.

placement: `room = clamp(1 - slope/45)` -- 45 degrees is the absolute slope at which a
    cell contributes nothing. `rate = occurrence * density * area_km2 / 1000 * room`.

=== nomads.json (version 2) ===
The ONLY balance lever is `weight`, because every gate returns 0..1.

candidate:
  density_per_1000km2 = 18.0     bands per 1000 km2 of habitable land, before thinning
  settlement_clearance_spacings = 0.3    the distance over which the placement rate
                                 ramps from 0 at a settlement to full at 0.3 spacings
  min_suitability = 0.12         *** DEAD. NO NOMAD CODE READS THIS KEY. *** The only
                                 `min_suitability` in the codebase is the beast-nest
                                 option `nest_min_suitability`, which is unrelated.

fission:
  classifications = ["wanderers"]      only herders split today
  rich_forage = 0.55                   round forage at which a round carries the TOP of
    the size band. CALIBRATED, not derived: a routed herding round runs 0.32-0.57 on
    seed 42 at size 33, and 0.55 seats a child for 27% of routed herders across six
    sampled populations, against 9% at 0.50 and 45% at 0.60. Must EXCEED the gate's
    `min_forage` or the capacity curve divides by zero width and every band of that
    class is either always or never over capacity -- `policy()` validates this.
  child_share = [0.3, 0.45]            the daughter takes 30-45% of the parent's head
    count, clamped back into the classification's authored size band.

per classification (weight / disposition / speed m/day / size band /
                    column_length_per_head_m):
  survivors  1.6  fleeing    11000  [30, 260]  1.4
  deserters  1.0  wary       17000  [12, 90]   2.2
  cultists   0.8  devout     13000  [15, 140]  1.8
  merchants  1.3  tradeable  24000  [8, 70]    6.0
  bandits    1.2  hostile    19000  [10, 80]   2.0
  wanderers  2.0  wary       15000  [40, 420]  3.2
  Speeds are ABSOLUTE metres per day, the one deliberate non-scaled distance, because a
  walking pace belongs to the traveller rather than to the world. `column_length_m` is
  `size * column_length_per_head_m` rounded to 6 dp -- a caravan strings out six metres
  per head because of the pack animals, a refugee column 1.4 m per head.

gates:
  survivors:  ruin_reach_spacings 0.9, refuge_reach_spacings 2.5
  deserters:  war_reach_spacings 0.8, min_ruggedness 0.32
  cultists:   min_ley_intensity 2.0, ley_reach_spacings 0.35,
              shrine_reach_spacings 0.8, claim_reach_spacings 1.2
  merchants:  road_reach_spacings 0.35, market_reach_spacings 3.0,
              min_markets 2, min_market_population 2000
  bandits:    min_ruggedness 0.34, strike_reach_spacings 1.3,
              control_clear_spacings 0.9
  wanderers:  min_forage 0.07, min_seasonal_swing 0.3,
              summer_reach_spacings 3.5, winter_reach_spacings 3.5
Thresholds are tuned so each hard gate admits a MINORITY of land: most ground raises
nobody, and a cult needs genuinely charged sacred ground rather than any ley whisper.

CULT PREVALENCE IS A FUNCTION OF HOW VIOLENT THE WORLD'S HISTORY WAS rather than an
authored constant: every destroyed city leaves a ley key point at intensity 3.5 to 4.0,
so a bloody age manufactures sacred ground. Tuning cult frequency means tuning the
WEIGHT, not the gate.

=== terrain_nomad_routes.py ===

ROUTES_VERSION = 3
    Separate from the band block's version, deliberately, and that split is the precise
    statement of what changed. It moved to 2 when lineage fission landed: no field is
    new, `parent_uid` and the `fission` branch were both declared from the start, and a
    v1 document still validates field for field -- what changed is the OCCUPANCY of those
    slots. Under v1 `parent_uid` was null on every band and `fission` never appeared, and
    the v1 `limits` sentence promised exactly that, so code written against it may have
    treated every band as independently placed and every band as present in `rolls`.
    Neither holds at 2. It moved to 3 on 2026-09-21 for the same kind of reason: no field
    is new and a v2 document still validates, but the central guarantee changed -- no
    merchant leg exceeds one day's march. A consumer that sized a day's travel off the
    longest leg, or counted a route's stations, reads different numbers at 3.

MIDSUMMER_NORTH = 0.5
    Midsummer in the northern hemisphere as a FRACTION of the year; the south is half a
    year away. Anchoring on a fraction rather than a day keeps this correct if the
    calendar length ever changes. At 360 days this is day 180.

DAY_MARCH_M = 35000.0
    A laden pack animal makes 30-40 km in a day, which is why caravan stations sat that
    far apart. ABSOLUTE metres, NOT scaled by settlement spacing -- so at a 200 km world
    (spacing ~8073 m) a march is ~4.3 spacings and at a 600 km world (~24218 m) it is
    ~1.4. Raising it makes caravan routes have fewer, longer legs; lowering it below one
    graph step (12500 m at size 17, 6250 m at size 33) makes the cut unable to satisfy
    its own bound.

GRADE_RELIEF = 2.2
    Multiplier on `cfg.road_max_grade` for the walking cost ceiling. Herds and refugees
    cross ground roads refuse: steeper, and they ford rather than bridge. Lowering this
    toward 1.0 makes bands route like roads and strands mountain herders.

CAMP_REST = {winter 5.0, summer 4.0, base 3.0, lair 5.0, shrine 2.0, station 1.0,
             terminal 0.35, calving 3.0}
    How long a band lingers, BY WHAT THE CAMP IS FOR, as a share of the spare year. A
    winter camp is where the year is survived and a lair is where a raider lives; a
    market stall and a village just robbed are both places you LEAVE. Splitting the year
    evenly across camps put a bandit at the hamlet it had raided for four months, which
    is not a raid. (`calving` is inherited from the beast-movement sibling and no nomad
    builder produces it.)

CAMP_REST_CEILING = {terminal 3, station 6, shrine 21}   -- days, integers
    Some stops are EVENTS, not seasons, however much of the year is left to divide. A
    raid and a market day are measured in days; a share of the leftover year is the wrong
    unit for them and produced a band sitting three weeks in a hamlet it had just robbed.
    A kind absent from this dict has no ceiling (the code passes `held` as the default,
    so `min(held, held)`).

DAYS_PER_YEAR = 360 (from terrain_astrology: 30 days/month x 12 months)
    Days are integers in [0, 360). DAYS_PER_YEAR // 2 == 180 is the midsummer/midwinter
    offset.

Route builder caps, all hard-coded rather than authored:
    market stations   the nearest 4 markets past the population floor
    sacred circuit    at most 3 ley nodes, nearest first
    raid orbit        at most 2 prizes, richest first
    sacred reach      claim_reach_spacings * 2.5 (= 3.0 spacings by default)
    deserter flight   war_reach_spacings * 3.0 (= 2.4 spacings by default)
    default reaches   3.5 summer, 3.5 winter, 3.0 market, 1.2 claim, 1.3 strike,
                      2.5 refuge, 0.8 war -- these `_reach` fallbacks only fire if the
                      key is missing from nomads.json, which it never is today.
travel cost          `d * (1 + 8*grade^2) * (1 - 0.25*(forage_i+forage_j)/2)`.
                     The 8 is the grade penalty (roads use 12); the 0.25 caps the forage
                     discount at 25%, so `green` is in [0.75, 1.0].
summer/winter score  `max(1, abs(base) + 400)` normalises the height difference; 400 m
                     is the floor that keeps a sea-level band from dividing by a tiny
                     number. `(temperature + 20)/45` maps -20..+25 C to 0..1.
                     `abs(tpi)/30` and `slope/30` are the shelter and openness scales.
deserter cover       `ruggedness * (0.4 + lead)` where `lead` is
                     `clamp((1 + dot(away, node_dir)) / 2)` -- 1.0 directly away from the
                     war, 0.0 directly toward it. The 0.4 floor means cover still counts
                     when direction is unknown.

=== terrain_nomad_effects.py ===

VERSION = 1                     the `effects` sub-block's version
DEVOTION_GAIN = 0.18            what a circuit leaves behind. A pilgrimage does not rip
                                the land open; it DEEPENS a groove that was already
                                there, so the gain is a fraction of the node's existing
                                charge: `intensity * 1.18`, multiplicative, shrine count
                                ignored.
INTENSITY_CEILING = 4.0         the ley scale's maximum, shared with edit_network's
                                validator. A node at exactly 4.0 is skipped (no write).
RAID_WEIGHT = {bandits 0.22, deserters 0.14}
                                a band's contribution to a town's sense of threat, per
                                band, BEFORE distance falls off. Deserters press less
                                than bandits because they are running, not hunting.
STRIKE_SPACINGS = 2.0           the reach within which a raider is felt, in settlement
                                spacings. Note this is DOUBLE the bandit gate's own
                                `control_clear_spacings` of 0.9 and wider than its
                                `strike_reach_spacings` of 1.3.

Rounding: `nomad_pressure`, `regional_threat`, `caravan_throughput`,
`migration_distance_m`, `column_length_m` and `round_length_m` are all rounded to 6
decimal places. `regional_threat` is additionally capped at 1.0.

=== terrain_scale / terrain_world (context for every "spacings" number) ===
REFERENCE_RADIUS_M = 1774.4123532462844, so REFERENCE_CIRCUMFERENCE_M = 11149.0 m.
`reach_scale(circumference_m) = circumference_m / 11149.0`.
`settlement_spacing` base 450.0 m, scaled by that factor; ~8073 / 16145 / 24218 m at the
200 / 400 / 600 km presets. REACH_MAX_M = 100000; settlement_spacing would bind at a
2477.55 km world, which no preset approaches.
THE VALUE `cfg.settlement_spacing` THAT REACHES THE NOMAD PASS IS ALREADY POST-SCALING.
Every reach in this subsystem is therefore an absolute metre figure derived at runtime,
and a spec reader implementing it needs no further scaling. DAY_MARCH_M is the single
exception: it is 35000 m at every world width.

## Data files

### Sim/icarus_sim/nomads.json

The only data file this subsystem loads. Read once via `policy()`, which is
`lru_cache(maxsize=1)` -- one parse per process. Path is resolved relative to the module
file (`Path(__file__).with_name('nomads.json')`), read as UTF-8.

Top-level shape:
```json
{
  "version": 2,
  "notes": "<prose>",
  "candidate":       { "density_per_1000km2": 18.0,
                       "settlement_clearance_spacings": 0.3,
                       "min_suitability": 0.12 },
  "fission":         { "notes": "<prose>",
                       "classifications": ["wanderers"],
                       "rich_forage": 0.55,
                       "child_share": [0.3, 0.45] },
  "classifications": { "<name>": { ... }, ... x6 }
}
```

A representative classification entry, verbatim:
```json
"merchants": {
  "weight": 1.3,
  "disposition": "tradeable",
  "speed_m_per_day": 24000,
  "size": [8, 70],
  "column_length_per_head_m": 6.0,
  "seeks": ["markets", "escort", "a safe night"],
  "carries": ["goods", "coin", "news"],
  "gate": { "road_reach_spacings": 0.35,
            "market_reach_spacings": 3.0,
            "min_markets": 2,
            "min_market_population": 2000 }
}
```

The `gate` sub-object has NO fixed schema: each classification's gate function reads the
keys it needs by name, and the route builders read some of the same keys with their own
fallbacks. The union of every key actually read is listed under `constants`.

VALIDATED ON LOAD, so a malformed row fails at import rather than at generation:
  - `version` must be exactly 2, else "Unsupported nomad policy version".
  - `classifications` must be a dict whose key SET equals `set(CLASSIFICATIONS)` exactly
    -- no extras, no omissions.
  - for every classification: `weight > 0`; `size` unpacks to `(low, high)` with
    `0 < low <= high`; `speed_m_per_day > 0`; `gate` is a dict.
  - `fission` must be a dict.
  - `fission.classifications` must be a SUBSET of CLASSIFICATIONS.
  - `fission.child_share` unpacks to `(low, high)` with `0 < low <= high < 1`.
  - for every name in `fission.classifications`:
    `fission.rich_forage > classifications[name].gate.get('min_forage', 0.0)`.
    The capacity curve runs from the gate's own forage floor to `rich_forage`, so a
    `rich_forage` at or below the floor would divide by zero width and make every band
    of that class either always or never over capacity.

`min_suitability` is present and is NEVER READ by any nomad module. Carry it in the port
for fidelity of the data file or drop it, but do not implement a suitability floor: none
exists in the reference behaviour.

### No other data file

The gods, the schools, the pantheon and the creature catalogues are read through
`result['religion']` and `result['magic']` -- the WORLD's own blocks -- rather than from
disk, and that is deliberate for the gods: `_god_for_school` reads the world's religion
block specifically because the authored pantheon file does not carry the four hidden
gods, so a blood cult would raise an error instead of finding its patron. Hidden gods
are the entries whose `family` is `'outside'`.

### Schema (for output validation, not input)
`Contracts/schemas/nomads.schema.json`, JSON Schema 2020-12, titled "Nomads v1". It
pins the block envelope, the band/camp/leg/roll shapes, the classification enum, the
camp-kind enum, the branch enum, the disposition enum, the uid pattern
`^nomad-[0-9]+-[0-9]+(-[0-9]+)?$`, and the three sub-block version consts
(`nomads` 1, `routes` 3, `effects` 1). It is a STRUCTURAL envelope only; the
classification gates themselves are validated by the test suite, which re-derives every
band's preconditions from the exported `basis`.

## Random streams

Every stream is a `random.Random` seeded from
`child_seed(master, domain, variation) = first 4 bytes, big-endian, of
sha256("tectonics-v1:<master>:<domain>:<variation>")`. In FWGC++ this is
`fwg::child_seed` (32-bit) or `Rng::child` / `child_seed64` (64-bit); since numeric
output need not match, use `Rng::child(cfg.seed, domain, variation)` throughout and DO
NOT invent a fourth derivation.

DOMAINS USED BY THIS SUBSYSTEM -- all four are string-keyed, which is what makes them
independent: a feature drawing only from new domains cannot perturb an existing stream.

  1. `'nomads-v1'`, variation = `int(options['nomad_variation'])`
     One stream for the WHOLE placement pass. Created once in `add_nomads`.
     Draws, in this order, per cell in NODE ORDER:
       - exactly ONE `random()` per land cell whose `rate > 0` (the fractional-count
         draw). This draw is taken EVEN WHEN it results in zero bands, so the stream
         position depends on how many cells reached step (e).
       - then, per drawn candidate: ONE `random()` inside `_pick` (only when at least one
         gate held) and ONE `randint(low, high)` (only when at least one gate held).
     DRAW COUNT IS DATA-DEPENDENT in three ways at once: how many cells are land, how
     many have a positive rate, and how many candidates precipitate. A candidate that
     satisfies no gate consumes NO draws beyond the cell's rate draw.
     NOTE `randint` is INCLUSIVE at both ends and CPython consumes a variable number of
     words for it. In C++, `Rng::randint(low, high)` is the matching primitive.

  2. `'nomad-fission-<parent uid>'`, variation = `int(options['nomad_variation'])`
     A FRESH stream per fission candidate, so the number of candidates cannot shift any
     other band's draws. Exactly ONE `uniform(child_share_low, child_share_high)` per
     candidate, taken BEFORE the inheritance loop, so the child's size does not depend on
     which camp it ends up seated on and a camp that fails costs nothing.

  3. `'nomad-request-<node>'`, variation = the request's `variation`
     One `random()`, only when the caller did not name a classification.

  4. `'nomad-request-size-<node>'`, variation = the request's `variation`
     One `randint(low, high)`.

THE ROUTE PASS ITSELF DRAWS NOTHING. `add_nomad_routes` creates no RNG; every choice it
makes is deterministic argmax or sort. The only randomness in the route pass is inside
`apply_fission`, from domain 2.
`apply_nomad_effects` DRAWS NOTHING at all.

TIME-ADVANCE SEEDING. On the tick path the pass is invoked with a config whose seed is
`child_seed(cfg.seed, 'time-nomads', absolute_step_index)` (and `'time-nomad_routes'`,
`'time-nomad_effects'` for the other two), keyed on the ABSOLUTE step index and never on
the call, so chunking a span cannot change which world it draws. A pass is a
deterministic function of the world and the seed, so a time-derived seed is the ONLY
thing that makes a band move at all between two ages.

DETERMINISM IS AN INVARIANT and is pinned by a test that generates the same world twice
and compares. Two things threaten it and both are handled explicitly: the cell iteration
order (fixed as node order) and the sort of bands before the ley write-back (fixed as
`sorted(bands, key=uid)`, specifically so two bands requesting the same node in a
different order cannot produce two different worlds).

## Ordering hazards

CONTAINER ORDERING IS LOAD-BEARING THROUGHOUT THIS SUBSYSTEM. Every one found:

1. **Cell iteration order drives the entire RNG stream.** `habitat_cells` returns cells
   in `sphere_grid` point order, which is `[(0,0)] + [(x,z) for z in 1..n-2 for x in
   0..n-2] + [(0,n-1)]` -- north pole, then row-major from the north, then south pole.
   NODE INDEX IS LATITUDE. The placement loop walks that order and draws from one shared
   stream, so ANY reordering changes every band in the world.

2. **`groups.sort(key=uid)` is a LEXICOGRAPHIC STRING SORT on `nomad-<age>-<node>`.**
   So `nomad-3-10` sorts before `nomad-3-9`, and `nomad-3-100` before `nomad-3-2`. This
   is NOT numeric node order. That sorted order then determines:
     - the fission candidate iteration order (and therefore which uid suffixes are taken)
     - the raid-pressure accumulation order (a float sum)
     - the caravan `ridden` dict insertion order
     - the cult ley-write order (which is re-sorted with the same key, so it is stable
       but equally lexicographic)
   A port MUST reproduce the string sort, not a numeric one, or the set of worlds is
   different even though "numbers need not match" -- because the SHAPE of which band gets
   which suffix and which camp changes.

3. **`CLASSIFICATIONS` order.** The gates are evaluated in that order, `shares` is built
   in that order, `total` accumulates in that order, and `_pick` walks the cumulative sum
   in that order. `eligible` in a roll record is emitted in that order. Appending a new
   classification is safe; inserting one is not.

4. **`_nearest` tie-break is `(distance, string key)` with strict `<`.** On an exact tie
   of both, the FIRST candidate in list order wins. The key strings differ per candidate
   list and two of them are unstable across an age:
     - `wealth` uses `site['uid']` for sites but `hamlet.get('id','')` -- the ORDINAL,
       `hamlet-7` -- for hamlets. Hamlets are rebuilt and re-sorted by defence score at
       every age, so `hamlet-7` names different ground after an advance. (The hamlet's
       stable handle is `uid` = `hamlet-node-<i>`, which this list does NOT use for the
       key, though `_gate_bandits` does use it for `prize_uid` via
       `prize.get('uid') or prize.get('id')`.)
     - `roads` uses `str(node)`, so `'10' < '9'`.
     - `settled` uses `str(enumeration index)`, same lexicographic issue, over a list that
       is `sites + ports + hamlets` in that concatenation order.

5. **`ground()` list construction order.** `wars` is ruins-first then sites-second;
   `wealth` is sites-first then hamlets-second. A tie between a ruin and a site with the
   same uid and distance resolves to the ruin. `ley` iterates `magic.networks` in DICT
   INSERTION ORDER (twelve networks) and then each network's `nodes` list order.

6. **`found.sort()` in `_gate_merchants`** sorts 4-tuples `(span, uid, site, people)`.
   If two markets are at exactly the same distance with the same uid, Python would try to
   compare the `site` DICTS and raise -- unreachable in practice because uids are unique,
   but a C++ port should sort on `(span, uid)` only and never dereference further.

7. **`found.sort()` in `_market_stations`** sorts `(dijkstra_cost, uid, node)`; ties break
   on uid then node.

8. **`nodes.sort()` in `_sacred_circuit`** sorts `(dijkstra_cost, ley_node_id, node)`.
   Ley node ids are `<school>-node-<i>` or `<ruin id>-key`, so again a lexicographic
   string tie-break.

9. **`prizes.sort()` in `_raid_orbit`** sorts `(-population, dijkstra_cost, node)`.
   Richest first; on equal population the nearest; on both equal the lowest node index.
   Note `population_of` returns a FOOD YIELD for hamlets, so a hamlet's `-population` is
   on a different scale from a city's and hamlets can outrank cities.

10. **`_best` tie-break is `(score, -node)` with strict `>`** -- highest score, then the
    LOWEST node index. Used by `_seasonal_camps` (summer and winter) and by the deserter
    flight's `cover`. `_within` feeds it in ascending node index order, but the tie-break
    makes that irrelevant.

11. **`_inheritable` sorts by `(kind != 'summer', camp.id)`** -- summer camps first, then
    by camp ID STRING, so `<uid>-camp-10` sorts before `<uid>-camp-2`.

12. **`_assign_days` rotation.** `start_index = next((k for k,l in enumerate(order) if
    l['from'] == winter.id), 0)` takes the FIRST leg leaving the winter camp, and `0` when
    none exists. `winter = next((c for c in camps if c.kind == 'winter'), None)` takes the
    FIRST winter camp; only one is ever produced.

13. **`_assign_days` "first write wins" on camp windows.** A camp reached twice only gets
    the window from its first arrival. A camp that is only ever a leg SOURCE gets its
    window from the post-pass, which iterates `order` and again takes the first matching
    leg.

14. **`_subdivide` station indices** are `len(out_camps)` at the moment of the cut, so
    station ids depend on how many legs were already subdivided and in what order legs
    appear. Legs appear in `order` order from `_legs_for`.

15. **`apply_raid_pressure` accumulates `pressure` as a float sum in `raiders` order**,
    which is groups order (uid-sorted). The result is rounded to 6 dp, which hides most
    reassociation, but the `regional_threat` cap at 1.0 can make it visible.

16. **`apply_caravan_trade` iterates `ridden.items()` in dict insertion order**, which is
    (first band that rode it, then route index). The per-route `carried` accumulation is
    over INTEGERS so it is order-independent; only the iteration order of the outer loop
    is observable, and only through nothing (each route is written independently). Safe,
    but worth knowing.

17. **`_flight` survivor target does NOT break** out of its loop, so the LAST site whose
    uid matches wins. Uids are unique, so this is equivalent to first-match, but a port
    should not assume.

18. **`war_direction` returns on the FIRST uid match** even if that record carries no
    direction (returning None rather than continuing to look for another record with the
    same uid). It scans `ruins + settlements.sites` in that concatenation order.

19. **`_god_for_school` returns the FIRST god in `religion.gods` list order** bound to the
    school. In the cultist gate's shrine branch, `evidence.update(god_id=god)` then
    OVERWRITES the shrine's own `god_id` with that first match, which need not be the
    shrine's god.

20. **`node_of` in the route pass** is built by iterating cells in order and assigning
    `node_of[direction] = i`, so a duplicate direction would keep the LAST. Directions are
    distinct per node by construction, so this never fires -- but a C++ port using a
    float-keyed map must not rely on exact equality; `nearest_node`'s linear scan gives
    the same answer (distance 0 is minimal, ties break on lowest index) and is the safe
    implementation.

21. **`shortest_paths` relaxation is strict `<`**, so an exact cost tie keeps the parent
    set by whichever node was popped first. Heap pop order for equal keys is unspecified
    in both Python's heapq and any C++ priority queue, so the parent array -- and
    therefore the exact leg node lists -- is NOT determined by the cost function alone on
    a tied graph. This is a genuine nondeterminism the reference already carries; see
    open questions.

22. **`sorted(candidates, key=id)` in `absorb_survivor_camps`** and
    `sorted(candidates, key=id)` in `seed_survivor_camps` are both lexicographic on
    `settlement-candidate-nomad-<age>-<node>`.

23. **`sorted(named)`, `sorted(taken + [uid])`, `sorted(uid for uid,_ in riders)`** are
    all lexicographic uid sorts in the output. Reproduce them; a consumer diffs them.

## Edge cases

GUARDS AND DEGENERATE INPUTS, by call site.

**add_nomads**
- `cfg.world_recipe` falsy or `cfg.phase < 16`: returns `result` untouched. No `nomads`
  key is created, so `materialize_stage(world, 15)` has no `nomads` and stage 16 does.
- `occurrence == 0`: every `rate` is 0, no cell passes `rate <= 0`, the block is written
  with empty `groups` and `rolls` and all-zero `counts`. A settled world.
- `g['settled']` empty (a world with no settlements, ports or hamlets): the clearance
  multiplier is skipped entirely rather than treated as infinite distance. `room` keeps
  its slope-only value.
- `clearance == 0` (would need `settlement_clearance_spacings == 0` or
  `settlement_spacing == 0`): `near / clearance` divides by zero. Not reachable with the
  shipped policy and a validated Config (`settlement_spacing` is bounded 10..100000), but
  a port should guard.
- A cell with `medium != 'land'` is skipped with NO roll record, so `rolls` covers only
  land candidates.
- `slope >= 45`: `room <= 0`, skipped, no roll.
- All six gates fail: a roll record with `precipitated: false`, `eligible: []`,
  `evidence: {}`, and NO `roll`/`chose`/`uid` keys. Roughly one candidate in eight.
- A gate returns a score of exactly 0: `share <= 0` so it is dropped from `shares`, is
  absent from `eligible`, and is absent from `evidence`.
- `_pick` falls off the end (float rounding makes `draw > cursor` at the last entry):
  returns the LAST eligible classification.

**Gates**
- Every gate must tolerate `_nearest` returning `(inf, None)` on an empty candidate list.
  Survivors, deserters, cultists (all three branches), merchants and the `wealth` arm of
  bandits all test `x is None` first. The bandit `control` arm does NOT: with no markets,
  `control = inf`, `inf < 0.9*spacing` is false so the gate PASSES, and
  `clamp(inf / spacing / 3)` is 1.0. A world with no cities makes maximally-unpoliced
  bandits, which is arguably correct.
- `legacy` missing or null on a fresh ruin: `(ruin.get('legacy', {}) or {})` handles both,
  and `intensity` defaults to 1.0, giving `scar = 0.25`.
- `war_history` present but empty list on a war site: `len(history) == 0` so the factor is
  `clamp(0.6) = 0.6`. Note the `wars` list is built from records with a TRUTHY
  `war_history`, so an empty list never enters it -- this branch is defensive only.
- A ley node in reach and above intensity whose school has no bound god: `_god_for_school`
  returns None and the gate returns None. It does NOT fall through to the shrine or claim
  branch, because the branches are an exclusive elif chain that already committed.
- A shrine in reach whose god has no `schools`: `school` stays None, gate returns None.
- A villain claim whose villain has no `school`: same.
- `villain['tier']` missing: defaults to 1.0; `max(1.0, tier)` floors a sub-1 tier at 1.
- `claim['influence']` missing: defaults to 1.0. A claim decayed below the 0.05 floor
  carries influence 0.0, making `strength` 0.0 and `score` 0.0, so `share <= 0` and the
  classification is dropped.
- `seasonal_environment` absent, or fewer than two months, or any month missing its
  `temperature` grid: `seasonal_swing` returns 0.0, which fails `min_seasonal_swing` of
  0.3, so NO WANDERERS AT ALL. A world generated without the ecology pass raises no
  herders and will not say why.
- `population_of` on a site with `population_estimate: None`: falls through to the urban
  and rural estimates, then to `delivered_food`, then returns 0.0. A prize worth 0 makes
  `worth <= 0` and the bandit gate returns None.

**Route pass**
- `result['nomads']` missing or `groups` empty: returns immediately, so `routes` is never
  written and a consumer must treat the sub-block as optional.
- No phase gate here, so a caller can run routes at any phase if a `nomads` block exists.
- Any builder returning None: `route_status = 'stranded'`, band keeps its single start
  camp and an empty `legs` list, counted in `routes.stranded`. This is a REAL OUTCOME,
  not an error: the wanderer gate checks forage and seasonal swing but not whether summer
  and winter ground are mutually REACHABLE, which needs the cost function, so a herder
  with nowhere to winter is discovered late and reported rather than pretended away.
- `_legs_for` returning an empty list (every leg was zero-length or unreachable): also
  stranded.
- `_seasonal_camps` returns None when summer and winter resolve to the SAME node, because
  a round with one ground is not a round.
- The start node is excluded from both seasonal candidate sets, so a base camp always has
  a non-zero base_limb leg to reference and therefore always gets a day window.
- `_market_stations` needs at least 2 reachable markets past the floor; note this is a
  DIJKSTRA-reachability test and the gate's was a great-circle one, so a band can pass the
  gate and strand here.
- `_sacred_circuit` picks nothing when the band's school has no ley node inside 3.0
  spacings by walking cost -- common for a shrine-derived or claim-derived cult, whose
  school may have no nodes near it at all.
- `_raid_orbit` excludes the start node from prizes, so a bandit lairing exactly on a
  hamlet strands.
- `_flight` survivors: the refuge must be Dijkstra-reachable within
  `refuge_reach_spacings`, which the gate did not test.
- `_flight` deserters: `war_direction` may be None (the war site has no direction, or the
  uid does not resolve), in which case `lead` is 0 and cover is pure ruggedness.
- `_legs_for` silently DROPS a leg whose endpoints share a node, and silently drops a leg
  whose trace fails. A partial circuit is emitted rather than refused.
- `_subdivide` cannot cut a leg with fewer than 3 nodes, or a single graph step wider than
  a day's march. Both are properties of the route graph, not of the cut, and neither
  arises at any shipped raster (step 12500 m at size 17, 6250 m at size 33, against a
  35000 m march).
- `_assign_days` with `total_travel >= DAYS_PER_YEAR`: `spare` falls back to
  `len(camps)`, so every camp gets `max(1, small)` = 1 day before its ceiling. The
  schedule still closes; it just stops being a year.
- `speed_m_per_day` is floored at 1.0 and every travel time at 1 day, so a zero-length leg
  still costs a day.
- `by_id.get(...)` returning None for a leg endpoint: only possible for a FISSION leg,
  whose `to` names a camp of ANOTHER band. `_assign_days` runs BEFORE the fission leg is
  appended, so this never fires in practice -- but the `is not None` guards are what make
  the code safe if it ever did.

**Fission**
- No candidates over capacity: returns `[]`, `fissioned` is 0.
- `_inheritable` empty (every camp sits on the band's own node -- impossible for a routed
  wanderer, whose summer and winter camps are both != start): the loop body never runs,
  `child` stays None, nothing is born.
- Every inheritable camp fails the gate, or its builder fails, or its legs are empty, or
  the join trace is shorter than 2 nodes: `child` stays None and NOTHING IS RECORDED. A
  daughter clan whose inherited camp cannot support a round of its own is REFUSED, not
  recorded as a stranded child. A segment with nowhere to winter has not split, it has
  starved.
- `uid` collision: the `while uid in taken` loop appends `-1`, `-2`, ... . `taken` is
  seeded from all existing band uids and grows as children are born.
- `size` is clamped back into `[low, high]` before the int cast, so a 30% share of a
  40-head clan (12) is lifted to the wanderer floor of 40, and a 45% share of a 420-head
  clan (189) stays inside the band.
- A child never splits: `candidates` filters on `not band.get('parent_uid')`, so the
  population is bounded at strictly less than twice the placed count.

**Effects**
- `cfg.magic_enabled` false, or no cultist bands with a school: `apply_cultist_leylines`
  returns `(result, 0, 0)` and writes nothing.
- A cult whose `basis` has no `ley_node_id` (shrine-derived or claim-derived): writes
  NOTHING AT ALL. This is the guard that used to cover only known schools, which is why
  the retired hidden branch could queue an entry naming no node.
- A cult naming a node that does not exist in its network: `current is None`, skipped.
  A cult may only DEEPEN, never create.
- A node already at exactly 4.0: `lifted == current.intensity`, skipped, not counted.
- `edit_network` raises `ValueError` if the intensity is outside `[0, 4]` or the node id
  is unknown -- the guards above make both unreachable from this pass.
- No `threat_assessments` or no `settlements.sites`: raid pressure returns `(result, 0)`.
- A city whose `city_uid` does not resolve, or whose site has no `direction`: skipped
  entirely, so it does NOT get a `nomad_pressure` key at all.
- A city with no raider in reach: gets `nomad_pressure: 0.0` but NO `nomad_contributors`
  and no change to `regional_threat`, and is not counted in `touched`.
- No `roads.routes`: caravan trade returns `(result, 0)`.
- A route with an empty `nodes` list: `len(route.get('nodes', []) or [1])` yields 1, so
  the division is safe -- but such a route can never share 2 nodes with a caravan anyway.
- No routed survivors, or no survivor with a `terminal` camp: `settlement_candidates` is
  NOT ASSIGNED, so a previous pass's list persists unchanged. This is exactly why
  `absorb_survivor_camps` has to be idempotent by band uid.

**absorb_survivor_camps**
- No candidates: returns `(0, 0)` immediately.
- A candidate whose `from_band` no longer resolves in `nomads.groups` (the block was
  replaced by a reclassification): skipped.
- A refuge that FELL at this boundary: not in `survivors`, so `standing.get` misses and
  the band absorbs nowhere. Measured on seed 42 size 17: the age-3 boundary leaves 3 of
  12 cities standing and takes all three refuges with it, so that world absorbs nobody --
  the filter working, not the pass failing.
- A band already in the site's `absorbed_bands`: skipped, no double count.
- `population_estimate` absent, null or <= 0: skipped.
- Writes nothing on a GENERATED world: `settlement_candidates` is written at stage 16,
  after both generation-internal age transitions, so this pass is only ever visible on a
  world that has been advanced.

**World edges and poles**
- `direction(x, z, n)` returns `(0, 1, 0)` at `z == 0` and `(0, -1, 0)` at `z == n-1`, so
  both poles are single nodes with a single direction.
- `sphere_grid` gives each pole node the area of the WHOLE ring below or above it
  (`area * (n-1)`), so a pole cell's `area_km2` is n-1 times a normal cell's and its
  placement rate is correspondingly enormous. A pole cell that is land and clear of
  settlements can raise many bands from one candidate point.
- A pole node's neighbours are the entire adjacent ring, so a leg through a pole is a
  legal single hop across a whole latitude band.
- `midsummer_day(0, n)` gives latitude exactly 90.0, which is `>= 0`, so the north pole
  takes the northern phase. `midsummer_day(n-1, n)` gives latitude -90.0, southern phase.
  At the equator row (only present when n is odd) latitude is exactly 0.0, which takes the
  NORTHERN phase.
- `travel_cost`'s diagonal-corner check is skipped when either endpoint is a pole row,
  because a pole has no meaningful corner neighbours.
- `_where` and `direction` both use `x % (n-1)` for longitude, so the seam column n-1 is
  the same place as column 0. `sphere_grid` never emits column n-1 as a distinct node.

## Reasoning worth preserving

These are the comments that record a measurement, a failed approach, or a ruling. They
are the most expensive thing in the files. Quoted or closely paraphrased.

**THE CENTRAL DESIGN CLAIM.** "The land decides who walks it. A candidate start point is
read for what surrounds it -- a fresh ruin, a battlefield, sacred ground, a road between
markets, rough country above wealth, or pasture with a seasonal swing -- and that reading
decides the band's classification before any weight is drawn. A classification whose
preconditions fail is ineligible outright, so cultists cannot rise where nothing is
sacred and caravans cannot form off the roads. A candidate that satisfies nothing
produces no band at all: empty ground is a result, not a failure." The record adds why
the inverse fails: "The obvious implementation -- roll a classification, then find
somewhere to put it -- produces cults where nothing is sacred, caravans with no road and
no market, and bandits in open country with nothing to rob. Those are not balance
problems that tuning fixes; they are the feature being wrong."

**UNNORMALISED SCORES MADE THE TABLE A LIE.** "Every gate returns a score in zero to one,
so the weight in `nomads.json` is the only balance lever. Unnormalised scores made that
table a lie: ley intensity runs to four while a bandit's score sat near a half, so
cultists won almost every contested draw no matter what the weights said." The record
calls the result "a table that looked tuned and was decorative."

**REACHES ARE MULTIPLES OF SPACING, DELIBERATELY, WITH ONE EXCEPTION.** "Every reach is a
multiple of settlement spacing, never a metre constant or a raster cell, so world scale
and raster size cannot turn a local band into a regional one. Travel speed is the one
deliberate absolute: a walking pace belongs to the traveller, not to the world." The
record quantifies the stakes: "world presets span 200 to 600 km of circumference, and
distances authored against the 11.15 km reference world would either vanish or swallow a
continent."

**WEALTH IS POPULATION, NOT city_class.** "Wealth is read as population rather than
`city_class`, which is a label ('capital', 'medium') and has no ordering to compare
against."

**THE HAMLET FALLBACK.** "Hamlets carry `population_estimate: None` -- they are sized by
the food they deliver, not by a head count -- so the chain ends at that yield. Without
the fallback a hamlet scores zero and bandits ignore the rural settlements that are their
easiest prey."

**CLEARANCE IS A RATE, NOT A REJECTION, AND THIS IS MEASURED.** "Clearance scales the
rate; it does not skip the cell. A skip discards whole cells, and a cell is 52 km2 at
raster 17 against 13 km2 at raster 33, so the same world quietly holds a different number
of bands at a different resolution -- the failure does not raise, it just changes the
world. Scaling the rate removes area instead of cells and is resolution-independent."
And separately: "Clearance belongs in the rate rather than in a rejection after the draw:
reject an already-drawn candidate and the world quietly holds fewer bands than asked."

**THE UID KEYING IS AN INVARIANT.** "Keyed to the terrain node, never to an ordinal or a
culture id: those are renumbered by an age transition and a band has to survive one. The
draw index disambiguates a cell that raises more than one band."

**WHY THE HIDDEN GODS FORCE A WORLD READ.** "Deliberately not `terrain_religion.
gods_by_id`: that reads `pantheon.json` alone, so the four hidden gods are absent from it
and a blood cult -- the exact case a villain of the blood god creates -- would raise
instead of finding its patron. Hidden gods are the entries whose family is 'outside'."

**HELD GROUND OUTLIVES ITS HOLDER.** "Deliberately not filtered to standing villains:
held ground outlives its holder, so a cult raised on a claim does not disband because the
villain that staked it fell. The holder record reached through `claim['villain']` may
therefore be fallen." And on reading influence off the claim: "Scaled by the claim's own
influence, which is 1.0 for a living holder and the fallen-claim value once its holder
goes. Reading it off the claim keeps the decay question in one place instead of in every
consumer."

**WHY THERE ARE TWO SOURCES OF WAR SITES.** "A war site is where a war was decided, which
is the ground a deserter runs from. Ruins carry the loser's history; surviving cities
carry their own."

**RUGGEDNESS IS SLOPE SHARPENED BY TPI.** "Slope carries it and the topographic position
index sharpens it -- a ridge or a ravine hides a band that open ground of the same
steepness does not."

**WHY WANDERERS EXIST.** "Wanderers exist because of this number: a round is only worth
walking where the good ground moves with the year. A flat year feeds a settled farm, not
a herd. The twelve monthly grids already carry the latitude and elevation proxy,
including the reversal across the equator, so a southern cell swings on the opposite half
of the year." And on transhumance: "Transhumance needs two grounds, not one -- high open
pasture for the warm months and sheltered low ground for the cold. A cell with forage but
no seasonal spread feeds a farm instead."

**THE THIRD BANDIT CONDITION IS AN ABSENCE.** "The productive pattern is refuge terrain
beside prosperity -- the hills supply safety and the lowlands supply targets -- so both
must hold, and authority must not."

**WHY SUMMER WANTS HEIGHT AND WINTER WANTS RELIEF.** "Summer wants height and openness --
ground that cannot be used in the cold months, so it is not competing with anyone. Winter
wants the opposite and one thing more: snow settles on the flat, so the camps that work
are tucked against relief where sun and wind strip the pasture clear."

**WHY THE START NODE CANNOT BE A SEASONAL CAMP.** "The start node is the base and cannot
also be a seasonal camp: if it were, the base_limb leg would collapse to zero length, be
dropped, and leave the base camp with no leg referencing it and so no day window at all."

**WHY THE WALKING COST IS NOT THE ROAD COST.** "Not `road_cost_function`: that refuses a
river without a bridge and caps grade at the road limit, both of which describe an
engineered route rather than a walked one. A band fords, and takes ground a road would
not. Water is still impassable -- these are not boats -- and the forage term makes a herd
prefer the green way round."

**A RAIDER ORBITS, IT DOES NOT MIGRATE.** "A raiding band does not migrate. The spur
returning to the lair is the whole shape: strike, vanish, and be somewhere defensible
before anyone organises."

**THE SUBDIVIDE RULING, WITH ITS MEASUREMENT.** "**No segment exceeds one day's march**
-- owner ruling, 2026-09-21, on NOMAD-SUBDIVIDE-DROPS-THE-LAST-CUT. The rejected reading
was that the bound is wrong and a herder day is elastic, so a short tail may be absorbed
into the previous day; it is not obviously wrong, and the reason it lost is that a
station exists to stop a train sleeping in the gap, which a 1.5-day segment does not do
at any elasticity." And: "The cut therefore closes at `a`, the last node the day's walk
reaches, rather than at `b`, the first node past it. Cutting on the crossing overshoots
by up to one graph step -- 6250 m at size 33, 17.9% of the march -- which would still
break the assertion it is meant to satisfy. The old tail condition `remaining >
day_march * .5` was worse than either: it declined to cut whenever the remainder was half
a march or less, so the remainder was absorbed backwards and a segment could reach 1.5
marches. Measured on seed 42 size 33, leg `nomad-2-723-camp-3 -> camp-4`: 11 steps,
cumulative 34730.3 m at the tenth node and 40980.3 m at the eleventh, so the only
crossing left 3125 m to run and no cut fired at all. It is emitted whole at 1.171
day-marches." And on the head of a segment: "`run > 0.` is not defensive: at the head of a
segment there is no earlier node to fall back to, so a single over-long hop is walked
rather than refused." And on the removed repair: "The tail always holds at least one
step: the cut closes at `a` and `b` is appended after it, so the last node is never a cut
point. The old shape cut at `b` and could therefore end with a one-node tail, which is
what the removed `elif out_legs: out_legs[-1]['to'] = leg['to']` branch repaired. It is
unreachable under this cut, so it is gone rather than left to be trusted." And on what it
cannot fix: "a leg with no intermediate node has nowhere to stand a station, and neither
has a single hop inside a longer leg. A graph step wider than a day's march is emitted
over-long by construction. It does not arise at any raster the generator ships -- the
step is 12500 m at size 17 and 6250 m at size 33 against a 35000 m march -- and it is a
property of the route graph, not of this cut."

**WHY CAMPS DO NOT SPLIT THE YEAR EVENLY.** "How long a band lingers, by what the camp is
for. A winter camp is where the year is survived and a lair is where a raider lives; a
market stall and a village just robbed are both places you leave. Splitting the year
evenly across camps put a bandit at the hamlet it had raided for four months, which is
not a raid." And on the ceiling: "Some stops are events, not seasons, however much of the
year is left to divide. A raid and a market day are measured in days; a share of the
leftover year is the wrong unit for them and produced a band sitting three weeks in a
hamlet it had just robbed."

**THE WINTER ANCHOR.** "A herding round is anchored so the winter camp is occupied at
midwinter -- that is the camp survival depends on, so it is the one the year is built
around. Everything else starts at its own midsummer and runs on travel time, which is
length over speed."

**THE EQUATOR INVERSION IS PINNED BY A TEST FOR A REASON.** "Days are integers in [0,
DAYS_PER_YEAR) and the seasonal phase **inverts across the equator**, so a southern clan
winters on the opposite half of the year. Getting that wrong is invisible on a world
whose land happens to sit in one hemisphere, which is why it is pinned by a test rather
than by inspection."

**THE BASE CAMP WINDOW.** "A camp that is only ever a leg's source -- the base a
transhumant group holds while the herd moves -- is never reached by the loop above, so it
would carry no window at all. It is occupied from when the moving limb leaves it until
the limb returns."

**THE ROUND IS NOT THE START CELL, AND THE TWO WERE MEASURED.** "The round rather than
the start cell, because the route pass picks the best ground in reach: a clan's start
point says where it was raised and its camps say what it eats. Measured on seed 42 at
size 33, a routed herding round runs 0.32 to 0.57 while the wanderer gate's floor at the
start cell is 0.07 -- the two are not the same quantity and a capacity read off the floor
would never bind."

**CAPACITY IS DIMENSIONLESS ON PURPOSE.** "The obvious model -- head per square kilometre
of forage -- reads the raster rather than the land, exactly as the placement rate would
have if clearance had been a rejection instead of a rate: a cell is 52 km2 at raster 17
against 13 km2 at raster 33, so the same clan on the same ground would outgrow it at one
resolution and not the other. The authored `size` band is already this module's statement
of how large a band of a given kind gets, so the round's forage picks a point on it: a
round at the gate's forage floor carries the bottom of the band, a round at `rich_forage`
carries the top."

**THE rich_forage CALIBRATION.** From nomads.json: "A routed herding round runs 0.32 to
0.57 on seed 42 at size 33 -- well above the gate's 0.07 start-cell floor, because the
route pass picks the best ground in reach -- and 0.55 seats a child for 27 percent of
routed herders across six sampled populations, against 9 percent at 0.50 and 45 percent
at 0.60."

**THE FISSION CADENCE RULING, RECORDED SO IT CAN BE REVERSED KNOWINGLY.** Three reasons,
the first two load-bearing. "1. *Nothing kills a band.* Per-age fission grows the
population every age with no term removing anyone, so band count rises without a ceiling.
Once at placement is bounded by construction: at most one child per placed band, and a
child never splits, so a world holds fewer than twice the bands it placed. 2. *A band
does not survive an age anyway.* `add_nomads` replaces the whole block at every age turn,
deliberately -- an aged world reclassifies against the world it actually has, because a
carried band would name ruins that had moved and refuges that no longer stood. There is
no surviving parent at an age boundary to split from, so 'fission per age' would have
meant inventing a lineage across a gap the generator does not model. 3. *An age is five
thousand years* (decision 028, ruled 2026-09-21, the same day this was built). A rule
that fires once per age fires once per two hundred generations of herders, which is not a
demographic model of anything. The cadence argument that made per-age look attractive was
written when an age was a century." And: "What is given up by choosing this: a clan's
history across ages is not legible, and no band has a grandchild. Reversing the ruling
means keeping bands across an age turn first, and then this needs the ceiling and the
death term point 1 names."

**FISSION IDEMPOTENCE IS STRUCTURAL.** "Idempotent by construction, because it has to be:
this pass runs again on every `nomad_request` and on the monthly route cadence of a time
advance. Every child is discarded and re-derived from the surviving parents rather than
added to the set, so calling it twice is calling it once." And at the call site:
"Daughter clans are derived from the parents below, never carried in. This pass runs
again on every `nomad_request` and on a monthly route cadence, so a fission step that
added to the set instead of replacing it would breed a new generation per call."

**THE CHILD IS REFUSED, NOT STRANDED.** "A segment with nowhere of its own to winter has
not split, it has starved. Refused rather than recorded as a stranded child."

**THE FISSION LEG CARRIES NO YEAR.** "The move that founded the child, appended after its
round is scheduled and deliberately carrying no day window: it happened once, so it is
not a leg of anybody's year, and giving it one would put the child at its parent's camp
every spring. `to` names a camp of the band in `parent_uid`; that is what makes the branch
a join between two rounds rather than one inside a round." And on the exclusion: "The
annual round only. The fission leg is not walked every year, so counting it would tell a
time mover the clan travels further than it does."

**WHY THE DAUGHTER TAKES SUMMER PASTURE FIRST.** "Its own start camp is not one of them:
a split that leaves the child standing where the parent already stands has divided
nothing. Summer pasture first, because high open ground in the warm months is what a herd
that has outgrown its range competes for."

**THE CHILD PASSES THE REAL GATE.** "The real gate, not a restatement of it. A child is
only seated on ground that would raise a band of its kind on its own merits, so every
precondition the classification tests assert of a placed band holds of a daughter band
too."

**COUNTS ARE RECOMPUTED, NOT INCREMENTED.** "Recomputed from the band set rather than
carried, because three callers add to it -- placement, the request API and fission -- and
a tally that is only ever incremented cannot survive a pass that removes anybody."

**STRANDED IS AN HONEST OUTCOME.** "A band whose circuit cannot be built keeps its start
camp and says so. The honest outcome: a herder with nowhere to winter is a real thing the
gates cannot see, because reachability needs this pass to exist."

**THE RETIRED LEY QUEUE, AND THE NUMBER THAT KILLED IT.** "There used to be a ley split
here, and it rested on a premise that was tested and is false. It held that
`advance_age_request` rejects any leyline edit whose school is outside `KNOWN_SCHOOLS`,
so a blood cult writing its own node would be refused at the next age boundary;
hidden-school intent therefore went into a `pending_ley_edits` block that a
corruption-side applier was supposed to drain. No applier was ever written. The gate
validates only the caller-supplied `body['leyline_edits']` of an age-advance request. It
never looks at the world's own networks. `validate_age_world` was run against a world
carrying a hidden-school node, both newly created and intensified, and accepted both - it
asserts the twelve networks exist and never asks which network a node sits in. This pass
writes through `edit_network` directly and never passes through that validator at all,
exactly as the known-school half already did. The queue was working around a gate that
did not apply to it, so it is gone." And: "One number is worth recording, because it is
the strongest argument against ever rebuilding the queue: the direct write is
`current['intensity'] * 1.18` - absolute, shrine count ignored - while the queued value
was `DEVOTION_GAIN * shrines`. On a node at 3.0 with three shrines those are 3.54 and
0.54, and `edit_network` SETS intensity rather than adding to it, so an applier reading
the queue the obvious way would have cut the node by 82% in the act of deepening it."
The invariant that DOES hold: "a cult may only **deepen** a hidden node that already
exists, never create one. Only the corruption API creates a node in a hidden network."

**WORSHIP DEEPENS A GROOVE.** "What a circuit leaves behind. A pilgrimage does not rip
the land open; it deepens a groove that was already there, so the gain is a fraction of
the node's existing charge."

**THE MINUS-ZERO TRICK.** "`x - 0.0` rather than max(0., x): max(0., -0.0) returns 0.0
and flips a sign bit that a byte comparison catches, for no gain."

**THE SORT THAT PREVENTS TWO WORLDS.** "Sorted so two bands touching the same node cannot
produce two different worlds depending on which happened to be placed first."

**RAID PRESSURE FALLS OFF, AND A FLAT TERM WAS TRIED.** "Falls off with distance: a lair
on the doorstep is not the same as one at the edge of the reach, and a flat term made
every town in a region read alike."

**SURVIVOR CAMPS ARE CANDIDATES, NOT SETTLEMENTS.** "A survivor band that reached its
refuge and stopped is exactly how a hamlet starts. It is recorded as a candidate rather
than founded outright, because founding one here would mean re-running settlement
generation after every downstream block has already read the settlements it produced --
which invalidates the world to add one hamlet."

**THE ABSORPTION RULING, WITH ITS REJECTED ALTERNATIVE.** "**Owner ruling, 2026-09-21.**
A candidate does not name free ground. It names the refuge city's own node, 4 of 4 across
two worlds and true by construction: `terrain_nomad_routes._flight` routes the band to
the node of the site whose uid is `basis['refuge_uid']`, and `seed_survivor_camps`
records the candidate at `terminal['node']`. 'Adopt the candidate as a settlement' would
therefore found a second settlement on a standing city. The band did not reach empty
ground and start a hamlet; it reached a town and stopped, so the town gets bigger. The
rejected alternative was recording the camp one cell short of the refuge, which buys free
ground at the price of a resolution-dependent placement constant -- 8 km at size 17,
under 1 km at size 129 -- which is the exact failure this subsystem exists to avoid."

**THE FIELD THAT HAD TO BE RE-VERIFIED.** "**The absorbed count has to ride
`CARRIED_SURVIVOR_KEYS`, not `population_estimate`.** The founding seam worked out on the
card does not transfer here, and this is the part that had to be re-verified rather than
assumed: `add_settlements` re-derives `population_estimate` from the population budget on
every rebuild (`residents = allowance//len(members) + ...`), so a number added to a
survivor row before `rebuild_tail` is overwritten by the very rebuild it was meant to
ride. What survives is `absorbed_refugees`, which the budget adds back after the capacity
split." And from the settlements module: "Refugees a city took in at an age boundary are
people the capacity split cannot know about: the split is derived from the ground this
city farms, and these walked in from a town that fell. Added after it, so the split stays
a pure function of habitat and the arrivals stay auditable in `absorbed_bands`."

**MEASURED ABSORPTION ON THE REFERENCE WORLD.** "Measured on seed 42 size 17: the age-3
boundary leaves 3 of 12 cities standing and takes all three refuges with it, so that
world absorbs nobody -- the filter working, not the pass failing."

**WHY THE REQUEST API HAS NO CAP.** "Deliberately **not** copying
`visitation.MAX_VISITATIONS`. A summoning cap is right for a god walking the mortal plane
and wrong for a spawner: bands are raised across ages as a matter of course, and a cap
would silently stop working partway through a long history." And: "A caller can ask for a
classification, but it only gets one whose gate the ground actually passes -- so a request
for cultists on ground with nothing sacred is refused rather than quietly honoured. The
land decides who walks it; that does not stop being true because something else asked."

**WHY EFFECTS RE-RUN ON EVERY TICK.** "`apply_nomad_effects` runs on the tick path for
the same reason it runs on the age path: `add_nomads` replaces the block wholesale, which
discards `nomads.effects` and would otherwise leave the previous population's cultist ley
deepening, raid pressure, ridden roads and survivor camps standing in a world whose bands
no longer exist."

**MECHANISM, NOT GENRE CONVENTION.** From the conformance record: "Each rests on a
mechanism rather than a genre convention. Transhumance is two long moves a year between
summer pasture and sheltered winter ground, not continuous drift. Caravanserai sat a
day's march apart precisely so a caravan never overnighted in the unpoliced gaps, which
is a bound and not a target. Banditry wants rugged refuge beside prosperity and weak
authority, which is why the third bandit condition is the *absence* of a seat. Refugees
overwhelmingly stay near home. Pilgrimage is a fixed repeated circuit widely read as
tracing ley lines, which is why cultists snap to the world's existing networks rather
than inventing geometry."

**CULT PREVALENCE IS A CONSEQUENCE, NOT A CONSTANT.** "Cult prevalence is a function of
how violent the world's history was rather than an authored constant: every destroyed
city leaves a ley key point at intensity 3.5 to 4.0, so a bloody age manufactures sacred
ground. Tuning cult frequency means tuning the weight, not the gate."

**THE COMPENSATED-SUM COMMENTS.** Three places say the same thing: "Plain accumulation,
not sum(): CPython compensates a builtin sum of floats and a native port would have to
replicate that to stay bit-exact. Not worth buying here." In a CLEAN REWRITE these are
simply loops and the comment's concern evaporates -- but note that the REFERENCE uses
compensated `sum()` in `distance()` and in `nearest_settled`, and a port that wanted
numeric parity would have needed Neumaier compensation there. This port does not.

**WHAT THE PASSES EXPLICITLY DO NOT ESTABLISH.** From `limits` and the record: fission is
a split, not a demographic model, and a fissioned parent keeps its own head count,
because rewriting a placed band's `size` from here would make the authored size band a
fiction. Circuits are STATIC: a band walks the same round every year and nothing
re-routes it when the world changes underneath. Rest is split evenly across camps rather
than earned from forage, so a rich camp holds no longer than a poor one. Sizes and speeds
are authored constants, not derived from carrying capacity. No band consumes forage,
raids, trades or dies. Caravan throughput is a share of road nodes ridden, not a modelled
cargo volume, and nothing consumes it. Raid pressure is added AFTER the threat assessment
was evaluated. `beast_movements` and `encounters` are NOT owned here.

## Notes for the C++ implementation

Read `include/fwg/` before writing any of this. The library has NO dependencies: its own
JSON writer, SHA-256, RNG and test harness.

### What maps cleanly

- `distance(a, b, radius)` -> `radius * std::acos(std::clamp(dot(a,b), -1.0, 1.0))` on
  `fwg::Vec3`. The reference uses CPython's compensated `sum()` for the dot product and
  says so in a comment; since numbers need not match, a plain three-term dot is correct
  here. Do NOT port the Neumaier compensation.
- `clamp(v) = std::clamp(v, 0.0, 1.0)`.
- `direction(x, z, n)` already exists as `fwg::direction`.
- `sphere_grid(n, radius)` already exists as `fwg::sphere_grid`, returning a
  `shared_ptr<const SphereGrid>` with `point_x`, `point_z`, `areas` and `neighbours`
  (each `{index, distance}`). The Python `points` list is `(point_x[i], point_z[i])` and
  the Python `graph[i]` is `neighbours[i]`. HOLD THE SHARED_PTR: the cache evicts and a
  reference dangles.
- `child_seed(cfg.seed, domain, variation)` -> `fwg::Rng::child(master, domain,
  variation)`. Four domains: `"nomads-v1"`, `"nomad-fission-<uid>"`,
  `"nomad-request-<node>"`, `"nomad-request-size-<node>"`. Build the last three as
  `std::string` and pass as `string_view`. DO NOT INVENT A FOURTH DERIVATION -- the
  header says three exist and a fourth must be declared there if ever needed.
- `rng.random()` -> `Rng::random()`. `rng.randint(low, high)` -> `Rng::randint(low, high)`
  (inclusive both ends, already matching). `rng.uniform(a, b)` -> `Rng::uniform(a, b)`.

### What has no direct equivalent and needs real work

**1. There is no `World` block model yet.** `fwg::World` today holds `layers` (a
`map<string, Grid>`), `plates`, some scalars and timings. Everything this subsystem reads
-- settlements, hamlets, roads, ruins, religion, villains, magic networks, history ages,
seasonal_environment, threat_assessments -- and everything it writes are TYPED BLOCKS
that do not exist. Define them as structs on `World`, not as a generic JSON tree. Suggested
minimum for this subsystem:

```cpp
struct NomadCamp   { std::string id; int node; Vec3 direction; CampKind kind;
                     std::optional<int> arrive_day, depart_day; };
struct NomadLeg    { std::string from, to; std::vector<int> nodes; double length_m;
                     std::optional<int> depart_day, arrive_day; Branch branch; };
struct NomadBasis  { /* see below -- NOT a free-form map */ };
struct NomadBand   { std::string uid; Classification classification;
                     std::optional<std::string> parent_uid;
                     Origin origin; std::optional<std::string> god_id, school;
                     int node, x, z; Vec3 direction; int biome; int size;
                     double speed_m_per_day, column_length_m;
                     Disposition disposition;
                     std::vector<std::string> seeks, carries;
                     std::vector<NomadCamp> camps; std::vector<NomadLeg> legs;
                     NomadBasis basis; RouteStatus route_status;
                     std::optional<double> round_length_m; };
```

**2. `basis` is a heterogeneous dict in Python and must become a typed variant.** The six
gates produce six different key sets, and the cultist gate produces THREE. A
`std::variant<SurvivorBasis, DeserterBasis, CultBasis, MerchantBasis, BanditBasis,
WandererBasis>` where `CultBasis` itself carries an optional ley/shrine/claim arm is the
right shape. The JSON writer then serialises whichever arm is live. Do NOT reach for a
`map<string, JsonValue>` -- the whole point of `basis` is that a consumer joins on it.

**3. `tier` means four different things in this codebase and `camp` collides too.**
Disambiguate in the C++ type names:
  - `villain['tier']` (read by the cultist gate) is a VILLAIN POWER LEVEL, a float, and
    is what scales cult strength.
  - `key_locations.tier` elsewhere is a SCALE, not danger.
  - `beast_nests` tier is DANGER TO A PLAYER, 1..5.
  - the conformance record notes a fourth usage.
  `camp` likewise: a nomad `NomadCamp` is not a beast-nest camp and not a `camp` in the
  creature placement sense. Name the enum `NomadCampKind`.

**4. `fields` is a per-cell dict of every layer plus derived keys.** In C++ this is a
struct built once per node from the `Grid` layers:
```cpp
struct HabitatCell { int node, x, z; double area_km2; int biome; Vec3 direction;
                     Medium medium; double slope_deg, tpi_m, height_m, temperature_c,
                     food_potential, natural_food_potential;
                     double swing = 0.0;   // filled lazily by seasonal_swing
                     /* plus whatever the other stage-16 passes need */ };
```
Note `cell['swing']` is MUTATED in place by both `add_nomads` and `apply_fission`, and
`apply_fission` re-reads `cells` that the route pass also holds -- so the cell vector is
shared mutable state across the two passes. Keep that (it is only a memoisation) or
compute swing on demand; both are correct because `seasonal_swing` is pure.

**5. The gate dispatch table.** Python has `GATES = {name: function}` and calls
`GATES[name](cell, g, gate_rule, result)`. In C++ use a fixed-size array of function
pointers indexed by the `Classification` enum, iterated in enum order, so the
CLASSIFICATIONS order is structurally enforced rather than remembered. The enum order IS
the evaluation order and IS append-only.

**6. The gate rule is a heterogeneous JSON sub-object.** Rather than a
`map<string, double>` lookup per gate call (which is what Python does), parse
`nomads.json` once into a typed `NomadPolicy` with a per-classification struct that has
named fields plus the shared `weight/size/speed/...`. Keep `policy()`'s five validation
rules as constructor invariants that throw.

**7. Dijkstra.** `shortest_paths` is a textbook lazy-deletion binary heap. In C++:
```cpp
std::priority_queue<std::pair<double,int>, std::vector<...>, std::greater<>> queue;
```
with the `if (value != distances[i]) continue;` stale check and strict `<` relaxation.
TWO THINGS TO GET RIGHT: (a) neighbours must be visited in ASCENDING node index order,
which `SphereGrid::neighbours` already gives because the Python builds them from a sorted
set; (b) `std::priority_queue` breaks ties on the SECOND tuple element in a different way
than Python's heapq for equal first elements. Since numbers need not match this is
acceptable, but it means the parent array on a tied graph is implementation-defined in
both. If you want it pinned, make the queue key `(cost, node)` and compare
lexicographically -- that makes the result a function of the graph alone.

**8. The paths cache.** `paths(start)` is memoised per start node for the whole route
pass, unbounded. On a size-129 world with many bands this can be a large number of full
Dijkstras over ~16k nodes. Use `std::unordered_map<int, std::shared_ptr<PathResult>>`
where `PathResult` holds `vector<double> distances; vector<int> parent;`. Note
`_legs_for` calls `paths(src.node)` for any leg whose source is not the band's start, so
the cache genuinely gets hit across bands.

**9. Ordering.** The uid sort is a LEXICOGRAPHIC `std::string` sort, which
`std::sort` on `std::string` gives for free -- but make it `std::stable_sort` or make the
comparator total, because uids are unique so either works. Do NOT be tempted to sort
numerically on the node. Same for `sorted(named)`, `sorted(absorbed_bands)`,
`sorted(caravan_riders)` and the candidate-id sort.
The `_nearest` tie-break is `(double, std::string)`; use `std::pair<double,
std::string_view>` and strict `<`, first-wins.
`_best` is `(double score, -int node)` with strict `>`; write it as
`if (best == nullopt || score > best_score || (score == best_score && node < best_node))`.

**10. Python `int(x)` truncates toward zero; `round(x)` is BANKER'S ROUNDING (half to
even).** Both appear:
  - `int(rate)`, `int(spare * share / weight)`, `int(min(high, max(low, round(...))))` --
    truncation, so `static_cast<int>`.
  - `int(round(leg.length_m / speed))` in `_assign_days` (twice) and
    `round(band.size * u)` in fission -- BANKER'S. `std::round` is half-away-from-zero.
    Since numbers need not match this is a free choice, but it is a real behavioural
    difference at exact halves and should be a deliberate one. Recommend `std::llround`
    and note the divergence.
  - `round(x, 6)` on output floats is a DECIMAL round to 6 places, not a bit operation.
    Implement it once as a helper and use it everywhere the reference does, or drop it
    and let the JSON writer control precision -- but be consistent, because
    `round(pressure - 0.0, 6)` and `round(min(1., threat + pressure), 6)` both feed
    comparisons downstream.

**11. `math.log10` and `math.acos`** are used in the gates. `std::log10` / `std::acos`.
No `pow` except `grade ** 2` in the cost function, which should be written `grade*grade`.

**12. `str(node)` as a sort key** appears in the roads and settled candidate lists. If
you keep the lexicographic behaviour you must actually format the integer; if you decide
a numeric tie-break is acceptable (it is, under "numbers need not match"), SAY SO in the
port's own record, because the two give different bands on tied cells.

**13. The `_where` resolution chain** is three-way polymorphic over Python dicts. In C++
give every placeable record a `std::optional<Vec3> direction` and an `int node = -1`, and
write one free function `resolve_direction(const T&, const SphereGrid&)` templated or
overloaded. The three arms are: explicit direction; `(x, z)`; node index (bounds-checked).

**14. `edit_network` deep-copies the whole network per edit.** In C++ that is a
`Network` copy per cult write; with a handful of cults per world it is free, but the
copy-on-write semantics matter for callers holding a reference. Consider making the C++
version mutate in place and keeping the validation (`0 <= intensity <= 4`, node must
exist) as an assertion, since the nomad pass is the only caller here that needs it.

**15. Things the reference gets from CPython that you must not emulate:** the compensated
`sum()`, the `random` module's exact stream, dict insertion ordering as a guarantee
(replace with explicit `std::vector<std::pair<K,V>>` where iteration order is
load-bearing -- the `magic.networks` iteration and the `ridden` dict are both such
places), and `float` repr. None of these are behavioural requirements under a clean
rewrite; all of them are places a naive port silently changes the SHAPE of the world.

**16. Stage gating.** `add_nomads` and `apply_nomad_effects` check
`cfg.world_recipe && cfg.phase >= 16`; `add_nomad_routes` does NOT. Reproduce that
asymmetry: the route pass is callable standalone by the request API and by the monthly
tick, and a phase gate there would break both.

**17. Timing.** Three entries into `World::timing_ms` (`nomads`, `nomad_routes`,
`nomad_effects`), each ALSO added to a running `total`. `fwg::measure` already exists.

**18. Order of the three passes is a hard dependency** and is enforced today only by the
call site. In C++, make `add_nomad_routes` assert that a `nomads` block exists and
`apply_nomad_effects` assert that routes have run (or tolerate their absence the way the
reference does -- `seed_survivor_camps` filters on `route_status == 'routed'`, so an
unrouted world simply produces no candidates).

## Open questions

1. **`candidate.min_suitability` (0.12) in `nomads.json` is dead.** No nomad module reads
   it. It is either a leftover from an earlier design in which the placement rate also
   multiplied by a habitat suitability score, or an intended-but-never-wired knob. A
   porter must decide: drop it from the C++ policy struct, or carry it inert for data-file
   fidelity. It cannot be implemented, because there is no reference behaviour to
   implement -- guessing at a suitability term would change every world's band count.

2. **`CAMP_REST['calving'] = 3.0` is dead for nomads.** No nomad builder produces a
   `calving` camp; the kind belongs to `terrain_beast_movement`, whose seasonal builder
   uses it for the start camp of a herd circuit. The schema's camp-kind enum lists it, so
   a validator would accept one. Keep the enum member for schema compatibility; the rest
   share is unreachable.

3. **Dijkstra parent ties are implementation-defined in the reference.** The relaxation is
   strict `<`, so on a graph with exactly equal path costs the parent -- and therefore the
   exact node list of a leg -- depends on heap pop order for equal keys, which neither
   Python's `heapq` nor `std::priority_queue` specifies. The reference has always carried
   this. A porter should decide whether to pin it (make the queue key `(cost, node)` and
   compare lexicographically, which makes the result a function of the graph alone) or to
   leave it as it is. Pinning is strictly better and costs nothing.

4. **The cultist gate's shrine branch overwrites the shrine's own god.** In branch B,
   `evidence['god_id']` is set to `shrine.get('god_id')`, then the final
   `evidence.update(school=school, god_id=god)` replaces it with whatever
   `_god_for_school` returned FIRST for that school. Since the school was derived from the
   shrine's god's `schools[0]`, the two usually agree, but they need not: if two gods
   share a school affinity and the other one appears earlier in `religion.gods`, the band
   is recorded as worshipping a god other than the one whose shrine classified it. This
   looks unintended. A porter must decide whether to preserve it (safest -- it is the
   reference behaviour and the conformance record does not flag it) or fix it by not
   overwriting a god already present in the evidence.

5. **`population_of` returns a food yield for hamlets and a head count for cities, and the
   two are compared on the same scale.** `_raid_orbit` sorts prizes by `-population_of`,
   mixing the two, so a high-yield hamlet can outrank a small city as a raid target. The
   `_gate_bandits` `worth` term applies `log10(1+x)/5` to both. The source comment
   defends the fallback ("without it a hamlet scores zero and bandits ignore the rural
   settlements that are their easiest prey") but does not address the unit mismatch. It is
   a deliberate approximation, not obviously a defect, but a porter reimplementing from
   this spec should know the numbers are not commensurable.

6. **The merchant gate and the merchant route builder measure "market reach" with
   different metrics.** The gate uses `market_reach_spacings * spacing` against a
   GREAT-CIRCLE distance; `_market_stations` uses the same constant against a DIJKSTRA
   WALKING COST, which is always >= the great-circle distance and can be much larger on
   rough ground. So a band can pass the gate on two markets and strand because neither is
   reachable within the same numeric reach. This is consistent with the module's own
   philosophy ("a herder with nowhere to winter is discovered late and reported"), and
   `route_status: "stranded"` is the documented outcome, so it is probably intended -- but
   it is not stated anywhere and a porter could reasonably read the shared constant as
   implying a shared metric.

7. **`_sacred_circuit` uses `claim_reach_spacings * 2.5` regardless of which gate branch
   classified the band.** A ley-derived cult, classified within 0.35 spacings of its node,
   gets a 3.0-spacing circuit reach keyed off the CLAIM constant. It works, but the choice
   of key looks incidental rather than reasoned; nothing in the source explains it. A
   porter should carry it verbatim and flag it rather than "fix" it.

8. **`ICARUS_CITY_WORKERS` does not appear in this subsystem.** The brief warns that both
   settlement planners fork a process pool controlled by that undocumented env var and
   that nothing ever proved pooled output equals serial. I grepped: neither
   `terrain_nomads.py`, `terrain_nomad_routes.py` nor `terrain_nomad_effects.py`
   references it, and none of them forks anything. They do, however, run AFTER
   `fill_cities` / `fill_hamlets` / `fill_castles` at stage 16, and they read
   `settlements.sites` and `humans.hamlets`. If pooled and serial planner output ever
   differ, the nomad pass reads the difference. Not a nomad defect; recorded so the
   dependency is visible.

9. **Nothing in this subsystem is covered by a numerical conformance fixture.** The
   `docs/conformance/nomads.md` record exists and is unusually complete -- I found NO
   disagreement between it and the code on any point I checked, including the routes-v3
   subdivide change, the fission cadence, the retired ley queue and the absorption seam.
   The one thing it does not mention is the dead `min_suitability` key. Tier is
   `EXERCISED`, so the record asserts behaviour through tests rather than through pinned
   output, which is the right shape for a clean rewrite to target.

## Addenda from the audit

The reader above wrote the specification; a second reader went back to the source
to find what it had left out or got wrong. Both are kept: the omissions are
content the implementer needs, and the corrections say where the specification
above is not to be trusted.

### Corrections

- **Claimed:** In habitat_cells, a cell's `biome` is `int(natural_biome or biome)`.
  **Actually:** It is `int(f.get('natural_biome', f.get('biome', 0)))` — a dict lookup with a default, not an `or`. The difference is real and silent: if the `natural_biome` layer EXISTS and its value at that cell is 0, the reference uses 0, whereas the spec's `or` form falls through to `biome`. A port written from the spec emits a different `biome` on every cell whose natural_biome is 0, and that value is copied straight onto the band record and onto every fission child. The fallback to `biome` fires only when the `natural_biome` key is absent from the layer set entirely.

- **Claimed:** "`_raid_orbit` excludes the start node from prizes, so a bandit lairing exactly on a hamlet strands."
  **Actually:** Excluding the start node strands the band only if that settlement was its ONLY prize within `strike_reach_spacings * spacing` by Dijkstra cost. `prizes` is built over all of `ctx['sites'] + ctx['hamlets']` and the builder returns None solely on `if not prizes`. A bandit lairing on a hamlet with any other reachable settlement in strike range routes normally — it simply gets no spur back to the settlement it sits on.

### Missing — blocking

**cfg.road_max_grade: no value, unit or range, although travel_cost's impassability ceiling is built entirely from it**

`road_max_grade` is a DIMENSIONLESS gradient (rise over run) — not degrees, not a percentage. Config bounds are (0.01, 1.0): terrain_world.py declares `'road_max_grade':(.01,1)`. It is not a fixed global default; it comes from the civilization profile's `road_grade_limit` (terrain_recipes.py maps `road_max_grade=p['road_grade_limit']`), and the shipped civilizations.json values run 0.35 / 0.40 / 0.45 / 0.55 across profiles. The nomad walking ceiling is therefore `road_max_grade * GRADE_RELIEF(2.2)` ~= 0.77 to 1.21 rise/run (roughly 38-50 degrees), and an edge steeper than that returns None from `cost` and is impassable. The comparison is against `grade = abs(height[j] - height[i]) / d`, where d is the great-circle distance in METRES between the two nodes, so both sides are dimensionless.

### Missing — important

**`seeks` and `carries` arrays for five of the six classifications (only merchants was quoted verbatim). Every band record writes both fields to output.**

From nomads.json, verbatim: survivors seeks ["shelter","kin","food"], carries ["what they could lift"]; deserters seeks ["cover","plunder","a way out"], carries ["army issue","stolen supply"]; cultists seeks ["the god","sacred ground","converts"], carries ["relics","offerings"]; merchants seeks ["markets","escort","a safe night"], carries ["goods","coin","news"]; bandits seeks ["plunder","ransom","weak escorts"], carries ["stolen goods"]; wanderers seeks ["pasture","water","a sheltered winter"], carries ["herds","tents","craft"]. The band copies them as `list(rule['seeks'])` / `list(rule['carries'])`; a fission child copies the same arrays from its inherited classification.

**The three `method` / `limits` prose strings, elided as "<long prose string>". `method` and `limits` are REQUIRED properties of the nomads block and of the routes sub-block in Contracts/schemas/nomads.schema.json, so the port must emit them or fail validation.**

nomads.method: 'A thinned point process over habitable land proposes start points, and the ground around each one decides the band. Six classifications are gated on hard geographic preconditions -- a ruin destroyed this age with a survivor settlement to flee to, a decided war with cover nearby, sacred ground with a god bound to its school, a road between two markets, rough country within strike of wealth and clear of authority, or forage with a seasonal swing -- and only eligible classifications enter a single weighted draw. A candidate that satisfies no gate produces no band. Every reach is a multiple of settlement spacing; speed is absolute metres per day because a walking pace belongs to the traveller rather than the world.' nomads.limits: 'Placement and classification only. Routes, seasonal camps, day windows and branch topology are a separate pass, so a band here carries the camp it starts from and nothing it walks to. Sizes are drawn from authored bands rather than from local carrying capacity, and no band consumes forage, raids, trades or dies. Nothing here writes back into the world yet.' The routes and effects pairs are of comparable length; their substance is recoverable from this spec's own ALGORITHM and 'WHAT THE PASSES EXPLICITLY DO NOT ESTABLISH' sections, but the literal strings are gone and must be re-authored.

**`settled_directions` resolves a record's direction by a DIFFERENT, two-arm chain from `_where`, and raises rather than skips on a record carrying neither.**

`settled_directions(result, cfg)` is `[s.get('direction') or direction(s['x'], s['z'], n) for s in settlements.sites + fisheries.ports + humans.hamlets]`. There is NO node arm, unlike `_where`, which falls back to `points[node]`. A port that reuses `_where` here resolves records the reference would have crashed on; a record with neither `direction` nor `(x,z)` raises KeyError in the reference. `ground()` then wraps it as `[(d, str(i), d) for i, d in enumerate(settled_directions(...)) if d]`, so the enumeration index `i` — which is the lexicographic tie-break key for the clearance `_nearest` — is taken over the UNFILTERED list, before falsy directions are dropped.

### Missing — minor

**`ground()`'s roads list silently drops out-of-range node indices, and the dropped node is not memoised as seen.**

The loop is `for node in route.get('nodes', []): if node in seen or not 0 <= node < len(points): continue; seen.add(node); roads.append((direction(points[node][0], points[node][1], n), str(node), node))`. The bounds check shares the `continue` with the dedupe, so an out-of-range node is NOT added to `seen` — it is simply skipped every time it appears. Only in-range nodes are deduped.

**`travel_cost`'s diagonal-corner check uses a (x,z) -> node index map that the spec's pseudocode references as `lookup` but never defines.**

`lookup = {p: i for i, p in enumerate(points)}`, built once per `travel_cost` call over the sphere_grid point list and keyed on the `(x, z)` tuple. The check reads `water[lookup[(xi, zj)]]` and `water[lookup[(xj, zi)]]`. Both keys are guaranteed present because the guard `zi not in (0, n-1) and zj not in (0, n-1)` restricts to non-pole rows, and every x in 0..n-2 exists on every non-pole row. In FWGC++ this is an index array of size (n-1) x n, or a lookup against SphereGrid::point_x / point_z.

**`trace(parent, start, end)` returns a ONE-element list when end == start, not an empty one.**

`path=[end]; while path[-1]!=start: j=parent[path[-1]]; if j<0: return []; path.append(j); return list(reversed(path))`. With end == start the loop body never runs and it returns `[start]`. This is why the fission join guards with `if len(joined) < 2` rather than `if not joined`: a child seated on its parent's own node would produce a one-node join, not an empty one. (`_inheritable` excludes the parent's start camp so it does not arise in practice, but the guard is written for it, and a port that returns [] for end==start changes the guard's meaning.)

**nomads.json's top-level `notes` string, which records the tuning intent that governs every threshold in the file.**

'Balance table for terrain_nomads. Every reach is a multiple of settlement spacing, never a metre constant, so a wider world does not turn a local band into a regional one. Speeds are absolute metres per day because a walking pace is a property of the traveller, not of the world. Gates are hard: a classification whose preconditions fail is ineligible outright, and a candidate that satisfies none produces no band at all. Thresholds are tuned so each hard gate admits a minority of land: most ground raises nobody, and a cult needs genuinely charged sacred ground rather than any ley whisper. Version 2 adds the `fission` section; nothing in `candidate` or `classifications` moved with it.'
