#include "serialize_blocks.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include "fwg/beasts.hpp"
#include "fwg/castle_plan.hpp"
#include "fwg/city_plan.hpp"
#include "fwg/colleges.hpp"
#include "fwg/heroes.hpp"
#include "fwg/humans.hpp"
#include "fwg/key_locations.hpp"
#include "fwg/magic.hpp"
#include "fwg/nomads.hpp"
#include "fwg/npcs.hpp"
#include "fwg/religion.hpp"
#include "fwg/society.hpp"
#include "fwg/story_web.hpp"

namespace fwg::serialize_detail {
namespace {

// ===========================================================================
// Primitives
// ===========================================================================
//
// Every double goes through `number`, which writes null for NaN and infinity -- the
// convention serialize.cpp already uses for an unreachable freshwater distance. The
// JsonWriter would otherwise spell infinity 1e999, which a strict reader parses as an
// infinite float rather than refusing, and no field here means "infinitely far".
//
// Typed helpers rather than JsonWriter::field: `field("k", "text")` resolves a string
// literal to value(bool) and writes `true`. Every string here goes in as a string_view.

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

// The C++ spells the reference's None as an empty string throughout.
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

void null_field(JsonWriter& json, std::string_view key) {
    json.key(key);
    json.value_null();
}

void optional_int(JsonWriter& json, std::string_view key, const std::optional<int>& v) {
    json.key(key);
    if (v) {
        json.value(*v);
    } else {
        json.value_null();
    }
}

void optional_real(JsonWriter& json, std::string_view key, const std::optional<double>& v) {
    json.key(key);
    if (v) {
        number(json, *v);
    } else {
        json.value_null();
    }
}

void optional_text(JsonWriter& json, std::string_view key, const std::optional<std::string>& v) {
    json.key(key);
    if (v) {
        json.value(std::string_view(*v));
    } else {
        json.value_null();
    }
}

template <typename Strings>
void strings(JsonWriter& json, const Strings& items) {
    json.begin_array();
    for (const auto& item : items) json.value(std::string_view(item));
    json.end_array();
}

template <typename Ints>
void ints(JsonWriter& json, const Ints& items) {
    json.begin_array();
    for (const auto item : items) json.value(static_cast<long long>(item));
    json.end_array();
}

template <typename Reals>
void reals(JsonWriter& json, const Reals& items) {
    json.begin_array();
    for (const double item : items) number(json, item);
    json.end_array();
}

void vec3(JsonWriter& json, const Vec3& v) {
    json.begin_array();
    number(json, v.x);
    number(json, v.y);
    number(json, v.z);
    json.end_array();
}

void xz(JsonWriter& json, double x, double z) {
    json.begin_array();
    number(json, x);
    number(json, z);
    json.end_array();
}

void point(JsonWriter& json, const Point& p) { xz(json, p.x, p.z); }

void points(JsonWriter& json, const std::vector<Point>& items) {
    json.begin_array();
    for (const Point& p : items) point(json, p);
    json.end_array();
}

// A flat [j * size + i] raster, as rows of j -- the reference's `[[... for i] for j]`.
//
// Square rasters go through the Grid writer, which is how every layer is already written:
// compactly and through the shared number format, whatever the pretty flag says. A plan's
// terrain is 40,000 values a raster and one per line would triple the document for nothing.
template <typename Value>
void raster(JsonWriter& json, const std::vector<Value>& cells, int size) {
    const std::size_t n = size > 0 ? static_cast<std::size_t>(size) : 0;
    if (cells.size() == n * n) {
        Grid grid(static_cast<int>(n));
        for (std::size_t j = 0; j < n; ++j) {
            for (std::size_t i = 0; i < n; ++i) {
                grid.at(static_cast<int>(i), static_cast<int>(j)) = static_cast<double>(cells[j * n + i]);
            }
        }
        json.value(grid);
        return;
    }
    // Not square: never produced by a planner, but a malformed raster must still be valid
    // JSON rather than a crash in the writer. Rows of `size` for as long as the cells last.
    json.begin_array();
    for (std::size_t start = 0; n > 0 && start < cells.size(); start += n) {
        json.begin_array();
        for (std::size_t k = start; k < std::min(cells.size(), start + n); ++k) {
            number(json, static_cast<double>(cells[k]));
        }
        json.end_array();
    }
    json.end_array();
}

// A row-major vector<vector<double>> the same way, when it is square.
void square_rows(JsonWriter& json, const std::vector<std::vector<double>>& rows) {
    const std::size_t n = rows.size();
    bool square = true;
    for (const auto& row : rows) square = square && row.size() == n;
    if (square) {
        Grid grid(static_cast<int>(n));
        for (std::size_t j = 0; j < n; ++j) {
            for (std::size_t i = 0; i < n; ++i) grid.at(static_cast<int>(i), static_cast<int>(j)) = rows[j][i];
        }
        json.value(grid);
        return;
    }
    json.begin_array();
    for (const auto& row : rows) reals(json, row);
    json.end_array();
}

bool ran(const World& world, std::string_view system) {
    for (const std::string& s : world.systems) {
        if (s == system) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// An opaque record, re-emitted.
//
// Religion carries three things it never interprets -- an avatar, the visitations and the
// corruptions -- as the JSON text the visitation and corruption APIs wrote with this
// library's own JsonWriter. The reference publishes them as JSON objects, so they are
// parsed and walked back through the writer rather than embedded as a string. Text that is
// not one well-formed value is written as a string instead: the record is kept verbatim and
// the document stays valid, which is the whole of what can be promised about bytes this
// file did not produce.
// ---------------------------------------------------------------------------
class OpaqueJson {
public:
    explicit OpaqueJson(std::string_view text) : text_(text) {}

    void write(JsonWriter& json) {
        pos_ = 0;
        bool whole = value(nullptr);
        if (whole) {
            skip();
            whole = pos_ == text_.size();
        }
        if (whole) {
            pos_ = 0;
            value(&json);
            return;
        }
        json.value(text_);
    }

private:
    void skip() {
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\n' || text_[pos_] == '\r' || text_[pos_] == '\t')) {
            ++pos_;
        }
    }

    bool literal(std::string_view word) {
        if (text_.substr(pos_, word.size()) != word) return false;
        pos_ += word.size();
        return true;
    }

    static void append_utf8(std::string& out, std::uint32_t code) {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    bool hex4(std::uint32_t& code) {
        if (pos_ + 4 > text_.size()) return false;
        code = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = text_[pos_++];
            code <<= 4;
            if (c >= '0' && c <= '9') code |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') code |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') code |= static_cast<std::uint32_t>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool quoted(std::string& out) {
        if (pos_ >= text_.size() || text_[pos_] != '"') return false;
        ++pos_;
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return false;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos_ >= text_.size()) return false;
            const char e = text_[pos_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    std::uint32_t code = 0;
                    if (!hex4(code)) return false;
                    if (code >= 0xD800 && code < 0xDC00) {
                        std::uint32_t low = 0;
                        if (!literal("\\u") || !hex4(low) || low < 0xDC00 || low >= 0xE000) return false;
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    } else if (code >= 0xDC00 && code < 0xE000) {
                        return false;
                    }
                    append_utf8(out, code);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }

    bool number_token(JsonWriter* json) {
        const std::size_t start = pos_;
        if (pos_ < text_.size() && text_[pos_] == '-') ++pos_;
        bool digits = false;
        bool fractional = false;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c >= '0' && c <= '9') {
                digits = true;
            } else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                fractional = true;
            } else {
                break;
            }
            ++pos_;
        }
        if (!digits) return false;
        const char* first = text_.data() + start;
        const char* last = text_.data() + pos_;
        if (!fractional) {
            long long whole = 0;
            const auto parsed = std::from_chars(first, last, whole);
            if (parsed.ec == std::errc() && parsed.ptr == last) {
                if (json != nullptr) json->value(whole);
                return true;
            }
        }
        double real_value = 0.0;
        const auto parsed = std::from_chars(first, last, real_value);
        if (parsed.ec != std::errc() || parsed.ptr != last) return false;
        if (json != nullptr) number(*json, real_value);
        return true;
    }

