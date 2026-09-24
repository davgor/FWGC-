# FWGC++ communicator

NPC dialogue, in two parts:
- the **interpreter** turns a player's words into one dialogue act;
- the **voice** puts a fact packet into the speaker's words.

It never sees a world. It includes `orchestrator/contract.hpp` alone and links only the
orchestrator's contract and `inference`. **The model translates and phrases; C++ decides.**

This is the `communicator/` component of the FWGC++ monorepo.

## Build and test

```bash
cmake -S communicator -B communicator/build -G "Visual Studio 17 2022" -A x64
cmake --build communicator/build --config Release
ctest --test-dir communicator/build -C Release --no-tests=error
```

The build adds `../inference`, which fetches llama.cpp pinned by hash (`inference/README.md`). It
also adds `../orchestrator`, but only the contract is built: the director, and the generator
behind it, are configured and never compiled.

The tests run the test model that `inference/README.md` names, and take about 50 seconds.
`FWGC_TEST_MODEL` points them at another copy. Without a model, every test that needs one fails
and says how to get it.

[`eval/`](eval/README.md) measures how well a model reads players and phrases packets, over
labelled cases. The tests guard the guarantees; `communicator_eval` measures what a model does
inside them.

## A conversation

The game joins the three components. The communicator never calls the director.

```
player's line --Interpreter--> DialogueAct --Director::respond--> FactPacket --Voice--> reply, streamed
                   ^                                                  |
                   +---------------------- Known::learn <-------------+
```

