#include "serialize_document.hpp"

#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fwg/civilization_report.hpp"
#include "fwg/civilizations.hpp"
#include "fwg/controls.hpp"
#include "fwg/debug_stats.hpp"
#include "fwg/entities.hpp"
#include "fwg/founding.hpp"
#include "fwg/heritage.hpp"
#include "fwg/options.hpp"
#include "serialize_blocks.hpp"

namespace fwg::serialize_detail {
namespace {

// ===========================================================================
// Primitives. As in serialize_blocks.cpp: every double goes through `number`, which writes
// null for NaN and infinity, and every string goes in as a string_view.
// ===========================================================================

void number(JsonWriter& json, double v) {
    if (std::isfinite(v)) {
        json.value(v);
    } else {
        json.value_null();
    }
}

void real(JsonWriter& json, std::string_view key, double v) {
    json.key(key);
    number(json, v);
}

void integer(JsonWriter& json, std::string_view key, long long v) {
    json.key(key);
    json.value(v);
}

void text(JsonWriter& json, std::string_view key, std::string_view v) {
    json.key(key);
    json.value(v);
}

// The C++ spells the reference's None as an empty string.
void text_or_null(JsonWriter& json, std::string_view key, std::string_view v) {
    json.key(key);
    if (v.empty()) {
        json.value_null();
    } else {
        json.value(v);
    }
}

void flag(JsonWriter& json, std::string_view key, bool v) {
    json.key(key);
    json.value(v);
}

void vec3(JsonWriter& json, const Vec3& v) {
    json.begin_array();
    number(json, v.x);
    number(json, v.y);
    number(json, v.z);
    json.end_array();
}

void rgb(JsonWriter& json, std::string_view key, const Rgb& c) {
    json.key(key);
    json.begin_array();
    json.value(c.r);
    json.value(c.g);
    json.value(c.b);
    json.end_array();
}

// ===========================================================================
// recipe
// ===========================================================================

std::string_view type_name(ControlType type) {
    switch (type) {
        case ControlType::Number: return "number";
        case ControlType::Integer: return "integer";
        case ControlType::String: return "string";
    }
    return "number";
}

bool integral(std::string_view name) {
    const ControlSpec* spec = control_spec(name);
    return spec != nullptr && spec->type == ControlType::Integer;
}

// A control's value as its published type says: an integer control is written whole.
void control_number(JsonWriter& json, std::string_view name, double v) {
    if (integral(name) && std::isfinite(v)) {
        json.value(static_cast<long long>(std::llround(v)));
    } else {
        number(json, v);
    }
}

// The reference's `world_options`: json.dumps of every explicitly set option -- the
// request's own and the ones a region request biased -- with sorted keys and Python's
// default separators. An integer option is written whole; a number option in the shortest
// round-trip form, so an option set to 1.0 reads `1` where the reference would echo `1.0`.
std::string world_options_text(const Config& cfg) {
    std::string out = "{";
    bool first = true;
    const auto specs = option_specs();  // sorted by name, which is json.dumps' sort_keys order
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const Option option = static_cast<Option>(i);
        if (!cfg.options.overridden(option)) continue;
        if (!first) out += ", ";
        first = false;
        out += '"';
        out += specs[i].name;
        out += "\": ";
        const double v = cfg.options[option];
        out += specs[i].is_integer ? std::to_string(static_cast<long long>(std::llround(v))) : format_double(v);
    }
    out += '}';
    return out;
}

// The seven reference Config fields this port has no field for, at the values recipe 3
// gives them (terrain_lab.Config and terrain_world.default_config(3), FantasyWorldGenerator
// b67c99b): ley_nodes, ley_width and magic_instability (the pre-network leyline model) and
// extent, depth, width and meander (the retired non-tectonic experiment). Nothing on recipe 3
// reads them, and the reference's registry marks all seven inactive and publishes none as a
// control, so no request can move them -- but its Config carries them, and `config`,
// `effective_config` and `recipe.resolved` are that dataclass, so all three publish them.
constexpr int kInactiveLeyNodes = 10;
constexpr double kInactiveLeyWidth = 180.0;
constexpr double kInactiveMagicInstability = 0.45;
constexpr double kInactiveExtent = 8000.0;
constexpr double kInactiveDepth = 500.0;
constexpr double kInactiveWidth = 450.0;
constexpr double kInactiveMeander = 650.0;

}  // namespace

