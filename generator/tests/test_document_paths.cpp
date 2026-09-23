// The world document's key tree against the reference's, as a checked-in contract.
//
// tests/fixtures/reference-world-paths-v1.json holds every normalized key path and every layer
// name of the reference's PUBLISHED document -- fantasy_world_generator.cli.world_document, so
// build_stages and timing_ms are not in it -- for one request; its provenance names the
// reference commit, the request and the date. This generates the same request here and
// requires every one of those paths and layers in this port's document, less the allowlists
// below. The worlds differ by design, so what is pinned is the key tree, not the values: the
// comparison doc_parity.py used to make by hand, kept from regressing.
//
// Normalization is doc_parity.py's: a dict child is `<parent>.<key>`, a list item
// `<parent>[]`; every key of the six id-keyed maps is `<k>`, any other all-digit key `<n>`
// and any `god_*` key `<god>`. Every list item is walked. Layer names are read off the
// document's own `layers` object, where the path tree sees only `.layers.<k>`.
//
// Two ways to fail, both on purpose:
//   * a fixture path or layer this document lacks and no entry excuses: a writer dropped or
//     renamed a key -- or this world stopped populating a structure, and an entry must say so;
//   * an entry that excuses nothing, because this document now writes everything under it:
//     the allowlist only shrinks, and a stale entry is how it would silently grow.
#include "check.hpp"

#include <array>
#include <cstdint>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fwg/json_value.hpp"
#include "fwg/serialize.hpp"
#include "fwg/world.hpp"
#include "product_fixtures.hpp"

using namespace fwg;

namespace {

// ===========================================================================
// What this port's document may lack, and why, one line each. A path root excuses itself
// and every path beneath it.
// ===========================================================================

struct Excused {
    std::string_view root;
    std::string_view reason;
};

constexpr std::array kAllowlist = {
    // This world, not this writer: structures the reference's world populates for this request
    // and this port's world does not. The port writes each when its world has one.
    Excused{".city_plans.cities[].fortifications.towers[].plot_id", "no wall tower in this world stands on a plot"},
    Excused{".city_plans.cities[].plots[].housing_upgrade", "no city plot in this world was upgraded"},
    Excused{".key_locations.sites[].suitability_factors.erosion", "no natural arch places"},
    Excused{".nomads.groups[].basis.nearest_seat_m", "no band in this world rides for a prize"},
    Excused{".nomads.groups[].basis.prize_distance_m", "no band in this world rides for a prize"},
    Excused{".nomads.groups[].basis.prize_uid", "no band in this world rides for a prize"},
};

// Layer names the document may lack: none, every reference layer is published.
constexpr std::array<Excused, 0> kLayerAllowlist{};

bool under(std::string_view path, std::string_view root) {
    if (path.size() < root.size() || path.substr(0, root.size()) != root) return false;
    if (path.size() == root.size()) return true;
    const char next = path[root.size()];
    return next == '.' || next == '[';
}

// ===========================================================================
// The normalized key tree of a document, in one pass over its text.
// ===========================================================================

struct Tree {
    std::set<std::string> paths;
    std::set<std::string> layers;  // the `layers` object's own keys
};

class KeyTree {
public:
    explicit KeyTree(std::string_view text) : text_(text) {}

    Tree walk() {
        pos_ = 0;
        path_.clear();
        value();
        skip();
        check::is_true(pos_ == text_.size(), "the document to be one JSON value");
        return std::move(out_);
    }

private:
    static bool id_keyed(std::string_view parent) {
        for (const std::string_view map : {".encounters.by_node", ".villains.tiers", ".story_web.summary.offered",
                                           ".layers", ".religion.faiths", ".magic.networks"}) {
            if (parent == map) return true;
        }
        return false;
    }

    static bool all_digits(std::string_view key) {
        std::size_t i = !key.empty() && key[0] == '-' ? 1 : 0;
        if (i >= key.size()) return false;
        for (; i < key.size(); ++i) {
            if (key[i] < '0' || key[i] > '9') return false;
        }
        return true;
    }

    static bool god_key(std::string_view key) {
        if (key.size() <= 4 || key.substr(0, 4) != "god_") return false;
        for (const char c : key.substr(4)) {
            if (!((c >= 'a' && c <= 'z') || c == '_')) return false;
        }
        return true;
    }

    char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }

