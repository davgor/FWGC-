// The world document's late blocks: stages 12, 13 and 16.
//
// What these pin, and why each is a separate question:
//
//   * PRESENCE follows the World. Every late block is written when the World says it
//     exists and not otherwise -- a phase-16 world carries all of them, a phase-11 world
//     none -- and each presence assertion is paired with the World flag it rests on, so a
//     generator that stopped producing a block fails here as "the world has no X" rather
//     than passing an empty document.
//   * BYTES are a function of the seed. Two generations serialise identically.
//   * The document is STRICT JSON. There is no JSON library in this tree, so this file
//     carries a small parser that refuses everything RFC 8259 refuses -- trailing commas,
//     bare NaN or Infinity, raw control characters, malformed UTF-8 -- plus two things
//     it permits that a world document must not do: a non-finite number (1e999 is
//     grammatical JSON and parses to infinity) and a duplicate key in one object.
//   * KEY NAMES are the reference's. A generated C++ world cannot reach every branch --
//     without age transitions there are no ruins, wars, warlords, survivor bands, cult
//     sites or successions -- so those writers are driven with hand-built records here,
//     and each expected key set is the one the reference's own generated world carries for
//     that record type (seed 42, size 65, phase 16; printed from its key tree rather than
//     recalled). Where the reference world has no such record either, the set comes from
//     the reference writer's source, cited beside it.
#include "check.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "fwg/serialize.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

// ===========================================================================
// A strict JSON reader
// ===========================================================================

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> members;

    const Json* find(std::string_view key) const {
        for (const auto& [name, value] : members) {
            if (name == key) return &value;
        }
        return nullptr;
    }
    const Json& at(std::string_view key) const {
        const Json* value = find(key);
        if (value == nullptr) check::fail("the document to carry key '" + std::string(key) + "'");
        return *value;
    }
    bool has(std::string_view key) const { return find(key) != nullptr; }
    std::set<std::string> keys() const {
        std::set<std::string> out;
        for (const auto& member : members) out.insert(member.first);
        return out;
    }
};

class StrictJson {
public:
    // `keep`, when given, names the top-level keys whose values are built into the tree;
    // every other value is validated and dropped, so a document with forty thousand
    // numbers a raster can be checked without holding a node per number. `hollow` names
    // keys, below the top level, whose values are validated but left as a null in the tree:
    // the key is still there to be counted, the raster behind it is not.
    StrictJson(std::string_view text, const std::set<std::string>* keep = nullptr,
               const std::set<std::string>* hollow = nullptr)
        : text_(text), keep_(keep), hollow_(hollow) {}

    Json parse() {
        Json root;
        skip();
        value(&root, 0);
        skip();
        if (pos_ != text_.size()) error("trailing bytes after the document");
        return root;
    }

    std::size_t numbers() const { return numbers_; }
    // Every top-level key, kept or not.
    const std::vector<std::string>& top_level() const { return top_level_; }

private:
    [[noreturn]] void error(const std::string& what) const {
        check::fail("strict JSON: " + what + " at byte " + std::to_string(pos_));
    }

    void skip() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            ++pos_;
        }
    }

    void expect_literal(std::string_view word) {
        if (text_.substr(pos_, word.size()) != word) error("a malformed literal");
        pos_ += word.size();
    }

    void utf8_continuation(int count) {
        for (int k = 0; k < count; ++k) {
            if (pos_ >= text_.size() || (static_cast<unsigned char>(text_[pos_]) & 0xC0) != 0x80) {
                error("malformed UTF-8");
            }
            ++pos_;
        }
    }

    std::string string_token() {
        if (pos_ >= text_.size() || text_[pos_] != '"') error("a string");
        ++pos_;
        std::string out;
        while (true) {
            if (pos_ >= text_.size()) error("an unterminated string");
            const unsigned char c = static_cast<unsigned char>(text_[pos_]);
            if (c == '"') {
                ++pos_;
                return out;
            }
            if (c < 0x20) error("a raw control character in a string");
            if (c == '\\') {
                ++pos_;
                if (pos_ >= text_.size()) error("an unterminated escape");
                const char e = text_[pos_++];
                switch (e) {
                    case '"': case '\\': case '/': out.push_back(e); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        if (pos_ + 4 > text_.size()) error("a short \\u escape");
                        for (int k = 0; k < 4; ++k) {
                            const char h = text_[pos_++];
                            const bool hex = (h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F');
                            if (!hex) error("a malformed \\u escape");
                        }
                        out.push_back('?');  // the code point itself is not needed here
                        break;
                    }
                    default: error("an unknown escape");
                }
                continue;
            }
            // UTF-8: a lead byte and the continuation bytes it announces.
            const std::size_t start = pos_;
            ++pos_;
            if (c >= 0x80) {
                if (c >= 0xC2 && c <= 0xDF) utf8_continuation(1);
                else if (c >= 0xE0 && c <= 0xEF) utf8_continuation(2);
                else if (c >= 0xF0 && c <= 0xF4) utf8_continuation(3);
                else error("malformed UTF-8");
            }
            out.append(text_.substr(start, pos_ - start));
        }
    }

    double number_token() {
        const std::size_t start = pos_;
        if (pos_ < text_.size() && text_[pos_] == '-') ++pos_;
        const auto digits = [&]() {
            const std::size_t first = pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
            return pos_ - first;
        };
        if (pos_ < text_.size() && text_[pos_] == '0') {
            ++pos_;
        } else if (digits() == 0) {
            error("a number");
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            if (digits() == 0) error("digits after a decimal point");
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            if (digits() == 0) error("digits in an exponent");
        }
        double value = 0.0;
        const auto result = std::from_chars(text_.data() + start, text_.data() + pos_, value);
        // 1e999 is grammatical and overflows: that is the writer's spelling of infinity,
        // which is exactly what must never reach a world document.
        if (result.ec != std::errc() || !std::isfinite(value)) {
            error("a non-finite number " + std::string(text_.substr(start, pos_ - start)));
        }
        ++numbers_;
        return value;
    }

    void value(Json* out, int depth) {
        if (depth > 200) error("nesting deeper than any world document");
        skip();
        if (pos_ >= text_.size()) error("a value");
        const char c = text_[pos_];
        if (c == '{') {
            ++pos_;
            if (out != nullptr) out->type = Json::Type::Object;
            std::set<std::string> seen;
            skip();
            if (pos_ < text_.size() && text_[pos_] == '}') {
                ++pos_;
                return;
            }
            while (true) {
                skip();
                std::string key = string_token();
                if (!seen.insert(key).second) error("a duplicate key '" + key + "'");
                if (depth == 0) top_level_.push_back(key);
                skip();
                if (pos_ >= text_.size() || text_[pos_] != ':') error("a ':'");
                ++pos_;
                Json* child = nullptr;
                const bool build = out != nullptr && (depth > 0 || keep_ == nullptr || keep_->count(key) > 0);
                if (build) {
                    out->members.emplace_back(key, Json{});
                    child = &out->members.back().second;
                    if (depth > 0 && hollow_ != nullptr && hollow_->count(key) > 0) child = nullptr;
                }
                value(child, depth + 1);
                skip();
                if (pos_ < text_.size() && text_[pos_] == ',') {
                    ++pos_;
                    continue;
                }
                if (pos_ < text_.size() && text_[pos_] == '}') {
                    ++pos_;
                    return;
                }
                error("',' or '}'");
            }
        }
        if (c == '[') {
            ++pos_;
            if (out != nullptr) out->type = Json::Type::Array;
            skip();
            if (pos_ < text_.size() && text_[pos_] == ']') {
                ++pos_;
                return;
            }
            while (true) {
                Json* child = nullptr;
                if (out != nullptr) {
                    out->items.emplace_back();
                    child = &out->items.back();
                }
                value(child, depth + 1);
                skip();
                if (pos_ < text_.size() && text_[pos_] == ',') {
                    ++pos_;
                    continue;
                }
                if (pos_ < text_.size() && text_[pos_] == ']') {
                    ++pos_;
                    return;
                }
                error("',' or ']'");
            }
        }
        if (c == '"') {
            std::string s = string_token();
            if (out != nullptr) {
                out->type = Json::Type::String;
                out->text = std::move(s);
            }
            return;
        }
        if (c == 't' || c == 'f') {
            expect_literal(c == 't' ? "true" : "false");
            if (out != nullptr) {
                out->type = Json::Type::Bool;
                out->boolean = c == 't';
            }
            return;
        }
        if (c == 'n') {
            expect_literal("null");
            return;
        }
        const double v = number_token();
        if (out != nullptr) {
            out->type = Json::Type::Number;
            out->number = v;
        }
    }

    std::string_view text_;
    const std::set<std::string>* keep_ = nullptr;
    const std::set<std::string>* hollow_ = nullptr;
    std::size_t pos_ = 0;
    std::size_t numbers_ = 0;
    std::vector<std::string> top_level_;
};

