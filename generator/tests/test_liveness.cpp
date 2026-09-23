// One predicate for "is this person still here", across four blocks that disagree.
//
// The tests that matter are about the seams rather than the words: that a block is never
// read as a person (the depth trap), that an absent status reads as present (the world of
// ghosts), that the villains' rule is delegated rather than restated, and that the only
// hand-written translation between two published contracts -- the NPC roster's mirror of a
// hero -- is the one these tables publish.
#include "check.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "fwg/liveness.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

// Whether `liveness` accepts a T at all. A block handed where a person belongs must not
// compile -- the typed form of the reference's run-time BLOCK_ONLY_KEYS refusal.
template <typename T>
concept has_liveness = requires(const T& record) { liveness(record); };

const World& generated() {
    static const World world = [] {
        Request r;
        r.seed = 42;
        r.size = 33;
        r.phase = 16;
        return generate(resolve(r));
    }();
    return world;
}

Hero hero_with(std::string uid, std::string status) {
    Hero h;
    h.uid = std::move(uid);
    h.status = std::move(status);
    return h;
}

NpcPerson npc_with(std::string uid, bool alive) {
    NpcPerson p;
    p.uid = std::move(uid);
    p.alive = alive;
    return p;
}

}  // namespace

TEST(liveness_every_vocabulary) {
    check::is_true(liveness_of_status(LivenessBlock::Heroes, "living") == Liveness::Present, "a living hero");
    check::is_true(liveness_of_status(LivenessBlock::Heroes, "legend") == Liveness::Gone, "a legend");
    check::is_true(liveness_of_status(LivenessBlock::Dreads, "living") == Liveness::Present, "a living dread");
    check::is_true(liveness_of_status(LivenessBlock::Dreads, "legend") == Liveness::Gone, "a dread passed to legend");
    check::is_true(liveness_of_status(LivenessBlock::Npcs, "alive") == Liveness::Present, "an npc alive");
    check::is_true(liveness_of_status(LivenessBlock::Npcs, "dead") == Liveness::Gone, "an npc dead");
    check::is_true(liveness_of_status(LivenessBlock::Villains, "living") == Liveness::Present, "a living villain");
    check::is_true(liveness_of_status(LivenessBlock::Villains, "fallen") == Liveness::Gone, "a fallen villain");

    // The typed records, dispatched on the block the TYPE belongs to.
    check::is_true(liveness(hero_with("h", "legend")) == Liveness::Gone, "a Hero record");
    HeroDread dread;
    dread.status = "living";
    check::is_true(liveness(dread) == Liveness::Present, "a HeroDread record");
    check::is_true(liveness(npc_with("n", false)) == Liveness::Gone, "an NpcPerson record");
    Villain villain;
    villain.status = "fallen";
    check::is_true(liveness(villain) == Liveness::Gone, "a Villain record");

    // Another block's word is not this block's present word: a hero cannot be "alive".
    check::is_true(liveness_of_status(LivenessBlock::Heroes, "alive") == Liveness::Gone,
                   "a hero read in the npc vocabulary is not present");
}

TEST(liveness_absent_status_reads_as_present) {
    // A record written before its package recorded departure described someone who stood.
    for (const LivenessBlock block : liveness_blocks()) {
        check::is_true(liveness_of_status(block, "") == Liveness::Present,
                       std::string(to_string(block)) + " with no status");
    }
    check::is_true(is_present(Hero{}), "a default Hero");
    check::is_true(is_present(HeroDread{}), "a default HeroDread");
    check::is_true(is_present(NpcPerson{}), "a default NpcPerson: alive is the absent key");
    check::is_true(is_present(Villain{}), "a default Villain");
}

TEST(liveness_a_block_is_not_a_person) {
    // `status` is ok/failed on the block and living/legend on a person, under one key name
    // at two depths. The typed predicate refuses a block at compile time.
    static_assert(has_liveness<Hero>, "a hero is a person");
    static_assert(has_liveness<HeroDread>, "a dread is a person");
    static_assert(has_liveness<NpcPerson>, "an npc is a person");
    static_assert(has_liveness<Villain>, "a villain is a person");
    static_assert(!has_liveness<HeroesBlock>, "the heroes block is not a person");
    static_assert(!has_liveness<NpcRoster>, "the npc roster is not a person");
    static_assert(!has_liveness<VillainBlock>, "the villain block is not a person");
    // The string form has the reference's exposure and no more: a block's word read as a
    // person's is simply not the present word.
    check::is_true(liveness_of_status(LivenessBlock::Heroes, "ok") == Liveness::Gone,
                   "a block-level 'ok' is not a living person");
}

