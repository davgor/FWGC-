#include "fwg/liveness.hpp"

#include <stdexcept>
#include <string>

#include "fwg/world.hpp"

namespace fwg {
namespace {

struct BlockRow {
    LivenessBlock block;
    std::string_view name;
    std::string_view present;  // PRESENT_STATUS
    std::string_view gone;     // GONE_STATUS
};

// The reference's two tables, as one. Order is the reference's table order and is what
// liveness_blocks() publishes.
constexpr BlockRow kBlocks[] = {
    {LivenessBlock::Heroes, "heroes", "living", "legend"},
    {LivenessBlock::Dreads, "dreads", "living", "legend"},
    {LivenessBlock::Npcs, "npcs", "alive", "dead"},
    {LivenessBlock::Villains, "villains", "living", "fallen"},
};

constexpr LivenessBlock kBlockOrder[] = {LivenessBlock::Heroes, LivenessBlock::Dreads, LivenessBlock::Npcs,
                                         LivenessBlock::Villains};

static_assert(sizeof(kBlocks) / sizeof(kBlocks[0]) == kLivenessBlockCount, "one row per block");

const BlockRow& row_of(LivenessBlock block) { return kBlocks[static_cast<std::size_t>(block)]; }

Liveness of(bool here) { return here ? Liveness::Present : Liveness::Gone; }

template <typename Person>
std::vector<const Person*> still_here(std::span<const Person> people) {
    std::vector<const Person*> out;
    for (const Person& person : people) {
        if (liveness(person) == Liveness::Present) out.push_back(&person);
    }
    return out;
}

template <typename Person>
LivenessCount count_of(const std::vector<Person>& people) {
    LivenessCount counts;
    for (const Person& person : people) {
        if (liveness(person) == Liveness::Present) {
            ++counts.present;
        } else {
            ++counts.gone;
        }
    }
    return counts;
}

}  // namespace

std::string_view to_string(Liveness state) {
    return state == Liveness::Present ? kLivenessPresent : kLivenessGone;
}

std::optional<Liveness> parse_liveness(std::string_view word) {
    if (word == kLivenessPresent) return Liveness::Present;
    if (word == kLivenessGone) return Liveness::Gone;
    return std::nullopt;
}

std::span<const LivenessBlock> liveness_blocks() { return std::span<const LivenessBlock>(kBlockOrder); }

std::string_view to_string(LivenessBlock block) { return row_of(block).name; }

std::optional<LivenessBlock> parse_liveness_block(std::string_view name) {
    for (const BlockRow& row : kBlocks) {
        if (row.name == name) return row.block;
    }
    return std::nullopt;
}

std::string_view present_status(LivenessBlock block) { return row_of(block).present; }

std::string_view gone_status(LivenessBlock block) { return row_of(block).gone; }

std::vector<std::string_view> published_statuses() {
    std::vector<std::string_view> out;
    const auto add = [&out](std::string_view word) {
        for (const std::string_view seen : out) {
            if (seen == word) return;
        }
        out.push_back(word);
    };
    for (const BlockRow& row : kBlocks) {
        add(row.present);
        add(row.gone);
    }
    return out;
}

std::string_view token(LivenessBlock block, Liveness state) {
    return state == Liveness::Present ? present_status(block) : gone_status(block);
}

std::string_view token(std::string_view block, std::string_view state) {
    const std::optional<LivenessBlock> which = parse_liveness_block(block);
    if (!which) throw std::invalid_argument("Unknown liveness block: '" + std::string(block) + "'");
    const std::optional<Liveness> what = parse_liveness(state);
    if (!what) throw std::invalid_argument("Unknown liveness state: '" + std::string(state) + "'");
    return token(*which, *what);
}

Liveness liveness_of_status(LivenessBlock block, std::string_view status) {
    if (block == LivenessBlock::Villains) {
        // The villain rule is authored in the module that writes it, including its own
        // absent-reads-as-standing clause. Delegated, not restated.
        Villain probe;
        probe.status = std::string(status);
        return of(is_standing(probe));
    }
    const std::string_view word = status.empty() ? present_status(block) : status;
    return of(word == present_status(block));
}

Liveness liveness(const Hero& person) { return liveness_of_status(LivenessBlock::Heroes, person.status); }

Liveness liveness(const HeroDread& dread) { return liveness_of_status(LivenessBlock::Dreads, dread.status); }

// The roster stores the published alive/dead pair as a flag rather than a word; the flag's
// default is the absent key.
Liveness liveness(const NpcPerson& person) { return of(person.alive); }

Liveness liveness(const Villain& villain) { return of(is_standing(villain)); }

bool is_present(const Hero& person) { return liveness(person) == Liveness::Present; }
bool is_present(const HeroDread& dread) { return liveness(dread) == Liveness::Present; }
bool is_present(const NpcPerson& person) { return liveness(person) == Liveness::Present; }
bool is_present(const Villain& villain) { return liveness(villain) == Liveness::Present; }

std::vector<const Hero*> present(std::span<const Hero> people) { return still_here(people); }
std::vector<const HeroDread*> present(std::span<const HeroDread> dreads) { return still_here(dreads); }
std::vector<const NpcPerson*> present(std::span<const NpcPerson> people) { return still_here(people); }
std::vector<const Villain*> present(std::span<const Villain> villains) { return still_here(villains); }

LivenessCensus census(const World& world) {
    LivenessCensus out;
    // A failed heroes package carries no people at all, and neither list is counted: the
    // dreads live inside the same block and fail with it.
    if (world.has_heroes && world.heroes.status == "ok") {
        out.blocks[static_cast<std::size_t>(LivenessBlock::Heroes)] = count_of(world.heroes.people);
        out.blocks[static_cast<std::size_t>(LivenessBlock::Dreads)] = count_of(world.heroes.dreads);
    }
    if (world.has_npcs && world.npcs.ok) {
        out.blocks[static_cast<std::size_t>(LivenessBlock::Npcs)] = count_of(world.npcs.people);
    }
    // The villain block has no block-level status to fail on.
    if (world.has_villains) {
        out.blocks[static_cast<std::size_t>(LivenessBlock::Villains)] = count_of(world.villains.people);
    }
    return out;
}

}  // namespace fwg
