# Port specification: 21-time-quests

Extracted from the Python `FantasyWorldGenerator` at commit `b67c99b` before that
repository was retired, then audited against the same source by a second reader.

**Source modules**

- `Sim/icarus_sim/terrain_time.py`
- `Sim/icarus_sim/terrain_quest_api.py`
- `Sim/icarus_sim/terrain_person_api.py`

**Estimated C++ size:** roughly 2600 lines, excluding tests.

## Audit: NOT YET IMPLEMENTABLE

This is a strong specification — one of the better ones I have audited. Parts A (the schedule), C (the quest lifecycle), the ordering-hazard list and the reasoning-preserved section are accurate and near-complete, and I verified the load-bearing claims against the source rather than trusting them: the cadence table and ranks, the half-open `crossings` window, `COALESCED`, the bisection in `longest_span`, `child_seed`'s exact SHA-256 form, `LEY_YEAR_LOW/HIGH` derived from `AGE_YEARS`, the union walk, `_turn_age_board`'s retire-and-rebuild at `index = floor(day)`, the first-writer-wins `_giver_index` against the list-collecting person-route `index`, the liveness tables and both depth traps, `STATE_KEYS` containing `world_clock` and `quests`, and the `wells.json` difficulty rubric all check out. I set implementable=false for four reasons, none of them those. (1) Two functions are transcribed without their type guards: `_apply_surge` and `_dedupe_warnings` both return early in the source and the spec's pseudocode does not — a port built from the spec crashes on every tick of a `magic_enabled == 0` world, which the spec itself documents as supported, and fabricates an empty `warnings` key on worlds that have none. (2) The entire malformed-world error surface is absent: `validate_time_world` wraps its body in `except (KeyError, TypeError, IndexError, OverflowError)` and rewrites those into a single `wrong_type` whose `received` is the exception class name; an implementer reaches that point and must guess. (3) `edit_network` requires `intensity` on a node/line edit and raises bare `ValueError` for all of its refusals — the only unenveloped refusals at this boundary — and the spec says neither, so a port would both accept a request the source refuses and wrap a refusal the source leaves naked. (4) The spec's all-or-nothing guarantee has a hole it does not record: with `commit: true`, an age span whose remainder exceeds `MAX_TICK_STEPS` advances the ages and then hands the remainder to a recursion that defaults `commit=False`, so the remainder is silently blocked and the returned world's `world_clock.day` contradicts its own `time_advance.to_day`. Beyond those, the omissions are mostly published output strings the spec elided as `<sentence>` (the two cost bases, `peak_mb_precision`, every `missing_block` message, every `over_capacity` noun) plus the five-item truncation in `refused_by_world`'s candidate list — all emitted document content in a subsystem whose own contract is byte-level, so they should be carried verbatim rather than paraphrased. The six errors I list are individually small, but the nest-id `get(k, default)`-versus-`or` and the `body.get(k, [])`-versus-`or []` ones are exactly the near-miss class this codebase keeps producing: a substitution that answers a slightly different question and stays invisible until a null shows up.

**4 blocking gap(s)** were found and are recorded in the addenda below.

## Purpose

The clock, the tick, the quest lifecycle and the person-liveness channel: the three stateless request APIs that move a *finished* world forward through simulated time and let a playing session write back the two facts only it can know.

`advance_time_request` (terrain_time.py) is the world's only clock. Before it existed there were seven request-shaped mutators (advance_age, corruption, cleanse, visitation, nomad, lunar, patch) and none of them knew what time it was, so every consequence of elapsed time had to be smuggled into an age boundary: "a swarm that should erupt in a bad year erupts in every year *because there are no years*". It takes a world document plus an elapsed span in seconds/days/years, decides which periodic passes cross that span, runs them in absolute-time order, drifts the ley field, recomputes the lunar surge, moves the quest board, and returns a new world. A span of one age (1,800,000 days) or longer is not a tick at all: it is delegated wholesale to `advance_age_request`.

`quest_request` (terrain_quest_api.py) writes `taken` and `abandoned` on a quest — the two transitions a *player* makes, which the world has no business inventing. `person_state_request` (terrain_person_api.py) records that a person is gone or back, in whichever of five vocabularies the block that holds them uses.

A quest in this world is **anchor + verb + difficulty 1..5** and nothing else. The generator emits no loot, no XP, no treasure table; `difficulty` is the one number a consuming game prices, and pricing it is that game's job. The world decides whether an offer still *stands*; it never decides whether a player succeeded, because an outcome the world invented would be an outcome the player did not play.

## Where it sits in the pipeline

**Not part of generation.** None of these three runs during `generate`. They run only when a caller asks, against a world generation has already finished and persisted.

Must already exist before `advance_time_request` runs (this is the literal gate, `validate_time_world`):
- `world` is a dict; `Config(**world['config'])` constructs; `cfg.world_recipe == 3` and `cfg.phase >= 13` (recipe 3 through the creatures stage).
- `world['terrain']['version'] == 6`, `world['generator_version'] == 16`, `world['recipe']['version'] == 3`.
- `world['astrology']` is a dict with `version == 1` (the moon contract — without a sky there is no calendar and no tide).

It deliberately does **not** call `validate_age_world`. That answers a different question — whether a world can advance an *age* — and carries extra contract requirements including its own grid ceiling. A world that cannot turn an age can still move its encounters and its quests, so the tick refuses only what it genuinely cannot do, and the age band asks the real gate (`age_gate`) rather than restating it.

Blocks it reads below the age band: `magic.networks`, `settlements.sites`, `beast_nests.sites`, `heroes` (`people`, `dreads`, `quest_hooks`), `npcs.people`, `villains.people`, `threat_assessments`, `history.ages`, `history.operations`, `timing_ms`, `effective_config.globe_radius` (via the villain outlook), `spacing_m`.

Downstream of it:
- `POST /world/quests` (read surface) reads the `quests` block this writes, and refuses a world that has never been advanced — no quests and no quest lifecycle are different facts.
- `quest_request` and `person_state_request` both call `validate_time_world` themselves, deliberately asking the *same* gate the tick asks: a weaker question here would admit a world `advance-time` refuses a moment later.
- `quest_request` additionally requires a `quests` block to already exist, i.e. the world must have been ticked at least one day.
- `world_clock` and `quests` are registered in `terrain_history.STATE_KEYS`. That registration is load-bearing and silent when wrong: `materialize_stage` rebuilds an earlier stage from every key *not* in that tuple, so an unregistered block is reported at every stage as though it had always been there rather than raising. `quest_actions`, `person_state` and `time_advance` are correctly **not** registered — they are reports of a call, not state.
- In C++ the new equivalents must be registered wherever the stage-materialisation analogue lives, or they leak backwards through stage history.

## Inputs

**Request body of `advance_time_request`** — allowed keys exactly `{api_version, world, elapsed, leyline_edits, resolutions, commit, moon_day}`; any other key raises `unknown_field` naming `sorted(extra)[0]`.
- `api_version`: must be *exactly* int 1 (`type(x) is not int` — a bool is rejected). Else `unsupported_api`, `retry: false`.
- `elapsed`: object naming **exactly one** unit from `('seconds','days','years')`. Value must be int or float (not bool), finite, `>= 0`. seconds → `value / (24*3600)`; years → `value * 360`; days → `float(value)`. Wrong shape → `wrong_type`; wrong count of keys → `cross_field`; unknown unit → `invalid_choice`.
- `moon_day`: optional; if present must be `type(x) is int` and `>= 0`. Passed through to `advance_age_request` unchanged.
- `leyline_edits`: list, at most **128** (`over_capacity`). Each edit is a dict whose keys are a subset of `('school','node_id','line_id','intensity','new_node')`. `new_node` and `intensity` together → `cross_field`. `school` must name a key of `result.magic.networks` → else `invalid_choice` listing `sorted(networks)`. Edits require `cfg.magic_enabled`, else `refused_by_world('leyline_edits', len(edits), ...)`.
- `resolutions`: list, at most **1024** (`over_capacity`). Each item a dict of exactly `{quest_id, outcome}`; `outcome` ∈ `{completed, failed, abandoned}`.
- `commit`: `type(x) is bool` exactly. Default `False`. It turns the two *reported* refusals (age gate cost, work ceiling) into execution.

**Request body of `quest_request`** — keys exactly `('api_version','world','actions')`. `actions` default `[]`, list, at most **1024**. Each item a dict with keys a subset of `('quest_id','action','day')`; `quest_id` a non-empty str; `action` ∈ `('take','abandon')`; `day` either `None` (meaning "whatever the world clock says") or a non-bool int/float that is finite and `>= 0`, normalised to float.

**Request body of `person_state_request`** — keys exactly `('api_version','world','changes')`. `changes` default `[]`, list, at most **1024**. Each item must carry **all four** of `('uid','status','reason','day')` — a missing one raises `unknown_field(missing[0], FIELDS, noun='person change field')`, which is an odd shape for a missing field but is the published behaviour. `uid` a non-empty str; `status` ∈ `('present','gone')`; `reason` a non-empty-after-strip str of at most **200** characters (bounded because it is copied verbatim into the persisted `history.operations`, and an unbounded string there is an unbounded world document); `day` non-bool finite number `>= 0`, normalised to float.

**World fields read.** `config.size` (float, for cost extrapolation); `config.seed`; `config.magic_enabled`; `timing_ms.age_advance_total` (ms); `history.ages` (list — its length is the age ordinal); `history.operations` (list); `magic.networks[school].{strength, nodes[], edges[]}` where each node/edge has `id` (str) and `intensity` (float, 0..4); `astrology.moon` (periods/offsets/tilt, handed to `tide`); `settlements.sites[].uid`; `beast_nests.sites[].id`; `heroes.status` (block-level `'ok'`/`'failed'`), `heroes.quest_hooks[]`, `heroes.people[].uid/.status`, `heroes.dreads[].uid/.status`; `npcs.status`, `npcs.people[].uid/.status`; `villains.people[].status`, `villains.outlook`; `build_stages` (list, shared not copied).

**Quest hook record** (written by `hero_generator.hooks.quest_hooks`, read here): `{hook_id, giver_uid, offered_from_face, stated_purpose, actual_effect, unwitting, target, verb, difficulty, target_node, unsited_reason}`. `actual_effect` is one of `{kind: 'ruin'|'relic'|'city'|'hamlet'|'fortress'|'port'|'shrine'|'college'|'nest'|'ley_node', ...}` carrying `uid` (or `nest_id` for a nest, or `node_id`+`school`+`intensity_delta` for a ley node) and, except for ley nodes, an `action`.

## Outputs

**`world_clock`** — `{version: 1, day: float, age: int, epoch_day: 0.0, derived_from: str}`. `day` is a real number of days since founding year 0 on the moon's calendar (24-hour days, 30-day months, 12-month years), so a tick and `POST /world/moon` cannot disagree about what time it is. `age` is `len(history.ages)`. `derived_from` is `'founding.end_year'` or `'epoch'` for a world that had no clock; it is *not* refreshed once a clock exists. Not optional once any advance has run.

**`quests`** — `{version: 1, quests: [...], log: [...]}`.
Each quest row, keyed on **`quest_id`** (which equals the `hook_id` it was opened from — note the key field is `quest_id`, not `id` or `uid`):
```
{quest_id: str,          # == hook_id; names ONE age's offer only
 giver_uid: str|null,    # hook.giver_uid
 anchor: str|null,       # hook.target, falling back to actual_effect.nest_id
 verb:   str|null,       # hook.verb,   falling back to actual_effect.action
 stated_purpose: str|null,
 unwitting: bool,        # hook.unwitting, default False
 state: 'offered'|'taken'|'expired'|'completed'|'failed'|'abandoned',
 offered_day: float,
 expires_day: float,     # offered_day + U(180,1080)
 closed_day: float|null,
 closed_reason: str|null}
```
**Difficulty is deliberately NOT emitted here.** It belongs to the quest generator and lives on `heroes.quest_hooks[].hook_id`; a consumer wanting a priced offer joins `quests.quests[].quest_id` to `heroes.quest_hooks[].hook_id`.

Each log entry: `{day: float, quest_id: str, event: str}`, where `event` is a closing reason, an outcome (`completed`/`failed`/`abandoned`), `'taken'`, or `'ley_applied'` (which additionally carries `school`, `node_id`, `intensity`).

`closed_reason` vocabulary — published, and `read-quests.schema.json` constrains it to exactly these plus `null`:
`('expired', 'giver_gone', 'target_gone', 'hook_gone', 'age_turned', 'resolved', 'abandoned')`.
`hook_gone` and `age_turned` are the pair that is easiest to collapse and **must not be**. `hook_gone` says *this hook* left `heroes.quest_hooks` while the world around it stood. `age_turned` says the world moved on: five thousand years passed, the cast was regenerated, and everyone who could have offered or populated the quest is gone. A consumer that cannot tell those apart cannot tell a player whether their quest went away or their age did. `abandoned` is written by the quest route, not by the tick; it is in the vocabulary because the vocabulary is the block's.

Note the state collapse: *every* world-side closure sets `state = 'expired'` regardless of reason. `closed_reason` is the only discriminator.

**`time_advance`** — the report for the call just made. On a committed tick it is `schedule.plan(now, days)` (= `{band, elapsed_days, from_day, to_day, ages, steps, by_cadence}`) updated with `{api_version: 1, committed: true, leyline_edits: int, resolutions: int, leylines_evaluated: bool}`. On the age band it is `{api_version, band:'age', elapsed_days, from_day, ages, cost_estimate, committed, note, [blocked], [remainder], [age_days], [to_day], [age]}`. On a reported refusal it carries `committed: false` and `blocked: <failure document>`.

**`time_api_version`** — int 1, top level.

**`history.operations`** — one appended entry **per call that advances the world**. Tick: `{api_version:1, kind:'time', band, elapsed_days, from_day, to_day, steps, leyline_edits:[...deep copy...], resolutions:[...deep copy...]}`. Age band: `{api_version:1, kind:'time', band:'age', elapsed_days: age_days, ages, from_day, to_day}`. Quest route: `{api_version:1, kind:'quest', actions:[{quest_id, action, day}...]}`. Person route: `{api_version:1, kind:'person', changes:[...verbatim normalised changes that applied...]}`. Estimates and refusals record nothing.

**`history.replay`** — set by `advance_time_request` only, to the fixed sentence *"Persist this returned world for subsequent calls. Config reproduces genesis; operation history records subsequent advances, player leyline edits and quest resolutions."* The other two routes leave it exactly as found; rewording it would make the key flip between two strings depending on which call ran last, which is churn in the bytes of a document whose byte-reproducibility is a contract.

**`magic.networks[*].intensity`** on nodes and edges — drifted, clamped to `min(4., ...)`, one-year cadence. **`magic.networks[*].surged_strength`** = `strength * tide_school(day)` — written only for schools present in the tide factors (a hidden school takes no surge and carries **no** `surged_strength` key at all). **`magic.lunar_surge`** = `{day, factors: dict, formula: 'surged_strength = strength * tide_school(day)'}`.

**`villains.outlook`** — refreshed each `villain_outlook` cadence crossing. The villain **cast** is never touched by a tick.

**Blocks the cadence passes rewrite** (this capability is a *new writer* of blocks other capabilities own): `encounters`, `beast_movements`, `nomad_routes`, `nomads`, `seasonal_food`, and `warnings` (deduped).

