#include "fwg/request_error.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>

namespace fwg {
namespace {

// Python's `%r` of a refused value: None, an integer, or a string's repr.
std::string repr_of(const RefusalValue& value) {
    if (const auto* number = std::get_if<long long>(&value)) return std::to_string(*number);
    if (const auto* text = std::get_if<std::string>(&value)) return py_repr(*text);
    return "None";
}

std::string bound_text(const std::optional<long long>& bound) {
    return bound ? std::to_string(*bound) : std::string("None");
}

// `str.casefold`, for the ASCII the catalogues are spelled in. Bytes past ASCII are left
// alone, which is where this and the reference part company.
std::string folded(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::string joined_reprs(std::span<const std::string> items) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) out += ", ";
        out += py_repr(items[i]);
    }
    return out;
}

// One longest common block of a[alo:ahi] and b[blo:bhi], as difflib's find_longest_match
// finds it with no junk: the first block of maximal length when the ends are walked in
// (i, j) order, which is what breaks a tie.
struct Block {
    std::size_t i = 0;
    std::size_t j = 0;
    std::size_t k = 0;
};

Block longest_block(std::string_view a, std::string_view b, std::size_t alo, std::size_t ahi, std::size_t blo,
                    std::size_t bhi) {
    Block best{alo, blo, 0};
    // run[j + 1] is the length of the common run ending at (i, j); index 0 stands for j = -1
    // and index blo for j = blo - 1, neither of which a row in this window ever writes.
    std::vector<std::size_t> previous(b.size() + 1, 0);
    std::vector<std::size_t> current(b.size() + 1, 0);
    for (std::size_t i = alo; i < ahi; ++i) {
        std::fill(current.begin(), current.end(), std::size_t{0});
        for (std::size_t j = blo; j < bhi; ++j) {
            if (a[i] != b[j]) continue;
            const std::size_t k = previous[j] + 1;
            current[j + 1] = k;
            if (k > best.k) best = {i + 1 - k, j + 1 - k, k};
        }
        std::swap(previous, current);
    }
    // difflib then widens the block over matching neighbours. With no junk the block is
    // already maximal and these never move it; kept so the procedure is the reference's.
    while (best.i > alo && best.j > blo && a[best.i - 1] == b[best.j - 1]) {
        --best.i;
        --best.j;
        ++best.k;
    }
    while (best.i + best.k < ahi && best.j + best.k < bhi && a[best.i + best.k] == b[best.j + best.k]) ++best.k;
    return best;
}

// difflib.get_close_matches(word, pool, n=5, cutoff=0.6): best ratio first, and on a tie the
// LARGER string first, because heapq.nlargest compares (ratio, string) tuples.
std::vector<std::string> close_matches(const std::string& word, std::span<const std::string> pool) {
    std::vector<std::pair<double, std::string>> scored;
    for (const std::string& candidate : pool) {
        // seq1 is the candidate and seq2 the word, as get_close_matches sets them. Its two
        // cheaper filters are upper bounds on the ratio, so testing the ratio alone admits
        // exactly the same set.
        const double ratio = sequence_ratio(candidate, word);
        if (ratio >= 0.6) scored.emplace_back(ratio, candidate);
    }
    std::sort(scored.begin(), scored.end(), [](const auto& x, const auto& y) { return x > y; });
    std::vector<std::string> out;
    for (std::size_t i = 0; i < scored.size() && i < 5; ++i) out.push_back(scored[i].second);
    return out;
}

}  // namespace

RequestError::RequestError(RequestRefusal refusal)
    : std::invalid_argument(refusal.message), refusal_(std::move(refusal)) {}

