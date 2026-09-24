# communicator/eval: the measuring stick

`communicator_eval` runs the interpreter over every case in `lines.txt` and the voice over every
case in `voice.txt`, on one model, and reports. It passes or fails nothing: it is how a model or
a prompt change is compared with another. The unit tests guard the guarantees. This folder
measures how well a model does inside them.

```bash
cmake --build communicator/build --config Release --target communicator_eval
communicator/build/Release/communicator_eval.exe [--model PATH] [--lines PATH] [--voice PATH] [--limit N] [--threads N]
```

Without `--model`, it uses `FWGC_TEST_MODEL`, then the test model `inference/README.md` names.
`--threads` sets the CPU threads (4 by default). Compare models at the same thread count:
greedy output is reproducible for one count, and can differ between counts.

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
  judge.

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
