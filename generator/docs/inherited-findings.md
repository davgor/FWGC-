# Inherited findings — the pre-port red team, triaged against this codebase

The [archived red team](archive/2026-09-21-pre-port-red-team.md) swept the Python
`FantasyWorldGenerator` on 2026-09-21, the day before the port was scoped. It was written
about a different language, a different implementation and a repository that is being
retired, so most of it owes this codebase nothing — and some of it is the most useful
writing anyone has done about this project.

This file separates the two. Every finding in that document is classified here as
**live** (binds now), **later** (binds on a stage not yet written), or **retired** (dies
with the Python repo, or with the clean-rewrite ruling).

## How this was produced, and how far to trust it

Eight agents each took a section, read it in full, and investigated **this** tree before
classifying — source and tests, not this repository's own documentation, which was written
in one sitting by the same person who wrote the code and is exactly the kind of claim the
exercise exists to check.

Every finding a section agent marked *already handled* was then given to a separate agent
instructed to **refute** it and to default to refuted when uncertain. **Seven claims were
made and all seven were refuted.** Each refutation was then read by hand and each one held
— they were specific defects in real files, not a default-to-refuse reflex. That is why
this document contains no `handled` column: nothing survived as handled without work, and
the work is listed in the next section.

A final agent read the whole review again and asked what the triage itself had missed; it
returned 10 further gaps, several sharper than anything in the sections.

**The limits, stated plainly.** Agents classified these; a human read the seven
refutations and the ten gaps, and spot-read the rest. A `later` entry is a judgement that
something does not bind *yet* — if a stage lands and its entry turns out to have bound all
along, the classification was wrong, not the finding. Nothing here is asserted by a test
except where the next section says a test now exists.

## What this turned up in code already shipped, and what was done about it

Seventeen defects in the tectonic kernel, all introduced in the same session that wrote
it, all fixed before this document was committed.

**The one worth reading twice.** `Rng::below` carried `if (bound == 1) return 0;` — free,
obviously correct in isolation, and it made the number of stream words a call consumes
depend on the *value* of its argument. That is precisely the defect
[determinism.md](determinism.md) cites CPython's two-branch `random.sample` for, in the
document explaining why this library hand-rolls its distributions to avoid it. A caller
whose bound reached 1 on some seeds would have shifted every later draw on exactly those
seeds. It is now gone, and `tests/test_substrate.cpp` asserts one word per call at five
bounds from 1 to 2^64−1.

| # | Defect | Fix |
|---|---|---|
| 1 | `Rng::below` consumed zero stream words at `bound == 1` — algorithm choice observable in output | shortcut removed; cost pinned at five bounds |
| 2 | `rng_gauss_consumes_a_predictable_stream` compared two RNGs that had done the same thing — **could not fail** | replaced with a test that distinguishes a cached spare from a discarded one |
| 3 | Nothing pinned the RNG stream; a changed rotl or SplitMix64 constant passed the whole suite | golden vectors, cross-checked against an independent Python reimplementation that reproduced all eight words |
| 4 | Stream discipline was described in comments and assertable nowhere | `Rng::draws()` counter; sample cost now pinned at five population sizes |
| 5 | `doubles_survive_the_round_trip` proved round-tripping, not bytes — `%.17g` would have passed | `float_formatting_is_pinned_to_its_bytes` added |
| 6 | `-0.0` case in that test was vacuous (`-0.0 == 0.0` under IEEE) | own case, asserting the sign survives |
| 7 | The byte-reproducibility proof ran at size 17, where 1 of 5 octaves resolves | raised to 129, with a non-vacuity assertion on `resolved_octaves` first |
| 8 | `land_km2 + submerged_km2 == total_km2` is true by construction — submerged is `total − land` | replaced with an independent recount of the cells the integral integrated |
| 9 | Three tests iterated containers with no floor, passing on empty | derived floors on layer count and neighbour degree |
| 10 | `sphere_grid` returned a reference into a two-entry evicting cache | returns `shared_ptr`; a test holds one across two evictions |
| 11 | The latitude-band area formula was written out twice, in `grid.cpp` and `world.cpp` | one `band_wedge_area`, called by both |
| 12 | `Config::step()` was dead code and a third copy of the cell-step formula | now the single definition, called by `tectonics.cpp` |
| 13 | A third seed derivation (`detail_seed + k*1013`) was an unnamed inline expression | named `octave_seed`; all three derivations declared together in `random.hpp` |
| 14 | `Rng::child`'s 64-bit derivation was unused and untested | exposed as `child_seed64` and wired into the plate stream |
| 15 | The CLI narrowed out-of-range integers into legal ones — `--size` past 2^32 silently wrapped | range-checked before the cast |
| 16 | The 64-bit layout seed was written as a JSON number — rounded above 2^53, negative above 2^63 | written as a string |
| 17 | The document reported a `world_size` preset even when `--circumference-km` built the world | `circumference_m` and `world_size_was_used` added |

