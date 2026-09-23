// What a people IS: its key traits, its culture, its tongue and its body.
//
// Heritage is a leaf. It reads no world layer, no config, no terrain and no seed, and
// it owns no identifiers of its own — every key in every table here is either a
// civilization id or a parent race id, and the binding is asserted by test rather than
// by an include. It is a pure function of eight authored documents plus two strings.
//
// Four layers, in the order they are built:
//   1. seventeen categorical KEY TRAITS, authored per parent race and extended by a
//      sparse per-civilization delta;
//   2. a CULTURE of 23 ethnographic fields, derived as a pure lookup on the traits;
//   3. a language GENOME — phoneme inventory, phonotactics, prosody, morphology,
//      orthography, three rates and a branch lexicon of morphemes;
//   4. an authored APPEARANCE, the one layer in the package that carries numbers.
// Each derived layer may then be patched by a sparse override.
//
// On top sits NAMING: morphemic glossable settlement names, dithematic personal names,
// a phonotactic fallback, realm names, and a per-people NAME STOCK that says which of
// the buildable names actually get used and how often.
//
// Two asymmetries are published contracts and are not accidents:
//
//   * An unknown `civ_id` resolves to its parent race's base, so a people heritage has
//     never heard of still arrives with a complete culture and tongue. An unknown or
//     empty `parent_race_id` THROWS: it selects the language FAMILY, so guessing one
//     yields a name in the wrong language rather than an approximate one. A caller that
//     cannot supply a parent race must leave the name empty and say so.
//
//   * `genome.inventory` is what the DERIVATION produced, which is not always what the
//     tongue ends up containing. The tidekin inventory keeps `th` while the tidekin
//     sound changes rewrite every `th` to `s`, and `hill_dwarf` lists uvulars its rules
//     shift away. The shifts run after coinage, so this is correct and it is also
//     misleading: read `inventory` as "what a syllable may be built from", never as
//     "what this tongue contains".
//
// Everything here lives in its own namespace because the words it needs are the plain
// ones — Traits, Culture, Genome, Value, resolve — and `fwg` already spends most of
// them on terrain.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fwg/random.hpp"

