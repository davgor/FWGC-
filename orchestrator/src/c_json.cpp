#include "c_json.hpp"

#include <cstdlib>
#include <cstring>
#include <new>
#include <ostream>
#include <stdexcept>

#include "brains.hpp"
#include "c_guard.hpp"
#include "fwg/api/serialize.hpp"

namespace orchestrator::c_api {

namespace {

fwg::JsonValue text_or_null(const std::optional<std::string>& text) {
    return text ? fwg::JsonValue(*text) : fwg::JsonValue(nullptr);
}

fwg::JsonValue strings(const std::vector<std::string>& items) {
    fwg::JsonValue out = fwg::JsonValue::array();
    for (const std::string& item : items) out.push_back(item);
    return out;
}

// A streambuf writing into one malloc'd buffer that grows as it fills, so a document tens of
// megabytes long is never held twice.
class HandedBuffer : public std::streambuf {
public:
    HandedBuffer() = default;
    HandedBuffer(const HandedBuffer&) = delete;
    HandedBuffer& operator=(const HandedBuffer&) = delete;
    ~HandedBuffer() override { std::free(data_); }

    // The text, NUL-terminated, now the caller's; its length in *size.
    char* release(std::size_t* size) {
        if (!room(1)) throw std::bad_alloc();
        data_[used_] = '\0';
        if (size != nullptr) *size = used_;
        char* out = data_;
        data_ = nullptr;
        used_ = capacity_ = 0;
        return out;
    }

protected:
    int_type overflow(int_type ch) override {
        if (traits_type::eq_int_type(ch, traits_type::eof())) return traits_type::not_eof(ch);
        if (!room(1)) return traits_type::eof();
        data_[used_++] = traits_type::to_char_type(ch);
        return ch;
    }
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        if (n <= 0) return 0;
        const auto count = static_cast<std::size_t>(n);
        if (!room(count)) return 0;
        std::memcpy(data_ + used_, s, count);
        used_ += count;
        return n;
    }

private:
    bool room(std::size_t more) {
        if (capacity_ - used_ >= more) return true;
        std::size_t grown = capacity_ == 0 ? std::size_t(1) << 20 : capacity_;
        while (grown - used_ < more) grown *= 2;
        char* moved = static_cast<char*>(std::realloc(data_, grown));
        if (moved == nullptr) return false;
        data_ = moved;
        capacity_ = grown;
        return true;
    }

    char* data_ = nullptr;
    std::size_t used_ = 0;
    std::size_t capacity_ = 0;
};

// A streambuf that keeps nothing but the FNV-1a (64-bit) hash of what passes through it.
class DigestBuffer : public std::streambuf {
public:
    std::uint64_t digest() const { return hash_; }

protected:
    int_type overflow(int_type ch) override {
        if (traits_type::eq_int_type(ch, traits_type::eof())) return traits_type::not_eof(ch);
        mix(static_cast<unsigned char>(traits_type::to_char_type(ch)));
        return ch;
    }
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        for (std::streamsize i = 0; i < n; ++i) mix(static_cast<unsigned char>(s[i]));
        return n;
    }

private:
    void mix(unsigned char byte) {
        hash_ ^= byte;
        hash_ *= 1099511628211ULL;
    }
    std::uint64_t hash_ = 14695981039346656037ULL;
};

}  // namespace

std::string required_text(const char* text, std::string_view what) {
    if (text == nullptr) throw BadArgument(std::string(what) + " is NULL");
    return optional_text(text, what);
}

std::string optional_text(const char* text, std::string_view what) {
    if (text == nullptr) return {};
    std::string out(text);
    if (!is_utf8(out)) throw BadArgument(std::string(what) + " is not UTF-8");
    return out;
}

ActKind act_named(const std::string& name) {
    const std::optional<ActKind> kind = act_kind(name);
    if (!kind) throw BadArgument("the contract has no act named '" + name + "'");
    return *kind;
}

