# tools/check_layers: the layer checker and the commit gate

Several agents build this repository over long sessions: Claude writes the code, GPT the art
and visual side. The owner's concern is that the components drift into depending on each other
one reasonable-looking edit at a time. Rules written only in documents erode that way, so the
rules here are enforced on every commit by two checks that fail the commit:

1. **`check_layers`**: a mechanical check of the staged tree against
   [`layers.txt`](layers.txt). It is deterministic, takes well under a second, and runs in the
   `pre-commit` hook.
2. **A context-cleared red team**: a fresh `claude -p` process that reviews the staged diff
   against [`../red-team/separation.md`](../red-team/separation.md), looking for what a
   mechanical check cannot see. It runs in the `commit-msg` hook, so it sees the message too.

Both are C++, in this folder, with no dependency beyond the standard library. The gate's process
layer (`src/process.cpp`) is Windows-only for now.

## Build and run

```bash
cmake -S tools/check_layers -B tools/check_layers/build -G "Visual Studio 17 2022" -A x64
cmake --build tools/check_layers/build --config Release
ctest --test-dir tools/check_layers/build -C Release --no-tests=error
tools/check_layers/build/Release/check_layers.exe --root .     # the working tree, any time
```

`cmake` ships with the VS 2022 Build Tools on this machine and is not on `PATH` (see
`generator/README.md`). The hooks find it themselves, and build the gate when it is missing or
older than its sources.

Enable the hooks once per clone:

```bash
git config core.hooksPath tools/hooks
```

## The rules

| Rule id | What fails | Why |
|---|---|---|
| `edge` | A component includes, links (CMake target or `../folder` path) or names (`ns::`) a component it does not `depend` on, or includes another component by relative path. | The dependency graph is one-way. The generator depends on nothing. |
| `api-include` | A dependant includes a generator header outside `fwg/api/`. | The generator's public face is `fwg/api/`. |
| `api-function` | A dependant writes `fwg::name`, where `name` is snake_case (or any function the generator declares, however it is spelled), and `name` is not in [`generator/api.txt`](../../generator/api.txt). Types (`UpperCamel`) and constants (`kName`) are free to read. | The generator's ~700 other functions are its own to change. |
| `api-namespace` | A dependant writes `using namespace fwg`, reopens `namespace fwg`, or aliases it. | Each of these hides which generator functions are called. |
| `api-adl` | A dependant makes an unqualified call to a function the generator declares and does not publish. Argument-dependent lookup would reach it through `fwg::` arguments. | Otherwise `plan_city(world, ...)` compiles without `fwg::`. |
| `api-list` | An `api.txt` entry names a function its `fwg/api/` header does not forward, or a forwarder that does not exist. | The list cannot drift from the code. |
| `vocab` | An identifier in a component contains a word `layers.txt` keeps out of it. Identifiers are split at `_` and case changes; comments and strings are not read. | Orchestrator concepts stay out of the generator; model concepts stay out of the orchestrator; world concepts stay out of inference. |
| `lang` | Non-C++ source in a runtime component. The declared exceptions are `generator/tools/` (Python content tooling) and `viewer/` (a JS dev tool). | Ruled 2026-09-24: the runtime is C++. |
| `no-code` | Source code under `concept art/`. | Concept art holds documents and assets. |
| `http` | A runtime component includes or links a network library. | The shipped runtime is fully local. |
| `path-text` | The viewer names `generator/src`, `include`, `data` or `tools` in code. | The viewer reads the generator through `fwg.exe` and its documents only. |
| `top-level` | An undeclared top-level folder, or one named `common`, `shared`, `utils` or the like. | What two components share belongs to the lower one, not to a grab-bag beside both. |
| `config` | `layers.txt` or an `api.txt` does not parse. | A broken rule file must not reach the next commit. |

### Calling the generator from another component

- Include only `fwg/api/*.hpp`.
- Write `fwg::` in full.
- Call only what `generator/api.txt` lists.

A function that should be public lands in its own generator commit:

1. a line in `api.txt` saying why;
2. its forwarder in `generator/include/fwg/api/` if the header is new;
3. a test.

The gate reads `api.txt` **from the last commit** when it checks a dependant, so the first caller
comes in the commit after that. That is also how guardrail 6 lands: a cross-layer change goes in
as a generator commit, then the consumer commit.

An unqualified name the generator also declares, such as the orchestrator's own `census()`, is
exempt when the calling component declares that name itself. Otherwise clear it with an `adl-ok`
line in `layers.txt`.

### Changing the rules

The gate checks every commit twice: under the last commit's `layers.txt` and `api.txt`, and under
the staged ones. So:

- A rule change takes effect from the next commit.
- A staged rule that the tree already breaks is caught before it lands.
- A new component's folder comes in the commit after its `component` line.

The red team treats any change to this folder, `generator/api.txt`, `tools/red-team/`,
`tools/hooks/` or an `AGENTS.md`/`CLAUDE.md` as a rules change. It blocks one that weakens a
guardrail: that is the owner's call, made outside the gate.

