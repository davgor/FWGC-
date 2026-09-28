#include "orchestrator/director.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <tuple>

#include "brains.hpp"
#include "fwg/api/json_value.hpp"
#include "fwg/api/quests.hpp"
#include "fwg/api/read.hpp"
#include "fwg/api/weather.hpp"
#include "fwg/api/world_time.hpp"
#include "knowledge.hpp"
#include "schedule.hpp"
#include "utf8.hpp"

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

// Standing (contract version 12): how far the stranger and a speaker have come, never falling.
// Regular once they have talked on this many calendar days; a favourite at this many days and
// exchanges, the most a roster person comes to.
constexpr int kRegularDays = 3;
constexpr int kFavouriteDays = 10;
constexpr int kFavouriteExchanges = 30;
// The longest a line or a reply words() keeps may be, in bytes.
constexpr std::size_t kMaxWordsBytes = 4000;
// What outcomes handed to compact() may keep: this many of the stranger's own words, each this long at
// most, and what they last spoke of this long at most.
constexpr std::size_t kMostQuotes = 5;
constexpr std::size_t kMaxQuoteBytes = 240;
constexpr std::size_t kMaxLastBytes = 300;
// The longest outcomes handed in may be, in bytes: many times what the most they may keep takes.
constexpr std::size_t kMaxOutcomesBytes = 16000;

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

// How far the stranger and a speaker have come (contract version 12), from counts that only grow.
std::string standing_of(const Director::Acquaintance& a, bool hero) {
    if (hero) return "hero";
    if (a.conversations == 0) return "stranger";
    int exchanges = 0;
    for (const auto& [kind, times] : a.heard) exchanges += times;
    if (a.days_met >= kFavouriteDays && exchanges >= kFavouriteExchanges) return "favourite";
    if (a.days_met >= kRegularDays) return "regular";
    return "acquainted";
}

// `heard`: how many times before this the player made this act to the speaker. `feeling` and
// `resting` on the fear ladder.
Fact memory_fact(const std::string& uid, const Director::Acquaintance& a, bool recognised, bool repeat, int heard, int feeling,
                 int resting, bool eased, bool joined, const std::string& standing) {
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
                {"joined", joined ? "yes" : "no"},
                {"standing", standing}};
    return f;
}

// What a fact the reply gave is called, as a speaker names it: a place as they say it ("the hamlet
// of Wenweg"), a person by name, work by why its giver says they ask it, a nest by its creature.
// Empty for anything else.
std::string called(const Fact& fact) {
    const auto field = [&](std::string_view key) {
        const std::string* value = fact.field(key);
        return value ? *value : std::string();
    };
    switch (fact.kind) {
        case FactKind::Place: return knowledge::place_label(fact);
        case FactKind::Person: return field("name");
        case FactKind::QuestOffer: return field("purpose");
        case FactKind::Danger: return field("creature");
        default: return {};
    }
}

// Whether a fact is one a reply names something by: a place, a person, work, a nest.
bool names_something(const Fact& fact) {
    return fact.kind == FactKind::Place || fact.kind == FactKind::Person || fact.kind == FactKind::QuestOffer ||
           fact.kind == FactKind::Danger;
}

// What the stranger did, as long memory keeps it, from what came of an act; empty for an outcome
// that is no deed.
std::string deed_of(Outcome outcome) {
    switch (outcome) {
        case Outcome::Accepted: return "taken";
        case Outcome::Abandoned: return "given_up";
        case Outcome::Thanked: return "thanked";
        case Outcome::Offended: return "insulted";
        case Outcome::Forgiven: return "apologised";
        case Outcome::Calmed: return "calmed";
        case Outcome::Exposed: return "exposed";
        default: return {};
    }
}

// What becomes of work, as the one who gave it knows it, that long memory keeps over its taking.
bool work_ended(std::string_view state) { return state == "finished" || state == "failed" || state == "given_up"; }

bool ascii_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// The words of `text`, each with whether a sentence starts with it: runs of letters, apostrophes
// and whatever is not ASCII, a possessive's "'s" left off ("Wenweg's" is Wenweg). By ASCII alone,
// whatever locale the game has set.
std::vector<std::pair<std::string, bool>> words_of(std::string_view text) {
    std::vector<std::pair<std::string, bool>> out;
    bool start = true;
    std::string word;
    const auto flush = [&] {
        while (!word.empty() && word.back() == '\'') word.pop_back();
        if (word.size() > 2 && word.compare(word.size() - 2, 2, "'s") == 0) word.resize(word.size() - 2);
        if (!word.empty()) {
            out.emplace_back(word, start);
            start = false;
        }
        word.clear();
    };
    for (const char c : text) {
        if (ascii_letter(c) || c == '\'' || static_cast<unsigned char>(c) >= 0x80) {
            word += c;
            continue;
        }
        flush();
        if (c == '.' || c == '!' || c == '?' || c == ':' || c == '"') start = true;
    }
    flush();
    return out;
}

// The names `text` uses: its capitalised words past a sentence's start, but "I".
std::vector<std::string> names_used(std::string_view text) {
    std::vector<std::string> out;
    for (const auto& [word, start] : words_of(text))
        if (!start && word[0] >= 'A' && word[0] <= 'Z' && word != "I") out.push_back(word);
    return out;
}

// `text` without the blanks at either end.
std::string trimmed(std::string_view text) {
    const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (!text.empty() && blank(text.front())) text.remove_prefix(1);
    while (!text.empty() && blank(text.back())) text.remove_suffix(1);
    return std::string(text);
}

