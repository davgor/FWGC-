// The ordinary working population a finished world already implies, made addressable.
//
// Every placed building in the city, hamlet and castle plans carries a staffing roster, so
// the world has always known how many people it employs. This package creates them: one
// thin record per staffed post, plus the hero and Dread cast folded in, so a quest
// generator has ONE list to ask "who here is worth talking to, and are they still alive".
//
// NOTHING IS INVENTED. A plot's `workers` is a count the planner budgeted and housed; the
// roster turns it into that many named posts, senior first. When the plot's count and the
// policy's roster disagree, the PLOT WINS -- it is what was actually built -- and the
// difference is counted rather than hidden. Nothing is ever padded with a generic
// "worker": padding is how a data bug becomes a person.
//
// Two fields carry the whole point. `important` is the earmark -- the people worth naming,
// kept to a bounded few per site so a consumer can scan them rather than walk the roster.
// `alive` is birth state, so a quest is never offered by a corpse.
//
// THE OWNER'S BASELINE (2026-09-23). Every person names the building they work in as the
// plan placed it (`workplace`); the one storekeeper post names the store it keeps (`store`);
// a city's stable holds a horse keeper, who sells mounts and runs carriages (the services
// table). And every person -- post or cast -- carries a gender and an age drawn from their
// own stream, the age inside their people's young, middle or old band, which the block
// publishes beside each parent race's. Hair and every finer choice are the game's.
//
// Like key_locations this is a READER: it takes a request rather than a World, writes
// nothing back, and never throws. A failure is a reported block.
//
// NAMING. Names come from the heritage language genome keyed on `civilization_id`, through
// the people's NAME STOCK rather than straight off its generator: a size-65 world carries
// some thirteen thousand people, and drawing each uniformly from the 88 names a heartland
// tongue can build made every name equally ordinary. There is NO fallback for a missing
// civilization id -- a record without one raises and fails the whole block, because the
// silent fallback is precisely the defect that was removed and a loud failure is the only
// thing that stops it coming back.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fwg {

// 2 (2026-09-23): one storekeeper post, the horse keeper, and each person's workplace, store
// and appearance, with the block's life spans and post services.
inline constexpr int kNpcRosterVersion = 2;

// One prefix is enough because cast uids already begin `hero-` or `dread-`; without it you
// read `npc-hero-hero-castellan-...`.
inline constexpr std::string_view kNpcUidPrefix = "npc-";

// ASCII 0x7E, after every letter and digit: the sort sentinel that lands null-site and
// null-building records last within their group rather than first.
inline constexpr std::string_view kNpcNullSortKey = "~";

// ---------------------------------------------------------------------------
// The policy, generated from data/npc-posts.json, data/npc-names-handover.json and
// data/npc-code-tables.json into src/generated/content_npcs.cpp by tools/embed/npcs.py.
// ---------------------------------------------------------------------------

struct NpcRole {
    std::string_view role;
    int target;
};

struct NpcBuilding {
    std::string_view id;
    // SENIORITY ORDER. The first role of the first row is the senior post.
    std::span<const NpcRole> roles;
};

struct NpcRoleVerbs {
    std::string_view role;
    std::span<const std::string_view> verbs;
};

struct NpcCap {
    std::string_view key;  // capital | medium | small | hamlet | fortress
    int cap;
};

// What a post offers the player beyond quest verbs (2026-09-23): a horse keeper's mounts
// and carriages. A post with no row offers none.
struct NpcRoleServices {
    std::string_view role;
    std::span<const std::string_view> services;
};

// One weighted choice in the appearance baseline (data/npc-appearance.json).
struct NpcWeight {
    std::string_view id;
    int weight;
};

// The ten canonical quest verbs, shared with the hero generator's hooks.
std::span<const std::string_view> npc_verbs();
std::span<const NpcBuilding> npc_buildings();   // sorted by id
std::span<const NpcRoleVerbs> npc_role_verbs(); // sorted by role
// THE INDEX IS THE RANK, 0 being best.
std::span<const std::string_view> npc_giver_rank();
std::span<const NpcCap> npc_caps();
// The eight "<block>.<key>" pairs `places` walks, in order. Published because the order
// decides which block wins an id collision, and that is not visible from the result.
std::span<const std::string_view> npc_place_sources();
int npc_posts_revision();
int npc_names_revision();
std::string_view npc_method();
std::string_view npc_limits();
std::span<const NpcRoleServices> npc_role_services();  // sorted by role
// Genders and the young | middle | old band weights, in file order: a draw walks them.
std::span<const NpcWeight> npc_genders();
std::span<const NpcWeight> npc_age_band_weights();
// How far from the adult onset to the elder onset YOUNG ends and MIDDLE begins.
double npc_young_to_middle();
int npc_appearance_revision();
// The services a post offers, empty for most.
std::span<const std::string_view> npc_services(std::string_view role);

