# Verification of the kernel against the reference

A clean rewrite gives up the one cheap acceptance test a parity port would have had: you
cannot diff the output. What is still available is a **distributional** comparison — the
two implementations draw different worlds, but from the same distribution, so aggregate
statistics over the same seeds must agree.

That comparison was run while stages 1–5 were being written, against the Python
FantasyWorldGenerator at `4e65893`, CPython 3.12.10. It is recorded here because the
reference is being retired and this is the last moment the comparison can be made.

**None of this is asserted by a test in this repository.** It was measured once. Re-measure
before relying on it, while the reference still exists.

## 1. The noise field is bit-compatible

`crust` depends on exactly two things: `child_seed(seed, "crust", 0)` and `perlin3`. It does
not touch the random stream. Mean crust over every distinct cell, size 65:

| seed | Python | C++ |
|---:|---:|---:|
| 42 | 0.4148 | **0.4148** |
| 7 | 0.5512 | **0.5512** |
| 1234 | 0.5219 | **0.5219** |
| 99 | 0.5282 | **0.5282** |
| 2026 | 0.4725 | **0.4725** |

Identical on all five. The SHA-256 child-seed derivation and the lattice-hash gradient noise
are therefore reproduced exactly, which is the intent stated in `noise.hpp`. (The quintic
blend uses `f*f*f` rather than `pow(f,3)`, so individual samples may differ in the last bit;
nothing at four decimals sees it.)

## 2. The plate draw is distributionally identical

The RNG is the deliberate departure, so the plates are the thing most likely to be subtly
wrong. 200 seeds, 12 plates each, 2400 angular velocities:

| | Python | C++ | expected |
|---|---:|---:|---|
| mean \|omega\| | 0.6229 | 0.6281 | 0.625, from `uniform(0.25, 1)` |
| min \|omega\| | 0.2503 | 0.2507 | → 0.25 |
| max \|omega\| | 0.9995 | 0.9993 | → 1.0 |
| mean of max pairwise \|Δomega\| per world | 1.6193 | 1.6429 | — |

## 3. The derived fields agree in distribution

Size 33, seeds 0–23:

| statistic | Python | C++ |
|---|---:|---:|
| `convergence` max, mean over seeds | 0.5047 ± 0.0977 | 0.4860 ± 0.0907 |
| `height` max, mean over seeds | 7761 ± 2564 m | 8960 ± 3114 m |

And at size 129, seed 42, the per-cell means (which are far more stable statistics than the
maxima): height mean −1179.6 vs −1174.6 m, `crust` 0.4166 vs 0.4166, `convergence` 0.0211 vs
0.0211, `volcanic` 0.0034 vs 0.0034.

**A caution about small samples, recorded because it nearly produced a false conclusion.**
The first comparison used five seeds and found C++ `convergence` max higher on five of five,
which looks like a systematic skew. Widening to 24 seeds reversed the sign (C++ slightly
lower) and showed the two distributions overlapping almost exactly. A maximum over a
heavy-tailed field is a high-variance statistic; five samples of it prove nothing. The means
were the reliable signal and they matched from the start.

## 4. Cost

Same kernel, same work, size 129, phase 5:

| | time |
|---|---:|
| Python `generate_tectonics` | 1.4 s |
| C++ `fwg::generate` (whole run, process included) | 32 ms |

About 44×. The scaling is linear in cells in both: C++ measures 2.0 / 8.0 / 32.3 / 130 / 568
ms at sizes 33 / 65 / 129 / 257 / 513.

## 5. What this does not establish

- Nothing about stages 6–16, which are not written.
- Nothing about whether the *model* is good — only that this implementation of it matches
  the reference implementation of it. Both produce ~20 km peaks at the 200 km preset with
  `orogeny` 10, which is far above the "4 km peaks" the reference's own comment anticipates.
  That is a question about the recipe, not about the port, and it is open.
- Nothing about any individual world. Distributions agreeing does not mean any particular
  seed produces a comparable planet, and it cannot: the plate layouts differ.
