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

The prompt holds four things:
- **The persona:** the face a stranger sees, and the card's stance and `never` lines.
  - Not the card's sample line: shown in any framing, Qwen3-4B said it back whole, in 9 of 24
    replies.
  - Not the card's manner: Qwen3-4B told it as fact, framed as manner or not ("The watch rota
    is sacred" came back as rotas "sworn", "standing firm" and "unbroken").
  - Not the card's voice line: from "precedent and oaths cited when pressed", Qwen3-4B swore
    oaths in 4 replies of 24, and in none without the line. Its speech grew plainer, and the
    owner chose that (2026-09-24).
- **The facts:** plain lines, without ids, as they would be said.
  - A part of a name in brackets is dropped, so "Wenweg (Age 2)" is Wenweg.
  - A scale of 1 to 5 comes in words beside the number: "easy work (2 of 5)", "threat high
    (4 of 5)". Bare, the numbers were misread ("danger level 5.1", difficulty 2 called hard).
  - Not the days an offer stands. However they were put ("948 days left", "the offer stands
    for 948 more days"), a model took them for how long the work lasts. The game can show a
    deadline.
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

Every capitalised word the model writes must be a word the prompt gives as something to say, or
"I". A sentence may also begin with one of a closed list of common words (`common_capitals()`:
"The", "Aye", "Greetings"...), and only begin with one: anywhere else it passes for a name, as
when Qwen3-4B told of "a castle named Greetings". No compass point is on that list, because no
packet holds a bearing. Everything else is lower-case words, numbers and plain punctuation:
- no markdown, no line breaks;
- no double quotes: after a stop, Qwen3-4B had closed quotes it never opened, 14 times in 24;
- a sentence ends with one stop, an ellipsis, `?!` or `!?`: a run of stops let one reply spin out
  to its token limit.

Lower-case words are held too. No word of `forbidden_words()` may be said unless the packet's
own text says it:
- **Compass points are stems:** no word may begin with one. When only whole words were barred,
  Qwen3-4B ran on past them 9 times in four runs of 24 replies: "northwardstretch",
  "westwardwhere", "westernmost".
- **Money and reward:** "gold", "coin", "wage", "bounty", "treasure" and more, whole, and every
  word that begins with "pay", "repay" or "reward". Only those three are stems, because other
  money words begin ordinary words ("golden", "coincidence"). "Crown" is not barred: in a
  fantasy world it is as often the throne.
- **A packet's own word is sayable, and only that word.** A speaker who knows the silver mine
  may say "silver". One who knows the southern tower may say "southern", and still not "south"
  or "southwest".

A reply therefore cannot name a person or a place its packet does not name, in capitals, nor
give a bearing or speak of pay in any of those words. A name in lower case gets through: asked
about Castle Grey, Qwen3-4B said it knew of no "castle named grey".

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
  it; held, it cannot. The test checks the held replies to eight packets and lines, and that
  none holds a common capital past a sentence's start.
- **A common capital only begins a sentence.** Told to say "It is a castle named Greetings.",
  the free model does; held, it cannot. "Greetings, traveller." and "Yes, I serve Selbren at
  Wenweg." still come out whole.
- **A reply opens by saying what happened.** Free, a speaker who knows nothing of Castle Grey
  claims to know it; held, every outcome but an answer opens with its own words.
- **Where there is little to say, one sentence.** Free, some reply runs on; held, unknown,
  refused, declined, abandoned, parted and not understood each say one.
- **A refusal says who to see in C++'s words,** exactly, and only when it names one person.
- **A reply quotes nothing.** Begun with an open quote, the free model closes it; held, it
  cannot.
- **A sentence ends with one stop.** The test model will not run on however it is asked, so
  this test reads the rule in each packet's grammar instead of a reply.
- **No bearing and no pay, in the barred words.** Told to say "north" or "gold", the free model
  does; held, it cannot. Told to say "northernmost" or "paymaster", which no list holds, held,
  no word it writes begins with the stem. No common capital is a barred word: the ban holds
  lower case, and with any capital allowed, the test model wrote "North."
- **A packet's own word may be said, and nothing past it.** Beside the silver mine, "silver" can
  be said, and elsewhere it cannot. Beside the southern tower, "southern" can, and "south" and
  "southwest" still cannot.
- **No world id reaches a model:** no uid or fact id is in any prompt or grammar.
- **The voice's prompt carries its packet, as it is said:**
  - the persona and its `never` lines;
  - every offer's giver and purpose;
  - the places and dangers;
  - a refusal's reason;
  - scales in words, and no days an offer stands;
  - no bracketed part of a name, no sample line to say back, and no manner or voice line to tell
    as fact.
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
- **The voice.** A separate session judged every reply against its packet, strictly, not told
  which version of the prompt wrote which. The counts are for the prompt and grammar as they
  stand.

  | | Qwen2.5-0.5B | Qwen3-4B |
  |---|---|---|
  | Replies with no fault, of 24 (lenient) | 10 (13) | 11 (18) |
  | The 9 open-ended answers with no fault (lenient) | 0 (3) | 2 (5) |
  | Confuses who is who | 6 | 0 |
  | Invents a fact | 5 | 12 |
  | Withholds an answer it had | 2 | 0 |
  | Contradicts the outcome | 0 | 1, borderline |
  | Reads out its card or instructions | 1 | 0 |

  - The test model speaks as the player ("Thank you, Selbren"), and once reads its card aloud
    instead of saying where the stranger is.
  - Qwen3-4B answers fully but invents. It no longer names a compass point, reads a deadline as
    the work's length, tells of the watch rota, swears the card's oaths, or makes a name of a
    common capital. What remains:
    - the card's stance and never-lines, told as fact: "you must prove your worth", "any duty I
      hold", in 4 replies;
    - a direction in other words ("to the immediate left"), distances out of order, and details
      nobody gave ("return by dusk");
    - once, work given up called "finished".

  Each change was judged in one session against the prompt before it. Barred words, with the
  manner and the deadline left out, took Qwen3-4B from 6 (11) replies with no fault to 9 (13).
  Dropping the voice line took it from 9 (14) to 11 (18), in a second session. The test model
  stayed at 11 (12), then 10 (13). Two sessions differ by a reply or two on the same replies, so
  compare counts from one session. The capitals rule came after the judging. It changed none of
  Qwen3-4B's judged replies and one of the test model's, a farewell ("Goodbye, Farewell."
  became "Goodbye, and thank you for your time.").

  The grammar keeps out names and barred words, not false claims.

The models that ship are a later decision: a trained classifier for the interpreter, and a
fine-tuned voice (the plan's "Building our own model").

## Not in this slice

- **Other ways to say which way, or what it pays.** The grammar bars words, not meanings:
  "toward the sunrise", "in the direction of the deeps" and "worth your while" get through. So
  does a word glued onto a barred money word that is not a stem. No reply has held one, but told
  to say "silver" with it barred, the test model wrote "silvergold".
- **Names in lower case.** The grammar holds capitals. Kept from "Grey", and since the
  capitals rule from "Greetings" too, Qwen3-4B said it knew of no "castle named grey". That
  echoes the player's own words; an invented name in lower case would get through the same way.
- **The card's stance and never-lines, told as fact.** Qwen3-4B turns "Proves the player's
  standing before trusting them" into "you must prove your worth", and "act outside the office
  they hold" into duties, in 4 replies of 24. Dropping them too would leave a speaker no
  character but a name, a post and a place.
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