// Every Config field the reference's dataclass has, in its declaration order. `effective`
// scales the four inactive lengths by world_scale, as the reference's apply_world_scale
// scales extent, depth, width and meander with the six lengths this port does carry.
void write_config_fields(JsonWriter& json, const Config& cfg, bool effective) {
    const double scale = effective ? cfg.world_scale : 1.0;
    integer(json, "world_recipe", cfg.world_recipe);
    text(json, "world_options", world_options_text(cfg));
    text(json, "world_size", to_string(cfg.world_size));
    integer(json, "auto_parameters", cfg.auto_parameters);
    text(json, "population_profile", cfg.population_profile);
    // Recipe 3 is a tectonic globe by construction; the port has no other shape.
    text(json, "shape", "globe");
    real(json, "globe_radius", cfg.globe_radius);
    integer(json, "tectonics", 1);
    integer(json, "phase", cfg.phase);
    integer(json, "magic_enabled", cfg.magic_enabled);
    integer(json, "ley_nodes", kInactiveLeyNodes);
    real(json, "ley_width", kInactiveLeyWidth);
    real(json, "magic_instability", kInactiveMagicInstability);
    real(json, "human_magic_limit", cfg.human_magic_limit);
    integer(json, "college_count", cfg.college_count);
    integer(json, "hamlets_per_core", cfg.hamlets_per_core);
    integer(json, "fortress_count", cfg.fortress_count);
    real(json, "support_reach", cfg.support_reach);
    real(json, "culture_link_cost", cfg.culture_link_cost);
    real(json, "urban_food_demand", cfg.urban_food_demand);
    real(json, "human_adaptation", cfg.human_adaptation);
    real(json, "wind_bearing", cfg.wind_bearing);
    integer(json, "rain_passes", cfg.rain_passes);
    real(json, "rain_strength", cfg.rain_strength);
    integer(json, "settlement_count", cfg.settlement_count);
    real(json, "settlement_spacing", cfg.settlement_spacing);
    real(json, "stubbornness", cfg.stubbornness);
    real(json, "road_max_grade", cfg.road_max_grade);
    real(json, "bridge_cost", cfg.bridge_cost);
    real(json, "river_threshold_km2", cfg.river_threshold_km2);
    integer(json, "erosion_passes", cfg.erosion_passes);
    real(json, "erosion_strength", cfg.erosion_strength);
    real(json, "temperature_offset", cfg.temperature_offset);
    real(json, "moisture_bias", cfg.moisture_bias);
    integer(json, "plate_count", cfg.plate_count);
    integer(json, "layout_variation", cfg.layout_variation);
    integer(json, "detail_variation", cfg.detail_variation);
    real(json, "crust_bias", cfg.crust_bias);
    real(json, "belt_width", cfg.belt_width);
    real(json, "tectonic_relief", cfg.tectonic_relief);
    real(json, "mountain_detail", cfg.mountain_detail);
    real(json, "orogeny", cfg.orogeny);
    real(json, "sea_level", cfg.sea_level);
    real(json, "world_scale", cfg.world_scale);
    integer(json, "seed", static_cast<long long>(cfg.seed));
    integer(json, "size", cfg.size);
    real(json, "extent", kInactiveExtent * scale);
    real(json, "amplitude", cfg.amplitude);
    real(json, "wavelength", cfg.wavelength);
    integer(json, "octaves", cfg.octaves);
    real(json, "ridge", cfg.ridge);
    real(json, "depth", kInactiveDepth * scale);
    real(json, "width", kInactiveWidth * scale);
    real(json, "meander", kInactiveMeander * scale);
    real(json, "radius", cfg.radius);
}

