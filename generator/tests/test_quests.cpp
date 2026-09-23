// The quest board: what the world opens, what it withdraws, and what a player does to it.
//
// Nearly every test here asserts a CLOSURE REASON rather than a state, because every
// world-side closure collapses to `expired` and the reason is the only discriminator. A test
// that checked the state alone would pass against a port that had collapsed `hook_gone` and
// `age_turned` into one another, which is precisely the pair the contract says must stay
// apart.
#include "check.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "fwg/quests.hpp"

using namespace fwg;

namespace {

QuestHook hook_of(std::string id, std::string giver, std::string target, std::string verb) {
    QuestHook h;
    h.hook_id = std::move(id);
    h.giver_uid = std::move(giver);
    h.target = std::move(target);
    h.verb = std::move(verb);
    h.stated_purpose = "a purpose";
    return h;
}

QuestGiver alive(std::string uid) { return QuestGiver{std::move(uid), true}; }
QuestGiver dead(std::string uid) { return QuestGiver{std::move(uid), false}; }

const QuestRecord* row(const QuestBoard& board, std::string_view id) {
    for (const QuestRecord& q : board.quests) {
        if (q.quest_id == id) return &q;
    }
    return nullptr;
}

std::string state_of(const QuestBoard& board, std::string_view id) {
    const QuestRecord* q = row(board, id);
    return q == nullptr ? std::string("<missing>") : std::string(to_string(q->state));
}

std::string reason_of(const QuestBoard& board, std::string_view id) {
    const QuestRecord* q = row(board, id);
    if (q == nullptr) return "<missing>";
    return q->closed_reason.has_value() ? std::string(to_string(*q->closed_reason))
                                        : std::string("<open>");
}

// Readers that answer "<missing>" instead of dereferencing a null. A test that CRASHES
// under a wrong implementation reports nothing useful about it, so nothing here indexes a
// row it has not been told is there.
std::string anchor_of(const QuestBoard& board, std::string_view id) {
    const QuestRecord* q = row(board, id);
    return q == nullptr ? std::string("<missing>") : q->anchor.value_or("<null>");
}

std::string verb_of(const QuestBoard& board, std::string_view id) {
    const QuestRecord* q = row(board, id);
    return q == nullptr ? std::string("<missing>") : q->verb.value_or("<null>");
}

double window_of(const QuestBoard& board, std::string_view id) {
    const QuestRecord* q = row(board, id);
    return q == nullptr ? -1.0 : q->expires_day;
}

double closed_day_of(const QuestBoard& board, std::string_view id) {
    const QuestRecord* q = row(board, id);
    if (q == nullptr) return -1.0;
    return q->closed_day.value_or(-1.0);
}

int events_named(const QuestBoard& board, std::string_view event) {
    int count = 0;
    for (const QuestLogEntry& entry : board.log) {
        if (entry.event == event) ++count;
    }
    return count;
}

// A cast whose spans stay alive for the life of the fixture.
struct Cast {
    std::vector<QuestGiver> heroes;
    std::vector<QuestGiver> dreads;
    std::vector<QuestGiver> npcs;
    bool heroes_ok = true;
    bool npcs_ok = true;

    QuestCast view() const {
        QuestCast c;
        c.heroes_ok = heroes_ok;
        c.npcs_ok = npcs_ok;
        c.heroes = heroes;
        c.dreads = dreads;
        c.npcs = npcs;
        return c;
    }
};

struct Fixture {
    std::vector<QuestHook> hooks;
    Cast cast;
    std::vector<std::string> settlement_uids;
    std::vector<std::string> nest_ids;

    QuestWorldView targets() const {
        QuestWorldView v;
        v.settlement_uids = settlement_uids;
        v.nest_ids = nest_ids;
        return v;
    }
};

// Everything the sweep needs, with the two indexes owned by the caller so their lifetimes
// are obvious.
struct Sweeper {
    QuestGiverIndex givers;
    QuestTargetIndex targets;
    const Fixture* fixture = nullptr;

    explicit Sweeper(const Fixture& f)
        : givers(f.cast.view()), targets(f.targets()), fixture(&f) {}

    // Deleted on an rvalue on purpose. The returned request holds POINTERS into this
    // object, so binding one to a variable built from a temporary sweeper leaves them
    // dangling the moment the statement ends -- which reads correctly right up until an
    // allocation pattern shifts and the suite segfaults instead of failing. A compile error
    // is the loud version of that, and it is how this was found.
    QuestStepRequest at(double, std::int64_t) const&& = delete;