std::string document(const World& world, bool pretty = false) {
    std::ostringstream out;
    write_world_json(out, world, pretty, false);
    return out.str();
}

Json parse_all(const std::string& text) { return StrictJson(text).parse(); }

std::string joined(const std::set<std::string>& keys) {
    std::string out;
    for (const std::string& k : keys) out += (out.empty() ? "" : ", ") + k;
    return out;
}

// Exactly these keys: a missing one and an extra one are both named in the failure.
void same_keys(const Json& record, const std::set<std::string>& expected, const std::string& what) {
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

// Every top-level key stages 12, 13 and 16 add. `settlement_candidates` is written only
// when a survivor band reached a refuge, which needs a ruin, which needs an age turn.
const std::vector<std::string>& late_blocks() {
    static const std::vector<std::string> keys = {
        "humans", "seasonal_food", "sky", "fisheries", "transport", "world_economy", "religion",
        "city_plans", "hamlet_plans", "castle_plans", "nomads", "beast_movements", "encounters",
        "heroes", "story_web", "npcs", "key_locations", "key_location_plans"};
    return keys;
}

World phase_world(int phase) {
    Request request;
    request.seed = 42;
    request.size = 33;
    request.phase = phase;
    return generate(resolve(request));
}

// One phase-16 world for every test that reads a generated one: it costs two seconds.
const World& world16() {
    static const World world = phase_world(16);
    return world;
}

const std::string& document16() {
    static const std::string text = document(world16());
    return text;
}

// ===========================================================================
// Generated worlds
// ===========================================================================

TEST(a_phase_16_document_carries_every_late_block) {
    const World& world = world16();
    // Non-vacuity: the flags each block rests on. A world that stopped producing a block
    // fails here as that, not as a missing key.
    check::is_true(world.has_humans, "a phase-16 world to have a hinterland");
    check::is_true(world.society.ran && world.society.has_economy, "the society pass to have run its economy");
    check::is_true(world.has_religion, "a phase-16 world to have a religion");
    check::is_true(world.has_plans, "a phase-16 world to have plans");
    check::is_true(world.has_nomads && world.has_beast_movements, "bands and herds");
    check::is_true(world.has_heroes && world.has_npcs && world.has_key_locations, "the cast, roster and places");
    check::is_true(!world.society.fisheries.ports.empty(), "harbours, so the harbour rows are exercised");

    const std::set<std::string> keep = {"humans", "fisheries", "magic", "settlements", "roads", "heroes",
                                        "npcs", "key_locations", "key_location_plans", "story_web", "nomads"};
    StrictJson parser(document16(), &keep);
    const Json root = parser.parse();
    const std::set<std::string> top(parser.top_level().begin(), parser.top_level().end());
    for (const std::string& key : late_blocks()) {
        check::is_true(top.count(key) == 1, "the phase-16 document to carry '" + key + "'");
    }
    check::is_true(!root.has("settlement_candidates") || world.nomad_effects.assign_settlement_candidates,
                   "settlement_candidates only when the effects pass produced candidates");

    // The fields the late stages add inside blocks written earlier.
    const Json& magic = root.at("magic");
    check::is_true(magic.has("colleges") && magic.has("diagnostics") && magic.has("college_spacing_m") &&
                       magic.has("college_method"),
                   "the college pass's fields inside magic");
    const Json& sites = root.at("settlements").at("sites");
    check::is_true(!sites.items.empty(), "settlements to have sites");
    for (const Json& site : sites.items) check::is_true(site.has("community_traits"), "community traits on a site");

    // Every package block reports ok on a generated world.
    for (const char* key : {"heroes", "npcs", "key_locations", "key_location_plans", "story_web"}) {
        check::equal(root.at(key).at("status").text, std::string("ok"), std::string(key) + " status");
    }
}

TEST(a_phase_11_document_carries_none_of_the_late_blocks) {
    const World world = phase_world(11);
    check::is_true(world.has_roads && !world.has_humans, "phase 11 to stop after the roads");
    const std::set<std::string> keep = {"magic"};
    const std::string text = document(world);
    const Json root = StrictJson(text, &keep).parse();
    for (const std::string& key : late_blocks()) {
        check::is_true(text.find("\"" + key + "\":") == std::string::npos,
                       "no '" + key + "' anywhere in a phase-11 document");
    }
    check::is_true(text.find("\"settlement_candidates\":") == std::string::npos, "no settlement candidates");
    check::is_true(text.find("\"community_traits\":") == std::string::npos, "no community traits before stage 12");
    // The magic block exists from stage 7 and carries the reference's pending college row
    // until the college pass runs: an empty list that means "not yet", said out loud.
    check::is_true(world.has_magic, "phase 11 to have magic");
    const Json& magic = root.at("magic");
    check::equal(magic.at("colleges").items.size(), std::size_t{0}, "no colleges before stage 12");
    const Json& pending = magic.at("diagnostics").items.at(0);
    check::equal(pending.at("reason").text,
                 std::string("the college pass has not run at this phase, so nothing was evaluated"),
                 "the pending college row");
    check::is_true(!magic.has("college_method"), "no college method before the pass");
}

TEST(two_generations_of_one_seed_serialise_to_identical_bytes) {
    const World again = phase_world(16);
    const std::string a = document16();
    const std::string b = document(again);
    check::equal(a.size(), b.size(), "document size");
    check::is_true(a == b, "two generations of one seed to serialise byte-identically");
    // And the pretty form, which walks the same writers through a different separator path.
    check::is_true(document(world16(), true) == document(again, true), "the pretty form too");
}

TEST(the_phase_16_document_is_strict_json_with_every_number_finite) {
    // The parser refuses 1e999, NaN, Infinity, duplicate keys, raw control characters and
    // malformed UTF-8; getting through it is the assertion. Both forms, because pretty
    // printing is a separate path through the writer's separators.
    std::set<std::string> none;
    StrictJson compact_lean(document16(), &none);
    compact_lean.parse();
    check::is_true(compact_lean.numbers() > 100000, "the proof to have read the rasters' numbers too");
    const std::string pretty = document(world16(), true);
    StrictJson pretty_lean(pretty, &none);
    pretty_lean.parse();
    check::equal(pretty_lean.numbers(), compact_lean.numbers(), "the same numbers in both forms");
}

TEST(a_harbour_row_in_the_hamlet_list_is_the_port_record) {
    const std::set<std::string> keep = {"humans", "fisheries"};
    const Json root = StrictJson(document16(), &keep).parse();
    std::map<std::string, const Json*> ports;
    for (const Json& p : root.at("fisheries").at("ports").items) ports[p.at("id").text] = &p;
    check::is_true(!ports.empty(), "harbours to compare");
    int harbours = 0;
    for (const Json& h : root.at("humans").at("hamlets").items) {
        const auto it = ports.find(h.at("id").text);
        if (it == ports.end()) {
            check::is_true(!h.has("sea_node"), "a rural hamlet to carry no harbour fields");
            continue;
        }
        ++harbours;
        // The port's own fields, plus the uid the C++ gives the row.
        std::set<std::string> expected = it->second->keys();
        expected.insert("uid");
        same_keys(h, expected, "harbour row " + h.at("id").text);
        check::equal(h.at("uid").text, "hamlet-node-" + std::to_string(static_cast<long long>(h.at("node").number)),
                     "a harbour's uid");
        check::equal(h.at("delivered_food").number, it->second->at("delivered_food").number, "the port's catch");
    }
    check::equal(static_cast<std::size_t>(harbours), ports.size(), "every harbour in the hamlet list");
}

TEST(an_npc_row_writes_its_workplace_store_and_age_as_the_roster_holds_them) {
    // The key sets are pinned below; this pins the values, which are what a game and the
    // viewer read (2026-09-23).
    const std::set<std::string> keep = {"npcs"};
    const Json root = StrictJson(document16(), &keep).parse();
    const Json& block = root.at("npcs");
    const NpcRoster& roster = world16().npcs;
    const std::vector<Json>& people = block.at("people").items;
    check::equal(people.size(), roster.people.size(), "one row per person");
    int stores = 0;
    for (std::size_t i = 0; i < people.size(); ++i) {
        const Json& row = people[i];
        const NpcPerson& person = roster.people[i];
        check::equal(row.at("uid").text, person.uid, "the roster's order");
        const auto text_or_null = [&](std::string_view key, const std::optional<std::string>& value) {
            const Json& field = row.at(key);
            if (value) {
                check::equal(field.text, *value, person.uid + " " + std::string(key));
            } else {
                check::is_true(field.type == Json::Type::Null, person.uid + " " + std::string(key) + " to be null");
            }
        };
        text_or_null("workplace", person.workplace);
        text_or_null("store", person.store);
        if (person.store) ++stores;
        check::is_true(person.appearance.has_value(), person.uid + " to be aged");
        const Json& a = row.at("appearance");
        check::equal(a.at("gender").text, std::string(person.appearance->gender), person.uid + " gender");
        check::equal(a.at("age_band").text, std::string(person.appearance->age_band), person.uid + " age band");
        check::equal(a.at("age_years").number, static_cast<double>(person.appearance->age_years), person.uid + " age");
        check::equal(a.at("civilization_id").text, person.appearance->civilization_id, person.uid + " people");
        check::equal(a.at("parent_race_id").text, person.appearance->parent_race_id, person.uid + " race");
    }
    check::is_true(stores > 0, "storekeepers among them");

    // Each span, and each band as the roster cut it.
    const auto spans = [&](std::string_view key, const std::vector<NpcLifeSpan>& expected) {
        const std::vector<Json>& rows = block.at("life_spans").at(key).items;
        check::equal(rows.size(), expected.size(), std::string(key) + " spans");
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const NpcLifeSpan& span = expected[i];
            check::equal(rows[i].at("id").text, span.id, "the span order");
            check::equal(rows[i].at("adult_years").number, span.life.adult, span.id + " adult years");
            check::equal(rows[i].at("elder_years").number, span.life.elder, span.id + " elder years");
            check::equal(rows[i].at("max_years").number, span.life.max, span.id + " longest life");
            for (const NpcAgeBand& band : span.bands) {
                const Json& b = rows[i].at("bands").at(band.id);
                const std::string what = span.id + " " + std::string(band.id);
                check::equal(b.at("from").number, static_cast<double>(band.from), what + " from");
                check::equal(b.at("to").number, static_cast<double>(band.to), what + " to");
            }
        }
    };
    spans("parent_races", roster.parent_race_life_spans);
    spans("peoples", roster.life_spans);

    const Json& services = block.at("post_services");
    check::equal(services.members.size(), std::size_t(1), "one post offering services");
    const std::vector<Json>& horse = services.at("horse_keeper").items;
    check::is_true(horse.size() == 2 && horse[0].text == "mounts" && horse[1].text == "carriages",
                   "a horse keeper's mounts and carriages");
    check::equal(block.at("summary").at("appearance_unresolved").number, 0.0, "nobody left unaged");
}

