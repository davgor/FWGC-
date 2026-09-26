# FWGC++ communicator

NPC dialogue, in three parts:
- the **interpreter** turns a player's words into one dialogue act;
- the **voice** puts a fact packet into the speaker's words, through a model;
- the **written lines** answer a fact packet with a line a person wrote, its blanks filled from
  the packet, and no model at all.

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
4. **`Voice::speak(packet, line, on_piece)`** streams the reply, piece by piece. Or
   **`Lines::speak(packet, line, previous)`** gives a written one ([Written lines](#written-lines)).

One `inference::Model` serves both the interpreter and the voice, called in turn.

**A conversation is remembered** (contract version 4). Every packet carries what its speaker
remembers of the player: whether they have met and how long ago, how the speaker takes them, the
player's dealings with the speaker's own work, what they asked about last time, and whether they
have asked the same thing twice now. The director keeps it; the communicator speaks from it. A
greeting to someone met before is not a first greeting, a cold speaker greets coldly, a speaker
whose work the player finished says so, and a question asked twice is answered "As I said, ...".

## The interpreter

It asks the model two questions, and a grammar holds each answer:

1. **Which act.** The answer can only be an act of the contract, under the contract's own name
   (`orchestrator::to_string`). It can only be an act with something to point at: with no offer
   known, `accept_quest` is not in the grammar; and `answer` only when the speaker's last words
   asked a question. Worked examples go in as earlier chat turns.
   - The sixteen acts are shown in the contract's order with `no_act` ("anything else") last:
     the nine first measured, then greet, small_talk, answer, thank, insult, ask_safety and
     confront. The test model reads the first acts listed far more often: shown greet and
     small_talk first, it read 16 of 23 clear lines as one of those two.
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
  - How far, in words, as roughly as the packet says it (contract version 3): "a short walk
    away", "an hour's walk away", "a long way off". Given kilometres, Qwen3-4B read them out in
    6 replies of 24 and rounded them wrong ("thirteen kilometers" for 13.6); given words, in 0.
  - Never a piece of work's map marker (`marker`, `marker_node`): it is for the game. Neither
    the prompt nor the grammar changes with it, so no id's words become sayable.
- **What just happened:** the outcome, with a refusal's reason.
- **What the speaker remembers of the stranger** (contract version 4), in sentences: "You have
  met this stranger before, last a few days ago.", "They finished the work you gave them.", "Last
  time they asked you about the burnt works.", "They have asked you this already: say so." Never
  the Memory fact's keys. Small talk's weather, "is it safe"'s verdict and an owned-to face come
  as facts in words: "Here and now: weather: stormy; wind: a gale ...", "Your judgement: it is
  not safe here.", "The stranger has seen through your mask: you are a magister, not a midwife."
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
| not understood | I don't follow, I'm not sure what you mean, What do you mean, That's beyond me, Speak plainly | one |
| answered, to "is it safe" | unsafe: No, Not here, Not safe, Far from it, I wouldn't say so; wary: Safe enough, Mostly, More or less, Careful, Keep your wits about you; safe: Safe enough, Yes, Aye, It's safe, Quiet enough, Nothing to fear | up to three |
| greeted | a first meeting: Hello, Greetings, Well met, Good day, Welcome, Hail; met before: Hello again, Back again, Welcome back, Good to see you, Well met again, Good day; a cold speaker: What do you want, What now, Keep back, Stay back | up to two |
| chatted (small talk) | anything | up to two |
| acknowledged | I see, Is that so, Fair enough, Good, Right, Ah, Well, Aye, Indeed | up to two |
| thanked | You're welcome, No need, Think nothing of it, Glad to, My pleasure, Don't mention it; a cold speaker: Save your thanks, Keep your thanks, Hmph, Spare me | one |
| offended | Watch your tongue, Mind your manners, How dare you, Enough, Careful, Hold your tongue, There's no need for that | one |
| exposed | So, So you know, Very well, Fine, Clever, Yes, You've found me out, Enough, then | up to three |
| denied | No, Nonsense, You're mistaken, I don't know what you mean, What are you on about, That's not so, You have the wrong one | one |

The model chooses among the openings and writes the rest. A reply that opens by stating what
happened rarely turns into its opposite. A short one leaves little room to invent.

A refusal ends with a sentence C++ writes, word for word: who to see instead ("Speak to Halbrann,
at the ruins of Linbrig."). A written line that names the giver itself ends with where they are
alone ("Find them at the ruins of Linbrig."), so the giver is named once.

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

## Written lines

`communicator/authored.hpp`. A reply is a line a person wrote, with blanks that C++ fills from
the packet. No model writes it, so it says only what the packet holds, in words a writer chose.
The owner asked for NPCs that feel like "scripted real lines, not fake AI slop", especially the
larger-than-life heroes.

- **The library:** [`data/lines/`](data/lines/README.md), one file per archetype plus the roster
  posts and `any`, rows of `voice | situation | line`. `Lines::load` reads every `.txt` in the
  folder. The folder's README holds the format and the rules for writers.
- **Banks.** A hero with one face speaks from their archetype and alignment ("warden/LN", the
  persona's `voice_key`, from contract version 2). An alignment with no bank of its own speaks
  from its archetype's true-neutral bank ("warden/NN"), where the archetype "shows almost
  unmodified". Everyone then falls back to their post's lines (`post:innkeeper`), then to `any`.
  The first bank with a line the packet can fill answers.
- **Situations** are read from the packet (`Lines::situation`): the outcome, and for an answer,
  what was asked. A refusal splits by its reason's `why`: the work not theirs to give or to
  release, the speaker gone, or refused by the world.
- **Conversation situations come as a chain, closest first** (`Lines::situations_for`, contract
  version 4), and each bank answers from the first it has a line for, before the next bank is
  tried: a warden with no warm greeting greets as a warden, from `greeted_again`.
  - A greeting by what the speaker remembers: a first meeting; met before, and warmly; a cold
    speaker; or what came of the speaker's own work (taken and not done, finished, failed, given
    up), first. `{since}` says how long ago ("a few days ago"), `{asked}` what was asked then.
  - Small talk by the weather where the speaker stands (fair, wet, storm), with `{season}`,
    `{time}` and the worst `{danger}`; a cold speaker has none of it.
  - "Is it safe" by the verdict, `{worst}` the place it rests on.
  - An exposure by the face owned to; a second insult or denial by its own lines first.
  - No chain crosses from one reply shape's openings to another's.
- **Asked twice in one conversation,** an answer is said after "As I said," or "Again," (C++'s
  words, its first word lowered unless it is "I" or a name): the speaker says so, and the line
  itself stays one a writer wrote.
- **Blanks** are filled from the packet only (`Lines::situations()` lists each situation's):
  `{giver}`, `{here}`, `{nearest}`, `{others}`, `{danger}`, `{own}`..., and in every situation
  `{speaker}`, `{post}` and `{place}`. A line with a blank its packet cannot fill is not said.
  - `{here}` is where the speaker stands, by name, so "where am I" has its answer.
  - A place comes with how far it is, roughly, how dangerous when it is, and who holds it: "the
    deeps under Linbrig, an hour's walk away, deadly, held by monsters". A name said twice is
    "another" the second time: "another hamlet of Wenweg".
  - The nearest three are named, and the most dangerous place the speaker knows always, even
    further off: asked whether it is safe, a speaker names the worst of it.
  - Two places at the same range share it: "Dunfor and Strandbrig, both a long walk away". One
    with details goes first ("the deeps under Linbrig, deadly, held by monsters, and the hamlet
    of Wenweg, both an hour's walk away"): said last, a judge read the hamlet as deadly too.
  - A nest is the worst the packet holds.
  - Work comes with how hard and how far: "fair work, a long walk away".
  - Another's work is told as theirs ("with me" becomes "with them"), by what it asks, not by
    the reason given first ("Our fields lie in its reach: drive off...").
  - A refusal names its giver only when it names one person.
  - A giver whose role is a body they sit on is "of" it: "Dornar of the council".
- **Optional parts.** A part of a line in `[brackets]` is left out whole when the packet cannot
  fill a blank in it. So one line serves a packet with a nest and one without:
  `The nearest is {nearest}.[ Then there's {others}.][ Mind the {danger}.]`
- **The fullest lines are said.** Of the lines that fit, only those that say the most of the
  packet are candidates: the most distinct blanks, so a blank said twice counts once, and the
  speaker's own name, post and place do not count. A speaker who knows of a nest does not drop
  it for a shorter line: the first judge found withholding the written lines' main fault.
- **Checking rows.** `communicator_eval --check-lines PATH` holds every row of a file or folder
  to the voice's rules, as the tests do, with no model: writers run it on their own rows.
- **A world refusal says why,** in the director's words ("the offer has lapsed"). Before
  orchestrator 1228ec5 that reason was the generator's own message, quest ids and all.
- **The choice goes round** (contract version 6). Each speaker starts at a line of their own and
  says the next each time they have heard the same act before (the Memory fact's `heard`), so the
  same packet gets the same reply and a farewell said on five visits is said five ways while the
  bank has five. `previous` (the last reply) is not said again while another line fits. A judged
  session (2026-09-26) had one armorer end five partings alike, and a speaker's later visits
  replay the first word for word.
- **A cold speaker stays cold.** A wary or hostile speaker greets, chats, takes thanks and parts
  (`parted_cold`: "Go, then", "Be on your way") from cold lines only; a bank without them leaves
  the reply to the next bank's rather than answering warmly.
- **Someone met before is no stranger.** A line that addresses the player as one ("Mind your
  manners, stranger") is not said to someone met before.
- **Small talk is one pool:** the weather's lines and the rest are said alike, and `small_talk`
  says nothing of the weather.
- **Small talk answers what it was about** (`communicator/meaning.hpp`), when the game hands
  `speak` a `Meaning`: an embedding model (Qwen3-Embedding-0.6B, Q8_0, about 640 MB, loaded for
  embeddings) compares the player's words with two written subjects, the weather and how the
  speaker is keeping. "Still cold out?" is answered from the weather's lines, "How are things with
  you?" from `small_talk`'s, each taking turns as ever. Measured 2026-09-26: 14 of 14 lines of
  small talk read right, and 29 of 30 held out ("Good day at the forge?" was read as the weather);
  0.012 s a turn on the CPU. In the judged session it changed the two answers the judge had faulted
  as weather to "how are things", and no other. Without a `Meaning`, the pool takes turns.
- **The library** (2026-09-25): 11,411 lines in 43 files.
  - **Every archetype, at every alignment its heroes carry:** 196 banks of about 44 lines,
    8,618 lines in all. The pairs were counted over 38 generated worlds (sizes 33, 65 and 129),
    plus a true-neutral bank for every archetype.
  - **Each bank's voice comes from the public persona the generator composes for that pair:**
    manner from the card's pole notes, voice and stance from the alignment overlays, never-lines
    and the sample line. Nothing the card keeps back.
  - **The faced archetypes** (27 banks, 1,188 lines, for deceiver, false_hero, beast and
    fractured_heart, and `ancient`) were written before any of their heroes spoke from them.
    Since contract version 4 a beast's calm face and a fractured heart's first speak from their
    archetype's banks, and a mask's true face does once exposed; a mask still worn speaks from
    its trade's post bank, and `ancient`'s dreads have no key.
  - **Every roster post, and the faces heroes show:** 86 banks of 32 lines, in
    `posts_<group>.txt` by trade: 2,741 lines, and the gate captain's first 11 in `posts.txt`.
    The roster's own posts, from the gate guard to the astronomer; the five posts and five
    trades a hero who hides their true self shows a stranger (sovereign, champion, pretender,
    domain holder, magister; charcoal burner, hermit, shepherd, pilgrim, midwife); and the
    dread. A post's voice is its job: the plain work every holder of it does, never a
    particular deed, customer or stock.
  - **Not written, on purpose:** reeve, council, harbourmaster, castellan, warlord, keeper,
    heresiarch, founder and exile. Only heroes with one face hold them, and those always speak
    from their archetype's banks, so a post bank would never be said.
  - **The pilot's two posts and `any`:** 52 lines.
  - **Drafted by one hosted-model writer per archetype, and one per group of posts,** from a
    brief (the public personas, or the posts' workplaces) and a shared guide. Each writer ran
    `--check-lines` until every row was sound, then the rows were read for the rules no test
    holds: pet names, claimed deeds, one bank sounding like another, and an offer's opening
    that says the speaker stays behind (19 rewritten: a hero's offer may be "... with me").
- **Judged** blind, with the voice's rubric and a craft score, over the 24 voice cases. Each
  session judged its sets not knowing which was which (2026-09-24). Counts from one session
  compare; the same replies moved by a reply or two between sessions.

  | | Qwen3-4B | Written, first | Written, now |
  |---|---|---|---|
  | Replies with no fault, of 24 (lenient) | 9 (16) | 19 (21) | 23 (24) |
  | The 9 open-ended answers with no fault (lenient) | 2 (4) | 4 (6) | 8 (9) |
  | Invents a fact | 12 | 3 | 0 |
  | Contradicts the outcome | 3 | 0 | 0 |
  | Withholds an answer it had | 0 | 2 | 1, borderline |
  | Craft, 1 to 5 | 2 | 3 | 4 |

  That is the second session, which judged all three. The first judged the first written lines
  against Qwen3-4B alone: 19 (23) against 11 (16), craft 3 against 2.
  - **The first written lines** said less than the packet held. "Where am I" was never told
    Wenweg, "is it safe" got no nest, and "we'll talk of more" held other work back. The
    warden's "stays behind the line", from the card's belief, read as a rule. The judges also
    heard "Dornar the council", two hamlets of Wenweg that sounded like one, and word-for-word
    repeats.
  - **Now** the fullest lines are said, `{here}` names where the speaker stands, a place named
    twice is "another", and the eval hands a speaker their last reply. The judge heard "B's
    voice with B's flaws fixed". One miss was left: "is it safe" got the places near by, but not
    the deadliest one or any threat word. Since fixed in the engine: a dangerous place says so,
    and the deadliest is always named. The posts' judge (below) heard the dangers named in all
    ten sets.
  - **Qwen3-4B** invented directions, a temperament for a people, duties from the card, a
    deadline and a route. It rounded 13.6 km to 13, and called abandoned work "finished".
  - **The roster posts** (2026-09-25), judged blind with the same rubric plus fit. Ten banks
    were drawn at random, one from each writer's group and the dread. Each spoke the castellan's
    19 cases as a person of its post (`communicator_eval --post`).
    - 166 of 190 replies had no fault, and 187 leniently. 3 were clearly false, and none
      contradicted the outcome. Craft 3.9, fit 4.3.
    - A second judge matched all 10 sets to their posts, 9 of them from the voice alone.
    - Its faults, fixed after in 83 rows and not judged again:
      - offers and outcomes that said what the work was ("if you don't mind soot");
      - menace or pride that changed a fact (the dread calling the nest "mere weather", the
        sovereign's "I do not offer twice");
      - one stock line in six banks.
    - It read "the hamlet of Wenweg and the deeps, deadly, held by monsters" as both deadly. The
      engine now says the dangerous place first.

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
- **A map marker reaches no prompt and no grammar:** both are the same with it and without,
  even when the marker's id holds a barred word ("silver" from a mine).
- **How far is said in words, never in numbers:** "a short walk away", never kilometres.
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
- **Written lines, with no model:**
  - **Every line in `data/lines/` keeps the voice's rules,** all 11,411. Each row is tried alone
    against every test packet, as its bank's speaker. At least one packet must speak it, and
    every reply must hold its outcome's opening, sentence count and closing. It may name only
    what the packet names, begin only sentences with common capitals, and say no barred word but
    the packet's own. It may hold no unfilled blank, no double quote and no id. The line's own
    text may hold no capital but a sentence's first word, "I" and its outcome's opening: a name
    typed into a line fails even where a test packet holds it. A control shows each check
    failing a line that breaks its rule.
  - **Every test packet gets a reply from the library.**
  - **A folder of lines files loads as one library:** every `.txt` in it and nothing else. A
    folder with none is refused.
  - **A line speaks only when its packet fills every blank,** in exact words: no nest near the
    inn, so only the line without one is said.
  - **Of the lines that fit, the fullest are said.** The speaker's own name, post and place make
    no line fuller, and nor does a blank said twice.
  - **An optional part is left out whole when its blank is not filled,** and kept when it is.
  - **Where the speaker stands is named, and a place named twice is "another" the second time.**
  - **Asked what is around, a speaker names the worst of it:** a dangerous place says so, and the
    deadliest is named past the nearest three. With nothing dangerous about, the nearest three are
    named and nothing is called dangerous.
  - **A map marker changes no written reply.**
  - **A refusal that names two people fills `{giver}` with neither.**
  - **A speaker's own bank comes first,** then their post's, then anyone's. A bank whose lines
    the packet cannot fill gives way.
  - **An alignment with no bank speaks from its archetype's true-neutral bank,** before its
    post's.
  - **Each situation is read from the packet,** a refusal by its kind.
  - **Another's work is told as theirs,** by what it asks, and a council's member is "of the
    council".
  - **A world refusal says why,** in the director's words.
  - **The choice is steady and goes round with what the speaker has heard,** starts apart for
    each speaker, and never repeats the last reply while another fits.
  - **A cold speaker parts and chats coldly whatever their bank holds; no one met before is
    called a stranger; small talk says the weather's lines and the rest alike.**
  - **Small talk's subject** (the embedding model, `tests/embedder.hpp`): closeness is one number
    a text and nearer for the nearer meaning; ten lines of small talk read as the weather or the
    speaker's day; each answered on its subject, round its lines in turn; a cold speaker and a
    farewell unchanged by it; a model loaded for text refused.
  - **A bad lines file is refused:** a missing file, a row of two or four cells, an unknown
    situation or blank, an unclosed blank, an empty voice, and an optional part that is unclosed,
    nested, never opened, or holds no blank. A byte-order mark and CRLF are read.

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
- **Written lines: meaning past small talk.** Only small talk's subject follows the player's
  words; a greeting, thanks or a farewell takes turns whatever was said. Ranking a bank's own lines
  by closeness to the words read the subject right only 70% of the time (the closer half of four
  lines, where chance is 50%): the model scores a line's shape ("thank you for asking") as much as
  its subject. A subject needs a written description of its own, measured before it is used.
- **Written lines: sayings.** The lines read a people's sayings (`saying:<people>`, contract
  version 5); the sayings themselves are still being written.
- **Written lines that ask the player something.** 488 lines, outside `not_understood`, put a
  question to the player ("Are you hurt?", "And you are?"). The interpreter has no act for an
  answer to the speaker's question, so a player who answers is told "I don't follow". Either
  the lines give up their questions, or the interpreter gains such an act: the owner's call.
- **Written lines: what was asked, within a situation.** "Is it safe?" and "What is around?" are
  both `surroundings` and draw on the same lines.
