// The split export (serialize.hpp, write_world_split) against the whole document it splits.
//
// A game reads the split export in parts. What it relies on is that the parts put back
// together ARE the document: every block, every plan row, in the document's order, and
// nothing missing but the catalogue copies a plan row carries -- each of which world.json
// holds once, at the place serialize.hpp names. One world is written both ways here and the
// two are held equal as parsed JSON, member order included.
#include "check.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/json_value.hpp"
#include "fwg/serialize.hpp"
#include "fwg/world.hpp"

using namespace fwg;
namespace fs = std::filesystem;

namespace {

// Size 17 has every plan kind -- 13 cities, 5 hamlets, 11 castles and 60 key-location plans
// for seed 42 -- and a world scene, in a document a third the size of size 33's.
const World& split_world() {
    static const World world = [] {
        Request request;
        request.seed = 42;
        request.size = 17;
        return generate(resolve(request));
    }();
    return world;
}

std::string read_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) check::fail("cannot read " + path.string());
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

JsonValue read_json(const fs::path& path) { return parse_json(read_file(path)); }

void write_file(const fs::path& path, std::string_view text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
}

// A directory of its own under the system's temporary one, removed with this object. The
// name is drawn fresh, so two suites running at once never share one.
class ScratchDir {
public:
    ScratchDir() {
        std::random_device entropy;
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0;; ++attempt) {
            path_ = fs::temp_directory_path() /
                    ("fwg-split-" + std::to_string(static_cast<std::uint64_t>(tick)) + "-" + std::to_string(entropy()));
            if (fs::create_directory(path_)) return;
            if (attempt == 20) check::fail("no fresh scratch directory under " + fs::temp_directory_path().string());
        }
    }
    ~ScratchDir() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }
    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

// The world written whole, and written split into a directory that did not exist.
struct BothWays {
    ScratchDir scratch;
    std::size_t whole_bytes = 0;
    JsonValue whole;
    fs::path dir;
    SplitExport result;
    JsonValue world;  // the split's world.json

    BothWays() {
        std::ostringstream out;
        write_world_json(out, split_world(), DocumentOptions{});
        whole_bytes = out.str().size();
        whole = parse_json(out.str());
        dir = scratch.path() / "world";
        result = write_world_split(dir, split_world());
        world = read_json(dir / "world.json");
    }
};

const BothWays& both() {
    static const BothWays fixture;
    return fixture;
}

// Each plan block: its rows' key in the whole document, the key its split listing names each
// file by, the folder its files go in, and which catalogue copies its rows carry.
struct PlanBlock {
    std::string_view block;
    std::string_view rows;
    std::string_view join;
    std::string_view folder;
    bool catalogues;       // biome_catalogue, magical_catalogue and magic_colors on the terrain
    bool terrain_detail;   // the detail definition on the terrain's surface
};

constexpr PlanBlock kPlanBlocks[] = {
    {"city_plans", "cities", "city_uid", "plans/city/", true, true},
    {"hamlet_plans", "hamlets", "hamlet_id", "plans/hamlet/", true, true},
    {"castle_plans", "castles", "fortress_id", "plans/castle/", true, false},
    {"key_location_plans", "plans", "location_id", "plans/key_location/", false, false},
};

bool is_plan_block(std::string_view key) {
    for (const PlanBlock& b : kPlanBlocks) {
        if (b.block == key) return true;
    }
    return false;
}

std::vector<std::string> keys_of(const JsonValue& object) {
    std::vector<std::string> keys;
    for (const auto& [key, value] : object.as_object()) keys.push_back(key);
    return keys;
}

// Every regular file under `dir`, as a path relative to it with forward slashes.
std::set<std::string> files_under(const fs::path& dir) {
    std::set<std::string> files;
    for (const fs::directory_entry& e : fs::recursive_directory_iterator(dir)) {
        if (e.is_regular_file()) files.insert(fs::relative(e.path(), dir).generic_string());
    }
    return files;
}

}  // namespace

