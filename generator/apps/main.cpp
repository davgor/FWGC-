// fwg -- generate a world and write it out.
//
// The CLI is deliberately thin: everything it does is available through the library
// headers, and nothing it does is available only here.
#include <algorithm>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/asset_list.hpp"
#include "fwg/capabilities.hpp"
#include "fwg/civilizations.hpp"
#include "fwg/json.hpp"
#include "fwg/json_value.hpp"
#include "fwg/patch.hpp"
#include "fwg/request_json.hpp"
#include "fwg/serialize.hpp"
#include "fwg/weather.hpp"
#include "fwg/workers.hpp"
#include "fwg/world.hpp"
#include "fwg/world_time.hpp"

namespace {

void usage() {
    std::cout <<
        R"(fwg -- fantasy world generator

usage:
  fwg [--terse-errors] generate [options]
  fwg registry            dump the civilization registry as JSON
  fwg materialize --stage N [-o PATH] DOCUMENT
                          rebuild the world at stage N from a document written
                          with --include-build-stages (compact JSON)
  fwg asset-list --output PATH
                          compile the exhaustive potential-state asset list
  fwg capabilities [--version N] --output PATH
                          export the capability descriptor (version 1)
  fwg controls [--group G] [--name N]... [--query TEXT] [--include-pinned]
               [--output PATH]
                          describe the generator controls without generating;
                          open controls only unless --include-pinned
  fwg patch --request PATH [--latitude DEG] [--longitude DEG] [--span M]
            [--spacing M] [--detail-height M] [--detail-scale M] [--output PATH]
                          regenerate the world a request file describes and cut a
                          local patch of ground from it: a metre mesh around one
                          point, as the fwg.patch document (compact JSON). Defaults
                          0, 0, 64 m, 0.5 m, 0.4 m, 8 m; a refusal is printed as
                          for generate --request
  fwg weather --request PATH [--day D] [--latitude DEG --longitude DEG]
              [--hours N] [--output PATH]
                          regenerate the world a request file describes and read
                          its weather hour by hour for N hours (default 1) from
                          day D (real days since founding year 0; default the
                          world's clock): each hour's storm systems and, given a
                          place, its intensity, wind and summer percentage then,
                          as the fwg.weather document (compact JSON). A refused
                          day or place is printed as a failure document, exit 2

--terse-errors          a refused request's JSON envelope carries only code,
                        message and field, not received/expected/suggestion

generate options:
  --request PATH        a JSON request {"seed", "recipe_version", "overrides"},
                        overrides keyed by published control name (see
                        `fwg controls`); cannot be combined with the flags below
                        that shape the world. A refusal is printed to stdout as a
                        fantasy-world-generator.failure document; exit 2.
  --seed N              master seed (default 42)
  --size N              grid cells along one edge, odd, 3..1025 (default 129)
  --phase N             stop after stage N, 1..16 (default 16; see below)
  --world-size NAME     small | medium | large (default small)
  --circumference-km X  authored world width; overrides --world-size
  --relief-m X          authored relief budget (default 1667)
  --plates N            plate count (default 12)
  --erosion-passes N    0..40 (default 6)
  --erosion-strength X  0..1 (default 0.5)
  --sea-level X         design-space datum (default 0)
  --magic-enabled N     0 leaves every ley network unmanifested (default 1)
  --settlement-count N  maximum surface cities; 0 for an empty world
  --population-profile  a civilization id, or "mixed" for all of them (default)
  --option NAME=VALUE   set one of the 164 world options by its published name;
                        repeatable. `fwg registry` lists them.
  -o, --out, --output PATH
                        write JSON here (default stdout); the destination is
                        checked before a world is spent on it
  --split DIR           write the world as a directory a game reads in parts:
                        world.json without the plans and the world scene, one
                        file per plan under plans/, the scene as scene.json;
                        each catalogue once. DIR must be new or empty
  --pretty              indent the document for reading; interchange output
                        is compact by default, as the reference's is
  --compact             one-line JSON (the default; kept for old scripts)
  --timings, --include-timings
                        include wall-clock timings in the document; off by
                        default so two runs of one seed are byte-identical
  --include-build-stages
                        embed the stage inspector's per-stage snapshots as
                        `build_stages` (several times the document's size)
  --threads N           worker threads for the stage-16 planners: 0 is every
                        hardware thread, 1 is serial (default: FWG_THREADS,
                        else 0). Not part of the world: the document is
                        byte-identical at every count
  --quiet               no progress on stderr
  -h, --help            this text

the sixteen-stage ladder:
  1  plate layout                 9  leylines, astrology, ecology,
  2  hypsometric surface            biome variants
  3  surface detail              10  founding: cities
  4  erosion                     11  roads
  5  land mask, water            12  populated regions
  6  tectonic transition         13  nests, beasts, religion
  7  relic carving               14  an age turns: wars, fates,
  8  climate, biomes                 ruins, villains, rebuilding
                                 15  the second, final age turn
                                 16  plans, bands, heroes, NPCs,
                                     key locations
See docs/porting-status.md.
)";
}

std::optional<double> to_double(std::string_view text) {
    // from_chars for double is not available everywhere yet; strtod is, and this is
    // argument parsing, not a hot path.
    std::string owned(text);
    char* end = nullptr;
    const double value = std::strtod(owned.c_str(), &end);
    if (end == owned.c_str() || *end != '\0') return std::nullopt;
    return value;
}

std::optional<long long> to_int(std::string_view text) {
    long long value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return std::nullopt;
    return value;
}

// The registry, plus a deterministic grid of habitat probes. The probes are emitted
// here rather than kept in a test because they are what makes the eligibility rules
// inspectable from outside the library at all -- and comparable against any other
// implementation without either side choosing the cases.
void dump_registry(std::ostream& out) {
    fwg::JsonWriter json(out, true);
    const fwg::RegistryIdentity identity = fwg::registry_identity();

    json.begin_object();
    json.field("schema", std::string_view("fwg.civilization-registry"));
    json.key("identity");
    json.begin_object();
    json.field("schema_version", identity.schema_version);
    json.field("revision", identity.revision);
    json.field("source_sha256", identity.source_sha256);
    json.field("sha256", identity.sha256);
    json.end_object();
    json.field("default_profile_id", fwg::default_profile_id());

    json.key("profiles");
    json.begin_object();
    for (const fwg::CivilizationProfile& p : fwg::civilization_profiles()) {
        json.key(p.id);
        json.begin_object();
        json.field("name", p.name);
        json.field("kind", std::string_view(p.is_entity ? "entity" : "aggregate"));
        json.field("allocation_group", p.allocation_group);
        json.field("structure_blocks", p.structure_blocks);
        json.field("priority", p.priority);
        json.key("traits");
        json.begin_object();
        const fwg::CivilizationTraits& t = p.traits;
        json.field("climate_weight", t.climate_weight);
        json.field("college_flood_limit", t.college_flood_limit);
        json.field("college_slope_limit", t.college_slope_limit);
        json.field("college_suitability_min", t.college_suitability_min);
        json.field("college_temperature_max", t.college_temperature_max);
        json.field("college_temperature_min", t.college_temperature_min);
        json.field("college_water_reach", t.college_water_reach);
        json.field("difficult_fraction", t.difficult_fraction);
        json.field("flood_penalty", t.flood_penalty);
        json.field("food_demand", t.food_demand);
        json.field("food_moisture_ideal", t.food_moisture_ideal);
        json.field("food_slope_comfort", t.food_slope_comfort);
        json.field("food_temperature_ideal", t.food_temperature_ideal);
        json.field("food_temperature_tolerance", t.food_temperature_tolerance);
        json.field("irrigation", t.irrigation);
        json.field("land_per_city_km2", t.land_per_city_km2);
        json.field("magic_penalty", t.magic_penalty);
        json.field("minimum_founding_residents", t.minimum_founding_residents);
        json.field("moisture_ideal", t.moisture_ideal);
        json.field("moisture_weight", t.moisture_weight);
        json.field("mutation_limit", t.mutation_limit);
        json.field("resource_weight", t.resource_weight);
        json.field("road_grade_limit", t.road_grade_limit);
        json.field("site_slope_limit", t.site_slope_limit);
        json.field("slope_comfort", t.slope_comfort);
        json.field("slope_weight", t.slope_weight);
        json.field("support_multiplier", t.support_multiplier);
        json.field("temperature_ideal", t.temperature_ideal);
        json.field("temperature_tolerance", t.temperature_tolerance);
        json.field("water_reach", t.water_reach);
        json.field("water_weight", t.water_weight);
        json.field("work_slope_limit", t.work_slope_limit);
        json.end_object();
        json.key("biome_preferences");
        json.begin_object();
        for (const fwg::BiomeOpinion& o : p.biome_preferences) json.field(std::to_string(o.biome_id), o.value);
        json.end_object();
        json.key("food_biome_multipliers");
        json.begin_object();
        for (const fwg::BiomeOpinion& o : p.food_biome_multipliers) json.field(std::to_string(o.biome_id), o.value);
        json.end_object();
        json.key("magic_biome_preferences");
        json.begin_object();
        for (const fwg::VariantOpinion& o : p.magic_biome_preferences) json.field(o.variant_id, o.value);
        json.end_object();
        json.key("food_magic_biome_multipliers");
        json.begin_object();
        for (const fwg::VariantOpinion& o : p.food_magic_biome_multipliers) json.field(o.variant_id, o.value);
        json.end_object();
        json.end_object();
    }
    json.end_object();

    json.key("habitat_probes");
    json.begin_array();
    for (int biome = 0; biome <= 17; ++biome) {
        for (int temp = -30; temp <= 30; temp += 10) {
            for (int moist = 0; moist <= 10; moist += 2) {
                for (int maritime = 0; maritime <= 1; ++maritime) {
                    fwg::HabitatSample s;
                    s.biome = biome;
                    s.temperature = temp;
                    s.moisture = moist / 10.0;
                    s.maritime = maritime;
                    s.landmass_area_m2 = maritime ? 5e9 : 5e11;
                    s.landmass_fraction = maritime ? 0.02 : 0.6;
                    s.largest_landmass = maritime ? 0.0 : 1.0;
                    json.begin_object();
                    json.field("biome", biome);
                    json.field("temperature", temp);
                    json.field("moisture", s.moisture);
                    json.field("maritime", maritime);
                    json.field("landmass_area_m2", s.landmass_area_m2);
                    json.field("landmass_fraction", s.landmass_fraction);
                    json.field("largest_landmass", s.largest_landmass);
                    json.key("eligible");
                    json.begin_array();
                    for (const std::string_view id : fwg::eligible_civilizations(s)) json.value(id);
                    json.end_array();
                    json.end_object();
                }
            }
        }
    }
    json.end_array();
    json.end_object();
    out << '\n';
}

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "fwg: " << message << '\n';
    std::exit(2);
}

// --- the product commands -----------------------------------------------------------------

// The reference writes these with json.dumps(value, ensure_ascii=False, indent=2) and a
// trailing newline, creating the parent directory first. Always LF here; the reference's
// text-mode write gives CRLF on Windows and LF elsewhere.
std::string indented(const fwg::JsonValue& value) {
    fwg::JsonDumpOptions options;
    options.ensure_ascii = false;
    options.indent = 2;
    return fwg::dump_json(value, options) + "\n";
}

void write_file(const std::string& path, const std::string& text) {
    const std::filesystem::path target(path);
    std::error_code ignored;
    if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), ignored);
    std::ofstream file(target, std::ios::binary);
    if (!file) fail("cannot write " + path);
    file << text;
    if (!file) fail("cannot write " + path);
}