Two documentation claims were also retracted rather than defended:

- **[determinism.md](determinism.md) promised byte-identical worlds "on any machine".**
  It cannot. The generation path makes ~30 libm calls — `sin`, `cos`, `exp`, `tanh`,
  `asin`, `atan2`, `hypot`, and `std::log` *inside `Rng::gauss`*, which puts libm in the
  random stream itself. None is required to be correctly rounded and the major C runtimes
  disagree in the last bit. The promise now names the compiler and the C runtime. Avoiding
  `std::pow` removed one such dependency and the document implied it had removed the class.
- **[archive/README.md](archive/README.md) called its three files "the ones whose value
  survives"** the Python repo. It lists what it does not cover now.

## Live — binds on this codebase now (37)

| | Area | Finding | What it asks for |
|---|---|---|---|
| **high** | `1` | Plate layout depends on evaluation order inside one expression, unremarked | Widen the rule to: never take more than one draw inside a single expression, bind each to a named local first. Rewrite src\tectonics.cpp:236 and :243 as three named locals. Fix tests\test_substrate.cpp:202. Say in determinism.md why the distinction exists - b… |
| **high** | `api` | Controls published as live that can move nothing - exported by a predicate testing existence, not reacha… | Mark the four carried fields where a consumer actually looks: either omit them from effective_config until a stage reads them, or emit them with a carried/inert flag and a warning, the way world.cpp:80-86 already announces an unreached stage. Decide the bound… |
| **high** | `api` | Reachability, not existence, as the inert-control predicate — and four inert controls ship here now | Make the predicate reachability, not existence: no control may be published in the world document, bound-checked or refused on unless some code path reads it. Concretely, either (a) mark settlement_spacing, support_reach, culture_link_cost and river_threshold… |
| **high** | `api` | Python round() and repr(float) reach published bytes, unmentioned anywhere | Now, on stages 1-5 infrastructure: 1. Pin the bytes, not the round trip. Add golden-string assertions beside `tests/test_world.cpp:417-425`: `format_double(0.1) == "0.1"`, `format_double(1.0/3.0) == "0.33333333333333331"`, `format_double(1e300) == "1e+300"`,… |
| **high** | `build` | Only one toolchain on the box: the GCC/Clang half of the determinism contract has never been compiled | Build once with GCC or Clang (WSL, MinGW or a container) before trusting any cross-machine determinism claim, and compare the SHA-256 of a size-129 document against the MSVC one — that single run is what converts determinism.md from argument to fact. Add a CI… |
| **high** | `build` | Nobody asked what C++ build the acceptance spec assumes | 1. Answer question 1 explicitly. Either narrow the promise at docs/determinism.md:6-9 from "on any machine" to "on any machine with the same C runtime and the flags in CMakeLists.txt", or remove the dependency: replace the libm transcendentals in the generati… |
| **high** | `docs` | The written spec for the unported stages is not in the C++ repo and the repo holding it is being deleted | Copy, before the Python repo goes: the eight conformance records that cover stage 6-16 modules (settlement-records, creature-placement, nomads, encounter-index, time-advance and the world-* records), coverage.json as the measured provenance of the 26/140 figu… |
| **high** | `docs` | State the real coverage number, and extract the 12 unspecified modules before the repo dies | Copy the twelve unspecified modules' source (and the eight surviving conformance records, ~3,350 lines) into docs/archive/ or a vendored reference/ tree before FantasyWorldGenerator is deleted. Prose about them is not a substitute: nothing in FWGC++ will tell… |
| **high** | `docs` | The RNG substrate is documented only in code decision 027 deletes | The substrate is written down; it is not pinned, and what is written down is wrong in two places. Owed now, on stages 1-5: 1. Pin the stream. Add golden vectors to tests/test_substrate.cpp: the first N next_u64() words from a fixed seed, and fixed-seed output… |
| **high** | `test` | Reference environment not pinned: the byte-identity promise outruns what the repo pins or tests | (a) Narrow the promise in docs/determinism.md:6-8 or pin what makes it true: sin/cos/exp/tanh/log/hypot are not correctly rounded and MSVC UCRT, glibc and Apple libm disagree in the last bit, so worlds are a property of the C runtime as well as of the flags.… |
| **high** | `test` | A mandated gate that cannot pass, and aborts before it reaches the determinism proof | Three things the C++ still owes now, on stages 1-5: 1. Raise the byte-reproducibility test off the starved raster and assert its own non-vacuity. In tests/test_world.cpp:51-56, generate at a size where the ladder fully resolves (513 per the CLI; test_substrat… |
| **high** | `test` | Core claims are read-and-believed, not asserted by any test (agreement is not verification) | Add a test that runs the CLI twice as separate processes and compares SHA-256 of the documents (the repo already has src/sha256.cpp and apps/fwg.exe, so this is a few lines of CTest). Until that exists, downgrade the wording in determinism.md from a promise t… |
| **high** | `test` | The test that claims to pin the gauss spare-cache rule cannot fail | Replace the test with one that distinguishes a cached from an uncached gauss: Rng a(5); a.gauss(0,1); a.gauss(0,1); x = a.next_u64(); against Rng b(5); b.gauss(0,1); y = b.next_u64(); assert x != y. A cached spare makes the second gauss consume zero words, wh… |
| **high** | `test` | The byte-reproducibility gate is same-machine, same-process, and the promise is broader | Add CI with at least MSVC/Windows, GCC/Linux and Clang jobs that each generate the same seed through the shipped CLI with default flags at a size that resolves octaves, hash the document, and compare the hashes across jobs and across two processes within a jo… |
| **high** | `test` | Nothing can detect a serializer that drops a top-level block | Add a test asserting a hard-coded list of required top-level keys, with the per-phase exceptions spelled out, so adding or removing a block is a deliberate edit to that list. When the document outgrows a hand list - which stages 6-16 will do - back it with a… |
| medium | `1` | A second, unused, untested seed derivation already sits beside the one in use | Either delete Rng::child until a stage needs it, or give it a call site, a test and a line in determinism.md saying it is a second derivation with a different width and namespace. Whichever way, state in determinism.md that a new seed derivation is a decision… |
| medium | `2` | Nobody judged whether generated worlds are good — only whether the documents are true | Settle the peak-height recipe question now, while stages 1-5 are the whole world and a re-tune is cheap — every later stage (climate, biomes, settlement siting) reads `height` and will be tuned against whatever it currently produces. Whichever way it is ruled… |
| medium | `api` | Read-surface join table points at unrelated code; the C++ document states no joins or layer meanings | Give the document a layer catalogue - per layer: units, what it means, what it is not - and state the one join it already has (`layers.plates` cell value indexes `plates[].id`). When stage 16 adds uid joins across blocks, name each writer by a greppable symbo… |
| medium | `api` | schema_version and generator_version are declared in the document and consulted by nothing | Either bind schema_version to the document's declared shape with a test that fails when the shape changes - a checked-in field-name list, or a digest over the key set of a fixed small world - so changing the document forces a deliberate bump, or delete the fi… |
| medium | `api` | Dead functions: delete or annotate, because a name with no caller misleads the next reader | Delete scale::physical_radius (nothing will use it; config.cpp uses design_radius). Collapse the duplicate Nyquist rule: have scale::resolved_octaves and noise::resolved_frequencies share one implementation, or have tectonics.cpp call scale::resolved_octaves,… |
| medium | `api` | Unreal/ was never reviewed, and it is the only written record of the engine-to-game API contract | Before the Python repo goes, copy the Unreal module's API shape into FWGC++ as a doc (or a stub header): the frame convention, the validate-then-generate split, the failed-request-leaves-the-previous-world-intact rule, and the data file names. Review it while… |
| medium | `api` | The sample world was never validated against any schema | Publish a schema for `fwg.world` v1 (a doc listing every top-level key and its contract is enough — it does not need a validator library) and make one test check a generated document against it key by key, so a dropped or renamed block fails rather than passi… |
| medium | `build` | The determinism flags do not reach a consumer who links a prebuilt library | Either add install()/export() rules that carry fwg_flags into the exported package and state the CRT requirement there, or change README.md and determinism.md to say plainly that the flags propagate only through add_subdirectory and that any other integration… |
| medium | `docs` | No enumeration of the world document's top-level keys, and the only key-walking test walks the struct no… | Add an enumeration of top-level document keys (a table in docs/, or a table in code that serialize.cpp is driven from) with a stated reason for each conditional key, and a drift test that walks the writer rather than the World struct, so a new block emitted w… |
| medium | `docs` | Documents advertise a schema name and version with no schema behind them and nothing validating | Either define fwg.world v1 as a real artifact and validate a generated document against it in the suite, or drop the schema/schema_version fields until one exists — a version integer on a schema nobody can read is a promise with no referent. When the inherite… |
| medium | `docs` | The human-facing index states state integers that nothing binds to the code | Make one of them the source of truth: drive the CLI's ladder from a table in code, and add a test that the stage number in porting-status.md and README matches kHighestImplementedStage (a grep-based assertion is legitimate here), or cut the copies down to one… |
| medium | `docs` | The legal input space and its refusal bounds are stated in three places, checked in none, and two ceilin… | One stated control registry - name, unit, default, bound, what refuses and why - either generated from validate() or asserted against it, so the CLI help text and the registry cannot drift apart. Add refusal tests that actually cross the two ceilings (a ~1200… |
| medium | `docs` | The handoff account states verification in the present tense that nothing runnable backs | Link docs/verification.md from README.md's Documents list and from porting-status.md, or it is lost the moment the author stops remembering it. Either back determinism.md:9's cross-process claim with a test (run the CLI twice, hash both files) or rewrite it i… |
| medium | `docs` | Nothing was fixed — the review is the only artifact, and it is not yet committed in the port | Commit docs/archive/2026-09-21-pre-port-red-team.md together with this triage, so the port carries both the findings and the ruling on each. The triage is the load-bearing half: the archived review is about a repo that will not exist, and without a per-findin… |
| medium | `test` | Tests that pass on empty output — suite floor exists, three individual tests are still vacuous | Give each of those three a derived floor: assert world.layers.size() equals the count the requested phase owes before iterating; assert every node's neighbour degree is at least 3 (pole nodes) before checking symmetry. Derived, not pinned — a pinned layer lis… |
| medium | `test` | An expected-failure ledger so a green closeout gate means something (§4) | 1) Make an empty run fail at the gate, not just inside the binary: add --no-tests=error to the documented command in README.md:23 (CMake 3.20 is already the minimum), or wrap the gate in a script that also checks the build was configured with FWG_BUILD_TESTS=… |
| medium | `test` | No world generated at large size, no timing re-measured; doc cost figures unreproduced | Either add a repeatable cost target (a bench mode of the CLI or a timed test at 257/513) or date every cost figure in verification.md/README to the commit and machine that produced it. At minimum add one generation test above 129 — 257 or 513 — since the conf… |
| low | `build` | Unused imports that imply a coupling: same defect present in the C++ includes, with a missing header bes… | Drop <cstring> from apps/main.cpp and add <cstdlib>; drop <vector> from src/sha256.cpp and <cstdlib> from tests/test_world.cpp. Standing demand as stages land: an include that names a determinism-bearing header (fwg/random.hpp, fwg/sha256.hpp) in a translatio… |
| low | `docs` | Measured cost tables shipped twice, pinned to nothing, with a derived multiplier against a reference abo… | Keep one copy of the ladder and have the other point at it. Stamp it with the commit, the compiler and the build configuration it was measured under, and say on the README copy - not only in verification.md - that it is a single measurement and not a test. Ma… |
| low | `docs` | One measurement stated in several places drifts; state it once and cite it | Make README.md:41-52 the single figure and have verification.md section 4 cite it rather than restate it; add the date and the machine, since 'on this machine' is not a re-measurable claim once the author's box changes. Re-measure when stage 6 lands - the tab… |
| low | `docs` | A measurement restated in a second file instead of cited - the C++ already has one | Pick one home for the kernel cost figure - the README table reads as the published one - and have docs/verification.md section 4 cite it rather than repeat it. While in the README, add docs/verification.md to the Documents list at README.md:76-83; it is the o… |
| low | `none` | Byte-identical duplicate copies of _angle/_unit/_offset in two modules | 1. Hoist the latitude-band cell-area formula into one function (e.g. `double band_area(int z, int n, double radius)` in include/fwg/grid.hpp) and call it from BOTH src/grid.cpp:117-120 (build_sphere_grid) and src/world.cpp:38-41 (land_area). Minimum acceptabl… |

## Later — binds when a stage lands (22)

Read the rows for a stage **before** writing it, not after.

| Stage | | Finding | What it asks for |
|---|---|---|---|
| `5` | medium | Item 4: apply_world_scale is a function named for scaling that generates half the world | Binds at stage 6 and, most sharply, at stage 8 (climate/biomes/ecology) and whenever terrain_water lands — the producers the reference hangs off apply_world_scale. When those stages land the C++ owes: 1. Each producer gets its own named entry point… |
| `8` | medium | Sky islands: the largest already-written feature not shipping, and it is not captured anywhere | Extract make_sky and its controls (sky_clusters, sky_occurrence, sky_altitude, sky_radius) plus the civilization-eligibility rules into the archive before the Python repo is deleted, and name sky islands explicitly in porting-status.md as a suspende… |
| `10` | **high** | Settlement count tracked the sampling raster, not the world - the surviving half of PERF-CITY-COUNT | When stages 10-12/16 place settlements, drive placement from absolute geography (land area in km2, physical reaches via scale::reach_scale) and never from per-cell probability. Pin it with a test in the style already used here: generate the same see… |
| `13` | medium | CONTENT-NO-ANTAGONIST step 2: wired systems that emit nothing and do not say so | When stage 13 (nests, religion) and stage 16 (heroes) land, every wired content system must publish a per-role/per-institution diagnostics row with a reason even when it places nothing, and the two kinds of zero must read differently. Then rule whet… |
| `14` | **high** | The age-boundary state question, and the modules no lens read | Before the Python repository is deleted: extract terrain_history.STATE_KEYS and STAGES verbatim into this repo as data or an archived source file, because they are the answer to 'is all state that survives an age boundary in the document' and portin… |
| `14` | low | AGE_BASIS_SECONDS: a hand-measured cost constant shipped as API advice, 5-30x stale | When stages 14-15 land and anything reports a predicted cost: derive it from the work actually queued (cells, ticks, passes) or measure at call time; do not embed a seconds constant. If a constant is unavoidable, put it in one named place beside the… |
| `16` | **high** | Settlement density tracks raster cells, not land area — grid size is a content control | Derive settlement, hamlet, castle and nest counts from area.land_km2 (and habitable area), never from cell counts or cell iteration, and assert per-km2 density invariance across sizes at a fixed seed — the same shape as the existing land_fraction_an… |
| `16` | **high** | The shipped entry point cannot use the process pool, and the determinism gate runs through it | At stage 16, before any parallelism is written: rule explicitly on whether parallel and serial output must be byte-identical, write the equality test first, and make any worker count a documented, defaulted option rather than an environment variable… |
| `16` | medium | Planner tests with an 'or all unbuildable' escape hatch | When stage 16's planners land: the acceptance test must carry a floor on the number of successfully placed settlements that is derived from the world (site count, land area, civilization count), never an alternative branch that accepts a total failu… |
| `16` | medium | Quest lifecycle spec collapses to green skips when no hooks are raised | When stage 16's quest lifecycle lands: one unconditional precondition test asserting the shared fixture world raises hooks at all, separate from the per-hook offer/expire/resolve/reap assertions. Any loop over a hook collection needs a non-empty ass… |
| `16` | medium | add_world_society: rewritten, three unverified equivalence arguments, near-zero coverage, no record | Before the Python tree is deleted, extract Sim/icarus_sim/terrain_society.py (and the three equivalence arguments in its comments at the bounded Dijkstra, bounded allocate_access and memoised sea search) into the C++ repo's archive — it is the speci… |
| `16` | medium | Process-pooled planners: no ruling on whether parallel output must equal serial, control surface un… | Rule the question before any pool exists, and write the workers=1-vs-parallel equality test before the parallel path: the Python repo got this order wrong and ended with two reviewers who had read both paths and executed neither. Two specific traps… |
| `16` | medium | Clearance prefilter and lazy candidate streams claimed output-preserving by comment only | When city/hamlet/castle planners land at stage 16: implement the exhaustive candidate sweep first and make it the reference. Any clearance prefilter or lazy candidate stream must then be proven against that exhaustive form by capturing full plot out… |
| `16` | medium | SUPER-VILLAINS: card close is bookkeeping, but S5b's unbuilt half is a real data-model decision | Before stage 16's settlement slice is designed, decide whether a settlement's population is single-faction or a mix, and write it into the C++ repo as a recorded decision. Model it as a mix (a weighted set of factions/cultures with a founder attribu… |
| `16` | medium | The antagonist cast is structurally unreachable, not unlucky - no ruin ever carries a beast cause | When stages 13-16 land: make the beast-destroys-settlement path actually fire, or drop the antagonist role rather than shipping a cast that can never be populated. Assert it - at the seed and size the repo uses for acceptance, the count of ruins wit… |
| `16` | medium | Rule whether pooled execution must be bit-identical to serial (§1.3) | Before any parallelism lands in stage 16 (or anywhere else), rule in docs/determinism.md whether parallel output must be byte-identical to serial, and land the workers=1-vs-pooled equality test *before* the pool, not after. The reference shipped the… |
| `16` | medium | Two planner rewrites and add_world_society are optimizations proven by comment only (§1.2, §1.9) | When stage 16's planners and terrain_society are written, treat the bounded/targeted/memoised forms as an optimization to be proved, not as the specification: implement or keep the exhaustive form and assert the fast one against it (the reference's… |
| `16` | medium | Pooled-vs-serial planner equivalence was reasoned, never executed | When stage 16 lands and any planner is pooled, the workers=1 vs workers=N equivalence must be executed as a test, not argued. Given the named-child-stream design (determinism.md:84-87), the cheap way is to make every planner take its own child strea… |
| `api` | medium | A third drift test walking the mutation verbs, so the block inventory cannot silently miss one (§1.… | When an operation/mutation surface lands, the document's top-level key inventory must be enumerated from the writers (or from a registry the writers are checked against), and the check must walk the verbs as well as a generated world. Generation-tim… |
| `api` | medium | Published schemas no test ever compares to a document, plus oneOf/default in the validator (§1.7) | Either define the fwg.world document schema and validate a generated document against it inside the suite, or drop the schema/schema_version claim until one exists. When schemas do arrive, 'the file exists' must not count as exercise, and the valida… |
| `api` | medium | The published verb table omitted the two writes a playing session makes most often | When a host/operation surface is built, derive the verb table from the routing table rather than maintaining a second list, classify reads as well as writes, and make 'every route is a classified operation' a test. Do this before a host is built aga… |
| `api` | low | Where a non-registry execution env var is documented (ICARUS_CITY_WORKERS) | Keep the process environment out of the generator, or — if an execution knob is ever added — publish it in the Config surface that is serialized into the document, so it cannot be invisible to a consumer holding a world. Say which, in writing, at th… |

## Gaps the triage itself missed (10)

From a final pass whose only job was to find what the eight section agents summarised
past. Items already acted on are in the fix table above.

| | Stage | Gap | What it asks for |
|---|---|---|---|
| **high** | `before the Python repo is deleted…` | Core/ — 19,203 lines of C++ prototype covering stages 8–16 — is not in the archive, and the triage filed it as "nothing… | Before C:\Users\davgo\Documents\GitHub\FantasyWorldGenerator is deleted, copy C:\Users\davgo\Documents\GitHub\FantasyWorldGenerator\Core into C:\Users\davgo\Documents\GitHub\FWGC++\docs\archive\core\ (or a vendored reference/ tree), verbatim, with t… |
| **high** | `before the Python repo is deleted…` | docs/decisions/ — twenty accepted rulings — dies with the repo; only 027 was archived | Copy C:\Users\davgo\Documents\GitHub\FantasyWorldGenerator\docs\decisions\ into C:\Users\davgo\Documents\GitHub\FWGC++\docs\archive\decisions\ before the repo goes, and add a one-line table to docs/archive/README.md marking which still bind (020, 02… |
| **high** | `now — stages 1-5 / CLI and contro…` | The CLI silently narrows out-of-range integers into legal ones, so validate() never sees what the user typed | Range-check in to_int's callers before the cast (reject anything outside the destination type, and outside the field's documented bound, naming the field and the value), and add CLI-level refusal tests to the ctest step that the triage's 4-validate-… |
| **high** | `now — stages 1-5 substrate` | Rng::below branches on population size and consumes zero stream words at bound == 1 — the C++ instance of the defect de… | Either state the size branch as a deliberate, pinned property of the stream in determinism.md §2 beside the sample_indices paragraph, or remove it and let bound == 1 draw and discard a word like every other bound. Either way pin the cost, not the va… |
| **high** | `now — stage 3 (surface detail); s…` | A third seed-derivation idiom is already live and undocumented: the noise octave seeds are derived by integer addition | Route the octave seed through the one surviving derivation — `child_seed(cfg.seed, "surface", cfg.detail_variation + k)` or a child of the detail stream keyed on the octave index — and add it to the golden vectors critic-1 asks for. Then narrow or c… |
| medium | `now — stages 1-5 infrastructure;…` | sphere_grid returns a reference into a two-entry evicting cache, and a test pins that reference's identity | Return by value-like handle rather than by reference (std::shared_ptr<const SphereGrid>), or make the cache append-only for the process lifetime, or document at the declaration (include/fwg/grid.hpp:83) that the returned reference is valid only unti… |
| medium | `now — stage 5 / document contract` | The document reports a world_size preset that did not build the world, and omits the two request fields that did | Either make world_size an enum with an "authored" member that resolve() sets when a circumference or globe_radius wins, or emit the resolved circumference_m and relief_m in the document beside globe_radius so the derivation is recoverable. Add the t… |
| medium | `now — stages 1-5` | Config::step() is a third copy of the cell-step formula and has no caller — and it is in neither of the triage's two en… | Delete Config::step() or make it the single definition that scale::resolved_octaves and tectonics.cpp:316 both call, and add it to 1.8's hoist list so the single-implementation pin 1.8 item 5 asks for covers three formulas rather than two. |
| medium | `now — docs/determinism.md and sta…` | No rule covers std:: algorithms and containers whose result is unspecified across implementations — only iteration order | Extend determinism.md §3 from "unordered iteration" to "any standard-library operation whose result is unspecified for equivalent elements or across implementations", naming std::sort's instability explicitly and stating the rule: every comparator t… |
| medium | `now — docs infrastructure, before…` | docs/archive/README.md states a completeness claim the triage itself refutes | Rewrite the sentence as a measured statement — these are the three documents copied so far — and give the file a checklist of what else is owed before deletion, with each row either done or open, so the archive's own coverage is a fact rather than a… |

## Retired (18)

**4 retired by the clean-rewrite ruling** — findings about reproducing CPython semantics
this library deliberately does not reproduce: `1.5`, `5-pin-interpreter`, `6-native-suite-run-once`, `6-core-not-built-or-reviewed`.

**14 retired with the repository** — the Python tree's own board cards, stale internal
documents, dead Python code and CI: `1.11`, `2.5`, `2.7`, `3-setup-py`, `4-close-castle-planner`, `4-close-pantheon`, `4-close-astrology`, `4-dead-sdet-ladder`, `4-readme-cost-basis`, `5-native-parity-suite`, `5-cite-blocking`, `6-board-perf-figures-unchecked`, `6-no-git-archaeology`, `critic-2`.

Retired means *owes this codebase nothing*. It does not mean the finding was wrong.
