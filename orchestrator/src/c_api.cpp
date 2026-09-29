// fwgc_director.h over orchestrator::Director. Every entry point runs through enter()
// (c_guard.hpp): no exception crosses, one lock, one float environment, poisoning.
#include "orchestrator/fwgc_director.h"

#include <atomic>
#include <cstdlib>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "c_guard.hpp"
#include "c_json.hpp"
#include "c_save.hpp"
#include "fwg/api/json_value.hpp"
#include "fwg/api/request_json.hpp"
#include "fwg/api/workers.hpp"
#include "fwg/api/world.hpp"
#include "orchestrator/director.hpp"

// A director, the request its world was made from, and the digest of that world as made: all a
// save needs beyond the inputs the director keeps.
struct fwgc_director {
    orchestrator::Director director;
    fwg::JsonValue request;
    std::string start_digest;
    bool poisoned = false;
};

namespace {

using orchestrator::c_api::BadArgument;
using orchestrator::c_api::enter;
using orchestrator::c_api::Incompatible;
using orchestrator::c_api::kNoHandle;
using orchestrator::c_api::Refused;

// How many threads making a world may use; 0 is every hardware thread.
std::atomic<unsigned> g_worker_threads{0};

void c_need(const void* pointer, const char* what) {
    if (pointer == nullptr) throw BadArgument(std::string(what) + " is NULL");
}

// A world from a request. A request the generator will not make is Refused, or, restoring a
// save, Incompatible.
fwg::World c_made_world(const fwg::JsonValue& request, bool restoring) {
    const fwg::Request asked = [&] {
        try {
            return fwg::request_from_json(request);
        } catch (const fwg::PatchError& e) {
            if (restoring) throw Incompatible(std::string("the save's request is refused: ") + e.what());
            throw Refused(orchestrator::c_api::dumped(fwg::refusal_document(e.refusal())));
        }
    }();
    const fwg::Config config = [&] {
        try {
            return fwg::resolve(asked);
        } catch (const std::invalid_argument& e) {
            if (restoring) throw Incompatible(std::string("the save's request is refused: ") + e.what());
            throw Refused(e.what());
        }
    }();
    fwg::set_worker_threads(g_worker_threads.load());
    return fwg::generate(config);
}

// Up to `capacity` of `items`, converted, into `out`; the full count in *count.
template <typename Item, typename Out, typename Convert>
fwgc_status c_filled(const std::vector<Item>& items, Out* out, size_t capacity, size_t* count, Convert convert) {
    c_need(count, "count");
    if (capacity > 0) c_need(out, "out");
    *count = items.size();
    const size_t written = items.size() < capacity ? items.size() : capacity;
    for (size_t i = 0; i < written; ++i) out[i] = convert(items[i]);
    if (items.size() <= capacity) return FWGC_OK;
    return orchestrator::c_api::failed(FWGC_BUFFER_TOO_SMALL, "the buffer holds " + std::to_string(capacity) + ", and there are " +
                                                                   std::to_string(items.size()));
}

// The roster index of `uid`, whom the roster names.
uint32_t c_index_of(const fwg::World& world, const std::string& uid) {
    const std::vector<fwg::NpcPerson>& people = world.npcs.people;
    for (size_t i = 0; i < people.size(); ++i) {
        if (people[i].uid == uid) return static_cast<uint32_t>(i);
    }
    throw std::logic_error("a post with a day the roster does not name: " + uid);
}

fwgc_whereabouts c_whereabouts(const orchestrator::Whereabouts& where, uint32_t index) {
    const auto& [person, site, workplace_plot, home_plot, day, along, stint] = where;
    fwgc_whereabouts out{};
    out.person = index;
    out.reserved = 0;
    out.day = day;
    out.along = along;
    out.stint = orchestrator::c_api::stint_of(stint);
    return out;
}

}  // namespace

