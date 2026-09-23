// Two deterministic habitat passes: animals, then monsters.
//
// Both are thinned point processes over the ground rather than a fixed anchor budget.
// For a species s in cell c the intensity is
//
//     lambda = density(tier) * occurrence * area_km2 * suitability * biome_weight * ramp
//
// so the expected count is an integral over habitable land and doubling the land doubles
// the creatures, with no cap anywhere. A cell places floor(lambda) + [u < frac(lambda)]
// groups and picks the species in proportion to its share.
//
// Tier is danger to a player, one to five, and it does the ecological work: density falls
// geometrically with it, so prey are many and apex creatures are few; territory and
// settlement clearance grow with it; and the danger ramp slides it outward from people.
//
// The two passes never read each other. Animals hold hunting grounds; monsters hold
// territory over the same ground, and a monster is excluded only by an EQUAL, so a
// dragon's range may contain a goblin camp whichever of the two was drawn first.
//
// Placement resolves at the raster cell, and that is the ceiling on all of it: every site
// in a cell carries the cell's own direction, so a bucket holds at most one site per cell
// whatever else the spacing rule does. `saturation` publishes the gap that leaves --
// cells reached, groups drawn, placed, refused -- because the pyramid is exact in the rate
// and clipped in what is placed, and a flat placed histogram is unreadable without both.
//
// These are static habitat proposals: no populations, migrations or runtime spawning.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/grid.hpp"
#include "fwg/vec3.hpp"

namespace fwg {

struct World;

// ---------------------------------------------------------------------------
// The authored catalogue. Generated from data/nest-profiles.json into
// src/generated/content_nests.cpp by tools/embed/nests.py.
// ---------------------------------------------------------------------------

enum class CreatureClass : int { Animal = 0, Monster = 1 };

// The only media a PROFILE may declare. `shore` is a land medium that additionally
// wants a coast: a shore species is matched against a land cell and then gated on
// `coast >= 0.1`, so it never matches water.
enum class CreatureMedium : int { Land = 0, Shore, Marine, Freshwater };

// What a CELL is, which is a wider vocabulary than what a profile may declare. A saline
// lake gets its own label that no profile can name, so nothing lives in a salt lake --
// and its area is still counted into the water budget, which thins every water species
// slightly. Inherited deliberately; see the note in src/nests.cpp.
enum class CellMedium : int { Land = 0, Marine, Freshwater, SaltLake };

// One (habitat field, number) pair out of a profile's `weights` or `requires`.
//
// `field` indexes nest_field_names(). These are HABITAT FIELDS, not biomes: some are raw
// layer names and seven are derived per cell. FILE ORDER is preserved because the
// weight-normalised score is a float sum over the terms in that order.
struct HabitatTerm {
    int field = 0;
    double value = 0.0;
};

// Biome ids run 0..17 with gaps; a table is a dense array over that range.
inline constexpr int kNestBiomeCount = 18;

struct CreatureProfile {
    std::string_view id;
    std::string_view name;
    CreatureClass creature_class = CreatureClass::Animal;
    int tier = 1;  // 1..5, DANGER to a player -- not scale, not a settlement tier
    // Feeding role; empty for monsters, which have none. An animal's biome table is
    // resolved through its role at generation time, so `biome_weights` below is already
    // the table that applies whichever half of the catalogue this row is in.
    std::string_view role;
    std::string_view family;
    std::string_view size;
    std::string_view kind;  // den | lair | aquatic territory | colony | roost
    CreatureMedium medium = CreatureMedium::Land;
    // Inclusive degrees Celsius, and a HARD gate.
    double temperature_min_c = 0.0;
    double temperature_max_c = 0.0;
    std::span<const HabitatTerm> weights;
    std::span<const HabitatTerm> requirements;  // `requires` is a C++20 keyword
    // sum(weights.value), precomputed in file order. The score divides by it, so
    // scaling every weight of a row changes nothing at all.
    double weight_total = 0.0;
    // Indexed by biome id. Zero is EXCLUSION, not neutrality: the authored tables are
    // sparse and an absent biome is ground the species does not use.
    std::span<const double> biome_weights;
    double spacing_m = 0.0;  // territory DIAMETER; the placed radius is half of it
    // This species' share of its danger tier, and nothing else. It divides the tier's
    // budget among the tier's members; it is not a multiplier on world population.
    double occurrence = 0.0;
    // Decided from the profile alone and true for every world: a positive `requires` on
    // a dormant ley school is a hard gate against a layer generation can never raise.
    bool never_satisfiable = false;

