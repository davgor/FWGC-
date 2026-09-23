// Pure selection over a finished world: the reference's terrain_read_select.
//
// Every function here reads the World it is handed and returns values. Nothing validates a
// request (read.cpp does), nothing throws a refusal, and nothing writes.
#include "fwg/read_select.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <iterator>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <type_traits>

#include "fwg/build_stages.hpp"
#include "fwg/castle_plan.hpp"
#include "fwg/city_plan.hpp"
#include "fwg/grid.hpp"
#include "fwg/magic.hpp"
#include "fwg/serialize.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

// ---------------------------------------------------------------------------
// The vocabularies.
// ---------------------------------------------------------------------------

constexpr std::string_view kNearCategories[] = {"settlements",   "hamlets",    "fortresses", "key_locations",
                                                "beast_nests",   "ley_nodes",  "encounters"};

constexpr PlaceKind kPlaceKinds[] = {PlaceKind::City, PlaceKind::Hamlet, PlaceKind::Fortress, PlaceKind::KeyLocation};

constexpr QuestState kQuestStates[] = {QuestState::Offered,   QuestState::Taken,  QuestState::Expired,
                                       QuestState::Completed, QuestState::Failed, QuestState::Abandoned};
static_assert(sizeof(kQuestStates) / sizeof(kQuestStates[0]) == std::tuple_size_v<decltype(QuestCounts::by_state)>,
              "one count per quest state");

bool ran(const World& world, std::string_view system) {
    return std::find(world.systems.begin(), world.systems.end(), system) != world.systems.end();
}

// ---------------------------------------------------------------------------
// A world document's structure, read without a parser.
//
// Only what the block accounting needs: where each value starts and ends, its members or
// elements, whether it holds an infinity, and how many bytes it costs with the insignificant
// whitespace taken out. Written for valid JSON (this library's writer produces nothing
// else); malformed text is refused with std::invalid_argument rather than read past its end.
// ---------------------------------------------------------------------------

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

[[noreturn]] void malformed(std::string_view what) {
    throw std::invalid_argument("not a JSON document: " + std::string(what));
}

std::size_t skip_space(std::string_view s, std::size_t i) {
    while (i < s.size() && is_space(s[i])) ++i;
    return i;
}

// One past the closing quote of the string token that opens at s[i].
std::size_t string_end(std::string_view s, std::size_t i) {
    for (++i; i < s.size(); ++i) {
        if (s[i] == '\\') {
            ++i;
        } else if (s[i] == '"') {
            return i + 1;
        }
    }
    malformed("an unterminated string");
}

// One past the end of the value that opens at s[i].
std::size_t value_end(std::string_view s, std::size_t i) {
    if (i >= s.size()) malformed("a value is missing");
    if (s[i] == '"') return string_end(s, i);
    if (s[i] == '{' || s[i] == '[') {
        int depth = 0;
        while (i < s.size()) {
            const char c = s[i];
            if (c == '"') {
                i = string_end(s, i);
                continue;
            }
            if (c == '{' || c == '[') {
                ++depth;
            } else if (c == '}' || c == ']') {
                if (--depth == 0) return i + 1;
            }
            ++i;
        }
        malformed("an unclosed object or array");
    }
    const std::size_t start = i;
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' && !is_space(s[i])) ++i;
    if (i == start) malformed("an empty value");
    return i;
}

void append_utf8(std::string& out, std::uint32_t code) {
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

std::uint32_t hex4(std::string_view token, std::size_t i) {
    if (i + 4 > token.size()) malformed("a short \\u escape");
    std::uint32_t code = 0;
    const auto [end, error] = std::from_chars(token.data() + i, token.data() + i + 4, code, 16);
    if (error != std::errc() || end != token.data() + i + 4) malformed("a bad \\u escape");
    return code;
}

// The text of a string token (quotes included), unescaped.
std::string decode_string(std::string_view token) {
    std::string out;
    out.reserve(token.size());
    for (std::size_t i = 1; i + 1 < token.size(); ++i) {
        const char c = token[i];
        if (c != '\\') {
            out += c;
            continue;
        }
        const char e = token[++i];
        switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                std::uint32_t code = hex4(token, i + 1);
                i += 4;
                if (code >= 0xD800 && code < 0xDC00 && i + 6 < token.size() && token[i + 1] == '\\' &&
                    token[i + 2] == 'u') {
                    const std::uint32_t low = hex4(token, i + 3);
                    if (low >= 0xDC00 && low < 0xE000) {
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        i += 6;
                    }
                }
                append_utf8(out, code);
                break;
            }
            default: malformed("an unknown escape");
        }
    }
    return out;
}

struct Member {
    std::string key;
    std::string_view value;
};

// The members of the object `text`, in written order.
std::vector<Member> members_of(std::string_view text) {
    std::vector<Member> out;
    std::size_t i = skip_space(text, 1);
    if (i < text.size() && text[i] == '}') return out;
    while (i < text.size()) {
        i = skip_space(text, i);
        if (i >= text.size() || text[i] != '"') malformed("an object key");
        const std::size_t key_end = string_end(text, i);
        std::string key = decode_string(text.substr(i, key_end - i));
        i = skip_space(text, key_end);
        if (i >= text.size() || text[i] != ':') malformed("a missing ':'");
        i = skip_space(text, i + 1);
        const std::size_t end = value_end(text, i);
        out.push_back({std::move(key), text.substr(i, end - i)});
        i = skip_space(text, end);
        if (i < text.size() && text[i] == ',') {
            ++i;
            continue;
        }
        if (i < text.size() && text[i] == '}') return out;
        malformed("an object that does not close");
    }
    malformed("an object that does not close");
}