namespace fwg::heritage {

// ---------------------------------------------------------------------------
// Cardinalities. Changing one of these changes the authored data with it.
// ---------------------------------------------------------------------------

inline constexpr std::size_t kAxisCount = 17;
// `craft_focus` holds exactly two distinct crafts, and the derivation indexes them
// POSITIONALLY: 0 is a people's primary livelihood and 1 its secondary. Swapping the
// two entries of a people's craft_focus swaps its trades.
inline constexpr std::size_t kCraftCount = 2;
// Twenty-three, not the twenty-four the port specification states in four places:
// `contact` carries three keys and the other five carry four.
inline constexpr std::size_t kCultureKeyCount = 23;

// Bounded redraw against the forbidden-form filter, for both settlement and personal
// names. Raise it and a pathological lexicon spins longer; lower it and banned forms
// leak. Reaching the limit is an authoring signal, not a runtime failure — both callers
// return the last rejected candidate rather than throwing.
inline constexpr int kRedrawLimit = 8;

// ---------------------------------------------------------------------------
// Authored vocabularies. Generated from data/heritage-axes.json.
// ---------------------------------------------------------------------------

struct Axis {
    std::string_view name;
    // The closed enum. A derivation map is emitted as an array POSITIONALLY indexed by
    // this span, which is what makes "every enum value must map" structural rather
    // than a runtime lint.
    std::span<const std::string_view> values;
    bool list_valued;  // true only for craft_focus
};

// The seventeen axes, in declared order. An axis's POSITION is how traits are stored.
std::span<const Axis> axes();
int axis_index(std::string_view name);  // -1 when unknown

std::span<const std::string_view> crafts();
// The eight schools generation can actually raise.
std::span<const std::string_view> known_schools();
// The four Outside schools, deliberately absent from the trait enum: nobody holds a
// cultural stance toward a school no world can raise. Naming one in traits throws with
// a message that says so rather than being quietly ignored.
std::span<const std::string_view> hidden_schools();

// Height envelope in metres a body_scale value admits, as (floor, ceiling). These are
// physical constraints rather than authoring taste, which is why they live beside the
// phoneme gates instead of inside the table they police. The `compact` and `small`
// ceilings together are what make "no dwarf stands over 1.3462 m" a property of the
// loader rather than a promise in a comment.
std::pair<double, double> body_scale_height_m(std::string_view body_scale);
// (low, high) years `life_stages.max_years` must lie inside for a lifespan_tempo.
std::pair<double, double> lifespan_tempo_years(std::string_view lifespan_tempo);

struct CultureKey {
    std::string_view block;
    std::string_view key;
};
// The 23 culture fields in derivation-document order. Position is how a culture is
// stored and is what an override names.
std::span<const CultureKey> culture_keys();

// ---------------------------------------------------------------------------
// The phoneme alphabet. Generated from data/heritage-phonology.json.
// ---------------------------------------------------------------------------

struct Alphabet {
    // ORDER IS A CONTRACT, and it is the least obvious one in the package. The gates
    // below build an unordered set; `inventory()` re-emits it by filtering THESE
    // tuples, so the published list follows the alphabet rather than the order the
    // gates fired. That list is then indexed positionally by every name draw, sliced
    // positionally by `name_table`, and hashed into the heritage identity. Reordering
    // either tuple changes every name in every world.
    std::span<const std::string_view> consonants;
    std::span<const std::string_view> vowels;
};
Alphabet alphabet();

// Any alphabet token longer than one letter. `apply_sound_changes` consumes one of
// these whole, which is what stops a rule naming `s` firing inside `sh`.
bool is_digraph(std::string_view token);

struct Revisions {
    int traits = 0;
    int derivation = 0;
    int lexicon = 0;
    int overrides = 0;
    int appearance = 0;
};
Revisions revisions();

// ---------------------------------------------------------------------------
// Values. Only `appearance` needs an open value tree; everything else is typed.
// ---------------------------------------------------------------------------

// A number and the exact text the canonical serialiser must emit for it.
//
// The identity digest is taken over a byte-for-byte JSON rendering of the resolved
// peoples, so a number's SPELLING is authored data in the same way its value is:
// `350` and `350.0` are the same quantity and a different digest. Carrying the
// spelling beside the value is how that survives a port, rather than hoping two
// shortest-round-trip printers agree on where a trailing `.0` goes.
struct Number {
    double value = 0.0;
    std::string_view canon;
};

struct Value;
using Field = std::pair<std::string_view, Value>;

struct Value {
    // Integer and Number are both JSON numbers and are kept apart because the digest
    // is over text: an authored `350` and an authored `350.0` are the same quantity
    // and different bytes. Integer is every whole number, Number every fractional one
    // and anything computed that has to keep an authored spelling.
    enum class Kind { Text, Number, Integer, Boolean, Array, Object };

    Kind kind = Kind::Text;
    std::string_view text;
    heritage::Number number;
    long long integer = 0;
    bool boolean = false;
    std::vector<Value> items;   // Array
    std::vector<Field> fields;  // Object, in authored insertion order

    const Value* find(std::string_view key) const;
    // Throws when the key is absent. For the handful of places a caller has already
    // proved the shape.
    const Value& at(std::string_view key) const;
    // The numeric value of an Integer or a Number; throws for anything else.
    double as_double() const;

    friend bool operator==(const Value& a, const Value& b);
};

// ---------------------------------------------------------------------------
// A resolved people.
// ---------------------------------------------------------------------------

struct Traits {
    // Indexed by axis position. The craft_focus slot stays empty — that axis has two
    // values and lives below.
    std::array<std::string_view, kAxisCount> values{};
    std::array<std::string_view, kCraftCount> craft_focus{};

