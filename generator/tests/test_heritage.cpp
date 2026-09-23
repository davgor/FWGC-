// Heritage invariants.
//
// Heritage is table-driven, so most of what can go wrong is an ORDERING or a MERGE
// rather than an arithmetic slip: a reordered alphabet renames every settlement in
// every world, a template list walked in the wrong order re-maps the draw intervals,
// and a delta merged onto the wrong base produces a body its own key trait forbids.
// These tests are aimed at those, plus the two published digests — the identity sha256
// and the twelve magic schools that seed ruin leylines.
//
// The registry bindings are asserted HERE rather than inside heritage, because heritage
// is a leaf: it never imports the civilization registry, and the price of that
// independence is a test that locks the two together in both directions.
#include "check.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "fwg/civilizations.hpp"
#include "fwg/entities.hpp"
#include "fwg/founding.hpp"
#include "fwg/heritage.hpp"
#include "fwg/random.hpp"

using namespace fwg;
using namespace fwg::heritage;

namespace {

// The twelve (civilization, parent race) pairs the registry ships. Heritage is handed
// these rather than looking them up, which is the whole of what makes it a leaf.
std::vector<std::pair<std::string, std::string>> registry_pairs() {
    std::vector<std::pair<std::string, std::string>> pairs;
    for (std::string_view id : civilization_ids()) {
        const EntityRules* rules = entity_rule(id);
        check::is_true(rules != nullptr, "an entity rule for civilization " + std::string(id));
        pairs.emplace_back(id, std::string(rules->parent_race_id));
    }
    return pairs;
}

std::vector<Resolved> every_people() {
    std::vector<Resolved> out;
    for (const auto& [civ_id, parent] : registry_pairs()) out.push_back(resolve(civ_id, parent));
    return out;
}

std::set<std::string> keys_of(std::span<const std::string_view> items) {
    std::set<std::string> out;
    for (std::string_view item : items) out.emplace(item);
    return out;
}

std::string joined(const std::set<std::string>& items) {
    std::string out;
    for (const std::string& item : items) {
        if (!out.empty()) out += ", ";
        out += item;
    }
    return out.empty() ? "<none>" : out;
}

void same_set(const std::set<std::string>& actual, const std::set<std::string>& expected,
              const std::string& what) {
    std::set<std::string> missing;
    std::set<std::string> extra;
    std::set_difference(expected.begin(), expected.end(), actual.begin(), actual.end(),
                        std::inserter(missing, missing.end()));
    std::set_difference(actual.begin(), actual.end(), expected.begin(), expected.end(),
                        std::inserter(extra, extra.end()));
    check::is_true(missing.empty() && extra.empty(),
                   what + " to match exactly; missing " + joined(missing) + ", extra " +
                       joined(extra));
}

std::vector<std::string> split_gloss(const std::string& gloss) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : gloss) {
        if (c == '-') {
            parts.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    parts.push_back(current);
    return parts;
}

bool is_subsequence(const std::vector<std::string_view>& part,
                    std::span<const std::string_view> whole) {
    std::size_t at = 0;
    for (std::string_view token : part) {
        while (at < whole.size() && whole[at] != token) ++at;
        if (at == whole.size()) return false;
        ++at;
    }
    return true;
}

const Resolved& people(std::string_view id) {
    static std::map<std::string, Resolved> cache;
    const auto found = cache.find(std::string(id));
    if (found != cache.end()) return found->second;
    for (const auto& [civ_id, parent] : registry_pairs()) {
        if (civ_id == id) return cache.emplace(civ_id, resolve(civ_id, parent)).first->second;
    }
    check::fail("no registry pairing for " + std::string(id));
}

}  // namespace

// --- registry bindings ----------------------------------------------------

TEST(every_civilization_has_key_traits_a_body_and_a_language_branch) {
    // Five bindings in both directions. Heritage cannot see the registry, so if this
    // test goes, nothing stops a new people arriving with no tongue and no face.
    const std::set<std::string> registry = keys_of(civilization_ids());
    check::is_true(registry.size() >= 12, "the registry to carry at least the shipped twelve");

    std::set<std::string> with_traits;
    for (const tables::TraitRow& row : tables::trait_peoples()) with_traits.emplace(row.id);
    same_set(with_traits, registry, "the peoples that carry a trait delta");

    std::set<std::string> with_bodies;
    for (const tables::BodyRow& row : tables::appearance_peoples()) with_bodies.emplace(row.id);
    same_set(with_bodies, registry, "the peoples that carry an authored body");

    std::set<std::string> races;
    for (const ParentRace& race : parent_races()) races.emplace(race.id);
    std::set<std::string> trait_races;
    for (const tables::TraitRow& row : tables::trait_races()) trait_races.emplace(row.id);
    std::set<std::string> body_races;
    for (const tables::BodyRow& row : tables::appearance_races()) body_races.emplace(row.id);
    same_set(trait_races, races, "the races that carry a trait base");
    same_set(body_races, races, "the races that carry a body base");

    std::set<std::string> branches;
    std::set<std::string> claimed;
    for (const LexiconFamily& family : lexicon().families) {
        claimed.emplace(family.parent_race);
        for (const BranchSpec& branch : family.branches) {
            check::is_true(branches.emplace(branch.id).second,
                           "branch " + std::string(branch.id) + " to be declared once");
        }
    }
    same_set(claimed, races, "the parent races a language family claims");
    same_set(branches, registry, "the civilizations that own a language branch");
}

TEST(the_magic_school_of_each_people_is_held_verbatim) {
    // These twelve decide which leyline school a fallen city of that culture seeds, so
    // a change here is a change to the world. Held verbatim rather than derived,
    // because a derived mapping compared against itself proves nothing.
    const std::map<std::string, std::string> expected{
        {"human_maritime", "water"},  {"human_desert", "fire"},   {"human_cold", "water"},
        {"human_large_island", "air"}, {"human_rainforest", "earth"},
        {"human_heartland", "radiant"}, {"dwarf", "earth"},       {"elf", "weave"},
        {"gnome", "weave"},           {"tidekin", "water"},        {"hill_dwarf", "earth"},
        {"frosthold_dwarf", "water"},
    };
    int checked = 0;
    for (const Resolved& r : every_people()) {
        const auto found = expected.find(r.id);
        check::is_true(found != expected.end(), "a held school for " + r.id);
        check::equal(r.traits.axis("magic_school"), std::string_view(found->second),
                     r.id + " magic school");
        // The culture republishes it, and the two must not drift.
        check::equal(r.culture.at("faith", "school"), std::string_view(found->second),
                     r.id + " faith.school");
        ++checked;
    }
    check::equal(checked, 12, "twelve peoples checked");
}

TEST(a_hidden_school_is_absent_from_the_trait_enum) {
    // Nobody holds a cultural stance toward a school no world can raise.
    const std::set<std::string> known = keys_of(known_schools());
    check::equal(known.size(), std::size_t{8}, "eight schools generation can raise");
    int hidden = 0;
    for (std::string_view school : hidden_schools()) {
        check::is_true(known.count(std::string(school)) == 0,
                       std::string(school) + " to be absent from the trait enum");
        ++hidden;
    }
    check::equal(hidden, 4, "four Outside schools held apart");
}

// --- phonemes -------------------------------------------------------------

