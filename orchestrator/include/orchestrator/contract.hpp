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
// 5: Persona::people, whose sayings the speaker knows.
// 6: the Memory fact's `heard`, and `since` by the calendar, with "yesterday".
// 7: the Memory fact's `asked_before` is what was asked in the last conversation only.
// 8: the act apologize and its outcomes, forgiven and unwronged; an answer about a place says who
//    is there, what nests near it and what work is there.
// 9: feelings. The act calm and its outcomes calmed, still_afraid and not_afraid; the Memory
//    fact's `feeling` and `resting`.
// 10: overhearing. The Memory fact's `eased` and `joined`.
// 11: news. A News fact for an event the speaker has heard of, in greetings, small talk and answers
//     about a place or a person.
// 12: the short log and long memory. A Recall fact for what the speaker remembers of the stranger
//     from before; the Memory fact's `standing`.
// 13: questions about the speaker. The act ask_about_them, answered with a Self fact, Deed facts and
//     Person facts that carry `tie`; an answer to ask_for_work names the speaker's own work and at
//     most two others', one errand from two givers once.
// 14: the Memory fact's `beside`: "yes" when the speaker's own work the stranger last dealt with is
//     at the place the speaker stands (work "with me", done beside them).
// 15: an answer to ask_for_work gives another giver's offer `giver_tie` ("rival", "kin", "ally"; "liege" since 19)
//     when that giver is tied to the speaker.
// 16: a speaker with work of their own names only theirs the first time they are asked for work in
//     a conversation, and others' too when asked again.
// 17: the Memory fact's `work_news`: "yes" in the first conversation to begin after the speaker's own
//     work was finished or failed, when what came of it is news between them.
// 18: the act complain and its outcome heard: a complaint about the speaker or their work, which
//     costs no regard and leaves no grievance, where an insult does both.
// 19: a castellan's past (the generator's heroes version 3): the Self fact's `held_years`,
//     `served_at` and `served_as`, a Person fact marked `tie` "liege" for the sovereign they are
//     sworn to, and Deed facts for the wars their city fought in their lifetime.
// 20: an Account fact: how the speaker's own work went, as the game reported it (Director::account):
//     what came, how many, what it cost, and the stranger's part.
// 21: the Recall memory "question": a question the speaker asked the stranger that the stranger never
//     answered, its words as the speaker asked it; a greeting carries it once, in a later
//     conversation.
// 22: a war Deed fact's `against`: the city fought, by name.
// 23: an apology carries the stranger's latest words of this conversation, a Recall fact "said".
// 24: the player's name: the act introduce and its outcomes introduced, known and renamed; the Memory
//     fact's `called`, `called_how` and `called_other`; a News fact of the player's deed carries `called`.
// 25: fame. A speaker knows their realm's sovereign and heroes whose known deed's word reaches them; an answer
//     about a person carries their `fame`, `sovereign` when they are the speaker's own, and a Deed fact of the
//     deed they are known for, with its `scope`; ask_about_person's target may be "=" and the player's words.
// 26: heroes' deeds. A News fact of event "hero": what a hero did at the director's call, how it went, and
//     against what.
// 27: a hero's own deed rides in their packets only when newer than their last meeting with the stranger (any,
//     at a first); a hero whose own work the player holds does not act until it is resolved.
// 28: a News fact of another hero's deed carries their `tie` to the speaker ("sovereign", "ally", "kin",
//     "rival"); heroes who fought one fight know each other's part of it by being there (`how` "there").
// 29: a hero's own deeds since their last meeting with the stranger ride in their packets, up to three, newest first
//     (at a first meeting, the newest); heroes defending against one foe on one call fight one fight, wherever each
//     of them stands.
inline constexpr int kContractVersion = 29;

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
    // 8.
    Apologize,  // the player says sorry
    // 9.
    Calm,  // it's all right, you're safe now: the player soothes a frightened speaker
    // 13.
    AskAboutThem,  // the speaker themself, beyond their name: how long, why, what they are after, what
                   // they did or lost, who stands with them, whether they would leave
    // 18.
    Complain,  // a complaint about the speaker or their work: you could have warned me, it was worse
               // than you said
    // 24.
    Introduce,  // target: the name the player gives, as they said it (not an id): perhaps a lie
};
inline constexpr int kActCount = static_cast<int>(ActKind::Introduce) + 1;