// A refused request: the machine-readable envelope on stdout for the caller to parse, the
// one-line reason on stderr for whoever is watching, and exit 2 -- the reference's shape.
int refuse(const fwg::PatchError& error, bool terse) {
    fwg::JsonDumpOptions options;
    options.ensure_ascii = false;
    std::cout << fwg::dump_json(fwg::refusal_document(error.refusal(), !terse), options) << '\n';
    std::cerr << error.refusal().code << ": " << error.what() << '\n';
    return 2;
}

// `--flag VALUE` pairs and bare switches, for the three small commands.
struct Options {
    std::vector<std::pair<std::string_view, std::string_view>> values;
    std::vector<std::string_view> switches;
};

Options parse_options(const std::vector<std::string_view>& args, std::initializer_list<std::string_view> valued,
                      std::initializer_list<std::string_view> bare) {
    Options out;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (std::find(valued.begin(), valued.end(), arg) != valued.end()) {
            if (i + 1 >= args.size()) fail(std::string(arg) + " needs a value");
            out.values.emplace_back(arg, args[++i]);
        } else if (std::find(bare.begin(), bare.end(), arg) != bare.end()) {
            out.switches.push_back(arg);
        } else {
            fail("unknown option '" + std::string(arg) + "' for " + std::string(args[0]));
        }
    }
    return out;
}