    // --- authored here, read elsewhere ------------------------------------
    // Nothing in a nest pass reads the five below, and no placed site copies them. They
    // are carried because the block publishes the catalogue and its consumers -- beast
    // movement, encounters, the asset list -- get them from there and from nowhere else.
    // Dropping them from the table would make this port look complete while quietly
    // deleting the data the next three stages need.
    std::string_view movement;  // nester | migratory | follower | drifter | irruptive
    bool sky = false;
    std::string_view likeness;           // art reference; monsters only
    std::span<const std::string_view> likeness_traits;
    std::string_view likeness_secondary;  // twelve monsters carry one

    // The authored `real` flag is NOT stored: it is asset identity and equals
    // (class == animal) on every row, which the codegen refuses to generate a table for
    // if it ever stops being true. A placed site recomputes it the same way.
};

struct CreatureRole {
    std::string_view name;
    std::span<const double> biome_weights;  // indexed by biome id
};

// The whole catalogue, stored GROUPED BY CLASS with file order preserved inside each
// group: all animals, then all monsters. The filtered sequences are what both passes
// consume and what the cumulative pick walks, so this storage order is exactly the
// contract; the interleaving of the two classes in the JSON file reaches nothing.
std::span<const CreatureProfile> creature_profiles();

// ORDER IS A CONTRACT. _place builds a per-cell candidate list in this order, float-sums
// the rates over it and walks it cumulatively, so reordering the same rows is a different
// world on the same seed.
std::span<const CreatureProfile> creature_catalogue(CreatureClass which);

// The feeding roles and the biomes they favour, shared by every animal that has one.
// Published by the wildlife block; monsters have no roles.
std::span<const CreatureRole> creature_roles();

// The interned habitat field names a profile's weights and requires may key. Seven of
// them are derived per cell rather than read from a layer; the rest name layers, and an
// absent layer reads 0.0 -- so a `requires` fails closed and a `weights` term contributes
// nothing rather than erroring.
std::span<const std::string_view> nest_field_names();

// The catalogue as authored, as JSON text (src/generated/content_nest_catalogue.cpp): the
// animal rows and the monster rows, each in file order with every authored key, and the
// feeding-role table. The world document's `profiles` and `roles` are these bytes; no pass
// reads them, because the typed tables above are what the passes run on.
std::string_view nest_animal_profiles_json();
std::string_view nest_monster_profiles_json();
std::string_view nest_roles_json();

// ---------------------------------------------------------------------------
// What a pass produces.
// ---------------------------------------------------------------------------

// Every site sits on the layer named here. Constant rather than a field: it is the same
// literal on every row and a per-row copy would only invite someone to vary it.
inline constexpr std::string_view kNestSiteLayer = "surface";

struct NestSite {
    // "<prefix>-<species_id>-<node>". The prefix is "animal" for wildlife and "nest" for
    // beast_nests -- the block name and the id prefix disagree, and that is published.
    std::string id;
    std::string_view species_id;
    std::string_view name;
    int node = 0;
    int x = 0;
    int z = 0;
    Vec3 direction;  // the CELL CENTRE, not a sub-cell position
    int biome = 0;
    int tier = 1;
    std::string_view family;
    std::string_view kind;
    std::string_view size;
    std::string_view role;  // empty for monsters
    CreatureClass creature_class = CreatureClass::Animal;
    double suitability = 0.0;  // the score that admitted it, >= nest_min_suitability
    double range_m = 0.0;      // territory RADIUS: spacing_m * nest_spacing / 2
    bool den = false;          // a den or lair is a place you can raid; a herd is not
    // Recomputed as (class == animal), NOT read from the profile row.
    bool real = false;
};

// Why a species is not in a world. Five values, because "absent" would otherwise bundle
// one catalogue defect and one placement rule with two ordinary facts about this world.
enum class NestReason : int {
    Placed = 0,
    LostTheDraw,
    AbsentFromThisWorld,
    // The only world-independent verdict.
    NeverSatisfiable,
    // NOT a world reason and must not be reported as one: a species whose habitat is the
    // ground people stand on can have all its eligible cells inside the clearance.
    ClearedOut,
};

// The published wording, verbatim.
std::string_view to_string(NestReason reason);

struct NestDiagnostic {
    std::string_view species_id;
    std::string_view name;
    int tier = 1;
    std::string_view role;  // published on wildlife rows; monsters have none
    long long placed = 0;   // after the nest_limit cap has rewritten the counts
    long long cells = 0;    // cells it could live in at all; NOT recomputed after the cap
    NestReason reason = NestReason::AbsentFromThisWorld;
};

// Per tier: what the rate asked for, what the raster could hold, and what survived.
struct NestSaturation {
    int tier = 1;
    long long cells = 0;    // raster cells the tier can reach: the ceiling on `placed`
    long long drawn = 0;    // groups the rate asked for, before any territory refusal
    long long placed = 0;
    long long refused = 0;  // drawn - placed
    // Rate-weighted mean distance from settled ground of the groups the intensity asks
    // for. This is the danger ramp integrated over the world and the only place the
    // gradient is exact. Absent when nothing is settled or the tier's rate is zero.
    std::optional<double> rate_distance_m;
    // Where the sites actually ended up. It barely moves with tier, because a saturated
    // tier is pinned to the cells it reaches and every tier reaches nearly the same
    // ground.
    std::optional<double> placed_distance_m;
};

struct NestReachability {
    long long authored = 0;           // this pass's catalogue, not all 680
    long long eligible = 0;           // profiles with at least one cell IN THIS WORLD
    long long placed = 0;             // profiles with at least one site IN THIS WORLD
    long long never_satisfiable = 0;  // proved from the profile alone
};

// The scope line that has to travel with those four integers: two of them are a sample.
std::string_view nest_reachability_scope();

struct NestBlock {
    int version = 0;
    std::vector<NestSite> sites;  // sorted by `id`, a LEXICOGRAPHIC string sort
    std::vector<NestDiagnostic> diagnostics;  // one row per profile, in catalogue order
    std::vector<NestSaturation> saturation;   // one row per live tier, ascending
    NestReachability reachability;
    std::span<const CreatureProfile> profiles;
    long long catalogue_count = 0;
    // Wildlife publishes the role tables; beast_nests leaves this empty.
    std::span<const CreatureRole> roles;
    std::string_view method;
    std::string_view limits;
    // The age whose ground the nests were placed on. An age transition places them on the
    // finished age first (age - 1) and again after its rebuild (age). Absent at stage 13,
    // where no age has turned.
    std::optional<int> evaluated_age;
};

struct NestPasses {
    NestBlock wildlife;
    NestBlock beast_nests;
};

// Version stamps. Bump only for an observable change.
inline constexpr int kWildlifeVersion = 3;
inline constexpr int kBeastNestsVersion = 4;

// ---------------------------------------------------------------------------
// Running a pass.
// ---------------------------------------------------------------------------

// `settled_nodes` are the grid nodes of everything already standing on the surface --
// cities, ports and hamlets -- for the clearance test and the danger ramp. The caller
// supplies them because the stages that produce them are not ported yet; an empty list
// is a legal world with no people in it, and the ramp degenerates cleanly to its
// tier-only form.
//
// THE ORDER OF `settled_nodes` IS A TIE-BREAK. nearest_settled sorts them by latitude
// with the original index breaking ties, so two settled things at exactly the same
// latitude have to be visited in the order the caller listed them.
//
// `grid` MUST be built at `world.effective.globe_radius`. `config` is DESIGN space and
// `effective` is metres; they differ by the world scale, a factor of 5.6 on the default
// world, and every reach here -- the clearance, the territory radius, the ramp span --
// is absolute metres. A design-space grid compares metres against design units and every
// gate silently answers a different question. Refused rather than documented.
//
// Nullopt when the phase gate refuses: no block is written below phase 7, or on a world
// with no recipe. That is the reference's own behaviour and is not an error.
std::optional<NestBlock> add_animals(const World& world, const SphereGrid& grid,
                                     const std::vector<int>& settled_nodes);
std::optional<NestBlock> add_monsters(const World& world, const SphereGrid& grid,
                                      const std::vector<int>& settled_nodes);

// Both passes, animals first. They never read each other.
std::optional<NestPasses> add_nests(const World& world, const SphereGrid& grid,
                                    const std::vector<int>& settled_nodes);

// ---------------------------------------------------------------------------
// The pieces, exposed because they are the published answers to questions a caller
// asks outside a pass -- "can this species live here", "how much does it like this
// ground" -- and because a test that can only reach them through a whole generation
// cannot say which of four gates refused a cell.
// ---------------------------------------------------------------------------

// Everything a cell is, to a creature. Built once per pass, in node order.
struct HabitatCell {
    int node = 0;
    int x = 0;
    int z = 0;
    int biome = 0;  // the NATURAL biome, never the magic variant
    double area_km2 = 0.0;
    Vec3 direction;
    CellMedium medium = CellMedium::Land;
    // Degrees Celsius. 100 when the world has no temperature layer -- above every
    // authored upper bound, so a world missing the layer places nothing.
    double temperature_c = 100.0;
    // The `coast` field, carried by name as well as by field id. The shore gate reads it
    // on every cell whether or not the catalogue happens to weight on it, so it cannot
    // live only in the id-indexed vector, which the catalogue decides the shape of.
    double coast = 0.0;
    // Dense, indexed by the ids of nest_field_names(). A field naming a layer this world
    // does not carry reads 0.0.
    std::span<const double> fields;
};

// Every cell's habitat description, in grid order: the ground both passes read.
//
// Named NestHabitat rather than HabitatField because civilizations.hpp already owns
// that name for the enum naming ONE habitat field, and this is the whole world's
// habitat. The two are unrelated and were colliding.
struct NestHabitat {
    std::vector<HabitatCell> cells;
    std::vector<double> storage;  // the flat backing for every cell's `fields`

