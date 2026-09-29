// The cast a finished world's history precipitates, and the quests that cast can offer.
//
// A character here is a NAMED HANDLE ON AN EVENT THE WORLD ALREADY RECORDED. Nothing is
// rolled from a template: the heir points at the ruin, the warlord at the war, the Dread
// at the nest the ruin's own evidence names. Seven wells walk the record -- ruins, magic,
// cities, guilds, countryside, ports, shrines -- and each candidate rolls ONCE against the
// precipitation policy. The roll is exported whether it succeeded or not, so the block
// records what history could have produced and did not; without the ledger a role that
// rolled and lost is indistinguishable from a role that was never implemented.
//
// This pass READS ONLY. It takes an explicit request and returns a value; nothing in the
// request is modified and nothing else in the world is written. That is what makes the
// try/catch boundary in `attach_heroes` safe and what lets a saved world replay identically
// to a live result.
//
// Nothing here is planned: the log, the situation, the faces and the hooks are the data an
// orchestrator improvises from. `limits()` says so in the exported block and is part of the
// contract, not decoration.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/grid.hpp"
#include "fwg/random.hpp"
#include "fwg/vec3.hpp"

namespace fwg {

// Stamped into every block. The reference is at 2, which added the quest contract's
// derived fields (verb, difficulty, target_node, unsited_reason) to every hook, so a
// consumer can tell a world whose hooks can be priced from one whose hooks cannot. 3 gives a
// castellan a past: the wars their city fought in their lifetime as deeds, and `tenure_years`,
// `served_at`, `served_as` and `liege_uid`. 4 gives every deed its `scope`, how far word of it went,
// and marks the one each record is known for (`known_for`).
inline constexpr int kHeroesVersion = 4;

// ---------------------------------------------------------------------------
// The authored catalogue, generated from data/hero-*.json into
// src/generated/content_heroes.cpp by tools/embed/heroes.py.
// ---------------------------------------------------------------------------

struct HeroAxisPair {
    double law = 0.0;
    double good = 0.0;
};

// One `requires` clause: every token must hold. A card is eligible when ANY clause does.
// Negation does not exist here -- only story_web's trope predicates carry `!`.
struct HeroClause {
    std::span<const std::string_view> tokens;
};

// A multiplier that applies when the person carries `feature`.
//
// FILE ORDER IS KEPT. The weight is a float product over these, multiplication does not
// associate, and a reordered table can move the last bit of a card's weight and flip a
// knife-edge draw.
struct HeroBoost {
    std::string_view feature;
    double factor;
};

struct HeroCardCore {
    std::string_view belief, wants, fears, tells, mechanic;
    // ORDER SURVIVES INTO THE EXPORT: the persona's `never` list starts with this and
    // appends the two overlays, deduplicating first-wins.
    std::span<const std::string_view> never;
};

struct HeroPoleNotes {
    std::string_view lawful, chaotic, good, evil;
};

// A sample utterance keyed by the two-character alignment code.
//
// FILE ORDER IS A CONTRACT and it is the least obvious thing in the catalogue. When a
// person's exact code is absent the fallback takes the first line whose GOOD letter
// matches, and failing that the FIRST LINE IN FILE ORDER. 27 of the 32 cards carry no
// neutral-good line, so every neutral-good persona in every world reads that first entry.
struct HeroLine {
    std::string_view code;
    std::string_view text;
};

// Applied AFTER the card is chosen, so a card is picked for a person who already has a
// moral colour and the push is the card's own signature.
enum class HeroIntensity : int { None = 0, Corner, GoodAxis };

// Which pair of faces a card declares. `Single` is the common case: one face.
enum class HeroFaceSpec : int { Single = 0, PublicTrue, CalmUnleashed, FirstSecond };

struct HeroArchetypeCard {
    std::string_view id, name;
    // A frequency tilt, never a filter. "A Conqueror is more often lawful, never only
    // lawful. Every archetype exists in all nine cells."
    HeroAxisPair prior;
    std::span<const HeroClause> requires_any;
    std::span<const HeroBoost> boosts;
    bool has_drift = false;
    HeroAxisPair drift;
    HeroIntensity intensity = HeroIntensity::None;
    HeroCardCore core;
    HeroPoleNotes pole_notes;
    std::span<const HeroLine> lines;
    HeroFaceSpec faces = HeroFaceSpec::Single;
};

// ORDER IS A CONTRACT: `choose_archetype` collects the eligible cards in this order and
// walks them with a SUBTRACTIVE cumulative selector, so the array position is observable
// in the chosen card for a given draw. Nothing sorts first.
std::span<const HeroArchetypeCard> hero_archetype_cards();
const HeroArchetypeCard* hero_archetype_card(std::string_view id);

// The prose one alignment pole contributes to every persona, whatever the card.
struct HeroOverlay {
    std::string_view voice, lies, cornered, toward_player;
    std::span<const std::string_view> never;
};

// Indexed by pole + 1, so 0 is chaotic/evil, 1 neutral, 2 lawful/good. The sign-to-index
// computation is shared with the name and code tables, which run the same way.
std::span<const HeroOverlay> hero_law_overlays();
std::span<const HeroOverlay> hero_good_overlays();

// An epithet or cover-role list for one role.
//
// EVERY LIST IN THE NAMES DOCUMENT IS POSITIONALLY DRAWN. The epithet draw filters to the
// templates whose placeholders the person can fill -- preserving relative order -- and
// then indexes into what survives, so both the full order and every surviving subsequence
// are contracts. The `legend` list is APPENDED AT THE TAIL for a legend, never merged, so
// the role's own entries keep indices 0..n-1.
struct HeroNameList {
    std::string_view role;
    std::span<const std::string_view> entries;
};
std::span<const HeroNameList> hero_epithets();
std::span<const HeroNameList> hero_cover_roles();
// Empty when the role has no list of its own. `hero_cover_roles` additionally carries a
// `default` row, which is the fallback and is required.
std::span<const std::string_view> hero_epithets_for(std::string_view role);
std::span<const std::string_view> hero_covers_for(std::string_view role);

// The pool one person draws an epithet from, IN DRAW ORDER: the role's own list, then the
// legend list when the person is a legend.
//
// Published because the tail-append is otherwise unobservable. Prepending the legend
// entries leaves the SET of possible epithets identical and quietly re-maps every legend's
// draw, so no frequency test and no output check can see it -- only the order can.
std::vector<std::string_view> hero_epithet_candidates(std::string_view role, bool legend);

struct HeroClassBonus {
    double small = 0.0, medium = 0.0, capital = 0.0;
};

// Every number the wells policy carries. Flattened rather than kept as a tree because
// every one of them is read by exactly one line and a tree would only add a lookup.
struct HeroWellsPolicy {
    // The hard ceiling on every precipitation chance. The 0.05 gap below certainty is what
    // keeps "history could have produced this and did not" possible everywhere.
    double cap;

