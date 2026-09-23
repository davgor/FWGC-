// The blocks that describe the world rather than one stage of it: phases, recipe, the
// peoples (population, population_profiles, population_budget, peoples, civilizations), the
// world scene, debug_stats, and the opt-in build_stages with materialize_stage.
//
// What is pinned, and why each is a separate question:
//
//   * PRESENCE follows the stage that writes each block: the four run-level blocks from
//     stage 1, the peoples' blocks from stage 10, the scene at 16, and build_stages only
//     when a recorder was attached.
//   * KEY NAMES are the reference's, read off its generated world (seed 42, size 65,
//     phase 16) rather than recalled, with this port's documented extras added by name.
//   * BYTES are a function of the seed, with and without the snapshots.
//   * SNAPSHOTS REPLAY: materialising stage N from a phase-16 document reproduces a phase-N
//     generation's layers and state keys exactly -- the reference's own test compares the
//     layers for stages 5-8 and the history for the last; this compares every stage and
//     every state key.
//   * debug_stats is checked against a hand-built world whose every figure is computed
//     here by hand, not against the code's own output.
#include "check.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <numbers>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "fwg/build_stages.hpp"
#include "fwg/civilization_report.hpp"
#include "fwg/controls.hpp"
#include "fwg/debug_stats.hpp"
#include "fwg/json.hpp"
#include "fwg/json_scan.hpp"
#include "fwg/serialize.hpp"
#include "fwg/sha256.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

// ===========================================================================
// Reading a document back: raw views over the writer's own text.
// ===========================================================================

struct View {
    std::string_view text;

    std::vector<json_scan::Member> members() const { return json_scan::object_members(text); }
    bool has(std::string_view key) const {
        for (const auto& m : members()) {
            if (m.key == key) return true;
        }
        return false;
    }
    View at(std::string_view key) const {
        for (const auto& m : members()) {
            if (m.key == key) return View{m.value};
        }
        check::fail("a member '" + std::string(key) + "' in " + std::string(text.substr(0, 120)));
    }
    std::vector<View> items() const {
        std::vector<View> out;
        for (const std::string_view item : json_scan::array_items(text)) out.push_back(View{item});
        return out;
    }
    std::set<std::string> keys() const {
        std::set<std::string> out;
        for (const auto& m : members()) out.insert(m.key);
        return out;
    }
    std::string str() const { return json_scan::string_value(text); }
    double num() const {
        double v = 0.0;
        const auto r = std::from_chars(text.data(), text.data() + text.size(), v);
        if (r.ec != std::errc() || r.ptr != text.data() + text.size()) {
            check::fail("a number, got " + std::string(text.substr(0, 60)));
        }
        return v;
    }
    long long integer() const { return json_scan::integer_value(text); }
    bool null() const { return json_scan::is_null(text); }
    bool boolean() const {
        if (text == "true") return true;
        if (text == "false") return false;
        check::fail("a boolean, got " + std::string(text.substr(0, 60)));
    }
};

std::string joined(const std::set<std::string>& keys) {
    std::string out;
    for (const std::string& k : keys) out += (out.empty() ? "" : ", ") + k;
    return out;
}

void same_keys(const View& record, const std::set<std::string>& expected, const std::string& what) {
    const std::set<std::string> actual = record.keys();
    std::set<std::string> missing;
    std::set<std::string> extra;
    for (const std::string& k : expected) {
        if (!actual.count(k)) missing.insert(k);
    }
    for (const std::string& k : actual) {
        if (!expected.count(k)) extra.insert(k);
    }
    if (!missing.empty() || !extra.empty()) {
        check::fail(what + ": missing {" + joined(missing) + "}, extra {" + joined(extra) + "}");
    }
}

// A document's top level, scanned once.
struct Document {
    std::string text;
    std::map<std::string, std::string_view> blocks;
    std::vector<std::string> order;

    explicit Document(std::string t) : text(std::move(t)) {
        for (const auto& m : json_scan::object_members(text)) {
            blocks.emplace(m.key, m.value);
            order.push_back(m.key);
        }
    }
    bool has(std::string_view key) const { return blocks.count(std::string(key)) != 0; }
    View at(std::string_view key) const {
        const auto it = blocks.find(std::string(key));
        if (it == blocks.end()) check::fail("the document to carry '" + std::string(key) + "'");
        return View{it->second};
    }
};

std::string write(const World& world, const std::vector<BuildStage>* stages = nullptr) {
    std::ostringstream out;
    DocumentOptions options;
    options.pretty = false;
    options.build_stages = stages;
    write_world_json(out, world, options);
    return out.str();
}

World phase_world(int phase, int size = 33) {
    Request request;
    request.seed = 42;
    request.size = size;
    request.phase = phase;
    return generate(resolve(request));
}

const World& world16() {
    static const World world = phase_world(16);
    return world;
}

const Document& doc16() {
    static const Document doc(write(world16()));
    return doc;
}

// A size-17 phase-16 world generated with the recorder attached: the snapshots, and the
// document that carries them.
struct Staged {
    World world;
    std::vector<BuildStage> stages;
    std::string document;
};

const Staged& staged() {
    static const Staged s = [] {
        Staged out;
        BuildStageRecorder recorder;
        Request request;
        request.seed = 42;
        request.size = 17;
        request.phase = 16;
        out.world = generate(resolve(request), [&](int stage, const World& w) { recorder.capture(stage, w); });
        out.stages = recorder.stages();
        out.document = write(out.world, &out.stages);
        return out;
    }();
    return s;
}

// The reference's validated profile: 38 traits, id, the civilization identity, the schema
// version and the digest.
const std::set<std::string>& profile_keys() {
    static const std::set<std::string> keys = {
        "biome_preferences", "civilization", "climate_weight", "college_flood_limit", "college_slope_limit",
        "college_suitability_min", "college_temperature_max", "college_temperature_min", "college_water_reach",
        "definition_hash", "description", "difficult_fraction", "flood_penalty", "food_biome_multipliers",
        "food_demand", "food_magic_biome_multipliers", "food_moisture_ideal", "food_slope_comfort",
        "food_temperature_ideal", "food_temperature_tolerance", "id", "irrigation", "land_per_city_km2",
        "magic_biome_preferences", "magic_penalty", "minimum_founding_residents", "moisture_ideal", "moisture_weight",
        "mutation_limit", "name", "resource_weight", "road_grade_limit", "schema_version", "site_slope_limit",
        "slope_comfort", "slope_weight", "support_multiplier", "temperature_ideal", "temperature_tolerance",
        "water_reach", "water_weight", "work_slope_limit"};
    return keys;
}