    // `json == nullptr` is the validating pass: the same walk, writing nothing.
    bool value(JsonWriter* json) {
        skip();
        if (pos_ >= text_.size()) return false;
        const char c = text_[pos_];
        if (c == '{') {
            ++pos_;
            if (json != nullptr) json->begin_object();
            skip();
            if (pos_ < text_.size() && text_[pos_] == '}') {
                ++pos_;
                if (json != nullptr) json->end_object();
                return true;
            }
            while (true) {
                skip();
                std::string name;
                if (!quoted(name)) return false;
                skip();
                if (pos_ >= text_.size() || text_[pos_] != ':') return false;
                ++pos_;
                if (json != nullptr) json->key(name);
                if (!value(json)) return false;
                skip();
                if (pos_ < text_.size() && text_[pos_] == ',') {
                    ++pos_;
                    continue;
                }
                if (pos_ < text_.size() && text_[pos_] == '}') {
                    ++pos_;
                    if (json != nullptr) json->end_object();
                    return true;
                }
                return false;
            }
        }
        if (c == '[') {
            ++pos_;
            if (json != nullptr) json->begin_array();
            skip();
            if (pos_ < text_.size() && text_[pos_] == ']') {
                ++pos_;
                if (json != nullptr) json->end_array();
                return true;
            }
            while (true) {
                if (!value(json)) return false;
                skip();
                if (pos_ < text_.size() && text_[pos_] == ',') {
                    ++pos_;
                    continue;
                }
                if (pos_ < text_.size() && text_[pos_] == ']') {
                    ++pos_;
                    if (json != nullptr) json->end_array();
                    return true;
                }
                return false;
            }
        }
        if (c == '"') {
            std::string out;
            if (!quoted(out)) return false;
            if (json != nullptr) json->value(std::string_view(out));
            return true;
        }
        if (literal("true")) {
            if (json != nullptr) json->value(true);
            return true;
        }
        if (literal("false")) {
            if (json != nullptr) json->value(false);
            return true;
        }
        if (literal("null")) {
            if (json != nullptr) json->value_null();
            return true;
        }
        return number_token(json);
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

void opaque(JsonWriter& json, std::string_view record) { OpaqueJson(record).write(json); }

// ===========================================================================
// Stage 12: the hinterland, the seasons, the sky, the harbours and the economy
// ===========================================================================

// The fields a hamlet and a fortress share, in the reference's `record()` order.
void rural_common(JsonWriter& json, const RuralSite& s) {
    text(json, "id", s.id);
    text(json, "uid", s.uid);
    text(json, "kind", s.kind);
    integer(json, "node", s.node);
    integer(json, "x", s.x);
    integer(json, "z", s.z);
    integer(json, "core_id", s.core_id);
    text(json, "population_profile", s.population_profile);
    text(json, "culture_id", s.culture_id);
    real(json, "height_m", s.height_m);
    real(json, "access_cost", s.access_cost);
    json.key("access_nodes");
    ints(json, s.access_nodes);
    text(json, "reason", s.reason);
}

// What the player chose, carried across a rebuild. The reference copies these keys onto a
// re-seated row only when the player's row had them, so a generated row has none.
void rural_player_fields(JsonWriter& json, const RuralSite& s) {
    if (s.founded_by.empty()) return;
    text(json, "founded_by", s.founded_by);
    integer(json, "founded_age", s.founded_age);
    if (!s.name.empty()) text(json, "name", s.name);
}

// A harbour, as the fisheries block publishes it. `uid` is written only for the copy in
// humans.hamlets, where the C++ carries one; see write_humans.
void harbour_port(JsonWriter& json, const HarbourPort& p, std::string_view uid) {
    json.begin_object();
    text(json, "id", p.id);
    if (!uid.empty()) text(json, "uid", uid);
    text(json, "kind", p.kind);
    text(json, "role", p.role);
    text(json, "layer", p.layer);
    integer(json, "node", p.node);
    integer(json, "sea_node", p.sea_node);
    real(json, "landing_distance_m", p.landing_distance_m);
    integer(json, "x", p.x);
    integer(json, "z", p.z);
    integer(json, "core_id", p.core_id);
    text(json, "population_profile", p.population_profile);
    text(json, "culture_id", p.culture_id);
    real(json, "height_m", p.height_m);
    real(json, "harbor_quality", p.harbor_quality);
    flag(json, "trade_terminal", p.trade_terminal);
    json.key("access_nodes");
    ints(json, p.access_nodes);
    real(json, "access_cost", p.access_cost);
    text(json, "reason", p.reason);
    real(json, "worked_area_km2", p.worked_area_km2);
    real(json, "delivered_food", p.delivered_food);
    real(json, "delivered_materials", p.delivered_materials);
    real(json, "irrigation_benefit", p.irrigation_benefit);
    json.key("monthly_fish");
    reals(json, p.monthly_fish);
    json.key("fishing_nodes");
    ints(json, p.fishing_nodes);
    real(json, "winter_fishing_fraction", p.winter_fishing_fraction);
    json.end_object();
}

void write_humans(JsonWriter& json, const World& world) {
    const HumanHinterland& h = world.humans;
    // The reference appends each harbour dict to humans.hamlets AS WELL AS publishing it in
    // fisheries.ports -- the same object twice. The C++ keeps a thinner RuralSite copy in
    // the hamlet list, so a harbour row there is written from the port record it was made
    // from, plus the uid the C++ gives it ("hamlet-node-<n>"; the reference's harbour rows
    // carry none).
    std::map<std::string_view, const HarbourPort*> ports;
    for (const HarbourPort& p : world.society.fisheries.ports) ports[p.id] = &p;

    json.begin_object();
    integer(json, "version", h.version);
    text(json, "population_profile", h.population_profile);

    json.key("cores");
    json.begin_array();
    for (const CityCore& c : h.cores) {
        json.begin_object();
        integer(json, "site_id", c.site_id);
        text(json, "population_profile", c.population_profile);
        text(json, "culture_id", c.culture_id);
        json.key("hamlet_ids");
        strings(json, c.hamlet_ids);
        json.key("fortress_ids");
        strings(json, c.fortress_ids);
        real(json, "food_demand", c.food_demand);
        real(json, "food_supply", c.food_supply);
        real(json, "food_deficit", c.food_deficit);
        real(json, "material_supply", c.material_supply);
        real(json, "food_imports", c.food_imports);
        real(json, "food_exports", c.food_exports);
        real(json, "trade_material_cost", c.trade_material_cost);
        json.end_object();
    }
    json.end_array();

    json.key("hamlets");
    json.begin_array();
    for (const RuralSite& s : h.hamlets) {
        if (const auto it = ports.find(s.id); it != ports.end()) {
            harbour_port(json, *it->second, s.uid);
            continue;
        }
        json.begin_object();
        rural_common(json, s);
        text(json, "role", s.role);
        real(json, "irrigation_benefit", s.irrigation_benefit);
        real(json, "worked_area_km2", s.worked_area_km2);
        real(json, "delivered_food", s.delivered_food);
        real(json, "delivered_materials", s.delivered_materials);
        rural_player_fields(json, s);
        json.end_object();
    }
    json.end_array();

    json.key("fortresses");
    json.begin_array();
    for (const RuralSite& s : h.fortresses) {
        json.begin_object();
        rural_common(json, s);
        real(json, "defence_score", s.defence_score);
        integer(json, "protected_route_node", s.protected_route_node);
        rural_player_fields(json, s);
        json.end_object();
    }
    json.end_array();

    json.key("cultures");
    json.begin_array();
    for (const CultureGroup& c : h.cultures) {
        json.begin_object();
        integer(json, "index", c.index);
        text(json, "id", c.id);
        json.key("city_ids");
        ints(json, c.city_ids);
        text(json, "population_profile", c.population_profile);
        text(json, "civilization_id", c.civilization_id);
        text(json, "architecture_style_id", c.architecture_style_id);
        json.end_object();
    }
    json.end_array();

    json.key("shipments");
    json.begin_array();
    for (const FoodShipment& s : h.shipments) {
        json.begin_object();
        integer(json, "from", s.from);
        integer(json, "to", s.to);
        real(json, "sent", s.sent);
        real(json, "delivered", s.delivered);
        real(json, "lost", s.lost);
        real(json, "material_cost", s.material_cost);
        json.key("city_path");
        ints(json, s.city_path);
        json.end_object();
    }
    json.end_array();

    const FortressDemand& d = h.fortress_demand;
    json.key("fortress_demand");
    json.begin_object();
    integer(json, "proposed", d.proposed);
    integer(json, "city_ceiling", d.city_ceiling);
    integer(json, "requested_ceiling", d.requested_ceiling);
    integer(json, "limit", d.limit);
    integer(json, "player_founded", d.player_founded);
    integer(json, "veteran_demand", d.veteran_demand);
    integer(json, "veteran_cities", d.veteran_cities);
    real(json, "road_length_m", d.road_length_m);
    integer(json, "crossings", d.crossings);
    real(json, "support_reach", d.support_reach);
    json.end_object();

    text(json, "method", h.method);
    text(json, "culture_method", h.culture_method);
    text(json, "defence_method", h.defence_method);
    json.end_object();
}

// A community's year. `site_id` is the surface city's index -- or, for a sky settlement,
// its string id: the world economy's identity is mixed, and the reference writes both
// under the one key.
void food_model(JsonWriter& json, const FoodModel& m) {
    json.begin_object();
    json.key("site_id");
    if (!m.sky_id.empty()) {
        json.value(std::string_view(m.sky_id));
    } else {
        json.value(m.site_id);
    }
    json.key("harvest");
    reals(json, m.harvest);
    real(json, "demand", m.demand);
    real(json, "storage", m.storage);
    real(json, "spoilage", m.spoilage);
    // The unlimited default is infinity, which JSON cannot hold: null is "no budget".
    real(json, "transport_budget", m.transport_budget);
    json.end_object();
}

void food_route_fields(JsonWriter& json, const FoodRoute& r) {
    integer(json, "from", r.from);
    integer(json, "to", r.to);
    json.key("capacity");
    reals(json, r.capacity);
    real(json, "efficiency", r.efficiency);
    real(json, "price", r.price);
}

void food_months(JsonWriter& json, const std::vector<FoodMonth>& months) {
    json.begin_array();
    for (const FoodMonth& m : months) {
        json.begin_object();
        integer(json, "year", m.year);
        integer(json, "month", m.month);
        json.key("cities");
        json.begin_array();
        for (const FoodMonthCity& c : m.cities) {
            json.begin_object();
            real(json, "opening", c.opening);
            real(json, "harvest", c.harvest);
            real(json, "spoiled", c.spoiled);
            real(json, "imports", c.imports);
            real(json, "exports", c.exports);
            real(json, "material_spent", c.material_spent);
            real(json, "demand", c.demand);
            real(json, "consumed", c.consumed);
            real(json, "shortage", c.shortage);
            real(json, "overflow", c.overflow);
            real(json, "closing", c.closing);
            json.end_object();
        }
        json.end_array();
        json.key("shipments");
        json.begin_array();
        for (const FoodMonthShipment& s : m.shipments) {
            json.begin_object();
            integer(json, "from", s.from);
            integer(json, "to", s.to);
            integer(json, "route_index", s.route_index);
            real(json, "sent", s.sent);
            real(json, "delivered", s.delivered);
            real(json, "lost", s.lost);
            real(json, "material_cost", s.material_cost);
            json.end_object();
        }
        json.end_array();
        real(json, "opening", m.opening);
        real(json, "harvest", m.harvest);
        real(json, "closing", m.closing);
        real(json, "transit_loss", m.transit_loss);
        real(json, "consumed", m.consumed);
        real(json, "spoiled", m.spoiled);
        real(json, "overflow", m.overflow);
        json.end_object();
    }
    json.end_array();
}

void write_seasonal_food(JsonWriter& json, const SeasonalFood& s) {
    json.begin_object();
    integer(json, "version", s.version);
    integer(json, "years", s.years);
    integer(json, "reported_year", s.reported_year);
    json.key("cities");
    json.begin_array();
    for (const SeasonalCity& c : s.cities) {
        json.begin_object();
        integer(json, "site_id", c.site_id);
        real(json, "annual_demand", c.annual_demand);
        real(json, "annual_harvest", c.annual_harvest);
        real(json, "annual_shortage", c.annual_shortage);
        real(json, "food_coverage", c.food_coverage);
        json.key("lean_months");
        ints(json, c.lean_months);
        real(json, "reserve_change", c.reserve_change);
        real(json, "ending_reserve", c.ending_reserve);
        flag(json, "passes_final_year", c.passes_final_year);
        integer(json, "urban_food_equivalent", c.urban_food_equivalent);
        json.end_object();
    }
    json.end_array();
    json.key("models");
    json.begin_array();
    for (const FoodModel& m : s.models) food_model(json, m);
    json.end_array();
    json.key("routes");
    json.begin_array();
    for (const FoodRoute& r : s.routes) {
        json.begin_object();
        food_route_fields(json, r);
        json.end_object();
    }
    json.end_array();
    json.key("months");
    food_months(json, s.months);
    text(json, "method", s.method);
    text(json, "limits", s.limits);
    json.end_object();
}

void write_sky(JsonWriter& json, const SkyReport& sky) {
    json.begin_object();
    integer(json, "version", sky.version);
    json.key("islands");
    json.begin_array();
    for (const SkyIsland& i : sky.islands) {
        json.begin_object();
        text(json, "id", i.id);
        text(json, "cluster_id", i.cluster_id);
        text(json, "layer", i.layer);
        integer(json, "x", i.x);
        integer(json, "z", i.z);
        json.key("direction");
        vec3(json, i.direction);
        real(json, "radius_m", i.radius_m);
        real(json, "altitude_m", i.altitude_m);
        real(json, "area_km2", i.area_km2);
        real(json, "temperature_c", i.temperature_c);
        real(json, "moisture", i.moisture);
        text(json, "cold_habitat", i.cold_habitat);
        real(json, "food_potential", i.food_potential);
        real(json, "freshwater_capacity", i.freshwater_capacity);
        real(json, "magic_support", i.magic_support);
        json.key("mesh");
        json.begin_object();
        json.key("vertices");
        json.begin_array();
        for (const auto& v : i.mesh.vertices) reals(json, v);
        json.end_array();
        json.key("triangles");
        json.begin_array();
        for (const auto& t : i.mesh.triangles) ints(json, t);
        json.end_array();
        // A constant in the reference, not a field of the C++ mesh.
        text(json, "coordinates", "local east, radial altitude, north; metres");
        json.end_object();
        json.key("months");
        json.begin_array();
        for (const SkyMonth& m : i.months) {
            json.begin_object();
            real(json, "temperature", m.temperature);
            real(json, "snow", m.snow);
            json.end_object();
        }
        json.end_array();
        json.end_object();
    }
    json.end_array();
    json.key("settlements");
    json.begin_array();
    for (const SkySettlement& s : sky.settlements) {
        json.begin_object();
        text(json, "id", s.id);
        text(json, "layer", s.layer);
        text(json, "kind", s.kind);
        text(json, "population_profile", s.population_profile);
        text(json, "civilization_id", s.civilization_id);
        integer(json, "x", s.x);
        integer(json, "z", s.z);
        json.key("direction");
        vec3(json, s.direction);
        real(json, "altitude_m", s.altitude_m);
        integer(json, "population_estimate", s.population_estimate);
        text(json, "name", s.name);
        real(json, "freshwater_capacity", s.freshwater_capacity);
        text(json, "reason", s.reason);
        json.end_object();
    }
    json.end_array();
    real(json, "area_km2", sky.area_km2);
    flag(json, "enabled", sky.enabled);
    text_or_null(json, "disabled_reason", sky.disabled_reason);
    text(json, "method", sky.method);
    json.end_object();
}

void write_fisheries(JsonWriter& json, const Fisheries& f) {
    json.begin_object();
    json.key("ports");
    json.begin_array();
    for (const HarbourPort& p : f.ports) harbour_port(json, p, {});
    json.end_array();
    real(json, "potential_annual_food", f.potential_annual_food);
    real(json, "delivered_annual_food", f.delivered_annual_food);
    text(json, "method", f.method);
    json.end_object();
}

// A site's identity where the economy mixes the two kinds: a surface city's int index or a
// sky settlement's string id.
void mixed_site_id(JsonWriter& json, std::string_view key, int site_id, const std::string& sky_id) {
    json.key(key);
    if (!sky_id.empty()) {
        json.value(std::string_view(sky_id));
    } else {
        json.value(site_id);
    }
}

void write_transport(JsonWriter& json, const TransportReport& t) {
    json.begin_object();
    json.key("routes");
    json.begin_array();
    for (const TransportRoute& r : t.routes) {
        json.begin_object();
        food_route_fields(json, r.food);
        text(json, "mode", r.mode);
        if (r.mode == "air") {
            // Air carries a cell path and no sphere nodes.
            json.key("path");
            json.begin_array();
            for (const auto& [x, z] : r.path) {
                json.begin_array();
                json.value(x);
                json.value(z);
                json.end_array();
            }
            json.end_array();
            real(json, "length_m", r.length_m);
            real(json, "cruise_altitude_m", r.cruise_altitude_m);
        } else {
            json.key("nodes");
            ints(json, r.nodes);
            real(json, "length_m", r.length_m);
            if (r.mode == "sea") {
                real(json, "cost", r.cost);
                json.key("ports");
                json.begin_array();
                json.value(std::string_view(r.port_a));
                json.value(std::string_view(r.port_b));
                json.end_array();
            }
        }
        json.end_object();
    }
    json.end_array();
    json.key("terminals");
    ints(json, t.terminals);
    json.key("sites");
    json.begin_array();
    for (const TransportSite& s : t.sites) {
        json.begin_object();
        mixed_site_id(json, "id", s.site_id, s.sky_site_id);
        text(json, "layer", s.layer);
        json.end_object();
    }
    json.end_array();
    json.end_object();
}

void write_world_economy(JsonWriter& json, const WorldEconomy& e) {
    json.begin_object();
    integer(json, "version", e.version);
    json.key("sites");
    json.begin_array();
    for (const EconomySite& s : e.sites) {
        json.begin_object();
        mixed_site_id(json, "site_id", s.site_id, s.sky_site_id);
        text(json, "layer", s.layer);
        text(json, "name", s.name);
        real(json, "annual_demand", s.annual_demand);
        real(json, "annual_harvest", s.annual_harvest);
        real(json, "annual_shortage", s.annual_shortage);
        real(json, "food_coverage", s.food_coverage);
        json.key("lean_months");
        ints(json, s.lean_months);
        real(json, "ending_reserve", s.ending_reserve);
        json.end_object();
    }
    json.end_array();
    json.key("models");
    json.begin_array();
    for (const FoodModel& m : e.models) food_model(json, m);
    json.end_array();
    json.key("months");
    food_months(json, e.months);
    text(json, "method", e.method);
    text(json, "limits", e.limits);
    json.end_object();
}

// ===========================================================================
// Stage 13: religion
// ===========================================================================

std::string_view rule_kind_name(RuleKind kind) {
    switch (kind) {
        case RuleKind::always: return "always";
        case RuleKind::magic_enabled: return "magic_enabled";
        case RuleKind::magic_disabled: return "magic_disabled";
        case RuleKind::school: return "school";
        case RuleKind::race: return "race";
        case RuleKind::civilization_city: return "civilization_city";
        case RuleKind::ocean_share: return "ocean_share";
        case RuleKind::sky: return "sky";
        case RuleKind::dragon: return "dragon";
        case RuleKind::nest_family: return "nest_family";
        case RuleKind::ruins: return "ruins";
        case RuleKind::diaspora: return "diaspora";
        // The catalogue spells the two combinators `any` and `all`; the enum cannot.
        case RuleKind::any_of: return "any";
        case RuleKind::all_of: return "all";
        case RuleKind::primordial_count: return "primordial_count";
        case RuleKind::gods: return "gods";
        case RuleKind::races: return "races";
    }
    return "always";
}

// The flattened rule back into the nested dict the catalogue authors and the reference
// exports verbatim. Each kind carries exactly the keys its authored form carries.
void rule_node(JsonWriter& json, std::span<const RuleNode> rule, std::uint32_t index, const RulePools& pools,
               int depth) {
    json.begin_object();
    if (index >= rule.size() || depth > 64) {
        // A malformed index is a broken catalogue; the lint refuses one. Say so in the
        // document rather than reading past the rule.
        text(json, "kind", "invalid");
        json.end_object();
        return;
    }
    const RuleNode& node = rule[index];
    const auto texts = [&]() {
        json.begin_array();
        for (std::uint32_t t = node.text_begin; t < node.text_end && t < pools.texts.size(); ++t) {
            json.value(pools.texts[t]);
        }
        json.end_array();
    };
    text(json, "kind", rule_kind_name(node.kind));
    switch (node.kind) {
        case RuleKind::school:
            text(json, "school", node.school);
            break;
        case RuleKind::race:
            text(json, "race", node.race);
            break;
        case RuleKind::ocean_share:
        case RuleKind::ruins:
        case RuleKind::primordial_count:
        case RuleKind::races:
            real(json, "min", node.min_value);
            break;
        case RuleKind::civilization_city:
            json.key("ids");
            texts();
            break;
        case RuleKind::nest_family:
            json.key("families");
            texts();
            break;
        case RuleKind::gods:
            json.key("ids");
            texts();
            // Authored only to say "any": the default the reference reads is all.
            if (!node.all) flag(json, "all", false);
            break;
        case RuleKind::any_of:
        case RuleKind::all_of:
            json.key("rules");
            json.begin_array();
            for (std::uint32_t c = node.child_begin; c < node.child_end && c < pools.children.size(); ++c) {
                rule_node(json, rule, pools.children[c], pools, depth + 1);
            }
            json.end_array();
            break;
        default:
            break;
    }
    json.end_object();
}

void evidence_body(JsonWriter& json, const AspectEvidence& e) {
    if (e.has_network) {
        real(json, "mean_intensity", e.mean_intensity);
        real(json, "instability", e.instability);
        real(json, "nest_push", e.nest_push);
        real(json, "midpoint", e.midpoint);
    }
    if (e.has_lean) {
        real(json, "mean_nod_degrees", e.mean_nod_degrees);
        text(json, "leaning", e.leaning);
    }
}

void god_record(JsonWriter& json, const GodRecord& g, const RulePools& pools) {
    json.begin_object();
    text(json, "id", g.id);
    text(json, "name", g.name);
    text(json, "family", g.family);
    text(json, "status", g.status);
    text_or_null(json, "aspect", g.aspect);
    text_or_null(json, "aspect_name", g.aspect_name);
    json.key("domains");
    strings(json, g.domains);
    json.key("schools");
    strings(json, g.schools);
    json.key("rule");
    if (g.rule.empty()) {
        json.value_null();
    } else {
        rule_node(json, g.rule, 0, pools, 0);
    }
    json.key("evidence");
    const AspectEvidence& e = g.evidence;
    if (g.family == kOutsideFamily && !e.has_network && !e.has_lean && !e.last_seen) {
        // A hidden god nothing has revealed: the reference reads `was.get('evidence')` off
        // a record that never existed, which is None -- not the empty object a known god
        // with nothing to measure carries.
        json.value_null();
    } else {
        json.begin_object();
        if (e.last_seen) {
            // ONE level: the C++ collapses the reference's ever-deepening
            // {'last_seen': {'last_seen': ...}} on purpose (religion.hpp, AspectEvidence).
            json.key("last_seen");
            json.begin_object();
            evidence_body(json, e);
            json.end_object();
        } else {
            evidence_body(json, e);
        }
        json.end_object();
    }
    text_or_null(json, "saint_of", g.saint_of);
    if (g.avatar) {
        json.key("avatar");
        opaque(json, *g.avatar);
    }
    json.end_object();
}

void write_religion(JsonWriter& json, const Religion& r) {
    const RulePools pools = pantheon_rule_pools();
    const auto schools = magic_school_table();

    json.begin_object();
    integer(json, "version", r.version);
    json.key("catalogue");
    json.begin_object();
    integer(json, "schema_version", r.catalogue.schema_version);
    integer(json, "revision", r.catalogue.revision);
    text(json, "sha256", r.catalogue.sha256);
    json.end_object();

    const WorldFacts& f = r.facts;
    json.key("facts");
    json.begin_object();
    json.key("schools");
    json.begin_object();
    for (int s = 0; s < kKnownSchools; ++s) {
        flag(json, schools[static_cast<std::size_t>(s)].name, f.schools[static_cast<std::size_t>(s)]);
    }
    json.end_object();
    integer(json, "primordial_count", f.primordial_count);
    json.key("races");
    strings(json, f.races);
    json.key("civilizations_with_cities");
    strings(json, f.civilizations_with_cities);
    real(json, "ocean_share", f.ocean_share);
    integer(json, "sky", f.sky);
    json.key("families");
    strings(json, f.families);
    integer(json, "dragons", f.dragons);
    integer(json, "ruins", f.ruins);
    integer(json, "diaspora", f.diaspora);
    integer(json, "wars", f.wars);
    flag(json, "magic_enabled", f.magic_enabled);
    real(json, "lunar_influence", f.lunar_influence);
    json.end_object();

    const CosmologyChoice& c = r.cosmology;
    json.key("cosmology");
    json.begin_object();
    text(json, "id", c.id);
    text(json, "name", c.name);
    json.key("high");
    strings(json, c.high);
    text(json, "arrangement", c.arrangement);
    json.key("eligible");
    json.begin_array();
    for (const auto& [id, weight] : c.eligible) {
        json.begin_object();
        text(json, "id", id);
        real(json, "weight", weight);
        json.end_object();
    }
    json.end_array();
    json.end_object();

    json.key("gods");
    json.begin_array();
    for (const GodRecord& g : r.gods) god_record(json, g, pools);
    json.end_array();

    // A dict keyed by civilization in the reference, in its sorted civilization order --
    // which is the order the C++ stores the faiths in.
    json.key("faiths");
    json.begin_object();
    for (const Faith& faith : r.faiths) {
        json.key(faith.civilization);
        json.begin_object();
        text(json, "parent_race_id", faith.parent_race_id);
        text_or_null(json, "patron", faith.patron);
        json.key("gods");
        strings(json, faith.gods);
        json.key("names");
        json.begin_object();
        for (const auto& [id, name] : faith.names) text(json, id, name);
        json.end_object();
        json.key("conversion");
        if (faith.conversion) {
            json.begin_object();
            text(json, "god_id", faith.conversion->god_id);
            integer(json, "visitation", faith.conversion->visitation);
            json.end_object();
        } else {
            json.value_null();
        }
        json.key("fear");
        json.begin_array();
        for (const FearRecord& fear : faith.fear) {
            json.begin_object();
            text(json, "god_id", fear.god_id);
            text_or_null(json, "city_uid", fear.city_uid);
            text(json, "nest_id", fear.nest_id);
            text(json, "mode", fear.mode);
            json.end_object();
        }
        json.end_array();
        json.key("sects");
        json.begin_array();
        for (const SectRecord& sect : faith.sects) {
            json.begin_object();
            integer(json, "node", sect.node);
            integer(json, "founding_year", sect.founding_year);
            text(json, "patron", sect.patron);
            text(json, "rival", sect.rival);
            text(json, "epithet", sect.epithet);
            json.end_object();
        }
        json.end_array();
        json.key("feasts");
        json.begin_object();
        json.key("surges");
        json.begin_object();
        for (const auto& [god, days] : faith.feasts.surges) {
            json.key(god);
            ints(json, days);
        }
        json.end_object();
        json.key("full_moons");
        ints(json, faith.feasts.full_moons);
        json.key("hollow_nights");
        json.begin_object();
        json.key("days");
        ints(json, faith.feasts.hollow_nights);
        text(json, "kept_as", faith.feasts.hollow_kept_as);
        json.end_object();
        json.end_object();
        json.key("weights");
        json.begin_object();
        for (const auto& [god, weight] : faith.weights) real(json, god, weight);
        json.end_object();
        json.end_object();
    }
    json.end_object();

    json.key("sites");
    json.begin_array();
    for (const ReligionSite& s : r.sites) {
        json.begin_object();
        text(json, "id", s.id);
        text(json, "kind", s.kind);
        text(json, "god_id", s.god_id);
        if (!s.ruin_id.empty()) text(json, "ruin_id", s.ruin_id);
        integer(json, "node", s.node);
        if (s.kind == "cult") {
            // Only a cult records the tide it was born under; a war shrine records nothing.
            flag(json, "born_under_surge", s.born_under_surge);
            optional_real(json, "weave_tide", s.weave_tide);
        }
        if (s.visitation >= 0) integer(json, "visitation", s.visitation);
        json.end_object();
    }
    json.end_array();

    json.key("visitations");
    json.begin_array();
    for (const OpaqueRecord& v : r.visitations) opaque(json, v);
    json.end_array();
    json.key("corruptions");
    json.begin_array();
    for (const OpaqueRecord& v : r.corruptions) opaque(json, v);
    json.end_array();

    text(json, "method", r.method);
    text(json, "limits", r.limits);
    json.end_object();
}

// ===========================================================================
// Stage 16: city, hamlet and castle plans
// ===========================================================================
//
// WHAT A PLAN DOES NOT CARRY, deliberately (city_plan.hpp): the per-plan copies of the
// biome and magical-variant catalogues and the magic colours, the terrain-detail definition
// and the two fixed strings beside the elevation surface, the projection formula, and the
// registry half of the planner identity. Each is one catalogue or one world block every
// plan would repeat; a consumer reads it once from where it lives.

void plot_box(JsonWriter& json, std::string_view key, const PlotSize& p) {
    json.key(key);
    json.begin_object();
    real(json, "width", p.width);
    real(json, "depth", p.depth);
    json.end_object();
}

void box3(JsonWriter& json, std::string_view key, const BoxSize& b) {
    json.key(key);
    json.begin_object();
    real(json, "width", b.width);
    real(json, "depth", b.depth);
    real(json, "height", b.height);
    json.end_object();
}

void bounds(JsonWriter& json, double half) {
    json.key("bounds_m");
    json.begin_array();
    number(json, -half);
    number(json, -half);
    number(json, half);
    number(json, half);
    json.end_array();
}

void terrain_raster(JsonWriter& json, const TerrainRaster& t) {
    json.key("terrain");
    json.begin_object();
    real(json, "cell_m", t.cell_m);
    integer(json, "size", t.size);
    json.key("codes");
    raster(json, t.codes, t.size);
    json.key("surface");
    json.begin_object();
    integer(json, "size", t.surface_size);
    real(json, "step_m", t.surface_step_m);
    json.key("heights_m");
    raster(json, t.surface_heights_m, t.surface_size);
    json.end_object();
    json.key("natural_biome");
    raster(json, t.natural_biome, t.size);
    json.key("biome_variant");
    raster(json, t.biome_variant, t.size);
    json.end_object();
}

void reference_frame(JsonWriter& json, const ReferenceFrame& f) {
    json.key("reference_frame");
    json.begin_object();
    real(json, "radius_m", f.radius_m);
    real(json, "latitude_degrees", f.latitude_degrees);
    real(json, "longitude_degrees", f.longitude_degrees);
    json.end_object();
}

void plan_plot(JsonWriter& json, const Plot& p) {
    json.begin_object();
    text(json, "id", p.id);
    text(json, "building_id", p.building_id);
    text(json, "name", p.name);
    text(json, "kind", p.kind);
    text(json, "phase", p.phase);
    real(json, "x_m", p.x_m);
    real(json, "z_m", p.z_m);
    real(json, "rotation_degrees", p.rotation_degrees);
    real(json, "ground_elevation_m", p.ground_elevation_m);
    real(json, "foundation_bottom_m", p.foundation_bottom_m);
    plot_box(json, "plot_m", p.plot_m);
    box3(json, "dimensions_m", p.dimensions_m);
    json.key("road_access");
    point(json, p.road_access);
    integer(json, "workers", p.workers);
    integer(json, "beds", p.beds);
    if (p.housing_upgrade) {
        // The reference records what the plot USED to be under `previous`.
        const HousingUpgrade& u = *p.housing_upgrade;
        json.key("housing_upgrade");
        json.begin_object();
        text(json, "phase", u.phase);
        json.key("previous");
        json.begin_object();
        text(json, "building_id", u.building_id);
        text(json, "name", u.name);
        box3(json, "dimensions_m", u.dimensions_m);
        integer(json, "beds", u.beds);
        json.end_object();
        json.end_object();
    }
    json.end_object();
}

void plan_cells(JsonWriter& json, const std::vector<Cell>& cells) {
    json.begin_array();
    for (const Cell& c : cells) {
        json.begin_array();
        json.value(c.first);
        json.value(c.second);
        json.end_array();
    }
    json.end_array();
}

void plan_unplaced(JsonWriter& json, const std::vector<Unplaced>& rows) {
    json.begin_array();
    for (const Unplaced& u : rows) {
        json.begin_object();
        text(json, "building_id", u.building_id);
        integer(json, "count", u.count);
        text(json, "reason", u.reason);
        json.end_object();
    }
    json.end_array();
}

void plan_passes(JsonWriter& json, const std::vector<Pass>& passes) {
    json.begin_array();
    for (const Pass& p : passes) {
        json.begin_object();
        text(json, "id", p.id);
        integer(json, "placed", p.placed);
        json.end_object();
    }
    json.end_array();
}

void road_connections(JsonWriter& json, const std::vector<RoadConnection>& rows) {
    json.begin_array();
    for (const RoadConnection& r : rows) {
        json.begin_object();
        text(json, "route_id", r.route_id);
        integer(json, "route_index", r.route_index);
        text(json, "end", r.end);
        integer(json, "outside_index", r.outside_index);
        text(json, "junction_id", r.junction_id);
        json.key("gate_local_m");
        point(json, r.gate_local_m);
        json.key("direction");
        vec3(json, r.direction);
        text(json, "status", r.status);
        text(json, "reason", r.reason);
        if (r.has_cell) {
            json.key("cell");
            json.begin_array();
            json.value(r.cell.first);
            json.value(r.cell.second);
            json.end_array();
        }
        if (!r.local_path_m.empty()) {
            json.key("local_path_m");
            points(json, r.local_path_m);
        }
        json.end_object();
    }
    json.end_array();
}

void street_paths(JsonWriter& json, const std::vector<std::vector<Point>>& paths) {
    json.begin_array();
    for (const auto& path : paths) points(json, path);
    json.end_array();
}

void housing_passes(JsonWriter& json, const std::vector<HousingAudit>& audits) {
    json.begin_array();
    for (const HousingAudit& a : audits) {
        json.begin_object();
        text(json, "phase", a.phase);
        integer(json, "workers", a.workers);
        integer(json, "starting_beds", a.starting_beds);
        integer(json, "houses_placed", a.houses_placed);
        integer(json, "upgrades", a.upgrades);
        flag(json, "plots_exhausted", a.plots_exhausted);
        // On a hamlet too, at zero: the C++ keeps "the rural audit the same shape as a
        // city's" (city_plan.hpp, HousingAudit). The reference's hamlet audit omits it.
        integer(json, "reserved_slots", a.reserved_slots);
        integer(json, "final_beds", a.final_beds);
        integer(json, "shortfall", a.shortfall);
        json.end_object();
    }
    json.end_array();
}

// Stats as the C++ carries them: every field on every path. The reference drops keys on a
// plan's early returns; the header says zero rather than absent is intended.
void plan_stats(JsonWriter& json, const PlanStats& s, bool city) {
    json.key("stats");
    json.begin_object();
    integer(json, "workers", s.workers);
    integer(json, "worker_beds", s.worker_beds);
    integer(json, "housing_shortfall", s.housing_shortfall);
    integer(json, "service_buildings", s.service_buildings);
    integer(json, "houses", s.houses);
    integer(json, "apartments", s.apartments);
    integer(json, "unplaced_core", s.unplaced_core);
    integer(json, "unplaced_total", s.unplaced_total);
    // A city always states its site's two figures, null where the site has none. A hamlet
    // has no demographic figures in the reference and none unless the C++ set one.
    if (city || s.population_estimate) {
        json.key("population_estimate");
        if (s.population_estimate) json.value(*s.population_estimate); else json.value_null();
    }
    if (city || s.urban_population_estimate) {
        json.key("urban_population_estimate");
        if (s.urban_population_estimate) json.value(*s.urban_population_estimate); else json.value_null();
    }
    real(json, "reserved_plot_area_m2", s.reserved_plot_area_m2);
    integer(json, "wall_segments", s.wall_segments);
    flag(json, "defended_perimeter", s.defended_perimeter);
    json.end_object();
}

void plan_debug(JsonWriter& json, const PlanDebug& d, bool city) {
    json.key("debug");
    json.begin_object();
    integer(json, "terrain_safe_cells", static_cast<long long>(d.terrain_safe_cells));
    integer(json, "total_cells", static_cast<long long>(d.total_cells));
    json.key("housing_passes");
    housing_passes(json, d.housing_passes);
    text(json, "apartment_policy", d.apartment_policy);
    if (city) {
        json.key("housing_reservations");
        json.begin_array();
        for (const HousingReservation& r : d.housing_reservations) {
            json.begin_object();
            text(json, "phase", r.phase);
            integer(json, "workers_budgeted", r.workers_budgeted);
            integer(json, "slots", r.slots);
            json.end_object();
        }
        json.end_array();
    }
    integer(json, "shape_safe_cells", static_cast<long long>(d.shape_safe_cells));
    integer(json, "road_cells", static_cast<long long>(d.road_cells));
    integer(json, "occupied_cells", static_cast<long long>(d.occupied_cells));
    integer(json, "access_cells", static_cast<long long>(d.access_cells));
    integer(json, "vacant_shape_cells", static_cast<long long>(d.vacant_shape_cells));
    integer(json, "housing_frontage_candidates", static_cast<long long>(d.housing_frontage_candidates));
    if (city) {
        integer(json, "fortification_rings", static_cast<long long>(d.fortification_rings));
        real(json, "program_half_m", d.program_half_m);
    }
    json.end_object();
}

void shape_selection(JsonWriter& json, const ShapeSelection& s) {
    json.key("shape");
    json.begin_object();
    integer(json, "version", s.version);
    integer(json, "catalogue_revision", s.catalogue_revision);
    text(json, "catalogue_sha256", s.catalogue_sha256);
    text_or_null(json, "shape_id", s.shape_id);
    json.key("parameters");
    json.begin_object();
    for (const ShapeParameter& p : s.parameters) real(json, p.key, p.value);
    json.end_object();
    json.key("candidates");
    json.begin_array();
    for (const ShapeCandidate& c : s.candidates) {
        json.begin_object();
        text(json, "id", c.id);
        text(json, "family", c.family);
        real(json, "weight", c.weight);
        json.key("matched_preferences");
        json.begin_array();
        for (const ShapeFeature f : c.matched_preferences) json.value(to_string(f));
        json.end_array();
        json.end_object();
    }
    json.end_array();
    flag(json, "runtime_geometry_generated", s.runtime_geometry_generated);
    // Present when a shape was chosen -- and on an authored town, which keeps the choice it
    // WOULD have made. Absent when nothing was eligible, as in the reference.
    if (!s.shape_id.empty() || !s.layout.empty() || !s.source_ids.empty()) {
        json.key("layout");
        json.begin_object();
        for (const ShapeLayoutNote& note : s.layout) text(json, note.key, note.text);
        json.end_object();
        json.key("source_ids");
        strings(json, s.source_ids);
    }
    text(json, "reason", s.reason);
    json.end_object();
}

void fortifications(JsonWriter& json, const Fortifications& f) {
    json.begin_object();
    integer(json, "version", f.version);
    flag(json, "defended_perimeter", f.defended_perimeter);
    json.key("rings");
    json.begin_array();
    for (const WallRing& r : f.rings) {
        json.begin_object();
        text(json, "id", r.id);
        text(json, "role", r.role);
        real(json, "scale", r.scale);
        json.key("polyline_m");
        points(json, r.polyline_m);
        text(json, "status", r.status);
        integer(json, "segment_count", r.segment_count);
        integer(json, "gate_count", r.gate_count);
        json.end_object();
    }
    json.end_array();
    json.key("segments");
    json.begin_array();
    for (const WallSegment& s : f.segments) {
        json.begin_object();
        text(json, "id", s.id);
        text(json, "ring_id", s.ring_id);
        text(json, "structure_id", s.structure_id);
        json.key("from_m");
        point(json, s.from_m);
        json.key("to_m");
        point(json, s.to_m);
        json.key("center_m");
        point(json, s.center_m);
        real(json, "length_m", s.length_m);
        real(json, "rotation_degrees", s.rotation_degrees);
        json.end_object();
    }
    json.end_array();
    json.key("nodes");
    json.begin_array();
    for (const WallNode& n : f.nodes) {
        json.begin_object();
        text(json, "id", n.id);
        text(json, "kind", n.kind);
        json.key("position_m");
        point(json, n.position_m);
        text(json, "ring_id", n.ring_id);
        json.end_object();
    }
    json.end_array();
    json.key("gates");
    json.begin_array();
    for (const WallGate& g : f.gates) {
        json.begin_object();
        text(json, "id", g.id);
        text(json, "ring_id", g.ring_id);
        text(json, "structure_id", g.structure_id);
        json.key("position_m");
        point(json, g.position_m);
        json.key("approach_m");
        point(json, g.approach_m);
        optional_int(json, "route_index", g.route_index);
        if (!g.plot_id.empty()) text(json, "plot_id", g.plot_id);
        json.end_object();
    }
    json.end_array();
    json.key("towers");
    json.begin_array();
    for (const WallTower& t : f.towers) {
        json.begin_object();
        text(json, "id", t.id);
        text(json, "ring_id", t.ring_id);
        text(json, "structure_id", t.structure_id);
        json.key("position_m");
        point(json, t.position_m);
        if (!t.plot_id.empty()) text(json, "plot_id", t.plot_id);
        json.end_object();
    }
    json.end_array();
    json.key("unplaced");
    json.begin_array();
    for (const UnplacedWall& u : f.unplaced) {
        json.begin_object();
        text(json, "building_id", u.building_id);
        // Empty when the whole enceinte failed rather than one ring; the reference's
        // whole-enceinte row has no ring_id key at all.
        if (!u.ring_id.empty()) text(json, "ring_id", u.ring_id);
        text(json, "reason", u.reason);
        json.end_object();
    }
    json.end_array();
    text(json, "method", f.method);
    text(json, "limits", f.limits);
    json.end_object();
}

void city_plan(JsonWriter& json, const CityPlan& p) {
    json.begin_object();
    integer(json, "version", p.version);
    text(json, "city_uid", p.city_uid);
    integer(json, "site_id", p.site_id);
    text(json, "name", p.name);
    text(json, "civilization_id", p.civilization_id);
    text(json, "city_class", city_class_name(p.city_class));
    text(json, "unit", "metres");
    bounds(json, p.half_m);
    terrain_raster(json, p.terrain);
    reference_frame(json, p.reference_frame);
    json.key("location");
    json.begin_object();
    real(json, "buildable_area_m2", p.location.buildable_area_m2);
    real(json, "usable_land_fraction", p.location.usable_land_fraction);
    real(json, "local_slope_degrees", p.location.local_slope_degrees);
    real(json, "aridity", p.location.aridity);
    real(json, "wooded_fraction", p.location.wooded_fraction);
    real(json, "regional_threat", p.location.regional_threat);
    real(json, "defense_priority", p.location.defense_priority);
    json.end_object();
    shape_selection(json, p.shape);
    json.key("plots");
    json.begin_array();
    for (const Plot& plot : p.plots) plan_plot(json, plot);
    json.end_array();
    json.key("roads");
    plan_cells(json, p.roads);
    json.key("unplaced");
    plan_unplaced(json, p.unplaced);
    json.key("fortifications");
    if (p.fortifications) {
        fortifications(json, *p.fortifications);
    } else {
        json.value_null();
    }
    json.key("passes");
    plan_passes(json, p.passes);
    json.key("warnings");
    strings(json, p.warnings);
    real(json, "source_resolution_m", p.source_resolution_m);
    text(json, "status", p.status);
    plan_stats(json, p.stats, true);
    plan_debug(json, p.debug, true);
    json.key("road_connections");
    road_connections(json, p.road_connections);
    json.key("street_paths_m");
    street_paths(json, p.street_paths_m);
    if (p.authored_by_player) text(json, "authored_by", "player");
    json.end_object();
}

void write_city_plans(JsonWriter& json, const CityPlans& plans) {
    json.begin_object();
    integer(json, "version", plans.version);
    json.key("identity");
    json.begin_object();
    integer(json, "version", plans.version);
    text(json, "shapes_sha256", plans.shapes_sha256);
    json.end_object();
    json.key("cities");
    json.begin_array();
    for (const CityPlan& p : plans.cities) city_plan(json, p);
    json.end_array();
    json.key("phase_order");
    strings(json, plans.phase_order);
    text(json, "scope", plans.scope);
    json.end_object();
}

void hamlet_plan(JsonWriter& json, const HamletPlan& p) {
    json.begin_object();
    integer(json, "version", p.version);
    text(json, "hamlet_id", p.hamlet_id);
    // The stable key beside the ordinal (city_plan.hpp, HamletPin); the reference's plan
    // joins on the ordinal alone.
    text(json, "hamlet_uid", p.hamlet_uid);
    text(json, "kind", p.kind);
    text_or_null(json, "role", p.role);
    text(json, "core_city_uid", p.core_city_uid);
    integer(json, "core_id", p.core_id);
    real(json, "x", p.x);
    real(json, "z", p.z);
    integer(json, "node", p.node);
    text(json, "civilization_id", p.civilization_id);
    text(json, "unit", "metres");
    bounds(json, p.half_m);
    terrain_raster(json, p.terrain);
    reference_frame(json, p.reference_frame);
    json.key("shape");
    json.begin_object();
    text(json, "shape_id", p.shape_id);
    text(json, "family", p.shape_family);
    json.key("parameters");
    json.begin_object();
    real(json, "block_spacing_m", p.shape_block_spacing_m);
    real(json, "aspect_ratio", p.shape_aspect_ratio);
    json.end_object();
    json.end_object();
    json.key("plots");
    json.begin_array();
    for (const Plot& plot : p.plots) plan_plot(json, plot);
    json.end_array();
    json.key("roads");
    plan_cells(json, p.roads);
    json.key("unplaced");
    plan_unplaced(json, p.unplaced);
    json.key("passes");
    plan_passes(json, p.passes);
    json.key("warnings");
    strings(json, p.warnings);
    real(json, "source_resolution_m", p.source_resolution_m);
    text(json, "status", p.status);
    plan_stats(json, p.stats, false);
    plan_debug(json, p.debug, false);
    json.key("road_connections");
    road_connections(json, p.road_connections);
    json.key("street_paths_m");
    street_paths(json, p.street_paths_m);
    json.end_object();
}

void write_hamlet_plans(JsonWriter& json, const HamletPlans& plans) {
    json.begin_object();
    integer(json, "version", plans.version);
    json.key("identity");
    json.begin_object();
    integer(json, "version", plans.version);
    json.end_object();
    json.key("hamlets");
    json.begin_array();
    for (const HamletPlan& p : plans.hamlets) hamlet_plan(json, p);
    json.end_array();
    json.key("phase_order");
    strings(json, plans.phase_order);
    text(json, "scope", plans.scope);
    json.end_object();
}

void castle_join(JsonWriter& json, const CastleJoin& j) {
    json.begin_object();
    text(json, "id", j.id);
    text(json, "ring_id", j.ring_id);
    text(json, "module_key", j.module_key);
    text(json, "module_id", j.module_id);
    text(json, "structure_id", j.structure_id);
    json.key("position_m");
    point(json, j.position_m);
    if (j.approach_m) {
        json.key("approach_m");
        point(json, *j.approach_m);
    }
    real(json, "thickness_m", j.thickness_m);
    real(json, "height_m", j.height_m);
    real(json, "curtain_height_m", j.curtain_height_m);
    flag(json, "walkway", j.walkway);
    // The reference tags the postern alone; a gate, tower or stair has no `kind` key, the
    // list it sits in says what it is.
    if (j.kind == "postern") text(json, "kind", j.kind);
    json.end_object();
}

void castle_network(JsonWriter& json, const CastleWallNetwork& n) {
    json.begin_object();
    text(json, "id", n.id);
    text(json, "role", n.role);
    json.key("polyline_m");
    points(json, n.polyline_m);
    text(json, "status", n.status);
    json.key("segments");
    json.begin_array();
    for (const CastleSegment& s : n.segments) {
        json.begin_object();
        text(json, "id", s.id);
        text(json, "ring_id", s.ring_id);
        text(json, "structure_id", s.structure_id);
        text(json, "module_id", s.module_id);
        json.key("from_m");
        point(json, s.from_m);
        json.key("to_m");
        point(json, s.to_m);
        json.key("center_m");
        point(json, s.center_m);
        real(json, "length_m", s.length_m);
        real(json, "rotation_degrees", s.rotation_degrees);
        real(json, "thickness_m", s.thickness_m);
        real(json, "height_m", s.height_m);
        flag(json, "walkway", s.walkway);
        json.end_object();
    }
    json.end_array();
    json.key("nodes");
    json.begin_array();
    for (const CastleWallNode& v : n.nodes) {
        json.begin_object();
        text(json, "id", v.id);
        text(json, "kind", v.kind);
        json.key("position_m");
        point(json, v.position_m);
        text(json, "ring_id", v.ring_id);
        json.end_object();
    }
    json.end_array();
    json.key("gates");
    json.begin_array();
    for (const CastleJoin& j : n.gates) castle_join(json, j);
    json.end_array();
    json.key("towers");
    json.begin_array();
    for (const CastleJoin& j : n.towers) castle_join(json, j);
    json.end_array();
    json.key("stairs");
    json.begin_array();
    for (const CastleJoin& j : n.stairs) castle_join(json, j);
    json.end_array();
    flag(json, "walkway_continuous", n.walkway_continuous);
    json.key("unplaced");
    json.begin_array();
    for (const CastleUnplaced& u : n.unplaced) {
        // A ring's own failures carry no count in the reference; the plan-level rows do.
        json.begin_object();
        text(json, "building_id", u.building_id);
        text(json, "reason", u.reason);
        json.end_object();
    }
    json.end_array();
    if (n.status == "closed") {
        // "Meaningful on a closed ring only", and only a closed ring carries them there.
        integer(json, "segment_count", n.segment_count);
        integer(json, "gate_count", n.gate_count);
        flag(json, "ditch", n.ditch);
    }
    // Not in the reference: whether the ring was clipped to buildable land or fell back to
    // the raw ellipse, which nothing re-checks against the ground (castle_plan.hpp).
    flag(json, "clipped_to_terrain", n.clipped_to_terrain);
    json.end_object();
}

void castle_plan(JsonWriter& json, const CastlePlan& p) {
    json.begin_object();
    integer(json, "version", p.version);
    text(json, "kind", p.kind);
    text(json, "fortress_id", p.fortress_id);
    // The stable key and its node, beside the ordinal (castle_plan.hpp, "WHAT DIFFERS").
    text(json, "fortress_uid", p.fortress_uid);
    integer(json, "node", p.node);
    json.key("core_id");
    if (p.core_id >= 0) json.value(p.core_id); else json.value_null();
    integer(json, "x", p.x);
    integer(json, "z", p.z);
    text(json, "kit_id", p.kit_id);
    text(json, "kit_name", p.kit_name);
    bounds(json, p.half_m);
    json.key("passes");
    plan_passes(json, p.passes);
    json.key("plots");
    json.begin_array();
    for (const CastlePlot& plot : p.plots) {
        json.begin_object();
        text(json, "id", plot.id);
        text(json, "building_id", plot.building_id);
        text(json, "name", plot.name);
        text(json, "kind", plot.kind);
        text(json, "phase", plot.phase);
        real(json, "x_m", plot.x_m);
        real(json, "z_m", plot.z_m);
        real(json, "rotation_degrees", plot.rotation_degrees);
        real(json, "ground_elevation_m", plot.ground_elevation_m);
        real(json, "foundation_bottom_m", plot.foundation_bottom_m);
        plot_box(json, "plot_m", plot.plot_m);
        box3(json, "dimensions_m", plot.dimensions_m);
        json.key("road_access");
        point(json, plot.road_access);
        integer(json, "workers", plot.workers);
        json.end_object();
    }
    json.end_array();
    json.key("wall_networks");
    json.begin_array();
    for (const CastleWallNetwork& n : p.wall_networks) castle_network(json, n);
    json.end_array();
    json.key("unplaced");
    json.begin_array();
    for (const CastleUnplaced& u : p.unplaced) {
        json.begin_object();
        text(json, "building_id", u.building_id);
        integer(json, "count", u.count);
        text(json, "reason", u.reason);
        json.end_object();
    }
    json.end_array();
    json.key("road_connections");
    road_connections(json, p.road_connections);
    json.key("warnings");
    strings(json, p.warnings);
    // "A truthful summary on every path" (castle_plan.hpp): all seven, always.
    json.key("stats");
    json.begin_object();
    integer(json, "wall_rings_closed", p.stats.wall_rings_closed);
    integer(json, "segments", p.stats.segments);
    integer(json, "plots", p.stats.plots);
    integer(json, "workers", p.stats.workers);
    text(json, "kit_id", p.stats.kit_id);
    flag(json, "walkway_continuous", p.stats.walkway_continuous);
    integer(json, "unplaced_total", p.stats.unplaced_total);
    json.end_object();
    json.key("debug");
    json.begin_object();
    integer(json, "valid_cells", static_cast<long long>(p.debug.valid_cells));
    json.key("approach_m");
    if (p.debug.approach_m) point(json, *p.debug.approach_m); else json.value_null();
    integer(json, "geometry_version", p.debug.geometry_version);
    json.end_object();
    text(json, "status", p.status);
    real(json, "source_resolution_m", p.source_resolution_m);
    terrain_raster(json, p.terrain);
    json.end_object();
}

void write_castle_plans(JsonWriter& json, const CastlePlans& plans) {
    json.begin_object();
    integer(json, "version", plans.version);
    json.key("identity");
    json.begin_object();
    integer(json, "version", plans.identity.version);
    integer(json, "geometry_version", plans.identity.geometry_version);
    text(json, "castles_sha256", plans.identity.castles_sha256);
    text(json, "castle_buildings_sha256", plans.identity.castle_buildings_sha256);
    json.end_object();
    json.key("castles");
    json.begin_array();
    for (const CastlePlan& p : plans.castles) castle_plan(json, p);
    json.end_array();
    json.key("phase_order");
    strings(json, plans.phase_order);
    text(json, "limits", plans.limits);
    text(json, "beds", plans.beds);
    json.end_object();
}

// ===========================================================================
// Stage 16: the bands, the herds and the encounter index
// ===========================================================================

// A band's evidence, keyed as its classification's gate keys it. One arm only: a cult
// carries exactly one of its three.
void nomad_basis(JsonWriter& json, const NomadBasis& b) {
    json.begin_object();
    switch (b.owner) {
        case NomadClass::Survivors:
            text(json, "ruin_uid", b.ruin_uid);
            real(json, "ruin_distance_m", b.ruin_distance_m);
            text(json, "refuge_uid", b.refuge_uid);
            real(json, "refuge_distance_m", b.refuge_distance_m);
            break;
        case NomadClass::Deserters:
            text(json, "war_site_uid", b.war_site_uid);
            real(json, "war_distance_m", b.war_distance_m);
            real(json, "ruggedness", b.ruggedness);
            integer(json, "engagements", b.engagements);
            break;
        case NomadClass::Cultists:
            if (!b.ley_node_id.empty()) {
                text(json, "ley_node_id", b.ley_node_id);
                real(json, "ley_distance_m", b.ley_distance_m);
                real(json, "ley_intensity", b.ley_intensity);
            } else if (!b.shrine_id.empty()) {
                text(json, "shrine_id", b.shrine_id);
                real(json, "shrine_distance_m", b.shrine_distance_m);
            } else if (!b.claim_id.empty()) {
                text(json, "claim_id", b.claim_id);
                text(json, "villain_uid", b.villain_uid);
                real(json, "claim_distance_m", b.claim_distance_m);
            }
            text(json, "school", b.school);
            text(json, "god_id", b.god_id);
            break;
        case NomadClass::Merchants:
            json.key("markets");
            strings(json, b.markets);
            integer(json, "market_count", b.market_count);
            real(json, "nearest_market_m", b.nearest_market_m);
            real(json, "road_distance_m", b.road_distance_m);
            integer(json, "road_node", b.road_node);
            break;
        case NomadClass::Bandits:
            text(json, "prize_uid", b.prize_uid);
            real(json, "prize_distance_m", b.prize_distance_m);
            real(json, "ruggedness", b.ruggedness);
            real(json, "nearest_seat_m", b.nearest_seat_m);
            break;
        case NomadClass::Wanderers:
            real(json, "forage", b.forage);
            real(json, "seasonal_swing", b.seasonal_swing);
            real(json, "openness", b.openness);
            break;
    }
    if (!b.inherited_camp.empty()) text(json, "inherited_camp", b.inherited_camp);
    json.end_object();
}

void nomad_camp(JsonWriter& json, const NomadCamp& c) {
    json.begin_object();
    text(json, "id", c.id);
    integer(json, "node", c.node);
    json.key("direction");
    vec3(json, c.direction);
    text(json, "kind", to_string(c.kind));
    optional_int(json, "arrive_day", c.arrive_day);
    optional_int(json, "depart_day", c.depart_day);
    json.end_object();
}

void nomad_band(JsonWriter& json, const NomadBand& b) {
    json.begin_object();
    text(json, "uid", b.uid);
    text(json, "classification", to_string(b.classification));
    text_or_null(json, "parent_uid", b.parent_uid);
    json.key("origin");
    json.begin_object();
    text(json, "kind", to_string(b.origin.kind));
    text_or_null(json, "id", b.origin.id);
    integer(json, "age", b.origin.age);
    json.end_object();
    text_or_null(json, "god_id", b.god_id);
    text_or_null(json, "school", b.school);
    integer(json, "node", b.node);
    integer(json, "x", b.x);
    integer(json, "z", b.z);
    json.key("direction");
    vec3(json, b.direction);
    integer(json, "biome", b.biome);
    integer(json, "size", b.size);
    real(json, "speed_m_per_day", b.speed_m_per_day);
    real(json, "column_length_m", b.column_length_m);
    text(json, "disposition", to_string(b.disposition));
    json.key("seeks");
    strings(json, b.seeks);
    json.key("carries");
    strings(json, b.carries);
    json.key("camps");
    json.begin_array();
    for (const NomadCamp& c : b.camps) nomad_camp(json, c);
    json.end_array();
    json.key("legs");
    json.begin_array();
    for (const NomadLeg& l : b.legs) {
        json.begin_object();
        text(json, "from", l.from);
        text(json, "to", l.to);
        json.key("nodes");
        ints(json, l.nodes);
        real(json, "length_m", l.length_m);
        optional_int(json, "depart_day", l.depart_day);
        optional_int(json, "arrive_day", l.arrive_day);
        text(json, "branch", to_string(l.branch));
        json.end_object();
    }
    json.end_array();
    json.key("basis");
    nomad_basis(json, b.basis);
    text(json, "route_status", to_string(b.route_status));
    // Set by the route pass on a routed band only; absent, not zero, otherwise.
    if (b.round_length_m) real(json, "round_length_m", *b.round_length_m);
    json.end_object();
}

void write_nomads(JsonWriter& json, const World& world) {
    const NomadBlock& n = world.nomads;
    json.begin_object();
    integer(json, "version", n.version);
    json.key("groups");
    json.begin_array();
    for (const NomadBand& b : n.groups) nomad_band(json, b);
    json.end_array();
    json.key("counts");
    json.begin_object();
    for (int c = 0; c < kNomadClassCount; ++c) {
        integer(json, to_string(static_cast<NomadClass>(c)), n.counts[static_cast<std::size_t>(c)]);
    }
    json.end_object();
    json.key("rolls");
    json.begin_array();
    for (const NomadRoll& r : n.rolls) {
        json.begin_object();
        integer(json, "node", r.node);
        integer(json, "x", r.x);
        integer(json, "z", r.z);
        json.key("eligible");
        json.begin_array();
        for (const NomadClass c : r.eligible) json.value(to_string(c));
        json.end_array();
        json.key("evidence");
        json.begin_object();
        for (std::size_t k = 0; k < r.eligible.size() && k < r.evidence.size(); ++k) {
            json.key(to_string(r.eligible[k]));
            nomad_basis(json, r.evidence[k]);
        }
        json.end_object();
        flag(json, "precipitated", r.precipitated);
        if (r.precipitated) {
            // A roll that raised nobody has no draw, choice or band to name.
            real(json, "roll", r.roll);
            text(json, "chose", to_string(r.chose));
            text(json, "uid", r.uid);
        }
        json.end_object();
    }
    json.end_array();
    text(json, "method", n.method);
    text(json, "limits", n.limits);
    if (n.routes) {
        // `elapsed_ms` is a wall-clock measurement and stays off the document.
        const NomadRoutes& r = *n.routes;
        json.key("routes");
        json.begin_object();
        integer(json, "version", r.version);
        integer(json, "routed", r.routed);
        integer(json, "stranded", r.stranded);
        integer(json, "fissioned", r.fissioned);
        real(json, "day_march_m", r.day_march_m);
        text(json, "method", r.method);
        text(json, "limits", r.limits);
        json.end_object();
    }
    if (ran(world, "nomad_effects")) {
        // The reference's summary of its four write-backs. The ley edits themselves were
        // applied to magic.networks by stage 16 and are published there; the raid pressure
        // rows belong on threat_assessments.cities, which this port does not yet carry.
        const NomadEffects& e = world.nomad_effects;
        json.key("effects");
        json.begin_object();
        integer(json, "version", e.version);
        integer(json, "leyline_edits", e.leyline_edits_known);
        integer(json, "leyline_edits_hidden", e.leyline_edits_hidden);
        integer(json, "towns_under_raid_pressure", e.towns_under_raid_pressure);
        integer(json, "roads_ridden", static_cast<long long>(e.caravan_trade.size()));
        integer(json, "settlement_candidates", static_cast<long long>(e.settlement_candidates.size()));
        text(json, "method", e.method);
        text(json, "limits", e.limits);
        json.end_object();
    }
    json.end_object();
}

void write_settlement_candidates(JsonWriter& json, const NomadEffects& e) {
    json.begin_array();
    for (const NomadSettlementCandidate& c : e.settlement_candidates) {
        json.begin_object();
        text(json, "id", c.id);
        text(json, "from_band", c.from_band);
        integer(json, "node", c.node);
        json.key("direction");
        vec3(json, c.direction);
        integer(json, "migration_source_node", c.migration_source_node);
        real(json, "migration_distance_m", c.migration_distance_m);
        integer(json, "population_estimate", c.population_estimate);
        text(json, "reason", c.reason);
        // Written by absorption at an age boundary, and only then.
        if (c.absorbed_age) integer(json, "absorbed_age", *c.absorbed_age);
        if (!c.absorbed_into.empty()) text(json, "absorbed_into", c.absorbed_into);
        json.end_object();
    }
    json.end_array();
}

std::string_view creature_class_name(CreatureClass c) { return c == CreatureClass::Monster ? "monster" : "animal"; }

void beast_camp(JsonWriter& json, const BeastCamp& c) {
    json.begin_object();
    text(json, "id", c.id);
    integer(json, "node", c.node);
    json.key("direction");
    vec3(json, c.direction);
    text(json, "kind", c.kind);
    optional_int(json, "arrive_day", c.arrive_day);
    optional_int(json, "depart_day", c.depart_day);
    json.end_object();
}

void write_beast_movements(JsonWriter& json, const BeastMovements& m) {
    json.begin_object();
    integer(json, "version", m.version);
    json.key("groups");
    json.begin_array();
    for (const BeastGroup& g : m.groups) {
        json.begin_object();
        text(json, "uid", g.uid);
        text(json, "species_id", g.species_id);
        text(json, "name", g.name);
        text(json, "movement", to_string(g.movement));
        text(json, "family", g.family);
        integer(json, "tier", g.tier);
        text(json, "kind", creature_class_name(g.kind));
        text(json, "source", g.source);
        text_or_null(json, "host_uid", g.host_uid);
        integer(json, "node", g.node);
        integer(json, "x", g.x);
        integer(json, "z", g.z);
        json.key("direction");
        vec3(json, g.direction);
        integer(json, "size", g.size);
        real(json, "speed_m_per_day", g.speed_m_per_day);
        real(json, "column_length_m", g.column_length_m);
        text(json, "disposition", g.disposition);
        text_or_null(json, "role", g.role);
        json.key("camps");
        json.begin_array();
        for (const BeastCamp& c : g.camps) beast_camp(json, c);
        json.end_array();
        json.key("legs");
        json.begin_array();
        for (const BeastLeg& l : g.legs) {
            json.begin_object();
            text(json, "from", l.from);
            text(json, "to", l.to);
            json.key("nodes");
            ints(json, l.nodes);
            real(json, "length_m", l.length_m);
            optional_int(json, "depart_day", l.depart_day);
            optional_int(json, "arrive_day", l.arrive_day);
            text(json, "branch", l.branch);
            json.end_object();
        }
        json.end_array();
        text(json, "route_status", to_string(g.route_status));
        if (g.round_length_m) real(json, "round_length_m", *g.round_length_m);
        json.end_object();
    }
    json.end_array();
    json.key("counts");
    json.begin_object();
    for (const auto& [movement, count] : m.counts) integer(json, movement, count);
    json.end_object();
    integer(json, "considered", m.considered);
    integer(json, "skipped", m.skipped);
    real(json, "share", m.share);
    integer(json, "routed", m.routed);
    integer(json, "stranded", m.stranded);
    integer(json, "solitary", m.solitary);
    integer(json, "year", m.year);
    real(json, "year_forage_factor", m.year_forage_factor);
    text(json, "method", m.method);
    text(json, "limits", m.limits);
    json.end_object();
}

void write_encounters(JsonWriter& json, const EncounterIndex& e) {
    json.begin_object();
    integer(json, "version", e.version);
    json.key("entries");
    json.begin_array();
    for (const EncounterEntry& entry : e.entries) {
        json.begin_object();
        text(json, "group_uid", entry.group_uid);
        text(json, "source", entry.source);
        text(json, "kind", entry.kind);
        text(json, "domain", entry.domain);
        text(json, "class", entry.entity_class);
        flag(json, "airborne", entry.airborne);
        text(json, "disposition", entry.disposition);
        integer(json, "threat_tier", entry.threat_tier);
        integer(json, "size", entry.size);
        json.key("carries");
        strings(json, entry.carries);
        json.key("seeks");
        strings(json, entry.seeks);
        text_or_null(json, "host_uid", entry.host_uid);
        text_or_null(json, "god_id", entry.god_id);
        real(json, "speed_m_per_day", entry.speed_m_per_day);
        real(json, "column_length_m", entry.column_length_m);
        json.end_object();
    }
    json.end_array();
    json.key("occupancy");
    json.begin_array();
    for (const EncounterOccupancy& o : e.occupancy) {
        json.begin_object();
        integer(json, "entry", o.entry);
        integer(json, "node", o.node);
        text(json, "camp_id", o.camp_id);
        text(json, "camp_kind", o.camp_kind);
        optional_int(json, "from_day", o.from_day);
        optional_int(json, "to_day", o.to_day);
        json.key("months");
        ints(json, o.months);
        json.end_object();
    }
    json.end_array();
    // Two dicts keyed by the published string. by_node is ascending BY NODE NUMBER, as the
    // C++ stores it and the reference sorts it (key=int), not by the string.
    json.key("by_month");
    json.begin_object();
    for (std::size_t m = 0; m < e.by_month.size(); ++m) {
        json.key(std::to_string(m));
        ints(json, e.by_month[m]);
    }
    json.end_object();
    json.key("by_node");
    json.begin_object();
    for (const auto& [node, indices] : e.by_node) {
        json.key(std::to_string(node));
        ints(json, indices);
    }
    json.end_object();
    text(json, "method", e.method);
    text(json, "limits", e.limits);
    json.end_object();
}

// ===========================================================================
// Stage 16: the cast and its story web
// ===========================================================================

void failed_block(JsonWriter& json, int version, std::string_view error) {
    json.begin_object();
    integer(json, "version", version);
    text(json, "status", "failed");
    text(json, "error", error);
    json.end_object();
}

void hero_alignment(JsonWriter& json, const HeroAlignment& a) {
    json.begin_object();
    real(json, "law", a.law);
    real(json, "good", a.good);
    text(json, "label", a.label);
    text(json, "code", a.code);
    json.key("drift");
    if (a.has_drift) {
        json.begin_object();
        real(json, "law", a.drift.law);
        real(json, "good", a.drift.good);
        json.end_object();
    } else {
        json.value_null();
    }
    json.end_object();
}

void hero_claim(JsonWriter& json, const HeroClaim& c) {
    json.begin_object();
    text(json, "verb", c.verb);
    text(json, "target_uid", c.target_uid);
    text(json, "target_name", c.target_name);
    json.end_object();
}

void hero_persona(JsonWriter& json, const HeroPersona& p) {
    json.begin_object();
    text(json, "archetype", p.archetype);
    text(json, "belief", p.belief);
    text(json, "wants", p.wants);
    text(json, "fears", p.fears);
    text(json, "tells", p.tells);
    text(json, "mechanic", p.mechanic);
    text(json, "manner", p.manner);
    text(json, "voice", p.voice);
    text(json, "lies", p.lies);
    text(json, "toward_player", p.toward_player);
    text(json, "cornered", p.cornered);
    json.key("never");
    strings(json, p.never);
    text(json, "line", p.line);
    json.end_object();
}

void hero_presence(JsonWriter& json, const std::optional<HeroPresence>& p) {
    json.key("presence");
    if (!p) {
        json.value_null();
        return;
    }
    json.begin_object();
    text(json, "site_kind", p->site_kind);
    text(json, "uid", p->uid);
    text(json, "situation", p->situation);
    json.end_object();
}

void hero_site_ref(JsonWriter& json, const HeroSiteRef& s) {
    json.begin_object();
    text(json, "kind", s.kind);
    text(json, "uid", s.uid);
    text(json, "name", s.name);
    json.end_object();
}

// A deed carries its four common keys, then whatever its event kind records: the war's kind
// and either the warlord's defeated city or the opponent, a creature tier, the city a Dread
// destroyed, or the reason a founding or a shrine gives.
void hero_deeds(JsonWriter& json, const std::vector<HeroDeed>& deeds) {
    json.begin_array();
    for (const HeroDeed& d : deeds) {
        json.begin_object();
        text(json, "event_kind", d.event_kind);
        text(json, "event_id", d.event_id);
        integer(json, "age", d.age);
        text(json, "role", d.role);
        if (d.event_kind == "war") {
            text(json, "kind", d.kind);
            if (!d.defeated_uid.empty()) {
                text(json, "defeated_uid", d.defeated_uid);
                text(json, "defeated_name", d.defeated_name);
                text(json, "defeated_class", d.defeated_class);
            } else {
                text_or_null(json, "opponent_uid", d.opponent_uid);
            }
        }
        if (!d.city_name.empty()) {
            text_or_null(json, "cause", d.cause);
            text(json, "city_name", d.city_name);
            text(json, "city_class", d.city_class);
        }
        if (d.has_tier) integer(json, "tier", d.tier);
        if (!d.reason.empty()) text(json, "reason", d.reason);
        json.end_object();
    }
    json.end_array();
}

void hero_log(JsonWriter& json, const std::vector<HeroLogEntry>& log) {
    json.begin_array();
    for (const HeroLogEntry& e : log) {
        json.begin_object();
        integer(json, "age", e.age);
        text(json, "event_kind", e.event_kind);
        text(json, "event_id", e.event_id);
        text(json, "role", e.role);
        text(json, "text", e.text);
        json.end_object();
    }
    json.end_array();
}

void hero_person(JsonWriter& json, const Hero& h) {
    json.begin_object();
    text(json, "uid", h.uid);
    text(json, "role", h.role);
    text(json, "well", h.well);
    text(json, "status", h.status);
    text(json, "civilization_id", h.civilization_id);
    text(json, "race_id", h.race_id);
    integer(json, "born_age", h.born_age);
    json.key("home");
    if (h.home) hero_site_ref(json, *h.home); else json.value_null();
    hero_presence(json, h.presence);
    text_or_null(json, "realm_uid", h.realm_uid);
    json.key("deeds");
    hero_deeds(json, h.deeds);
    json.key("claim");
    hero_claim(json, h.claim);
    real(json, "fame", h.fame);
    text(json, "tier", h.tier);
    text(json, "name", h.name);
    text(json, "epithet", h.epithet);
    text(json, "display_name", h.display_name);
    text(json, "archetype", h.archetype);
    text(json, "archetype_name", h.archetype_name);
    json.key("alignment");
    hero_alignment(json, h.alignment);
    json.key("persona");
    hero_persona(json, h.persona);
    json.key("faces");
    json.begin_array();
    for (const HeroFace& f : h.faces) {
        json.begin_object();
        text(json, "label", f.label);
        text(json, "name", f.name);
        text(json, "apparent_role", f.apparent_role);
        json.key("alignment");
        hero_alignment(json, f.alignment);
        json.key("claim");
        hero_claim(json, f.claim);
        json.key("persona");
        hero_persona(json, f.persona);
        json.end_object();
    }
    json.end_array();
    json.key("log");
    hero_log(json, h.log);
    text(json, "situation", h.situation);
    json.key("companion");
    json.begin_object();
    flag(json, "eligible", h.companion.eligible);
    text_or_null(json, "join_condition", h.companion.join_condition);
    text_or_null(json, "offered_from_face", h.companion.offered_from_face);
    json.end_object();
    json.key("rivals");
    strings(json, h.rivals);
    json.key("kin");
    strings(json, h.kin);
    json.key("allies");
    strings(json, h.allies);
    // Filled from the orgs' own member lists; the reference leaves it null always
    // (heroes.hpp: "A DELIBERATE DEPARTURE").
    text_or_null(json, "org_uid", h.org_uid);
    integer(json, "seed", h.seed);
    json.key("selectable");
    strings(json, h.selectable);
    // The six the reference always sets, null when the person has none.
    text_or_null(json, "predecessor_uid", h.predecessor_uid);
    text_or_null(json, "school", h.school);
    text_or_null(json, "node_id", h.node_id);
    text_or_null(json, "seat", h.seat);
    text_or_null(json, "nest_id", h.nest_id);
    text_or_null(json, "site_name", h.site_name);

    // The per-role keys, present only on the roles whose wells write them.
    if (h.lost) {
        json.key("lost");
        json.begin_object();
        text(json, "uid", h.lost->uid);
        text(json, "ruin_id", h.lost->ruin_id);
        text(json, "name", h.lost->name);
        text(json, "city_class", h.lost->city_class);
        text_or_null(json, "cause", h.lost->cause);
        integer(json, "destroyed_age", h.lost->destroyed_age);
        json.end_object();
    }
    if (!h.victor_uid.empty()) {  // a warlord
        text(json, "victor_uid", h.victor_uid);
        text(json, "victor_name", h.victor_name);
        flag(json, "victor_fell", h.victor_fell);
        json.key("defeated");
        json.begin_array();
        for (const HeroDefeatedCity& d : h.defeated) {
            json.begin_object();
            text(json, "uid", d.uid);
            text(json, "name", d.name);
            text(json, "war_id", d.war_id);
            text(json, "kind", d.kind);
            json.end_object();
        }
        json.end_array();
    }
    if (h.college) {
        json.key("college");
        json.begin_object();
        text(json, "id", h.college->id);
        integer(json, "node", h.college->node);
        integer(json, "x", h.college->x);
        integer(json, "z", h.college->z);
        json.end_object();
    }
    // A champion names its nest; a reeve names the one threatening its fields, or null.
    if (h.role == "champion" || h.role == "reeve") text_or_null(json, "nest_name", h.nest_name);
    if (h.has_routes) integer(json, "routes", h.routes);
    if (h.well == "shrines") text_or_null(json, "god_id", h.god_id);
    if (!h.diaspora_reason.empty()) {  // a diaspora leader
        text(json, "diaspora_reason", h.diaspora_reason);
        text_or_null(json, "mother_civilization_id", h.mother_civilization_id);
    }
    json.end_object();
}

void hero_dread(JsonWriter& json, const HeroDread& d) {
    json.begin_object();
    text(json, "uid", d.uid);
    text(json, "role", d.role);
    text(json, "well", d.well);
    text(json, "nest_id", d.nest_id);
    text(json, "species", d.species);
    text_or_null(json, "family", d.family);
    integer(json, "nest_tier", d.nest_tier);
    text(json, "status", d.status);
    hero_presence(json, d.presence);
    json.key("deeds");
    hero_deeds(json, d.deeds);
    json.key("destroyed");
    json.begin_array();
    for (const HeroDreadKill& k : d.destroyed) {
        json.begin_object();
        text(json, "uid", k.uid);
        text(json, "name", k.name);
        integer(json, "age", k.age);
        json.end_object();
    }
    json.end_array();
    real(json, "fame", d.fame);
    text(json, "tier", d.tier);
    text(json, "name", d.name);
    text(json, "display_name", d.display_name);
    // A Dread with no eligible card keeps these null and raises nothing.
    text_or_null(json, "archetype", d.archetype);
    text_or_null(json, "archetype_name", d.archetype_name);
    json.key("alignment");
    hero_alignment(json, d.alignment);
    json.key("persona");
    if (d.has_persona) hero_persona(json, d.persona); else json.value_null();
    json.key("log");
    hero_log(json, d.log);
    text(json, "situation", d.situation);
    integer(json, "seed", d.seed);
    json.key("selectable");
    strings(json, d.selectable);
    json.end_object();
}

// The five policy revisions, in POLICY_FILES order. The schema version and digest the C++
// also carries are not part of the reference's `policy_revision`.
void hero_revisions(JsonWriter& json, const HeroCatalogueIdentity& r) {
    json.begin_object();
    integer(json, "archetypes", r.archetypes);
    integer(json, "axis_overlays", r.axis_overlays);
    integer(json, "alignment", r.alignment);
    integer(json, "names", r.names);
    integer(json, "wells", r.wells);
    json.end_object();
}

void write_heroes(JsonWriter& json, const HeroesBlock& h) {
    if (h.status != "ok") {
        failed_block(json, h.version, h.error);
        return;
    }
    json.begin_object();
    integer(json, "version", h.version);
    text(json, "status", h.status);
    json.key("policy_revision");
    hero_revisions(json, h.policy_revision);
    integer(json, "final_age", h.final_age);
    json.key("summary");
    json.begin_object();
    integer(json, "living", h.summary.living);
    integer(json, "legends", h.summary.legends);
    integer(json, "dreads", h.summary.dreads);
    integer(json, "realms", h.summary.realms);
    integer(json, "orgs", h.summary.orgs);
    integer(json, "camps", h.summary.camps);
    integer(json, "hooks", h.summary.hooks);
    integer(json, "candidates", h.summary.candidates);
    integer(json, "precipitated", h.summary.precipitated);
    json.end_object();

    json.key("people");
    json.begin_array();
    for (const Hero& p : h.people) hero_person(json, p);
    json.end_array();

    json.key("orgs");
    json.begin_array();
    for (const HeroOrg& o : h.orgs) {
        json.begin_object();
        text(json, "uid", o.uid);
        text(json, "kind", o.kind);
        text(json, "name", o.name);
        json.key("home");
        hero_site_ref(json, o.home);
        json.key("members");
        strings(json, o.members);
        json.key("charter");
        json.begin_object();
        if (o.kind == "order" || o.kind == "remnant") {
            text(json, "against", o.against);
            text_or_null(json, "nest_id", o.nest_id);
        } else {
            text(json, "holds", o.holds);
        }
        json.end_object();
        json.end_object();
    }
    json.end_array();

    json.key("realms");
    json.begin_array();
    for (const HeroRealm& r : h.realms) {
        json.begin_object();
        text(json, "uid", r.uid);
        text(json, "name", r.name);
        text(json, "english_name", r.english_name);
        text(json, "civilization_id", r.civilization_id);
        text(json, "civilization_name", r.civilization_name);
        text(json, "parent_race_id", r.parent_race_id);
        text(json, "capital_uid", r.capital_uid);
        json.key("city_uids");
        strings(json, r.city_uids);
        text(json, "culture_id", r.culture_id);
        text_or_null(json, "sovereign_uid", r.sovereign_uid);
        json.end_object();
    }
    json.end_array();

    json.key("dreads");
    json.begin_array();
    for (const HeroDread& d : h.dreads) hero_dread(json, d);
    json.end_array();

    json.key("camps");
    json.begin_array();
    for (const HeroCamp& c : h.camps) {
        json.begin_object();
        text(json, "uid", c.uid);
        text(json, "ruin_uid", c.ruin_uid);
        text(json, "name", c.name);
        integer(json, "node", c.node);
        integer(json, "x", c.x);
        integer(json, "z", c.z);
        json.key("direction");
        vec3(json, c.direction);
        text(json, "origin", c.origin);
        text(json, "civilization_id", c.civilization_id);
        integer(json, "since_age", c.since_age);
        if (!c.leader_uid.empty()) text(json, "leader_uid", c.leader_uid);
        json.end_object();
    }
    json.end_array();

    json.key("quest_hooks");
    json.begin_array();
    for (const HeroQuestHook& q : h.quest_hooks) {
        json.begin_object();
        text(json, "hook_id", q.hook_id);
        text(json, "giver_uid", q.giver_uid);
        text(json, "offered_from_face", q.offered_from_face);
        text(json, "stated_purpose", q.stated_purpose);
        json.key("actual_effect");
        json.begin_object();
        const HeroEffect& e = q.actual_effect;
        text(json, "kind", e.kind);
        if (!e.uid.empty()) text(json, "uid", e.uid);
        if (!e.node_id.empty()) text(json, "node_id", e.node_id);
        if (!e.nest_id.empty()) text(json, "nest_id", e.nest_id);
        if (!e.school.empty()) text(json, "school", e.school);
        if (e.has_delta) real(json, "intensity_delta", e.intensity_delta);
        if (!e.action.empty()) text(json, "action", e.action);
        json.end_object();
        flag(json, "unwitting", q.unwitting);
        text_or_null(json, "target", q.target);
        text(json, "verb", q.verb);
        integer(json, "difficulty", q.difficulty);
        json.key("target_node");
        if (q.has_target_node) json.value(q.target_node); else json.value_null();
        text_or_null(json, "unsited_reason", q.unsited_reason);
        json.end_object();
    }
    json.end_array();

    json.key("bonds");
    json.begin_array();
    for (const HeroBond& b : h.bonds) {
        json.begin_object();
        text(json, "a", b.a);
        text(json, "b", b.b);
        text(json, "kind", b.kind);
        text(json, "from_event", b.from_event);
        json.end_object();
    }
    json.end_array();

    json.key("relics");
    json.begin_array();
    for (const HeroRelic& r : h.relics) {
        json.begin_object();
        text(json, "uid", r.uid);
        text(json, "kind", r.kind);
        text(json, "name", r.name);
        text(json, "origin_event", r.origin_event);
        text(json, "resting_at", r.resting_at);
        text_or_null(json, "holder_uid", r.holder_uid);
        text_or_null(json, "school", r.school);
        json.end_object();
    }
    json.end_array();

    json.key("mantles");
    json.begin_array();
    for (const HeroMantle& m : h.mantles) {
        json.begin_object();
        json.key("claim");
        hero_claim(json, m.claim);
        text(json, "predecessor_uid", m.predecessor_uid);
        text(json, "civilization_id", m.civilization_id);
        integer(json, "vacant_since_age", m.vacant_since_age);
        json.end_object();
    }
    json.end_array();

    json.key("rolls");
    json.begin_array();
    for (const HeroRoll& r : h.rolls) {
        json.begin_object();
        text(json, "candidate_uid", r.candidate_uid);
        text(json, "kind", r.kind);
        // Null, not absent, for a candidate with no deeds: the reference reads them off the
        // first deed with a None default.
        text_or_null(json, "event_id", r.event_id);
        json.key("age");
        if (r.has_age) json.value(r.age); else json.value_null();
        real(json, "chance", r.chance);
        real(json, "roll", r.roll);
        flag(json, "precipitated", r.precipitated);
        json.end_object();
    }
    json.end_array();

    json.key("diagnostics");
    json.begin_array();
    for (const HeroDiagnostic& d : h.diagnostics) {
        json.begin_object();
        text(json, "role", d.role);
        integer(json, "placed", d.placed);
        integer(json, "candidates", d.candidates);
        integer(json, "sources", d.sources);
        text(json, "source_kind", d.source_kind);
        text(json, "reason", d.reason);
        json.end_object();
    }
    json.end_array();

    text(json, "method", h.method);
    text(json, "limits", h.limits);
    json.end_object();
}

void story_considered(JsonWriter& json, const std::vector<StoryConsidered>& rows) {
    json.begin_array();
    for (const StoryConsidered& c : rows) {
        json.begin_object();
        text(json, "trope_id", c.trope_id);
        json.key("missing");
        strings(json, c.missing);
        json.end_object();
    }
    json.end_array();
}

void write_story_web(JsonWriter& json, const StoryWebBlock& s) {
    if (s.status != "ok") {
        failed_block(json, s.version, s.error);
        return;
    }
    json.begin_object();
    integer(json, "version", s.version);
    text(json, "status", s.status);
    json.key("policy_revision");
    json.begin_object();
    integer(json, "tropes", s.policy_revision.tropes);
    integer(json, "constraints", s.policy_revision.constraints);
    integer(json, "weights", s.policy_revision.weights);
    json.end_object();
    json.key("heroes_revision");
    hero_revisions(json, s.heroes_revision);
    json.key("summary");
    json.begin_object();
    integer(json, "woven", s.summary.woven);
    integer(json, "unwoven", s.summary.unwoven);
    integer(json, "tropes", s.summary.tropes);
    integer(json, "reachable_tropes", s.summary.reachable_tropes);
    json.key("offered");
    json.begin_object();
    for (const auto& [trope, count] : s.summary.offered) integer(json, trope, count);
    json.end_object();
    real(json, "mean_spokes", s.summary.mean_spokes);
    json.end_object();

    json.key("webs");
    json.begin_array();
    for (const StoryWebEntry& w : s.webs) {
        json.begin_object();
        text(json, "uid", w.uid);
        text_or_null(json, "display_name", w.display_name);
        text(json, "role", w.role);
        text_or_null(json, "archetype", w.archetype);
        json.key("alignment");
        json.begin_object();
        real(json, "law", w.law);
        real(json, "good", w.good);
        text(json, "code", w.code);
        real(json, "angle", w.angle);
        json.end_object();
        text(json, "claim", w.claim);
        json.key("facts");
        strings(json, w.facts);
        json.key("constraints");
        json.begin_object();
        flag(json, "claim_locked", w.claim_locked);
        flag(json, "drift_locked", w.drift_locked);
        json.key("allowed_verbs");
        if (w.has_allowed_verbs) strings(json, w.allowed_verbs); else json.value_null();
        json.end_object();
        json.key("spokes");
        json.begin_array();
        for (const StorySpoke& sp : w.spokes) {
            json.begin_object();
            text(json, "trope_id", sp.trope_id);
            text(json, "name", sp.name);
            real(json, "weight", sp.weight);
            json.key("breakdown");
            json.begin_object();
            real(json, "boosts", sp.breakdown.boosts);
            json.key("boosted_by");
            strings(json, sp.breakdown.boosted_by);
            real(json, "alignment_fit", sp.breakdown.alignment_fit);
            real(json, "archetype_fit", sp.breakdown.archetype_fit);
            real(json, "claim_fit", sp.breakdown.claim_fit);
            json.end_object();
            real(json, "angle", sp.angle);
            json.end_object();
        }
        json.end_array();
        text(json, "offered", w.offered);
        json.key("rest");
        json.begin_object();
        json.key("hook");
        json.begin_object();
        text(json, "trope_id", w.rest.hook_trope_id);
        text(json, "act_id", w.rest.hook_act_id);
        text(json, "prompt", w.rest.hook_prompt);
        integer(json, "expires_day", w.rest.expires_day);
        json.end_object();
        json.key("armed");
        json.begin_array();
        for (const auto& [trope, weight] : w.rest.armed) {
            json.begin_object();
            text(json, "trope_id", trope);
            real(json, "weight", weight);
            json.end_object();
        }
        json.end_array();
        integer(json, "initiative_day", w.rest.initiative_day);
        json.end_object();
        json.key("acts");
        json.begin_array();
        for (const StoryBoundAct& a : w.acts) {
            json.begin_object();
            text(json, "id", a.id);
            text(json, "title", a.title);
            integer(json, "ring", a.ring);
            text(json, "prompt", a.prompt);
            json.key("completion");
            json.begin_object();
            text(json, "kind", a.completion_kind);
            text(json, "target", a.completion_target);
            text(json, "state", a.completion_state);
            json.end_object();
            json.key("abandonment");
            json.begin_object();
            text(json, "kind", a.abandonment_kind);
            integer(json, "days", a.abandonment_days);
            json.key("implicit");
            strings(json, a.implicit);
            json.end_object();
            json.key("options");
            json.begin_array();
            for (const StoryBoundOption& o : a.options) {
                json.begin_object();
                text(json, "id", o.id);
                text(json, "text", o.text);
                json.key("delta");
                json.begin_object();
                real(json, "law", o.delta.law);
                real(json, "good", o.delta.good);
                json.end_object();
                text(json, "effect", o.effect);
                json.end_object();
            }
            json.end_array();
            json.end_object();
        }
        json.end_array();
        json.key("threads");
        json.begin_array();
        for (const StoryThread& t : w.threads) {
            json.begin_object();
            text(json, "to", t.to);
            real(json, "weight", t.weight);
            json.key("condition");
            json.begin_object();
            text(json, "kind", t.condition_kind);
            if (t.condition_kind == "claim") {
                text(json, "verb", t.verb);
            } else if (t.condition_kind == "feature") {
                json.key("facts");
                strings(json, t.facts);
            } else if (t.condition_kind == "alignment") {
                text(json, "axis", t.axis);
                text(json, "toward", t.toward);
            }
            json.end_object();
            text_or_null(json, "cut", t.cut);
            json.end_object();
        }
        json.end_array();
        json.key("considered");
        story_considered(json, w.considered);
        json.key("examples");
        strings(json, w.examples);
        json.end_object();
    }
    json.end_array();

    json.key("unwoven");
    json.begin_array();
    for (const StoryUnwoven& u : s.unwoven) {
        json.begin_object();
        text(json, "uid", u.uid);
        text_or_null(json, "display_name", u.display_name);
        text_or_null(json, "archetype", u.archetype);
        json.key("facts");
        strings(json, u.facts);
        json.key("nearest");
        story_considered(json, u.nearest);
        json.end_object();
    }
    json.end_array();

    json.key("tropes");
    json.begin_array();
    for (const StoryTropeSummary& t : s.tropes) {
        json.begin_object();
        text(json, "id", t.id);
        text(json, "name", t.name);
        json.key("pull");
        json.begin_object();
        real(json, "law", t.pull.law);
        real(json, "good", t.pull.good);
        json.end_object();
        json.key("cast");
        strings(json, t.cast);
        real(json, "angle", t.angle);
        json.end_object();
    }
    json.end_array();
    json.key("unreachable_tropes");
    strings(json, s.unreachable_tropes);
    text(json, "method", s.method);
    text(json, "limits", s.limits);
    json.end_object();
}

// ===========================================================================
// Stage 16: the working population
// ===========================================================================

void npc_number(JsonWriter& json, std::string_view key, const NpcNumber& v) {
    json.key(key);
    if (!v.present || v.null_value) {
        json.value_null();
    } else {
        number(json, v.value);
    }
}

void write_npcs(JsonWriter& json, const NpcRoster& r) {
    if (!r.ok) {
        failed_block(json, r.version, r.error);
        return;
    }
    json.begin_object();
    integer(json, "version", r.version);
    text(json, "status", "ok");
    json.key("policy_revision");
    json.begin_object();
    integer(json, "posts", r.policy_revision_posts);
    integer(json, "names", r.policy_revision_names);
    json.end_object();
    json.key("registry");
    json.begin_object();
    optional_int(json, "world_revision", r.registry_world_revision);
    text_or_null(json, "world_sha256", r.registry_world_sha256);
    text(json, "labels_from", r.registry_labels_from);
    json.end_object();

    // One flat object in the reference: the headline counts, then every note counter.
    const NpcSummary& s = r.summary;
    json.key("summary");
    json.begin_object();
    integer(json, "total", s.total);
    integer(json, "alive", s.alive);
    integer(json, "dead", s.dead);
    integer(json, "important", s.important);
    real(json, "important_fraction", s.important_fraction);
    integer(json, "sites", s.sites);
    integer(json, "heroes_linked", s.heroes_linked);
    integer(json, "unresolved_parent_race", s.notes.unresolved_parent_race);
    integer(json, "unanchored_sites", s.notes.unanchored_sites);
    integer(json, "roster_mismatches", s.notes.roster_mismatches);
    integer(json, "duplicate_site_keys", s.notes.duplicate_site_keys);
    integer(json, "important_capped_out", s.notes.important_capped_out);
    integer(json, "cast_outside_a_planned_site", s.notes.cast_outside_a_planned_site);
    integer(json, "cast_located_by_presence", s.notes.cast_located_by_presence);
    integer(json, "cast_presence_unresolved", s.notes.cast_presence_unresolved);
    json.end_object();

    json.key("sites");
    json.begin_array();
    for (const NpcSite& site : r.sites) {
        json.begin_object();
        text(json, "uid", site.uid);
        text(json, "kind", site.kind);
        text(json, "name", site.name);
        optional_text(json, "civilization_id", site.civilization_id);
        optional_text(json, "parent_race_id", site.parent_race_id);
        optional_text(json, "city_class", site.city_class);
        text(json, "plan_id", site.plan_id);
        optional_int(json, "node", site.node);
        npc_number(json, "x", site.x);
        npc_number(json, "z", site.z);
        optional_text(json, "plan_status", site.plan_status);
        integer(json, "posts", site.posts);
        json.end_object();
    }
    json.end_array();

    json.key("people");
    json.begin_array();
    for (const NpcPerson& p : r.people) {
        json.begin_object();
        text(json, "uid", p.uid);
        optional_text(json, "name", p.name);
        optional_text(json, "site_uid", p.site_uid);
        optional_text(json, "site_kind", p.site_kind);
        text(json, "post", p.post);
        optional_text(json, "building_id", p.building_id);
        text(json, "plot_id", p.plot_id);
        text(json, "status", p.alive ? "alive" : "dead");
        flag(json, "important", p.important);
        if (p.is_cast) {
            text(json, "hero_uid", p.hero_uid);
            optional_text(json, "presence_kind", p.presence_kind);
            optional_text(json, "presence_uid", p.presence_uid);
            // ABSENT, not null, when the person stands where they are filed (npcs.hpp).
            if (p.presence_node) integer(json, "presence_node", *p.presence_node);
            if (p.presence_x) real(json, "presence_x", *p.presence_x);
            if (p.presence_z) real(json, "presence_z", *p.presence_z);
        }
        json.end_object();
    }
    json.end_array();

    json.key("post_verbs");
    json.begin_object();
    for (const NpcRoleVerbs& rv : npc_role_verbs()) {
        json.key(rv.role);
        strings(json, rv.verbs);
    }
    json.end_object();
    json.key("verbs");
    strings(json, npc_verbs());
    text(json, "method", r.method);
    text(json, "limits", r.limits);
    json.end_object();
}

// ===========================================================================
// Stage 16: key locations and their plans
// ===========================================================================
//
// The enums are the catalogue's closed vocabularies; each name is the lower-cased
// enumerator, which is exactly how tools/embed/key_locations.py generated the enumerator
// from the catalogue's word.

std::string_view domain_name(KeyLocationDomain v) {
    static constexpr std::string_view names[] = {"land", "water", "ocean", "lake", "any"};
    return names[static_cast<int>(v)];
}
std::string_view placement_name(KeyLocationPlacement v) {
    static constexpr std::string_view names[] = {"node", "path", "cluster"};
    return names[static_cast<int>(v)];
}
std::string_view along_name(KeyLocationAlong v) {
    static constexpr std::string_view names[] = {"road", "frontier", "approach"};
    return names[static_cast<int>(v)];
}
std::string_view plan_name(KeyLocationPlan v) {
    static constexpr std::string_view names[] = {"linear", "branching", "radial", "warren", "vault"};
    return names[static_cast<int>(v)];
}
std::string_view part_kind_name(KeyLocationPartKind v) {
    static constexpr std::string_view names[] = {"structure", "terrain", "opening", "path"};
    return names[static_cast<int>(v)];
}
std::string_view arrangement_name(KeyLocationArrangement v) {
    static constexpr std::string_view names[] = {"single", "ring", "scatter", "row", "courtyard", "terrace", "mouth"};
    return names[static_cast<int>(v)];
}
std::string_view shape_name(KeyLocationShape v) {
    static constexpr std::string_view names[] = {"mound", "cut", "rim", "platform", "bank", "trench", "level"};
    return names[static_cast<int>(v)];
}

void key_location_site(JsonWriter& json, const KeyLocationSite& s) {
    json.begin_object();
    text(json, "id", s.id);
    text(json, "placement", placement_name(s.placement));
    text(json, "kind", s.kind);
    text(json, "name", s.name);
    text(json, "family", s.family);
    integer(json, "tier", s.tier);
    text(json, "state", s.state);
    text(json, "occupant", s.occupant);
    integer(json, "node", s.node);
    integer(json, "x", s.x);
    integer(json, "z", s.z);
    json.key("direction");
    vec3(json, s.direction);
    real(json, "height_m", s.height_m);
    text(json, "layer", s.layer);
    text(json, "domain", domain_name(s.domain));
    real(json, "suitability", s.suitability);
    json.key("suitability_factors");
    json.begin_object();
    for (const KeyLocationFactor& f : s.suitability_factors) real(json, f.layer, f.value);
    json.end_object();

    json.key("origin");
    json.begin_object();
    integer(json, "age", s.origin.age);
    optional_text(json, "culture_id", s.origin.culture_id);
    optional_text(json, "civilization_id", s.origin.civilization_id);
    optional_text(json, "ruin_id", s.origin.ruin_id);
    optional_text(json, "school", s.origin.school);
    integer(json, "ages_since", s.origin.ages_since);
    json.end_object();

    const KeyLocationEnvironment& e = s.environment;
    json.key("environment");
    json.begin_object();
    optional_int(json, "natural_biome", e.natural_biome);
    text_or_null(json, "natural_biome_name", e.natural_biome_name);
    optional_int(json, "biome_variant", e.biome_variant);
    text_or_null(json, "variant_asset_id", e.variant_asset_id);
    text_or_null(json, "variant_name", e.variant_name);
    optional_real(json, "temperature_c", e.temperature_c);
    optional_real(json, "moisture", e.moisture);
    optional_real(json, "slope", e.slope);
    flag(json, "coastal", e.coastal);
    json.end_object();

    json.key("links");
    json.begin_object();
    text_or_null(json, "chain", s.links.chain);
    json.key("chain_index");
    if (s.links.chain_index >= 0) json.value(s.links.chain_index); else json.value_null();
    text_or_null(json, "cluster", s.links.cluster);
    text_or_null(json, "anchor", s.links.anchor);
    json.end_object();

    text(json, "asset_id", s.asset_id);
    text(json, "reason", s.reason);
    json.key("succession");
    if (s.succession) {
        json.begin_object();
        text(json, "from_occupant", s.succession->from_occupant);
        text(json, "to_occupant", s.succession->to_occupant);
        text(json, "note", s.succession->note);
        json.end_object();
    } else {
        json.value_null();
    }
    integer(json, "threat", s.threat);
    json.key("interior");
    if (s.interior) {
        const KeyLocationInterior& in = *s.interior;
        json.begin_object();
        integer(json, "version", in.version);
        text(json, "plan", plan_name(in.plan));
        text(json, "plan_note", in.plan_note);
        integer(json, "levels", in.levels);
        json.key("chambers");
        json.begin_array();
        for (const KeyLocationChamber& c : in.chambers) {
            json.begin_object();
            text(json, "id", c.id);
            integer(json, "depth", c.depth);
            text(json, "role", c.role);
            json.key("size_m");
            xz(json, c.size_w_m, c.size_d_m);
            json.key("connects");
            strings(json, c.connects);
            flag(json, "flooded", c.flooded);
            flag(json, "collapsed", c.collapsed);
            json.end_object();
        }
        json.end_array();
        json.key("entrances");
        json.begin_array();
        for (const KeyLocationEntrance& en : in.entrances) {
            json.begin_object();
            text(json, "id", en.id);
            text(json, "chamber", en.chamber);
            text(json, "kind", en.kind);
            flag(json, "hidden", en.hidden);
            json.end_object();
        }
        json.end_array();
        // Fixed prose the interior carries in the reference; the C++ keeps it once, in code.
        text(json, "method", key_location_interior_method());
        text(json, "limits", key_location_interior_limits());
        json.end_object();
    } else {
        json.value_null();
    }
    json.end_object();
}

void write_key_locations(JsonWriter& json, const KeyLocationBlock& b) {
    if (!b.ok) {
        failed_block(json, b.version, b.error);
        return;
    }
    json.begin_object();
    integer(json, "version", b.version);
    text(json, "status", "ok");
    integer(json, "catalogue_revision", b.catalogue_revision);
    integer(json, "catalogue_count", b.catalogue_count);
    json.key("summary");
    json.begin_object();
    integer(json, "sites", b.summary.sites);
    json.key("by_tier");
    json.begin_object();
    for (const auto& [tier, count] : b.summary.by_tier) integer(json, std::to_string(tier), count);
    json.end_object();
    json.key("by_family");
    json.begin_object();
    for (const auto& [family, count] : b.summary.by_family) integer(json, family, count);
    json.end_object();
    json.key("by_state");
    json.begin_object();
    for (const auto& [state, count] : b.summary.by_state) integer(json, state, count);
    json.end_object();
    json.key("by_occupant");
    json.begin_object();
    for (const auto& [occupant, count] : b.summary.by_occupant) integer(json, occupant, count);
    json.end_object();
    integer(json, "dungeons", b.summary.dungeons);
    json.end_object();

    json.key("sites");
    json.begin_array();
    for (const KeyLocationSite& s : b.sites) key_location_site(json, s);
    json.end_array();

    json.key("chains");
    json.begin_array();
    for (const KeyLocationChain& c : b.chains) {
        json.begin_object();
        text(json, "id", c.id);
        text(json, "kind", c.kind);
        text(json, "name", c.name);
        text(json, "archetype", c.archetype);
        text(json, "along", along_name(c.along));
        real(json, "interval_m", c.interval_m);
        real(json, "length_m", c.length_m);
        text_or_null(json, "anchor", c.anchor);
        json.key("members");
        strings(json, c.members);
        text(json, "reason", c.reason);
        json.end_object();
    }
    json.end_array();

    json.key("clusters");
    json.begin_array();
    for (const KeyLocationCluster& c : b.clusters) {
        json.begin_object();
        text(json, "id", c.id);
        text(json, "kind", c.kind);
        text(json, "name", c.name);
        text(json, "anchor", c.anchor);
        text(json, "anchor_kind", c.anchor_kind);
        text(json, "anchor_name", c.anchor_name);
        json.key("members");
        strings(json, c.members);
        text(json, "reason", c.reason);
        json.end_object();
    }
    json.end_array();

    // The catalogue's own family table, republished as the reference does.
    json.key("families");
    json.begin_object();
    for (const KeyLocationFamily& f : key_location_families()) {
        json.key(f.id);
        json.begin_object();
        text(json, "name", f.name);
        text(json, "glyph", f.glyph);
        json.key("color");
        json.begin_array();
        for (const int c : f.color) json.value(c);
        json.end_array();
        real(json, "family_spacing_cells", f.family_spacing_cells);
        json.end_object();
    }
    json.end_object();

    json.key("diagnostics");
    json.begin_array();
    for (const KeyLocationDiagnostic& d : b.diagnostics) {
        json.begin_object();
        text(json, "archetype", d.archetype);
        integer(json, "placed", d.placed);
        integer(json, "wanted", d.wanted);
        integer(json, "candidates", d.candidates);
        text(json, "reason", d.reason);
        json.end_object();
    }
    json.end_array();
    text(json, "method", b.method);
    text(json, "limits", b.limits);
    json.end_object();
}

// Which of the two vertical extents a shape's authored spec carries. KeyLocationShaping
// holds both as plain doubles with no presence flag, so absence is read off the shape:
// every shipped part authors exactly these (data/key-location-exteriors.json) and the
// reference copies the authored dict, so a mound has no depth_m key and a cut no height_m.
bool shape_has_height(KeyLocationShape s) {
    return s == KeyLocationShape::Mound || s == KeyLocationShape::Rim || s == KeyLocationShape::Level ||
           s == KeyLocationShape::Bank || s == KeyLocationShape::Platform;
}
bool shape_has_depth(KeyLocationShape s) {
    return s == KeyLocationShape::Cut || s == KeyLocationShape::Rim || s == KeyLocationShape::Trench;
}

void write_key_location_plans(JsonWriter& json, const KeyLocationPlanBlock& b) {
    if (!b.ok) {
        failed_block(json, b.version, b.error);
        return;
    }
    json.begin_object();
    integer(json, "version", b.version);
    text(json, "status", "ok");
    integer(json, "catalogue_revision", b.catalogue_revision);
    integer(json, "part_count", b.part_count);
    integer(json, "kit_count", b.kit_count);
    text(json, "unit", b.unit);
    text(json, "coordinates", b.coordinates);
    json.key("summary");
    json.begin_object();
    integer(json, "plans", b.summary_plans);
    integer(json, "plots", b.summary_plots);
    real(json, "plots_per_plan", b.summary_plots_per_plan);
    json.end_object();
    json.key("plans");
    json.begin_array();
    for (const KeyLocationSitePlan& p : b.plans) {
        json.begin_object();
        text(json, "location_id", p.location_id);
        text(json, "kind", p.kind);
        text(json, "family", p.family);
        integer(json, "tier", p.tier);
        text(json, "kit_id", p.kit_id);
        integer(json, "version", p.version);
        text(json, "status", p.complete ? "complete" : "empty");
        text(json, "arrangement", arrangement_name(p.arrangement));
        json.key("origin");
        json.begin_object();
        json.key("direction");
        vec3(json, p.origin_direction);
        real(json, "height_m", p.origin_height_m);
        integer(json, "node", p.origin_node);
        json.end_object();
        json.key("bounds_m");
        json.begin_object();
        real(json, "width", p.bounds_width_m);
        real(json, "depth", p.bounds_depth_m);
        json.end_object();
        real(json, "slope_bearing_degrees", p.slope_bearing_degrees);

        const KeyLocationTerrain& t = p.terrain;
        json.key("terrain");
        json.begin_object();
        real(json, "cell_m", t.cell_m);
        integer(json, "size", t.size);
        json.key("bounds_m");
        json.begin_array();
        for (const double v : t.bounds_m) number(json, v);
        json.end_array();
        json.key("shaping");
        json.begin_array();
        for (const KeyLocationShapingPlacement& s : t.shaping) {
            json.begin_object();
            text(json, "shape", shape_name(s.spec.shape));
            if (s.spec.has_radius) real(json, "radius_m", s.spec.radius_m);
            if (s.spec.has_length) real(json, "length_m", s.spec.length_m);
            if (s.spec.has_width) real(json, "width_m", s.spec.width_m);
            if (shape_has_height(s.spec.shape)) real(json, "height_m", s.spec.height_m);
            if (shape_has_depth(s.spec.shape)) real(json, "depth_m", s.spec.depth_m);
            real(json, "x_m", s.x_m);
            real(json, "z_m", s.z_m);
            real(json, "rotation_degrees", s.rotation_degrees);
            text(json, "part", s.part);
            json.end_object();
        }
        json.end_array();
        json.key("relief_m");
        json.begin_object();
        real(json, "raised_m", t.raised_m);
        real(json, "lowered_m", t.lowered_m);
        json.end_object();
        json.key("surface");
        json.begin_object();
        integer(json, "size", t.surface.size);
        real(json, "step_m", t.surface.step_m);
        json.key("heights_m");
        square_rows(json, t.surface.heights_m);
        json.end_object();
        json.end_object();

        json.key("plots");
        json.begin_array();
        for (const KeyLocationPlot& plot : p.plots) {
            json.begin_object();
            text(json, "id", plot.id);
            text(json, "building_id", plot.building_id);
            text(json, "name", plot.name);
            text(json, "kind", part_kind_name(plot.kind));
            integer(json, "phase", plot.phase);
            real(json, "x_m", plot.x_m);
            real(json, "z_m", plot.z_m);
            real(json, "rotation_degrees", plot.rotation_degrees);
            real(json, "ground_elevation_m", plot.ground_elevation_m);
            real(json, "foundation_bottom_m", plot.foundation_bottom_m);
            json.key("plot_m");
            json.begin_object();
            real(json, "width", plot.plot_width_m);
            real(json, "depth", plot.plot_depth_m);
            json.end_object();
            json.key("dimensions_m");
            json.begin_object();
            real(json, "width", plot.dimension_width_m);
            real(json, "depth", plot.dimension_depth_m);
            real(json, "height", plot.dimension_height_m);
            json.end_object();
            json.key("road_access");
            xz(json, plot.road_access_x_m, plot.road_access_z_m);
            integer(json, "workers", plot.workers);
            integer(json, "beds", plot.beds);
            json.end_object();
        }
        json.end_array();
        json.key("stats");
        json.begin_object();
        integer(json, "plots", p.stat_plots);
        integer(json, "parts", p.stat_parts);
        integer(json, "shaping", p.stat_shaping);
        json.end_object();
        json.end_object();
    }
    json.end_array();
    text(json, "method", b.method);
    text(json, "limits", b.limits);
    json.end_object();
}

// The college pass's diagnostic row names its institution and what it read; both are
// constants in the reference and not fields of the C++ report.
constexpr std::string_view kCollegeInstitution = "wizard_college";
constexpr std::string_view kCollegeSourceKind = "cells within a city's support reach";

// ===========================================================================
// Stages 13-15: the threat assessment, the ruins, the villains and the history
// ===========================================================================

void threat_contributor(JsonWriter& json, const ThreatContributor& c) {
    json.begin_object();
    text(json, "kind", c.kind);
    real(json, "weight", c.weight);
    if (c.kind == "ley") {
        text(json, "school", c.school);
        real(json, "potency", c.potency);
    } else if (c.kind == "war") {
        integer(json, "wars_fought", c.wars_fought);
    } else {
        text(json, "nest_id", c.nest_id);
        text(json, "name", c.name);
        text(json, "family", c.family);
        real(json, "distance_m", c.distance_m);
        real(json, "reach_m", c.reach_m);
    }
    json.end_object();
}

void write_threat_assessments(JsonWriter& json, const ThreatAssessmentBlock& b) {
    json.begin_object();
    integer(json, "version", b.version);
    text(json, "evaluated_after", b.evaluated_after);
    optional_int(json, "age", b.age);
    json.key("cities");
    json.begin_array();
    for (const ThreatReport& r : b.cities) {
        json.begin_object();
        text(json, "city_uid", r.city_uid);
        text(json, "city_name", r.city_name);
        integer(json, "site_id", r.site_id);
        real(json, "regional_threat", r.regional_threat);
        real(json, "nest_pressure", r.nest_pressure);
        real(json, "ley_pressure", r.ley_pressure);
        real(json, "war_pressure", r.war_pressure);
        integer(json, "wars_fought", r.wars_fought);
        integer(json, "wars_recent", r.outlook.wars_recent);
        flag(json, "enemy_living", r.outlook.enemy_living);
        real(json, "war_risk", r.outlook.war_risk);
        text_or_null(json, "war_risk_kind", r.outlook.war_risk_kind);
        text_or_null(json, "war_risk_opponent_uid", r.outlook.war_risk_opponent_uid);
        real(json, "war_hunger", r.outlook.war_hunger);
        text_or_null(json, "war_hunger_target_uid", r.outlook.war_hunger_target_uid);
        json.key("contributors");
        json.begin_array();
        for (const ThreatContributor& c : r.contributors) threat_contributor(json, c);
        json.end_array();
        json.key("ley");
        if (r.ley) {
            json.begin_object();
            text(json, "school", r.ley->school);
            real(json, "potency", r.ley->potency);
            json.end_object();
        } else {
            json.value_null();
        }
        // Written by stage 16's nomad effects: every town's raid pressure, and the raiders
        // by name where any are in reach.
        if (r.nomad_pressure) real(json, "nomad_pressure", *r.nomad_pressure);
        if (!r.nomad_contributors.empty()) {
            json.key("nomad_contributors");
            strings(json, r.nomad_contributors);
        }
        if (r.divine_favor) real(json, "divine_favor", *r.divine_favor);
        json.end_object();
    }
    json.end_array();
    text(json, "method", b.method);
    text(json, "limits", b.limits);
    json.end_object();
}

void war_participation(JsonWriter& json, const WarParticipation& w) {
    json.begin_object();
    text(json, "war_id", w.war_id);
    integer(json, "age", w.age);
    text(json, "kind", w.kind);
    text(json, "outcome", w.outcome);
    text(json, "opponent_uid", w.opponent_uid);
    text(json, "opponent_civilization_id", w.opponent_civilization_id);
    text(json, "opponent_parent_race_id", w.opponent_parent_race_id);
    json.end_object();
}

void war_history(JsonWriter& json, const std::vector<WarParticipation>& history) {
    json.key("war_history");
    json.begin_array();
    for (const WarParticipation& w : history) war_participation(json, w);
    json.end_array();
}

bool is_school_name(std::string_view cause) { return magic_school_index(cause) >= 0; }

// A fate's evidence carries the keys of the cause that was chosen, in that cause's order.
void fate_evidence(JsonWriter& json, const Ruin& ruin) {
    const std::string_view cause = ruin.cause;
    const CityFateEvidence& e = ruin.evidence;
    json.key("evidence");
    json.begin_object();
    if (cause == "rot_plague" || cause == "void_unmade") {
        // A corruption's: the god, its school, the falloff share, the corruption record.
        text(json, "god_id", ruin.god_id);
        text(json, "school", e.school);
        if (ruin.share) real(json, "share", *ruin.share);
        if (ruin.corruption) integer(json, "corruption", *ruin.corruption);
    } else if (cause.rfind("divine_", 0) == 0) {
        // A visitation's own cause: the god in person.
        text(json, "god_id", ruin.god_id);
        if (e.distance_m) real(json, "distance_m", *e.distance_m);
        if (e.reach_m) real(json, "reach_m", *e.reach_m);
        text(json, "aspect", ruin.aspect);
        if (ruin.faith_factor) real(json, "faith_factor", *ruin.faith_factor);
    } else if (is_school_name(cause)) {
        text(json, "school", e.school);
        if (e.potency) real(json, "potency", *e.potency);
        if (e.moon_tide) real(json, "moon_tide", *e.moon_tide);
    } else if (cause == "dragon" || cause == "monster") {
        text(json, "nest_id", e.nest_id);
        text(json, "family", e.family);
        if (e.distance_m) real(json, "distance_m", *e.distance_m);
        if (e.reach_m) real(json, "reach_m", *e.reach_m);
    } else if (cause.rfind("villain_", 0) == 0) {
        text(json, "villain_uid", e.villain_uid);
        text(json, "growth", e.growth);
        if (e.tier) real(json, "tier", *e.tier);
        if (e.distance_m) real(json, "distance_m", *e.distance_m);
        if (e.reach_m) real(json, "reach_m", *e.reach_m);
        text_or_null(json, "school", e.school);
    } else if (cause.rfind("war_", 0) == 0) {
        text(json, "war_id", e.war_id);
        text(json, "kind", e.kind);
        text(json, "opponent_uid", e.opponent_uid);
        text(json, "opponent_civilization_id", e.opponent_civilization_id);
        text(json, "opponent_parent_race_id", e.opponent_parent_race_id);
        if (e.distance_m) real(json, "distance_m", *e.distance_m);
        if (e.pressure) real(json, "pressure", *e.pressure);
    }
    // self_magic carries no evidence of its own.
    json.end_object();
}

void ruin_record(JsonWriter& json, const Ruin& r) {
    json.begin_object();
    // The city as it stood: the reference's RUIN_KEYS, in its order.
    text(json, "uid", r.uid);
    text(json, "name", r.name);
    integer(json, "node", r.node);
    integer(json, "x", r.x);
    integer(json, "z", r.z);
    json.key("direction");
    vec3(json, r.direction);
    real(json, "height_m", r.height_m);
    text(json, "population_profile", r.population_profile);
    text(json, "civilization_id", r.civilization_id);
    text(json, "city_class", city_class_name(r.city_class));
    text(json, "source_culture", r.source_culture);
    integer(json, "founded_age", r.founded_age);
    if (!r.war_history.empty()) war_history(json, r.war_history);
    // The ruin.
    text(json, "id", r.id);
    text(json, "kind", r.kind);
    integer(json, "destroyed_age", r.destroyed_age);
    text(json, "asset_id", r.asset_id);
    // How it fell.
    text(json, "cause", r.cause);
    text(json, "reason", r.reason);
    fate_evidence(json, r);
    real(json, "probability", r.probability);
    real(json, "roll", r.roll);
    text_or_null(json, "new_node_school", r.new_node_school);
    json.key("legacy");
    json.begin_object();
    text(json, "school", r.legacy.school);
    real(json, "intensity", r.legacy.intensity);
    text(json, "basis", r.legacy.basis);
    json.end_object();
    // Which operation made it, for a god's work.
    if (r.corruption) integer(json, "corruption", *r.corruption);
    if (r.visitation) integer(json, "visitation", *r.visitation);
    json.end_object();
}

void write_ruins(JsonWriter& json, const std::vector<Ruin>& ruins) {
    json.begin_array();
    for (const Ruin& r : ruins) ruin_record(json, r);
    json.end_array();
}

void villain_log(JsonWriter& json, const std::vector<VillainLogEntry>& log) {
    json.key("log");
    json.begin_array();
    for (const VillainLogEntry& e : log) {
        json.begin_object();
        integer(json, "age", e.age);
        text(json, "event", e.event);
        if (e.concentration) real(json, "concentration", *e.concentration);
        if (!e.school.empty()) text(json, "school", e.school);
        if (e.lines) integer(json, "lines", *e.lines);
        if (!e.god.empty()) text(json, "god", e.god);
        if (e.encounters) integer(json, "encounters", *e.encounters);
        json.end_object();
    }
    json.end_array();
}

void villain_well(JsonWriter& json, const std::optional<VillainWell>& well) {
    if (!well) return;
    json.key("well");
    json.begin_object();
    text(json, "node", well->node);
    text(json, "school", well->school);
    text_or_null(json, "god", well->god);
    json.key("lines");
    strings(json, well->lines);
    integer(json, "age", well->age);
    json.end_object();
}

void villain_claims(JsonWriter& json, const std::vector<VillainClaim>& claims) {
    if (claims.empty()) return;
    json.key("claims");
    json.begin_array();
    for (const VillainClaim& c : claims) {
        json.begin_object();
        text(json, "id", c.id);
        text(json, "villain_uid", c.villain_uid);
        integer(json, "age", c.age);
        json.key("direction");
        vec3(json, c.direction);
        integer(json, "node", c.node);
        text(json, "kind", c.kind);
        integer(json, "refreshed_age", c.refreshed_age);
        json.key("factions");
        strings(json, c.factions);
        json.key("drawn_from");
        strings(json, c.drawn_from);
        text_or_null(json, "well", c.well);
        text(json, "holder_status", c.holder_status);
        if (c.holder_fell_age) integer(json, "holder_fell_age", *c.holder_fell_age);
        real(json, "influence", c.influence);
        json.end_object();
    }
    json.end_array();
}

void villain_record(JsonWriter& json, const Villain& v) {
    json.begin_object();
    text(json, "uid", v.uid);
    text(json, "region", v.region);
    integer(json, "born_age", v.born_age);
    text(json, "seat_uid", v.seat_uid);
    integer(json, "node", v.node);
    json.key("direction");
    vec3(json, v.direction);
    text_or_null(json, "school", v.school);
    json.key("held_nodes");
    strings(json, v.held_nodes);
    real(json, "tier", v.tier);
    real(json, "reach_m", v.reach_m);
    text(json, "growth", v.growth);
    text(json, "status", v.status);
    if (v.fell_age) integer(json, "fell_age", *v.fell_age);
    villain_log(json, v.log);
    if (!v.god.empty()) text(json, "god", v.god);
    if (v.corrupted) flag(json, "corrupted", true);
    villain_well(json, v.well);
    villain_claims(json, v.claims);
    json.end_object();
}

void fallen_mark(JsonWriter& json, const FallenVillainMark& m) {
    json.begin_object();
    text(json, "uid", m.uid);
    text(json, "region", m.region);
    integer(json, "born_age", m.born_age);
    text(json, "seat_uid", m.seat_uid);
    integer(json, "node", m.node);
    json.key("direction");
    vec3(json, m.direction);
    text_or_null(json, "school", m.school);
    text(json, "growth", m.growth);
    json.key("held_nodes");
    strings(json, m.held_nodes);
    villain_claims(json, m.claims);
    villain_well(json, m.well);
    if (!m.god.empty()) text(json, "god", m.god);
    villain_log(json, m.log);
    text(json, "id", m.id);
    text(json, "kind", m.kind);
    integer(json, "fell_age", m.fell_age);
    integer(json, "reigned_ages", m.reigned_ages);
    real(json, "tier_at_fall", m.tier_at_fall);
    json.key("left");
    json.begin_object();
    json.key("ruins");
    strings(json, m.left_ruins);
    json.key("claims");
    strings(json, m.left_claims);
    text_or_null(json, "well", m.left_well);
    json.key("held_nodes");
    strings(json, m.left_held_nodes);
    json.end_object();
    json.end_object();
}

void write_villains(JsonWriter& json, const VillainBlock& b) {
    json.begin_object();
    integer(json, "version", b.version);
    json.key("people");
    json.begin_array();
    for (const Villain& v : b.people) villain_record(json, v);
    json.end_array();
    json.key("tiers");
    json.begin_object();
    for (const auto& [region, tier] : b.tiers) real(json, region, tier);
    json.end_object();
    json.key("outlook");
    json.begin_object();
    real(json, "rise", b.outlook.rise);
    integer(json, "ceiling", b.outlook.ceiling);
    integer(json, "standing", b.outlook.standing);
    json.key("regions");
    json.begin_array();
    for (const VillainOutlookRow& row : b.outlook.regions) {
        json.begin_object();
        text(json, "region", row.region);
        json.key("culture");
        if (row.has_culture) {
            json.value(std::string_view(row.culture));
        } else {
            json.value_null();
        }
        real(json, "concentration", row.concentration);
        real(json, "tier", row.tier);
        real(json, "to_threshold", row.to_threshold);
        flag(json, "seated", row.seated);
        flag(json, "stalled", row.stalled);
        integer(json, "cities", row.cities);
        json.end_object();
    }
    json.end_array();
    json.end_object();
    json.key("fallen");
    json.begin_array();
    for (const FallenVillainMark& m : b.fallen) fallen_mark(json, m);
    json.end_array();
    if (!b.works.empty()) {
        json.key("works");
        json.begin_array();
        for (const VillainWork& w : b.works) {
            json.begin_object();
            text(json, "villain_uid", w.villain_uid);
            flag(json, "well", w.well);
            flag(json, "claim", w.claim);
            json.end_object();
        }
        json.end_array();
    }
    json.end_object();
}

void war_record(JsonWriter& json, const WarRecord& w) {
    json.begin_object();
    text(json, "id", w.id);
    integer(json, "age", w.age);
    text(json, "kind", w.kind);
    json.key("participants");
    strings(json, w.participants);
    text(json, "victor_uid", w.victor_uid);
    text(json, "defeated_uid", w.defeated_uid);
    text(json, "victor_civilization_id", w.victor_civilization_id);
    text(json, "defeated_civilization_id", w.defeated_civilization_id);
    text(json, "victor_parent_race_id", w.victor_parent_race_id);
    text(json, "defeated_parent_race_id", w.defeated_parent_race_id);
    real(json, "distance_m", w.distance_m);
    real(json, "proximity_pressure", w.proximity_pressure);
    real(json, "supply_pressure", w.supply_pressure);
    real(json, "pressure", w.pressure);
    integer(json, "victor_strength", w.victor_strength);
    integer(json, "defeated_strength", w.defeated_strength);
    real(json, "chance", w.chance);
    real(json, "roll", w.roll);
    text(json, "reason", w.reason);
    // Written only when war_survival is above zero, as the reference writes them.
    if (!w.outcome.empty()) text(json, "outcome", w.outcome);
    if (w.survival_chance) real(json, "survival_chance", *w.survival_chance);
    if (w.survival_roll) real(json, "survival_roll", *w.survival_roll);
    json.end_object();
}

void moon_record(JsonWriter& json, const std::optional<CityFateLunar>& moon) {
    json.key("moon");
    if (!moon) {
        json.value_null();
        return;
    }
    json.begin_object();
    integer(json, "day", moon->day);
    real(json, "phase_fraction", moon->phase_fraction);
    real(json, "elongation_degrees", moon->elongation_degrees);
    real(json, "illumination", moon->illumination);
    text(json, "leaning", moon->leaning);
    json.key("tide");
    json.begin_object();
    const auto schools = magic_school_table();
    for (std::size_t i = 0; i < moon->tide.size() && i < schools.size(); ++i) {
        real(json, schools[i].name, moon->tide[i]);
    }
    json.end_object();
    json.end_object();
}

void leyline_edit(JsonWriter& json, const LeylineEdit& e) {
    json.begin_object();
    text(json, "school", e.school);
    if (e.node_id) text(json, "node_id", *e.node_id);
    if (e.line_id) text(json, "line_id", *e.line_id);
    if (e.intensity) real(json, "intensity", *e.intensity);
    if (e.new_node) {
        json.key("new_node");
        json.begin_object();
        text(json, "id", e.new_node->id);
        json.key("direction");
        vec3(json, e.new_node->direction);
        real(json, "intensity", e.new_node->intensity);
        json.end_object();
    }
    json.end_object();
}

void history_operation(JsonWriter& json, const AgeOperation& op) {
    json.begin_object();
    integer(json, "api_version", op.api_version);
    integer(json, "after_age", op.after_age);
    integer(json, "steps", op.steps);
    json.key("leyline_edits");
    json.begin_array();
    for (const LeylineEdit& e : op.leyline_edits) leyline_edit(json, e);
    json.end_array();
    json.key("moon_day");
    if (op.moon_day) {
        json.value(*op.moon_day);
    } else {
        json.value_null();
    }
    json.end_object();
}

void history_operation(JsonWriter& json, const CorruptionOperation& op) {
    json.begin_object();
    integer(json, "api_version", op.api_version);
    text(json, "kind", "corruption");
    integer(json, "corruption", op.corruption);
    text(json, "god_id", op.god_id);
    text(json, "villain_uid", op.villain_uid);
    integer(json, "encounters", op.encounters);
    integer(json, "variation", op.variation);
    json.end_object();
}

// The reference's {"kind": "cleanse", ..., **report}: the report's kind keeps the first
// key's position and replaces its value.
void history_operation(JsonWriter& json, const CleanseOperation& op) {
    json.begin_object();
    integer(json, "api_version", op.api_version);
    text(json, "kind", op.kind);
    json.key("target");
    json.begin_object();
    if (op.target_corruption) integer(json, "corruption", *op.target_corruption);
    if (op.target_god_id) text(json, "god_id", *op.target_god_id);
    json.end_object();
    real(json, "power", op.power);
    if (op.kind == "god") {
        text(json, "god_id", op.god_id);
        flag(json, "departed", op.departed);
    } else {
        integer(json, "corruption", op.corruption);
        text(json, "god_id", op.god_id);
        json.key("removed");
        strings(json, op.removed);
        integer(json, "remaining", op.remaining);
        flag(json, "cleansed", op.cleansed);
    }
    json.end_object();
}

void history_operation(JsonWriter& json, const VisitationOperation& op) {
    json.begin_object();
    integer(json, "api_version", op.api_version);
    text(json, "kind", op.kind);
    integer(json, "visitation", op.visitation);
    text(json, "god_id", op.god_id);
    if (op.node) integer(json, "node", *op.node);
    if (op.wrath) real(json, "wrath", *op.wrath);
    if (op.variation) integer(json, "variation", *op.variation);
    json.end_object();
}

// A tick carries its steps and, verbatim, the edits and resolutions it applied; an age span
// carries only the whole ages it turned (its remainder records its own tick).
void history_operation(JsonWriter& json, const TimeOperationRecord& op) {
    json.begin_object();
    integer(json, "api_version", op.api_version);
    text(json, "kind", "time");
    text(json, "band", to_string(op.band));
    real(json, "elapsed_days", op.elapsed_days);
    if (op.ages) integer(json, "ages", *op.ages);
    real(json, "from_day", op.from_day);
    real(json, "to_day", op.to_day);
    if (op.steps) {
        integer(json, "steps", *op.steps);
        json.key("leyline_edits");
        json.begin_array();
        for (const LeylineEdit& e : op.leyline_edits) leyline_edit(json, e);
        json.end_array();
        json.key("resolutions");
        json.begin_array();
        for (const QuestResolution& r : op.resolutions) {
            json.begin_object();
            text(json, "quest_id", r.quest_id);
            text(json, "outcome", to_string(r.outcome));
            json.end_object();
        }
        json.end_array();
    }
    json.end_object();
}

void history_operation(JsonWriter& json, const QuestOperationRecord& op) {
    json.begin_object();
    integer(json, "api_version", op.api_version);
    text(json, "kind", "quest");
    json.key("actions");
    json.begin_array();
    for (const QuestActionEntry& a : op.actions) {
        json.begin_object();
        text(json, "quest_id", a.quest_id);
        text(json, "action", to_string(a.action));
        real(json, "day", a.day);
        json.end_object();
    }
    json.end_array();
    json.end_object();
}

void write_history(JsonWriter& json, const WorldHistory& h) {
    json.begin_object();
    integer(json, "version", h.version);
    json.key("ages");
    json.begin_array();
    for (const AgeRecord& a : h.ages) {
        json.begin_object();
        integer(json, "age", a.age);
        json.key("events");
        write_ruins(json, a.events);
        json.key("wars");
        json.begin_array();
        for (const WarRecord& w : a.wars) war_record(json, w);
        json.end_array();
        moon_record(json, a.moon);
        json.key("surviving_city_ids");
        strings(json, a.surviving_city_ids);
        json.key("new_city_ids");
        strings(json, a.new_city_ids);
        json.key("order");
        strings(json, age_order());
        json.end_object();
    }
    json.end_array();
    // Written once any API has changed a world after generation, as the reference writes it.
    if (!h.operations.empty()) {
        json.key("operations");
        json.begin_array();
        for (const HistoryOperation& operation : h.operations) {
            std::visit([&json](const auto& op) { history_operation(json, op); }, operation);
        }
        json.end_array();
        // In the words of the API that wrote it last.
        if (!h.replay.empty()) text(json, "replay", h.replay);
    }
    json.end_object();
}

}  // namespace

// ===========================================================================
// Entry points
// ===========================================================================

void write_college_fields(JsonWriter& json, const World& world) {
    // The reference's magic block carries `colleges` and a diagnostics row from the moment
    // it is written; the college pass fills them at stage 12. Before that pass the empty
    // list means "not yet", and the pending row says so rather than reading as "none".
    const bool placed = world.has_humans;
    json.key("colleges");
    json.begin_array();
    if (placed) {
        for (const College& c : world.colleges.colleges) {
            json.begin_object();
            text(json, "id", c.id);
            text(json, "kind", kCollegeInstitution);
            integer(json, "node", c.node);
            integer(json, "x", c.x);
            integer(json, "z", c.z);
            integer(json, "core_id", c.core_id);
            text(json, "population_profile", c.population_profile);
            text(json, "culture_id", c.culture_id);
            real(json, "density", c.density);
            real(json, "hazard", c.hazard);
            real(json, "suitability", c.suitability);
            json.key("access_nodes");
            ints(json, c.access_nodes);
            real(json, "access_cost", c.access_cost);
            // A constant in the reference, not a field of College.
            text(json, "reason",
                 "Strong magic within population mutation, slope, climate, freshwater, flood and suitability "
                 "limits; reachable from a city.");
            json.end_object();
        }
    }
    json.end_array();

    json.key("diagnostics");
    json.begin_array();
    json.begin_object();
    text(json, "institution", kCollegeInstitution);
    if (placed) {
        const CollegeDiagnostic& d = world.colleges.diagnostic;
        integer(json, "placed", d.placed);
        integer(json, "wanted", d.wanted);
        integer(json, "candidates", d.candidates);
        integer(json, "sources", d.sources);
        text(json, "source_kind", kCollegeSourceKind);
        text(json, "reason", d.reason);
    } else {
        integer(json, "placed", 0);
        integer(json, "wanted", world.effective.college_count);
        integer(json, "candidates", 0);
        integer(json, "sources", 0);
        text(json, "source_kind", kCollegeSourceKind);
        text(json, "reason", "the college pass has not run at this phase, so nothing was evaluated");
    }
    json.end_object();
    json.end_array();

    if (placed) {
        real(json, "college_spacing_m", world.colleges.spacing_m);
        text(json, "college_method",
             "Colleges share the selected population safety limit; no protective ward discount. Sites may be "
             "fewer than requested, and magic.diagnostics says per institution how many were asked for, how many "
             "cells were within a city's support reach, how many of those cleared every limit, and what refused "
             "the rest. Institutions have no separate population or supply demand yet.");
    }
}

void write_route_trade_fields(JsonWriter& json, const World& world, std::size_t route_index) {
    if (!world.has_nomads || !ran(world, "nomad_effects")) return;
    for (const NomadCaravanTrade& t : world.nomad_effects.caravan_trade) {
        if (t.route_index != route_index) continue;
        json.key("caravan_riders");
        strings(json, t.riders);
        real(json, "caravan_throughput", t.throughput);
        return;
    }
}

void write_site_age_fields(JsonWriter& json, const World& world, std::size_t site_index) {
    if (site_index >= world.settlements.sites.size()) return;
    const SettlementSite& s = world.settlements.sites[site_index];
    // Both keys appear only once an age has given the city something to record: a war it
    // fought, or the victor it now serves.
    if (!s.war_history.empty()) war_history(json, s.war_history);
    if (!s.vassal_of.empty()) text(json, "vassal_of", s.vassal_of);
}

void write_site_society_fields(JsonWriter& json, const World& world, std::size_t site_index) {
    // The society pass writes the traits onto every site once the economy half runs.
    if (!world.has_humans || !world.society.has_economy) return;
    if (site_index >= world.settlements.sites.size()) return;
    json.key("community_traits");
    strings(json, world.settlements.sites[site_index].community);
}

void write_late_blocks(JsonWriter& json, const World& world) {
    if (world.has_humans) {
        json.key("humans");
        write_humans(json, world);
        json.key("seasonal_food");
        write_seasonal_food(json, world.seasonal_food);
        if (world.society.ran) {
            json.key("sky");
            write_sky(json, world.society.sky);
        }
        if (world.society.has_economy) {
            json.key("fisheries");
            write_fisheries(json, world.society.fisheries);
            json.key("transport");
            write_transport(json, world.society.transport);
            json.key("world_economy");
            write_world_economy(json, world.society.world_economy);
        }
    }

    // Stage 13 writes the threat between the nests and the faiths; every age turn rewrites it.
    if (world.has_threat_assessments) {
        json.key("threat_assessments");
        write_threat_assessments(json, world.threat_assessments);
    }

    if (world.has_religion) {
        json.key("religion");
        write_religion(json, world.religion);
    }

    // The ages. `ruins` and `history` exist from the first stage in the reference, empty
    // until an age turns, so they are written for every world.
    json.key("ruins");
    write_ruins(json, world.ruins);
    json.key("history");
    write_history(json, world.history);
    if (world.has_villains) {
        json.key("villains");
        write_villains(json, world.villains);
    }

    if (world.has_plans) {
        json.key("city_plans");
        write_city_plans(json, world.city_plans);
        if (world.has_humans) {
            json.key("hamlet_plans");
            write_hamlet_plans(json, world.hamlet_plans);
            json.key("castle_plans");
            write_castle_plans(json, world.castle_plans);
        }
    }

    if (world.has_nomads) {
        json.key("nomads");
        write_nomads(json, world);
        // Assigned only when the pass had candidates, as the reference assigns it.
        if (ran(world, "nomad_effects") && world.nomad_effects.assign_settlement_candidates) {
            json.key("settlement_candidates");
            write_settlement_candidates(json, world.nomad_effects);
        }
    }
    if (world.has_beast_movements) {
        json.key("beast_movements");
        write_beast_movements(json, world.beast_movements);
    }
    if (ran(world, "encounters")) {
        json.key("encounters");
        write_encounters(json, world.encounters);
    }

    if (world.has_heroes) {
        json.key("heroes");
        write_heroes(json, world.heroes);
        if (ran(world, "story_web")) {
            json.key("story_web");
            write_story_web(json, world.story_web);
        }
    }
    if (world.has_npcs) {
        json.key("npcs");
        write_npcs(json, world.npcs);
    }
    if (world.has_key_locations) {
        json.key("key_locations");
        write_key_locations(json, world.key_locations);
        // The reference yields no plan block at all when the locations failed; a plan pass
        // that failed on its own is a reported failure.
        if (world.key_locations.ok) {
            json.key("key_location_plans");
            write_key_location_plans(json, world.key_location_plans);
        }
    }
}

}  // namespace fwg::serialize_detail
