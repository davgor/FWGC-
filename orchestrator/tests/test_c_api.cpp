// The C interface, through the DLL a game loads, held to the C++ director it wraps: the same
// request makes the same world, the same inputs give the same packets, days and feelings, and a
// save restores byte for byte. Every test that looks for something first checks it is there.
#include "orchestrator/fwgc_director.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <new>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#include <float.h>
#endif

#include "c_guard.hpp"
#include "check.hpp"
#include "fwg/api/json_value.hpp"
#include "fwg/api/read.hpp"
#include "fwg/api/request_json.hpp"
#include "fwg/api/serialize.hpp"
#include "fwg/api/world.hpp"
#include "fwg/api/world_time.hpp"
#include "orchestrator/director.hpp"

namespace {

using orchestrator::Director;
using orchestrator::Doing;
using orchestrator::FactPacket;

// ---- the ABI, checked where it is compiled ----------------------------------------------------

static_assert(sizeof(fwgc_stint) == 40 && offsetof(fwgc_stint, from_day) == 8 && offsetof(fwgc_stint, to_along) == 32);
static_assert(sizeof(fwgc_position) == 48 && offsetof(fwgc_position, stint) == 8);
static_assert(sizeof(fwgc_whereabouts) == 64 && offsetof(fwgc_whereabouts, day) == 8 &&
              offsetof(fwgc_whereabouts, along) == 16 && offsetof(fwgc_whereabouts, stint) == 24);
static_assert(sizeof(fwgc_status) == 4);

// ---- helpers ----------------------------------------------------------------------------------

std::string request_for(unsigned seed, int size) {
    return "{\"seed\": " + std::to_string(seed) + ", \"overrides\": {\"size\": " + std::to_string(size) + "}}";
}
const std::string kRequest = request_for(42, 33);

// The world the request makes, made here the way the library makes it.
const fwg::World& made_here() {
    static const fwg::World world = fwg::generate(fwg::resolve(fwg::request_from_json(fwg::parse_json(kRequest))));
    return world;
}

std::string error_text() { return fwgc_last_error(); }

void succeeded(fwgc_status status, const std::string& what) {
    check::equal(status, FWGC_OK, what + " (" + error_text() + ")");
}

// A handle, destroyed with this.
class Handle {
public:
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : d_(std::exchange(other.d_, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            fwgc_destroy(d_);
            d_ = std::exchange(other.d_, nullptr);
        }
        return *this;
    }
    ~Handle() { fwgc_destroy(d_); }
    fwgc_director* get() const { return d_; }
    fwgc_director** reset() {
        fwgc_destroy(d_);
        d_ = nullptr;
        return &d_;
    }

private:
    fwgc_director* d_ = nullptr;
};

Handle created(const std::string& request = kRequest) {
    Handle h;
    succeeded(fwgc_create(request.c_str(), h.reset()), "fwgc_create");
    check::is_true(h.get() != nullptr, "a handle");
    return h;
}

Handle restored(const std::string& save) {
    Handle h;
    succeeded(fwgc_restore(save.c_str(), h.reset()), "fwgc_restore");
    check::is_true(h.get() != nullptr, "a restored handle");
    return h;
}

// Text the library handed over, freed.
std::string taken(char* text) {
    std::string out = text != nullptr ? std::string(text) : std::string();
    fwgc_free(text);
    return out;
}

std::string save_of(const Handle& h) {
    char* json = nullptr;
    std::size_t size = 0;
    succeeded(fwgc_save(h.get(), &json, &size), "fwgc_save");
    std::string text = taken(json);
    check::equal(text.size(), size, "the save's size as reported");
    return text;
}

std::string world_of(const Handle& h) {
    char* json = nullptr;
    std::size_t size = 0;
    succeeded(fwgc_world_json(h.get(), &json, &size), "fwgc_world_json");
    std::string text = taken(json);
    check::equal(text.size(), size, "the world document's size as reported");
    return text;
}

std::string world_here(const fwg::World& world) {
    std::ostringstream out;
    fwg::write_world_json(out, world, /*pretty=*/false, /*include_timings=*/false);
    return out.str();
}

std::string digest_of(std::string_view text) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char c : text) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ULL;
    }
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out = "fnv1a64:";
    for (int shift = 60; shift >= 0; shift -= 4) out += kHex[(hash >> shift) & 0xFu];
    return out;
}

std::string start_digest_in(const std::string& save) { return fwg::parse_json(save).at("start_digest").as_string(); }

double clock_of(const Handle& h) {
    double day = 0.0;
    succeeded(fwgc_clock(h.get(), &day), "fwgc_clock");
    return day;
}

std::int32_t code_of(Doing doing) {
    switch (doing) {
        case Doing::asleep: return FWGC_ASLEEP;
        case Doing::at_home: return FWGC_AT_HOME;
        case Doing::walking: return FWGC_WALKING;
        case Doing::working: return FWGC_WORKING;
        case Doing::held: return FWGC_HELD;
    }
    return -1;
}

bool same_stint(const fwgc_stint& c, const orchestrator::Stint& s) {
    return c.doing == code_of(s.doing) && c.reserved == 0 && c.from_day == s.from_day && c.to_day == s.to_day &&
           c.from_along == s.from_along && c.to_along == s.to_along;
}

std::vector<fwgc_position> positions_of(const Handle& h) {
    std::size_t count = 0;
    const fwgc_status probe = fwgc_positions(h.get(), nullptr, 0, &count);
    check::is_true(probe == FWGC_OK || probe == FWGC_BUFFER_TOO_SMALL, "positions counted");
    std::vector<fwgc_position> out(count);
    succeeded(fwgc_positions(h.get(), out.data(), out.size(), &count), "fwgc_positions");
    check::equal(count, out.size(), "every position fitted");
    return out;
}

void same_positions(const Handle& h, const Director& d, const std::string& what) {
    const std::vector<fwgc_position> c = positions_of(h);
    const std::vector<orchestrator::Position> cpp = d.positions();
    check::equal(c.size(), cpp.size(), what + ": as many positions");
    for (std::size_t i = 0; i < c.size() && i < cpp.size(); ++i) {
        check::is_true(c[i].person == cpp[i].person && c[i].reserved == 0 && same_stint(c[i].stint, cpp[i].stint),
                       what + ": position " + std::to_string(i) + " as the director has it");
    }
}

void keys_are(const fwg::JsonValue& object, std::initializer_list<const char*> keys, const std::string& what) {
    check::is_true(object.is_object() && object.size() == keys.size(), what + ": exactly its keys");
    for (const char* key : keys) check::is_true(object.contains(key), what + " has " + key);
}

// A packet read back from the interface's JSON: if it prints as the director's did, the JSON lost
// nothing.
FactPacket packet_from_json(const fwg::JsonValue& json) {
    keys_are(json, {"contract_version", "speaker", "answering", "outcome", "persona", "facts"}, "a packet");
    FactPacket out;
    auto& [contract_version, speaker, answering, outcome, persona, facts] = out;
    contract_version = static_cast<int>(json.at("contract_version").as_int());
    speaker = json.at("speaker").as_string();
    const std::optional<orchestrator::ActKind> act = orchestrator::act_kind(json.at("answering").as_string());
    const std::optional<orchestrator::Outcome> came = orchestrator::outcome_named(json.at("outcome").as_string());
    check::is_true(act.has_value() && came.has_value(), "a packet's act and outcome by name");
    answering = act.value_or(orchestrator::ActKind::NoAct);
    outcome = came.value_or(orchestrator::Outcome::NotUnderstood);
    const fwg::JsonValue& who = json.at("persona");
    keys_are(who, {"uid", "name", "role", "place", "manner", "voice", "toward_player", "line", "never", "voice_key", "face", "people"},
             "a persona");
    auto& [uid, name, role, place, manner, voice, toward_player, line, never, voice_key, face, people] = persona;
    uid = who.at("uid").as_string();
    name = who.at("name").as_string();
    role = who.at("role").as_string();
    place = who.at("place").as_string();
    manner = who.at("manner").as_string();
    voice = who.at("voice").as_string();
    toward_player = who.at("toward_player").as_string();
    line = who.at("line").as_string();
    for (const fwg::JsonValue& item : who.at("never").as_array()) never.push_back(item.as_string());
    voice_key = who.at("voice_key").as_string();
    face = who.at("face").as_string();
    people = who.at("people").as_string();
    for (const fwg::JsonValue& item : json.at("facts").as_array()) {
        keys_are(item, {"kind", "subject", "fields"}, "a fact");
        orchestrator::Fact fact;
        auto& [kind, subject, fields] = fact;
        const std::optional<orchestrator::FactKind> named = orchestrator::fact_kind_named(item.at("kind").as_string());
        check::is_true(named.has_value(), "a fact's kind by name");
        kind = named.value_or(orchestrator::FactKind::Place);
        subject = item.at("subject").as_string();
        for (const fwg::JsonValue& pair : item.at("fields").as_array()) {
            check::is_true(pair.is_array() && pair.size() == 2, "a field as a pair");
            fields.emplace_back(pair.at(0).as_string(), pair.at(1).as_string());
        }
        facts.push_back(std::move(fact));
    }
    return out;
}

