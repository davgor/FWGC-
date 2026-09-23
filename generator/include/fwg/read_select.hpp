// Pure selection over a world that already exists: the half of the read surface with no
// request.
//
// A selector takes a world and plain arguments and returns rows. It validates nothing,
// refuses nothing in the request envelope, mints nothing and mutates nothing, so every claim
// about WHAT IS IN A WORLD is testable without building a request -- the determinism argument
// and the join argument both live on this side. read.hpp is the request half.
//
// **Every join here is the writer's own field**, and the writing site is named beside it
// (and again in the `joins` rows the read API returns). Resolving a join is writing a second
// implementation of it, and a near miss -- a suffix test for equality, a node where the
// writer keyed on an id, a positional index where the writer wrote a foreign key -- answers a
// different question and looks correct. Where two fields would agree on today's world and
// only one is the writer's, the writer's is used.
//
// **Ids that renumber are never returned alone.** Hamlet, fortress, plot, nest and culture
// ids are ordinals re-derived at every age boundary, so anything returned here spells out the
// terrain node as well.
//
// **The reference read blocks of a JSON document by name; this reads the World's typed
// fields.** Each selector reads exactly what the world DOCUMENT (serialize.hpp) carries for
// that block, gated the way the document writer gates it: `has_settlements` for
// `settlements`, `has_humans` for `humans` and `seasonal_food`, `has_wildlife` for
// `beast_nests`, `has_magic` for `magic`, the `encounters` system having run for
// `encounters`, `has_plans` (and `has_humans` for the hamlet and castle plans) for the plan
// blocks, an ok block for `heroes`, `npcs`, `key_locations` and `key_location_plans` -- a
// failed block's document carries its error and no rows. Where the C++ names a block
// differently from the reference's STATE_KEYS the difference is stated at the site; the
// `blocks` selector reports the rest (see below).
//
// Ported from `Sim/icarus_sim/terrain_read_select.py` (SELECT_VERSION 1). The Python tree is
// being deleted; this is the only copy.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "fwg/build_stages.hpp"
#include "fwg/heroes.hpp"
#include "fwg/humans.hpp"
#include "fwg/key_locations.hpp"
#include "fwg/liveness.hpp"
#include "fwg/npcs.hpp"
#include "fwg/quests.hpp"
#include "fwg/settlements_stage.hpp"
#include "fwg/vec3.hpp"
#include "fwg/villains.hpp"

