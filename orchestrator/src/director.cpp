#include "orchestrator/director.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>

#include "fwg/api/quests.hpp"
#include "fwg/api/read.hpp"
#include "fwg/api/world_time.hpp"
#include "knowledge.hpp"

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
            // The distance is to the anchor's node, which is right whatever else stands there.
            for (const KnownPlace& place : places)
                if (place.node == hook->target_node) {
                    f.fields.emplace_back("distance", knowledge::distance_text(place.distance_m));
                    break;
                }
        }
    }
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

}  // namespace

Director::Director(fwg::World world) : world_(std::move(world)) {}

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
    inputs_.push_back(Advance{days});
}

FactPacket Director::respond(const std::string& speaker_uid, const DialogueAct& act) {
    if (act_needs_target(act.kind) && act.target.empty())
        throw Error("respond: " + std::string(to_string(act.kind)) + " needs a target");
    std::optional<Speaker> found = knowledge::speaker(world_, speaker_uid);
    if (!found) throw Error("respond: the world names no person '" + speaker_uid + "'");
    Speaker& s = *found;
    const auto recorded = [&](FactPacket p) {
        inputs_.push_back(Say{speaker_uid, act});
        return p;
    };

    if (!s.present) {
        FactPacket p = packet_for(s, act, Outcome::Refused);
        p.facts.push_back(reason("gone", (s.persona.name.empty() ? std::string("the one spoken to") : s.persona.name) + " is gone"));
        return recorded(std::move(p));
    }

    switch (act.kind) {
        case ActKind::NoAct:
            return recorded(packet_for(s, act, Outcome::NotUnderstood));

        case ActKind::Farewell:
            return recorded(packet_for(s, act, Outcome::Parted));

        case ActKind::AskSurroundings: {
            FactPacket p = packet_for(s, act, Outcome::Answered);
            const std::vector<KnownPlace> places = knowledge::places_in_reach(world_, s);
            p.facts = surroundings(places);
            for (Fact& danger : knowledge::dangers_near(world_, s)) p.facts.push_back(std::move(danger));
            return recorded(std::move(p));
        }

        case ActKind::AskAboutPlace: {
            const std::vector<KnownPlace> places = knowledge::places_in_reach(world_, s);
            for (const KnownPlace& place : places) {
                if (place.fact.subject != act.target) continue;
                FactPacket p = packet_for(s, act, Outcome::Answered);
                p.facts.push_back(place.fact);
                return recorded(std::move(p));
            }
            return recorded(packet_for(s, act, Outcome::Unknown));
        }

        case ActKind::AskAboutPerson: {
            const std::optional<knowledge::KnownPerson> person = knowledge::person_fact(world_, act.target);
            if (!person || !person->node) return recorded(packet_for(s, act, Outcome::Unknown));
            // Known when they stand where the speaker stands, or somewhere within the speaker's reach.
            bool known = s.node && *person->node == *s.node;
            if (!known)
                for (const KnownPlace& place : knowledge::places_in_reach(world_, s))
                    if (place.node == *person->node) {
                        known = true;
                        break;
                    }
            if (!known) return recorded(packet_for(s, act, Outcome::Unknown));
            FactPacket p = packet_for(s, act, Outcome::Answered);
            p.facts.push_back(person->fact);
            return recorded(std::move(p));
        }

        case ActKind::AskForWork: {
            FactPacket p = packet_for(s, act, Outcome::Answered);
            const std::vector<KnownPlace> places = knowledge::places_in_reach(world_, s);
            const double today = fwg::world_clock_of(world_).day;
            for (const Offer& offer : offers_known_to(world_, s)) p.facts.push_back(offer_fact(world_, offer, places, today));
            return recorded(std::move(p));
        }

        case ActKind::DeclineQuest: {
            for (const Offer& offer : offers_known_to(world_, s))
                if (offer.row.quest_id == act.target) return recorded(packet_for(s, act, Outcome::Declined));
            return recorded(packet_for(s, act, Outcome::Unknown));
        }

        case ActKind::AcceptQuest: {
            const std::vector<Offer> offers = offers_known_to(world_, s);
            const auto offer = std::find_if(offers.begin(), offers.end(), [&](const Offer& o) { return o.row.quest_id == act.target; });
            if (offer == offers.end()) return recorded(packet_for(s, act, Outcome::Unknown));
            if (!offer->own) {
                // Taking work is agreed with whoever gives it: send the player to them.
                FactPacket p = packet_for(s, act, Outcome::Refused);
                std::optional<knowledge::KnownPerson> giver;
                if (offer->row.giver_uid) giver = knowledge::person_fact(world_, *offer->row.giver_uid);
                p.facts.push_back(reason("not_theirs_to_give", "the work is " + whose(giver) + " to give"));
                if (giver) p.facts.push_back(giver->fact);
                return recorded(std::move(p));
            }
            fwg::QuestRequest request;
            request.actions.push_back(fwg::QuestAction{act.target, fwg::QuestActionKind::Take, std::nullopt});
            try {
                fwg::QuestRequestResult result = fwg::world_quest_request(world_, request);
                world_ = std::move(result.world);
            } catch (const fwg::WorldTimeError& refusal) {
                if (!refusal.refused_by_world()) throw;
                FactPacket p = packet_for(s, act, Outcome::Refused);
                p.facts.push_back(reason("refused_by_world", refusal.message()));
                return recorded(std::move(p));
            }
            FactPacket p = packet_for(s, act, Outcome::Accepted);
            p.facts.push_back(quest_state_fact(act.target, "taken"));
            return recorded(std::move(p));
        }

        case ActKind::AbandonQuest: {
            const std::optional<std::vector<fwg::QuestRow>> taken = board(world_, fwg::QuestState::Taken);
            if (!taken) return recorded(packet_for(s, act, Outcome::Unknown));
            const auto row = std::find_if(taken->begin(), taken->end(), [&](const fwg::QuestRow& q) { return q.quest_id == act.target; });
            if (row == taken->end()) return recorded(packet_for(s, act, Outcome::Unknown));
            if (!row->giver_uid || *row->giver_uid != s.uid) {
                FactPacket p = packet_for(s, act, Outcome::Refused);
                std::optional<knowledge::KnownPerson> giver;
                if (row->giver_uid) giver = knowledge::person_fact(world_, *row->giver_uid);
                p.facts.push_back(reason("not_theirs_to_release", "the work is " + whose(giver) + " to release"));
                if (giver) p.facts.push_back(giver->fact);
                return recorded(std::move(p));
            }
            fwg::QuestRequest request;
            request.actions.push_back(fwg::QuestAction{act.target, fwg::QuestActionKind::Abandon, std::nullopt});
            try {
                fwg::QuestRequestResult result = fwg::world_quest_request(world_, request);
                world_ = std::move(result.world);
            } catch (const fwg::WorldTimeError& refusal) {
                if (!refusal.refused_by_world()) throw;
                FactPacket p = packet_for(s, act, Outcome::Refused);
                p.facts.push_back(reason("refused_by_world", refusal.message()));
                return recorded(std::move(p));
            }
            FactPacket p = packet_for(s, act, Outcome::Abandoned);
            p.facts.push_back(quest_state_fact(act.target, "abandoned"));
            return recorded(std::move(p));
        }
    }
    throw Error("respond: an act outside the contract");
}

Director replay(fwg::World start, const std::vector<Input>& inputs) {
    Director director(std::move(start));
    for (const Input& input : inputs) {
        if (const Advance* advance = std::get_if<Advance>(&input)) {
            director.advance(advance->days);
        } else {
            const Say& say = std::get<Say>(input);
            (void)director.respond(say.speaker, say.act);
        }
    }
    return director;
}

}  // namespace orchestrator
