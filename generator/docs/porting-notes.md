# Porting notes: what was expensive to learn, and what is still true

Carried over from the Python FantasyWorldGenerator, which this project replaces and which
will be abandoned. Most of what a porter needs lives in that tree's `Sim/` modules and its
`docs/conformance/` records; this file holds the part that lived nowhere but in comments and
in a pre-port red team, and that would have been lost with the repository.

Measured against that tree at `4e65893`, 2026-09-21, CPython 3.12.10.

Each item is marked with whether the clean-rewrite decision retired it.

---

## Retired by the clean rewrite

These were real, and they are no longer this project's problem. They are recorded because
"why doesn't this match Python?" is a question someone will eventually ask.

**Builtin `sum()` over floats is Neumaier-compensated from CPython 3.12.** Not a faster
loop — a different value. Shadowing `sum` in the reference's globe module with plain
left-to-right accumulation moved **457 of 1089** TPI cells on a size-33 field. The
reference's package metadata declared `requires-python = ">=3.9"`, so it invited an
interpreter that produced different worlds from the ones its own documents measured.
`math.hypot`'s algorithm also changed inside that range. → **Retired.** This library sums
left to right and says so.

**Four different seed-derivation idioms were live** in the reference, and only one was
described in any record: `sha256[:4]` big-endian, `sha256[:8]` big-endian, `sha256[:8] /
2**64` as a float weight, and a 64-character **hex string** passed to `random.Random` —
which hashes the string rather than using an integer, and which forced a hand-rolled SHA-512
in the C++ prototype purely to reproduce it. → **Retired.** One derivation here:
`Rng::child` / `child_seed`, SHA-256 over a namespaced string.

**Python's `round(x, n)` is correctly-rounded-to-decimal with banker's ties**, and 161
non-test call sites fed published surface heights through it. `std::round` and the common
`floor(x*1e4 + 0.5)` idiom are neither. **Five sites hashed `json.dumps(...)` output into a
SHA-256 that shipped inside the world**, which put CPython's shortest-round-trip float
`repr` inside a published digest — and the sites did not all use the same separators. →
**Retired.** Nothing here rounds for presentation, and `format_double` uses `std::to_chars`
shortest round-trip, which is a property of the C++ standard rather than of an interpreter.

**`random.sample` picks between two algorithms by population size**, and the two consume
different word sequences, so the choice is part of the stream. Both branches occurred in the
reference — a large city took one, a small or heavily clipped site the other. → **Retired**,
and [determinism.md](determinism.md) §2 records the rule that replaced it.

**`gauss` caches a spare deviate between calls, and a rejected loop iteration carries it.**
The reference's plate layout draws three gaussians per attempt and may `continue`; three is
odd, so the spare alignment survives the rejected attempt. A `gauss` without the persistent
spare gives a different planet, not a different ulp. → **Retired**, deliberately: see
[determinism.md](determinism.md) §2 for why the spare is discarded here.

---

## Still true, and still able to bite

**Argument evaluation order was load-bearing in one place.** The reference built a config
whose keyword arguments drew from three interleaved RNG streams inside a single call
expression. Python evaluates arguments left to right; **C++ does not specify the order**.
That path was dead in the reference (it required a parameter combination recipe 3 forbids),
but the hazard is general: never draw from more than one stream inside one expression in
C++. Named child streams make this mostly moot, but "mostly" is not "never".

**Float `//` floors toward negative infinity; C++ integer division truncates toward zero.**
And Python's `%` returns the sign of the divisor. `grid.cpp`'s `floor_mod` exists for
exactly this: the neighbour scan reaches `x = -1` and must land on `n-2`, not on `-1`.

**Both settlement planners forked a process pool by default**, controlled by an
**undocumented** environment variable (`ICARUS_CITY_WORKERS`) that appeared in zero `.md` or
`.json` files in the whole repository and that one variable controlled *both* planners.
Nothing compared pooled output to serial output, nothing set `workers=1`, and the
byte-reproducibility gate ran through the pooled path. Two reviewers read both paths and
believed they agreed; neither executed it. **Whether pooled and serial output must be
byte-identical was never ruled on.** When stage 16 lands here and someone reaches for
parallelism: decide that question first, and write the equality test before the pool.

**Both planners ordered sites by `str(uid)`**, so `"10" < "9"`. Deterministic, and portable
only if you know not to sort numerically.

**Set iteration over strings was believed safe and asserted nowhere.** `PYTHONHASHSEED` was
pinned nowhere in the reference; the sampled sites happened to funnel through `sorted()` or
`min()`. No test claimed it and no document did. Treat every unordered iteration that
reaches output as a defect — see [determinism.md](determinism.md) §3.

**The reference's acceptance suite passed on empty output in three places.** The planner
tests carried an explicit `or all(status == 'unbuildable')` escape hatch; the quest lifecycle
tests opened with `skipTest('this world raised no quest hooks')` and nothing asserted that
the shared world raised any, so an implementation emitting nothing turned the whole
offer/expire/resolve spec into green skips. The lesson is the one this suite's header
states: **a test that cannot fail is not a control.** Where a floor is needed, derive it —
do not pin a histogram, because the pin gets updated to match whatever came out.

**The reference's acceptance spec covered 26 of 140 modules**, while three of its own
documents said it covered all of them. Anything written about coverage here should be a
measured number or nothing.

**A metric that ignores its own knob is the measurement failing.** This one cost real time
in the reference. `land_fraction_answers_to_sea_level` in `tests/test_world.cpp` is the
shape of the test that catches it.

---

## What this document is not

- **Not a port plan.** It is the list of things that are invisible until they are wrong.
- **Not complete.** It covers the substrate and the traps, not the algorithms.
- **Not tested.** Nothing here is asserted by a test in this repository, and the measured
  claims were taken against a tree that no longer needs to exist. Re-verify before relying
  on any line of it.
