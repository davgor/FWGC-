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

The tests run the test model that `inference/README.md` names, and take about 35 seconds.
`FWGC_TEST_MODEL` points them at another copy. Without a model, every test that needs one fails
and says how to get it.

## A conversation

The game joins the three components. The communicator never calls the director.

```
player's line --Interpreter--> DialogueAct --Director::respond--> FactPacket --Voice--> reply, streamed
                   ^                                                  |
                   +---------------------- Known::learn <-------------+
```

1. **`Known::learn(packet)`**, after every packet, records what the player has been told.
2. **`Interpreter::interpret(line, known, last_reply)`** gives an `Interpretation`: the act, the
   words its target was named by, and `meant`.
3. **The game hands the act to `Director::respond`.**
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
- **The persona:** the face a stranger sees, and the card's voice and `never` lines.
- **The facts:** plain lines, without ids.
- **What just happened:** the outcome, with a refusal's reason.

A grammar holds the reply. Every capitalised word must be a word of the packet's own text, or one
of a closed list of common words (`common_capitals()`: "I", "The", "Aye"...). Everything else is
lower-case words, digits and plain punctuation: no markdown, no line breaks. A reply therefore
cannot name a person or a place its packet does not name.

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
- **What the player knows:**
  - within a packet, the nearest comes first; across packets, the latest;
  - taking work moves it to held, and abandoning it forgets it;
  - the oldest are forgotten past a limit;
  - a packet of another contract version is refused.
- **The voice names only what its packet names.** Asked about Castle Grey, the free model names
  it; held, it cannot. The test checks the held replies to eight packets and lines.
- **No world id reaches a model:** no uid or fact id is in any prompt or grammar.
- **The voice's prompt carries its packet:**
  - the persona and its `never` lines;
  - every offer's giver and purpose;
  - the places and dangers;
  - a refusal's reason.
- **Streaming adds up, stops when asked, and greedy replies repeat.**
- **Refusals:** another contract version, a line past `kMaxLineBytes`, options out of range.

A mutation pass switched off each of these in a scratch copy, one at a time, and a test failed
every time (see the commit that added this component).

## What the test model does (measured, not guaranteed)

Qwen2.5-0.5B is a stand-in: too small to read players well, or to follow a prompt closely.

- **It reads 11 of 17 varied lines right** against a real castellan's packets. It reads 12 if
  "Where can I find the ninth stone on the Linbrig road?" counts as asking the way.
  - It reads the act right more often than the target.
  - It can take "Hello there!" for a question about the surroundings, and a demand for gold for
    a request for work.
  - `clear_lines_find_their_act` pins eight lines it reads right. It is a floor, not a score.
- **Its replies are short and often flat, and it misses instructions.**
  - Asked about a place it does not know, it describes one anyway, in words the grammar allows
    ("the castle is a grand and ancient structure"). The grammar keeps out names, not false
    claims.
  - A refusal can come out as agreement.
- **Timing** on this machine, 4 threads: an interpretation takes 0.9–1.7 s (one question or two),
  and a reply 0.3–1.4 s.

The models that ship are a later decision: a trained classifier for the interpreter, and a
fine-tuned voice (the plan's "Building our own model").

## Not in this slice

- **Names no packet gave.** "Castle Grey" can only become `no_act`. Looking a player's words up in
  the world needs a director read, which comes first as an orchestrator slice.
- **The player's own name.** No packet holds it, so the voice cannot say it.
- **Memory beyond the last reply.** There is also no prompt caching: the interpreter re-reads its
  examples on every call.
- **Names as the world gives them.** City names carry their age ("Wenweg (Age 2)"). The voice is
  told to leave the bracket out, and the test model often does not. Whether the world should give
  speakable names is a question for the generator.
- **Raw values.** Snake-case values such as `human_cold` are spaced into words. Better phrasing
  would come from the director or the generator, not from here.
