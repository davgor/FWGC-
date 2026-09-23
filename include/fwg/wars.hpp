// Age-transition wars: which neighbours are in contention, and who is left standing.
//
// Cities quarrel over the two things the simulation already gives them: ground and food.
// Two cities nearer than the spacing their founders were meant to keep are pressed against
// each other's hinterland, and a city whose own land cannot feed it lives off whatever its
// roads reach, which is some neighbour's ground whether either of them likes it. Both are
// measured from the world as it stands at the end of an age, never invented here.
//
// A war is named for the relationship between its participants, not its cause: inside one
// civilization it is civil, inside one parent race regional, and across parent races
// international.
//
// An artistic history, not a campaign: no front line, no season of manoeuvre and no
// casualty model. One exchange, decided by the people and forts each city already has, and
// the loser becomes a ruin like any other lost city.
//
// Like the villain passes, nothing here reaches into `World` or writes anything. Every pass
// takes what it reads and returns what it decided; the age transition keeps what it keeps.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/grid.hpp"
#include "fwg/humans.hpp"
#include "fwg/roads.hpp"
#include "fwg/settlement.hpp"
#include "fwg/vec3.hpp"
#include "fwg/villains.hpp"

namespace fwg {

// ---------------------------------------------------------------------------
// Constants. Every one of these is seed-visible.
// ---------------------------------------------------------------------------

inline constexpr std::string_view kWarCivil = "civil";
inline constexpr std::string_view kWarRegional = "regional";
inline constexpr std::string_view kWarInternational = "international";

// Two cities are pressed together once they stand nearer than this many settlement
// spacings. At exactly the limit there is no quarrel.
//
// SCALES. It multiplies settlement_spacing, which recipe 3 resolves as 450 m *
// reach_scale(circumference): the limit is 12 109 m on the 200 km world, about eight cells
// at size 129 and two at size 33, and the same share of the planet at every width.
inline constexpr double kWarProximityFactor = 1.5;

// A villain whose reach covers both cities of a pair adds up to this much contention,
// falling linearly to nothing at its reach. SCALES: reach_m is itself a multiple of
// settlement_spacing, so no metre constant is authored here.
inline constexpr double kWarVillainContention = 0.35;

// A contested pair usually fights, and heavy pressure nearly assures it.
//
// THE CAP NEVER BINDS, faithfully. Pressure is clamped to 1 before it is scaled, so the
// chance tops out at 0.5 + 0.4 = 0.9 and 0.92 is unreachable. Kept as the reference wrote
// it, because raising the gain to where the cap bites is a change to every world.
inline constexpr double kWarBaseChance = 0.5;
inline constexpr double kWarPressureGain = 0.4;
inline constexpr double kWarMaxChance = 0.92;

// What a standing fortress is worth to its city's side of a war, in residents.
inline constexpr long long kWarFortStrength = 50;

// ---------------------------------------------------------------------------
// What a war reads.
// ---------------------------------------------------------------------------

// One line of a city's own war record. Both sides carry every war, so a survivor's history
// says what it has already fought -- which is what thickens a veteran's walls.
struct WarParticipation {
    std::string war_id;
    int age = 0;
    std::string kind;
    std::string outcome;  // victor | defeated
    std::string opponent_uid;
    std::string opponent_civilization_id;
    std::string opponent_parent_race_id;
};

// A standing city, as every age-turn pass reads one: the wars here, and the fate lottery and
// threat assessment in fates.hpp, which take this same record so an age transition builds
// one list and hands it to all three.
//
// TWO KEYS. `uid` is the stable identity every war, fate, ruin and report is keyed on.
// `id` is the INTEGER site index the hinterland cores and the roads name, and it renumbers
// on every rebuild -- it is read only to find a city's core and its roads.
struct WarCity {
    std::string uid;
    int id = 0;
    int node = 0;
    int x = 0;  // the ley grid cell
    int z = 0;
    Vec3 direction{};  // unit vector of that cell
    std::string name;
    std::string population_profile;  // the civilization
    std::string parent_race_id;
    CityClass city_class = CityClass::Small;
    long long population_estimate = 0;
    std::vector<WarParticipation> war_history;
};

// The ground contention is measured on. Shared by the lottery and the forecast, because
// the forecast promises to be the same pair pressure the next age draws against -- and two
// request shapes is how two readings of one quantity stop agreeing.
struct WarGround {
    // METRES. world.effective.globe_radius, not world.config's: the two differ by the
    // world scale, a factor of 5.6 on the default world, and design units here would
    // compare every separation against a spacing in the wrong units.
    double globe_radius_m = 1.0;
    // METRES and ALREADY SCALED: world.effective.settlement_spacing, 8072.5 m on the 200 km
    // world. The proximity limit is a multiple of it, never an authored metre figure.
    double settlement_spacing_m = 450.0;

