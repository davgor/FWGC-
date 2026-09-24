// The read surface: pure selectors over a finished world, and the bounded read API.
//
// Two halves, as the reference's own suite had. The pure half needs no world at all -- the
// block accounting over documents written by hand, the vocabularies, and every refusal a
// hand-built world can reach. The world half generates one seed-42 world at size 33 through
// stage 16, ticks it a month so a quest board exists, and then SWEEPS rather than samples:
// every id a read returns is resolved back in the same world, because a join that is right
// for the first row and wrong for the hundredth looks exactly like a join that works.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "fwg/castle_plan.hpp"
#include "fwg/city_plan.hpp"
#include "fwg/grid.hpp"
#include "fwg/key_locations.hpp"
#include "fwg/liveness.hpp"
#include "fwg/read.hpp"
#include "fwg/serialize.hpp"
#include "fwg/world_time.hpp"

using namespace fwg;

namespace {

World generated(int size, int phase = 16) {
    Request r;
    r.seed = 42;
    r.size = size;
    r.phase = phase;
    return generate(resolve(r));
}

const World& genesis() {
    static const World world = generated(33);
    return world;
}

// A month on: the quest board is written by the time API and by nothing else.
const World& ticked() {
    static const World world = [] {
        TimeAdvanceRequest request;
        request.elapsed = TimeElapsed{TimeUnit::Days, 30.0};
        return advance_world_time(genesis(), request).world;
    }();
    return world;
}

template <typename Fn>
WorldTimeError refused(Fn&& fn, const std::string& what) {
    try {
        fn();
    } catch (const WorldTimeError& refusal) {
        return refusal;
    }
    check::fail(what + ": expected a WorldTimeError, none thrown");
}

// A checked read of an optional: an empty one is a test failure, never a dereference.
template <typename T>
const T& must(const std::optional<T>& value, const std::string& what) {
    if (!value) check::fail("expected " + what);
    return *value;
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// Two long texts are equal, or the failure says where they first differ -- never the whole
// texts, which can be world documents.
void same_text(const std::string& actual, const std::string& expected, const std::string& what) {
    if (actual == expected) return;
    const auto first = std::mismatch(actual.begin(), actual.end(), expected.begin(), expected.end());
    const auto at = static_cast<std::size_t>(first.first - actual.begin());
    const auto near = [at](const std::string& s) { return at < s.size() ? s.substr(at, 80) : std::string(); };
    check::fail(what + ": differs at byte " + std::to_string(at) + " of " + std::to_string(actual.size()) + " vs " +
                std::to_string(expected.size()) + ": '" + near(actual) + "' vs '" + near(expected) + "'");
}

template <typename T>
bool has(const std::vector<T>& items, const T& item) {
    return std::find(items.begin(), items.end(), item) != items.end();
}

// Everything the caller's world holds that a read could touch: the whole document with its
// timings, and the time API's blocks the document does not carry.
std::string everything_of(const World& w) {
    std::ostringstream o;
    o << std::setprecision(17);
    write_world_json(o, w, /*pretty=*/false, /*include_timings=*/true);
    if (w.world_clock) o << "clock " << w.world_clock->day << ' ' << w.world_clock->age << '\n';
    if (w.quests) {
        for (const QuestRecord& q : w.quests->quests) {
            o << q.quest_id << ' ' << to_string(q.state) << ' ' << q.giver_uid.value_or("-") << '\n';
        }
        o << "log " << w.quests->log.size() << '\n';
    }
    o << "ops " << w.history.operations.size() << '\n';
    return o.str();
}

std::string uid_of(const PersonRecord& record) {
    return std::visit([](const auto& person) { return person.uid; }, record);
}

std::string anchor_text(const PersonAnchor& a) {
    std::ostringstream o;
    o << (a.node ? std::to_string(*a.node) : "-") << ' ' << (a.source ? *a.source : "-") << ' '
      << a.uid.value_or("-") << ' ' << a.unresolved_uid.value_or("-");
    return o.str();
}

// --- digests of each read, for "the same read twice gives the same answer" ---------------

std::string digest(const BlocksRead& r) {
    std::ostringstream o;
    for (const BlockRow& row : r.summary.blocks) {
        o << row.name << ' ' << row.registered << ' ' << row.version.value_or(-1) << ' ' << row.type << ' '
          << row.count.value_or(0) << ' ' << row.bytes.value_or(0) << ' ' << row.encodable << '\n';
    }
    o << r.summary.total_bytes << ' ' << r.summary.absent.size() << ' ' << r.summary.unregistered.size();
    return o.str();
}

std::string digest(const NearRead& r) {
    std::ostringstream o;
    o << std::setprecision(17) << r.node << ' ' << r.radius_m << ' ' << r.rows << '\n';
    for (const auto& row : r.near.settlements) o << "s " << row.uid << ' ' << row.distance_m << '\n';
    for (const auto& row : r.near.hamlets) o << "h " << row.id << ' ' << row.distance_m << '\n';
    for (const auto& row : r.near.fortresses) o << "f " << row.id << ' ' << row.distance_m << '\n';
    for (const auto& row : r.near.key_locations) o << "k " << row.id << ' ' << row.distance_m << '\n';
    for (const auto& row : r.near.beast_nests) o << "n " << row.id << ' ' << row.distance_m << '\n';
    for (const auto& row : r.near.ley_nodes) o << "l " << row.id << ' ' << row.distance_m << '\n';
    for (const auto& row : r.near.encounters) o << "e " << row.occupancy << ' ' << row.distance_m << '\n';
    for (const auto& [name, value] : r.cell.layers) o << name << '=' << value << ' ';
    return o.str();
}

std::string plan_text(const std::optional<PlanSummary>& p) {
    if (!p) return "-";
    std::ostringstream o;
    o << p->block << ' ' << p->id << ' ' << p->status << ' ' << p->version << ' ' << p->plots << ' ' << p->unplaced
      << ' ' << (p->bytes ? std::to_string(*p->bytes) : "-");
    return o.str();
}

std::string digest(const PlaceRead& r) {
    std::ostringstream o;
    const PlaceDocument& d = r.place;
    o << d.anchor.id << ' ' << to_string(d.anchor.kind) << ' ' << d.anchor.node << ' '
      << d.anchor.roster_key.value_or("-") << ' ' << d.anchor.id_survives_an_age << '\n'
      << plan_text(d.resolved.plan) << '\n'
      << plan_text(d.resolved.location_plan) << '\n'
      << d.resolved.post_count << ' ' << d.resolved.posts_truncated << '\n';
    for (const NpcPostRow& post : d.resolved.npc_posts) o << post.uid << ' ' << post.status << '\n';
    for (const ReadJoin& join : d.joins) o << join.name << ' ';
    return o.str();
}

std::string digest(const PersonRead& r) {
    std::ostringstream o;
    o << to_string(r.person.block) << ' ' << uid_of(r.person.person) << ' ' << to_string(r.person.liveness) << ' '
      << anchor_text(r.person.anchor);
    return o.str();
}

std::string digest(const QuestsRead& r) {
    std::ostringstream o;
    o << std::setprecision(17);
    for (const QuestRow& q : r.quests) {
        o << q.quest_id << ' ' << to_string(q.state) << ' ' << q.offered_day << ' ' << q.expires_day << ' ';
        if (q.giver) {
            o << q.giver->uid << ' ' << to_string(q.giver->block) << ' ' << to_string(q.giver->liveness) << ' '
              << anchor_text(q.giver->anchor);
        }
        o << '\n';
    }
    o << r.counts.total;
    return o.str();
}

// The node a settlement stands on, across the whole list: node ids are latitude-ordered, so a
// sample from the front of any list sorted by node would all sit near the pole.
std::vector<int> sample_nodes(std::size_t count) {
    return {0, 1, static_cast<int>(count / 5), static_cast<int>(count / 3), static_cast<int>(count / 2),
            static_cast<int>(2 * count / 3), static_cast<int>(count) - 2, static_cast<int>(count) - 1};
}

}  // namespace

// ===========================================================================
// The vocabularies
// ===========================================================================

TEST(read_state_keys_are_the_references_forty_nine_in_its_order) {
    const std::span<const std::string_view> keys = state_keys();
    check::equal(keys.size(), std::size_t{49}, "STATE_KEYS size");
    check::equal(keys.front(), std::string_view("world_scene"), "first key");
    check::equal(keys.back(), std::string_view("religion"), "last key");
    const std::set<std::string_view> unique(keys.begin(), keys.end());
    check::equal(unique.size(), keys.size(), "no key registered twice");
    for (const char* key : {"world_clock", "quests", "heroes", "npcs", "villains", "settlements", "encounters"}) {
        check::is_true(unique.count(key) == 1, std::string("registered: ") + key);
    }
    // Reports of a call are not state, and the reference did not register them.
    for (const char* key : {"quest_actions", "person_state", "time_advance", "config", "layers"}) {
        check::is_true(unique.count(key) == 0, std::string("not registered: ") + key);
    }
}

TEST(read_the_categories_kinds_and_reads_are_the_references) {
    const std::vector<std::string_view> categories(near_categories().begin(), near_categories().end());
    check::is_true(categories == std::vector<std::string_view>{"settlements", "hamlets", "fortresses",
                                                                "key_locations", "beast_nests", "ley_nodes",
                                                                "encounters"},
                   "NEAR_CATEGORIES in order");
    std::vector<std::string_view> kinds;
    for (const PlaceKind kind : place_kinds()) kinds.push_back(to_string(kind));
    check::is_true(kinds == std::vector<std::string_view>{"city", "hamlet", "fortress", "key_location"},
                   "PLACE_KINDS in order");
    check::is_true(id_survives_an_age(PlaceKind::City) && id_survives_an_age(PlaceKind::KeyLocation),
                   "a city uid and a key location id survive an age");
    check::is_true(!id_survives_an_age(PlaceKind::Hamlet) && !id_survives_an_age(PlaceKind::Fortress),
                   "a hamlet or fortress id is an ordinal");
    const std::vector<std::string_view> reads(read_names().begin(), read_names().end());
    check::is_true(reads == std::vector<std::string_view>{"blocks", "near", "place", "person", "quests"},
                   "the reference's five reads");
    check::equal(quest_states().size(), std::size_t{6}, "six quest states");
}

// ===========================================================================
// blocks: the accounting, on documents written by hand
// ===========================================================================

TEST(read_block_accounting_counts_the_compact_encoding_whatever_the_layout) {
    const std::vector<BlockRow> rows = block_rows_of(
        R"({ "b": { "version": 3, "a": [1, 2, 3] },
             "a": "x y",
             "c": [1,2,3],
             "d": null, "e": true, "f": 1.5 })");
    std::vector<std::string> names;
    for (const BlockRow& row : rows) names.push_back(row.name);
    check::is_true(names == std::vector<std::string>{"a", "b", "c", "d", "e", "f"}, "sorted by name");
    check::equal(rows[0].type, std::string_view("string"), "a is a string");
    check::equal(must(rows[0].bytes, "bytes"), std::size_t{5}, "a string's own space is counted: \"x y\"");
    check::equal(rows[1].type, std::string_view("object"), "b is an object");
    check::equal(must(rows[1].count, "a count"), std::size_t{2}, "b has two members");
    check::equal(must(rows[1].version, "a version"), 3LL, "b declares version 3");
    // {"version":3,"a":[1,2,3]} -- whitespace outside strings is not counted.
    check::equal(must(rows[1].bytes, "bytes"), std::size_t{25}, "b's compact bytes");
    check::equal(rows[2].type, std::string_view("array"), "c is an array");
    check::equal(must(rows[2].count, "a count"), std::size_t{3}, "c has three elements");
    check::is_true(!rows[2].version, "an array declares no version");
    check::equal(rows[3].type, std::string_view("null"), "d");
    check::equal(rows[4].type, std::string_view("boolean"), "e");
    check::equal(rows[5].type, std::string_view("number"), "f");
    check::is_true(!rows[5].count, "a scalar has no count");
    for (const BlockRow& row : rows) {
        check::is_true(!row.registered, row.name + " is not a state key");
        check::is_true(row.encodable, row.name + " is encodable");
    }
}