// One element of a list, by a condition: `0` the first, `k=v` the first whose string field
// k is v, `k!=v` the first whose field k is not v, `k^=v` the first whose field starts v.
const Json* pick(const Json& list, std::string_view condition) {
    if (condition == "0") return list.items.empty() ? nullptr : &list.items.front();
    std::size_t op = condition.find_first_of("!^=");
    const std::string key(condition.substr(0, op));
    const bool negate = condition[op] == '!';
    const bool prefix = condition[op] == '^';
    const std::string_view wanted = condition.substr(condition.find('=') + 1);
    for (const Json& item : list.items) {
        const Json* field = item.find(key);
        const std::string_view text = field != nullptr ? std::string_view(field->text) : std::string_view();
        const bool match = prefix ? text.substr(0, wanted.size()) == wanted : text == wanted;
        if (field != nullptr && match != negate) return &item;
    }
    return nullptr;
}

// A dotted path whose segments may carry one [condition] each.
const Json* resolve(const Json& root, std::string_view selector) {
    const Json* node = &root;
    std::size_t pos = 0;
    while (node != nullptr && pos < selector.size()) {
        const std::size_t end = std::min(selector.find_first_of(".[", pos), selector.size());
        node = node->find(selector.substr(pos, end - pos));
        pos = end;
        if (node != nullptr && pos < selector.size() && selector[pos] == '[') {
            const std::size_t close = selector.find(']', pos);
            node = pick(*node, selector.substr(pos + 1, close - pos - 1));
            pos = close + 1;
        }
        if (pos < selector.size() && selector[pos] == '.') ++pos;
    }
    return node;
}

// The key set of one representative record of each type the generated world writes.
//
// GENERATED, NOT WRITTEN BY HAND: each set is the reference world's key set for that record
// type (FantasyWorldGenerator seed 42, size 65, phase 16, read from its own writer's output),
// with the deviations the C++ headers document applied -- `hamlet_uid`, `fortress_uid`,
// `node`, `clipped_to_terrain` and the hamlet stats' wall fields added; the per-plan
// catalogue copies, the projection string and the registry identity removed. A rename in a
// writer fails here without the reference world on hand.
struct PinnedRecord {
    const char* selector;
    std::set<std::string> keys;
};