const std::set<std::string> kCivilizationIdentityKeys = {"allocation_group", "environment", "habitat",
                                                          "inspiration", "kind", "priority", "structure_blocks"};

}  // namespace

// ===========================================================================
// Presence
// ===========================================================================

TEST(a_phase_16_document_carries_every_block_the_reference_does) {
    const World& world = world16();
    check::is_true(world.has_settlements && world.has_world_scene, "a phase-16 world to have settled and a scene");
    const Document& doc = doc16();
    for (const char* key : {"phases", "recipe", "population", "population_profiles", "population_budget",
                            "peoples", "civilizations", "world_scene", "debug_stats"}) {
        check::is_true(doc.has(key), std::string("the phase-16 document to carry '") + key + "'");
    }
    // Opt-in, and nothing asked for it.
    check::is_true(!doc.has("build_stages"), "no build_stages unless a recorder was attached");
    // The budget is a top-level block now; the copy the settlements block used to carry
    // under the port's own names had no reader, and the reference has none there.
    check::is_true(!doc.at("settlements").has("population_budget"), "no population_budget inside settlements");
    // Each written once: a duplicated key would be a second, disagreeing copy.
    std::set<std::string> seen;
    for (const std::string& key : doc.order) check::is_true(seen.insert(key).second, "one '" + key + "' only");
}

TEST(the_run_level_blocks_exist_from_stage_1_and_the_peoples_from_stage_10) {
    const Document early(write(phase_world(1)));
    for (const char* key : {"phases", "recipe", "population", "population_profiles", "debug_stats"}) {
        check::is_true(early.has(key), std::string("a phase-1 document to carry '") + key + "'");
    }
    const Document nine(write(phase_world(9)));
    for (const char* key : {"population_budget", "peoples", "civilizations", "world_scene"}) {
        check::is_true(!nine.has(key), std::string("no '") + key + "' before stage 10");
    }
    const World thirteen = phase_world(13);
    check::is_true(thirteen.has_settlements && !thirteen.has_world_scene, "phase 13 to have cities and no scene");
    const Document at13(write(thirteen));
    for (const char* key : {"population_budget", "peoples", "civilizations"}) {
        check::is_true(at13.has(key), std::string("a phase-13 document to carry '") + key + "'");
    }
    check::is_true(!at13.has("world_scene"), "no world_scene before stage 16");
}

// ===========================================================================
// Key names
// ===========================================================================

TEST(phases_and_recipe_carry_the_reference_keys) {
    const Document& doc = doc16();
    const View phases = doc.at("phases");
    same_keys(phases, {"completed", "seeds", "plates", "motion_units", "default_layer", "version", "titles"}, "phases");
    same_keys(phases.at("seeds"), {"layout", "crust", "detail"}, "phases.seeds");
    same_keys(phases.at("plates").items().at(0), {"id", "center", "omega"}, "phases.plates[0]");
    check::equal(phases.at("completed").integer(), 16LL, "phases.completed");
    check::equal(phases.at("version").integer(), 2LL, "phases.version");
    check::equal(phases.at("default_layer").str(), std::string("height"), "phases.default_layer");
    const auto titles = phases.at("titles").items();
    check::equal(titles.size(), std::size_t{16}, "sixteen stage titles");
    check::equal(titles.front().str(), std::string("Plate layout"), "the first title");
    check::equal(titles.back().str(), std::string("Simulation complete"), "the last title");

    const View recipe = doc.at("recipe");
    same_keys(recipe, {"version", "seed", "overrides", "resolved", "parameters", "provenance", "biases"}, "recipe");
    check::equal(recipe.at("version").integer(), 3LL, "recipe.version");
    const View parameters = recipe.at("parameters");
    check::equal(parameters.keys().size(), std::size_t{209}, "the reference registry's 209 published controls");
    check::equal(parameters.keys().size(), control_specs().size(), "one parameter per control spec");
    for (const auto& m : parameters.members()) {
        const View p{m.value};
        const std::set<std::string> keys = p.keys();
        for (const char* k : {"default", "group", "description", "type", "units"}) {
            check::is_true(keys.count(k) == 1, "parameter " + m.key + " to carry " + k);
        }
        check::is_true(!p.at("description").str().empty(), "parameter " + m.key + " to say what it does");
    }
    same_keys(parameters.at("population_profile"),
              {"default", "group", "description", "type", "units", "choices", "choice_labels"},
              "parameters.population_profile");
    same_keys(parameters.at("seed"), {"default", "group", "description", "type", "units", "min", "max"},
              "parameters.seed");
    check::equal(joined(recipe.at("provenance").keys()), joined(parameters.keys()), "one provenance entry per parameter");
    // 219, as the reference's: the 55 Config fields -- ley_nodes, ley_width, magic_instability,
    // extent, depth, width and meander included at recipe 3's values, though this port has no
    // such field and nothing reads them -- and the 164 world options.
    check::equal(recipe.at("resolved").keys().size(), std::size_t{219}, "resolved: every Config field and option");
}

