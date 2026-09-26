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
which holds every kind of hidden face. They take about forty seconds.

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
| `confront` | `exposed`, with a Reveal fact, when the speaker hides a face and the player has cause to say so; `denied` otherwise. |
| `farewell`, `no_act` | `parted`, `not_understood`: nothing said, the world untouched. |

Every packet also carries one Memory fact (below). Only `accept_quest` and `abandon_quest` change
the world, and `act_changes_world(kind)` in the contract says so. A test grants all sixteen acts
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
  speaker's work raise it; insults, giving work up and failing it lower it;
- `work`, the player's latest dealing with the speaker's own work;
- `asked_before`, the place or person the player asked about in their last conversation, by
  name (contract version 7: before it, what was last asked in any earlier one);
- `exposed`, once the player has made the speaker own to a hidden face;
- `feeling` and `resting` (contract version 9): how the speaker feels as they reply, and at rest
  ([Feelings](#feelings)).

A conversation ends at a farewell, when time passes, or when the player turns to someone else.
The next act to that speaker begins another.

**Saying sorry** (contract version 8). Each insult, and each piece of the speaker's own work
given up, is a grievance, kept at what it cost the speaker's regard: at the bottom of the scale an
insult costs less. `apologize` eases the latest and gives back what it cost (`Forgiven`), so an
insult and an apology leave the speaker where they were. Said again once every one is forgiven,
it is taken again and moves nothing (`Forgiven` too: "nothing to forgive" would deny the wrong).
From someone who never wronged the speaker, or to a face that does not know the player (a beast
unleashed), there is nothing to forgive (`Unwronged`).

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

## What a speaker says of a place

Asked about a place (contract version 8), a speaker says what they know of it past the place
itself: up to three of a settlement's trades from its roster (`trades` on the Place fact, the
roster's own posts first), the heroes who hold a post there (up to two Person facts, never the
speaker: asked about their own town, a speaker does not name themself), the nests within 2 km of
it (up to two Danger facts marked `around`, their `range` from the place), and the work they know
of that is anchored there (QuestOffer facts, as for work).

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

## A session, to listen to

`orchestrator_session SCRIPT` (`tools/session.cpp`) plays a scripted session against a generated
world and writes each packet out as text (`orchestrator::packet_text`, which
`packets_from_text` reads back whole). `communicator_eval --session` speaks them, so a person or a
judge can hear a whole conversation: several meetings with one speaker, time passing, work taken
and finished, a mask confronted. The script names its speakers by kind -- a roster person, a
giver, a mask, a beast, a fractured heart -- and the tool picks them from the world. A script can
frighten a speaker (`fright NAME LEVEL`), gather speakers (`gather NAME...`) and let minutes pass
(`minutes N`). It is a measuring tool; nothing a game ships.

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

- Pacing and hero direction: the director's build-up, peak and relax, and gameplay events in.
- Days for heroes, dreads and villains; rest days; days that change with the weather or the
  season; and a speaker saying what they are about, or that they are late.
- The acts `trade`, `persuade`, `threaten`, `bribe`.
- A question the speaker asks the player, remembered until answered: `answer` is acknowledged,
  and nothing yet records what was asked.
- Bearings ("to the north").
- The C interface an Unreal module binds to.

Each needs its own plan.
