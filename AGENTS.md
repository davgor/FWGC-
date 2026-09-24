# FWGC++: instructions for every agent

These are the working rules for everyone who changes this repository: Claude, GPT and the
owner's other tools. Each component folder has its own `AGENTS.md` with the local rules; read it
before you work there.

## What this is

A monorepo behind two games: a 2D game in Unreal first, then a 3D one. Neither is a text game.
Text is only freeform conversation with NPCs, which is how players get quests and information.

| Folder | What | Kind | Lane |
|---|---|---|---|
| `generator/` | The world generator: a seed in, a finished world out. C++20 library `fwg` and CLI `fwg.exe` | runtime | Claude (its visual data fields: GPT) |
| `viewer/` | A browser viewer for generated worlds, over `fwg.exe` | dev tool (JS) | GPT |
| `concept art/` | The concept art production map and its assets | assets | GPT |
| `tools/` | The layer checker, the commit gate, the red-team prompt, the git hooks | dev tool (C++) | Claude |
| `orchestrator/` | The deterministic director. Built so far: dialogue acts answered as knowledge-scoped fact packets, taking and releasing quests, and time. Pacing and hero direction are still to come | runtime | Claude |
| `communicator/` | *Planned.* NPC dialogue: the interpreter (text to a dialogue act) and the NPC voice | runtime | Claude |
| `creator/` | *Planned.* New-world setup: a player's description to a seed and generator settings | runtime | Claude |
| `inference/` | *Planned.* The local model runtime (llama.cpp and a small classifier), knowing nothing of worlds | runtime | Claude |

The planned folders do not exist yet. Their rules are already in
`tools/check_layers/layers.txt`, but creating one is a slice the owner starts, not something to
scaffold in passing. The same holds for each new slice of `orchestrator/`: plan it before building
it.

**The architecture in one line: the LLM translates and phrases; C++ decides.** A model turns
player text into a closed set of actions and phrases the results. Deterministic C++ decides what
happens and changes the world.

## Lanes

**Claude writes the code:**
- `generator/`, its C++ and its Python content tools;
- `tools/`;
- the runtime components to come.

**GPT does the art and visual side:**
- `viewer/` and `concept art/`;
- asset output;
- the visual fields of the generator's content data: appearance prose, colours and asset
  references in files such as `generator/data/heritage-appearance.json`, `npc-appearance.json`,
  `key-location-exteriors.json` and `world-asset-requirements.json`.

Crossing a lane is allowed. The commit gate notes it so the owner sees it. Declare who you are on
every commit with `FWGC_AGENT=claude` or `FWGC_AGENT=gpt`.

## How the components stay apart

The dependency graph runs one way:

```
orchestrator -> generator
communicator -> orchestrator, inference
creator      -> generator, inference
generator, inference -> nothing
viewer       -> fwg.exe and its documents (a process boundary, not a dependency)
```

| Guardrail | Rule | Enforced by |
|---|---|---|
| G1 | A dependant includes only `fwg/api/`, writes `fwg::` in full, and calls only what `generator/api.txt` lists. | `check_layers` (`api-include`, `api-function`, `api-namespace`, `api-adl`, `api-list`), red team |
| G2 | Nothing in the generator names, includes, links, or is shaped for a consumer. | `check_layers` (`edge`), red team |
| G3 | Only the generator's logged verbs change a World. Each takes `const World&`, returns a new world and appends to `history.operations`. Consumers never write into a World. | red team; later, an op-log replay test in each consumer |
| G4 | The generator has no callbacks into consumers, and there is no shared `common/`-style folder. What two components share belongs to the lower one. | `check_layers` (`top-level`), red team |
| G5 | Each layer keeps its own vocabulary: no pacing or dialogue in the generator, no model plumbing in the orchestrator, no world in inference. | `check_layers` (`vocab`), red team |
| G6 | Contracts carry `api_version`. A cross-layer change is two commits: the generator first, then the consumer. | `check_layers` (`api.txt` is read from the last commit), red team |
| G7 | The shipped runtime is fully local: no network in a runtime component. | `check_layers` (`http`), red team |
| Language | Runtime components are C++. The declared exceptions are `generator/tools/` (Python content tooling) and `viewer/` (JS). Concept art holds no code. | `check_layers` (`lang`, `no-code`) |

[`tools/check_layers/README.md`](tools/check_layers/README.md) explains each rule and how to add
a public generator function.

## Working in the shared tree

Several agents work in this one working tree, and share one index, at the same time.

- **Never** run `git checkout -- <file>`, `git restore`, `git reset --hard`, `git stash` or
  `git clean`. Another agent's uncommitted work dies with it. Undo your own change by editing
  it back.
- Stage files **by name**. Never `git add -A`, `git add .` or `git commit -a`: the index may
  hold someone else's staged work.
- Leave files you did not touch alone, even if they look broken. A red test or an unfamiliar
  file is often someone's work in flight. Check its modification time before assuming anything.
- Push only when the owner asks.

## Committing

Enable the hooks once per clone:

```bash
git config core.hooksPath tools/hooks
```

Every commit then passes two gates:

- **`pre-commit`** runs `check_layers` on the staged tree. It takes about a second.
- **`commit-msg`** runs the checks again, then a context-cleared red-team review of the staged diff
  and message. That review is a fresh `claude -p` process with none of your session. The gate then
  appends `Separation-Review: PASS <tree>`. A review usually takes 5–30 seconds, and a large
  diff longer.

Commit with your agent declared:

```bash
FWGC_AGENT=claude git commit -m "..."        # PowerShell: $env:FWGC_AGENT = 'gpt'; git commit -m "..."
```

If your shell kills commands sooner than a review can finish, review ahead and then commit with
the identical message. The commit reuses the verdict:

```bash
tools/check_layers/build/Release/commit_gate.exe review -m "the message"
```

When the gate stops you:

- **A layer finding:** fix the cause. The message names the rule.
- **A red-team FAIL:** read its findings. Fix the change. If you believe the reviewer is wrong,
  tell the owner. Do not reword the commit to talk the reviewer round.
- **The review could not run** (no CLI, a timeout, an error): the commit stops. Fix the cause or
  ask the owner. It is never skipped.

Rules for particular commits:

- **Never `git commit --no-verify`.** That is the owner's alone, and `commit_gate audit` lists
  every commit without a matching review.
- **Never touch the gate's records.** Never write, edit or delete anything in
  `.git/fwgc-review/`, and never type a `Separation-Review` trailer yourself. `audit` checks each
  trailer against those records.
- **Rule changes go in a commit of their own.** Rule files are `tools/check_layers/`,
  `generator/api.txt`, `tools/red-team/`, `tools/hooks/` and these `AGENTS.md`/`CLAUDE.md`
  files. A rule change takes effect from the next commit. Weakening a guardrail is the owner's
  decision.
- **A new top-level folder** needs its `component` line in `layers.txt` committed first.

## Runtime rules

- **Deterministic generator.** The same request, library version, compiler and C runtime give a
  byte-identical world document: `generator/docs/determinism.md`. Nothing that is not a function
  of the seed goes into a world document.
- **Local only.** No network, telemetry or online dependency in anything that ships. Dev tooling
  may use the network.
- **No rewards.** Quests are an anchor, a verb and a difficulty from 1 to 5. The game prices
  rewards, and the generator never emits loot, XP or rewards.