namespace {

// `recipe.resolved`: the Config fields, then every world option.
void write_resolved(JsonWriter& json, const Config& cfg) {
    json.begin_object();
    write_config_fields(json, cfg, /*effective=*/false);
    const auto specs = option_specs();
    for (std::size_t i = 0; i < specs.size(); ++i) {
        json.key(specs[i].name);
        const double v = cfg.options[static_cast<Option>(i)];
        if (specs[i].is_integer) {
            json.value(static_cast<long long>(std::llround(v)));
        } else {
            number(json, v);
        }
    }
    json.end_object();
}

void write_parameter(JsonWriter& json, const ControlSpec& c) {
    json.begin_object();
    json.key("default");
    if (c.type == ControlType::String) {
        json.value(c.default_text);
    } else if (c.type == ControlType::Integer) {
        json.value(static_cast<long long>(std::llround(c.default_number)));
    } else {
        number(json, c.default_number);
    }
    text(json, "group", c.group);
    text(json, "description", c.description);
    text(json, "type", type_name(c.type));
    text(json, "units", c.units);
    if (!c.choices.empty()) {
        json.key("choices");
        json.begin_array();
        for (const std::string_view choice : c.choices) json.value(choice);
        json.end_array();
    }
    if (!c.choice_labels.empty()) {
        json.key("choice_labels");
        json.begin_object();
        for (const ControlChoiceLabel& label : c.choice_labels) text(json, label.id, label.label);
        json.end_object();
    }
    if (c.has_min) {
        json.key("min");
        control_number(json, c.name, c.min_value);
    }
    if (c.has_max) {
        json.key("max");
        control_number(json, c.name, c.max_value);
    }
    json.end_object();
}

// ===========================================================================
// civilizations
// ===========================================================================

void write_heritage(JsonWriter& json, const heritage::Resolved& people) {
    json.begin_object();

    json.key("traits");
    json.begin_object();
    const auto axes = heritage::axes();
    for (std::size_t i = 0; i < axes.size(); ++i) {
        json.key(axes[i].name);
        if (axes[i].list_valued) {
            json.begin_array();
            for (const std::string_view craft : people.traits.craft_focus) json.value(craft);
            json.end_array();
        } else {
            json.value(people.traits.values[i]);
        }
    }
    json.end_object();

    json.key("culture");
    json.begin_object();
    const auto keys = heritage::culture_keys();
    for (std::size_t i = 0; i < keys.size();) {
        const std::string_view block = keys[i].block;
        json.key(block);
        json.begin_object();
        for (; i < keys.size() && keys[i].block == block; ++i) text(json, keys[i].key, people.culture.values[i]);
        json.end_object();
    }
    json.end_object();

    const heritage::Genome& g = people.genome;
    const auto list = [&](std::string_view key, const auto& items) {
        json.key(key);
        json.begin_array();
        for (const auto& item : items) json.value(std::string_view(item));
        json.end_array();
    };
    json.key("genome");
    json.begin_object();
    text(json, "family", g.family);
    text(json, "branch", g.branch);
    json.key("inventory");
    json.begin_object();
    list("consonants", g.inventory.consonants);
    list("vowels", g.inventory.vowels);
    list("forbidden", g.inventory.forbidden);
    list("onsets", g.inventory.onsets);
    json.end_object();
    json.key("sound_changes");
    json.begin_array();
    for (const heritage::SoundChange& change : g.sound_changes) {
        json.begin_array();
        json.value(change.old_form);
        json.value(change.new_form);
        json.end_array();
    }
    json.end_array();
    list("forbidden_forms", g.forbidden_forms);
    json.key("lexicon");
    json.begin_object();
    for (const heritage::Root& root : g.lexicon) text(json, root.slot, root.value);
    json.end_object();
    json.key("phonotactics");
    json.begin_object();
    text(json, "syllable", g.phonotactics.syllable);
    text(json, "vowel_length", g.phonotactics.vowel_length);
    text(json, "hiatus", g.phonotactics.hiatus);
    flag(json, "geminates", g.phonotactics.geminates);
    integer(json, "max_onset_cluster", g.phonotactics.max_onset_cluster);
    integer(json, "max_coda_cluster", g.phonotactics.max_coda_cluster);
    list("onset_clusters", g.phonotactics.onset_clusters);
    list("coda_clusters", g.phonotactics.coda_clusters);
    list("allowed_clusters", g.phonotactics.allowed_clusters);
    json.end_object();
    json.key("prosody");
    json.begin_object();
    text(json, "stress", g.prosody.stress);
    text(json, "tone", g.prosody.tone);
    text(json, "word_shape", g.prosody.word_shape);
    json.end_object();
    json.key("morphology");
    json.begin_object();
    text(json, "compounding", g.morphology.compounding);
    text(json, "honorific", g.morphology.honorific);
    flag(json, "call_form", g.morphology.call_form);
    text(json, "word_order", g.morphology.word_order);
    text(json, "affixing", g.morphology.affixing);
    text(json, "patronymic", g.morphology.patronymic);
    json.end_object();
    json.key("orthography");
    json.begin_object();
    text(json, "script_form", g.orthography.script_form);
    text(json, "direction", g.orthography.direction);
    list("digraphs", g.orthography.digraphs);
    json.end_object();
    json.key("rates");
    json.begin_object();
    real(json, "third_syllable", g.rates.third_syllable.value);
    real(json, "cluster", g.rates.cluster.value);
    real(json, "loanword", g.rates.loanword.value);
    json.end_object();
    json.end_object();

    json.key("provenance");
    json.begin_object();
    const heritage::Revisions& r = people.provenance.revisions;
    json.key("revisions");
    json.begin_object();
    integer(json, "traits", r.traits);
    integer(json, "derivation", r.derivation);
    integer(json, "lexicon", r.lexicon);
    integer(json, "overrides", r.overrides);
    integer(json, "appearance", r.appearance);
    json.end_object();
    list("overridden", people.provenance.overridden);
    json.end_object();

    json.end_object();
}

// ===========================================================================
// world_scene
// ===========================================================================

// The reference tags every entry with its settlement kind and ONE join key -- city_uid,
// hamlet_id or fortress_id. The last two are ordinals that renumber on an age advance, so
// this port also writes the key that does not: hamlet_uid, fortress_uid. A city's join key
// is already its stable uid.
void scene_owner(JsonWriter& json, const SceneOwner& owner) {
    text(json, "settlement_kind", to_string(owner.kind));
    switch (owner.kind) {
        case SceneSettlementKind::City:
            text(json, "city_uid", owner.id);
            break;
        case SceneSettlementKind::Hamlet:
            text(json, "hamlet_id", owner.id);
            text(json, "hamlet_uid", owner.uid);
            break;
        case SceneSettlementKind::Castle:
            text(json, "fortress_id", owner.id);
            text(json, "fortress_uid", owner.uid);
            break;
    }
}

void positions(JsonWriter& json, std::string_view key, const std::vector<Vec3>& points) {
    json.key(key);
    json.begin_array();
    for (const Vec3& p : points) vec3(json, p);
    json.end_array();
}

// ===========================================================================
// debug_stats
// ===========================================================================

void debug_group(JsonWriter& json, const DebugGroup& g) {
    json.begin_object();
    text(json, "id", g.id);
    integer(json, "cities", g.cities);
    integer(json, "urban_population", g.urban_population);
    integer(json, "workers", g.workers);
    integer(json, "beds", g.beds);
    json.end_object();
}

void counts(JsonWriter& json, const std::vector<std::pair<std::string, long long>>& rows) {
    json.begin_object();
    for (const auto& [key, value] : rows) integer(json, key, value);
    json.end_object();
}

void distribution(JsonWriter& json, const NetworkDistribution& d) {
    json.begin_object();
    integer(json, "version", d.version);
    text(json, "model", "landscape_alignments");
    json.key("aligned_nodes");
    json.begin_array();
    for (const int a : d.aligned_nodes) json.value(a);
    json.end_array();
    integer(json, "clusters", d.clusters);
    real(json, "spread", d.spread);
    real(json, "outlier_fraction", d.outlier_fraction);
    real(json, "extra_link_probability", d.extra_link_probability);
    json.end_object();
}

}  // namespace

