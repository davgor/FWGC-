# tools/: local rules

The rules' machinery. **Lane: Claude.** The repository-wide rules are in
[`../AGENTS.md`](../AGENTS.md).

| Folder | What |
|---|---|
| `check_layers/` | The layer checker (`check_layers.exe`), the commit gate (`commit_gate.exe`), the rules (`layers.txt`) and their tests. See [`check_layers/README.md`](check_layers/README.md). |
| `red-team/` | The context-cleared reviewer's prompt and the schema of its verdict. |
| `hooks/` | The `pre-commit` and `commit-msg` shims (`git config core.hooksPath tools/hooks`). |

## Rules

- **Every change here is a rule change.** Commit it on its own. It binds the commits after it,
  because the gate reads the last commit's rules and red-team prompt. The red team blocks a change
  that weakens a guardrail; that is the owner's decision.
- **A new rule comes with its fixture.** Add a `bad_*` fixture under
  `check_layers/tests/fixtures/` that plants a violation next to a clean control line. Then prove
  the fixture guards the rule: switch the rule off in a scratch build and watch the fixture fail.
- **The fixtures are deliberate violations.** `layers.txt` skips them (`skip-path`). Never skip
  anything else to make a check pass.
- **The gate never skips silently.** A check or review that cannot run stops the commit.
- **These tools are C++ and depend on nothing in the repository.** Git hooks stay thin `sh` shims
  that only find, build and run the gate. `hooks/.gitattributes` keeps them LF, because `sh` reads
  a carriage return as part of a command.

## Build and test

```bash
cmake -S tools/check_layers -B tools/check_layers/build -G "Visual Studio 17 2022" -A x64
cmake --build tools/check_layers/build --config Release
ctest --test-dir tools/check_layers/build -C Release --no-tests=error
```