TEST(a_split_export_holds_every_block_of_the_whole_document_in_its_order) {
    const BothWays& b = both();
    const World& w = split_world();
    // Non-vacuity: every part the split writes exists in this world.
    check::is_true(w.has_plans && w.has_rural_plans, "city, hamlet and castle plans");
    check::is_true(w.has_key_locations && w.key_locations.ok && w.key_location_plans.ok, "key-location plans");
    check::is_true(w.has_world_scene, "a world scene");

    // The split's own record, after the document's identity.
    const JsonValue* split = b.world.find("split");
    check::is_true(split != nullptr, "a split record in world.json");
    check::is_true(*split == JsonValue(JsonValue::Object{{"version", 1}, {"scene", "scene.json"}}),
                   "the split record names its version and the scene's file");

    // The whole document's keys, in its order, with the split record after completed_stage
    // and the world scene gone to its own file.
    std::vector<std::string> expected;
    for (const std::string& key : keys_of(b.whole)) {
        if (key == "world_scene") continue;
        expected.push_back(key);
        if (key == "completed_stage") expected.push_back("split");
    }
    check::is_true(keys_of(b.world) == expected, "world.json's keys to be the whole document's, in its order");

    // Every block but the plans is the whole document's, value for value.
    for (const auto& [key, value] : b.whole.as_object()) {
        if (key == "world_scene" || is_plan_block(key)) continue;
        check::is_true(b.world.at(key) == value, "world.json's '" + key + "' to be the whole document's");
    }
    check::is_true(read_json(b.dir / "scene.json") == b.whole.at("world_scene"), "scene.json to be the world scene");
}

TEST(each_plan_row_splits_into_its_own_file_less_only_the_catalogues_world_json_holds_once) {
    const BothWays& b = both();
    // The catalogues world.json holds once, where every row carries its own copy.
    const JsonValue& biomes = b.world.at("terrain").at("biomes");
    const JsonValue& magical = b.world.at("terrain").at("magical_biomes");
    const JsonValue& detail = b.world.at("terrain_detail");
    JsonValue colors = JsonValue::object();
    for (const auto& [name, network] : b.world.at("magic").at("networks").as_object()) {
        colors.set(name, network.at("color"));
    }

    std::set<std::string> paths;
    std::size_t rows_checked = 0;
    std::size_t long_keys = 0;
    for (const PlanBlock& p : kPlanBlocks) {
        const std::string block(p.block);
        const JsonValue& whole_block = b.whole.at(block);
        const JsonValue& split_block = b.world.at(block);
        const JsonValue::Array& rows = whole_block.at(p.rows).as_array();
        check::is_true(!rows.empty(), block + " to have rows, or nothing is tested");

        // The block's own members are the whole document's, with its rows replaced in place
        // by the listing of their files.
        std::vector<std::string> expected_keys;
        for (const std::string& key : keys_of(whole_block)) expected_keys.push_back(key == p.rows ? "files" : key);
        check::is_true(keys_of(split_block) == expected_keys, block + "'s keys, rows listed in their place");
        for (const auto& [key, value] : whole_block.as_object()) {
            if (key != p.rows) check::is_true(split_block.at(key) == value, block + "." + key + " unchanged");
        }

        const JsonValue::Array& files = split_block.at("files").as_array();
        check::equal(files.size(), rows.size(), block + ": a file for every row");
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const JsonValue& row = rows[i];
            const JsonValue& listed = files[i];
            const std::string what = block + " row " + std::to_string(i);
            check::is_true(keys_of(listed) == std::vector<std::string>{std::string(p.join), "path"}, what + "'s listing");
            check::is_true(listed.at(p.join) == row.at(p.join), what + " listed by its own key, in the row's order");
            const std::string& path = listed.at("path").as_string();
            check::is_true(path.rfind(std::string(p.folder), 0) == 0 && path.size() > 5 &&
                               path.compare(path.size() - 5, 5, ".json") == 0,
                           what + " filed under " + std::string(p.folder) + ": " + path);
            check::is_true(paths.insert(path).second, what + "'s path unique in the export: " + path);
            // Short enough to sit under a deep directory on Windows, however long the key.
            check::is_true(path.size() <= 80, what + "'s path at most 80 characters: " + path);
            if (listed.at(p.join).as_string().size() > 48) ++long_keys;

            // The row less its catalogue copies -- each of which it must have carried, and
            // each equal to the one world.json holds.
            JsonValue expected = row;
            if (p.catalogues) {
                const JsonValue& terrain = row.at("terrain");
                check::is_true(terrain.at("biome_catalogue") == biomes, what + "'s biome catalogue is world.json's");
                check::is_true(terrain.at("magical_catalogue") == magical,
                               what + "'s magical catalogue is world.json's");
                check::is_true(terrain.at("magic_colors") == colors, what + "'s ley colours are world.json's");
                JsonValue& t = expected.at("terrain");
                check::is_true(t.erase("biome_catalogue") && t.erase("magical_catalogue") && t.erase("magic_colors"),
                               what + " to carry the catalogue copies");
            }
            if (p.terrain_detail) {
                check::is_true(row.at("terrain").at("surface").at("terrain_detail") == detail,
                               what + "'s terrain detail is world.json's");
                check::is_true(expected.at("terrain").at("surface").erase("terrain_detail"),
                               what + " to carry the terrain detail");
            }
            check::is_true(read_json(b.dir / path) == expected, what + ": " + path + " is the row less its copies");
            ++rows_checked;
        }
    }
    check::is_true(long_keys > 0, "a key too long for its file name, or the shortening is not tested");
    check::equal(rows_checked,
                 b.world.at("city_plans").at("files").size() + b.world.at("hamlet_plans").at("files").size() +
                     b.world.at("castle_plans").at("files").size() +
                     b.world.at("key_location_plans").at("files").size(),
                 "every listed file checked");
}