// ===========================================================================
// Entry points
// ===========================================================================

void write_phases(JsonWriter& json, const World& world) {
    json.begin_object();
    integer(json, "completed", world.completed_stage);
    json.key("seeds");
    json.begin_object();
    // A string, as the top-level `seeds.layout` is: this port's layout seed is 64-bit, and
    // most JSON readers hold numbers as doubles. The reference's is 32-bit and an integer.
    text(json, "layout", std::to_string(world.layout_seed));
    integer(json, "crust", static_cast<long long>(world.crust_seed));
    integer(json, "detail", static_cast<long long>(world.detail_seed));
    json.end_object();
    json.key("plates");
    json.begin_array();
    for (const Plate& plate : world.plates) {
        json.begin_object();
        integer(json, "id", plate.id);
        json.key("center");
        vec3(json, plate.center);
        json.key("omega");
        vec3(json, plate.omega);
        json.end_object();
    }
    json.end_array();
    text(json, "motion_units", "dimensionless angular velocity");
    text(json, "default_layer", world.config.phase == 1 ? "plates" : "height");
    integer(json, "version", 2);
    json.key("titles");
    json.begin_array();
    for (const std::string_view title : stage_titles()) json.value(title);
    json.end_array();
    json.end_object();
}

void write_recipe(JsonWriter& json, const World& world) {
    const Config& cfg = world.config;
    json.begin_object();
    integer(json, "version", 3);
    integer(json, "seed", static_cast<long long>(cfg.seed));

    // What the request set, as it set it -- not every resolved value that differs from its
    // default, which would list the radius a world_size preset derived as if someone had
    // asked for it.
    json.key("overrides");
    json.begin_object();
    for (const Config::RequestedOverride& o : cfg.requested_overrides) {
        json.key(o.name);
        if (o.is_text) {
            json.value(std::string_view(o.text));
        } else {
            control_number(json, o.name, o.number);
        }
    }
    json.end_object();

    json.key("resolved");
    write_resolved(json, cfg);

    json.key("parameters");
    json.begin_object();
    for (const ControlSpec& c : control_specs()) {
        json.key(c.name);
        write_parameter(json, c);
    }
    json.end_object();

    // Every published control's source: the request's own overrides, the region-request
    // biases, the world_size preset's radius and the request's seed, as the reference labels
    // them. The preset label follows the reference's test exactly -- globe_radius not
    // overridden and a preset other than small -- including when circumference_km, not the
    // preset, set the width.
    const auto requested = [&](std::string_view name) {
        for (const Config::RequestedOverride& o : cfg.requested_overrides) {
            if (o.name == name) return true;
        }
        return false;
    };
    json.key("provenance");
    json.begin_object();
    for (const ControlSpec& c : control_specs()) {
        std::string source = requested(c.name) ? "override" : "default";
        for (const Config::OptionBias& bias : cfg.option_biases) {
            if (bias.target == c.name) {
                source = bias.zone + " request biases suitable conditions; placement remains conditional";
            }
        }
        if (c.name == "globe_radius" && !requested("globe_radius") && cfg.world_size != WorldSize::Small) {
            source = "world_size preset";
        }
        if (c.name == "seed") source = "request";
        text(json, c.name, source);
    }
    json.end_object();

    json.key("biases");
    json.begin_object();
    for (const Config::OptionBias& bias : cfg.option_biases) {
        json.key(bias.target);
        json.begin_object();
        json.key("value");
        if (const auto option = option_by_name(bias.target)) {
            number(json, cfg.options[*option]);
        } else if (bias.target == "moisture_bias") {
            number(json, cfg.moisture_bias);
        } else {
            json.value_null();
        }
        text(json, "source", bias.zone + " request biases suitable conditions; placement remains conditional");
        json.end_object();
    }
    json.end_object();
    json.end_object();
}

