# Authored distances that stopped working when the world grew

The reference generator was authored for a world **11.15 km around**, whose raster cells
are about 87 m across. The shipped world presets are **200, 400 and 600 km** around, with
cells of 1562 m (size 129), 3125 m (size 65) or 6250 m (size 33) at the 200 km default.

Three distances scale with the planet. Everything else authored in metres does not, and
was tuned against a world eighteen times smaller. This document lists what that costs,
because several whole systems do not exist at any shipped width and nothing in the
generator says so.

This is inherited from the reference, not introduced by the port. The reference's own
authors diagnosed exactly this disease once — `island_habitat` had an absolute-metre
floor that had become unreachable, and there is a long comment recording the measurement
and the fix — and did not sweep the rest.

**Two are fixed, by the owner's rulings** (both 2026-09-23): the fortress junction
preference and a lair's reach, each recorded where it stands below. Every other one changes
every world it touches, so the choice is the owner's. What the port does instead is pin each of them with a test, so
that a fix cannot land silently and so that the list cannot quietly grow.

## What scales

Exactly three, via `reach_scale(circumference)`. At the 200 km preset the factor is
×17.94:

| Setting | Authored | At 200 km |
|---|---|---|
| `support_reach` | 1000 m | 17 939 m |
| `settlement_spacing` | 450 m | 8 072 m |
| `culture_link_cost` | 1800 m | 32 290 m |

Anything derived as a MULTIPLE of these is therefore safe. The villain and corruption
reaches are all multiples of `settlement_spacing`, and the four beast reaches likewise —
both ports state so explicitly and test it by running the same world at two spacings.

## Dead: cannot fire at any shipped width

**The whole air network.** `air_reach` is 4500 m, and the nearest pair of air terminals on
a generated world already exceeds it. Measured: **zero air routes at sizes 33, 65 and
129.** With it go the terminals, the gnome industrial minimum, the 0.12 weave threshold,
cruise altitude, the 8000/5000 decay and price scales and the 80 m clearance. The option's
own maximum of 50 000 m would be needed before a single route existed.

**Irrigation, very nearly.** A people's `water_reach` trait is 400–650 m against a cell
step of 1562–6250 m, so `exp(-distance / water_reach)` underflows as soon as a cell is
even one step from water.

Measured rather than assumed, because the first version of this document said irrigation
was *identically* zero and that is not true. It fires on 75 cells at size 33, 535 at size
65 and 2433 at size 129 — the cells standing on or immediately beside water, where the
distance is zero or near it. The mean gain where it fires at all is 0.0008 to 0.007, and
the largest single gain is about 0.17.

So the rule is not unreachable; it is confined to the water's edge, where the authored
intent was a reach of several hundred metres around every watercourse. A system that was
meant to shape a region instead decorates a shoreline.

**Every hamlet's catchment is the cell it stands on.** The catchment reach is 250 road-cost
units and one non-polar step already costs more than that. The city-node exclusion, the
slope gate and the cross-catchment refusal inside the delivery loop are all consequently
unreachable too.

**Every fishery is one cell.** `fishing_reach` is 700 m against a 3125 m step. Even the
option's maximum of 4000 m is under three cells.

**The hamlet access-cost floor of 100** is dead at *every* width, including the reference
one: a road cost is at least the great-circle length of the path it prices, so any node the
120 m separation admits already costs more than 100.

**The 200 m hut-to-hut separation** is below one cell, so it never refuses anything.

**A city's node-scale layout claims its own node or nothing, and draws no building asset.**
The district anchor search walks `max(180, 4 × river_buffer_m + 180)` metres from the city
-- 540 m for the standard profile, 460 m for the riverfront -- over a raster whose cells are
kilometres across, so the only node it ever holds is the city's own. Every city therefore
reports at most one district anchor against targets of a dozen or more, a `fallback` reason,
and `required_asset_count` 0: the building-pack asset draw has no node left to place on.
Measured at seed 42, phase 10: sizes 33 and 65 hold 0 or 1 anchor per city and size 129 one
city with 2 (a near-pole neighbour closer than the band), with no asset drawn at any of the
three. The reference's own record says the same of its worlds and assigns the fix to
SCALE-METRE-CONSTANTS-COLLAPSE. Pinned in `test_city_layout.cpp`, which proves the draw on a
synthetic 20 m lattice instead.

## Degraded: fires, but not as authored

**Sea routes are an artefact of resolution.** `sea_reach` is 6000 m, roughly two cell steps
at size 65. Measured: **0 sea routes at size 33, 4 at size 65, 10 at size 129.** The network
grows with the raster rather than with the geography, which is backwards.

**Hamlet spacing decides almost nothing.** 120 m against a smallest neighbour step of
~153 m collapses to "not on the exact same node". The fortress spacing had the same defect
until the fort rule was redesigned (2026-09-23, porting-status.md ruling 4): it is now the
reference's 200 m scaled with the world, 3.6 km on a 200 km world, and it binds.