TEST(read_a_block_version_is_read_and_never_guessed) {
    const std::vector<BlockRow> rows = block_rows_of(
        R"({"no_version":{"a":1},"string_version":{"version":"two"},"float_version":{"version":2.0},)"
        R"("exponent":{"version":1e2},"negative":{"version":-3},"listed":[1,2,3],"twice":{"version":1,"version":4}})");
    std::map<std::string, std::optional<long long>> version;
    for (const BlockRow& row : rows) version[row.name] = row.version;
    check::is_true(!version["no_version"], "no version key");
    check::is_true(!version["string_version"], "a string is not a version");
    check::is_true(!version["float_version"], "a float is not a version");
    check::is_true(!version["exponent"], "an exponent is a float");
    check::equal(must(version["negative"], "a version"), -3LL, "a negative integer is an integer");
    check::is_true(!version["listed"], "an array declares none");
    check::equal(must(version["twice"], "a version"), 4LL, "the last of two keys, as a JSON reader keeps it");
}

TEST(read_a_block_that_cannot_cross_is_named_rather_than_counted) {
    const BlockSummary summary = block_summary_of(
        R"({"broken":{"value":1e999},"negative":{"value":[-1e999]},"quoted":{"value":"1e999"},"fine":{"value":1}})");
    std::map<std::string, const BlockRow*> by_name;
    for (const BlockRow& row : summary.blocks) by_name[row.name] = &row;
    check::is_true(!by_name["broken"]->encodable && !by_name["broken"]->bytes, "an infinity is not encodable");
    check::is_true(!by_name["negative"]->encodable, "nor is a negative one, at any depth");
    check::is_true(by_name["quoted"]->encodable, "the same characters inside a string are text");
    check::is_true(by_name["fine"]->encodable, "a finite block is encodable");
    check::is_true(summary.unencodable == std::vector<std::string>{"broken", "negative"}, "named, in row order");
    check::equal(summary.total_bytes, must(by_name["quoted"]->bytes, "bytes") + must(by_name["fine"]->bytes, "bytes"),
                 "the total is over the encodable rows");
}

TEST(read_block_accounting_decodes_keys_and_is_not_fooled_by_brackets_in_strings) {
    // The backslash of each \u escape is built rather than typed, so nothing between this line
    // and the compiler can turn the escape into the character it names -- which is how an
    // earlier draft of this test came to hold no escape at all and still pass.
    const std::string bs(1, static_cast<char>(92));
    const std::string document = R"({"a\"b":{"k":"}]{[\"","n":[1]},"z)" + bs + "u00e9" + R"(":[],"s)" + bs + "ud83d" +
                                 bs + R"(ude00":{}})";
    check::is_true(document.find(bs + "u00e9") != std::string::npos, "the document carries the escape itself");
    const std::vector<BlockRow> rows = block_rows_of(document);
    check::equal(rows.size(), std::size_t{3}, "three top-level keys");
    check::equal(rows[0].name, std::string("a\"b"), "an escaped quote in a key");
    check::equal(must(rows[0].count, "a count"), std::size_t{2}, "brackets inside a string open nothing");
    check::equal(rows[1].name, std::string("s\xf0\x9f\x98\x80"), "a surrogate pair decodes to one code point");
    check::equal(rows[2].name, std::string("z\xc3\xa9"), "a \\u escape decodes to UTF-8");
    check::equal(must(rows[2].count, "a count"), std::size_t{0}, "an empty array");
    check::throws([] { (void)block_rows_of("[1, 2]"); }, "a document that is not an object");
    check::throws([] { (void)block_rows_of(R"({"a": [1, 2)"); }, "an unclosed document");
}

TEST(read_the_registry_is_marked_in_both_directions) {
    const BlockSummary summary = block_summary_of(R"({"settlements":{"version":1},"invented_later":{"version":1}})");
    check::is_true(summary.unregistered == std::vector<std::string>{"invented_later"},
                   "a block the registry does not know is reported, not dropped");
    check::equal(summary.registered_count, std::size_t{1}, "one registered block");
    check::equal(summary.state_keys, std::size_t{49}, "the registry's size");
    check::equal(summary.absent.size(), std::size_t{48}, "every other registered block is absent");
    check::is_true(std::is_sorted(summary.absent.begin(), summary.absent.end()), "absent is sorted");
    check::is_true(!has(summary.absent, std::string("settlements")), "a carried block is not absent");
}

// ===========================================================================
// blocks: on a generated world
// ===========================================================================

TEST(read_blocks_names_every_top_level_key_of_the_document) {
    const World& w = genesis();
    const BlocksRead read = read_blocks(w);
    const BlockSummary& s = read.summary;
    std::set<std::string> names;
    std::size_t total = 0;
    for (const BlockRow& row : s.blocks) {
        check::is_true(names.insert(row.name).second, "every top-level key once: " + row.name);
        check::equal(row.registered, has(std::vector<std::string_view>(state_keys().begin(), state_keys().end()),
                                         std::string_view(row.name)),
                     "registration of " + row.name);
        if (row.bytes) total += *row.bytes;
    }
    check::equal(s.total_bytes, total, "the total is the sum of the rows");
    check::is_true(std::is_sorted(s.blocks.begin(), s.blocks.end(),
                                  [](const BlockRow& a, const BlockRow& b) { return a.name < b.name; }),
                   "rows sorted by name");
    for (const std::string_view key : state_keys()) {
        check::equal(has(s.absent, std::string(key)), names.count(std::string(key)) == 0,
                     "absent is exactly the registered keys not carried: " + std::string(key));
    }

    // Facts checkable against the World itself.
    std::map<std::string, const BlockRow*> by_name;
    for (const BlockRow& row : s.blocks) by_name[row.name] = &row;
    for (const char* name : {"ruins", "ocean_archipelagos", "layers", "heroes", "magic", "config", "schema"}) {
        check::is_true(by_name.count(name) == 1, std::string("the document carries ") + name);
    }
    check::equal(must(by_name.at("ruins")->count, "a count"), w.ruins.size(), "ruins is the ruin list");
    check::equal(must(by_name.at("ocean_archipelagos")->count, "a count"), w.ecology.archipelagos.size(),
                 "ocean_archipelagos is the archipelago list");
    check::equal(must(by_name.at("layers")->count, "a count"), w.layers.size(), "layers holds every layer");
    check::equal(must(by_name.at("heroes")->version, "a version"), static_cast<long long>(w.heroes.version),
                 "heroes declares its version");
    check::equal(must(by_name.at("magic")->version, "a version"), static_cast<long long>(w.magic.version),
                 "magic declares its version");
    check::is_true(by_name.at("config")->type == "object" && !by_name.at("config")->registered,
                   "config is carried and is not state");
    check::is_true(by_name.at("schema")->type == "string", "the document's own schema line is a row too");
    for (const BlockRow& row : s.blocks) check::is_true(row.encodable, "a generated world crosses: " + row.name);

    // The measure is the document's: the rows' bytes are exactly the compact document less its
    // keys, colons, commas, braces and trailing newline.
    std::ostringstream document;
    write_world_json(document, w, /*pretty=*/false, /*include_timings=*/false);
    std::size_t framing = 2 + (s.blocks.size() - 1) + 1;  // {}, commas, the newline
    for (const BlockRow& row : s.blocks) framing += row.name.size() + 3;  // "name":
    check::equal(s.total_bytes + framing, document.str().size(), "the rows account for every byte of the document");
    check::equal(read.envelope.read, std::string_view("blocks"), "envelope");
}

// The blocks read describes the document a host receives, so a block the world holds in memory
// is present there only once the writer carries it. Every registered block a ticked world
// holds -- the time API's clock and board among them -- is carried now.
TEST(read_blocks_reports_the_time_apis_blocks_as_carried) {
    const World& w = ticked();
    check::is_true(w.world_clock.has_value() && w.quests.has_value(), "the ticked world holds a clock and a board");
    const BlockSummary s = read_blocks(w).summary;
    for (const char* key : {"world_clock", "quests", "world_scene", "population_budget"}) {
        check::is_true(!has(s.absent, std::string(key)), std::string("the document carries ") + key);
    }
}

// ===========================================================================
// Geometry
// ===========================================================================

TEST(read_a_node_cell_is_the_grids_own_point) {
    const World& w = genesis();
    const std::shared_ptr<const SphereGrid> grid = sphere_grid(w.config.size, w.effective.globe_radius);
    check::equal(node_count(w), grid->node_count(), "node count");
    for (const int node : sample_nodes(grid->node_count())) {
        const NodeCell cell = node_cell(w, node);
        const auto i = static_cast<std::size_t>(node);
        check::equal(cell.x, grid->point_x[i], "x of node " + std::to_string(node));
        check::equal(cell.z, grid->point_z[i], "z of node " + std::to_string(node));
        const Vec3 d = direction(cell.x, cell.z, w.config.size);
        check::is_true(cell.direction.x == d.x && cell.direction.y == d.y && cell.direction.z == d.z,
                       "the cell's direction is direction(x, z, n)");
    }
    check::throws([&] { (void)node_cell(w, -1); }, "a negative node");
    check::throws([&] { (void)node_cell(w, static_cast<int>(grid->node_count())); }, "a node past the end");
}