void write_population(JsonWriter& json, const World& world) {
    const CivilizationProfile* profile = civilization_profile(world.config.population_profile);
    if (profile == nullptr) {
        json.value_null();  // validate() refuses such a config, so no generated world has one
        return;
    }
    write_profile_record(json, *profile);
}

void write_population_profiles(JsonWriter& json) {
    json.begin_array();
    for (const CivilizationProfile& p : civilization_profiles()) {
        json.begin_object();
        text(json, "id", p.id);
        text(json, "name", p.name);
        json.end_object();
    }
    json.end_array();
}

void write_population_budget(JsonWriter& json, const World& world) {
    const PopulationBudget& b = world.settlements.budget;
    const auto per_people = [&](std::string_view key, const auto& values) {
        json.key(key);
        json.begin_object();
        for (const auto& [people, value] : values) {
            json.key(people);
            if constexpr (std::is_floating_point_v<std::decay_t<decltype(value)>>) {
                number(json, value);
            } else {
                json.value(static_cast<long long>(value));
            }
        }
        json.end_object();
    };
    json.begin_object();
    integer(json, "version", 4);
    integer(json, "world_cap", b.world_capacity);
    per_people("allowances", b.allowances);
    per_people("shares", b.shares);
    per_people("weighted_habitat_km2", b.weighted_habitat_km2);
    per_people("requested_cities", b.requested_cities);
    json.key("calibration");
    json.begin_object();
    // routing.cpp life_capacity: areas / 1e6 * 100 * the best people's potential.
    integer(json, "residents_per_productive_km2", 100);
    per_people("minimum_city_region_residents", b.minimum_city_region_residents);
    integer(json, "spacing_city_ceiling", b.ceiling.packing);
    real(json, "habitable_km2", b.ceiling.habitable_km2);
    // This port's additions: the ceiling's own arithmetic and the limit that actually bound
    // founding. The reference computes all three and publishes none.
    real(json, "per_city_km2", b.ceiling.per_city_km2);
    real(json, "resolvable_spacing_m", b.ceiling.resolvable_spacing_m);
    integer(json, "city_limit", b.city_limit);
    json.end_object();
    // The reference's prose, less the sentence it appends on the full recipe about marine
    // habitat: this port's capacity has no marine term (habitation.cpp integrates farmed
    // ground only), so that sentence would describe a computation it does not make.
    text(json, "method",
         "Capacity integrates area-weighted farming potential, climate, freshwater/irrigation, slope, biome and "
         "mutation penalties. Dwarven productive hinterland may extend beyond mineral uplands only over safe terrain "
         "routes within support reach. Overlapping peoples split each cell capacity. Provisional 100 residents per "
         "fully productive km2, not validated agricultural yields; access and trade can reduce realized support. "
         "Cities require habitat footprint and their civilization-specific minimum regional residents; no species "
         "count or majority guarantee. Diaspora can waive a city-count footprint quota for an unused civilization "
         "with its required supported residents, once per parent; habitat and spacing still apply. Unsettled "
         "capacity remains unused.");
    per_people("placed_cities", b.placed_cities);
    integer(json, "allocated", b.allocated);
    integer(json, "unallocated", b.unallocated);
    json.end_object();
}