TEST(the_dwarf_inventory_is_exactly_what_its_soma_admits) {
    const PhonemeInventory dwarf = phoneme_inventory(people("dwarf").traits);
    const std::vector<std::string_view> consonants{"p", "b", "t",  "d",  "k", "g", "q", "m",
                                                   "n", "ng", "s", "kh", "gh", "h", "l", "r"};
    const std::vector<std::string_view> vowels{"a", "e", "i", "o", "u", "aa"};
    const std::vector<std::string_view> forbidden{"th", "dh", "z", "sh", "zh", "ts", "ch", "y"};
    check::is_true(dwarf.consonants == consonants, "the dwarf consonant inventory");
    check::is_true(dwarf.vowels == vowels, "the dwarf vowel inventory");
    check::is_true(dwarf.forbidden == forbidden, "the dwarf forbidden set");
}

TEST(a_removal_beats_an_addition_from_another_axis) {
    // `even` dentition adds th; `blunt` drops it. Give a people both gates' inputs in
    // turn and the drop must win whichever order the axes are declared in, because the
    // forbidden set is a union and the subtraction runs once at the end.
    Traits traits = people("dwarf").traits;  // blunt dentition, so th is banned
    const auto add_th = phoneme_inventory(people("elf").traits);  // even dentition
    check::is_true(std::find(add_th.consonants.begin(), add_th.consonants.end(), "th") !=
                       add_th.consonants.end(),
                   "an even-dentition people to keep th");

    // Now take the elf's traits and blunt its teeth: the addition must not survive.
    traits = people("elf").traits;
    traits.values[static_cast<std::size_t>(axis_index("dentition"))] = "blunt";
    const PhonemeInventory blunted = phoneme_inventory(traits);
    check::is_true(std::find(blunted.consonants.begin(), blunted.consonants.end(), "th") ==
                       blunted.consonants.end(),
                   "a blunted people to lose th even though another gate offered it");
    check::is_true(std::find(blunted.forbidden.begin(), blunted.forbidden.end(), "th") !=
                       blunted.forbidden.end(),
                   "th to be listed as forbidden rather than silently missing");
    check::is_true(blunted.consonants.size() >= 10,
                   "the blunted inventory to still be a usable tongue, not an empty set");
}

TEST(an_inventory_follows_the_alphabet_and_not_the_order_the_gates_fired) {
    // The least obvious contract in the package: the published list is a filter over
    // the alphabet tuples, and it is then indexed positionally by every name draw and
    // hashed into the identity.
    const Alphabet letters = alphabet();
    int checked = 0;
    for (const Resolved& r : every_people()) {
        check::is_true(is_subsequence(r.genome.inventory.consonants, letters.consonants),
                       r.id + " consonants to run in alphabet order");
        check::is_true(is_subsequence(r.genome.inventory.vowels, letters.vowels),
                       r.id + " vowels to run in alphabet order");
        check::is_true(is_subsequence(r.genome.orthography.digraphs, letters.consonants),
                       r.id + " digraphs to run in alphabet order");
        // `ng` is a fine coda and a poor word onset; without this, dwarven produced
        // names like `Ngukhuls`.
        check::is_true(std::find(r.genome.inventory.onsets.begin(),
                                 r.genome.inventory.onsets.end(),
                                 "ng") == r.genome.inventory.onsets.end(),
                       r.id + " to bar ng from word onset");
        check::is_true(!r.genome.inventory.consonants.empty() && !r.genome.inventory.vowels.empty(),
                       r.id + " to have a non-empty inventory");
        ++checked;
    }
    check::equal(checked, 12, "twelve inventories checked");
}

// --- sound changes --------------------------------------------------------

TEST(a_rule_naming_one_letter_does_not_fire_inside_a_digraph) {
    // A naive replace reads the `s` inside `sh` as an `s`, so the rainforest rule
    // s -> sh turned `mush` into `mushh`.
    const std::vector<SoundChange> to_sh{{"s", "sh"}};
    check::equal(apply_sound_changes("mush", to_sh), std::string("mush"), "mush stays mush");
    check::equal(apply_sound_changes("sal", to_sh), std::string("shal"), "a bare s still shifts");
    // The escape: a rule whose `old` IS the digraph still fires on it.
    const std::vector<SoundChange> shorten{{"aa", "a"}};
    check::equal(apply_sound_changes("khaas", shorten), std::string("khas"), "aa -> a fires on aa");
    // And a single-letter rule must not reach inside that same digraph.
    const std::vector<SoundChange> a_to_o{{"a", "o"}};
    check::equal(apply_sound_changes("khaas", a_to_o), std::string("khaas"),
                 "a -> o leaves aa alone");
}

TEST(sound_changes_run_in_order_and_feed_each_other) {
    const std::vector<SoundChange> forward{{"k", "h"}, {"h", "g"}};
    const std::vector<SoundChange> backward{{"h", "g"}, {"k", "h"}};
    check::equal(apply_sound_changes("kal", forward), std::string("gal"), "k->h->g");
    check::equal(apply_sound_changes("kal", backward), std::string("hal"), "h->g then k->h");
    check::equal(apply_sound_changes("kal", {}), std::string("kal"), "no rules, no change");
}

TEST(a_branch_lexicon_is_the_family_roots_under_the_branch_s_own_shifts) {
    check::equal(*people("dwarf").genome.root("stone"), std::string("khas"), "dwarven stone");
    check::equal(*people("frosthold_dwarf").genome.root("stone"), std::string("qos"),
                 "frostholder stone under kh->q, a->o");
    check::equal(*people("gnome").genome.root("stone"), std::string("kas"), "gnomish stone");
    check::equal(*people("hill_dwarf").genome.root("stone"), std::string("has"),
                 "hill dwarvish stone");
    // A coinage is added AFTER the shifts, so gnomish keeps `spril` intact rather than
    // shifting a word the branch invented after the shift ran.
    check::equal(*people("gnome").genome.root("gear"), std::string("spril"), "gnomish gear");
    // And a coinage naming an inherited slot REPLACES it: the human `well` is `pat`,
    // which p->f would give as `fat`, but the desert authors `kanat`.
    check::equal(*people("human_desert").genome.root("well"), std::string("kanat"),
                 "desert well is the coinage, not the shifted inheritance");
}

// --- derivation -----------------------------------------------------------

TEST(every_people_derives_a_complete_culture_and_genome) {
    int checked = 0;
    for (const Resolved& r : every_people()) {
        for (std::size_t i = 0; i < culture_keys().size(); ++i) {
            check::is_true(!r.culture.values[i].empty(),
                           r.id + " culture " + std::string(culture_keys()[i].block) + "." +
                               std::string(culture_keys()[i].key) + " to be derived");
        }
        check::is_true(!r.genome.family.empty(), r.id + " to belong to a family");
        check::equal(r.genome.branch, r.id, r.id + " branch is its own id");
        check::is_true(!r.genome.phonotactics.syllable.empty(), r.id + " syllable template");
        check::is_true(!r.genome.prosody.word_shape.empty(), r.id + " word shape");
        check::is_true(!r.genome.morphology.patronymic.empty(), r.id + " patronymic form");
        check::is_true(!r.genome.orthography.script_form.empty(), r.id + " script form");
        check::is_true(r.genome.lexicon.size() >= 29,
                       r.id + " to carry at least the nineteen shared roots and its own");
        check::equal(r.genome.forbidden_forms.size(), std::size_t{14},
                     r.id + " carries the global banned list");
        ++checked;
    }
    check::equal(checked, 12, "twelve genomes checked");
    check::equal(culture_keys().size(), std::size_t{23}, "twenty-three culture keys");
}