    QuestStepRequest at(double day, std::int64_t index) const& {
        QuestStepRequest r;
        r.seed = 42;
        r.day = day;
        r.index = index;
        r.hooks = fixture->hooks;
        r.heroes_ok = fixture->cast.heroes_ok;
        r.givers = &givers;
        r.targets = &targets;
        return r;
    }
};

Fixture three_offers() {
    Fixture f;
    f.hooks.push_back(hook_of("hook-a", "hero-1", "city-1", "defend"));
    f.hooks.push_back(hook_of("hook-b", "hero-2", "city-1", "slay"));
    f.hooks.push_back(hook_of("hook-c", "npc-1", "city-1", "trade"));
    f.cast.heroes = {alive("hero-1"), alive("hero-2")};
    f.cast.npcs = {alive("npc-1")};
    return f;
}

}  // namespace

// --- opening an offer -------------------------------------------------------

TEST(a_sweep_opens_one_offer_per_hook_and_draws_one_window_for_each) {
    const Fixture f = three_offers();
    const Sweeper sweep(f);
    QuestBoard board;
    quest_step(board, sweep.at(0.0, 0));

    check::equal(board.quests.size(), std::size_t{3}, "one quest per hook");
    check::equal(board.version, kQuestsVersion, "the board version");
    std::vector<double> windows;
    for (const QuestRecord& q : board.quests) {
        check::equal(std::string(to_string(q.state)), std::string("offered"), q.quest_id);
        check::equal(q.offered_day, 0.0, "the day it was offered");
        check::between(q.expires_day, kQuestOfferMinDays, kQuestOfferMaxDays, "the offer window");
        check::is_true(!q.closed_day.has_value(), "an open quest to carry no closing day");
        windows.push_back(q.expires_day);
    }
    // Three DISTINCT draws from one per-day generator. Equal windows would mean the
    // generator was rebuilt per quest, which is how a stream ends up keyed on something
    // other than the day.
    std::sort(windows.begin(), windows.end());
    check::is_true(std::unique(windows.begin(), windows.end()) == windows.end(),
                   "three distinct offer windows");

    check::equal(anchor_of(board, "hook-a"), std::string("city-1"),
                 "the anchor from the hook's target");
    check::equal(verb_of(board, "hook-a"), std::string("defend"), "the verb from the hook");
}

TEST(hook_list_order_decides_which_quest_draws_which_window) {
    // The single most load-bearing ordering fact in the module. Reorder the hooks and every
    // quest opened that day gets a different expiry, because the draws come from one
    // generator in walk order.
    Fixture forward = three_offers();
    Fixture reversed = three_offers();
    std::reverse(reversed.hooks.begin(), reversed.hooks.end());

    QuestBoard one;
    const Sweeper sweep_2(forward);
    quest_step(one, sweep_2.at(0.0, 0));
    QuestBoard two;
    const Sweeper sweep_3(reversed);
    quest_step(two, sweep_3.at(0.0, 0));

    const double first_draw = window_of(one, "hook-a");
    const double third_draw = window_of(one, "hook-c");
    // Non-vacuity: if the first and third draws happened to be equal the swap below would
    // prove nothing.
    check::is_true(first_draw != third_draw, "the first and third draws to differ");

    check::close(window_of(two, "hook-c"), first_draw, 1e-12,
                 "the first hook walked to take the first draw whichever id it carries");
    check::close(window_of(two, "hook-a"), third_draw, 1e-12,
                 "the last hook walked to take the last draw");
    check::close(window_of(two, "hook-b"), window_of(one, "hook-b"), 1e-12,
                 "the middle hook to be unmoved by the reversal");
}

TEST(a_second_sweep_opens_nothing_and_redraws_no_window) {
    const Fixture f = three_offers();
    const Sweeper sweep(f);
    QuestBoard board;
    quest_step(board, sweep.at(0.0, 0));
    const std::vector<QuestRecord> after_first = board.quests;
    check::equal(after_first.size(), std::size_t{3}, "three offers to carry forward");

    quest_step(board, sweep.at(1.0, 1));
    check::equal(board.quests.size(), after_first.size(), "no second copy of any offer");
    for (std::size_t i = 0; i < board.quests.size(); ++i) {
        check::close(board.quests[i].expires_day, after_first[i].expires_day, 1e-12,
                     "a window drawn once and never redrawn");
        check::equal(board.quests[i].offered_day, 0.0, "the day it was first offered");
    }
}