TEST(read_arc_distance_reproduces_the_grids_neighbour_distances) {
    const World& w = genesis();
    const std::shared_ptr<const SphereGrid> grid = sphere_grid(w.config.size, w.effective.globe_radius);
    const double radius = globe_radius(w);
    check::equal(radius, w.effective.globe_radius, "the radius is the metres");
    std::size_t checked = 0;
    for (const int node : sample_nodes(grid->node_count())) {
        const NodeCell here = node_cell(w, node);
        for (const SphereNeighbour& n : grid->neighbours[static_cast<std::size_t>(node)]) {
            const NodeCell there = node_cell(w, n.index);
            check::equal(arc_distance_m(radius, here.direction, there.direction), n.distance,
                         "node " + std::to_string(node) + " -> " + std::to_string(n.index));
            ++checked;
        }
    }
    check::is_true(checked > 20, "neighbours were checked");
}

TEST(read_a_layer_sample_is_an_exact_read_of_every_layer) {
    const World& w = genesis();
    const NodeCell cell = node_cell(w, static_cast<int>(node_count(w) / 2));
    const auto sample = layer_sample(w, cell.x, cell.z);
    check::equal(sample.size(), w.layers.size(), "every layer");
    for (const auto& [name, value] : sample) {
        check::equal(value, w.layers.at(name).at(cell.x, cell.z), "layer " + name);
    }
    check::is_true(std::is_sorted(sample.begin(), sample.end()), "sorted by name");
    check::is_true(layer_sample(w, -1, 0).empty() && layer_sample(w, w.config.size, 0).empty(),
                   "a cell off the grid samples nothing");
}

// ===========================================================================
// near
// ===========================================================================

TEST(read_near_at_every_city_node_finds_that_city_at_exactly_zero) {
    const World& w = genesis();
    check::is_true(!w.settlements.sites.empty(), "the world has cities");
    for (const SettlementSite& site : w.settlements.sites) {
        NearReadRequest request;
        request.node = site.node;
        const NearRead read = read_near(w, request);  // radius 0: what stands on the node
        bool found = false;
        for (const NearSettlementRow& row : read.near.settlements) {
            if (row.uid != site.uid) continue;
            found = true;
            check::equal(row.distance_m, 0.0, "a city on the node is at exactly 0 m");
            check::equal(row.node, site.node, "and on its own node");
            check::equal(row.id, site.id, "its ordinal");
            check::equal(row.kind, std::string_view("city"), "a settlement row is a city");
            check::equal(row.city_class, city_class_name(site.city_class), "its class");
        }
        check::is_true(found, "node " + std::to_string(site.node) + " finds " + site.uid);
    }
}

TEST(read_near_small_sites_stand_at_their_node) {
    const World& w = genesis();
    check::is_true(!w.humans.hamlets.empty() && !w.humans.fortresses.empty(), "hamlets and fortresses exist");
    for (const auto* list : {&w.humans.hamlets, &w.humans.fortresses}) {
        for (const RuralSite& record : *list) {
            const NearRows rows = near_rows(w, record.node, 0.0);
            const auto& found = record.kind == "fortress" ? rows.fortresses : rows.hamlets;
            const bool here = std::any_of(found.begin(), found.end(), [&](const NearSmallSiteRow& row) {
                return row.id == record.id && row.distance_m == 0.0 && row.core_id == record.core_id &&
                       !row.id_survives_an_age;
            });
            check::is_true(here, record.id + " at node " + std::to_string(record.node));
        }
    }
}

// Reproduced from the reference, which recorded it: the hamlet and fortress row under-reports,
// leaving out the node-keyed uid that survives an age. Pinned at compile time, so adding it is
// a deliberate change to a published row.
template <typename T>
concept carries_uid = requires(const T& row) { row.uid; };

TEST(read_near_small_site_row_does_not_carry_the_surviving_uid) {
    static_assert(!carries_uid<NearSmallSiteRow>, "the small-site near row carries no uid, as the reference's");
    static_assert(carries_uid<NearSettlementRow>, "the check can see a uid where there is one");
    check::is_true(true, "compile-time");
}

TEST(read_near_encounters_resolve_through_the_index_the_writer_built) {
    const World& w = genesis();
    const EncounterIndex& e = w.encounters;
    check::is_true(!e.by_node.empty(), "the world has an encounter index");
    const auto busiest = std::max_element(e.by_node.begin(), e.by_node.end(), [](const auto& a, const auto& b) {
        return a.second.size() < b.second.size();
    });
    const NearRows rows = near_rows(w, busiest->first, 0.0);
    check::equal(rows.encounters.size(), busiest->second.size(), "every occupancy row filed under the node");
    for (std::size_t i = 0; i < rows.encounters.size(); ++i) {
        const NearEncounterRow& row = rows.encounters[i];
        const auto index = static_cast<std::size_t>(busiest->second[i]);
        check::equal(row.occupancy, index, "the writer's order within the node");
        const EncounterOccupancy& occupancy = e.occupancy[index];
        check::equal(row.node, occupancy.node, "occupancy node");
        check::equal(row.camp_id, occupancy.camp_id, "camp id");
        check::equal(must(row.group_uid, "an entry"), e.entries[static_cast<std::size_t>(occupancy.entry)].group_uid,
                     "entry");
        check::equal(row.distance_m, 0.0, "on the node");
    }
}

TEST(read_near_every_returned_id_resolves_in_the_same_world) {
    const World& w = genesis();
    const double radius = globe_radius(w) * kNearRadiusFraction / 4.0;
    std::set<std::string> cities, hamlets, fortresses, locations, nests, ley;
    for (const auto& s : w.settlements.sites) cities.insert(s.uid);
    for (const auto& h : w.humans.hamlets) hamlets.insert(h.id);
    for (const auto& f : w.humans.fortresses) fortresses.insert(f.id);
    for (const auto& s : w.key_locations.sites) locations.insert(s.id);
    for (const auto& s : w.beast_nests.sites) nests.insert(s.id);
    for (const auto& [name, net] : w.magic.networks) {
        for (const LeyNode& n : net.nodes) ley.insert(n.id);
    }
    std::size_t total = 0;
    const auto within = [&](double d, const std::string& what) {
        check::is_true(d >= 0.0 && d <= radius + 1e-6, what + " is within the radius");
        ++total;
    };
    for (const SettlementSite& site : w.settlements.sites) {
        NearReadRequest request;
        request.node = site.node;
        request.radius_m = radius;
        const NearRead read = read_near(w, request);
        check::equal(read.rows, read.near.total(), "rows is the sum of the categories");
        const auto known = [&](const std::set<std::string>& ids, const std::string& id, double d) {
            check::is_true(ids.count(id) == 1, id + " resolves");
            within(d, id);
        };
        for (const auto& r : read.near.settlements) known(cities, r.uid, r.distance_m);
        for (const auto& r : read.near.hamlets) known(hamlets, r.id, r.distance_m);
        for (const auto& r : read.near.fortresses) known(fortresses, r.id, r.distance_m);
        for (const auto& r : read.near.key_locations) known(locations, r.id, r.distance_m);
        for (const auto& r : read.near.beast_nests) known(nests, r.id, r.distance_m);
        for (const auto& r : read.near.ley_nodes) {
            check::is_true(ley.count(r.id) == 1 && !r.node, r.id + " is a ley node on no terrain node");
            within(r.distance_m, r.id);
        }
        for (const auto& r : read.near.encounters) {
            check::equal(w.encounters.occupancy.at(r.occupancy).camp_id, r.camp_id, "encounter camp");
            within(r.distance_m, r.camp_id);
        }
    }
    check::is_true(total > 0, "a quarter-ceiling radius around the cities found something");
}

TEST(read_near_is_ordered_by_distance_then_by_the_writers_order) {
    const World& w = genesis();
    const NearRows rows = near_rows(w, w.settlements.sites.front().node, globe_radius(w) * kNearRadiusFraction / 2.0);
    const auto ordered = [](const auto& list) {
        return std::is_sorted(list.begin(), list.end(),
                              [](const auto& a, const auto& b) { return a.distance_m < b.distance_m; });
    };
    check::is_true(ordered(rows.settlements) && ordered(rows.hamlets) && ordered(rows.fortresses) &&
                       ordered(rows.key_locations) && ordered(rows.beast_nests) && ordered(rows.ley_nodes) &&
                       ordered(rows.encounters),
                   "every category ascending by distance");
    // Ties keep the writer's order: two rows at one distance appear as the writer listed them.
    for (std::size_t i = 1; i < rows.settlements.size(); ++i) {
        if (rows.settlements[i].distance_m == rows.settlements[i - 1].distance_m) {
            check::is_true(rows.settlements[i - 1].id < rows.settlements[i].id, "a tie keeps site order");
        }
    }
}

TEST(read_near_radius_bounds_are_bounds_and_not_off_by_one) {
    const World& w = genesis();
    const double ceiling = globe_radius(w) * kNearRadiusFraction;
    NearReadRequest request;
    request.node = 0;
    request.radius_m = ceiling;
    try {
        const NearRead read = read_near(w, request);  // the ceiling itself is accepted
        check::equal(read.radius_ceiling_m, ceiling, "the ceiling reported");
    } catch (const WorldTimeError& refusal) {
        // A full-ceiling radius may be refused for its row count, never for its radius.
        check::equal(refusal.field(), std::string("near.rows"), "only the row ceiling may refuse the ceiling radius");
    }
    request.radius_m = std::nextafter(ceiling, std::numeric_limits<double>::infinity());
    WorldTimeError over = refused([&] { (void)read_near(w, request); }, "a radius past the ceiling");
    check::equal(over.field(), std::string("radius_m"), "names radius_m");
    check::is_true(over.refused_by_world(), "the world is the constraint, not the value's shape");

    request.radius_m = -1.0;
    WorldTimeError negative = refused([&] { (void)read_near(w, request); }, "a negative radius");
    check::equal(negative.field(), std::string("radius_m"), "names radius_m");
    check::equal(negative.code(), kRefusalInvalidInput, "INVALID_INPUT");
    // The reference's sentence, "0..None" and all: an absent bound is spelled None, and the unit
    // that repeats the field name's last word is not spoken.
    check::equal(negative.message(), std::string("radius_m must be 0..None; received -1."), "out of range");

    for (const double bad : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity()}) {
        request.radius_m = bad;
        check::equal(refused([&] { (void)read_near(w, request); }, "a non-finite radius").field(),
                     std::string("radius_m"), "names radius_m");
    }
}