    double heir_base;
    HeroClassBonus heir_per_city_class;
    double warlord_base, warlord_civil, warlord_regional, warlord_international;
    double warlord_per_extra_war;
    bool warlord_one_per_city;
    double dread_base, dread_per_nest_tier;
    double domain_holder_base, domain_holder_per_intensity;
    double magister_base, college_magister_base;
    double camp_base;
    HeroClassBonus camp_per_city_class;
    double sovereign_base;
    double council_base, council_capital_bonus;
    double diaspora_base;
    double champion_base, champion_per_nest_tier, champion_reach_factor;
    double reeve_base, reeve_starving_bonus, reeve_threatened_bonus, reeve_reach_factor;
    double castellan_base, castellan_pressed_bonus, castellan_per_defence;
    double harbourmaster_base, harbourmaster_per_route, harbourmaster_terminal_bonus;
    double keeper_base, cult_base;

    // The +/- magnitude a ley hook proposes. Its SIGN, not its size, chooses the verb.
    double hook_intensity_delta;
    std::string_view ley_verb_strengthen, ley_verb_weaken;
    double ley_floor, ley_intensity_weight;
    double pressure_per_tier, pressure_cap;
    int difficulty_min, difficulty_max;

    // The umbral/infernal layer value above which a home counts as tainted ground.
    double tainted_threshold;
    bool heir_leads_own_camp;
};
const HeroWellsPolicy& hero_wells_policy();

// The council seats a city class declares. LIST ORDER IS THE CANDIDATE ORDER, therefore
// the roll-ledger order and the council org's `members` order, and both are exported.
std::span<const std::string_view> hero_seats_for(std::string_view city_class);
// The schools that set `school:dark`. NOTE that `weave`, the default fallback school, is
// in this list, so a magister with no recorded school is dark by default.
std::span<const std::string_view> hero_dark_schools();
// Empty when the school has no opposite, which means NO counter-node hook is emitted --
// not a self-mapping and not a zero.
std::string_view hero_opposite_school(std::string_view school);
// The two situations a dispossessed person drawn to a camp lands in. POSITIONALLY DRAWN.
std::span<const std::string_view> hero_magister_at_camp();
// The difficulty floor for a hook kind. An unlisted kind prices at `difficulty_min`.
double hero_opposition_for(std::string_view kind);

struct HeroAlignmentPolicy {
    // The half-width of the uniform spread on each axis. Larger than the 0.34 pole cut, so
    // jitter alone can move an undecided person across a boundary; the sign lock is what
    // stops it undoing a deed.
    double jitter;
    HeroAxisPair dread_base;
};
const HeroAlignmentPolicy& hero_alignment_policy();
// Absence here is EXACTLY an authored {0, 0}: the shift is added, not multiplied.
HeroAxisPair hero_civilization_bias(std::string_view civilization_id);
HeroAxisPair hero_deed_shift(std::string_view key);
HeroAxisPair hero_dread_family_shift(std::string_view family);

// The 82 feature tokens an archetype precondition may name.
//
// Published because the story web declares the same vocabulary and a test ties the two
// lists together; the reference held two literal copies in sync by comment alone.
std::span<const std::string_view> hero_feature_vocabulary();

// The closed quest-verb vocabulary. Code and not policy, deliberately: "a new verb needs a
// new hook branch to emit it", so adding one here without a branch changes nothing.
std::span<const std::string_view> hero_quest_verbs();
// Why a hook has no `target_node`. A published vocabulary a consumer branches on.
std::span<const std::string_view> hero_unsited_reasons();

struct HeroFameTuning {
    double small, medium, capital;      // class weight; an unknown class reads `small`
    double civil, regional, international;  // war weight; an unknown kind reads `civil`
    double hamlet, fortress, port, shrine;  // site weight
    double legend_reach;
    double legendary_floor, renowned_floor;  // a DESCENDING ladder, walked top-down
};
const HeroFameTuning& hero_fame_tuning();
// How far word of a deed went (heroes version 4), from its own fame: "local" under the renowned floor,
// "regional" under the legendary one, "realm" at or past it. Nothing wider: no deed of fifty worlds reached
// twice the legendary floor (2026-09-29).
std::string hero_deed_scope(double fame);
// An unknown seat weighs 1.0. `librarian` is in the table and unreachable: no city class
// declares that seat, so nothing can ever be seated in it.
double hero_seat_weight(std::string_view seat);

struct HeroTextPair {
    std::string_view key, value;
};

// The relic a cause leaves behind: a kind and a name template over {name} and {school}.
struct HeroRelicRule {
    std::string_view cause_kind;  // war | beast | self_magic | ley
    std::string_view relic_kind;
    std::string_view name_template;
};

// The authored English and the authored classifications the reference kept in its code
// rather than in a policy document. Carried as data because they are content: every one of
// them reaches a player through a log line, a situation sentence or a relic's name.
//
// All are keyed lookups, so ORDER IS FREE throughout -- which is worth saying positively,
// because it is the exception in this subsystem.
struct HeroWordTables {
    std::span<const HeroTextPair> class_word;      // small -> "small city"
    std::span<const HeroTextPair> cause_phrases;   // dragon -> "a dragon"
    std::span<const HeroTextPair> companion_join;  // captive -> "rescued"
    std::span<const HeroTextPair> diaspora_roles;  // exile -> "exile"
    std::span<const HeroTextPair> seat_titles;     // market_steward -> "Steward"
    // An unlisted seat is a NAMED FALLBACK, not an error and not an empty string.
    std::string_view seat_title_default;
    std::span<const HeroTextPair> role_word;  // farming -> "farmstead"
    std::string_view role_word_default;
    std::span<const HeroTextPair> site_roles;  // shrine -> "keeper", cult -> "heresiarch"
    std::span<const HeroRelicRule> relic_by_cause;

