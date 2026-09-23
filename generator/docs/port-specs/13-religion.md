# Port specification: 13-religion

Extracted from the Python `FantasyWorldGenerator` at commit `b67c99b` before that
repository was retired, then audited against the same source by a second reader.

**Source modules**

- `Sim/icarus_sim/terrain_religion.py`

**Estimated C++ size:** roughly 1450 lines, excluding tests.

## Audit: NOT YET IMPLEMENTABLE

This is a very strong spec — the algorithm sections for world_facts, rule_holds, aspect_of, resolve_gods, _draw, choose_cosmology, mean_potency, choose_faiths, feasts, resolve_sites, hidden_records and catalogue_identity are faithful line-for-line, every module constant (VERSION 1, PRIMORDIAL, SURGE_BIRTH 1.3) and every catalogue constant (aspect_midpoint 1.2, nest_aspect_push 0.15, affinity_weights, fear_gods, wild_push_families, fold_free_cosmologies) is present with its meaning, all 11 cosmologies and all 12 affinity tables are reproduced exactly as the JSON has them, hidden_pantheon.json is reproduced in full including the notes rationale, and every WHY comment in the file is carried verbatim. I could not find a defect in the control flow. I am nonetheless setting implementable=false for three reasons. (1) The catalogue content is incomplete in a way that cannot be recovered once the repo is deleted: the spec prints sovereign/wild aspect names only for the 8 school gods and god_turning_moon, and omits the `aspects.sovereign` string for the other 16 gods entirely — its tables just say "sovereign aspect only". `aspect_name` is an exported field on every manifest god record and is the second link in the faith display-name chain, so an implementer would emit null (or invent a name) for 16 of 25 gods and silently produce a different export. The omitted content happens to be trivial (sovereign == the god's `name` for all 16), but nothing in the spec says so. (2) The spec names the wrong file as the pipeline registry: `terrain_profile.py` is an opt-in wall-clock profiler whose STEP_TARGETS tuple is explicitly annotated "Order here is documentation only; wrapping is by name". The real stage-13 dispatch is in terrain_history.py, and it passes `cfg`, which decides which of the two `lunar_influence` sources is live in production. (3) The age-transition re-run is guarded — `rebuild_tail` only calls add_religion when `'astrology' in result`, and takes a `refresh_astrology` branch otherwise — which the spec presents as unconditional. Fix those three and the document stands on its own.

**1 blocking gap(s)** were found and are recorded in the addenda below.

## Purpose

`terrain_religion.py` (stage 13, "13-religion") gives the world a pantheon: 25 authored gods whose existence is decided independently by rules evaluated against already-exported world state, one cosmology drawn by seed from the templates that fit, one faith per civilization that has a standing city, and a handful of cult/shrine sites attached to ruins. It also exports four *hidden* gods from a separate file, always as `absent`, so that the corruption API (`terrain_corruption.py`) has somewhere to write when one of them wakes.

The subsystem is deliberately inert: the module docstring says "A god is manifest, sleeping or absent; it changes nothing in the world unless the orchestrator summons it through terrain_visitation." Nothing in religion touches terrain, layers, food, threat or influence. Its unit test `test_religion_never_touches_the_world` deep-copies the world, runs `add_religion`, pops `result['religion']` and asserts equality — that is the contract: **`add_religion` writes exactly one key, `result['religion']`, and mutates nothing else.**

It exists so that the generated world carries narrative provenance that is *derivable* rather than invented: which gods a world could plausibly have is a function of what the world actually contains (which leyline networks have key points, whether there is an ocean, whether dragons nest, whether anyone was exiled), so a reader or a game host can justify every deity from the world data.

## Where it sits in the pipeline

**Registered as a pipeline stage** in `terrain_profile.py` under the comment "Stage 13: creatures, threat, religion", third of three:

```
('icarus_sim.terrain_nests',    'add_nests'),
('icarus_sim.terrain_history',  'add_threat_assessments'),
('icarus_sim.terrain_religion', 'add_religion'),
```

**Must already exist before it runs** (all optional in the sense that the code `.get()`s every one of them with a default, but the result is degenerate if they are missing):

- `world['config']['seed']` — the master seed (int). Falls back to the `seed=` argument, then to `0`.
- `world['magic']` — `{'enabled': bool, 'networks': {school: {'nodes': [...], 'edges': [...], 'instability': float, ...}}}` from `terrain_leyline_history.generate_networks` / `evaluate_networks` (stage 9).
- `world['layers']['ley_<school>']` — 8 potency grids, stage 9.
- `world['settlements']['sites']` and `world['settlements']['founding']` — stages 10–12.
- `world['ruins']`, `world['history']['ages']` — written by age transitions (stages 14–15) and by `terrain_history` generally; empty on a first stage-13 pass.
- `world['water']` — stage 5.
- `world['beast_nests']['sites']` — stage 13, the `add_nests` immediately before it.
- `world['sky']['settlements']` — stage 9.
- `world['astrology']` / `world['lunar_almanac']` — stage 9 (`terrain_astrology`). Supplies `months[].nod_degrees` and `events[]`.
- `world['effective_config']['globe_radius']` — metres, design space.
- `world['recipe']['resolved']['lunar_influence']` — only used when `cfg is None`.

**Re-run points.** The module is idempotent-with-memory: every call reads `result['religion']` as `previous` and carries state forward. It is re-resolved:

1. at stage 13 of generation;
2. at the end of every age transition — `terrain_history.rebuild_tail(..., religion=True)` calls `add_religion(result, cfg)`;
3. after every corruption — `terrain_corruption.corrupt()` appends the corruption record, calls `add_religion`, then calls `_reveal()`;
4. after every visitation — `terrain_visitation` sets a god `walking`, appends the theophany site and the conversion, then calls `add_religion`. Note `rebuild_tail` is called from a visitation with `religion=False` precisely because "a visitation resolves religion itself once its own records are written".

`docs/pantheon.md` states: "Religion is resolved after beast nests (stage 13), at the end of every age transition, after every age-API step, and after every visitation. It reads only final-stage inputs, so it is never stage-dependent."

**What depends on it afterwards:**

- `terrain_visitation.py` — gates on `religion.version == 1` and `religion.catalogue == catalogue_identity()`; requires a god record with status `manifest` or `sleeping` to summon; writes `status: 'walking'`, `avatar`, `faiths[civ]['conversion']`, and `sites[]` entries of kind `theophany` / `pilgrimage`.
- `terrain_corruption.py` — same two gates plus a `NODE_BUDGET` check over `religion['corruptions']`; reads `hidden_catalogue()`, `hidden_gods_by_id()`, `rule_holds`, `gods_by_id`.
- `terrain_history.py` age gate — refuses a world whose `religion.version != 1` ("This world predates the pantheon. Regenerate...") or whose `religion.catalogue` differs from the current `catalogue_identity()`.
- `terrain_nomads.py` — `_god_for_school` scans `religion['gods']` for a god whose `schools` contains a given school (**deliberately not `gods_by_id()`**, because that reads `pantheon.json` only and so cannot find the hidden four; the comment says "Hidden gods are the entries whose family is 'outside'"). `ground()` also collects `religion['sites']` as "shrines" and uses `site['node']`, `site['id']`, `site['god_id']` to seat nomad cult bands, reading `record['schools'][0]` of the named god.
- `terrain_villains.school_god(school)` — scans `catalogue()['gods']` for `family == 'school'` with that school in `school_affinity`.
- `world_debug`, exporters, the Unreal plugin (receives the JSON but "does not yet act on it").

## Inputs

All reads go through `.get()` with defaults; none raise on a missing block.

**`world_facts(world, cfg)` inputs — this is the complete set of world state any existence rule can see:**

| Source path | Type / units | Used for |
|---|---|---|
| `world['magic']['enabled']` | bool; default `any(schools.values())` | `facts['magic_enabled']` |
| `world['magic']['networks'][s]['nodes']` | list; only emptiness is read | `facts['schools'][s] = bool(nodes)` — **nodes only, edges do not count** |
| `world['magic']['networks'][s]['edges']` | list of `{intensity: float, ...}` | `aspect_of` only |
| `world['magic']['networks'][s]['instability']` | float 0..1, default 0.0 | `aspect_of` only |
| `world['settlements']['sites'][].population_profile` | civilization id string | `facts['civilizations_with_cities']`, `facts['races']` |
| `world['settlements']['sites'][].diaspora` | truthy flag | `facts['diaspora']` |
| `world['settlements']['founding']['used_civilizations']` | list of civ ids | `facts['races']` (history, not just standing cities) |
| `world['settlements']['founding']['events'][].status` | `'diaspora_founded'` among others | `facts['diaspora']` |
| `world['ruins'][].population_profile` | civ id | `facts['races']` — **only counted if it is a known entity civilization id** |
| `world['ruins']` | list | `facts['ruins'] = len(ruins)` |
| `world['water']['ocean_km2' / 'lake_km2' / 'dry_km2']` | km², default 0.0 | `facts['ocean_share'] = ocean/(ocean+lake+dry)`, 0.0 if total is 0 |
| `world['sky']['settlements']` | list | `facts['sky'] = len(...)` (an int, but only `> 0` is ever tested) |
| `world['beast_nests']['sites'][].real` | bool, default `True` | a nest is "fantasy" iff `real` is explicitly falsey |
| `world['beast_nests']['sites'][].family` | `'draconic'`, `'infernal'`, `'undead'`, `'aberrant'`, … | `facts['families']` (sorted, truthy only), `facts['dragons']` |
| `world['beast_nests']['sites'][].name` | string | `facts['dragons']` — counted if `'dragon' in name.lower()` |
| `world['history']['ages'][].wars` | list | `facts['wars'] = sum(len(...))` |
| `options(cfg)['lunar_influence']` when `cfg is not None`, else `world['recipe']['resolved']['lunar_influence']` (default 0.0) | float, roughly 0..1 | `facts['lunar_influence']` |

**Additional reads outside `world_facts`:**

- `world['magic']['networks'][school]['nodes'] + ['edges']`, field `intensity` (float, ~0.35–4.0) — `aspect_of` for school gods.
- `world['lunar_almanac']['months'][].nod_degrees` (degrees, ±`tilt_max_degrees`) — `aspect_of` for `god_turning_moon`.
- `world['lunar_almanac']['events'][]` — records `{day:int, kind:str, school?:str, tide?:float}`; kinds used: `'surge'` (carries `school`), `'full_moon'`, `'hollow_night'`. Days are day-of-year 0..359.
- `world['layers']['ley_<school>'][z][x]` — **indexed row-major by `[z][x]`**, potency (dimensionless, roughly 0..4). Read at each city's `z`,`x` by `mean_potency`.
- `world['settlements']['sites'][].{uid, x, z, direction}` — `uid` is a string key, `direction` a unit 3-vector.
- `world['effective_config']['globe_radius']` — metres, **default 1.0 if absent** (degenerate; see edge cases).
- `world['beast_nests']['sites'][].{id, layer, tier, range_m, spacing_m, direction}` — via `fantasy_nest_threats`.
- `world['ruins'][].{id, node, cause, destroyed_age}` — `resolve_sites`.
- `world['history']['ages'][].{age, moon.tide.weave}` — `resolve_sites`; `moon.tide` is the per-school lunar tide multiplier on the day the age turned (range 0.5–1.8).
- `world['religion']` from a previous run — see the "previous" contract in the algorithm.
- `world['config']['seed']` — int.

**External helpers it calls** (reproduce these; they live in other modules):

- `child_seed(master, domain, variation=0)` (from `terrain_tectonics`):
  `int.from_bytes(sha256(f'tectonics-v1:{master}:{domain}:{variation}'.encode()).digest()[:4], 'big')` — a 32-bit seed.
- `KNOWN_SCHOOLS` (from `terrain_leyline_history`) — an **ordered** dict of exactly eight: `weave, umbral, infernal, radiant, fire, water, earth, air`. The comment there says "the places that must stay eight forever say KNOWN_SCHOOLS rather than SCHOOLS". The four `HIDDEN_SCHOOLS` are `blood, void, rot, eldritch`; religion never iterates them.
- `civilization_ids()` (from `terrain_profiles`) — the twelve entity civilizations, in insertion order: `human_maritime, human_desert, human_cold, human_large_island, human_rainforest, human_heartland, dwarf, elf, gnome, tidekin, hill_dwarf, frosthold_dwarf`.
- `entity_rules(civ)['parent_race_id']` (from `civilization_registry`) — the map is:
  `human_maritime/human_desert/human_cold/human_large_island/human_rainforest/human_heartland → human`; `elf, tidekin → elf`; `dwarf, gnome, hill_dwarf, frosthold_dwarf → dwarf`.
  Note `gnome`, `hill_dwarf` and `frosthold_dwarf` are all **dwarf**-parented, and `tidekin` is **elf**-parented; there are only three parent races.
- `fantasy_nest_threats(city, nests, radius)` (from `terrain_history`) — reproduced in full in the algorithm section.

## Outputs

**`add_religion` writes exactly one key: `result['religion']`.** Everything else in the world is untouched (asserted by test).

```
religion = {
  'version':   1,                       # int, the module's VERSION
  'catalogue': {'schema_version': 1, 'revision': 1, 'sha256': '<64 hex>'},
  'facts':     {...},                   # see below
  'cosmology': {'id', 'name', 'high': [god_id], 'arrangement': str,
                'eligible': [{'id': cosmology_id, 'weight': float}]},
  'gods':      [ <25 known records> , <4 hidden records> ],
  'faiths':    { civilization_id: {...} },
  'sites':     [ {...} ],               # sorted by 'id'
  'visitations': [...],                 # carried forward verbatim (deep copy)
  'corruptions': [...],                 # carried forward verbatim (deep copy)
  'method':    '<fixed English paragraph>',
  'limits':    '<fixed English paragraph>',
}
```

**`facts`** (all of these are exported, not just used internally):

| field | type | range |
|---|---|---|
| `schools` | dict of 8 bools, key order = KNOWN_SCHOOLS order | — |
| `primordial_count` | int | 0..4 (`fire, water, earth, air`) |
| `races` | sorted list of strings | subset of `dwarf, elf, human` |
| `civilizations_with_cities` | sorted list of civ ids | ≤ 12 |
| `ocean_share` | float | 0.0..1.0 |
| `sky` | int | count of sky settlements |
| `families` | sorted list of strings | fantasy nest families present |
| `dragons` | int | count of dragon-ish fantasy nests |
| `ruins` | int | — |
| `diaspora` | int | — |
| `wars` | int | **exported only; no rule kind and no cosmology weight reads it** |
| `magic_enabled` | bool | — |
| `lunar_influence` | float | ~0.0..1.0 |

**A god record** (known gods; 25 of them, in catalogue file order):

| field | type | notes |
|---|---|---|
| `id` | string | **the key field for this block is `id`** (not `uid`, not `name`) |
| `name` | string | authored English |
| `family` | `'school' \| 'ancestor' \| 'feature' \| 'civic'` | (hidden gods use `'outside'`) |
| `status` | `'absent' \| 'manifest' \| 'sleeping' \| 'walking'` | |
| `aspect` | `'sovereign' \| 'wild' \| null` | null iff `status == 'absent'` |
| `aspect_name` | string or null | `aspects[aspect]`, authored English |
| `domains` | list of strings | copied from the catalogue |
| `schools` | list of strings | **this is `school_affinity`, not the existence school**; may contain the pseudo-school `'moon'` |
| `rule` | object | the raw `exists` expression, **shared by reference** with the catalogue in Python |
| `evidence` | object | `{}` when absent; aspect evidence when manifest/walking; `{'last_seen': <prior evidence>}` when sleeping |
| `saint_of` | god id or null | written in `add_religion` after the cosmology is chosen |
| `avatar` | object, **optional** | present only if the prior record was `walking` and carried one, deep-copied forward |
| `roused_by` | list, **optional** | written by `terrain_corruption._reveal`; **dropped on the next `add_religion`** |

**A hidden god record** (4 of them, always appended after the 25):

`{'id', 'name', 'family': 'outside', 'status', 'aspect', 'aspect_name', 'domains', 'schools': [<one hidden school>], 'rule', 'evidence', 'saint_of': None}`

`status` defaults to `'absent'` and is carried from the previous block; `aspect`, `aspect_name` and `evidence` default to `None`/`None`/`None` and are carried. `saint_of` is **always** `None`. `schools` is a one-element list holding the god's hidden school (`blood`/`rot`/`eldritch`/`void`) — this is why `terrain_nomads._god_for_school` can find a blood cult's patron.

**A faith record**, keyed by civilization id (dict built in `sorted` civilization order):

```
{ 'parent_race_id': 'human'|'elf'|'dwarf',
  'patron':  god_id or null,
  'gods':    [god_id],            # 0..4, patron first
  'names':   {god_id: string},    # display name per worshipped god
  'conversion': {'god_id':…, 'visitation': int} or null,
  'fear':    [{'god_id', 'city_uid', 'nest_id', 'mode': 'fear'}],   # sorted
  'sects':   [{'node', 'founding_year', 'patron', 'rival', 'epithet'}],
  'feasts':  {'surges': {god_id: [day…]},
              'full_moons': [day…],
              'hollow_nights': {'days': [day…], 'kept_as': 'vigil'|'dread'}},
  'weights': {god_id: float}      # rounded to 6 dp
}
```

**A site record** (list sorted by `id`; **the key field here is `id`**):

- `{'id': 'cult-<ruin id>', 'kind': 'cult', 'god_id': 'god_weave', 'ruin_id', 'node', 'born_under_surge': bool, 'weave_tide': float or null}`
- `{'id': 'shrine-<ruin id>', 'kind': 'shrine', 'god_id': 'god_red_field', 'ruin_id', 'node'}`
- carried forward from `previous`: any site whose `kind` is `'theophany'` or `'pilgrimage'` (written by `terrain_visitation`, shape `{'id': 'theophany-<index>-<god id>', 'kind', 'god_id', 'node', 'visitation'}`).

`node` is a terrain node index (int), which is the *stable* key across age boundaries.

**Module-level helpers that are part of the public surface** (other modules import them): `catalogue()`, `hidden_catalogue()`, `hidden_gods_by_id()`, `gods_by_id()`, `catalogue_identity()`, `lint_catalogue()`, `rule_holds()`, `world_facts()`, `resolve_gods()`, `choose_cosmology()`, `add_religion()`, `VERSION`.

## Algorithm

## 0. `add_religion(result, cfg=None, seed=None)` — the driver

```
previous = result.get('religion')                     # may be None
if seed is None and 'config' in result: seed = result['config']['seed']
elif seed is None:                      seed = 0
facts     = world_facts(result, cfg)
gods      = resolve_gods(result, facts, previous)     # 25 known gods
manifest  = frozenset(g.id for g in gods if g.status in ('manifest','walking'))
cosmology = choose_cosmology(facts, manifest, seed)
fold_free = cosmology.id in doc.fold_free_cosmologies  # {'cos_mosaic','cos_ancestor_halls'}
for god in gods:
    target = catalogue[god.id].folds_into            # absent for 19 of 25
    god.saint_of = target if (target and target in manifest
                              and not fold_free
                              and god.status != 'absent') else None
conversions = { civ: f.conversion
                for civ, f in (previous or {}).get('faiths', {}).items()
                if f.get('conversion') }
faiths = choose_faiths(result, facts, gods, cosmology, seed, conversions)
result['religion'] = { version:1, catalogue:catalogue_identity(), facts, cosmology,
                       gods: gods + hidden_records(previous),
                       faiths, sites: resolve_sites(result, gods, previous),
                       visitations: deepcopy(previous.visitations or []),
                       corruptions: deepcopy(previous.corruptions or []),
                       method:…, limits:… }
return result
```

**Order is load-bearing:** `manifest` is computed *before* the hidden records are appended, so a hidden god can never satisfy a `gods` rule, be a cosmology high god, or be worshipped. `saint_of` is computed *after* the cosmology, because folding is cosmology-dependent. `conversions` is read from `previous` *before* `faiths` overwrites it.

---

## 1. `world_facts(world, cfg)`

```
magic    = world.get('magic', {})
networks = magic.get('networks', {})
schools  = { s: bool(networks.get(s, {}).get('nodes')) for s in KNOWN_SCHOOLS }   # 8 entries, in KNOWN_SCHOOLS order

settlements = world.get('settlements', {})
sites       = settlements.get('sites', [])
founding    = settlements.get('founding', {}) or {}
ruins       = world.get('ruins', [])
ids         = set(civilization_ids())                                # the 12

ever  = set(founding.get('used_civilizations', []))
      | { s['population_profile'] for s in sites }
      | { r['population_profile'] for r in ruins if r.get('population_profile') in ids }
races = sorted({ entity_rules(c)['parent_race_id'] for c in ever if c in ids })

water = world.get('water', {})
ocean = water.get('ocean_km2', 0.0)
total = ocean + water.get('lake_km2', 0.0) + water.get('dry_km2', 0.0)

fantasy  = [ n for n in world.get('beast_nests', {}).get('sites', []) if not n.get('real', True) ]
families = sorted({ n.get('family') for n in fantasy if n.get('family') })
dragons  = count of n in fantasy with n.get('family')=='draconic' or 'dragon' in n.get('name','').lower()

diaspora = count of s in sites with truthy s.get('diaspora')
         + count of e in founding.get('events', []) with e.get('status')=='diaspora_founded'

wars = sum(len(age.get('wars', [])) for age in world.get('history', {}).get('ages', []))

lunar = options(cfg)['lunar_influence']  if cfg is not None
      else world.get('recipe',{}).get('resolved',{}).get('lunar_influence', 0.0)

return { schools, primordial_count: schools['fire']+schools['water']+schools['earth']+schools['air'],
         races, civilizations_with_cities: sorted({ s['population_profile'] for s in sites }),
         ocean_share: ocean/total if total else 0.0,
         sky: len(world.get('sky',{}).get('settlements',[])),
         families, dragons, ruins: len(ruins), diaspora, wars,
         magic_enabled: bool(magic.get('enabled', any(schools.values()))),
         lunar_influence: lunar }
```

Note the asymmetry: `used_civilizations` and standing-site profiles are added to `ever` **unfiltered**, ruins' profiles are filtered by `in ids` at insertion, and the whole set is filtered by `in ids` again when computing `races`. The double filter is harmless; reproduce the final `if c in ids` guard and the result is identical.

`magic_enabled`'s default matters: a world with no `magic.enabled` key is magic-enabled iff at least one school has nodes.

---

## 2. `rule_holds(rule, facts, manifest=∅)` — the existence-rule interpreter

A rule is a small tagged record. `RULE_KINDS` is exactly:
`always, magic_enabled, magic_disabled, school, race, civilization_city, ocean_share, sky, dragon, nest_family, ruins, diaspora, any, all, primordial_count, gods, races`.

```
always            -> true
magic_enabled     -> facts.magic_enabled
magic_disabled    -> not facts.magic_enabled
school            -> facts.schools[rule.school]           # KeyError on an unknown school
race              -> rule.race in facts.races
races             -> len(facts.races) >= rule.min
civilization_city -> any(c in facts.civilizations_with_cities for c in rule.ids)
ocean_share       -> facts.ocean_share >= rule.min
sky               -> facts.sky > 0
dragon            -> facts.dragons > 0
nest_family       -> any(f in facts.families for f in rule.families)
ruins             -> facts.ruins >= rule.min
diaspora          -> facts.diaspora > 0
primordial_count  -> facts.primordial_count >= rule.min
gods              -> all(i in manifest for i in rule.ids)   if rule.get('all', True)
                     else any(i in manifest for i in rule.ids)
any               -> any(rule_holds(r, facts, manifest) for r in rule.rules)
all               -> all(rule_holds(r, facts, manifest) for r in rule.rules)
otherwise         -> raise ValueError('Unknown existence rule ' + kind)
```

`manifest` defaults to the empty set, so **`resolve_gods` calls `rule_holds` without a manifest set** — meaning a `gods` rule can never hold inside `resolve_gods` (no god in `pantheon.json` uses a `gods` rule; only the `cos_dual_dawn` and `cos_sea_and_sky` cosmologies do, and those are evaluated in `choose_cosmology`, which does pass `manifest`).

---

## 3. `resolve_gods(world, facts, previous)` — the status ladder

Iterate `catalogue()['gods']` **in file order** and build one record each.

```
before = { g.id: g for g in (previous or {}).get('gods', []) }     # includes the 4 hidden ones
for god in catalogue.gods:                                         # known 25 only
    holds = rule_holds(god.exists, facts)                          # note: no manifest set
    prior = before.get(god.id)
    if   prior and prior['status'] == 'walking':                    status = 'walking'
    elif holds:                                                     status = 'manifest'
    elif prior and prior['status'] in ('manifest','sleeping','walking'):
                                                                    status = 'sleeping'
    else:                                                           status = 'absent'

    if status in ('manifest','walking'):  aspect, evidence = aspect_of(god, world, facts)
    elif status == 'sleeping':            aspect, evidence = prior['aspect'], {'last_seen': prior.get('evidence', {})}
    else:                                 aspect, evidence = None, {}

    record = { id, name, family,
               status, aspect,
               aspect_name: god.aspects.get(aspect) if aspect else None,
               domains: list(god.domains),
               schools: list(god.school_affinity),
               rule: god.exists,
               evidence }
    if prior and prior['status']=='walking' and prior.get('avatar'):
        record['avatar'] = deepcopy(prior['avatar'])
```

The ladder is monotone in an important way: **a god never returns to `absent` once it has existed.** Sleeping keeps the last aspect (`test_a_god_that_stops_qualifying_sleeps_with_its_last_aspect`). `walking` is sticky and only `terrain_visitation.depart_god` or `terrain_corruption.cleanse` can end it.

Note that `saint_of` is *not* set here; `add_religion` adds it afterwards. A record straight out of `resolve_gods` has no `saint_of` key.

---

## 4. `aspect_of(god, world, facts)` — Sovereign or Wild

Docstring: *"Sovereign below the intensity/instability midpoint, Wild above; the moon follows its lean."*

```
if 'wild' not in god.aspects:                       # true for 16 of the 25
    return 'sovereign', {}

if god.family == 'school':
    net   = world['magic']['networks'].get(god.exists.school, {})
    items = net.get('nodes', []) + net.get('edges', [])          # concatenation order matters only for the sum
    mean  = sum(i.intensity for i in items) / len(items)  if items else 0.0
    score = mean * (1 + net.get('instability', 0.0))
    push  = 0.15 if any(f in facts.families for f in wild_push_families.get(god.id, [])) else 0.0
    evidence = {'mean_intensity': mean, 'instability': net.instability,
                'nest_push': push, 'midpoint': 1.2}
    return ('wild' if score + push > 1.2 else 'sovereign'), evidence

if god.id == 'god_turning_moon':
    months = world.get('lunar_almanac', {}).get('months', [])     # 12 entries
    lean   = sum(m.nod_degrees for m in months) / len(months) if months else 0.0
    return ('sovereign' if lean >= 0 else 'wild'),
           {'mean_nod_degrees': lean, 'leaning': 'still' if lean >= 0 else 'restless'}

return 'sovereign', {}
```

`wild_push_families` is `{'god_umbral': ['undead'], 'god_infernal': ['infernal']}` — undead nests push the Umbral toward Wild, infernal nests push the Infernal. The comparison is **strictly greater than** 1.2. With no items, `mean = 0`, `score = 0`, so a school god with only edges and no nodes can still be manifest? No — `facts.schools[s]` requires nodes, so a school god that is manifest always has at least one node. But a **sleeping-then-walking** school god (via corruption/visitation) could hit the empty-items branch.

Worked example from the test suite: weave network nodes `[1.4, 1.5]`, instability `0.2` → mean 1.45, score `1.45 × 1.2 = 1.74 > 1.2` → **wild**. Water `[0.5, 0.6]`, instability 0.2 → mean 0.55, score 0.66 → **sovereign**.

---

## 5. `_draw(rng, weighted)` — the weighted pick

```
total  = sum(w for _, w in weighted)
target = rng.random() * total                # exactly one uniform draw, always
for item, weight in weighted:
    target -= weight
    if target <= 0: return item
return weighted[-1][0]                       # float-drift fallback
```

Docstring: *"One seeded pick from (item, weight) pairs with positive weights, in list order."* Every caller filters `weight > 0` before calling, so the linear walk terminates on the first item whose cumulative weight reaches `target`. `_draw` **always consumes exactly one `random()`**, even on a single-element list — this matters for stream position.

---

## 6. `choose_cosmology(facts, manifest, seed)`

```
eligible = []
for cosmology in doc.cosmologies:                       # file order
    if not rule_holds(cosmology.requires, facts, manifest): continue
    spec   = cosmology.weight
    value  = facts.get(spec.get('fact', ''), 0.0)       # missing key -> 0.0
    if value is a list/tuple/set: value = len(value)    # 'races' is a list
    weight = spec.get('base', 0.0) + spec.get('per', 0.0) * value
    if weight > 0: eligible.append((cosmology, weight))

if not eligible:
    eligible = [(the cosmology with id 'cos_mosaic', 1.0)]     # the guaranteed fallback

rng    = Random(child_seed(seed, 'pantheon-v1'))
chosen = _draw(rng, eligible)

return { 'id': chosen.id, 'name': chosen.name,
         'high': [g for g in chosen.get('high', []) if g in manifest],   # filtered to manifest
         'arrangement': chosen.arrangement,
         'eligible': [{'id': c.id, 'weight': w} for c, w in eligible] }
```

`cos_mosaic` requires `always` with `base 0.5`, so in practice `eligible` is never empty and the fallback branch is dead — but keep it: a catalogue edit that removes `cos_mosaic`'s `always` rule would otherwise crash. Note the fallback uses `next(c for c in doc.cosmologies if c.id=='cos_mosaic')`, which raises `StopIteration` if the id is gone.

`high` is filtered against `manifest`, so a cosmology can be chosen and export an empty `high` list (`cos_mosaic` and `cos_spirits_of_place` have `high: []` by construction).

The `titans` and `outsiders` keys on `cos_elemental_court` are **read by nothing**; they are catalogue flavour that never reaches the export.

---

## 7. `mean_potency(world, cities)`

```
for school in KNOWN_SCHOOLS:                      # all 8, in order
    grid   = world['layers'].get('ley_' + school)
    values = [grid[c['z']][c['x']] for c in cities] if grid else []
    result[school] = sum(values)/len(values) if values else 0.0
```

Plain arithmetic mean of the potency at each of the civilization's city cells. `[z][x]` — row index first. Missing layer or no cities → 0.0. The result always has all 8 keys.

---

## 8. `choose_faiths(world, facts, gods, cosmology, seed, conversions)`

One faith per entry of `facts['civilizations_with_cities']`, **in that list's sorted order**, which fixes the insertion order of the `faiths` dict.

```
weights  = doc.affinity_weights      # strong 2, mild 1, aversion -2, high 1.5, civic_base 0.5
by_id    = { g.id: g for g in gods } # resolved records, known 25 only
manifest = [ g for g in gods if g.status in ('manifest','walking') ]   # list, catalogue order
sites    = world.settlements.sites
nests    = world.beast_nests.sites
radius   = world.effective_config.get('globe_radius', 1.0)
founding = world.settlements.founding or {}
almanac  = world.lunar_almanac or {}

for civilization in facts.civilizations_with_cities:
    cities  = [ s for s in sites if s.population_profile == civilization ]
    race    = entity_rules(civilization).parent_race_id
    potency = mean_potency(world, cities)
    table   = doc.affinities.get(civilization, {})

    # (a) the ancestor god of this people's parent race, if it is live
    ancestor = first g in manifest with g.family=='ancestor'
                 and catalogue[g.id].get('race') == race          # else None

    rng = Random(child_seed(seed, 'faith-v1-' + civilization))

    # (b) affinity scoring over every live god except the ancestor
    weighted = []
    for god in manifest:
        if ancestor and god.id == ancestor.id: continue
        w = 0.0
        if god.id in table.get('strong',   []): w += 2.0
        if god.id in table.get('mild',     []): w += 1.0
        if god.id in table.get('aversion', []): w -= 2.0
        if god.id in cosmology.high:            w += 1.5
        if god.family == 'civic':               w += 0.5
        w += sum(potency[s] for s in god.schools if s in potency)   # 'moon' is not in potency
        if w > 0: weighted.append((god.id, w))

    # (c) patron
    patron = ancestor.id if ancestor else (_draw(rng, weighted) if weighted else None)

    # (d) a conversion from a past visitation overrides, while its god still exists
    conversion = conversions.get(civilization)
    if conversion and by_id.get(conversion.god_id, {}).get('status') in ('manifest','walking'):
        # comment: "A god that walked among this people is its patron until it stops existing."
        if ancestor and ancestor.id != conversion.god_id:
            weighted.append((ancestor.id, 2.0))       # the displaced ancestor rejoins the pool at 'strong'
        patron = conversion.god_id

    # (e) up to three more, drawn without replacement
    chosen = [patron] if patron else []
    pool   = [ (g, w) for g, w in weighted if g != patron ]
    while pool and len(chosen) < 4:
        pick = _draw(rng, pool)
        chosen.append(pick)
        pool = [ (g, w) for g, w in pool if g != pick ]
```

The three loop-body checks in (b) are **cumulative, not exclusive** — a god listed in both `strong` and `aversion` would score 0 and be dropped; no shipped table does this, but the code adds rather than selects.

The `sum(potency[s] for s in god.schools ...)` term sums the *mean potency of every school the god has affinity for*. `god_outer_unnamed` has two (`weave, umbral`), `god_red_field` has two (`fire, infernal`), `god_turning_moon` has one (`moon`) which is never in `potency` and therefore contributes **zero** — the moon god gets only affinity-table and cosmology points.

### (f) display names

```
for god_id in chosen:
    record        = catalogue[god_id]
    names[god_id] = record.get('epithets', {}).get(civilization)
                    or by_id[god_id].aspect_name
                    or record.name
```

Only the three ancestor gods carry `epithets`, so for every other god this resolves to the aspect name (e.g. The Weave in Wild aspect is named "The Laughing Unmaker"). `or` chaining means an empty-string epithet would fall through — none exist.

### (g) fear entries

```
fear = []
for city in cities:                                        # list order = settlement site order
    for threat in fantasy_nest_threats(city, nests, radius):
        god_id = doc.fear_gods.get(threat.family)
                 or (doc.fear_gods['draconic'] if threat.kind == 'dragon' else None)
        if god_id and by_id.get(god_id, {}).get('status') in ('manifest','walking') \
           and no existing f in fear with f.god_id==god_id and f.city_uid==city.get('uid'):
            fear.append({'god_id': god_id, 'city_uid': city.get('uid'),
                         'nest_id': threat.nest_id, 'mode': 'fear'})
fear = sorted(fear, key=lambda f: (f['city_uid'] or '', f['god_id']))
```

`fear_gods` is `{draconic: god_great_wyrm, infernal: god_infernal, undead: god_umbral, aberrant: god_outer_unnamed}`. **Family is consulted first**: a nest named "…dragon…" whose family is `undead` produces a fear of the Umbral, not the Wyrm. The dedupe keeps the *first* nest per (god, city) pair — i.e. the lowest nest id, because `fantasy_nest_threats` sorts by id. The final sort makes the list order independent of city iteration order, but the retained `nest_id` is not.

`fantasy_nest_threats(city, nests, radius)`, reproduced from `terrain_history.py` because religion's fear list depends on it exactly:

```
for nest in sorted(nests, key=lambda n: n['id']):                  # string sort on id
    if nest.get('layer','surface') != 'surface': continue
    if nest.get('real', True):                   continue          # real animals never frighten a god
    dragon    = 'dragon' in nest.get('name','').lower()
    dangerous = dragon or nest.get('family') in ('infernal','undead','aberrant')
    if not dangerous: continue
    cos      = clamp(dot(city.direction, nest.direction), -1.0, 1.0)
    distance = radius * acos(cos)                                  # great circle, metres
    reach    = min(radius*0.5, max(350.0, nest.get('range_m', nest.get('spacing_m', 350.0)) * 2))
    if distance > reach: continue
    yield { kind: 'dragon' if dragon else 'monster',
            weight: 0.11 * nest.get('tier', 5) * (1 - distance/reach),
            reason: nest.name + ' drove the inhabitants away.',
            nest_id: nest.id, name: nest.name, family: nest.get('family'),
            distance_m: distance, reach_m: reach }
```

Religion reads only `family`, `kind` and `nest_id` from a threat; `weight` and `reason` are for the age-fate lottery. The comment on the original notes tier scaling exists so "a greater lair at the gates is the reason a city falls and a tier one nuisance is not; tier five keeps the flat .55 the single fixed weight used to give every monster alike" (0.11 × 5 = 0.55). **`tier` here is the nest *danger* tier 1–5** — not the settlement-scale `tier`, and not `key_locations.tier`.

**`350.0` and `radius*0.5` are absolute metres in design space, pre-physical-scaling**, tuned for the reference world whose radius is `REFERENCE_RADIUS_M = 1774.4123532462844` (circumference ≈ 11.15 km). `range_m`/`spacing_m` are likewise design-space metres.

### (h) sects

```
sects = []
if patron:
    rivals = [ r for r in catalogue[patron].get('rivals', [])
               if r in by_id and by_id[r].status in ('manifest','walking') ]   # catalogue list order
    rival  = rivals[0] if rivals else (chosen[1] if len(chosen) > 1 else None)
    for event in founding.get('events', []):                                  # event list order
        if event.get('status') == 'diaspora_founded' \
           and event.get('diaspora_reason') == 'religious_schism' \
           and event.get('population_profile') == civilization and rival:
            sects.append({ 'node': event.node,
                           'founding_year': event.get('founding_year'),
                           'patron': rival,          # the schism's god
                           'rival':  patron,         # the orthodoxy it broke from
                           'epithet': (names.get(rival) or by_id[rival].name) + ' of the Schism' })
```

Note the inversion: the sect's `patron` is the parent faith's rival and its `rival` is the parent faith's patron. `names.get(rival, by_id[rival]['name'])` — if the rival is not one of the four worshipped gods it has no entry in `names`, so the plain catalogue name is used. `docs/pantheon.md` describes only the "first manifest rival" path and omits the `chosen[1]` fallback used when the patron has no live rival (e.g. `god_first_hearth`, whose `rivals` list is empty — which is exactly the case the schism test exercises).

### (i) assemble

```
faiths[civilization] = { parent_race_id: race, patron, gods: chosen, names,
                         conversion, fear: <sorted>, sects,
                         feasts: feasts(chosen, by_id, almanac),
                         weights: { g: round(w, 6) for g, w in weighted } }
```

`weights` is built from the `weighted` **list** into a dict, so if the conversion branch appended the ancestor a second time the later entry wins; in practice the ancestor is excluded from the loop so there is no duplicate. `round(w, 6)` is Python banker's-rounding on the decimal repr; any sane rounding is acceptable in a clean rewrite (numbers need not match).

---

## 9. `feasts(chosen, by_id, almanac)`

```
events = almanac.get('events', [])
surges = {}
for god_id in chosen:                                  # chosen order
    god = catalogue[god_id]
    if god.family == 'school':
        school = god.exists.school
        days = [ e.day for e in events if e.kind=='surge' and e.school==school ]
        if days: surges[god_id] = days

full   = [ e.day for e in events if e.kind=='full_moon' ] if 'god_turning_moon' in chosen else []
hollow = [ e.day for e in events if e.kind=='hollow_night' ]
vigil  = any(by_id[g].aspect=='wild' and g in ('god_umbral','god_infernal') for g in chosen[:1])

return { 'surges': surges, 'full_moons': full,
         'hollow_nights': {'days': hollow, 'kept_as': 'vigil' if vigil else 'dread'} }
```

**`chosen[:1]` is the patron alone** — a people whose *second* god is the Wild Umbral still keeps hollow nights as dread. `hollow` is computed unconditionally even when the list is `dread`. Almanac event days are day-of-year 0..359 (`DAYS_PER_YEAR = 360`, `MONTHS_PER_YEAR = 12`, `DAYS_PER_MONTH = 30`). Almanac events are already sorted by `(day, kind, school)` upstream, so the extracted day lists are ascending.

---

## 10. `resolve_sites(world, gods, previous)`

```
by_id = { g.id: g for g in gods }                       # known gods only
ages  = world.get('history', {}).get('ages', [])
sites = []
for ruin in world.get('ruins', []):                     # ruin list order
    cause = ruin.get('cause', '')
    if cause == 'self_magic' and by_id.get('god_weave', {}).get('status') != 'absent':
        age   = first a in ages with a.age == ruin.get('destroyed_age')   # else None
        surge = age.moon.tide.weave if age else None                      # per-school tide multiplier, 0.5..1.8
        sites.append({ 'id': 'cult-'+ruin.id, 'kind': 'cult', 'god_id': 'god_weave',
                       'ruin_id': ruin.id, 'node': ruin.node,
                       'born_under_surge': bool(surge is not None and surge >= 1.3),
                       'weave_tide': surge })
    elif cause.startswith('war_'):
        sites.append({ 'id': 'shrine-'+ruin.id, 'kind': 'shrine', 'god_id': 'god_red_field',
                       'ruin_id': ruin.id, 'node': ruin.node })

for site in (previous or {}).get('sites', []):
    if site.get('kind') in ('theophany', 'pilgrimage'):
        sites.append(deepcopy(site))

return sorted(sites, key=lambda s: s['id'])
```

Ruin `cause` values come from `city_fate` / `resolve_wars`: one of the eight school names, `'dragon'`, `'monster'`, `'self_magic'`, `'divine_<god id>'`, a villain cause, or `'war_' + pair_kind`. Only `self_magic` and the `war_*` prefix produce a site.

The cult test is `!= 'absent'`, so a **sleeping** Weave god still gets cults at its ruins. The shrine has no god-status test at all — a Red Field shrine is written even if `god_red_field` were somehow absent (it cannot be; its rule is `always`).

`age['moon']` is the `lunar_context` record written at the age transition: `{day, phase_fraction, elongation_degrees, illumination, leaning, tide}`, where `tide` is the per-school multiplier on that day. A ruin whose `destroyed_age` matches no age (or a world with no ages) gets `weave_tide: null` and `born_under_surge: false`.

Only theophany/pilgrimage sites survive from the previous block; cults and shrines are rebuilt from the ruin list every time, so removing a ruin removes its site.

---

## 11. `hidden_records(previous)`

Docstring: *"Hidden gods as exported: absent until something reveals one. Exported rather than withheld on purpose. Nothing in a generated world points at them, no faith names one and no rule can raise one; keeping them out of sight is the game's job, not the generator's."*

```
prior = { g.id: g for g in (previous or {}).get('gods', []) if g.get('family') == 'outside' }
for god in hidden_catalogue().gods:                              # file order
    was = prior.get(god.id, {})
    out.append({ 'id': god.id, 'name': god.name, 'family': 'outside',
                 'status':      was.get('status', 'absent'),
                 'aspect':      was.get('aspect'),
                 'aspect_name': was.get('aspect_name'),
                 'domains':     list(god.domains),
                 'schools':     [god.school],                    # one hidden school
                 'rule':        dict(god.exists),
                 'evidence':    was.get('evidence'),
                 'saint_of':    None })
```

The hidden gods' own `exists` rules are evaluated by **nothing in this module** — `rule` is exported for the corruption API, which calls `rule_holds` on it itself. Status is pure carry-forward; `add_religion` never promotes a hidden god. `avatar` and `roused_by` are **not** carried forward for hidden gods.

---

## 12. `catalogue_identity()` and `lint_catalogue()`

```
canonical = json.dumps(doc, ensure_ascii=False, separators=(',',':'), sort_keys=True).encode('utf-8')
return { 'schema_version': doc.schema_version, 'revision': doc.revision,
         'sha256': sha256(canonical).hexdigest() }
```

Only `pantheon.json` is hashed. `hidden_pantheon.json` has its own file *specifically* so it can be edited without retiring saved worlds — the docstring: *"Their own file, with its own identity, so catalogue_identity() never moves: hashing them into pantheon.json would retire every saved world each time one was touched."*

`lint_catalogue()` is a test/CI gate, not run in the pipeline. It raises on: duplicate god ids; school-family gods not covering `KNOWN_SCHOOLS` exactly once; an unknown rule kind anywhere in a god's or a cosmology's rule tree; an `allies`/`rivals` entry naming an unknown god; a `folds_into` naming an unknown god; an empty `school_affinity` or one containing a token that is neither a known school nor the literal `'moon'`; a god with no `sovereign` aspect; a cosmology naming an unknown high god; `affinities` keys not equal to `set(civilization_ids())`; an affinity table naming an unknown god; and a `fear_gods` value or `wild_push_families` key that is not a known god.

## Constants

**Module-level (`terrain_religion.py`):**

| name | value | meaning / effect of changing it |
|---|---|---|
| `VERSION` | `1` | Stamped into `religion.version`. Three downstream modules (`terrain_visitation`, `terrain_corruption`, `terrain_history`) refuse a world whose value differs, with a "regenerate the world" error. Bumping it invalidates every saved world. |
| `PRIMORDIAL` | `('fire','water','earth','air')` | The four schools counted by `facts.primordial_count`, which gates `cos_elemental_court` (`min: 3`) and scales its weight (`per: 1`). Adding a fifth would make the Elemental Court easier to reach and heavier. |
| `SURGE_BIRTH` | `1.3` | Threshold on that age's Weave lunar tide for `born_under_surge` on a cult site. **Note it is not the almanac's own surge threshold, which is `SURGE = 1.6` in `terrain_astrology`.** Lowering it marks more cults as surge-born; raising it above 1.8 (`TIDE_MAX`) makes the flag permanently false. Purely cosmetic — nothing reads it back. |

**Catalogue-level (`pantheon.json`):**

| key | value | effect |
|---|---|---|
| `schema` | `"fantasy-world-generator.pantheon"` | identity only |
| `schema_version` | `1` | exported in `religion.catalogue` |
| `revision` | `1` | exported; **must be bumped whenever the file's bytes change**, or two different catalogues share a revision |
| `aspect_midpoint` | `1.2` | School-god Sovereign/Wild threshold on `mean_intensity × (1 + instability) + nest_push`. Strictly-greater comparison. Lowering it makes Wild aspects common, which matters because a Wild god's name is different and (via `terrain_visitation`) its divine-fate weight doubles. |
| `nest_aspect_push` | `0.15` | Additive push toward Wild for the Umbral (undead nests present) and the Infernal (infernal nests present). 12.5% of the midpoint — enough to tip a network sitting just under it, not enough to tip a quiet one. |
| `fold_free_cosmologies` | `["cos_mosaic","cos_ancestor_halls"]` | Under these two cosmologies no civic god becomes a saint (`saint_of` stays null for everyone). |
| `affinity_weights.strong` | `2` | affinity-table bonus |
| `affinity_weights.mild` | `1` | affinity-table bonus |
| `affinity_weights.aversion` | `-2` | large enough to knock a god out of the pool entirely unless it has cosmology/civic/potency support |
| `affinity_weights.high` | `1.5` | bonus for a cosmology high god |
| `affinity_weights.civic_base` | `0.5` | floor that keeps the seven civic gods always drawable — without it a civilization with no affinity entry for them would never worship one |
| `fear_gods` | `{draconic: god_great_wyrm, infernal: god_infernal, undead: god_umbral, aberrant: god_outer_unnamed}` | nest family → the god a threatened city fears |
| `wild_push_families` | `{god_umbral: [undead], god_infernal: [infernal]}` | which nest families push which god toward Wild |

**Constants borrowed from neighbours that this module's behaviour depends on:**

- `child_seed` prefix string `'tectonics-v1:'` and the 4-byte big-endian truncation of SHA-256.
- Seed domains: `'pantheon-v1'`, `'faith-v1-' + civilization_id`.
- `fantasy_nest_threats`: `350.0` m minimum reach, `× 2` on the nest's range, `radius × 0.5` reach cap, danger families `('infernal','undead','aberrant')`, default tier `5`. **All distances are design-space metres, pre-physical-scaling**, tuned against `REFERENCE_RADIUS_M = 1774.4123532462844` (≈11.15 km circumference).
- Almanac calendar: `DAYS_PER_MONTH = 30`, `MONTHS_PER_YEAR = 12`, `DAYS_PER_YEAR = 360`; tide band `TIDE_MIN 0.5 .. TIDE_MAX 1.8`; `SURGE = 1.6`, `HOLLOW = 0.75`.
- `KNOWN_SCHOOLS` has exactly 8 members in the fixed order `weave, umbral, infernal, radiant, fire, water, earth, air`.

## Data files

Two JSON files sit beside the module and are loaded lazily into module-level globals `_CATALOGUE` and `_HIDDEN` (cached for the process lifetime; `Path(__file__).with_name(...)`, `encoding='utf-8'`).

---

# A. `Sim/icarus_sim/pantheon.json` — 25 gods, 11 cosmologies, 12 affinity tables

Top-level keys: `schema`, `schema_version` (1), `revision` (1), `description`, `aspect_midpoint` (1.2), `nest_aspect_push` (0.15), `gods`, `cosmologies`, `fold_free_cosmologies`, `affinities`, `affinity_weights`, `fear_gods`, `wild_push_families`.

`description`: *"God catalogue and cosmology templates. Every god is independent: an existence rule evaluated against world state decides whether it is manifest, nothing inherits, and no god acts unless the orchestrator summons it."*

## A.1 Gods, in file order (this order IS the `religion.gods` order)

Every record has `id, family, name, aspects{sovereign[, wild]}, domains[], exists{}, allies[], rivals[], school_affinity[], purge_families[], smite`. Optional: `race` (ancestor gods), `epithets{}` (ancestor gods), `folds_into` (6 civic gods), `festival` (one god).

`purge_families` and `smite` are **read only by `terrain_visitation`**, never by this module; `allies` is read by nothing in the pipeline (only linted); `rivals` is read here (sects) and by `terrain_corruption` (for the hidden four).

### School family (8) — `exists: {kind:'school', school:<s>}`, all have both aspects

| id | name | sovereign / wild | domains | allies | rivals | school_affinity | purge_families |
|---|---|---|---|---|---|---|---|
| `god_weave` | The Weave | The Loom-Mother / The Laughing Unmaker | creation, creativity, chance, magic itself | god_air, god_fortune_fate | god_umbral | weave | — |
| `god_umbral` | The Umbral | The Quiet Judge / The Hollow Hunger | death, endings, memory, order | god_grave_memory | god_weave, god_radiant | umbral | holy, fey |
| `god_infernal` | The Infernal | The Chained Tyrant / The Devourer Below | ambition, pacts, ruin, temptation | — | god_radiant, god_crown_law | infernal | holy |
| `god_radiant` | The Radiant | The Dawn Healer / The Burning Verdict | healing, light, law, judgement | god_crown_law | god_infernal, god_umbral | radiant | undead, infernal, unholy |
| `god_fire` | The Fire | The Hearth-Forger / The Wildfire | fire, forge, renewal, passion | god_forge_craft | god_water | fire | undead |
| `god_water` | The Water | The Tide-Keeper / The Winter Drowned | sea, rain, ice, patience | god_drowned_mother | god_fire | water | infernal |
| `god_earth` | The Earth | The Green Grandmother / The Titan Under the Hill | harvest, stone, growth, endurance | god_hearth_harvest, god_deep_forge | god_air | earth | aberrant |
| `god_air` | The Air | The Wayfarer of Winds / The Storm-Crown | wind, storms, travel, freedom | god_road_market, god_wanderer | god_earth | air | — |

Smite sentences (for ruin flavour, used by visitation): Weave "reality unpicked itself around the city; nothing of it holds a shape"; Umbral "every hearth went cold in one night and the dead walked out of the gate"; Infernal "the ground opened under the market and the city paid the price of its pacts"; Radiant "a verdict of light burned the city to white ash"; Fire "the city burned for nine days and the stones ran"; Water "the river turned in its bed and took the city with it"; Earth "the hill stood up and the city was under it"; Air "a storm with a crown of lightning scattered the city across the plain".

### Ancestor family (3) — `exists: {kind:'race', race:<r>}`, sovereign aspect only

| id | name | race | domains | school_affinity | allies / rivals | purge |
|---|---|---|---|---|---|---|
| `god_first_hearth` | The First Hearth | human | founding, kinship, the walled town | radiant | god_hearth_harvest / — | — |
| `god_elder_star` | The Elder Star | elf | living wood, long memory, starlight | weave | god_weave / god_infernal | infernal |
| `god_deep_forge` | The Deep Forge | dwarf | stone, craft, oaths, hoard | earth | god_earth, god_fire / god_air | aberrant |

Epithets (the per-civilization display names; **the only source of `epithets` in the catalogue**):

- `god_first_hearth`: human_maritime → *Harbour-Mother*; human_desert → *Well-Father*; human_cold → *Long-Night Keeper*; human_large_island → *Shore-Ancestor*; human_rainforest → *Feathered Founder*; human_heartland → *The Old King*.
- `god_elder_star`: elf → *The Grove-Singer*; tidekin → *The Foam-Born*.
- `god_deep_forge`: dwarf → *The Anvil-Father*; gnome → *The Tinkering Spark*; hill_dwarf → *The Full Table*; frosthold_dwarf → *The Ember Under Ice*.

Smites: First Hearth "the founders' own walls closed on the city that forgot them"; Elder Star "the woods remembered the city and grew through it in a season"; Deep Forge "the oath-breakers' halls were sealed in stone with everyone inside".

### Feature family (7)

| id | name | exists rule | aspects | domains | school_affinity | allies / rivals | purge |
|---|---|---|---|---|---|---|---|
| `god_drowned_mother` | The Drowned Mother | `any[ ocean_share ≥ 0.5 ; civilization_city ids=[human_maritime, human_large_island, tidekin] ]` | sovereign only | the sea, fishing, storms at sea, drowning | water | god_water / god_fire | infernal |
| `god_sky_court` | The Sky Court | `sky` | sovereign only | floating islands, altitude, the far view | air | god_air / god_earth | — |
| `god_great_wyrm` | The Great Wyrm | `dragon` | sovereign only | dragons, hoards, fear, fire from above | fire | god_fire / god_crown_law | fey, holy |
| `god_outer_unnamed` | The Unnamed Outside | `nest_family families=[aberrant]` | sovereign only | madness, the wrong shape of things, forbidden knowledge | weave, umbral | — / god_radiant, god_earth | holy, fey, primordial |
| `god_fallen_cities` | The Keeper of Fallen Cities | `ruins min=1` | sovereign only | ruins, lost knowledge, warnings | umbral | god_grave_memory / — | — |
| `god_wanderer` | The Wanderer | `diaspora` | sovereign only | exile, roads between peoples, second chances | air | god_air, god_road_market / — | — |
| `god_turning_moon` | The Turning Moon | `always` | **The Pale Warden / The Turning Face** | the moon, tides of magic, calendars, the great year | **moon** (not a real school) | god_fortune_fate / — | — |

`god_turning_moon` additionally carries `festival`: *"full moons; hollow nights are dread for every faith but the Wild Umbral and Infernal cults, who keep them as vigils"* — flavour text only; the actual rule is in `feasts()`.

Smites: Drowned Mother "the sea came up the streets and did not go back"; Sky Court "the court sat in judgement above the city and let it fall from its favour"; Great Wyrm "the Wyrm took the city as its hoard and left the walls as its teeth"; Unnamed Outside "the city is still there, but its angles are wrong and nobody who enters comes back the same"; Keeper "the Keeper added one more name to the list"; Wanderer "the roads left the city and its people followed them"; Turning Moon "every school surged at once under a moon that filled half the sky, and the city could not stand it".

### Civic family (7) — all `exists: {kind:'always'}`, sovereign aspect only

| id | name | folds_into | domains | school_affinity | allies / rivals | purge |
|---|---|---|---|---|---|---|
| `god_hearth_harvest` | The Hearth and the Harvest | god_earth | home, bread, children | earth | god_earth / god_red_field | undead |
| `god_forge_craft` | The Maker | god_fire | tools, walls, guilds | fire | god_fire, god_deep_forge / — | — |
| `god_road_market` | The Coin and the Road | god_air | trade, oaths between strangers, travel | air | god_air, god_wanderer / — | — |
| `god_crown_law` | The Crowned Scale | god_radiant | kingship, law, treaties | radiant | god_radiant / god_infernal, god_great_wyrm | infernal |
| `god_grave_memory` | The Grave Keeper | god_umbral | natural death, ancestors, remembrance | umbral | god_umbral, god_fallen_cities / — | undead |
| `god_fortune_fate` | The Turning Wheel | god_weave | luck, weather, fate | weave | god_weave, god_turning_moon / — | — |
| `god_red_field` | The Red Field | **none** | war, courage, the honoured dead | fire, infernal | — / god_hearth_harvest | — |

Smites: Hearth "the fields refused the city and the hearths would not light"; Maker "every wall the city ever raised came down in the same hour"; Coin and Road "no road led to the city any more, and it starved in sight of its neighbours"; Crowned Scale "the Scale found the city wanting and the sentence was carried out at once"; Grave Keeper "the ancestors rose to reclaim what the living had squandered"; Turning Wheel "the wheel turned once and the city's luck ran out all together"; Red Field "the city fought a war with no enemy and lost it".

## A.2 Cosmologies, in file order (this order drives `eligible`)

| id | name | requires | weight | high | arrangement |
|---|---|---|---|---|---|
| `cos_elemental_court` | The Elemental Court | `primordial_count min=3` | `base 0 + 1×primordial_count` | fire, water, earth, air | "Fire, Water, Earth and Air are high; Weave and Umbral are titans before the court; Radiant and Infernal are outsiders." (also carries unused `titans:[god_weave,god_umbral]`, `outsiders:[god_radiant,god_infernal]`) |
| `cos_dual_dawn` | Dawn and Pit | `gods ids=[god_radiant, god_infernal]` (implicit `all: true`) | `base 2` | radiant, infernal | "Two high gods; every other god is a saint of one or a devil of the other." |
| `cos_ancestor_halls` | The Ancestor Halls | `races min=2` | `base 0 + 1×len(races)` | first_hearth, elder_star, deep_forge | "Parent-race gods are high; school gods are impersonal powers with no temples." |
| `cos_spirits_of_place` | Spirits of Place | `any[ school weave ; school earth ]` | `base 1.5` | *(none)* | "No high gods; every leyline key point is a named local spirit; civic gods are household spirits." |
| `cos_one_sun` | The One Sun | `any[ school radiant ; magic_disabled ]` | `base 1.5` | radiant, crown_law | "One over-god (Radiant Sovereign, else the Crowned Scale); all others become saints or angels." |
| `cos_silent_heavens` | The Silent Heavens | `any[ ruins min=2 ; school umbral ]` | `base 0.5 + 0.5×ruins` | fallen_cities, wanderer | "The gods are dead or withdrawn; only the Keeper of Fallen Cities, the Wanderer and cults of the Wild aspects are active." |
| `cos_sea_and_sky` | Mother Sea, Father Sky | `any[ gods ids=[god_drowned_mother] ; sky ]` | `base 1.5` | drowned_mother, air, sky_court | "The Drowned Mother and Air (or the Sky Court) are the high pair; others are their children." |
| `cos_wyrm_thrones` | The Wyrm Thrones | `dragon` | `base 0 + 1×dragons` | great_wyrm | "The Great Wyrm is high; each dragon nest is a demigod with a territory." |
| `cos_the_woven` | The Woven | `school weave` | `base 1.5` | weave | "Magic itself is divine; the Loom-Mother is high and colleges are its churches." |
| `cos_turning_moon` | The Turning Moon | `magic_enabled` | `base 0 + 8×lunar_influence` | turning_moon | "The moon is high; each school god is one of its eight faces and the civic gods are its months." |
| `cos_mosaic` | The Mosaic | `always` | `base 0.5` | *(none)* | "No world-level high gods; each civilization ranks its own faith." |

Four of these have `base 0`, so a world where the fact is zero makes them ineligible even when `requires` holds — e.g. `cos_turning_moon` requires only `magic_enabled` but weighs `8 × lunar_influence`, so `lunar_influence == 0` drops it out.

## A.3 Affinity tables (one per civilization; keys must equal `civilization_ids()`)

| civilization | strong (+2 each) | mild (+1 each) | aversion (−2 each) |
|---|---|---|---|
| human_maritime | drowned_mother, water | road_market, air | earth |
| human_desert | fire, water | crown_law, wanderer | drowned_mother |
| human_cold | water, hearth_harvest | red_field, umbral | fire |
| human_large_island | drowned_mother, air | elder_star, fortune_fate | infernal |
| human_rainforest | earth, great_wyrm | fire, umbral | water |
| human_heartland | crown_law, hearth_harvest | radiant, red_field | outer_unnamed |
| elf | elder_star, weave, earth | radiant | infernal, fire |
| tidekin | elder_star, drowned_mother | water, air | earth |
| dwarf | deep_forge, earth, fire | red_field | air, weave |
| gnome | deep_forge, weave, forge_craft | air, fortune_fate | umbral |
| hill_dwarf | hearth_harvest, earth | fortune_fate, road_market | great_wyrm |
| frosthold_dwarf | deep_forge, water, fire | red_field | sky_court |

(`god_` prefix elided in the table for width.)

---

# B. `Sim/icarus_sim/hidden_pantheon.json` — 4 gods

Top-level: `schema` (`"fantasy-world-generator.hidden-pantheon"`), `schema_version` 1, `revision` 1, `description`, `notes`, `gods`.

`description`: *"The four the known pantheon does not speak of. They are not in pantheon.json on purpose: catalogue_identity() hashes that file, and a world would be retired every time one of these was touched. They are still exported, as absent, because hiding them is the game's job and not the generator's."*

`notes` (design rationale; **record this, it is the whole design of the corruption trigger**):
- `noticing`: *"Encounters the game has reported before this god will act at all. Below it the chance is exactly zero, not merely small: early playthroughs are guaranteed clean, which is what makes a first corruption read as an escalation the player caused rather than a bad roll."*
- `interest`: *"Which god takes an interest is read from the world, never rolled. The counter says whether; the world says which."*
- `climb`: *"Encounters above the noticing point, at which this god is certain to act if it is the most interested."*

Record shape: `id, family:'outside', name, school (a HIDDEN school), aspects{sovereign, wild}, domains[], exists{}, interest, noticing, climb, rivals[], purge_families[], smite`. **No `school_affinity`, no `allies`, no `epithets`, no `folds_into`** — which is why `hidden_records` builds `schools` from the single `school` field rather than `school_affinity`.

| id | name | school | sovereign / wild | exists | interest | noticing | climb | rivals | purge_families |
|---|---|---|---|---|---|---|---|---|---|
| `god_long_thirst` | The Long Thirst | blood | The Patient Vein / The Red Harvest | `magic_enabled` | `living` | 3 | 12 | radiant, hearth_harvest, first_hearth | holy, fey |
| `god_glad_mother` | The Glad Mother | rot | The Kind Decay / The Teeming Bloom | `magic_enabled` | `decay` | 6 | 16 | radiant, grave_memory | holy |
| `god_shape_beneath` | The Shape Beneath | eldritch | The Quiet Geometry / The Waking Angle | `any[ nest_family families=[aberrant] ; ruins min=1 ]` | `aberrant` | 10 | 22 | radiant, earth, crown_law | holy, fey, primordial |
| `god_unwriting` | The Unwriting | void | The Long Silence / The Open Absence | `always` | `dead_magic` | 15 | 30 | weave, fire, great_wyrm | holy, fey, primordial, draconic |

Domains: Long Thirst — sacrifice, lineage, vitality taken and spent, hunger that does not end. Glad Mother — disease, undeath, decay that is fertile, rebirth nobody asked for. Shape Beneath — madness, wrong geometry, dominion of minds, knowledge that costs. Unwriting — absence, entropy, unmaking, the silence after.

Smites: Long Thirst "the city stood and its people did not; what walked the walls afterward wore their faces"; Glad Mother "the dead did not stay where they were put, and there were more of them each morning"; Shape Beneath "nothing was destroyed; the gates opened on time and the wrong people gave the orders"; Unwriting "the ground where it stood is on no map now, and the ley runs around the place it was".

`interest`, `noticing`, `climb`, `rivals`, `purge_families` and `smite` are consumed by `terrain_corruption.py`, not by this module — but this module is what makes them reachable in a saved world, so the fields must survive the port even though religion itself only touches `id, name, domains, school, exists, aspects.wild` (the last via `_reveal`).

## Random streams

Two named streams, both derived with `child_seed(master, domain, variation=0)` =
`int.from_bytes(sha256(f'tectonics-v1:{master}:{domain}:{variation}').digest()[:4], 'big')`, then fed to CPython's `random.Random(seed)` (Mersenne Twister). `master` is `result['config']['seed']`.

**1. Cosmology stream — `child_seed(seed, 'pantheon-v1')`.**
Exactly **one** `random()` draw per `add_religion` call, always, even when only one template is eligible (`_draw` unconditionally calls `rng.random()`). The stream is created fresh inside `choose_cosmology` and discarded.

**2. Faith streams — `child_seed(seed, 'faith-v1-' + civilization_id)`, one per civilization.**
A fresh `Random` per civilization, so the number of draws a civilization takes cannot move any other civilization's result. Draw count is **data-dependent**:

- `0 or 1` for the patron: **zero** if the civilization's parent-race ancestor god is manifest/walking (the ancestor is taken directly); **one** if not and `weighted` is non-empty; **zero** if `weighted` is empty too.
- `0..3` for the secondaries: the `while pool and len(chosen) < 4` loop draws once per addition and removes the picked god from the pool. With a patron already seated it draws at most 3; with no patron at all, at most 4 — but a patron-less civilization has empty `weighted` and therefore an empty pool, so in practice it is 0.

Maximum per civilization: 4 draws. With 12 civilizations, worst case 48 + 1 = 49 draws per resolution.

**A stream-position hazard worth naming:** when a visitation conversion overrides the patron, the initial `_draw` for the patron has *already happened* if there was no ancestor — the value is computed and then thrown away. Remove that draw and every subsequent secondary pick for that civilization shifts. In a clean rewrite you may reorder this (numbers need not match), but if you do, say so, because it changes which gods a converted people worships alongside its new patron.

**No other randomness.** `resolve_gods`, `aspect_of`, `resolve_sites`, `feasts`, `hidden_records`, `world_facts` and the `saint_of` pass are fully deterministic functions of world state.

**Determinism contract** (asserted by `test_existence_follows_the_world_and_is_deterministic`): two independent `add_religion` calls on identical worlds must produce byte-identical `religion` blocks, and the block must be JSON-serialisable with `allow_nan=False` (no NaN/Inf can reach `weights`, `weave_tide`, `mean_intensity` or `mean_nod_degrees`).

**C++ mapping:** `fwg::Rng::child(master, domain, variation)` is the established third seed derivation. Use `Rng::child(seed, "pantheon-v1")` and `Rng::child(seed, "faith-v1-" + civ)` — do **not** invent a fourth derivation. `Rng::random()` returns `[0,1)` with 53 bits, which is what `_draw` needs.

## Ordering hazards

Every place iteration order, sort stability or container ordering reaches the exported block. This list is exhaustive for this module.

1. **`catalogue()['gods']` file order → `religion['gods']` order.** `resolve_gods` iterates the JSON array as loaded. A C++ table must preserve exactly this order; `test_removing_the_dragon_removes_the_wyrm_and_nothing_else` zips two god lists positionally and would silently pass on a reordered list only by luck.

2. **`catalogue()['cosmologies']` file order → `cosmology['eligible']` order → `_draw`'s linear walk.** Reordering the array changes which cosmology a given seed picks, even with identical weights.

3. **`hidden_catalogue()['gods']` file order → the last four entries of `religion['gods']`.**

4. **`KNOWN_SCHOOLS` dict order** drives `facts['schools']` key order (exported) and `mean_potency`'s result order. The declaring comment insists the eight are positional forever.

5. **`facts['races'] = sorted({...})`** — a set comprehension over `entity_rules(...)['parent_race_id']`, explicitly sorted. The pre-port red team called this out by line: *"`PYTHONHASHSEED` is pinned nowhere in the repo … set iteration over strings is safe today only because the sites I sampled funnel through `sorted()` (`terrain_religion.py:138,143`)"*. Line 138 is `races`, line 143 is `families`. **Both must stay sorted; nothing else in the module protects them.**

6. **`facts['families'] = sorted({...})`** — same hazard. Only `any(... in families)` membership is tested downstream, so the sort is for export stability rather than logic, but it is exported.

7. **`facts['civilizations_with_cities'] = sorted({...})`** — this one is *logic*: it is the iteration order of `choose_faiths`, and therefore the insertion order of the `faiths` dict. It is a set comprehension over `population_profile`, sorted. Without the sort, faith dict order (and, in a language with real per-key streams, nothing else) would vary.

8. **`facts['schools']` is built as a dict comprehension over `KNOWN_SCHOOLS`** — fixed.

9. **`manifest` list in `choose_faiths`** is `[g for g in gods if …]` — catalogue order. This is `weighted`'s build order, and `weighted` is `_draw`'s walk order. **Catalogue order therefore decides ties in the weighted draw.**

10. **`net['nodes'] + net['edges']` concatenation in `aspect_of`** — only a mean is taken, so order affects only floating-point summation order. Irrelevant for a clean rewrite (numbers need not match) but note it if you ever compare against Python.

11. **`rivals[0]` in the sect branch** — the first entry of the catalogue's `rivals` array that is live. Reordering `rivals` changes which god a schism follows.

12. **`chosen` list order** — patron first, then draws in pick order. `feasts()` reads `chosen[:1]` (patron only) and `sects` falls back to `chosen[1]`. Both are position-sensitive.

13. **`sorted(nests, key=lambda n: n['id'])` inside `fantasy_nest_threats`** — a **string** sort on nest ids. This decides which nest is retained by the fear dedupe (first wins), so the exported `nest_id` depends on lexicographic id order, not numeric.

14. **`fear = sorted(fear, key=lambda f: (f['city_uid'] or '', f['god_id']))`** — two-key sort; `city_uid` may be `None`, coerced to `''`, which sorts first. Python's sort is stable, but the key pair is unique per entry (the dedupe guarantees it), so stability does not matter.

15. **`resolve_sites` returns `sorted(sites, key=lambda s: s['id'])`** — string sort. Ruin-derived ids (`cult-…`, `shrine-…`) and carried theophany/pilgrimage ids interleave lexicographically.

16. **`world['ruins']` list order** feeds `resolve_sites` before the sort, so it affects nothing in the output — but the *carried* theophany sites are appended after and then sorted, so the final order is purely lexicographic.

17. **`founding['events']` list order → `sects` list order.** Not sorted. Multiple schism events for one civilization produce sects in event order.

18. **`cities` list order (settlement site array order) → the `nest_id` retained by a fear dedupe** for a (god, city) pair is per-city, so this one does not actually leak — but the *order in which* `fear` is appended does, before the final sort erases it.

19. **`conversions` dict built from `previous['faiths'].items()`** — Python dict insertion order, which is the previous run's sorted civilization order. Only used as a lookup, so order does not reach output.

20. **`weights` dict built from the `weighted` list** — exported as an object; in JSON the key order is insertion order in Python. A C++ `std::map` would emit sorted keys instead. Harmless for consumers, but it is an observable diff.

21. **`religion` block key order** — Python dict literal order is the JSON emission order. If byte-comparable exports matter to a downstream tool, fix the writer's field order to: `version, catalogue, facts, cosmology, gods, faiths, sites, visitations, corruptions, method, limits`.

22. **`catalogue_identity()` canonicalisation** uses `sort_keys=True`, so the hash is order-independent — deliberately, so a re-indented catalogue keeps its identity.

## Edge cases

**Empty / missing world blocks.** Every read is `.get()`-guarded, so `add_religion({})` does not crash: with no `config`, seed falls to 0; `facts` comes out all-zero/all-empty; `magic_enabled` is `bool({}.get('enabled', any([])))` = `False`; all eight school gods are absent; the seven civic gods and `god_turning_moon` are manifest (`always`); `facts['civilizations_with_cities']` is empty so `faiths` is `{}`; `sites` is `[]`; the only eligible cosmologies are `cos_one_sun` (via `magic_disabled`) and `cos_mosaic`. The magic-disabled path is explicitly tested.

**No cities at all.** `choose_faiths` returns `{}`. No crash, no fear, no sects, no feasts.

**A civilization with cities but no affinity table.** `table = doc['affinities'].get(civilization, {})` — all three `table.get(key, [])` return empty, so the god scores only cosmology-high, civic-base and potency. `lint_catalogue` guarantees all twelve are present, but the `.get` default means a new civilization added to `terrain_profiles.json` without a pantheon entry degrades rather than crashes at runtime (it *does* fail the lint).

**Empty `weighted` and no ancestor.** `patron = None`, `chosen = []`, `names = {}`, `pool = []`, so no draws happen at all. `sects` is skipped (`if patron`). `feasts` gets an empty `chosen`: `surges = {}`, `full = []` (moon not in chosen), `hollow` is still populated, `vigil = any(... for g in [])` = `False` → `kept_as: 'dread'`. The faith record is exported with `patron: null, gods: []`.

**Fewer than four live gods.** The `while pool and len(chosen) < 4` loop stops on the empty pool. `len(faith['gods']) <= 4` is asserted by test; there is no lower bound.

**A god in `chosen` that is not in the known catalogue.** Impossible: `chosen` is drawn from `manifest`, which comes from `resolve_gods`, which iterates `catalogue()['gods']`. But `feasts()` and the `names` loop both index `gods_by_id()[god_id]` with `[]` — a **KeyError**, not a graceful miss, if that ever stops holding. In particular a hidden god can never enter `chosen` because `manifest` in `choose_faiths` is computed from `gods` (known only) and `conversions` is checked against `by_id` (known only).

**A conversion naming a god that went absent.** `by_id.get(conversion['god_id'], {}).get('status') in ('manifest','walking')` — a sleeping or absent god fails the test, so the conversion is ignored for patron purposes but is **still exported** in `faith['conversion']` (the field is assigned before the guard's effect). So a faith can carry a `conversion` record whose god is not its patron. The comment says the conversion holds "until it stops existing" — sleeping counts as stopped.

**`previous` records missing keys.** `resolve_gods` reads `prior['status']` and `prior['aspect']` with `[]`. A hand-built or truncated previous block without those keys raises `KeyError`. `hidden_records` is more forgiving (`was.get(...)` throughout).

**`prior['status'] == 'walking'` is absolutely sticky in this module.** Nothing in `resolve_gods` can end it; only `terrain_visitation.depart_god` (which pops `avatar` and sets `manifest`) or `terrain_corruption.cleanse` (which sets `sleeping` with `evidence.last_seen.cleansed_age`) can.

**A sleeping god with no prior evidence.** `{'last_seen': prior.get('evidence', {})}` — nests an empty dict. Repeated sleeps nest `last_seen` inside `last_seen` each time? No: `evidence` is rebuilt fresh each resolve, so a god that stays asleep across N resolves accumulates N levels of `{'last_seen': {'last_seen': …}}`. **This is unbounded nesting in a long-lived world.** It is bounded in practice by the number of age transitions and API calls, but a heavily-played world will grow it. Worth flagging as a defect, not a feature.

**`globe_radius` missing → `1.0`.** Then in `fantasy_nest_threats`, `distance = 1.0 × acos(dot) ≤ π` and `reach = min(0.5, max(350, …)) = 0.5`, so only nests within 0.5 radians (~28.6°) of a city produce fear. No crash, but the fear list becomes an angular test rather than a metric one. The unit test supplies `globe_radius: 1000.0` explicitly.

**`total == 0` in the water block** → `ocean_share = 0.0`, so `god_drowned_mother` falls back to the `civilization_city` half of its rule.

**No almanac.** `aspect_of('god_turning_moon')` sees `months = []` → `lean = 0.0` → `lean >= 0` → **sovereign**, "still". `feasts` sees `events = []` → all three lists empty, `kept_as: 'dread'`.

**No history ages.** `facts['wars'] = 0`; `resolve_sites` finds `age = None` for every ruin → `surge = None` → `weave_tide: null`, `born_under_surge: false`.

**`ruin['destroyed_age']` matching no age** — same as above.

**`nest_family` / `families` with a `None` family.** `families` filters `if n.get('family')` before adding, so `None` and `''` never enter the sorted set — but `facts['dragons']` and `fear_gods.get(threat['family'])` both tolerate `None` (`.get(None)` returns `None`).

**Pole / seam behaviour.** Religion never touches the grid geometry directly except through `layers['ley_<school>'][z][x]` at city cells. Cities are never placed on a pole row or on the duplicated seam column by the settlement stage, so `mean_potency` reads interior cells. If a city ever did land on column `n-1`, it would read the seam duplicate of column 0, which is the correct value anyway.

**World edges.** No wrapping, no neighbourhood walk, no flood fill — nothing in this module has an edge case at the world boundary. Great-circle distance in `fantasy_nest_threats` is `acos` of a clamped dot product, which is wrap-correct by construction; the clamp to `[-1, 1]` guards `acos` against floating-point overshoot on coincident or antipodal directions.

**`_draw` with all-zero total.** Cannot happen (all callers filter `w > 0`), but if it did, `target = 0.0`, the first `target -= weight` leaves `0.0 <= 0` true, and the first item is returned. The trailing `return weighted[-1][0]` is the float-drift guard for the case where cumulative subtraction never quite reaches zero.

**`lint_catalogue` failures** are raised as `ValueError` and are never caught; the function is not called from the pipeline, only from tests and the identity pin.

## Reasoning worth preserving

Quoted from the source, because these comments record decisions and failures that exist nowhere else.

**Module docstring** — the whole design contract:
> "Pantheon: gods that may exist, a cosmology the world chooses, faiths the peoples keep. Everything here is narrative provenance resolved deterministically from world state. A god is manifest, sleeping or absent; it changes nothing in the world unless the orchestrator summons it through terrain_visitation. The catalogue ships as data (pantheon.json, decision 020); this module only interprets it."

**`hidden_catalogue()`** — why the hidden four live in a second file:
> "The four the known pantheon does not speak of. Their own file, with its own identity, so catalogue_identity() never moves: hashing them into pantheon.json would retire every saved world each time one was touched."

**`hidden_records()`** — why they are exported at all rather than withheld:
> "Hidden gods as exported: absent until something reveals one. Exported rather than withheld on purpose. Nothing in a generated world points at them, no faith names one and no rule can raise one; keeping them out of sight is the game's job, not the generator's."

**`lint_catalogue()`**:
> "Raise on a catalogue that could not be resolved; used by tests and the identity pin."

**`world_facts()`**:
> "The inputs existence rules read, taken from what the generator already exports."
This is a scoping rule: an existence rule may only read facts the world already publishes. Do not add a rule kind that reaches into a layer or recomputes geometry.

**`aspect_of()`**:
> "Sovereign below the intensity/instability midpoint, Wild above; the moon follows its lean."

**`_draw()`**:
> "One seeded pick from (item, weight) pairs with positive weights, in list order."

**The conversion branch in `choose_faiths`**:
> "# A god that walked among this people is its patron until it stops existing."

**`add_religion()`**:
> "Resolve gods, cosmology, faiths and sites from the current world. Narrative only."

**The `corruptions` carry-forward**:
> "# Carried forward like visitations: resolving religion rebuilds this block, and a corruption record outliving its rebuild is the whole point of a persistent node."

**The exported `method` string** (this is user-facing provenance and should be reproduced as-is or updated deliberately):
> "Every god has an independent existence rule read from exported world state; a god that stops qualifying sleeps with its last aspect. One cosmology is drawn from the eligible templates by fit weight. Each civilization with a live city takes its ancestor god as patron and draws up to three more by affinity, cosmology rank and the leyline potency under its cities. Feasts follow the almanac."

**The exported `limits` string**:
> "Gods are narrative provenance only: no terrain, food, threat or influence effect until the orchestrator summons one through the visitation API. Names and epithets are authored English."

**From `hidden_pantheon.json`'s `notes`** — the reason the corruption trigger has a hard floor rather than a small probability:
> `noticing`: "Encounters the game has reported before this god will act at all. Below it the chance is exactly zero, not merely small: early playthroughs are guaranteed clean, which is what makes a first corruption read as an escalation the player caused rather than a bad roll."
> `interest`: "Which god takes an interest is read from the world, never rolled. The counter says whether; the world says which."

**From `terrain_corruption._reveal()`** — why a revelation pushes the known gods Wild:
> "The known gods stop not speaking of it. Before a return there is no trace of the hidden four anywhere; afterwards the known pantheon gains it as a rival and its aspects are pushed Wild, which is what makes the cure able to become worse than the disease: a Wild god destroys four times as much."

**From `terrain_nomads._god_for_school()`** — why it does *not* use `gods_by_id()`:
> "Deliberately not `terrain_religion.gods_by_id`: that reads `pantheon.json` alone, so the four hidden gods are absent from it and a blood cult -- the exact case a villain of the blood god creates -- would raise instead of finding its patron. Hidden gods are the entries whose family is 'outside'."

**From `terrain_history.rebuild_tail()`** — why religion is re-resolved by one caller and not the other:
> "`religion` re-resolves the pantheon, which an age turning does and a visitation does not, because a visitation resolves religion itself once its own records are written."

**From `terrain_leyline_history`** — why the eight schools are a closed set:
> "The world's own taxonomy. Everything this world can name, measure, chart against its moon or raise a god for is one of these eight, and the places that must stay eight forever say KNOWN_SCHOOLS rather than SCHOOLS."

**From `terrain_history.fantasy_nest_threats()`** — the tier scaling religion inherits through the fear list:
> "Weight scales with the danger tier, so a greater lair at the gates is the reason a city falls and a tier one nuisance is not; tier five keeps the flat .55 the single fixed weight used to give every monster alike."

**From the test docstring** (`Sim/tests/test_religion.py`):
> "The pantheon resolves from world state and changes nothing until summoned. Hand-built worlds keep this independent of terrain_history wiring."

## Notes for the C++ implementation

Read `include/fwg/` first; nothing below invents a convention.

**1. There is no JSON reader.** `include/fwg/json.hpp` exposes only `JsonWriter` (with `value_null()`). The library has *no dependencies at all*. `pantheon.json` and `hidden_pantheon.json` therefore cannot be loaded as data without writing a parser. Two options, and the choice must be deliberate:
   - **(a) Embed the catalogue as a C++ table** — a `constexpr`/static array of structs in `src/religion_catalogue.cpp`. This is the lower-risk option and matches "no dependencies". The cost is that `catalogue_identity().sha256` has no file to hash; see point 3.
   - **(b) Write a minimal JSON reader** and ship the two files as data. This preserves decision 020 ("the catalogue ships as data") and makes the catalogue editable without a rebuild, but it adds a parser, a file-location convention and an encoding contract the library does not currently have.
   Recommend (a) with the identity handled per point 3, and say so in `docs/porting-status.md`.

**2. `std::map` vs Python dict ordering.** Python dicts preserve insertion order and that order reaches the JSON output for `facts.schools`, `religion.faiths`, `faith.names`, `faith.weights`, `feasts.surges` and the top-level `religion` object. `std::map<std::string, T>` emits sorted keys instead. For `faiths` and `facts.schools` this changes the export. Use an insertion-ordered container (a `std::vector<std::pair<std::string, T>>` with a lookup index) wherever the key order is observable, or accept sorted output and record the divergence. `facts.schools` in KNOWN_SCHOOLS order is the one that matters most — a consumer reading positionally would break.

**3. `catalogue_identity()` has no natural C++ equivalent.** The Python computes `sha256(json.dumps(doc, ensure_ascii=False, separators=(',',':'), sort_keys=True).encode('utf-8'))`. Three downstream gates (`terrain_visitation`, `terrain_corruption`, `terrain_history`'s age gate) compare a saved world's `religion.catalogue` against the live value and refuse the world if they differ. In a clean rewrite with an embedded table, produce the hash from a canonical serialisation of the embedded table using `fwg::sha256` — write the table out with `JsonWriter` in a fixed field order, hash the bytes. The important property is not matching Python's digest (numbers need not match) but that **the digest changes iff the catalogue changes**. Keep `schema_version` and `revision` as literal integers alongside it; `revision` must be bumped by hand on every catalogue edit, and there is no automated check for that.

**4. No `fwg::Grid` interaction beyond point sampling.** The only layer read is `layers["ley_" + school](z, x)` at city cells — check the accessor convention in `grid.hpp` (the Python is `grid[z][x]`, row-major with `z` first). Religion does **not** need `fwg::sphere_grid`; `fantasy_nest_threats` works on unit direction vectors (`fwg::Vec3` dot product) and a scalar radius, not on the node graph. `acos(std::clamp(dot, -1.0, 1.0))` — keep the clamp.

**5. `fwg::Rng` maps cleanly.** Use `Rng::child(config.seed, "pantheon-v1")` and `Rng::child(config.seed, "faith-v1-" + civ_id)`; `Rng::random()` is the `[0,1)` draw `_draw` needs. Do not add a fourth seed derivation. Note the `child` signature takes `std::int64_t variation` — pass nothing (default 0), matching `child_seed`'s default.

**6. Optional fields.** Python emits `None` for `aspect`, `aspect_name`, `evidence` (hidden gods), `saint_of`, `patron`, `conversion`, `weave_tide`, and omits `avatar` and `roused_by` entirely when absent. Model this with `std::optional<T>` plus an explicit "present" flag where Python distinguishes *absent key* from *null value* — `avatar` is the one case where the distinction is real (`prior.get('avatar')` is falsy-tested, and the key is only written when truthy). `JsonWriter::value_null()` covers the null case.

**7. `rule_holds` is a tagged recursive expression.** In Python it is a chain of string comparisons over a `dict`. In C++, model it as a small variant/enum tree: an enum for the 17 kinds, plus a struct holding whatever payload that kind needs (`school`, `race`, `ids`, `families`, `min`, `all`, `rules`). Since the catalogue is embedded, the tree can be built once at static-init time, not parsed per call. Keep the "unknown kind throws" branch — it is the only guard against a catalogue typo becoming a silently-false rule.

**8. `god['rule']` is shared by reference in Python.** `record['rule'] = god['exists']` aliases the catalogue object into the export. In C++ you will copy it, which is strictly safer; nothing mutates it. But note that `hidden_records` does `dict(god['exists'])` — a *shallow* copy that still aliases any nested `rules` list, which matters for `god_shape_beneath`'s `any[...]` rule. Copy deeply; no caller mutates, so no behaviour changes either way.

**9. `copy.deepcopy` appears four times**: `record['avatar']`, carried `theophany`/`pilgrimage` sites, `visitations`, `corruptions`. These are opaque JSON blobs written by other modules. Until `terrain_visitation` and `terrain_corruption` are ported, a `fwg::World` has nowhere to put them — define the fields now (a generic JSON-value type, or typed structs once those modules land) rather than dropping them, because a world that round-trips through the C++ and loses its `corruptions` array loses game state permanently.

**10. Python truthiness pitfalls to make explicit in C++.**
   - `bool(networks.get(s, {}).get('nodes'))` — *non-empty list*, not "key present".
   - `not n.get('real', True)` — a nest is fantasy iff `real` is present and falsey. In C++ with a typed nest struct, `real` will have a default; make it `true` and test `!nest.real`.
   - `magic.get('enabled', any(schools.values()))` — the default is computed, not `false`.
   - `sum(schools[s] for s in PRIMORDIAL)` sums bools as ints.
   - `weight > 0` filters, and `aversion` is `-2`, so scores can legitimately be negative and are dropped.
   - `names.get(rival, by_id[rival]['name'])` and `record.get('epithets',{}).get(civ) or … or record['name']` — the `or` chain treats an empty string as missing.

**11. `round(w, 6)`** on the exported weights is CPython's round-half-to-even on the decimal representation. A clean rewrite may use any sane rounding; note the divergence and move on.

**12. Unbounded `last_seen` nesting** (see edge cases): a god that sleeps across many resolves accumulates `{'last_seen': {'last_seen': …}}`. In a typed C++ model this becomes an unbounded recursive structure. Fix it in the port — collapse to a single level, or store `last_seen_age` — and record the change; this is a defect, not intent.

**13. `fantasy_nest_threats` must be shared, not duplicated.** It lives in `terrain_history` (spec 14/15) and is called by the age-fate lottery, the threat assessment and religion's fear list. Put it in one place in the C++ and have all three call it, because the three must agree on nest eligibility and reach — the Python comment says as much ("Shared by age fate and regional threat assessment", "Same nest eligibility and reach as age-fate weights").

**14. Stage gating.** `fwg::World::completed_stage` is currently 5 and `kHighestImplementedStage` is 5. Religion is stage 13 and reads stage 5, 9, 10–12 and 13 outputs; it cannot be built meaningfully before hydrology, climate, leylines, settlements and nests exist. It is, however, one of the cheapest stages to port once they do — pure data interpretation with no geometry.

**15. `docs/pantheon.md` states the opposite of the current mandate**: *"Both modules are Python only, like the age API; the native core does not port them."* That sentence is now false and should be struck when this spec lands.

## Open questions

There is **no conformance record** for this module. `docs/conformance/` contains only: `actor-scale-writes, creature-placement, encounter-index, nomads, read-surface, request-failures, settlement-founding, settlement-records, stage-profile, time-advance, world-geometry, world-handles, world-parameters`. There *is* a narrative doc, `docs/pantheon.md`, and where it disagrees with the code the code wins; the disagreements are listed below.

**Disagreements between `docs/pantheon.md` and `terrain_religion.py`:**

1. *"Both modules are Python only, like the age API; the native core does not port them."* — directly contradicted by the 1:1 port mandate. Strike it.
2. *"A founding with reason `religious_schism` becomes a sect: patron and first manifest rival swapped"* — the doc omits the fallback. When the patron has **no** live rival (e.g. `god_first_hearth`, whose `rivals` list is empty — which is exactly the case `test_schism_founding_makes_one_sect` exercises), the code uses `chosen[1]`, the second worshipped god. When there is no `chosen[1]` either, no sect is written at all.
3. *"hollow nights kept as dread, or as vigils by Wild Umbral and Infernal cults"* — the code tests `chosen[:1]`, the **patron only**. A people whose second or third god is the Wild Umbral keeps dread. Is the patron-only test intended, or is `chosen[:1]` a leftover from an earlier shape? The slice is suspicious: `any(... for g in chosen[:1])` is a one-element loop written as a comprehension, which is how a `chosen` full-list version usually degrades.

**Apparently defective, a porter must decide:**

4. **`last_seen` nests without bound.** A god that stays `sleeping` across N resolutions accumulates `{'last_seen': {'last_seen': {…}}}` N deep, because `evidence` is rebuilt every call from the *previous* evidence. Over a long-lived world with many age turns, visitations and corruptions this grows the export monotonically. Almost certainly unintended. Decide: collapse to one level, store a scalar (`last_seen_age`), or reproduce the nesting faithfully.

5. **`facts['wars']` is computed and exported but read by nothing.** No `RULE_KINDS` entry consumes it and no cosmology `weight.fact` names it. Either a rule was planned and never written, or it is export-only telemetry. `test_sites_follow_ruins_and_the_moon` asserts its value, so it cannot simply be dropped without updating the test.

6. **`SURGE_BIRTH = 1.3` vs `terrain_astrology.SURGE = 1.6`.** `born_under_surge` uses a *different, lower* threshold than the almanac's own surge event, and applies it to a single day's Weave tide from `age['moon']['tide']['weave']` rather than to the almanac's surge list. Two thresholds for "a surge" in one world is at best confusing. Intentional (a ruin needs only an elevated tide, not a full surge) or drift?

7. **`resolve_sites` writes Weave cults when `god_weave` is merely `sleeping`** (`!= 'absent'`). Is a cult to a sleeping god intended? It is consistent with the "gods withdraw, cults remain" flavour of `cos_silent_heavens`, but it is worth an explicit ruling because the Red Field shrine branch has no status test at all.

8. **`terrain_corruption._reveal`'s effects are erased by the next `add_religion`.** `_reveal` forces rival gods to their Wild aspect and appends `roused_by`; `resolve_gods` builds a fresh record on the next resolution and carries neither (aspect is recomputed from `aspect_of`, and `roused_by` is simply not in the record literal). Meanwhile the hidden god's own `walking` status *does* survive, via `hidden_records`' status carry-forward. So after one age turn the world says a hidden god walks but the known pantheon has forgotten it roused them. Is the rousing meant to be momentary, or is this a carry-forward that was missed?

9. **The discarded patron draw.** When a conversion overrides the patron and the civilization has no ancestor god, `_draw(rng, weighted)` has already consumed a draw whose result is thrown away, shifting every subsequent secondary pick for that people. Harmless in Python (it is deterministic), but a clean rewrite has to choose whether to keep the dead draw. Recommend removing it and documenting the change.

10. **`ICARUS_CITY_WORKERS` does not apply here.** Neither settlement planner is involved; religion forks no processes and is single-threaded throughout. Noted only because the brief asked.

11. **Decision 020** ("the catalogue ships as data") is cited by the module docstring. Porting the catalogue to an embedded C++ table (recommended, given there is no JSON reader) reverses that decision for the C++ side. Someone should confirm that is acceptable, or accept the cost of a parser.

12. **`revision` bumping is unenforced.** `catalogue_identity()` exports `revision: 1` alongside the SHA-256, and nothing checks that the two move together. A catalogue edit that forgets the bump produces two catalogues claiming revision 1 with different hashes. The existing memory note "pinned files need a manifest revision" applies directly.

## Addenda from the audit

The reader above wrote the specification; a second reader went back to the source
to find what it had left out or got wrong. Both are kept: the omissions are
content the implementer needs, and the corrections say where the specification
above is not to be trusted.

### Corrections

- **Claimed:** "PIPELINE POSITION: **Registered as a pipeline stage** in `terrain_profile.py` under the comment 'Stage 13: creatures, threat, religion', third of three."
  **Actually:** terrain_profile.py is an opt-in wall-clock/RSS profiler. That tuple is STEP_TARGETS, headed by the comment "Named pipeline steps, as (module, attribute)... Order here is documentation only; wrapping is by name" — it selects what gets wrapped for timing, and nothing in it causes a stage to run. The stage-13 dispatch lives in terrain_history.py's `elif stage==13:` branch, and the age-turn re-run in `rebuild_tail`. An implementer who treated terrain_profile.py as the registry would mis-model the ladder and miss that `cfg` is passed at the call.

- **Claimed:** "READS: All reads go through `.get()` with defaults; none raise on a missing block."
  **Actually:** Many reads are direct subscripts and do raise. In this module alone: `result['config']['seed']` (KeyError if `config` exists without `seed`); `{s['population_profile'] for s in sites}` in world_facts; `facts['schools'][rule['school']]` in rule_holds; `i['intensity']` and `m['nod_degrees']` in aspect_of; `prior['status']` and `prior['aspect']` in resolve_gods; `grid[c['z']][c['x']]` in mean_potency; `entity_rules(civilization)`, `gods_by_id()[god_id]`, `by_id[god_id]['aspect_name']`, `by_id[rival]['name']`, `event['node']` in choose_faiths; `e['kind']`, `e['day']`, `e['school']` in feasts; `ruin['id']`, `ruin['node']`, `a['age']` in resolve_sites; `city['direction']`, `nest['direction']`, `nest['id']`, `nest['name']` in fantasy_nest_threats. The spec's own EDGE CASES section contradicts this sentence ("A hand-built or truncated previous block without those keys raises KeyError"). The guarantee is only that a missing top-level *block* defaults; a present-but-malformed record throws.

- **Claimed:** READS table: "`world['sky']['settlements']` | list | `facts['sky'] = len(...)`" listed under "`world['sky']['settlements']` — stage 9".
  **Actually:** `result['sky']` is written by terrain_society.add_world_society, which runs inside `civilization()` — stages 10-12, the same group as settlements — not stage 9. The field content is also phase-gated (`sky_sites if cfg.phase>=7 else []`).

- **Claimed:** choose_faiths pseudocode, step (h): "'epithet': (names.get(rival) or by_id[rival].name) + ' of the Schism'".
  **Actually:** The source is `names.get(rival, by_id[rival]['name']) + ' of the Schism'` — a dict default, not an `or` chain. The two differ when `names[rival]` exists and is falsey (empty string): the `or` form falls through to the catalogue name, the real code keeps the empty string. No shipped epithet is empty, so behaviour is identical today, but the spec's own C++ NOTES §10 calls out `or`-chain truthiness as a pitfall to model explicitly, so transcribing the wrong one here is a live trap.

### Missing — blocking

**Sovereign aspect names for the 16 gods that have no wild aspect**

`aspects.sovereign` is exported as the god record's `aspect_name` and is the middle term of `names[god_id] = epithets.get(civ) or by_id[god_id]['aspect_name'] or record['name']`. For all 16 single-aspect gods it is byte-identical to the god's `name`: god_first_hearth 'The First Hearth'; god_elder_star 'The Elder Star'; god_deep_forge 'The Deep Forge'; god_drowned_mother 'The Drowned Mother'; god_sky_court 'The Sky Court'; god_great_wyrm 'The Great Wyrm'; god_outer_unnamed 'The Unnamed Outside'; god_fallen_cities 'The Keeper of Fallen Cities'; god_wanderer 'The Wanderer'; god_hearth_harvest 'The Hearth and the Harvest'; god_forge_craft 'The Maker'; god_road_market 'The Coin and the Road'; god_crown_law 'The Crowned Scale'; god_grave_memory 'The Grave Keeper'; god_fortune_fate 'The Turning Wheel'; god_red_field 'The Red Field'. (The 9 two-aspect gods the spec does cover are the 8 school gods plus god_turning_moon 'The Pale Warden'/'The Turning Face'.)

### Missing — important

**The real stage-13 call site, with its arguments**

In terrain_history.py's stage ladder: `elif stage==13: add_nests(result,replace(cfg,phase=9)); add_threat_assessments(result,'beast_nests',spacing=cfg.settlement_spacing); from .terrain_religion import add_religion; add_religion(result,cfg)`. Because `cfg` is always supplied in the pipeline, `facts['lunar_influence']` in production always comes from `options(cfg)['lunar_influence']`; the `world['recipe']['resolved']['lunar_influence']` fallback is reachable only from direct/test calls such as `add_religion(world)`. terrain_profile.py, which the spec names, is the opt-in profiler's step table, not a registry.

**rebuild_tail's astrology guard on the age-transition re-run**

`rebuild_tail` does not call add_religion unconditionally. The tail is: `if religion:\n    if 'astrology' in result:\n        from .terrain_religion import add_religion\n        add_religion(result,cfg)\nelse:\n    refresh_astrology(result,cfg)`. So (a) a world with no `astrology` block silently skips the religion resolve at an age turn, and (b) the `religion=False` path (used by visitations) runs `refresh_astrology(result,cfg)` instead — the two are alternatives, not independent.

**facts['civilizations_with_cities'] is not filtered to known civilization ids**

`sorted({s['population_profile'] for s in sites})` takes every settlement site's profile unfiltered — unlike `facts['races']`, which guards with `if c in ids`. The spec's range note '≤ 12' is therefore an assumption, not a bound. Any profile string that is not one of the twelve entity civilizations reaches `entity_rules(civilization)['parent_race_id']` on the first line of the choose_faiths loop body and raises there, not at the fact-building step. A C++ port needs to decide explicitly: enforce membership when building the list, or reproduce the late failure.

### Missing — minor

**The full diaspora_reason value set**

From founding.py: `reasons=['exile','religious_schism','expedition']+(['magical_displacement'] if magic_enabled else [])`, chosen with `rng.choice(reasons)` per diaspora founding. The sect branch fires only on `'religious_schism'`, so roughly one diaspora event in three (one in four in a magic-enabled world) is a schism candidate. The spec names only the matching value, which leaves an implementer unable to build a founding-event fixture that exercises the sect path or to judge how often it fires.

**world['sky']['settlements'] is empty below phase 7**

terrain_society.add_world_society writes `result['sky']={'version':1,'islands':islands,'settlements':sky_sites if cfg.phase>=7 else [], ...}`. So on a short run `facts['sky']` is 0 even when sky islands exist, god_sky_court is absent, and cos_sea_and_sky loses its `sky` disjunct. The spec gives no phase condition.

**Exact expression for the sect rival lookup's list membership guard**

`rivals = [r for r in gods_by_id()[patron].get('rivals', []) if r in by_id and by_id[r]['status'] in ('manifest','walking')]`. The `r in by_id` half is what makes the later `by_id[rival]['name']` safe; the spec's pseudocode shows the status filter but writes the membership test only as a parenthetical, so an implementer transcribing the pseudocode could drop it and then index a missing key on the `chosen[1]` fallback path.