const std::vector<PinnedRecord>& pinned_records() {
    static const std::vector<PinnedRecord> rows = {
    {"humans", {"cores", "culture_method", "cultures", "defence_method", "fortress_demand", "fortresses", "hamlets", "method", "population_profile", "shipments", "version"}},
    {"humans.cores[0]", {"culture_id", "food_deficit", "food_demand", "food_exports", "food_imports", "food_supply", "fortress_ids", "hamlet_ids", "material_supply", "population_profile", "site_id", "trade_material_cost"}},
    {"humans.hamlets[id^=hamlet-]", {"access_cost", "access_nodes", "core_id", "culture_id", "delivered_food", "delivered_materials", "height_m", "id", "irrigation_benefit", "kind", "node", "population_profile", "reason", "role", "uid", "worked_area_km2", "x", "z"}},
    {"humans.fortresses[watch=road]", {"access_cost", "access_nodes", "core_id", "core_uid", "culture_id", "defence_score", "height_m", "id", "kind", "node", "population_profile", "protected_route_node", "raised_age", "reason", "uid", "watch", "x", "z"}},
    {"humans.fortresses[watch=frontier]", {"access_cost", "access_nodes", "core_id", "core_uid", "culture_id", "defence_score", "frontier", "height_m", "id", "kind", "node", "population_profile", "protected_route_node", "raised_age", "reason", "uid", "watch", "x", "z"}},
    {"humans.fortresses[watch=frontier].frontier", {"between", "corridor_km2"}},
    {"humans.cultures[0]", {"architecture_style_id", "city_ids", "civilization_id", "id", "index", "population_profile"}},
    {"humans.shipments[0]", {"city_path", "delivered", "from", "lost", "material_cost", "sent", "to"}},
    {"humans.fortress_demand", {"capitals", "carried", "city_ceiling", "crossings", "frontier_forts", "frontiers", "limit", "player_founded", "proposed", "requested_ceiling", "road_forts", "road_length_m", "spacing_m", "support_reach", "unanswered", "veteran_cities", "veteran_demand"}},
    {"seasonal_food", {"cities", "limits", "method", "models", "months", "reported_year", "routes", "version", "years"}},
    {"seasonal_food.cities[0]", {"annual_demand", "annual_harvest", "annual_shortage", "ending_reserve", "food_coverage", "lean_months", "passes_final_year", "reserve_change", "site_id", "urban_food_equivalent"}},
    {"seasonal_food.models[0]", {"demand", "harvest", "site_id", "spoilage", "storage", "transport_budget"}},
    {"seasonal_food.routes[0]", {"capacity", "efficiency", "from", "price", "to"}},
    {"seasonal_food.months[0]", {"cities", "closing", "consumed", "harvest", "month", "opening", "overflow", "shipments", "spoiled", "transit_loss", "year"}},
    {"seasonal_food.months[0].cities[0]", {"closing", "consumed", "demand", "exports", "harvest", "imports", "material_spent", "opening", "overflow", "shortage", "spoiled"}},
    {"sky", {"area_km2", "disabled_reason", "enabled", "islands", "method", "settlements", "version"}},
    {"fisheries", {"delivered_annual_food", "method", "ports", "potential_annual_food"}},
    {"fisheries.ports[0]", {"access_cost", "access_nodes", "core_id", "culture_id", "delivered_food", "delivered_materials", "fishing_nodes", "harbor_quality", "height_m", "id", "irrigation_benefit", "kind", "landing_distance_m", "layer", "monthly_fish", "node", "population_profile", "reason", "role", "sea_node", "trade_terminal", "winter_fishing_fraction", "worked_area_km2", "x", "z"}},
    {"transport", {"routes", "sites", "terminals"}},
    {"transport.routes[mode=ground]", {"capacity", "efficiency", "from", "length_m", "mode", "nodes", "price", "to"}},
    {"transport.sites[0]", {"id", "layer"}},
    {"world_economy", {"limits", "method", "models", "months", "sites", "version"}},
    {"world_economy.sites[0]", {"annual_demand", "annual_harvest", "annual_shortage", "ending_reserve", "food_coverage", "layer", "lean_months", "name", "site_id"}},
    {"world_economy.models[0]", {"demand", "harvest", "site_id", "spoilage", "storage", "transport_budget"}},
    {"religion", {"catalogue", "corruptions", "cosmology", "facts", "faiths", "gods", "limits", "method", "sites", "version", "visitations"}},
    {"religion.facts", {"civilizations_with_cities", "diaspora", "dragons", "families", "lunar_influence", "magic_enabled", "ocean_share", "primordial_count", "races", "ruins", "schools", "sky", "wars"}},
    {"religion.cosmology", {"arrangement", "eligible", "high", "id", "name"}},
    {"city_plans", {"cities", "identity", "phase_order", "scope", "version"}},
    {"city_plans.identity", {"registry", "shapes_sha256", "version"}},
    {"city_plans.cities[status!=unbuildable]", {"bounds_m", "city_class", "city_uid", "civilization_id", "debug", "fortifications", "location", "name", "passes", "plots", "reference_frame", "road_connections", "roads", "shape", "site_id", "source_resolution_m", "stats", "status", "street_paths_m", "terrain", "unit", "unplaced", "version", "warnings"}},
    {"city_plans.cities[status!=unbuildable].plots[0]", {"beds", "building_id", "dimensions_m", "foundation_bottom_m", "ground_elevation_m", "id", "kind", "name", "phase", "plot_m", "road_access", "rotation_degrees", "workers", "x_m", "z_m"}},
    {"city_plans.cities[status!=unbuildable].stats", {"apartments", "defended_perimeter", "houses", "housing_shortfall", "population_estimate", "reserved_plot_area_m2", "service_buildings", "unplaced_core", "unplaced_total", "urban_population_estimate", "wall_segments", "worker_beds", "workers"}},
    {"city_plans.cities[status!=unbuildable].debug", {"access_cells", "apartment_policy", "fortification_rings", "housing_frontage_candidates", "housing_passes", "housing_reservations", "occupied_cells", "program_half_m", "road_cells", "shape_safe_cells", "terrain_safe_cells", "total_cells", "vacant_shape_cells"}},
    {"city_plans.cities[status!=unbuildable].location", {"aridity", "buildable_area_m2", "defense_priority", "local_slope_degrees", "regional_threat", "usable_land_fraction", "wooded_fraction"}},
    {"city_plans.cities[status!=unbuildable].reference_frame", {"latitude_degrees", "longitude_degrees", "projection", "radius_m"}},
    {"city_plans.cities[status!=unbuildable].shape", {"candidates", "catalogue_revision", "catalogue_sha256", "layout", "parameters", "reason", "runtime_geometry_generated", "shape_id", "source_ids", "version"}},
    {"hamlet_plans", {"hamlets", "identity", "phase_order", "scope", "version"}},
    {"hamlet_plans.hamlets[status!=unbuildable]", {"bounds_m", "civilization_id", "core_city_uid", "core_id", "debug", "hamlet_id", "hamlet_uid", "kind", "node", "passes", "plots", "reference_frame", "road_connections", "roads", "role", "shape", "source_resolution_m", "stats", "status", "street_paths_m", "terrain", "unit", "unplaced", "version", "warnings", "x", "z"}},
    {"hamlet_plans.hamlets[status!=unbuildable].stats", {"apartments", "houses", "housing_shortfall", "reserved_plot_area_m2", "service_buildings", "unplaced_core", "unplaced_total", "worker_beds", "workers"}},
    {"hamlet_plans.hamlets[status!=unbuildable].shape", {"family", "parameters", "shape_id"}},
    {"castle_plans", {"beds", "castles", "identity", "limits", "phase_order", "version"}},
    {"castle_plans.identity", {"castle_buildings_sha256", "castles_sha256", "geometry_version", "version"}},
    {"castle_plans.castles[status=complete]", {"bounds_m", "core_id", "debug", "fortress_id", "fortress_uid", "kind", "kit_id", "kit_name", "node", "passes", "plots", "road_connections", "roads", "source_resolution_m", "stats", "status", "terrain", "unplaced", "version", "wall_networks", "warnings", "x", "z"}},
    {"castle_plans.castles[status=complete].plots[0]", {"building_id", "dimensions_m", "foundation_bottom_m", "ground_elevation_m", "id", "kind", "name", "phase", "plot_m", "road_access", "rotation_degrees", "workers", "x_m", "z_m"}},
    {"castle_plans.castles[status=complete].stats", {"kit_id", "plots", "segments", "unplaced_total", "walkway_continuous", "wall_rings_closed", "workers"}},
    {"castle_plans.castles[status=complete].debug", {"approach_m", "geometry_version", "valid_cells"}},
    {"castle_plans.castles[status=complete].wall_networks[status=closed]", {"clipped_to_terrain", "ditch", "gate_count", "gates", "id", "nodes", "polyline_m", "role", "segment_count", "segments", "stairs", "status", "towers", "unplaced", "walkway_continuous"}},
    {"nomads", {"counts", "effects", "groups", "limits", "method", "rolls", "routes", "version"}},
    {"nomads.routes", {"day_march_m", "fissioned", "limits", "method", "routed", "stranded", "version"}},
    {"nomads.effects", {"leyline_edits", "leyline_edits_hidden", "limits", "method", "roads_ridden", "settlement_candidates", "towns_under_raid_pressure", "version"}},
    {"beast_movements", {"considered", "counts", "groups", "limits", "method", "routed", "share", "skipped", "solitary", "stranded", "version", "year", "year_forage_factor"}},
    {"beast_movements.groups[route_status=routed]", {"camps", "column_length_m", "direction", "disposition", "family", "host_uid", "kind", "legs", "movement", "name", "node", "role", "round_length_m", "route_status", "size", "source", "species_id", "speed_m_per_day", "tier", "uid", "x", "z"}},
    {"encounters", {"by_month", "by_node", "entries", "limits", "method", "occupancy", "version"}},
    {"encounters.entries[0]", {"airborne", "carries", "class", "column_length_m", "disposition", "domain", "god_id", "group_uid", "host_uid", "kind", "seeks", "size", "source", "speed_m_per_day", "threat_tier"}},
    {"encounters.occupancy[0]", {"camp_id", "camp_kind", "entry", "from_day", "months", "node", "to_day"}},
    {"heroes", {"bonds", "camps", "diagnostics", "dreads", "final_age", "limits", "mantles", "method", "orgs", "people", "policy_revision", "quest_hooks", "realms", "relics", "rolls", "status", "summary", "version"}},
    {"heroes.summary", {"camps", "candidates", "dreads", "hooks", "legends", "living", "orgs", "precipitated", "realms"}},
    {"heroes.people[role=castellan]", {"alignment", "allies", "archetype", "archetype_name", "born_age", "civilization_id", "claim", "companion", "deeds", "display_name", "epithet", "faces", "fame", "home", "kin", "log", "name", "nest_id", "node_id", "org_uid", "persona", "predecessor_uid", "presence", "race_id", "realm_uid", "rivals", "role", "school", "seat", "seed", "selectable", "site_name", "situation", "status", "tier", "uid", "well"}},
    {"heroes.people[role=reeve]", {"alignment", "allies", "archetype", "archetype_name", "born_age", "civilization_id", "claim", "companion", "deeds", "display_name", "epithet", "faces", "fame", "home", "kin", "log", "name", "nest_id", "nest_name", "node_id", "org_uid", "persona", "predecessor_uid", "presence", "race_id", "realm_uid", "rivals", "role", "school", "seat", "seed", "selectable", "site_name", "situation", "status", "tier", "uid", "well"}},
    {"heroes.realms[0]", {"capital_uid", "city_uids", "civilization_id", "civilization_name", "culture_id", "english_name", "name", "parent_race_id", "sovereign_uid", "uid"}},
    {"heroes.rolls[0]", {"age", "candidate_uid", "chance", "event_id", "kind", "precipitated", "roll"}},
    {"heroes.diagnostics[0]", {"candidates", "placed", "reason", "role", "source_kind", "sources"}},
    {"heroes.bonds[0]", {"a", "b", "from_event", "kind"}},
    {"story_web", {"heroes_revision", "limits", "method", "policy_revision", "status", "summary", "tropes", "unreachable_tropes", "unwoven", "version", "webs"}},
    {"story_web.summary", {"mean_spokes", "offered", "reachable_tropes", "tropes", "unwoven", "woven"}},
    {"story_web.webs[0]", {"acts", "alignment", "archetype", "claim", "considered", "constraints", "display_name", "examples", "facts", "offered", "rest", "role", "spokes", "threads", "uid"}},
    {"story_web.webs[0].rest", {"armed", "hook", "initiative_day"}},
    {"story_web.tropes[0]", {"angle", "cast", "id", "name", "pull"}},
    {"npcs", {"life_spans", "limits", "method", "people", "policy_revision", "post_services", "post_verbs", "registry", "sites", "status", "summary", "verbs", "version"}},
    {"npcs.summary", {"alive", "appearance_unresolved", "cast_located_by_presence", "cast_outside_a_planned_site", "cast_presence_unresolved", "dead", "duplicate_site_keys", "heroes_linked", "important", "important_capped_out", "important_fraction", "roster_mismatches", "sites", "total", "unanchored_sites", "unresolved_parent_race"}},
    {"npcs.life_spans", {"parent_races", "peoples"}},
    {"npcs.life_spans.parent_races[0]", {"adult_years", "bands", "elder_years", "id", "max_years"}},
    {"npcs.life_spans.peoples[0]", {"adult_years", "bands", "elder_years", "id", "max_years", "parent_race_id"}},
    {"npcs.life_spans.peoples[0].bands", {"middle", "old", "young"}},
    {"npcs.life_spans.peoples[0].bands.young", {"from", "to"}},
    {"npcs.registry", {"labels_from", "world_revision", "world_sha256"}},
    {"npcs.sites[0]", {"city_class", "civilization_id", "kind", "name", "node", "parent_race_id", "plan_id", "plan_status", "posts", "uid", "x", "z"}},
    {"npcs.people[plot_id^=plot-]", {"appearance", "building_id", "important", "name", "plot_id", "post", "site_kind", "site_uid", "status", "store", "uid", "workplace"}},
    {"npcs.people[plot_id^=plot-].appearance", {"age_band", "age_years", "civilization_id", "gender", "parent_race_id"}},
    {"key_locations", {"catalogue_count", "catalogue_revision", "chains", "clusters", "diagnostics", "families", "limits", "method", "sites", "status", "summary", "version"}},
    {"key_locations.summary", {"by_family", "by_occupant", "by_state", "by_tier", "dungeons", "sites"}},
    {"key_locations.sites[placement=node]", {"asset_id", "direction", "domain", "environment", "family", "height_m", "id", "interior", "kind", "layer", "links", "name", "node", "occupant", "origin", "placement", "reason", "state", "succession", "suitability", "suitability_factors", "threat", "tier", "x", "z"}},
    {"key_locations.sites[placement=node].environment", {"biome_variant", "coastal", "moisture", "natural_biome", "natural_biome_name", "slope", "temperature_c", "variant_asset_id", "variant_name"}},
    {"key_locations.sites[placement=node].origin", {"age", "ages_since", "civilization_id", "culture_id", "ruin_id", "school"}},
    {"key_locations.sites[placement=node].links", {"anchor", "chain", "chain_index", "cluster"}},
    {"key_locations.diagnostics[0]", {"archetype", "candidates", "placed", "reason", "wanted"}},
    {"key_location_plans", {"catalogue_revision", "coordinates", "kit_count", "limits", "method", "part_count", "plans", "status", "summary", "unit", "version"}},
    {"key_location_plans.plans[status=complete]", {"arrangement", "bounds_m", "family", "kind", "kit_id", "location_id", "origin", "plots", "slope_bearing_degrees", "stats", "status", "terrain", "tier", "version"}},
    {"key_location_plans.plans[status=complete].terrain", {"bounds_m", "cell_m", "relief_m", "shaping", "size", "surface"}},
    {"key_location_plans.plans[status=complete].plots[0]", {"beds", "building_id", "dimensions_m", "foundation_bottom_m", "ground_elevation_m", "id", "kind", "name", "phase", "plot_m", "road_access", "rotation_degrees", "workers", "x_m", "z_m"}},
    };
    return rows;
}