TEST(the_syllable_template_decides_the_cluster_bounds) {
    // `(C)(C)V(C)` is two onsets and one coda, `(C)V(C)(C)` the reverse, and N counts
    // as a coda. `surf`'s `(V)` tail is counted by neither, exactly as the reference
    // ignores it.
    const std::map<std::string, std::pair<int, int>> expected{
        {"human_heartland", {2, 1}}, {"human_maritime", {1, 1}}, {"human_desert", {1, 2}},
        {"human_cold", {1, 2}},      {"human_large_island", {1, 1}},
        {"human_rainforest", {1, 1}}, {"elf", {1, 1}},           {"tidekin", {1, 1}},
        {"dwarf", {1, 1}},           {"gnome", {2, 1}},          {"hill_dwarf", {2, 1}},
        {"frosthold_dwarf", {1, 2}},
    };
    for (const Resolved& r : every_people()) {
        const auto& want = expected.at(r.id);
        check::equal(r.genome.phonotactics.max_onset_cluster, want.first, r.id + " onset bound");
        check::equal(r.genome.phonotactics.max_coda_cluster, want.second, r.id + " coda bound");
        // Every shipped template closes, so a name can always take a final consonant.
        check::is_true(r.genome.phonotactics.max_coda_cluster > 0, r.id + " to close a syllable");
    }
}

TEST(clusters_are_built_from_what_the_inventory_actually_holds) {
    const std::vector<std::string> plain_onsets{"bl", "br", "dr", "gl", "gr", "kl", "kr",
                                                "pl", "pr", "sk", "sp", "st", "tr"};
    check::is_true(people("human_heartland").genome.phonotactics.onset_clusters == plain_onsets,
                   "the heartland onset set");
    // A cluster set is a function of the INVENTORY, not a constant per template: hill
    // dwarvish keeps `q`, so it reaches two more onsets under the same template.
    std::vector<std::string> hill = plain_onsets;
    hill.insert(hill.begin() + 9, "ql");
    hill.insert(hill.begin() + 10, "qr");
    check::is_true(people("hill_dwarf").genome.phonotactics.onset_clusters == hill,
                   "the hill dwarvish onset set, with its uvulars");
    // Thirteen codas, not twelve: the homorganic rule produces `nd` as well as `nt`.
    const std::vector<std::string> codas{"lk", "lp", "ls", "lt",  "mb", "mp", "nd",
                                         "ngk", "nt", "rk", "rp", "rs", "rt"};
    check::is_true(people("human_cold").genome.phonotactics.coda_clusters == codas,
                   "the cold coda set");
    // `tl` and `dl` are the two stop-liquid pairs most tongues refuse, and any nasal
    // paired with any stop produced `nb` and names like `Wongd`.
    for (const Resolved& r : every_people()) {
        for (const std::string& cluster : r.genome.phonotactics.onset_clusters) {
            check::is_true(cluster != "tl" && cluster != "dl",
                           r.id + " to refuse the stop-liquid pair " + cluster);
        }
        for (const std::string& cluster : r.genome.phonotactics.coda_clusters) {
            check::is_true(cluster != "nb" && cluster != "ngg" && cluster != "md",
                           r.id + " to keep nasal-stop codas homorganic, not " + cluster);
        }
        std::vector<std::string> allowed = r.genome.phonotactics.onset_clusters;
        allowed.insert(allowed.end(), r.genome.phonotactics.coda_clusters.begin(),
                       r.genome.phonotactics.coda_clusters.end());
        std::sort(allowed.begin(), allowed.end());
        allowed.erase(std::unique(allowed.begin(), allowed.end()), allowed.end());
        check::is_true(r.genome.phonotactics.allowed_clusters == allowed,
                       r.id + " allowed_clusters to be the sorted union");
    }
}

TEST(the_loanword_rate_is_published_and_read_by_nothing) {
    // Authored, validated and carried. There is no consumer anywhere; do not invent
    // one, and do not drop it either — it is part of the published genome shape.
    const std::map<std::string, double> by_outsiders{{"hostile", 0.05},    {"wary", 0.15},
                                                     {"transactional", 0.3}, {"hospitable", 0.3},
                                                     {"assimilating", 0.45}};
    int checked = 0;
    for (const Resolved& r : every_people()) {
        const auto want = by_outsiders.at(std::string(r.traits.axis("outsiders")));
        check::close(r.genome.rates.loanword.value, want, 1e-12, r.id + " loanword rate");
        check::between(r.genome.rates.third_syllable.value, 0.0, 1.0, r.id + " third syllable");
        check::between(r.genome.rates.cluster.value, 0.0, 1.0, r.id + " cluster rate");
        ++checked;
    }
    check::equal(checked, 12, "twelve rate sets checked");
}

// --- resolution, merges and overrides -------------------------------------

TEST(an_unknown_civilization_resolves_and_an_unknown_parent_race_does_not) {
    // The asymmetry is the whole point of the package: a people heritage has never
    // heard of still arrives complete, while a guessed parent race would produce a name
    // in the WRONG language rather than an approximate one.
    const Resolved stranger = resolve("copper_folk", "human");
    check::equal(stranger.id, std::string("copper_folk"), "the synthetic people keeps its id");
    check::is_true(stranger.traits.values == people("human_heartland").traits.values,
                   "an unlisted people to wear its parent race's base traits");
    // An unlisted branch does not exist in the family, so it speaks the roots unshifted.
    check::equal(stranger.genome.branch, std::string("copper_folk"), "the branch is the civ id");
    check::is_true(stranger.genome.sound_changes.empty(), "an unlisted branch shifts nothing");
    check::equal(*stranger.genome.root("stone"), std::string("kor"), "the unshifted human stone");

    check::throws([] { resolve("dwarf", ""); }, "an empty parent race");
    check::throws([] { resolve("dwarf", "orc"); }, "an unknown parent race");
    try {
        resolve("dwarf", "orc");
        check::fail("resolve accepted an unknown parent race");
    } catch (const std::exception& error) {
        check::is_true(std::string(error.what()).find("parent race") != std::string::npos,
                       "the message to name the parent race, got: " + std::string(error.what()));
    }
}

TEST(a_delta_that_restates_what_it_inherits_is_refused) {
    // A sparse delta has to read as the difference from its archetype. Asking for a
    // people against the wrong parent race is how that fires at runtime.
    check::throws([] { resolve("tidekin", "human"); },
                  "the tidekin delta against the human base");
    try {
        resolve("gnome", "elf");
        check::fail("the gnome delta against the elf base was accepted");
    } catch (const std::exception& error) {
        const std::string message = error.what();
        check::is_true(message.find("inherited value") != std::string::npos,
                       "the message to say what went wrong, got: " + message);
        check::is_true(message.find("vocal_tract") != std::string::npos,
                       "the message to name the offending axis, got: " + message);
    }
    // And the merge is not simply refusing everything: the three archetypes carry empty
    // deltas, and an empty delta against ANY base is legal.
    check::equal(resolve("human_heartland", "dwarf").traits.axis("body_scale"),
                 std::string_view("compact"), "an empty delta inherits whatever it is given");
}