// `r` into `all`: the same memory again (the same kind, subject, act and words) counts once more
// and keeps when it first was; anything else is a memory of its own, after the rest.
void fold_in(std::vector<Remembered>& all, Remembered r) {
    for (Remembered& had : all) {
        if (had.memory != r.memory || had.subject != r.subject || had.act != r.act || had.words != r.words) continue;
        had.times += r.times;
        had.first_day = std::min(had.first_day, r.first_day);
        had.last_day = std::max(had.last_day, r.last_day);
        if (!r.about.empty()) had.about = r.about;
        had.source = r.source;
        return;
    }
    all.push_back(std::move(r));
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

// ---- events -------------------------------------------------------------------------------------

// How fast word travels, and how far past where it starts each kind of news goes: a day's ride a
// day; the end of a piece of work as far as its difficulty says, from the giver's place and from
// the place it was for; a mask pulled off, or a threat beaten or lost, twenty kilometres; anything
// else no further than where it happened.
constexpr double kWordMetresPerDay = 20000.0;
constexpr std::array<double, 5> kWorkWordM = {5000.0, 10000.0, 20000.0, 40000.0, 60000.0};
constexpr double kExposedWordM = 20000.0;
constexpr double kThreatWordM = 20000.0;

// Frights at one place within an hour of each other are one scare; calming words there, or the
// game's word of how it ended, reach it within a day of its last fright.
constexpr double kScareJoinMinutes = 60.0;
constexpr double kScareOpenDays = 1.0;

// Hearing of what the player did where they live, or to someone they are tied to, moves someone's
// regard this much, once an event.
constexpr int kHeardWell = 1;
constexpr int kHeardBadly = -1;

constexpr const char* kEventColumns =
    "id, kind, day, place, place_name, node, about_node, person, person_name, quest_id, verb, purpose, difficulty, was, "
    "revealed, player, state, state_day, actor";

Event event_from(const brains::Statement& row) {
    Event e;
    e.id = row.text(0);
    e.kind = row.text(1);
    e.day = row.real(2);
    e.place = row.text(3);
    e.place_name = row.text(4);
    e.node = row.maybe(5);
    e.about_node = row.maybe(6);
    e.person = row.text(7);
    e.person_name = row.text(8);
    e.quest_id = row.text(9);
    e.verb = row.text(10);
    e.purpose = row.text(11);
    e.difficulty = static_cast<int>(row.integer(12));
    e.was = row.text(13);
    e.revealed = row.text(14);
    e.player = row.integer(15) != 0;
    e.state = row.text(16);
    e.state_day = row.real(17);
    e.actor = row.text(18);
    return e;
}

// A board row's state, as its event says it. Taking work, and finishing, failing or giving it up,
// are only ever the player's; the world closes the rest.
std::string work_state(const fwg::QuestRow& row) {
    switch (row.state) {
        case fwg::QuestState::Offered: return "asked";
        case fwg::QuestState::Taken: return "taken";
        case fwg::QuestState::Completed: return "finished";
        case fwg::QuestState::Failed: return "failed";
        case fwg::QuestState::Abandoned: return "given_up";
        case fwg::QuestState::Expired: break;
    }
    if (!row.closed_reason) return "lapsed";
    switch (*row.closed_reason) {
        case fwg::QuestClosedReason::GiverGone: return "giver_gone";
        case fwg::QuestClosedReason::TargetGone: return "target_gone";
        case fwg::QuestClosedReason::HookGone: return "withdrawn";
        case fwg::QuestClosedReason::Abandoned: return "given_up";
        case fwg::QuestClosedReason::Expired:
        case fwg::QuestClosedReason::AgeTurned:
        case fwg::QuestClosedReason::Resolved: break;
    }
    return "lapsed";
}

bool players_work(std::string_view state) {
    return state == "taken" || state == "finished" || state == "failed" || state == "given_up";
}

// How far past where it starts word of an event's state goes, in metres.
double word_reach(const Event& e) {
    if (e.kind == "work")
        return e.state == "finished" || e.state == "failed" || e.state == "given_up"
                   ? kWorkWordM[static_cast<std::size_t>(std::clamp(e.difficulty, 1, 5) - 1)]
                   : 0.0;
    if (e.kind == "exposed") return kExposedWordM;
    if (e.kind == "scare") return e.state == "beaten" || e.state == "lost" ? kThreatWordM : 0.0;
    return 0.0;
}

// Word of someone's hidden face reaches those tied to them wherever they are.
bool ties_carry(const Event& e) { return e.kind == "exposed"; }

int regard_from_word(const Event& e) {
    if (e.kind == "work") {
        if (e.state == "finished") return kHeardWell;
        if (e.state == "failed" || e.state == "given_up") return kHeardBadly;
    }
    if (e.kind == "scare" && e.state == "beaten" && e.actor == "player") return kHeardWell;
    return 0;
}

// Every row of the board, in the board's order; empty before it opens.
std::vector<fwg::QuestRow> whole_board(const fwg::World& world) {
    try {
        return fwg::read_quests(world).quests;
    } catch (const fwg::WorldTimeError& refusal) {
        if (refusal.field() == "world.quests") return {};  // no board until the first tick
        if (refusal.code() != fwg::kRefusalStateCapacity) throw;
    }
    // More rows than one read answers: a read for each state.
    std::vector<fwg::QuestRow> rows;
    for (const fwg::QuestState state : {fwg::QuestState::Offered, fwg::QuestState::Taken, fwg::QuestState::Expired,
                                        fwg::QuestState::Completed, fwg::QuestState::Failed, fwg::QuestState::Abandoned}) {
        fwg::QuestsReadRequest request;
        request.states = std::vector<fwg::QuestState>{state};
        for (fwg::QuestRow& row : fwg::read_quests(world, request).quests) rows.push_back(std::move(row));
    }
    std::stable_sort(rows.begin(), rows.end(), [](const fwg::QuestRow& a, const fwg::QuestRow& b) { return a.quest_id < b.quest_id; });
    return rows;
}

// The place standing on `node`, by the id a packet names it by and as a speaker says it.
void place_on(const fwg::World& world, const std::optional<int>& node, Event& e) {
    if (!node) return;
    if (const std::optional<Fact> here = knowledge::place_at(world, *node)) {
        e.place = here->subject;
        e.place_name = knowledge::place_label(*here);
    }
}

// Where a place stands: a roster site's uid, or a place id a packet names.
std::optional<int> node_of_site(const fwg::World& world, const std::string& site) {
    for (const fwg::NpcSite& row : world.npcs.sites)
        if (row.uid == site) return row.node;
    fwg::PlaceReadRequest request;
    request.place_id = site;
    try {
        return fwg::read_place(world, request).place.anchor.node;
    } catch (const fwg::WorldTimeError&) {
        return std::nullopt;
    }
}

// A person's name as a stranger has it, or empty.
std::string name_of(const fwg::World& world, const std::string& uid) {
    if (uid.empty()) return {};
    const std::optional<knowledge::KnownPerson> person = knowledge::person_fact(world, uid);
    const std::string* name = person ? person->fact.field("name") : nullptr;
    return name ? *name : std::string();
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

Director::Director(fwg::World world) : world_(std::move(world)) { note_board(); }

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
    ++world_changes_;
    note_board();
    // Time passing is walking away: every conversation under way ends.
    for (auto& [uid, a] : acquainted_)
        if (a.talking) end_conversation(uid);
    ++acquaintance_changes_;
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
    ++world_changes_;
    note_board();
    // The giver remembers: work done for them, or let fail. Finishing what was asked under a
    // false purpose shows the player what it was for.
    if (const fwg::HeroQuestHook* hook = hook_of(world_, quest_id); hook && !hook->giver_uid.empty()) {
        Acquaintance& giver = acquainted_[hook->giver_uid];
        giver.work = completed ? "finished" : "failed";
        regard(giver, completed ? kWorkFinished : kWorkFailed);
        if (completed) ++giver.works_finished;
        if (completed && hook->unwitting) giver.finished_covert = true;
    }
    ++acquaintance_changes_;
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
    // A hero, a dread or a villain: whatever came before, their standing is a hero's.
    const fwg::NpcPerson* roster = std::get_if<fwg::NpcPerson>(&s.document.person);
    const bool cast = roster == nullptr || roster->is_cast;
    if (!s.present) {
        FactPacket p = packet_for(s, act, Outcome::Refused);
        p.facts.push_back(reason("gone", (s.persona.name.empty() ? std::string("the one spoken to") : s.persona.name) + " is gone"));
        // Nobody heard it, so nothing is counted, and nobody's log keeps it.
        const auto heard = a.heard.find(act.kind);
        p.facts.push_back(memory_fact(speaker_uid, a, true, false, heard == a.heard.end() ? 0 : heard->second,
                                      fear_now(speaker_uid, resting, today), resting,
                                      a.eased_day >= 0.0 && today - a.eased_day < kEasedDays, joined, standing_of(a, cast)));
        ++acquaintance_changes_;
        if (!joined) inputs_.push_back(Say{speaker_uid, act});
        last_ = Exchange{speaker_uid, act, p.outcome, true, {}};
        last_logged_ = -1;
        return p;
    }

    // The conversation: turning to this speaker ends the last one, and a speaker not already in
    // one begins another.
    if (!talking_to_.empty() && talking_to_ != speaker_uid) end_conversation(talking_to_);
    if (!a.talking) {
        // A calendar day met on: the first conversation's, and any later one's on another day.
        if (a.conversations == 0 || std::floor(today) != std::floor(a.last_day)) ++a.days_met;
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
        // What they remember of the stranger from before that bears on this (contract version 12):
        // a face that does not recognise the player remembers nothing of them.
        if (recognised)
            for (Fact& remembered : recall(speaker_uid, act, today)) p.facts.push_back(std::move(remembered));
        p.facts.push_back(memory_fact(speaker_uid, a, recognised, repeat, heard, fear_now(speaker_uid, resting, today), resting,
                                      a.eased_day >= 0.0 && today - a.eased_day < kEasedDays, joined, standing_of(a, cast)));
        a.last_day = today;
        // Into their log: the exchange as it went, the words to follow from the game.
        last_logged_ = log_exchange(speaker_uid, a, act, p, today, joined);
        if (act.kind == ActKind::Farewell) end_conversation(speaker_uid);
        ++acquaintance_changes_;
        if (!joined) inputs_.push_back(Say{speaker_uid, act});
        // What a bystander overhears: an answer that held nothing, a place or person unknown or no
        // work, is one they may answer instead; and the news it carried, which they hear too.
        const bool no_work = act.kind == ActKind::AskForWork &&
                             std::none_of(p.facts.begin(), p.facts.end(), [](const Fact& f) { return f.kind == FactKind::QuestOffer; });
        last_ = Exchange{speaker_uid, act, p.outcome, p.outcome == Outcome::Unknown || no_work, {}};
        for (const Fact& f : p.facts)
            if (const std::string* state = f.field("state"); f.kind == FactKind::News && state) last_->news.emplace_back(f.subject, *state);
        return p;
    };

    switch (act.kind) {
        case ActKind::NoAct:
            return finish(packet_for(s, act, Outcome::NotUnderstood));

        case ActKind::Farewell:
            return finish(packet_for(s, act, Outcome::Parted));

        case ActKind::Greet: {
            FactPacket p = packet_for(s, act, Outcome::Greeted);
            // What they have heard the player did, if anything: a face that does not know the
            // player has nothing to say of them.
            if (recognised)
                for (Fact& told : news(speaker_uid, "player", std::nullopt, 1)) p.facts.push_back(std::move(told));
            return finish(std::move(p));
        }

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
            // A scare where they stand, not a day old, is one the player talked people down from.
            if (s.node) {
                std::string scare;
                {
                    brains::Statement open(brains_.handle(),
                                           "SELECT id FROM events WHERE kind = 'scare' AND node = ?1 AND state = 'frightened' AND "
                                           "state_day >= ?2 ORDER BY seq DESC LIMIT 1");
                    open.integer(1, *s.node).real(2, today - kScareOpenDays);
                    if (open.step()) scare = open.text(0);
                }
                if (!scare.empty()) {
                    brains::Transaction transaction(brains_.handle());
                    move_event(scare, "calmed", today, "player");
                    learn(speaker_uid, scare, "calmed", today, "there");
                    transaction.commit();
                }
            }
            return finish(packet_for(s, act, Outcome::Calmed));
        }

        case ActKind::SmallTalk: {
            FactPacket p = packet_for(s, act, Outcome::Chatted);
            p.facts.push_back(chat_fact(world_, s, today));
            // The worst nest near, the nearest of the worst: what people talk about. The list
            // comes worst first.
            const std::vector<Fact> dangers = knowledge::dangers_near(world_, s);
            if (!dangers.empty()) p.facts.push_back(dangers.front());
            // And the latest news of where they stand, if they have heard any.
            if (s.node)
                for (Fact& told : news(speaker_uid, "", s.node, 1)) p.facts.push_back(std::move(told));
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
            // Word goes out that the player brought a hidden face out: once, the first time.
            bool first = true;
            {
                brains::Statement before(brains_.handle(), "SELECT 1 FROM events WHERE kind = 'exposed' AND person = ?1");
                first = !before.text(1, speaker_uid).step();
            }
            if (first) {
                Event e;
                e.kind = "exposed";
                e.day = today;
                e.node = s.node;
                e.about_node = s.node;
                place_on(world_, s.node, e);
                e.person = speaker_uid;
                e.person_name = known ? known->name : std::string();
                e.was = known ? known->apparent_role : std::string();
                e.revealed = hidden ? hidden->apparent_role : std::string();
                e.player = true;
                e.state = "exposed";
                e.state_day = today;
                e.actor = "player";
                brains::Transaction transaction(brains_.handle());
                const std::string id = make_event(e);
                learn(speaker_uid, id, "exposed", today, "there");
                transaction.commit();
            }
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
                for (Fact& told : news(speaker_uid, "", place.node, 2)) p.facts.push_back(std::move(told));
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
            for (Fact& told : news(speaker_uid, act.target, std::nullopt, 1)) p.facts.push_back(std::move(told));
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
            ++world_changes_;
            note_board();
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
            ++world_changes_;
            note_board();
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
    // A scare where they stand: the one under way there, frightening people within the hour, or
    // a new one. They know it, having been there.
    if (s->node) {
        brains::Transaction transaction(brains_.handle());
        std::string scare;
        {
            brains::Statement open(brains_.handle(),
                                   "SELECT id FROM events WHERE kind = 'scare' AND node = ?1 AND state = 'frightened' AND "
                                   "state_day >= ?2 ORDER BY seq DESC LIMIT 1");
            open.integer(1, *s->node).real(2, now - kScareJoinMinutes / kMinutesPerDay);
            if (open.step()) scare = open.text(0);
        }
        if (scare.empty()) {
            Event e;
            e.kind = "scare";
            e.day = now;
            e.node = s->node;
            e.about_node = s->node;
            place_on(world_, s->node, e);
            e.state = "frightened";
            e.state_day = now;
            scare = make_event(e);
        } else {
            brains::Statement later(brains_.handle(), "UPDATE events SET state_day = ?2 WHERE id = ?1");
            later.text(1, scare).real(2, now).run();
        }
        learn(person, scare, "frightened", now, "there");
        transaction.commit();
    }
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
    const std::int64_t of = last_logged_;
    const double today = fwg::world_clock_of(world_).day;
    // Everyone named first, so a uid the world does not name changes nothing.
    std::vector<knowledge::Speaker> heard_by;
    for (const std::string& uid : people) {
        std::optional<knowledge::Speaker> s = knowledge::speaker(world_, uid);
        if (!s) throw Error("overhear: the world names no person '" + uid + "'");
        heard_by.push_back(std::move(*s));
    }
    std::vector<Bystander> out;
    bool joined = false;
    for (knowledge::Speaker& bystander : heard_by) {
        const std::string uid = bystander.uid;
        knowledge::Speaker* s = &bystander;
        // Whoever heard it keeps it in their log, and hears the news it carried: a later state of an
        // event than the one they know it by.
        if (uid != heard.speaker && s->present && of > 0) {
            Brains::Handle& h = brains_.handle();
            brains::Transaction transaction(h);
            const std::int64_t id = ++exchanges_made_;
            {
                brains::Statement copy(h,
                                       "INSERT INTO exchanges (id, person, conversation, turn, day, act, target, about, outcome, heard_from, "
                                       "of_exchange, joined, line, reply) SELECT ?1, ?2, 0, 0, day, act, target, about, outcome, person, id, "
                                       "0, line, reply FROM exchanges WHERE id = ?3");
                copy.integer(1, id).text(2, uid).integer(3, of).run();
            }
            {
                brains::Statement copy(h,
                                       "INSERT INTO exchange_facts (exchange, seq, kind, subject, about) SELECT ?1, seq, kind, subject, about "
                                       "FROM exchange_facts WHERE exchange = ?2");
                copy.integer(1, id).integer(2, of).run();
            }
            for (const auto& [event, state] : heard.news) {
                std::optional<std::int64_t> newer, known;
                {
                    brains::Statement seqs(h,
                                           "SELECT (SELECT MAX(seq) FROM event_states WHERE event = ?1 AND state = ?2), (SELECT MAX(s.seq) "
                                           "FROM facts AS f JOIN event_states AS s ON s.event = f.event AND s.state = f.state WHERE "
                                           "f.person = ?3 AND f.event = ?1)");
                    seqs.text(1, event).text(2, state).text(3, uid);
                    if (seqs.step()) {
                        if (!seqs.null(0)) newer = seqs.integer(0);
                        if (!seqs.null(1)) known = seqs.integer(1);
                    }
                }
                if (!newer || (known && *known >= *newer)) continue;
                std::optional<Event> e;
                {
                    brains::Statement one(h, std::string("SELECT ") + kEventColumns + " FROM events WHERE id = ?1");
                    one.text(1, event);
                    if (one.step()) e = event_from(one);
                }
                if (!e) continue;
                {
                    brains::Statement actor(h, "SELECT actor FROM event_states WHERE event = ?1 AND seq = ?2");
                    actor.text(1, event).integer(2, *newer);
                    e->actor = actor.step() ? actor.text(0) : std::string();
                }
                e->state = state;
                hear(uid, *e, today, "overheard");
            }
            transaction.commit();
        }
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
    ++acquaintance_changes_;
    inputs_.push_back(Overhear{people});
    return out;
}

// ---- events, and what people have heard ----------------------------------------------------------

std::string Director::make_event(const Event& e) {
    const std::string id = "event-" + std::to_string(++events_made_);
    brains::Statement insert(brains_.handle(),
                             "INSERT INTO events (id, seq, kind, day, place, place_name, node, about_node, person, person_name, "
                             "quest_id, verb, purpose, difficulty, was, revealed, player, state, state_day, actor) VALUES (?1, ?2, "
                             "?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20)");
    insert.text(1, id).integer(2, events_made_).text(3, e.kind).real(4, e.day).text_or_null(5, e.place).text_or_null(6, e.place_name);
    insert.maybe(7, e.node).maybe(8, e.about_node).text_or_null(9, e.person).text_or_null(10, e.person_name);
    insert.text_or_null(11, e.quest_id).text_or_null(12, e.verb).text_or_null(13, e.purpose);
    if (e.difficulty > 0) insert.integer(14, e.difficulty);
    insert.text_or_null(15, e.was).text_or_null(16, e.revealed).integer(17, e.player ? 1 : 0).text(18, e.state);
    insert.real(19, e.state_day).text_or_null(20, e.actor);
    insert.run();
    brains::Statement first(brains_.handle(), "INSERT INTO event_states (event, seq, state, day, actor) VALUES (?1, 1, ?2, ?3, ?4)");
    first.text(1, id).text(2, e.state).real(3, e.state_day).text_or_null(4, e.actor).run();
    return id;
}

void Director::move_event(const std::string& id, const std::string& state, double day, const std::string& actor) {
    brains::Statement update(brains_.handle(),
                             "UPDATE events SET state = ?2, state_day = ?3, actor = ?4, player = MAX(player, ?5) WHERE id = ?1");
    update.text(1, id).text(2, state).real(3, day).text_or_null(4, actor).integer(5, actor == "player" ? 1 : 0).run();
    brains::Statement next(brains_.handle(),
                           "INSERT INTO event_states (event, seq, state, day, actor) SELECT ?1, COUNT(*) + 1, ?2, ?3, ?4 FROM "
                           "event_states WHERE event = ?1");
    next.text(1, id).text(2, state).real(3, day).text_or_null(4, actor).run();
}

void Director::learn(const std::string& person, const std::string& event, const std::string& state, double day,
                     const std::string& how) {
    brains::Statement upsert(brains_.handle(),
                             "INSERT INTO facts (person, event, state, day, how) VALUES (?1, ?2, ?3, ?4, ?5) ON CONFLICT (person, "
                             "event) DO UPDATE SET state = excluded.state, day = excluded.day, how = excluded.how");
    upsert.text(1, person).text(2, event).text(3, state).real(4, day).text(5, how).run();
}

void Director::hear(const std::string& person, const Event& e, double day, const std::string& how) {
    learn(person, e.id, e.state, day, how);
    const int moves = regard_from_word(e);
    if (moves == 0) return;
    // Only for what happened where they live, or to someone they are tied to.
    const Population& everyone = population();
    const auto who = everyone.people.find(person);
    if (who == everyone.people.end()) return;
    const auto ties = everyone.ties.find(person);
    const bool close = (who->second.node && (who->second.node == e.node || who->second.node == e.about_node)) ||
                       (!e.person.empty() && ties != everyone.ties.end() && ties->second.count(e.person));
    if (close) regard(acquainted_[person], moves);
}

void Director::note_board() {
    const std::vector<fwg::QuestRow> rows = whole_board(world_);
    if (rows.empty()) return;
    const double today = fwg::world_clock_of(world_).day;
    brains::Transaction transaction(brains_.handle());
    for (const fwg::QuestRow& row : rows) {
        const std::string now = work_state(row);
        std::string id, was;
        {
            brains::Statement known(brains_.handle(), "SELECT id, state FROM events WHERE quest_id = ?1");
            known.text(1, row.quest_id);
            if (known.step()) {
                id = known.text(0);
                was = known.text(1);
            }
        }
        const std::string giver = row.giver_uid.value_or("");
        if (id.empty()) {
            // Offered: the giver knows their own work, and word of it starts where they stand.
            Event e;
            e.kind = "work";
            e.day = row.offered_day;
            e.quest_id = row.quest_id;
            e.person = giver;
            if (!giver.empty())
                if (const std::optional<knowledge::KnownPerson> who = knowledge::person_fact(world_, giver)) {
                    e.node = who->node;
                    if (const std::string* name = who->fact.field("name")) e.person_name = *name;
                }
            e.verb = row.verb.value_or("");
            e.purpose = row.stated_purpose.value_or("");
            if (const fwg::HeroQuestHook* hook = hook_of(world_, row.quest_id)) {
                e.difficulty = hook->difficulty;
                if (hook->has_target_node) e.about_node = hook->target_node;
            }
            if (row.anchor) {
                e.place = *row.anchor;
                if (const std::optional<std::string> name = knowledge::anchor_name(world_, *row.anchor)) e.place_name = *name;
            }
            e.state = "asked";
            e.state_day = row.offered_day;
            e.actor = giver;
            id = make_event(e);
            was = "asked";
            if (!giver.empty()) learn(giver, id, "asked", today, "there");
        }
        if (was == now) continue;
        const double day = row.closed_day.value_or(today);
        move_event(id, now, day, players_work(now) ? "player" : "");
        if (!giver.empty() && now != "giver_gone") learn(giver, id, now, today, "there");
    }
    transaction.commit();
}

std::vector<Event> Director::events() const {
    std::vector<Event> out;
    brains::Statement all(brains_.handle(), std::string("SELECT ") + kEventColumns + " FROM events ORDER BY seq");
    while (all.step()) out.push_back(event_from(all));
    return out;
}

const Director::Population& Director::population() const {
    if (population_ && population_of_ == world_changes_) return *population_;
    Population out;
    std::map<std::string, int> site_nodes;
    for (const fwg::NpcSite& site : world_.npcs.sites)
        if (site.node) site_nodes.emplace(site.uid, *site.node);
    for (const fwg::NpcPerson& p : world_.npcs.people) {
        if (p.is_cast) continue;  // a hero or a dread, whose own record says where they are
        Someone one;
        one.name = p.name.value_or("");
        one.kind = "roster";
        one.role = p.post;
        one.site = p.site_uid.value_or("");
        one.present = p.alive;
        if (p.presence_node) {
            one.node = *p.presence_node;
        } else if (p.site_uid) {
            if (const auto found = site_nodes.find(*p.site_uid); found != site_nodes.end()) one.node = found->second;
        }
        out.people.emplace(p.uid, std::move(one));
    }
    const auto add = [&](const std::string& uid, const char* kind) {
        if (uid.empty() || out.people.count(uid)) return;
        fwg::PersonReadRequest request;
        request.uid = uid;
        std::optional<fwg::PersonRead> read;
        try {
            read = fwg::read_person(world_, request);
        } catch (const fwg::WorldTimeError&) {
            return;  // a record in a block that failed: nobody the world names
        }
        Someone one;
        one.kind = kind;
        std::tie(one.name, one.role) = knowledge::known_as(read->person);
        one.node = read->person.anchor.node;
        one.present = read->person.liveness == fwg::Liveness::Present;
        out.people.emplace(uid, std::move(one));
    };
    for (const fwg::Hero& hero : world_.heroes.people) add(hero.uid, "hero");
    for (const fwg::HeroDread& dread : world_.heroes.dreads) add(dread.uid, "dread");
    for (const fwg::Villain& villain : world_.villains.people) add(villain.uid, "villain");
    for (const auto& [uid, one] : out.people)
        if (one.node) out.at_node[*one.node].push_back(uid);
    for (const fwg::Hero& hero : world_.heroes.people)
        for (const std::vector<std::string>* list : {&hero.rivals, &hero.kin, &hero.allies})
            for (const std::string& other : *list) {
                out.ties[hero.uid].insert(other);
                out.ties[other].insert(hero.uid);
            }
    population_ = std::move(out);
    population_of_ = world_changes_;
    return *population_;
}

std::vector<std::string> Director::reached(const Event& e, double today) const {
    const Population& everyone = population();
    const double reach = word_reach(e);
    // Word starts where it happened; the end of a piece of work is told where it was for, too.
    std::vector<int> starts;
    if (e.node) starts.push_back(*e.node);
    if (reach > 0.0 && e.about_node && e.about_node != e.node) starts.push_back(*e.about_node);
    std::set<std::string> heard;
    const auto at = [&](int node) {
        if (const auto found = everyone.at_node.find(node); found != everyone.at_node.end())
            heard.insert(found->second.begin(), found->second.end());
    };
    for (const int node : starts) at(node);
    if (ties_carry(e) && !e.person.empty())
        if (const auto found = everyone.ties.find(e.person); found != everyone.ties.end())
            heard.insert(found->second.begin(), found->second.end());
    if (reach > 0.0) {
        const double radius = std::min(reach, std::max(0.0, today - e.state_day) * kWordMetresPerDay);
        if (radius > 0.0)
            for (const int node : starts)
                for (const int near : knowledge::place_nodes_within(world_, node, radius)) at(near);
    }
    std::vector<std::string> out;
    for (const std::string& uid : heard)
        if (const auto found = everyone.people.find(uid); found != everyone.people.end() && found->second.present) out.push_back(uid);
    return out;
}

std::vector<Learned> Director::share(const std::vector<std::string>& people) {
    for (const std::string& uid : people)
        if (!names_a_person(world_, uid)) throw Error("share: the world names no person '" + uid + "'");
    const std::set<std::string> named(people.begin(), people.end());
    const double today = fwg::world_clock_of(world_).day;
    std::vector<Learned> out;
    brains::Transaction transaction(brains_.handle());
    for (const Event& e : events()) {
        const std::vector<std::string> reach = reached(e, today);
        if (reach.empty()) continue;
        std::map<std::string, std::string> known;  // person -> the state they know it by
        {
            brains::Statement read(brains_.handle(), "SELECT person, state FROM facts WHERE event = ?1");
            read.text(1, e.id);
            while (read.step()) known.emplace(read.text(0), read.text(1));
        }
        // A state that moves anyone's regard is its event's last, so nobody is moved twice by one.
        for (const std::string& uid : reach) {
            if (!named.empty() && !named.count(uid)) continue;
            const auto found = known.find(uid);
            if (found != known.end() && found->second == e.state) continue;
            hear(uid, e, today, "word");
            out.push_back({uid, e.id, e.state});
        }
    }
    transaction.commit();
    ++acquaintance_changes_;
    inputs_.push_back(Share{people});
    return out;
}

Related Director::related(const std::string& event) const {
    std::optional<Event> found;
    {
        brains::Statement one(brains_.handle(), std::string("SELECT ") + kEventColumns + " FROM events WHERE id = ?1");
        one.text(1, event);
        if (one.step()) found = event_from(one);
    }
    if (!found) throw Error("related: there is no event '" + event + "'");
    Related out;
    std::map<std::string, std::string> known;
    {
        brains::Statement read(brains_.handle(), "SELECT person, state FROM facts WHERE event = ?1 ORDER BY person");
        read.text(1, event);
        while (read.step()) {
            out.knows.push_back(read.text(0));
            known.emplace(read.text(0), read.text(1));
        }
    }
    for (const std::string& uid : reached(*found, fwg::world_clock_of(world_).day)) {
        const auto k = known.find(uid);
        if (k == known.end() || k->second != found->state) out.would_learn.push_back(uid);
    }
    return out;
}

void Director::report_event(const std::string& site, const std::string& outcome, const std::string& by) {
    if (outcome != "beaten" && outcome != "lost" && outcome != "passed")
        throw Error("report_event: an outcome is \"beaten\", \"lost\" or \"passed\", not '" + outcome + "'");
    const std::optional<int> node = node_of_site(world_, site);
    if (!node) throw Error("report_event: the world names no place '" + site + "'");
    if (!by.empty() && by != "player" && !names_a_person(world_, by))
        throw Error("report_event: the world names no person '" + by + "'");
    const double today = fwg::world_clock_of(world_).day;
    brains::Transaction transaction(brains_.handle());
    std::string scare;
    {
        brains::Statement open(brains_.handle(),
                               "SELECT id FROM events WHERE kind = 'scare' AND node = ?1 AND state IN ('frightened', 'calmed') "
                               "AND state_day >= ?2 ORDER BY seq DESC LIMIT 1");
        open.integer(1, *node).real(2, today - kScareOpenDays);
        if (open.step()) scare = open.text(0);
    }
    if (scare.empty()) {
        // A threat nobody reported frightening anyone: its end is the first the director hears of it.
        Event e;
        e.kind = "scare";
        e.day = today;
        e.node = node;
        e.about_node = node;
        place_on(world_, node, e);
        e.player = by == "player";
        e.state = outcome;
        e.state_day = today;
        e.actor = by;
        scare = make_event(e);
    } else {
        move_event(scare, outcome, today, by);
    }
    if (!by.empty() && by != "player") learn(by, scare, outcome, today, "there");
    transaction.commit();
    inputs_.push_back(Report{site, outcome, by});
}

std::vector<Fact> Director::news(const std::string& speaker, const std::string& about, std::optional<int> node,
                                 std::size_t most) const {
    std::string sql = std::string("SELECT ") +
                      "e.id, e.kind, e.day, e.place, e.place_name, e.node, e.about_node, e.person, e.person_name, e.quest_id, "
                      "e.verb, e.purpose, e.difficulty, e.was, e.revealed, e.player, f.state, s.day, s.actor, f.how "
                      "FROM facts AS f JOIN events AS e ON e.id = f.event JOIN event_states AS s ON s.event = f.event AND "
                      "s.state = f.state WHERE f.person = ?1 AND ";
    if (about == "player")
        sql += "s.actor = 'player'";
    else if (!about.empty())
        sql += "e.person = ?2";
    else
        sql += "(e.node = ?3 OR e.about_node = ?3)";
    sql += " ORDER BY s.day DESC, e.seq DESC LIMIT ?4";
    brains::Statement read(brains_.handle(), sql);
    read.text(1, speaker);
    if (!about.empty() && about != "player") read.text(2, about);
    if (about.empty()) read.maybe(3, node);
    read.integer(4, static_cast<std::int64_t>(most));
    const double today = fwg::world_clock_of(world_).day;
    std::vector<Fact> out;
    while (read.step()) {
        // The event as the speaker knows it: the state they heard, when it came about, who did it.
        const std::string kind = read.text(1);
        const std::string state = read.text(16);
        const std::string actor = read.text(18);
        Fact f{FactKind::News, read.text(0), {}};
        f.fields.emplace_back("event", kind);
        f.fields.emplace_back("state", state);
        if (const std::string place = read.text(4); !place.empty()) f.fields.emplace_back("place", place);
        if (kind == "work") {
            if (const std::string verb = read.text(10); !verb.empty()) f.fields.emplace_back("verb", verb);
            if (const std::string purpose = read.text(11); !purpose.empty()) f.fields.emplace_back("purpose", purpose);
            if (const std::string giver = read.text(8); !giver.empty()) f.fields.emplace_back("giver", giver);
        } else if (kind == "exposed" || kind == "gone") {
            if (const std::string person = read.text(8); !person.empty()) f.fields.emplace_back("person", person);
            if (kind == "exposed") {
                f.fields.emplace_back("was", read.text(13));
                f.fields.emplace_back("is", read.text(14));
            }
        }
        f.fields.emplace_back("player", actor == "player" ? "yes" : "no");
        if (kind == "scare" && (state == "beaten" || state == "lost" || state == "passed") && !actor.empty()) {
            const std::string by = actor == "player" ? actor : name_of(world_, actor);
            if (!by.empty()) f.fields.emplace_back("by", by);
        }
        f.fields.emplace_back("since", since_band(today, read.real(17)));
        f.fields.emplace_back("how", read.text(19));
        out.push_back(std::move(f));
    }
    return out;
}

// ---- short and long memory ----------------------------------------------------------------------

std::int64_t Director::log_exchange(const std::string& person, const Acquaintance& a, const DialogueAct& act, const FactPacket& packet,
                                    double day, bool joined) {
    Brains::Handle& h = brains_.handle();
    const std::int64_t id = ++exchanges_made_;
    const std::string about = about_of(act, packet);
    brains::Transaction transaction(h);
    {
        brains::Statement insert(h,
                                 "INSERT INTO exchanges (id, person, conversation, turn, day, act, target, about, outcome, heard_from, "
                                 "of_exchange, joined, line, reply) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, NULL, NULL, ?10, NULL, NULL)");
        insert.integer(1, id).text(2, person).integer(3, a.conversations).integer(4, a.turn).real(5, day);
        insert.text(6, to_string(act.kind)).text(7, act.target).text(8, about).text(9, to_string(packet.outcome));
        insert.integer(10, joined ? 1 : 0).run();
    }
    // What the reply named: what the speaker told the stranger of.
    std::int64_t seq = 0;
    for (const Fact& fact : packet.facts) {
        if (!names_something(fact)) continue;
        brains::Statement insert(h, "INSERT INTO exchange_facts (exchange, seq, kind, subject, about) VALUES (?1, ?2, ?3, ?4, ?5)");
        insert.integer(1, id).integer(2, ++seq).text(3, to_string(fact.kind)).text(4, fact.subject).text(5, called(fact)).run();
    }
    transaction.commit();
    return id;
}

std::string Director::about_of(const DialogueAct& act, const FactPacket& packet) const {
    if (act.target.empty()) return {};
    for (const Fact& fact : packet.facts)
        if (fact.subject == act.target && names_something(fact))
            if (std::string name = called(fact); !name.empty()) return name;
    // The reply named nothing of it -- a place or person the speaker does not know, work taken or
    // turned down -- and the stranger named it all the same.
    switch (act.kind) {
        case ActKind::AskAboutPlace: return knowledge::anchor_name(world_, act.target).value_or("");
        case ActKind::AskAboutPerson: return name_of(world_, act.target);
        case ActKind::AcceptQuest:
        case ActKind::DeclineQuest:
        case ActKind::AbandonQuest: {
            brains::Statement work(brains_.handle(), "SELECT purpose FROM events WHERE quest_id = ?1");
            work.text(1, act.target);
            return work.step() ? work.text(0) : std::string();
        }
        default: return {};
    }
}

std::vector<Director::Logged> Director::log_of(const std::string& person) const {
    Brains::Handle& h = brains_.handle();
    std::vector<Logged> out;
    {
        brains::Statement rows(h,
                               "SELECT id, conversation, day, act, target, about, outcome, heard_from, line, reply FROM exchanges WHERE "
                               "person = ?1 ORDER BY id");
        rows.text(1, person);
        while (rows.step()) {
            Logged x;
            x.id = rows.integer(0);
            x.conversation = static_cast<int>(rows.integer(1));
            x.day = rows.real(2);
            x.act = rows.text(3);
            x.target = rows.text(4);
            x.about = rows.text(5);
            x.outcome = rows.text(6);
            x.heard_from = rows.text(7);
            x.line = rows.text(8);
            x.reply = rows.text(9);
            out.push_back(std::move(x));
        }
    }
    if (out.empty()) return out;
    brains::Statement named_rows(h,
                                 "SELECT f.exchange, f.kind, f.subject, f.about FROM exchange_facts AS f JOIN exchanges AS x ON x.id = "
                                 "f.exchange WHERE x.person = ?1 ORDER BY f.exchange, f.seq");
    named_rows.text(1, person);
    std::size_t at = 0;
    while (named_rows.step()) {
        const std::int64_t exchange = named_rows.integer(0);
        while (at < out.size() && out[at].id != exchange) ++at;
        if (at == out.size()) break;  // both come in the order of their rows, so this cannot be
        out[at].named.push_back({named_rows.text(1), named_rows.text(2), named_rows.text(3)});
    }
    return out;
}

std::vector<Remembered> Director::memories(const std::string& person) const {
    std::vector<Remembered> out;
    brains::Statement stored(brains_.handle(),
                             "SELECT memory, subject, act, about, times, first_day, last_day, words, source, seq FROM memories WHERE "
                             "person = ?1 ORDER BY seq");
    stored.text(1, person);
    while (stored.step()) {
        Remembered r;
        r.person = person;
        r.memory = stored.text(0);
        r.subject = stored.text(1);
        r.act = stored.text(2);
        r.about = stored.text(3);
        r.times = static_cast<int>(stored.integer(4));
        r.first_day = stored.real(5);
        r.last_day = stored.real(6);
        r.words = stored.text(7);
        r.source = stored.text(8);
        r.seq = stored.integer(9);
        out.push_back(std::move(r));
    }
    return out;
}

std::vector<Remembered> Director::remembered(const std::string& person, const std::vector<Logged>& log, bool quotes,
                                             int last_before) const {
    std::vector<Remembered> all = memories(person);
    const auto one = [&](std::string memory, std::string subject, std::string act, std::string about, double day, std::string words) {
        Remembered r;
        r.person = person;
        r.memory = std::move(memory);
        r.subject = std::move(subject);
        r.act = std::move(act);
        r.about = std::move(about);
        r.times = 1;
        r.first_day = day;
        r.last_day = day;
        r.words = std::move(words);
        r.source = "rules";
        return r;
    };
    // What they last spoke of: the latest thing named, else the latest that was more than a
    // greeting, a farewell or a line not understood.
    const Logged* last_named = nullptr;
    const Logged* last_spoken = nullptr;
    std::vector<const Logged*> answers;
    for (const Logged& x : log) {
        if (!x.heard_from.empty()) {
            // The stranger talking with someone else.
            fold_in(all, one("overheard", x.heard_from, x.act, name_of(world_, x.heard_from), x.day, ""));
            continue;
        }
        if (x.act.rfind("ask_", 0) == 0) fold_in(all, one("asked", x.target, x.act, x.about, x.day, ""));
        for (const Named& told : x.named) fold_in(all, one("told", told.subject, told.kind, told.about, x.day, ""));
        if (const std::optional<Outcome> outcome = outcome_named(x.outcome)) {
            const std::string deed = deed_of(*outcome);
            const bool work = *outcome == Outcome::Accepted || *outcome == Outcome::Abandoned;
            if (!deed.empty()) fold_in(all, one("did", work ? x.target : std::string(), deed, work ? x.about : std::string(), x.day, ""));
        }
        // The stranger's answer to a question of theirs is what they asked to know.
        if (quotes && x.act == "answer" && !trimmed(x.line).empty() && trimmed(x.line).size() <= kMaxQuoteBytes) answers.push_back(&x);
        if (last_before != 0 && x.conversation >= last_before) continue;
        if (!x.about.empty()) last_named = &x;
        if (x.act != "greet" && x.act != "farewell" && x.act != "no_act") last_spoken = &x;
    }
    for (std::size_t i = answers.size() > kMostQuotes ? answers.size() - kMostQuotes : 0; i < answers.size(); ++i)
        fold_in(all, one("said", "", "said", "", answers[i]->day, trimmed(answers[i]->line)));
    if (const Logged* last = last_named ? last_named : last_spoken) {
        all.erase(std::remove_if(all.begin(), all.end(), [](const Remembered& r) { return r.memory == "last"; }), all.end());
        all.push_back(one("last", last->target, last->act, last->about, last->day, ""));
    }

    // Work, as the one who gave it knows it now: taken, and since finished, is remembered finished.
    Brains::Handle& h = brains_.handle();
    for (Remembered& r : all) {
        if (r.memory != "did" || r.subject.empty()) continue;
        brains::Statement state(h,
                                "SELECT f.state, s.day FROM facts AS f JOIN events AS e ON e.id = f.event JOIN event_states AS s ON "
                                "s.event = f.event AND s.state = f.state WHERE f.person = ?1 AND e.quest_id = ?2 ORDER BY s.seq DESC "
                                "LIMIT 1");
        state.text(1, person).text(2, r.subject);
        if (!state.step()) continue;
        const std::string now = state.text(0);
        if (!work_ended(now) || now == r.act) continue;
        r.act = now;
        r.last_day = std::max(r.last_day, state.real(1));
    }
    // One memory a piece of work: the first kept, with the last of what became of it.
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i].memory != "did" || all[i].subject.empty()) continue;
        for (std::size_t j = i + 1; j < all.size();) {
            if (all[j].memory != "did" || all[j].subject != all[i].subject) {
                ++j;
                continue;
            }
            Remembered& kept = all[i];
            const Remembered& later = all[j];
            if (work_ended(later.act) != work_ended(kept.act) ? work_ended(later.act) : later.last_day > kept.last_day) kept.act = later.act;
            kept.times = std::max(kept.times, later.times);
            kept.first_day = std::min(kept.first_day, later.first_day);
            kept.last_day = std::max(kept.last_day, later.last_day);
            if (kept.about.empty()) kept.about = later.about;
            all.erase(all.begin() + static_cast<std::ptrdiff_t>(j));
        }
    }

    // The work of theirs the stranger holds and has not done: the director's own state, never the log's.
    std::vector<Remembered> open;
    {
        brains::Statement held(h,
                               "SELECT quest_id, purpose, state_day FROM events WHERE kind = 'work' AND person = ?1 AND state = 'taken' "
                               "ORDER BY seq");
        held.text(1, person);
        while (held.step()) open.push_back(one("open", held.text(0), "taken", held.text(1), held.real(2), ""));
    }
    const auto holds = [&](const std::string& quest) {
        return std::any_of(open.begin(), open.end(), [&](const Remembered& o) { return o.subject == quest; });
    };
    all.erase(std::remove_if(all.begin(), all.end(), [&](const Remembered& r) { return r.memory == "open" && !holds(r.subject); }),
              all.end());
    for (Remembered& o : open)
        if (std::none_of(all.begin(), all.end(), [&](const Remembered& r) { return r.memory == "open" && r.subject == o.subject; }))
            all.push_back(std::move(o));
    return all;
}