TEST(read_near_refuses_a_node_that_is_absent_or_off_the_grid) {
    const World& w = genesis();
    NearReadRequest request;
    WorldTimeError absent = refused([&] { (void)read_near(w, request); }, "no node");
    check::equal(absent.field(), std::string("node"), "a node is required, not defaulted to zero");
    const auto count = static_cast<int>(node_count(w));
    for (const int bad : {count, -1}) {
        request.node = bad;
        WorldTimeError off = refused([&] { (void)read_near(w, request); }, "node off the grid");
        check::equal(off.field(), std::string("node"), "names node");
        check::equal(off.received(), std::to_string(bad), "the node received");
        check::is_true(off.refused_by_world(), "the world is the constraint");
        check::is_true(contains(off.message(), "numbered 0.." + std::to_string(count - 1)), "says what exists");
    }
    request.node = count - 1;
    (void)read_near(w, request);  // the last node is on the grid
}

TEST(read_near_refuses_a_world_without_a_grid_by_its_block) {
    World w;
    w.config.size = 1;
    NearReadRequest request;
    request.node = 0;
    WorldTimeError size = refused([&] { (void)read_near(w, request); }, "size 1");
    check::equal(size.field(), std::string("world.config"), "the config block");
    check::is_true(!size.retry(), "not retryable: the world is missing it");
    w.config.size = 17;
    for (const double radius : {0.0, -5.0, std::numeric_limits<double>::quiet_NaN()}) {
        w.effective.globe_radius = radius;
        check::equal(refused([&] { (void)read_near(w, request); }, "no radius").field(),
                     std::string("world.effective_config"), "the effective config block");
    }
}

TEST(read_near_row_ceiling_refuses_rather_than_truncating_when_it_is_reached) {
    // Driven on a hand-built world, which puts the ceiling exactly at the boundary. Generated
    // worlds do reach it (2026-09-24: most cities of the seed-42 size-33 world, ticked a month,
    // at the radius ceiling, through their encounter rows; see the categories tests below),
    // but never by exactly one row, and the off-by-one is what this pins.
    World w;
    w.config.size = 17;
    w.effective.globe_radius = 31830.988618379066;
    w.has_wildlife = true;
    const NodeCell cell = node_cell(w, 5);
    for (std::size_t i = 0; i <= kMaxNearRows; ++i) {
        NestSite site;
        site.id = "nest-" + std::to_string(i);
        site.species_id = "s";
        site.name = "n";
        site.family = "f";
        site.node = 5;
        site.x = cell.x;
        site.z = cell.z;
        site.direction = cell.direction;
        w.beast_nests.sites.push_back(site);
    }
    NearReadRequest request;
    request.node = 5;
    WorldTimeError over = refused([&] { (void)read_near(w, request); }, "one row past the ceiling");
    check::equal(over.code(), kRefusalStateCapacity, "STATE_CAPACITY");
    check::equal(over.field(), std::string("near.rows"), "names the rows");
    check::equal(over.received(), std::to_string(kMaxNearRows + 1), "the count received");
    check::is_true(contains(over.message(), std::to_string(kMaxNearRows)), "and the ceiling");
    w.beast_nests.sites.pop_back();
    check::equal(read_near(w, request).rows, kMaxNearRows, "the ceiling itself is answered in full");
}

// ---------------------------------------------------------------------------
// near: the categories filter (this port's addition)
// ---------------------------------------------------------------------------

namespace {

// A category's rows by its published name. The test's own table, so a filter that maps a
// name onto the wrong category disagrees with it.
std::size_t category_size(const NearRows& rows, std::string_view name) {
    if (name == "settlements") return rows.settlements.size();
    if (name == "hamlets") return rows.hamlets.size();
    if (name == "fortresses") return rows.fortresses.size();
    if (name == "key_locations") return rows.key_locations.size();
    if (name == "beast_nests") return rows.beast_nests.size();
    if (name == "ley_nodes") return rows.ley_nodes.size();
    if (name == "encounters") return rows.encounters.size();
    check::fail("the test's table has no category named " + std::string(name));
}

NearReadRequest near_request(int node, double radius_m, std::optional<std::vector<std::string>> categories) {
    NearReadRequest request;
    request.node = node;
    request.radius_m = radius_m;
    request.categories = std::move(categories);
    return request;
}

}  // namespace

TEST(read_near_without_categories_answers_every_category) {
    const World& w = ticked();
    const double radius = globe_radius(w) * kNearRadiusFraction / 4.0;
    const int node = w.settlements.sites.front().node;
    const NearRead read = read_near(w, near_request(node, radius, std::nullopt));
    const NearRows all = near_rows(w, node, radius);
    check::equal(read.rows, all.total(), "every row near_rows finds");
    for (const std::string_view name : near_categories())
        check::equal(category_size(read.near, name), category_size(all, name), std::string(name));
    check::is_true(!read.categories.has_value(), "no filter echoed");
}

TEST(read_near_categories_select_exactly_the_categories_they_name) {
    // For each category, a node and radius where it AND some other category both have rows
    // and the whole neighbourhood fits the ceiling -- so selecting it must keep its rows and
    // empty the others, and a filter that mapped the name onto another category fails here.
    const World& w = ticked();
    const double cap = globe_radius(w) * kNearRadiusFraction;
    for (const std::string_view name : near_categories()) {
        bool tested = false;
        for (const SettlementSite& site : w.settlements.sites) {
            for (const double radius : {cap / 2.0, cap / 4.0, cap / 8.0}) {
                const NearRows all = near_rows(w, site.node, radius);
                const std::size_t own = category_size(all, name);
                if (own == 0 || own == all.total() || all.total() > kMaxNearRows) continue;
                const NearRead read = read_near(w, near_request(site.node, radius, std::vector<std::string>{std::string(name)}));
                check::equal(read.rows, own, std::string(name) + " alone counts only its rows");
                for (const std::string_view other : near_categories())
                    check::equal(category_size(read.near, other), other == name ? own : std::size_t{0},
                                 std::string(name) + " filter, category " + std::string(other));
                tested = true;
                break;
            }
            if (tested) break;
        }
        check::is_true(tested, "a city whose neighbourhood holds " + std::string(name) + " and something else");
    }
}

TEST(read_near_categories_let_a_dense_neighbourhood_answer) {
    // The reason the filter exists: at the radius ceiling most cities' reads are refused, nearly
    // all of it encounters, so without a filter nobody could ask which places stand in reach.
    const World& w = ticked();
    const double cap = globe_radius(w) * kNearRadiusFraction;
    const std::vector<std::string> places = {"settlements", "hamlets", "fortresses", "key_locations"};
    std::size_t dense = 0;
    for (const SettlementSite& site : w.settlements.sites) {
        const NearRows all = near_rows(w, site.node, cap);
        if (all.total() <= kMaxNearRows) continue;
        ++dense;
        WorldTimeError over = refused([&] { (void)read_near(w, near_request(site.node, cap, std::nullopt)); },
                                      "the unfiltered read of a dense neighbourhood");
        check::equal(over.code(), kRefusalStateCapacity, "STATE_CAPACITY");
        check::is_true(contains(over.message(), "fewer categories"), "the refusal names the new remedy");
        std::size_t expected = 0;
        for (const std::string& name : places) expected += category_size(all, name);
        check::is_true(expected <= kMaxNearRows, "the places alone fit the ceiling at " + site.uid);
        const NearRead read = read_near(w, near_request(site.node, cap, places));
        check::equal(read.rows, expected, "every place in reach of " + site.uid);
        check::equal(read.near.encounters.size(), std::size_t{0}, "no encounters");
        check::equal(read.near.beast_nests.size(), std::size_t{0}, "no nests");
    }
    check::is_true(dense > 0, "the premise: some city's unfiltered read at the ceiling is refused");
}

TEST(read_near_an_empty_categories_filter_selects_nothing) {
    const World& w = ticked();
    const int node = w.settlements.sites.front().node;
    const NearRead read = read_near(w, near_request(node, globe_radius(w) * kNearRadiusFraction / 4.0, std::vector<std::string>{}));
    check::equal(read.rows, std::size_t{0}, "no rows");
    check::equal(read.near.total(), std::size_t{0}, "every category empty");
    check::is_true(read.categories.has_value() && read.categories->empty(), "the empty filter echoed");
}

TEST(read_near_refuses_a_category_outside_the_vocabulary) {
    const World& w = ticked();
    const int node = w.settlements.sites.front().node;
    WorldTimeError typo = refused(
        [&] { (void)read_near(w, near_request(node, 1000.0, std::vector<std::string>{"hamlets", "settlement"})); },
        "a misspelt category");
    check::equal(typo.code(), kRefusalInvalidInput, "INVALID_INPUT");
    check::equal(typo.field(), std::string("categories"), "names the field");
    check::equal(typo.received(), std::string("settlement"), "echoes the unknown name, not the good one");
    check::is_true(has(typo.candidates(), std::string("settlements")), "suggests the name meant");
}

// ===========================================================================
// place
// ===========================================================================

TEST(read_every_place_id_in_the_world_resolves) {
    const World& w = genesis();
    const std::vector<PlaceId> ids = place_ids(w);
    std::size_t kinds[4] = {0, 0, 0, 0};
    for (const PlaceId& id : ids) {
        PlaceReadRequest request;
        request.place_id = id.id;
        const PlaceRead read = read_place(w, request);
        check::equal(read.place.anchor.id, id.id, "anchor id");
        check::is_true(read.place.anchor.kind == id.kind, "anchor kind of " + id.id);
        check::equal(read.place.anchor.id_survives_an_age, id_survives_an_age(id.kind), "survival of " + id.id);
        ++kinds[static_cast<int>(id.kind)];
    }
    check::equal(kinds[0], w.settlements.sites.size(), "every city");
    check::equal(kinds[1], w.humans.hamlets.size(), "every hamlet");
    check::equal(kinds[2], w.humans.fortresses.size(), "every fortress");
    check::equal(kinds[3], w.key_locations.sites.size(), "every key location");
}