void write_peoples(JsonWriter& json, const World& world) {
    // One full profile per people that competed for the ground: the budget's share keys.
    json.begin_object();
    for (const auto& [people, share] : world.settlements.budget.shares) {
        (void)share;
        const CivilizationProfile* profile = civilization_profile(people);
        if (profile == nullptr) continue;
        json.key(people);
        write_profile_record(json, *profile);
    }
    json.end_object();
}

void write_civilizations(JsonWriter& json, const World& world) {
    const std::vector<SettlementSite>& sites = world.settlements.sites;
    const std::span<const std::string_view> ids = civilization_ids();

    json.begin_object();
    integer(json, "version", kCivilizationReportVersion);

    // This port's own registry digest, over the bytes it actually carries: the one a world
    // records and the NPC roster already cites. The reference's digest of its own registry
    // file is `fwg registry`'s source_sha256.
    const RegistryIdentity identity = registry_identity();
    json.key("registry");
    json.begin_object();
    integer(json, "schema_version", identity.schema_version);
    integer(json, "revision", identity.revision);
    text(json, "sha256", identity.sha256);
    json.end_object();

    std::vector<std::pair<std::string, std::string>> pairs;
    for (const std::string_view id : ids) {
        const EntityRules* rules = entity_rule(id);
        pairs.emplace_back(std::string(id), rules != nullptr ? std::string(rules->parent_race_id) : std::string());
    }
    const heritage::Identity heritage_id = heritage::heritage_identity(pairs);
    json.key("heritage");
    json.begin_object();
    json.key("revision");
    json.begin_object();
    integer(json, "traits", heritage_id.revisions.traits);
    integer(json, "derivation", heritage_id.revisions.derivation);
    integer(json, "lexicon", heritage_id.revisions.lexicon);
    integer(json, "overrides", heritage_id.revisions.overrides);
    integer(json, "appearance", heritage_id.revisions.appearance);
    json.end_object();
    text(json, "sha256", heritage_id.sha256);
    json.end_object();

    real(json, "medium_suitability_min", registry_defaults().medium_suitability_min);
    text(json, "capital_scope", capital_scope());

    json.key("parent_races");
    json.begin_object();
    for (const ParentRace& race : parent_races()) {
        json.key(race.id);
        json.begin_object();
        text(json, "name", race.name);
        real(json, "founding_participation_percent", race.founding_participation_percent);
        json.end_object();
    }
    json.end_object();

    json.key("presentation_defaults");
    json.begin_object();
    for (const PresentationDefault& d : presentation_defaults()) rgb(json, d.key, d.color);
    json.end_object();

    // One row per entity in registry order, used or not: region_index is its position.
    json.key("entities");
    json.begin_array();
    for (std::size_t index = 0; index < ids.size(); ++index) {
        const std::string_view id = ids[index];
        const CivilizationProfile* profile = civilization_profile(id);
        const EntityRules* rules = entity_rule(id);
        if (profile == nullptr || rules == nullptr) continue;
        json.begin_object();
        text(json, "id", id);
        integer(json, "region_index", static_cast<long long>(index));
        text(json, "name", profile->name);
        // The profile's civilization identity, splatted into the row as the reference does.
        text(json, "kind", profile->is_entity ? "entity" : "aggregate");
        text_or_null(json, "inspiration", profile->inspiration);
        text(json, "environment", profile->environment);
        json.key("habitat");
        write_habitat(json, profile->habitat, civilization_habitat_pools(), 0);
        text_or_null(json, "allocation_group", profile->allocation_group);
        integer(json, "priority", profile->priority);
        text_or_null(json, "structure_blocks", profile->structure_blocks);
        json.key("presentation");
        json.begin_object();
        rgb(json, "map_color_rgb", rules->presentation.map_color);
        rgb(json, "wall_color_rgb", rules->presentation.wall_color);
        rgb(json, "roof_color_rgb", rules->presentation.roof_color);
        flag(json, "outpost_colors", entity_outpost_colors(id).value_or(false));
        json.end_object();
        text(json, "parent_race_id", rules->parent_race_id);
        json.key("heritage");
        write_heritage(json, heritage::resolve(id, rules->parent_race_id));
        long long cities = 0;
        std::optional<int> capital;
        for (const SettlementSite& s : sites) {
            if (s.population_profile != id) continue;
            ++cities;
            if (!capital && s.city_class == CityClass::Capital) capital = s.node;
        }
        integer(json, "city_count", cities);
        json.key("capital_node");
        if (capital) {
            json.value(*capital);
        } else {
            json.value_null();
        }
        json.end_object();
    }
    json.end_array();
    json.end_object();
}