TEST(the_cross_layer_appearance_checks_reject_a_body_its_traits_forbid) {
    // A delta cannot be judged until it has been merged onto what it inherits, which is
    // why these run on the resolved body and not at load.
    try {
        resolve("frosthold_dwarf", "elf");
        check::fail("a compact body on a tall frame was accepted");
    } catch (const std::exception& error) {
        const std::string message = error.what();
        check::is_true(message.find("height envelope") != std::string::npos,
                       "the height envelope to be named, got: " + message);
    }
    try {
        resolve("hill_dwarf", "elf");
        check::fail("a 280-year life on an ancient tempo was accepted");
    } catch (const std::exception& error) {
        const std::string message = error.what();
        check::is_true(message.find("lifespan_tempo") != std::string::npos,
                       "the lifespan tempo to be named, got: " + message);
    }
}

TEST(no_dwarf_stands_over_four_foot_five) {
    // 1.3462 m. This is a property of the loader rather than a promise in a comment:
    // every people under the dwarf parent race is `compact` or `small`, and both
    // ceilings sit below it. Raise either envelope and this dies loudly.
    check::is_true(body_scale_height_m("compact").second <= 1.3462, "the compact ceiling");
    check::is_true(body_scale_height_m("small").second <= 1.3462, "the small ceiling");
    int dwarves = 0;
    for (const Resolved& r : every_people()) {
        if (r.parent_race_id != "dwarf") continue;
        const auto scale = r.traits.axis("body_scale");
        check::is_true(scale == "compact" || scale == "small",
                       r.id + " to be compact or small, not " + std::string(scale));
        for (std::string_view sex : tables::sexes()) {
            const double top =
                r.appearance.at("frame").at("height_m").at(sex).at("max").as_double();
            check::is_true(top <= 1.3462, r.id + " " + std::string(sex) + " to stand under 4ft5");
        }
        ++dwarves;
    }
    check::equal(dwarves, 4, "four peoples under the dwarf parent race");
}

TEST(an_override_lands_and_is_recorded_in_provenance) {
    const Resolved& tidekin = people("tidekin");
    check::equal(tidekin.culture.at("memory", "taboo"), std::string_view("naming_the_drowned"),
                 "the tidekin taboo");
    check::equal(tidekin.culture.at("faith", "festival"), std::string_view("the_long_ebb"),
                 "the tidekin festival");
    const std::vector<std::string> expected{"culture.memory.taboo", "culture.faith.festival",
                                            "genome.inventory.consonants"};
    check::is_true(tidekin.provenance.overridden == expected,
                   "the overridden paths, culture first then genome, in document order");

    // A people with no override records none, so the list is a real signal.
    check::is_true(people("dwarf").provenance.overridden.empty(), "an unpatched people");
    int patched = 0;
    for (const Resolved& r : every_people()) {
        if (!r.provenance.overridden.empty()) ++patched;
    }
    check::equal(patched, 5, "five peoples carry a hand-tune");
}

TEST(an_inventory_patch_re_emits_in_alphabet_order) {
    const Inventory& inventory = people("tidekin").genome.inventory;
    const auto at = [&](std::string_view token) {
        return std::find(inventory.consonants.begin(), inventory.consonants.end(), token) -
               inventory.consonants.begin();
    };
    check::is_true(at("lh") < static_cast<long long>(inventory.consonants.size()),
                   "lh to have been added");
    check::is_true(at("l") < at("lh") && at("lh") < at("r"),
                   "lh to land between l and r rather than at the end");
    check::is_true(at("p") == static_cast<long long>(inventory.consonants.size()),
                   "p to have been dropped");
    check::is_true(at("g") == static_cast<long long>(inventory.consonants.size()),
                   "g to have been dropped");

    // A RECORDED DEFECT, reproduced rather than fixed: the patch names `consonants` and
    // `onsets` was computed before it, so the tidekin word-onset list still offers `p`
    // and `g` — phonemes the published consonant inventory says the tongue has lost.
    check::is_true(std::find(inventory.onsets.begin(), inventory.onsets.end(), "p") !=
                       inventory.onsets.end(),
                   "the stale p to survive in onsets, as the reference leaves it");
    check::is_true(std::find(inventory.onsets.begin(), inventory.onsets.end(), "lh") ==
                       inventory.onsets.end(),
                   "and lh to be missing from onsets for the same reason");
}

TEST(resolve_hands_back_a_copy_that_cannot_poison_the_cache) {
    Resolved first = resolve("dwarf", "dwarf");
    const std::string original = *first.genome.root("stone");
    first.genome.lexicon.clear();
    first.culture.values[0] = "vandalised";
    first.appearance.fields.clear();

    const Resolved second = resolve("dwarf", "dwarf");
    check::is_true(second.genome.root("stone") != nullptr, "the cached lexicon to survive");
    check::equal(*second.genome.root("stone"), original, "the cached root to be unchanged");
    check::is_true(!second.appearance.fields.empty(), "the cached body to survive");
    check::equal(second.culture.values[0], people("dwarf").culture.values[0],
                 "the cached culture to survive");
}

// --- appearance -----------------------------------------------------------

TEST(every_coloration_palette_is_a_population_frequency) {
    int palettes = 0;
    for (const Resolved& r : every_people()) {
        const Value& coloration = r.appearance.at("coloration");
        for (std::string_view channel : {"skin", "hair", "eye"}) {
            const Value& swatches = coloration.at(channel).at("swatches");
            check::is_true(swatches.items.size() >= 3,
                           r.id + " " + std::string(channel) + " to carry at least 3 swatches");
            double total = 0.0;
            std::set<std::string> names;
            for (const Value& swatch : swatches.items) {
                const double weight = swatch.at("weight").as_double();
                check::is_true(weight > 0.0, r.id + " swatch weights to be positive");
                total += weight;
                names.emplace(swatch.at("name").text);
                const std::string_view hex = swatch.at("hex").text;
                check::equal(hex.size(), std::size_t{7}, r.id + " hex length");
                check::is_true(hex[0] == '#', r.id + " hex to start with #");
            }
            check::equal(names.size(), swatches.items.size(), r.id + " swatch names to be unique");
            check::close(total, 1.0, 1e-6, r.id + " " + std::string(channel) + " weights");
            ++palettes;
        }
    }
    check::equal(palettes, 36, "three palettes for each of twelve peoples");
}

