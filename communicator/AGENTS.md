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
- **An embedding model only compares** (`meaning.hpp`): it turns the player's words and written
  texts into vectors, and C++ takes the closest of a few written options. It never writes what a
  speaker says, and no id reaches it. A new subject it chooses between comes with its written
  description and a measure on lines held out from tuning; ranking a bank's own lines by
  closeness read small talk's subject right only 70% of the time.
- **No id reaches a model.** A model sees labels; C++ maps a label back to its id. A test checks
  every prompt and grammar for ids.
- **The voice names only what its packet names.** Nothing that can be a name goes into
  `common_capitals()`: only words sentences begin with. The grammar lets them begin a sentence
  and stand nowhere else; past a sentence's start, a capital is the packet's own, or "I".
- **The voice gives no bearing and speaks of no pay, in the words of `forbidden_words()`, and
  counts out no distance or time, in the words of `counting_words()`.**
  - A packet's own word stays sayable, and only that exact word.
  - An entry is a stem (`north*`) only when every word it begins should be barred too.
    Otherwise it goes in whole, with its forms.
  - No common capital may be a barred word. A counting word may begin a sentence ("Two of them,
    then."), since every unit and plural time that could count after it is barred.
  - `counting_words()` binds the voice alone: written lines keep their idioms ("most days", "the
    two of us"), and the truth check holds them to no counted distance or time.
- **What a reply must show goes in what just happened, not only in what the speaker remembers.**
  Every model tried followed the instruction over the memory (2026-09-26): told "Answer in kind"
  beside "You dislike them", each chatted cordially. A state the words must show (dislike, a
  talk-down remembered, a join) is said in the instruction, and where the start can hold it, the
  grammar holds it too (`ReplyShape::stance`).
- **A written line keeps the voice's rules and says only what its blanks hold.**
  - `every_line_is_spoken_and_keeps_the_voices_rules` holds every row of `data/lines/` to the
    voice's rules. A row that breaks one fails by its own words. Run
    `communicator_eval --check-lines` on new rows before a commit does.
  - A bank sounds like its own archetype and alignment, not like its neighbours, and says how
    the speaker stands, never what they have done. A pet name at most once in a bank, and only
    as contempt or command: repeated, it is a tic.
  - A post's bank speaks from the job: the plain work every holder of it does is fair ("I've a
    gate to watch"). A particular deed, customer, stock or rumour is invented.
  - An offer, and what is said when it is taken, turned down or given up, never says what the
    work is like or who goes. `{own}` may be any verb, and a hero's offer may be "... with me":
    a judge counted "if you don't mind soot" and "I can't leave my desk" against it. Asking for a
    quality says what the work is like too ("if you've the nerve", "someone steady on their
    feet"), and wishing the stranger back says who goes ("come back in one piece"): a second
    judge found both in the rows that fixed the first judge's.
  - A speaker remembers only through the packet: their situation (met before, liked, cold, what
    came of their own work) and its blanks (`{since}`, `{asked}`). A line never invents a shared
    past ("you saved my brother", "you still owe me").
  - A blank that fills in lower case never opens a sentence: `{post}`, `{role}`, `{time}`,
    `{season}`, `{was}`, `{is}`.
  - A line adds manner, never a fact. A place, a creature, a duty or a distance comes only
    through a blank, and a blank only from the packet.
  - Draw a voice from a card's public part (manner, voice, stance, sample line, never), never
    from what the card keeps back (belief, wants, fears, tells, lies). The first warden lines
    took "stays behind the line" from the warden's belief, and a judge heard it as a rule.
  - A new situation or blank goes in `Lines::situations()`, with a test packet that speaks it.
  - A frightened speaker's lines never say what frightened them, where it is, or whether it is
    still there: the packet does not say. Fear shows in how they speak.
  - No saying in a line: four blind judges of five heard one after a farewell as a non sequitur or a
    rebuke, and after small talk both the hero benchmark reached were faulted; the owner, 2026-09-28:
    "Drop em". The rules check refuses `{saying}` in any row.
  - No question after an apology for how the stranger is (`unwronged_state`): "Whatever for?", two
    turns after a brother's death, took the stranger's state as having no cause (2026-09-28). The
    rules check refuses it.
- **News is said in C++'s words** (`src/news.hpp`, contract version 11): what the stranger did,
  and what happened, written from a News fact's own fields. A written line's `{deed}` and `{news}`
  hold them whole, and the voice is shown them as they are; neither adds what happened, who else
  was there, or what comes of it. A new event kind or state the director adds comes with its words
  there and a test of them, or is left unsaid. Nothing is said of the speaker's own work or of
  themself, which their own lines say. Which of a packet's News facts may be passed on is
  `may_pass_on`'s to say, and which one is, `news_to_pass_on`'s: the lines, their blanks and the
  voice all go by them, and anything new that says news does too. With two choosers, a line was
  chosen for one piece of news and its blank filled with another, and the voice, shown news the
  lines skip, gave work to the wrong giver (2026-09-27). What the speaker knows stays the
  director's: these only choose among it. What the stranger did is said to them as their deed
  (`heard_deed`), never handed to them as news, and never read back as fact once told: they never
  said it was them.
- **What a speaker says of themself is said in C++'s words** (`src/them.hpp`, contract version 13):
  what they are after, how they stand, a city lost, a deed, their ties, written from the Self, Deed
  and Person facts' own fields. A written line's `them_*` blanks hold them whole, and a field the
  words do not know says nothing. The card's secrets are in no packet: `them_secret` lines keep back
  in character without naming what. How long they have been anywhere is never a time (`them_long`),
  since the world does not record it; a castellan's years at their post, which it does (contract
  version 19), are said only as C++'s `{held_for}` (`them_long_known`), which the truth check reads as
  the Self fact's own and no other count of years. What they stand against (`{danger}` in `them_*`) is named
  without how far, which small talk and "is it safe" say: said with it, one conversation heard the
  same nest four times.
- **The player's own words come back only as C++ turns them** (contract version 12): `{words}` is
  the kept words said back to the stranger (`text::said_back`), and a `recalled` line adds only how
  the speaker takes having kept them: no fact about them, no question after them (but "is it any
  easier now?" after a hardship, `recalled_hardship`, which fits any), no judgement of
  them, no claim they are exact, no when, and no praise of the speaker's own memory or listening ("I've
  a good memory for what folk tell me", "you got an ear": three blind judges heard a boast,
  2026-09-28). Before the words begin it says the stranger told them
  ("what you told me", "your words", "from you"): after "If I may:" or "What I'm told:", "you'd be
  back before the frost" is heard as the speaker's own word, and `recalled_problems` holds it. Read a
  row straight after the greetings it follows, since a pronoun or trade noun before the colon takes
  theirs. The words are never said back after a greeting that thanks the stranger for easing the
  speaker, nor one for work the stranger holds, failed or gave up, nor the one that thanks them for
  work finished while that is news between them (`says_back`, contract version 17). Words that speak to
  someone are never said back, nor words of "we": whether the speaker was one of them the words do not
  say, and "you lost two good people" handed a shared loss back (two judges, 2026-09-28). "As I told
  you" before an answer is C++'s, dated by the Recall fact.
- **The summarizer's answer is held by a grammar** (`summarizer.hpp`): a quote only as a run of the
  stranger's own whole words within one sentence, never a question; the rest in lower case and the
  log's own capitalised words. The director checks it again; a change to what may be quoted comes
  with `communicator_eval --summarize` before and after.
- **Every reply passes the truth check before it is shown** (`truth.hpp`, `untrue()`).
  `Lines::speak` checks written lines as it chooses them, and says the next line when one fails;
  whatever joins the communicator to the game checks a voice reply and shows the written line
  instead. A new claim a line or the voice can make (a new blank, situation or fact) comes with its
  check in `untrue()`, shown backed by one packet and unbacked by another, and a run of
  `--check-lines` over the library. A phrase list reads what claims nothing too ("I'll tell you
  again" is not "you again"): the first pass flagged 105 sound rows, so narrow it with a test for
  each wrong read.
- **An act is offered to the model only where it can apply:** an answer after a question, calming
  words to a frightened speaker, a complaint where the speaker's own work is between them (contract
  version 18). A listed act costs a small model on every line: offered to everyone, `calm` took the
  test model from 112 lines of 310 read right to 79 (2026-09-26).
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