**`quest_actions`** — `{api_version:1, day: float, applied: int, actions: [{quest_id, action, day, state}]}` where `state` ∈ `{taken, abandoned}`. Overwritten by the next such call; describes no earlier one.

**`person_state`** — `{api_version:1, applied: int, changes: [{uid, block, from_status, to_status, token, day, reason, changed}], notes: [str]}`. `block` ∈ `{heroes, dreads, npcs, villains}`; `from_status`/`to_status` ∈ `{present, gone}`; `token` ∈ `{living, legend, alive, dead, fallen}`.

**`heroes.people[].status` / `heroes.dreads[].status` / `npcs.people[].status` / `villains.people[].status`** — exactly one field on one record per applied change, in that block's own word.

**`timing_ms.time_advance_total`** — milliseconds, wall clock. Written by the tick only; neither the quest route nor the person route writes a timing, because a document whose byte-reproducibility is a contract should not gain a fresh wall-clock value because a party accepted a quest.

## Algorithm

════════════════════════════════════════════════════════
PART A — THE SCHEDULE (pure; no world, no RNG, no clock)
════════════════════════════════════════════════════════

**The binding invariant: the RNG domain is keyed by simulated time, never by tick ordinal or call count.** Every cadence fires on an absolute step index `floor(day / period)` measured from the epoch, so a call for elapsed E executes exactly the indices falling in `(now, now+E]` and nothing else. No draw is made per call and no index depends on how the span was cut. That is what makes `advance(30y)` and `advance(15y)` twice identical.

The failure mode this shape exists to prevent is a seed derived from tick ordinal. It is invisible until someone chunks differently, and by then a saved world has been advanced across it.

A1. CALENDAR
```
HOURS_PER_DAY   = 24.
DAYS_PER_MONTH  = 30.
MONTHS_PER_YEAR = 12.
DAYS_PER_YEAR   = 360.
SEASON_DAYS     = 90.
AGE_YEARS       = 5000.
AGE_DAYS        = 1'800'000.
```

A2. CADENCES — ordered, and the order is load-bearing twice over: it breaks ties between cadences firing on the same day, **and it is dependency order, producers before consumers.**
```
rank  name              period (days)
 0    quests            1.
 1    ley_drift         360.
 2    seasonal_food     90.
 3    nomads            360.
 4    nomad_routes      30.
 5    beast_movements   30.
 6    encounters        30.
 7    nomad_effects     360.
 8    villain_outlook   360.
```
`add_nomad_routes` reads and writes the block `add_nomads` replaced; `add_encounters` reads both `nomads` and `beast_movements`. Ordering them any other way would build an index of bands that no longer exist. `nomad_effects` runs last for the same reason: `add_nomads` replaces its block wholesale, discarding `nomads.effects` and orphaning the cultist ley deepening, raid pressure, ridden roads and survivor camps the *previous* population left behind, so the write-backs must re-run after routes exist to ride.

A3. COALESCED = `{seasonal_food, nomads, nomad_routes, beast_movements, encounters, nomad_effects, villain_outlook}` — every pass that **replaces its block wholesale**. Only the *last* crossing inside a span is executed, because every earlier one would be overwritten by it. Thirty years runs one `add_encounters` instead of 360. This is sound only because (a) a shorter period divides a longer one, so a consumer's last crossing is never earlier than its producer's, and (b) none of them reads the block it writes. **A pass that accumulates must never be listed**: `quests` carries expiry forward and `ley_drift` is a compounding random walk, and neither is.

A4. `band(days)`:
```
days < 1.       -> INSTANT
days < 90.      -> DAY
days < 720.     -> SEASON
days < 1800000. -> LONG
else            -> AGE
```
**A band names how big a span is. It does NOT decide which cadences may run.** It used to — `steps()` filtered by `BAND_CADENCES[band(days)]` — and that was a silent, *permanent* data-loss bug rather than a conservative one: the window is half-open at the start, so a crossing skipped because *this* call was too short is never revisited by any later call. A game ticking a month at a time would never once run ley drift, nomad reclassification, seasonal food or the villain outlook — not deferred, skipped — and a game ticking by the second would advance nothing but its clock, forever. It also made the central invariant false: the same span cut across a band boundary produced a different world. `BAND_CADENCES` survives only as a label for the report and as documentation of what a span of each size typically touches.

A5. `crossings(name, now, days) -> range`:
```
period = CADENCE_PERIOD[name]
first  = floor(now / period) + 1
last   = floor((now + days) / period)
return [first .. last]          # empty when last < first
```
Half-open at the start so a step is never executed twice by two adjacent calls; closed at the end so a step landing exactly on the boundary is executed once.

A6. `steps(now, days) -> [(day, name, index)]`:
```
if days < 0: raise
out = []
for name in every cadence:                 # order: CADENCES order
    idx = crossings(name, now, days)
    if name in COALESCED: idx = last element only (or empty)
    for i in idx: out.append((i * period, name, i))
sort by (day, CADENCE_RANK[name], index)
```
Note `day` is `index * period`, the absolute cadence boundary — **not** `now + something`.

A7. `by_cadence(now, days) -> {name: count}` — `len(range)` is O(1), so a 1.8-million-step span is priced without allocating a tuple per step. Coalesced cadences contribute `min(count, 1)`. Cadences contributing zero are absent. Returned as `dict(sorted(items))`.

A8. `work(now, days) = sum(by_cadence(now, days).values())`. It must answer exactly the question `steps` answers; the guard test asserts it against the enumerator itself rather than against a formula, because a counter that answers a slightly different question than the thing it stands in for is the near-miss that keeps being mistaken here for an answer.

A9. `longest_span(now, limit) -> float days` — `work` is non-decreasing in `days` (a longer window contains every crossing a shorter one did), so this is a bisection over **whole days** in `[0, int(AGE_DAYS)]`, rounded down:
```
if work(now, 1800000.) <= limit: return 1800000.
low, high = 0, 1800000
while low < high:
    mid = (low + high + 1) // 2
    if work(now, float(mid)) <= limit: low = mid
    else: high = mid - 1
return float(low)
```
Rounded down so a caller that sends the value back is not refused again by a fraction of a day.

A10. `plan(now, days)` = `{band, elapsed_days: days, from_day: now, to_day: now+days, ages: ages_for(days) if band==AGE else 0, steps: sum(counts), by_cadence: counts}`.

A11. `ages_for(days) = int(days // AGE_DAYS)`.

════════════════════════════════════════════════════════
PART B — `advance_time_request(body)`
════════════════════════════════════════════════════════

B1. Validate the body (see *reads*). Everything judgeable without the world first.
B2. `cfg = validate_time_world(world)`.
B3. If `edits` and not `cfg.magic_enabled` → `refused_by_world`.
B4. `started = perf_counter()`.
B5. `now = clock(world)`:
```
existing = world.get('world_clock')
if isinstance(existing, dict) and existing['version'] == 1: return dict(existing)
year, source = reported_year(world)         # settlements.founding.end_year, else (0,'epoch')
return {version:1, day: float(year)*360., age: len(history.ages or []),
        epoch_day: 0., derived_from: source}
```
A world with no clock is read at its founding year, which is the same day the age lottery already samples, so adopting a clock does not move a world's position in time.
B6. `which = band(days)`.

B7. **WORK CEILING, asked before anything is copied and before the age branch.**
```
tick_days  = days - ages_for(days)*AGE_DAYS   if which == AGE  else days
tick_from  = now.day + (days - tick_days)
tick_steps = work(tick_from, tick_days)
if tick_steps > MAX_TICK_STEPS and not commit:
    document = over_capacity('elapsed', tick_steps, 200000, 'cadence steps per request').document()
    fits = longest_span(now.day, 200000)          # measured from the CALLER'S clock
    document.suggestion = {kind:'clamp', value:{days: fits},
                           note:'the longest span from this world_clock that runs at most 200000 steps'}
    document.cost_estimate = tick_cost_estimate(world, tick_days, tick_steps)
    out = deep copy of world
    out.world_clock = now
    out.time_advance = {api_version:1, band:which, elapsed_days:days, from_day:now.day,
                        to_day:now.day+days, steps:tick_steps,
                        ages: ages_for(days) if which==AGE else 0,
                        committed:false, blocked:document,
                        note:'Estimate only. Send commit: true to run it anyway, or ask for at most %g day(s).' % fits}
    return out
```
Only the remainder after whole ages is priced, because the ages themselves are delegated and are not ticked — pricing them as cadence steps would refuse every age advance for work it never does. It is asked *before* the age branch so a span of ages *plus* an unaffordable remainder is reported whole instead of advancing its ages and then baulking. `fits` is measured from the caller's own clock rather than from `tick_from`, so sending the value straight back is guaranteed to be accepted; a suggestion computed from the day the ages would have ended is a suggestion about a request the caller cannot make. The suggestion is *replaced* rather than taken from `over_capacity`, which would otherwise tell a caller to clamp `elapsed` to a step count — a unit this API does not accept.

B8. **AGE BAND** (`which == AGE`):
```
ages = ages_for(days)
if ages > 50:  return world-copy with time_advance.blocked = over_capacity(...)+cost_estimate, note 'Ask for fewer ages, or send several requests.'
estimate = cost_estimate(world, ages)
blocked  = age_gate(world)
report   = {api_version:1, band:'age', elapsed_days:days, from_day:now.day, ages:ages,
            cost_estimate:estimate, committed:false,
            note:'A span of one age or longer is an age advancement, not a tick.'}
if blocked:      return world-copy with report+blocked
if not commit:   return world-copy with report, note 'Estimate only. Send commit: true to advance N age(s), or ask for a shorter span to tick instead.'
advanced   = _advance_ages(world, ages, moon_day)      # chunks of <=10 through advance_age_request
age_days   = ages * AGE_DAYS
clock_after      = clock(advanced)
clock_after.day  = now.day + age_days
clock_after.age  = len(advanced.history.ages)
advanced.world_clock = clock_after
_turn_age_board(advanced, cfg, clock_after.day)        # BEFORE the remainder ticks
advanced.history.operations += [{api_version:1, kind:'time', band:'age',
                                 elapsed_days:age_days, ages:ages,
                                 from_day:now.day, to_day:clock_after.day}]
remainder = days - age_days
if remainder > 0 or edits or resolutions:
    advanced = advance_time_request({api_version:1, world:advanced,
                                     elapsed:{days:remainder},
                                     leyline_edits:edits, resolutions:resolutions})
    report.remainder = advanced.time_advance
else:
    _apply_surge(advanced, clock_after.day)
report.committed = true; report.elapsed_days = days; report.age_days = age_days
report.to_day = now.day + days; report.age = clock_after.age
advanced.time_advance = report
return advanced
```
Dropping the remainder made `advance(150y)` differ from `advance(100y)` then `advance(50y)` — a counterexample to the determinism invariant that needed no band table to produce — and left the operation log claiming an elapsed the clock never moved. Edits and resolutions ride the same recursive request rather than being validated and discarded. Note `MAX_AGES_PER_REQUEST` is kept *beside* `MAX_TICK_STEPS` rather than replaced by it, because a work ceiling and an ages ceiling refuse different things: one prices a tick, the other bounds a loop of whole-world rebuilds this module does not execute itself.

B9. **INSTANT SHORT-CIRCUIT.**
```
executed = steps(now.day, days)
if not executed and not edits and not resolutions:  return _instant(world, now, days, started)
```
Keyed on "this span crosses nothing", **not** on "this span is short". The old test was `band == INSTANT`, which let a sub-day call carrying a resolution fall through to the full path and move the ley field — the opposite of what the band promised.

`_instant` writes four keys and copies four keys:
```
end_day = now.day + days
result = shallow copy of world
if magic is a dict: result.magic = {**magic, 'networks': {name: dict(net) for ...}}   # one level deep
_apply_surge(result, end_day)
result.world_clock = dict(now) with day=end_day, age=len(history.ages or [])
history = dict(world.history or {})
history.operations = list(...) + [{api_version:1, kind:'time', band:band(days),
                                   elapsed_days:days, from_day:now.day, to_day:end_day,
                                   steps:0, leyline_edits:[], resolutions:[]}]
history.replay = <the fixed sentence>
if days > 0: result.history = history        # days == 0 records NOTHING
result.time_advance = plan(now.day, days) + {api_version:1, committed:true,
                                             leyline_edits:0, resolutions:0,
                                             leylines_evaluated:false}
result.time_api_version = 1
result.timing_ms = {**(world.timing_ms or {}), 'time_advance_total': elapsed_ms}
return result
```
A full deep copy costs the better part of two seconds on a size-33 world and grows with the square of the grid; a game asking what time it is every frame cannot pay that, and paying it to change a clock and twelve floats would make the band's whole point — that a moment is cheap because nothing can happen in it — a fiction. Structural sharing is safe in the one direction that matters: nothing below is mutated in place, so the caller's world is unchanged. An `elapsed` of exactly 0 appends no operation, so "what time is it" is free.

B10. **THE PRIVATE COPY.** Every other span:
```
result = deepcopy({k:v for k,v in world.items() if k != 'build_stages'})
if 'build_stages' in world: result.build_stages = list(world.build_stages)   # shared, not copied
_timing(result)          # result.timing_ms.setdefault, timing_ms['total'] = 0.
```
**Failure is all-or-nothing.** Every pass runs against the private copy and the caller's object is never touched; a raise leaves no half-advanced world anywhere. That guarantee is nearly free in a world-in/world-out shape and expensive to retrofit once anything mutates in place, so it is taken here rather than later.

`_timing` exists because the CLI strips `timing_ms` recursively to keep an exported world byte-reproducible, and several passes then write `result['timing_ms'][...]` unguarded, so a persisted world raises `KeyError` before doing any work.

B11. **LEY EDITS**, in caller order:
```
for edit in edits:
    validate shape; networks = result.magic.networks
    networks[school] = edit_network(networks[school], **{k:v for k,v in edit if k != 'school'})
ley_touched = bool(edits)
```
`edit_network` is copy-on-write: deep copies the network, requires exactly one of `node_id`/`line_id`/`new_node`, validates a `new_node` as `{id, direction, intensity}` with a unique non-empty id and a unit direction (`|sum(v*v) - 1| <= 1e-6`), and requires `0 <= intensity <= 4` finite.

B12. `givers = _giver_index(result)` — built **once** rather than on each of a long span's daily quest steps, because a tick never changes who exists (the cast moves at an age boundary and nowhere else).
```
index = {}
for (key, block, listing) in [('heroes','heroes','people'),
                              ('heroes','dreads','dreads'),
                              ('npcs','npcs','people')]:
    container = result.get(key)
    if not dict or container.status != 'ok': continue      # a failed package carries no people
    for record in container[listing] or []:
        uid = record.get('uid')
        if uid is not None and uid not in index: index[uid] = (record, block)   # FIRST WRITER WINS
```
Villains are not givers.

B13. **RESOLUTIONS FIRST.** Resolutions describe what the player did *before* this span, so they are applied before it runs. Applying them at the end meant the world could expire a quest during the very span the player spent completing it, and then refuse the completion.
```
if resolutions:
    if 'quests' not in result: _quest_step(result, cfg, now.day, 0, liveness, givers)
    if _apply_resolutions(result, cfg, now.day, resolutions): ley_touched = True
```