TEST(the_board_is_sorted_by_quest_id_after_every_sweep) {
    Fixture f;
    f.hooks.push_back(hook_of("hook-z", "hero-1", "city-1", "slay"));
    f.hooks.push_back(hook_of("hook-a", "hero-1", "city-1", "slay"));
    f.hooks.push_back(hook_of("hook-m", "hero-1", "city-1", "slay"));
    f.cast.heroes = {alive("hero-1")};
    QuestBoard board;
    const Sweeper sweep_4(f);
    quest_step(board, sweep_4.at(0.0, 0));
    check::equal(board.quests.size(), std::size_t{3}, "three rows to sort");
    check::equal(board.quests[0].quest_id, std::string("hook-a"), "first by byte-wise id");
    check::equal(board.quests[1].quest_id, std::string("hook-m"), "second by byte-wise id");
    check::equal(board.quests[2].quest_id, std::string("hook-z"), "third by byte-wise id");
}

// --- withdrawing one --------------------------------------------------------

TEST(a_vanished_hook_closes_its_quest_as_hook_gone_and_leaves_the_rest_standing) {
    Fixture f = three_offers();
    QuestBoard board;
    {
        const Sweeper sweep(f);
        quest_step(board, sweep.at(0.0, 0));
    }
    check::equal(board.quests.size(), std::size_t{3}, "three offers before the hook vanishes");

    f.hooks.erase(f.hooks.begin() + 1);  // hook-b leaves the roster
    const Sweeper sweep_5(f);
    quest_step(board, sweep_5.at(1.0, 1));

    check::equal(state_of(board, "hook-b"), std::string("expired"), "the orphaned quest closes");
    check::equal(reason_of(board, "hook-b"), std::string("hook_gone"), "and says why");
    check::equal(closed_day_of(board, "hook-b"), 1.0, "on the day it closed");
    // The floor: the union walk must not close the world along with the hook.
    check::equal(reason_of(board, "hook-a"), std::string("<open>"), "hook-a still standing");
    check::equal(reason_of(board, "hook-c"), std::string("<open>"), "hook-c still standing");
    check::equal(board.quests.size(), std::size_t{3}, "the closed row is kept, not dropped");
}

TEST(a_gone_giver_a_missing_giver_and_a_failed_package_all_close_as_giver_gone) {
    {
        Fixture f = three_offers();
        f.cast.heroes = {dead("hero-1"), alive("hero-2")};
        QuestBoard board;
        const Sweeper sweep_6(f);
        quest_step(board, sweep_6.at(0.0, 0));
        check::equal(reason_of(board, "hook-a"), std::string("giver_gone"), "a dead giver");
        check::equal(reason_of(board, "hook-b"), std::string("<open>"),
                     "a living giver's quest to stand");
    }
    {
        // A hook with no giver at all. The index never holds an empty uid, so the lookup
        // misses and the offer closes on its first sweep.
        Fixture f;
        f.hooks.push_back(hook_of("hook-a", "", "city-1", "slay"));
        QuestBoard board;
        const Sweeper sweep_7(f);
        quest_step(board, sweep_7.at(0.0, 0));
        check::equal(reason_of(board, "hook-a"), std::string("giver_gone"), "a giverless hook");
    }
    {
        // A FAILED package carries no people. Zero present people and no package at all are
        // different facts, and reading a failed block as an empty cast would leave every
        // quest it gave open forever. The npcs package is the one that shows this cleanly,
        // because the hooks themselves live in the heroes block.
        Fixture f = three_offers();
        f.cast.npcs_ok = false;
        QuestBoard board;
        const Sweeper sweep_8(f);
        quest_step(board, sweep_8.at(0.0, 0));
        check::equal(reason_of(board, "hook-c"), std::string("giver_gone"),
                     "a quest whose giver was in a failed package");
        check::equal(reason_of(board, "hook-a"), std::string("<open>"),
                     "the heroes package to be untouched by the npcs package failing");
    }
    {
        // The heroes package failing is a bigger event, because the HOOKS live in it. A
        // world whose heroes package failed offers nothing at all -- and a board built while
        // it stood closes as `hook_gone`, because that is what happened: the hooks left while
        // the world around them stood.
        Fixture f = three_offers();
        QuestBoard board;
        const Sweeper sweep_9(f);
        quest_step(board, sweep_9.at(0.0, 0));
        check::equal(board.quests.size(), std::size_t{3}, "a board opened while it stood");

        f.cast.heroes_ok = false;
        const Sweeper sweep_10(f);
        quest_step(board, sweep_10.at(1.0, 1));
        check::equal(reason_of(board, "hook-a"), std::string("hook_gone"),
                     "the hooks going with the package");
        check::equal(reason_of(board, "hook-c"), std::string("hook_gone"), "all of them");

        QuestBoard empty;
        const Sweeper sweep_11(f);
        quest_step(empty, sweep_11.at(0.0, 0));
        check::equal(empty.quests.size(), std::size_t{0},
                     "and a failed package offering nothing in the first place");
    }
}