std::string_view to_string(ActKind kind);
std::optional<ActKind> act_kind(std::string_view name);  // nullopt for a name not in the set
bool act_needs_target(ActKind kind);

// Whether the director changes the world when it grants this act: taking work, or giving it up.
// A game confirms such an act with the player before handing it to the director, so a misread
// line costs a question, never a quest.
bool act_changes_world(ActKind kind);

struct DialogueAct {
    ActKind kind = ActKind::NoAct;
    // Empty unless act_needs_target(kind); an id, but the name the player gives for introduce, and for
    // ask_about_person, an id or "=" and the words the player named someone by (contract version 25: "=the
    // king", "=Aldomir"), which the director looks up among the people the speaker knows.
    std::string target;
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
    // 8: an apology.
    Forgiven,      // the speaker held a grievance (an insult, their own work given up) and one eases:
                   // their regard gives back what it cost; or every one was forgiven before, and the
                   // apology is taken again, moving nothing
    Unwronged,     // there is nothing to forgive: the player never wronged the speaker, or the face
                   // speaking does not know them
    // 9: feelings.
    Calmed,       // the speaker was afraid, and the player's words ease them a step
    StillAfraid,  // afraid, and not eased: what frightens them is still there, or they do not trust
                  // the one soothing them
    NotAfraid,    // nothing to ease: the speaker feels as they do at rest
    // 18: a complaint.
    Heard,  // the speaker hears it out, and thinks no less of the player for it. From a speaker cold
            // to the player, or a face that does not know them, a complaint is Offended, as an insult
    // 24: the player's name.
    Introduced,  // a name the speaker did not know them by: they know them by it now
    Known,       // the name the speaker already knew them by
    Renamed,     // another name than the one the speaker knew them by: they know them by the new one, and
                 // keep the old as the Memory fact's `called_other`
};
inline constexpr int kOutcomeCount = static_cast<int>(Outcome::Renamed) + 1;

std::string_view to_string(Outcome outcome);