TEST(read_a_city_resolves_its_core_its_food_model_its_plan_and_its_roster) {
    const World& w = genesis();
    for (const SettlementSite& site : w.settlements.sites) {
        PlaceReadRequest request;
        request.place_id = site.uid;
        const PlaceDocument d = read_place(w, request).place;
        const PlaceResolved& r = d.resolved;
        check::equal(must(r.core, "a core").site_id, site.id, "core of " + site.uid);
        check::equal(must(r.seasonal_food_model, "a food model").site_id, site.id, "food model of " + site.uid);
        check::equal(must(r.seasonal_food_summary, "a food summary").site_id, site.id, "food summary of " + site.uid);
        const PlanSummary& plan = must(r.plan, "a city plan");
        check::equal(plan.id, site.uid, "plan of " + site.uid);
        check::equal(plan.block, std::string_view("city_plans"), "the city plan block");
        check::equal(plan.id_field, std::string_view("city_uid"), "joined on city_uid");
        check::is_true(!r.location_plan && !r.core_city, "no location plan and no owning city");
        check::equal(must(r.npc_site, "a roster site").uid, site.uid, "the roster's site");
        check::equal(must(d.anchor.roster_key, "a roster key"), site.uid, "a city's roster key is its uid");
        check::equal(d.anchor.node, site.node, "anchor node");
        check::equal(std::get<SettlementSite>(d.place).uid, site.uid, "the row itself");
        const std::string status = plan.status;
        check::is_true(status == "complete" || status == "partial" || status == "unbuildable", "status " + status);
    }
}

TEST(read_city_posts_are_exactly_the_roster_rows_filed_under_that_site) {
    const World& w = genesis();
    std::size_t posts = 0;
    for (const SettlementSite& site : w.settlements.sites) {
        PlaceReadRequest request;
        request.place_id = site.uid;
        const PlaceResolved r = read_place(w, request).place.resolved;
        std::vector<std::string> expected;
        for (const NpcPerson& p : w.npcs.people) {
            if (p.site_uid && *p.site_uid == site.uid) expected.push_back(p.uid);
        }
        check::equal(r.post_count, expected.size(), "post count at " + site.uid);
        check::equal(r.npc_posts.size(), std::min(expected.size(), kMaxPlacePosts), "posts projected");
        for (std::size_t i = 0; i < r.npc_posts.size(); ++i) {
            check::equal(r.npc_posts[i].uid, expected[i], "the roster's own order");
            check::is_true(r.npc_posts[i].status == "alive" || r.npc_posts[i].status == "dead",
                           "the roster's own word");
        }
        check::equal(r.posts_truncated, expected.size() > kMaxPlacePosts, "truncation reported");
        posts += r.npc_posts.size();
    }
    check::is_true(posts > 0, "the cities are staffed");
}

// The post ceiling, on a hand-built roster: no generated site staffs a thousand posts, and a
// cap that is never reached is a cap that is never tested.
TEST(read_place_posts_are_capped_and_the_true_count_reported) {
    World w;
    w.has_settlements = true;
    SettlementSite site;
    site.uid = "surface-city-0-5-human_heartland";
    site.node = 5;
    w.settlements.sites.push_back(site);
    w.has_npcs = true;
    w.npcs.ok = true;
    for (std::size_t i = 0; i <= kMaxPlacePosts; ++i) {
        NpcPerson p;
        p.uid = "npc-" + std::to_string(i);
        p.site_uid = site.uid;
        p.post = "guard";
        w.npcs.people.push_back(p);
        if (i == 3) {  // a post filed elsewhere, in the middle of the roster
            NpcPerson other;
            other.uid = "npc-elsewhere";
            other.site_uid = "surface-city-0-6-human_heartland";
            w.npcs.people.push_back(other);
        }
    }
    PlaceReadRequest request;
    request.place_id = site.uid;
    PlaceResolved r = read_place(w, request).place.resolved;
    check::equal(r.post_count, kMaxPlacePosts + 1, "the true count");
    check::equal(r.npc_posts.size(), kMaxPlacePosts, "projected to the ceiling");
    check::is_true(r.posts_truncated, "and the truncation reported");
    check::equal(r.npc_posts[4].uid, std::string("npc-4"), "a post filed elsewhere is skipped");
    check::equal(r.npc_posts.back().uid, "npc-" + std::to_string(kMaxPlacePosts - 1), "the first posts, in order");
    w.npcs.people.pop_back();
    r = read_place(w, request).place.resolved;
    check::is_true(r.post_count == kMaxPlacePosts && r.npc_posts.size() == kMaxPlacePosts && !r.posts_truncated,
                   "the ceiling itself is answered whole");
}

TEST(read_a_hamlet_joins_its_plan_by_hamlet_id_and_its_city_by_position) {
    const World& w = genesis();
    std::map<std::string, const HamletPlan*> plans;
    for (const HamletPlan& p : w.hamlet_plans.hamlets) plans[p.hamlet_id] = &p;
    for (const RuralSite& hamlet : w.humans.hamlets) {
        PlaceReadRequest request;
        request.place_id = hamlet.id;
        const PlaceDocument d = read_place(w, request).place;
        check::is_true(d.anchor.kind == PlaceKind::Hamlet, hamlet.id + " is a hamlet");
        check::equal(d.anchor.node, hamlet.node, "anchor node");
        check::equal(must(d.anchor.roster_key, "a roster key"), "hamlet-node-" + std::to_string(hamlet.node),
                     "roster key");
        const PlanSummary& plan = must(d.resolved.plan, "a hamlet plan for " + hamlet.id);
        check::equal(plan.id, hamlet.id, "plan id");
        check::is_true(plans.count(hamlet.id) == 1, "the planner wrote a plan for " + hamlet.id);
        check::equal(plan.status, std::string(plans.at(hamlet.id)->status), "the planner's status");
        check::equal(must(d.resolved.core_city, "an owning city").uid,
                     w.settlements.sites.at(static_cast<std::size_t>(hamlet.core_id)).uid,
                     "owning city of " + hamlet.id);
        check::is_true(!d.resolved.core && !d.resolved.location_plan, "no city core, no location plan");
    }
}

TEST(read_a_fortress_joins_its_castle_plan_by_fortress_id) {
    const World& w = genesis();
    std::map<std::string, const CastlePlan*> plans;
    for (const CastlePlan& p : w.castle_plans.castles) plans[p.fortress_id] = &p;
    check::is_true(!w.humans.fortresses.empty(), "fortresses exist");
    for (const RuralSite& fort : w.humans.fortresses) {
        PlaceReadRequest request;
        request.place_id = fort.id;
        const PlaceDocument d = read_place(w, request).place;
        check::is_true(d.anchor.kind == PlaceKind::Fortress, fort.id + " is a fortress");
        check::equal(must(d.anchor.roster_key, "a roster key"), "fortress-node-" + std::to_string(fort.node),
                     "roster key");
        const PlanSummary& plan = must(d.resolved.plan, "a castle plan for " + fort.id);
        check::is_true(plans.count(fort.id) == 1, "the planner wrote a castle for " + fort.id);
        check::equal(plan.block, std::string_view("castle_plans"), "the castle block");
        check::equal(plan.status, std::string(plans.at(fort.id)->status), "the castle's status");
        check::equal(plan.plots, plans.at(fort.id)->plots.size(), "the castle's plots");
    }
}

TEST(read_a_key_location_answers_under_location_plan_and_never_under_plan) {
    const World& w = genesis();
    check::is_true(!w.key_locations.sites.empty(), "key locations exist");
    std::map<std::string, const KeyLocationSitePlan*> plans;
    for (const KeyLocationSitePlan& p : w.key_location_plans.plans) plans[p.location_id] = &p;
    for (const KeyLocationSite& site : w.key_locations.sites) {
        PlaceReadRequest request;
        request.place_id = site.id;
        const PlaceDocument d = read_place(w, request).place;
        check::is_true(d.anchor.kind == PlaceKind::KeyLocation, site.id + " is a key location");
        check::is_true(!d.resolved.plan, "never under plan");
        check::is_true(!d.anchor.roster_key && !d.resolved.npc_site && d.resolved.npc_posts.empty() &&
                           d.resolved.post_count == 0,
                       "the roster does not staff a key location");
        check::is_true(plans.count(site.id) == 1, "a plan for " + site.id);
        const KeyLocationSitePlan& plan = *plans.at(site.id);
        const PlanSummary& summary = must(d.resolved.location_plan, "a location plan for " + site.id);
        check::equal(summary.status, std::string(plan.complete ? "complete" : "empty"), "status");
        check::equal(summary.unplaced, std::size_t{0}, "no unplaced list");
        check::equal(summary.plots, plan.plots.size(), "plots");
    }
}

TEST(read_a_site_plan_and_a_location_plan_never_share_a_key) {
    const World& w = genesis();
    std::set<std::string> site_tokens, location_tokens;
    for (const PlaceId& id : place_ids(w)) {
        PlaceReadRequest request;
        request.place_id = id.id;
        const PlaceResolved r = read_place(w, request).place.resolved;
        check::is_true(!(r.plan && r.location_plan), id.id + " filled both plan keys");
        if (r.plan) {
            check::is_true(id.kind != PlaceKind::KeyLocation, "a site plan belongs to a site");
            site_tokens.insert(r.plan->status);
        }
        if (r.location_plan) {
            check::is_true(id.kind == PlaceKind::KeyLocation, "a location plan belongs to a key location");
            location_tokens.insert(r.location_plan->status);
        }
    }
    check::is_true(!site_tokens.empty() && !location_tokens.empty(), "both kinds appear");
    for (const std::string& t : site_tokens) {
        check::is_true(t == "complete" || t == "partial" || t == "unbuildable", "site status " + t);
    }
    for (const std::string& t : location_tokens) {
        check::is_true(t == "complete" || t == "empty", "location status " + t);
    }
}

