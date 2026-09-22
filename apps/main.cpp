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

#include "fwg/serialize.hpp"
#include "fwg/world.hpp"

namespace {

void usage() {
    std::cout <<
        R"(fwg -- fantasy world generator

usage:
  fwg generate [options]

options:
  --seed N              master seed (default 42)
  --size N              grid cells along one edge, odd, 3..1025 (default 129)
  --phase N             stop after stage N, 1..16 (default 5; see below)
  --world-size NAME     small | medium | large (default small)
  --circumference-km X  authored world width; overrides --world-size
  --relief-m X          authored relief budget (default 1667)
  --plates N            plate count (default 12)
  --erosion-passes N    0..40 (default 6)
  --erosion-strength X  0..1 (default 0.5)
  --sea-level X         design-space datum (default 0)
  -o, --out PATH        write JSON here (default stdout)
  --compact             one-line JSON
  --timings             include wall-clock timings in the document; off by
                        default so two runs of one seed are byte-identical
  --quiet               no progress on stderr
  -h, --help            this text

the sixteen-stage ladder, and where this port has reached:
  1  plate layout           done     9  networks, astrology     todo
  2  hypsometric surface    done    10  biome variants, civs    todo
  3  surface detail         done    11  civilizations           todo
  4  erosion                done    12  civilizations           todo
  5  land mask and area     done    13  nests, religion         todo
  6  tectonic transition    todo    14  age transition          todo
  7  relic carving          todo    15  age transition          todo
  8  climate, biomes        todo    16  settlements, heroes     todo
A --phase above 5 runs what exists, reports it in completed_stage, and says so
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
        const auto need_int = [&](std::string_view flag) {
            const auto v = to_int(next(i, flag));
            if (!v) fail(std::string(flag) + " needs an integer");
            return *v;
        };
        const auto need_double = [&](std::string_view flag) {
            const auto v = to_double(next(i, flag));
            if (!v) fail(std::string(flag) + " needs a number");
            return *v;
        };

        if (arg == "--seed") request.seed = static_cast<std::uint32_t>(need_int(arg));
        else if (arg == "--size") request.size = static_cast<int>(need_int(arg));
        else if (arg == "--phase") request.phase = static_cast<int>(need_int(arg));
        else if (arg == "--plates") request.plate_count = static_cast<int>(need_int(arg));
        else if (arg == "--erosion-passes") request.erosion_passes = static_cast<int>(need_int(arg));
        else if (arg == "--erosion-strength") request.erosion_strength = need_double(arg);
        else if (arg == "--sea-level") request.sea_level = need_double(arg);
        else if (arg == "--circumference-km") request.circumference_km = need_double(arg);
        else if (arg == "--relief-m") request.relief_m = need_double(arg);
        else if (arg == "--world-size") {
            const auto parsed = fwg::parse_world_size(next(i, arg));
            if (!parsed) fail("--world-size must be small, medium or large");
            request.world_size = *parsed;
        }
        else if (arg == "-o" || arg == "--out") out_path = std::string(next(i, arg));
        else if (arg == "--compact") pretty = false;
        else if (arg == "--timings") timings = true;
        else if (arg == "--quiet") quiet = true;
        else if (arg == "-h" || arg == "--help") { usage(); return 0; }
        else fail("unknown option '" + std::string(arg) + "'");
    }

    if (!request.phase) request.phase = 5;

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
