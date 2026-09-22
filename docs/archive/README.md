# Archive — inherited from FantasyWorldGenerator

Verbatim copies of documents from the Python `FantasyWorldGenerator` repository, taken at
its commit **`b67c99b`** (2026-09-22). That repository is being retired.

**These three are not everything worth keeping, and this folder should not be read as a
complete rescue.** An earlier version of this sentence said they were "the ones whose value
survives it", which a triage of the red team immediately refuted. Still in that repository
and not here, in rough order of what it would cost to lose:

| Still only there | Why it matters |
|---|---|
| The source of 12 of the 19 modules behind stages 6-16 | They have no conformance record. The code **is** the specification — see [../porting-status.md](../porting-status.md). |
| `Contracts/` — 2.4 MB, 38 files | Not documentation. Schemas, the block registry and the catalogues are **data stages 8-16 need at runtime**. |
| `Sim/icarus_sim/*.json` — ~1.5 MB | Same: `civilizations.json`, `buildings.json`, `city_shapes.json`, `biomes.json`. |
| `docs/decisions/` — twenty accepted rulings | Only 027 is archived here. The rest govern stages 8, 10-16 and the age model. |
| `docs/conformance/` — ~3,350 lines, 8 relevant records | The written acceptance spec for the seven stage-6-16 modules that have one. |
| `Core/` — 19,203 lines of C++ | Ruling 027 calls it a prototype and not a parity target. It is still the only existing implementation of stages 8-16 in this language. |

Rescuing those is a separate decision, not an oversight this folder has already handled.

**Nothing in this folder is edited, and nothing in it is current.** Every file describes
the Python implementation and the tree it lived in. Paths, line numbers, versions and costs
in them refer to that tree, not to this one. They are kept because they record *reasoning*
and *measurement* that cannot be recovered once the source repository is gone — not because
they are still true.

For what any of it means for **this** codebase, read
[../inherited-findings.md](../inherited-findings.md), which triages the red team against
what is actually built here. Where the two disagree, the triage is current and the archive
is history.

| File | What it is |
|---|---|
| [2026-09-21-pre-port-red-team.md](2026-09-21-pre-port-red-team.md) | An eight-lens adversarial sweep of the whole Python tree run the day before the port was scoped — 180 agents, 85 findings raised, 49 surviving two independent refuters. Its section 1 is titled "Blocking for the port" and its closing "Completeness critic" is the most port-relevant part of the document. |
| [2026-09-21-python-porting-notes.md](2026-09-21-python-porting-notes.md) | The CPython substrate a bit-exact reimplementation would have had to reproduce: compensated `sum()`, the `random` module's algorithms, `round()` half-to-even, `repr(float)` inside shipped digests, container ordering, and the float build flags nobody had chosen. Mostly retired by the clean-rewrite ruling; kept because it is the record of *why* each one mattered. |
| [027-native-port-deferred-to-a-full-redo.md](027-native-port-deferred-to-a-full-redo.md) | The 2026-09-21 ruling that the C++ prototype in that repo's `Core/` was not a parity target and would be rewritten. This codebase is that rewrite. Superseded on 2026-09-22 only in its timing — the port is now scoped, and as a clean rewrite rather than the parity port 027 assumed. |

## A caution the red team states about itself

It used two independent verifiers per finding and a single refute killed a finding, so
**"refuted" in that document means "not unanimous", not "disproved"**. Several refuted items
were real port hazards that one verifier judged already-documented or out-of-scope. Its own
section 6 lists what it did not check — including that the pooled-vs-serial planner question
was *reasoned, not run*, and that no world was generated above size 33 during the sweep.
Treat unrun claims as unrun.