std::vector<Fact> Director::recall(const std::string& speaker, const DialogueAct& act, double today) const {
    const bool greeting = act.kind == ActKind::Greet;
    const bool asked = act.kind == ActKind::AskAboutPlace || act.kind == ActKind::AskAboutPerson;
    const bool work = act.kind == ActKind::AskForWork;
    if (!greeting && !asked && !work) return {};
    // What they last spoke of is from before this conversation.
    const auto met = acquainted_.find(speaker);
    const std::vector<Remembered> all =
        remembered(speaker, log_of(speaker), true, met == acquainted_.end() ? 0 : std::max(1, met->second.conversations));
    // The latest `most` of those `take` passes, latest first.
    const auto latest = [&](const auto& take, std::size_t most) {
        std::vector<const Remembered*> found;
        for (auto r = all.rbegin(); r != all.rend(); ++r)
            if (take(*r)) found.push_back(&*r);
        std::stable_sort(found.begin(), found.end(), [](const Remembered* x, const Remembered* y) { return x->last_day > y->last_day; });
        if (found.size() > most) found.resize(most);
        return found;
    };
    std::vector<Fact> out;
    const auto say = [&](const std::vector<const Remembered*>& found) {
        for (const Remembered* r : found)
            out.push_back(Fact{FactKind::Recall,
                               r->subject.empty() ? speaker : r->subject,
                               {{"memory", r->memory},
                                {"act", r->act},
                                {"about", r->about},
                                {"times", std::to_string(r->times)},
                                {"since", since_band(today, r->last_day)},
                                {"first", since_band(today, r->first_day)},
                                {"words", r->words}}});
    };
    if (greeting) {
        say(latest([](const Remembered& r) { return r.memory == "last"; }, 1));
        say(latest([](const Remembered& r) { return r.memory == "open"; }, 1));
        say(latest([](const Remembered& r) { return r.memory == "said"; }, 1));
    } else if (asked) {
        say(latest([&](const Remembered& r) { return r.memory == "asked" && r.subject == act.target; }, 1));
        say(latest([&](const Remembered& r) { return r.memory == "told" && r.subject == act.target; }, 1));
    } else {
        say(latest([](const Remembered& r) { return r.memory == "did" && !r.subject.empty(); }, 2));
        say(latest([](const Remembered& r) { return r.memory == "open"; }, 2));
    }
    return out;
}

