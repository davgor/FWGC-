// Stage 13's third pass: gods that may exist, one cosmology, and the faiths peoples keep.
//
// Everything here is narrative provenance resolved deterministically from world state. A
// god is manifest, sleeping or absent, and it changes NOTHING in the world: no terrain,
// no food, no threat, no influence. It acts only when an orchestrator summons it through
// the visitation API. `resolve_religion` therefore returns a block rather than mutating
// anything, and that is the contract, not an implementation detail.
//
// The point of resolving rather than inventing is that every deity is justifiable from
// the world data. Which gods a world could plausibly have is a function of what it
// actually contains -- which leyline networks have key points, whether there is an ocean,
// whether dragons nest, whether anyone was exiled -- so a reader or a game host can trace
// any god back to the thing that raised it.
//
// The catalogue ships as data (data/pantheon.json) and is generated into a typed table;
// this header only interprets it.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/astrology.hpp"
#include "fwg/grid.hpp"
#include "fwg/magic.hpp"
#include "fwg/vec3.hpp"

namespace fwg {

// Stamped into every resolved block. Three downstream subsystems (visitation,
// corruption, and the age gate) refuse a saved world whose value differs, with a
// "regenerate the world" error -- so bumping this invalidates every saved world.
inline constexpr int kReligionVersion = 1;

// Threshold on the Weave lunar tide of the age a city fell in, for a cult site's
// `born_under_surge`. Deliberately NOT the almanac's own surge threshold (kSurge, 1.6):
// a ruin needs only an elevated tide, not a full surge. Purely cosmetic -- nothing reads
// it back. Raising it past the tide ceiling (1.8) makes the flag permanently false.
inline constexpr double kSurgeBirth = 1.3;

// The four schools `facts.primordial_count` counts, as indices into the known-school
// order (weave, umbral, infernal, radiant, fire, water, earth, air).
inline constexpr int kPrimordialSchools[4] = {4, 5, 6, 7};

// ---------------------------------------------------------------------------
// Ids the ALGORITHM names rather than the data.
//
// Every one of these is hardcoded in the reference and is not derivable from the
// catalogue. A rename that only touches the JSON leaves the code compiling, the tables
// well-formed and the behaviour silently gone -- the fallback never fires, the moon god
// stops leaning, the cult stops being written -- so they are named here with the reason
// they are named, and the codegen fails closed on the one it can check.
// ---------------------------------------------------------------------------

// `choose_cosmology` falls back to this by id when nothing is eligible. In practice the
// branch is dead because cos_mosaic requires `always` with a positive base, but a
// catalogue edit that removed that rule would otherwise leave the draw with no items.
inline constexpr std::string_view kFallbackCosmologyId = "cos_mosaic";

// Special-cased twice: its aspect follows the moon's mean lean instead of a leyline
// network, and a faith that takes it as patron keeps full-moon feasts.
inline constexpr std::string_view kMoonGodId = "god_turning_moon";

// A city that unmade itself with its own magic leaves a cult of the Weave; a city that
// fell in a war leaves a shrine of the Red Field. Neither is looked up by family or
// domain -- the two ids are written into `resolve_sites` itself.
inline constexpr std::string_view kSelfMagicCultGodId = "god_weave";
inline constexpr std::string_view kWarShrineGodId = "god_red_field";

// Hollow nights are dread for everyone except a people whose PATRON is one of these two
// in its Wild aspect, who keep them as vigils instead.
inline constexpr std::string_view kVigilGodIds[2] = {"god_umbral", "god_infernal"};

// A threat of kind `dragon` whose nest family has no fear god of its own falls back to
// the draconic entry. Family is consulted FIRST: a nest named "...dragon..." whose family
// is `undead` frightens a city into the Umbral, not the Wyrm.
inline constexpr std::string_view kDraconicFearFamily = "draconic";

// The family string the four Outside gods carry, and the only thing that identifies them
// in an exported world -- terrain_nomads finds a blood cult's patron by this alone.
inline constexpr std::string_view kOutsideFamily = "outside";

// The pseudo-school on god_turning_moon's affinity list. It names no potency grid, so it
// contributes exactly zero to faith weighting; the moon god scores only from affinity
// tables and cosmology rank. This is deliberate, not a missing school.
inline constexpr std::string_view kMoonPseudoSchool = "moon";

// ---------------------------------------------------------------------------
// The catalogue, generated from data/pantheon.json into src/generated.
// ---------------------------------------------------------------------------

enum class RuleKind : int {
    always = 0,
    magic_enabled,
    magic_disabled,
    school,
    race,
    civilization_city,
    ocean_share,
    sky,
    dragon,
    nest_family,
    ruins,
    diaspora,
    any_of,
    all_of,
    primordial_count,
    gods,
    races,
};

// One node of an existence rule, in a flat array.
//
// Children are a range into a shared pool of node INDICES, not a range of nodes, for the
// same reason the habitat trees are: a range of nodes is only correct while no child has
// children of its own, and nesting one level deeper would silently sweep in a grandchild.
struct RuleNode {
    RuleKind kind;
    std::string_view school;  // `school`
    std::string_view race;    // `race`
    double min_value;         // ocean_share | ruins | races | primordial_count
    // `gods` only: every named god must be manifest, or merely one of them.
    bool all;
    // Membership pool range: civilization_city ids, nest_family families, gods ids.
    std::uint32_t text_begin, text_end;
    std::uint32_t child_begin, child_end;
};

// The pools a flattened rule indexes into. Bundled for the same reason HabitatPools is:
// a tree that carried only its nodes would index whatever pool it was handed.
struct RulePools {
    std::span<const std::uint32_t> children;
    std::span<const std::string_view> texts;
};
RulePools pantheon_rule_pools();

// Sovereign below the intensity/instability midpoint, Wild above. `wild` is empty for the
// sixteen gods that have only one face, and for those the sovereign name is the god's own.
struct GodAspects {
    std::string_view sovereign;
    std::string_view wild;
};

// What one people calls a god. Only the three ancestor gods carry any.
struct Epithet {
    std::string_view civilization;
    std::string_view name;
};

struct PantheonGod {
    std::string_view id;
    std::string_view name;
    std::string_view family;  // school | ancestor | feature | civic
    GodAspects aspects;
    std::span<const std::string_view> domains;
    std::span<const RuleNode> exists;  // root at relative index 0
    std::span<const std::string_view> allies;
    std::span<const std::string_view> rivals;
    // The schools this god draws faith from -- NOT the school its existence rule tests.
    // May contain kMoonPseudoSchool.
    std::span<const std::string_view> school_affinity;
    std::span<const std::string_view> purge_families;
    std::string_view smite;       // ruin flavour; read by visitation, never here
    std::string_view race;        // ancestor gods only; empty otherwise
    std::span<const Epithet> epithets;
    std::string_view folds_into;  // empty when this god is nobody's saint
    std::string_view festival;    // flavour; only the moon god carries one
};

// One of the four the known pantheon does not speak of.
//
// They carry `school` SINGULAR -- one hidden school -- where a known god carries
// `school_affinity`, a list. The export maps the one field into a one-element list, which
// is what lets a nomad blood cult find its patron.
//
// `interest`, `noticing` and `climb` are consumed by the corruption API, not here; this
// stage is what makes them reachable in a saved world.
struct HiddenGod {
    std::string_view id;
    std::string_view name;
    std::string_view school;
    GodAspects aspects;
    std::span<const std::string_view> domains;
    std::span<const RuleNode> exists;  // evaluated by NOTHING here; exported for corruption
    std::string_view interest;
    int noticing;  // encounters below which the chance is exactly zero, not merely small
    int climb;     // encounters above which this god is certain to act, if most interested
    std::span<const std::string_view> rivals;
    std::span<const std::string_view> purge_families;
    std::string_view smite;
};

// base + per * <the named world fact>. An empty `fact` leaves the weight at its base; a
// fact that is a LIST contributes its LENGTH.
struct CosmologyWeight {
    std::string_view fact;
    double base;
    double per;
};

struct Cosmology {
    std::string_view id;
    std::string_view name;
    std::span<const RuleNode> requirement;
    CosmologyWeight weight;
    std::span<const std::string_view> high;  // filtered to the manifest on export
    std::string_view arrangement;
};

// What one people thinks of the pantheon before any of it is drawn.
struct AffinityTable {
    std::string_view civilization;
    std::span<const std::string_view> strong;
    std::span<const std::string_view> mild;
    std::span<const std::string_view> aversion;
};

// Which god a city threatened by this kind of nest fears.
struct FearGod {
    std::string_view family;
    std::string_view god_id;
};

// Which nest families push a god toward its Wild aspect.
struct WildPush {
    std::string_view god_id;
    std::span<const std::string_view> families;
};

struct AffinityWeights {
    double strong;      // an affinity table's own favourite
    double mild;
    double aversion;    // negative, and large enough to drop a god from the pool outright
    double high;        // a cosmology's high god
    // The floor that keeps the civic gods always drawable. Without it a people with no
    // affinity entry for one would never worship it.
    double civic_base;
};

struct PantheonTuning {
    // School-god Sovereign/Wild threshold on mean intensity x (1 + instability) + push.
    // The comparison is STRICTLY greater. Not a fraction: the score is unbounded above.
    double aspect_midpoint;
    double nest_aspect_push;
    AffinityWeights affinity;
};

// Identity of the catalogue these tables came from, and of this copy of it.
//
// THE SCOPE IS DELIBERATELY NARROW. `sha256` covers the known catalogue ALONE. The four
// Outside gods are excluded so that touching one does not retire every saved world --
// which is exactly why the reference kept them in a second file with its own identity.
struct PantheonIdentity {
    int schema_version;
    int revision;
    std::string_view source_sha256;  // the reference catalogue's own digest, for provenance
    std::string_view sha256;         // over THIS data; what a world records
};

std::span<const PantheonGod> pantheon_gods();
std::span<const HiddenGod> hidden_pantheon_gods();
std::span<const Cosmology> pantheon_cosmologies();
std::span<const AffinityTable> pantheon_affinities();
std::span<const std::string_view> fold_free_cosmologies();
std::span<const FearGod> fear_gods();
std::span<const WildPush> wild_push_families();
PantheonTuning pantheon_tuning();
PantheonIdentity pantheon_identity();
PantheonIdentity hidden_pantheon_identity();

const PantheonGod* pantheon_god(std::string_view id);
const HiddenGod* hidden_pantheon_god(std::string_view id);
const Cosmology* pantheon_cosmology(std::string_view id);
const AffinityTable* pantheon_affinity(std::string_view civilization);
std::string_view epithet_for(const PantheonGod& god, std::string_view civilization);

// Raise on a catalogue that could not be resolved. The generator already refuses to emit
// a broken table, so this is the runtime half of the same gate: it proves the table that
// was actually linked in is the one the codegen checked.
void lint_pantheon();

// ---------------------------------------------------------------------------
// What the world has to say for itself.
// ---------------------------------------------------------------------------

// The complete set of world state an existence rule can see.
//
// This is a scoping rule, not a convenience: a rule may only read facts the world already
// publishes. Do not add a kind that reaches into a layer or recomputes geometry.
struct WorldFacts {
    // In known-school order (weave, umbral, infernal, radiant, fire, water, earth, air) --
    // the same order as the first eight rows of magic_school_table(). True means the
    // network has KEY POINTS; edges alone do not count.
    std::array<bool, kKnownSchools> schools{};
    int primordial_count = 0;
    std::vector<std::string> races;  // sorted; a subset of the parent races
    // Sorted, and NOT filtered to the known civilizations: the reference takes every
    // standing site's profile verbatim. An unknown profile therefore reaches
    // choose_faiths and is refused there, loudly, rather than silently dropped here.
    std::vector<std::string> civilizations_with_cities;
    double ocean_share = 0.0;
    int sky = 0;
    std::vector<std::string> families;  // sorted; fantasy nest families present
    int dragons = 0;
    int ruins = 0;
    int diaspora = 0;
    // Exported only. No rule kind reads it and no cosmology weighs on it; it is here
    // because the reference publishes it and a consumer may show it.
    int wars = 0;
    bool magic_enabled = false;
    double lunar_influence = 0.0;
};

// A standing city, as religion reads one.
struct ReligionCity {
    std::string uid;                 // the key a fear entry is recorded against
    std::string population_profile;  // the civilization
    int x = 0;
    int z = 0;               // the ley grid cell this city stands on
    int node = -1;
    Vec3 direction{};        // unit vector, for the great-circle distance to a nest
    bool diaspora = false;   // carried here by exile, schism or expedition
};

// A beast nest, as religion reads one. Only fantasy nests matter: real animals never
// frighten a god.
struct ReligionNest {
    std::string id;  // STRING-sorted, and that sort decides which nest a fear records
    std::string name;
    std::string family;
    std::string layer = "surface";
    // A nest is fantasy iff this is false. It defaults TRUE, so a caller that forgets the
    // field gets an ordinary animal rather than a monster.
    bool real = true;
    int tier = 5;  // DANGER tier 1..5 -- not a settlement scale and not a key-location tier
    // Absent falls through to `spacing_m`, and absent again to 350 m. Zero is a real
    // value meaning "no reach at all", which is why neither is a plain double.
    std::optional<double> range_m;
    std::optional<double> spacing_m;
    Vec3 direction{};
};

// A fallen city. `node` is the terrain node, which is the stable key across an age
// boundary -- ruin ids are not.
struct ReligionRuin {
    std::string id;
    int node = -1;
    // One of the eight school names, `dragon`, `monster`, `self_magic`, `divine_<god id>`,
    // a villain cause, or `war_<pair kind>`. Only self_magic and the war_ prefix raise a
    // site.
    std::string cause;
    int destroyed_age = -1;
    // Who lived here. Counted toward `facts.races` -- a people whose last city fell still
    // has an ancestor god -- but ONLY when it names a civilization the registry knows.
    std::string population_profile;
};

// One age that has already turned.
struct ReligionAge {
    int age = 0;
    int wars = 0;  // how many, not which: religion only sums the counts
    // The Weave lunar tide on the day this age turned, 0.5..1.8. Absent for an age with
    // no lunar context, and a cult born in such an age records no tide at all.
    std::optional<double> weave_tide;
};

// A founding event, as religion reads one. Only the diaspora records matter, and among
// those only the schisms. Built from a FoundingReport's events plus the founded site they
// produced -- religion needs the reason and the year, which the event alone does not carry.
struct ReligionFoundingEvent {
    std::string status;            // diaspora_founded | founded | ...
    std::string diaspora_reason;   // religious_schism | exile | expedition | magical_displacement
    std::string population_profile;
    int node = -1;
    long long founding_year = 0;
};

// A record this stage carries but never interprets: written by the visitation and
// corruption APIs, which are not ported. Carried as its own serialised text rather than
// dropped, because a world that round-trips through here and loses its corruptions has
// lost game state permanently.
using OpaqueRecord = std::string;

// A people's conversion by a god that walked among it.
struct Conversion {
    std::string god_id;
    int visitation = -1;
};

// The prior block, whatever wrote it. Every resolution reads it and carries state
// forward: `walking` is sticky, a god that stops qualifying sleeps rather than vanishing,
// and the hidden four keep whatever status something woke them to.
struct Religion;

struct ReligionRequest {
    std::uint32_t seed = 0;