// The elements of the array `text`, in written order.
std::vector<std::string_view> elements_of(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t i = skip_space(text, 1);
    if (i < text.size() && text[i] == ']') return out;
    while (i < text.size()) {
        i = skip_space(text, i);
        const std::size_t end = value_end(text, i);
        out.push_back(text.substr(i, end - i));
        i = skip_space(text, end);
        if (i < text.size() && text[i] == ',') {
            ++i;
            continue;
        }
        if (i < text.size() && text[i] == ']') return out;
        malformed("an array that does not close");
    }
    malformed("an array that does not close");
}

std::string_view json_type(std::string_view text) {
    switch (text.front()) {
        case '{': return "object";
        case '[': return "array";
        case '"': return "string";
        case 't':
        case 'f': return "boolean";
        case 'n': return "null";
        default: return "number";
    }
}

// What a value costs, and whether it can cross. The byte count leaves out whitespace outside
// strings, so it is the compact encoding's whatever the text's layout. An infinity is the
// one non-finite value this library's writer can emit as a number, and it spells it 1e999.
struct Cost {
    std::size_t bytes = 0;
    bool infinite = false;
};

Cost cost_of(std::string_view text) {
    Cost cost;
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (c == '"') {
            const std::size_t end = string_end(text, i);
            cost.bytes += end - i;
            i = end;
        } else if (is_space(c)) {
            ++i;
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            const std::size_t start = i;
            while (i < text.size() && text[i] != ',' && text[i] != '}' && text[i] != ']' && !is_space(text[i])) ++i;
            const std::string_view number = text.substr(start, i - start);
            if (number == "1e999" || number == "-1e999") cost.infinite = true;
            cost.bytes += i - start;
        } else {
            ++cost.bytes;
            ++i;
        }
    }
    return cost;
}

// block["version"], only when the block declares an INTEGER one: a JSON number with no
// fraction and no exponent. The LAST such key wins, as a JSON reader keeps the last of two.
std::optional<long long> declared_version(const std::vector<Member>& members) {
    std::optional<long long> out;
    for (const Member& m : members) {
        if (m.key != "version") continue;
        out.reset();
        const std::string_view v = m.value;
        const std::size_t digits = !v.empty() && v.front() == '-' ? 1 : 0;
        if (v.size() == digits) continue;
        if (!std::all_of(v.begin() + static_cast<std::ptrdiff_t>(digits), v.end(),
                         [](char c) { return c >= '0' && c <= '9'; })) {
            continue;
        }
        long long parsed = 0;
        const auto [end, error] = std::from_chars(v.data(), v.data() + v.size(), parsed);
        if (error == std::errc() && end == v.data() + v.size()) out = parsed;
    }
    return out;
}

bool registered(std::string_view name) { return is_state_key(name); }

BlockRow block_row(std::string name, std::string_view value) {
    BlockRow row;
    row.registered = registered(name);
    row.name = std::move(name);
    row.type = json_type(value);
    if (row.type == "object") {
        const std::vector<Member> members = members_of(value);
        row.count = members.size();
        row.version = declared_version(members);
    } else if (row.type == "array") {
        row.count = elements_of(value).size();
    }
    const Cost cost = cost_of(value);
    row.encodable = !cost.infinite;
    if (row.encodable) row.bytes = cost.bytes;
    return row;
}

std::string compact_document(const World& world) {
    std::ostringstream out;
    write_world_json(out, world, /*pretty=*/false, /*include_timings=*/false);
    return std::move(out).str();
}

std::string_view top_object(std::string_view document) {
    const std::size_t start = skip_space(document, 0);
    if (start >= document.size() || document[start] != '{') {
        throw std::invalid_argument("a world document is a JSON object");
    }
    return document.substr(start, value_end(document, start) - start);
}

// The member `key` of the object `text`: the last one, as a JSON reader keeps it.
std::optional<std::string_view> member(std::string_view text, std::string_view key) {
    std::optional<std::string_view> out;
    for (const Member& m : members_of(text)) {
        if (m.key == key) out = m.value;
    }
    return out;
}

// ---------------------------------------------------------------------------
// A plan row's size, measured in the document written for a world holding only that row.
//
// The row's writer depends on the row alone, so the bytes are the bytes it costs in the full
// document -- a test holds the two equal for every plan kind -- without serialising the
// whole world for one place. The probe is gated the way write_late_blocks gates each plan
// block; `setup` sets those flags and puts the one row in.
// ---------------------------------------------------------------------------

template <typename Setup>
std::optional<std::size_t> probe_row_bytes(Setup setup, std::string_view block, std::string_view rows_key,
                                           std::string_view id_field, std::string_view id) {
    World probe;
    setup(probe);
    const std::string document = compact_document(probe);
    const std::optional<std::string_view> container = member(top_object(document), block);
    const std::optional<std::string_view> rows =
        container && json_type(*container) == "object" ? member(*container, rows_key) : std::nullopt;
    if (rows && json_type(*rows) == "array") {
        for (const std::string_view row : elements_of(*rows)) {
            if (json_type(row) != "object") continue;
            const std::optional<std::string_view> key = member(row, id_field);
            if (!key || json_type(*key) != "string" || decode_string(*key) != id) continue;
            const Cost cost = cost_of(row);
            if (cost.infinite) return std::nullopt;
            return cost.bytes;
        }
    }
    throw std::logic_error("the " + std::string(block) + " row " + std::string(id) +
                           " is not in the document written to measure it");
}

// ---------------------------------------------------------------------------
// Geometry.
// ---------------------------------------------------------------------------

std::shared_ptr<const SphereGrid> grid_of(const World& world) {
    return sphere_grid(grid_size(world), globe_radius(world));
}