void write_world_scene(JsonWriter& json, const WorldScene& scene) {
    json.begin_object();
    integer(json, "version", scene.version);
    text(json, "unit", scene.unit);
    text(json, "coordinates", scene.coordinates);
    real(json, "radius_m", scene.radius_m);
    // The definition the height field actually applied; the same as the world's own
    // terrain_detail block on every world that has one (world_scene.hpp).
    json.key("terrain_detail");
    write_terrain_detail(json, scene.terrain_detail);

    json.key("buildings");
    json.begin_array();
    for (const SceneBuilding& b : scene.buildings) {
        json.begin_object();
        text(json, "id", b.id);
        scene_owner(json, b.owner);
        text(json, "plot_id", b.plot_id);
        text(json, "asset_id", b.asset_id);
        json.key("position_m");
        vec3(json, b.position_m);
        json.key("up");
        vec3(json, b.up);
        json.key("width_axis");
        vec3(json, b.width_axis);
        json.key("depth_axis");
        vec3(json, b.depth_axis);
        real(json, "rotation_degrees", b.rotation_degrees);
        json.key("dimensions_m");
        json.begin_object();
        real(json, "width", b.dimensions_m.width);
        real(json, "depth", b.dimensions_m.depth);
        real(json, "height", b.dimensions_m.height);
        json.end_object();
        json.key("footprint_positions_m");
        json.begin_array();
        for (const Vec3& corner : b.footprint_positions_m) vec3(json, corner);
        json.end_array();
        json.end_object();
    }
    json.end_array();

    json.key("streets");
    json.begin_array();
    for (const SceneStreet& s : scene.streets) {
        json.begin_object();
        text(json, "id", s.id);
        scene_owner(json, s.owner);
        // Only an approach street belongs to a junction.
        if (!s.junction_id.empty()) text(json, "junction_id", s.junction_id);
        positions(json, "positions_m", s.positions_m);
        json.end_object();
    }
    json.end_array();

    json.key("junctions");
    json.begin_array();
    for (const SceneJunction& j : scene.junctions) {
        json.begin_object();
        text(json, "id", j.id);
        scene_owner(json, j.owner);
        text(json, "route_id", j.route_id);
        json.key("position_m");
        vec3(json, j.position_m);
        text(json, "status", j.status);
        json.end_object();
    }
    json.end_array();

    json.key("regional_roads");
    json.begin_array();
    for (const SceneRegionalRoad& r : scene.regional_roads) {
        json.begin_object();
        text(json, "id", r.id);
        integer(json, "source_route_index", r.source_route_index);
        positions(json, "positions_m", r.positions_m);
        text_or_null(json, "from_junction", r.from_junction);
        text_or_null(json, "to_junction", r.to_junction);
        text(json, "from_status", r.from_status);
        text(json, "to_status", r.to_status);
        json.key("bridge_candidates");
        json.begin_array();
        for (const auto& [a, b] : r.bridge_candidates) {
            json.begin_array();
            json.value(a);
            json.value(b);
            json.end_array();
        }
        json.end_array();
        json.end_object();
    }
    json.end_array();
    text(json, "limits", scene.limits);
    json.end_object();
}