    std::string_view axis(std::string_view name) const;
};

struct Culture {
    // Indexed by position in culture_keys().
    std::array<std::string_view, kCultureKeyCount> values{};

    std::string_view at(std::string_view block, std::string_view key) const;
};

struct SoundChange {
    std::string_view old_form;
    std::string_view new_form;
};

// An authored morpheme, exactly as the lexicon spells it.
struct Morpheme {
    std::string_view slot;
    std::string_view root;
};

// A morpheme in a resolved genome. The root is a std::string because the branch's sound
// changes have already rewritten it — `khas` is dwarven and `qos` is frostholder.
struct Root {
    std::string_view slot;
    std::string value;
};

struct Inventory {
    std::vector<std::string_view> consonants;
    std::vector<std::string_view> vowels;
    std::vector<std::string_view> forbidden;
    // The consonants minus `ng`. `ng` is a fine coda and a poor word onset in most
    // tongues; without this, dwarven produced names like `Ngukhuls`.
    std::vector<std::string_view> onsets;
};

struct Phonotactics {
    std::string_view syllable;      // e.g. "(C)(C)V(C)"
    std::string_view vowel_length;  // none | contrastive
    std::string_view hiatus;        // forbidden | allowed | preferred
    bool geminates = false;
    int max_onset_cluster = 0;
    int max_coda_cluster = 0;
    std::vector<std::string> onset_clusters;
    std::vector<std::string> coda_clusters;
    std::vector<std::string> allowed_clusters;
};

struct Prosody {
    std::string_view stress;      // penultimate | initial
    std::string_view tone;        // none | register
    std::string_view word_shape;  // monosyllabic_heavy | disyllabic | trisyllabic_flowing
};

struct Morphology {
    std::string_view compounding;
    std::string_view honorific;
    bool call_form = false;
    std::string_view word_order;  // from the family
    std::string_view affixing;    // from the family
    // The one place the genome reads the culture: mapped from
    // culture.household.naming_pattern. Everything else here reads the traits.
    std::string_view patronymic;
};

struct Orthography {
    std::string_view script_form;
    std::string_view direction;
    // The multi-letter consonants the inventory actually holds, in alphabet order.
    std::vector<std::string_view> digraphs;
};

struct Rates {
    Number third_syllable;
    Number cluster;
    // Authored, validated, published and read by NOTHING. Presumably reserved for a
    // borrowing mechanic that was never built. Carried because it is part of the
    // published genome shape; do not invent a consumer for it.
    Number loanword;
};

struct Genome {
    std::string_view family;  // human | elven | dwarven
    // The civilization id, ALWAYS — even for a people the family has no branch for,
    // which then speaks the family roots unshifted.
    std::string branch;
    Inventory inventory;
    // The branch's own rules, carried so coined words undergo the same shifts the
    // inherited roots did. Without it two siblings with the same soma generate
    // identical personal names, which is exactly what human_large_island and
    // human_rainforest did.
    std::vector<SoundChange> sound_changes;
    std::vector<std::string_view> forbidden_forms;
    // In family-root order, then the branch's own coinages.
    std::vector<Root> lexicon;
    Phonotactics phonotactics;
    Prosody prosody;
    Morphology morphology;
    Orthography orthography;
    Rates rates;

