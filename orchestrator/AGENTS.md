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
  input the session records (a fright, a gathering, an overhearing, an act), and a test replays
  it. Time eases fear by the world's clock, never by the wall's.
- **The player's name is only ever one the player gave** (contract version 24): told to the
  speaker, overheard, or carried with word of work the player did for someone they told. Nothing
  here makes one up or takes one from elsewhere, and a name spreads as it was given, a lie as a
  lie; the owner ruled it so (README "The player's name"). A new way a name travels comes from a
  recorded input, with a test that replays it.
- **Heroes act only at the game's call** (contract version 26, `direct_heroes`, README "Heroes' deeds"):
  the director decides what a hero does and how it goes, and the world records it through the
  generator's logged verb, never by hand. How it goes is a hash of the hero's seed, the day and the
  deed, never a roll, so replay acts the same. A new action, a new rule of chance, or anything that
  follows from an outcome (how a hero takes it) comes with a test that replays it, and a number the
  chance is held to binds on some deed or is not kept. How a hero takes a deed (contract version 30,
  README "How a hero takes their deed") comes in only by `react`, the game's call: one of a closed set,
  refused unless open to how the deed went, what it does decided here by rule, and recorded as applied,
  so replay runs no picker; with none handed in, the rules pick. What it does to the world goes through
  the generator's verb (`hero_shift_request`), and what it does to their next deed is folded from the
  recorded inputs, as everything here is.
- **Disclose only what the speaker would say:**
  - the face the rules bring out, never a hidden face before then: a mask until the player
    exposes it with cause, a beast's calm face but on the full moon's days, a fractured heart's
    face of the day;
  - a card's voice (`manner`, `voice`, `toward_player`, `line`, `never`), never its secrets
    (`lies`, `belief`, `wants`, `fears`, `tells`);
  - of themself (`ask_about_them`, contract version 13), the world's public record of them, never
    the card's: a hero's epithet, fame, home, presence, claim, a city lost, deeds and ties; a mask
    still worn, only its trade and where it stands; a roster person, their post and place;
  - a hook's stated purpose, never its actual effect or whether the purpose is true;
  - how their own work went (contract version 20), as the game's account has it, to its giver alone
    and only while that work's end is their latest dealing with the player;
  - places and people within reach, and a hero's rivals, kin and allies wherever they stand;
  - by fame (contract version 25), their own realm's sovereign wherever they stand, and a hero whose
    known deed's word reaches them (through the hero's realm; a region's width about where the hero
    stands), with that one deed and how far word of it went, never a mask's. Someone asked of by the
    player's words is looked up only among those the speaker knows: a king named is that king, never
    taken for the speaker's own. A width or a rule of fame comes with a world where it binds, or it
    is not added: a "world" scope no deed of fifty worlds reached was left out;
  - a refusal's reason in the director's own words, never the generator's message, which names
    quests by id and lists work the speaker does not know of;
  - a `voice_key` for the face shown, never for a mask still worn.

  Every fact names the world id it came from. A test guards each of these; keep them, and prove a
  new one by switching the rule off and watching its test fail.
- **A log becomes long memory only through `compact()`, and only the game calls it** (README
  "Short and long memory"). Nothing here compacts of itself, on a timer or a count. Outcomes handed
  to it are data: each quote is found word for word in the stranger's own lines, each name in the
  log, and what fails is refused and said; deeds, work, what is open and what became of work come
  from the director's own state, never from them. They are recorded with the compaction, so replay
  runs nothing that wrote them. These docs name no model: what writes a summary is the
  communicator's. A question the speaker asked (contract version 21) is the game's to mark with the
  words, held word for word to the reply; whether it was answered is the stranger's next act, never
  a reading of any words, and a greeting carries it again once, which the log records.
- **The brain database is the director's to write and the game's to read** (`src/brains.cpp`,
  README "News, and the brain database").
  - Only the director's recorded inputs write it, so replay writes it again row for row; a test
    compares every table of a replayed director with the live one. A game's query only reads: a
    statement that would write, attach a file, change a setting or begin a transaction is refused
    before it runs, and a test tries each kind.
  - It lives in memory and nowhere else: no file is opened, attached or written.
  - Every statement the director runs is deterministic: whatever it reads to decide is read in a
    fixed order (`ORDER BY`, a key), and it calls no `random()` and no clock.
  - Its tables are part of the C interface. A change to a table or a view a game reads bumps
    `brains::kSchemaVersion` and `FWGC_API_VERSION`, and README says what changed.
  - Its copies of the director's own state (`people`, `places`, `work`, `relationships`) are
    written afresh before a query when what they copy has moved; nothing the director decides reads
    them.
- **`include/orchestrator/contract.hpp` names nothing of the generator.** The communicator includes
  it and nothing else from here; the layer checker refuses any other orchestrator header in a
  dependant (`public-include`). Keep it that way, and keep `orchestrator_contract` unlinked from
  `fwg`.
- **The C interface keeps pace with the director.** Every public `Director` call and input kind
  has its `fwgc_` function in `include/orchestrator/fwgc_director.h` and a check in
  `tests/test_c_api.cpp`, and a change that adds one lands with them. Its tripwires stop the build
  until it does: the count of input kinds in `src/c_save.cpp`, and the structured bindings over
  every type it writes out in `src/c_json.cpp`. A new call trips nothing, so mind it. Nothing
  crosses the interface but a status and plain data: no exception, no C++ type. The communicator
  links `orchestrator_contract`, never `fwgc_director`.
- **No model concepts.** Prompts and model plumbing belong to the communicator and inference. The
  layer checker refuses `llm`, `llama`, `gguf`, `inference`, `prompt`, `prompts`, `tokenizer` and
  `logits` in identifiers here. The red team watches for the rest (token, model, sampling), which
  the list leaves out because those words are too common to ban.
- **Tests assert properties and check their premise first.** A test that looks for something
  fails if the world does not hold it, rather than passing over nothing.