// Metres from the query node, or nullopt when the row is out of range or cannot be placed.
class Measure {
public:
    Measure(const World& world, int node, double radius_m)
        : grid_(grid_of(world)),
          size_(grid_size(world)),
          radius_(globe_radius(world)),
          node_(node),
          radius_m_(radius_m),
          origin_(node_cell(world, node).direction) {}

    std::optional<double> operator()(const std::optional<Vec3>& vector, std::optional<int> other) const {
        // By identity, not by arithmetic: see near_rows.
        if (other && *other == node_) return 0.0;
        Vec3 there{};
        if (vector) {
            there = *vector;
        } else {
            if (!other || *other < 0 || static_cast<std::size_t>(*other) >= grid_->node_count()) return std::nullopt;
            const auto index = static_cast<std::size_t>(*other);
            there = direction(grid_->point_x[index], grid_->point_z[index], size_);
        }
        const double distance = arc_distance_m(radius_, origin_, there);
        if (distance <= radius_m_ + 1e-9) return distance;
        return std::nullopt;
    }

private:
    std::shared_ptr<const SphereGrid> grid_;
    int size_;
    double radius_;
    int node_;
    double radius_m_;
    Vec3 origin_;
};

template <typename Row>
void by_distance(std::vector<Row>& rows) {
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.distance_m < b.distance_m; });
}

// ---------------------------------------------------------------------------
// The blocks this file reads, gated the way the world document writes them.
// ---------------------------------------------------------------------------

const std::vector<SettlementSite>* sites_of(const World& world) {
    return world.has_settlements ? &world.settlements.sites : nullptr;
}

bool heroes_ok(const World& world) { return world.has_heroes && world.heroes.status == "ok"; }
bool npcs_ok(const World& world) { return world.has_npcs && world.npcs.ok; }
bool key_locations_ok(const World& world) { return world.has_key_locations && world.key_locations.ok; }

// Every person record, in PERSON_BLOCKS order -- heroes.people, heroes.dreads, npcs.people,
// villains.people -- each list gated as the document gates it: a failed heroes or npcs
// package carries no people, and the villain block has no block-level status to fail on.
// `visit(uid, entry)` returns true to stop the walk. The one walk the index and the single
// lookup share, so the two cannot disagree about order or gates.
template <typename Visit>
void walk_people(const World& world, Visit visit) {
    const auto list = [&visit](const auto& people, LivenessBlock block) {
        for (std::size_t i = 0; i < people.size(); ++i) {
            if (visit(people[i].uid, PersonEntry{block, i})) return true;
        }
        return false;
    };
    if (heroes_ok(world)) {
        if (list(world.heroes.people, LivenessBlock::Heroes)) return;
        if (list(world.heroes.dreads, LivenessBlock::Dreads)) return;
    }
    if (npcs_ok(world) && list(world.npcs.people, LivenessBlock::Npcs)) return;
    if (world.has_villains) list(world.villains.people, LivenessBlock::Villains);
}