    std::span<const std::string_view> beast_causes;          // dragon, monster
    std::span<const std::string_view> long_lived_families;
    std::span<const std::string_view> dark_families;
    std::span<const std::string_view> seated_roles;          // sovereign, warlord
    std::span<const std::string_view> tainting_layers;       // ley_umbral, ley_infernal
    std::span<const std::string_view> seat_exempt_roles;     // who does NOT gain seat:none
    std::span<const std::string_view> school_scoring_excludes;  // ley_holy, ley_primordial
    // A one-element list whose MEMBERSHIP exempts a hook kind from the pressure term.
    std::span<const std::string_view> pressure_exempt;

    std::string_view default_school;    // weave
    std::string_view self_magic_cause;  // the cause a city that unmade itself carries
    std::string_view deed_keeper;
};
const HeroWordTables& hero_word_tables();
std::string_view hero_word(std::span<const HeroTextPair> table, std::string_view key,
                           std::string_view fallback = {});

struct HeroCatalogueIdentity {
    int schema_version;
    // The five `revision` integers, in POLICY_FILES order (archetypes, axis_overlays,
    // alignment, names, wells), republished into the block as `policy_revision` and read
    // back by the story web as `heroes_revision`.
    int archetypes, axis_overlays, alignment, names, wells;
    std::string_view sha256;  // over THIS data, so a content edit is visible
};
HeroCatalogueIdentity hero_catalogue_identity();

// Fixed prose, published in the block. Both are load-bearing: `limits` is the sentence
// that stops a reader treating the cast as a demographic claim.
std::string_view heroes_method();
std::string_view heroes_limits();

// Raise on a catalogue that could not be resolved. The generator already refuses to emit a
// broken table, so this is the runtime half of the same gate: it proves the table that was
// actually linked in is the one the codegen checked.
void lint_hero_catalogue();

// ---------------------------------------------------------------------------
// What the world has to say for itself. Every record is what the reference reads from one
// exported block, with the join field named, because no two blocks name their key alike.
// ---------------------------------------------------------------------------

// A war one city won and another lost.
struct HeroWar {
    std::string id;
    int age = 0;
    std::string kind;  // civil | regional | international
    std::string victor_uid;
    std::string civilization_id;             // the victor's
    std::string victor_parent_race_id;       // empty falls through to the city's own
    std::string defeated_uid;
};

// One row of a city's or ruin's own war record.
struct HeroWarRecord {
    std::string war_id;
    int age = 0;
    std::string outcome;  // victor | defeated
    std::string kind;
    std::string opponent_uid;
};

// A fallen city. `uid` is THE key; `id` is the DIFFERENT, `ruin-`-prefixed string the ley
// key points are built from. Both are used and they are not interchangeable.
struct HeroRuin {
    std::string uid;
    std::string id;
    std::string name;
    std::string city_class = "small";  // small | medium | capital
    std::string cause;  // war_<kind> | dragon | monster | self_magic | a ley school word
    int destroyed_age = 0;
    std::string civilization_id;
    std::string population_profile;
    std::string parent_race_id;
    int node = -1;
    int x = 0, z = 0;
    bool has_direction = false;
    Vec3 direction{};
    std::string new_node_school;  // empty means the magister falls back to `weave`
    std::string evidence_nest_id;
    std::vector<HeroWarRecord> war_history;
};

// A standing city. `uid` is the key religion, threat and the wars join on; `id` is the
// DIFFERENT key colleges, hamlets, fortresses, ports and shipments point at.
struct HeroCity {
    std::string uid;
    std::string id;
    std::string name;
    std::string city_class = "small";
    std::string civilization_id;
    std::string population_profile;
    std::string parent_race_id;
    int node = -1;
    int x = 0, z = 0;
    Vec3 direction{};
    double suitability = 0.0;
    int founded_age = 0;
    bool founding_capital = false;
    // religious_schism | exile | expedition | magical_displacement; empty for a city that
    // was not founded by a diaspora.
    std::string diaspora_reason;
    std::string source_civilization_id;
    std::vector<HeroWarRecord> war_history;
};

struct HeroCivilization {
    std::string id;
    std::string name;
    std::string parent_race_id;
};

// A road-linked group of one people's cities. `city_ids` joins on the settlement `id`,
// NOT the uid.
struct HeroCulture {
    std::string id;
    std::string civilization_id;
    std::vector<std::string> city_ids;
};

// `range_m` is ABSOLUTE metres, post-scaling. `tier` is a DANGER rating 1..5 and is the
// quest rubric itself; it is not a settlement scale and not a fame band.
struct HeroNest {
    std::string id;
    std::string name;
    std::string family;
    int tier = 1;
    bool has_tier = false;    // the guilds well's default is 1 and the dread's is 5
    double range_m = 1000.0;
    bool has_range = false;   // `range_m: 0` is a real value in the guilds well and not 1000
    bool has_grid = false;    // a nest with no x/z is skipped by every grid-metre search
    int x = 0, z = 0;
    Vec3 direction{};
    // The terrain node the lair stands on. The reference's `beast_nests.sites[]` rows do
    // NOT carry one, so EVERY slay hook in a reference world reports `target_has_no_node`
    // and a player is never pointed at a lair. Left at -1 that behaviour is reproduced
    // exactly; a caller that has the node may supply it and site the hook.
    int node = -1;
};

// `id` is AN ORDINAL. See `hero_small_site_anchor` for why nothing may be keyed on it.
struct HeroHamlet {
    std::string id;
    std::string kind;  // only `hamlet` produces a reeve
    std::string role;  // farming | resource | coastal harbor | ...
    std::string core_id;  // a city `id`
    int node = -1;
    int x = 0, z = 0;
};

struct HeroFortress {
    std::string id;  // AN ORDINAL, rebuilt in descending defence_score every age
    std::string core_id;
    int node = -1;
    int x = 0, z = 0;
    double defence_score = 0.0;
};

struct HeroRuralCore {
    std::string site_id;  // a city `id`
    double food_deficit = 0.0;
};

struct HeroShipment {
    std::string from;  // city `id`s, counted per endpoint
    std::string to;
};

struct HeroCollege {
    std::string id;
    std::string core_id;  // a city `id`
    int node = -1;
    int x = 0, z = 0;
};

struct HeroLeyNodeRef {
    std::string id;
    Vec3 direction{};
    double intensity = 0.0;
};

// One school's node list.
//
// A VECTOR AND NOT A MAP, on purpose. The reference builds the node->school map by walking
// the networks dict and the node->intensity map by walking the SAME dict SORTED, so a node
// id present in two schools resolves differently in the two passes. That is recorded as an
// open question in the port spec; here both maps are built from this one order, so they
// agree by construction.
struct HeroLeyNetwork {
    std::string school;
    std::vector<HeroLeyNodeRef> nodes;
};

struct HeroGod {
    std::string id;
    std::string name;
};

// `node` is carried and `x`/`z` are NOT: a shrine stands on a terrain node with no grid
// position, so the ruin it was born of answers for it when a hook measures pressure.
struct HeroReligionSite {
    std::string id;
    std::string kind;  // shrine | cult
    std::string god_id;
    std::string ruin_id;  // a ruin `id`
    int node = -1;
    bool born_under_surge = false;
};

struct HeroPort {
    std::string id;
    std::string core_id;  // a city `id`
    int node = -1;
    int sea_node = -1;
    bool trade_terminal = false;
};

// A sea lane. The reference reads the endpoints from any of six differently-named fields;
// they are collapsed here into the set of strings a route touches.
struct HeroSeaRoute {
    std::vector<std::string> endpoints;
};

struct HeroCityThreat {
    std::string city_uid;
    double war_pressure = 0.0;
};

struct HeroesRequest {
    std::uint64_t seed = 0;
    // len(history.ages). Everything born "now" is born at max(1, final_age), and a person
    // is living only while final_age - born_age <= 1.
    int final_age = 0;

