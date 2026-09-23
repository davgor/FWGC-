# Port specification: 06-astrology

Extracted from the Python `FantasyWorldGenerator` at commit `b67c99b` before that
repository was retired, then audited against the same source by a second reader.

**Source modules**

- `Sim/icarus_sim/terrain_astrology.py`

**Estimated C++ size:** roughly 560 lines, excluding tests.

## Audit: IMPLEMENTABLE

The spec is unusually faithful — I re-derived every constant, formula, field name, dict-key order and exported string from the source and found the ALGORITHM section reproducible essentially line-for-line. All eleven FORMULAS strings, the LIMITS and method paragraphs, the KNOWN_SCHOOLS table with its rgb values, the `child_seed` derivation, the option-registry bounds (0..34 / 0..63 / 0..37 / 0..35 and lunar_influence 0.5/0..1), the `coastal_exposure` semantics, the `1 + influence*sway*(tide-1)` lottery formula, the religion consumers and the relative-vs-absolute event-day trap all check out exactly as written; the hardcoded C++ `region_latitude`/`region_quarter` arrays are correct; and the claim that a test asserts `moon_tide == 1.8` does not contradict the unreachability argument, because that value is an injected `city_fate` fixture rather than a real tide. A competent C++ programmer can build this subsystem from the document alone, so I set implementable=true. The qualification is that two pieces of the document are actively wrong in ways a porter would act on: the §9 `reported_year` pseudocode indexes `result['settlements']` directly and would throw at stage 9 on every world, where astrology first runs and no settlements block yet exists (the prose two sections earlier states the correct behaviour, so this is a transcription bug the porter must catch); and the claim that the scheduler imports the calendar from this module is false — `terrain_time_schedule.py` keeps a second, float-typed copy, so 'astrology owns the calendar' is an aspiration, not a fact, and a port that centralises it will silently drop the duplicate. Beyond that, the two omissions worth fixing before the repo goes are the hand-computable fixture from the unit tests (periods 30/40/20, zero offsets, tilt 30, with tide 1.25 exactly at day 0 and 0.7 exactly for the far-side schools) — which is the only self-check available to a rewrite whose numbers are allowed to differ — and the closed form `tide_peak(gamma) = 0.7 + 0.55*(1 + sin 2*gamma)`, which simultaneously explains why TIDE_MAX is exactly 1.8 (it is FLOOR+GAIN, the value at a 45-degree lean), gives the 1.76683 ceiling, and reveals that worlds with a seeded tilt below about 19.77 degrees can never produce a surge at all — a cliff the spec describes as mere rarity.

## Purpose

The astrology subsystem gives the world one moon and turns it into a clock and a magic modulator. The moon is not tidally locked: it has two named hemispheres (Still at moon-latitude +45 degrees, Restless at -45 degrees), each carrying four of the eight known magic schools, one school per 90-degree quarter of longitude. Three seeded integer-day cycles — synodic (phase), spin (which meridian faces the world) and nod (which hemisphere leans toward the world) — beat against each other with a coprime preference, so the compound rhythm ("the great year", the least common multiple of the three periods) is far longer than any one cycle: fully replayable from the exported parameters, long enough to read as chaos. From that geometry the module computes a per-school tide multiplier in roughly 0.7..1.77, a per-cell `lunar_sensitivity` layer saying how far each place's magic sways with the sky, a twelve-month almanac of events (full moons, new moons, surges, hollow nights, hemisphere ascendancies, next grand alignment), and a stateless query API answering "what is the sky and the surge at this instant". The tide is what kills cities: the age lottery reads leyline potencies scaled by the momentary surge, so the moon is a cause of history and not only decoration. Design rule stated in the module docstring: every quantity here is a cycle fraction or a unit vector, never a metre, so world scale cannot move it, and only sin, cos, products, sums, min/max and integer modulo are used — no `pow` — so a C++ mirror can reproduce it.

## Where it sits in the pipeline

`add_astrology(result, cfg)` runs at **stage 9** of the 16-stage ladder, immediately after `generate_networks` (the leyline/magic networks pass) and `refresh_environment`, inside the same stage block. Everything it reads must exist by then:

MUST ALREADY EXIST
- `layers['instability_<school>']` for all eight known schools — written by the leyline pass in the same stage, before astrology.
- `layers['water_type']` — written at the water/hydrology stage (codes 0 dry, 1 ocean, 2 lake).
- `layers['coastal_exposure']` — written by the ecology/environment pass (stage 8), 0..1.
- `magic['networks']` — optional in practice; if absent, the surge fields are simply not written.
- `settlements['founding']['end_year']` — optional; absent at stage 9, present from stage 10 on, and it is what decides the reported year.
- `cfg.seed`, `cfg.size`, `cfg.world_recipe`, `cfg.world_options` (for the `moon_*` overrides).

WHAT DEPENDS ON IT AFTERWARDS
- **Age transitions (history).** `lunar_context()` samples `tide(moon, day)` at `int(settlements.founding.end_year) * 360` (or an explicit `moon_day` from the advance-age request) and multiplies every city's known-school leyline potency by `1 + lunar_influence * lunar_sensitivity[z][x] * (tide_school - 1)` before the fate lottery. A ruin destroyed by a school cause records `evidence.moon_tide`. `history.ages[].moon` records `{day, phase_fraction, elongation_degrees, illumination, leaning, tide}`.
- **Religion.** `aspect_of()` for the god of the Turning Moon averages `months[].nod_degrees` over the twelve almanac months: mean >= 0 makes it Sovereign, else Wild. `feasts()` reads `lunar_almanac.events` for per-school surge days, full-moon days and hollow-night days. `resolve_sites()` reads `history.ages[].moon.tide.weave` to decide whether a self-destruction cult was "born under surge".
- **Time advance.** `_apply_surge(result, day)` recomputes `magic.networks[*].surged_strength` and `magic.lunar_surge` at the clock's day using the same `tide` primitive. `clock()` derives a world's day from `reported_year()` so a tick and a moon query cannot disagree about the time.
- **Visitation (summon a god).** `god_schools()` uses `moon_state(...)['nod_degrees'] >= 0` to pick which hemisphere's four schools an avatar of a moon-affiliated god saturates; the visitation record stores `moon = {day, **day_state(moon, day)}`.
- **Calendar constants for the whole codebase.** `terrain_beast_movement`, `terrain_encounters`, `terrain_nomad_routes` and the scheduler all import `DAYS_PER_YEAR` (360) from this module. Astrology owns the calendar.
- **Re-run points.** `refresh_astrology` (never reseeds) is called at the end of `civilization()`, by `rebuild_tail()` when religion is not re-resolved, and after age transitions. `add_astrology` is called exactly once per world.
- **Load-time validation.** `validate_age_world` (history) re-checks the stored moon on any advance-age/summon: each period must be an int inside its range, each offset an int in `[0, period)`, tilt inside 10..35, and `great_year_days == lcm(periods)`. A world without `astrology.version == 1` and `lunar_almanac.version == 1` is refused with "regenerate rather than migrate".

## Inputs

**Config / options** (via `options(cfg)`, which is `{}` when `cfg.world_recipe` is falsy, so all overrides then read as 0 = seeded):
- `cfg.seed` — 32-bit-ish world master seed.
- `cfg.size` — `n`, the grid edge, for `lunar_sensitivity`.
- `moon_variation` (int, 0..4294967295, default 0) — independent moon seed variation.
- `moon_synodic_days` (int, registry range 0..34, default 0) — 0 means seeded; otherwise pins the synodic period, and must be 26..34 or `seed_moon` raises.
- `moon_spin_days` (int, registry 0..63, default 0) — 0 seeded, else must be 15..63.
- `moon_nod_days` (int, registry 0..37, default 0) — 0 seeded, else must be 17..37.
- `moon_tilt_degrees` (float, registry 0..35, default 0.) — 0 seeded, else must be 10..35.
- `lunar_influence` (float 0..1, default 0.5) — NOT read by this module; read by the age lottery and by religion's facts. 0 leaves the moon a spectator.

**Layers read (all `n x n` arrays of doubles, row index `z`, column index `x`):**
- `instability_weave`, `instability_umbral`, `instability_infernal`, `instability_radiant`, `instability_fire`, `instability_water`, `instability_earth`, `instability_air` — each 0..1 (`min(1, ley_power * network_instability)`). Missing layer reads as all zeros.
- `water_type` — 0 dry land, 1 ocean, 2 lake. Missing reads as zeros.
- `coastal_exposure` — 0..1. For a coastal land cell it is the fraction of graph neighbours that are ocean; for any other cell the fraction of neighbours with any water. Missing reads as zeros.

**World blocks read:**
- `result['astrology']['moon']` — the seeded moon (by `refresh_astrology`, `lunar_request`, and every external consumer).
- `result['settlements']['founding']['end_year']` — optional; `int()` of it is the reported year. Absent => year 0, source `'epoch'`.
- `result['magic']['networks']` — a dict keyed by school name; each value has `strength` (double). Optional; only the eight KNOWN schools get a surge, hidden schools (`blood`, `void`, `rot`, `eldritch`) are skipped entirely.
- `result['lunar_almanac']['reported_year']` — read by `lunar_request` only, as the default year when the caller gives no time.