TEST(a_split_export_reports_exactly_the_files_it_wrote_and_is_smaller_than_the_whole) {
    const BothWays& b = both();
    const SplitExport& r = b.result;
    check::is_true(!r.files.empty() && r.files.front() == "world.json", "world.json listed first");
    const std::set<std::string> reported(r.files.begin(), r.files.end());
    check::equal(reported.size(), r.files.size(), "no file reported twice");
    check::is_true(reported == files_under(b.dir), "the files reported to be exactly the files on disk");

    std::uintmax_t total = 0;
    for (const std::string& f : r.files) total += fs::file_size(b.dir / f);
    check::equal(r.total_bytes, total, "total_bytes to be the files' sizes");
    check::equal(r.world_bytes, fs::file_size(b.dir / "world.json"), "world_bytes to be world.json's size");

    // Each catalogue once rather than once per plan: smaller in total, and world.json alone
    // a fraction of the whole.
    check::is_true(r.total_bytes < b.whole_bytes, "the parts together smaller than the whole document");
    check::is_true(r.world_bytes * 2 < b.whole_bytes, "world.json under half the whole document");
}

TEST(a_pretty_split_holds_the_same_values) {
    const BothWays& b = both();
    ScratchDir scratch;
    const fs::path dir = scratch.path() / "pretty";
    const SplitExport pretty = write_world_split(dir, split_world(), /*pretty=*/true);
    check::is_true(pretty.files == b.result.files, "the same files");
    const std::string text = read_file(dir / "world.json");
    check::is_true(text.find("\n  \"") != std::string::npos, "an indented world.json");
    check::is_true(parse_json(text) == b.world, "world.json's values unchanged by the indent");
    for (const std::string& f : pretty.files) {
        check::is_true(read_json(dir / f) == read_json(b.dir / f), f + "'s values unchanged by the indent");
    }
}

