# communicator/: local rules

NPC dialogue: the interpreter and the voice. **Lane: Claude.** The repository-wide rules are in
[`../AGENTS.md`](../AGENTS.md), and [`README.md`](README.md) says what it does, what each test
guarantees and what the test model manages.

## Build and test

```bash
cmake -S communicator -B communicator/build -G "Visual Studio 17 2022" -A x64
cmake --build communicator/build --config Release
ctest --test-dir communicator/build -C Release --no-tests=error
```

## Rules

- **It never sees a world.**
  - Of the orchestrator it includes `orchestrator/contract.hpp` alone, and links
    `fwgc::orchestrator_contract`, never `fwgc::orchestrator`.
  - Nothing of the generator.
  - The layer checker refuses any other orchestrator header, and any `fwg/` include or `fwg::`
    name.
- **The model classifies and phrases; C++ decides.** The communicator turns words into an act
  of the contract, and a packet into words. It never decides an outcome, or what a speaker
  knows: those are the director's. It never calls the director either; the game does.
- **Every model answer is held by a grammar,** and C++ reads only answers the grammar allows. A
  new question put to a model comes with its grammar, and a test that the model cannot answer
  outside it.
- **No id reaches a model.** A model sees labels; C++ maps a label back to its id. A test checks
  every prompt and grammar for ids.
- **The voice names only what its packet names.** Nothing that can be a name goes into
  `common_capitals()`: only words sentences begin with.
- **Local only**, like every runtime component: models are local files, loaded through
  `inference`.
- **Tests fail loudly without their model, and check their premise first.**
  - A control shows what the free model does before a test shows what the grammar prevents.
  - `clear_lines_find_their_act` is the floor the test model reaches, not a score. When the
    model changes, re-measure it; do not tune prompts until it passes.
