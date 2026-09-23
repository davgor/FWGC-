#include "fwg/counter_kernel.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include "fwg/sha256.hpp"

namespace fwg::kernel {
namespace {

[[noreturn]] void raise(std::string_view code, const std::string& message, std::string field = {}) {
    throw KernelError(std::string(code), message, std::move(field));
}

constexpr std::string_view kInvalidInput = "INVALID_INPUT";

bool is_lower_alnum(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }
bool is_hex_lower(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }

// [a-z0-9][a-z0-9._-]{0,63}
bool identifier_body(std::string_view s) {
    if (s.empty() || s.size() > 64 || !is_lower_alnum(s[0])) return false;
    return std::all_of(s.begin() + 1, s.end(), [](char c) { return is_lower_alnum(c) || c == '.' || c == '_' || c == '-'; });
}

// The reference's `integer(version, 'version')` on a Python scalar, then the version check.
void check_version(int version) {
    if (version < 0) raise(kInvalidInput, "version must be an integer in the declared range", "version");
    if (version != 1) raise("UNSUPPORTED_VERSION", "only contract version 1 is supported", "version");
}

// Whether a stored string can be encoded as UTF-8: strict UTF-8, with no surrogate. A lone
// surrogate can only be here because an escape put it there, which is exactly the string
// the reference's `.encode('utf-8')` refuses.
bool encodable(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto lead = static_cast<unsigned char>(s[i]);
        std::size_t length = 0;
        std::uint32_t cp = 0;
        if (lead < 0x80) {
            ++i;
            continue;
        } else if (lead >= 0xC2 && lead <= 0xDF) {
            length = 2;
            cp = lead & 0x1Fu;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            length = 3;
            cp = lead & 0x0Fu;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            length = 4;
            cp = lead & 0x07u;
        } else {
            return false;
        }
        if (i + length > s.size()) return false;
        for (std::size_t k = 1; k < length; ++k) {
            const auto b = static_cast<unsigned char>(s[i + k]);
            if ((b & 0xC0u) != 0x80u) return false;
            cp = (cp << 6) | (b & 0x3Fu);
        }
        if ((length == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) ||
            (length == 4 && (cp < 0x10000 || cp > 0x10FFFF))) {
            return false;
        }
        i += length;
    }
    return true;
}

void validate_text(const std::string& value) {
    if (!encodable(value)) raise(kInvalidInput, "unpaired Unicode surrogate");
    if (value.size() > kMaxStringBytes) raise(kInvalidInput, "string exceeds 4096 UTF-8 bytes");
}

// The reference's `_validate`, node for node: a key counts as a node one level below its
// object, and is checked before its value.
void validate_value(const JsonValue& value, int depth, int& count) {
    ++count;
    if (depth > kMaxDepth || count > kMaxNodes) raise(kInvalidInput, "JSON depth/node limit exceeded");
    switch (value.type()) {
        case JsonValue::Type::Null:
        case JsonValue::Type::Bool: return;
        case JsonValue::Type::Integer: require_integer(value, "JSON integer", -kMaxSafe); return;
        case JsonValue::Type::String: validate_text(value.as_string()); return;
        case JsonValue::Type::Array:
            for (const JsonValue& item : value.as_array()) validate_value(item, depth + 1, count);
            return;
        case JsonValue::Type::Object:
            for (const JsonValue::Member& member : value.as_object()) {
                validate_text(member.first);
                ++count;
                if (depth + 1 > kMaxDepth || count > kMaxNodes) raise(kInvalidInput, "JSON depth/node limit exceeded");
                validate_value(member.second, depth + 1, count);
            }
            return;
        case JsonValue::Type::Float: break;
    }
    raise(kInvalidInput, "kernel JSON excludes floats and non-JSON types");
}

void check_word(std::string_view word) {
    if (word.size() != 16 || !std::all_of(word.begin(), word.end(), is_hex_lower)) {
        raise(kInvalidInput, "word/seed requires 16 lowercase hexadecimal digits");
    }
}

std::uint64_t parse_word(std::string_view word) {
    std::uint64_t v = 0;
    for (const char c : word) v = (v << 4) | static_cast<std::uint64_t>(c <= '9' ? c - '0' : c - 'a' + 10);
    return v;
}

void append_be64(std::string& out, std::uint64_t v) {
    for (int shift = 56; shift >= 0; shift -= 8) out.push_back(static_cast<char>((v >> shift) & 0xFFu));
}

// ---------------------------------------------------------------------------------------
// The counter.

// (time_ms, event_id): the one order every sequence here is kept in.
std::pair<std::int64_t, std::string> order_of(const JsonValue& event) {
    return {event.at("time_ms").as_int(), event.at("event_id").as_string()};
}

const JsonValue* find_by_id(const std::vector<std::pair<std::string, JsonValue>>& table, const std::string& id) {
    for (const auto& [key, value] : table) {
        if (key == id) return &value;
    }
    return nullptr;
}

Candidate make_candidate(const JsonValue& base, const JsonValue& command, const JsonValue& state,
                         const JsonValue& effects, std::int64_t work) {
    Candidate candidate;
    candidate.base_state = dump_snapshot(base);
    candidate.command = canonical_bytes(command);
    candidate.next_state = dump_snapshot(state);
    candidate.effects = canonical_bytes(effects);
    candidate.work_used = work;
    return candidate;
}

// `commit`, with work_used as it arrived: a transported candidate can carry anything there,
// and the reference refuses it only after the snapshot and the byte envelopes pass.
JsonValue commit_checked(const JsonValue& snapshot, const std::string& base_state, const std::string& command,
                         const std::string& next_state, const std::string& effects, const JsonValue& work_used) {
    JsonValue current = validate_snapshot(snapshot);
    for (const std::string* bytes : {&base_state, &command, &next_state, &effects}) {
        if (bytes->size() > kMaxBytes) raise("INVALID_CANDIDATE", "candidate contains invalid byte envelopes");
    }
    const std::int64_t work = require_integer(work_used, "work_used", 0, kMaxBatch, "INVALID_CANDIDATE");
    Candidate expected;
    try {
        expected = evaluate(load_snapshot(base_state), canonical_loads(command));
    } catch (const KernelError&) {
        raise("INVALID_CANDIDATE", "candidate base or command is invalid");
    }
    const Candidate given{base_state, command, next_state, effects, work};
    if (!(given == expected)) {
        raise("INVALID_CANDIDATE", "candidate does not match the independently re-evaluated transition");
    }
    const std::string current_bytes = dump_snapshot(current);
    if (current_bytes == next_state) return current;  // an identical already-committed candidate
    if (current_bytes != base_state) raise("STALE_REVISION", "current state differs from the evaluated base");
    return load_snapshot(next_state);
}

}  // namespace