TEST(the_generated_records_carry_the_reference_key_sets) {
    std::set<std::string> keep(late_blocks().begin(), late_blocks().end());
    // Values nobody's keys are read from: rasters, cell lists and polylines.
    const std::set<std::string> hollow = {"codes", "natural_biome", "biome_variant", "heights_m", "street_paths_m",
                                          "polyline_m", "local_path_m", "access_nodes", "fishing_nodes", "segments",
                                          "legs", "by_node"};
    const Json root = StrictJson(document16(), &keep, &hollow).parse();
    for (const PinnedRecord& row : pinned_records()) {
        const Json* record = resolve(root, row.selector);
        // Every row must find its record: a row that silently found nothing would pin nothing.
        check::is_true(record != nullptr, std::string("the size-33 phase-16 document to hold ") + row.selector);
        same_keys(*record, row.keys, row.selector);
    }
}

// ===========================================================================
// Hand-built worlds: the branches no generated C++ world reaches yet
// ===========================================================================

Hero person(const std::string& role, const std::string& well, const std::string& uid) {
    Hero h;
    h.uid = uid;
    h.role = role;
    h.well = well;
    h.status = "living";
    h.civilization_id = "elf";
    h.race_id = "elf";
    h.born_age = 1;
    h.home = HeroSiteRef{"city", "surface-city-0-1-elf", "Laigond"};
    h.presence = HeroPresence{"city", "surface-city-0-1-elf", "court"};
    h.claim = HeroClaim{"hold", "surface-city-0-1-elf", "Laigond"};
    h.fame = 1.5;
    h.tier = "notable";
    h.name = "Kelevthal";
    h.epithet = "the Warden";
    h.display_name = "Kelevthal the Warden";
    h.archetype = "warden";
    h.archetype_name = "The Warden";
    h.alignment.label = "True Neutral";
    h.alignment.code = "NN";
    h.persona.archetype = "warden";
    h.faces.push_back(HeroFace{"public", h.name, role, h.alignment, h.claim, h.persona});
    h.seed = 7;
    return h;
}

