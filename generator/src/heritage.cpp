#include "fwg/heritage.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

#include "fwg/sha256.hpp"

namespace fwg::heritage {
namespace {

[[noreturn]] void fault(const std::string& message) { throw std::invalid_argument(message); }

std::string quoted(std::string_view text) { return "'" + std::string(text) + "'"; }

// ---------------------------------------------------------------------------
// Small string work. ASCII only, deliberately: the alphabet is ASCII digraphs rather
// than IPA because the repository hashes bytes and disables text normalisation for
// every provenance destination.
// ---------------------------------------------------------------------------

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }
char upper(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; }

std::string lowercased(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = lower(c);
    return out;
}

// Python's str.capitalize: upper the first character AND LOWER THE REST. Everything
// composed here is already lowercase except a capital name handed to realm_name, which
// is exactly why the second half matters.
std::string capitalized(std::string_view text) {
    std::string out = lowercased(text);
    if (!out.empty()) out[0] = upper(out[0]);
    return out;
}

bool contains(std::span<const std::string_view> items, std::string_view what) {
    return std::find(items.begin(), items.end(), what) != items.end();
}

bool contains(const std::vector<std::string_view>& items, std::string_view what) {
    return std::find(items.begin(), items.end(), what) != items.end();
}

// ---------------------------------------------------------------------------
// Canonical serialisation.
//
// The identity digest is a digest of THIS text, so it is built here rather than through
// JsonWriter: the writer emits in insertion order with whitespace, and the digest needs
// sorted keys, no separators and non-ASCII left literal. That is a third serialisation
// scheme in this codebase and it agrees with neither of the other two on a non-ASCII
// string — do not assume one of them will do.
// ---------------------------------------------------------------------------

void emit_string(std::string& out, std::string_view text) {
    out += '"';
    for (char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (byte < 0x20) {
                    char buffer[7];
                    std::snprintf(buffer, sizeof buffer, "\\u%04x", byte);
                    out += buffer;
                } else {
                    // Non-ASCII stays literal UTF-8: the reference serialises with
                    // ensure_ascii=False and the digest is over those bytes.
                    out += c;
                }
        }
    }
    out += '"';
}