std::optional<std::string_view> last_value(const Options& options, std::string_view flag) {
    std::optional<std::string_view> found;
    for (const auto& [name, value] : options.values) {
        if (name == flag) found = value;
    }
    return found;
}

int run_asset_list(const std::vector<std::string_view>& args) {
    const Options options = parse_options(args, {"--output"}, {});
    const auto output = last_value(options, "--output");
    if (!output) fail("asset-list needs --output PATH");
    try {
        write_file(std::string(*output), indented(fwg::compile_asset_list()));
    } catch (const std::exception& error) {
        fail(error.what());
    }
    return 0;
}

int run_capabilities(const std::vector<std::string_view>& args) {
    const Options options = parse_options(args, {"--output", "--version"}, {});
    const auto output = last_value(options, "--output");
    if (!output) fail("capabilities needs --output PATH");
    int version = fwg::kCapabilityVersion;
    if (const auto text = last_value(options, "--version")) {
        const auto parsed = to_int(*text);
        if (!parsed || *parsed < -2147483647LL || *parsed > 2147483647LL) fail("--version needs an integer");
        version = static_cast<int>(*parsed);
    }
    try {
        // Resolved before the file is touched: an unsupported version never overwrites.
        const std::string text = indented(fwg::capabilities_document(version));
        write_file(std::string(*output), text);
    } catch (const std::exception& error) {
        fail(error.what());
    }
    return 0;
}

