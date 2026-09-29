// What a speaker knows: who they present as, where they stand, how far they know, and the
// world within that reach -- read through the generator's public reads and nothing else.
//
// Every fact built here names the world id it came from, and none is invented: a place is a
// row the near read returned, a danger is a nest it returned, a quest is a board row. What a
// speaker would not know stays out: a hidden face, a hook's actual effect, whether a stated
// purpose is the true one, and places beyond their reach.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fwg/api/read.hpp"
#include "fwg/api/world.hpp"
#include "orchestrator/contract.hpp"

namespace orchestrator::knowledge {

// How far a speaker knows the world, in metres, by who they are. A person knows the land a
// day or two's travel around where they stand, and a renowned hero knows further; each reach
// is clamped to the near read's own ceiling (half the globe's radius), so on a small world
// everyone's reach is the ceiling. Tunable, and nothing depends on the exact numbers.
inline constexpr double kHamletFolkReachM = 10000.0;   // a roster person at a hamlet or fortress
inline constexpr double kCityFolkReachM = 20000.0;     // a roster person in a city
inline constexpr double kNotableReachM = 25000.0;      // a hero of the notable band
inline constexpr double kRenownedReachM = 40000.0;     // renowned
inline constexpr double kLegendaryReachM = 60000.0;    // legendary
inline constexpr double kDreadReachM = 20000.0;
inline constexpr double kMinimumVillainReachM = 10000.0;  // a villain's own reach, at least this

// Dangers are what is close: the nests within this, never further than the speaker's reach.
inline constexpr double kDangerReachM = 5000.0;

// What one read may describe: the nearest places, and the nearest dangers.
inline constexpr std::size_t kMaxPlaces = 12;
inline constexpr std::size_t kMaxDangers = 5;

// A place the speaker knows: the fact they would state, and where it stands.
struct KnownPlace {
    Fact fact;
    int node = 0;
    double distance_m = 0.0;
};

struct Speaker {
    std::string uid;
    fwg::PersonDocument document;
    bool present = true;
    std::optional<int> node;  // where they stand; nullopt when the world does not say
    double reach_m = 0.0;     // after the ceiling, and after any narrowing a dense read needed
    Persona persona;
};

// The speaker, or nullopt for a uid the world does not name. They present the face a stranger
// sees.
std::optional<Speaker> speaker(const fwg::World& world, const std::string& uid);

// A hero's record, when the speaker is one; nullptr otherwise.
const fwg::Hero* hero_of(const Speaker& speaker);

// Contract version 25: fame. The realm a speaker belongs to: a hero's own, else the realm of the city their
// home is; a roster person's, the realm whose cities hold their site, a hamlet or a fortress by the city it
// belongs to. Empty for none.
std::string realm_of(const fwg::World& world, const Speaker& speaker);

// A realm's sovereign, or empty: about one realm in ten has none.
std::string sovereign_of(const fwg::World& world, const std::string& realm_uid);

// The deed a hero is known for (heroes version 4), or nullptr for a record with none.
const fwg::HeroDeed* known_deed(const fwg::Hero& hero);

// How far a hero's name carries from where they stand when their known deed went through a region: as far as a
// renowned hero's own knowledge reaches, since a deed of renown is a regional one; clamped, as every reach is,
// to the near read's ceiling, which only a large world's passes. Tunable.
inline constexpr double kRegionalFameM = kRenownedReachM;

// Whether `speaker` knows `hero` by fame, never having met them: the sovereign of their own realm ("a common
// NPC should know the legendary heroes or kings in their land, even if they have never met them": the owner,
// 2026-09-29), and a hero whose known deed's word reaches them: through the hero's realm, to anyone of it;
// through a region, to anyone within kRegionalFameM of where the hero stands (`hero_node`). Word that went
// through a realm went at least as far as a region's does, past its borders too.
bool known_by_fame(const fwg::World& world, const Speaker& speaker, const fwg::Hero& hero, std::optional<int> hero_node);

// A Deed fact of one of a hero's deeds (contract version 13's fields): what, their part, where, the war's kind
// and whom it was against, when.
Fact deed_fact(const fwg::World& world, const fwg::HeroDeed& deed);

// The people the words a player said name (contract version 25), in the world's order. Words that ask for a
// sovereign ("king", "queen", "rules"...) name a realm's: the realm whose own name or a city's the words hold
// ("the queen of Linbrig"), else, for a title and nothing more ("the king", "who rules here"), `speaker`'s own.
// Otherwise, and for a title with a name ("King Harwin"), every hero, then every roster person, whose name's
// every word the said words hold, case aside. The caller keeps the first the speaker knows.
std::vector<std::string> people_named(const fwg::World& world, const Speaker& speaker, const std::string& words);

// A hero's ties, each with how they stand to the hero ("liege", "rival", "kin", "ally"): the sovereign
// a castellan is sworn to (contract version 19), then rivals, then kin, then allies, each in the record's
// own order. The one place a tie is named, for what a hero says of
// themself and for another giver's work in their list.
std::vector<std::pair<std::string, std::string>> ties_of(const fwg::Hero& hero);

// The face a stranger sees: the public face, else the first of two, else the calm one.
// nullptr for a hero with faces and none of those.
const fwg::HeroFace* stranger_face(const fwg::Hero& hero);

// The speaker as `face` presents: its name, role, card and written lines. A mask (a public face
// over a true one) speaks from its trade's lines, so it names no voice; any other face speaks as
// its archetype and its own alignment ("beast/CE"). Persona::face is set for a face a stranger
// does not see.
void show_face(Speaker& speaker, const fwg::Hero& hero, const fwg::HeroFace& face);

// A place fact's name, as a person would say it: "Wenweg", "the hamlet of Wenweg".
std::string place_label(const Fact& place);

// Every place within the speaker's reach, nearest first: cities, hamlets, fortresses and key
// locations. Empty when the speaker stands nowhere the world says.
std::vector<KnownPlace> places_in_reach(const fwg::World& world, Speaker& speaker);

// The nests within kDangerReachM of the speaker (never beyond their reach), the worst first, and
// the nearest first among equals: what someone would warn about.
std::vector<Fact> dangers_near(const fwg::World& world, const Speaker& speaker);

// The nests within `radius_m` of a place's node, the worst first and the nearest first among
// equals, at most `most`: Danger facts marked `around`, their `range` from the place.
std::vector<Fact> nests_around(const fwg::World& world, int node, double radius_m, std::size_t most);

// Who a settlement's roster says is there: up to three of its trades by role, the roster's own
// posts first, and the uids of up to two heroes who hold a post there, never `speaker` (asked
// about their own town, a speaker does not name themself). Both empty for a place with no
// roster, a key location.
struct PlaceFolk {
    std::vector<std::string> trades;
    std::vector<std::string> heroes;
};
PlaceFolk folk_at(const fwg::World& world, const std::string& place_id, const std::string& speaker);

// What stands on one node, as a place fact: a quest anchor's target node, or where a person
// stands. nullopt when nothing named stands there.
std::optional<Fact> place_at(const fwg::World& world, int node);

// A quest anchor by its own identity: a city, the ruins of one, a hamlet or fortress, a
// landmark, a nest, or a relic and the ruins it rests in. nullopt for what the world's blocks
// do not name, a ley node or a shrine, whose offer's stated purpose says where it is. Never
// whatever else happens to stand on the anchor's node: a hamlet built on a ruin is not the ruin.
std::optional<std::string> anchor_name(const fwg::World& world, const std::string& anchor);

// A person as the speaker would describe them: their public name, apparent role, where they
// stand and whether they are still about -- and the node they stand on, which decides whether
// the speaker would know them. nullopt for a villain (who a villain is, is not what ordinary
// talk discloses) or a uid the world does not name.
struct KnownPerson {
    Fact fact;
    std::optional<int> node;
};
std::optional<KnownPerson> person_fact(const fwg::World& world, const std::string& uid);

// A person's name and role as a stranger has them: the face a stranger sees, a roster post, a
// villain's role alone.
std::pair<std::string, std::string> known_as(const fwg::PersonDocument& document);

// The terrain nodes of the places -- cities, hamlets, fortresses, key locations -- within
// `radius_m` of `node`, the node itself among them when a place stands there: where word spreads
// to. No further than the near read's ceiling (half the globe's radius), as every reach here is,
// and fewer when the read had to narrow its radius.
std::vector<int> place_nodes_within(const fwg::World& world, int node, double radius_m);

// How far a thing is, as a speaker would put it: a band, never metres, which nobody standing
// there has measured. contract.hpp lists the bands.
std::string range_of(double metres);
// What the speaker would say of themself when asked (contract version 13): a Self fact, then their
// Deed facts, then a Person fact for each of their ties, marked `tie`. Public only: a hero's record
// beyond their card, never the card's secrets. `whole_story` false (a mask still worn) gives the Self
// fact's role and place alone. contract.hpp lists the fields.
std::vector<Fact> self_facts(const fwg::World& world, const Speaker& speaker, bool whole_story);
// The most ties and deeds an answer about the speaker names.
inline constexpr std::size_t kTiesNamed = 3;
inline constexpr std::size_t kDeedsNamed = 3;

}  // namespace orchestrator::knowledge