    // ABSOLUTE METRES, post-scaling: `world.effective.globe_radius`, not `world.config`'s.
    // The two differ by the world scale. The champion reach test compares a great-circle
    // distance against a nest's `range_m` in metres, so handing this design units (or the
    // reference's silent 1.0 default) compares radians with metres and quietly produces no
    // champions at all. `resolve_heroes` therefore REFUSES a non-positive radius rather
    // than answering a different question -- a deliberate departure from the reference,
    // which defaults it to 1.0.
    double globe_radius_m = 0.0;
    // Metres per grid node. Every countryside and quest-pressure distance is measured on
    // the grid with this, never on the globe, so the two passes cannot disagree.
    double spacing_m = 1000.0;

    std::vector<HeroRuin> ruins;
    std::vector<HeroCity> cities;
    std::vector<HeroWar> wars;  // every age's wars, flattened, in age order
    std::vector<HeroCivilization> civilizations;  // the whole registry, cities or none
    std::vector<HeroCulture> cultures;
    std::vector<HeroNest> nests;
    std::vector<HeroHamlet> hamlets;
    std::vector<HeroFortress> fortresses;
    std::vector<HeroRuralCore> cores;
    std::vector<HeroShipment> shipments;
    std::vector<HeroCollege> colleges;
    std::vector<HeroLeyNetwork> networks;
    std::vector<HeroGod> gods;
    std::vector<HeroReligionSite> religion_sites;
    std::vector<HeroPort> ports;
    std::vector<HeroSeaRoute> sea_routes;
    std::vector<HeroCityThreat> threats;