TEST(liveness_an_unknown_block_or_state_is_a_caller_error) {
    check::is_true(!parse_liveness_block("nomads").has_value(), "nomads carry no liveness");
    check::is_true(!parse_liveness_block("people").has_value(), "a list name is not a block");
    check::is_true(!parse_liveness_block("").has_value(), "nor is nothing");
    check::throws([] { (void)token("nomads", "present"); }, "an unknown block");
    check::throws([] { (void)token("settlements", "gone"); }, "another unknown block");
    check::throws([] { (void)token("heroes", "missing"); }, "an unknown state");
    check::throws([] { (void)token("heroes", "living"); }, "a block word is not a state");
    check::equal(std::string(token("npcs", "gone")), std::string("dead"), "the named form answers");
    check::is_true(!parse_liveness("dead").has_value(), "a block word is not the published pair");
    check::is_true(parse_liveness("present") == Liveness::Present, "present parses");
    check::is_true(parse_liveness("gone") == Liveness::Gone, "gone parses");
}

TEST(liveness_villains_delegate_to_is_standing) {
    // Two implementations of one rule is the defect this module exists to prevent.
    for (const char* status : {"living", "fallen", "", "legend", "dead", "LIVING"}) {
        Villain villain;
        villain.status = status;
        check::equal(is_present(villain), is_standing(villain), std::string("villain status '") + status + "'");
        check::equal(liveness_of_status(LivenessBlock::Villains, status) == Liveness::Present, is_standing(villain),
                     std::string("the string form for '") + status + "'");
    }
}

TEST(liveness_present_filters_in_order) {
    const std::vector<NpcPerson> people{npc_with("a", true), npc_with("b", false), npc_with("c", true)};
    const std::vector<const NpcPerson*> here = present(std::span<const NpcPerson>(people));
    check::equal(here.size(), std::size_t{2}, "two still here");
    check::equal(here[0]->uid, std::string("a"), "first in the order given");
    check::equal(here[1]->uid, std::string("c"), "then the next");

    const std::vector<Hero> heroes{hero_with("x", "legend"), hero_with("y", ""), hero_with("z", "living")};
    const std::vector<const Hero*> standing = present(std::span<const Hero>(heroes));
    check::equal(standing.size(), std::size_t{2}, "the absent status counts as here");
    check::equal(standing[0]->uid, std::string("y"), "in order");
    check::equal(standing[1]->uid, std::string("z"), "in order");
}

TEST(liveness_the_tables_round_trip_and_publish_no_new_word) {
    // What the writer writes is what the reader reads back, in each block's own words.
    for (const LivenessBlock block : liveness_blocks()) {
        check::equal(token(block, Liveness::Present), present_status(block), std::string(to_string(block)));
        check::equal(token(block, Liveness::Gone), gone_status(block), std::string(to_string(block)));
        for (const Liveness state : {Liveness::Present, Liveness::Gone}) {
            check::is_true(liveness_of_status(block, token(block, state)) == state,
                           std::string(to_string(block)) + " round trip of " + std::string(to_string(state)));
        }
        check::is_true(parse_liveness_block(to_string(block)) == block, "the block name parses back");
    }
    std::set<std::string> published;
    for (const std::string_view word : published_statuses()) published.insert(std::string(word));
    check::is_true(published == std::set<std::string>{"living", "legend", "alive", "dead", "fallen"},
                   "exactly the five words the blocks already use");
    check::equal(published_statuses().size(), std::size_t{5}, "each once");
    check::equal(liveness_blocks().size(), kLivenessBlockCount, "four blocks");
    check::equal(std::string(to_string(liveness_blocks()[0])), std::string("heroes"), "in the reference's order");
    check::equal(std::string(to_string(liveness_blocks()[3])), std::string("villains"), "in the reference's order");
}