KernelError::KernelError(std::string code, const std::string& message, std::string field)
    : std::invalid_argument(message.empty() ? code : message), code_(std::move(code)), field_(std::move(field)) {}

JsonValue KernelError::document() const {
    JsonValue doc = JsonValue::object();
    doc.set("schema", std::string(kFailureSchema));
    doc.set("schema_version", 1);
    doc.set("code", code_);
    doc.set("message", std::string(what()));
    return doc;
}

// --- contract ------------------------------------------------------------------------------

std::int64_t require_integer(const JsonValue& value, std::string_view name, std::int64_t minimum,
                             std::int64_t maximum, std::string_view code) {
    if (!value.is_int64() || value.as_int() < minimum || value.as_int() > maximum) {
        raise(code, std::string(name) + " must be an integer in the declared range", std::string(name));
    }
    return value.as_int();
}

const std::string& require_identifier(const JsonValue& value, std::string_view kind, std::string_view field) {
    if (value.is_string()) {
        const std::string& s = value.as_string();
        if (s.size() > kind.size() && s.compare(0, kind.size(), kind) == 0 && s[kind.size()] == ':' &&
            identifier_body(std::string_view(s).substr(kind.size() + 1))) {
            return s;
        }
    }
    raise(kInvalidInput, "invalid " + std::string(kind) + " identity", std::string(field));
}

void require_version(const JsonValue& value) {
    require_integer(value, "version");
    if (value.as_int() != 1) raise("UNSUPPORTED_VERSION", "only contract version 1 is supported", "version");
}

void require_exact(const JsonValue& value, std::initializer_list<std::string_view> fields) {
    bool ok = value.is_object() && value.size() == fields.size();
    if (ok) {
        for (const std::string_view field : fields) {
            if (!value.contains(field)) {
                ok = false;
                break;
            }
        }
    }
    if (!ok) raise(kInvalidInput, "unexpected or missing envelope fields");
}