fwg::JsonValue packet_json(const FactPacket& packet) {
    const auto& [contract_version, speaker, answering, outcome, persona, facts] = packet;
    const auto& [uid, name, role, place, manner, voice, toward_player, line, never, voice_key, face, people] = persona;
    fwg::JsonValue who = fwg::JsonValue::object();
    who.set("uid", uid);
    who.set("name", name);
    who.set("role", role);
    who.set("place", place);
    who.set("manner", manner);
    who.set("voice", voice);
    who.set("toward_player", toward_player);
    who.set("line", line);
    who.set("never", strings(never));
    who.set("voice_key", voice_key);
    who.set("face", face);
    who.set("people", people);
    fwg::JsonValue said = fwg::JsonValue::array();
    for (const Fact& fact : facts) {
        const auto& [kind, subject, fields] = fact;
        fwg::JsonValue pairs = fwg::JsonValue::array();
        for (const auto& [key, value] : fields) {
            fwg::JsonValue pair = fwg::JsonValue::array();
            pair.push_back(key);
            pair.push_back(value);
            pairs.push_back(std::move(pair));
        }
        fwg::JsonValue one = fwg::JsonValue::object();
        one.set("kind", orchestrator::to_string(kind));
        one.set("subject", subject);
        one.set("fields", std::move(pairs));
        said.push_back(std::move(one));
    }
    fwg::JsonValue out = fwg::JsonValue::object();
    out.set("contract_version", contract_version);
    out.set("speaker", speaker);
    out.set("answering", orchestrator::to_string(answering));
    out.set("outcome", orchestrator::to_string(outcome));
    out.set("persona", std::move(who));
    out.set("facts", std::move(said));
    return out;
}

fwg::JsonValue acquaintance_json(const Director::Acquaintance& acquaintance) {
    const auto& [conversations, talking, last_day, since, turn, disposition, thanked, calmed, eased_day, said, heard,
                 grievances, forgiven, asked_now, asked_before, work, exposed, finished_covert, heard_truth,
                 seen_unleashed, seen_second, days_met, works_finished, work_news_in] = acquaintance;
    fwg::JsonValue acts = fwg::JsonValue::array();
    for (const auto& [kind, target] : said) {
        fwg::JsonValue pair = fwg::JsonValue::array();
        pair.push_back(orchestrator::to_string(kind));
        pair.push_back(target);
        acts.push_back(std::move(pair));
    }
    fwg::JsonValue counted = fwg::JsonValue::object();
    for (const auto& [kind, times] : heard) counted.set(orchestrator::to_string(kind), times);
    fwg::JsonValue costs = fwg::JsonValue::array();
    for (const int cost : grievances) costs.push_back(cost);
    fwg::JsonValue out = fwg::JsonValue::object();
    out.set("conversations", conversations);
    out.set("talking", talking);
    out.set("last_day", last_day);
    out.set("since", since);
    out.set("turn", turn);
    out.set("disposition", disposition);
    out.set("thanked", thanked);
    out.set("calmed", calmed);
    out.set("eased_day", eased_day);
    out.set("said", std::move(acts));
    out.set("heard", std::move(counted));
    out.set("grievances", std::move(costs));
    out.set("forgiven", forgiven);
    out.set("asked_now", asked_now);
    out.set("asked_before", asked_before);
    out.set("work", work);
    out.set("exposed", exposed);
    out.set("finished_covert", finished_covert);
    out.set("heard_truth", heard_truth);
    out.set("seen_unleashed", seen_unleashed);
    out.set("seen_second", seen_second);
    out.set("days_met", days_met);
    out.set("works_finished", works_finished);
    out.set("work_news_in", work_news_in);
    return out;
}

fwg::JsonValue remembered_json(const std::vector<Remembered>& memories) {
    fwg::JsonValue out = fwg::JsonValue::array();
    for (const Remembered& memory : memories) {
        const auto& [person, kind, subject, act, about, times, first_day, last_day, words, source, seq] = memory;
        fwg::JsonValue one = fwg::JsonValue::object();
        one.set("person", person);
        one.set("memory", kind);
        one.set("subject", subject);
        one.set("act", act);
        one.set("about", about);
        one.set("times", times);
        one.set("first_day", first_day);
        one.set("last_day", last_day);
        one.set("words", words);
        one.set("source", source);
        one.set("seq", seq);
        out.push_back(std::move(one));
    }
    return out;
}