TEST(the_giver_index_takes_the_first_writer_across_the_three_lists) {
    // heroes.people, then heroes.dreads, then npcs.people. A uid claimed by two resolves to
    // the first, silently and deliberately: widening it would change which people the
    // lifecycle considers.
    Cast cast;
    cast.heroes = {alive("shared")};
    cast.dreads = {dead("shared")};
    cast.npcs = {dead("shared"), alive("npc-only")};
    const QuestGiverIndex index(cast.view());
    check::equal(index.size(), std::size_t{2}, "one entry per distinct uid");
    check::is_true(index.find(std::string("shared"))->present,
                   "the living heroes entry to win over the two gone ones");
    check::is_true(index.find(std::string("npc-only")) != nullptr, "the npc-only uid");
    check::is_true(index.find(std::optional<std::string>{}) == nullptr,
                   "a null giver uid to resolve to nobody");

    // The reverse: a uid whose FIRST list has it gone stays gone, so the order is doing the
    // work rather than a preference for the living.
    Cast flipped;
    flipped.heroes = {dead("shared")};
    flipped.npcs = {alive("shared")};
    check::is_true(!QuestGiverIndex(flipped.view()).find(std::string("shared"))->present,
                   "the gone heroes entry to win when it is first");
}

TEST(settlements_join_on_uid_and_nests_join_on_id) {
    Fixture f;
    QuestHook city = hook_of("hook-city", "hero-1", "city-7", "defend");
    city.actual_effect.kind = QuestEffectKind::City;
    city.actual_effect.uid = "city-7";
    QuestHook nest = hook_of("hook-nest", "hero-1", "", "slay");
    nest.actual_effect.kind = QuestEffectKind::Nest;
    nest.actual_effect.uid = "city-7";  // the WRONG id space on purpose
    nest.actual_effect.nest_id = "nest-wolf-12";
    f.hooks = {city, nest};
    f.cast.heroes = {alive("hero-1")};
    f.settlement_uids = {"city-7"};
    f.nest_ids = {"nest-wolf-12"};

    QuestBoard board;
    const Sweeper sweep_12(f);
    quest_step(board, sweep_12.at(0.0, 0));
    check::equal(reason_of(board, "hook-city"), std::string("<open>"), "a city that still stands");
    check::equal(reason_of(board, "hook-nest"), std::string("<open>"), "a nest that still stands");
    // The nest anchored on its nest_id, not on the uid it also carries.
    check::equal(anchor_of(board, "hook-nest"), std::string("nest-wolf-12"),
                 "a targetless nest hook to anchor on its nest id");

    // Swap the two id spaces: each lookup now asks the other block's question and finds
    // nothing. A port that joined nests on `uid` would pass the test above and fail here.
    Fixture swapped = f;
    swapped.settlement_uids = {"nest-wolf-12"};
    swapped.nest_ids = {"city-7"};
    QuestBoard crossed;
    const Sweeper sweep_13(swapped);
    quest_step(crossed, sweep_13.at(0.0, 0));
    check::equal(reason_of(crossed, "hook-city"), std::string("target_gone"),
                 "a city looked up in the nest list");
    check::equal(reason_of(crossed, "hook-nest"), std::string("target_gone"),
                 "a nest looked up in the settlement list");
}

TEST(an_absent_nest_id_falls_back_to_the_uid_and_a_present_empty_one_does_not) {
    // A dict get with a default fires only when the key is MISSING. An effect carrying an
    // empty nest id therefore matches no nest and closes the quest; treating empty as absent
    // would match by uid and leave it open, which is the opposite answer on exactly the
    // malformed-hook case.
    Fixture f;
    QuestHook absent = hook_of("hook-absent", "hero-1", "", "slay");
    absent.actual_effect.kind = QuestEffectKind::Nest;
    absent.actual_effect.uid = "nest-bear-3";  // no nest_id at all
    QuestHook empty = hook_of("hook-empty", "hero-1", "", "slay");
    empty.actual_effect.kind = QuestEffectKind::Nest;
    empty.actual_effect.uid = "nest-bear-3";
    empty.actual_effect.nest_id = std::string();  // present, and empty
    f.hooks = {absent, empty};
    f.cast.heroes = {alive("hero-1")};
    f.nest_ids = {"nest-bear-3"};

    QuestBoard board;
    const Sweeper sweep_14(f);
    quest_step(board, sweep_14.at(0.0, 0));
    check::equal(reason_of(board, "hook-absent"), std::string("<open>"),
                 "an absent nest id to fall back to the uid");
    check::equal(reason_of(board, "hook-empty"), std::string("target_gone"),
                 "a present but empty nest id to match no nest");
}