namespace fwg {

struct World;

inline constexpr int kSelectVersion = 1;

// ---------------------------------------------------------------------------
// The vocabularies.
// ---------------------------------------------------------------------------

// The registry of world-state blocks is the reference's `terrain_history.STATE_KEYS`, the
// 49 top-level blocks in its order: `state_keys()` in build_stages.hpp, which the stage
// inspector also reads. The `blocks` selector marks every block of a world against it in
// both directions. It is the reference's list and not this port's: where the C++ document
// does not carry a block, that disagreement is exactly what `blocks` exists to report.

// NEAR_CATEGORIES: the categories `near_rows` answers with, in a fixed order.
std::span<const std::string_view> near_categories();

// PLACE_KINDS, in the order `place_ids` lists them.
enum class PlaceKind : int { City = 0, Hamlet, Fortress, KeyLocation };
std::string_view to_string(PlaceKind kind);  // city | hamlet | fortress | key_location
std::span<const PlaceKind> place_kinds();

// STABLE_PLACE_KINDS: a city's uid and a key location's id survive an age advance; a
// hamlet's or a fortress's id is an ordinal that renumbers.
bool id_survives_an_age(PlaceKind kind);

// QUEST_STATES: every state the time API writes into the board, in the enum's order. Not
// widened here: a state this vocabulary invented would be one no writer can produce.
std::span<const QuestState> quest_states();

// ---------------------------------------------------------------------------
// blocks
// ---------------------------------------------------------------------------

// One top-level key of a world document.
struct BlockRow {
    std::string name;
    bool registered = false;              // in state_keys()
    std::optional<long long> version;     // block["version"], only when it is an integer
    std::string_view type;                // object | array | string | number | boolean | null
    std::optional<std::size_t> count;     // members of an object, elements of an array
    std::optional<std::size_t> bytes;     // its compact encoding; nullopt when unencodable
    bool encodable = true;
};

// The rows, plus both directions of disagreement with the registry.
struct BlockSummary {
    std::vector<BlockRow> blocks;               // sorted by name
    std::size_t total_bytes = 0;                // over the encodable rows
    std::vector<std::string> unregistered;      // carried, not in state_keys(), in row order
    std::vector<std::string> absent;            // in state_keys(), not carried, sorted
    std::vector<std::string> unencodable;       // in row order
    std::size_t registered_count = 0;
    std::size_t state_keys = 0;                 // state_keys().size()
};

// One row per top-level key of the world DOCUMENT, sorted by name: write_world_json's
// compact output with no timings, which is the document a host receives by default. Walking
// the document rather than the registry is the whole design -- a report built from the
// registry would omit a block the world carries and the registry does not, and here such a
// block shows up as `registered: false` instead of not showing up.
//
// **What the byte count means here.** The reference measured each block in its transfer
// encoding (sorted keys, no whitespace, UTF-8). This measures the same block as this
// library's writer encodes it: no whitespace and raw UTF-8, in the writer's key order, which
// changes no byte count. **Non-finite numbers differ.** The reference's encoder refused NaN
// and Infinity, so a block holding one had no byte count and read `encodable: false`. This
// library's writer never refuses: it writes NaN as null everywhere and, in every block
// written from stage 12 on, infinity as null too, so those blocks read encodable. Only an
// infinity the early writers spell `1e999` is detectable, and such a block reads
// `encodable: false` with no byte count, as the reference's did.
std::vector<BlockRow> block_rows(const World& world);

// The same rows, read from a world document's text. Accepts any valid JSON object, compact
// or not: whitespace outside strings is not counted. Throws std::invalid_argument when the
// text is not a JSON object. Exposed so the accounting can be tested on documents no
// generated world produces (a NaN, a duplicate key, a string version).
std::vector<BlockRow> block_rows_of(std::string_view document);

BlockSummary block_summary(const World& world);
BlockSummary block_summary_of(std::string_view document);

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

// config.size: the grid every node is numbered on.
int grid_size(const World& world);

// The radius every distance is measured against, in METRES: effective.globe_radius. The
// reference read effective_config first and fell back to config; a C++ World always
// carries both, and the effective one is the metres.
double globe_radius(const World& world);

// How many terrain nodes the world has, from the generator's own sphere grid. Nodes are
// numbered 0..count-1.
std::size_t node_count(const World& world);

// One terrain node: where it sits on the layer grids and the unit vector `direction(x, z,
// n)` gives its cell, which is what every placed thing carries as its own direction.
struct NodeCell {
    int node = 0;
    int x = 0;
    int z = 0;
    Vec3 direction{};
};

// Throws std::out_of_range for a node off the grid -- a precondition, which the near read
// refuses by name before it ever gets here.
NodeCell node_cell(const World& world, int node);

// Great-circle metres between two unit directions: great_circle_m, the one distance every
// subsystem measures with (vec3.hpp). Named for the reference's `arc_distance_m`.
double arc_distance_m(double radius, const Vec3& a, const Vec3& b);

// Every layer's value at one cell, by the layer's own name, sorted by name: an exact read of
// world.layers[name].at(x, z), never a recomputation. A cell off a layer's grid is skipped.
std::vector<std::pair<std::string, double>> layer_sample(const World& world, int x, int z);

// ---------------------------------------------------------------------------
// near
// ---------------------------------------------------------------------------

// A city. `kind` is the constant the reference's every settlement site carries ("city");
// the C++ SettlementSite has no such field.
struct NearSettlementRow {
    std::string uid;
    int id = 0;  // the ordinal; renumbers
    std::string_view kind = "city";
    std::string name;
    int node = 0;
    int x = 0;
    int z = 0;
    std::string_view city_class;  // capital | medium | small
    std::string population_profile;
    double distance_m = 0.0;
};

// A hamlet or a fortress. `id` is an ordinal that renumbers at an age boundary and the row
// says so; `core_id` is the POSITION in settlements.sites the writer stored, not a uid.
//
// Reproduced from the reference, which recorded it as a limitation: the row under-reports.
// The source record also carries a node-keyed `uid` that DOES survive an age, and the
// reference left adding it to the read contract to a product decision rather than taking it
// in passing. It is left out here too, and a test pins its absence.
struct NearSmallSiteRow {
    std::string id;
    std::string kind;  // hamlet | fortress
    int node = 0;
    int x = 0;
    int z = 0;
    int core_id = -1;
    std::string population_profile;
    bool id_survives_an_age = false;
    double distance_m = 0.0;
};

// A key location. `tier` is physical SCALE and `threat` is danger: two numbers on one row,
// both carried so a caller never has to pick the wrong one.
struct NearKeyLocationRow {
    std::string id;
    std::string kind;
    std::string name;
    std::string family;
    int node = 0;
    int x = 0;
    int z = 0;
    int tier = 0;
    int threat = 1;
    std::string state;
    std::string occupant;
    double distance_m = 0.0;
};

// A beast nest. `tier` here is a creature's danger, one to five -- not the key-location tier.
struct NearNestRow {
    std::string id;
    std::string species_id;
    std::string name;
    int node = 0;
    int x = 0;
    int z = 0;
    int tier = 1;
    std::string family;
    double range_m = 0.0;
    double distance_m = 0.0;
};

// A ley node, placed by its own direction. It stands on no terrain node, so `node` is always
// empty -- the reference's null, carried so the row says it rather than omitting it.
struct NearLeyNodeRow {
    std::string id;
    std::string school;
    std::string group;
    double intensity = 0.0;
    std::optional<int> node;
    double distance_m = 0.0;
};

// An occupancy row, resolved through the index the encounter pass built for it: by_node
// holds indices into `occupancy`, each of which names an index into `entries`. The entry's
// fields are empty when its index is off the list, as the reference read them from `{}`.
struct NearEncounterRow {
    std::size_t occupancy = 0;
    int node = 0;
    std::string camp_id;
    std::string camp_kind;
    std::optional<int> from_day;
    std::optional<int> to_day;
    std::vector<int> months;
    std::optional<std::string> group_uid;
    std::optional<std::string> source;
    std::optional<std::string> group_kind;
    std::optional<std::string> disposition;
    std::optional<int> threat_tier;
    std::optional<int> size;
    double distance_m = 0.0;
};

struct NearRows {
    std::vector<NearSettlementRow> settlements;
    std::vector<NearSmallSiteRow> hamlets;
    std::vector<NearSmallSiteRow> fortresses;
    std::vector<NearKeyLocationRow> key_locations;
    std::vector<NearNestRow> beast_nests;
    std::vector<NearLeyNodeRow> ley_nodes;
    std::vector<NearEncounterRow> encounters;

