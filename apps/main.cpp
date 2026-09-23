// fwg -- generate a world and write it out.
//
// The CLI is deliberately thin: everything it does is available through the library
// headers, and nothing it does is available only here.
#include <charconv>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/civilizations.hpp"
#include "fwg/json.hpp"
#include "fwg/serialize.hpp"
#include "fwg/world.hpp"

namespace {

void usage() {
    std::cout <<
        R"(fwg -- fantasy world generator

usage:
  fwg generate [options]
  fwg registry            dump the civilization registry as JSON

options:
  --seed N              master seed (default 42)
  --size N              grid cells along one edge, odd, 3..1025 (default 129)
  --phase N             stop after stage N, 1..16 (default 11; see below)
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
  -o, --out PATH        write JSON here (default stdout)
  --compact             one-line JSON
  --timings             include wall-clock timings in the document; off by
                        default so two runs of one seed are byte-identical
  --quiet               no progress on stderr
  -h, --help            this text

the sixteen-stage ladder, and where this port has reached:
  1  plate layout           done     9  leylines, astrology,    done
  2  hypsometric surface    done        ecology, biome variants
  3  surface detail         done    10  founding: cities        done
  4  erosion                done    11  roads                   done
  5  land mask, water       done    12  populated regions       todo
  6  tectonic transition    done    13  nests, beasts, religion todo
  7  relic carving          done    14  age transition          todo
  8  climate, biomes        done    15  age transition          todo
                                    16  heroes, NPCs, quests    todo
A --phase above 11 runs what exists, reports it in completed_stage, and says so
in warnings. See docs/porting-status.md.
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

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string_view> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "-h" || args[0] == "--help") {
        usage();
        return args.empty() ? 2 : 0;
    }
    if (args[0] == "registry") {
        dump_registry(std::cout);
        return 0;
    }
    if (args[0] != "generate") fail("unknown command '" + std::string(args[0]) + "'; try --help");

    fwg::Request request;
    std::string out_path;
    bool pretty = true;
    bool quiet = false;
    bool timings = false;

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
        else if (arg == "-o" || arg == "--out") out_path = std::string(next(i, arg));
        else if (arg == "--compact") pretty = false;
        else if (arg == "--timings") timings = true;
        else if (arg == "--quiet") quiet = true;
        else if (arg == "-h" || arg == "--help") { usage(); return 0; }
        else fail("unknown option '" + std::string(arg) + "'");
    }

    // The default is the highest stage that actually runs, so `fwg generate` with no
    // arguments produces the most complete world this port can make rather than the
    // most complete one it could make when the flag was last touched.
    if (!request.phase) request.phase = fwg::kHighestImplementedStage;

    try {
        const fwg::Config cfg = fwg::resolve(request);
        if (!quiet) {
            std::cerr << "seed " << cfg.seed << ", size " << cfg.size << ", phase " << cfg.phase
                      << ", radius " << cfg.globe_radius * cfg.world_scale << " m\n";
        }

        const fwg::World world = fwg::generate(cfg);

        if (!quiet) {
            std::cerr << "generated in " << world.timing_ms.at("total") << " ms; land "
                      << world.area.land_fraction * 100.0 << "% of "
                      << world.area.total_km2 << " km2; " << world.resolved_octaves << '/'
                      << cfg.octaves << " octaves resolved\n";
            for (const std::string& warning : world.warnings) std::cerr << "  note: " << warning << '\n';
        }

        if (out_path.empty()) {
            fwg::write_world_json(std::cout, world, pretty, timings);
        } else {
            std::ofstream file(out_path, std::ios::binary);
            if (!file) fail("cannot write " + out_path);
            fwg::write_world_json(file, world, pretty, timings);
            if (!quiet) std::cerr << "wrote " << out_path << '\n';
        }
    } catch (const std::exception& error) {
        fail(error.what());
    }
    return 0;
}