// A building ABSENT from the policy yields NO posts at all and its workers silently
// vanish, which is the intended case for houses and unstaffed plots.
const NpcBuilding* npc_building(std::string_view id);
// -1 means NEVER A GIVER. It does not mean rank 0, which is the best rank: defaulting a
// missing rank to 0 inverts the meaning exactly.
int npc_giver_index(std::string_view building_id);
// A role with no verbs can hold a post but can never become a giver. Eleven shipped roles
// have no entry at all.
bool npc_role_verbed(std::string_view role);
// -1 when the key is absent. The earmark treats absent and 0 identically, which is the
// reference's own conflation and is kept.
int npc_cap(std::string_view key);

// ---------------------------------------------------------------------------
// The request.
// ---------------------------------------------------------------------------

// A JSON number that may be ABSENT, null, or a value, kept apart because the city row
// takes `plan.get('x', site.get('x'))` -- a KEY-ABSENCE default, not a falsy one. A plan
// carrying `x: 0` or `x: null` keeps its own value; only a plan with no `x` key at all
// falls back to the settlement's. `name` and `civilization_id` beside it genuinely are
// falsy-fallbacks, so the three cannot be implemented with one rule.
struct NpcNumber {
    bool present = false;
    bool null_value = false;
    double value = 0.0;

    static NpcNumber absent() { return {}; }
    static NpcNumber of(double v) { return {true, false, v}; }
    static NpcNumber null() { return {true, true, 0.0}; }
};

// `workers` is a COUNT, never a breakdown.
struct NpcPlot {
    std::string id;  // plot-<k>
    std::string building_id;
    int workers = 0;
    // The building's name as the plan placed it ("Granary", "Winter supply warehouse"):
    // each person's workplace, and a storekeeper's store.
    std::string name;
};

// A people's life, in years, as the heritage appearance layer resolves it: the subrace's
// own stages where it states them, else its parent race's.
struct NpcLifeStages {
    double adult = 0.0;  // the adult onset
    double elder = 0.0;  // the elder onset
    double max = 0.0;    // the longest life of the people
};

struct NpcCityPlan {
    std::string city_uid;  // required, or the site is dropped and unanchored_sites counts it
    std::string civilization_id;
    std::string name;
    std::string city_class;  // anything outside capital|medium|small stores none
    std::string status;
    NpcNumber x, z;
    std::vector<NpcPlot> plots;
};

struct NpcHamletPlan {
    std::optional<int> node;  // required
    std::string civilization_id;
    std::string core_city_uid;
    // An INDEX into the settlement list, not a uid. The small-site builder stores owner[i]
    // and the hamlet planner dereferences it by position.
    std::optional<int> core_id;
    std::string kind;
    std::string role;
    std::string hamlet_id;  // ordinal, convenience only, NEVER identity
    std::string status;
    NpcNumber x, z;
    std::vector<NpcPlot> plots;
};

struct NpcCastlePlan {
    std::string fortress_id;
    std::string status;
    NpcNumber x, z;
    std::vector<NpcPlot> plots;
};

struct NpcFortress {
    std::string id;
    std::optional<int> node;  // a fortress with no node cannot anchor a site
    std::string population_profile;  // used as the fortress's civilization_id
    std::optional<int> core_id;      // an index into the settlement list
};

struct NpcSettlementSite {
    std::string uid;  // uid where the block has one, else id
    std::string name;
    std::string civilization_id;
    std::string parent_race_id;
    std::optional<int> node;
    NpcNumber x, z;
};

struct NpcCivilizationEntity {
    std::string id;
    std::string parent_race_id;
    // Absent where heritage states no body for the people; its people then get no age.
    std::optional<NpcLifeStages> life;
};

// A parent race's own stages, published beside its peoples' so a subrace's departure from
// its race is visible.
struct NpcParentRace {
    std::string id;
    NpcLifeStages life;
};

// A row from one of the eight place-index blocks, or from `ruins`.
struct NpcPlaceRow {
    std::string uid;
    std::optional<int> node;
    std::optional<double> x, z;
};