void emit_value(std::string& out, const Value& value) {
    switch (value.kind) {
        case Value::Kind::Text: emit_string(out, value.text); return;
        case Value::Kind::Number: out += value.number.canon; return;
        case Value::Kind::Integer: out += std::to_string(value.integer); return;
        case Value::Kind::Boolean: out += value.boolean ? "true" : "false"; return;
        case Value::Kind::Array: {
            out += '[';
            for (std::size_t i = 0; i < value.items.size(); ++i) {
                if (i != 0) out += ',';
                emit_value(out, value.items[i]);
            }
            out += ']';
            return;
        }
        case Value::Kind::Object: {
            // Keys sorted, ELEMENTS NOT: sorted keys do not sort a list, which is why
            // every list order in this package is a contract on the digest as well as
            // on the output.
            std::vector<const Field*> ordered;
            ordered.reserve(value.fields.size());
            for (const Field& field : value.fields) ordered.push_back(&field);
            std::sort(ordered.begin(), ordered.end(),
                      [](const Field* a, const Field* b) { return a->first < b->first; });
            out += '{';
            for (std::size_t i = 0; i < ordered.size(); ++i) {
                if (i != 0) out += ',';
                emit_string(out, ordered[i]->first);
                out += ':';
                emit_value(out, ordered[i]->second);
            }
            out += '}';
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Value construction helpers
// ---------------------------------------------------------------------------

Value text_value(std::string_view text) {
    Value v;
    v.kind = Value::Kind::Text;
    v.text = text;
    return v;
}

Value integer_value(long long n) {
    Value v;
    v.kind = Value::Kind::Integer;
    v.integer = n;
    return v;
}

Value number_value(Number n) {
    Value v;
    v.kind = Value::Kind::Number;
    v.number = n;
    return v;
}

Value boolean_value(bool b) {
    Value v;
    v.kind = Value::Kind::Boolean;
    v.boolean = b;
    return v;
}

Value array_value(std::vector<Value> items) {
    Value v;
    v.kind = Value::Kind::Array;
    v.items = std::move(items);
    return v;
}

Value object_value(std::vector<Field> fields) {
    Value v;
    v.kind = Value::Kind::Object;
    v.fields = std::move(fields);
    return v;
}

template <typename Range>
Value text_array(const Range& items) {
    std::vector<Value> out;
    for (const auto& item : items) out.push_back(text_value(item));
    return array_value(std::move(out));
}

// ---------------------------------------------------------------------------
// The appearance arena
// ---------------------------------------------------------------------------

Value materialise(std::uint32_t index) {
    const tables::ValueNode& node = tables::appearance_nodes()[index];
    Value value;
    value.kind = node.kind;
    value.text = node.text;
    value.number = {node.number, node.canon};
    value.integer = node.integer;
    value.boolean = node.boolean;
    const auto children = tables::appearance_children();
    const auto keys = tables::appearance_child_keys();
    for (std::uint32_t i = node.child_begin; i < node.child_end; ++i) {
        if (node.kind == Value::Kind::Object) {
            value.fields.emplace_back(keys[i], materialise(children[i]));
        } else {
            value.items.push_back(materialise(children[i]));
        }
    }
    return value;
}

Value body_for(std::span<const tables::BodyRow> rows, std::string_view id, bool required,
               std::string_view what) {
    for (const tables::BodyRow& row : rows) {
        if (row.id == id) return materialise(row.root);
    }
    if (required) fault("no " + std::string(what) + " appearance for " + quoted(id));
    return object_value({});
}

// ---------------------------------------------------------------------------
// Traits
// ---------------------------------------------------------------------------

int craft_focus_axis() {
    static const int index = axis_index("craft_focus");
    return index;
}

const tables::TraitRow* trait_row(std::span<const tables::TraitRow> rows, std::string_view id) {
    for (const tables::TraitRow& row : rows) {
        if (row.id == id) return &row;
    }
    return nullptr;
}

Traits base_traits(const tables::TraitRow& row) {
    Traits traits;
    for (const tables::AxisValue& entry : row.axes) {
        if (entry.axis == craft_focus_axis()) {
            traits.craft_focus = entry.crafts;
        } else {
            traits.values[static_cast<std::size_t>(entry.axis)] = entry.value;
        }
    }
    return traits;
}

// Shallow, key by key: an axis has no internal structure, so there is nothing to merge.
//
// The reference's other error here — a delta naming an axis the base does not carry —
// is unrepresentable in the port: a base is an array of all seventeen and a delta entry
// carries an axis INDEX the generator resolved. What remains is the one that can still
// fire on a call, because a delta authored against one race can be asked for against
// another: a delta that restates what it inherits is an authoring error, since a delta
// is meant to read as the difference from its archetype.
Traits merge_traits(const Traits& base, const tables::TraitRow* delta, std::string_view label) {
    Traits merged = base;
    if (delta == nullptr) return merged;
    for (const tables::AxisValue& entry : delta->axes) {
        const auto name = axes()[static_cast<std::size_t>(entry.axis)].name;
        if (entry.axis == craft_focus_axis()) {
            if (base.craft_focus == entry.crafts) {
                fault(std::string(label) + " overrides axis 'craft_focus' with its inherited value");
            }
            merged.craft_focus = entry.crafts;
        } else {
            if (base.values[static_cast<std::size_t>(entry.axis)] == entry.value) {
                fault(std::string(label) + " overrides axis " + quoted(name) +
                      " with its inherited value");
            }
            merged.values[static_cast<std::size_t>(entry.axis)] = entry.value;
        }
    }
    return merged;
}

// Depth two: a block, then a key, and a key's value replaces its inherited value whole.
// Deeper than the trait merge because a body has internal structure — a height band is
// four numbers and a palette is a list of swatches — and no deeper than two, which is
// the same bound the override merge keeps so the rule stays statable in one sentence.
Value merge_appearance(Value base, const Value& delta, std::string_view label) {
    for (const Field& block : delta.fields) {
        Value* target = nullptr;
        for (Field& candidate : base.fields) {
            if (candidate.first == block.first) target = &candidate.second;
        }
        if (target == nullptr) {
            fault(std::string(label) + " extends unknown appearance block " + quoted(block.first));
        }
        for (const Field& entry : block.second.fields) {
            Value* slot = nullptr;
            for (Field& candidate : target->fields) {
                if (candidate.first == entry.first) slot = &candidate.second;
            }
            if (slot == nullptr) {
                fault(std::string(label) + " extends unknown key " + std::string(block.first) +
                      "." + std::string(entry.first));
            }
            if (*slot == entry.second) {
                fault(std::string(label) + " extends " + std::string(block.first) + "." +
                      std::string(entry.first) + " with its inherited value");
            }
            *slot = entry.second;
        }
    }
    return base;
}

std::string number_text(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%g", value);
    return buffer;
}

// Cross-layer agreement, judged on a COMPLETE body. One implementation, two callers:
// a race base at load and a people after the merge, because a sparse delta cannot be
// measured against an envelope until it has been merged onto what it inherits.
void check_resolved_appearance(const Value& body, const Traits& traits, std::string_view label) {
    const auto scale = traits.axis("body_scale");
    const auto [floor_m, ceiling_m] = body_scale_height_m(scale);
    const Value& heights = body.at("frame").at("height_m");
    for (std::string_view sex : tables::sexes()) {
        const Value& band = heights.at(sex);
        const double low = band.at("min").as_double();
        const double high = band.at("max").as_double();
        if (low < floor_m || high > ceiling_m) {
            fault(std::string(label) + " is body_scale " + quoted(scale) +
                  ", whose height envelope is " + number_text(floor_m) + ".." +
                  number_text(ceiling_m) + " m; its " + std::string(sex) + " band is " +
                  number_text(low) + ".." + number_text(high) + " m");
        }
    }

    const Value& stages = body.at("life_stages");
    const auto order = tables::life_stage_order();
    if (order.size() < 4) fault("the life-stage order table lost a stage");
    std::vector<double> onsets;
    for (std::string_view stage : order) {
        onsets.push_back(stages.at(stage).at("onset_years").as_double());
    }
    if (onsets.front() != 0.0) {
        fault(std::string(label) + " life_stages.child must begin at year 0");
    }
    for (std::size_t i = 0; i + 1 < onsets.size(); ++i) {
        if (onsets[i] >= onsets[i + 1]) {
            fault(std::string(label) + " life_stages." + std::string(order[i]) + " begins at " +
                  number_text(onsets[i]) + " and " + std::string(order[i + 1]) + " at " +
                  number_text(onsets[i + 1]) + "; stages run in order");
        }
    }

    std::vector<double> growth;
    for (std::size_t i = 0; i < 3 && i < order.size(); ++i) {
        growth.push_back(stages.at(order[i]).at("height_fraction").as_double());
    }
    if (!(growth[0] < growth[1] && growth[1] < growth[2])) {
        fault(std::string(label) + " does not grow from child to adult");
    }
    // An elder is deliberately NOT covered: the elf base is exactly 1.0 and five others
    // shrink below it, so a strict elder < adult bound would reject shipped data.
    if (stages.at("adult").at("height_fraction").as_double() != 1.0) {
        fault(std::string(label) +
              " life_stages.adult.height_fraction is the reference stature every band is "
              "stated at, and must be exactly 1");
    }
    const double maximum = stages.at("max_years").as_double();
    if (maximum <= onsets.back()) {
        fault(std::string(label) + " life_stages.max_years " + number_text(maximum) +
              " is not past its elder onset " + number_text(onsets.back()));
    }
    const auto tempo = traits.axis("lifespan_tempo");
    const auto [low_years, high_years] = lifespan_tempo_years(tempo);
    if (maximum < low_years || maximum > high_years) {
        fault(std::string(label) + " has lifespan_tempo " + quoted(tempo) + ", whose span is " +
              number_text(low_years) + ".." + number_text(high_years) + " years; max_years is " +
              number_text(maximum));
    }
}

// ---------------------------------------------------------------------------
// Derivation
// ---------------------------------------------------------------------------

std::size_t enum_index(int axis, std::string_view value) {
    const auto& spec = axes()[static_cast<std::size_t>(axis)];
    for (std::size_t i = 0; i < spec.values.size(); ++i) {
        if (spec.values[i] == value) return i;
    }
    fault("trait " + quoted(spec.name) + " holds " + quoted(value) + ", which is not one of its "
          "enum values");
}

std::string_view apply_rule(const tables::Rule& rule, const Traits& traits) {
    switch (rule.kind) {
        case tables::Rule::Kind::Copy:
            return traits.values[static_cast<std::size_t>(rule.axis)];
        case tables::Rule::Kind::Craft:
            return traits.craft_focus[static_cast<std::size_t>(rule.craft)];
        case tables::Rule::Kind::Map:
            return rule.map[enum_index(rule.axis, traits.values[static_cast<std::size_t>(rule.axis)])];
    }
    fault("unreachable derivation rule kind");
}

std::string_view apply(const tables::TextRule& rule, const Traits& traits) {
    return rule.map[enum_index(rule.axis, traits.values[static_cast<std::size_t>(rule.axis)])];
}
bool apply(const tables::BoolRule& rule, const Traits& traits) {
    return rule.map[enum_index(rule.axis, traits.values[static_cast<std::size_t>(rule.axis)])];
}
Number apply(const tables::NumberRule& rule, const Traits& traits) {
    return rule.map[enum_index(rule.axis, traits.values[static_cast<std::size_t>(rule.axis)])];
}

Culture derive_culture(const Traits& traits) {
    Culture culture;
    const auto rules = tables::culture_rules();
    for (std::size_t i = 0; i < rules.size(); ++i) culture.values[i] = apply_rule(rules[i], traits);
    return culture;
}

// (max_onset_cluster, max_coda_cluster). `(C)(C)V(C)` is two onsets and one coda,
// `(C)V(C)(C)` the reverse, and N COUNTS AS A CODA. This counts literal substrings
// rather than parsing a grammar: the eight shipped templates are the whole input domain
// and the counting rule IS the specification. `surf`'s `(V)` tail is counted by neither
// and is silently ignored, exactly as the reference ignores it.
std::pair<int, int> template_shape(std::string_view syllable) {
    const auto split = syllable.find('V');
    const auto head = syllable.substr(0, split == std::string_view::npos ? syllable.size() : split);
    const auto tail = split == std::string_view::npos ? std::string_view{}
                                                      : syllable.substr(split + 1);
    const auto count = [](std::string_view text, std::string_view needle) {
        int found = 0;
        for (std::size_t at = text.find(needle); at != std::string_view::npos;
             at = text.find(needle, at + needle.size())) {
            ++found;
        }
        return found;
    };
    return {count(head, "(C)"), count(tail, "(C)") + count(tail, "(N)")};
}

std::vector<std::string> sorted_unique(std::vector<std::string> items) {
    std::sort(items.begin(), items.end());
    items.erase(std::unique(items.begin(), items.end()), items.end());
    return items;
}

// Plausible clusters from what the inventory actually holds.
//
// Onsets and codas are built separately because they are not interchangeable: a sequence
// that opens a syllable rarely closes one. Letting any fricative lead produced onsets
// like `kht`, and pairing any nasal with any stop produced codas like `nb` and names
// like `Pimd` and `Wongd`. Both are the kind of form a real tongue would have levelled
// long ago.
std::pair<std::vector<std::string>, std::vector<std::string>> clusters(
    const std::vector<std::string_view>& consonants, int onset_max, int coda_max) {
    const tables::ClusterTables& table = tables::cluster_tables();
    const auto present = [&](std::span<const std::string_view> source) {
        std::vector<std::string_view> out;
        for (std::string_view token : source) {
            if (contains(consonants, token)) out.push_back(token);
        }
        return out;
    };
    const auto stops = present(table.onset_stops);
    const auto voiceless = present(table.voiceless_stops);
    const auto liquids = present(table.liquids);

    std::vector<std::string> onset;
    std::vector<std::string> coda;
    if (onset_max > 1) {
        for (std::string_view a : stops) {
            for (std::string_view b : liquids) {
                bool refused = false;
                for (const auto& pair : table.refused_stop_liquid_onsets) {
                    if (pair[0] == a && pair[1] == b) refused = true;
                }
                if (!refused) onset.push_back(std::string(a) + std::string(b));
            }
        }
        if (contains(consonants, "s")) {
            for (std::string_view b : voiceless) onset.push_back("s" + std::string(b));
        }
    }
    if (coda_max > 1) {
        for (std::string_view a : liquids) {
            for (std::string_view b : voiceless) coda.push_back(std::string(a) + std::string(b));
        }
        if (contains(consonants, "s")) {
            for (std::string_view a : liquids) coda.push_back(std::string(a) + "s");
        }
        for (const tables::Homorganic& row : table.homorganic_nasal_stop_codas) {
            if (!contains(consonants, row.nasal)) continue;
            for (std::string_view b : row.stops) {
                if (contains(consonants, b)) coda.push_back(std::string(row.nasal) + std::string(b));
            }
        }
    }
    return {sorted_unique(std::move(onset)), sorted_unique(std::move(coda))};
}

const LexiconFamily& family_for(const Lexicon& lex, std::string_view parent_race_id) {
    for (const LexiconFamily& family : lex.families) {
        if (family.parent_race == parent_race_id) return family;
    }
    fault("no lexicon family for parent race " + quoted(parent_race_id));
}

const BranchSpec* branch_for(const LexiconFamily& family, std::string_view branch) {
    for (const BranchSpec& spec : family.branches) {
        if (spec.id == branch) return &spec;
    }
    return nullptr;  // an unlisted people speaks the family roots unshifted
}

// Family roots rewritten by the branch's ordered sound changes, then its own coinages.
std::vector<Root> branch_lexicon(const LexiconFamily& family, const BranchSpec* spec) {
    std::vector<Root> roots;
    for (const Morpheme& morpheme : family.roots) {
        if (spec != nullptr && contains(spec->drop_roots, morpheme.slot)) continue;
        roots.push_back({morpheme.slot,
                         apply_sound_changes(morpheme.root,
                                             spec != nullptr ? spec->sound_changes
                                                             : std::span<const SoundChange>{})});
    }
    if (spec == nullptr) return roots;
    for (const Morpheme& coinage : spec->add_roots) {
        // A coinage naming a slot the family already holds REPLACES the shifted root
        // and keeps its position, exactly as dict.update does: `human_desert` has
        // `well: kanat`, not the shifted `fat`.
        auto existing = std::find_if(roots.begin(), roots.end(),
                                     [&](const Root& r) { return r.slot == coinage.slot; });
        if (existing != roots.end()) {
            existing->value = std::string(coinage.root);
        } else {
            roots.push_back({coinage.slot, std::string(coinage.root)});
        }
    }
    return roots;
}

Genome derive_genome(const Culture& culture, const Traits& traits, const Lexicon& lex,
                     std::string_view parent_race_id, std::string_view branch) {
    const LexiconFamily& family = family_for(lex, parent_race_id);
    const BranchSpec* spec = branch_for(family, branch);
    const PhonemeInventory phonemes = phoneme_inventory(traits);
    const tables::GenomeDerivation& rules = tables::genome_derivation();

    Genome genome;
    genome.family = family.name;
    genome.branch = std::string(branch);
    genome.inventory.consonants = phonemes.consonants;
    genome.inventory.vowels = phonemes.vowels;
    genome.inventory.forbidden = phonemes.forbidden;
    for (std::string_view consonant : phonemes.consonants) {
        if (!contains(tables::word_onset_excludes(), consonant)) {
            genome.inventory.onsets.push_back(consonant);
        }
    }
    if (spec != nullptr) {
        for (const SoundChange& change : spec->sound_changes) genome.sound_changes.push_back(change);
    }
    for (std::string_view form : lex.forbidden_forms) genome.forbidden_forms.push_back(form);
    genome.lexicon = branch_lexicon(family, spec);

    genome.phonotactics.syllable = apply(rules.syllable, traits);
    genome.phonotactics.vowel_length = apply(rules.vowel_length, traits);
    genome.phonotactics.hiatus = apply(rules.hiatus, traits);
    genome.phonotactics.geminates = apply(rules.geminates, traits);
    const auto [onset_max, coda_max] = template_shape(genome.phonotactics.syllable);
    genome.phonotactics.max_onset_cluster = onset_max;
    genome.phonotactics.max_coda_cluster = coda_max;
    auto [onsets, codas] = clusters(genome.inventory.consonants, onset_max, coda_max);
    genome.phonotactics.onset_clusters = onsets;
    genome.phonotactics.coda_clusters = codas;
    std::vector<std::string> allowed = onsets;
    allowed.insert(allowed.end(), codas.begin(), codas.end());
    genome.phonotactics.allowed_clusters = sorted_unique(std::move(allowed));

    genome.prosody.stress = apply(rules.stress, traits);
    genome.prosody.tone = apply(rules.tone, traits);
    genome.prosody.word_shape = apply(rules.word_shape, traits);

    genome.morphology.compounding = apply(rules.compounding, traits);
    genome.morphology.honorific = apply(rules.honorific, traits);
    genome.morphology.call_form = apply(rules.call_form, traits);
    genome.morphology.word_order = family.word_order;
    genome.morphology.affixing = family.affixing;
    const auto pattern = culture.at("household", "naming_pattern");
    for (const tables::PatronymicRow& row : tables::patronymic_table()) {
        if (row.naming_pattern == pattern) genome.morphology.patronymic = row.patronymic;
    }
    if (genome.morphology.patronymic.empty()) {
        fault("no patronymic form for naming pattern " + quoted(pattern));
    }

    genome.orthography.script_form = apply(rules.script_form, traits);
    genome.orthography.direction = apply(rules.direction, traits);
    for (std::string_view consonant : genome.inventory.consonants) {
        if (consonant.size() > 1) genome.orthography.digraphs.push_back(consonant);
    }

    genome.rates.third_syllable = apply(rules.third_syllable, traits);
    genome.rates.cluster = apply(rules.cluster, traits);
    genome.rates.loanword = apply(rules.loanword, traits);
    return genome;
}

// ---------------------------------------------------------------------------
// Overrides
// ---------------------------------------------------------------------------

std::vector<std::string_view> patch_inventory(const std::vector<std::string_view>& derived,
                                              std::span<const std::string_view> add,
                                              std::span<const std::string_view> drop,
                                              const std::string& where) {
    std::vector<std::string_view> overlap;
    for (std::string_view token : add) {
        if (contains(drop, token)) overlap.push_back(token);
    }
    std::sort(overlap.begin(), overlap.end());
    if (!overlap.empty()) {
        fault(where + " both adds and drops phoneme " + quoted(overlap.front()));
    }
    const Alphabet letters = alphabet();
    const auto in_alphabet = [&](std::string_view token) {
        return contains(letters.consonants, token) || contains(letters.vowels, token);
    };
    for (std::span<const std::string_view> list : {add, drop}) {
        for (std::string_view token : list) {
            if (!in_alphabet(token)) {
                fault(where + " names phoneme " + quoted(token) + " outside the alphabet");
            }
        }
    }
    for (std::string_view token : drop) {
        if (!contains(derived, token)) {
            fault(where + " drops phoneme " + quoted(token) + " the derivation never produced");
        }
    }
    for (std::string_view token : add) {
        if (contains(derived, token)) {
            fault(where + " adds phoneme " + quoted(token) + " the derivation already produced");
        }
    }
    std::set<std::string_view> kept(derived.begin(), derived.end());
    for (std::string_view token : add) kept.insert(token);
    for (std::string_view token : drop) kept.erase(token);
    // Re-emitted in ALPHABET order, discarding both the derived list's order and the
    // patch's: a tidekin `lh` lands between `l` and `r`, not at the end.
    std::vector<std::string_view> out;
    for (std::string_view token : letters.consonants) {
        if (kept.count(token) != 0) out.push_back(token);
    }
    for (std::string_view token : letters.vowels) {
        if (kept.count(token) != 0) out.push_back(token);
    }
    return out;
}

void apply_genome_patch(Genome& genome, const tables::GenomePatch& patch, std::string_view label) {
    const std::string where = "override for " + std::string(label) + " at " +
                              std::string(patch.block) + "." + std::string(patch.key);
    const auto restates = [&] { fault("override for " + std::string(label) +
                                      " restates the derived value at " + std::string(patch.block) +
                                      "." + std::string(patch.key)); };
    const auto wrong_kind = [&](std::string_view wanted) {
        fault(where + " must be " + std::string(wanted));
    };

    if (patch.block == "inventory") {
        std::vector<std::string_view>* target = nullptr;
        if (patch.key == "consonants") target = &genome.inventory.consonants;
        else if (patch.key == "vowels") target = &genome.inventory.vowels;
        else if (patch.key == "forbidden") target = &genome.inventory.forbidden;
        else if (patch.key == "onsets") target = &genome.inventory.onsets;
        else fault(where + " names no key the inventory has");
        if (patch.kind != tables::GenomePatch::Kind::Inventory) {
            // A tightening: the reference would have let a bare string replace the
            // consonant list outright, which corrupts every name the tongue can build.
            wrong_kind("an add/drop patch; a phoneme list cannot be replaced wholesale");
        }
        *target = patch_inventory(*target, patch.add, patch.drop, where);
        return;
    }

    const auto set_text = [&](std::string_view& field) {
        if (patch.kind != tables::GenomePatch::Kind::Text) wrong_kind("a string");
        if (field == patch.text) restates();
        field = patch.text;
    };
    const auto set_bool = [&](bool& field) {
        if (patch.kind != tables::GenomePatch::Kind::Boolean) wrong_kind("a boolean");
        if (field == patch.boolean) restates();
        field = patch.boolean;
    };
    const auto set_number = [&](Number& field) {
        if (patch.kind != tables::GenomePatch::Kind::Number) wrong_kind("a number");
        if (field.canon == patch.number.canon) restates();
        field = patch.number;
    };

    if (patch.block == "phonotactics") {
        if (patch.key == "syllable") return set_text(genome.phonotactics.syllable);
        if (patch.key == "vowel_length") return set_text(genome.phonotactics.vowel_length);
        if (patch.key == "hiatus") return set_text(genome.phonotactics.hiatus);
        if (patch.key == "geminates") return set_bool(genome.phonotactics.geminates);
    } else if (patch.block == "prosody") {
        if (patch.key == "stress") return set_text(genome.prosody.stress);
        if (patch.key == "tone") return set_text(genome.prosody.tone);
        if (patch.key == "word_shape") return set_text(genome.prosody.word_shape);
    } else if (patch.block == "morphology") {
        if (patch.key == "compounding") return set_text(genome.morphology.compounding);
        if (patch.key == "honorific") return set_text(genome.morphology.honorific);
        if (patch.key == "call_form") return set_bool(genome.morphology.call_form);
        if (patch.key == "word_order") return set_text(genome.morphology.word_order);
        if (patch.key == "affixing") return set_text(genome.morphology.affixing);
        if (patch.key == "patronymic") return set_text(genome.morphology.patronymic);
    } else if (patch.block == "orthography") {
        if (patch.key == "script_form") return set_text(genome.orthography.script_form);
        if (patch.key == "direction") return set_text(genome.orthography.direction);
    } else if (patch.block == "rates") {
        if (patch.key == "third_syllable") return set_number(genome.rates.third_syllable);
        if (patch.key == "cluster") return set_number(genome.rates.cluster);
        if (patch.key == "loanword") return set_number(genome.rates.loanword);
    }
    // The computed keys — the cluster lists, the digraphs — are deliberately not
    // patchable: they are a function of the inventory and the template, so patching one
    // would disagree with the inventory that produced it.
    fault(where + " names no genome field an override may reach");
}

// ---------------------------------------------------------------------------
// The resolved people as a value tree, for the canonical document
// ---------------------------------------------------------------------------

Value traits_value(const Traits& traits) {
    std::vector<Field> fields;
    for (std::size_t i = 0; i < axes().size(); ++i) {
        const auto& spec = axes()[i];
        if (spec.list_valued) {
            fields.emplace_back(spec.name, text_array(traits.craft_focus));
        } else {
            fields.emplace_back(spec.name, text_value(traits.values[i]));
        }
    }
    return object_value(std::move(fields));
}

Value culture_value(const Culture& culture) {
    std::vector<Field> blocks;
    const auto keys = culture_keys();
    for (std::size_t i = 0; i < keys.size(); ++i) {
        auto block = std::find_if(blocks.begin(), blocks.end(),
                                  [&](const Field& f) { return f.first == keys[i].block; });
        if (block == blocks.end()) {
            blocks.emplace_back(keys[i].block, object_value({}));
            block = std::prev(blocks.end());
        }
        block->second.fields.emplace_back(keys[i].key, text_value(culture.values[i]));
    }
    return object_value(std::move(blocks));
}

Value genome_value(const Genome& genome) {
    std::vector<Value> changes;
    for (const SoundChange& change : genome.sound_changes) {
        changes.push_back(array_value({text_value(change.old_form), text_value(change.new_form)}));
    }
    std::vector<Field> roots;
    for (const Root& root : genome.lexicon) roots.emplace_back(root.slot, text_value(root.value));

    return object_value({
        {"family", text_value(genome.family)},
        {"branch", text_value(genome.branch)},
        {"inventory", object_value({{"consonants", text_array(genome.inventory.consonants)},
                                    {"vowels", text_array(genome.inventory.vowels)},
                                    {"forbidden", text_array(genome.inventory.forbidden)},
                                    {"onsets", text_array(genome.inventory.onsets)}})},
        {"sound_changes", array_value(std::move(changes))},
        {"forbidden_forms", text_array(genome.forbidden_forms)},
        {"lexicon", object_value(std::move(roots))},
        {"phonotactics",
         object_value({{"syllable", text_value(genome.phonotactics.syllable)},
                       {"vowel_length", text_value(genome.phonotactics.vowel_length)},
                       {"hiatus", text_value(genome.phonotactics.hiatus)},
                       {"geminates", boolean_value(genome.phonotactics.geminates)},
                       {"max_onset_cluster", integer_value(genome.phonotactics.max_onset_cluster)},
                       {"max_coda_cluster", integer_value(genome.phonotactics.max_coda_cluster)},
                       {"onset_clusters", text_array(genome.phonotactics.onset_clusters)},
                       {"coda_clusters", text_array(genome.phonotactics.coda_clusters)},
                       {"allowed_clusters", text_array(genome.phonotactics.allowed_clusters)}})},
        {"prosody", object_value({{"stress", text_value(genome.prosody.stress)},
                                  {"tone", text_value(genome.prosody.tone)},
                                  {"word_shape", text_value(genome.prosody.word_shape)}})},
        {"morphology", object_value({{"compounding", text_value(genome.morphology.compounding)},
                                     {"honorific", text_value(genome.morphology.honorific)},
                                     {"call_form", boolean_value(genome.morphology.call_form)},
                                     {"word_order", text_value(genome.morphology.word_order)},
                                     {"affixing", text_value(genome.morphology.affixing)},
                                     {"patronymic", text_value(genome.morphology.patronymic)}})},
        {"orthography", object_value({{"script_form", text_value(genome.orthography.script_form)},
                                      {"direction", text_value(genome.orthography.direction)},
                                      {"digraphs", text_array(genome.orthography.digraphs)}})},
        {"rates", object_value({{"third_syllable", number_value(genome.rates.third_syllable)},
                                {"cluster", number_value(genome.rates.cluster)},
                                {"loanword", number_value(genome.rates.loanword)}})},
    });
}

Value resolved_value(const Resolved& people) {
    std::vector<Value> overridden;
    for (const std::string& path : people.provenance.overridden) {
        Value v;
        v.kind = Value::Kind::Text;
        v.text = path;  // borrows from `people`, which outlives the emission below
        overridden.push_back(v);
    }
    const Revisions& r = people.provenance.revisions;
    return object_value({
        {"id", text_value(people.id)},
        {"parent_race_id", text_value(people.parent_race_id)},
        {"traits", traits_value(people.traits)},
        {"appearance", people.appearance},
        {"culture", culture_value(people.culture)},
        {"genome", genome_value(people.genome)},
        {"provenance",
         object_value({{"revisions", object_value({{"traits", integer_value(r.traits)},
                                                   {"derivation", integer_value(r.derivation)},
                                                   {"lexicon", integer_value(r.lexicon)},
                                                   {"overrides", integer_value(r.overrides)},
                                                   {"appearance", integer_value(r.appearance)}})},
                       {"overridden", array_value(std::move(overridden))}})},
    });
}

// ---------------------------------------------------------------------------
// Naming internals
// ---------------------------------------------------------------------------

// The five plain vowel LETTERS, not the vowel phonemes: a seam is judged on the
// characters either side of it, so `aa` counts through its first `a`.
bool is_vowel_letter(char c) {
    return tables::name_draw().vowel_letters.find(c) != std::string_view::npos;
}

// Smooth a compound seam under the tongue's own rules. THE THREE RULES ARE ORDERED and
// the order is load-bearing: geminate, then hiatus, then digraph seam. Reordering them
// changes names.
std::string join(std::string_view first, std::string_view second, const Genome& genome) {
    if (first.empty()) return std::string(second);
    if (second.empty()) return std::string(first);
    const char last = first.back();
    const char next = second.front();
    if (last == next) {
        // A doubled letter across a seam is a geminate; tongues that lack them simplify.
        return genome.phonotactics.geminates ? std::string(first) + std::string(second)
                                             : std::string(first) + std::string(second.substr(1));
    }
    if (is_vowel_letter(last) && is_vowel_letter(next) &&
        genome.phonotactics.hiatus == "forbidden") {
        return std::string(first.substr(0, first.size() - 1)) + std::string(second);
    }
    // A seam can spell a digraph its two halves never contained: `ulk` + `hakn` reads as
    // `ul-khakn`, inventing a `kh` neither root has and mis-teaching the reader where
    // the compound breaks. Drop the first root's final consonant rather than let the
    // seam lie. The length guard stops a two-letter element being eaten down to one.
    const std::string seam{last, next};
    if (is_digraph(seam) && first.size() > 2) {
        return std::string(first.substr(0, first.size() - 1)) + std::string(second);
    }
    return std::string(first) + std::string(second);
}

// Exactly ONE random() call, whichever template it lands on.
const NameTemplate& weighted(std::span<const NameTemplate> templates, Rng& draw) {
    double total = 0.0;
    for (const NameTemplate& t : templates) total += t.weight;
    const double cut = draw.random() * total;
    double running = 0.0;
    for (const NameTemplate& t : templates) {
        running += t.weight;
        if (cut < running) return t;
    }
    return templates.back();
}

// Reject a form that reads as an English word nobody meant to ship. Not fastidiousness:
// a prototype generated `Person` for a cold human from `per` + `son`, and `Mordor` for a
// hill dwarf from `copper` + `gate`. The check is a SUBSTRING match because the
// compound, not the root, is where the accident happens.
bool acceptable(std::string_view word, const std::vector<std::string_view>& forms) {
    const std::string lowered = lowercased(word);
    for (std::string_view bad : forms) {
        if (lowered.find(bad) != std::string::npos) return false;
    }
    return true;
}

std::vector<std::string_view> all_slots(const Genome& genome) {
    std::vector<std::string_view> slots;
    for (const Root& root : genome.lexicon) slots.push_back(root.slot);
    std::sort(slots.begin(), slots.end());
    return slots;
}

// The slots this role admits that this branch's lexicon actually holds, in slot_roles
// document order. Every caller sorts before drawing; without that the name would depend
// on JSON key order.
std::vector<std::string_view> role_options(const Genome& genome, const Lexicon& lex,
                                           std::string_view role) {
    const SlotRole* spec = lex.role(role);
    if (spec == nullptr) fault("lexicon has no slot role " + quoted(role));
    std::vector<std::string_view> options;
    for (std::string_view slot : spec->slots) {
        if (genome.has_root(slot)) options.push_back(slot);
    }
    return options;
}

std::string compose(const Genome& genome, std::span<const std::string_view> slots) {
    std::string word;
    for (std::string_view slot : slots) word = join(word, *genome.root(slot), genome);
    return capitalized(word);
}

std::string_view nucleus(const Genome& genome, Rng& draw) {
    const tables::NameDraw& numbers = tables::name_draw();
    std::vector<std::string_view> simple;
    std::vector<std::string_view> complex_;
    for (std::string_view vowel : genome.inventory.vowels) {
        (vowel.size() == 1 ? simple : complex_).push_back(vowel);
    }
    if (simple.empty()) simple = genome.inventory.vowels;
    double share = numbers.nucleus_share_base;
    if (genome.phonotactics.vowel_length == "contrastive") share = numbers.nucleus_share_contrastive;
    // The hiatus test runs second and WINS; it is a second unconditional test, not an
    // else. The complex share is raised only where the genome says the speakers hold
    // vowels, because drawing uniformly from the whole vowel set gave elvish
    // `Paemuayuan` — every nucleus a diphthong, which no tongue does.
    if (genome.phonotactics.hiatus == "preferred") share = numbers.nucleus_share_hiatus;
    if (!complex_.empty() && draw.random() < share) return draw.choice(complex_);
    return draw.choice(simple);
}

enum class CodaMode { None, Optional, Required };

std::string syllable(const Genome& genome, Rng& draw, bool allow_onset_cluster, CodaMode mode) {
    const tables::NameDraw& numbers = tables::name_draw();
    const double rate = genome.rates.cluster.value;
    const auto& onsets = genome.phonotactics.onset_clusters;
    const auto& codas = genome.phonotactics.coda_clusters;

    std::string part;
    if (allow_onset_cluster && !onsets.empty() && draw.random() < rate) {
        part = draw.choice(onsets);
    } else {
        const auto& source =
            genome.inventory.onsets.empty() ? genome.inventory.consonants : genome.inventory.onsets;
        part = std::string(draw.choice(source));
    }
    part += nucleus(genome, draw);
    if (mode == CodaMode::None) return part;
    if (mode == CodaMode::Optional && draw.random() >= numbers.optional_coda_skip) return part;
    if (!codas.empty() && draw.random() < rate) return part + draw.choice(codas);
    std::vector<std::string_view> short_consonants;
    for (std::string_view consonant : genome.inventory.consonants) {
        if (consonant.size() <= 2) short_consonants.push_back(consonant);
    }
    return part + std::string(draw.choice(short_consonants));
}

std::string draw_person(const Resolved& resolved, Rng& draw) {
    const Genome& genome = resolved.genome;
    const tables::NameDraw& numbers = tables::name_draw();
    const auto shape = genome.prosody.word_shape;
    int count = 0;
    if (shape == "monosyllabic_heavy") count = numbers.monosyllabic_heavy;
    else if (shape == "disyllabic") count = numbers.disyllabic;
    else if (shape == "trisyllabic_flowing") count = numbers.trisyllabic_flowing;
    else fault("no syllable count for word shape " + quoted(shape));
    if (draw.random() < genome.rates.third_syllable.value) ++count;

    const bool onset_cluster = genome.phonotactics.max_onset_cluster > 1;
    const bool closes = genome.phonotactics.max_coda_cluster > 0;
    std::string word;
    for (int index = 0; index < count; ++index) {
        const bool last = index == count - 1;
        // Only the final syllable closes, so a medial coda never meets the next onset
        // and builds a cluster the tongue does not permit. A one-syllable name must
        // close, or a heavy monosyllabic tongue produces bare `Na`.
        CodaMode mode = CodaMode::Optional;
        if (!last || !closes) mode = CodaMode::None;
        else if (count == 1 || shape == "monosyllabic_heavy") mode = CodaMode::Required;
        // A cluster onset only on syllable 0: a medial `br-` or `st-` is a form the
        // reference never coins, and permitting one also changes the draw count.
        word = join(word, syllable(genome, draw, onset_cluster && index == 0, mode), genome);
    }
    // A sound change is a property of the tongue and not of the inherited vocabulary: a
    // speaker who cannot say `th` cannot say it in a new name either.
    return capitalized(apply_sound_changes(word, genome.sound_changes));
}

// The two elements of a dithematic name, or empty when the branch cannot fill the drawn
// template. Unlike settlement naming there is NO fallback to the whole root table: a
// personal name is two meaning-bearing elements or it is nothing.
std::vector<std::string_view> pick_elements(const Resolved& resolved, const Lexicon& lex,
                                            Rng& draw) {
    const NameTemplate& chosen = weighted(lex.personal_templates, draw);
    std::vector<std::string_view> slots;
    for (std::string_view role : chosen.pattern) {
        std::vector<std::string_view> options = role_options(resolved.genome, lex, role);
        if (options.empty()) return {};
        std::sort(options.begin(), options.end());
        slots.push_back(draw.choice(options));
    }
    // A personal name never repeats one element.
    if (slots[0] == slots[1]) return {};
    return slots;
}

// ---------------------------------------------------------------------------
// The stock
// ---------------------------------------------------------------------------

struct StockCandidate {
    std::string name;
    std::string gloss;
    int weight = 0;
    std::array<std::string_view, 2> slots{};
};

// The ordered role pairs the authored templates leave unspent. Both authored sets are
// removed: pairs already spent on personal names are what COMMON means, and pairs spent
// on settlements would make a person read as a town.
std::vector<std::array<std::string_view, 2>> rare_patterns(const Lexicon& lex) {
    std::vector<std::string_view> roles;
    for (const SlotRole& role : lex.slot_roles) roles.push_back(role.role);
    std::sort(roles.begin(), roles.end());

    std::set<std::pair<std::string_view, std::string_view>> spent;
    for (std::span<const NameTemplate> list : {lex.personal_templates, lex.settlement_templates}) {
        for (const NameTemplate& t : list) spent.emplace(t.pattern[0], t.pattern[1]);
    }
    std::vector<std::array<std::string_view, 2>> out;
    for (std::string_view first : roles) {
        for (std::string_view second : roles) {
            if (first == second) continue;
            if (spent.count({first, second}) != 0) continue;
            out.push_back({first, second});
        }
    }
    return out;
}

// Every name those patterns admit here, keyed on the FINISHED NAME because composition
// collides: two different root pairs can join to one string once the seam rules have
// had their say. The first reading wins and the highest weight wins, so an entry can
// carry a weight from one pairing and a gloss from another — that is deliberate, and a
// name a common template can reach is a common name however else it is also reachable.
std::vector<StockCandidate> enumerate_names(
    const Resolved& resolved, const Lexicon& lex,
    const std::vector<std::pair<std::array<std::string_view, 2>, int>>& patterns) {
    std::vector<StockCandidate> found;
    std::map<std::string, std::size_t> at;
    for (const auto& [pattern, weight] : patterns) {
        std::vector<std::string_view> heads = role_options(resolved.genome, lex, pattern[0]);
        std::vector<std::string_view> tails = role_options(resolved.genome, lex, pattern[1]);
        std::sort(heads.begin(), heads.end());
        std::sort(tails.begin(), tails.end());
        for (std::string_view first : heads) {
            for (std::string_view second : tails) {
                if (first == second) continue;
                const std::array<std::string_view, 2> slots{first, second};
                std::string name = compose(resolved.genome, slots);
                if (!acceptable(name, resolved.genome.forbidden_forms)) continue;
                const auto seen = at.find(name);
                if (seen == at.end()) {
                    at.emplace(name, found.size());
                    found.push_back({std::move(name),
                                     std::string(first) + "-" + std::string(second), weight, slots});
                } else if (weight > found[seen->second].weight) {
                    found[seen->second].weight = weight;
                }
            }
        }
    }
    return found;
}

// Authored weight, then length, then spelling. The key is TOTAL over distinct names, so
// std::sort's instability cannot reach the result — do not "fix" this to stable_sort and
// do not drop a tie-breaker, because the stock has to be identical in every world and
// every process.
void order_names(std::vector<StockCandidate>& found) {
    std::sort(found.begin(), found.end(), [](const StockCandidate& a, const StockCandidate& b) {
        if (a.weight != b.weight) return a.weight > b.weight;
        if (a.name.size() != b.name.size()) return a.name.size() < b.name.size();
        return a.name < b.name;
    });
}

// Commonest first, with the head spread across the space rather than bunched in it.
// Ranking on length alone put the whole heartland head on one root — `Eldsel`, `Frosel`,
// `Nersel`, `Versel` — and the elves on `-naur` four times over, which is worse than the
// uniform draw it replaces. What is skipped is not discarded: it falls to the body in
// its own rank order.
std::vector<StockEntry> ranked(std::vector<StockCandidate> found) {
    order_names(found);
    const auto spread = static_cast<std::size_t>(tables::name_draw().head_spread);
    std::vector<StockEntry> head;
    std::vector<StockEntry> body;
    std::set<std::string_view> spent;
    for (const StockCandidate& entry : found) {
        const bool fresh = spent.count(entry.slots[0]) == 0 && spent.count(entry.slots[1]) == 0;
        if (head.size() < spread && fresh) {
            spent.insert(entry.slots[0]);
            spent.insert(entry.slots[1]);
            head.push_back({entry.name, entry.gloss});
        } else {
            body.push_back({entry.name, entry.gloss});
        }
    }
    if (head.size() < std::min(spread, found.size())) {
        // Too inbred to fill the head with root-disjoint names: the rank order stands
        // unaltered rather than the head running short.
        std::vector<StockEntry> plain;
        for (const StockCandidate& entry : found) plain.push_back({entry.name, entry.gloss});
        return plain;
    }
    head.insert(head.end(), body.begin(), body.end());
    return head;
}

// The tabulated head, then a plain 1/r tail, NORMALISED AT THE END so a stock smaller
// than the head table still sums to one rather than silently dropping its last ranks
// below the last threshold.
std::vector<double> thresholds(std::size_t count) {
    if (count == 0) return {};
    const tables::NameDraw& numbers = tables::name_draw();
    std::vector<double> shares;
    for (std::size_t i = 0; i < count && i < numbers.head_shares.size(); ++i) {
        shares.push_back(numbers.head_shares[i]);
    }
    const std::size_t rest = count - shares.size();
    if (rest > 0) {
        double remainder = 1.0;
        for (double share : shares) remainder -= share;
        double harmonic = 0.0;
        for (std::size_t rank = 1; rank <= rest; ++rank) {
            harmonic += 1.0 / std::pow(static_cast<double>(rank), numbers.zipf_exponent);
        }
        for (std::size_t rank = 1; rank <= rest; ++rank) {
            shares.push_back(remainder / std::pow(static_cast<double>(rank), numbers.zipf_exponent) /
                             harmonic);
        }
    }
    double total = 0.0;
    for (double share : shares) total += share;
    double running = 0.0;
    std::vector<double> cumulative;
    for (double share : shares) {
        running += share / total;
        cumulative.push_back(running);
    }
    return cumulative;
}

// ---------------------------------------------------------------------------
// Resolution, memoised
// ---------------------------------------------------------------------------

Resolved resolve_uncached(std::string_view civ_id, std::string_view parent_race_id) {
    const tables::TraitRow* race = trait_row(tables::trait_races(), parent_race_id);
    if (race == nullptr) {
        fault("unknown parent race " + quoted(parent_race_id) +
              "; a parent race selects the language family, so there is no default to "
              "guess — leave the name empty instead");
    }
    const std::string label = "people " + quoted(civ_id);

    Resolved people;
    people.id = std::string(civ_id);
    people.parent_race_id = std::string(parent_race_id);
    // An unlisted civilization resolves to its parent base. That is what lets a new
    // people arrive with a plausible culture and language already attached.
    people.traits = merge_traits(base_traits(*race), trait_row(tables::trait_peoples(), civ_id),
                                 label);
    people.appearance =
        merge_appearance(body_for(tables::appearance_races(), parent_race_id, true, "race"),
                         body_for(tables::appearance_peoples(), civ_id, false, "people"), label);
    check_resolved_appearance(people.appearance, people.traits, label);

    people.culture = derive_culture(people.traits);
    for (const tables::CultureOverrideRow& row : tables::culture_overrides()) {
        if (row.civ_id != civ_id) continue;
        for (const tables::CulturePatch& patch : row.patches) {
            const CultureKey& key = culture_keys()[static_cast<std::size_t>(patch.key)];
            if (people.culture.values[static_cast<std::size_t>(patch.key)] == patch.value) {
                fault("override for " + std::string(civ_id) +
                      " restates the derived value at " + std::string(key.block) + "." +
                      std::string(key.key));
            }
            people.culture.values[static_cast<std::size_t>(patch.key)] = patch.value;
            people.provenance.overridden.push_back("culture." + std::string(key.block) + "." +
                                                   std::string(key.key));
        }
    }

    // The branch IS the civ id, even when the family has no such branch.
    people.genome = derive_genome(people.culture, people.traits, lexicon(), parent_race_id, civ_id);
    for (const tables::GenomeOverrideRow& row : tables::genome_overrides()) {
        if (row.civ_id != civ_id) continue;
        for (const tables::GenomePatch& patch : row.patches) {
            apply_genome_patch(people.genome, patch, civ_id);
            people.provenance.overridden.push_back("genome." + std::string(patch.block) + "." +
                                                   std::string(patch.key));
        }
    }

    people.provenance.revisions = revisions();
    return people;
}

}  // namespace

// ---------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------

const Value* Value::find(std::string_view key) const {
    for (const Field& field : fields) {
        if (field.first == key) return &field.second;
    }
    return nullptr;
}

const Value& Value::at(std::string_view key) const {
    const Value* found = find(key);
    if (found == nullptr) fault("appearance has no key " + quoted(key));
    return *found;
}

double Value::as_double() const {
    if (kind == Kind::Integer) return static_cast<double>(integer);
    if (kind == Kind::Number) return number.value;
    fault("appearance value is not a number");
}

bool operator==(const Value& a, const Value& b) {
    const bool a_numeric = a.kind == Value::Kind::Integer || a.kind == Value::Kind::Number;
    const bool b_numeric = b.kind == Value::Kind::Integer || b.kind == Value::Kind::Number;
    // `1` and `1.0` compare equal, as they do in the reference — the delta merge's
    // "restates its inherited value" rule is about the quantity, not the spelling.
    if (a_numeric && b_numeric) return a.as_double() == b.as_double();
    if (a.kind != b.kind) return false;
    switch (a.kind) {
        case Value::Kind::Text: return a.text == b.text;
        case Value::Kind::Boolean: return a.boolean == b.boolean;
        case Value::Kind::Array: return a.items == b.items;
        case Value::Kind::Object: return a.fields == b.fields;
        default: return false;
    }
}

// ---------------------------------------------------------------------------
// Traits / Culture / Genome / Lexicon accessors
// ---------------------------------------------------------------------------

std::string_view Traits::axis(std::string_view name) const {
    const int index = axis_index(name);
    if (index < 0) fault("unknown heritage axis " + quoted(name));
    if (heritage::axes()[static_cast<std::size_t>(index)].list_valued) {
        fault("axis " + quoted(name) + " holds a list; read craft_focus directly");
    }
    return values[static_cast<std::size_t>(index)];
}

std::string_view Culture::at(std::string_view block, std::string_view key) const {
    const auto keys = culture_keys();
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].block == block && keys[i].key == key) return values[i];
    }
    fault("culture has no key " + std::string(block) + "." + std::string(key));
}