B14. **THE STEP LOOP** — `for (day, name, index) in executed:`
```
name == 'quests'          -> _quest_step(result, cfg, day, index, liveness, givers)
name == 'ley_drift'       -> ley_touched = _ley_drift(result, cfg, index) or ley_touched
name == 'villain_outlook' -> _villain_outlook(result, cfg)
otherwise                 -> module, func = CADENCE_PASSES[name]
                             stepped = replace(cfg, seed=child_seed(cfg.seed, 'time-'+name, index))
                             getattr(import_module(module), func)(result, stepped)
                             _dedupe_warnings(result)
                             if name in {'nomad_effects'}: ley_touched = True
```
`CADENCE_PASSES = {encounters: terrain_encounters.add_encounters, beast_movements: terrain_beast_movement.add_beast_movements, nomad_routes: terrain_nomad_routes.add_nomad_routes, nomads: terrain_nomads.add_nomads, nomad_effects: terrain_nomad_effects.apply_nomad_effects, seasonal_food: terrain_seasons.add_seasonal_food}` — every one takes `(result, cfg)`.

A pass is a deterministic function of the world and the seed, so a time-derived seed is what makes it move at all. Keyed on the **absolute step index**, never on the call, so chunking cannot change which world it draws.

`ley_drift` must set `ley_touched` only when it *actually moved the field*: setting the flag unconditionally sent a magicless world into `evaluate_networks`, which indexes `result['magic']` and died on a bare `KeyError` rather than any refusal this boundary knows how to make.

B15. **CLOSE OUT.**
```
end_day = now.day + days
if ley_touched: evaluate_networks(result, cfg)          # the expensive globe pass, at most once per request
_apply_surge(result, end_day)                            # closed form, free either way
result.world_clock = dict(now) with day=end_day, age=len(result.history.ages or [])
result.time_advance = plan(now.day, days) + {api_version:1, committed:true,
                                             leyline_edits:len(edits), resolutions:len(resolutions),
                                             leylines_evaluated:ley_touched}
result.time_api_version = 1
result.history.operations += [{api_version:1, kind:'time', band:which, elapsed_days:days,
                               from_day:now.day, to_day:end_day, steps:len(executed),
                               leyline_edits: deepcopy(edits), resolutions: deepcopy(resolutions)}]
result.history.replay = <the fixed sentence>
result.timing_ms.time_advance_total = elapsed_ms
return result
```

════════════════════════════════════════════════════════
PART C — THE QUEST LIFECYCLE
════════════════════════════════════════════════════════

C1. `_quest_step(result, cfg, day, index, liveness_of, givers)` — **"open what the world now offers, and close what it no longer does."**
```
heroes = result.get('heroes')
hooks  = heroes.quest_hooks or []   if heroes is a dict and heroes.status == 'ok'   else []
block  = result.setdefault('quests', {version:1, quests:[], log:[]})
block.version = 1
tracked = {q.quest_id: q for q in block.quests}
by_hook = {h.hook_id: h for h in hooks}
rng     = Random(child_seed(cfg.seed, 'time-quests', index))

# Walk the UNION: a hook the world no longer offers still has a quest to close.
for hook_id in list(by_hook) + [q for q in tracked if q not in by_hook]:
    hook  = by_hook.get(hook_id)
    quest = tracked.get(hook_id)

    if hook is None:                                   # the hook vanished
        if quest and quest.state in ('offered','taken'):
            quest.state = 'expired'; quest.closed_day = day
            quest.closed_reason = 'hook_gone'
            block.log.append({day, quest_id, event:'hook_gone'})
        continue

    if quest is None:                                  # OPEN A NEW OFFER
        quest = {quest_id: hook.hook_id,
                 giver_uid: hook.giver_uid,
                 anchor: hook.target      or (hook.actual_effect or {}).nest_id,
                 verb:   hook.verb        or (hook.actual_effect or {}).action,
                 stated_purpose: hook.stated_purpose,
                 unwitting: hook.unwitting or False,
                 state: 'offered', offered_day: day,
                 expires_day: day + rng.uniform(180., 1080.),
                 closed_day: None, closed_reason: None}
        tracked[quest_id] = quest; block.quests.append(quest)

    if quest.state not in ('offered','taken'): continue

    entry = givers.get(quest.giver_uid)
    reason = None
    if entry is None or liveness_of(*entry) == 'gone':      reason = 'giver_gone'
    elif not _target_present(result, hook.actual_effect or {}): reason = 'target_gone'
    elif quest.state == 'offered' and day > quest.expires_day: reason = 'expired'
    if reason:
        quest.state = 'expired'; quest.closed_day = day; quest.closed_reason = reason
        block.log.append({day, quest_id, event: reason})

block.quests.sort(key = quest_id)
```
The union walk matters: walking hooks alone left an open quest for every hook that vanished, immortal and growing. The expiry test is guarded on `state == 'offered'` — **a taken quest does not expire on its offer window**, which is the whole reason the quest route must refuse a late take.

Both anchor/verb fallbacks exist for a world generated before `heroes` version 2, the version that made the hook answer for itself. A pre-2 `target` was uid-or-node_id only, so a nest hook — which carries `nest_id` — anchored to null and named nothing at all; a pre-2 ley hook had no `action` on its effect, so its verb read as null however the board was queried. A ley effect carries a school and a delta and never an action, so **every ley quest of a pre-2 world reads as a verbless offer**, and that is the only way `verb` can still be null.

C2. `_target_present(result, effect)`:
```
kind == 'city' -> any(site.uid == effect.uid for site in settlements.sites)
kind == 'nest' -> any(site.id  == (effect.nest_id or effect.uid) for site in beast_nests.sites)
otherwise      -> True
```
Only the kinds with an id this module can resolve are tested. An unresolvable kind reads as present: **a quest wrongly left open is a smaller lie than a quest silently reaped because this module did not recognise its target.** Note the mismatched key fields — settlements join on `uid`, nests join on `id`.

C3. `_turn_age_board(result, cfg, day)` — **retire the board at an age turn and rebuild it from the new age's hooks.**
```
block = result.get('quests');  if not a dict: return
log = block.setdefault('log', [])
for quest in block.quests or []:
    if quest.state in ('offered','taken'):
        quest.state='expired'; quest.closed_day=day; quest.closed_reason='age_turned'
        log.append({day, quest_id, event:'age_turned'})
block.quests = []                                  # the previous age's rows are RETIRED, not kept
_quest_step(result, cfg, day, int(floor(day)), liveness, _giver_index(result))
```
**It does not depend on the id disappearing, and that is the whole point.** That is how reaping used to happen, and it was only mostly true. The regenerated cast re-mints positional uids — `hero-reeve-hamlet-node-<n>`, `hero-sovereign-<city uid>` — as the same string, so `by_hook.get(hook_id)` found a *different* hook wearing the old id and the quest carried on: the old age's `anchor`, `verb` and `stated_purpose`, the new age's target check, and a `giver_uid` naming someone who no longer exists. **Measured 2026-09-21 with all 49 open quests taken and one age advanced: 39 closed, 10 survived as that chimera.**

The previous age's rows are retired *with* it rather than kept beside the new ones, for the same reason rather than for tidiness: a retained closed row whose id the new cast re-mints could not be told apart from the offer that id now names, and which rows survived would again be decided by an accident of uid minting. `quests.log` keeps every closure with its day and its reason, so why a quest ended outlives the board it stood on.

Only a world that already carries a board gets one back — an age turn *continues* a lifecycle rather than starting one.

The index handed to the rebuild is `int(floor(day))`, keyed on the day the age turned, like every other step in this module: the quests cadence has a period of one day, so the absolute index of the sweep at `day` is `floor(day)`. The next tick's window is half-open at the start and begins at `floor(day)+1`, so nothing is drawn twice and no offer opened here depends on how the span that reached this age was cut.

C4. `_apply_resolutions(result, cfg, day, resolutions) -> bool moved`:
```
if not resolutions: return False
if result.quests is not a dict: raise missing_block('quests', 'This world carries no quests to resolve. Advance it by at least one day first, which is what opens them.')
by_id = {q.quest_id: q for q in block.quests}
hooks = {h.hook_id: h for h in (result.heroes or {}).quest_hooks or []}
for item in resolutions:                       # caller order
    validate dict; extra keys -> unknown_field; outcome not in {completed,failed,abandoned} -> invalid_choice
    quest = by_id.get(item.quest_id);  if None -> unknown_field(str(id), sorted(by_id), noun='quest')
    if quest.state in ('completed','failed','abandoned','expired'):
        raise refused_by_world('quest_id', id, 'Quest X is already S and cannot be resolved again.',
                               alternatives=[q.quest_id for q in block.quests if q.state in ('offered','taken')])
    quest.state = item.outcome; quest.closed_day = day; quest.closed_reason = 'resolved'
    block.log.append({day, quest_id, event: item.outcome})
    effect = (hooks.get(quest.quest_id) or {}).actual_effect or {}
    if item.outcome == 'completed' and cfg.magic_enabled and 'school' in effect and 'intensity_delta' in effect:
        net = result.magic.networks.get(effect.school);  node_id = effect.node_id
        if net is not None and node_id is not None:
            current = first node.intensity where node.id == node_id, else None
            if current is not None:
                target = min(4., max(0., current + float(effect.intensity_delta)))
                networks[effect.school] = edit_network(net, node_id=node_id, intensity=target)
                block.log.append({day, quest_id, event:'ley_applied', school, node_id, intensity:target})
                moved = True
return moved
```
A completed hook whose declared effect is a ley change applies it, **because the hook states the school and the delta itself**. Every other kind is recorded and nothing else: inventing a world consequence the hook does not declare would be this module deciding play. The already-closed case is `refused_by_world`, not malformed: the id was real and the caller is simply late.

C5. `_ley_drift(result, cfg, index) -> bool moved`:
```
if not cfg.magic_enabled: return False
for (name, net) in magic.networks.items():                    # insertion order
    rng = Random(child_seed(cfg.seed, 'time-ley-' + name, index))
    for item in net.nodes + net.edges:                        # nodes first, then edges, in list order
        item.intensity = min(4., item.intensity * rng.uniform(LEY_YEAR_LOW, LEY_YEAR_HIGH))
        moved = True
return moved
```
One year of drift, keyed on the absolute year index. The age transition's own drift is unchanged and still owns the age boundary; this is the same random walk at a year's granularity, composing to the same band across an age, so a world advanced by a hundred year-steps and one advanced by an age arrive at comparable intensities without either pretending to be the other. There is no lower clamp — the multiplier is strictly positive, so intensity approaches but never reaches 0.

C6. `_apply_surge(result, day)` — recompute the lunar surge. Closed form, no state, no draw:
```
factors = tide(astrology.moon, day)
for (name, net) in magic.networks.items():
    if name in factors: net.surged_strength = net.strength * factors[name]
magic.lunar_surge = {day, factors: dict(factors),
                     formula: 'surged_strength = strength * tide_school(day)'}
```
The same two lines `refresh_astrology` applies, against the clock's day rather than the reported year, and using the same `tide` primitive so the two cannot disagree. A hidden school takes no surge and keeps no `surged_strength`; that asymmetry is the twelve-school contract and is not an oversight.

C7. `_villain_outlook(result, cfg)` — `if 'villains' not in result: return`, else `result.villains.outlook = outlook(result, cfg, float(options(cfg).get('villain_rise', 0.)), float(options(cfg).get('villain_density', 3.)))`. **A villain rises, holds or falls at an age boundary and nowhere else**: `advance` reads the threat assessment an age inherited, so raising one mid-tick would let a villain be raised by its own damage. The outlook is a reading of present ground and is safe to refresh; the cast is not.

C8. `_dedupe_warnings(result)` — keeps `warnings` a set of statements rather than a tally of how often a pass ran:
```
seen, out = set(), []
for w in result.warnings:
    key = json.dumps(w, sort_keys=True, default=str)
    if key in seen: continue
    seen.add(key); out.append(w)
result.warnings = out
```
`add_seasonal_food` ends with `result['warnings'].append(...)`, so it is not quite the wholesale replacement the rest of its body is: every run leaves another copy of the same sentence. Run once at generation that is invisible; run on a cadence it turns a list of limitations into a counter, and it would differ between two chunkings of one span purely by how many calls were made. Order is preserved, and only exact repeats are dropped — a warning this tick did not produce is never removed. **This was caught by `test_a_year_in_four_seasons_is_a_year`, not by reasoning, and a future pass that appends to a shared list will need the same treatment.**

════════════════════════════════════════════════════════
PART D — COST REPORTING
════════════════════════════════════════════════════════

D1. `cost_estimate(world, ages)`:
```
size = float(world.config.size)
recorded = world.timing_ms.age_advance_total
if recorded:
    previous = [op for op in history.operations if op.get('steps') or op.get('ages')]
    last     = previous[-1] if previous else {}
    carried  = max(1, int(last.get('ages') or last.get('steps') or 1))
    per_age  = float(recorded)/1000./carried
    basis    = "this world's own recorded age_advance_total over the %d age(s) that call advanced" % carried
    measured = True
else:
    per_age  = 181.8 * (size/33.)**2
    basis    = <the long extrapolation sentence>
    measured = False
return {ages, per_age_seconds: round(per_age,3), total_seconds: round(per_age*ages,3),
        peak_mb: round(566.*(size/17.)**2, 1), peak_mb_precision: <sentence>, basis, measured}
```
`age_advance_total` is the wall time of a whole call, and a call may have carried up to ten ages; dividing by the ages that call actually advanced is what makes it a per-age figure. Reading it raw reported a ten-age run as the cost of one. `measured` says whether **this world's own** cost was used, not whether anything has ever been timed.

D2. `tick_cost_estimate(world, days, steps)`:
```
size  = float(world.config.size or 17.)
scale = (size/17.)**2
copy_seconds = 1.0 * scale
return {steps, elapsed_days: days, elapsed_years: round(days/360., 3),
        total_seconds: round(copy_seconds + steps*0.0000142*scale, 3),
        copy_seconds: round(copy_seconds,3),
        per_step_seconds: 0.0000142*scale,
        peak_mb: round(51.*scale + steps*132./1048576., 1),
        basis: <sentence>, precision: <sentence>, measured: True}
```
Two terms, because the cost has two shapes: a fixed cost per call (the private deep copy, scaling with the world and not with the span) and a marginal cost per cadence step (scaling with the span). A single-term model gets whichever one it omits badly wrong. **Steps are a proxy for cost and not cost** — a coalesced pass runs once per call however many crossings it had, while the daily quest sweep runs every day — so two spans with equal counts can differ by orders of magnitude, and `precision` says so.

D3. `age_gate(world)` — calls `terrain_history.validate_age_world(world)` and, on `ValueError`, returns `exc.document()` (or a hand-built INVALID_INPUT envelope if the exception is a bare ValueError) with `document['gate'] = 'terrain_history.validate_age_world'`; returns `None` if it passes. **Reported rather than raised, and that is deliberate.** A caller that asked for five thousand years deserves the estimate *and* the reason together; an exception would throw away the more useful half. This used to hard-code `size > 257` and name `validate_age_world` as the source — a ceiling that had already been raised to 1025, in a check that never contained the number. A caller debugging it would have found nothing at the named location, and the whole 258..1025 band was refused a span it would have advanced happily. **Asking the real predicate is the only version of this that cannot go stale again.**