void require_header(const JsonValue& value, std::string_view schema) {
    const JsonValue& declared = value.at("schema");
    if (!declared.is_string() || declared.as_string() != schema) {
        raise("UNSUPPORTED_VERSION", "unsupported schema identity", "schema");
    }
    require_version(value.at("schema_version"));
}

JsonValue validate_event(const JsonValue& event, std::string_view world_id) {
    require_exact(event, {"schema", "schema_version", "world_id", "event_id", "actor_id", "target_id", "time_ms", "delta"});
    require_header(event, kEventSchema);
    require_identifier(event.at("world_id"), "world", "world_id");
    require_identifier(event.at("event_id"), "event", "event_id");
    require_identifier(event.at("actor_id"), "actor", "actor_id");
    require_identifier(event.at("target_id"), "counter", "target_id");
    if (event.at("world_id").as_string() != world_id || event.at("target_id").as_string() != "counter:main") {
        raise("UNKNOWN_ID", "world or target is not owned by this snapshot",
              event.at("world_id").as_string() != world_id ? "world_id" : "target_id");
    }
    require_integer(event.at("time_ms"), "time_ms");
    require_integer(event.at("delta"), "delta", 1);
    return event;
}

JsonValue validate_command(const JsonValue& command) {
    require_exact(command, {"schema", "schema_version", "world_id", "authority_epoch", "expected_revision",
                            "target_time_ms", "budget", "events"});
    require_header(command, kCommandSchema);
    require_identifier(command.at("world_id"), "world", "world_id");
    for (const std::string_view key : {"authority_epoch", "expected_revision", "target_time_ms"}) {
        require_integer(command.at(key), key);
    }
    require_integer(command.at("budget"), "budget", 1, kMaxBatch, "WORK_BUDGET");
    const JsonValue& events = command.at("events");
    if (!events.is_array()) raise(kInvalidInput, "events must be an array", "events");
    if (events.size() > static_cast<std::size_t>(kMaxBatch)) {
        raise("STATE_CAPACITY", "a command accepts at most 64 event entries", "events");
    }
    for (const JsonValue& event : events.as_array()) validate_event(event, command.at("world_id").as_string());
    return command;
}

// --- numeric -------------------------------------------------------------------------------

std::string canonical_bytes(const JsonValue& value, int version) {
    check_version(version);
    int count = 0;
    validate_value(value, 0, count);
    std::string result = dump_canonical(value, /*ensure_ascii=*/true);
    if (result.size() > kMaxBytes) raise(kInvalidInput, "JSON byte limit exceeded");
    return result;
}

JsonValue canonical_loads(std::string_view raw, int version) {
    check_version(version);
    if (raw.size() > kMaxBytes ||
        (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF && static_cast<unsigned char>(raw[1]) == 0xBB &&
         static_cast<unsigned char>(raw[2]) == 0xBF)) {
        raise(kInvalidInput, "expected UTF-8 bytes without BOM within byte limit");
    }
    JsonParseOptions options;
    options.reject_duplicate_keys = true;
    options.allow_nan_literals = false;
    options.allow_floats = false;
    options.max_integer_digits = 16;
    JsonValue result;
    try {
        result = parse_json(raw, options);
    } catch (const JsonError& error) {
        raise(kInvalidInput, std::string("invalid JSON encoding or structure: ") + error.what());
    }
    canonical_bytes(result);
    return result;
}

std::string digest(const JsonValue& value, int version) { return sha256_hex(canonical_bytes(value, version)); }

std::string random_word(std::string_view seed, std::string_view stream, std::int64_t index, int version) {
    check_version(version);
    check_word(seed);
    const bool stream_ok = !stream.empty() && stream.size() <= 64 && stream[0] >= 'a' && stream[0] <= 'z' &&
                           std::all_of(stream.begin() + 1, stream.end(), [](char c) {
                               return is_lower_alnum(c) || c == '.' || c == '_' || c == '-';
                           });
    if (!stream_ok) raise(kInvalidInput, "invalid stream domain", "stream");
    if (index < 0 || index > kMaxSafe) raise(kInvalidInput, "stream index must be an integer in the declared range", "index");

    // Frozen kernel v1 domain label; not the product name. Do not rename.
    std::string frame("MathLab/stream/v1");
    frame.push_back('\0');
    append_be64(frame, parse_word(seed));
    frame.push_back(static_cast<char>(stream.size()));
    frame.append(stream);
    append_be64(frame, static_cast<std::uint64_t>(index));
    return sha256_hex(frame).substr(0, 16);
}