// A request file as generate --request takes it: {"seed", "recipe_version", "overrides"}.
fwg::Request read_request(const std::string& path) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) fail("cannot read " + path);
    std::ostringstream text;
    text << file.rdbuf();
    return fwg::request_from_json(fwg::parse_json(text.str()));
}

// The default is the highest stage that actually runs, so `fwg generate` with no arguments
// produces the most complete world this port can make rather than the most complete one it
// could make when the flag was last touched. Set only where the library's own default says
// otherwise: the document's recipe.overrides lists what the request set, and a phase nobody
// typed is not an override anybody asked for. Applied after a request file is read, so a
// file without a phase gets the same default as flags -- and `fwg patch` regenerates the
// same world from a file that `fwg generate --request` made.
void default_to_highest_stage(fwg::Request& request) {
    if (!request.phase && fwg::Config{}.phase != fwg::kHighestImplementedStage) {
        request.phase = fwg::kHighestImplementedStage;
    }
}

// The library's patch_request: the world regenerated from its request, then the patch cut.
int run_patch(const std::vector<std::string_view>& args, bool terse) {
    const Options options = parse_options(
        args, {"--request", "--latitude", "--longitude", "--span", "--spacing", "--detail-height", "--detail-scale", "--output"},
        {});
    const auto request_path = last_value(options, "--request");
    if (!request_path) fail("patch needs --request PATH, a request as generate --request takes it");
    fwg::PatchConfig cfg;
    const auto number = [&options](std::string_view flag, double& into) {
        if (const auto text = last_value(options, flag)) {
            const auto v = to_double(*text);
            if (!v) fail(std::string(flag) + " needs a number");
            into = *v;
        }
    };
    number("--latitude", cfg.latitude);
    number("--longitude", cfg.longitude);
    number("--span", cfg.span);
    number("--spacing", cfg.spacing);
    number("--detail-height", cfg.detail_height);
    number("--detail-scale", cfg.detail_scale);
    try {
        fwg::Request request = read_request(std::string(*request_path));
        default_to_highest_stage(request);
        const fwg::Patch patch = fwg::patch_request(request, cfg);
        std::ostringstream text;
        fwg::write_patch_json(text, patch);
        if (const auto output = last_value(options, "--output")) {
            write_file(std::string(*output), text.str());
        } else {
            std::cout << text.str();
        }
    } catch (const fwg::PatchError& error) {
        return refuse(error, terse);
    } catch (const std::exception& error) {
        fail(error.what());
    }
    return 0;
}

