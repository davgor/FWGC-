# Determinism: what a seed promises, and what can quietly break it

A world is one long chain of floating-point operations seeded by one integer. The promise
this library makes is narrow and worth stating exactly:

> **The same version of this library, built with the flags in `CMakeLists.txt`, by the
> same compiler against the same C runtime, produces byte-identical world documents from
> the same request — in any process, on any machine.**

Verified across separate processes at size 129 on MSVC 19.44 / UCRT: two runs, one SHA-256.
The thread count is not an input either (§4): on 2026-09-23 eight worlds, sizes 33 to 257,
hashed the same on 1 and 24 threads and under the serial code the pool replaced.

**"The same C runtime" is load-bearing and was missing from an earlier draft of this
sentence.** The generation path calls `sin`, `cos`, `exp`, `log`, `tanh`, `asin`, `atan2`
and `hypot` — 30-odd call sites across `grid.cpp`, `tectonics.cpp`, `measure.cpp` and
`random.cpp`, including `std::log` inside `Rng::gauss`, which puts libm inside the *random
stream* and not merely inside the terrain arithmetic. **None of those functions is required
to be correctly rounded**, and MSVC's UCRT, glibc and Apple's libm genuinely disagree in the
last bit. No compiler flag fixes that; only shipping our own implementations would, and
that has not been done. So a world is a property of the C runtime as well as of this
source, and the promise cannot be widened until that changes.

Four things are *not* promised, and each is a deliberate limit rather than an oversight.

**Worlds do not match the Python FantasyWorldGenerator.** This is a clean rewrite. Seed 42
here and seed 42 there are different planets. That was the explicit trade: the alternative
was reproducing CPython's Mersenne Twister, its `random.sample` algorithm selection, its
`gauss` spare cache, its Neumaier-compensated `sum()`, its banker's rounding and its
shortest-round-trip float `repr` — roughly two thousand lines of substrate whose only
purpose is to agree with an interpreter this project no longer uses.

**Worlds do not survive a change to this library.** Tuning the hypsometric curve or the
erosion proxy changes every world. Worlds are disposable; the seed plus the library version
is the world's identity, and a stored world document is a cache, not an archive.

**Worlds do not survive different compiler flags.** See §1 below.

**Worlds are not promised to match across C runtimes.** See the note above. The GCC/Clang
half of §1 has never been executed — only one toolchain exists on the machine this was
written on — so the non-MSVC flags are reasoned, not verified. Treat a first build on
another platform as an experiment, not a regression.

---

## 1. The float contract

`CMakeLists.txt` builds every target through the `fwg_flags` interface target, and that
target is **part of the library's public interface** — it propagates to anything that links
`fwg`. A consumer that compiles this code with different float settings gets different
worlds and no warning.

| | MSVC | GCC / Clang |
|---|---|---|
| contraction (FMA) | off — `/fp:precise` leaves it off; it is opt-in via `/fp:contract` | off — `-ffp-contract=off`, which is **not** the default |
| reassociation | off — never set `/fp:fast` | off — `-fno-fast-math` |

**GCC and Clang contract to FMA by default.** `a*b + c` becomes one rounding instead of
two, and the result differs in the last bit. That bit compounds through erosion passes and
moves cells across the sea-level threshold. This is the single most likely way a port of
this port silently diverges, and it is why `-ffp-contract=off` is not optional.

MSVC has no `/fp:contract-`; contraction is opt-in there, so `/fp:precise` is the whole of
what is needed. Passing `/fp:contract-` is silently ignored with warning D9002.

`std::pow` is avoided in the hot path. `f*f*f` is exact and identical everywhere;
`std::pow(f, 3)` is a libm call whose last bit is a property of the C runtime, and on
Windows the static and shared CRTs disagree with each other by one ulp.

That removes **one** libm dependency; it does not remove the class. The transcendentals
listed at the top of this document are still libm calls and still not correctly rounded.
Avoiding `pow` is worth doing and is not the same as being libm-independent — an earlier
version of this section implied it was.

## 2. The random stream

`fwg::Rng` is xoshiro256++ seeded through SplitMix64. It is hand-rolled, and `<random>` is
deliberately unused, for one reason worth knowing:

> **`std::uniform_real_distribution` and `std::normal_distribution` are not specified to
> produce the same values across standard library implementations.** Only the *engines* are
> portable. A world generated against libstdc++ would not match one generated against
> MSVC's STL.

So every distribution here is written out: `random()`, `uniform()`, `below()`, `randint()`,
`gauss()`, `shuffle()` and `sample_indices()`.

Three choices inside them are load-bearing:

**`gauss` discards the polar method's spare deviate.** Caching it — which is free, and which
CPython does — makes the number of stream words a call consumes depend on whether the
previous call happened. An early `continue` in a rejection loop then shifts every draw after
it, and the planet changes. The reference implementation's plate layout draws three
gaussians per attempt; three is odd, so the spare alignment survives a rejected attempt and
the bug is invisible until you compare worlds. The wasted deviate is cheaper than that class
of defect.

