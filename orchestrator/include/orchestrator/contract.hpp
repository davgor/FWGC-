// The orchestrator's contract with the layer above it: a dialogue act in, a fact packet out.
//
// Nothing here names the generator. The communicator includes this header and nothing else of
// the orchestrator, so it phrases what the director decided without seeing a world: every
// fact in a packet is text the director has already chosen to disclose, and the persona is
// the face a stranger sees.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace orchestrator {

// 2: Persona::voice_key, and a Reason fact's `why`.
// 3: how far a thing is, as a rough `range` in place of a `distance` in kilometres; an exact
//    `marker` and `marker_node` on offered and taken work, for the game's map.
// 4: conversation. The acts greet, small_talk, answer, thank, insult, ask_safety and confront,
//    and their outcomes; a Memory fact in every packet; a Chat fact in small talk; a Safety
//    fact answering ask_safety; a Reveal fact when a hidden face comes out; Persona::face.
inline constexpr int kContractVersion = 4;

// What the player attempts, as the interpreter classifies it. A closed set: anything the
// interpreter cannot place is NoAct, and the speaker answers in character without acting.
enum class ActKind : int {
    AskSurroundings = 0,  // what stands around here
    AskAboutPlace,        // target: a place id
    AskAboutPerson,       // target: a person uid
    AskForWork,           // the offers this speaker makes or knows of
    AcceptQuest,          // target: a quest id
    DeclineQuest,         // target: a quest id
    AbandonQuest,         // target: a quest id
    Farewell,
    NoAct,
    // 4: conversation.
    Greet,      // hello, good day
    SmallTalk,  // how are you, how is the work, the weather: talk for its own sake
    Answer,     // the player answers something the speaker asked
    Thank,
    Insult,     // rudeness, a threat
    AskSafety,  // is it safe, what should I fear
    Confront,   // the player tells the speaker they are not what they seem
};
inline constexpr int kActCount = static_cast<int>(ActKind::Confront) + 1;

std::string_view to_string(ActKind kind);
std::optional<ActKind> act_kind(std::string_view name);  // nullopt for a name not in the set
bool act_needs_target(ActKind kind);

// Whether the director changes the world when it grants this act: taking work, or giving it up.
// A game confirms such an act with the player before handing it to the director, so a misread
// line costs a question, never a quest.
bool act_changes_world(ActKind kind);

struct DialogueAct {
    ActKind kind = ActKind::NoAct;
    std::string target;  // empty unless act_needs_target(kind)
};

enum class Outcome : int {
    Answered = 0,   // the facts are the answer
    Accepted,       // the quest is now the player's
    Declined,       // nothing changed
    Abandoned,      // the player gave the quest up
    Unknown,        // the speaker knows nothing of the target
    Refused,        // the world would not have it; a Reason fact says why, in `reason`, and
                    // what kind of why, in `why`: "not_theirs_to_give", "not_theirs_to_release",
                    // "gone" (the speaker), or "refused_by_world"
    Parted,         // farewell
    NotUnderstood,  // NoAct: answer in character, act on nothing
    // 4: conversation. Nothing here changes the world; each changes what the speaker
    // remembers (the Memory fact).
    Greeted,       // a greeting returned
    Chatted,       // small talk; a Chat fact holds what there is to talk of
    Acknowledged,  // the player answered the speaker
    Thanked,
    Offended,      // an insult taken; the speaker thinks less of the player
    Exposed,       // the speaker owns to a hidden face, and a Reveal fact names it. A mask's true
                   // face is the persona from then on; a beast's or a fractured heart's other face
                   // still shows only on its own days
    Denied,        // confronted without cause, or with nothing to hide: the speaker denies it
};
inline constexpr int kOutcomeCount = static_cast<int>(Outcome::Denied) + 1;

std::string_view to_string(Outcome outcome);

// Memory: what the speaker and the player have between them. Chat: what small talk can be
// about. Safety: a verdict on the danger about. Reveal: what a face that came out had hidden.
enum class FactKind : int { Place = 0, Person, QuestOffer, QuestState, Danger, Reason, Memory, Chat, Safety, Reveal };

std::string_view to_string(FactKind kind);