void same_acquaintance(const Handle& h, const Director& d, const std::string& speaker, const std::string& what) {
    char* json = nullptr;
    succeeded(fwgc_acquaintance(h.get(), speaker.c_str(), &json), "fwgc_acquaintance");
    const std::string text = taken(json);
    const Director::Acquaintance* known = d.acquaintance(speaker);
    if (known == nullptr) {
        check::equal(text, std::string("null"), what + ": no acquaintance");
        return;
    }
    const fwg::JsonValue j = fwg::parse_json(text);
    const auto& [conversations, talking, last_day, since, turn, disposition, thanked, calmed, eased_day, said, heard,
                 grievances, forgiven, asked_now, asked_before, work, exposed, finished_covert, heard_truth,
                 seen_unleashed, seen_second, days_met, works_finished, work_news_in, called, called_how, called_other] = *known;
    check::equal(j.size(), std::size_t(27), what + ": every field of the acquaintance");
    check::equal(j.at("conversations").as_int(), std::int64_t(conversations), what + " conversations");
    check::equal(j.at("talking").as_bool(), talking, what + " talking");
    check::is_true(j.at("last_day").as_float() == last_day, what + " last_day");
    check::equal(j.at("since").as_string(), since, what + " since");
    check::equal(j.at("turn").as_int(), std::int64_t(turn), what + " turn");
    check::equal(j.at("disposition").as_int(), std::int64_t(disposition), what + " disposition");
    check::equal(j.at("thanked").as_bool(), thanked, what + " thanked");
    check::equal(j.at("calmed").as_bool(), calmed, what + " calmed");
    check::is_true(j.at("eased_day").as_float() == eased_day, what + " eased_day");
    check::equal(j.at("said").size(), said.size(), what + " said");
    for (std::size_t i = 0; i < said.size() && i < j.at("said").size(); ++i) {
        check::equal(j.at("said").at(i).at(0).as_string(), std::string(orchestrator::to_string(said[i].first)), what + " said, the act");
        check::equal(j.at("said").at(i).at(1).as_string(), said[i].second, what + " said, the target");
    }
    check::equal(j.at("heard").size(), heard.size(), what + " heard");
    for (const auto& [act, times] : heard)
        check::equal(j.at("heard").at(orchestrator::to_string(act)).as_int(), std::int64_t(times), what + " heard, counted");
    check::equal(j.at("grievances").size(), grievances.size(), what + " grievances");
    for (std::size_t i = 0; i < grievances.size() && i < j.at("grievances").size(); ++i)
        check::equal(j.at("grievances").at(i).as_int(), std::int64_t(grievances[i]), what + " a grievance");
    check::equal(j.at("forgiven").as_int(), std::int64_t(forgiven), what + " forgiven");
    check::equal(j.at("asked_now").as_string(), asked_now, what + " asked_now");
    check::equal(j.at("asked_before").as_string(), asked_before, what + " asked_before");
    check::equal(j.at("work").as_string(), work, what + " work");
    check::equal(j.at("exposed").as_bool(), exposed, what + " exposed");
    check::equal(j.at("finished_covert").as_bool(), finished_covert, what + " finished_covert");
    check::equal(j.at("heard_truth").as_bool(), heard_truth, what + " heard_truth");
    check::equal(j.at("seen_unleashed").as_bool(), seen_unleashed, what + " seen_unleashed");
    check::equal(j.at("seen_second").as_bool(), seen_second, what + " seen_second");
    check::equal(j.at("days_met").as_int(), std::int64_t(days_met), what + " days_met");
    check::equal(j.at("works_finished").as_int(), std::int64_t(works_finished), what + " works_finished");
    check::equal(j.at("work_news_in").as_int(), std::int64_t(work_news_in), what + " work_news_in");
    check::equal(j.at("called").as_string(), called, what + " called");
    check::equal(j.at("called_how").as_string(), called_how, what + " called_how");
    check::equal(j.at("called_other").as_string(), called_other, what + " called_other");
}

// Memories as the interface writes them, held to the director's, field for field.
void same_memories(const fwg::JsonValue& json, const std::vector<orchestrator::Remembered>& cpp, const std::string& what) {
    check::equal(json.size(), cpp.size(), what + ": as many memories");
    for (std::size_t i = 0; i < cpp.size() && i < json.size(); ++i) {
        const auto& [person, memory, subject, act, about, times, first_day, last_day, words, source, seq] = cpp[i];
        const fwg::JsonValue& one = json.at(i);
        keys_are(one, {"person", "memory", "subject", "act", "about", "times", "first_day", "last_day", "words", "source", "seq"},
                 what + ": a memory");
        check::is_true(one.at("person").as_string() == person && one.at("memory").as_string() == memory &&
                           one.at("subject").as_string() == subject && one.at("act").as_string() == act &&
                           one.at("about").as_string() == about && one.at("times").as_int() == times &&
                           one.at("first_day").to_double() == std::optional<double>(first_day) &&
                           one.at("last_day").to_double() == std::optional<double>(last_day) && one.at("words").as_string() == words &&
                           one.at("source").as_string() == source && one.at("seq").as_int() == seq,
                       what + ": memory " + std::to_string(i) + " as the director has it");
    }
}

void same_feeling(const Handle& h, const Director& d, const std::string& person, const std::string& what) {
    char* json = nullptr;
    succeeded(fwgc_feeling(h.get(), person.c_str(), &json), "fwgc_feeling");
    const fwg::JsonValue j = fwg::parse_json(taken(json));
    const auto& [now, resting] = d.feeling(person);
    keys_are(j, {"now", "resting"}, what + ": a feeling");
    check::equal(j.at("now").as_string(), now, what + ": feeling now");
    check::equal(j.at("resting").as_string(), resting, what + ": feeling at rest");
}

std::uint32_t index_in(const fwg::World& world, const std::string& uid) {
    for (std::size_t i = 0; i < world.npcs.people.size(); ++i) {
        if (world.npcs.people[i].uid == uid) return static_cast<std::uint32_t>(i);
    }
    check::fail("the premise: the roster names " + uid);
}

bool same_whereabouts(const fwgc_whereabouts& c, const orchestrator::Whereabouts& w, const fwg::World& world) {
    return c.person == index_in(world, w.person) && c.reserved == 0 && c.day == w.day && c.along == w.along &&
           same_stint(c.stint, w.stint);
}

// Who a script talks to, found in the seed-42 world a month on: two quest givers and their
// offers, a smith who walks to work, someone else at the smith's site, and a place the smith does
// not know with someone elsewhere who does, and so joins in when asked it within earshot.
struct Players {
    std::string giver1, quest1, giver2, quest2, smith, neighbour, site, far_place, far_knower;
};

const Players& players() {
    static const Players found = [] {
        Director d(made_here());
        d.advance(30.0);
        fwg::QuestsReadRequest request;
        request.states = std::vector<fwg::QuestState>{fwg::QuestState::Offered};
        std::vector<std::pair<std::string, std::string>> offers;  // (quest, giver)
        for (const fwg::QuestRow& row : fwg::read_quests(d.world(), request).quests) {
            if (row.giver_uid && row.giver && row.giver->liveness == fwg::Liveness::Present)
                offers.emplace_back(row.quest_id, *row.giver_uid);
        }
        if (offers.size() < 2) check::fail("the premise: two offers on the board a month on");
        Players out;
        out.quest1 = offers[0].first;
        out.giver1 = offers[0].second;
        out.quest2 = offers[1].first;
        out.giver2 = offers[1].second;
        for (const fwg::NpcPerson& p : d.world().npcs.people) {
            if (!p.is_cast && p.alive && p.site_kind == std::optional<std::string>("city") && p.commute_m && *p.commute_m >= 60 &&
                p.building_id == std::optional<std::string>("building.smithy")) {
                out.smith = p.uid;
                out.site = p.site_uid.value_or("");
                break;
            }
        }
        if (out.smith.empty()) check::fail("the premise: a smith who walks to work");
        for (const fwg::NpcPerson& p : d.world().npcs.people) {
            if (!p.is_cast && p.alive && p.uid != out.smith && p.site_uid == std::optional<std::string>(out.site)) {
                out.neighbour = p.uid;
                break;
            }
        }
        if (out.neighbour.empty()) check::fail("the premise: someone else at the smith's site");
        std::vector<std::string> elsewhere;  // one post from each other site
        std::set<std::string> sites;
        for (const fwg::NpcPerson& p : d.world().npcs.people) {
            if (!p.is_cast && p.alive && p.site_uid && *p.site_uid != out.site && sites.insert(*p.site_uid).second)
                elsewhere.push_back(p.uid);
        }
        for (const fwg::SettlementSite& city : d.world().settlements.sites) {
            Director trial = d;
            const FactPacket asked =
                trial.respond(out.smith, orchestrator::DialogueAct{orchestrator::ActKind::AskAboutPlace, city.uid});
            if (asked.outcome != orchestrator::Outcome::Unknown) continue;
            for (const orchestrator::Bystander& b : trial.overhear(elsewhere)) {
                if (b.reaction == orchestrator::Overheard::joins) {
                    out.far_place = city.uid;
                    out.far_knower = b.person;
                }
            }
            if (!out.far_place.empty()) break;
        }
        if (out.far_place.empty()) check::fail("the premise: a place the smith does not know that someone elsewhere does");
        return out;
    }();
    return found;
}