## The commit gate

`tools/hooks/` holds three `sh` shims that find, build and run `commit_gate.exe`. All the
decisions are in [`src/main_gate.cpp`](src/main_gate.cpp).

A working-tree run skips the `ignore-dir` names (build output). The gate does not: a staged
snapshot holds only what is being committed, so a source file force-added under a folder named
`build/` or `node_modules/` is still checked.

| When | What runs |
|---|---|
| `pre-commit` | `check_layers` on a copy of the staged tree (`git checkout-index`), under both rule sets. |
| `commit-msg` | The same checks, then the review, then the `Separation-Review: PASS <tree>` trailer. |
| `commit_gate review -m MSG` | The review ahead of time. `commit-msg` reuses a verdict for the same tree, message and red-team prompt. |
| `commit_gate check` | The layer checks on the staged tree, printed. |
| `commit_gate audit [RANGE]` | Commits since the gate landed whose trailer is missing (a `--no-verify` bypass) or names a different tree. |

### The review

The review is context-cleared by construction:

- **A fresh process.** It never sees the committing session's conversation.
- **No inherited session.** Every `CLAUDE*` and `ANTHROPIC*` environment variable is dropped, so
  the CLI runs on its own login (the owner's subscription), and it runs the same whether Claude
  or GPT commits.
- **Restricted tools.** It runs with `--restricted --strict-mcp-config
  --no-session-persistence`. Its tools are Read, Grep and Glob plus `git diff`/`show`/`log`/
  `grep`/`ls-files`, under `--permission-mode dontAsk`.
- **Staged inputs.** It reads the staged snapshot with `git show :path`.
- **Structured answer.** It answers through the schema in `../red-team/verdict.schema.json`.

The gate fails the commit on a FAIL verdict or on any `block` finding.

**A review that cannot run is a failed review.** That includes a missing CLI, a timeout, an error,
or output that does not parse.

Settings, all optional:

| Variable | Default | |
|---|---|---|
| `FWGC_AGENT` | unset | `claude` or `gpt`. The reviewer notes lane crossings and an unset agent. |
| `FWGC_REVIEW_MODEL` | the CLI's default | passed as `--model` |
| `FWGC_REVIEW_BUDGET` | `5` | `--max-budget-usd` |
| `FWGC_REVIEW_TIMEOUT_MIN` | `20` | the child and everything it started are ended at the limit |
| `FWGC_CLAUDE` | search | the CLI to run. Otherwise `PATH`, `%USERPROFILE%\.local\bin`, then the desktop app's newest copy |

Measured 2026-09-24 in a scratch clone, on Opus 5.5 at list price (the subscription pays in
usage, not dollars):

| Change | Review | Notional cost |
|---|---|---|
| A one-line docs commit | 6 s | $0.02 |
| A planted `api.txt` widening | 17 s | $0.11 |
| A planted direct `World` mutation | 14 s | $0.03 |

A large diff costs more. Past 400 KB the packet is truncated and the reviewer reads the rest
per file.

### Never bypass it

`git commit --no-verify` skips both hooks. Agents never use it. The owner may.

`audit` lists every commit that:
- lacks a trailer;
- carries a trailer naming a different tree;
- carries a trailer with no passing review of its tree recorded in `.git/fwgc-review/` on this
  machine (a trailer typed by hand).

That record directory is the gate's alone. Agents never write, edit or delete anything in it.

## What this does not catch

These checks are a strong floor, not a proof. The checker lexes C++ without compiling it, so:

- a macro that expands to a generator internal is invisible to it;
- a call written after `*`, `&` or `>` reads as a declaration;
- writes into a `World`'s members are not seen at all.

The red team exists for exactly those. It is a model, so its verdict is a second opinion that
cites evidence, and it can be wrong in either direction. The owner reads its notes.

Nor can the gate stop **deliberate forgery**. Any process that can write to `.git` can fabricate
a review record and a trailer to match, because every agent runs as the same user on this
machine. The gate stops drift and casual bypass, and makes a bypass visible to `audit`. Stopping a
determined forger would take a reviewer the committing agent cannot write to, such as CI.

## Tests

`layers_tests` runs the lexer's and the JSON reader's corner cases, then every fixture in
[`tests/fixtures/`](tests/fixtures/):

- a `bad_*` fixture plants violations next to clean control lines, and must produce exactly the
  findings in its `expect.txt`;
- a `clean_*` fixture must produce none.

On 2026-09-24 each rule was switched off in turn in a scratch build, and the fixture that guards
it failed every time: vocabulary, ADL, API function, relative include, HTTP case, CMake target,
namespace edge, string blanking, API list and top level. The gate was then driven end to end
in a scratch clone. It stopped:

- an internal include;
- a planted deny word;
- a red-team-only `api.txt` widening;
- a direct `World` mutation;
- a commit whose reviewer could not run.

It passed a docs-only change. `audit` flagged a `--no-verify` commit and a trailer copied onto
another tree.