TEST(the_peoples_blocks_carry_the_reference_keys) {
    const Document& doc = doc16();
    const View population = doc.at("population");
    same_keys(population, profile_keys(), "population");
    same_keys(population.at("civilization"), kCivilizationIdentityKeys, "population.civilization");
    check::equal(population.at("id").str(), std::string("mixed"), "the requested profile");
    check::equal(population.at("schema_version").integer(), 4LL, "profile schema version");

    const auto profiles = doc.at("population_profiles").items();
    check::equal(profiles.size(), civilization_profiles().size(), "one row per registry profile");
    for (const View& p : profiles) same_keys(p, {"id", "name"}, "population_profiles[]");

    const View peoples = doc.at("peoples");
    check::equal(peoples.keys().size(), std::size_t{12}, "every competing people of a mixed world");
    for (const auto& m : peoples.members()) same_keys(View{m.value}, profile_keys(), "peoples." + m.key);

    const View budget = doc.at("population_budget");
    same_keys(budget, {"version", "world_cap", "allowances", "shares", "weighted_habitat_km2", "requested_cities",
                       "calibration", "method", "placed_cities", "allocated", "unallocated"},
              "population_budget");
    // The reference's four, plus this port's three: the ceiling's own arithmetic and the
    // limit that bound founding.
    same_keys(budget.at("calibration"),
              {"residents_per_productive_km2", "minimum_city_region_residents", "spacing_city_ceiling",
               "habitable_km2", "per_city_km2", "resolvable_spacing_m", "city_limit"},
              "population_budget.calibration");

    const View civilizations = doc.at("civilizations");
    same_keys(civilizations, {"version", "registry", "heritage", "medium_suitability_min", "capital_scope",
                              "parent_races", "presentation_defaults", "entities"},
              "civilizations");
    same_keys(civilizations.at("registry"), {"schema_version", "revision", "sha256"}, "civilizations.registry");
    same_keys(civilizations.at("heritage"), {"revision", "sha256"}, "civilizations.heritage");
    same_keys(civilizations.at("parent_races"), {"human", "elf", "dwarf"}, "civilizations.parent_races");
    same_keys(civilizations.at("presentation_defaults"), {"outpost_wall_color_rgb", "outpost_roof_color_rgb"},
              "civilizations.presentation_defaults");
    for (const View& e : civilizations.at("entities").items()) {
        same_keys(e, {"id", "region_index", "name", "kind", "inspiration", "environment", "habitat",
                      "allocation_group", "priority", "structure_blocks", "presentation", "parent_race_id",
                      "heritage", "city_count", "capital_node"},
                  "civilizations.entities[]");
        same_keys(e.at("presentation"), {"map_color_rgb", "wall_color_rgb", "roof_color_rgb", "outpost_colors"},
                  "entities[].presentation");
        const View heritage = e.at("heritage");
        same_keys(heritage, {"traits", "culture", "genome", "provenance"}, "entities[].heritage");
        same_keys(heritage.at("genome"), {"family", "branch", "inventory", "sound_changes", "forbidden_forms",
                                          "lexicon", "phonotactics", "prosody", "morphology", "orthography", "rates"},
                  "entities[].heritage.genome");
        same_keys(heritage.at("culture"), {"household", "governance", "livelihood", "memory", "contact", "faith"},
                  "entities[].heritage.culture");
        check::equal(heritage.at("traits").keys().size(), std::size_t{17}, "seventeen key traits");
        same_keys(heritage.at("provenance"), {"revisions", "overridden"}, "entities[].heritage.provenance");
    }
}

TEST(the_scene_and_debug_stats_carry_the_reference_keys) {
    const Document& doc = doc16();
    const View scene = doc.at("world_scene");
    same_keys(scene, {"version", "unit", "coordinates", "radius_m", "terrain_detail", "buildings", "streets",
                      "junctions", "regional_roads", "limits"},
              "world_scene");
    // The scene carries the definition it sampled: the world's own terrain_detail, byte for byte.
    check::equal(std::string(scene.at("terrain_detail").text), std::string(doc.at("terrain_detail").text),
                 "the scene's terrain detail to be the world's");

    const WorldScene& built = world16().world_scene;
    const auto buildings = scene.at("buildings").items();
    check::equal(buildings.size(), built.buildings.size(), "every building");
    check::is_true(!buildings.empty(), "buildings to check");
    const std::set<std::string> box = {"id", "settlement_kind", "plot_id", "asset_id", "position_m", "up",
                                       "width_axis", "depth_axis", "rotation_degrees", "dimensions_m",
                                       "footprint_positions_m"};
    std::set<std::string> kinds;
    for (std::size_t i = 0; i < buildings.size(); ++i) {
        std::set<std::string> expected = box;
        const std::string kind = buildings[i].at("settlement_kind").str();
        kinds.insert(kind);
        // The reference's one join key, and for the two that renumber, the port's stable one.
        if (kind == "city") expected.insert("city_uid");
        if (kind == "hamlet") expected.insert({"hamlet_id", "hamlet_uid"});
        if (kind == "castle") expected.insert({"fortress_id", "fortress_uid"});
        same_keys(buildings[i], expected, "world_scene.buildings[" + kind + "]");
        same_keys(buildings[i].at("dimensions_m"), {"width", "depth", "height"}, "buildings[].dimensions_m");
        check::equal(buildings[i].at("footprint_positions_m").items().size(), std::size_t{4}, "four corners");
    }
    check::is_true(kinds.count("city") == 1 && kinds.count("hamlet") == 1, "city and hamlet buildings both");
    bool approach = false;
    for (const View& s : scene.at("streets").items()) {
        std::set<std::string> keys = s.keys();
        approach = approach || keys.count("junction_id") != 0;
        keys.erase("junction_id");
        keys.erase("city_uid");
        keys.erase("hamlet_id");
        keys.erase("hamlet_uid");
        keys.erase("fortress_id");
        keys.erase("fortress_uid");
        check::equal(joined(keys), std::string("id, positions_m, settlement_kind"), "a street's own keys");
    }
    check::is_true(approach, "an approach street carrying its junction");
    for (const View& j : scene.at("junctions").items()) {
        check::is_true(j.has("route_id") && j.has("position_m") && j.has("status"), "a junction's keys");
    }
    for (const View& r : scene.at("regional_roads").items()) {
        same_keys(r, {"id", "source_route_index", "positions_m", "from_junction", "to_junction", "from_status",
                      "to_status", "bridge_candidates"},
                  "world_scene.regional_roads[]");
    }
    check::equal(scene.at("regional_roads").items().size(), built.regional_roads.size(), "every regional road");

    const View debug = doc.at("debug_stats");
    same_keys(debug, {"version", "scope", "parent_races", "civilizations", "cities", "diaspora", "founding_outcomes",
                      "founding_events", "ruins", "timeline", "leylines", "biome_coverage"},
              "debug_stats");
    const auto cities = debug.at("cities").items();
    check::equal(cities.size(), world16().settlements.sites.size(), "one debug row per city");
    for (const View& c : cities) {
        same_keys(c, {"uid", "name", "civilization", "parent_race", "urban_population_estimate", "nearest_city_m",
                      "stats", "placement", "unplaced", "junctions"},
                  "debug_stats.cities[]");
    }
    for (const View& g : debug.at("parent_races").items()) {
        same_keys(g, {"id", "cities", "urban_population", "workers", "beds"}, "debug_stats.parent_races[]");
    }
    same_keys(debug.at("diaspora"), {"founded", "surviving", "destroyed"}, "debug_stats.diaspora");
    for (const View& t : debug.at("timeline").items()) {
        same_keys(t, {"stage", "title", "cities", "urban_population"}, "debug_stats.timeline[]");
    }
    // The founding events are the settlements block's rows, which carry the founded site as
    // the reference's do, the three diaspora fields on a diaspora founding only.
    const std::set<std::string> round = {"turn", "parent_race_id", "status", "radius_m"};
    std::set<std::string> founded = round;
    founded.insert({"suitability", "node", "population_profile", "founding_turn", "founding_capital", "founding_year",
                    "migration_source_node", "source_civilization_id", "migration_distance_m", "cultural_branch"});
    std::set<std::string> statuses;
    for (const View& e : debug.at("founding_events").items()) {
        const std::string status = e.at("status").str();
        statuses.insert(status);
        if (status == "founded") {
            same_keys(e, founded, "a founded event");
        } else if (status == "diaspora_founded") {
            std::set<std::string> diaspora = founded;
            diaspora.insert({"diaspora", "diaspora_reason", "diaspora_bonus"});
            same_keys(e, diaspora, "a diaspora event");
        } else if (status == "did_not_participate") {
            std::set<std::string> skipped = round;
            skipped.insert("draw_percent");
            same_keys(e, skipped, "a did_not_participate event");
        }
    }
    check::is_true(statuses.count("founded") == 1 && statuses.count("did_not_participate") == 1,
                   "founded and did_not_participate events both, so both shapes are checked");
}