TEST(a_target_kind_this_module_cannot_resolve_reads_as_present) {
    // A quest wrongly left open is a smaller lie than a quest silently reaped because this
    // module did not recognise its target.
    Fixture f;
    f.cast.heroes = {alive("hero-1")};
    for (const QuestEffectKind kind :
         {QuestEffectKind::Ruin, QuestEffectKind::Relic, QuestEffectKind::Hamlet,
          QuestEffectKind::Fortress, QuestEffectKind::Port, QuestEffectKind::Shrine,
          QuestEffectKind::College, QuestEffectKind::LeyNode, QuestEffectKind::Unknown}) {
        QuestHook h = hook_of("hook-" + std::to_string(static_cast<int>(kind)), "hero-1",
                              "somewhere", "investigate");
        h.actual_effect.kind = kind;
        h.actual_effect.uid = "nothing-that-exists";
        f.hooks.push_back(h);
    }
    check::equal(f.hooks.size(), std::size_t{9}, "nine unresolvable kinds to try");
    QuestBoard board;
    const Sweeper sweep_15(f);
    quest_step(board, sweep_15.at(0.0, 0));
    for (const QuestRecord& q : board.quests) {
        check::equal(reason_of(board, q.quest_id), std::string("<open>"), q.quest_id);
    }
}

TEST(an_offer_expires_on_its_window_and_a_taken_quest_does_not) {
    const Fixture f = three_offers();
    const Sweeper sweep(f);
    QuestBoard board;
    quest_step(board, sweep.at(0.0, 0));

    // Take one of them, and leave the others offered.
    const std::vector<QuestAction> take{{"hook-a", QuestActionKind::Take, std::optional<double>{}}};
    quest_apply_actions(board, take, 1.0);
    check::equal(state_of(board, "hook-a"), std::string("taken"), "the quest was taken");

    // One day past the longest window any of them could have drawn.
    const double late = kQuestOfferMaxDays + 1.0;
    quest_step(board, sweep.at(late, static_cast<std::int64_t>(late)));

    check::equal(state_of(board, "hook-a"), std::string("taken"),
                 "a taken quest to survive its offer window");
    // The floor: the sweep WAS capable of expiring something on this day.
    check::equal(reason_of(board, "hook-b"), std::string("expired"), "an untaken offer lapses");
    check::equal(reason_of(board, "hook-c"), std::string("expired"), "and so does the other");
    check::equal(events_named(board, "expired"), 2, "two lapses in the log");
}

// --- the age turn -----------------------------------------------------------

TEST(an_age_turn_retires_every_open_row_under_its_own_reason_and_rebuilds_the_board) {
    Fixture f = three_offers();
    QuestBoard board;
    {
        const Sweeper sweep(f);
        quest_step(board, sweep.at(0.0, 0));
        const std::vector<QuestAction> take{
            {"hook-a", QuestActionKind::Take, std::optional<double>{}}};
        quest_apply_actions(board, take, 1.0);
    }
    check::equal(board.quests.size(), std::size_t{3}, "a board to retire");

    // The chimera: the regenerated cast re-mints positional uids, so the SAME hook ids come
    // back naming different things. A reap that depended on the id disappearing would leave
    // these rows carrying the old age's anchor and a giver who no longer exists.
    Fixture next;
    next.hooks.push_back(hook_of("hook-a", "hero-1", "city-99", "reclaim"));
    next.hooks.push_back(hook_of("hook-b", "hero-2", "city-99", "cleanse"));
    next.cast.heroes = {alive("hero-1"), alive("hero-2")};

    const double turn_day = 1800000.0;  // one age, on the calendar this module fixes
    const Sweeper sweep_16(next);
    quest_turn_age_board(board, sweep_16.at(turn_day, 0));

    check::equal(board.quests.size(), std::size_t{2}, "the new age's board, and only it");
    check::equal(events_named(board, "age_turned"), 3, "every open row closed as age_turned");
    check::equal(events_named(board, "hook_gone"), 0,
                 "and none of them mistaken for a vanished hook");
    for (const QuestRecord& q : board.quests) {
        check::equal(std::string(to_string(q.state)), std::string("offered"), q.quest_id);
        check::equal(q.offered_day, turn_day, "a fresh offer, dated at the turn");
    }
    check::equal(anchor_of(board, "hook-a"), std::string("city-99"),
                 "the NEW age's anchor, not the old one carried through");
    check::equal(verb_of(board, "hook-a"), std::string("reclaim"), "the new age's verb");
    // The log outlives the board it stood on.
    check::is_true(board.log.size() >= 3, "the closures to survive the retirement");
}