const std::string* Genome::root(std::string_view slot) const {
    for (const Root& entry : lexicon) {
        if (entry.slot == slot) return &entry.value;
    }
    return nullptr;
}

const SlotRole* Lexicon::role(std::string_view name) const {
    for (const SlotRole& entry : slot_roles) {
        if (entry.role == name) return &entry;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Phonemes
// ---------------------------------------------------------------------------

PhonemeInventory phoneme_inventory(const Traits& traits) {
    std::set<std::string_view> kept;
    std::set<std::string_view> banned;
    for (std::string_view token : tables::core_consonants()) kept.insert(token);
    for (std::string_view token : tables::core_vowels()) kept.insert(token);
    for (const tables::Gate& gate : tables::gates()) {
        if (traits.values[static_cast<std::size_t>(gate.axis)] != gate.value) continue;
        for (std::string_view token : gate.add) kept.insert(token);
        for (std::string_view token : gate.drop) banned.insert(token);
    }
    for (std::string_view token : banned) kept.erase(token);

    const Alphabet letters = alphabet();
    PhonemeInventory out;
    for (std::string_view token : letters.consonants) {
        if (kept.count(token) != 0) out.consonants.push_back(token);
        if (banned.count(token) != 0) out.forbidden.push_back(token);
    }
    for (std::string_view token : letters.vowels) {
        if (kept.count(token) != 0) out.vowels.push_back(token);
        if (banned.count(token) != 0) out.forbidden.push_back(token);
    }
    return out;
}

std::string apply_sound_changes(std::string_view word, std::span<const SoundChange> changes) {
    std::string current(word);
    for (const SoundChange& change : changes) {
        std::string out;
        std::size_t index = 0;
        while (index < current.size()) {
            const std::string_view pair =
                std::string_view(current).substr(index, std::min<std::size_t>(2, current.size() - index));
            // Consume a digraph whole, unless the rule names that digraph: a rule for
            // `s` must not fire inside `sh`, but gnomish `aa -> a` must still fire on
            // `aa`.
            if (pair.size() == 2 && is_digraph(pair) && change.old_form != pair) {
                out += pair;
                index += 2;
                continue;
            }
            if (current.compare(index, change.old_form.size(), change.old_form) == 0) {
                out += change.new_form;
                index += change.old_form.size();
                continue;
            }
            out += current[index];
            ++index;
        }
        current = std::move(out);
    }
    return current;
}

// ---------------------------------------------------------------------------
// Resolution
// ---------------------------------------------------------------------------

Value race_appearance(std::string_view parent_race_id) {
    return body_for(tables::appearance_races(), parent_race_id, /*required=*/true, "race");
}

Resolved resolve(std::string_view civ_id, std::string_view parent_race_id) {
    // Memoised, and the entry point hands back a COPY: a caller that mutates a resolved
    // people must not poison what the next caller sees. Nothing in the library calls this from
    // a second thread -- the one pool, the stage-16 planners' (workers.hpp), never reaches it
    // -- which is the only reason it has no lock. Two worlds generated at once on two of a
    // host's threads would race here.
    static std::map<std::pair<std::string, std::string>, Resolved> cache;
    const std::pair<std::string, std::string> key{std::string(civ_id), std::string(parent_race_id)};
    const auto found = cache.find(key);
    if (found != cache.end()) return found->second;
    Resolved people = resolve_uncached(civ_id, parent_race_id);
    cache.emplace(key, people);
    return people;
}

std::string canonical_document(std::span<const std::pair<std::string, std::string>> pairs) {
    std::vector<std::pair<std::string, std::string>> sorted(pairs.begin(), pairs.end());
    std::sort(sorted.begin(), sorted.end());
    std::vector<Resolved> peoples;
    peoples.reserve(sorted.size());
    for (const auto& [civ_id, parent] : sorted) peoples.push_back(resolve(civ_id, parent));

    std::vector<Field> fields;
    for (const Resolved& people : peoples) fields.emplace_back(people.id, resolved_value(people));
    const Value document = object_value(std::move(fields));
    std::string out;
    emit_value(out, document);
    return out;
}

Identity heritage_identity(std::span<const std::pair<std::string, std::string>> pairs) {
    return {revisions(), sha256_hex(canonical_document(pairs))};
}

// ---------------------------------------------------------------------------
// Naming
// ---------------------------------------------------------------------------

std::pair<std::string, std::string> settlement_name(const Resolved& resolved, const Lexicon& lex,
                                                    Rng& draw, std::string_view terrain_slot) {
    std::string word;
    std::vector<std::string_view> chosen;
    for (int attempt = 0; attempt < tables::name_draw().redraw_limit; ++attempt) {
        const NameTemplate& chosen_template = weighted(lex.settlement_templates, draw);
        chosen.clear();
        for (std::size_t index = 0; index < chosen_template.pattern.size(); ++index) {
            const std::string_view role = chosen_template.pattern[index];
            std::vector<std::string_view> options = role_options(resolved.genome, lex, role);
            if (options.empty()) options = all_slots(resolved.genome);
            // The terrain bias, and the one place a name is not drawn: the membership
            // test runs against the unsorted list, which is harmless.
            if (index == 0 && role == "terrain" && !terrain_slot.empty() &&
                contains(options, terrain_slot)) {
                chosen.push_back(terrain_slot);
                continue;
            }
            std::sort(options.begin(), options.end());
            chosen.push_back(draw.choice(options));
        }
        word.clear();
        for (std::string_view slot : chosen) word = join(word, *resolved.genome.root(slot), resolved.genome);
        if (acceptable(word, resolved.genome.forbidden_forms)) break;
    }
    // Bounded, so naming always terminates. Falling out of the loop returns the LAST
    // attempt even though it failed the filter; reaching there means the lexicon is
    // small enough that the filter cannot be satisfied, which is an authoring problem
    // rather than a runtime one.
    std::string gloss;
    for (std::size_t i = 0; i < chosen.size(); ++i) {
        if (i != 0) gloss += '-';
        gloss += std::string(chosen[i]);
    }
    return {capitalized(word), gloss};
}

std::pair<std::string, std::string> person_name_with_gloss(const Resolved& resolved,
                                                           const Lexicon& lex, Rng& draw) {
    const std::vector<std::string_view> slots = pick_elements(resolved, lex, draw);
    if (slots.empty()) {
        // An EMPTY gloss, not a two-slot one: a caller splitting on '-' gets one empty
        // field and must cope with it.
        return {draw_person(resolved, draw), ""};
    }
    return {compose(resolved.genome, slots),
            std::string(slots[0]) + "-" + std::string(slots[1])};
}

std::string person_name(const Resolved& resolved, const Lexicon& lex, Rng& draw) {
    std::string candidate;
    for (int attempt = 0; attempt < tables::name_draw().redraw_limit; ++attempt) {
        const std::vector<std::string_view> slots = pick_elements(resolved, lex, draw);
        candidate = slots.empty() ? std::string() : compose(resolved.genome, slots);
        if (!candidate.empty() && acceptable(candidate, resolved.genome.forbidden_forms)) {
            return candidate;
        }
    }
    return candidate.empty() ? draw_person(resolved, draw) : candidate;
}

std::string person_name(const Resolved& resolved, Rng& draw) {
    std::string candidate;
    for (int attempt = 0; attempt < tables::name_draw().redraw_limit; ++attempt) {
        candidate = draw_person(resolved, draw);
        if (!candidate.empty() && acceptable(candidate, resolved.genome.forbidden_forms)) {
            return candidate;
        }
    }
    return candidate.empty() ? draw_person(resolved, draw) : candidate;
}

std::string realm_name(const Resolved& resolved, std::string_view capital_name) {
    const std::string* root = resolved.genome.root("realm");
    if (root == nullptr || root->empty() || capital_name.empty()) return "";
    // Whether a doubled letter survives is the tongue's decision and not this
    // function's: `Bor` + `rik` is `Borik` in gnomish and hill-dwarvish, which have no
    // geminates, and `Borrik` in dwarvish and frostholder, whose stone-hall and
    // wind-scoured acoustics do.
    return capitalized(join(lowercased(capital_name), *root, resolved.genome));
}

NameTable name_table(const Resolved& resolved) {
    const Genome& genome = resolved.genome;
    const tables::NameDraw& numbers = tables::name_draw();
    const auto& consonants = genome.inventory.consonants;
    const auto& vowels = genome.inventory.vowels;

    std::vector<std::string> onsets;
    for (std::size_t c = 0; c < consonants.size() && c < static_cast<std::size_t>(numbers.name_table_consonants); ++c) {
        for (std::size_t v = 0; v < vowels.size() && v < static_cast<std::size_t>(numbers.name_table_vowels); ++v) {
            onsets.push_back(std::string(consonants[c]) + std::string(vowels[v]));
        }
    }
    std::vector<std::string> codas;
    for (std::string_view consonant : consonants) {
        if (consonant.size() <= 2) codas.push_back(std::string(consonant));
    }
    if (genome.phonotactics.hiatus == "preferred") {
        // Two are appended and the deduplicating sort collapses them to ONE, so an open
        // syllable is about one entry in twenty-two rather than twice as likely. The
        // reference's own comment claims otherwise; the code is what ships.
        codas.push_back("");
        codas.push_back("");
    }
    return {sorted_unique(std::move(onsets)), vowels, sorted_unique(std::move(codas)),
            genome.rates.third_syllable.value};
}

NameStock name_stock(const Resolved& resolved, const Lexicon& lex) {
    std::vector<std::pair<std::array<std::string_view, 2>, int>> common_patterns;
    for (const NameTemplate& t : lex.personal_templates) {
        common_patterns.emplace_back(t.pattern, t.weight);
    }
    std::vector<std::pair<std::array<std::string_view, 2>, int>> tail_patterns;
    for (const auto& pattern : rare_patterns(lex)) tail_patterns.emplace_back(pattern, 1);

    NameStock stock;
    stock.common = ranked(enumerate_names(resolved, lex, common_patterns));
    std::vector<StockEntry> rare = ranked(enumerate_names(resolved, lex, tail_patterns));
    std::set<std::string> spoken;
    for (const StockEntry& entry : stock.common) spoken.insert(entry.name);
    // A rare pairing that composes to a name the common stock already holds is not rare.
    for (StockEntry& entry : rare) {
        if (spoken.count(entry.name) == 0) stock.rare.push_back(std::move(entry));
    }
    stock.thresholds = thresholds(stock.common.size());
    return stock;
}

std::optional<StockEntry> stock_name(const NameStock& stock, Rng& draw) {
    // TWO DRAWS WHATEVER THE OUTCOME, so the stream a caller sees does not depend on
    // which branch was taken and adding the rare tail cannot desynchronise a replay
    // from itself. Both are spent up front for exactly that reason.
    const double roll = draw.random();
    const double cut = draw.random();
    if (stock.common.empty()) {
        // No common stock means no personal template this family's roots can fill. The
        // rare pairings may still compose, but promoting them would make the tail the
        // whole tongue and hide the authoring fault behind names that look fine.
        return std::nullopt;
    }
    if (!stock.rare.empty() && roll < tables::name_draw().rare_share) {
        const auto index = std::min(static_cast<std::size_t>(cut * static_cast<double>(stock.rare.size())),
                                    stock.rare.size() - 1);
        return stock.rare[index];
    }
    for (std::size_t index = 0; index < stock.thresholds.size(); ++index) {
        if (cut < stock.thresholds[index]) return stock.common[index];
    }
    return stock.common.back();
}

}  // namespace fwg::heritage
