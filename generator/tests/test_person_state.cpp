// The person channel: one liveness vocabulary in, four block-local ones out.
//
// The tests that matter are the ones about the seam rather than the effect: a write goes
// through the liveness predicate and never around it, no sixth vocabulary is published, a
// person marked gone reads gone through every read and every liveness question and reads
// present again when marked back, the caller's world is never touched, and the one log records
// exactly the calls that changed something, in order.
#include "check.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "fwg/person_state.hpp"
#include "fwg/read.hpp"
#include "fwg/serialize.hpp"
#include "fwg/world_time.hpp"

using namespace fwg;

namespace {

World generated(int size) {
    Request r;
    r.seed = 42;
    r.size = size;
    r.phase = 16;
    return generate(resolve(r));
}

const World& genesis() {
    static const World world = generated(33);
    return world;
}

TimeAdvanceRequest days(double value) {
    TimeAdvanceRequest request;
    request.elapsed = TimeElapsed{TimeUnit::Days, value};
    return request;
}

// A month on, so the quest board is open.
const World& ticked() {
    static const World world = advance_world_time(genesis(), days(30.0)).world;
    return world;
}

PersonChange change(const std::string& uid, Liveness status, const std::string& reason = "slain in play",
                    double day = 5.0) {
    PersonChange out;
    out.uid = uid;
    out.status = status;
    out.reason = reason;
    out.day = day;
    return out;
}

PersonStateResult call(const World& world, std::vector<PersonChange> changes) {
    PersonStateRequest request;
    request.changes = std::move(changes);
    return person_state_request(world, request);
}

template <typename Fn>
WorldTimeError refused(Fn&& fn, const std::string& what) {
    try {
        fn();
    } catch (const WorldTimeError& refusal) {
        return refusal;
    }
    check::fail(what + ": expected a WorldTimeError, none thrown");
}

template <typename T>
const T& must(const std::optional<T>& value, const std::string& what) {
    if (!value) check::fail("expected " + what);
    return *value;
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// Two long texts are equal, or the failure says where they first differ -- never the whole
// texts, which are world documents.
void same_text(const std::string& actual, const std::string& expected, const std::string& what) {
    if (actual == expected) return;
    const auto first = std::mismatch(actual.begin(), actual.end(), expected.begin(), expected.end());
    const auto at = static_cast<std::size_t>(first.first - actual.begin());
    const auto near = [at](const std::string& s) { return at < s.size() ? s.substr(at, 80) : std::string(); };
    check::fail(what + ": differs at byte " + std::to_string(at) + " of " + std::to_string(actual.size()) + " vs " +
                std::to_string(expected.size()) + ": '" + near(actual) + "' vs '" + near(expected) + "'");
}

// The whole caller-visible world: the document with its timings, and the time API's blocks
// the document does not carry.
std::string everything_of(const World& w) {
    std::ostringstream o;
    o << std::setprecision(17);
    write_world_json(o, w, /*pretty=*/false, /*include_timings=*/true);
    if (w.world_clock) o << "clock " << w.world_clock->day << ' ' << w.world_clock->age << '\n';
    if (w.quests) {
        for (const QuestRecord& q : w.quests->quests) {
            o << q.quest_id << ' ' << to_string(q.state) << ' ' << q.giver_uid.value_or("-") << '\n';
        }
    }
    o << "ops " << w.history.operations.size() << ' ' << w.history.replay << '\n';
    return o.str();
}

std::string history_json(const World& w) {
    std::ostringstream o;
    write_world_json(o, w, /*pretty=*/false);
    const std::string doc = o.str();
    const std::size_t at = doc.find("\"history\":");
    if (at == std::string::npos) check::fail("a history block in the document");
    return doc.substr(at);
}

// The first person of `block` who reads `state`, by the predicate.
std::string someone(const World& w, LivenessBlock block, Liveness state = Liveness::Present) {
    const auto first = [&](const auto& people) -> std::string {
        for (const auto& p : people) {
            if (liveness(p) == state) return p.uid;
        }
        check::fail("a " + std::string(to_string(block)) + " who is " + std::string(to_string(state)));
    };
    switch (block) {
        case LivenessBlock::Heroes: return first(w.heroes.people);
        case LivenessBlock::Dreads: return first(w.heroes.dreads);
        case LivenessBlock::Npcs: return first(w.npcs.people);
        case LivenessBlock::Villains: break;
    }
    return first(w.villains.people);
}

// Liveness of a uid, read through the read API: the answer every consumer gets.
Liveness read_liveness(const World& w, const std::string& uid) {
    PersonReadRequest request;
    request.uid = uid;
    return read_person(w, request).person.liveness;
}

template <typename Op>
std::vector<const Op*> ops_of(const World& w) {
    std::vector<const Op*> out;
    for (const HistoryOperation& entry : w.history.operations) {
        if (const auto* op = std::get_if<Op>(&entry)) out.push_back(op);
    }
    return out;
}

}  // namespace

// ===========================================================================
// Pure: the vocabulary and the plan
// ===========================================================================

TEST(person_state_speaks_the_predicates_pair_and_no_new_word) {
    check::is_true(kPersonStatuses[0] == Liveness::Present && kPersonStatuses[1] == Liveness::Gone,
                   "STATUSES is the liveness pair");
    const std::vector<std::string_view> published = published_statuses();
    check::is_true(published == std::vector<std::string_view>{"living", "legend", "alive", "dead", "fallen"},
                   "no word that is not already a block's");
    const std::vector<std::string_view> fields(person_change_fields().begin(), person_change_fields().end());
    check::is_true(fields == std::vector<std::string_view>{"uid", "status", "reason", "day"}, "FIELDS in order");
    check::equal(kMaxPersonChanges, std::size_t{1024}, "one batch ceiling");
    check::equal(kMaxPersonReason, std::size_t{200}, "the reason bound");
}

TEST(person_state_a_plan_goes_through_the_predicate_in_every_block) {
    Hero hero;
    hero.uid = "hero-1";
    hero.status = "living";
    const PersonStatePlan<Hero> gone = person_state_plan(hero, Liveness::Gone);
    check::is_true(gone.before == Liveness::Present && gone.after == Liveness::Gone, "present -> gone");
    check::equal(must(gone.record, "a record").status, std::string("legend"), "a hero's word");
    check::equal(hero.status, std::string("living"), "the record given is not modified");
    const PersonStatePlan<Hero> back = person_state_plan(*gone.record, Liveness::Present);
    check::equal(must(back.record, "a record").status, std::string("living"), "and back");

    HeroDread dread;
    dread.status = "living";
    check::equal(must(person_state_plan(dread, Liveness::Gone).record, "a record").status, std::string("legend"),
                 "the dread vocabulary is reachable");

    NpcPerson npc;
    const PersonStatePlan<NpcPerson> dead = person_state_plan(npc, Liveness::Gone);
    check::is_true(!must(dead.record, "a record").alive, "an npc's flag carries the word dead");
    check::equal(token(LivenessBlock::Npcs, liveness(*dead.record)), std::string_view("dead"), "read back as dead");

    Villain villain;
    const PersonStatePlan<Villain> fallen = person_state_plan(villain, Liveness::Gone);
    check::equal(must(fallen.record, "a record").status, std::string("fallen"), "a villain's word");
    check::is_true(!fallen.record->fell_age, "and nothing else: no fall bookkeeping");
}

TEST(person_state_an_absent_status_reads_present_and_a_no_op_writes_nothing) {
    Hero hero;  // no status at all
    const PersonStatePlan<Hero> plan = person_state_plan(hero, Liveness::Present);
    check::is_true(plan.before == Liveness::Present && plan.after == Liveness::Present, "absent reads present");
    check::is_true(!plan.record, "a no-op adds no status to a record that had none");
    Villain villain;
    check::is_true(!person_state_plan(villain, Liveness::Present).record, "an empty villain status is standing");
}

// The typed form of the reference's refusal of a block handed where a person belongs.
template <typename Block>
concept plannable = requires(const Block& b) { person_state_plan(b, Liveness::Gone); };

TEST(person_state_a_block_cannot_be_planned_as_a_person) {
    static_assert(!plannable<HeroesBlock> && !plannable<NpcRoster> && !plannable<VillainBlock>,
                  "a block is not a person");
    static_assert(plannable<Hero> && plannable<HeroDread> && plannable<NpcPerson> && plannable<Villain>,
                  "the check can see a person");
    check::is_true(true, "compile-time");
}

// ===========================================================================
// Pure: shape validation
// ===========================================================================

TEST(person_state_shape_validation_normalises_and_refuses_by_field) {
    const std::vector<PersonChange> good{change("u", Liveness::Gone, "slain", 10.0)};
    const std::vector<PersonChangeEntry> out = validate_changes(good);
    check::equal(out.size(), std::size_t{1}, "one change");
    check::is_true(out[0].uid == "u" && out[0].status == Liveness::Gone && out[0].reason == "slain" &&
                       out[0].day == 10.0,
                   "normalised verbatim");

    const auto field_of = [](PersonChange c) {
        const std::vector<PersonChange> one{std::move(c)};
        return refused([&] { (void)validate_changes(one); }, "a bad change").field();
    };
    PersonChange c = change("u", Liveness::Gone);
    c.uid.clear();
    check::equal(field_of(c), std::string("uid"), "an empty uid");
    c = change("u", Liveness::Gone);
    c.status = static_cast<Liveness>(7);
    check::equal(field_of(c), std::string("status"), "a status outside the pair");
    for (const std::string& blank : {std::string(""), std::string("   "), std::string("\t\n"),
                                     std::string("\xc2\xa0"), std::string("\xe3\x80\x80 \xe2\x80\xa8")}) {
        c = change("u", Liveness::Gone, blank);
        check::equal(field_of(c), std::string("reason"), "a blank reason, Unicode whitespace included");
    }
    for (const double bad : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        c = change("u", Liveness::Gone, "r", bad);
        check::equal(field_of(c), std::string("day"), "a day that is not finite");
    }
    c = change("u", Liveness::Gone, "r", 0.0);
    check::equal(validate_changes(std::vector<PersonChange>{c}).size(), std::size_t{1}, "day 0 is founding year 0");
}

TEST(person_state_the_reason_is_bounded_in_characters_not_bytes) {
    std::string reason;
    for (int i = 0; i < 200; ++i) reason += "\xc3\xa9";  // 200 characters, 400 bytes
    (void)validate_changes(std::vector<PersonChange>{change("u", Liveness::Gone, reason)});
    reason += "e";
    WorldTimeError long_reason =
        refused([&] { (void)validate_changes(std::vector<PersonChange>{change("u", Liveness::Gone, reason)}); },
                "201 characters");
    check::equal(long_reason.field(), std::string("reason"), "names reason");
    check::equal(long_reason.code(), kRefusalStateCapacity, "a capacity bound");
    check::equal(long_reason.received(), std::string("201"), "counted in characters");
}

TEST(person_state_the_batch_is_bounded) {
    std::vector<PersonChange> batch(kMaxPersonChanges, change("u", Liveness::Gone));
    check::equal(validate_changes(batch).size(), kMaxPersonChanges, "the ceiling itself is accepted");
    batch.push_back(change("u", Liveness::Gone));
    WorldTimeError over = refused([&] { (void)validate_changes(batch); }, "one past the ceiling");
    check::equal(over.field(), std::string("changes"), "names changes");
    check::equal(over.code(), kRefusalStateCapacity, "STATE_CAPACITY");
}

// Reference defect REPRODUCED: a missing field is refused as an unknown one, and offered
// itself as the correction.
TEST(person_state_a_missing_field_is_named_in_the_references_odd_shape) {
    PersonChange c = change("u", Liveness::Gone);
    c.day.reset();
    c.status.reset();
    WorldTimeError missing = refused([&] { (void)validate_changes(std::vector<PersonChange>{c}); }, "no status");
    check::equal(missing.field(), std::string("status"), "the first missing field in FIELDS order");
    check::equal(missing.message(), std::string("No person change field named 'status'. Did you mean 'status'?"),
                 "the reference's sentence");
    c.status = Liveness::Gone;
    check::equal(refused([&] { (void)validate_changes(std::vector<PersonChange>{c}); }, "no day").field(),
                 std::string("day"), "then day");
}

// Reference defect REPRODUCED: a negative day is refused as a wrong TYPE, "must be a finite
// number", although -1 is one.
TEST(person_state_a_negative_day_is_refused_with_the_wrong_sentence) {
    WorldTimeError negative = refused(
        [] { (void)validate_changes(std::vector<PersonChange>{change("u", Liveness::Gone, "r", -1.0)}); },
        "a negative day");
    check::equal(negative.field(), std::string("day"), "names day");
    check::equal(negative.message(), std::string("day must be a finite number; received -1."),
                 "the reference's sentence");
}

// ===========================================================================
// On a world
// ===========================================================================

TEST(person_state_a_change_lands_in_each_blocks_own_words) {
    World w = ticked();
    // Every block is exercised, dreads included: a world that happens to have none gets one,
    // rather than letting that branch go untested.
    if (w.heroes.dreads.empty()) {
        HeroDread dread;
        dread.uid = "dread-forged";
        dread.status = "living";
        w.heroes.dreads.push_back(dread);
    }
    for (const LivenessBlock block : liveness_blocks()) {
        const std::string uid = someone(w, block);
        const PersonStateResult out = call(w, {change(uid, Liveness::Gone)});
        const PersonDocument after = must(person_document(out.world, uid), "the person after");
        check::is_true(after.liveness == Liveness::Gone, std::string(to_string(block)) + " reads gone");
        const std::string word = std::visit(
            [](const auto& p) -> std::string {
                if constexpr (std::is_same_v<std::decay_t<decltype(p)>, NpcPerson>) {
                    return p.alive ? "alive" : "dead";
                } else {
                    return p.status;
                }
            },
            after.person);
        check::equal(word, std::string(gone_status(block)), "the block's own word");
        check::equal(out.report.applied, 1, "one applied");
        const PersonStateChangeReport& row = out.report.changes.at(0);
        check::is_true(row.block == block && row.from_status == Liveness::Present && row.to_status == Liveness::Gone &&
                           row.token == gone_status(block) && row.changed,
                       "the report row for " + std::string(to_string(block)));
    }
}

TEST(person_state_gone_reads_gone_everywhere_and_back_reads_present_again) {
    const World& w = ticked();
    // A giver of an open offer, so the quest read and the lifecycle see the change too.
    const std::map<std::string, PersonEntry> index = person_index(w);
    std::string uid;
    for (const QuestRecord& q : w.quests->quests) {
        if (q.state != QuestState::Offered || !q.giver_uid) continue;
        const auto it = index.find(*q.giver_uid);
        if (it != index.end() && it->second.block == LivenessBlock::Heroes &&
            is_present(w.heroes.people.at(it->second.position))) {
            uid = *q.giver_uid;
            break;
        }
    }
    check::is_true(!uid.empty(), "an open offer held by a living hero");
    const std::size_t hero = index.at(uid).position;
    const LivenessCount before = must(census(w).at(LivenessBlock::Heroes), "a heroes census");

    const World gone = call(w, {change(uid, Liveness::Gone)}).world;
    check::is_true(!is_present(gone.heroes.people.at(hero)), "is_present");
    check::equal(liveness(gone.heroes.people.at(hero)) == Liveness::Gone, true, "liveness");
    check::is_true(read_liveness(gone, uid) == Liveness::Gone, "the person read");
    const std::vector<const Hero*> still = present(std::span<const Hero>(gone.heroes.people));
    check::is_true(std::none_of(still.begin(), still.end(), [&](const Hero* h) { return h->uid == uid; }),
                   "present() leaves them out");
    const LivenessCount during = must(census(gone).at(LivenessBlock::Heroes), "a heroes census");
    check::is_true(during.present == before.present - 1 && during.gone == before.gone + 1, "the census moves by one");
    bool seen = false;
    for (const QuestRow& q : read_quests(gone).quests) {
        if (q.giver_uid && *q.giver_uid == uid) {
            seen = true;
            check::is_true(must(q.giver, "a giver").liveness == Liveness::Gone, "the quest read's giver");
        }
    }
    check::is_true(seen, "the quest read names the giver");

    const World back = call(gone, {change(uid, Liveness::Present, "found alive", 6.0)}).world;
    check::is_true(is_present(back.heroes.people.at(hero)), "present again");
    check::is_true(read_liveness(back, uid) == Liveness::Present, "the person read agrees");
    const LivenessCount after = must(census(back).at(LivenessBlock::Heroes), "a heroes census");
    check::is_true(after.present == before.present && after.gone == before.gone, "the census is back");
    for (const QuestRow& q : read_quests(back).quests) {
        if (q.giver_uid && *q.giver_uid == uid) {
            check::is_true(must(q.giver, "a giver").liveness == Liveness::Present, "the giver is back");
        }
    }
    check::equal(back.heroes.people.at(hero).status, std::string("living"), "in the heroes' word");
}

TEST(person_state_an_npc_marked_gone_reads_dead_at_its_post) {
    const World& w = genesis();
    std::string uid, site;
    for (const NpcPerson& p : w.npcs.people) {
        if (p.alive && p.site_uid) {
            uid = p.uid;
            site = *p.site_uid;
            break;
        }
    }
    check::is_true(!uid.empty(), "a living npc at a site");
    const World gone = call(w, {change(uid, Liveness::Gone)}).world;
    const auto status_at_post = [&](const World& world) -> std::string_view {
        const auto found = find_place(world, site);
        if (!found) return "-";  // a site key that is not a place id (a hamlet or fortress key)
        PlaceReadRequest request;
        request.place_id = site;
        for (const NpcPostRow& post : read_place(world, request).place.resolved.npc_posts) {
            if (post.uid == uid) return post.status;
        }
        return "-";
    };
    if (find_place(w, site)) {
        check::equal(status_at_post(gone), std::string_view("dead"), "the roster's own word at the post");
        check::equal(status_at_post(call(gone, {change(uid, Liveness::Present)}).world), std::string_view("alive"),
                     "and back");
    }
    const LivenessCount before = must(census(w).at(LivenessBlock::Npcs), "an npc census");
    const LivenessCount after = must(census(gone).at(LivenessBlock::Npcs), "an npc census");
    check::is_true(after.present == before.present - 1 && after.gone == before.gone + 1, "the census moves by one");
}

TEST(person_state_the_callers_world_is_never_touched_on_any_path) {
    const World& w = ticked();
    const std::string before = everything_of(w);
    const std::string hero = someone(w, LivenessBlock::Heroes);
    (void)call(w, {change(hero, Liveness::Gone)});
    same_text(everything_of(w), before, "a successful call");
    const std::vector<std::vector<PersonChange>> refusals{
        {change("no-such-person", Liveness::Gone)},
        {change(hero, static_cast<Liveness>(3))},
        {change(hero, Liveness::Gone, "x", -1.0)},
        {change(hero, Liveness::Gone), change("no-such-person", Liveness::Gone)},
    };
    for (const std::vector<PersonChange>& changes : refusals) {
        (void)refused([&] { (void)call(w, changes); }, "a refused batch");
        same_text(everything_of(w), before, "a refused call, all or nothing");
    }
}

TEST(person_state_an_unknown_uid_names_the_field_and_suggests) {
    const World& w = ticked();
    const std::string uid = someone(w, LivenessBlock::Npcs);
    const std::string typo = uid.substr(0, uid.size() - 1);
    WorldTimeError unknown = refused([&] { (void)call(w, {change(typo, Liveness::Gone)}); }, "a typo");
    check::equal(unknown.field(), typo, "the unknown uid is the field");
    const std::vector<std::string>& candidates = unknown.candidates();
    check::is_true(std::find(candidates.begin(), candidates.end(), uid) != candidates.end(), "the real uid is suggested");
    check::equal(unknown.code(), kRefusalInvalidInput, "INVALID_INPUT");
}

TEST(person_state_an_unsupported_api_version_is_not_retryable) {
    PersonStateRequest request;
    request.api_version = 99;
    WorldTimeError version = refused([&] { (void)person_state_request(ticked(), request); }, "api 99");
    check::equal(version.code(), kRefusalUnsupportedVersion, "UNSUPPORTED_VERSION");
    check::is_true(!version.retry(), "not retryable");
}

TEST(person_state_asks_the_same_world_gate_as_the_tick) {
    World w = ticked();
    w.config.phase = 12;
    check::equal(refused([&] { (void)call(w, {}); }, "phase 12").field(), std::string("world.config"),
                 "the recipe and phase gate");
    w = ticked();
    w.has_astrology = false;
    check::equal(refused([&] { (void)call(w, {}); }, "no sky").field(), std::string("world.astrology"),
                 "the sky gate");
    w = ticked();
    w.has_heroes = false;
    w.has_npcs = false;
    w.has_villains = false;
    WorldTimeError none = refused([&] { (void)call(w, {}); }, "no person lists");
    check::equal(none.field(), std::string("world.people"), "no person lists at all");
    check::is_true(!none.retry(), "not retryable");
    w = ticked();
    w.heroes.status = "failed";
    w.npcs.ok = false;
    w.has_villains = false;
    check::equal(refused([&] { (void)call(w, {}); }, "failed packages").field(), std::string("world.people"),
                 "failed packages carry no people");
}

TEST(person_state_a_uid_two_blocks_claim_is_refused_by_the_world) {
    World w = ticked();
    const std::string hero = someone(w, LivenessBlock::Heroes);
    w.npcs.people.front().uid = hero;
    const std::map<std::string, std::vector<PersonEntry>> index = person_state_index(w);
    check::equal(index.at(hero).size(), std::size_t{2}, "detected, not resolved by order");
    WorldTimeError contested = refused([&] { (void)call(w, {change(hero, Liveness::Gone)}); }, "a contested uid");
    check::equal(contested.field(), std::string("uid"), "names uid");
    check::equal(contested.received(), hero, "the uid received");
    check::is_true(contested.refused_by_world(), "the world is the constraint");
    check::is_true(contains(contested.message(), "more than one block (heroes, npcs)"), "names both, sorted");
}

TEST(person_state_one_operation_per_call_that_changes_the_world_and_none_otherwise) {
    const World& w = ticked();
    const std::size_t before = w.history.operations.size();
    const std::string uid = someone(w, LivenessBlock::Npcs);
    const PersonStateResult out = call(w, {change(uid, Liveness::Gone, "slain in play", 5.0)});
    check::equal(out.world.history.operations.size(), before + 1, "one entry");
    const PersonOperation* op = std::get_if<PersonOperation>(&out.world.history.operations.back());
    check::is_true(op != nullptr, "the last entry is the person route's");
    check::equal(op->api_version, kPersonApiVersion, "api version");
    check::equal(op->changes.size(), std::size_t{1}, "one change");
    check::is_true(op->changes[0].uid == uid && op->changes[0].status == Liveness::Gone &&
                       op->changes[0].reason == "slain in play" && op->changes[0].day == 5.0,
                   "the normalised change, verbatim");
    check::equal(out.world.history.replay, w.history.replay, "the replay sentence is left as found");
    check::is_true(contains(history_json(out.world),
                            "{\"api_version\":1,\"kind\":\"person\",\"changes\":[{\"uid\":\"" + uid +
                                "\",\"status\":\"gone\",\"reason\":\"slain in play\",\"day\":5}]}"),
                   "written with the reference's keys");

    check::equal(call(w, {}).world.history.operations.size(), before, "an empty batch records nothing");
    const PersonStateResult idle = call(w, {change(uid, Liveness::Present, "still here", 5.0)});
    check::equal(idle.report.applied, 0, "a no-op applies nothing");
    check::equal(idle.world.history.operations.size(), before, "and records nothing");
    check::is_true(!idle.report.changes.at(0).changed, "reported unchanged");
    same_text(everything_of(idle.world), everything_of(w), "a no-op returns the world as it was");
}

TEST(person_state_the_operation_is_logged_in_call_order_among_the_others) {
    const World& w = ticked();
    const std::string npc = someone(w, LivenessBlock::Npcs);
    const std::string hero = someone(w, LivenessBlock::Heroes);
    World a = call(w, {change(npc, Liveness::Gone)}).world;
    a = advance_world_time(a, days(1.0)).world;
    a = call(a, {change(hero, Liveness::Gone), change(npc, Liveness::Present)}).world;
    const std::size_t n = a.history.operations.size();
    check::is_true(n >= w.history.operations.size() + 3, "three more entries");
    check::is_true(std::holds_alternative<PersonOperation>(a.history.operations[n - 3]), "person first");
    check::is_true(std::holds_alternative<TimeOperationRecord>(a.history.operations[n - 2]), "then the tick");
    check::is_true(std::holds_alternative<PersonOperation>(a.history.operations[n - 1]), "then the second batch");
    const auto persons = ops_of<PersonOperation>(a);
    check::equal(persons.back()->changes.size(), std::size_t{2}, "a batch is one entry");
    check::equal(persons.back()->changes[0].uid, hero, "in caller order");
}

TEST(person_state_the_same_request_twice_gives_the_same_world) {
    const World& w = ticked();
    const std::vector<PersonChange> changes{change(someone(w, LivenessBlock::Heroes), Liveness::Gone, "x", 2.0),
                                            change(someone(w, LivenessBlock::Villains), Liveness::Gone, "y", 2.0)};
    const PersonStateResult first = call(w, changes);
    const PersonStateResult second = call(w, changes);
    same_text(everything_of(first.world), everything_of(second.world), "byte-identical worlds");
    check::equal(first.report.applied, second.report.applied, "identical reports");
}

TEST(person_state_a_batch_applies_in_order_against_its_own_writes) {
    const World& w = ticked();
    const std::string uid = someone(w, LivenessBlock::Villains);
    const PersonStateResult out = call(w, {change(uid, Liveness::Gone), change(uid, Liveness::Present)});
    check::equal(out.report.applied, 2, "the second change saw the first");
    check::is_true(out.report.changes[1].from_status == Liveness::Gone, "from gone");
    check::is_true(read_liveness(out.world, uid) == Liveness::Present, "present at the end");
    check::equal(std::get<PersonOperation>(out.world.history.operations.back()).changes.size(), std::size_t{2},
                 "both recorded");
}

TEST(person_state_moves_one_field_on_one_record_and_nothing_else) {
    const World& w = ticked();
    const std::string uid = someone(w, LivenessBlock::Heroes);
    const std::size_t at = person_index(w).at(uid).position;
    World out = call(w, {change(uid, Liveness::Gone)}).world;
    // Nothing is recounted: the block summaries keep what generation wrote -- the reference
    // writes one field and nothing else, and the census is the count that moves.
    check::equal(out.heroes.summary.living, w.heroes.summary.living, "heroes.summary is not recounted");
    check::equal(out.npcs.summary.alive, w.npcs.summary.alive, "npcs.summary is not recounted");
    // Put the one field back and drop the one entry: what is left is the caller's world, byte
    // for byte.
    out.heroes.people.at(at).status = w.heroes.people.at(at).status;
    out.history.operations.pop_back();
    same_text(everything_of(out), everything_of(w), "nothing else moved");
}

TEST(person_state_this_resolver_agrees_with_the_quest_lifecycles_own) {
    const World& w = ticked();
    const std::map<std::string, std::vector<PersonEntry>> mine = person_state_index(w);
    const std::map<std::string, PersonEntry> first = person_index(w);
    std::vector<QuestGiver> heroes, dreads, npcs;
    for (const Hero& p : w.heroes.people) heroes.push_back({p.uid, is_present(p)});
    for (const HeroDread& d : w.heroes.dreads) dreads.push_back({d.uid, is_present(d)});
    for (const NpcPerson& p : w.npcs.people) npcs.push_back({p.uid, is_present(p)});
    QuestCast cast;
    cast.heroes = heroes;
    cast.dreads = dreads;
    cast.npcs = npcs;
    const QuestGiverIndex givers(cast);
    std::set<std::string> giver_uids;
    for (const auto* list : {&heroes, &dreads, &npcs}) {
        for (const QuestGiver& g : *list) {
            giver_uids.insert(g.uid);
            check::is_true(givers.find(g.uid) != nullptr, "the lifecycle resolves " + g.uid);
            check::is_true(mine.count(g.uid) == 1, "this route finds " + g.uid);
            check::is_true(mine.at(g.uid).front().block == first.at(g.uid).block,
                           "the two agree on the block of " + g.uid);
        }
    }
    for (const auto& [uid, where] : mine) {
        check::equal(where.size(), std::size_t{1}, "no generated uid is contested: " + uid);
        if (giver_uids.count(uid) == 0) {
            check::is_true(where.front().block == LivenessBlock::Villains, "the only extra uids are villains");
        }
    }
    check::equal(mine.size(), giver_uids.size() + w.villains.people.size(), "villains, and only villains, added");
}

TEST(person_state_a_giver_marked_gone_closes_the_quest_on_the_next_tick) {
    const World& w = ticked();
    const QuestRecord* quest = nullptr;
    const std::map<std::string, PersonEntry> index = person_index(w);
    for (const QuestRecord& q : w.quests->quests) {
        if (q.state != QuestState::Offered || !q.giver_uid) continue;
        const auto it = index.find(*q.giver_uid);
        if (it != index.end() && it->second.block == LivenessBlock::Heroes &&
            is_present(w.heroes.people.at(it->second.position))) {
            quest = &q;
            break;
        }
    }
    check::is_true(quest != nullptr, "an open offer held by a living hero");
    const World gone = call(w, {change(*quest->giver_uid, Liveness::Gone, "slain in play", w.world_clock->day)}).world;
    // Nothing cascades at the write: the offer still stands until the lifecycle reads liveness.
    const auto state_of = [&](const World& world) {
        for (const QuestRecord& q : world.quests->quests) {
            if (q.quest_id == quest->quest_id) return q;
        }
        check::fail("the quest on the board");
    };
    check::is_true(state_of(gone).state == QuestState::Offered, "nothing cascades from the call");
    const World after = advance_world_time(gone, days(2.0)).world;
    const QuestRecord closed = state_of(after);
    check::is_true(closed.state == QuestState::Expired && closed.closed_reason == QuestClosedReason::GiverGone,
                   "the next tick closes it as giver_gone");
}

TEST(person_state_the_npc_mirror_of_a_hero_is_not_updated_and_the_report_says_so) {
    const World& w = ticked();
    const Hero* hero = nullptr;
    const NpcPerson* mirror = nullptr;
    for (const Hero& h : w.heroes.people) {
        if (!is_present(h)) continue;
        for (const NpcPerson& p : w.npcs.people) {
            if (p.is_cast && p.hero_uid == h.uid) {
                hero = &h;
                mirror = &p;
                break;
            }
        }
        if (hero != nullptr) break;
    }
    check::is_true(hero != nullptr, "a hero with a roster mirror");
    const PersonStateResult out = call(w, {change(hero->uid, Liveness::Gone)});
    const auto& people = out.world.npcs.people;
    const auto after =
        std::find_if(people.begin(), people.end(), [&](const NpcPerson& p) { return p.uid == mirror->uid; });
    check::is_true(after != people.end() && after->alive == mirror->alive,
                   "the mirror did not move; this test and the record now describe something the route does not do");
    check::is_true(std::any_of(out.report.notes.begin(), out.report.notes.end(),
                               [](std::string_view note) { return note.find("mirror") != std::string_view::npos; }),
                   "the report says so");
}

TEST(person_state_a_villain_marked_gone_keeps_its_claims_and_the_report_says_so) {
    const World& w = ticked();
    const std::string uid = someone(w, LivenessBlock::Villains);
    const std::size_t at = person_index(w).at(uid).position;
    const PersonStateResult out = call(w, {change(uid, Liveness::Gone)});
    const Villain& before = w.villains.people.at(at);
    const Villain& after = out.world.villains.people.at(at);
    check::equal(after.status, std::string("fallen"), "fallen");
    check::equal(after.claims.size(), before.claims.size(), "keeps its claims");
    check::is_true(after.held_nodes == before.held_nodes, "and its held nodes");
    check::equal(out.world.villains.fallen.size(), w.villains.fallen.size(), "and is not added to villains.fallen");
    check::equal(out.report.notes.size(), std::size_t{2}, "the cascade note and the villain note");
    check::is_true(out.report.notes[1].find("keeps its claims") != std::string_view::npos, "the villain note");
    // Marking one back reports no villain note: the note is about a fall.
    check::equal(call(out.world, {change(uid, Liveness::Present)}).report.notes.size(), std::size_t{1},
                 "no villain note for a return");
}