// ===========================================================================
// Values
// ===========================================================================

TEST(the_population_budget_is_the_founding_it_describes) {
    const World& world = world16();
    const PopulationBudget& b = world.settlements.budget;
    const Document& doc = doc16();
    const View budget = doc.at("population_budget");
    check::equal(budget.at("world_cap").integer(), b.world_capacity, "world_cap");
    check::is_true(b.world_capacity > 0, "a world whose ground feeds somebody");

    std::map<std::string, long long> placed;
    long long allocated = 0;
    for (const SettlementSite& s : world.settlements.sites) {
        ++placed[s.population_profile];
        allocated += s.population_estimate;
    }
    check::equal(budget.at("allocated").integer(), allocated, "allocated: every city's residents");
    check::equal(budget.at("unallocated").integer(), b.world_capacity - allocated, "unallocated: the rest");
    for (const auto& m : budget.at("placed_cities").members()) {
        check::equal(View{m.value}.integer(), placed[m.key], m.key + "'s placed cities");
    }
    for (const auto& m : budget.at("shares").members()) {
        const double expected = static_cast<double>(b.allowances.at(m.key)) / static_cast<double>(b.world_capacity);
        check::equal(View{m.value}.num(), expected, m.key + "'s share");
    }
    for (const auto& m : budget.at("calibration").at("minimum_city_region_residents").members()) {
        check::equal(View{m.value}.integer(),
                     static_cast<long long>(civilization_profile(m.key)->traits.minimum_founding_residents),
                     m.key + "'s founding minimum");
    }
    // What founding actually worked to, as the reference publishes it.
    const View quotas = doc.at("settlements").at("founding").at("effective_quotas");
    check::equal(std::string(budget.at("requested_cities").text), std::string(quotas.text),
                 "requested_cities to be the founding report's effective quotas");
    // One full profile per people that competed, and the requested profile itself.
    check::equal(joined(doc.at("peoples").keys()), joined(budget.at("shares").keys()), "peoples: one per share");
}

TEST(a_profile_record_hashes_its_own_canonical_text) {
    // The digest is SHA-256 of the record without it, sorted keys, compact: recompute it from
    // the document's own bytes by removing the one member.
    const Document& doc = doc16();
    std::set<std::string> hashes;
    for (const auto& m : doc.at("peoples").members()) {
        std::string rebuilt = "{";
        std::string stated;
        std::string previous;
        for (const auto& field : json_scan::object_members(m.value)) {
            check::is_true(previous < field.key, "a profile record's keys to be sorted");
            previous = field.key;
            if (field.key == "definition_hash") {
                stated = json_scan::string_value(field.value);
                continue;
            }
            if (rebuilt.size() > 1) rebuilt += ',';
            rebuilt += json_scan::quoted(field.key) + ":" + std::string(field.value);
        }
        rebuilt += '}';
        check::equal(stated, sha256_hex(rebuilt), m.key + "'s definition_hash");
        check::equal(stated, profile_definition_hash(*civilization_profile(m.key)), m.key + " against the library");
        hashes.insert(stated);
    }
    check::equal(hashes.size(), std::size_t{12}, "twelve different peoples, twelve different digests");
}

TEST(the_civilizations_block_counts_the_cities_it_names) {
    const World& world = world16();
    const View block = doc16().at("civilizations");
    check::equal(block.at("registry").at("sha256").str(), std::string(registry_identity().sha256),
                 "the port's own registry digest");
    const auto entities = block.at("entities").items();
    const auto ids = civilization_ids();
    check::equal(entities.size(), ids.size(), "one row per entity, used or not");
    long long total = 0;
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const View& e = entities[i];
        check::equal(e.at("id").str(), std::string(ids[i]), "registry order");
        check::equal(e.at("region_index").integer(), static_cast<long long>(i), "region_index is the position");
        long long cities = 0;
        int capital = -1;
        for (const SettlementSite& s : world.settlements.sites) {
            if (s.population_profile != ids[i]) continue;
            ++cities;
            if (s.city_class == CityClass::Capital) capital = s.node;
        }
        check::equal(e.at("city_count").integer(), cities, std::string(ids[i]) + "'s city count");
        if (capital < 0) {
            check::is_true(e.at("capital_node").null(), std::string(ids[i]) + " with no city has no capital");
        } else {
            check::equal(e.at("capital_node").integer(), static_cast<long long>(capital), std::string(ids[i]) + "'s capital");
        }
        total += cities;
        check::equal(e.at("presentation").at("outpost_colors").boolean(), *entity_outpost_colors(ids[i]),
                     "outpost_colors from the registry");
    }
    check::equal(total, static_cast<long long>(world.settlements.sites.size()), "every city in some row");
    // human_large_island's habitat tests a boolean field, and says so as a boolean.
    const std::string island(entities[3].at("habitat").text);
    check::is_true(island.find("\"equals\":false") != std::string::npos,
                   "largest_landmass tested against false, not 0: " + island);
}