TEST(a_gnome_carries_a_dwarf_s_nose_and_an_elf_s_ears_at_once) {
    // That pairing is the entire likeness of a gnome, so it is asserted rather than
    // trusted to the prose.
    const Value& gnome = people("gnome").appearance;
    const std::string_view nose_note = gnome.at("features").at("nose").at("note").text;
    check::is_true(nose_note.find("ear") != std::string_view::npos,
                   "the gnome nose note to explain the ears it sits between");
    std::set<std::string> avoid;
    for (const Value& item : gnome.at("art_direction").at("avoid").items) avoid.emplace(item.text);
    check::is_true(avoid.count("small_ears") == 1, "the gnome avoid list to bar small ears");
    check::is_true(avoid.count("small_nose") == 1, "the gnome avoid list to bar a small nose");

    const std::string_view elf_ear = people("elf").appearance.at("features").at("ear").at("form").text;
    check::is_true(elf_ear.find("point") != std::string_view::npos,
                   "elves to have pointed ears, got " + std::string(elf_ear));
    const std::string_view dwarf_nose =
        people("dwarf").appearance.at("features").at("nose").at("form").text;
    check::is_true(dwarf_nose.find("broad") != std::string_view::npos,
                   "dwarves to have a broad nose, got " + std::string(dwarf_nose));
}

// --- the identity digest --------------------------------------------------

TEST(the_heritage_identity_digests_the_resolved_peoples) {
    // This port's digest over the twelve registry pairs since the owner's lifespan ruling
    // (2026-09-23: human 80, elf 1000, dwarf 500, gnome 750 years); FantasyWorldGenerator
    // b67c99b published 9b708d23... before it. It covers the RESOLVED peoples and not the
    // source bytes, so a derivation edit that changes no output invalidates nothing while a
    // one-character override that does change an output is caught. Appearance IS inside it,
    // even though it is deliberately kept out of the world document.
    const auto pairs = registry_pairs();
    const Identity identity = heritage_identity(pairs);
    check::equal(identity.sha256,
                 std::string("611007f58664a4f7456dd7153756455e9d4f685d040d09c138c2990e79e3da5d"),
                 "the heritage digest over the twelve shipped peoples");
    check::equal(identity.revisions.traits, 2, "traits revision");
    check::equal(identity.revisions.appearance, 2, "appearance revision");

    const std::string document = canonical_document(pairs);
    check::is_true(document.size() > 90000, "the canonical document to carry the whole record");
    check::is_true(document.find(' ') == std::string::npos ||
                       document.find("\": ") == std::string::npos,
                   "no separator whitespace between a key and its value");
    check::is_true(document.find("\"appearance\":") != std::string::npos,
                   "appearance to be inside the digested bytes");
    // Sorted keys, so a resolved people always opens on the same field.
    check::is_true(document.find("{\"appearance\":") != std::string::npos,
                   "each people's record to open on its sorted-first key");

    // And the digest is a function of the pairs: drop one and it must move.
    auto fewer = pairs;
    fewer.pop_back();
    check::is_true(heritage_identity(fewer).sha256 != identity.sha256,
                   "a different set of peoples to digest differently");
}

TEST(the_digest_changes_when_a_resolved_value_changes) {
    // A floor for the test above: if the serialiser dropped a layer the digest would
    // still be stable, and stable is exactly what a broken hash looks like.
    const std::vector<std::pair<std::string, std::string>> one{{"dwarf", "dwarf"}};
    const std::vector<std::pair<std::string, std::string>> other{{"gnome", "dwarf"}};
    const std::string a = canonical_document(one);
    const std::string b = canonical_document(other);
    check::is_true(a != b, "two peoples of one race to serialise differently");
    check::is_true(a.find("khas") != std::string::npos, "the dwarven stone root in the bytes");
    check::is_true(b.find("spril") != std::string::npos, "the gnomish coinage in the bytes");
    check::is_true(a.find("\"loanword\":") != std::string::npos,
                   "the unread loanword rate to still be published");
    check::is_true(heritage_identity(one).sha256 != heritage_identity(other).sha256,
                   "two peoples to carry two digests");
}

// --- naming ---------------------------------------------------------------

TEST(a_settlement_name_glosses_the_roots_it_is_built_from) {
    int named = 0;
    std::set<std::string> distinct;
    for (const Resolved& r : every_people()) {
        for (int variation = 0; variation < 40; ++variation) {
            Rng draw = Rng::child(42, "city-name-" + r.id, variation);
            const auto [name, gloss] = settlement_name(r, lexicon(), draw);
            check::is_true(!name.empty(), r.id + " to produce a name");
            check::is_true(name[0] >= 'A' && name[0] <= 'Z', r.id + " name to be capitalised: " + name);
            const auto slots = split_gloss(gloss);
            check::equal(slots.size(), std::size_t{2}, r.id + " gloss to name two slots: " + gloss);
            for (const std::string& slot : slots) {
                check::is_true(r.genome.has_root(slot),
                               r.id + " gloss slot " + slot + " to be in its own lexicon");
            }
            distinct.insert(name);
            ++named;
        }
    }
    check::equal(named, 480, "forty names for each of twelve peoples");
    check::is_true(distinct.size() > 150,
                   "the sweep to reach a real variety of names, got " +
                       std::to_string(distinct.size()));
}

TEST(the_terrain_bias_is_honoured_only_at_the_head_of_a_name) {
    // Live, tested and used by nothing in the pipeline — exactly as the loanword rate
    // is. Carried so a caller that wants a name to echo its ground can ask for one.
    const Resolved& dwarf = people("dwarf");
    int biased = 0;
    int terrain_first = 0;
    for (int variation = 0; variation < 200; ++variation) {
        Rng draw = Rng::child(42, "terrain-bias", variation);
        const auto [name, gloss] = settlement_name(dwarf, lexicon(), draw, "water");
        const auto slots = split_gloss(gloss);
        const SlotRole* terrain = lexicon().role("terrain");
        const bool head_is_terrain =
            std::find(terrain->slots.begin(), terrain->slots.end(), slots[0]) !=
            terrain->slots.end();
        if (head_is_terrain) {
            ++terrain_first;
            check::equal(slots[0], std::string("water"),
                         "a terrain-headed name to take the caller's slot, got " + gloss);
            ++biased;
        }
        check::is_true(slots[1] != "water" || !head_is_terrain,
                       "the bias not to reach the tail: " + gloss);
    }
    check::is_true(terrain_first > 20,
                   "the sweep to draw a terrain-headed template often enough to mean "
                   "something, got " + std::to_string(terrain_first));
    check::equal(biased, terrain_first, "every terrain-headed name to be biased");
}

TEST(a_forbidden_form_never_survives_the_filter) {
    // Not fastidiousness: the heartland roots `harbour`=mar and `salt`=sel compose to
    // `Marsel`, which spells `arse`. Innocent roots collide into loaded words, and a
    // generated world produces far more names than anyone reviews.
    const Resolved& heartland = people("human_heartland");
    const NameStock stock = name_stock(heartland, lexicon());
    for (const StockEntry& entry : stock.common) {
        check::is_true(entry.name != "Marsel", "Marsel to be filtered out of the common stock");
    }
    for (const StockEntry& entry : stock.rare) {
        check::is_true(entry.name != "Marsel", "Marsel to be filtered out of the rare tail");
    }
    // The pairing is reachable, so the filter is what removed it and not an absence.
    check::is_true(heartland.genome.has_root("harbour") && heartland.genome.has_root("salt"),
                   "both roots of the offending compound to exist");

    int drawn = 0;
    for (const Resolved& r : every_people()) {
        for (int variation = 0; variation < 30; ++variation) {
            Rng city = Rng::child(7, "forbidden-" + r.id, variation);
            Rng person = Rng::child(9, "forbidden-" + r.id, variation);
            const std::string names[] = {settlement_name(r, lexicon(), city).first,
                                         person_name(r, lexicon(), person),
                                         person_name(r, person)};
            for (const std::string& name : names) {
                std::string lowered = name;
                for (char& c : lowered) c = static_cast<char>(std::tolower(c));
                for (std::string_view bad : r.genome.forbidden_forms) {
                    check::is_true(lowered.find(bad) == std::string::npos,
                                   r.id + " produced " + name + ", which spells " +
                                       std::string(bad));
                }
                ++drawn;
            }
        }
    }
    check::equal(drawn, 1080, "three names for each of thirty draws for twelve peoples");
}

