# FWGC++ orchestrator

The director: deterministic C++ that holds one world and changes it only through the
generator's logged verbs. It answers a player's dialogue acts as **fact packets**: what the
person spoken to knows and would say, what they remember of the player, and the face they show.
A model or a writer's lines phrase those packets later, in `communicator/`. **The model
translates and phrases; this decides.**

This is the `orchestrator/` component of the FWGC++ monorepo. It depends on `generator/` alone,
through `fwg/api/` and the functions `generator/api.txt` lists.

## Build and test

```bash
cmake -S orchestrator -B orchestrator/build -G "Visual Studio 17 2022" -A x64
cmake --build orchestrator/build --config Release
ctest --test-dir orchestrator/build -C Release --no-tests=error
```

The build adds `../generator` as a sibling, so the generator's library is built beside it with its
determinism flags. `generator/README.md` says where `cmake` is on this machine.

The tests generate the seed-42 size-33 world, small and large, and the seed-99 size-33 world,
which holds every kind of hidden face. They take about two and a half minutes on this machine
(measured 2026-09-27; the suite before the brain database took two). The same build makes
`fwgc_director.dll`, the C interface below, and `orchestrator_c_tests`, which hold it to the
director in about forty seconds more. The build fetches SQLite's source as `../dependencies.json`
names it, through the shared cache (`../dependencies.cmake`).

## What it does

`orchestrator::Director` (`include/orchestrator/director.hpp`) holds a world. It changes the world
in three places only:

- `advance(days)` calls `fwg::advance_world_time`;
- `resolve(quest, completed)`, the game reporting taken work finished or failed, calls
  `fwg::advance_world_time` with that resolution and no time passing;
- accepting or abandoning a quest calls `fwg::world_quest_request`.

Each logs to the world's `history.operations`. Every input is recorded (a hold too: see Days), and
`replay(start, inputs)` rebuilds the same world byte for byte, and the same memories and days. That is how
a test proves determinism, and how a save is restored: a save is the inputs.

`respond(speaker, act)` answers one act addressed to one person. That person can be a hero, a
dread, a roster NPC or a villain.

| Act | Answer |
|---|---|
| `greet` | `greeted`. |
| `small_talk` | `chatted`, with a Chat fact: the weather, wind and season where the speaker stands, and the time of day, in words. The worst nest within 5 km, if any. |
| `ask_surroundings` | The places within the speaker's reach, nearest first, a few of each kind: cities, hamlets, fortresses, landmarks. Then the worst nests within 5 km. |
| `ask_safety` | A verdict, `unsafe`, `wary` or `safe`, on what lies within a walk; the worst three dangerous places, those within a walk first; the nests near. |
| `ask_about_place` | That place, if it is within reach; `unknown` otherwise. |
| `ask_about_person` | That person as a stranger knows them, if they are the speaker's rival, kin or ally, or stand where the speaker stands or somewhere within reach. `unknown` otherwise, and always for a villain. |
| `ask_for_work` | The speaker's own open offers, then those of givers standing where the speaker stands. An innkeeper knows who at the inn is hiring. |
| `accept_quest` | Taken, if the speaker is its giver. A broker refuses and names the giver to go to. |
| `decline_quest` | Nothing changes. |
| `abandon_quest` | Released, if the speaker gave it. |
| `answer`, `thank`, `insult` | `acknowledged`, `thanked`, `offended`: the speaker remembers. |
| `complain` | `heard` (contract version 18): a complaint about the speaker or their work is heard out and costs nothing; from one the speaker is cold to, or a face that does not know them, `offended`, as an insult. |
| `confront` | `exposed`, with a Reveal fact, when the speaker hides a face and the player has cause to say so; `denied` otherwise. |
| `farewell`, `no_act` | `parted`, `not_understood`: nothing said, the world untouched. |

Every packet also carries one Memory fact (below). Only `accept_quest` and `abandon_quest` change
the world, and `act_changes_world(kind)` in the contract says so. A test grants every act
in turn and checks that the world document changes on exactly those two. A game confirms such an
act with the player before handing it over, so a misread line costs a question, never a quest.

`include/orchestrator/contract.hpp` holds the packet types (`DialogueAct`, `FactPacket`, `Fact`,
`Persona`) and names nothing of the generator. The communicator includes that header alone. The
`orchestrator_contract` library links nothing of the generator, so a component that depends on it
cannot reach a world.

- **A refusal says why, and what kind of why.** Its Reason fact names who, by name: "the work is
  Halbrann's to give", "Selbren is gone". `why` is one of `not_theirs_to_give`,
  `not_theirs_to_release`, `gone` or `refused_by_world`, so a caller can answer each kind in its
  own words. "The speaker's", as a reason once put it, was read by the voice as the stranger's.
- **A reason is always the director's words, never the generator's message.** A world refusal
  names quests by id and lists open work the speaker may know nothing of. An offer lapses at a
  moment of a day, and the board sweeps it at the next whole day, so taking it in between is
  refused: the reason is "the offer has lapsed".