struct NpcCastMember {
    std::string uid;
    std::string display_name;
    std::string name;
    std::string role;
    std::string status;  // 'living', anything else is treated as dead
    std::string home_uid;
    std::string presence_uid;
    std::string presence_site_kind;
    std::string civilization_id;  // the member's own people, for its appearance
};

struct NpcRequest {
    std::uint64_t seed = 0;

    // At least one of these three must be PRESENT, even if empty: a world with planned
    // blocks and zero plans reports an ok block with zero people, while a world with no
    // planned block at all is a world whose planners have not run.
    bool has_city_plans = false;
    bool has_hamlet_plans = false;
    bool has_castle_plans = false;
    std::vector<NpcCityPlan> city_plans;
    std::vector<NpcHamletPlan> hamlet_plans;
    std::vector<NpcCastlePlan> castle_plans;

    std::vector<NpcFortress> fortresses;
    // ORDER IS LOAD-BEARING: `core_id` is a POSITION in this list.
    std::vector<NpcSettlementSite> settlements;
    std::vector<NpcCivilizationEntity> entities;
    std::vector<NpcParentRace> parent_races;

    // Copied into the output for drift visibility only. The post labels come from a
    // snapshot of the building registry, so naming the world's own registry identity
    // beside the snapshot's is what makes a divergence visible rather than silent.
    std::optional<int> registry_revision;
    std::string registry_sha256;

    // The cast is folded in only when its own block reported ok.
    bool heroes_ok = false;
    // heroes.people THEN heroes.dreads: that concatenation is the record order.
    std::vector<NpcCastMember> cast;

    // The eight place-index sources, in the order `places` walks them. FIRST WRITER WINS
    // on an id collision across blocks, so these are separate fields rather than one list.
    std::vector<NpcPlaceRow> place_settlements;
    std::vector<NpcPlaceRow> place_hamlets;
    std::vector<NpcPlaceRow> place_fortresses;
    std::vector<NpcPlaceRow> place_ports;
    std::vector<NpcPlaceRow> place_religion_sites;
    std::vector<NpcPlaceRow> place_hero_camps;
    std::vector<NpcPlaceRow> place_colleges;
    std::vector<NpcPlaceRow> place_nests;
    // Walked last and keyed on `id`. Ruin presences name the city the ruin used to be.
    std::vector<NpcPlaceRow> place_ruins;
};

// ---------------------------------------------------------------------------
// The block.
// ---------------------------------------------------------------------------

struct NpcSite {
    std::string uid;   // THE KEY FIELD
    std::string kind;  // city | hamlet | fortress, an open vocabulary
    std::string name;
    std::optional<std::string> civilization_id;
    std::optional<std::string> parent_race_id;
    std::optional<std::string> city_class;
    std::string plan_id;  // convenience only, NEVER identity
    std::optional<int> node;
    NpcNumber x, z;
    std::optional<std::string> plan_status;
    int posts = 0;
};

// One adult age band of a people, in whole years, both ends inclusive.
struct NpcAgeBand {
    std::string_view id;  // young | middle | old
    int from = 0;
    int to = 0;
};

// A people's (or a parent race's) life and the three bands it gives (2026-09-23). YOUNG runs
// from the adult onset to npc_young_to_middle() of the way to the elder onset, MIDDLE from
// there to the elder onset, OLD from the elder onset to the longest life. Every onset is
// rounded to a whole year first, so the bands meet without a gap or an overlap.
struct NpcLifeSpan {
    std::string id;
    std::string parent_race_id;  // empty on a parent race's own row
    NpcLifeStages life;
    std::vector<NpcAgeBand> bands;
};
NpcLifeSpan npc_life_span(std::string id, std::string parent_race_id, const NpcLifeStages& life);

// The baseline a game builds a body on: hair and every finer choice are the game's.
struct NpcAppearance {
    std::string_view gender;    // an npc_genders() id
    std::string_view age_band;  // young | middle | old
    int age_years = 0;
    std::string civilization_id;  // the people the age was drawn for
    std::string parent_race_id;
};

