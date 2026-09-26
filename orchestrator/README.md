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

Each logs to the world's `history.operations`. Every input is recorded, and
`replay(start, inputs)` rebuilds the same world byte for byte, and the same memories. That is how
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
- `exposed`, once the player has made the speaker own to a hidden face.

A conversation ends at a farewell, when time passes, or when the player turns to someone else.
The next act to that speaker begins another.

**Saying sorry** (contract version 8). Each insult, and each piece of the speaker's own work
given up, is a grievance, kept at what it cost the speaker's regard: at the bottom of the scale an
insult costs less. `apologize` eases the latest and gives back what it cost (`Forgiven`), so an
insult and an apology leave the speaker where they were. Said again once every one is forgiven,
it is taken again and moves nothing (`Forgiven` too: "nothing to forgive" would deny the wrong).
From someone who never wronged the speaker, or to a face that does not know the player (a beast
unleashed), there is nothing to forgive (`Unwronged`).

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

## A session, to listen to

`orchestrator_session SCRIPT` (`tools/session.cpp`) plays a scripted session against a generated
world and writes each packet out as text (`orchestrator::packet_text`, which
`packets_from_text` reads back whole). `communicator_eval --session` speaks them, so a person or a
judge can hear a whole conversation: several meetings with one speaker, time passing, work taken
and finished, a mask confronted. The script names its speakers by kind -- a roster person, a
giver, a mask, a beast, a fractured heart -- and the tool picks them from the world. It is a
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

- Pacing and hero direction: the director's build-up, peak and relax, and gameplay events in.
- The acts `trade`, `persuade`, `threaten`, `bribe`.
- A question the speaker asks the player, remembered until answered: `answer` is acknowledged,
  and nothing yet records what was asked.
- Bearings ("to the north").
- The C interface an Unreal module binds to.

Each needs its own plan.
