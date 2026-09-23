// One predicate for "is this person still here", across four blocks that disagree.
//
// Four packages answer the same question in three vocabularies and no caller can ask it
// once: a hero's status is `living` or `legend`, an NPC's is `alive` or `dead`, and a
// villain's is `living` or `fallen`. The dreads -- the antagonist cast INSIDE the heroes
// block -- share the heroes' words. A tick crossing many ages asks this constantly, so it is
// asked here instead of at every call site.
//
// **Read-only.** Nothing in this module writes to a world, and no vocabulary is migrated:
// the per-package predicates stay authoritative and this delegates to them where one
// exists (`is_standing` for villains). Writers ask `token()` which word a block uses; they
// never pick one by matching on a string.
//
// Ported IN FULL from `Sim/icarus_sim/terrain_liveness.py`: the two tables, `token`,
// `liveness`, `is_present`, `present` and `census`. The person API and the read surface
// (`terrain_person_api.py`, `terrain_read_select.py`) are built on it, and so is the quest
// lifecycle's giver check. The Python tree is being deleted; this is the only copy.
//
// Two traps this module exists to survive:
//
// **`status` means two different things at two depths of one block.** The hero generator
// writes a block-level status of `ok`/`failed` and a person-level status of
// `living`/`legend` under the same key name; the NPC roster does the same. The reference
// guarded that at run time by refusing a dict carrying a block-only key (`people`,
// `quest_hooks`, `policy_revision`). Here the records are typed, so the guard is the type
// system: `liveness()` has an overload per PERSON type and is DELETED for the three block
// types, so handing it a block does not compile. The string form, `liveness_of_status`,
// cannot tell a block's word from a person's -- `liveness_of_status(Heroes, "ok")` reads
// GONE, exactly as the reference read `{'status': 'ok'}` -- so never feed it a block's
// status.
//
// **Absent status means present.** A record written before a package recorded departure at
// all described someone who stood. `is_standing` says so for villains, and every world
// generated before the fall pair landed would otherwise read as a world of ghosts. An
// empty status string is the absent key here, and an NPC's `alive` flag defaults to true.
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "fwg/heroes.hpp"
#include "fwg/npcs.hpp"
#include "fwg/villains.hpp"

namespace fwg {

struct World;

// ---------------------------------------------------------------------------
// The published pair, and the four blocks that hold people.
// ---------------------------------------------------------------------------

// The predicate's own vocabulary. A request that speaks about a person's liveness speaks
// these two words and no sixth vocabulary: there are already five, and one claiming to
// unify them would be the sixth.
enum class Liveness : int { Present = 0, Gone };

inline constexpr std::string_view kLivenessPresent = "present";
inline constexpr std::string_view kLivenessGone = "gone";

std::string_view to_string(Liveness state);

// "present" or "gone" -> the state; anything else is nullopt, which a request boundary
// refuses as an invalid choice.
std::optional<Liveness> parse_liveness(std::string_view word);

// The blocks whose records carry liveness, in the reference's table order. A block absent
// from this list is not one whose records carry liveness, and asking about it is a caller
// error rather than a default -- nomads, for instance, have no departure state at all.
enum class LivenessBlock : int { Heroes = 0, Dreads, Npcs, Villains };

inline constexpr std::size_t kLivenessBlockCount = 4;

// All four, in table order: heroes, dreads, npcs, villains.
std::span<const LivenessBlock> liveness_blocks();

// "heroes" | "dreads" | "npcs" | "villains".
std::string_view to_string(LivenessBlock block);

// The block of that name, or nullopt -- the reference's "Unknown liveness block".
std::optional<LivenessBlock> parse_liveness_block(std::string_view name);

// ---------------------------------------------------------------------------
// The two tables. Three words for one event, two of them carrying a judgement the third
// does not, and that is the published surface rather than a choice made here.
//
//   block      present   gone
//   heroes     living    legend
//   dreads     living    legend
//   npcs       alive     dead
//   villains   living    fallen
// ---------------------------------------------------------------------------

// PRESENT_STATUS: the word `block` writes for a person who is still here.
std::string_view present_status(LivenessBlock block);

// GONE_STATUS: the word `block` writes for a person who is no longer here.
std::string_view gone_status(LivenessBlock block);

// Every word any block writes, each once: living, legend, alive, dead, fallen.
std::vector<std::string_view> published_statuses();

// The word `block` writes for `state`. The inverse of liveness, and deliberately the ONLY
// one: a writer that picked a word by matching on a string would put `dead` on a hero or
// `legend` on an NPC, and both read back through liveness as the wrong answer rather than
// as an error.
std::string_view token(LivenessBlock block, Liveness state);

// The same, keyed by name. Throws std::invalid_argument on an unknown block
// ("Unknown liveness block: 'x'") or an unknown state ("Unknown liveness state: 'x'").
std::string_view token(std::string_view block, std::string_view state);

// ---------------------------------------------------------------------------
// The predicate.
// ---------------------------------------------------------------------------

// Liveness of a status WORD read from a person of `block`. Empty is the absent key and
// reads as present; the block's present word reads as present; anything else is gone.
// Villains delegate to `is_standing`, whose rule is authored where the villains are:
// two implementations of one rule is the defect this module exists to prevent.
//
// The depth trap applies to this form (see the header comment): hand it a PERSON's word.
Liveness liveness_of_status(LivenessBlock block, std::string_view status);

// Liveness of a typed person, dispatched on the block the type belongs to. A dread is a
// HeroDread, which is how the heroes/dreads distinction survives: the two share one
// vocabulary but are different lists.
Liveness liveness(const Hero& person);
Liveness liveness(const HeroDread& dread);
Liveness liveness(const NpcPerson& person);  // `alive`, which defaults to true
Liveness liveness(const Villain& villain);   // exactly `is_standing`

// A block handed where a person belongs is refused at compile time -- the typed form of the
// reference's BLOCK_ONLY_KEYS guard.
Liveness liveness(const HeroesBlock&) = delete;
Liveness liveness(const NpcRoster&) = delete;
Liveness liveness(const VillainBlock&) = delete;

// True while this person is still in the world.
bool is_present(const Hero& person);
bool is_present(const HeroDread& dread);
bool is_present(const NpcPerson& person);
bool is_present(const Villain& villain);

// The still-here records of `people`, in the order given.
std::vector<const Hero*> present(std::span<const Hero> people);
std::vector<const HeroDread*> present(std::span<const HeroDread> dreads);
std::vector<const NpcPerson*> present(std::span<const NpcPerson> people);
std::vector<const Villain*> present(std::span<const Villain> villains);

// ---------------------------------------------------------------------------
// The census.
// ---------------------------------------------------------------------------

struct LivenessCount {
    int present = 0;
    int gone = 0;
};

// Per-block counts over whatever blocks this world carries. A block ABSENT here is one the
// world does not have or whose package failed -- not an empty cast: zero present people
// and no package at all are different facts, and a tick must not confuse them.
//
//   heroes, dreads -- when world.has_heroes and heroes.status == "ok"
//   npcs           -- when world.has_npcs and the roster is ok
//   villains       -- when world.has_villains; the villain block has no block-level status
//
// The reference also skipped a block whose people list was null; a typed block always has
// a list, so an ok block with nobody in it counts as {0, 0}.
struct LivenessCensus {
    std::array<std::optional<LivenessCount>, kLivenessBlockCount> blocks{};
    const std::optional<LivenessCount>& at(LivenessBlock block) const {
        return blocks[static_cast<std::size_t>(block)];
    }
};

LivenessCensus census(const World& world);

}  // namespace fwg
