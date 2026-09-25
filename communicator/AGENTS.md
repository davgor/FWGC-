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
  `common_capitals()`: only words sentences begin with. The grammar lets them begin a sentence
  and stand nowhere else; past a sentence's start, a capital is the packet's own, or "I".
- **The voice gives no bearing and speaks of no pay, in the words of `forbidden_words()`.**
  - A packet's own word stays sayable, and only that exact word.
  - An entry is a stem (`north*`) only when every word it begins should be barred too.
    Otherwise it goes in whole, with its forms.
  - No common capital may be a barred word.
- **A written line keeps the voice's rules and says only what its blanks hold.**
  - `every_line_is_spoken_and_keeps_the_voices_rules` holds every row of `data/lines/` to the
    voice's rules. A row that breaks one fails by its own words. Run
    `communicator_eval --check-lines` on new rows before a commit does.
  - A bank sounds like its own archetype and alignment, not like its neighbours, and says how
    the speaker stands, never what they have done. A pet name at most once in a bank, and only
    as contempt or command: repeated, it is a tic.
  - A line adds manner, never a fact. A place, a creature, a duty or a distance comes only
    through a blank, and a blank only from the packet.
  - Draw a voice from a card's public part (manner, voice, stance, sample line, never), never
    from what the card keeps back (belief, wants, fears, tells, lies). The first warden lines
    took "stays behind the line" from the warden's belief, and a judge heard it as a rule.
  - A new situation or blank goes in `Lines::situations()`, with a test packet that speaks it.
- **A world-changing act is confirmed before it reaches the director.** Whatever joins the
  communicator to the director (the game, a binding) asks `Interpretation::confirm` and hands on
  the act only on yes. A reply's shape (`reply_shape()`) states the outcome first; an outcome
  added to the contract gets its openings and its length there.
- **Measure a prompt or model change with `communicator_eval`,** before and after, at the same
  thread count. Put both numbers in the commit message. A change that helps the tests' lines
  and hurts the set is a regression.
- **Local only**, like every runtime component: models are local files, loaded through
  `inference`.
- **Tests fail loudly without their model, and check their premise first.**
  - A control shows what the free model does before a test shows what the grammar prevents.
  - `clear_lines_find_their_act` is the floor the test model reaches, not a score. When the
    model changes, re-measure it; do not tune prompts until it passes.