// Every table of the brain database, read through the DLL and from the director: the same columns,
// and the same rows, cell for cell.
void same_brains(const Handle& h, const Director& d, const std::string& what) {
    for (const char* table :
         {"people", "places", "work", "relationships", "events", "event_states", "facts", "accounts", "exchanges",
          "exchange_facts", "memories"}) {
        const std::string sql = std::string("SELECT * FROM ") + table + " ORDER BY 1, 2";
        char* json = nullptr;
        succeeded(fwgc_query(h.get(), sql.c_str(), &json), what + ": fwgc_query " + table);
        const fwg::JsonValue read = fwg::parse_json(taken(json));
        keys_are(read, {"columns", "rows"}, "a query's answer");
        const orchestrator::QueryResult cpp = d.query(sql);
        const std::string where = what + ", " + table;
        check::equal(read.at("columns").size(), cpp.columns.size(), where + ": columns");
        for (std::size_t c = 0; c < cpp.columns.size() && c < read.at("columns").size(); ++c)
            check::equal(read.at("columns").at(c).as_string(), cpp.columns[c], where + ": a column's name");
        check::equal(read.at("rows").size(), cpp.rows.size(), where + ": rows");
        for (std::size_t r = 0; r < cpp.rows.size() && r < read.at("rows").size(); ++r)
            for (std::size_t c = 0; c < cpp.rows[r].size(); ++c) {
                const fwg::JsonValue& cell = read.at("rows").at(r).at(c);
                const orchestrator::Cell& want = cpp.rows[r][c];
                bool same = false;
                if (const auto* integer = std::get_if<std::int64_t>(&want))
                    same = cell.is_int64() && cell.as_int() == *integer;
                else if (const auto* real = std::get_if<double>(&want))
                    same = cell.to_double() == std::optional<double>(*real);
                else if (const auto* text = std::get_if<std::string>(&want))
                    same = cell.is_string() && cell.as_string() == *text;
                else
                    same = cell.is_null();
                if (!same) check::fail(where + ": row " + std::to_string(r) + ", " + cpp.columns[c] + " differs");
            }
    }
}

// The interface and the C++ director, fed the same inputs, compared after each.
class Lockstep {
public:
    Lockstep() : c_(created()), cpp_(made_here()) {}

    const Handle& handle() const { return c_; }
    const Director& director() const { return cpp_; }
    const std::set<std::string>& acts() const { return acts_; }
    const std::set<std::size_t>& kinds() const { return kinds_; }

    void advance(double days) {
        succeeded(fwgc_advance(c_.get(), days), "fwgc_advance");
        cpp_.advance(days);
        compare("advance");
    }
    FactPacket say(const std::string& speaker, const std::string& act, const std::string& target = "") {
        char* json = nullptr;
        char* text = nullptr;
        succeeded(fwgc_respond(c_.get(), speaker.c_str(), act.c_str(), target.empty() ? nullptr : target.c_str(), &json, &text),
                  "fwgc_respond " + act);
        const std::optional<orchestrator::ActKind> kind = orchestrator::act_kind(act);
        check::is_true(kind.has_value(), "the premise: an act named " + act);
        const FactPacket packet = cpp_.respond(speaker, orchestrator::DialogueAct{kind.value_or(orchestrator::ActKind::NoAct), target});
        const std::string printed = orchestrator::packet_text(packet);
        check::equal(taken(text), printed, act + ": the packet text");
        check::equal(orchestrator::packet_text(packet_from_json(fwg::parse_json(taken(json)))), printed,
                     act + ": the packet's JSON, read back");
        same_acquaintance(c_, cpp_, speaker, act);
        same_feeling(c_, cpp_, speaker, act);
        acts_.insert(act);
        compare("say " + act);
        return packet;
    }
    void resolve(const std::string& quest, bool completed) {
        succeeded(fwgc_resolve(c_.get(), quest.c_str(), completed ? 1 : 0), "fwgc_resolve");
        cpp_.resolve(quest, completed);
        compare("resolve");
    }
    void account(const std::string& quest, const std::string& came, const std::string& many, const std::string& cost,
                 const std::string& part) {
        const auto given = [](const std::string& text) { return text.empty() ? nullptr : text.c_str(); };
        succeeded(fwgc_account(c_.get(), quest.c_str(), given(came), given(many), given(cost), given(part)), "fwgc_account");
        cpp_.account(quest, came, many, cost, part);
        same_brains(c_, cpp_, "account");
        compare("account");
    }
    void hold(const std::string& person, double minutes) {
        succeeded(fwgc_hold(c_.get(), person.c_str(), minutes), "fwgc_hold");
        cpp_.hold(person, minutes);
        compare("hold");
    }
    void fright(const std::string& person, int level) {
        succeeded(fwgc_fright(c_.get(), person.c_str(), level), "fwgc_fright");
        cpp_.fright(person, level);
        same_feeling(c_, cpp_, person, "fright");
        compare("fright");
    }
    void gather(const std::vector<std::string>& people) {
        std::vector<const char*> names;
        for (const std::string& person : people) names.push_back(person.c_str());
        succeeded(fwgc_gather(c_.get(), names.data(), names.size()), "fwgc_gather");
        cpp_.gather(people);
        for (const std::string& person : people) same_feeling(c_, cpp_, person, "gather");
        compare("gather");
    }
    std::vector<orchestrator::Bystander> overhear(const std::vector<std::string>& people) {
        std::vector<const char*> names;
        for (const std::string& person : people) names.push_back(person.c_str());
        char* json = nullptr;
        char* text = nullptr;
        succeeded(fwgc_overhear(c_.get(), names.data(), names.size(), &json, &text), "fwgc_overhear");
        const std::vector<orchestrator::Bystander> cpp = cpp_.overhear(people);
        const fwg::JsonValue heard = fwg::parse_json(taken(json));
        check::equal(heard.size(), cpp.size(), "overhear: a reaction for each bystander");
        std::string joined;
        for (std::size_t i = 0; i < cpp.size() && i < heard.size(); ++i) {
            const auto& [person, reaction, packet] = cpp[i];
            const fwg::JsonValue& one = heard.at(i);
            keys_are(one, {"person", "reaction", "packet"}, "a bystander");
            check::equal(one.at("person").as_string(), person, "overhear: the bystander");
            check::equal(one.at("reaction").as_string(), std::string(orchestrator::to_string(reaction)), "overhear: " + person + "'s reaction");
            check::equal(one.at("packet").is_null(), !packet.has_value(), "overhear: a packet exactly for a joiner");
            if (packet) {
                joined = orchestrator::packet_text(*packet);
                check::equal(orchestrator::packet_text(packet_from_json(one.at("packet"))), joined, "overhear: the joiner's packet, read back");
            }
            same_feeling(c_, cpp_, person, "overhear");
            same_acquaintance(c_, cpp_, person, "overhear");
        }
        check::equal(taken(text), joined, "overhear: the joiner's packet text, or nothing");
        compare("overhear");
        return cpp;
    }
    std::vector<orchestrator::Learned> share(const std::vector<std::string>& people) {
        std::vector<const char*> names;
        for (const std::string& person : people) names.push_back(person.c_str());
        char* json = nullptr;
        succeeded(fwgc_share(c_.get(), names.empty() ? nullptr : names.data(), names.size(), &json), "fwgc_share");
        const std::vector<orchestrator::Learned> cpp = cpp_.share(people);
        const fwg::JsonValue heard = fwg::parse_json(taken(json));
        check::equal(heard.size(), cpp.size(), "share: who heard what");
        for (std::size_t i = 0; i < cpp.size() && i < heard.size(); ++i) {
            const auto& [person, event, state] = cpp[i];
            keys_are(heard.at(i), {"person", "event", "state"}, "one who heard");
            check::is_true(heard.at(i).at("person").as_string() == person && heard.at(i).at("event").as_string() == event &&
                               heard.at(i).at("state").as_string() == state,
                           "share: the same, in the same order");
        }
        same_brains(c_, cpp_, "share");
        compare("share");
        return cpp;
    }
    std::vector<orchestrator::HeroDone> direct_heroes(const std::vector<std::string>& heroes) {
        std::vector<const char*> names;
        for (const std::string& hero : heroes) names.push_back(hero.c_str());
        char* json = nullptr;
        succeeded(fwgc_direct_heroes(c_.get(), names.empty() ? nullptr : names.data(), names.size(), &json), "fwgc_direct_heroes");
        const std::vector<orchestrator::HeroDone> cpp = cpp_.direct_heroes(heroes);
        const fwg::JsonValue done = fwg::parse_json(taken(json));
        check::equal(done.size(), cpp.size(), "direct_heroes: who acted");
        for (std::size_t i = 0; i < cpp.size() && i < done.size(); ++i) {
            const auto& [hero, action, outcome, place, against, day, event, text] = cpp[i];
            keys_are(done.at(i), {"hero", "action", "outcome", "place", "against", "day", "event", "text"}, "one who acted");
            const fwg::JsonValue& one = done.at(i);
            check::is_true(one.at("hero").as_string() == hero && one.at("action").as_string() == action &&
                               one.at("outcome").as_string() == outcome && one.at("place").as_string() == place &&
                               one.at("against").as_string() == against && one.at("day").as_float() == day &&
                               one.at("event").as_string() == event && one.at("text").as_string() == text,
                           "direct_heroes: the same, in the same order");
        }
        same_brains(c_, cpp_, "direct_heroes");
        compare("direct_heroes");
        return cpp;
    }
    // A hero's brief read, then their reaction applied (contract version 30), `reaction` or the rules' pick.
    orchestrator::Reaction react(const std::string& hero, const std::string& event, std::optional<orchestrator::Reaction> reaction) {
        char* json = nullptr;
        succeeded(fwgc_reaction_brief(c_.get(), hero.c_str(), event.c_str(), &json), "fwgc_reaction_brief");
        const fwg::JsonValue read = fwg::parse_json(taken(json));
        keys_are(read, {"hero", "event", "name", "archetype", "alignment", "code", "manner", "voice", "toward_player", "line", "never", "deed",
                        "outcome", "open"},
                 "a reaction brief");
        const orchestrator::ReactionBrief brief = cpp_.reaction_brief(hero, event);
        const auto& [b_hero, b_event, name, archetype, alignment, code, manner, voice, toward_player, line, never, deed, outcome, open] = brief;
        for (const auto& [key, want] : {std::pair<const char*, const std::string*>{"hero", &b_hero}, {"event", &b_event}, {"name", &name},
                                        {"archetype", &archetype}, {"alignment", &alignment}, {"code", &code}, {"manner", &manner},
                                        {"voice", &voice}, {"toward_player", &toward_player}, {"line", &line}, {"deed", &deed},
                                        {"outcome", &outcome}})
            check::equal(read.at(key).as_string(), *want, std::string("reaction brief: ") + key);
        check::equal(read.at("never").size(), never.size(), "reaction brief: never");
        for (std::size_t i = 0; i < never.size() && i < read.at("never").size(); ++i)
            check::equal(read.at("never").at(i).as_string(), never[i], "reaction brief: never, in order");
        check::equal(read.at("open").size(), open.size(), "reaction brief: open");
        for (std::size_t i = 0; i < open.size() && i < read.at("open").size(); ++i)
            check::equal(read.at("open").at(i).as_string(), std::string(orchestrator::to_string(open[i])), "reaction brief: open, in order");
        const std::string named = reaction ? std::string(orchestrator::to_string(*reaction)) : std::string();
        succeeded(fwgc_react(c_.get(), hero.c_str(), event.c_str(), reaction ? named.c_str() : nullptr, &json), "fwgc_react");
        const orchestrator::Reaction cpp = cpp_.react(hero, event, reaction);
        const fwg::JsonValue done = fwg::parse_json(taken(json));
        keys_are(done, {"reaction"}, "a reaction");
        check::equal(done.at("reaction").as_string(), std::string(orchestrator::to_string(cpp)), "react: the same");
        same_brains(c_, cpp_, "react");
        compare("react");
        return cpp;
    }
    void report_event(const std::string& site, const std::string& outcome, const std::string& by) {
        succeeded(fwgc_report_event(c_.get(), site.c_str(), outcome.c_str(), by.empty() ? nullptr : by.c_str()), "fwgc_report_event");
        cpp_.report_event(site, outcome, by);
        same_brains(c_, cpp_, "report");
        compare("report");
    }
    void words(const std::string& line, const std::string& reply, const std::string& asked = "") {
        succeeded(fwgc_words(c_.get(), line.c_str(), reply.c_str(), asked.empty() ? nullptr : asked.c_str()), "fwgc_words");
        cpp_.words(line, reply, asked);
        compare("words");
    }
    orchestrator::Compacted compact(const std::string& person, const std::string& outcomes) {
        char* json = nullptr;
        succeeded(fwgc_compact(c_.get(), person.c_str(), outcomes.empty() ? nullptr : outcomes.c_str(), &json), "fwgc_compact");
        const orchestrator::Compacted cpp = cpp_.compact(person, outcomes);
        const fwg::JsonValue done = fwg::parse_json(taken(json));
        keys_are(done, {"kept", "refused", "cleared"}, "a compaction");
        same_memories(done.at("kept"), cpp.kept, "compact: kept");
        check::equal(done.at("refused").size(), cpp.refused.size(), "compact: as many refused");
        for (std::size_t i = 0; i < cpp.refused.size() && i < done.at("refused").size(); ++i)
            check::equal(done.at("refused").at(i).as_string(), cpp.refused[i], "compact: why, in order");
        check::equal(done.at("cleared").as_int(), static_cast<std::int64_t>(cpp.cleared), "compact: cleared");
        char* all = nullptr;
        succeeded(fwgc_memories(c_.get(), person.c_str(), &all), "fwgc_memories");
        same_memories(fwg::parse_json(taken(all)), cpp_.memories(person), "compact: long memory");
        same_acquaintance(c_, cpp_, person, "compact");
        same_brains(c_, cpp_, "compact");
        compare("compact");
        return cpp;
    }

private:
    void compare(const std::string& what) {
        kinds_.insert(cpp_.inputs().back().index());
        same_positions(c_, cpp_, what);
        check::is_true(clock_of(c_) == fwg::world_clock_of(cpp_.world()).day, what + ": the same clock");
    }