TEST(the_age_turn_rebuild_is_keyed_on_the_floor_of_the_day_whatever_index_it_is_handed) {
    // The quests cadence has a period of one day, so the absolute index of the sweep at
    // `day` is floor(day) -- and the next tick's half-open window begins at floor(day) + 1,
    // so nothing is drawn twice. A rebuild keyed on anything else would draw a window no
    // later call could reproduce.
    const Fixture f = three_offers();
    const double turn_day = 1800000.5;

    QuestBoard turned;
    turned.quests.push_back(QuestRecord{"hook-stale", std::nullopt, std::nullopt, std::nullopt,
                                        std::nullopt, false, QuestState::Offered, 0.0, 10.0,
                                        std::nullopt, std::nullopt});
    // The sweeper is NAMED rather than a temporary: a request holds pointers into it, so a
    const Sweeper sweep_17(f);
    // `QuestStepRequest r = sweep_17.at(...)` outlives the indexes it points at.
    const Sweeper sweep(f);
    QuestStepRequest misleading = sweep.at(turn_day, 999999);
    quest_turn_age_board(turned, misleading);

    QuestBoard reference;
    quest_step(reference, sweep.at(turn_day, 1800000));

    check::equal(reference.quests.size(), std::size_t{3}, "the reference board");
    check::equal(turned.quests.size(), reference.quests.size(), "the rebuilt board");
    for (std::size_t i = 0; i < turned.quests.size(); ++i) {
        check::close(turned.quests[i].expires_day, reference.quests[i].expires_day, 1e-12,
                     "the rebuild to ignore the index it was handed and use floor(day)");
    }
    // Non-vacuity: a rebuild at the misleading index really would have drawn differently.
    QuestBoard wrong;
    quest_step(wrong, sweep.at(turn_day, 999999));
    check::equal(wrong.quests.size(), reference.quests.size(), "a comparable control board");
    check::is_true(window_of(wrong, "hook-a") != window_of(reference, "hook-a"),
                   "the two indices to draw different windows");
}

// --- what the game reports back ---------------------------------------------

namespace {

std::map<std::string, LeyNetwork> one_network(double intensity) {
    LeyNetwork net;
    net.name = "fire";
    net.strength = 1.0;
    net.nodes.push_back(LeyNode{"node-1", Vec3{1.0, 0.0, 0.0}, intensity});
    std::map<std::string, LeyNetwork> networks;
    networks.emplace("fire", net);
    return networks;
}

QuestHook ley_hook(std::string id, double delta) {
    QuestHook h = hook_of(std::move(id), "hero-1", "node-1", "");
    h.actual_effect.kind = QuestEffectKind::LeyNode;
    h.actual_effect.school = "fire";
    h.actual_effect.node_id = "node-1";
    h.actual_effect.has_intensity_delta = true;
    h.actual_effect.intensity_delta = delta;
    return h;
}

}  // namespace

TEST(a_resolution_closes_a_quest_once_and_refuses_a_second_attempt) {
    const Fixture f = three_offers();
    QuestBoard board;
    const Sweeper sweep_18(f);
    quest_step(board, sweep_18.at(0.0, 0));

    const std::vector<QuestResolution> done{{"hook-a", QuestOutcome::Completed}};
    const QuestResolutionResult first =
        quest_apply_resolutions(board, done, 5.0, f.hooks, false, nullptr);
    check::is_true(!first.ley_moved, "a non-ley completion to move no field");
    check::equal(state_of(board, "hook-a"), std::string("completed"), "the outcome the game sent");
    check::equal(reason_of(board, "hook-a"), std::string("resolved"),
                 "resolved, distinct from abandoned");
    check::equal(row(board, "hook-a")->closed_day.value_or(-1.0), 5.0, "the closing day");
    check::equal(events_named(board, "completed"), 1, "one completion in the log");

    check::throws([&] { quest_apply_resolutions(board, done, 6.0, f.hooks, false, nullptr); },
                  "a second resolution of a closed quest");
    check::throws(
        [&] {
            const std::vector<QuestResolution> unknown{{"hook-nowhere", QuestOutcome::Failed}};
            quest_apply_resolutions(board, unknown, 6.0, f.hooks, false, nullptr);
        },
        "a resolution naming a quest that is not on the board");
    // The floor: the refusals above changed nothing.
    check::equal(state_of(board, "hook-a"), std::string("completed"), "still exactly one closure");
    check::equal(events_named(board, "completed"), 1, "and one log entry for it");
}