// The keys every person carries, whatever their role: the reference's intersection over
// every role in its generated world. Each role test adds what its wells write.
std::set<std::string> person_keys(std::initializer_list<const char*> extra) {
    std::set<std::string> keys = {
        "alignment", "allies", "archetype", "archetype_name", "born_age", "civilization_id", "claim",
        "companion", "deeds", "display_name", "epithet", "faces", "fame", "home", "kin", "log", "name",
        "nest_id", "node_id", "org_uid", "persona", "predecessor_uid", "presence", "race_id", "realm_uid",
        "rivals", "role", "school", "seat", "seed", "selectable", "site_name", "situation", "status", "tier",
        "uid", "well"};
    for (const char* k : extra) keys.insert(k);
    return keys;
}

const Json* find_by(const Json& list, std::string_view key, std::string_view value) {
    for (const Json& item : list.items) {
        if (const Json* v = item.find(key); v != nullptr && v->text == value) return &item;
    }
    check::fail("a record with " + std::string(key) + " = " + std::string(value));
}

TEST(every_hero_branch_carries_the_reference_keys) {
    World world;
    world.has_heroes = true;
    HeroesBlock& h = world.heroes;

    Hero warlord = person("warlord", "ruins", "hero-warlord-1-surface-city-0-1-elf");
    warlord.victor_uid = "surface-city-0-1-elf";
    warlord.victor_name = "Laigond";
    warlord.defeated.push_back({"surface-city-0-9-gnome", "Sirlrak", "war-1-4", "regional"});
    HeroDeed credited;
    credited.event_kind = "war";
    credited.event_id = "war-1-4";
    credited.age = 1;
    credited.role = "credited";
    credited.kind = "regional";
    credited.defeated_uid = "surface-city-0-9-gnome";
    credited.defeated_name = "Sirlrak";
    credited.defeated_class = "capital";
    warlord.deeds.push_back(credited);
    h.people.push_back(warlord);

    Hero pretender = person("pretender", "ruins", "hero-pretender-surface-city-0-9-gnome");
    pretender.lost = HeroLost{"surface-city-0-9-gnome", "ruin-surface-city-0-9-gnome", "Sirlrak", "capital",
                              "war_regional", 1};
    h.people.push_back(pretender);

    Hero keeper = person("keeper", "shrines", "hero-keeper-shrine-ruin-x");
    keeper.god_id = "god_red_field";
    keeper.site_name = "the shrine at Sirlrak";
    h.people.push_back(keeper);

    Hero exile = person("exile", "cities", "hero-exile-surface-city-0-5-elf");
    exile.diaspora_reason = "exile";
    exile.mother_civilization_id = "gnome";
    HeroDeed founding;
    founding.event_kind = "founding";
    founding.event_id = "founding-5";
    founding.age = 1;
    founding.role = "credited";
    founding.reason = "exile";
    exile.deeds.push_back(founding);
    h.people.push_back(exile);

    Hero champion = person("champion", "guilds", "hero-champion-surface-city-0-1-elf");
    champion.nest_id = "nest-wyrm-7";
    champion.nest_name = "Mountain wyrms";
    h.people.push_back(champion);

    Hero harbourmaster = person("harbourmaster", "ports", "hero-harbourmaster-coastal-0-0");
    harbourmaster.site_name = "the quay at Laigond";
    harbourmaster.has_routes = true;
    harbourmaster.routes = 2;
    h.people.push_back(harbourmaster);

    Hero sovereign = person("sovereign", "cities", "hero-sovereign-surface-city-0-1-elf");
    HeroDeed fought;
    fought.event_kind = "war";
    fought.event_id = "war-1-2";
    fought.age = 1;
    fought.role = "victim";
    fought.kind = "civil";
    fought.opponent_uid = "surface-city-0-3-elf";
    sovereign.deeds.push_back(fought);
    h.people.push_back(sovereign);

    HeroDread dread;
    dread.uid = "dread-nest-wyrm-7";
    dread.nest_id = "nest-wyrm-7";
    dread.species = "Mountain wyrm";
    dread.status = "living";
    HeroDeed ruined;
    ruined.event_kind = "ruin";
    ruined.event_id = "ruin-surface-city-0-9-gnome";
    ruined.age = 1;
    ruined.role = "actual";
    ruined.cause = "dragon";
    ruined.city_name = "Sirlrak";
    ruined.city_class = "capital";
    dread.deeds.push_back(ruined);
    dread.destroyed.push_back({"surface-city-0-9-gnome", "Sirlrak", 1});
    h.dreads.push_back(dread);

    HeroQuestHook ley;
    ley.hook_id = "hook-a-0";
    ley.actual_effect.kind = "ley_node";
    ley.actual_effect.node_id = "ruin-x-key";
    ley.actual_effect.school = "radiant";
    ley.actual_effect.intensity_delta = 0.5;
    ley.actual_effect.has_delta = true;
    ley.target = "ruin-x-key";
    h.quest_hooks.push_back(ley);
    HeroQuestHook reclaim;
    reclaim.hook_id = "hook-a-1";
    reclaim.actual_effect.kind = "ruin";
    reclaim.actual_effect.uid = "surface-city-0-9-gnome";
    reclaim.actual_effect.action = "reclaim";
    reclaim.target = "surface-city-0-9-gnome";
    reclaim.has_target_node = true;
    reclaim.target_node = 9;
    h.quest_hooks.push_back(reclaim);

    HeroCamp camp;
    camp.uid = "camp-surface-city-0-9-gnome";
    h.camps.push_back(camp);
    HeroRelic relic;
    relic.uid = "relic-surface-city-0-9-gnome";
    h.relics.push_back(relic);
    HeroMantle mantle;
    mantle.predecessor_uid = "hero-warlord-1-surface-city-0-1-elf";
    h.mantles.push_back(mantle);
    HeroRoll unrolled;
    unrolled.candidate_uid = "hero-x";
    h.rolls.push_back(unrolled);

    const Json root = parse_all(document(world));
    const Json& heroes = root.at("heroes");
    const Json& people = heroes.at("people");
    // From the reference world's key tree at each subtype (heroes.people[<well>:<role>]).
    same_keys(*find_by(people, "role", "warlord"),
              person_keys({"defeated", "victor_fell", "victor_name", "victor_uid"}), "warlord");
    same_keys(*find_by(people, "role", "pretender"), person_keys({"lost"}), "pretender");
    same_keys(*find_by(people, "role", "keeper"), person_keys({"god_id"}), "shrine keeper");
    same_keys(*find_by(people, "role", "exile"), person_keys({"diaspora_reason", "mother_civilization_id"}),
              "exile");
    same_keys(*find_by(people, "role", "champion"), person_keys({"nest_name"}), "champion");
    same_keys(*find_by(people, "role", "harbourmaster"), person_keys({"routes"}), "harbourmaster");
    same_keys(find_by(people, "role", "warlord")->at("deeds").items.at(0),
              {"age", "defeated_class", "defeated_name", "defeated_uid", "event_id", "event_kind", "kind", "role"},
              "a warlord's war deed");
    same_keys(find_by(people, "role", "sovereign")->at("deeds").items.at(0),
              {"age", "event_id", "event_kind", "kind", "opponent_uid", "role"}, "a war deed with an opponent");
    same_keys(find_by(people, "role", "warlord")->at("defeated").items.at(0), {"kind", "name", "uid", "war_id"},
              "a warlord's defeated city");
    same_keys(find_by(people, "role", "pretender")->at("lost"),
              {"cause", "city_class", "destroyed_age", "name", "ruin_id", "uid"}, "a pretender's lost city");
    same_keys(find_by(heroes.at("quest_hooks"), "hook_id", "hook-a-0")->at("actual_effect"),
              {"intensity_delta", "kind", "node_id", "school"}, "a ley hook's effect");
    same_keys(find_by(heroes.at("quest_hooks"), "hook_id", "hook-a-1")->at("actual_effect"),
              {"action", "kind", "uid"}, "a ruin hook's effect");
    same_keys(heroes.at("camps").items.at(0),
              {"civilization_id", "direction", "name", "node", "origin", "ruin_uid", "since_age", "uid", "x", "z"},
              "a camp nobody leads");
    same_keys(heroes.at("relics").items.at(0),
              {"holder_uid", "kind", "name", "origin_event", "resting_at", "school", "uid"}, "a relic");
    same_keys(heroes.at("mantles").items.at(0), {"civilization_id", "claim", "predecessor_uid", "vacant_since_age"},
              "a mantle");
    // No Dread in the reference world either: hero_generator/wells/ruins.py _dread and
    // __init__.py _finish_dread write these, with `features` popped into `selectable`.
    same_keys(heroes.at("dreads").items.at(0),
              {"uid", "role", "well", "nest_id", "species", "family", "nest_tier", "status", "presence", "deeds",
               "destroyed", "fame", "tier", "name", "display_name", "archetype", "archetype_name", "alignment",
               "persona", "log", "situation", "seed", "selectable"},
              "a Dread");
    same_keys(heroes.at("dreads").items.at(0).at("deeds").items.at(0),
              {"age", "cause", "city_class", "city_name", "event_id", "event_kind", "role"}, "a Dread's ruin");
    // A candidate with no deeds names no event and no age -- null, not absent.
    const Json& roll = heroes.at("rolls").items.at(0);
    check::is_true(roll.at("event_id").type == Json::Type::Null && roll.at("age").type == Json::Type::Null,
                   "a deedless roll's event and age to be null");
}