void Director::words(const std::string& line, const std::string& reply) {
    if (!last_) throw Error("words: nothing has been said yet");
    if (line.size() > kMaxWordsBytes || reply.size() > kMaxWordsBytes)
        throw Error("words: a line or a reply is at most " + std::to_string(kMaxWordsBytes) + " bytes");
    if (!is_utf8(line) || !is_utf8(reply)) throw Error("words: the line and the reply must be UTF-8");
    if (last_logged_ > 0) {
        brains::Statement update(brains_.handle(), "UPDATE exchanges SET line = ?2, reply = ?3 WHERE id = ?1 OR of_exchange = ?1");
        update.integer(1, last_logged_).text(2, line).text(3, reply).run();
    }
    inputs_.push_back(Words{line, reply});
}

namespace {

// Whether `quote` stands in `line` as whole words: nothing of a word cut at either end.
bool quoted_whole(const std::string& line, const std::string& quote) {
    const auto inside = [](char c) { return ascii_letter(c) || (c >= '0' && c <= '9') || static_cast<unsigned char>(c) >= 0x80; };
    for (std::size_t at = line.find(quote); at != std::string::npos; at = line.find(quote, at + 1)) {
        const std::size_t end = at + quote.size();
        const bool starts = at == 0 || !inside(line[at - 1]) || !inside(quote.front());
        const bool ends = end == line.size() || !inside(line[end]) || !inside(quote.back());
        if (starts && ends) return true;
    }
    return false;
}

}  // namespace