// Memory: what the speaker and the player have between them, and how the speaker feels. Chat:
// what small talk can be about. Safety: a verdict on the danger about. Reveal: what a face that came out had hidden.
// News: something that happened, as the speaker heard of it. Recall: something the speaker
// remembers of the stranger from before.
enum class FactKind : int {
    Place = 0,
    Person,
    QuestOffer,
    QuestState,
    Danger,
    Reason,
    Memory,
    Chat,
    Safety,
    Reveal,
    News,
    Recall,
    Self,     // 13
    Deed,     // 13
    Account,  // 20
};
inline constexpr int kFactKindCount = static_cast<int>(FactKind::Account) + 1;

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
// - `since`: how long since the last one ended, by the calendar: "today" (the same day),
//   "yesterday", "days" (under a week), "weeks" (under 60 days), "months" (under two years),
//   "years"; empty the first time;
// - `mood`: how the speaker takes the player: "warm", "friendly", "neutral", "wary", "hostile".
//   Thanks and finished work raise it; insults and abandoned work lower it, a complaint does not;
// - `work`: the player's latest dealing with the speaker's own work: "offered", "taken",
//   "declined", "abandoned", "finished", "failed"; empty when there is none;
// - `asked_before`: the place or person the player last asked this speaker about in their last
//   conversation, by name; empty when they asked about none then;
// - `turn`: this act's place in the conversation, 1 for the first, as digits;
// - `repeat`: "yes" when the player has made this same act, with the same target, earlier in
//   this conversation, else "no";
// - `heard`: how many times before this one the player has made this act to this speaker,
//   whatever its target, in this conversation or any earlier one, as digits: "0" the first time;
// - `exposed`: "yes" once the player has made this speaker own to a hidden face, else "no";
// - `feeling`: how the speaker feels as they reply, on the fear ladder: "calm", "uneasy",
//   "afraid", "panicked". The game shows it as their expression. Fear rises with what the game
//   reports frightening them, spreads through people together, and falls a step toward rest for
//   every half hour nothing new frightens them, or when the player calms them;
// - `resting`: how they feel when nothing frightens them: "uneasy" where a nest of danger 4 or
//   more lies within a kilometre of where they stand, else "calm";
// - `eased`: "yes" when the player's calming words eased the speaker within the last two days,
//   else "no";
// - `joined`: "yes" on the packet of a bystander who overheard the player and joined in unasked,
//   answering what the one the player spoke to could not; else "no";
// - `standing`: how far the stranger and the speaker have come, never falling: "stranger" before
//   they first spoke, "acquainted" after, "regular" once they have talked on three different days,
//   "favourite" at ten days and thirty exchanges, the most a roster person comes to; "hero" for a
//   hero, a dread or a villain, whatever came before;
// - `called` (contract version 24): the name the speaker knows the player by, as it was given, perhaps a
//   lie; empty when they know none. `called_how`: "told" (the player told them), "word" (it came with
//   word of the player's deed), "overheard" (they heard the player give it to someone else).
//   `called_other`: another name they also know the player by, when two differ; empty otherwise.
// A conversation ends at a farewell, when time passes, or when the player turns to someone
// else. A face that does not recognise the player (a beast unleashed) gives a first meeting and
// a hostile mood.
//
// An answer about a place (ask_about_place) says what the speaker knows of it, beyond the place:
// - the Place fact's `trades`: up to three of a settlement's roster posts, by role, the roster's
//   own first, joined by ", " ("innkeeper, smith, healer"); absent for a place with no roster;
// - a Person fact for each hero who holds a post there (up to two, never the speaker), `where`
//   naming the place;
// - a Danger fact for each nest within 2 km of the place (up to two, the worst first), marked
//   `around` "yes", its `range` from the place, not from the speaker;
// - a QuestOffer fact for each piece of work the speaker knows of (as ask_for_work would give
//   it) that is anchored at the place.
//
// A Person fact carries `hides`, "yes", when the speaker knows that person is not what they
// seem: a rival, kin or ally of a hero whose public face is a mask. It is how a player learns a
// mask is one.
//
// Fame (contract version 25). A speaker knows, never having met them, the sovereign of their own
// realm and a hero whose known deed's word reaches them: through the hero's realm to anyone of it,
// and through a region, or past a realm's borders, to anyone within a region's width of where the
// hero stands. An answer about a person who is a hero with one face carries in the Person fact:
// - `fame`: their band, "notable", "renowned" or "legendary";
// - `sovereign`: "yes" when they are the sovereign of the speaker's own realm;
// and after it a Deed fact of the deed they are known for (the Self answer's fields), with its
// `scope`: how far word of it went, "local", "regional" or "realm". Asked by the words the
// player said ("=the king"), the director looks them up: words that ask for a sovereign ("king",
// "queen", "sovereign", "monarch", "ruler", "emperor", "empress", "rules") name the sovereign of a
// realm they name by its own name or a city's ("=the queen of Linbrig"), else, when they are a title
// and nothing more, the speaker's own; other words, and a title with a name ("=King Harwin"), the
// first hero and then roster person whose name's every word they hold, case aside, whom the speaker
// knows. None, and the answer is Unknown; the act is recorded as looked up.
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
//
// A News fact, its subject the event's id: something that happened, at the state the speaker knows
// it by, which may be older than its latest.
// - `event`: what kind of thing: "work" (a piece of work on the board), "exposed" (someone's hidden
//   face brought out), "scare" (frights at a place, and how the threat ended), "hero" (what a hero did,
//   contract version 26);
// - `state`: work "asked", "taken", "finished", "failed", "given_up", "lapsed", "withdrawn",
//   "giver_gone", "target_gone"; exposed "exposed"; scare "frightened", "calmed", "beaten", "lost",
//   "passed"; hero "won", "lost";
// - `place`: where, by name; absent where no place is named;
// - `verb`, `purpose`, `giver`: work only: what the giver asks, why they say they ask it (never
//   what it is really for), and who gives it, by name;
// - `person`, `was`, `is`: exposed only: whom, by the name a stranger knew them by; the role the
//   face they showed claimed, and the role of the face now out;
// - `person`, `verb`, `against` (contract version 26): hero only: the hero, by the name a stranger knew
//   them by; what they did ("defended" a place, "hunted" a nest, "assaulted" a ruin); what they faced, by
//   name ("the glacier dragons"), absent when nothing stood against them;
// - `player`: "yes" when the player brought this state about, else "no";
// - `by`: a scare's end: "player", or the name of whoever else brought it about; absent otherwise;
// - `since`: when this state came about, in the Memory fact's calendar bands;
// - `how`: "there" when the speaker saw it or it happened to them, "word" when they heard it;
// - `called` (contract version 24): of work the player finished, failed, gave up or took, the name its
//   giver knew the player by, which travels with word of it; absent when the giver knew none.
// A greeting carries the newest event whose state the speaker knows the player brought about; small
// talk the newest they know of at the place they stand; an answer about a place up to two about it;
// an answer about a person one about them. At most those, and none the speaker has not heard. A hero's
// greeting, small talk and answer about themself carry their own newest deed too (contract version 26):
// their side of it.
//
// A Recall fact, its subject what is remembered (a place id, a person uid, a quest id; the speaker's
// own uid for a memory of no one thing): something the speaker remembers of the stranger from
// before this exchange, from their conversation log since they were last compacted and from their
// long memory (what compaction kept). Only what bears on the act:
// - `memory`: "asked" (the stranger asked the speaker about it), "told" (the speaker told the
//   stranger of it, when asked of it, or work they told of), "did" (what the stranger did: took,
//   finished, failed or gave up the speaker's
//   work; thanked, insulted, apologised; talked them down), "open" (the speaker's work the stranger
//   holds and has not done), "said" (the stranger's own words, kept word for word), "last" (what
//   they last spoke of, in an earlier conversation), "question" (contract version 21: a question the
//   speaker asked the stranger in an earlier conversation that the stranger's next line did not
//   answer, `words` as the speaker asked it);
// - `act`: what was done, by the contract's names: an act ("ask_about_place"), an outcome ("taken",
//   "finished", "thanked"), or "said";
// - `about`: what it concerns, by name as the speaker says it; empty for a memory of no one thing;
// - `times`: how many times, as digits;
// - `since`: when it last was, in the Memory fact's calendar bands; `first`: when it first was;
// - `words`: the stranger's words for "said", and for "last" what the speaker kept of it, when a
//   summary handed to the compaction held it and C++ checked it; the speaker's own question for
//   "question"; empty otherwise.
// A greeting carries what they last spoke of, the work the stranger holds, the stranger's latest kept
// words, and the latest question of theirs the stranger never answered, until a greeting has carried
// it once (at most one each); an answer about a place or a person what was asked and told of it
// before; asking for work what came of the speaker's work before and what is open; an apology the
// stranger's latest words in this conversation, their last answer to the speaker, as "said" (contract
// version 23): "Sorry, I'm not myself today" two lines after a brother's death is heard in its light.
//
// A Self fact, its subject the speaker: what they would say of themself when asked (ask_about_them,
// contract version 13). Public only: never a card's belief, wants, fears, tells or lies; and only for
// the face shown, so a mask still worn gives its role and where it stands, nothing more. Fields where
// the world has them:
// - `role`: what they are, a hero's role or a roster post; `at`: where they stand, by name;
// - `known_as`: a hero's name beyond their own ("the Castellan", "Wyrmward", "of the Order");
// - `fame`: a hero's fame, "notable", "renowned" or "legendary";
// - `home`: the city they call home, by name;
// - `stands`: how a hero stands where they are: "council", "court", "reeve", "quay", "warding",
//   "throne", "garrison", "altar", "refugee", "captive", "command", "hidden", "cult";
// - `after`, `after_what`: what a hero is after, as a verb ("hold", "exploit", "retake",
//   "protect", "kill", "convert") and its object by name;
// - `lost`, `lost_to`, `lost_when`: a city they lost, by name; what took it: "war", "dragon",
//   "monster", "magic" (their own), "radiance"; and when;
// - `since`: when they were born;
// - `held_years`, `served_at`, `served_as` (contract version 19): a castellan's years at their post, as
//   digits; where they served before it, by name; and as what: "soldier", "sergeant", or "second" to
//   the castellan there.
// `lost_when`, `since` and a Deed's `when` are "this age", "the last age" or "ages ago", against the
// world's latest age.
// A Deed fact, its subject the event's id: a part they had in something that happened, from the
// world's record of them, newest first, at most three:
// - `deed`: "ruin", "war", "nest", "founding", "hamlet", "fortress", "port", "shrine";
// - `part`: "heir", "actual", "keeper", "credited", "victim";
// - `place`: where, by name, when the world names it;
// - `war`: a war's reach, "international", "regional" or "civil"; war deeds only;
// - `against`: the city fought, by name ("the ruins of" one since fallen); war deeds only (contract
//   version 22);
// - `when`.
// Asked about themself, a speaker answers with a Self fact, their Deed facts, and a Person fact for
// the sovereign they are sworn to and each of their rivals, kin and allies (at most three, in that
// order), marked `tie`: "liege" (contract version 19), "rival", "kin", "ally"; and the worst nest
// near where they stand, a Danger fact, as small talk has it: what they stand against. Nothing of
// their work: what they would say of that is asked for (ask_for_work).
//
// An Account fact, its subject the quest id (contract version 20): how the speaker's own work went,
// the last of theirs the stranger finished or failed, as the game reported it (Director::account).
// Every field is optional; the game says what it knows:
// - `outcome`: "finished" or "failed";
// - `came`: what came, by name: a nest's creature ("Abyssal krakens"), or a person's name;
// - `came_as`: "nest" or "person";
// - `many`: how many came: "one", "few", "many", "horde";
// - `cost`: what it cost those who stood there: "none", "few", "many" lost;
// - `part`: the stranger's part: "fought", "led", "turned" (turned the fight), "held" (held when it
//   counted), "hurt" (took a wound).
// A greeting and small talk carry it, while the speaker's latest dealing with the stranger is that
// work's end.
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
    // The speaker's people, by the world's id ("human_cold", "hill_dwarf"): whose sayings they
    // know. Empty when the world does not say (a dread, a villain).
    std::string people;
};

struct FactPacket {
    int contract_version = kContractVersion;
    std::string speaker;  // uid
    ActKind answering = ActKind::NoAct;
    Outcome outcome = Outcome::NotUnderstood;
    Persona persona;
    std::vector<Fact> facts;
};

// An outcome or a fact kind by its name, or nullopt for a name not in the set.
std::optional<Outcome> outcome_named(std::string_view name);
std::optional<FactKind> fact_kind_named(std::string_view name);

// A packet as text, for passing packets between tools -- the orchestrator's session tool writes
// them and the communicator's eval reads them -- one field to a line, from `packet <version>` to
// `end`. Every value has its backslashes, newlines and carriage returns escaped, so any text
// comes back whole; a field's key and a fact's subject hold no space.
std::string packet_text(const FactPacket& packet);

// Every packet in `text`, in order; lines outside a packet are left alone. Throws
// std::invalid_argument, naming the line, for a packet it cannot read.
std::vector<FactPacket> packets_from_text(std::string_view text);

}  // namespace orchestrator