NomadBand band(NomadClass classification, const std::string& uid) {
    NomadBand b;
    b.uid = uid;
    b.classification = classification;
    b.basis.owner = classification;
    b.camps.push_back(NomadCamp{uid + "-camp-0", 1, {}, NomadCampKind::Base, std::nullopt, std::nullopt});
    return b;
}

TEST(every_nomad_branch_carries_the_reference_keys) {
    World world;
    world.has_nomads = true;
    world.systems = {"nomad_effects"};
    world.has_roads = true;
    world.roads.routes.resize(2);
    NomadBlock& n = world.nomads;

    NomadBand survivors = band(NomadClass::Survivors, "nomad-1-10");
    survivors.basis.ruin_uid = "surface-city-0-9-gnome";
    survivors.basis.refuge_uid = "surface-city-0-1-elf";
    NomadBand deserters = band(NomadClass::Deserters, "nomad-1-11");
    deserters.basis.war_site_uid = "surface-city-0-9-gnome";
    NomadBand shrine_cult = band(NomadClass::Cultists, "nomad-1-12");
    shrine_cult.basis.shrine_id = "shrine-ruin-x";
    shrine_cult.basis.school = "fire";
    shrine_cult.basis.god_id = "god_fire";
    NomadBand claim_cult = band(NomadClass::Cultists, "nomad-1-13");
    claim_cult.basis.claim_id = "claim-villain-1-5";
    claim_cult.basis.villain_uid = "villain-1-5";
    claim_cult.basis.school = "infernal";
    claim_cult.basis.god_id = "god_infernal";
    NomadBand ley_cult = band(NomadClass::Cultists, "nomad-1-14");
    ley_cult.basis.ley_node_id = "fire-node-3";
    ley_cult.basis.school = "fire";
    ley_cult.basis.god_id = "god_fire";
    NomadBand child = band(NomadClass::Wanderers, "nomad-1-15");
    child.parent_uid = "nomad-1-16";
    child.basis.inherited_camp = "nomad-1-16-camp-2";
    child.round_length_m = 12000.0;
    child.route_status = NomadRouteStatus::Routed;
    n.groups = {survivors, deserters, shrine_cult, claim_cult, ley_cult, child};

    NomadRoll roll;
    roll.eligible = {NomadClass::Survivors, NomadClass::Deserters};
    roll.evidence = {survivors.basis, deserters.basis};
    roll.precipitated = true;
    roll.chose = NomadClass::Survivors;
    roll.uid = "nomad-1-10";
    NomadRoll empty;
    n.rolls = {roll, empty};

    NomadEffects& e = world.nomad_effects;
    e.assign_settlement_candidates = true;
    NomadSettlementCandidate candidate;
    candidate.id = "settlement-candidate-nomad-1-10";
    candidate.from_band = "nomad-1-10";
    e.settlement_candidates.push_back(candidate);
    e.caravan_trade.push_back(NomadCaravanTrade{1, {"nomad-1-20"}, 0.25});

    const Json root = parse_all(document(world));
    const Json& groups = root.at("nomads").at("groups");
    // The reference world's key sets at nomads.groups[<classification>].basis.
    same_keys(find_by(groups, "uid", "nomad-1-10")->at("basis"),
              {"refuge_distance_m", "refuge_uid", "ruin_distance_m", "ruin_uid"}, "a survivor band's basis");
    same_keys(find_by(groups, "uid", "nomad-1-11")->at("basis"),
              {"engagements", "ruggedness", "war_distance_m", "war_site_uid"}, "a deserter band's basis");
    same_keys(find_by(groups, "uid", "nomad-1-12")->at("basis"),
              {"god_id", "school", "shrine_distance_m", "shrine_id"}, "a shrine cult's basis");
    same_keys(find_by(groups, "uid", "nomad-1-13")->at("basis"),
              {"claim_distance_m", "claim_id", "god_id", "school", "villain_uid"}, "a claim cult's basis");
    // Not in the reference world: terrain_nomads.py _gate_cultists, the ley arm, then
    // `evidence.update(school=..., god_id=...)`.
    same_keys(find_by(groups, "uid", "nomad-1-14")->at("basis"),
              {"god_id", "ley_distance_m", "ley_intensity", "ley_node_id", "school"}, "a ley cult's basis");
    same_keys(find_by(groups, "uid", "nomad-1-15")->at("basis"),
              {"forage", "inherited_camp", "openness", "seasonal_swing"}, "a fission child's basis");
    same_keys(*find_by(groups, "uid", "nomad-1-15"),
              {"basis", "biome", "camps", "carries", "classification", "column_length_m", "direction",
               "disposition", "god_id", "legs", "node", "origin", "parent_uid", "round_length_m", "route_status",
               "school", "seeks", "size", "speed_m_per_day", "uid", "x", "z"},
              "a fission child");
    check::is_true(!find_by(groups, "uid", "nomad-1-10")->has("round_length_m"),
                   "no round length on a band the route pass did not route");

    const Json& rolls = root.at("nomads").at("rolls");
    same_keys(rolls.items.at(0).at("evidence"), {"survivors", "deserters"}, "evidence keyed by classification");
    same_keys(rolls.items.at(1), {"node", "x", "z", "eligible", "evidence", "precipitated"},
              "a roll that raised nobody has no draw, choice or uid");

    same_keys(root.at("nomads").at("effects"),
              {"version", "leyline_edits", "leyline_edits_hidden", "towns_under_raid_pressure", "roads_ridden",
               "settlement_candidates", "method", "limits"},
              "the effects summary");
    same_keys(root.at("settlement_candidates").items.at(0),
              {"direction", "from_band", "id", "migration_distance_m", "migration_source_node", "node",
               "population_estimate", "reason"},
              "a settlement candidate");
    // terrain_nomad_effects.py apply_caravan_trade: only the ridden road gains the two keys.
    const Json& routes = root.at("roads").at("routes");
    check::is_true(!routes.items.at(0).has("caravan_riders"), "an unridden road to carry no riders");
    check::is_true(routes.items.at(1).has("caravan_riders") && routes.items.at(1).has("caravan_throughput"),
                   "the ridden road to carry its riders and throughput");
}