TEST(a_split_of_a_world_without_plans_or_scene_is_its_document_alone) {
    Request request;
    request.seed = 42;
    request.size = 17;
    request.phase = 11;
    const World w = generate(resolve(request));
    check::is_true(!w.has_plans && !w.has_world_scene && !w.has_key_locations, "a world with no plans and no scene");
    ScratchDir scratch;
    const fs::path dir = scratch.path() / "early";
    const SplitExport r = write_world_split(dir, w);
    check::is_true(r.files == std::vector<std::string>{"world.json"}, "world.json alone");
    check::is_true(files_under(dir) == std::set<std::string>{"world.json"}, "and nothing else on disk");

    // The document, with the split record naming no scene.
    std::ostringstream out;
    write_world_json(out, w, DocumentOptions{});
    JsonValue expected = parse_json(out.str());
    JsonValue::Object members;
    for (auto& member : expected.as_object()) {
        members.push_back(member);
        if (member.first == "completed_stage") {
            members.emplace_back("split", JsonValue(JsonValue::Object{{"version", 1}, {"scene", nullptr}}));
        }
    }
    check::is_true(read_json(dir / "world.json") == JsonValue(members), "world.json the document, scene null");
}

TEST(a_split_export_writes_only_into_a_new_or_empty_directory) {
    ScratchDir scratch;
    const World& w = split_world();
    const auto refused = [&](const fs::path& dir) {
        try {
            (void)write_world_split(dir, w);
        } catch (const std::runtime_error&) {
            return true;
        }
        return false;
    };

    // A directory that holds anything is refused, and left as it was.
    const fs::path occupied = scratch.path() / "occupied";
    fs::create_directory(occupied);
    write_file(occupied / "mine.txt", "mine");
    check::is_true(refused(occupied), "a directory holding a file refused");
    check::is_true(files_under(occupied) == std::set<std::string>{"mine.txt"}, "nothing written beside it");
    check::equal(read_file(occupied / "mine.txt"), std::string("mine"), "and it untouched");

    // A path that is a file is refused, and the file left as it was.
    const fs::path file = scratch.path() / "a-file";
    write_file(file, "mine");
    check::is_true(refused(file), "a file refused");
    check::equal(read_file(file), std::string("mine"), "the file untouched");

    // An empty directory is taken.
    const fs::path empty = scratch.path() / "empty";
    fs::create_directory(empty);
    const SplitExport r = write_world_split(empty, w);
    check::is_true(fs::is_regular_file(empty / "world.json"), "an empty directory written into");
    check::equal(r.files.size(), files_under(empty).size(), "every file written");
}

#ifdef _WIN32
// Windows refuses a path longer than 260 characters, which makes a failure part way through an
// export reproducible: a destination 228 characters long takes its directories, and not its
// first plan file.
TEST(a_split_export_that_fails_part_way_leaves_its_destination_as_it_found_it) {
    ScratchDir scratch;
    const World& w = split_world();
    const std::size_t base = scratch.path().string().size();
    check::is_true(base < 200, "a temporary directory short enough to build the long path under");
    const fs::path dest = scratch.path() / std::string(228 - base - 1, 'd');
    check::equal(dest.string().size(), std::size_t{228}, "the destination's length");
    const auto failure = [&] {
        try {
            (void)write_world_split(dest, w);
        } catch (const std::runtime_error& error) {
            return std::string(error.what());
        }
        return std::string("<finished>");
    };

    // A new destination is removed again, with every directory under it.
    std::string what = failure();
    check::is_true(what.rfind("cannot write ", 0) == 0, "the export to fail writing a file, not refuse: " + what);
    check::is_true(!fs::exists(dest), "a destination it created removed again");
    check::is_true(fs::is_empty(scratch.path()), "nothing left beside it");

    // An empty destination is left empty, and left.
    fs::create_directory(dest);
    what = failure();
    check::is_true(what.rfind("cannot write ", 0) == 0, "the export to fail writing a file: " + what);
    check::is_true(fs::is_directory(dest) && fs::is_empty(dest), "an empty destination left, and empty");
}
#endif