// A plan row's bytes are measured in a document holding that row alone. Held to the whole
// document here: removing every row of a plan block shrinks the block by exactly the rows'
// bytes and the commas between them.
TEST(read_plan_bytes_are_what_the_rows_cost_in_the_world_document) {
    const World& w = genesis();
    const auto block_bytes = [](const std::vector<BlockRow>& rows, const std::string& name) {
        for (const BlockRow& row : rows) {
            if (row.name == name) return must(row.bytes, "bytes for " + name);
        }
        check::fail("the document to carry " + name);
    };
    const auto rows_bytes = [&](const std::vector<std::string>& ids) {
        std::size_t sum = 0;
        for (const std::string& id : ids) {
            PlaceReadRequest request;
            request.place_id = id;
            const PlaceResolved r = read_place(w, request).place.resolved;
            const std::optional<PlanSummary>& plan = r.plan ? r.plan : r.location_plan;
            check::is_true(plan && plan->bytes, "a measured plan for " + id);
            sum += *plan->bytes;
        }
        return sum + (ids.empty() ? 0 : ids.size() - 1);
    };

    std::vector<std::string> cities, hamlets, castles, locations;
    for (const CityPlan& p : w.city_plans.cities) cities.push_back(p.city_uid);
    for (const HamletPlan& p : w.hamlet_plans.hamlets) hamlets.push_back(p.hamlet_id);
    for (const CastlePlan& p : w.castle_plans.castles) castles.push_back(p.fortress_id);
    for (const KeyLocationSitePlan& p : w.key_location_plans.plans) locations.push_back(p.location_id);
    check::is_true(!cities.empty() && !hamlets.empty() && !castles.empty() && !locations.empty(),
                   "every plan block has rows");

    World bare = w;
    bare.city_plans.cities.clear();
    bare.hamlet_plans.hamlets.clear();
    bare.castle_plans.castles.clear();
    bare.key_location_plans.plans.clear();
    const std::vector<BlockRow> full = block_rows(w);
    const std::vector<BlockRow> emptied = block_rows(bare);
    check::equal(block_bytes(full, "city_plans") - block_bytes(emptied, "city_plans"), rows_bytes(cities),
                 "city plans");
    check::equal(block_bytes(full, "hamlet_plans") - block_bytes(emptied, "hamlet_plans"), rows_bytes(hamlets),
                 "hamlet plans");
    check::equal(block_bytes(full, "castle_plans") - block_bytes(emptied, "castle_plans"), rows_bytes(castles),
                 "castle plans");
    check::equal(block_bytes(full, "key_location_plans") - block_bytes(emptied, "key_location_plans"),
                 rows_bytes(locations), "key location plans");
}

TEST(read_place_names_the_writing_site_of_every_join_it_resolves) {
    const World& w = genesis();
    for (const std::string& id : {w.settlements.sites.front().uid, w.humans.hamlets.front().id,
                                  w.humans.fortresses.front().id, w.key_locations.sites.front().id}) {
        PlaceReadRequest request;
        request.place_id = id;
        const PlaceDocument d = read_place(w, request).place;
        check::is_true(!d.joins.empty(), "joins for " + id);
        for (const ReadJoin& join : d.joins) {
            check::is_true(join.writer.rfind("src/", 0) == 0, "a C++ writing site for " + std::string(join.name));
        }
    }
}

TEST(read_place_refusals_name_their_field) {
    const World& w = genesis();
    PlaceReadRequest request;
    WorldTimeError empty = refused([&] { (void)read_place(w, request); }, "an empty place id");
    check::equal(empty.field(), std::string("place_id"), "names place_id");

    const std::string uid = w.settlements.sites.front().uid;
    request.place_id = uid.substr(0, uid.size() - 1);
    WorldTimeError unknown = refused([&] { (void)read_place(w, request); }, "a truncated uid");
    check::equal(unknown.field(), request.place_id, "the unknown id is the field, as the reference names it");
    check::is_true(has(unknown.candidates(), uid), "and the real uid is a candidate");
    check::is_true(unknown.retry(), "retryable with another id");

    // Equality, never a prefix: the uid with a character added is not the city.
    request.place_id = uid + "0";
    check::equal(refused([&] { (void)read_place(w, request); }, "a uid with a suffix").field(), request.place_id,
                 "a suffix is not a match");
}

// Reference defect REPRODUCED: the refusal says founding is phase 9, and founding is stage 10
// in both ladders. A phase-9 world has no settlements; a phase-10 world does.
TEST(read_place_missing_settlements_names_phase_nine_which_is_wrong) {
    const World ninth = generated(17, 9);
    const World tenth = generated(17, 10);
    check::is_true(!ninth.has_settlements, "a phase-9 world has no settlements");
    check::is_true(tenth.has_settlements && !tenth.settlements.sites.empty(), "a phase-10 world has them");
    PlaceReadRequest request;
    request.place_id = "anything";
    WorldTimeError missing = refused([&] { (void)read_place(ninth, request); }, "no settlements");
    check::equal(missing.field(), std::string("world.settlements"), "names the block");
    check::is_true(!missing.retry(), "not retryable");
    check::is_true(contains(missing.message(), "(phase 9)"), "the reference's sentence, defect included");
}

// ===========================================================================
// person
// ===========================================================================

TEST(read_the_person_index_agrees_with_the_quest_givers_index) {
    const World& w = genesis();
    const std::map<std::string, PersonEntry> index = person_index(w);
    // The lifecycle's own index, built the way the time API builds it.
    std::vector<QuestGiver> heroes, dreads, npcs;
    for (const Hero& p : w.heroes.people) heroes.push_back({p.uid, is_present(p)});
    for (const HeroDread& d : w.heroes.dreads) dreads.push_back({d.uid, is_present(d)});
    for (const NpcPerson& p : w.npcs.people) npcs.push_back({p.uid, is_present(p)});
    QuestCast cast;
    cast.heroes_ok = w.heroes.status == "ok";
    cast.heroes = heroes;
    cast.dreads = dreads;
    cast.npcs_ok = w.npcs.ok;
    cast.npcs = npcs;
    const QuestGiverIndex givers(cast);
    std::size_t resolved = 0;
    for (const auto* list : {&heroes, &dreads, &npcs}) {
        for (const QuestGiver& g : *list) {
            const QuestGiver* giver = givers.find(g.uid);
            check::is_true(giver != nullptr, "the lifecycle resolves " + g.uid);
            const auto it = index.find(g.uid);
            check::is_true(it != index.end(), "the read resolves " + g.uid);
            const PersonDocument doc = *person_document(w, g.uid);
            check::equal(doc.liveness == Liveness::Present, giver->present, "the two agree on " + g.uid);
            ++resolved;
        }
    }
    check::equal(index.size(), givers.size() + w.villains.people.size(), "villains are the only addition");
    check::is_true(resolved > 100, "a real cast");
}

TEST(read_every_person_uid_resolves_and_reports_the_one_liveness) {
    const World& w = genesis();
    const std::map<std::string, PersonEntry> index = person_index(w);
    std::size_t blocks[4] = {0, 0, 0, 0};
    for (const auto& [uid, entry] : index) {
        PersonReadRequest request;
        request.uid = uid;
        const PersonDocument d = read_person(w, request).person;
        check::equal(uid_of(d.person), uid, "the record is the one asked for");
        check::is_true(d.block == entry.block, "block of " + uid);
        const Liveness expected = std::visit([](const auto& p) { return liveness(p); }, d.person);
        check::is_true(d.liveness == expected, "liveness through the predicate for " + uid);
        ++blocks[static_cast<int>(d.block)];
    }
    check::is_true(blocks[0] > 0 && blocks[2] > 0 && blocks[3] > 0, "heroes, npcs and villains all answer");
    check::equal(blocks[1], w.heroes.dreads.size(), "every dread answers");
}

// First writer wins, in PERSON_BLOCKS order, for the index and for the single lookup alike.
// No generated world has a shared uid, so the order is pinned on a forged one.
TEST(read_a_uid_two_blocks_share_resolves_to_the_first_writer) {
    World w = genesis();
    const std::string hero = w.heroes.people.front().uid;
    const std::string npc = w.npcs.people.at(1).uid;
    w.npcs.people.front().uid = hero;
    w.villains.people.front().uid = npc;
    const std::map<std::string, PersonEntry> index = person_index(w);
    check::is_true(index.at(hero).block == LivenessBlock::Heroes, "a hero before an npc");
    check::is_true(index.at(npc).block == LivenessBlock::Npcs, "an npc before a villain");
    PersonReadRequest request;
    request.uid = hero;
    check::is_true(read_person(w, request).person.block == LivenessBlock::Heroes, "the read agrees: hero");
    request.uid = npc;
    check::is_true(read_person(w, request).person.block == LivenessBlock::Npcs, "the read agrees: npc");
}

TEST(read_a_person_carries_a_node_anchor_wherever_the_world_knows_one) {
    const World& w = genesis();
    std::size_t placed = 0;
    for (const NpcPerson& p : w.npcs.people) {
        const PersonAnchor a = person_anchor(w, p);
        if (p.presence_node) {
            check::equal(must(a.node, "a node"), *p.presence_node, "presence first for " + p.uid);
            check::equal(must(a.source, "a source"), std::string_view("npcs.people[].presence_node"), "source");
        } else {
            check::equal(must(a.source, "a source"), std::string_view("npcs.sites[].node"), "site source for " + p.uid);
            if (a.node) {
                const auto site = std::find_if(w.npcs.sites.begin(), w.npcs.sites.end(), [&](const NpcSite& s) {
                    return p.site_uid && s.uid == *p.site_uid;
                });
                check::is_true(site != w.npcs.sites.end() && site->node == a.node, "the site's own node");
            }
        }
        if (a.node) ++placed;
    }
    check::is_true(placed > 0, "npcs are placed");
    for (const Villain& v : w.villains.people) {
        const PersonAnchor a = person_anchor(w, v);
        check::equal(must(a.node, "a node"), v.node, "a villain's own node");
        check::equal(must(a.uid, "a seat"), v.seat_uid, "its seat");
    }
}