// A refused read, in the failure envelope's terse tier -- code, message and field, which is all
// the read envelope carries -- on stdout, the reason on stderr, and exit 2.
int refuse(const fwg::WorldTimeError& error) {
    fwg::PatchRefusal refusal;
    refusal.code = error.code();
    refusal.field = error.field();
    refusal.message = error.message();
    fwg::JsonDumpOptions options;
    options.ensure_ascii = false;
    std::cout << fwg::dump_json(fwg::refusal_document(refusal, /*detail=*/false), options) << '\n';
    std::cerr << error.code() << ": " << error.what() << '\n';
    return 2;
}

// The library's weather read: the world regenerated from its request, then its weather hour by
// hour from a moment -- each hour's systems and, given a place, that place's weather then.
int run_weather(const std::vector<std::string_view>& args, bool terse) {
    const Options options =
        parse_options(args, {"--request", "--day", "--hours", "--latitude", "--longitude", "--output"}, {});
    const auto request_path = last_value(options, "--request");
    if (!request_path) fail("weather needs --request PATH, a request as generate --request takes it");
    const auto number = [&options](std::string_view flag) -> std::optional<double> {
        const auto text = last_value(options, flag);
        if (!text) return std::nullopt;
        const auto v = to_double(*text);
        if (!v) fail(std::string(flag) + " needs a number");
        return v;
    };
    const auto day = number("--day");
    const auto latitude = number("--latitude");
    const auto longitude = number("--longitude");
    if (latitude.has_value() != longitude.has_value()) fail("weather needs --latitude and --longitude together");
    long long hours = 1;
    if (const auto text = last_value(options, "--hours")) {
        const auto v = to_int(*text);
        if (!v || *v < 1 || *v > 8760) fail("--hours needs an integer 1..8760");
        hours = *v;
    }
    try {
        // Refused before a world is spent on it, by the read's own checks.
        if (day) fwg::weather_require_day(*day);
        if (latitude) fwg::weather_require_place(*latitude, *longitude);

        fwg::Request request = read_request(std::string(*request_path));
        default_to_highest_stage(request);
        const fwg::World world = fwg::generate(fwg::resolve(request));
        const double from = day ? *day : fwg::world_clock_of(world).day;
        std::vector<fwg::WeatherMoment> moments;
        for (long long h = 0; h < hours; ++h) {
            fwg::WeatherMoment moment;
            moment.report = fwg::weather_report(world, from + static_cast<double>(h) / 24.0);
            if (latitude) moment.samples.push_back(fwg::weather_sample(world, moment.report, *latitude, *longitude));
            moments.push_back(std::move(moment));
        }
        std::ostringstream text;
        fwg::write_weather_json(text, moments);
        text << '\n';
        if (const auto output = last_value(options, "--output")) {
            write_file(std::string(*output), text.str());
        } else {
            std::cout << text.str();
        }
    } catch (const fwg::WorldTimeError& error) {
        return refuse(error);
    } catch (const fwg::PatchError& error) {
        return refuse(error, terse);
    } catch (const std::exception& error) {
        fail(error.what());
    }
    return 0;
}