extern "C" {

FWGC_API int32_t fwgc_api_version(void) noexcept { return FWGC_API_VERSION; }

FWGC_API int32_t fwgc_contract_version(void) noexcept { return orchestrator::kContractVersion; }

FWGC_API void fwgc_free(void* memory) noexcept { std::free(memory); }

FWGC_API const char* fwgc_last_error(void) noexcept { return orchestrator::c_api::last_error().c_str(); }

FWGC_API void fwgc_set_worker_threads(uint32_t count) noexcept { g_worker_threads.store(count); }

FWGC_API fwgc_status fwgc_contract_json(char** json) noexcept {
    return enter(kNoHandle, false, false, [&]() -> fwgc_status {
        c_need(json, "json");
        *json = nullptr;
        *json = orchestrator::c_api::handed_over(orchestrator::c_api::dumped(orchestrator::c_api::contract_json()));
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_create(const char* request_json, fwgc_director** director) noexcept {
    if (director != nullptr) *director = nullptr;
    return enter(kNoHandle, false, false, [&]() -> fwgc_status {
        c_need(director, "director");
        const std::string text = orchestrator::c_api::required_text(request_json, "request_json");
        fwg::JsonValue request = [&] {
            try {
                return fwg::parse_json(text);
            } catch (const fwg::JsonError& e) {
                throw BadArgument(std::string("the request is not JSON: ") + e.what());
            }
        }();
        fwg::World world = c_made_world(request, false);
        std::string digest = orchestrator::c_api::world_digest(world);
        *director = new fwgc_director{orchestrator::Director(std::move(world)), std::move(request), std::move(digest), false};
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_restore(const char* save_json, fwgc_director** director) noexcept {
    if (director != nullptr) *director = nullptr;
    return enter(kNoHandle, false, false, [&]() -> fwgc_status {
        c_need(director, "director");
        const std::string text = orchestrator::c_api::required_text(save_json, "save_json");
        orchestrator::c_api::Save save = orchestrator::c_api::read_save(text);
        fwg::World world = c_made_world(save.request, true);
        std::string digest = orchestrator::c_api::world_digest(world);
        if (digest != save.start_digest)
            throw Incompatible("the save's world comes out differently here (" + digest + ", where the save has " +
                               save.start_digest + "): another build of the library, or a C runtime that computes "
                               "differently");
        orchestrator::Director restored = [&] {
            try {
                return orchestrator::replay(std::move(world), save.inputs);
            } catch (const std::bad_alloc&) {
                throw;
            } catch (const std::exception& e) {
                throw Incompatible(std::string("the save does not replay here: ") + e.what());
            }
        }();
        if (orchestrator::c_api::clock_day(restored) != save.day ||
            orchestrator::c_api::operation_count(restored) != save.operations)
            throw Incompatible("the save replays to a different world than it was saved from");
        *director = new fwgc_director{std::move(restored), std::move(save.request), std::move(digest), false};
        return FWGC_OK;
    });
}

FWGC_API void fwgc_destroy(fwgc_director* director) noexcept {
    if (director == nullptr) return;
    (void)enter(kNoHandle, false, false, [&]() -> fwgc_status {
        delete director;
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_save(const fwgc_director* director, char** json, size_t* size) noexcept {
    return enter(director, false, /*saving=*/true, [&]() -> fwgc_status {
        c_need(director, "director");
        c_need(json, "json");
        *json = nullptr;
        const std::string text = orchestrator::c_api::save_text(director->request, director->start_digest, director->director);
        *json = orchestrator::c_api::handed_over(text);
        if (size != nullptr) *size = text.size();
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_world_json(const fwgc_director* director, char** json, size_t* size) noexcept {
    return enter(director, false, false, [&]() -> fwgc_status {
        c_need(director, "director");
        c_need(json, "json");
        *json = nullptr;
        *json = orchestrator::c_api::handed_world(director->director.world(), size);
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_clock(const fwgc_director* director, double* day) noexcept {
    return enter(director, false, false, [&]() -> fwgc_status {
        c_need(director, "director");
        c_need(day, "day");
        *day = orchestrator::c_api::clock_day(director->director);
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_advance(fwgc_director* director, double days) noexcept {
    return enter(director, true, false, [&]() -> fwgc_status {
        c_need(director, "director");
        director->director.advance(days);
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_respond(fwgc_director* director, const char* speaker, const char* act, const char* target,
                                  char** json_out, char** text_out) noexcept {
    if (json_out != nullptr) *json_out = nullptr;
    if (text_out != nullptr) *text_out = nullptr;
    return enter(director, true, false, [&]() -> fwgc_status {
        c_need(director, "director");
        const std::string who = orchestrator::c_api::required_text(speaker, "speaker");
        const orchestrator::DialogueAct said{orchestrator::c_api::act_named(orchestrator::c_api::required_text(act, "act")),
                                             orchestrator::c_api::optional_text(target, "target")};
        const orchestrator::FactPacket packet = director->director.respond(who, said);
        // The act is recorded now; what follows only hands the answer over.
        const std::string as_json = json_out ? orchestrator::c_api::dumped(orchestrator::c_api::packet_json(packet)) : std::string();
        const std::string as_text = text_out ? orchestrator::packet_text(packet) : std::string();
        char* json_copy = json_out ? orchestrator::c_api::handed_over(as_json) : nullptr;
        char* text_copy = nullptr;
        try {
            if (text_out != nullptr) text_copy = orchestrator::c_api::handed_over(as_text);
        } catch (...) {
            std::free(json_copy);
            throw;
        }
        if (json_out != nullptr) *json_out = json_copy;
        if (text_out != nullptr) *text_out = text_copy;
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_resolve(fwgc_director* director, const char* quest_id, int32_t completed) noexcept {
    return enter(director, true, false, [&]() -> fwgc_status {
        c_need(director, "director");
        director->director.resolve(orchestrator::c_api::required_text(quest_id, "quest_id"), completed != 0);
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_account(fwgc_director* director, const char* quest_id, const char* came, const char* many,
                                  const char* cost, const char* part) noexcept {
    return enter(director, true, false, [&]() -> fwgc_status {
        c_need(director, "director");
        director->director.account(orchestrator::c_api::required_text(quest_id, "quest_id"), orchestrator::c_api::optional_text(came, "came"),
                                   orchestrator::c_api::optional_text(many, "many"), orchestrator::c_api::optional_text(cost, "cost"),
                                   orchestrator::c_api::optional_text(part, "part"));
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_hold(fwgc_director* director, const char* person, double minutes) noexcept {
    return enter(director, true, false, [&]() -> fwgc_status {
        c_need(director, "director");
        director->director.hold(orchestrator::c_api::required_text(person, "person"), minutes);
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_fright(fwgc_director* director, const char* person, int32_t level) noexcept {
    return enter(director, true, false, [&]() -> fwgc_status {
        c_need(director, "director");
        director->director.fright(orchestrator::c_api::required_text(person, "person"), level);
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_gather(fwgc_director* director, const char* const* people, size_t count) noexcept {
    return enter(director, true, false, [&]() -> fwgc_status {
        c_need(director, "director");
        if (count > 0) c_need(people, "people");
        std::vector<std::string> names;
        names.reserve(count);
        for (size_t i = 0; i < count; ++i)
            names.push_back(orchestrator::c_api::required_text(people[i], "people[" + std::to_string(i) + "]"));
        director->director.gather(names);
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_overhear(fwgc_director* director, const char* const* people, size_t count,
                                   char** json_out, char** text_out) noexcept {
    if (json_out != nullptr) *json_out = nullptr;
    if (text_out != nullptr) *text_out = nullptr;
    return enter(director, true, false, [&]() -> fwgc_status {
        c_need(director, "director");
        if (count > 0) c_need(people, "people");
        std::vector<std::string> names;
        names.reserve(count);
        for (size_t i = 0; i < count; ++i)
            names.push_back(orchestrator::c_api::required_text(people[i], "people[" + std::to_string(i) + "]"));
        const std::vector<orchestrator::Bystander> bystanders = director->director.overhear(names);
        // Recorded now; what follows only hands the answer over.
        std::string joined;
        for (const orchestrator::Bystander& bystander : bystanders) {
            if (bystander.packet) joined = orchestrator::packet_text(*bystander.packet);
        }
        const std::string as_json = json_out ? orchestrator::c_api::dumped(orchestrator::c_api::bystanders_json(bystanders)) : std::string();
        char* json_copy = json_out ? orchestrator::c_api::handed_over(as_json) : nullptr;
        char* text_copy = nullptr;
        try {
            if (text_out != nullptr) text_copy = orchestrator::c_api::handed_over(joined);
        } catch (...) {
            std::free(json_copy);
            throw;
        }
        if (json_out != nullptr) *json_out = json_copy;
        if (text_out != nullptr) *text_out = text_copy;
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_share(fwgc_director* director, const char* const* people, size_t count, char** json_out) noexcept {
    if (json_out != nullptr) *json_out = nullptr;
    return enter(director, true, false, [&]() -> fwgc_status {
        c_need(director, "director");
        if (count > 0) c_need(people, "people");
        std::vector<std::string> names;
        names.reserve(count);
        for (size_t i = 0; i < count; ++i)
            names.push_back(orchestrator::c_api::required_text(people[i], "people[" + std::to_string(i) + "]"));
        const std::vector<orchestrator::Learned> learned = director->director.share(names);
        // Recorded now; what follows only hands the answer over.
        if (json_out != nullptr)
            *json_out = orchestrator::c_api::handed_over(orchestrator::c_api::dumped(orchestrator::c_api::learned_json(learned)));
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_direct_heroes(fwgc_director* director, const char* const* heroes, size_t count, char** json_out) noexcept {
    if (json_out != nullptr) *json_out = nullptr;
    return enter(director, true, false, [&]() -> fwgc_status {
        c_need(director, "director");
        if (count > 0) c_need(heroes, "heroes");
        std::vector<std::string> names;
        names.reserve(count);
        for (size_t i = 0; i < count; ++i)
            names.push_back(orchestrator::c_api::required_text(heroes[i], "heroes[" + std::to_string(i) + "]"));
        const std::vector<orchestrator::HeroDone> done = director->director.direct_heroes(names);
        // Recorded now; what follows only hands the answer over.
        if (json_out != nullptr)
            *json_out = orchestrator::c_api::handed_over(orchestrator::c_api::dumped(orchestrator::c_api::heroes_done_json(done)));
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_report_event(fwgc_director* director, const char* site, const char* outcome, const char* by) noexcept {
    return enter(director, true, false, [&]() -> fwgc_status {
        c_need(director, "director");
        director->director.report_event(orchestrator::c_api::required_text(site, "site"),
                                        orchestrator::c_api::required_text(outcome, "outcome"),
                                        orchestrator::c_api::optional_text(by, "by"));
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_words(fwgc_director* director, const char* line, const char* reply, const char* asked) noexcept {
    return enter(director, true, false, [&]() -> fwgc_status {
        c_need(director, "director");
        director->director.words(orchestrator::c_api::optional_text(line, "line"), orchestrator::c_api::optional_text(reply, "reply"),
                                 orchestrator::c_api::optional_text(asked, "asked"));
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_compact(fwgc_director* director, const char* person, const char* outcomes, char** json_out) noexcept {
    if (json_out != nullptr) *json_out = nullptr;
    return enter(director, true, false, [&]() -> fwgc_status {
        c_need(director, "director");
        const orchestrator::Compacted compacted =
            director->director.compact(orchestrator::c_api::required_text(person, "person"),
                                       orchestrator::c_api::optional_text(outcomes, "outcomes"));
        // Recorded now; what follows only hands the answer over.
        if (json_out != nullptr)
            *json_out = orchestrator::c_api::handed_over(orchestrator::c_api::dumped(orchestrator::c_api::compacted_json(compacted)));
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_memories(const fwgc_director* director, const char* person, char** json) noexcept {
    return enter(director, false, false, [&]() -> fwgc_status {
        c_need(director, "director");
        c_need(json, "json");
        *json = nullptr;
        const std::vector<orchestrator::Remembered> memories =
            director->director.memories(orchestrator::c_api::required_text(person, "person"));
        *json = orchestrator::c_api::handed_over(orchestrator::c_api::dumped(orchestrator::c_api::remembered_json(memories)));
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_related(const fwgc_director* director, const char* event, char** json) noexcept {
    return enter(director, false, false, [&]() -> fwgc_status {
        c_need(director, "director");
        c_need(json, "json");
        *json = nullptr;
        const orchestrator::Related related = director->director.related(orchestrator::c_api::required_text(event, "event"));
        *json = orchestrator::c_api::handed_over(orchestrator::c_api::dumped(orchestrator::c_api::related_json(related)));
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_query(const fwgc_director* director, const char* sql, char** json) noexcept {
    return enter(director, false, false, [&]() -> fwgc_status {
        c_need(director, "director");
        c_need(json, "json");
        *json = nullptr;
        const orchestrator::QueryResult rows = director->director.query(orchestrator::c_api::required_text(sql, "sql"));
        *json = orchestrator::c_api::handed_over(orchestrator::c_api::dumped(orchestrator::c_api::query_json(rows)));
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_acquaintance(const fwgc_director* director, const char* speaker, char** json) noexcept {
    return enter(director, false, false, [&]() -> fwgc_status {
        c_need(director, "director");
        c_need(json, "json");
        *json = nullptr;
        const orchestrator::Director::Acquaintance* known =
            director->director.acquaintance(orchestrator::c_api::required_text(speaker, "speaker"));
        *json = orchestrator::c_api::handed_over(known ? orchestrator::c_api::dumped(orchestrator::c_api::acquaintance_json(*known))
                                                       : std::string("null"));
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_feeling(const fwgc_director* director, const char* person, char** json) noexcept {
    return enter(director, false, false, [&]() -> fwgc_status {
        c_need(director, "director");
        c_need(json, "json");
        *json = nullptr;
        const orchestrator::Feeling feeling = director->director.feeling(orchestrator::c_api::required_text(person, "person"));
        *json = orchestrator::c_api::handed_over(orchestrator::c_api::dumped(orchestrator::c_api::feeling_json(feeling)));
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_positions(const fwgc_director* director, fwgc_position* out, size_t capacity, size_t* count) noexcept {
    return enter(director, false, false, [&]() -> fwgc_status {
        c_need(director, "director");
        return c_filled(director->director.positions(), out, capacity, count, [](const orchestrator::Position& at) {
            const auto& [person, stint] = at;
            fwgc_position one{};
            one.person = person;
            one.reserved = 0;
            one.stint = orchestrator::c_api::stint_of(stint);
            return one;
        });
    });
}

FWGC_API fwgc_status fwgc_whereabouts_of(const fwgc_director* director, const char* person, fwgc_whereabouts* out,
                                         int32_t* has_day) noexcept {
    return enter(director, false, false, [&]() -> fwgc_status {
        c_need(director, "director");
        c_need(out, "out");
        c_need(has_day, "has_day");
        const std::string who = orchestrator::c_api::required_text(person, "person");
        *has_day = 0;
        const std::optional<orchestrator::Whereabouts> where = director->director.whereabouts(who);
        if (!where) return FWGC_OK;
        *out = c_whereabouts(*where, c_index_of(director->director.world(), who));
        *has_day = 1;
        return FWGC_OK;
    });
}

FWGC_API fwgc_status fwgc_whereabouts_at(const fwgc_director* director, const char* site, fwgc_whereabouts* out,
                                         size_t capacity, size_t* count) noexcept {
    return enter(director, false, false, [&]() -> fwgc_status {
        c_need(director, "director");
        const std::vector<orchestrator::Whereabouts> all =
            director->director.whereabouts_at(orchestrator::c_api::required_text(site, "site"));
        // In roster order, so one walk down the roster finds every index.
        const std::vector<fwg::NpcPerson>& people = director->director.world().npcs.people;
        std::vector<uint32_t> indices;
        indices.reserve(all.size());
        size_t next = 0;
        for (const orchestrator::Whereabouts& where : all) {
            while (next < people.size() && people[next].uid != where.person) ++next;
            if (next == people.size()) throw std::logic_error("whereabouts out of roster order: " + where.person);
            indices.push_back(static_cast<uint32_t>(next++));
        }
        size_t k = 0;
        return c_filled(all, out, capacity, count, [&](const orchestrator::Whereabouts& where) {
            return c_whereabouts(where, indices[k++]);
        });
    });
}

FWGC_API fwgc_status fwgc_day_of(const fwgc_director* director, const char* person, double day, fwgc_stint* out,
                                 size_t capacity, size_t* count) noexcept {
    return enter(director, false, false, [&]() -> fwgc_status {
        c_need(director, "director");
        const std::vector<orchestrator::Stint> stints =
            director->director.day_of(orchestrator::c_api::required_text(person, "person"), day);
        return c_filled(stints, out, capacity, count, [](const orchestrator::Stint& s) { return orchestrator::c_api::stint_of(s); });
    });
}

FWGC_API fwgc_status fwgc_roster(const fwgc_director* director, char** json) noexcept {
    return enter(director, false, false, [&]() -> fwgc_status {
        c_need(director, "director");
        c_need(json, "json");
        *json = nullptr;
        *json = orchestrator::c_api::handed_over(
            orchestrator::c_api::dumped(orchestrator::c_api::roster_json(director->director.world(), director->director.positions())));
        return FWGC_OK;
    });
}

}  // extern "C"