D4. `_advance_ages(world, ages, moon_day)`:
```
current = world; done = 0
while done < ages:
    steps = min(10, ages - done)
    body = {api_version:1, world:current, steps:steps}
    if moon_day is not None: body.moon_day = moon_day
    current = advance_age_request(body)
    done += steps
return current
```

════════════════════════════════════════════════════════
PART E — `quest_request(body)`
════════════════════════════════════════════════════════
```
actions = validate_actions(body.actions or [])        # shape only, no world
world   = body.world;  validate_time_world(world)     # the SAME gate the tick uses
block   = world.quests;  if not dict or block.quests not a list:
    raise missing_block('quests', 'This world carries no quests to act on. Advance it by at least one day first, which is what opens them.')
now = clock(world).day
if not actions:
    result = shallow copy of world
    result.quest_actions = {api_version:1, day:now, applied:0, actions:[]}
    return result                                    # nothing changed -> nothing recorded
result = shallow copy of world
result.quests = deepcopy(block)                      # exactly one block copied
result.quests.setdefault('log', [])                  # a hand-persisted board may lack one
by_id = {q.quest_id: i for i, q in enumerate(quests) if q is a dict and q.quest_id is a str}
for item in actions:                                 # caller order
    if item.quest_id not in by_id: raise unknown_field(id, sorted(by_id), noun='quest')
    day   = now if item.day is None else item.day
    index = by_id[item.quest_id]
    alternatives = [q.quest_id for q in quests if q.state in ('offered','taken') and q.quest_id is a str]
    quest, entry = transition(quests[index], item.action, day, alternatives)
    quests[index] = quest
    result.quests.log.append(entry)
    applied.append({quest_id, action, day, state: quest.state})
result.quest_actions = {api_version:1, day:now, applied:len(applied), actions:applied}
result.history = dict(world.history or {}) with operations = list(old) + [{api_version:1, kind:'quest', actions:[{quest_id, action, day}...]}]
return result
```
`alternatives` is recomputed per action rather than once: a batch that closes three quests should not keep offering them back as alternatives.

`transition(quest, action, day, alternatives) -> (new record, log entry)` — pure, never mutates:
```
state = quest.state
if state not in ('offered','taken'):
    raise refused_by_world('quest_id', id, 'Quest X is already S and cannot be An.', alternatives)
if action == 'take':
    if state == 'taken':  raise refused_by_world(..., 'Quest X has already been taken.', alternatives)
    expires = quest.expires_day
    if expires is not None and day > expires:
        raise refused_by_world(..., 'Quest X was offered until day E and day D is past that; the world has already let it lapse.', alternatives)
    out = copy of quest; out.state = 'taken'
    return out, {day, quest_id, event:'taken'}
out = copy of quest; out.state='abandoned'; out.closed_day=day; out.closed_reason='abandoned'
return out, {day, quest_id, event:'abandoned'}
```
The late-take refusal is **not pedantry**: `_quest_step` tests the window only while a quest is `offered`, so a take applied after the window closed but before the next sweep would make the offer immortal — it leaves `offered`, and the expiry branch is never reached again. `abandoned` as a `closed_reason` is distinct from `resolved`: a resolution is the game reporting an outcome it played; this is the party putting the promise down, and a consumer reading the board should be able to tell those apart without re-reading the log.

**The copy is structural.** This route writes exactly two blocks, `quests` and `history`, so exactly those two are copied and everything else in the returned document is the caller's own object. A full deep copy grows with the square of the grid, and a session that takes a quest cannot pay a two-second copy of its terrain to record it.

════════════════════════════════════════════════════════
PART F — `person_state_request(body)`
════════════════════════════════════════════════════════
```
changes = validate_changes(body.changes or [])
world   = body.world;  validate_time_world(world)
known   = index(world)
if not known: raise missing_block('people', 'This world carries no person lists to change. ...')

# RESOLVE EVERYTHING FIRST -- after this loop no refusal remains, so no path exists
# on which a partly written world was ever built.
resolved = []
for change in changes:
    where = known.get(change.uid)
    if not where: raise unknown_field(uid, sorted(known), noun='person')
    if len(where) > 1: raise refused_by_world('uid', uid,
        'uid X names a person in more than one block (a, b), so this call cannot tell which one you mean.')
    resolved.append((change, where[0]))

touched = sorted({entry[0] for _, entry in resolved})      # top-level keys, SORTED
result  = shallow copy of world
for key in touched: result[key] = deepcopy(world[key])

for (change, (key, listing, block, position)) in resolved:
    people = result[key][listing]
    record, before, after = plan(people[position], block, change.status)
    row = {uid, block, from_status:before, to_status:after, token: token(block, after),
           day: change.day, reason: change.reason, changed: record is not None}
    if record is not None:
        people[position] = record; applied.append(row); requested.append(dict(change))
    report.append(row)

if not applied:                                  # discard the copies entirely
    result = shallow copy of world
    result.person_state = {api_version:1, applied:0, changes:report, notes:_notes([])}
    return result
result.person_state = {api_version:1, applied:len(applied), changes:report, notes:_notes(applied)}
result.history = dict(world.history or {}) with operations = list(old) + [{api_version:1, kind:'person', changes:requested}]
return result
```

`index(world)` — `uid -> [(top key, listing, liveness block, position)]`:
```
SOURCES = (('heroes','people','heroes'), ('heroes','dreads','dreads'),
           ('npcs','people','npcs'),     ('villains','people','villains'))
for (key, listing, block) in SOURCES:
    container = world.get(key);  if not a dict: continue
    if key != 'villains' and container.status != 'ok': continue   # villains has no block status
    people = container.get(listing);  if not a list: continue
    for (position, record) in enumerate(people):
        if record is a dict and record.uid is a str: found.setdefault(uid, []).append((key, listing, block, position))
```
The value is a **list** rather than a single entry so a uid claimed by two blocks is *detected* rather than resolved by iteration order. No generated world has one today; that is a fact about the uid minters, not a guarantee any of them makes.

`plan(record, block, status) -> (new record or None, before, after)`:
```
before = liveness(record, block)
if before == status: return None, before, before          # a NO-OP WRITES NOTHING
out = dict(record);  out.status = token(block, status)
after = liveness(out, block)
if after != status: raise ValueError('liveness translation for B does not round-trip: wrote W, read R')
return out, before, after
```
The round-trip check is unreachable while `token` is the inverse of `liveness`, and asserted rather than assumed because the two tables live side by side and a future block added to one and not the other would otherwise write a word that reads back as its opposite.

`_notes(applied)` — always the first note; plus a roster-mirror note if any applied row's block is `heroes` or `dreads`; plus a villain note if any applied row is `villains` → `gone`. Exact strings are in *constants*.

════════════════════════════════════════════════════════
PART G — THE LIVENESS PREDICATE
════════════════════════════════════════════════════════
Four packages answer one question in three vocabularies:
```
block      present word   gone word
heroes     living         legend
dreads     living         legend       # the antagonist cast INSIDE the heroes block
npcs       alive          dead
villains   living         fallen
```
`liveness(record, block)`:
1. `block` not in the table → `ValueError('Unknown liveness block: ...')`.
2. `record` not a dict → `ValueError`.
3. If the record carries any of `('people','quest_hooks','policy_revision')` → `ValueError('liveness expects a person record, not the B block')`. **`status` means two different things at two depths of one block**: `hero_generator` writes a block-level `status` of `'ok'`/`'failed'` and a person-level `status` of `'living'`/`'legend'` under the same key name, and `npc_roster` does the same. A predicate matching on the string alone would read a failed block as a living person.
4. `block == 'villains'` → delegate to `terrain_villains.is_standing(record)`, which is `(record.get('status') or 'living') == 'living'`. Two implementations of one rule is the defect this module exists to prevent, so it delegates rather than restating.
5. Otherwise `PRESENT if (record.get('status') or PRESENT_STATUS[block]) == PRESENT_STATUS[block] else GONE`. **Absent status means present** — a record written before a package recorded departure at all described someone who stood, and reading a missing key as gone would turn every world generated before the fall pair into a world of ghosts.

`token(block, state)` is the inverse and the *only* published translation: a writer that picked a word by matching on a string would put `dead` on a hero or `legend` on an npc, and both read back through `liveness` as the wrong answer rather than as an error.

`census(world)` → per-block `{present: n, gone: n}` over `(('heroes','heroes'),('dreads','heroes'),('npcs','npcs'),('villains','villains'))`, skipping a non-dict container, skipping a non-villains container whose `status != 'ok'`, and skipping a block whose people list is `None`. A failed block is reported as *absent* rather than as an empty cast, because zero present people and no package at all are different facts.

## Constants

**Calendar (terrain_time_schedule).** All exact, all dimensionless days.
- `HOURS_PER_DAY = 24.` — used only to convert `elapsed.seconds` (`v / (24*3600)`).
- `DAYS_PER_MONTH = 30.` — the `nomad_routes`, `beast_movements` and `encounters` period.
- `MONTHS_PER_YEAR = 12.`
- `DAYS_PER_YEAR = 360.` — the `ley_drift`, `nomads`, `nomad_effects`, `villain_outlook` period, and the years→days conversion.
- `SEASON_DAYS = 90.` — the `seasonal_food` period and the DAY/SEASON band boundary.
Changing any of these changes what a "year" means to every consumer, including `POST /world/moon`, which reads the same calendar. They exist here so a tick and the moon cannot disagree about what time it is.

- `AGE_YEARS = 5000.`, `AGE_DAYS = 1'800'000.` — **new contract, not a discovered constant.** An age had no duration before the clock existed: `history.ages` is a list, and the age lottery samples `int(founding.end_year) * 360` for its moon day, so nothing in the generator said how long one lasts. A tick cannot route a long span into an age advance without an answer. It is load-bearing in two places outside its own line and both move with it or break silently: (1) `band` returns `AGE` at `AGE_DAYS`, so what counts as a tick rather than an age advance moves with it — a century is a `LONG` tick, one fiftieth of an age; (2) the per-year ley bounds are derived from it.

**Band thresholds:** `INSTANT` < 1 day; `DAY` < 90; `SEASON` < 720 (2 years); `LONG` < 1,800,000; `AGE` at or above. Moving a threshold changes only the *label* in the report and the route into the age band — it cannot change which cadences run, and any C++ port that lets it do so reintroduces the data-loss defect.

**Quest offer window (terrain_time).**
- `OFFER_MIN_DAYS = 180.`, `OFFER_MAX_DAYS = 1080.` — half a year to three years, drawn uniformly. The window is drawn once per quest from the step that created it, so it is a function of simulated time like everything else here, and a quest created at the same day by two differently chunked calls draws the same window. Widening it makes quests linger; narrowing it churns the board.

**Ley drift bounds (terrain_time).**
- `LEY_AGE_LOW, LEY_AGE_HIGH = .65, 1.35` — the band ley intensities drift across one age transition, owned by `terrain_history` and unchanged here.
- `LEY_YEAR_LOW  = 0.65 ** (1/5000)  ≈ 0.99991385`
- `LEY_YEAR_HIGH = 1.35 ** (1/5000)  ≈ 1.00006002`
**The exponent is derived from `AGE_YEARS` and must never be written as a literal.** The two numbers are one contract: a year step must compose to the age band over an age rather than applying an age-sized shock once per year. The pair that composes correctly over a *century* composes to `0.65 ** 50` — about 2e-10 — over the five thousand years an age now lasts, **which is the magic layer draining to zero through ordinary ticking, silently, with every individual step inside its stated bounds and nothing out of range anywhere to notice.** Do not round them to friendlier numbers.

**Ceilings.**
- `MAX_AGE_STEPS = 10` — `advance_age_request` accepts 1..10 steps per call, so a longer run is chunked.
- `MAX_AGES_PER_REQUEST = 50` — a run has to end; each age rebuilds every derived layer, so an unbounded span is an unbounded loop (a million years is two hundred ages). Its *meaning* changed with decision 028 and the number did not: fifty ages was five thousand years and is now two hundred and fifty thousand. Whether that is still the right ceiling is deliberately left open. Refused with the estimate attached rather than silently attempted.
- `MAX_TICK_STEPS = 200000` — the most work one request executes as a tick before it is **reported** rather than attempted. **An absolute count, and never a share of an age.** Writing it as a fraction of `AGE_DAYS` would re-couple the guard to the band boundary, which is the defect itself: how much work a machine can afford does not change when the calendar is redefined. 200,000 steps is about 554 years measured from day zero — roughly 3.8 s and 25 MB of transient step tuples at size 17, against a *measured* 26.8 s and 230 MB for the 4,999-year span the band permits. It is an order of magnitude above any span a game drives (a century is 36,107 steps) and an order of magnitude below the band's own maximum, so what it catches is a caller that did not know what it was asking for rather than a caller playing the game. **It advises; it does not forbid** — `commit: true` proceeds. It does **not** scale with the world, deliberately: a ceiling read out of the cost model would move every time somebody re-measured, and the same request would be refused on Monday and run on Tuesday with no constant edited.
- Capacity bounds: `leyline_edits` ≤ 128; `resolutions` ≤ 1024; `actions` ≤ 1024 (`MAX_ACTIONS`); `changes` ≤ 1024 (`MAX_CHANGES`); `reason` ≤ 200 characters (`MAX_REASON`). The three 1024s are deliberately the same number so a caller batching both does not have to learn two.

**Intensity clamps.** Ley drift clamps at `min(4., ...)` with no lower bound. A resolved hook's delta clamps to `min(4., max(0., current + delta))`. `edit_network` refuses anything outside `0 <= intensity <= 4` and requires a `new_node` direction to be a unit vector within `1e-6`.

**Cost basis constants (terrain_time)** — every one is *description with its basis attached*, not invariant. Measured 2026-09-21, seed 42, recipe 3, through phase 16, on a development machine with five other sessions sharing it, so **wall clock is an upper bound and peak working set a lower one.**
- `AGE_BASIS_SIZE = 33.`, `AGE_BASIS_SECONDS = 181.8` — one age advance took 34.3 s, 37.6 s and 39.4 s at size 17 and 181.8 s at size 33. The *larger* point is the basis on purpose: an age advance grows faster than cell count between them — 4.7x the time for 3.8x the cells — so extrapolating from size 17 under-reports size 33 by a fifth, while extrapolating from 33 over-reports 17 by about the same. A cost guard should err toward warning.
- `AGE_BASIS_MB_SIZE = 17.`, `AGE_BASIS_MB = 566.` — **memory has its own reference size, and that is deliberate rather than untidy.** The only figure available is peak working set, and the machine was trimming working sets under load: the size-33 reading came back *smaller per cell* than the size-17 one, which is the instrument and not the world. So the size-17 reading is used — 566 MB peak across a generation and one age advance, against 378 MB for the generation alone — and it is a **lower** bound. Pretending both quantities share one reference would state a precision neither has.
- `TICK_BASIS_SIZE = 17.`, `TICK_BASIS_COPY_SECONDS = 1.`, `TICK_BASIS_STEP_SECONDS = .0000142`, `TICK_BASIS_COPY_MB = 51.`, `TICK_BASIS_STEP_BYTES = 132.` — measured at size 17: 18,057 steps in 1.15 s, 72,207 in 2.05 s, 361,007 in 6.15 s and 1,804,646 in 26.81 s, a straight line at 14.2 µs a step over a 1.0 s copy. The copy is 51 MB resident with `build_stages` shared rather than copied, as the code does it; a step tuple is 132 bytes, measured over a century's 36,107 of them. Both terms are scaled by cell count, which the size-33 readings justify rather than assume: the same spans took 4.30 s, 6.47 s, 19.26 s and 79.90 s, so the copy grew 3.2x and the per-step cost 3.0x for 3.8x the cells. Scaling both quadratically therefore over-reports size 33 by about a fifth, which is the safe direction.