int run_controls(const std::vector<std::string_view>& args) {
    const Options options =
        parse_options(args, {"--group", "--name", "--query", "--output"}, {"--include-pinned"});
    fwg::ControlQuery query;
    for (const auto& [flag, value] : options.values) {
        if (flag == "--group") query.group = std::string(value);
        if (flag == "--query") query.query = std::string(value);
        if (flag == "--name") {
            if (!query.names) query.names.emplace();
            query.names->emplace_back(value);
        }
    }
    query.include_pinned = !options.switches.empty();
    try {
        const std::string text = indented(fwg::describe_controls(query));
        if (const auto output = last_value(options, "--output")) {
            write_file(std::string(*output), text);
        } else {
            std::cout << text;
        }
    } catch (const std::exception& error) {
        fail(error.what());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string_view> args(argv + 1, argv + argc);
    // The reference's one global option, which precedes the command.
    bool terse = false;
    while (!args.empty() && args[0] == "--terse-errors") {
        terse = true;
        args.erase(args.begin());
    }
    if (args.empty() || args[0] == "-h" || args[0] == "--help") {
        usage();
        return args.empty() ? 2 : 0;
    }
    if (args[0] == "registry") {
        dump_registry(std::cout);
        return 0;
    }
    if (args[0] == "materialize") {
        // The stage inspector's replay, from outside the library: the finished document's
        // non-state blocks plus the first N snapshots.
        std::string in_path;
        std::string out_path;
        long long stage = 0;
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--stage" && i + 1 < args.size()) {
                const auto v = to_int(args[++i]);
                if (!v) fail("--stage needs an integer");
                stage = *v;
            } else if ((args[i] == "-o" || args[i] == "--out") && i + 1 < args.size()) {
                out_path = std::string(args[++i]);
            } else if (in_path.empty() && !args[i].starts_with("-")) {
                in_path = std::string(args[i]);
            } else {
                fail("unknown argument '" + std::string(args[i]) + "'; try --help");
            }
        }
        if (in_path.empty()) fail("materialize needs a document");
        std::ifstream file(in_path, std::ios::binary);
        if (!file) fail("cannot read " + in_path);
        const std::string document((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        // Range-checked BEFORE the narrowing cast, as --size is: a stage past int would wrap
        // to a small one and replay a stage nobody asked for. Zero is refused as ungenerated.
        const int wanted = stage >= 1 && stage <= 1000000 ? static_cast<int>(stage) : 0;
        try {
            const std::string stage_document = fwg::materialize_stage(document, wanted);
            if (out_path.empty()) {
                std::cout << stage_document << '\n';
            } else {
                std::ofstream out(out_path, std::ios::binary);
                if (!out) fail("cannot write " + out_path);
                out << stage_document << '\n';
            }
        } catch (const std::exception& error) {
            fail(error.what());
        }
        return 0;
    }
    if (args[0] == "asset-list") return run_asset_list(args);
    if (args[0] == "capabilities") return run_capabilities(args);
    if (args[0] == "controls") return run_controls(args);
    if (args[0] == "patch") return run_patch(args, terse);
    if (args[0] == "weather") return run_weather(args, terse);
    if (args[0] != "generate") fail("unknown command '" + std::string(args[0]) + "'; try --help");

    fwg::Request request;
    std::string out_path;
    std::string split_dir;
    std::string request_path;
    // Compact unless --pretty, as the reference's interchange output is.
    bool pretty = false;
    bool quiet = false;
    bool timings = false;
    bool build_stages = false;
    std::vector<std::string> shaping_flags;  // what --request cannot be combined with

    const auto next = [&](std::size_t& i, std::string_view flag) -> std::string_view {
        if (i + 1 >= args.size()) fail(std::string(flag) + " needs a value");
        return args[++i];
    };

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        // Range-checked BEFORE the narrowing cast. `static_cast<int>` of a parsed
        // long long silently wraps, so --size 4294967313 became size 17 and built a
        // world nobody asked for; the library's own validate() never saw the value
        // the user typed. Refuse at the boundary instead.
        const auto need_int = [&](std::string_view flag, long long low, long long high) {
            const auto v = to_int(next(i, flag));
            if (!v) fail(std::string(flag) + " needs an integer");
            if (*v < low || *v > high) {
                fail(std::string(flag) + " must be " + std::to_string(low) + ".." + std::to_string(high) +
                     "; received " + std::to_string(*v));
            }
            return *v;
        };
        const auto need_double = [&](std::string_view flag) {
            const auto v = to_double(next(i, flag));
            if (!v) fail(std::string(flag) + " needs a number");
            return *v;
        };
        const bool shapes = arg == "--seed" || arg == "--size" || arg == "--phase" || arg == "--plates" ||
                            arg == "--erosion-passes" || arg == "--erosion-strength" || arg == "--sea-level" ||
                            arg == "--circumference-km" || arg == "--relief-m" || arg == "--world-size" ||
                            arg == "--magic-enabled" || arg == "--settlement-count" ||
                            arg == "--population-profile" || arg == "--option";
        if (shapes) shaping_flags.emplace_back(arg);

        if (arg == "--seed") request.seed = static_cast<std::uint32_t>(need_int(arg, 0, 4294967295LL));
        else if (arg == "--size") request.size = static_cast<int>(need_int(arg, 3, 1025));
        else if (arg == "--phase") request.phase = static_cast<int>(need_int(arg, 1, 16));
        else if (arg == "--plates") request.plate_count = static_cast<int>(need_int(arg, 2, 64));
        else if (arg == "--erosion-passes") request.erosion_passes = static_cast<int>(need_int(arg, 0, 40));
        else if (arg == "--erosion-strength") request.erosion_strength = need_double(arg);
        else if (arg == "--sea-level") request.sea_level = need_double(arg);
        else if (arg == "--circumference-km") request.circumference_km = need_double(arg);
        else if (arg == "--relief-m") request.relief_m = need_double(arg);
        else if (arg == "--world-size") {
            const auto parsed = fwg::parse_world_size(next(i, arg));
            if (!parsed) fail("--world-size must be small, medium or large");
            request.world_size = *parsed;
        }
        else if (arg == "--magic-enabled") request.magic_enabled = static_cast<int>(need_int(arg, 0, 1));
        else if (arg == "--settlement-count") request.settlement_count = static_cast<int>(need_int(arg, 0, 100000));
        else if (arg == "--population-profile") request.population_profile = std::string(next(i, arg));
        else if (arg == "--option") {
            // NAME=VALUE, repeatable. The whole 164-knob surface from the command line,
            // rather than a flag per knob; resolve() refuses an unknown name or an
            // out-of-range value, so a typo is an error and never a default-shaped world.
            const std::string_view pair = next(i, arg);
            const auto eq = pair.find('=');
            if (eq == std::string_view::npos) fail("--option needs NAME=VALUE, got '" + std::string(pair) + "'");
            const std::string name(pair.substr(0, eq));
            try {
                request.options[name] = std::stod(std::string(pair.substr(eq + 1)));
            } catch (const std::exception&) {
                fail("--option " + name + " needs a number, got '" + std::string(pair.substr(eq + 1)) + "'");
            }
        }
        else if (arg == "--request") request_path = std::string(next(i, arg));
        else if (arg == "-o" || arg == "--out" || arg == "--output") out_path = std::string(next(i, arg));
        else if (arg == "--split") split_dir = std::string(next(i, arg));
        else if (arg == "--compact") pretty = false;
        else if (arg == "--pretty") pretty = true;
        else if (arg == "--timings" || arg == "--include-timings") timings = true;
        else if (arg == "--include-build-stages") build_stages = true;
        // A process setting, not a request field: the request is written into the document
        // and the thread count must never be.
        else if (arg == "--threads") fwg::set_worker_threads(static_cast<unsigned>(need_int(arg, 0, 4096)));
        else if (arg == "--terse-errors") terse = true;
        else if (arg == "--quiet") quiet = true;
        else if (arg == "-h" || arg == "--help") { usage(); return 0; }
        else fail("unknown option '" + std::string(arg) + "'");
    }

    try {
        if (!request_path.empty()) {
            if (!shaping_flags.empty()) {
                fail("--request cannot be combined with " + shaping_flags.front() +
                     "; the request file is the whole request");
            }
            request = read_request(request_path);
        }

        default_to_highest_stage(request);

        const fwg::Config cfg = fwg::resolve(request);
        // Resolved now, so a malformed FWG_THREADS is refused before a world is spent on it.
        const unsigned workers = fwg::worker_threads();

        if (!split_dir.empty()) {
            if (!out_path.empty()) fail("--split writes a directory; it cannot be combined with -o/--output");
            if (build_stages) fail("--split does not carry build stages; drop --include-build-stages");
            if (timings) fail("--split does not carry timings; drop --timings");
            // Judged before spending a generation on it, as the single document's path is.
            std::error_code ignored;
            const std::filesystem::path dir(split_dir);
            if (std::filesystem::exists(dir, ignored)) {
                if (!std::filesystem::is_directory(dir, ignored)) {
                    fail("--split " + split_dir + " is a file; give a new or empty directory");
                }
                if (!std::filesystem::is_empty(dir, ignored)) {
                    fail("--split " + split_dir + " already holds files; give a new or empty directory");
                }
            }
        }

        // Fail on an unwritable destination before spending a whole generation on it.
        // Opened for append, so a failed generation does not truncate what was there.
        if (!out_path.empty()) {
            const std::filesystem::path target(out_path);
            std::error_code ignored;
            if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), ignored);
            std::ofstream probe(target, std::ios::binary | std::ios::app);
            if (!probe) fail("cannot write " + out_path);
        }

        if (!quiet) {
            std::cerr << "seed " << cfg.seed << ", size " << cfg.size << ", phase " << cfg.phase
                      << ", radius " << cfg.globe_radius * cfg.world_scale << " m, " << workers
                      << (workers == 1 ? " worker thread\n" : " worker threads\n");
        }

        // The stage inspector's snapshots, only when asked for: each finished stage's world
        // is serialised and split at its top-level keys, and only what changed is kept.
        fwg::BuildStageRecorder recorder;
        fwg::StageObserver observer;
        if (build_stages) {
            observer = [&recorder](int stage, const fwg::World& stage_world) { recorder.capture(stage, stage_world); };
        }
        const fwg::World world = fwg::generate(cfg, observer);

        if (!quiet) {
            std::cerr << "generated in " << world.timing_ms.at("total") << " ms; land "
                      << world.area.land_fraction * 100.0 << "% of "
                      << world.area.total_km2 << " km2; " << world.resolved_octaves << '/'
                      << cfg.octaves << " octaves resolved\n";
            if (build_stages) std::cerr << "captured " << recorder.stages().size() << " build stages\n";
            for (const std::string& warning : world.warnings) std::cerr << "  note: " << warning << '\n';
        }

        if (!split_dir.empty()) {
            const fwg::SplitExport split = fwg::write_world_split(split_dir, world, pretty);
            if (!quiet) {
                std::cerr << "wrote " << split.files.size() << " files to " << split_dir << ": world.json "
                          << split.world_bytes / 1000000.0 << " MB of " << split.total_bytes / 1000000.0 << " MB\n";
            }
            return 0;
        }

        fwg::DocumentOptions document;
        document.pretty = pretty;
        document.include_timings = timings;
        document.build_stages = build_stages ? &recorder.stages() : nullptr;
        if (out_path.empty()) {
            fwg::write_world_json(std::cout, world, document);
        } else {
            std::ofstream file(out_path, std::ios::binary);
            if (!file) fail("cannot write " + out_path);
            fwg::write_world_json(file, world, document);
            if (!quiet) std::cerr << "wrote " << out_path << '\n';
        }
    } catch (const fwg::PatchError& error) {
        return refuse(error, terse);
    } catch (const std::exception& error) {
        fail(error.what());
    }
    return 0;
}