    const std::string* root(std::string_view slot) const;
    bool has_root(std::string_view slot) const { return root(slot) != nullptr; }
};

struct Provenance {
    heritage::Revisions revisions;
    // Dotted paths, culture first then genome, in OVERRIDE-DOCUMENT order rather than
    // sorted. The reference does not sort it and the identity digest is taken over the
    // list as it stands, so sorting here would move the digest for no gain.
    std::vector<std::string> overridden;
};

struct Resolved {
    std::string id;  // THE KEY FIELD IS `id` — not uid, site_id or kind.
    std::string parent_race_id;
    heritage::Traits traits;
    Value appearance;  // 8 blocks; the only layer carrying numbers
    heritage::Culture culture;
    heritage::Genome genome;
    heritage::Provenance provenance;
};

// ---------------------------------------------------------------------------
// The shared lexicon document.
// ---------------------------------------------------------------------------

struct NameTemplate {
    std::array<std::string_view, 2> pattern;  // two roles
    int weight = 0;
};

struct SlotRole {
    std::string_view role;
    std::span<const std::string_view> slots;
};

struct BranchSpec {
    std::string_view id;
    std::span<const SoundChange> sound_changes;  // ORDERED; rules feed each other
    // Applied AFTER the changes, because a word the branch invented was not present
    // when the shift ran — which is why gnomish keeps `spril` intact. An entry naming a
    // slot the family already holds REPLACES the shifted root rather than being
    // skipped: `human_desert` has `well: kanat`, not the shifted `fat`.
    std::span<const Morpheme> add_roots;
    std::span<const std::string_view> drop_roots;
};

struct LexiconFamily {
    std::string_view name;
    std::string_view parent_race;
    std::string_view word_order;
    std::string_view affixing;
    std::span<const Morpheme> roots;  // INSERTION ORDER; branch_lexicon walks it
    std::span<const BranchSpec> branches;
};

struct Lexicon {
    std::span<const std::string_view> shared_slots;
    std::span<const LexiconFamily> families;
    std::span<const std::string_view> forbidden_forms;
    // ORDER IS A CONTRACT for both template lists: they are drawn by a cumulative
    // weighted walk in document order, so reordering one re-maps the draw intervals
    // and renames every settlement in every world at the same seed.
    std::span<const NameTemplate> personal_templates;
    std::span<const NameTemplate> settlement_templates;
    std::span<const SlotRole> slot_roles;