**The old constants and why they were wrong** — worth carrying because it is the mistake a porter will repeat. The fallback used to extrapolate from *generation* cost (11 s / 30 MB at size 129), a different pass entirely, and both halves had gone stale. The MB figure was the worse of the two: 30 MB at size 129 scales down to **0.5 MB at size 17**, where generation alone now peaks at 378 MB. Wrong by nearly three orders of magnitude, **in the direction that tells a caller an expensive call is free.**

**Version constants.** `TIME_API_VERSION = 1`, `CLOCK_VERSION = 1`, `QUESTS_VERSION = 1`, `QUEST_API_VERSION = 1`, `PERSON_API_VERSION = 1`. The world gate pins `terrain.version == 6`, `generator_version == 16`, `recipe.version == 3`, `astrology.version == 1`, `config.world_recipe == 3`, `config.phase >= 13`.

**Quest difficulty rubric (hero_generator, `policies/wells.json` → `quests`)** — not written by these three modules but it is the third field of the quest contract, so it is recorded here:
```
difficulty = clamp(1, 5, floor(opposition + pressure + 0.5))
opposition: nest      -> the nest's own `tier`  (the rubric itself)
            ley_node  -> ley_floor + ley_intensity_weight * node intensity   = 1.0 + 0.5*i
            otherwise -> opposition[kind]: ruin 2.0, relic 2.0, city 2.0, hamlet 2.0,
                         fortress 2.0, port 2.0, shrine 1.0, college 1.0; default 1
pressure  : 0 for a nest target (PRESSURE_EXEMPT); otherwise
            min(pressure_cap=1.0, pressure_per_tier=0.25 * worst nest tier whose
            declared range_m reaches the target, measured in grid metres at spacing_m)
difficulty_min = 1, difficulty_max = 5
```
The banding is `floor(x + 0.5)` and **not** `round`, because CPython rounds halves to even and a rubric that sends 2.5 down and 3.5 up reads as arbitrary to anyone checking it by hand. The rubric reaches 5 only through what stands at the target, never through the ground around it, so a long walk past a lair can never turn an errand into a campaign threat. Tier meaning: 1 harmless, 2 can hurt you, 3 kills the careless, 4 kills the prepared, 5 a campaign threat.

**Verb vocabulary (closed contract, not a tuning table):** `('reclaim','recover','defend','slay','trade','tend','convert','supply','cleanse','investigate')`. A ley effect carries no `action`, so its verb comes from the sign of `intensity_delta` through `quests.ley_verb`: `>= 0 -> 'tend'`, `< 0 -> 'cleanse'`. `hook_intensity_delta = 0.5`. A new verb needs a new hook branch to emit it, which is why the list is code and not policy.

**Person-route note strings (exact, published to consumers):**
1. Always: *"Nothing cascades from this call. The next tick closes an offer whose giver is gone, as it already does for a giver the world removed; everything else is untouched."*
2. If any applied row is `heroes`/`dreads`: *"The npcs roster mirror of this person was not updated. npc_roster copies the cast into npcs.people under a prefixed uid at generation time and that copy is a snapshot; it is rebuilt at the next age boundary."*
3. If any applied row is a villain going `gone`: *"A villain marked gone keeps its claims, its held nodes and its absence from villains.fallen. The bookkeeping an age boundary does around a fall is not run here."*

## Data files

**None of the three assigned modules loads a data file.** They are pure glue over the world document plus `terrain_errors`, `terrain_liveness`, `terrain_astrology.tide` and `terrain_leyline_history.edit_network`.

Two files reachable one hop out matter to the quest contract:

**`Sim/hero_generator/policies/wells.json`**, key `quests` — read by `hero_generator.hooks` at generation, not by the tick. Schema and values are quoted in full under *constants*. Representative entry:
```json
"quests": {
  "notes": "The quest contract's derived fields. A hook already names the anchor; this says how hard the thing is. The generator prices no reward -- difficulty is the number the consuming game turns into treasure. ...",
  "ley_verb": {"strengthen": "tend", "weaken": "cleanse"},
  "opposition": {"ruin":2.0,"relic":2.0,"city":2.0,"hamlet":2.0,"fortress":2.0,"port":2.0,"shrine":1.0,"college":1.0},
  "ley_floor": 1.0, "ley_intensity_weight": 0.5,
  "pressure_per_tier": 0.25, "pressure_cap": 1.0,
  "difficulty_min": 1, "difficulty_max": 5
}
```
The same file's `magic` key supplies `hook_intensity_delta: 0.5` and the `opposites` table (radiant↔umbral, fire↔water, earth↔air, weave↔infernal) that decides which opposing node a covert ley hook asks the player to weaken.

**`Sim/npc_roster/policies/posts.json`**, key `verbs` — holds a second copy of the verb list. `hero_generator` may not import a sibling package (the leaf packages are forbidden from importing the generator; `npc_roster/policy.py` states the rule and `hero_generator/seeds.py` duplicates `child_seed` rather than break it), so the list is declared in both places and a test is what keeps the two copies one list.