Compacted Director::compact(const std::string& person, const std::string& outcomes) {
    if (!names_a_person(world_, person)) throw Error("compact: the world names no person '" + person + "'");
    if (outcomes.size() > kMaxOutcomesBytes)
        throw Error("compact: the outcomes are longer than " + std::to_string(kMaxOutcomesBytes) + " bytes");
    // The outcomes handed in, read before anything changes.
    struct Quote {
        std::string quote, about;
    };
    std::optional<std::vector<Quote>> quoted;
    std::optional<std::string> gist;
    if (!outcomes.empty()) {
        const fwg::JsonValue root = [&] {
            try {
                return fwg::parse_json(outcomes);
            } catch (const fwg::JsonError& e) {
                throw Error(std::string("compact: the outcomes are not JSON: ") + e.what());
            }
        }();
        const auto refuse = [](const std::string& why) {
            throw Error("compact: the outcomes are not {\"said\": [{\"quote\": \"...\", \"about\": \"...\"}], \"last\": \"...\"}: " + why);
        };
        if (!root.is_object()) refuse("not an object");
        for (const auto& [key, value] : root.as_object()) {
            if (key == "said") {
                if (!value.is_array()) refuse("\"said\" is not a list");
                quoted.emplace();
                for (const fwg::JsonValue& item : value.as_array()) {
                    if (!item.is_object()) refuse("an item of \"said\" is not an object");
                    Quote q;
                    for (const auto& [field, text] : item.as_object()) {
                        if (!text.is_string()) refuse("\"" + field + "\" is not text");
                        if (field == "quote")
                            q.quote = text.as_string();
                        else if (field == "about")
                            q.about = text.as_string();
                        else
                            refuse("an item of \"said\" has a key it should not: '" + field + "'");
                    }
                    quoted->push_back(std::move(q));
                }
            } else if (key == "last") {
                if (!value.is_string()) refuse("\"last\" is not text");
                gist = value.as_string();
            } else {
                refuse("a key it should not have: '" + key + "'");
            }
        }
    }

    // A conversation under way ends, as walking away ends one.
    end_conversation(person);
    const std::vector<Logged> log = log_of(person);
    const std::vector<Remembered> before = memories(person);
    std::vector<Remembered> after = remembered(person, log, !quoted.has_value(), 0);
    Compacted out;
    // What the outcomes may name: the words the log holds.
    std::set<std::string> held;
    for (const Logged& x : log) {
        for (const std::string* text : {&x.line, &x.reply, &x.about})
            for (const auto& word : words_of(*text)) held.insert(word.first);
        for (const Named& told : x.named)
            for (const auto& word : words_of(told.about)) held.insert(word.first);
    }
    const auto unheard = [&](const std::string& text) -> std::string {
        for (const std::string& name : names_used(text))
            if (!held.count(name)) return name;
        return {};
    };
    if (quoted) {
        std::size_t kept = 0;
        for (const Quote& q : *quoted) {
            const std::string quote = trimmed(q.quote);
            const std::string about = trimmed(q.about);
            std::string why;
            const Logged* from = nullptr;
            if (!is_utf8(quote) || !is_utf8(about)) {
                why = "a quote that is not UTF-8";
            } else if (quote.empty()) {
                why = "a quote with no words";
            } else if (quote.size() > kMaxQuoteBytes) {
                why = "a quote longer than " + std::to_string(kMaxQuoteBytes) + " bytes";
            } else {
                for (const Logged& x : log)
                    if (quoted_whole(x.line, quote)) {
                        from = &x;
                        break;
                    }
                if (from == nullptr) {
                    why = "\"" + quote + "\" is not the stranger's words";
                } else if (about.size() > kMaxQuoteBytes) {
                    why = "what \"" + quote + "\" is about is longer than " + std::to_string(kMaxQuoteBytes) + " bytes";
                } else if (const std::string name = unheard(about); !name.empty()) {
                    why = "what \"" + quote + "\" is about names " + name + ", which the log does not";
                } else if (kept == kMostQuotes) {
                    why = "\"" + quote + "\" is past the " + std::to_string(kMostQuotes) + " quotes kept";
                }
            }
            if (!why.empty()) {
                out.refused.push_back(why);
                continue;
            }
            ++kept;
            Remembered r;
            r.person = person;
            r.memory = "said";
            r.act = "said";
            r.about = about;
            r.times = 1;
            r.first_day = from->day;
            r.last_day = from->day;
            r.words = quote;
            r.source = "given";
            fold_in(after, std::move(r));
        }
    }
    if (gist) {
        const std::string text = trimmed(*gist);
        const auto made = std::find_if(after.begin(), after.end(), [](const Remembered& r) { return r.memory == "last" && r.seq == 0; });
        std::string why;
        if (!is_utf8(text))
            why = "a last that is not UTF-8";
        else if (text.empty())
            why = "a last with no words";
        else if (text.size() > kMaxLastBytes)
            why = "a last longer than " + std::to_string(kMaxLastBytes) + " bytes";
        else if (made == after.end())
            why = "a last, where the log holds nothing they spoke of";
        else if (const std::string name = unheard(text); !name.empty())
            why = "a last that names " + name + ", which the log does not";
        if (why.empty()) {
            made->words = text;
            made->source = "given";
        } else {
            out.refused.push_back(why);
        }
    }

    // Long memory written afresh, what was there before keeping its place; the log cleared.
    Brains::Handle& h = brains_.handle();
    brains::Transaction transaction(h);
    brains::Statement(h, "DELETE FROM memories WHERE person = ?1").text(1, person).run();
    for (Remembered& r : after) {
        const bool same = std::any_of(before.begin(), before.end(), [&](const Remembered& b) {
            return b.seq == r.seq && b.memory == r.memory && b.subject == r.subject && b.act == r.act && b.about == r.about &&
                   b.times == r.times && b.first_day == r.first_day && b.last_day == r.last_day && b.words == r.words &&
                   b.source == r.source;
        });
        if (r.seq == 0) r.seq = ++memories_made_;
        brains::Statement insert(h,
                                 "INSERT INTO memories (person, memory, subject, act, words, about, times, first_day, last_day, source, "
                                 "seq) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11)");
        insert.text(1, person).text(2, r.memory).text(3, r.subject).text(4, r.act).text(5, r.words).text(6, r.about);
        insert.integer(7, r.times).real(8, r.first_day).real(9, r.last_day).text(10, r.source).integer(11, r.seq).run();
        if (!same) out.kept.push_back(r);
    }
    brains::Statement(h, "DELETE FROM exchange_facts WHERE exchange IN (SELECT id FROM exchanges WHERE person = ?1)").text(1, person).run();
    brains::Statement(h, "DELETE FROM exchanges WHERE person = ?1").text(1, person).run();
    transaction.commit();
    out.cleared = log.size();
    ++acquaintance_changes_;
    inputs_.push_back(Compact{person, outcomes});
    return out;
}