    const SlotRole* role(std::string_view name) const;
};

// The shared root tables and name templates. Loaded once; the same document for every
// people, so it is fetched separately rather than copied into each resolution.
const Lexicon& lexicon();

// ---------------------------------------------------------------------------
// Resolution.
// ---------------------------------------------------------------------------

// (consonants, vowels, forbidden) for a trait record, each in ALPHABET order.
//
// This is code rather than data because it is a physical constraint and not a taste: a
// people with blunt dentition cannot make an interdental fricative, and no amount of
// authoring should let one. Additions accumulate and REMOVALS WIN — the forbidden set
// is a union, so one impossibility is not argued away by another axis that would have
// permitted it.
struct PhonemeInventory {
    std::vector<std::string_view> consonants;
    std::vector<std::string_view> vowels;
    std::vector<std::string_view> forbidden;
};
PhonemeInventory phoneme_inventory(const Traits& traits);

// Rewrite a word, treating multi-letter phonemes as atomic and running the rules in
// order. A naive find-and-replace reads the `s` inside `sh` as an `s`, so the rainforest
// rule s->sh turned `mush` into `mushh`.
std::string apply_sound_changes(std::string_view word, std::span<const SoundChange> changes);

// One resolved people. `civ_id` may be unknown; `parent_race_id` may not.
Resolved resolve(std::string_view civ_id, std::string_view parent_race_id);

struct Identity {
    heritage::Revisions revisions;
    std::string sha256;
};

// Hash the RESOLVED peoples, not the source bytes: a derivation edit that changes no
// output then invalidates nothing, while a one-character override that does change an
// output is caught. `pairs` is (civilization_id, parent_race_id) and is sorted here, so
// this package stays a leaf and never looks a parent race up.
//
// Nothing gates on this digest — only the registry identity gates age advance — so it
// reports a change without rejecting anything.
Identity heritage_identity(std::span<const std::pair<std::string, std::string>> pairs);

// The exact bytes `heritage_identity` digests. Exposed because a mismatch is otherwise
// a 64-character hex string with no way to see what moved.
std::string canonical_document(std::span<const std::pair<std::string, std::string>> pairs);

// ---------------------------------------------------------------------------
// Naming. Every entry point takes the caller's own Rng; heritage carries no seed logic
// at all, which keeps every seeding decision with the subsystem that owns it.
// ---------------------------------------------------------------------------

// (name, gloss). `terrain_slot` lets a name echo the ground it stands on — it is honoured
// only at pattern position 0 and only for the `terrain` role. NOTHING IN THE PIPELINE
// PASSES IT today; it is live, tested and unused, exactly as `rates.loanword` is.
std::pair<std::string, std::string> settlement_name(const Resolved& resolved,
                                                    const Lexicon& lex, Rng& draw,
                                                    std::string_view terrain_slot = {});

// Dithematic: two meaning-bearing elements, as Thor-bjorn and Ead-weard are. Falls back
// to the phonotactic coinage when the branch's roots cannot fill the drawn template.
std::string person_name(const Resolved& resolved, const Lexicon& lex, Rng& draw);
// Phonotactic only, for a caller that has a genome but no lexicon to hand. This is a
// different name distribution, not a degraded one.
std::string person_name(const Resolved& resolved, Rng& draw);

// As `person_name`, plus the English reading of its two elements.
//
// A RECORDED DEFECT, ported as it stands: this path draws ONCE and does NOT run the
// forbidden-form filter, while `person_name` redraws up to eight times against it. For
// `human_large_island` the roots `wood`=har and `salt`=ser compose to a name spelling a
// word nobody meant to ship, and it reaches a player through here at roughly one draw
// in a hundred and fifty. A caller that shows these to a player should filter against
// `genome.forbidden_forms` itself until this is fixed here.
std::pair<std::string, std::string> person_name_with_gloss(const Resolved& resolved,
                                                           const Lexicon& lex, Rng& draw);

// The capital's name in its own tongue plus the tongue's `realm` morpheme. Empty when
// the branch has no realm root or the capital name is empty — which lets a caller keep
// its English form rather than invent one.
std::string realm_name(const Resolved& resolved, std::string_view capital_name);

struct StockEntry {
    std::string name;
    std::string gloss;  // "slot-slot"
};

struct NameStock {
    std::vector<StockEntry> common;
    std::vector<StockEntry> rare;
    // Cumulative, over `common`. The last entry is 1.0 to within float error.
    std::vector<double> thresholds;
};

// Every name this people uses, ranked, with the thresholds that say how often.
//
// Pure and seedless, so one people yields one stock in every world: rank has to be a
// property of the culture rather than of a seed, the way John was common in every
// English county. Enumerating the whole reachable space is cheap but not free — BUILD
// IT ONCE PER (civilization, parent race) and hand it to `stock_name` per person. A
// size-65 world carries around thirteen thousand people.
NameStock name_stock(const Resolved& resolved, const Lexicon& lex);

// One name under the people's own concentration, or nothing when the common stock is
// empty. SPENDS EXACTLY TWO random() CALLS ON EVERY PATH — a branch that spent a
// different number would desynchronise a replay from itself the moment the rare tail
// was added.
std::optional<StockEntry> stock_name(const NameStock& stock, Rng& draw);

// The syllable tables the retired `hero_generator.naming.given_name` consumed.
//
// A compatibility bridge with no consumer left anywhere: the function it adapted to has
// been deleted. Carried rather than dropped so the shape stays recorded — note that the
// two empty codas a `preferred`-hiatus tongue appends collapse to ONE under the
// deduplicating sort, so an open syllable is about 4.5% likely and not twice as likely.
struct NameTable {
    std::vector<std::string> onsets;
    std::vector<std::string_view> nuclei;
    std::vector<std::string> codas;
    double third_syllable_chance = 0.0;
};
NameTable name_table(const Resolved& resolved);

// ---------------------------------------------------------------------------
// The authored tables themselves, generated from data/heritage-*.json into
// src/generated/content_heritage.cpp by tools/embed/heritage.py.
//
// These are the loader's inputs rather than the package's answer, so they sit behind
// their own namespace. They are published anyway because heritage IS its data: a
// catalogue exporter or a content editor wants the rules, not only the results.
//
// Several of the reference's five linters have moved into the generator and do not
// appear here at all — totality of a derivation map, the shape of an appearance key,
// palette weights, prose length, root spellability. A total map is emitted as an array
// indexed by the axis enum, so an incomplete one cannot be represented; the rest are
// checked in Python over fixed data and re-asserted from the resolved bodies by test.
// What stayed in C++ is everything that can fire on a call: an unknown parent race, a
// delta that restates what it inherits, an override that restates a derived value, and
// the cross-layer appearance envelopes.
// ---------------------------------------------------------------------------

namespace tables {

// One authored axis value. `crafts` is filled only for the list-valued axis.
struct AxisValue {
    int axis = -1;
    std::string_view value;
    std::array<std::string_view, kCraftCount> crafts{};
};

struct TraitRow {
    std::string_view id;
    std::span<const AxisValue> axes;
};

// Complete: every race base names all seventeen axes.
std::span<const TraitRow> trait_races();
// Sparse deltas. A civilization absent from here is a plain member of its race — that
// is the property that lets a new people arrive complete.
std::span<const TraitRow> trait_peoples();

// A derivation rule. The three shapes are exclusive here; the reference tested `copy`
// first and would have accepted a rule carrying both a copy and a map, silently
// ignoring the map. The generator refuses that instead.
struct Rule {
    enum class Kind { Copy, Craft, Map };
    Kind kind = Kind::Copy;
    int axis = -1;   // Copy and Map
    int craft = -1;  // Craft
    // Map: one entry per value of `axis`, POSITIONALLY. Totality is structural.
    std::span<const std::string_view> map;
};

// Parallel to culture_keys(), 23 entries.
std::span<const Rule> culture_rules();

// Every genome rule in the shipped derivation is a from/map rule, and each maps to a
// typed field, so they are emitted as typed positional tables rather than as a generic
// rule list. The generator refuses a genome rule of any other shape — it would have no
// typed home.
struct TextRule {
    int axis = -1;
    std::span<const std::string_view> map;
};
struct BoolRule {
    int axis = -1;
    std::span<const bool> map;
};
struct NumberRule {
    int axis = -1;
    std::span<const Number> map;
};

struct GenomeDerivation {
    TextRule syllable, vowel_length, hiatus;
    BoolRule geminates;
    TextRule stress, tone, word_shape;
    TextRule compounding, honorific;
    BoolRule call_form;
    TextRule script_form, direction;
    NumberRule third_syllable, cluster, loanword;
};
const GenomeDerivation& genome_derivation();

// What one soma axis value opens and shuts. A value with no rule contributes nothing,
// and the five gated axes commute — adds union, drops union, and the subtraction runs
// once at the end. The declared order is kept anyway so an error names the same axis
// twice running.
struct Gate {
    int axis = -1;
    std::string_view value;
    std::span<const std::string_view> add;
    std::span<const std::string_view> drop;
};
std::span<const Gate> gates();
std::span<const std::string_view> core_consonants();
std::span<const std::string_view> core_vowels();

// Nasal plus stop only where the two share a place of articulation. Pairing any nasal
// with any stop produced codas like `nb` and names like `Pimd` and `Wongd`; `ng` takes
// only the voiceless member because ng + g renders as the unreadable `ngg`.
struct Homorganic {
    std::string_view nasal;
    std::span<const std::string_view> stops;
};

struct ClusterTables {
    std::span<const std::string_view> onset_stops;
    std::span<const std::string_view> voiceless_stops;
    std::span<const std::string_view> liquids;
    std::span<const std::string_view> nasals;
    // `tl` and `dl` and their `lh` forms: the stop-liquid pairs most tongues refuse.
    std::span<const std::array<std::string_view, 2>> refused_stop_liquid_onsets;
    std::span<const Homorganic> homorganic_nasal_stop_codas;
};
const ClusterTables& cluster_tables();

std::span<const std::string_view> word_onset_excludes();

struct PatronymicRow {
    std::string_view naming_pattern;
    std::string_view patronymic;
};
std::span<const PatronymicRow> patronymic_table();

// Every number the name generator draws against. A port that misses one of these still
// produces names — just a different world's names — which is what makes them the most
// silently lossy thing in the package.
struct NameDraw {
    // The five commonest names' population shares, consumed POSITIONALLY as ranks 1..5.
    // Tabulated rather than fitted: the measured curve is steeper at the head than any
    // single Zipf exponent reproduces.
    std::span<const double> head_shares;
    double zipf_exponent = 1.0;
    double rare_share = 0.0;
    int head_spread = 0;
    int redraw_limit = 0;
    double nucleus_share_base = 0.0;
    double nucleus_share_contrastive = 0.0;
    double nucleus_share_hiatus = 0.0;
    // Note the SENSE: a syllable RETURNS EARLY when the roll is at or above this, so an
    // optional coda appears 55% of the time. Getting it backwards flips every optional
    // coda in the world.
    double optional_coda_skip = 0.0;
    int monosyllabic_heavy = 0;
    int disyllabic = 0;
    // 2, not 3. The third syllable arrives through rates.third_syllable, which is 0.7
    // for the sustained breath that is the only trait producing this shape.
    int trisyllabic_flowing = 0;
    std::string_view vowel_letters;
    int name_table_consonants = 0;
    int name_table_vowels = 0;
};
const NameDraw& name_draw();

struct CulturePatch {
    int key = -1;  // index into culture_keys()
    std::string_view value;
};
struct CultureOverrideRow {
    std::string_view civ_id;
    std::span<const CulturePatch> patches;  // in override-document order
};
std::span<const CultureOverrideRow> culture_overrides();

struct GenomePatch {
    enum class Kind { Text, Boolean, Number, Inventory };
    Kind kind = Kind::Text;
    std::string_view block;
    std::string_view key;
    std::string_view text;
    bool boolean = false;
    heritage::Number number;
    // Inventory only. Every token is load-bearing: adding one already present, dropping
    // one the derivation never produced, naming one outside the alphabet, or naming one
    // in both lists is an error rather than a no-op.
    std::span<const std::string_view> add;
    std::span<const std::string_view> drop;
};
struct GenomeOverrideRow {
    std::string_view civ_id;
    std::span<const GenomePatch> patches;
};
std::span<const GenomeOverrideRow> genome_overrides();

// The appearance bodies, flattened into one node arena. A body is a tree and the
// library's other generated tables are flat, so it is stored the way the habitat
// predicates are: nodes, plus a shared pool of child INDICES and the key each child
// hangs under.
struct ValueNode {
    Value::Kind kind = Value::Kind::Text;
    std::string_view text;
    double number = 0.0;
    std::string_view canon;
    long long integer = 0;
    bool boolean = false;
    std::uint32_t child_begin = 0;
    std::uint32_t child_end = 0;
};
std::span<const ValueNode> appearance_nodes();
std::span<const std::uint32_t> appearance_children();
// Parallel to appearance_children(); empty for an array element.
std::span<const std::string_view> appearance_child_keys();

struct BodyRow {
    std::string_view id;
    std::uint32_t root = 0;
};
// Complete bodies, one per parent race.
std::span<const BodyRow> appearance_races();
// Sparse deltas at depth two. EVERY civilization has an entry, even an empty one —
// unlike traits, an absent people is an authoring error, because a subrace silently
// wearing its archetype's body is a wrong answer nobody notices until the art comes
// back looking like its parent.
std::span<const BodyRow> appearance_peoples();

// (male, female), for the height envelope check.
std::span<const std::string_view> sexes();
// ORDER IS A CONTRACT: the resolved check walks it to require strictly increasing
// onsets and strictly increasing height over its first three entries.
std::span<const std::string_view> life_stage_order();

}  // namespace tables

}  // namespace fwg::heritage