    const MagicReport* magic = nullptr;                     // stage 9
    const std::map<std::string, Grid>* layers = nullptr;    // the ley_<school> potency grids
    const Almanac* almanac = nullptr;                       // stage 9

    std::vector<ReligionCity> cities;                       // stages 10-12
    std::vector<std::string> used_civilizations;            // peoples that ever founded
    std::vector<ReligionFoundingEvent> founding_events;
    std::vector<ReligionRuin> ruins;
    std::vector<ReligionNest> nests;                        // stage 13, the pass before this
    std::vector<ReligionAge> ages;

    int sky_settlements = 0;
    double ocean_km2 = 0.0;
    double lake_km2 = 0.0;
    double dry_km2 = 0.0;
    // The resolved world option, roughly 0..1. It reaches only one place: it scales
    // cos_turning_moon's weight, whose base is zero -- so a world with no lunar influence
    // cannot have the moon as its high god however much magic it has.
    double lunar_influence = 0.0;

    // METRES. This is world.effective.globe_radius, not world.config's -- the two differ
    // by the world scale, a factor of 5.6 on the default world. The 350 m floor and the
    // radius/2 ceiling in the fear reach are absolute metres authored against the
    // reference world (kReferenceRadiusM), so handing this design units compares metres
    // with design units and every fear test silently answers a different question.
    double globe_radius_m = 1.0;