**A river crossing is nearly free.** `bridge_cost` is 200 m, about 13% of a cell step where
it was 2.3 cells at the reference.

**Trade is priced for an 11 km world.** The 4000/5000 decay and price scales give a 30 km
road an efficiency of about 5.5e-4. Trade still happens — 18 shipments at size 65 — but
every number in it was chosen for distances eighteen times shorter.

**The fortress junction preference had lost its degree** -- a `d/500` term against a 1562 m
step was −3.1 on a base of 1 to 4, so the route node always won outright, where at the
reference it was −0.17. Fixed with the fort rule (2026-09-23): the 500 m scales with the
world as the spacing does, which restores −0.17 at size 129.

## Two more, found earlier and pinned

**`maritime` is a constant.** It is `exp(-distance_to_ocean / 300 m)`, so the first land
cell inland reads effectively zero. `human_maritime`'s habitat needs 0.3 and therefore
settles **no world at any shipped size**; 21 authored shore nest species place nothing.
The only cells clearing the threshold are a polar artefact where longitude convergence
brings neighbours within 153 m. Pinned in `test_habitation.cpp` and `test_nests.cpp`.

**Of three authored nest distances only the ramp span scales.** The clearance and the
spacing do not, so the widest monster clearance can refuse only a settlement's own cell.

**A lair's reach held no city -- fixed** (2026-09-23, "beasts are a real menace"). The threat a
nest puts on a city, min(radius / 2, max(350 m, 2 x range)), was reference metres: seed 42
measured 0 of 34 cities threatened at size 33, 4 of 43 at 65, 1 of 47 at 129, so no monster
ever razed a city and no dread ever formed. It now passes through `reach_scale`
(`nest_threat_reach_m`), and only the strongest lair in reach weighs on a city, since a dozen
or more stand in the scaled reach. Still in reference metres: the reeve well's nest reach
(1.5 x range) and the champion well's (3 x range) -- so a reeve finds a lair only through a
dread's scaled reach, and a city's own champion almost never finds one -- and the nest
clearance and spacing above. The champions of the razed cities come from the ruins, not a
reach, and are unaffected.

## Correctly scale-free

Worth recording so that nobody "fixes" them: `tpi/30` and the 80 m air clearance are
relief-denominated and relief is deliberately constant across the presets; `sea_draft` and
the fishing draft are depths; the 1e6 m²→km² conversion and the 100 residents per
productive km² are area-denominated; the 0.25 radian sky-cluster separation is an angle.
Every dimensionless threshold is fine by construction.

**The world scene's 4 m road densification is ground-denominated, and its cost is not.**
`world_scene` drapes every regional road onto the height field one vertex per 4 m of arc
(`kSceneRoadStepM`). The step is a sampling density for the ground under a road — the
same metre as the planners' 4 m street cell, and finer than the 6 m finest detail band —
so it does what it was authored to do at every width, and scaling it with the planet
would under-sample the very relief it exists to follow. What grows is the vertex COUNT,
linearly with total road length. Measured, seed 42, phase 16, Release, this machine (the
payload counts every position and axis at 24 bytes and no ids; the time is the whole
scene, best of three calls):

| World | Size | Roads | Road arc | Road vertices | Share of the scene's vectors | `build_world_scene` |
|---|---:|---:|---:|---:|---:|---:|
| 11.15 km (reference, `relief_m` 200) | 65 | 1 | 0.9 km | 220 | 6% of 0.09 MB | 0.4 ms |
| 200 km | 33 | 25 | 311 km | 77 695 | 42% of 4.4 MB | 14 ms |
| 200 km | 65 | 28 | 366 km | 91 644 | 37% of 6.0 MB | 30 ms |
| 200 km | 129 | 37 | 448 km | 112 343 | 36% of 7.5 MB | 43 ms |
| 600 km | 65 | 33 | 1 167 km | 291 886 | 61% of 11.5 MB | 63 ms |

So it is not enormous at any shipped width — tens of milliseconds and at most a dozen
megabytes — but it is the one part of the scene that grows with the planet rather than
with the number of settlements, and at 600 km it is most of it. If that ever matters, the
remedy is to densify on demand from the coarse node path and its two gates, both of which
the world already carries, not to lengthen the step. The step is pinned both ways — no
longer than 4 m, no denser than one vertex per 4 m per coarse segment — in
`test_world_scene.cpp`.

## One that blocks reproducing the reference world

The relief budget guard refuses an 11.15 km world at the default 1667 m relief: *"11.15 km
admits at most 206.07 m"*. The C++ generator cannot build the reference world at shipped
relief at all. Tests that need reference-width behaviour pass `relief_m = 200`.