QueryResult Director::query(std::string_view sql) const {
    mirror();
    return brains::read_only(brains_.handle(), sql);
}

void Director::mirror() const {
    const bool world = mirrored_world_ != world_changes_;
    const bool met = mirrored_acquaintances_ != acquaintance_changes_;
    if (!world && !met) return;
    Brains::Handle& h = brains_.handle();
    brains::Transaction transaction(h);
    if (world) {
        brains::Statement(h, "DELETE FROM people").run();
        for (const auto& [uid, one] : population().people) {
            brains::Statement insert(h, "INSERT INTO people (uid, name, kind, role, site, node, present) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)");
            insert.text(1, uid).text(2, one.name).text(3, one.kind).text(4, one.role).text_or_null(5, one.site).maybe(6, one.node);
            insert.integer(7, one.present ? 1 : 0).run();
        }
        brains::Statement(h, "DELETE FROM places").run();
        const auto place = [&](const std::string& id, const std::string& kind, const std::string& name, int node) {
            brains::Statement insert(h, "INSERT OR IGNORE INTO places (id, kind, name, node) VALUES (?1, ?2, ?3, ?4)");
            insert.text(1, id).text(2, kind).text(3, name).integer(4, node).run();
        };
        for (const fwg::SettlementSite& city : world_.settlements.sites) place(city.uid, "city", city.name, city.node);
        for (const std::vector<fwg::RuralSite>* sites : {&world_.humans.hamlets, &world_.humans.fortresses})
            for (const fwg::RuralSite& site : *sites)
                place(site.id, site.kind, knowledge::anchor_name(world_, site.id).value_or(site.kind), site.node);
        for (const fwg::KeyLocationSite& site : world_.key_locations.sites) place(site.id, site.kind, site.name, site.node);
        brains::Statement(h, "DELETE FROM work").run();
        for (const fwg::QuestRow& row : whole_board(world_)) {
            brains::Statement insert(h,
                                     "INSERT INTO work (quest_id, giver, verb, purpose, anchor, node, difficulty, state, offered_day, "
                                     "expires_day, closed_day, closed_reason) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)");
            insert.text(1, row.quest_id).text_or_null(2, row.giver_uid.value_or("")).text_or_null(3, row.verb.value_or(""));
            insert.text_or_null(4, row.stated_purpose.value_or("")).text_or_null(5, row.anchor.value_or(""));
            if (const fwg::HeroQuestHook* hook = hook_of(world_, row.quest_id)) {
                if (hook->has_target_node) insert.integer(6, hook->target_node);
                insert.integer(7, hook->difficulty);
            }
            insert.text(8, work_state(row)).real(9, row.offered_day).real(10, row.expires_day);
            if (row.closed_day) insert.real(11, *row.closed_day);
            if (row.closed_reason) insert.text(12, fwg::to_string(*row.closed_reason));
            insert.run();
        }
    }
    if (met) {
        brains::Statement(h, "DELETE FROM relationships").run();
        const Population& everyone = population();
        for (const auto& [uid, a] : acquainted_) {
            brains::Statement insert(h,
                                     "INSERT INTO relationships (person, conversations, talking, last_day, regard, mood, work, "
                                     "grievances, forgiven, asked_before, exposed, eased_day, days_met, exchanges, works_finished, "
                                     "standing) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16)");
            insert.text(1, uid).integer(2, a.conversations).integer(3, a.talking ? 1 : 0).real(4, a.last_day);
            insert.integer(5, a.disposition).text(6, mood_of(a.disposition)).text(7, a.work);
            insert.integer(8, static_cast<std::int64_t>(a.grievances.size())).integer(9, a.forgiven).text(10, a.asked_before);
            insert.integer(11, a.exposed ? 1 : 0);
            if (a.eased_day >= 0.0) insert.real(12, a.eased_day);
            int exchanges = 0;
            for (const auto& [kind, times] : a.heard) exchanges += times;
            const auto known = everyone.people.find(uid);
            insert.integer(13, a.days_met).integer(14, exchanges).integer(15, a.works_finished);
            insert.text(16, standing_of(a, known != everyone.people.end() && known->second.kind != "roster"));
            insert.run();
        }
    }
    transaction.commit();
    mirrored_world_ = world_changes_;
    mirrored_acquaintances_ = acquaintance_changes_;
}