TEST(recipe_records_what_the_request_set_and_why_the_rest_moved) {
    Request request;
    request.seed = 7;
    request.size = 17;
    request.phase = 1;
    request.world_size = WorldSize::Medium;
    request.options["draconic_occurrence"] = 0.9;  // asks for dragons, so mountains are biased up
    const World world = generate(resolve(request));
    const Document doc(write(world));
    const View recipe = doc.at("recipe");

    const View overrides = recipe.at("overrides");
    same_keys(overrides, {"draconic_occurrence", "phase", "size", "world_size"}, "recipe.overrides");
    check::equal(overrides.at("size").text, std::string_view("17"), "an integer control written whole");
    check::equal(overrides.at("world_size").str(), std::string("medium"), "a string control as a string");
    check::equal(overrides.at("draconic_occurrence").num(), 0.9, "the option as requested");
    check::equal(recipe.at("seed").integer(), 7LL, "recipe.seed");

    const View provenance = recipe.at("provenance");
    check::equal(provenance.at("size").str(), std::string("override"), "size");
    check::equal(provenance.at("draconic_occurrence").str(), std::string("override"), "the requested option");
    check::equal(provenance.at("mountain_abundance").str(),
                 std::string("draconic request biases suitable conditions; placement remains conditional"),
                 "the biased prerequisite");
    // Not requested, but not the default either: the medium preset set it.
    check::equal(provenance.at("globe_radius").str(), std::string("world_size preset"), "globe_radius");
    check::equal(provenance.at("seed").str(), std::string("request"), "seed");
    check::equal(provenance.at("plate_count").str(), std::string("default"), "an untouched control");

    const View biases = recipe.at("biases");
    same_keys(biases, {"mountain_abundance"}, "recipe.biases");
    check::equal(biases.at("mountain_abundance").at("value").num(), world.config.options[Option::mountain_abundance],
                 "the biased value");

    const View resolved = recipe.at("resolved");
    check::equal(resolved.at("globe_radius").num(), world.config.globe_radius, "resolved is the design-space config");
    check::equal(resolved.at("world_size").str(), std::string("medium"), "resolved world_size");
    check::equal(resolved.at("world_options").str(),
                 "{\"draconic_occurrence\": 0.9, \"mountain_abundance\": " +
                     format_double(world.config.options[Option::mountain_abundance]) + "}",
                 "world_options: every explicitly set option, sorted, Python's separators");

    // A request that sets nothing reports nothing -- the CLI no longer volunteers a phase.
    Request bare;
    bare.size = 17;
    bare.phase = 1;
    const Document bare_doc(write(generate(resolve(bare))));
    const View plain = bare_doc.at("recipe");
    same_keys(plain.at("overrides"), {"phase", "size"}, "only what was set");
    check::equal(plain.at("provenance").at("globe_radius").str(), std::string("default"),
                 "the small preset is the reference's default label");
}

TEST(every_published_default_is_the_one_this_build_runs_with) {
    // The control table is documentation; Config and the option table are what generate. A
    // default that drifts between them publishes a world nobody built.
    const Config cfg;
    const std::map<std::string, double> config_defaults = {
        {"amplitude", cfg.amplitude}, {"belt_width", cfg.belt_width}, {"bridge_cost", cfg.bridge_cost},
        {"college_count", cfg.college_count}, {"crust_bias", cfg.crust_bias},
        {"culture_link_cost", cfg.culture_link_cost}, {"detail_variation", cfg.detail_variation},
        {"erosion_passes", cfg.erosion_passes}, {"erosion_strength", cfg.erosion_strength},
        {"fortress_count", cfg.fortress_count}, {"globe_radius", cfg.globe_radius},
        {"hamlets_per_core", cfg.hamlets_per_core}, {"human_magic_limit", cfg.human_magic_limit},
        {"layout_variation", cfg.layout_variation}, {"magic_enabled", cfg.magic_enabled},
        {"moisture_bias", cfg.moisture_bias}, {"mountain_detail", cfg.mountain_detail}, {"octaves", cfg.octaves},
        {"orogeny", cfg.orogeny},
        {"phase", cfg.phase}, {"plate_count", cfg.plate_count}, {"radius", cfg.radius},
        {"rain_passes", cfg.rain_passes}, {"rain_strength", cfg.rain_strength}, {"ridge", cfg.ridge},
        {"river_threshold_km2", cfg.river_threshold_km2}, {"road_max_grade", cfg.road_max_grade},
        {"sea_level", cfg.sea_level}, {"seed", cfg.seed}, {"settlement_count", cfg.settlement_count},
        {"settlement_spacing", cfg.settlement_spacing}, {"size", cfg.size}, {"stubbornness", cfg.stubbornness},
        {"support_reach", cfg.support_reach}, {"tectonic_relief", cfg.tectonic_relief},
        {"temperature_offset", cfg.temperature_offset}, {"wavelength", cfg.wavelength},
        {"wind_bearing", cfg.wind_bearing}, {"world_scale", cfg.world_scale}};
    std::vector<std::string> disagree;
    for (const ControlSpec& c : control_specs()) {
        if (c.world_option) {
            const auto option = option_by_name(c.name);
            check::is_true(option.has_value(), "world option " + std::string(c.name) + " in the option table");
            check::equal(c.default_number, option_spec(*option).default_value, std::string(c.name) + " default");
            continue;
        }
        // Not Config fields: the two authored inputs the request builder consumes, and the
        // fixed shape; the two string controls are compared below.
        static const std::set<std::string> elsewhere = {"circumference_km", "relief_m", "tectonics", "shape",
                                                       "world_size", "population_profile"};
        if (elsewhere.count(std::string(c.name)) != 0) continue;
        const auto it = config_defaults.find(std::string(c.name));
        check::is_true(it != config_defaults.end(), "a Config default to compare " + std::string(c.name) + " with");
        if (it->second != c.default_number) disagree.push_back(std::string(c.name));
    }
    // KNOWN, and pinned so a change is noticed: the reference's dataclass defaults orogeny to
    // 1.0 and so does its published registry, while this port's Config carries 10.0. The two
    // resolve the same on every preset-width world (both derive 10 there) and differ only when
    // an explicit globe_radius bypasses the width derivation.
    check::equal(joined(std::set<std::string>(disagree.begin(), disagree.end())), std::string("orogeny"),
                 "published defaults that differ from Config's");
    check::equal(control_spec("population_profile")->default_text, std::string_view(cfg.population_profile),
                 "population_profile default");
    check::equal(control_spec("world_size")->default_text, std::string_view(to_string(cfg.world_size)),
                 "world_size default");
}

// ===========================================================================
// Bytes
// ===========================================================================