const NpcSite* npc_site(const World& world, const std::optional<std::string>& uid) {
    if (!uid || !npcs_ok(world)) return nullptr;
    for (const NpcSite& site : world.npcs.sites) {
        if (site.uid == *uid) return &site;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Joins, and the site in this library that writes each key.
// ---------------------------------------------------------------------------

constexpr ReadJoin kCoreJoin{"core", "settlements.sites[].id", "humans.cores[].site_id",
                             "src/humans.cpp add_humans (core.site_id = site.id)"};
constexpr ReadJoin kSeasonalJoin{"seasonal_food", "settlements.sites[].id", "seasonal_food.models[].site_id",
                                 "src/humans.cpp add_seasonal_food (model.site_id = site.id)"};
constexpr ReadJoin kCityPlanJoin{"plan", "settlements.sites[].uid", "city_plans.cities[].city_uid",
                                 "src/city_plan.cpp plan_city (plan.city_uid = site.key())"};
constexpr ReadJoin kHamletCoreJoin{"core_city", "humans.hamlets[].core_id", "settlements.sites[] index",
                                   "src/humans.cpp add_humans (site.core_id = core)"};
constexpr ReadJoin kFortressCoreJoin{"core_city", "humans.fortresses[].core_id", "settlements.sites[] index",
                                     "src/humans.cpp add_humans (site.core_id = core)"};
constexpr ReadJoin kHamletPlanJoin{"plan", "humans.hamlets[].id", "hamlet_plans.hamlets[].hamlet_id",
                                   "src/city_plan.cpp plan_hamlet (plan.hamlet_id = pin.id)"};
constexpr ReadJoin kCastlePlanJoin{"plan", "humans.fortresses[].id", "castle_plans.castles[].fortress_id",
                                   "src/castle_plan.cpp plan_castle_with_kit (plan.fortress_id = fortress.id)"};
constexpr ReadJoin kLocationPlanJoin{"location_plan", "key_locations.sites[].id",
                                     "key_location_plans.plans[].location_id",
                                     "src/key_locations.cpp plan_for (plan.location_id = site.id)"};
constexpr ReadJoin kPostsJoin{"npc_posts", "npcs.sites[].uid", "npcs.people[].site_uid",
                              "src/npcs.cpp generate_npc_roster (person.site_uid)"};

constexpr std::string_view kCityPlanWriter = "src/city_plan.cpp plan_city";
constexpr std::string_view kHamletPlanWriter = "src/city_plan.cpp plan_hamlet";
constexpr std::string_view kCastlePlanWriter = "src/castle_plan.cpp plan_castle_with_kit";
constexpr std::string_view kLocationPlanWriter = "src/key_locations.cpp plan_for";

// ---------------------------------------------------------------------------
// place
// ---------------------------------------------------------------------------

std::optional<PlanSummary> city_plan_summary(const World& world, const std::string& wanted) {
    if (!world.has_plans) return std::nullopt;
    for (const CityPlan& plan : world.city_plans.cities) {
        if (plan.city_uid != wanted) continue;
        PlanSummary out{"city_plans", "city_uid", wanted, std::string(plan.status), plan.version,
                        plan.plots.size(), plan.unplaced.size(), std::nullopt, kCityPlanWriter};
        out.bytes = probe_row_bytes(
            [&](World& probe) {
                probe.has_plans = true;
                probe.city_plans.cities.push_back(plan);
            },
            "city_plans", "cities", "city_uid", wanted);
        return out;
    }
    return std::nullopt;
}

std::optional<PlanSummary> hamlet_plan_summary(const World& world, const std::string& wanted) {
    if (!world.has_plans || !world.has_humans) return std::nullopt;
    for (const HamletPlan& plan : world.hamlet_plans.hamlets) {
        if (plan.hamlet_id != wanted) continue;
        PlanSummary out{"hamlet_plans", "hamlet_id", wanted, std::string(plan.status), plan.version,
                        plan.plots.size(), plan.unplaced.size(), std::nullopt, kHamletPlanWriter};
        out.bytes = probe_row_bytes(
            [&](World& probe) {
                probe.has_plans = true;
                probe.has_humans = true;
                probe.hamlet_plans.hamlets.push_back(plan);
            },
            "hamlet_plans", "hamlets", "hamlet_id", wanted);
        return out;
    }
    return std::nullopt;
}

std::optional<PlanSummary> castle_plan_summary(const World& world, const std::string& wanted) {
    if (!world.has_plans || !world.has_humans) return std::nullopt;
    for (const CastlePlan& plan : world.castle_plans.castles) {
        if (plan.fortress_id != wanted) continue;
        PlanSummary out{"castle_plans", "fortress_id", wanted, std::string(plan.status), plan.version,
                        plan.plots.size(), plan.unplaced.size(), std::nullopt, kCastlePlanWriter};
        out.bytes = probe_row_bytes(
            [&](World& probe) {
                probe.has_plans = true;
                probe.has_humans = true;
                probe.castle_plans.castles.push_back(plan);
            },
            "castle_plans", "castles", "fortress_id", wanted);
        return out;
    }
    return std::nullopt;
}

std::optional<PlanSummary> location_plan_summary(const World& world, const std::string& wanted) {
    // A failed key-location block writes no plan block at all, and a failed plan block
    // writes its error and no rows.
    if (!key_locations_ok(world) || !world.key_location_plans.ok) return std::nullopt;
    for (const KeyLocationSitePlan& plan : world.key_location_plans.plans) {
        if (plan.location_id != wanted) continue;
        // A key-location plan carries no unplaced list, which the reference read as zero.
        PlanSummary out{"key_location_plans", "location_id", wanted, plan.complete ? "complete" : "empty",
                        plan.version, plan.plots.size(), 0, std::nullopt, kLocationPlanWriter};
        out.bytes = probe_row_bytes(
            [&](World& probe) {
                probe.has_key_locations = true;
                probe.key_locations.ok = true;
                probe.key_location_plans.ok = true;
                probe.key_location_plans.plans.push_back(plan);
            },
            "key_location_plans", "plans", "location_id", wanted);
        return out;
    }
    return std::nullopt;
}

// The roster rows filed under one site key, in the roster's own order, projected. `total`
// is the true count however many were projected.
std::vector<NpcPostRow> npc_posts(const World& world, const std::string& site_uid, std::size_t limit,
                                  std::size_t& total) {
    std::vector<NpcPostRow> posts;
    total = 0;
    if (!npcs_ok(world)) return posts;
    for (const NpcPerson& person : world.npcs.people) {
        if (!person.site_uid || *person.site_uid != site_uid) continue;
        ++total;
        if (posts.size() >= limit) continue;
        NpcPostRow row;
        row.uid = person.uid;
        row.name = person.name;
        row.post = person.post;
        row.building_id = person.building_id;
        row.status = token(LivenessBlock::Npcs, liveness(person));
        row.important = person.important;
        if (person.is_cast) row.hero_uid = person.hero_uid;
        posts.push_back(std::move(row));
    }
    return posts;
}

// ---------------------------------------------------------------------------
// person
// ---------------------------------------------------------------------------

// Python's `first or second`: the first when it is a non-empty string, else the second.
std::optional<std::string> truthy_or(const std::optional<std::string>& first,
                                     const std::optional<std::string>& second) {
    return first && !first->empty() ? first : second;
}

// A record in its own list, by reference: the four person types a PersonEntry can name.
using PersonRef = std::variant<const Hero*, const HeroDread*, const NpcPerson*, const Villain*>;

PersonRef ref_at(const World& world, const PersonEntry& entry) {
    switch (entry.block) {
        case LivenessBlock::Heroes: return &world.heroes.people.at(entry.position);
        case LivenessBlock::Dreads: return &world.heroes.dreads.at(entry.position);
        case LivenessBlock::Npcs: return &world.npcs.people.at(entry.position);
        case LivenessBlock::Villains: break;
    }
    return &world.villains.people.at(entry.position);
}

PersonRecord copy_of(const PersonRef& ref) {
    return std::visit([](const auto* person) { return PersonRecord(*person); }, ref);
}

// Through the one predicate, which dispatches on the record's type -- its block.
Liveness liveness_of(const PersonRef& ref) {
    return std::visit([](const auto* person) { return liveness(*person); }, ref);
}

PersonAnchor anchor_of(const World& world, const PersonRef& ref) {
    return std::visit([&world](const auto* person) { return person_anchor(world, *person); }, ref);
}

// The display name, else the name; the role, else the roster post: the reference's `or`
// chains over whichever keys a block's record carries.
std::optional<std::string> giver_name(const PersonRef& ref) {
    return std::visit(
        [](const auto* person) -> std::optional<std::string> {
            using T = std::decay_t<decltype(*person)>;
            if constexpr (std::is_same_v<T, Hero> || std::is_same_v<T, HeroDread>) {
                return person->display_name.empty() ? person->name : person->display_name;
            } else if constexpr (std::is_same_v<T, NpcPerson>) {
                return person->name;
            } else {
                return std::nullopt;  // a villain carries neither
            }
        },
        ref);
}

std::optional<std::string> giver_role(const PersonRef& ref) {
    return std::visit(
        [](const auto* person) -> std::optional<std::string> {
            using T = std::decay_t<decltype(*person)>;
            if constexpr (std::is_same_v<T, Hero> || std::is_same_v<T, HeroDread>) {
                if (person->role.empty()) return std::nullopt;  // no role, and no post to fall back to
                return person->role;
            } else if constexpr (std::is_same_v<T, NpcPerson>) {
                return person->post;
            } else {
                return std::nullopt;
            }
        },
        ref);
}

// A hero's or a dread's presence, then home, against the cities standing and fallen.
PersonAnchor cast_anchor(const World& world, const std::optional<std::string>& presence,
                         const std::optional<std::string>& home) {
    const std::map<std::string, int> nodes = settlement_nodes(world);
    std::set<std::string> standing;
    if (const auto* sites = sites_of(world)) {
        for (const SettlementSite& site : *sites) {
            if (!site.uid.empty()) standing.insert(site.uid);
        }
    }
    for (const std::optional<std::string>& uid : {presence, home}) {
        if (!uid) continue;
        const auto it = nodes.find(*uid);
        if (it == nodes.end()) continue;
        PersonAnchor out;
        out.node = it->second;
        out.source = standing.count(*uid) != 0 ? std::string_view("settlements.sites[].uid")
                                               : std::string_view("ruins[].uid");
        out.uid = *uid;
        return out;
    }
    PersonAnchor out;
    out.uid = truthy_or(presence, home);
    out.unresolved_uid = out.uid;
    return out;
}

}  // namespace

// ===========================================================================
// The vocabularies
// ===========================================================================

std::span<const std::string_view> near_categories() { return std::span<const std::string_view>(kNearCategories); }

std::string_view to_string(PlaceKind kind) {
    switch (kind) {
        case PlaceKind::City: return "city";
        case PlaceKind::Hamlet: return "hamlet";
        case PlaceKind::Fortress: return "fortress";
        case PlaceKind::KeyLocation: break;
    }
    return "key_location";
}

std::span<const PlaceKind> place_kinds() { return std::span<const PlaceKind>(kPlaceKinds); }

bool id_survives_an_age(PlaceKind kind) { return kind == PlaceKind::City || kind == PlaceKind::KeyLocation; }

std::span<const QuestState> quest_states() { return std::span<const QuestState>(kQuestStates); }

// ===========================================================================
// blocks
// ===========================================================================

std::vector<BlockRow> block_rows_of(std::string_view document) {
    std::vector<BlockRow> rows;
    for (Member& m : members_of(top_object(document))) rows.push_back(block_row(std::move(m.key), m.value));
    // By name, which is the order the reference's transfer bytes are written in. Stable, so a
    // key written twice is reported twice, in written order, rather than silently merged.
    std::stable_sort(rows.begin(), rows.end(), [](const BlockRow& a, const BlockRow& b) { return a.name < b.name; });
    return rows;
}

std::vector<BlockRow> block_rows(const World& world) { return block_rows_of(compact_document(world)); }

BlockSummary block_summary_of(std::string_view document) {
    BlockSummary out;
    out.blocks = block_rows_of(document);
    std::set<std::string_view> carried;
    for (const BlockRow& row : out.blocks) {
        carried.insert(row.name);
        if (row.bytes) out.total_bytes += *row.bytes;
        if (!row.registered) out.unregistered.push_back(row.name);
        if (!row.encodable) out.unencodable.push_back(row.name);
        if (row.registered) ++out.registered_count;
    }
    for (const std::string_view key : state_keys()) {
        if (carried.count(key) == 0) out.absent.emplace_back(key);
    }
    std::sort(out.absent.begin(), out.absent.end());
    out.state_keys = state_keys().size();
    return out;
}

BlockSummary block_summary(const World& world) { return block_summary_of(compact_document(world)); }

// ===========================================================================
// Geometry
// ===========================================================================

int grid_size(const World& world) { return world.config.size; }

double globe_radius(const World& world) { return world.effective.globe_radius; }

std::size_t node_count(const World& world) { return grid_of(world)->node_count(); }

NodeCell node_cell(const World& world, int node) {
    const std::shared_ptr<const SphereGrid> grid = grid_of(world);
    if (node < 0 || static_cast<std::size_t>(node) >= grid->node_count()) {
        throw std::out_of_range("node " + std::to_string(node) + " is off this world's grid of " +
                                std::to_string(grid->node_count()) + " nodes");
    }
    const auto index = static_cast<std::size_t>(node);
    NodeCell cell;
    cell.node = node;
    cell.x = grid->point_x[index];
    cell.z = grid->point_z[index];
    cell.direction = direction(cell.x, cell.z, grid_size(world));
    return cell;
}

double arc_distance_m(double radius, const Vec3& a, const Vec3& b) { return great_circle_m(radius, a, b); }

std::vector<std::pair<std::string, double>> layer_sample(const World& world, int x, int z) {
    std::vector<std::pair<std::string, double>> out;
    for (const auto& [name, grid] : world.layers) {  // a std::map: already sorted by name
        if (x < 0 || z < 0 || x >= grid.size() || z >= grid.size()) continue;
        out.emplace_back(name, grid.at(x, z));
    }
    return out;
}

// ===========================================================================
// near
// ===========================================================================

std::size_t NearRows::total() const {
    return settlements.size() + hamlets.size() + fortresses.size() + key_locations.size() + beast_nests.size() +
           ley_nodes.size() + encounters.size();
}

NearRows near_rows(const World& world, int node, double radius_m) {
    const Measure measure(world, node, radius_m);
    NearRows rows;

    if (const auto* sites = sites_of(world)) {
        for (const SettlementSite& site : *sites) {
            const std::optional<double> d = measure(site.direction, site.node);
            if (!d) continue;
            NearSettlementRow row;
            row.uid = site.uid;
            row.id = site.id;
            row.name = site.name;
            row.node = site.node;
            row.x = site.x;
            row.z = site.z;
            row.city_class = city_class_name(site.city_class);
            row.population_profile = site.population_profile;
            row.distance_m = *d;
            rows.settlements.push_back(std::move(row));
        }
    }

    // humans.hamlets and humans.fortresses carry a node and no direction, so each is placed
    // at its node's cell.
    if (world.has_humans) {
        const auto small_sites = [&](const std::vector<RuralSite>& list, std::vector<NearSmallSiteRow>& into) {
            for (const RuralSite& record : list) {
                const std::optional<double> d = measure(std::nullopt, record.node);
                if (!d) continue;
                NearSmallSiteRow row;
                row.id = record.id;
                row.kind = record.kind;
                row.node = record.node;
                row.x = record.x;
                row.z = record.z;
                row.core_id = record.core_id;
                row.population_profile = record.population_profile;
                row.distance_m = *d;
                into.push_back(std::move(row));
            }
        };
        small_sites(world.humans.hamlets, rows.hamlets);
        small_sites(world.humans.fortresses, rows.fortresses);
    }

    if (key_locations_ok(world)) {
        for (const KeyLocationSite& site : world.key_locations.sites) {
            const std::optional<double> d = measure(site.direction, site.node);
            if (!d) continue;
            NearKeyLocationRow row;
            row.id = site.id;
            row.kind = site.kind;
            row.name = site.name;
            row.family = site.family;
            row.node = site.node;
            row.x = site.x;
            row.z = site.z;
            row.tier = site.tier;
            row.threat = site.threat;
            row.state = site.state;
            row.occupant = site.occupant;
            row.distance_m = *d;
            rows.key_locations.push_back(std::move(row));
        }
    }

    // beast_nests is written with wildlife: the two nest blocks are one stage.
    if (world.has_wildlife) {
        for (const NestSite& site : world.beast_nests.sites) {
            const std::optional<double> d = measure(site.direction, site.node);
            if (!d) continue;
            NearNestRow row;
            row.id = site.id;
            row.species_id = std::string(site.species_id);
            row.name = std::string(site.name);
            row.node = site.node;
            row.x = site.x;
            row.z = site.z;
            row.tier = site.tier;
            row.family = std::string(site.family);
            row.range_m = site.range_m;
            row.distance_m = *d;
            rows.beast_nests.push_back(std::move(row));
        }
    }

    // Schools in the school table's order -- the document's `school_order` -- and any network
    // the table does not name after them.
    if (world.has_magic) {
        std::vector<const LeyNetwork*> order;
        for (const MagicSchool& school : magic_school_table()) {
            const auto it = world.magic.networks.find(std::string(school.name));
            if (it != world.magic.networks.end()) order.push_back(&it->second);
        }
        for (const auto& [name, network] : world.magic.networks) {
            if (magic_school_index(name) < 0) order.push_back(&network);
        }
        for (const LeyNetwork* network : order) {
            for (const LeyNode& ley : network->nodes) {
                const std::optional<double> d = measure(ley.direction, std::nullopt);
                if (!d) continue;
                NearLeyNodeRow row;
                row.id = ley.id;
                row.school = network->name;
                row.group = network->group;
                row.intensity = ley.intensity;
                row.distance_m = *d;
                rows.ley_nodes.push_back(std::move(row));
            }
        }
    }

    // by_node -> occupancy -> entries, every step the writer's own. A radius widens the set
    // of nodes consulted; it never changes how a node is resolved.
    if (ran(world, "encounters")) {
        const EncounterIndex& block = world.encounters;
        std::vector<const std::pair<int, std::vector<int>>*> keyed;
        for (const auto& bucket : block.by_node) keyed.push_back(&bucket);
        std::stable_sort(keyed.begin(), keyed.end(), [](const auto* a, const auto* b) { return a->first < b->first; });
        for (const auto* bucket : keyed) {
            const std::optional<double> d = measure(std::nullopt, bucket->first);
            if (!d) continue;
            for (const int index : bucket->second) {
                if (index < 0 || static_cast<std::size_t>(index) >= block.occupancy.size()) continue;
                const EncounterOccupancy& record = block.occupancy[static_cast<std::size_t>(index)];
                const EncounterEntry* entry =
                    record.entry >= 0 && static_cast<std::size_t>(record.entry) < block.entries.size()
                        ? &block.entries[static_cast<std::size_t>(record.entry)]
                        : nullptr;
                NearEncounterRow row;
                row.occupancy = static_cast<std::size_t>(index);
                row.node = record.node;
                row.camp_id = record.camp_id;
                row.camp_kind = record.camp_kind;
                row.from_day = record.from_day;
                row.to_day = record.to_day;
                row.months = record.months;
                if (entry != nullptr) {
                    row.group_uid = entry->group_uid;
                    row.source = entry->source;
                    row.group_kind = entry->kind;
                    row.disposition = entry->disposition;
                    row.threat_tier = entry->threat_tier;
                    row.size = entry->size;
                }
                row.distance_m = *d;
                rows.encounters.push_back(std::move(row));
            }
        }
    }

    by_distance(rows.settlements);
    by_distance(rows.hamlets);
    by_distance(rows.fortresses);
    by_distance(rows.key_locations);
    by_distance(rows.beast_nests);
    by_distance(rows.ley_nodes);
    by_distance(rows.encounters);
    return rows;
}

// ===========================================================================
// place
// ===========================================================================

std::vector<PlaceId> place_ids(const World& world) {
    std::vector<PlaceId> out;
    if (const auto* sites = sites_of(world)) {
        for (const SettlementSite& site : *sites) {
            if (!site.uid.empty()) out.push_back({site.uid, PlaceKind::City});
        }
    }
    if (world.has_humans) {
        for (const RuralSite& record : world.humans.hamlets) {
            if (!record.id.empty()) out.push_back({record.id, PlaceKind::Hamlet});
        }
        for (const RuralSite& record : world.humans.fortresses) {
            if (!record.id.empty()) out.push_back({record.id, PlaceKind::Fortress});
        }
    }
    if (key_locations_ok(world)) {
        for (const KeyLocationSite& site : world.key_locations.sites) {
            if (!site.id.empty()) out.push_back({site.id, PlaceKind::KeyLocation});
        }
    }
    return out;
}

std::optional<PlaceMatch> find_place(const World& world, std::string_view place_id) {
    if (const auto* sites = sites_of(world)) {
        for (const SettlementSite& site : *sites) {
            if (site.uid == place_id) return PlaceMatch{&site, PlaceKind::City};
        }
    }
    if (world.has_humans) {
        for (const RuralSite& record : world.humans.hamlets) {
            if (record.id == place_id) return PlaceMatch{&record, PlaceKind::Hamlet};
        }
        for (const RuralSite& record : world.humans.fortresses) {
            if (record.id == place_id) return PlaceMatch{&record, PlaceKind::Fortress};
        }
    }
    if (key_locations_ok(world)) {
        for (const KeyLocationSite& site : world.key_locations.sites) {
            if (site.id == place_id) return PlaceMatch{&site, PlaceKind::KeyLocation};
        }
    }
    return std::nullopt;
}

std::optional<PlaceDocument> place_document(const World& world, std::string_view place_id, std::size_t max_posts) {
    const std::optional<PlaceMatch> found = find_place(world, place_id);
    if (!found) return std::nullopt;
    const std::string wanted(place_id);

    PlaceDocument doc;
    doc.anchor.id = wanted;
    doc.anchor.kind = found->kind;
    doc.anchor.id_survives_an_age = id_survives_an_age(found->kind);

    if (found->kind == PlaceKind::City) {
        const SettlementSite& site = *std::get<const SettlementSite*>(found->row);
        doc.place = site;
        doc.anchor.node = site.node;
        doc.anchor.x = site.x;
        doc.anchor.z = site.z;
        // The key the writer stored (core.site_id = site.id), not the core's position in
        // its list. They coincide today because cores are built in site order; matching the
        // field is what keeps that a coincidence rather than a dependency.
        if (world.has_humans) {
            for (const CityCore& core : world.humans.cores) {
                if (core.site_id == site.id) {
                    doc.resolved.core = core;
                    break;
                }
            }
            for (const FoodModel& model : world.seasonal_food.models) {
                if (model.sky_id.empty() && model.site_id == site.id) {
                    doc.resolved.seasonal_food_model = model;
                    break;
                }
            }
            for (const SeasonalCity& summary : world.seasonal_food.cities) {
                if (summary.site_id == site.id) {
                    doc.resolved.seasonal_food_summary = summary;
                    break;
                }
            }
        }
        doc.joins.push_back(kCoreJoin);
        doc.joins.push_back(kSeasonalJoin);
        doc.resolved.plan = city_plan_summary(world, wanted);
        doc.joins.push_back(kCityPlanJoin);
        doc.anchor.roster_key = wanted;
    } else if (found->kind == PlaceKind::Hamlet || found->kind == PlaceKind::Fortress) {
        const RuralSite& record = *std::get<const RuralSite*>(found->row);
        const bool hamlet = found->kind == PlaceKind::Hamlet;
        doc.place = record;
        doc.anchor.node = record.node;
        doc.anchor.x = record.x;
        doc.anchor.z = record.z;
        // core_id is a POSITION in settlements.sites, not a uid, and the site's own id
        // equals that position by invariant -- so the index is checked against it rather
        // than trusted.
        if (const auto* sites = sites_of(world);
            sites != nullptr && record.core_id >= 0 && static_cast<std::size_t>(record.core_id) < sites->size()) {
            const SettlementSite& core = (*sites)[static_cast<std::size_t>(record.core_id)];
            if (core.id == record.core_id) doc.resolved.core_city = CoreCity{core.uid, core.id, core.node, core.name};
        }
        doc.joins.push_back(hamlet ? kHamletCoreJoin : kFortressCoreJoin);
        doc.resolved.plan = hamlet ? hamlet_plan_summary(world, wanted) : castle_plan_summary(world, wanted);
        doc.joins.push_back(hamlet ? kHamletPlanJoin : kCastlePlanJoin);
        doc.anchor.roster_key = std::string(to_string(found->kind)) + "-node-" + std::to_string(record.node);
    } else {
        const KeyLocationSite& site = *std::get<const KeyLocationSite*>(found->row);
        doc.place = site;
        doc.anchor.node = site.node;
        doc.anchor.x = site.x;
        doc.anchor.z = site.z;
        // Under its own key, never under `plan`: complete | empty is a different question
        // from complete | partial | unbuildable, and `complete` is in both.
        doc.resolved.location_plan = location_plan_summary(world, wanted);
        doc.joins.push_back(kLocationPlanJoin);
        // The roster staffs cities, hamlets and fortresses only. Asking for a key here
        // would invent one.
    }

    if (doc.anchor.roster_key) {
        const std::string& key = *doc.anchor.roster_key;
        if (const NpcSite* site = npc_site(world, key)) doc.resolved.npc_site = *site;
        doc.resolved.npc_posts = npc_posts(world, key, max_posts, doc.resolved.post_count);
        doc.resolved.posts_truncated = doc.resolved.post_count > doc.resolved.npc_posts.size();
        doc.joins.push_back(kPostsJoin);
    }
    return doc;
}

// ===========================================================================
// person
// ===========================================================================

std::map<std::string, PersonEntry> person_index(const World& world) {
    std::map<std::string, PersonEntry> index;
    walk_people(world, [&index](const std::string& uid, const PersonEntry& at) {
        index.emplace(uid, at);  // the first writer of a uid wins
        return false;
    });
    return index;
}

std::map<std::string, int> settlement_nodes(const World& world) {
    std::map<std::string, int> nodes;
    if (const auto* sites = sites_of(world)) {
        for (const SettlementSite& site : *sites) {
            if (!site.uid.empty()) nodes[site.uid] = site.node;
        }
    }
    // A ruin keeps its city's uid and node. Never over a standing city.
    for (const Ruin& ruin : world.ruins) {
        if (!ruin.uid.empty()) nodes.emplace(ruin.uid, ruin.node);
    }
    return nodes;
}

PersonAnchor person_anchor(const World& world, const Hero& person) {
    return cast_anchor(world, person.presence ? std::optional<std::string>(person.presence->uid) : std::nullopt,
                       person.home ? std::optional<std::string>(person.home->uid) : std::nullopt);
}

PersonAnchor person_anchor(const World& world, const HeroDread& dread) {
    // A dread has no home.
    return cast_anchor(world, dread.presence ? std::optional<std::string>(dread.presence->uid) : std::nullopt,
                       std::nullopt);
}

PersonAnchor person_anchor(const World& world, const NpcPerson& person) {
    PersonAnchor out;
    if (person.presence_node) {
        out.node = person.presence_node;
        out.source = "npcs.people[].presence_node";
        out.uid = person.presence_uid;
        return out;
    }
    const NpcSite* site = npc_site(world, person.site_uid);
    if (site != nullptr) out.node = site->node;
    out.source = "npcs.sites[].node";
    out.uid = person.site_uid;
    if (site == nullptr && person.site_uid && !person.site_uid->empty()) out.unresolved_uid = person.site_uid;
    return out;
}

PersonAnchor person_anchor(const World&, const Villain& villain) {
    PersonAnchor out;
    out.node = villain.node;
    out.source = "villains.people[].node";
    out.uid = villain.seat_uid;
    return out;
}

PersonAnchor person_anchor(const World& world, const PersonEntry& entry) {
    return anchor_of(world, ref_at(world, entry));
}

std::optional<PersonDocument> person_document(const World& world, std::string_view uid) {
    // The index's answer for one uid without building the index: the same walk, stopped at
    // the first match -- which is exactly the writer first-writer-wins keeps.
    std::optional<PersonEntry> entry;
    walk_people(world, [&](const std::string& candidate, const PersonEntry& at) {
        if (candidate != uid) return false;
        entry = at;
        return true;
    });
    if (!entry) return std::nullopt;
    const PersonRef ref = ref_at(world, *entry);
    PersonDocument doc;
    doc.block = entry->block;
    doc.person = copy_of(ref);
    doc.liveness = liveness_of(ref);
    doc.anchor = anchor_of(world, ref);
    return doc;
}

// ===========================================================================
// quests
// ===========================================================================

std::vector<QuestRow> quest_rows(const World& world, const std::optional<std::vector<QuestState>>& states,
                                 const std::map<std::string, PersonEntry>& person_lookup) {
    std::vector<QuestRow> rows;
    if (!world.quests) return rows;
    for (const QuestRecord& quest : world.quests->quests) {
        if (states && std::find(states->begin(), states->end(), quest.state) == states->end()) continue;
        QuestRow row;
        row.quest_id = quest.quest_id;
        row.state = quest.state;
        row.giver_uid = quest.giver_uid;
        if (quest.giver_uid) {
            if (const auto it = person_lookup.find(*quest.giver_uid); it != person_lookup.end()) {
                const PersonRef ref = ref_at(world, it->second);
                QuestGiverRead giver;
                giver.uid = *quest.giver_uid;
                giver.block = it->second.block;
                giver.name = giver_name(ref);
                giver.role = giver_role(ref);
                giver.liveness = liveness_of(ref);
                giver.anchor = anchor_of(world, ref);
                row.giver = std::move(giver);
            }
        }
        row.anchor = quest.anchor;
        row.verb = quest.verb;
        row.stated_purpose = quest.stated_purpose;
        row.unwitting = quest.unwitting;
        row.offered_day = quest.offered_day;
        row.expires_day = quest.expires_day;
        row.closed_day = quest.closed_day;
        row.closed_reason = quest.closed_reason;
        rows.push_back(std::move(row));
    }
    return rows;
}

QuestCounts quest_counts(const World& world) {
    QuestCounts counts;
    if (!world.quests) return counts;
    std::size_t other = 0;
    for (const QuestRecord& quest : world.quests->quests) {
        const auto state = static_cast<std::size_t>(quest.state);
        if (state < counts.by_state.size()) {
            ++counts.by_state[state];
        } else {
            ++other;
        }
    }
    counts.total = world.quests->quests.size();
    if (other != 0) counts.unknown_state = other;
    return counts;
}

}  // namespace fwg