    Handle c_;
    Director cpp_;
    std::set<std::string> acts_;
    std::set<std::size_t> kinds_;
};

// A session using every act and every input kind.
void play(Lockstep& both) {
    const Players& p = players();
    both.advance(30.0);
    both.say(p.giver1, "greet");
    both.words("Good day to you.", "And to you, stranger. What brings you here?", "What brings you here?");
    // The player's name (contract version 24), given through the DLL.
    const FactPacket named = both.say(p.giver1, "introduce", "Aldric");
    check::is_true(named.outcome == orchestrator::Outcome::Introduced, "the premise: a name given");
    // Someone asked of by the player's words (contract version 25), through the DLL: a name nobody bears.
    check::is_true(both.say(p.giver1, "ask_about_person", "=Nobody Of Nowhere").outcome == orchestrator::Outcome::Unknown,
                   "a name nobody bears, looked up"); 
    both.say(p.giver1, "ask_for_work");
    both.say(p.giver1, "decline_quest", p.quest1);
    const FactPacket taken_on = both.say(p.giver1, "accept_quest", p.quest1);
    check::is_true(taken_on.outcome == orchestrator::Outcome::Accepted, "the premise: the first offer taken");
    both.resolve(p.quest1, true);
    // The game's account of it (contract version 20), and its giver telling it.
    const std::vector<fwg::NestSite>& nests = both.director().world().beast_nests.sites;
    check::is_true(!nests.empty(), "the premise: a nest");
    both.account(p.quest1, nests.front().id, "many", "", "held");
    const FactPacket told = both.say(p.giver1, "greet");
    check::is_true(std::any_of(told.facts.begin(), told.facts.end(), [](const orchestrator::Fact& f) { return f.kind == orchestrator::FactKind::Account; }),
                   "an Account fact through the dll");
    both.say(p.giver2, "accept_quest", p.quest2);
    both.say(p.giver2, "abandon_quest", p.quest2);
    const FactPacket around = both.say(p.smith, "ask_surroundings");
    std::string place;
    for (const orchestrator::Fact& fact : around.facts) {
        if (fact.kind == orchestrator::FactKind::Place && place.empty()) place = fact.subject;
    }
    check::is_true(!place.empty(), "the premise: a place the smith knows");
    both.say(p.smith, "ask_about_place", place);
    // Asked about a place he does not know, within earshot of someone who does: they join in.
    const FactPacket unknown = both.say(p.smith, "ask_about_place", p.far_place);
    check::is_true(unknown.outcome == orchestrator::Outcome::Unknown, "the premise: a place the smith cannot answer for");
    const std::vector<orchestrator::Bystander> joined = both.overhear({p.far_knower});
    check::is_true(joined.size() == 1 && joined[0].reaction == orchestrator::Overheard::joins && joined[0].packet,
                   "the premise: the one who knows it joins in, with a reply");
    both.words("And what of that place, far off?", "I know it well, as it happens.");
    // The one who joined keeps both: the exchange they overheard, and their own answer with its words.
    std::vector<std::string> joiner_log;
    for (const std::vector<orchestrator::Cell>& row :
         both.director().query("SELECT heard_from, joined, act, outcome, line FROM exchanges WHERE person = '" + p.far_knower + "' ORDER BY id").rows) {
        std::string text;
        for (const orchestrator::Cell& cell : row) {
            if (const auto* word = std::get_if<std::string>(&cell)) text += *word + "|";
            else if (const auto* number = std::get_if<std::int64_t>(&cell)) text += std::to_string(*number) + "|";
            else text += "NULL|";
        }
        joiner_log.push_back(text);
    }
    check::is_true(joiner_log == std::vector<std::string>{p.smith + "|0|ask_about_place|unknown|NULL|",
                                                         "NULL|1|ask_about_place|answered|And what of that place, far off?|"},
                   "the joiner's log: what they overheard, then their answer and its words");
    both.say(p.smith, "ask_about_person", p.neighbour);
    // Contract version 13: asked about themself, a giver tells their story.
    const FactPacket about = both.say(p.giver1, "ask_about_them");
    check::is_true(std::any_of(about.facts.begin(), about.facts.end(), [](const orchestrator::Fact& f) { return f.kind == orchestrator::FactKind::Self; }),
                   "a Self fact through the dll");
    for (const char* act : {"small_talk", "thank", "insult", "apologize", "complain", "ask_safety", "answer", "confront", "no_act"}) {
        both.say(p.smith, act);
        if (std::string_view(act) == "answer") both.words("My sister keeps the mill at the ford.", "Does she now.");
    }
    both.hold(p.smith, 20.0);
    // The neighbour panics and the smith, beside them, is left afraid; once the fright has passed
    // the player calms the neighbour, and the smith overhears it.
    both.fright(p.neighbour, 3);
    both.gather({p.smith, p.neighbour});
    both.advance(0.02);
    const FactPacket eased = both.say(p.neighbour, "calm");
    check::is_true(eased.outcome == orchestrator::Outcome::Calmed, "the premise: the neighbour calmed");
    const std::vector<orchestrator::Bystander> heard = both.overhear({p.smith});
    check::is_true(heard.size() == 1 && heard[0].reaction == orchestrator::Overheard::calmed, "the premise: the smith eased with them");
    // The game says the threat there was beaten, and news goes round: to the smith alone, then all.
    both.report_event(p.site, "beaten", "player");
    // Heroes act at the game's call (contract version 26): those due on the first call.
    const std::vector<orchestrator::HeroDone> acted = both.direct_heroes({});
    check::is_true(!acted.empty(), "the premise: a hero acts");
    // How they took it (contract version 30): the first by the rules; a deed lost, embittered, which the world records.
    const auto lost = std::find_if(acted.begin() + 1, acted.end(), [](const orchestrator::HeroDone& d) { return d.outcome == "lost"; });
    check::is_true(lost != acted.end(), "the premise: another deed, lost");
    (void)both.react(acted.front().hero, acted.front().event, std::nullopt);
    if (lost != acted.end())
        check::is_true(both.react(lost->hero, lost->event, orchestrator::Reaction::Embittered) == orchestrator::Reaction::Embittered, "embittered");
    const std::vector<orchestrator::Learned> smith_heard = both.share({p.smith});
    check::is_true(!smith_heard.empty(), "the premise: the smith hears something");
    both.share({});
    both.say(p.smith, "farewell");
    // The smith's log made into long memory, with outcomes handed in: one quote the stranger said,
    // one they never did.
    const orchestrator::Compacted done = both.compact(
        p.smith,
        R"({"said": [{"quote": "My sister keeps the mill", "about": "the stranger's sister"}, {"quote": "I slew a dragon"}], "last": "they talked of a sister at the mill."})");
    check::is_true(!done.kept.empty() && done.refused.size() == 1 && done.cleared > 0,
                   "the premise: memories kept, the invented quote refused, the log cleared");
    both.advance(0.25);
}

}  // namespace