**Units:** every quantity in this module is dimensionless (cycle fraction, unit-vector component, multiplier) or in days, degrees or hours. There is not one metre anywhere, so nothing here is pre- or post-world-scaling; the module is scale-invariant by design and the docstring says so explicitly.

## Outputs

**`result['astrology']`** (written once by `add_astrology`, never rewritten):
- `version` = 1 (integer; validators compare against exactly 1).
- `calendar` = `{hours_per_day: 24, days_per_month: 30, months_per_year: 12, days_per_year: 360, epoch: 'day 0 is founding year 0; hour 0 is sunrise'}`.
- `moon` = the `seed_moon` result plus static descriptive fields:
  - `periods` = `{synodic: int, spin: int, nod: int}` in days.
  - `offsets` = `{synodic: int, spin: int, nod: int}`, each in `[0, period)`.
  - `tilt_max_degrees` = double in 10..35.
  - `great_year_days` = `lcm(synodic, spin, nod)` (int days).
  - `controls` = `{variation: int, overrides: {<key>: value, ...}}` — only keys actually pinned appear, in the order they were iterated.
  - `hemispheres` = `{still: [radiant, water, earth, umbral], restless: [weave, fire, air, infernal]}`.
  - `regions` = `{school: [x, y, z]}` unit vectors, keyed by school **in KNOWN_SCHOOLS order**.
  - `period_ranges` = `{synodic: [26,34], spin: [15,63], nod: [17,37]}`.
  - `tilt_range_degrees` = `[10.0, 35.0]`.
- `formulas` = a dict of 11 human-readable formula strings (reproduced verbatim in the ALGORITHM section) so a consumer that never reads the docs can still evaluate the sky itself.
- `method` = the prose paragraph describing the body.
- `limits` = the LIMITS string.

**`result['warnings']`** — the LIMITS string is appended (`setdefault('warnings', []).append(...)`). Note downstream `_dedupe_warnings` in the time module exists because passes like this one append on every run; `add_astrology` runs once, so it appends once.

**`result['lunar_almanac']`** (rewritten by every `refresh_astrology`):
- `version` = 1.
- `reported_year_source` = `'founding.end_year'` or `'epoch'`.
- `reported_year` = int.
- `now` = `{day: <absolute day = year*360>, **day_state(moon, day)}` — i.e. `phase_fraction` (0..1), `elongation_degrees` (0..360), `illumination` (0..1), `moonrise_hour` (0..24), `spin_fraction` (0..1), `nod_degrees` (-tilt..+tilt), `leaning` ('still'|'restless'), `tide` (dict school -> double).
- `months` = list of 12, each `{first_day: <absolute>, **day_state(moon, first_day), mean_tide: {school: double}}`.
- `events` = list of dicts sorted by `(day, kind, school or '')`. **`day` here is 0..359 relative to the start of the reported year**, unlike every other day field in the block. Kinds: `full_moon`, `new_moon`, `still_ascendant`, `restless_ascendant`, `surge` (carries `school` and `tide`), `hollow_night`.
- `next_grand_alignment_day` = **absolute** day or `null`.

**`result['layers']['lunar_sensitivity']`** — `n x n` doubles in 0..1. Per-cell, no neighbourhood term, so the longitude seam and pole rows of its inputs carry through unchanged.

**`result['magic']`** (only when `magic` is present):
- `networks[<known school>]['surged_strength']` = `strength * tide_school(now.day)` (double). A hidden school gets **no such key at all** — the asymmetry is deliberate and is asserted by tests.
- `lunar_surge` = `{day: <absolute day>, factors: {school: double}, formula: 'surged_strength = strength * tide_school(day)'}`.

**`result['timing_ms']['astrology']`** — accumulated with `+=`, not assigned, so repeated refreshes sum.

**Key fields / id fields:** this subsystem has no entity ids. Its dict keys are school **names** (strings, the eight KNOWN_SCHOOLS), hemisphere names (`still` / `restless`), cycle names (`synodic` / `spin` / `nod`), and event `kind` strings. It is one of the few blocks that never renumbers across an age boundary, because it stores no ids at all — `astrology.moon` is written once at generation and is identical in every age.

## Algorithm

Below, `P_k` and `O_k` are the period and offset of cycle `k`; `t` is a real number of days since founding year 0 (day 0 = founding year 0, hour 0 = sunrise).

---
### 1. Static geometry: the eight school regions

School order is fixed and is the KNOWN_SCHOOLS declaration order — **weave, umbral, infernal, radiant, fire, water, earth, air**. Hemisphere membership is declared the other way round, as ordered lists per hemisphere:

    still    (latitude +45 deg): [radiant, water, earth, umbral]   -> quarters 0,1,2,3
    restless (latitude -45 deg): [weave,   fire,  air,   infernal] -> quarters 0,1,2,3

For a school, build `(hemisphere, quarter)` from that table, then

    latitude  = hemisphere_latitude * pi/180        (+45 or -45 degrees)
    longitude = quarter * 90 * pi/180               (0, 90, 180, 270 degrees)
    c = ( cos(latitude)*cos(longitude),
          sin(latitude),
          cos(latitude)*sin(longitude) )

`y` is the moon's polar axis; `+x` is "toward the world". A constant `WORLDWARD = (1,0,0)` is declared to document that and **is never used** — the code writes `max(0., x)` directly.

Resulting vectors (cos45 = sin45 = 0.70710678...; the near-zero components are the IEEE residue of cos(90 deg), about 6.1e-17, and are harmless):

    radiant  (+.7071, +.7071,  0    )   weave    (+.7071, -.7071,  0    )
    water    ( 0    , +.7071, +.7071)   fire     ( 0    , -.7071, +.7071)
    earth    (-.7071, +.7071,  0    )   air      (-.7071, -.7071,  0    )
    umbral   ( 0    , +.7071, -.7071)   infernal ( 0    , -.7071, -.7071)

The region table must be built and iterated **in KNOWN_SCHOOLS order** because the resulting dict's key order is exported and consumed.

---
### 2. `seed_moon(seed, variation=0, overrides=None)` — draw order is part of the contract

One RNG stream: `rng = Random(child_seed(seed, 'astrology-v1', variation))`.

    1.  synodic = rng.randint(26, 34)                  # inclusive both ends
    2.  spin    = rng.randint(15, 63)
    3.  repeat up to 8 times:
            if gcd(spin, synodic) == 1: break
            spin = rng.randint(15, 63)
        (the loop tests FIRST, then redraws; so at most 8 redraws, and an
         8th failure is accepted as-is -- coprimality is a preference, never a
         guarantee)
    4.  nod = rng.randint(17, 37)
    5.  repeat up to 8 times:
            if gcd(nod, synodic) == 1 and gcd(nod, spin) == 1: break
            nod = rng.randint(17, 37)
    6.  offsets, in exactly this order: synodic, spin, nod
            offset_k = rng.randint(0, P_k - 1)
    7.  tilt = rng.uniform(10.0, 35.0)

Then apply overrides. `controls = {variation, overrides: {}}`. Iterate the overrides mapping in its insertion order (the caller builds it as synodic, spin, nod, tilt):

    for key, value in overrides:
        if not value: continue                 # 0 and 0.0 mean "keep the seeded value"
        if key == 'tilt':
            if not (10.0 <= value <= 35.0): raise ValueError(
                'moon_tilt_degrees must be 0 or 10..35')
            tilt = float(value)
        elif key in {'synodic','spin','nod'}:
            (low, high) = the range for that key
            if value is not an int, or not low <= value <= high: raise ValueError(
                'moon_<key>_days must be 0 or <low>..<high>')
            P_key   = value
            O_key   = O_key % value            # fold the already-drawn offset into the new period
        else:
            raise ValueError('Unknown moon override ' + key)
        controls['overrides'][key] = value

Finally `great_year_days = lcm(synodic, spin, nod)` and return
`{periods, offsets, tilt_max_degrees, great_year_days, controls}`.

**Why the draws happen before the overrides are applied:** pinning one cycle must never move another. The stream is consumed identically whether or not anything is pinned, and only the pinned value is replaced afterwards. A test pins the synodic period and asserts the spin and nod periods AND their offsets are bit-identical to the unpinned world.

---
### 3. Cycle fractions and instantaneous state

    cycle_fraction(moon, k, t) = ((t + O_k) mod P_k) / P_k          in [0, 1)

Integer days stay in integer arithmetic (Python's `%` on two ints); a fractional `t` uses float modulo. The result must use **floor**-modulo semantics (Python's `%`); `t` is validated non-negative wherever it enters from a request, but the correct operator is floor-mod, not C's truncating `%`.

    moon_state(moon, t):
        phase_angle  alpha = 2*pi * f_synodic(t)       # 0 = full moon, pi = new moon
        spin_angle   beta  = 2*pi * f_spin(t)
        nod_degrees  gamma = tilt_max_degrees * sin(2*pi * f_nod(t))
                                                      # positive leans STILL toward the world

