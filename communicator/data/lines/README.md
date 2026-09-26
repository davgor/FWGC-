# Written lines

The lines speakers answer in, read by `communicator/include/communicator/authored.hpp`
(`Lines::load` reads every `.txt` here, in name order). The communicator README's "Written
lines" says how they are chosen.

One file per hero archetype, every alignment's bank in it. `posts.txt` holds the pilot's two
posts and `posts_<group>.txt` the rest of the roster's, grouped by trade; `any.txt` is everyone's
fallback.

## Format

```
voice | situation | line
```

- **voice:**
  - a hero's archetype and alignment, as the director's `voice_key` gives it (`warden/LN`);
  - `post:<post>` for a roster post;
  - `any` for everyone.

  A speaker answers from their own bank first, then their archetype's `NN` bank when their
  alignment has none, then their post's, then `any`.
- **situation:** one of `Lines::situations()`, each with its blanks:

  | Situation | Blanks |
  |---|---|
  | `about_self` | |
  | `about_person` | `{name}` `{role}` `{where}` |
  | `about_place` | `{about}` `{trades}` `{who}` `{around}` `{work}` |
  | `surroundings` | `{here}` `{nearest}` `{others}` `{danger}` |
  | `work_own` | `{own}` `{others}` |
  | `work_others` | `{others}` |
  | `work_none` | |
  | `unknown` | |
  | `refused_give` | `{giver}` |
  | `refused_release` | `{giver}` |
  | `refused_gone` | |
  | `refused` | `{reason}` |
  | `accepted`, `declined`, `abandoned`, `parted`, `not_understood` | |
  | `parted_cold` (a wary or hostile speaker, seeing the stranger off) | |
  | `greeted` (a first meeting) | |
  | `greeted_again`, `greeted_warm` (met before; warm: the speaker likes them) | `{since}` `{asked}` |
  | `greeted_cold` (the speaker is wary or hostile, met before or not) | |
  | `greeted_taken`, `greeted_finished`, `greeted_failed`, `greeted_abandoned` (the speaker's own work, taken and not done, finished, failed, given up) | `{since}` |
  | `small_talk`, `small_talk_fair`, `small_talk_wet`, `small_talk_storm` (the weather where the speaker stands) | `{season}` `{time}` `{danger}` |
  | `small_talk_cold` (a wary or hostile speaker, asked for small talk) | |
  | `acknowledged` (the stranger answered the speaker's question), `thanked`, `thanked_cold`, `offended`, `offended_again` | |
  | `safety_unsafe`, `safety_wary`, `safety_safe` ("is it safe", by the verdict) | `{worst}` `{others}` `{danger}` |
  | `exposed`, `exposed_true`, `exposed_unleashed`, `exposed_second` (a hidden face owned to) | `{was}` `{is}` |
  | `denied`, `denied_again` (accused of hiding something, with nothing owned to) | |
  | `forgiven`, `forgiven_cold` (an apology for a wrong, taken, or said again once every wrong is forgiven; cold: the speaker is still wary or hostile after it) | |
  | `unwronged` (an apology from someone who never wronged the speaker, or to a face that does not know them) | |

  Every situation may also use `{speaker}`, `{post}`, `{place}` and `{saying}`.

- **Sayings** (contract version 5): a bank `saying:<people>` (`saying:human_cold`) holds that
  people's sayings, one per row, in the situation `saying`. `{saying}` is one of the speaker's own
  people's, the same through a turn: "Go well[; as we say, {saying}]." A saying opens in lower
  case and has no stop of its own, so it fits mid-sentence, and takes a capital where it starts
  one ("[ {saying}.]"). Put `{saying}` in brackets: a people with no bank leaves the part out.
  `mixed` has none. A saying ends its sentence: its stop comes straight after it, or after the
  bracket that closes its part. `sayings.txt` holds twelve peoples' banks, 24 sayings each. A saying's first
  word takes a capital where it opens a sentence, so it is one a sentence may begin with
  (`common_capitals()`: "the", "no", "never"...), and a saying is a general truth, never a fact
  of the world.

  A packet's situations come closest first, and a bank answers from the first it has a line
  for: a warm greeting to someone met before is `greeted_warm`, then `greeted_again`; what came
  of the speaker's own work comes before either. `offended_again` and `denied_again`, for the
  second time, fall back to `offended` and `denied`; `exposed_true` and the rest fall back to
  `exposed`.
  - A cold speaker has only `greeted_cold`, `small_talk_cold`, `thanked_cold`, `parted_cold` and
    `forgiven_cold`: a bank without them leaves the reply to the next bank's, and never answers
    warmly.
  - Small talk says the weather's lines (`small_talk_fair`, `_wet`, `_storm`) and `small_talk`'s
    alike: which fits is for the stranger's words to say. So `small_talk` says nothing of the
    weather: it is the speaker's day, their work, the season, what is near.
  - `{since}` is how long since they met, roughly: "earlier today", "yesterday", "a few days
    ago", "some weeks ago", "some months ago", "years ago".
  - `{asked}` is the place or person the stranger asked about last time, by name; never the
    town the speaker stands in, which the stranger is in again.
  - `{season}` is "deep winter" to "high summer"; `{time}` "dawn" to "night".
  - A place asked about (contract version 8): `{about}` is the place itself; `{trades}` up to
    three of a settlement's trades ("an administrator, a smith and an officiant"); `{who}` the
    heroes who stand there, one or two ("Tornar of the council", "Froswen the warden and Legmok
    of the council"), so never "{who} is"; `{around}` the nests within 2 km of it, from
    it ("Frost owls nesting close by it"); `{work}` the work there the speaker knows of, a
    sentence of its own ("I want someone to ..." for the speaker's own, "Tornar of the council
    wants someone to ..." for another's). Each goes in its own bracketed sentence, so a place
    with less to it is said with less.
  - `{worst}` is the place the verdict rests on: within a walk when anything is. `{was}` and
    `{is}` are the roles the face known claimed and the face owned to: they differ only for a
    mask. A beast's or a fractured heart's two faces share a role: its lines say what it is
    without them.
  - Asked the same thing twice in one conversation, a speaker's answer is said after "As I
    said," (C++'s words): the line needs no repeat of its own.
- **line:** a line with a blank its packet cannot fill is not said.
- **`[optional parts]`:** a part in brackets is left out whole when the packet cannot fill a
  blank in it, so one line serves a packet with a nest and one without. Put the space before a
  part inside its brackets: `The nearest is {nearest}.[ Mind the {danger}.]`
- **The fullest lines are said.** Of the lines that fit, only those that say the most of the
  packet are said. Write lines for what a full packet holds, and shorter ones for packets that
  hold less. `{speaker}`, `{post}`, `{place}`, `{saying}`, `{since}`, `{asked}`, `{season}` and
  `{time}` do not count: they are there to say whenever their situation is, so a line saying one
  silences no sibling, and a greeting recalls what was asked now and then, not every visit. Nor
  does `{danger}` in small talk: a nest near is something to mention, not something every word of
  small talk must.
- **Each time, the next line.** A speaker starts at a line of their own among those said, and
  says the next each time they have heard the same thing before (the Memory fact's `heard`), so a
  farewell said five times is said five ways while the bank has five. Write as many lines for a
  situation as it will be met: greetings and farewells every visit.
- **A nest is mentioned in passing once.** Small talk leaves out an optional part that would warn
  of a nest the speaker has warned of lately (the game keeps what they said lately); an answer
  about what is around, or "is it safe", says it whatever was said before.
- **Someone met before is no stranger.** A line that addresses the stranger as one ("Hello,
  stranger", "Go well, good stranger") is not said to someone met before; the next line is.
  "Don't be a stranger" and the word's other uses stay.

## Rules a test holds every row to

`every_line_is_spoken_and_keeps_the_voices_rules` tries each row alone against every test
packet, and a row that says a saying once more with one dropped in. `communicator_eval
--check-lines PATH` runs the same check on a file or a folder.
- A sentence starts with a word from `common_capitals()`, or with a blank. Not with `{post}` or
  `{role}`: the sentence's capital makes "castellan" read as a name the packet did not give.
- Past a sentence's start, a capital is "I" or a blank's.
- No barred word (`forbidden_words()`: compass points, money and pay), and no double quote.
- An outcome that is not an answer opens with its outcome's words (`reply_shape.cpp`) and is one
  sentence; `accepted` may run to two. A refusal's last sentence ("Speak to ...") is added by
  C++: after a line that says `{giver}`, it says only where ("Find them at ...").
- Conversation: a greeting or an acknowledgement runs to two sentences, small talk to two with
  any start, thanks, an insult taken and a denial to one, an exposure to three. An apology taken
  runs to two and opens "Very well", "All right", "Apology accepted", "Let's forget it",
  "Forgiven" or "No hard feelings", or from a speaker still cold "Words are cheap", "We'll see",
  "Maybe", "Hmph" or "It's a start"; nothing to forgive runs to two ("What for? You've done me no
  wrong.") and opens "What for", "No harm done", "There's nothing to forgive", "Nothing to
  forgive" or "Whatever for". A greeting opens
  by what the speaker remembers: a first meeting "Hello", "Greetings", "Well met", "Good day",
  "Welcome" or "Hail"; met before, "Hello again", "Back again", "Welcome back", "Good to see
  you", "Well met again" or "Good day"; a cold speaker "What do you want", "What now", "Keep
  back" or "Stay back". "Is it safe" opens with its verdict: unsafe "No", "Not here", "Not
  safe", "Far from it", "I wouldn't say so"; wary "Safe enough", "Mostly", "More or less",
  "Careful", "Keep your wits about you"; safe "Safe enough", "Yes", "Aye", "It's safe", "Quiet
  enough", "Nothing to fear". `reply_shape.cpp` holds every opening.
- An answer runs to three sentences at most, and an answer about a place to five: the place,
  its trades, who is there, the nests around it and the work there.
- An article fits the word a blank fills in after it: "a {time}" says "a evening", "an {post}"
  "an farmer". `{season}` may be "the turn of the season", so nothing that needs a noun of its own
  stands before it ("this {season}", "every {season}").
- Some test packet speaks the row.

## Rules no test can hold

- **A line says only what its blanks hold.** It carries manner, never a fact of its own: no
  place, creature, duty, rule, time or distance outside a blank.
- **Draw a voice from a card's public part** (manner, voice, stance, sample line, never), never
  from what the card keeps back (belief, wants, fears, tells, lies). A stranger never hears those.
- **Never name the archetype or a title.** `{post}` says what the speaker does.
- **A post's voice is its job.** The packet holds the post, so the plain work every holder of it
  does is fair ("I've a gate to watch"). A particular deed, event, rule, customer, stock or
  rumour is invented.
- **An offer's opening never says who goes.** A hero's offer may put them alongside ("hold the
  fortress above Wenweg with me"), so "I can't leave my desk: {own}" would contradict it.
- **A forgiveness never names the wrong.** It may be an insult or the speaker's own work given
  up, and the line cannot know which: no "what you said", no "the work".
- **A place line carries every blank it can.** Of the lines that fit, the fullest are said, so an
  `about_place` line without `{trades}`, `{who}`, `{around}` or `{work}` is never heard where that
  blank is filled. Nothing in it may hold only when a part is dropped or kept ("That's all I
  know", "a quiet place").