double unit_float(std::string_view word, int version) {
    check_version(version);
    check_word(word);
    return static_cast<double>(parse_word(word) >> 11) / 9007199254740992.0;  // 2^53, exact
}

// --- counter -------------------------------------------------------------------------------

JsonValue initialize(std::string_view world_id, std::int64_t authority_epoch) {
    require_identifier(JsonValue(world_id), "world", "world_id");
    require_integer(JsonValue(authority_epoch), "authority_epoch");
    JsonValue state = JsonValue::object();
    state.set("schema", std::string(kStateSchema));
    state.set("schema_version", 1);
    state.set("rules_version", 1);
    state.set("numeric_version", 1);
    state.set("world_id", std::string(world_id));
    state.set("authority_epoch", authority_epoch);
    state.set("revision", 0);
    state.set("time_ms", 0);
    state.set("counter", 0);
    state.set("receipts", JsonValue::array());
    state.set("pending", JsonValue());
    return state;
}

JsonValue validate_snapshot(const JsonValue& state) {
    require_exact(state, {"schema", "schema_version", "rules_version", "numeric_version", "world_id", "authority_epoch",
                          "revision", "time_ms", "counter", "receipts", "pending"});
    require_header(state, kStateSchema);
    require_version(state.at("rules_version"));
    require_version(state.at("numeric_version"));
    require_identifier(state.at("world_id"), "world", "world_id");
    for (const std::string_view key : {"authority_epoch", "revision", "time_ms", "counter"}) {
        require_integer(state.at(key), key);
    }
    const std::string& world_id = state.at("world_id").as_string();
    const std::int64_t time_ms = state.at("time_ms").as_int();
    const JsonValue& receipts = state.at("receipts");
    const JsonValue& pending = state.at("pending");
    if (!receipts.is_array()) raise(kInvalidInput, "receipts must be an array", "receipts");
    if (receipts.size() > static_cast<std::size_t>(kMaxReceipts)) {
        raise("STATE_CAPACITY", "receipt limit exceeded", "receipts");
    }
    std::int64_t ceiling = time_ms;
    const JsonValue::Array* waiting = nullptr;
    if (!pending.is_null()) {
        require_exact(pending, {"target_time_ms", "events"});
        require_integer(pending.at("target_time_ms"), "target_time_ms");
        const JsonValue& events = pending.at("events");
        if (pending.at("target_time_ms").as_int() <= time_ms || !events.is_array() || events.size() < 1 ||
            events.size() > static_cast<std::size_t>(kMaxBatch)) {
            raise(kInvalidInput, "invalid pending interval", "pending");
        }
        waiting = &events.as_array();
        ceiling = pending.at("target_time_ms").as_int();
    }
    const std::size_t waiting_count = waiting != nullptr ? waiting->size() : 0;
    if (receipts.size() + waiting_count > static_cast<std::size_t>(kMaxReceipts)) {
        raise("STATE_CAPACITY", "pending receipts exceed retained capacity", "pending");
    }

    std::vector<std::string> seen;
    std::int64_t total = 0;
    bool have_previous = false;
    std::pair<std::int64_t, std::string> previous;
    const auto unseen = [&](const std::string& id) { return std::find(seen.begin(), seen.end(), id) == seen.end(); };
    for (const JsonValue& receipt : receipts.as_array()) {
        require_exact(receipt, {"event", "counter_after"});
        const JsonValue& event = receipt.at("event");
        validate_event(event, world_id);
        const auto key = order_of(event);
        const std::int64_t t = key.first;
        if (!unseen(key.second) || (have_previous && key <= previous) || !(0 < t && t <= ceiling)) {
            raise(kInvalidInput, "receipt identity, order or time is invalid", "receipts");
        }
        seen.push_back(key.second);
        previous = key;
        have_previous = true;
        total += event.at("delta").as_int();  // both at most 2^53 - 1: no int64 overflow
        require_integer(JsonValue(total), "counter total");
        require_integer(receipt.at("counter_after"), "counter_after");
        if (receipt.at("counter_after").as_int() != total) {
            raise(kInvalidInput, "receipt accounting mismatch", "counter_after");
        }
    }
    if (state.at("counter").as_int() != total) {
        raise(kInvalidInput, "counter does not match retained effects", "counter");
    }
    std::int64_t future_total = total;
    if (waiting != nullptr) {
        for (const JsonValue& event : *waiting) {
            validate_event(event, world_id);
            const auto key = order_of(event);
            const std::int64_t t = key.first;
            if (!unseen(key.second) || (have_previous && key <= previous) || !(time_ms < t && t <= ceiling)) {
                raise(kInvalidInput, "pending identity, order or time is invalid", "pending");
            }
            seen.push_back(key.second);
            previous = key;
            have_previous = true;
            future_total += event.at("delta").as_int();
            require_integer(JsonValue(future_total), "pending counter total");
        }
    }
    canonical_bytes(state);
    return state;
}

