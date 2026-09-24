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

// The speaker, or nullopt for a uid the world does not name.
std::optional<Speaker> speaker(const fwg::World& world, const std::string& uid);

// Every place within the speaker's reach, nearest first: cities, hamlets, fortresses and key
// locations. Empty when the speaker stands nowhere the world says.
std::vector<KnownPlace> places_in_reach(const fwg::World& world, Speaker& speaker);

// The nests within kDangerReachM of the speaker (never beyond their reach), nearest first.
std::vector<Fact> dangers_near(const fwg::World& world, const Speaker& speaker);

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

// "3.4 km", or "here" under fifty metres.
std::string distance_text(double metres);

}  // namespace orchestrator::knowledge