// ---------------------------------------------------------------------------------------------

TEST(the_header_is_the_abi) {
    check::equal(fwgc_api_version(), FWGC_API_VERSION, "the DLL speaks the header's version");
    check::equal(fwgc_contract_version(), orchestrator::kContractVersion, "the DLL's contract is the director's");
    const std::vector<fwgc_status> codes = {FWGC_OK, FWGC_BAD_ARGUMENT, FWGC_BUFFER_TOO_SMALL, FWGC_REFUSED,
                                            FWGC_INCOMPATIBLE, FWGC_POISONED, FWGC_OUT_OF_MEMORY, FWGC_FAILED};
    for (std::size_t i = 0; i < codes.size(); ++i) check::equal(codes[i], static_cast<fwgc_status>(i), "status codes in order");
    check::is_true(FWGC_ASLEEP == 0 && FWGC_AT_HOME == 1 && FWGC_WALKING == 2 && FWGC_WORKING == 3 && FWGC_HELD == 4,
                   "activity codes in order");
}

TEST(the_contract_describes_itself) {
    char* json = nullptr;
    succeeded(fwgc_contract_json(&json), "fwgc_contract_json");
    const fwg::JsonValue contract = fwg::parse_json(taken(json));
    keys_are(contract, {"contract_version", "brains_schema", "acts", "outcomes", "fact_kinds", "doing", "reactions"}, "the contract");
    check::equal(contract.at("brains_schema").as_int(), std::int64_t{7}, "the brain database's schema");
    const fwg::JsonValue& reactions = contract.at("reactions");
    check::equal(reactions.size(), std::size_t(orchestrator::kReactionCount), "every reaction");
    for (int i = 0; i < orchestrator::kReactionCount && static_cast<std::size_t>(i) < reactions.size(); ++i)
        check::equal(reactions.at(static_cast<std::size_t>(i)).as_string(),
                     std::string(orchestrator::to_string(static_cast<orchestrator::Reaction>(i))), "a reaction's name, in order");
    check::equal(contract.at("contract_version").as_int(), std::int64_t(orchestrator::kContractVersion), "its version");
    const fwg::JsonValue& acts = contract.at("acts");
    check::equal(acts.size(), std::size_t(orchestrator::kActCount), "every act");
    for (int i = 0; i < orchestrator::kActCount && static_cast<std::size_t>(i) < acts.size(); ++i) {
        const auto kind = static_cast<orchestrator::ActKind>(i);
        const fwg::JsonValue& act = acts.at(static_cast<std::size_t>(i));
        check::equal(act.at("name").as_string(), std::string(orchestrator::to_string(kind)), "an act's name");
        check::equal(act.at("needs_target").as_bool(), orchestrator::act_needs_target(kind), "whether it needs a target");
        check::equal(act.at("changes_world").as_bool(), orchestrator::act_changes_world(kind), "whether it changes the world");
    }
    check::equal(contract.at("outcomes").size(), std::size_t(orchestrator::kOutcomeCount), "every outcome");
    for (int i = 0; i < orchestrator::kOutcomeCount; ++i)
        check::equal(contract.at("outcomes").at(static_cast<std::size_t>(i)).as_string(),
                     std::string(orchestrator::to_string(static_cast<orchestrator::Outcome>(i))), "an outcome's name");
    const fwg::JsonValue& kinds = contract.at("fact_kinds");
    for (std::size_t i = 0; i < kinds.size(); ++i)
        check::is_true(orchestrator::fact_kind_named(kinds.at(i).as_string()).has_value(), "a fact kind the contract names");
    check::equal(kinds.size(), std::size_t(orchestrator::kFactKindCount), "every fact kind");
    const fwg::JsonValue& doing = contract.at("doing");
    check::equal(doing.size(), std::size_t(5), "every activity");
    for (const Doing d : {Doing::asleep, Doing::at_home, Doing::walking, Doing::working, Doing::held}) {
        const fwg::JsonValue& one = doing.at(static_cast<std::size_t>(code_of(d)));
        check::equal(one.at("name").as_string(), std::string(orchestrator::to_string(d)), "an activity's name by its code");
        check::equal(one.at("code").as_int(), std::int64_t(code_of(d)), "an activity's code");
    }
}

TEST(a_request_makes_the_world_the_generator_makes) {
    const Handle h = created();
    const std::string here = world_here(made_here());
    const std::string there = world_of(h);
    check::is_true(!here.empty(), "the premise: a world document");
    check::is_true(there == here, "the DLL's world byte for byte the one the request makes here");
    check::equal(start_digest_in(save_of(h)), digest_of(here), "the save's digest, the world's");
}

TEST(a_refused_request_says_why) {
    Handle h;
    check::equal(fwgc_create("{\"seed\": 7, \"overrides\": {\"size\": 2000}}", h.reset()), FWGC_REFUSED, "a size too big");
    check::is_true(h.get() == nullptr, "and no handle");
    const fwg::JsonValue refusal = fwg::parse_json(error_text());
    check::is_true(refusal.contains("field") && refusal.at("field").as_string().find("size") != std::string::npos,
                   "the refusal document naming the size: " + error_text());
    check::equal(fwgc_create("{\"seed\": 7, \"overrides\": {\"size\": 34}}", h.reset()), FWGC_REFUSED, "an even size");
    check::is_true(!error_text().empty() && h.get() == nullptr, "said why, and no handle");
    check::equal(fwgc_create("{\"seed\": ", h.reset()), FWGC_BAD_ARGUMENT, "text that is not JSON");
    check::equal(fwgc_create(nullptr, h.reset()), FWGC_BAD_ARGUMENT, "no request");
    check::equal(fwgc_create(kRequest.c_str(), nullptr), FWGC_BAD_ARGUMENT, "nowhere to put the handle");
}