    // The ley potency grids, keyed `ley_<school>`. Read at two places only: the
    // tainted-home test and the college's dominant-school scan. Any lookup failure is
    // swallowed and costs one feature, exactly as the reference's try/except does.
    const std::map<std::string, Grid>* layers = nullptr;
};

// ---------------------------------------------------------------------------
// What comes out.
// ---------------------------------------------------------------------------

struct HeroSiteRef {
    std::string kind;  // city | ruin
    std::string uid;
    std::string name;
};

struct HeroPresence {
    std::string site_kind;  // city | ruin | camp | college | nest | hamlet | fortress | port | shrine
    std::string uid;
    // One of the nineteen the story web knows. An unlisted word silently costs that hero
    // its `presence:` fact there, which is why the vocabulary is published.
    std::string situation;
};

struct HeroClaim {
    std::string verb;
    std::string target_uid;
    std::string target_name;
};

// The city this person lost. Carried by pretenders, domain holders, ruin magisters and
// fallen champions.
struct HeroLost {
    std::string uid;
    std::string ruin_id;
    std::string name;
    std::string city_class;
    std::string cause;
    int destroyed_age = 0;
};

struct HeroAlignment {
    double law = 0.0, good = 0.0;  // 3 dp
    std::string label;             // "Lawful Good", or the single word "True Neutral"
    std::string code;              // two characters, e.g. "LG"
    bool has_drift = false;
    HeroAxisPair drift;
};

struct HeroPersona {
    std::string archetype;
    std::string belief, wants, fears, tells, mechanic;
    std::string manner, voice, lies, toward_player, cornered;
    // The union of every hard line, so a downstream proposal that crosses one can be
    // rejected mechanically. Order: the card's own, then the law overlay, then the good.
    std::vector<std::string> never;
    std::string line;
};

struct HeroFace {
    std::string label;  // public | true | calm | unleashed | first | second
    std::string name;
    std::string apparent_role;  // the real role, or a cover trade on a public face
    HeroAlignment alignment;
    HeroClaim claim;
    HeroPersona persona;
};

struct HeroDeed {
    std::string event_kind;  // ruin | war | nest | founding | hamlet | fortress | port | shrine
    std::string event_id;
    int age = 0;
    std::string role;  // heir | actual | keeper | credited | victim
    std::string kind;  // war only
    std::string opponent_uid;
    std::string defeated_uid, defeated_name, defeated_class;  // a warlord's war deed
    std::string cause, city_name, city_class;                 // a dread's ruin deed
    int tier = 0;
    bool has_tier = false;
    std::string reason;  // founding and shrine deeds
    // How far word of the deed went (heroes version 4): its own weight in the fame formula, times the
    // reach of the one who did it, against the fame floors: "local" under the renowned floor,
    // "regional" under the legendary one, "realm" at or past it. And whether it is
    // the deed they are known for: their widest, the first of equals in the order the deeds were
    // written. Exactly one of a record's deeds is, when it has any.
    std::string scope;
    bool known_for = false;
};

struct HeroLogEntry {
    int age = 0;
    std::string event_kind, event_id, role, text;
};

struct HeroCompanion {
    bool eligible = false;
    std::string join_condition;   // rescued | found | hired; empty is null
    std::string offered_from_face;
};

struct HeroDefeatedCity {
    std::string uid, name, war_id, kind;
};

struct HeroCollegeRef {
    std::string id;
    int node = -1, x = 0, z = 0;
};

struct Hero {
    std::string uid;   // THE key field, shape `hero-<role>-<anchor>`
    std::string role;
    std::string well;
    std::string status;  // living | legend
    std::string civilization_id;
    // The PARENT race. It selects a language family and nothing finer; the civilization
    // selects the language.
    std::string race_id;
    int born_age = 0;