std::string dump_snapshot(const JsonValue& state) { return canonical_bytes(validate_snapshot(state)); }

JsonValue load_snapshot(std::string_view raw) { return validate_snapshot(canonical_loads(raw)); }

Candidate evaluate(const JsonValue& snapshot, const JsonValue& request) {
    const JsonValue state = validate_snapshot(snapshot);
    JsonValue command = validate_command(request);
    if (command.at("world_id").as_string() != state.at("world_id").as_string()) {
        raise("UNKNOWN_ID", "command belongs to another world", "world_id");
    }
    if (command.at("authority_epoch").as_int() != state.at("authority_epoch").as_int()) {
        raise("AUTHORITY_MISMATCH", "command authority epoch is stale or unknown", "authority_epoch");
    }

    // Identical entries collapse, first position kept; a reused id with another payload fails.
    std::vector<std::pair<std::string, JsonValue>> unique;
    for (const JsonValue& event : command.at("events").as_array()) {
        const std::string& id = event.at("event_id").as_string();
        const JsonValue* existing = find_by_id(unique, id);
        if (existing != nullptr) {
            if (!python_equal(*existing, event)) {
                raise("CONFLICTING_EVENT", "event ID reused with a different payload", "event_id");
            }
            continue;
        }
        unique.emplace_back(id, event);
    }
    JsonValue::Array sorted_events;
    for (const auto& entry : unique) sorted_events.push_back(entry.second);
    std::stable_sort(sorted_events.begin(), sorted_events.end(),
                     [](const JsonValue& a, const JsonValue& b) { return order_of(a) < order_of(b); });
    command.set("events", JsonValue(sorted_events));

    std::vector<std::pair<std::string, JsonValue>> received;
    for (const JsonValue& receipt : state.at("receipts").as_array()) {
        received.emplace_back(receipt.at("event").at("event_id").as_string(), receipt.at("event"));
    }
    const JsonValue& pending = state.at("pending");
    JsonValue::Array waiting;
    if (!pending.is_null()) waiting = pending.at("events").as_array();
    std::vector<std::pair<std::string, JsonValue>> known = received;
    for (const JsonValue& event : waiting) {
        const std::string& id = event.at("event_id").as_string();
        bool replaced = false;
        for (auto& entry : known) {
            if (entry.first == id) {
                entry.second = event;
                replaced = true;
                break;
            }
        }
        if (!replaced) known.emplace_back(id, event);
    }
    for (const auto& [id, event] : unique) {
        const JsonValue* existing = find_by_id(known, id);
        if (existing != nullptr && !python_equal(*existing, event)) {
            raise("CONFLICTING_EVENT", "event ID reused with a different payload", "event_id");
        }
    }

    const std::int64_t time_ms = state.at("time_ms").as_int();
    const std::int64_t revision = state.at("revision").as_int();
    const std::int64_t target = command.at("target_time_ms").as_int();
    const std::int64_t expected_revision = command.at("expected_revision").as_int();

    // A completed delivery retry can only acknowledge existing identical effects.
    const bool all_received = std::all_of(unique.begin(), unique.end(), [&](const auto& entry) {
        return find_by_id(received, entry.first) != nullptr;
    });
    if (pending.is_null() && !unique.empty() && all_received && target <= time_ms && expected_revision <= revision) {
        return make_candidate(state, command, state, JsonValue::array(), 0);
    }
    if (expected_revision != revision) {
        raise("STALE_REVISION", "refresh the committed snapshot before evaluation", "expected_revision");
    }
    if (target < time_ms || (!pending.is_null() && target != pending.at("target_time_ms").as_int())) {
        raise("INTERVAL_CONFLICT", "cannot rewind or replace a pending interval", "target_time_ms");
    }
    if (!pending.is_null()) {
        for (const auto& entry : unique) {
            if (find_by_id(known, entry.first) == nullptr) {
                raise("INTERVAL_CONFLICT", "cannot insert work into an accepted pending interval", "events");
            }
        }
    } else {
        waiting.clear();
        for (const JsonValue& event : command.at("events").as_array()) {
            if (find_by_id(received, event.at("event_id").as_string()) == nullptr) waiting.push_back(event);
        }
        for (const JsonValue& event : waiting) {
            const std::int64_t t = event.at("time_ms").as_int();
            if (!(time_ms < t && t <= target)) {
                raise("INTERVAL_CONFLICT", "new events must lie inside (completed time, target time]", "events");
            }
        }
    }
    if (state.at("receipts").size() + waiting.size() > static_cast<std::size_t>(kMaxReceipts)) {
        raise("STATE_CAPACITY", "receipt retention is full; IDs cannot be forgotten or reused", "receipts");
    }
    std::int64_t interval = 0;  // at most 64 deltas of at most 2^53 - 1 each: fits int64
    for (const JsonValue& event : waiting) interval += event.at("delta").as_int();
    if (state.at("counter").as_int() + interval > kMaxSafe) {
        raise("NUMERIC_OVERFLOW", "the complete interval exceeds the counter domain", "counter");
    }

    const std::int64_t work = std::min<std::int64_t>(command.at("budget").as_int(), static_cast<std::int64_t>(waiting.size()));
    JsonValue result = state;
    JsonValue effects = JsonValue::array();
    std::int64_t counter = result.at("counter").as_int();
    for (std::int64_t k = 0; k < work; ++k) {
        const JsonValue& event = waiting[static_cast<std::size_t>(k)];
        counter += event.at("delta").as_int();
        result.set("counter", counter);
        JsonValue receipt = JsonValue::object();
        receipt.set("event", event);
        receipt.set("counter_after", counter);
        result.at("receipts").push_back(receipt);
        effects.push_back(std::move(receipt));
    }
    JsonValue::Array remaining(waiting.begin() + static_cast<std::ptrdiff_t>(work), waiting.end());
    if (!remaining.empty()) {
        JsonValue next = JsonValue::object();
        next.set("target_time_ms", target);
        next.set("events", JsonValue(std::move(remaining)));
        result.set("pending", std::move(next));
    } else {
        result.set("pending", JsonValue());
        result.set("time_ms", target);
    }
    if (!python_equal(result, state)) {
        if (revision == kMaxSafe) raise("NUMERIC_OVERFLOW", "revision cannot wrap", "revision");
        result.set("revision", revision + 1);
    }
    return make_candidate(state, command, result, effects, work);
}