    std::vector<WarCity> cities;
    // The hinterland of the same cities, keyed by `site_id` == `WarCity::id`. Absent, or a
    // city with no core, contributes no supply pressure and no forts.
    const std::vector<CityCore>* cores = nullptr;
    // Only `from` and `to` are read, in either orientation. Absent means no roads.
    const std::vector<Road>* routes = nullptr;
    // The STANDING cast. The pressure reads `reach_m` with no status test of its own, and
    // `advance_villains` refreshes a reach before it decides a fall -- so a fallen villain
    // handed in here presses cities together from beyond its grave.
    std::vector<Villain> villains;
};

// A pair under territorial or supply pressure.
struct WarPair {
    // Positions in `WarGround::cities`, a < b. NOT site ids.
    int a = 0;
    int b = 0;
    std::string a_uid;
    std::string b_uid;
    double distance_m = 0.0;
    double proximity_pressure = 0.0;
    double supply_pressure = 0.0;
    double villain_pressure = 0.0;
    double pressure = 0.0;  // min(1, proximity + supply + villain)
    std::string kind;
};

// One war. THE KEY FIELD IS `id`: `war-<age>-<ordinal>`, where the ordinal counts wars
// FOUGHT this age, not pairs examined.
struct WarRecord {
    std::string id;
    int age = 0;
    std::string kind;
    std::array<std::string, 2> participants;  // [a_uid, b_uid], in pair order
    std::string victor_uid;
    std::string defeated_uid;
    std::string victor_civilization_id;
    std::string defeated_civilization_id;
    std::string victor_parent_race_id;
    std::string defeated_parent_race_id;
    double distance_m = 0.0;
    double proximity_pressure = 0.0;
    double supply_pressure = 0.0;
    double pressure = 0.0;
    long long victor_strength = 0;
    long long defeated_strength = 0;
    double chance = 0.0;
    double roll = 0.0;
    std::string reason;
    // Written only when `war_survival` is above zero, so a default world's records keep the
    // shape they had before the option existed. `outcome` is vassalage | ruin.
    std::string outcome;
    std::optional<double> survival_chance;
    std::optional<double> survival_roll;
};

// How a war ended a city, in the shape the fate lottery produces so the age transition
// ruins it through the one path that already exists (`war_ruin_fate` in fates.hpp).
struct WarFate {
    std::string cause;  // war_<kind>
    std::string reason;
    // evidence
    std::string war_id;
    std::string kind;
    std::string opponent_uid;
    std::string opponent_civilization_id;
    std::string opponent_parent_race_id;
    double distance_m = 0.0;
    double pressure = 0.0;