double sequence_ratio(std::string_view a, std::string_view b) {
    std::size_t matched = 0;
    std::vector<std::array<std::size_t, 4>> queue{{0, a.size(), 0, b.size()}};
    while (!queue.empty()) {
        const auto [alo, ahi, blo, bhi] = queue.back();
        queue.pop_back();
        const Block block = longest_block(a, b, alo, ahi, blo, bhi);
        if (block.k == 0) continue;
        matched += block.k;
        if (alo < block.i && blo < block.j) queue.push_back({alo, block.i, blo, block.j});
        if (block.i + block.k < ahi && block.j + block.k < bhi) {
            queue.push_back({block.i + block.k, ahi, block.j + block.k, bhi});
        }
    }
    const std::size_t length = a.size() + b.size();
    return length == 0 ? 1.0 : 2.0 * static_cast<double>(matched) / static_cast<double>(length);
}

std::vector<std::string> nearest_names(const std::string& name, std::span<const std::string> candidates) {
    std::vector<std::string> pool(candidates.begin(), candidates.end());
    std::sort(pool.begin(), pool.end());
    const std::string lowered = folded(name);

    std::vector<std::string> prefixed;
    std::vector<std::string> contained;
    for (const std::string& k : pool) {
        if (folded(k).rfind(lowered, 0) == 0) prefixed.push_back(k);
    }
    const auto listed = [](const std::vector<std::string>& list, const std::string& k) {
        return std::find(list.begin(), list.end(), k) != list.end();
    };
    for (const std::string& k : pool) {
        if (folded(k).find(lowered) != std::string::npos && !listed(prefixed, k)) contained.push_back(k);
    }
    std::vector<std::string> out = prefixed;
    out.insert(out.end(), contained.begin(), contained.end());
    for (const std::string& k : close_matches(name, pool)) {
        if (!listed(prefixed, k) && !listed(contained, k)) out.push_back(k);
    }
    if (out.size() > 5) out.resize(5);
    return out;
}

std::string py_repr(std::string_view text) {
    const bool single = text.find('\'') != std::string_view::npos;
    const bool twin = text.find('"') != std::string_view::npos;
    const char quote = single && !twin ? '"' : '\'';
    std::string out(1, quote);
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '\\') {
            out += "\\\\";
        } else if (c == quote) {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else if (byte < 0x20 || byte == 0x7f) {
            std::array<char, 8> hex{};
            std::snprintf(hex.data(), hex.size(), "\\x%02x", static_cast<unsigned>(byte));
            out += hex.data();
        } else {
            out += c;
        }
    }
    out += quote;
    return out;
}

RequestRefusal wrong_type(std::string field, RefusalValue value, std::string_view type) {
    const std::string_view article = type == "integer" ? "an integer" : type == "string" ? "text" : "a finite number";
    RequestRefusal r;
    r.kind = "wrong_type";
    r.message = field + " must be " + std::string(article) + "; received " + repr_of(value) + ".";
    r.field = std::move(field);
    r.received = std::move(value);
    r.expected_type = type;
    return r;
}

RequestRefusal out_of_range(std::string field, long long value, std::optional<long long> min,
                            std::optional<long long> max, std::string_view units) {
    RequestRefusal r;
    r.kind = "out_of_range";
    r.received = value;
    r.expected_type = "integer";
    r.min = min;
    r.max = max;
    if (min && max && *min == *max) {
        r.message = field + " is pinned at " + std::to_string(*min) + " and rejects every other value; received " +
                    std::to_string(value) + ".";
        r.pinned = true;
        r.field = std::move(field);
        return r;
    }
    // A unit that merely repeats a word of the field's name carries nothing, so it stays out
    // of the sentence and in `units`.
    bool repeats = false;
    if (!units.empty()) {
        std::size_t start = 0;
        while (start <= field.size()) {
            const std::size_t end = std::min(field.find('_', start), field.size());
            if (std::string_view(field).substr(start, end - start) == units) repeats = true;
            start = end + 1;
        }
    }
    const std::string spoken = repeats ? std::string() : std::string(units);
    r.message = field + " must be " + bound_text(min) + ".." + bound_text(max) + (spoken.empty() ? "" : " " + spoken) +
                "; received " + std::to_string(value) + ".";
    long long clamped = value;
    if (min) clamped = std::max(clamped, *min);
    if (max) clamped = std::min(clamped, *max);
    r.units = std::string(units);
    r.suggestion = "clamp";
    r.suggestion_value = clamped;
    r.field = std::move(field);
    return r;
}