struct NpcPerson {
    std::string uid;  // THE KEY FIELD
    // Absent when the site's parent race could not be resolved: NEVER guessed as human,
    // because a guessed parent is a wrong tongue.
    std::optional<std::string> name;
    std::optional<std::string> site_uid;
    std::optional<std::string> site_kind;
    std::string post;
    std::optional<std::string> building_id;
    std::string plot_id;  // '' for a cast record
    bool alive = true;
    bool important = false;
    // The name of the building this person works in, as the plan placed it.
    std::optional<std::string> workplace;
    // A storekeeper's store, by its building: granary, warehouse, root_cellar,
    // hamlet_food_store. The exact store is (site_uid, plot_id).
    std::optional<std::string> store;
    // Absent only for a person whose people has no life stages -- a dread, say -- and
    // counted in the notes rather than guessed.
    std::optional<NpcAppearance> appearance;

    // Cast records only.
    bool is_cast = false;
    std::string hero_uid;
    std::optional<std::string> presence_kind;
    std::optional<std::string> presence_uid;
    // ABSENT, not null, when the person stands where they are filed. Its PRESENCE is
    // itself the signal that the home-city fallback was in play, and a confidently wrong
    // location is worse than none for anything measuring travel.
    std::optional<int> presence_node;
    std::optional<double> presence_x, presence_z;
};

// EVERY COUNTER IS ALWAYS PRESENT, EVEN AT ZERO. A monitoring counter that appears only
// once it is non-zero cannot be told apart from one that was never computed, which makes
// it useless on exactly the day it would have earned its keep.
struct NpcNotes {
    int unresolved_parent_race = 0;
    int unanchored_sites = 0;
    int roster_mismatches = 0;
    int duplicate_site_keys = 0;
    int important_capped_out = 0;
    int cast_outside_a_planned_site = 0;
    int cast_located_by_presence = 0;
    int cast_presence_unresolved = 0;
    int appearance_unresolved = 0;  // people with no life stages to draw an age from
};

struct NpcSummary {
    int total = 0;
    int alive = 0;
    int dead = 0;
    int important = 0;
    double important_fraction = 0.0;
    int sites = 0;
    int heroes_linked = 0;
    NpcNotes notes;
};

struct NpcRoster {
    int version = kNpcRosterVersion;
    bool ok = false;
    std::string error;
    int policy_revision_posts = 0;
    int policy_revision_names = 0;
    std::optional<int> registry_world_revision;
    std::string registry_world_sha256;
    std::string_view registry_labels_from = "npc_roster.policies.posts";
    NpcSummary summary;
    std::vector<NpcSite> sites;
    std::vector<NpcPerson> people;
    // Each parent race's life and bands, then each people's, both sorted by id.
    std::vector<NpcLifeSpan> parent_race_life_spans;
    std::vector<NpcLifeSpan> life_spans;
    // `post_verbs` and `verbs` are the static npc_role_verbs() and npc_verbs() spans
    // rather than copies. The reference copies them because its policy document is cached
    // and shared and a consumer mutating the block would corrupt every later roster; a
    // constexpr table cannot be mutated, so the copy has nothing left to protect.
    std::string_view method;
    std::string_view limits;
};

// NEVER THROWS: a failure is a reported block with ok == false.
NpcRoster generate_npc_roster(const NpcRequest& request);

// ---------------------------------------------------------------------------
// The pieces worth pinning on their own.
// ---------------------------------------------------------------------------

// One post standing in a site.
struct NpcPost {
    std::string_view building_id;
    std::string_view role;
    int ordinal = 0;
    std::string plot_id;
    // Only the FIRST plot of a given building in a site can produce a senior post, because
    // `ordinals` is scoped to the whole site rather than to the plot.
    bool senior = false;
};

// Ordered posts for one site's plots, senior first within each building.
//
// Plots are walked in NUMERIC-TAIL order so `plot-10` does not sort before `plot-2`;
// without it every ordinal in the site shifts. `mismatches` counts plots whose roster and
// worker count disagreed.
std::vector<NpcPost> npc_posts_in(const std::vector<NpcPlot>& plots, int& mismatches);

// `building.smithy` reads as `smithy` in a uid: the prefix is constant and costs twelve
// thousand copies.
std::string_view npc_short_building(std::string_view building_id);

// `Bargdorn (Age 2)` reads as `Bargdorn` inside a small site's name.
//
// The reference also strips a trailing ' City' here, from the retired convention where the
// generator appended one. That strip is DROPPED: heritage names cities now, the replace is
// the identity on every world this library can generate, and the key_locations copy of the
// same function already dropped it on the ruling that "a name that ends in those
// characters is now the name, and cutting them would rename the place".
std::string npc_short_name(std::string_view name);

}  // namespace fwg