TEST(the_gloss_carrying_personal_name_skips_the_filter_that_person_name_runs) {
    // A RECORDED DEFECT of the reference, reproduced rather than repaired so the
    // difference is visible and the fix has an obvious home. `person_name` redraws up
    // to eight times against the banned list; `person_name_with_gloss` draws once and
    // never checks. For human_large_island the roots wood=har and salt=ser compose to
    // a name spelling a word nobody meant to ship, and terrain+craft is an authored
    // personal shape, so the pairing is reachable.
    const Resolved& island = people("human_large_island");
    check::equal(*island.genome.root("wood"), std::string("har"), "the island wood root");
    check::equal(*island.genome.root("salt"), std::string("ser"), "the island salt root");

    int leaked = 0;
    int filtered_leaks = 0;
    const int rolls = 3000;
    for (int variation = 0; variation < rolls; ++variation) {
        Rng glossed = Rng::child(5, "gloss-leak", variation);
        Rng filtered = Rng::child(5, "gloss-leak", variation);
        const auto unguarded = person_name_with_gloss(island, lexicon(), glossed).first;
        const auto guarded = person_name(island, lexicon(), filtered);
        const auto spells_a_banned_form = [&](const std::string& name) {
            std::string lowered = name;
            for (char& c : lowered) c = static_cast<char>(std::tolower(c));
            for (std::string_view bad : island.genome.forbidden_forms) {
                if (lowered.find(bad) != std::string::npos) return true;
            }
            return false;
        };
        if (spells_a_banned_form(unguarded)) ++leaked;
        if (spells_a_banned_form(guarded)) ++filtered_leaks;
    }
    check::is_true(leaked > 0,
                   "the unguarded path to leak at least once in " + std::to_string(rolls) +
                       " draws; if this stops happening the defect was fixed and this "
                       "test should be turned into its guarantee");
    check::equal(filtered_leaks, 0, "and the guarded path to leak never");
}

TEST(the_seam_rules_run_in_the_order_that_makes_them_mean_anything) {
    // Geminate, then hiatus, then digraph. Each is observable in a composed stock name.
    const NameStock dwarf = name_stock(people("dwarf"), lexicon());
    bool found_digraph_repair = false;
    for (const StockEntry& entry : dwarf.common) {
        if (entry.gloss == "old-shore") {
            // `ulk` + `hakn` would read as `ul-khakn`, inventing a `kh` neither root
            // has. The first root's final consonant goes instead.
            check::equal(entry.name, std::string("Ulhakn"), "the digraph seam to be repaired");
            found_digraph_repair = true;
        }
    }
    check::is_true(found_digraph_repair, "the old-shore compound to be in the dwarf stock");

    // Whether a doubled letter survives is the tongue's decision. Dwarvish keeps
    // geminates; gnomish does not.
    check::is_true(people("dwarf").genome.phonotactics.geminates, "dwarvish to hold geminates");
    check::is_true(!people("gnome").genome.phonotactics.geminates, "gnomish to simplify them");
    check::equal(realm_name(people("dwarf"), "Bor"), std::string("Borrik"), "dwarvish Bor+rik");
    check::equal(realm_name(people("gnome"), "Bor"), std::string("Borik"), "gnomish Bor+rik");
    check::equal(realm_name(people("dwarf"), "Bargdorn"), std::string("Bargdornrik"),
                 "a realm named for its seat");
    check::equal(realm_name(people("dwarf"), ""), std::string(),
                 "no capital, no realm name — the caller keeps its English form");
}

TEST(the_geminate_rule_is_consulted_before_the_digraph_repair) {
    // No shipped pair of roots reaches this, so the case is synthetic. It matters
    // because a doubled VOWEL across a seam is both a geminate and a digraph: `ula` +
    // `ank` spells `aa`, and the two rules disagree about it. Geminate first keeps the
    // long vowel a stone-hall tongue actually has; digraph first eats it.
    Resolved dwarf = resolve("dwarf", "dwarf");
    check::is_true(dwarf.genome.phonotactics.geminates, "a tongue that holds geminates");
    dwarf.genome.lexicon = {{"old", "ula"}, {"iron", "ank"}};
    const auto composed = [](const Resolved& r, const std::string& gloss) {
        for (const StockEntry& entry : name_stock(r, lexicon()).common) {
            if (entry.gloss == gloss) return entry.name;
        }
        check::fail("no stock entry glossed " + gloss);
    };
    check::equal(composed(dwarf, "old-iron"), std::string("Ulaank"),
                 "the geminate to survive rather than the seam being repaired to Ulank");

    // And the same seam under a tongue with no geminates collapses to one letter.
    Resolved gnome = resolve("gnome", "dwarf");
    check::is_true(!gnome.genome.phonotactics.geminates, "a tongue that simplifies them");
    gnome.genome.lexicon = {{"old", "ula"}, {"iron", "ank"}};
    check::equal(composed(gnome, "old-iron"), std::string("Ulank"), "the simplified seam");
}

TEST(the_template_tables_are_in_their_authored_order) {
    // A cumulative weighted walk runs these IN DOCUMENT ORDER, so reordering either
    // list re-maps the draw intervals and renames every settlement in every world at
    // the same seed. Nothing downstream would notice: the names stay plausible.
    const auto& settlements = lexicon().settlement_templates;
    check::equal(settlements.size(), std::size_t{3}, "three settlement shapes");
    const std::vector<std::vector<std::string>> want_settlements{
        {"terrain", "site", "5"}, {"quality", "terrain", "3"}, {"craft", "site", "2"}};
    for (std::size_t i = 0; i < settlements.size(); ++i) {
        check::equal(settlements[i].pattern[0], std::string_view(want_settlements[i][0]),
                     "settlement template " + std::to_string(i) + " head");
        check::equal(settlements[i].pattern[1], std::string_view(want_settlements[i][1]),
                     "settlement template " + std::to_string(i) + " tail");
        check::equal(std::to_string(settlements[i].weight), want_settlements[i][2],
                     "settlement template " + std::to_string(i) + " weight");
    }

    const auto& personal = lexicon().personal_templates;
    check::equal(personal.size(), std::size_t{4}, "four personal shapes");
    const std::vector<std::vector<std::string>> want_personal{{"quality", "craft", "4"},
                                                              {"craft", "quality", "3"},
                                                              {"terrain", "craft", "2"},
                                                              {"quality", "terrain", "2"}};
    for (std::size_t i = 0; i < personal.size(); ++i) {
        check::equal(personal[i].pattern[0], std::string_view(want_personal[i][0]),
                     "personal template " + std::to_string(i) + " head");
        check::equal(personal[i].pattern[1], std::string_view(want_personal[i][1]),
                     "personal template " + std::to_string(i) + " tail");
        check::equal(std::to_string(personal[i].weight), want_personal[i][2],
                     "personal template " + std::to_string(i) + " weight");
        // No personal shape is terrain+site, which is what a settlement is: a person
        // and a town must not come out looking like the same kind of word. And none
        // reaches `site` at all.
        check::is_true(personal[i].pattern[0] != "site" && personal[i].pattern[1] != "site",
                       "no personal template to name a site");
    }
}