---
### 4. `_facing(vector, beta, gamma_degrees)` — two rotations, order and association fixed

    (x, y, z) = region vector
    cs = cos(beta);  ss = sin(beta)
    (x, z) = ( x*cs + z*ss,  -x*ss + z*cs )        # spin about the polar axis y
    cn = cos(gamma * pi/180);  sn = sin(gamma * pi/180)
    (x, y) = ( x*cn + y*sn,  -x*sn + y*cn )        # lean about z

Both assignments are simultaneous (tuple assignment): the OLD x is used in both components of each pair. Getting this wrong is the classic port bug; the existing C++ mirror spells it out with `xs`/`zs` and `xn`/`yn` temporaries and a comment saying it is "the same two rotations, in the same order and association, as the reference".

Sign convention: a positive nod turns the Still hemisphere (y > 0) toward +x, i.e. toward the world.

---
### 5. `tide(moon, t)` — the per-school strength multiplier

    state = moon_state(moon, t)
    sun = ( cos(alpha), 0, sin(alpha) )            # the sun's direction in the moon frame
    for each school, in KNOWN_SCHOOLS order:
        (x, y, z) = _facing(region[school], beta, gamma)
        visible = max(0, x)                        # 0 on the far side
        lit     = max(0, x*sun.x + z*sun.z)        # 0 in shadow; sun.y is 0 so y drops out
        tide[school] = min(1.8, max(0.5, 0.7 + 1.1 * visible * lit))

Interpretation: a region surges only when its quarter both **faces** the world and is **lit**. A region on the far side or in shadow ebbs to exactly the floor 0.7. A new moon (alpha = pi, sun = (-1,0,0)) dims all eight at once, because `lit` is then `-x` which is <= 0 wherever `visible` > 0 — that is a hollow night. The nod decides which hemisphere's four schools run hot, and since the nod period is 17..37 days that hot spell lasts roughly a fortnight.

---
### 6. `day_state(moon, t)` — everything a clock needs

    f = f_synodic(t)
    phase_fraction     = f
    elongation_degrees = 360 * f
    illumination       = (1 + cos(alpha)) / 2                # 1 at full, 0 at new
    moonrise_hour      = 24 * f                              # hours after sunrise;
                                                             # full moon rises at sunset
    spin_fraction      = f_spin(t)
    nod_degrees        = gamma
    leaning            = 'still' if gamma >= 0 else 'restless'
    tide               = tide(moon, t)

Note `moonrise_hour` depends only on the synodic fraction, so a fractional `t` (an hour within a day) does not move it beyond the fractional-day contribution to `f`.