TEST(the_new_blocks_serialise_to_identical_bytes_across_runs) {
    // Without snapshots the whole size-33 document, new blocks included, is pinned by
    // test_serialize's two_generations_of_one_seed_serialise_to_identical_bytes. What is new
    // here: a second recorded generation writes the same build_stages.
    BuildStageRecorder recorder;
    Request request;
    request.seed = 42;
    request.size = 17;
    request.phase = 16;
    const World staged_again =
        generate(resolve(request), [&](int stage, const World& w) { recorder.capture(stage, w); });
    const std::string c = write(staged_again, &recorder.stages());
    check::equal(c.size(), staged().document.size(), "staged document size");
    check::is_true(c == staged().document, "the snapshots to be byte-identical too");
    // Recording does not change the world: a generation with no observer writes the same
    // document the recorded one does, snapshots aside.
    check::is_true(write(generate(resolve(request))) == write(staged().world),
                   "the recorder to observe, not perturb");
}

TEST(a_founding_event_carries_the_site_it_founded) {
    // The reference's founding event IS the founded site's record with the round's fields
    // added. Every city founded in the world's last age has its event, and the event says
    // what the site says.
    const World& world = world16();
    const auto& events = world.settlements.founding.events;
    int checked = 0;
    for (const SettlementSite& s : world.settlements.sites) {
        if (s.founded_age != world.age) continue;  // a survivor's founding is an earlier age's report
        const FoundingEvent* found = nullptr;
        for (const FoundingEvent& e : events) {
            if (e.node == s.node && e.population_profile == s.population_profile) found = &e;
        }
        check::is_true(found != nullptr, s.uid + " to have its founding event");
        check::equal(found->founding_year, s.founding_year, s.uid + " founding year");
        check::equal(found->founding_turn, s.founding_turn, s.uid + " founding turn");
        check::equal(found->founding_capital, s.founding_capital, s.uid + " capital flag");
        check::equal(found->migration_source_node, s.migration_source_node, s.uid + " migration source");
        check::equal(found->cultural_branch, s.cultural_branch, s.uid + " cultural branch");
        check::equal(found->diaspora, s.diaspora, s.uid + " diaspora");
        ++checked;
    }
    check::is_true(checked > 0, "cities founded in the last age to check");
}

// ===========================================================================
// build_stages
// ===========================================================================

TEST(sixteen_snapshots_each_holding_only_what_changed) {
    const Staged& s = staged();
    check::equal(s.stages.size(), std::size_t{16}, "one snapshot per stage");
    for (std::size_t i = 0; i < s.stages.size(); ++i) {
        const BuildStage& entry = s.stages[i];
        check::equal(entry.stage, static_cast<int>(i) + 1, "stage order");
        check::equal(entry.title, std::string(stage_titles()[i]), "the stage's title");
        for (const auto& [key, value] : entry.state) {
            check::is_true(is_state_key(key), "only state keys in a snapshot: " + key);
        }
        check::is_true(std::is_sorted(entry.removed_layers.begin(), entry.removed_layers.end()),
                       "removed layers sorted");
    }
    // Replaying the deltas: every recorded value differs from the one before it, and an
    // unrecorded one is unchanged -- checked against a full capture of each stage's own
    // phase-N generation below; here, that nothing is recorded twice unchanged.
    std::map<std::string, std::string> layers;
    std::map<std::string, std::string> state;
    for (const BuildStage& entry : s.stages) {
        for (const auto& [name, value] : entry.layers) {
            const auto it = layers.find(name);
            check::is_true(it == layers.end() || it->second != *value,
                           "stage " + std::to_string(entry.stage) + " to record layer " + name + " only if it changed");
            layers[name] = *value;
        }
        for (const std::string& name : entry.removed_layers) layers.erase(name);
        for (const auto& [key, value] : entry.state) {
            const auto it = state.find(key);
            if (value == nullptr) {
                check::is_true(it != state.end(), "a removal of a block that existed");
                state.erase(key);
                continue;
            }
            check::is_true(it == state.end() || it->second != *value,
                           "stage " + std::to_string(entry.stage) + " to record " + key + " only if it changed");
            state[key] = *value;
        }
    }
    // Stage 1 is the world's first state: the plates, and the blocks that exist from the start.
    std::set<std::string> first;
    for (const auto& [key, value] : s.stages.front().state) first.insert(key);
    check::equal(joined(first), std::string("history, population, population_profiles, ruins"), "stage 1's state");
    // Roads change the roads, and nothing else the settlements block holds.
    std::set<std::string> eleven;
    for (const auto& [key, value] : s.stages[10].state) eleven.insert(key);
    check::equal(joined(eleven), std::string("roads"), "stage 11's state");
}

TEST(materialising_stage_n_reproduces_a_phase_n_generation) {
    const Staged& s = staged();
    const Document finished(s.document);
    for (int stage = 1; stage <= 16; ++stage) {
        Request request;
        request.seed = 42;
        request.size = 17;
        request.phase = stage;
        const Document expected(write(generate(resolve(request))));
        const Document actual(materialize_stage(s.document, stage));
        const std::string where = "stage " + std::to_string(stage);

        // The layers, all of them, byte for byte.
        std::map<std::string, std::string_view> want;
        for (const auto& m : expected.at("layers").members()) want.emplace(m.key, m.value);
        std::map<std::string, std::string_view> got;
        for (const auto& m : actual.at("layers").members()) got.emplace(m.key, m.value);
        check::equal(got.size(), want.size(), where + " layer count");
        for (const auto& [name, text] : want) {
            const auto it = got.find(name);
            check::is_true(it != got.end(), where + " to have layer " + name);
            check::is_true(it->second == text, where + " layer " + name + " to be the phase-" +
                                                   std::to_string(stage) + " layer");
        }
        // Every state key: present in both with the same bytes, or in neither.
        for (const std::string_view key : state_keys()) {
            check::equal(actual.has(key), expected.has(key), where + " presence of " + std::string(key));
            if (expected.has(key)) {
                check::is_true(actual.at(key).text == expected.at(key).text,
                               where + " " + std::string(key) + " to be the phase-" + std::to_string(stage) + " block");
            }
        }
        // And the rest from the finished document, whatever the stage.
        check::is_true(actual.at("recipe").text == finished.at("recipe").text,
                       where + " to take the non-state blocks from the finished world");
        check::is_true(!actual.has("build_stages"), where + " to drop the snapshots themselves");
    }
    check::throws([&] { (void)materialize_stage(s.document, 0); }, "stage 0 was not generated");
    check::throws([&] { (void)materialize_stage(s.document, 17); }, "stage 17 was not generated");
    check::throws([] { (void)materialize_stage("{\"layers\":{}}", 1); }, "a document without snapshots");
}

