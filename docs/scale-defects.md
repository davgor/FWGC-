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

**Nothing here is fixed.** Every one of these changes every world it touches, so the
choice is the owner's. What the port does instead is pin each of them with a test, so
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

**Irrigation, everywhere.** A people's `water_reach` trait is 400–650 m and freshwater
distances on a shipped world are kilometres, so `exp(-distance / water_reach)` underflows
to zero at every cell. `food_potential` therefore equals `natural_food_potential` on every
world this generator makes, and `irrigation_benefit` is identically zero. The irrigation
rules are correct and unreachable.

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

## Degraded: fires, but not as authored

**Sea routes are an artefact of resolution.** `sea_reach` is 6000 m, roughly two cell steps
at size 65. Measured: **0 sea routes at size 33, 4 at size 65, 10 at size 129.** The network
grows with the raster rather than with the geography, which is backwards.

**Hamlet and fortress spacing decide almost nothing.** 120 m and 200 m against a smallest
neighbour step of ~153 m collapse to "not on the exact same node". The published
`defence_method` prose still claims the 200 m spacing decides how many forts exist; at
shipped widths the city-count ceiling decides, and the spacing decides nothing.

**A river crossing is nearly free.** `bridge_cost` is 200 m, about 13% of a cell step where
it was 2.3 cells at the reference.

**Trade is priced for an 11 km world.** The 4000/5000 decay and price scales give a 30 km
road an efficiency of about 5.5e-4. Trade still happens — 18 shipments at size 65 — but
every number in it was chosen for distances eighteen times shorter.

**The fortress junction preference has lost its degree.** A `d/500` term against a 1562 m
step is −3.1 on a base of 1 to 4, so the route node always wins outright. At the reference
it was −0.17, a mild preference. The direction survives; the gradation does not.

## Two more, found earlier and pinned

**`maritime` is a constant.** It is `exp(-distance_to_ocean / 300 m)`, so the first land
cell inland reads effectively zero. `human_maritime`'s habitat needs 0.3 and therefore
settles **no world at any shipped size**; 21 authored shore nest species place nothing.
The only cells clearing the threshold are a polar artefact where longitude convergence
brings neighbours within 153 m. Pinned in `test_habitation.cpp` and `test_nests.cpp`.

**Of three authored nest distances only the ramp span scales.** The clearance and the
spacing do not, so the widest monster clearance can refuse only a settlement's own cell.

## Correctly scale-free

Worth recording so that nobody "fixes" them: `tpi/30` and the 80 m air clearance are
relief-denominated and relief is deliberately constant across the presets; `sea_draft` and
the fishing draft are depths; the 1e6 m²→km² conversion and the 100 residents per
productive km² are area-denominated; the 0.25 radian sky-cluster separation is an angle.
Every dimensionless threshold is fine by construction.

## One that blocks reproducing the reference world

The relief budget guard refuses an 11.15 km world at the default 1667 m relief: *"11.15 km
admits at most 206.07 m"*. The C++ generator cannot build the reference world at shipped
relief at all. Tests that need reference-width behaviour pass `relief_m = 200`.