TEST(every_act_and_input_answers_through_the_dll_as_the_director_does) {
    Lockstep both;
    play(both);
    check::equal(both.acts().size(), std::size_t(orchestrator::kActCount), "the premise: every act made");
    check::equal(both.kinds().size(), std::variant_size_v<orchestrator::Input>, "the premise: every input kind given");

    const Players& p = players();
    const Handle& h = both.handle();
    const Director& d = both.director();
    // One post, alone.
    fwgc_whereabouts where{};
    std::int32_t has_day = -1;
    succeeded(fwgc_whereabouts_of(h.get(), p.smith.c_str(), &where, &has_day), "fwgc_whereabouts_of");
    const std::optional<orchestrator::Whereabouts> cpp_where = d.whereabouts(p.smith);
    check::is_true(has_day == 1 && cpp_where && same_whereabouts(where, *cpp_where, d.world()), "the smith where the director has him");
    succeeded(fwgc_whereabouts_of(h.get(), p.giver1.c_str(), &where, &has_day), "fwgc_whereabouts_of a giver");
    check::equal(has_day, std::int32_t(d.whereabouts(p.giver1) ? 1 : 0), "a day exactly when the director gives one");
    // Everyone at the smith's site.
    const std::vector<orchestrator::Whereabouts> cpp_site = d.whereabouts_at(p.site);
    std::vector<fwgc_whereabouts> site(cpp_site.size());
    std::size_t count = 0;
    succeeded(fwgc_whereabouts_at(h.get(), p.site.c_str(), site.data(), site.size(), &count), "fwgc_whereabouts_at");
    check::is_true(count == cpp_site.size() && !cpp_site.empty(), "everyone at the site");
    for (std::size_t i = 0; i < cpp_site.size(); ++i)
        check::is_true(same_whereabouts(site[i], cpp_site[i], d.world()), "someone at the site where the director has them");
    // The smith's day.
    const double today = std::floor(fwg::world_clock_of(d.world()).day);
    const std::vector<orchestrator::Stint> cpp_day = d.day_of(p.smith, today);
    std::vector<fwgc_stint> day(cpp_day.size() + 2);
    succeeded(fwgc_day_of(h.get(), p.smith.c_str(), today, day.data(), day.size(), &count), "fwgc_day_of");
    check::is_true(count == cpp_day.size() && !cpp_day.empty(), "the smith's day, whole");
    for (std::size_t i = 0; i < cpp_day.size(); ++i) check::is_true(same_stint(day[i], cpp_day[i]), "a stretch of the smith's day");
    // And the world, at the end.
    check::is_true(world_of(h) == world_here(d.world()), "the world byte for byte the director's");
}

TEST(a_save_restores_byte_for_byte_and_goes_on_alike) {
    Lockstep both;
    play(both);
    const std::string first = save_of(both.handle());
    const Handle again = restored(first);
    check::equal(save_of(again), first, "save, restore, save: the same bytes");
    check::is_true(world_of(again) == world_of(both.handle()), "the same world");
    const Players& p = players();
    // What people remember and feel is no part of the world, so a save's check cannot see it:
    // compare it here, for everyone the session touched.
    for (const std::string& who : {p.giver1, p.giver2, p.smith, p.neighbour, p.far_knower}) {
        for (const auto ask : {&fwgc_acquaintance, &fwgc_feeling}) {
            char* one = nullptr;
            char* two = nullptr;
            succeeded(ask(both.handle().get(), who.c_str(), &one), "asked of the original");
            succeeded(ask(again.get(), who.c_str(), &two), "asked of the restored");
            check::equal(taken(two), taken(one), who + " remembered and felt alike");
        }
    }
    char* one = nullptr;
    char* two = nullptr;
    succeeded(fwgc_respond(both.handle().get(), p.smith.c_str(), "greet", nullptr, nullptr, &one), "the original answers");
    succeeded(fwgc_respond(again.get(), p.smith.c_str(), "greet", nullptr, nullptr, &two), "the restored answers");
    check::equal(taken(two), taken(one), "the same answer from both");
    for (const Handle* h : {&both.handle(), &again}) {
        succeeded(fwgc_advance(h->get(), 0.5), "time on both");
        succeeded(fwgc_hold(h->get(), p.smith.c_str(), 45.0), "a hold on both");
    }
    check::equal(save_of(again), save_of(both.handle()), "and their saves still alike");
}

TEST(the_brain_database_reads_through_the_dll_and_comes_back_from_a_save) {
    Lockstep both;
    play(both);
    same_brains(both.handle(), both.director(), "after the session");
    const std::vector<orchestrator::Event> events = both.director().events();
    check::is_true(events.size() > 1, "the premise: events");
    for (const orchestrator::Event& e : {events.front(), events.back()}) {
        char* json = nullptr;
        succeeded(fwgc_related(both.handle().get(), e.id.c_str(), &json), "fwgc_related");
        const fwg::JsonValue related = fwg::parse_json(taken(json));
        keys_are(related, {"knows", "would_learn"}, "who knows an event");
        const orchestrator::Related cpp = both.director().related(e.id);
        for (const auto& [key, want] : {std::pair{"knows", cpp.knows}, std::pair{"would_learn", cpp.would_learn}}) {
            check::equal(related.at(key).size(), want.size(), e.id + ": " + key);
            for (std::size_t i = 0; i < want.size() && i < related.at(key).size(); ++i)
                check::equal(related.at(key).at(i).as_string(), want[i], e.id + ": " + key + ", in order");
        }
    }
    // A save holds no database: the restored one writes it again, the same.
    const Handle again = restored(save_of(both.handle()));
    same_brains(again, both.director(), "restored from its save");
}

TEST(a_save_keeps_every_number_exactly) {
    const Players& p = players();
    const Handle h = created();
    succeeded(fwgc_advance(h.get(), 1.0 / 3.0), "a third of a day");
    succeeded(fwgc_hold(h.get(), p.smith.c_str(), 17.123456789), "an odd hold");
    succeeded(fwgc_advance(h.get(), 0.1234567890123), "an odd span");
    const std::string save = save_of(h);
    const Handle again = restored(save);
    check::is_true(clock_of(again) == clock_of(h), "the clock to the bit");
    const double today = std::floor(clock_of(h));
    std::vector<fwgc_stint> a(16), b(16);
    std::size_t na = 0, nb = 0;
    succeeded(fwgc_day_of(h.get(), p.smith.c_str(), today, a.data(), a.size(), &na), "the day before");
    succeeded(fwgc_day_of(again.get(), p.smith.c_str(), today, b.data(), b.size(), &nb), "the day after");
    check::is_true(na == nb && na > 0 && std::memcmp(a.data(), b.data(), na * sizeof(fwgc_stint)) == 0, "the held day to the bit");
    check::equal(save_of(again), save, "and the save the same bytes");
}

TEST(a_save_that_does_not_restore_here_is_refused) {
    const Players& p = players();
    const Handle h = created();
    succeeded(fwgc_advance(h.get(), 0.2), "time");
    char* text = nullptr;
    succeeded(fwgc_respond(h.get(), p.smith.c_str(), "greet", nullptr, nullptr, &text), "a greeting");
    fwgc_free(text);
    const std::string save = save_of(h);
    const auto edited = [&](auto change) {
        fwg::JsonValue root = fwg::parse_json(save);
        change(root);
        return fwg::dump_json(root);
    };
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"not JSON", "a save, honestly"},
        {"another schema", edited([](fwg::JsonValue& r) { r.set("schema", "fwgc.other"); })},
        {"another version", edited([](fwg::JsonValue& r) { r.set("version", 2); })},
        {"a key it does not know", edited([](fwg::JsonValue& r) { r.set("extra", 1); })},
        {"an input kind it does not know",
         edited([](fwg::JsonValue& r) {
             fwg::JsonValue odd = fwg::JsonValue::object();
             odd.set("teleport", fwg::JsonValue::object());
             r.at("inputs").push_back(std::move(odd));
         })},
        {"an act it does not know", edited([](fwg::JsonValue& r) { r.at("inputs").at(1).at("say").set("act", "sing"); })},
        {"a digest another world made", edited([](fwg::JsonValue& r) { r.set("start_digest", "fnv1a64:0000000000000000"); })},
        {"a check another session made", edited([](fwg::JsonValue& r) { r.at("check").set("operations", 999); })},
        {"a speaker the world does not name",
         edited([](fwg::JsonValue& r) { r.at("inputs").at(1).at("say").set("speaker", "npc-nobody-at-all"); })},
    };
    for (const auto& [why, broken] : cases) {
        Handle none;
        check::equal(fwgc_restore(broken.c_str(), none.reset()), FWGC_INCOMPATIBLE, why);
        check::is_true(none.get() == nullptr && !error_text().empty(), why + ": no handle, and a reason");
    }
    const Handle fine = restored(save);
    check::equal(save_of(fine), save, "the save itself restores");
}