void write_debug_stats(JsonWriter& json, const World& world) {
    const DebugStats d = build_debug(world);
    json.begin_object();
    integer(json, "version", d.version);
    text(json, "scope", d.scope);

    json.key("parent_races");
    json.begin_array();
    for (const DebugGroup& g : d.parent_races) debug_group(json, g);
    json.end_array();
    json.key("civilizations");
    json.begin_array();
    for (const DebugGroup& g : d.civilizations) debug_group(json, g);
    json.end_array();

    json.key("cities");
    json.begin_array();
    for (const DebugCity& c : d.cities) {
        json.begin_object();
        text(json, "uid", c.uid);
        text(json, "name", c.name);
        text(json, "civilization", c.civilization);
        text(json, "parent_race", c.parent_race);
        integer(json, "urban_population_estimate", c.urban_population_estimate);
        json.key("nearest_city_m");
        if (c.nearest_city_m) {
            number(json, *c.nearest_city_m);
        } else {
            json.value_null();
        }
        // An unplanned city reads {} and [], as the reference's .get(..., {}) does.
        json.key("stats");
        if (c.planned) {
            write_city_plan_stats(json, c.stats);
        } else {
            json.begin_object();
            json.end_object();
        }
        json.key("placement");
        if (c.planned) {
            write_city_plan_debug(json, c.placement);
        } else {
            json.begin_object();
            json.end_object();
        }
        json.key("unplaced");
        write_plan_unplaced(json, c.unplaced);
        json.key("junctions");
        counts(json, c.junctions);
        json.end_object();
    }
    json.end_array();

    json.key("diaspora");
    json.begin_object();
    integer(json, "founded", d.diaspora_founded);
    integer(json, "surviving", d.diaspora_surviving);
    integer(json, "destroyed", d.diaspora_destroyed);
    json.end_object();

    json.key("founding_outcomes");
    counts(json, d.founding_outcomes);
    json.key("founding_events");
    json.begin_array();
    for (const FoundingEvent& e : d.founding_events) write_founding_event(json, e);
    json.end_array();

    json.key("ruins");
    write_ruin_list(json, d.ruins);

    json.key("timeline");
    json.begin_array();
    for (const DebugTimelineRow& row : d.timeline) {
        json.begin_object();
        integer(json, "stage", row.stage);
        text(json, "title", row.title);
        integer(json, "cities", row.cities);
        integer(json, "urban_population", row.urban_population);
        json.end_object();
    }
    json.end_array();

    json.key("leylines");
    json.begin_array();
    for (const DebugLeyline& l : d.leylines) {
        json.begin_object();
        text(json, "id", l.id);
        integer(json, "nodes", l.nodes);
        integer(json, "lines", l.lines);
        json.key("distribution");
        if (l.distribution) {
            distribution(json, *l.distribution);
        } else {
            json.value_null();
        }
        json.end_object();
    }
    json.end_array();

    json.key("biome_coverage");
    json.begin_array();
    for (const DebugBiomeCoverage& b : d.biome_coverage) {
        json.begin_object();
        integer(json, "id", b.id);
        text(json, "name", b.name);
        real(json, "percent_world", b.percent_world);
        json.end_object();
    }
    json.end_array();
    json.end_object();
}

void write_build_stages(JsonWriter& json, std::span<const BuildStage> stages) {
    json.begin_array();
    for (const BuildStage& s : stages) {
        json.begin_object();
        integer(json, "stage", s.stage);
        text(json, "title", s.title);
        json.key("layers");
        json.begin_object();
        for (const auto& [name, value] : s.layers) {
            json.key(name);
            json.raw(*value);
        }
        json.end_object();
        json.key("removed_layers");
        json.begin_array();
        for (const std::string& name : s.removed_layers) json.value(std::string_view(name));
        json.end_array();
        json.key("state");
        json.begin_object();
        for (const auto& [key, value] : s.state) {
            json.key(key);
            if (value) {
                json.raw(*value);
            } else {
                json.value_null();
            }
        }
        json.end_object();
        json.end_object();
    }
    json.end_array();
}

}  // namespace fwg::serialize_detail
