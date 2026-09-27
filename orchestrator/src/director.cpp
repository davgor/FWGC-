#include "orchestrator/director.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <set>

#include "fwg/api/quests.hpp"
#include "fwg/api/read.hpp"
#include "fwg/api/weather.hpp"
#include "fwg/api/world_time.hpp"
#include "knowledge.hpp"
#include "schedule.hpp"

namespace orchestrator {

namespace {

using knowledge::KnownPlace;
using knowledge::Speaker;

// How many of each kind of place a description of the surroundings names, nearest first. The
// cities in reach are few and all named; waystones and markers crowd everything else out
// unless held to a handful.
constexpr std::size_t kSurroundingCities = 8;
constexpr std::size_t kSurroundingHamlets = 3;
constexpr std::size_t kSurroundingFortresses = 3;
constexpr std::size_t kSurroundingLandmarks = 6;

// How many dangerous places an answer to "is it safe" names, worst first.
constexpr std::size_t kSafetyPlaces = 3;

// How a speaker's regard for the player moves. Clamped to [-3, 3].
constexpr int kThanked = 1;
constexpr int kInsulted = -2;
constexpr int kWorkTaken = 1;
constexpr int kWorkAbandoned = -2;
constexpr int kWorkFinished = 2;
constexpr int kWorkFailed = -1;

// What an answer about a place says of what is around it: the nests this near it, and how many.
constexpr double kAroundPlaceM = 2000.0;
constexpr std::size_t kNestsAround = 2;

// A mask comes off to a player who has met its wearer this many times: long acquaintance sees
// through it, whatever else the player has learned. Counting the conversation under way.
constexpr int kMeetingsThatSeeThrough = 3;

// Feelings: the fear ladder, how a speaker's regard moves when the player eases them, how long
// fear takes to fall a step, how long a fright counts as still there, and how near a nest leaves
// people uneasy at rest.
constexpr std::array<std::string_view, 4> kFearWords = {"calm", "uneasy", "afraid", "panicked"};
constexpr int kCalmed = 1;
constexpr double kEaseMinutes = 30.0;
constexpr double kThreatMinutes = 15.0;
constexpr double kUneasyNestM = 1000.0;
// How long someone remembers the player talking them down: today and tomorrow. A visit is many
// conversations (turning to someone else ends one), so the days, not the conversations, keep it.
constexpr double kEasedDays = 2.0;
constexpr int kUneasyNestDanger = 4;

// Why a request is refused: the reason in words, and its kind (contract.hpp, Outcome::Refused),
// for a caller that answers each kind in its own words.
Fact reason(std::string why, std::string text) {
    return Fact{FactKind::Reason, "", {{"reason", std::move(text)}, {"why", std::move(why)}}};
}

// Whose a piece of work is, as a refusal's reason says it: the giver's by name, else "someone
// else's". Never "the speaker's": voiced by the speaker, that was read as the stranger, and
// came out "not yours to give" (judged 2026-09-24).
std::string whose(const std::optional<knowledge::KnownPerson>& giver) {
    const std::string* name = giver ? giver->fact.field("name") : nullptr;
    return name && !name->empty() ? *name + "'s" : std::string("someone else's");
}

FactPacket packet_for(const Speaker& s, const DialogueAct& act, Outcome outcome) {
    FactPacket p;
    p.speaker = s.uid;
    p.answering = act.kind;
    p.outcome = outcome;
    p.persona = s.persona;
    return p;
}

const fwg::HeroQuestHook* hook_of(const fwg::World& world, const std::string& quest_id) {
    // quest_id is the hook_id the offer opened from; difficulty lives on the hook.
    for (const fwg::HeroQuestHook& hook : world.heroes.quest_hooks)
        if (hook.hook_id == quest_id) return &hook;
    return nullptr;
}

std::optional<std::vector<fwg::QuestRow>> board(const fwg::World& world, fwg::QuestState state) {
    fwg::QuestsReadRequest request;
    request.states = std::vector<fwg::QuestState>{state};
    try {
        return fwg::read_quests(world, request).quests;
    } catch (const fwg::WorldTimeError& refusal) {
        if (refusal.field() == "world.quests") return std::nullopt;  // no board until the first tick
        throw;
    }
}

struct Offer {
    fwg::QuestRow row;
    bool own = false;
};

// The open offers a speaker can speak to: their own, then those of givers present where they
// stand -- someone at an inn knows who at the inn is hiring. Empty before the board opens.
std::vector<Offer> offers_known_to(const fwg::World& world, const Speaker& s) {
    std::vector<Offer> own, brokered;
    const std::optional<std::vector<fwg::QuestRow>> rows = board(world, fwg::QuestState::Offered);
    if (!rows) return own;
    for (const fwg::QuestRow& row : *rows) {
        if (row.giver_uid && *row.giver_uid == s.uid) {
            own.push_back({row, true});
        } else if (s.node && row.giver && row.giver->anchor.node && *row.giver->anchor.node == *s.node &&
                   row.giver->liveness == fwg::Liveness::Present) {
            brokered.push_back({row, false});
        }
    }
    own.insert(own.end(), brokered.begin(), brokered.end());
    return own;
}

// Where the game marks a piece of work on its map: the anchor, and the anchor's terrain node,
// exactly. For the game alone; the speaker says how far only roughly (`range`).
void add_marker(const fwg::World& world, const fwg::QuestRow& q, Fact& f) {
    if (q.anchor && !q.anchor->empty()) f.fields.emplace_back("marker", *q.anchor);
    if (const fwg::HeroQuestHook* hook = hook_of(world, q.quest_id); hook && hook->has_target_node)
        f.fields.emplace_back("marker_node", std::to_string(hook->target_node));
}

// An offer as the speaker would put it: what the giver asks and why they say they ask it.
// Never the hook's actual effect, and never whether the stated purpose is the true one -- the
// giver may not know, and the speaker certainly does not.
Fact offer_fact(const fwg::World& world, const Offer& offer, const std::vector<KnownPlace>& places, double today) {
    const fwg::QuestRow& q = offer.row;
    Fact f{FactKind::QuestOffer, q.quest_id, {}};
    if (q.giver_uid) {
        if (const std::optional<knowledge::KnownPerson> giver = knowledge::person_fact(world, *q.giver_uid)) {
            if (const std::string* name = giver->fact.field("name")) f.fields.emplace_back("giver", *name);
            if (const std::string* role = giver->fact.field("role")) f.fields.emplace_back("giver_role", *role);
        }
    }
    f.fields.emplace_back("from_speaker", offer.own ? "yes" : "no");
    if (q.verb) f.fields.emplace_back("verb", *q.verb);
    if (q.stated_purpose && !q.stated_purpose->empty()) f.fields.emplace_back("purpose", *q.stated_purpose);
    if (q.anchor)
        if (const std::optional<std::string> where = knowledge::anchor_name(world, *q.anchor)) f.fields.emplace_back("where", *where);
    if (const fwg::HeroQuestHook* hook = hook_of(world, q.quest_id)) {
        f.fields.emplace_back("difficulty", std::to_string(hook->difficulty));
        if (hook->has_target_node) {
            // How far is to the anchor's node, which is right whatever else stands there.
            for (const KnownPlace& place : places)
                if (place.node == hook->target_node) {
                    f.fields.emplace_back("range", knowledge::range_of(place.distance_m));
                    break;
                }
        }
    }
    add_marker(world, q, f);
    const double left = std::floor(q.expires_day - today);
    if (left >= 0.0) f.fields.emplace_back("days_left", std::to_string(static_cast<long long>(left)));
    return f;
}

Fact quest_state_fact(const std::string& quest_id, std::string_view state) {
    return Fact{FactKind::QuestState, quest_id, {{"state", std::string(state)}}};
}

// The nearest few of each kind, then all of them nearest first.
std::vector<Fact> surroundings(const std::vector<KnownPlace>& places) {
    std::map<std::string, std::size_t> taken;
    const auto cap = [](const std::string& kind) {
        if (kind == "city") return kSurroundingCities;
        if (kind == "hamlet") return kSurroundingHamlets;
        if (kind == "fortress") return kSurroundingFortresses;
        return kSurroundingLandmarks;
    };
    std::vector<Fact> out;
    for (const KnownPlace& place : places) {
        const std::string* kind = place.fact.field("kind");
        const std::string group = kind && (*kind == "city" || *kind == "hamlet" || *kind == "fortress") ? *kind : "landmark";
        if (taken[group] == cap(group)) continue;
        ++taken[group];
        out.push_back(place.fact);
    }
    return out;
}

int number_field(const Fact& fact, std::string_view key) {
    const std::string* value = fact.field(key);
    if (value == nullptr || value->empty()) return 0;
    try {
        return std::stoi(*value);
    } catch (...) {
        return 0;
    }
}

// ---- conversation ---------------------------------------------------------------------------

// How long ago, as someone would say it: by the calendar, so a meeting late yesterday was
// "yesterday" however few hours ago it was.
std::string since_band(double today, double then) {
    const double days = std::floor(today) - std::floor(then);
    if (days < 1.0) return "today";
    if (days < 2.0) return "yesterday";
    if (days < 7.0) return "days";
    if (days < 60.0) return "weeks";
    if (days < 730.0) return "months";
    return "years";
}

std::string mood_of(int disposition) {
    if (disposition >= 2) return "warm";
    if (disposition == 1) return "friendly";
    if (disposition == 0) return "neutral";
    if (disposition == -1) return "wary";
    return "hostile";
}

// Moves the speaker's regard, within its bounds; returns how far it moved.
int regard(Director::Acquaintance& a, int change) {
    const int before = a.disposition;
    a.disposition = std::clamp(a.disposition + change, -3, 3);
    return a.disposition - before;
}

// `heard`: how many times before this the player made this act to the speaker. `feeling` and
// `resting` on the fear ladder.
Fact memory_fact(const std::string& uid, const Director::Acquaintance& a, bool recognised, bool repeat, int heard, int feeling,
                 int resting, bool eased, bool joined) {
    Fact f{FactKind::Memory, uid, {}};
    const bool again = recognised && a.conversations > 1;
    f.fields = {{"met", again ? "again" : "first"},
                {"times", std::to_string(recognised ? std::max(0, a.conversations - 1) : 0)},
                {"since", recognised && again ? a.since : std::string()},
                {"mood", recognised ? mood_of(a.disposition) : std::string("hostile")},
                {"work", recognised ? a.work : std::string()},
                {"asked_before", recognised ? a.asked_before : std::string()},
                {"turn", std::to_string(a.turn)},
                {"repeat", repeat ? "yes" : "no"},
                {"heard", std::to_string(recognised ? heard : 0)},
                {"exposed", recognised && a.exposed ? "yes" : "no"},
                {"feeling", std::string(kFearWords[static_cast<std::size_t>(feeling)])},
                {"resting", std::string(kFearWords[static_cast<std::size_t>(resting)])},
                {"eased", recognised && eased ? "yes" : "no"},
                {"joined", joined ? "yes" : "no"}};
    return f;
}

// How someone feels at rest: uneasy where a nest of danger 4 or more lies within a kilometre of
// where they stand, else calm.
int resting_fear(const fwg::World& world, const knowledge::Speaker& s) {
    if (!s.node) return 0;
    for (const Fact& nest : knowledge::nests_around(world, *s.node, kUneasyNestM, 1))
        if (number_field(nest, "danger") >= kUneasyNestDanger) return 1;
    return 0;
}

// ---- small talk -------------------------------------------------------------------------------

std::string weather_word(double intensity_pct) {
    if (intensity_pct < 10.0) return "clear";
    if (intensity_pct < 30.0) return "fair";
    if (intensity_pct < 55.0) return "unsettled";
    if (intensity_pct < 80.0) return "stormy";
    return "a great storm";
}

std::string wind_word(double metres_per_second) {
    if (metres_per_second < 2.0) return "still";
    if (metres_per_second < 6.0) return "breezy";
    if (metres_per_second < 12.0) return "windy";
    return "a gale";
}

std::string season_word(double summer_pct) {
    if (summer_pct >= 80.0) return "high summer";
    if (summer_pct >= 60.0) return "summer";
    if (summer_pct >= 40.0) return "the turn of the season";
    if (summer_pct >= 20.0) return "winter";
    return "deep winter";
}

// The weather report's hour counts from sunrise.
std::string time_word(double hour) {
    if (hour < 1.0) return "dawn";
    if (hour < 4.0) return "morning";
    if (hour < 8.0) return "midday";
    if (hour < 11.0) return "afternoon";
    if (hour < 13.0) return "evening";
    return "night";
}

// What small talk can be about where the speaker stands, now. A world generated short of its
// seasons has none to read, and says so by saying nothing of them.
Fact chat_fact(const fwg::World& world, const Speaker& s, double today) {
    Fact f{FactKind::Chat, s.uid, {}};
    try {
        const fwg::WeatherReport report = fwg::weather_report(world, today);
        if (s.node) {
            const fwg::WeatherSample here = fwg::weather_sample_at_node(world, report, *s.node);
            f.fields.emplace_back("weather", weather_word(here.intensity_pct));
            f.fields.emplace_back("wind", wind_word(here.wind_speed_m_s));
            f.fields.emplace_back("season", season_word(here.summer_pct));
        }
        f.fields.emplace_back("time", time_word(report.hour));
    } catch (const fwg::WorldTimeError&) {
    }
    return f;
}

// ---- is it safe -------------------------------------------------------------------------------

bool within_a_walk(const Fact& place) {
    const std::string* range = place.field("range");
    return range && (*range == "here" || *range == "close" || *range == "near" || *range == "walk");
}

// The dangerous places in reach -- those within a walk first, each group worst then nearest --
// and a verdict on what is within a walk: a deadly place a long way off does not make here
// unsafe, though it is still named when nothing nearer is. The verdict's own reasons are always
// among the places named.
std::vector<Fact> safety_facts(const std::vector<KnownPlace>& places, const std::vector<Fact>& dangers) {
    std::vector<const KnownPlace*> dangerous;
    for (const KnownPlace& place : places)
        if (number_field(place.fact, "threat") >= 4) dangerous.push_back(&place);
    std::stable_sort(dangerous.begin(), dangerous.end(), [](const KnownPlace* a, const KnownPlace* b) {
        const bool na = within_a_walk(a->fact);
        const bool nb = within_a_walk(b->fact);
        if (na != nb) return na;
        const int ta = number_field(a->fact, "threat");
        const int tb = number_field(b->fact, "threat");
        if (ta != tb) return ta > tb;
        return a->distance_m < b->distance_m;
    });
    bool unsafe = false;
    bool wary = false;
    for (const KnownPlace* place : dangerous) {
        if (!within_a_walk(place->fact)) continue;
        wary = true;
        if (number_field(place->fact, "threat") >= 5) unsafe = true;
    }
    for (const Fact& nest : dangers) {
        const int level = number_field(nest, "danger");
        if (level >= 4) unsafe = true;
        if (level >= 3) wary = true;
    }
    std::vector<Fact> out;
    out.push_back(Fact{FactKind::Safety, "", {{"verdict", unsafe ? "unsafe" : wary ? "wary" : "safe"}}});
    for (std::size_t i = 0; i < dangerous.size() && i < kSafetyPlaces; ++i) out.push_back(dangerous[i]->fact);
    for (const Fact& nest : dangers) out.push_back(nest);
    return out;
}

// ---- faces ------------------------------------------------------------------------------------

enum class Faces { One, Mask, Beast, Fractured };

Faces faces_of(const fwg::Hero& hero) {
    const auto has = [&](std::string_view label) {
        return std::any_of(hero.faces.begin(), hero.faces.end(), [&](const fwg::HeroFace& f) { return f.label == label; });
    };
    if (has("public") && has("true")) return Faces::Mask;
    if (has("calm") && has("unleashed")) return Faces::Beast;
    if (has("first") && has("second")) return Faces::Fractured;
    return Faces::One;
}

const fwg::HeroFace* face_labelled(const fwg::Hero& hero, std::string_view label) {
    for (const fwg::HeroFace& face : hero.faces)
        if (face.label == label) return &face;
    return nullptr;
}

// FNV-1a, 64-bit: a hash with the same value everywhere, which std::hash is not.
std::uint64_t stable_hash(std::string_view text) {
    std::uint64_t h = 1469598103934665603ULL;
    for (const unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

// The full moon's day, or the day either side of it.
bool full_moon_near(const fwg::World& world, double today) {
    for (const double day : {today - 1.0, today, today + 1.0}) {
        if (day < 0.0) continue;
        try {
            const std::optional<fwg::MoonPhase> moon = fwg::moon_phase_on(world, day);
            if (moon && moon->full) return true;
        } catch (const fwg::WorldTimeError&) {
        }
    }
    return false;
}

// The face the player meets today: the stranger's face, unless a hidden one is out.
const fwg::HeroFace* face_shown(const fwg::World& world, const fwg::Hero& hero, const Director::Acquaintance& a,
                                double today) {
    switch (faces_of(hero)) {
        case Faces::Mask:
            return face_labelled(hero, a.exposed ? "true" : "public");
        case Faces::Beast:
            return face_labelled(hero, full_moon_near(world, today) ? "unleashed" : "calm");
        case Faces::Fractured: {
            // The two take turns, a day each, from a turn of the hero's own.
            const auto day = static_cast<std::uint64_t>(std::max(0.0, std::floor(today)));
            return face_labelled(hero, (day + stable_hash(hero.uid)) % 2 == 0 ? "first" : "second");
        }
        case Faces::One:
            break;
    }
    return knowledge::stranger_face(hero);
}

// Whether a confrontation lands: the player has cause to say it.
bool has_cause(Faces kind, const Director::Acquaintance& a) {
    switch (kind) {
        case Faces::Mask:
            return a.finished_covert || a.heard_truth || a.conversations >= kMeetingsThatSeeThrough;
        case Faces::Beast:
            return a.seen_unleashed;
        case Faces::Fractured:
            return a.seen_second;
        case Faces::One:
            break;
    }
    return false;
}

std::string_view hidden_label(Faces kind) {
    switch (kind) {
        case Faces::Mask:
            return "true";
        case Faces::Beast:
            return "unleashed";
        case Faces::Fractured:
            return "second";
        case Faces::One:
            break;
    }
    return {};
}

// Whether `speaker_uid` knows `hero` well enough to know a mask for one.
bool knows_them(const fwg::Hero& hero, const std::string& speaker_uid) {
    for (const std::vector<std::string>* list : {&hero.rivals, &hero.kin, &hero.allies})
        if (std::binary_search(list->begin(), list->end(), speaker_uid)) return true;
    return false;
}

// ---- days ---------------------------------------------------------------------------------------

constexpr double kMinutesPerDay = 1440.0;

// A roster post with a day: living, standing at a site, working on a plot of it. A cast record
// is a hero or a dread, who stands where the world says.
bool has_a_day(const fwg::NpcPerson& p) { return !p.is_cast && p.alive && p.site_uid && !p.plot_id.empty(); }

// Whether `s` knows the person `target`: their rival, kin or ally, wherever they are; else someone
// standing where the speaker stands, or somewhere within the speaker's reach. The one rule for an
// answer about a person and for a bystander who could give one.
bool knows_person(const fwg::World& world, knowledge::Speaker& s, const std::string& target) {
    const std::optional<knowledge::KnownPerson> person = knowledge::person_fact(world, target);
    if (!person || !person->node) return false;
    fwg::PersonReadRequest request;
    request.uid = target;
    const fwg::PersonRead read = fwg::read_person(world, request);
    const fwg::Hero* asked = std::get_if<fwg::Hero>(&read.person.person);
    const fwg::Hero* hero = knowledge::hero_of(s);
    if ((asked && knows_them(*asked, s.uid)) || (hero && knows_them(*hero, target))) return true;
    if (s.node && *person->node == *s.node) return true;
    for (const KnownPlace& place : knowledge::places_in_reach(world, s))
        if (place.node == *person->node) return true;
    return false;
}

// The roster post with a day that `uid` names, or nullptr.
const fwg::NpcPerson* post_with_a_day(const fwg::World& world, const std::string& uid) {
    for (const fwg::NpcPerson& p : world.npcs.people) {
        if (p.uid == uid) return has_a_day(p) ? &p : nullptr;
    }
    return nullptr;
}

bool names_a_person(const fwg::World& world, const std::string& uid) {
    fwg::PersonReadRequest request;
    request.uid = uid;
    try {
        (void)fwg::read_person(world, request);
        return true;
    } catch (const fwg::WorldTimeError&) {
        return false;
    }
}

// A post's ordinal in its building, the last part of its uid ("...-smithy-1"): which of a
// watch's shifts it keeps.
int ordinal_of(const std::string& uid) {
    const std::size_t dash = uid.rfind('-');
    if (dash == std::string::npos) return 0;
    int ordinal = 0;
    const std::from_chars_result read = std::from_chars(uid.data() + dash + 1, uid.data() + uid.size(), ordinal);
    return read.ec == std::errc{} ? ordinal : 0;
}

schedule::Person schedule_of(const fwg::NpcPerson& p) {
    return {schedule::hours_of(p.building_id.value_or(""), ordinal_of(p.uid)),
            p.commute_m ? schedule::walk_days(*p.commute_m) : 0.0};
}

const std::vector<Detour>& detours_in(const std::map<std::string, std::vector<Detour>>& all, const std::string& uid) {
    static const std::vector<Detour> none;
    const auto found = all.find(uid);
    return found == all.end() ? none : found->second;
}

Whereabouts whereabouts_of(const fwg::NpcPerson& p, const std::vector<Detour>& detours, double now) {
    Whereabouts w;
    w.person = p.uid;
    w.site = p.site_uid.value_or("");
    w.workplace_plot = p.plot_id;
    w.home_plot = p.home_plot_id.value_or("");
    w.day = now;
    w.stint = schedule::stint_at(schedule_of(p), detours, now);
    w.along = schedule::along_at(w.stint, now);
    return w;
}

}  // namespace

std::string_view to_string(Overheard reaction) {
    switch (reaction) {
        case Overheard::stays_out: return "stays_out";
        case Overheard::calmed: return "calmed";
        case Overheard::joins: return "joins";
    }
    return "";
}

std::string_view to_string(Doing doing) {
    switch (doing) {
        case Doing::asleep: return "asleep";
        case Doing::at_home: return "at_home";
        case Doing::walking: return "walking";
        case Doing::working: return "working";
        case Doing::held: return "held";
    }
    return "";
}

Director::Director(fwg::World world) : world_(std::move(world)) {}

const Director::Acquaintance* Director::acquaintance(const std::string& speaker) const {
    const auto found = acquainted_.find(speaker);
    return found == acquainted_.end() ? nullptr : &found->second;
}

void Director::end_conversation(const std::string& speaker) {
    const auto found = acquainted_.find(speaker);
    if (found == acquainted_.end() || !found->second.talking) return;
    Acquaintance& a = found->second;
    a.talking = false;
    // What was asked last time, not ever: kept on, it was asked after on every later visit ("Still
    // after Dunweg?" three visits running, judged 2026-09-26).
    a.asked_before = a.asked_now;
    a.asked_now.clear();
    if (talking_to_ == speaker) talking_to_.clear();
}

void Director::advance(double days) {
    if (!std::isfinite(days) || days < 0.0) throw Error("advance: days must be a finite number, zero or more");
    if (days >= fwg::kAgeDays)
        throw Error("advance: " + std::to_string(days) + " days is an age or more; turning an age is not a conversation's clock");
    fwg::TimeAdvanceRequest request;
    request.elapsed = fwg::TimeElapsed{fwg::TimeUnit::Days, days};
    fwg::TimeAdvanceResult result = [&] {
        try {
            return fwg::advance_world_time(world_, request);
        } catch (const fwg::WorldTimeError& refusal) {
            throw Error(std::string("advance: the world refused the span: ") + refusal.what());
        }
    }();
    if (result.report.tick.blocked)
        throw Error("advance: the world refused the span: " + result.report.tick.blocked->message);
    world_ = std::move(result.world);
    // Time passing is walking away: every conversation under way ends.
    for (auto& [uid, a] : acquainted_)
        if (a.talking) end_conversation(uid);
    inputs_.push_back(Advance{days});
}

void Director::resolve(const std::string& quest_id, bool completed) {
    const std::optional<std::vector<fwg::QuestRow>> taken = board(world_, fwg::QuestState::Taken);
    const bool held = taken && std::any_of(taken->begin(), taken->end(), [&](const fwg::QuestRow& q) { return q.quest_id == quest_id; });
    if (!held) throw Error("resolve: no taken work '" + quest_id + "' on the board");
    fwg::TimeAdvanceRequest request;
    request.elapsed = fwg::TimeElapsed{fwg::TimeUnit::Days, 0.0};
    request.resolutions.push_back(
        fwg::QuestResolution{quest_id, completed ? fwg::QuestOutcome::Completed : fwg::QuestOutcome::Failed});
    fwg::TimeAdvanceResult result = [&] {
        try {
            return fwg::advance_world_time(world_, request);
        } catch (const fwg::WorldTimeError& refusal) {
            throw Error(std::string("resolve: the world refused it: ") + refusal.what());
        }
    }();
    if (result.report.tick.blocked) throw Error("resolve: the world refused it: " + result.report.tick.blocked->message);
    world_ = std::move(result.world);
    // The giver remembers: work done for them, or let fail. Finishing what was asked under a
    // false purpose shows the player what it was for.
    if (const fwg::HeroQuestHook* hook = hook_of(world_, quest_id); hook && !hook->giver_uid.empty()) {
        Acquaintance& giver = acquainted_[hook->giver_uid];
        giver.work = completed ? "finished" : "failed";
        regard(giver, completed ? kWorkFinished : kWorkFailed);
        if (completed && hook->unwitting) giver.finished_covert = true;
    }
    inputs_.push_back(Resolve{quest_id, completed});
}

FactPacket Director::respond(const std::string& speaker_uid, const DialogueAct& act) { return reply(speaker_uid, act, false); }

FactPacket Director::reply(const std::string& speaker_uid, const DialogueAct& act, bool joined) {
    if (act_needs_target(act.kind) && act.target.empty())
        throw Error("respond: " + std::string(to_string(act.kind)) + " needs a target");
    std::optional<Speaker> found = knowledge::speaker(world_, speaker_uid);
    if (!found) throw Error("respond: the world names no person '" + speaker_uid + "'");
    Speaker& s = *found;
    const double today = fwg::world_clock_of(world_).day;
    Acquaintance& a = acquainted_[speaker_uid];

    const int resting = resting_fear(world_, s);
    if (!s.present) {
        FactPacket p = packet_for(s, act, Outcome::Refused);
        p.facts.push_back(reason("gone", (s.persona.name.empty() ? std::string("the one spoken to") : s.persona.name) + " is gone"));
        // Nobody heard it, so nothing is counted.
        const auto heard = a.heard.find(act.kind);
        p.facts.push_back(memory_fact(speaker_uid, a, true, false, heard == a.heard.end() ? 0 : heard->second,
                                      fear_now(speaker_uid, resting, today), resting,
                                      a.eased_day >= 0.0 && today - a.eased_day < kEasedDays, joined));
        if (!joined) inputs_.push_back(Say{speaker_uid, act});
        last_ = Exchange{speaker_uid, act, p.outcome, true};
        return p;
    }

    // The conversation: turning to this speaker ends the last one, and a speaker not already in
    // one begins another.
    if (!talking_to_.empty() && talking_to_ != speaker_uid) end_conversation(talking_to_);
    if (!a.talking) {
        ++a.conversations;
        a.since = a.conversations > 1 ? since_band(today, a.last_day) : std::string();
        a.turn = 0;
        a.said.clear();
        a.thanked = false;
        a.calmed = false;
        a.talking = true;
    }
    talking_to_ = speaker_uid;
    ++a.turn;
    const bool repeat = std::find(a.said.begin(), a.said.end(), std::pair{act.kind, act.target}) != a.said.end();
    a.said.emplace_back(act.kind, act.target);
    const int heard = a.heard[act.kind]++;

    // The face the player meets.
    const fwg::Hero* hero = knowledge::hero_of(s);
    const Faces kind = hero ? faces_of(*hero) : Faces::One;
    bool recognised = true;
    const auto show = [&] {
        if (kind == Faces::One) return;
        if (const fwg::HeroFace* face = face_shown(world_, *hero, a, today)) {
            knowledge::show_face(s, *hero, *face);
            if (face->label == "unleashed") {
                a.seen_unleashed = true;
                recognised = false;  // the unleashed face does not recognise allies
            }
            if (face->label == "second") a.seen_second = true;
        }
    };
    show();

    const auto finish = [&](FactPacket p) {
        p.facts.push_back(memory_fact(speaker_uid, a, recognised, repeat, heard, fear_now(speaker_uid, resting, today), resting,
                                      a.eased_day >= 0.0 && today - a.eased_day < kEasedDays, joined));
        a.last_day = today;
        if (act.kind == ActKind::Farewell) end_conversation(speaker_uid);
        if (!joined) inputs_.push_back(Say{speaker_uid, act});
        // What a bystander overhears: an answer that held nothing, a place or person unknown or no
        // work, is one they may answer instead.
        const bool no_work = act.kind == ActKind::AskForWork &&
                             std::none_of(p.facts.begin(), p.facts.end(), [](const Fact& f) { return f.kind == FactKind::QuestOffer; });
        last_ = Exchange{speaker_uid, act, p.outcome, p.outcome == Outcome::Unknown || no_work};
        return p;
    };

    switch (act.kind) {
        case ActKind::NoAct:
            return finish(packet_for(s, act, Outcome::NotUnderstood));

        case ActKind::Farewell:
            return finish(packet_for(s, act, Outcome::Parted));

        case ActKind::Greet:
            return finish(packet_for(s, act, Outcome::Greeted));

        case ActKind::Answer:
            return finish(packet_for(s, act, Outcome::Acknowledged));

        case ActKind::Thank: {
            if (!a.thanked) regard(a, kThanked);
            a.thanked = true;
            return finish(packet_for(s, act, Outcome::Thanked));
        }

        case ActKind::Insult:
            // A grievance, kept at what it cost: at the bottom of the scale an insult costs less.
            a.grievances.push_back(regard(a, kInsulted));
            return finish(packet_for(s, act, Outcome::Offended));

        case ActKind::Apologize: {
            // An apology eases the latest grievance and gives back what it cost, so an insult and
            // an apology leave the speaker where they were. Said again once every one is
            // forgiven, it is taken again, and nothing moves: "nothing to forgive" there would deny
            // the wrong. To a face that does not know the player, or from one who never wronged
            // the speaker, there is nothing to forgive.
            if (!recognised) return finish(packet_for(s, act, Outcome::Unwronged));
            if (a.grievances.empty()) return finish(packet_for(s, act, a.forgiven > 0 ? Outcome::Forgiven : Outcome::Unwronged));
            regard(a, -a.grievances.back());
            a.grievances.pop_back();
            ++a.forgiven;
            return finish(packet_for(s, act, Outcome::Forgiven));
        }

        case ActKind::Calm: {
            // Calming words ease a frightened speaker a step, toward how they feel at rest: not
            // while what frightens them is still there, and not from someone they do not trust (a
            // hostile speaker, or a face that does not know the player).
            const int now = fear_now(speaker_uid, resting, today);
            if (now <= resting) return finish(packet_for(s, act, Outcome::NotAfraid));
            Fear& fear = fears_[speaker_uid];
            const bool threat = fear.threat_day >= 0.0 && (today - fear.threat_day) * kMinutesPerDay < kThreatMinutes;
            if (threat || !recognised || a.disposition <= -2) return finish(packet_for(s, act, Outcome::StillAfraid));
            fear.level = now - 1;
            fear.since_day = today;
            if (!a.calmed) regard(a, kCalmed);
            a.calmed = true;
            a.eased_day = today;
            return finish(packet_for(s, act, Outcome::Calmed));
        }

        case ActKind::SmallTalk: {
            FactPacket p = packet_for(s, act, Outcome::Chatted);
            p.facts.push_back(chat_fact(world_, s, today));
            // The worst nest near, the nearest of the worst: what people talk about. The list
            // comes worst first.
            const std::vector<Fact> dangers = knowledge::dangers_near(world_, s);
            if (!dangers.empty()) p.facts.push_back(dangers.front());
            return finish(std::move(p));
        }

        case ActKind::AskSafety: {
            FactPacket p = packet_for(s, act, Outcome::Answered);
            const std::vector<KnownPlace> places = knowledge::places_in_reach(world_, s);
            p.facts = safety_facts(places, knowledge::dangers_near(world_, s));
            return finish(std::move(p));
        }

        case ActKind::Confront: {
            if (kind == Faces::One || !has_cause(kind, a)) return finish(packet_for(s, act, Outcome::Denied));
            const fwg::HeroFace* known = knowledge::stranger_face(*hero);
            const fwg::HeroFace* hidden = face_labelled(*hero, hidden_label(kind));
            a.exposed = true;
            show();  // a mask exposed shows its true face from now on
            FactPacket p = packet_for(s, act, Outcome::Exposed);
            p.facts.push_back(Fact{FactKind::Reveal,
                                   speaker_uid,
                                   {{"face", std::string(hidden_label(kind))},
                                    {"was", known ? known->apparent_role : std::string()},
                                    {"is", hidden ? hidden->apparent_role : std::string()}}});
            return finish(std::move(p));
        }

        case ActKind::AskSurroundings: {
            FactPacket p = packet_for(s, act, Outcome::Answered);
            const std::vector<KnownPlace> places = knowledge::places_in_reach(world_, s);
            p.facts = surroundings(places);
            for (Fact& danger : knowledge::dangers_near(world_, s)) p.facts.push_back(std::move(danger));
            return finish(std::move(p));
        }

        case ActKind::AskAboutPlace: {
            const std::vector<KnownPlace> places = knowledge::places_in_reach(world_, s);
            for (const KnownPlace& place : places) {
                if (place.fact.subject != act.target) continue;
                FactPacket p = packet_for(s, act, Outcome::Answered);
                // What the speaker knows of it past the place itself: its trades and the heroes who
                // hold a post there, the nests near it, and the work there they know of ("Tell me
                // more about that place" had only the place's own phrase again; judged 2026-09-26).
                const knowledge::PlaceFolk folk = knowledge::folk_at(world_, place.fact.subject, speaker_uid);
                Fact about = place.fact;
                if (!folk.trades.empty()) {
                    std::string trades;
                    for (const std::string& trade : folk.trades) trades += (trades.empty() ? "" : ", ") + trade;
                    about.fields.emplace_back("trades", trades);
                }
                p.facts.push_back(std::move(about));
                for (const std::string& uid : folk.heroes)
                    if (std::optional<knowledge::KnownPerson> there = knowledge::person_fact(world_, uid))
                        if (const std::string* status = there->fact.field("status"); status && *status == "about")
                            p.facts.push_back(there->fact);
                for (Fact& nest : knowledge::nests_around(world_, place.node, kAroundPlaceM, kNestsAround)) p.facts.push_back(std::move(nest));
                for (const Offer& offer : offers_known_to(world_, s))
                    if (const fwg::HeroQuestHook* hook = hook_of(world_, offer.row.quest_id);
                        hook && hook->has_target_node && hook->target_node == place.node)
                        p.facts.push_back(offer_fact(world_, offer, places, today));
                a.asked_now = knowledge::place_label(place.fact);
                return finish(std::move(p));
            }
            return finish(packet_for(s, act, Outcome::Unknown));
        }

        case ActKind::AskAboutPerson: {
            if (!knows_person(world_, s, act.target)) return finish(packet_for(s, act, Outcome::Unknown));
            std::optional<knowledge::KnownPerson> person = knowledge::person_fact(world_, act.target);
            fwg::PersonReadRequest request;
            request.uid = act.target;
            const fwg::PersonRead read = fwg::read_person(world_, request);
            const fwg::Hero* asked = std::get_if<fwg::Hero>(&read.person.person);
            // Someone who knows a masked hero well -- a rival, kin, an ally -- knows the mask for
            // one, and says so: how a player learns to confront it.
            if (asked && faces_of(*asked) == Faces::Mask && knows_them(*asked, speaker_uid)) {
                person->fact.fields.emplace_back("hides", "yes");
                acquainted_[act.target].heard_truth = true;
            }
            FactPacket p = packet_for(s, act, Outcome::Answered);
            if (const std::string* name = person->fact.field("name")) a.asked_now = *name;
            p.facts.push_back(person->fact);
            return finish(std::move(p));
        }

        case ActKind::AskForWork: {
            FactPacket p = packet_for(s, act, Outcome::Answered);
            const std::vector<KnownPlace> places = knowledge::places_in_reach(world_, s);
            for (const Offer& offer : offers_known_to(world_, s)) {
                p.facts.push_back(offer_fact(world_, offer, places, today));
                if (offer.own && a.work.empty()) a.work = "offered";
            }
            return finish(std::move(p));
        }

        case ActKind::DeclineQuest: {
            for (const Offer& offer : offers_known_to(world_, s)) {
                if (offer.row.quest_id != act.target) continue;
                if (offer.own) a.work = "declined";
                return finish(packet_for(s, act, Outcome::Declined));
            }
            return finish(packet_for(s, act, Outcome::Unknown));
        }

        case ActKind::AcceptQuest: {
            const std::vector<Offer> offers = offers_known_to(world_, s);
            const auto offer = std::find_if(offers.begin(), offers.end(), [&](const Offer& o) { return o.row.quest_id == act.target; });
            if (offer == offers.end()) return finish(packet_for(s, act, Outcome::Unknown));
            if (!offer->own) {
                // Taking work is agreed with whoever gives it: send the player to them.
                FactPacket p = packet_for(s, act, Outcome::Refused);
                std::optional<knowledge::KnownPerson> giver;
                if (offer->row.giver_uid) giver = knowledge::person_fact(world_, *offer->row.giver_uid);
                p.facts.push_back(reason("not_theirs_to_give", "the work is " + whose(giver) + " to give"));
                if (giver) p.facts.push_back(giver->fact);
                return finish(std::move(p));
            }
            fwg::QuestRequest request;
            request.actions.push_back(fwg::QuestAction{act.target, fwg::QuestActionKind::Take, std::nullopt});
            try {
                fwg::QuestRequestResult result = fwg::world_quest_request(world_, request);
                world_ = std::move(result.world);
            } catch (const fwg::WorldTimeError& refusal) {
                if (!refusal.refused_by_world()) throw;
                // Never the generator's message: it names quests by id, and lists work the speaker
                // may know nothing of. An offer lapses at a moment of a day and leaves the board
                // at the next day's sweep, so between the two, taking it is refused.
                const bool lapsed = offer->row.expires_day < fwg::world_clock_of(world_).day;
                FactPacket p = packet_for(s, act, Outcome::Refused);
                p.facts.push_back(reason("refused_by_world", lapsed ? "the offer has lapsed" : "the work cannot be taken now"));
                return finish(std::move(p));
            }
            a.work = "taken";
            regard(a, kWorkTaken);
            FactPacket p = packet_for(s, act, Outcome::Accepted);
            Fact taken = quest_state_fact(act.target, "taken");
            add_marker(world_, offer->row, taken);  // the game marks taken work on its map
            p.facts.push_back(std::move(taken));
            return finish(std::move(p));
        }

        case ActKind::AbandonQuest: {
            const std::optional<std::vector<fwg::QuestRow>> taken = board(world_, fwg::QuestState::Taken);
            if (!taken) return finish(packet_for(s, act, Outcome::Unknown));
            const auto row = std::find_if(taken->begin(), taken->end(), [&](const fwg::QuestRow& q) { return q.quest_id == act.target; });
            if (row == taken->end()) return finish(packet_for(s, act, Outcome::Unknown));
            if (!row->giver_uid || *row->giver_uid != s.uid) {
                FactPacket p = packet_for(s, act, Outcome::Refused);
                std::optional<knowledge::KnownPerson> giver;
                if (row->giver_uid) giver = knowledge::person_fact(world_, *row->giver_uid);
                p.facts.push_back(reason("not_theirs_to_release", "the work is " + whose(giver) + " to release"));
                if (giver) p.facts.push_back(giver->fact);
                return finish(std::move(p));
            }
            fwg::QuestRequest request;
            request.actions.push_back(fwg::QuestAction{act.target, fwg::QuestActionKind::Abandon, std::nullopt});
            try {
                fwg::QuestRequestResult result = fwg::world_quest_request(world_, request);
                world_ = std::move(result.world);
            } catch (const fwg::WorldTimeError& refusal) {
                if (!refusal.refused_by_world()) throw;
                // Never the generator's message, as in taking work.
                FactPacket p = packet_for(s, act, Outcome::Refused);
                p.facts.push_back(reason("refused_by_world", "the work cannot be given up now"));
                return finish(std::move(p));
            }
            a.work = "abandoned";
            a.grievances.push_back(regard(a, kWorkAbandoned));
            FactPacket p = packet_for(s, act, Outcome::Abandoned);
            p.facts.push_back(quest_state_fact(act.target, "abandoned"));
            return finish(std::move(p));
        }
    }
    throw Error("respond: an act outside the contract");
}

void Director::hold(const std::string& person, double minutes) {
    if (!std::isfinite(minutes) || minutes <= 0.0 || minutes > kMinutesPerDay)
        throw Error("hold: minutes must be more than zero and at most a day");
    const fwg::NpcPerson* post = post_with_a_day(world_, person);
    if (post == nullptr && !names_a_person(world_, person)) throw Error("hold: the world names no person '" + person + "'");
    if (post != nullptr) {
        const double now = fwg::world_clock_of(world_).day;
        schedule::hold(schedule_of(*post), detours_[person], now, now + minutes / kMinutesPerDay);
    }
    inputs_.push_back(Hold{person, minutes});
}

std::optional<Whereabouts> Director::whereabouts(const std::string& person) const {
    const fwg::NpcPerson* post = post_with_a_day(world_, person);
    if (post == nullptr) return std::nullopt;
    return whereabouts_of(*post, detours_in(detours_, person), fwg::world_clock_of(world_).day);
}

std::vector<Stint> Director::day_of(const std::string& person, double day) const {
    if (!std::isfinite(day)) throw Error("day_of: the day must be a finite number");
    const fwg::NpcPerson* post = post_with_a_day(world_, person);
    if (post == nullptr) return {};
    const double sunrise = std::floor(day);
    return schedule::stints(schedule_of(*post), detours_in(detours_, person), sunrise, sunrise + 1.0);
}

std::vector<Whereabouts> Director::whereabouts_at(const std::string& site) const {
    std::vector<Whereabouts> out;
    const double now = fwg::world_clock_of(world_).day;
    for (const fwg::NpcPerson& p : world_.npcs.people) {
        if (has_a_day(p) && *p.site_uid == site) out.push_back(whereabouts_of(p, detours_in(detours_, p.uid), now));
    }
    return out;
}

std::vector<Position> Director::positions() const {
    const double now = fwg::world_clock_of(world_).day;
    const std::vector<fwg::NpcPerson>& people = world_.npcs.people;
    std::vector<Position> out;
    out.reserve(people.size());
    for (std::size_t i = 0; i < people.size(); ++i) {
        const fwg::NpcPerson& p = people[i];
        if (!has_a_day(p)) continue;
        out.push_back({static_cast<std::uint32_t>(i), schedule::stint_at(schedule_of(p), detours_in(detours_, p.uid), now)});
    }
    return out;
}

int Director::fear_now(const std::string& uid, int resting, double now) const {
    const auto found = fears_.find(uid);
    if (found == fears_.end()) return resting;
    const Fear& fear = found->second;
    const double minutes = std::max(0.0, now - fear.since_day) * kMinutesPerDay;
    const int eased = static_cast<int>(std::min(3.0, std::floor(minutes / kEaseMinutes)));
    return std::max(resting, fear.level - eased);
}

void Director::fright(const std::string& person, int level) {
    if (level < 1 || level > 3) throw Error("fright: the level must be 1 (uneasy), 2 (afraid) or 3 (panicked)");
    const std::optional<knowledge::Speaker> s = knowledge::speaker(world_, person);
    if (!s) throw Error("fright: the world names no person '" + person + "'");
    const double now = fwg::world_clock_of(world_).day;
    Fear& fear = fears_[person];
    fear.level = std::max(level, fear_now(person, resting_fear(world_, *s), now));
    fear.since_day = now;
    fear.threat_day = now;
    inputs_.push_back(Fright{person, level});
}

void Director::gather(const std::vector<std::string>& people) {
    const double now = fwg::world_clock_of(world_).day;
    std::vector<int> felt;
    for (const std::string& uid : people) {
        const std::optional<knowledge::Speaker> s = knowledge::speaker(world_, uid);
        if (!s) throw Error("gather: the world names no person '" + uid + "'");
        felt.push_back(fear_now(uid, resting_fear(world_, *s), now));
    }
    // Each from what the others felt before any of them moved.
    for (std::size_t i = 0; i < people.size(); ++i) {
        int most = 0, second = 0;
        for (std::size_t j = 0; j < people.size(); ++j) {
            if (j == i || people[j] == people[i]) continue;
            if (felt[j] > most) {
                second = most;
                most = felt[j];
            } else {
                second = std::max(second, felt[j]);
            }
        }
        const int spread = std::max(most - 1, second);
        if (spread <= felt[i]) continue;
        Fear& fear = fears_[people[i]];
        fear.level = spread;
        fear.since_day = now;
    }
    inputs_.push_back(Gather{people});
}

Feeling Director::feeling(const std::string& person) const {
    const std::optional<knowledge::Speaker> s = knowledge::speaker(world_, person);
    if (!s) throw Error("feeling: the world names no person '" + person + "'");
    const int resting = resting_fear(world_, *s);
    const int now = fear_now(person, resting, fwg::world_clock_of(world_).day);
    return {std::string(kFearWords[static_cast<std::size_t>(now)]), std::string(kFearWords[static_cast<std::size_t>(resting)])};
}

std::vector<Bystander> Director::overhear(const std::vector<std::string>& people) {
    if (!last_) throw Error("overhear: nothing has been said yet");
    const Exchange heard = *last_;  // a bystander who joins makes their own exchange the last
    const double today = fwg::world_clock_of(world_).day;
    std::vector<Bystander> out;
    bool joined = false;
    for (const std::string& uid : people) {
        std::optional<knowledge::Speaker> s = knowledge::speaker(world_, uid);
        if (!s) throw Error("overhear: the world names no person '" + uid + "'");
        Bystander b{uid, Overheard::stays_out, std::nullopt};
        const auto known = acquainted_.find(uid);
        const int regard = known == acquainted_.end() ? 0 : known->second.disposition;
        const int resting = resting_fear(world_, *s);
        const int now = fear_now(uid, resting, today);
        const auto found = fears_.find(uid);
        const bool threat = found != fears_.end() && found->second.threat_day >= 0.0 &&
                            (today - found->second.threat_day) * kMinutesPerDay < kThreatMinutes;
        if (uid == heard.speaker || !s->present) {
            // The one spoken to heard nothing new; someone gone hears nothing at all.
        } else if (heard.act.kind == ActKind::Calm && heard.outcome == Outcome::Calmed) {
            if (now > resting && !threat && regard > -2) {
                Fear& fear = fears_[uid];
                fear.level = now - 1;
                fear.since_day = today;
                b.reaction = Overheard::calmed;
            }
        } else if (!joined && heard.nothing && now < 3 && regard > -2) {
            bool can = false;
            if (heard.act.kind == ActKind::AskAboutPlace) {
                const std::vector<KnownPlace> places = knowledge::places_in_reach(world_, *s);
                can = std::any_of(places.begin(), places.end(), [&](const KnownPlace& p) { return p.fact.subject == heard.act.target; });
            } else if (heard.act.kind == ActKind::AskAboutPerson) {
                can = knows_person(world_, *s, heard.act.target);
            } else if (heard.act.kind == ActKind::AskForWork) {
                can = !offers_known_to(world_, *s).empty();
            }
            if (can) {
                b.packet = reply(uid, heard.act, true);
                b.reaction = Overheard::joins;
                joined = true;
            }
        }
        out.push_back(std::move(b));
    }
    inputs_.push_back(Overhear{people});
    return out;
}

Director replay(fwg::World start, const std::vector<Input>& inputs) {
    Director director(std::move(start));
    for (const Input& input : inputs) {
        if (const Advance* advance = std::get_if<Advance>(&input)) {
            director.advance(advance->days);
        } else if (const Fright* fright = std::get_if<Fright>(&input)) {
            director.fright(fright->person, fright->level);
        } else if (const Gather* gathering = std::get_if<Gather>(&input)) {
            director.gather(gathering->people);
        } else if (const Overhear* overheard = std::get_if<Overhear>(&input)) {
            (void)director.overhear(overheard->people);
        } else if (const Resolve* resolution = std::get_if<Resolve>(&input)) {
            director.resolve(resolution->quest_id, resolution->completed);
        } else if (const Hold* held = std::get_if<Hold>(&input)) {
            director.hold(held->person, held->minutes);
        } else {
            const Say& say = std::get<Say>(input);
            (void)director.respond(say.speaker, say.act);
        }
    }
    return director;
}

}  // namespace orchestrator