TEST(a_completed_ley_hook_applies_the_delta_it_declares_and_clamps_it_both_ways) {
    Fixture f;
    f.hooks = {ley_hook("hook-up", 0.5), ley_hook("hook-down", -5.0)};
    f.cast.heroes = {alive("hero-1")};

    {
        std::map<std::string, LeyNetwork> networks = one_network(3.8);
        QuestBoard board;
        const Sweeper sweep_19(f);
        quest_step(board, sweep_19.at(0.0, 0));
        const std::vector<QuestResolution> done{{"hook-up", QuestOutcome::Completed}};
        const QuestResolutionResult moved =
            quest_apply_resolutions(board, done, 5.0, f.hooks, true, &networks);
        check::is_true(moved.ley_moved, "the field to have moved");
        check::equal(moved.applied.size(), std::size_t{1}, "one application");
        check::close(networks.at("fire").nodes[0].intensity, 4.0, 1e-12,
                     "3.8 + 0.5 clamped at the ceiling");
        check::equal(events_named(board, "ley_applied"), 1, "the ley write in the log");
    }
    {
        std::map<std::string, LeyNetwork> networks = one_network(3.8);
        QuestBoard board;
        const Sweeper sweep_20(f);
        quest_step(board, sweep_20.at(0.0, 0));
        const std::vector<QuestResolution> done{{"hook-down", QuestOutcome::Completed}};
        quest_apply_resolutions(board, done, 5.0, f.hooks, true, &networks);
        check::close(networks.at("fire").nodes[0].intensity, 0.0, 1e-12,
                     "3.8 - 5.0 clamped at the floor rather than going negative");
    }
    {
        // A failure declares the same effect and applies none of it: the world records what
        // the game played, it does not invent a consequence.
        std::map<std::string, LeyNetwork> networks = one_network(3.8);
        QuestBoard board;
        const Sweeper sweep_21(f);
        quest_step(board, sweep_21.at(0.0, 0));
        const std::vector<QuestResolution> lost{{"hook-up", QuestOutcome::Failed}};
        const QuestResolutionResult moved =
            quest_apply_resolutions(board, lost, 5.0, f.hooks, true, &networks);
        check::is_true(!moved.ley_moved, "a failed quest to move nothing");
        check::close(networks.at("fire").nodes[0].intensity, 3.8, 1e-12, "the node untouched");
    }
    {
        // Magic disabled: recorded, and nothing moves. Claiming the field moved anyway is
        // what sent a magicless world into the network evaluation.
        std::map<std::string, LeyNetwork> networks = one_network(3.8);
        QuestBoard board;
        const Sweeper sweep_22(f);
        quest_step(board, sweep_22.at(0.0, 0));
        const std::vector<QuestResolution> done{{"hook-up", QuestOutcome::Completed}};
        const QuestResolutionResult moved =
            quest_apply_resolutions(board, done, 5.0, f.hooks, false, &networks);
        check::is_true(!moved.ley_moved, "a magicless world to move no field");
        check::close(networks.at("fire").nodes[0].intensity, 3.8, 1e-12, "the node untouched");
        check::equal(state_of(board, "hook-up"), std::string("completed"),
                     "the outcome still recorded");
        check::equal(moved.applied.size(), std::size_t{0}, "and no application claimed");
    }
}

// --- the two transitions a player makes -------------------------------------

TEST(a_take_is_refused_one_day_past_the_window_and_accepted_on_the_last_day_of_it) {
    // Not pedantry: a sweep tests the window only while a quest is `offered`, so a take
    // applied after the window closed but before the next sweep would make the offer
    // immortal -- it leaves `offered` and the expiry branch is never reached again.
    QuestRecord quest;
    quest.quest_id = "hook-a";
    quest.expires_day = 400.0;

    const QuestTransition on_time = quest_transition(quest, QuestActionKind::Take, 400.0);
    check::equal(std::string(to_string(on_time.quest.state)), std::string("taken"),
                 "a take on the last legal day");
    check::equal(on_time.entry.event, std::string("taken"), "the log entry");
    check::equal(std::string(to_string(quest.state)), std::string("offered"),
                 "the argument to be left alone");

    check::throws([&] { quest_transition(quest, QuestActionKind::Take, 400.001); },
                  "a take past the window");
    check::throws([&] { quest_transition(on_time.quest, QuestActionKind::Take, 100.0); },
                  "a second take of a taken quest");
}