TEST(materialise_follows_the_reference_order_within_one_snapshot) {
    // Written, then removed, in one snapshot: removed. A state set to null: gone. A key the
    // first snapshot set and the second removed and the third set again: at the end.
    const std::string document =
        R"({"config":{"a":1},"population":{"p":1},"layers":{"x":[[9]]},"build_stages":[)"
        R"({"stage":1,"title":"one","layers":{"x":[[1]],"y":[[2]]},"removed_layers":[],"state":{"population":{"p":1},"history":{"h":1}}},)"
        R"({"stage":2,"title":"two","layers":{"z":[[3]]},"removed_layers":["y","z"],"state":{"population":null}},)"
        R"({"stage":3,"title":"three","layers":{"x":[[4]]},"removed_layers":[],"state":{"population":{"p":3}}}]})";
    check::equal(materialize_stage(document, 1),
                 std::string(R"({"config":{"a":1},"layers":{"x":[[1]],"y":[[2]]},"population":{"p":1},"history":{"h":1}})"),
                 "stage 1");
    check::equal(materialize_stage(document, 2),
                 std::string(R"({"config":{"a":1},"layers":{"x":[[1]]},"history":{"h":1}})"), "stage 2");
    check::equal(materialize_stage(document, 3),
                 std::string(R"({"config":{"a":1},"layers":{"x":[[4]]},"history":{"h":1},"population":{"p":3}})"),
                 "stage 3");
}

// ===========================================================================
// debug_stats
// ===========================================================================

TEST(generation_records_each_stage_that_changed_the_settlements) {
    // Two independent detectors: the fingerprint generate() keeps, and the snapshot recorder's
    // byte comparison of the same block. They must name the same stages.
    const Staged& s = staged();
    std::vector<int> recorded;
    for (const SettlementStageRecord& r : s.world.settlement_stages) recorded.push_back(r.stage);
    std::vector<int> captured;
    for (const BuildStage& entry : s.stages) {
        for (const auto& [key, value] : entry.state) {
            if (key == "settlements") captured.push_back(entry.stage);
        }
    }
    check::is_true(!recorded.empty() && recorded.front() == 10, "the first record at stage 10");
    check::equal(recorded.size(), captured.size(), "as many records as snapshots of the settlements");
    for (std::size_t i = 0; i < recorded.size(); ++i) check::equal(recorded[i], captured[i], "the same stages");
    // debug_stats' timeline is those records.
    const Document doc(s.document);
    const auto timeline = doc.at("debug_stats").at("timeline").items();
    check::equal(timeline.size(), recorded.size(), "one timeline row per record");
    for (std::size_t i = 0; i < timeline.size(); ++i) {
        check::equal(timeline[i].at("stage").integer(), static_cast<long long>(recorded[i]), "timeline stage");
    }
}

