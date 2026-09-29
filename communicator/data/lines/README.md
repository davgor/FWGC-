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
  | `surroundings_told`, `safety_unsafe_told`, `safety_wary_told`, `safety_safe_told`, `about_place_told` (the answer asked again when the stranger heard all of it from the speaker lately) | as the answer's |
  | `work_own_told`, `work_others_told` (asked for work again with nothing new to it; contract version 13) | as the answer's |
  | `work_more` (asked for work again after hearing the speaker's own: the others' work; contract version 16) | `{others}` |
  | `them_who` (asked who they are; contract version 13) | `{known_as}` `{home}` `{after}` `{lost}` `{feat}` |
  | `them_past` (their past) | `{done}` `{lost}` |
  | `them_after` (what they are after, why it matters) | `{after}` `{danger}` `{home}` |
  | `them_need`, `them_stakes` (why they need help; what comes if the work fails) | `{after}` `{danger}` |
  | `them_here` (their post, their days) | `{standing}` `{done}` |
  | `them_long` (how long they have been here) | `{standing}` |
  | `them_ties` (friends, allies, rivals) | `{ties}` |
  | `them_plans` (whether they would leave, what comes next) | `{after}` `{standing}` `{danger}` `{away}` `{home_here}` |
  | `them_secret` (what they fear or hide) | |
  | `them_home` (where home is: where they stand, or somewhere else) | `{home_here}` `{away}` |
  | `them_foes` (who stands against them: their rivals), `them_foes_none` (the same, with none on record: never an ally in their place) | `{foes}` ("Ulknord is my rival", a whole clause), `{danger}`; `them_foes_none`: `{danger}` |
  | `acknowledged_hardship`, `acknowledged_grief`, `acknowledged_deed`, `acknowledged_plan`, `acknowledged_kin` (what the stranger told of themself, by what it is) and `acknowledged_hardship_work`, `acknowledged_grief_work`, `acknowledged_deed_work` (in the light of the speaker's own work) | |
  | `acknowledged_doubt` (the stranger's doubt of themself: untried, afraid of failing), `acknowledged_doubt_work` (told while the speaker's work is offered or taken), `acknowledged_doubt_done` (told once it is done: fear they stood through) | |
  | `small_talk_work_done`, `small_talk_work_done_beside`, `small_talk_since_work` (the speaker's work the stranger finished: how it went, how it went when the speaker was there, how things have been since) | `small_talk_since_work`: `{danger}`, the worst nest near by name alone ("the {danger} are still about") |
  | `greeted_finished_beside` (back with the speaker's work done where they stand, beside them; contract version 14) | `{since}` |
  | `thanked_work` (thanked for their own work while it is still to do), `offended_complaint` (a complaint about their work), `unwronged_state` (an apology for how the stranger is: never opened with a question, "Whatever for?", which took a grieving stranger's state as having no cause), `forgiven_state` (the same, once there is a wrong to forgive) | |
  | `parted_work` (a farewell to someone with the speaker's work to do: luck, never what the work is like or who goes) | |
  | `parted_returned` (a farewell that wishes the speaker well, wished back: "Take care, and the same to you") | |
  | `heard` (a complaint about the speaker or their work, heard out: contract version 18's `complain` and `heard`; opens as fair, "That's fair", "Maybe so", "I hear you", never a rebuke) | |
  | `parted_work_beside` (a farewell with the speaker's own work to do beside them, contract version 14's `beside`: luck to them both, since the work was offered "with me") | |
  | `them_past_none` (asked about their past with none on record: no city lost, no deed but the post they keep, nowhere served before it; nothing worth the telling, never a past made up) | |
  | `them_long_known` (how long a castellan has held their post, on record since contract version 19) | `{held_for}` ("ten years and more", after "for" or "held it"), `{standing}` |
  | `them_past_served` (where a castellan served before their post, contract version 19) | `{served}` (after "I": "served as a sergeant in the garrison of Iskmor"), `{done}`, `{lost}` |
  | `unwronged_state_grief`, `unwronged_state_hardship`, `forgiven_state_grief`, `forgiven_state_hardship` (an apology for how the stranger is, in the light of a loss or a hardship they told earlier in this conversation; the outcome's openings, never a question; contract version 23) | |
  | `asked_again` (a question of the speaker's the stranger never answered, asked again after a greeting in a later conversation: a sentence that says so, then `{question}` as its own sentence, which must end the line; contract version 21) | `{question}` (the speaker's own question, whole, with its capital and its question mark) |
  | `account` (how the speaker's own work went, done beside them, said after the reply: one sentence; contract version 20), `account_heard` (the same, done out of their sight) | `{account}` (a whole clause list after a colon: "Abyssal krakens came at us, more than I'd have liked; we lost a few good people, and you held when it counted"; it may open with a creature's name or with "we", so it never opens a line) |
  | `recalled_grief`, `recalled_hardship`, `recalled_doubt` (the stranger's words said back as what they were: a loss, a hardship, fear they came through with the speaker's work done; one sentence, the recalled rules, and `recalled_hardship` alone may ask, "is it any easier now?") | `{words}` |
  | `greeted_who` (said after a first greeting that asks who they are) | |

  Every situation may also use `{speaker}`, `{post}` and `{place}`.

- **No line says a saying** (the owner, 2026-09-28: "Drop em, willing to bet it gets weird"): after a
  farewell four blind judges of five heard one as a non sequitur or a rebuke, and after small talk both
  that the hero benchmark reached were faulted. The 290 rows that had one keep the rest of their line,
  and the rules check refuses `{saying}` in any row. The peoples' banks and the blank stay, as below,
  for a use that fits.

- **Sayings** (contract version 5): a bank `saying:<people>` (`saying:human_cold`) holds that
  people's sayings, one per row, in the situation `saying`. `{saying}` is one of the speaker's own
  people's, the same through a turn: "Go well[; as we say, {saying}]." A saying opens in lower
  case and has no stop of its own, so it fits mid-sentence, and takes a capital where it starts
  one ("[ {saying}.]"). Put `{saying}` in brackets: a people with no bank leaves the part out.
  `mixed` has none. A saying ends its sentence: its stop comes straight after it, or after the
  bracket that closes its part. `sayings.txt` holds twelve peoples' banks, 24 sayings each. A saying's first
  word takes a capital where it opens a sentence, so it is one a sentence may begin with
  (`common_capitals()`: "the", "no", "never"...), and a saying is a general truth, never a fact
  of the world. No saying after a farewell (`parted`, `parted_cold`): blind judges of four hero
  sessions of five heard one there as a non sequitur or a rebuke ("Safe travels; as we say, hold your
  tongue on thin ice", 2026-09-28). A saying that speaks of strangers or guests is never chosen for a
  speaker who is not cold to the stranger: said to one, "if a stranger smiles, watch their hands" is a
  jab (the engine skips it; the row may stay).

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
  - **Said again, told** (2026-09-26): an answer (what is around, "is it safe", a place) whose
    every place, nest, person and piece of work the stranger heard from the speaker lately (what
    the game keeps of their replies) is said from its `_told` situation, and only from it: a bank
    without told lines leaves the answer to the next bank's, as a cold speaker's greeting does.
    A told line says so itself ("As I told you, ...", "Nothing's changed since I told you: ..."),
    so no "As I said," goes before it. Its blanks make no line fuller: a short reminder takes its
    turn with a long one. An answer with anything new in it is the answer, not told. What is
    around and "is it safe" are told only when the stranger asked the same before; a place,
    whenever all of it was said, so `about_place_told` says the stranger has had all the speaker
    knows of it, never that it was described. Put every blank but `{about}` in brackets. "Same" is
    not a word a sentence may begin with: "It's the same as I told you" is.
  - A told line tells less: its places by name (`{nearest}`, `{worst}`: "the silent mouth", with
    no how far or how dangerous) and its nests by kind (`{danger}`, `{around}`: "Glacier
    dragons"). `{about}` stays whole, and so do `{here}`, `{others}`, `{trades}`, `{who}` and
    `{work}`.
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
  situation as it will be met: greetings and farewells every visit. A line said lately is not
  said again while another fits, and a line is the same line whatever its passing blanks say
  (`{since}`, `{asked}`, `{season}`, `{time}`, `{saying}`): "I saw you a few days ago" and "I saw
  you some weeks ago" are one greeting.
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
- An opening is whole words: "No" opens "No, never." but not "Nothing to worry about".
- A frightened speaker (contract version 9): greetings (`greeted_afraid`, `greeted_panicked`) run
  to two sentences and open "Oh", "Please", "Stay back", "Who's there", "Wait", "Thank goodness"
  or "Hello"; small talk (`small_talk_afraid`, `small_talk_panicked`) to two with any start;
  thanks (`thanked_*`) to one, opening "Yes", "Of course", "Please", "Go" or "Don't thank me";
  farewells (`parted_*`) to one, opening "Go", "Be careful", "Stay safe", "Please", "Wait" or
  "Goodbye". A lead (`afraid_lead`, `panicked_lead`), said before an answer, is one sentence, no
  question, and neither opens nor ends with yes, no, aye or nay: it goes before "I've no work" as
  readily as before a list of places. It never goes before "is it safe", whose verdict opens its
  answer: "Forgive me, I'm shaking. Far from it." heard as a denial of the shaking. Calming words taken (`calmed`) run to two and open "All right", "Thank you", "I'll
  try", "Maybe you're right", "Yes" or "I know, I know"; not taken (`still_afraid`) to two,
  opening "No", "I can't", "Not yet", "Easy for you to say", "You don't understand", "Stay back"
  or "Keep away"; nothing to ease (`not_afraid`) to one, opening "I'm not afraid", "I'm fine",
  "I'm all right", "There's no need" or "Who said I was afraid".
- Talked down lately (contract version 10): a greeting (`greeted_eased`) runs to two and opens as
  any greeting to someone met again does ("Hello again", "Back again", "Welcome back", "Good to see
  you", "Well met again", "Good day"); nothing left to ease (`not_afraid_eased`) runs to one and
  opens "I'm fine", "I'm all right" or "There's no need", never "I'm not afraid". A bystander's
  lead (`joined_lead`) has a lead's shape: one sentence, no question, not opening or ending with
  yes, no, aye or nay.
- News (contract version 11), said after the reply, a sentence of its own: what the stranger did
  (`heard_deed`, `{deed}` the words after "you": "helped Wenweg hold"), after a greeting, small talk
  or an answer; what others did, after small talk or an answer about a place or a person (`news`,
  `{news}`: "Halbrann wants someone to stand with Wenweg when they come for it", "Orlan beat off
  the threat here"). One sentence; it may ask ("aren't you?"), since nothing comes after it. No
  opening of its own: it follows the reply's, which may be any of those. Either is also said
  alone, to small talk that asks for news.
- Small talk that asks for news ("Any news?", read by meaning), told lately: others' news said as
  told (`news_told`: "As I said, {news}."), and the stranger's own deed as the talk of it
  (`heard_deed_told`). None heard: `no_news` ("Nothing's reached me."). One sentence, as small
  talk.
- The stranger's own words, kept (contract version 12), said after a greeting to someone met
  before when there is no news (`recalled`, `{words}`: the words turned to the stranger, and what
  was under way shifted back, "your sister keeps the mill at the ford", "you were headed for the
  coast", "to the coast, by the high road"). One sentence, no opening of its own, heard straight
  after one of the bank's met-before greetings. `{words}` comes after a colon, never "that" or a
  sentence of its own, since it may be a fragment; the line says no when ("last time", "the other
  day", "yesterday"), since the words may have been said several visits back; and it is never a
  question, which would ask after what the words do not hold. The rules test holds these, and tries
  every row with a clause, a fragment and words opening with a name. What no test holds: nothing
  before the colon may read as the greeting's or the trade's own things (an armorer's "I've kept it
  oiled against the rust:" was heard as the mail), and no pronoun after the words may point inside
  them ("I have not let it fall", after a mill).
- An answer runs to three sentences at most, and an answer about a place to five: the place,
  its trades, who is there, the nests around it and the work there.
- An article fits the word a blank fills in after it: "a {time}" says "a evening", "an {post}"
  "an farmer". `{season}` may be "the turn of the season", so nothing that needs a noun of its own
  stands before it ("this {season}", "every {season}").
- No claim that no packet can back (`truth.hpp`, `never_backed()`): no greeting sent to someone
  named, and no news asked of them ("give them my regards", "tell them I sent you", "don't say I
  sent you", "tell me how they're keeping"); no distance or time counted out ("three days", "a
  day's march", "in a few years"). "One day", "on your own two feet" and "all in a day's work"
  count nothing.
- Some test packet speaks the row.

The rest of the truth check is held where a line is said: a line that says "welcome back" is not
said to a stranger, nor rain on a clear day, nor "dangerous" of a nest of three, nor "you steadied
me" to someone the stranger never talked down, nor "I couldn't help overhearing" by someone asked
outright, and the next line is. `--check-lines` lists those as "(unsaid for this packet)". A line said only where it is true
is fine; one that no packet the game gives can make true is never heard.

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
- **Knowing of someone is not knowing them.** A line about the people a packet names (`{who}`,
  `{giver}`, `{name}`) points the stranger to them; it never sends greetings ("give them my
  regards", "tell them I said hello"), asks after them or asks for news of them ("tell me how
  they're keeping"), since no packet says the speaker knows them. Good-hearted banks drift into
  this: 33 place lines in one round and 26 told lines in the next (2026-09-26).
- **Frightened words never say what frightened them,** where it is, what it did, or whether it is
  still there: the packet does not say. Fear shows in how they speak, through their own life (a
  baker's hands that will not knead). Afraid is shaken but whole; panicking is broken, repeated,
  pleading; a proud bank stays terse. A frightened line is said whether or not the speaker has met
  the player, and whatever they think of them, hostile after an insult too: no "again", no
  "stranger", no warmth ("a kind voice"), only relief at a voice ("Thank goodness"). `calmed` is
  a step down, never all better ("I'll try", never "the danger's passed"); `still_afraid` never
  says why, since it may be the danger still there or distrust of the one soothing them.
- **Talked down lately, the speaker owns it:** `greeted_eased` and `not_afraid_eased` say they are
  steadier or all right now, and may thank the stranger for it. They never say what frightened them
  or that it has gone, and never deny the fear. The stranger may have talked them down yesterday,
  so no "just now".
- **A bystander who joins says only that they overheard** (`joined_lead`): the stranger asking, and
  the answer to come. Never what the one asked said or could not say, nor who that was: the packet
  names neither. No acquaintance with either of them, and no "again".
- **News is the blank's, never the line's.** `{deed}` and `{news}` hold all that happened, in C++'s
  words, and when ("helped us hold a few days back"); a line says only how the speaker takes it
  ("You're the one who {deed}, aren't you?", "The word is {news}."), and never a time of its own
  ("just yesterday"), which would say it twice or say it wrong. Never what the speaker thinks it means, who else was there, or what comes of
  it: the packet says none of that. `{news}` may open with a name or with "you", so it never opens
  a sentence and never follows "you" ("They say you {news}" says "you you"); `{deed}` is what the
  stranger did, so it follows "you", "who" or "you that", with nothing between ("you would {deed}"
  says "you would helped"). The rules test holds both. A line opens with a word sentences begin
  with (`common_capitals()`): "Word is" and "Folk are saying" do not. News may be the stranger's
  own failure ("you failed Halbrann"), so a line takes it evenly: no praise, no scolding. And it
  follows the speaker's own reply: a line that says again what the bank's greetings or small talk
  say ("don't hold up the line" after "don't hold up the line") echoes (a writer found 25 in their
  own first draft, 2026-09-27).
- **The stranger's words are theirs** (`recalled`): the line adds only how the speaker takes having
  kept them. No fact about them, no question after them ("is she well?"), no judgement of them ("a
  fine thing", "I'm sorry for it"): they may be anything, a loss, a plan, a boast. A tail after them
  names its own subject ("I keep what I'm told"), never "it", which the player's last noun takes
  ("...the mill at the ford, and it's kept"). It follows the greeting, so it neither echoes it nor
  says what a greeting denies ("I know the face; the rest you'll have to tell me" had to go: seven
  writers, 2026-09-27).
- **Told news is said as told** (`news_told`): no more than the speaker said, and nothing new
  about it. **The stranger's deed, told lately** (`heard_deed_told`), is the talk of it, never the
  deed read back as fact: the stranger never said it was them ("Only the talk of what you did, and
  you'd tell that better than me."). **No news** (`no_news`) is only that: not what the speaker
  thinks of it, and nothing that says the place is quiet or safe, which the packet does not say.
- **What a hero says of themself is the blanks'** (`them_*`, contract version 13): `{after}` is a
  verb phrase after "to", `{standing}`, `{lost}`, `{feat}` and `{done}` follow "I", `{ties}` is a
  whole clause that may hold only allies or only rivals, `{danger}` a plural creature after "the".
  How long they have been anywhere is never a time (`them_long`): the world does not record it. A
  castellan's years at their post are `{held_for}` (`them_long_known`), C++'s words from their record,
  and never a count of the line's own.
  What they hide stays hidden without being named (`them_secret`): no packet holds it. What comes if
  the work fails is only what the blanks say (`them_stakes`, `them_need`): no burning city of the
  line's own.
- **What the stranger tells is answered in proportion** (`acknowledged_*`): a death with
  condolence, never "that hurt"; a hurt in the speaker's service with the speaker's own debt. The
  line never repeats or guesses the details, which it does not know: a question back must fit any
  such telling ("What was their name?", "Is it mending?").
- **Done beside them is spoken as by one who was there** (`_beside`): never "by the look of you".
- **Who they are, after a greeting, never says the guess was right** (`greeted_who`): the program
  does not know what the stranger guessed.
- **A place line carries every blank it can.** Of the lines that fit, the fullest are said, so an
  `about_place` line without `{trades}`, `{who}`, `{around}` or `{work}` is never heard where that
  blank is filled. Nothing in it may hold only when a part is dropped or kept ("That's all I
  know", "a quiet place").