    std::size_t total() const;
};

// Everything within `radius_m` metres of one terrain node, by category.
//
// Each category is ordered by distance, then by the order its writer produced it -- a
// stable sort over the writer's own iteration, so a read never claims an order the
// generator did not have. Ley nodes are walked in the school table's order.
//
// **Two things on one node are at one node, by identity and not by arithmetic.** acos loses
// about half its digits next to a dot product of one, so a cell measured against itself
// geometrically comes out a fraction of a millimetre away; a row whose own node is the query
// node is at exactly 0 m, and a zero radius finds exactly what stands there. Otherwise a
// row's own direction is preferred, because it is what its writer stored; a row with only a
// node is placed at that node's cell, and a node off the grid drops the row rather than
// guessing. A row is in range when its distance is at most `radius_m + 1e-9`.
//
// Throws std::out_of_range for a node off the grid.
NearRows near_rows(const World& world, int node, double radius_m);

// ---------------------------------------------------------------------------
// place
// ---------------------------------------------------------------------------

struct PlaceId {
    std::string id;
    PlaceKind kind = PlaceKind::City;
};

// Every id `place_document` accepts, in block order: cities by settlements.sites[].uid,
// hamlets and fortresses by humans.hamlets[]/fortresses[].id, key locations by
// key_locations.sites[].id. An empty id is skipped, as the reference skipped a falsy one.
std::vector<PlaceId> place_ids(const World& world);

// One place's row, matched on the exact field its writer wrote. EQUALITY, never a prefix or
// a suffix: `surface-city-0-10-x` and `surface-city-0-100-x` share a prefix, and `hamlet-1`
// is a suffix of `hamlet-11`. The pointer is into `world`.
using PlaceRef = std::variant<const SettlementSite*, const RuralSite*, const KeyLocationSite*>;
struct PlaceMatch {
    PlaceRef row;
    PlaceKind kind = PlaceKind::City;
};
std::optional<PlaceMatch> find_place(const World& world, std::string_view place_id);

// A plan's identity, status and size -- never its body. A city plan is most of a megabyte,
// so it is named and measured rather than shipped: `bytes` is what the row costs in the
// world document, so a caller deciding to fetch it decides with a number.
//
// `status` is carried verbatim and is NOT one vocabulary: the three site planners write
// complete | partial | unbuildable and key_location_plans writes complete | empty. The two
// are returned under different keys of PlaceResolved (`plan`, `location_plan`) and never
// merged, so a consumer cannot compare `complete` across them.
struct PlanSummary {
    std::string_view block;     // city_plans | hamlet_plans | castle_plans | key_location_plans
    std::string_view id_field;  // city_uid | hamlet_id | fortress_id | location_id
    std::string id;
    std::string status;
    int version = 0;
    std::size_t plots = 0;
    std::size_t unplaced = 0;  // a key-location plan has no unplaced list: always 0
    std::optional<std::size_t> bytes;
    std::string_view writer;
};

// A hamlet's or a fortress's owning city, resolved from `core_id` -- checked, not trusted:
// the index must name a site whose own `id` is that index.
struct CoreCity {
    std::string uid;
    int id = 0;
    int node = 0;
    std::string name;
};

// One roster post, projected rather than copied: a capital holds hundreds, and the full
// record is what the person read is for. `status` is the roster's own word, alive | dead,
// never translated into the heroes' words.
struct NpcPostRow {
    std::string uid;
    std::optional<std::string> name;
    std::string post;
    std::optional<std::string> building_id;
    std::string_view status;
    bool important = false;
    std::optional<std::string> hero_uid;  // a cast record's hero; absent otherwise
};

// A join the answer resolved, and the site in THIS library that writes its key.
struct ReadJoin {
    std::string_view name;
    std::string_view from;
    std::string_view to;
    std::string_view writer;
};

// What joins to the place. A field the reference omits for a kind is empty here:
// `core`, `seasonal_food_model` and `seasonal_food_summary` belong to a city, `core_city`
// to a hamlet or a fortress. `plan` and `location_plan` are both always present and at most
// one is ever filled.
struct PlaceResolved {
    std::optional<CityCore> core;
    std::optional<FoodModel> seasonal_food_model;
    std::optional<SeasonalCity> seasonal_food_summary;
    std::optional<CoreCity> core_city;
    std::optional<PlanSummary> plan;
    std::optional<PlanSummary> location_plan;
    std::optional<NpcSite> npc_site;
    std::vector<NpcPostRow> npc_posts;
    std::size_t post_count = 0;  // the true total, however many were projected
    bool posts_truncated = false;
};

struct PlaceAnchor {
    std::string id;
    PlaceKind kind = PlaceKind::City;
    int node = 0;
    int x = 0;
    int z = 0;
    // The roster's site key: the city uid unchanged, or "<kind>-node-<node>" for a hamlet or
    // a fortress, because the ordinal ids renumber every age. Empty for a key location,
    // which the roster does not staff; asking for a key there would invent one.
    std::optional<std::string> roster_key;
    bool id_survives_an_age = false;
};

using PlaceRecord = std::variant<SettlementSite, RuralSite, KeyLocationSite>;

struct PlaceDocument {
    PlaceAnchor anchor;
    PlaceRecord place;  // the row itself, copied
    PlaceResolved resolved;
    std::vector<ReadJoin> joins;
};

// One place with the rows that join to it resolved, or nullopt if no such id exists. At
// most `max_posts` roster posts are projected; `post_count` is the true total.
std::optional<PlaceDocument> place_document(const World& world, std::string_view place_id,
                                            std::size_t max_posts);

// ---------------------------------------------------------------------------
// person
// ---------------------------------------------------------------------------

// Where a record lives: which list, and its position in it.
struct PersonEntry {
    LivenessBlock block = LivenessBlock::Heroes;
    std::size_t position = 0;
};

// uid -> where, for every person a world names: heroes.people, heroes.dreads, npcs.people,
// then villains.people. FIRST WRITER WINS, exactly as the quest lifecycle's giver index
// resolves heroes, dreads and npcs, with villains appended after them, so every uid that
// index resolves resolves identically here. A heroes or npcs package that failed is
// skipped: no people and a missing package are different facts.
std::map<std::string, PersonEntry> person_index(const World& world);

// uid -> node for the cities a world still has, then for the ones it used to have: an age
// advance moves a fallen city into `ruins` keeping its uid and node. Standing cities are
// indexed first, so a uid that is somehow both resolves to the standing city rather than to
// its own grave. Empty uids are skipped.
std::map<std::string, int> settlement_nodes(const World& world);

// Where a person stands, spelled out as a terrain node wherever one is knowable. Three
// blocks answer it three ways and none is a guess:
//
//   * a villain carries `node` on the record (source "villains.people[].node"; uid = its
//     seat);
//   * an NPC carries `presence_node` when their true placement differs from the site they
//     are filed under (source "npcs.people[].presence_node"), and otherwise stands at the
//     node of their `site_uid` row (source "npcs.sites[].node");
//   * a hero or a dread carries no node: its presence, then its home, is matched against
//     settlement uids and then ruin uids (source "settlements.sites[].uid" or
//     "ruins[].uid"). A presence naming a camp, college, port, shrine or nest resolves to
//     neither and is reported unresolved rather than approximated by the home.
struct PersonAnchor {
    std::optional<int> node;
    std::optional<std::string_view> source;
    std::optional<std::string> uid;
    std::optional<std::string> unresolved_uid;
};

PersonAnchor person_anchor(const World& world, const Hero& person);
PersonAnchor person_anchor(const World& world, const HeroDread& dread);
PersonAnchor person_anchor(const World& world, const NpcPerson& person);
PersonAnchor person_anchor(const World& world, const Villain& villain);
PersonAnchor person_anchor(const World& world, const PersonEntry& entry);

using PersonRecord = std::variant<Hero, HeroDread, NpcPerson, Villain>;

// One person, with liveness resolved through the one predicate (liveness.hpp): a caller
// never has to know which of the status vocabularies applies to the record it received.
struct PersonDocument {
    LivenessBlock block = LivenessBlock::Heroes;
    PersonRecord person;  // the record, copied
    Liveness liveness = Liveness::Present;
    PersonAnchor anchor;
};

std::optional<PersonDocument> person_document(const World& world, std::string_view uid);

// ---------------------------------------------------------------------------
// quests
// ---------------------------------------------------------------------------

// A quest's giver, resolved through the person index. `name` is the display name, else the
// name; `role` is the role, else the roster post -- the reference's `or` chains.
struct QuestGiverRead {
    std::string uid;
    LivenessBlock block = LivenessBlock::Heroes;
    std::optional<std::string> name;
    std::optional<std::string> role;
    Liveness liveness = Liveness::Present;
    PersonAnchor anchor;
};

// A board row with its giver resolved. `anchor` and `verb` are carried verbatim: they are
// the contract's own fields, derived once by the board from the hook, and re-deriving them
// here would be a second implementation of that rule.
struct QuestRow {
    std::string quest_id;
    QuestState state = QuestState::Offered;
    std::optional<std::string> giver_uid;
    std::optional<QuestGiverRead> giver;
    std::optional<std::string> anchor;
    std::optional<std::string> verb;
    std::optional<std::string> stated_purpose;
    bool unwitting = false;
    double offered_day = 0.0;
    double expires_day = 0.0;
    std::optional<double> closed_day;
    std::optional<QuestClosedReason> closed_reason;
};

// The board in the order the board sorts itself (by quest_id), filtered to `states` when
// given -- an empty filter selects nothing. Empty on a world with no board.
std::vector<QuestRow> quest_rows(const World& world, const std::optional<std::vector<QuestState>>& states,
                                 const std::map<std::string, PersonEntry>& person_lookup);

// How many quests stand in each state, over the WHOLE board rather than a filtered view.
// `unknown_state` is present only when a row's state is outside the vocabulary.
struct QuestCounts {
    std::array<std::size_t, 6> by_state{};  // in QuestState order
    std::size_t total = 0;
    std::optional<std::size_t> unknown_state;

    std::size_t at(QuestState state) const { return by_state[static_cast<std::size_t>(state)]; }
};

QuestCounts quest_counts(const World& world);

}  // namespace fwg