**`sample_indices` uses one algorithm at every population size.** CPython's `random.sample`
switches between a selection set and a pool depending on how large the population is, and
the two consume different amounts of stream — so the choice of algorithm is observable in
the output, and a port implementing one branch produces correct-looking worlds that diverge
on size.

**`below` rejects rather than taking a modulo.** The bias would be unmeasurable on a 64-bit
draw, but rejection is unbiased and just as deterministic, and it costs nothing.

Subsystems take **named child streams** (`Rng::child`, `child_seed`) derived through
SHA-256 from the master seed and a domain string. Two subsystems never share a stream, so
adding a draw to one cannot move the other. That is what makes it safe to work on one stage
without silently rerolling every other.

## 3. Ordering

Anywhere a set or a map decides iteration order, the order is part of the world. Two rules:

- Routing ties in `erosion.cpp` break on the higher node index. Arbitrary, but fixed, and
  independent of the order neighbours happen to be visited in.
- `sphere_grid` builds its neighbour lists from a `std::set<int>`, so they are sorted by
  node index, not by insertion.

Never iterate an unordered container in a way that reaches the output.

## 4. Traps that are invisible until they are wrong

Each of these cost time once, and none is caught by the compiler.

- **Argument evaluation order.** C++ leaves it unspecified. Never take more than one draw,
  from one stream or several, inside a single expression: bind each draw to a named local
  first, so the order is the order of the statements.
- **Division and modulo of negatives.** Integer division truncates toward zero and `%`
  takes the sign of the dividend. Where a floor is meant -- a neighbour scan reaching
  `x = -1` must land on `n - 2` -- use `grid.cpp`'s `floor_mod`, never a bare `%`.
- **Ids sorted as strings.** Blocks ordered by id -- the castle plans by fortress id, the
  quest board by quest id -- compare strings, so `"10" < "9"`. That order is published; do
  not "fix" it into a numeric sort.
- **Parallelism.** Stage 16's three planners run on a pool, and the ruling that came with it
  is that a world is **byte for byte the same on any number of threads**. The equality test
  was written before the pool, while it could only pass trivially: a reference planner once
  parallelised behind an undocumented variable, and nothing ever compared the two paths.

  *What runs in parallel.* Every hamlet plan and every castle plan; and each city's plan in
  two parallel halves -- the survey (window, ground raster, elevation surface, the seven site
  facts) and the finish (streets, walls, packing, housing). *What stays serial:* the cities'
  shape draws, one at a time in key order between the two halves, because each is weighted
  by the repetition tally the cities before it left. A draw costs microseconds; the survey
  and the finish are the cost. Everything outside the three planners is serial, the world
  scene built from their plans included.

  *Why the bytes cannot depend on it.* Each task reads only what no task writes -- the world,
  the grid, the site and pin lists -- and writes only its own slot of a vector sized before
  the pass, so results land in index order whatever order threads finish in. The planners
  keep no mutable static state: their tables are `constexpr`, the catalogue digests are
  function-local statics (initialised thread-safely, and first read before any pass starts),
  every random stream is a child of the seed keyed on the site's own key or uid, and the
  sphere-grid cache, which is locked anyway, is not called from a pass. The tally is the one
  piece of cross-city state, and only the serial walk touches it. A failure is the serial
  loop's: the pool rethrows the exception of the LOWEST index that threw, and a city whose
  survey fails holds that failure until the walk reaches it -- an earlier city may yet fail
  in its finish, and that is the one serial planning reports. And every task runs in the
  calling thread's floating-point environment: a new thread starts in the default one, so a
  host that rounds upward or flushes denormals would otherwise get one world from the pool
  and another from the loop.

  *The count is not part of the world.* `fwg::set_worker_threads` (workers.hpp), `fwg generate
  --threads N` or `FWG_THREADS`: 0 is every hardware thread, the default, and 1 is the plain
  loop with no thread started. It is a process setting and never a field of Config or
  Request, both of which are written into the document.

  *The test that holds it.* `tests/test_workers.cpp` generates worlds at 1 worker and at 2, 3
  and every hardware thread and compares the SHA-256 of the whole compact document: size 33
  at seeds 42 and 7, size 65 at seed 42, and a redraw over a player's city. Each world first
  proves an ordering defect could show in it -- plans from all three planners, and a shape
  worn by two cities. Beside it: fill_cities against the one-at-a-time loop it replaced;
  failures at every count against the serial failure; the lowest-index rule; and the
  floating-point environment, checked on every pool thread and compared as bits. Each was
  sabotaged before it was trusted: letting every city read the tally as it stood when the
  city started -- the obvious pool -- changed the document at 2, 3 and 24 threads.

## 5. What is deliberately outside the document

Wall-clock timings are **not** written to the world document by default. They are the one
part of a world that is not a function of its seed, and including them would cost the
property worth most downstream: that a document can be hashed, cached and compared.
`--timings` puts them back for a human reader. The numbers stay on the `World` struct
either way.
