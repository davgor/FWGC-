#include "fwg/request_json.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_map>

#include "fwg/capabilities.hpp"

namespace fwg {
namespace {

// Python's repr of a JSON value, for messages: 'text', True, None, 1.0.
std::string py_repr(const JsonValue& value) {
    switch (value.type()) {
        case JsonValue::Type::Null: return "None";
        case JsonValue::Type::Bool: return value.as_bool() ? "True" : "False";
        case JsonValue::Type::Float: return python_float_repr(value.as_float());
        case JsonValue::Type::String: {
            std::string out = "'";
            for (const char c : value.as_string()) {
                if (c == '\'' || c == '\\') out.push_back('\\');
                out.push_back(c);
            }
            return out + "'";
        }
        default: {
            JsonDumpOptions options;
            options.ensure_ascii = false;
            return dump_json(value, options);
        }
    }
}

// The received value exactly as JSON, and as a double where it is a finite number.
void set_received(PatchRefusal& r, const JsonValue& received) {
    JsonDumpOptions options;
    options.ensure_ascii = false;
    try {
        r.received_json = dump_json(received, options);
    } catch (const JsonError&) {
        r.received_json = dump_json(received);  // a lone surrogate: fall back to the escaped spelling
    }
    const std::optional<double> number = received.is_number() ? received.to_double() : std::nullopt;
    if (number && std::isfinite(*number)) r.received = *number;
}

std::string number_text(double v) {
    if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 1e15) return std::to_string(static_cast<long long>(v));
    return python_float_repr(v);
}

std::string json_text(const JsonValue& value) {
    JsonDumpOptions options;
    options.ensure_ascii = false;
    return dump_json(value, options);
}

// --- difflib, over code points ------------------------------------------------------------

std::u32string code_points(std::string_view s) {
    std::u32string out;
    std::size_t i = 0;
    while (i < s.size()) {
        const auto lead = static_cast<unsigned char>(s[i]);
        std::size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        if (i + length > s.size()) length = s.size() - i;
        char32_t cp = length == 1 ? lead : length == 2 ? (lead & 0x1Fu) : length == 3 ? (lead & 0x0Fu) : (lead & 0x07u);
        for (std::size_t k = 1; k < length; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3Fu);
        out.push_back(cp);
        i += length;
    }
    return out;
}

// CPython's difflib.SequenceMatcher with isjunk=None and autojunk=True, `a` against a
// fixed `b`: the three ratios get_close_matches reads.
class SequenceMatcher {
public:
    explicit SequenceMatcher(std::u32string b) : b_(std::move(b)) {
        for (std::size_t j = 0; j < b_.size(); ++j) b2j_[b_[j]].push_back(j);
        // Autojunk: an element occurring in more than 1% of a long b is "popular" and is
        // dropped from the index, exactly as CPython does from 200 elements on.
        if (b_.size() >= 200) {
            const std::size_t ntest = b_.size() / 100 + 1;
            for (auto it = b2j_.begin(); it != b2j_.end();) {
                if (it->second.size() > ntest) {
                    it = b2j_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (const char32_t c : b_) ++fullbcount_[c];
    }

    void set_a(std::u32string a) { a_ = std::move(a); }

    double real_quick_ratio() const {
        return calculate(std::min(a_.size(), b_.size()), a_.size() + b_.size());
    }

    double quick_ratio() const {
        std::unordered_map<char32_t, long> avail;
        std::size_t matches = 0;
        for (const char32_t c : a_) {
            long numb = 0;
            const auto found = avail.find(c);
            if (found != avail.end()) {
                numb = found->second;
            } else {
                const auto full = fullbcount_.find(c);
                numb = full != fullbcount_.end() ? full->second : 0;
            }
            avail[c] = numb - 1;
            if (numb > 0) ++matches;
        }
        return calculate(matches, a_.size() + b_.size());
    }

    double ratio() const {
        std::size_t matches = 0;
        std::vector<std::array<std::size_t, 4>> queue{{0, a_.size(), 0, b_.size()}};
        while (!queue.empty()) {
            const auto [alo, ahi, blo, bhi] = queue.back();
            queue.pop_back();
            const auto [i, j, k] = longest_match(alo, ahi, blo, bhi);
            if (k != 0) {
                matches += k;
                if (alo < i && blo < j) queue.push_back({alo, i, blo, j});
                if (i + k < ahi && j + k < bhi) queue.push_back({i + k, ahi, j + k, bhi});
            }
        }
        return calculate(matches, a_.size() + b_.size());
    }

private:
    static double calculate(std::size_t matches, std::size_t length) {
        return length != 0 ? 2.0 * static_cast<double>(matches) / static_cast<double>(length) : 1.0;
    }

    std::array<std::size_t, 3> longest_match(std::size_t alo, std::size_t ahi, std::size_t blo, std::size_t bhi) const {
        std::size_t besti = alo, bestj = blo, bestsize = 0;
        std::unordered_map<std::size_t, std::size_t> j2len;
        for (std::size_t i = alo; i < ahi; ++i) {
            std::unordered_map<std::size_t, std::size_t> next;
            const auto found = b2j_.find(a_[i]);
            if (found != b2j_.end()) {
                for (const std::size_t j : found->second) {
                    if (j < blo) continue;
                    if (j >= bhi) break;
                    std::size_t previous = 0;
                    if (j > 0) {
                        const auto p = j2len.find(j - 1);
                        if (p != j2len.end()) previous = p->second;
                    }
                    const std::size_t k = next[j] = previous + 1;
                    if (k > bestsize) {
                        besti = i - k + 1;
                        bestj = j - k + 1;
                        bestsize = k;
                    }
                }
            }
            j2len = std::move(next);
        }
        // No element is junk, so only the first pair of CPython's extension loops can move.
        while (besti > alo && bestj > blo && a_[besti - 1] == b_[bestj - 1]) {
            --besti;
            --bestj;
            ++bestsize;
        }
        while (besti + bestsize < ahi && bestj + bestsize < bhi && a_[besti + bestsize] == b_[bestj + bestsize]) {
            ++bestsize;
        }
        return {besti, bestj, bestsize};
    }

    std::u32string a_;
    std::u32string b_;
    std::map<char32_t, std::vector<std::size_t>> b2j_;
    std::unordered_map<char32_t, long> fullbcount_;
};

std::string ascii_fold(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::vector<std::string> json_texts(const std::vector<std::string>& strings) {
    std::vector<std::string> out;
    for (const std::string& s : strings) out.push_back(json_text(JsonValue(s)));
    return out;
}

// The request root's three fields, and nothing else.
constexpr std::string_view kRootFields[] = {"overrides", "recipe_version", "seed"};

// --- Request fields by published control name ----------------------------------------------

int to_int(const std::string& field, const JsonValue& value, const JsonValue& definition) {
    const std::int64_t v = value.as_int();
    if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max()) {
        PatchRefusal r = refusal::out_of_range(field, value, definition.at("min").to_double(),
                                               static_cast<double>(std::numeric_limits<int>::max()), "integer",
                                               "");
        r.message = field + " " + py_repr(value) + " is published up to " + json_text(definition.at("max")) +
                    " but this producer holds it as a 32-bit integer; values above 2147483647 are refused.";
        throw PatchError(std::move(r));
    }
    return static_cast<int>(v);
}

// Applies one validated override to the Request. Returns false for a name it does not know,
// which is a catalogue/port disagreement rather than a caller error.
bool apply_config_field(Request& request, const std::string& name, const JsonValue& value, const JsonValue& definition) {
    const auto real = [&]() { return *value.to_double(); };
    const auto integer = [&]() { return to_int(name, value, definition); };
    static const std::unordered_map<std::string_view, int> kind = {
        {"size", 0}, {"phase", 1}, {"world_size", 2}, {"population_profile", 3}, {"shape", 4}, {"tectonics", 5},
        {"plate_count", 6}, {"layout_variation", 7}, {"detail_variation", 8}, {"octaves", 9},
        {"erosion_passes", 10}, {"magic_enabled", 11}, {"rain_passes", 12}, {"settlement_count", 13},
        {"hamlets_per_core", 14}, {"fortress_count", 15}, {"college_count", 16},
    };
    const auto found = kind.find(name);
    if (found != kind.end()) {
        switch (found->second) {
            case 0: request.size = integer(); return true;
            case 1: request.phase = integer(); return true;
            case 2: request.world_size = parse_world_size(value.as_string()); return true;
            case 3: request.population_profile = value.as_string(); return true;
            case 4: return true;  // "globe", the only value the catalogue admits, and the only shape there is
            case 5: return true;  // pinned at 1: tectonics always run
            case 6: request.plate_count = integer(); return true;
            case 7: request.layout_variation = integer(); return true;
            case 8: request.detail_variation = integer(); return true;
            case 9: request.octaves = integer(); return true;
            case 10: request.erosion_passes = integer(); return true;
            case 11: request.magic_enabled = integer(); return true;
            case 12: request.rain_passes = integer(); return true;
            case 13: request.settlement_count = integer(); return true;
            case 14: request.hamlets_per_core = integer(); return true;
            case 15: request.fortress_count = integer(); return true;
            case 16: request.college_count = integer(); return true;
            default: break;
        }
    }
    static const std::unordered_map<std::string_view, std::optional<double> Request::*> real_fields = {
        {"world_scale", &Request::world_scale},
        {"globe_radius", &Request::globe_radius},
        {"tectonic_relief", &Request::tectonic_relief},
        {"amplitude", &Request::amplitude},
        {"wavelength", &Request::wavelength},
        {"ridge", &Request::ridge},
        {"sea_level", &Request::sea_level},
        {"radius", &Request::radius},
        {"crust_bias", &Request::crust_bias},
        {"belt_width", &Request::belt_width},
        {"mountain_detail", &Request::mountain_detail},
        {"orogeny", &Request::orogeny},
        {"erosion_strength", &Request::erosion_strength},
        {"wind_bearing", &Request::wind_bearing},
        {"rain_strength", &Request::rain_strength},
        {"temperature_offset", &Request::temperature_offset},
        {"moisture_bias", &Request::moisture_bias},
        {"road_max_grade", &Request::road_max_grade},
        {"bridge_cost", &Request::bridge_cost},
        {"human_magic_limit", &Request::human_magic_limit},
        {"stubbornness", &Request::stubbornness},
        {"settlement_spacing", &Request::settlement_spacing},
        {"support_reach", &Request::support_reach},
        {"culture_link_cost", &Request::culture_link_cost},
        {"river_threshold_km2", &Request::river_threshold_km2},
        {"circumference_km", &Request::circumference_km},
        {"relief_m", &Request::relief_m},
    };
    const auto field = real_fields.find(name);
    if (field == real_fields.end()) return false;
    // The reference reads relief as `relief or DEFAULT_RELIEF_M`: zero keeps the default.
    if (name == "relief_m" && real() == 0.0) return true;
    request.*(field->second) = real();
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------------------
// The refusal constructors.

namespace refusal {

PatchRefusal wrong_type(std::string field, const JsonValue& received, std::string_view expected_type) {
    PatchRefusal r;
    r.kind = "wrong_type";
    const std::string article = expected_type == "integer" ? "an integer"
                                : expected_type == "string" ? "text"
                                : expected_type == "object" ? "an object"
                                : expected_type == "array"  ? "an array"
                                                            : "a finite number";
    r.message = field + " must be " + article + "; received " + py_repr(received) + ".";
    r.field = std::move(field);
    set_received(r, received);
    r.expected_type = expected_type;
    return r;
}

PatchRefusal out_of_range(std::string field, const JsonValue& received, std::optional<double> min,
                          std::optional<double> max, std::string_view expected_type, std::string_view units) {
    PatchRefusal r;
    r.kind = "out_of_range";
    r.field = field;
    set_received(r, received);
    r.expected_type = expected_type;
    r.min = min;
    r.max = max;
    r.units = units;
    const auto bound = [](const std::optional<double>& v) { return v ? number_text(*v) : std::string("None"); };
    if (min && max && *min == *max) {
        r.message = field + " is pinned at " + bound(min) + " and rejects every other value; received " +
                    py_repr(received) + ".";
        r.suggestion = "none";
        return r;
    }
    // "seed must be 0..4294967295 seed" is a stutter: a unit that repeats the field's own
    // name is dropped from the sentence and kept in `expected`.
    bool spoken = !units.empty();
    for (std::size_t start = 0; spoken && start <= field.size();) {
        const std::size_t end = std::min(field.find('_', start), field.size());
        if (std::string_view(field).substr(start, end - start) == units) spoken = false;
        start = end + 1;
    }
    r.message = field + " must be " + bound(min) + ".." + bound(max) + (spoken ? " " + std::string(units) : "") +
                "; received " + py_repr(received) + ".";
    r.suggestion = "clamp";
    if (r.received) {
        double clamped = *r.received;
        if (min) clamped = std::max(clamped, *min);
        if (max) clamped = std::min(clamped, *max);
        if (expected_type == "integer") clamped = std::nearbyint(clamped);
        r.suggestion_value = clamped;
    } else {
        r.suggestion = "none";
    }
    return r;
}

PatchRefusal invalid_choice(std::string field, const JsonValue& received, const std::vector<std::string>& choices) {
    PatchRefusal r;
    r.kind = "invalid_choice";
    std::string listed;
    for (const std::string& c : choices) listed += (listed.empty() ? "'" : ", '") + c + "'";
    r.message = field + " must be one of " + listed + "; received " + py_repr(received) + ".";
    r.field = std::move(field);
    set_received(r, received);
    r.expected_type = "string";
    r.choices_json = json_texts(choices);
    if (received.is_string()) {
        const std::vector<std::string> guesses = nearest(received.as_string(), choices);
        if (!guesses.empty()) {
            r.suggestion = "choose";
            r.candidates_json = json_texts(guesses);
        }
    }
    return r;
}

namespace {
PatchRefusal version_refusal(std::string_view kind, std::string field, const JsonValue& received,
                             const std::vector<JsonValue>& supported, std::string_view verb) {
    PatchRefusal r;
    r.code = "UNSUPPORTED_VERSION";
    r.kind = kind;
    std::string listed;
    for (const JsonValue& v : supported) listed += (listed.empty() ? "" : ", ") + json_text(v);
    r.message = field + " " + py_repr(received) + std::string(verb) + (listed.empty() ? "nothing" : listed) +
                (kind == "retired_version" ? ". Regenerate rather than migrating." : ".");
    r.field = std::move(field);
    set_received(r, received);
    for (const JsonValue& v : supported) r.choices_json.push_back(json_text(v));
    if (!supported.empty()) {
        r.suggestion = "choose";
        r.candidates_json = r.choices_json;
    }
    r.retry = false;
    return r;
}
}  // namespace

PatchRefusal retired_version(std::string field, const JsonValue& received, const std::vector<JsonValue>& supported) {
    return version_refusal("retired_version", std::move(field), received, supported,
                           " is retired; this producer accepts ");
}

PatchRefusal unsupported_api(std::string field, const JsonValue& received, const std::vector<JsonValue>& supported) {
    return version_refusal("unsupported_api", std::move(field), received, supported,
                           " is not implemented; this producer speaks ");
}

PatchRefusal unknown_field(std::string name, const std::vector<std::string>& candidates, std::string_view noun) {
    PatchRefusal r;
    r.kind = "unknown_field";
    const std::vector<std::string> guesses = nearest(name, candidates);
    r.message = "No " + std::string(noun) + " named '" + name + "'.";
    if (!guesses.empty()) {
        std::string listed;
        for (const std::string& g : guesses) listed += (listed.empty() ? "'" : ", '") + g + "'";
        r.message += " Did you mean " + listed + "?";
    } else if (noun == "control") {
        r.message += " Call describe_controls to list what exists.";
    } else {
        std::vector<std::string> known = candidates;
        std::sort(known.begin(), known.end());
        if (known.empty()) {
            r.message += " Nothing of that kind exists in this world.";
        } else {
            std::string listed;
            for (std::size_t k = 0; k < known.size() && k < 8; ++k) listed += (k ? ", '" : "'") + known[k] + "'";
            r.message += " Known " + std::string(noun) + "s: " + listed + ".";
        }
    }
    r.field = std::move(name);
    r.received_json = "null";  // the reference sends received: None for an unknown name
    r.candidates_json = json_texts(guesses);
    r.suggestion = guesses.empty() ? "none" : "did_you_mean";
    return r;
}

PatchRefusal cross_field(std::string message, std::vector<std::string_view> fields) {
    PatchRefusal r;
    r.kind = "cross_field";
    r.message = std::move(message);
    r.fields = std::move(fields);
    return r;
}

PatchRefusal over_capacity(std::string field, const JsonValue& received, double limit, std::string_view noun) {
    PatchRefusal r;
    r.code = "STATE_CAPACITY";
    r.kind = "over_capacity";
    r.message = field + " " + py_repr(received) + " is above the " + std::string(noun) + " maximum of " +
                number_text(limit) + ".";
    r.field = std::move(field);
    set_received(r, received);
    r.max = limit;
    r.suggestion = "clamp";
    r.suggestion_value = limit;
    return r;
}

PatchRefusal misplaced(std::string field, const JsonValue& received, std::string message) {
    PatchRefusal r;
    r.kind = "misplaced";
    r.message = std::move(message);
    r.field = std::move(field);
    set_received(r, received);
    return r;
}

PatchRefusal malformed(std::string message, const JsonValue& received) {
    PatchRefusal r;
    r.kind = "malformed";
    r.message = std::move(message);
    r.received_json = json_text(JsonValue(received.type_name()));
    return r;
}

}  // namespace refusal

// ---------------------------------------------------------------------------------------

std::vector<std::string> close_matches(std::string_view word, const std::vector<std::string>& possibilities,
                                       std::size_t n, double cutoff) {
    if (n == 0 || !(cutoff >= 0.0 && cutoff <= 1.0)) throw std::invalid_argument("n must be > 0 and cutoff in [0, 1]");
    SequenceMatcher matcher(code_points(word));
    std::vector<std::pair<double, std::string>> scored;
    for (const std::string& candidate : possibilities) {
        matcher.set_a(code_points(candidate));
        if (matcher.real_quick_ratio() >= cutoff && matcher.quick_ratio() >= cutoff) {
            const double score = matcher.ratio();
            if (score >= cutoff) scored.emplace_back(score, candidate);
        }
    }
    // heapq.nlargest over (score, string): highest score first, then the larger string.
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a > b; });
    std::vector<std::string> out;
    for (std::size_t k = 0; k < scored.size() && k < n; ++k) out.push_back(scored[k].second);
    return out;
}

std::vector<std::string> nearest(std::string_view name, std::vector<std::string> candidates) {
    std::sort(candidates.begin(), candidates.end());
    const std::string lowered = ascii_fold(name);
    std::vector<std::string> prefixed;
    std::vector<std::string> contained;
    for (const std::string& k : candidates) {
        const std::string folded = ascii_fold(k);
        if (folded.rfind(lowered, 0) == 0) {
            prefixed.push_back(k);
        } else if (folded.find(lowered) != std::string::npos) {
            contained.push_back(k);
        }
    }
    std::vector<std::string> out = prefixed;
    out.insert(out.end(), contained.begin(), contained.end());
    for (const std::string& k : close_matches(name, candidates, 5, 0.6)) {
        if (std::find(out.begin(), out.end(), k) == out.end()) out.push_back(k);
    }
    if (out.size() > 5) out.resize(5);
    return out;
}

JsonValue refusal_document(const PatchRefusal& r, bool detail) {
    JsonValue doc = JsonValue::object();
    doc.set("schema", std::string(kFailureSchema));
    doc.set("schema_version", kFailureSchemaVersion);
    doc.set("code", std::string(r.code));
    doc.set("message", r.message);
    doc.set("field", r.field.empty() ? JsonValue() : JsonValue(r.field));
    if (!detail) return doc;

    const auto number = [&](double v) {
        return r.expected_type == "integer" && v == std::floor(v) && std::fabs(v) < 9e15 ? JsonValue(static_cast<long long>(v))
                                                                                          : JsonValue(v);
    };
    const auto optional_number = [&](const std::optional<double>& v) { return v ? number(*v) : JsonValue(); };
    const auto parsed = [](const std::vector<std::string>& texts) {
        JsonValue out = JsonValue::array();
        for (const std::string& t : texts) out.push_back(parse_json(t));
        return out;
    };

    if (!r.received_json.empty()) {
        doc.set("received", parse_json(r.received_json));
    } else {
        doc.set("received", r.received ? number(*r.received) : JsonValue());
    }

    JsonValue expected = JsonValue::object();
    const std::string_view kind = r.kind;
    if (kind == "wrong_type") {
        expected.set("type", std::string(r.expected_type));
    } else if (kind == "out_of_range") {
        expected.set("type", std::string(r.expected_type));
        expected.set("min", optional_number(r.min));
        expected.set("max", optional_number(r.max));
        if (r.min && r.max && *r.min == *r.max) {
            expected.set("pinned", true);
        } else {
            expected.set("units", r.units.empty() ? JsonValue() : JsonValue(std::string(r.units)));
        }
    } else if (kind == "cross_field") {
        JsonValue fields = JsonValue::array();
        for (const std::string_view f : r.fields) fields.push_back(std::string(f));
        expected.set("fields", std::move(fields));
    } else if (kind == "refused_by_world") {
        expected.set("refused_by", "world state");
    } else if (kind == "missing_block") {
        expected.set("requires_block", std::string(r.requires_block));
    } else if (kind == "retired_version" || kind == "unsupported_api") {
        expected.set("choices", parsed(r.choices_json));
    } else if (kind == "invalid_choice") {
        expected.set("type", r.expected_type.empty() ? std::string("string") : std::string(r.expected_type));
        expected.set("choices", parsed(r.choices_json));
    } else if (kind == "unknown_field") {
        expected.set("kind", "one_of_catalogue");
        expected.set("candidates", parsed(r.candidates_json));
    } else if (kind == "over_capacity") {
        expected.set("max", optional_number(r.max));
    } else if (kind == "misplaced") {
        expected.set("location", "request root");
    } else {
        expected = JsonValue();  // malformed: the reference sends expected: None
    }
    doc.set("expected", std::move(expected));

    JsonValue suggestion = JsonValue::object();
    const std::string_view how = r.suggestion;
    if (how == "clamp" && r.suggestion_value) {
        suggestion.set("kind", "clamp");
        suggestion.set("value", number(*r.suggestion_value));
    } else if (how == "choose" && !r.candidates_json.empty()) {
        suggestion.set("kind", "choose");
        suggestion.set("value", parse_json(r.candidates_json.front()));
        suggestion.set("candidates", parsed(r.candidates_json));
    } else if (how == "did_you_mean" && !r.candidates_json.empty()) {
        suggestion.set("kind", "did_you_mean");
        suggestion.set("candidates", parsed(r.candidates_json));
    } else {
        suggestion.set("kind", "none");
    }
    doc.set("suggestion", std::move(suggestion));
    doc.set("retry", r.retry);
    return doc;
}

// ---------------------------------------------------------------------------------------

Request request_from_json(const JsonValue& body) {
    if (!body.is_object()) {
        throw PatchError(refusal::malformed(
            "A generate request is an object with seed, recipe_version and optional overrides.", body));
    }
    std::vector<std::string> extra;
    for (const auto& member : body.as_object()) {
        if (std::find(std::begin(kRootFields), std::end(kRootFields), member.first) == std::end(kRootFields)) {
            extra.push_back(member.first);
        }
    }
    if (!extra.empty()) {
        std::sort(extra.begin(), extra.end());
        throw PatchError(refusal::unknown_field(extra.front(), {"seed", "recipe_version", "overrides"}, "request field"));
    }

    const JsonValue* recipe = body.find("recipe_version");
    if (recipe != nullptr && !(recipe->is_int64() && recipe->as_int() == 3)) {
        throw PatchError(refusal::retired_version("recipe_version", *recipe, {JsonValue(3)}));
    }

    Request request;
    const JsonValue* seed = body.find("seed");
    if (seed != nullptr) {
        if (!seed->is_integer()) throw PatchError(refusal::wrong_type("seed", *seed, "integer"));
        if (!seed->is_int64() || seed->as_int() < 0 || seed->as_int() > 4294967295LL) {
            throw PatchError(refusal::out_of_range("seed", *seed, 0.0, 4294967295.0, "integer", "seed"));
        }
        request.seed = static_cast<std::uint32_t>(seed->as_int());
    }

    static const JsonValue kNoOverrides = JsonValue::object();
    const JsonValue* overrides = body.find("overrides");
    if (overrides == nullptr) overrides = &kNoOverrides;
    if (!overrides->is_object()) throw PatchError(refusal::wrong_type("overrides", *overrides, "object"));

    std::vector<std::string> unknown;
    for (const auto& member : overrides->as_object()) {
        if (control_definition(member.first) == nullptr) unknown.push_back(member.first);
    }
    if (!unknown.empty()) {
        std::sort(unknown.begin(), unknown.end());
        throw PatchError(refusal::unknown_field(unknown.front(), control_names()));
    }
    if (const JsonValue* inner = overrides->find("seed")) {
        throw PatchError(refusal::misplaced("seed", *inner,
                                            "Supply seed at the top level of the request, not inside overrides."));
    }

    for (const auto& [name, value] : overrides->as_object()) {
        const JsonValue& definition = *control_definition(name);
        const std::string& type = definition.at("type").as_string();
        if (type == "string") {
            if (!value.is_string()) throw PatchError(refusal::wrong_type(name, value, "string"));
            if (const JsonValue* choices = definition.find("choices")) {
                std::vector<std::string> vocabulary;
                bool listed = false;
                for (const JsonValue& c : choices->as_array()) {
                    vocabulary.push_back(c.as_string());
                    listed = listed || c.as_string() == value.as_string();
                }
                if (!listed) throw PatchError(refusal::invalid_choice(name, value, vocabulary));
            }
        } else {
            const std::optional<double> number = value.to_double();
            // Neither a bool nor a non-finite float is a number here; an integer too large
            // for a double is out of any published range.
            if (!value.is_number() || (value.is_float() && !std::isfinite(value.as_float()))) {
                throw PatchError(refusal::wrong_type(name, value, type));
            }
            if (type == "integer" && !value.is_integer()) throw PatchError(refusal::wrong_type(name, value, type));
            const std::optional<double> low = definition.find("min") ? definition.at("min").to_double() : std::nullopt;
            const std::optional<double> high = definition.find("max") ? definition.at("max").to_double() : std::nullopt;
            const std::string_view units =
                definition.find("units") ? std::string_view(definition.at("units").as_string()) : std::string_view();
            // Exact: every published bound and every integer that could meet one is well
            // inside the doubles' exact-integer range.
            if (!number || (low && *number < *low) || (high && *number > *high)) {
                throw PatchError(refusal::out_of_range(name, value, low, high, type, units));
            }
        }
    }

    // Apply in the request's own order; a world option goes to the option table.
    for (const auto& [name, value] : overrides->as_object()) {
        if (option_by_name(name)) {
            request.options[name] = *value.to_double();
            continue;
        }
        if (!apply_config_field(request, name, value, *control_definition(name))) {
            throw std::logic_error("control '" + name + "' is published but this producer has no field for it");
        }
    }

    // A relief budget alone cannot size a world: it needs a width to be a relief OF. The
    // width falls back to the world_size preset unless globe_radius was given directly, so
    // this is refused only when the radius was set and no width was.
    const JsonValue* relief = overrides->find("relief_m");
    const JsonValue* width = overrides->find("circumference_km");
    const bool has_width = width != nullptr && *width->to_double() > 0.0;
    if (relief != nullptr && *relief->to_double() > 0.0 && !has_width && overrides->find("globe_radius") != nullptr) {
        throw PatchError(refusal::cross_field(
            "relief_m needs circumference_km; a relief budget alone cannot size a world. Give both, or neither and "
            "take the world_size preset.",
            {"relief_m", "circumference_km"}));
    }
    return request;
}

}  // namespace fwg