JsonValue commit(const JsonValue& snapshot, const Candidate& candidate) {
    return commit_checked(snapshot, candidate.base_state, candidate.command, candidate.next_state, candidate.effects,
                          JsonValue(candidate.work_used));
}

std::string dump_candidate(const Candidate& candidate) {
    const JsonValue base = load_snapshot(candidate.base_state);
    commit(base, candidate);
    JsonValue document = JsonValue::object();
    document.set("schema", std::string(kCandidateSchema));
    document.set("schema_version", 1);
    document.set("base_state", base);
    document.set("command", canonical_loads(candidate.command));
    document.set("next_state", canonical_loads(candidate.next_state));
    document.set("effects", canonical_loads(candidate.effects));
    document.set("work_used", candidate.work_used);
    return canonical_bytes(document);
}

Candidate load_candidate(std::string_view raw) {
    const JsonValue document = canonical_loads(raw);
    require_exact(document, {"schema", "schema_version", "base_state", "command", "next_state", "effects", "work_used"});
    require_header(document, kCandidateSchema);
    Candidate candidate;
    candidate.base_state = canonical_bytes(document.at("base_state"));
    candidate.command = canonical_bytes(document.at("command"));
    candidate.next_state = canonical_bytes(document.at("next_state"));
    candidate.effects = canonical_bytes(document.at("effects"));
    commit_checked(document.at("base_state"), candidate.base_state, candidate.command, candidate.next_state,
                   candidate.effects, document.at("work_used"));
    candidate.work_used = document.at("work_used").as_int();
    return candidate;
}

}  // namespace fwg::kernel