// One thing the speaker may say. `subject` is the world id it is about -- a place id, a
// person uid, a quest id, a nest id -- so every fact traces back to the record it came from.
// `fields` are rendered text, in the order the director wrote them.
//
// How far a place, a nest or a piece of work is comes as `range`, never as a measure: nobody
// standing there has measured it. The bands: "here" (under 50 m), "close" (under 1 km), "near"
// (under 3 km), "walk" (under 7 km), "far" (under 15 km), "distant".
//
// Offered work, and work once taken, also carry `marker` (the anchor's id) and `marker_node`
// (the anchor's terrain node): where the game marks it on its map, exactly. They are for the
// game, not for saying: no voice speaks them.
//
// Every packet carries one Memory fact, its subject the speaker, folded from the session's
// inputs (so replay restores it):
// - `met`: "first" in the first conversation with this speaker, else "again";
// - `times`: how many conversations came before this one, as digits;
// - `since`: how long since the last one ended, roughly: "today", "days", "weeks", "months",
//   "years"; empty the first time;
// - `mood`: how the speaker takes the player: "warm", "friendly", "neutral", "wary", "hostile".
//   Thanks and finished work raise it; insults and abandoned work lower it;
// - `work`: the player's latest dealing with the speaker's own work: "offered", "taken",
//   "declined", "abandoned", "finished", "failed"; empty when there is none;
// - `asked_before`: the place or person the player last asked this speaker about, in an earlier
//   conversation, by name; empty when there is none;
// - `turn`: this act's place in the conversation, 1 for the first, as digits;
// - `repeat`: "yes" when the player has made this same act, with the same target, earlier in
//   this conversation, else "no";
// - `exposed`: "yes" once the player has made this speaker own to a hidden face, else "no".
// A conversation ends at a farewell, when time passes, or when the player turns to someone
// else. A face that does not recognise the player (a beast unleashed) gives a first meeting and
// a hostile mood.
//
// A Person fact carries `hides`, "yes", when the speaker knows that person is not what they
// seem: a rival, kin or ally of a hero whose public face is a mask. It is how a player learns a
// mask is one.
//
// A Chat fact: `weather` ("clear", "fair", "unsettled", "stormy", "a great storm"), `wind`
// ("still", "breezy", "windy", "a gale"), `season` ("deep winter", "winter", "the turn of the
// season", "summer", "high summer"), `time` ("dawn", "morning", "midday", "afternoon",
// "evening", "night"). The worst danger near the speaker comes as a Danger fact beside it.
//
// A Safety fact: `verdict`, from the dangers near the speaker, which come beside it as Place
// facts (up to three places of threat 4 or more: those within a walk first, each group worst
// then nearest first, so the verdict's reasons are always named) and Danger facts (the nests
// near, nearest first):
// - "unsafe": a place of threat 5 within a walk (range "here", "close", "near" or "walk"), or a
//   nest of danger 4 or more;
// - "wary": otherwise, a place of threat 4 within a walk, or a nest of danger 3;
// - "safe": none of those. A dangerous place further off is still named.
//
// A Reveal fact, its subject the speaker: `face`, the hidden face now out ("true", "unleashed",
// "second"); `was`, the role the face the player knew claimed; `is`, the role of the face now
// out. Only a mask's two roles differ: a beast's or a fractured heart's faces share one role,
// and differ in manner and alignment. A mask exposed shows its true face from then on; a beast
// or a fractured heart owns to its other face, which shows on its own days.
struct Fact {
    FactKind kind = FactKind::Place;
    std::string subject;
    std::vector<std::pair<std::string, std::string>> fields;

    // The value of a field, or nullptr.
    const std::string* field(std::string_view key) const;
};

// How the speaker presents: the face the player sees. That is the face a stranger sees, unless
// a hidden one is out: a mask's true face once the player exposed it; a beast's unleashed face
// on the full moon's day and the day either side; a fractured heart's second face, which has
// the upper hand every other day.
struct Persona {
    std::string uid;
    std::string name;
    std::string role;   // the apparent role, or the roster post
    std::string place;  // where they stand, by name; empty when the world does not say
    // A card's voice, for a speaker who has one; empty for a roster NPC, whom the voice plays
    // from name, role and place.
    std::string manner, voice, toward_player, line;
    std::vector<std::string> never;  // lines the voice must not cross
    // The written lines the speaker speaks from: a hero speaks as their archetype and the
    // alignment of the face shown ("warden/LN", "deceiver/LE" once exposed). Empty for a mask
    // still worn (a deceiver's or a false hero's public face), a dread and a roster NPC, who
    // speak from their post's lines. Nothing it names is kept back from the player.
    std::string voice_key;
    // Which face is shown, when it is not the one a stranger sees: "true", "unleashed" or
    // "second". Empty otherwise, so a mask never says there is something behind it.
    std::string face;
};

struct FactPacket {
    int contract_version = kContractVersion;
    std::string speaker;  // uid
    ActKind answering = ActKind::NoAct;
    Outcome outcome = Outcome::NotUnderstood;
    Persona persona;
    std::vector<Fact> facts;
};

}  // namespace orchestrator
