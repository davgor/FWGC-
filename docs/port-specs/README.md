# Port specifications

Implementable specifications for every part of the Python `FantasyWorldGenerator`
that this codebase has not reached yet, extracted **before that repository was
retired** and audited against its source.

These exist because of a measured fact: twelve of the nineteen modules behind stages
6-16 had no written specification of any kind, so the Python source *was* their
specification and it stops being readable the day that tree is deleted. What is here
is the only description of those subsystems that will survive.

## How they were made, and how far to trust them

Twenty-two agents each read their modules in full — code, comments, docstrings and
data files — and wrote a specification aimed at a reader who has this repository in
front of them and nothing else. A second agent per group then went back to the
source and tried to implement from the specification alone, reporting the first
point at which it would have had to guess.

Both halves are kept. The specification is the narrative; the **Addenda** section at
the end of each file holds what the auditor found missing, with the actual content,
and any claim it found to be false. Where the two disagree, the addendum was written
second and with the source open.

**They are not tested.** Nothing here is asserted by a test in this repository, and
no world was generated to check any of it. They are careful readings of code, made
once. Treat a specification as a strong starting point and the source of truth as
whatever the implementation and its tests end up proving.

## The specifications

| Group | Source modules | Est. C++ | Audit | Addenda |
|---|---|---:|---|---|
| [01-recipes-water](01-recipes-water.md) | `terrain_recipes`, `terrain_water` | ~520 | implementable | 6 |
| [02-climate](02-climate.md) | `terrain_climate` | ~420 | implementable | 5 |
| [03-biomes-detail](03-biomes-detail.md) | `terrain_biomes`, `terrain_detail` | ~950 | implementable | 10 |
| [04-ecology-islands](04-ecology-islands.md) | `terrain_ecology` | ~780 | implementable | 9 |
| [05-leylines-magic](05-leylines-magic.md) | `terrain_leyline_history`, `terrain_magic` | ~1150 | implementable | 10 |
| [06-astrology](06-astrology.md) | `terrain_astrology` | ~560 | implementable | 6 |
| [07-civ-registry-profiles](07-civ-registry-profiles.md) | `civilization_registry`, `terrain_profiles` | ~2000 | **gaps** | 14 (6 blocking) |
| [08-settlements](08-settlements.md) | `terrain_settlements` | ~2600 | **gaps** | 13 (5 blocking) |
| [09-humans-seasons](09-humans-seasons.md) | `terrain_humans`, `terrain_seasons` | ~1650 | implementable | 12 |
| [10-society](10-society.md) | `terrain_society` | ~1500 | **gaps** | 11 (1 blocking) |
| [11-nests](11-nests.md) | `terrain_nests` | ~1050 | **gaps** | 6 (1 blocking) |
| [12-history-ages](12-history-ages.md) | `terrain_history` | ~3000 | **gaps** | 11 (3 blocking) |
| [13-religion](13-religion.md) | `terrain_religion` | ~1450 | **gaps** | 7 (1 blocking) |
| [14-city-planner](14-city-planner.md) | `city_planner`, `city_shapes`, `city_geometry`, `city_fortifications` | ~4200 | **gaps** | 15 (3 blocking) |
| [15-hamlet-castle](15-hamlet-castle.md) | `hamlet_planner`, `castle_planner`, `castle_geometry` | ~2600 | **gaps** | 13 (2 blocking) |
| [16-nomads](16-nomads.md) | `terrain_nomads`, `terrain_nomad_routes`, `terrain_nomad_effects` | ~2600 | **gaps** | 8 (1 blocking) |
| [17-beasts-encounters](17-beasts-encounters.md) | `terrain_beast_movement`, `terrain_encounters` | ~1500 | **gaps** | 8 (1 blocking) |
| [18-villains-corruption](18-villains-corruption.md) | `terrain_villains`, `terrain_corruption`, `terrain_visitation` | ~2600 | **gaps** | 13 (4 blocking) |
| [19-heroes-storyweb](19-heroes-storyweb.md) | ``, `` | ~6500 | **gaps** | 14 (5 blocking) |
| [20-npcs-keylocations](20-npcs-keylocations.md) | ``, `` | ~7200 | **gaps** | 13 (4 blocking) |
| [21-time-quests](21-time-quests.md) | `terrain_time`, `terrain_quest_api`, `terrain_person_api` | ~2600 | **gaps** | 17 (4 blocking) |
| [22-heritage](22-heritage.md) | `` | ~2600 | **gaps** | 14 (5 blocking) |

Estimated total: **roughly 50,030 lines of C++**, excluding tests, across 22 groups.

For what is already built and where these fit in the ladder, see
[../porting-status.md](../porting-status.md).
