# FWGC++ orchestrator

The director: deterministic C++ that holds one world and changes it only through the
generator's logged verbs. It answers a player's dialogue acts as **fact packets**: what the
person spoken to knows and would say, with the face they show a stranger. A model phrases those
packets later, in `communicator/`. **The model translates and phrases; this decides.**

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

The tests generate the seed-42 size-33 world, small and large. They take under ten seconds.

## What it does

`orchestrator::Director` (`include/orchestrator/director.hpp`) holds a world. It changes the world
in two places only:

- `advance(days)` calls `fwg::advance_world_time`;
- accepting or abandoning a quest calls `fwg::world_quest_request`.

Both log to the world's `history.operations`. Every input is recorded, and `replay(start, inputs)`
rebuilds the same world byte for byte. That is how a test proves determinism, and how a save is
restored.

`respond(speaker, act)` answers one act addressed to one person. That person can be a hero, a
dread, a roster NPC or a villain.

| Act | Answer |
|---|---|
| `ask_surroundings` | The places within the speaker's reach, nearest first, a few of each kind: cities, hamlets, fortresses, landmarks. Then the worst nests within 5 km. |
| `ask_about_place` | That place, if it is within reach; `unknown` otherwise. |
| `ask_about_person` | That person as a stranger knows them, if they stand where the speaker stands or somewhere within reach. `unknown` otherwise, and always for a villain. |
| `ask_for_work` | The speaker's own open offers, then those of givers standing where the speaker stands. An innkeeper knows who at the inn is hiring. |
| `accept_quest` | Taken, if the speaker is its giver. A broker refuses and names the giver to go to. |
| `decline_quest` | Nothing changes. |
| `abandon_quest` | Released, if the speaker gave it. |
| `farewell`, `no_act` | `parted`, `not_understood`: no facts, the world untouched. |

Only `accept_quest` and `abandon_quest` change the world, and `act_changes_world(kind)` in the
contract says so. A test grants all nine acts in turn and checks that the world document changes
on exactly those two. A game confirms such an act with the player before handing it over, so a
misread line costs a question, never a quest.

`include/orchestrator/contract.hpp` holds the packet types (`DialogueAct`, `FactPacket`, `Fact`,
`Persona`) and names nothing of the generator. The communicator includes that header alone. The
`orchestrator_contract` library links nothing of the generator, so a component that depends on it
cannot reach a world.

## What a speaker knows

- **Reach.** A roster person knows about 10 km around them (hamlet, fortress) or 20 km (city). A
  hero knows 25, 40 or 60 km by fame band. A dread knows 20 km. A villain knows its own `reach_m`.
  Every reach is clamped to the near read's ceiling, half the globe's radius. On the small world
  (200 km around) that ceiling is 15.9 km, so everyone's reach is the ceiling there. On the large
  world (600 km) it is 47.7 km, and the tiers show. `src/knowledge.hpp` holds the numbers. They
  are tunable, and a test pins that the tiers bind.
- **Places** come from `fwg::read_near` with the category filter. At the ceiling most cities' reads
  would be refused over the encounter rows, which is why the generator grew that filter.
- **Offers** carry the giver's public name and role, the verb, the *stated* purpose, the
  difficulty from the hook, the anchor by its own name, its distance when within reach, and the
  days left.
  - A ruin is named as the ruins, never as the hamlet built on the same node.
  - An anchor the world's blocks do not name (a ley node, a shrine) gets no `where`. Its stated
    purpose says where it is.

## What a packet never carries

Each rule below has a test, and each test was proven to fail without its rule: a mutation pass
switched each rule off in a scratch build.

- **A hidden face.** A hero speaks with their public face (else the first, else the calm one),
  never the true, second or unleashed face.
- **What a card keeps back.** The persona passes `manner`, `voice`, `toward_player`, `line` and the
  `never` list. It does not pass `lies`, `belief`, `wants`, `fears` or `tells`.
- **A hook's actual effect, or whether its stated purpose is true.** Offers carry a fixed set of
  fields; `unwitting` is not one of them.
- **Places beyond reach.**
- **Anything invented.** Every fact names the world id it came from.

## Not in this slice

- Pacing and hero direction: the director's build-up, peak and relax, and gameplay events in.
- The acts `trade`, `persuade`, `threaten`, `bribe`.
- Bearings ("to the north").
- The C interface an Unreal module binds to.

Each needs its own plan.