TEST(religion_writes_sites_evidence_and_opaque_records_as_the_reference_does) {
    World world;
    world.has_religion = true;
    Religion& r = world.religion;

    GodRecord hidden;
    hidden.id = "god_unwriting";
    hidden.family = "outside";
    hidden.status = "absent";
    GodRecord sleeping;
    sleeping.id = "god_water";
    sleeping.family = "school";
    sleeping.status = "sleeping";
    sleeping.aspect = "wild";
    sleeping.evidence.last_seen = true;
    sleeping.evidence.has_network = true;
    GodRecord manifest;
    manifest.id = "god_fire";
    manifest.family = "school";
    manifest.status = "manifest";
    manifest.aspect = "sovereign";
    manifest.evidence.has_network = true;
    GodRecord plain;
    plain.id = "god_crown_law";
    plain.family = "civic";
    plain.status = "absent";
    r.gods = {hidden, sleeping, manifest, plain};

    ReligionSite cult;
    cult.id = "cult-ruin-x";
    cult.kind = "cult";
    cult.god_id = "god_weave";
    cult.ruin_id = "ruin-x";
    cult.node = 4;
    cult.weave_tide = 0.7;
    ReligionSite shrine;
    shrine.id = "shrine-ruin-y";
    shrine.kind = "shrine";
    shrine.god_id = "god_red_field";
    shrine.ruin_id = "ruin-y";
    shrine.node = 5;
    r.sites = {cult, shrine};

    // Opaque records are the JSON the visitation and corruption APIs wrote. A well-formed
    // one is republished as the object it is; a malformed one as the string it is.
    r.visitations = {R"({"god_id":"god_fire","day":12,"nodes":[1,2],"note":"a \"quoted\" line"})",
                     "{not json"};

    const Json root = parse_all(document(world));
    const Json& religion = root.at("religion");
    const Json& gods = religion.at("gods");
    check::is_true(find_by(gods, "id", "god_unwriting")->at("evidence").type == Json::Type::Null,
                   "a hidden god nothing revealed to have null evidence");
    same_keys(find_by(gods, "id", "god_water")->at("evidence"), {"last_seen"}, "a sleeping god's evidence");
    same_keys(find_by(gods, "id", "god_water")->at("evidence").at("last_seen"),
              {"mean_intensity", "instability", "nest_push", "midpoint"}, "what it was last seen with, one level");
    same_keys(find_by(gods, "id", "god_fire")->at("evidence"),
              {"mean_intensity", "instability", "nest_push", "midpoint"}, "a manifest school god's evidence");
    check::is_true(find_by(gods, "id", "god_crown_law")->at("evidence").type == Json::Type::Object &&
                       find_by(gods, "id", "god_crown_law")->at("evidence").members.empty(),
                   "a known god with nothing to measure to carry an empty object, not null");
    // The reference world's key sets at religion.sites[cult] and [shrine].
    same_keys(*find_by(religion.at("sites"), "kind", "cult"),
              {"born_under_surge", "god_id", "id", "kind", "node", "ruin_id", "weave_tide"}, "a cult site");
    same_keys(*find_by(religion.at("sites"), "kind", "shrine"), {"god_id", "id", "kind", "node", "ruin_id"},
              "a war shrine");

    const Json& visitations = religion.at("visitations");
    check::equal(visitations.items.size(), std::size_t{2}, "both records kept");
    const Json& first = visitations.items.at(0);
    check::is_true(first.type == Json::Type::Object, "a well-formed record republished as an object");
    check::equal(first.at("day").number, 12.0, "its number");
    check::equal(first.at("note").text, std::string("a \"quoted\" line"), "its escaped string");
    check::equal(first.at("nodes").items.size(), std::size_t{2}, "its array");
    check::is_true(visitations.items.at(1).type == Json::Type::String &&
                       visitations.items.at(1).text == "{not json",
                   "a malformed record kept verbatim as a string rather than dropped or emitted broken");
}

TEST(the_college_row_and_the_key_location_succession_carry_the_reference_keys) {
    World world;
    world.has_magic = true;
    // The magic writer reads every school's network by name; a hand-built world has to
    // carry all twelve for the block the colleges live in to be written at all.
    for (const MagicSchool& school : magic_school_table()) {
        LeyNetwork network;
        network.name = std::string(school.name);
        world.magic.networks[std::string(school.name)] = network;
    }
    world.has_humans = true;
    College college;
    college.id = "college-1";
    college.access_nodes = {3, 2, 1};
    world.colleges.colleges.push_back(college);
    world.has_key_locations = true;
    world.key_locations.ok = true;
    KeyLocationSite site;
    site.id = "keyloc-barrow-1";
    site.succession = KeyLocationSuccessionRecord{"cult", "undead", "Whatever was being worked on is still there."};
    world.key_locations.sites.push_back(site);
    world.key_location_plans.ok = true;

    const Json root = parse_all(document(world));
    // Not in the reference world, which placed no college: terrain_magic.py add_colleges.
    same_keys(root.at("magic").at("colleges").items.at(0),
              {"id", "kind", "node", "x", "z", "core_id", "population_profile", "culture_id", "density", "hazard",
               "suitability", "access_nodes", "access_cost", "reason"},
              "a college");
    same_keys(root.at("key_locations").at("sites").items.at(0).at("succession"),
              {"from_occupant", "note", "to_occupant"}, "a succession");
}

TEST(a_failed_package_is_a_reported_block_and_a_failed_placement_has_no_plans) {
    World world;
    world.has_heroes = true;
    world.heroes.status = "failed";
    world.heroes.error = "ValueError: no archetype";
    world.has_npcs = true;  // ok defaults to false: a roster that did not build
    world.npcs.error = "missing civilization";
    world.has_key_locations = true;  // ok defaults to false
    world.key_locations.error = "no height layer";
    const Json root = parse_all(document(world));
    same_keys(root.at("heroes"), {"version", "status", "error"}, "a failed cast");
    check::equal(root.at("heroes").at("status").text, std::string("failed"), "its status");
    same_keys(root.at("npcs"), {"version", "status", "error"}, "a failed roster");
    same_keys(root.at("key_locations"), {"version", "status", "error"}, "failed key locations");
    check::is_true(!root.has("key_location_plans"), "no plan block for locations that failed");
    check::is_true(!root.has("story_web"), "no story web the world did not compile");
}

TEST(a_non_finite_number_in_a_late_block_is_written_as_null) {
    World world;
    world.has_humans = true;
    FoodModel model;  // the unlimited transport budget is infinity by default
    model.demand = std::numeric_limits<double>::quiet_NaN();
    world.seasonal_food.models.push_back(model);
    const std::string text = document(world);
    check::is_true(text.find("1e999") == std::string::npos, "no infinity spelled as 1e999");
    const Json root = parse_all(text);  // refuses a non-finite number outright
    const Json& written = root.at("seasonal_food").at("models").items.at(0);
    check::is_true(written.at("transport_budget").type == Json::Type::Null, "an unlimited budget as null");
    check::is_true(written.at("demand").type == Json::Type::Null, "NaN as null");
}

}  // namespace