TEST(debug_stats_matches_a_hand_computed_world) {
    World world;
    world.effective.globe_radius = 1000.0;
    world.has_settlements = true;
    const auto site = [](int id, std::string uid, int node, std::string people, std::string parent, Vec3 direction,
                         long long urban) {
        SettlementSite s;
        s.id = id;
        s.uid = std::move(uid);
        s.node = node;
        s.name = "City " + std::to_string(id);
        s.population_profile = std::move(people);
        s.parent_race_id = std::move(parent);
        s.direction = direction;
        s.urban_population_estimate = urban;
        return s;
    };
    // Three cities a quarter of the way round from one another, on the equator and the pole.
    world.settlements.sites = {site(0, "c0", 10, "elf", "elf", {1, 0, 0}, 100),
                               site(1, "c1", 20, "dwarf", "dwarf", {0, 0, 1}, 50),
                               site(2, "c2", 30, "elf", "elf", {0, 1, 0}, 7)};
    // Only the first is planned.
    world.has_plans = true;
    CityPlan plan;
    plan.city_uid = "c0";
    plan.stats.workers = 11;
    plan.stats.worker_beds = 12;
    plan.unplaced.push_back({"building.well", 2, "Unverified prerequisite"});
    RoadConnection connected;
    connected.status = "connected";
    RoadConnection unreachable;
    unreachable.status = "unreachable";
    plan.road_connections = {connected, unreachable, connected};
    world.city_plans.cities.push_back(plan);

    const auto event = [](std::string status, int turn, std::string parent, int node, std::string people,
                          long long year) {
        FoundingEvent e;
        e.status = std::move(status);
        e.turn = turn;
        e.parent_race_id = std::move(parent);
        e.node = node;
        e.population_profile = std::move(people);
        e.founding_year = year;
        return e;
    };
    // Stage 10's founding, then an age's. The age repeats elf's turn-2 refusal (no node, no
    // people, no year: one identity) with a different draw -- the later replaces the earlier
    // in its place -- and founds a diaspora city that later fell.
    SettlementStageRecord ten;
    ten.stage = 10;
    ten.title = "Founding";
    ten.cities = 2;
    ten.urban_population = 150;
    ten.founding_events = {event("founded", 1, "elf", 10, "elf", 0), event("did_not_participate", 2, "elf", -1, "", 0),
                           event("founded", 1, "dwarf", 20, "dwarf", 0)};
    ten.founding_events[1].draw_percent = 10.0;
    SettlementStageRecord fourteen;
    fourteen.stage = 14;
    fourteen.title = "Age transition 1";
    fourteen.cities = 3;
    fourteen.urban_population = 157;
    fourteen.founding_events = {event("did_not_participate", 2, "elf", -1, "", 0),
                                event("diaspora_founded", 3, "elf", 30, "elf", 5500),
                                event("diaspora_founded", 4, "dwarf", 40, "dwarf", 5750)};
    fourteen.founding_events[0].draw_percent = 99.0;
    world.settlement_stages = {ten, fourteen};
    // The world as it stands repeats stage 14's founding exactly: nothing new.
    world.settlements.founding.events = fourteen.founding_events;
    Ruin fallen;
    fallen.node = 40;
    fallen.population_profile = "dwarf";
    world.ruins.push_back(fallen);

    // Two ley networks: one manifested, one not.
    world.has_magic = true;
    LeyNetwork weave;
    weave.name = "weave";
    weave.nodes.resize(3);
    weave.edges.resize(2);
    weave.manifested = true;
    weave.distribution.clusters = 2;
    world.magic.networks["weave"] = weave;
    LeyNetwork blood;
    blood.name = "blood";
    world.magic.networks["blood"] = blood;

    // A 3x3 natural biome raster: snow on both pole rows, ocean then grassland on the
    // equator row; the seam column (x = 2) must not count.
    world.has_terrain = true;
    Grid biome(3, 6.0);
    biome.at(0, 1) = 0.0;
    biome.at(1, 1) = 3.0;
    biome.at(2, 1) = 0.0;  // the seam repeats x = 0
    world.layers["natural_biome"] = biome;

    const DebugStats d = build_debug(world);

    // Groups, first seen first; workers and beds from the one plan.
    check::equal(d.parent_races.size(), std::size_t{2}, "two parent races");
    check::equal(d.parent_races[0].id, std::string("elf"), "elf first");
    check::equal(d.parent_races[0].cities, 2LL, "elf's cities");
    check::equal(d.parent_races[0].urban_population, 107LL, "elf's urban population");
    check::equal(d.parent_races[0].workers, 11LL, "elf's planned workers");
    check::equal(d.parent_races[0].beds, 12LL, "elf's planned beds");
    check::equal(d.parent_races[1].workers, 0LL, "an unplanned people's workers");

    // Every city is a quarter of the sphere from every other: radius * pi / 2.
    const double quarter = 1000.0 * std::numbers::pi / 2.0;
    for (const DebugCity& c : d.cities) {
        check::is_true(c.nearest_city_m.has_value(), "a nearest city");
        check::close(*c.nearest_city_m, quarter, 1e-9, c.uid + "'s nearest city");
    }
    check::is_true(d.cities[0].planned && !d.cities[1].planned, "only c0 planned");
    check::equal(d.cities[0].junctions.size(), std::size_t{2}, "two junction statuses");
    check::equal(d.cities[0].junctions[0].first, std::string("connected"), "connected first");
    check::equal(d.cities[0].junctions[0].second, 2LL, "two connected");
    check::equal(d.cities[0].junctions[1].second, 1LL, "one unreachable");
    check::equal(d.cities[0].unplaced.size(), std::size_t{1}, "the plan's unplaced row");

    // Five distinct events from nine rows: the stage-14 refusal took the stage-10 one's
    // place, and the finished world's repeat of stage 14 added nothing.
    check::equal(d.founding_events.size(), std::size_t{5}, "de-duplicated events");
    check::equal(d.founding_events[1].status, std::string("did_not_participate"), "the refusal kept its place");
    check::equal(d.founding_events[1].draw_percent, 99.0, "and took the later record");
    check::equal(d.founding_outcomes.size(), std::size_t{3}, "three outcomes");
    check::equal(d.founding_outcomes[0].first, std::string("founded"), "founded first");
    check::equal(d.founding_outcomes[0].second, 2LL, "two foundings");
    check::equal(d.founding_outcomes[2].first, std::string("diaspora_founded"), "diaspora last");
    check::equal(d.founding_outcomes[2].second, 2LL, "two diaspora foundings");
    check::equal(d.diaspora_founded, 2LL, "diaspora founded");
    check::equal(d.diaspora_surviving, 1LL, "c2 still stands");
    check::equal(d.diaspora_destroyed, 1LL, "the dwarf diaspora fell");

    check::equal(d.timeline.size(), std::size_t{2}, "a row per recorded stage");
    check::equal(d.timeline[1].stage, 14, "the age's row");
    check::equal(d.timeline[1].urban_population, 157LL, "the age's urban population");

    // School-table order, known before hidden; an unmanifested network has no distribution.
    check::equal(d.leylines.size(), std::size_t{2}, "the two networks the world has");
    check::equal(d.leylines[0].id, std::string("weave"), "weave first");
    check::equal(d.leylines[0].nodes, 3LL, "weave's nodes");
    check::equal(d.leylines[0].lines, 2LL, "weave's lines");
    check::is_true(d.leylines[0].distribution.has_value() && d.leylines[0].distribution->clusters == 2,
                   "a manifested network's distribution");
    check::is_true(!d.leylines[1].distribution.has_value(), "no distribution for blood");

    // Bands on a size-3 grid: the pole rows each cover 1 - sin(pi/4) of the sphere's height
    // range, the equator row 2 sin(pi/4). Two columns count.
    const double pole = 1.0 - std::sin(std::numbers::pi / 4.0);
    const double equator = 2.0 * std::sin(std::numbers::pi / 4.0);
    const double total = 2.0 * (2.0 * pole + equator);
    check::equal(d.biome_coverage.size(), std::size_t{3}, "three biomes");
    check::equal(d.biome_coverage[0].id, 0, "ascending id");
    check::equal(d.biome_coverage[0].name, std::string("Ocean"), "the catalogue's name");
    check::close(d.biome_coverage[0].percent_world, 100.0 * equator / total, 1e-12, "ocean");
    check::close(d.biome_coverage[1].percent_world, 100.0 * equator / total, 1e-12, "grassland");
    check::close(d.biome_coverage[2].percent_world, 100.0 * 4.0 * pole / total, 1e-12, "snow");
}

// ===========================================================================
// json_scan
// ===========================================================================

TEST(the_scanner_splits_at_the_seams_and_nowhere_else) {
    // A backslash-u, assembled from its character code: spelled out in this source it was
    // decoded to the character itself on the way in, and the escape went untested.
    const std::string u = std::string(1, static_cast<char>(92)) + "u";
    const std::string text = R"( { "a" : [1, {"b":"}]"}, "x\"y"], "k)" + u + R"(00e9y":{"n":null} , "t":true } )";
    check::is_true(text.find(u + "00e9") != std::string::npos, "the text to carry the escape itself");
    const auto members = json_scan::object_members(text);
    check::equal(members.size(), std::size_t{3}, "three members");
    check::equal(members[0].key, std::string("a"), "first key");
    check::equal(std::string(members[0].value), std::string(R"([1, {"b":"}]"}, "x\"y"])"), "a bracket inside a string");
    check::equal(members[1].key, std::string("k\xc3\xa9y"), "a \\u escape decoded to UTF-8");
    check::equal(json_scan::array_items(members[0].value).size(), std::size_t{3}, "three items");
    check::equal(json_scan::string_value(R"("x\"y")"), std::string("x\"y"), "an escaped quote");
    check::equal(json_scan::string_value("\"" + u + "d83d" + u + "de00\""), std::string("\xf0\x9f\x98\x80"),
                 "a surrogate pair");
    check::throws([] { (void)json_scan::object_members(R"({"a":1)"); }, "an unclosed object");
    check::throws([] { (void)json_scan::object_members(R"({"a" 1})"); }, "a missing colon");
    check::throws([] { (void)json_scan::object_members(R"({"a":1}})"); }, "a stray brace");
    check::throws([] { (void)json_scan::array_items(R"(["a)"); }, "an unterminated string");
}