// The anchor's precedence and its unresolved cases, on records built for the purpose: a
// generated world does not reliably hold every one of them.
TEST(read_person_anchors_follow_the_references_precedence) {
    const World& w = genesis();
    const SettlementSite& city = w.settlements.sites.front();
    const SettlementSite& other = w.settlements.sites.back();
    check::is_true(!w.ruins.empty(), "a ruin to stand at");
    const Ruin& ruin = w.ruins.front();
    const auto hero = [](std::optional<std::string> presence, std::optional<std::string> home) {
        Hero h;
        if (presence) h.presence = HeroPresence{"somewhere", *presence, ""};
        if (home) h.home = HeroSiteRef{"city", *home, ""};
        return h;
    };
    PersonAnchor a = person_anchor(w, hero(std::string("camp-7"), city.uid));
    check::is_true(a.node == city.node && a.uid == city.uid, "a camp presence falls through to the home city");
    check::equal(must(a.source, "a source"), std::string_view("settlements.sites[].uid"), "a standing city");
    a = person_anchor(w, hero(city.uid, other.uid));
    check::is_true(a.node == city.node, "presence before home");
    a = person_anchor(w, hero(std::string("camp-7"), std::nullopt));
    check::is_true(!a.node && !a.source && a.uid == "camp-7" && a.unresolved_uid == "camp-7",
                   "an unplaceable presence is reported unresolved, never approximated");
    a = person_anchor(w, hero(std::string(), ruin.uid));
    check::is_true(a.node == ruin.node && a.uid == ruin.uid, "an empty presence falls through to a ruined home");
    check::equal(must(a.source, "a source"), std::string_view("ruins[].uid"), "at the ruin");
    a = person_anchor(w, hero(std::string(), std::nullopt));
    check::is_true(!a.node && !a.uid && !a.unresolved_uid, "an empty presence and no home: nothing, not ''");

    NpcPerson filed;
    filed.site_uid = "no-such-site";
    a = person_anchor(w, filed);
    check::is_true(!a.node && a.uid == "no-such-site" && a.unresolved_uid == "no-such-site",
                   "a site key the roster does not have is unresolved");
    NpcPerson unfiled;
    a = person_anchor(w, unfiled);
    check::is_true(!a.node && !a.uid && !a.unresolved_uid, "no site key at all is not an unresolved one");
    check::equal(must(a.source, "a source"), std::string_view("npcs.sites[].node"), "the site is still the source");
    NpcPerson present_elsewhere;
    present_elsewhere.site_uid = city.uid;
    present_elsewhere.presence_node = 3;
    present_elsewhere.presence_uid = "camp-3";
    a = person_anchor(w, present_elsewhere);
    check::is_true(a.node == 3 && a.uid == "camp-3", "a presence node beats the filed site");
}

TEST(read_a_cast_presence_at_a_ruined_city_still_resolves_to_its_node) {
    const World& w = genesis();
    std::map<std::string, int> ruins;
    for (const Ruin& r : w.ruins) ruins[r.uid] = r.node;
    check::is_true(!ruins.empty(), "this world has ruins, so the branch is tested");
    std::size_t at_ruin = 0, at_city = 0;
    const auto look = [&](const PersonAnchor& a, const std::string& who) {
        if (a.source && *a.source == "ruins[].uid") {
            ++at_ruin;
            const std::string& uid = must(a.uid, "a uid");
            check::is_true(ruins.count(uid) == 1, who + " names a real ruin");
            check::equal(must(a.node, "a node"), ruins.at(uid), who + " at the ruin's node");
        } else if (a.source && *a.source == "settlements.sites[].uid") {
            ++at_city;
        } else {
            check::is_true(!a.node && a.unresolved_uid == a.uid, who + " unresolved rather than guessed");
        }
    };
    for (const Hero& h : w.heroes.people) look(person_anchor(w, h), h.uid);
    for (const HeroDread& d : w.heroes.dreads) look(person_anchor(w, d), d.uid);
    check::is_true(at_ruin > 0, "someone in this world stands at a ruin");
    check::is_true(at_city > 0, "someone stands in a city");

    // A standing city wins over a ruin carrying the same uid, so a rebuilt name never resolves
    // to the grave of the town it replaced.
    World forged = w;
    const SettlementSite& site = forged.settlements.sites.front();
    Ruin grave;
    grave.uid = site.uid;
    grave.node = site.node + 1;
    grave.id = "ruin-x";
    forged.ruins.push_back(grave);
    check::equal(settlement_nodes(forged).at(site.uid), site.node, "the standing city wins");
}

TEST(read_a_villain_is_a_person_too) {
    const World& w = genesis();
    check::is_true(!w.villains.people.empty(), "the world has villains");
    for (const Villain& v : w.villains.people) {
        PersonReadRequest request;
        request.uid = v.uid;
        const PersonDocument d = read_person(w, request).person;
        check::is_true(d.block == LivenessBlock::Villains, "a villain's block");
        check::is_true(d.liveness == liveness(v), "its liveness");
        check::equal(must(d.anchor.node, "a node"), v.node, "its node");
    }
}

TEST(read_person_refusals_name_their_field) {
    const World& w = genesis();
    PersonReadRequest request;
    check::equal(refused([&] { (void)read_person(w, request); }, "an empty uid").field(), std::string("uid"),
                 "names uid");
    // A block is never answered as a person.
    request.uid = "npcs";
    check::equal(refused([&] { (void)read_person(w, request); }, "a block name").field(), std::string("npcs"),
                 "unknown uid");
    const std::string uid = w.npcs.people.front().uid;
    request.uid = uid.substr(0, uid.size() - 1);
    WorldTimeError typo = refused([&] { (void)read_person(w, request); }, "a typo");
    check::is_true(has(typo.candidates(), uid), "the real uid is suggested");

    World empty;
    request.uid = "anyone";
    WorldTimeError missing = refused([&] { (void)read_person(empty, request); }, "no cast");
    check::equal(missing.field(), std::string("world.heroes"), "names the heroes block");
    check::is_true(!missing.retry(), "not retryable");
}

// ===========================================================================
// quests
// ===========================================================================

TEST(read_quests_returns_the_board_the_time_api_wrote_in_its_order) {
    const World& w = ticked();
    const QuestsRead read = read_quests(w);
    const QuestBoard& board = *w.quests;
    check::equal(read.quests.size(), board.quests.size(), "every row");
    for (std::size_t i = 0; i < board.quests.size(); ++i) {
        check::equal(read.quests[i].quest_id, board.quests[i].quest_id, "the board's own order");
        check::is_true(read.quests[i].state == board.quests[i].state, "state carried");
        check::is_true(read.quests[i].anchor == board.quests[i].anchor && read.quests[i].verb == board.quests[i].verb,
                       "anchor and verb carried verbatim");
    }
    check::equal(read.counts.total, board.quests.size(), "counts total");
    std::size_t summed = 0;
    for (const QuestState s : quest_states()) summed += read.counts.at(s);
    check::equal(summed, read.counts.total, "the per-state counts cover the board");
    check::is_true(!read.counts.unknown_state, "no state outside the vocabulary");
    check::is_true(!read.states, "no filter echoed");
    check::equal(read.row_ceiling, kMaxQuestRows, "the ceiling");
}

TEST(read_every_quest_giver_resolves) {
    const World& w = ticked();
    const QuestsRead read = read_quests(w);
    check::is_true(!read.quests.empty(), "the board has quests");
    const std::map<std::string, PersonEntry> index = person_index(w);
    std::set<LivenessBlock> blocks;
    for (const QuestRow& q : read.quests) {
        const std::string& uid = must(q.giver_uid, q.quest_id + " to have a giver");
        const QuestGiverRead& giver = must(q.giver, q.quest_id + "'s giver to resolve");
        check::equal(giver.uid, uid, "giver uid");
        check::is_true(index.count(uid) == 1 && giver.block == index.at(uid).block, "giver block");
        const std::size_t at = index.at(uid).position;
        blocks.insert(giver.block);
        // The reference's `or` chains: the display name, else the name; the role, else the post.
        if (giver.block == LivenessBlock::Heroes) {
            const Hero& h = w.heroes.people.at(at);
            check::equal(must(giver.name, "a name"), h.display_name.empty() ? h.name : h.display_name, "hero name");
            check::equal(must(giver.role, "a role"), h.role, "hero role");
        } else if (giver.block == LivenessBlock::Dreads) {
            const HeroDread& d = w.heroes.dreads.at(at);
            check::equal(must(giver.name, "a name"), d.display_name.empty() ? d.name : d.display_name, "dread name");
            check::equal(must(giver.role, "a role"), d.role, "dread role");
        } else if (giver.block == LivenessBlock::Npcs) {
            const NpcPerson& p = w.npcs.people.at(at);
            check::is_true(giver.name == p.name, "an npc giver's name");
            check::equal(must(giver.role, "a post"), p.post, "an npc's post stands in for its role");
        }
        check::is_true(giver.liveness == must(person_document(w, uid), "the giver").liveness, "giver liveness");
    }
    check::is_true(!blocks.empty(), "givers resolved");
}

// Every giver block on one hand-built board: the generated board's givers are all heroes, so
// the other branches of the `or` chains would otherwise never run.
TEST(read_a_quest_giver_is_named_in_its_own_blocks_words) {
    World w = genesis();
    const Hero& hero = w.heroes.people.front();
    const NpcPerson& npc = w.npcs.people.front();
    const Villain& villain = w.villains.people.front();
    w.quests = QuestBoard{};
    const auto offer = [&w](const std::string& id, const std::optional<std::string>& giver) {
        QuestRecord q;
        q.quest_id = id;
        q.giver_uid = giver;
        w.quests->quests.push_back(q);
    };
    offer("q-hero", hero.uid);
    offer("q-npc", npc.uid);
    offer("q-villain", villain.uid);
    offer("q-nobody", std::string("no-such-person"));
    offer("q-null", std::nullopt);
    if (!w.heroes.dreads.empty()) offer("q-dread", w.heroes.dreads.front().uid);
    std::map<std::string, QuestRow> rows;
    for (QuestRow& row : read_quests(w).quests) rows.emplace(row.quest_id, std::move(row));

    const QuestGiverRead& h = must(rows.at("q-hero").giver, "a hero giver");
    check::equal(must(h.name, "a name"), hero.display_name.empty() ? hero.name : hero.display_name, "the display name");
    check::equal(must(h.role, "a role"), hero.role, "the hero's role");
    const QuestGiverRead& n = must(rows.at("q-npc").giver, "an npc giver");
    check::is_true(n.block == LivenessBlock::Npcs && n.name == npc.name, "an npc's name");
    check::equal(must(n.role, "a post"), npc.post, "an npc has no role; its post stands in");
    const QuestGiverRead& v = must(rows.at("q-villain").giver, "a villain giver");
    check::is_true(v.block == LivenessBlock::Villains && !v.name && !v.role, "a villain carries neither");
    check::equal(must(v.anchor.node, "a node"), villain.node, "and stands on its own node");
    check::is_true(!rows.at("q-nobody").giver && rows.at("q-nobody").giver_uid,
                   "an unknown giver is named, not resolved");
    check::is_true(!rows.at("q-null").giver && !rows.at("q-null").giver_uid, "a null giver stays null");
    if (!w.heroes.dreads.empty()) {
        const HeroDread& dread = w.heroes.dreads.front();
        const QuestGiverRead& d = must(rows.at("q-dread").giver, "a dread giver");
        check::equal(must(d.role, "a role"), dread.role, "a dread's role");
        check::equal(must(d.name, "a name"), dread.display_name.empty() ? dread.name : dread.display_name, "its name");
    }
}