    void skip() {
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\n' || text_[pos_] == '\r' || text_[pos_] == '\t')) {
            ++pos_;
        }
    }

    // A string's raw text between its quotes. Keys are compared as written: every key the
    // fixture carries is plain ASCII, so no escape needs decoding to match one.
    std::string_view string_token() {
        check::is_true(peek() == '"', "a string at offset " + std::to_string(pos_));
        const std::size_t start = ++pos_;
        while (pos_ < text_.size() && text_[pos_] != '"') pos_ += text_[pos_] == '\\' ? 2 : 1;
        check::is_true(pos_ < text_.size(), "a closed string");
        return text_.substr(start, pos_++ - start);
    }

    void object() {
        ++pos_;
        skip();
        if (peek() == '}') {
            ++pos_;
            return;
        }
        const std::size_t parent = path_.size();
        const bool maps_ids = id_keyed(path_);
        const bool layer_names = path_ == ".layers";
        while (true) {
            skip();
            const std::string_view key = string_token();
            if (layer_names) out_.layers.emplace(key);
            path_ += '.';
            if (maps_ids) {
                path_ += "<k>";
            } else if (all_digits(key)) {
                path_ += "<n>";
            } else if (god_key(key)) {
                path_ += "<god>";
            } else {
                path_ += key;
            }
            if (out_.paths.count(path_) == 0) out_.paths.insert(path_);
            skip();
            check::is_true(peek() == ':', "a ':' after a key");
            ++pos_;
            value();
            path_.resize(parent);
            skip();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            check::is_true(peek() == '}', "a '}' closing an object");
            ++pos_;
            return;
        }
    }

    void array() {
        ++pos_;
        skip();
        if (peek() == ']') {
            ++pos_;
            return;
        }
        const std::size_t parent = path_.size();
        path_ += "[]";
        while (true) {
            value();
            skip();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            check::is_true(peek() == ']', "a ']' closing an array");
            ++pos_;
            break;
        }
        path_.resize(parent);
    }

    void value() {
        skip();
        const char c = peek();
        check::is_true(c != '\0', "a value at offset " + std::to_string(pos_));
        if (c == '{') {
            object();
        } else if (c == '[') {
            array();
        } else if (c == '"') {
            string_token();
        } else {
            // A number or a literal: nothing beneath it.
            while (pos_ < text_.size() && text_[pos_] != ',' && text_[pos_] != '}' && text_[pos_] != ']' &&
                   text_[pos_] != ' ' && text_[pos_] != '\n' && text_[pos_] != '\r' && text_[pos_] != '\t') {
                ++pos_;
            }
        }
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    std::string path_;
    Tree out_;
};

std::string listed(const std::vector<std::string>& rows) {
    std::string out;
    for (const std::string& row : rows) out += "\n    " + row;
    return out;
}

struct Fixture {
    std::uint32_t seed = 0;
    int size = 0;
    std::vector<std::string> paths;
    std::vector<std::string> layers;
};

const Fixture& fixture() {
    static const Fixture f = [] {
        const JsonValue doc = product_test::fixture("reference-world-paths-v1.json");
        const JsonValue& request = doc.at("provenance").at("request");
        check::equal(request.at("recipe_version").as_int(), std::int64_t{3}, "the fixture's recipe version");
        Fixture out;
        out.seed = static_cast<std::uint32_t>(request.at("seed").as_int());
        out.size = static_cast<int>(request.at("overrides").at("size").as_int());
        for (const JsonValue& p : doc.at("paths").as_array()) out.paths.push_back(p.as_string());
        for (const JsonValue& l : doc.at("layers").as_array()) out.layers.push_back(l.as_string());
        return out;
    }();
    return f;
}

// This port's published document for the fixture's own request, walked once.
const Tree& ours() {
    static const Tree tree = [] {
        Request request;
        request.seed = fixture().seed;
        request.size = fixture().size;
        request.phase = 16;
        const World world = generate(resolve(request));
        std::ostringstream text;
        DocumentOptions options;
        options.pretty = false;
        write_world_json(text, world, options);
        return KeyTree(text.str()).walk();
    }();
    return tree;
}

// The names in `wanted` that `have` lacks and no entry excuses, and the entries that excuse
// nothing `have` lacks.
template <std::size_t N>
std::pair<std::vector<std::string>, std::vector<std::string>> audit(const std::vector<std::string>& wanted,
                                                                    const std::set<std::string>& have,
                                                                    const std::array<Excused, N>& allowlist) {
    std::vector<std::string> unexcused;
    std::set<std::string_view> used;
    for (const std::string& name : wanted) {
        if (have.count(name) != 0) continue;
        bool excused = false;
        for (const Excused& e : allowlist) {
            if (under(name, e.root)) {
                used.insert(e.root);
                excused = true;
            }
        }
        if (!excused) unexcused.push_back(name);
    }
    std::vector<std::string> stale;
    for (const Excused& e : allowlist) {
        if (used.count(e.root) == 0) stale.push_back(std::string(e.root) + "  (" + std::string(e.reason) + ")");
    }
    return {unexcused, stale};
}

}  // namespace

TEST(the_document_carries_every_reference_path_the_allowlist_does_not_excuse) {
    const Fixture& ref = fixture();
    check::is_true(ref.paths.size() > 6000, "a fixture of the whole document, not a fragment");
    const auto [unexcused, stale] = audit(ref.paths, ours().paths, kAllowlist);
    check::is_true(unexcused.empty(),
                   "every reference path in this document; missing, and excused by no allowlist entry:" +
                       listed(unexcused));
    check::is_true(stale.empty(),
                   "every allowlist entry to excuse a path this document lacks; these excuse nothing -- the "
                   "document writes them now, or the fixture has no such path -- so remove them:" +
                       listed(stale));
}

TEST(the_document_publishes_every_reference_layer) {
    const Fixture& ref = fixture();
    check::is_true(ref.layers.size() > 100, "a fixture carrying the reference's layer names");
    const auto [unexcused, stale] = audit(ref.layers, ours().layers, kLayerAllowlist);
    check::is_true(unexcused.empty(), "every reference layer in this document; missing:" + listed(unexcused));
    check::is_true(stale.empty(), "every layer allowlist entry to excuse a missing layer:" + listed(stale));
}