    std::optional<HeroSiteRef> home;
    std::optional<HeroPresence> presence;
    std::string realm_uid;  // empty is null
    std::vector<HeroDeed> deeds;
    HeroClaim claim;

    double fame = 0.0;   // 3 dp
    // A FAME BAND, not a danger rating: notable | renowned | legendary. The word `tier`
    // means a creature's danger elsewhere in this library and the two do not join.
    std::string tier;

    std::string name, epithet, display_name;
    std::string archetype, archetype_name;
    HeroAlignment alignment;
    HeroPersona persona;
    std::vector<HeroFace> faces;
    std::vector<HeroLogEntry> log;
    std::string situation;  // one sentence, sometimes empty
    HeroCompanion companion;

    std::vector<std::string> rivals, kin, allies;  // sorted uid lists
    // Which org this person belongs to. The reference leaves this null ALWAYS and the
    // person->org back-reference is dead; it is filled here from the orgs' own member
    // lists, which is what the field was evidently for. A DELIBERATE DEPARTURE.
    std::string org_uid;
    // A per-person sub-seed handed to consumers to branch from. Opaque.
    std::uint32_t seed = 0;
    // The feature tokens that were true, sorted. This is what the story web reads.
    std::vector<std::string> selectable;

    // Always present in the export, usually empty.
    std::string predecessor_uid, school, node_id, seat, nest_id, site_name;

    std::optional<HeroLost> lost;
    std::string victor_uid, victor_name;
    bool victor_fell = false;
    std::vector<HeroDefeatedCity> defeated;
    std::optional<HeroCollegeRef> college;
    std::string nest_name;
    int routes = 0;
    bool has_routes = false;
    std::string god_id, diaspora_reason, mother_civilization_id;
    // A castellan's past (heroes version 3): how many years they have held their post; where they
    // served before it (a site's name) and as what ("soldier", "sergeant", or "second" to another
    // fortress's castellan); and the realm's sovereign they are sworn to. Zero and empty for everyone
    // else.
    int tenure_years = 0;
    std::string served_at, served_as, liege_uid;

    // --- construction scratch, not part of the exported record ---
    //
    // ORDERED, not hashed. `selectable` is then just a copy, and iteration during
    // debugging is deterministic instead of depending on a hash seed.
    std::set<std::string> features;
    // The self-magic magister and the fallen champion. Placement reads it and the
    // reference deletes the field before export.
    bool dispossessed = false;
    // The globe direction of the ground this person stands on, used only to find the
    // nearest opposing ley node. Deleted before export in the reference.
    std::optional<Vec3> anchor_direction;
};

struct HeroDreadKill {
    std::string uid, name;
    int age = 0;
};

// A named monster. A dread gets NO rivals, kin, allies, claim, civilization, home or
// faces, so in the story web it always reads `claim:none` and `home:none`.
struct HeroDread {
    std::string uid;  // `dread-<nest id>`
    std::string role = "dread";
    std::string well = "ruins";
    std::string nest_id;
    std::string species;
    std::string family;  // empty when the nest is gone
    int nest_tier = 5;
    std::string status;
    std::optional<HeroPresence> presence;
    std::vector<HeroDeed> deeds;
    std::vector<HeroDreadKill> destroyed;
    double fame = 0.0;
    std::string tier;
    std::string name;          // the epithet alone
    std::string display_name;  // "<species>, <epithet>"
    // A dread with no eligible card keeps these empty and does NOT raise; a person does.
    std::string archetype, archetype_name;
    bool has_persona = false;
    HeroPersona persona;
    HeroAlignment alignment;
    std::vector<HeroLogEntry> log;
    std::string situation;
    std::vector<std::string> selectable;
    std::uint32_t seed = 0;