TEST(read_a_state_filter_selects_and_never_invents) {
    const World& w = ticked();
    const QuestsRead everything = read_quests(w);
    QuestsReadRequest request;
    request.states = std::vector<QuestState>{QuestState::Offered};
    const QuestsRead offered = read_quests(w, request);
    std::vector<std::string> expected;
    for (const QuestRow& q : everything.quests) {
        if (q.state == QuestState::Offered) expected.push_back(q.quest_id);
    }
    std::vector<std::string> got;
    for (const QuestRow& q : offered.quests) got.push_back(q.quest_id);
    check::is_true(got == expected && !got.empty(), "the offered rows, in board order");
    check::equal(offered.counts.total, everything.counts.total, "counts are over the whole board");
    request.states = std::vector<QuestState>{};
    check::is_true(read_quests(w, request).quests.empty(), "an empty filter selects nothing");
}

TEST(read_quests_refusals_name_their_field) {
    WorldTimeError never = refused([] { (void)read_quests(genesis()); }, "a world never advanced");
    check::equal(never.field(), std::string("world.quests"), "no board is a missing block, not an empty list");
    check::is_true(!never.retry(), "not retryable");
    check::is_true(contains(never.message(), "advance_world_time"), "names the call that writes it");

    QuestsReadRequest request;
    request.states = std::vector<QuestState>{QuestState::Taken, static_cast<QuestState>(9)};
    WorldTimeError bad = refused([&] { (void)read_quests(ticked(), request); }, "a state outside the vocabulary");
    check::equal(bad.field(), std::string("states"), "names states");
    check::is_true(contains(bad.message(), "'offered', 'taken', 'expired', 'completed', 'failed', 'abandoned'"),
                   "lists the vocabulary");

    // The row ceiling, on a hand-built board: no generated world reaches it.
    World w;
    w.quests = QuestBoard{};
    for (std::size_t i = 0; i <= kMaxQuestRows; ++i) {
        QuestRecord q;
        q.quest_id = "q-" + std::to_string(i);
        w.quests->quests.push_back(q);
    }
    WorldTimeError over = refused([&] { (void)read_quests(w); }, "one row past the ceiling");
    check::equal(over.code(), kRefusalStateCapacity, "STATE_CAPACITY");
    check::equal(over.field(), std::string("quests.rows"), "names the rows");
    w.quests->quests.pop_back();
    check::equal(read_quests(w).quests.size(), kMaxQuestRows, "the ceiling itself is answered");
    QuestsReadRequest narrow;
    narrow.states = std::vector<QuestState>{QuestState::Taken};
    w.quests->quests.push_back(QuestRecord{});
    check::is_true(read_quests(w, narrow).quests.empty(), "the remedy -- a narrower filter -- works");

    // A row whose state is outside the vocabulary is counted apart, not dropped.
    World odd;
    odd.quests = QuestBoard{};
    QuestRecord strange;
    strange.quest_id = "q-strange";
    strange.state = static_cast<QuestState>(9);
    odd.quests->quests.push_back(strange);
    odd.quests->quests.push_back(QuestRecord{});
    const QuestCounts counts = read_quests(odd).counts;
    check::equal(counts.total, std::size_t{2}, "the whole board");
    check::equal(must(counts.unknown_state, "an unknown_state count"), std::size_t{1}, "counted apart");
    check::equal(counts.at(QuestState::Offered), std::size_t{1}, "the rest by state");
}

// ===========================================================================
// What every read shares
// ===========================================================================

TEST(read_every_read_refuses_an_api_version_it_does_not_speak) {
    const World& w = ticked();
    const auto check_version = [](const WorldTimeError& e, const std::string& name) {
        check::equal(e.code(), kRefusalUnsupportedVersion, name + " UNSUPPORTED_VERSION");
        check::equal(e.field(), std::string("api_version"), name + " names api_version");
        check::is_true(!e.retry(), name + " is not retryable");
    };
    BlocksReadRequest blocks;
    blocks.api_version = 2;
    check_version(refused([&] { (void)read_blocks(w, blocks); }, "blocks"), "blocks");
    NearReadRequest near;
    near.api_version = 0;
    near.node = 0;
    check_version(refused([&] { (void)read_near(w, near); }, "near"), "near");
    PlaceReadRequest place;
    place.api_version = 2;
    place.place_id = w.settlements.sites.front().uid;
    check_version(refused([&] { (void)read_place(w, place); }, "place"), "place");
    PersonReadRequest person;
    person.api_version = 2;
    person.uid = w.npcs.people.front().uid;
    check_version(refused([&] { (void)read_person(w, person); }, "person"), "person");
    QuestsReadRequest quests;
    quests.api_version = 2;
    check_version(refused([&] { (void)read_quests(w, quests); }, "quests"), "quests");
    PatchReadRequest patch;
    patch.api_version = 2;
    check_version(refused([&] { (void)read_patch(w, patch); }, "patch"), "patch");
}

TEST(read_no_read_mutates_mints_or_records) {
    const World& w = ticked();
    const std::string before = everything_of(w);
    const std::size_t operations = w.history.operations.size();

    const BlocksRead blocks = read_blocks(w);
    NearReadRequest near;
    near.node = w.settlements.sites.front().node;
    near.radius_m = 5000.0;
    const NearRead nearby = read_near(w, near);
    PlaceReadRequest place;
    place.place_id = w.settlements.sites.front().uid;
    const PlaceRead where = read_place(w, place);
    PersonReadRequest person;
    person.uid = w.npcs.people.front().uid;
    const PersonRead who = read_person(w, person);
    const QuestsRead quests = read_quests(w);
    PatchReadRequest patch;
    const PatchRead ground = read_patch(w, patch);

    same_text(everything_of(w), before, "no read touched the caller's world");
    check::equal(w.history.operations.size(), operations, "no read recorded an operation");
    const std::pair<const ReadEnvelope*, std::string_view> envelopes[] = {
        {&blocks.envelope, "blocks"}, {&nearby.envelope, "near"},   {&where.envelope, "place"},
        {&who.envelope, "person"},    {&quests.envelope, "quests"}, {&ground.envelope, "patch"}};
    for (const auto& [e, name] : envelopes) {
        check::equal(e->read, name, "the read names itself");
        check::equal(e->schema, "fantasy-world-generator.read." + std::string(name), "its schema");
        check::equal(e->schema_version, kReadSchemaVersion, "schema version");
        check::equal(e->api_version, kReadApiVersion, "api version");
        check::equal(e->source.seed, w.config.seed, "which world: seed");
        check::equal(e->source.size, w.config.size, "size");
        check::equal(e->source.phase, w.config.phase, "phase");
        check::equal(e->source.generator_version, w.generator_version, "generator version");
        check::equal(must(e->source.day, "a day"), must(w.world_clock, "a clock").day, "the clock's day");
        check::equal(must(e->source.age, "an age"), must(w.world_clock, "a clock").age, "the clock's age");
    }
    check::is_true(!read_blocks(genesis()).envelope.source.day, "a world never advanced has no clock to name");
}

TEST(read_two_identical_reads_are_identical) {
    const World& w = ticked();
    NearReadRequest near;
    near.node = w.settlements.sites.back().node;
    near.radius_m = 4000.0;
    PlaceReadRequest place;
    place.place_id = w.settlements.sites.back().uid;
    PersonReadRequest person;
    person.uid = w.heroes.people.front().uid;
    same_text(digest(read_blocks(w)), digest(read_blocks(w)), "blocks");
    same_text(digest(read_near(w, near)), digest(read_near(w, near)), "near");
    same_text(digest(read_place(w, place)), digest(read_place(w, place)), "place");
    same_text(digest(read_person(w, person)), digest(read_person(w, person)), "person");
    same_text(digest(read_quests(w)), digest(read_quests(w)), "quests");
    // And a copy of the world reads the same: nothing keys on an address.
    const World copy = w;
    same_text(digest(read_near(copy, near)), digest(read_near(w, near)), "near over a copy");
    same_text(digest(read_place(copy, place)), digest(read_place(w, place)), "place over a copy");
}

// ===========================================================================
// patch -- this port's addition
// ===========================================================================

TEST(read_patch_cuts_the_world_it_is_handed) {
    const World& w = genesis();
    PatchReadRequest request;
    request.patch.span = 32.0;
    request.patch.spacing = 2.0;
    const PatchRead read = read_patch(w, request);
    const Patch direct = generate_patch(w, request.patch);
    check::equal(read.envelope.read, std::string_view("patch"), "envelope");
    check::is_true(read.patch.height == direct.height && read.patch.local_y == direct.local_y,
                   "exactly generate_patch over the same world");

    // Never regenerated: raise the held world's ground and the patch's base rises with it. A
    // read that rebuilt the world from its config would not see the change. (The base is the
    // coarse bilinear sample of layers.height, which is height less the reported relief.)
    World raised = w;
    for (double& h : raised.layers.at("height").cells()) h += 100.0;
    const PatchRead higher = read_patch(raised, request);
    check::equal(higher.patch.height.size(), read.patch.height.size(), "same mesh");
    for (std::size_t i = 0; i < read.patch.height.size(); i += 97) {
        const double before = read.patch.height[i] - read.patch.detail[i];
        const double after = higher.patch.height[i] - higher.patch.detail[i];
        check::close(after - before, 100.0, 1e-6, "the patch samples the world it was handed");
    }
}

TEST(read_patch_refusals_are_the_patch_modules_own) {
    const World& w = genesis();
    PatchReadRequest request;
    request.patch.spacing = 0.0;
    try {
        (void)read_patch(w, request);
        check::fail("expected a PatchError for a zero spacing");
    } catch (const PatchError& refused_patch) {
        check::equal(refused_patch.refusal().field, std::string("patch.spacing"), "names the patch field");
        check::equal(refused_patch.refusal().kind, std::string_view("out_of_range"), "with its bound");
    }
    request.patch.spacing = 0.5;
    World tiny = w;
    tiny.effective.globe_radius = 100.0;  // half of it is under the default 64 m span
    try {
        (void)read_patch(tiny, request);
        check::fail("expected a PatchError for a span the world cannot hold");
    } catch (const PatchError& refused_patch) {
        check::is_true(refused_patch.refusal().refused_by_world, "the world refused it");
    }
}