**Published JSON schemas that constrain what these modules emit** (`Contracts/schemas/`): `read-quests.schema.json` (constrains `closed_reason` to the seven reasons plus null, and the `state` enum to the six states), `quest-actions.schema.json`, `person-state.schema.json`, `operation-envelope.schema.json` (carries the producing API's report as an **untyped object** precisely so `time_advance` can grow without a version bump), `counter-kernel.schema.json` (the three error codes). A C++ port that emits these blocks should keep the same field names and enums or the read surface breaks.

## Random streams

**Three RNG streams, all keyed on simulated time and never on a call ordinal.** This is the binding invariant of the whole subsystem.

1. **Quest offer windows.** `random.Random(child_seed(cfg.seed, 'time-quests', index))` where `index` is the absolute day index `floor(day/1.0)`. One `Random` is constructed per quest sweep (i.e. per simulated day crossed, plus one for the age-turn rebuild at `int(floor(day))`, plus possibly one at `index = 0` for the bootstrap sweep a resolution triggers on a world with no board). Draws: exactly one `uniform(180., 1080.)` per **newly opened** quest, in the order the union walk reaches them. A sweep that opens nothing draws nothing but still constructs the generator.

2. **Ley drift.** `random.Random(child_seed(cfg.seed, 'time-ley-' + school_name, index))` where `index` is the absolute year index `floor(day/360.)`. One `Random` per school per year crossing. Draws: exactly one `uniform(LEY_YEAR_LOW, LEY_YEAR_HIGH)` per node and per edge, nodes first then edges, in list order. Draw count is fully data-dependent on network size — a 12-school world with 3..24 nodes each plus edges draws hundreds per year step.

3. **The cadence passes.** Not a stream this module owns. Each pass is re-seeded by *replacing the config's seed*: `replace(cfg, seed=child_seed(cfg.seed, 'time-' + name, index))`, and the pass then derives its own streams from that seed however it normally does. A pass is a deterministic function of the world and the seed, so a time-derived seed is what makes it move at all.

`child_seed(master, domain, variation)` is `int.from_bytes(sha256(f'tectonics-v1:{master}:{domain}:{variation}').digest()[:4], 'big')` — a 32-bit value. The domain strings used here, verbatim, are `'time-quests'`, `'time-ley-<school>'`, `'time-encounters'`, `'time-beast_movements'`, `'time-nomad_routes'`, `'time-nomads'`, `'time-nomad_effects'`, `'time-seasonal_food'`.

**Never keyed on a call.** `_instant` draws nothing. `_apply_surge` draws nothing — it is a closed form. `_villain_outlook` draws nothing. `quest_request` and `person_state_request` draw nothing at all: a quest's one random quantity, its offer window, is drawn once by `_quest_step` from the absolute step index and is never redrawn.

**Draw counts are data-dependent in three ways** and a C++ port must preserve each: the number of quests opened on a day (hooks minus already-tracked), the number of ley nodes+edges per school, and whichever pass-internal draws the six cadence passes make. None of these depends on how the caller cut the span, which is the point.

The determinism claim the tests actually assert is byte equality of the serialised world with the operation log and wall-clock timings excluded, for `advance(30y)` vs `advance(15y)` twice, a year vs four seasons, a year of monthly ticks vs one yearly tick (the cut that matters most, because it crosses a band boundary), and one age plus thirty days vs one age then thirty days.

## Ordering hazards

Every one of these reaches output. This is the longest list in the subsystem and the C++ port will get it wrong by default, because `std::unordered_map` has no insertion order and Python dicts do.

1. **`heroes.quest_hooks` list order decides which new quest draws which offer window.** `_quest_step` walks `list(by_hook) + [q for q in tracked if q not in by_hook]`, where `by_hook` is a dict built by iterating `hooks` in list order. New quests are created in that walk order, and each one draws one `rng.uniform` from a single per-day generator. Reorder the hooks and every quest opened that day gets a different expiry. **This is the single most load-bearing ordering fact in the module.**

2. **`tracked` iteration order** — the second half of that concatenation iterates `block['quests']` in list order, which after any previous sweep is sorted by `quest_id`. On the very first sweep it is empty. Those entries never draw, so this half only affects log-append order for `hook_gone` closures.

3. **`block['quests'].sort(key=quest_id)`** at the end of every sweep. A stable sort by string `quest_id`, using Python's byte-wise `str` comparison (code-point order). The read surface documents the board as being "in the order terrain_time sorts the board, which is by quest_id", so this ordering is published.

4. **`quests.log` append order** is the order closures and openings happen within a sweep, which is (1). Across sweeps it is chronological.

5. **`magic.networks` dict iteration order** — used by `_ley_drift` (which constructs a per-school `Random`, so order does not change the values, only the order they are written) and by `_apply_surge`. Order does not change results here, but it *does* change the JSON key order of `magic.networks` and of `lunar_surge.factors`, which matters for byte-reproducibility.

6. **`net['nodes'] + net['edges']` order inside `_ley_drift`** — nodes first, then edges, each in list order, all drawing from one generator. Reordering silently reassigns every multiplier.

7. **`CADENCES` tuple order** — the tie-break rank when two cadences fall on the same day, *and* dependency order. `steps()` sorts by `(day, CADENCE_RANK[name], index)`. Steps are returned in absolute-time order, ties broken by the pinned cadence order; **ordering by time rather than by subsystem is the second half of chunk-independence**, because a subsystem-major order would run a year of one pass before a month of another, and the two orders diverge the moment a call boundary lands between them.

8. **`by_cadence` returns `dict(sorted(counts.items()))`** — alphabetical by cadence name, and it lands in `time_advance.by_cadence` in the output document.

9. **`_giver_index` first-writer-wins** across `heroes.people`, then `heroes.dreads`, then `npcs.people`. A uid appearing in two of those resolves to the first, decided by that tuple order plus each list's own order. The read surface documents giver resolution as using "the same index, in the same first-writer-wins order".

10. **`terrain_person_api.index` is the opposite policy** — it collects *all* matches into a list and refuses a contested uid rather than preferring one. The two resolvers are deliberately different and are held to the same answer by a test rather than a shared function, because the shared function would have to be the wider one and widening `_giver_index` would change which people the quest lifecycle considers.

11. **`touched = sorted({...})`** in the person route — the deep-copy order of top-level blocks. Sorted explicitly, so it is deterministic; copy it.

12. **`sorted(set(body) - allowed)[0]`** in every validator — which unknown field gets named in the refusal is decided by sorting. Same for `sorted(set(edit) - fields)[0]`, `sorted(set(item) - {...})[0]`, `sorted(by_id)`, `sorted(known)`, `sorted(networks)`, and `', '.join(sorted(w[2] for w in where))`.

13. **`_apply_resolutions` iterates `resolutions` in caller order**, so which refusal fires first on a batch with two bad items is caller-controlled — and because it raises, the whole call is discarded, so this is observable only in the message.

14. **`quest_request` iterates `actions` in caller order**, and `alternatives` is recomputed per action, so the suggestion list shrinks as a batch closes quests.

15. **`_dedupe_warnings` preserves first-seen order** and drops only exact repeats (keyed on `json.dumps(w, sort_keys=True, default=str)`). A warning this tick did not produce is never removed.

16. **`history.operations` append order** is call order, and the operations list is excluded from the byte-equality determinism test precisely because two calls are honestly two operations.

17. **`cost_estimate` reads `previous[-1]`** — the *last* operation carrying `steps` or `ages`, which is order-dependent in a way that is arguably wrong (see *open questions*).

18. **`_turn_age_board` closes in `block['quests']` order**, which is `quest_id` order from the last sweep, so the `age_turned` log entries come out sorted by quest id.

## Edge cases

**Span of exactly zero.** `elapsed: {days: 0}` is legal. `steps(now, 0)` is empty (`first = floor(now/p)+1 > last = floor(now/p)`), so with no edits and no resolutions it takes `_instant`, which then does **not** append an operation (`if days > 0`). The clock is rewritten to the same day and `timing_ms.time_advance_total` is stamped. This is the "what time is it every frame" path and it is deliberately free of history growth.

**Span that crosses nothing but is not zero.** Any `days` in `(0, 1)` that does not step over a day boundary. `_instant` runs, an operation *is* appended, `steps: 0`. Keyed on "crosses nothing", not "is short": a sub-day call carrying a resolution falls through to the full path, which is correct, because the old `band == INSTANT` test let such a call move the ley field.

**A sub-day span that does cross a day boundary** runs that day's quest sweep. That is correct rather than a violation — three seconds normally runs nothing because three seconds normally crosses nothing.

**Resolutions on a world with no `quests` block.** `advance_time_request` bootstraps one by calling `_quest_step(result, cfg, now.day, 0, ...)` — note `index = 0`, not `floor(now.day)`. Only then does `_apply_resolutions` run; if the block still does not exist it raises `missing_block`. `quest_request` refuses outright with `missing_block` rather than bootstrapping.

**Empty `actions` / empty `changes` / every change a no-op.** Nothing is copied that survives, nothing is recorded in `history.operations`, and the report says `applied: 0`. The person route still reports every requested change with `changed: false`. This matches the age band's estimate path: an operation log that grows on every no-op is an unbounded record of nothing happening.

**A person already in the asked-for state.** `plan` returns `None` and **writes nothing at all** — it does not stamp a `status` in. An absent status reads as present, so stamping one would add a key to a record for no reason and move the bytes of a document whose reproducibility is a contract.

**A block with `status: 'failed'`.** `_giver_index`, `index` and `census` all skip it (villains excepted, which has no block-level status). Zero present people and no package at all are different facts. A quest whose giver was in a failed block therefore closes as `giver_gone` on the next sweep.

**A record that is actually a block.** `liveness` raises `ValueError` if the record carries `people`, `quest_hooks` or `policy_revision`. This is the depth collision: `status` means `'ok'/'failed'` at block level and `'living'/'legend'` at person level under the same key name.

**A uid in two blocks.** The person route refuses with `refused_by_world` naming both blocks. The quest lifecycle's `_giver_index` silently takes the first. Different policies, deliberately.

**A hook with no `target` and no `nest_id`, or no `verb` and no `action`.** `anchor`/`verb` land as `None`. The read schema types both as `string|null`. A pre-heroes-2 ley hook always produces a null verb.

**A quest whose giver_uid is `None`.** `givers.get(None)` returns `None`, so it closes as `giver_gone` immediately.

**A target kind this module cannot resolve** (`ruin`, `relic`, `hamlet`, `fortress`, `port`, `shrine`, `college`, `ley_node`). `_target_present` returns `True` unconditionally. A quest wrongly left open is a smaller lie than a quest silently reaped because this module did not recognise its target.

**A `taken` quest past its expiry window.** Never expires — `_quest_step` guards the window check on `state == 'offered'`. This is why `transition` refuses a take whose `day > expires_day`: a take applied after the window closed but before the next sweep would make the offer immortal.

**An action or resolution naming a day in the past or the future.** Accepted, as long as it is finite and `>= 0`. The quest route uses it verbatim for `closed_day` and the log entry.

**An age span with zero remainder and no edits/resolutions.** The recursive remainder tick is skipped and `_apply_surge(advanced, clock_after.day)` runs instead, so the surge is still correct for the new day.

**An age span with a remainder of exactly 0 but carrying edits or resolutions.** The recursion still fires with `elapsed: {days: 0}`; `executed` is empty but `edits`/`resolutions` are not, so it takes the full deep-copy path rather than `_instant`.

**A resolution for a quest of the outgoing age, sent with an age span.** The board the remainder sees is the *new* age's, so the caller is refused with `unknown_field` naming the quest — which is a worse refusal than the `refused_by_world` it would get for a quest that merely closed. Recorded as a known wart.

**`magic_enabled == 0`.** `_ley_drift` returns `False` immediately; the ley-applied branch of `_apply_resolutions` is skipped; `leyline_edits` are refused outright. Critically, `ley_touched` must not be set by a no-op drift, or `evaluate_networks` indexes a missing `magic` and dies on a bare `KeyError` rather than any refusal this boundary knows how to make.

**A hidden magic school.** `tide` returns factors only for the known schools, so a hidden network gets no `surged_strength` key at all. Widening that to all twelve would add a key to four networks in every world and change the bytes.

**A world with no `villains` block.** `_villain_outlook` returns immediately.

**A CLI-persisted world with `timing_ms` stripped.** `_timing` recreates `timing_ms` with `total: 0.` before any pass runs. 32 producers across 18 modules write into `timing_ms` by subscript, so the key has to exist first. The producers are deliberately **not** guarded one by one; a `setdefault` at each of the 32 would scatter the contract across 18 modules and arm the next producer written.

**A world with no `history`.** `clock` reads `(world.get('history') or {}).get('ages') or []` → age 0. The tick uses `result.setdefault('history', {})`.

**A quest board hand-persisted without a `log`.** `quest_request` does `setdefault('log', [])` — a bare `KeyError` here would be the one failure at this boundary with no envelope around it.

**No world edges or poles are involved.** Nothing in these three modules touches the grid, a layer, the seam column or the pole rows. They are document-level code.

## Reasoning worth preserving

Quoted or near-quoted from the source, because none of it survives the repo.

**On the whole shape.** *"Failure is all-or-nothing. Every pass runs against a private copy and the caller's object is never touched; a raise leaves no half-advanced world anywhere. That guarantee is nearly free in a world-in/world-out shape and expensive to retrofit once anything mutates in place, so it is taken here rather than later."*

**On what a tick may not touch.** *"Terrain, water, climate, tectonics, biomes, civilizations, settlements, roads, city/hamlet/castle plans and the ley geometry are read-only below the age band: a tick moves the living layer over ground that does not move. Only an age advance rebuilds ground, and this module reaches that by calling `advance_age_request`, never by reimplementing it."* Note the one exception: the ley *field grids* are not read-only — when base intensities move, `evaluate_networks` rewrites the `ley_*` layers, which is the point of running it. Only the ley *geometry* (which nodes and edges exist) is fixed; only a player edit or a resolved hook adds a node.

**On bands.** *"A band names how big a span is. It does not decide which cadences may run. It used to. `steps()` filtered by `BAND_CADENCES[band(days)]`, which was a silent, permanent data-loss bug rather than a conservative one: the window is half-open at the start, so a crossing skipped because this call was too short is never revisited by any later call. A game ticking a month at a time would never once run ley drift, nomad reclassification, seasonal food or the villain outlook — not deferred, skipped — and a game ticking by the second would advance nothing but its clock, forever."*

**On why banding exists at all.** *"The alternative — one code path that runs everything and returns early — makes 'three seconds' and 'thirty years' the same function with different arguments, and the first time a rounding error lets a three-second call into the fate lottery, a player loses a city to a frame of animation."*

**On the determinism shape.** *"The failure mode this shape exists to prevent is a seed derived from tick ordinal. It is invisible until someone chunks differently, and by then a saved world has been advanced across it. Float addition is not associative and city identity is compared at zero tolerance downstream, so a quantity summed in two chunks that differs in the last ulp propagates into a different city set."*

**On the ley exponent.** *"The exponent is derived from `AGE_YEARS` and must never be written as a literal. The two numbers are one contract: the pair that composes correctly over a century composes to `0.65 ** 50` — about 2e-10 — over the five thousand years an age now lasts, which is the magic layer draining to zero through ordinary ticking, silently, with every individual step inside its stated bounds."* The guard test *"is a guard against a future edit rather than against this line."*

**On the age turn's quest reap.** *"It does not depend on the id disappearing, and that is the whole point. That is how reaping used to happen, and it was only mostly true. The regenerated cast re-mints positional uids as the same string, so `by_hook.get(hook_id)` found a different hook wearing the old id and the quest carried on: the old age's anchor, verb and stated_purpose, the new age's target check, and a `giver_uid` naming someone who no longer exists. Measured 2026-09-21 with all 49 open quests taken and one age advanced: 39 closed, 10 survived as that chimera."*

**On retiring the previous age's rows.** *"The previous age's rows are retired with it rather than kept beside the new ones, for the same reason rather than for tidiness: a retained closed row whose id the new cast re-mints could not be told apart from the offer that id now names, and which rows survived would again be decided by an accident of uid minting."*

**On the two easily-collapsed reasons.** *"`hook_gone` and `age_turned` are the pair that is easiest to collapse and must not be... A consumer that cannot tell those apart cannot tell a player whether their quest went away or their age did."*

**On who decides outcomes.** *"The world decides whether an offer still stands. It never decides whether a player succeeded: that arrives as a resolution from the game, because an outcome the world invented would be an outcome the player did not play."* And: *"inventing a world consequence the hook does not declare would be this module deciding play."*

**On resolutions running first.** *"Resolutions describe what the player did before this span, so they are applied before it runs. Applying them at the end meant the world could expire a quest during the very span the player spent completing it, and then refuse the completion."*

**On the union walk.** *"Walking hooks alone left an open quest for every hook that vanished, immortal and growing."*

**On the unresolvable target.** *"An unresolvable kind reads as present: a quest wrongly left open is a smaller lie than a quest silently reaped because this module did not recognise its target."*

**On the late take.** *"Not pedantry. `_quest_step` tests the window only while a quest is `offered`, so a take applied after the window closed but before the next sweep would make the offer immortal: it leaves `offered`, and the expiry branch is never reached again."*

**On the remainder.** *"Dropping it made `advance(150y)` differ from `advance(100y)` then `advance(50y)` — a counterexample to the invariant that needed no band table to produce — and left the operation log claiming an elapsed the clock never moved."*

**On the instant band's structural sharing.** *"A full deep copy costs the better part of two seconds on a size-33 world and grows with the square of the grid; a game asking what time it is every frame cannot pay that, and paying it to change a clock and twelve floats would make the band's whole point — that a moment is cheap because nothing can happen in it — a fiction."*

**On the villain cast.** *"A villain rises, holds or falls at an age boundary and nowhere else: `advance` reads the threat assessment an age inherited, so raising one mid-tick would let a villain be raised by its own damage. The outlook is a reading of present ground and is safe to refresh; the cast is not."*

**On the work ceiling's axis.** *"This guard exists because the one the module had was on the wrong axis. `cost_estimate`, `age_gate` and the ages-per-request cap were all reached only when `band(days) == AGE`, so a span below that boundary was executed however long it was — and decision 028 moved that boundary from a century to five thousand years, putting a fifty-fold cost increase on the unguarded side of it. A caller could ask for 4,999 years, get 1.8 million cadence steps, and reach none of the machinery built to say what that would cost."*

**On the ceiling not scaling.** *"A ceiling read out of the cost model instead would move every time somebody re-measured — the same request refused on Monday and run on Tuesday, with no constant edited — which is worse than a ceiling that is honest about being one number."*

**On the age gate's staleness.** *"This used to hard-code `size > 257` and name `validate_age_world` as the source — a ceiling that had already been raised to 1025, in a check that never contained the number. A caller debugging it would have found nothing at the named location, and the whole 258..1025 band was refused a span it would have advanced happily. Asking the real predicate is the only version of this that cannot go stale again."*

**On the memory basis mismatch.** *"Memory has its own reference size, and that is deliberate rather than untidy... the size-33 reading came back smaller per cell than the size-17 one, which is the instrument and not the world... Pretending both quantities share one reference would state a precision neither has."*

**On the old cost constants.** *"Wrong by nearly three orders of magnitude, in the direction that tells a caller an expensive call is free."*

**On the per-age divisor.** *"`age_advance_total` is the wall time of a whole call, and a call may have carried up to ten ages. Dividing by the ages that call actually advanced is what makes it a per-age figure; reading it raw reported a ten-age run as the cost of one."*

**On warnings.** *"`add_seasonal_food` ends with `result['warnings'].append(...)`, so it is not quite the wholesale replacement the rest of its body is: every run leaves another copy of the same sentence. Run once at generation that is invisible; run on a cadence it turns a list of limitations into a counter."* The conformance record adds: *"This was caught by `test_a_year_in_four_seasons_is_a_year`, not by reasoning, and a future pass that appends to a shared list will need the same treatment."*

**On the ley-touched flag.** *"Only claim the field moved if it did. `_ley_drift` returns early on a world with magic disabled, and setting the flag anyway sent a magicless world into `evaluate_networks`, which indexes `result['magic']` and died on a bare KeyError rather than any refusal this boundary knows how to make."*

**On the two resolvers.** *"The two are held to the same answer by a test rather than by a shared function, because the shared function would have to be the wider one and widening `_giver_index` would change which people the quest lifecycle considers."*

**On the liveness depth trap.** *"`hero_generator` writes a block-level `status` of `'ok'` or `'failed'` and a person-level `status` of `'living'` or `'legend'` under the same key name, at two different depths of one block. `npc_roster` does the same. A predicate that matched on the string alone would read a failed block as a living person."*

**On absent status.** *"A record written before a package recorded departure at all described someone who stood... every world generated before the fall pair landed would be read as a world of ghosts."*

**On a sixth vocabulary.** *"There are already five and a sixth claiming to unify them would be the sixth."*

**On the contested uid.** *"No generated world has one today; that is a fact about the uid minters, not a guarantee any of them makes."*

**On why the routes exist at all.** *"Once a consumer edits a world outside a request boundary, every invariant the boundary exists to enforce stops being enforceable."* And: *"Taking a quest happens at an instant, not over a span, and a caller that only wants to record an acceptance should not have to name an elapsed time it does not mean."*

**On `refused_by_world`.** *"A caller that cannot tell the two apart will keep correcting a value that was never the problem."*

**On difficulty's banding.** *"The banding is `floor(x + 0.5)` rather than `round`, because CPython rounds halves to even and a rubric that sends 2.5 down and 3.5 up reads as arbitrary to anyone checking it by hand."*

**On difficulty's ceiling.** *"The rubric reaches 5 only through what stands at the target, never through the ground around it, so a long walk past a lair can never turn an errand into a campaign threat."*

**On the reward.** *"It never names a reward. The generator emits no loot, no XP and no treasure table; `difficulty` is the one number a consuming game prices, and pricing it is that game's job, not this one's."*

**On `history.replay`.** *"Rewording it here would make the key flip between two strings depending on which call ran last, which is churn in the bytes of a document whose byte-reproducibility is a contract."*

**On why `MAX_AGES_PER_REQUEST` stays beside `MAX_TICK_STEPS`.** *"A work ceiling and an ages ceiling refuse different things — one prices a tick, the other bounds a loop of whole-world rebuilds this module does not execute itself."*

**On what the whole capability fixes.** *"There were seven request-shaped mutators before this one and no clock between them... a swarm that should erupt in a bad year erupts in every year because there are no years."*

## Notes for the C++ implementation

**The big structural mismatch: this subsystem does not read a `Grid`.** It is document-level code over `World`'s typed blocks. `FWGC++` today has `World` as a struct of layers, plates and reports, with a JSON *writer* and no reader. A tick is world-in/world-out over a *persisted* document, so a C++ port needs either (a) a JSON reader so a world can round-trip, or (b) the tick to operate on a live `fwg::World` in memory with serialisation only at the edges. (b) is much less work and loses nothing the game needs, but it changes the API shape from stateless-request to in-process mutation, and the all-or-nothing failure guarantee then has to be bought by copying the `World` explicitly. **Decide this before writing a line.**

**`fwg::Grid` and `sphere_grid` are not used here.** Nothing in these three modules touches the seam column, the pole rows or the node graph. The only geometry that leaks in is `_pressure` in the hook generator (grid metres via `spacing_m`), which is not part of this assignment.

**`fwg::Rng`.** Python builds a *fresh* `random.Random(seed)` per quest sweep and per school-year, then draws from it. Map that to constructing `fwg::Rng(child_seed64(cfg.seed, domain, index))` at the same points. **`Rng::child(master, domain, variation)` is exactly the right primitive** — it is the fourth-derivation-forbidden rule's intended use. Do not add a fifth derivation for "time" domains; the domain string carries that. The domain strings are `"time-quests"`, `"time-ley-" + school`, `"time-" + cadence_name`.

Values will differ (xoshiro256++ vs Mersenne Twister, and `Rng::uniform` is hand-rolled). That is fine — this is a clean rewrite, functionality must match and numbers need not. What must match is the *structure*: one generator per (domain, absolute index), one draw per opened quest in walk order, one draw per node then per edge.

**`variation` is `std::int64_t` and the index can be large.** A day index at the top of the `LONG` band is ~1.8 million; a year index ~5,000. Both fit comfortably, but the `child_seed` string form must serialise the integer the same way every time — Python formats it as a plain decimal. Use `std::to_string`, not a locale-sensitive formatter.

**`fwg::Config` needs new fields** or a sibling struct: `world_recipe`, `phase` (exists), `magic_enabled`, and the `options()` resolution that supplies `villain_rise` / `villain_density`. The Python `replace(cfg, seed=...)` idiom (a frozen-dataclass copy with one field changed) maps to copying `Config` by value and assigning `.seed` — cheap, and `Config` is already a value type.

**Ordered containers everywhere.** Python dict = insertion-ordered. Use `std::vector<std::pair<K,V>>` or `std::map` and be explicit about which you mean:
- `magic.networks` — Python iterates in insertion order. `std::map<std::string, Network>` gives *sorted* order instead, which changes JSON key order and the order `lunar_surge.factors` is written. If byte-reproducibility across the two implementations does not matter (it does not, per the brief), `std::map` is fine and is *more* deterministic. Pick one and write it down.
- `by_hook` and `tracked` in `_quest_step` — **must preserve `heroes.quest_hooks` list order**, because that order decides which new quest draws which offer window. Build them as a `std::vector` of hooks plus a `std::unordered_map<std::string, std::size_t>` index, never as a map you iterate.
- `block.quests` — a `std::vector` sorted by `quest_id` at the end of each sweep with `std::stable_sort` and plain `std::string` `<` (byte-wise, matching Python's code-point comparison for ASCII ids, which all hook ids are).
- `by_cadence` output — explicitly sorted by name.
- Every `sorted(set(...))[0]` in a refusal message — `std::set<std::string>` and take `*begin()`.

**Typed blocks instead of dicts.** `quests`, `quest_actions`, `person_state`, `world_clock` and the `history.operations` entries should be structs in `fwg::World`. The one place this is awkward is `history.operations`, which is heterogeneous (time ops, age ops, quest ops, person ops, and the age route's own entries). A tagged variant or a struct with a `kind` string and optional fields is the honest shape; the Python is a bare dict per kind.

**`std::optional` for the nullable quest fields.** `anchor`, `verb`, `stated_purpose`, `giver_uid`, `closed_day`, `closed_reason` are all `T|null` in the published schema and the null cases are reachable (a pre-heroes-2 ley hook always produces a null verb). Do not collapse null to empty string — the read schema distinguishes them.

**The error envelope has no C++ analogue yet.** `terrain_errors` produces a document with `{schema, schema_version, code, message, field, received, expected, suggestion, retry}`, where `received` and `expected` are arbitrary JSON. `FWGC++` has a JSON writer but no variant JSON value type. Either add a minimal `JsonValue` variant, or make the refusal a struct with the handful of shapes actually used (`clamp` with a number or a `{days:n}` object, `choose` with a string plus candidates, `did_you_mean` with candidates, `none`). The latter is smaller and loses nothing. Note the `nearest()` suggestion helper uses `difflib.get_close_matches` (Ratcliff-Obershelp, cutoff 0.6, n=5) with prefix and substring matches folded in *ahead* of it; a C++ port can substitute any reasonable fuzzy match, since the suggestion is advisory and its exact ranking is not contractual — but keep the prefix-then-substring-then-fuzzy precedence, which exists because `difflib` alone is good on a typo and poor on a bare prefix.

**Two refusals are returned, not thrown.** The age gate and the work ceiling come back inside `time_advance.blocked` with `committed: false`, and the world comes back otherwise unchanged. If the C++ uses exceptions for the raised refusals, these two must still be a normal return. A `std::expected`-shaped result for the whole call, with the blocked case as a *successful* return carrying a report, is the cleanest mapping.

**`perf_counter()` → `std::chrono::steady_clock`**, and remember `timing_ms` is excluded from every determinism comparison because it is the only reason two runs of one seed differ.

**`copy.deepcopy` of the whole world minus `build_stages`.** In C++ this is a copy constructor; `build_stages` (shared, not copied) maps to a `std::shared_ptr<const std::vector<Stage>>` or an index into a side table. The measured cost is ~1 s and 51 MB at size 17, growing with cell count squared — in C++ it will be far cheaper, which may mean `MAX_TICK_STEPS` and the whole `tick_cost_estimate` basis should be **re-measured rather than ported**. Port the *structure* of the estimate (two terms, fixed copy plus marginal step, both scaled by cell count, with `precision` saying steps are a proxy) and replace the five numbers.

**The structural-sharing optimisations (`_instant`, the quest route, the person route) are a Python refcount trick** — they return a shallow-copied top-level dict whose unwritten values alias the caller's. In C++ with value semantics they are either (a) unnecessary, because the whole `World` is copied cheaply enough, or (b) expressed as copy-on-write handles per block. Do not try to reproduce the aliasing literally; reproduce the *guarantee*, which is that the caller's world is unchanged on every path including every refusal.

**Recursion.** The age band calls `advance_time_request` on itself for the remainder. Depth is at most 1 (the remainder is always below `AGE_DAYS`), so no trampolining is needed, but the C++ version must not re-run the work ceiling in a way that double-charges.

**`math.floor` on a double.** `crossings` uses `int(math.floor(now / period)) + 1`. Use `std::floor` then cast, not integer division, and not `static_cast<int>` on a negative (days are non-negative here, but `now` is unbounded above — at 1.8 million days with period 1.0 the index still fits an `int`, but use `std::int64_t` and stop worrying).

**`json.dumps(w, sort_keys=True, default=str)` in `_dedupe_warnings`.** The dedupe key is a canonical serialisation of a warning. If warnings are plain strings in C++ (they are, in `World::warnings`), this collapses to a simple seen-set of strings preserving first-seen order.

**Things that are *missing* from the C++ and must be built before this can be ported at all:** `heroes` with `quest_hooks`, `npcs`, `villains`, `magic.networks`, `astrology.moon` and `tide`, `settlements.sites`, `beast_nests.sites`, `history.ages`, `advance_age_request`, and the six cadence passes. This subsystem is glue; it is close to the *last* thing to port, not an early one. What can be built early and independently, with real value, is Part A — the schedule is pure, has no dependencies, and its invariants are exactly the ones a test can pin without generating anything.

## Open questions

**1. `cost_estimate` reads the wrong operation.** When `timing_ms.age_advance_total` is present it divides by `carried`, taken from `previous[-1]` — the *last* operation carrying `steps` or `ages`. But the tick appends its own `{kind:'time', steps:N}` operation after every advance, so on any world that has been ticked since its last age advance, `carried` is the tick's **step count** (potentially 200,000) rather than the age count. `per_age` then comes back near zero and `measured: true` claims it. The filter `op.get('steps') or op.get('ages')` was clearly meant to find the age operation; it finds whichever qualifying operation is last. A porter should either key on `op.get('kind') == 'time' and op.get('band') == 'age'`, or record the per-age cost directly. **I did not verify this by running it** — it is read from the code.

**2. `MAX_AGES_PER_REQUEST = 50` is explicitly unresolved.** The number was chosen when fifty ages was five thousand years; it is now two hundred and fifty thousand, which at the one measured size is over two hours of age advances. Decision 028 left the question open and the module says so. A porter must pick a number and should not assume 50 carries meaning.

**3. A resolution cannot cross an age turn, and the refusal is the wrong shape.** Resolutions ride the remainder tick, which runs after the ages, so the board it sees is the new age's. A caller sending an age span plus a resolution for an outgoing-age quest gets `unknown_field` naming the quest — a *malformed-argument* refusal for a request that was well formed. It should be `refused_by_world`, as a quest that merely closed would get. The Python records this as known and does not fix it.

**4. `_villain_outlook` defaults disagree with the option table.** It passes `float(o.get('villain_rise', 0.))` but `world_options` declares `villain_rise` with a default of `0.5`. `options(cfg)` normally resolves the option so the fallback is unreachable, but a world whose options block is incomplete would silently get zero rise instead of the declared default. `villain_density` defaults to `3.` in both places and is consistent.

**5. `validate_changes` reports a missing field as `unknown_field`.** `unknown_field(missing[0], FIELDS, noun='person change field')` produces the message *"No person change field named 'day'"* for a change that simply omitted `day`. The code is `INVALID_INPUT` and the field name is right, but the sentence tells a caller the opposite of what happened. Cosmetic, but a small model reading the prose is the declared consumer of this envelope.

**6. Nothing cascades from a person state change, and three tickets share that question.** No tombstone is written, no claim decays, no post is vacated, no roster mirror moves, a fallen villain keeps its claims and stays out of `villains.fallen`. The repo's own process requires those to be answered together and they were not answered. A C++ port inherits an open design decision, not a completed one.

**7. Pooled vs serial settlement planning is not relevant here** — neither settlement planner is reachable from these three modules. Flagged only to say it was checked.

**8. `ICARUS_CITY_WORKERS` does not reach this subsystem** either, for the same reason.

**9. `clock()` never refreshes `derived_from` or `epoch_day`.** Once a world carries a clock, `derived_from` keeps naming how the *first* clock was derived, forever, including across an age advance that moved `settlements.founding.end_year`. Probably intentional (it is provenance, not state) but it is undocumented.

**10. Whether the tick's `world_clock.age` and an age advance's `history.ages` can disagree.** The tick sets `age = len(history.ages or [])` on every call, and the age band overwrites it from `len(advanced.history.ages)`. These agree today. A caller that turns an age through `POST /world/advance-age` directly, then ticks, gets a corrected age but **carries its quest board across unchanged, including the quests whose `hook_id` the regenerated cast re-mints** — the chimera described above. The reap belongs to `advance-time` alone and the age route does not do it. That is stated in the docs as a known gap, not a defect to fix, but a C++ port that unifies the two routes should reap in both.

## Addenda from the audit

The reader above wrote the specification; a second reader went back to the source
to find what it had left out or got wrong. Both are kept: the omissions are
content the implementer needs, and the corrections say where the specification
above is not to be trusted.

### Corrections

- **Claimed:** `_target_present`: `kind == 'nest' -> any(site.id == (effect.nest_id or effect.uid) for site in beast_nests.sites)`
  **Actually:** The source is `nest_id = effect.get('nest_id', uid)` — a dict `.get` with a default, not an `or`. The fallback to `uid` fires only when the key `nest_id` is ABSENT. An effect carrying `nest_id: null` (or an empty string) resolves to `None`/`''` in the source and would resolve to `uid` under the spec's `or`, so the two disagree on exactly the malformed-hook case: the source then matches no nest and closes the quest `target_gone`, the spec's version matches by uid and leaves it open. Full source: `kind = effect.get('kind'); uid = effect.get('uid'); if kind == 'city': return any(site.get('uid') == uid for site in result.get('settlements', {}).get('sites', [])); if kind == 'nest': nest_id = effect.get('nest_id', uid); return any(site.get('id') == nest_id for site in result.get('beast_nests', {}).get('sites', [])); return True`

- **Claimed:** `_timing(result)` is `result.timing_ms.setdefault, timing_ms['total'] = 0.`
  **Actually:** Both operations are setdefaults: `timing = result.setdefault('timing_ms', {}); timing.setdefault('total', 0.); return timing`. An existing `timing_ms['total']` is preserved, not zeroed. The spec's rendering reads as an unconditional assignment, which would destroy the generation total on every tick. `_timing` also returns the dict, which the close-out uses as `_timing(result)['time_advance_total'] = (perf_counter() - started) * 1000` — i.e. it is called a second time at close-out.

- **Claimed:** "`BAND_CADENCES` survives only as a label for the report and as documentation of what a span of each size typically touches."
  **Actually:** Nothing reads it. A grep over the whole `Sim/` tree finds `BAND_CADENCES` only at its own definition and in the comment above it in `terrain_time_schedule.py`; `plan()` emits `band` (the string `'instant'|'day'|'season'|'long'|'age'`) and no report field ever carries a cadence list. It is dead documentation only. (The source's own comment makes the same overstated claim, so the spec inherited it — but a porter told it is "a label for the report" will hunt for an output field that does not exist.) For completeness the table is `{INSTANT: (), DAY: ('quests','encounters','beast_movements','nomad_routes'), SEASON: ('quests','encounters','beast_movements','nomad_routes','seasonal_food','ley_drift','nomads','nomad_effects','villain_outlook')}` with `BAND_CADENCES[LONG] = BAND_CADENCES[SEASON]`.

- **Claimed:** `actions = validate_actions(body.actions or [])`, `changes = validate_changes(body.changes or [])`, and (in B1) `leyline_edits`/`resolutions` default to `[]`.
  **Actually:** All four use `body.get(<name>, [])` — the default applies only when the key is ABSENT. An explicit JSON `null` is NOT defaulted: it reaches the list check and is refused as `wrong_type('actions', None, {'type':'array','description':'a list of quest actions'})` (resp. `'changes'`/`'leyline_edits'`/`'resolutions'`). The spec's `or []` silently accepts `null` as empty, which is a different published contract.

- **Claimed:** Person-route note 3 (given as an exact, published string): "A villain marked gone keeps its claims, its held nodes and its absence from villains.fallen. The bookkeeping an age boundary does around a fall is not run here."
  **Actually:** The source string continues: "... is not run here; see board/backlog/VILLAIN-FALL-UNRECORDED.md." The spec truncates a string it declares exact. (Dropping a repo-path reference is defensible for a port, but the spec should say it is dropping it rather than present the truncation as the published text.)

- **Claimed:** "`quest_request` ... `alternatives = [q.quest_id for q in quests if q.state in ('offered','taken') and q.quest_id is a str]`"
  **Actually:** The source also requires `isinstance(q, dict)` before reading `.get('state')`: `[q['quest_id'] for q in quests if isinstance(q, dict) and q.get('state') in OPEN_STATES and isinstance(q.get('quest_id'), str)]`. A hand-persisted board containing a non-dict row crashes under the spec's version and is skipped under the source's. Same omission in `by_id`, which the spec does get right.

### Missing — blocking

**`_apply_surge` has an early-return guard the spec's C6 pseudocode omits entirely**

```
def _apply_surge(result, day):
    magic = result.get('magic')
    astrology = result.get('astrology')
    if not isinstance(magic, dict) or not isinstance(astrology, dict):
        return
    factors = tide(astrology['moon'], day)
    for name, net in (magic.get('networks') or {}).items():
        if name in factors:
            net['surged_strength'] = net['strength'] * factors[name]
    magic['lunar_surge'] = {'day': day, 'factors': dict(factors),
                            'formula': 'surged_strength = strength * tide_school(day)'}
```
The spec's C6 starts at `factors = tide(astrology.moon, day)`. `_apply_surge` is called on EVERY path that advances: the `_instant` band, the full-path close-out, and the age band's zero-remainder branch. The spec itself documents `magic_enabled == 0` as a supported world, and such a world carries no `magic` block — so without this guard every single tick of a magicless world dies on `None.get('networks')`. It is also the guard that makes `networks or {}` safe. Note too that `astrology['moon']` is a bare subscript: the gate only requires `astrology` to be a dict with `version == 1`, so a sky block without `moon` raises a bare `KeyError` here with no failure envelope.

**`_dedupe_warnings` has a type guard, and the spec's C8 pseudocode iterates unconditionally**

```
warnings = result.get('warnings')
if not isinstance(warnings, list):
    return
seen, out = set(), []
for warning in warnings:
    key = json.dumps(warning, sort_keys=True, default=str)
    if key in seen: continue
    seen.add(key); out.append(warning)
result['warnings'] = out
```
A world with no `warnings` key (or a non-list one) is left exactly as found — no key is created. The spec's version (`for w in result.warnings: ...` then `result.warnings = out`) crashes on the first world without warnings and, worse, would materialise an empty `warnings: []` on every world that has none, changing the bytes of a document whose byte-reproducibility the spec repeatedly calls a contract. `_dedupe_warnings` runs after every one of the six `CADENCE_PASSES`, so this is on the hot path.

**The whole malformed-world error surface of `validate_time_world`: a try/except that rewrites five exception types into one refusal**

The body of the gate after the `isinstance(world, dict)` check is wrapped:
```
try:
    cfg = Config(**world['config'])
    if cfg.world_recipe != 3 or cfg.phase < 13:
        raise missing_block('config', 'Time advance requires a recipe 3 world generated through '
                            'creatures (phase 13); this world is recipe %s at phase %s.'
                            % (cfg.world_recipe, cfg.phase))
    if world['terrain']['version'] != 6 or world['generator_version'] != 16 or world['recipe']['version'] != 3:
        raise retired_version('world.generator_version', world.get('generator_version'), (16,))
    astrology = world.get('astrology')
    if not isinstance(astrology, dict) or astrology.get('version') != 1:
        raise missing_block('astrology', 'This world predates the moon contract and carries no sky '
                            'to advance; regenerate it with recipe_version 3.')
except (KeyError, TypeError, IndexError, OverflowError) as exc:
    raise wrong_type('world', type(exc).__name__,
                     {'type': 'object',
                      'description': 'a complete recipe 3 world; ' + str(exc)}) from exc
return cfg
```
Consequences an implementer must reproduce and cannot guess: (1) a missing `config`, `terrain`, `recipe` or `generator_version` key, and a `config` dict carrying a key `Config` does not accept, all come back as ONE refusal — `wrong_type` on field `world` whose `received` is the *exception class name as a string* (`"KeyError"`, `"TypeError"`), with the exception text appended to `expected.description`; (2) `RequestError` subclasses `ValueError`, so the `missing_block` and `retired_version` raised inside the try are NOT swallowed by the except clause and reach the caller intact; (3) the first refusal, on a non-dict world, is `wrong_type('world', world, {'type':'object','description':'a generated recipe 3 world document'})`. The spec gives the gate's predicates but nothing about what it raises, so a porter inventing an error surface here will diverge for every malformed world.

**`edit_network` refuses with BARE `ValueError` (no failure envelope), and `intensity` is mandatory for a node/line edit**

```
def edit_network(network, *, node_id=None, line_id=None, intensity=None, new_node=None):
    net = copy.deepcopy(network)
    if sum(v is not None for v in (node_id, line_id, new_node)) != 1:
        raise ValueError('Select one node, line, or new key point')
    if new_node is not None:
        if not isinstance(new_node, dict) or set(new_node) != {'id','direction','intensity'}:
            raise ValueError('New node requires id, direction, intensity')
        p = new_node['direction']
        if not isinstance(new_node['id'], str) or not new_node['id'] or any(n['id'] == new_node['id'] for n in net['nodes']):
            raise ValueError('Node id must be unique and nonempty')
        if not isinstance(p, (list, tuple)) or len(p) != 3 or any(type(v) not in (int,float) or not math.isfinite(v) for v in p):
            raise ValueError('Direction must be a finite unit vector')
        if abs(sum(v*v for v in p) - 1) > 1e-6:
            raise ValueError('Direction must be a unit vector')
        intensity = new_node['intensity']
        target = copy.deepcopy(new_node)
        net['nodes'].append(target)
    else:
        collection = net['nodes'] if node_id is not None else net['edges']
        target = next((v for v in collection if v['id'] == (node_id if node_id is not None else line_id)), None)
        if target is None:
            raise ValueError('Unknown key point or line')
    if type(intensity) not in (int,float) or not math.isfinite(intensity) or not 0 <= intensity <= 4:
        raise ValueError('Intensity must be finite and between 0 and 4')
    target['intensity'] = intensity
    return net
```
The spec says `edit_network` "requires `0 <= intensity <= 4` finite" but never says it is REQUIRED: `{'school': 'fire', 'node_id': 'n3'}` with no `intensity` is refused, because the final check runs on `intensity=None`. Nor does it say `{'school': 'fire'}` alone is refused. Most importantly, every one of these is a bare `ValueError`, not a `RequestError` — so leyline edits are the ONLY refusals reachable from `advance_time_request` carrying no `{schema, code, message, field, received, expected, suggestion, retry}` document. A porter following the spec's error-envelope section would wrongly envelope them. Note also `edit_network` deep-copies the whole network per edit, which is why the 128-edit batch bound matters.

### Missing — important

**A committed age span with an unaffordable remainder advances the ages, silently does not tick the remainder, and returns a world whose clock contradicts its own report**

With `commit: true` the B7 work ceiling is skipped (`tick_steps > MAX_TICK_STEPS and not commit` is false). The age branch then runs `_advance_ages`, sets `clock_after['day'] = now['day'] + ages*AGE_DAYS`, turns the board, logs the age operation, and calls itself for the remainder:
```
advanced = advance_time_request({'api_version': 1, 'world': advanced,
                                 'elapsed': {'days': remainder},
                                 'leyline_edits': edits, 'resolutions': resolutions})
```
— with NO `commit` key, so the recursion defaults `commit=False`. A remainder above ~200,000 steps (anything past roughly 554 days of daily quest sweeps) therefore trips the recursion's own work ceiling and returns a BLOCKED world: a `copy.deepcopy` of the post-age world with `world_clock` left at the post-age day and `time_advance.committed = False`. The outer frame then overwrites that report with its own, setting `report['committed'] = True`, `report['to_day'] = now['day'] + days` and `report['remainder'] = <the blocked document>`. Net effect: `world_clock.day` says `now + ages*AGE_DAYS` while `time_advance.to_day` says `now + days`, no operation is logged for the remainder, and the top-level report claims `committed: true`. The spec asserts "failure at this boundary is all-or-nothing" and "a span of ages *plus* an unaffordable remainder is reported whole instead of advancing its ages and then baulking" — true only for `commit: false`. A C++ port should either propagate `commit` into the recursion or re-check the remainder before `_advance_ages`; either way the behaviour must be recorded, because it is exactly the case the spec says cannot happen.

**One `retired_version` refusal covers three different version mismatches and names the wrong field for two of them**

`if world['terrain']['version'] != 6 or world['generator_version'] != 16 or world['recipe']['version'] != 3: raise retired_version('world.generator_version', world.get('generator_version'), (16,))`. A world with `terrain.version == 5` but `generator_version == 16` is refused with `field='world.generator_version'`, `received=16`, `expected={'choices':[16]}`, `retry=False` and the message "world.generator_version 16 is retired; this producer accepts 16. Regenerate rather than migrating." — a refusal that names the one version that was correct, tells the caller it is retired, and then lists it as the accepted value. The spec's declared consumer is a small model reading the prose, which cannot act on that. The spec lists the three version pins but not that they collapse into one misattributed refusal.

**The exact published prose strings emitted in `cost_estimate` and `tick_cost_estimate`, which the spec elides as `<the long extrapolation sentence>` / `<sentence>`**

These are fields of a returned document, not comments. `cost_estimate` fallback `basis`: "extrapolated by cell count from age advances timed directly on 2026-09-21 (34.3-39.4 s at size 17 and 181.8 s at size 33, seed 42 through phase 16, on a contended development machine, so upper bounds); the seconds take size 33 as the reference because the cost grows faster than cell count between the two, and nothing has been timed above size 33". `cost_estimate.peak_mb_precision` (written on both branches): "566 MB peak working set measured at size 17, extrapolated by cell count. The measurement is a lower bound, because the machine was trimming working sets under load; the extrapolation is an over-estimate of unknown size, because the two measured world documents grew 3.0x for 3.8x the cells rather than in step with them. Nothing above size 33 has been measured at all, and no age advance above size 33 has been run". `tick_cost_estimate.basis`: "measured 2026-09-21 on seed 42 through phase 16, on a contended development machine: at size 17, 72,207 steps in 2.05 s and 1,804,646 in 26.81 s, so 14.2 us a step over a 1.0 s copy; at size 33 the same spans took 6.47 s and 79.90 s, so both terms are scaled by cell count, which over-reports size 33 by about a fifth". `tick_cost_estimate.precision`: "order of magnitude; steps are a proxy for cost, because a coalesced pass runs once per call however many crossings it had while the daily quest sweep runs every day".

**The exact `missing_block` message strings, which are the refusal prose a consuming model reads**

Gate, recipe/phase: "Time advance requires a recipe 3 world generated through creatures (phase 13); this world is recipe %s at phase %s." (field `world.config`). Gate, sky: "This world predates the moon contract and carries no sky to advance; regenerate it with recipe_version 3." (field `world.astrology`). `_apply_resolutions` and `quest_request` use the same sentence with a different verb: "This world carries no quests to resolve. Advance it by at least one day first, which is what opens them." and "This world carries no quests to act on. Advance it by at least one day first, which is what opens them." Person route: "This world carries no person lists to change. heroes, npcs and villains are written at generation; a world whose cast packages failed has none." Every `missing_block` sets `field = 'world.' + block`, `received = None`, `expected = {'requires_block': block}`, `suggestion = {'kind':'none'}`, `retry = False`.

**The `noun` argument of every `over_capacity` refusal, which appears verbatim in the emitted message**

`over_capacity(field, value, limit, noun)` formats "%s %r is above the %s maximum of %s." and sets `code='STATE_CAPACITY'`, `expected={'max': limit}`, `suggestion={'kind':'clamp','value': limit}`, `retry=True`. The nouns used by these modules: `leyline_edits` → 'edits per request'; `resolutions` → 'resolutions per request'; `elapsed`/steps → 'cadence steps per request'; `elapsed`/ages → 'ages per request'; `actions` → 'quest actions per request'; `changes` → 'person changes per request'; `reason` → 'characters'. The spec gives every limit but no noun, so the emitted messages would differ.

**Full `RequestError` envelope shape, the `detail=False` tier, and the five-item truncation of `alternatives`**

`document(detail=True)` = `{schema: 'fantasy-world-generator.failure', schema_version: 2, code, message, field, received, expected, suggestion (defaulting to {'kind':'none'}), retry}`; `document(detail=False)` returns only the first five fields and exists so an evaluation harness can measure whether `suggestion` actually reduces a small model's retry count — "a field that cannot be switched off cannot be measured." Codes are exactly `INVALID_INPUT`, `UNSUPPORTED_VERSION`, `STATE_CAPACITY`. `suggestion.kind` is one of `clamp`, `choose`, `did_you_mean`, `none`. Critically, `refused_by_world` sets `suggestion = {'kind':'choose','value': alternatives[0], 'candidates': list(alternatives)[:5]}` — **truncated to five candidates** — which none of the spec's several `alternatives=[...]` renderings mentions, so a port would emit an unbounded list into a refusal.

### Missing — minor

**`cost_estimate` reads `world['config']['size']` unguarded, unlike its sibling**

`size = float(world['config']['size'])` — a bare double subscript. `tick_cost_estimate` guards the identical read: `float((world.get('config') or {}).get('size') or TICK_BASIS_SIZE)`, falling back to `TICK_BASIS_SIZE = 17.`. The gate has already constructed `Config(**world['config'])` by the time `cost_estimate` runs and `size` is a `Config` field, so it is safe in practice; the asymmetry is worth carrying because it tells a porter the two helpers are not interchangeable.

**`terrain_time_schedule` signatures and guards the spec's Part A drops**

`steps(now, days, allowed=None)` takes a third parameter: `names = CADENCE_PERIOD if allowed is None else allowed`. It is the residue of the band-filtering data-loss defect the spec describes at length, and `terrain_time` never passes it — a port should drop it, but should know it existed so the removal is deliberate. Also: `steps` and `by_cadence` both raise `ValueError('days must be >= 0')` on a negative span, and `longest_span` raises `ValueError('limit must be >= 0')`. These are bare ValueErrors, not envelopes; `days` cannot be negative by the time `terrain_time` calls them, because `elapsed_days` already refuses `value < 0` with a `wrong_type`.

**`transition`'s refusal message has a formatting defect that reaches the caller**

`'Quest %s is already %s and cannot be %sn.' % (quest.get('quest_id'), state, action)` — the trailing `n` is appended to the raw action word, so `take` → "cannot be taken." but `abandon` → "cannot be **abandonn**." The spec renders this as "cannot be An.", which hides the defect; an implementer reading that will write "cannot be abandoned." and silently change a published string. Decide deliberately which the port emits. The other two are clean: "Quest %s has already been taken." and "Quest %s was offered until day %s and day %s is past that; the world has already let it lapse."

**The `hook_id` minting format, which is the mechanism behind the age-turn chimera the spec spends a paragraph on**

`hero_generator.hooks` builds `'hook_id': f"hook-{person['uid']}-{index_of}"` — the giver's uid plus that giver's zero-based hook ordinal. That is precisely why a regenerated cast that re-mints positional uids (`hero-reeve-hamlet-node-<n>`, `hero-sovereign-<city uid>`) re-mints hook ids too, and why `by_hook.get(hook_id)` matched a *different* hook after an age turn. The spec asserts the consequence without giving the id format, so a porter who mints quest ids differently (a UUID, say) would not realise they had just removed `_turn_age_board`'s entire reason for existing — and might then drop the reap, reintroducing the measured 10-of-49 survivals.

**`_target_present` will raise an unenveloped `AttributeError` on a present-but-null settlements or beast_nests block**

`result.get('settlements', {}).get('sites', [])` — `.get` with a default only covers an ABSENT key. A world carrying `settlements: null` raises `AttributeError` on `None.get`, which escapes the request boundary with no failure document. Same shape for `result.get('beast_nests', {})`. Contrast the defensive `(result.get('heroes') or {})` used two functions away in `_apply_resolutions`; the inconsistency is in the source, not a reading error, and a C++ port with typed blocks removes the hazard for free.

**`terrain_person_api.index` silently ignores a record whose `uid` is not a string, while `_giver_index` does not**

Person route: `if isinstance(record, dict) and isinstance(record.get('uid'), str): found.setdefault(record['uid'], []).append((key, listing, block, position))`. A person record with a non-string uid is invisible to the route — it can never be the target of a change, and it is absent from the `sorted(known)` candidate list in the `unknown_field` refusal. `terrain_time._giver_index` applies no such filter: `uid = record.get('uid'); if uid is not None and uid not in index`. So the two resolvers the spec says are "held to the same answer by a test rather than a shared function" in fact accept different uid types. Record it alongside ordering hazard #10.

**`validate_changes` checks unknown fields BEFORE missing fields, and `missing` is in declaration order, not sorted**

```
extra = sorted(set(item) - set(FIELDS))
if extra: raise unknown_field(extra[0], FIELDS, noun='person change field')
missing = [name for name in FIELDS if name not in item]
if missing: raise unknown_field(missing[0], FIELDS, noun='person change field')
```
A change that both misspells a field and omits one reports the misspelling. And `missing` preserves `FIELDS` order — `('uid','status','reason','day')` — so it is NOT `sorted(missing)[0]`: the first missing field in declaration order is named. The spec writes `unknown_field(missing[0], FIELDS, noun='person change field')` without saying what orders `missing`, which is a guess point given how carefully it documents every other `sorted(set(...))[0]` in its ordering-hazard list.