    double probability = 0.0;
    double roll = 0.0;
    // The school that already held the defeated city's ground, empty where none held it
    // clearly or magic is off. NOTE the age transition discards this: it re-resolves the
    // scar through the ruin legacy, which always names a school. See `war_ruin_fate`.
    std::string new_node_school;
};

// What a living city can expect of war. `war_risk` is the strongest contention pressure a
// living neighbour puts on it; `war_hunger` the pressure it puts on a neighbour because it
// cannot feed its people. Empty strings are the reference's nulls.
struct WarOutlook {
    int wars_recent = 0;
    bool enemy_living = false;
    double war_risk = 0.0;
    std::string war_risk_kind;
    std::string war_risk_opponent_uid;
    double war_hunger = 0.0;
    std::string war_hunger_target_uid;
};

struct WarRequest {
    WarGround ground;
    std::uint32_t seed = 0;
    int age = 0;
    bool magic_enabled = true;
    // World option `war_survival`, 0..1: the chance a defeated city stands on as the
    // victor's vassal instead of falling. Zero, the default, draws nothing extra.
    double survival = 0.0;
    // The ley_<school> potency grids, read for the key-point school a loser leaves.
    const std::map<std::string, Grid>* layers = nullptr;
    // Which school holds a cell. Pass world.magic's published mutation_threshold and
    // dominance_margin -- the numbers the biome variants read -- so the ground and the war
    // cannot disagree about which school held it. These defaults are those numbers.
    double dominance_threshold = 0.35;
    double dominance_margin = 0.08;

    // Test seams, keyed by war id with "*" as the default. PRESENT-BUT-SILENT is a roll of
    // 1.0, which fights nothing -- a map that names neither the id nor "*" is not a fallback
    // to the seeded draw.
    std::optional<std::map<std::string, double>> rolls;
    std::optional<std::map<std::string, double>> survival_rolls;
};

struct WarResolution {
    std::vector<WarRecord> wars;  // in the order they were fought
    // Keyed by the DEFEATED city's uid. A vassal has no entry: it stands.
    std::map<std::string, WarFate> fates;
};

// ---------------------------------------------------------------------------
// The pieces, exposed because they are the properties worth testing on their own.
// ---------------------------------------------------------------------------

// Civil inside one civilization, regional inside one parent race, else international.
std::string_view war_kind(const WarCity& a, const WarCity& b);

// The share of its own demand a city's hinterland cannot feed, in 0..1. Zero for no core,
// for no demand, and for a surplus.
//
// Deliberately the PRE-TRADE figure, from what its own land delivers against what its
// people need. A city covered only because a neighbour exports is exactly the city under
// pressure here, and reading the post-trade deficit would hide the thing being measured.
double war_shortfall(const CityCore* core);

// min(cap, base + gain * clamp01(pressure)).
double war_chance(double pressure);

// People plus the forts already on its roads. A city with no core brings its people alone.
long long war_strength(const WarCity& city, const CityCore* core);

// How hard the seated villains press two cities together, in 0..kWarVillainContention: the
// worst single villain whose reach covers BOTH, falling off with the farther of the two.
double war_villain_pressure(const WarCity& a, const WarCity& b, const std::vector<Villain>& villains,
                            double radius_m);

// Every pair under territorial or supply pressure, strongest contention first, then by
// (a_uid, b_uid) -- so the same world presents the same quarrels in the same order. A
// villain presses a pair together but cannot create one: proximity or supply must already
// be there.
std::vector<WarPair> contested_pairs(const WarGround& ground);

// What each living city can expect, keyed by uid. `age` counts `wars_recent` and is absent
// before any age has turned (stage 13), when nothing is recent.
std::map<std::string, WarOutlook> war_outlook(const WarGround& ground, std::optional<int> age);

// Fight every contested pair that draws for it.
//
// A city can be dragged into several wars but lost only once: a pair whose participant has
// already fallen, or already been spared as a vassal, is skipped without a draw. Draws come
// from a stream named for the pair, `war-<a uid>-<b uid>` at variation `age`, so a pair's
// roll does not depend on which other pairs were fought.
WarResolution resolve_wars(const WarRequest& request);

// One city's line in its own war history.
WarParticipation war_participation(const WarRecord& war, std::string_view uid);

// How many wars a city carries, whatever it did in them.
int veteran_wars(const WarCity& city);

}  // namespace fwg