TEST(liveness_the_census_skips_a_failed_package) {
    // Zero present people and no package at all are different facts.
    World world;
    world.has_heroes = true;
    world.heroes.status = "failed";
    world.heroes.people.push_back(hero_with("h", "living"));
    world.has_npcs = true;
    world.npcs.ok = true;
    world.npcs.people = {npc_with("a", true), npc_with("b", false)};
    const LivenessCensus counts = census(world);
    check::is_true(!counts.at(LivenessBlock::Heroes).has_value(), "a failed heroes package counts nobody");
    check::is_true(!counts.at(LivenessBlock::Dreads).has_value(), "and neither do its dreads");
    check::is_true(!counts.at(LivenessBlock::Villains).has_value(), "no villain block, no count");
    check::is_true(counts.at(LivenessBlock::Npcs).has_value(), "an ok roster is counted");
    check::equal(counts.at(LivenessBlock::Npcs)->present, 1, "one alive");
    check::equal(counts.at(LivenessBlock::Npcs)->gone, 1, "one dead");

    World empty;
    empty.has_heroes = true;  // ok, with nobody in it
    const LivenessCensus quiet = census(empty);
    check::is_true(quiet.at(LivenessBlock::Heroes).has_value(), "an ok block with nobody is still a block");
    check::equal(quiet.at(LivenessBlock::Heroes)->present, 0, "counted as zero");
}

TEST(liveness_the_census_counts_a_generated_world) {
    const World& world = generated();
    const LivenessCensus counts = census(world);
    check::is_true(world.has_heroes && world.heroes.status == "ok", "the fixture's cast to have generated");
    const auto total = [](const std::optional<LivenessCount>& c) { return c ? c->present + c->gone : -1; };
    check::equal(total(counts.at(LivenessBlock::Heroes)), static_cast<int>(world.heroes.people.size()), "heroes");
    check::equal(total(counts.at(LivenessBlock::Dreads)), static_cast<int>(world.heroes.dreads.size()), "dreads");
    check::equal(total(counts.at(LivenessBlock::Npcs)), static_cast<int>(world.npcs.people.size()), "npcs");
    check::equal(total(counts.at(LivenessBlock::Villains)), static_cast<int>(world.villains.people.size()),
                 "villains");
    // Non-vacuity: a cast in which nobody has passed to legend tests only one word.
    check::is_true(counts.at(LivenessBlock::Heroes).has_value(), "the heroes counted");
    check::is_true(counts.at(LivenessBlock::Heroes)->present > 0 && counts.at(LivenessBlock::Heroes)->gone > 0,
                   "both living heroes and legends in the fixture");
}

TEST(liveness_the_roster_mirror_of_a_hero_is_the_published_mapping) {
    // The only translation between two published contracts: the roster copies each hero into
    // its people as a cast record, turning the hero's word into its own. It is bound here to
    // the published tables rather than restated, so it fails if either side moves.
    const World& world = generated();
    std::map<std::string, const Hero*> heroes;
    for (const Hero& h : world.heroes.people) heroes[h.uid] = &h;
    int cast = 0;
    std::set<bool> seen;
    for (const NpcPerson& person : world.npcs.people) {
        if (!person.is_cast) continue;
        const auto it = heroes.find(person.hero_uid);
        check::is_true(it != heroes.end(), person.uid + " to mirror a hero of this world");
        const bool expected = it->second->status == present_status(LivenessBlock::Heroes);
        check::equal(person.alive, expected, person.uid + " alive exactly when its hero is " +
                                                 std::string(present_status(LivenessBlock::Heroes)));
        check::equal(std::string(token(LivenessBlock::Npcs, person.alive ? Liveness::Present : Liveness::Gone)),
                     std::string(person.alive ? "alive" : "dead"), person.uid + " in the roster's own word");
        seen.insert(person.alive);
        ++cast;
    }
    check::is_true(cast > 0, "some cast record in the roster, or nothing is bound");
    check::is_true(seen.size() == 2, "both a living and a legendary hero mirrored, or only one word is bound");
}