TEST(abandon_writes_its_own_closed_reason_rather_than_resolved) {
    // A resolution is the game reporting an outcome it played; this is the party putting the
    // promise down, and a consumer reading the board should tell those apart without
    // re-reading the log.
    QuestRecord quest;
    quest.quest_id = "hook-a";
    quest.expires_day = 400.0;
    quest.state = QuestState::Taken;

    const QuestTransition gone = quest_transition(quest, QuestActionKind::Abandon, 12.0);
    check::equal(std::string(to_string(gone.quest.state)), std::string("abandoned"), "the state");
    check::is_true(gone.quest.closed_reason.has_value(), "a closing reason");
    check::equal(std::string(to_string(*gone.quest.closed_reason)), std::string("abandoned"),
                 "abandoned, not resolved");
    check::equal(gone.quest.closed_day.value_or(-1.0), 12.0, "the closing day");
    // An abandon is still refused past the window? No -- only a TAKE is. Putting a promise
    // down is always possible while the quest is open.
    check::equal(std::string(to_string(
                     quest_transition(quest, QuestActionKind::Abandon, 99999.0).quest.state)),
                 std::string("abandoned"), "an abandon at any day");
    check::throws([&] { quest_transition(gone.quest, QuestActionKind::Abandon, 13.0); },
                  "abandoning an already abandoned quest");
}

TEST(a_batch_of_actions_is_all_or_nothing_and_its_alternatives_shrink_as_it_goes) {
    const Fixture f = three_offers();
    QuestBoard board;
    const Sweeper sweep_23(f);
    quest_step(board, sweep_23.at(0.0, 0));
    const QuestBoard before = board;

    // The second action names a quest that is not there, so the whole batch is discarded --
    // including the first action, which on its own would have succeeded.
    const std::vector<QuestAction> mixed{
        {"hook-a", QuestActionKind::Take, std::optional<double>{}},
        {"hook-nowhere", QuestActionKind::Take, std::optional<double>{}}};
    check::throws([&] { quest_apply_actions(board, mixed, 1.0); }, "a batch with a bad id");
    check::equal(state_of(board, "hook-a"), std::string("offered"),
                 "the first action to have been rolled back with the batch");
    check::equal(board.log.size(), before.log.size(), "and nothing logged");

    // Alternatives are recomputed per action, so a batch that closes a quest stops offering
    // it back. Abandon two, then trip a refusal and read what it suggests.
    const std::vector<QuestAction> closing{
        {"hook-a", QuestActionKind::Abandon, std::optional<double>{}},
        {"hook-b", QuestActionKind::Abandon, std::optional<double>{}}};
    const QuestActionResult applied = quest_apply_actions(board, closing, 1.0);
    check::equal(applied.actions.size(), std::size_t{2}, "two actions applied");
    check::equal(applied.day, 1.0, "the clock day the batch used");
    std::string message;
    try {
        const std::vector<QuestAction> late{
            {"hook-a", QuestActionKind::Take, std::optional<double>{}}};
        quest_apply_actions(board, late, 2.0);
    } catch (const std::exception& error) {
        message = error.what();
    }
    check::is_true(message.find("hook-c") != std::string::npos,
                   "the one quest still open to be offered as an alternative");
    check::is_true(message.find("hook-b") == std::string::npos,
                   "a quest this batch closed not to be offered back");
}

TEST(an_empty_batch_changes_nothing_and_records_nothing) {
    // An operation log that grows on every no-op is an unbounded record of nothing happening.
    const Fixture f = three_offers();
    QuestBoard board;
    const Sweeper sweep_24(f);
    quest_step(board, sweep_24.at(0.0, 0));
    const std::size_t log_size = board.log.size();

    const QuestActionResult empty = quest_apply_actions(board, {}, 7.0);
    check::equal(empty.actions.size(), std::size_t{0}, "no actions reported");
    check::equal(empty.day, 7.0, "the clock day still reported");
    check::equal(board.log.size(), log_size, "nothing appended to the log");
    check::equal(board.quests.size(), std::size_t{3}, "the board untouched");

    check::throws(
        [&] {
            const std::vector<QuestAction> bad{
                {"hook-a", QuestActionKind::Take, std::optional<double>{-1.0}}};
            quest_apply_actions(board, bad, 7.0);
        },
        "an action naming a day before founding year 0");
}