    std::set<std::string> features;  // construction scratch; see Hero::features
};

struct HeroRealm {
    std::string uid;  // `realm-<culture id>`
    std::string name;          // in the people's own tongue
    std::string english_name;  // "Realm of <capital>"; a map label and a chronicle line
    std::string civilization_id, civilization_name, parent_race_id;
    std::string capital_uid;
    std::vector<std::string> city_uids;  // in culture order
    std::string culture_id;
    std::string sovereign_uid;  // empty until the cities well seats one
};

struct HeroOrg {
    std::string uid;
    std::string kind;  // order | remnant | dynasty | council
    std::string name;
    HeroSiteRef home;
    std::vector<std::string> members;
    // {against, nest_id} for an order or remnant, {holds} for a dynasty or council.
    std::string against, nest_id, holds;
};

struct HeroCamp {
    std::string uid;  // `camp-<ruin uid>`
    std::string ruin_uid, name;
    int node = -1, x = 0, z = 0;
    Vec3 direction{};
    std::string origin = "ruin_squat";
    std::string civilization_id;
    int since_age = 0;
    std::string leader_uid;  // present only when an heir leads it
};

struct HeroRelic {
    std::string uid;  // `relic-<ruin uid>`
    std::string kind;  // focus | banner | crown | reliquary
    std::string name;
    std::string origin_event;  // the ruin `id`
    std::string resting_at;    // the ruin `uid`
    std::string holder_uid;    // always empty: a relic rests until somebody takes it
    std::string school;        // a focus only
};

struct HeroBond {
    std::string a, b;
    std::string kind;  // rival | kin | ally | mentor | betrayed
    // A war id, a ruin id or a deed's event id -- except on a council->sovereign edge,
    // where the reference writes the HOME CITY UID. Carried as-is; see the report.
    std::string from_event;
};

// A claim a legend left open. NOTE `predecessor_uid` here is THE LEGEND'S OWN uid, where
// the same field name on a `Hero` is the legend they inherit FROM. Two referents, one
// name, in one block; recorded rather than renamed so a consumer of the reference's
// documents still joins correctly.
struct HeroMantle {
    HeroClaim claim;
    std::string predecessor_uid;
    std::string civilization_id;
    int vacant_since_age = 0;
};

// Every evaluated candidate, precipitated or not. This is the whole point of the design:
// the block records what history could have produced and did not.
struct HeroRoll {
    std::string candidate_uid;
    std::string kind;
    std::string event_id;  // empty when the candidate has no deeds
    int age = 0;
    bool has_age = false;
    double chance = 0.0;  // 0..cap, 3 dp
    double roll = 0.0;    // 0..1, 6 dp
    bool precipitated = false;
};

// What a hook actually does, typed against a thing the world has.
struct HeroEffect {
    std::string kind;  // ruin | relic | city | nest | hamlet | fortress | port | shrine | college | ley_node
    std::string uid;
    std::string node_id;  // ley_node only
    std::string nest_id;  // nest only
    std::string action;   // empty on a ley effect, whose verb comes from the delta's sign
    std::string school;
    double intensity_delta = 0.0;
    bool has_delta = false;
};

// An anchor, a verb and a difficulty. There is NO reward, loot or xp field at any version:
// the generator emits what the quest is, and the consuming game prices it.
struct HeroQuestHook {
    std::string hook_id;  // `hook-<giver uid>-<index>`
    std::string giver_uid;
    std::string offered_from_face;
    std::string stated_purpose;
    HeroEffect actual_effect;
    // True when the stated purpose comes from a public face: the player is handed the
    // wrong reason for a real effect.
    bool unwitting = false;
    std::string target;  // the id the effect names
    std::string verb;    // one of the ten; anything else is a hard error
    int difficulty = 1;  // 1 harmless .. 5 a campaign threat
    int target_node = -1;
    bool has_target_node = false;
    std::string unsited_reason;  // empty when sited
};

// One row per role the precipitation policy declares, whether or not it fired.
//
// `sources` is a SECOND, independent count: how many world records the role had to work
// with before any roll. It exceeds `candidates` wherever the well walked records that did
// not qualify. What this does NOT establish is whether a zero is correct -- it reports
// that no ruin named a beast, not whether a world of this size ought to have produced one.
struct HeroDiagnostic {
    std::string role;
    int placed = 0;
    int candidates = 0;
    int sources = 0;
    std::string source_kind;  // the published phrase the reason sentence is built from
    std::string reason;
};

struct HeroesSummary {
    int living = 0, legends = 0, dreads = 0, realms = 0, orgs = 0, camps = 0, hooks = 0;
    int candidates = 0, precipitated = 0;
};

struct HeroesBlock {
    int version = kHeroesVersion;
    // ok | failed. A failed block carries `error` and nothing else; the world still
    // returns, which is the whole of what the isolation contract buys.
    std::string status = "ok";
    std::string error;
    HeroCatalogueIdentity policy_revision{};
    int final_age = 0;
    HeroesSummary summary;
    std::vector<Hero> people;
    std::vector<HeroOrg> orgs;
    std::vector<HeroRealm> realms;
    std::vector<HeroDread> dreads;
    std::vector<HeroCamp> camps;
    std::vector<HeroQuestHook> quest_hooks;
    std::vector<HeroBond> bonds;
    std::vector<HeroRelic> relics;
    std::vector<HeroMantle> mantles;
    std::vector<HeroRoll> rolls;
    std::vector<HeroDiagnostic> diagnostics;
    std::string method;
    std::string limits;
};

// ---------------------------------------------------------------------------
// The pass, and the pieces a caller or a test may want on their own.
// ---------------------------------------------------------------------------

// The handle a countryside person is keyed on: the terrain node, spelled out.
//
// `humans.hamlets[].id` and `humans.fortresses[].id` are ORDINALS -- the position in an
// accepted list that is rebuilt every age, fortresses in descending defence_score over a
// strategic map rebuilt from the road graph. Cities die, roads move and war history
// reweights the score, so both the membership and the ordering change at every age
// boundary: `fortress-36` names different ground after an advance, and the old uid STILL
// RESOLVES -- to a real person at a real fortress, the wrong one, with no error.
//
// `-node-` is written into the key rather than left implicit because `fortress-13` and
// `fortress-node-13` would otherwise read alike and mean different things. The kind is
// part of the key because a hamlet and a fortress can share a node: 0 of 56 shared on the
// seed-42 size-33 sample, but 18 shared at size 65, so the prefix is load-bearing.
//
// The anchor is spent in FOUR places for one person -- uid, presence.uid, claim.target_uid
// and the deed's event_id. Repairing one leaves three ways to join the wrong row, and
// three of them are the ones a story layer, a quest and a save file reach for.
std::string hero_small_site_anchor(std::string_view kind, int node, std::string_view ordinal_id);

// Surface metres along the great circle between two placed records.
double hero_separation_m(const Vec3& a, const Vec3& b, double radius_m);

// Cuts a ` City` the live generator never writes.
//
// `heritage::settlement_name` replaced the round-robin that appended it, so on any
// generated world this is the identity. It still fires on the synthetic worlds a test
// builds, so removing it is a fixture change and not a one-line change. NOTE it removes
// EVERY occurrence, not merely the first -- the port specification says the opposite twice
// and the reference's `str.replace` has no count argument.
std::string hero_short_name(std::string_view name);

// The city or ruin a ley node of `school` is named by, and the ground a hook on it sends a
// player to (owner, 2026-09-26: "Quests at the place"):
// - the place it is the school's nearest node to, the one nearest the node when several are
//   (rank 1);
// - where it is no place's nearest, the place it comes soonest from, counted among the
//   school's nodes (rank 2 is the second).
// Ties between nodes fall to the lower id, as the ley hooks break them. Words two places share
// point to neither, so those places are passed over. Nullopt when `school` has no node
// `node_id`, no place is left to name it by, or it comes later than tenth.
struct HeroLeyPlace {
    std::string words;  // "Wensal", "the ruins of Korpat"
    int rank = 1;
    bool ruin = false;
    std::string uid;  // the city's or the ruin's
    int node = -1;
    int x = 0, z = 0;
};
std::optional<HeroLeyPlace> hero_ley_node_place(const HeroesRequest& request, const std::string& node_id,
                                                const std::string& school);

// A ley node of `school` as a person names it, never by its id (2026-09-25), by its
// hero_ley_node_place: "the water node nearest Wensal", or "the second water node from the
// ruins of Korpat". Either is true by construction and fits no other node of the school.
// Empty where the place is nullopt. A key point is named by its ruin before this is asked.
std::string hero_ley_node_name(const HeroesRequest& request, const std::string& node_id, const std::string& school);

// The nine-cell label and two-character code for a pair of axis values.
HeroAlignment hero_alignment_record(double law, double good);

// Seeded weighted choice in CATALOGUE ORDER; null when no card's precondition holds.
//
// The walk is SUBTRACTIVE and the boundary is `selector <= 0`, not `selector < running`.
// `random()` is [0, 1) so the selector can be exactly 0.0, which selects the FIRST
// eligible card; the trailing fallback to the last option is a float-accumulation guard
// and not a branch that can be dropped.
const HeroArchetypeCard* choose_archetype(const std::set<std::string>& features,
                                          const HeroAlignment& alignment, Rng& draw);

// An alignment-neutral card coloured by the two axis overlays.
HeroPersona compose_persona(const HeroArchetypeCard& card, const HeroAlignment& alignment);

// difficulty = clamp(1, 5, floor(opposition + pressure + 0.5))
//
// The banding is floor(x + 0.5) and NOT a round-half-to-even, "because a rubric that sends
// 2.5 down and 3.5 up reads as arbitrary to anyone checking it by hand". Opposition is what
// stands at the target and is the only term that can reach 5; pressure is the danger
// recorded around it, capped, and NEVER applied to a nest, whose tier is already the whole
// answer and whose own range_m would otherwise count twice.
int hero_hook_difficulty(const HeroEffect& effect, double opposition_intensity,
                         bool target_has_grid, int target_x, int target_z,
                         const std::vector<HeroNest>& nests, double spacing_m);

// The whole pass. Throws on a malformed request or an unsatisfiable catalogue; see
// `attach_heroes` for the never-raising boundary the pipeline uses.
HeroesBlock resolve_heroes(const HeroesRequest& request);

// The isolation contract: a failure is a reported block, never a crashed world.
//
// This is the whole point of the design and it must survive the port. A revision that
// raises yields `{version, status: "failed", error}` and the world still returns.
HeroesBlock attach_heroes(const HeroesRequest& request);

}  // namespace fwg
