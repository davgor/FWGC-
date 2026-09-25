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
inline constexpr int kContractVersion = 2;

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
};

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
};

std::string_view to_string(Outcome outcome);

enum class FactKind : int { Place = 0, Person, QuestOffer, QuestState, Danger, Reason };

std::string_view to_string(FactKind kind);

// One thing the speaker may say. `subject` is the world id it is about -- a place id, a
// person uid, a quest id, a nest id -- so every fact traces back to the record it came from.
// `fields` are rendered text, in the order the director wrote them.
struct Fact {
    FactKind kind = FactKind::Place;
    std::string subject;
    std::vector<std::pair<std::string, std::string>> fields;

    // The value of a field, or nullptr.
    const std::string* field(std::string_view key) const;
};

// How the speaker presents: always the face a stranger sees, never a hidden one.
struct Persona {
    std::string uid;
    std::string name;
    std::string role;   // the apparent role, or the roster post
    std::string place;  // where they stand, by name; empty when the world does not say
    // A card's voice, for a speaker who has one; empty for a roster NPC, whom the voice plays
    // from name, role and place.
    std::string manner, voice, toward_player, line;
    std::vector<std::string> never;  // lines the voice must not cross
    // The written lines the speaker speaks from, where their public face names them: a hero
    // with one face speaks as their archetype and alignment ("warden/LN"). Empty for anyone
    // else: a face that hides another, a dread, a roster NPC, who speak from their post's lines.
    // Nothing it names is kept back from a stranger.
    std::string voice_key;
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