TEST(a_named_city_is_the_same_city_on_the_same_seed) {
    // A golden record, and the only test here that pins the whole naming chain at once:
    // the template walk's direction, the sorted option lists, the seam rules, the
    // alphabet order and the sound shifts all reach these strings. Any of them moving
    // renames every settlement in every world, which is a change that deserves to be
    // made deliberately rather than noticed in a screenshot.
    // Compared as one string so a break shows the whole record rather than the first
    // row that moved.
    // These six between them land on all three settlement templates, so a walk taken in
    // the wrong direction moves at least one of them.
    const std::string expected =
        "dwarf=Sulkbod[salt-house] "
        "elf=Laensila[field-grove] "
        "human_heartland=Strandpat[shore-well] "
        "frosthold_dwarf=Borbrol[gold-forge] "
        "gnome=Guldbod[wind-house] "
        "tidekin=Lainael[green-water]";
    std::string actual;
    for (const char* civ : {"dwarf", "elf", "human_heartland", "frosthold_dwarf", "gnome",
                            "tidekin"}) {
        Rng draw = Rng::child(42, std::string("city-name-") + civ, 0);
        const auto [name, gloss] = settlement_name(people(civ), lexicon(), draw);
        if (!actual.empty()) actual += ' ';
        actual += std::string(civ) + "=" + name + "[" + gloss + "]";
    }
    check::equal(actual, expected, "the named cities of seed 42");
}

TEST(only_the_final_syllable_of_a_coined_name_closes) {
    // A medial coda would meet the next onset and build a cluster the tongue does not
    // permit, and a one-syllable name must close or a heavy monosyllabic tongue
    // produces bare `Na`. human_cold has short breath, so it is that tongue.
    const Resolved& cold = people("human_cold");
    check::equal(cold.genome.prosody.word_shape, std::string_view("monosyllabic_heavy"),
                 "a short-breathed people speaks in heavy monosyllables");
    const std::string vowels = "aeiou";
    int closed = 0;
    for (int variation = 0; variation < 200; ++variation) {
        Rng draw = Rng::child(11, "coined", variation);
        const std::string name = person_name(cold, draw);
        check::is_true(!name.empty(), "a coined name");
        check::is_true(vowels.find(name.back()) == std::string::npos,
                       "a heavy monosyllable to end on a consonant, got " + name);
        ++closed;
    }
    check::equal(closed, 200, "two hundred coined names checked");
}

TEST(an_optional_coda_appears_rather_more_often_than_not) {
    // The gate is written as a SKIP at 0.55, so a coda appears 55% of the time. Written
    // the other way round it would appear 45% of the time and every optional coda in
    // the world would flip — a change nothing else here would notice, because the names
    // stay perfectly plausible.
    //
    // Dwarvish is disyllabic and closes, and its count is never one, so its final
    // syllable always takes the optional gate and nothing else does.
    const Resolved& dwarf = people("dwarf");
    check::equal(dwarf.genome.prosody.word_shape, std::string_view("disyllabic"),
                 "a disyllabic tongue, so the last syllable is optional rather than required");
    check::is_true(dwarf.genome.sound_changes.empty(),
                   "and one with no shifts, so the coined ending is the drawn one");
    const std::string vowels = "aeiou";
    int closed = 0;
    const int rolls = 4000;
    for (int variation = 0; variation < rolls; ++variation) {
        Rng draw = Rng::child(17, "optional-coda", variation);
        const std::string name = person_name(dwarf, draw);
        check::is_true(!name.empty(), "a coined name");
        if (vowels.find(name.back()) == std::string::npos) ++closed;
    }
    const double share = static_cast<double>(closed) / rolls;
    check::between(share, 0.50, 0.61, "the share of coined names that close");
    check::is_true(closed > 0 && closed < rolls,
                   "both outcomes to occur, got " + std::to_string(closed) + " closed");
}

TEST(a_dithematic_name_falls_back_to_a_coinage_rather_than_failing) {
    int with_gloss = 0;
    int without = 0;
    for (const Resolved& r : every_people()) {
        for (int variation = 0; variation < 30; ++variation) {
            Rng draw = Rng::child(13, "person-" + r.id, variation);
            const auto [name, gloss] = person_name_with_gloss(r, lexicon(), draw);
            check::is_true(!name.empty(), r.id + " to produce a personal name");
            if (gloss.empty()) {
                ++without;
            } else {
                const auto slots = split_gloss(gloss);
                check::equal(slots.size(), std::size_t{2}, r.id + " gloss: " + gloss);
                check::is_true(slots[0] != slots[1],
                               "a personal name never repeats one element: " + gloss);
                check::is_true(r.genome.has_root(slots[0]) && r.genome.has_root(slots[1]),
                               r.id + " gloss slots to be in its own lexicon");
                ++with_gloss;
            }
        }
    }
    check::equal(with_gloss + without, 360, "thirty names for each of twelve peoples");
    check::is_true(with_gloss > 300,
                   "the dithematic path to be the usual one, got " + std::to_string(with_gloss));
}

TEST(a_name_table_for_a_preferred_hiatus_tongue_carries_one_empty_coda) {
    // A compatibility bridge with no consumer left. Its own comment claims the two
    // appended empty codas make an open syllable twice as likely; the deduplicating
    // sort collapses them to ONE, which is about one entry in twenty-two.
    const NameTable elf = name_table(people("elf"));
    check::equal(elf.third_syllable_chance, 0.7, "the elven third-syllable rate");
    check::equal(elf.onsets.size(), std::size_t{36}, "twelve consonants by three vowels");
    check::equal(elf.nuclei.size(), std::size_t{12}, "the elven vowel set");
    const auto empties = std::count(elf.codas.begin(), elf.codas.end(), std::string());
    check::equal(empties, std::ptrdiff_t{1}, "exactly one empty coda, not two");
    check::equal(elf.codas.size(), std::size_t{22}, "twenty-two coda entries");

    const NameTable dwarf = name_table(people("dwarf"));
    check::equal(std::count(dwarf.codas.begin(), dwarf.codas.end(), std::string()),
                 std::ptrdiff_t{0}, "a forbidden-hiatus tongue appends none");
}

// --- the stock ------------------------------------------------------------