1. **`Known::learn(packet)`**, after every packet, records what the player has been told. Open a
   conversation with a packet (the speaker's greeting, for instance the answer to `no_act`), so
   the speaker's name is known before the player's first line.
2. **`Interpreter::interpret(line, known, last_reply)`** gives an `Interpretation`: the act, the
   words its target was named by, `meant`, and `confirm`.
3. **When `confirm` is set, the game asks the player first.** Only taking or giving up work
   changes the world (`orchestrator::act_changes_world`). For those acts, the game asks the
   question in `confirm` ("Take on this work?", with `label` beneath it), and the act goes to
   `Director::respond` only on yes. A misread line then costs a question, never a quest.
4. **`Voice::speak(packet, line, on_piece)`** streams the reply, piece by piece.

One `inference::Model` serves both the interpreter and the voice, called in turn.

## The interpreter

It asks the model two questions, and a grammar holds each answer:

1. **Which act.** The answer can only be an act of the contract, under the contract's own name
   (`orchestrator::to_string`). It can only be an act with something to point at: with no offer
   known, `accept_quest` is not in the grammar. Worked examples go in as earlier chat turns.
2. **Which target, for an act that takes one.** The model sees six known things of the act's
   kind, most likely first (those sharing the most words with the line and the last reply),
   and `(not listed)`.
   - `(not listed)` means the player named something no packet did ("Who is Gandalf?"). The act
     becomes `no_act`, and `meant` says what they meant.

**A name said outright needs no guess.** If the line holds a known label's words whole, that label
is the target, and the model is not asked. Case does not matter, nor do curly quotes, nor a part
in brackets: "Hakndum" names "Hakndum (Age 2)". The longest name said wins, and a tie is left to
the model.

`Known` labels things the way a player would say them:
- a place by its name, or as "the hamlet of X" when it has none;
- a person by their public name;
- work as "Giver: what they say it is for".

When two things share a label, the nearest in the latest packet is meant. Ids never reach the
model: C++ maps the chosen label back to the id the act carries.

## The voice

The prompt holds three things:
- **The persona:** the face a stranger sees, and the card's manner, voice, stance and `never`
  lines. Not the card's sample line: shown in any framing, Qwen3-4B said it back whole, in 9
  of 24 replies.
- **The facts:** plain lines, without ids, as they would be said.
  - A part of a name in brackets is dropped, so "Wenweg (Age 2)" is Wenweg.
  - A scale of 1 to 5 comes in words beside the number: "easy work (2 of 5)", "threat high
    (4 of 5)".
  - An offer's days left read as "the offer stands for 948 more days". Bare, both models took
    the numbers for how long the work lasts, or for "danger level 5.1".
- **What just happened:** the outcome, with a refusal's reason.
- **The closing rules:** a length, no names beyond the packet's, nothing of pay, and how far
  places are but never which way.

A grammar holds the reply, and what happened shapes it:

| Outcome | The reply opens with one of | Sentences |
|---|---|---|
| answered | anything | up to three |
| accepted | Good, Then it's agreed, Done, Very good, Thank you | up to two |
| declined | Very well, As you wish, Suit yourself, So be it, Fair enough | one |
| abandoned | So be it, Very well, A pity, As you wish, Then it's done | one |
| unknown | I don't know, I've never heard, I can't say, I know nothing, Never heard, No idea, I couldn't tell you | one |
| refused | No, I can't, I cannot, I won't, Not from me, That's not mine | one, then C++'s closing |
| parted | Farewell, Safe travels, Go well, Goodbye, Take care | one |
| not understood | I don't follow, I'm not sure what you mean, What do you mean, I can't help with that, Speak plainly | one |

The model chooses among the openings and writes the rest. A reply that opens by stating what
happened rarely turns into its opposite. A short one leaves little room to invent.

A refusal ends with a sentence C++ writes, word for word: who to see instead ("Speak to Halbrann,
at the ruins of Linbrig.").

Every capitalised word the model writes must be a word the prompt gives as something to say,
or one of a closed list of common words (`common_capitals()`: "I", "The", "Aye"...). No compass
point is on that list, because no packet holds a bearing. Everything else is lower-case words,
numbers and plain punctuation:
- no markdown, no line breaks;
- no double quotes: after a stop, Qwen3-4B had closed quotes it never opened, 14 times in 24;
- a sentence ends with one stop, an ellipsis, `?!` or `!?`: a run of stops let one reply spin out
  to its token limit.

A reply therefore cannot name a person or a place its packet does not name.

## What it guarantees, each with a test

- **An act of the contract, always.** Hostile lines, nonsense, other languages, JSON and empty
  lines all come back as one act, with a target exactly when the act takes one. Told to answer
  an act the contract lacks, the free model does. Held, it cannot.
- **A target is something the player was told of, of the act's kind.** Told to copy a place
  into a person question, the free model does. Held, it cannot.
- **Nothing known, no target.** With nothing to point at, no act that takes a target is offered.
- **The acts are the contract's own,** under the contract's names.
- **A name said outright wins over the model's guess.** In the test, the model alone picks the
  wrong one of two people.
- **Taking or giving up work comes with a question,** and no other act does.
- **What the player knows:**
  - within a packet, the nearest comes first; across packets, the latest;
  - taking work moves it to held, and abandoning it forgets it;
  - the oldest are forgotten past a limit;
  - a packet of another contract version is refused.
- **The voice names only what its packet names.** Asked about Castle Grey, the free model names
  it; held, it cannot. The test checks the held replies to eight packets and lines.
- **A reply opens by saying what happened.** Free, a speaker who knows nothing of Castle Grey
  claims to know it; held, every outcome but an answer opens with its own words.
- **Where there is little to say, one sentence.** Free, some reply runs on; held, unknown,
  refused, declined, abandoned, parted and not understood each say one.
- **A refusal says who to see in C++'s words,** exactly, and only when it names one person.
- **A reply quotes nothing.** Begun with an open quote, the free model closes it; held, it
  cannot.
- **A sentence ends with one stop.** The test model will not run on however it is asked, so
  this test reads the rule in each packet's grammar instead of a reply.
- **No world id reaches a model:** no uid or fact id is in any prompt or grammar.
- **The voice's prompt carries its packet, as it is said:**
  - the persona and its `never` lines;
  - every offer's giver and purpose;
  - the places and dangers;
  - a refusal's reason;
  - scales in words and the offer's days as a standing offer;
  - no bracketed part of a name, and no sample line to say back.
- **Streaming adds up, stops when asked, and greedy replies repeat.**
- **Refusals:** another contract version, a line past `kMaxLineBytes`, options out of range.

A mutation pass switched off each of these in a scratch copy, one at a time, and a test failed
every time (see the commit that added this component).

## What the models do (measured, not guaranteed)

`communicator_eval` measured two models on 2026-09-24, over 243 lines and 24 replies, on this
machine's CPU at 8 threads. [`eval/README.md`](eval/README.md) has the full table.

| | Qwen2.5-0.5B (test model) | Qwen3-4B-Instruct-2507 |
|---|---|---|
| Lines read fully right | 48% | 77% |
| Unsafe reads | 12 (5%) | 0 |
| Replies holding their shape | 24 of 24 | 24 of 24 |
| Per line / per reply (CPU) | 0.8 s / 0.4 s | 5.3 s / 4.3 s |

- **The interpreter.**
  - The test model guesses "what is around" and "work" too often. It reads a named place 3
    times in 37.
  - Qwen3-4B reads places (25 of 37), work and small talk well. It misses the second question:
    "Who are you?" and giving up work both end as `(not listed)`, and turning work down picks
    the wrong work.
  - `clear_lines_find_their_act` pins eight lines the test model reads right. It is a floor,
    not a score.
- **The voice.** A separate session judged every reply against its packet, strictly.

  | | Qwen2.5-0.5B | Qwen3-4B |
  |---|---|---|
  | Replies with no fault, of 24 (lenient) | 10 (12) | 7 (15) |
  | The 9 open-ended answers with no fault (lenient) | 0 (2) | 0 (4) |
  | Confuses who is who | 6 | 0 |
  | Invents a fact | 6 | 17 |
  | Contradicts the outcome, or repeats instructions | 0 | 0 |

  - The shapes hold: no reply contradicts what happened, and none parrots its instructions.
  - The test model speaks as the player ("Thank you, Selbren").
  - Qwen3-4B answers fully but invents, from three sources:
    - the card's manner ("The watch rota is sacred" turns into oaths and rules);
    - directions it was told not to give ("To the north...");
    - "the offer stands for 948 more days", read as how long the work takes.

  The grammar keeps out names, not false claims.

The models that ship are a later decision: a trained classifier for the interpreter, and a
fine-tuned voice (the plan's "Building our own model").

## Not in this slice

- **Words a reply may not use.** The grammar limits capitalised words only. A lower-case
  compass point ("to the north") or a coin gets through. Banning whole words in the grammar
  (compass points, money) would make "never which way" and "nothing of pay" hold, as the names
  rule does.
- **"Who are you?"** Telling the model who is spoken to made things worse, measured three ways
  (`eval/README.md`).
- **Names no packet gave.** "Castle Grey" can only become `no_act`. Looking a player's words up in
  the world needs a director read, which comes first as an orchestrator slice.
- **The player's own name.** No packet holds it, so the voice cannot say it.
- **Memory beyond the last reply.** There is also no prompt caching: the interpreter re-reads its
  examples on every call.
- **Names as the world gives them.** City names carry their age ("Wenweg (Age 2)"). The voice
  drops the bracket before the model sees it, but labels keep it, so a game showing `label` shows
  it. Whether the world should give speakable names is a question for the generator.
- **Raw values.** Snake-case values such as `human_cold` are spaced into words. Better phrasing
  would come from the director or the generator, not from here.
