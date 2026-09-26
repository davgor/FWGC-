# orchestrator/: local rules

The director: deterministic C++ over one world. **Lane: Claude.** The repository-wide rules are
in [`../AGENTS.md`](../AGENTS.md), and [`README.md`](README.md) says what it does and what a
speaker knows.

## Build and test

```bash
cmake -S orchestrator -B orchestrator/build -G "Visual Studio 17 2022" -A x64
cmake --build orchestrator/build --config Release
ctest --test-dir orchestrator/build -C Release --no-tests=error
```

## Rules

- **Deterministic.** No clock, no randomness, no file or network I/O. The same world and inputs
  give the same world and the same packets, and `replay()` must reproduce a session byte for byte.
  Every new input kind is recorded and replayed.
- **The world changes only through the generator's logged verbs,** the ones `generator/api.txt`
  lists: each takes `const World&`, returns a new world and logs the operation. Never write into a
  `World`.
- **Reach the generator through `fwg/api/` only.** Write `fwg::` in full, and call only listed
  functions. A function you need that the list lacks is added in a generator commit of its own
  first; this commit then uses it.
- **Beware argument-dependent lookup.** A helper called unqualified with a `fwg::` argument also
  searches namespace `fwg`. Check a new helper's name against the generator's declarations. Two
  of this component's first names collided.
- **What a speaker remembers, and how anyone feels, is folded from the recorded inputs.**
  Nothing is remembered or felt that replay cannot rebuild: a new thing to remember comes from an
  input the session records (a fright, a gathering, an act), and a test replays it. Time eases
  fear by the world's clock, never by the wall's.
- **Disclose only what the speaker would say:**
  - the face the rules bring out, never a hidden face before then: a mask until the player
    exposes it with cause, a beast's calm face but on the full moon's days, a fractured heart's
    face of the day;
  - a card's voice (`manner`, `voice`, `toward_player`, `line`, `never`), never its secrets
    (`lies`, `belief`, `wants`, `fears`, `tells`);
  - a hook's stated purpose, never its actual effect or whether the purpose is true;
  - places and people within reach, and a hero's rivals, kin and allies wherever they stand;
  - a refusal's reason in the director's own words, never the generator's message, which names
    quests by id and lists work the speaker does not know of;
  - a `voice_key` for the face shown, never for a mask still worn.

  Every fact names the world id it came from. A test guards each of these; keep them, and prove a
  new one by switching the rule off and watching its test fail.
- **`include/orchestrator/contract.hpp` names nothing of the generator.** The communicator includes
  it and nothing else from here; the layer checker refuses any other orchestrator header in a
  dependant (`public-include`). Keep it that way, and keep `orchestrator_contract` unlinked from
  `fwg`.
- **No model concepts.** Prompts and model plumbing belong to the communicator and inference. The
  layer checker refuses `llm`, `llama`, `gguf`, `inference`, `prompt`, `prompts`, `tokenizer` and
  `logits` in identifiers here. The red team watches for the rest (token, model, sampling), which
  the list leaves out because those words are too common to ban.
- **Tests assert properties and check their premise first.** A test that looks for something
  fails if the world does not hold it, rather than passing over nothing.