TEST(a_name_stock_is_the_same_in_every_world_and_every_process) {
    // Rank has to be a property of the culture rather than of a seed — Eldsel is a
    // common heartland name in every world the way John was common in every English
    // county — and the package carries no seed logic to permute it with.
    const std::map<std::string, std::size_t> expected_common{
        {"human_heartland", 88}, {"elf", 112}, {"dwarf", 130}, {"gnome", 184}};
    int checked = 0;
    for (const Resolved& r : every_people()) {
        const NameStock first = name_stock(r, lexicon());
        const NameStock second = name_stock(r, lexicon());
        check::is_true(first.common.size() == second.common.size(),
                       r.id + " stock to be reproducible");
        for (std::size_t i = 0; i < first.common.size(); ++i) {
            check::equal(first.common[i].name, second.common[i].name,
                         r.id + " rank " + std::to_string(i));
        }
        const auto want = expected_common.find(r.id);
        if (want != expected_common.end()) {
            check::equal(first.common.size(), want->second, r.id + " common stock size");
        }
        check::is_true(first.common.size() >= 80, r.id + " to compose a usable stock");
        check::is_true(!first.rare.empty(), r.id + " to have a rare tail");
        std::set<std::string> spoken;
        for (const StockEntry& e : first.common) spoken.insert(e.name);
        for (const StockEntry& e : first.rare) {
            check::is_true(spoken.count(e.name) == 0,
                           r.id + " rare " + e.name + " to not also be common");
        }
        ++checked;
    }
    check::equal(checked, 12, "twelve stocks checked");
}

TEST(the_head_of_a_stock_is_held_root_disjoint) {
    // Ranking on length alone put the whole heartland head on one root — Eldsel,
    // Frosel, Nersel, Versel — and a reader meeting them in succession hears a stutter
    // rather than a culture.
    int checked = 0;
    for (const Resolved& r : every_people()) {
        const NameStock stock = name_stock(r, lexicon());
        check::is_true(stock.common.size() >= 5, r.id + " to fill a head");
        std::set<std::string> slots;
        for (std::size_t i = 0; i < 5; ++i) {
            for (const std::string& slot : split_gloss(stock.common[i].gloss)) slots.insert(slot);
        }
        check::equal(slots.size(), std::size_t{10},
                     r.id + " head to stand on ten distinct roots");
        ++checked;
    }
    check::equal(checked, 12, "twelve heads checked");
    check::equal(name_stock(people("human_heartland"), lexicon()).common.front().name,
                 std::string("Eldsel"), "the commonest heartland name");
}

TEST(a_stock_too_inbred_to_spread_its_head_keeps_the_plain_rank_order) {
    // No shipped people reaches this branch, so the test is synthetic — a version built
    // on the data would pass on the data rather than on the property. Cut a genome down
    // to a handful of roots and the head cannot be filled with root-disjoint names.
    Resolved narrow = resolve("dwarf", "dwarf");
    std::vector<Root> kept;
    for (const Root& root : narrow.genome.lexicon) {
        if (root.slot == "old" || root.slot == "high" || root.slot == "gold" ||
            root.slot == "iron") {
            kept.push_back(root);
        }
    }
    check::equal(kept.size(), std::size_t{4}, "two qualities and two crafts to remain");
    narrow.genome.lexicon = kept;

    const NameStock stock = name_stock(narrow, lexicon());
    check::is_true(!stock.common.empty(), "a narrow lexicon to still compose something");
    check::is_true(stock.common.size() < 10, "and to compose only a handful");
    // The rank order comes back WHOLE rather than the head running short.
    check::equal(stock.thresholds.size(), stock.common.size(),
                 "one threshold per rank, however the head was filled");
    check::close(stock.thresholds.back(), 1.0, 1e-12, "the last threshold");
}

TEST(the_thresholds_are_cumulative_and_always_reach_one) {
    // Normalised at the end so a stock smaller than the head table still sums to one
    // rather than silently dropping its last ranks below the last threshold.
    int checked = 0;
    for (const Resolved& r : every_people()) {
        const NameStock stock = name_stock(r, lexicon());
        check::equal(stock.thresholds.size(), stock.common.size(), r.id + " one per rank");
        for (std::size_t i = 1; i < stock.thresholds.size(); ++i) {
            check::is_true(stock.thresholds[i] > stock.thresholds[i - 1],
                           r.id + " thresholds to rise at rank " + std::to_string(i));
        }
        check::close(stock.thresholds.front(), 0.35, 1e-9, r.id + " commonest share");
        check::close(stock.thresholds[4], 0.79, 1e-9, r.id + " top five share");
        check::close(stock.thresholds.back(), 1.0, 1e-12, r.id + " last threshold");
        ++checked;
    }
    check::equal(checked, 12, "twelve threshold ladders checked");
}

TEST(stock_name_spends_exactly_two_draws_whichever_branch_it_takes) {
    // Load-bearing, and the reason both draws happen up front: a branch that spent a
    // different number of words would desynchronise a replay from itself the moment
    // the rare tail was added.
    const NameStock stock = name_stock(people("human_heartland"), lexicon());
    std::set<std::string> rare_names;
    for (const StockEntry& e : stock.rare) rare_names.insert(e.name);

    Rng draw(42);
    int rare_seen = 0;
    int common_seen = 0;
    for (int i = 0; i < 4000; ++i) {
        const std::uint64_t before = draw.draws();
        const auto picked = stock_name(stock, draw);
        check::equal(draw.draws() - before, std::uint64_t{2},
                     "two words per draw at call " + std::to_string(i));
        check::is_true(picked.has_value(), "a name from a non-empty stock");
        if (rare_names.count(picked->name) != 0) {
            ++rare_seen;
        } else {
            ++common_seen;
        }
    }
    check::is_true(rare_seen > 10,
                   "the rare branch to be exercised, got " + std::to_string(rare_seen));
    check::is_true(common_seen > 3000, "and the common branch to be the usual one");

    // An empty common stock is refused whole, even when the tail composes: promoting
    // the rare pairings would make the tail the whole tongue and hide an authoring
    // fault behind names that look perfectly fine.
    const NameStock tail_only{{}, {{"Ghost", "old-iron"}}, {}};
    Rng other(7);
    for (int i = 0; i < 200; ++i) {
        const std::uint64_t before = other.draws();
        check::is_true(!stock_name(tail_only, other).has_value(),
                       "a tail-only stock to yield nothing");
        check::equal(other.draws() - before, std::uint64_t{2},
                     "and to still spend its two words");
    }
}

TEST(the_commonest_name_lands_on_about_a_third_of_a_people) {
    // The measured target: 35% for the commonest, about 79% for the top five. Drawn
    // flat from 88 names those would be 1.1% and 5.7%.
    const NameStock stock = name_stock(people("human_heartland"), lexicon());
    Rng draw = Rng::child(42, "roster", 0);
    std::map<std::string, int> counts;
    const int rolls = 4000;
    for (int i = 0; i < rolls; ++i) {
        const auto picked = stock_name(stock, draw);
        check::is_true(picked.has_value(), "a name");
        ++counts[picked->name];
    }
    std::vector<int> ordered;
    for (const auto& [name, count] : counts) ordered.push_back(count);
    std::sort(ordered.rbegin(), ordered.rend());
    const double commonest = static_cast<double>(ordered.front()) / rolls;
    double top_five = 0.0;
    for (std::size_t i = 0; i < 5 && i < ordered.size(); ++i) {
        top_five += static_cast<double>(ordered[i]) / rolls;
    }
    check::between(commonest, 0.30, 0.40, "the commonest name's share");
    check::between(top_five, 0.72, 0.85, "the top five's share");
    check::is_true(counts.size() > 20,
                   "a long tail to exist as well, got " + std::to_string(counts.size()) +
                       " distinct names");
}