- **A persona's `voice_key`** names the written lines the speaker speaks from: the archetype and
  the alignment of the face shown ("warden/LN", "deceiver/LN" once exposed). A mask -- a public
  face over a true one -- gets none, since the archetype (`false_hero`, `deceiver`) is what it
  hides. So do a dread and a roster person, who speak from their post's lines. Contract version 2
  added both.
- **How far is said roughly.** A place, a nest or a piece of work comes with a `range` band,
  never metres: `here` (under 50 m), `close` (under 1 km), `near` (under 3 km), `walk` (under
  7 km), `far` (under 15 km), `distant`. Nobody standing there has measured it. A voice read the
  kilometres out, and rounded them wrong ("thirteen kilometres" for 13.6).
- **Work carries an exact marker for the game.** `marker` (the anchor's id) and `marker_node`
  (its terrain node) are on offered work and on work once taken, so the game marks it on its
  map exactly. They are for the game; no voice says them. Contract version 3 added both.

## Conversation and memory

Contract version 4. What a speaker and the player have between them is folded from the recorded
inputs, so replay rebuilds it and a save needs nothing new. Every packet carries one Memory fact
about its speaker:

- `met` ("first" or "again"), `times` (conversations before this one) and `since` (how long ago
  the last one was, by the calendar: "today", "yesterday", "days", "weeks", "months", "years");
- `turn` in this conversation, and `repeat` when the player asks the same thing twice in it;
- `heard`, how many times before the player made this act to the speaker, in any conversation
  (contract version 6): what a speaker says to it can differ each time;
- `mood`, from "hostile" to "warm": thanks (once a conversation) and taking or finishing the
  speaker's work raise it; insults, giving work up and failing it lower it (a complaint does not);
- `work`, the player's latest dealing with the speaker's own work, and `work_news` (contract
  version 17): "yes" in the first conversation to begin after that work was finished or failed,
  when what came of it is news between them, and "no" after (a castellan had thanked the player
  for work finished "as if it were news, two weeks and a whole visit later");
- `asked_before`, the place or person the player asked about in their last conversation, by
  name (contract version 7: before it, what was last asked in any earlier one);
- `exposed`, once the player has made the speaker own to a hidden face;
- `feeling` and `resting` (contract version 9): how the speaker feels as they reply, and at rest
  ([Feelings](#feelings));
- `eased` (contract version 10): the player's calming words eased the speaker within the last
  two days, so a greeting can recall it and "not afraid" need not deny it. By the days, not the
  conversations: a visit is many conversations, since turning to someone else ends one;
- `joined`: the speaker overheard the player and joined in unasked ([Overhearing](#overhearing));
- `standing` (contract version 12): how far the two have come, never falling ([Short and long
  memory](#short-and-long-memory)).

A conversation ends at a farewell, when time passes, or when the player turns to someone else.
The next act to that speaker begins another.

**Saying sorry** (contract version 8). Each insult, and each piece of the speaker's own work
given up, is a grievance, kept at what it cost the speaker's regard: at the bottom of the scale an
insult costs less. `apologize` eases the latest and gives back what it cost (`Forgiven`), so an
insult and an apology leave the speaker where they were. Said again once every one is forgiven,
it is taken again and moves nothing (`Forgiven` too: "nothing to forgive" would deny the wrong).
From someone who never wronged the speaker, or to a face that does not know the player (a beast
unleashed), there is nothing to forgive (`Unwronged`): a complaint heard out (`complain`, contract
version 18) is no wrong, and "You never said there'd be so many of them", taken as an insult, had a
castellan bristle at a grieving ally (three blind judges of six, 2026-09-28).

## Feelings

Contract version 9. The owner asked for NPCs whose words and faces show how they feel, with a
state of their own at rest: a villager panicking because those around her are, whom the player's
words can calm (2026-09-26). The first feeling is fear, on a ladder: calm, uneasy, afraid,
panicked.

- **At rest,** everyone is calm, or uneasy where a nest of danger 4 or more lies within a
  kilometre of where they stand: a village under a dragon's crag is on edge.
- **A fright.** `fright(person, level)` is the game reporting something frightening someone, a
  threat near them, at 1 (uneasy), 2 (afraid) or 3 (panicked). Their fear rises to it when it is
  lower. What frightens them counts as still there for a quarter of an hour after the game last
  said so: report it again while it lasts.
- **Fear spreads.** `gather(people)` is the game reporting people together, near enough to see and
  hear each other. Each rises to one step below the most afraid of the others, and to what two of
  the others share: one panicking villager leaves the rest afraid, two leave them panicking, and
  someone afraid leaves the rest uneasy. Nobody's fear falls for company.
- **Fear falls** a step toward rest for every half hour since it last rose or was eased, never
  below rest.
- **Calming words** (the act `calm`: "it's all right, you're safe now") ease a frightened speaker
  a step (`Calmed`), and the speaker thinks better of the player, once a conversation. Not while
  what frightened them is still there, and not from someone they distrust, a hostile speaker or a
  face that does not know the player (`StillAfraid`). Someone at rest has nothing to ease
  (`NotAfraid`).
- **Asking.** `feeling(person)` says how anyone feels now and at rest, for the game to show as
  their expression; every packet's Memory fact says it for the speaker.

Frights and gatherings are inputs, so replay rebuilds every feeling. The packet never says what
frightened someone: the director does not know, and a speaker's words cannot name it.

## Overhearing

Contract version 10. The owner asked for NPCs who listen to the player's conversations with
others, "and decide if they want to join the conversation, or stay out of it": calmed by words
that calm the one spoken to, or walking over with something they can tell (2026-09-26). The game
places people and says who was near enough to hear; the director decides what each makes of it.

`overhear(people)`, right after an exchange (a `respond`), gives each person's reaction:
- **calmed**: the player's calming words eased the one spoken to, and this bystander is afraid,
  with nothing still frightening them and no hostility to the player. They are eased a step, as
  if spoken to, and think no better of the player for it: the words were not for them.
- **joins**: the one spoken to could not answer, and the bystander can: a place they did not know
  that the bystander knows, a person the same (by the one rule an answer about a person keeps),
  or work when they had none and the bystander has some. Not someone panicking, nor someone hostile
  to the player. They answer as if asked: the packet is theirs, its Memory fact `joined`, and a
  conversation with them begins, ending the last. The game walks them over. At most one joins, the
  first in the list who can, so the game lists the nearest first.
- **stays_out**: everything else, the one spoken to among them, and anyone gone.

An overhearing is an input, so replay rebuilds a joiner's conversation and a bystander's calm.
Since contract version 12 every bystander who heard it (all but the one spoken to and anyone gone)
also keeps it in their log, and hears the news it carried ([Short and long
memory](#short-and-long-memory)).

## News, and the brain database

Contract version 11. The owner asked for NPC brains: facts that are "common knowledge" ("everyone
in the village should know that the player saved them"), brought up to date "like a cron job", a
long memory written by a `compact` the game calls, and a short one of the conversation; stored per
NPC, and kept "like a database, that I can do SQL queries on" (2026-09-27). This is the first
layer: events, and who has heard of them. The short and long memories are the next ([Short and
long memory](#short-and-long-memory)).

**Events.** The director makes one wherever it sees something happen that people could know of,
and moves its state on as it goes; the game never writes one.

| Kind | Made when | States | Knows it at once | Word goes |
|---|---|---|---|---|
| `work` | work is offered on the board | `asked`, then `taken`, `finished`, `failed`, `given_up` (the player's); `lapsed`, `withdrawn`, `giver_gone`, `target_gone` (the world's) | the giver | `asked`, `taken` and the world's: where the giver stands. `finished`, `failed`, `given_up`: as far as its difficulty says, 5, 10, 20, 40 or 60 km, from where the giver stands and from where the work was for |
| `exposed` | the player brings a hidden face out (once a person) | `exposed` | the one exposed | 20 km, and everyone tied to them wherever they are |
| `scare` | the game reports someone frightened (frights within an hour at one place are one scare), or reports how a threat ended where none was | `frightened`, `calmed` (the player talked people there down within a day), then the game's word: `beaten`, `lost`, `passed`, and by whom | the frightened; whoever the game says ended it | where it happened; `beaten` or `lost`: 20 km |

The owner's example, "wraignwight village was attacked" becoming "the player helped wraignwight
defend itself", is a `work` event: a defend offer ("Stand with Wraignwight when they come for it")
is the attack people know of, and the player finishing it moves the event to `finished`. Or a
`scare`, when the game frightens people there and then reports the threat `beaten` by the player
(`report_event(site, outcome, by)`). A work event's words are the giver's stated purpose, never
what the work is really for.

**What people hear.** Whoever an event happened to knows it at once, "there". Everyone else hears
through `share(people)`, the game's cron job, called when it likes, for the people it names or
for everyone: each hears the latest state of every event that has reached them, "word", by these
rules:
- they stand where word of it starts;
- they are tied to whom it concerns (a hero's rivals, kin and allies), for an exposure;
- it is within the event's reach of where they stand, and word has had time to travel: 20 km a
  day, from when that state came about. No reach is further than the near read looks (half the
  globe's radius), as a speaker's own reach is not.

A later state replaces the one someone knew; nothing heard is forgotten. Someone who hears that the
player finished work where they live, or for someone they are tied to, thinks one better of the
player, and one worse for work failed or given up: once an event, before they have ever met (a
first meeting shows it). `related(event)` says who knows an event and who the next share would
tell, changing nothing: the game's "query related NPCs". Share and the game's report are inputs,
so replay rebuilds all of it.

**What a speaker says of it.** A News fact (contract.hpp lists its fields) carries one event as
the speaker knows it: the state they heard, which may be older than its latest, whether the player
brought it about, since when, and how they know. A greeting carries the newest the speaker knows the
player brought about; small talk the newest at the place they stand; an answer about a place up to
two there; about a person, one about them. Nobody says anything they have not heard.

**The brain database.** One SQLite database in memory per director (SQLite 3.53.4, fetched at
build time as `../dependencies.json` names it; public domain). The game reads it with SQL, `query(sql)` and
`fwgc_query`; only the director writes it, through its recorded inputs. A save does not hold it:
replaying the inputs writes it again, row for row (tested table by table).

| Table | Rows |
|---|---|
| `people` | everyone the world names: `uid`, `name` and `role` as a stranger has them, `kind` (roster, hero, dread, villain), a roster post's `site`, the `node` they stand on, `present` |
| `places` | every city, hamlet, fortress and key location: `id` (as a packet names it), `kind`, `name`, `node` |
| `work` | the quest board as it stands: `quest_id`, `giver`, `verb`, `purpose` (as stated), `anchor`, `node`, `difficulty`, `state` (as its event says it), `offered_day`, `expires_day`, `closed_day`, `closed_reason` |
| `relationships` | each person the player has dealt with: `conversations`, `talking`, `last_day`, `regard` (-3 to 3), `mood`, `work`, `grievances`, `forgiven`, `asked_before`, `exposed`, `eased_day`, `days_met`, `exchanges`, `works_finished`, `standing` |
| `events` | every event: `id`, `seq`, `kind`, `day`, the `place` it is about (`place_name`, `about_node`), the `node` word starts from, whom it concerns (`person`, `person_name`), work's `quest_id`, `verb`, `purpose` and `difficulty`, an exposure's `was` and `revealed`, `player`, and its latest `state`, `state_day` and `actor` |
| `event_states` | each state an event has been in, in order: `event`, `seq`, `state`, `day`, `actor` |
| `facts` | who has heard what: `person`, `event`, the `state` they know, the `day` they learned it, `how` ("there", "word", or "overheard") |
| `accounts` | the game's account of work the player finished or failed (schema 3; [below](#an-account-of-the-work)): `event`, `quest_id`, the `day` it was given, what `came` (its id, `came_as` "nest" or "person", `came_name`), `many`, `cost`, `part` |
| `news` (a view) | facts joined to their events |
| `exchanges` | each person's conversation log, since they were last compacted (below) |
| `exchange_facts` | what each exchange's reply named |
| `memories` | each person's long memory (below) |
| `active_short` (a view) | who has a log: `person`, how many `exchanges`, `since_day`, `last_day` |

The first four are copies of the director's own state, written afresh before a query when what
they copy has changed; the rest are the brains themselves. A query is one statement that only
reads: a write, a second statement, a pragma, a transaction, a file attached, is refused, as is a
statement that runs past about a second's work or answers more than 100,000 rows. Nothing a query
does is recorded, and none can change what the director decides. The schema's version is
`brains_schema` in `fwgc_contract_json`.

## An account of the work

Contract version 20, schema 3, C interface 5. The owner agreed (2026-09-28) that the game should
report how the fight went, and the hero tell it: four of six judges of the golden benchmark had
stopped believing in the hero where he had nothing to say of work done beside him. The director
knows only that work was finished or failed; the game saw how.

`account(quest_id, came, many, cost, part)` (`fwgc_account`), once work is finished or failed:
what came (a nest id or a person uid, so a speaker can name it), how many ("one", "few", "many",
"horde"), what it cost those who stood there ("none", "few", "many" lost), and the player's part
("fought", "led", "turned", "held", "hurt"). Each may be left out. It is kept on the work's event,
a row of `accounts`, and a later account of the same work replaces it. It changes neither the
world nor anyone's regard: how the work came out already did, through `resolve`. Recorded, so
replay keeps it.

The giver tells it. While the end of that work is their latest dealing with the player, their
greetings and small talk carry an Account fact (contract.hpp lists its fields): the account of the
last of their work the player finished or failed, never an older one's in its place. Taking up
their next work ends it. Nobody else is given it: news of the work travels as its event does, and
the account is the giver's own.

## Short and long memory

Contract version 12, schema 2, C interface 3. The owner, on compaction: "It should mirror what
you do in downsizing the conversations. Short is going to be a full conversation log. Long term,
should be the outcomes of the short, much like how people record things into long term"; and a
function exposed to the game, "for consistency, and 2 so we can test it here" (2026-09-27).

**Short: the log.** Everyone the player speaks to keeps every exchange (`exchanges`): its `id`,
the `conversation` and `turn`, the `day`, the `act` and its `target`, `about` (what it concerned by
name, as the speaker says it), the `outcome`, and `joined` for a bystander's answer; and in
`exchange_facts` what the reply named (a place, a person, work, a nest: `kind`, `subject`, `about`).
A bystander who overheard it keeps it too: `heard_from` the one spoken to, `of_exchange` that row,
`conversation` 0. `words(line, reply)` hands over the player's line and the reply as the game showed
them, once a reply is shown (after `respond`, and after `overhear` for a joiner's): the log of the
one who answered keeps them, and those who overheard it. The director reads no meaning into the
words; they are there for the game, the communicator, and whatever summarizes a log. Someone gone
hears nothing, and their log keeps nothing. Every row is numbered in the order made, never twice.

A bystander also hears the news the exchange carried: each event of its packet's News facts, at the
state the one spoken to knew it by, when that is later than the one they know it by (`how`
"overheard"). They take the player better or worse for it as word would move them, once an event.
The owner's example is a test: a villager who has heard that work was asked for in their town
overhears its giver greet the player once it is done, knows it finished and who did it, is
compacted, and says so when greeted.

**`compact(person, outcomes)`: the one way a log becomes long memory.** Nothing here calls it; the
game does, for whom it likes (someone despawned, a time skip, whoever `active_short` lists), and so
do the tests and the session tool. It ends a conversation under way with them, folds the log into
their long memory, and clears the log. Like a conversation compacted into what matters of it, the
long memory is the outcomes, by these rules, from the log and the director's own state:

| Memory | What | Subject, act |
|---|---|---|
| `asked` | what the stranger asked them | the target, the act (`ask_about_place`, `ask_for_work`, ...) |
| `told` | what they told the stranger of: what was asked about, and work (not each place a list named) | the thing, its kind (`place`, `person`, `quest_offer`) |
| `did` | what the stranger did: took, finished, failed or gave up their work; thanked, insulted, apologised; talked them down; brought a hidden face out | work's quest, or none; `taken`, `finished`, `failed`, `given_up`, `thanked`, `insulted`, `apologised`, `calmed`, `exposed` |
| `said` | the stranger's own words, word for word: their answers to the speaker's questions (the latest five of a log) | none; `said` |
| `overheard` | whom they overheard the stranger talking with | the one spoken to; the act |
| `open` | work of theirs the stranger holds and has not done | the quest; `taken` |
| `last` | what they last spoke of: the latest exchange that named something, else the latest that was more than a greeting, a farewell or a line not understood | its target; its act |

Each keeps `about` (by name), `times`, `first_day`, `last_day`, `words`, `source` ("rules", or
"given" from outcomes handed in) and `seq` (the order kept). A memory of what came up before is brought up to date,
keeping when it first was: asked again, it counts once more; work taken and since finished is
remembered finished, from when it was taken (as the one who gave it knows it). What is open is the
director's own state, never the log's, and so is what became of work. `last` is replaced.

**What a summary may add.** `outcomes`, when not empty, is a summary of the log written elsewhere, as JSON:
`{"said": [{"quote": "...", "about": "..."}], "last": "..."}`. Its quotes stand in for the rules':
each must be the stranger's own words, found word for word in a line of the log and cut at no
word, at most 240 bytes, at most five kept; `about` and `last` may use a name past a sentence's
start only if the log holds it; `last` is at most 300 bytes and kept beside the rules' own `last`,
never in place of it. Whatever fails is refused, and `compact` says why. Outcomes that are not that
JSON are refused before anything changes. Deeds, what is open, and what became of work never come
from outcomes handed in. They are recorded with the compaction, so replay never needs whatever
wrote them. What writes such a summary is the communicator's, and the next part of this slice.

**Recall facts.** A packet carries what the speaker remembers that bears on the act, from their
long memory and their log together, so nothing waits on a compaction: a greeting what they last
spoke of (in an earlier conversation), the work the stranger holds, and the stranger's latest kept
words; an answer about a place or a person that it was asked of before, and told of, how many times
and since when; asking for work what came of the speaker's work, and what the stranger holds. Each
before the Memory fact; contract.hpp lists the fields. A face that does not know the player
recalls nothing.

**Standing** (the Memory fact's `standing`, and `relationships`): `acquainted` from the first
conversation, `regular` once they have talked on three calendar days, `favourite` at ten days and
thirty exchanges, the most a roster person comes to; `hero` for a hero, a dread or a villain. It
never falls: an insult moves regard, not standing. Only heroes give work, so `works_finished`
counts but decides nothing yet. Standing does not decide what compaction keeps; nothing here drops
a memory for room ("Unbounded across the board").

## What a speaker says of a place

Asked about a place (contract version 8), a speaker says what they know of it past the place
itself: up to three of a settlement's trades from its roster (`trades` on the Place fact, the
roster's own posts first), the heroes who hold a post there (up to two Person facts, never the
speaker: asked about their own town, a speaker does not name themself), the nests within 2 km of
it (up to two Danger facts marked `around`, their `range` from the place), and the work they know
of that is anchored there (QuestOffer facts, as for work).

## What a speaker says of themself

Asked about themself (`ask_about_them`, contract version 13): how long they have held what they
hold, why they are here, what they are after, what they did or lost, who stands with them. The
answer is what the world records of them beyond their card: a Self fact (a hero's name beyond their
own, fame, home, how they stand where they are and what they are after, a city they lost and what
took it, when they were born, in bands of ages: "this age", "the last age", "ages ago"), up to three
Deed facts from their record, newest first (the kind of event, their part, where, when), and a
Person fact for each of up to three rivals, kin and allies, marked `tie`. Never the card's secrets,
and nothing of their work, which is asked for. A mask still worn gives its trade and where it stands
and nothing more; exposed, the story behind it. A roster person gives their post and where they
stand: the world keeps no story of them. The owner's golden benchmark (`communicator/eval/
session_hero.txt`, 2026-09-27) is a sustained conversation with a hero; its first judge heard the
hero unable to say how long they had held their fortress.

A castellan has a past (contract version 19, the generator's heroes version 3; the owner,
2026-09-28: "Yup lets add one in"): the Self fact's `held_years` (their years at the post, as
digits), `served_at` and `served_as` (where they served before it, and as a soldier, a sergeant, or
second to another fortress's castellan); the sovereign they are sworn to, a Person fact marked
`tie` "liege", first of their ties; and Deed facts for the wars their city fought in their
lifetime. Every castellan of the benchmark's six worlds had held only their post.

Another giver's work, named in an answer to ask_for_work, says how that giver stands to the
speaker (contract version 15, `giver_tie`: rival, kin or ally; liege since 19), from the speaker's hero record: the
castellan had named his own ally's work "with no view on either".

Work done beside the speaker (contract version 14): the Memory fact's `beside` is "yes" when the
speaker's own work the stranger last dealt with (taken, finished, failed or given up) is anchored at
the place the speaker stands, work "with me". It is read from the board each time, so nothing new is
remembered. A castellan whose fortress the player held beside him had judged the fight "by the look
of you".

Asked for work (contract version 13), a speaker names their own and at most two others': what an
answer names is what a reply says of it, and what the speaker is remembered to have told. A packet
of eight offers, three of them said, had kept eight as told. One errand from two givers is named
once, the first giver's: two harbourmasters of one harbour, each wanting its sea lanes kept open,
were named one after the other. The first time they are asked in a conversation, a speaker with
work of their own names only theirs, and others' when asked again (contract version 16): a hero's own
stand said beside two others' errands read as a notice board to every blind judge of the hero
benchmark.

## Faces

A hero with two faces shows one at a time. The persona is the face shown, and `persona.face`
names it when it is not the one a stranger sees.

- **A mask** (a deceiver's or a false hero's public face over its true one) stays on until the
  player confronts it with cause: they finished work it gave under a false purpose, or someone who
  knows it -- a rival, kin, an ally -- told them what it is (a Person fact's `hides`), or this is
  their third conversation. Confronted without cause, it denies it. Once off, the true face speaks
  from then on, with its archetype's lines.
- **A beast** is its calm face except on the full moon's day and the day either side, when it is
  unleashed: it speaks as that face and does not know the player. A player who has met it
  unleashed can confront its calm face, which owns to it.
- **A fractured heart** shows its first and second faces on alternate days. A player who has met
  the second can confront the first, which owns to it.

An exposure answers `exposed`, with a Reveal fact: the face owned to, the role the known face
claimed, and the role of the one it hid. Only a mask's two roles differ; a beast's or a
fractured heart's faces share one role and differ in manner and alignment.

## Days

Every living roster post keeps a day (`src/schedule.hpp`): asleep, at home, the walk to work,
work, the walk home, the evening at home. The hours are the building's, counted from sunrise, the
world's hour 0. With sunrise at six:

| Hours | Buildings | Work |
|---|---|---|
| Day trade | every building not named below: smithy, pottery, market stall, library, temple... | 7:00 to 17:00; up at 5:00, asleep at 21:30 |
| Early | bakery, butchery, farms, orchards, herds, mills, boats, wells and water, the market square, the stable, the cart yards | 4:30 to 14:30 |
| Late | inn, tavern, guildhall, bathhouse, observatory | 11:00 to 22:00 |
| Watch | gates, towers, barracks, the keep, cells, infirmaries | three shifts of eight hours from sunrise, a building's posts taking them in turn |

The walk is the roster's `commute_m` (the straight metres from the bed its plan built them to
their work) at a metre a second, since streets wind: never under a minute, and never longer than
the margin around work, so it never eats into sleep. Someone the plan built no bed for -- a
fortress's people, an unplaced stable's hands -- lives at their post and never walks. Heroes,
dreads, villains and the dead have no day: they stand where the world says. The hours are tunable
constants, like the reaches.

**A hold.** `hold(person, minutes)` is the game reporting that it kept someone where they stood,
from the clock on: a conversation, a road blocked, anything. Afterwards they make for where their
day has them. A stretch they cannot reach before it ends is skipped for the next, from where they
stand, and each stretch they reach still ends when it was to:

- held 20 minutes on the walk to work, they arrive 20 minutes late, and the shop still shuts on
  time, open 20 minutes less;
- held at work within its hours, nothing is lost; held past closing, the overrun comes out of the
  evening at home, and bedtime holds;
- held on the way until past closing, they turn for home, and the shop stays shut that day;
- a hold begun while another runs keeps them until the later one ends.

By the end of the stretch they reach, the day runs as planned again. A hold changes nothing else:
not the world, not what anyone remembers, not a packet. Each is worked out once, when made, as a
detour of the day, so a day reads the same whatever span is asked of it; it is an input, so
replay rebuilds it. Holding someone with no day is recorded and changes nothing. Minutes must be
more than zero and at most a day.

**Asking.** `whereabouts(person)` says what they are doing now, where they are along the way from
home (0) to work (1), and the stretch under way. `day_of(person, day)` gives the stretches that
meet that calendar day, sunrise to sunrise. `whereabouts_at(site)` gives everyone with a day at a
site. The director says when; the game draws the path between the two plots (`home_plot`,
`workplace_plot`).

**Everyone at once.** `positions()` gives every roster post with a day and the stretch they are
in, in roster order, by their index in `world().npcs.people`: one pass over the roster, copying
no strings. Measured on 2026-09-26, Release, on this machine: 0.5 ms for the 5,423 posts of seed
42 at size 33, and 1.4 ms for the 11,985 of seed 99 at size 65 (2.6 ms with a thousand of them
held), where asking after each by uid takes 19 ms and 94 ms. A stretch changes only when it
ends or when someone is held, and on a walk where they are follows from the time, so a game can
keep what it was given and ask again after a hold, after the clock moves, or when a stretch it
holds runs out. Each stint is worked out from the one day it falls in (`schedule::stint_at`),
and a test holds it to what the whole day gives, to the bit.

## The C interface

`fwgc_director.dll` holds the whole director behind a plain C interface,
[`include/orchestrator/fwgc_director.h`](include/orchestrator/fwgc_director.h), for a game engine
to load: an opaque handle, UTF-8 text in and out, and plain structs for what a game reads every
frame. This folder's own build makes it (`ORCHESTRATOR_BUILD_C_API`, on when the folder is built
on its own, never when another component adds it as a sibling), into `bin/` beside the test that
holds it to the director, with its import library in `lib/`; `cmake --install` lays out `bin/`,
`lib/` and `include/` for a game's third-party folder.

- **Everything the director does.** A world from a generator request (`fwgc_create`, the request
  `fwg.exe --request` reads); time (`fwgc_advance`, `fwgc_clock`); acts by name (`fwgc_respond`,
  its packet as JSON for the game and in the contract's text form for the communicator);
  who overheard it (`fwgc_overhear`, each bystander's reaction and a joiner's reply); work
  (`fwgc_resolve`, and `fwgc_account`, how it went); feelings (`fwgc_fright`, `fwgc_gather`, `fwgc_feeling`); days
  (`fwgc_hold`, `fwgc_positions`, `fwgc_whereabouts_of`, `fwgc_whereabouts_at`, `fwgc_day_of`,
  `fwgc_roster`); what a speaker remembers (`fwgc_acquaintance`, `fwgc_memories`); the log's
  words and compaction (`fwgc_words`, `fwgc_compact`); news (`fwgc_share`, the cron
  job, who heard what back as JSON; `fwgc_report_event`, how a threat ended; `fwgc_related`, who
  knows an event); the brain database (`fwgc_query`, read-only SQL, rows back as JSON); the world
  document (`fwgc_world_json`, for drawing the world only: it holds what speakers keep back); and
  `fwgc_contract_json`, every act (whether it needs a target, whether it changes the world),
  outcome and fact kind by name, and the brain database's schema version.
- **Saves.** `fwgc_save` writes the request, the digest of the world it made, every input, and a
  check: the clock and the count of the world's logged operations after them. `fwgc_restore`
  makes the world again, compares the digest, replays, compares the check, and refuses
  (`FWGC_INCOMPATIBLE`) a save that comes out any differently rather than restore it onto another
  world. With the shared C runtime the math is the operating system's, so a save restores where
  the world comes out the same: the same build, on a runtime that computes alike. Save, restore,
  save gives the same bytes.
- **Nothing crosses but a status.** Every call returns an `fwgc_status`, and `fwgc_last_error()`
  says why, in the calling thread. A caller's mistake -- a NULL, text that is not UTF-8, an act or
  a uid the director does not know -- is `FWGC_BAD_ARGUMENT` and records nothing. A failure part
  way through a change poisons the handle: only its save and its destruction still work, and the
  save holds only what succeeded, so the game restores from it.
- **One lock** covers every call, since the generator keeps caches two threads must not share.
  Creating, restoring and advancing run long: make them off the game thread. Every call runs in
  the float environment worlds are made in, and gives the caller's back: an engine that flushes
  denormals, or rounds another way, would otherwise make another world. A test sets both and
  gets the same world.
- **It keeps pace.** A new input kind, or a new field on a type the interface writes out, stops
  its build until the interface carries it.

Measured on 2026-09-26, Release, on this machine, seed 42: creating a size-33 world takes 1.2 s
and a size-65 one 2.6 s, restoring about the same, and advancing a month 47 ms and 144 ms. About
0.8 s of a create or a restore is the digest, one pass over the whole world document: the price
of refusing a save that would come out as another world. A save is a few hundred bytes plus its
inputs; the world document is 53 MB at size 33 and 107 MB at 65.

The tests (`orchestrator_c_tests`) run a session through the DLL and the C++ director side by
side -- every act and every input kind -- and compare every packet, memory, feeling, position and
day after each step, and the world at the end, byte for byte; then save, restore and refuse
broken saves. An Unreal module binds the DLL as an external library: the header, the import
library, the DLL as a runtime dependency, loaded and its `fwgc_api_version()` checked at start-up.

## A session, to listen to

`orchestrator_session SCRIPT` (`tools/session.cpp`) plays a scripted session against a generated
world and writes each packet out as text (`orchestrator::packet_text`, which
`packets_from_text` reads back whole). `communicator_eval --session` speaks them, so a person or a
judge can hear a whole conversation: several meetings with one speaker, time passing, work taken
and finished, a mask confronted. The script names its speakers by kind -- a roster person, a
giver, a mask, a beast, a fractured heart -- and the tool picks them from the world. A script can
frighten a speaker (`fright NAME LEVEL`), gather speakers (`gather NAME...`), let minutes pass
(`minutes N`), name someone standing where another speaker stands (`who NAME near OTHER N`), ask
about a place any earlier packet named by saying its name (`@said`), report who overheard the
last exchange (`overhear NAME...`), a joiner's reply written as their turn, and compact a speaker's
log (`compact NAME [FILE]`, FILE outcomes as compact takes them), what it kept written out, and give
the game's account of work done (`account NAME CAME MANY COST PART`, CAME `@nest` for the nest the
latest packet named). Every line said is
handed over as the exchange's words (the replies are spoken later, by the communicator). It is a
measuring tool; nothing a game ships.

## What a speaker knows

- **Reach.** A roster person knows about 10 km around them (hamlet, fortress) or 20 km (city). A
  hero knows 25, 40 or 60 km by fame band. A dread knows 20 km. A villain knows its own `reach_m`.
  Every reach is clamped to the near read's ceiling, half the globe's radius. On the small world
  (200 km around) that ceiling is 15.9 km, so everyone's reach is the ceiling there. On the large
  world (600 km) it is 47.7 km, and the tiers show. `src/knowledge.hpp` holds the numbers. They
  are tunable, and a test pins that the tiers bind.
- **Places** come from `fwg::read_near` with the category filter. At the ceiling most cities' reads
  would be refused over the encounter rows, which is why the generator grew that filter.
- **People** are known where they stand within reach, and a hero's rivals, kin and allies know
  each other wherever they are. Only those three know a mask for one.
- **Their own people.** A persona carries `people`, the speaker's people by the world's id: a
  hero's civilization, a roster person's (contract version 5). It says whose sayings they know.
- **The weather and the moon** come from `fwg::weather_sample_at_node` and `fwg::moon_phase_on`,
  reads of the world at its own clock.
- **Offers** carry the giver's public name and role, the verb, the *stated* purpose, the
  difficulty from the hook, the anchor by its own name, how far it is when within reach (a
  `range`), the days left, and the exact marker for the game.
  - A ruin is named as the ruins, never as the hamlet built on the same node.
  - An anchor the world's blocks do not name (a ley node, a shrine) gets no `where`. Its stated
    purpose says where it is.

## What a packet never carries

Each rule below has a test, and each test was proven to fail without its rule: a mutation pass
switched each rule off in a scratch build.

- **A hidden face that is not out.** A hero speaks with the face a stranger sees (the public
  face, else the first, else the calm one) unless the rules above bring another out. A mask's true
  face never shows before it is exposed, and no persona says there is anything behind a mask.
- **What a card keeps back.** The persona passes `manner`, `voice`, `toward_player`, `line` and the
  `never` list. It does not pass `lies`, `belief`, `wants`, `fears` or `tells`.
- **A hook's actual effect, or whether its stated purpose is true.** Offers carry a fixed set of
  fields; `unwitting` is not one of them.
- **Places beyond reach.**
- **Anything invented.** Every fact names the world id it came from.

## Not in this slice

- The communicator's half of slice 15: what writes `compact`'s outcomes from a log, Recall said in
  the written lines and the voice, and the truth check for it. An event's memory written from the
  event log and checked is not built.
- Heroes' brains and `promoteNPC` (slice 16): a roster person made a full hero, through a
  generator verb.
- News of someone gone. The board closes work when its giver is gone, but no test world's giver
  goes in three years of its time (seed 99, size 33, measured 2026-09-27: every piece of work lapsed),
  so the planned `gone` event had nothing to be tested against. The work's own event still moves to
  `giver_gone`.
- Pacing and hero direction: the director's build-up, peak and relax, and gameplay events in.
- Days for heroes, dreads and villains; rest days; days that change with the weather or the
  season; and a speaker saying what they are about, or that they are late.
- The acts `trade`, `persuade`, `threaten`, `bribe`.
- A question the speaker asks the player, remembered until answered: `answer` is acknowledged,
  and nothing yet records what was asked.
- Bearings ("to the north").

Each needs its own plan.
