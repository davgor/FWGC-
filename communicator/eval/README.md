# communicator/eval: the measuring stick

`communicator_eval` runs the interpreter over every case in `lines.txt` and the voice over every
case in `voice.txt`, on one model, and reports. It passes or fails nothing: it is how a model or
a prompt change is compared with another. The unit tests guard the guarantees. This folder
measures how well a model does inside them.

```bash
cmake --build communicator/build --config Release --target communicator_eval
communicator/build/Release/communicator_eval.exe [--model PATH] [--lines PATH] [--voice PATH] [--authored PATH] [--limit N] [--threads N] [--gpu-layers N]
```

Without `--model`, it uses `FWGC_TEST_MODEL`, then the test model `inference/README.md` names.
`--threads` sets the CPU threads (4 by default). Compare models at the same thread count:
greedy output is reproducible for one count, and can differ between counts.

`--gpu-layers` offloads that many layers to a GPU, in a build of `inference` with
`INFERENCE_VULKAN`. The report's `device:` line says where the model ran. Compare on one device:
a GPU reads as many lines right, but words its replies differently (`inference/README.md`, "The
GPU build").

`--authored PATH` speaks every `voice.txt` case from written lines (`communicator/data/lines.txt`;
the communicator README's "Written lines") instead of the model, with the same checks and counts.
The model is still loaded: the interpreter runs on it, and the card-word count reads the words
its prompt gives. `--lines ""` skips the interpreter.

## What it reports

- **For the interpreter:**
  - how many lines are read as labelled: the act alone, and the act with its target;
  - the same split by the act meant, and by tag;
  - a confusion table (the act meant, then what it was read as);
  - every miss.
  - **Unsafe reads:** a line read as an act that would change the world (taking or giving up
    work) that the player did not mean. The game confirms those acts with the player before the
    director sees them, so an unsafe read costs a question. The count still says how often
    that question would surprise a player.
- **For the voice:** each reply is checked against its shape. It must open with its outcome's
  words, stay within its sentence count, end with its closing, and not be cut off. Every reply
  is printed, because whether it claims something its packet does not hold is for a reader to
  judge. Four counts help that reader:
  - **Card words:** words of the card (manner, voice, stance, `never` lines) that the reply
    says and nothing else in the prompt holds. They are the card taken for facts: "the watch
    rota", "sworn oaths". This is a proxy: it counts words, not how they were used, and leaves
    out short and common ones.
  - **Forbidden words:** words of `forbidden_words()`, or words beginning with one of its
    stems, that the packet does not say. Under the grammar it is 0. It counts only when a build
    without the ban is measured beside it.
  - **Days:** replies that speak of days. The prompt gives no number of days.
  - **Mid-sentence capitals:** common capitals past a sentence's start, "I" and the packet's own
    capitals aside, which pass for names ("a castle named Greetings"). Under the grammar it is
    0. It counts only when a build without the rule is measured beside it.

## lines.txt

One case per line, cells parted by `|` and trimmed; `-` is an empty cell, and `#` starts a
comment. A cell cannot hold `|`.

```
context | last_reply | line | act | target | tags
```

- **context:** what the player has been told (below).
- **last_reply:** what the speaker said just before, or `-`.
- **line:** what the player types.
- **act:** the act the director should answer, by the contract's name.
- **target:** the label of the thing meant, exactly as `Known` names it. `?` marks a name no
  packet gave; it is read right as `no_act` with `meant` set to the act. `-` marks an act
  without a target.
- **tags:** comma-separated. The vocabulary is clear, typo, slang, short, long, rambling,
  context, multi, hostile, offtopic, unnamed, partial, foreign, polite and command.
  - `unnamed` marks exactly the cases whose target is `?`.
  - `context` marks a line that needs `last_reply`.
  - `partial` marks a place named in part ("Hakndum" for Hakndum (Age 2)).

### How a line is labelled

1. The act is what the director should do with the line.
2. A greeting or thanks with a request is the request ("Hi! Any work?" is `ask_for_work`).
3. **Places:** a question about one named place, including the way there, is `ask_about_place`
   with that place. A question about what is around, about dangers, or about the way somewhere
   not named, is `ask_surroundings`.
4. **Names no packet gave:** a place or person the context never named has target `?`. A giver
   named only in a piece of work is not a known person.
5. "Who are you?" is `ask_about_person` with the speaker.
6. **Taking work:** saying yes to work is `accept_quest`, for the work the line names or the
   work just offered in `last_reply`. A line that could mean several pieces of work, with
   nothing to tell them apart, is left out of the set.
7. **Turning down and giving up:** `decline_quest` works the same way. `abandon_quest` is only
   possible where work is held.
8. **Nothing to point at:** an act with nothing of its kind in the context is `no_act`. That
   covers taking work where none is offered, and asking about a place in a context with no
   places.
9. **Leaving** is `farewell`.
10. **Everything else is `no_act`:** small talk, thanks alone, insults, threats, demands,
    nonsense, questions no act answers, and attempts to instruct the model.

### Contexts

Each is built in `main.cpp` from the packets the tests use (`tests/packets.hpp`).

| Context | Speaker | Places | People | Work on offer | Work held |
|---|---|---|---|---|---|
| `castellan` | Selbren, castellan at Wenweg (Age 2) | Wenweg (Age 2); the ninth stone on the Linbrig road; the hamlet of Wenweg (Age 2); the burnt works; the deeps under Linbrig; the fortress of Wenweg (Age 2); Hakndum (Age 2); Nerstrann (Age 1) | Selbren; Halbrann | Selbren's, Nerbrann's and two of Halbrann's | none |
| `castellan-held` | the same | the same | the same | the three not taken | Selbren: Hold the fortress above Wenweg (Age 2) with me. |
| `gate` | Fersel, gate captain at the fortress of Wenweg (Age 2) | the fortress of Wenweg (Age 2); the hamlet of Wenweg (Age 2); the hospice on the Wenweg (Age 2) pass | Fersel | none | none |
| `inn` | Dornwen, innkeeper at Torhas (Age 1) | Torhas (Age 1); the cold marker; the silent keep; Dunfor (Age 2); Strandbrig | Dornwen | Dornar's and Mekmoh's, neither Dornwen's own | none |
| `opening` | Fersel, a conversation just begun | none | Fersel | none | none |

## Models measured

Measured on 2026-09-24 on this machine's CPU (Core Ultra 9 275HX), 8 threads, the prompts as
committed. On a GPU the times would fall; the accuracy would not change.

| | Qwen2.5-0.5B (test model) | Qwen3-4B-Instruct-2507 |
|---|---|---|
| File (in `inference/models/`) | `qwen2.5-0.5b-instruct-q4_0.gguf` | `Qwen3-4B-Instruct-2507-Q4_K_M.gguf`, 2,497,281,120 bytes, Apache-2.0 |
| Lines read fully right | 117 of 243 (48%) | 186 of 243 (77%) |
| Unsafe reads | 12 | 0 |
| A named place | 3 of 37 | 25 of 37 |
| A person | 5 of 19 | 10 of 19 |
| Taking work | 15 of 23 | 19 of 23 |
| Turning work down | 3 of 13 | 2 of 13 |
| Giving work up | 4 of 8 | 1 of 8 |
| Small talk and the rest (`no_act`) | 26 of 57 | 50 of 57 |
| Time per line | 0.8 s | 5.3 s |
| Replies holding their shape | 24 of 24 | 24 of 24 |
| Replies saying card words | 1 (11 words: it read its card aloud) | 7 (9 words) |
| Forbidden words said | 0 | 0 |
| Replies talking of days | 0 | 0 |
| Common capitals mid-sentence | 0 | 0 |
| Replies with no fault, judged strictly (leniently) | 10 (13) | 11 (18) |
| Open-ended answers with no fault, of 9 | 0 (3) | 2 (5) |
| Time per reply | 0.46 s | 4.5 s |

The fault counts come from a separate session. It read every reply against its packet, with the
prompt code, and judged the model's own words: not the openings or closing the program writes.

The voice rows are for the prompt and grammar as they stand: barred words, common capitals only
where a sentence begins, and no card manner, card voice line or offer's days in the prompt.
Each change was measured against the one before it:

| Qwen3-4B | Card words | Forbidden | Days | Mid-sentence capitals | No fault (judged) |
|---|---|---|---|---|---|
| 1aaacce | 12 replies (25 words) | 3 | 3 | not counted | 6 (11) |
| Barred words, no manner or days | 6 (7) | 0 | 1 | 1 | 9 (13) |
| Common capitals only begin a sentence | 6 (7) | 0 | 1 | 0 | not judged: 1 reply changed |
| No card voice line | 7 (9) | 0 | 0 | 0 | 11 (18), against 9 (14) |

The card-word count is a proxy. Without the voice line, Qwen3-4B no longer swears the card's
oaths (4 replies to 0), but says "post" and "watch" instead, which count the same. The judged
column comes from two sessions, the last row from the second: two sessions differ by a reply or
two on the same replies, so compare counts from one session. The test model's counts barely
moved throughout. Without the voice line it once read its card aloud ("Toward strangers: offers
a fair trade..."), in a reply where it had leaked its prompt before.

Qwen3-4B's misses are mostly in the second question: which work, which person.
- **"Who are you?"** is read as a question about a person, then the model answers
  `(not listed)`, because nothing tells it who "you" is.
- **Giving work up** fails the same way: one piece of work held, and the model still answers
  `(not listed)`.
- **Turning work down** picks the wrong work.

**What did not help:** telling the model who is spoken to, so that "you" could mean them. It was
tried three ways, and each lowered Qwen3-4B's score to 74%.
- In every question, it taught the model that only the speaker's own work counts: taking work
  fell from 19 of 23 to 9.
- In the target questions alone, with examples that broke the pairing, taking work reached 12.
- In the person question alone, named people rose to 14 of 19, but unknown names collapsed from
  12 of 13 to 3: the model took strangers for the speaker.

The same change dropped the test model to 28%. The prompts went back to the ones above.

## voice.txt

`packet | line`. The packet names are in `main.cpp` (`voice_packets`).

## Where the cases came from

Written on 2026-09-24 with the owner's approval to spend hosted-model usage on them.

- **The core cases** (the first block of `lines.txt`, and `voice.txt`) were written by Claude.
  They are clear lines for every act in every context, and the cases that matter most:
  - the names no packet gave;
  - answers to an offer;
  - a hostile line.
- **The player-style cases** were written by a separate Claude session from the same rules:
  typos, slang, fragments, rambling, rudeness, other languages, attempts to steer the model.
  Every label was then checked against the rules above.