---
### 7. `almanac(moon, year)` — one reported year

    start = year * 360                                      # absolute day

    (a) MONTHS. for m in 0..11:
            first = start + m*30
            totals[school] = 0 for all eight
            for d in 0..29:
                factors = tide(moon, first + d)
                totals[school] += factors[school]           # accumulated in day order,
                                                            # school order inside the day
            month = { first_day: first,
                      **day_state(moon, first),
                      mean_tide: { school: totals[school]/30 } }   # school order preserved

    (b) EVENTS. for d in 0..359:
            day = start + d
            s = (day + O_synodic) mod P_synodic
            if s == 0:                        events += {day: d, kind:'full_moon'}
            elif s == P_synodic // 2:         events += {day: d, kind:'new_moon'}
            nd = (day + O_nod) mod P_nod
            if nd == P_nod // 4:              events += {day: d, kind:'still_ascendant'}
            elif nd == (3*P_nod) // 4:        events += {day: d, kind:'restless_ascendant'}
            factors = tide(moon, day)
            for school in KNOWN_SCHOOLS order:
                if factors[school] >= 1.6:    events += {day: d, kind:'surge',
                                                         school: school,
                                                         tide: factors[school]}
            if every one of the eight factors <= 0.75:
                                              events += {day: d, kind:'hollow_night'}

        NOTE the two elif chains: full/new moon are mutually exclusive (they can only
        collide when P_synodic//2 == 0, impossible for P >= 26), and the two
        ascendancies are mutually exclusive. The surge loop and the hollow-night test
        are independent of both.

        NOTE the day stored in an event is `d`, 0..359, RELATIVE to the year start,
        while `month.first_day`, `now.day` and `next_grand_alignment_day` are ABSOLUTE.
        This is not a typo -- consumers (religion's feast calendar) treat event days as
        days-of-year -- but it is the single most likely thing to get wrong in a port.

    (c) SORT. events.sort(key = (day, kind, school if present else ''))
        Lexicographic on the kind string, so within one day the order is:
        full_moon < hollow_night < new_moon < restless_ascendant < still_ascendant < surge,
        and surges within a day are ordered alphabetically by school name
        (air, earth, fire, infernal, radiant, umbral, water, weave) -- NOT in
        KNOWN_SCHOOLS order, because the sort key re-orders what the emission loop
        produced. The sort is the total order; it is fully deterministic.

    (d) GRAND ALIGNMENT. linear scan:
            for day in [start, start + great_year_days):
                if (day + O_k) mod P_k == 0 for all three k: alignment = day; break
            else alignment = None
        This is "the next day all three cycles restart together". Because the periods
        are only PREFERRED coprime, the three congruences day = -O_k (mod P_k) can be
        incompatible, in which case there is no solution anywhere and the result is
        null. Null is a reachable, legitimate value; the field is nullable.
        The scan is at most lcm(P) iterations (worst case about 34*63*37 = 79254
        when the periods are pairwise coprime).

    return { reported_year, now: {day: start, **day_state(moon,start)},
             months, events, next_grand_alignment_day }

---
### 8. `lunar_sensitivity(result, cfg)` — the per-cell sway layer

    n = cfg.size
    empty = an n x n grid of zeros (used for every missing input layer)
    instability = [ layers['instability_'+school] or empty, for the eight in KNOWN order ]
    water    = layers['water_type']      or empty
    exposure = layers['coastal_exposure'] or empty
    for z in 0..n-1:
        for x in 0..n-1:
            total = sum over the eight instability layers of layer[z][x]
            shore = 1.0 if water[z][x] == 2 else exposure[z][x]     # 2 == lake
            out[z][x] = min(1.0, max(0.0, 0.2 + 0.5*total + 0.3*shore))

Reading: unstable ground and shorelines sway most; a calm inland cell still sways a
baseline 0.2. `total` can reach 8.0 in principle, so the clamp at 1.0 does real work
here (unlike the tide clamps). **Lakes** get the full shore term; ocean cells do not
get a hardcoded 1 -- they fall through to `coastal_exposure`, which for open ocean is
near 1 anyway because most of an ocean cell's neighbours are water.

---
### 9. `reported_year(result)` and `refresh_astrology(result, cfg)`

    reported_year:
        founding = result['settlements']['founding'] or {}
        if 'end_year' in founding: return (int(founding['end_year']), 'founding.end_year')
        return (0, 'epoch')

    refresh_astrology (NEVER reseeds -- the moon is drawn once per world):
        t0 = now()
        moon = result['astrology']['moon']
        (year, source) = reported_year(result)
        report = almanac(moon, year)
        result['lunar_almanac'] = {version: 1, reported_year_source: source, **report}
        result['layers']['lunar_sensitivity'] = lunar_sensitivity(result, cfg)
        factors = report['now']['tide']
        if result has 'magic':
            for name, net in magic['networks'] (insertion order):
                if name in factors:                      # known eight only
                    net['surged_strength'] = net['strength'] * factors[name]
            magic['lunar_surge'] = {day: report['now']['day'],
                                    factors: copy of factors,
                                    formula: 'surged_strength = strength * tide_school(day)'}
        timing_ms['astrology'] += elapsed_ms                # ACCUMULATES, does not assign

---
### 10. `add_astrology(result, cfg)` — stage 9 entry point

    o = options(cfg) if cfg.world_recipe else {}
    moon = seed_moon(cfg.seed,
                     o.get('moon_variation', 0),
                     {'synodic': o.get('moon_synodic_days', 0),
                      'spin':    o.get('moon_spin_days', 0),
                      'nod':     o.get('moon_nod_days', 0),
                      'tilt':    o.get('moon_tilt_degrees', 0.)})
    write result['astrology'] = {version, calendar, moon + static tables,
                                 formulas, method, limits}
    append LIMITS to result['warnings']
    return refresh_astrology(result, cfg)

---
### 11. `lunar_request(body)` — the stateless moon query (POST /world/moon)

Accepted fields, exactly: `api_version`, `world`, `day`, `year`, `month`, `day_of_month`, `hour`. Validation order matters because it decides which error a bad request gets:

    1. body must be a dict                        -> wrong_type('request', ...)
    2. any field outside the seven                -> unknown_field(<lexicographically
                                                     first extra>, noun='request field')
    3. api_version must be exactly the int 1      -> unsupported_api
    4. world must be a dict                       -> wrong_type('world', ...)
    5. world['astrology'] must be a dict with version == 1
                                                  -> missing_block('astrology',
                                                     "...regenerated rather than migrated")
    6. TIME. If 'day' is present:
           - any of year/month/day_of_month/hour also present
                 -> cross_field("Give an absolute day or a calendar time, not both.
                                 They are two spellings of one instant and this call
                                 cannot tell which you meant.")
           - day must be a finite int or float    -> wrong_type
           - day must be >= 0                     -> out_of_range (units: days since
                                                     founding year 0)
           - year, remainder = divmod(int(t), 360)
             month, day_of_month = divmod(remainder, 30)
             hour = (t - int(t)) * 24
             month += 1;  day_of_month += 1          (both are 1-based in the report)
       else (calendar path):
           - year default = world['lunar_almanac']['reported_year'] if present else 0
           - month default 1, day_of_month default 1, hour default 0
           - year: int, >= 0;  month: int in 1..12;  day_of_month: int in 1..30;
             hour: finite number in 0..24 INCLUSIVE (24 is accepted and means the
             start of the next day)
           - t = year*360 + (month-1)*30 + (day_of_month-1)
             if hour:  t = t + hour/24        # the `if` keeps t an exact integer at
                                              # hour 0, so an integer-day query stays
                                              # in integer arithmetic
    7. state = day_state(moon, t)
    8. surged_strength = { name: net['strength'] * state['tide'][name]
                           for name, net in world['magic']['networks'] (insertion order)
                           if name in state['tide'] }
       -- a hidden school is ABSENT from this report rather than reported as unmoved,
          "because the moon never charted it". Missing magic block => empty dict.
    9. report = almanac(moon, year)           # yes, the whole year is recomputed per query
   10. return { api_version: 1,
                time: {day: t, year, month, day_of_month, hour},
                **state,                        # phase_fraction, elongation_degrees,
                                                # illumination, moonrise_hour,
                                                # spin_fraction, nod_degrees, leaning, tide
                surged_strength,
                year_events: report['events'],
                next_grand_alignment_day: report['next_grand_alignment_day'],
                great_year_days: moon['great_year_days'] }

The round trip is exact: a calendar query and a `day` query for the same instant produce identical `tide` dicts and identical `time` sub-dicts (a test pins year 3, month 2, day 5, hour 6 -> day 1114.25 -> back to (3, 2, 5, 6.0)).

---
### 12. Exported formula strings (verbatim; they ARE part of the contract, consumers parse-free-read them)

    time:             't is real days since founding year 0; hour 0 is sunrise; integer days use integer modulo'
    fraction:         'f_k(t) = ((t + offsets[k]) mod periods[k]) / periods[k] for k in synodic, spin, nod'
    phase_angle:      'alpha = 2*pi*f_synodic; 0 is full, pi is new'
    spin_angle:       'beta = 2*pi*f_spin, rotation of the moon frame about its polar axis y'
    nod_degrees:      'gamma = tilt_max_degrees * sin(2*pi*f_nod); positive leans the still hemisphere toward the world'
    region:           'x,z = x*cos(beta)+z*sin(beta), -x*sin(beta)+z*cos(beta); then x,y = x*cos(gamma)+y*sin(gamma), -x*sin(gamma)+y*cos(gamma)'
    tide:             'tide_s = clamp(.7 + 1.1 * max(0,x) * max(0, x*cos(alpha)+z*sin(alpha)), .5, 1.8) using that school region'
    illumination:     '(1 + cos(alpha)) / 2'
    moonrise_hour:    '24 * f_synodic hours after sunrise (full moon rises at sunset)'
    elongation_degrees: '360 * f_synodic'
    leyline_at_time:  'ley_s(t) = ley_s * (1 + lunar_sensitivity * (tide_s(t) - 1))'

The last one is the **consumer contract**: the exported ley grids are the base field and the surge is a multiplier a consumer applies at an instant. The field is never rewritten.

---
### 13. The LIMITS string (also appended to warnings, verbatim)

"The moon is artistic astronomy: one body reduced to phase, rise hour, spin and lean. No altitude, no latitude dependence, no solar eclipses, no water tides. Surges are momentary multipliers; the exported leyline grids are the base field."

## Constants

**Calendar** (owned here, imported by beast movement, encounters, nomad routes, scheduler):
- `HOURS_PER_DAY = 24`. Changing it changes `moonrise_hour`'s scale and the fractional-day conversion in the query API.
- `DAYS_PER_MONTH = 30`, `MONTHS_PER_YEAR = 12`, `DAYS_PER_YEAR = 360`. Changing any of these changes the almanac's shape (12 months of 30 days), the `year*360` day arithmetic everywhere, and the age lottery's sample day. 360 is assumed by several unrelated modules that import it.

**Geometry:**
- `HEMISPHERE_LATITUDE = {still: +45.0, restless: -45.0}` degrees. The magnitude 45 fixes the maximum |x| a region can reach: with no lean, `cos 45 = 0.7071`. Pushing it toward 0 would make every region face the world more strongly and raise every tide; pushing it toward 90 would park the schools at the poles and flatten the tide to the floor.
- Quarter longitudes are `quarter * 90` degrees, quarter in 0..3, four schools per hemisphere. This is structural, not tunable: four quarters, four schools.
- `DEG = pi/180`.

**Cycles:**
- `PERIOD_RANGES = {synodic: (26,34), spin: (15,63), nod: (17,37)}`, inclusive integer days. Widening them widens the great year but also lowers the chance of coprimality (more `None` alignments). The nod range sets how long a hemisphere's hot spell lasts: about half of 17..37 days, "a fortnight".
- `TILT_RANGE = (10.0, 35.0)` degrees. This bounds how hard a hemisphere can lean in, and so bounds the peak tide (see below). 35 was chosen so the peak lands near 1.77, just above the surge threshold of 1.6; lowering the max toward 10 would make surges rare, raising it toward 45 would make them routine.
- `REDRAWS = 8`. The number of retries allowed when seeking coprime periods. It is a *preference*: after 8 failed redraws the non-coprime value is kept. A test asserts that over 40 seeds, more than 30 give coprime synodic/spin, which is what "the norm" means here. Raising it makes non-coprime worlds rarer but never impossible; lowering it toward 0 would make the great year frequently collapse to a short lcm and alignments frequently null.

**Tide:**
- `TIDE_FLOOR = 0.7` — the multiplier for a region that is unseen or unlit. It is the resting state of seven-eighths of the sky on most days; it deliberately sits *below* 1.0, so the default condition is a slight ebb, not neutrality.
- `TIDE_GAIN = 1.1` — how much visible*lit buys. Peak `visible*lit` is about 0.9698 (see below), so 1.1 puts the peak at about 1.767.
- `TIDE_MIN = 0.5`, `TIDE_MAX = 1.8` — clamp bounds that are **never reached** with the shipped geometry. Minimum achievable is exactly 0.7 (the floor, since `1.1*visible*lit >= 0`). Maximum achievable: a Still region at longitude 0 under spin 0, full moon, with the maximum lean, gives `x = cos45*(cos35 + sin35) = 0.98481`, `z ~ 0`, `lit = x`, so `tide = 0.7 + 1.1*0.98481^2 = 1.76683`. Both clamps are defensive only; they become live only if the latitudes, the tilt range or the gain are changed. Keep them anyway — they are the contract's stated range and a consumer may rely on 0.5..1.8.
- `SURGE = 1.6` — the threshold at or above which a school's tide is reported as a `surge` event. Raising it makes surge feasts and school-caused ruins rarer; the gap between 1.6 and the 1.767 ceiling is what makes a surge a real event rather than a daily occurrence.
- `HOLLOW = 0.75` — a `hollow_night` is emitted when **all eight** tides are <= 0.75. Because the floor is 0.7, this is a narrow band just above the floor: a hollow night means essentially everything is unlit. Lowering it to 0.7 would make hollow nights depend on exact floating-point equality with the floor; 0.75 gives it 0.05 of slack, which is why it is not 0.7.

**lunar_sensitivity:**
- base `0.2` — every cell sways at least a fifth as much as the tide asks.
- instability weight `0.5` applied to the SUM of eight instability layers (so a maximally unstable cell saturates the clamp on its own).
- shore weight `0.3`.
- clamp to `[0, 1]`.
- lake code `2` in `water_type` is the value tested for the hard shore = 1.

**Versions:**
- `astrology.version = 1`, `lunar_almanac.version = 1`, `MOON_API_VERSION = 1`. All three are checked with `!= 1` / `!= MOON_API_VERSION` at several load points; a mismatch means regenerate, never migrate.

**Dead constant:**
- `WORLDWARD = (1., 0., 0.)` is declared and never referenced. It documents that +x is the direction of the world; the code inlines `max(0., x)`.

**Not owned here but read by consumers:** `lunar_influence` (default 0.5, range 0..1) scales how far a surge sways the age lottery; `0` reproduces a world in which the moon is a spectator.

## Data files

**None.** `terrain_astrology.py` loads no JSON and no external content. Every table it uses is a module constant.

The one table it imports is `KNOWN_SCHOOLS` from `terrain_leyline_history.py`, which is a Python dict literal, not a file. Its declaration order is load-bearing and must be reproduced exactly:

    weave     Raw magic     chaos, creation, creativity                        rgb [166,104,214]
    umbral    Raw magic     necrotic, entropy, death, order                    rgb [ 72, 49, 35]
    infernal  Holy/unholy   demons, hellscapes, corruption, evil               rgb [154, 43, 49]
    radiant   Holy/unholy   holy, healing, order, peace, righteousness         rgb [225,209,133]
    fire      Primordial    fire, heat, renewal, chaos                         rgb [207, 86, 37]
    water     Primordial    water, ice, currents, order                        rgb [ 65,156,202]
    earth     Primordial    nature, ground, stone, roots, order                rgb [ 67,120, 51]
    air       Primordial    wind, storms, chaos, force                         rgb [176,210,213]

The comment above it is worth carrying: "The world's own taxonomy. Everything this world can name, measure, chart against its moon or raise a god for is one of these eight, and the places that must stay eight forever say KNOWN_SCHOOLS rather than SCHOOLS."

There are four further `HIDDEN_SCHOOLS` (`blood`, `void`, `rot`, `eldritch`) appended after the known eight in the combined `SCHOOLS` dict. Astrology deliberately ignores them: they have no region on the moon, no tide, and no `surged_strength` key. Generation can never raise one; only the corruption API can put a node in one.

Downstream, religion reads a `pantheon.json` catalogue that contains a god weighted by the `lunar_influence` fact (`{"weight": {"base": 0, "fact": "lunar_influence", "per": 8}}`) and a `god_turning_moon` whose aspect is decided from the almanac's mean nod. That file is religion's, not astrology's, and is out of scope here beyond the fact that `lunar_almanac.months[].nod_degrees` and `lunar_almanac.events` must exist for it.

## Random streams

**One stream, drawn exactly once per world, in `seed_moon` only.** Nothing else in this module draws; `tide`, `day_state`, `almanac`, `lunar_sensitivity`, `lunar_request` and `refresh_astrology` are all closed-form and deterministic. `refresh_astrology` explicitly never reseeds ("Recompute everything that depends on the year or on the ley fields; never reseed"), which is why the moon is identical in every age of a world.

**Seeding (reference):** `child_seed(master, 'astrology-v1', variation)` = the first 4 bytes, big-endian, of `sha256("tectonics-v1:{master}:{domain}:{variation}")`, giving a 32-bit integer used to seed CPython's Mersenne Twister. `variation` comes from the `moon_variation` option and exists so the moon can be reseeded independently of the world.

**Draw count is data-dependent:**
- 1 `randint` for synodic.
- 1 `randint` for spin, plus 0..8 redraws (a redraw happens on each of up to 8 iterations where `gcd(spin, synodic) != 1`).
- 1 `randint` for nod, plus 0..8 redraws (condition: coprime with both).
- 3 `randint` for the offsets, in the order synodic, spin, nod, each over `[0, P-1]`.
- 1 `uniform(10, 35)` for the tilt.

Total: 6 to 22 draws. The overrides consume **no** stream — they are applied after all draws, precisely so that pinning one cycle cannot move another.

**For FWGC++:** use `fwg::Rng::child(master, "astrology-v1", variation)` — that is exactly the existing named-child mechanism, and no fourth seed derivation is needed or permitted. `Rng::randint(low, high)` is already inclusive-inclusive, matching the reference; `Rng::uniform(low, high)` is [low, high). Numbers need not match the Python; only the *structure* of the draw sequence matters, and it matters for one reason: the redraw loops and the override-after-draw ordering are the behaviour, not the values. Reproduce the loop shape (test-then-redraw, at most 8 redraws, accept the 8th failure) even though no specific integers are being matched.

## Ordering hazards

1. **KNOWN_SCHOOLS declaration order reaches output in five places.** `REGIONS` is built by iterating it, so the exported `astrology.moon.regions` dict is in that order; `tide()` returns a dict in that order and a test asserts `list(factors) == list(KNOWN_SCHOOLS)`; `mean_tide` per month is built in that order; the almanac's surge-event emission loop walks it; and `lunar_sensitivity` sums the eight instability layers in that order (float addition is not associative, so the sum order is observable in the last bits even in a clean rewrite's own arithmetic). Use a fixed array in that order, not a hash map.

2. **`HEMISPHERES` list order is the quarter assignment.** `still: [radiant, water, earth, umbral]` means radiant is quarter 0 (longitude 0), water quarter 1 (90), earth quarter 2 (180), umbral quarter 3 (270); likewise `restless: [weave, fire, air, infernal]`. Reordering either list silently rotates the sky.

3. **The event sort re-orders what the emission loop produced.** `events.sort(key=(day, kind, school or ''))` is a total order on the key, so it is deterministic, but note that within one day the surges come out **alphabetically by school** (air, earth, fire, infernal, radiant, umbral, water, weave), not in KNOWN_SCHOOLS order. A port that emits in school order and forgets the sort will produce a differently ordered `events` array, which religion's `feasts()` slices by `e['day']` (order-insensitive) but a diff-based conformance check would flag. Kind ordering within a day is lexicographic: full_moon < hollow_night < new_moon < restless_ascendant < still_ascendant < surge.

4. **`seed_moon`'s draw order is explicitly "part of the contract"** (the docstring says so). synodic, then spin (+redraws), then nod (+redraws), then the three offsets in the order synodic/spin/nod, then the tilt.

5. **Override iteration order** decides which `ValueError` a caller sees when two overrides are both invalid, and decides the key order of the exported `controls.overrides` dict. The caller (`add_astrology`) always builds the mapping as synodic, spin, nod, tilt.

6. **`magic.networks` iteration order** decides the key order of `magic.lunar_surge.factors` (a copy of the tide dict, so KNOWN order) and of `lunar_request`'s `surged_strength` (networks' own insertion order, filtered). Only key order, never values.

7. **`mean_tide` accumulation order**: days ascending 0..29 within each month, and the division by 30 happens after the full sum. A port that computes a running mean, or sums in a different order, will differ in the last bits.

8. **The grand-alignment scan is a linear scan from `start` upward and takes the FIRST hit**; it is not a CRT solve. Any replacement must return the same first day, not merely a valid alignment day.

9. **`lunar_sensitivity` iterates z outer, x inner** and writes a fresh row list per z. Purely per-cell, so the order does not affect values — but the grid's row/column convention (`layer[z][x]`) does, and it is the opposite of the `(x, z)` point tuples used by the hydrology graph elsewhere in the codebase.

## Edge cases

**Missing layers.** `lunar_sensitivity` substitutes a single shared all-zeros grid for any absent instability, `water_type` or `coastal_exposure` layer. The same `empty` object is aliased into up to ten slots; it is only ever read, so the aliasing is benign, but a port must not hand out a shared mutable grid.

**Missing magic block.** `refresh_astrology` skips the whole surge section if `result['magic']` is None; `lunar_request` returns `surged_strength: {}` when `world['magic']` or its `networks` is missing.

**Hidden schools.** `blood`, `void`, `rot`, `eldritch` have no moon region. In `refresh_astrology` they are skipped by `if name in factors`, so they carry **no `surged_strength` key at all** rather than a key equal to their base strength. The comment is explicit: "A hidden school takes no surge and carries no surged_strength key: alien magic does not answer to this world's sky." A test asserts `assertNotIn('surged_strength', net)` for them. Do not "helpfully" write 1.0.

**No grand alignment.** `next_grand_alignment_day` is `None`/null when the three congruences are incompatible (possible because coprimality is only preferred). The field is nullable and consumers must handle null.

**Tide clamps never bind.** Minimum reachable is exactly 0.7, maximum about 1.76683. `TIDE_MIN = 0.5` is unreachable by construction. Keep the clamp; do not "simplify" it away, because it is the published range.

**Hollow night vs floor.** `HOLLOW = 0.75` sits 0.05 above the floor, deliberately, so the test is not a float-equality test against 0.7.

**Even/odd periods in event days.** `new_moon` fires at `P_synodic // 2` and the ascendancies at `P_nod // 4` and `(3*P_nod) // 4`, all integer floors. For periods not divisible by 2 or 4 the marked day is the floor, i.e. approximate by up to a day. That is accepted.

**Fractional days.** `t` may be a float. `cycle_fraction` then uses float modulo; `day_state` works; `tide` works. The `if hour:` guard in `lunar_request` keeps `t` an exact integer when the hour is 0, so an integer-day query stays in integer arithmetic. `divmod(int(t), 360)` truncates toward zero, which is correct only because `t >= 0` is enforced.

**Negative time.** Rejected at the API boundary (`day < 0` -> `out_of_range`; `year < 0` -> `out_of_range`). Internally nothing guards it, and Python's floor-modulo would give a sensible answer anyway. A C++ port using truncating `%` would NOT, so use floor-mod.

**hour == 24.** Accepted (`0 <= hour <= 24`), and means midnight at the end of the day, i.e. the start of the next day.

**Day-1114.25-style round trip.** `divmod` reconstruction yields 1-based month and day_of_month, and `hour = (t - int(t)) * 24`. A pure-integer day gives `hour = 0.0` (a float, where the calendar path would have carried the caller's int 0). Numerically equal, different type — irrelevant in C++, relevant if a port serialises `0` vs `0.0`.

**Override value 0 / 0.0 means "seeded".** `if not value: continue`. A caller cannot pin a period of 0 — nor should it, since 0 would divide by zero in `cycle_fraction`.

**Override type strictness.** A period override must be *exactly* an int (`type(value) is not int`), so a bool or a float like `30.0` is rejected. The tilt is coerced with `float(value)`.

**World edges / poles.** `lunar_sensitivity` is a pure per-cell map, so it inherits whatever seam and pole structure its inputs have: column `n-1` duplicates column 0 and rows 0 and `n-1` are single pole values repeated, because `node_grid` produced the inputs that way. No seam repair is needed or performed. There is no latitude term anywhere in the moon's own math — the LIMITS string says so explicitly ("No altitude, no latitude dependence"), so a cell at the pole and a cell at the equator see the same sky.

**Grid size.** `n = cfg.size`; the sensitivity grid is always built at full size even if every input layer is missing (giving a uniform 0.2 field).

**Repeated refresh.** `timing_ms['astrology']` accumulates across refreshes rather than being overwritten. `lunar_almanac` and the `lunar_sensitivity` layer are fully rewritten each time, so repeated refreshes are idempotent in content (a test asserts the sensitivity grid is unchanged after an age advance when the ley fields have not moved).

**Validation on load.** `validate_age_world` re-derives `lcm(periods)` and refuses a world whose `great_year_days` disagrees, whose period or offset is out of range or not an int, or whose tilt is outside 10..35. Port that check with the block, not separately.

## Reasoning worth preserving

Quoted from the source; these explain *why* and exist nowhere else once the repo goes.

Module docstring: "One moon, two hemispheres, eight schools: seeded cycles, tide, almanac. No engine I/O. The moon is not tidally locked. Its Still hemisphere carries the ordered schools and its Restless hemisphere the chaotic ones, one school per quarter. Three integer-day cycles with a coprime preference (phase, spin, nod) make a beat far longer than any one of them: fully replayable, and long enough to read as chaos. Every quantity here is a ratio of a cycle or a unit vector, never a metre, so world scale cannot move it."

"Numeric contract: only sin, cos, products, sums, min and max, and integer modulo on integer days. No pow, so the C++ mirror in Core/astrology.cpp can reproduce it." — the reason there is no exponential falloff anywhere in the tide. FWGC++ is a clean rewrite and need not match numbers, but the no-pow discipline is cheap and keeps the function's character.

`seed_moon`: "Seeded integer-day periods, offsets and nod tilt; draw order is part of the contract. `overrides` may pin any period or the tilt (zero or absent keeps the seeded value); the draws still happen in the same order so an override never changes another cycle." — this is the whole reason the overrides are applied after every draw instead of short-circuiting.

`cycle_fraction`: "Fraction of a cycle at time t in days; integer days stay in integer arithmetic."

`_facing`: "Rotate a moon-frame vector by the spin about y, then lean by the nod about z." and inline: "A positive nod turns the Still (y > 0) hemisphere toward the world (+x)."

`tide`: "Per-school strength multiplier: visible fraction times lit fraction of its quarter."

`day_state`: "What a clock needs: phase, rise hour after sunrise, illumination, lean and tide."

`almanac`: "One reported year: month heads, events and the next day all three cycles restart."

`lunar_request`: "Stateless moon query: the sky and the surge at any day or hour of a generated world. Time is `day` (real days since founding year 0) or year, month, day_of_month and hour; both give the same closed-form answer the exported formulas describe, so an orchestrator can drive a clock from this and stay bit-consistent with the almanac."

Inside `lunar_request`: "Only the known schools have a tide. A hidden school is absent from this report rather than reported as unmoved, because the moon never charted it."

Inside `refresh_astrology`: "A hidden school takes no surge and carries no surged_strength key: alien magic does not answer to this world's sky."

`refresh_astrology`: "Recompute everything that depends on the year or on the ley fields; never reseed."

`lunar_sensitivity`: "How far a cell's magic sways with the moon, from fields that are already ratios." — i.e. the inputs were chosen precisely because they are already 0..1 and carry no units.

`add_astrology`: "Stage 9: seed the moon once per world, then publish the first almanac."

The `method` string that ships in the world: "One moon, not tidally locked. Its still hemisphere carries radiant, water, earth and umbral; its restless hemisphere weave, fire, air and infernal, one school per quarter. Seeded integer-day phase, spin and nod cycles prefer coprime periods, so the great year is their least common multiple. A school surges when its quarter both faces the world and is lit."

The `missing_block` message for a world without the contract: "This world lacks the astrology contract at version 1. The moon is seeded during generation, so a world exported before that contract has to be regenerated rather than migrated." — policy: generated worlds are disposable, there is no migration path.

The `cross_field` message for day + calendar together: "Give an absolute day or a calendar time, not both. They are two spellings of one instant and this call cannot tell which you meant."

From the existing C++ mirror (`Core/astrology.cpp`), the note attached to the two rotations: "Spin about the polar axis, then lean the still hemisphere toward +x: the same two rotations, in the same order and association, as the reference." — someone found this worth a comment; that is a hint it was got wrong once.

From `docs/astrology.md`, the intent behind the numbers: "A region on the far side or in shadow ebbs to 0.7. A region centred on the facing meridian under a full moon with a favourable lean surges near 1.77. A new moon dims all eight at once (a hollow night). The nod decides which hemisphere's four schools run hot for a fortnight."

Also from the doc, the consumer discipline: "the exported ley grids stay the base field and the surge is a multiplier a consumer applies at a moment in time", and about the age lottery: "A surge at the wrong moment is what kills a city; the field itself is not rewritten." And: "Standing threat assessments and wars use base potencies: the assessment is standing, the surge is momentary." And: "`lunar_influence: 0` reproduces a world without lunar sway."

From `docs/conformance/time-advance.md`: "`_apply_surge` uses `terrain_astrology.tide` rather than restating the formula. The [alternative] would be free to drift from the almanac the lab and `/world/moon` both read." — one tide primitive, three call sites, deliberately.

## Notes for the C++ implementation

**Mapping to FWGC++ conventions** (read `include/fwg/` first; the notes below assume `Grid`, `Rng`, `Config`, `World`, the hand-rolled JSON writer and the no-dependency rule).

**Types.** A `Moon` struct with `std::int64_t synodic, spin, nod` and matching offsets, `double tilt_max_degrees`, `std::int64_t great_year_days`, plus the controls (`std::int64_t variation` and a small ordered list of pinned key/value pairs). The existing mirror in the dying repo already has exactly this shape and it is the right one. Store the moon on `World` as a typed block, not as a map of doubles — `periods` and `offsets` must stay integral, and a validator downstream checks they are integers.

**School array, not a map.** Eight schools in the fixed order weave, umbral, infernal, radiant, fire, water, earth, air. Use `std::array<double, 8>` for a tide and an `enum class School` or a `constexpr std::array<std::string_view, 8>` for names. Build the region table as a `constexpr` pair of arrays (latitude sign and quarter index per school), the way the existing mirror does:
`region_latitude = {-45, +45, -45, +45, -45, +45, +45, -45}` and `region_quarter = {0, 3, 3, 0, 1, 1, 2, 2}` for that school order. Derive them from the two hemisphere lists in a static initialiser if you prefer — but if you hardcode, add a unit test that re-derives them from the hemisphere lists, because a transcription error here is silent.

**Modulo.** Python's `%` is floor-modulo; C++'s `%` truncates toward zero and `std::fmod` does the same for doubles. All time inputs are validated non-negative, so the two agree in practice — but write a `floor_mod` helper for both the integer and double paths anyway rather than relying on that invariant holding forever.

**No `pow`.** Nothing in this module needs it. Keep it that way; `std::pow` is the one libm function whose last-bit behaviour has actually bitten this codebase across linkage models.

**`std::gcd` / `std::lcm`** from `<numeric>` cover the coprime loops and the great year. `std::lcm` takes two arguments; nest it. Beware overflow is not a concern here (max lcm is about 79k).

**RNG.** `fwg::Rng::child(master, "astrology-v1", variation)`. `randint(low, high)` is already inclusive on both ends, matching the reference's `randint`; `uniform(low, high)` is [low, high), so a tilt of exactly 35.0 is unreachable — that is fine, the reference's `random.uniform` has the same open-ish upper end in practice. Do not invent a fourth seed derivation. Preserve the loop shape (test-then-redraw, at most 8 redraws, accept the 8th failure) even though the values will differ.

**Grids.** `lunar_sensitivity` is a pure per-cell map over `n x n`. Take `const std::map<std::string, Grid>& layers` and look up each name once outside the loop, holding either a pointer to the Grid or `nullptr` for absent — do NOT look up by string per cell (the leyline pass in the dying repo has a comment about exactly that costing 263k redundant lookups at size 513). The result needs no seam repair: column `n-1` and the pole rows come out right because the inputs already satisfy the invariant. Assert that invariant in a test rather than fixing it up.

**`sphere_grid` is not used here.** Astrology touches no node graph, no neighbours, no great-circle distances, and no metres. It is the cheapest subsystem in the generator to port and it is fully unit-testable without a world.

**Strings and dict order.** Several exported objects are keyed dicts whose order is observable (`regions`, `tide`, `mean_tide`, `factors`, `hemispheres`, `controls.overrides`). `std::map` sorts alphabetically and would silently reorder them. Use the library's JSON writer with an insertion-ordered object type, or emit from an array in the fixed school order.

**Floats in the almanac.** `mean_tide` sums 30 daily values in ascending day order and divides by 30 at the end. Write it that way; do not use a running mean.

**Nullable field.** `next_grand_alignment_day` is `std::optional<std::int64_t>` and serialises as JSON `null` when empty. It is genuinely reachable.

**Performance.** `almanac` costs 360 (months) + 360 (events) tide evaluations plus a scan of up to `great_year_days` (<= ~79k) cheap integer tests. `lunar_request` recomputes the entire almanac on every call, which is fine at this cost in C++ but is worth a cached-per-(moon, year) memo if the query becomes hot. `tide` itself is eight rotations of a 3-vector: trivially vectorisable, and the per-day `cos/sin` of alpha, beta and gamma should be hoisted out of the school loop (the existing mirror already does this; the Python does not, since `_facing` recomputes `cos(spin)`/`sin(spin)` per school).

**Error surface.** The request API returns structured refusals (`wrong_type`, `unknown_field`, `out_of_range`, `unsupported_api`, `missing_block`, `cross_field`) that carry a field name, the received value, an expected descriptor and a suggestion. FWGC++ has no equivalent yet. Either port a minimal version of that error type alongside this module or defer `lunar_request` entirely and port only the generation half (`seed_moon`, `tide`, `day_state`, `almanac`, `lunar_sensitivity`, `refresh`) — the generator does not need the query API, only an orchestrator does. Say which you did in porting-status.

**Validation.** Port `validate_age_world`'s moon checks (periods int and in range, offsets int and in `[0, period)`, tilt in 10..35, `great_year_days == lcm`) as a `validate(const Moon&)` next to the struct, so the age system can call it without duplicating the bounds.

## Open questions

1. **Option-registry bounds and `seed_moon` bounds disagree, and the mismatch throws a bare `ValueError`.** The registry declares `moon_synodic_days` as 0..34, `moon_spin_days` 0..63, `moon_nod_days` 0..37, `moon_tilt_degrees` 0..35 — so `validate_options` happily accepts `moon_synodic_days = 5` or `moon_tilt_degrees = 3.0`, and `seed_moon` then raises a plain `ValueError("moon_synodic_days must be 0 or 26..34")` rather than a structured `out_of_range` RequestError. Generation fails with an unstructured error in the middle of stage 9. A porter must decide: tighten the registry to a discontinuous domain ({0} union [low, high]), or convert the `seed_moon` raises to structured request errors, or accept the current behaviour.

2. **Event days are relative, everything else is absolute.** `events[].day` is 0..359 within the reported year; `months[].first_day`, `now.day`, `magic.lunar_surge.day` and `next_grand_alignment_day` are absolute days since founding year 0. Nothing in the code or the docs states this. Religion's feast calendar treats event days as days-of-year and is therefore consistent, but `lunar_request` returns `year_events` beside an absolute `next_grand_alignment_day` and an absolute `time.day` in one flat object, which is easy for a consumer to mix up. Keep the behaviour (consumers depend on it) or normalise it deliberately and update every consumer — do not change it by accident.

3. **`TIDE_MIN = 0.5` is unreachable** with the shipped latitudes and tilt range (the floor is 0.7 and the gain is non-negative). Is it a vestige of an earlier geometry, or deliberate headroom? The published range 0.5..1.8 is quoted in the docs, so a consumer may be validating against it. Recommended: keep the clamp, document that 0.7..1.767 is the achievable band.

4. **`WORLDWARD = (1., 0., 0.)` is dead code** — declared, documented by position, never referenced. Either use it in the `visible`/`lit` dot products (which would make the +x convention explicit and self-documenting) or drop it.

5. **`lunar_request` recomputes the whole almanac per call**, including a linear scan of up to ~79k days for the grand alignment, to answer a question about one instant. That is a lot of work for a "stateless" point query. Cache per (moon, year), or return alignment lazily.

6. **`timing_ms['astrology']` accumulates rather than assigns.** Every `refresh_astrology` adds to it, so a world that has advanced several ages reports the sum of all refreshes. Every other timing in the codebase that I checked is written the same way, so this is probably intended — but it means the number is not "how long astrology took", it is "how long astrology has ever taken in this world". Decide and state it.

7. **Coprimality is a preference with no fallback.** After 8 redraws a non-coprime triple is accepted, which shortens the great year and can make `next_grand_alignment_day` null forever. No warning is emitted and nothing records that the redraws were exhausted. Consider recording it in `controls` so a world with a degenerate sky is identifiable.

8. **Ocean cells get no hard shore term in `lunar_sensitivity`.** Lakes (`water_type == 2`) get `shore = 1`, oceans (`water_type == 1`) fall through to `coastal_exposure`. In practice open ocean's exposure is near 1, so the difference is small — but the asymmetry is unexplained and may be an oversight: "shore" for a lake is the lake itself, while "shore" for the sea is its coastline. I could find no comment justifying it.

9. **No conformance record exists.** `docs/conformance/coverage.json` lists `Sim/icarus_sim/terrain_astrology.py` under `uncovered` (ticket `board/in-progress/CONFORMANCE-DOCS.md`) and `Core/astrology` likewise (ticket `board/retired/UNREAL-IMPORT-CONFORMANCE.md`). `docs/astrology.md` is the only written spec and I found **no disagreement** between it and the code — every constant, formula, range and export it names matches the source, including the "surges near 1.77" figure (computed peak 1.76683). Its one omission is the relative-vs-absolute event day noted above.

## Addenda from the audit

The reader above wrote the specification; a second reader went back to the source
to find what it had left out or got wrong. Both are kept: the omissions are
content the implementer needs, and the corrections say where the specification
above is not to be trusted.

### Corrections

- **Claimed:** "`terrain_beast_movement`, `terrain_encounters`, `terrain_nomad_routes` and the scheduler all import `DAYS_PER_YEAR` (360) from this module. Astrology owns the calendar."
  **Actually:** The scheduler does NOT import it. `terrain_time_schedule.py` re-declares the whole calendar independently, as floats: `HOURS_PER_DAY = 24.`, `DAYS_PER_MONTH = 30.`, `MONTHS_PER_YEAR = 12.`, `DAYS_PER_YEAR = DAYS_PER_MONTH * MONTHS_PER_YEAR # 360`, `SEASON_DAYS = DAYS_PER_YEAR / 4. # 90`, with a comment pointing at docs/astrology.md instead of an import ("The calendar is the moon's, and it already exists... Time here is a real number of days on that same scale so a tick and `POST /world/moon` cannot disagree"). Only beast_movement, encounters and nomad_routes actually `from .terrain_astrology import DAYS_PER_YEAR`. So the calendar exists in TWO independent copies, and the scheduler's copy is float-typed while astrology's is int-typed (astrology relies on int-ness to keep integer days in integer modulo). A port that centralises the calendar in the astrology module must either keep the duplicate or deliberately unify it and accept the int/float change.

- **Claimed:** §9 pseudocode: `reported_year: founding = result['settlements']['founding'] or {}`
  **Actually:** The source is `founding = result.get('settlements', {}).get('founding') or {}`. This is not cosmetic: astrology first runs at stage 9, and at stage 9 the `settlements` block does not exist at all (a test asserts `materialize_stage(world, 9)['lunar_almanac']['reported_year'] == 0` and that stage 10 is the first with `reported_year_source == 'founding.end_year'`). A literal transcription of the spec's pseudocode throws on the very first call of every world. Both levels must be absence-tolerant: missing `settlements`, missing `founding`, and `founding` present but falsy all fall through to `(0, 'epoch')`.

- **Claimed:** "`TILT_RANGE`... 35 was chosen so the peak lands near 1.77, just above the surge threshold of 1.6; lowering the max toward 10 would make surges rare"
  **Actually:** Below a lean of about 19.77 degrees surges become IMPOSSIBLE, not rare. The peak attainable tide at lean gamma is `0.7 + 1.1*cos^2(45deg - gamma)`; reaching SURGE = 1.6 requires `sin(2*gamma) >= 0.63636`, i.e. `gamma >= 19.77deg`. At tilt_max = 10 the global ceiling is `0.7 + 0.55*(1 + sin 20deg) = 1.43811`, so no `surge` event, no surge feast and no school-caused ruin can ever occur in such a world. Since the tilt is drawn `uniform(10, 35)`, roughly 39% of unpinned worlds are permanently surge-free. This is a behavioural cliff, not a smooth rarity gradient.

- **Claimed:** "`lunar_request` returns `surged_strength: {}` when `world['magic']` or its `networks` is missing" / "Missing magic block => empty dict"
  **Actually:** True only when the KEY IS ABSENT. The source is `world.get('magic', {}).get('networks', {}).items()`, so a `magic` key present with value `None` (or `networks` present and `None`) makes `.get` return `None` and `.items()` raise `AttributeError` — an unstructured crash, not a refusal and not an empty dict. The other two call sites defend and `lunar_request` does not: `refresh_astrology` uses `magic = result.get('magic')` then `if magic is not None`, and `terrain_time._apply_surge` uses `(magic.get('networks') or {})`. A port should use the defensive spelling at all three.

- **Claimed:** "The request API returns structured refusals (`wrong_type`, `unknown_field`, `out_of_range`, `unsupported_api`, `missing_block`, `cross_field`)"
  **Actually:** They are RAISED, not returned: every check is `raise wrong_type(...)` etc., and these factories build exceptions that derive from `ValueError` (the tests assert `with self.assertRaises(ValueError)` for bad requests, and `add_astrology`'s override failures raise plain `ValueError` through the same generation path). A porter designing the boundary as a return-code/result type would reshape the contract; the whole generation pipeline propagates these as exceptions.

- **Claimed:** "the near-zero components are the IEEE residue of cos(90 deg), about 6.1e-17"
  **Actually:** The residue is scaled by `cos(45deg)` because the component is `cos(latitude)*cos(longitude)`. The x components of water and umbral are `0.70711 * cos(pi/2) ~ 4.33e-17`, and the z components of earth and air are `0.70711 * sin(pi) ~ 8.66e-17`. Harmless either way, but the stated figure is not the one that appears in the exported `regions` table.

### Missing — important

**Hand-checkable verification fixture (the only way a clean rewrite can self-check when numbers need not match)**

The unit tests carry a round-numbered moon that makes every expectation computable by hand, and it is the single most valuable thing for a numbers-need-not-match port. Fixture: periods {synodic 30, spin 40, nod 20}, offsets {0, 0, 0}, tilt_max_degrees 30, great_year_days 120 (note: no `controls` key — nothing downstream of seed_moon reads it). Expected values, all exactly derivable: (a) day 0 is full moon, spin 0, no lean, so longitude 0 faces the world; radiant and weave both sit there at +/-45 deg, giving x = cos45 = 0.70711, lit = x, tide = 0.7 + 1.1*0.5 = 1.25 EXACTLY, and tide[radiant] == tide[weave] bit-for-bit. (b) earth and air are at longitude 180, unseen, so tide = 0.7 exactly. (c) day 15 is new moon: every one of the eight is within 1e-12 of 0.7. (d) with nod offset 5 (f_nod = 0.25, sin(pi/2) = 1, gamma = +30), radiant = 0.7 + 1.1*cos^2(15deg) = 1.72632 (> SURGE) while weave = 0.7 + 1.1*cos^2(75deg) = 0.77370, so the still hemisphere outranks the restless one; nod offset 15 (gamma = -30) reverses it exactly. (e) almanac(fixture, 0) gives next_grand_alignment_day == 0, full_moon days == [0, 30, 60, ..., 330], and day 15 among the hollow_night days. (f) day_state(fixture, 7.5) gives phase_fraction exactly 0.25, proving the fractional-day path. Other invariants worth porting as tests: every region vector has unit length to 12 places and |y| == sin(45deg); `list(tide(...)) == list(KNOWN_SCHOOLS)`; `tide(moon, day) == tide(moon, float(day))`; every lunar_sensitivity row satisfies row[0] == row[-1] and row 0 is a single repeated pole value.

**Closed form of the tide peak, and the structural relationships that explain TIDE_MAX and the surge threshold**

After both rotations, the largest x a region of the leaning hemisphere can reach is `x = cos(latitude)*cos(gamma) + sin(latitude)*sin(gamma) = cos(45deg - gamma)` (achieved on the facing meridian, spin fraction 0, where z = 0), and at full moon lit == x, so the ceiling is `tide_peak(gamma) = TIDE_FLOOR + TIDE_GAIN*cos^2(45deg - gamma) = 0.7 + 0.55*(1 + sin 2*gamma)`. Consequences the spec needs: (1) at gamma = 35 this is 0.7 + 0.55*(1 + sin70deg) = 1.76683, the quoted 'near 1.77'; (2) at gamma = 45 it would be exactly 0.7 + 1.1 = 1.8, so TIDE_MAX is not arbitrary and not a vestige — it is TIDE_FLOOR + TIDE_GAIN, the exact supremum the formula would reach if the tilt could equal the 45-degree hemisphere latitude, and it is unreachable only because TILT_RANGE stops at 35 and because visible*lit <= 1 for unit vectors; (3) SURGE = 1.6 is crossed only when gamma >= 19.77deg, which is what makes a surge a genuine event rather than a daily one; (4) HOLLOW = 0.75 requires visible*lit <= 0.04545 for all eight simultaneously. This closed form also gives a porter an O(1) assertion for the clamp bounds instead of a sampling test.

### Missing — minor

**All three cycles are sampled only at integer days inside `almanac`, so the continuous peak is essentially never attained**

`almanac` evaluates `tide(moon, start + d)` for integer d only (0..359 for events, and the 30 integer days per month for mean_tide). The spin, nod and synodic fractions therefore take only P_spin, P_nod and P_synodic discrete values, and the simultaneous coincidence that produces the continuous ceiling (facing meridian AND full moon AND maximum lean on the same integer day) is essentially never sampled. A porter must not expect a year to contain a surge even when the tilt is high: a conformance test of the form 'a generated year contains at least one surge event' is flaky by construction. The only safe assertion is the conditional one the reference test uses — if an event of kind 'surge' exists then `tide(moon, start + event['day'])[event['school']] >= 1.6`, and if one of kind 'hollow_night' exists then all eight factors on that day are <= 0.75.

**The module's shared tables are exported into the world by reference, not copied**

`add_astrology` writes `'hemispheres': HEMISPHERES`, `'period_ranges': PERIOD_RANGES` and `'formulas': FORMULAS` — the module-level objects themselves, not copies (only `regions` is rebuilt, as `{school: list(v) for ...}`, and only `magic['lunar_surge']['factors']` is copied, via `dict(factors)`). A caller that mutates an exported world therefore mutates the module constants for the rest of the process, and two worlds generated in one process share those sub-objects. Likewise `lunar_sensitivity` aliases one `empty = [[0.]*n for _ in range(n)]` grid into up to ten input slots. Both are benign in the reference because nothing writes through them, but a C++ port must hand out copies or const references and must not expose a shared mutable grid.

**The exact text and companion checks of the load-time moon validation in `validate_age_world`**

The spec says to port the check but not what it says or what it sits beside. Verbatim: it first requires `astrology` and `lunar_almanac` to both be dicts with `version == 1`, else `missing_block('astrology', 'This world predates the moon. It is seeded during generation, so regenerate rather than migrate.')` — a DIFFERENT message from `lunar_request`'s ('This world lacks the astrology contract at version 1...'), so there are two refusal strings, not one. Then, for each of the three PERIOD_RANGES keys: `if type(period) is not int or not low <= period <= high or type(offset) is not int or not 0 <= offset < period: raise ValueError('Invalid moon ' + key + ' cycle')`. Then `if type(moon['tilt_max_degrees']) not in (int, float) or not TILT_RANGE[0] <= tilt <= TILT_RANGE[1]: raise ValueError('Invalid moon tilt')`. Then `if moon['great_year_days'] != math.lcm(*moon['periods'].values()): raise ValueError('Invalid great year')` — note this splats the periods dict, so it silently depends on the dict holding exactly the three cycles and no more. These raise bare ValueErrors, not structured refusals.

**`lunar_context` derives the age's moon day by its own path, not through `reported_year()`**

The spec implies one year-resolution path. There are two, which agree by construction rather than by sharing code: `refresh_astrology` calls `reported_year(result)` (which returns the `(year, source)` pair and reports `'epoch'` when absent), while `terrain_history.lunar_context` computes `day = int((result.get('settlements', {}).get('founding') or {}).get('end_year', 0)) * DAYS_PER_YEAR` inline, with a literal 0 default and no source string. It also short-circuits with `if 'astrology' not in result: return None, None`, and pulls the influence with a direct index `options(cfg)['lunar_influence']` (KeyError if absent, unlike every `.get` around it). The age record it builds is a strict SUBSET of `day_state`: `{day, phase_fraction, elongation_degrees, illumination, leaning, tide}` — it deliberately drops `moonrise_hour`, `spin_fraction` and `nod_degrees`, so `history.ages[].moon` cannot answer a lean-magnitude question even though `day_state` computed it.