namespace {

// One call for each kind of input. A kind with no call here stops the build, where a chain of
// get_ifs let a new kind fall through to the last branch and throw only on replay.
template <class... Calls>
struct EachInput : Calls... {
    using Calls::operator()...;
};
template <class... Calls>
EachInput(Calls...) -> EachInput<Calls...>;

}  // namespace

Director replay(fwg::World start, const std::vector<Input>& inputs) {
    Director director(std::move(start));
    for (const Input& input : inputs) {
        std::visit(EachInput{
                       [&](const Advance& advance) { director.advance(advance.days); },
                       [&](const Say& say) { (void)director.respond(say.speaker, say.act); },
                       [&](const Resolve& resolution) { director.resolve(resolution.quest_id, resolution.completed); },
                       [&](const Hold& held) { director.hold(held.person, held.minutes); },
                       [&](const Fright& fright) { director.fright(fright.person, fright.level); },
                       [&](const Gather& gathering) { director.gather(gathering.people); },
                       [&](const Overhear& overheard) { (void)director.overhear(overheard.people); },
                       [&](const Share& shared) { (void)director.share(shared.people); },
                       [&](const Report& report) { director.report_event(report.site, report.outcome, report.by); },
                       [&](const Words& said) { director.words(said.line, said.reply); },
                       [&](const Compact& compacted) { (void)director.compact(compacted.person, compacted.outcomes); },
                   },
                   input);
    }
    return director;
}

}  // namespace orchestrator