    // Each cell's `fields` is a span into `storage`, so the two only ever travel
    // together. A copy would leave every span pointing into the original's buffer and
    // dangle the moment it died; a move keeps the heap block and is safe. The copy is
    // deleted rather than fixed up, because a fixed-up copy would be a second way to own
    // the same cells and nothing needs one.
    NestHabitat() = default;
    NestHabitat(NestHabitat&&) = default;
    NestHabitat& operator=(NestHabitat&&) = default;
    NestHabitat(const NestHabitat&) = delete;
    NestHabitat& operator=(const NestHabitat&) = delete;
};
NestHabitat habitat_cells(const World& world, const SphereGrid& grid);

// Hard eligibility before preferences; absent required fields fail closed. Returns the
// weight-NORMALISED mean sum(clamp(field)*w)/sum(w), in [0,1], or 0.0 for a cell the
// species cannot use at all.
double suitability(const CreatureProfile& profile, const HabitatCell& cell);

// The species' own table, already resolved through its role. Zero for ground it does not
// use -- an absent biome is exclusion, not neutrality.
double biome_weight(const CreatureProfile& profile, int biome_id);

// Rate multiplier that slides danger outward from settled ground. 1 on a settlement's
// doorstep for tier one and the mirror of that for tier five; a floor keeps every tier
// possible on every ground, because nothing may become impossible.
//
// A ratio of squares rather than an exponential: no pow, no exp, nothing whose last bit
// depends on the libm the build links against.
double danger_ramp(int tier, double clear_of_m, double span_m);

// Groups per square kilometre at full suitability: a pyramid, not a ladder. The pyramid
// is a property of the TIER, so authoring twenty more tier one animals makes each of them
// rarer rather than making the world more crowded.
double tier_density(double base, double falloff, int tier);

// Distance from every cell to the nearest settled thing, in grid order, in metres.
// Infinity for every cell when nothing is settled.
std::vector<double> nearest_settled(const std::vector<HabitatCell>& cells,
                                    const std::vector<Vec3>& settled, double radius);

// How far out the danger ramp turns over, in REFERENCE-WORLD metres. Scaled by
// circumference at use, because an absolute metre constant is sub-cell on a 200 km world
// and ramps nothing.
inline constexpr double kNestDangerSpanM = 300.0;

// The share of the rate that survives at the wrong end of the ramp.
inline constexpr double kNestDangerFloor = 0.2;

}  // namespace fwg