fwg::JsonValue compacted_json(const Compacted& compacted) {
    const auto& [kept, refused, cleared] = compacted;
    fwg::JsonValue out = fwg::JsonValue::object();
    out.set("kept", remembered_json(kept));
    out.set("refused", strings(refused));
    out.set("cleared", cleared);
    return out;
}

fwg::JsonValue bystanders_json(const std::vector<Bystander>& bystanders) {
    fwg::JsonValue out = fwg::JsonValue::array();
    for (const Bystander& bystander : bystanders) {
        const auto& [person, reaction, packet] = bystander;
        fwg::JsonValue one = fwg::JsonValue::object();
        one.set("person", person);
        one.set("reaction", orchestrator::to_string(reaction));
        one.set("packet", packet ? packet_json(*packet) : fwg::JsonValue(nullptr));
        out.push_back(std::move(one));
    }
    return out;
}

fwg::JsonValue feeling_json(const Feeling& feeling) {
    const auto& [now, resting] = feeling;
    fwg::JsonValue out = fwg::JsonValue::object();
    out.set("now", now);
    out.set("resting", resting);
    return out;
}

fwg::JsonValue query_json(const QueryResult& result) {
    const auto& [columns, rows] = result;
    fwg::JsonValue out = fwg::JsonValue::object();
    out.set("columns", strings(columns));
    fwg::JsonValue all = fwg::JsonValue::array();
    for (const std::vector<Cell>& row : rows) {
        fwg::JsonValue cells = fwg::JsonValue::array();
        for (const Cell& cell : row) {
            if (const auto* integer = std::get_if<std::int64_t>(&cell)) {
                cells.push_back(*integer);
            } else if (const auto* real = std::get_if<double>(&cell)) {
                cells.push_back(*real);
            } else if (const auto* text = std::get_if<std::string>(&cell)) {
                if (is_utf8(*text)) {
                    cells.push_back(*text);
                } else {
                    // Bytes that are not text (a blob, or text cast from one): as SQL writes them, x'..'.
                    static constexpr char kHex[] = "0123456789abcdef";
                    std::string written = "x'";
                    for (const unsigned char byte : *text) {
                        written += kHex[byte >> 4];
                        written += kHex[byte & 0xFu];
                    }
                    cells.push_back(written + "'");
                }
            } else {
                cells.push_back(fwg::JsonValue(nullptr));
            }
        }
        all.push_back(std::move(cells));
    }
    out.set("rows", std::move(all));
    return out;
}

fwg::JsonValue learned_json(const std::vector<Learned>& learned) {
    fwg::JsonValue out = fwg::JsonValue::array();
    for (const Learned& one : learned) {
        const auto& [person, event, state] = one;
        fwg::JsonValue item = fwg::JsonValue::object();
        item.set("person", person);
        item.set("event", event);
        item.set("state", state);
        out.push_back(std::move(item));
    }
    return out;
}

fwg::JsonValue related_json(const Related& related) {
    const auto& [knows, would_learn] = related;
    fwg::JsonValue out = fwg::JsonValue::object();
    out.set("knows", strings(knows));
    out.set("would_learn", strings(would_learn));
    return out;
}