RequestRefusal invalid_choice(std::string field, const std::string& value, std::span<const std::string> choices) {
    RequestRefusal r;
    r.kind = "invalid_choice";
    r.message = field + " must be one of " + joined_reprs(choices) + "; received " + py_repr(value) + ".";
    r.received = value;
    r.expected_type = "string";
    for (const std::string& c : choices) r.choices.emplace_back(c);
    const std::vector<std::string> guesses = nearest_names(value, choices);
    if (!guesses.empty()) {
        r.suggestion = "choose";
        r.suggestion_value = guesses.front();
        for (const std::string& g : guesses) r.suggestion_candidates.emplace_back(g);
    }
    r.field = std::move(field);
    return r;
}

RequestRefusal unknown_field(const std::string& name, std::span<const std::string> candidates, std::string_view noun) {
    RequestRefusal r;
    r.kind = "unknown_field";
    const std::vector<std::string> guesses = nearest_names(name, candidates);
    r.message = "No " + std::string(noun) + " named " + py_repr(name) + ".";
    if (!guesses.empty()) {
        r.message += " Did you mean " + joined_reprs(guesses) + "?";
    } else {
        std::vector<std::string> known(candidates.begin(), candidates.end());
        std::sort(known.begin(), known.end());
        if (known.size() > 8) known.resize(8);
        r.message += known.empty() ? std::string(" Nothing of that kind exists in this world.")
                                   : " Known " + std::string(noun) + "s: " + joined_reprs(known) + ".";
    }
    r.field = name;
    r.one_of_catalogue = true;
    r.catalogue = guesses;
    if (!guesses.empty()) {
        r.suggestion = "did_you_mean";
        for (const std::string& g : guesses) r.suggestion_candidates.emplace_back(g);
    }
    return r;
}

RequestRefusal unsupported_api(std::string field, long long value, std::span<const long long> supported) {
    RequestRefusal r;
    r.code = "UNSUPPORTED_VERSION";
    r.kind = "unsupported_api";
    std::string spoken;
    for (std::size_t i = 0; i < supported.size(); ++i) {
        if (i > 0) spoken += ", ";
        spoken += std::to_string(supported[i]);
    }
    r.message = field + " " + std::to_string(value) + " is not implemented; this producer speaks " + spoken + ".";
    r.field = std::move(field);
    r.received = value;
    for (const long long s : supported) {
        r.choices.emplace_back(s);
        r.suggestion_candidates.emplace_back(s);
    }
    if (!supported.empty()) {
        r.suggestion = "choose";
        r.suggestion_value = supported.front();
    }
    r.retry = false;
    return r;
}

RequestRefusal missing_block(std::string_view block, std::string message) {
    RequestRefusal r;
    r.kind = "missing_block";
    r.message = std::move(message);
    r.field = "world." + std::string(block);
    r.requires_block = std::string(block);
    r.retry = false;
    return r;
}

RequestRefusal refused_by_world(std::string field, RefusalValue value, std::string reason,
                                std::span<const std::string> alternatives) {
    RequestRefusal r;
    r.kind = "refused_by_world";
    r.message = std::move(reason);
    r.field = std::move(field);
    r.received = std::move(value);
    r.refused_by_world = true;
    if (!alternatives.empty()) {
        r.suggestion = "choose";
        r.suggestion_value = alternatives.front();
        for (std::size_t i = 0; i < alternatives.size() && i < 5; ++i) r.suggestion_candidates.emplace_back(alternatives[i]);
    }
    return r;
}

}  // namespace fwg