TEST(a_callers_mistake_is_refused_and_records_nothing) {
    const Players& p = players();
    const Handle h = created();
    succeeded(fwgc_advance(h.get(), 30.0), "a month");
    const std::string before = save_of(h);
    const char* bad[] = {"\xC3\x28", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xC0\xAF", "\x80"};
    char* out = nullptr;
    fwgc_whereabouts where{};
    std::int32_t has_day = 0;
    std::size_t count = 0;
    for (const char* text : bad) {
        check::equal(fwgc_respond(h.get(), text, "greet", nullptr, &out, nullptr), FWGC_BAD_ARGUMENT, "a speaker not UTF-8");
        check::equal(fwgc_respond(h.get(), p.smith.c_str(), text, nullptr, &out, nullptr), FWGC_BAD_ARGUMENT, "an act not UTF-8");
        check::equal(fwgc_respond(h.get(), p.smith.c_str(), "ask_about_place", text, &out, nullptr), FWGC_BAD_ARGUMENT,
                     "a target not UTF-8");
        check::equal(fwgc_resolve(h.get(), text, 1), FWGC_BAD_ARGUMENT, "a quest not UTF-8");
        for (int i = 0; i < 5; ++i) {
            const char* given[5] = {"work-1", nullptr, nullptr, nullptr, nullptr};
            given[i] = text;
            check::equal(fwgc_account(h.get(), given[0], given[1], given[2], given[3], given[4]), FWGC_BAD_ARGUMENT,
                         "an account's word not UTF-8");
        }
        check::equal(fwgc_hold(h.get(), text, 5.0), FWGC_BAD_ARGUMENT, "a person held not UTF-8");
        check::equal(fwgc_fright(h.get(), text, 1), FWGC_BAD_ARGUMENT, "a person frightened not UTF-8");
        const char* people[] = {p.smith.c_str(), text};
        check::equal(fwgc_gather(h.get(), people, 2), FWGC_BAD_ARGUMENT, "one of those gathered not UTF-8");
        check::equal(fwgc_feeling(h.get(), text, &out), FWGC_BAD_ARGUMENT, "a person felt not UTF-8");
        check::equal(fwgc_acquaintance(h.get(), text, &out), FWGC_BAD_ARGUMENT, "an acquaintance not UTF-8");
        check::equal(fwgc_whereabouts_of(h.get(), text, &where, &has_day), FWGC_BAD_ARGUMENT, "whereabouts not UTF-8");
        check::equal(fwgc_whereabouts_at(h.get(), text, nullptr, 0, &count), FWGC_BAD_ARGUMENT, "a site not UTF-8");
        check::equal(fwgc_day_of(h.get(), text, 0.0, nullptr, 0, &count), FWGC_BAD_ARGUMENT, "a day's person not UTF-8");
        check::equal(fwgc_query(h.get(), text, &out), FWGC_BAD_ARGUMENT, "a query not UTF-8");
        check::equal(fwgc_related(h.get(), text, &out), FWGC_BAD_ARGUMENT, "an event not UTF-8");
        check::equal(fwgc_report_event(h.get(), text, "beaten", nullptr), FWGC_BAD_ARGUMENT, "a site not UTF-8");
        check::equal(fwgc_report_event(h.get(), p.site.c_str(), "beaten", text), FWGC_BAD_ARGUMENT, "a doer not UTF-8");
        const char* named[] = {text};
        check::equal(fwgc_share(h.get(), named, 1, &out), FWGC_BAD_ARGUMENT, "someone to share with not UTF-8");
        check::equal(fwgc_direct_heroes(h.get(), named, 1, &out), FWGC_BAD_ARGUMENT, "a hero to direct not UTF-8");
        check::equal(fwgc_reaction_brief(h.get(), text, "event-1", &out), FWGC_BAD_ARGUMENT, "a hero's brief, the hero not UTF-8");
        check::equal(fwgc_reaction_brief(h.get(), "hero-x", text, &out), FWGC_BAD_ARGUMENT, "a hero's brief, the deed not UTF-8");
        check::equal(fwgc_react(h.get(), text, "event-1", nullptr, &out), FWGC_BAD_ARGUMENT, "a hero reacting not UTF-8");
        check::equal(fwgc_react(h.get(), "hero-x", text, nullptr, &out), FWGC_BAD_ARGUMENT, "a deed reacted to not UTF-8");
        check::equal(fwgc_react(h.get(), "hero-x", "event-1", text, &out), FWGC_BAD_ARGUMENT, "a reaction not UTF-8");
        check::equal(fwgc_words(h.get(), text, "a reply", nullptr), FWGC_BAD_ARGUMENT, "a line not UTF-8");
        check::equal(fwgc_words(h.get(), "a line", text, nullptr), FWGC_BAD_ARGUMENT, "a reply not UTF-8");
        check::equal(fwgc_words(h.get(), "a line", "a reply", text), FWGC_BAD_ARGUMENT, "a question not UTF-8");
        check::equal(fwgc_compact(h.get(), text, nullptr, &out), FWGC_BAD_ARGUMENT, "someone compacted not UTF-8");
        check::equal(fwgc_compact(h.get(), p.smith.c_str(), text, &out), FWGC_BAD_ARGUMENT, "outcomes not UTF-8");
        check::equal(fwgc_memories(h.get(), text, &out), FWGC_BAD_ARGUMENT, "someone remembering not UTF-8");
        Handle none;
        check::equal(fwgc_create(text, none.reset()), FWGC_BAD_ARGUMENT, "a request not UTF-8");
    }
    // The director's own refusals, and NULLs.
    const char* bystanders[] = {p.smith.c_str()};
    check::equal(fwgc_overhear(h.get(), bystanders, 1, &out, nullptr), FWGC_BAD_ARGUMENT, "overheard, with nothing said");
    const char* unreadable[] = {"\xC3\x28"};
    check::equal(fwgc_overhear(h.get(), unreadable, 1, &out, nullptr), FWGC_BAD_ARGUMENT, "a bystander not UTF-8");
    check::equal(fwgc_overhear(h.get(), nullptr, 1, &out, nullptr), FWGC_BAD_ARGUMENT, "no list of bystanders");
    check::equal(fwgc_respond(h.get(), p.smith.c_str(), "sing", nullptr, &out, nullptr), FWGC_BAD_ARGUMENT, "an act with no name");
    check::equal(fwgc_respond(h.get(), p.smith.c_str(), "ask_about_place", nullptr, &out, nullptr), FWGC_BAD_ARGUMENT,
                 "an act that needs a target, without one");
    check::equal(fwgc_respond(h.get(), "npc-nobody-at-all", "greet", nullptr, &out, nullptr), FWGC_BAD_ARGUMENT, "nobody");
    check::equal(fwgc_respond(h.get(), nullptr, "greet", nullptr, &out, nullptr), FWGC_BAD_ARGUMENT, "no speaker");
    check::equal(fwgc_respond(nullptr, p.smith.c_str(), "greet", nullptr, &out, nullptr), FWGC_BAD_ARGUMENT, "no director");
    check::equal(fwgc_advance(h.get(), -1.0), FWGC_BAD_ARGUMENT, "time backward");
    check::equal(fwgc_advance(h.get(), std::numeric_limits<double>::quiet_NaN()), FWGC_BAD_ARGUMENT, "time that is not a span");
    check::equal(fwgc_hold(h.get(), p.smith.c_str(), 0.0), FWGC_BAD_ARGUMENT, "a hold of no time");
    check::equal(fwgc_hold(h.get(), p.smith.c_str(), 1441.0), FWGC_BAD_ARGUMENT, "a hold of more than a day");
    check::equal(fwgc_fright(h.get(), p.smith.c_str(), 4), FWGC_BAD_ARGUMENT, "a fright off the ladder");
    check::equal(fwgc_resolve(h.get(), "no-such-work", 1), FWGC_BAD_ARGUMENT, "work never taken");
    check::equal(fwgc_account(h.get(), "no-such-work", nullptr, "many", nullptr, nullptr), FWGC_BAD_ARGUMENT, "an account of work never done");
    check::equal(fwgc_account(h.get(), nullptr, nullptr, nullptr, nullptr, nullptr), FWGC_BAD_ARGUMENT, "an account of no work");
    check::equal(fwgc_gather(h.get(), nullptr, 2), FWGC_BAD_ARGUMENT, "no list of people");
    check::equal(fwgc_day_of(h.get(), p.smith.c_str(), std::numeric_limits<double>::infinity(), nullptr, 0, &count),
                 FWGC_BAD_ARGUMENT, "a day that is not a day");
    check::equal(fwgc_clock(h.get(), nullptr), FWGC_BAD_ARGUMENT, "nowhere to put the clock");
    check::equal(fwgc_save(nullptr, &out, nullptr), FWGC_BAD_ARGUMENT, "saving no director");
    check::equal(fwgc_positions(h.get(), nullptr, 0, nullptr), FWGC_BAD_ARGUMENT, "nowhere to put the count");
    check::equal(fwgc_query(h.get(), "DELETE FROM events", &out), FWGC_BAD_ARGUMENT, "a query that writes");
    check::equal(fwgc_query(h.get(), "SELECT 1", nullptr), FWGC_BAD_ARGUMENT, "nowhere to put the rows");
    check::equal(fwgc_query(h.get(), nullptr, &out), FWGC_BAD_ARGUMENT, "no query");
    check::equal(fwgc_related(h.get(), "event-none", &out), FWGC_BAD_ARGUMENT, "an event there is none of");
    check::equal(fwgc_report_event(h.get(), p.site.c_str(), "won", "player"), FWGC_BAD_ARGUMENT, "an outcome off the list");
    check::equal(fwgc_report_event(h.get(), "nowhere-at-all", "beaten", "player"), FWGC_BAD_ARGUMENT, "a place the world does not name");
    check::equal(fwgc_report_event(h.get(), p.site.c_str(), "beaten", "npc-nobody-at-all"), FWGC_BAD_ARGUMENT, "a doer nobody is");
    const char* nobody[] = {"npc-nobody-at-all"};
    check::equal(fwgc_share(h.get(), nobody, 1, &out), FWGC_BAD_ARGUMENT, "sharing with nobody");
    check::equal(fwgc_share(h.get(), nullptr, 1, &out), FWGC_BAD_ARGUMENT, "no list to share with");
    const char* no_hero[] = {"hero-nobody-at-all"};
    check::equal(fwgc_direct_heroes(h.get(), no_hero, 1, &out), FWGC_BAD_ARGUMENT, "directing a hero nobody is");
    check::equal(fwgc_direct_heroes(h.get(), nullptr, 1, &out), FWGC_BAD_ARGUMENT, "no list of heroes");
    check::equal(fwgc_reaction_brief(h.get(), "hero-nobody-at-all", "event-1", &out), FWGC_BAD_ARGUMENT, "a brief for a hero nobody is");
    check::equal(fwgc_reaction_brief(h.get(), p.giver1.c_str(), "event-none", &out), FWGC_BAD_ARGUMENT, "a brief for a deed there is none of");
    check::equal(fwgc_reaction_brief(h.get(), p.giver1.c_str(), "event-none", nullptr), FWGC_BAD_ARGUMENT, "nowhere to put the brief");
    check::equal(fwgc_react(h.get(), p.giver1.c_str(), "event-none", nullptr, &out), FWGC_BAD_ARGUMENT, "a deed there is none of");
    check::equal(fwgc_react(h.get(), p.giver1.c_str(), "event-none", "furious", &out), FWGC_BAD_ARGUMENT, "a reaction with no name");
    check::equal(fwgc_react(h.get(), nullptr, "event-none", nullptr, &out), FWGC_BAD_ARGUMENT, "no hero");
    check::equal(fwgc_words(h.get(), "a line", "a reply", nullptr), FWGC_BAD_ARGUMENT, "words, with nothing said");
    check::equal(fwgc_compact(h.get(), "npc-nobody-at-all", nullptr, &out), FWGC_BAD_ARGUMENT, "compacting nobody");
    check::equal(fwgc_compact(h.get(), nullptr, nullptr, &out), FWGC_BAD_ARGUMENT, "compacting no one");
    for (const char* outcomes : {"not json", "[]", "{\"said\": 3}", "{\"said\": [{\"quote\": 3}]}", "{\"said\": [{\"words\": \"x\"}]}",
                                 "{\"last\": [1]}", "{\"story\": \"x\"}"})
        check::equal(fwgc_compact(h.get(), p.smith.c_str(), outcomes, &out), FWGC_BAD_ARGUMENT, std::string("outcomes: ") + outcomes);
    check::equal(fwgc_memories(h.get(), p.smith.c_str(), nullptr), FWGC_BAD_ARGUMENT, "nowhere to put the memories");
    check::is_true(!error_text().empty(), "a reason given");
    check::equal(save_of(h), before, "nothing recorded by any of them");
    check::equal(error_text(), std::string(), "and a success clears the reason");
}

TEST(a_buffer_takes_what_fits_and_the_count_says_how_many) {
    const Handle h = created();
    std::size_t count = 0;
    check::equal(fwgc_positions(h.get(), nullptr, 0, &count), FWGC_BUFFER_TOO_SMALL, "no room");
    const std::size_t n = count;
    check::is_true(n > 1, "the premise: posts with a day");
    fwgc_position sentinel{};
    sentinel.person = 0xDEADBEEFu;
    std::vector<fwgc_position> room(n + 3, sentinel);
    check::equal(fwgc_positions(h.get(), room.data(), n - 1, &count), FWGC_BUFFER_TOO_SMALL, "one short");
    check::is_true(count == n && room[n - 1].person == 0xDEADBEEFu && room[n - 2].person != 0xDEADBEEFu,
                   "the count whole, and nothing written past what fitted");
    room.assign(n + 3, sentinel);
    succeeded(fwgc_positions(h.get(), room.data(), n + 3, &count), "room to spare");
    check::is_true(count == n && room[n - 1].person != 0xDEADBEEFu && room[n].person == 0xDEADBEEFu &&
                       room[n + 2].person == 0xDEADBEEFu,
                   "every position, and the spare room untouched");
    check::equal(fwgc_positions(h.get(), nullptr, 5, &count), FWGC_BAD_ARGUMENT, "room claimed with no buffer");
}

TEST(a_failed_change_poisons_its_handle_and_only_that) {
    using orchestrator::c_api::enter;
    struct Fake {
        bool poisoned = false;
    };
    const auto ok_work = []() -> fwgc_status { return FWGC_OK; };
    Fake a;
    check::equal(enter(&a, true, false, []() -> fwgc_status { throw orchestrator::Error("a mistake"); }), FWGC_BAD_ARGUMENT,
                 "the director's Error");
    check::is_true(!a.poisoned, "a mistake poisons nothing");
    check::equal(enter(&a, false, false, []() -> fwgc_status { throw std::runtime_error("a read failed"); }), FWGC_FAILED,
                 "a read's failure");
    check::is_true(!a.poisoned, "a read poisons nothing");
    check::equal(enter(&a, true, false,
                       []() -> fwgc_status { throw fwg::WorldTimeError("INVALID_INPUT", "world.quests", "refused"); }),
                 FWGC_FAILED, "a world refusal the director let escape");
    check::is_true(a.poisoned, "poisons a change");
    check::equal(enter(&a, false, false, ok_work), FWGC_POISONED, "then refuses");
    check::equal(enter(&a, false, true, ok_work), FWGC_OK, "but still saves");
    Fake b;
    check::equal(enter(&b, true, false, []() -> fwgc_status { throw std::bad_alloc(); }), FWGC_OUT_OF_MEMORY, "out of memory");
    check::is_true(b.poisoned, "poisons a change");
    Fake c;
    check::equal(enter(&c, true, false, []() -> fwgc_status { throw 42; }), FWGC_FAILED, "a throw of no exception at all");
    // This guard is the test's own copy, so its reason is too (the DLL keeps its own).
    check::is_true(c.poisoned && !orchestrator::c_api::last_error().empty(), "poisons, and says so");
    Fake d;
    check::equal(enter(&d, true, false, ok_work), FWGC_OK, "a handle that did nothing wrong");
    check::is_true(!d.poisoned, "stays whole");
}

#if defined(_MSC_VER)
TEST(the_callers_float_settings_change_nothing_and_come_back) {
    const std::string plain = start_digest_in(save_of(created()));
    unsigned int before = 0, now = 0;
    _controlfp_s(&before, 0, 0);
    constexpr unsigned int kMask = _MCW_RC | _MCW_DN;
    _controlfp_s(&now, _RC_CHOP | _DN_FLUSH, kMask);
    Handle h;
    const fwgc_status made = fwgc_create(kRequest.c_str(), h.reset());
    unsigned int after = 0;
    _controlfp_s(&after, 0, 0);
    _controlfp_s(&now, before, kMask);
    succeeded(made, "a world made under the caller's odd settings");
    check::equal(after & kMask, static_cast<unsigned int>(_RC_CHOP | _DN_FLUSH), "the caller's settings given back");
    check::equal(start_digest_in(save_of(h)), plain, "the same world as under plain ones");
}
#endif

TEST(the_roster_indexes_the_world_s_people) {
    const Handle h = created();
    char* json = nullptr;
    succeeded(fwgc_roster(h.get(), &json), "fwgc_roster");
    const fwg::JsonValue roster = fwg::parse_json(taken(json));
    const Director d(made_here());
    const std::vector<orchestrator::Position> cpp = d.positions();
    check::equal(roster.size(), cpp.size(), "one row for each post with a day");
    check::is_true(!cpp.empty(), "the premise: posts with a day");
    const auto text_or_null = [](const std::optional<std::string>& text) {
        return text ? fwg::JsonValue(*text) : fwg::JsonValue(nullptr);
    };
    for (std::size_t i = 0; i < cpp.size() && i < roster.size(); ++i) {
        const fwg::JsonValue& row = roster.at(i);
        keys_are(row, {"index", "uid", "name", "post", "site", "workplace", "workplace_plot", "home", "home_plot"}, "a roster row");
        const fwg::NpcPerson& person = d.world().npcs.people.at(cpp[i].person);
        check::equal(row.at("index").as_int(), std::int64_t(cpp[i].person), "the index positions give");
        check::equal(row.at("uid").as_string(), person.uid, "the uid at that index");
        check::is_true(row.at("name") == text_or_null(person.name) && row.at("post").as_string() == person.post &&
                           row.at("site") == text_or_null(person.site_uid) && row.at("workplace") == text_or_null(person.workplace) &&
                           row.at("workplace_plot").as_string() == person.plot_id && row.at("home") == text_or_null(person.home) &&
                           row.at("home_plot") == text_or_null(person.home_plot_id),
                       person.uid + ": the roster's own words");
    }
}

TEST(text_crosses_as_utf8_and_frees_safely) {
    const Players& p = players();
    const Handle h = created();
    const std::string far_off = "\xC3\x96lm\xC3\xBCtz \xE2\x80\x94 \xF0\x9F\x8F\xB0";  // "Ölmütz — " and a castle
    char* text = nullptr;
    succeeded(fwgc_respond(h.get(), p.smith.c_str(), "ask_about_place", far_off.c_str(), nullptr, &text),
              "a place nobody knows, named in UTF-8");
    fwgc_free(text);
    const std::string save = save_of(h);
    check::is_true(save.find("\\u00d6") != std::string::npos, "the save ASCII only, the name escaped");
    for (const char c : save) check::is_true(static_cast<unsigned char>(c) < 0x80, "nothing but ASCII in a save");
    const Handle again = restored(save);
    check::equal(save_of(again), save, "the name back whole");
    fwgc_free(nullptr);
    fwgc_destroy(nullptr);
}

TEST(two_threads_making_worlds_at_once_make_the_same_world) {
    Handle a, b;
    fwgc_status sa = FWGC_FAILED, sb = FWGC_FAILED;
    std::thread first([&] { sa = fwgc_create(kRequest.c_str(), a.reset()); });
    std::thread second([&] { sb = fwgc_create(kRequest.c_str(), b.reset()); });
    first.join();
    second.join();
    check::is_true(sa == FWGC_OK && sb == FWGC_OK, "both made");
    check::equal(start_digest_in(save_of(a)), start_digest_in(save_of(b)), "the same world");
}