fwg::JsonValue contract_json() {
    fwg::JsonValue acts = fwg::JsonValue::array();
    for (int i = 0; i < kActCount; ++i) {
        const auto kind = static_cast<ActKind>(i);
        fwg::JsonValue act = fwg::JsonValue::object();
        act.set("name", orchestrator::to_string(kind));
        act.set("needs_target", act_needs_target(kind));
        act.set("changes_world", act_changes_world(kind));
        acts.push_back(std::move(act));
    }
    fwg::JsonValue outcomes = fwg::JsonValue::array();
    for (int i = 0; i < kOutcomeCount; ++i) outcomes.push_back(orchestrator::to_string(static_cast<Outcome>(i)));
    fwg::JsonValue kinds = fwg::JsonValue::array();
    for (int i = 0; i < kFactKindCount; ++i) kinds.push_back(orchestrator::to_string(static_cast<FactKind>(i)));
    fwg::JsonValue doing = fwg::JsonValue::array();
    for (const Doing d : {Doing::asleep, Doing::at_home, Doing::walking, Doing::working, Doing::held}) {
        fwg::JsonValue one = fwg::JsonValue::object();
        one.set("name", orchestrator::to_string(d));
        one.set("code", doing_code(d));
        doing.push_back(std::move(one));
    }
    fwg::JsonValue out = fwg::JsonValue::object();
    out.set("contract_version", kContractVersion);
    out.set("brains_schema", brains::kSchemaVersion);
    out.set("acts", std::move(acts));
    out.set("outcomes", std::move(outcomes));
    out.set("fact_kinds", std::move(kinds));
    out.set("doing", std::move(doing));
    return out;
}

fwg::JsonValue roster_json(const fwg::World& world, const std::vector<Position>& positions) {
    fwg::JsonValue out = fwg::JsonValue::array();
    for (const Position& at : positions) {
        const fwg::NpcPerson& p = world.npcs.people.at(at.person);
        fwg::JsonValue one = fwg::JsonValue::object();
        one.set("index", at.person);
        one.set("uid", p.uid);
        one.set("name", text_or_null(p.name));
        one.set("post", p.post);
        one.set("site", text_or_null(p.site_uid));
        one.set("workplace", text_or_null(p.workplace));
        one.set("workplace_plot", p.plot_id);
        one.set("home", text_or_null(p.home));
        one.set("home_plot", text_or_null(p.home_plot_id));
        out.push_back(std::move(one));
    }
    return out;
}

std::string dumped(const fwg::JsonValue& value) {
    fwg::JsonDumpOptions options;
    options.ensure_ascii = true;
    options.item_separator = ",";
    options.key_separator = ":";
    options.allow_nan = false;
    return fwg::dump_json(value, options);
}

char* handed_over(std::string_view text) {
    char* out = static_cast<char*>(std::malloc(text.size() + 1));
    if (out == nullptr) throw std::bad_alloc();
    if (!text.empty()) std::memcpy(out, text.data(), text.size());
    out[text.size()] = '\0';
    return out;
}

char* handed_world(const fwg::World& world, std::size_t* size) {
    HandedBuffer buffer;
    std::ostream out(&buffer);
    fwg::write_world_json(out, world, /*pretty=*/false, /*include_timings=*/false);
    out.flush();
    if (!out) throw std::bad_alloc();  // the buffer could not grow
    return buffer.release(size);
}

std::string world_digest(const fwg::World& world) {
    DigestBuffer buffer;
    std::ostream out(&buffer);
    fwg::write_world_json(out, world, /*pretty=*/false, /*include_timings=*/false);
    out.flush();
    static constexpr char kHex[] = "0123456789abcdef";
    std::string text = "fnv1a64:";
    const std::uint64_t digest = buffer.digest();
    for (int shift = 60; shift >= 0; shift -= 4) text += kHex[(digest >> shift) & 0xFu];
    return text;
}

// The activity codes, one case each and no default, with the warning for an enumerator a switch
// leaves out raised to an error: a new Doing stops the build here.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(1 : 4062)
#endif
std::int32_t doing_code(Doing doing) {
    switch (doing) {
        case Doing::asleep: return FWGC_ASLEEP;
        case Doing::at_home: return FWGC_AT_HOME;
        case Doing::walking: return FWGC_WALKING;
        case Doing::working: return FWGC_WORKING;
        case Doing::held: return FWGC_HELD;
    }
    throw std::logic_error("an activity the C interface has no code for");
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

fwgc_stint stint_of(const Stint& stint) {
    const auto& [doing, from_day, to_day, from_along, to_along] = stint;
    fwgc_stint out{};
    out.doing = doing_code(doing);
    out.reserved = 0;
    out.from_day = from_day;
    out.to_day = to_day;
    out.from_along = from_along;
    out.to_along = to_along;
    return out;
}

}  // namespace orchestrator::c_api