    const Religion* previous = nullptr;
};

// ---------------------------------------------------------------------------
// What comes out.
// ---------------------------------------------------------------------------

// Why a god wears the aspect it does.
//
// The two shapes differ because the two kinds of god that have a choice are measured by
// different things: a school god by its network, the moon god by its lean.
struct AspectEvidence {
    bool has_network = false;
    double mean_intensity = 0.0;
    double instability = 0.0;
    double nest_push = 0.0;
    double midpoint = 0.0;

    bool has_lean = false;
    double mean_nod_degrees = 0.0;
    std::string leaning;  // still | restless

    // Set on a SLEEPING god: this is the evidence it was last seen with, kept so a
    // withdrawn god still says why it wore the face it did.
    //
    // ONE level, deliberately. The reference nests {'last_seen': {'last_seen': ...}} one
    // deeper on every resolution, so a god that sleeps through many age turns grows the
    // export without bound. That is a defect, not intent; collapsing it here is a
    // recorded departure.
    bool last_seen = false;
};

struct GodRecord {
    std::string id;  // THE key field for this block -- not name, not uid
    std::string name;
    std::string family;  // school | ancestor | feature | civic | outside
    std::string status;  // absent | manifest | sleeping | walking
    // Empty iff absent. A sleeping god keeps the aspect it last wore.
    std::string aspect;
    std::string aspect_name;
    std::vector<std::string> domains;
    // The god's school AFFINITIES, not the school its existence rule tests. For a hidden
    // god this is the one hidden school, as a one-element list.
    std::vector<std::string> schools;
    AspectEvidence evidence;
    // Whose saint this god is under the chosen cosmology, empty when nobody's. Written
    // after the cosmology is drawn, because folding is cosmology-dependent.
    std::string saint_of;
    // Present only when a prior record was `walking` and carried one. Opaque: written by
    // the visitation API.
    std::optional<OpaqueRecord> avatar;
    // The god's existence rule, exported so the corruption API can evaluate a hidden
    // god's without loading the catalogue itself.
    std::span<const RuleNode> rule;
};

struct CosmologyChoice {
    std::string id;
    std::string name;
    std::string arrangement;
    std::vector<std::string> high;  // filtered to the manifest, so it may come out empty
    // Every template that fitted, in CATALOGUE ORDER with the weight it drew on. This is
    // the order the single uniform draw walked.
    std::vector<std::pair<std::string, double>> eligible;
};

// A god a city fears because of what nests near it.
struct FearRecord {
    std::string god_id;
    std::string city_uid;
    std::string nest_id;  // the FIRST nest per (god, city), by string id order
    std::string mode;     // always "fear"
};

// A schism that founded its own town. Note the inversion: the sect's patron is the parent
// faith's rival, and its rival is the parent faith's patron.
struct SectRecord {
    int node = -1;
    long long founding_year = 0;
    std::string patron;
    std::string rival;
    std::string epithet;
};

struct Feasts {
    // Per worshipped SCHOOL god, the days its school surges. Insertion-ordered by the
    // faith's own god order, which is observable in the export.
    std::vector<std::pair<std::string, std::vector<int>>> surges;
    std::vector<int> full_moons;     // empty unless the moon god is worshipped
    std::vector<int> hollow_nights;  // populated either way
    std::string hollow_kept_as;      // vigil | dread
};

struct Faith {
    std::string civilization;
    std::string parent_race_id;
    std::string patron;  // empty when this people draws nobody at all
    // Patron first, then up to three more drawn without replacement. Position matters:
    // the feast rule reads the patron alone and a sect falls back to the second god.
    std::vector<std::string> gods;
    // Display name per worshipped god, in `gods` order.
    std::vector<std::pair<std::string, std::string>> names;
    // Still exported when its god has stopped existing, in which case it is NOT the
    // patron. The reference's comment is "until it stops existing"; sleeping counts.
    std::optional<Conversion> conversion;
    std::vector<FearRecord> fear;  // sorted by (city_uid, god_id)
    std::vector<SectRecord> sects;
    Feasts feasts;
    // Every god that scored above zero, in catalogue order -- the order the draw walked.
    std::vector<std::pair<std::string, double>> weights;
};

struct ReligionSite {
    std::string id;  // THE key field, and what the list is sorted by
    std::string kind;  // cult | shrine | theophany | pilgrimage
    std::string god_id;
    std::string ruin_id;  // empty on a carried theophany or pilgrimage
    int node = -1;        // the terrain node: stable across an age boundary
    bool born_under_surge = false;
    std::optional<double> weave_tide;
    int visitation = -1;  // carried sites only
};

struct Religion {
    int version = kReligionVersion;
    PantheonIdentity catalogue{};
    WorldFacts facts;
    CosmologyChoice cosmology;
    // The 25 known gods in catalogue order, then the 4 hidden ones in theirs.
    std::vector<GodRecord> gods;
    // One per civilization with a standing city, in sorted civilization order.
    std::vector<Faith> faiths;
    std::vector<ReligionSite> sites;  // sorted by id
    std::vector<OpaqueRecord> visitations;
    std::vector<OpaqueRecord> corruptions;
    std::string method;
    std::string limits;
};

// ---------------------------------------------------------------------------
// The pass itself, and the pieces a caller or a test may want on their own.
// ---------------------------------------------------------------------------

// A set of god ids, transparently comparable so a string_view can look one up.
using GodIdSet = std::set<std::string, std::less<>>;

// The inputs existence rules read, taken from what the generator already exports.
WorldFacts world_facts(const ReligionRequest& request);

// Evaluate a flattened rule. `manifest` is empty for a god's own rule -- which is what
// makes a `gods` rule unsatisfiable there, and is why only cosmologies use one.
bool rule_holds(std::span<const RuleNode> rule, const WorldFacts& facts,
                const GodIdSet& manifest = {});

// Sovereign or Wild, and the measurement that decided it.
std::pair<std::string, AspectEvidence> aspect_of(const PantheonGod& god,
                                                 const ReligionRequest& request,
                                                 const WorldFacts& facts);

// The status ladder, one record per catalogue god in catalogue order.
//
// Monotone in one direction that matters: a god NEVER returns to absent once it has
// existed. It sleeps, keeping its last aspect. `walking` is sticky and only the
// visitation or corruption APIs can end it.
std::vector<GodRecord> resolve_gods(const ReligionRequest& request, const WorldFacts& facts);

// One template drawn from those that fit, by fit weight. Consumes exactly ONE uniform
// draw, always, even when only one template is eligible.
CosmologyChoice choose_cosmology(const WorldFacts& facts, const GodIdSet& manifest,
                                 std::uint32_t seed);

// Mean potency of each known school under a people's cities, in known-school order.
// A missing layer or no cities reads 0.
std::array<double, kKnownSchools> mean_potency(const ReligionRequest& request,
                                               const std::vector<const ReligionCity*>& cities);

// Standing monster pressure on one city.
//
// SHARED, not religion's own: the age-fate lottery and the regional threat assessment run
// the same eligibility and the same reach, and three copies of this would be three
// answers. It lives here only because stage 14 is not ported yet; when it lands, this
// moves and all three callers take it from one place.
struct NestThreat {
    std::string kind;  // dragon | monster
    double weight = 0.0;
    std::string reason;
    std::string nest_id;
    std::string name;
    std::string family;
    double distance_m = 0.0;
    double reach_m = 0.0;
};
std::vector<NestThreat> nest_threats(const ReligionCity& city,
                                     const std::vector<ReligionNest>& nests, double radius_m);

// Resolve gods, cosmology, faiths and sites from the current world. Narrative only:
// nothing in the request is modified and nothing else in the world is written.
//
// Throws when a city names a civilization the registry does not have -- the reference
// fails at the same point, and a silent skip would leave a people with no faith and
// nobody the wiser.
Religion resolve_religion(const ReligionRequest& request);

}  // namespace fwg
